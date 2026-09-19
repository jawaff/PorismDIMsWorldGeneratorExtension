// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutProfileSolver.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;
	/** Builds a full continuation deck with one edge-to-interior terrain-seam component. */
	TArray<FLayoutPlannedCell> BuildContinuationTerrainSeamPlan()
	{
		TArray<FLayoutPlannedCell> PlannedCells;
		for (int32 Y = 0; Y < 5; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, 0);
				Cell.Intent = ELayoutCellIntent::Connector;
				if (Y == 2 && X <= 2)
				{
					Cell.TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosY);
				}
			}
		}

		for (int32 X = 0; X <= 2; ++X)
		{
			FLayoutPlannedCell& UpperLanding = PlannedCells.AddDefaulted_GetRef();
			UpperLanding.Cell = FIntVector(X, 2, 1);
			UpperLanding.Intent = ELayoutCellIntent::Connector;
			UpperLanding.bIsBridgeCell = true;
		}

		// The first endpoint lies on the seam component's perimeter. The second
		// endpoint remains a separate continuation contract outside that component.
		for (FLayoutPlannedCell& Cell : PlannedCells)
		{
			if (Cell.Cell == FIntVector(0, 2, 0)
				|| Cell.Cell == FIntVector(4, 2, 0))
			{
				Cell.Intent = ELayoutCellIntent::Entry;
				Cell.EntryOrigin = ELayoutEntryOrigin::None;
			}
		}
		return PlannedCells;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutContinuationTerrainSeamEntryPrefersInteriorTest,
	"PorismExtension.Layout.Solver.Continuation.TerrainSeamEntryPrefersInterior",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContinuationTerrainSeamEntryPrefersInteriorTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutContinuationTerrainSeamProfile"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bEnableTerrainSeams = true;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		917,
		TEXT("ContinuationTerrainSeamFixture"));
	Request.FootprintSize = FIntPoint(5, 5);
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;

	TArray<FLayoutPlannedCell> PlannedCells = BuildContinuationTerrainSeamPlan();
	FString FailureReason;
	const bool bFinalized = FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
		PlannedCells,
		Request,
		FailureReason);
	TestTrue(*FString::Printf(TEXT("Continuation terrain-seam finalization succeeds: %s"), *FailureReason), bFinalized);

	auto CollectEntryState = [&PlannedCells](TArray<const FLayoutPlannedCell*>& OutTerrainSeamEntries, int32& OutEntryCount)
	{
		OutTerrainSeamEntries.Reset();
		OutEntryCount = 0;
		for (const FLayoutPlannedCell& Cell : PlannedCells)
		{
			if (Cell.Intent == ELayoutCellIntent::Entry)
			{
				++OutEntryCount;
			}
			if (Cell.Intent == ELayoutCellIntent::Entry
				&& Cell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam)
			{
				OutTerrainSeamEntries.Add(&Cell);
			}
		}
	};

	const FLayoutPlannedCell* StartEndpoint = PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(0, 2, 0);
	});
	const FLayoutPlannedCell* EndEndpoint = PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(4, 2, 0);
	});
	TestNotNull(TEXT("Continuation start endpoint exists"), StartEndpoint);
	TestNotNull(TEXT("Continuation end endpoint exists"), EndEndpoint);
	if (StartEndpoint != nullptr && EndEndpoint != nullptr)
	{
		TestEqual(TEXT("Continuation start endpoint remains Entry"), StartEndpoint->Intent, ELayoutCellIntent::Entry);
		TestEqual(TEXT("Continuation end endpoint remains Entry"), EndEndpoint->Intent, ELayoutCellIntent::Entry);
		TestEqual(TEXT("Continuation start endpoint is not terrain-seam-owned"),
			StartEndpoint->EntryOrigin,
			ELayoutEntryOrigin::None);
		TestEqual(TEXT("Continuation end endpoint is not terrain-seam-owned"),
			EndEndpoint->EntryOrigin,
			ELayoutEntryOrigin::None);
	}

	TArray<const FLayoutPlannedCell*> TerrainSeamEntries;
	int32 EntryCount = 0;
	CollectEntryState(TerrainSeamEntries, EntryCount);
	TestEqual(TEXT("Two endpoints plus one terrain-seam gate are Entries"), EntryCount, 3);
	TestEqual(TEXT("One Entry is selected for the connected terrain-seam component"), TerrainSeamEntries.Num(), 1);
	if (TerrainSeamEntries.Num() == 1)
	{
		TestEqual(TEXT("Interior terrain seam outranks perimeter seam"), TerrainSeamEntries[0]->Cell, FIntVector(1, 2, 0));
		TestTrue(TEXT("Terrain-seam gate retains its seam face"), TerrainSeamEntries[0]->TerrainSeamFaceMask != 0);
		TestEqual(TEXT("Terrain seam Entry retains its source classification"),
			TerrainSeamEntries[0]->EntryOrigin,
			ELayoutEntryOrigin::TerrainSeam);
	}

	FString RepeatFailureReason;
	const bool bRefinalized = FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
		PlannedCells,
		Request,
		RepeatFailureReason);
	TestTrue(*FString::Printf(TEXT("Repeated continuation terrain-seam finalization succeeds: %s"), *RepeatFailureReason), bRefinalized);
	CollectEntryState(TerrainSeamEntries, EntryCount);
	TestEqual(TEXT("Repeated finalization keeps two endpoints plus one terrain-seam gate"), EntryCount, 3);
	TestEqual(TEXT("Repeated finalization keeps one terrain-seam gate"), TerrainSeamEntries.Num(), 1);
	if (TerrainSeamEntries.Num() == 1)
	{
		TestEqual(TEXT("Repeated finalization keeps the same interior terrain-seam gate"),
			TerrainSeamEntries[0]->Cell,
			FIntVector(1, 2, 0));
	}

	FLayoutRegionSolveRequest DisabledRequest = Request;
	DisabledRequest.ProfileSnapshot.bEnableTerrainSeams = false;
	TArray<FLayoutPlannedCell> DisabledCells = BuildContinuationTerrainSeamPlan();
	FString DisabledFailureReason;
	TestTrue(*FString::Printf(TEXT("Disabled terrain-seam finalization succeeds: %s"), *DisabledFailureReason),
		FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
			DisabledCells,
			DisabledRequest,
			DisabledFailureReason));
	int32 DisabledEntryCount = 0;
	for (const FLayoutPlannedCell& Cell : DisabledCells)
	{
		DisabledEntryCount += Cell.Intent == ELayoutCellIntent::Entry ? 1 : 0;
		TestEqual(TEXT("Disabled continuation clears terrain seam face mask"), Cell.TerrainSeamFaceMask, uint8(0));
		TestEqual(TEXT("Disabled continuation clears terrain seam entry origin"), Cell.EntryOrigin, ELayoutEntryOrigin::None);
	}
	TestEqual(TEXT("Disabled continuation retains only endpoint Entries"), DisabledEntryCount, 2);
	return true;
}
