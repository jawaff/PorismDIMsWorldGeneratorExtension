// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Contracts/LayoutPreparedContractTestUtilities.h"
#include "Layout/Runtime/LayoutCellWorldTransform.h"
#include "Layout/Runtime/LayoutRealizationWritePlan.h"
#include "Layout/Terrain/LayoutTerrainOperationBuilder.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

#include "Misc/AutomationTest.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	FLayoutRegionSolveRequest BuildFoundationParityRequest(
		const bool bUnderground,
		const int32 TemplatePlacementZOffsetBlocks)
	{
		FLayoutRegionSolveRequest Request;
		Request.Seed = 811;
		Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		Request.FootprintSize = FIntPoint(1, 1);
		Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(5, 5, 5);
		Request.TemplatePlacementZOffsetBlocks = TemplatePlacementZOffsetBlocks;
		Request.ProfileSnapshot.LevelCount = 1;
		Request.ProfileSnapshot.bUndergroundPlacement = bUnderground;
		Request.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		Request.ProfileSnapshot.bEnableTerrainSeams = true;
		Request.PlannedCells.AddDefaulted_GetRef().Cell = FIntVector::ZeroValue;
		Request.WorldBindingPlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
		Request.WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth = 2;

		FLayoutModePlan ModePlan;
		ModePlan.Scope = ELayoutContractRegionScope::Root;
		ModePlan.EnvironmentMode = bUnderground
			? ELayoutContractEnvironmentMode::UndergroundPocketPlacement
			: ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
		ModePlan.bUsesSteppedTerrainTopology = true;
		ModePlan.SiteCenterBlockWorldPos = FIntVector(2, 2, 10);
		ModePlan.SolveSeed = Request.Seed;
		ModePlan.WorldSeed = 19;
		ModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		ModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
		ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
		ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
		Request.bHasSelectedModePlan = true;
		Request.SelectedModePlan = ModePlan;

		FLayoutFrozenTerrainBiomeAdapterInput& Artifact = Request.FrozenTerrainBiomeAdapterInput;
		Request.bHasFrozenTerrainBiomeAdapterInput = true;
		Artifact.ArtifactId = TEXT("TerrainParity.Foundation");
		Artifact.ModePlanId = ModePlan.ModePlanId;
		Artifact.SiteCenterBlockWorldPos = ModePlan.SiteCenterBlockWorldPos;
		Artifact.FootprintMinBlockWorldPos = FIntVector(0, 0, 10);
		Artifact.FootprintSizeInBlocks = FIntPoint(5, 5);
		Artifact.SearchMinBlockXY = FIntPoint(0, 0);
		Artifact.SearchMaxBlockXY = FIntPoint(4, 4);
		Artifact.SearchStartZBlockWorld = 30;
		Artifact.SearchDepthBlocks = 30;
		Artifact.TerrainSampleGridSpacing = 5;
		Artifact.EligibleBiomeRowName = TEXT("Biome.Parity");
		Artifact.bHasFiniteSearchBounds = true;
		Artifact.bHasSampledColumnEvidence = true;
		Artifact.bHasSteppedSupportEvidence = true;
		Artifact.bHasFootprintClassificationEvidence = true;
		Artifact.bHasTerrainPlacementEvidence = true;
		Artifact.bHasRelativeEnvironmentClassification = true;
		Artifact.bIsClassifiedUnderground = bUnderground;
		Artifact.bHasPocketVoidIntervalEvidence = bUnderground;

		FLayoutTerrainSurfaceSample& Surface = Artifact.SurfaceSamples.AddDefaulted_GetRef();
		Surface.bIsValid = true;
		Surface.BlockXY = FIntPoint(0, 0);
		Surface.SurfaceBlockWorldPos = FIntVector(0, 0, 9);
		Artifact.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = FIntPoint(0, 0);

		FLayoutSteppedTerrainSupportSample& Support = Artifact.SteppedSupportSamples.AddDefaulted_GetRef();
		Support.LocalCell = FIntVector::ZeroValue;
		Support.SupportSurfaceZ = 9;
		Support.SnappedSupportFloorZ = 5;
		Support.SnappedSupportCeilingZ = 10;
		Request.SteppedTerrainSupportMap.SupportSamples.Add(Support);

		FLayoutTerrainPlacementCellEvidence& Evidence = Artifact.TerrainPlacementCells.AddDefaulted_GetRef();
		Evidence.Cell = FIntVector::ZeroValue;
		Evidence.bPlaceableForSelectedMode = true;
		Evidence.bHasFoundationFillEvidence = true;
		Evidence.RequiredFoundationDepth = 2;
		Evidence.FoundationMaterial = 7;
		Evidence.EntryTraversability = ELayoutEntryTraversabilityVerdict::Walkable;
		Evidence.ProvenanceId = TEXT("TerrainParity.Foundation.Cell");

		for (int32 Y = 0; Y < 5; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				FLayoutFrozenTerrainVoidIntervalSample& Interval = Artifact.PocketVoidIntervals.AddDefaulted_GetRef();
				Interval.BlockXY = FIntPoint(X, Y);
				Interval.MinZ = 0;
				Interval.MaxZ = 30;
				Interval.bHasVoidEvidence = true;
				if (X == 0)
				{
					Interval.bHasFloorMaterialIndex = true;
					Interval.FloorMaterialIndex = 9;
				}
			}
		}
		return Request;
	}

	FLayoutRegionSolveRequest BuildTransitionParityRequest(
		const bool bUnderground,
		const int32 TemplatePlacementZOffsetBlocks)
	{
		FLayoutRegionSolveRequest Request = BuildFoundationParityRequest(
			bUnderground, TemplatePlacementZOffsetBlocks);
		Request.FootprintSize = FIntPoint(3, 3);
		Request.ProfileSnapshot.MinimumFootprintInCells = Request.FootprintSize;
		Request.ProfileSnapshot.MaximumFootprintInCells = Request.FootprintSize;
		Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::Exact;
		Request.ProfileSnapshot.EntryCount = 1;
		Request.ProfileSnapshot.MinEntryCount = 1;
		Request.ProfileSnapshot.MaxEntryCount = 1;
		Request.WorldBindingPlacementPolicy.TerrainTransition.bAllowFoundationFill = false;
		Request.WorldBindingPlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
		Request.WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth = 3;
		Request.SelectedModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
		Request.SelectedModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(Request.SelectedModePlan);
		Request.PlannedCells.Reset();
		Request.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();

		FLayoutFrozenTerrainBiomeAdapterInput& Artifact = Request.FrozenTerrainBiomeAdapterInput;
		Artifact.ModePlanId = Request.SelectedModePlan.ModePlanId;
		Artifact.FootprintSizeInBlocks = FIntPoint(15, 15);
		Artifact.SearchMaxBlockXY = FIntPoint(14, 14);
		Artifact.SurfaceSamples.Reset();
		Artifact.FootprintClassification.CellClassifications.Reset();
		Artifact.SteppedSupportSamples.Reset();
		Artifact.TerrainPlacementCells.Reset();
		Artifact.PocketVoidIntervals.Reset();

		for (int32 CellY = 0; CellY < 3; ++CellY)
		{
			for (int32 CellX = 0; CellX < 3; ++CellX)
			{
				const bool bBoundary = CellX == 0 || CellY == 0 || CellX == 2 || CellY == 2;
				const bool bCorner = (CellX == 0 || CellX == 2) && (CellY == 0 || CellY == 2);
				FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
				PlannedCell.Cell = FIntVector(CellX, CellY, 0);
				PlannedCell.Intent = bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;

				FLayoutSteppedTerrainSupportSample& Support = Artifact.SteppedSupportSamples.AddDefaulted_GetRef();
				Support.LocalCell = PlannedCell.Cell;
				Support.SupportSurfaceZ = 9;
				Support.SnappedSupportFloorZ = 5;
				Support.SnappedSupportCeilingZ = 10;
				Request.SteppedTerrainSupportMap.SupportSamples.Add(Support);

				const FIntPoint BlockXY(CellX * 5, CellY * 5);
				FLayoutTerrainSurfaceSample& Surface = Artifact.SurfaceSamples.AddDefaulted_GetRef();
				Surface.bIsValid = true;
				Surface.BlockXY = BlockXY;
				Surface.SurfaceBlockWorldPos = FIntVector(BlockXY.X, BlockXY.Y, 9);
				Artifact.FootprintClassification.CellClassifications.AddDefaulted_GetRef().BlockXY = BlockXY;

				FLayoutTerrainPlacementCellEvidence& Evidence = Artifact.TerrainPlacementCells.AddDefaulted_GetRef();
				Evidence.Cell = PlannedCell.Cell;
				Evidence.bPlaceableForSelectedMode = true;
				Evidence.EntryTraversability = ELayoutEntryTraversabilityVerdict::Walkable;
				Evidence.ProvenanceId = FLayoutId(*FString::Printf(TEXT("TerrainParity.Transition.%d.%d"), CellX, CellY));
				if (bBoundary && !bCorner)
				{
					Evidence.bHasRampTransitionEvidence = true;
					Evidence.bHasExcavationEvidence = true;
					Evidence.bHasLocalOverlapZ = true;
					Evidence.OverlapMinLocalZ = FLayoutLocalBlockCoord8(0);
					Evidence.OverlapMaxLocalZ = FLayoutLocalBlockCoord8(4);
				}
			}
		}
		for (int32 Y = -5; Y < 20; ++Y)
		{
			for (int32 X = -5; X < 20; ++X)
			{
				FLayoutFrozenTerrainVoidIntervalSample& Interval = Artifact.PocketVoidIntervals.AddDefaulted_GetRef();
				Interval.BlockXY = FIntPoint(X, Y);
				Interval.MinZ = X == 5 ? 12 : 9;
				Interval.MaxZ = 30;
				Interval.bHasVoidEvidence = true;
				Interval.bHasFloorMaterialIndex = true;
				Interval.FloorMaterialIndex = 7;

				FLayoutTerrainSurfaceSample& PerimeterSample = Artifact.PerimeterSurfaceSamples.AddDefaulted_GetRef();
				PerimeterSample.BlockXY = FIntPoint(X, Y);
				PerimeterSample.bIsValid = !(X == 7 && Y == -1);
				PerimeterSample.SurfaceBlockWorldPos = FIntVector(X, Y, Interval.MinZ - 1);
				PerimeterSample.SurfaceMaterialIndex = 7;
				PerimeterSample.bHasSolidRunBounds = true;
				PerimeterSample.SolidRunMinZ = X == 5 ? 10 : 0;
				PerimeterSample.SolidRunMaxZ = Interval.MinZ - 1;
			}
		}
		return Request;
	}

	FLayoutRegionSolveRequest BuildSteppedTopologyParityRequest(const bool bUnderground)
	{
		FLayoutRegionSolveRequest Request = BuildTransitionParityRequest(bUnderground, 0);
		Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::None;
		Request.ProfileSnapshot.EntryCount = 0;
		Request.ProfileSnapshot.MinEntryCount = 0;
		Request.ProfileSnapshot.MaxEntryCount = 0;
		Request.WorldBindingPlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = false;
		Request.WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells = 3;
		Request.SelectedModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
		Request.SelectedModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(Request.SelectedModePlan);
		Request.FrozenTerrainBiomeAdapterInput.ModePlanId = Request.SelectedModePlan.ModePlanId;
		for (FLayoutTerrainPlacementCellEvidence& Evidence : Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells)
		{
			Evidence.bHasRampTransitionEvidence = false;
			Evidence.bHasExcavationEvidence = false;
			Evidence.bHasLocalOverlapZ = false;
			if (Evidence.Cell.Y == 2)
			{
				Evidence.TerrainStageIndex = 1;
				Evidence.VerticalShiftBlocks = 5;
			}
		}
		for (FLayoutSteppedTerrainSupportSample& Support : Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples)
		{
			if (Support.LocalCell.Y == 2)
			{
				Support.SupportSurfaceZ = 14;
				Support.SnappedSupportFloorZ = 10;
				Support.SnappedSupportCeilingZ = 15;
			}
		}
		Request.SteppedTerrainSupportMap.SupportSamples =
			Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples;
		return Request;
	}

	TArray<FLayoutFrozenTerrainWriteRecord> BuildFinalEntryTransitionWrites(FLayoutRegionSolveRequest Request)
	{
		FLayoutTerrainSampling::ApplySelectedComponentPerimeterRampEvidence(
			Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos,
			Request.ModuleCatalog.SharedCellSizeInBlocks,
			Request.FootprintSize,
			Request.WorldBindingPlacementPolicy.TerrainTransition,
			Request.FrozenTerrainBiomeAdapterInput);
		FLayoutFrozenTerrainContract Contract;
		Contract.FootprintMinBlockWorldPos = Request.FrozenTerrainBiomeAdapterInput.FootprintMinBlockWorldPos;
		Contract.SharedCellSizeInBlocks = Request.ModuleCatalog.SharedCellSizeInBlocks;
		for (const FLayoutTerrainPlacementCellEvidence& Evidence : Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells)
		{
			FLayoutTerrainCellContractRecord& CellContract = Contract.CellContracts.AddDefaulted_GetRef();
			CellContract.Cell = Evidence.Cell;
			CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
			CellContract.bHasRampTransitionEvidence = Evidence.bHasRampTransitionEvidence;
		}
		FLayoutPlannedCell* const FinalEntry = Request.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FIntVector(1, 0, 0);
		});
		check(FinalEntry != nullptr);
		FinalEntry->Intent = ELayoutCellIntent::Entry;
		FinalEntry->EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
		TArray<FLayoutFrozenTerrainWriteRecord> Writes;
		LayoutTerrainOperationBuilder::BuildOrdinaryRootWrites(
			Contract,
			Request.FrozenTerrainBiomeAdapterInput.PocketVoidIntervals,
			Request.FrozenTerrainBiomeAdapterInput.PerimeterSurfaceSamples,
			0,
			Request.TemplatePlacementZOffsetBlocks,
			Request.WorldBindingPlacementPolicy.TerrainTransition,
			Writes);
		return Writes;
	}

	bool BuildAdapterOutput(
		const FLayoutRegionSolveRequest& Request,
		FLayoutAdapterOutput& OutOutput,
		FString& OutFailureReason)
	{
		const FLayoutContractManifest Manifest = FLayoutContractPipeline::BuildManifestFromSolveRequest(Request);
		FLayoutContractModeAdapterInput Input;
		Input.ModePlan = Request.SelectedModePlan;
		Input.SolveRequest = &Request;
		Input.Manifest = &Manifest;
		return FLayoutContractModeAdapter::TryRunAdapter(Input, OutOutput, OutFailureReason);
	}

	bool BuildContract(
		const FLayoutRegionSolveRequest& SourceRequest,
		FLayoutRegionContract& OutContract,
		FString& OutFailureReason)
	{
		FLayoutRegionSolveRequest Request = SourceRequest;
		if (!FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, OutFailureReason))
		{
			return false;
		}
		return PorismLayoutContractTestUtilities::PrepareAndBuildRegionContract(
			Request,
			Request.SelectedModePlan.SiteCenterBlockWorldPos,
			0,
			OutContract,
			OutFailureReason);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSharedCellWorldTransformOffsetTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.CellWorldTransformUsesTemplateOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSharedCellWorldTransformOffsetTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainContract Contract;
	Contract.FootprintMinBlockWorldPos = FIntVector(100, 200, 10);
	Contract.SharedCellSizeInBlocks = FIntVector(5, 5, 5);
	FLayoutFrozenTerrainStageCellRecord& Stage = Contract.StageMap.AddDefaulted_GetRef();
	Stage.FootprintCellXY = FIntPoint(1, 2);
	Stage.ResolvedStageBaseBlockWorldZ = 25;
	const FIntVector Cell(1, 2, 1);
	TestEqual(TEXT("Negative offset lowers shared base"),
		LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract, Cell, 1, -2),
		FIntVector(105, 210, 23));
	TestEqual(TEXT("Zero offset preserves shared base"),
		LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract, Cell, 1, 0),
		FIntVector(105, 210, 25));
	TestEqual(TEXT("Positive offset raises shared base"),
		LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract, Cell, 1, 3),
		FIntVector(105, 210, 28));

	const FIntVector FlatCell(0, 0, 1);
	TestEqual(TEXT("Flat cell negative offset lowers shared base"),
		LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract, FlatCell, 0, -2),
		FIntVector(100, 200, 13));
	TestEqual(TEXT("Flat cell zero offset preserves shared base"),
		LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract, FlatCell, 0, 0),
		FIntVector(100, 200, 15));
	TestEqual(TEXT("Flat cell positive offset raises shared base"),
		LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract, FlatCell, 0, 3),
		FIntVector(100, 200, 18));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainFillWithoutMaterialHintsTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.FillGeometryWithoutMaterialHints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainFillWithoutMaterialHintsTest::RunTest(const FString& Parameters)
{
	const auto ClearMaterialHints = [](FLayoutRegionSolveRequest& Request)
	{
		auto& Evidence = Request.FrozenTerrainBiomeAdapterInput;
		for (auto& Cell : Evidence.TerrainPlacementCells) Cell.FoundationMaterial = DefaultMaterial;
		for (auto& Interval : Evidence.PocketVoidIntervals)
		{
			Interval.bHasFloorMaterialIndex = false;
			Interval.FloorMaterialIndex = DefaultMaterial;
		}
		for (auto& Surface : Evidence.PerimeterSurfaceSamples) Surface.SurfaceMaterialIndex = DefaultMaterial;
	};
	FLayoutRegionSolveRequest Foundation = BuildFoundationParityRequest(false, -2);
	FLayoutRegionContract KnownContract;
	FLayoutRegionContract UnknownContract;
	FString Failure;
	if (!TestTrue(TEXT("Known foundation fixture builds"), BuildContract(Foundation, KnownContract, Failure))) return false;
	ClearMaterialHints(Foundation);
	if (!TestTrue(TEXT("Noise-only foundation fixture builds"), BuildContract(Foundation, UnknownContract, Failure))) return false;
	TestTrue(TEXT("Reference foundation has fill geometry"), !KnownContract.FrozenTerrainContract.TerrainWrites.IsEmpty());
	TestEqual(TEXT("Missing prewarm material cannot remove foundation geometry"),
		UnknownContract.FrozenTerrainContract.TerrainWrites.Num(), KnownContract.FrozenTerrainContract.TerrainWrites.Num());

	FLayoutRegionSolveRequest Ramp = BuildTransitionParityRequest(false, 0);
	const auto KnownRampWrites = BuildFinalEntryTransitionWrites(Ramp);
	ClearMaterialHints(Ramp);
	const auto UnknownRampWrites = BuildFinalEntryTransitionWrites(Ramp);
	TestTrue(TEXT("Reference ramp has fill geometry"), KnownRampWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write) { return Write.Material != EmptyMaterial; }));
	TestEqual(TEXT("Missing prewarm material cannot remove ramp geometry"), UnknownRampWrites.Num(), KnownRampWrites.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainDeferredRootPlanCoverageTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.DeferredRootPlanCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainDeferredRootPlanCoverageTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request = BuildTransitionParityRequest(false, 0);
	Request.PlannedCells.Reset(); // Automatic prewarm defers plan generation to the adapter.
	FLayoutAdapterOutput Output;
	FString Failure;
	const bool bBuilt = BuildAdapterOutput(Request, Output, Failure);
	TestTrue(*FString::Printf(TEXT("Complete root support works before authored planning: %s"), *Failure), bBuilt);
	if (!Failure.IsEmpty()) AddInfo(Failure);
	TestEqual(TEXT("Deferred root retains every footprint column"), Output.FrozenTerrainContract.StageMap.Num(), 9);

	Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.Pop();
	TestFalse(TEXT("Deferred plan cannot admit incomplete rectangular support"), BuildAdapterOutput(Request, Output, Failure));
	TestTrue(TEXT("Missing support fails the coverage gate"), Failure.Contains(TEXT("covers 8 planned columns, expected 9")));
	Request = BuildTransitionParityRequest(false, 0);
	Request.PlannedCells.RemoveAll([](const FLayoutPlannedCell& Cell) { return Cell.Cell.X == 2 && Cell.Cell.Y == 2; });
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	Request.SelectedModePlan.PlacementKind = Request.RootPlacementKind;
	Request.SelectedModePlan.Scope = ELayoutContractRegionScope::Continuation;
	TestFalse(TEXT("Explicit sparse plan still rejects support outside its owned columns"), BuildAdapterOutput(Request, Output, Failure));
	TestTrue(TEXT("Sparse support fails the exact coverage gate"), Failure.Contains(TEXT("covers 9 planned columns, expected 8")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainFillProducerChainTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.ProducerChainUsesAcceptedCellBases",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainFillProducerChainTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request = BuildTransitionParityRequest(false, 0);
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::None;
	Request.ProfileSnapshot.EntryCount = 0;
	Request.ProfileSnapshot.MinEntryCount = 0;
	Request.ProfileSnapshot.MaxEntryCount = 0;
	Request.WorldBindingPlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	Request.WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth = 2;
	Request.WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells = 3;
	Request.SelectedModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
	Request.SelectedModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(Request.SelectedModePlan);
	auto& Artifact = Request.FrozenTerrainBiomeAdapterInput;
	Artifact.ModePlanId = Request.SelectedModePlan.ModePlanId;

	// Follow runtime's producer order. Solid density carries no material hint; neither
	// foundation/ramp flags nor required depths are supplied to the writer by the test.
	TArray<int> Materials;
	for (int32 Z = 0; Z <= 30; ++Z)
		for (int32 Y = -5; Y < 20; ++Y)
			for (int32 X = -5; X < 20; ++X)
				Materials.Add(Z <= 12 ? DefaultMaterial : EmptyMaterial);
	FString Failure;
	const bool bPrepared = FLayoutTerrainSampling::TryPrepareSelectedSiteTerrainFromMaterialSamples(
		FIntVector(-5, -5, 0), 25, 25, 0, 30, Artifact.SiteCenterBlockWorldPos,
		Request.ModuleCatalog.SharedCellSizeInBlocks, 5,
		Request.WorldBindingPlacementPolicy.TerrainTransition, EmptyMaterial,
		Materials, TConstArrayView<int>(), Artifact, Failure);
	if (!TestTrue(*FString::Printf(TEXT("Selected component prepares: %s"), *Failure), bPrepared)) return false;
	if (!TestTrue(TEXT("Selected component augments stepped support"),
		FLayoutTerrainSampling::TryAugmentSelectedComponentWithSteppedTerrainEvidence(
			false, Artifact.FootprintMinBlockWorldPos, Request.ModuleCatalog.SharedCellSizeInBlocks,
			Request.FootprintSize, Request.WorldBindingPlacementPolicy.TerrainTransition,
			Request.SteppedTerrainSupportMap, Artifact, Failure))) return false;
	FLayoutTerrainSampling::ApplySelectedComponentPerimeterRampEvidence(
		Artifact.FootprintMinBlockWorldPos, Request.ModuleCatalog.SharedCellSizeInBlocks,
		Request.FootprintSize, Request.WorldBindingPlacementPolicy.TerrainTransition, Artifact);

	FLayoutRegionContract Contract;
	const bool bBuilt = BuildContract(Request, Contract, Failure);
	if (!TestTrue(*FString::Printf(TEXT("Producer contract builds: %s"), *Failure), bBuilt)) return false;
	const auto& Writes = Contract.FrozenTerrainContract.TerrainWrites;
	for (const auto& Cell : Contract.FrozenTerrainContract.CellContracts)
		TestEqual(TEXT("Accepted base snaps above the flat procedural plane"),
			LayoutCellWorldTransform::ResolveAcceptedCellBase(Contract.FrozenTerrainContract, Cell.Cell, 0, 0).Z, 15);
	int32 FoundationCount = 0;
	for (const auto& Write : Writes)
	{
		if (Write.BlockWorldPos.X >= 0 && Write.BlockWorldPos.X < 15
			&& Write.BlockWorldPos.Y >= 0 && Write.BlockWorldPos.Y < 15)
		{
			++FoundationCount;
			TestTrue(TEXT("Foundation fills only the gap under the accepted raised base"),
				Write.BlockWorldPos.Z == 13 || Write.BlockWorldPos.Z == 14);
			TestTrue(TEXT("Producer defers material selection"), Write.bResolveMaterialFromTerrain);
			TestEqual(TEXT("Producer freezes the procedural floor as material source"), Write.MaterialSourceBlockWorldPos.Z, 12);
		}
	}
	TestEqual(TEXT("Snapped 15x15 footprint gets both missing support layers"), FoundationCount, 450);
	TestTrue(TEXT("Perimeter ramp emits fill outside the raised footprint"), Writes.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.bResolveMaterialFromTerrain && Write.BlockWorldPos.Y == 15 && Write.BlockWorldPos.Z == 13;
		}));
	TestEqual(TEXT("Complete footprint and two-ring perimeter ramp have 650 writes"), Writes.Num(), 650);
	for (const auto& Cell : Contract.FrozenTerrainContract.CellContracts)
	{
		TestTrue(TEXT("Frozen cell owns its emitted foundation"), Cell.bHasFoundationFillEvidence);
		TestEqual(TEXT("Frozen depth matches both support layers"), Cell.RequiredFoundationDepth, 2);
	}

	// Exercise material realization with the exact producer output, not a manually
	// assembled write contract. Available terrain supplies different materials per column.
	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Producer realization has a chunk world"), Harness.World)) return false;
	for (const auto& Write : Writes)
		Harness.World->SetBlockValueByBlockWorldPos(Write.MaterialSourceBlockWorldPos,
			40 + FMath::Abs(Write.BlockWorldPos.X) % 5, false);
	const bool bApplied = FLayoutContractPipeline::ApplyFrozenTerrainContract(Harness.World, Contract.FrozenTerrainContract, Failure);
	if (!TestTrue(*FString::Printf(TEXT("Producer output realizes: %s"), *Failure), bApplied)) return false;
	for (const auto& Write : Writes)
		TestEqual(TEXT("Realized foundation and ramp use the column support material"),
			Harness.World->GetBlockValueByBlockWorldPos(Write.BlockWorldPos, ERessourceType::MaterialIndex, 0),
			40 + FMath::Abs(Write.BlockWorldPos.X) % 5);

	const auto RebuildWrites = [&](FLayoutFrozenTerrainContract& Frozen, const FLayoutWorldBindingTerrainTransitionPolicy& Policy)
	{
		LayoutTerrainOperationBuilder::BuildOrdinaryRootWrites(Frozen, Artifact.PocketVoidIntervals,
			Artifact.PerimeterSurfaceSamples, 0, 0, Policy, Frozen.TerrainWrites);
	};
	FLayoutFrozenTerrainContract Rebuilt = Contract.FrozenTerrainContract;
	RebuildWrites(Rebuilt, Request.WorldBindingPlacementPolicy.TerrainTransition);
	TestEqual(TEXT("Rebuilding settled support is idempotent"), FLayoutContractPipeline::BuildTerrainWriteArtifactId(Rebuilt),
		FLayoutContractPipeline::BuildTerrainWriteArtifactId(Contract.FrozenTerrainContract));
	FLayoutTerrainCellContractRecord& UpperFloor = Rebuilt.CellContracts.AddDefaulted_GetRef();
	UpperFloor.Cell = FIntVector(1, 1, 1);
	UpperFloor.bHasFoundationFillEvidence = true;
	UpperFloor.RequiredFoundationDepth = 2;
	UpperFloor.bHasRampTransitionEvidence = true;
	RebuildWrites(Rebuilt, Request.WorldBindingPlacementPolicy.TerrainTransition);
	TestEqual(TEXT("Upper floor cannot fill the building interior or grow another ramp"), Rebuilt.TerrainWrites.Num(), Writes.Num());
	TestFalse(TEXT("Upper floor has no foundation obligation"), Rebuilt.CellContracts.Last().bHasFoundationFillEvidence);
	TestFalse(TEXT("Upper floor has no ramp obligation"), Rebuilt.CellContracts.Last().bHasRampTransitionEvidence);

	auto Policy = Request.WorldBindingPlacementPolicy.TerrainTransition;
	Policy.bAllowFoundationFill = false;
	RebuildWrites(Rebuilt, Policy);
	TestEqual(TEXT("Foundation toggle leaves only the perimeter ramp"), Rebuilt.TerrainWrites.Num(), 200);
	Policy.bAllowPerimeterRampTransition = false;
	RebuildWrites(Rebuilt, Policy);
	TestTrue(TEXT("Disabled support policies cannot emit terrain writes"), Rebuilt.TerrainWrites.IsEmpty());
	Policy.bAllowFoundationFill = true;
	RebuildWrites(Rebuilt, Policy);
	TestEqual(TEXT("Ramp toggle leaves only the foundation"), Rebuilt.TerrainWrites.Num(), 450);
	Policy.MaxFoundationDepth = 1;
	RebuildWrites(Rebuilt, Policy);
	TestEqual(TEXT("Existing depth budget limits actual column thickness"), Rebuilt.TerrainWrites.Num(), 225);
	Policy.MaxFoundationDepth = 0;
	RebuildWrites(Rebuilt, Policy);
	TestTrue(TEXT("Zero depth budget cannot emit fill"), Rebuilt.TerrainWrites.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDeferredFillMaterialRealizationTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.SamplesFillMaterialAtRealization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDeferredFillMaterialRealizationTest::RunTest(const FString& Parameters)
{
	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Realization fixture has a chunk world"), Harness.World)) return false;
	FLayoutRegionSolveRequest Request = BuildFoundationParityRequest(false, -2);
	for (auto& Interval : Request.FrozenTerrainBiomeAdapterInput.PocketVoidIntervals)
	{
		// Template base is Z=8; known support Z=5 is within the two-block fill budget.
		Interval.MinZ = 6;
		Interval.bHasFloorMaterialIndex = false;
		Interval.FloorMaterialIndex = DefaultMaterial;
	}
	Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells[0].FoundationMaterial = DefaultMaterial;
	FLayoutRegionContract Contract;
	FString Failure;
	if (!TestTrue(TEXT("Prewarm freezes unknown-material fill"), BuildContract(Request, Contract, Failure))) return false;
	auto& Frozen = Contract.FrozenTerrainContract;
	if (!TestEqual(TEXT("Prewarm retains complete two-block foundation"), Frozen.TerrainWrites.Num(), 50)) return false;
	const auto OriginalWrites = Frozen.TerrainWrites;
	for (const auto& Write : Frozen.TerrainWrites)
	{
		TestTrue(TEXT("Foundation defers material sampling"), Write.bResolveMaterialFromTerrain);
		TestEqual(TEXT("Foundation freezes support below its own column"), Write.MaterialSourceBlockWorldPos,
			FIntVector(Write.BlockWorldPos.X, Write.BlockWorldPos.Y, 5));
		// Terrain becomes available after planning. Each column has its own material.
		Harness.World->SetBlockValueByBlockWorldPos(Write.MaterialSourceBlockWorldPos, 40 + Write.BlockWorldPos.X, false);
	}
	if (!TestTrue(FString::Printf(TEXT("Realization applies deferred fill: %s"), *Failure),
		FLayoutContractPipeline::ApplyFrozenTerrainContract(Harness.World, Frozen, Failure))) return false;
	for (int32 Index = 0; Index < Frozen.TerrainWrites.Num(); ++Index)
	{
		const auto& Write = Frozen.TerrainWrites[Index];
		TestEqual(TEXT("Each fill column inherits its current support material"),
			Harness.World->GetBlockValueByBlockWorldPos(Write.BlockWorldPos, ERessourceType::MaterialIndex, 0), 40 + Write.BlockWorldPos.X);
		TestEqual(TEXT("Realization leaves frozen material hints unchanged"), Write.Material, OriginalWrites[Index].Material);
	}

	// Procedural support can sit above the generated surface. Resolve material only,
	// in the same column and within the frozen authored depth; do not move writes.
	for (const auto& Write : Frozen.TerrainWrites)
	{
		TestEqual(TEXT("Producer freezes authored material search depth"), Write.MaterialSourceSearchDepthBlocks, 2);
		Harness.World->SetBlockValueByBlockWorldPos(Write.MaterialSourceBlockWorldPos, EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(Write.MaterialSourceBlockWorldPos - FIntVector(0, 0, 1), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(Write.MaterialSourceBlockWorldPos - FIntVector(0, 0, 2), 80 + Write.BlockWorldPos.X, false);
	}
	auto ShallowContract = Frozen;
	for (auto& Write : ShallowContract.TerrainWrites) Write.MaterialSourceSearchDepthBlocks = 1;
	TestNotEqual(TEXT("Material search bounds participate in frozen identity"),
		FLayoutContractPipeline::BuildTerrainWriteArtifactId(ShallowContract), FLayoutContractPipeline::BuildTerrainWriteArtifactId(Frozen));
	TestFalse(TEXT("Material below the frozen search bound cannot authorize writes"),
		FLayoutContractPipeline::ApplyFrozenTerrainContract(Harness.World, ShallowContract, Failure));
	TestTrue(TEXT("Material lookup reaches actual support inside the frozen depth"),
		FLayoutContractPipeline::ApplyFrozenTerrainContract(Harness.World, Frozen, Failure));
	for (const auto& Write : Frozen.TerrainWrites)
		TestEqual(TEXT("Unmoved fill uses first solid material below predicted support"),
			Harness.World->GetBlockValueByBlockWorldPos(Write.BlockWorldPos, ERessourceType::MaterialIndex, 0), 80 + Write.BlockWorldPos.X);

	FLayoutRegionSolveRequest RampRequest = BuildTransitionParityRequest(false, 0);
	FLayoutRegionContract RampContract;
	if (!TestTrue(TEXT("Ramp realization fixture builds"), BuildContract(RampRequest, RampContract, Failure))) return false;
	RampContract.FrozenTerrainContract.TerrainWrites = BuildFinalEntryTransitionWrites(RampRequest);
	for (const auto& Write : RampContract.FrozenTerrainContract.TerrainWrites)
	{
		if (Write.bResolveMaterialFromTerrain)
		{
			Harness.World->SetBlockValueByBlockWorldPos(Write.MaterialSourceBlockWorldPos,
				60 + FMath::Abs(Write.BlockWorldPos.X) % 5, false);
		}
	}
	if (!TestTrue(TEXT("Ramp samples support at realization"),
		FLayoutContractPipeline::ApplyFrozenTerrainContract(Harness.World, RampContract.FrozenTerrainContract, Failure))) return false;
	for (const auto& Write : RampContract.FrozenTerrainContract.TerrainWrites)
	{
		TestEqual(TEXT("Ramp fills inherit local support while cuts remain explicit air"),
			Harness.World->GetBlockValueByBlockWorldPos(Write.BlockWorldPos, ERessourceType::MaterialIndex, 0),
			Write.bResolveMaterialFromTerrain ? 60 + FMath::Abs(Write.BlockWorldPos.X) % 5 : Write.Material);
	}

	// A missing support read must not apply even an earlier explicit clear write.
	FLayoutFrozenTerrainWriteRecord Clear;
	Clear.BlockWorldPos = FIntVector(0, 0, 7);
	Clear.Material = EmptyMaterial;
	Clear.SourceContract = Frozen.TerrainWrites[0].SourceContract;
	Frozen.TerrainWrites.Insert(Clear, 0);
	Harness.World->SetBlockValueByBlockWorldPos(Clear.BlockWorldPos, 40, false);
	// Unavailable means no solid material anywhere in this contract's search interval.
	for (int32 Z = 3; Z <= 5; ++Z)
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(4, 4, Z), EmptyMaterial, false);
	TestFalse(TEXT("Unavailable fill material rejects replay"), FLayoutContractPipeline::ApplyFrozenTerrainContract(Harness.World, Frozen, Failure));
	TestEqual(TEXT("Material resolution finishes before any clear or fill"),
		Harness.World->GetBlockValueByBlockWorldPos(Clear.BlockWorldPos, ERessourceType::MaterialIndex, 0), 40);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSurfaceUndergroundFoundationWriteParityTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.FoundationWritesUseSharedOffsetAwareBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSurfaceUndergroundFoundationWriteParityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionContract SurfaceContract;
	FLayoutRegionContract UndergroundContract;
	FString SurfaceFailure;
	FString UndergroundFailure;
	const bool bSurfaceAccepted = BuildContract(BuildFoundationParityRequest(false, -2), SurfaceContract, SurfaceFailure);
	const bool bUndergroundAccepted = BuildContract(BuildFoundationParityRequest(true, -2), UndergroundContract, UndergroundFailure);
	TestTrue(FString::Printf(TEXT("Surface parity fixture builds: %s"), *SurfaceFailure), bSurfaceAccepted);
	TestTrue(FString::Printf(TEXT("Underground parity fixture builds: %s"), *UndergroundFailure), bUndergroundAccepted);
	if (!bSurfaceAccepted || !bUndergroundAccepted)
	{
		return false;
	}

	const TArray<FLayoutFrozenTerrainWriteRecord>& SurfaceWrites = SurfaceContract.FrozenTerrainContract.TerrainWrites;
	const TArray<FLayoutFrozenTerrainWriteRecord>& UndergroundWrites = UndergroundContract.FrozenTerrainContract.TerrainWrites;
	TestTrue(TEXT("Surface foundation policy emits writes"), !SurfaceWrites.IsEmpty());
	TestEqual(TEXT("Surface and Underground emit same foundation write count"), SurfaceWrites.Num(), UndergroundWrites.Num());
	TestTrue(TEXT("Offset -2 foundation begins below resolved template base"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.Z == 6 && Write.Material == 7;
		}));
	TestTrue(TEXT("Foundation uses per-column frozen support material"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.X == 0 && Write.Material == 9;
		}));
	TestTrue(TEXT("Foundation falls back to cell support material when a column has no frozen material"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.X == 1 && Write.Material == 7;
		}));
	TestFalse(TEXT("Foundation never overlaps resolved template base Z=8"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.Z >= 8;
		}));
	for (int32 Index = 0; Index < FMath::Min(SurfaceWrites.Num(), UndergroundWrites.Num()); ++Index)
	{
		TestEqual(TEXT("Surface and Underground foundation positions match"), SurfaceWrites[Index].BlockWorldPos, UndergroundWrites[Index].BlockWorldPos);
		TestEqual(TEXT("Surface and Underground foundation materials match"), SurfaceWrites[Index].Material, UndergroundWrites[Index].Material);
	}

	FLayoutRegionSolveRequest FlatSurfaceRequest = BuildFoundationParityRequest(false, -2);
	FlatSurfaceRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = false;
	FlatSurfaceRequest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::NonSteppedWorldPlacement;
	FlatSurfaceRequest.SelectedModePlan.bUsesSteppedTerrainTopology = false;
	FlatSurfaceRequest.SelectedModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(FlatSurfaceRequest.SelectedModePlan);
	FlatSurfaceRequest.FrozenTerrainBiomeAdapterInput.ModePlanId = FlatSurfaceRequest.SelectedModePlan.ModePlanId;
	FLayoutRegionContract FlatSurfaceContract;
	FString FlatSurfaceFailure;
	const bool bFlatSurfaceAccepted = BuildContract(FlatSurfaceRequest, FlatSurfaceContract, FlatSurfaceFailure);
	TestTrue(FString::Printf(TEXT("Non-stepped Surface fixture builds: %s"), *FlatSurfaceFailure), bFlatSurfaceAccepted);
	if (bFlatSurfaceAccepted)
	{
		TestEqual(TEXT("Non-stepped Surface shares foundation writes"),
			FlatSurfaceContract.FrozenTerrainContract.TerrainWrites.Num(), SurfaceWrites.Num());
	}

	FLayoutRegionSolveRequest CacheMissCornerRequest = BuildFoundationParityRequest(false, -2);
	CacheMissCornerRequest.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells[0].FoundationMaterial = DefaultMaterial;
	FLayoutRegionContract CacheMissCornerContract;
	FString CacheMissCornerFailure;
	const bool bCacheMissCornerAccepted = BuildContract(
		CacheMissCornerRequest,
		CacheMissCornerContract,
		CacheMissCornerFailure);
	TestTrue(FString::Printf(TEXT("Cache-miss corner fixture builds: %s"), *CacheMissCornerFailure), bCacheMissCornerAccepted);
	if (bCacheMissCornerAccepted)
	{
		TestEqual(TEXT("Cache-miss corner retains complete bounded foundation writes"),
			CacheMissCornerContract.FrozenTerrainContract.TerrainWrites.Num(), SurfaceWrites.Num());
		TestFalse(TEXT("Cache-miss corner never publishes DefaultMaterial as foundation"),
			CacheMissCornerContract.FrozenTerrainContract.TerrainWrites.ContainsByPredicate(
				[](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.Material == DefaultMaterial;
				}));
		TestTrue(TEXT("Cache-miss corner uses nearest concrete floor material"),
			CacheMissCornerContract.FrozenTerrainContract.TerrainWrites.ContainsByPredicate(
				[](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.BlockWorldPos.X == 4 && Write.Material == 9;
				}));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSurfaceUndergroundPerimeterTransitionParityTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.PerimeterTransitionsUseSharedOffsetAwareBase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSurfaceUndergroundPerimeterTransitionParityTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutFrozenTerrainWriteRecord> SurfaceWrites =
		BuildFinalEntryTransitionWrites(BuildTransitionParityRequest(false, -2));
	const TArray<FLayoutFrozenTerrainWriteRecord> UndergroundWrites =
		BuildFinalEntryTransitionWrites(BuildTransitionParityRequest(true, -2));
	const TArray<FLayoutFrozenTerrainWriteRecord> PositiveOffsetWrites =
		BuildFinalEntryTransitionWrites(BuildTransitionParityRequest(false, 3));
	TestTrue(TEXT("Perimeter cells emit bounded fill and excavation ramp writes"), !SurfaceWrites.IsEmpty());
	TestEqual(TEXT("Surface and Underground transition write counts match"), SurfaceWrites.Num(), UndergroundWrites.Num());
	TestEqual(TEXT("Template offset does not change transition write count"), SurfaceWrites.Num(), PositiveOffsetWrites.Num());
	int32 MinWriteZ = MAX_int32;
	int32 MaxWriteZ = MIN_int32;
	for (int32 Index = 0; Index < FMath::Min(SurfaceWrites.Num(), UndergroundWrites.Num()); ++Index)
	{
		MinWriteZ = FMath::Min(MinWriteZ, SurfaceWrites[Index].BlockWorldPos.Z);
		MaxWriteZ = FMath::Max(MaxWriteZ, SurfaceWrites[Index].BlockWorldPos.Z);
		TestEqual(TEXT("Surface and Underground transition positions match"), SurfaceWrites[Index].BlockWorldPos, UndergroundWrites[Index].BlockWorldPos);
		TestEqual(TEXT("Template offset does not move transition positions"), SurfaceWrites[Index].BlockWorldPos, PositiveOffsetWrites[Index].BlockWorldPos);
	}
	TestTrue(TEXT("Ramp raises immediate downhill surface to one block below unadjusted base"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos == FIntVector(6, -1, 9) && Write.Material == 7;
		}));
	TestFalse(TEXT("Ramp never fills into unadjusted layout base"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos == FIntVector(6, -1, 10) && Write.Material != EmptyMaterial;
		}));
	TestFalse(TEXT("Ramp stops after joining flat terrain instead of cutting an outer moat"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.X == 6 && Write.BlockWorldPos.Y <= -2;
		}));
	TestTrue(TEXT("Excavated ramp starts at same immediate join surface as fill"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos == FIntVector(5, -1, 10) && Write.Material == EmptyMaterial;
		}));
	TestTrue(TEXT("Excavated ramp rises outward toward overlapping terrain"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos == FIntVector(5, -2, 11) && Write.Material == EmptyMaterial;
		}));
	TestFalse(TEXT("Excavated ramp stops after joining high terrain"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.X == 5 && Write.BlockWorldPos.Y <= -3;
		}));
	TestFalse(TEXT("Invalid no-terrain perimeter sample emits no speculative removal"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos.X == 7 && Write.BlockWorldPos.Y == -1;
		}));
	TestTrue(TEXT("Diagonal corner receives terrain-driven ramp coverage"), SurfaceWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos == FIntVector(-1, -1, 9) && Write.Material == 7;
		}));

	FLayoutRegionSolveRequest DiagonalOnlyRequest = BuildTransitionParityRequest(false, 0);
	for (FLayoutTerrainSurfaceSample& Sample : DiagonalOnlyRequest.FrozenTerrainBiomeAdapterInput.PerimeterSurfaceSamples)
	{
		Sample.SurfaceBlockWorldPos.Z = 9;
		Sample.SolidRunMaxZ = 9;
		if (Sample.BlockXY == FIntPoint(-1, -1))
		{
			Sample.SurfaceBlockWorldPos.Z = 8;
			Sample.SolidRunMaxZ = 8;
		}
	}
	const TArray<FLayoutFrozenTerrainWriteRecord> DiagonalOnlyWrites =
		BuildFinalEntryTransitionWrites(MoveTemp(DiagonalOnlyRequest));
	TestTrue(TEXT("Diagonal mismatch alone authorizes corner ramp transition"), DiagonalOnlyWrites.ContainsByPredicate(
		[](const FLayoutFrozenTerrainWriteRecord& Write)
		{
			return Write.BlockWorldPos == FIntVector(-1, -1, 9) && Write.Material == 7;
		}));
	TestEqual(TEXT("Transition clears only sampled solid overlap"), MaxWriteZ, 11);
	TestEqual(TEXT("Transition starts at corrected edge surface"), MinWriteZ, 9);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSurfaceUndergroundFallbackParityTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.MissingSteppedEvidenceUsesSharedFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSurfaceUndergroundFallbackParityTest::RunTest(const FString& Parameters)
{
	auto BuildMissingSupportRequest = [](const bool bUnderground)
	{
		FLayoutRegionSolveRequest Request = BuildFoundationParityRequest(bUnderground, 0);
		Request.WorldBindingPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible = true;
		Request.SelectedModePlan.PlacementPolicy = Request.WorldBindingPlacementPolicy;
		Request.SelectedModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(Request.SelectedModePlan);
		Request.FrozenTerrainBiomeAdapterInput.ModePlanId = Request.SelectedModePlan.ModePlanId;
		Request.FrozenTerrainBiomeAdapterInput.bHasSteppedSupportEvidence = false;
		Request.FrozenTerrainBiomeAdapterInput.SteppedSupportSamples.Reset();
		Request.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
		return Request;
	};
	FLayoutRegionSolveRequest SurfaceRequest = BuildMissingSupportRequest(false);
	FLayoutRegionSolveRequest UndergroundRequest = BuildMissingSupportRequest(true);
	FString SurfaceFailure;
	FString UndergroundFailure;
	const bool bSurfaceAccepted = FLayoutContractPipeline::TryPrecomputeAdapterOutput(SurfaceRequest, SurfaceFailure);
	const bool bUndergroundAccepted = FLayoutContractPipeline::TryPrecomputeAdapterOutput(UndergroundRequest, UndergroundFailure);
	TestTrue(FString::Printf(TEXT("Surface classified fallback succeeds: %s"), *SurfaceFailure), bSurfaceAccepted);
	TestTrue(FString::Printf(TEXT("Underground classified fallback succeeds: %s"), *UndergroundFailure), bUndergroundAccepted);
	if (!bSurfaceAccepted || !bUndergroundAccepted)
	{
		return false;
	}
	TestFalse(TEXT("Surface fallback clears stepped topology decision"), SurfaceRequest.SelectedModePlan.bUsesSteppedTerrainTopology);
	TestFalse(TEXT("Underground fallback clears stepped topology decision"), UndergroundRequest.SelectedModePlan.bUsesSteppedTerrainTopology);
	TestTrue(TEXT("Surface fallback clears StageMap"), SurfaceRequest.PrecomputedFrozenTerrainContract.StageMap.IsEmpty());
	TestTrue(TEXT("Underground fallback clears StageMap"), UndergroundRequest.PrecomputedFrozenTerrainContract.StageMap.IsEmpty());
	TestEqual(TEXT("Surface and Underground fallback planned-cell counts match"),
		SurfaceRequest.PrecomputedPlannedCells.Num(), UndergroundRequest.PrecomputedPlannedCells.Num());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSharedFoundationClearanceRealizationTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.RealizesMixedFoundationClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSharedFoundationClearanceRealizationTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.SharedCellSizeInBlocks = FIntVector(5, 5, 5);
	SolveResult.FootprintSize = FIntPoint(1, 1);
	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector::ZeroValue;
	Placement.SourceContentEntryId = TEXT("TerrainParity.Content");
	Placement.ModuleSnapshotId = TEXT("TerrainParity.Module");
	Placement.TemplatePath = FSoftObjectPath(TEXT("/Game/Test/Templates/T_TerrainParity.T_TerrainParity"));

	FLayoutFrozenTerrainContract Contract;
	Contract.ContractId = TEXT("TerrainParity.FoundationClearance");
	Contract.SharedCellSizeInBlocks = SolveResult.SharedCellSizeInBlocks;
	Contract.FootprintSizeInCells = SolveResult.FootprintSize;
	FLayoutContractActiveCellRecord& ActiveCell = Contract.ActiveCells.AddDefaulted_GetRef();
	ActiveCell.Cell = FIntVector::ZeroValue;
	FLayoutTerrainCellContractRecord& CellContract = Contract.CellContracts.AddDefaulted_GetRef();
	CellContract.Cell = FIntVector::ZeroValue;
	CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
	CellContract.bHasFoundationFillEvidence = true;
	CellContract.RequiredFoundationDepth = 1;
	CellContract.FoundationMaterial = 7;
	CellContract.bHasClearanceEvidence = true;
	FLayoutFrozenTerrainWriteRecord& Write = Contract.TerrainWrites.AddDefaulted_GetRef();
	Write.BlockWorldPos = FIntVector(0, 0, -1);
	Write.Material = 7;
	Write.SourceContract = ELayoutFrozenTerrainCellContract::Active;

	FLayoutRealizationWritePlan WritePlan;
	FString FailureReason;
	const bool bBuilt = LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource::Site,
		TEXT("TerrainParity.FoundationClearance"),
		1,
		SolveResult,
		Contract,
		WritePlan,
		FailureReason);
	TestTrue(FString::Printf(TEXT("Mixed foundation/clearance contract realizes: %s"), *FailureReason), bBuilt);
	Write.bResolveMaterialFromTerrain = true;
	Write.MaterialSourceBlockWorldPos = FIntVector(0, 0, -2);
	Write.MaterialSourceSearchDepthBlocks = 2;
	if (TestTrue(TEXT("Typed replay preserves deferred source authority"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource::Site, TEXT("TerrainParity.FoundationClearance"), 1,
		SolveResult, Contract, WritePlan, FailureReason)))
	{
		TestTrue(TEXT("Matching deferred replay validates"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));
		TestEqual(TEXT("Typed replay retains the material search bound"), WritePlan.TerrainWrites.Support.FoundationRampWrites[0].MaterialSourceSearchDepthBlocks, 2);
		WritePlan.TerrainWrites.Support.FoundationRampWrites[0].MaterialSourceSearchDepthBlocks = 3;
		TestFalse(TEXT("Expanded material search cannot bypass frozen replay authority"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));
		WritePlan.TerrainWrites.Support.FoundationRampWrites[0].MaterialSourceSearchDepthBlocks = 2;
		WritePlan.TerrainWrites.Support.FoundationRampWrites[0].MaterialSourceBlockWorldPos.Z = -3;
		TestFalse(TEXT("Changed support source cannot bypass frozen replay authority"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSurfaceUndergroundSteppedTopologyParityTest,
	"PorismExtension.Layout.Terrain.SurfaceUndergroundParity.SteppedTopologyAndTerrainSeamsMatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSurfaceUndergroundSteppedTopologyParityTest::RunTest(const FString& Parameters)
{
	FLayoutAdapterOutput SurfaceOutput;
	FLayoutAdapterOutput UndergroundOutput;
	FString SurfaceFailure;
	FString UndergroundFailure;
	const bool bSurfaceAccepted = BuildAdapterOutput(BuildSteppedTopologyParityRequest(false), SurfaceOutput, SurfaceFailure);
	const bool bUndergroundAccepted = BuildAdapterOutput(BuildSteppedTopologyParityRequest(true), UndergroundOutput, UndergroundFailure);
	TestTrue(FString::Printf(TEXT("Surface stepped fixture builds: %s"), *SurfaceFailure), bSurfaceAccepted);
	TestTrue(FString::Printf(TEXT("Underground stepped fixture builds: %s"), *UndergroundFailure), bUndergroundAccepted);
	if (!bSurfaceAccepted || !bUndergroundAccepted)
	{
		return false;
	}
	TestTrue(TEXT("Shared stepped fixture publishes StageMap"), !SurfaceOutput.FrozenTerrainContract.StageMap.IsEmpty());
	TestEqual(TEXT("Surface and Underground StageMap counts match"),
		SurfaceOutput.FrozenTerrainContract.StageMap.Num(),
		UndergroundOutput.FrozenTerrainContract.StageMap.Num());
	TestEqual(TEXT("Surface and Underground planned topology counts match"),
		SurfaceOutput.PlannedCells.Num(),
		UndergroundOutput.PlannedCells.Num());
	bool bFoundTerrainSeam = false;
	for (int32 Index = 0; Index < FMath::Min(SurfaceOutput.PlannedCells.Num(), UndergroundOutput.PlannedCells.Num()); ++Index)
	{
		const FLayoutPlannedCell& SurfaceCell = SurfaceOutput.PlannedCells[Index];
		const FLayoutPlannedCell& UndergroundCell = UndergroundOutput.PlannedCells[Index];
		TestEqual(TEXT("Surface and Underground planned cells match"), SurfaceCell.Cell, UndergroundCell.Cell);
		TestEqual(TEXT("Surface and Underground bridge flags match"), SurfaceCell.bIsBridgeCell, UndergroundCell.bIsBridgeCell);
		TestEqual(TEXT("Surface and Underground terrain-seam masks match"), SurfaceCell.TerrainSeamFaceMask, UndergroundCell.TerrainSeamFaceMask);
		bFoundTerrainSeam |= SurfaceCell.TerrainSeamFaceMask != 0;
	}
	TestTrue(TEXT("Equivalent stepped topology produces nonzero terrain-seam masks"), bFoundTerrainSeam);
	return true;
}

#endif
