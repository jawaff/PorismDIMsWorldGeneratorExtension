// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutCertifiedChildWitnessSubset.h"

#include "Layout/Solver/LayoutProfileSolver.h"

FLayoutCertifiedChildWitnessSubsetArtifacts LayoutCertifiedChildWitnessSubset::BuildArtifactsFromExplicitIds(
	const TArray<FLayoutId>& SelectedTraversalCapabilityIds,
	const TArray<FLayoutId>& ParentAssertionSubsetIds)
{
	FLayoutCertifiedChildWitnessSubsetArtifacts Artifacts;
	Artifacts.TraversalWitness = LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
		SelectedTraversalCapabilityIds);
	Artifacts.ParentAssertionSubset = LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact(
		ParentAssertionSubsetIds);
	return Artifacts;
}

bool LayoutCertifiedChildWitnessSubset::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutId>& SelectedTraversalCapabilityIds,
	const TArray<FLayoutId>& ParentAssertionSubsetIds,
	FString Provenance,
	FLayoutProducedCertifiedChildWitnessSubsetArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedCertifiedChildWitnessSubsetArtifact();
	OutFailureReason.Reset();
	if (SelectedTraversalCapabilityIds.IsEmpty() && ParentAssertionSubsetIds.IsEmpty())
	{
		return true;
	}
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Produced child witness subset artifact requires an artifact id when exact ids are present.");
		return false;
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.SubsetArtifacts = BuildArtifactsFromExplicitIds(
		SelectedTraversalCapabilityIds,
		ParentAssertionSubsetIds);
	OutArtifact.Provenance = MoveTemp(Provenance);
	return true;
}

bool LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromProducedArtifact(
	const FLayoutProducedCertifiedChildWitnessSubsetArtifact* ProducedArtifact,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	FLayoutCertifiedChildWitnessSubsets& OutSubsets,
	FString& OutFailureReason)
{
	OutSubsets = FLayoutCertifiedChildWitnessSubsets();
	OutFailureReason.Reset();
	if (ProducedArtifact == nullptr || !ProducedArtifact->bHasProducedArtifact)
	{
		return true;
	}
	if (ProducedArtifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Produced child witness subset artifact is missing its artifact id.");
		return false;
	}
	return TryExtractWitnessSubsets(
		&ProducedArtifact->SubsetArtifacts,
		ParentAssertions,
		OutSubsets,
		OutFailureReason);
}

bool LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
	const FLayoutChildTraversalWitnessMappingArtifact* TraversalWitnessArtifact,
	const FLayoutChildParentAssertionSubsetMappingArtifact* ParentAssertionSubsetArtifact,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	FLayoutCertifiedChildWitnessSubsets& OutSubsets,
	FString& OutFailureReason)
{
	OutSubsets = FLayoutCertifiedChildWitnessSubsets();
	OutFailureReason.Reset();
	if (TraversalWitnessArtifact == nullptr && ParentAssertionSubsetArtifact == nullptr)
	{
		return true;
	}

	FLayoutCertifiedChildWitnessSubsetArtifacts Artifacts;
	if (TraversalWitnessArtifact != nullptr)
	{
		Artifacts.TraversalWitness = *TraversalWitnessArtifact;
	}
	if (ParentAssertionSubsetArtifact != nullptr)
	{
		Artifacts.ParentAssertionSubset = *ParentAssertionSubsetArtifact;
	}
	return TryExtractWitnessSubsets(&Artifacts, ParentAssertions, OutSubsets, OutFailureReason);
}

bool LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromRequestCarriedArtifacts(
	const FLayoutRegionSolveRequest& ChildRequest,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	FLayoutCertifiedChildWitnessSubsets& OutSubsets,
	FString& OutFailureReason)
{
	TOptional<FLayoutChildTraversalWitnessMappingArtifact> TraversalWitnessArtifact;
	if (ChildRequest.bHasCertifiedSelectedTraversalCapabilityArtifact)
	{
		if (ChildRequest.CertifiedSelectedTraversalCapabilityIds.IsEmpty())
		{
			OutSubsets = FLayoutCertifiedChildWitnessSubsets();
			OutFailureReason = TEXT("Request-carried selected traversal artifact is present but contains no capability ids.");
			return false;
		}
		TraversalWitnessArtifact = LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
			ChildRequest.CertifiedSelectedTraversalCapabilityIds);
	}

	TOptional<FLayoutChildParentAssertionSubsetMappingArtifact> ParentAssertionSubsetArtifact;
	if (ChildRequest.bHasCertifiedParentAssertionSubsetArtifact)
	{
		if (ChildRequest.CertifiedParentAssertionSubsetIds.IsEmpty())
		{
			OutSubsets = FLayoutCertifiedChildWitnessSubsets();
			OutFailureReason = TEXT("Request-carried parent assertion subset artifact is present but contains no assertion ids.");
			return false;
		}
		ParentAssertionSubsetArtifact = LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact(
			ChildRequest.CertifiedParentAssertionSubsetIds);
	}

	return TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
		TraversalWitnessArtifact.IsSet() ? &TraversalWitnessArtifact.GetValue() : nullptr,
		ParentAssertionSubsetArtifact.IsSet() ? &ParentAssertionSubsetArtifact.GetValue() : nullptr,
		ParentAssertions,
		OutSubsets,
		OutFailureReason);
}

bool LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsets(
	const FLayoutCertifiedChildWitnessSubsetArtifacts* Artifacts,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	FLayoutCertifiedChildWitnessSubsets& OutSubsets,
	FString& OutFailureReason)
{
	OutSubsets = FLayoutCertifiedChildWitnessSubsets();
	OutFailureReason.Reset();
	if (Artifacts == nullptr)
	{
		return true;
	}

	if (!LayoutCertifiedChildTraversalWitness::TryExtractSelectedTraversalCapabilityIds(
			&Artifacts->TraversalWitness,
			OutSubsets.SelectedTraversalCapabilityIds,
			OutFailureReason))
	{
		OutSubsets = FLayoutCertifiedChildWitnessSubsets();
		return false;
	}

	TArray<FLayoutId> ExtractedParentAssertionSubsetIds;
	if (!LayoutCertifiedChildAssertionSubset::TryExtractParentAssertionSubsetIds(
			&Artifacts->ParentAssertionSubset,
			ParentAssertions,
			ExtractedParentAssertionSubsetIds,
			OutFailureReason))
	{
		OutSubsets = FLayoutCertifiedChildWitnessSubsets();
		return false;
	}
	if (!LayoutCertifiedChildAssertionSubset::TryResolveParentWitnessAssertionIds(
			ParentAssertions,
			ExtractedParentAssertionSubsetIds,
			OutSubsets.ParentAssertionIds,
			OutFailureReason))
	{
		OutSubsets = FLayoutCertifiedChildWitnessSubsets();
		return false;
	}

	return true;
}
