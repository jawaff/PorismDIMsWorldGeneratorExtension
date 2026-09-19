// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutCompatibleBiomeAllowList.h"

bool LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FName>& CompatibleBiomeRowNames,
	FString Provenance,
	FLayoutProducedCompatibleBiomeAllowListArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedCompatibleBiomeAllowListArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Compatible-biome allow-list artifact requires an artifact id.");
		return false;
	}
	if (CompatibleBiomeRowNames.IsEmpty())
	{
		OutFailureReason = TEXT("Compatible-biome allow-list artifact requires at least one explicit biome row name.");
		return false;
	}

	TArray<FName> UniqueRows;
	for (const FName RowName : CompatibleBiomeRowNames)
	{
		if (RowName.IsNone())
		{
			OutFailureReason = TEXT("Compatible-biome allow-list artifact contains an empty biome row name.");
			return false;
		}
		if (UniqueRows.Contains(RowName))
		{
			OutFailureReason = FString::Printf(TEXT("Compatible-biome allow-list artifact duplicates biome row '%s'."), *RowName.ToString());
			return false;
		}
		UniqueRows.Add(RowName);
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.CompatibleBiomeRowNames = MoveTemp(UniqueRows);
	OutArtifact.Provenance = MoveTemp(Provenance);
	return true;
}

bool LayoutCompatibleBiomeAllowList::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedCompatibleBiomeAllowListArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedCompatibleBiomeAllowListArtifact();
	OutFailureReason.Reset();
	TArray<FName> ExplicitRows = FrozenTerrainBiomeArtifact.EligibleBiomeRowNames;
	if (ExplicitRows.IsEmpty() && !FrozenTerrainBiomeArtifact.EligibleBiomeRowName.IsNone())
	{
		ExplicitRows.Add(FrozenTerrainBiomeArtifact.EligibleBiomeRowName);
	}
	if (ExplicitRows.IsEmpty())
	{
		OutFailureReason = TEXT("Compatible-biome allow-list producer requires explicit row names on the frozen terrain/biome artifact.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		ExplicitRows,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutCompatibleBiomeAllowList::TryApplyProducedArtifact(
	const FLayoutProducedCompatibleBiomeAllowListArtifact* Artifact,
	const bool bRequireAllowList,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		InOutTerrainBiomeArtifact.EligibleBiomeRowNames.Reset();
		InOutTerrainBiomeArtifact.bRequiresBiomeOwnership = false;
		InOutTerrainBiomeArtifact.bHasBiomeOwnershipEvidence = false;
		if (bRequireAllowList)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires an authoritative compatible-biome allow-list artifact.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone() || Artifact->CompatibleBiomeRowNames.IsEmpty())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected a stale compatible-biome allow-list artifact.");
		return false;
	}

	InOutTerrainBiomeArtifact.EligibleBiomeRowNames.Reset();
	for (const FName RowName : Artifact->CompatibleBiomeRowNames)
	{
		if (RowName.IsNone() || InOutTerrainBiomeArtifact.EligibleBiomeRowNames.Contains(RowName))
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact rejected an invalid compatible-biome row name.");
			InOutTerrainBiomeArtifact.EligibleBiomeRowNames.Reset();
			InOutTerrainBiomeArtifact.bRequiresBiomeOwnership = false;
			InOutTerrainBiomeArtifact.bHasBiomeOwnershipEvidence = false;
			return false;
		}
		InOutTerrainBiomeArtifact.EligibleBiomeRowNames.Add(RowName);
	}
	InOutTerrainBiomeArtifact.bRequiresBiomeOwnership = true;
	InOutTerrainBiomeArtifact.bHasBiomeOwnershipEvidence = !InOutTerrainBiomeArtifact.BiomeOwnershipSamples.IsEmpty();
	InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
		TEXT("CompatibleBiomeAllowListArtifact=%s Rows=%d Provenance=%s"),
		*Artifact->ArtifactId.ToString(),
		InOutTerrainBiomeArtifact.EligibleBiomeRowNames.Num(),
		*Artifact->Provenance));
	return true;
}
