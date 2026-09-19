// Copyright 2026 Spotted Loaf Studio

#include "Layout/Terrain/LayoutTerrainOperationBuilder.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Runtime/LayoutCellWorldTransform.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"

namespace LayoutTerrainOperationBuilder
{
	void SortWrites(TArray<FLayoutFrozenTerrainWriteRecord>& InOutWrites)
	{
		InOutWrites.Sort([](const FLayoutFrozenTerrainWriteRecord& Left, const FLayoutFrozenTerrainWriteRecord& Right)
		{
			if (Left.BlockWorldPos.Z != Right.BlockWorldPos.Z) return Left.BlockWorldPos.Z < Right.BlockWorldPos.Z;
			if (Left.BlockWorldPos.Y != Right.BlockWorldPos.Y) return Left.BlockWorldPos.Y < Right.BlockWorldPos.Y;
			return Left.BlockWorldPos.X < Right.BlockWorldPos.X;
		});
	}

	void BuildOrdinaryRootWrites(
		FLayoutFrozenTerrainContract& FrozenTerrainContract,
		const TConstArrayView<FLayoutFrozenTerrainVoidIntervalSample> SelectedIntervals,
		const TConstArrayView<FLayoutTerrainSurfaceSample> PerimeterSurfaceSamples,
		const int32 StructuralAlignmentLevel,
		const int32 TemplatePlacementZOffsetBlocks,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		TArray<FLayoutFrozenTerrainWriteRecord>& OutWrites)
	{
		TMap<FIntVector, FLayoutFrozenTerrainWriteRecord> WritesByPosition;
		TMap<FIntPoint, const FLayoutFrozenTerrainVoidIntervalSample*> SelectedIntervalByXY;
		for (const FLayoutFrozenTerrainVoidIntervalSample& Interval : SelectedIntervals)
		{
			if (Interval.bHasVoidEvidence)
			{
				SelectedIntervalByXY.Add(Interval.BlockXY, &Interval);
			}
		}
		TMap<FIntPoint, const FLayoutTerrainSurfaceSample*> PerimeterSurfaceByXY;
		for (const FLayoutTerrainSurfaceSample& Sample : PerimeterSurfaceSamples)
		{
			PerimeterSurfaceByXY.Add(Sample.BlockXY, &Sample);
		}
		const int32 MaxRampRunBlocks = FMath::Max(0, TerrainTransitionPolicy.MaxFoundationDepth);
		int32 MaxCellX = 0;
		int32 MaxCellY = 0;
		TMap<FIntPoint, int32> LowestCellZByColumn;
		for (const FLayoutTerrainCellContractRecord& CellContract : FrozenTerrainContract.CellContracts)
		{
			MaxCellX = FMath::Max(MaxCellX, CellContract.Cell.X);
			MaxCellY = FMath::Max(MaxCellY, CellContract.Cell.Y);
			int32& LowestZ = LowestCellZByColumn.FindOrAdd(FIntPoint(CellContract.Cell.X, CellContract.Cell.Y), CellContract.Cell.Z);
			LowestZ = FMath::Min(LowestZ, CellContract.Cell.Z);
		}
		const auto IsConcreteFoundationMaterial = [](const int32 Material)
		{
			return Material != EmptyMaterial && Material != DefaultMaterial;
		};
		const auto ResolveNearestFoundationMaterial = [&FrozenTerrainContract, &IsConcreteFoundationMaterial](
			const FLayoutTerrainCellContractRecord& SourceCell)
		{
			if (IsConcreteFoundationMaterial(SourceCell.FoundationMaterial))
			{
				return SourceCell.FoundationMaterial;
			}
			int32 BestMaterial = DefaultMaterial;
			int32 BestDistance = MAX_int32;
			FIntVector BestCell(MAX_int32);
			for (const FLayoutTerrainCellContractRecord& Candidate : FrozenTerrainContract.CellContracts)
			{
				if (!Candidate.bHasFoundationFillEvidence
					|| !IsConcreteFoundationMaterial(Candidate.FoundationMaterial))
				{
					continue;
				}
				const int32 Distance = FMath::Abs(Candidate.Cell.X - SourceCell.Cell.X)
					+ FMath::Abs(Candidate.Cell.Y - SourceCell.Cell.Y);
				if (Distance < BestDistance
					|| (Distance == BestDistance && Candidate.Cell.Z < BestCell.Z)
					|| (Distance == BestDistance && Candidate.Cell.Z == BestCell.Z && Candidate.Cell.Y < BestCell.Y)
					|| (Distance == BestDistance && Candidate.Cell.Z == BestCell.Z && Candidate.Cell.Y == BestCell.Y && Candidate.Cell.X < BestCell.X))
				{
					BestDistance = Distance;
					BestCell = Candidate.Cell;
					BestMaterial = Candidate.FoundationMaterial;
				}
			}
			return BestMaterial;
		};
		TMap<FIntPoint, int32> NearestFloorMaterialByXY;
		const auto ResolveNearestFloorMaterial = [&SelectedIntervals, &NearestFloorMaterialByXY, &IsConcreteFoundationMaterial](
			const FIntPoint BlockXY)
		{
			if (const int32* const CachedMaterial = NearestFloorMaterialByXY.Find(BlockXY))
			{
				return *CachedMaterial;
			}
			int32 BestMaterial = DefaultMaterial;
			int32 BestDistance = MAX_int32;
			FIntPoint BestXY(MAX_int32);
			for (const FLayoutFrozenTerrainVoidIntervalSample& Candidate : SelectedIntervals)
			{
				if (!Candidate.bHasFloorMaterialIndex
					|| !IsConcreteFoundationMaterial(Candidate.FloorMaterialIndex))
				{
					continue;
				}
				const int32 Distance = FMath::Abs(Candidate.BlockXY.X - BlockXY.X)
					+ FMath::Abs(Candidate.BlockXY.Y - BlockXY.Y);
				if (Distance < BestDistance
					|| (Distance == BestDistance && Candidate.BlockXY.Y < BestXY.Y)
					|| (Distance == BestDistance && Candidate.BlockXY.Y == BestXY.Y && Candidate.BlockXY.X < BestXY.X))
				{
					BestDistance = Distance;
					BestXY = Candidate.BlockXY;
					BestMaterial = Candidate.FloorMaterialIndex;
				}
			}
			NearestFloorMaterialByXY.Add(BlockXY, BestMaterial);
			return BestMaterial;
		};
		for (FLayoutTerrainCellContractRecord& CellContract : FrozenTerrainContract.CellContracts)
		{
			// Pre-snap flags/depths cannot authorize writes at a different accepted base.
			// Upper floors must not fill the building interior or emit stacked perimeter ramps.
			CellContract.bHasFoundationFillEvidence = false;
			CellContract.RequiredFoundationDepth = 0;
			CellContract.bHasRampTransitionEvidence = false;
			if (CellContract.Cell.Z != LowestCellZByColumn.FindChecked(FIntPoint(CellContract.Cell.X, CellContract.Cell.Y))
				|| CellContract.Contract == ELayoutFrozenTerrainCellContract::Removed
				|| CellContract.Contract == ELayoutFrozenTerrainCellContract::Forbidden
				|| CellContract.Contract == ELayoutFrozenTerrainCellContract::BridgeSpan)
			{
				continue;
			}
			const FIntVector CellMin = LayoutCellWorldTransform::ResolveAcceptedCellBase(
				FrozenTerrainContract,
				CellContract.Cell,
				StructuralAlignmentLevel,
				TemplatePlacementZOffsetBlocks);
			if (TerrainTransitionPolicy.bAllowFoundationFill && MaxRampRunBlocks > 0)
			{
				const int32 CellFoundationMaterial = ResolveNearestFoundationMaterial(CellContract);
				int32 PositionCount = 0;
				for (int32 LocalY = 0; LocalY < FrozenTerrainContract.SharedCellSizeInBlocks.Y; ++LocalY)
				{
					for (int32 LocalX = 0; LocalX < FrozenTerrainContract.SharedCellSizeInBlocks.X; ++LocalX)
					{
						const FIntPoint BlockXY(CellMin.X + LocalX, CellMin.Y + LocalY);
						const FLayoutFrozenTerrainVoidIntervalSample* const Interval = SelectedIntervalByXY.FindRef(BlockXY);
						if (Interval == nullptr || CellMin.Z <= Interval->MinZ || CellMin.Z - 1 > Interval->MaxZ)
						{
							continue;
						}
						const int32 FillMinZ = FMath::Max(Interval->MinZ, CellMin.Z - MaxRampRunBlocks);
						CellContract.RequiredFoundationDepth = FMath::Max(CellContract.RequiredFoundationDepth, CellMin.Z - FillMinZ);
						CellContract.bHasFoundationFillEvidence = true;
						CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
						const int32 FoundationMaterial = Interval->bHasFloorMaterialIndex
							? Interval->FloorMaterialIndex
							: (IsConcreteFoundationMaterial(CellFoundationMaterial)
								? CellFoundationMaterial : ResolveNearestFloorMaterial(BlockXY));
						for (int32 Z = FillMinZ; Z < CellMin.Z; ++Z)
						{
							const FIntVector BlockWorldPos(BlockXY.X, BlockXY.Y, Z);
							FLayoutFrozenTerrainWriteRecord& Write = WritesByPosition.FindOrAdd(BlockWorldPos);
							Write.BlockWorldPos = BlockWorldPos;
							Write.Material = FoundationMaterial;
							Write.bResolveMaterialFromTerrain = true;
							Write.MaterialSourceBlockWorldPos = FIntVector(BlockXY.X, BlockXY.Y, Interval->MinZ - 1);
							Write.MaterialSourceSearchDepthBlocks = MaxRampRunBlocks;
							Write.SourceContract = CellContract.Contract;
							++PositionCount;
						}
					}
				}
				INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainFoundationWritePositions, static_cast<uint32>(PositionCount));
			}

			if (!TerrainTransitionPolicy.bAllowPerimeterRampTransition || MaxRampRunBlocks <= 0)
			{
				continue;
			}
			// Ramp target is layout base before template placement offset. Templates may move
			// independently without moving terrain join height.
			const FIntVector RampCellBase = LayoutCellWorldTransform::ResolveAcceptedCellBase(
				FrozenTerrainContract,
				CellContract.Cell,
				StructuralAlignmentLevel,
				0);
			const int32 CellFoundationMaterial = ResolveNearestFoundationMaterial(CellContract);
			int32 RampWriteCount = 0;
			const auto EmitRampColumnWrites = [&](
				const FIntPoint& BlockXY,
				const int32 ExistingSurfaceZ,
				const int32 DesiredSurfaceZ,
				const bool bFill,
				const FLayoutTerrainSurfaceSample* SurfaceSample,
				const FLayoutFrozenTerrainVoidIntervalSample* Interval)
			{
				if (bFill)
				{
					const int32 Material = SurfaceSample != nullptr
						&& IsConcreteFoundationMaterial(SurfaceSample->SurfaceMaterialIndex)
							? SurfaceSample->SurfaceMaterialIndex
							: (Interval != nullptr && Interval->bHasFloorMaterialIndex
								? Interval->FloorMaterialIndex
								: (IsConcreteFoundationMaterial(CellFoundationMaterial)
									? CellFoundationMaterial
									: ResolveNearestFloorMaterial(BlockXY)));
					CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
					for (int32 Z = ExistingSurfaceZ + 1; Z <= DesiredSurfaceZ; ++Z)
					{
						FLayoutFrozenTerrainWriteRecord& Write = WritesByPosition.FindOrAdd(FIntVector(BlockXY.X, BlockXY.Y, Z));
						Write.BlockWorldPos = FIntVector(BlockXY.X, BlockXY.Y, Z);
						Write.Material = Material;
						Write.bResolveMaterialFromTerrain = true;
						Write.MaterialSourceBlockWorldPos = FIntVector(BlockXY.X, BlockXY.Y, ExistingSurfaceZ);
						Write.MaterialSourceSearchDepthBlocks = MaxRampRunBlocks;
						Write.SourceContract = CellContract.Contract;
						++RampWriteCount;
					}
					return;
				}
				const int32 ClearMinZ = SurfaceSample != nullptr && SurfaceSample->bHasSolidRunBounds
					? FMath::Max(DesiredSurfaceZ + 1, SurfaceSample->SolidRunMinZ)
					: DesiredSurfaceZ + 1;
				const int32 ClearMaxZ = SurfaceSample != nullptr && SurfaceSample->bHasSolidRunBounds
					? SurfaceSample->SolidRunMaxZ
					: ExistingSurfaceZ;
				if (ClearMinZ <= ClearMaxZ) CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
				for (int32 Z = ClearMinZ; Z <= ClearMaxZ; ++Z)
				{
					FLayoutFrozenTerrainWriteRecord& Write = WritesByPosition.FindOrAdd(FIntVector(BlockXY.X, BlockXY.Y, Z));
					Write.BlockWorldPos = FIntVector(BlockXY.X, BlockXY.Y, Z);
					Write.Material = EmptyMaterial;
					Write.bResolveMaterialFromTerrain = false;
					Write.MaterialSourceBlockWorldPos = FIntVector::ZeroValue;
					Write.MaterialSourceSearchDepthBlocks = 0;
					Write.SourceContract = CellContract.Contract;
					++RampWriteCount;
				}
			};
			const auto AddRampFace = [&](const FIntPoint Direction)
			{
				const int32 AxisSize = Direction.X != 0
					? FrozenTerrainContract.SharedCellSizeInBlocks.X
					: FrozenTerrainContract.SharedCellSizeInBlocks.Y;
				const int32 TangentSize = Direction.X != 0
					? FrozenTerrainContract.SharedCellSizeInBlocks.Y
					: FrozenTerrainContract.SharedCellSizeInBlocks.X;
				const int32 RampRun = FMath::Min(MaxRampRunBlocks, AxisSize);
				for (int32 Tangent = 0; Tangent < TangentSize; ++Tangent)
				{
					int32 RampSlopeDirection = 0;
					for (int32 Distance = 1; Distance <= RampRun; ++Distance)
					{
						const FIntPoint BlockXY(
							Direction.X < 0 ? RampCellBase.X - Distance : Direction.X > 0 ? RampCellBase.X + FrozenTerrainContract.SharedCellSizeInBlocks.X - 1 + Distance : RampCellBase.X + Tangent,
							Direction.Y < 0 ? RampCellBase.Y - Distance : Direction.Y > 0 ? RampCellBase.Y + FrozenTerrainContract.SharedCellSizeInBlocks.Y - 1 + Distance : RampCellBase.Y + Tangent);
						const FLayoutTerrainSurfaceSample* const SurfaceSample = PerimeterSurfaceByXY.FindRef(BlockXY);
						const FLayoutFrozenTerrainVoidIntervalSample* const Interval = SelectedIntervalByXY.FindRef(BlockXY);
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
						if (RampSlopeDirection == 0)
						{
							RampSlopeDirection = ExistingSurfaceZ < RampCellBase.Z - 1 ? -1
								: ExistingSurfaceZ > RampCellBase.Z - 1 ? 1 : 0;
							if (RampSlopeDirection == 0)
							{
								break;
							}
						}
						const int32 DesiredSurfaceZ = RampSlopeDirection < 0
							? RampCellBase.Z - Distance
							: RampCellBase.Z + Distance - 2;
						if ((RampSlopeDirection < 0 && ExistingSurfaceZ >= DesiredSurfaceZ)
							|| (RampSlopeDirection > 0 && ExistingSurfaceZ <= DesiredSurfaceZ))
						{
							break;
						}
						const int32 SurfaceDelta = FMath::Abs(ExistingSurfaceZ - DesiredSurfaceZ);
						if (SurfaceDelta > MaxRampRunBlocks)
						{
							break;
						}
						EmitRampColumnWrites(
							BlockXY,
							ExistingSurfaceZ,
							DesiredSurfaceZ,
							RampSlopeDirection < 0,
							SurfaceSample,
							Interval);
					}
				}
			};
			const auto AddRampCorner = [&](const FIntPoint Direction)
			{
				const int32 RampRun = FMath::Min(
					MaxRampRunBlocks,
					FMath::Min(
						FrozenTerrainContract.SharedCellSizeInBlocks.X,
						FrozenTerrainContract.SharedCellSizeInBlocks.Y));
				for (int32 OffsetY = 1; OffsetY <= RampRun; ++OffsetY)
				{
					for (int32 OffsetX = 1; OffsetX <= RampRun; ++OffsetX)
					{
						const int32 Distance = FMath::Max(OffsetX, OffsetY);
						const FIntPoint BlockXY(
							Direction.X < 0 ? RampCellBase.X - OffsetX : RampCellBase.X + FrozenTerrainContract.SharedCellSizeInBlocks.X - 1 + OffsetX,
							Direction.Y < 0 ? RampCellBase.Y - OffsetY : RampCellBase.Y + FrozenTerrainContract.SharedCellSizeInBlocks.Y - 1 + OffsetY);
						const FLayoutTerrainSurfaceSample* const SurfaceSample = PerimeterSurfaceByXY.FindRef(BlockXY);
						const FLayoutFrozenTerrainVoidIntervalSample* const Interval = SelectedIntervalByXY.FindRef(BlockXY);
						if ((SurfaceSample != nullptr && !SurfaceSample->bIsValid)
							|| (SurfaceSample == nullptr && Interval == nullptr))
						{
							continue;
						}
						const int32 ExistingSurfaceZ = SurfaceSample != nullptr
							? SurfaceSample->SurfaceBlockWorldPos.Z
							: Interval->MinZ - 1;
						const int32 ImmediateJoinZ = RampCellBase.Z - 1;
						const bool bFill = ExistingSurfaceZ < ImmediateJoinZ;
						const bool bExcavate = ExistingSurfaceZ > ImmediateJoinZ;
						if (!bFill && !bExcavate)
						{
							continue;
						}
						const int32 DesiredSurfaceZ = bFill
							? RampCellBase.Z - Distance
							: RampCellBase.Z + Distance - 2;
						if ((bFill && ExistingSurfaceZ >= DesiredSurfaceZ)
							|| (bExcavate && ExistingSurfaceZ <= DesiredSurfaceZ)
							|| FMath::Abs(ExistingSurfaceZ - DesiredSurfaceZ) > MaxRampRunBlocks)
						{
							continue;
						}
						EmitRampColumnWrites(
							BlockXY,
							ExistingSurfaceZ,
							DesiredSurfaceZ,
							bFill,
							SurfaceSample,
							Interval);
					}
				}
			};
			if (CellContract.Cell.X == 0) { AddRampFace(FIntPoint(-1, 0)); }
			if (CellContract.Cell.X == MaxCellX) { AddRampFace(FIntPoint(1, 0)); }
			if (CellContract.Cell.Y == 0) { AddRampFace(FIntPoint(0, -1)); }
			if (CellContract.Cell.Y == MaxCellY) { AddRampFace(FIntPoint(0, 1)); }
			if (CellContract.Cell.X == 0 && CellContract.Cell.Y == 0) { AddRampCorner(FIntPoint(-1, -1)); }
			if (CellContract.Cell.X == 0 && CellContract.Cell.Y == MaxCellY) { AddRampCorner(FIntPoint(-1, 1)); }
			if (CellContract.Cell.X == MaxCellX && CellContract.Cell.Y == 0) { AddRampCorner(FIntPoint(1, -1)); }
			if (CellContract.Cell.X == MaxCellX && CellContract.Cell.Y == MaxCellY) { AddRampCorner(FIntPoint(1, 1)); }
			CellContract.bHasRampTransitionEvidence = RampWriteCount > 0;
			INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainEntryHaloWritePositions, static_cast<uint32>(RampWriteCount));
		}
		WritesByPosition.GenerateValueArray(OutWrites);
		SortWrites(OutWrites);
		const auto* Ledger = LayoutSolveExecution::CurrentThreadLedger();
		if (Ledger != nullptr && !Ledger->DiagnosticContext.IsEmpty())
		{
			int32 MinBaseZ = MAX_int32, MaxBaseZ = MIN_int32, MinVoidZ = MAX_int32, MaxVoidZ = MIN_int32;
			for (const auto& Cell : FrozenTerrainContract.CellContracts)
			{
				const int32 Z = LayoutCellWorldTransform::ResolveAcceptedCellBase(FrozenTerrainContract, Cell.Cell, StructuralAlignmentLevel, TemplatePlacementZOffsetBlocks).Z;
				MinBaseZ = FMath::Min(MinBaseZ, Z);
				MaxBaseZ = FMath::Max(MaxBaseZ, Z);
			}
			for (const auto& Interval : SelectedIntervals)
			{
				MinVoidZ = FMath::Min(MinVoidZ, Interval.MinZ);
				MaxVoidZ = FMath::Max(MaxVoidZ, Interval.MinZ);
			}
			UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s event=terrain-writes fill=%d ramps=%d maxDepth=%d offset=%d cells=%d intervals=%d validIntervals=%d perimeter=%d baseZ=[%d,%d] voidMinZ=[%d,%d] writes=%d"),
				*Ledger->DiagnosticContext, TerrainTransitionPolicy.bAllowFoundationFill, TerrainTransitionPolicy.bAllowPerimeterRampTransition,
				MaxRampRunBlocks, TemplatePlacementZOffsetBlocks, FrozenTerrainContract.CellContracts.Num(), SelectedIntervals.Num(),
				SelectedIntervalByXY.Num(), PerimeterSurfaceSamples.Num(), MinBaseZ, MaxBaseZ, MinVoidZ, MaxVoidZ, OutWrites.Num());
		}
	}
}
