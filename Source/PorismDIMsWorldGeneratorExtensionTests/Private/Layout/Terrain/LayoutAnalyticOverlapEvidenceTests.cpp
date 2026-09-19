// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutAnalyticOverlapEvidence.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutAnalyticOverlapCellEvidence MakeAnalyticOverlapCell(
		const FIntVector LocalCell,
		const int32 OverlapArea,
		const FLayoutId ProvenanceId)
	{
		FLayoutAnalyticOverlapCellEvidence Cell;
		Cell.LocalCell = LocalCell;
		Cell.OverlapArea = OverlapArea;
		Cell.ProvenanceId = ProvenanceId;
		return Cell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutAnalyticOverlapEvidenceAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.AnalyticOverlapEvidence.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutAnalyticOverlapEvidenceAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedAnalyticOverlapEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Analytic overlap artifact builds from explicit cells"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			TEXT("AnalyticOverlap.Test"),
			{
				MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 16, TEXT("Overlap.Sample.A")),
				MakeAnalyticOverlapCell(FIntVector(3, 4, 0), 4, TEXT("Overlap.Sample.B"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Explicit analytic overlap artifact applies"),
		LayoutAnalyticOverlapEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Analytic overlap evidence flag set"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	TestEqual(TEXT("Audit entry per analytic overlap cell"), TerrainArtifact.AuditMessages.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutAnalyticOverlapEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.AnalyticOverlapEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutAnalyticOverlapEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasAnalyticOverlapEvidence = true;
	FrozenArtifact.AnalyticOverlapCells.Add(MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 16, TEXT("Overlap.Sample")));

	FLayoutProducedAnalyticOverlapEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Analytic-overlap artifact builds only from flagged frozen explicit cells"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("AnalyticOverlap.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	TestEqual(TEXT("Frozen analytic-overlap cell copied"), Artifact.Cells.Num(), 1);

	FrozenArtifact.bHasAnalyticOverlapEvidence = false;
	TestFalse(
		TEXT("Unflagged frozen analytic-overlap cells are rejected"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("AnalyticOverlap.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutAnalyticOverlapEvidenceMissingRequiredArtifactFailsClosedTest,
	"PorismExtension.Layout.Terrain.AnalyticOverlapEvidence.MissingRequiredArtifactFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutAnalyticOverlapEvidenceMissingRequiredArtifactFailsClosedTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required analytic overlap artifact fails closed"),
		LayoutAnalyticOverlapEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing required analytic overlap artifact leaves flag false"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutAnalyticOverlapEvidenceOptionalMissingArtifactStaysEmptyTest,
	"PorismExtension.Layout.Terrain.AnalyticOverlapEvidence.OptionalMissingArtifactStaysEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutAnalyticOverlapEvidenceOptionalMissingArtifactStaysEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Optional missing analytic overlap artifact succeeds"),
		LayoutAnalyticOverlapEvidence::TryApplyProducedArtifact(
			nullptr,
			false,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Optional missing analytic overlap artifact does not infer evidence"), TerrainArtifact.bHasAnalyticOverlapEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutAnalyticOverlapEvidenceInvalidArtifactsFailClosedTest,
	"PorismExtension.Layout.Terrain.AnalyticOverlapEvidence.InvalidArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutAnalyticOverlapEvidenceInvalidArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutProducedAnalyticOverlapEvidenceArtifact Artifact;
	TestFalse(
		TEXT("Missing artifact id fails closed"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			NAME_None,
			{MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 16, TEXT("Overlap.Sample"))},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Empty analytic overlap cells fail closed"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			TEXT("AnalyticOverlap.Test"),
			{},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Non-positive overlap area fails closed"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			TEXT("AnalyticOverlap.Test"),
			{MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 0, TEXT("Overlap.Sample"))},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Duplicate analytic overlap cells fail closed"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			TEXT("AnalyticOverlap.Test"),
			{
				MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 16, TEXT("Overlap.Sample.A")),
				MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 4, TEXT("Overlap.Sample.B"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Missing cell provenance fails closed"),
		LayoutAnalyticOverlapEvidence::TryBuildProducedArtifact(
			TEXT("AnalyticOverlap.Test"),
			{MakeAnalyticOverlapCell(FIntVector(1, 2, 0), 16, NAME_None)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
