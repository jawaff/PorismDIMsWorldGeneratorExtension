// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatPlanRejectsReservedOpenSpaceCapacityConflictTest,
	"PorismExtension.Layout.Solver.FlatPlan.RejectsReservedOpenSpaceCapacityConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatPlanAppliesAuthoredLevelFillAndReservedOpenSpaceTest,
	"PorismExtension.Layout.Solver.FlatPlan.AppliesAuthoredLevelFillAndReservedOpenSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFlatPlanPrewarmAvoidsZeroDomainBorderAdjacentVoidTest,
	"PorismExtension.Layout.Solver.FlatPlan.PrewarmAvoidsZeroDomainBorderAdjacentVoid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies protected structural cells cause an explicit hard-open capacity rejection. */
bool FLayoutFlatPlanRejectsReservedOpenSpaceCapacityConflictTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FlatPlanReservationConflict"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		1,
		false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = 1;
	Profile->MinEntryCount = 1;
	Profile->MaxEntryCount = 1;

	FLayoutReservedOpenSpaceRule& ReservedRule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	ReservedRule.RuleId = TEXT("RequiredHardOpen");
	ReservedRule.PlacementZone = ELayoutPlacementZone::Any;
	ReservedRule.MinReservedCells = 1;
	ReservedRule.MaxReservedCells = 1;

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 137);
	TestFalse(TEXT("Reserved open space rejects when protected Entry cells leave no capacity"), Result.bSucceeded);
	TestTrue(TEXT("Reservation capacity failure identifies its rule"), Result.FailureReason.Contains(ReservedRule.RuleId.ToString()));
	return true;
}

/** Verifies reserved-open prewarm skips voids that would empty a neighboring boundary domain. */
bool FLayoutFlatPlanPrewarmAvoidsZeroDomainBorderAdjacentVoidTest::RunTest(const FString& Parameters)
{
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	UChunkStructureTemplate* Template = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutReservationPrewarmTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* InteriorModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutReservationPrewarmInterior"),
		Template,
		{ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
		BuildFilledCubeFaces(
			OpenTags,
			OpenTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			OpenTags,
			OpenTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutModuleAsset* EdgeModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutReservationPrewarmEdge"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			OpenTags,
			OpenTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			OpenTags,
			OpenTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutModuleAsset* CornerModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutReservationPrewarmCorner"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceOpen, OpenTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});
	auto MakeEntry = [](const FName EntryId, ULayoutModuleAsset* Module, const ELayoutPlacementZone PlacementZone)
	{
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = EntryId;
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		Entry.ModuleSettings.PlacementZone = PlacementZone;
		return Entry;
	};
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ReservationPrewarm"),
		FIntPoint(7, 7),
		FIntPoint(7, 7),
		1,
		0,
		false);
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutReservationPrewarmContentSet"),
		{
			MakeEntry(TEXT("Interior"), InteriorModule, ELayoutPlacementZone::Interior),
			MakeEntry(TEXT("Edge"), EdgeModule, ELayoutPlacementZone::Edge),
			MakeEntry(TEXT("Corner"), CornerModule, ELayoutPlacementZone::Corner)
		});
	FLayoutReservedOpenSpaceRule& Rule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	Rule.RuleId = TEXT("AvoidZeroDomainBorderAdjacentVoid");
	Rule.PlacementZone = ELayoutPlacementZone::Interior;
	Rule.MinReservedCells = 1;
	Rule.MaxReservedCells = 1;

	for (int32 Seed = 0; Seed < 32; ++Seed)
	{
		const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, Seed);
		TestTrue(FString::Printf(TEXT("Seed %d selects a statically feasible hard-open cell"), Seed), Result.bSucceeded);
		if (!Result.bSucceeded)
		{
			AddError(Result.FailureReason);
			return false;
		}

		int32 MissingCellCount = 0;
		FIntVector MissingCell = FIntVector::ZeroValue;
		for (int32 Y = 0; Y < 7; ++Y)
		{
			for (int32 X = 0; X < 7; ++X)
			{
				const FIntVector CandidateCell(X, Y, 0);
				if (!Result.PlannedCells.ContainsByPredicate([CandidateCell](const FLayoutPlannedCell& PlannedCell)
				{
					return PlannedCell.Cell == CandidateCell;
				}))
				{
					++MissingCellCount;
					MissingCell = CandidateCell;
				}
			}
		}
		TestEqual(FString::Printf(TEXT("Seed %d produces one reserved hard-open cell"), Seed), MissingCellCount, 1);
		if (MissingCellCount == 1)
		{
			TestTrue(FString::Printf(TEXT("Seed %d avoids a void adjacent to a border Corner domain"), Seed),
				MissingCell.X > 1 && MissingCell.X < 5
				&& MissingCell.Y > 1 && MissingCell.Y < 5);
			const FLayoutSolveResult RepeatedResult = FLayoutProfileSolver::Solve(Profile, Seed);
			TestTrue(FString::Printf(TEXT("Seed %d repeats successfully"), Seed), RepeatedResult.bSucceeded);
			TestFalse(FString::Printf(TEXT("Seed %d repeats the same reserved hard-open cell"), Seed),
				RepeatedResult.PlannedCells.ContainsByPredicate([MissingCell](const FLayoutPlannedCell& PlannedCell)
				{
					return PlannedCell.Cell == MissingCell;
				}));
		}
	}

	Rule.MinReservedCells = 25;
	Rule.MaxReservedCells = 25;
	const FLayoutSolveResult ImpossibleResult = FLayoutProfileSolver::Solve(Profile, 0);
	TestFalse(TEXT("An impossible hard-open mask rejects during prewarm"), ImpossibleResult.bSucceeded);
	TestTrue(TEXT("Prewarm rejection identifies its reserved-open rule"), ImpossibleResult.FailureReason.Contains(Rule.RuleId.ToString()));
	TestTrue(TEXT("Prewarm rejection identifies the first rejected candidate"), ImpossibleResult.FailureReason.Contains(TEXT("First rejected cell=")));
	TestTrue(TEXT("Prewarm rejection identifies the first unsupported domain"), ImpossibleResult.FailureReason.Contains(TEXT("first unsupported domain=")));
	TestFalse(TEXT("Prewarm rejection occurs before prepared CSP-domain failure"), ImpossibleResult.FailureReason.Contains(TEXT("Prepared solve has no initial candidates")));
	return true;
}

/** Verifies flat authored planning freezes level fill and deterministic hard-open removal before solving. */
bool FLayoutFlatPlanAppliesAuthoredLevelFillAndReservedOpenSpaceTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FlatPlanFillAndReservation"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		3,
		0,
		false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;

	FLayoutLevelFillRule& TopLevelRule = Profile->LevelFillRules.AddDefaulted_GetRef();
	TopLevelRule.RuleId = TEXT("TopBoundaryOnly");
	TopLevelRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;
	TopLevelRule.FillMode = ELayoutLevelFillMode::BoundaryOnly;

	FLayoutReservedOpenSpaceRule& ReservedRule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	ReservedRule.RuleId = TEXT("GroundHardOpen");
	ReservedRule.PlacementZone = ELayoutPlacementZone::Any;
	ReservedRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	ReservedRule.MinReservedCells = 1;
	ReservedRule.MaxReservedCells = 1;
	ReservedRule.TerrainBehavior = ELayoutReservedOpenTerrainBehavior::ClearReservedCell;

	const FLayoutSolveResult FirstResult = FLayoutProfileSolver::Solve(Profile, 131);
	const FLayoutSolveResult SecondResult = FLayoutProfileSolver::Solve(Profile, 131);
	if (!FirstResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Flat authored plan solve failed: %s"), *FirstResult.FailureReason));
		return false;
	}

	TestEqual(TEXT("Flat plan keeps two full authored levels and one boundary-only top level minus one hard-open cell"), FirstResult.PlannedCells.Num(), 25);
	TestFalse(TEXT("Boundary-only top level removes its interior cell"), FirstResult.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 1, 2);
	}));
	TestEqual(TEXT("Repeated seed keeps flat planned-cell count"), SecondResult.PlannedCells.Num(), FirstResult.PlannedCells.Num());
	const FLayoutCellReservationRecord* FirstReservation = FirstResult.CompiledReservations.FindByPredicate([](const FLayoutCellReservationRecord& Reservation)
	{
		return Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty;
	});
	const FLayoutCellReservationRecord* SecondReservation = SecondResult.CompiledReservations.FindByPredicate([](const FLayoutCellReservationRecord& Reservation)
	{
		return Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty;
	});
	TestNotNull(TEXT("Flat plan preserves selected reserved-open record"), FirstReservation);
	TestNotNull(TEXT("Repeated seed preserves selected reserved-open record"), SecondReservation);
	if (FirstReservation != nullptr && SecondReservation != nullptr)
	{
		TestEqual(TEXT("Reserved-open record retains ClearReservedCell behavior"), FirstReservation->TerrainBehavior, ELayoutReservedOpenTerrainBehavior::ClearReservedCell);
		TestEqual(TEXT("Repeated seed keeps reserved-open identity"), SecondReservation->ReservationId, FirstReservation->ReservationId);
		TestEqual(TEXT("Repeated seed keeps reserved-open cell"), SecondReservation->Cell, FirstReservation->Cell);
		TestFalse(TEXT("Reserved-open record cell remains removed from plan"), FirstResult.PlannedCells.ContainsByPredicate([FirstReservation](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FirstReservation->Cell;
		}));
	}
	for (int32 Index = 0; Index < FirstResult.PlannedCells.Num() && Index < SecondResult.PlannedCells.Num(); ++Index)
	{
		const FLayoutPlannedCell& FirstCell = FirstResult.PlannedCells[Index];
		const FLayoutPlannedCell& SecondCell = SecondResult.PlannedCells[Index];
		TestEqual(FString::Printf(TEXT("Flat plan preserves deterministic cell %d"), Index), FirstCell.Cell, SecondCell.Cell);
		TestEqual(FString::Printf(TEXT("Flat plan preserves deterministic intent %d"), Index), FirstCell.Intent, SecondCell.Intent);
		TestEqual(FString::Printf(TEXT("Flat cell %d preserves authored module level"), Index), FirstCell.ModuleLevelIndex, FirstCell.Cell.Z);
	}
	return true;
}
