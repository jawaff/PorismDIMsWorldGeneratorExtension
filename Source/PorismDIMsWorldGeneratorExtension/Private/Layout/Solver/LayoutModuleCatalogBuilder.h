// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

class ULayoutRegionContentSetAsset;
class UObject;

namespace LayoutModuleCatalogBuilder
{
	/** Compiles module-backed content entries from one unified content set into the current module-solve snapshot carrier. */
	void PopulateModuleCatalogFromContentSet(
		FLayoutModuleCatalog& Snapshot,
		const ULayoutRegionContentSetAsset* ContentSet);

}
