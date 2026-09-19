// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit terrain-path evidence artifact for worker-side continuation terrain classification. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedTerrainPathEvidenceArtifact
{
	/** True only when an authoritative path sampler emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Pointer-free classified terrain path samples. */
	TArray<FLayoutFrozenTerrainPathSample> TerrainPathSamples;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only terrain-path evidence scaffold for continuation/open-space terrain modes. */
namespace LayoutTerrainPathEvidence
{
	/** Builds a produced terrain-path artifact from exact classified path samples. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutFrozenTerrainPathSample>& TerrainPathSamples,
		FString Provenance,
		FLayoutProducedTerrainPathEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds terrain-path evidence from explicit path samples already frozen on a worker-safe terrain/biome adapter input. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedTerrainPathEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit terrain-path evidence, or fails closed when a path evidence requirement is active. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedTerrainPathEvidenceArtifact* Artifact,
		bool bRequireTerrainPathEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
