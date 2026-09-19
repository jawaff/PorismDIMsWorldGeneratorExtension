// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit pocket/void interval evidence artifact for underground/tunnel terrain modes. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedPocketVoidEvidenceArtifact
{
	/** True only when an authoritative pocket/void sampler emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Pointer-free pocket/void interval samples. */
	TArray<FLayoutFrozenTerrainVoidIntervalSample> PocketVoidIntervals;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only pocket/void interval evidence scaffold for underground/tunnel terrain modes. */
namespace LayoutPocketVoidEvidence
{
	/** Builds a produced pocket/void artifact from exact interval samples. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutFrozenTerrainVoidIntervalSample>& PocketVoidIntervals,
		FString Provenance,
		FLayoutProducedPocketVoidEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds pocket/void evidence from explicit intervals already frozen on a worker-safe terrain/biome adapter input. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedPocketVoidEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit pocket/void interval evidence, or fails closed when interval evidence is required. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedPocketVoidEvidenceArtifact* Artifact,
		bool bRequirePocketVoidEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
