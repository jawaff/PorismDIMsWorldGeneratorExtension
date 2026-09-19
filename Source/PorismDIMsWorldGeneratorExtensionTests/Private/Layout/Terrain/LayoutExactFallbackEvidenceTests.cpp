// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutExactFallbackEvidence.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutExactFallbackCellEvidence MakeExactFallbackCell(
		const FIntVector LocalCell,
		const ELayoutExactFallbackEvidenceKind EvidenceKind,
		const FLayoutId ProvenanceId)
	{
		FLayoutExactFallbackCellEvidence Cell;
		Cell.LocalCell = LocalCell;
		Cell.EvidenceKind = EvidenceKind;
		Cell.ProvenanceId = ProvenanceId;
		return Cell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutExactFallbackEvidenceAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.ExactFallbackEvidence.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutExactFallbackEvidenceAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedExactFallbackEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Exact fallback artifact builds from explicit cells"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifact(
			TEXT("ExactFallback.Test"),
			{
				MakeExactFallbackCell(FIntVector(1, 2, 0), ELayoutExactFallbackEvidenceKind::ThresholdBand, TEXT("Threshold.Sample")),
				MakeExactFallbackCell(FIntVector(3, 4, 0), ELayoutExactFallbackEvidenceKind::PinnedCell, TEXT("Pinned.Sample")),
				MakeExactFallbackCell(FIntVector(5, 6, 0), ELayoutExactFallbackEvidenceKind::NonHeightfieldCell, TEXT("NonHeightfield.Sample"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Explicit exact fallback artifact applies"),
		LayoutExactFallbackEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Exact fallback evidence flag set"), TerrainArtifact.bHasExactFallbackEvidence);
	TestEqual(TEXT("Audit entry per exact fallback cell"), TerrainArtifact.AuditMessages.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutExactFallbackEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.ExactFallbackEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutExactFallbackEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasExactFallbackEvidence = true;
	FrozenArtifact.ExactFallbackCells.Add(MakeExactFallbackCell(FIntVector(1, 2, 0), ELayoutExactFallbackEvidenceKind::ThresholdBand, TEXT("Threshold.Sample")));

	FLayoutProducedExactFallbackEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Exact fallback artifact builds only from flagged frozen explicit cells"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("ExactFallback.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	TestEqual(TEXT("Frozen exact fallback cell copied"), Artifact.Cells.Num(), 1);

	FrozenArtifact.bHasExactFallbackEvidence = false;
	TestFalse(
		TEXT("Unflagged frozen exact fallback cells are rejected"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("ExactFallback.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutExactFallbackEvidenceMissingRequiredArtifactFailsClosedTest,
	"PorismExtension.Layout.Terrain.ExactFallbackEvidence.MissingRequiredArtifactFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutExactFallbackEvidenceMissingRequiredArtifactFailsClosedTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required exact fallback artifact fails closed"),
		LayoutExactFallbackEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing required exact fallback artifact leaves flag false"), TerrainArtifact.bHasExactFallbackEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutExactFallbackEvidenceOptionalMissingArtifactStaysEmptyTest,
	"PorismExtension.Layout.Terrain.ExactFallbackEvidence.OptionalMissingArtifactStaysEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutExactFallbackEvidenceOptionalMissingArtifactStaysEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Optional missing exact fallback artifact succeeds"),
		LayoutExactFallbackEvidence::TryApplyProducedArtifact(
			nullptr,
			false,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Optional missing exact fallback artifact does not infer evidence"), TerrainArtifact.bHasExactFallbackEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutExactFallbackEvidenceInvalidArtifactsFailClosedTest,
	"PorismExtension.Layout.Terrain.ExactFallbackEvidence.InvalidArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutExactFallbackEvidenceInvalidArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutProducedExactFallbackEvidenceArtifact Artifact;
	TestFalse(
		TEXT("Missing artifact id fails closed"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifact(
			NAME_None,
			{MakeExactFallbackCell(FIntVector(1, 2, 0), ELayoutExactFallbackEvidenceKind::ThresholdBand, TEXT("Threshold.Sample"))},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Empty exact fallback cells fail closed"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifact(
			TEXT("ExactFallback.Test"),
			{},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Duplicate exact fallback cells fail closed"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifact(
			TEXT("ExactFallback.Test"),
			{
				MakeExactFallbackCell(FIntVector(1, 2, 0), ELayoutExactFallbackEvidenceKind::ThresholdBand, TEXT("Threshold.Sample")),
				MakeExactFallbackCell(FIntVector(1, 2, 0), ELayoutExactFallbackEvidenceKind::PinnedCell, TEXT("Pinned.Sample"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Missing cell provenance fails closed"),
		LayoutExactFallbackEvidence::TryBuildProducedArtifact(
			TEXT("ExactFallback.Test"),
			{MakeExactFallbackCell(FIntVector(1, 2, 0), ELayoutExactFallbackEvidenceKind::ThresholdBand, NAME_None)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
