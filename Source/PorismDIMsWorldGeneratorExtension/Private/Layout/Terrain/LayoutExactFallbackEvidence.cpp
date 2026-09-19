// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutExactFallbackEvidence.h"

namespace
{
	bool IsValidEvidenceKind(const ELayoutExactFallbackEvidenceKind EvidenceKind)
	{
		switch (EvidenceKind)
		{
		case ELayoutExactFallbackEvidenceKind::ThresholdBand:
		case ELayoutExactFallbackEvidenceKind::PinnedCell:
		case ELayoutExactFallbackEvidenceKind::NonHeightfieldCell:
			return true;
		default:
			return false;
		}
	}

	const TCHAR* ToAuditString(const ELayoutExactFallbackEvidenceKind EvidenceKind)
	{
		switch (EvidenceKind)
		{
		case ELayoutExactFallbackEvidenceKind::ThresholdBand:
			return TEXT("ThresholdBand");
		case ELayoutExactFallbackEvidenceKind::PinnedCell:
			return TEXT("PinnedCell");
		case ELayoutExactFallbackEvidenceKind::NonHeightfieldCell:
			return TEXT("NonHeightfieldCell");
		default:
			return TEXT("Unknown");
		}
	}

	bool ValidateCells(
		const TArray<FLayoutExactFallbackCellEvidence>& Cells,
		FString& OutFailureReason)
	{
		if (Cells.IsEmpty())
		{
			OutFailureReason = TEXT("Exact fallback evidence artifact requires at least one cell.");
			return false;
		}

		TSet<FIntVector> SeenCells;
		for (const FLayoutExactFallbackCellEvidence& Cell : Cells)
		{
			if (!IsValidEvidenceKind(Cell.EvidenceKind))
			{
				OutFailureReason = TEXT("Exact fallback evidence artifact contains an invalid evidence kind.");
				return false;
			}
			if (Cell.ProvenanceId.IsNone())
			{
				OutFailureReason = TEXT("Exact fallback evidence cell requires provenance id.");
				return false;
			}
			if (SeenCells.Contains(Cell.LocalCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Exact fallback evidence duplicates cell '%s'."),
					*Cell.LocalCell.ToString());
				return false;
			}
			SeenCells.Add(Cell.LocalCell);
		}
		return true;
	}
}

bool LayoutExactFallbackEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutExactFallbackCellEvidence>& Cells,
	FString Provenance,
	FLayoutProducedExactFallbackEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedExactFallbackEvidenceArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Exact fallback evidence artifact requires an artifact id.");
		return false;
	}
	if (!ValidateCells(Cells, OutFailureReason))
	{
		return false;
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.Cells = Cells;
	OutArtifact.Provenance = MoveTemp(Provenance);
	return true;
}

bool LayoutExactFallbackEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedExactFallbackEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedExactFallbackEvidenceArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasExactFallbackEvidence)
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact does not carry explicit exact fallback evidence.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.ExactFallbackCells,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutExactFallbackEvidence::TryApplyProducedArtifact(
	const FLayoutProducedExactFallbackEvidenceArtifact* Artifact,
	const bool bRequireExactFallbackEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	InOutTerrainBiomeArtifact.bHasExactFallbackEvidence = false;
	InOutTerrainBiomeArtifact.ExactFallbackCells.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		if (bRequireExactFallbackEvidence)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires authoritative exact fallback evidence.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected exact fallback evidence without artifact id.");
		return false;
	}
	if (!ValidateCells(Artifact->Cells, OutFailureReason))
	{
		return false;
	}

	InOutTerrainBiomeArtifact.bHasExactFallbackEvidence = true;
	InOutTerrainBiomeArtifact.ExactFallbackCells = Artifact->Cells;
	for (const FLayoutExactFallbackCellEvidence& Cell : Artifact->Cells)
	{
		InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
			TEXT("ExactFallbackEvidenceArtifact=%s Cell=%s Kind=%s Provenance=%s Producer=%s"),
			*Artifact->ArtifactId.ToString(),
			*Cell.LocalCell.ToString(),
			ToAuditString(Cell.EvidenceKind),
			*Cell.ProvenanceId.ToString(),
			*Artifact->Provenance));
	}
	return true;
}
