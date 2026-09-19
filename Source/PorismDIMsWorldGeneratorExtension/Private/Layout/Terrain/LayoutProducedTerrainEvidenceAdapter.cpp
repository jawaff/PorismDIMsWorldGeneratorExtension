// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutProducedTerrainEvidenceAdapter.h"

namespace
{
	bool AddProducedArtifactId(
		const bool bHasArtifact,
		const FLayoutId ArtifactId,
		const TCHAR* SourceName,
		TSet<FLayoutId>& SeenArtifactIds,
		FString& OutFailureReason)
	{
		if (!bHasArtifact)
		{
			return true;
		}
		if (ArtifactId.IsNone())
		{
			OutFailureReason = FString::Printf(TEXT("Produced %s artifact requires a stable artifact id."), SourceName);
			return false;
		}
		if (SeenArtifactIds.Contains(ArtifactId))
		{
			OutFailureReason = FString::Printf(TEXT("Produced terrain evidence adapter has duplicate explicit artifact id %s."), *ArtifactId.ToString());
			return false;
		}
		SeenArtifactIds.Add(ArtifactId);
		return true;
	}

	bool ValidateProducedArtifactIdentities(
		const FLayoutProducedTerrainEvidenceArtifacts& Artifacts,
		FString& OutFailureReason)
	{
		TSet<FLayoutId> SeenArtifactIds;
		return AddProducedArtifactId(Artifacts.CompatibleBiomeAllowList.IsSet(), Artifacts.CompatibleBiomeAllowList.IsSet() ? Artifacts.CompatibleBiomeAllowList.GetValue().ArtifactId : NAME_None, TEXT("compatible-biome"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.ShiftedFootprintProof.IsSet(), Artifacts.ShiftedFootprintProof.IsSet() ? Artifacts.ShiftedFootprintProof.GetValue().ArtifactId : NAME_None, TEXT("shifted-footprint"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.TerrainPathEvidence.IsSet(), Artifacts.TerrainPathEvidence.IsSet() ? Artifacts.TerrainPathEvidence.GetValue().ArtifactId : NAME_None, TEXT("terrain-path"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.PocketVoidEvidence.IsSet(), Artifacts.PocketVoidEvidence.IsSet() ? Artifacts.PocketVoidEvidence.GetValue().ArtifactId : NAME_None, TEXT("pocket/void"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.ExactFallbackEvidence.IsSet(), Artifacts.ExactFallbackEvidence.IsSet() ? Artifacts.ExactFallbackEvidence.GetValue().ArtifactId : NAME_None, TEXT("exact-fallback"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.FlatClearanceEvidence.IsSet(), Artifacts.FlatClearanceEvidence.IsSet() ? Artifacts.FlatClearanceEvidence.GetValue().ArtifactId : NAME_None, TEXT("flat-clearance"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.AnalyticOverlapEvidence.IsSet(), Artifacts.AnalyticOverlapEvidence.IsSet() ? Artifacts.AnalyticOverlapEvidence.GetValue().ArtifactId : NAME_None, TEXT("analytic-overlap"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.TerrainPlacementEvidence.IsSet(), Artifacts.TerrainPlacementEvidence.IsSet() ? Artifacts.TerrainPlacementEvidence.GetValue().ArtifactId : NAME_None, TEXT("terrain-placement"), SeenArtifactIds, OutFailureReason)
			&& AddProducedArtifactId(Artifacts.TerrainPrimitiveClassificationEvidence.IsSet(), Artifacts.TerrainPrimitiveClassificationEvidence.IsSet() ? Artifacts.TerrainPrimitiveClassificationEvidence.GetValue().ArtifactId : NAME_None, TEXT("primitive-classification"), SeenArtifactIds, OutFailureReason);
	}

	bool ValidateProducedArtifactProvenance(
		const FLayoutProducedTerrainEvidenceArtifacts& Artifacts,
		FString& OutFailureReason)
	{
		if (Artifacts.CompatibleBiomeAllowList.IsSet() && Artifacts.CompatibleBiomeAllowList.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced compatible-biome allow-list artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.ShiftedFootprintProof.IsSet() && Artifacts.ShiftedFootprintProof.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced shifted-footprint proof artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.TerrainPathEvidence.IsSet() && Artifacts.TerrainPathEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced terrain-path evidence artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.PocketVoidEvidence.IsSet() && Artifacts.PocketVoidEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced pocket/void evidence artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.ExactFallbackEvidence.IsSet() && Artifacts.ExactFallbackEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced exact-fallback evidence artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.FlatClearanceEvidence.IsSet() && Artifacts.FlatClearanceEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced flat-clearance evidence artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.AnalyticOverlapEvidence.IsSet() && Artifacts.AnalyticOverlapEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced analytic-overlap evidence artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.TerrainPlacementEvidence.IsSet() && Artifacts.TerrainPlacementEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced terrain-placement evidence artifact requires explicit provenance.");
			return false;
		}
		if (Artifacts.TerrainPrimitiveClassificationEvidence.IsSet() && Artifacts.TerrainPrimitiveClassificationEvidence.GetValue().Provenance.IsEmpty())
		{
			OutFailureReason = TEXT("Produced primitive-classification evidence artifact requires explicit provenance.");
			return false;
		}
		return true;
	}
}

bool LayoutProducedTerrainEvidenceAdapter::TryBuildArtifactsFromRegionPrewarmEvidence(
	const FLayoutRegionPrewarmEvidenceArtifacts& RegionPrewarmArtifacts,
	FLayoutProducedTerrainEvidenceArtifacts& OutProducedArtifacts,
	FString& OutFailureReason)
{
	OutProducedArtifacts = FLayoutProducedTerrainEvidenceArtifacts();
	OutFailureReason.Reset();

	FLayoutProducedTerrainEvidenceArtifacts SourceArtifactsForValidation;
	SourceArtifactsForValidation.CompatibleBiomeAllowList = RegionPrewarmArtifacts.CompatibleBiomeAllowList;
	SourceArtifactsForValidation.TerrainPathEvidence = RegionPrewarmArtifacts.TerrainPathEvidence;
	SourceArtifactsForValidation.PocketVoidEvidence = RegionPrewarmArtifacts.PocketVoidEvidence;
	SourceArtifactsForValidation.ExactFallbackEvidence = RegionPrewarmArtifacts.ExactFallbackEvidence;
	SourceArtifactsForValidation.FlatClearanceEvidence = RegionPrewarmArtifacts.FlatClearanceEvidence;
	SourceArtifactsForValidation.AnalyticOverlapEvidence = RegionPrewarmArtifacts.AnalyticOverlapEvidence;
	SourceArtifactsForValidation.TerrainPlacementEvidence = RegionPrewarmArtifacts.TerrainPlacementEvidence;
	SourceArtifactsForValidation.TerrainPrimitiveClassificationEvidence = RegionPrewarmArtifacts.PrimitiveClassificationEvidence;
	if (!ValidateProducedArtifactProvenance(SourceArtifactsForValidation, OutFailureReason))
	{
		return false;
	}
	if (!ValidateProducedArtifactIdentities(SourceArtifactsForValidation, OutFailureReason))
	{
		return false;
	}

	if (RegionPrewarmArtifacts.CompatibleBiomeAllowList.IsSet())
	{
		const FLayoutProducedCompatibleBiomeAllowListArtifact& SourceArtifact = RegionPrewarmArtifacts.CompatibleBiomeAllowList.GetValue();
		FLayoutProducedCompatibleBiomeAllowListArtifact ValidatedArtifact;
		if (!LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.CompatibleBiomeRowNames,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.CompatibleBiomeAllowList = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.TerrainPathEvidence.IsSet())
	{
		const FLayoutProducedTerrainPathEvidenceArtifact& SourceArtifact = RegionPrewarmArtifacts.TerrainPathEvidence.GetValue();
		FLayoutProducedTerrainPathEvidenceArtifact ValidatedArtifact;
		if (!LayoutTerrainPathEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.TerrainPathSamples,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.TerrainPathEvidence = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.PocketVoidEvidence.IsSet())
	{
		const FLayoutProducedPocketVoidEvidenceArtifact& SourceArtifact = RegionPrewarmArtifacts.PocketVoidEvidence.GetValue();
		FLayoutProducedPocketVoidEvidenceArtifact ValidatedArtifact;
		if (!LayoutPocketVoidEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.PocketVoidIntervals,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.PocketVoidEvidence = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.ExactFallbackEvidence.IsSet())
	{
		const FLayoutProducedExactFallbackEvidenceArtifact& SourceArtifact = RegionPrewarmArtifacts.ExactFallbackEvidence.GetValue();
		FLayoutProducedExactFallbackEvidenceArtifact ValidatedArtifact;
		if (!LayoutExactFallbackEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.Cells,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.ExactFallbackEvidence = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.FlatClearanceEvidence.IsSet())
	{
		const FLayoutProducedFlatClearanceEvidenceArtifact& SourceArtifact = RegionPrewarmArtifacts.FlatClearanceEvidence.GetValue();
		FLayoutProducedFlatClearanceEvidenceArtifact ValidatedArtifact;
		if (!LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.Cells,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.FlatClearanceEvidence = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.AnalyticOverlapEvidence.IsSet())
	{
		const FLayoutProducedAnalyticOverlapEvidenceArtifact& SourceArtifact = RegionPrewarmArtifacts.AnalyticOverlapEvidence.GetValue();
		FLayoutProducedAnalyticOverlapEvidenceArtifact ValidatedArtifact;
		if (!LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.Cells,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.AnalyticOverlapEvidence = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.TerrainPlacementEvidence.IsSet())
	{
		const FLayoutProducedTerrainPlacementEvidenceArtifact& SourceArtifact = RegionPrewarmArtifacts.TerrainPlacementEvidence.GetValue();
		FLayoutProducedTerrainPlacementEvidenceArtifact ValidatedArtifact;
		if (!LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.Cells,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.TerrainPlacementEvidence = MoveTemp(ValidatedArtifact);
	}

	if (RegionPrewarmArtifacts.PrimitiveClassificationEvidence.IsSet())
	{
		const FLayoutProducedTerrainPrimitiveClassificationArtifact& SourceArtifact = RegionPrewarmArtifacts.PrimitiveClassificationEvidence.GetValue();
		FLayoutProducedTerrainPrimitiveClassificationArtifact ValidatedArtifact;
		if (!LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
				SourceArtifact.ArtifactId,
				SourceArtifact.Cells,
				SourceArtifact.Provenance,
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}
		OutProducedArtifacts.TerrainPrimitiveClassificationEvidence = MoveTemp(ValidatedArtifact);
	}

	return true;
}

bool LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
	const FLayoutProducedTerrainEvidenceArtifacts& ProducedArtifacts,
	const FLayoutProducedTerrainEvidenceRequirements& Requirements,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!ValidateProducedArtifactProvenance(ProducedArtifacts, OutFailureReason))
	{
		return false;
	}
	if (!ValidateProducedArtifactIdentities(ProducedArtifacts, OutFailureReason))
	{
		return false;
	}
	if (!LayoutCompatibleBiomeAllowList::TryApplyProducedArtifact(
			ProducedArtifacts.CompatibleBiomeAllowList.IsSet() ? &ProducedArtifacts.CompatibleBiomeAllowList.GetValue() : nullptr,
			Requirements.bRequireCompatibleBiomeAllowList,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutShiftedFootprintProof::TryApplyProducedArtifact(
			ProducedArtifacts.ShiftedFootprintProof.IsSet() ? &ProducedArtifacts.ShiftedFootprintProof.GetValue() : nullptr,
			Requirements.RequiredShiftId,
			Requirements.RequiredShiftCells,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutTerrainPathEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.TerrainPathEvidence.IsSet() ? &ProducedArtifacts.TerrainPathEvidence.GetValue() : nullptr,
			Requirements.bRequireTerrainPathEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutPocketVoidEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.PocketVoidEvidence.IsSet() ? &ProducedArtifacts.PocketVoidEvidence.GetValue() : nullptr,
			Requirements.bRequirePocketVoidEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutExactFallbackEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.ExactFallbackEvidence.IsSet() ? &ProducedArtifacts.ExactFallbackEvidence.GetValue() : nullptr,
			Requirements.bRequireExactFallbackEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutFlatClearanceEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.FlatClearanceEvidence.IsSet() ? &ProducedArtifacts.FlatClearanceEvidence.GetValue() : nullptr,
			Requirements.bRequireFlatClearanceEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutAnalyticOverlapEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.AnalyticOverlapEvidence.IsSet() ? &ProducedArtifacts.AnalyticOverlapEvidence.GetValue() : nullptr,
			Requirements.bRequireAnalyticOverlapEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutTerrainPlacementEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.TerrainPlacementEvidence.IsSet() ? &ProducedArtifacts.TerrainPlacementEvidence.GetValue() : nullptr,
			Requirements.bRequireTerrainPlacementEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	if (!LayoutTerrainPrimitiveClassificationEvidence::TryApplyProducedArtifact(
			ProducedArtifacts.TerrainPrimitiveClassificationEvidence.IsSet() ? &ProducedArtifacts.TerrainPrimitiveClassificationEvidence.GetValue() : nullptr,
			Requirements.bRequireTerrainPrimitiveClassificationEvidence,
			InOutTerrainBiomeArtifact,
			OutFailureReason))
	{
		return false;
	}

	InOutTerrainBiomeArtifact.AuditMessages.Add(TEXT("ProducedTerrainEvidenceAdapter=Applied"));
	return true;
}

bool LayoutProducedTerrainEvidenceAdapter::TryApplyRegionPrewarmEvidenceArtifacts(
	const FLayoutRegionPrewarmEvidenceArtifacts& RegionPrewarmArtifacts,
	const FLayoutProducedTerrainEvidenceRequirements& Requirements,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	FLayoutProducedTerrainEvidenceArtifacts ProducedArtifacts;
	if (!TryBuildArtifactsFromRegionPrewarmEvidence(
			RegionPrewarmArtifacts,
			ProducedArtifacts,
			OutFailureReason))
	{
		return false;
	}
	return TryApplyProducedArtifacts(
		ProducedArtifacts,
		Requirements,
		InOutTerrainBiomeArtifact,
		OutFailureReason);
}

bool LayoutProducedTerrainEvidenceAdapter::TryApplySourceWriterEvidence(
	const FLayoutRegionPrewarmEvidenceSourceWriterInput& SourceWriterInput,
	const FLayoutProducedTerrainEvidenceRequirements& Requirements,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	if (!LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			SourceWriterInput,
			ProducerInput,
			OutFailureReason))
	{
		return false;
	}

	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	if (!LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(
			ProducerInput,
			RegionPrewarmArtifacts,
			OutFailureReason))
	{
		return false;
	}

	return TryApplyRegionPrewarmEvidenceArtifacts(
		RegionPrewarmArtifacts,
		Requirements,
		InOutTerrainBiomeArtifact,
		OutFailureReason);
}

bool LayoutProducedTerrainEvidenceAdapter::TryApplyFrozenTerrainBiomeArtifactEvidence(
	const FLayoutId ProducerId,
	FString Provenance,
	const FLayoutRegionPrewarmEvidenceRequirements& SourceRequirements,
	const FLayoutProducedTerrainEvidenceRequirements& ApplyRequirements,
	FLayoutFrozenTerrainBiomeAdapterInput& InOutTerrainBiomeArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	const FLayoutFrozenTerrainBiomeAdapterInput SourceArtifact = InOutTerrainBiomeArtifact;
	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	if (!LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInputFromFrozenTerrainBiomeArtifact(
			ProducerId,
			MoveTemp(Provenance),
			SourceRequirements,
			SourceArtifact,
			ProducerInput,
			OutFailureReason))
	{
		return false;
	}

	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	if (!LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(
			ProducerInput,
			RegionPrewarmArtifacts,
			OutFailureReason))
	{
		return false;
	}

	return TryApplyRegionPrewarmEvidenceArtifacts(
		RegionPrewarmArtifacts,
		ApplyRequirements,
		InOutTerrainBiomeArtifact,
		OutFailureReason);
}
