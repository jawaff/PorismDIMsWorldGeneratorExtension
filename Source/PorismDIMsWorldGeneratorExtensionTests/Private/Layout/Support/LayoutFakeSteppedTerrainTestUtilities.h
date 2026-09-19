// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Contracts/LayoutContractPipeline.h"

namespace PorismLayoutFakeSteppedTerrainTestUtilities
{
	/** Axis whose positive half receives the raised fake terrain surface. */
	enum class ECenteredHalfStepAxis : uint8
	{
		X,
		Y
	};

	/** Configures deterministic two-height terrain evidence for solver and fixture tests. */
	struct FCenteredHalfStepConfig
	{
		/** Shared layout cell dimensions in blocks. */
		FIntVector SharedCellSizeInBlocks = FIntVector(5, 5, 5);

		/** Minimum block-world corner of the fake footprint. */
		FIntVector FootprintMinBlockWorldPos = FIntVector::ZeroValue;

		/** Surface Z used by the lower half. */
		int32 LowerSurfaceZ = 0;

		/** Height added to the raised half. */
		int32 StepHeightBlocks = 5;

		/** Split direction; cells at or beyond SplitCellIndex use the raised surface. */
		ECenteredHalfStepAxis SplitAxis = ECenteredHalfStepAxis::X;

		/** First raised cell on SplitAxis; INDEX_NONE selects the footprint midpoint. */
		int32 SplitCellIndex = INDEX_NONE;

		/** Stable biome row carried through frozen test evidence. */
		FName EligibleBiomeRowName = TEXT("FakeSteppedTerrain");

		/** Placement kind stamped onto the test request. */
		ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;

		/** Continuations use cardinal-only raised-cluster connectivity, matching runtime behavior. */
		bool bUseCardinalShiftClusters = true;
	};

	/** Builds frozen surface samples for an arbitrary route-grid region under the configured half-plane terrain. */
	inline bool BuildCenteredHalfStepFrozenSurfaceEvidence(
		const FCenteredHalfStepConfig& Config,
		const FIntPoint& RouteGridOriginCell,
		const TArray<FIntPoint>& SampleCells,
		FLayoutFrozenTerrainBiomeAdapterInput& OutEvidence)
	{
		OutEvidence = FLayoutFrozenTerrainBiomeAdapterInput();
		if (SampleCells.IsEmpty()
			|| Config.SharedCellSizeInBlocks.X <= 0
			|| Config.SharedCellSizeInBlocks.Y <= 0
			|| Config.SharedCellSizeInBlocks.Z <= 0)
		{
			return false;
		}

		int32 MinAxis = MAX_int32;
		int32 MaxAxis = MIN_int32;
		for (const FIntPoint& Cell : SampleCells)
		{
			const int32 AxisValue = Config.SplitAxis == ECenteredHalfStepAxis::X ? Cell.X : Cell.Y;
			MinAxis = FMath::Min(MinAxis, AxisValue);
			MaxAxis = FMath::Max(MaxAxis, AxisValue);
		}
		const int32 SplitCell = Config.SplitCellIndex == INDEX_NONE
			? MinAxis + (MaxAxis - MinAxis + 1) / 2
			: Config.SplitCellIndex;
		OutEvidence.bHasSampledColumnEvidence = true;
		OutEvidence.bHasTerrainPathEvidence = true;
		OutEvidence.EligibleBiomeRowName = Config.EligibleBiomeRowName;
		OutEvidence.EligibleBiomeRowNames = {Config.EligibleBiomeRowName};
		OutEvidence.FootprintMinBlockWorldPos = FIntVector(
			Config.FootprintMinBlockWorldPos.X + RouteGridOriginCell.X * Config.SharedCellSizeInBlocks.X,
			Config.FootprintMinBlockWorldPos.Y + RouteGridOriginCell.Y * Config.SharedCellSizeInBlocks.Y,
			Config.LowerSurfaceZ);
		for (const FIntPoint& Cell : SampleCells)
		{
			const int32 AxisValue = Config.SplitAxis == ECenteredHalfStepAxis::X ? Cell.X : Cell.Y;
			const int32 SurfaceZ = Config.LowerSurfaceZ
				+ (AxisValue >= SplitCell ? Config.StepHeightBlocks : 0);
			FLayoutTerrainSurfaceSample& Sample = OutEvidence.SurfaceSamples.AddDefaulted_GetRef();
			Sample.bIsValid = true;
			Sample.BlockXY = FIntPoint(
				OutEvidence.FootprintMinBlockWorldPos.X + Cell.X * Config.SharedCellSizeInBlocks.X,
				OutEvidence.FootprintMinBlockWorldPos.Y + Cell.Y * Config.SharedCellSizeInBlocks.Y);
			Sample.SurfaceBlockWorldPos = FIntVector(Sample.BlockXY.X, Sample.BlockXY.Y, SurfaceZ);
			FLayoutFrozenTerrainPathSample& PathSample = OutEvidence.TerrainPathSamples.AddDefaulted_GetRef();
			PathSample.BlockXY = Sample.BlockXY;
			PathSample.SurfaceZ = SurfaceZ;
			PathSample.bHasClassificationEvidence = true;
		}
		OutEvidence.SurfaceSamples.Sort([](const FLayoutTerrainSurfaceSample& Left, const FLayoutTerrainSurfaceSample& Right)
		{
			return Left.BlockXY.Y != Right.BlockXY.Y
				? Left.BlockXY.Y < Right.BlockXY.Y
				: Left.BlockXY.X < Right.BlockXY.X;
		});
		return true;
	}

	/** Applies centered half-step terrain evidence and precomputes the real adapter output on one frozen request. */
	inline bool ApplyCenteredHalfStepAndPrecompute(
		FLayoutRegionSolveRequest& InOutRequest,
		const FCenteredHalfStepConfig& Config,
		FString& OutFailureReason,
		FLayoutSteppedTerrainSupportMap* OutSourceSupportMap = nullptr)
	{
		OutFailureReason.Reset();
		const TArray<FLayoutPlannedCell>& SourceCells = !InOutRequest.PrecomputedPlannedCells.IsEmpty()
			? InOutRequest.PrecomputedPlannedCells
			: InOutRequest.PlannedCells;
		if (SourceCells.IsEmpty())
		{
			OutFailureReason = TEXT("Fake stepped terrain requires caller-supplied planned cells before adapter precompute.");
			return false;
		}
		if (InOutRequest.FootprintSize.X <= 1 || InOutRequest.FootprintSize.Y <= 1
			|| Config.SharedCellSizeInBlocks.X <= 0
			|| Config.SharedCellSizeInBlocks.Y <= 0
			|| Config.SharedCellSizeInBlocks.Z <= 0
			|| Config.SharedCellSizeInBlocks.X != Config.SharedCellSizeInBlocks.Y
			|| Config.StepHeightBlocks <= 0)
		{
			OutFailureReason = TEXT("Fake stepped terrain requires a multi-cell footprint, equal positive XY cell dimensions, positive Z cell height, and a positive step height.");
			return false;
		}

		const int32 AxisCellCount = Config.SplitAxis == ECenteredHalfStepAxis::X
			? InOutRequest.FootprintSize.X
			: InOutRequest.FootprintSize.Y;
		const int32 SplitCell = Config.SplitCellIndex == INDEX_NONE
			? AxisCellCount / 2
			: FMath::Clamp(Config.SplitCellIndex, 1, AxisCellCount - 1);
		auto BuildSurfaceMap = [&Config, SplitCell](const TArray<FLayoutPlannedCell>& Cells)
		{
			TMap<FIntPoint, int32> SurfaceZByBlockXY;
			for (const FLayoutPlannedCell& PlannedCell : Cells)
			{
				const bool bRaised = Config.SplitAxis == ECenteredHalfStepAxis::X
					? PlannedCell.Cell.X >= SplitCell
					: PlannedCell.Cell.Y >= SplitCell;
				const FIntPoint BlockXY(
					Config.FootprintMinBlockWorldPos.X + PlannedCell.Cell.X * Config.SharedCellSizeInBlocks.X,
					Config.FootprintMinBlockWorldPos.Y + PlannedCell.Cell.Y * Config.SharedCellSizeInBlocks.Y);
				SurfaceZByBlockXY.FindOrAdd(BlockXY) = Config.LowerSurfaceZ + (bRaised ? Config.StepHeightBlocks : 0);
			}
			return SurfaceZByBlockXY;
		};
		const TMap<FIntPoint, int32> SurfaceZByBlockXY = BuildSurfaceMap(SourceCells);

		FLayoutSteppedTerrainSupportMap SupportMap;
		if (!FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
				Config.FootprintMinBlockWorldPos,
				Config.SharedCellSizeInBlocks,
				Config.SharedCellSizeInBlocks.X,
				SourceCells,
				SurfaceZByBlockXY,
				SupportMap,
				OutFailureReason))
		{
			return false;
		}

		TSet<FIntVector> PlannedCellSet;
		for (const FLayoutPlannedCell& PlannedCell : SourceCells)
		{
			PlannedCellSet.Add(PlannedCell.Cell);
		}
		TArray<FLayoutPlannedCell> NeighborHaloCells;
		for (const FLayoutPlannedCell& PlannedCell : SourceCells)
		{
			for (const FIntVector& Delta : {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)})
			{
				const FIntVector NeighborCell = PlannedCell.Cell + Delta;
				if (PlannedCellSet.Contains(NeighborCell)
					|| NeighborHaloCells.ContainsByPredicate([NeighborCell](const FLayoutPlannedCell& Candidate)
					{
						return Candidate.Cell == NeighborCell;
					}))
				{
					continue;
				}
				FLayoutPlannedCell& HaloCell = NeighborHaloCells.AddDefaulted_GetRef();
				HaloCell.Cell = NeighborCell;
				HaloCell.Intent = ELayoutCellIntent::Interior;
			}
		}
		FLayoutSteppedTerrainSupportMap NeighborHaloMap;
		if (!NeighborHaloCells.IsEmpty()
			&& !FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
				Config.FootprintMinBlockWorldPos,
				Config.SharedCellSizeInBlocks,
				Config.SharedCellSizeInBlocks.X,
				NeighborHaloCells,
				BuildSurfaceMap(NeighborHaloCells),
				NeighborHaloMap,
				OutFailureReason))
		{
			return false;
		}

		if (OutSourceSupportMap != nullptr)
		{
			*OutSourceSupportMap = SupportMap;
		}
		InOutRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
		InOutRequest.ModuleCatalog.SharedCellSizeInBlocks = Config.SharedCellSizeInBlocks;
		InOutRequest.RootPlacementKind = Config.PlacementKind;
		InOutRequest.SteppedTerrainSupportMap = SupportMap;

		FLayoutModePlan ModePlan;
		ModePlan.Scope = Config.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
			? ELayoutContractRegionScope::Root
			: ELayoutContractRegionScope::Continuation;
		ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
		ModePlan.bUsesSteppedTerrainTopology = true;
		ModePlan.PlacementKind = Config.PlacementKind;
		ModePlan.SiteCenterBlockWorldPos = FIntVector(
			Config.FootprintMinBlockWorldPos.X + (InOutRequest.FootprintSize.X * Config.SharedCellSizeInBlocks.X) / 2,
			Config.FootprintMinBlockWorldPos.Y + (InOutRequest.FootprintSize.Y * Config.SharedCellSizeInBlocks.Y) / 2,
			Config.LowerSurfaceZ);
		ModePlan.SolveSeed = InOutRequest.Seed;
		ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth = FMath::Max(
			ModePlan.PlacementPolicy.TerrainTransition.MaxFoundationDepth,
			Config.StepHeightBlocks);
		ModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(ModePlan.PlacementShiftCells);
		ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(ModePlan);
		InOutRequest.bHasSelectedModePlan = true;
		InOutRequest.SelectedModePlan = ModePlan;

		FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
		SurfaceSearch.TerrainSearchStartZ = Config.LowerSurfaceZ + Config.StepHeightBlocks + Config.SharedCellSizeInBlocks.Z;
		SurfaceSearch.TerrainSearchDepthBlocks = FMath::Max(1, Config.StepHeightBlocks + Config.SharedCellSizeInBlocks.Z * 2);
		const FIntPoint FootprintSizeInBlocks(
			InOutRequest.FootprintSize.X * Config.SharedCellSizeInBlocks.X,
			InOutRequest.FootprintSize.Y * Config.SharedCellSizeInBlocks.Y);
		const TArray<FName> EligibleRows = {Config.EligibleBiomeRowName};
		InOutRequest.FrozenTerrainBiomeAdapterInput =
			FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
				ModePlan.SiteCenterBlockWorldPos,
				Config.FootprintMinBlockWorldPos,
				Config.SharedCellSizeInBlocks,
				FootprintSizeInBlocks,
				SurfaceSearch,
				ModePlan.PlacementPolicy.TerrainTransition,
				FLayoutNoiseCoordinateSettings(),
				Config.SharedCellSizeInBlocks.X,
				Config.EligibleBiomeRowName,
				EligibleRows,
				SupportMap,
				Config.bUseCardinalShiftClusters,
				NeighborHaloMap.SupportSamples);
		InOutRequest.bHasFrozenTerrainBiomeAdapterInput = true;
		return FLayoutContractPipeline::TryPrecomputeAdapterOutput(InOutRequest, OutFailureReason);
	}
}
