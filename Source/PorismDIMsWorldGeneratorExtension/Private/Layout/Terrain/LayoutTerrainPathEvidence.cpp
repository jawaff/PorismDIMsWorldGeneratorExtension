// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutTerrainPathEvidence.h"

bool LayoutTerrainPathEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutFrozenTerrainPathSample>& TerrainPathSamples,
	FString Provenance,
	FLayoutProducedTerrainPathEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedTerrainPathEvidenceArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Terrain-path evidence artifact requires an artifact id.");
		return false;
	}
	if (TerrainPathSamples.IsEmpty())
	{
		OutFailureReason = TEXT("Terrain-path evidence artifact requires at least one classified path sample.");
		return false;
	}

	TSet<FIntPoint> SeenColumns;
	for (const FLayoutFrozenTerrainPathSample& Sample : TerrainPathSamples)
	{
		if (!Sample.bHasClassificationEvidence)
		{
			OutFailureReason = TEXT("Terrain-path evidence artifact contains an unclassified path sample.");
			return false;
		}
		if (SeenColumns.Contains(Sample.BlockXY))
		{
			OutFailureReason = FString::Printf(TEXT("Terrain-path evidence artifact duplicates path column (%d,%d)."), Sample.BlockXY.X, Sample.BlockXY.Y);
			return false;
		}
		SeenColumns.Add(Sample.BlockXY);
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.TerrainPathSamples = TerrainPathSamples;
	OutArtifact.Provenance = MoveTemp(Provenance);
	return true;
}

bool LayoutTerrainPathEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedTerrainPathEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedTerrainPathEvidenceArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasTerrainPathEvidence)
	{
		OutFailureReason = TEXT("Terrain-path producer requires explicit path evidence on the frozen terrain/biome artifact.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.TerrainPathSamples,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutTerrainPathEvidence::TryApplyProducedArtifact(
	const FLayoutProducedTerrainPathEvidenceArtifact* Artifact,
	const bool bRequireTerrainPathEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		InOutTerrainBiomeArtifact.TerrainPathSamples.Reset();
		InOutTerrainBiomeArtifact.bHasTerrainPathEvidence = false;
		if (bRequireTerrainPathEvidence)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires an authoritative terrain-path evidence artifact.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone() || Artifact->TerrainPathSamples.IsEmpty())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected a stale terrain-path evidence artifact.");
		return false;
	}

	FLayoutProducedTerrainPathEvidenceArtifact RevalidatedArtifact;
	if (!TryBuildProducedArtifact(
			Artifact->ArtifactId,
			Artifact->TerrainPathSamples,
			Artifact->Provenance,
			RevalidatedArtifact,
			OutFailureReason))
	{
		InOutTerrainBiomeArtifact.TerrainPathSamples.Reset();
		InOutTerrainBiomeArtifact.bHasTerrainPathEvidence = false;
		return false;
	}

	InOutTerrainBiomeArtifact.TerrainPathSamples = Artifact->TerrainPathSamples;
	InOutTerrainBiomeArtifact.bHasTerrainPathEvidence = true;
	InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
		TEXT("TerrainPathEvidenceArtifact=%s Samples=%d Provenance=%s"),
		*Artifact->ArtifactId.ToString(),
		Artifact->TerrainPathSamples.Num(),
		*Artifact->Provenance));
	return true;
}
