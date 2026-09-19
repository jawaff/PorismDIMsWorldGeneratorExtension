// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Types/LayoutTypes.h"

/** Explicit requirement-to-assertion mapping artifact for one certified child demand. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildParentAssertionSubsetMappingArtifact
{
	/** True only when negotiation emitted exact requirement-to-assertion ids for this child. */
	bool bHasExplicitMapping = false;

	/** Exact parent assertion ids selected by negotiation for this child. Empty means no subset. */
	TArray<FLayoutId> ParentAssertionSubsetIds;
};

/** Explicit-only parent assertion subset extraction for certified child witnesses. */
namespace LayoutCertifiedChildAssertionSubset
{
	/** Builds an explicit mapping artifact from already-named ids; empty ids mean no artifact is present. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildParentAssertionSubsetMappingArtifact BuildExplicitMappingArtifact(
		const TArray<FLayoutId>& ParentAssertionSubsetIds);

	/** Extracts exact parent assertion subset ids from an explicit mapping artifact, or emits empty when absent. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryExtractParentAssertionSubsetIds(
		const FLayoutChildParentAssertionSubsetMappingArtifact* MappingArtifact,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		TArray<FLayoutId>& OutSubsetIds,
		FString& OutFailureReason);

	/** Resolves witness assertion ids from an already-extracted exact subset and fails closed on missing/failed ids. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveParentWitnessAssertionIds(
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		const TArray<FLayoutId>& ParentAssertionSubsetIds,
		TArray<FLayoutId>& OutParentWitnessAssertionIds,
		FString& OutFailureReason);
}
