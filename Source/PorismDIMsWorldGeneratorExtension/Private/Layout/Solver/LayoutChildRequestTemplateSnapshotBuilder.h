// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutChildRequestTemplateSnapshotBuilder
{
	/**
	 * Copies the already-compiled child profile/content/module snapshots into one immutable child-request
	 * template base before later recursive caching/invocation code runs.
	 */
	void PopulateChildRequestTemplateSnapshotBase(
		FLayoutChildRequestTemplateSnapshot& Template,
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog);

	/**
	 * Attaches one compiled child-request template to a unified content-entry snapshot and forwards the
	 * proof/assertion surface onto the parent content-set snapshot.
	 */
	void FinalizeChildRequestTemplateForContentEntry(
		FLayoutRegionContentSetSolveSnapshot& Snapshot,
		FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const TSharedPtr<FLayoutChildRequestTemplateSnapshot>& CompiledChildRequestTemplate);
}
