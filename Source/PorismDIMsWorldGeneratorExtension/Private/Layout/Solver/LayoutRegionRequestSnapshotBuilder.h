// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutRegionRequestSnapshotBuilder
{
	/** Populates one standalone region request from request-owned root identity, execution settings, and world-facing terrain settings before snapshot attachment. */
	void PopulateStandaloneRequestBase(
		FLayoutRegionSolveRequest& Request,
		int32 SnapshotSchemaVersion,
		int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		FLayoutId RootPlacementPolicyId,
		FLayoutId RootCandidateId,
		FLayoutId RootSolveId,
		int32 TemplatePlacementZOffsetBlocks,
		ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy);

	/** Appends the request-owned root identity and template-offset assertions before content/module snapshots are necessarily attached. */
	void AppendStandaloneRequestBaseAssertions(FLayoutRegionSolveRequest& Request);

	/** Refreshes the request-owned forced bundle insertion assertion after callers populate anchored insertions. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RefreshStandaloneForcedPlacementBundleInsertionAssertions(FLayoutRegionSolveRequest& Request);

	/** Refreshes the request-owned live composite bundle assertion after callers mutate frozen composite-backed module snapshots. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RefreshStandaloneLiveCompositeBundleAssertions(FLayoutRegionSolveRequest& Request);

	/** Refreshes the request-owned required-route assertion after callers populate explicit route constraints. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RefreshStandaloneRequiredRouteConstraintAssertions(FLayoutRegionSolveRequest& Request);

	/** Refreshes the request-owned stepped-terrain assertions after callers populate stepped support or supplied planned cells. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RefreshStandaloneSteppedTerrainAssertions(FLayoutRegionSolveRequest& Request);

	/** Refreshes the request-owned effective-snapshot assertion after callers mutate frozen snapshot carriers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RefreshStandaloneSnapshotContractAssertions(FLayoutRegionSolveRequest& Request);

	/** Finalizes one standalone region request after content/module/profile snapshots have been attached. */
	void FinalizeStandaloneRequestSnapshots(FLayoutRegionSolveRequest& Request);
}
