// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/ChunkWorldCore.h"
#include "Misc/AutomationTest.h"

#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Runtime/LayoutRealizationWritePlan.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;

namespace
{
	/** Builds smallest accepted payload containing one active template cell and one removed reserved-open cell. */
	FLayoutSolveResult BuildSolveResult(const ELayoutReservedOpenTerrainBehavior TerrainBehavior)
	{
		FLayoutSolveResult Result;
		Result.bSucceeded = true;
		Result.SharedCellSizeInBlocks = FIntVector(2, 3, 2);
		Result.TemplatePlacementZOffsetBlocks = 3;

		FLayoutPlannedCell& PlannedCell = Result.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = ELayoutCellIntent::Interior;

		FLayoutPlacedModule& Placement = Result.Placements.AddDefaulted_GetRef();
		Placement.Cell = FIntVector::ZeroValue;
		Placement.Intent = ELayoutCellIntent::Interior;
		Placement.ModuleSnapshotId = TEXT("ReservedOpenTerrain.Module");
		Placement.TemplatePath = FSoftObjectPath(TEXT("/Game/Test/ReservedOpenTerrain.ReservedOpenTerrain"));

		FLayoutCellReservationRecord& Reservation = Result.CompiledReservations.AddDefaulted_GetRef();
		Reservation.ReservationId = TEXT("ReservedOpenTerrain.ClearCell");
		Reservation.Cell = FIntVector(1, 0, 0);
		Reservation.Intent = ELayoutCellIntent::Interior;
		Reservation.ReservationKind = ELayoutCellReservationKind::ReservedEmpty;
		Reservation.TerrainBehavior = TerrainBehavior;
		return Result;
	}

	/** Builds frozen terrain authority matching BuildSolveResult without granting active-cell authority to removed cell. */
	FLayoutFrozenTerrainContract BuildFrozenTerrainContract(const ELayoutReservedOpenTerrainBehavior TerrainBehavior)
	{
		FLayoutFrozenTerrainContract Contract;
		Contract.ContractId = TEXT("ReservedOpenTerrain.Contract");
		Contract.FootprintMinBlockWorldPos = FIntVector(10, 20, 30);
		Contract.SharedCellSizeInBlocks = FIntVector(2, 3, 2);
		Contract.FootprintSizeInCells = FIntPoint(2, 1);
		Contract.ActiveCells = {{FIntVector::ZeroValue}};
		Contract.CellContracts = {{FIntVector::ZeroValue, ELayoutFrozenTerrainCellContract::Active}};

		FLayoutCellReservationRecord& Reservation = Contract.ReservedOpenTerrainReservations.AddDefaulted_GetRef();
		Reservation.ReservationId = TEXT("ReservedOpenTerrain.ClearCell");
		Reservation.Cell = FIntVector(1, 0, 0);
		Reservation.Intent = ELayoutCellIntent::Interior;
		Reservation.ReservationKind = ELayoutCellReservationKind::ReservedEmpty;
		Reservation.TerrainBehavior = TerrainBehavior;
		return Contract;
	}

	/** Builds one validated write plan for terrain-behavior assertions. */
	bool TryBuildWritePlan(
		const ELayoutReservedOpenTerrainBehavior TerrainBehavior,
		FLayoutRealizationWritePlan& OutWritePlan,
		FLayoutFrozenTerrainContract& OutContract,
		FString& OutFailureReason)
	{
		const FLayoutSolveResult SolveResult = BuildSolveResult(TerrainBehavior);
		OutContract = BuildFrozenTerrainContract(TerrainBehavior);
		return LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Site,
			TEXT("ReservedOpenTerrain.Artifact"),
			OutContract.ActiveCells.Num(),
			SolveResult,
			OutContract,
			OutWritePlan,
			OutFailureReason);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservedOpenTerrainJsonRoundTripTest,
	"PorismExtension.Layout.Runtime.ReservedOpenTerrain.JsonRoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservedOpenTerrainJsonRoundTripTest::RunTest(const FString& Parameters)
{
	FLayoutReservedOpenSpaceRule DefaultRule;
	TestEqual(TEXT("Reserved-open terrain behavior defaults to LeaveExistingTerrain"), DefaultRule.TerrainBehavior, ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain);

	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_ReservedOpenTerrainJson"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	FLayoutReservedOpenSpaceRule& LeaveRule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	LeaveRule.RuleId = TEXT("LeaveTerrain");
	LeaveRule.TerrainBehavior = ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain;
	FLayoutReservedOpenSpaceRule& ClearRule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	ClearRule.RuleId = TEXT("ClearTerrain");
	ClearRule.TerrainBehavior = ELayoutReservedOpenTerrainBehavior::ClearReservedCell;

	FString Json;
	FString ExportError;
	if (!TestTrue(TEXT("Reserved-open terrain behavior exports to JSON"), FLayoutProfileJsonFixture::ExportToString(Profile, Profile->ContentSet, Json, ExportError)))
	{
		AddError(ExportError);
		return false;
	}
	FLayoutProfileJsonFixtureAssets Imported;
	TArray<FString> ImportIssues;
	if (!TestTrue(TEXT("Reserved-open terrain behavior imports from JSON"), FLayoutProfileJsonFixture::ImportFromString(Json, GetTransientPackage(), Imported, ImportIssues)))
	{
		AddError(FString::Join(ImportIssues, TEXT("; ")));
		return false;
	}
	TestEqual(TEXT("JSON round trip preserves reserved-open rule count"), Imported.Profile->ReservedOpenSpaceRules.Num(), 2);
	if (Imported.Profile->ReservedOpenSpaceRules.Num() == 2)
	{
		TestEqual(TEXT("JSON round trip preserves LeaveExistingTerrain"), Imported.Profile->ReservedOpenSpaceRules[0].TerrainBehavior, ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain);
		TestEqual(TEXT("JSON round trip preserves ClearReservedCell"), Imported.Profile->ReservedOpenSpaceRules[1].TerrainBehavior, ELayoutReservedOpenTerrainBehavior::ClearReservedCell);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservedOpenTerrainDirectRootContractTest,
	"PorismExtension.Layout.Runtime.ReservedOpenTerrain.DirectRootPreservesClearReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservedOpenTerrainLeaveBehaviorTest,
	"PorismExtension.Layout.Runtime.ReservedOpenTerrain.LeaveEmitsNoClearWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservedOpenTerrainDirectRootContractTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Reserved-open direct-root test creates runtime component"), Harness.RuntimeComponent))
	{
		return false;
	}

	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_ReservedOpenTerrainDirectRoot"),
		FIntPoint(7, 7),
		FIntPoint(7, 7),
		3,
		0,
		true);
	Profile->bUndergroundPlacement = true;
	// Three 16-block levels fit air at Z=2..49; solid ceiling ends at surface Z=100.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAUUkduQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAACcM6K7AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAMGoJLwAAAAA"));
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(16, 16, 2), EmptyMaterial, false);

	FLayoutReservedOpenSpaceRule& Rule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	Rule.RuleId = TEXT("ClearDirectRootTerrain");
	Rule.PlacementZone = ELayoutPlacementZone::Any;
	Rule.MinReservedCells = 1;
	Rule.MaxReservedCells = 1;
	Rule.TerrainBehavior = ELayoutReservedOpenTerrainBehavior::ClearReservedCell;

	FResolvedLayoutSiteRecord SiteRecord;
	FLayoutRegionSolveScheduleResult ScheduleResult;
	FString FailureReason;
	if (!TestTrue(TEXT("Reserved-open direct-root solve succeeds"), Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
			FIntVector(8, 8, 2),
			Profile,
			601,
			SiteRecord,
			ScheduleResult,
			&FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	FLayoutFrozenTerrainContract Contract;
	if (!TestTrue(TEXT("Direct-root solve retains frozen terrain contract"), Harness.RuntimeComponent->TryGetAcceptedFrozenTerrainContract(SiteRecord, Contract, &FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Direct-root frozen contract retains one reserved-open record"), Contract.ReservedOpenTerrainReservations.Num(), 1);
	if (Contract.ReservedOpenTerrainReservations.IsEmpty())
	{
		return false;
	}
	TestEqual(TEXT("Direct-root frozen reservation retains ClearReservedCell"), Contract.ReservedOpenTerrainReservations[0].TerrainBehavior, ELayoutReservedOpenTerrainBehavior::ClearReservedCell);

	const FLayoutCellReservationRecord& Reservation = Contract.ReservedOpenTerrainReservations[0];
	const FIntVector ClearVolumeMin = Contract.FootprintMinBlockWorldPos
		+ FIntVector(
			Reservation.Cell.X * Contract.SharedCellSizeInBlocks.X,
			Reservation.Cell.Y * Contract.SharedCellSizeInBlocks.Y,
			Reservation.Cell.Z * Contract.SharedCellSizeInBlocks.Z + SiteRecord.GetResolvedSiteSolvedPayload().SolveResult.TemplatePlacementZOffsetBlocks);
	for (int32 Z = 0; Z < Contract.SharedCellSizeInBlocks.Z; ++Z)
	{
		for (int32 Y = 0; Y < Contract.SharedCellSizeInBlocks.Y; ++Y)
		{
			for (int32 X = 0; X < Contract.SharedCellSizeInBlocks.X; ++X)
			{
				Harness.World->SetBlockValueByBlockWorldPos(ClearVolumeMin + FIntVector(X, Y, Z), SinfullMaterial, false);
			}
		}
	}
	if (!TestTrue(TEXT("Direct-root ClearReservedCell realization succeeds"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(
		TEXT("Direct-root ClearReservedCell clears its exact shared-cell volume"),
		Harness.World->GetBlockValueByBlockWorldPos(ClearVolumeMin, ERessourceType::MaterialIndex, 0),
		EmptyMaterial);
	return true;
}

bool FLayoutReservedOpenTerrainLeaveBehaviorTest::RunTest(const FString& Parameters)
{
	FLayoutRealizationWritePlan WritePlan;
	FLayoutFrozenTerrainContract Contract;
	FString FailureReason;
	TestTrue(
		TEXT("LeaveExistingTerrain builds write plan"),
		TryBuildWritePlan(ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain, WritePlan, Contract, FailureReason));
	TestEqual(TEXT("LeaveExistingTerrain has no clear cells"), WritePlan.TerrainWrites.Excavation.ReservedOpenClearCells.Num(), 0);
	TestEqual(TEXT("LeaveExistingTerrain has no clear writes"), WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites.Num(), 0);
	TestEqual(TEXT("LeaveExistingTerrain leaves terrain-write count unchanged"), WritePlan.TerrainWrites.TerrainWriteCount, 0);
	TestTrue(TEXT("LeaveExistingTerrain replay validation passes"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservedOpenTerrainStageAnchorTest,
	"PorismExtension.Layout.Runtime.ReservedOpenTerrain.UsesFrozenStageAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservedOpenTerrainStageAnchorTest::RunTest(const FString& Parameters)
{
	const FLayoutSolveResult SolveResult = BuildSolveResult(ELayoutReservedOpenTerrainBehavior::ClearReservedCell);
	FLayoutFrozenTerrainContract Contract = BuildFrozenTerrainContract(ELayoutReservedOpenTerrainBehavior::ClearReservedCell);
	FLayoutFrozenTerrainStageCellRecord& Stage = Contract.StageMap.AddDefaulted_GetRef();
	Stage.FootprintCellXY = FIntPoint(1, 0);
	Stage.ResolvedStageBaseBlockWorldZ = 50;

	FLayoutRealizationWritePlan WritePlan;
	FString FailureReason;
	if (!TestTrue(TEXT("Frozen stage anchor builds reserved-open clear plan"), LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Site,
			TEXT("ReservedOpenTerrain.StageArtifact"),
			Contract.ActiveCells.Num(),
			SolveResult,
			Contract,
			WritePlan,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Reserved-open clear volume uses frozen stage base"), WritePlan.TerrainWrites.Excavation.ReservedOpenClearCells[0].VolumeMinBlockWorldPos, FIntVector(12, 20, 53));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutReservedOpenTerrainClearBehaviorTest,
	"PorismExtension.Layout.Runtime.ReservedOpenTerrain.ClearWritesExactSharedCellVolume",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutReservedOpenTerrainClearBehaviorTest::RunTest(const FString& Parameters)
{
	FLayoutRealizationWritePlan WritePlan;
	FLayoutFrozenTerrainContract Contract;
	FString FailureReason;
	if (!TestTrue(
		TEXT("ClearReservedCell builds write plan"),
		TryBuildWritePlan(ELayoutReservedOpenTerrainBehavior::ClearReservedCell, WritePlan, Contract, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	const FLayoutRealizationExcavationSubplan& Excavation = WritePlan.TerrainWrites.Excavation;
	TestEqual(TEXT("ClearReservedCell creates one clear volume"), Excavation.ReservedOpenClearCells.Num(), 1);
	TestEqual(TEXT("ClearReservedCell creates exact shared-cell write count"), Excavation.ReservedOpenClearWrites.Num(), 12);
	TestEqual(TEXT("ClearReservedCell contributes all generated writes"), WritePlan.TerrainWrites.TerrainWriteCount, 12);
	if (Excavation.ReservedOpenClearCells.IsEmpty())
	{
		return false;
	}
	const FLayoutRealizationReservedOpenClearCell& ClearCell = Excavation.ReservedOpenClearCells[0];
	TestEqual(TEXT("Clear volume uses resolved template anchor"), ClearCell.VolumeMinBlockWorldPos, FIntVector(12, 20, 33));
	TestEqual(TEXT("Clear volume stops at one shared cell"), ClearCell.VolumeMaxBlockWorldPos, FIntVector(13, 22, 34));
	for (const FLayoutRealizationTerrainWriteEntry& ClearWrite : Excavation.ReservedOpenClearWrites)
	{
		TestTrue(TEXT("Clear write stays inside selected volume"),
			ClearWrite.BlockWorldPos.X >= ClearCell.VolumeMinBlockWorldPos.X && ClearWrite.BlockWorldPos.X <= ClearCell.VolumeMaxBlockWorldPos.X
				&& ClearWrite.BlockWorldPos.Y >= ClearCell.VolumeMinBlockWorldPos.Y && ClearWrite.BlockWorldPos.Y <= ClearCell.VolumeMaxBlockWorldPos.Y
				&& ClearWrite.BlockWorldPos.Z >= ClearCell.VolumeMinBlockWorldPos.Z && ClearWrite.BlockWorldPos.Z <= ClearCell.VolumeMaxBlockWorldPos.Z);
		TestEqual(TEXT("Clear write uses EmptyMaterial"), ClearWrite.Material, EmptyMaterial);
	}
	TestTrue(TEXT("ClearReservedCell replay validation passes"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("ClearReservedCell creates chunk-world harness"), Harness.World))
	{
		return false;
	}
	for (const FLayoutRealizationTerrainWriteEntry& ClearWrite : Excavation.ReservedOpenClearWrites)
	{
		Harness.World->SetBlockValueByBlockWorldPos(ClearWrite.BlockWorldPos, 1, false);
	}
	const FIntVector AdjacentTerrain(14, 20, 33);
	const FIntVector TerrainBelow(12, 20, 32);
	Harness.World->SetBlockValueByBlockWorldPos(AdjacentTerrain, 1, false);
	Harness.World->SetBlockValueByBlockWorldPos(TerrainBelow, 1, false);
	FLayoutRealizationWritePlanExecutionState ExecutionState;
	TestTrue(TEXT("ClearReservedCell applies through terrain replay"), LayoutRealizationWritePlan::ApplyTerrainWriteReplay(Harness.World, WritePlan, Contract, ExecutionState, FailureReason));
	TestTrue(TEXT("ClearReservedCell terrain replay completes before templates"), ExecutionState.bTerrainReplayApplied);
	for (const FLayoutRealizationTerrainWriteEntry& ClearWrite : Excavation.ReservedOpenClearWrites)
	{
		TestEqual(TEXT("ClearReservedCell mutates every selected block"), Harness.World->GetBlockValueByBlockWorldPos(ClearWrite.BlockWorldPos, ERessourceType::MaterialIndex, 0), EmptyMaterial);
	}
	TestEqual(TEXT("ClearReservedCell preserves adjacent terrain"), Harness.World->GetBlockValueByBlockWorldPos(AdjacentTerrain, ERessourceType::MaterialIndex, 0), 1);
	TestEqual(TEXT("ClearReservedCell preserves terrain below volume"), Harness.World->GetBlockValueByBlockWorldPos(TerrainBelow, ERessourceType::MaterialIndex, 0), 1);
	TestTrue(TEXT("ClearReservedCell replay is idempotent"), LayoutRealizationWritePlan::ApplyTerrainWriteReplay(Harness.World, WritePlan, Contract, ExecutionState, FailureReason));

	WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites[0].Material = 1;
	TestFalse(TEXT("Tampered clear material rejects before replay"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));
	TestTrue(TEXT("Tampered clear material failure is explicit"), FailureReason.Contains(TEXT("stale typed terrain subplan")));

	TestTrue(TEXT("Fresh clear plan rebuilds for write-position tamper"), TryBuildWritePlan(ELayoutReservedOpenTerrainBehavior::ClearReservedCell, WritePlan, Contract, FailureReason));
	WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites[0].BlockWorldPos.X += 1;
	TestFalse(TEXT("Tampered clear write position rejects before replay"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));

	TestTrue(TEXT("Fresh clear plan rebuilds for write-count tamper"), TryBuildWritePlan(ELayoutReservedOpenTerrainBehavior::ClearReservedCell, WritePlan, Contract, FailureReason));
	WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites.Pop();
	TestFalse(TEXT("Tampered clear write count rejects before replay"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));

	TestTrue(TEXT("Fresh clear plan rebuilds for volume-bounds tamper"), TryBuildWritePlan(ELayoutReservedOpenTerrainBehavior::ClearReservedCell, WritePlan, Contract, FailureReason));
	WritePlan.TerrainWrites.Excavation.ReservedOpenClearCells[0].VolumeMaxBlockWorldPos.Z -= 1;
	TestFalse(TEXT("Tampered clear volume bounds reject before replay"), LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(WritePlan, Contract, FailureReason));
	return true;
}
