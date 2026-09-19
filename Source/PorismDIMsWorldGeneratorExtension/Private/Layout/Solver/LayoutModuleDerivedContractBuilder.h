// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutModuleDerivedContractBuilder
{
	/** Compiles per-cell exposed-face snapshots plus internal traversal bridges from the frozen module snapshot carrier. */
	void BuildModuleLocalShapeContracts(FLayoutModuleSolveSnapshot& Snapshot);

	/** Derives endpoint, span, and vertical-access contracts from the already-compiled local module shape carrier. */
	void DeriveModuleInterfaceContracts(FLayoutModuleSolveSnapshot& Snapshot);
}
