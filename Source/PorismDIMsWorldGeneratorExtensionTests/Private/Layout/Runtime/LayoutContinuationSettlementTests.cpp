// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Misc/AutomationTest.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutContinuationSettlementTest,
	"PorismExtension.Layout.Runtime.Continuation.Ledger.Settlement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContinuationSettlementTest::RunTest(const FString& Parameters)
{
	UChunkWorldLayoutRuntimeComponent* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	ULayoutPlanningWindowStore* Store = Runtime->GetLayoutPlanningWindowStore();
	Runtime->RefreshConnectorRecords();
	const uint64 InitialGeneration = Runtime->ConnectorRefreshGeneration;
	Runtime->RefreshConnectorRecords();
	TestEqual(TEXT("Unchanged tick does not rebuild connector generation"), Runtime->ConnectorRefreshGeneration, InitialGeneration);
	auto ReserveRoute = [&](const FLayoutId RouteId, const int32 RootOffset, const TArray<uint64>& Keys)
	{
		TArray<FResolvedLayoutConnectorEndpoint> Endpoints;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			FLayoutPlanningWindowEndpointRecord Record;
			Record.RootRecordKey = FString::Printf(TEXT("Root%d"), RootOffset + Index);
			Record.StableEndpointKey = Record.RootRecordKey + TEXT("/Entry");
			Record.WorldBindingId = TEXT("Binding");
			Record.ContinuationFamilyId = TEXT("Road");
			Record.Endpoint.RootRecordKey = Record.RootRecordKey;
			Record.Endpoint.SiteReservationKey = FIntPoint(RootOffset + Index, 0);
			Record.RemainingConnections = 2;
			Record.MaxConnectionsPerSite = 2;
			Record.State = ELayoutContinuationEndpointState::Ready;
			FLayoutPlanningWindowEndpointRecord Stored;
			TestTrue(TEXT("Publish unit endpoint"), Store->UpsertContinuationEndpointRecord(Record, Stored));
			Endpoints.Add(Record.Endpoint);
		}
		FString EdgeKey, Failure;
		TestTrue(TEXT("Reserve unit pair"), Store->ReserveContinuationEndpointPair(
			Endpoints[0], Endpoints[1], TEXT("Road"), TEXT("Path"), EdgeKey, Failure));
		Runtime->RegisterContinuationRouteReservation(RouteId, EdgeKey, Keys);
	};

	ReserveRoute(TEXT("Partial"), 0, { 1, 2, 3 });
	Runtime->ReleaseContinuationReservationForConnector(2, true);
	TestTrue(TEXT("Failed middle segment keeps pending siblings"), Runtime->ContinuationRouteReservations.Contains(TEXT("Partial")));
	TestTrue(TEXT("First applied sibling consumes route capacity"), Runtime->ConsumeContinuationReservationForConnector(1));
	TestEqual(TEXT("Capacity consumed before last sibling is ready"), Store->GetContinuationEndpointRecords()[0].RemainingConnections, 1);
	TestTrue(TEXT("Waiting last sibling retains route lifetime"), Runtime->ContinuationRouteReservations.Contains(TEXT("Partial")));
	TestTrue(TEXT("Repeated completion is idempotent"), Runtime->ConsumeContinuationReservationForConnector(1));
	TestTrue(TEXT("Last valid sibling applies without failed middle"), Runtime->ConsumeContinuationReservationForConnector(3));
	TestFalse(TEXT("Mixed terminal outcomes release transient route"), Runtime->ContinuationRouteReservations.Contains(TEXT("Partial")));
	TestEqual(TEXT("Entire partial route consumes only one allowance"), Store->GetContinuationEndpointRecords()[0].RemainingConnections, 1);

	ReserveRoute(TEXT("Failed"), 2, { 4, 5 });
	Runtime->ReleaseContinuationReservationForConnector(4, true);
	Runtime->ReleaseContinuationReservationForConnector(5, true);
	TestFalse(TEXT("Entirely failed route releases reservation"), Runtime->ContinuationRouteReservations.Contains(TEXT("Failed")));
	for (const FLayoutPlanningWindowEndpointRecord& Endpoint : Store->GetContinuationEndpointRecords())
	{
		if (Endpoint.RootRecordKey == TEXT("Root2") || Endpoint.RootRecordKey == TEXT("Root3"))
		{
			TestEqual(TEXT("Failure does not consume capacity"), Endpoint.RemainingConnections, 2);
			TestTrue(TEXT("Failure releases endpoint"), Endpoint.State == ELayoutContinuationEndpointState::Ready);
		}
	}
	Runtime->RefreshConnectorRecords();
	TestEqual(TEXT("Changed endpoints wake one discovery pass"), Runtime->ConnectorRefreshGeneration, InitialGeneration + 1);
	Runtime->RefreshConnectorRecords();
	TestEqual(TEXT("Settled unchanged endpoints stay idle"), Runtime->ConnectorRefreshGeneration, InitialGeneration + 1);
	int32 CallbackCount = 0;
	Runtime->ExplicitContinuationCompletionCallbacks.Add(42, [this, Runtime, &CallbackCount](const FLayoutExplicitContinuationSolveResult&)
	{
		++CallbackCount;
		TestFalse(TEXT("Completion releases ownership before callback"), Runtime->ExplicitContinuationCompletionCallbacks.Contains(42));
		Runtime->ExplicitContinuationCompletionCallbacks.Add(42, [&CallbackCount](const FLayoutExplicitContinuationSolveResult&) { ++CallbackCount; });
	});
	Runtime->CompleteExplicitContinuationRequest(42, FLayoutExplicitContinuationSolveResult());
	TestTrue(TEXT("Reentrant request survives prior completion"), Runtime->ExplicitContinuationCompletionCallbacks.Contains(42));
	Runtime->CompleteExplicitContinuationRequest(42, FLayoutExplicitContinuationSolveResult());
	Runtime->CompleteExplicitContinuationRequest(42, FLayoutExplicitContinuationSolveResult());
	TestEqual(TEXT("Each request callback completes once"), CallbackCount, 2);

	const FIntVector FirstChunk(0, 0, 0), SecondChunk(16, 0, 0);
	Runtime->ObservedCoverageTileSize = FIntVector(16);
	Runtime->ObservedChunkLayers.SetNum(1);
	Runtime->ObservedChunkLayers[0].ChunkSizeInBlocks = FIntVector(16);
	Runtime->ObservedChunkLayers[0].Chunks.Add(FirstChunk, FLayoutLoadedChunkState{});
	Runtime->RefreshPlacedContinuationRootReadiness();
	TestTrue(TEXT("Loaded chunks alone never upgrade solved-only endpoints"), Runtime->LoadedContinuationRootKeys.IsEmpty());
	Runtime->PlacedContinuationRootChunks.Add(TEXT("PlacedA"), {FirstChunk, SecondChunk});
	Runtime->RefreshPlacedContinuationRootReadiness();
	TestFalse(TEXT("Partially loaded root cannot connect"), Runtime->LoadedContinuationRootKeys.Contains(TEXT("PlacedA")));
	Runtime->ObservedChunkLayers[0].Chunks.Add(SecondChunk, FLayoutLoadedChunkState{});
	Runtime->RefreshPlacedContinuationRootReadiness();
	TestTrue(TEXT("Whole placed root becomes eligible on readiness"), Runtime->LoadedContinuationRootKeys.Contains(TEXT("PlacedA")));
	Runtime->PlacedContinuationRootChunks.Add(TEXT("PlacedB"), {FirstChunk});
	Runtime->RefreshPlacedContinuationRootReadiness();
	FLayoutPreparedContinuationRoute Prepared;
	Prepared.Route.StartRootEndpoint.RootRecordKey = TEXT("PlacedA");
	Prepared.Route.EndRootEndpoint.RootRecordKey = TEXT("PlacedB");
	Runtime->RetainedPreparedContinuationRoutesById.Add(TEXT("LoadedPair"), Prepared);
	TestTrue(TEXT("Both placed loaded roots permit route"), Runtime->IsContinuationRouteEligible(TEXT("LoadedPair")));
	Runtime->LastConnectorEndpointRevision = 99;
	Runtime->RefreshPlacedContinuationRootReadiness();
	TestEqual(TEXT("Duplicate readiness does not wake discovery"), Runtime->LastConnectorEndpointRevision, uint64(99));
	Runtime->ObservedChunkLayers[0].Chunks.Remove(SecondChunk);
	Runtime->RefreshPlacedContinuationRootReadiness();
	TestFalse(TEXT("Root unload invalidates route despite nearby endpoints"), Runtime->IsContinuationRouteEligible(TEXT("LoadedPair")));
	TestEqual(TEXT("Real readiness change wakes discovery"), Runtime->LastConnectorEndpointRevision, uint64(0));
	Runtime->RetainedPreparedContinuationRoutesById.Reset();
	Runtime->PruneContinuationEndpointsOutsideExpandedWindows({}, {});
	TestTrue(TEXT("Entry eviction releases root readiness payload"), Runtime->PlacedContinuationRootChunks.IsEmpty());
	TestTrue(TEXT("Entry eviction releases loaded-root history"), Runtime->LoadedContinuationRootKeys.IsEmpty());

	FResolvedLayoutConnectorRecord DisplayRecord;
	DisplayRecord.ContinuationRouteId = TEXT("DisplayPartial");
	DisplayRecord.bLayoutSolved = true;
	DisplayRecord.SolveResult.bSucceeded = true;
	DisplayRecord.bHasBeenCommittedToChunkWorld = true;
	Runtime->ResolvedConnectorRecords.Add(101, DisplayRecord);
	DisplayRecord.bHasBeenCommittedToChunkWorld = false;
	Runtime->ResolvedConnectorRecords.Add(102, DisplayRecord);
	FLayoutContinuationRouteReservationState& DisplayState = Runtime->ContinuationRouteReservations.Add(TEXT("DisplayPartial"));
	DisplayState.SegmentKeys = {101, 102, 103, 104};
	DisplayState.CommittedSegmentKeys = {101};
	DisplayState.FailedSegmentKeys = {103};
	DisplayRecord.ContinuationRouteId = TEXT("DisplayUnplaced");
	Runtime->ResolvedConnectorRecords.Add(105, DisplayRecord);
	DisplayRecord.ContinuationRouteId = TEXT("InvalidPreview");
	DisplayRecord.SolveResult.bSucceeded = false;
	Runtime->ResolvedConnectorRecords.Add(106, DisplayRecord);
	TestTrue(TEXT("HUD counts partial routes once and excludes invalid previews"), Runtime->BuildPlanningWindowDebugStatusMessage().Contains(
		TEXT("Retained routes: solving=1 solved=2 placed=1 unplaced=1")));
	DisplayState.FailedSegmentKeys.Add(104);
	TestTrue(TEXT("Omitted siblings no longer report solving work"), Runtime->BuildPlanningWindowDebugStatusMessage().Contains(
		TEXT("Retained routes: solving=0 solved=2 placed=1 unplaced=1")));
	Runtime->ReleaseContinuationRouteReservation(TEXT("DisplayPartial"));
	Runtime->ExplicitConnectorKeys.Add(101);
	Runtime->PruneContinuationEndpointsOutsideExpandedWindows({}, {});
	TestTrue(TEXT("Explicit committed carrier retains Apply/Clear ownership"), Runtime->ResolvedConnectorRecords.Contains(101));
	Runtime->ExplicitConnectorKeys.Remove(101);
	Runtime->PruneContinuationEndpointsOutsideExpandedWindows({}, {});
	TestFalse(TEXT("Settled automatic segment releases heavy carrier after application"), Runtime->ResolvedConnectorRecords.Contains(101));
	TestTrue(TEXT("Pending segment payload is not mistaken for a retired placement"), Runtime->ResolvedConnectorRecords.Contains(102));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutContinuationRootCapacityTest,
	"PorismExtension.Layout.Runtime.Continuation.Ledger.RootCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContinuationRootCapacityTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* Store = NewObject<ULayoutPlanningWindowStore>();
	auto AddEntry = [this, Store](const FString& Root, const int32 Entry)
	{
		FLayoutPlanningWindowEndpointRecord Record, Stored;
		Record.RootRecordKey = Root;
		Record.StableEndpointKey = Root + FString::Printf(TEXT("/Entry%d"), Entry);
		Record.ContinuationFamilyId = TEXT("Road");
		Record.Endpoint.RootRecordKey = Root;
		Record.Endpoint.LocalCell = FIntVector(Entry, 0, 0);
		Record.RemainingConnections = 1;
		Record.MaxConnectionsPerSite = 2;
		Record.State = ELayoutContinuationEndpointState::Ready;
		TestTrue(TEXT("Publish capacity endpoint"), Store->UpsertContinuationEndpointRecord(Record, Stored));
		return Record.Endpoint;
	};
	const auto A0 = AddEntry(TEXT("A"), 0), A1 = AddEntry(TEXT("A"), 1), A2 = AddEntry(TEXT("A"), 2);
	const auto B0 = AddEntry(TEXT("B"), 0), B1 = AddEntry(TEXT("B"), 1);
	const auto C0 = AddEntry(TEXT("C"), 0), D0 = AddEntry(TEXT("D"), 0);
	FString AB, AC, AD, Rejected, Failure;
	TestTrue(TEXT("First root pair reserves"), Store->ReserveContinuationEndpointPair(A0, B0, TEXT("Road"), TEXT("Path"), AB, Failure));
	TestFalse(TEXT("Another entry/candidate cannot duplicate root pair"), Store->ReserveContinuationEndpointPair(A1, B1, TEXT("Road"), TEXT("OtherPath"), Rejected, Failure));
	TestTrue(TEXT("Distinct neighbor reserves second slot"), Store->ReserveContinuationEndpointPair(A1, C0, TEXT("Road"), TEXT("Path"), AC, Failure));
	TestFalse(TEXT("Other entry cannot exceed shared root limit"), Store->ReserveContinuationEndpointPair(A2, D0, TEXT("Road"), TEXT("Path"), Rejected, Failure));
	TestEqual(TEXT("Both in-flight routes count"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 2);
	Store->ReleaseContinuationEndpointPair(AB);
	TestTrue(TEXT("Cancellation releases root allowance"), Store->ReserveContinuationEndpointPair(A2, D0, TEXT("Road"), TEXT("Path"), AD, Failure));
	TestTrue(TEXT("First committed segment consumes one route"), Store->ConsumeContinuationEndpointPair(AC));
	TestTrue(TEXT("Commit replay is idempotent"), Store->ConsumeContinuationEndpointPair(AC));
	TestEqual(TEXT("Committed plus in-flight still share limit"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 2);
	Store->ReleaseContinuationEndpointPair(AD);
	Store->ReleaseContinuationEndpointPair(AC);
	TestEqual(TEXT("Release cannot forget committed allowance"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 1);
	for (int32 Attempt = 0; Attempt < 4; ++Attempt)
	{
		const auto Start = AddEntry(TEXT("A"), 3 + Attempt), End = AddEntry(TEXT("B"), 3 + Attempt);
		FString Edge;
		const bool bReserved = Store->ReserveContinuationEndpointPair(Start, End, TEXT("Road"), TEXT("Path"), Edge, Failure);
		TestTrue(TEXT("Greedy discovery reaches every distinct alternative after failures"), bReserved);
		if (bReserved)
		{
			Store->ReleaseContinuationEndpointPair(Edge, true);
			TestFalse(TEXT("Same deterministic failed edge cannot repeat"), Store->ReserveContinuationEndpointPair(Start, End, TEXT("Road"), TEXT("Path"), Rejected, Failure));
			TestTrue(TEXT("Explicit retry can reopen a failed edge"), Store->ReserveContinuationEndpointPair(Start, End, TEXT("Road"), TEXT("Path"), Edge, Failure, true));
			TestEqual(TEXT("Explicit retry still reserves shared capacity"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 2);
			Store->ReleaseContinuationEndpointPair(Edge, true);
		}
	}
	TestEqual(TEXT("Failures do not consume committed capacity"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 1);
	Store->InvalidateContinuationFailureHistory();
	TestTrue(TEXT("Input revision reopens failed root pair"), Store->ReserveContinuationEndpointPair(A0, B0, TEXT("Road"), TEXT("Path"), AB, Failure));
	Store->InvalidateContinuationFailureHistory();
	TestEqual(TEXT("Input invalidation preserves reserved and committed capacity"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 2);
	TestEqual(TEXT("Reservations cannot prove terminal capacity"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road"), true), 1);
	TestTrue(TEXT("Second committed route settles root capacity"), Store->ConsumeContinuationEndpointPair(AB));
	for (const auto& Endpoint : Store->GetContinuationEndpointRecords())
		TestNotEqual(TEXT("Capacity-complete root drops unused entries"), Endpoint.RootRecordKey, FString(TEXT("A")));
	Store->ReleaseContinuationEndpointPair(AB);
	FLayoutPlanningWindowEndpointRecord Replay, Stored;
	Replay.RootRecordKey = TEXT("A");
	Replay.StableEndpointKey = TEXT("A/Entry1");
	Replay.ContinuationFamilyId = TEXT("Road");
	Replay.Endpoint = A1;
	Replay.RemainingConnections = 1;
	Replay.State = ELayoutContinuationEndpointState::Ready;
	FLayoutPlanningWindowEndpointRecord Unused = Replay;
	Unused.StableEndpointKey = TEXT("A/Entry2");
	Unused.Endpoint = A2;
	Unused.MaxConnectionsPerSite = 2;
	TestFalse(TEXT("Committed capacity prevents unused-entry republication"), Store->UpsertContinuationEndpointRecord(Unused, Stored));
	Store->RemoveContinuationEndpointRecordsOutsideWindows({FIntPoint::ZeroValue}, {FIntPoint::ZeroValue});
	TestFalse(TEXT("Retained committed influence preserves consumed-entry protection"), Store->UpsertContinuationEndpointRecord(Replay, Stored));
	Store->RemoveContinuationEndpointRecordsOutsideWindows({}, {});
	TestEqual(TEXT("Window eviction releases retained edge history"), Store->CountContinuationRootConnections(TEXT("A"), TEXT("Road")), 0);
	TestTrue(TEXT("Full influence eviction also reclaims consumed-entry history"), Store->UpsertContinuationEndpointRecord(Replay, Stored));
	return true;
}

#endif
