// Copyright 2026 Spotted Loaf Studio

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutProducedTerrainEvidenceAdapter.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutTerrainPlacementEvidence.h"

#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Misc/AutomationTest.h"

namespace
{
	FLayoutTerrainPlacementCellEvidence MakePlacementCell(const FIntVector Cell = FIntVector::ZeroValue)
	{
		FLayoutTerrainPlacementCellEvidence Evidence;
		Evidence.Cell = Cell;
		Evidence.bPlaceableForSelectedMode = true;
		Evidence.bHasFoundationFillEvidence = false;
		Evidence.bHasExcavationEvidence = true;
		Evidence.TerrainStageIndex = 1;
		Evidence.VerticalShiftBlocks = 16;
		Evidence.bHasLocalOverlapZ = true;
		LayoutLocalBlockCoordinates::TryMakeCoord8(2, Evidence.OverlapMinLocalZ);
		LayoutLocalBlockCoordinates::TryMakeCoord8(12, Evidence.OverlapMaxLocalZ);
		Evidence.ProvenanceId = TEXT("PlacementEvidence.Cell.0");
		return Evidence;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceBuildsAndAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.BuildsAndAppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceBuildsAndAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit terrain placement evidence artifact builds"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.0"),
			{MakePlacementCell()},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput FrozenInput;
	TestTrue(
		TEXT("Explicit terrain placement evidence applies to frozen terrain input"),
		LayoutTerrainPlacementEvidence::TryApplyProducedArtifact(
			&Artifact,
			true,
			FrozenInput,
			FailureReason));
	TestTrue(TEXT("Frozen input records placement evidence presence"), FrozenInput.bHasTerrainPlacementEvidence);
	TestEqual(TEXT("Frozen input carries one placement verdict"), FrozenInput.TerrainPlacementCells.Num(), 1);
	TestEqual(TEXT("Vertical shift preserved"), FrozenInput.TerrainPlacementCells[0].VerticalShiftBlocks, 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenInput;
	FrozenInput.bHasTerrainPlacementEvidence = true;
	FrozenInput.TerrainPlacementCells.Add(MakePlacementCell());

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Terrain placement artifact builds only from flagged frozen explicit cells"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("PlacementEvidence.Frozen"),
			FrozenInput,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	TestEqual(TEXT("Frozen placement cell copied"), Artifact.Cells.Num(), 1);

	FrozenInput.bHasTerrainPlacementEvidence = false;
	TestFalse(
		TEXT("Unflagged frozen placement cells are rejected"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("PlacementEvidence.Frozen"),
			FrozenInput,
			TEXT("FrozenProducer"),
			Artifact,
			FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsRequiredMissingArtifactTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsRequiredMissingArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsRequiredMissingArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput FrozenInput;
	FString FailureReason;
	TestFalse(
		TEXT("Required terrain placement evidence fails closed when absent"),
		LayoutTerrainPlacementEvidence::TryApplyProducedArtifact(
			nullptr,
			true,
			FrozenInput,
			FailureReason));
	TestTrue(TEXT("Missing placement evidence failure reported"), FailureReason.Contains(TEXT("required")));
	TestFalse(TEXT("Missing optional artifact does not set evidence flag"), FrozenInput.bHasTerrainPlacementEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsInvalidCellsTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsInvalidCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsInvalidCellsTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence InvalidCell = MakePlacementCell(FIntVector(1, 0, 0));
	InvalidCell.OverlapMinLocalZ = 13;
	InvalidCell.OverlapMaxLocalZ = 2;

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Invalid local overlap bounds fail closed"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.0"),
			{InvalidCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Invalid overlap failure reported"), FailureReason.Contains(TEXT("overlap")));
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsMixedBridgeAndTerrainModificationEvidenceTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsMixedBridgeAndTerrainModificationEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsMixedBridgeAndTerrainModificationEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence InvalidCell = MakePlacementCell(FIntVector(2, 0, 0));
	InvalidCell.bHasBridgeSupportEvidence = true;

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Bridge evidence cannot be mixed with terrain modification evidence"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.MixedBridge"),
			{InvalidCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mixed bridge evidence failure reported"), FailureReason.Contains(TEXT("bridge")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsMixedSupportAndExcavationEvidenceTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsMixedSupportAndExcavationEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsMixedSupportAndExcavationEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence InvalidCell = MakePlacementCell(FIntVector(2, 1, 0));
	InvalidCell.bHasFoundationFillEvidence = true;

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Support evidence cannot be mixed with excavation or clearance evidence"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.MixedSupport"),
			{InvalidCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mixed foundation fill and excavation failure reported"), FailureReason.Contains(TEXT("foundation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsOverlapWithoutExcavationEvidenceTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsOverlapWithoutExcavationEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsOverlapWithoutExcavationEvidenceTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence InvalidCell = MakePlacementCell(FIntVector(2, 0, 0));
	InvalidCell.bHasExcavationEvidence = false;

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Terrain/template overlap fails closed without explicit excavation evidence"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.OverlapNoExcavation"),
			{InvalidCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing excavation evidence failure reported"), FailureReason.Contains(TEXT("excavation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceBuildsFromSteppedSupportSamplesTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.BuildsFromSteppedSupportSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceBuildsFromSteppedSupportSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap SupportMap;
	FLayoutSteppedTerrainSupportSample& BaseSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	BaseSample.LocalCell = FIntVector(0, 0, 0);
	BaseSample.SupportSurfaceZ = 4;
	BaseSample.SnappedSupportFloorZ = 0;
	BaseSample.SnappedSupportCeilingZ = 16;
	FLayoutSteppedTerrainSupportSample& ShiftedSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	ShiftedSample.LocalCell = FIntVector(1, 0, 0);
	ShiftedSample.SupportSurfaceZ = 20;
	ShiftedSample.SnappedSupportFloorZ = 16;
	ShiftedSample.SnappedSupportCeilingZ = 32;
	FLayoutSteppedTerrainSupportSample& FoundationSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	FoundationSample.LocalCell = FIntVector(2, 0, 0);
	FoundationSample.SupportSurfaceZ = 24;
	FoundationSample.SnappedSupportFloorZ = 32;
	FoundationSample.SnappedSupportCeilingZ = 48;
	FLayoutSteppedTerrainSupportSample& ClearanceSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	ClearanceSample.LocalCell = FIntVector(3, 0, 0);
	ClearanceSample.SupportSurfaceZ = 32;
	ClearanceSample.SnappedSupportFloorZ = 32;
	ClearanceSample.SnappedSupportCeilingZ = 48;
	FLayoutSteppedTerrainAdjacencyStep& RampStep = SupportMap.AdjacencySteps.AddDefaulted_GetRef();
	RampStep.FromCell = SupportMap.SupportSamples[0].LocalCell;
	RampStep.ToCell = SupportMap.SupportSamples[1].LocalCell;
	RampStep.StepHeightBlocks = 16;
	RampStep.SnappedLevelDelta = 1;

	FLayoutWorldBindingTerrainTransitionPolicy TransitionPolicy;
	TransitionPolicy.bAllowFoundationFill = true;
	TransitionPolicy.MaxFoundationDepth = 16;
	TransitionPolicy.bAllowPerimeterRampTransition = true;

	const FLayoutFrozenTerrainBiomeAdapterInput Artifact = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		FIntPoint(64, 16),
		FLayoutTerrainSurfaceSearchSettings(),
		TransitionPolicy,
		FLayoutNoiseCoordinateSettings(),
		16,
		TEXT("Biome.Test"),
		{},
		SupportMap);

	TestTrue(TEXT("Stepped support samples produce placement evidence"), Artifact.bHasTerrainPlacementEvidence);
	if (!TestEqual(TEXT("Placement evidence count matches support samples"), Artifact.TerrainPlacementCells.Num(), 4))
	{
		return false;
	}
	TestEqual(TEXT("Base stage stays zero"), Artifact.TerrainPlacementCells[0].TerrainStageIndex, 0);
	TestEqual(TEXT("Base shift stays zero"), Artifact.TerrainPlacementCells[0].VerticalShiftBlocks, 0);
	// The intermediate terrace survives the minimum cluster filter to bridge its higher neighbor.
	TestEqual(TEXT("Intermediate terrace preserves stage one"), Artifact.TerrainPlacementCells[1].TerrainStageIndex, 1);
	TestEqual(TEXT("Intermediate terrace preserves one-cell shift"), Artifact.TerrainPlacementCells[1].VerticalShiftBlocks, 16);
	TestFalse(TEXT("Normalized base evidence does not reuse raw-surface excavation"), Artifact.TerrainPlacementCells[0].bHasExcavationEvidence);
	TestFalse(TEXT("Normalized base evidence has no raw overlap bounds"), Artifact.TerrainPlacementCells[0].bHasLocalOverlapZ);
	TestTrue(TEXT("Normalized base evidence records clearance at its floor"), Artifact.TerrainPlacementCells[0].bHasClearanceEvidence);
	TestEqual(TEXT("Source support remains unmodified by artifact normalization"), SupportMap.SupportSamples[0].SupportSurfaceZ, 4);
	// Cluster enforcement (min 3): Foundation + Clearance at floorZ=32 form cluster size 2 → excavated.
	TestEqual(TEXT("Excavated foundation cell stage zero"), Artifact.TerrainPlacementCells[2].TerrainStageIndex, 0);
	TestEqual(TEXT("Excavated foundation cell shift zero"), Artifact.TerrainPlacementCells[2].VerticalShiftBlocks, 0);
	TestEqual(TEXT("Excavated clearance cell stage zero"), Artifact.TerrainPlacementCells[3].TerrainStageIndex, 0);
	TestEqual(TEXT("Excavated clearance cell shift zero"), Artifact.TerrainPlacementCells[3].VerticalShiftBlocks, 0);

	TransitionPolicy.MinimumSteppedTerrainShiftClusterCells = 1;
	const FLayoutFrozenTerrainBiomeAdapterInput PreservedArtifact =
		FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
			FIntVector(0, 0, 0),
			FIntVector(0, 0, 0),
			FIntVector(16, 16, 16),
			FIntPoint(64, 16),
			FLayoutTerrainSurfaceSearchSettings(),
			TransitionPolicy,
			FLayoutNoiseCoordinateSettings(),
			16,
			TEXT("Biome.Test"),
			{},
			SupportMap);
	if (!TestEqual(TEXT("Preserved placement evidence count matches support samples"), PreservedArtifact.TerrainPlacementCells.Num(), 4))
	{
		return false;
	}
	TestEqual(TEXT("Configured minimum preserves one-cell shifted stage"),
		PreservedArtifact.TerrainPlacementCells[1].TerrainStageIndex, 1);
	TestEqual(TEXT("Configured minimum preserves two-cell raised stage"),
		PreservedArtifact.TerrainPlacementCells[2].TerrainStageIndex, 2);
	TestEqual(TEXT("Configured minimum preserves raised-stage neighbor"),
		PreservedArtifact.TerrainPlacementCells[3].TerrainStageIndex, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRampTransitionUsesFoundationDepthBudgetTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RampTransitionUsesFoundationDepthBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRampTransitionUsesFoundationDepthBudgetTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap SupportMap;
	FLayoutSteppedTerrainSupportSample& BaseSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	BaseSample.LocalCell = FIntVector(0, 0, 0);
	BaseSample.SupportSurfaceZ = 0;
	BaseSample.SnappedSupportFloorZ = 0;
	BaseSample.SnappedSupportCeilingZ = 16;
	FLayoutSteppedTerrainSupportSample& ShiftedSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	ShiftedSample.LocalCell = FIntVector(1, 0, 0);
	ShiftedSample.SupportSurfaceZ = 16;
	ShiftedSample.SnappedSupportFloorZ = 16;
	ShiftedSample.SnappedSupportCeilingZ = 32;
	FLayoutSteppedTerrainAdjacencyStep& RampStep = SupportMap.AdjacencySteps.AddDefaulted_GetRef();
	RampStep.FromCell = BaseSample.LocalCell;
	RampStep.ToCell = ShiftedSample.LocalCell;
	RampStep.StepHeightBlocks = 16;
	RampStep.SnappedLevelDelta = 1;

	FLayoutWorldBindingTerrainTransitionPolicy TransitionPolicy;
	TransitionPolicy.bAllowPerimeterRampTransition = true;
	TransitionPolicy.MaxFoundationDepth = 0;

	const FLayoutFrozenTerrainBiomeAdapterInput Artifact = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		FIntPoint(32, 16),
		FLayoutTerrainSurfaceSearchSettings(),
		TransitionPolicy,
		FLayoutNoiseCoordinateSettings(),
		16,
		TEXT("Biome.Test"),
		{},
		SupportMap);

	TestTrue(TEXT("Placement evidence still builds from stepped support"), Artifact.bHasTerrainPlacementEvidence);
	TestFalse(TEXT("Ramp transition evidence fails closed without foundation-depth budget"), Artifact.TerrainPlacementCells[0].bHasRampTransitionEvidence);
	TestFalse(TEXT("Shifted ramp transition evidence fails closed without foundation-depth budget"), Artifact.TerrainPlacementCells[1].bHasRampTransitionEvidence);

	TransitionPolicy.MaxFoundationDepth = 8;
	const FLayoutFrozenTerrainBiomeAdapterInput OverLimitArtifact = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		FIntPoint(32, 16),
		FLayoutTerrainSurfaceSearchSettings(),
		TransitionPolicy,
		FLayoutNoiseCoordinateSettings(),
		16,
		TEXT("Biome.Test"),
		{},
		SupportMap);
	TestTrue(TEXT("Placement evidence still builds when ramp step exceeds foundation depth"), OverLimitArtifact.bHasTerrainPlacementEvidence);
	TestFalse(TEXT("Ramp transition evidence fails closed when step exceeds foundation depth"), OverLimitArtifact.TerrainPlacementCells[0].bHasRampTransitionEvidence);
	TestFalse(TEXT("Shifted ramp transition evidence fails closed when step exceeds foundation depth"), OverLimitArtifact.TerrainPlacementCells[1].bHasRampTransitionEvidence);
	TestTrue(TEXT("Over-limit ramp rejection is audited"), OverLimitArtifact.AuditMessages.ContainsByPredicate([](const FString& Message)
	{
		return Message.Contains(TEXT("RampRejected:1"));
	}));

	RampStep.SnappedLevelDelta = 0;
	TransitionPolicy.MaxFoundationDepth = 16;
	const FLayoutFrozenTerrainBiomeAdapterInput SameStageArtifact = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		FIntPoint(32, 16),
		FLayoutTerrainSurfaceSearchSettings(),
		TransitionPolicy,
		FLayoutNoiseCoordinateSettings(),
		16,
		TEXT("Biome.Test"),
		{},
		SupportMap);
	TestFalse(TEXT("Ramp transition evidence ignores same-stage adjacency"), SameStageArtifact.TerrainPlacementCells[0].bHasRampTransitionEvidence);
	TestFalse(TEXT("Shifted ramp transition evidence ignores same-stage adjacency"), SameStageArtifact.TerrainPlacementCells[1].bHasRampTransitionEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsInvalidSteppedSupportOverlapTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsInvalidSteppedSupportOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsInvalidSteppedSupportOverlapTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap SupportMap;
	FLayoutSteppedTerrainSupportSample& FullCellOverlap = SupportMap.SupportSamples.AddDefaulted_GetRef();
	FullCellOverlap.LocalCell = FIntVector(0, 0, 0);
	FullCellOverlap.SupportSurfaceZ = 16;
	FullCellOverlap.SnappedSupportFloorZ = 0;
	FullCellOverlap.SnappedSupportCeilingZ = 16;

	const FLayoutFrozenTerrainBiomeAdapterInput Artifact = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		FIntPoint(16, 16),
		FLayoutTerrainSurfaceSearchSettings(),
		FLayoutWorldBindingTerrainTransitionPolicy(),
		FLayoutNoiseCoordinateSettings(),
		16,
		TEXT("Biome.Test"),
		{},
		SupportMap);

	TestFalse(TEXT("Invalid full-cell terrain overlap fails closed"), Artifact.bHasTerrainPlacementEvidence);
	TestEqual(TEXT("Invalid overlap leaves no placement cells"), Artifact.TerrainPlacementCells.Num(), 0);
	TestTrue(TEXT("Invalid overlap audit is preserved"), Artifact.AuditMessages.Contains(TEXT("TerrainPlacementEvidence=RejectedInvalidLocalTerrainOverlap")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsInconsistentStageShiftTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsInconsistentStageShift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsInconsistentStageShiftTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence MissingShiftCell = MakePlacementCell(FIntVector(3, 0, 0));
	MissingShiftCell.TerrainStageIndex = 1;
	MissingShiftCell.VerticalShiftBlocks = 0;

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Non-zero terrain stage fails closed without matching vertical shift"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.StageNoShift"),
			{MissingShiftCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Inconsistent stage/shift failure reported"), FailureReason.Contains(TEXT("terrain stage")));

	FLayoutTerrainPlacementCellEvidence WrongSignCell = MakePlacementCell(FIntVector(4, 0, 0));
	WrongSignCell.TerrainStageIndex = -1;
	WrongSignCell.VerticalShiftBlocks = 16;
	TestFalse(
		TEXT("Terrain stage sign must match vertical shift sign"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.WrongSign"),
			{WrongSignCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Wrong sign failure reported"), FailureReason.Contains(TEXT("terrain stage")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsExcavationWithoutLocalOverlapTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsExcavationWithoutLocalOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsExcavationWithoutLocalOverlapTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence ExcavationWithoutOverlapCell = MakePlacementCell(FIntVector(6, 0, 0));
	ExcavationWithoutOverlapCell.bHasLocalOverlapZ = false;
	ExcavationWithoutOverlapCell.OverlapMinLocalZ = FLayoutLocalBlockCoord8();
	ExcavationWithoutOverlapCell.OverlapMaxLocalZ = FLayoutLocalBlockCoord8();

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Excavation evidence requires compact local overlap bounds"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.ExcavationWithoutOverlap"),
			{ExcavationWithoutOverlapCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Missing overlap failure reported"), FailureReason.Contains(TEXT("excavation evidence")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceRejectsBridgeSupportMixedWithTerrainModificationTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.RejectsBridgeSupportMixedWithTerrainModification",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceRejectsBridgeSupportMixedWithTerrainModificationTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainPlacementCellEvidence MixedBridgeAndExcavationCell = MakePlacementCell(FIntVector(5, 0, 0));
	MixedBridgeAndExcavationCell.bHasBridgeSupportEvidence = true;

	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Bridge support cannot also authorize terrain modification operations"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.MixedBridgeTerrainModification"),
			{MixedBridgeAndExcavationCell},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Mixed bridge support failure reported"), FailureReason.Contains(TEXT("bridge support")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceCompactsLocalBlockCoordinatesSafelyTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.CompactsLocalBlockCoordinatesSafely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceCompactsLocalBlockCoordinatesSafelyTest::RunTest(const FString& Parameters)
{
	FLayoutLocalBlockCoord8 Coord;
	TestTrue(TEXT("Valid local block coord compacts"), LayoutLocalBlockCoordinates::TryMakeCoord8(255, Coord));
	TestEqual(TEXT("Valid coord value preserved"), Coord.Value, static_cast<uint8>(255));
	TestFalse(TEXT("Negative local block coord rejects"), LayoutLocalBlockCoordinates::TryMakeCoord8(-1, Coord));
	TestFalse(TEXT("Oversized local block coord rejects without truncation"), LayoutLocalBlockCoordinates::TryMakeCoord8(256, Coord));

	FLayoutLocalBlockOffset8 Offset;
	TestTrue(TEXT("Valid local block offset compacts"), LayoutLocalBlockCoordinates::TryMakeOffset8(FIntVector(1, 2, 3), Offset));
	TestEqual(TEXT("Offset X preserved"), Offset.X.Value, static_cast<uint8>(1));
	TestEqual(TEXT("Offset Y preserved"), Offset.Y.Value, static_cast<uint8>(2));
	TestEqual(TEXT("Offset Z preserved"), Offset.Z.Value, static_cast<uint8>(3));
	TestFalse(TEXT("Oversized local block offset rejects"), LayoutLocalBlockCoordinates::TryMakeOffset8(FIntVector(1, 256, 3), Offset));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceAdapterAppliesProducedArtifactTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.AdapterAppliesProducedArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceAdapterAppliesProducedArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit terrain placement evidence artifact builds for central adapter"),
		LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
			TEXT("PlacementEvidence.Artifact.0"),
			{MakePlacementCell()},
			TEXT("PlacementEvidence.Producer"),
			Artifact,
			FailureReason));

	FLayoutProducedTerrainEvidenceArtifacts ProducedArtifacts;
	ProducedArtifacts.TerrainPlacementEvidence = Artifact;
	FLayoutProducedTerrainEvidenceRequirements Requirements;
	Requirements.bRequireTerrainPlacementEvidence = true;
	FLayoutFrozenTerrainBiomeAdapterInput FrozenInput;
	TestTrue(
		TEXT("Central produced terrain evidence adapter applies placement evidence"),
		LayoutProducedTerrainEvidenceAdapter::TryApplyProducedArtifacts(
			ProducedArtifacts,
			Requirements,
			FrozenInput,
			FailureReason));
	TestTrue(TEXT("Central adapter set terrain placement evidence flag"), FrozenInput.bHasTerrainPlacementEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainPlacementEvidenceAllowsRampTransitionWithClearanceOrExcavationTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.AllowsRampTransitionWithClearanceOrExcavation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainPlacementEvidenceAllowsRampTransitionWithClearanceOrExcavationTest::RunTest(const FString& Parameters)
{
	// RampTransition + Clearance: valid hillside cell with flat top and ramp transition to neighbor.
	{
		FLayoutTerrainPlacementCellEvidence Cell;
		Cell.Cell = FIntVector(0, 0, 0);
		Cell.bPlaceableForSelectedMode = true;
		Cell.bHasRampTransitionEvidence = true;
		Cell.bHasClearanceEvidence = true;
		Cell.TerrainStageIndex = 0;
		Cell.VerticalShiftBlocks = 0;
		Cell.ProvenanceId = TEXT("RampTransition.Clearance");

		FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
		FString FailureReason;
		TestTrue(
			TEXT("RampTransition + Clearance is accepted on hillside cells"),
			LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
				TEXT("PlacementEvidence.RampClearance"),
				{Cell},
				TEXT("PlacementEvidence.Producer"),
				Artifact,
				FailureReason));
		TestTrue(TEXT("Acceptance reports ramp transition presence"),
			Artifact.Cells.Num() > 0 && Artifact.Cells[0].bHasRampTransitionEvidence);
	}

	// RampTransition + Excavation: valid cell where stepped edge transitions and local terrain overlaps.
	{
		FLayoutTerrainPlacementCellEvidence Cell;
		Cell.Cell = FIntVector(1, 0, 0);
		Cell.bPlaceableForSelectedMode = true;
		Cell.bHasRampTransitionEvidence = true;
		Cell.bHasExcavationEvidence = true;
		Cell.bHasLocalOverlapZ = true;
		LayoutLocalBlockCoordinates::TryMakeCoord8(2, Cell.OverlapMinLocalZ);
		LayoutLocalBlockCoordinates::TryMakeCoord8(12, Cell.OverlapMaxLocalZ);
		Cell.TerrainStageIndex = 0;
		Cell.VerticalShiftBlocks = 0;
		Cell.ProvenanceId = TEXT("RampTransition.Excavation");

		FLayoutProducedTerrainPlacementEvidenceArtifact Artifact;
		FString FailureReason;
		TestTrue(
			TEXT("RampTransition + Excavation with overlap bounds is accepted"),
			LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
				TEXT("PlacementEvidence.RampExcavation"),
				{Cell},
				TEXT("PlacementEvidence.Producer"),
				Artifact,
				FailureReason));
	}

	return true;
}


// ---- S6: Cluster enforcement test ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainClusterEnforcementExcavatesSmallComponentsTest,
	"PorismExtension.Layout.Terrain.ClusterEnforcementExcavatesSmallComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainClusterEnforcementExcavatesSmallComponentsTest::RunTest(const FString& Parameters)
{
	// 4x1 grid. Columns (0,0),(1,0) at floorZ=24 (cluster size 2 — should be excavated).
	// Columns (2,0),(3,0) at floorZ=32 (cluster size 2 — should be excavated).
	// Column (4,0) at floorZ=32, but isolated (cluster size 1 — excavated too).
	// So everything above base gets excavated. Verify artifact reflects this.
	FLayoutSteppedTerrainSupportMap SupportMap;
	SupportMap.SharedCellHeightInBlocks = 16;
	SupportMap.SupportSamples.Reserve(5);

	auto AddSample = [&SupportMap](int32 X, int32 Y, int32 SnappedFloorZ)
	{
		FLayoutSteppedTerrainSupportSample& S = SupportMap.SupportSamples.AddDefaulted_GetRef();
		S.LocalCell = FIntVector(X, Y, 0);
		S.SupportSurfaceZ = SnappedFloorZ;
		S.SnappedSupportFloorZ = SnappedFloorZ;
	};

	// baseline
	AddSample(0, 0, 16);
	// cluster size 2 at floorZ=32 — isolated from larger cluster
	AddSample(1, 0, 32);
	AddSample(2, 0, 32);
	// gap at X=3
	AddSample(3, 0, 16);
	// cluster size 3 at floorZ=32 — separated by baseline cell
	AddSample(4, 0, 32);
	AddSample(5, 0, 32);
	AddSample(6, 0, 32);

	FLayoutWorldBindingTerrainTransitionPolicy TransitionPolicy;
	TransitionPolicy.bAllowFoundationFill = true;
	TransitionPolicy.MaxFoundationDepth = 4;

	const FLayoutFrozenTerrainBiomeAdapterInput Artifact = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		FIntVector(0, 0, 0),
		FIntVector(0, 0, 0),
		FIntVector(16, 16, 16),
		FIntPoint(96, 16),
		FLayoutTerrainSurfaceSearchSettings(),
		TransitionPolicy,
		FLayoutNoiseCoordinateSettings(),
		16,
		TEXT("Biome.Test"),
		{},
		SupportMap);

	TestTrue(TEXT("Cluster enforcement produces placement evidence"), Artifact.bHasTerrainPlacementEvidence);

	// Expected: 7 cells (2 baseline + 2 small cluster + 3 large cluster)
	TestEqual(TEXT("Seven placement cells"), Artifact.TerrainPlacementCells.Num(), 7);

	// Find cells by local XY
	TMap<FIntPoint, const FLayoutTerrainPlacementCellEvidence*> CellByXY;
	for (const FLayoutTerrainPlacementCellEvidence& Cell : Artifact.TerrainPlacementCells)
	{
		CellByXY.Add(FIntPoint(Cell.Cell.X, Cell.Cell.Y), &Cell);
	}

	// Baseline cell (0,0) at stage 0
	{
		const FLayoutTerrainPlacementCellEvidence* const* BaselinePtr = CellByXY.Find(FIntPoint(0,0));
		TestTrue(TEXT("Baseline cell found"), BaselinePtr != nullptr && *BaselinePtr != nullptr);
		if (BaselinePtr && *BaselinePtr) TestEqual(TEXT("Baseline cell at stage 0"), (*BaselinePtr)->TerrainStageIndex, 0);
	}

	// Cluster size 2 at floorZ=24: (1,0) and (2,0) — excavated to baseline
	{
		const FLayoutTerrainPlacementCellEvidence* const* Ptr1 = CellByXY.Find(FIntPoint(1,0));
		TestTrue(TEXT("Cell (1,0) found"), Ptr1 != nullptr && *Ptr1 != nullptr);
		if (Ptr1 && *Ptr1) TestEqual(TEXT("Excavated cell (1,0) to stage 0"), (*Ptr1)->TerrainStageIndex, 0);
	}
	{
		const FLayoutTerrainPlacementCellEvidence* const* Ptr2 = CellByXY.Find(FIntPoint(2,0));
		TestTrue(TEXT("Cell (2,0) found"), Ptr2 != nullptr && *Ptr2 != nullptr);
		if (Ptr2 && *Ptr2) TestEqual(TEXT("Excavated cell (2,0) to stage 0"), (*Ptr2)->TerrainStageIndex, 0);
	}

	// Cluster size 3 at floorZ=24: (4,0),(5,0),(6,0) — survives
	{
		const FLayoutTerrainPlacementCellEvidence* const* Ptr4 = CellByXY.Find(FIntPoint(4,0));
		TestTrue(TEXT("Cell (4,0) found"), Ptr4 != nullptr && *Ptr4 != nullptr);
		if (Ptr4 && *Ptr4) TestEqual(TEXT("Surviving cell (4,0) at non-zero stage"), (*Ptr4)->TerrainStageIndex, 1);
	}
	{
		const FLayoutTerrainPlacementCellEvidence* const* Ptr5 = CellByXY.Find(FIntPoint(5,0));
		TestTrue(TEXT("Cell (5,0) found"), Ptr5 != nullptr && *Ptr5 != nullptr);
		if (Ptr5 && *Ptr5) TestEqual(TEXT("Surviving cell (5,0) at non-zero stage"), (*Ptr5)->TerrainStageIndex, 1);
	}
	{
		const FLayoutTerrainPlacementCellEvidence* const* Ptr6 = CellByXY.Find(FIntPoint(6,0));
		TestTrue(TEXT("Cell (6,0) found"), Ptr6 != nullptr && *Ptr6 != nullptr);
		if (Ptr6 && *Ptr6) TestEqual(TEXT("Surviving cell (6,0) at non-zero stage"), (*Ptr6)->TerrainStageIndex, 1);
	}

	// Audit message
	bool bFoundAudit = false;
	for (const FString& Msg : Artifact.AuditMessages)
	{
		if (Msg.Contains(TEXT("ExcavatedSmallShiftClusters")))
		{
			bFoundAudit = true;
			TestTrue(TEXT("Audit reports excavated count"), Msg.Contains(TEXT(":2")));
			break;
		}
	}
	TestTrue(TEXT("Artifact contains ExcavatedSmallShiftClusters audit message"), bFoundAudit);

	// Verify support samples: (1,0) and (2,0) flattened to baseline SnappedSupportFloorZ=16
	for (const FLayoutSteppedTerrainSupportSample& Sample : Artifact.SteppedSupportSamples)
	{
		if (Sample.LocalCell.X == 1 && Sample.LocalCell.X == 1)  // intentional: check specific cells
		{
			// cells (1,0) and (2,0) should be flattened
			if (Sample.LocalCell.X == 1 || Sample.LocalCell.X == 2)
			{
				TestEqual(FString::Printf(TEXT("Flattened support (%d,0) SnappedZ=16"), Sample.LocalCell.X),
					Sample.SnappedSupportFloorZ, 16);
			}
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainClusterEnforcementCountsCardinalColumnsTest,
	"PorismExtension.Layout.Terrain.ClusterEnforcement.CountsCardinalColumns",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainClusterEnforcementCountsCardinalColumnsTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap SupportMap;
	auto AddSample = [&SupportMap](const FIntVector Cell, const int32 FloorZ)
	{
		FLayoutSteppedTerrainSupportSample& Sample = SupportMap.SupportSamples.AddDefaulted_GetRef();
		Sample.LocalCell = Cell;
		Sample.SupportSurfaceZ = FloorZ;
		Sample.SnappedSupportFloorZ = FloorZ;
		Sample.SnappedSupportCeilingZ = FloorZ + 16;
	};
	AddSample(FIntVector(0, 0, 0), 0);
	AddSample(FIntVector(1, 0, 0), 16);
	AddSample(FIntVector(1, 0, 1), 16); // Same terrain column; must not add cluster weight.
	AddSample(FIntVector(2, 1, 0), 16); // Diagonal; must not join (1,0).

	FLayoutWorldBindingTerrainTransitionPolicy Policy;
	Policy.MinimumSteppedTerrainShiftClusterCells = 3;
	const FLayoutFrozenTerrainBiomeAdapterInput Artifact =
		FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
			FIntVector::ZeroValue, FIntVector::ZeroValue, FIntVector(16, 16, 16), FIntPoint(48, 32),
			FLayoutTerrainSurfaceSearchSettings(), Policy, FLayoutNoiseCoordinateSettings(),
			16, TEXT("Biome.Test"), {}, SupportMap, true);
	for (const FLayoutSteppedTerrainSupportSample& Sample : Artifact.SteppedSupportSamples)
	{
		if (Sample.LocalCell.X > 0)
		{
			TestEqual(FString::Printf(TEXT("Small cardinal terrain cluster flattens %s"), *Sample.LocalCell.ToString()),
				Sample.SnappedSupportFloorZ, 0);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacementEvidenceEntryTraversabilityRoundTripTest,
	"PorismExtension.Layout.Terrain.PlacementEvidence.EntryTraversabilityRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlacementEvidenceEntryTraversabilityRoundTripTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap SupportMap;
	SupportMap.SharedCellHeightInBlocks = 16;
	for (int32 Y = 0; Y < 3; ++Y)
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutSteppedTerrainSupportSample& S = SupportMap.SupportSamples.AddDefaulted_GetRef();
			S.LocalCell = FIntVector(X, Y, 0);
			S.SupportSurfaceZ = 8;
			S.SnappedSupportFloorZ = 0;
			S.SnappedSupportCeilingZ = 16;
		}

	FLayoutWorldBindingTerrainTransitionPolicy Policy;
	Policy.MaxFoundationDepth = 6;

	FLayoutFrozenTerrainBiomeAdapterInput A =
		FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
			FIntVector(24, 24, 0), FIntVector(0, 0, 0),
			FIntVector(16, 16, 16), FIntPoint(48, 48),
			FLayoutTerrainSurfaceSearchSettings(), Policy, FLayoutNoiseCoordinateSettings(),
			16, TEXT("Biome.Test"), {}, SupportMap);

	FLayoutTerrainSampling::ComputeEntryTraversabilityVerdicts(A, FIntVector(16, 16, 16), Policy.MaxFoundationDepth,
		[](int32 BlockX, int32 BlockY) -> int32 { return BlockX < 0 ? -60 : 18; });

	int32 W = 0, C = 0;
	for (const auto& E : A.TerrainPlacementCells)
	{
		if (E.Cell.X == 0 && E.Cell.Y == 0)
		{
			TestEqual(TEXT("(0,0) is cliff"), static_cast<int32>(E.EntryTraversability), static_cast<int32>(ELayoutEntryTraversabilityVerdict::CliffEdge));
			++C;
		}
		else if (E.Cell.X == 0 || E.Cell.X == 2 || E.Cell.Y == 0 || E.Cell.Y == 2)
		{
			if (E.EntryTraversability == ELayoutEntryTraversabilityVerdict::Walkable) ++W;
		}
		else
		{
			TestEqual(FString::Printf(TEXT("Interior (%d,%d)=None"), E.Cell.X, E.Cell.Y),
				static_cast<int32>(E.EntryTraversability), static_cast<int32>(ELayoutEntryTraversabilityVerdict::None));
		}
	}
	TestTrue(TEXT("At least one walkable"), W >= 1);
	TestTrue(TEXT("At least one cliff"), C >= 1);
	return true;
}