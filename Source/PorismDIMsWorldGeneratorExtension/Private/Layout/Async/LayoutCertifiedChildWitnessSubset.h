// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutCertifiedChildAssertionSubset.h"
#include "Layout/Async/LayoutCertifiedChildTraversalWitness.h"

struct FLayoutRegionSolveRequest;

/** Combined explicit subset artifacts selected by parent negotiation for one certified child. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCertifiedChildWitnessSubsetArtifacts
{
	/** Explicit selected traversal capability ids when negotiation emitted them. */
	FLayoutChildTraversalWitnessMappingArtifact TraversalWitness;

	/** Explicit requirement-to-parent-assertion ids when negotiation emitted them. */
	FLayoutChildParentAssertionSubsetMappingArtifact ParentAssertionSubset;
};

/** Extracted exact witness subsets that may be copied into a child witness bundle. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCertifiedChildWitnessSubsets
{
	/** Exact selected traversal capability ids; empty when no explicit artifact exists. */
	TArray<FLayoutId> SelectedTraversalCapabilityIds;

	/** Exact passed parent assertion ids; empty when no explicit artifact exists. */
	TArray<FLayoutId> ParentAssertionIds;
};

/** Authoritative produced witness-subset artifact emitted by parent negotiation when exact ids are known. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedCertifiedChildWitnessSubsetArtifact
{
	/** True only when parent negotiation emitted exact subset artifacts. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics/provenance. */
	FLayoutId ArtifactId;

	/** Combined exact traversal/assertion artifacts. */
	FLayoutCertifiedChildWitnessSubsetArtifacts SubsetArtifacts;

	/** Human-readable producer/provenance label. */
	FString Provenance;
};

/** Explicit-only combined extraction for certified child traversal/assertion subsets. */
namespace LayoutCertifiedChildWitnessSubset
{
	/** Builds explicit artifacts from already-produced request-carried ids. Empty arrays mean absent artifacts. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCertifiedChildWitnessSubsetArtifacts BuildArtifactsFromExplicitIds(
		const TArray<FLayoutId>& SelectedTraversalCapabilityIds,
		const TArray<FLayoutId>& ParentAssertionSubsetIds);

	/** Builds an authoritative produced subset artifact; empty id arrays mean no artifact is emitted. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedArtifact(
		FLayoutId ArtifactId,
		const TArray<FLayoutId>& SelectedTraversalCapabilityIds,
		const TArray<FLayoutId>& ParentAssertionSubsetIds,
		FString Provenance,
		FLayoutProducedCertifiedChildWitnessSubsetArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Extracts exact subsets from an authoritative produced artifact; absent artifacts emit empty arrays and invalid artifacts fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryExtractWitnessSubsetsFromProducedArtifact(
		const FLayoutProducedCertifiedChildWitnessSubsetArtifact* ProducedArtifact,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		FLayoutCertifiedChildWitnessSubsets& OutSubsets,
		FString& OutFailureReason);

	/** Extracts exact subsets from separate authoritative artifacts; absent artifacts emit empty arrays and invalid artifacts fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
		const FLayoutChildTraversalWitnessMappingArtifact* TraversalWitnessArtifact,
		const FLayoutChildParentAssertionSubsetMappingArtifact* ParentAssertionSubsetArtifact,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		FLayoutCertifiedChildWitnessSubsets& OutSubsets,
		FString& OutFailureReason);

	/** Extracts exact subsets from request-carried authoritative artifact fields; unflagged arrays are ignored. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryExtractWitnessSubsetsFromRequestCarriedArtifacts(
		const FLayoutRegionSolveRequest& ChildRequest,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		FLayoutCertifiedChildWitnessSubsets& OutSubsets,
		FString& OutFailureReason);

	/** Extracts exact subsets from explicit artifacts; absent artifacts emit empty arrays and invalid artifacts fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryExtractWitnessSubsets(
		const FLayoutCertifiedChildWitnessSubsetArtifacts* Artifacts,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		FLayoutCertifiedChildWitnessSubsets& OutSubsets,
		FString& OutFailureReason);
}
