// Copyright 2026 Spotted Loaf Studio
#include "ChunkWorldExtended/ChunkWorldNetworkSubsystem.h"
#include "ChunkWorldExtended/ChunkWorldNetManager.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace
{
    class FPartialTransport final : public IChunkWorldNetTransport
    {
    public:
        explicit FPartialTransport(int32& InLive) : Live(InLive) { ++Live; }
        ~FPartialTransport() override { --Live; }
        void Close() override { bConnected = false; }
        bool IsConnected() const override { return bConnected; }
        bool WaitForData(float) override { return false; }
        bool Recv(uint8*, int32, int32& Count) override { Count = 0; return false; }
        bool Send(const uint8* Data, int32 Size, int32& Count) override
        {
            Count = FMath::Min(Size, MaxWrite);
            Written.Append(Data, Count);
            return bConnected;
        }
        int32 MaxWrite = 2;
        TArray<uint8> Written;
    private:
        int32& Live;
        bool bConnected = true;
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldNetworkTransportTest, "PorismExtension.ChunkWorld.Network.Transport",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FChunkWorldNetworkTransportTest::RunTest(const FString&)
{
    int32 Live = 0;
    {
        FPartialTransport Stream(Live);
        const TArray<uint8> Expected = {1, 2, 3, 4, 5, 6, 7};
        TestTrue(TEXT("Partial writes complete one field"), Stream.SendAll(Expected.GetData(), Expected.Num()));
        TestTrue(TEXT("Stream retains exact field bytes"), Stream.Written == Expected);
        Stream.MaxWrite = 0;
        TestFalse(TEXT("Zero progress fails rather than spinning"), Stream.SendAll(Expected.GetData(), Expected.Num()));
        Stream.Close();
        TestFalse(TEXT("Closed transport fails"), Stream.SendAll(Expected.GetData(), Expected.Num()));
    }
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    auto* Network = World->GetSubsystem<UChunkWorldNetworkSubsystem>();
    for (int32 Index = 0; Index < 128; ++Index)
        Network->Accept(MakeUnique<FPartialTransport>(Live));
    TestEqual(TEXT("Limit includes sockets queued before the game-thread tick"), Live, 64);
    Network->Tick(0);
    TestEqual(TEXT("Tick retains only bounded waiting handshakes"), Live, 64);
    World->DestroyWorld(false);
    TestEqual(TEXT("Shutdown releases queued and active handshakes"), Live, 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldRequestedSaveSectionsTest, "PorismExtension.ChunkWorld.Network.RequestedSaveSections",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FChunkWorldRequestedSaveSectionsTest::RunTest(const FString&)
{
    int32 Live = 0;
    FWorldContext* Context = nullptr;
    UWorld* World = ChunkWorldSpawnTest::CreateWorld(Context);
    auto* Terrain = World->SpawnActor<AChunkWorldExtended>();
    auto* Manager = World->SpawnActor<AChunkWorldNetManager>();
    ON_SCOPE_EXIT
    {
        Manager->CleanupTcpSocket();
        FLayoutTestWorldSupport::ShutdownTransientChunkWorld(Terrain);
        ChunkWorldSpawnTest::DestroyWorld(World, Context);
    };
    FLayoutTestWorldSupport::InitializeTransientChunkWorld(Terrain);
    const FIntVector Cell(2, 3, 4);
    Terrain->SetBlockValueByBlockWorldPos(Cell, SinfullMaterial, false);
    const auto* Layer = Terrain->PrimLayer;
    const FIntVector Grid = Terrain->BlockWorldPosToChunkGridPos(Cell, Layer);
    auto Capture = MakeUnique<FPartialTransport>(Live);
    auto& Written = Capture->Written;
    Capture->MaxWrite = MAX_int32;
    Manager->Transport = MoveTemp(Capture);
    Manager->bTcpConnected = true;
    const int32 WorldId = Manager->RegisterWorld(Terrain);
    auto CheckReply = [&](bool bExpectMesh)
    {
        TArray<int64> Material, Mesh, Transforms;
        TestTrue(TEXT("Host has a material save"), Terrain->DriveSave->JustReadToArray(Material, Layer->BlockBufferSize,
            Grid.X, Grid.Y, Grid.Z, Layer->BlockFactor));
        TestEqual(TEXT("Host mesh indices present only after placement"), Terrain->MeshSave->JustReadToArray(Mesh,
            Layer->BlockBufferSize, Grid.X, Grid.Y, Grid.Z, Layer->BlockFactor), bExpectMesh);
        TestEqual(TEXT("Host transforms present only after placement"), Terrain->MeshSave->JustReadToArray(Transforms,
            Layer->BlockBufferSize, Grid.X, Grid.Y, Grid.Z, Layer->BlockFactor, true), bExpectMesh);
        TArray<int64> Expected;
        for (const auto* Section : {&Material, &Mesh, &Transforms})
        {
            Expected.Add(Section->Num());
            Expected.Append(*Section);
        }
        // Inspect the locally produced reply; no receiver or client mesh read needed.
        Written.Reset();
        Manager->ReceiveChunkSaveDataRequests_Implementation(WorldId, {Grid}, {0});
        constexpr int32 HeaderBytes = 3 * sizeof(int32);
        AddInfo(FString::Printf(TEXT("Host material/mesh/transform words=%d/%d/%d; reply payload words=%d, expected=%d."),
            Material.Num(), Mesh.Num(), Transforms.Num(), (Written.Num() - HeaderBytes) / int32(sizeof(int64)), Expected.Num()));
        TestTrue(bExpectMesh ? TEXT("Populated mesh sections retain exact payloads") : TEXT("Material-only chunk sends two empty mesh sections"),
            Written.Num() == HeaderBytes + Expected.Num() * sizeof(int64)
            && FMemory::Memcmp(Written.GetData() + HeaderBytes, Expected.GetData(), Expected.Num() * sizeof(int64)) == 0);
    };
    CheckReply(false);
    FMeshData PlacedMesh;
    PlacedMesh.MeshId = 0;
    PlacedMesh.Flag = 42;
    Terrain->SetMeshDataByBlockWorldPos(Cell, PlacedMesh, false);
    CheckReply(true);
    return true;
}
