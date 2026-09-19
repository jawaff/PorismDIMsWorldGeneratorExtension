// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutShiftedFootprintProof.h"

namespace
{
	bool IsZeroShift(const FLayoutId ShiftId, const FIntVector& ShiftCells)
	{
		return ShiftCells == FIntVector::ZeroValue || ShiftId.IsNone();
	}
}

bool LayoutShiftedFootprintProof::TryBuildProducedArtifact(
	const FLayoutId ArtifactId,
	const FLayoutFrozenShiftedFootprintEvidence& Evidence,
	FString Provenance,
	FLayoutProducedShiftedFootprintProofArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedShiftedFootprintProofArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Shifted-footprint proof artifact requires an artifact id.");
		return false;
	}
	if (Evidence.PlacementShiftId.IsNone() || Evidence.ShiftCells == FIntVector::ZeroValue)
	{
		OutFailureReason = TEXT("Shifted-footprint proof artifact requires a non-zero placement shift id and cell offset.");
		return false;
	}
	if (Evidence.SteppedSupportSamples.IsEmpty())
	{
		OutFailureReason = TEXT("Shifted-footprint proof artifact requires shifted stepped-support samples.");
		return false;
	}
	if (Evidence.BiomeOwnershipSamples.IsEmpty())
	{
		OutFailureReason = TEXT("Shifted-footprint proof artifact requires shifted biome-ownership samples.");
		return false;
	}
	if (!Evidence.bHasTerrainFitProof || !Evidence.bHasReservationCollisionProof || !Evidence.bHasChunkOverlapProvenance)
	{
		OutFailureReason = TEXT("Shifted-footprint proof artifact requires terrain-fit, reservation/collision, and chunk-overlap provenance.");
		return false;
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.Evidence = Evidence;
	OutArtifact.Provenance = MoveTemp(Provenance);
	return true;
}

bool LayoutShiftedFootprintProof::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
	const FLayoutId ArtifactId,
	const FLayoutId RequiredShiftId,
	const FIntVector& RequiredShiftCells,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FString Provenance,
	FLayoutProducedShiftedFootprintProofArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedShiftedFootprintProofArtifact();
	OutFailureReason.Reset();
	if (IsZeroShift(RequiredShiftId, RequiredShiftCells))
	{
		OutFailureReason = TEXT("Shifted-footprint proof producer requires a non-zero required shift.");
		return false;
	}
	if (!FrozenTerrainBiomeArtifact.bHasShiftedFootprintEvidence)
	{
		OutFailureReason = TEXT("Shifted-footprint proof producer requires explicit shifted evidence on the frozen terrain/biome artifact.");
		return false;
	}

	const FLayoutFrozenShiftedFootprintEvidence* const MatchingEvidence = FrozenTerrainBiomeArtifact.ShiftedFootprintEvidence.FindByPredicate(
		[RequiredShiftId, RequiredShiftCells](const FLayoutFrozenShiftedFootprintEvidence& Evidence)
		{
			return Evidence.PlacementShiftId == RequiredShiftId && Evidence.ShiftCells == RequiredShiftCells;
		});
	if (MatchingEvidence == nullptr)
	{
		OutFailureReason = TEXT("Shifted-footprint proof producer could not find matching shifted evidence in the frozen terrain/biome artifact.");
		return false;
	}

	return TryBuildProducedArtifact(
		ArtifactId,
		*MatchingEvidence,
		MoveTemp(Provenance),
		OutArtifact,
		OutFailureReason);
}

bool LayoutShiftedFootprintProof::TryApplyProducedArtifact(
	const FLayoutProducedShiftedFootprintProofArtifact* Artifact,
	const FLayoutId RequiredShiftId,
	const FIntVector& RequiredShiftCells,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (IsZeroShift(RequiredShiftId, RequiredShiftCells))
	{
		if (Artifact != nullptr && Artifact->bHasProducedArtifact)
		{
			OutFailureReason = TEXT("Unshifted branch rejected unexpected shifted-footprint proof artifact.");
			return false;
		}
		return true;
	}
	if (Artifact == nullptr || !Artifact->bHasProducedArtifact)
	{
		InOutTerrainBiomeArtifact.bHasShiftedFootprintEvidence = false;
		InOutTerrainBiomeArtifact.ShiftedFootprintEvidence.Reset();
		OutFailureReason = TEXT("Non-zero shifted branch requires an authoritative shifted-footprint proof artifact.");
		return false;
	}
	if (Artifact->ArtifactId.IsNone()
		|| Artifact->Evidence.PlacementShiftId != RequiredShiftId
		|| Artifact->Evidence.ShiftCells != RequiredShiftCells)
	{
		InOutTerrainBiomeArtifact.bHasShiftedFootprintEvidence = false;
		InOutTerrainBiomeArtifact.ShiftedFootprintEvidence.Reset();
		OutFailureReason = TEXT("Shifted-footprint proof artifact does not match the required shifted branch.");
		return false;
	}

	FLayoutProducedShiftedFootprintProofArtifact RevalidatedArtifact;
	if (!TryBuildProducedArtifact(
			Artifact->ArtifactId,
			Artifact->Evidence,
			Artifact->Provenance,
			RevalidatedArtifact,
			OutFailureReason))
	{
		InOutTerrainBiomeArtifact.bHasShiftedFootprintEvidence = false;
		InOutTerrainBiomeArtifact.ShiftedFootprintEvidence.Reset();
		return false;
	}

	InOutTerrainBiomeArtifact.ShiftedFootprintEvidence.Add(Artifact->Evidence);
	InOutTerrainBiomeArtifact.bHasShiftedFootprintEvidence = true;
	InOutTerrainBiomeArtifact.AuditMessages.Add(FString::Printf(
		TEXT("ShiftedFootprintProofArtifact=%s Shift=%s Provenance=%s"),
		*Artifact->ArtifactId.ToString(),
		*Artifact->Evidence.ShiftCells.ToString(),
		*Artifact->Provenance));
	return true;
}
