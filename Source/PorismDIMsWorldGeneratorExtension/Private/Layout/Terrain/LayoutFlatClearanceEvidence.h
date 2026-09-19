// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit produced artifact carrying flat-clearance evidence for overlap-sensitive terrain cells. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedFlatClearanceEvidenceArtifact
{
	/** True only when an authoritative producer emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Exact flat-clearance cells emitted by the producer. */
	TArray<FLayoutFlatClearanceCellEvidence> Cells;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only flat-clearance evidence adapter. */
namespace LayoutFlatClearanceEvidence
{
	/** Builds a produced artifact from already-authored flat-clearance cell evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutFlatClearanceCellEvidence>& Cells,
		FString Provenance,
		FLayoutProducedFlatClearanceEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced artifact only from explicit flat-clearance evidence already frozen on the terrain/biome artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedFlatClearanceEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit produced flat-clearance evidence, or fails closed when required evidence is absent/invalid. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedFlatClearanceEvidenceArtifact* Artifact,
		bool bRequireFlatClearanceEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
