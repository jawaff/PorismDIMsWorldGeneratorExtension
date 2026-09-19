// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutSolveSnapshotValidation
{
	/** Re-runs the module local-shape snapshot assertions after snapshot mutation or import. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void ValidateModuleLocalShapeContracts(FLayoutModuleSolveSnapshot& Snapshot);
}
