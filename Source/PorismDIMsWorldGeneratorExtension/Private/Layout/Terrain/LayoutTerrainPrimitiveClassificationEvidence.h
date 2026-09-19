// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit produced artifact carrying terrain primitive classifications. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedTerrainPrimitiveClassificationArtifact
{
	/** True only when an authoritative producer emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Exact primitive classifications emitted by the producer. */
	TArray<FLayoutTerrainPrimitiveClassificationCellEvidence> Cells;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only terrain primitive classification evidence adapter. */
namespace LayoutTerrainPrimitiveClassificationEvidence
{
	/** Builds a produced artifact from already-authored primitive classification cell evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutTerrainPrimitiveClassificationCellEvidence>& Cells,
		FString Provenance,
		FLayoutProducedTerrainPrimitiveClassificationArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced artifact only from explicit primitive classification evidence already frozen on the terrain/biome artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedTerrainPrimitiveClassificationArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit produced primitive classification evidence, or fails closed when required evidence is absent/invalid. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedTerrainPrimitiveClassificationArtifact* Artifact,
		bool bRequireTerrainPrimitiveClassificationEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
