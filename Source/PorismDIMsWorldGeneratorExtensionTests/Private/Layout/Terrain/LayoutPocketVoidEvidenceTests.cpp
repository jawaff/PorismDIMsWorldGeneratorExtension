// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutPocketVoidEvidence.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutFrozenTerrainVoidIntervalSample MakeVoidInterval(const int32 X, const int32 Y, const int32 MinZ, const int32 MaxZ)
	{
		FLayoutFrozenTerrainVoidIntervalSample Interval;
		Interval.BlockXY = FIntPoint(X, Y);
		Interval.MinZ = MinZ;
		Interval.MaxZ = MaxZ;
		Interval.bHasVoidEvidence = true;
		return Interval;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPocketVoidEvidenceAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.PocketVoidEvidence.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPocketVoidEvidenceAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedPocketVoidEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Pocket/void artifact builds from proven intervals"),
		LayoutPocketVoidEvidence::TryBuildProducedArtifact(
			TEXT("PocketVoid.Test"),
			{MakeVoidInterval(0, 0, -16, 8), MakeVoidInterval(16, 0, -12, 6)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Pocket/void artifact applies"),
		LayoutPocketVoidEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Pocket/void evidence flag set"), TerrainArtifact.bHasPocketVoidIntervalEvidence);
	TestEqual(TEXT("Pocket/void intervals copied exactly"), TerrainArtifact.PocketVoidIntervals.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPocketVoidEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.PocketVoidEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPocketVoidEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasPocketVoidIntervalEvidence = true;
	FrozenArtifact.PocketVoidIntervals = { MakeVoidInterval(0, 0, -16, 8), MakeVoidInterval(16, 0, -12, 6) };

	FLayoutProducedPocketVoidEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen terrain/biome explicit pocket/void intervals build artifact"),
		LayoutPocketVoidEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("PocketVoid.FrozenTerrain"),
			FrozenArtifact,
			TEXT("FrozenTerrainProducer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Produced pocket/void artifact present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Produced interval count copied"), Artifact.PocketVoidIntervals.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPocketVoidEvidenceRejectsFrozenTerrainBiomeWithoutEvidenceFlagTest,
	"PorismExtension.Layout.Terrain.PocketVoidEvidence.RejectsFrozenTerrainBiomeWithoutEvidenceFlag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPocketVoidEvidenceRejectsFrozenTerrainBiomeWithoutEvidenceFlagTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.PocketVoidIntervals = { MakeVoidInterval(0, 0, -16, 8) };

	FLayoutProducedPocketVoidEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Frozen terrain/biome intervals without explicit evidence flag fail closed"),
		LayoutPocketVoidEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("PocketVoid.FrozenTerrain"),
			FrozenArtifact,
			TEXT("FrozenTerrainProducer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing explicit pocket/void flag failure reported"), FailureReason.Contains(TEXT("explicit interval evidence")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPocketVoidEvidenceRejectsMissingRequiredArtifactTest,
	"PorismExtension.Layout.Terrain.PocketVoidEvidence.RejectsMissingRequiredArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPocketVoidEvidenceRejectsMissingRequiredArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required pocket/void artifact fails closed"),
		LayoutPocketVoidEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing pocket/void artifact leaves flag false"), TerrainArtifact.bHasPocketVoidIntervalEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPocketVoidEvidenceRejectsInvalidIntervalsTest,
	"PorismExtension.Layout.Terrain.PocketVoidEvidence.RejectsInvalidIntervals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPocketVoidEvidenceRejectsInvalidIntervalsTest::RunTest(const FString& Parameters)
{
	FLayoutProducedPocketVoidEvidenceArtifact Artifact;
	FString FailureReason;
	FLayoutFrozenTerrainVoidIntervalSample InvalidInterval = MakeVoidInterval(0, 0, 8, -8);
	TestFalse(
		TEXT("Invalid interval range fails closed"),
		LayoutPocketVoidEvidence::TryBuildProducedArtifact(
			TEXT("PocketVoid.Test"),
			{InvalidInterval},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	TestFalse(
		TEXT("Duplicate interval columns fail closed"),
		LayoutPocketVoidEvidence::TryBuildProducedArtifact(
			TEXT("PocketVoid.Test"),
			{MakeVoidInterval(0, 0, -16, 8), MakeVoidInterval(0, 0, -12, 6)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
