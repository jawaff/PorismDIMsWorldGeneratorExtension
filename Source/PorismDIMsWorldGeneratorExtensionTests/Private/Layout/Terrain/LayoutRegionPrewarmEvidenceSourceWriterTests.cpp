// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutRegionPrewarmEvidenceSourceWriter.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput BuildExplicitSourceWriterInput()
	{
		FLayoutRegionPrewarmEvidenceSourceWriterInput Input;
		Input.ProducerId = TEXT("SourceWriter.Producer.0");
		Input.Provenance = TEXT("SourceWriter.Test");

		Input.bHasCompatibleBiomeAllowListSource = true;
		Input.CompatibleBiomeAllowListArtifactId = TEXT("SourceWriter.Biome.0");
		Input.CompatibleBiomeRowNames = { TEXT("Biome.Forest") };

		Input.bHasExactFallbackEvidenceSource = true;
		Input.ExactFallbackArtifactId = TEXT("SourceWriter.ExactFallback.0");
		FLayoutExactFallbackCellEvidence ExactFallbackCell;
		ExactFallbackCell.LocalCell = FIntVector(0, 0, 0);
		ExactFallbackCell.EvidenceKind = ELayoutExactFallbackEvidenceKind::ThresholdBand;
		ExactFallbackCell.ProvenanceId = TEXT("SourceWriter.ExactFallback.Cell.0");
		Input.ExactFallbackCells.Add(ExactFallbackCell);

		Input.bHasFlatClearanceEvidenceSource = true;
		Input.FlatClearanceArtifactId = TEXT("SourceWriter.FlatClearance.0");
		FLayoutFlatClearanceCellEvidence FlatClearanceCell;
		FlatClearanceCell.LocalCell = FIntVector(1, 0, 0);
		LayoutLocalBlockCoordinates::TryMakeCoord8(4, FlatClearanceCell.RequiredClearanceBlocks);
		LayoutLocalBlockCoordinates::TryMakeCoord8(5, FlatClearanceCell.ObservedClearanceBlocks);
		FlatClearanceCell.ProvenanceId = TEXT("SourceWriter.FlatClearance.Cell.0");
		Input.FlatClearanceCells.Add(FlatClearanceCell);

		Input.bHasAnalyticOverlapEvidenceSource = true;
		Input.AnalyticOverlapArtifactId = TEXT("SourceWriter.AnalyticOverlap.0");
		FLayoutAnalyticOverlapCellEvidence AnalyticOverlapCell;
		AnalyticOverlapCell.LocalCell = FIntVector(2, 0, 0);
		AnalyticOverlapCell.OverlapArea = 16;
		AnalyticOverlapCell.ProvenanceId = TEXT("SourceWriter.AnalyticOverlap.Cell.0");
		Input.AnalyticOverlapCells.Add(AnalyticOverlapCell);

		Input.bHasTerrainPlacementEvidenceSource = true;
		Input.TerrainPlacementArtifactId = TEXT("SourceWriter.Placement.0");
		FLayoutTerrainPlacementCellEvidence PlacementCell;
		PlacementCell.Cell = FIntVector(4, 0, 0);
		PlacementCell.bPlaceableForSelectedMode = true;
		PlacementCell.bHasFoundationFillEvidence = true;
		PlacementCell.RequiredFoundationDepth = 1;
		PlacementCell.FoundationMaterial = 1;
		PlacementCell.TerrainStageIndex = 1;
		PlacementCell.VerticalShiftBlocks = 16;
		PlacementCell.ProvenanceId = TEXT("SourceWriter.Placement.Cell.0");
		Input.TerrainPlacementCells.Add(PlacementCell);

		Input.bHasPrimitiveClassificationEvidenceSource = true;
		Input.PrimitiveClassificationArtifactId = TEXT("SourceWriter.Primitive.0");
		FLayoutTerrainPrimitiveClassificationCellEvidence PrimitiveCell;
		PrimitiveCell.LocalCell = FIntVector(3, 0, 0);
		PrimitiveCell.Classification = ELayoutTerrainPrimitiveClassification::Supportable;
		PrimitiveCell.ProvenanceId = TEXT("SourceWriter.Primitive.Cell.0");
		Input.PrimitiveClassificationCells.Add(PrimitiveCell);

		Input.bHasTerrainPathEvidenceSource = true;
		Input.TerrainPathArtifactId = TEXT("SourceWriter.Path.0");
		FLayoutFrozenTerrainPathSample PathSample;
		PathSample.BlockXY = FIntPoint(10, 11);
		PathSample.SurfaceZ = 64;
		PathSample.bHasClassificationEvidence = true;
		Input.TerrainPathSamples.Add(PathSample);

		Input.bHasPocketVoidEvidenceSource = true;
		Input.PocketVoidArtifactId = TEXT("SourceWriter.PocketVoid.0");
		FLayoutFrozenTerrainVoidIntervalSample VoidInterval;
		VoidInterval.BlockXY = FIntPoint(12, 13);
		VoidInterval.MinZ = 16;
		VoidInterval.MaxZ = 48;
		VoidInterval.bHasVoidEvidence = true;
		Input.PocketVoidIntervals.Add(VoidInterval);

		return Input;
	}

	FLayoutFrozenTerrainBiomeAdapterInput BuildFrozenTerrainBiomeArtifactWithExplicitSources()
	{
		FLayoutFrozenTerrainBiomeAdapterInput Artifact;
		Artifact.EligibleBiomeRowNames = { TEXT("Biome.Forest") };
		Artifact.bRequiresBiomeOwnership = true;

		Artifact.bHasExactFallbackEvidence = true;
		FLayoutExactFallbackCellEvidence ExactFallbackCell;
		ExactFallbackCell.LocalCell = FIntVector(0, 0, 0);
		ExactFallbackCell.EvidenceKind = ELayoutExactFallbackEvidenceKind::ThresholdBand;
		ExactFallbackCell.ProvenanceId = TEXT("FrozenTerrain.ExactFallback.Cell.0");
		Artifact.ExactFallbackCells.Add(ExactFallbackCell);

		Artifact.bHasFlatClearanceEvidence = true;
		FLayoutFlatClearanceCellEvidence FlatClearanceCell;
		FlatClearanceCell.LocalCell = FIntVector(1, 0, 0);
		LayoutLocalBlockCoordinates::TryMakeCoord8(4, FlatClearanceCell.RequiredClearanceBlocks);
		LayoutLocalBlockCoordinates::TryMakeCoord8(5, FlatClearanceCell.ObservedClearanceBlocks);
		FlatClearanceCell.ProvenanceId = TEXT("FrozenTerrain.FlatClearance.Cell.0");
		Artifact.FlatClearanceCells.Add(FlatClearanceCell);

		Artifact.bHasAnalyticOverlapEvidence = true;
		FLayoutAnalyticOverlapCellEvidence AnalyticOverlapCell;
		AnalyticOverlapCell.LocalCell = FIntVector(2, 0, 0);
		AnalyticOverlapCell.OverlapArea = 16;
		AnalyticOverlapCell.ProvenanceId = TEXT("FrozenTerrain.AnalyticOverlap.Cell.0");
		Artifact.AnalyticOverlapCells.Add(AnalyticOverlapCell);

		Artifact.bHasTerrainPathEvidence = true;
		FLayoutFrozenTerrainPathSample PathSample;
		PathSample.BlockXY = FIntPoint(10, 11);
		PathSample.SurfaceZ = 64;
		PathSample.bHasClassificationEvidence = true;
		Artifact.TerrainPathSamples.Add(PathSample);

		Artifact.bHasTerrainPlacementEvidence = true;
		FLayoutTerrainPlacementCellEvidence PlacementCell;
		PlacementCell.Cell = FIntVector(4, 0, 0);
		PlacementCell.bPlaceableForSelectedMode = true;
		PlacementCell.ProvenanceId = TEXT("FrozenTerrain.Placement.Cell.0");
		Artifact.TerrainPlacementCells.Add(PlacementCell);

		Artifact.bHasTerrainPrimitiveClassificationEvidence = true;
		FLayoutTerrainPrimitiveClassificationCellEvidence PrimitiveCell;
		PrimitiveCell.LocalCell = FIntVector(3, 0, 0);
		PrimitiveCell.Classification = ELayoutTerrainPrimitiveClassification::Supportable;
		PrimitiveCell.ProvenanceId = TEXT("FrozenTerrain.Primitive.Cell.0");
		Artifact.TerrainPrimitiveClassificationCells.Add(PrimitiveCell);

		Artifact.bHasPocketVoidIntervalEvidence = true;
		FLayoutFrozenTerrainVoidIntervalSample VoidInterval;
		VoidInterval.BlockXY = FIntPoint(12, 13);
		VoidInterval.MinZ = 16;
		VoidInterval.MaxZ = 48;
		VoidInterval.bHasVoidEvidence = true;
		Artifact.PocketVoidIntervals.Add(VoidInterval);
		return Artifact;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterBuildsProducerInputTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.BuildsProducerInput",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterBuildsProducerInputTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit source evidence builds prewarm producer input"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			BuildExplicitSourceWriterInput(),
			ProducerInput,
			FailureReason));
	TestEqual(TEXT("Producer id copied"), ProducerInput.ProducerId, FLayoutId(TEXT("SourceWriter.Producer.0")));
	TestTrue(TEXT("Compatible biome source flag copied"), ProducerInput.bHasCompatibleBiomeAllowListSource);
	TestTrue(TEXT("Terrain placement source copied"), ProducerInput.bHasTerrainPlacementEvidenceSource);
	TestTrue(TEXT("Terrain path source copied"), ProducerInput.bHasTerrainPathEvidenceSource);
	TestTrue(TEXT("Pocket/void source copied"), ProducerInput.bHasPocketVoidEvidenceSource);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingRequiredSourceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsMissingRequiredSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingRequiredSourceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput Input;
	Input.ProducerId = TEXT("SourceWriter.Producer.0");
	Input.Provenance = TEXT("SourceWriter.Test");
	Input.Requirements.bRequirePocketVoidEvidence = true;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required explicit source fails closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			Input,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Missing required source failure reported"), FailureReason.Contains(TEXT("pocket/void")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsInvalidExplicitSourceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsInvalidExplicitSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsInvalidExplicitSourceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput Input = BuildExplicitSourceWriterInput();
	Input.FlatClearanceCells[0].ObservedClearanceBlocks = FLayoutLocalBlockCoord8(0);

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Invalid explicit source fails closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			Input,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Invalid source failure reported"), FailureReason.Contains(TEXT("Flat-clearance")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceRequirements Requirements;
	Requirements.bRequireCompatibleBiomeAllowList = true;
	Requirements.bRequireTerrainPathEvidence = true;
	Requirements.bRequirePocketVoidEvidence = true;
	Requirements.bRequireExactFallbackEvidence = true;
	Requirements.bRequireFlatClearanceEvidence = true;
	Requirements.bRequireAnalyticOverlapEvidence = true;
	Requirements.bRequireTerrainPlacementEvidence = true;
	Requirements.bRequirePrimitiveClassificationEvidence = true;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen terrain/biome artifact explicit evidence builds producer input"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInputFromFrozenTerrainBiomeArtifact(
			TEXT("FrozenTerrain.Source.0"),
			TEXT("FrozenTerrainSourceWriterTest"),
			Requirements,
			BuildFrozenTerrainBiomeArtifactWithExplicitSources(),
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Compatible biome source copied from frozen artifact"), ProducerInput.bHasCompatibleBiomeAllowListSource);
	TestTrue(TEXT("Terrain path source copied from frozen artifact"), ProducerInput.bHasTerrainPathEvidenceSource);
	TestTrue(TEXT("Pocket/void source copied from frozen artifact"), ProducerInput.bHasPocketVoidEvidenceSource);
	TestTrue(TEXT("Terrain placement source copied from frozen artifact"), ProducerInput.bHasTerrainPlacementEvidenceSource);
	TestTrue(TEXT("Exact fallback source copied from frozen artifact"), ProducerInput.bHasExactFallbackEvidenceSource);
	TestTrue(TEXT("Flat clearance source copied from frozen artifact"), ProducerInput.bHasFlatClearanceEvidenceSource);
	TestTrue(TEXT("Analytic overlap source copied from frozen artifact"), ProducerInput.bHasAnalyticOverlapEvidenceSource);
	TestTrue(TEXT("Primitive classification source copied from frozen artifact"), ProducerInput.bHasPrimitiveClassificationEvidenceSource);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingFrozenArtifactRequiredEvidenceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsMissingFrozenArtifactRequiredEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingFrozenArtifactRequiredEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceRequirements Requirements;
	Requirements.bRequireExactFallbackEvidence = true;
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact = BuildFrozenTerrainBiomeArtifactWithExplicitSources();
	FrozenArtifact.bHasExactFallbackEvidence = false;
	FrozenArtifact.ExactFallbackCells.Reset();

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Unsupported required evidence fails closed when frozen artifact lacks explicit source arrays"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInputFromFrozenTerrainBiomeArtifact(
			TEXT("FrozenTerrain.Source.0"),
			TEXT("FrozenTerrainSourceWriterTest"),
			Requirements,
			FrozenArtifact,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Missing exact fallback source failure reported"), FailureReason.Contains(TEXT("exact-fallback")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsUnflaggedExplicitSourceEvidenceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsUnflaggedExplicitSourceEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsUnflaggedExplicitSourceEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput Input = BuildExplicitSourceWriterInput();
	Input.bHasTerrainPlacementEvidenceSource = false;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Unflagged source-writer terrain-placement arrays fail closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			Input,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Unflagged terrain-placement failure reported"), FailureReason.Contains(TEXT("terrain-placement")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsUnflaggedFrozenArtifactEvidenceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsUnflaggedFrozenArtifactEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsUnflaggedFrozenArtifactEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact = BuildFrozenTerrainBiomeArtifactWithExplicitSources();
	FrozenArtifact.bHasAnalyticOverlapEvidence = false;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Unflagged frozen analytic-overlap arrays fail closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInputFromFrozenTerrainBiomeArtifact(
			TEXT("FrozenTerrain.Source.0"),
			TEXT("FrozenTerrainSourceWriterTest"),
			FLayoutRegionPrewarmEvidenceRequirements(),
			FrozenArtifact,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Unflagged analytic-overlap failure reported"), FailureReason.Contains(TEXT("analytic-overlap")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingProducerIdentityTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsMissingProducerIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingProducerIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput Input = BuildExplicitSourceWriterInput();
	Input.ProducerId = NAME_None;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Missing producer id fails closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			Input,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Missing producer id failure reported"), FailureReason.Contains(TEXT("producer id")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingArtifactIdentityTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsMissingArtifactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsMissingArtifactIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput Input = BuildExplicitSourceWriterInput();
	Input.FlatClearanceArtifactId = NAME_None;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required explicit source artifact id fails closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			Input,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Missing artifact id failure reported"), FailureReason.Contains(TEXT("artifact id")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceSourceWriterRejectsDuplicateArtifactIdentityTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceSourceWriter.RejectsDuplicateArtifactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceSourceWriterRejectsDuplicateArtifactIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceSourceWriterInput Input = BuildExplicitSourceWriterInput();
	Input.TerrainPlacementArtifactId = Input.FlatClearanceArtifactId;

	FLayoutRegionPrewarmEvidenceProducerInput ProducerInput;
	FString FailureReason;
	TestFalse(
		TEXT("Duplicate explicit source artifact ids fail closed"),
		LayoutRegionPrewarmEvidenceSourceWriter::TryBuildProducerInput(
			Input,
			ProducerInput,
			FailureReason));
	TestTrue(TEXT("Duplicate artifact id failure reported"), FailureReason.Contains(TEXT("duplicate explicit artifact id")));
	return true;
}
