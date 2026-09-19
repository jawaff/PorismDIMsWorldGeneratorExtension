// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Algo/Count.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningRejectsExactCountBeyondSelectedFootprintCapacityTest,
	"PorismExtension.Layout.Solver.EntryPlanning.RejectsExactCountBeyondSelectedFootprintCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningRequiresCatalogSupportedCornerTest,
	"PorismExtension.Layout.Solver.EntryPlanning.RequiresCatalogSupportedCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningRejectsVoidAdjacentSuppliedEntryTest,
	"PorismExtension.Layout.Solver.EntryPlanning.RejectsVoidAdjacentSuppliedEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningFinalizesDeterministicNonCornerEntriesTest,
	"PorismExtension.Layout.Solver.EntryPlanning.FinalizesDeterministicNonCornerEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningRejectsFinalStaticCapacityTest,
	"PorismExtension.Layout.Solver.EntryPlanning.RejectsFinalStaticCapacity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningDefersChildExternalFaceContractsTest,
	"PorismExtension.Layout.Solver.EntryPlanning.DefersChildExternalFaceContracts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningParentDomainUsesChildPlannedZoneTest,
	"PorismExtension.Layout.Solver.EntryPlanning.ParentDomainUsesChildPlannedZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningContinuationDoesNotSatisfyAuthoredCountTest,
	"PorismExtension.Layout.Solver.EntryPlanning.ContinuationDoesNotSatisfyAuthoredCount",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryPlanningContinuationScopeBypassesAuthoredCountAuditTest,
	"PorismExtension.Layout.Solver.EntryPlanning.ContinuationScopeBypassesAuthoredCountAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies exact exterior Entry counts reject against selected-footprint capacity instead of silently clamping. */
bool FLayoutEntryPlanningRejectsExactCountBeyondSelectedFootprintCapacityTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_EntryExactCapacity"), FIntPoint(3, 3), FIntPoint(5, 5), 1, 1, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = 10;
	Profile->MinEntryCount = 10;
	Profile->MaxEntryCount = 10;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 901);
	Request.FootprintSize = FIntPoint(3, 3);
	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;

	TestFalse(TEXT("Exact exterior Entry count rejects when selected footprint has insufficient boundary cells"), Result.bSucceeded);
	TestTrue(FString::Printf(TEXT("Entry capacity rejection reports exact requested count: %s"), *Result.FailureReason), Result.FailureReason.Contains(TEXT("requires exactly 10 exterior Entry cells")));
	TestTrue(TEXT("Entry capacity rejection reports selected-footprint capacity"), Result.FailureReason.Contains(TEXT("only 8 boundary cells")));
	return true;
}

/** Verifies exterior Entry planning uses immutable catalog Corner support rather than raw perimeter capacity. */
bool FLayoutEntryPlanningRequiresCatalogSupportedCornerTest::RunTest(const FString& Parameters)
{
	auto ConfigureExactOneEntry = [](ULayoutProfileAsset* Profile)
	{
		Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
		Profile->EntryCount = 1;
		Profile->MinEntryCount = 1;
		Profile->MaxEntryCount = 1;
	};

	ULayoutProfileAsset* EdgeOnlyProfile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_EdgeOnlyEntry"), FIntPoint(2, 2), FIntPoint(2, 2), 1, 1, false);
	ConfigureExactOneEntry(EdgeOnlyProfile);
	for (FLayoutRegionContentEntry& Entry : EdgeOnlyProfile->ContentSet->Entries)
	{
		Entry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;
	}
	const FLayoutSolveResult EdgeOnlyResult = FLayoutProfileSolver::Solve(EdgeOnlyProfile, 903);
	TestFalse(TEXT("Edge-only catalog rejects a 2x2 exterior Entry request before CSP"), EdgeOnlyResult.bSucceeded);
	TestTrue(FString::Printf(TEXT("Corner-support rejection reports catalog-supported capacity: %s"), *EdgeOnlyResult.FailureReason), EdgeOnlyResult.FailureReason.Contains(TEXT("only 0 catalog-supported exterior Entry cells")));

	ULayoutProfileAsset* CornerProfile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_CornerEntry"), FIntPoint(2, 2), FIntPoint(2, 2), 1, 1, false);
	ConfigureExactOneEntry(CornerProfile);
	CornerProfile->ContentSet->Entries[0].ModuleSettings.PlacementZone = ELayoutPlacementZone::Corner;
	CornerProfile->ContentSet->Entries[1].ModuleSettings.PlacementZone = ELayoutPlacementZone::Corner;
	CornerProfile->ContentSet->Entries[2].ModuleSettings.PlacementZone = ELayoutPlacementZone::Corner;
	const FLayoutSolveResult CornerResult = FLayoutProfileSolver::Solve(CornerProfile, 907);
	TestTrue(TEXT("Catalog-supported Corner Entry solves on a 2x2 footprint"), CornerResult.bSucceeded);
	TestEqual(TEXT("Catalog-supported Corner Entry produces one exterior Entry"),
		static_cast<int32>(Algo::CountIf(CornerResult.PlannedCells, [](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::Entry;
		})), 1);
	return true;
}

/** Verifies an Entry cell cannot fall back to Boundary content after final topology adds a void face. */
bool FLayoutEntryPlanningRejectsVoidAdjacentSuppliedEntryTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_EntryVoidAdmission"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 0, false);
	ULayoutModuleAsset* const WallModule = Profile->ContentSet->Entries[0].ModuleSettings.Module.Get();
	ULayoutModuleAsset* const EntryModule = Profile->ContentSet->Entries[1].ModuleSettings.Module.Get();
	WallModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};
	EntryModule->Roles = {ELayoutModuleRole::Entry};
	EntryModule->FaceRules.PosX = MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
	for (FLayoutFaceRule* Face : {&EntryModule->FaceRules.NegX, &EntryModule->FaceRules.PosY, &EntryModule->FaceRules.NegY})
	{
		*Face = MakeConnectionFaceRule(Face->Direction, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
	}
	EntryModule->NormalizeFaceRuleDirections();

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 911);
	Request.PlannedCells.Reset();
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			if (X == 1 && Y == 2) continue;
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = X == 0 && Y == 2 ? ELayoutCellIntent::Entry : ELayoutCellIntent::Boundary;
		}
	}
	const FLayoutSolveResult Rejected = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	TestFalse(TEXT("Void-adjacent Entry rejects"), Rejected.bSucceeded);

	for (FLayoutPlannedCell& Cell : Request.PlannedCells)
	{
		if (Cell.Cell == FIntVector(0, 2, 0)) Cell.Intent = ELayoutCellIntent::Boundary;
		else if (Cell.Cell == FIntVector(4, 2, 0)) Cell.Intent = ELayoutCellIntent::Entry;
	}
	const FLayoutSolveResult Alternate = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	TestTrue(FString::Printf(TEXT("Alternate Entry slot solves: %s"), *Alternate.FailureReason), Alternate.bSucceeded);
	return true;
}

/** Verifies flat finalization preserves deterministic non-corner authored exterior Entries. */
bool FLayoutEntryPlanningFinalizesDeterministicNonCornerEntriesTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_FinalizedEntries"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = 2;
	const FLayoutSolveResult First = FLayoutProfileSolver::Solve(Profile, 919);
	const FLayoutSolveResult Repeat = FLayoutProfileSolver::Solve(Profile, 919);
	TestTrue(FString::Printf(TEXT("First finalized solve succeeds: %s"), *First.FailureReason), First.bSucceeded);
	TestTrue(FString::Printf(TEXT("Repeated finalized solve succeeds: %s"), *Repeat.FailureReason), Repeat.bSucceeded);
	TArray<FIntVector> FirstEntries;
	TArray<FIntVector> RepeatEntries;
	for (const FLayoutPlannedCell& Cell : First.PlannedCells)
	{
		if (Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary)
		{
			FirstEntries.Add(Cell.Cell);
			TestFalse(TEXT("Finalized Entry avoids a Corner while Edge slots exist"), (Cell.Cell.X == 0 || Cell.Cell.X == 4) && (Cell.Cell.Y == 0 || Cell.Cell.Y == 4));
		}
	}
	for (const FLayoutPlannedCell& Cell : Repeat.PlannedCells)
	{
		if (Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary) RepeatEntries.Add(Cell.Cell);
	}
	const auto SortCells = [](TArray<FIntVector>& Cells)
	{
		Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z) return Left.Z < Right.Z;
			if (Left.Y != Right.Y) return Left.Y < Right.Y;
			return Left.X < Right.X;
		});
	};
	SortCells(FirstEntries);
	SortCells(RepeatEntries);
	TestEqual(TEXT("Finalizer produces exact authored exterior count"), FirstEntries.Num(), 2);
	if (FirstEntries.Num() == 2)
	{
		const int32 Separation = FMath::Abs(FirstEntries[0].X - FirstEntries[1].X)
			+ FMath::Abs(FirstEntries[0].Y - FirstEntries[1].Y);
		TestTrue(TEXT("Finalizer separates multiple authored Entries when alternate edge slots exist"), Separation > 1);
	}
	TestTrue(TEXT("Finalizer repeats same authored Entry cells for same seed"), FirstEntries == RepeatEntries);

	for (const int32 RequestedEntryCount : {3, 4})
	{
		Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = RequestedEntryCount;
		const FLayoutSolveResult SideCoverageResult = FLayoutProfileSolver::Solve(Profile, 930 + RequestedEntryCount);
		TSet<int32> CoveredSides;
		for (const FLayoutPlannedCell& Cell : SideCoverageResult.PlannedCells)
		{
			if (Cell.Intent != ELayoutCellIntent::Entry || Cell.EntryOrigin != ELayoutEntryOrigin::AuthoredBoundary)
			{
				continue;
			}
			if (Cell.Cell.X == 0) { CoveredSides.Add(0); }
			else if (Cell.Cell.X == 4) { CoveredSides.Add(1); }
			else if (Cell.Cell.Y == 0) { CoveredSides.Add(2); }
			else if (Cell.Cell.Y == 4) { CoveredSides.Add(3); }
		}
		TestTrue(
			FString::Printf(TEXT("Exact %d Entry solve succeeds: %s"), RequestedEntryCount, *SideCoverageResult.FailureReason),
			SideCoverageResult.bSucceeded);
		TestEqual(
			FString::Printf(TEXT("Exact %d Entries cover separate cardinal sides"), RequestedEntryCount),
			CoveredSides.Num(),
			RequestedEntryCount);
	}
	Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = 2;

	Profile->bSupportsSteppedTerrainSolve = true;
	const FLayoutSolveResult SteppedCapableFlat = FLayoutProfileSolver::Solve(Profile, 919);
	TestTrue(FString::Printf(TEXT("Flat plan on stepped-capable profile still finalizes: %s"), *SteppedCapableFlat.FailureReason), SteppedCapableFlat.bSucceeded);
	TestEqual(TEXT("Flat stepped-capable profile retains exact authored Entries"), static_cast<int32>(Algo::CountIf(SteppedCapableFlat.PlannedCells, [](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary;
	})), 2);
	Profile->bSupportsSteppedTerrainSolve = false;

	Profile->EntryCountMode = ELayoutCountConstraintMode::Range;
	Profile->MinEntryCount = 1;
	Profile->MaxEntryCount = 3;
	for (int32 Seed = 920; Seed < 925; ++Seed)
	{
		const FLayoutSolveResult Ranged = FLayoutProfileSolver::Solve(Profile, Seed);
		const int32 RangedCount = Algo::CountIf(Ranged.PlannedCells, [](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary;
		});
		TestTrue(FString::Printf(TEXT("Range Entry solve %d succeeds: %s"), Seed, *Ranged.FailureReason), Ranged.bSucceeded);
		TestTrue(FString::Printf(TEXT("Range Entry count %d stays in bounds"), Seed), RangedCount >= 1 && RangedCount <= 3);
	}

	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	const FLayoutSolveResult NoneResult = FLayoutProfileSolver::Solve(Profile, 925);
	TestTrue(FString::Printf(TEXT("None Entry solve succeeds: %s"), *NoneResult.FailureReason), NoneResult.bSucceeded);
	TestEqual(TEXT("None Entry mode creates no authored exterior Entries"), static_cast<int32>(Algo::CountIf(NoneResult.PlannedCells, [](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary;
	})), 0);
	return true;
}

/** Verifies raw catalog support cannot satisfy an exact count when final face admission rejects every slot. */
bool FLayoutEntryPlanningRejectsFinalStaticCapacityTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_FinalStaticCapacity"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = 1;
	ULayoutModuleAsset* const WallModule = Profile->ContentSet->Entries[0].ModuleSettings.Module.Get();
	ULayoutModuleAsset* const EntryModule = Profile->ContentSet->Entries[1].ModuleSettings.Module.Get();
	ULayoutModuleAsset* const VerticalModule = Profile->ContentSet->Entries[2].ModuleSettings.Module.Get();
	WallModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};
	VerticalModule->Roles = {ELayoutModuleRole::VerticalAccess};
	EntryModule->Roles = {ELayoutModuleRole::Entry};
	Profile->ContentSet->Entries[1].ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;
	EntryModule->FaceRules.PosX = MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
	EntryModule->FaceRules.PosY = MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
	EntryModule->FaceRules.NegX = MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
	EntryModule->FaceRules.NegY = MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
	EntryModule->NormalizeFaceRuleDirections();

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 929);
	TestFalse(TEXT("Final static Entry capacity rejects exact count"), Result.bSucceeded);
	TestTrue(FString::Printf(TEXT("Final static Entry capacity reports feasible slots: %s"), *Result.FailureReason), Result.FailureReason.Contains(TEXT("finalized flat plan has 0 Edge and 0 Corner slots")));
	TestTrue(TEXT("Final static Entry capacity reports the relevant Edge-slot rejection"), Result.FailureReason.Contains(TEXT("First Edge rejection=")));
	TestTrue(TEXT("Final static Entry capacity reports the selected footprint"), Result.FailureReason.Contains(TEXT("Footprint=5x5")));
	TestEqual(TEXT("Final static Entry capacity is a typed topology preparation failure"),
		Result.PreparationFailureKind,
		ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible);

	Profile->ContentSet->Entries[1].ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
	const FLayoutSolveResult CornerResult = FLayoutProfileSolver::Solve(Profile, 929);
	TestTrue(FString::Printf(TEXT("Corner fallback solves after catalog enables Corner: %s"), *CornerResult.FailureReason), CornerResult.bSucceeded);
	TestEqual(TEXT("Final static fallback selects one Corner Entry"), static_cast<int32>(Algo::CountIf(CornerResult.PlannedCells, [](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry
			&& Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary
			&& (Cell.Cell.X == 0 || Cell.Cell.X == 4)
			&& (Cell.Cell.Y == 0 || Cell.Cell.Y == 4);
	})), 1);
	return true;
}

/** Verifies child-intrinsic preparation preserves strict external faces for later transform certification. */
bool FLayoutEntryPlanningDefersChildExternalFaceContractsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_DeferredChildBoundary"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 1, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = 1;

	ULayoutModuleAsset* const BoundaryModule = Profile->ContentSet->Entries[0].ModuleSettings.Module.Get();
	BoundaryModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};
	Profile->ContentSet->Entries[0].ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
	for (FLayoutFaceRule* Face : {
		&BoundaryModule->FaceRules.PosX,
		&BoundaryModule->FaceRules.NegX,
		&BoundaryModule->FaceRules.PosY,
		&BoundaryModule->FaceRules.NegY})
	{
		*Face = MakeConnectionFaceRule(
			Face->Direction,
			LayoutGameplayTags::FaceOpen,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}));
		Face->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceInterior;
	}
	BoundaryModule->NormalizeFaceRuleDirections();

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 929);
	Request.FootprintSize = FIntPoint(3, 3);
	const FLayoutRegionSolveResult OrdinaryPrepared =
		LayoutProfileSolverInternal::BuildRequestBackedRegionPreparedTopology(Request);
	TestFalse(TEXT("Ordinary standalone preparation enforces open exterior faces"), OrdinaryPrepared.SolveResult.bSucceeded);

	const FLayoutRegionSolveResult ChildPrepared =
		LayoutProfileSolverInternal::BuildRequestBackedChildIntrinsicPreparedTopology(Request);
	TestTrue(FString::Printf(TEXT("Child-intrinsic preparation defers transform-owned exterior faces: %s"), *ChildPrepared.SolveResult.FailureReason), ChildPrepared.SolveResult.bSucceeded);
	TestEqual(TEXT("Child-intrinsic preparation preserves exact authored Entry count"),
		static_cast<int32>(Algo::CountIf(ChildPrepared.SolveResult.PlannedCells, [](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::Entry
				&& Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary;
		})),
		1);
	return true;
}

/** Verifies parent-domain admission cannot satisfy a Corner child cell with an Edge-only module. */
bool FLayoutEntryPlanningParentDomainUsesChildPlannedZoneTest::RunTest(const FString& Parameters)
{
	FLayoutRegionContentSetSolveSnapshot ChildContentSet;
	FLayoutModuleCatalog ChildCatalog;
	ChildCatalog.Modules.SetNum(2);
	ChildContentSet.Entries.SetNum(2);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FLayoutModuleSolveSnapshot& Module = ChildCatalog.Modules[Index];
		Module.SnapshotId = FLayoutId(*FString::Printf(TEXT("ChildBoundary.%d"), Index));
		Module.SupportedCellIntents = {ELayoutCellIntent::Boundary};
		Module.AllowedYawRotationSteps = {0};
		FLayoutRegionContentEntrySolveSnapshot& Entry = ChildContentSet.Entries[Index];
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSnapshotIndex = Index;
		Entry.ModulePlacementZone = Index == 0
			? ELayoutPlacementZone::Edge
			: ELayoutPlacementZone::Corner;
	}

	const FLayoutFaceRule CompatibleChildFace = MakeConnectionFaceRule(
		ELayoutFaceDirection::NegX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor);
	ChildCatalog.Modules[0].EffectiveFaceRules.SetRule(CompatibleChildFace);
	FLayoutFaceRule IncompatibleCornerFace = CompatibleChildFace;
	IncompatibleCornerFace.ConnectionTag = LayoutGameplayTags::FaceSolid;
	IncompatibleCornerFace.AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceSolid});
	ChildCatalog.Modules[1].EffectiveFaceRules.SetRule(IncompatibleCornerFace);

	FLayoutFaceRule ParentFace = MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor);
	TMap<ELayoutFaceDirection, FLayoutFaceRule> ParentFaces;
	ParentFaces.Add(ELayoutFaceDirection::PosX, ParentFace);
	FLayoutPlannedCell ChildCornerCell;
	ChildCornerCell.Cell = FIntVector::ZeroValue;
	ChildCornerCell.Intent = ELayoutCellIntent::Boundary;
	ChildCornerCell.PlacementZone = ELayoutPlacementZone::Corner;
	ChildCornerCell.ModuleLevelIndex = 0;

	TestFalse(TEXT("Edge-only compatible module cannot certify a Corner child cell"),
		LayoutProfileSolverInternal::HasCompatibleChildBoundaryModule(
			ChildContentSet,
			ChildCatalog,
			ChildCornerCell,
			0,
			ParentFaces));

	ChildCatalog.Modules[1].EffectiveFaceRules.SetRule(CompatibleChildFace);
	TestTrue(TEXT("Compatible Corner module certifies the same parent face domain"),
		LayoutProfileSolverInternal::HasCompatibleChildBoundaryModule(
			ChildContentSet,
			ChildCatalog,
			ChildCornerCell,
			0,
			ParentFaces));
	return true;
}

/** Verifies continuation-owned Entries cannot satisfy active authored exterior count. */
bool FLayoutEntryPlanningContinuationDoesNotSatisfyAuthoredCountTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_ContinuationEntryCount"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 1, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = 1;
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 937);
	Request.SourceParentRegionDebugPath = TEXT("Parent/Continuation");
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Request.PlannedCells.Reset();
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = X == 0 && Y == 1 ? ELayoutCellIntent::Entry : ELayoutCellIntent::Boundary;
			Cell.EntryOrigin = Cell.Intent == ELayoutCellIntent::Entry ? ELayoutEntryOrigin::Continuation : ELayoutEntryOrigin::None;
		}
	}
	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	TestFalse(TEXT("Continuation Entry cannot satisfy exact authored exterior count"), Result.bSucceeded);
	TestTrue(TEXT("Authored count audit excludes continuation origin"), Result.FailureReason.Contains(TEXT("expected 1, found authored=0 childContract=0")));
	TestEqual(TEXT("Continuation does not satisfy authored exterior count"), static_cast<int32>(Algo::CountIf(Result.PlannedCells, [](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary;
	})), 0);
	TestEqual(TEXT("Continuation origin remains present"), static_cast<int32>(Algo::CountIf(Result.PlannedCells, [](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::Continuation;
	})), 1);
	return true;
}

/** Continuation scope bypasses root Entry counts while preserving strict endpoint-route enforcement. */
bool FLayoutEntryPlanningContinuationScopeBypassesAuthoredCountAuditTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(), TEXT("LayoutProfile_ContinuationScopeEntryCount"), FIntPoint(3, 3), FIntPoint(3, 3), 1, 1, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = Profile->MinEntryCount = Profile->MaxEntryCount = 2;
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 941);
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan.Scope = ELayoutContractRegionScope::Continuation;
	Request.SelectedModePlan.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	Request.PlannedCells.Reset();
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = (X == 0 && Y == 1) || (X == 2 && Y == 1)
				? ELayoutCellIntent::Entry
				: ELayoutCellIntent::Boundary;
			Cell.EntryOrigin = Cell.Intent == ELayoutCellIntent::Entry
				? ELayoutEntryOrigin::Continuation
				: ELayoutEntryOrigin::None;
		}
	}
	const FLayoutIndexedDomainSnapshot PreparationSnapshot =
		FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	// Universal horizontal faces connect this filled footprint. The old negative
	// expectation described an obsolete route-planner rejection, not blocked content.
	TestTrue(FString::Printf(TEXT("Connected continuation prepares: %s"), *PreparationSnapshot.FailureReason),
		PreparationSnapshot.bSucceeded);
	TestEqual(TEXT("Connected preparation has no topology failure"),
		PreparationSnapshot.PreparationFailureKind, ELayoutSolvePreparationFailureKind::None);

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	TestFalse(
		TEXT("Continuation scope never fails root-only authored Entry audit"),
		Result.FailureReason.Contains(TEXT("Authored exterior Entry count mismatch")));
	TestTrue(FString::Printf(TEXT("Connected continuation solves: %s"), *Result.FailureReason), Result.bSucceeded);
	TestTrue(TEXT("Connected continuation retains required route claims"), !Result.RouteConstraints.IsEmpty());
	TestEqual(TEXT("Both supplied endpoints retain continuation origin"), static_cast<int32>(Algo::CountIf(
		Result.PlannedCells, [](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::Entry && Cell.EntryOrigin == ELayoutEntryOrigin::Continuation;
		})), 2);

	// Keep topology and endpoints unchanged, but force a non-traversable middle
	// column. This tests real disconnection rather than an old preparation stage.
	TArray<FLayoutFaceRule> BarrierFaces;
	for (int32 Direction = 0; Direction < 6; ++Direction)
	{
		BarrierFaces.Add(MakeConnectionFaceRule(static_cast<ELayoutFaceDirection>(Direction),
			LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}
	auto* BarrierTemplate = CreateTemplate(GetTransientPackage(), TEXT("ContinuationBarrierTemplate"), FIntVector(16, 16, 16));
	auto* BarrierModule = CreateModule(GetTransientPackage(), TEXT("ContinuationBarrier"), BarrierTemplate,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior}, BarrierFaces);
	auto BlockedRequest = Request;
	// The shared fixture defaults to optional reachability. A disconnected-route
	// negative must request strict reachability rather than assume that default.
	BlockedRequest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	const auto Barrier = FLayoutProfileSolver::BuildModuleSnapshot(BarrierModule, 1);
	BlockedRequest.ModuleCatalog.Modules.Add(Barrier);
	BlockedRequest.CandidateDomainCertificateId = TEXT("ContinuationBarrier");
	for (int32 Y = 0; Y < 3; ++Y)
	{
		auto& Restriction = BlockedRequest.CandidateDomainRestrictions.AddDefaulted_GetRef();
		Restriction.Cell = FIntVector(1, Y, 0);
		Restriction.RestrictionId = FLayoutId(*FString::Printf(TEXT("ContinuationBarrier.%d"), Y));
		auto& Candidate = Restriction.AllowedCandidates.AddDefaulted_GetRef();
		Candidate.ModuleSnapshotId = Barrier.SnapshotId;
		Candidate.YawRotationSteps = 0;
	}
	const auto BlockedPreparation = FLayoutProfileSolver::BuildIndexedDomainSnapshot(BlockedRequest);
	TestFalse(TEXT("Blocked continuation rejects before indexed CSP"), BlockedPreparation.bSucceeded);
	TestEqual(TEXT("Blocked preparation preserves typed topology failure"),
		BlockedPreparation.PreparationFailureKind, ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible);
	TestEqual(TEXT("Disconnected preparation performs zero candidate attempts"), BlockedPreparation.CandidateAttemptCount, 0);
	const auto Blocked = FLayoutProfileSolver::SolveRegion(BlockedRequest).SolveResult;
	TestFalse(TEXT("Continuation scope cannot bypass disconnected endpoint rejection"), Blocked.bSucceeded);
	TestTrue(FString::Printf(TEXT("Blocked continuation reports route enforcement: %s"), *Blocked.FailureReason),
		Blocked.FailureReason.Contains(TEXT("Required traversal anchor could not be connected")));
	TestEqual(TEXT("Blocked solve preserves typed topology failure"),
		Blocked.PreparationFailureKind, ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible);
	return true;
}
