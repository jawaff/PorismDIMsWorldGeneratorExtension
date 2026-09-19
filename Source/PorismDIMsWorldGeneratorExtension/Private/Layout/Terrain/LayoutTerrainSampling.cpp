// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"

namespace
{
	bool DoesActiveBiomeRowMatchName(const FLayoutActiveBiomeRowRef& RowRef, const FName RequestedName)
	{
		return !RequestedName.IsNone()
			&& (RowRef.RowName == RequestedName || FName(*RowRef.BiomeName) == RequestedName);
	}

	bool DoesActiveBiomeRowMatchAnyName(
		const FLayoutActiveBiomeRowRef& RowRef,
		const TConstArrayView<FName> RequestedNames)
	{
		for (const FName RequestedName : RequestedNames)
		{
			if (DoesActiveBiomeRowMatchName(RowRef, RequestedName))
			{
				return true;
			}
		}

		return false;
	}

	int32 SnapTerrainSurfaceZDownToSharedCellLattice(
		const int32 SharedCellHeightInBlocks,
		const int32 SurfaceZ)
	{
		if (SharedCellHeightInBlocks <= 0)
		{
			return SurfaceZ;
		}

		const int64 LatticeIndex = FMath::FloorToInt64(
			static_cast<double>(SurfaceZ) / static_cast<double>(SharedCellHeightInBlocks));
		return static_cast<int32>(LatticeIndex * static_cast<int64>(SharedCellHeightInBlocks));
	}


	template <typename TMatchesOwnedSolid>
	FLayoutTerrainSurfaceSample SampleTopDownOwnedActiveBiomeSurfaceFromWorld(
		AChunkWorldCore* const World,
		const FIntPoint& BlockXY,
		const int32 SearchStartZ,
		const int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		TMatchesOwnedSolid&& MatchesOwnedSolid)
	{
		FLayoutTerrainSurfaceSample Result;
		Result.BlockXY = BlockXY;
		if (World == nullptr || !ActiveBiomeSampler.IsInitialized())
		{
			return Result;
		}

		const int32 SafeDepth = FMath::Max(1, SearchDepthBlocks);
		auto BuildBlockWorldPosition =
			[&BlockXY](const int32 ZBlockWorld)
		{
			return FIntVector(BlockXY.X, BlockXY.Y, ZBlockWorld);
		};
		auto TrySampleOwnedSolidAtZ =
			[&](
				const int32 ZBlockWorld,
				FLayoutTerrainSurfaceSample& OutSurface,
				bool& bOutMatches)
		{
			bOutMatches = false;
			OutSurface = FLayoutTerrainSurfaceSample();
			OutSurface.BlockXY = BlockXY;

			const FIntVector BlockWorldPosition = BuildBlockWorldPosition(ZBlockWorld);
			FLayoutActiveBiomeSample BiomeSample;
			if (!ActiveBiomeSampler.SampleAtBlockPosition(BlockWorldPosition, CoordinateSettings, BiomeSample))
			{
				return false;
			}

			if (!MatchesOwnedSolid(BiomeSample) || !BiomeSample.bTerrainSolid)
			{
				return true;
			}

			const int32 MaterialIndex = World->GetBlockValueByBlockWorldPos(
				BlockWorldPosition,
				ERessourceType::MaterialIndex,
				0);
			if (MaterialIndex == EmptyMaterial)
			{
				return true;
			}

			bOutMatches = true;
			OutSurface.bIsValid = true;
			OutSurface.SurfaceBlockWorldPos = BlockWorldPosition;
			OutSurface.SurfaceMaterialIndex = MaterialIndex;
			OutSurface.SurfaceBiomeSwitchIndex = World->GetBlockValueByBlockWorldPos(
				BlockWorldPosition,
				ERessourceType::BiomeSwitchIndex,
				0);
			return true;
		};

		FLayoutTerrainSurfaceSample HighestMatchingSurface;
		bool bStartMatches = false;
		if (!TrySampleOwnedSolidAtZ(SearchStartZ, HighestMatchingSurface, bStartMatches))
		{
			return Result;
		}

		// Project-specific parity fix: world-backed stepped support must recover
		// the highest owned solid reachable within the same search budget that
		// the noise-backed active-biome sampler already uses.
		if (bStartMatches)
		{
			for (int32 ZOffset = 1; ZOffset < SafeDepth; ++ZOffset)
			{
				FLayoutTerrainSurfaceSample CandidateSurface;
				bool bCandidateMatches = false;
				if (!TrySampleOwnedSolidAtZ(SearchStartZ + ZOffset, CandidateSurface, bCandidateMatches))
				{
					return Result;
				}

				if (!bCandidateMatches)
				{
					return HighestMatchingSurface;
				}

				HighestMatchingSurface = CandidateSurface;
			}

			return HighestMatchingSurface;
		}

		for (int32 ZOffset = 0; ZOffset < SafeDepth; ++ZOffset)
		{
			FLayoutTerrainSurfaceSample CandidateSurface;
			bool bCandidateMatches = false;
			if (!TrySampleOwnedSolidAtZ(SearchStartZ - ZOffset, CandidateSurface, bCandidateMatches))
			{
				return Result;
			}

			if (bCandidateMatches)
			{
				return CandidateSurface;
			}
		}

		return Result;
	}

	TArray<int32> BuildSampleAxisPositions(const int32 ExtentInBlocks, const int32 GridSpacing)
	{
		TArray<int32> Positions;
		if (ExtentInBlocks <= 0)
		{
			return Positions;
		}

		const int32 ClampedSpacing = FMath::Max(1, GridSpacing);
		for (int32 Position = 0; Position < ExtentInBlocks; Position += ClampedSpacing)
		{
			Positions.AddUnique(Position);
		}

		Positions.AddUnique(ExtentInBlocks - 1);
		Positions.Sort();
		return Positions;
	}

	struct FLayoutSamplePair
	{
		const FLayoutTerrainSurfaceSample* Left = nullptr;
		const FLayoutTerrainSurfaceSample* Right = nullptr;
	};

	FLayoutTerrainPerimeterTransitionResult BuildPerimeterTransitionAnalysis(
		const FIntVector& CandidateAnchorBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		const int32 MaxSupportableNeighborHeightDelta,
		TFunctionRef<FLayoutTerrainSurfaceSample(const FIntPoint&)> SampleSurface)
	{
		FLayoutTerrainPerimeterTransitionResult Result;
		if (FootprintSizeInBlocks.X <= 0 || FootprintSizeInBlocks.Y <= 0)
		{
			return Result;
		}

		auto AddPerimeterTransition = [&](
			const FIntPoint& AdjacentFootprintBlockXY,
			const FIntPoint& PerimeterBlockXY)
		{
			const FLayoutTerrainSurfaceSample AdjacentFootprintSample = SampleSurface(AdjacentFootprintBlockXY);
			if (!AdjacentFootprintSample.bIsValid)
			{
				return;
			}

			const FLayoutTerrainSurfaceSample PerimeterSample = SampleSurface(PerimeterBlockXY);
			if (!PerimeterSample.bIsValid)
			{
				Result.MissingPerimeterBlockXYs.AddUnique(PerimeterBlockXY);
				return;
			}

			FLayoutTerrainPerimeterTransition& Transition = Result.BoundaryTransitions.AddDefaulted_GetRef();
			Transition.AdjacentFootprintBlockXY = AdjacentFootprintBlockXY;
			Transition.PerimeterBlockXY = PerimeterBlockXY;
			Transition.FootprintSurfaceZ = AdjacentFootprintSample.SurfaceBlockWorldPos.Z;
			Transition.PerimeterSurfaceZ = PerimeterSample.SurfaceBlockWorldPos.Z;
			Transition.StepHeight = FMath::Abs(Transition.FootprintSurfaceZ - Transition.PerimeterSurfaceZ);
			Transition.bIsSupportableTransition = Transition.StepHeight <= MaxSupportableNeighborHeightDelta;

			Result.bHasSupportableTransitions |= Transition.bIsSupportableTransition;
			Result.bHasExcessiveTransitions |= !Transition.bIsSupportableTransition;
		};

		for (int32 LocalX = 0; LocalX < FootprintSizeInBlocks.X; ++LocalX)
		{
			AddPerimeterTransition(
				FIntPoint(CandidateAnchorBlockWorldPos.X + LocalX, CandidateAnchorBlockWorldPos.Y),
				FIntPoint(CandidateAnchorBlockWorldPos.X + LocalX, CandidateAnchorBlockWorldPos.Y - 1));
			AddPerimeterTransition(
				FIntPoint(CandidateAnchorBlockWorldPos.X + LocalX, CandidateAnchorBlockWorldPos.Y + FootprintSizeInBlocks.Y - 1),
				FIntPoint(CandidateAnchorBlockWorldPos.X + LocalX, CandidateAnchorBlockWorldPos.Y + FootprintSizeInBlocks.Y));
		}

		for (int32 LocalY = 0; LocalY < FootprintSizeInBlocks.Y; ++LocalY)
		{
			AddPerimeterTransition(
				FIntPoint(CandidateAnchorBlockWorldPos.X, CandidateAnchorBlockWorldPos.Y + LocalY),
				FIntPoint(CandidateAnchorBlockWorldPos.X - 1, CandidateAnchorBlockWorldPos.Y + LocalY));
			AddPerimeterTransition(
				FIntPoint(CandidateAnchorBlockWorldPos.X + FootprintSizeInBlocks.X - 1, CandidateAnchorBlockWorldPos.Y + LocalY),
				FIntPoint(CandidateAnchorBlockWorldPos.X + FootprintSizeInBlocks.X, CandidateAnchorBlockWorldPos.Y + LocalY));
		}

		Result.BoundaryTransitions.Sort([](
			const FLayoutTerrainPerimeterTransition& Left,
			const FLayoutTerrainPerimeterTransition& Right)
		{
			if (Left.PerimeterBlockXY.Y != Right.PerimeterBlockXY.Y)
			{
				return Left.PerimeterBlockXY.Y < Right.PerimeterBlockXY.Y;
			}

			if (Left.PerimeterBlockXY.X != Right.PerimeterBlockXY.X)
			{
				return Left.PerimeterBlockXY.X < Right.PerimeterBlockXY.X;
			}

			if (Left.AdjacentFootprintBlockXY.Y != Right.AdjacentFootprintBlockXY.Y)
			{
				return Left.AdjacentFootprintBlockXY.Y < Right.AdjacentFootprintBlockXY.Y;
			}

			return Left.AdjacentFootprintBlockXY.X < Right.AdjacentFootprintBlockXY.X;
		});

		Result.MissingPerimeterBlockXYs.Sort([](const FIntPoint& Left, const FIntPoint& Right)
		{
			if (Left.Y != Right.Y)
			{
				return Left.Y < Right.Y;
			}

			return Left.X < Right.X;
		});

		return Result;
	}

	FLayoutTerrainAnchorResult ResolveFootprintAnchorFromSampler(
		const FIntVector& CandidateAnchorBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		const int32 GridSpacing,
		const int32 HeightIgnoreThreshold,
		TFunctionRef<FLayoutTerrainSurfaceSample(const FIntPoint&)> SampleSurface,
		TFunctionRef<bool(int32)> IsMaterialAllowed,
		const ELayoutTerrainAnchorFailureKind MissingSurfaceFailureKind,
		const FString& MissingSurfaceMessagePrefix)
	{
		FLayoutTerrainAnchorResult Result;
		if (FootprintSizeInBlocks.X <= 0 || FootprintSizeInBlocks.Y <= 0)
		{
			Result.FailureReason = TEXT("Terrain anchor resolution requires a positive footprint size.");
			return Result;
		}

		const TArray<int32> SampleXs = BuildSampleAxisPositions(FootprintSizeInBlocks.X, GridSpacing);
		const TArray<int32> SampleYs = BuildSampleAxisPositions(FootprintSizeInBlocks.Y, GridSpacing);
		TMap<FIntPoint, int32> SampleHeights;
		bool bEncounteredMissingSupportSurface = false;
		FIntPoint FirstMissingSupportColumn = FIntPoint::ZeroValue;

		bool bFirstValidSample = true;
		for (const int32 LocalY : SampleYs)
		{
			for (const int32 LocalX : SampleXs)
			{
				const FIntPoint SampleXY(
					CandidateAnchorBlockWorldPos.X + LocalX,
					CandidateAnchorBlockWorldPos.Y + LocalY);
				const FLayoutTerrainSurfaceSample Sample = SampleSurface(SampleXY);

				if (!Sample.bIsValid)
				{
					if (!bEncounteredMissingSupportSurface)
					{
						bEncounteredMissingSupportSurface = true;
						FirstMissingSupportColumn = SampleXY;
					}
					continue;
				}

				if (!IsMaterialAllowed(Sample.SurfaceMaterialIndex))
				{
					Result.FailureKind = ELayoutTerrainAnchorFailureKind::DisallowedSurfaceMaterial;
					Result.FailureReason = FString::Printf(
						TEXT("Terrain surface material %d is not allowed under footprint column (%d, %d)."),
						Sample.SurfaceMaterialIndex,
						SampleXY.X,
						SampleXY.Y);
					return Result;
				}

				Result.Samples.Add(Sample);
				SampleHeights.Add(FIntPoint(LocalX, LocalY), Sample.SurfaceBlockWorldPos.Z);

				if (bFirstValidSample)
				{
					bFirstValidSample = false;
					Result.MinimumTerrainZ = Sample.SurfaceBlockWorldPos.Z;
					Result.MaximumTerrainZ = Sample.SurfaceBlockWorldPos.Z;
				}
				else
				{
					Result.MinimumTerrainZ = FMath::Min(Result.MinimumTerrainZ, Sample.SurfaceBlockWorldPos.Z);
					Result.MaximumTerrainZ = FMath::Max(Result.MaximumTerrainZ, Sample.SurfaceBlockWorldPos.Z);
				}
			}
		}

		if (Result.Samples.IsEmpty())
		{
			Result.FailureKind = MissingSurfaceFailureKind;
			Result.FailureReason = FString::Printf(
				TEXT("%s footprint column (%d, %d)."),
				*MissingSurfaceMessagePrefix,
				FirstMissingSupportColumn.X,
				FirstMissingSupportColumn.Y);
			return Result;
		}

		if (bEncounteredMissingSupportSurface)
		{
			Result.FailureKind = ELayoutTerrainAnchorFailureKind::SparseSupportSurface;
			Result.FailureReason = FString::Printf(
				TEXT("%s footprint column (%d, %d)."),
				*MissingSurfaceMessagePrefix,
				FirstMissingSupportColumn.X,
				FirstMissingSupportColumn.Y);
			return Result;
		}

		for (int32 YIndex = 0; YIndex < SampleYs.Num(); ++YIndex)
		{
			for (int32 XIndex = 0; XIndex < SampleXs.Num(); ++XIndex)
			{
				const FIntPoint CurrentKey(SampleXs[XIndex], SampleYs[YIndex]);
				const int32 CurrentHeight = SampleHeights[CurrentKey];

				if (XIndex + 1 < SampleXs.Num())
				{
					const int32 NeighborHeight =
						SampleHeights[FIntPoint(SampleXs[XIndex + 1], SampleYs[YIndex])];
					Result.MaximumObservedNeighborHeightDelta = FMath::Max(
						Result.MaximumObservedNeighborHeightDelta,
						FMath::Abs(CurrentHeight - NeighborHeight));
				}

				if (YIndex + 1 < SampleYs.Num())
				{
					const int32 NeighborHeight =
						SampleHeights[FIntPoint(SampleXs[XIndex], SampleYs[YIndex + 1])];
					Result.MaximumObservedNeighborHeightDelta = FMath::Max(
						Result.MaximumObservedNeighborHeightDelta,
						FMath::Abs(CurrentHeight - NeighborHeight));
				}
			}
		}

		Result.TerrainClassification =
			FLayoutTerrainSampling::ClassifyFootprintTerrainSamples(Result.Samples, 0);
		Result.PerimeterTransitionAnalysis = BuildPerimeterTransitionAnalysis(
			CandidateAnchorBlockWorldPos,
			FootprintSizeInBlocks,
			0,
			SampleSurface);

		if (false)
		{
			Result.FailureKind = ELayoutTerrainAnchorFailureKind::ExcessiveNeighborHeightDelta;
			Result.FailureReason = FString::Printf(
				TEXT("Terrain step %d exceeds the allowed neighboring sample delta %d."),
				Result.MaximumObservedNeighborHeightDelta,
				0);
			return Result;
		}

		if ((Result.MaximumTerrainZ - Result.MinimumTerrainZ) > HeightIgnoreThreshold)
		{
			Result.FailureKind = ELayoutTerrainAnchorFailureKind::ExcessiveFootprintVariation;
			Result.FailureReason = FString::Printf(
				TEXT("Terrain variation %d exceeds the allowed footprint threshold %d."),
				Result.MaximumTerrainZ - Result.MinimumTerrainZ,
				HeightIgnoreThreshold);
			return Result;
		}

		Result.AnchorBlockWorldPos = CandidateAnchorBlockWorldPos;
		Result.AnchorBlockWorldPos.Z = Result.MaximumTerrainZ + 1;
		Result.RequiredFoundationDepth =
			FMath::Max(0, Result.AnchorBlockWorldPos.Z - 1 - Result.MinimumTerrainZ);
		Result.bRequiresFoundationFill = Result.RequiredFoundationDepth > 0;
		Result.bIsValid = true;
		return Result;
	}

	FIntPoint ComputeSteppedTerrainCenterSampleBlockXY(
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntVector& LocalCell)
	{
		return FIntPoint(
			AnchorBlockWorldPos.X + (LocalCell.X * SharedCellSizeInBlocks.X) + (SharedCellSizeInBlocks.X / 2),
			AnchorBlockWorldPos.Y + (LocalCell.Y * SharedCellSizeInBlocks.Y) + (SharedCellSizeInBlocks.Y / 2));
	}

	bool TryBuildSteppedTerrainSupportMapFromSampleGridSurfaceHeights(
		const FIntVector& SharedCellSizeInBlocks,
		const int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TMap<FIntPoint, int32>& SurfaceZBySampleGridXY,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason)
	{
		OutSupportMap = FLayoutSteppedTerrainSupportMap();
		OutFailureReason.Reset();

		if (SharedCellSizeInBlocks.X <= 0 || SharedCellSizeInBlocks.Y <= 0 || SharedCellSizeInBlocks.Z <= 0)
		{
			OutFailureReason = TEXT("Stepped terrain support map requires a positive shared cell size in all axes.");
			return false;
		}

		if (TerrainSampleGridSpacing != SharedCellSizeInBlocks.X || TerrainSampleGridSpacing != SharedCellSizeInBlocks.Y)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped terrain support map requires terrain sample spacing %d to align with the shared cell size (%d, %d)."),
				TerrainSampleGridSpacing,
				SharedCellSizeInBlocks.X,
				SharedCellSizeInBlocks.Y);
			return false;
		}

		if (PlannedCells.IsEmpty())
		{
			OutFailureReason = TEXT("Stepped terrain support map requires at least one planned cell.");
			return false;
		}

		OutSupportMap.SharedCellHeightInBlocks = SharedCellSizeInBlocks.Z;
		OutSupportMap.MaximumObservedNeighborHeightDelta = 0;
		OutSupportMap.MaximumObservedSnappedLevelDelta = 0;

		TMap<FIntVector, int32> SurfaceZByLocalCell;
		TMap<FIntVector, int32> SnappedSupportCeilingZByLocalCell;
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			const FIntPoint SampleGridXY(PlannedCell.Cell.X, PlannedCell.Cell.Y);
			const int32* SurfaceZ = SurfaceZBySampleGridXY.Find(SampleGridXY);
			if (SurfaceZ == nullptr)
			{
				OutSupportMap = FLayoutSteppedTerrainSupportMap();
				OutFailureReason = FString::Printf(
					TEXT("Stepped terrain support map is missing one sampled support surface for planned cell %s at sample grid (%d, %d)."),
					*PlannedCell.Cell.ToString(),
					SampleGridXY.X,
					SampleGridXY.Y);
				return false;
			}

			FLayoutSteppedTerrainSupportSample& SupportSample = OutSupportMap.SupportSamples.AddDefaulted_GetRef();
			SupportSample.LocalCell = PlannedCell.Cell;
			SupportSample.SupportSurfaceZ = *SurfaceZ;
			SupportSample.SnappedSupportFloorZ = SnapTerrainSurfaceZDownToSharedCellLattice(
				SharedCellSizeInBlocks.Z,
				*SurfaceZ);
			SupportSample.SnappedSupportCeilingZ = FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(
				SharedCellSizeInBlocks.Z,
				*SurfaceZ);
			SurfaceZByLocalCell.Add(PlannedCell.Cell, *SurfaceZ);
			SnappedSupportCeilingZByLocalCell.Add(
				PlannedCell.Cell,
				SupportSample.SnappedSupportCeilingZ);
		}

		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			const FIntVector NeighborOffsets[2] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0)};
			const int32* FromSurfaceZ = SurfaceZByLocalCell.Find(PlannedCell.Cell);
			const int32* FromSnappedSupportCeilingZ =
				SnappedSupportCeilingZByLocalCell.Find(PlannedCell.Cell);
			if (FromSurfaceZ == nullptr)
			{
				continue;
			}
			if (FromSnappedSupportCeilingZ == nullptr)
			{
				continue;
			}

			for (const FIntVector& NeighborOffset : NeighborOffsets)
			{
				const FIntVector NeighborCell = PlannedCell.Cell + NeighborOffset;
				const int32* ToSurfaceZ = SurfaceZByLocalCell.Find(NeighborCell);
				const int32* ToSnappedSupportCeilingZ =
					SnappedSupportCeilingZByLocalCell.Find(NeighborCell);
				if (ToSurfaceZ == nullptr || ToSnappedSupportCeilingZ == nullptr)
				{
					continue;
				}

				FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep = OutSupportMap.AdjacencySteps.AddDefaulted_GetRef();
				AdjacencyStep.FromCell = PlannedCell.Cell;
				AdjacencyStep.ToCell = NeighborCell;
				AdjacencyStep.StepHeightBlocks = FMath::Abs(*FromSurfaceZ - *ToSurfaceZ);
				AdjacencyStep.SnappedLevelDelta =
					FMath::Abs(*FromSnappedSupportCeilingZ - *ToSnappedSupportCeilingZ)
					/ SharedCellSizeInBlocks.Z;
				OutSupportMap.MaximumObservedNeighborHeightDelta = FMath::Max(
					OutSupportMap.MaximumObservedNeighborHeightDelta,
					AdjacencyStep.StepHeightBlocks);
				OutSupportMap.MaximumObservedSnappedLevelDelta = FMath::Max(
					OutSupportMap.MaximumObservedSnappedLevelDelta,
					AdjacencyStep.SnappedLevelDelta);
			}
		}

		OutSupportMap.SupportSamples.Sort([](const FLayoutSteppedTerrainSupportSample& Left, const FLayoutSteppedTerrainSupportSample& Right)
		{
			if (Left.LocalCell.Z != Right.LocalCell.Z)
			{
				return Left.LocalCell.Z < Right.LocalCell.Z;
			}
			if (Left.LocalCell.Y != Right.LocalCell.Y)
			{
				return Left.LocalCell.Y < Right.LocalCell.Y;
			}
			return Left.LocalCell.X < Right.LocalCell.X;
		});

		OutSupportMap.AdjacencySteps.Sort([](const FLayoutSteppedTerrainAdjacencyStep& Left, const FLayoutSteppedTerrainAdjacencyStep& Right)
		{
			if (Left.FromCell.Z != Right.FromCell.Z)
			{
				return Left.FromCell.Z < Right.FromCell.Z;
			}
			if (Left.FromCell.Y != Right.FromCell.Y)
			{
				return Left.FromCell.Y < Right.FromCell.Y;
			}
			if (Left.FromCell.X != Right.FromCell.X)
			{
				return Left.FromCell.X < Right.FromCell.X;
			}
			if (Left.ToCell.Z != Right.ToCell.Z)
			{
				return Left.ToCell.Z < Right.ToCell.Z;
			}
			if (Left.ToCell.Y != Right.ToCell.Y)
			{
				return Left.ToCell.Y < Right.ToCell.Y;
			}
			return Left.ToCell.X < Right.ToCell.X;
		});

		return true;
	}
}

FLayoutTerrainSurfaceSample FLayoutTerrainSampling::SampleTopDownSurface(
	const FIntPoint& BlockXY,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	TFunctionRef<int32(const FIntVector&)> ReadMaterialIndex,
	TFunctionRef<int32(const FIntVector&)> ReadBiomeSwitchIndex)
{
	FLayoutTerrainSurfaceSample Result;
	Result.BlockXY = BlockXY;

	const int32 SearchDepth = FMath::Max(1, SearchDepthBlocks);
	for (int32 Z = SearchStartZ; Z > SearchStartZ - SearchDepth; --Z)
	{
		const FIntVector CandidatePos(BlockXY.X, BlockXY.Y, Z);
		const int32 MaterialIndex = ReadMaterialIndex(CandidatePos);
		if (MaterialIndex == EmptyMaterial)
		{
			continue;
		}

		Result.bIsValid = true;
		Result.SurfaceBlockWorldPos = CandidatePos;
		Result.SurfaceMaterialIndex = MaterialIndex;
		Result.SurfaceBiomeSwitchIndex = ReadBiomeSwitchIndex(CandidatePos);
		return Result;
	}

	return Result;
}

FLayoutTerrainAnchorResult FLayoutTerrainSampling::ResolveFootprintAnchor(
	const FIntVector& CandidateAnchorBlockWorldPos,
	const FIntPoint& FootprintSizeInBlocks,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	const int32 GridSpacing,
	const int32 HeightIgnoreThreshold,
	TFunctionRef<int32(const FIntVector&)> ReadMaterialIndex,
	TFunctionRef<int32(const FIntVector&)> ReadBiomeSwitchIndex,
	TFunctionRef<bool(int32)> IsMaterialAllowed)
{
	return ResolveFootprintAnchorFromSampler(
		CandidateAnchorBlockWorldPos,
		FootprintSizeInBlocks,
		GridSpacing,
		HeightIgnoreThreshold,
		[&](const FIntPoint& SampleXY)
		{
			return SampleTopDownSurface(
				SampleXY,
				SearchStartZ,
				SearchDepthBlocks,
				ReadMaterialIndex,
				ReadBiomeSwitchIndex);
		},
		IsMaterialAllowed,
		ELayoutTerrainAnchorFailureKind::MissingSupportSurface,
		TEXT("No supporting terrain surface was found under"));
}

FLayoutTerrainSurfaceSample FLayoutTerrainSampling::SampleTopDownSurfaceFromWorld(
	AChunkWorldCore* World,
	const FIntPoint& BlockXY,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks)
{
	if (World == nullptr)
	{
		return FLayoutTerrainSurfaceSample();
	}

	return SampleTopDownSurface(
		BlockXY,
		SearchStartZ,
		SearchDepthBlocks,
		[World](const FIntVector& BlockPos)
		{
			return World->GetBlockValueByBlockWorldPos(BlockPos, ERessourceType::MaterialIndex, 0);
		},
		[World](const FIntVector& BlockPos)
		{
			return World->GetBlockValueByBlockWorldPos(BlockPos, ERessourceType::BiomeSwitchIndex, 0);
		});
}

FLayoutTerrainSurfaceSample FLayoutTerrainSampling::SampleTopDownEligibleBiomeSurfaceFromWorld(
	AChunkWorldCore* const World,
	const FName EligibleBiomeRowName,
	const FIntPoint& BlockXY,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler)
{
	return SampleTopDownOwnedActiveBiomeSurfaceFromWorld(
		World,
		BlockXY,
		SearchStartZ,
		SearchDepthBlocks,
		CoordinateSettings,
		ActiveBiomeSampler,
		[EligibleBiomeRowName](const FLayoutActiveBiomeSample& Sample)
		{
			return EligibleBiomeRowName.IsNone()
				? Sample.bAnyPositiveDomain
				: (Sample.bAnyPositiveDomain
					&& DoesActiveBiomeRowMatchName(Sample.WinningRow, EligibleBiomeRowName));
		});
}

FLayoutTerrainSurfaceSample FLayoutTerrainSampling::SampleTopDownEligibleBiomeSurfaceFromAnyRowFromWorld(
	AChunkWorldCore* const World,
	const TConstArrayView<FName> EligibleBiomeRowNames,
	const FIntPoint& BlockXY,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler)
{
	if (EligibleBiomeRowNames.IsEmpty())
	{
		FLayoutTerrainSurfaceSample Result;
		Result.BlockXY = BlockXY;
		return Result;
	}

	return SampleTopDownOwnedActiveBiomeSurfaceFromWorld(
		World,
		BlockXY,
		SearchStartZ,
		SearchDepthBlocks,
		CoordinateSettings,
		ActiveBiomeSampler,
		[EligibleBiomeRowNames](const FLayoutActiveBiomeSample& Sample)
		{
			return Sample.bAnyPositiveDomain
				&& DoesActiveBiomeRowMatchAnyName(Sample.WinningRow, EligibleBiomeRowNames);
		});
}

FLayoutTerrainAnchorResult FLayoutTerrainSampling::ResolveFootprintAnchorFromWorld(
	AChunkWorldCore* World,
	const FIntVector& CandidateAnchorBlockWorldPos,
	const FIntPoint& FootprintSizeInBlocks,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	const int32 GridSpacing,
	const int32 HeightIgnoreThreshold,
	TFunctionRef<bool(int32)> IsMaterialAllowed)
{
	if (World == nullptr)
	{
		FLayoutTerrainAnchorResult Result;
		Result.FailureReason = TEXT("Cannot resolve a terrain anchor from a null chunk world.");
		return Result;
	}

	return ResolveFootprintAnchor(
		CandidateAnchorBlockWorldPos,
		FootprintSizeInBlocks,
		SearchStartZ,
		SearchDepthBlocks,
		GridSpacing,
		HeightIgnoreThreshold,
		[World](const FIntVector& BlockPos)
		{
			return World->GetBlockValueByBlockWorldPos(BlockPos, ERessourceType::MaterialIndex, 0);
		},
		[World](const FIntVector& BlockPos)
		{
			return World->GetBlockValueByBlockWorldPos(BlockPos, ERessourceType::BiomeSwitchIndex, 0);
		},
		IsMaterialAllowed);
}

FLayoutTerrainAnchorResult FLayoutTerrainSampling::ResolveFootprintAnchorFromActiveBiomeSurface(
	AChunkWorldCore* const World,
	const FName EligibleBiomeRowName,
	const FIntVector& CandidateAnchorBlockWorldPos,
	const FIntPoint& FootprintSizeInBlocks,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	const int32 GridSpacing,
	const int32 HeightIgnoreThreshold,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
	TFunctionRef<bool(int32)> IsMaterialAllowed)
{
	if (World == nullptr)
	{
		FLayoutTerrainAnchorResult Result;
		Result.FailureReason = TEXT("Cannot resolve a terrain anchor from a null chunk world.");
		return Result;
	}

	if (!ActiveBiomeSampler.IsInitialized())
	{
		FLayoutTerrainAnchorResult Result;
		Result.FailureReason = TEXT("Cannot resolve an active-biome terrain anchor without one initialized biome sampler.");
		return Result;
	}

	const FString MissingSurfaceMessagePrefix = EligibleBiomeRowName.IsNone()
		? TEXT("No active biome surface was found under")
		: FString::Printf(
			TEXT("No eligible active biome surface for row '%s' was found under"),
			*EligibleBiomeRowName.ToString());
	return ResolveFootprintAnchorFromSampler(
		CandidateAnchorBlockWorldPos,
		FootprintSizeInBlocks,
		GridSpacing,
		HeightIgnoreThreshold,
		[&](const FIntPoint& SampleXY)
		{
			return SampleTopDownEligibleBiomeSurfaceFromWorld(
				World,
				EligibleBiomeRowName,
				SampleXY,
				SearchStartZ,
				SearchDepthBlocks,
				CoordinateSettings,
				ActiveBiomeSampler);
		},
		IsMaterialAllowed,
		ELayoutTerrainAnchorFailureKind::MissingEligibleBiomeSurface,
		MissingSurfaceMessagePrefix);
}

FLayoutTerrainAnchorResult FLayoutTerrainSampling::ResolveFootprintAnchorFromAnyActiveBiomeSurface(
	AChunkWorldCore* const World,
	const TConstArrayView<FName> EligibleBiomeRowNames,
	const FIntVector& CandidateAnchorBlockWorldPos,
	const FIntPoint& FootprintSizeInBlocks,
	const int32 SearchStartZ,
	const int32 SearchDepthBlocks,
	const int32 GridSpacing,
	const int32 HeightIgnoreThreshold,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
	TFunctionRef<bool(int32)> IsMaterialAllowed)
{
	if (World == nullptr)
	{
		FLayoutTerrainAnchorResult Result;
		Result.FailureReason = TEXT("Cannot resolve a terrain anchor from a null chunk world.");
		return Result;
	}

	if (!ActiveBiomeSampler.IsInitialized())
	{
		FLayoutTerrainAnchorResult Result;
		Result.FailureReason = TEXT("Cannot resolve an active-biome terrain anchor without one initialized biome sampler.");
		return Result;
	}

	const FString MissingSurfaceMessagePrefix =
		TEXT("No eligible active biome surface from the supplied row allow-list was found under");
	return ResolveFootprintAnchorFromSampler(
		CandidateAnchorBlockWorldPos,
		FootprintSizeInBlocks,
		GridSpacing,
		HeightIgnoreThreshold,
		[&](const FIntPoint& SampleXY)
		{
			return SampleTopDownEligibleBiomeSurfaceFromAnyRowFromWorld(
				World,
				EligibleBiomeRowNames,
				SampleXY,
				SearchStartZ,
				SearchDepthBlocks,
				CoordinateSettings,
				ActiveBiomeSampler);
		},
		IsMaterialAllowed,
		ELayoutTerrainAnchorFailureKind::MissingEligibleBiomeSurface,
		MissingSurfaceMessagePrefix);
}

FLayoutTerrainClassificationResult FLayoutTerrainSampling::ClassifyFootprintTerrainSamples(
	const TArray<FLayoutTerrainSurfaceSample>& Samples,
	const int32 MaxSupportableNeighborHeightDelta)
{
	FLayoutTerrainClassificationResult Result;
	if (Samples.IsEmpty())
	{
		return Result;
	}

	TMap<FIntPoint, const FLayoutTerrainSurfaceSample*> SampleMap;
	TMap<FIntPoint, ELayoutTerrainCellShape> ShapeByBlockXY;
	for (const FLayoutTerrainSurfaceSample& Sample : Samples)
	{
		if (!Sample.bIsValid)
		{
			continue;
		}

		SampleMap.Add(Sample.BlockXY, &Sample);
		ShapeByBlockXY.Add(Sample.BlockXY, ELayoutTerrainCellShape::Flat);
	}

	auto PromoteShape = [&ShapeByBlockXY](const FIntPoint& BlockXY, const ELayoutTerrainCellShape NewShape)
	{
		ELayoutTerrainCellShape& CurrentShape = ShapeByBlockXY.FindOrAdd(BlockXY, ELayoutTerrainCellShape::Flat);
		if (CurrentShape == ELayoutTerrainCellShape::SteepEdge || CurrentShape == NewShape)
		{
			return;
		}

		if (NewShape == ELayoutTerrainCellShape::SteepEdge
			|| (CurrentShape == ELayoutTerrainCellShape::Flat && NewShape == ELayoutTerrainCellShape::MildSlope))
		{
			CurrentShape = NewShape;
		}
	};

	for (const TPair<FIntPoint, const FLayoutTerrainSurfaceSample*>& Pair : SampleMap)
	{
		const FIntPoint NeighborOffsets[2] = {FIntPoint(1, 0), FIntPoint(0, 1)};
		for (const FIntPoint& Offset : NeighborOffsets)
		{
			const FIntPoint NeighborXY = Pair.Key + Offset;
			const FLayoutTerrainSurfaceSample* const* NeighborSamplePtr = SampleMap.Find(NeighborXY);
			if (NeighborSamplePtr == nullptr || *NeighborSamplePtr == nullptr)
			{
				continue;
			}

			const int32 StepHeight = FMath::Abs(Pair.Value->SurfaceBlockWorldPos.Z - (*NeighborSamplePtr)->SurfaceBlockWorldPos.Z);
			if (StepHeight <= 0)
			{
				continue;
			}

			if (StepHeight <= MaxSupportableNeighborHeightDelta)
			{
				PromoteShape(Pair.Key, ELayoutTerrainCellShape::MildSlope);
				PromoteShape(NeighborXY, ELayoutTerrainCellShape::MildSlope);
				Result.bHasMildSlopeTransitions = true;

				FLayoutTerrainTransitionRequirement& Transition = Result.TransitionRequirements.AddDefaulted_GetRef();
				Transition.FromBlockXY = Pair.Key;
				Transition.ToBlockXY = NeighborXY;
				Transition.StepHeight = StepHeight;
			}
			else
			{
				PromoteShape(Pair.Key, ELayoutTerrainCellShape::SteepEdge);
				PromoteShape(NeighborXY, ELayoutTerrainCellShape::SteepEdge);
				Result.bHasSteepEdges = true;
			}
		}
	}

	Result.CellClassifications.Reserve(SampleMap.Num());
	for (const FLayoutTerrainSurfaceSample& Sample : Samples)
	{
		if (!Sample.bIsValid)
		{
			continue;
		}

		FLayoutTerrainCellClassification& Classification = Result.CellClassifications.AddDefaulted_GetRef();
		Classification.BlockXY = Sample.BlockXY;
		Classification.SurfaceZ = Sample.SurfaceBlockWorldPos.Z;
		Classification.Shape = ShapeByBlockXY.FindRef(Sample.BlockXY);
	}

	Result.CellClassifications.Sort([](const FLayoutTerrainCellClassification& Left, const FLayoutTerrainCellClassification& Right)
	{
		if (Left.BlockXY.Y != Right.BlockXY.Y)
		{
			return Left.BlockXY.Y < Right.BlockXY.Y;
		}

		return Left.BlockXY.X < Right.BlockXY.X;
	});

	return Result;
}

bool FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMap(
	const FIntVector& AnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 TerrainSampleGridSpacing,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const FLayoutTerrainAnchorResult& AnchorResult,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FString& OutFailureReason)
{
	if (!AnchorResult.bIsValid || AnchorResult.Samples.IsEmpty())
	{
		OutSupportMap = FLayoutSteppedTerrainSupportMap();
		OutFailureReason = TEXT("Stepped terrain support map requires a valid terrain anchor result with sampled support surfaces.");
		return false;
	}

	TMap<FIntPoint, int32> SurfaceZByBlockXY;
	for (const FLayoutTerrainSurfaceSample& Sample : AnchorResult.Samples)
	{
		if (!Sample.bIsValid)
		{
			continue;
		}

		SurfaceZByBlockXY.Add(Sample.BlockXY, Sample.SurfaceBlockWorldPos.Z);
	}

	return TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
		AnchorBlockWorldPos,
		SharedCellSizeInBlocks,
		TerrainSampleGridSpacing,
		PlannedCells,
		SurfaceZByBlockXY,
		OutSupportMap,
		OutFailureReason);
}

int32 FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(
	const int32 SharedCellHeightInBlocks,
	const int32 SurfaceZ)
{
	if (SharedCellHeightInBlocks <= 0)
	{
		return SurfaceZ;
	}

	const int64 LatticeIndex = FMath::CeilToInt64(
		static_cast<double>(SurfaceZ) / static_cast<double>(SharedCellHeightInBlocks));
	return static_cast<int32>(LatticeIndex * static_cast<int64>(SharedCellHeightInBlocks));
}

bool FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
	const FIntVector& AnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 TerrainSampleGridSpacing,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const TMap<FIntPoint, int32>& SurfaceZByBlockXY,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FString& OutFailureReason)
{
	OutSupportMap = FLayoutSteppedTerrainSupportMap();
	OutFailureReason.Reset();

	if (SharedCellSizeInBlocks.X <= 0 || SharedCellSizeInBlocks.Y <= 0 || SharedCellSizeInBlocks.Z <= 0)
	{
		OutFailureReason = TEXT("Stepped terrain support map requires a positive shared cell size in all axes.");
		return false;
	}

	if (TerrainSampleGridSpacing != SharedCellSizeInBlocks.X || TerrainSampleGridSpacing != SharedCellSizeInBlocks.Y)
	{
		OutFailureReason = FString::Printf(
			TEXT("Stepped terrain support map requires terrain sample spacing %d to align with the shared cell size (%d, %d)."),
			TerrainSampleGridSpacing,
			SharedCellSizeInBlocks.X,
			SharedCellSizeInBlocks.Y);
		return false;
	}

	if (PlannedCells.IsEmpty())
	{
		OutFailureReason = TEXT("Stepped terrain support map requires at least one planned cell.");
		return false;
	}

	TMap<FIntPoint, int32> SurfaceZBySampleGridXY;
	for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
	{
		const FIntPoint SampleGridXY(PlannedCell.Cell.X, PlannedCell.Cell.Y);
		const FIntPoint SampleBlockXY(
			AnchorBlockWorldPos.X + (PlannedCell.Cell.X * SharedCellSizeInBlocks.X),
			AnchorBlockWorldPos.Y + (PlannedCell.Cell.Y * SharedCellSizeInBlocks.Y));
		const int32* SurfaceZ = SurfaceZByBlockXY.Find(SampleBlockXY);
		if (SurfaceZ == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped terrain support map is missing an aligned terrain sample for planned cell %s at block XY (%d, %d)."),
				*PlannedCell.Cell.ToString(),
				SampleBlockXY.X,
				SampleBlockXY.Y);
			return false;
		}

		SurfaceZBySampleGridXY.Add(SampleGridXY, *SurfaceZ);
	}

	return TryBuildSteppedTerrainSupportMapFromSampleGridSurfaceHeights(
		SharedCellSizeInBlocks,
		TerrainSampleGridSpacing,
		PlannedCells,
		SurfaceZBySampleGridXY,
		OutSupportMap,
		OutFailureReason);
}

bool FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
	const FIntVector& FootprintMinBlockWorldPos,
	const int32 FootprintWidthBlocks,
	const int32 FootprintHeightBlocks,
	const int32 MinZ,
	const int32 MaxZ,
	const TConstArrayView<uint8> EmptyStates,
	TArray<FLayoutTerrainColumnProfile>& OutProfiles,
	FString& OutFailureReason)
{
	OutProfiles.Reset();
	OutFailureReason.Reset();
	const int32 HeightBlocks = MaxZ - MinZ + 1;
	const int64 ExpectedValues = static_cast<int64>(FootprintWidthBlocks) * FootprintHeightBlocks * HeightBlocks;
	if (FootprintWidthBlocks <= 0 || FootprintHeightBlocks <= 0 || HeightBlocks <= 0 || ExpectedValues > MAX_int32)
	{
		OutFailureReason = TEXT("Terrain column profiling requires positive bounded footprint and Z extent.");
		return false;
	}
	if (EmptyStates.Num() != ExpectedValues)
	{
		OutFailureReason = FString::Printf(TEXT("Terrain column empty-state batch returned %d values; expected %lld."), EmptyStates.Num(), ExpectedValues);
		return false;
	}

	OutProfiles.Reserve(FootprintWidthBlocks * FootprintHeightBlocks);
	for (int32 Y = 0; Y < FootprintHeightBlocks; ++Y)
	{
		for (int32 X = 0; X < FootprintWidthBlocks; ++X)
		{
			FLayoutTerrainColumnProfile& Profile = OutProfiles.AddDefaulted_GetRef();
			Profile.BlockXY = FIntPoint(FootprintMinBlockWorldPos.X + X, FootprintMinBlockWorldPos.Y + Y);
			for (int32 ZOffset = 0; ZOffset < HeightBlocks; ++ZOffset)
			{
				const int32 SampleIndex = (ZOffset * FootprintHeightBlocks + Y) * FootprintWidthBlocks + X;
				const bool bIsEmpty = EmptyStates[SampleIndex] != 0;
				FLayoutTerrainColumnRun* const PreviousRun = Profile.Runs.IsEmpty() ? nullptr : &Profile.Runs.Last();
				if (PreviousRun != nullptr && PreviousRun->bIsEmpty == bIsEmpty)
				{
					PreviousRun->MaxZ = MinZ + ZOffset;
					continue;
				}

				FLayoutTerrainColumnRun& Run = Profile.Runs.AddDefaulted_GetRef();
				Run.MinZ = MinZ + ZOffset;
				Run.MaxZ = Run.MinZ;
				Run.bIsEmpty = bIsEmpty;
			}
		}
	}
	return true;
}

bool FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromActiveBiomeSampler(
	const FIntVector& FootprintMinBlockWorldPos,
	const int32 FootprintWidthBlocks,
	const int32 FootprintHeightBlocks,
	const int32 MinZ,
	const int32 MaxZ,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
	TArray<FLayoutTerrainColumnProfile>& OutProfiles,
	FString& OutFailureReason)
{
	OutProfiles.Reset();
	OutFailureReason.Reset();
	if (!ActiveBiomeSampler.IsInitialized())
	{
		OutFailureReason = TEXT("Generated terrain column profiling requires one initialized active-biome sampler.");
		return false;
	}

	const int32 HeightBlocks = MaxZ - MinZ + 1;
	const int64 ExpectedValues = static_cast<int64>(FootprintWidthBlocks) * FootprintHeightBlocks * HeightBlocks;
	if (FootprintWidthBlocks <= 0 || FootprintHeightBlocks <= 0 || HeightBlocks <= 0 || ExpectedValues > MAX_int32)
	{
		OutFailureReason = TEXT("Generated terrain column profiling requires positive bounded footprint and Z extent.");
		return false;
	}

	TArray<uint8> EmptyStates;
	EmptyStates.Reserve(static_cast<int32>(ExpectedValues));
	for (int32 Z = MinZ; Z <= MaxZ; ++Z)
	{
		for (int32 Y = 0; Y < FootprintHeightBlocks; ++Y)
		{
			for (int32 X = 0; X < FootprintWidthBlocks; ++X)
			{
				FLayoutActiveBiomeSample Sample;
				if (!ActiveBiomeSampler.SampleAtBlockPosition(
						FIntVector(FootprintMinBlockWorldPos.X + X, FootprintMinBlockWorldPos.Y + Y, Z),
						CoordinateSettings,
						Sample))
				{
					OutFailureReason = FString::Printf(TEXT("Generated terrain column profiling failed at (%d,%d,%d)."),
						FootprintMinBlockWorldPos.X + X,
						FootprintMinBlockWorldPos.Y + Y,
						Z);
					OutProfiles.Reset();
					return false;
				}
				EmptyStates.Add(Sample.bTerrainSolid ? 0 : 1);
			}
		}
	}

	return TryBuildTerrainColumnProfilesFromEmptySamples(
		FootprintMinBlockWorldPos,
		FootprintWidthBlocks,
		FootprintHeightBlocks,
		MinZ,
		MaxZ,
		EmptyStates,
		OutProfiles,
		OutFailureReason);
}

bool FLayoutTerrainSampling::TryClassifyCenterColumnEnvironment(
	const FLayoutTerrainColumnProfile& CenterProfile,
	const int32 LayoutEnvelopeMinZ,
	const int32 LayoutEnvelopeHeightInBlocks,
	FLayoutTerrainColumnRun& OutSelectedAirRun,
	bool& bOutUnderground,
	FString& OutFailureReason)
{
	OutSelectedAirRun = FLayoutTerrainColumnRun();
	bOutUnderground = false;
	OutFailureReason.Reset();
	if (CenterProfile.Runs.IsEmpty() || LayoutEnvelopeHeightInBlocks <= 0)
	{
		OutFailureReason = TEXT("Center-column environment classification requires column runs and a positive layout-envelope height.");
		return false;
	}
	const int64 LayoutEnvelopeMaxZ = static_cast<int64>(LayoutEnvelopeMinZ) + LayoutEnvelopeHeightInBlocks - 1;
	int32 SelectedRunIndex = INDEX_NONE;
	int64 BestOverlap = 0;
	int64 BestCenterDistance = MAX_int64;
	const int64 LayoutCenterTimesTwo = static_cast<int64>(LayoutEnvelopeMinZ) + LayoutEnvelopeMaxZ;
	for (int32 RunIndex = 0; RunIndex < CenterProfile.Runs.Num(); ++RunIndex)
	{
		const FLayoutTerrainColumnRun& Run = CenterProfile.Runs[RunIndex];
		if (!Run.bIsEmpty)
		{
			continue;
		}
		const int64 Overlap = FMath::Min<int64>(Run.MaxZ, LayoutEnvelopeMaxZ)
			- FMath::Max<int64>(Run.MinZ, LayoutEnvelopeMinZ) + 1;
		if (Overlap <= 0)
		{
			continue;
		}
		const int64 RunCenterTimesTwo = static_cast<int64>(Run.MinZ) + Run.MaxZ;
		const int64 CenterDistance = FMath::Abs(RunCenterTimesTwo - LayoutCenterTimesTwo);
		if (SelectedRunIndex == INDEX_NONE
			|| Overlap > BestOverlap
			|| (Overlap == BestOverlap && CenterDistance < BestCenterDistance)
			|| (Overlap == BestOverlap && CenterDistance == BestCenterDistance
				&& Run.MinZ < CenterProfile.Runs[SelectedRunIndex].MinZ))
		{
			SelectedRunIndex = RunIndex;
			BestOverlap = Overlap;
			BestCenterDistance = CenterDistance;
		}
	}
	if (SelectedRunIndex == INDEX_NONE)
	{
		OutFailureReason = FString::Printf(
			TEXT("Center-column environment classification found no air gap overlapping layout envelope [%d,%lld] at (%d,%d)."),
			LayoutEnvelopeMinZ,
			LayoutEnvelopeMaxZ,
			CenterProfile.BlockXY.X,
			CenterProfile.BlockXY.Y);
		return false;
	}
	if (SelectedRunIndex <= 0 || CenterProfile.Runs[SelectedRunIndex - 1].bIsEmpty)
	{
		OutFailureReason = FString::Printf(
			TEXT("Center-column environment air gap [%d,%d] lacks terrain below at (%d,%d)."),
			CenterProfile.Runs[SelectedRunIndex].MinZ,
			CenterProfile.Runs[SelectedRunIndex].MaxZ,
			CenterProfile.BlockXY.X,
			CenterProfile.BlockXY.Y);
		return false;
	}
	OutSelectedAirRun = CenterProfile.Runs[SelectedRunIndex];
	bOutUnderground = SelectedRunIndex + 1 < CenterProfile.Runs.Num()
		&& !CenterProfile.Runs[SelectedRunIndex + 1].bIsEmpty;
	return true;
}

bool FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
	const TArray<FLayoutTerrainColumnProfile>& Profiles,
	const FIntVector& SiteCenterBlockWorldPos,
	const int32 LayoutEnvelopeHeightInBlocks,
	TMap<FIntPoint, FLayoutTerrainColumnRun>& OutSelectedRuns,
	bool& bOutUnderground,
	FString& OutFailureReason)
{
	OutSelectedRuns.Reset();
	bOutUnderground = false;
	OutFailureReason.Reset();
	if (Profiles.IsEmpty() || LayoutEnvelopeHeightInBlocks <= 0)
	{
		OutFailureReason = TEXT("Selected terrain component requires column profiles and a positive layout-envelope height.");
		return false;
	}

	TMap<FIntPoint, const FLayoutTerrainColumnProfile*> ProfileByXY;
	for (const FLayoutTerrainColumnProfile& Profile : Profiles)
	{
		if (Profile.Runs.IsEmpty() || ProfileByXY.Contains(Profile.BlockXY))
		{
			OutFailureReason = FString::Printf(
				TEXT("Selected terrain component requires one non-empty profile for column (%d,%d)."),
				Profile.BlockXY.X,
				Profile.BlockXY.Y);
			return false;
		}
		int32 PreviousMaxZ = MIN_int32;
		for (const FLayoutTerrainColumnRun& Run : Profile.Runs)
		{
			if (Run.MinZ > Run.MaxZ || (PreviousMaxZ != MIN_int32 && Run.MinZ != PreviousMaxZ + 1))
			{
				OutFailureReason = FString::Printf(
					TEXT("Selected terrain component found incomplete runs for column (%d,%d)."),
					Profile.BlockXY.X,
					Profile.BlockXY.Y);
				return false;
			}
			PreviousMaxZ = Run.MaxZ;
		}
		ProfileByXY.Add(Profile.BlockXY, &Profile);
	}

	const FIntPoint SiteXY(SiteCenterBlockWorldPos.X, SiteCenterBlockWorldPos.Y);
	const FLayoutTerrainColumnProfile* const* AnchorProfilePtr = ProfileByXY.Find(SiteXY);
	if (AnchorProfilePtr == nullptr || *AnchorProfilePtr == nullptr)
	{
		OutFailureReason = FString::Printf(
			TEXT("Selected terrain component lacks anchor column (%d,%d)."), SiteXY.X, SiteXY.Y);
		return false;
	}
	const FLayoutTerrainColumnProfile& AnchorProfile = **AnchorProfilePtr;
	FLayoutTerrainColumnRun ClassifiedAnchorRun;
	if (!TryClassifyCenterColumnEnvironment(
			AnchorProfile,
			SiteCenterBlockWorldPos.Z,
			LayoutEnvelopeHeightInBlocks,
			ClassifiedAnchorRun,
			bOutUnderground,
			OutFailureReason))
	{
		return false;
	}
	const FLayoutTerrainColumnRun* const AnchorRun = AnchorProfile.Runs.FindByPredicate(
		[&ClassifiedAnchorRun](const FLayoutTerrainColumnRun& Run)
		{
			return Run.bIsEmpty == ClassifiedAnchorRun.bIsEmpty
				&& Run.MinZ == ClassifiedAnchorRun.MinZ
				&& Run.MaxZ == ClassifiedAnchorRun.MaxZ;
		});
	if (AnchorRun == nullptr)
	{
		OutFailureReason = TEXT("Selected terrain component could not recover its classified center air run.");
		return false;
	}

	OutSelectedRuns.Add(SiteXY, *AnchorRun);
	TArray<FIntPoint> Queue = {SiteXY};
	for (int32 QueueIndex = 0; QueueIndex < Queue.Num(); ++QueueIndex)
	{
		const FIntPoint CurrentXY = Queue[QueueIndex];
		const FLayoutTerrainColumnRun* const CurrentRunPtr = OutSelectedRuns.Find(CurrentXY);
		if (CurrentRunPtr == nullptr)
		{
			continue;
		}
		// Adding a neighbor can reallocate the map; keep this run independent of its storage.
		const FLayoutTerrainColumnRun CurrentRun = *CurrentRunPtr;
		const FIntPoint NeighborOffsets[] = {
			FIntPoint(0, -1), FIntPoint(-1, 0), FIntPoint(1, 0), FIntPoint(0, 1)
		};
		for (const FIntPoint& Offset : NeighborOffsets)
		{
			const FIntPoint NeighborXY = CurrentXY + Offset;
			if (OutSelectedRuns.Contains(NeighborXY))
			{
				continue;
			}
			const FLayoutTerrainColumnProfile* const* NeighborProfilePtr = ProfileByXY.Find(NeighborXY);
			if (NeighborProfilePtr == nullptr || *NeighborProfilePtr == nullptr)
			{
				continue;
			}
			const FLayoutTerrainColumnRun* BestRun = nullptr;
			int32 BestOverlap = 0;
			int32 BestFloorDelta = MAX_int32;
			for (const FLayoutTerrainColumnRun& Candidate : (*NeighborProfilePtr)->Runs)
			{
				if (!Candidate.bIsEmpty)
				{
					continue;
				}
				const int32 Overlap = FMath::Min(CurrentRun.MaxZ, Candidate.MaxZ)
					- FMath::Max(CurrentRun.MinZ, Candidate.MinZ) + 1;
				if (Overlap <= 0)
				{
					continue;
				}
				const int32 FloorDelta = FMath::Abs(Candidate.MinZ - AnchorRun->MinZ);
				if (BestRun == nullptr
					|| Overlap > BestOverlap
					|| (Overlap == BestOverlap && FloorDelta < BestFloorDelta)
					|| (Overlap == BestOverlap && FloorDelta == BestFloorDelta && Candidate.MinZ < BestRun->MinZ))
				{
					BestRun = &Candidate;
					BestOverlap = Overlap;
					BestFloorDelta = FloorDelta;
				}
			}
			if (BestRun != nullptr)
			{
				OutSelectedRuns.Add(NeighborXY, *BestRun);
				Queue.Add(NeighborXY);
			}
		}
	}

	return true;
}

bool FLayoutTerrainSampling::TryPrepareSelectedSiteTerrainFromMaterialSamples(
	const FIntVector& SampleFootprintMinBlockWorldPos,
	const int32 SampleFootprintWidthBlocks,
	const int32 SampleFootprintHeightBlocks,
	const int32 SampleMinZ,
	const int32 SampleMaxZ,
	const FIntVector& SiteCenterBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 LayoutEnvelopeHeightInBlocks,
	const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
	const int32 EmptyMaterialValue,
	const TConstArrayView<int> Materials,
	const TConstArrayView<int> BiomeIndices,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (SharedCellSizeInBlocks.X <= 0 || SharedCellSizeInBlocks.Y <= 0 || SharedCellSizeInBlocks.Z <= 0)
	{
		OutFailureReason = TEXT("Selected-site terrain preparation requires positive shared cell dimensions.");
		return false;
	}
	TArray<uint8> EmptyStates;
	EmptyStates.Reserve(Materials.Num());
	for (const int Material : Materials)
	{
		EmptyStates.Add(Material == EmptyMaterialValue ? 1 : 0);
	}

	TArray<FLayoutTerrainColumnProfile> ColumnProfiles;
	TMap<FIntPoint, FLayoutTerrainColumnRun> SelectedRuns;
	bool bUnderground = false;
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_TerrainComponentBuild, STAT_PorismLayout_TerrainComponentBuild);
		if (!TryBuildTerrainColumnProfilesFromEmptySamples(
				SampleFootprintMinBlockWorldPos,
				SampleFootprintWidthBlocks,
				SampleFootprintHeightBlocks,
				SampleMinZ,
				SampleMaxZ,
				EmptyStates,
				ColumnProfiles,
				OutFailureReason))
		{
			return false;
		}

		if (!TrySelectConnectedEmptyComponent(
				ColumnProfiles,
				SiteCenterBlockWorldPos,
				LayoutEnvelopeHeightInBlocks,
				SelectedRuns,
				bUnderground,
				OutFailureReason))
		{
			return false;
		}
	}
	INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainSelectedColumns, static_cast<uint32>(SelectedRuns.Num()));
	TMap<FIntPoint, const FLayoutTerrainColumnProfile*> ColumnProfileByXY;
	for (const FLayoutTerrainColumnProfile& Profile : ColumnProfiles)
	{
		ColumnProfileByXY.Add(Profile.BlockXY, &Profile);
	}

	InOutArtifact.PerimeterSurfaceSamples.Reset(ColumnProfiles.Num());
	const int32 TargetSurfaceZ = SiteCenterBlockWorldPos.Z - 1;
	const int32 LayoutTopZ = SiteCenterBlockWorldPos.Z + LayoutEnvelopeHeightInBlocks - 1;
	for (const FLayoutTerrainColumnProfile& Profile : ColumnProfiles)
	{
		const FLayoutTerrainColumnRun* RelevantSolidRun = nullptr;
		if (const FLayoutTerrainColumnRun* const SelectedRun = SelectedRuns.Find(Profile.BlockXY))
		{
			if (SelectedRun->MinZ > SampleMinZ)
			{
				for (const FLayoutTerrainColumnRun& Run : Profile.Runs)
				{
					if (!Run.bIsEmpty && Run.MaxZ == SelectedRun->MinZ - 1)
					{
						RelevantSolidRun = &Run;
						break;
					}
				}
			}
		}
		if (RelevantSolidRun == nullptr)
		{
			for (const FLayoutTerrainColumnRun& Run : Profile.Runs)
			{
				if (!Run.bIsEmpty && Run.MinZ <= TargetSurfaceZ && Run.MaxZ >= TargetSurfaceZ)
				{
					RelevantSolidRun = &Run;
					break;
				}
			}
		}
		if (RelevantSolidRun == nullptr)
		{
			for (const FLayoutTerrainColumnRun& Run : Profile.Runs)
			{
				if (!Run.bIsEmpty && Run.MinZ <= LayoutTopZ && Run.MaxZ >= SiteCenterBlockWorldPos.Z)
				{
					RelevantSolidRun = &Run;
					break;
				}
			}
		}
		if (RelevantSolidRun == nullptr)
		{
			for (int32 RunIndex = Profile.Runs.Num() - 1; RunIndex >= 0; --RunIndex)
			{
				const FLayoutTerrainColumnRun& Run = Profile.Runs[RunIndex];
				if (!Run.bIsEmpty && Run.MaxZ < SiteCenterBlockWorldPos.Z)
				{
					RelevantSolidRun = &Run;
					break;
				}
			}
		}
		FLayoutTerrainSurfaceSample& Sample = InOutArtifact.PerimeterSurfaceSamples.AddDefaulted_GetRef();
		Sample.BlockXY = Profile.BlockXY;
		if (RelevantSolidRun == nullptr)
		{
			continue;
		}
		Sample.bIsValid = true;
		Sample.SurfaceBlockWorldPos = FIntVector(Profile.BlockXY.X, Profile.BlockXY.Y, RelevantSolidRun->MaxZ);
		Sample.bHasSolidRunBounds = true;
		Sample.SolidRunMinZ = RelevantSolidRun->MinZ;
		Sample.SolidRunMaxZ = RelevantSolidRun->MaxZ;
		const int32 SampleIndex = (RelevantSolidRun->MaxZ - SampleMinZ)
			* SampleFootprintWidthBlocks * SampleFootprintHeightBlocks
			+ (Profile.BlockXY.Y - SampleFootprintMinBlockWorldPos.Y) * SampleFootprintWidthBlocks
			+ Profile.BlockXY.X - SampleFootprintMinBlockWorldPos.X;
		if (Materials.IsValidIndex(SampleIndex)
			&& Materials[SampleIndex] != EmptyMaterialValue
			&& Materials[SampleIndex] != DefaultMaterial)
		{
			Sample.SurfaceMaterialIndex = Materials[SampleIndex];
		}
	}

	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_TerrainFitEvidence, STAT_PorismLayout_TerrainFitEvidence);
		for (FLayoutTerrainPlacementCellEvidence& PlacementCell : InOutArtifact.TerrainPlacementCells)
		{
			// Selected final occupancy replaces provisional top-down/site-flat evidence.
			PlacementCell.bHasFoundationFillEvidence = false;
			PlacementCell.bHasExcavationEvidence = false;
			PlacementCell.bHasClearanceEvidence = false;
			PlacementCell.bHasRampTransitionEvidence = false;
			PlacementCell.bHasBridgeSupportEvidence = false;
			PlacementCell.bHasLocalOverlapZ = false;
			PlacementCell.RequiredFoundationDepth = 0;
			PlacementCell.FoundationMaterial = 0;
			PlacementCell.OverlapMinLocalZ = FLayoutLocalBlockCoord8();
			PlacementCell.OverlapMaxLocalZ = FLayoutLocalBlockCoord8();
			PlacementCell.EntryTraversability = ELayoutEntryTraversabilityVerdict::None;
			const FIntVector CellMin = InOutArtifact.FootprintMinBlockWorldPos + FIntVector(
				PlacementCell.Cell.X * SharedCellSizeInBlocks.X,
				PlacementCell.Cell.Y * SharedCellSizeInBlocks.Y,
				PlacementCell.Cell.Z * SharedCellSizeInBlocks.Z);
			int32 MinSolidLocalZ = MAX_int32;
			int32 MaxSolidLocalZ = MIN_int32;
			for (int32 LocalZ = 0; LocalZ < SharedCellSizeInBlocks.Z; ++LocalZ)
			{
				for (int32 LocalY = 0; LocalY < SharedCellSizeInBlocks.Y; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < SharedCellSizeInBlocks.X; ++LocalX)
					{
						const FIntVector BlockWorldPos = CellMin + FIntVector(LocalX, LocalY, LocalZ);
						const int32 SampleX = BlockWorldPos.X - SampleFootprintMinBlockWorldPos.X;
						const int32 SampleY = BlockWorldPos.Y - SampleFootprintMinBlockWorldPos.Y;
						const int32 SampleZ = BlockWorldPos.Z - SampleMinZ;
						if (SampleX < 0 || SampleX >= SampleFootprintWidthBlocks
							|| SampleY < 0 || SampleY >= SampleFootprintHeightBlocks
							|| SampleZ < 0 || BlockWorldPos.Z > SampleMaxZ)
						{
							OutFailureReason = FString::Printf(
								TEXT("Selected-site terrain packet does not cover planned cell %s at block %s."),
								*PlacementCell.Cell.ToString(),
								*BlockWorldPos.ToString());
							return false;
						}
						const int32 SampleIndex = SampleZ * SampleFootprintWidthBlocks * SampleFootprintHeightBlocks
							+ SampleY * SampleFootprintWidthBlocks + SampleX;
						if (!Materials.IsValidIndex(SampleIndex))
						{
							OutFailureReason = TEXT("Selected-site terrain material packet was truncated inside planned occupancy.");
							return false;
						}
						if (Materials[SampleIndex] != EmptyMaterialValue)
						{
							MinSolidLocalZ = FMath::Min(MinSolidLocalZ, LocalZ);
							MaxSolidLocalZ = FMath::Max(MaxSolidLocalZ, LocalZ);
						}
					}
				}
			}
			if (MinSolidLocalZ != MAX_int32)
			{
				PlacementCell.bHasExcavationEvidence = true;
				PlacementCell.bHasLocalOverlapZ = true;
				PlacementCell.bHasClearanceEvidence = false;
				PlacementCell.OverlapMinLocalZ = FLayoutLocalBlockCoord8(MinSolidLocalZ);
				PlacementCell.OverlapMaxLocalZ = FLayoutLocalBlockCoord8(MaxSolidLocalZ);
				continue;
			}

			if (!TerrainTransitionPolicy.bAllowFoundationFill || TerrainTransitionPolicy.MaxFoundationDepth <= 0)
			{
				continue;
			}

			int32 RequiredFoundationDepth = 0;
			int32 FoundationMaterial = 0;
			bool bFoundFoundationMaterial = false;
			for (int32 LocalY = 0; LocalY < SharedCellSizeInBlocks.Y; ++LocalY)
			{
				for (int32 LocalX = 0; LocalX < SharedCellSizeInBlocks.X; ++LocalX)
				{
					int32 SupportingZ = INDEX_NONE;
					const FIntPoint BlockXY(CellMin.X + LocalX, CellMin.Y + LocalY);
					const FLayoutTerrainColumnProfile* const Profile = ColumnProfileByXY.FindRef(BlockXY);
					if (Profile != nullptr)
					{
						for (int32 RunIndex = Profile->Runs.Num() - 1; RunIndex >= 0; --RunIndex)
						{
							const FLayoutTerrainColumnRun& Run = Profile->Runs[RunIndex];
							if (!Run.bIsEmpty && Run.MaxZ < CellMin.Z)
							{
								SupportingZ = Run.MaxZ;
								break;
							}
						}
					}
					if (SupportingZ != INDEX_NONE && !bFoundFoundationMaterial)
					{
						const int32 SampleX = BlockXY.X - SampleFootprintMinBlockWorldPos.X;
						const int32 SampleY = BlockXY.Y - SampleFootprintMinBlockWorldPos.Y;
						const int32 SampleZ = SupportingZ - SampleMinZ;
						const int32 SampleIndex = SampleZ * SampleFootprintWidthBlocks * SampleFootprintHeightBlocks
							+ SampleY * SampleFootprintWidthBlocks + SampleX;
						if (Materials.IsValidIndex(SampleIndex)
							&& Materials[SampleIndex] != EmptyMaterialValue
							&& Materials[SampleIndex] != DefaultMaterial)
						{
							FoundationMaterial = Materials[SampleIndex];
							bFoundFoundationMaterial = true;
						}
					}
					const int32 ColumnDepth = SupportingZ == INDEX_NONE
						? TerrainTransitionPolicy.MaxFoundationDepth
						: FMath::Min(
							TerrainTransitionPolicy.MaxFoundationDepth,
							FMath::Max(0, CellMin.Z - SupportingZ - 1));
					RequiredFoundationDepth = FMath::Max(RequiredFoundationDepth, ColumnDepth);
				}
			}
			if (RequiredFoundationDepth > 0)
			{
				PlacementCell.bHasFoundationFillEvidence = true;
				PlacementCell.bHasClearanceEvidence = false;
				PlacementCell.RequiredFoundationDepth = RequiredFoundationDepth;
				PlacementCell.FoundationMaterial = FoundationMaterial;
			}
		}
	}

	InOutArtifact.PocketVoidIntervals.Reset();
	TArray<FIntPoint> SelectedColumns;
	SelectedRuns.GenerateKeyArray(SelectedColumns);
	SelectedColumns.Sort([](const FIntPoint& Left, const FIntPoint& Right)
	{
		return Left.Y == Right.Y ? Left.X < Right.X : Left.Y < Right.Y;
	});
	InOutArtifact.PocketVoidIntervals.Reserve(SelectedColumns.Num());
	for (const FIntPoint& BlockXY : SelectedColumns)
	{
		const FLayoutTerrainColumnRun* const Run = SelectedRuns.Find(BlockXY);
		if (Run == nullptr)
		{
			continue;
		}
		FLayoutFrozenTerrainVoidIntervalSample& Interval =
			InOutArtifact.PocketVoidIntervals.AddDefaulted_GetRef();
		Interval.BlockXY = BlockXY;
		Interval.MinZ = Run->MinZ;
		Interval.MaxZ = Run->MaxZ;
		Interval.bHasVoidEvidence = true;
		const int32 FloorSampleIndex = (Interval.MinZ - 1 - SampleMinZ)
			* SampleFootprintWidthBlocks * SampleFootprintHeightBlocks
			+ (BlockXY.Y - SampleFootprintMinBlockWorldPos.Y) * SampleFootprintWidthBlocks
			+ BlockXY.X - SampleFootprintMinBlockWorldPos.X;
		if (Materials.IsValidIndex(FloorSampleIndex)
			&& Materials[FloorSampleIndex] != EmptyMaterialValue
			&& Materials[FloorSampleIndex] != DefaultMaterial)
		{
			Interval.FloorMaterialIndex = Materials[FloorSampleIndex];
			Interval.bHasFloorMaterialIndex = true;
		}
		if (BiomeIndices.IsValidIndex(FloorSampleIndex))
		{
			Interval.FloorBiomeIndex = BiomeIndices[FloorSampleIndex];
			Interval.bHasFloorBiomeIndex = true;
		}
	}

	InOutArtifact.bHasPocketVoidIntervalEvidence =
		bUnderground && !InOutArtifact.PocketVoidIntervals.IsEmpty();
	InOutArtifact.bHasRelativeEnvironmentClassification = true;
	InOutArtifact.bIsClassifiedUnderground = bUnderground;
	return true;
}

bool FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromVoidIntervals(
	const FIntVector& AnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 TerrainSampleGridSpacing,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const TArray<FLayoutFrozenTerrainVoidIntervalSample>& VoidIntervals,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FString& OutFailureReason,
	const int32 MinimumSampledZ)
{
	TMap<FIntPoint, const FLayoutFrozenTerrainVoidIntervalSample*> IntervalByXY;
	for (const FLayoutFrozenTerrainVoidIntervalSample& Interval : VoidIntervals)
	{
		if (Interval.bHasVoidEvidence)
		{
			IntervalByXY.FindOrAdd(Interval.BlockXY) = &Interval;
		}
	}

	TArray<FLayoutReservationPocketSample> PocketSamples;
	TSet<FIntPoint> AddedColumns;
	for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
	{
		const FIntPoint SampleXY = ComputeSteppedTerrainCenterSampleBlockXY(
			AnchorBlockWorldPos, SharedCellSizeInBlocks, PlannedCell.Cell);
		const FLayoutFrozenTerrainVoidIntervalSample* const Interval = IntervalByXY.FindRef(SampleXY);
		if (Interval == nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Selected-component stepped support is missing interval evidence for planned cell %s at block XY (%d,%d)."), *PlannedCell.Cell.ToString(), SampleXY.X, SampleXY.Y);
			return false;
		}
		if (Interval->MinZ <= MinimumSampledZ)
		{
			OutFailureReason = FString::Printf(
				TEXT("Selected-component stepped support has no observed floor at block XY (%d,%d): air reaches sampled lower bound Z=%d."),
				SampleXY.X, SampleXY.Y, MinimumSampledZ);
			return false;
		}
		const FIntPoint SampleGridXY(PlannedCell.Cell.X, PlannedCell.Cell.Y);
		if (AddedColumns.Contains(SampleGridXY))
		{
			continue;
		}
		FLayoutReservationPocketSample& Sample = PocketSamples.AddDefaulted_GetRef();
		Sample.SampleGridXY = SampleGridXY;
		Sample.BlockXY = SampleXY;
		Sample.bHasSurfaceZ = true;
		Sample.SurfaceZBlockWorld = Interval->MinZ - 1;
		AddedColumns.Add(SampleGridXY);
	}
	return TryBuildSteppedTerrainSupportMapFromPocketSamples(
		AnchorBlockWorldPos, SharedCellSizeInBlocks, TerrainSampleGridSpacing, PlannedCells, PocketSamples, OutSupportMap, OutFailureReason);
}

namespace
{
	/** Marks shared perimeter cells whose immediate cardinal or diagonal exterior terrain does not meet the unadjusted layout base. */
	void ApplySharedPerimeterRampEvidence(
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntPoint& FootprintSizeInCells,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		const TConstArrayView<FLayoutFrozenTerrainVoidIntervalSample> SelectedIntervals,
		const TConstArrayView<FLayoutTerrainSurfaceSample> PerimeterSurfaceSamples,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact)
	{
		if (!TerrainTransitionPolicy.bAllowPerimeterRampTransition
			|| TerrainTransitionPolicy.MaxFoundationDepth <= 0)
		{
			return;
		}
		TMap<FIntPoint, const FLayoutFrozenTerrainVoidIntervalSample*> IntervalByXY;
		for (const FLayoutFrozenTerrainVoidIntervalSample& Interval : SelectedIntervals)
		{
			if (Interval.bHasVoidEvidence)
			{
				IntervalByXY.Add(Interval.BlockXY, &Interval);
			}
		}
		TMap<FIntPoint, const FLayoutTerrainSurfaceSample*> SurfaceByXY;
		for (const FLayoutTerrainSurfaceSample& Sample : PerimeterSurfaceSamples)
		{
			SurfaceByXY.Add(Sample.BlockXY, &Sample);
		}
		for (FLayoutTerrainPlacementCellEvidence& Cell : InOutArtifact.TerrainPlacementCells)
		{
			const bool bMinX = Cell.Cell.X == 0;
			const bool bMaxX = Cell.Cell.X == FootprintSizeInCells.X - 1;
			const bool bMinY = Cell.Cell.Y == 0;
			const bool bMaxY = Cell.Cell.Y == FootprintSizeInCells.Y - 1;
			if (!bMinX && !bMaxX && !bMinY && !bMaxY)
			{
				continue;
			}
			const int32 TargetSurfaceZ = FootprintMinBlockWorldPos.Z + Cell.VerticalShiftBlocks - 1;
			const FIntVector CellMin = FootprintMinBlockWorldPos + FIntVector(
				Cell.Cell.X * SharedCellSizeInBlocks.X,
				Cell.Cell.Y * SharedCellSizeInBlocks.Y,
				Cell.VerticalShiftBlocks);
			const auto FaceNeedsRamp = [&](const FIntPoint Direction)
			{
				const int32 TangentCount = Direction.X != 0
					? SharedCellSizeInBlocks.Y
					: SharedCellSizeInBlocks.X;
				for (int32 Tangent = 0; Tangent < TangentCount; ++Tangent)
				{
					const FIntPoint ExteriorXY(
						Direction.X < 0 ? CellMin.X - 1 : Direction.X > 0 ? CellMin.X + SharedCellSizeInBlocks.X : CellMin.X + Tangent,
						Direction.Y < 0 ? CellMin.Y - 1 : Direction.Y > 0 ? CellMin.Y + SharedCellSizeInBlocks.Y : CellMin.Y + Tangent);
					const FLayoutTerrainSurfaceSample* const SurfaceSample = SurfaceByXY.FindRef(ExteriorXY);
					const FLayoutFrozenTerrainVoidIntervalSample* const Interval = IntervalByXY.FindRef(ExteriorXY);
					if (SurfaceSample != nullptr && !SurfaceSample->bIsValid)
					{
						continue;
					}
					if (SurfaceSample == nullptr && Interval == nullptr)
					{
						continue;
					}
					const int32 ExistingSurfaceZ = SurfaceSample != nullptr
						? SurfaceSample->SurfaceBlockWorldPos.Z
						: Interval->MinZ - 1;
					const int32 SurfaceDelta = FMath::Abs(ExistingSurfaceZ - TargetSurfaceZ);
					if (SurfaceDelta > 0 && SurfaceDelta <= TerrainTransitionPolicy.MaxFoundationDepth)
					{
						return true;
					}
				}
				return false;
			};
			const auto CornerNeedsRamp = [&](const FIntPoint Direction)
			{
				const FIntPoint ExteriorXY(
					Direction.X < 0 ? CellMin.X - 1 : CellMin.X + SharedCellSizeInBlocks.X,
					Direction.Y < 0 ? CellMin.Y - 1 : CellMin.Y + SharedCellSizeInBlocks.Y);
				const FLayoutTerrainSurfaceSample* const SurfaceSample = SurfaceByXY.FindRef(ExteriorXY);
				const FLayoutFrozenTerrainVoidIntervalSample* const Interval = IntervalByXY.FindRef(ExteriorXY);
				if ((SurfaceSample != nullptr && !SurfaceSample->bIsValid)
					|| (SurfaceSample == nullptr && Interval == nullptr))
				{
					return false;
				}
				const int32 ExistingSurfaceZ = SurfaceSample != nullptr
					? SurfaceSample->SurfaceBlockWorldPos.Z
					: Interval->MinZ - 1;
				const int32 SurfaceDelta = FMath::Abs(ExistingSurfaceZ - TargetSurfaceZ);
				return SurfaceDelta > 0 && SurfaceDelta <= TerrainTransitionPolicy.MaxFoundationDepth;
			};
			Cell.bHasRampTransitionEvidence =
				(bMinX && FaceNeedsRamp(FIntPoint(-1, 0)))
				|| (bMaxX && FaceNeedsRamp(FIntPoint(1, 0)))
				|| (bMinY && FaceNeedsRamp(FIntPoint(0, -1)))
				|| (bMaxY && FaceNeedsRamp(FIntPoint(0, 1)))
				|| (bMinX && bMinY && CornerNeedsRamp(FIntPoint(-1, -1)))
				|| (bMinX && bMaxY && CornerNeedsRamp(FIntPoint(-1, 1)))
				|| (bMaxX && bMinY && CornerNeedsRamp(FIntPoint(1, -1)))
				|| (bMaxX && bMaxY && CornerNeedsRamp(FIntPoint(1, 1)));
		}
	}

	/** Applies only enclosed-component lateral Entry proof to a canonical selected-component artifact. */
	bool ApplyUndergroundSelectedComponentEntryEvidence(
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntPoint& FootprintSizeInCells,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		const FLayoutSteppedTerrainSupportMap& SupportMap,
		const TConstArrayView<FLayoutFrozenTerrainVoidIntervalSample> SelectedIntervals,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
		FString& OutFailureReason)
	{
		TMap<FIntPoint, const FLayoutFrozenTerrainVoidIntervalSample*> IntervalByXY;
		for (const FLayoutFrozenTerrainVoidIntervalSample& Interval : SelectedIntervals)
		{
			if (!Interval.bHasVoidEvidence || Interval.MinZ > Interval.MaxZ)
			{
				OutFailureReason = TEXT("Underground Entry evidence contains an invalid selected-component interval.");
				return false;
			}
			if (IntervalByXY.Contains(Interval.BlockXY))
			{
				OutFailureReason = FString::Printf(
					TEXT("Underground Entry evidence contains duplicate interval column (%d,%d)."),
					Interval.BlockXY.X,
					Interval.BlockXY.Y);
				return false;
			}
			IntervalByXY.Add(Interval.BlockXY, &Interval);
		}

		TMap<FIntVector, int32> RoundedBaseZByCell;
		for (const FLayoutSteppedTerrainSupportSample& SupportSample : SupportMap.SupportSamples)
		{
			if (SharedCellSizeInBlocks.Z <= 0)
			{
				OutFailureReason = TEXT("Underground Entry evidence requires a positive shared cell height.");
				return false;
			}
			RoundedBaseZByCell.Add(
				SupportSample.LocalCell,
				FMath::DivideAndRoundUp(SupportSample.SupportSurfaceZ + 1, SharedCellSizeInBlocks.Z)
					* SharedCellSizeInBlocks.Z);
		}

		for (FLayoutTerrainPlacementCellEvidence& PlacementCell : InOutArtifact.TerrainPlacementCells)
		{
			const bool bBoundary = PlacementCell.Cell.X == 0 || PlacementCell.Cell.X == FootprintSizeInCells.X - 1
				|| PlacementCell.Cell.Y == 0 || PlacementCell.Cell.Y == FootprintSizeInCells.Y - 1;
			const bool bCorner = (PlacementCell.Cell.X == 0 || PlacementCell.Cell.X == FootprintSizeInCells.X - 1)
				&& (PlacementCell.Cell.Y == 0 || PlacementCell.Cell.Y == FootprintSizeInCells.Y - 1);
			if (!bBoundary || bCorner)
			{
				continue;
			}

			PlacementCell.EntryTraversability = ELayoutEntryTraversabilityVerdict::CliffEdge;
			const int32* const RoundedBaseZ = RoundedBaseZByCell.Find(PlacementCell.Cell);
			if (RoundedBaseZ == nullptr)
			{
				continue;
			}
			FIntPoint ExteriorXY = ComputeSteppedTerrainCenterSampleBlockXY(
				FootprintMinBlockWorldPos,
				SharedCellSizeInBlocks,
				PlacementCell.Cell);
			if (PlacementCell.Cell.X == 0) { ExteriorXY.X -= SharedCellSizeInBlocks.X; }
			else if (PlacementCell.Cell.X == FootprintSizeInCells.X - 1) { ExteriorXY.X += SharedCellSizeInBlocks.X; }
			else if (PlacementCell.Cell.Y == 0) { ExteriorXY.Y -= SharedCellSizeInBlocks.Y; }
			else { ExteriorXY.Y += SharedCellSizeInBlocks.Y; }

			const FLayoutFrozenTerrainVoidIntervalSample* const ExteriorInterval = IntervalByXY.FindRef(ExteriorXY);
			const int32 EntryMaxZ = *RoundedBaseZ + SharedCellSizeInBlocks.Z - 1;
			if (ExteriorInterval != nullptr
				&& ExteriorInterval->MinZ <= EntryMaxZ
				&& ExteriorInterval->MaxZ >= *RoundedBaseZ)
			{
				PlacementCell.EntryTraversability = ELayoutEntryTraversabilityVerdict::Walkable;
			}
			else if (TerrainTransitionPolicy.bAllowPerimeterRampTransition)
			{
				PlacementCell.EntryTraversability = ELayoutEntryTraversabilityVerdict::RampNeeded;
			}
		}
		return true;
	}
}

void FLayoutTerrainSampling::ApplySelectedComponentPerimeterRampEvidence(
	const FIntVector& FootprintMinBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const FIntPoint& FootprintSizeInCells,
	const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact)
{
	ApplySharedPerimeterRampEvidence(
		FootprintMinBlockWorldPos,
		SharedCellSizeInBlocks,
		FootprintSizeInCells,
		TerrainTransitionPolicy,
		InOutArtifact.PocketVoidIntervals,
		InOutArtifact.PerimeterSurfaceSamples,
		InOutArtifact);
}

/** Augments one selected-component artifact with shared stepped support and environment-specific Entry evidence. */
bool FLayoutTerrainSampling::TryAugmentSelectedComponentWithSteppedTerrainEvidence(
	const bool bUnderground,
	const FIntVector& FootprintMinBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const FIntPoint& FootprintSizeInCells,
	const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
	FString& OutFailureReason)
{
	TArray<FLayoutPlannedCell> SupportCells;
	SupportCells.Reserve(FootprintSizeInCells.X * FootprintSizeInCells.Y);
	for (int32 CellY = 0; CellY < FootprintSizeInCells.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FootprintSizeInCells.X; ++CellX)
		{
			const bool bBoundary = CellX == 0 || CellY == 0
				|| CellX == FootprintSizeInCells.X - 1 || CellY == FootprintSizeInCells.Y - 1;
			SupportCells.Add({FIntVector(CellX, CellY, 0),
				bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior});
		}
	}

	if (!FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromVoidIntervals(
			FootprintMinBlockWorldPos,
			SharedCellSizeInBlocks,
			SharedCellSizeInBlocks.X,
			SupportCells,
			InOutArtifact.PocketVoidIntervals,
			OutSupportMap,
			OutFailureReason,
			InOutArtifact.bHasFiniteSearchBounds && InOutArtifact.SearchDepthBlocks > 0
				? InOutArtifact.SearchStartZBlockWorld - InOutArtifact.SearchDepthBlocks + 1
				: MIN_int32))
	{
		return false;
	}

	InOutArtifact.bHasSteppedSupportEvidence = !OutSupportMap.SupportSamples.IsEmpty();
	InOutArtifact.bHasSampledColumnEvidence = !OutSupportMap.SupportSamples.IsEmpty();
	InOutArtifact.SteppedSupportSamples = OutSupportMap.SupportSamples;
	InOutArtifact.SteppedAdjacencySteps = OutSupportMap.AdjacencySteps;
	InOutArtifact.MaxObservedNeighborHeightDelta = OutSupportMap.MaximumObservedNeighborHeightDelta;
	InOutArtifact.MaxObservedSnappedLevelDelta = OutSupportMap.MaximumObservedSnappedLevelDelta;
	InOutArtifact.bHasFootprintClassificationEvidence = !OutSupportMap.SupportSamples.IsEmpty();
	InOutArtifact.FootprintClassification.CellClassifications.Reset(OutSupportMap.SupportSamples.Num());
	InOutArtifact.SurfaceSamples.Reset(OutSupportMap.SupportSamples.Num());
	InOutArtifact.AnchorResult = FLayoutTerrainAnchorResult();
	InOutArtifact.AnchorResult.bIsValid = !OutSupportMap.SupportSamples.IsEmpty();
	InOutArtifact.AnchorResult.AnchorBlockWorldPos = FootprintMinBlockWorldPos;
	InOutArtifact.AnchorResult.MaximumObservedNeighborHeightDelta = OutSupportMap.MaximumObservedNeighborHeightDelta;
	int32 BaseSnappedSupportFloorZ = MAX_int32;
	TMap<FIntPoint, int32> SnappedFloorByXY;
	for (const FLayoutSteppedTerrainSupportSample& Sample : OutSupportMap.SupportSamples)
	{
		const FIntPoint CellXY(Sample.LocalCell.X, Sample.LocalCell.Y);
		SnappedFloorByXY.Add(CellXY, Sample.SnappedSupportFloorZ);
		BaseSnappedSupportFloorZ = FMath::Min(BaseSnappedSupportFloorZ, Sample.SnappedSupportFloorZ);
		FLayoutTerrainCellClassification& Classification =
			InOutArtifact.FootprintClassification.CellClassifications.AddDefaulted_GetRef();
		Classification.BlockXY = FIntPoint(
			FootprintMinBlockWorldPos.X + Sample.LocalCell.X * SharedCellSizeInBlocks.X,
			FootprintMinBlockWorldPos.Y + Sample.LocalCell.Y * SharedCellSizeInBlocks.Y);
		Classification.SurfaceZ = Sample.SupportSurfaceZ;
		FLayoutTerrainSurfaceSample& SurfaceSample = InOutArtifact.SurfaceSamples.AddDefaulted_GetRef();
		SurfaceSample.bIsValid = true;
		SurfaceSample.BlockXY = Classification.BlockXY;
		SurfaceSample.SurfaceBlockWorldPos = FIntVector(
			Classification.BlockXY.X, Classification.BlockXY.Y, Sample.SupportSurfaceZ);
		InOutArtifact.AnchorResult.Samples.Add(SurfaceSample);
		if (InOutArtifact.AnchorResult.Samples.Num() == 1)
		{
			InOutArtifact.MinSurfaceZ = Sample.SupportSurfaceZ;
			InOutArtifact.MaxSurfaceZ = Sample.SupportSurfaceZ;
			InOutArtifact.AnchorResult.MinimumTerrainZ = Sample.SupportSurfaceZ;
			InOutArtifact.AnchorResult.MaximumTerrainZ = Sample.SupportSurfaceZ;
		}
		else
		{
			InOutArtifact.MinSurfaceZ = FMath::Min(InOutArtifact.MinSurfaceZ, Sample.SupportSurfaceZ);
			InOutArtifact.MaxSurfaceZ = FMath::Max(InOutArtifact.MaxSurfaceZ, Sample.SupportSurfaceZ);
			InOutArtifact.AnchorResult.MinimumTerrainZ = FMath::Min(
				InOutArtifact.AnchorResult.MinimumTerrainZ, Sample.SupportSurfaceZ);
			InOutArtifact.AnchorResult.MaximumTerrainZ = FMath::Max(
				InOutArtifact.AnchorResult.MaximumTerrainZ, Sample.SupportSurfaceZ);
		}
	}
	if (BaseSnappedSupportFloorZ != MAX_int32)
	{
		for (FLayoutTerrainPlacementCellEvidence& CellEvidence : InOutArtifact.TerrainPlacementCells)
		{
			if (const int32* const SnappedFloorZ = SnappedFloorByXY.Find(
					FIntPoint(CellEvidence.Cell.X, CellEvidence.Cell.Y)))
			{
				CellEvidence.VerticalShiftBlocks = *SnappedFloorZ - BaseSnappedSupportFloorZ;
				CellEvidence.TerrainStageIndex = SharedCellSizeInBlocks.Z > 0
					? CellEvidence.VerticalShiftBlocks / SharedCellSizeInBlocks.Z
					: 0;
			}
		}
	}
	if (bUnderground
		&& !ApplyUndergroundSelectedComponentEntryEvidence(
			FootprintMinBlockWorldPos,
			SharedCellSizeInBlocks,
			FootprintSizeInCells,
			TerrainTransitionPolicy,
			OutSupportMap,
			InOutArtifact.PocketVoidIntervals,
			InOutArtifact,
			OutFailureReason))
	{
		return false;
	}
	return true;
}


bool FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromPocketSamples(
	const FIntVector& AnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 TerrainSampleGridSpacing,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const TArray<FLayoutReservationPocketSample>& PocketSamples,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FString& OutFailureReason)
{
	TMap<FIntPoint, int32> SurfaceZBySampleGridXY;
	for (const FLayoutReservationPocketSample& PocketSample : PocketSamples)
	{
		if (!PocketSample.bHasSurfaceZ)
		{
			continue;
		}

		SurfaceZBySampleGridXY.Add(PocketSample.SampleGridXY, PocketSample.SurfaceZBlockWorld);
	}

	return TryBuildSteppedTerrainSupportMapFromSampleGridSurfaceHeights(
		SharedCellSizeInBlocks,
		TerrainSampleGridSpacing,
		PlannedCells,
		SurfaceZBySampleGridXY,
		OutSupportMap,
		OutFailureReason);
}

FLayoutFrozenTerrainBiomeAdapterInput FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
	const FIntVector& SiteCenterBlockWorldPos,
	const FIntVector& FootprintMinBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const FIntPoint& FootprintSizeInBlocks,
	const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
	const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const int32 TerrainSampleGridSpacing,
	const FName EligibleBiomeRowName,
	TConstArrayView<FName> EligibleBiomeRowNames,
	const FLayoutSteppedTerrainSupportMap& SupportMap,
	const bool bUseCardinalShiftClusters,
	const TConstArrayView<FLayoutSteppedTerrainSupportSample> NeighborHaloSamples)
{
	FLayoutFrozenTerrainBiomeAdapterInput Artifact;
	Artifact.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
	Artifact.FootprintMinBlockWorldPos = FootprintMinBlockWorldPos;
	Artifact.FootprintSizeInBlocks = FootprintSizeInBlocks;
	Artifact.SearchMinBlockXY = FIntPoint(FootprintMinBlockWorldPos.X, FootprintMinBlockWorldPos.Y);
	Artifact.SearchMaxBlockXY = FIntPoint(
		FootprintMinBlockWorldPos.X + FMath::Max(0, FootprintSizeInBlocks.X - 1),
		FootprintMinBlockWorldPos.Y + FMath::Max(0, FootprintSizeInBlocks.Y - 1));
	for (const FLayoutSteppedTerrainSupportSample& HaloSample : NeighborHaloSamples)
	{
		const FIntPoint HaloBlockXY(
			FootprintMinBlockWorldPos.X + HaloSample.LocalCell.X * SharedCellSizeInBlocks.X,
			FootprintMinBlockWorldPos.Y + HaloSample.LocalCell.Y * SharedCellSizeInBlocks.Y);
		Artifact.SearchMinBlockXY.X = FMath::Min(Artifact.SearchMinBlockXY.X, HaloBlockXY.X);
		Artifact.SearchMinBlockXY.Y = FMath::Min(Artifact.SearchMinBlockXY.Y, HaloBlockXY.Y);
		Artifact.SearchMaxBlockXY.X = FMath::Max(Artifact.SearchMaxBlockXY.X, HaloBlockXY.X);
		Artifact.SearchMaxBlockXY.Y = FMath::Max(Artifact.SearchMaxBlockXY.Y, HaloBlockXY.Y);
	}
	Artifact.CoordinateSettings = CoordinateSettings;
	Artifact.SearchStartZBlockWorld = SurfaceSearch.TerrainSearchStartZ;
	Artifact.SearchDepthBlocks = SurfaceSearch.TerrainSearchDepthBlocks;
	Artifact.TerrainSampleGridSpacing = TerrainSampleGridSpacing;
	Artifact.EligibleBiomeRowName = EligibleBiomeRowName;
	Artifact.EligibleBiomeRowNames.Append(EligibleBiomeRowNames.GetData(), EligibleBiomeRowNames.Num());
	if (!EligibleBiomeRowName.IsNone())
	{
		Artifact.EligibleBiomeRowNames.AddUnique(EligibleBiomeRowName);
	}
	Artifact.bRequiresBiomeOwnership = !Artifact.EligibleBiomeRowNames.IsEmpty();
	Artifact.bHasBiomeOwnershipEvidence = Artifact.bRequiresBiomeOwnership;
	Artifact.bHasSampledColumnEvidence = SupportMap.SupportSamples.Num() > 0;
	Artifact.bHasFiniteSearchBounds = FootprintSizeInBlocks.X > 0
		&& FootprintSizeInBlocks.Y > 0
		&& SurfaceSearch.TerrainSearchDepthBlocks > 0;
	Artifact.bHasSteppedSupportEvidence = SupportMap.SupportSamples.Num() > 0;
	Artifact.SteppedSupportSamples = SupportMap.SupportSamples;
	Artifact.SteppedNeighborHaloSamples.Append(NeighborHaloSamples.GetData(), NeighborHaloSamples.Num());
	Artifact.SteppedAdjacencySteps = SupportMap.AdjacencySteps;
	Artifact.MaxObservedNeighborHeightDelta = SupportMap.MaximumObservedNeighborHeightDelta;
	Artifact.MaxObservedSnappedLevelDelta = SupportMap.MaximumObservedSnappedLevelDelta;
	Artifact.AnchorResult.MaximumObservedNeighborHeightDelta = SupportMap.MaximumObservedNeighborHeightDelta;
	Artifact.AnchorResult.bIsValid = SupportMap.SupportSamples.Num() > 0;
	Artifact.AnchorResult.AnchorBlockWorldPos = FootprintMinBlockWorldPos;
	Artifact.AnchorResult.Samples.Reset(SupportMap.SupportSamples.Num());
	Artifact.AuditMessages.Add(FString::Printf(
		TEXT("SupportSamples=%d AdjacencySteps=%d BiomeRows=%d FiniteSearch=%d"),
		SupportMap.SupportSamples.Num(),
		SupportMap.AdjacencySteps.Num(),
		Artifact.EligibleBiomeRowNames.Num(),
		Artifact.bHasFiniteSearchBounds ? 1 : 0));
	Artifact.AuditMessages.Add(TEXT("TerrainPathSamples=0 PocketVoidIntervals=0 ShiftedFootprintEvidence=0 ExactFallbackEvidence=0"));
	const bool bAllowFoundationFillEvidence = TerrainTransitionPolicy.bAllowFoundationFill
		&& TerrainTransitionPolicy.MaxFoundationDepth > 0;
	const bool bAllowRampTransitionEvidence = TerrainTransitionPolicy.bAllowPerimeterRampTransition
		&& TerrainTransitionPolicy.MaxFoundationDepth > 0;
	int32 FoundationFillEvidenceCount = 0;
	int32 RampTransitionEvidenceCount = 0;
	int32 RampTransitionRejectedCount = 0;
	int32 ClearanceEvidenceCount = 0;
	bool bCanBuildPlacementEvidenceFromSteppedSupport = SharedCellSizeInBlocks.Z > 0 && !SupportMap.SupportSamples.IsEmpty();
	int32 BaseSnappedSupportFloorZ = 0;
	if (bCanBuildPlacementEvidenceFromSteppedSupport)
	{
		BaseSnappedSupportFloorZ = SupportMap.SupportSamples[0].SnappedSupportFloorZ;
		for (const FLayoutSteppedTerrainSupportSample& SupportSample : SupportMap.SupportSamples)
		{
			// Normalization must not turn an invalid source overlap into clearance evidence.
			const int64 SourceOverlap = static_cast<int64>(SupportSample.SupportSurfaceZ) - SupportSample.SnappedSupportFloorZ;
			if (SourceOverlap >= SharedCellSizeInBlocks.Z)
			{
				Artifact.AuditMessages.Add(TEXT("TerrainPlacementEvidence=RejectedInvalidLocalTerrainOverlap"));
				return Artifact;
			}
			BaseSnappedSupportFloorZ = FMath::Min(BaseSnappedSupportFloorZ, SupportSample.SnappedSupportFloorZ);
		}
	}
	bool bHasSurfaceRange = false;
	TMap<FIntPoint, int32> ModifiedFloorZByXY;

	// Build a mutable map of floor Z values.  For entries that survive the
	// cluster-size filter below, this stays equal to the original
	// SnappedSupportFloorZ; cells in too-small clusters get reset to
	// BaseSnappedSupportFloorZ before placement cells and stage map are built.
	for (const FLayoutSteppedTerrainSupportSample& SupportSample : SupportMap.SupportSamples)
	{
		ModifiedFloorZByXY.Add(
			FIntPoint(SupportSample.LocalCell.X, SupportSample.LocalCell.Y),
			SupportSample.SnappedSupportFloorZ);
	}

	if (bCanBuildPlacementEvidenceFromSteppedSupport && !SupportMap.SupportSamples.IsEmpty())
	{
		const int32 MinShiftClusterSize =
			TerrainTransitionPolicy.MinimumSteppedTerrainShiftClusterCells > 0
				? TerrainTransitionPolicy.MinimumSteppedTerrainShiftClusterCells
				: 3;

		// Count unique terrain columns, never stacked planned cells. Root terrain
		// preserves its established diagonal support; continuations opt into
		// cardinal-only clusters so diagonal corridor contact cannot preserve a step.
		TMap<int32, TSet<FIntPoint>> CellsByFloorZ;
		for (const FLayoutSteppedTerrainSupportSample& SupportSample : SupportMap.SupportSamples)
		{
			if (SupportSample.SnappedSupportFloorZ > BaseSnappedSupportFloorZ)
			{
				CellsByFloorZ.FindOrAdd(SupportSample.SnappedSupportFloorZ).Add(
					FIntPoint(SupportSample.LocalCell.X, SupportSample.LocalCell.Y));
			}
		}

		int32 ExcavatedCount = 0;
		for (TPair<int32, TSet<FIntPoint>>& FloorZPair : CellsByFloorZ)
		{
			TSet<FIntPoint> Unvisited = FloorZPair.Value;
			while (!Unvisited.IsEmpty())
			{
				// BFS over 8-connected components at this floor Z
				TArray<FIntPoint> Component;
				TArray<FIntPoint> Queue;
				Queue.Add(*Unvisited.CreateIterator());
				Unvisited.Remove(Queue[0]);
				while (!Queue.IsEmpty())
				{
					const FIntPoint Current = Queue.Pop(EAllowShrinking::No);
					Component.Add(Current);
					for (int32 DeltaY = -1; DeltaY <= 1; ++DeltaY)
					{
						for (int32 DeltaX = -1; DeltaX <= 1; ++DeltaX)
						{
							if ((DeltaX == 0 && DeltaY == 0)
								|| (bUseCardinalShiftClusters && DeltaX != 0 && DeltaY != 0))
							{
								continue;
							}
							const FIntPoint Neighbor(Current.X + DeltaX, Current.Y + DeltaY);
							if (Unvisited.Remove(Neighbor) > 0)
							{
								Queue.Add(Neighbor);
							}
						}
					}
				}

				// Keep a small terrace when it is the only intermediate stage below a
				// higher neighbor. Flattening it would manufacture an unsupported two-stage
				// cliff instead of preserving root-equivalent one-step bridge topology.
				bool bBridgesHigherTerrace = false;
				for (const FIntPoint& Pt : Component)
				{
					for (int32 DeltaY = -1; DeltaY <= 1 && !bBridgesHigherTerrace; ++DeltaY)
					{
						for (int32 DeltaX = -1; DeltaX <= 1; ++DeltaX)
						{
							if (DeltaX == 0 && DeltaY == 0)
							{
								continue;
							}
							if (const int32* const NeighborFloorZ = ModifiedFloorZByXY.Find(FIntPoint(Pt.X + DeltaX, Pt.Y + DeltaY));
								NeighborFloorZ != nullptr && *NeighborFloorZ > FloorZPair.Key)
							{
								bBridgesHigherTerrace = true;
								break;
							}
						}
					}
				}
				if (Component.Num() < MinShiftClusterSize && !bBridgesHigherTerrace)
				{
					for (const FIntPoint& Pt : Component)
					{
						if (int32* FloorZPtr = ModifiedFloorZByXY.Find(Pt))
						{
							*FloorZPtr = BaseSnappedSupportFloorZ;
							++ExcavatedCount;
						}
					}
				}
			}
		}

		UE_LOG(LogTemp, Display,
			TEXT("LayoutTerrainSampling: shift cluster analysis running. floorZ groups=%d, samples=%d, baseZ=%d"),
			CellsByFloorZ.Num(),
			SupportMap.SupportSamples.Num(),
			BaseSnappedSupportFloorZ);

		UE_LOG(LogTemp, Display,
			TEXT("LayoutTerrainSampling: cluster enforcement complete. excavated=%d"),
			ExcavatedCount);

		if (ExcavatedCount > 0)
		{
			Artifact.AuditMessages.Add(FString::Printf(
				TEXT("TerrainPlacementEvidence=ExcavatedSmallShiftClusters:%d (min cluster size=%d, floorZ groups=%d, samples=%d)"),
				ExcavatedCount,
				MinShiftClusterSize,
				CellsByFloorZ.Num(),
				SupportMap.SupportSamples.Num()));
		}
		else
		{
			Artifact.AuditMessages.Add(FString::Printf(
				TEXT("TerrainPlacementEvidence=AllShiftClustersLargeEnough (min cluster size=%d, floorZ groups=%d, samples=%d)"),
				MinShiftClusterSize,
				CellsByFloorZ.Num(),
				SupportMap.SupportSamples.Num()));
		}

		// Rebuild BaseSnappedSupportFloorZ from the (possibly flattened) values.
		BaseSnappedSupportFloorZ = INT_MAX;
		for (const auto& Pair : ModifiedFloorZByXY)
		{
			BaseSnappedSupportFloorZ = FMath::Min(BaseSnappedSupportFloorZ, Pair.Value);
		}

		// Push modified floor Z values into the artifact's support samples so the
		// adapter's StageMap builds from the same flattened data.
		for (FLayoutSteppedTerrainSupportSample& Sample : Artifact.SteppedSupportSamples)
		{
			if (const int32* ModifiedZ = ModifiedFloorZByXY.Find(FIntPoint(Sample.LocalCell.X, Sample.LocalCell.Y)))
			{
				Sample.SnappedSupportFloorZ = *ModifiedZ;
				// Also flatten the surface Z so ResolvedStageBaseBlockWorldZ (computed
				// from SupportSurfaceZ) matches the flattened floor.
				Sample.SupportSurfaceZ = *ModifiedZ;
			}
		}
	}

	// All downstream evidence reads normalized artifact support, never stale source
	// support. Preview, adapter stages, and frozen contract now share one artifact.
	for (const FLayoutSteppedTerrainSupportSample& SupportSample : Artifact.SteppedSupportSamples)
	{
		const FIntPoint SampleBlockXY(
			FootprintMinBlockWorldPos.X + SupportSample.LocalCell.X * SharedCellSizeInBlocks.X,
			FootprintMinBlockWorldPos.Y + SupportSample.LocalCell.Y * SharedCellSizeInBlocks.Y);
		FLayoutTerrainSurfaceSample& SurfaceSample = Artifact.SurfaceSamples.AddDefaulted_GetRef();
		SurfaceSample.bIsValid = true;
		SurfaceSample.BlockXY = SampleBlockXY;
		SurfaceSample.SurfaceBlockWorldPos = FIntVector(
			SampleBlockXY.X,
			SampleBlockXY.Y,
			SupportSample.SupportSurfaceZ);
		Artifact.AnchorResult.Samples.Add(SurfaceSample);
		FLayoutTerrainCellClassification& CellClassification = Artifact.FootprintClassification.CellClassifications.AddDefaulted_GetRef();
		CellClassification.BlockXY = SampleBlockXY;
		CellClassification.SurfaceZ = SupportSample.SupportSurfaceZ;
		CellClassification.Shape = ELayoutTerrainCellShape::Flat;
		if (bCanBuildPlacementEvidenceFromSteppedSupport)
		{
			const int32* ModifiedFloorZ =
				ModifiedFloorZByXY.Find(FIntPoint(SupportSample.LocalCell.X, SupportSample.LocalCell.Y));
			const int32 EffectiveFloorZ = ModifiedFloorZ != nullptr
				? *ModifiedFloorZ
				: SupportSample.SnappedSupportFloorZ;
			const int32 VerticalShiftBlocks = EffectiveFloorZ - BaseSnappedSupportFloorZ;
			if (VerticalShiftBlocks % SharedCellSizeInBlocks.Z != 0)
			{
				bCanBuildPlacementEvidenceFromSteppedSupport = false;
				Artifact.TerrainPlacementCells.Reset();
				Artifact.AuditMessages.Add(TEXT("TerrainPlacementEvidence=RejectedIncoherentSteppedSupportShift"));
			}
			else
			{
				FLayoutTerrainPlacementCellEvidence& PlacementCell = Artifact.TerrainPlacementCells.AddDefaulted_GetRef();
						PlacementCell.Cell = SupportSample.LocalCell;
				PlacementCell.bPlaceableForSelectedMode = true;
				PlacementCell.TerrainStageIndex = VerticalShiftBlocks / SharedCellSizeInBlocks.Z;
				PlacementCell.VerticalShiftBlocks = VerticalShiftBlocks;
				PlacementCell.ProvenanceId = FLayoutId(*FString::Printf(
					TEXT("SteppedSupport.%d.%d.%d"),
					SupportSample.LocalCell.X,
					SupportSample.LocalCell.Y,
					SupportSample.LocalCell.Z));

				const int32 LocalTerrainOverlapTopZ = SupportSample.SupportSurfaceZ - SupportSample.SnappedSupportFloorZ;
				if (LocalTerrainOverlapTopZ > 0)
				{
					if (LocalTerrainOverlapTopZ >= SharedCellSizeInBlocks.Z
						|| !LayoutLocalBlockCoordinates::TryMakeCoord8(0, PlacementCell.OverlapMinLocalZ)
						|| !LayoutLocalBlockCoordinates::TryMakeCoord8(LocalTerrainOverlapTopZ, PlacementCell.OverlapMaxLocalZ))
					{
						bCanBuildPlacementEvidenceFromSteppedSupport = false;
						Artifact.TerrainPlacementCells.Reset();
						Artifact.AuditMessages.Add(TEXT("TerrainPlacementEvidence=RejectedInvalidLocalTerrainOverlap"));
					}
					else
					{
						PlacementCell.bHasExcavationEvidence = true;
						PlacementCell.bHasLocalOverlapZ = true;
					}
				}
				else if (LocalTerrainOverlapTopZ < 0)
				{
					const int32 RequiredFoundationDepth = -LocalTerrainOverlapTopZ;
					if (bAllowFoundationFillEvidence && RequiredFoundationDepth <= TerrainTransitionPolicy.MaxFoundationDepth)
					{
						PlacementCell.bHasFoundationFillEvidence = true;
						++FoundationFillEvidenceCount;
					}
					else
					{
						PlacementCell.bPlaceableForSelectedMode = false;
					}
				}
				else
				{
					PlacementCell.bHasClearanceEvidence = true;
					++ClearanceEvidenceCount;
				}
			}
		}
		FLayoutFrozenBiomeOwnershipSample& OwnershipSample = Artifact.BiomeOwnershipSamples.AddDefaulted_GetRef();
		OwnershipSample.BlockXY = SampleBlockXY;
		OwnershipSample.OwningBiomeRowName = EligibleBiomeRowName;
		OwnershipSample.bOwnedByAllowList = !EligibleBiomeRowName.IsNone() || Artifact.EligibleBiomeRowNames.IsEmpty();
		OwnershipSample.bHasSurfaceEvidence = true;
		OwnershipSample.SurfaceZ = SupportSample.SupportSurfaceZ;
		if (!bHasSurfaceRange)
		{
			bHasSurfaceRange = true;
			Artifact.MinSurfaceZ = SupportSample.SupportSurfaceZ;
			Artifact.MaxSurfaceZ = SupportSample.SupportSurfaceZ;
			Artifact.AnchorResult.MinimumTerrainZ = SupportSample.SupportSurfaceZ;
			Artifact.AnchorResult.MaximumTerrainZ = SupportSample.SupportSurfaceZ;
		}
		else
		{
			Artifact.MinSurfaceZ = FMath::Min(Artifact.MinSurfaceZ, SupportSample.SupportSurfaceZ);
			Artifact.MaxSurfaceZ = FMath::Max(Artifact.MaxSurfaceZ, SupportSample.SupportSurfaceZ);
			Artifact.AnchorResult.MinimumTerrainZ = FMath::Min(Artifact.AnchorResult.MinimumTerrainZ, SupportSample.SupportSurfaceZ);
			Artifact.AnchorResult.MaximumTerrainZ = FMath::Max(Artifact.AnchorResult.MaximumTerrainZ, SupportSample.SupportSurfaceZ);
		}
	}
	for (const FLayoutSteppedTerrainAdjacencyStep& Step : SupportMap.AdjacencySteps)
	{
		if (Step.StepHeightBlocks > 0 && Step.SnappedLevelDelta > 0)
		{
			const bool bStepWithinRampDepthBudget = bAllowRampTransitionEvidence
				&& Step.StepHeightBlocks <= TerrainTransitionPolicy.MaxFoundationDepth;
			if (!bStepWithinRampDepthBudget)
			{
				++RampTransitionRejectedCount;
			}
			FLayoutTerrainTransitionRequirement& Transition = Artifact.FootprintClassification.TransitionRequirements.AddDefaulted_GetRef();
			Transition.FromBlockXY = FIntPoint(
				FootprintMinBlockWorldPos.X + Step.FromCell.X * SharedCellSizeInBlocks.X,
				FootprintMinBlockWorldPos.Y + Step.FromCell.Y * SharedCellSizeInBlocks.Y);
			Transition.ToBlockXY = FIntPoint(
				FootprintMinBlockWorldPos.X + Step.ToCell.X * SharedCellSizeInBlocks.X,
				FootprintMinBlockWorldPos.Y + Step.ToCell.Y * SharedCellSizeInBlocks.Y);
			Transition.StepHeight = Step.StepHeightBlocks;
			Artifact.FootprintClassification.bHasMildSlopeTransitions = true;
			for (FLayoutTerrainCellClassification& CellClassification : Artifact.FootprintClassification.CellClassifications)
			{
				if (CellClassification.BlockXY == Transition.FromBlockXY || CellClassification.BlockXY == Transition.ToBlockXY)
				{
					CellClassification.Shape = ELayoutTerrainCellShape::MildSlope;
				}
			}
		}
	}
	Artifact.bHasTerrainPlacementEvidence = bCanBuildPlacementEvidenceFromSteppedSupport && !Artifact.TerrainPlacementCells.IsEmpty();
	if (Artifact.bHasTerrainPlacementEvidence)
	{
		Artifact.AuditMessages.Add(FString::Printf(
			TEXT("TerrainPlacementEvidence=SteppedSupportSamples:%d Foundation:%d Ramp:%d RampRejected:%d Clearance:%d"),
			Artifact.TerrainPlacementCells.Num(),
			FoundationFillEvidenceCount,
			RampTransitionEvidenceCount,
			RampTransitionRejectedCount,
			ClearanceEvidenceCount));
	}
	Artifact.bHasFootprintClassificationEvidence = !Artifact.FootprintClassification.CellClassifications.IsEmpty();
	Artifact.AnchorResult.RequiredFoundationDepth = bHasSurfaceRange
		? FMath::Max(0, Artifact.MaxSurfaceZ - Artifact.MinSurfaceZ)
		: 0;
	Artifact.AnchorResult.bRequiresFoundationFill = Artifact.AnchorResult.RequiredFoundationDepth > 0;
	return Artifact;
}

void FLayoutTerrainSampling::ComputeEntryTraversabilityVerdicts(
	FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 MaxFoundationDepth,
	TFunctionRef<int32(int32, int32)> ResolveSurfaceZ)
{
	if (MaxFoundationDepth <= 0 || InOutArtifact.TerrainPlacementCells.IsEmpty())
	{
		return;
	}

	const int32 FootprintWidth = InOutArtifact.FootprintSizeInBlocks.X / SharedCellSizeInBlocks.X;
	const int32 FootprintHeight = InOutArtifact.FootprintSizeInBlocks.Y / SharedCellSizeInBlocks.Y;
	if (FootprintWidth <= 0 || FootprintHeight <= 0)
	{
		return;
	}

	// Build a map of inside surface Z values so the verdict loop can look up
	// the current cell's surface without relying on placement-cell ordering.
	TMap<FIntPoint, int32> InsideSurfaceZByCellXY;
	for (const FLayoutTerrainPlacementCellEvidence& Cell : InOutArtifact.TerrainPlacementCells)
	{
		InsideSurfaceZByCellXY.Add(FIntPoint(Cell.Cell.X, Cell.Cell.Y), Cell.VerticalShiftBlocks);
	}

	// Surface Z is relative to the base, but the existing placement evidence stores
	// VerticalShiftBlocks. For entry verdicts the delta computation uses absolute
	// surfaces; compute the inside absolute block Z from the evidence already present.
	// Placement cells were built from stepped support samples, each carrying
	// a surface Z. We resolve the absolute surface from the artifact's surface samples.
	TMap<FIntPoint, int32> InsideAbsoluteSurfaceZByCellXY;
	for (int32 CellY = 0; CellY < FootprintHeight; ++CellY)
	{
		for (int32 CellX = 0; CellX < FootprintWidth; ++CellX)
		{
			const int32 BlockX = InOutArtifact.FootprintMinBlockWorldPos.X + CellX * SharedCellSizeInBlocks.X + SharedCellSizeInBlocks.X / 2;
			const int32 BlockY = InOutArtifact.FootprintMinBlockWorldPos.Y + CellY * SharedCellSizeInBlocks.Y + SharedCellSizeInBlocks.Y / 2;
			const int32 SurfaceZ = ResolveSurfaceZ(BlockX, BlockY);
			InsideAbsoluteSurfaceZByCellXY.Add(FIntPoint(CellX, CellY), SurfaceZ);
		}
	}

	const int32 HalfFoundation = MaxFoundationDepth / 2;
	for (FLayoutTerrainPlacementCellEvidence& Cell : InOutArtifact.TerrainPlacementCells)
	{
		const int32 CellX = Cell.Cell.X;
		const int32 CellY = Cell.Cell.Y;
		const bool bOnBoundary = CellX == 0 || CellX == FootprintWidth - 1
			|| CellY == 0 || CellY == FootprintHeight - 1;
		if (!bOnBoundary)
		{
			continue;
		}

		int32 OutsideBlockX = InOutArtifact.FootprintMinBlockWorldPos.X + CellX * SharedCellSizeInBlocks.X + SharedCellSizeInBlocks.X / 2;
		int32 OutsideBlockY = InOutArtifact.FootprintMinBlockWorldPos.Y + CellY * SharedCellSizeInBlocks.Y + SharedCellSizeInBlocks.Y / 2;
		if (CellX == 0)           { OutsideBlockX -= SharedCellSizeInBlocks.X; }
		else if (CellX == FootprintWidth - 1) { OutsideBlockX += SharedCellSizeInBlocks.X; }
		if (CellY == 0)           { OutsideBlockY -= SharedCellSizeInBlocks.Y; }
		else if (CellY == FootprintHeight - 1) { OutsideBlockY += SharedCellSizeInBlocks.Y; }

		const int32 OutsideSurfaceZ = ResolveSurfaceZ(OutsideBlockX, OutsideBlockY);
		const int32* InsideSurfaceZ = InsideAbsoluteSurfaceZByCellXY.Find(FIntPoint(CellX, CellY));
		if (InsideSurfaceZ == nullptr)
		{
			continue;
		}

		const int32 Delta = FMath::Abs(*InsideSurfaceZ - OutsideSurfaceZ);
		if (Delta <= HalfFoundation)
		{
			Cell.EntryTraversability = ELayoutEntryTraversabilityVerdict::Walkable;
		}
		else if (Delta <= MaxFoundationDepth)
		{
			Cell.EntryTraversability = ELayoutEntryTraversabilityVerdict::RampNeeded;
		}
		else if (*InsideSurfaceZ < OutsideSurfaceZ)
		{
			Cell.EntryTraversability = ELayoutEntryTraversabilityVerdict::ExcavationNeeded;
		}
		else
		{
			Cell.EntryTraversability = ELayoutEntryTraversabilityVerdict::CliffEdge;
		}
	}

	InOutArtifact.AuditMessages.Add(FString::Printf(
		TEXT("EntryTraversability=Computed boundaryCells footPrint=%dx%d maxFoundation=%d"),
		FootprintWidth, FootprintHeight, MaxFoundationDepth));
}

bool FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
	const FIntVector& AnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 TerrainSampleGridSpacing,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FString& OutFailureReason)
{
	OutSupportMap = FLayoutSteppedTerrainSupportMap();
	OutFailureReason.Reset();

	if (!ActiveBiomeSampler.IsInitialized())
	{
		OutFailureReason = TEXT("Stepped terrain support map requires one initialized active biome sampler.");
		return false;
	}

	TArray<FLayoutReservationPocketSample> PocketSamples;
	PocketSamples.Reserve(PlannedCells.Num());

	for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
	{
		const FIntPoint SampleBlockXY = ComputeSteppedTerrainCenterSampleBlockXY(
			AnchorBlockWorldPos,
			SharedCellSizeInBlocks,
			PlannedCell.Cell);
		FLayoutActiveBiomeSurfaceSample Surface;
		const bool bSurfaceSearchCompleted = ActiveBiomeSampler.FindAnyActiveBiomeSurface(
			SampleBlockXY,
			SurfaceSearch.TerrainSearchStartZ,
			SurfaceSearch.TerrainSearchDepthBlocks,
			CoordinateSettings,
			Surface);
		if (!bSurfaceSearchCompleted)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped terrain support map failed to evaluate active biome surface for planned cell %s at block XY (%d, %d)."),
				*PlannedCell.Cell.ToString(),
				SampleBlockXY.X,
				SampleBlockXY.Y);
			return false;
		}

		if (!Surface.bIsValid)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped terrain support map found no active biome surface for planned cell %s at block XY (%d, %d)."),
				*PlannedCell.Cell.ToString(),
				SampleBlockXY.X,
				SampleBlockXY.Y);
			return false;
		}

		FLayoutReservationPocketSample& PocketSample = PocketSamples.AddDefaulted_GetRef();
		PocketSample.SampleGridXY = FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y);
		PocketSample.BlockXY = SampleBlockXY;
		PocketSample.NoiseValue = Surface.BiomeSample.WinningDomainValue;
		PocketSample.bHasSurfaceZ = true;
		PocketSample.SurfaceZBlockWorld = Surface.SurfaceBlockWorldPos.Z;
	}

	return TryBuildSteppedTerrainSupportMapFromPocketSamples(
		AnchorBlockWorldPos,
		SharedCellSizeInBlocks,
		TerrainSampleGridSpacing,
		PlannedCells,
		PocketSamples,
		OutSupportMap,
		OutFailureReason);
}

bool FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSurfaceWorld(
	AChunkWorldCore* const World,
	const FIntVector& AnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 TerrainSampleGridSpacing,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
	const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
	FLayoutSteppedTerrainSupportMap& OutSupportMap,
	FString& OutFailureReason)
{
	OutSupportMap = FLayoutSteppedTerrainSupportMap();
	OutFailureReason.Reset();

	if (World == nullptr)
	{
		OutFailureReason = TEXT("Stepped terrain support map requires one non-null chunk world when sampling live active-biome surfaces.");
		return false;
	}

	if (!ActiveBiomeSampler.IsInitialized())
	{
		OutFailureReason = TEXT("Stepped terrain support map requires one initialized active biome sampler when sampling live active-biome surfaces.");
		return false;
	}

	TArray<FLayoutReservationPocketSample> PocketSamples;
	PocketSamples.Reserve(PlannedCells.Num());

	for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
	{
		const FIntPoint SampleBlockXY = ComputeSteppedTerrainCenterSampleBlockXY(
			AnchorBlockWorldPos,
			SharedCellSizeInBlocks,
			PlannedCell.Cell);
		const FLayoutTerrainSurfaceSample Surface =
			SampleTopDownEligibleBiomeSurfaceFromWorld(
				World,
				NAME_None,
				SampleBlockXY,
				SurfaceSearch.TerrainSearchStartZ,
				SurfaceSearch.TerrainSearchDepthBlocks,
				CoordinateSettings,
				ActiveBiomeSampler);
		if (!Surface.bIsValid)
		{
			OutFailureReason = FString::Printf(
				TEXT("Stepped terrain support map found no active biome surface for planned cell %s at block XY (%d, %d)."),
				*PlannedCell.Cell.ToString(),
				SampleBlockXY.X,
				SampleBlockXY.Y);
			return false;
		}

		FLayoutReservationPocketSample& PocketSample = PocketSamples.AddDefaulted_GetRef();
		PocketSample.SampleGridXY = FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y);
		PocketSample.BlockXY = SampleBlockXY;
		PocketSample.NoiseValue = 1.0f;
		PocketSample.bHasSurfaceZ = true;
		PocketSample.SurfaceZBlockWorld = Surface.SurfaceBlockWorldPos.Z;
	}

	return TryBuildSteppedTerrainSupportMapFromPocketSamples(
		AnchorBlockWorldPos,
		SharedCellSizeInBlocks,
		TerrainSampleGridSpacing,
		PlannedCells,
		PocketSamples,
		OutSupportMap,
		OutFailureReason);
}

bool FLayoutTerrainSampling::AreSteppedTerrainSupportMapsEquivalent(
	const FLayoutSteppedTerrainSupportMap& Left,
	const FLayoutSteppedTerrainSupportMap& Right,
	FString* const OutMismatchReason)
{
	if (OutMismatchReason != nullptr)
	{
		OutMismatchReason->Reset();
	}

	const auto SetMismatch = [OutMismatchReason](const FString& Reason)
	{
		if (OutMismatchReason != nullptr)
		{
			*OutMismatchReason = Reason;
		}
		return false;
	};

	if (Left.SharedCellHeightInBlocks != Right.SharedCellHeightInBlocks)
	{
		return SetMismatch(FString::Printf(
			TEXT("shared cell height differs: %d vs %d"),
			Left.SharedCellHeightInBlocks,
			Right.SharedCellHeightInBlocks));
	}
	if (Left.MaximumObservedNeighborHeightDelta != Right.MaximumObservedNeighborHeightDelta)
	{
		return SetMismatch(FString::Printf(
			TEXT("maximum raw neighbor delta differs: %d vs %d"),
			Left.MaximumObservedNeighborHeightDelta,
			Right.MaximumObservedNeighborHeightDelta));
	}
	if (Left.MaximumObservedSnappedLevelDelta != Right.MaximumObservedSnappedLevelDelta)
	{
		return SetMismatch(FString::Printf(
			TEXT("maximum snapped level delta differs: %d vs %d"),
			Left.MaximumObservedSnappedLevelDelta,
			Right.MaximumObservedSnappedLevelDelta));
	}
	if (Left.SupportSamples.Num() != Right.SupportSamples.Num())
	{
		return SetMismatch(FString::Printf(
			TEXT("support sample count differs: %d vs %d"),
			Left.SupportSamples.Num(),
			Right.SupportSamples.Num()));
	}
	if (Left.AdjacencySteps.Num() != Right.AdjacencySteps.Num())
	{
		return SetMismatch(FString::Printf(
			TEXT("adjacency step count differs: %d vs %d"),
			Left.AdjacencySteps.Num(),
			Right.AdjacencySteps.Num()));
	}

	for (int32 Index = 0; Index < Left.SupportSamples.Num(); ++Index)
	{
		const FLayoutSteppedTerrainSupportSample& LeftSample = Left.SupportSamples[Index];
		const FLayoutSteppedTerrainSupportSample& RightSample = Right.SupportSamples[Index];
		if (LeftSample.LocalCell != RightSample.LocalCell
			|| LeftSample.SupportSurfaceZ != RightSample.SupportSurfaceZ
			|| LeftSample.SnappedSupportFloorZ != RightSample.SnappedSupportFloorZ
			|| LeftSample.SnappedSupportCeilingZ != RightSample.SnappedSupportCeilingZ)
		{
			return SetMismatch(FString::Printf(
				TEXT("support sample %d differs: %s/%d/%d/%d vs %s/%d/%d/%d"),
				Index,
				*LeftSample.LocalCell.ToString(),
				LeftSample.SupportSurfaceZ,
				LeftSample.SnappedSupportFloorZ,
				LeftSample.SnappedSupportCeilingZ,
				*RightSample.LocalCell.ToString(),
				RightSample.SupportSurfaceZ,
				RightSample.SnappedSupportFloorZ,
				RightSample.SnappedSupportCeilingZ));
		}
	}

	for (int32 Index = 0; Index < Left.AdjacencySteps.Num(); ++Index)
	{
		const FLayoutSteppedTerrainAdjacencyStep& LeftStep = Left.AdjacencySteps[Index];
		const FLayoutSteppedTerrainAdjacencyStep& RightStep = Right.AdjacencySteps[Index];
		if (LeftStep.FromCell != RightStep.FromCell
			|| LeftStep.ToCell != RightStep.ToCell
			|| LeftStep.StepHeightBlocks != RightStep.StepHeightBlocks
			|| LeftStep.SnappedLevelDelta != RightStep.SnappedLevelDelta)
		{
			return SetMismatch(FString::Printf(
				TEXT("adjacency step %d differs: %s->%s raw %d snapped %d vs %s->%s raw %d snapped %d"),
				Index,
				*LeftStep.FromCell.ToString(),
				*LeftStep.ToCell.ToString(),
				LeftStep.StepHeightBlocks,
				LeftStep.SnappedLevelDelta,
				*RightStep.FromCell.ToString(),
				*RightStep.ToCell.ToString(),
				RightStep.StepHeightBlocks,
				RightStep.SnappedLevelDelta));
		}
	}

	return true;
}

bool FLayoutTerrainSampling::AreSteppedTerrainSupportMapsContractEquivalent(
	const FLayoutSteppedTerrainSupportMap& Left,
	const FLayoutSteppedTerrainSupportMap& Right,
	FString* const OutMismatchReason)
{
	if (OutMismatchReason != nullptr)
	{
		OutMismatchReason->Reset();
	}

	const auto SetMismatch = [OutMismatchReason](const FString& Reason)
	{
		if (OutMismatchReason != nullptr)
		{
			*OutMismatchReason = Reason;
		}
		return false;
	};

	if (Left.SharedCellHeightInBlocks != Right.SharedCellHeightInBlocks)
	{
		return SetMismatch(FString::Printf(
			TEXT("shared cell height differs: %d vs %d"),
			Left.SharedCellHeightInBlocks,
			Right.SharedCellHeightInBlocks));
	}
	if (Left.MaximumObservedSnappedLevelDelta != Right.MaximumObservedSnappedLevelDelta)
	{
		return SetMismatch(FString::Printf(
			TEXT("maximum snapped level delta differs: %d vs %d"),
			Left.MaximumObservedSnappedLevelDelta,
			Right.MaximumObservedSnappedLevelDelta));
	}
	if (Left.SupportSamples.Num() != Right.SupportSamples.Num())
	{
		return SetMismatch(FString::Printf(
			TEXT("support sample count differs: %d vs %d"),
			Left.SupportSamples.Num(),
			Right.SupportSamples.Num()));
	}
	if (Left.AdjacencySteps.Num() != Right.AdjacencySteps.Num())
	{
		return SetMismatch(FString::Printf(
			TEXT("adjacency step count differs: %d vs %d"),
			Left.AdjacencySteps.Num(),
			Right.AdjacencySteps.Num()));
	}

	if (Left.MaximumObservedNeighborHeightDelta != Right.MaximumObservedNeighborHeightDelta)
	{
		return SetMismatch(FString::Printf(
			TEXT("raw neighbor delta differs: %d vs %d"),
			Left.MaximumObservedNeighborHeightDelta,
			Right.MaximumObservedNeighborHeightDelta));
	}

	for (int32 Index = 0; Index < Left.SupportSamples.Num(); ++Index)
	{
		const FLayoutSteppedTerrainSupportSample& LeftSample = Left.SupportSamples[Index];
		const FLayoutSteppedTerrainSupportSample& RightSample = Right.SupportSamples[Index];
		if (LeftSample.LocalCell != RightSample.LocalCell
			|| LeftSample.SnappedSupportFloorZ != RightSample.SnappedSupportFloorZ
			|| LeftSample.SnappedSupportCeilingZ != RightSample.SnappedSupportCeilingZ)
		{
			return SetMismatch(FString::Printf(
				TEXT("support sample %d snapped contract differs: %s/%d/%d vs %s/%d/%d"),
				Index,
				*LeftSample.LocalCell.ToString(),
				LeftSample.SnappedSupportFloorZ,
				LeftSample.SnappedSupportCeilingZ,
				*RightSample.LocalCell.ToString(),
				RightSample.SnappedSupportFloorZ,
				RightSample.SnappedSupportCeilingZ));
		}
	}

	for (int32 Index = 0; Index < Left.AdjacencySteps.Num(); ++Index)
	{
		const FLayoutSteppedTerrainAdjacencyStep& LeftStep = Left.AdjacencySteps[Index];
		const FLayoutSteppedTerrainAdjacencyStep& RightStep = Right.AdjacencySteps[Index];
		if (LeftStep.FromCell != RightStep.FromCell
			|| LeftStep.ToCell != RightStep.ToCell
			|| LeftStep.SnappedLevelDelta != RightStep.SnappedLevelDelta)
		{
			return SetMismatch(FString::Printf(
				TEXT("adjacency step %d snapped contract differs: %s->%s delta %d vs %s->%s delta %d"),
				Index,
				*LeftStep.FromCell.ToString(),
				*LeftStep.ToCell.ToString(),
				LeftStep.SnappedLevelDelta,
				*RightStep.FromCell.ToString(),
				*RightStep.ToCell.ToString(),
				RightStep.SnappedLevelDelta));
		}
	}

	return true;
}

void FLayoutTerrainSampling::BuildFoundationFillBlocks(
	const FLayoutTerrainAnchorResult& AnchorResult,
	TArray<FIntVector>& OutBlockPositions,
	TArray<int32>& OutMaterialIndices)
{
	OutBlockPositions.Reset();
	OutMaterialIndices.Reset();

	if (!AnchorResult.bIsValid || !AnchorResult.bRequiresFoundationFill)
	{
		return;
	}

	TSet<FIntVector> SeenPositions;
	for (const FLayoutTerrainSurfaceSample& Sample : AnchorResult.Samples)
	{
		if (!Sample.bIsValid || Sample.SurfaceMaterialIndex == EmptyMaterial)
		{
			continue;
		}

		for (int32 Z = Sample.SurfaceBlockWorldPos.Z + 1; Z < AnchorResult.AnchorBlockWorldPos.Z; ++Z)
		{
			const FIntVector FillPos(Sample.BlockXY.X, Sample.BlockXY.Y, Z);
			if (SeenPositions.Contains(FillPos))
			{
				continue;
			}

			SeenPositions.Add(FillPos);
			OutBlockPositions.Add(FillPos);
			OutMaterialIndices.Add(Sample.SurfaceMaterialIndex);
		}
	}
}
