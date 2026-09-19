// Copyright 2026 Spotted Loaf Studio

#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Runtime/LayoutRealizationWritePlan.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainResidualSuppressesTerrainWritesTest,
	"PorismExtension.Layout.Runtime.TerrainResidual.SuppressUntouchedTerrainWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Proves intentional terrain residual authority remains active while suppressing terrain and template writes. */
bool FLayoutTerrainResidualSuppressesTerrainWritesTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.FootprintSize = FIntPoint(2, 1);
	SolveResult.SharedCellSizeInBlocks = FIntVector(2, 2, 2);
	FLayoutPlannedCell& PlannedCell = SolveResult.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::Interior;
	PlannedCell.ModuleLevelIndex = 0;
	PlannedCell.PlacementZone = ELayoutPlacementZone::Interior;
	FLayoutPlannedCell& StructuralCell = SolveResult.PlannedCells.AddDefaulted_GetRef();
	StructuralCell.Cell = FIntVector(1, 0, 0);
	StructuralCell.Intent = ELayoutCellIntent::Boundary;
	StructuralCell.ModuleLevelIndex = 0;
	StructuralCell.PlacementZone = ELayoutPlacementZone::Edge;
	FLayoutPlacedModule& StructuralPlacement = SolveResult.Placements.AddDefaulted_GetRef();
	StructuralPlacement.Cell = StructuralCell.Cell;
	StructuralPlacement.Intent = StructuralCell.Intent;
	StructuralPlacement.ModuleSnapshotId = TEXT("TerrainResidualRealization.StructuralModule");
	StructuralPlacement.TemplatePath = FSoftObjectPath(TEXT("/Game/Test/TerrainResidualStructural.TerrainResidualStructural"));
	FLayoutResidualCellRecord& Residual = SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
	Residual.Cell = FIntVector::ZeroValue;
	Residual.Intent = ELayoutCellIntent::Interior;
	Residual.Source = ELayoutResidualCellSource::TerrainBackedRule;
	Residual.SourceRegionDebugPath = TEXT("TerrainResidualRealization");
	Residual.SourceSparsePlacementRuleId = TEXT("PreserveTerrain");
	Residual.ModuleLevelIndex = 0;
	Residual.PlacementZone = ELayoutPlacementZone::Interior;

	FLayoutFrozenTerrainContract Contract;
	Contract.ContractId = TEXT("TerrainResidualRealization.Contract");
	Contract.FootprintMinBlockWorldPos = FIntVector(10, 20, 30);
	Contract.SharedCellSizeInBlocks = FIntVector(2, 2, 2);
	Contract.FootprintSizeInCells = FIntPoint(2, 1);
	Contract.ActiveCells = {{FIntVector::ZeroValue}, {FIntVector(1, 0, 0)}};
	FLayoutTerrainCellContractRecord& CellContract = Contract.CellContracts.AddDefaulted_GetRef();
	CellContract.Cell = FIntVector::ZeroValue;
	CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
	CellContract.bHasFoundationFillEvidence = true;
	CellContract.RequiredFoundationDepth = 1;
	FLayoutTerrainCellContractRecord& StructuralCellContract = Contract.CellContracts.AddDefaulted_GetRef();
	StructuralCellContract.Cell = FIntVector(1, 0, 0);
	StructuralCellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
	FLayoutFrozenTerrainWriteRecord& TerrainWrite = Contract.TerrainWrites.AddDefaulted_GetRef();
	TerrainWrite.BlockWorldPos = Contract.FootprintMinBlockWorldPos;
	TerrainWrite.Material = 7;
	TerrainWrite.SourceContract = ELayoutFrozenTerrainCellContract::Active;

	FLayoutRealizationWritePlan WritePlan;
	FString FailureReason;
	const bool bBuilt = LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource::Site,
		TEXT("TerrainResidualRealization.Artifact"),
		2,
		SolveResult,
		Contract,
		WritePlan,
		FailureReason);
	TestTrue(TEXT("Terrain-residual write plan builds with active no-write authority"), bBuilt);
	if (!bBuilt)
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Untouched terrain residual suppresses frozen terrain write"), WritePlan.TerrainWrites.TerrainWriteCount, 0);
	TestEqual(TEXT("Untouched terrain residual emits no foundation/ramp write"), WritePlan.TerrainWrites.Support.FoundationRampWrites.Num(), 0);
	TestEqual(TEXT("Only structural cell emits template placement"), WritePlan.TemplatePlacements.Entries.Num(), 1);
	if (!WritePlan.TemplatePlacements.Entries.IsEmpty())
	{
		TestEqual(TEXT("Template placement excludes untouched residual cell"), WritePlan.TemplatePlacements.Entries[0].Cell, FIntVector(1, 0, 0));
	}
	return true;
}
