// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutRegionPrewarmEvidenceSourceWriter.h"

namespace
{
	FLayoutId BuildSourceArtifactId(const FLayoutId ProducerId, const TCHAR* Suffix)
	{
		return FLayoutId(*FString::Printf(TEXT("%s.%s"), *ProducerId.ToString(), Suffix));
	}

	bool ValidateSourceWriterFlagsMatchPayloads(
		const FLayoutRegionPrewarmEvidenceSourceWriterInput& Input,
		FString& OutFailureReason)
	{
		if (!Input.bHasCompatibleBiomeAllowListSource && !Input.CompatibleBiomeRowNames.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has compatible-biome rows without the explicit compatible-biome source flag.");
			return false;
		}
		if (!Input.bHasExactFallbackEvidenceSource && !Input.ExactFallbackCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has exact-fallback cells without the explicit exact-fallback source flag.");
			return false;
		}
		if (!Input.bHasFlatClearanceEvidenceSource && !Input.FlatClearanceCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has flat-clearance cells without the explicit flat-clearance source flag.");
			return false;
		}
		if (!Input.bHasAnalyticOverlapEvidenceSource && !Input.AnalyticOverlapCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has analytic-overlap cells without the explicit analytic-overlap source flag.");
			return false;
		}
		if (!Input.bHasTerrainPlacementEvidenceSource && !Input.TerrainPlacementCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has terrain-placement cells without the explicit terrain-placement source flag.");
			return false;
		}
		if (!Input.bHasPrimitiveClassificationEvidenceSource && !Input.PrimitiveClassificationCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has primitive-classification cells without the explicit primitive-classification source flag.");
			return false;
		}
		if (!Input.bHasTerrainPathEvidenceSource && !Input.TerrainPathSamples.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has terrain-path samples without the explicit terrain-path source flag.");
			return false;
		}
		if (!Input.bHasPocketVoidEvidenceSource && !Input.PocketVoidIntervals.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence source writer has pocket/void intervals without the explicit pocket/void source flag.");
			return false;
		}
		return true;
	}

	bool AddSourceArtifactId(
		const bool bHasSource,
		const FLayoutId ArtifactId,
		const TCHAR* SourceName,
		TSet<FLayoutId>& SeenArtifactIds,
		FString& OutFailureReason)
	{
		if (!bHasSource)
		{
			return true;
		}
		if (ArtifactId.IsNone())
		{
			OutFailureReason = FString::Printf(TEXT("Region prewarm evidence source writer requires a stable %s artifact id."), SourceName);
			return false;
		}
		if (SeenArtifactIds.Contains(ArtifactId))
		{
			OutFailureReason = FString::Printf(TEXT("Region prewarm evidence source writer has duplicate explicit artifact id %s."), *ArtifactId.ToString());
			return false;
		}
		SeenArtifactIds.Add(ArtifactId);
		return true;
	}

	bool ValidateSourceWriterArtifactIdentities(
		const FLayoutRegionPrewarmEvidenceSourceWriterInput& Input,
		FString& OutFailureReason)
	{
		TSet<FLayoutId> SeenArtifactIds;
		return AddSourceArtifactId(Input.bHasCompatibleBiomeAllowListSource, Input.CompatibleBiomeAllowListArtifactId, TEXT("compatible-biome"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasExactFallbackEvidenceSource, Input.ExactFallbackArtifactId, TEXT("exact-fallback"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasFlatClearanceEvidenceSource, Input.FlatClearanceArtifactId, TEXT("flat-clearance"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasAnalyticOverlapEvidenceSource, Input.AnalyticOverlapArtifactId, TEXT("analytic-overlap"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasTerrainPlacementEvidenceSource, Input.TerrainPlacementArtifactId, TEXT("terrain-placement"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasPrimitiveClassificationEvidenceSource, Input.PrimitiveClassificationArtifactId, TEXT("primitive-classification"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasTerrainPathEvidenceSource, Input.TerrainPathArtifactId, TEXT("terrain-path"), SeenArtifactIds, OutFailureReason)
			&& AddSourceArtifactId(Input.bHasPocketVoidEvidenceSource, Input.PocketVoidArtifactId, TEXT("pocket/void"), SeenArtifactIds, OutFailureReason);
	}

	bool ValidateFrozenArtifactFlagsMatchPayloads(
		const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
		FString& OutFailureReason)
	{
		if (!FrozenTerrainBiomeArtifact.bHasExactFallbackEvidence && !FrozenTerrainBiomeArtifact.ExactFallbackCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has exact-fallback cells without the explicit exact-fallback evidence flag.");
			return false;
		}
		if (!FrozenTerrainBiomeArtifact.bHasFlatClearanceEvidence && !FrozenTerrainBiomeArtifact.FlatClearanceCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has flat-clearance cells without the explicit flat-clearance evidence flag.");
			return false;
		}
		if (!FrozenTerrainBiomeArtifact.bHasAnalyticOverlapEvidence && !FrozenTerrainBiomeArtifact.AnalyticOverlapCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has analytic-overlap cells without the explicit analytic-overlap evidence flag.");
			return false;
		}
		if (!FrozenTerrainBiomeArtifact.bHasTerrainPlacementEvidence && !FrozenTerrainBiomeArtifact.TerrainPlacementCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has terrain-placement cells without the explicit terrain-placement evidence flag.");
			return false;
		}
		if (!FrozenTerrainBiomeArtifact.bHasTerrainPrimitiveClassificationEvidence && !FrozenTerrainBiomeArtifact.TerrainPrimitiveClassificationCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has primitive-classification cells without the explicit primitive-classification evidence flag.");
			return false;
		}
		if (!FrozenTerrainBiomeArtifact.bHasTerrainPathEvidence && !FrozenTerrainBiomeArtifact.TerrainPathSamples.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has terrain-path samples without the explicit terrain-path evidence flag.");
			return false;
		}
		if (!FrozenTerrainBiomeArtifact.bHasPocketVoidIntervalEvidence && !FrozenTerrainBiomeArtifact.PocketVoidIntervals.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain/biome artifact has pocket/void intervals without the explicit pocket/void evidence flag.");
			return false;
		}
		return true;
	}
}

bool LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
	const FLayoutRegionPrewarmEvidenceSourceWriterInput& Input,
	FLayoutRegionPrewarmEvidenceProducerInput& OutProducerInput,
	FString& OutFailureReason)
{
	OutProducerInput = FLayoutRegionPrewarmEvidenceProducerInput();
	OutFailureReason.Reset();

	if (Input.ProducerId.IsNone())
	{
		OutFailureReason = TEXT("Region prewarm evidence source writer requires a stable producer id.");
		return false;
	}
	if (Input.Provenance.IsEmpty())
	{
		OutFailureReason = TEXT("Region prewarm evidence source writer requires explicit provenance.");
		return false;
	}

	if (!ValidateSourceWriterFlagsMatchPayloads(Input, OutFailureReason))
	{
		return false;
	}
	if (!ValidateSourceWriterArtifactIdentities(Input, OutFailureReason))
	{
		return false;
	}

	FLayoutRegionPrewarmEvidenceProducerInput Candidate;
	Candidate.ProducerId = Input.ProducerId;
	Candidate.Provenance = Input.Provenance;
	Candidate.Requirements = Input.Requirements;

	Candidate.bHasCompatibleBiomeAllowListSource = Input.bHasCompatibleBiomeAllowListSource;
	Candidate.CompatibleBiomeAllowListArtifactId = Input.CompatibleBiomeAllowListArtifactId;
	Candidate.CompatibleBiomeRowNames = Input.CompatibleBiomeRowNames;

	Candidate.bHasExactFallbackEvidenceSource = Input.bHasExactFallbackEvidenceSource;
	Candidate.ExactFallbackArtifactId = Input.ExactFallbackArtifactId;
	Candidate.ExactFallbackCells = Input.ExactFallbackCells;

	Candidate.bHasFlatClearanceEvidenceSource = Input.bHasFlatClearanceEvidenceSource;
	Candidate.FlatClearanceArtifactId = Input.FlatClearanceArtifactId;
	Candidate.FlatClearanceCells = Input.FlatClearanceCells;

	Candidate.bHasAnalyticOverlapEvidenceSource = Input.bHasAnalyticOverlapEvidenceSource;
	Candidate.AnalyticOverlapArtifactId = Input.AnalyticOverlapArtifactId;
	Candidate.AnalyticOverlapCells = Input.AnalyticOverlapCells;

	Candidate.bHasTerrainPlacementEvidenceSource = Input.bHasTerrainPlacementEvidenceSource;
	Candidate.TerrainPlacementArtifactId = Input.TerrainPlacementArtifactId;
	Candidate.TerrainPlacementCells = Input.TerrainPlacementCells;

	Candidate.bHasPrimitiveClassificationEvidenceSource = Input.bHasPrimitiveClassificationEvidenceSource;
	Candidate.PrimitiveClassificationArtifactId = Input.PrimitiveClassificationArtifactId;
	Candidate.PrimitiveClassificationCells = Input.PrimitiveClassificationCells;

	Candidate.bHasTerrainPathEvidenceSource = Input.bHasTerrainPathEvidenceSource;
	Candidate.TerrainPathArtifactId = Input.TerrainPathArtifactId;
	Candidate.TerrainPathSamples = Input.TerrainPathSamples;

	Candidate.bHasPocketVoidEvidenceSource = Input.bHasPocketVoidEvidenceSource;
	Candidate.PocketVoidArtifactId = Input.PocketVoidArtifactId;
	Candidate.PocketVoidIntervals = Input.PocketVoidIntervals;

	FLayoutRegionPrewarmEvidenceArtifacts ScratchArtifacts;
	if (!LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(
			Candidate,
			ScratchArtifacts,
			OutFailureReason))
	{
		return false;
	}

	OutProducerInput = MoveTemp(Candidate);
	return true;
}

bool LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInputFromFrozenTerrainBiomeArtifact(
	const FLayoutId ProducerId,
	FString Provenance,
	const FLayoutRegionPrewarmEvidenceRequirements& Requirements,
	const FLayoutFrozenTerrainBiomeAdapterInput& FrozenTerrainBiomeArtifact,
	FLayoutRegionPrewarmEvidenceProducerInput& OutProducerInput,
	FString& OutFailureReason)
{
	OutProducerInput = FLayoutRegionPrewarmEvidenceProducerInput();
	OutFailureReason.Reset();

	if (!ValidateFrozenArtifactFlagsMatchPayloads(FrozenTerrainBiomeArtifact, OutFailureReason))
	{
		return false;
	}

	FLayoutRegionPrewarmEvidenceSourceWriterInput SourceInput;
	SourceInput.ProducerId = ProducerId;
	SourceInput.Provenance = MoveTemp(Provenance);
	SourceInput.Requirements = Requirements;

	TArray<FName> CompatibleRows = FrozenTerrainBiomeArtifact.EligibleBiomeRowNames;
	if (CompatibleRows.IsEmpty() && !FrozenTerrainBiomeArtifact.EligibleBiomeRowName.IsNone())
	{
		CompatibleRows.Add(FrozenTerrainBiomeArtifact.EligibleBiomeRowName);
	}
	if (!CompatibleRows.IsEmpty())
	{
		SourceInput.bHasCompatibleBiomeAllowListSource = true;
		SourceInput.CompatibleBiomeAllowListArtifactId = BuildSourceArtifactId(ProducerId, TEXT("CompatibleBiomeAllowList"));
		SourceInput.CompatibleBiomeRowNames = MoveTemp(CompatibleRows);
	}

	if (FrozenTerrainBiomeArtifact.bHasExactFallbackEvidence)
	{
		SourceInput.bHasExactFallbackEvidenceSource = true;
		SourceInput.ExactFallbackArtifactId = BuildSourceArtifactId(ProducerId, TEXT("ExactFallback"));
		SourceInput.ExactFallbackCells = FrozenTerrainBiomeArtifact.ExactFallbackCells;
	}

	if (FrozenTerrainBiomeArtifact.bHasFlatClearanceEvidence)
	{
		SourceInput.bHasFlatClearanceEvidenceSource = true;
		SourceInput.FlatClearanceArtifactId = BuildSourceArtifactId(ProducerId, TEXT("FlatClearance"));
		SourceInput.FlatClearanceCells = FrozenTerrainBiomeArtifact.FlatClearanceCells;
	}

	if (FrozenTerrainBiomeArtifact.bHasAnalyticOverlapEvidence)
	{
		SourceInput.bHasAnalyticOverlapEvidenceSource = true;
		SourceInput.AnalyticOverlapArtifactId = BuildSourceArtifactId(ProducerId, TEXT("AnalyticOverlap"));
		SourceInput.AnalyticOverlapCells = FrozenTerrainBiomeArtifact.AnalyticOverlapCells;
	}

	if (FrozenTerrainBiomeArtifact.bHasTerrainPlacementEvidence)
	{
		SourceInput.bHasTerrainPlacementEvidenceSource = true;
		SourceInput.TerrainPlacementArtifactId = BuildSourceArtifactId(ProducerId, TEXT("TerrainPlacement"));
		SourceInput.TerrainPlacementCells = FrozenTerrainBiomeArtifact.TerrainPlacementCells;
	}

	if (FrozenTerrainBiomeArtifact.bHasTerrainPrimitiveClassificationEvidence)
	{
		SourceInput.bHasPrimitiveClassificationEvidenceSource = true;
		SourceInput.PrimitiveClassificationArtifactId = BuildSourceArtifactId(ProducerId, TEXT("PrimitiveClassification"));
		SourceInput.PrimitiveClassificationCells = FrozenTerrainBiomeArtifact.TerrainPrimitiveClassificationCells;
	}

	if (FrozenTerrainBiomeArtifact.bHasTerrainPathEvidence)
	{
		SourceInput.bHasTerrainPathEvidenceSource = true;
		SourceInput.TerrainPathArtifactId = BuildSourceArtifactId(ProducerId, TEXT("TerrainPath"));
		SourceInput.TerrainPathSamples = FrozenTerrainBiomeArtifact.TerrainPathSamples;
	}

	if (FrozenTerrainBiomeArtifact.bHasPocketVoidIntervalEvidence)
	{
		SourceInput.bHasPocketVoidEvidenceSource = true;
		SourceInput.PocketVoidArtifactId = BuildSourceArtifactId(ProducerId, TEXT("PocketVoid"));
		SourceInput.PocketVoidIntervals = FrozenTerrainBiomeArtifact.PocketVoidIntervals;
	}

	return TryBuildProducerInput(
		SourceInput,
		OutProducerInput,
		OutFailureReason);
}
