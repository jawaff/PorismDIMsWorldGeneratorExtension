// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutPlacementOccupancy.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"

namespace
{
	FIntVector RotateOccupiedLocalCellInFootprintYaw(
		const FIntVector& LocalCell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		const int32 NormalizedYaw = ((YawRotationSteps % 4) + 4) % 4;
		switch (NormalizedYaw)
		{
		case 1:
			return FIntVector(FootprintSize.Y - 1 - LocalCell.Y, LocalCell.X, LocalCell.Z);
		case 2:
			return FIntVector(FootprintSize.X - 1 - LocalCell.X, FootprintSize.Y - 1 - LocalCell.Y, LocalCell.Z);
		case 3:
			return FIntVector(LocalCell.Y, FootprintSize.X - 1 - LocalCell.X, LocalCell.Z);
		default:
			return LocalCell;
		}
	}
}

TArray<FIntVector> LayoutPlacementOccupancy::ResolveOccupiedLocalCells(
	const FLayoutPlacedModule& Placement,
	const FLayoutModuleSolveSnapshot* ModuleSnapshot)
{
	if (!Placement.OccupiedLocalCells.IsEmpty())
	{
		return Placement.OccupiedLocalCells;
	}

	if (ModuleSnapshot != nullptr && !ModuleSnapshot->OccupiedLocalCells.IsEmpty())
	{
		return ModuleSnapshot->OccupiedLocalCells;
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

FIntVector LayoutPlacementOccupancy::BuildOccupiedLocalCellBounds(const TArray<FIntVector>& OccupiedLocalCells)
{
	if (OccupiedLocalCells.IsEmpty())
	{
		return FIntVector::ZeroValue;
	}

	FIntVector MinCell = OccupiedLocalCells[0];
	FIntVector MaxCell = OccupiedLocalCells[0];
	for (const FIntVector& LocalCell : OccupiedLocalCells)
	{
		MinCell.X = FMath::Min(MinCell.X, LocalCell.X);
		MinCell.Y = FMath::Min(MinCell.Y, LocalCell.Y);
		MinCell.Z = FMath::Min(MinCell.Z, LocalCell.Z);
		MaxCell.X = FMath::Max(MaxCell.X, LocalCell.X);
		MaxCell.Y = FMath::Max(MaxCell.Y, LocalCell.Y);
		MaxCell.Z = FMath::Max(MaxCell.Z, LocalCell.Z);
	}

	return FIntVector(
		FMath::Max(1, MaxCell.X - MinCell.X + 1),
		FMath::Max(1, MaxCell.Y - MinCell.Y + 1),
		FMath::Max(1, MaxCell.Z - MinCell.Z + 1));
}

FIntVector LayoutPlacementOccupancy::ProjectOccupiedLocalCellToWorld(
	const FLayoutPlacedModule& Placement,
	const FIntVector& LocalCell,
	const FIntVector& OccupiedBoundsCells)
{
	return ProjectLocalCellToWorld(Placement.Cell, LocalCell, OccupiedBoundsCells, Placement.YawRotationSteps);
}

TArray<FIntVector> LayoutPlacementOccupancy::BuildWorldOccupiedCells(
	const FLayoutPlacedModule& Placement,
	const FLayoutModuleSolveSnapshot* ModuleSnapshot)
{
	const TArray<FIntVector> OccupiedLocalCells = ResolveOccupiedLocalCells(Placement, ModuleSnapshot);
	if (OccupiedLocalCells.IsEmpty())
	{
		return {Placement.Cell};
	}

	const FIntVector OccupiedBoundsCells = BuildOccupiedLocalCellBounds(OccupiedLocalCells);
	TArray<FIntVector> WorldCells;
	WorldCells.Reserve(OccupiedLocalCells.Num());
	for (const FIntVector& LocalCell : OccupiedLocalCells)
	{
		WorldCells.Add(ProjectOccupiedLocalCellToWorld(Placement, LocalCell, OccupiedBoundsCells));
	}

	return WorldCells;
}

TArray<FIntVector> LayoutPlacementOccupancy::ResolveSnapshotOccupiedLocalCells(
	const FLayoutModuleSolveSnapshot* ModuleSnapshot)
{
	if (ModuleSnapshot == nullptr)
	{
		return {};
	}

	return !ModuleSnapshot->OccupiedLocalCells.IsEmpty()
		? ModuleSnapshot->OccupiedLocalCells
		: TArray<FIntVector>();
}

FIntVector LayoutPlacementOccupancy::ProjectLocalCellToWorld(
	const FIntVector& RootCell,
	const FIntVector& LocalCell,
	const FIntVector& RotationFootprintBoundsCells,
	const int32 YawRotationSteps)
{
	const FIntPoint RotationFootprint(
		FMath::Max(1, RotationFootprintBoundsCells.X),
		FMath::Max(1, RotationFootprintBoundsCells.Y));
	return RootCell + RotateOccupiedLocalCellInFootprintYaw(LocalCell, RotationFootprint, YawRotationSteps);
}

TArray<FIntVector> LayoutPlacementOccupancy::BuildSnapshotWorldOccupiedCells(
	const FLayoutModuleSolveSnapshot* ModuleSnapshot,
	const FIntVector& RootCell,
	const int32 YawRotationSteps)
{
	const TArray<FIntVector> OccupiedLocalCells = ResolveSnapshotOccupiedLocalCells(ModuleSnapshot);
	if (OccupiedLocalCells.IsEmpty())
	{
		return {RootCell};
	}

	TArray<FIntVector> WorldCells;
	WorldCells.Reserve(OccupiedLocalCells.Num());
	for (const FIntVector& LocalCell : OccupiedLocalCells)
	{
		WorldCells.Add(ProjectLocalCellToWorld(
			RootCell,
			LocalCell,
			ModuleSnapshot->BoundsCells,
			YawRotationSteps));
	}

	return WorldCells;
}
