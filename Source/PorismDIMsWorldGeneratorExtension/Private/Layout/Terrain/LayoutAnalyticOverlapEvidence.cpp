// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutAnalyticOverlapEvidence.h"

namespace
{
	bool ValidateCells(
		const TArray<FLayoutAnalyticOverlapCellEvidence>& Cells,
		FString& OutFailureReason)
	{
		if (Cells.IsEmpty())
		{
			OutFailureReason = TEXT("Analytic overlap evidence artifact requires at least one cell.");
			return false;
		}

		TSet<FIntVector> SeenCells;
		for (const FLayoutAnalyticOverlapCellEvidence& Cell : Cells)
		{
			if (Cell.OverlapArea <= 0)
			{
				OutFailureReason = TEXT("Analytic overlap evidence cell requires positive overlap area.");
				return false;
			}
			if (Cell.ProvenanceId.IsNone())
			{
				OutFailureReason = TEXT("Analytic overlap evidence cell requires provenance id.");
				return false;
			}
			if (SeenCells.Contains(Cell.LocalCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Analytic overlap evidence duplicates cell '%s'."),
					*Cell.LocalCell.ToString());
				return false;
			}
			SeenCells.Add(Cell.LocalCell);
		}
		return true;
	}
}

bool LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutAnalyticOverlapCellEvidence>& Cells,
	FString Provenance,
	FLayoutProducedAnalyticOverlapEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedAnalyticOverlapEvidenceArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Analytic overlap evidence artifact requires an artifact id.");
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

bool LayoutAnalyticOverlapEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedAnalyticOverlapEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedAnalyticOverlapEvidenceArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasAnalyticOverlapEvidence)
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact does not carry explicit analytic-overlap evidence.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.AnalyticOverlapCells,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutAnalyticOverlapEvidence::TryApplyProducedArtifact(
	const FLayoutProducedAnalyticOverlapEvidenceArtifact* Artifact,
	const bool bRequireAnalyticOverlapEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	InOutTerrainBiomeArtifact.bHasAnalyticOverlapEvidence = false;
	InOutTerrainBiomeArtifact.AnalyticOverlapCells.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		if (bRequireAnalyticOverlapEvidence)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires authoritative analytic overlap evidence.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected analytic overlap evidence without artifact id.");
		return false;
	}
	if (!ValidateCells(Artifact->Cells, OutFailureReason))
	{
		return false;
	}

	InOutTerrainBiomeArtifact.bHasAnalyticOverlapEvidence = true;
	InOutTerrainBiomeArtifact.AnalyticOverlapCells = Artifact->Cells;
	for (const FLayoutAnalyticOverlapCellEvidence& Cell : Artifact->Cells)
	{
		InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
			TEXT("AnalyticOverlapEvidenceArtifact=%s Cell=%s Area=%d Provenance=%s Producer=%s"),
			*Artifact->ArtifactId.ToString(),
			*Cell.LocalCell.ToString(),
			Cell.OverlapArea,
			*Cell.ProvenanceId.ToString(),
			*Artifact->Provenance));
	}
	return true;
}
