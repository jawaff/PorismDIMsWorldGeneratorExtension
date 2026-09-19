// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutFlatClearanceEvidence.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutFlatClearanceCellEvidence MakeFlatClearanceCell(
		const FIntVector LocalCell,
		const int32 RequiredClearanceBlocks,
		const int32 ObservedClearanceBlocks,
		const FLayoutId ProvenanceId)
	{
		FLayoutFlatClearanceCellEvidence Cell;
		Cell.LocalCell = LocalCell;
		LayoutLocalBlockCoordinates::TryMakeCoord8(RequiredClearanceBlocks, Cell.RequiredClearanceBlocks);
		LayoutLocalBlockCoordinates::TryMakeCoord8(ObservedClearanceBlocks, Cell.ObservedClearanceBlocks);
		Cell.ProvenanceId = ProvenanceId;
		return Cell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatClearanceEvidenceAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.FlatClearanceEvidence.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFlatClearanceEvidenceAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedFlatClearanceEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Flat-clearance artifact builds from explicit cells"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			TEXT("FlatClearance.Test"),
			{
				MakeFlatClearanceCell(FIntVector(1, 2, 0), 3, 3, TEXT("Flat.Sample.A")),
				MakeFlatClearanceCell(FIntVector(3, 4, 0), 4, 6, TEXT("Flat.Sample.B"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Explicit flat-clearance artifact applies"),
		LayoutFlatClearanceEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Flat-clearance evidence flag set"), TerrainArtifact.bHasFlatClearanceEvidence);
	TestEqual(TEXT("Audit entry per flat-clearance cell"), TerrainArtifact.AuditMessages.Num(), 2);
	TestEqual(TEXT("Required clearance compact value preserved"), TerrainArtifact.FlatClearanceCells[0].RequiredClearanceBlocks.Value, static_cast<uint8>(3));
	TestEqual(TEXT("Observed clearance compact value preserved"), TerrainArtifact.FlatClearanceCells[1].ObservedClearanceBlocks.Value, static_cast<uint8>(6));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatClearanceEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.FlatClearanceEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFlatClearanceEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasFlatClearanceEvidence = true;
	FrozenArtifact.FlatClearanceCells.Add(MakeFlatClearanceCell(FIntVector(1, 2, 0), 3, 4, TEXT("Flat.Sample")));

	FLayoutProducedFlatClearanceEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Flat-clearance artifact builds only from flagged frozen explicit cells"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("FlatClearance.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	TestEqual(TEXT("Frozen flat-clearance cell copied"), Artifact.Cells.Num(), 1);

	FrozenArtifact.bHasFlatClearanceEvidence = false;
	TestFalse(
		TEXT("Unflagged frozen flat-clearance cells are rejected"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("FlatClearance.Frozen"),
			FrozenArtifact,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatClearanceEvidenceMissingRequiredArtifactFailsClosedTest,
	"PorismExtension.Layout.Terrain.FlatClearanceEvidence.MissingRequiredArtifactFailsClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFlatClearanceEvidenceMissingRequiredArtifactFailsClosedTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing required flat-clearance artifact fails closed"),
		LayoutFlatClearanceEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing required flat-clearance artifact leaves flag false"), TerrainArtifact.bHasFlatClearanceEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatClearanceEvidenceOptionalMissingArtifactStaysEmptyTest,
	"PorismExtension.Layout.Terrain.FlatClearanceEvidence.OptionalMissingArtifactStaysEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFlatClearanceEvidenceOptionalMissingArtifactStaysEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Optional missing flat-clearance artifact succeeds"),
		LayoutFlatClearanceEvidence::TryApplyProducedArtifact(
			nullptr,
			false,
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Optional missing flat-clearance artifact does not infer evidence"), TerrainArtifact.bHasFlatClearanceEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatClearanceEvidenceInvalidArtifactsFailClosedTest,
	"PorismExtension.Layout.Terrain.FlatClearanceEvidence.InvalidArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFlatClearanceEvidenceInvalidArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutProducedFlatClearanceEvidenceArtifact Artifact;
	TestFalse(
		TEXT("Missing artifact id fails closed"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			NAME_None,
			{MakeFlatClearanceCell(FIntVector(1, 2, 0), 3, 3, TEXT("Flat.Sample"))},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Empty flat-clearance cells fail closed"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			TEXT("FlatClearance.Test"),
			{},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Insufficient observed clearance fails closed"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			TEXT("FlatClearance.Test"),
			{MakeFlatClearanceCell(FIntVector(1, 2, 0), 4, 3, TEXT("Flat.Sample"))},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Duplicate flat-clearance cells fail closed"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			TEXT("FlatClearance.Test"),
			{
				MakeFlatClearanceCell(FIntVector(1, 2, 0), 3, 3, TEXT("Flat.Sample.A")),
				MakeFlatClearanceCell(FIntVector(1, 2, 0), 3, 4, TEXT("Flat.Sample.B"))
			},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	TestFalse(
		TEXT("Missing cell provenance fails closed"),
		LayoutFlatClearanceEvidence::TryBuildProducedArtifact(
			TEXT("FlatClearance.Test"),
			{MakeFlatClearanceCell(FIntVector(1, 2, 0), 3, 3, NAME_None)},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
