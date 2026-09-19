// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutPocketVoidEvidence.h"

bool LayoutPocketVoidEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutFrozenTerrainVoidIntervalSample>& PocketVoidIntervals,
	FString Provenance,
	FLayoutProducedPocketVoidEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedPocketVoidEvidenceArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Pocket/void evidence artifact requires an artifact id.");
		return false;
	}
	if (PocketVoidIntervals.IsEmpty())
	{
		OutFailureReason = TEXT("Pocket/void evidence artifact requires at least one interval sample.");
		return false;
	}

	TSet<FIntPoint> SeenColumns;
	for (const FLayoutFrozenTerrainVoidIntervalSample& Interval : PocketVoidIntervals)
	{
		if (!Interval.bHasVoidEvidence || Interval.MinZ > Interval.MaxZ)
		{
			OutFailureReason = TEXT("Pocket/void evidence artifact contains an invalid or unproven interval.");
			return false;
		}
		if (SeenColumns.Contains(Interval.BlockXY))
		{
			OutFailureReason = FString::Printf(TEXT("Pocket/void evidence artifact duplicates interval column (%d,%d)."), Interval.BlockXY.X, Interval.BlockXY.Y);
			return false;
		}
		SeenColumns.Add(Interval.BlockXY);
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.PocketVoidIntervals = PocketVoidIntervals;
	OutArtifact.Provenance = MoveTemp(Provenance);
	return true;
}

bool LayoutPocketVoidEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedPocketVoidEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedPocketVoidEvidenceArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasPocketVoidIntervalEvidence)
	{
		OutFailureReason = TEXT("Pocket/void producer requires explicit interval evidence on the frozen terrain/biome artifact.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.PocketVoidIntervals,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutPocketVoidEvidence::TryApplyProducedArtifact(
	const FLayoutProducedPocketVoidEvidenceArtifact* Artifact,
	const bool bRequirePocketVoidEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		InOutTerrainBiomeArtifact.PocketVoidIntervals.Reset();
		InOutTerrainBiomeArtifact.bHasPocketVoidIntervalEvidence = false;
		if (bRequirePocketVoidEvidence)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires an authoritative pocket/void evidence artifact.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone() || Artifact->PocketVoidIntervals.IsEmpty())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected a stale pocket/void evidence artifact.");
		return false;
	}

	FLayoutProducedPocketVoidEvidenceArtifact RevalidatedArtifact;
	if (!TryBuildProducedArtifact(
			Artifact->ArtifactId,
			Artifact->PocketVoidIntervals,
			Artifact->Provenance,
			RevalidatedArtifact,
			OutFailureReason))
	{
		InOutTerrainBiomeArtifact.PocketVoidIntervals.Reset();
		InOutTerrainBiomeArtifact.bHasPocketVoidIntervalEvidence = false;
		return false;
	}

	InOutTerrainBiomeArtifact.PocketVoidIntervals = Artifact->PocketVoidIntervals;
	InOutTerrainBiomeArtifact.bHasPocketVoidIntervalEvidence = true;
	InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
		TEXT("PocketVoidEvidenceArtifact=%s Intervals=%d Provenance=%s"),
		*Artifact->ArtifactId.ToString(),
		Artifact->PocketVoidIntervals.Num(),
		*Artifact->Provenance));
	return true;
}
