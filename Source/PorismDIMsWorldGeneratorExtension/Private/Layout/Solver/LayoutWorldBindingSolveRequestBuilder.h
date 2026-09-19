// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Solver/LayoutProfileSolver.h"

class AChunkWorldCore;

namespace LayoutWorldBindingSolveRequestBuilder
{
	/** Maps one authored solve budget onto the frozen solver execution-settings carrier. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolverExecutionSettings BuildExecutionSettingsFromSolveBudget(
		const FLayoutRootSolveBudgetSettings& SolveBudget);

	/**
	 * Builds one standalone solve request from the current world-binding-facing inputs.
	 * This is the shared request seam for planning-window and direct-root flows.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildStandaloneSolveRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		int32 SolveSeed,
		const FString& RegionDebugPath,
		FLayoutId RootPlacementPolicyId,
		FLayoutId RootCandidateId,
		FLayoutId RootSolveId,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason);

	/** Builds one planning-window standalone solve request from the stored planned-site record. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildPlanningWindowSolveRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason);

	/**
	 * Compiles one frozen stepped-terrain support map for a planned world-binding
	 * site from the active-biome sampler without reading loaded chunks.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildPlanningWindowSteppedTerrainSupportMap(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason,
		const AChunkWorldCore* const World = nullptr);

	/**
	 * Materializes one planning-window stepped-support contract onto a supplied
	 * request and refreshes the named stepped request assertions.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPopulatePlanningWindowSteppedTerrainSupportOnRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& InOutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World = nullptr);

	/**
	 * Builds one planning-window request and then materializes the active-biome
	 * stepped-support contract onto that same frozen request surface.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildPlanningWindowSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World = nullptr);

	/** Convenience overload that derives FootprintSize from the profile and builds a simple flat-grid planned-cell set for test call sites where the prewarm adapter will produce the authoritative cells. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildPlanningWindowSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World = nullptr);

}
