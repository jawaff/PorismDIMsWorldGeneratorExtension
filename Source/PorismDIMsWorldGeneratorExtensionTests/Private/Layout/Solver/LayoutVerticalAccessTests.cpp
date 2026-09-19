// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessBuildsDerivedContractsTest,
	"PorismExtension.Layout.Solver.VerticalAccess.BuildsDerivedContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessCountsSnapshotOnlyCompositeLikeOrientationsTest,
	"PorismExtension.Layout.Solver.VerticalAccess.CountsSnapshotOnlyCompositeLikeOrientations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessShadowCellKeepsHorizontalExposureTest,
	"PorismExtension.Layout.Solver.VerticalAccess.ShadowCellKeepsHorizontalExposure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessCompositeOwnsDirectUpperLandingTest,
	"PorismExtension.Layout.Solver.VerticalAccess.CompositeOwnsDirectUpperLanding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessTerrainSeamRejectsOrdinaryLowerModulesTest,
	"PorismExtension.Layout.Solver.VerticalAccess.TerrainSeamRejectsOrdinaryLowerModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessProofSelectedHostRefinementPreservesContractTest,
	"PorismExtension.Layout.Solver.VerticalAccess.ProofSelectedHostRefinementPreservesContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessChildReplacementSelectsNonOverlappingHostAssignmentTest,
	"PorismExtension.Layout.Solver.VerticalAccess.ChildReplacementSelectsNonOverlappingHostAssignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessRouteCostOrdersHostAssignmentTest,
	"PorismExtension.Layout.Solver.VerticalAccess.RouteCostOrdersHostAssignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessRangeOwnershipUsesMinimumTest,
	"PorismExtension.Layout.Solver.VerticalAccess.RangeOwnershipUsesMinimum",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Proves child negotiation preserves authored Range bounds without turning maximum into required count. */
bool FLayoutVerticalAccessRangeOwnershipUsesMinimumTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("RangeVerticalAccessRoot");
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Range;
	Request.ProfileSnapshot.MinVerticalAccessCount = 2;
	Request.ProfileSnapshot.MaxVerticalAccessCount = 6;

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
		Cell.Cell = FIntVector(Index, 0, 0);
		Cell.Intent = ELayoutCellIntent::VerticalAccess;
	}

	const FLayoutRecursiveVerticalAccessSummary Summary =
		LayoutProfileSolverInternal::ResolveParentOnlyVerticalAccessOwnershipForTests(
			Request,
			PlannedCells);
	TestTrue(TEXT("Range ownership remains satisfied"), Summary.FailureReason.IsEmpty());
	TestEqual(TEXT("Range minimum is required provider count"), Summary.RequiredHostProviderCount, 2);
	TestEqual(TEXT("Only minimum providers are counted"), Summary.CountedParentProviderCount, 2);
	TestEqual(TEXT("Additional legal stairs remain route support"), Summary.RetainedParentRouteSupportVerticalAccessCells.Num(), 4);
	return true;
}

bool FLayoutVerticalAccessProofSelectedHostRefinementPreservesContractTest::RunTest(const FString& Parameters)
{
	FLayoutNegotiatedChildResponsibilityContract NegotiatedContract;
	NegotiatedContract.ParentRegionDebugPath = TEXT("Root");
	NegotiatedContract.ChildRegionDebugPath = TEXT("Root/Room");
	NegotiatedContract.HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
	NegotiatedContract.RequiredHostProviderCount = 1;
	NegotiatedContract.CountedParentProviderCount = 1;
	NegotiatedContract.CountedParentVerticalAccessCells = {FIntVector(1, 1, 0)};
	NegotiatedContract.RetainedParentRouteSupportVerticalAccessCells = {FIntVector(2, 2, 0)};

	FLayoutRecursiveVerticalAccessSummary ActualSummary;
	ActualSummary.RequiredHostProviderCount = 1;
	ActualSummary.ResolvedHostProviderCount = 1;
	ActualSummary.CountedParentProviderCount = 1;
	ActualSummary.CountedParentVerticalAccessCells = {FIntVector(3, 1, 0)};
	ActualSummary.RetainedParentRouteSupportVerticalAccessCells = {FIntVector(3, 2, 0)};

	FLayoutNegotiatedChildResponsibilityContract ActualContract = NegotiatedContract;
	ActualContract.CountedParentVerticalAccessCells = ActualSummary.CountedParentVerticalAccessCells;
	ActualContract.RetainedParentRouteSupportVerticalAccessCells =
		ActualSummary.RetainedParentRouteSupportVerticalAccessCells;
	TestTrue(
		TEXT("Validated proof-selected host cells replace negotiated representatives during final audit"),
		LayoutProfileSolverInternal::DoesProofSelectedParentHostRefinementPreserveResponsibilityContractForTests(
			NegotiatedContract,
			ActualSummary,
			ActualContract));

	FLayoutNegotiatedLevelCellSet& ChangedReplacement =
		ActualContract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	ChangedReplacement.Level = 0;
	ChangedReplacement.Cells = {FIntVector(4, 4, 0)};
	return TestFalse(
		TEXT("Host refinement cannot change child replacement authority"),
		LayoutProfileSolverInternal::DoesProofSelectedParentHostRefinementPreserveResponsibilityContractForTests(
			NegotiatedContract,
			ActualSummary,
			ActualContract));
}

bool FLayoutVerticalAccessChildReplacementSelectsNonOverlappingHostAssignmentTest::RunTest(const FString& Parameters)
{
	auto MakeOption = [](const FIntVector& LowerCell)
	{
		FLayoutVerticalAccessHostOption Option;
		Option.LowerCell = LowerCell;
		Option.UpperCell = LowerCell + FIntVector(0, 0, 1);
		return Option;
	};

	FLayoutVerticalAccessHostGroup LowerGroup;
	LowerGroup.GroupId = TEXT("MappedVerticalAccess_0_0_0");
	LowerGroup.Options = {
		MakeOption(FIntVector(4, 3, 0)),
		MakeOption(FIntVector(2, 2, 0)),
		MakeOption(FIntVector(3, 2, 0))};
	FLayoutVerticalAccessHostGroup UpperGroup;
	UpperGroup.GroupId = TEXT("MappedVerticalAccess_1_0_0");
	UpperGroup.Options = {
		MakeOption(FIntVector(2, 2, 1)),
		MakeOption(FIntVector(3, 3, 1))};
	const TArray<FLayoutVerticalAccessHostGroup> HostGroups = {LowerGroup, UpperGroup};

	TSet<FIntVector> ParentPlannedCells;
	for (const FLayoutVerticalAccessHostGroup& Group : HostGroups)
	{
		for (const FLayoutVerticalAccessHostOption& Option : Group.Options)
		{
			ParentPlannedCells.Add(Option.LowerCell);
			ParentPlannedCells.Add(Option.UpperCell);
		}
	}
	const TSet<FIntVector> ReservedCells = {FIntVector(4, 3, 0)};
	TArray<FLayoutVerticalAccessHostGroup> SelectedGroups;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Original host domains retain one complete assignment after child replacement"),
		LayoutProfileSolverInternal::TrySelectAdmittedParentVerticalAccessHostsForTests(
			HostGroups,
			ParentPlannedCells,
			ReservedCells,
			SelectedGroups,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	TestEqual(TEXT("Every host requirement freezes one assignment"), SelectedGroups.Num(), 2);
	TSet<FIntVector> OccupiedCells;
	for (const FLayoutVerticalAccessHostGroup& Group : SelectedGroups)
	{
		if (!TestEqual(TEXT("Selected host group contains one option"), Group.Options.Num(), 1))
		{
			return false;
		}
		const FLayoutVerticalAccessHostOption& Option = Group.Options[0];
		TestFalse(TEXT("Selected host lower cell avoids child replacement"), ReservedCells.Contains(Option.LowerCell));
		TestFalse(TEXT("Selected host upper cell avoids child replacement"), ReservedCells.Contains(Option.UpperCell));
		TestFalse(TEXT("Selected host lower cell does not overlap another stair"), OccupiedCells.Contains(Option.LowerCell));
		TestFalse(TEXT("Selected host upper cell does not overlap another stair"), OccupiedCells.Contains(Option.UpperCell));
		OccupiedCells.Add(Option.LowerCell);
		OccupiedCells.Add(Option.UpperCell);
	}
	return true;
}

/** Proves route cost outranks input order while same-seed host selection remains stable. */
bool FLayoutVerticalAccessRouteCostOrdersHostAssignmentTest::RunTest(const FString& Parameters)
{
	auto MakeOption = [](const FIntVector& LowerCell)
	{
		FLayoutVerticalAccessHostOption Option;
		Option.LowerCell = LowerCell;
		Option.UpperCell = LowerCell + FIntVector(0, 0, 1);
		return Option;
	};
	FLayoutVerticalAccessHostGroup Group;
	Group.GroupId = TEXT("RouteAwareHost");
	Group.Options = {
		MakeOption(FIntVector(1, 2, 0)),
		MakeOption(FIntVector(3, 2, 0))};
	TSet<FIntVector> PlannedCells;
	for (const FLayoutVerticalAccessHostOption& Option : Group.Options)
	{
		PlannedCells.Add(Option.LowerCell);
		PlannedCells.Add(Option.UpperCell);
	}

	const TArray<FIntVector> LowerTargets = {FIntVector(2, 2, 0)};
	const TArray<FIntVector> UpperTargets = {FIntVector(4, 2, 1)};
	TArray<FLayoutVerticalAccessHostGroup> Selected;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Route-aware host assignment succeeds"),
		LayoutProfileSolverInternal::TrySelectRouteAwareParentVerticalAccessHostsForTests(
			{Group},
			PlannedCells,
			{},
			LowerTargets,
			UpperTargets,
			77,
			Selected,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("One route-aware host group is selected"), Selected.Num(), 1);
	if (Selected.Num() == 1 && Selected[0].Options.Num() == 1)
	{
		TestEqual(TEXT("Shortest combined lower and upper route cost wins"), Selected[0].Options[0].LowerCell, FIntVector(3, 2, 0));
	}

	Group.Options.Swap(0, 1);
	TArray<FLayoutVerticalAccessHostGroup> Replay;
	TestTrue(
		TEXT("Reordered input host assignment succeeds"),
		LayoutProfileSolverInternal::TrySelectRouteAwareParentVerticalAccessHostsForTests(
			{Group},
			PlannedCells,
			{},
			LowerTargets,
			UpperTargets,
			77,
			Replay,
			FailureReason));
	if (Replay.Num() == 1 && Replay[0].Options.Num() == 1)
	{
		TestEqual(TEXT("Same seed ignores input order"), Replay[0].Options[0].LowerCell, FIntVector(3, 2, 0));
	}
	return true;
}

bool FLayoutVerticalAccessBuildsDerivedContractsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	FLayoutInternalAccessLink AccessLink;
	AccessLink.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	AccessLink.ToTraversalChannel = LayoutGameplayTags::TraversalSecondary;
	AccessLink.bBidirectional = true;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_DerivedVerticalAccess"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_DerivedVerticalAccess"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, MakeTags({LayoutGameplayTags::TraversalSecondary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		},
		FGameplayTagContainer(),
		{AccessLink});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Snapshot validation succeeds"), Snapshot.Validation.IsValid());
	TestTrue(TEXT("Derived vertical-access contracts exist"), Snapshot.DerivedVerticalAccessContracts.Num() > 0);
	TestTrue(TEXT("Derived vertical-access proof exists"), Snapshot.ProofRecords.ContainsByPredicate([](const FLayoutProofRecord& ProofRecord)
	{
		return ProofRecord.ProofKind == ELayoutProofKind::DerivedVerticalAccess;
	}));
	TestTrue(TEXT("Derived vertical-access contract carries lower and upper traversal channels"), Snapshot.DerivedVerticalAccessContracts.ContainsByPredicate([](const FLayoutDerivedVerticalAccessContract& Contract)
	{
		return Contract.ExitFaceDirection == ELayoutFaceDirection::PosZ
			&& Contract.SourceTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary)
			&& Contract.ExitTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalSecondary);
	}));

	return true;
}

bool FLayoutVerticalAccessCountsSnapshotOnlyCompositeLikeOrientationsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_SnapshotOnlyCompositeLike"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_SnapshotOnlyCompositeLike"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutModule_SnapshotOnlyCompositeLike"),
		Template,
		{ELayoutCellIntent::VerticalAccess, ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	FLayoutProfileSolveSnapshot ProfileSnapshot;
	const int32 BaselineOrientationCount = LayoutProfileSolverInternal::CountViableVerticalAccessOrientationsAtCellForTests(
		{Snapshot},
		ProfileSnapshot,
		FIntPoint(2, 1),
		{},
		FIntVector::ZeroValue);

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_SnapshotOnlyCarrier"));
	Snapshot.SourceModule = nullptr;

	const int32 ViableOrientationCount = LayoutProfileSolverInternal::CountViableVerticalAccessOrientationsAtCellForTests(
		{Snapshot},
		ProfileSnapshot,
		FIntPoint(2, 1),
		{},
		FIntVector::ZeroValue);

	TestTrue(TEXT("Baseline snapshot exposes at least one viable vertical-access orientation"), BaselineOrientationCount > 0);
	TestEqual(
		TEXT("Snapshot-only composite-like vertical-access candidates keep the same viable orientation count from the frozen face contract"),
		ViableOrientationCount,
		BaselineOrientationCount);

	return true;
}

bool FLayoutVerticalAccessShadowCellKeepsHorizontalExposureTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_ShadowCellContinuation"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_ShadowCellContinuation"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* FillerModule = CreateModule(
		Outer,
		TEXT("LayoutModule_ShadowCellContinuation_Filler"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	ULayoutModuleAsset* VerticalModule = CreateModule(
		Outer,
		TEXT("LayoutModule_ShadowCellContinuation_Vertical"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	ULayoutModuleAsset* DecoyVerticalModule = CreateModule(
		Outer,
		TEXT("LayoutModule_ShadowCellContinuation_DecoyVertical"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	ULayoutCompositeModuleAsset* CompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_ShadowCellContinuation"));
	{
		FLayoutCompositeModuleCell& RootFillerCell = CompositeModule->Cells.AddDefaulted_GetRef();
		RootFillerCell.Module = FillerModule;
		RootFillerCell.LocalCell = FIntVector(0, 0, 0);
		RootFillerCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& ShadowVerticalCell = CompositeModule->Cells.AddDefaulted_GetRef();
		ShadowVerticalCell.Module = VerticalModule;
		ShadowVerticalCell.LocalCell = FIntVector(1, 0, 0);
		ShadowVerticalCell.RelativeYawRotationSteps = 0;
	}

	const FLayoutValidationResult CompositeValidation = CompositeModule->ValidateCompositeModule();
	TestTrue(TEXT("Composite module validates"), CompositeValidation.IsValid());
	if (!CompositeValidation.IsValid())
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot CompositeSnapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(CompositeModule);
	TestTrue(TEXT("Composite snapshot validates"), CompositeSnapshot.Validation.IsValid());
	if (!CompositeSnapshot.Validation.IsValid())
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot DecoySnapshot = FLayoutProfileSolver::BuildModuleSnapshot(DecoyVerticalModule, 1);
	TestTrue(TEXT("Decoy snapshot validates"), DecoySnapshot.Validation.IsValid());
	if (!DecoySnapshot.Validation.IsValid())
	{
		return false;
	}

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.ModuleSnapshots = {CompositeSnapshot, DecoySnapshot};
	Context.FootprintSize = FIntPoint(3, 1);
	Context.ProfileSnapshot.LevelCount = 1;
	Context.PlannedCellIntents.Add(FIntVector(0, 0, 0), ELayoutCellIntent::Boundary);
	Context.PlannedCellIntents.Add(FIntVector(1, 0, 0), ELayoutCellIntent::VerticalAccess);
	Context.PlannedCellIntents.Add(FIntVector(2, 0, 0), ELayoutCellIntent::Interior);
	LayoutProfileSolverInternal::BuildOrientedVariantsForTests(Context);

	int32 CompositeVariantCount = 0;
	for (const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant :
		Context.Variants)
	{
		CompositeVariantCount += Variant.ModuleSnapshotIndex == 0;
	}
	TestEqual(
		TEXT("Multi-cell composite preserves every allowed yaw because local-cell face contracts rotate"),
		CompositeVariantCount,
		CompositeSnapshot.AllowedYawRotationSteps.Num());
	TestTrue(TEXT("Composite and decoy snapshots build oriented variants"), Context.Variants.Num() >= 2);
	if (Context.Variants.Num() < 2)
	{
		return false;
	}

	int32 CompositeVariantIndex = INDEX_NONE;
	int32 DecoyVariantIndex = INDEX_NONE;
	for (int32 VariantIndex = 0; VariantIndex < Context.Variants.Num(); ++VariantIndex)
	{
		if (Context.Variants[VariantIndex].ModuleSnapshotIndex == 0 && CompositeVariantIndex == INDEX_NONE)
		{
			CompositeVariantIndex = VariantIndex;
		}
		else if (Context.Variants[VariantIndex].ModuleSnapshotIndex == 1 && DecoyVariantIndex == INDEX_NONE)
		{
			DecoyVariantIndex = VariantIndex;
		}
	}

	TestTrue(TEXT("Composite snapshot variant exists"), CompositeVariantIndex != INDEX_NONE);
	TestTrue(TEXT("Decoy snapshot variant exists"), DecoyVariantIndex != INDEX_NONE);
	if (CompositeVariantIndex == INDEX_NONE || DecoyVariantIndex == INDEX_NONE)
	{
		return false;
	}

	LayoutProfileSolverInternal::FSolveCandidate RootCandidate;
	RootCandidate.YawRotationSteps = Context.Variants[CompositeVariantIndex].YawRotationSteps;
	RootCandidate.VariantIndex = CompositeVariantIndex;
	RootCandidate.ModuleSnapshotIndex = Context.Variants[CompositeVariantIndex].ModuleSnapshotIndex;
	RootCandidate.ModuleSnapshotId = Context.Variants[CompositeVariantIndex].ModuleSnapshotId;
	RootCandidate.bEmpty = false;
	Context.InitialDomains.Add(FIntVector(0, 0, 0), {RootCandidate});

	LayoutProfileSolverInternal::FSolveCandidate ShadowDecoyCandidate;
	ShadowDecoyCandidate.YawRotationSteps = Context.Variants[DecoyVariantIndex].YawRotationSteps;
	ShadowDecoyCandidate.VariantIndex = DecoyVariantIndex;
	ShadowDecoyCandidate.ModuleSnapshotIndex = Context.Variants[DecoyVariantIndex].ModuleSnapshotIndex;
	ShadowDecoyCandidate.ModuleSnapshotId = Context.Variants[DecoyVariantIndex].ModuleSnapshotId;
	ShadowDecoyCandidate.bEmpty = false;
	Context.InitialDomains.Add(FIntVector(1, 0, 0), {ShadowDecoyCandidate});

	const int32 ShadowExposure = LayoutProfileSolverInternal::GetBestHorizontalTraversalExposureForCellForTests(
		Context,
		FIntVector(1, 0, 0));
	TestTrue(
		TEXT("Bundle-covered shadow vertical-access cell still exposes horizontal traversal from the covering composite candidate even when the shadow cell also has its own direct domain"),
		ShadowExposure > 0);

	return true;
}

/** Requires a legal upper endpoint instead of promoting GroundOnly content above its authored level. */
bool FLayoutVerticalAccessCompositeOwnsDirectUpperLandingTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_CompositeLanding"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutTemplate_CompositeLanding"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* LowerStair = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositeLanding_LowerStair"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceStair, MakeTags({LayoutGameplayTags::FaceStair}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});
	ULayoutModuleAsset* UpperLanding = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositeLanding_UpperLanding"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceStair, MakeTags({LayoutGameplayTags::FaceStair}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}))
		});
	ULayoutModuleAsset* Floor = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositeLanding_Floor"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* Wall = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositeLanding_Wall"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	ULayoutModuleAsset* Entry = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositeLanding_Entry"),
		Template,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(
		Outer,
		TEXT("LayoutComposite_CompositeLanding"));
	FLayoutCompositeModuleCell& LowerCell = Composite->Cells.AddDefaulted_GetRef();
	LowerCell.Module = LowerStair;
	LowerCell.LocalCell = FIntVector(0, 0, 0);
	FLayoutCompositeModuleCell& UpperCell = Composite->Cells.AddDefaulted_GetRef();
	UpperCell.Module = UpperLanding;
	UpperCell.LocalCell = FIntVector(0, 0, 1);

	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	TestTrue(TEXT("Self-contained VerticalAccess composite validates"), CompositeValidation.IsValid());
	if (!CompositeValidation.IsValid())
	{
		return false;
	}

	FLayoutModuleSolveSnapshot CompositeSnapshot = FLayoutProfileSolver::BuildCompositeModuleSnapshot(Composite);
	CompositeSnapshot.AllowedYawRotationSteps = {0};
	FLayoutModuleSolveSnapshot FloorSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(Floor, 1);
	FloorSnapshot.AllowedYawRotationSteps = {0};
	FloorSnapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	FLayoutModuleSolveSnapshot WallSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(Wall, 1);
	WallSnapshot.AllowedYawRotationSteps = {0};
	FLayoutModuleSolveSnapshot EntrySnapshot = FLayoutProfileSolver::BuildModuleSnapshot(Entry, 1);
	EntrySnapshot.AllowedYawRotationSteps = {0};
	FLayoutRegionSolveRequest Request;
	Request.Seed = 9017;
	Request.ProfileSnapshot.LevelCount = 2;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	FLayoutSparsePlacementRuleSolveSnapshot& PreserveRule = Request.ProfileSnapshot.SparsePlacementRules.AddDefaulted_GetRef();
	PreserveRule.RuleId = TEXT("GroundInteriorPreserve");
	PreserveRule.RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;
	PreserveRule.PlacementZone = ELayoutPlacementZone::Interior;
	PreserveRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
	Request.ModuleCatalog.Modules = {CompositeSnapshot, FloorSnapshot, WallSnapshot, EntrySnapshot};

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 5; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
				PlannedCell.Cell = FIntVector(X, Y, Z);
				PlannedCell.ModuleLevelIndex = Z;
				PlannedCell.Intent = X == 0 || X == 4 || Y == 0 || Y == 4
					? ELayoutCellIntent::Boundary
					: ELayoutCellIntent::Interior;
			}
		}
	}

	FString FailureReason;
	TestFalse(
		TEXT("Interior host cannot use GroundOnly Floor for its upper route endpoint"),
		LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
			Request,
			FIntPoint(5, 5),
			PlannedCells,
			FIntVector(2, 2, 0),
			FailureReason));
	if (!FailureReason.IsEmpty())
	{
		AddInfo(FailureReason);
	}

	FailureReason.Reset();
	TestFalse(
		TEXT("VerticalAccess host preflight rejects a fixed-axis composite whose only route opening points into ordinary Boundary shell"),
		LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
			Request,
			FIntPoint(5, 5),
			PlannedCells,
			FIntVector(1, 1, 0),
			FailureReason));
	TestTrue(
		TEXT("Route-ineligible host rejection names missing reciprocal endpoint"),
		FailureReason.Contains(TEXT("no exact reciprocal horizontal traversal endpoint")));

	ULayoutModuleAsset* WalkableWall = CreateModule(
		Outer, TEXT("LayoutModule_CompositeLanding_WalkableWall"), Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	Request.ModuleCatalog.Modules[2] = FLayoutProfileSolver::BuildModuleSnapshot(WalkableWall, 1);
	FailureReason.Reset();
	TestTrue(TEXT("Composite landing connects directly to a legal upper perimeter endpoint"),
		LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
			Request, FIntPoint(5, 5), PlannedCells, FIntVector(2, 3, 0), FailureReason));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);

	// A child-owned neighbor remains an internal carrier, not newly authored perimeter.
	TArray<FLayoutPlannedCell> ChildReservedPlan = PlannedCells;
	ChildReservedPlan.RemoveAll([](const FLayoutPlannedCell& Cell)
		{ return Cell.Cell == FIntVector(2, 2, 1); });
	Request.ExternalPlannedNeighborFaceMasks.Add(
		FIntVector(1, 2, 1), LayoutFaceDirectionMask(ELayoutFaceDirection::PosX));
	FailureReason.Reset();
	TestFalse(TEXT("Child replacement cannot manufacture a walkable upper perimeter endpoint"),
		LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
			Request, FIntPoint(5, 5), ChildReservedPlan, FIntVector(1, 1, 0), FailureReason));
	Request.ExternalPlannedNeighborFaceMasks.Reset();

	// Viable fixed-axis hosts sit at the far perimeter, beyond early entry-distance candidates.
	Request.FootprintSize = FIntPoint(20, 16);
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.VerticalAccessCount = 2;
	PlannedCells.Reset();
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < Request.FootprintSize.Y; ++Y)
		{
			for (int32 X = 0; X < Request.FootprintSize.X; ++X)
			{
				FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, Z);
				Cell.ModuleLevelIndex = Z;
				Cell.Intent = X == 0 || Y == 0 || X == 19 || Y == 15
					? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
			}
		}
	}
	PlannedCells[0].Intent = ELayoutCellIntent::Entry;
	TArray<FLayoutVerticalAccessHostGroup> HostGroups;
	FailureReason.Reset();
	TestTrue(TEXT("Prewarm finds legal distant perimeter hosts before discarding candidate domains"),
		LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			Request, PlannedCells, HostGroups, FailureReason));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);
	TestEqual(TEXT("Both exact host obligations have admitted alternatives"), HostGroups.Num(), 2);
	return true;
}

bool FLayoutVerticalAccessTerrainSeamRejectsOrdinaryLowerModulesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_TerrainSeamRole"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutTemplate_TerrainSeamVerticalRole"),
		FIntVector(8, 8, 8));
	const FGameplayTagContainer FaceTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer TraversalTags = MakeTags({LayoutGameplayTags::TraversalPrimary});

	ULayoutModuleAsset* OrdinaryBoundary = CreateModule(
		Outer,
		TEXT("LayoutModule_AAA_TerrainSeamOrdinaryBoundary"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FaceTags, FaceTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FaceTags, FaceTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	ULayoutModuleAsset* VerticalAccess = CreateModule(
		Outer,
		TEXT("LayoutModule_TerrainSeamVerticalAccess"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			FaceTags, FaceTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FaceTags, FaceTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(), FGameplayTagContainer(), TraversalTags));
	ULayoutModuleAsset* Landing = CreateModule(
		Outer,
		TEXT("LayoutModule_TerrainSeamLanding"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			FaceTags, FaceTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FaceTags, FaceTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(), TraversalTags));

	TArray<FLayoutRegionContentEntry> Entries;
	for (ULayoutModuleAsset* Module : {OrdinaryBoundary, VerticalAccess, Landing})
	{
		FLayoutRegionContentEntry& Entry = Entries.AddDefaulted_GetRef();
		Entry.EntryId = Module->GetFName();
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
	}
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_TerrainSeamVerticalRole"),
		Entries);
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_TerrainSeamVerticalRole"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);
	Profile->ContentSet = ContentSet;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		313,
		TEXT("VerticalAccess/TerrainSeamRole"));
	Request.FootprintSize = FIntPoint(1, 1);
	Request.PlannedCells.Reset();
	FLayoutPlannedCell& LowerCell = Request.PlannedCells.AddDefaulted_GetRef();
	LowerCell.Cell = FIntVector(0, 0, 0);
	LowerCell.Intent = ELayoutCellIntent::VerticalAccess;
	LowerCell.TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
	FLayoutPlannedCell& UpperCell = Request.PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(0, 0, 1);
	UpperCell.Intent = ELayoutCellIntent::Interior;

	const FLayoutIndexedDomainSnapshot Domains = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	if (!TestTrue(TEXT("Terrain-seam VerticalAccess probe builds indexed domains"), Domains.bSucceeded))
	{
		AddError(Domains.FailureReason);
		return false;
	}
	const FLayoutIndexedCellDomain* const LowerDomain = Domains.CellDomains.FindByPredicate([](const FLayoutIndexedCellDomain& Domain)
	{
		return Domain.Cell == FIntVector(0, 0, 0);
	});
	if (!TestNotNull(TEXT("Selected VerticalAccess lower domain exists"), LowerDomain))
	{
		return false;
	}
	const auto LowerDomainContains = [&Domains, LowerDomain](const FName ModuleName)
	{
		return LowerDomain->OrderedCandidateIndices.ContainsByPredicate([&Domains, ModuleName](const int32 CandidateIndex)
		{
			return Domains.Candidates.IsValidIndex(CandidateIndex)
				&& Domains.Candidates[CandidateIndex].ModuleDebugName == ModuleName;
		});
	};
	TestTrue(TEXT("Selected VerticalAccess lower keeps VerticalAccess-role module"), LowerDomainContains(VerticalAccess->GetFName()));
	TestFalse(TEXT("Terrain-seam Boundary seed cannot admit ordinary module on selected VerticalAccess lower"), LowerDomainContains(OrdinaryBoundary->GetFName()));
	return true;
}
