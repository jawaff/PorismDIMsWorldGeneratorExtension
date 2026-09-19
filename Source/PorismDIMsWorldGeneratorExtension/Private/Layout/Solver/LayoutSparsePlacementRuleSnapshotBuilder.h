// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

struct FLayoutSparsePlacementRuleBase;

namespace LayoutSparsePlacementRuleSnapshotBuilder
{
	/**
	 * Copies one sparse-placement rule plus its already-compiled content/module snapshots into the
	 * immutable sparse-rule snapshot base before later proof/assertion finalization runs.
	 */
	void PopulateSparsePlacementRuleSnapshotBase(
		FLayoutSparsePlacementRuleSolveSnapshot& Snapshot,
		const FLayoutSparsePlacementRuleBase& Rule,
		ELayoutSparsePlacementRuleKind RuleKind,
		ELayoutSparseCandidateSource CandidateSource,
		int32 Count,
		int32 MinCount,
		int32 MaxCount,
		int32 MinSpacingCells,
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog);
}
