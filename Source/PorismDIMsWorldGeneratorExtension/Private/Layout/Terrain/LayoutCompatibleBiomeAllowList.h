// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Explicit produced compatible-biome allow-list artifact for frozen terrain/biome inputs. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedCompatibleBiomeAllowListArtifact
{
	/** True only when an authoritative producer emitted this allow-list. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Exact compatible biome row names. Empty means no allow-list artifact. */
	TArray<FName> CompatibleBiomeRowNames;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only compatible-biome allow-list production/validation for frozen terrain/biome artifacts. */
namespace LayoutCompatibleBiomeAllowList
{
	/** Builds an authoritative compatible-biome artifact only from explicit produced row names. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FName>& CompatibleBiomeRowNames,
		FString Provenance,
		FLayoutProducedCompatibleBiomeAllowListArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds compatible-biome evidence from explicit row names already frozen on a worker-safe terrain/biome adapter input. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
		FLayoutId ArtifactId,
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString Provenance,
		FLayoutProducedCompatibleBiomeAllowListArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies an explicit allow-list artifact to a frozen terrain/biome input, or fails when required but absent. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyProducedArtifact(
		const FLayoutProducedCompatibleBiomeAllowListArtifact* Artifact,
		bool bRequireAllowList,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
		FString& OutFailureReason);
}
