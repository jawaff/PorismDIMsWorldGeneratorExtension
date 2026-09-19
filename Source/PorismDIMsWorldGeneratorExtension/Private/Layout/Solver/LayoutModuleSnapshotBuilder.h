// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

class ULayoutModuleAsset;

namespace LayoutModuleSnapshotBuilder
{
	/** Copies the effective leaf-module contract into the shared immutable snapshot carrier before later snapshot-stage derivation runs. */
	void PopulateLeafModuleSnapshotBase(
		FLayoutModuleSolveSnapshot& Snapshot,
		const ULayoutModuleAsset* Module,
		int32 Weight,
		ELayoutPlacementZone PlacementZone,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		int32 SpecificLevel,
		bool bOptional,
		const FIntVector& EstablishedSharedCellSizeInBlocks);

	/**
	 * Builds one immutable leaf-module snapshot from the authored module surface while preserving the
	 * frozen public snapshot carrier and proof/assertion behavior.
	 */
	FLayoutModuleSolveSnapshot BuildLeafModuleSnapshotFromAsset(
		int32 SnapshotSchemaVersion,
		const ULayoutModuleAsset* Module,
		int32 Weight,
		ELayoutPlacementZone PlacementZone,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		int32 SpecificLevel,
		bool bOptional,
		const FIntVector& EstablishedSharedCellSizeInBlocks);
}
