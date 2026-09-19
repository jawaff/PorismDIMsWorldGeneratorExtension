// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutCertifiedChildAssertionSubset.h"
#include "Layout/Async/LayoutCertifiedChildTraversalWitness.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** One explicit parent requirement to parent assertion id mapping emitted for a certified child branch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedRequirementToAssertionMapping
{
	/** Stable parent requirement id that was satisfied by the assertion. */
	FName RequirementId;

	/** Exact parent assertion id proving the requirement for this child branch. */
	FLayoutId AssertionId;
};

/** Produced selected traversal artifact emitted only by parent negotiation/prewarm for one certified child branch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedSelectedTraversalArtifact
{
	/** True only after an authoritative producer emitted explicit selected traversal ids. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics and replay. */
	FLayoutId ArtifactId;

	/** Stable selected child branch/scout id this artifact belongs to. */
	FLayoutId ChildScoutResultId;

	/** Exact selected traversal capability ids emitted by parent negotiation. */
	TArray<FLayoutId> SelectedTraversalCapabilityIds;
};

/** Produced requirement-to-assertion artifact emitted only by parent negotiation/prewarm for one certified child branch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedRequirementToAssertionArtifact
{
	/** True only after an authoritative producer emitted explicit requirement-to-assertion ids. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics and replay. */
	FLayoutId ArtifactId;

	/** Stable selected child branch/scout id this artifact belongs to. */
	FLayoutId ChildScoutResultId;

	/** Exact requirement-to-assertion mappings emitted by parent negotiation. */
	TArray<FLayoutProducedRequirementToAssertionMapping> Mappings;
};

/** Explicit parent-branch certificate input emitted at the parent negotiation selection point. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutParentBranchCertificateInput
{
	/** Stable certificate artifact id for diagnostics and replay. */
	FLayoutId ArtifactId;

	/** Parent proof/contract id that selected this child branch. */
	FLayoutId ParentContractId;

	/** Deterministic hash/fingerprint of the parent contract referenced by this certificate. */
	uint64 ParentContractHash = 0;

	/** Stable selected branch id owned by parent negotiation. */
	FLayoutId BranchId;

	/** Stable selected child scout/branch result id shared with child witness artifacts. */
	FLayoutId ChildScoutResultId;

	/** Stable child identity selected by parent negotiation. */
	FString StableChildId;

	/** Parent content-entry id that spawned or delegated this child branch. */
	FName SourceContentEntryId;

	/** Optional closure-span capability id selected by parent negotiation when child closure-span support is part of the branch. */
	FLayoutId SelectedClosureSpanCapabilityId;

	/** Optional endpoint ids selected by parent negotiation when endpoint capabilities are part of the branch. */
	TArray<FLayoutId> SelectedEndpointCapabilityIds;

	/** Optional seam capability ids selected by parent negotiation when seam capabilities are part of the branch. */
	TArray<FLayoutId> SelectedSeamCapabilityIds;

	/** Optional seam witness ids selected by parent negotiation when seam ownership/proof records are part of the branch. */
	TArray<FLayoutId> SelectedSeamWitnessIds;

	/** Optional vertical ids selected by parent negotiation when vertical capabilities are part of the branch. */
	TArray<FLayoutId> SelectedVerticalCapabilityIds;

	/** Optional traversal ids selected by parent negotiation when traversal capabilities are part of the branch. */
	TArray<FLayoutId> SelectedTraversalCapabilityIds;

	/** True only when the parent explicitly selected traversal ids that must appear in the certificate. */
	bool bHasSelectedTraversalIds = false;

	/** Feature requirement ids delegated by parent negotiation to this child branch. */
	TArray<FLayoutId> DelegatedFeatureRequirementIds;

	/** Closure requirement ids delegated by parent negotiation to this child branch. */
	TArray<FLayoutId> DelegatedClosureRequirementIds;

	/** Optional seam obligation witness ids delegated by parent negotiation. */
	TArray<FLayoutId> DelegatedSeamWitnessIds;

	/** Optional host VerticalAccess obligation witness ids delegated by parent negotiation. */
	TArray<FLayoutId> DelegatedHostVerticalAccessWitnessIds;

	/** Optional proof/assertion ids directly referenced by parent negotiation for diagnostics. */
	TArray<FLayoutId> ReferencedProofAssertionIds;
};

/** Worker/result-local parent branch certificate consumed by child handoff construction. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProducedParentBranchCertificateArtifact
{
	/** True only after parent negotiation emitted this certificate. */
	bool bHasProducedArtifact = false;

	/** Stable certificate artifact id for diagnostics and replay. */
	FLayoutId ArtifactId;

	/** Parent proof/contract id that selected this child branch. */
	FLayoutId ParentContractId;

	/** Deterministic hash/fingerprint of the parent contract referenced by this certificate. */
	uint64 ParentContractHash = 0;

	/** Stable selected branch id owned by parent negotiation. */
	FLayoutId BranchId;

	/** Stable selected child scout/branch result id shared with child witness artifacts. */
	FLayoutId ChildScoutResultId;

	/** Stable child identity selected by parent negotiation. */
	FString StableChildId;

	/** Parent content-entry id that spawned or delegated this child branch. */
	FName SourceContentEntryId;

	/** Selected traversal capability ids from the explicit selected-traversal artifact. */
	TArray<FLayoutId> SelectedTraversalCapabilityIds;

	/** Requirement-to-assertion mappings from the explicit requirement artifact. */
	TArray<FLayoutProducedRequirementToAssertionMapping> RequirementToAssertionMappings;

	/** Optional closure-span capability id selected by parent negotiation. */
	FLayoutId SelectedClosureSpanCapabilityId;

	/** Optional endpoint ids selected by parent negotiation. */
	TArray<FLayoutId> SelectedEndpointCapabilityIds;

	/** Optional seam capability ids selected by parent negotiation. */
	TArray<FLayoutId> SelectedSeamCapabilityIds;

	/** Optional seam witness ids selected by parent negotiation. */
	TArray<FLayoutId> SelectedSeamWitnessIds;

	/** Optional vertical ids selected by parent negotiation. */
	TArray<FLayoutId> SelectedVerticalCapabilityIds;

	/** Feature requirement ids delegated by parent negotiation to this child branch. */
	TArray<FLayoutId> DelegatedFeatureRequirementIds;

	/** Closure requirement ids delegated by parent negotiation to this child branch. */
	TArray<FLayoutId> DelegatedClosureRequirementIds;

	/** Seam obligation witness ids delegated by parent negotiation. */
	TArray<FLayoutId> DelegatedSeamWitnessIds;

	/** Host VerticalAccess obligation witness ids delegated by parent negotiation. */
	TArray<FLayoutId> DelegatedHostVerticalAccessWitnessIds;

	/** Optional proof/assertion ids directly referenced by parent negotiation for diagnostics. */
	TArray<FLayoutId> ReferencedProofAssertionIds;

	/** Deterministic fingerprint of selected branch fields plus parent contract id/hash. */
	uint64 CertificateInputHash = 0;
};

/** Explicit-only producer helpers for certified child witness artifacts. */
namespace LayoutCertifiedChildWitnessArtifactProducer
{
	/** Builds a produced selected-traversal artifact from explicit ids; missing/empty/duplicate ids fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildSelectedTraversalArtifact(
		FLayoutId ArtifactId,
		FLayoutId ChildScoutResultId,
		const TArray<FLayoutId>& SelectedTraversalCapabilityIds,
		FLayoutProducedSelectedTraversalArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced selected-traversal artifact from one explicit handoff-carried authoritative traversal payload. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildSelectedTraversalArtifactFromChildHandoff(
		FLayoutId ArtifactId,
		FLayoutId ChildScoutResultId,
		const FLayoutChildSolveHandoff& ChildHandoff,
		FLayoutProducedSelectedTraversalArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced selected-traversal artifact from one explicit request-carried authoritative traversal payload. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildSelectedTraversalArtifactFromChildRequest(
		FLayoutId ArtifactId,
		FLayoutId ChildScoutResultId,
		const FLayoutRegionSolveRequest& ChildRequest,
		FLayoutProducedSelectedTraversalArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced requirement-to-assertion artifact from explicit mappings; missing/empty/duplicate/missing-failed assertion ids fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRequirementToAssertionArtifact(
		FLayoutId ArtifactId,
		FLayoutId ChildScoutResultId,
		const TArray<FLayoutProducedRequirementToAssertionMapping>& Mappings,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		FLayoutProducedRequirementToAssertionArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a produced requirement-to-assertion artifact from one explicit handoff-carried authoritative mapping payload. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRequirementToAssertionArtifactFromChildHandoff(
		FLayoutId ArtifactId,
		FLayoutId ChildScoutResultId,
		const FLayoutChildSolveHandoff& ChildHandoff,
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		FLayoutProducedRequirementToAssertionArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Converts a produced selected-traversal artifact into the existing explicit traversal witness mapping artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildTraversalWitnessMappingArtifact(
		const FLayoutProducedSelectedTraversalArtifact* ProducedArtifact,
		FLayoutChildTraversalWitnessMappingArtifact& OutMappingArtifact,
		FString& OutFailureReason);

	/** Converts a produced requirement-to-assertion artifact into the existing explicit parent assertion subset mapping artifact. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildParentAssertionSubsetMappingArtifact(
		const FLayoutProducedRequirementToAssertionArtifact* ProducedArtifact,
		FLayoutChildParentAssertionSubsetMappingArtifact& OutMappingArtifact,
		FString& OutFailureReason);

	/** Builds a worker/result-local parent branch certificate from explicit selected witness artifacts only. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildParentBranchCertificateArtifact(
		const FLayoutParentBranchCertificateInput& Input,
		const FLayoutProducedSelectedTraversalArtifact* SelectedTraversalArtifact,
		const FLayoutProducedRequirementToAssertionArtifact* RequirementToAssertionArtifact,
		FLayoutProducedParentBranchCertificateArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Applies a produced parent branch certificate to a frozen child handoff without inventing traversal or assertion ids. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryApplyParentBranchCertificateToChildHandoff(
		const FLayoutProducedParentBranchCertificateArtifact& CertificateArtifact,
		FLayoutChildSolveHandoff& InOutChildHandoff,
		FString& OutFailureReason);
}
