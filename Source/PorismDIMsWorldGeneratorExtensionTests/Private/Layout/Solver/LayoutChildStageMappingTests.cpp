// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Misc/AutomationTest.h"
#include "HAL/PlatformTime.h"

#include "Layout/Types/LayoutGameplayTags.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildStageMappingDerivesEffectiveGeometryTest,
	"PorismExtension.Layout.Solver.ChildStageMapping.DerivesEffectiveGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildStageMappingGroundPolicyUsesAuthoredLevelTest,
	"PorismExtension.Layout.Solver.ChildStageMapping.GroundPolicyUsesAuthoredLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildStageMappingLookupEquivalenceTest,
	"PorismExtension.Layout.Solver.ChildStageMapping.LookupEquivalence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildStageMappingLookupEquivalenceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Parent;
	TArray<FLayoutPlannedCell> ParentCells;
	TArray<FLayoutPlannedCell> ChildCells;
	const FIntVector Offset(8, 8, 0);
	for (int32 Y = 0; Y < 32; ++Y)
	{
		for (int32 X = 0; X < 32; ++X)
		{
			auto& Stage = Parent.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
			Stage.FootprintCellXY = FIntPoint(X, Y);
			Stage.TerrainStageIndex = X % 2;
			for (int32 Z = 0; Z < 2; ++Z)
			{
				auto& Cell = ParentCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, Z);
				Cell.ModuleLevelIndex = Z;
				Cell.Intent = ELayoutCellIntent::Interior;
				if (Cell.Cell != Offset)
				{
					auto& Contract = Parent.PrecomputedFrozenTerrainContract.CellContracts.AddDefaulted_GetRef();
					Contract.Cell = Cell.Cell;
					Contract.FoundationMaterial = 1000 + X + 32 * Y + 1024 * Z;
				}
				auto& Face = Parent.PrecomputedFrozenTerrainContract.TerrainBackedNeighborFaces.AddDefaulted_GetRef();
				Face.Cell = Cell.Cell;
				auto& Reservation = Parent.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.AddDefaulted_GetRef();
				Reservation.Cell = Cell.Cell;
				Parent.PrecomputedFrozenTerrainContract.ProtectedOccupiedCells.Add(Cell.Cell);
				if (X >= 8 && X < 24 && Y >= 8 && Y < 24)
				{
					FLayoutPlannedCell Child = Cell;
					Child.Cell -= Offset;
					ChildCells.Add(Child);
				}
			}
		}
	}
	// Legacy linear lookup takes the first record, while absent records use defaults.
	auto& Duplicate = Parent.PrecomputedFrozenTerrainContract.CellContracts.AddDefaulted_GetRef();
	Duplicate.Cell = Offset + FIntVector(0, 0, 1);
	Duplicate.FoundationMaterial = -1;

	FLayoutChildStageMappingResult Mapping;
	FString Failure;
	FLayoutId FirstMappingId;
	TArray<double> Durations;
	for (int32 Run = 0; Run < 7; ++Run)
	{
		const double Start = FPlatformTime::Seconds();
		if (!TestTrue(TEXT("Large two-level mapping succeeds"),
			LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
				Parent, ParentCells, FIntPoint(16, 16), Offset, TEXT("LookupRoom"),
				0, false, ChildCells, Mapping, Failure))) return false;
		Durations.Add((FPlatformTime::Seconds() - Start) * 1000.0);
		if (Run == 0) FirstMappingId = Mapping.MappingId;
		TestEqual(TEXT("Repeated mapping identity is stable"), Mapping.MappingId, FirstMappingId);
	}
	Durations.Sort();
	AddInfo(FString::Printf(TEXT("Child mapping: 2048 parent / 512 child cells, median %.3f ms; id=%s"),
		Durations[Durations.Num() / 2], *Mapping.MappingId.ToString()));
	TestEqual(TEXT("Mapping identity matches pre-index baseline"), Mapping.MappingId,
		FLayoutId(TEXT("ChildStage.LookupRoom.Yaw0.OffsetX=8 Y=8 Z=0.Class0.b5ecbbc2")));
	TestEqual(TEXT("Cross-stage rigid volume remains neutral"), Mapping.StageClass, ELayoutChildPlacementStageClass::StageNeutral);
	TestEqual(TEXT("Frontiers deduplicate stacked columns"), Mapping.CrossedFrontierIds.Num(), 15 * 16);
	TestEqual(TEXT("All child cells mapped"), Mapping.Cells.Num(), ChildCells.Num());
	const auto& Terrain = Mapping.ChildLocalTerrainContract;
	TestEqual(TEXT("Only mapped faces retained"), Terrain.TerrainBackedNeighborFaces.Num(), ChildCells.Num());
	TestEqual(TEXT("Only mapped reservations retained"), Terrain.ReservedOpenTerrainReservations.Num(), ChildCells.Num());
	TestEqual(TEXT("Only mapped protection retained"), Terrain.ProtectedOccupiedCells.Num(), ChildCells.Num());
	if (!TestEqual(TEXT("One contract per child cell"), Terrain.CellContracts.Num(), ChildCells.Num())) return false;
	for (int32 Index = 0; Index < ChildCells.Num(); ++Index)
	{
		const FIntVector ChildCell = ChildCells[Index].Cell;
		const FIntVector ParentCell = ChildCell + Offset;
		TestEqual(TEXT("Child order unchanged"), Terrain.CellContracts[Index].Cell, ChildCell);
		TestEqual(TEXT("First parent contract or default retained"), Terrain.CellContracts[Index].FoundationMaterial,
			ParentCell == Offset ? 0 : 1000 + ParentCell.X + 32 * ParentCell.Y + 1024 * ParentCell.Z);
		if (Terrain.TerrainBackedNeighborFaces.IsValidIndex(Index))
			TestEqual(TEXT("Face translation/order unchanged"), Terrain.TerrainBackedNeighborFaces[Index].Cell, ChildCell);
		if (Terrain.ReservedOpenTerrainReservations.IsValidIndex(Index))
			TestEqual(TEXT("Reservation translation/order unchanged"), Terrain.ReservedOpenTerrainReservations[Index].Cell, ChildCell);
		if (Terrain.ProtectedOccupiedCells.IsValidIndex(Index))
			TestEqual(TEXT("Protection translation/order unchanged"), Terrain.ProtectedOccupiedCells[Index], ChildCell);
	}
	return true;
}

bool FLayoutChildStageMappingGroundPolicyUsesAuthoredLevelTest::RunTest(const FString& Parameters)
{
	FLayoutPlannedCell MappedGroundCell;
	MappedGroundCell.Cell = FIntVector(0, 0, 1);
	MappedGroundCell.ModuleLevelIndex = 0;

	FLayoutPlannedCell MappedUpperCell;
	MappedUpperCell.Cell = FIntVector(0, 0, 2);
	MappedUpperCell.ModuleLevelIndex = 1;

	const bool bAcceptsShiftedGround = TestTrue(
		TEXT("Ground-only child accepts authored ground shifted to physical Z=1"),
		LayoutProfileSolverInternal::DoesMappedChildPlanMatchLevelPlacementPolicyForTests(
			{MappedGroundCell, MappedUpperCell},
			{MappedGroundCell, MappedUpperCell},
			0,
			ELayoutLevelPlacementPolicy::GroundOnly,
			0));
	const bool bRejectsAuthoredUpperLevel = TestFalse(
		TEXT("Ground-only child rejects authored parent level 1 regardless of physical stage"),
		LayoutProfileSolverInternal::DoesMappedChildPlanMatchLevelPlacementPolicyForTests(
			{MappedGroundCell, MappedUpperCell},
			{MappedGroundCell, MappedUpperCell},
			1,
			ELayoutLevelPlacementPolicy::GroundOnly,
			0));
	return bAcceptsShiftedGround && bRejectsAuthoredUpperLevel;
}

bool FLayoutChildStageMappingDerivesEffectiveGeometryTest::RunTest(const FString& Parameters)
{
	auto AddPlannedCell = [](TArray<FLayoutPlannedCell>& Cells, const FIntVector Cell, const int32 ModuleLevel)
	{
		FLayoutPlannedCell& PlannedCell = Cells.AddDefaulted_GetRef();
		PlannedCell.Cell = Cell;
		PlannedCell.ModuleLevelIndex = ModuleLevel;
		PlannedCell.Intent = ELayoutCellIntent::Interior;
	};
	auto AddStage = [](FLayoutRegionSolveRequest& Request, const FIntPoint XY, const int32 Stage)
	{
		FLayoutFrozenTerrainStageCellRecord& Record =
			Request.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		Record.FootprintCellXY = XY;
		Record.TerrainStageIndex = Stage;
	};

	TArray<FLayoutPlannedCell> ChildCells;
	AddPlannedCell(ChildCells, FIntVector(0, 0, 0), 0);
	AddPlannedCell(ChildCells, FIntVector(1, 0, 0), 0);

	FLayoutRegionSolveRequest FlatParent;
	TArray<FLayoutPlannedCell> FlatParentCells;
	AddPlannedCell(FlatParentCells, FIntVector(0, 0, 0), 0);
	AddPlannedCell(FlatParentCells, FIntVector(1, 0, 0), 0);
	FLayoutChildStageMappingResult Mapping;
	FString FailureReason;
	TestTrue(
		TEXT("Flat parent maps through stage-neutral authority"),
		LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
			FlatParent,
			FlatParentCells,
			FIntPoint(2, 1),
			FIntVector::ZeroValue,
			TEXT("Room"),
			0,
			false,
			ChildCells,
			Mapping,
			FailureReason));
	TestEqual(TEXT("Flat mapping class"), Mapping.StageClass, ELayoutChildPlacementStageClass::StageNeutral);

	FLayoutRegionSolveRequest RigidStageParent;
	AddStage(RigidStageParent, FIntPoint(0, 0), 1);
	AddStage(RigidStageParent, FIntPoint(1, 0), 1);
	TArray<FLayoutPlannedCell> RigidStageParentCells;
	AddPlannedCell(RigidStageParentCells, FIntVector(0, 0, 1), 1);
	AddPlannedCell(RigidStageParentCells, FIntVector(1, 0, 1), 1);
	FailureReason.Reset();
	TestTrue(
		TEXT("One selected parent stage produces a rigid child mapping"),
		LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
			RigidStageParent,
			RigidStageParentCells,
			FIntPoint(2, 1),
			FIntVector(0, 0, 1),
			TEXT("RigidRoom"),
			0,
			false,
			ChildCells,
			Mapping,
			FailureReason));
	TestEqual(TEXT("Rigid mapping class"), Mapping.StageClass, ELayoutChildPlacementStageClass::StageRigid);

	FLayoutRegionSolveRequest StageNeutralParent;
	AddStage(StageNeutralParent, FIntPoint(0, 0), 0);
	AddStage(StageNeutralParent, FIntPoint(1, 0), 1);
	TArray<FLayoutPlannedCell> StageNeutralParentCells;
	AddPlannedCell(StageNeutralParentCells, FIntVector(0, 0, 1), 1);
	AddPlannedCell(StageNeutralParentCells, FIntVector(1, 0, 1), 1);
	FailureReason.Reset();
	TestTrue(
		TEXT("Rigid upper-story geometry remains stage-neutral across underlying stage labels"),
		LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
			StageNeutralParent,
			StageNeutralParentCells,
			FIntPoint(2, 1),
			FIntVector(0, 0, 1),
			TEXT("UpperRoom"),
			0,
			false,
			ChildCells,
			Mapping,
			FailureReason));
	TestEqual(TEXT("Upper-story mapping ignores underlying stage labels"), Mapping.StageClass, ELayoutChildPlacementStageClass::StageNeutral);

	FLayoutRegionSolveRequest SteppedParent;
	AddStage(SteppedParent, FIntPoint(0, 0), 0);
	AddStage(SteppedParent, FIntPoint(1, 0), 1);
	TArray<FLayoutPlannedCell> SteppedParentCells;
	AddPlannedCell(SteppedParentCells, FIntVector(0, 0, 0), 0);
	AddPlannedCell(SteppedParentCells, FIntVector(1, 0, 1), 0);
	FailureReason.Reset();
	TestTrue(
		TEXT("Stepped-capable child inherits finalized parent stages"),
		LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
			SteppedParent,
			SteppedParentCells,
			FIntPoint(2, 1),
			FIntVector::ZeroValue,
			TEXT("Room"),
			1,
			true,
			ChildCells,
			Mapping,
			FailureReason));
	TestEqual(TEXT("Inherited mapping class"), Mapping.StageClass, ELayoutChildPlacementStageClass::InheritedStage);
	TestEqual(TEXT("First mapped child remains at local Z=0"), Mapping.ChildLocalPlannedCells[0].Cell.Z, 0);
	TestEqual(TEXT("Raised mapped child follows parent stage at local Z=1"), Mapping.ChildLocalPlannedCells[1].Cell.Z, 1);
	TestEqual(TEXT("Authored child level remains unchanged"), Mapping.ChildLocalPlannedCells[1].ModuleLevelIndex, 0);
	TestEqual(TEXT("Two parent stages are clipped into child authority"), Mapping.ChildLocalStageMap.Num(), 2);
	TestEqual(TEXT("One crossed frontier is recorded"), Mapping.CrossedFrontierIds.Num(), 1);

	FLayoutChildStageMappingResult CrossStageMapping;
	CrossStageMapping.StageClass = ELayoutChildPlacementStageClass::InheritedStage;
	CrossStageMapping.MappingId = TEXT("CrossStageCandidate");
	auto AddMappedCell = [&CrossStageMapping](const FIntVector Cell, const int32 Stage)
	{
		FLayoutChildStageMappedCell& MappedCell = CrossStageMapping.Cells.AddDefaulted_GetRef();
		MappedCell.SourceChildCell = Cell;
		MappedCell.MappedChildCell = Cell;
		MappedCell.ParentCell = Cell;
		MappedCell.ModuleLevelIndex = Cell.Z;
		MappedCell.TerrainStageIndex = Stage;
	};
	AddMappedCell(FIntVector(0, 0, 0), 0);
	AddMappedCell(FIntVector(1, 0, 0), 1);
	AddMappedCell(FIntVector(1, 0, 1), 1);
	AddMappedCell(FIntVector(2, 0, 1), 1);
	TArray<FLayoutPlannedCell> CrossStageCells;
	AddPlannedCell(CrossStageCells, FIntVector(0, 0, 0), 0);
	CrossStageCells.Last().Intent = ELayoutCellIntent::Entry;
	AddPlannedCell(CrossStageCells, FIntVector(1, 0, 0), 0);
	CrossStageCells.Last().Intent = ELayoutCellIntent::VerticalAccess;
	AddPlannedCell(CrossStageCells, FIntVector(1, 0, 1), 1);
	CrossStageCells.Last().Intent = ELayoutCellIntent::VerticalAccess;
	AddPlannedCell(CrossStageCells, FIntVector(2, 0, 1), 1);
	CrossStageCells.Last().Intent = ELayoutCellIntent::Entry;
	TArray<FLayoutCommittedEndpointAnchor> CrossStageCommitments;
	FLayoutCommittedEndpointAnchor& LowerAnchor = CrossStageCommitments.AddDefaulted_GetRef();
	LowerAnchor.LocalCell = FIntVector(0, 0, 0);
	LowerAnchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
	FLayoutCommittedEndpointAnchor& UpperAnchor = CrossStageCommitments.AddDefaulted_GetRef();
	UpperAnchor.LocalCell = FIntVector(2, 0, 1);
	UpperAnchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
	TArray<FIntVector> CrossStageRouteCells;
	FailureReason.Reset();
	TestTrue(
		TEXT("Committed route crossing mapped stages promotes through internal VerticalAccess"),
		LayoutProfileSolverInternal::TryPromoteChildStageMappingForCommittedRouteForTests(
			CrossStageMapping,
			CrossStageCommitments,
			CrossStageCells,
			{FIntVector(1, 0, 0), FIntVector(1, 0, 1)},
			CrossStageRouteCells,
			FailureReason));
	TestEqual(TEXT("Crossing route promotes mapping class"), CrossStageMapping.StageClass, ELayoutChildPlacementStageClass::CrossStage);
	TestTrue(TEXT("Cross-stage route includes internal VerticalAccess"), CrossStageRouteCells.Contains(FIntVector(1, 0, 0)));

	FailureReason.Reset();
	TestFalse(
		TEXT("Non-stepped child rejects inherited multi-stage geometry"),
		LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
			SteppedParent,
			SteppedParentCells,
			FIntPoint(2, 1),
			FIntVector::ZeroValue,
			TEXT("FlatRoom"),
			0,
			false,
			ChildCells,
			Mapping,
			FailureReason));
	TestTrue(TEXT("Non-stepped rejection names inherited stages"), FailureReason.Contains(TEXT("does not support Stepped")));

	return true;
}
