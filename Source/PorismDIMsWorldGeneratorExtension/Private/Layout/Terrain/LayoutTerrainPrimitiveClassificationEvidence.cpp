// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutTerrainPrimitiveClassificationEvidence.h"

namespace
{
	bool IsValidClassification(const ELayoutTerrainPrimitiveClassification Classification)
	{
		switch (Classification)
		{
		case ELayoutTerrainPrimitiveClassification::Usable:
		case ELayoutTerrainPrimitiveClassification::Supportable:
		case ELayoutTerrainPrimitiveClassification::Contractible:
		case ELayoutTerrainPrimitiveClassification::Excavatable:
		case ELayoutTerrainPrimitiveClassification::InvalidRequired:
			return true;
		default:
			return false;
		}
	}

	const TCHAR* ToAuditString(const ELayoutTerrainPrimitiveClassification Classification)
	{
		switch (Classification)
		{
		case ELayoutTerrainPrimitiveClassification::Usable:
			return TEXT("Usable");
		case ELayoutTerrainPrimitiveClassification::Supportable:
			return TEXT("Supportable");
		case ELayoutTerrainPrimitiveClassification::Contractible:
			return TEXT("Contractible");
		case ELayoutTerrainPrimitiveClassification::Excavatable:
			return TEXT("Excavatable");
		case ELayoutTerrainPrimitiveClassification::InvalidRequired:
			return TEXT("InvalidRequired");
		default:
			return TEXT("Unknown");
		}
	}

	bool ValidateCells(
		const TArray<FLayoutTerrainPrimitiveClassificationCellEvidence>& Cells,
		FString& OutFailureReason)
	{
		if (Cells.IsEmpty())
		{
			OutFailureReason = TEXT("Terrain primitive classification artifact requires at least one cell.");
			return false;
		}

		TSet<FIntVector> SeenCells;
		for (const FLayoutTerrainPrimitiveClassificationCellEvidence& Cell : Cells)
		{
			if (!IsValidClassification(Cell.Classification))
			{
				OutFailureReason = TEXT("Terrain primitive classification artifact contains an invalid classification.");
				return false;
			}
			if (Cell.ProvenanceId.IsNone())
			{
				OutFailureReason = TEXT("Terrain primitive classification cell requires provenance id.");
				return false;
			}
			if (SeenCells.Contains(Cell.LocalCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Terrain primitive classification duplicates cell '%s'."),
					*Cell.LocalCell.ToString());
				return false;
			}
			SeenCells.Add(Cell.LocalCell);
		}
		return true;
	}
}

bool LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const TArray<FLayoutTerrainPrimitiveClassificationCellEvidence>& Cells,
	FString Provenance,
	FLayoutProducedTerrainPrimitiveClassificationArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedTerrainPrimitiveClassificationArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Terrain primitive classification artifact requires an artifact id.");
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

bool LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedTerrainPrimitiveClassificationArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedTerrainPrimitiveClassificationArtifact();
	OutFailureReason.Reset();
	if (!FrozenTerrainBiomeArtifact.bHasTerrainPrimitiveClassificationEvidence)
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact does not carry explicit terrain primitive classification evidence.");
		return false;
	}
	return TryBuildProducedArtifact(
		ArtifactId,
		FrozenTerrainBiomeArtifact.TerrainPrimitiveClassificationCells,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutTerrainPrimitiveClassificationEvidence::TryApplyProducedArtifact(
	const FLayoutProducedTerrainPrimitiveClassificationArtifact* Artifact,
	const bool bRequireTerrainPrimitiveClassificationEvidence,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	InOutTerrainBiomeArtifact.bHasTerrainPrimitiveClassificationEvidence = false;
	InOutTerrainBiomeArtifact.TerrainPrimitiveClassificationCells.Reset();
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		if (bRequireTerrainPrimitiveClassificationEvidence)
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact requires authoritative terrain primitive classification evidence.");
			return false;
		}
		return true;
	}
	if (Artifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Frozen terrain/biome artifact rejected terrain primitive classification evidence without artifact id.");
		return false;
	}
	if (!ValidateCells(Artifact->Cells, OutFailureReason))
	{
		return false;
	}

	InOutTerrainBiomeArtifact.bHasTerrainPrimitiveClassificationEvidence = true;
	InOutTerrainBiomeArtifact.TerrainPrimitiveClassificationCells = Artifact->Cells;
	for (const FLayoutTerrainPrimitiveClassificationCellEvidence& Cell : Artifact->Cells)
	{
		InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
			TEXT("TerrainPrimitiveClassificationArtifact=%s Cell=%s Classification=%s Provenance=%s Producer=%s"),
			*Artifact->ArtifactId.ToString(),
			*Cell.LocalCell.ToString(),
			ToAuditString(Cell.Classification),
			*Cell.ProvenanceId.ToString(),
			*Artifact->Provenance));
	}
	return true;
}
