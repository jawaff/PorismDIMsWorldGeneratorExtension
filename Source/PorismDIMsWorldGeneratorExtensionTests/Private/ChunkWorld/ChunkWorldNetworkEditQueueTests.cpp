// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "ChunkWorldExtended/ChunkWorldNetManager.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldQueuedEditsTest, "PorismExtension.ChunkWorld.Network.QueuedEdits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldQueuedEditsTest::RunTest(const FString&)
{
	FWorldContext* Context = nullptr;
	UWorld* World = ChunkWorldSpawnTest::CreateWorld(Context);
	auto* Manager = World->SpawnActor<AChunkWorldNetManager>();
	auto* Waiting = World->SpawnActor<AChunkWorldExtended>();
	auto* Ready = World->SpawnActor<AChunkWorldExtended>();
	ON_SCOPE_EXIT
	{
		FLayoutTestWorldSupport::ShutdownTransientChunkWorld(Waiting);
		FLayoutTestWorldSupport::ShutdownTransientChunkWorld(Ready);
		ChunkWorldSpawnTest::DestroyWorld(World, Context);
	};
	const auto Start = [](AChunkWorldExtended* Terrain)
	{
		FLayoutTestWorldSupport::InitializeTransientChunkWorld(Terrain);
		if (Terrain->CustomSave.empty())
		{
			Terrain->StopGen();
			Terrain->WorldGenDef->CustomFeatureDefaultData.Add(0);
			Terrain->StartGen();
		}
	};
	Start(Ready);
	Ready->SetRole(ROLE_SimulatedProxy);
	const int32 ReadyId = Manager->RegisterWorld(Ready);
	// Receive frames before this world is bound; later bind alone still cannot release them.
	const int32 WaitingId = AChunkWorldNetManager::NextWorldNetId.fetch_add(1, std::memory_order_relaxed);
	Waiting->ReplicatedWorldNetId = WaitingId;
	Manager->NetMode = NM_Client;
	const FIntVector Cell(2, 3, 4);
	const CChunkData* Layer = Ready->PrimLayer;
	const FIntVector Grid = Ready->BlockWorldPosToChunkGridPos(Cell, Layer);
	const int32 Index = Ready->BlockChunkPosToBlockChunkIndex(Ready->BlockWorldPosToBlockChunkPos(Cell, Layer), Layer);
	auto Queue = [](auto& Queue, int32 Id, const auto& Edit)
	{
		static_assert(sizeof(Edit) % sizeof(uint32) == 0);
		auto& Batch = Queue.AddDefaulted_GetRef();
		Batch.WorldNetId = Id;
		Batch.Data.SetNumUninitialized(sizeof(Edit) / sizeof(uint32));
		FMemory::Memcpy(Batch.Data.GetData(), &Edit, sizeof(Edit));
	};
	auto Send = [&](int32 Id, bool bFinal)
	{
		Queue(Manager->PendingBlockChanges, Id, SBlockChangeCall{Grid, Index, 0, bFinal ? SinfullMaterial : EmptyMaterial});
		SMeshChangeCall Mesh;
		Mesh.chunkPose = Grid;
		Mesh.blockChunkIndex = Index;
		Mesh.transform.MeshId = bFinal ? 0 : EmptyMesh;
		Mesh.transform.Flag = bFinal ? 42 : 0;
		Queue(Manager->PendingMeshChanges, Id, Mesh);
		Queue(Manager->PendingCustomDataChanges, Id, SCustomDataChangeCall{Grid, Index, 0, 0, bFinal ? 85 : 10});
	};
	auto CheckFinal = [&](AChunkWorldExtended* Terrain)
	{
		TestEqual(TEXT("Newer material wins"), Terrain->GetBlockValueByBlockWorldPos(Cell), SinfullMaterial);
		const FMeshData Mesh = Terrain->GetMeshDataByBlockWorldPos(Cell);
		TestEqual(TEXT("Newer mesh wins"), Mesh.MeshId, 0);
		TestEqual(TEXT("Exact mesh transform payload survives"), int32(Mesh.Flag), 42);
		TestEqual(TEXT("Newer custom value wins"), Terrain->GetBlockValueByBlockWorldPos(Cell, ERessourceType::CustomData, 0), 85);
	};
	Send(WaitingId, false);
	Send(ReadyId, false);
	Send(WaitingId, true);
	Send(ReadyId, true);
	Manager->Tick(0.f);
	CheckFinal(Ready);
	Manager->RegisterWorld(Waiting);
	Manager->Tick(0.f);
	TestEqual(TEXT("Bound unstarted world keeps both material frames"), Manager->PendingBlockChanges.Num(), 2);
	TestEqual(TEXT("Both mesh frames retained"), Manager->PendingMeshChanges.Num(), 2);
	TestEqual(TEXT("Both custom frames retained"), Manager->PendingCustomDataChanges.Num(), 2);
	Start(Waiting);
	Waiting->SetRole(ROLE_SimulatedProxy);
	if (!TestFalse(TEXT("Custom write store exists"), Waiting->CustomSave.empty())) return false;
	{
		// Remote material/custom apply observes the old mesh too, unlike an authority-side write.
		TGuardValue<CCacheManager*> MissingCache(Waiting->MeshCache, nullptr);
		Manager->Tick(0.f);
		TestEqual(TEXT("Running proxy cannot observe missing mesh cache"), Manager->PendingBlockChanges.Num(), 2);
	}
	{
		TGuardValue<CCacheManager*> MissingStore(Waiting->CustomSave[0], nullptr);
		Manager->Tick(0.f);
		TestTrue(TEXT("Ready material/mesh queues drain past held custom store"),
			Manager->PendingBlockChanges.IsEmpty() && Manager->PendingMeshChanges.IsEmpty());
		TestEqual(TEXT("Running alone cannot release custom writes"), Manager->PendingCustomDataChanges.Num(), 2);
	}
	TestEqual(TEXT("No custom write before release"), Waiting->GetBlockValueByBlockWorldPos(Cell, ERessourceType::CustomData, 0), 0);
	Manager->Tick(0.f);
	CheckFinal(Waiting);
	TestTrue(TEXT("Deferred frames consumed exactly once"), Manager->PendingCustomDataChanges.IsEmpty());
	return true;
}
