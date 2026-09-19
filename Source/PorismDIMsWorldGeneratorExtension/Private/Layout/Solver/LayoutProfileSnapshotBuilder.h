// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"
#include "Templates/Function.h"

class ULayoutProfileAsset;
struct FInstancedStruct;

namespace LayoutProfileSnapshotBuilder
{
	/** Callback used to lazily compile one sparse placement-rule snapshot while preserving caller-owned recursive state. */
	using FBuildSparsePlacementRuleSnapshotFn = TFunctionRef<FLayoutSparsePlacementRuleSolveSnapshot(const FInstancedStruct&)>;

	/**
	 * Copies one profile solve contract plus its already-compiled sparse placement rules into the
	 * immutable profile snapshot base before later proof/assertion finalization runs.
	 */
	void PopulateProfileSnapshotBase(
		FLayoutProfileSolveSnapshot& Snapshot,
		const ULayoutProfileAsset& Profile,
		const TArray<FLayoutSparsePlacementRuleSolveSnapshot>& SparsePlacementRuleSnapshots);

	/**
	 * Builds one immutable profile snapshot from the authored profile surface while preserving the
	 * frozen public snapshot carrier and proof/assertion behavior.
	 */
	FLayoutProfileSolveSnapshot BuildProfileSnapshotFromProfile(
		int32 SnapshotSchemaVersion,
		const ULayoutProfileAsset* Profile,
		FBuildSparsePlacementRuleSnapshotFn BuildSparsePlacementRuleSnapshot);
}
