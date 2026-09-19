// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutRegionPrewarmEvidenceProducer.h"

namespace
{
	bool RequireSource(const bool bHasSource, const bool bRequireSource, const TCHAR* MissingReason, FString& OutFailureReason)
	{
		if (!bHasSource && bRequireSource)
		{
			OutFailureReason = MissingReason;
			return false;
		}
		return true;
	}

	bool AddProducerArtifactId(
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
			OutFailureReason = FString::Printf(TEXT("Region prewarm evidence producer requires a stable %s artifact id."), SourceName);
			return false;
		}
		if (SeenArtifactIds.Contains(ArtifactId))
		{
			OutFailureReason = FString::Printf(TEXT("Region prewarm evidence producer has duplicate explicit artifact id %s."), *ArtifactId.ToString());
			return false;
		}
		SeenArtifactIds.Add(ArtifactId);
		return true;
	}

	bool ValidateProducerArtifactIdentities(
		const FLayoutRegionPrewarmEvidenceProducerInput& Input,
		FString& OutFailureReason)
	{
		TSet<FLayoutId> SeenArtifactIds;
		return AddProducerArtifactId(Input.bHasCompatibleBiomeAllowListSource, Input.CompatibleBiomeAllowListArtifactId, TEXT("compatible-biome"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasExactFallbackEvidenceSource, Input.ExactFallbackArtifactId, TEXT("exact-fallback"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasFlatClearanceEvidenceSource, Input.FlatClearanceArtifactId, TEXT("flat-clearance"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasAnalyticOverlapEvidenceSource, Input.AnalyticOverlapArtifactId, TEXT("analytic-overlap"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasTerrainPlacementEvidenceSource, Input.TerrainPlacementArtifactId, TEXT("terrain-placement"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasPrimitiveClassificationEvidenceSource, Input.PrimitiveClassificationArtifactId, TEXT("primitive-classification"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasTerrainPathEvidenceSource, Input.TerrainPathArtifactId, TEXT("terrain-path"), SeenArtifactIds, OutFailureReason)
			&& AddProducerArtifactId(Input.bHasPocketVoidEvidenceSource, Input.PocketVoidArtifactId, TEXT("pocket/void"), SeenArtifactIds, OutFailureReason);
	}

	bool ValidateProducerFlagsMatchPayloads(
		const FLayoutRegionPrewarmEvidenceProducerInput& Input,
		FString& OutFailureReason)
	{
		if (!Input.bHasCompatibleBiomeAllowListSource && !Input.CompatibleBiomeRowNames.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has compatible-biome rows without the explicit compatible-biome source flag.");
			return false;
		}
		if (!Input.bHasExactFallbackEvidenceSource && !Input.ExactFallbackCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has exact-fallback cells without the explicit exact-fallback source flag.");
			return false;
		}
		if (!Input.bHasFlatClearanceEvidenceSource && !Input.FlatClearanceCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has flat-clearance cells without the explicit flat-clearance source flag.");
			return false;
		}
		if (!Input.bHasAnalyticOverlapEvidenceSource && !Input.AnalyticOverlapCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has analytic-overlap cells without the explicit analytic-overlap source flag.");
			return false;
		}
		if (!Input.bHasTerrainPlacementEvidenceSource && !Input.TerrainPlacementCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has terrain-placement cells without the explicit terrain-placement source flag.");
			return false;
		}
		if (!Input.bHasPrimitiveClassificationEvidenceSource && !Input.PrimitiveClassificationCells.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has primitive-classification cells without the explicit primitive-classification source flag.");
			return false;
		}
		if (!Input.bHasTerrainPathEvidenceSource && !Input.TerrainPathSamples.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has terrain-path samples without the explicit terrain-path source flag.");
			return false;
		}
		if (!Input.bHasPocketVoidEvidenceSource && !Input.PocketVoidIntervals.IsEmpty())
		{
			OutFailureReason = TEXT("Region prewarm evidence producer has pocket/void intervals without the explicit pocket/void source flag.");
			return false;
		}
		return true;
	}
}

bool LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(
	const FLayoutRegionPrewarmEvidenceProducerInput& Input,
	FLayoutRegionPrewarmEvidenceArtifacts& OutArtifacts,
	FString& OutFailureReason)
{
	OutArtifacts = FLayoutRegionPrewarmEvidenceArtifacts();
	OutFailureReason.Reset();

	if (Input.ProducerId.IsNone())
	{
		OutFailureReason = TEXT("Region prewarm evidence producer requires a stable producer id.");
		return false;
	}
	if (Input.Provenance.IsEmpty())
	{
		OutFailureReason = TEXT("Region prewarm evidence producer requires explicit provenance.");
		return false;
	}
	if (!ValidateProducerFlagsMatchPayloads(Input, OutFailureReason))
	{
		return false;
	}
	if (!ValidateProducerArtifactIdentities(Input, OutFailureReason))
	{
		return false;
	}

	if (!RequireSource(
			Input.bHasCompatibleBiomeAllowListSource,
			Input.Requirements.bRequireCompatibleBiomeAllowList,
			TEXT("Region prewarm evidence requires an explicit compatible-biome allow-list source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasCompatibleBiomeAllowListSource)
	{
		FLayoutProducedCompatibleBiomeAllowListArtifact Artifact;
		if (!LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
				Input.CompatibleBiomeAllowListArtifactId,
				Input.CompatibleBiomeRowNames,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.CompatibleBiomeAllowList = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasExactFallbackEvidenceSource,
			Input.Requirements.bRequireExactFallbackEvidence,
			TEXT("Region prewarm evidence requires explicit exact-fallback evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasExactFallbackEvidenceSource)
	{
		FLayoutProducedExactFallbackEvidenceArtifact Artifact;
		if (!LayoutExactFallbackEvidence::TryBuildProducedArtifact(
				Input.ExactFallbackArtifactId,
				Input.ExactFallbackCells,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.ExactFallbackEvidence = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasFlatClearanceEvidenceSource,
			Input.Requirements.bRequireFlatClearanceEvidence,
			TEXT("Region prewarm evidence requires explicit flat-clearance evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasFlatClearanceEvidenceSource)
	{
		FLayoutProducedFlatClearanceEvidenceArtifact Artifact;
		if (!LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
				Input.FlatClearanceArtifactId,
				Input.FlatClearanceCells,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.FlatClearanceEvidence = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasAnalyticOverlapEvidenceSource,
			Input.Requirements.bRequireAnalyticOverlapEvidence,
			TEXT("Region prewarm evidence requires explicit analytic-overlap evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasAnalyticOverlapEvidenceSource)
	{
		FLayoutProducedAnalyticOverlapEvidenceArtifact Artifact;
		if (!LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
				Input.AnalyticOverlapArtifactId,
				Input.AnalyticOverlapCells,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.AnalyticOverlapEvidence = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasTerrainPlacementEvidenceSource,
			Input.Requirements.bRequireTerrainPlacementEvidence,
			TEXT("Region prewarm evidence requires explicit terrain-placement evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasTerrainPlacementEvidenceSource)
	{
		FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
		if (!LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
				Input.TerrainPlacementArtifactId,
				Input.TerrainPlacementCells,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.TerrainPlacementEvidence = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasPrimitiveClassificationEvidenceSource,
			Input.Requirements.bRequirePrimitiveClassificationEvidence,
			TEXT("Region prewarm evidence requires explicit primitive-classification evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasPrimitiveClassificationEvidenceSource)
	{
		FLayoutProducedTerrainPrimitiveClassificationArtifact Artifact;
		if (!LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
				Input.PrimitiveClassificationArtifactId,
				Input.PrimitiveClassificationCells,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.PrimitiveClassificationEvidence = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasTerrainPathEvidenceSource,
			Input.Requirements.bRequireTerrainPathEvidence,
			TEXT("Region prewarm evidence requires explicit terrain-path evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasTerrainPathEvidenceSource)
	{
		FLayoutProducedTerrainPathEvidenceArtifact Artifact;
		if (!LayoutTerrainPathEvidence::TryBuildProducedArtifact(
				Input.TerrainPathArtifactId,
				Input.TerrainPathSamples,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.TerrainPathEvidence = MoveTemp(Artifact);
	}

	if (!RequireSource(
			Input.bHasPocketVoidEvidenceSource,
			Input.Requirements.bRequirePocketVoidEvidence,
			TEXT("Region prewarm evidence requires explicit pocket/void evidence source."),
			OutFailureReason))
	{
		return false;
	}
	if (Input.bHasPocketVoidEvidenceSource)
	{
		FLayoutProducedPocketVoidEvidenceArtifact Artifact;
		if (!LayoutPocketVoidEvidence::TryBuildProducedArtifact(
				Input.PocketVoidArtifactId,
				Input.PocketVoidIntervals,
				Input.Provenance,
				Artifact,
				OutFailureReason))
		{
			return false;
		}
		OutArtifacts.PocketVoidEvidence = MoveTemp(Artifact);
	}

	return true;
}
