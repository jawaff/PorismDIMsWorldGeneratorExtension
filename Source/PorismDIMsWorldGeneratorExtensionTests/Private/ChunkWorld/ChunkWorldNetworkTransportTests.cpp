// Copyright 2026 Spotted Loaf Studio
#include "ChunkWorldExtended/ChunkWorldNetworkSubsystem.h"
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
