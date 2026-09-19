// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

class UObject;
class ULayoutRegionContentSetAsset;

namespace LayoutContentSetSnapshotBuilder
{
	/** Copies one unified content-set snapshot's non-recursive base metadata before later entry recursion and proof wiring runs. */
	void PopulateContentSetSnapshotBase(
		FLayoutRegionContentSetSolveSnapshot& Snapshot,
		const ULayoutRegionContentSetAsset* ContentSet);

}
