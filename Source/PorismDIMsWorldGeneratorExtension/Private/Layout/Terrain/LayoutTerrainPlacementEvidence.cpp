// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutTerrainPlacementEvidence.h"

namespace
{
	bool ValidateCells(
		const TArray<FLayoutTerrainPlacementCellEvidence>& Cells,
		FString& OutFailureReason)
	{
		if (Cells.IsEmpty())
		{
			OutFailureReason = TEXT("Terrain placement evidence requires at least one cell verdict.");
			return false;
		}

		TSet<FIntVector> SeenCells;
		for (const FLayoutTerrainPlacementCellEvidence& CellEvidence : Cells)
		{
			if (SeenCells.Contains(CellEvidence.Cell))
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected duplicate cell verdicts.");
				return false;
			}
			SeenCells.Add(CellEvidence.Cell);

			if (CellEvidence.ProvenanceId.IsNone())
			{
				OutFailureReason = TEXT("Terrain placement evidence requires stable per-cell provenance ids.");
				return false;
			}
			if (CellEvidence.bHasLocalOverlapZ
				&& CellEvidence.OverlapMinLocalZ.Value > CellEvidence.OverlapMaxLocalZ.Value)
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected invalid local overlap Z bounds.");
				return false;
			}
			if (CellEvidence.bHasLocalOverlapZ && !CellEvidence.bHasExcavationEvidence)
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected local overlap bounds without excavation evidence.");
				return false;
			}
			if (CellEvidence.bHasExcavationEvidence && !CellEvidence.bHasLocalOverlapZ)
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected excavation evidence without local overlap bounds.");
				return false;
			}
			if ((CellEvidence.TerrainStageIndex == 0) != (CellEvidence.VerticalShiftBlocks == 0)
				|| (CellEvidence.TerrainStageIndex > 0 && CellEvidence.VerticalShiftBlocks < 0)
				|| (CellEvidence.TerrainStageIndex < 0 && CellEvidence.VerticalShiftBlocks > 0))
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected inconsistent terrain stage and vertical shift.");
				return false;
			}
			if (CellEvidence.bHasBridgeSupportEvidence
				&& (CellEvidence.bHasFoundationFillEvidence
					|| CellEvidence.bHasRampTransitionEvidence
					|| CellEvidence.bHasExcavationEvidence
					|| CellEvidence.bHasClearanceEvidence))
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected bridge support mixed with terrain modification evidence.");
				return false;
			}
			if (CellEvidence.bHasFoundationFillEvidence
				&& (CellEvidence.bHasExcavationEvidence || CellEvidence.bHasClearanceEvidence))
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected foundation fill mixed with excavation or clearance evidence.");
				return false;
			}
			if (CellEvidence.bHasFoundationFillEvidence && CellEvidence.RequiredFoundationDepth <= 0)
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected foundation fill without positive bounded depth.");
				return false;
			}
			if (!CellEvidence.bHasFoundationFillEvidence && CellEvidence.RequiredFoundationDepth != 0)
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected foundation depth without foundation-fill authority.");
				return false;
			}
            if (CellEvidence.bHasRampTransitionEvidence
                && CellEvidence.bHasExcavationEvidence
                && !CellEvidence.bHasLocalOverlapZ)
            {
                OutFailureReason = TEXT("Terrain placement evidence rejected ramp transition with excavation missing local overlap bounds.");
                return false;
            }
			if (!CellEvidence.bPlaceableForSelectedMode
				&& (CellEvidence.bHasFoundationFillEvidence
					|| CellEvidence.bHasRampTransitionEvidence
					|| CellEvidence.bHasBridgeSupportEvidence
					|| CellEvidence.bHasExcavationEvidence
					|| CellEvidence.bHasClearanceEvidence))
			{
				OutFailureReason = TEXT("Terrain placement evidence rejected operation evidence on an unplaceable selected-mode cell.");
				return false;
			}
		}

		return true;
	}
}

bool LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutTerrainPlacementCellEvidence>& Cells,
	const FString& Provenance,
	FLayoutProducedTerrainPlacementEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedTerrainPlacementEvidenceArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Terrain placement evidence requires an artifact id.");
		return false;
	}
	if (Provenance.IsEmpty())
	{
		OutFailureReason = TEXT("Terrain placement evidence requires provenance.");
		return false;
	}
	if (!ValidateCells(Cells, OutFailureReason))
	{
		return false;
	}

	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.Cells = Cells;
	OutArtifact.Provenance = Provenance;
	return true;
}

bool LayoutTerrainPlacementEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	const FString& Provenance,
	FLayoutProducedTerrainPlacementEvidenceArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedTerrainPlacementEvidenceArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasTerrainPlacementEvidence)
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact does not carry explicit terrain placement evidence.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.TerrainPlacementCells,
		Provenance,
		OutArtifact,
		OutFailureReason);
}

bool LayoutTerrainPlacementEvidence::TryApplyProducedArtifact(
	const FLayoutProducedTerrainPlacementEvidenceArtifact* Artifact,
	const bool bRequireTerrainPlacementEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (Artifact == nullptr)
	{
		if (bRequireTerrainPlacementEvidence)
		{
			OutFailureReason = TEXT("Terrain placement evidence is required but no produced artifact was supplied.");
			return false;
		}
		return true;
	}

	FLayoutProducedTerrainPlacementEvidenceArtifact ValidatedArtifact;
	if (!TryBuildProducedArtifact(
			Artifact->ArtifactId,
			Artifact->Cells,
			Artifact->Provenance,
			ValidatedArtifact,
			OutFailureReason))
	{
		return false;
	}

	InOutTerrainBiomeArtifact.bHasTerrainPlacementEvidence = true;
	InOutTerrainBiomeArtifact.TerrainPlacementCells = ValidatedArtifact.Cells;
	InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
		TEXT("TerrainPlacementEvidence=%s"),
		*ValidatedArtifact.ArtifactId.ToString()));
	return true;
}
