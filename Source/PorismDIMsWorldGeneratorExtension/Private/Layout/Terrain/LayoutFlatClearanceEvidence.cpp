// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutFlatClearanceEvidence.h"

namespace
{
	bool ValidateCells(
		const TArray<FLayoutFlatClearanceCellEvidence>& Cells,
		FString& OutFailureReason)
	{
		if (Cells.IsEmpty())
		{
			OutFailureReason = TEXT("Flat-clearance evidence artifact requires at least one cell.");
			return false;
		}

		TSet<FIntVector> SeenCells;
		for (const FLayoutFlatClearanceCellEvidence& Cell : Cells)
		{
			if (Cell.RequiredClearanceBlocks.Value == 0)
			{
				OutFailureReason = TEXT("Flat-clearance evidence cell requires positive required clearance.");
				return false;
			}
			if (Cell.ObservedClearanceBlocks.Value < Cell.RequiredClearanceBlocks.Value)
			{
				OutFailureReason = FString::Printf(
					TEXT("Flat-clearance evidence cell '%s' observed clearance is below required clearance."),
					*Cell.LocalCell.ToString());
				return false;
			}
			if (Cell.ProvenanceId.IsNone())
			{
				OutFailureReason = TEXT("Flat-clearance evidence cell requires provenance id.");
				return false;
			}
			if (SeenCells.Contains(Cell.LocalCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Flat-clearance evidence duplicates cell '%s'."),
					*Cell.LocalCell.ToString());
				return false;
			}
			SeenCells.Add(Cell.LocalCell);
		}
		return true;
	}
}

bool LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutFlatClearanceCellEvidence>& Cells,
	FString Provenance,
	FLayoutProducedFlatClearanceEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFlatClearanceEvidenceArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Flat-clearance evidence artifact requires an artifact id.");
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

bool LayoutFlatClearanceEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedFlatClearanceEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFlatClearanceEvidenceArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasFlatClearanceEvidence)
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact does not carry explicit flat-clearance evidence.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.FlatClearanceCells,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutFlatClearanceEvidence::TryApplyProducedArtifact(
	const FLayoutProducedFlatClearanceEvidenceArtifact* Artifact,
	const bool bRequireFlatClearanceEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	InOutTerrainBiomeArtifact.bHasFlatClearanceEvidence = false;
	InOutTerrainBiomeArtifact.FlatClearanceCells.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		if (bRequireFlatClearanceEvidence)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires authoritative flat-clearance evidence.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected flat-clearance evidence without artifact id.");
		return false;
	}
	if (!ValidateCells(Artifact->Cells, OutFailureReason))
	{
		return false;
	}

	InOutTerrainBiomeArtifact.bHasFlatClearanceEvidence = true;
	InOutTerrainBiomeArtifact.FlatClearanceCells = Artifact->Cells;
	for (const FLayoutFlatClearanceCellEvidence& Cell : Artifact->Cells)
	{
		InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
			TEXT("FlatClearanceEvidenceArtifact=%s Cell=%s Required=%d Observed=%d Provenance=%s Producer=%s"),
			*Artifact->ArtifactId.ToString(),
			*Cell.LocalCell.ToString(),
			Cell.RequiredClearanceBlocks.Value,
			Cell.ObservedClearanceBlocks.Value,
			*Cell.ProvenanceId.ToString(),
			*Artifact->Provenance));
	}
	return true;
}
