// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutCertifiedChildWitnessArtifactProducer.h"

namespace
{
	const FLayoutValidationAssertionRecord* FindWitnessProducerAssertionById(
		const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
		const FLayoutId AssertionId)
	{
		return ParentAssertions.FindByPredicate([AssertionId](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == AssertionId;
		});
	}

	bool ValidateProducerIds(const FLayoutId ArtifactId, const FLayoutId ChildScoutResultId, const TCHAR* Context, FString& OutFailureReason)
	{
		if (ArtifactId.IsNone())
		{
			OutFailureReason = FString::Printf(TEXT("%s requires a produced artifact id."), Context);
			return false;
		}
		if (ChildScoutResultId.IsNone())
		{
			OutFailureReason = FString::Printf(TEXT("%s requires a child scout result id."), Context);
			return false;
		}
		return true;
	}

	bool ValidateOptionalNameArray(const TArray<FLayoutId>& Ids, const TCHAR* Context, FString& OutFailureReason)
	{
		TArray<FLayoutId> UniqueIds;
		for (const FLayoutId Id : Ids)
		{
			if (Id.IsNone())
			{
				OutFailureReason = FString::Printf(TEXT("%s contains an empty id."), Context);
				return false;
			}
			if (UniqueIds.Contains(Id))
			{
				OutFailureReason = FString::Printf(TEXT("%s duplicates id '%s'."), Context, *Id.ToString());
				return false;
			}
			UniqueIds.Add(Id);
		}
		return true;
	}

	uint64 HashNameIntoCertificate(const uint64 CurrentHash, const FLayoutId Id)
	{
		return HashCombineFast(CurrentHash, static_cast<uint64>(GetTypeHash(Id)));
	}

	uint64 HashStringIntoCertificate(const uint64 CurrentHash, const FString& Value)
	{
		return HashCombineFast(CurrentHash, static_cast<uint64>(GetTypeHash(Value)));
	}

	uint64 HashNameArrayIntoCertificate(uint64 CurrentHash, const TArray<FLayoutId>& Ids)
	{
		CurrentHash = HashCombineFast(CurrentHash, static_cast<uint64>(Ids.Num()));
		for (const FLayoutId Id : Ids)
		{
			CurrentHash = HashNameIntoCertificate(CurrentHash, Id);
		}
		return CurrentHash;
	}

	uint64 HashRequirementMappingsIntoCertificate(uint64 CurrentHash, const TArray<FLayoutProducedRequirementToAssertionMapping>& Mappings)
	{
		CurrentHash = HashCombineFast(CurrentHash, static_cast<uint64>(Mappings.Num()));
		for (const FLayoutProducedRequirementToAssertionMapping& Mapping : Mappings)
		{
			CurrentHash = HashNameIntoCertificate(CurrentHash, Mapping.RequirementId);
			CurrentHash = HashNameIntoCertificate(CurrentHash, Mapping.AssertionId);
		}
		return CurrentHash;
	}
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
	const FLayoutId ArtifactId,
	const FLayoutId ChildScoutResultId,
	const TArray<FLayoutId>& SelectedTraversalCapabilityIds,
	FLayoutProducedSelectedTraversalArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedSelectedTraversalArtifact();
	OutFailureReason.Reset();
	if (!ValidateProducerIds(ArtifactId, ChildScoutResultId, TEXT("Selected traversal produced artifact"), OutFailureReason))
	{
		return false;
	}
	if (SelectedTraversalCapabilityIds.IsEmpty())
	{
		OutFailureReason = TEXT("Selected traversal produced artifact requires explicit selected traversal capability ids.");
		return false;
	}

	TArray<FLayoutId> UniqueIds;
	for (const FLayoutId CapabilityId : SelectedTraversalCapabilityIds)
	{
		if (CapabilityId.IsNone())
		{
			OutFailureReason = TEXT("Selected traversal produced artifact contains an empty capability id.");
			return false;
		}
		if (UniqueIds.Contains(CapabilityId))
		{
			OutFailureReason = FString::Printf(TEXT("Selected traversal produced artifact duplicates capability id '%s'."), *CapabilityId.ToString());
			return false;
		}
		UniqueIds.Add(CapabilityId);
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.ChildScoutResultId = ChildScoutResultId;
	OutArtifact.SelectedTraversalCapabilityIds = SelectedTraversalCapabilityIds;
	return true;
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifactFromChildHandoff(
	const FLayoutId ArtifactId,
	const FLayoutId ChildScoutResultId,
	const FLayoutChildSolveHandoff& ChildHandoff,
	FLayoutProducedSelectedTraversalArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedSelectedTraversalArtifact();
	OutFailureReason.Reset();
	if (!ChildHandoff.bHasCertifiedSelectedTraversalCapabilityArtifact)
	{
		OutFailureReason = TEXT("Selected traversal handoff source producer requires an authoritative traversal artifact flag.");
		return false;
	}
	return TryBuildSelectedTraversalArtifact(
		ArtifactId,
		ChildScoutResultId,
		ChildHandoff.CertifiedSelectedTraversalCapabilityIds,
		OutArtifact,
		OutFailureReason);
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifactFromChildRequest(
	const FLayoutId ArtifactId,
	const FLayoutId ChildScoutResultId,
	const FLayoutRegionSolveRequest& ChildRequest,
	FLayoutProducedSelectedTraversalArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedSelectedTraversalArtifact();
	OutFailureReason.Reset();
	if (!ChildRequest.bHasCertifiedSelectedTraversalCapabilityArtifact)
	{
		OutFailureReason = TEXT("Selected traversal source producer requires a request-carried authoritative traversal artifact flag.");
		return false;
	}
	return TryBuildSelectedTraversalArtifact(
		ArtifactId,
		ChildScoutResultId,
		ChildRequest.CertifiedSelectedTraversalCapabilityIds,
		OutArtifact,
		OutFailureReason);
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifact(
	const FLayoutId ArtifactId,
	const FLayoutId ChildScoutResultId,
	const TArray<FLayoutProducedRequirementToAssertionMapping>& Mappings,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	FLayoutProducedRequirementToAssertionArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedRequirementToAssertionArtifact();
	OutFailureReason.Reset();
	if (!ValidateProducerIds(ArtifactId, ChildScoutResultId, TEXT("Requirement-to-assertion produced artifact"), OutFailureReason))
	{
		return false;
	}
	if (Mappings.IsEmpty())
	{
		OutFailureReason = TEXT("Requirement-to-assertion produced artifact requires explicit mappings.");
		return false;
	}

	TArray<FName> RequirementIds;
	TArray<FLayoutId> AssertionIds;
	for (const FLayoutProducedRequirementToAssertionMapping& Mapping : Mappings)
	{
		if (Mapping.RequirementId.IsNone())
		{
			OutFailureReason = TEXT("Requirement-to-assertion produced artifact contains an empty requirement id.");
			return false;
		}
		if (Mapping.AssertionId.IsNone())
		{
			OutFailureReason = TEXT("Requirement-to-assertion produced artifact contains an empty assertion id.");
			return false;
		}
		if (RequirementIds.Contains(Mapping.RequirementId))
		{
			OutFailureReason = FString::Printf(TEXT("Requirement-to-assertion produced artifact duplicates requirement id '%s'."), *Mapping.RequirementId.ToString());
			return false;
		}
		if (AssertionIds.Contains(Mapping.AssertionId))
		{
			OutFailureReason = FString::Printf(TEXT("Requirement-to-assertion produced artifact duplicates assertion id '%s'."), *Mapping.AssertionId.ToString());
			return false;
		}
		const FLayoutValidationAssertionRecord* const ParentAssertion = FindWitnessProducerAssertionById(ParentAssertions, Mapping.AssertionId);
		if (ParentAssertion == nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Requirement-to-assertion produced artifact references missing assertion id '%s'."), *Mapping.AssertionId.ToString());
			return false;
		}
		if (!ParentAssertion->bPassed)
		{
			OutFailureReason = FString::Printf(TEXT("Requirement-to-assertion produced artifact references failed assertion id '%s'."), *Mapping.AssertionId.ToString());
			return false;
		}
		RequirementIds.Add(Mapping.RequirementId);
		AssertionIds.Add(Mapping.AssertionId);
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.ChildScoutResultId = ChildScoutResultId;
	OutArtifact.Mappings = Mappings;
	return true;
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifactFromChildHandoff(
	const FLayoutId ArtifactId,
	const FLayoutId ChildScoutResultId,
	const FLayoutChildSolveHandoff& ChildHandoff,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	FLayoutProducedRequirementToAssertionArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedRequirementToAssertionArtifact();
	OutFailureReason.Reset();
	if (!ChildHandoff.bHasCertifiedRequirementToAssertionArtifact)
	{
		OutFailureReason = TEXT("Requirement-to-assertion handoff source producer requires an authoritative requirement-to-assertion artifact flag.");
		return false;
	}

	TArray<FLayoutProducedRequirementToAssertionMapping> Mappings;
	Mappings.Reserve(ChildHandoff.CertifiedRequirementToAssertionMappings.Num());
	for (const FLayoutCertifiedChildRequirementToAssertionMapping& HandoffMapping : ChildHandoff.CertifiedRequirementToAssertionMappings)
	{
		FLayoutProducedRequirementToAssertionMapping Mapping;
		Mapping.RequirementId = HandoffMapping.RequirementId;
		Mapping.AssertionId = HandoffMapping.AssertionId;
		Mappings.Add(Mapping);
	}

	return TryBuildRequirementToAssertionArtifact(
		ArtifactId,
		ChildScoutResultId,
		Mappings,
		ParentAssertions,
		OutArtifact,
		OutFailureReason);
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildTraversalWitnessMappingArtifact(
	const FLayoutProducedSelectedTraversalArtifact* ProducedArtifact,
	FLayoutChildTraversalWitnessMappingArtifact& OutMappingArtifact,
	FString& OutFailureReason)
{
	OutMappingArtifact = FLayoutChildTraversalWitnessMappingArtifact();
	OutFailureReason.Reset();
	if (ProducedArtifact == nullptr || !ProducedArtifact->bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Selected traversal witness mapping requires an explicit produced selected-traversal artifact.");
		return false;
	}
	FLayoutProducedSelectedTraversalArtifact ValidatedArtifact;
	if (!TryBuildSelectedTraversalArtifact(
			ProducedArtifact->ArtifactId,
			ProducedArtifact->ChildScoutResultId,
			ProducedArtifact->SelectedTraversalCapabilityIds,
			ValidatedArtifact,
			OutFailureReason))
	{
		return false;
	}
	OutMappingArtifact = LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
		ValidatedArtifact.SelectedTraversalCapabilityIds);
	return true;
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentAssertionSubsetMappingArtifact(
	const FLayoutProducedRequirementToAssertionArtifact* ProducedArtifact,
	FLayoutChildParentAssertionSubsetMappingArtifact& OutMappingArtifact,
	FString& OutFailureReason)
{
	OutMappingArtifact = FLayoutChildParentAssertionSubsetMappingArtifact();
	OutFailureReason.Reset();
	if (ProducedArtifact == nullptr || !ProducedArtifact->bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Parent assertion subset mapping requires an explicit produced requirement-to-assertion artifact.");
		return false;
	}
	if (ProducedArtifact->ArtifactId.IsNone() || ProducedArtifact->ChildScoutResultId.IsNone() || ProducedArtifact->Mappings.IsEmpty())
	{
		OutFailureReason = TEXT("Parent assertion subset mapping rejected an incomplete produced requirement-to-assertion artifact.");
		return false;
	}

	TArray<FLayoutId> AssertionIds;
	for (const FLayoutProducedRequirementToAssertionMapping& Mapping : ProducedArtifact->Mappings)
	{
		if (Mapping.RequirementId.IsNone() || Mapping.AssertionId.IsNone())
		{
			OutFailureReason = TEXT("Parent assertion subset mapping rejected an invalid requirement-to-assertion mapping.");
			return false;
		}
		if (AssertionIds.Contains(Mapping.AssertionId))
		{
			OutFailureReason = FString::Printf(TEXT("Parent assertion subset mapping duplicates assertion id '%s'."), *Mapping.AssertionId.ToString());
			return false;
		}
		AssertionIds.Add(Mapping.AssertionId);
	}

	OutMappingArtifact = LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact(AssertionIds);
	return true;
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
	const FLayoutParentBranchCertificateInput& Input,
	const FLayoutProducedSelectedTraversalArtifact* SelectedTraversalArtifact,
	const FLayoutProducedRequirementToAssertionArtifact* RequirementToAssertionArtifact,
	FLayoutProducedParentBranchCertificateArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedParentBranchCertificateArtifact();
	OutFailureReason.Reset();
	if (Input.ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Parent branch certificate requires an artifact id.");
		return false;
	}
	if (Input.ParentContractId.IsNone())
	{
		OutFailureReason = TEXT("Parent branch certificate requires a parent contract id.");
		return false;
	}
	if (Input.ParentContractHash == 0)
	{
		OutFailureReason = TEXT("Parent branch certificate requires a parent contract hash.");
		return false;
	}
	if (Input.BranchId.IsNone() || Input.ChildScoutResultId.IsNone())
	{
		OutFailureReason = TEXT("Parent branch certificate requires branch and child scout result ids.");
		return false;
	}
	if (Input.StableChildId.IsEmpty())
	{
		OutFailureReason = TEXT("Parent branch certificate requires a stable child id.");
		return false;
	}
	if (Input.SourceContentEntryId.IsNone())
	{
		OutFailureReason = TEXT("Parent branch certificate requires a source content-entry id.");
		return false;
	}
	if (!ValidateOptionalNameArray(Input.SelectedEndpointCapabilityIds, TEXT("Parent branch certificate endpoint ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.SelectedSeamCapabilityIds, TEXT("Parent branch certificate seam capability ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.SelectedSeamWitnessIds, TEXT("Parent branch certificate seam witness ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.SelectedVerticalCapabilityIds, TEXT("Parent branch certificate vertical ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.DelegatedFeatureRequirementIds, TEXT("Parent branch certificate delegated feature requirement ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.DelegatedClosureRequirementIds, TEXT("Parent branch certificate delegated closure requirement ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.DelegatedSeamWitnessIds, TEXT("Parent branch certificate delegated seam witness ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.DelegatedHostVerticalAccessWitnessIds, TEXT("Parent branch certificate delegated host VerticalAccess witness ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(Input.ReferencedProofAssertionIds, TEXT("Parent branch certificate proof/assertion ids"), OutFailureReason))
	{
		return false;
	}
	if (Input.SelectedSeamCapabilityIds.Num() != Input.SelectedSeamWitnessIds.Num())
	{
		OutFailureReason = TEXT("Parent branch certificate rejected unmatched selected seam capability and witness ids.");
		return false;
	}

	if (Input.bHasSelectedTraversalIds && SelectedTraversalArtifact == nullptr)
	{
		OutFailureReason = TEXT("Selected traversal ids require an explicit traversal artifact.");
		return false;
	}

	TArray<FLayoutId> SelectedTraversalCapabilityIds;
	if (SelectedTraversalArtifact != nullptr)
	{
		FLayoutProducedSelectedTraversalArtifact ValidatedTraversalArtifact;
		if (!TryBuildSelectedTraversalArtifact(
				SelectedTraversalArtifact->ArtifactId,
				SelectedTraversalArtifact->ChildScoutResultId,
				SelectedTraversalArtifact->SelectedTraversalCapabilityIds,
				ValidatedTraversalArtifact,
				OutFailureReason))
		{
			return false;
		}
		if (ValidatedTraversalArtifact.ChildScoutResultId != Input.ChildScoutResultId)
		{
			OutFailureReason = TEXT("Parent branch certificate rejected selected traversal artifact for a different child scout result.");
			return false;
		}
		SelectedTraversalCapabilityIds = ValidatedTraversalArtifact.SelectedTraversalCapabilityIds;
	}

	TArray<FLayoutProducedRequirementToAssertionMapping> RequirementToAssertionMappings;
	if (RequirementToAssertionArtifact != nullptr)
	{
		if (!RequirementToAssertionArtifact->bHasProducedArtifact
			|| RequirementToAssertionArtifact->ArtifactId.IsNone()
			|| RequirementToAssertionArtifact->ChildScoutResultId.IsNone()
			|| RequirementToAssertionArtifact->Mappings.IsEmpty())
		{
			OutFailureReason = TEXT("Parent branch certificate rejected an incomplete requirement-to-assertion artifact.");
			return false;
		}
		if (RequirementToAssertionArtifact->ChildScoutResultId != Input.ChildScoutResultId)
		{
			OutFailureReason = TEXT("Parent branch certificate rejected requirement-to-assertion artifact for a different child scout result.");
			return false;
		}
		TArray<FName> RequirementIds;
		TArray<FLayoutId> AssertionIds;
		for (const FLayoutProducedRequirementToAssertionMapping& Mapping : RequirementToAssertionArtifact->Mappings)
		{
			if (Mapping.RequirementId.IsNone() || Mapping.AssertionId.IsNone())
			{
				OutFailureReason = TEXT("Parent branch certificate rejected an invalid requirement-to-assertion mapping.");
				return false;
			}
			if (!Input.ReferencedProofAssertionIds.Contains(Mapping.AssertionId))
			{
				OutFailureReason = TEXT("Parent branch certificate rejected requirement-to-assertion mapping without referenced proof assertion id.");
				return false;
			}
			if (RequirementIds.Contains(Mapping.RequirementId) || AssertionIds.Contains(Mapping.AssertionId))
			{
				OutFailureReason = TEXT("Parent branch certificate rejected duplicate requirement-to-assertion mappings.");
				return false;
			}
			RequirementIds.Add(Mapping.RequirementId);
			AssertionIds.Add(Mapping.AssertionId);
		}
		RequirementToAssertionMappings = RequirementToAssertionArtifact->Mappings;
	}

	uint64 CertificateInputHash = HashNameIntoCertificate(0, Input.ParentContractId);
	CertificateInputHash = HashCombineFast(CertificateInputHash, Input.ParentContractHash);
	CertificateInputHash = HashNameIntoCertificate(CertificateInputHash, Input.BranchId);
	CertificateInputHash = HashNameIntoCertificate(CertificateInputHash, Input.ChildScoutResultId);
	CertificateInputHash = HashStringIntoCertificate(CertificateInputHash, Input.StableChildId);
	CertificateInputHash = HashNameIntoCertificate(CertificateInputHash, Input.SourceContentEntryId);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, SelectedTraversalCapabilityIds);
	CertificateInputHash = HashRequirementMappingsIntoCertificate(CertificateInputHash, RequirementToAssertionMappings);
	CertificateInputHash = HashNameIntoCertificate(CertificateInputHash, Input.SelectedClosureSpanCapabilityId);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.SelectedEndpointCapabilityIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.SelectedSeamCapabilityIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.SelectedSeamWitnessIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.SelectedVerticalCapabilityIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.DelegatedFeatureRequirementIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.DelegatedClosureRequirementIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.DelegatedSeamWitnessIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.DelegatedHostVerticalAccessWitnessIds);
	CertificateInputHash = HashNameArrayIntoCertificate(CertificateInputHash, Input.ReferencedProofAssertionIds);

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = Input.ArtifactId;
	OutArtifact.ParentContractId = Input.ParentContractId;
	OutArtifact.ParentContractHash = Input.ParentContractHash;
	OutArtifact.BranchId = Input.BranchId;
	OutArtifact.ChildScoutResultId = Input.ChildScoutResultId;
	OutArtifact.StableChildId = Input.StableChildId;
	OutArtifact.SourceContentEntryId = Input.SourceContentEntryId;
	OutArtifact.SelectedTraversalCapabilityIds = MoveTemp(SelectedTraversalCapabilityIds);
	OutArtifact.RequirementToAssertionMappings = MoveTemp(RequirementToAssertionMappings);
	OutArtifact.SelectedClosureSpanCapabilityId = Input.SelectedClosureSpanCapabilityId;
	OutArtifact.SelectedEndpointCapabilityIds = Input.SelectedEndpointCapabilityIds;
	OutArtifact.SelectedSeamCapabilityIds = Input.SelectedSeamCapabilityIds;
	OutArtifact.SelectedSeamWitnessIds = Input.SelectedSeamWitnessIds;
	OutArtifact.SelectedVerticalCapabilityIds = Input.SelectedVerticalCapabilityIds;
	OutArtifact.DelegatedFeatureRequirementIds = Input.DelegatedFeatureRequirementIds;
	OutArtifact.DelegatedClosureRequirementIds = Input.DelegatedClosureRequirementIds;
	OutArtifact.DelegatedSeamWitnessIds = Input.DelegatedSeamWitnessIds;
	OutArtifact.DelegatedHostVerticalAccessWitnessIds = Input.DelegatedHostVerticalAccessWitnessIds;
	OutArtifact.ReferencedProofAssertionIds = Input.ReferencedProofAssertionIds;
	OutArtifact.CertificateInputHash = CertificateInputHash;
	return true;
}

bool LayoutCertifiedChildWitnessArtifactProducer::TryApplyParentBranchCertificateToChildHandoff(
	const FLayoutProducedParentBranchCertificateArtifact& CertificateArtifact,
	FLayoutChildSolveHandoff& InOutChildHandoff,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!CertificateArtifact.bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Child handoff requires a produced parent branch certificate artifact.");
		return false;
	}
	if (CertificateArtifact.ArtifactId.IsNone()
		|| CertificateArtifact.ParentContractId.IsNone()
		|| CertificateArtifact.ParentContractHash == 0
		|| CertificateArtifact.BranchId.IsNone()
		|| CertificateArtifact.ChildScoutResultId.IsNone()
		|| CertificateArtifact.StableChildId.IsEmpty()
		|| CertificateArtifact.SourceContentEntryId.IsNone()
		|| CertificateArtifact.CertificateInputHash == 0)
	{
		OutFailureReason = TEXT("Child handoff rejected incomplete parent branch certificate artifact.");
		return false;
	}
	if (InOutChildHandoff.StableChildKey.IsEmpty())
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate because the handoff is missing stable child identity.");
		return false;
	}
	if (InOutChildHandoff.StableChildKey != CertificateArtifact.StableChildId)
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate for a different stable child id.");
		return false;
	}
	if (InOutChildHandoff.ContentMetadata.SourceContentEntryId.IsNone())
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate because the handoff is missing source content-entry identity.");
		return false;
	}
	if (InOutChildHandoff.ContentMetadata.SourceContentEntryId != CertificateArtifact.SourceContentEntryId)
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate for a different source content-entry id.");
		return false;
	}
	if (!ValidateOptionalNameArray(CertificateArtifact.SelectedTraversalCapabilityIds, TEXT("Parent branch certificate traversal ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.SelectedEndpointCapabilityIds, TEXT("Parent branch certificate endpoint ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.SelectedSeamCapabilityIds, TEXT("Parent branch certificate seam capability ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.SelectedSeamWitnessIds, TEXT("Parent branch certificate seam witness ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.SelectedVerticalCapabilityIds, TEXT("Parent branch certificate vertical ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.DelegatedFeatureRequirementIds, TEXT("Parent branch certificate delegated feature requirement ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.DelegatedClosureRequirementIds, TEXT("Parent branch certificate delegated closure requirement ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.DelegatedSeamWitnessIds, TEXT("Parent branch certificate delegated seam witness ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.DelegatedHostVerticalAccessWitnessIds, TEXT("Parent branch certificate delegated host VerticalAccess witness ids"), OutFailureReason)
		|| !ValidateOptionalNameArray(CertificateArtifact.ReferencedProofAssertionIds, TEXT("Parent branch certificate proof/assertion ids"), OutFailureReason))
	{
		return false;
	}
	if (CertificateArtifact.SelectedSeamCapabilityIds.Num() > 1)
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate with multiple selected seam capability ids; the current witness bundle supports one selected seam capability id.");
		return false;
	}
	if (CertificateArtifact.SelectedSeamWitnessIds.Num() > 1)
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate with multiple selected seam witness ids; the current witness bundle supports one selected seam witness id.");
		return false;
	}
	if (CertificateArtifact.SelectedSeamCapabilityIds.Num() != CertificateArtifact.SelectedSeamWitnessIds.Num())
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate with unmatched selected seam capability and witness ids.");
		return false;
	}

	uint64 ExpectedCertificateInputHash = HashNameIntoCertificate(0, CertificateArtifact.ParentContractId);
	ExpectedCertificateInputHash = HashCombineFast(ExpectedCertificateInputHash, CertificateArtifact.ParentContractHash);
	ExpectedCertificateInputHash = HashNameIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.BranchId);
	ExpectedCertificateInputHash = HashNameIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.ChildScoutResultId);
	ExpectedCertificateInputHash = HashStringIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.StableChildId);
	ExpectedCertificateInputHash = HashNameIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SourceContentEntryId);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SelectedTraversalCapabilityIds);
	ExpectedCertificateInputHash = HashRequirementMappingsIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.RequirementToAssertionMappings);
	ExpectedCertificateInputHash = HashNameIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SelectedClosureSpanCapabilityId);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SelectedEndpointCapabilityIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SelectedSeamCapabilityIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SelectedSeamWitnessIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.SelectedVerticalCapabilityIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.DelegatedFeatureRequirementIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.DelegatedClosureRequirementIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.DelegatedSeamWitnessIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.DelegatedHostVerticalAccessWitnessIds);
	ExpectedCertificateInputHash = HashNameArrayIntoCertificate(ExpectedCertificateInputHash, CertificateArtifact.ReferencedProofAssertionIds);
	if (CertificateArtifact.CertificateInputHash != ExpectedCertificateInputHash)
	{
		OutFailureReason = TEXT("Child handoff rejected parent branch certificate with a stale selected-field hash.");
		return false;
	}

	TArray<FLayoutCertifiedChildRequirementToAssertionMapping> HandoffMappings;
	TArray<FLayoutId> ParentAssertionSubsetIds;
	TArray<FName> RequirementIds;
	HandoffMappings.Reserve(CertificateArtifact.RequirementToAssertionMappings.Num());
	ParentAssertionSubsetIds.Reserve(CertificateArtifact.RequirementToAssertionMappings.Num());
	for (const FLayoutProducedRequirementToAssertionMapping& Mapping : CertificateArtifact.RequirementToAssertionMappings)
	{
		if (Mapping.RequirementId.IsNone() || Mapping.AssertionId.IsNone())
		{
			OutFailureReason = TEXT("Child handoff rejected invalid certificate requirement-to-assertion mapping.");
			return false;
		}
		if (!CertificateArtifact.ReferencedProofAssertionIds.Contains(Mapping.AssertionId))
		{
			OutFailureReason = TEXT("Child handoff rejected certificate requirement-to-assertion mapping without referenced proof assertion id.");
			return false;
		}
		if (RequirementIds.Contains(Mapping.RequirementId) || ParentAssertionSubsetIds.Contains(Mapping.AssertionId))
		{
			OutFailureReason = TEXT("Child handoff rejected duplicate certificate requirement-to-assertion mappings.");
			return false;
		}
		RequirementIds.Add(Mapping.RequirementId);
		ParentAssertionSubsetIds.Add(Mapping.AssertionId);

		FLayoutCertifiedChildRequirementToAssertionMapping HandoffMapping;
		HandoffMapping.RequirementId = Mapping.RequirementId;
		HandoffMapping.AssertionId = Mapping.AssertionId;
		HandoffMappings.Add(HandoffMapping);
	}

	InOutChildHandoff.bHasCertifiedSelectedTraversalCapabilityArtifact = !CertificateArtifact.SelectedTraversalCapabilityIds.IsEmpty();
	InOutChildHandoff.CertifiedSelectedTraversalCapabilityIds = CertificateArtifact.SelectedTraversalCapabilityIds;
	InOutChildHandoff.bHasCertifiedRequirementToAssertionArtifact = !HandoffMappings.IsEmpty();
	InOutChildHandoff.CertifiedRequirementToAssertionMappings = MoveTemp(HandoffMappings);
	InOutChildHandoff.bHasCertifiedParentAssertionSubsetArtifact = !ParentAssertionSubsetIds.IsEmpty();
	InOutChildHandoff.CertifiedParentAssertionSubsetIds = ParentAssertionSubsetIds;
	InOutChildHandoff.DelegatedZoneFeatureRequirementIds = CertificateArtifact.DelegatedFeatureRequirementIds;
	InOutChildHandoff.DelegatedClosureRequirementIds = CertificateArtifact.DelegatedClosureRequirementIds;
	InOutChildHandoff.CertifiedWitnessBundle.SourceContentEntryId = CertificateArtifact.SourceContentEntryId;
	InOutChildHandoff.CertifiedWitnessBundle.StableChildId = FLayoutId(*CertificateArtifact.StableChildId);
	InOutChildHandoff.CertifiedWitnessBundle.BranchId = CertificateArtifact.BranchId;
	InOutChildHandoff.CertifiedWitnessBundle.SelectedClosureSpanCapabilityId = CertificateArtifact.SelectedClosureSpanCapabilityId;
	InOutChildHandoff.CertifiedWitnessBundle.SelectedEndpointCapabilityIds = CertificateArtifact.SelectedEndpointCapabilityIds;
	InOutChildHandoff.CertifiedWitnessBundle.SelectedTraversalCapabilityIds = CertificateArtifact.SelectedTraversalCapabilityIds;
	InOutChildHandoff.CertifiedWitnessBundle.SelectedVerticalCapabilityIds = CertificateArtifact.SelectedVerticalCapabilityIds;
	InOutChildHandoff.CertifiedWitnessBundle.DelegatedFeatureRequirementIds = CertificateArtifact.DelegatedFeatureRequirementIds;
	InOutChildHandoff.CertifiedWitnessBundle.DelegatedClosureRequirementIds = CertificateArtifact.DelegatedClosureRequirementIds;
	InOutChildHandoff.CertifiedWitnessBundle.DelegatedSeamWitnessIds = CertificateArtifact.DelegatedSeamWitnessIds;
	InOutChildHandoff.CertifiedWitnessBundle.DelegatedHostVerticalAccessWitnessIds = CertificateArtifact.DelegatedHostVerticalAccessWitnessIds;
	InOutChildHandoff.CertifiedWitnessBundle.ParentAssertionIds = ParentAssertionSubsetIds;
	if (CertificateArtifact.SelectedSeamCapabilityIds.Num() == 1)
	{
		InOutChildHandoff.CertifiedWitnessBundle.SelectedSeamCapabilityId = CertificateArtifact.SelectedSeamCapabilityIds[0];
	}
	if (CertificateArtifact.SelectedSeamWitnessIds.Num() == 1)
	{
		InOutChildHandoff.CertifiedWitnessBundle.SelectedSeamWitnessId = CertificateArtifact.SelectedSeamWitnessIds[0];
	}
	InOutChildHandoff.RefreshProofCertificate(CertificateArtifact.ArtifactId);
	return true;
}
