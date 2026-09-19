// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateSparseChildBoundaryTestOuter()
	{
		return CreatePackage(*FString::Printf(
			TEXT("/Temp/LayoutSparseChildBoundary_%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	/** Creates one ordinary module whose faces can border filled structure or untouched sparse terrain. */
	ULayoutModuleAsset* CreateSparseBoundaryModule(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<ELayoutCellIntent>& Intents)
	{
		const FGameplayTagContainer Open = MakeTags({LayoutGameplayTags::FaceOpen});
		return CreateModule(
			Outer,
			Name,
			CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Name), FIntVector(8, 8, 8)),
			Intents,
			BuildFilledCubeFaces(
				Open,
				Open,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				Open,
				Open,
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseChildBoundaryPreservesUpperInteriorEmptyNeighborTest,
	"PorismExtension.Layout.Solver.SparseChildBoundary.PreservesUpperInteriorEmptyNeighbor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Proves child boundary certification keeps adjacent upper Interior PreserveTerrain cells empty. */
bool FLayoutSparseChildBoundaryPreservesUpperInteriorEmptyNeighborTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSparseChildBoundaryTestOuter();
	ULayoutModuleAsset* ParentShell = CreateSparseBoundaryModule(
		Outer,
		TEXT("SparseBoundaryParentShell"),
		{ELayoutCellIntent::Boundary});
	ULayoutModuleAsset* ParentFloor = CreateSparseBoundaryModule(
		Outer,
		TEXT("SparseBoundaryParentFloor"),
		{ELayoutCellIntent::Interior, ELayoutCellIntent::Core, ELayoutCellIntent::Connector});
	ULayoutModuleAsset* ChildShell = CreateSparseBoundaryModule(
		Outer,
		TEXT("SparseBoundaryChildShell"),
		{ELayoutCellIntent::Boundary});

	FLayoutRegionContentEntry ChildModuleEntry;
	ChildModuleEntry.EntryId = TEXT("ChildShell");
	ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ChildModuleEntry.ModuleSettings.Module = ChildShell;
	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SparseBoundaryChildContentSet"),
		{ChildModuleEntry});
	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("SparseBoundaryChildProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;
	ChildProfile->bRequireAllTraversalChannelsReachable = false;

	FLayoutRegionContentEntry ParentShellEntry;
	ParentShellEntry.EntryId = TEXT("ParentShell");
	ParentShellEntry.ContentKind = ELayoutRegionContentKind::Module;
	ParentShellEntry.ModuleSettings.Module = ParentShell;
	ParentShellEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;

	FLayoutRegionContentEntry ParentFloorEntry;
	ParentFloorEntry.EntryId = TEXT("ParentFloor");
	ParentFloorEntry.ContentKind = ELayoutRegionContentKind::Module;
	ParentFloorEntry.ModuleSettings.Module = ParentFloor;
	ParentFloorEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;
	ParentFloorEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	FLayoutRegionContentEntry ChildRegionEntry;
	ChildRegionEntry.EntryId = TEXT("RequiredChild");
	ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildRegionEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;
	ChildRegionEntry.ChildRegionSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SparseBoundaryParentContentSet"),
		{ParentShellEntry, ParentFloorEntry, ChildRegionEntry});
	ULayoutProfileAsset* ParentProfile = CreateProfile(
		Outer,
		TEXT("SparseBoundaryParentProfile"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		2,
		0,
		false);
	ParentProfile->ContentSet = ParentContentSet;
	ParentProfile->bRequireAllTraversalChannelsReachable = false;
	FLayoutSparsePreserveTerrainRule PreserveRule;
	PreserveRule.RuleId = TEXT("PreserveInterior");
	PreserveRule.PlacementZone = ELayoutPlacementZone::Interior;
	PreserveRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
	ParentProfile->SparsePlacementRules.Add(FInstancedStruct::Make(PreserveRule));

	const FLayoutRegionSolveRequest RootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ParentContentSet,
		ParentProfile,
		1701,
		TEXT("SparseBoundaryParent"));
	const FLayoutRegionSolveScheduleResult Schedule = FLayoutProfileSolver::SolveRegionTree(RootRequest);
	if (!TestTrue(TEXT("Sparse parent and required child solve"), Schedule.bSucceeded))
	{
		AddError(Schedule.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* ParentResult = Schedule.RegionResults.FindByPredicate(
		[](const FLayoutRegionSolveResult& Result)
		{
			return Result.RegionDebugPath == TEXT("SparseBoundaryParent");
		});
	const FLayoutRegionSolveResult* ChildResult = Schedule.RegionResults.FindByPredicate(
		[](const FLayoutRegionSolveResult& Result)
		{
			return Result.SourceContentEntryId == TEXT("RequiredChild");
		});
	if (!TestNotNull(TEXT("Sparse parent result exists"), ParentResult)
		|| !TestNotNull(TEXT("Required child result exists"), ChildResult))
	{
		return false;
	}

	const FIntVector ChildUpperCell = ChildResult->RegionCellOffset + FIntVector(0, 0, 1);
	const FLayoutResidualCellRecord* AdjacentUpperResidual =
		ParentResult->SolveResult.ResidualUnoccupiedCells.FindByPredicate(
			[&ChildUpperCell](const FLayoutResidualCellRecord& Residual)
			{
				return Residual.Cell.Z == ChildUpperCell.Z
					&& FMath::Abs(Residual.Cell.X - ChildUpperCell.X)
						+ FMath::Abs(Residual.Cell.Y - ChildUpperCell.Y) == 1;
			});
	if (!TestNotNull(TEXT("Child keeps an adjacent upper Interior cell as sparse residual"), AdjacentUpperResidual))
	{
		return false;
	}
	TestEqual(TEXT("Adjacent residual keeps sparse rule provenance"), AdjacentUpperResidual->SourceSparsePlacementRuleId, FName(TEXT("PreserveInterior")));
	TestEqual(TEXT("Adjacent residual keeps Interior placement zone"), AdjacentUpperResidual->PlacementZone, ELayoutPlacementZone::Interior);
	TestEqual(TEXT("Adjacent residual keeps upper module level"), AdjacentUpperResidual->ModuleLevelIndex, 1);
	TestFalse(
		TEXT("Adjacent sparse residual has no structural placement"),
		ParentResult->SolveResult.Placements.ContainsByPredicate(
			[AdjacentUpperResidual](const FLayoutPlacedModule& Placement)
			{
				return Placement.Cell == AdjacentUpperResidual->Cell;
			}));
	return true;
}
