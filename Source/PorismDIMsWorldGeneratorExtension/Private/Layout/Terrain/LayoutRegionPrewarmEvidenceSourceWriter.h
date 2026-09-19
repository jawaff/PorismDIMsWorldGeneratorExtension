// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutRegionPrewarmEvidenceProducer.h"

/** Pointer-free explicit source evidence copied into a region prewarm producer input. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionPrewarmEvidenceSourceWriterInput
{
	/** Stable producer/scout id for diagnostics and artifact correlation. */
	FLayoutId ProducerId;

	/** Human-readable provenance copied to produced evidence artifacts. */
	FString Provenance;

	/** Evidence required by the downstream region prewarm producer. */
	FLayoutRegionPrewarmEvidenceRequirements Requirements;

	/** True only when compatible biome rows were explicitly supplied. */
	bool bHasCompatibleBiomeAllowListSource = false;

	/** Stable artifact id for compatible biome evidence. */
	FLayoutId CompatibleBiomeAllowListArtifactId;

	/** Explicit compatible biome row names. */
	TArray<FName> CompatibleBiomeRowNames;

	/** True only when exact fallback cell evidence was explicitly supplied. */
	bool bHasExactFallbackEvidenceSource = false;

	/** Stable artifact id for exact fallback evidence. */
	FLayoutId ExactFallbackArtifactId;

	/** Explicit exact fallback cell evidence. */
	TArray<FLayoutExactFallbackCellEvidence> ExactFallbackCells;

	/** True only when flat clearance cell evidence was explicitly supplied. */
	bool bHasFlatClearanceEvidenceSource = false;

	/** Stable artifact id for flat clearance evidence. */
	FLayoutId FlatClearanceArtifactId;

	/** Explicit flat clearance cell evidence. */
	TArray<FLayoutFlatClearanceCellEvidence> FlatClearanceCells;

	/** True only when analytic overlap cell evidence was explicitly supplied. */
	bool bHasAnalyticOverlapEvidenceSource = false;

	/** Stable artifact id for analytic overlap evidence. */
	FLayoutId AnalyticOverlapArtifactId;

	/** Explicit analytic overlap cell evidence. */
	TArray<FLayoutAnalyticOverlapCellEvidence> AnalyticOverlapCells;

	/** True only when selected-mode terrain placement cell evidence was explicitly supplied. */
	bool bHasTerrainPlacementEvidenceSource = false;

	/** Stable artifact id for terrain placement evidence. */
	FLayoutId TerrainPlacementArtifactId;

	/** Explicit selected-mode terrain placement cell evidence. */
	TArray<FLayoutTerrainPlacementCellEvidence> TerrainPlacementCells;

	/** True only when primitive classification cell evidence was explicitly supplied. */
	bool bHasPrimitiveClassificationEvidenceSource = false;

	/** Stable artifact id for primitive classification evidence. */
	FLayoutId PrimitiveClassificationArtifactId;

	/** Explicit primitive classification cell evidence. */
	TArray<FLayoutTerrainPrimitiveClassificationCellEvidence> PrimitiveClassificationCells;

	/** True only when classified terrain path samples were explicitly supplied. */
	bool bHasTerrainPathEvidenceSource = false;

	/** Stable artifact id for terrain path evidence. */
	FLayoutId TerrainPathArtifactId;

	/** Explicit classified terrain path samples. */
	TArray<FLayoutFrozenTerrainPathSample> TerrainPathSamples;

	/** True only when pocket/void interval samples were explicitly supplied. */
	bool bHasPocketVoidEvidenceSource = false;

	/** Stable artifact id for pocket/void evidence. */
	FLayoutId PocketVoidArtifactId;

	/** Explicit pocket/void interval samples. */
	TArray<FLayoutFrozenTerrainVoidIntervalSample> PocketVoidIntervals;
};

/** Explicit source writer that copies already-produced terrain observations into prewarm producer input. */
namespace LayoutRegionPrewarmEvidenceSourceWriter
{
	/** Copies and validates explicit source evidence into a producer input; missing required sources fail closed and no terrain facts are inferred. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducerInput(
		const FLayoutRegionPrewarmEvidenceSourceWriterInput& Input,
		FLayoutRegionPrewarmEvidenceProducerInput& OutProducerInput,
		FString& OutFailureReason);

	/** Copies explicit frozen adapter evidence into producer input; flagged payload mismatches fail closed so stale arrays are never inferred. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducerInputFromFrozenTerrainBiomeArtifact(
		FLayoutId ProducerId,
		FString Provenance,
		const FLayoutRegionPrewarmEvidenceRequirements& Requirements,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FLayoutRegionPrewarmEvidenceProducerInput& OutProducerInput,
		FString& OutFailureReason);
}
