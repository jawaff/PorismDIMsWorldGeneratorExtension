// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

class ULayoutCompositeModuleAsset;

namespace LayoutCompositeModuleSnapshotBuilder
{
	/** Returns the stable cell ordering used when compiling deterministic composite snapshots. */
	bool IsLexicographicallyEarlierCompositeCell(const FIntVector& Left, const FIntVector& Right);

	/** Lowers one validated composite into the shared per-cell local-shape carrier used by later solver stages. */
	void BuildCompositeLocalShapeContracts(FLayoutModuleSolveSnapshot& Snapshot, const ULayoutCompositeModuleAsset* Composite);

	/**
	 * Builds one immutable composite-module snapshot from the authored composite surface while preserving
	 * the frozen public snapshot carrier and proof/assertion behavior.
	 */
	FLayoutModuleSolveSnapshot BuildCompositeModuleSnapshotFromAsset(
		int32 SnapshotSchemaVersion,
		const ULayoutCompositeModuleAsset* Composite,
		int32 Weight,
		ELayoutPlacementZone PlacementZone,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		int32 SpecificLevel,
		bool bOptional,
		const FIntVector& EstablishedSharedCellSizeInBlocks);
}
