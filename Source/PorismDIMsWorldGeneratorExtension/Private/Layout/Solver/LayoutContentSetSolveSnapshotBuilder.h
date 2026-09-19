// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"
#include "Templates/Function.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;

namespace LayoutContentSetSolveSnapshotBuilder
{
	/** Callback used to lazily compile a child request template for one child-profile-backed content entry. */
	using FBuildChildRequestTemplateFn = TFunctionRef<TSharedPtr<FLayoutChildRequestTemplateSnapshot>(const ULayoutProfileAsset*)>;

	/**
	 * Builds one immutable unified content-set snapshot from the authored content-set surface while
	 * preserving the frozen content-entry carrier and child-template merge behavior.
	 */
	FLayoutRegionContentSetSolveSnapshot BuildContentSetSnapshotFromContentSet(
		int32 SnapshotSchemaVersion,
		const ULayoutRegionContentSetAsset* ContentSet,
		FBuildChildRequestTemplateFn BuildChildRequestTemplate,
		const FIntVector* SharedCellSizeOverride = nullptr);

	/**
	 * Builds one immutable module-solve snapshot from the unified content-set surface.
	 */
	FLayoutModuleCatalog BuildModuleCatalogFromContentSet(
		int32 SnapshotSchemaVersion,
		const ULayoutRegionContentSetAsset* ContentSet,
		const FIntVector* SharedCellSizeOverride = nullptr);
}
