// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Misc/AutomationTest.h"

namespace
{
	bool StoreValidationContains(const FLayoutValidationResult& Result, const FString& Needle)
	{
		for (const FLayoutValidationMessage& Message : Result.Messages)
		{
			if (Message.Message.Contains(Needle))
			{
				return true;
			}
		}

		return false;
	}

	FPlannedLayoutSiteRecord MakePlanningStoreTestRecord(
		const FName WorldBindingId,
		const FName WorldBindingCandidateId,
		const FIntPoint ReservationKey,
		const FIntVector SiteCenterBlockWorldPos)
	{
		FPlannedLayoutSiteRecord Record;
		FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
		FrontendSelection.WorldBindingId = WorldBindingId;
		FrontendSelection.WorldBindingCandidateId = WorldBindingCandidateId;
		FrontendSelection.BiomeRowName = TEXT("Biome_Test");
		Record.SetWorldBindingFrontendSelection(FrontendSelection);
		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootCandidateId = WorldBindingCandidateId;
		PublicationMetadata.RootPlacementPolicyId = WorldBindingId;
		Record.SetRootPublicationMetadata(PublicationMetadata);
		FLayoutSiteSolveSourceSelection SiteSolveSourceSelection;
		SiteSolveSourceSelection.SolveSeed = 42;
		Record.SetSiteSolveSourceSelection(SiteSolveSourceSelection);
		FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection;
		ReservationSourceSelection.ReservationKey = ReservationKey;
		ReservationSourceSelection.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		ReservationSourceSelection.WorldSeed = 1337;
		Record.SetPlannedSiteReservationSourceSelection(ReservationSourceSelection);
		return Record;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreValidatesSettingsTest,
	"PorismExtension.Layout.Planning.WindowStore.ValidatesSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreValidatesSettingsTest::RunTest(const FString& Parameters)
{
	FLayoutPlanningWindowSettings Settings;
	TestTrue(TEXT("Default planning-window settings are valid"), Settings.Validate().IsValid());

	Settings.SampleSpacing = 0;
	const FLayoutValidationResult ValidationResult = Settings.Validate();

	TestFalse(TEXT("Invalid planning-window settings are rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Invalid settings report sample spacing"), StoreValidationContains(ValidationResult, TEXT("SampleSpacing")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreBuildsStableRecordKeysTest,
	"PorismExtension.Layout.Planning.WindowStore.BuildsStableRecordKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreBuildsStableRecordKeysTest::RunTest(const FString& Parameters)
{
	const FString FirstKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKey(
		TEXT("Binding_A"),
		TEXT("Candidate_A"),
		FLayoutResolvedWorldBindingContinuationSelection(),
		TEXT("Biome_A"),
		FIntPoint(1, 2),
		FIntVector(10, 20, 30),
		99);
	const FString SecondKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKey(
		TEXT("Binding_A"),
		TEXT("Candidate_A"),
		FLayoutResolvedWorldBindingContinuationSelection(),
		TEXT("Biome_A"),
		FIntPoint(1, 2),
		FIntVector(10, 20, 30),
		99);
	FLayoutWorldBindingSiteFrontendSelection EquivalentFrontendSelection;
	EquivalentFrontendSelection.WorldBindingId = TEXT("Binding_A");
	EquivalentFrontendSelection.WorldBindingCandidateId = TEXT("Candidate_A");
	EquivalentFrontendSelection.BiomeRowName = TEXT("Biome_A");
	FLayoutPlannedSiteReservationSourceSelection EquivalentReservationSourceSelection;
	EquivalentReservationSourceSelection.ReservationKey = FIntPoint(1, 2);
	EquivalentReservationSourceSelection.SiteCenterBlockWorldPos = FIntVector(10, 20, 30);
	EquivalentReservationSourceSelection.WorldSeed = 99;
	const FString FrontendSelectionKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromFrontendSelection(
		EquivalentFrontendSelection,
		FIntPoint(1, 2),
		FIntVector(10, 20, 30),
		99);
	const FString SelectionCarrierKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
		EquivalentFrontendSelection,
		EquivalentReservationSourceSelection);
	FLayoutResolvedWorldBindingContinuationSelection DifferentContinuationSelection;
	DifferentContinuationSelection.FamilyId = TEXT("BridgeFamily");
	DifferentContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	DifferentContinuationSelection.ResolvedEntryLevel = 2;
	FLayoutResolvedWorldBindingContinuationSelection DifferentPlacementKindSelection = DifferentContinuationSelection;
	DifferentPlacementKindSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	const FString DifferentKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKey(
		TEXT("Binding_A"),
		TEXT("Candidate_A"),
		FLayoutResolvedWorldBindingContinuationSelection(),
		TEXT("Biome_B"),
		FIntPoint(1, 2),
		FIntVector(10, 20, 30),
		99);
	const FString DifferentContinuationKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKey(
		TEXT("Binding_A"),
		TEXT("Candidate_A"),
		DifferentContinuationSelection,
		TEXT("Biome_A"),
		FIntPoint(1, 2),
		FIntVector(10, 20, 30),
		99);
	const FString DifferentPlacementKindKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKey(
		TEXT("Binding_A"),
		TEXT("Candidate_A"),
		DifferentPlacementKindSelection,
		TEXT("Biome_A"),
		FIntPoint(1, 2),
		FIntVector(10, 20, 30),
		99);

	TestEqual(TEXT("Stable record keys are deterministic"), SecondKey, FirstKey);
	TestEqual(TEXT("Frontend-selection key helper matches the loose-field key helper"), FrontendSelectionKey, FirstKey);
	TestEqual(TEXT("Carrier-based key helper matches the loose-field key helper"), SelectionCarrierKey, FirstKey);
	TestNotEqual(TEXT("Stable record keys include biome row identity"), DifferentKey, FirstKey);
	TestNotEqual(TEXT("Stable record keys include continuation selection identity"), DifferentContinuationKey, FirstKey);
	TestNotEqual(TEXT("Stable record keys include continuation placement kind identity"), DifferentPlacementKindKey, DifferentContinuationKey);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreUpsertsPendingRecordsIdempotentlyTest,
	"PorismExtension.Layout.Planning.WindowStore.UpsertsPendingRecordsIdempotently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStorePreservesPrecomputedPendingCarriersTest,
	"PorismExtension.Layout.Planning.WindowStore.PreservesPrecomputedPendingCarriers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreUpsertsPendingRecordsIdempotentlyTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord InsertedRecord;
	const bool bInsertedFirst = Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30)),
		InsertedRecord);

	FPlannedLayoutSiteRecord ExistingRecord;
	const bool bInsertedSecond = Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30)),
		ExistingRecord);

	TestTrue(TEXT("First upsert inserts the record"), bInsertedFirst);
	TestFalse(TEXT("Second upsert returns the existing record"), bInsertedSecond);
	const FLayoutPlannedSiteLifecycleMetadata InsertedLifecycleMetadata =
		InsertedRecord.GetPlannedSiteLifecycleMetadata();
	const FLayoutPlannedSiteLifecycleMetadata ExistingLifecycleMetadata =
		ExistingRecord.GetPlannedSiteLifecycleMetadata();
	const FLayoutRootPublicationMetadata InsertedPublicationMetadata =
		InsertedRecord.GetRootPublicationMetadata();
	TestEqual(TEXT("Duplicate upsert preserves the same stable key"), ExistingLifecycleMetadata.StableRecordKey, InsertedLifecycleMetadata.StableRecordKey);
	TestEqual<FLayoutId>(TEXT("Upsert does not synthesize a missing planned root solve id"), InsertedPublicationMetadata.RootSolveId, NAME_None);
	TestEqual(TEXT("Upsert preserves the planned root candidate id"), InsertedPublicationMetadata.RootCandidateId, FLayoutId(TEXT("Candidate_A")));
	TestEqual(TEXT("Upsert preserves the planned root placement-policy id"), InsertedPublicationMetadata.RootPlacementPolicyId, FLayoutId(TEXT("Binding_A")));
	TestEqual(TEXT("Upsert preserves the planned solve-source seed"), InsertedRecord.GetSiteSolveSourceSelection().SolveSeed, 42);
	TestEqual(TEXT("Upsert preserves the pending lifecycle state on the explicit carrier"), InsertedLifecycleMetadata.State, EPlannedLayoutSiteState::Pending);
	TestEqual(TEXT("Store contains one record after duplicate upsert"), Store->GetPlannedLayoutSiteRecords().Num(), 1);

	return true;
}

bool FLayoutPlanningWindowStorePreservesPrecomputedPendingCarriersTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord InputRecord =
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30));
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
		InputRecord.GetWorldBindingFrontendSelection();
	const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
		InputRecord.GetPlannedSiteReservationSourceSelection();
	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
	LifecycleMetadata.StableRecordKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
		FrontendSelection,
		ReservationSourceSelection);
	InputRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	FLayoutRootPublicationMetadata PublicationMetadata = InputRecord.GetRootPublicationMetadata();
	PublicationMetadata.RootSolveId = FLayoutId(*LifecycleMetadata.StableRecordKey);
	InputRecord.SetRootPublicationMetadata(PublicationMetadata);

	FPlannedLayoutSiteRecord InsertedRecord;
	const bool bInserted = Store->UpsertPendingPlannedLayoutSiteRecord(
		InputRecord,
		InsertedRecord);

	TestTrue(TEXT("Precomputed pending record inserts cleanly"), bInserted);
	TestEqual(
		TEXT("Store preserves the planner-owned stable record key"),
		InsertedRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey,
		LifecycleMetadata.StableRecordKey);
	TestEqual(
		TEXT("Store preserves the planner-owned root solve id"),
		InsertedRecord.GetRootPublicationMetadata().RootSolveId,
		PublicationMetadata.RootSolveId);
	TestEqual(
		TEXT("Store preserves the planner-owned pending lifecycle state"),
		InsertedRecord.GetPlannedSiteLifecycleMetadata().State,
		EPlannedLayoutSiteState::Pending);
	TestEqual(
		TEXT("Store preserves the planner-owned structured terrain-fit diagnostic default"),
		InsertedRecord.GetPlannedSiteLifecycleMetadata().TerrainFitDiagnosticKind,
		ELayoutWorldBindingTerrainFitDiagnosticKind::None);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreTracksFrozenSubmissionStateTest,
	"PorismExtension.Layout.Planning.WindowStore.TracksFrozenSubmissionState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreTracksFrozenSubmissionStateTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord Record;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30)),
		Record);
	const FString StableRecordKey = Record.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	TestFalse(
		TEXT("Pending record rejects descriptor-ready state without descriptor id"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady,
			NAME_None,
			7,
			1,
			12345));
	TestFalse(
		TEXT("Pending record rejects solve-queued state with invalid attempt"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::SolveQueued,
			TEXT("Region.Test.0"),
			7,
			-1,
			12345));
	TestFalse(
		TEXT("Pending record rejects completed-awaiting-publish state without audit hash"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::CompletedAwaitingPublish,
			TEXT("Region.Test.0"),
			7,
			1,
			0));
	TestFalse(
		TEXT("Pending record rejects solve-queued state before descriptor-ready state"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::SolveQueued,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));
	TestTrue(
		TEXT("Pending record accepts descriptor-ready state"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));
	TestFalse(
		TEXT("Descriptor-ready record rejects completed-awaiting-publish before solve-queued state"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::CompletedAwaitingPublish,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));

	FPlannedLayoutSiteRecord DescriptorReadyRecord;
	TestTrue(TEXT("Descriptor-ready record can be queried"), Store->TryGetPlannedLayoutSiteRecord(StableRecordKey, DescriptorReadyRecord));
	const FLayoutPlannedSiteLifecycleMetadata DescriptorReadyMetadata =
		DescriptorReadyRecord.GetPlannedSiteLifecycleMetadata();
	TestEqual(TEXT("Descriptor state is stored"), DescriptorReadyMetadata.FrozenSubmissionState, ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady);
	TestEqual(TEXT("Descriptor id is stored"), DescriptorReadyMetadata.FrozenSubmissionDescriptorId, FLayoutId(TEXT("Region.Test.0")));
	TestEqual(TEXT("Descriptor generation is stored"), DescriptorReadyMetadata.FrozenSubmissionGeneration, uint64(7));
	TestEqual(TEXT("Descriptor attempt is stored"), DescriptorReadyMetadata.FrozenSubmissionAttemptIndex, 1);
	TestEqual(TEXT("Descriptor audit hash is stored"), DescriptorReadyMetadata.FrozenSubmissionAuditHash, 12345);

	TestTrue(
		TEXT("Descriptor-ready record accepts solve-queued state with matching identity"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::SolveQueued,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));
	TestTrue(
		TEXT("Solve-queued record accepts completed-awaiting-publish state with matching identity"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::CompletedAwaitingPublish,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));
	TestFalse(
		TEXT("Completed-awaiting-publish record rejects descriptor-ready rewind"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));
	TestTrue(
		TEXT("Pending record accepts tombstone state"),
		Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::Tombstoned,
			TEXT("Region.Test.0"),
			7,
			1,
			12345));
	FPlannedLayoutSiteRecord TombstonedRecord;
	TestTrue(TEXT("Tombstoned record can be queried"), Store->TryGetPlannedLayoutSiteRecord(StableRecordKey, TombstonedRecord));
	TestEqual(
		TEXT("Tombstone state is stored"),
		TombstonedRecord.GetPlannedSiteLifecycleMetadata().FrozenSubmissionState,
		ELayoutPlannedSiteFrozenSubmissionState::Tombstoned);

	TestTrue(TEXT("Tombstoned record can reset for a new frozen submission attempt"), Store->ResetPlannedLayoutSiteRecordForFrozenSubmissionRetry(StableRecordKey));
	FPlannedLayoutSiteRecord RetryRecord;
	TestTrue(TEXT("Retry record can be queried"), Store->TryGetPlannedLayoutSiteRecord(StableRecordKey, RetryRecord));
	const FLayoutPlannedSiteLifecycleMetadata RetryMetadata = RetryRecord.GetPlannedSiteLifecycleMetadata();
	TestEqual(TEXT("Retry returns record to pending state"), RetryMetadata.State, EPlannedLayoutSiteState::Pending);
	TestEqual<FLayoutId>(TEXT("Retry clears descriptor id"), RetryMetadata.FrozenSubmissionDescriptorId, NAME_None);
	TestEqual(TEXT("Retry clears descriptor generation"), RetryMetadata.FrozenSubmissionGeneration, uint64(0));
	TestEqual(TEXT("Retry increments descriptor attempt"), RetryMetadata.FrozenSubmissionAttemptIndex, 2);
	TestEqual(TEXT("Retry clears descriptor audit hash"), RetryMetadata.FrozenSubmissionAuditHash, 0);

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.Seed = Record.GetSiteSolveSourceSelection().SolveSeed;
	TestTrue(TEXT("Tombstoned pending record can still be accepted by publish path"), Store->AcceptPlannedLayoutSiteRecord(StableRecordKey, SolveResult));
	FPlannedLayoutSiteRecord AcceptedRecord;
	TestTrue(TEXT("Accepted record can be queried"), Store->TryGetPlannedLayoutSiteRecord(StableRecordKey, AcceptedRecord));
	const FLayoutPlannedSiteLifecycleMetadata AcceptedMetadata = AcceptedRecord.GetPlannedSiteLifecycleMetadata();
	TestEqual<FLayoutId>(TEXT("Accept clears descriptor id"), AcceptedMetadata.FrozenSubmissionDescriptorId, NAME_None);
	TestEqual(TEXT("Accept marks frozen submission accepted"), AcceptedMetadata.FrozenSubmissionState, ELayoutPlannedSiteFrozenSubmissionState::Accepted);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreTransitionsRecordsIdempotentlyTest,
	"PorismExtension.Layout.Planning.WindowStore.TransitionsRecordsIdempotently",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreTransitionsRecordsIdempotentlyTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord Record;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30)),
		Record);

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.Seed = Record.GetSiteSolveSourceSelection().SolveSeed;
	SolveResult.FootprintSize = FIntPoint(3, 3);
	FLayoutPlacedModule& SolvePlacement = SolveResult.Placements.AddDefaulted_GetRef();
	SolvePlacement.Cell = FIntVector::ZeroValue;
	SolvePlacement.ModuleSnapshotId = TEXT("PlanningStoreTransition.Module");
	SolvePlacement.TemplatePath = FSoftObjectPath(TEXT("/Game/Automation/Layout/PlanningStoreTransitionTemplate.PlanningStoreTransitionTemplate"));
	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FrozenTerrainContract.ContractId = TEXT("PlanningStoreTransitionContract");
	FrozenTerrainContract.SiteCenterBlockWorldPos = FIntVector(10, 20, 30);
	FrozenTerrainContract.FootprintMinBlockWorldPos = FIntVector(8, 18, 30);
	FrozenTerrainContract.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	FrozenTerrainContract.FootprintSizeInCells = FIntPoint(3, 3);
	FrozenTerrainContract.DiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFlatFit;
	FLayoutContractActiveCellRecord& AcceptedActiveCell = FrozenTerrainContract.ActiveCells.AddDefaulted_GetRef();
	AcceptedActiveCell.Cell = FIntVector::ZeroValue;
	FLayoutContractActiveCellRecord& AcceptedPseudoCell = FrozenTerrainContract.ActiveCells.AddDefaulted_GetRef();
	AcceptedPseudoCell.Cell = FIntVector(0, 0, 1);

	const FLayoutPlannedSiteLifecycleMetadata InitialLifecycleMetadata =
		Record.GetPlannedSiteLifecycleMetadata();
	FLayoutFrozenTerrainContract MissingActiveCellsContract = FrozenTerrainContract;
	MissingActiveCellsContract.ActiveCells.Reset();
	TestFalse(TEXT("Pending record rejects accepted terrain contracts without solved-artifact metadata"), Store->AcceptPlannedLayoutSiteRecordWithFrozenTerrainContract(InitialLifecycleMetadata.StableRecordKey, SolveResult, FrozenTerrainContract));
	TestFalse(TEXT("Pending record rejects accepted terrain contracts without active-cell provenance"), Store->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(InitialLifecycleMetadata.StableRecordKey, SolveResult, MissingActiveCellsContract, TEXT("Solved.Artifact.Transition")));
	FLayoutFrozenTerrainContract MissingTerrainCellAuthorityContract = FrozenTerrainContract;
	FLayoutFrozenTerrainWriteRecord& UnauthorizedWrite = MissingTerrainCellAuthorityContract.TerrainWrites.AddDefaulted_GetRef();
	UnauthorizedWrite.BlockWorldPos = FIntVector(10, 20, 29);
	MissingTerrainCellAuthorityContract.CellContracts.Reset();
	TestFalse(TEXT("Pending record rejects accepted terrain writes without terrain cell contract authority"), Store->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(InitialLifecycleMetadata.StableRecordKey, SolveResult, MissingTerrainCellAuthorityContract, TEXT("Solved.Artifact.Transition")));
	// Regional artifact cardinality is no longer a checksum; the consumed array must still be nonempty.
	FLayoutSolveResult EmptySolveResult = SolveResult;
	EmptySolveResult.Placements.Reset();
	TestFalse(TEXT("Pending record rejects empty merged placements"), Store->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(InitialLifecycleMetadata.StableRecordKey, EmptySolveResult, FrozenTerrainContract, TEXT("Solved.Artifact.Transition")));
	TestTrue(TEXT("Pending record can be accepted"), Store->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(InitialLifecycleMetadata.StableRecordKey, SolveResult, FrozenTerrainContract, TEXT("Solved.Artifact.Transition")));
	TestTrue(TEXT("Accepted record can be accepted repeatedly"), Store->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(InitialLifecycleMetadata.StableRecordKey, SolveResult, FrozenTerrainContract, TEXT("Solved.Artifact.Transition")));
	FPlannedLayoutSiteRecord StaleAcceptedRecord;
	TestTrue(TEXT("Accepted record can be queried before realization"), Store->TryGetPlannedLayoutSiteRecord(InitialLifecycleMetadata.StableRecordKey, StaleAcceptedRecord));
	FLayoutPlannedSiteAcceptedSolvePayload StaleAcceptedPayload = StaleAcceptedRecord.GetPlannedSiteAcceptedSolvePayload();
	StaleAcceptedPayload.SolvedArtifactActiveCellCount += 1;
	StaleAcceptedRecord.SetPlannedSiteAcceptedSolvePayload(StaleAcceptedPayload);
	Store->ImportPlannedLayoutSiteRecords({StaleAcceptedRecord}, false);
	FPlannedLayoutSiteRecord RemovedStaleRecord;
	TestFalse(TEXT("Import removes existing accepted record with stale active-cell metadata"), Store->TryGetPlannedLayoutSiteRecord(InitialLifecycleMetadata.StableRecordKey, RemovedStaleRecord));
	TestFalse(TEXT("Accepted record rejects realization with stale active-cell metadata"), Store->MarkPlannedLayoutSiteRecordRealized(InitialLifecycleMetadata.StableRecordKey));
	StaleAcceptedPayload.SolvedArtifactActiveCellCount = FrozenTerrainContract.ActiveCells.Num();
	StaleAcceptedRecord.SetPlannedSiteAcceptedSolvePayload(StaleAcceptedPayload);
	Store->ImportPlannedLayoutSiteRecords({StaleAcceptedRecord}, false);
	FPlannedLayoutSiteRecord ImportedRecord;
	TestTrue(TEXT("Import accepts restored same-payload integrity"), Store->TryGetPlannedLayoutSiteRecord(InitialLifecycleMetadata.StableRecordKey, ImportedRecord));
	TestEqual(TEXT("Import preserves consumed merged placements"), ImportedRecord.SolveResult.Placements.Num(), SolveResult.Placements.Num());
	TestTrue(TEXT("Accepted record can be marked realized"), Store->MarkPlannedLayoutSiteRecordRealized(InitialLifecycleMetadata.StableRecordKey));
	TestTrue(TEXT("Realized record can be marked realized repeatedly"), Store->MarkPlannedLayoutSiteRecordRealized(InitialLifecycleMetadata.StableRecordKey));
	TestFalse(TEXT("Realized record cannot be rejected"), Store->RejectPlannedLayoutSiteRecord(InitialLifecycleMetadata.StableRecordKey, TEXT("Too late")));

	FPlannedLayoutSiteRecord RejectedAfterAcceptRecord;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_RejectedAfterAccept"), FIntPoint(9, 9), FIntVector(90, 90, 30)),
		RejectedAfterAcceptRecord);
	TestTrue(TEXT("Second record can be accepted before rejection"), Store->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(RejectedAfterAcceptRecord.StableRecordKey, SolveResult, FrozenTerrainContract, TEXT("Solved.Artifact.RejectedAfterAccept")));
	TestTrue(TEXT("Accepted record can be rejected before realization"), Store->RejectPlannedLayoutSiteRecord(RejectedAfterAcceptRecord.StableRecordKey, TEXT("Rejected before realization")));
	FPlannedLayoutSiteRecord RejectedAfterAcceptStoredRecord;
	TestTrue(TEXT("Rejected accepted record can be queried"), Store->TryGetPlannedLayoutSiteRecord(RejectedAfterAcceptRecord.StableRecordKey, RejectedAfterAcceptStoredRecord));
	const FLayoutPlannedSiteAcceptedSolvePayload RejectedAfterAcceptPayload = RejectedAfterAcceptStoredRecord.GetPlannedSiteAcceptedSolvePayload();
	TestEqual<FLayoutId>(TEXT("Rejected accepted record clears solved artifact id"), RejectedAfterAcceptPayload.SolvedArtifactId, NAME_None);
	TestTrue(TEXT("Rejected accepted record clears merged placements"), RejectedAfterAcceptPayload.SolveResult.Placements.IsEmpty());
	TestEqual<FLayoutId>(TEXT("Rejected accepted record clears frozen terrain contract id"), RejectedAfterAcceptPayload.FrozenTerrainContract.ContractId, NAME_None);

	FPlannedLayoutSiteRecord FinalRecord;
	TestTrue(TEXT("Final record can be queried"), Store->TryGetPlannedLayoutSiteRecord(InitialLifecycleMetadata.StableRecordKey, FinalRecord));
	TestEqual(
		TEXT("Final record is realized"),
		FinalRecord.GetPlannedSiteLifecycleMetadata().State,
		EPlannedLayoutSiteState::Realized);
	TestEqual(
		TEXT("Final record retains solved footprint authority through accept/realize transitions"),
		FinalRecord.GetPlannedSiteLifecycleMetadata().RetainedSolvedFootprintSize,
		SolveResult.FootprintSize);
	TestEqual(
		TEXT("Final record preserves the accepted terrain diagnostic from the frozen terrain contract"),
		FinalRecord.GetPlannedSiteLifecycleMetadata().TerrainFitDiagnosticKind,
		FrozenTerrainContract.DiagnosticKind);
	TestEqual<FLayoutId>(
		TEXT("Final record clears the frozen terrain contract after realization"),
		FinalRecord.GetPlannedSiteAcceptedSolvePayload().FrozenTerrainContract.ContractId,
		NAME_None);
	TestEqual(
		TEXT("Final record clears solved-artifact active-cell count after realization"),
		FinalRecord.GetPlannedSiteAcceptedSolvePayload().SolvedArtifactActiveCellCount,
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreRejectsRecordsTest,
	"PorismExtension.Layout.Planning.WindowStore.RejectsRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreRejectsRecordsTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord Record;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30)),
		Record);

	TestTrue(
		TEXT("Pending record can be rejected"),
		Store->RejectPlannedLayoutSiteRecord(
			Record.StableRecordKey,
			TEXT("Footprint overlap"),
			ELayoutWorldBindingTerrainFitDiagnosticKind::RejectedFoundationDepthExceeded));
	TestFalse(TEXT("Rejected record cannot be accepted"), Store->AcceptPlannedLayoutSiteRecord(Record.StableRecordKey, FLayoutSolveResult()));

	FPlannedLayoutSiteRecord FinalRecord;
	TestTrue(TEXT("Rejected record can be queried"), Store->TryGetPlannedLayoutSiteRecord(Record.StableRecordKey, FinalRecord));
	const FLayoutPlannedSiteLifecycleMetadata FinalLifecycleMetadata =
		FinalRecord.GetPlannedSiteLifecycleMetadata();
	TestEqual(TEXT("Final record is rejected"), FinalLifecycleMetadata.State, EPlannedLayoutSiteState::Rejected);
	TestEqual(TEXT("Rejection reason is preserved"), FinalLifecycleMetadata.RejectionReason, FString(TEXT("Footprint overlap")));
	TestEqual(
		TEXT("Structured rejection diagnostic is preserved"),
		FinalLifecycleMetadata.TerrainFitDiagnosticKind,
		ELayoutWorldBindingTerrainFitDiagnosticKind::RejectedFoundationDepthExceeded);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreUpdatesStructuredTerrainDiagnosticsTest,
	"PorismExtension.Layout.Planning.WindowStore.UpdatesStructuredTerrainDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreUpdatesStructuredTerrainDiagnosticsTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord Record;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(10, 20, 30)),
		Record);

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	TestTrue(TEXT("Pending record can be accepted before updating the structured diagnostic"), Store->AcceptPlannedLayoutSiteRecord(Record.StableRecordKey, SolveResult));
	TestTrue(
		TEXT("Accepted record structured terrain diagnostic can be updated without changing lifecycle state"),
		Store->UpdatePlannedLayoutSiteRecordTerrainFitDiagnostic(
			Record.StableRecordKey,
			ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFlatFit));

	FPlannedLayoutSiteRecord FinalRecord;
	TestTrue(TEXT("Updated accepted record can be queried"), Store->TryGetPlannedLayoutSiteRecord(Record.StableRecordKey, FinalRecord));
	const FLayoutPlannedSiteLifecycleMetadata FinalLifecycleMetadata =
		FinalRecord.GetPlannedSiteLifecycleMetadata();
	TestEqual(TEXT("Updated record remains accepted"), FinalLifecycleMetadata.State, EPlannedLayoutSiteState::Accepted);
	TestEqual(
		TEXT("Updated record preserves the structured terrain-fit diagnostic"),
		FinalLifecycleMetadata.TerrainFitDiagnosticKind,
		ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFlatFit);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreQueriesAcceptedOverlapsTest,
	"PorismExtension.Layout.Planning.WindowStore.QueriesAcceptedOverlaps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreQueriesAcceptedOverlapsTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord FirstRecord;
	FPlannedLayoutSiteRecord SecondRecord;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(100, 100, 0)),
		FirstRecord);
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_B"), TEXT("Candidate_B"), FIntPoint(5, 5), FIntVector(400, 400, 0)),
		SecondRecord);

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.FootprintSize = FIntPoint(4, 4);
	Store->AcceptPlannedLayoutSiteRecord(FirstRecord.StableRecordKey, SolveResult);
	Store->AcceptPlannedLayoutSiteRecord(SecondRecord.StableRecordKey, SolveResult);

	const TArray<FPlannedLayoutSiteRecord> OverlappingRecords =
		Store->GetAcceptedPlannedLayoutSiteRecordsOverlappingBlockBounds(
			FIntPoint(90, 90),
			FIntPoint(120, 120),
			FIntVector(16, 16, 16));

	TestEqual(TEXT("Overlap query returns one matching accepted record"), OverlappingRecords.Num(), 1);
	TestEqual(
		TEXT("Overlap query returns the first record"),
		OverlappingRecords[0].GetPlannedSiteLifecycleMetadata().StableRecordKey,
		FirstRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey);

	const TArray<FPlannedLayoutSiteRecord> NonOverlappingRecords =
		Store->GetAcceptedPlannedLayoutSiteRecordsOverlappingBlockBounds(
			FIntPoint(200, 200),
			FIntPoint(250, 250),
			FIntVector(16, 16, 16));

	TestEqual(TEXT("Non-overlap query returns no records"), NonOverlappingRecords.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStorePreservesClosureArtifactsTest,
	"PorismExtension.Layout.Planning.WindowStore.PreservesClosureArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStorePreservesClosureArtifactsTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());
	FPlannedLayoutSiteRecord Record;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("ClosureBinding"), TEXT("ClosureCandidate"), FIntPoint(1, 1), FIntVector(100, 100, 0)),
		Record);

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.FootprintSize = FIntPoint(1, 1);
	FLayoutClosureCoverageRecord& Coverage = SolveResult.ClosureCoverage.AddDefaulted_GetRef();
	Coverage.ClosureId = TEXT("OuterPerimeter");
	Coverage.RequiredSegmentCount = 1;
	Coverage.CoveredSegmentCount = 1;
	Coverage.bSatisfied = true;
	FLayoutClosureCoverageSegmentRecord& Segment = SolveResult.ClosureSegments.AddDefaulted_GetRef();
	Segment.ClosureId = Coverage.ClosureId;
	Segment.Cell = FIntVector(1, 2, 3);
	Segment.FaceDirection = ELayoutFaceDirection::PosX;
	Segment.bCovered = true;
	Segment.ProviderId = TEXT("Provider.Span");
	FLayoutClosureRunRecord& Run = SolveResult.ClosureRuns.AddDefaulted_GetRef();
	Run.ClosureId = Coverage.ClosureId;
	Run.FaceDirection = Segment.FaceDirection;
	Run.StartCell = Run.EndCell = Segment.Cell;
	Run.bCovered = true;
	Run.ProviderId = Segment.ProviderId;
	Run.SegmentCount = 1;

	TestTrue(TEXT("Accepted record stores closure artifact"), Store->AcceptPlannedLayoutSiteRecord(Record.StableRecordKey, SolveResult));
	FPlannedLayoutSiteRecord AcceptedRecord;
	if (!TestTrue(TEXT("Accepted closure artifact record is retrievable"), Store->TryGetPlannedLayoutSiteRecord(Record.StableRecordKey, AcceptedRecord)))
	{
		return false;
	}

	const FLayoutSolveResult& StoredResult = AcceptedRecord.GetPlannedSiteAcceptedSolvePayload().SolveResult;
	TestEqual(TEXT("Accepted artifact preserves one closure summary"), StoredResult.ClosureCoverage.Num(), 1);
	TestEqual(TEXT("Accepted artifact preserves one closure segment"), StoredResult.ClosureSegments.Num(), 1);
	TestEqual(TEXT("Accepted artifact preserves one closure run"), StoredResult.ClosureRuns.Num(), 1);
	if (StoredResult.ClosureCoverage.Num() == 0 || StoredResult.ClosureSegments.Num() == 0 || StoredResult.ClosureRuns.Num() == 0)
	{
		return false;
	}
	TestEqual(TEXT("Accepted artifact preserves closure satisfaction"), StoredResult.ClosureCoverage[0].bSatisfied, Coverage.bSatisfied);
	TestEqual(TEXT("Accepted artifact preserves closure segment identity"), StoredResult.ClosureSegments[0].Cell, Segment.Cell);
	TestEqual(TEXT("Accepted artifact preserves closure provider identity"), StoredResult.ClosureSegments[0].ProviderId, Segment.ProviderId);
	TestEqual(TEXT("Accepted artifact preserves closure run identity"), StoredResult.ClosureRuns[0].ProviderId, Run.ProviderId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreOverlapQueryIncludesRealizedRecordsTest,
	"PorismExtension.Layout.Planning.WindowStore.OverlapQueryIncludesRealizedRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreOverlapQueryIncludesRealizedRecordsTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());

	FPlannedLayoutSiteRecord Record;
	Store->UpsertPendingPlannedLayoutSiteRecord(
		MakePlanningStoreTestRecord(TEXT("Binding_A"), TEXT("Candidate_A"), FIntPoint(1, 1), FIntVector(100, 100, 0)),
		Record);

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.FootprintSize = FIntPoint(4, 4);
	TestTrue(TEXT("Pending record accepts before realization"), Store->AcceptPlannedLayoutSiteRecord(Record.StableRecordKey, SolveResult));
	TestTrue(TEXT("Accepted record realizes"), Store->MarkPlannedLayoutSiteRecordRealized(Record.StableRecordKey));

	FPlannedLayoutSiteRecord RealizedRecord;
	TestTrue(TEXT("Realized record remains queryable"), Store->TryGetPlannedLayoutSiteRecord(Record.StableRecordKey, RealizedRecord));
	TestEqual(
		TEXT("Realized record retains solved footprint authority after payload clear"),
		RealizedRecord.GetPlannedSiteLifecycleMetadata().RetainedSolvedFootprintSize,
		SolveResult.FootprintSize);
	TestEqual(
		TEXT("Realized record discards full solve payload"),
		RealizedRecord.GetPlannedSiteAcceptedSolvePayload().SolveResult.FootprintSize,
		FIntPoint::ZeroValue);

	const TArray<FPlannedLayoutSiteRecord> OverlappingRecords =
		Store->GetAcceptedPlannedLayoutSiteRecordsOverlappingBlockBounds(
			FIntPoint(90, 90),
			FIntPoint(120, 120),
			FIntVector(16, 16, 16));

	TestEqual(TEXT("Overlap query includes realized records"), OverlappingRecords.Num(), 1);
	if (OverlappingRecords.Num() == 1)
	{
		TestEqual(
			TEXT("Overlap query returns the realized record"),
			OverlappingRecords[0].GetPlannedSiteLifecycleMetadata().State,
			EPlannedLayoutSiteState::Realized);
	}
	TestEqual(TEXT("Relevant realized root keeps compact overlap authority"), Store->RemoveSettledRecordsWithoutInfluence({Record.StableRecordKey}), 0);
	TestEqual(TEXT("Irrelevant realized root releases metadata"), Store->RemoveSettledRecordsWithoutInfluence({}), 1);
	TestFalse(TEXT("Evicted root no longer appears in store"), Store->TryGetPlannedLayoutSiteRecord(Record.StableRecordKey, RealizedRecord));
	TestTrue(TEXT("Revisited record can enter a new retained lifetime"), Store->UpsertPendingPlannedLayoutSiteRecord(Record, RealizedRecord));
	TestEqual(TEXT("Unsettled work survives history pruning"), Store->RemoveSettledRecordsWithoutInfluence({}), 0);
	TestTrue(TEXT("Failure fixture settles as rejected"), Store->RejectPlannedLayoutSiteRecord(Record.StableRecordKey, TEXT("Failed candidate")));
	TestEqual(TEXT("Retained area keeps failure diagnostics"), Store->RemoveSettledRecordsWithoutInfluence({Record.StableRecordKey}), 0);
	TestEqual(TEXT("Evicted area releases failure diagnostics"), Store->RemoveSettledRecordsWithoutInfluence({}), 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlanningWindowStoreReservesReleasesAndConsumesEndpointPairTest,
	"PorismExtension.Layout.Planning.WindowStore.EndpointPairLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningWindowStoreReservesReleasesAndConsumesEndpointPairTest::RunTest(const FString& Parameters)
{
	ULayoutPlanningWindowStore* const Store = NewObject<ULayoutPlanningWindowStore>(GetTransientPackage());
	auto AddEndpoint = [Store](const TCHAR* Key, const TCHAR* RootKey, const FIntPoint ReservationKey, const FIntVector Position)
	{
		FLayoutPlanningWindowEndpointRecord Record;
		Record.StableEndpointKey = Key;
		Record.RootRecordKey = RootKey;
		Record.WorldBindingId = TEXT("Binding_A");
		Record.ContinuationFamilyId = TEXT("Family_A");
		Record.RemainingConnections = 1;
		Record.State = ELayoutContinuationEndpointState::Ready;
		Record.Endpoint.SiteReservationKey = ReservationKey;
		Record.Endpoint.RootRecordKey = RootKey;
		Record.Endpoint.LocalCell = FIntVector::ZeroValue;
		Record.Endpoint.EndpointBlockWorldPos = Position;
		Record.Endpoint.ConnectorTypeTag = FGameplayTag::RequestGameplayTag(TEXT("Layout.Connector.Test"), false);
		Record.Endpoint.ExposedEntryFaceDirection = ReservationKey.X == 1
			? ELayoutFaceDirection::PosX : ELayoutFaceDirection::NegX;
		FLayoutPlanningWindowEndpointRecord Stored;
		return Store->UpsertContinuationEndpointRecord(Record, Stored);
	};
	const FIntPoint SharedReservationKey(1, 0);
	TestTrue(TEXT("First endpoint publishes"), AddEndpoint(TEXT("RootA/Cell"), TEXT("RootA"), SharedReservationKey, FIntVector(0, 0, 0)));
	TestTrue(TEXT("Second endpoint in same reservation cell publishes"), AddEndpoint(TEXT("RootB/Cell"), TEXT("RootB"), SharedReservationKey, FIntVector(16, 0, 0)));

	const TArray<FLayoutPlanningWindowEndpointRecord> Endpoints = Store->GetContinuationEndpointRecords();
	FString EdgeKey;
	FString FailureReason;
	TestTrue(TEXT("Pair reserves"), Store->ReserveContinuationEndpointPair(
		Endpoints[0].Endpoint, Endpoints[1].Endpoint, TEXT("Family_A"), TEXT("Candidate_A"), EdgeKey, FailureReason));
	TestFalse(TEXT("Reserved pair cannot reserve twice"), Store->ReserveContinuationEndpointPair(
		Endpoints[0].Endpoint, Endpoints[1].Endpoint, TEXT("Family_A"), TEXT("Candidate_A"), EdgeKey, FailureReason));
	Store->ReleaseContinuationEndpointPair(EdgeKey);
	TestTrue(TEXT("Released pair reserves again"), Store->ReserveContinuationEndpointPair(
		Endpoints[0].Endpoint, Endpoints[1].Endpoint, TEXT("Family_A"), TEXT("Candidate_A"), EdgeKey, FailureReason));
	TestTrue(TEXT("Reserved pair consumes once"), Store->ConsumeContinuationEndpointPair(EdgeKey));
	TestTrue(TEXT("Committed consume is idempotent"), Store->ConsumeContinuationEndpointPair(EdgeKey));
	TestEqual(TEXT("Final capacity removes both endpoints"), Store->GetContinuationEndpointRecords().Num(), 0);
	FLayoutPlanningWindowEndpointRecord ReplayedPublication;
	TestFalse(TEXT("Replayed root publication cannot restore consumed endpoint capacity"),
		Store->UpsertContinuationEndpointRecord(Endpoints[0], ReplayedPublication));

	// Either retained endpoint keeps committed capacity; complete eviction permits a new visit.
	Store->RemoveContinuationEndpointRecordsOutsideWindows({FIntPoint(16, 0)}, {FIntPoint(16, 0)});
	TestFalse(TEXT("One relevant endpoint preserves both consumed keys"), Store->UpsertContinuationEndpointRecord(Endpoints[0], ReplayedPublication));
	Store->RemoveContinuationEndpointRecordsOutsideWindows({}, { FIntPoint::ZeroValue });
	TestTrue(TEXT("Mismatched bounds do not retire committed history"), Store->ConsumeContinuationEndpointPair(EdgeKey));
	Store->RemoveContinuationEndpointRecordsOutsideWindows({}, {});
	TestFalse(TEXT("No windows retire committed edge history"), Store->ConsumeContinuationEndpointPair(EdgeKey));
	TestTrue(TEXT("Complete eviction permits endpoint discovery on revisit"), Store->UpsertContinuationEndpointRecord(Endpoints[0], ReplayedPublication));
	TestEqual(TEXT("Revisited endpoint also releases outside coverage"), Store->RemoveContinuationEndpointRecordsOutsideWindows({}, {}), 1);
	TestTrue(TEXT("Unused endpoint C publishes"), AddEndpoint(TEXT("RootC/Cell"), TEXT("RootC"), SharedReservationKey, FIntVector(32, 0, 0)));
	TestTrue(TEXT("Unused endpoint D publishes"), AddEndpoint(TEXT("RootD/Cell"), TEXT("RootD"), SharedReservationKey, FIntVector(48, 0, 0)));
	const TArray<FLayoutPlanningWindowEndpointRecord> PendingEndpoints = Store->GetContinuationEndpointRecords();
	if (!TestEqual(TEXT("Two unused endpoints available"), PendingEndpoints.Num(), 2)) return false;
	TestTrue(TEXT("Unused pair reserves"), Store->ReserveContinuationEndpointPair(
		PendingEndpoints[0].Endpoint, PendingEndpoints[1].Endpoint, TEXT("Family_A"), TEXT("Candidate_A"), EdgeKey, FailureReason));
	TestEqual(TEXT("No windows remove both pending endpoints"), Store->RemoveContinuationEndpointRecordsOutsideWindows({}, {}), 2);
	TestEqual(TEXT("Endpoint ledger is empty"), Store->GetContinuationEndpointRecords().Num(), 0);
	TestFalse(TEXT("No windows retire reserved edge history"), Store->ConsumeContinuationEndpointPair(EdgeKey));
	TestEqual(TEXT("Repeated empty-window cleanup is idempotent"), Store->RemoveContinuationEndpointRecordsOutsideWindows({}, {}), 0);
	TestTrue(TEXT("Uncommitted endpoint can be rediscovered after eviction"), Store->UpsertContinuationEndpointRecord(PendingEndpoints[0], ReplayedPublication));
	return true;
}
