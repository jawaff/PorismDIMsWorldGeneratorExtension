// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutReservationPocketPlanning.h"
#include "Layout/Planning/LayoutRootSpacing.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

class AChunkWorldExtended;

namespace LayoutWorldBindingSitePlanner
{
	/** Automatic-only gate. Dedicated salted text hash excludes name-pool handles, candidate/attempt/LOD
	 * identity and probability itself; threshold edits reuse the draw. Invalid probabilities fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool PassesOccupancy(
		int32 WorldSeed, FName BindingId, FIntVector SnappedSite, float Probability);

	/** Enumerates one deterministic normal-cell-aligned candidate per intersecting spacing bucket.
	 * Only final positions inside inclusive owner bounds are returned. Negative buckets use floor division;
	 * invalid inputs or regions exceeding four buckets per axis fail closed. No biome rejection rerolls. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FIntPoint> BuildBoundedNormalCellSiteCenters(
		FIntPoint Min, FIntPoint Max, FIntVector CellSize, FIntPoint SpacingInCells,
		float JitterFraction, int32 WorldSeed, FName BindingId);

	/** Raw block-world bounds for the finite axes on one chunk world. */
	struct FChunkWorldFiniteAxisBlockBounds
	{
		bool bHasFiniteX = false;
		bool bHasFiniteY = false;
		bool bHasFiniteZ = false;
		FIntVector MinInclusive = FIntVector::ZeroValue;
		FIntVector MaxInclusive = FIntVector::ZeroValue;
	};

	/** Copied candidate discovery data. Prototype asset paths remain opaque on workers. */
	struct FPlanningCandidateSnapshot
	{
		int32 Weight = 1;
		bool bValid = false;
		FPlannedLayoutSiteRecord Prototype;
		FName CandidateId;
		FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;
		FLayoutProfileSolveSnapshot FootprintProfile;
		int32 LevelCount = 1;
		bool bUnderground = false;
		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	};

	/** Owns location-selection inputs captured before dispatch, without world/profile dereferences during sampling. */
	struct FSitePlanningSnapshot
	{
		FName BindingId;
		FName MatchingBiomeRowName;
		int32 WorldSeed = 0;
		float OccupancyProbability = 1.0f;
		FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;
		int32 MinimumRootGapCells = 0;
		// One immutable metadata snapshot shared by all binding rows in an area job.
		TSharedPtr<const TMap<FString, FLayoutRootSpacingReservation>, ESPMode::ThreadSafe> RootReservations;
		FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
		FLayoutNoiseCoordinateSettings CoordinateSettings;
		int32 RootReferenceZ = INDEX_NONE;
		/** Optional inclusive native-owner bounds for final snapped centers, never for footprint/search clipping. */
		FBox CandidateCenterBoundsInBlocks = FBox(ForceInit);
		bool bQualifyEnvironment = false;
		FChunkWorldFiniteAxisBlockBounds FiniteAxisBounds;
		TArray<FPlanningCandidateSnapshot> Candidates;
	};

	/**
	 * Resolves the ordinary-root site-center plane on the binding-owned global Z
	 * lattice. The returned Z is the first valid root/top-surface plane at or above
	 * the sampled terrain-top plane (`surface block + 1`).
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API int32 ResolveOrdinaryRootSiteCenterZ(
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 TerrainTopSurfaceZ);

	/** Variant used by runtime/frontend carriers that already resolved shared cell height. */
	PORISMDIMSWORLDGENERATOREXTENSION_API int32 ResolveOrdinaryRootSiteCenterZ(
		int32 SharedCellHeightInBlocks,
		int32 TerrainTopSurfaceZ);

	/**
	 * Resolves the ordinary-root site-center plane on the binding-owned global Z
	 * lattice while keeping the resolved plane inside one finite chunk-world Z span.
	 * This is the project-owned runtime constraint that can raise one below-floor
	 * snap back onto the first valid in-bounds plane, but rejects one above-ceiling
	 * snap because lowering the plane would place the root below the sampled terrain.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
		int32 SharedCellHeightInBlocks,
		int32 TerrainTopSurfaceZ,
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		int32& OutSiteCenterZ);

	/** World-binding convenience wrapper for finite-axis ordinary-root Z resolution. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 TerrainTopSurfaceZ,
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		int32& OutSiteCenterZ);

	/**
	 * Resolves finite raw block-world bounds from the shared worldgen scale-context
	 * contract so layout lattice callers can respect single-chunk axis limits
	 * without reimplementing WorldGenDef span logic locally.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveChunkWorldFiniteAxisBlockBounds(
		const AChunkWorldExtended* ChunkWorld,
		FChunkWorldFiniteAxisBlockBounds& OutBounds);

	/** Checks whether one raw block-world site center stays inside the finite chunk-world axes. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool IsBlockWorldPosInsideFiniteAxisBounds(
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		const FIntVector& BlockWorldPos);

	/** Verifies final-occupancy discovery evidence has not changed before Planning Window prewarm. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateDiscoveredEnvironmentEvidence(
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		bool bPrepared,
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainArtifact,
		FString& OutFailureReason);

	/** Selects one deterministic weighted candidate runtime view for an ordinary root site. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TrySelectOrdinaryRootRuntimeViewForSite(
		const ULayoutWorldBindingAsset* WorldBinding,
		FName MatchingBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 WorldSeed,
		FLayoutWorldBindingRuntimeView& OutRuntimeView,
		FString& OutFailureReason);

	/** Captures authored candidate order, policies and profile bounds on the game thread.
	 * Invalid candidates keep their weighted slot, preserving selection/rejection rather than choosing a replacement. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FSitePlanningSnapshot CaptureSitePlanningInputs(
		const ULayoutWorldBindingAsset* WorldBinding, FName MatchingBiomeRowName,
		int32 WorldSeed,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		int32 RootReferenceZ = INDEX_NONE, AChunkWorldExtended* ChunkWorld = nullptr);

	/** Builds candidates from copied inputs and owned noise; no asset/world reads or store mutation.
	 * Preserves seed selection, surface/cavity qualification and footprint ownership. Returns unique
	 * ordered proposals, including overlapping alternatives; runtime reservations enforce final spacing.
	 * Final center ownership rejects before footprint sampling; surface/cavity search remains unchanged.
	 * Optional outputs identify captured blockers and count occupancy/owner rejections. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FPlannedLayoutSiteRecord> BuildPendingSiteRecordsFromPockets(
		const TArray<FLayoutReservationPocket>& Pockets, const FSitePlanningSnapshot& Inputs,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler, TSet<FString>* OutBlockingReservations = nullptr,
		int32* OutOccupancyRejected = nullptr, int32* OutOwnerRejected = nullptr);
}
