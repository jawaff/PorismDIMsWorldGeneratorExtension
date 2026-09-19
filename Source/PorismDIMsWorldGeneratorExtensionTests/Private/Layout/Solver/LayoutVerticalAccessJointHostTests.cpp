// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessJointLandingPrewarmTest,
	"PorismExtension.Layout.Solver.VerticalAccess.Prewarm.JointLandings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Proves neighboring composite landings without treating GroundOnly floor as upper support. */
bool FLayoutVerticalAccessJointLandingPrewarmTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_JointLandings"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("Cell"), FIntVector(8, 8, 8));
	TArray<FLayoutFaceRule> FloorFaces;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		FloorFaces.Add(MakeConnectionFaceRule(
			static_cast<ELayoutFaceDirection>(Index), LayoutGameplayTags::FaceOpen,
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			Index < 4 ? MakeTags({LayoutGameplayTags::TraversalPrimary}) : FGameplayTagContainer()));
	}
	TArray<FLayoutFaceRule> LowerFaces = FloorFaces;
	TArray<FLayoutFaceRule> UpperFaces = FloorFaces;
	TArray<FLayoutFaceRule> SolidFaces = FloorFaces;
	for (int32 Index = 0; Index < 6; ++Index)
	{
		const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(Index);
		FLayoutFaceRule& Lower = LowerFaces[Index];
		if (Direction != ELayoutFaceDirection::NegY && Direction != ELayoutFaceDirection::PosZ)
		{
			Lower.ConnectedTraversalChannels.Reset();
		}
		else
		{
			Lower.OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
			Lower.ConnectedTraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
		}
		FLayoutFaceRule& Upper = UpperFaces[Index];
		if (Direction == ELayoutFaceDirection::NegY || Direction == ELayoutFaceDirection::NegZ)
		{
			Upper.OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
			Upper.ConnectedTraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
		}
		if (Direction == ELayoutFaceDirection::PosZ) Upper.ConnectedTraversalChannels.Reset();
		SolidFaces[Index].ConnectionTag = LayoutGameplayTags::FaceSolid;
		SolidFaces[Index].AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});
		SolidFaces[Index].ConnectedTraversalChannels.Reset();
	}
	ULayoutModuleAsset* Lower = CreateModule(Outer, TEXT("Lower"), Template, {ELayoutCellIntent::VerticalAccess}, LowerFaces);
	ULayoutModuleAsset* Upper = CreateModule(Outer, TEXT("Upper"), Template, {ELayoutCellIntent::Interior}, UpperFaces);
	ULayoutModuleAsset* Floor = CreateModule(Outer, TEXT("Floor"), Template, {ELayoutCellIntent::Interior, ELayoutCellIntent::Core}, FloorFaces);
	ULayoutModuleAsset* Solid = CreateModule(Outer, TEXT("Solid"), Template, {ELayoutCellIntent::Boundary}, SolidFaces);
	ULayoutModuleAsset* Perimeter = CreateModule(Outer, TEXT("Perimeter"), Template, {ELayoutCellIntent::Boundary}, FloorFaces);
	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("Stairs"));
	FLayoutCompositeModuleCell& LowerPart = Composite->Cells.AddDefaulted_GetRef();
	LowerPart.Module = Lower;
	LowerPart.LocalCell = FIntVector::ZeroValue;
	FLayoutCompositeModuleCell& UpperPart = Composite->Cells.AddDefaulted_GetRef();
	UpperPart.Module = Upper;
	UpperPart.LocalCell = FIntVector(0, 0, 1);

	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("JointLandingPrewarm");
	Request.Seed = 77;
	Request.FootprintSize = FIntPoint(4, 4);
	Request.ProfileSnapshot.LevelCount = 2;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::None;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.VerticalAccessCount = 2;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	FLayoutSparsePlacementRuleSolveSnapshot& Preserve = Request.ProfileSnapshot.SparsePlacementRules.AddDefaulted_GetRef();
	Preserve.RuleId = TEXT("PreservedInterior");
	Preserve.RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;
	Preserve.PlacementZone = ELayoutPlacementZone::Interior;
	Preserve.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
	Request.ModuleCatalog.Modules = {
		FLayoutProfileSolver::BuildCompositeModuleSnapshot(Composite),
		FLayoutProfileSolver::BuildModuleSnapshot(Floor, 1),
		FLayoutProfileSolver::BuildModuleSnapshot(Solid, 1),
		FLayoutProfileSolver::BuildModuleSnapshot(Perimeter, 1)};
	Request.ModuleCatalog.Modules[0].AllowedYawRotationSteps = {0, 2};
	Request.ModuleCatalog.Modules[1].LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	Request.ModuleCatalog.Modules[2].LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	Request.ModuleCatalog.Modules[3].LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 4; ++Y)
		{
			for (int32 X = 0; X < 4; ++X)
			{
				FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, Z);
				Cell.ModuleLevelIndex = Z;
				Cell.Intent = X == 0 || Y == 0 || X == 3 || Y == 3
					? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
			}
		}
	}

	TArray<FLayoutPlannedCell> TrialCells = Request.PlannedCells;
	TArray<FLayoutVerticalAccessHostGroup> GeneratedGroups;
	FString FailureReason;
	TestTrue(TEXT("Prewarm retains hosts supported by neighboring composite landings"),
		LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			Request, TrialCells, GeneratedGroups, FailureReason));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);

	FLayoutRegionSolveRequest Exact1Request = Request;
	Exact1Request.ProfileSnapshot.VerticalAccessCount = 1;
	TArray<FLayoutPlannedCell> Exact1Cells = Request.PlannedCells;
	TArray<FLayoutVerticalAccessHostGroup> Exact1Groups;
	TestFalse(TEXT("Exact1 cannot grow another counted stair to supply mandatory upper support"),
		LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			Exact1Request, Exact1Cells, Exact1Groups, FailureReason));

	if (GeneratedGroups.Num() == 2)
	{
		FLayoutRegionSolveRequest ExactPairRequest = Request;
		ExactPairRequest.VerticalAccessHostGroups = GeneratedGroups;
		for (int32 Index = 0; Index < 2; ++Index)
		{
			ExactPairRequest.VerticalAccessHostGroups[Index].Options.RemoveAll(
				[Index](const FLayoutVerticalAccessHostOption& Option)
				{
					return Option.LowerCell != FIntVector(1, Index + 1, 0)
						|| Option.LowerYawRotationSteps != (Index == 0 ? 2 : 0);
				});
		}
		FLayoutRegionSolveRequest ExactPairFrozen;
		FailureReason.Reset();
		if (!TestTrue(TEXT("Generated exact witnesses retain joint composite support through freeze"),
			LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
				ExactPairRequest, false, {}, {}, ExactPairFrozen, FailureReason)))
		{
			AddError(FailureReason);
		}
	}

	FLayoutRegionSolveRequest BoundaryRequest = Request;
	BoundaryRequest.ProfileSnapshot.VerticalAccessCount = 1;
	BoundaryRequest.ModuleCatalog.Modules[0].PlacementZone = ELayoutPlacementZone::Perimeter;
	BoundaryRequest.ModuleCatalog.Modules[2] = FLayoutProfileSolver::BuildModuleSnapshot(Perimeter, 1);
	BoundaryRequest.ModuleCatalog.Modules[2].LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	BoundaryRequest.ModuleCatalog.Modules[2].SnapshotId = TEXT("GroundPerimeter");
	TArray<FLayoutPlannedCell> BoundaryCells = Request.PlannedCells;
	TArray<FLayoutVerticalAccessHostGroup> BoundaryGroups;
	FailureReason.Reset();
	if (!TestTrue(TEXT("Normal-zone eligible stair supplies Exact1 without Interior duplication"),
		LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			BoundaryRequest, BoundaryCells, BoundaryGroups, FailureReason)))
	{
		AddError(FailureReason);
	}
	TestEqual(TEXT("One requirement spans sparse and normal zones"), BoundaryGroups.Num(), 1);
	if (BoundaryGroups.Num() == 1 && TestTrue(TEXT("Boundary group retains exact alternatives"), !BoundaryGroups[0].Options.IsEmpty()))
	{
		TestFalse(TEXT("Perimeter-only provider never selects Interior root"),
			BoundaryGroups[0].Options.ContainsByPredicate([](const FLayoutVerticalAccessHostOption& Option)
			{
				return Option.LowerCell.X > 0 && Option.LowerCell.X < 3
					&& Option.LowerCell.Y > 0 && Option.LowerCell.Y < 3;
			}));
		const FLayoutVerticalAccessHostOption ContactOption = BoundaryGroups[0].Options[0];
		TArray<FLayoutPlannedCell> MissingContactCells = BoundaryCells;
		MissingContactCells.RemoveAll([&ContactOption](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell.Z == ContactOption.UpperCell.Z && Cell.Cell != ContactOption.UpperCell;
		});
		TestFalse(TEXT("Removing direct upper contacts invalidates the local stair witness"),
			LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
				BoundaryRequest, BoundaryRequest.FootprintSize, MissingContactCells, ContactOption.LowerCell, FailureReason));
		FLayoutRegionSolveRequest ChildContactRequest = BoundaryRequest;
		FLayoutNegotiatedChildResponsibilityContract& ChildContract = ChildContactRequest.NegotiatedChildResponsibilityContracts.AddDefaulted_GetRef();
		ChildContract.ParentRegionDebugPath = ChildContactRequest.RegionDebugPath;
		ChildContract.ChildRegionDebugPath = TEXT("JointLandingPrewarm/UpperChild");
		FLayoutNegotiatedLevelCellSet& ChildVolume = ChildContract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
		ChildVolume.Level = ContactOption.UpperCell.Z;
		for (const FLayoutPlannedCell& Cell : BoundaryCells)
		{
			if (Cell.Cell.Z == ChildVolume.Level && Cell.Cell != ContactOption.UpperCell) ChildVolume.Cells.Add(Cell.Cell);
		}
		TestFalse(TEXT("Child ownership cannot replace removed landing contacts without an exact child offer"),
			LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
				ChildContactRequest, ChildContactRequest.FootprintSize, MissingContactCells, ContactOption.LowerCell, FailureReason));
		BoundaryGroups[0].Options.SetNum(1);
		BoundaryRequest.VerticalAccessHostGroups = BoundaryGroups;
		FLayoutRegionSolveRequest BoundaryFrozen;
		if (!TestTrue(TEXT("Exact normal-zone provider has a prepared module proof"),
			LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
				BoundaryRequest, false, {}, {}, BoundaryFrozen, FailureReason)))
		{
			AddError(FailureReason);
		}
	}

	// Independently check that the production joint proof accepts this exact authored pair.
	for (int32 Y = 1; Y <= 2; ++Y)
	{
		FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
		Group.GroupId = FLayoutId(*FString::Printf(TEXT("JointHost%d"), Y));
		Group.DeckCell = FIntVector(1, Y, 1);
		FLayoutVerticalAccessHostOption& Option = Group.Options.AddDefaulted_GetRef();
		Option.LowerCell = FIntVector(1, Y, 0);
		Option.UpperCell = Group.DeckCell;
		Request.PlannedCells.FindByPredicate([&Option](const FLayoutPlannedCell& Cell)
			{ return Cell.Cell == Option.LowerCell; })->Intent = ELayoutCellIntent::VerticalAccess;
	}
	Request.bHasFinalizedSteppedTerrainIntents = true;
	FLayoutRegionSolveRequest Frozen;
	FailureReason.Reset();
	TestTrue(TEXT("Exact joint host assignment has a production prepared-prefix witness"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request, false, {}, {}, Frozen, FailureReason));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);

	Request.bHasFinalizedSteppedTerrainIntents = false;
	FailureReason.Reset();
	TestTrue(TEXT("Settling explicit host groups must not rerun generic host selection inside each proof attempt"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request, false, {}, {}, Frozen, FailureReason));
	if (!FailureReason.IsEmpty()) AddError(FailureReason);
	return true;
}
