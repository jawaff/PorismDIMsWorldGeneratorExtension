// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"
#include "Templates/Function.h"

struct FLayoutRegionContentEntry;
class ULayoutProfileAsset;

namespace LayoutContentSetEntrySnapshotBuilder
{
	/** Callback used to lazily compile a child request template for one child-profile-backed content entry. */
	using FBuildChildRequestTemplateFn = TFunctionRef<TSharedPtr<FLayoutChildRequestTemplateSnapshot>(const ULayoutProfileAsset*)>;

	/**
	 * Appends one immutable content-entry snapshot to the supplied content-set snapshot and performs the
	 * child-template merge/proof forwarding needed for child-profile-backed entries.
	 */
	void AppendContentSetEntrySnapshot(
		FLayoutRegionContentSetSolveSnapshot& Snapshot,
		const FLayoutRegionContentEntry& Entry,
		int32& InOutModuleEntryCount,
		FBuildChildRequestTemplateFn BuildChildRequestTemplate);
}
