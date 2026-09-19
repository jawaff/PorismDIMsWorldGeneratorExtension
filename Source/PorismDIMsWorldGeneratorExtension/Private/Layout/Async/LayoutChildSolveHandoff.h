// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Pointer-free child content metadata copied from the selected child content entry. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorkerChildContentMetadata
{
	/** Stable source content-entry id that authored this child handoff. */
	FName SourceContentEntryId;

	/** Soft path for the selected child profile so later game-thread tooling can rehydrate if needed. */
	FSoftObjectPath ChildProfilePath;

	/** Parent-relative placement zone selected for this child entry. */
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

	/** Parent-relative level placement policy selected for this child entry. */
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific parent-relative level used when LevelPlacementPolicy is SpecificLevel. */
	int32 SpecificLevel = 0;

	/** True when the selected child entry may be dropped during parent negotiation. */
	bool bOptional = false;

	/** True when the child may satisfy one host vertical-access requirement. */
	bool bContributesHostVerticalAccess = false;

	/** Feature tags this child entry may contribute to the parent when delegated. */
	FGameplayTagContainer ProvidedZoneFeatures;
};

/** Pointer-free parent requirement-to-assertion mapping certified before child dispatch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCertifiedChildRequirementToAssertionMapping
{
	/** Stable parent requirement id satisfied by the selected parent assertion. */
	FName RequirementId;

	/** Exact parent assertion id proving the requirement for this child branch. */
	FLayoutId AssertionId;
};

/** Exact pointer-free parent-selected witness data required before certified child dispatch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCertifiedChildWitnessBundle
{
	/** Source content-entry id selected by parent negotiation. */
	FName SourceContentEntryId;

	/** Stable child id selected by parent negotiation. */
	FLayoutId StableChildId;

	/** Stable parent branch id that owns this witness. */
	FLayoutId BranchId;

	/** Exact selected child closure-span capability id, when relevant. */
	FLayoutId SelectedClosureSpanCapabilityId;

	/** Exact selected child seam capability id, when relevant. */
	FLayoutId SelectedSeamCapabilityId;

	/** Exact selected child seam witness id, when relevant. */
	FLayoutId SelectedSeamWitnessId;

	/** Exact selected endpoint capability ids used by parent negotiation. */
	TArray<FLayoutId> SelectedEndpointCapabilityIds;

	/** Exact selected traversal capability ids used by parent negotiation. */
	TArray<FLayoutId> SelectedTraversalCapabilityIds;

	/** Exact selected vertical capability ids used by parent negotiation. */
	TArray<FLayoutId> SelectedVerticalCapabilityIds;

	/** Exact delegated feature requirement ids certified for this child. */
	TArray<FLayoutId> DelegatedFeatureRequirementIds;

	/** Exact delegated closure requirement ids certified for this child. */
	TArray<FLayoutId> DelegatedClosureRequirementIds;

	/** Exact delegated seam witness ids certified for this child. */
	TArray<FLayoutId> DelegatedSeamWitnessIds;

	/** Exact delegated junction witness ids certified for this child. */
	TArray<FLayoutId> DelegatedJunctionWitnessIds;

	/** Exact delegated host VerticalAccess witness ids certified for this child. */
	TArray<FLayoutId> DelegatedHostVerticalAccessWitnessIds;

	/** Exact child validation assertion ids used by parent negotiation for this certificate. */
	TArray<FLayoutId> AssertionIds;

	/** Exact parent validation assertion ids present on the negotiated proof request. */
	TArray<FLayoutId> ParentAssertionIds;

	/** Returns true when no exact witness content has been populated yet. */
	bool IsEmpty() const;
};

/** Stable proof certificate copied onto the child handoff before dispatch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildSolveCertificate
{
	/** Stable certificate id used for diagnostics and replay. */
	FLayoutId CertificateId;

	/** Deterministic hash of the frozen handoff inputs the certificate was built against. */
	uint64 InputHash = 0;
};

/** Frozen pointer-free child solve handoff emitted by parent negotiation before child dispatch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildSolveHandoff
{
	/** Parent region that negotiated this child handoff. */
	FString ParentRegionDebugPath;

	/** Child region that must execute this certified handoff. */
	FString ChildRegionDebugPath;

	/** Stable parent artifact or resume id used to correlate child completions. */
	FLayoutId ParentArtifactOrResumeId;

	/** Stable child branch key chosen by parent negotiation. */
	FString StableChildKey;

	/** Deterministic parent-owned child branch attempt index. */
	int32 AttemptIndex = 0;

	/** Parent- or root-relative child region cell offset selected during negotiation. */
	FIntVector ChildRegionCellOffset = FIntVector::ZeroValue;

	/** True when parent negotiation also fixed a block-world anchor for this child branch. */
	bool bHasChildBlockWorldAnchor = false;

	/** Optional block-world anchor selected during parent negotiation. */
	FIntVector ChildBlockWorldAnchor = FIntVector::ZeroValue;

	/** Flattened child content-entry metadata copied from authoring. */
	FLayoutWorkerChildContentMetadata ContentMetadata;

	/** Exact child capability proof selected during parent negotiation. */
	FLayoutChildCapabilityEnvelope ChildCapabilityEnvelope;

	/** Direct parent-to-child commitments frozen before child solve starts. */
	FLayoutDirectChildRegionCommitment DirectCommitment;

	/** Additional protected parent structural cells reserved for this child branch. */
	TArray<FIntVector> ProtectedStructuralCells;

	/** Additional route constraints the child must preserve. */
	TArray<FLayoutRouteConstraintRecord> RequiredRouteConstraints;

	/** Parent zone feature requirements delegated to this child branch. */
	TArray<FLayoutId> DelegatedZoneFeatureRequirementIds;

	/** Parent closure requirements delegated to this child branch. */
	TArray<FLayoutId> DelegatedClosureRequirementIds;

	/** True when CertifiedSelectedTraversalCapabilityIds came from an authoritative selected-traversal artifact. */
	bool bHasCertifiedSelectedTraversalCapabilityArtifact = false;

	/** Exact request-carried traversal capability ids selected by an authoritative parent negotiation artifact. */
	TArray<FLayoutId> CertifiedSelectedTraversalCapabilityIds;

	/** True when CertifiedParentAssertionSubsetIds came from an authoritative requirement-to-assertion artifact. */
	bool bHasCertifiedParentAssertionSubsetArtifact = false;

	/** Exact request-carried parent assertion ids selected by an authoritative requirement-to-assertion artifact. */
	TArray<FLayoutId> CertifiedParentAssertionSubsetIds;

	/** True when CertifiedRequirementToAssertionMappings came from an authoritative requirement-to-assertion artifact. */
	bool bHasCertifiedRequirementToAssertionArtifact = false;

	/** Exact request-carried requirement-to-assertion mappings selected by authoritative parent negotiation. */
	TArray<FLayoutCertifiedChildRequirementToAssertionMapping> CertifiedRequirementToAssertionMappings;

	/** True when dispatch must reject this handoff unless exact child witness data is populated. */
	bool bRequiresCertifiedWitnessBundle = false;

	/** Exact parent-selected witness bundle used to build a certified child dispatch. */
	FLayoutCertifiedChildWitnessBundle CertifiedWitnessBundle;

	/** Stable proof certificate proving this exact handoff was certified before dispatch. */
	FLayoutChildSolveCertificate ProofCertificate;

	/** Returns deterministic hash of the frozen handoff inputs that certification depends on. */
	uint64 ComputeDeterministicInputHash() const;

	/** Refreshes ProofCertificate from the current frozen handoff inputs. */
	void RefreshProofCertificate(FLayoutId InCertificateId = NAME_None);

	/** Returns true when this handoff stays pointer-free and self-consistent for worker execution. */
	bool ValidateNoLiveObjectCarriers(FString& OutFailureReason) const;
};
