// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateRegionTreeSolveTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	ULayoutModuleAsset* CreateOpenFillModule(
		UObject* Outer,
		const TCHAR* ModuleName,
		const TArray<ELayoutCellIntent>& SupportedIntents)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), ModuleName), FIntVector(1, 1, 1));
		return CreateModule(
			Outer,
			ModuleName,
			Template,
			SupportedIntents,
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}

	TArray<FLayoutFaceRule> BuildRecursiveTraversalFaces(
		const FGameplayTagContainer& PosXTraversal,
		const FGameplayTagContainer& NegXTraversal,
		const FGameplayTagContainer& PosYTraversal,
		const FGameplayTagContainer& NegYTraversal,
		const FGameplayTagContainer& PosZTraversal = FGameplayTagContainer(),
		const FGameplayTagContainer& NegZTraversal = FGameplayTagContainer())
	{
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenSolidAndEntryTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid, LayoutGameplayTags::FaceEntry});
		return {
			MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PosXTraversal),
			MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, NegXTraversal),
			MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PosYTraversal),
			MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, NegYTraversal),
			MakeFaceRule(ELayoutFaceDirection::PosZ, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, PosZTraversal),
			MakeFaceRule(ELayoutFaceDirection::NegZ, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, NegZTraversal)
		};
	}

	ULayoutRegionContentSetAsset* CreateRecursiveVerticalAccessContentSet(UObject* Outer)
	{
		const FIntVector CellSize(5, 5, 5);
		const FGameplayTagContainer Primary = MakeTags({LayoutGameplayTags::TraversalPrimary});
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenAndSolidTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});
		const FGameplayTagContainer ExteriorEntryTags = MakeTags({LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer ExteriorTags = MakeTags({LayoutGameplayTags::FaceSolid});

		auto MakeRouteModule = [&](const TCHAR* Name, const TArray<ELayoutCellIntent>& SupportedIntents, const TArray<FLayoutFaceRule>& Faces)
		{
			ULayoutModuleAsset* Module = CreateModule(
				Outer,
				Name,
				CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Name), CellSize),
				SupportedIntents,
				Faces);
			return Module;
		};

		ULayoutModuleAsset* Entry = MakeRouteModule(
			TEXT("RegionTreeVerticalHostEntry"),
			{ELayoutCellIntent::Entry},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, Primary),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, Primary),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, Primary),
				MakeFaceRule(ELayoutFaceDirection::NegY, ExteriorEntryTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor, Primary),
				MakeFaceRule(ELayoutFaceDirection::PosZ, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, OpenTags, OpenAndSolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
			});

		ULayoutModuleAsset* Boundary = MakeRouteModule(
			TEXT("RegionTreeVerticalHostBoundary"),
			{ELayoutCellIntent::Boundary},
			BuildRecursiveTraversalFaces(Primary, Primary, Primary, Primary, FGameplayTagContainer(), Primary));

		ULayoutModuleAsset* Floor = MakeRouteModule(
			TEXT("RegionTreeVerticalHostFloor"),
			{ELayoutCellIntent::Core, ELayoutCellIntent::Interior, ELayoutCellIntent::Connector},
			BuildRecursiveTraversalFaces(Primary, Primary, Primary, Primary, FGameplayTagContainer(), Primary));

		ULayoutModuleAsset* Stair = MakeRouteModule(
			TEXT("RegionTreeVerticalHostStair"),
			{ELayoutCellIntent::VerticalAccess},
			BuildRecursiveTraversalFaces(Primary, Primary, Primary, Primary, Primary, Primary));

		TArray<FLayoutRegionContentEntry> Entries;
		auto AddModuleEntry = [&Entries](const TCHAR* EntryId, ULayoutModuleAsset* Module, const ELayoutPlacementZone PlacementZone)
		{
			FLayoutRegionContentEntry& Entry = Entries.AddDefaulted_GetRef();
			Entry.EntryId = FName(EntryId);
			Entry.ContentKind = ELayoutRegionContentKind::Module;
			Entry.ModuleSettings.Module = Module;
			Entry.ModuleSettings.PlacementZone = PlacementZone;
		};

		AddModuleEntry(TEXT("Entry"), Entry, ELayoutPlacementZone::Edge);
		AddModuleEntry(TEXT("Boundary"), Boundary, ELayoutPlacementZone::Perimeter);
		AddModuleEntry(TEXT("Floor"), Floor, ELayoutPlacementZone::Any);
		AddModuleEntry(TEXT("Stair"), Stair, ELayoutPlacementZone::Any);
		return CreateRegionContentSet(Outer, TEXT("RegionTreeVerticalHostContentSet"), Entries);
	}

	TArray<FLayoutPlannedCell> BuildHostVerticalAccessPlannedCells()
	{
		TArray<FLayoutPlannedCell> PlannedCells;
		for (int32 Level = 0; Level < 2; ++Level)
		{
			for (int32 Y = 0; Y < 7; ++Y)
			{
				for (int32 X = 0; X < 7; ++X)
				{
					FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
					Cell.Cell = FIntVector(X, Y, Level);
					const bool bBoundary = X == 0 || Y == 0 || X == 6 || Y == 6;
					Cell.Intent = bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
				}
			}
		}

		for (FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			if (PlannedCell.Cell == FIntVector(3, 0, 0))
			{
				PlannedCell.Intent = ELayoutCellIntent::Entry;
			}
			else if (PlannedCell.Cell == FIntVector(3, 3, 0))
			{
				PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
			}
		}

		return PlannedCells;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionTreeSolveExpandsDirectChildEntriesTest,
	"PorismExtension.Layout.Solver.RegionTree.ExpandsDirectChildEntries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionTreeSolveIndependentRecursiveChildProofResultTest,
	"PorismExtension.Layout.Solver.RegionTree.IndependentRecursiveChildProofResult",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionTreeSolvePreservesRequestOwnedMergedMetricsOnRecursiveSolveTest,
	"PorismExtension.Layout.Solver.RegionTree.PreservesRequestOwnedMergedMetricsOnRecursiveSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionTreeSolveExpandsDirectChildEntriesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateRegionTreeSolveTestOuter(TEXT("LayoutRegionTreeSolve"));

	ULayoutModuleAsset* ParentModule = CreateOpenFillModule(
		Outer,
		TEXT("RegionTreeParentShell"),
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
	ULayoutModuleAsset* ChildModule = CreateOpenFillModule(
		Outer,
		TEXT("RegionTreeChildRoom"),
		{ELayoutCellIntent::Boundary});

	FLayoutRegionContentEntry ChildModuleEntry;
	ChildModuleEntry.EntryId = TEXT("RoomShell");
	ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ChildModuleEntry.ModuleSettings.Module = ChildModule;
	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("RegionTreeChildContentSet"),
		{ChildModuleEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("RegionTreeChildProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ParentModuleEntry;
	ParentModuleEntry.EntryId = TEXT("CastleShell");
	ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ParentModuleEntry.ModuleSettings.Module = ParentModule;

	FLayoutRegionContentEntry ChildRegionEntry;
	ChildRegionEntry.EntryId = TEXT("Room");
	ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildRegionEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

	ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
		Outer,
		TEXT("RegionTreeParentContentSet"),
		{ParentModuleEntry, ChildRegionEntry});

	ULayoutProfileAsset* ParentProfile = CreateProfile(
		Outer,
		TEXT("RegionTreeParentProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	ParentProfile->ContentSet = ParentContentSet;

	const FLayoutRegionSolveRequest RootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ParentContentSet,
		ParentProfile,
		913,
		TEXT("Root"));
	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionTree(RootRequest);
	TestTrue(TEXT("Root region tree solve succeeds"), ScheduleResult.bSucceeded);
	TestEqual(TEXT("Root region tree solve returns the parent and one child result"), ScheduleResult.RegionResults.Num(), 2);
	if (!ScheduleResult.bSucceeded || ScheduleResult.RegionResults.Num() != 2)
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* ChildResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& Result)
	{
		return Result.SourceContentEntryId == FLayoutId(TEXT("Room"));
	});
	TestNotNull(TEXT("Child region result is present in the schedule"), ChildResult);
	if (ChildResult == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Child region was placed away from the parent perimeter"), ChildResult->RegionCellOffset.X > 0 && ChildResult->RegionCellOffset.X < 2);
	TestTrue(TEXT("Child region was placed away from the parent perimeter"), ChildResult->RegionCellOffset.Y > 0 && ChildResult->RegionCellOffset.Y < 2);
	TestTrue(TEXT("Certified child result retains its worker-safe solved module identity before stable merge"), ChildResult->SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId == TEXT("RoomShell")
			&& !Placement.ModuleSnapshotId.IsNone();
	}));
	TestTrue(TEXT("Merged placements include the child module identity"), ScheduleResult.MergedSolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId == TEXT("RoomShell")
			&& !Placement.ModuleSnapshotId.IsNone();
	}));
	TestEqual(TEXT("Merged solve keeps the full parent footprint size"), ScheduleResult.MergedSolveResult.FootprintSize, FIntPoint(3, 3));
	TestEqual(TEXT("Merged solve contains one planned child placement"), ScheduleResult.MergedSolveResult.Placements.FilterByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId == TEXT("RoomShell")
			&& !Placement.ModuleSnapshotId.IsNone();
	}).Num(), 1);

	// The child consumes the only Interior cell. Typed preservation still selects
	// the parent's sparse path; empty eligibility must not hide a legacy fallback.
	FLayoutSparsePreserveTerrainRule Rule;
	Rule.RuleId = TEXT("ParentInterior");
	Rule.PlacementZone = ELayoutPlacementZone::Interior;
	ParentProfile->SparsePlacementRules.Add(FInstancedStruct::Make(Rule));
	const auto SparseRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ParentContentSet, ParentProfile, 913, TEXT("Root"));
	LayoutSolveExecution::FScope SparseScope(0.0, 50000);
	const auto SparseResult = FLayoutProfileSolver::SolveRegionTree(SparseRequest);
	if (!TestTrue(TEXT("Preserving parent keeps independent child solve"), SparseResult.bSucceeded))
	{
		AddError(SparseResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Sparse scheduling retains both regional results"), SparseResult.RegionResults.Num(), 2);
	TestTrue(TEXT("Parent sparse dispatch survives fully delegated Interior"),
		LayoutSolveExecution::CurrentThreadLedger()->SparseWork > 0);

	auto* LargerProfile = CreateProfile(Outer, TEXT("LargerSparseParent"),
		FIntPoint(5, 5), FIntPoint(5, 5), 1, 0, false);
	LargerProfile->ContentSet = ParentContentSet;
	LargerProfile->SparsePlacementRules.Add(FInstancedStruct::Make(Rule));
	const auto LargerRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ParentContentSet, LargerProfile, 913, TEXT("Root"));
	const auto LargerResult = FLayoutProfileSolver::SolveRegionTree(LargerRequest);
	if (!TestTrue(TEXT("Preserving parent solves with residual space around child"), LargerResult.bSucceeded))
	{
		AddError(LargerResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Parent boundary and child remain structural"), LargerResult.MergedSolveResult.Placements.Num(), 17);
	TestEqual(TEXT("Only unclaimed parent Interior becomes residual"), LargerResult.MergedSolveResult.ResidualUnoccupiedCells.Num(), 8);
	return true;
}

bool FLayoutRegionTreeSolveIndependentRecursiveChildProofResultTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateRegionTreeSolveTestOuter(TEXT("LayoutIndependentRecursiveChildProof"));

	ULayoutModuleAsset* ChildModule = CreateOpenFillModule(
		Outer,
		TEXT("IndependentRecursiveChildShell"),
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
	ULayoutModuleAsset* GrandchildModule = CreateOpenFillModule(
		Outer,
		TEXT("IndependentRecursiveGrandchildRoom"),
		{ELayoutCellIntent::Boundary});

	FLayoutRegionContentEntry GrandchildModuleEntry;
	GrandchildModuleEntry.EntryId = TEXT("InnerRoomShell");
	GrandchildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	GrandchildModuleEntry.ModuleSettings.Module = GrandchildModule;
	ULayoutRegionContentSetAsset* GrandchildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("IndependentRecursiveGrandchildContentSet"),
		{GrandchildModuleEntry});

	ULayoutProfileAsset* GrandchildProfile = CreateProfile(
		Outer,
		TEXT("IndependentRecursiveGrandchildProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	GrandchildProfile->ContentSet = GrandchildContentSet;

	FLayoutRegionContentEntry ChildModuleEntry;
	ChildModuleEntry.EntryId = TEXT("RoomShell");
	ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ChildModuleEntry.ModuleSettings.Module = ChildModule;

	FLayoutRegionContentEntry GrandchildRegionEntry;
	GrandchildRegionEntry.EntryId = TEXT("InnerRoom");
	GrandchildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	GrandchildRegionEntry.ChildRegionSettings.RegionProfile = GrandchildProfile;
	GrandchildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("IndependentRecursiveChildContentSet"),
		{ChildModuleEntry, GrandchildRegionEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("IndependentRecursiveChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionSolveRequest ChildProofRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ChildContentSet,
		ChildProfile,
		1313,
		TEXT("ChildProofRoot"));
	ChildProofRequest.CandidateDomainCertificateId = TEXT("InheritedChildBoundaryCertificate");
	FLayoutCellCandidateDomainRestriction& InheritedRestriction =
		ChildProofRequest.CandidateDomainRestrictions.AddDefaulted_GetRef();
	InheritedRestriction.RestrictionId = TEXT("InheritedChildBoundaryCell");
	InheritedRestriction.Cell = FIntVector(0, 0, 0);
	FLayoutCandidateVariantIdentity& AllowedCandidate =
		InheritedRestriction.AllowedCandidates.AddDefaulted_GetRef();
	AllowedCandidate.ModuleSnapshotId = ChildProofRequest.ModuleCatalog.Modules[0].SnapshotId;
	AllowedCandidate.YawRotationSteps = 0;

	FLayoutRegionSolveResult ChildProofResult;
	FString FailureReason;
	const bool bBuiltProofResult = LayoutProfileSolverInternal::BuildIndependentChildProofResultForTests(
		ChildProofRequest,
		ChildProofResult,
		FailureReason);
	if (!TestTrue(TEXT("Independent child proof builder succeeds for a recursive child request"), bBuiltProofResult))
	{
		AddError(FailureReason);
		return false;
	}

	TestEqual(TEXT("Collapsed independent child proof keeps the child root path"), ChildProofResult.RegionDebugPath, FString(TEXT("ChildProofRoot")));
	TestTrue(TEXT("Collapsed independent child proof result succeeded"), ChildProofResult.SolveResult.bSucceeded);
	TestFalse(
		TEXT("Collapsed independent child proof keeps regional domain certificate provenance"),
		ChildProofResult.SolveResult.CandidateDomainCertificateId.IsNone());
	TestTrue(
		TEXT("Collapsed independent child proof keeps consumed root restriction"),
		ChildProofResult.SolveResult.CandidateDomainRestrictionIds.Contains(
			InheritedRestriction.RestrictionId));
	TestTrue(TEXT("Collapsed independent child proof keeps worker-safe child-shell placements"), ChildProofResult.SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId == TEXT("RoomShell")
			&& !Placement.ModuleSnapshotId.IsNone();
	}));
	TestTrue(TEXT("Collapsed independent child proof includes worker-safe grandchild placements from the subtree merge"), ChildProofResult.SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId == TEXT("InnerRoomShell")
			&& !Placement.ModuleSnapshotId.IsNone();
	}));
	return true;
}

bool FLayoutRegionTreeSolvePreservesRequestOwnedMergedMetricsOnRecursiveSolveTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateRegionTreeSolveTestOuter(TEXT("LayoutRegionTreeSolveMergedMetrics"));
	const FIntVector SharedCellSizeInBlocks(8, 8, 6);
	const int32 TemplatePlacementZOffsetBlocks = -3;

	ULayoutModuleAsset* ParentModule = CreateOpenFillModule(
		Outer,
		TEXT("RegionTreeMergedMetricsParentShell"),
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
	ULayoutModuleAsset* ChildModule = CreateOpenFillModule(
		Outer,
		TEXT("RegionTreeMergedMetricsChildRoom"),
		{ELayoutCellIntent::Boundary});

	FLayoutRegionContentEntry ChildModuleEntry;
	ChildModuleEntry.EntryId = TEXT("RoomShell");
	ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ChildModuleEntry.ModuleSettings.Module = ChildModule;
	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("RegionTreeMergedMetricsChildContentSet"),
		{ChildModuleEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("RegionTreeMergedMetricsChildProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ParentModuleEntry;
	ParentModuleEntry.EntryId = TEXT("CastleShell");
	ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ParentModuleEntry.ModuleSettings.Module = ParentModule;

	FLayoutRegionContentEntry ChildRegionEntry;
	ChildRegionEntry.EntryId = TEXT("Room");
	ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildRegionEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

	ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
		Outer,
		TEXT("RegionTreeMergedMetricsParentContentSet"),
		{ParentModuleEntry, ChildRegionEntry});

	ULayoutProfileAsset* ParentProfile = CreateProfile(
		Outer,
		TEXT("RegionTreeMergedMetricsParentProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	ParentProfile->ContentSet = ParentContentSet;

	FLayoutRegionSolveRequest RootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ParentContentSet,
		ParentProfile,
		9913,
		TEXT("RootMergedMetrics"),
		FLayoutSolverExecutionSettings(),
		NAME_None,
		NAME_None,
		TemplatePlacementZOffsetBlocks,
		NAME_None,
		&SharedCellSizeInBlocks);

	TestEqual(TEXT("Recursive merged-metrics fixture preserves the explicit request-owned shared-cell override on the frozen content-set snapshot"), RootRequest.ContentSetSnapshot.SharedCellSizeInBlocks, SharedCellSizeInBlocks);
	TestEqual(TEXT("Recursive merged-metrics fixture preserves the explicit request-owned shared-cell override on the frozen module-set snapshot"), RootRequest.ModuleCatalog.SharedCellSizeInBlocks, SharedCellSizeInBlocks);

	const FLayoutRegionSolveScheduleResult ScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(RootRequest);
	if (!TestTrue(TEXT("Recursive region-tree solve succeeds for the merged-metrics fixture"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	TestEqual(
		TEXT("Recursive merged solve keeps the request-owned shared cell size"),
		ScheduleResult.MergedSolveResult.SharedCellSizeInBlocks,
		SharedCellSizeInBlocks);
	TestEqual(
		TEXT("Recursive merged solve keeps the request-owned template placement offset"),
		ScheduleResult.MergedSolveResult.TemplatePlacementZOffsetBlocks,
		TemplatePlacementZOffsetBlocks);
	const FLayoutRegionSolveResult* RootResult =
		ScheduleResult.RegionResults.FindByPredicate([&RootRequest](const FLayoutRegionSolveResult& Result)
		{
			return Result.RegionDebugPath == RootRequest.RegionDebugPath;
		});
	if (!TestNotNull(TEXT("Root region result is present in the recursive merged-metrics fixture"), RootResult))
	{
		return false;
	}
	TestEqual(
		TEXT("Root region result keeps the request-owned shared cell size"),
		RootResult->SolveResult.SharedCellSizeInBlocks,
		SharedCellSizeInBlocks);
	TestEqual(
		TEXT("Root region result keeps the request-owned template placement offset"),
		RootResult->SolveResult.TemplatePlacementZOffsetBlocks,
		TemplatePlacementZOffsetBlocks);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionTreeSolveChildVerticalAccessContributionTest,
	"PorismExtension.Layout.Solver.RegionTree.ChildVerticalAccessContribution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionTreeSolveChildVerticalAccessContributionTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("HostVerticalAccessOwnership");
	RootRequest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	RootRequest.ProfileSnapshot.VerticalAccessCount = 1;
	RootRequest.ProfileSnapshot.MinVerticalAccessCount = 1;
	RootRequest.ProfileSnapshot.MaxVerticalAccessCount = 1;

	TArray<FLayoutPlannedCell> ParentPlannedCells;
	FLayoutPlannedCell& ParentVerticalAccess = ParentPlannedCells.AddDefaulted_GetRef();
	ParentVerticalAccess.Cell = FIntVector(2, 2, 0);
	ParentVerticalAccess.Intent = ELayoutCellIntent::VerticalAccess;

	TSet<FIntVector> ReservedParentCells;
	ReservedParentCells.Add(ParentVerticalAccess.Cell);

	TArray<LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider> ChildProviders;
	LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider& LocalOnlyProvider = ChildProviders.AddDefaulted_GetRef();
	LocalOnlyProvider.bSupportsVerticalAccess = true;
	LocalOnlyProvider.bContributesHostVerticalAccess = false;
	LocalOnlyProvider.VerticalAccessLocalCells = {
		FIntVector(1, 1, 0),
		FIntVector(1, 1, 1)
	};
	LocalOnlyProvider.PlannedCells = {
		{FIntVector(1, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::VerticalAccess}
	};
	LocalOnlyProvider.ChildRegionDebugPath = TEXT("Root/ChildLocalOnly");

	const LayoutProfileSolverInternal::FRecursiveHostVerticalAccessComposition LocalOnlyComposition =
		LayoutProfileSolverInternal::DetermineRecursiveHostVerticalAccessCompositionForSchedule(
			RootRequest,
			ParentPlannedCells,
			ReservedParentCells,
			ChildProviders);
	TestEqual(TEXT("Local-only child vertical access keeps the host count on the parent side"), LocalOnlyComposition.CountedParentProviderCount, 1);
	TestEqual(TEXT("Local-only child vertical access does not get counted upward"), LocalOnlyComposition.CountedChildRegionDebugPaths.Num(), 0);
	TestEqual(TEXT("Local-only child provider is reported separately"), LocalOnlyComposition.LocalOnlyChildRegionDebugPaths.Num(), 1);

	ChildProviders[0].bContributesHostVerticalAccess = true;
	FLayoutCommittedEndpointAnchor& LowerAnchor = ChildProviders[0].EndpointCommitments.AddDefaulted_GetRef();
	LowerAnchor.CommitmentId = TEXT("LowerDoor");
	LowerAnchor.LocalCell = FIntVector(1, 1, 0);
	LowerAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	LowerAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	FLayoutCommittedEndpointAnchor& UpperAnchor = ChildProviders[0].EndpointCommitments.AddDefaulted_GetRef();
	UpperAnchor.CommitmentId = TEXT("UpperDoor");
	UpperAnchor.LocalCell = FIntVector(1, 1, 1);
	UpperAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	UpperAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	FString OwnedChildRegionPath;
	const LayoutProfileSolverInternal::ERecursiveVerticalAccessOwnerKind ContributingOwner =
		LayoutProfileSolverInternal::DetermineRecursiveVerticalAccessOwnerForSchedule(
			RootRequest,
			ParentPlannedCells,
			ReservedParentCells,
			ChildProviders,
			&OwnedChildRegionPath);
	TestEqual(TEXT("Host-contributing child vertical access becomes the composed owner when parent stairs are reserved away"), ContributingOwner, LayoutProfileSolverInternal::ERecursiveVerticalAccessOwnerKind::ChildRegion);
	TestEqual(TEXT("Host-contributing ownership reports the contributing child path"), OwnedChildRegionPath, FString(TEXT("Root/ChildLocalOnly")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionTreeSolveComposedVerticalAccessCountingTest,
	"PorismExtension.Layout.Solver.RegionTree.ComposedVerticalAccessCounting",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionTreeSolveComposedVerticalAccessCountingTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("HostVerticalAccessComposition");
	RootRequest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	RootRequest.ProfileSnapshot.VerticalAccessCount = 2;
	RootRequest.ProfileSnapshot.MinVerticalAccessCount = 2;
	RootRequest.ProfileSnapshot.MaxVerticalAccessCount = 2;

	TArray<FLayoutPlannedCell> ParentPlannedCells;
	FLayoutPlannedCell& FirstParentVerticalAccess = ParentPlannedCells.AddDefaulted_GetRef();
	FirstParentVerticalAccess.Cell = FIntVector(2, 2, 0);
	FirstParentVerticalAccess.Intent = ELayoutCellIntent::VerticalAccess;
	FLayoutPlannedCell& SecondParentVerticalAccess = ParentPlannedCells.AddDefaulted_GetRef();
	SecondParentVerticalAccess.Cell = FIntVector(4, 2, 0);
	SecondParentVerticalAccess.Intent = ELayoutCellIntent::VerticalAccess;

	TSet<FIntVector> ReservedParentCells;
	ReservedParentCells.Add(SecondParentVerticalAccess.Cell);

	TArray<LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider> ChildProviders;
	LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider& ContributingChild = ChildProviders.AddDefaulted_GetRef();
	ContributingChild.bSupportsVerticalAccess = true;
	ContributingChild.bContributesHostVerticalAccess = true;
	ContributingChild.VerticalAccessLocalCells = {
		FIntVector(1, 1, 0),
		FIntVector(1, 1, 1)
	};
	ContributingChild.PlannedCells = {
		{FIntVector(1, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::VerticalAccess}
	};
	FLayoutCommittedEndpointAnchor& LowerContributingAnchor = ContributingChild.EndpointCommitments.AddDefaulted_GetRef();
	LowerContributingAnchor.CommitmentId = TEXT("LowerDoor");
	LowerContributingAnchor.LocalCell = FIntVector(1, 1, 0);
	LowerContributingAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	LowerContributingAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	FLayoutCommittedEndpointAnchor& UpperContributingAnchor = ContributingChild.EndpointCommitments.AddDefaulted_GetRef();
	UpperContributingAnchor.CommitmentId = TEXT("UpperDoor");
	UpperContributingAnchor.LocalCell = FIntVector(1, 1, 1);
	UpperContributingAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	UpperContributingAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	ContributingChild.ChildRegionDebugPath = TEXT("Root/ChildContributing");

	LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider& LocalOnlyChild = ChildProviders.AddDefaulted_GetRef();
	LocalOnlyChild.bSupportsVerticalAccess = true;
	LocalOnlyChild.bContributesHostVerticalAccess = false;
	LocalOnlyChild.VerticalAccessLocalCells = {
		FIntVector(2, 1, 0),
		FIntVector(2, 1, 1)
	};
	LocalOnlyChild.PlannedCells = {
		{FIntVector(2, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(2, 1, 1), ELayoutCellIntent::VerticalAccess}
	};
	LocalOnlyChild.ChildRegionDebugPath = TEXT("Root/ChildLocalOnly");

	const LayoutProfileSolverInternal::FRecursiveHostVerticalAccessComposition Composition =
		LayoutProfileSolverInternal::DetermineRecursiveHostVerticalAccessCompositionForSchedule(
			RootRequest,
			ParentPlannedCells,
			ReservedParentCells,
			ChildProviders);

	TestEqual(TEXT("Composed host vertical-access requirement is preserved"), Composition.RequiredHostProviderCount, 2);
	TestEqual(TEXT("One unreserved parent provider remains counted"), Composition.CountedParentProviderCount, 1);
	TestEqual(TEXT("One child provider is counted to satisfy the remaining host requirement"), Composition.CountedChildRegionDebugPaths.Num(), 1);
	TestEqual(TEXT("The counted child provider is deterministic"), Composition.CountedChildRegionDebugPaths[0], FString(TEXT("Root/ChildContributing")));
	TestEqual(TEXT("Local-only child providers remain ignored for host counting"), Composition.LocalOnlyChildRegionDebugPaths.Num(), 1);
	TestEqual(TEXT("The local-only child provider is reported separately"), Composition.LocalOnlyChildRegionDebugPaths[0], FString(TEXT("Root/ChildLocalOnly")));
	TestTrue(TEXT("Satisfied composed host count does not report a failure reason"), Composition.FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionTreeSolveVerticalAccessContributionDiagnosticsTest,
	"PorismExtension.Layout.Solver.RegionTree.VerticalAccessContributionDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionTreeSolveVerticalAccessContributionDiagnosticsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("HostVerticalAccessDiagnostics");
	RootRequest.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	RootRequest.ProfileSnapshot.VerticalAccessCount = 2;
	RootRequest.ProfileSnapshot.MinVerticalAccessCount = 2;
	RootRequest.ProfileSnapshot.MaxVerticalAccessCount = 2;

	TArray<FLayoutPlannedCell> ParentPlannedCells;
	FLayoutPlannedCell& ParentVerticalAccess = ParentPlannedCells.AddDefaulted_GetRef();
	ParentVerticalAccess.Cell = FIntVector(2, 2, 0);
	ParentVerticalAccess.Intent = ELayoutCellIntent::VerticalAccess;

	TSet<FIntVector> ReservedParentCells;
	ReservedParentCells.Add(ParentVerticalAccess.Cell);

	TArray<LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider> ChildProviders;
	LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider& LocalOnlyChild = ChildProviders.AddDefaulted_GetRef();
	LocalOnlyChild.bSupportsVerticalAccess = true;
	LocalOnlyChild.bContributesHostVerticalAccess = false;
	LocalOnlyChild.VerticalAccessLocalCells = {FIntVector(1, 1, 0)};
	LocalOnlyChild.ChildRegionDebugPath = TEXT("Root/ChildLocalOnly");

	LayoutProfileSolverInternal::FRecursiveVerticalAccessProvider& UnusableContributingChild = ChildProviders.AddDefaulted_GetRef();
	UnusableContributingChild.bSupportsVerticalAccess = false;
	UnusableContributingChild.bContributesHostVerticalAccess = true;
	UnusableContributingChild.ChildRegionDebugPath = TEXT("Root/ChildContributingButUnusable");

	const LayoutProfileSolverInternal::FRecursiveHostVerticalAccessComposition Composition =
		LayoutProfileSolverInternal::DetermineRecursiveHostVerticalAccessCompositionForSchedule(
			RootRequest,
			ParentPlannedCells,
			ReservedParentCells,
			ChildProviders);

	TestEqual(TEXT("The unresolved host requirement remains on the parent side when no child can contribute upward"), Composition.CountedParentProviderCount, 2);
	TestEqual(TEXT("No child providers are counted when none are both contributing and usable"), Composition.CountedChildRegionDebugPaths.Num(), 0);
	TestEqual(TEXT("Local-only providers are called out explicitly"), Composition.LocalOnlyChildRegionDebugPaths.Num(), 1);
	TestEqual(TEXT("Unusable contributing providers are called out explicitly"), Composition.UnusableContributingChildRegionDebugPaths.Num(), 1);
	TestFalse(TEXT("Unsatisfied host composition reports a failure reason"), Composition.FailureReason.IsEmpty());
	TestTrue(TEXT("Failure reason explains ignored local-only child vertical access"), Composition.FailureReason.Contains(TEXT("Local-only child vertical access")));
	TestTrue(TEXT("Failure reason explains unusable host-contributing child vertical access"), Composition.FailureReason.Contains(TEXT("Host-contributing child entries without usable vertical access")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRegionTreeProfileLocalNestedDispatchTest,
	"PorismExtension.Layout.Solver.RegionTree.ProfileLocalNestedDispatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionTreeProfileLocalNestedDispatchTest::RunTest(const FString& Parameters)
{
	auto* Outer = CreateRegionTreeSolveTestOuter(TEXT("ProfileLocalNestedDispatch"));
	auto* Module = CreateOpenFillModule(Outer, TEXT("NestedShell"),
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
	auto MakeRegion = [&](const TCHAR* Name, int32 Side, ULayoutProfileAsset* Child, bool bPreserve)
	{
		FLayoutRegionContentEntry ModuleEntry;
		ModuleEntry.EntryId = TEXT("Shell");
		ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ModuleEntry.ModuleSettings.Module = Module;
		TArray<FLayoutRegionContentEntry> Entries{ModuleEntry};
		if (Child != nullptr)
		{
			auto& Entry = Entries.AddDefaulted_GetRef();
			Entry.EntryId = TEXT("Child");
			Entry.ContentKind = ELayoutRegionContentKind::ChildRegion;
			Entry.ChildRegionSettings.RegionProfile = Child;
			Entry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;
		}
		auto* Profile = CreateProfile(Outer, Name, FIntPoint(Side, Side), FIntPoint(Side, Side), 1, 0, false);
		Profile->ContentSet = CreateRegionContentSet(Outer, *FString::Printf(TEXT("%s_Content"), Name), Entries);
		if (bPreserve)
		{
			FLayoutSparsePreserveTerrainRule Rule;
			Rule.RuleId = TEXT("MiddleInterior");
			Rule.PlacementZone = ELayoutPlacementZone::Interior;
			Profile->SparsePlacementRules.Add(FInstancedStruct::Make(Rule));
		}
		return Profile;
	};
	auto* Grandchild = MakeRegion(TEXT("NormalGrandchild"), 3, nullptr, false);
	auto* Child = MakeRegion(TEXT("SparseChild"), 7, Grandchild, true);
	auto* Root = MakeRegion(TEXT("NormalRoot"), 9, Child, false);
	const auto Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Root, 913, TEXT("Root"));
	LayoutSolveExecution::FScope Scope(0.0, 50000);
	const auto Result = FLayoutProfileSolver::SolveRegionTree(Request);
	if (!TestTrue(TEXT("Normal/sparse/normal hierarchy solves"), Result.bSucceeded))
	{
		AddError(Result.FailureReason);
		return false;
	}
	TestTrue(TEXT("Nested child enters sparse dispatch"), LayoutSolveExecution::CurrentThreadLedger()->SparseWork > 0);
	TestEqual(TEXT("Normal root shell, sparse child shell and full normal grandchild remain"), Result.MergedSolveResult.Placements.Num(), 65);
	TestEqual(TEXT("Only the middle profile preserves unclaimed Interior"), Result.MergedSolveResult.ResidualUnoccupiedCells.Num(), 16);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRegionalNoAscentHandoffPolicyTest,
	"PorismExtension.Layout.Solver.RegionTree.NoAscentHandoffPreservesChildPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionalNoAscentHandoffPolicyTest::RunTest(const FString& Parameters)
{
	using namespace LayoutProfileSolverInternal;
	for (const bool bSparse : {false, true})
	{
		for (const bool bRequired : {false, true})
		{
			auto* Outer = CreateRegionTreeSolveTestOuter(TEXT("RegionalPolicy"));
			auto* Profile = CreateProfileWithUniversalContentSet(Outer, TEXT("ChildPolicy"),
				FIntPoint(5, 5), FIntPoint(5, 5), 1, 1, false);
			Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
			Profile->bRequireAllTraversalChannelsReachable = bRequired;
			if (bSparse)
			{
				FLayoutSparsePreserveTerrainRule Rule;
				Rule.RuleId = TEXT("ChildInterior");
				Rule.PlacementZone = ELayoutPlacementZone::Interior;
				Profile->SparsePlacementRules.Add(FInstancedStruct::Make(Rule));
			}
			auto Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 8173, TEXT("Root/Child"));
			const auto Topology = BuildRequestBackedRegionPreparedTopology(Request);
			if (!TestTrue(TEXT("Child topology prepares"), Topology.SolveResult.bSucceeded)) return false;
			Request.PlannedCells = Topology.SolveResult.PlannedCells;
			Request.PrecomputedPlannedCells = Request.PlannedCells;
			const auto* Entry = Request.PlannedCells.FindByPredicate([](const auto& Cell)
				{ return Cell.Intent == ELayoutCellIntent::Entry; });
			if (!TestNotNull(TEXT("Child has its own Entry"), Entry)) return false;
			auto& Anchor = Request.CommittedTraversalAnchors.AddDefaulted_GetRef();
			Anchor.Cell = Entry->Cell;
			Anchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

			FLayoutDirectChildRegionCommitment Handoff;
			Handoff.ParentRegionDebugPath = TEXT("Root");
			Handoff.ChildRegionDebugPath = Request.RegionDebugPath;
			Handoff.NegotiatedResponsibilityContract.ParentRegionDebugPath = Handoff.ParentRegionDebugPath;
			Handoff.NegotiatedResponsibilityContract.ChildRegionDebugPath = Handoff.ChildRegionDebugPath;
			FLayoutRegionSolveRequest Delegated;
			FString Failure;
			if (!TestTrue(TEXT("Independent handoff builds"), BuildIndependentCommittedChildProofRequestFromHandoff(
				Handoff, Request, Delegated, Failure))) return false;
			TestEqual(TEXT("Child retains its own traversal policy"), Delegated.ProfileSnapshot.bRequireAllTraversalChannelsReachable, bRequired);
			TestEqual(TEXT("Child retains its own sparse rules"), Delegated.ProfileSnapshot.SparsePlacementRules.Num(), bSparse ? 1 : 0);
			TestFalse(TEXT("No parent ascent obligation means no traversal deferral"), Delegated.bDeferTraversalValidationToSchedule);
			TestEqual(TEXT("Handoff retains child-local traversal obligations"), Delegated.CommittedTraversalAnchors.Num(), 1);
			LayoutSolveExecution::FScope Scope(0.0, 50000);
			const auto Result = SolveRequestBackedRegion(Delegated);
			if (!TestTrue(TEXT("Child solves independently under its policy"), Result.SolveResult.bSucceeded))
			{
				AddError(Result.SolveResult.FailureReason);
				return false;
			}
			TestEqual(TEXT("Sparse dispatch follows the child's profile"), LayoutSolveExecution::CurrentThreadLedger()->SparseWork > 0, bSparse);

			auto& Endpoint = Handoff.EndpointCommitments.AddDefaulted_GetRef();
			Endpoint.CommitmentId = TEXT("VerticalContact");
			Endpoint.LocalCell = Entry->Cell;
			Endpoint.FaceDirection = ELayoutFaceDirection::PosZ;
			Endpoint.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
			if (!TestTrue(TEXT("No-ascent endpoint handoff builds"), BuildIndependentCommittedChildProofRequestFromHandoff(
				Handoff, Request, Delegated, Failure))) return false;
			TestFalse(TEXT("No ascent demand cannot relax vertical endpoint channels"), Delegated.CommittedEndpointAnchors[0].TraversalChannels.IsEmpty());
			Handoff.NegotiatedResponsibilityContract.RequiredHostProviderCount = 1;
			if (!TestTrue(TEXT("Parent-ascent endpoint handoff builds"), BuildIndependentCommittedChildProofRequestFromHandoff(
				Handoff, Request, Delegated, Failure))) return false;
			TestTrue(TEXT("Actual parent ascent retains deferred traversal"), Delegated.bDeferTraversalValidationToSchedule);
			TestTrue(TEXT("Actual parent ascent relaxes vertical endpoint channels"), Delegated.CommittedEndpointAnchors[0].TraversalChannels.IsEmpty());
		}
	}
	return true;
}
