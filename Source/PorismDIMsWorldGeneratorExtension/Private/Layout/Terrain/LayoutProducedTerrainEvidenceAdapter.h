// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutAnalyticOverlapEvidence.h"
#include "Layout/Terrain/LayoutCompatibleBiomeAllowList.h"
#include "Layout/Terrain/LayoutExactFallbackEvidence.h"
#include "Layout/Terrain/LayoutFlatClearanceEvidence.h"
#include "Layout/Terrain/LayoutPocketVoidEvidence.h"
#include "Layout/Terrain/LayoutRegionPrewarmEvidenceProducer.h"
#include "Layout/Terrain/LayoutRegionPrewarmEvidenceSourceWriter.h"
#include "Layout/Terrain/LayoutShiftedFootprintProof.h"
#include "Layout/Terrain/LayoutTerrainPathEvidence.h"
#include "Layout/Terrain/LayoutTerrainPlacementEvidence.h"
#include "Layout/Terrain/LayoutTerrainPrimitiveClassificationEvidence.h"

/** Requirement mask for produced terrain evidence artifacts consumed before worker terrain adapters run. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedTerrainEvidenceRequirements
{
	/** True when compatible-biome allow-list evidence is required. */
	bool bRequireCompatibleBiomeAllowList = false;

	/** True when terrain-path evidence is required. */
	bool bRequireTerrainPathEvidence = false;

	/** True when pocket/void interval evidence is required. */
	bool bRequirePocketVoidEvidence = false;

	/** True when exact-fallback evidence is required. */
	bool bRequireExactFallbackEvidence = false;

	/** True when flat-clearance evidence is required. */
	bool bRequireFlatClearanceEvidence = false;

	/** True when analytic-overlap evidence is required. */
	bool bRequireAnalyticOverlapEvidence = false;

	/** True when explicit selected-mode terrain placement evidence is required. */
	bool bRequireTerrainPlacementEvidence = false;

	/** True when terrain primitive classification evidence is required. */
	bool bRequireTerrainPrimitiveClassificationEvidence = false;

	/** Required shifted branch id for non-zero shifted proof, if any. */
	FLayoutId RequiredShiftId;

	/** Required shifted cells for non-zero shifted proof, if any. */
	FIntVector RequiredShiftCells = FIntVector::ZeroValue;
};

/** Explicit produced terrain evidence artifacts supplied by upstream prewarm/scout producers. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedTerrainEvidenceArtifacts
{
	/** Optional authoritative compatible-biome allow-list artifact. */
	TOptional<FLayoutProducedCompatibleBiomeAllowListArtifact> CompatibleBiomeAllowList;

	/** Optional authoritative shifted-footprint proof artifact. */
	TOptional<FLayoutProducedShiftedFootprintProofArtifact> ShiftedFootprintProof;

	/** Optional authoritative terrain-path evidence artifact. */
	TOptional<FLayoutProducedTerrainPathEvidenceArtifact> TerrainPathEvidence;

	/** Optional authoritative pocket/void interval evidence artifact. */
	TOptional<FLayoutProducedPocketVoidEvidenceArtifact> PocketVoidEvidence;

	/** Optional authoritative exact-fallback evidence artifact. */
	TOptional<FLayoutProducedExactFallbackEvidenceArtifact> ExactFallbackEvidence;

	/** Optional authoritative flat-clearance evidence artifact. */
	TOptional<FLayoutProducedFlatClearanceEvidenceArtifact> FlatClearanceEvidence;

	/** Optional authoritative analytic-overlap evidence artifact. */
	TOptional<FLayoutProducedAnalyticOverlapEvidenceArtifact> AnalyticOverlapEvidence;

	/** Optional authoritative selected-mode terrain placement evidence artifact. */
	TOptional<FLayoutProducedTerrainPlacementEvidenceArtifact> TerrainPlacementEvidence;

	/** Optional authoritative terrain primitive classification evidence artifact. */
	TOptional<FLayoutProducedTerrainPrimitiveClassificationArtifact> TerrainPrimitiveClassificationEvidence;
};

/** Central fail-closed adapter for explicit produced terrain evidence artifacts. */
namespace LayoutProducedTerrainEvidenceAdapter
{
	/** Builds the central adapter artifact bundle from explicit region-prewarm artifacts without inferring missing evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildArtifactsFromRegionPrewarmEvidence(
		const FLayoutRegionPrewarmEvidenceArtifacts& RegionPrewarmArtifacts,
		FLayoutProducedTerrainEvidenceArtifacts& OutProducedArtifacts,
		FString& OutFailureReason);

	/** Applies all explicit produced evidence to a frozen terrain/biome input and fails closed when required artifacts are absent or stale. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifacts(
		const FLayoutProducedTerrainEvidenceArtifacts& ProducedArtifacts,
		const FLayoutProducedTerrainEvidenceRequirements& Requirements,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);

	/** Builds and applies explicit region-prewarm evidence artifacts through the central adapter; never infers absent evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyRegionPrewarmEvidenceArtifacts(
		const FLayoutRegionPrewarmEvidenceArtifacts& RegionPrewarmArtifacts,
		const FLayoutProducedTerrainEvidenceRequirements& Requirements,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);

	/** Copies explicit source-writer evidence through producer, region-prewarm artifacts, and central adapter apply in one fail-closed path. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplySourceWriterEvidence(
		const FLayoutRegionPrewarmEvidenceSourceWriterInput& SourceWriterInput,
		const FLayoutProducedTerrainEvidenceRequirements& Requirements,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);

	/** Copies explicit evidence from an existing frozen terrain/biome input through source writer and central adapter apply. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyFrozenTerrainBiomeArtifactEvidence(
		FLayoutId ProducerId,
		FString Provenance,
		const FLayoutRegionPrewarmEvidenceRequirements& SourceRequirements,
		const FLayoutProducedTerrainEvidenceRequirements& ApplyRequirements,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
