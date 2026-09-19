// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;

namespace LayoutStandaloneRegionRequestBuilder
{
	/** Shared deterministic footprint selection for discovery and request capture; consumes only footprint bounds and seed. */
	FIntPoint SelectFootprintSize(const FLayoutProfileSolveSnapshot& ProfileSnapshot, int32 Seed);

	/**
	 * Builds one standalone region request from the unified content-set caller surface while preserving
	 * the frozen public request carrier and request-owned assertion behavior.
	 */
	FLayoutRegionSolveRequest BuildRequestFromContentSet(
		int32 SnapshotSchemaVersion,
		const ULayoutRegionContentSetAsset* ContentSet,
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		FLayoutId RootPlacementPolicyId,
		FLayoutId RootCandidateId,
		int32 TemplatePlacementZOffsetBlocks,
		FLayoutId RootSolveId,
		ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
		const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap = nullptr,
		const FIntVector* SharedCellSizeOverride = nullptr);

	/**
	 * Builds one standalone region request from the profile-only caller surface, including the current
	 * profile/content-set presence assertions before later content-set delegation runs.
	 */
	FLayoutRegionSolveRequest BuildRequestFromProfile(
		int32 SnapshotSchemaVersion,
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		FLayoutId RootPlacementPolicyId,
		FLayoutId RootCandidateId,
		int32 TemplatePlacementZOffsetBlocks,
		FLayoutId RootSolveId,
		ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
		const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap = nullptr,
		const FIntVector* SharedCellSizeOverride = nullptr);
}
