// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutFrozenSubmissionDescriptorProducer.h"

/** Identifies which selected prewarm result shape is being sealed into a solve descriptor. */
enum class ELayoutRegionPrewarmDescriptorSourceKind : uint8
{
	RootScoutResult,
	ContinuationEdgeScoutResult,
	ChildScoutResult
};

/**
 * Generic pointer-free region prewarm descriptor input.
 *
 * Prewarm owns conversion from exactly one selected scout/branch result plus an
 * already-frozen snapshot into the descriptor artifact later consumed by solve.
 */
struct FLayoutRegionPrewarmDescriptorInput
{
	/** Selected result shape carried by this prewarm input. */
	ELayoutRegionPrewarmDescriptorSourceKind SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult;

	/** Stable produced descriptor artifact id for diagnostics. */
	FLayoutId ArtifactId;

	/** Descriptor id chosen before prewarm so later lifecycle lookup stays stable. */
	FLayoutId DescriptorId;

	/** Selected scout/result id for root, continuation edge, or child branch. */
	FLayoutId SelectedResultId;

	/** Region group, connector key, or parent branch key this descriptor belongs to. */
	uint64 RegionGroupId = 0;

	/** Generation expected by later submit lookup. */
	uint64 Generation = 0;

	/** Attempt index expected by later submit lookup. */
	int32 AttemptIndex = 0;

	/** World seed used by root prewarm to verify finalized solve seed. */
	int32 WorldSeed = 0;

	/** Expected solve seed for continuation and child prewarm descriptor verification. */
	int32 ExpectedSolveSeed = 0;

	/** Finalized site/endpoint/anchor selected before the descriptor is produced. */
	FIntVector FinalizedPrimaryBlockWorldPos = FIntVector::ZeroValue;

	/** Stable child key selected by parent negotiation for child prewarm descriptors. */
	FString ExpectedStableChildKey;

	/** Proof certificate id selected by parent negotiation for child prewarm descriptors. */
	FLayoutId ExpectedProofCertificateId;

	/** Already-frozen pointer-free snapshot/manifest basis; this producer never rebuilds it. */
	FLayoutPreSubmitFrozenSnapshot Snapshot;
};

/**
 * Batch C region prewarm producer facade.
 *
 * This keeps root, continuation, and child descriptor production on one generic
 * prewarm seam while delegating exact fail-closed validation to existing typed
 * descriptor producers.
 */
namespace LayoutRegionPrewarmDescriptorProducer
{
	/** Converts one selected prewarm result into one produced frozen descriptor artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedDescriptorArtifact(
		const FLayoutRegionPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Revalidates a produced descriptor artifact as the expected region kind before solve submission consumes its seed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryGetDescriptorSeedForSolveSubmission(
		ELayoutRegionPrewarmDescriptorSourceKind ExpectedSourceKind,
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);

	/** Revalidates a produced descriptor artifact as the expected root/continuation kind before manifest prewarm consumption. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryGetManifestPrewarmInputForSolveSubmission(
		ELayoutRegionPrewarmDescriptorSourceKind ExpectedSourceKind,
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutManifestPrewarmInput& OutInput,
		FString& OutFailureReason);
}
