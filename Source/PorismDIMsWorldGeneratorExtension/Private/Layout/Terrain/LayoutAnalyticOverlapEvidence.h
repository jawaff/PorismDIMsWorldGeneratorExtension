// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit produced artifact carrying analytic overlap evidence for sampled terrain cells. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedAnalyticOverlapEvidenceArtifact
{
	/** True only when an authoritative producer emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Exact analytic-overlap cells emitted by the producer. */
	TArray<FLayoutAnalyticOverlapCellEvidence> Cells;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only analytic overlap evidence adapter. */
namespace LayoutAnalyticOverlapEvidence
{
	/** Builds a produced artifact from already-authored analytic overlap cell evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutAnalyticOverlapCellEvidence>& Cells,
		FString Provenance,
		FLayoutProducedAnalyticOverlapEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced artifact only from explicit analytic-overlap evidence already frozen on the terrain/biome artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedAnalyticOverlapEvidenceArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit produced analytic overlap evidence, or fails closed when required evidence is absent/invalid. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedAnalyticOverlapEvidenceArtifact* Artifact,
		bool bRequireAnalyticOverlapEvidence,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
