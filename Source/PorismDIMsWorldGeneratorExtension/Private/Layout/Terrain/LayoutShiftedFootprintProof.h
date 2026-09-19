// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit shifted-footprint proof artifact for non-zero coarse placement branches. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedShiftedFootprintProofArtifact
{
	/** True only when an authoritative shifted-footprint proof producer emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Pointer-free shifted-footprint evidence bundle. */
	FLayoutFrozenShiftedFootprintEvidence Evidence;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only shifted-footprint proof scaffold for terrain-backed shifted branches. */
namespace LayoutShiftedFootprintProof
{
	/** Builds a produced shifted-footprint proof artifact from complete exact evidence. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenShiftedFootprintEvidence& Evidence,
		FString Provenance,
		FLayoutProducedShiftedFootprintProofArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds shifted-footprint proof from an exact shifted evidence bundle already frozen on a worker-safe terrain/biome adapter input. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		FLayoutId RequiredShiftId,
		const FIntVector& RequiredShiftCells,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedShiftedFootprintProofArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies explicit shifted proof to a terrain/biome artifact, or fails closed when a non-zero shift requires proof. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedShiftedFootprintProofArtifact* Artifact,
		FLayoutId RequiredShiftId,
		const FIntVector& RequiredShiftCells,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
