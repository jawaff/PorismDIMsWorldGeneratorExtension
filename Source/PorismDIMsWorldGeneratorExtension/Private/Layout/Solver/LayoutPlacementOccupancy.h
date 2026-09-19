// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutPlacementOccupancy
{
	/** Resolves occupied local cells from solved carrier data before falling back to live asset contracts. */
	TArray<FIntVector> ResolveOccupiedLocalCells(
		const FLayoutPlacedModule& Placement,
		const FLayoutModuleSolveSnapshot* ModuleSnapshot);

	/** Builds the minimal occupied-footprint bounds that still contain every occupied local cell. */
	FIntVector BuildOccupiedLocalCellBounds(const TArray<FIntVector>& OccupiedLocalCells);

	/** Projects one occupied local cell into world space using the placement yaw and occupied-footprint basis. */
	FIntVector ProjectOccupiedLocalCellToWorld(
		const FLayoutPlacedModule& Placement,
		const FIntVector& LocalCell,
		const FIntVector& OccupiedBoundsCells);

	/** Projects one placed result into the world cells it actually occupies. */
	TArray<FIntVector> BuildWorldOccupiedCells(
		const FLayoutPlacedModule& Placement,
		const FLayoutModuleSolveSnapshot* ModuleSnapshot);

	/** Resolves snapshot-owned occupied local cells for candidate projection on the live root solve path. */
	TArray<FIntVector> ResolveSnapshotOccupiedLocalCells(const FLayoutModuleSolveSnapshot* ModuleSnapshot);

	/** Projects one local cell into world space using an explicit rotation-footprint basis. */
	FIntVector ProjectLocalCellToWorld(
		const FIntVector& RootCell,
		const FIntVector& LocalCell,
		const FIntVector& RotationFootprintBoundsCells,
		const int32 YawRotationSteps);

	/** Projects one snapshot-backed candidate bundle into the world cells it would occupy. */
	TArray<FIntVector> BuildSnapshotWorldOccupiedCells(
		const FLayoutModuleSolveSnapshot* ModuleSnapshot,
		const FIntVector& RootCell,
		const int32 YawRotationSteps);
}
