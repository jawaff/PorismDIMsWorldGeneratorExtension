// Copyright 2026 Spotted Loaf Studio

#if WITH_AUTOMATION_TESTS

#include "Layout/Types/LayoutId.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"

namespace
{
	FLayoutPlannedCell MakeCell(
		const int32 X,
		const int32 Y,
		const int32 Z,
		const ELayoutCellIntent Intent,
		const int32 ModuleLevelIndex,
		const bool bIsBridgeCell,
		const ELayoutPlacementZone PlacementZone,
		const uint8 TerrainSeamFaceMask,
		const ELayoutEntryOrigin EntryOrigin = ELayoutEntryOrigin::None)
	{
		FLayoutPlannedCell Cell;
		Cell.Cell = FIntVector(X, Y, Z);
		Cell.Intent = Intent;
		Cell.ModuleLevelIndex = ModuleLevelIndex;
		Cell.bIsBridgeCell = bIsBridgeCell;
		Cell.PlacementZone = PlacementZone;
		Cell.TerrainSeamFaceMask = TerrainSeamFaceMask;
		Cell.EntryOrigin = EntryOrigin;
		return Cell;
	}

	/** Reconstructs captured five-shift continuation topology without world sampling. */
	TArray<FLayoutPlannedCell> BuildCapturedFiveShiftPlan()
	{
		using enum ELayoutCellIntent;
		constexpr ELayoutPlacementZone ZoneCorner = ELayoutPlacementZone::Corner;
		constexpr ELayoutPlacementZone ZoneEdge = ELayoutPlacementZone::Edge;
		constexpr ELayoutPlacementZone ZoneInterior = ELayoutPlacementZone::Interior;
		return {
			MakeCell(0, 0, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(1, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(2, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(3, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(4, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(5, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(6, 0, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(0, 1, 0, Entry, 0, false, ZoneEdge, 0, ELayoutEntryOrigin::Continuation),
			MakeCell(1, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(2, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(3, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(4, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(5, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(6, 1, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(0, 2, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(1, 2, 0, Interior, 0, false, ZoneEdge, 0),
			MakeCell(2, 2, 0, Interior, 0, false, ZoneEdge, 0),
			MakeCell(3, 2, 0, Core, 0, false, ZoneEdge, 0),
			MakeCell(4, 2, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(5, 2, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(6, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(4, 3, 0, Interior, 0, false, ZoneEdge, 0),
			MakeCell(5, 3, 0, VerticalAccess, 0, false, ZoneInterior, 0),
			MakeCell(6, 3, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(4, 4, 0, Interior, 0, false, ZoneEdge, 0),
			MakeCell(5, 4, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(6, 4, 1, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(4, 5, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(5, 5, 0, Entry, 0, false, ZoneEdge, 0, ELayoutEntryOrigin::Continuation),
			MakeCell(6, 5, 1, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(6, 4, 0, Boundary, 0, true, ZoneEdge, 0),
			MakeCell(6, 5, 0, Boundary, 0, true, ZoneCorner, 0),
			MakeCell(5, 3, 1, Boundary, 1, true, ZoneCorner, 0),
			MakeCell(6, 3, 1, Boundary, 1, true, ZoneCorner, 0),
			MakeCell(5, 4, 1, Boundary, 1, true, ZoneCorner, 0)
		};
	}

	/** Reconstructs the captured low road corridor with a raised boundary-only side deck. */
	TArray<FLayoutPlannedCell> BuildCapturedBoundaryOnlySideDeckPlan()
	{
		using enum ELayoutCellIntent;
		constexpr ELayoutPlacementZone ZoneCorner = ELayoutPlacementZone::Corner;
		constexpr ELayoutPlacementZone ZoneEdge = ELayoutPlacementZone::Edge;
		constexpr ELayoutPlacementZone ZoneInterior = ELayoutPlacementZone::Interior;
		return {
			MakeCell(0, 0, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(1, 0, 0, Entry, 0, false, ZoneEdge, 0, ELayoutEntryOrigin::Continuation),
			MakeCell(2, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(3, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(4, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(5, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(6, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(7, 0, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(8, 0, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(0, 1, 1, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(1, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(2, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(3, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(4, 1, 0, Core, 0, false, ZoneInterior, 0),
			MakeCell(5, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(6, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(7, 1, 0, Interior, 0, false, ZoneInterior, 0),
			MakeCell(8, 1, 0, Entry, 0, false, ZoneEdge, 0, ELayoutEntryOrigin::Continuation),
			MakeCell(0, 2, 1, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(1, 2, 1, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(2, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(3, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(4, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(5, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(6, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(7, 2, 0, Boundary, 0, false, ZoneEdge, 0),
			MakeCell(8, 2, 0, Boundary, 0, false, ZoneCorner, 0),
			MakeCell(0, 1, 0, Boundary, 0, true, ZoneEdge, 0),
			MakeCell(0, 2, 0, Boundary, 0, true, ZoneCorner, 0),
			MakeCell(1, 2, 0, Boundary, 0, true, ZoneEdge, 0),
			MakeCell(1, 1, 1, Boundary, 1, true, ZoneEdge, 0),
			MakeCell(2, 1, 1, Boundary, 1, true, ZoneCorner, 0),
			MakeCell(2, 2, 1, Boundary, 1, true, ZoneCorner, 0)
		};
	}

	/** Supplies prewarm terrain evidence for the captured boundary-only continuation fringe. */
	void PopulateBoundaryOnlySideDeckAdapterInput(FLayoutRegionSolveRequest& Request)
	{
		FLayoutFrozenTerrainBiomeAdapterInput& Terrain = Request.FrozenTerrainBiomeAdapterInput;
		Request.bHasFrozenTerrainBiomeAdapterInput = true;
		Terrain = FLayoutFrozenTerrainBiomeAdapterInput();
		Terrain.ArtifactId = TEXT("BoundaryOnlySideDeckTerrain");
		Terrain.EligibleBiomeRowName = TEXT("Biome.Test");
		Terrain.ModePlanId = Request.SelectedModePlan.ModePlanId;
		Terrain.SiteCenterBlockWorldPos = FIntVector::ZeroValue;
		Terrain.FootprintMinBlockWorldPos = FIntVector::ZeroValue;
		Terrain.FootprintSizeInBlocks = Request.FootprintSize;
		Terrain.SearchMinBlockXY = FIntPoint::ZeroValue;
		Terrain.SearchMaxBlockXY = FIntPoint(8, 2);
		Terrain.SearchDepthBlocks = 16;
		Terrain.TerrainSampleGridSpacing = 1;
		Terrain.bHasFiniteSearchBounds = true;
		Terrain.bHasSampledColumnEvidence = true;
		Terrain.bHasSteppedSupportEvidence = true;
		Terrain.bHasFootprintClassificationEvidence = true;
		Terrain.bHasTerrainPlacementEvidence = true;
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 9; ++X)
			{
				const bool bRaisedBoundary =
					(X == 0 && (Y == 1 || Y == 2)) || (X == 1 && Y == 2);
				FLayoutTerrainSurfaceSample& Surface = Terrain.SurfaceSamples.AddDefaulted_GetRef();
				Surface.bIsValid = true;
				Surface.BlockXY = FIntPoint(X, Y);
				Surface.SurfaceBlockWorldPos = FIntVector(X, Y, bRaisedBoundary ? 10 : 5);
				FLayoutSteppedTerrainSupportSample& Support =
					Terrain.SteppedSupportSamples.AddDefaulted_GetRef();
				Support.LocalCell = FIntVector(X, Y, 0);
				Support.SupportSurfaceZ = bRaisedBoundary ? 10 : 5;
				Support.SnappedSupportFloorZ = bRaisedBoundary ? 5 : 0;
				Support.SnappedSupportCeilingZ = bRaisedBoundary ? 15 : 5;
				FLayoutTerrainPlacementCellEvidence& Placement =
					Terrain.TerrainPlacementCells.AddDefaulted_GetRef();
				Placement.Cell = FIntVector(X, Y, 0);
				Placement.bPlaceableForSelectedMode = true;
				Placement.bHasExcavationEvidence = bRaisedBoundary;
				Placement.bHasClearanceEvidence = bRaisedBoundary;
				Placement.bHasLocalOverlapZ = bRaisedBoundary;
				Placement.OverlapMinLocalZ = FLayoutLocalBlockCoord8(0);
				Placement.OverlapMaxLocalZ = FLayoutLocalBlockCoord8(4);
				Placement.TerrainStageIndex = bRaisedBoundary ? 1 : 0;
				Placement.VerticalShiftBlocks = bRaisedBoundary ? 5 : 0;
				Placement.ProvenanceId = FLayoutId(*FString::Printf(TEXT("BoundaryOnlySideDeck.%d.%d"), X, Y));
			}
		}
		FLayoutTerrainCellClassification& Classification =
			Terrain.FootprintClassification.CellClassifications.AddDefaulted_GetRef();
		Classification.BlockXY = FIntPoint::ZeroValue;
	}

	/** Builds one imported road request with production module snapshots. */
	bool BuildRoadFixtureRequest(
		const int32 Seed,
		const TCHAR* RegionDebugPath,
		FLayoutRegionSolveRequest& OutRequest,
		FString& OutError)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Road_DA_ContentSet_Road.json"));
		FLayoutProfileJsonFixtureAssets Assets;
		TArray<FString> ImportIssues;
		if (!FLayoutProfileJsonFixture::ImportFromFile(
			FixturePath,
			GetTransientPackage(),
			Assets,
			ImportIssues))
		{
			OutError = FString::Join(ImportIssues, TEXT("\n"));
			return false;
		}

		return FLayoutProfileJsonFixture::BuildImportedRootRequest(
			Assets,
			Seed,
			RegionDebugPath,
			OutRequest,
			FLayoutRootSolveBudgetSettings(),
			NAME_None,
			NAME_None,
			0,
			&OutError);
	}

	/** Supplies finalized active-cell and per-column stage authority for captured topology. */
	void PopulateCapturedTerrainAuthority(FLayoutRegionSolveRequest& Request)
	{
		Request.PrecomputedActiveCells.Reset();
		Request.PrecomputedFrozenTerrainContract = FLayoutFrozenTerrainContract();
		TSet<FIntPoint> SeenColumns;
		for (const FLayoutPlannedCell& PlannedCell : Request.PrecomputedPlannedCells)
		{
			FLayoutContractActiveCellRecord& ActiveCell =
				Request.PrecomputedActiveCells.AddDefaulted_GetRef();
			ActiveCell.Cell = PlannedCell.Cell;
			FLayoutContractActiveCellRecord& FrozenActiveCell =
				Request.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef();
			FrozenActiveCell.Cell = PlannedCell.Cell;
			FLayoutTerrainCellContractRecord& CellContract =
				Request.PrecomputedFrozenTerrainContract.CellContracts.AddDefaulted_GetRef();
			CellContract.Cell = PlannedCell.Cell;
			CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;

			const FIntPoint Column(PlannedCell.Cell.X, PlannedCell.Cell.Y);
			if (SeenColumns.Contains(Column))
			{
				continue;
			}
			SeenColumns.Add(Column);
			FLayoutFrozenTerrainStageCellRecord& Stage =
				Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
			Stage.FootprintCellXY = Column;
			Stage.TerrainStageIndex =
				(Column.X == 6 && (Column.Y == 4 || Column.Y == 5)) ? 1 : 0;
			Stage.VerticalShiftBlocks = Stage.TerrainStageIndex * 5;
			Stage.ResolvedStageBaseBlockWorldZ = 196 + Stage.VerticalShiftBlocks;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContinuationBoundaryOnlySideDeckDoesNotForceVerticalAccessTest,
	"PorismExtension.Layout.Solver.Continuation.BoundaryOnlySideDeckDoesNotForceVerticalAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContinuationFiveShiftHostFailureDiagnosticsTest,
	"PorismExtension.Layout.Solver.Continuation.FiveShiftHostFailureDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContinuationBoundaryOnlySideDeckDoesNotForceVerticalAccessTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	FString RequestError;
	if (!BuildRoadFixtureRequest(
		2011618119,
		TEXT("Continuation/BoundaryOnlySideDeck"),
		Request,
		RequestError))
	{
		AddError(RequestError);
		return false;
	}

	Request.FootprintSize = FIntPoint(9, 3);
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Request.RootContinuationSelection.FamilyId = TEXT("Road");
	Request.SelectedModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Request.SelectedModePlan.Scope = ELayoutContractRegionScope::Continuation;
	Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	Request.SelectedModePlan.SolveSeed = Request.Seed;
	Request.SelectedModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
	Request.SelectedModePlan.ModePlanId =
		FLayoutContractPipeline::BuildModePlanId(Request.SelectedModePlan);
	Request.bHasSelectedModePlan = true;
	Request.WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells = 3;
	Request.WorldBindingPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible = true;
	Request.SelectedModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
	Request.SelectedModePlan.ModePlanId =
		FLayoutContractPipeline::BuildModePlanId(Request.SelectedModePlan);

	TArray<FLayoutPlannedCell> Plan = BuildCapturedBoundaryOnlySideDeckPlan();
	Plan.RemoveAll([](const FLayoutPlannedCell& Cell)
	{
		return Cell.bIsBridgeCell;
	});
	for (FLayoutPlannedCell& Cell : Plan)
	{
		Cell.Cell.Z = Cell.ModuleLevelIndex;
	}
	Request.PlannedCells = Plan;
	PopulateBoundaryOnlySideDeckAdapterInput(Request);

	FString FailureReason;
	const bool bPrepared = FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, FailureReason);
	TestTrue(*FString::Printf(TEXT("Boundary-only continuation prewarm succeeds: %s"), *FailureReason), bPrepared);
	TestEqual(TEXT("Boundary-only continuation adds no mandatory VerticalAccess host"), Request.VerticalAccessHostGroups.Num(), 0);
	TestTrue(TEXT("Boundary-only continuation records excavation provenance"), Request.PrecomputedAdapterDiagnostics.ContainsByPredicate([](const FLayoutAdapterDiagnostic& Diagnostic)
	{
		return Diagnostic.Detail.Contains(TEXT("boundary-only continuation fringe"));
	}));
	TestFalse(TEXT("Boundary-only continuation keeps every authored cell on its authored level"), Request.PrecomputedPlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell.Z != Cell.ModuleLevelIndex || Cell.bIsBridgeCell;
	}));
	const FLayoutPlannedCell* CorridorCell = Request.PrecomputedPlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(2, 1, 0);
	});
	TestTrue(TEXT("Captured low corridor cell remains present"), CorridorCell != nullptr);
	if (CorridorCell != nullptr)
	{
		TestEqual(TEXT("Captured low corridor remains Interior"), CorridorCell->Intent, ELayoutCellIntent::Interior);
	}

	const FLayoutRegionSolveResult SolveResult = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(
		*FString::Printf(TEXT("Captured boundary-only side deck solves without a forced stair: %s"), *SolveResult.SolveResult.FailureReason),
		SolveResult.SolveResult.bSucceeded);
	return true;
}

bool FLayoutContinuationFiveShiftHostFailureDiagnosticsTest::RunTest(const FString& Parameters)
{
	const FString FixturePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Road_DA_ContentSet_Road.json"));
	FLayoutProfileJsonFixtureAssets Assets;
	TArray<FString> ImportIssues;
	if (!FLayoutProfileJsonFixture::ImportFromFile(
			FixturePath,
			GetTransientPackage(),
			Assets,
			ImportIssues))
	{
		AddError(FString::Join(ImportIssues, TEXT("\n")));
		return false;
	}

	FLayoutRegionSolveRequest Request;
	FString RequestError;
	if (!FLayoutProfileJsonFixture::BuildImportedRootRequest(
			Assets,
			1225628543,
			TEXT("Continuation/FiveShiftHostAlternatives"),
			Request,
			FLayoutRootSolveBudgetSettings(),
			NAME_None,
			NAME_None,
			0,
			&RequestError))
	{
		AddError(RequestError);
		return false;
	}

	Request.FootprintSize = FIntPoint(7, 6);
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Request.SelectedModePlan.Scope = ELayoutContractRegionScope::Continuation;
	Request.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	Request.bHasSelectedModePlan = true;
	Request.bHasFinalizedSteppedTerrainIntents = true;
	Request.PlannedCells = BuildCapturedFiveShiftPlan();
	Request.PrecomputedPlannedCells = Request.PlannedCells;
	PopulateCapturedTerrainAuthority(Request);

	FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
	Group.GroupId = TEXT("GeneratedDeckVerticalAccess_0_0");
	Group.DeckCell = FIntVector(5, 4, 1);
	FLayoutVerticalAccessHostOption& BlockingOption = Group.Options.AddDefaulted_GetRef();
	BlockingOption.LowerCell = FIntVector(5, 4, 0);
	BlockingOption.UpperCell = FIntVector(5, 4, 1);
	BlockingOption.Tier = ELayoutVerticalAccessHostTier::Constrained;
	FLayoutVerticalAccessHostOption& AlternateOption = Group.Options.AddDefaulted_GetRef();
	AlternateOption.LowerCell = FIntVector(5, 3, 0);
	AlternateOption.UpperCell = FIntVector(5, 3, 1);
	AlternateOption.Tier = ELayoutVerticalAccessHostTier::Constrained;

	const FLayoutRegionSolveResult CapturedResult = FLayoutProfileSolver::SolveRegion(Request);
	TestFalse(TEXT("Captured content reproduces shifted host failure"), CapturedResult.SolveResult.bSucceeded);
	// Optional reachability no longer rejects this host at route discovery. Both
	// attempts reach the real content mismatch; preserve its first-attempt evidence.
	FString FirstAttemptDiagnostic;
	TestTrue(TEXT("Host failure summary separates first and last attempts"),
		CapturedResult.SolveResult.FailureReason.Split(TEXT(" Last selection: "), &FirstAttemptDiagnostic, nullptr));
	TestTrue(
		FString::Printf(TEXT("First rejected host retains incompatible-face evidence: %s"), *FirstAttemptDiagnostic),
		FirstAttemptDiagnostic.Contains(
			TEXT("First selection: group=GeneratedDeckVerticalAccess_0_0 deck=X=5 Y=4 Z=1 selectedHost=X=5 Y=4 Z=0"))
		&& FirstAttemptDiagnostic.Contains(TEXT("First failure:"))
		&& FirstAttemptDiagnostic.Contains(TEXT("planned cell X=6 Y=4 Z=1"))
		&& FirstAttemptDiagnostic.Contains(TEXT("failure=ConnectionTags"))
		&& FirstAttemptDiagnostic.Contains(TEXT("sourceTag=Layout.Face.Wall.Floor.Right"))
		&& FirstAttemptDiagnostic.Contains(TEXT("neighborTag=Layout.Face.Wall.Floor.LeftRight")));
	TestTrue(
		TEXT("Last rejected host reports decisive upper half-wall mismatch"),
		CapturedResult.SolveResult.FailureReason.Contains(
			TEXT("Last selection: group=GeneratedDeckVerticalAccess_0_0 deck=X=5 Y=4 Z=1 selectedHost=X=5 Y=3 Z=0"))
		&& CapturedResult.SolveResult.FailureReason.Contains(
			TEXT("Last failure: Propagation found no legal candidates for planned cell X=6 Y=4 Z=1"))
		&& CapturedResult.SolveResult.FailureReason.Contains(
			TEXT("sourceTag=Layout.Face.Wall.Floor.Right"))
		&& CapturedResult.SolveResult.FailureReason.Contains(
			TEXT("neighborTag=Layout.Face.Wall.Floor.LeftRight")));
	return true;
}

#endif
