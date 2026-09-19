// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutSolvedArtifact.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	ULayoutModuleAsset* CreateSelectionModuleForIntents(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<ELayoutCellIntent>& Intents)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Name), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			Name,
			Template,
			Intents,
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}

	ULayoutModuleAsset* CreateSelectionModule(UObject* Outer, const TCHAR* Name)
	{
		return CreateSelectionModuleForIntents(Outer, Name, {ELayoutCellIntent::Boundary});
	}

	FLayoutRegionContentEntry MakeModuleEntry(
		const FName EntryId,
		ULayoutModuleAsset* Module,
		const int32 Weight,
		const ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any,
		const ELayoutLevelPlacementPolicy LevelPolicy = ELayoutLevelPlacementPolicy::AnyLevel,
		const int32 SpecificLevel = 0)
	{
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = EntryId;
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.Weight = Weight;
		Entry.ModuleSettings.Module = Module;
		Entry.ModuleSettings.PlacementZone = PlacementZone;
		Entry.ModuleSettings.LevelPlacementPolicy = LevelPolicy;
		Entry.ModuleSettings.SpecificLevel = SpecificLevel;
		return Entry;
	}

	ULayoutProfileAsset* CreateSelectionProfile(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<FLayoutRegionContentEntry>& Entries)
	{
		ULayoutProfileAsset* Profile = CreateProfile(Outer, Name, FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
		Profile->ContentSet = CreateRegionContentSet(Outer, *FString::Printf(TEXT("%s_Content"), Name), Entries);
		return Profile;
	}

	bool RequireSinglePlacement(FAutomationTestBase& Test, const FLayoutSolveResult& Result, const TCHAR* Context)
	{
		if (!Result.bSucceeded)
		{
			Test.AddError(FString::Printf(TEXT("%s failed: %s"), Context, *Result.FailureReason));
			return false;
		}
		if (Result.Placements.Num() != 1)
		{
			Test.AddError(FString::Printf(TEXT("%s produced %d placements instead of one."), Context, Result.Placements.Num()));
			return false;
		}
		return true;
	}

	const FLayoutPlacedModule* FindPlacementAt(const FLayoutSolveResult& Result, const FIntVector& Cell)
	{
		return Result.Placements.FindByPredicate([&Cell](const FLayoutPlacedModule& Placement)
		{
			return Placement.Cell == Cell;
		});
	}

	TArray<FLayoutPlannedCell> MakeAuthoredColumn(const TArray<TPair<int32, int32>>& PhysicalAndAuthoredLevels)
	{
		TArray<FLayoutPlannedCell> Cells;
		for (const TPair<int32, int32>& Levels : PhysicalAndAuthoredLevels)
		{
			FLayoutPlannedCell& Cell = Cells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(0, 0, Levels.Key);
			Cell.Intent = ELayoutCellIntent::Boundary;
			Cell.ModuleLevelIndex = Levels.Value;
		}
		return Cells;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleSelectionWeightAndIdentityTest,
	"PorismExtension.Layout.Solver.ModuleSelectionPolicy.WeightAndSourceIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies weight orders otherwise legal modules and selected identity survives pointer-free artifact creation. */
bool FLayoutModuleSelectionWeightAndIdentityTest::RunTest(const FString& Parameters)
{
	ULayoutModuleAsset* LowModule = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutSelectionLowWeight"));
	ULayoutModuleAsset* HighModule = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutSelectionHighWeight"));
	const FName HighEntryId(TEXT("HighWeightEntry"));
	ULayoutProfileAsset* Profile = CreateSelectionProfile(
		GetTransientPackage(),
		TEXT("LayoutSelectionWeightProfile"),
		{
			MakeModuleEntry(TEXT("LowWeightEntry"), LowModule, 1),
			MakeModuleEntry(HighEntryId, HighModule, 9)
		});

	const FLayoutSolveResult First = FLayoutProfileSolver::Solve(Profile, 731);
	const FLayoutSolveResult Repeated = FLayoutProfileSolver::Solve(Profile, 731);
	if (!RequireSinglePlacement(*this, First, TEXT("Weighted module solve"))
		|| !RequireSinglePlacement(*this, Repeated, TEXT("Repeated weighted module solve")))
	{
		return false;
	}

	TestEqual(TEXT("Higher weight wins between otherwise legal modules"), First.Placements[0].SourceContentEntryId, HighEntryId);
	TestEqual(TEXT("Repeated solve preserves source entry id"), Repeated.Placements[0].SourceContentEntryId, First.Placements[0].SourceContentEntryId);
	TestEqual(TEXT("Repeated solve preserves module snapshot id"), Repeated.Placements[0].ModuleSnapshotId, First.Placements[0].ModuleSnapshotId);
	TestEqual(TEXT("Repeated solve preserves cell"), Repeated.Placements[0].Cell, First.Placements[0].Cell);
	TestEqual(TEXT("Repeated solve preserves yaw"), Repeated.Placements[0].YawRotationSteps, First.Placements[0].YawRotationSteps);
	TestEqual(TEXT("Repeated solve preserves candidate attempts"), Repeated.PropagationStats.CandidateAttemptCount, First.PropagationStats.CandidateAttemptCount);
	TestEqual(TEXT("Repeated solve preserves backtracks"), Repeated.PropagationStats.BacktrackCount, First.PropagationStats.BacktrackCount);

	FLayoutRegionSolveResult RegionResult;
	RegionResult.RegionDebugPath = TEXT("Root/WeightIdentity");
	RegionResult.SolveResult = First;
	FLayoutSolvedArtifact Artifact;
	FString ArtifactFailure;
	TestTrue(
		TEXT("Weighted solve builds pointer-free artifact"),
		LayoutSolvedArtifact::TryBuildFromSolveResult(TEXT("Artifact.WeightIdentity"), RegionResult, Artifact, ArtifactFailure));
	if (Artifact.Placements.Num() == 1)
	{
		TestEqual(TEXT("Artifact preserves selected source entry id"), Artifact.Placements[0].SourceContentEntryId, HighEntryId);
		TestEqual(TEXT("Artifact preserves selected module snapshot id"), Artifact.Placements[0].ModuleSnapshotId, First.Placements[0].ModuleSnapshotId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleSelectionHardPolicyOverridesWeightTest,
	"PorismExtension.Layout.Solver.ModuleSelectionPolicy.HardPolicyOverridesWeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies hard finalized-cell policies filter candidates before weight ordering. */
bool FLayoutModuleSelectionHardPolicyOverridesWeightTest::RunTest(const FString& Parameters)
{
	ULayoutModuleAsset* LegalModule = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutSelectionLegal"));
	ULayoutModuleAsset* IllegalModule = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutSelectionIllegal"));
	const FName LegalEntryId(TEXT("LegalLowWeightEntry"));
	ULayoutProfileAsset* Profile = CreateSelectionProfile(
		GetTransientPackage(),
		TEXT("LayoutSelectionHardPolicyProfile"),
		{
			MakeModuleEntry(LegalEntryId, LegalModule, 1),
			MakeModuleEntry(
				TEXT("IllegalHighWeightEntry"),
				IllegalModule,
				100,
				ELayoutPlacementZone::Interior,
				ELayoutLevelPlacementPolicy::SpecificLevel,
				1)
		});

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 733);
	if (!RequireSinglePlacement(*this, Result, TEXT("Hard-policy weighted solve")))
	{
		return false;
	}
	TestEqual(TEXT("Hard zone and level policy override higher weight"), Result.Placements[0].SourceContentEntryId, LegalEntryId);

	UChunkStructureTemplate* FaceTemplate = CreateTemplate(GetTransientPackage(), TEXT("LayoutSelectionIllegalFace_Template"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* IllegalFaceModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutSelectionIllegalFace"),
		FaceTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutSelectionHardFaceContent"),
		{
			MakeModuleEntry(LegalEntryId, LegalModule, 1),
			MakeModuleEntry(TEXT("IllegalFaceHighWeightEntry"), IllegalFaceModule, 100)
		});
	const FLayoutSolveResult FaceResult = FLayoutProfileSolver::Solve(Profile, 737);
	if (!RequireSinglePlacement(*this, FaceResult, TEXT("Hard-face weighted solve")))
	{
		return false;
	}
	TestEqual(TEXT("Hard exterior face occupancy overrides higher weight"), FaceResult.Placements[0].SourceContentEntryId, LegalEntryId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeSelectionWeightSnapshotTest,
	"PorismExtension.Layout.Solver.ModuleSelectionPolicy.CompositeWeightSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies composite entry weight reaches immutable module catalog without entering Batch 11 occupancy repair. */
bool FLayoutCompositeSelectionWeightSnapshotTest::RunTest(const FString& Parameters)
{
	ULayoutModuleAsset* Leaf = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutSelectionCompositeLeaf"));
	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(GetTransientPackage(), TEXT("LayoutSelectionComposite"));
	FLayoutCompositeModuleCell& Cell = Composite->Cells.AddDefaulted_GetRef();
	Cell.Module = Leaf;
	Cell.LocalCell = FIntVector::ZeroValue;

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("WeightedCompositeEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 13;
	Entry.ModuleSettings.CompositeModule = Composite;
	ULayoutProfileAsset* Profile = CreateSelectionProfile(
		GetTransientPackage(),
		TEXT("LayoutSelectionCompositeProfile"),
		{Entry});

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 739, TEXT("CompositeWeightSnapshot"));
	TestEqual(TEXT("Composite compiles one immutable module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() == 1)
	{
		TestEqual(TEXT("Composite snapshot preserves entry weight"), Request.ModuleCatalog.Modules[0].Weight, 13);
		TestEqual(TEXT("Composite snapshot preserves source entry id"), Request.ModuleCatalog.Modules[0].SourceContentEntryId, Entry.EntryId);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleSelectionAuthoredLevelPolicyMatrixTest,
	"PorismExtension.Layout.Solver.ModuleSelectionPolicy.AuthoredLevelMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Characterizes every authored-level policy through supplied cells whose physical Z differs from authored level. */
bool FLayoutModuleSelectionAuthoredLevelPolicyMatrixTest::RunTest(const FString& Parameters)
{
	struct FPolicyCase
	{
		ELayoutLevelPlacementPolicy Policy;
		int32 SpecificLevel;
		TSet<int32> ExpectedAuthoredLevels;
	};
	const TArray<FPolicyCase> Cases = {
		{ELayoutLevelPlacementPolicy::AnyLevel, 0, {0, 1, 2}},
		{ELayoutLevelPlacementPolicy::GroundOnly, 0, {0}},
		{ELayoutLevelPlacementPolicy::SpecificLevel, 1, {1}},
		{ELayoutLevelPlacementPolicy::TopLevelOnly, 0, {2}},
		{ELayoutLevelPlacementPolicy::AboveGroundLevel, 0, {1, 2}},
		{ELayoutLevelPlacementPolicy::BelowTopLevel, 0, {0, 1}}
	};
	const TArray<FLayoutPlannedCell> ShiftedColumn = MakeAuthoredColumn({{4, 0}, {5, 1}, {6, 2}});

	for (int32 CaseIndex = 0; CaseIndex < Cases.Num(); ++CaseIndex)
	{
		const FPolicyCase& PolicyCase = Cases[CaseIndex];
		ULayoutModuleAsset* FallbackModule = CreateSelectionModule(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutLevelFallback_%d"), CaseIndex));
		ULayoutModuleAsset* PolicyModule = CreateSelectionModule(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutLevelPolicy_%d"), CaseIndex));
		const FName PolicyEntryId(*FString::Printf(TEXT("PolicyEntry_%d"), CaseIndex));
		ULayoutProfileAsset* Profile = CreateSelectionProfile(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutLevelProfile_%d"), CaseIndex),
			{
				MakeModuleEntry(TEXT("Fallback"), FallbackModule, 1),
				MakeModuleEntry(
					PolicyEntryId,
					PolicyModule,
					10,
					ELayoutPlacementZone::Any,
					PolicyCase.Policy,
					PolicyCase.SpecificLevel)
			});
		Profile->LevelCount = 3;

		const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells(
			Profile,
			751 + CaseIndex,
			FIntPoint(1, 1),
			ShiftedColumn);
		if (!Result.bSucceeded)
		{
			AddError(FString::Printf(TEXT("Authored-level policy case %d failed: %s"), CaseIndex, *Result.FailureReason));
			return false;
		}
		for (const FLayoutPlannedCell& PlannedCell : ShiftedColumn)
		{
			const FLayoutPlacedModule* Placement = FindPlacementAt(Result, PlannedCell.Cell);
			if (!TestNotNull(FString::Printf(TEXT("Policy case %d places physical Z=%d"), CaseIndex, PlannedCell.Cell.Z), Placement))
			{
				return false;
			}
			const bool bExpectedPolicyEntry = PolicyCase.ExpectedAuthoredLevels.Contains(PlannedCell.ModuleLevelIndex);
			TestEqual(
				FString::Printf(TEXT("Policy case %d uses authored level %d at physical Z=%d"), CaseIndex, PlannedCell.ModuleLevelIndex, PlannedCell.Cell.Z),
				Placement->SourceContentEntryId == PolicyEntryId,
				bExpectedPolicyEntry);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleSelectionFlatZoneMatrixTest,
	"PorismExtension.Layout.Solver.ModuleSelectionPolicy.FlatZoneMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Characterizes common flat zones and proves authored Core content can win at finalized center cells. */
bool FLayoutModuleSelectionFlatZoneMatrixTest::RunTest(const FString& Parameters)
{
	ULayoutModuleAsset* Boundary = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutZoneBoundary"));
	ULayoutModuleAsset* Corner = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutZoneCorner"));
	ULayoutModuleAsset* Edge = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutZoneEdge"));
	ULayoutModuleAsset* Core = CreateSelectionModuleForIntents(
		GetTransientPackage(),
		TEXT("LayoutZoneCore"),
		{ELayoutCellIntent::Core});
	ULayoutModuleAsset* Interior = CreateSelectionModuleForIntents(
		GetTransientPackage(),
		TEXT("LayoutZoneInterior"),
		{ELayoutCellIntent::Core, ELayoutCellIntent::Interior});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutZoneMatrixProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutZoneMatrixContent"),
		{
			MakeModuleEntry(TEXT("AnyBoundary"), Boundary, 1),
			MakeModuleEntry(TEXT("Corner"), Corner, 20, ELayoutPlacementZone::Corner),
			MakeModuleEntry(TEXT("Edge"), Edge, 20, ELayoutPlacementZone::Edge),
			MakeModuleEntry(TEXT("Interior"), Interior, 10, ELayoutPlacementZone::Interior),
			MakeModuleEntry(TEXT("Core"), Core, 20, ELayoutPlacementZone::Core)
		});

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 769);
	if (!Result.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Flat zone matrix failed: %s"), *Result.FailureReason));
		return false;
	}
	for (const FLayoutPlacedModule& Placement : Result.Placements)
	{
		const bool bCorner = (Placement.Cell.X == 0 || Placement.Cell.X == 2)
			&& (Placement.Cell.Y == 0 || Placement.Cell.Y == 2);
		const bool bEdge = !bCorner && (Placement.Cell.X == 0 || Placement.Cell.X == 2
			|| Placement.Cell.Y == 0 || Placement.Cell.Y == 2);
		const FName Expected = bCorner ? FName(TEXT("Corner"))
			: bEdge ? FName(TEXT("Edge"))
			: FName(TEXT("Core"));
		TestEqual(FString::Printf(TEXT("Cell %s selects finalized flat zone"), *Placement.Cell.ToString()), Placement.SourceContentEntryId, Expected);
	}

	ULayoutModuleAsset* Perimeter = CreateSelectionModule(GetTransientPackage(), TEXT("LayoutZonePerimeter"));
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutZonePerimeterContent"),
		{
			MakeModuleEntry(TEXT("AnyBoundary"), Boundary, 1),
			MakeModuleEntry(TEXT("Perimeter"), Perimeter, 20, ELayoutPlacementZone::Perimeter),
			MakeModuleEntry(TEXT("Interior"), Interior, 10, ELayoutPlacementZone::Interior)
		});
	const FLayoutSolveResult PerimeterResult = FLayoutProfileSolver::Solve(Profile, 773);
	if (!PerimeterResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("Perimeter zone solve failed: %s"), *PerimeterResult.FailureReason));
		return false;
	}
	for (const FLayoutPlacedModule& Placement : PerimeterResult.Placements)
	{
		if (Placement.Cell.X == 0 || Placement.Cell.X == 2 || Placement.Cell.Y == 0 || Placement.Cell.Y == 2)
		{
			TestEqual(TEXT("Perimeter policy covers Edge and Corner cells"), Placement.SourceContentEntryId, FName(TEXT("Perimeter")));
		}
	}

	const uint8 PosX = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
	const uint8 NegX = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
	const uint8 PosY = LayoutFaceDirectionMask(ELayoutFaceDirection::PosY);
	const uint8 NegY = LayoutFaceDirectionMask(ELayoutFaceDirection::NegY);
	TestEqual(TEXT("No lateral exposure resolves Interior"), ResolveLayoutPlacementZoneFromLateralFaceMask(0), ELayoutPlacementZone::Interior);
	TestEqual(TEXT("One lateral exposure resolves Edge"), ResolveLayoutPlacementZoneFromLateralFaceMask(PosX), ELayoutPlacementZone::Edge);
	TestEqual(TEXT("Orthogonal exposure resolves Corner"), ResolveLayoutPlacementZoneFromLateralFaceMask(PosX | PosY), ELayoutPlacementZone::Corner);
	TestEqual(TEXT("Opposite exposure retains established Corner classification"), ResolveLayoutPlacementZoneFromLateralFaceMask(PosX | NegX), ELayoutPlacementZone::Corner);
	TestEqual(TEXT("Three exposed faces resolve Corner"), ResolveLayoutPlacementZoneFromLateralFaceMask(PosX | NegX | PosY), ELayoutPlacementZone::Corner);
	TestEqual(TEXT("Four exposed faces resolve Corner"), ResolveLayoutPlacementZoneFromLateralFaceMask(PosX | NegX | PosY | NegY), ELayoutPlacementZone::Corner);

	Profile->MinimumFootprintInCells = FIntPoint(4, 4);
	Profile->MaximumFootprintInCells = FIntPoint(4, 4);
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutZoneEvenCoreContent"),
		{
			MakeModuleEntry(TEXT("AnyBoundary"), Boundary, 1),
			MakeModuleEntry(TEXT("Corner"), Corner, 20, ELayoutPlacementZone::Corner),
			MakeModuleEntry(TEXT("Edge"), Edge, 20, ELayoutPlacementZone::Edge),
			MakeModuleEntry(TEXT("Interior"), Interior, 10, ELayoutPlacementZone::Interior),
			MakeModuleEntry(TEXT("Core"), Core, 20, ELayoutPlacementZone::Core)
		});
	const FLayoutSolveResult EvenResult = FLayoutProfileSolver::Solve(Profile, 777);
	TestTrue(TEXT("Even-footprint Core solve succeeds"), EvenResult.bSucceeded);
	TestEqual(TEXT("Even footprint selects four central Core placements"), EvenResult.Placements.FilterByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId == FName(TEXT("Core"));
	}).Num(), 4);

	Profile->MinimumFootprintInCells = FIntPoint(5, 5);
	Profile->MaximumFootprintInCells = FIntPoint(5, 5);
	TArray<FLayoutPlannedCell> RemovedInteriorPlan;
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			if (X == 1 && Y == 1)
			{
				continue;
			}
			FLayoutPlannedCell& Cell = RemovedInteriorPlan.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = X == 0 || X == 4 || Y == 0 || Y == 4
				? ELayoutCellIntent::Boundary
				: X == 2 && Y == 2
					? ELayoutCellIntent::Core
					: ELayoutCellIntent::Interior;
		}
	}
	const FLayoutSolveResult RemovedInteriorResult = FLayoutProfileSolver::SolveWithPlannedCells(
		Profile,
		779,
		FIntPoint(5, 5),
		RemovedInteriorPlan);
	TestTrue(TEXT("Core solve survives a removed non-central interior cell"), RemovedInteriorResult.bSucceeded);
	if (!RemovedInteriorResult.bSucceeded)
	{
		AddError(RemovedInteriorResult.FailureReason);
		return false;
	}
	const FLayoutPlacedModule* RemovedInteriorCore = FindPlacementAt(RemovedInteriorResult, FIntVector(2, 2, 0));
	TestNotNull(TEXT("Removed-interior solve keeps center placement"), RemovedInteriorCore);
	if (RemovedInteriorCore != nullptr)
	{
		TestEqual(TEXT("Removed-interior solve keeps authored Core selection"), RemovedInteriorCore->SourceContentEntryId, FName(TEXT("Core")));
	}
	return true;
}
