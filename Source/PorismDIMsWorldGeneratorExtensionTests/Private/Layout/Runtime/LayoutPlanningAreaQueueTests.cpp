// Copyright 2026 Spotted Loaf Studio

#include "Misc/AutomationTest.h"
#include "Layout/Runtime/LayoutPlanningAreaQueue.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "UObject/UnrealType.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "../Support/LayoutWorldTestUtilities.h"
#include "Engine/DataTable.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPlanningAreaQueueTest,
	"PorismExtension.Layout.Runtime.PlanningAreaQueue.ProgressAndAllowance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningAreaQueueTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutLoadedChunkLayer> Directory;
	FLayoutPlanningAreaQueue Queue(Directory);
	using FKey = FLayoutPlanningAreaQueue::FChunkKey;
	const FKey Near(0, FIntVector::ZeroValue), Far(0, FIntVector(64, 0, 0));
	const FIntVector Size(16, 16, 16);
	const TArray<FIntVector> Centers{FIntVector::ZeroValue};
	FIntPoint Area;
	Queue.Configure(4, 1);
	TestTrue(TEXT("New Created observation wakes screening"), Queue.Observe(Near, Size, true));
	TestFalse(TEXT("Duplicate observation coalesces"), Queue.Observe(Near, Size, true));
	TestTrue(TEXT("Diagnostics count one actual addition, not repeated observations"),
		Queue.DescribeBookkeeping().Contains(TEXT("L0=1(+1/-0)")));
	TestTrue(TEXT("Initial coverage queues"), Queue.TakeNext(Centers, Area));
	TestEqual(TEXT("Unscreened metadata uses no working slot"), Queue.GetWorkingCount(), 0);
	Queue.MarkIrrelevant(Area, Queue.GetScanId(Area));
	Queue.FinishScan(Area, false, Queue.GetScanId(Area));
	TestEqual(TEXT("Irrelevant terrain uses no working slot"), Queue.GetWorkingCount(), 0);
	TestFalse(TEXT("Proven irrelevant coverage stays idle"), Queue.TakeNext(Centers, Area));

	Queue.Reset();
	TestTrue(TEXT("Input revision rescreens loaded terrain"), Queue.TakeNext(Centers, Area));
	TestTrue(TEXT("Positive evidence promotes a slot"), Queue.MarkEligible(Area, Queue.GetScanId(Area)));
	TestTrue(TEXT("First candidate reserves allowance"), Queue.BeginAttempt(Area, TEXT("SuccessA")));
	Queue.FinishScan(Area, true, Queue.GetScanId(Area));
	Queue.FinishAttempt(Area, TEXT("SuccessA"), false);
	TestTrue(TEXT("Success leaves other candidates discoverable"), Queue.TakeNext(Centers, Area));
	TestTrue(TEXT("Same chunk can produce a second root"), Queue.BeginAttempt(Area, TEXT("SuccessB")));
	Queue.FinishAttempt(Area, TEXT("SuccessB"), false);
	TestTrue(TEXT("First failure allowance reserved"), Queue.BeginAttempt(Area, TEXT("A")));
	TestTrue(TEXT("Second candidate shares allowance"), Queue.BeginAttempt(Area, TEXT("B")));
	TestTrue(TEXT("Third candidate shares allowance"), Queue.BeginAttempt(Area, TEXT("C")));
	TestFalse(TEXT("In-flight attempts cannot overshoot allowance"), Queue.BeginAttempt(Area, TEXT("D")));
	Queue.FinishScan(Area, true, Queue.GetScanId(Area));
	Queue.FinishAttempt(Area, TEXT("A"), false, true);
	TestTrue(TEXT("Cancellation returns allowance"), Queue.TakeNext(Centers, Area));
	Queue.FinishAttempt(Area, TEXT("B"), true);
	Queue.FinishAttempt(Area, TEXT("C"), true);
	TestFalse(TEXT("Deterministic failure does not retry"), Queue.BeginAttempt(Area, TEXT("B")));
	TestTrue(TEXT("Canceled work may retry"), Queue.BeginAttempt(Area, TEXT("A")));
	Queue.FinishAttempt(Area, TEXT("A"), true);
	Queue.FinishAttempt(Area, TEXT("A"), true);
	Queue.FinishScan(Area, false, Queue.GetScanId(Area));
	Queue.Observe(FKey(1, FIntVector::ZeroValue), Size, true);
	TestFalse(TEXT("Overlapping LOD cannot bypass shared failure allowance"), Queue.TakeNext(Centers, Area));
	int32 Queued = 0, Exhausted = 0;
	Queue.GetCounts(Queued, Exhausted);
	TestEqual(TEXT("HUD counts exhausted canonical area once"), Exhausted, 1);

	Queue.ResetLoadedDirectory();
	Queue.Observe(Near, Size, true);
	Queue.TakeNext(Centers, Area);
	Queue.BeginAttempt(Area, TEXT("SynchronousFailure"));
	Queue.FinishAttempt(Area, TEXT("SynchronousFailure"), true);
	Queue.FinishScan(Area, true, Queue.GetScanId(Area));
	TestTrue(TEXT("Synchronous failure preserves deferred scan"), Queue.TakeNext(Centers, Area));
	Queue.BeginAttempt(Area, TEXT("PendingReservation"));
	Queue.MarkBlockedByReservation(Area, TEXT("PendingReservation"));
	Queue.FinishScan(Area, false, Queue.GetScanId(Area));
	Queue.NotifyReservationReleased(TEXT("UnrelatedRoot"));
	TestFalse(TEXT("Unrelated reservation release leaves blocked region dormant"), Queue.TakeNext(Centers, Area));
	Queue.FinishAttempt(Area, TEXT("PendingReservation"), true);
	TestTrue(TEXT("Failed reservation wakes blocked candidates"), Queue.TakeNext(Centers, Area));
	const uint64 BeforeReset = Queue.GetScanId(Area);
	Queue.Reset();
	Queue.TakeNext(Centers, Area);
	const uint64 AfterReset = Queue.GetScanId(Area);
	Queue.FinishScan(Area, false, BeforeReset);
	TestTrue(TEXT("Input reset fences old completion"), Queue.IsCurrentScan(Area, AfterReset));

	Queue.ResetLoadedDirectory();
	Queue.Observe(Near, Size, true);
	Queue.Observe(Far, Size, true);
	const TArray<FIntVector> TwoCenters{FIntVector::ZeroValue, FIntVector(64, 0, 0)};
	TestTrue(TEXT("First center receives nearest chunk"), Queue.TakeNext(TwoCenters, Area));
	TestEqual(TEXT("First center canonical area"), Area, FIntPoint::ZeroValue);
	TestTrue(TEXT("First center promotes"), Queue.MarkEligible(Area, Queue.GetScanId(Area)));
	Queue.FinishScan(Area, true, Queue.GetScanId(Area));
	TestTrue(TEXT("Next turn serves separated center"), Queue.TakeNext(TwoCenters, Area));
	TestEqual(TEXT("Second center canonical area"), Area, FIntPoint(4, 0));
	TestTrue(TEXT("Second center can replace waiting slot"), Queue.MarkEligible(Area, Queue.GetScanId(Area)));
	TestEqual(TEXT("Shared working capacity never multiplies by centers"), Queue.GetWorkingCount(), 1);
	TestTrue(TEXT("Pressure reports eviction"), Queue.GetEvictedCount() > 0);
	TestEqual(TEXT("Working eviction preserves both loaded chunks"), Directory[0].Chunks.Num(), 2);
	const FString BeforeReplacement = Queue.DescribeBookkeeping();
	const uint64 OldScan = Queue.GetScanId(Area);
	Queue.Forget(Far);
	Queue.Observe(Far, Size, true);
	TestEqual(TEXT("Replacement preserves loaded count"), Directory[0].Chunks.Num(), 2);
	TestNotEqual(TEXT("Diagnostics expose membership replacement despite equal totals"),
		Queue.DescribeBookkeeping(), BeforeReplacement);
	TestFalse(TEXT("Recreated chunk rejects old scan"), Queue.IsCurrentScan(Area, OldScan));
	Queue.RetireWorkingSet();
	TestTrue(TEXT("Same centers resume unfinished work after disable"), Queue.TakeNext(TwoCenters, Area));
	const uint64 NewScan = Queue.GetScanId(Area);
	Queue.FinishScan(Area, false, OldScan);
	TestTrue(TEXT("Old callback cannot settle replacement scan"), Queue.IsCurrentScan(Area, NewScan));
	Queue.FinishScan(Area, false, NewScan, true);
	TestTrue(TEXT("Canceled scan remains available"), Queue.TakeNext(TwoCenters, Area));

	Queue.ResetLoadedDirectory();
	Queue.Observe(FKey(0, FIntVector(-16, -16, 0)), Size, true);
	TestTrue(TEXT("Negative coordinates queue"), Queue.TakeNext({FIntVector(-1, -1, 0)}, Area));
	TestEqual(TEXT("Negative area uses floor division"), Area, FIntPoint(-1, -1));
	FIntPoint Min, Max;
	Queue.GetBounds(Area, Min, Max);
	TestEqual(TEXT("Negative inclusive lower bound"), Min, FIntPoint(-16, -16));
	TestEqual(TEXT("Negative inclusive upper bound"), Max, FIntPoint(-1, -1));
	Queue.FinishScan(Area, false, Queue.GetScanId(Area));
	const FString SettledBookkeeping = Queue.DescribeBookkeeping();
	for (int32 Repeat = 0; Repeat < 32; ++Repeat)
		Queue.Observe(FKey(0, FIntVector(-16, -16, 0)), Size, Repeat % 2 == 0);
	TestEqual(TEXT("Stationary Created/Updated preserves settled bookkeeping"), Queue.DescribeBookkeeping(), SettledBookkeeping);
	TestFalse(TEXT("Stationary settled coverage does not rescan"), Queue.TakeNext({FIntVector(-1, -1, 0)}, Area));
	Queue.ResetLoadedDirectory();
	Queue.Configure(4, 1);
	Queue.Observe(Near, FIntVector(8), true);
	TestTrue(TEXT("First partial native coverage starts its canonical area"), Queue.TakeNext(Centers, Area));
	Queue.BeginAttempt(Area, TEXT("RetainedFailure"));
	Queue.FinishAttempt(Area, TEXT("RetainedFailure"), true);
	Queue.FinishScan(Area, false, Queue.GetScanId(Area));
	TestFalse(TEXT("Unchanged partial coverage settles"), Queue.TakeNext(Centers, Area));
	Queue.Observe(FKey(0, FIntVector(8, 0, 0)), FIntVector(8), true);
	TestTrue(TEXT("New neighbor coverage reopens the shared completed area"), Queue.TakeNext(Centers, Area));
	TestTrue(TEXT("Coverage arrival preserves failed-key allowance"), Queue.HasFailed(Area, TEXT("RetainedFailure")));
	Queue.MarkBlockedByReservation(Area, TEXT("ReleasedDuringScan"));
	Queue.NotifyReservationReleased(TEXT("ReleasedDuringScan"));
	Queue.FinishScan(Area, false, Queue.GetScanId(Area));
	TestTrue(TEXT("Scan completion cannot swallow a relevant release"), Queue.TakeNext(Centers, Area));

	Queue.ResetLoadedDirectory();
	Queue.Configure(3, 1);
	Queue.Observe(FKey(0, FIntVector(MIN_int32, MAX_int32, 0)), FIntVector(1), true);
	TestTrue(TEXT("Extreme coordinates remain bounded"), Queue.TakeNext({FIntVector(MIN_int32, MAX_int32, 0)}, Area));
	Queue.GetBounds(Area, Min, Max);
	TestTrue(TEXT("Clipped bounds retain requested position"), Min.X == MIN_int32 && Max.Y == MAX_int32);

	UChunkWorldLayoutRuntimeComponent* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	ULayoutWorldBindingAsset* FirstBinding = NewObject<ULayoutWorldBindingAsset>();
	Runtime->SetLayoutWorldBindings({FirstBinding});
	Runtime->PlanningAreaQueue->Configure(4, 1);
	Runtime->PlanningAreaQueue->Observe(Near, Size, true);
	Runtime->PlanningAreaQueue->TakeNext(Centers, Area);
	TestTrue(TEXT("Runtime reserves preparation attempt"), Runtime->PlanningAreaQueue->BeginAttempt(Area, TEXT("BeforeStore")));
	Runtime->PlanningAreasByRecordKey.Add(TEXT("BeforeStore"), Area);
	Runtime->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(TEXT("BeforeStore"), TEXT("Selected-site preparation failed."), TEXT("BeforeStore.Descriptor"));
	TestTrue(TEXT("Pre-store preparation failure charges area allowance"), Runtime->PlanningAreaQueue->HasFailed(Area, TEXT("BeforeStore")));
	Runtime->FinishPlanningAreaAttempt(TEXT("BeforeStore"), false, true);
	TestFalse(TEXT("Scope-exit cleanup cannot reopen failed preparation"), Runtime->PlanningAreaQueue->BeginAttempt(Area, TEXT("BeforeStore")));
	Runtime->SetLayoutWorldBindings({FirstBinding});
	TestTrue(TEXT("Unchanged bindings preserve failure suppression"), Runtime->PlanningAreaQueue->HasFailed(Area, TEXT("BeforeStore")));
	TSet<FString> RetainedFailures;
	Runtime->PlanningAreaQueue->AppendRetainedFailureKeys(RetainedFailures);
	TestTrue(TEXT("Current area retains its diagnostic record"), RetainedFailures.Contains(TEXT("BeforeStore")));
	Runtime->SetLayoutWorldBindings({NewObject<ULayoutWorldBindingAsset>()});
	RetainedFailures.Reset();
	Runtime->PlanningAreaQueue->AppendRetainedFailureKeys(RetainedFailures);
	TestTrue(TEXT("Discarded area cannot retain historical diagnostics"), RetainedFailures.IsEmpty());
	TestFalse(TEXT("Changed bindings retire old area revision"), Runtime->PlanningAreaQueue->HasFailed(Area, TEXT("BeforeStore")));
	TestTrue(TEXT("Changed bindings wake unchanged coverage"), Runtime->PlanningAreaQueue->TakeNext(Centers, Area));
#if WITH_EDITOR
	Runtime->SetLayoutWorldBindings({FirstBinding});
	ULayoutProfileAsset* Profile = NewObject<ULayoutProfileAsset>();
	FirstBinding->Candidates.AddDefaulted_GetRef().LayoutProfile = Profile;
	Profile->ContentSet = NewObject<ULayoutRegionContentSetAsset>();
	// Recursive invalid authoring must not hang unrelated-asset filtering.
	FLayoutRegionContentEntry& Child = Profile->ContentSet->Entries.AddDefaulted_GetRef();
	Child.ContentKind = ELayoutRegionContentKind::ChildRegion;
	Child.ChildRegionSettings.RegionProfile = Profile;
	Runtime->PlanningAreaQueue->TakeNext(Centers, Area);
	const uint64 BeforeAssetEdit = Runtime->PlanningAreaQueue->GetScanId(Area);
	FPropertyChangedEvent Changed(nullptr);
	Runtime->HandlePlanningAssetChanged(NewObject<ULayoutProfileAsset>(), Changed);
	TestTrue(TEXT("Unrelated asset edits preserve current scan"), Runtime->PlanningAreaQueue->IsCurrentScan(Area, BeforeAssetEdit));
	Runtime->HandlePlanningAssetChanged(Profile->ContentSet, Changed);
	TestFalse(TEXT("Referenced nested asset edit invalidates current scan"), Runtime->PlanningAreaQueue->IsCurrentScan(Area, BeforeAssetEdit));
	TestTrue(TEXT("Referenced asset edit wakes unchanged coverage"), Runtime->PlanningAreaQueue->TakeNext(Centers, Area));
	// Keep harness alive until all raw-pointer fixtures finish; its teardown collects garbage.
	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(GetTransientPackage());
	auto* OwnedRuntime = Harness.RuntimeComponent;
	OwnedRuntime->SetLayoutWorldBindings({FirstBinding});
	OwnedRuntime->PlanningAreaQueue->Configure(4, 1);
	OwnedRuntime->PlanningAreaQueue->Observe(Near, Size, true);
	// Production entry point must settle full exclusion without submitting a noise/preparation job.
	Profile->MaximumFootprintInCells = FIntPoint(100);
	FLayoutRootSpacingReservation Exclusion;
	const FName BindingId = FirstBinding->BindingId.IsNone() ? FirstBinding->GetFName() : FirstBinding->BindingId;
	TestTrue(TEXT("Full-area exclusion fixture builds"), FLayoutRootSpacingReservation::TryBuild(
		BindingId, FIntVector(8, 8, 0), FIntPoint(100), FirstBinding->BaseCellDimensionsBlocks, Exclusion));
	OwnedRuntime->RootSpacingReservations.Add(TEXT("AreaBlocker"), Exclusion);
	TestTrue(TEXT("Full-area exclusion starts a scan"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	OwnedRuntime->SubmitPlanningAreaDiscovery(Area, FIntVector(8, 8, 0), FIntPoint(0), FIntPoint(15));
	TestFalse(TEXT("Full exclusion creates no discovery worker"), OwnedRuntime->PendingPlanningAreaDiscovery.IsSet());
	TestFalse(TEXT("Full exclusion remains dormant"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	OwnedRuntime->FinishPlanningAreaAttempt(TEXT("AreaBlocker"), false, true);
	TestTrue(TEXT("Released root without an area attempt reopens discovery"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	OwnedRuntime->PlanningAreaQueue->FinishScan(Area, true, OwnedRuntime->PlanningAreaQueue->GetScanId(Area));
	OwnedRuntime->RootSpacingReservations.Add(TEXT("ClearedRoot"), Exclusion);
	TestTrue(TEXT("Explicit reset fixture starts a scan"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	OwnedRuntime->SubmitPlanningAreaDiscovery(Area, FIntVector(8, 8, 0), FIntPoint(0), FIntPoint(15));
	OwnedRuntime->ResetResolvedLayoutSiteRecords(true);
	TestTrue(TEXT("Explicit root clear wakes its blocked region"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	OwnedRuntime->PlanningAreaQueue->FinishScan(Area, true, OwnedRuntime->PlanningAreaQueue->GetScanId(Area));
	FirstBinding->OccupancyProbability = 0.0f;
	TestTrue(TEXT("Zero occupancy starts its bounded scan"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	OwnedRuntime->SubmitPlanningAreaDiscovery(Area, FIntVector(8, 8, 0), FIntPoint(0), FIntPoint(15));
	TestFalse(TEXT("Zero occupancy creates no discovery worker"), OwnedRuntime->PendingPlanningAreaDiscovery.IsSet());
	TestTrue(TEXT("Zero occupancy reserves no root spacing"), OwnedRuntime->RootSpacingReservations.IsEmpty());
	TestFalse(TEXT("Zero occupancy does not label terrain biome-irrelevant"),
		OwnedRuntime->ObservedChunkLayers[0].Chunks.FindChecked(FIntVector::ZeroValue).Screening == ELayoutChunkScreening::Irrelevant);
	FirstBinding->OccupancyProbability = 1.0f;
	OwnedRuntime->PlanningAreaQueue->Reset();
	Harness.World->WorldGenDef->WorldBiomesDT = NewObject<UDataTable>();
	for (UObject* Input : TArray<UObject*>{Harness.World->WorldGenDef, Harness.World->WorldGenDef->WorldBiomesDT})
	{
		TestTrue(TEXT("Definition fixture starts a current scan"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
		const uint64 BeforeDefinitionEdit = OwnedRuntime->PlanningAreaQueue->GetScanId(Area);
		OwnedRuntime->HandlePlanningAssetChanged(NewObject<UWorldGenDef>(), Changed);
		TestTrue(TEXT("Unrelated world definition preserves scan"), OwnedRuntime->PlanningAreaQueue->IsCurrentScan(Area, BeforeDefinitionEdit));
		if (auto* Table = Cast<UDataTable>(Input)) Table->HandleDataTableChanged();
		else OwnedRuntime->HandlePlanningAssetChanged(Input, Changed);
		TestFalse(TEXT("World definition or biome table edit fences old scan"), OwnedRuntime->PlanningAreaQueue->IsCurrentScan(Area, BeforeDefinitionEdit));
	}
	UDataTable* PreviousBiomeTable = Harness.World->WorldGenDef->WorldBiomesDT;
	Harness.World->WorldGenDef->WorldBiomesDT = nullptr;
	auto& NoiseRow = Harness.World->WorldGenDef->WorldBiomes[0];
	NoiseRow.GenA.Reset();
	NoiseRow.GenABP = nullptr;
	NoiseRow.GenARun = NewObject<UBiomeFastNoiseEditor>();
	for (UObject* Input : TArray<UObject*>{NoiseRow.GenARun, NewObject<UBiomeFastNoiseEditor>(NoiseRow.GenARun)})
	{
		TestTrue(TEXT("Noise fixture starts a current scan"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
		const uint64 BeforeNoiseEdit = OwnedRuntime->PlanningAreaQueue->GetScanId(Area);
		OwnedRuntime->HandlePlanningAssetChanged(NewObject<UBiomeFastNoiseEditor>(), Changed);
		TestTrue(TEXT("Unreferenced noise editor preserves scan"), OwnedRuntime->PlanningAreaQueue->IsCurrentScan(Area, BeforeNoiseEdit));
		OwnedRuntime->HandlePlanningAssetChanged(Input, Changed);
		TestFalse(TEXT("Referenced noise editor or child edit fences scan"), OwnedRuntime->PlanningAreaQueue->IsCurrentScan(Area, BeforeNoiseEdit));
	}
	TestTrue(TEXT("Replacement input starts a new scan"), OwnedRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	const uint64 AfterTableReplacement = OwnedRuntime->PlanningAreaQueue->GetScanId(Area);
	PreviousBiomeTable->HandleDataTableChanged();
	TestTrue(TEXT("Detached biome table cannot invalidate current work"), OwnedRuntime->PlanningAreaQueue->IsCurrentScan(Area, AfterTableReplacement));
	FLayoutRootSpacingReservation Bounds;
	TestTrue(TEXT("Retention fixture has valid bounds"), FLayoutRootSpacingReservation::TryBuild(
		TEXT("Binding"), FIntVector::ZeroValue, FIntPoint(1, 1), FIntVector(1, 1, 1), Bounds));
	Runtime->RootSpacingReservations.Add(TEXT("Automatic"), Bounds);
	Runtime->RootSpacingReservations.Add(TEXT("Explicit"), Bounds);
	Runtime->ResolvedSiteRecords.Add(TEXT("Explicit"), FResolvedLayoutSiteRecord());
	Runtime->bEnablePlanningWindowRuntimeUpdates = false;
	Runtime->UpdateLoadedChunkPlanning();
	TestFalse(TEXT("Disabled automatic planning releases obsolete spacing"), Runtime->RootSpacingReservations.Contains(TEXT("Automatic")));
	TestTrue(TEXT("Disabled automatic planning preserves explicit root ownership"), Runtime->RootSpacingReservations.Contains(TEXT("Explicit")));
	TestTrue(TEXT("Disabled automatic planning preserves explicit root carrier"), Runtime->ResolvedSiteRecords.Contains(TEXT("Explicit")));
	OwnedRuntime->bEnablePlanningWindowRuntimeUpdates = false;
	Harness.World->SetDetailedDiagnostics(true);
	OwnedRuntime->NextBookkeepingDiagnosticTime = 0.0;
	OwnedRuntime->ProcessQueuedLayoutWorkNow();
	TestFalse(TEXT("Enabled bookkeeping emits a zero-event snapshot"), OwnedRuntime->LastBookkeepingDiagnostic.IsEmpty());
	const double NextReport = OwnedRuntime->NextBookkeepingDiagnosticTime;
	OwnedRuntime->ProcessQueuedLayoutWorkNow();
	TestEqual(TEXT("Bookkeeping reporting cannot run every tick"), OwnedRuntime->NextBookkeepingDiagnosticTime, NextReport);
	Harness.World->SetDetailedDiagnostics(false);
#endif
	UChunkWorldLayoutRuntimeComponent* CapacityRuntime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	CapacityRuntime->MaxCachedPlanningChunks = 1;
	CapacityRuntime->PlanningCenterSnapshot = Centers;
	CapacityRuntime->bRealizationDirty = false; // No loaded-readiness change in this bookkeeping fixture.
	ULayoutPlanningWindowStore* Store = CapacityRuntime->GetLayoutPlanningWindowStore();
	FPlannedLayoutSiteRecord Pending, Stored;
	FLayoutWorldBindingSiteFrontendSelection Frontend;
	Frontend.WorldBindingId = TEXT("CapacityBinding");
	Frontend.WorldBindingCandidateId = TEXT("CapacityCandidate");
	Frontend.BiomeRowName = TEXT("CapacityBiome");
	Pending.SetWorldBindingFrontendSelection(Frontend);
	FLayoutPlannedSiteReservationSourceSelection Reservation;
	Reservation.SiteCenterBlockWorldPos = FIntVector(64, 0, 0);
	Pending.SetPlannedSiteReservationSourceSelection(Reservation);
	TestTrue(TEXT("Capacity fixture registers a real store owner"), Store->UpsertPendingPlannedLayoutSiteRecord(Pending, Stored));
	const FString RootKey = Stored.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	TestTrue(TEXT("Capacity fixture accepts a waiting result"), Store->AcceptPlannedLayoutSiteRecord(RootKey, FLayoutSolveResult()));
	FLayoutBackgroundSolveHandle OwnedRoot;
	OwnedRoot.LayoutGroupId = 1;
	CapacityRuntime->PendingPlanningWindowSolveHandlesByRecordKey.Add(RootKey, OwnedRoot);
	TestEqual(TEXT("Solved-waiting root consumes one owner"), CapacityRuntime->CountAutomaticPlanningWork(), 1);
	CapacityRuntime->ContinuationRouteReservations.Add(TEXT("ExplicitRoute"), FLayoutContinuationRouteReservationState());
	TestEqual(TEXT("Explicit preview does not consume automatic allowance"), CapacityRuntime->CountAutomaticPlanningWork(), 1);
	TestFalse(TEXT("Farther incoming work cannot evict a nearer result"), CapacityRuntime->MakeAutomaticPlanningRoom(FIntVector(128, 0, 0)));
	TestTrue(TEXT("Approaching work evicts farther solved-waiting payload"), CapacityRuntime->MakeAutomaticPlanningRoom(FIntVector::ZeroValue));
	TestEqual(TEXT("Eviction releases retired owner capacity"), CapacityRuntime->CountAutomaticPlanningWork(), 0);
	TestNull(TEXT("Eviction releases the store payload"), Store->FindPlannedLayoutSiteRecord(RootKey));
	CapacityRuntime->PlanningAreaQueue->Configure(4, 1);
	CapacityRuntime->PlanningAreaQueue->Observe(Near, Size, true);
	TestTrue(TEXT("HUD fixture starts one screening area"), CapacityRuntime->PlanningAreaQueue->TakeNext(Centers, Area));
	TestTrue(TEXT("HUD fixture occupies its shared working slot"), CapacityRuntime->PlanningAreaQueue->MarkEligible(
		Area, CapacityRuntime->PlanningAreaQueue->GetScanId(Area)));
	const FString Status = CapacityRuntime->BuildPlanningWindowDebugStatusMessage();
	TestTrue(TEXT("Full working cache reports saturation without unfinished solves"), Status.Contains(TEXT("owners=0/1 frontier=1 areas backlog=1 chunks saturated=yes")));
	TestTrue(TEXT("HUD reports working slots separately"), Status.Contains(TEXT("working=1/1")));
	return true;
}
#endif
