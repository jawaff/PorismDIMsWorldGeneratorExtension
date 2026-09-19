// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessPrewarmAdjacentHostsTest,
	"PorismExtension.Layout.Solver.VerticalAccess.Prewarm.AdjacentHosts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutVerticalAccessPrewarmPreservesSoleEntryIngressTest,
	"PorismExtension.Layout.Solver.VerticalAccess.Prewarm.PreservesSoleEntryIngress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Exercises host assignment through the production prewarm route-domain proof. */
bool FLayoutVerticalAccessPrewarmAdjacentHostsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_AdjacentHosts"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("Cell"), FIntVector(8, 8, 8));
	TArray<FLayoutFaceRule> Faces;
	for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
	{
		Faces.Add(MakeConnectionFaceRule(
			static_cast<ELayoutFaceDirection>(DirectionIndex),
			LayoutGameplayTags::FaceOpen,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	}
	ULayoutModuleAsset* Stair = CreateModule(
		Outer, TEXT("Stair"), Template, {ELayoutCellIntent::VerticalAccess}, Faces);
	ULayoutModuleAsset* Floor = CreateModule(
		Outer, TEXT("Floor"), Template, {ELayoutCellIntent::Interior, ELayoutCellIntent::Boundary}, Faces);

	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("AdjacentHostPrewarm");
	Request.Seed = 77;
	Request.FootprintSize = FIntPoint(2, 1);
	Request.ProfileSnapshot.LevelCount = 2;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::None;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.VerticalAccessCount = 2;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	Request.ModuleCatalog.Modules = {
		FLayoutProfileSolver::BuildModuleSnapshot(Stair, 1),
		FLayoutProfileSolver::BuildModuleSnapshot(Floor, 1)};
	Request.bHasFinalizedSteppedTerrainIntents = true;
	for (int32 X = 0; X < 2; ++X)
	{
		for (int32 Z = 0; Z < 2; ++Z)
		{
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, 0, Z);
			Cell.ModuleLevelIndex = Z;
			Cell.Intent = Z == 0 ? ELayoutCellIntent::VerticalAccess : ELayoutCellIntent::Interior;
		}
		FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
		Group.GroupId = FLayoutId(*FString::Printf(TEXT("AuthoredHost%d"), X));
		Group.DeckCell = FIntVector(X, 0, 1);
		FLayoutVerticalAccessHostOption& Option = Group.Options.AddDefaulted_GetRef();
		Option.LowerCell = FIntVector(X, 0, 0);
		Option.UpperCell = Group.DeckCell;
	}

	FLayoutRegionSolveRequest Frozen;
	FString FailureReason;
	const bool bSucceeded = LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
		Request, false, {}, {}, Frozen, FailureReason);
	if (!TestTrue(TEXT("Compatible adjacent hosts survive production prewarm"), bSucceeded))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Both authored host groups remain frozen"), Frozen.VerticalAccessHostGroups.Num(), 2);
	TestEqual(TEXT("Both selected stair intents remain"), Frozen.PlannedCells.FilterByPredicate(
		[](const FLayoutPlannedCell& Cell) { return Cell.Intent == ELayoutCellIntent::VerticalAccess; }).Num(), 2);

	Request.VerticalAccessHostGroups[1].Options = Request.VerticalAccessHostGroups[0].Options;
	TestFalse(TEXT("Overlapping hosts still reject"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request, false, {}, {}, Frozen, FailureReason));
	return true;
}

/** Prevents a stair with no reciprocal face from occupying an Entry's only interior route edge. */
bool FLayoutVerticalAccessPrewarmPreservesSoleEntryIngressTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutVerticalAccess_SoleEntryIngress"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("Cell"), FIntVector(8, 8, 8));
	const FGameplayTagContainer Open = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer Primary = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const auto MakeOpenFace = [&Open, &Primary](
		const ELayoutFaceDirection Direction,
		const bool bTraversable)
	{
		return MakeFaceRule(
			Direction,
			Open,
			Open,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			bTraversable ? Primary : FGameplayTagContainer());
	};

	TArray<FLayoutFaceRule> StairFaces;
	for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
	{
		const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
		StairFaces.Add(MakeOpenFace(
			Direction,
			Direction == ELayoutFaceDirection::PosX || Direction == ELayoutFaceDirection::PosZ));
	}
	TArray<FLayoutFaceRule> WalkableFaces;
	for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
	{
		WalkableFaces.Add(MakeOpenFace(
			static_cast<ELayoutFaceDirection>(DirectionIndex),
			DirectionIndex < 4));
	}
	ULayoutModuleAsset* Stair = CreateModule(
		Outer, TEXT("OneWayStair"), Template, {ELayoutCellIntent::VerticalAccess}, StairFaces);
	TArray<FLayoutFaceRule> WallFaces;
	for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
	{
		WallFaces.Add(MakeOpenFace(static_cast<ELayoutFaceDirection>(DirectionIndex), false));
	}
	ULayoutModuleAsset* Walkable = CreateModule(
		Outer, TEXT("Walkable"), Template, {ELayoutCellIntent::Interior}, WalkableFaces);
	ULayoutModuleAsset* Wall = CreateModule(
		Outer, TEXT("Wall"), Template, {ELayoutCellIntent::Boundary}, WallFaces);
	ULayoutModuleAsset* Entry = CreateModule(
		Outer, TEXT("Entry"), Template, {ELayoutCellIntent::Entry}, WalkableFaces);

	FLayoutRegionSolveRequest Request;
	Request.FootprintSize = FIntPoint(5, 3);
	Request.ProfileSnapshot.LevelCount = 2;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.EntryCount = 1;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.VerticalAccessCount = 1;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	Request.ModuleCatalog.Modules = {
		FLayoutProfileSolver::BuildModuleSnapshot(Stair, 1),
		FLayoutProfileSolver::BuildModuleSnapshot(Walkable, 1),
		FLayoutProfileSolver::BuildModuleSnapshot(Wall, 1),
		FLayoutProfileSolver::BuildModuleSnapshot(Entry, 1)};
	for (FLayoutModuleSolveSnapshot& Module : Request.ModuleCatalog.Modules)
	{
		Module.AllowedYawRotationSteps = {0};
	}
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 5; ++X)
			{
				FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, Z);
				Cell.ModuleLevelIndex = Z;
				Cell.Intent = Z == 0 && X == 0 && Y == 1
					? ELayoutCellIntent::Entry
					: (X == 0 || X == 4 || Y == 0 || Y == 2
						? ELayoutCellIntent::Boundary
						: ELayoutCellIntent::Interior);
			}
		}
	}

	TArray<FLayoutVerticalAccessHostGroup> Groups;
	FString FailureReason;
	if (!TestTrue(TEXT("Host generation succeeds"),
		LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			Request, Request.PlannedCells, Groups, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestTrue(TEXT("A host remains away from the Entry"), !Groups.IsEmpty() && !Groups[0].Options.IsEmpty());
	if (!Groups.IsEmpty())
	{
		TestFalse(TEXT("Stair cannot consume the Entry's sole route edge without a reciprocal face"),
			Groups[0].Options.ContainsByPredicate([](const FLayoutVerticalAccessHostOption& Option)
			{
				return Option.LowerCell == FIntVector(1, 1, 0);
			}));
	}
	Request.ModuleCatalog.Modules[0].AllowedYawRotationSteps = {2};
	FLayoutCommittedTraversalAnchor& OtherAnchor = Request.CommittedTraversalAnchors.AddDefaulted_GetRef();
	OtherAnchor.Cell = FIntVector(3, 1, 0);
	OtherAnchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	TestTrue(TEXT("Reciprocal Entry host remains locally admissible with another same-level anchor"),
		LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			Request, Request.PlannedCells, Groups, FailureReason));
	TestTrue(TEXT("Admission does not infer global disconnection from missing second horizontal stair exit"),
		Groups.ContainsByPredicate([](const FLayoutVerticalAccessHostGroup& Group)
		{
			return Group.Options.ContainsByPredicate([](const FLayoutVerticalAccessHostOption& Option)
			{
				return Option.LowerCell == FIntVector(1, 1, 0);
			});
		}));
	Request.ModuleCatalog.Modules[1].LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	TestFalse(TEXT("GroundOnly floor cannot supply upper landing"),
		LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
			Request, Request.FootprintSize, Request.PlannedCells, FIntVector(1, 1, 0), FailureReason));
	Request.ModuleCatalog.Modules[1].LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
	Request.ModuleCatalog.Modules.RemoveAt(0);
	TestFalse(TEXT("No stair catalog cannot produce a legal host"),
		LayoutProfileSolverInternal::DoesVerticalAccessHostAdmitCandidatePairForTests(
			Request, Request.FootprintSize, Request.PlannedCells, FIntVector(1, 1, 0), FailureReason));
	return true;
}
