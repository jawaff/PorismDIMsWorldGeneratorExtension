// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutProducedTerrainEvidenceAdapter.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutFrozenBiomeOwnershipSample MakeBiomeOwnershipSample()
	{
		FLayoutFrozenBiomeOwnershipSample Sample;
		Sample.BlockXY = FIntPoint(16, 0);
		Sample.OwningBiomeRowName = TEXT("Biome.Forest");
		Sample.bOwnedByAllowList = true;
		Sample.bHasSurfaceEvidence = true;
		return Sample;
	}

	FLayoutProducedTerrainEvidenceArtifacts MakeCompleteProducedArtifacts()
	{
		FLayoutProducedTerrainEvidenceArtifacts Artifacts;
		FString FailureReason;

		FLayoutProducedCompatibleBiomeAllowListArtifact AllowList;
		LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
			TEXT("BiomeAllowList.Adapter"),
			{TEXT("Biome.Forest")},
			TEXT("AdapterTest"),
			AllowList,
			FailureReason);
		Artifacts.CompatibleBiomeAllowList = AllowList;

		FLayoutFrozenShiftedFootprintEvidence ShiftedEvidence;
		ShiftedEvidence.PlacementShiftId = TEXT("Shift.East.1");
		ShiftedEvidence.ShiftCells = FIntVector(1, 0, 0);
		FLayoutSteppedTerrainSupportSample& SupportSample = ShiftedEvidence.SteppedSupportSamples.AddDefaulted_GetRef();
		SupportSample.LocalCell = FIntVector::ZeroValue;
		SupportSample.SupportSurfaceZ = 8;
		ShiftedEvidence.BiomeOwnershipSamples.Add(MakeBiomeOwnershipSample());
		ShiftedEvidence.bHasTerrainFitProof = true;
		ShiftedEvidence.bHasReservationCollisionProof = true;
		ShiftedEvidence.bHasChunkOverlapProvenance = true;
		FLayoutProducedShiftedFootprintProofArtifact ShiftedProof;
		LayoutShiftedFootprintProof::TryBuildProducedArtifact(
			TEXT("ShiftProof.Adapter"),
			ShiftedEvidence,
			TEXT("AdapterTest"),
			ShiftedProof,
			FailureReason);
		Artifacts.ShiftedFootprintProof = ShiftedProof;

		FLayoutFrozenTerrainPathSample PathSample;
		PathSample.BlockXY = FIntPoint(16, 0);
		PathSample.SurfaceZ = 8;
		PathSample.bHasClassificationEvidence = true;
		FLayoutProducedTerrainPathEvidenceArtifact PathEvidence;
		LayoutTerrainPathEvidence::TryBuildProducedArtifact(
			TEXT("TerrainPath.Adapter"),
			{PathSample},
			TEXT("AdapterTest"),
			PathEvidence,
			FailureReason);
		Artifacts.TerrainPathEvidence = PathEvidence;

		FLayoutFrozenTerrainVoidIntervalSample VoidInterval;
		VoidInterval.BlockXY = FIntPoint(16, 0);
		VoidInterval.MinZ = -16;
		VoidInterval.MaxZ = 8;
		VoidInterval.bHasVoidEvidence = true;
		FLayoutProducedPocketVoidEvidenceArtifact PocketEvidence;
		LayoutPocketVoidEvidence::TryBuildProducedArtifact(
			TEXT("PocketVoid.Adapter"),
			{VoidInterval},
			TEXT("AdapterTest"),
			PocketEvidence,
			FailureReason);
		Artifacts.PocketVoidEvidence = PocketEvidence;

		FLayoutExactFallbackCellEvidence ExactFallbackCell;
		ExactFallbackCell.LocalCell = FIntVector(1, 0, 0);
		ExactFallbackCell.EvidenceKind = ELayoutExactFallbackEvidenceKind::ThresholdBand;
		ExactFallbackCell.ProvenanceId = TEXT("ExactFallback.Adapter.Cell");
		FLayoutProducedExactFallbackEvidenceArtifact ExactFallbackEvidence;
		LayoutExactFallbackEvidence::TryBuildProducedArtifact(
			TEXT("ExactFallback.Adapter"),
			{ExactFallbackCell},
			TEXT("AdapterTest"),
			ExactFallbackEvidence,
			FailureReason);
		Artifacts.ExactFallbackEvidence = ExactFallbackEvidence;

		FLayoutFlatClearanceCellEvidence FlatClearanceCell;
		FlatClearanceCell.LocalCell = FIntVector(2, 0, 0);
		LayoutLocalBlockCoordinates::TryMakeCoord8(3, FlatClearanceCell.RequiredClearanceBlocks);
		LayoutLocalBlockCoordinates::TryMakeCoord8(4, FlatClearanceCell.ObservedClearanceBlocks);
		FlatClearanceCell.ProvenanceId = TEXT("FlatClearance.Adapter.Cell");
		FLayoutProducedFlatClearanceEvidenceArtifact FlatClearanceEvidence;
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			TEXT("FlatClearance.Adapter"),
			{FlatClearanceCell},
			TEXT("AdapterTest"),
			FlatClearanceEvidence,
			FailureReason);
		Artifacts.FlatClearanceEvidence = FlatClearanceEvidence;

		FLayoutAnalyticOverlapCellEvidence AnalyticOverlapCell;
		AnalyticOverlapCell.LocalCell = FIntVector(3, 0, 0);
		AnalyticOverlapCell.OverlapArea = 12;
		AnalyticOverlapCell.ProvenanceId = TEXT("AnalyticOverlap.Adapter.Cell");
		FLayoutProducedAnalyticOverlapEvidenceArtifact AnalyticOverlapEvidence;
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			TEXT("AnalyticOverlap.Adapter"),
			{AnalyticOverlapCell},
			TEXT("AdapterTest"),
			AnalyticOverlapEvidence,
			FailureReason);
		Artifacts.AnalyticOverlapEvidence = AnalyticOverlapEvidence;

		FLayoutTerrainPlacementCellEvidence PlacementCell;
		PlacementCell.Cell = FIntVector(5, 0, 0);
		PlacementCell.bPlaceableForSelectedMode = true;
		PlacementCell.bHasFoundationFillEvidence = true;
		PlacementCell.RequiredFoundationDepth = 1;
		PlacementCell.FoundationMaterial = 1;
		PlacementCell.ProvenanceId = TEXT("Placement.Adapter.Cell");
		FLayoutProducedTerrainPlacementEvidenceArtifact PlacementEvidence;
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("Placement.Adapter"),
			{PlacementCell},
			TEXT("AdapterTest"),
			PlacementEvidence,
			FailureReason);
		Artifacts.TerrainPlacementEvidence = PlacementEvidence;

		FLayoutTerrainPrimitiveClassificationCellEvidence PrimitiveCell;
		PrimitiveCell.LocalCell = FIntVector(4, 0, 0);
		PrimitiveCell.Classification = ELayoutTerrainPrimitiveClassification::Supportable;
		PrimitiveCell.ProvenanceId = TEXT("Primitive.Adapter.Cell");
		FLayoutProducedTerrainPrimitiveClassificationArtifact PrimitiveEvidence;
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
			TEXT("Primitive.Adapter"),
			{PrimitiveCell},
			TEXT("AdapterTest"),
			PrimitiveEvidence,
			FailureReason);
		Artifacts.TerrainPrimitiveClassificationEvidence = PrimitiveEvidence;

		return Artifacts;
	}

	FLayoutFrozenTerrainBiomeAdapterInput MakeFrozenTerrainBiomeArtifactWithExplicitSources()
	{
		FLayoutFrozenTerrainBiomeAdapterInput Artifact;
		Artifact.EligibleBiomeRowNames = { TEXT("Biome.Forest") };
		Artifact.bRequiresBiomeOwnership = true;
		Artifact.BiomeOwnershipSamples.Add(MakeBiomeOwnershipSample());

		Artifact.bHasExactFallbackEvidence = true;
		FLayoutExactFallbackCellEvidence ExactFallbackCell;
		ExactFallbackCell.LocalCell = FIntVector(1, 0, 0);
		ExactFallbackCell.EvidenceKind = ELayoutExactFallbackEvidenceKind::ThresholdBand;
		ExactFallbackCell.ProvenanceId = TEXT("FrozenTerrain.ExactFallback.Cell");
		Artifact.ExactFallbackCells.Add(ExactFallbackCell);

		Artifact.bHasFlatClearanceEvidence = true;
		FLayoutFlatClearanceCellEvidence FlatClearanceCell;
		FlatClearanceCell.LocalCell = FIntVector(2, 0, 0);
		LayoutLocalBlockCoordinates::TryMakeCoord8(3, FlatClearanceCell.RequiredClearanceBlocks);
		LayoutLocalBlockCoordinates::TryMakeCoord8(4, FlatClearanceCell.ObservedClearanceBlocks);
		FlatClearanceCell.ProvenanceId = TEXT("FrozenTerrain.FlatClearance.Cell");
		Artifact.FlatClearanceCells.Add(FlatClearanceCell);

		Artifact.bHasAnalyticOverlapEvidence = true;
		FLayoutAnalyticOverlapCellEvidence AnalyticOverlapCell;
		AnalyticOverlapCell.LocalCell = FIntVector(3, 0, 0);
		AnalyticOverlapCell.OverlapArea = 12;
		AnalyticOverlapCell.ProvenanceId = TEXT("FrozenTerrain.AnalyticOverlap.Cell");
		Artifact.AnalyticOverlapCells.Add(AnalyticOverlapCell);

		Artifact.bHasTerrainPathEvidence = true;
		FLayoutFrozenTerrainPathSample PathSample;
		PathSample.BlockXY = FIntPoint(16, 0);
		PathSample.SurfaceZ = 8;
		PathSample.bHasClassificationEvidence = true;
		Artifact.TerrainPathSamples.Add(PathSample);

		Artifact.bHasTerrainPlacementEvidence = true;
		FLayoutTerrainPlacementCellEvidence PlacementCell;
		PlacementCell.Cell = FIntVector(5, 0, 0);
		PlacementCell.bPlaceableForSelectedMode = true;
		PlacementCell.ProvenanceId = TEXT("FrozenTerrain.Placement.Cell");
		Artifact.TerrainPlacementCells.Add(PlacementCell);

		Artifact.bHasTerrainPrimitiveClassificationEvidence = true;
		FLayoutTerrainPrimitiveClassificationCellEvidence PrimitiveCell;
		PrimitiveCell.LocalCell = FIntVector(4, 0, 0);
		PrimitiveCell.Classification = ELayoutTerrainPrimitiveClassification::Supportable;
		PrimitiveCell.ProvenanceId = TEXT("FrozenTerrain.Primitive.Cell");
		Artifact.TerrainPrimitiveClassificationCells.Add(PrimitiveCell);

		Artifact.bHasPocketVoidIntervalEvidence = true;
		FLayoutFrozenTerrainVoidIntervalSample VoidInterval;
		VoidInterval.BlockXY = FIntPoint(16, 0);
		VoidInterval.MinZ = -16;
		VoidInterval.MaxZ = 8;
		VoidInterval.bHasVoidEvidence = true;
		Artifact.PocketVoidIntervals.Add(VoidInterval);
		return Artifact;
	}

	FLayoutRegionPrewarmEvidenceSourceWriterInput MakeSourceWriterInputFromCompleteArtifacts()
	{
		const FLayoutProducedTerrainEvidenceArtifacts CompleteArtifacts = MakeCompleteProducedArtifacts();
		FLayoutRegionPrewarmEvidenceSourceWriterInput Input;
		Input.ProducerId = TEXT("Adapter.SourceWriter.0");
		Input.Provenance = TEXT("AdapterSourceWriterTest");

		const FLayoutProducedCompatibleBiomeAllowListArtifact& AllowList = CompleteArtifacts.CompatibleBiomeAllowList.GetValue();
		Input.bHasCompatibleBiomeAllowListSource = true;
		Input.CompatibleBiomeAllowListArtifactId = AllowList.ArtifactId;
		Input.CompatibleBiomeRowNames = AllowList.CompatibleBiomeRowNames;

		const FLayoutProducedTerrainPathEvidenceArtifact& Path = CompleteArtifacts.TerrainPathEvidence.GetValue();
		Input.bHasTerrainPathEvidenceSource = true;
		Input.TerrainPathArtifactId = Path.ArtifactId;
		Input.TerrainPathSamples = Path.TerrainPathSamples;

		const FLayoutProducedPocketVoidEvidenceArtifact& PocketVoid = CompleteArtifacts.PocketVoidEvidence.GetValue();
		Input.bHasPocketVoidEvidenceSource = true;
		Input.PocketVoidArtifactId = PocketVoid.ArtifactId;
		Input.PocketVoidIntervals = PocketVoid.PocketVoidIntervals;

		const FLayoutProducedExactFallbackEvidenceArtifact& ExactFallback = CompleteArtifacts.ExactFallbackEvidence.GetValue();
		Input.bHasExactFallbackEvidenceSource = true;
		Input.ExactFallbackArtifactId = ExactFallback.ArtifactId;
		Input.ExactFallbackCells = ExactFallback.Cells;

		const FLayoutProducedFlatClearanceEvidenceArtifact& FlatClearance = CompleteArtifacts.FlatClearanceEvidence.GetValue();
		Input.bHasFlatClearanceEvidenceSource = true;
		Input.FlatClearanceArtifactId = FlatClearance.ArtifactId;
		Input.FlatClearanceCells = FlatClearance.Cells;

		const FLayoutProducedAnalyticOverlapEvidenceArtifact& AnalyticOverlap = CompleteArtifacts.AnalyticOverlapEvidence.GetValue();
		Input.bHasAnalyticOverlapEvidenceSource = true;
		Input.AnalyticOverlapArtifactId = AnalyticOverlap.ArtifactId;
		Input.AnalyticOverlapCells = AnalyticOverlap.Cells;

		const FLayoutProducedTerrainPlacementEvidenceArtifact& Placement = CompleteArtifacts.TerrainPlacementEvidence.GetValue();
		Input.bHasTerrainPlacementEvidenceSource = true;
		Input.TerrainPlacementArtifactId = Placement.ArtifactId;
		Input.TerrainPlacementCells = Placement.Cells;

		const FLayoutProducedTerrainPrimitiveClassificationArtifact& Primitive = CompleteArtifacts.TerrainPrimitiveClassificationEvidence.GetValue();
		Input.bHasPrimitiveClassificationEvidenceSource = true;
		Input.PrimitiveClassificationArtifactId = Primitive.ArtifactId;
		Input.PrimitiveClassificationCells = Primitive.Cells;
		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterAppliesFrozenTerrainBiomeArtifactEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.AppliesFrozenTerrainBiomeArtifactEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterAppliesFrozenTerrainBiomeArtifactEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceRequirements SourceRequirements;
	SourceRequirements.bRequireCompatibleBiomeAllowList = true;
	SourceRequirements.bRequireTerrainPathEvidence = true;
	SourceRequirements.bRequirePocketVoidEvidence = true;
	SourceRequirements.bRequireTerrainPlacementEvidence = true;
	SourceRequirements.bRequireExactFallbackEvidence = true;
	SourceRequirements.bRequireFlatClearanceEvidence = true;
	SourceRequirements.bRequireAnalyticOverlapEvidence = true;
	SourceRequirements.bRequirePrimitiveClassificationEvidence = true;

	FLayoutProducedTerrainEvidenceRequirements ApplyRequirements;
	ApplyRequirements.bRequireCompatibleBiomeAllowList = true;
	ApplyRequirements.bRequireTerrainPathEvidence = true;
	ApplyRequirements.bRequirePocketVoidEvidence = true;
	ApplyRequirements.bRequireTerrainPlacementEvidence = true;
	ApplyRequirements.bRequireExactFallbackEvidence = true;
	ApplyRequirements.bRequireFlatClearanceEvidence = true;
	ApplyRequirements.bRequireAnalyticOverlapEvidence = true;
	ApplyRequirements.bRequireTerrainPrimitiveClassificationEvidence = true;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact = MakeFrozenTerrainBiomeArtifactWithExplicitSources();
	FString FailureReason;
	TestTrue(
		TEXT("Frozen terrain/biome explicit evidence applies through source writer and central adapter"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyFrozenTerrainBiomeArtifactEvidence(
			TEXT("FrozenTerrain.Adapter.0"),
			TEXT("FrozenTerrainAdapterTest"),
			SourceRequirements,
			ApplyRequirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Compatible biome evidence remains applied"), TerrainArtifact.bRequiresBiomeOwnership);
	TestTrue(TEXT("Terrain path evidence remains applied"), TerrainArtifact.bHasTerrainPathEvidence);
	TestTrue(TEXT("Pocket/void evidence remains applied"), TerrainArtifact.bHasPocketVoidIntervalEvidence);
	TestTrue(TEXT("Terrain placement evidence remains applied"), TerrainArtifact.bHasTerrainPlacementEvidence);
	TestTrue(TEXT("Exact fallback evidence remains applied"), TerrainArtifact.bHasExactFallbackEvidence);
	TestTrue(TEXT("Flat clearance evidence remains applied"), TerrainArtifact.bHasFlatClearanceEvidence);
	TestTrue(TEXT("Analytic overlap evidence remains applied"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	TestTrue(TEXT("Primitive classification evidence remains applied"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	TestFalse(TEXT("Shifted proof not inferred from frozen terrain/biome source"), TerrainArtifact.bHasShiftedFootprintEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsMissingFrozenTerrainBiomeEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsMissingFrozenTerrainBiomeEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsMissingFrozenTerrainBiomeEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceRequirements SourceRequirements;
	SourceRequirements.bRequireAnalyticOverlapEvidence = true;

	FLayoutProducedTerrainEvidenceRequirements ApplyRequirements;
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact = MakeFrozenTerrainBiomeArtifactWithExplicitSources();
	TerrainArtifact.bHasAnalyticOverlapEvidence = false;
	TerrainArtifact.AnalyticOverlapCells.Reset();
	FString FailureReason;
	TestFalse(
		TEXT("Unsupported required frozen terrain/biome source evidence fails closed"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyFrozenTerrainBiomeArtifactEvidence(
			TEXT("FrozenTerrain.Adapter.0"),
			TEXT("FrozenTerrainAdapterTest"),
			SourceRequirements,
			ApplyRequirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Missing analytic overlap source failure reported"), FailureReason.Contains(TEXT("analytic-overlap")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterAppliesSourceWriterEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.AppliesSourceWriterEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterAppliesSourceWriterEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceRequirements Requirements;
	Requirements.bRequireCompatibleBiomeAllowList = true;
	Requirements.bRequireTerrainPathEvidence = true;
	Requirements.bRequirePocketVoidEvidence = true;
	Requirements.bRequireExactFallbackEvidence = true;
	Requirements.bRequireFlatClearanceEvidence = true;
	Requirements.bRequireAnalyticOverlapEvidence = true;
	Requirements.bRequireTerrainPlacementEvidence = true;
	Requirements.bRequireTerrainPrimitiveClassificationEvidence = true;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TerrainArtifact.BiomeOwnershipSamples.Add(MakeBiomeOwnershipSample());
	FString FailureReason;
	TestTrue(
		TEXT("Explicit source-writer evidence applies through producer and central adapter"),
		LayoutProducedTerrainEvidenceAdapter::TryApplySourceWriterEvidence(
			MakeSourceWriterInputFromCompleteArtifacts(),
			Requirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Compatible biome evidence applied from source writer"), TerrainArtifact.bRequiresBiomeOwnership);
	TestTrue(TEXT("Terrain path evidence applied from source writer"), TerrainArtifact.bHasTerrainPathEvidence);
	TestTrue(TEXT("Pocket/void evidence applied from source writer"), TerrainArtifact.bHasPocketVoidIntervalEvidence);
	TestTrue(TEXT("Exact fallback evidence applied from source writer"), TerrainArtifact.bHasExactFallbackEvidence);
	TestTrue(TEXT("Flat clearance evidence applied from source writer"), TerrainArtifact.bHasFlatClearanceEvidence);
	TestTrue(TEXT("Analytic overlap evidence applied from source writer"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	TestTrue(TEXT("Terrain placement evidence applied from source writer"), TerrainArtifact.bHasTerrainPlacementEvidence);
	TestTrue(TEXT("Primitive classification evidence applied from source writer"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	TestFalse(TEXT("Shifted proof not inferred from source writer"), TerrainArtifact.bHasShiftedFootprintEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsInvalidSourceWriterEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsInvalidSourceWriterEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsInvalidSourceWriterEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput SourceWriterInput = MakeSourceWriterInputFromCompleteArtifacts();
	SourceWriterInput.ProducerId = NAME_None;

	FLayoutProducedTerrainEvidenceRequirements Requirements;
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Invalid source-writer input fails closed before adapter apply"),
		LayoutProducedTerrainEvidenceAdapter::TryApplySourceWriterEvidence(
			SourceWriterInput,
			Requirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Invalid source-writer failure reported"), FailureReason.Contains(TEXT("producer id")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterBuildsFromRegionPrewarmEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.BuildsFromRegionPrewarmEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterBuildsFromRegionPrewarmEvidenceTest::RunTest(const FString& Parameters)
{
	const FLayoutProducedTerrainEvidenceArtifacts CompleteArtifacts = MakeCompleteProducedArtifacts();
	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	RegionPrewarmArtifacts.CompatibleBiomeAllowList = CompleteArtifacts.CompatibleBiomeAllowList.GetValue();
	RegionPrewarmArtifacts.TerrainPathEvidence = CompleteArtifacts.TerrainPathEvidence.GetValue();
	RegionPrewarmArtifacts.PocketVoidEvidence = CompleteArtifacts.PocketVoidEvidence.GetValue();
	RegionPrewarmArtifacts.ExactFallbackEvidence = CompleteArtifacts.ExactFallbackEvidence.GetValue();
	RegionPrewarmArtifacts.FlatClearanceEvidence = CompleteArtifacts.FlatClearanceEvidence.GetValue();
	RegionPrewarmArtifacts.AnalyticOverlapEvidence = CompleteArtifacts.AnalyticOverlapEvidence.GetValue();
	RegionPrewarmArtifacts.TerrainPlacementEvidence = CompleteArtifacts.TerrainPlacementEvidence.GetValue();
	RegionPrewarmArtifacts.PrimitiveClassificationEvidence = CompleteArtifacts.TerrainPrimitiveClassificationEvidence.GetValue();

	FLayoutProducedTerrainEvidenceArtifacts ProducedArtifacts;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit region-prewarm evidence artifacts build central adapter artifacts"),
		LayoutProducedTerrainEvidenceAdapter::TryBuildArtifactsFromRegionPrewarmEvidence(
			RegionPrewarmArtifacts,
			ProducedArtifacts,
			FailureReason));
	TestTrue(TEXT("Compatible biome artifact bridged"), ProducedArtifacts.CompatibleBiomeAllowList.IsSet());
	TestFalse(TEXT("Shifted proof is not inferred from region prewarm evidence"), ProducedArtifacts.ShiftedFootprintProof.IsSet());
	TestTrue(TEXT("Terrain path artifact bridged"), ProducedArtifacts.TerrainPathEvidence.IsSet());
	TestTrue(TEXT("Pocket/void artifact bridged"), ProducedArtifacts.PocketVoidEvidence.IsSet());
	TestTrue(TEXT("Exact fallback artifact bridged"), ProducedArtifacts.ExactFallbackEvidence.IsSet());
	TestTrue(TEXT("Flat clearance artifact bridged"), ProducedArtifacts.FlatClearanceEvidence.IsSet());
	TestTrue(TEXT("Analytic overlap artifact bridged"), ProducedArtifacts.AnalyticOverlapEvidence.IsSet());
	TestTrue(TEXT("Terrain placement artifact bridged"), ProducedArtifacts.TerrainPlacementEvidence.IsSet());
	TestTrue(TEXT("Primitive classification artifact bridged"), ProducedArtifacts.TerrainPrimitiveClassificationEvidence.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsInvalidRegionPrewarmEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsInvalidRegionPrewarmEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsInvalidRegionPrewarmEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	FLayoutProducedExactFallbackEvidenceArtifact InvalidExactFallback;
	InvalidExactFallback.bHasProducedArtifact = true;
	InvalidExactFallback.ArtifactId = TEXT("ExactFallback.Invalid");
	InvalidExactFallback.Provenance = TEXT("AdapterTest");
	RegionPrewarmArtifacts.ExactFallbackEvidence = InvalidExactFallback;

	FLayoutProducedTerrainEvidenceArtifacts ProducedArtifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Invalid explicit region-prewarm artifact fails closed"),
		LayoutProducedTerrainEvidenceAdapter::TryBuildArtifactsFromRegionPrewarmEvidence(
			RegionPrewarmArtifacts,
			ProducedArtifacts,
			FailureReason));
	TestTrue(TEXT("Invalid region prewarm failure reported"), FailureReason.Contains(TEXT("Exact fallback")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterAppliesRegionPrewarmEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.AppliesRegionPrewarmEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterAppliesRegionPrewarmEvidenceTest::RunTest(const FString& Parameters)
{
	const FLayoutProducedTerrainEvidenceArtifacts CompleteArtifacts = MakeCompleteProducedArtifacts();
	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	RegionPrewarmArtifacts.CompatibleBiomeAllowList = CompleteArtifacts.CompatibleBiomeAllowList.GetValue();
	RegionPrewarmArtifacts.TerrainPathEvidence = CompleteArtifacts.TerrainPathEvidence.GetValue();
	RegionPrewarmArtifacts.PocketVoidEvidence = CompleteArtifacts.PocketVoidEvidence.GetValue();
	RegionPrewarmArtifacts.ExactFallbackEvidence = CompleteArtifacts.ExactFallbackEvidence.GetValue();
	RegionPrewarmArtifacts.FlatClearanceEvidence = CompleteArtifacts.FlatClearanceEvidence.GetValue();
	RegionPrewarmArtifacts.AnalyticOverlapEvidence = CompleteArtifacts.AnalyticOverlapEvidence.GetValue();
	RegionPrewarmArtifacts.TerrainPlacementEvidence = CompleteArtifacts.TerrainPlacementEvidence.GetValue();
	RegionPrewarmArtifacts.PrimitiveClassificationEvidence = CompleteArtifacts.TerrainPrimitiveClassificationEvidence.GetValue();

	FLayoutProducedTerrainEvidenceRequirements Requirements;
	Requirements.bRequireCompatibleBiomeAllowList = true;
	Requirements.bRequireTerrainPathEvidence = true;
	Requirements.bRequirePocketVoidEvidence = true;
	Requirements.bRequireExactFallbackEvidence = true;
	Requirements.bRequireFlatClearanceEvidence = true;
	Requirements.bRequireAnalyticOverlapEvidence = true;
	Requirements.bRequireTerrainPlacementEvidence = true;
	Requirements.bRequireTerrainPrimitiveClassificationEvidence = true;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TerrainArtifact.BiomeOwnershipSamples.Add(MakeBiomeOwnershipSample());
	FString FailureReason;
	TestTrue(
		TEXT("Explicit region-prewarm evidence artifacts apply through central adapter"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyRegionPrewarmEvidenceArtifacts(
			RegionPrewarmArtifacts,
			Requirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Compatible biome evidence applied from region prewarm"), TerrainArtifact.bRequiresBiomeOwnership);
	TestTrue(TEXT("Terrain path evidence applied from region prewarm"), TerrainArtifact.bHasTerrainPathEvidence);
	TestTrue(TEXT("Pocket/void evidence applied from region prewarm"), TerrainArtifact.bHasPocketVoidIntervalEvidence);
	TestTrue(TEXT("Exact fallback evidence applied from region prewarm"), TerrainArtifact.bHasExactFallbackEvidence);
	TestTrue(TEXT("Flat clearance evidence applied from region prewarm"), TerrainArtifact.bHasFlatClearanceEvidence);
	TestTrue(TEXT("Analytic overlap evidence applied from region prewarm"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	TestTrue(TEXT("Terrain placement evidence applied from region prewarm"), TerrainArtifact.bHasTerrainPlacementEvidence);
	TestTrue(TEXT("Primitive classification evidence applied from region prewarm"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	TestFalse(TEXT("Shifted proof not inferred from region prewarm"), TerrainArtifact.bHasShiftedFootprintEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsMissingRequiredRegionPrewarmEvidenceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsMissingRequiredRegionPrewarmEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsMissingRequiredRegionPrewarmEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceRequirements Requirements;
	Requirements.bRequireAnalyticOverlapEvidence = true;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required region-prewarm evidence fails closed through central adapter"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyRegionPrewarmEvidenceArtifacts(
			FLayoutRegionPrewarmEvidenceArtifacts(),
			Requirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Missing required analytic overlap failure reported"), FailureReason.Contains(TEXT("analytic overlap")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterAppliesCompleteArtifactsTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.AppliesCompleteArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterAppliesCompleteArtifactsTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceRequirements Requirements;
	Requirements.bRequireCompatibleBiomeAllowList = true;
	Requirements.bRequireTerrainPathEvidence = true;
	Requirements.bRequirePocketVoidEvidence = true;
	Requirements.RequiredShiftId = TEXT("Shift.East.1");
	Requirements.RequiredShiftCells = FIntVector(1, 0, 0);
	Requirements.bRequireExactFallbackEvidence = true;
	Requirements.bRequireFlatClearanceEvidence = true;
	Requirements.bRequireAnalyticOverlapEvidence = true;
	Requirements.bRequireTerrainPlacementEvidence = true;
	Requirements.bRequireTerrainPrimitiveClassificationEvidence = true;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TerrainArtifact.BiomeOwnershipSamples.Add(MakeBiomeOwnershipSample());
	FString FailureReason;
	TestTrue(
		TEXT("Complete produced evidence artifacts apply"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
			MakeCompleteProducedArtifacts(),
			Requirements,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Compatible biome ownership required"), TerrainArtifact.bRequiresBiomeOwnership);
	TestTrue(TEXT("Shifted proof applied"), TerrainArtifact.bHasShiftedFootprintEvidence);
	TestTrue(TEXT("Terrain path evidence applied"), TerrainArtifact.bHasTerrainPathEvidence);
	TestTrue(TEXT("Pocket/void evidence applied"), TerrainArtifact.bHasPocketVoidIntervalEvidence);
	TestTrue(TEXT("Exact fallback evidence applied"), TerrainArtifact.bHasExactFallbackEvidence);
	TestTrue(TEXT("Flat clearance evidence applied"), TerrainArtifact.bHasFlatClearanceEvidence);
	TestTrue(TEXT("Analytic overlap evidence applied"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	TestTrue(TEXT("Terrain placement evidence applied"), TerrainArtifact.bHasTerrainPlacementEvidence);
	TestTrue(TEXT("Primitive classification evidence applied"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterOptionalMissingArtifactsStayEmptyTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.OptionalMissingArtifactsStayEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterOptionalMissingArtifactsStayEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceRequirements Requirements;
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TerrainArtifact.BiomeOwnershipSamples.Add(MakeBiomeOwnershipSample());
	FString FailureReason;
	TestTrue(
		TEXT("Optional missing produced evidence artifacts do not fail"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
			FLayoutProducedTerrainEvidenceArtifacts(),
			Requirements,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("No compatible-biome artifact means no inferred ownership requirement"), TerrainArtifact.bRequiresBiomeOwnership);
	TestFalse(TEXT("No shifted proof artifact means no inferred shifted evidence"), TerrainArtifact.bHasShiftedFootprintEvidence);
	TestTrue(TEXT("No compatible-biome artifact leaves allow-list empty"), TerrainArtifact.EligibleBiomeRowNames.IsEmpty());
	TestTrue(TEXT("No shifted proof artifact leaves shifted evidence empty"), TerrainArtifact.ShiftedFootprintEvidence.IsEmpty());
	TestFalse(TEXT("No exact fallback artifact means no inferred exact fallback evidence"), TerrainArtifact.bHasExactFallbackEvidence);
	TestFalse(TEXT("No flat clearance artifact means no inferred flat clearance evidence"), TerrainArtifact.bHasFlatClearanceEvidence);
	TestFalse(TEXT("No analytic overlap artifact means no inferred analytic overlap evidence"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	TestFalse(TEXT("No terrain placement artifact means no inferred terrain placement evidence"), TerrainArtifact.bHasTerrainPlacementEvidence);
	TestFalse(TEXT("No primitive classification artifact means no inferred primitive classification evidence"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsMissingProducedArtifactProvenanceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsMissingProducedArtifactProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsMissingProducedArtifactProvenanceTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceArtifacts Artifacts = MakeCompleteProducedArtifacts();
	Artifacts.AnalyticOverlapEvidence.GetValue().Provenance.Reset();

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing produced-artifact provenance fails closed before apply"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
			Artifacts,
			FLayoutProducedTerrainEvidenceRequirements(),
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Missing provenance failure reported"), FailureReason.Contains(TEXT("provenance")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsMissingRegionPrewarmProvenanceTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsMissingRegionPrewarmProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsMissingRegionPrewarmProvenanceTest::RunTest(const FString& Parameters)
{
	const FLayoutProducedTerrainEvidenceArtifacts CompleteArtifacts = MakeCompleteProducedArtifacts();
	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	RegionPrewarmArtifacts.TerrainPlacementEvidence = CompleteArtifacts.TerrainPlacementEvidence.GetValue();
	RegionPrewarmArtifacts.TerrainPlacementEvidence.GetValue().Provenance.Reset();

	FLayoutProducedTerrainEvidenceArtifacts ProducedArtifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Missing region-prewarm provenance fails closed before bridge"),
		LayoutProducedTerrainEvidenceAdapter::TryBuildArtifactsFromRegionPrewarmEvidence(
			RegionPrewarmArtifacts,
			ProducedArtifacts,
			FailureReason));
	TestTrue(TEXT("Missing region-prewarm provenance failure reported"), FailureReason.Contains(TEXT("provenance")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsMissingRequiredArtifactsTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsMissingRequiredArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsMissingRequiredArtifactsTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceRequirements Requirements;
	Requirements.bRequireCompatibleBiomeAllowList = true;
	Requirements.bRequireTerrainPathEvidence = true;
	Requirements.bRequirePocketVoidEvidence = true;
	Requirements.RequiredShiftId = TEXT("Shift.East.1");
	Requirements.RequiredShiftCells = FIntVector(1, 0, 0);
	Requirements.bRequireExactFallbackEvidence = true;
	Requirements.bRequireFlatClearanceEvidence = true;
	Requirements.bRequireAnalyticOverlapEvidence = true;
	Requirements.bRequireTerrainPlacementEvidence = true;
	Requirements.bRequireTerrainPrimitiveClassificationEvidence = true;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required produced evidence artifacts fail closed"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
			FLayoutProducedTerrainEvidenceArtifacts(),
			Requirements,
			TerrainArtifact,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsDuplicateProducedArtifactIdentityTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsDuplicateProducedArtifactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsDuplicateProducedArtifactIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainEvidenceArtifacts Artifacts = MakeCompleteProducedArtifacts();
	Artifacts.TerrainPlacementEvidence.GetValue().ArtifactId = Artifacts.FlatClearanceEvidence.GetValue().ArtifactId;

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Duplicate produced artifact ids fail closed before apply"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
			Artifacts,
			FLayoutProducedTerrainEvidenceRequirements(),
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Duplicate artifact id failure reported"), FailureReason.Contains(TEXT("duplicate explicit artifact id")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProducedTerrainEvidenceAdapterRejectsDuplicateRegionPrewarmArtifactIdentityTest,
	"PorismExtension.Layout.Terrain.ProducedEvidenceAdapter.RejectsDuplicateRegionPrewarmArtifactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProducedTerrainEvidenceAdapterRejectsDuplicateRegionPrewarmArtifactIdentityTest::RunTest(const FString& Parameters)
{
	const FLayoutProducedTerrainEvidenceArtifacts CompleteArtifacts = MakeCompleteProducedArtifacts();
	FLayoutRegionPrewarmEvidenceArtifacts RegionPrewarmArtifacts;
	RegionPrewarmArtifacts.TerrainPlacementEvidence = CompleteArtifacts.TerrainPlacementEvidence.GetValue();
	RegionPrewarmArtifacts.FlatClearanceEvidence = CompleteArtifacts.FlatClearanceEvidence.GetValue();
	RegionPrewarmArtifacts.TerrainPlacementEvidence.GetValue().ArtifactId = RegionPrewarmArtifacts.FlatClearanceEvidence.GetValue().ArtifactId;

	FLayoutProducedTerrainEvidenceArtifacts ProducedArtifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Duplicate region-prewarm artifact ids fail closed before bridge"),
		LayoutProducedTerrainEvidenceAdapter::TryBuildArtifactsFromRegionPrewarmEvidence(
			RegionPrewarmArtifacts,
			ProducedArtifacts,
			FailureReason));
	TestTrue(TEXT("Duplicate region-prewarm artifact id failure reported"), FailureReason.Contains(TEXT("duplicate explicit artifact id")));
	return true;
}
