// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutCertifiedChildAssertionSubset.h"

namespace
{
	const FLayoutValidationAssertionRecord* FindAssertionById(
		const TArray<FLayoutValidationAssertionRecord>& Assertions,
		const FLayoutId AssertionId)
	{
		return Assertions.FindByPredicate([AssertionId](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == AssertionId;
		});
	}
}

FLayoutChildParentAssertionSubsetMappingArtifact LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact(
	const TArray<FLayoutId>& ParentAssertionSubsetIds)
{
	FLayoutChildParentAssertionSubsetMappingArtifact Artifact;
	Artifact.bHasExplicitMapping = !ParentAssertionSubsetIds.IsEmpty();
	Artifact.ParentAssertionSubsetIds = ParentAssertionSubsetIds;
	return Artifact;
}

bool LayoutCertifiedChildAssertionSubset::TryExtractParentAssertionSubsetIds(
	const FLayoutChildParentAssertionSubsetMappingArtifact* MappingArtifact,
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	TArray<FLayoutId>& OutSubsetIds,
	FString& OutFailureReason)
{
	OutSubsetIds.Reset();
	OutFailureReason.Reset();
	if (MappingArtifact == nullptr || !MappingArtifact->bHasExplicitMapping)
	{
		return true;
	}
	if (MappingArtifact->ParentAssertionSubsetIds.IsEmpty())
	{
		OutFailureReason = TEXT("Parent assertion subset mapping artifact is present but contains no assertion ids.");
		return false;
	}

	for (const FLayoutId AssertionId : MappingArtifact->ParentAssertionSubsetIds)
	{
		if (AssertionId.IsNone())
		{
			OutFailureReason = TEXT("Parent assertion subset mapping contains an empty assertion id.");
			OutSubsetIds.Reset();
			return false;
		}
		if (OutSubsetIds.Contains(AssertionId))
		{
			OutFailureReason = FString::Printf(TEXT("Parent assertion subset mapping duplicates assertion id '%s'."), *AssertionId.ToString());
			OutSubsetIds.Reset();
			return false;
		}
		const FLayoutValidationAssertionRecord* const ParentAssertion = FindAssertionById(ParentAssertions, AssertionId);
		if (ParentAssertion == nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Parent assertion subset mapping references missing assertion id '%s'."), *AssertionId.ToString());
			OutSubsetIds.Reset();
			return false;
		}
		if (!ParentAssertion->bPassed)
		{
			OutFailureReason = FString::Printf(TEXT("Parent assertion subset mapping references failed assertion id '%s'."), *AssertionId.ToString());
			OutSubsetIds.Reset();
			return false;
		}
		OutSubsetIds.Add(AssertionId);
	}
	return true;
}

bool LayoutCertifiedChildAssertionSubset::TryResolveParentWitnessAssertionIds(
	const TArray<FLayoutValidationAssertionRecord>& ParentAssertions,
	const TArray<FLayoutId>& ParentAssertionSubsetIds,
	TArray<FLayoutId>& OutParentWitnessAssertionIds,
	FString& OutFailureReason)
{
	OutParentWitnessAssertionIds.Reset();
	OutFailureReason.Reset();
	for (const FLayoutId AssertionId : ParentAssertionSubsetIds)
	{
		if (AssertionId.IsNone())
		{
			OutFailureReason = TEXT("Parent witness assertion subset contains an empty assertion id.");
			OutParentWitnessAssertionIds.Reset();
			return false;
		}
		if (OutParentWitnessAssertionIds.Contains(AssertionId))
		{
			OutFailureReason = FString::Printf(TEXT("Parent witness assertion subset duplicates assertion id '%s'."), *AssertionId.ToString());
			OutParentWitnessAssertionIds.Reset();
			return false;
		}
		const FLayoutValidationAssertionRecord* const ParentAssertion = FindAssertionById(ParentAssertions, AssertionId);
		if (ParentAssertion == nullptr)
		{
			OutFailureReason = FString::Printf(TEXT("Parent witness assertion subset references missing assertion id '%s'."), *AssertionId.ToString());
			OutParentWitnessAssertionIds.Reset();
			return false;
		}
		if (!ParentAssertion->bPassed)
		{
			OutFailureReason = FString::Printf(TEXT("Parent witness assertion subset references failed assertion id '%s'."), *AssertionId.ToString());
			OutParentWitnessAssertionIds.Reset();
			return false;
		}
		OutParentWitnessAssertionIds.Add(AssertionId);
	}
	return true;
}
