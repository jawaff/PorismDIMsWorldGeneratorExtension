// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutAnalyticOverlapEvidence.h"
#include "Layout/Terrain/LayoutCompatibleBiomeAllowList.h"
#include "Layout/Terrain/LayoutExactFallbackEvidence.h"
#include "Layout/Terrain/LayoutFlatClearanceEvidence.h"
#include "Layout/Terrain/LayoutPocketVoidEvidence.h"
#include "Layout/Terrain/LayoutTerrainPathEvidence.h"
#include "Layout/Terrain/LayoutTerrainPlacementEvidence.h"
#include "Layout/Terrain/LayoutTerrainPrimitiveClassificationEvidence.h"

/** Required evidence mask for one pointer-free region prewarm evidence production pass. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionPrewarmEvidenceRequirements
{
	/** True when an explicit compatible-biome allow-list source must be present. */
	bool bRequireCompatibleBiomeAllowList = false;

	/** True when explicit exact-fallback evidence must be present. */
	bool bRequireExactFallbackEvidence = false;

	/** True when explicit flat-clearance evidence must be present. */
	bool bRequireFlatClearanceEvidence = false;

	/** True when explicit analytic-overlap evidence must be present. */
	bool bRequireAnalyticOverlapEvidence = false;

	/** True when explicit selected-mode terrain placement evidence must be present. */
	bool bRequireTerrainPlacementEvidence = false;

	/** True when explicit primitive-classification evidence must be present. */
	bool bRequirePrimitiveClassificationEvidence = false;

	/** True when explicit terrain-path evidence must be present. */
	bool bRequireTerrainPathEvidence = false;

	/** True when explicit pocket/void interval evidence must be present. */
	bool bRequirePocketVoidEvidence = false;
};

/** Pointer-free explicit source evidence carried into prewarm; absent sources are never inferred. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionPrewarmEvidenceProducerInput
{
	/** Stable producer/scout id for diagnostics and fail-closed provenance. */
	FLayoutId ProducerId;

	/** Human-readable provenance copied to produced evidence artifacts. */
	FString Provenance;

	/** Evidence required for this prewarm pass. */
	FLayoutRegionPrewarmEvidenceRequirements Requirements;

	/** True only when CompatibleBiomeRowNames was explicitly produced upstream. */
	bool bHasCompatibleBiomeAllowListSource = false;

	/** Stable artifact id for compatible-biome evidence. */
	FLayoutId CompatibleBiomeAllowListArtifactId;

	/** Explicit compatible biome row names. */
	TArray<FName> CompatibleBiomeRowNames;

	/** True only when ExactFallbackCells was explicitly produced upstream. */
	bool bHasExactFallbackEvidenceSource = false;

	/** Stable artifact id for exact-fallback evidence. */
	FLayoutId ExactFallbackArtifactId;

	/** Explicit exact-fallback cell evidence. */
	TArray<FLayoutExactFallbackCellEvidence> ExactFallbackCells;

	/** True only when FlatClearanceCells was explicitly produced upstream. */
	bool bHasFlatClearanceEvidenceSource = false;

	/** Stable artifact id for flat-clearance evidence. */
	FLayoutId FlatClearanceArtifactId;

	/** Explicit flat-clearance cell evidence. */
	TArray<FLayoutFlatClearanceCellEvidence> FlatClearanceCells;

	/** True only when AnalyticOverlapCells was explicitly produced upstream. */
	bool bHasAnalyticOverlapEvidenceSource = false;

	/** Stable artifact id for analytic-overlap evidence. */
	FLayoutId AnalyticOverlapArtifactId;

	/** Explicit analytic-overlap cell evidence. */
	TArray<FLayoutAnalyticOverlapCellEvidence> AnalyticOverlapCells;

	/** True only when TerrainPlacementCells was explicitly produced upstream. */
	bool bHasTerrainPlacementEvidenceSource = false;

	/** Stable artifact id for terrain placement evidence. */
	FLayoutId TerrainPlacementArtifactId;

	/** Explicit selected-mode terrain placement cell evidence. */
	TArray<FLayoutTerrainPlacementCellEvidence> TerrainPlacementCells;

	/** True only when PrimitiveClassificationCells was explicitly produced upstream. */
	bool bHasPrimitiveClassificationEvidenceSource = false;

	/** Stable artifact id for primitive-classification evidence. */
	FLayoutId PrimitiveClassificationArtifactId;

	/** Explicit primitive-classification cell evidence. */
	TArray<FLayoutTerrainPrimitiveClassificationCellEvidence> PrimitiveClassificationCells;

	/** True only when TerrainPathSamples was explicitly produced upstream. */
	bool bHasTerrainPathEvidenceSource = false;

	/** Stable artifact id for terrain-path evidence. */
	FLayoutId TerrainPathArtifactId;

	/** Explicit classified terrain-path samples. */
	TArray<FLayoutFrozenTerrainPathSample> TerrainPathSamples;

	/** True only when PocketVoidIntervals was explicitly produced upstream. */
	bool bHasPocketVoidEvidenceSource = false;

	/** Stable artifact id for pocket/void evidence. */
	FLayoutId PocketVoidArtifactId;

	/** Explicit pocket/void interval samples. */
	TArray<FLayoutFrozenTerrainVoidIntervalSample> PocketVoidIntervals;
};

/** Produced explicit evidence bundle emitted by prewarm without terrain reads or fallback synthesis. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionPrewarmEvidenceArtifacts
{
	/** Optional produced compatible-biome allow-list artifact. */
	TOptional<FLayoutProducedCompatibleBiomeAllowListArtifact> CompatibleBiomeAllowList;

	/** Optional produced exact-fallback evidence artifact. */
	TOptional<FLayoutProducedExactFallbackEvidenceArtifact> ExactFallbackEvidence;

	/** Optional produced flat-clearance evidence artifact. */
	TOptional<FLayoutProducedFlatClearanceEvidenceArtifact> FlatClearanceEvidence;

	/** Optional produced analytic-overlap evidence artifact. */
	TOptional<FLayoutProducedAnalyticOverlapEvidenceArtifact> AnalyticOverlapEvidence;

	/** Optional produced selected-mode terrain placement evidence artifact. */
	TOptional<FLayoutProducedTerrainPlacementEvidenceArtifact> TerrainPlacementEvidence;

	/** Optional produced primitive-classification evidence artifact. */
	TOptional<FLayoutProducedTerrainPrimitiveClassificationArtifact> PrimitiveClassificationEvidence;

	/** Optional produced terrain-path evidence artifact. */
	TOptional<FLayoutProducedTerrainPathEvidenceArtifact> TerrainPathEvidence;

	/** Optional produced pocket/void evidence artifact. */
	TOptional<FLayoutProducedPocketVoidEvidenceArtifact> PocketVoidEvidence;
};

/** Explicit-only region prewarm evidence producer; validates source artifacts and never samples terrain. */
namespace LayoutRegionPrewarmEvidenceProducer
{
	/** Builds produced evidence artifacts only from explicit pointer-free source evidence; missing required sources fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifacts(
		const FLayoutRegionPrewarmEvidenceProducerInput& Input,
		FLayoutRegionPrewarmEvidenceArtifacts& OutArtifacts,
		FString& OutFailureReason);
}
