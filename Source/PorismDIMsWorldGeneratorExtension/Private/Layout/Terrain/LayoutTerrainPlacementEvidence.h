// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit produced terrain-placement artifact; prewarm writers must provide every verdict. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedTerrainPlacementEvidenceArtifact
{
	/** Stable artifact id for diagnostics and fail-closed provenance. */
	FLayoutId ArtifactId;

	/** Explicit per-cell selected-mode terrain placement verdicts. */
	TArray<FLayoutTerrainPlacementCellEvidence> Cells;

	/** Human-readable provenance copied into audit output. */
	FString Provenance;
};

/** Explicit-only terrain placement evidence helpers; no terrain reads or classification inference. */
namespace LayoutTerrainPlacementEvidence
{
	/** Builds one produced artifact from explicit cell verdicts; invalid or duplicate cells fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutTerrainPlacementCellEvidence>& Cells,
		const FString& Provenance,
		FLayoutProducedTerrainPlacementEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced artifact only from explicit placement evidence already frozen on the terrain/biome artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		const FString& Provenance,
		FLayoutProducedTerrainPlacementEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies produced placement evidence to frozen terrain input, failing closed when required evidence is absent. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedTerrainPlacementEvidenceArtifact* Artifact,
		bool bRequireTerrainPlacementEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
