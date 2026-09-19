// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

struct FLayoutRegionContentEntry;

namespace LayoutContentEntrySnapshotBuilder
{
	/** Copies one unified content-entry contract into the immutable entry snapshot base before later child-template/proof wiring runs. */
	void PopulateContentEntrySnapshotBase(
		FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FLayoutRegionContentEntry& Entry,
		int32& InOutModuleEntryCount);
}
