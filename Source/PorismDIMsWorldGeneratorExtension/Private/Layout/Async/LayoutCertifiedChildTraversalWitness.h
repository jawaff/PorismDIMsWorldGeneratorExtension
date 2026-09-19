// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"

/** Explicit selected traversal capability artifact for one certified child demand. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildTraversalWitnessMappingArtifact
{
	/** True only when negotiation emitted exact selected traversal capability ids. */
	bool bHasExplicitTraversalWitness = false;

	/** Exact selected traversal capability ids. Empty means no explicit traversal witness. */
	TArray<FLayoutId> SelectedTraversalCapabilityIds;
};

/** Explicit-only selected traversal witness extraction for certified child handoffs. */
namespace LayoutCertifiedChildTraversalWitness
{
	/** Builds an explicit traversal witness artifact from already-named ids; empty ids mean no artifact is present. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildTraversalWitnessMappingArtifact BuildExplicitTraversalWitnessArtifact(
		const TArray<FLayoutId>& SelectedTraversalCapabilityIds);

	/** Extracts exact selected traversal capability ids or emits empty when no explicit artifact exists. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryExtractSelectedTraversalCapabilityIds(
		const FLayoutChildTraversalWitnessMappingArtifact* MappingArtifact,
		TArray<FLayoutId>& OutSelectedTraversalCapabilityIds,
		FString& OutFailureReason);
}
