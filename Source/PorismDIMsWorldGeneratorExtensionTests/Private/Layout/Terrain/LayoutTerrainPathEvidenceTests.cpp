// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutTerrainPathEvidence.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutFrozenTerrainPathSample MakeClassifiedPathSample(const int32 X, const int32 Y, const int32 SurfaceZ)
	{
		FLayoutFrozenTerrainPathSample Sample;
		Sample.BlockXY = FIntPoint(X, Y);
		Sample.SurfaceZ = SurfaceZ;
		Sample.bHasClassificationEvidence = true;
		return Sample;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPathEvidenceAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.TerrainPathEvidence.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPathEvidenceAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainPathEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Classified terrain-path artifact builds"),
		LayoutTerrainPathEvidence::TryBuildProducedArtifact(
			TEXT("TerrainPath.Test"),
			{MakeClassifiedPathSample(0, 0, 8), MakeClassifiedPathSample(16, 0, 9)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Classified terrain-path artifact applies"),
		LayoutTerrainPathEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Terrain path evidence flag set"), TerrainArtifact.bHasTerrainPathEvidence);
	TestEqual(TEXT("Path samples copied exactly"), TerrainArtifact.TerrainPathSamples.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPathEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.TerrainPathEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPathEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasTerrainPathEvidence = true;
	FrozenArtifact.TerrainPathSamples = { MakeClassifiedPathSample(0, 0, 8), MakeClassifiedPathSample(16, 0, 9) };

	FLayoutProducedTerrainPathEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen terrain/biome explicit path samples build terrain-path artifact"),
		LayoutTerrainPathEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("TerrainPath.FrozenTerrain"),
			FrozenArtifact,
			TEXT("FrozenTerrainProducer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Produced path artifact present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Produced path sample count copied"), Artifact.TerrainPathSamples.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPathEvidenceRejectsFrozenTerrainBiomeWithoutPathFlagTest,
	"PorismExtension.Layout.Terrain.TerrainPathEvidence.RejectsFrozenTerrainBiomeWithoutPathFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPathEvidenceRejectsFrozenTerrainBiomeWithoutPathFlagTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.TerrainPathSamples = { MakeClassifiedPathSample(0, 0, 8) };

	FLayoutProducedTerrainPathEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Frozen terrain/biome path samples without explicit evidence flag fail closed"),
		LayoutTerrainPathEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("TerrainPath.FrozenTerrain"),
			FrozenArtifact,
			TEXT("FrozenTerrainProducer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing explicit path flag failure reported"), FailureReason.Contains(TEXT("explicit path evidence")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPathEvidenceRejectsMissingRequiredArtifactTest,
	"PorismExtension.Layout.Terrain.TerrainPathEvidence.RejectsMissingRequiredArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPathEvidenceRejectsMissingRequiredArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required terrain-path artifact fails closed"),
		LayoutTerrainPathEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing path artifact leaves path flag false"), TerrainArtifact.bHasTerrainPathEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPathEvidenceRejectsUnclassifiedOrDuplicateSamplesTest,
	"PorismExtension.Layout.Terrain.TerrainPathEvidence.RejectsUnclassifiedOrDuplicateSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPathEvidenceRejectsUnclassifiedOrDuplicateSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainPathEvidenceArtifact Artifact;
	FString FailureReason;
	FLayoutFrozenTerrainPathSample UnclassifiedSample = MakeClassifiedPathSample(0, 0, 8);
	UnclassifiedSample.bHasClassificationEvidence = false;
	TestFalse(
		TEXT("Unclassified path sample fails closed"),
		LayoutTerrainPathEvidence::TryBuildProducedArtifact(
			TEXT("TerrainPath.Test"),
			{UnclassifiedSample},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	TestFalse(
		TEXT("Duplicate path columns fail closed"),
		LayoutTerrainPathEvidence::TryBuildProducedArtifact(
			TEXT("TerrainPath.Test"),
			{MakeClassifiedPathSample(0, 0, 8), MakeClassifiedPathSample(0, 0, 9)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
