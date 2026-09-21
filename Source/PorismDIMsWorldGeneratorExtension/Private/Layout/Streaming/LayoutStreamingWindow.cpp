// Copyright 2026 Spotted Loaf Studio

#include "Layout/Streaming/LayoutStreamingWindow.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Contracts/LayoutContractTypes.h"

namespace
{
	int32 FloorDivide(const int32 Value, const int32 Divisor)
	{
		check(Divisor > 0);
		if (Value >= 0)
		{
			return Value / Divisor;
		}

		return -(((-Value) + Divisor - 1) / Divisor);
	}

	int32 SnapToNearestLatticePlane(const int32 Value, const int32 LatticeSpacing)
	{
		if (LatticeSpacing <= 0)
		{
			return Value;
		}

		return FMath::RoundToInt(static_cast<double>(Value) / static_cast<double>(LatticeSpacing))
			* LatticeSpacing;
	}

	FIntVector RotateStreamingPlacementCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		const int32 NormalizedYawSteps = ((YawRotationSteps % 4) + 4) % 4;
		switch (NormalizedYawSteps)
		{
		case 1:
			return FIntVector(FootprintSize.Y - 1 - Cell.Y, Cell.X, Cell.Z);
		case 2:
			return FIntVector(FootprintSize.X - 1 - Cell.X, FootprintSize.Y - 1 - Cell.Y, Cell.Z);
		case 3:
			return FIntVector(Cell.Y, FootprintSize.X - 1 - Cell.X, Cell.Z);
		case 0:
		default:
			return Cell;
		}
	}

	FIntPoint ResolveOccupiedLocalCellRotationFootprint(const TArray<FIntVector>& OccupiedLocalCells)
	{
		if (OccupiedLocalCells.IsEmpty())
		{
			return FIntPoint(1, 1);
		}

		FIntVector MinCell = OccupiedLocalCells[0];
		FIntVector MaxCell = OccupiedLocalCells[0];
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			MinCell.X = FMath::Min(MinCell.X, LocalCell.X);
			MinCell.Y = FMath::Min(MinCell.Y, LocalCell.Y);
			MaxCell.X = FMath::Max(MaxCell.X, LocalCell.X);
			MaxCell.Y = FMath::Max(MaxCell.Y, LocalCell.Y);
		}

		return FIntPoint(
			FMath::Max(1, MaxCell.X - MinCell.X + 1),
			FMath::Max(1, MaxCell.Y - MinCell.Y + 1));
	}

	TArray<FIntVector> ResolveStreamingPlacementOccupiedLocalCells(const FLayoutPlacedModule& Placement)
	{
		if (!Placement.OccupiedLocalCells.IsEmpty())
		{
			return Placement.OccupiedLocalCells;
		}

		if (Placement.CompositeModule != nullptr)
		{
			return Placement.CompositeModule->GetOccupiedLocalCells();
		}

		if (Placement.Module != nullptr)
		{
			return Placement.Module->GetOccupiedLocalCells();
		}

		return {};
	}

	FIntPoint ResolveStreamingPlacementRotationFootprint(const FLayoutPlacedModule& Placement)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolveStreamingPlacementOccupiedLocalCells(Placement);
		if (!OccupiedLocalCells.IsEmpty())
		{
			return ResolveOccupiedLocalCellRotationFootprint(OccupiedLocalCells);
		}

		if (Placement.BundleBoundsCells != FIntVector::ZeroValue)
		{
			return FIntPoint(
				FMath::Max(1, Placement.BundleBoundsCells.X),
				FMath::Max(1, Placement.BundleBoundsCells.Y));
		}

		if (Placement.CompositeModule != nullptr)
		{
			const FIntVector BoundsCells = Placement.CompositeModule->GetBoundsCells();
			return FIntPoint(FMath::Max(1, BoundsCells.X), FMath::Max(1, BoundsCells.Y));
		}

		if (Placement.Module != nullptr)
		{
			// Live leaf modules use a one-cell contract on active caller paths. If
			// the solved occupied-cell carrier is absent, do not reopen raw legacy
			// BoundsCells as a fallback here.
			return FIntPoint(1, 1);
		}

		return FIntPoint(1, 1);
	}

	TArray<FIntVector> BuildStreamingPlacementAnchorBlockWorldPositions(
		const FLayoutPlacedModule& Placement,
		const FIntVector& RootAnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolveStreamingPlacementOccupiedLocalCells(Placement);
		if (OccupiedLocalCells.IsEmpty())
		{
			return {RootAnchorBlockWorldPos};
		}

		const FIntPoint RotationFootprint = ResolveOccupiedLocalCellRotationFootprint(OccupiedLocalCells);
		TArray<FIntVector> AnchorPositions;
		AnchorPositions.Reserve(OccupiedLocalCells.Num());
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			const FIntVector RotatedLocalCell =
				RotateStreamingPlacementCellInFootprintYaw(LocalCell, RotationFootprint, Placement.YawRotationSteps);
			AnchorPositions.Add(RootAnchorBlockWorldPos + FIntVector(
				RotatedLocalCell.X * SharedCellSizeInBlocks.X,
				RotatedLocalCell.Y * SharedCellSizeInBlocks.Y,
				RotatedLocalCell.Z * SharedCellSizeInBlocks.Z));
		}

		return AnchorPositions;
	}
}

TArray<FIntVector> FLayoutStreamingWindow::BuildPlacementAnchorBlockWorldPositions(
	const FLayoutPlacedModule& Placement,
	const FIntVector& RootAnchorBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks)
{
	return BuildStreamingPlacementAnchorBlockWorldPositions(
		Placement,
		RootAnchorBlockWorldPos,
		SharedCellSizeInBlocks);
}

FIntVector FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(
	const FIntVector& BlockWorldPos,
	const FIntVector& ChunkSizeInBlocks)
{
	const FIntVector SafeChunkSize(
		FMath::Max(1, ChunkSizeInBlocks.X),
		FMath::Max(1, ChunkSizeInBlocks.Y),
		FMath::Max(1, ChunkSizeInBlocks.Z));

	const FIntVector ChunkGridPos(
		FloorDivide(BlockWorldPos.X, SafeChunkSize.X),
		FloorDivide(BlockWorldPos.Y, SafeChunkSize.Y),
		FloorDivide(BlockWorldPos.Z, SafeChunkSize.Z));

	return FIntVector(
		ChunkGridPos.X * SafeChunkSize.X,
		ChunkGridPos.Y * SafeChunkSize.Y,
		ChunkGridPos.Z * SafeChunkSize.Z);
}

FIntVector FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
	const FIntVector& SiteCenterBlockWorldPos,
	const FIntPoint& FootprintSizeInCells,
	const FIntVector& SharedCellSizeInBlocks)
{
	const FIntVector FootprintSizeInBlocks(
		FootprintSizeInCells.X * SharedCellSizeInBlocks.X,
		FootprintSizeInCells.Y * SharedCellSizeInBlocks.Y,
		0);

	FIntVector MinBlockWorldPos = SiteCenterBlockWorldPos;
	MinBlockWorldPos.X -= FootprintSizeInBlocks.X / 2;
	MinBlockWorldPos.Y -= FootprintSizeInBlocks.Y / 2;
	return MinBlockWorldPos;
}

FIntVector FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
	const FIntVector& SiteCenterBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks)
{
	return FIntVector(
		SnapToNearestLatticePlane(SiteCenterBlockWorldPos.X, SharedCellSizeInBlocks.X),
		SnapToNearestLatticePlane(SiteCenterBlockWorldPos.Y, SharedCellSizeInBlocks.Y),
		SnapToNearestLatticePlane(SiteCenterBlockWorldPos.Z, SharedCellSizeInBlocks.Z));
}

FIntVector FLayoutStreamingWindow::ComputeSitePlacementAnchorBlockWorldPos(
	const FResolvedLayoutSiteRecord& SiteRecord,
	const FLayoutPlacedModule& Placement,
	const FIntVector& SharedCellSizeInBlocks)
{
	const FResolvedLayoutSiteLocationMetadata LocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	const FIntVector FootprintMin =
		LocationMetadata.RealizedFootprintMinBlockWorldPos != FIntVector::ZeroValue
			? LocationMetadata.RealizedFootprintMinBlockWorldPos
			: ComputeFootprintMinBlockWorldPos(
				LocationMetadata.SiteCenterBlockWorldPos,
				SolvedPayload.SolveResult.FootprintSize,
				SharedCellSizeInBlocks);

	// Use per-column ResolvedStageBaseBlockWorldZ from the frozen stage map when available
	// so stepped-terrain anchors match the write-plan and debug-draw positions.
	int32 AnchorZ = FootprintMin.Z + Placement.Cell.Z * SharedCellSizeInBlocks.Z + SolvedPayload.SolveResult.TemplatePlacementZOffsetBlocks;
	if (const int32* const BaseZ = SiteRecord.CachedFrozenTerrainBaseZByColumn.Find(FIntPoint(Placement.Cell.X, Placement.Cell.Y)))
	{
		AnchorZ = *BaseZ + Placement.Cell.Z * SharedCellSizeInBlocks.Z + SolvedPayload.SolveResult.TemplatePlacementZOffsetBlocks;
	}

	return FIntVector(
		FootprintMin.X + Placement.Cell.X * SharedCellSizeInBlocks.X,
		FootprintMin.Y + Placement.Cell.Y * SharedCellSizeInBlocks.Y,
		AnchorZ);
}

FIntVector FLayoutStreamingWindow::ComputeConnectorFootprintMinBlockWorldPos(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const FIntVector& SharedCellSizeInBlocks)
{
	return ConnectorRecord.PathOriginBlockWorldPos - FIntVector(
		SharedCellSizeInBlocks.X / 2,
		SharedCellSizeInBlocks.Y / 2,
		SharedCellSizeInBlocks.Z / 2);
}

TSet<FIntVector> FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
	const FResolvedLayoutSiteRecord& SiteRecord,
	const FIntVector& SharedCellSizeInBlocks,
	const FIntVector& ChunkSizeInBlocks)
{
	TSet<FIntVector> ChunkOrigins;
	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	for (const FLayoutPlacedModule& Placement : SolvedPayload.SolveResult.Placements)
	{
		for (const FIntVector& AnchorBlockWorldPos : BuildStreamingPlacementAnchorBlockWorldPositions(
			Placement,
			ComputeSitePlacementAnchorBlockWorldPos(SiteRecord, Placement, SharedCellSizeInBlocks),
			SharedCellSizeInBlocks))
		{
			ChunkOrigins.Add(BlockWorldPosToChunkOrigin(AnchorBlockWorldPos, ChunkSizeInBlocks));
		}
	}

	return ChunkOrigins;
}

void FLayoutStreamingWindow::AddFrozenTerrainChunkOrigins(
	const FLayoutFrozenTerrainContract& Contract,
	const FIntVector& ChunkSizeInBlocks,
	TSet<FIntVector>& InOutChunkOrigins)
{
	for (const FLayoutFrozenTerrainWriteRecord& Write : Contract.TerrainWrites)
	{
		InOutChunkOrigins.Add(BlockWorldPosToChunkOrigin(Write.BlockWorldPos, ChunkSizeInBlocks));
		if (Write.bResolveMaterialFromTerrain)
		{
			const FIntVector SourceOrigin = BlockWorldPosToChunkOrigin(Write.MaterialSourceBlockWorldPos, ChunkSizeInBlocks);
			const int32 MinSourceZ = static_cast<int32>(FMath::Max<int64>(MIN_int32,
				static_cast<int64>(Write.MaterialSourceBlockWorldPos.Z) - FMath::Max(0, Write.MaterialSourceSearchDepthBlocks)));
			const int32 MinOriginZ = BlockWorldPosToChunkOrigin(FIntVector(SourceOrigin.X, SourceOrigin.Y, MinSourceZ), ChunkSizeInBlocks).Z;
			for (int64 Z = SourceOrigin.Z; Z >= MinOriginZ; Z -= FMath::Max(1, ChunkSizeInBlocks.Z))
			{
				InOutChunkOrigins.Add(FIntVector(SourceOrigin.X, SourceOrigin.Y, static_cast<int32>(Z)));
			}
		}
	}
}

int32 FLayoutStreamingWindow::FindLoadedLayerAtPosition(
	const FIntVector& Position,
	const TConstArrayView<FLayoutLoadedChunkLayer> Layers,
	const bool bRequireCreated)
{
	for (int32 Level = Layers.Num() - 1; Level >= 0; --Level)
	{
		const FLayoutLoadedChunkLayer& Layer = Layers[Level];
		if (Layer.ChunkSizeInBlocks.GetMin() <= 0) continue;
		const FLayoutLoadedChunkState* Chunk = Layer.Chunks.Find(BlockWorldPosToChunkOrigin(Position, Layer.ChunkSizeInBlocks));
		if (Chunk) return !bRequireCreated || Chunk->bCreated ? Level : INDEX_NONE;
	}
	return INDEX_NONE;
}

bool FLayoutStreamingWindow::IsBlockBoxCovered(
	const FIntVector& Min,
	const FIntVector& Max,
	const TConstArrayView<FLayoutLoadedChunkLayer> Layers,
	const bool bRequireCreated)
{
	if (Min.X > Max.X || Min.Y > Max.Y || Min.Z > Max.Z) return false;
	struct FBox { FIntVector Min; FIntVector Max; };
	TArray<FBox, TInlineAllocator<8>> Remaining;
	Remaining.Add({Min, Max});
	if (bRequireCreated)
	{
		// Scan current loaded records; add a spatial index only if coverage profiling warrants it.
		// Consume finest coverage first, including fine chunks strictly inside a coarse box.
		// Once consumed, coarser restored data cannot override that authority.
		for (int32 Level = Layers.Num() - 1; Level >= 0; --Level)
		{
			const FLayoutLoadedChunkLayer& Layer = Layers[Level];
			if (Layer.ChunkSizeInBlocks.GetMin() <= 0) continue;
			for (const auto& Pair : Layer.Chunks)
			{
				for (int32 Index = Remaining.Num() - 1; Index >= 0; --Index)
				{
					const FBox Box = Remaining[Index];
					FBox Intersection;
					bool bIntersects = true;
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						Intersection.Min[Axis] = FMath::Max(Box.Min[Axis], Pair.Key[Axis]);
						Intersection.Max[Axis] = static_cast<int32>(FMath::Min<int64>(Box.Max[Axis],
							int64(Pair.Key[Axis]) + Layer.ChunkSizeInBlocks[Axis] - 1));
						bIntersects &= Intersection.Min[Axis] <= Intersection.Max[Axis];
					}
					if (!bIntersects) continue;
					if (!Pair.Value.bCreated) return false;
					Remaining.RemoveAtSwap(Index, 1, EAllowShrinking::No);
					// Six disjoint residual slabs; strict inequalities keep +/-1 in range.
					FBox Residual = Box;
					for (int32 Axis = 0; Axis < 3; ++Axis)
					{
						if (Residual.Min[Axis] < Intersection.Min[Axis])
						{
							FBox Slab = Residual;
							Slab.Max[Axis] = Intersection.Min[Axis] - 1;
							Remaining.Add(Slab);
						}
						if (Residual.Max[Axis] > Intersection.Max[Axis])
						{
							FBox Slab = Residual;
							Slab.Min[Axis] = Intersection.Max[Axis] + 1;
							Remaining.Add(Slab);
						}
						Residual.Min[Axis] = Intersection.Min[Axis];
						Residual.Max[Axis] = Intersection.Max[Axis];
					}
				}
				if (Remaining.IsEmpty()) return true;
			}
		}
		return false;
	}
	while (!Remaining.IsEmpty())
	{
		const FBox Box = Remaining.Pop(EAllowShrinking::No);
		const int32 Level = FindLoadedLayerAtPosition(Box.Min, Layers, bRequireCreated);
		if (Level == INDEX_NONE) return false;
		const FIntVector Size = Layers[Level].ChunkSizeInBlocks;
		const FIntVector Origin = BlockWorldPosToChunkOrigin(Box.Min, Size);
		FIntVector CoveredMax;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			CoveredMax[Axis] = static_cast<int32>(FMath::Min<int64>(
				static_cast<int64>(Origin[Axis]) + Size[Axis] - 1, Box.Max[Axis]));
		}
		// The intersection starts at Box.Min. Three disjoint residual slabs cover everything else.
		FBox Residual = Box;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (CoveredMax[Axis] < Box.Max[Axis])
			{
				FBox Slab = Residual;
				Slab.Min[Axis] = CoveredMax[Axis] + 1;
				Remaining.Add(Slab);
			}
			Residual.Max[Axis] = CoveredMax[Axis];
		}
	}
	return true;
}

bool FLayoutStreamingWindow::AreRequiredChunkOriginsObserved(
	const TSet<FIntVector>& RequiredChunkOrigins,
	const TSet<FIntVector>& ObservedChunkOrigins)
{
	if (RequiredChunkOrigins.IsEmpty())
	{
		return false;
	}

	for (const FIntVector& RequiredChunkOrigin : RequiredChunkOrigins)
	{
		if (!ObservedChunkOrigins.Contains(RequiredChunkOrigin))
		{
			return false;
		}
	}

	return true;
}
