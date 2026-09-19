// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutShiftedFootprintProof.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutFrozenShiftedFootprintEvidence MakeCompleteShiftedEvidence()
	{
		FLayoutFrozenShiftedFootprintEvidence Evidence;
		Evidence.PlacementShiftId = TEXT("Shift.East.1");
		Evidence.ShiftCells = FIntVector(1, 0, 0);
		Evidence.FootprintMinBlockWorldPos = FIntVector(16, 0, 0);
		Evidence.SearchMinBlockXY = FIntPoint(16, 0);
		Evidence.SearchMaxBlockXY = FIntPoint(31, 15);

		FLayoutSteppedTerrainSupportSample& SupportSample = Evidence.SteppedSupportSamples.AddDefaulted_GetRef();
		SupportSample.LocalCell = FIntVector::ZeroValue;
		SupportSample.SupportSurfaceZ = 8;

		FLayoutFrozenBiomeOwnershipSample& BiomeSample = Evidence.BiomeOwnershipSamples.AddDefaulted_GetRef();
		BiomeSample.BlockXY = FIntPoint(16, 0);
		BiomeSample.OwningBiomeRowName = TEXT("Biome.Forest");
		BiomeSample.bOwnedByAllowList = true;
		BiomeSample.bHasSurfaceEvidence = true;

		Evidence.bHasTerrainFitProof = true;
		Evidence.bHasReservationCollisionProof = true;
		Evidence.bHasChunkOverlapProvenance = true;
		return Evidence;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutShiftedFootprintProofAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.ShiftedFootprintProof.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutShiftedFootprintProofAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedShiftedFootprintProofArtifact Artifact;
	FString FailureReason;
	const FLayoutFrozenShiftedFootprintEvidence Evidence = MakeCompleteShiftedEvidence();
	TestTrue(
		TEXT("Complete shifted proof artifact builds"),
		LayoutShiftedFootprintProof::TryBuildProducedArtifact(
			TEXT("ShiftProof.Test"),
			Evidence,
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	TestTrue(
		TEXT("Complete shifted proof artifact applies"),
		LayoutShiftedFootprintProof::TryApplyProducedArtifact(
			&Artifact,
			Evidence.PlacementShiftId,
			Evidence.ShiftCells,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Shifted evidence flag set"), TerrainArtifact.bHasShiftedFootprintEvidence);
	TestEqual(TEXT("One shifted evidence bundle copied"), TerrainArtifact.ShiftedFootprintEvidence.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutShiftedFootprintProofBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.ShiftedFootprintProof.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutShiftedFootprintProofBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	const FLayoutFrozenShiftedFootprintEvidence Evidence = MakeCompleteShiftedEvidence();
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasShiftedFootprintEvidence = true;
	FrozenArtifact.ShiftedFootprintEvidence.Add(Evidence);

	FLayoutProducedShiftedFootprintProofArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen terrain/biome explicit shifted evidence builds proof artifact"),
		LayoutShiftedFootprintProof::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("ShiftProof.FrozenTerrain"),
			Evidence.PlacementShiftId,
			Evidence.ShiftCells,
			FrozenArtifact,
			TEXT("FrozenTerrainProducer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Produced shifted proof artifact present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Produced shifted proof id preserved"), Artifact.Evidence.PlacementShiftId, Evidence.PlacementShiftId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutShiftedFootprintProofRejectsFrozenTerrainBiomeWithoutMatchingEvidenceTest,
	"PorismExtension.Layout.Terrain.ShiftedFootprintProof.RejectsFrozenTerrainBiomeWithoutMatchingEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutShiftedFootprintProofRejectsFrozenTerrainBiomeWithoutMatchingEvidenceTest::RunTest(const FString& Parameters)
{
	const FLayoutFrozenShiftedFootprintEvidence Evidence = MakeCompleteShiftedEvidence();
	FLayoutFrozenTerrainBiomeAdapterInput FrozenArtifact;
	FrozenArtifact.bHasShiftedFootprintEvidence = true;
	FrozenArtifact.ShiftedFootprintEvidence.Add(Evidence);

	FLayoutProducedShiftedFootprintProofArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Frozen terrain/biome artifact without matching shifted evidence fails closed"),
		LayoutShiftedFootprintProof::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("ShiftProof.FrozenTerrain"),
			TEXT("Shift.North.1"),
			FIntVector(0, 1, 0),
			FrozenArtifact,
			TEXT("FrozenTerrainProducer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing matching shifted evidence failure reported"), FailureReason.Contains(TEXT("matching shifted evidence")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutShiftedFootprintProofRejectsMissingArtifactTest,
	"PorismExtension.Layout.Terrain.ShiftedFootprintProof.RejectsMissingArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutShiftedFootprintProofRejectsMissingArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Missing proof rejects non-zero shift"),
		LayoutShiftedFootprintProof::TryApplyProducedArtifact(
			nullptr,
			TEXT("Shift.East.1"),
			FIntVector(1, 0, 0),
			TerrainArtifact,
			FailureReason));
	TestFalse(TEXT("Missing proof leaves shifted flag false"), TerrainArtifact.bHasShiftedFootprintEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutShiftedFootprintProofRejectsIncompleteArtifactTest,
	"PorismExtension.Layout.Terrain.ShiftedFootprintProof.RejectsIncompleteArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutShiftedFootprintProofRejectsIncompleteArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedShiftedFootprintProofArtifact Artifact;
	FLayoutFrozenShiftedFootprintEvidence Evidence = MakeCompleteShiftedEvidence();
	Evidence.bHasReservationCollisionProof = false;
	FString FailureReason;
	TestFalse(
		TEXT("Incomplete shifted proof artifact fails closed"),
		LayoutShiftedFootprintProof::TryBuildProducedArtifact(
			TEXT("ShiftProof.Test"),
			Evidence,
			TEXT("TestProducer"),
			Artifact,
			FailureReason));
	return true;
}
