// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit produced artifact carrying exact fallback evidence for threshold-band, pinned, or non-heightfield cells. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedExactFallbackEvidenceArtifact
{
	/** True only when an authoritative producer emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Exact fallback cells emitted by the producer. */
	TArray<FLayoutExactFallbackCellEvidence> Cells;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only exact fallback evidence adapter. */
namespace LayoutExactFallbackEvidence
{
	/** Builds a produced artifact from already-authored exact fallback cell evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutExactFallbackCellEvidence>& Cells,
		FString Provenance,
		FLayoutProducedExactFallbackEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced artifact only from explicit exact fallback evidence already frozen on the terrain/biome artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedExactFallbackEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit produced exact fallback evidence, or fails closed when required evidence is absent/invalid. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedExactFallbackEvidenceArtifact* Artifact,
		bool bRequireExactFallbackEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
