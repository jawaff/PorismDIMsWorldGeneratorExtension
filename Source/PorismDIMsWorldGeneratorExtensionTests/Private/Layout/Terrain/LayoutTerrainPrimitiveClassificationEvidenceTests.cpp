// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutTerrainPrimitiveClassificationEvidence.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutTerrainPrimitiveClassificationCellEvidence MakePrimitiveClassificationCell(
		const FIntVector LocalCell,
		const ELayoutTerrainPrimitiveClassification Classification,
		const FLayoutId ProvenanceId)
	{
		FLayoutTerrainPrimitiveClassificationCellEvidence Cell;
		Cell.LocalCell = LocalCell;
		Cell.Classification = Classification;
		Cell.ProvenanceId = ProvenanceId;
		return Cell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPrimitiveClassificationEvidenceAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.PrimitiveClassificationEvidence.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPrimitiveClassificationEvidenceAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainPrimitiveClassificationArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Primitive classification artifact builds from explicit cells"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
			TEXT("PrimitiveClassification.Test"),
			{
				MakePrimitiveClassificationCell(FIntVector(1, 0, 0), ELayoutTerrainPrimitiveClassification::Usable, TEXT("Class.Usable")),
				MakePrimitiveClassificationCell(FIntVector(2, 0, 0), ELayoutTerrainPrimitiveClassification::Supportable, TEXT("Class.Supportable")),
				MakePrimitiveClassificationCell(FIntVector(3, 0, 0), ELayoutTerrainPrimitiveClassification::Contractible, TEXT("Class.Contractible")),
				MakePrimitiveClassificationCell(FIntVector(4, 0, 0), ELayoutTerrainPrimitiveClassification::Excavatable, TEXT("Class.Excavatable")),
				MakePrimitiveClassificationCell(FIntVector(5, 0, 0), ELayoutTerrainPrimitiveClassification::InvalidRequired, TEXT("Class.InvalidRequired"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Explicit primitive classification artifact applies"),
		LayoutTerrainPrimitiveClassificationEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Primitive classification evidence flag set"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	TestEqual(TEXT("Audit entry per primitive classification cell"), TerrainArtifact.AuditMessages.Num(), 5);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPrimitiveClassificationEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.PrimitiveClassificationEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPrimitiveClassificationEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasTerrainPrimitiveClassificationEvidence = true;
	FrozenArtifact.TerrainPrimitiveClassificationCells.Add(MakePrimitiveClassificationCell(FIntVector(1, 0, 0), ELayoutTerrainPrimitiveClassification::Supportable, TEXT("Class.Supportable")));

	FLayoutProducedTerrainPrimitiveClassificationArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Primitive classification artifact builds only from flagged frozen explicit cells"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("PrimitiveClassification.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	TestEqual(TEXT("Frozen primitive classification cell copied"), Artifact.Cells.Num(), 1);

	FrozenArtifact.bHasTerrainPrimitiveClassificationEvidence = false;
	TestFalse(
		TEXT("Unflagged frozen primitive classification cells are rejected"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("PrimitiveClassification.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPrimitiveClassificationEvidenceMissingRequiredArtifactFailsClosedTest,
	"PorismExtension.Layout.Terrain.PrimitiveClassificationEvidence.MissingRequiredArtifactFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPrimitiveClassificationEvidenceMissingRequiredArtifactFailsClosedTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required primitive classification artifact fails closed"),
		LayoutTerrainPrimitiveClassificationEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing required primitive classification artifact leaves flag false"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPrimitiveClassificationEvidenceOptionalMissingArtifactStaysEmptyTest,
	"PorismExtension.Layout.Terrain.PrimitiveClassificationEvidence.OptionalMissingArtifactStaysEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPrimitiveClassificationEvidenceOptionalMissingArtifactStaysEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Optional missing primitive classification artifact succeeds"),
		LayoutTerrainPrimitiveClassificationEvidence::TryApplyProducedArtifact(
			nullptr,
			false,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Optional missing primitive classification artifact does not infer evidence"), TerrainArtifact.bHasTerrainPrimitiveClassificationEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPrimitiveClassificationEvidenceInvalidArtifactsFailClosedTest,
	"PorismExtension.Layout.Terrain.PrimitiveClassificationEvidence.InvalidArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPrimitiveClassificationEvidenceInvalidArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutProducedTerrainPrimitiveClassificationArtifact Artifact;
	TestFalse(
		TEXT("Missing artifact id fails closed"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
			NAME_None,
			{MakePrimitiveClassificationCell(FIntVector(1, 0, 0), ELayoutTerrainPrimitiveClassification::Usable, TEXT("Class.Usable"))},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Empty primitive classification cells fail closed"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
			TEXT("PrimitiveClassification.Test"),
			{},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Duplicate primitive classification cells fail closed"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
			TEXT("PrimitiveClassification.Test"),
			{
				MakePrimitiveClassificationCell(FIntVector(1, 0, 0), ELayoutTerrainPrimitiveClassification::Usable, TEXT("Class.Usable")),
				MakePrimitiveClassificationCell(FIntVector(1, 0, 0), ELayoutTerrainPrimitiveClassification::Supportable, TEXT("Class.Supportable"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Missing cell provenance fails closed"),
		LayoutTerrainPrimitiveClassificationEvidence::TryBuildProducedArtifact(
			TEXT("PrimitiveClassification.Test"),
			{MakePrimitiveClassificationCell(FIntVector(1, 0, 0), ELayoutTerrainPrimitiveClassification::Usable, NAME_None)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
