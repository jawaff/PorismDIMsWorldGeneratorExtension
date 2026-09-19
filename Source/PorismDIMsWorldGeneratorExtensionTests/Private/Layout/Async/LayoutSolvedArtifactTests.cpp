// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutSolvedArtifact.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Runtime/LayoutRealizationWritePlan.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutRegionSolveResult BuildSuccessfulSolvedResult(
		const FString& RegionDebugPath = TEXT("Root/Region"),
		const FIntVector RegionCellOffset = FIntVector(1, 2, 3),
		const FIntVector PlacementCell = FIntVector(2, 0, 1),
		const FLayoutId ModuleSnapshotId = TEXT("Module.Snapshot.0"))
	{
		FLayoutRegionSolveResult Result;
		Result.RegionDebugPath = RegionDebugPath;
		Result.RegionCellOffset = RegionCellOffset;
		Result.SolveResult.bSucceeded = true;
		Result.SolveResult.Seed = 123;
		Result.SolveResult.FootprintSize = FIntPoint(4, 5);
		Result.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 8);

		FLayoutPlacedModule Placement;
		Placement.Cell = PlacementCell;
		Placement.Intent = ELayoutCellIntent::Interior;
		Placement.SourceContentEntryId = TEXT("Content.Entry.0");
		Placement.ModuleSnapshotId = ModuleSnapshotId;
		Placement.TemplatePath = FSoftObjectPath(TEXT("/Game/Test/Templates/T_Module.T_Module"));
		Placement.YawRotationSteps = 2;
		Placement.OccupiedLocalCells = { FIntVector::ZeroValue };
		Result.SolveResult.Placements.Add(Placement);

		FLayoutValidationAssertionRecord Assertion;
		Assertion.AssertionId = TEXT("Assertion.Solved.0");
		Assertion.bPassed = true;
		Result.ValidationAssertions.Add(Assertion);
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactBuildsPointerFreeArtifactTest,
	"PorismExtension.Layout.Async.SolvedArtifact.BuildsPointerFreeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactBuildsPointerFreeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Successful pointer-free solve result builds solved artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.0"),
			BuildSuccessfulSolvedResult(),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Solved artifact marked present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Solved artifact starts solved-cell complete"), static_cast<uint8>(Artifact.Status), static_cast<uint8>(ELayoutSolvedArtifactStatus::SolvedCellsComplete));
	TestEqual(TEXT("Solved placement count copied"), Artifact.Placements.Num(), 1);
	TestEqual(TEXT("Solved placement owner path copied"), Artifact.Placements[0].RegionDebugPath, FString(TEXT("Root/Region")));
	TestEqual(TEXT("Solved placement owner offset copied"), Artifact.Placements[0].RegionCellOffset, FIntVector(1, 2, 3));
	TestEqual(TEXT("Solved template path copied"), Artifact.Placements[0].TemplatePath, FSoftObjectPath(TEXT("/Game/Test/Templates/T_Module.T_Module")));
	TestEqual(TEXT("Validation assertion copied"), Artifact.ValidationAssertions[0].AssertionId, FLayoutId(TEXT("Assertion.Solved.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactPreservesCompositeOccupiedCellDescriptorsTest,
	"PorismExtension.Layout.Async.SolvedArtifact.PreservesCompositeOccupiedCellDescriptors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactBuildsFromScheduleResultTest,
	"PorismExtension.Layout.Async.SolvedArtifact.BuildsFromScheduleResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactPreservesCompositeOccupiedCellDescriptorsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveResult Result = BuildSuccessfulSolvedResult();
	FLayoutPlacedModule& Placement = Result.SolveResult.Placements[0];
	Placement.TemplatePath.Reset();
	Placement.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(1, 0, 0)};
	for (const FIntVector& LocalCell : Placement.OccupiedLocalCells)
	{
		FLayoutPlacedLocalCellFaceRuleSnapshot& LocalCellDescriptor =
			Placement.LocalCellFaceRules.AddDefaulted_GetRef();
		LocalCellDescriptor.LocalCell = LocalCell;
		LocalCellDescriptor.TemplatePath = LocalCell == FIntVector::ZeroValue
			? FSoftObjectPath(TEXT("/Game/Test/Templates/T_Left.T_Left"))
			: FSoftObjectPath(TEXT("/Game/Test/Templates/T_Right.T_Right"));
		LocalCellDescriptor.RelativeYawRotationSteps = LocalCell.X;
		LocalCellDescriptor.Roles = {ELayoutModuleRole::Interior};
		LocalCellDescriptor.SupportedCellIntents = {ELayoutCellIntent::Interior};
	}

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(TEXT("Composite solve result with complete frozen local-cell descriptors builds an artifact"), LayoutSolvedArtifact::TryBuildFromSolveResult(
		TEXT("Artifact.CompositeDescriptors"),
		Result,
		Artifact,
		FailureReason));
	if (Artifact.Placements.IsEmpty())
	{
		return false;
	}
	TestEqual(TEXT("Composite artifact keeps one root placement"), Artifact.Placements.Num(), 1);
	TestEqual(TEXT("Composite artifact copies every occupied local-cell descriptor"), Artifact.Placements[0].OccupiedCellDescriptors.Num(), 2);
	TestEqual(TEXT("Composite artifact preserves right leaf template path"), Artifact.Placements[0].OccupiedCellDescriptors[1].TemplatePath, FSoftObjectPath(TEXT("/Game/Test/Templates/T_Right.T_Right")));
	TestEqual(TEXT("Composite artifact preserves right leaf relative yaw"), Artifact.Placements[0].OccupiedCellDescriptors[1].RelativeYawRotationSteps, 1);

	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FrozenTerrainContract.ContractId = TEXT("Contract.CompositeDescriptors");
	FLayoutContractActiveCellRecord ActiveCell;
	ActiveCell.Cell = Placement.Cell;
	FrozenTerrainContract.ActiveCells = {ActiveCell};
	FLayoutTerrainCellContractRecord CellContract;
	CellContract.Cell = ActiveCell.Cell;
	FrozenTerrainContract.CellContracts = {CellContract};
	FLayoutRealizationWritePlan WritePlan;
	TestTrue(TEXT("Composite write plan preserves frozen local-cell descriptors"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource::Site,
		TEXT("Artifact.CompositeDescriptors"),
		FrozenTerrainContract.ActiveCells.Num(),
		Result.SolveResult,
		FrozenTerrainContract,
		WritePlan,
		FailureReason));
	TestEqual(TEXT("Composite write plan keeps every frozen local-cell descriptor"), WritePlan.TemplatePlacements.Entries[0].LocalCellDescriptors.Num(), 2);

	Placement.LocalCellFaceRules.Reset();
	TestFalse(TEXT("Composite artifact rejects incomplete frozen local-cell descriptors"), LayoutSolvedArtifact::TryBuildFromSolveResult(
		TEXT("Artifact.CompositeDescriptorsMissing"),
		Result,
		Artifact,
		FailureReason));
	TestTrue(TEXT("Composite descriptor rejection reports missing complete metadata"), FailureReason.Contains(TEXT("complete composite local-cell descriptors")));
	return true;
}

bool FLayoutSolvedArtifactBuildsFromScheduleResultTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveScheduleResult ScheduleResult;
	ScheduleResult.bSucceeded = true;
	ScheduleResult.MergedSolveResult = BuildSuccessfulSolvedResult().SolveResult;
	FLayoutRegionSolveResult RegionResult = BuildSuccessfulSolvedResult();
	FLayoutZoneFeatureProviderCommitment& ProviderCommitment =
		RegionResult.SolveResult.ZoneFeatureProviderCommitments.AddDefaulted_GetRef();
	ProviderCommitment.RequirementId = TEXT("Room");
	ProviderCommitment.ProviderCommitmentId = TEXT("Provider.Room.Child0");
	ProviderCommitment.SourceContentEntryId = TEXT("RoomChild");
	ScheduleResult.RegionResults.Add(RegionResult);
	FLayoutChildStageMappingResult& StageMapping =
		ScheduleResult.ChildStageMappings.AddDefaulted_GetRef();
	StageMapping.MappingId = TEXT("ChildStage.Room.0");
	StageMapping.StageClass = ELayoutChildPlacementStageClass::InheritedStage;
	StageMapping.Cells.AddDefaulted_GetRef().SourceChildCell = FIntVector::ZeroValue;
	StageMapping.ChildLocalPlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;
	StageMapping.ParentTranslatedPlannedCells.AddDefaulted_GetRef().Cell = FIntVector(2, 2, 1);
	FLayoutPartitionSeamRecord& Seam =
		ScheduleResult.MergedSolveResult.PartitionSeams.AddDefaulted_GetRef();
	Seam.SeamId = TEXT("Seam.Parent.Child0");
	Seam.ParentRegionDebugPath = TEXT("Root/Region");
	Seam.OwnerRegionDebugPath = TEXT("Root/Region");
	Seam.PassiveRegionDebugPath = TEXT("Root/Child0");
	FLayoutOwnedSeamJunctionRequirement& Junction =
		ScheduleResult.PlannedJunctionRequirements.AddDefaulted_GetRef();
	Junction.JunctionRequirementId = TEXT("Junction.Parent.Child0");
	Junction.OwnerRegionDebugPath = TEXT("Root/Region");
	Junction.ContinuingSeamId = Seam.SeamId;
	Junction.BranchSeamId = TEXT("Seam.Parent.Child1");

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Successful schedule result builds solved artifact"),
		LayoutSolvedArtifact::TryBuildFromScheduleResult(
			TEXT("Artifact.Solved.Schedule.0"),
			TEXT("Fallback/Region"),
			ScheduleResult,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Schedule solved artifact marked present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Schedule region path copied from first region"), Artifact.RegionDebugPath, FString(TEXT("Root/Region")));
	TestEqual(TEXT("Schedule placement owner path copied"), Artifact.Placements[0].RegionDebugPath, FString(TEXT("Root/Region")));
	TestTrue(TEXT("Successful schedule artifact marks structural commitments authoritative"), Artifact.bStructuralCommitmentsAuthoritative);
	TestEqual(TEXT("Schedule artifact preserves provider id"), Artifact.ZoneFeatureProviderCommitments[0].ProviderCommitmentId, FLayoutId(TEXT("Provider.Room.Child0")));
	TestEqual(TEXT("Schedule artifact preserves child stage mapping id"), Artifact.ChildStageMappings[0].MappingId, FLayoutId(TEXT("ChildStage.Room.0")));
	TestEqual(TEXT("Schedule artifact preserves seam id"), Artifact.PartitionSeams[0].SeamId, FLayoutId(TEXT("Seam.Parent.Child0")));
	TestEqual(TEXT("Schedule artifact preserves junction id"), Artifact.JunctionRequirements[0].JunctionRequirementId, FLayoutId(TEXT("Junction.Parent.Child0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactPreservesScheduleRegionPlacementOwnersTest,
	"PorismExtension.Layout.Async.SolvedArtifact.PreservesScheduleRegionPlacementOwners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactPreservesScheduleRegionPlacementOwnersTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveScheduleResult ScheduleResult;
	ScheduleResult.bSucceeded = true;
	ScheduleResult.MergedSolveResult = BuildSuccessfulSolvedResult().SolveResult;
	ScheduleResult.RegionResults.Add(BuildSuccessfulSolvedResult(
		TEXT("Root/Region"),
		FIntVector(1, 2, 3),
		FIntVector(2, 0, 1),
		TEXT("Module.Snapshot.Root")));
	ScheduleResult.RegionResults.Add(BuildSuccessfulSolvedResult(
		TEXT("Root/ChildA"),
		FIntVector(9, 8, 7),
		FIntVector(0, 1, 0),
		TEXT("Module.Snapshot.Child")));

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Schedule artifact preserves placement owner metadata"),
		LayoutSolvedArtifact::TryBuildFromScheduleResult(
			TEXT("Artifact.Solved.Schedule.Multi"),
			TEXT("Fallback/Region"),
			ScheduleResult,
			Artifact,
			FailureReason));
	TestEqual(TEXT("Schedule artifact includes all region placements"), Artifact.Placements.Num(), 2);
	TestEqual(TEXT("First placement keeps parent region path"), Artifact.Placements[0].RegionDebugPath, FString(TEXT("Root/Region")));
	TestEqual(TEXT("Second placement keeps child region path"), Artifact.Placements[1].RegionDebugPath, FString(TEXT("Root/ChildA")));
	TestEqual(TEXT("Second placement keeps child region offset"), Artifact.Placements[1].RegionCellOffset, FIntVector(9, 8, 7));
	TestEqual(TEXT("Second placement keeps child snapshot id"), Artifact.Placements[1].ModuleSnapshotId, FLayoutId(TEXT("Module.Snapshot.Child")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactAttachesActiveCellProvenanceTest,
	"PorismExtension.Layout.Async.SolvedArtifact.AttachesActiveCellProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactAttachesActiveCellProvenanceTest::RunTest(const FString& Parameters)
{
	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Successful solve result builds solved artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.ActiveCells"),
			BuildSuccessfulSolvedResult(),
			Artifact,
			FailureReason));

	FLayoutContractActiveCellRecord RealCell;
	RealCell.Cell = FIntVector(0, 0, 0);
	FLayoutContractActiveCellRecord SecondRealCell;
	SecondRealCell.Cell = FIntVector(0, 0, 1);
	TestTrue(
		TEXT("Solved artifact accepts valid active-cell provenance"),
		LayoutSolvedArtifact::TryAttachActiveCellProvenance({RealCell, SecondRealCell}, Artifact, FailureReason));
	TestTrue(TEXT("Solved artifact records active-cell provenance flag"), Artifact.bHasActiveCellProvenance);
	TestEqual(TEXT("Solved artifact advances to write-plan-ready status"), static_cast<uint8>(Artifact.Status), static_cast<uint8>(ELayoutSolvedArtifactStatus::WritePlanReady));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactWriteInputsRequireActiveCellProvenanceTest,
	"PorismExtension.Layout.Async.SolvedArtifact.WriteInputsRequireActiveCellProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactWriteInputsRequireActiveCellProvenanceTest::RunTest(const FString& Parameters)
{
	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Successful solve result builds solved artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.WriteInputs"),
			BuildSuccessfulSolvedResult(),
			Artifact,
			FailureReason));

	FailureReason.Reset();
	TestFalse(
		TEXT("Write inputs reject solved artifacts without active-cell provenance"),
		LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(Artifact, FailureReason));
	TestTrue(TEXT("Missing active-cell provenance failure is reported"), FailureReason.Contains(TEXT("active-cell provenance")));

	FLayoutContractActiveCellRecord RealCell;
	RealCell.Cell = FIntVector(0, 0, 0);
	FLayoutContractActiveCellRecord SecondRealCell;
	SecondRealCell.Cell = FIntVector(0, 0, 1);
	TestTrue(
		TEXT("Solved artifact accepts active-cell provenance"),
		LayoutSolvedArtifact::TryAttachActiveCellProvenance({RealCell, SecondRealCell}, Artifact, FailureReason));
	FailureReason.Reset();
	TestTrue(
		TEXT("Write inputs accept validated active-cell provenance"),
		LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(Artifact, FailureReason));

	FLayoutSolvedArtifact StaleStatusArtifact = Artifact;
	StaleStatusArtifact.Status = ELayoutSolvedArtifactStatus::SolvedCellsComplete;
	FailureReason.Reset();
	TestFalse(
		TEXT("Write inputs reject solved artifacts that have not reached write-plan-ready status"),
		LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(StaleStatusArtifact, FailureReason));
	TestTrue(TEXT("Stale status failure is reported"), FailureReason.Contains(TEXT("WritePlanReady")));

	FLayoutSolvedArtifact RealOnlyArtifact = Artifact;
	RealOnlyArtifact.ActiveCells = {SecondRealCell};
	FailureReason.Reset();
	TestTrue(
		TEXT("Write inputs accept active-cell provenance with any Real cell"),
		LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(RealOnlyArtifact, FailureReason));

	// All active cells are Real — endpoint and VerticalAccess placements on bridge cells
	// pass validation normally since there is no longer a separate pseudo cell category.
	FLayoutSolvedArtifact EndpointArtifact;
	TestTrue(
		TEXT("Successful endpoint solve result builds solved artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.Endpoint"),
			BuildSuccessfulSolvedResult(TEXT("Root/Region"), FIntVector(1, 2, 3), SecondRealCell.Cell),
			EndpointArtifact,
			FailureReason));
	EndpointArtifact.Placements[0].Intent = ELayoutCellIntent::Entry;
	TestTrue(
		TEXT("Solved artifact accepts active-cell provenance with endpoint placement on second real cell"),
		LayoutSolvedArtifact::TryAttachActiveCellProvenance({RealCell, SecondRealCell}, EndpointArtifact, FailureReason));
	FailureReason.Reset();
	TestTrue(
		TEXT("Write inputs accept endpoint placements on real cells"),
		LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(EndpointArtifact, FailureReason));

	FLayoutSolvedArtifact VerticalAccessArtifact;
	TestTrue(
		TEXT("Successful VA solve result builds solved artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.VA"),
			BuildSuccessfulSolvedResult(TEXT("Root/Region"), FIntVector(1, 2, 3), SecondRealCell.Cell),
			VerticalAccessArtifact,
			FailureReason));
	VerticalAccessArtifact.Placements[0].Intent = ELayoutCellIntent::VerticalAccess;
	TestTrue(
		TEXT("Solved artifact accepts active-cell provenance with VA on second real cell"),
		LayoutSolvedArtifact::TryAttachActiveCellProvenance({RealCell, SecondRealCell}, VerticalAccessArtifact, FailureReason));
	FailureReason.Reset();
	TestTrue(
		TEXT("Write inputs accept VA placements on real cells"),
		LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(VerticalAccessArtifact, FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactBuildsRetainedPartialScheduleTest,
	"PorismExtension.Layout.Async.SolvedArtifact.BuildsRetainedPartialSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactBuildsRetainedPartialScheduleTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveScheduleResult ScheduleResult;
	ScheduleResult.bSucceeded = false;
	FLayoutRegionSolveResult RetainedPartialResult = BuildSuccessfulSolvedResult();
	RetainedPartialResult.SolveResult.bSucceeded = false;
	ScheduleResult.MergedSolveResult = RetainedPartialResult.SolveResult;
	FLayoutZoneFeatureProviderCommitment& PartialProvider =
		ScheduleResult.MergedSolveResult.ZoneFeatureProviderCommitments.AddDefaulted_GetRef();
	PartialProvider.RequirementId = TEXT("Room");
	PartialProvider.ProviderCommitmentId = TEXT("Partial.Provider.Room");
	FLayoutPartitionSeamRecord& PartialSeam =
		ScheduleResult.MergedSolveResult.PartitionSeams.AddDefaulted_GetRef();
	PartialSeam.SeamId = TEXT("Partial.Seam");
	FLayoutOwnedSeamJunctionRequirement& PartialJunction =
		ScheduleResult.PlannedJunctionRequirements.AddDefaulted_GetRef();
	PartialJunction.JunctionRequirementId = TEXT("Partial.Junction");
	ScheduleResult.MergedSolveResult.RegionalFailure.Scope =
		ELayoutRegionalFailureScope::Child;
	ScheduleResult.MergedSolveResult.RegionalFailure.Phase =
		TEXT("ChildPlacementDomain");
	ScheduleResult.MergedSolveResult.RegionalFailure.RegionDebugPath =
		TEXT("Root/Room");
	ScheduleResult.MergedSolveResult.RegionalFailure.SourceContentEntryId =
		TEXT("Room");
	ScheduleResult.MergedSolveResult.RegionalFailure.FirstCause =
		TEXT("Room child had no legal reciprocal placement.");

	// Failed recursion may retain its best frontier only on merged result;
	// stale region-local placements must not discard it during artifact creation.
	FLayoutRegionSolveResult StaleRegionResult = RetainedPartialResult;
	StaleRegionResult.SolveResult.Placements.Reset();
	ScheduleResult.RegionResults.Add(StaleRegionResult);

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Rejected schedule with retained placements builds a pointer-free artifact"),
		LayoutSolvedArtifact::TryBuildFromPartialScheduleResult(
			TEXT("Artifact.Partial.Schedule.0"),
			TEXT("Fallback/Region"),
			ScheduleResult,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Partial artifact is marked present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Partial artifact copies retained placement"), Artifact.Placements.Num(), 1);
	TestEqual(TEXT("Partial artifact retains provider diagnostics"), Artifact.ZoneFeatureProviderCommitments.Num(), 1);
	TestEqual(TEXT("Partial artifact retains seam diagnostics"), Artifact.PartitionSeams.Num(), 1);
	TestEqual(TEXT("Partial artifact retains junction diagnostics"), Artifact.JunctionRequirements.Num(), 1);
	TestTrue(TEXT("Partial artifact retains structured regional first cause"),
		Artifact.RegionalFailure.IsSet());
	TestEqual(TEXT("Partial artifact retains failed child source"),
		Artifact.RegionalFailure.SourceContentEntryId,
		FName(TEXT("Room")));
	TestFalse(TEXT("Partial artifact structural commitments remain non-authoritative"), Artifact.bStructuralCommitmentsAuthoritative);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRetainedPartialBuildsWritePlanTest,
	"PorismExtension.Layout.Async.SolvedArtifact.RetainedPartialBuildsWritePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRetainedPartialBuildsWritePlanTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult RetainedPartial = BuildSuccessfulSolvedResult().SolveResult;
	RetainedPartial.bSucceeded = false;

	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FrozenTerrainContract.ContractId = TEXT("Contract.Partial");
	FLayoutContractActiveCellRecord ActiveCell;
	ActiveCell.Cell = RetainedPartial.Placements[0].Cell;
	FrozenTerrainContract.ActiveCells = { ActiveCell };
	FLayoutTerrainCellContractRecord CellContract;
	CellContract.Cell = ActiveCell.Cell;
	FrozenTerrainContract.CellContracts = { CellContract };

	FLayoutRealizationWritePlan WritePlan;
	FString FailureReason;
	TestTrue(
		TEXT("Retained partial build preserves template authority"),
		LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Site,
			TEXT("Artifact.Partial"),
			FrozenTerrainContract.ActiveCells.Num(),
			RetainedPartial,
			FrozenTerrainContract,
			WritePlan,
			FailureReason));
	TestEqual(TEXT("Retained partial write plan preserves template placement"), WritePlan.TemplatePlacements.Entries.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutMergedPlacementAuthorityTest,
	"PorismExtension.Layout.Async.SolvedArtifact.MergedPlacementAuthority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutMergedPlacementAuthorityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveScheduleResult Schedule;
	Schedule.bSucceeded = true;
	Schedule.RegionResults = {BuildSuccessfulSolvedResult(TEXT("Root")), BuildSuccessfulSolvedResult(TEXT("Root/Child"))};
	// Region-local contributions overlap; the consumed merged payload owns one placement.
	Schedule.MergedSolveResult = Schedule.RegionResults[0].SolveResult;
	FLayoutSolvedArtifact Artifact;
	FString Failure;
	if (!TestTrue(TEXT("Regional artifact builds"), LayoutSolvedArtifact::TryBuildFromScheduleResult(
		TEXT("Artifact.MergedAuthority"), TEXT("Root"), Schedule, Artifact, Failure))) return false;
	TestEqual(TEXT("Artifact retains two regional contributions"), Artifact.Placements.Num(), 2);
	TestEqual(TEXT("Merged payload retains one placement"), Schedule.MergedSolveResult.Placements.Num(), 1);
	FLayoutFrozenTerrainContract Contract;
	Contract.ContractId = TEXT("Contract.MergedAuthority");
	FLayoutContractActiveCellRecord Active;
	Active.Cell = Schedule.MergedSolveResult.Placements[0].Cell;
	Contract.ActiveCells = {Active};
	for (const ELayoutRealizationWritePlanSource Source : {ELayoutRealizationWritePlanSource::Site, ELayoutRealizationWritePlanSource::Connector})
	{
		FLayoutRealizationWritePlan Plan;
		TestTrue(TEXT("Write plan consumes merged placements, not regional count"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			Source, Artifact.ArtifactId, Contract.ActiveCells.Num(), Schedule.MergedSolveResult, Contract, Plan, Failure));
		TestEqual(TEXT("Exactly one template write"), Plan.TemplatePlacements.Entries.Num(), 1);
		FLayoutSolveResult Invalid = Schedule.MergedSolveResult;
		Invalid.Placements[0].ModuleSnapshotId = NAME_None;
		TestFalse(TEXT("Missing module identity still rejects"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			Source, Artifact.ArtifactId, Contract.ActiveCells.Num(), Invalid, Contract, Plan, Failure));
		Invalid = Schedule.MergedSolveResult;
		Invalid.Placements[0].YawRotationSteps = 4;
		TestFalse(TEXT("Invalid yaw still rejects"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			Source, Artifact.ArtifactId, Contract.ActiveCells.Num(), Invalid, Contract, Plan, Failure));
		Invalid = Schedule.MergedSolveResult;
		Invalid.Placements.Add(Schedule.MergedSolveResult.Placements[0]);
		TestFalse(TEXT("Duplicate merged writes still reject"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			Source, Artifact.ArtifactId, Contract.ActiveCells.Num(), Invalid, Contract, Plan, Failure));
	}
	FLayoutPlannedSiteAcceptedSolvePayload Payload;
	Payload.SolveResult = Schedule.MergedSolveResult;
	Payload.FrozenTerrainContract = Contract;
	Payload.SolvedArtifactId = Artifact.ArtifactId;
	Payload.SolvedArtifactActiveCellCount = Contract.ActiveCells.Num();
	TestTrue(TEXT("Legacy regional count cannot reject merged realization inputs"),
		LayoutRealizationWritePlan::ValidateAcceptedSolvePayloadForRealizationInputs(Payload, Failure));
	Payload.SolvedArtifactActiveCellCount++;
	TestFalse(TEXT("Same-payload active-cell integrity remains enforced"),
		LayoutRealizationWritePlan::ValidateAcceptedSolvePayloadForRealizationInputs(Payload, Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactRejectsFailedSolveTest,
	"PorismExtension.Layout.Async.SolvedArtifact.RejectsFailedSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactRejectsFailedSolveTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveResult Result = BuildSuccessfulSolvedResult();
	Result.SolveResult.bSucceeded = false;

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Failed solve result cannot build solved artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.0"),
			Result,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Failed solve failure reported"), FailureReason.Contains(TEXT("successful solve")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactRejectsMissingSnapshotIdTest,
	"PorismExtension.Layout.Async.SolvedArtifact.RejectsMissingSnapshotId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactRejectsMissingSnapshotIdTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveResult Result = BuildSuccessfulSolvedResult();
	Result.SolveResult.Placements[0].ModuleSnapshotId = NAME_None;

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing module snapshot id fails closed"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.0"),
			Result,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing snapshot failure reported"), FailureReason.Contains(TEXT("module snapshot")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactRejectsMissingTemplatePathTest,
	"PorismExtension.Layout.Async.SolvedArtifact.RejectsMissingTemplatePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactRejectsMissingTemplatePathTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveResult Result = BuildSuccessfulSolvedResult();
	Result.SolveResult.Placements[0].TemplatePath = FSoftObjectPath();
	Result.SolveResult.Placements[0].OccupiedLocalCells.Reset();

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing template path fails closed"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Solved.0"),
			Result,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing template path failure reported"), FailureReason.Contains(TEXT("template path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSolvedArtifactPublishesEntryHandoffCarrierTest,
	"PorismExtension.Layout.Async.SolvedArtifactPublishesEntryHandoffCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSolvedArtifactPublishesEntryHandoffCarrierTest::RunTest(const FString& Parameters)
{
	// Build a solve result with one entry placement at boundary cell (0,1).
	FLayoutRegionSolveResult Result;
	Result.RegionDebugPath = TEXT("Root/Test");
	Result.SolveResult.bSucceeded = true;
	Result.SolveResult.FootprintSize = FIntPoint(3, 3);
	Result.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	Result.SolveResult.Seed = 42;

	FLayoutPlacedModule& EntryPlacement = Result.SolveResult.Placements.AddDefaulted_GetRef();
	EntryPlacement.Cell = FIntVector(0, 1, 0);
	EntryPlacement.Intent = ELayoutCellIntent::Entry;
	EntryPlacement.ModuleSnapshotId = TEXT("Entry.Module");
	EntryPlacement.TemplatePath = FSoftObjectPath(TEXT("/Game/Templates/EntryTemplate"));

	FLayoutSolvedArtifact Artifact;
	FString FailureReason;
	TestTrue(TEXT("Entry solve builds artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(
			TEXT("Artifact.Entry"), Result, Artifact, FailureReason));

	TestEqual(TEXT("Exposed entry cell is first entry placement"),
		Artifact.ExposedEntryCell, FIntVector(0, 1, 0));
	TestEqual(TEXT("Entry at X=0 exposes NegX face"),
		static_cast<int32>(Artifact.ExposedEntryFaceDirection),
		static_cast<int32>(ELayoutFaceDirection::NegX));

	return true;
}
