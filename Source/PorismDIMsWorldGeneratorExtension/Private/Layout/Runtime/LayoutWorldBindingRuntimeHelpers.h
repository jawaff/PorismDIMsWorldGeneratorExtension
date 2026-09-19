// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutProfileSolver.h"

class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;
class ULayoutWorldBindingAsset;
class AChunkWorldCore;
struct FLayoutRegionContentEntry;

namespace LayoutWorldBindingRuntimeHelpers
{
	/** Finds a live carrier matching frozen placement identity across the content graph.
	 * Entry names and catalog indices are not global identities. Ambiguous or changed
	 * assets return null; callers must retain frozen template/leaf authority instead.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API const FLayoutRegionContentEntry* FindFrozenPlacementSourceEntry(
		const ULayoutRegionContentSetAsset* ContentSet, const FLayoutPlacedModule& Placement);

	/** Maps the authored direct-root solve budget into the frozen solver execution settings carrier. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolverExecutionSettings BuildExecutionSettingsFromSolveBudget(const FLayoutRootSolveBudgetSettings& SolveBudget);

	/** Returns the preferred profile-owned content set when the runtime path is not forced onto a legacy module-set override. */
	PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutRegionContentSetAsset* ResolveRuntimePreferredContentSet(const ULayoutProfileAsset* Profile);

	/**
	 * Packages plain explicit-root inputs into the shared runtime-view carrier.
	 * Active runtime callers use this to stay on the runtime-view request seam
	 * instead of reopening raw profile/content/module request construction.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorldBindingRuntimeView BuildExplicitRootRuntimeView(
		ULayoutProfileAsset* LayoutProfile,
		ULayoutRegionContentSetAsset* LayoutContentSet,
		const FLayoutRootSolveBudgetSettings& SolveBudget,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy);

	/**
	 * Rebuilds one binding-aware explicit-root runtime view from an authored
	 * world binding plus one already-chosen profile. Editor/runtime tooling uses
	 * this when the binding-owned lattice, terrain policy, or continuation-family
	 * placement contract must drive the active explicit-root request path.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildExplicitRootRuntimeViewFromWorldBindingProfile(
		const ULayoutWorldBindingAsset* WorldBinding,
		ULayoutProfileAsset* LayoutProfile,
		FLayoutWorldBindingRuntimeView& OutRuntimeView,
		FLayoutWorldBindingSiteFrontendSelection& OutFrontendSelection,
		FString& OutFailureReason);

	/**
	 * Builds the explicit-root runtime request without mutating the chunk world.
	 * This overload consumes one already-resolved runtime view so binding-driven
	 * callers do not reopen raw profile/content/module request reconstruction.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildExplicitRuntimeSolveRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 SolveSeed,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason);

	/**
	 * Materializes one non-loaded-chunk stepped-support contract onto a supplied
	 * explicit-root runtime request and refreshes the named stepped assertions.
	 * This overload preserves the caller-resolved runtime-view metrics and
	 * placement offset instead of reopening them from raw assets.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		FName EligibleBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& InOutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* World = nullptr);

	/**
	 * Builds one explicit-root runtime request and then materializes the
	 * active-biome stepped-support contract onto that same frozen request.
	 * This overload consumes the caller-resolved runtime view directly.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		FName EligibleBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 SolveSeed,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* World = nullptr);

	/** Convenience overload that derives FootprintSize from the profile and builds a simple flat-grid planned-cell set for test/editor call sites where the prewarm adapter will produce the authoritative cells. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		FName EligibleBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 SolveSeed,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* World = nullptr);

	/**
	 * Builds one cached resolved-site record from explicit runtime solve
	 * carriers. Active direct-root runtime callers use this to keep root
	 * publication, solve-source, and solved-payload assembly on one helper seam
	 * instead of rebuilding those carriers inline after solve.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedLayoutSiteRecord BuildResolvedSiteRecordFromRuntimeSolve(
		const FResolvedLayoutSiteLocationMetadata& LocationMetadata,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		int32 SolveSeed,
		const FLayoutRegionSolveRequest& SolveRequest,
		const FLayoutSolveResult& SolveResult,
		const FLayoutWorldBindingSiteFrontendSelection* FrontendSelection = nullptr);

	/** Resolves the runtime shared cell size by preferring the solved carrier before falling back to live content/module set owners. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FIntVector ResolveRuntimeSharedCellSizeInBlocks(
		const FLayoutSolveResult& SolveResult,
		const ULayoutRegionContentSetAsset* ContentSet);

	/** Resolves the runtime shared cell size from live content/module set owners when no solve result carrier is available. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FIntVector ResolveRuntimeSharedCellSizeInBlocks(
		const ULayoutRegionContentSetAsset* ContentSet);
}
