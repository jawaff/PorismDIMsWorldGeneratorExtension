// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutCertifiedChildTraversalWitness.h"

FLayoutChildTraversalWitnessMappingArtifact LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
	const TArray<FLayoutId>& SelectedTraversalCapabilityIds)
{
	FLayoutChildTraversalWitnessMappingArtifact Artifact;
	Artifact.bHasExplicitTraversalWitness = !SelectedTraversalCapabilityIds.IsEmpty();
	Artifact.SelectedTraversalCapabilityIds = SelectedTraversalCapabilityIds;
	return Artifact;
}

bool LayoutCertifiedChildTraversalWitness::TryExtractSelectedTraversalCapabilityIds(
	const FLayoutChildTraversalWitnessMappingArtifact* MappingArtifact,
	TArray<FLayoutId>& OutSelectedTraversalCapabilityIds,
	FString& OutFailureReason)
{
	OutSelectedTraversalCapabilityIds.Reset();
	OutFailureReason.Reset();
	if (MappingArtifact == nullptr || !MappingArtifact->bHasExplicitTraversalWitness)
	{
		return true;
	}
	if (MappingArtifact->SelectedTraversalCapabilityIds.IsEmpty())
	{
		OutFailureReason = TEXT("Selected traversal witness artifact is present but contains no capability ids.");
		return false;
	}

	for (const FLayoutId CapabilityId : MappingArtifact->SelectedTraversalCapabilityIds)
	{
		if (CapabilityId.IsNone())
		{
			OutFailureReason = TEXT("Selected traversal witness contains an empty capability id.");
			OutSelectedTraversalCapabilityIds.Reset();
			return false;
		}
		if (OutSelectedTraversalCapabilityIds.Contains(CapabilityId))
		{
			OutFailureReason = FString::Printf(TEXT("Selected traversal witness duplicates capability id '%s'."), *CapabilityId.ToString());
			OutSelectedTraversalCapabilityIds.Reset();
			return false;
		}
		OutSelectedTraversalCapabilityIds.Add(CapabilityId);
	}
	return true;
}
