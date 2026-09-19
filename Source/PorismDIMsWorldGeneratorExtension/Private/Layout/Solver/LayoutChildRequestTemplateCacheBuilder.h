// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"
#include "Templates/Function.h"

class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;

namespace LayoutChildRequestTemplateCacheBuilder
{
	/** Mutable recursion/cycle-detection state used while compiling snapshot-only child request templates. */
	struct FRecursiveChildTemplateBuildState
	{
		TSet<const ULayoutProfileAsset*> ActiveChildProfiles;
		TMap<const ULayoutProfileAsset*, TSharedPtr<FLayoutChildRequestTemplateSnapshot>> ChildRequestTemplatesByProfile;
	};

	/** Callback used to lazily compile one child profile snapshot while preserving recursive state. */
	using FBuildChildProfileSnapshotFn = TFunctionRef<FLayoutProfileSolveSnapshot(const ULayoutProfileAsset*)>;

	/** Callback used to lazily compile one child content-set snapshot while preserving recursive state. */
	using FBuildChildContentSetSnapshotFn = TFunctionRef<FLayoutRegionContentSetSolveSnapshot(const ULayoutRegionContentSetAsset*)>;

	/** Callback used to compile the legacy module-solve snapshot for one child content set. */
	using FBuildChildModuleCatalogFn = TFunctionRef<FLayoutModuleCatalog(const ULayoutRegionContentSetAsset*)>;

	/**
	 * Builds or reuses one cached immutable child-request template while preserving recursive cycle
	 * detection and deterministic snapshot reuse across repeated child profile references.
	 */
	TSharedPtr<FLayoutChildRequestTemplateSnapshot> BuildChildRequestTemplateSnapshot(
		const ULayoutProfileAsset* Profile,
		FRecursiveChildTemplateBuildState& State,
		int32 SnapshotSchemaVersion,
		FBuildChildProfileSnapshotFn BuildProfileSnapshot,
		FBuildChildContentSetSnapshotFn BuildContentSetSnapshot,
		FBuildChildModuleCatalogFn BuildModuleCatalog);
}
