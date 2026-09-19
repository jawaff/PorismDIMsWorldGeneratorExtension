// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutRegionPrewarmEvidenceProducer.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutRegionPrewarmEvidenceProducerInput BuildFullExplicitPrewarmEvidenceInput()
	{
		FLayoutRegionPrewarmEvidenceProducerInput Input;
		Input.ProducerId = TEXT("PrewarmEvidence.Producer.0");
		Input.Provenance = TEXT("PrewarmEvidence.Test");

		Input.bHasCompatibleBiomeAllowListSource = true;
		Input.CompatibleBiomeAllowListArtifactId = TEXT("PrewarmEvidence.Biome.0");
		Input.CompatibleBiomeRowNames = { TEXT("Biome.Forest"), TEXT("Biome.Ridge") };

		Input.bHasExactFallbackEvidenceSource = true;
		Input.ExactFallbackArtifactId = TEXT("PrewarmEvidence.ExactFallback.0");
		FLayoutExactFallbackCellEvidence ExactFallbackCell;
		ExactFallbackCell.LocalCell = FIntVector(0, 0, 0);
		ExactFallbackCell.EvidenceKind = ELayoutExactFallbackEvidenceKind::ThresholdBand;
		ExactFallbackCell.ProvenanceId = TEXT("ExactFallback.Cell.0");
		Input.ExactFallbackCells.Add(ExactFallbackCell);

		Input.bHasFlatClearanceEvidenceSource = true;
		Input.FlatClearanceArtifactId = TEXT("PrewarmEvidence.FlatClearance.0");
		FLayoutFlatClearanceCellEvidence FlatClearanceCell;
		FlatClearanceCell.LocalCell = FIntVector(1, 0, 0);
		LayoutLocalBlockCoordinates::TryMakeCoord8(4, FlatClearanceCell.RequiredClearanceBlocks);
		LayoutLocalBlockCoordinates::TryMakeCoord8(5, FlatClearanceCell.ObservedClearanceBlocks);
		FlatClearanceCell.ProvenanceId = TEXT("FlatClearance.Cell.0");
		Input.FlatClearanceCells.Add(FlatClearanceCell);

		Input.bHasAnalyticOverlapEvidenceSource = true;
		Input.AnalyticOverlapArtifactId = TEXT("PrewarmEvidence.AnalyticOverlap.0");
		FLayoutAnalyticOverlapCellEvidence AnalyticOverlapCell;
		AnalyticOverlapCell.LocalCell = FIntVector(2, 0, 0);
		AnalyticOverlapCell.OverlapArea = 16;
		AnalyticOverlapCell.ProvenanceId = TEXT("AnalyticOverlap.Cell.0");
		Input.AnalyticOverlapCells.Add(AnalyticOverlapCell);

		Input.bHasTerrainPlacementEvidenceSource = true;
		Input.TerrainPlacementArtifactId = TEXT("PrewarmEvidence.Placement.0");
		FLayoutTerrainPlacementCellEvidence PlacementCell;
		PlacementCell.Cell = FIntVector(4, 0, 0);
		PlacementCell.bPlaceableForSelectedMode = true;
		PlacementCell.bHasFoundationFillEvidence = true;
		PlacementCell.RequiredFoundationDepth = 1;
		PlacementCell.FoundationMaterial = 1;
		PlacementCell.TerrainStageIndex = 1;
		PlacementCell.VerticalShiftBlocks = 16;
		PlacementCell.ProvenanceId = TEXT("Placement.Cell.0");
		Input.TerrainPlacementCells.Add(PlacementCell);

		Input.bHasPrimitiveClassificationEvidenceSource = true;
		Input.PrimitiveClassificationArtifactId = TEXT("PrewarmEvidence.Primitive.0");
		FLayoutTerrainPrimitiveClassificationCellEvidence PrimitiveCell;
		PrimitiveCell.LocalCell = FIntVector(3, 0, 0);
		PrimitiveCell.Classification = ELayoutTerrainPrimitiveClassification::Supportable;
		PrimitiveCell.ProvenanceId = TEXT("Primitive.Cell.0");
		Input.PrimitiveClassificationCells.Add(PrimitiveCell);

		Input.bHasTerrainPathEvidenceSource = true;
		Input.TerrainPathArtifactId = TEXT("PrewarmEvidence.Path.0");
		FLayoutFrozenTerrainPathSample PathSample;
		PathSample.BlockXY = FIntPoint(10, 11);
		PathSample.SurfaceZ = 64;
		PathSample.bHasClassificationEvidence = true;
		Input.TerrainPathSamples.Add(PathSample);

		Input.bHasPocketVoidEvidenceSource = true;
		Input.PocketVoidArtifactId = TEXT("PrewarmEvidence.PocketVoid.0");
		FLayoutFrozenTerrainVoidIntervalSample VoidInterval;
		VoidInterval.BlockXY = FIntPoint(12, 13);
		VoidInterval.MinZ = 16;
		VoidInterval.MaxZ = 48;
		VoidInterval.bHasVoidEvidence = true;
		Input.PocketVoidIntervals.Add(VoidInterval);

		return Input;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceProducerBuildsExplicitArtifactsTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceProducer.BuildsExplicitArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceProducerBuildsExplicitArtifactsTest::RunTest(const FString& Parameters)
{
	const FLayoutRegionPrewarmEvidenceProducerInput Input = BuildFullExplicitPrewarmEvidenceInput();

	FLayoutRegionPrewarmEvidenceArtifacts Artifacts;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit prewarm evidence sources build produced artifacts"),
		LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(Input, Artifacts, FailureReason));
	TestTrue(TEXT("Compatible biome artifact present"), Artifacts.CompatibleBiomeAllowList.IsSet());
	TestTrue(TEXT("Exact fallback artifact present"), Artifacts.ExactFallbackEvidence.IsSet());
	TestTrue(TEXT("Flat clearance artifact present"), Artifacts.FlatClearanceEvidence.IsSet());
	TestTrue(TEXT("Analytic overlap artifact present"), Artifacts.AnalyticOverlapEvidence.IsSet());
	TestTrue(TEXT("Terrain placement artifact present"), Artifacts.TerrainPlacementEvidence.IsSet());
	TestTrue(TEXT("Primitive classification artifact present"), Artifacts.PrimitiveClassificationEvidence.IsSet());
	TestTrue(TEXT("Terrain path artifact present"), Artifacts.TerrainPathEvidence.IsSet());
	TestTrue(TEXT("Pocket/void artifact present"), Artifacts.PocketVoidEvidence.IsSet());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceProducerRejectsMissingRequiredSourceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceProducer.RejectsMissingRequiredSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceProducerRejectsMissingRequiredSourceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceProducerInput Input;
	Input.ProducerId = TEXT("PrewarmEvidence.Producer.0");
	Input.Provenance = TEXT("PrewarmEvidence.Test");
	Input.Requirements.bRequireTerrainPathEvidence = true;

	FLayoutRegionPrewarmEvidenceArtifacts Artifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required explicit source fails closed"),
		LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(Input, Artifacts, FailureReason));
	TestTrue(TEXT("Missing required source failure reported"), FailureReason.Contains(TEXT("terrain-path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceProducerRejectsUnflaggedExplicitSourceEvidenceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceProducer.RejectsUnflaggedExplicitSourceEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceProducerRejectsUnflaggedExplicitSourceEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceProducerInput Input = BuildFullExplicitPrewarmEvidenceInput();
	Input.bHasTerrainPathEvidenceSource = false;

	FLayoutRegionPrewarmEvidenceArtifacts Artifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Unflagged producer terrain-path arrays fail closed"),
		LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(Input, Artifacts, FailureReason));
	TestTrue(TEXT("Unflagged terrain-path failure reported"), FailureReason.Contains(TEXT("terrain-path")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceProducerRejectsInvalidExplicitSourceTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceProducer.RejectsInvalidExplicitSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceProducerRejectsInvalidExplicitSourceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceProducerInput Input = BuildFullExplicitPrewarmEvidenceInput();
	Input.PocketVoidIntervals[0].MaxZ = Input.PocketVoidIntervals[0].MinZ - 1;

	FLayoutRegionPrewarmEvidenceArtifacts Artifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Present but invalid explicit source fails closed"),
		LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(Input, Artifacts, FailureReason));
	TestTrue(TEXT("Invalid explicit source failure reported"), !FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceProducerRejectsMissingArtifactIdentityTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceProducer.RejectsMissingArtifactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceProducerRejectsMissingArtifactIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceProducerInput Input = BuildFullExplicitPrewarmEvidenceInput();
	Input.TerrainPathArtifactId = NAME_None;

	FLayoutRegionPrewarmEvidenceArtifacts Artifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Missing explicit source artifact id fails closed"),
		LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(Input, Artifacts, FailureReason));
	TestTrue(TEXT("Missing artifact id failure reported"), FailureReason.Contains(TEXT("artifact id")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionPrewarmEvidenceProducerRejectsDuplicateArtifactIdentityTest,
	"PorismExtension.Layout.Terrain.RegionPrewarmEvidenceProducer.RejectsDuplicateArtifactIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionPrewarmEvidenceProducerRejectsDuplicateArtifactIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionPrewarmEvidenceProducerInput Input = BuildFullExplicitPrewarmEvidenceInput();
	Input.PocketVoidArtifactId = Input.TerrainPathArtifactId;

	FLayoutRegionPrewarmEvidenceArtifacts Artifacts;
	FString FailureReason;
	TestFalse(
		TEXT("Duplicate explicit source artifact ids fail closed"),
		LayoutRegionPrewarmEvidenceProducer::TryBuildProducedArtifacts(Input, Artifacts, FailureReason));
	TestTrue(TEXT("Duplicate artifact id failure reported"), FailureReason.Contains(TEXT("duplicate explicit artifact id")));
	return true;
}
