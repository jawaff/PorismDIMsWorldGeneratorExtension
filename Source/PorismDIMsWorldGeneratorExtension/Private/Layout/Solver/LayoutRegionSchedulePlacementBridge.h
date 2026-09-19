// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutProfileSolverInternal.h"

/**
 * Thin orchestration seam for the placement-backed recursive scheduler.
 *
 * The long-term goal is for this interface to stay small while specialized
 * helper units own parent probing and deferred complete validation.
 */
namespace LayoutRegionScheduleSolverPrivate
{
	/** Solves one multi-region schedule through the placement bridge orchestration layer. */
	FLayoutRegionSolveScheduleResult SolveRegionsSynchronouslyInternal(
		const FLayoutRegionSolveScheduleRequest& ScheduleRequest);

	/** Solves one root request and any recursive children through the placement bridge. */
	FLayoutRegionSolveScheduleResult SolveRegionTreeInternal(
		const FLayoutRegionSolveRequest& RootRequest);
}
