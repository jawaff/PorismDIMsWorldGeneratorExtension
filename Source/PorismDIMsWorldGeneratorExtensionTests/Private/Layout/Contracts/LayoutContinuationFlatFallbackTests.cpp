// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractPipeline.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContractContinuationSingletonShiftFallsBackToSparseFlatPlanTest,
	"PorismExtension.Layout.Contracts.ContinuationSingletonShiftFallsBackToSparseFlatPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies one artificial continuation upper cell cannot activate stepped topology or expand sparse route geometry. */
bool FLayoutContractContinuationSingletonShiftFallsBackToSparseFlatPlanTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.Seed = 7001;
	Request.RegionDebugPath = TEXT("SingletonShiftContinuation");
	Request.FootprintSize = FIntPoint(5, 3);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::None;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(1, 1, 1);
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	Request.RootContinuationSelection.FamilyId = TEXT("SingletonShift");
	Request.WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells = 3;
	Request.WorldBindingPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible = true;

	for (int32 X = 0; X < 3; ++X)
	{
		FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
		Cell.Cell = FIntVector(X, 0, X == 0 ? 1 : 0);
		Cell.ModuleLevelIndex = 0;
		Cell.Intent = ELayoutCellIntent::Connector;
	}

	FLayoutModePlan ModePlan;
	ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ModePlan.bUsesSteppedTerrainTopology = true;
	ModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	ModePlan.SiteCenterBlockWorldPos = FIntVector::ZeroValue;
	ModePlan.SolveSeed = Request.Seed;
	ModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
	ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan = ModePlan;

	FLayoutFrozenTerrainBiomeAdapterInput& Terrain = Request.FrozenTerrainBiomeAdapterInput;
	Request.bHasFrozenTerrainBiomeAdapterInput = true;
	Terrain.ArtifactId = TEXT("SingletonShiftTerrain");
	Terrain.EligibleBiomeRowName = TEXT("Biome.Test");
	Terrain.ModePlanId = ModePlan.ModePlanId;
	Terrain.SiteCenterBlockWorldPos = FIntVector::ZeroValue;
	Terrain.FootprintMinBlockWorldPos = FIntVector::ZeroValue;
	Terrain.FootprintSizeInBlocks = Request.FootprintSize;
	Terrain.SearchMinBlockXY = FIntPoint::ZeroValue;
	Terrain.SearchMaxBlockXY = FIntPoint(4, 2);
	Terrain.SearchDepthBlocks = 16;
	Terrain.TerrainSampleGridSpacing = 1;
	Terrain.bHasFiniteSearchBounds = true;
	Terrain.bHasSampledColumnEvidence = true;
	Terrain.bHasSteppedSupportEvidence = true;
	Terrain.bHasFootprintClassificationEvidence = true;
	Terrain.bHasTerrainPlacementEvidence = true;
	for (int32 X = 0; X < 3; ++X)
	{
		FLayoutTerrainSurfaceSample& Surface = Terrain.SurfaceSamples.AddDefaulted_GetRef();
		Surface.bIsValid = true;
		Surface.BlockXY = FIntPoint(X, 0);
		Surface.SurfaceBlockWorldPos = FIntVector(X, 0, 0);
		FLayoutSteppedTerrainSupportSample& Support = Terrain.SteppedSupportSamples.AddDefaulted_GetRef();
		Support.LocalCell = FIntVector(X, 0, 0);
		Support.SupportSurfaceZ = 0;
		Support.SnappedSupportFloorZ = 0;
		Support.SnappedSupportCeilingZ = 1;
		FLayoutTerrainPlacementCellEvidence& Placement = Terrain.TerrainPlacementCells.AddDefaulted_GetRef();
		Placement.Cell = Support.LocalCell;
		Placement.bPlaceableForSelectedMode = true;
		Placement.TerrainStageIndex = 0;
		Placement.ProvenanceId = FLayoutId(*FString::Printf(TEXT("SingletonShift.%d"), X));
	}
	FLayoutTerrainCellClassification& Classification =
		Terrain.FootprintClassification.CellClassifications.AddDefaulted_GetRef();
	Classification.BlockXY = FIntPoint::ZeroValue;

	FString FailureReason;
	TestTrue(
		FString::Printf(TEXT("Singleton shifted continuation falls back to its sparse flat plan: %s"), *FailureReason),
		FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, FailureReason));
	TestEqual(
		TEXT("Singleton shifted continuation selects flat terrain mode"),
		Request.SelectedModePlan.EnvironmentMode,
		ELayoutContractEnvironmentMode::NonSteppedWorldPlacement);
	TestEqual(TEXT("Flat fallback preserves sparse route cell count"), Request.PlannedCells.Num(), 3);
	TestFalse(TEXT("Flat fallback removes artificial upper cells"), Request.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell.Z != Cell.ModuleLevelIndex;
	}));
	TestTrue(TEXT("Fallback diagnostic records minimum shifted-cluster rejection"), Request.PrecomputedAdapterDiagnostics.ContainsByPredicate([](const FLayoutAdapterDiagnostic& Diagnostic)
	{
		return Diagnostic.Detail.Contains(TEXT("below configured minimum"));
	}));
	return true;
}
