// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutTypes.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutParentEntrySettlementRelocatesChildBlockedEntryTest,
	"PorismExtension.Layout.Solver.ParentEntrySettlement.RelocatesChildBlockedEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutParentEntrySettlementRelocatesChildBlockedEntryTest::RunTest(const FString& Parameters)
{
	const FIntPoint FootprintSize(5, 5);
	TArray<FLayoutPlannedCell> ParentPlan;
	for (int32 Y = 0; Y < FootprintSize.Y; ++Y)
	{
		for (int32 X = 0; X < FootprintSize.X; ++X)
		{
			FLayoutPlannedCell& Cell = ParentPlan.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.Intent = X == 0 || Y == 0 || X == FootprintSize.X - 1 || Y == FootprintSize.Y - 1
				? ELayoutCellIntent::Boundary
				: ELayoutCellIntent::Interior;
		}
	}
	ParentPlan.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(4, 2, 0);
	})->Intent = ELayoutCellIntent::Entry;

	const TSet<FIntVector> ReservedCells = {
		FIntVector(3, 2, 0),
		FIntVector(4, 2, 0),
		FIntVector(3, 3, 0),
		FIntVector(4, 3, 0)};
	TArray<FLayoutPlannedCell> FirstResult;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Ordinary parent Entry settles outside child replacement"),
		LayoutProfileSolverInternal::TrySettleParentEntriesAroundChildReplacementForTests(
			ParentPlan,
			FootprintSize,
			ReservedCells,
			1337,
			true,
			FirstResult,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	TArray<FIntVector> SettledEntries;
	for (const FLayoutPlannedCell& Cell : FirstResult)
	{
		if (Cell.Intent == ELayoutCellIntent::Entry)
		{
			SettledEntries.Add(Cell.Cell);
		}
	}
	if (!TestEqual(TEXT("Entry count remains unchanged"), SettledEntries.Num(), 1))
	{
		return false;
	}
	TestFalse(TEXT("Settled Entry avoids child replacement"), ReservedCells.Contains(SettledEntries[0]));

	TArray<FLayoutPlannedCell> SecondResult;
	FString SecondFailureReason;
	if (!TestTrue(
		TEXT("Repeated settlement succeeds"),
		LayoutProfileSolverInternal::TrySettleParentEntriesAroundChildReplacementForTests(
			ParentPlan,
			FootprintSize,
			ReservedCells,
			1337,
			true,
			SecondResult,
			SecondFailureReason)))
	{
		AddError(SecondFailureReason);
		return false;
	}
	const FLayoutPlannedCell* RepeatedEntry = SecondResult.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry;
	});
	if (!TestTrue(TEXT("Repeated settlement retains one Entry"), RepeatedEntry != nullptr)
		|| !TestEqual(TEXT("Same seed retains settled Entry"), RepeatedEntry->Cell, SettledEntries[0]))
	{
		return false;
	}

	TArray<FLayoutPlannedCell> FixedEntryResult;
	FString FixedEntryFailureReason;
	TestFalse(
		TEXT("Continuation-owned Entry remains fixed instead of relocating around child replacement"),
		LayoutProfileSolverInternal::TrySettleParentEntriesAroundChildReplacementForTests(
			ParentPlan,
			FootprintSize,
			ReservedCells,
			1337,
			false,
			FixedEntryResult,
			FixedEntryFailureReason));
	return TestTrue(
		TEXT("Fixed Entry rejection retains exact overlap reason"),
		FixedEntryFailureReason.Contains(TEXT("protected parent")));

}
