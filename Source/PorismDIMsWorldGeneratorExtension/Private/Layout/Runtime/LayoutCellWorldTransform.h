// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Contracts/LayoutContractTypes.h"

/** Shared world-space transform for finalized layout cells and terrain operations. */
namespace LayoutCellWorldTransform
{
	/** Resolves template and terrain-operation base from one finalized cell contract. */
	inline FIntVector ResolveAcceptedCellBase(
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		const FIntVector& Cell,
		const int32 StructuralAlignmentLevel,
		const int32 TemplatePlacementZOffsetBlocks)
	{
		int32 BaseZ = FrozenTerrainContract.FootprintMinBlockWorldPos.Z;
		for (const FLayoutFrozenTerrainStageCellRecord& Stage : FrozenTerrainContract.StageMap)
		{
			if (Stage.FootprintCellXY == FIntPoint(Cell.X, Cell.Y))
			{
				BaseZ = Stage.ResolvedStageBaseBlockWorldZ;
				break;
			}
		}
		return FIntVector(
			FrozenTerrainContract.FootprintMinBlockWorldPos.X
				+ Cell.X * FrozenTerrainContract.SharedCellSizeInBlocks.X,
			FrozenTerrainContract.FootprintMinBlockWorldPos.Y
				+ Cell.Y * FrozenTerrainContract.SharedCellSizeInBlocks.Y,
			BaseZ
				+ (Cell.Z - StructuralAlignmentLevel) * FrozenTerrainContract.SharedCellSizeInBlocks.Z
				+ TemplatePlacementZOffsetBlocks);
	}
}
