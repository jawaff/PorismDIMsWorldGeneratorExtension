// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseChildBoundaryEmptyParentTest,
	"PorismExtension.Layout.Solver.TerrainResidualSparse.ChildBoundaryAllowsEmptyParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Keeps preserved parent space empty only when the selected child domain permits that occupancy. */
bool FLayoutSparseChildBoundaryEmptyParentTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutSparseChildBoundary"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("Parent"), FIntPoint(5, 5), FIntPoint(5, 5), 1, 0, false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Profile->bRequireAllTraversalChannelsReachable = false;
	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("Parent");
	Request.Seed = 31;
	Request.FootprintSize = FIntPoint(5, 5);
	Request.ProfileSnapshot = FLayoutProfileSolver::BuildProfileSnapshot(Profile);
	Request.ModuleCatalog = FLayoutProfileSolver::BuildModuleCatalog(Profile->ContentSet);
	FLayoutSparsePlacementRuleSolveSnapshot& Preserve = Request.ProfileSnapshot.SparsePlacementRules.AddDefaulted_GetRef();
	Preserve.RuleId = TEXT("PreservedInterior");
	Preserve.RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;
	Preserve.PlacementZone = ELayoutPlacementZone::Interior;
	Preserve.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
	const FIntVector ChildCell(2, 2, 0);
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			if (FIntVector(X, Y, 0) == ChildCell) continue;
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = X == 0 || Y == 0 || X == 4 || Y == 4
				? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
		}
	}
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ChildCell"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Wall = CreateModule(Outer, TEXT("ChildWall"), Template, {ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	FLayoutModuleCatalog ChildCatalog;
	ChildCatalog.Modules.Add(FLayoutProfileSolver::BuildModuleSnapshot(Wall, 1));
	ChildCatalog.Modules[0].AllowedYawRotationSteps = {0};
	Request.ChildModuleCatalogs.Add(TEXT("Parent/Child"), ChildCatalog);
	FLayoutRegionContentSetSolveSnapshot ChildContent;
	FLayoutRegionContentEntrySolveSnapshot& ChildEntry = ChildContent.Entries.AddDefaulted_GetRef();
	ChildEntry.ContentKind = ELayoutRegionContentKind::Module;
	ChildEntry.ModuleSnapshotIndex = 0;
	Request.ChildContentSetSnapshots.Add(TEXT("Parent/Child"), ChildContent);
	FLayoutPlannedCell ChildPlan;
	ChildPlan.Cell = ChildCell;
	ChildPlan.ModuleLevelIndex = 0;
	ChildPlan.Intent = ELayoutCellIntent::Boundary;
	Request.ChildPlannedCellsByRegion.Add(TEXT("Parent/Child"), {ChildPlan});
	FLayoutNegotiatedChildResponsibilityContract& Contract = Request.NegotiatedChildResponsibilityContracts.AddDefaulted_GetRef();
	Contract.ParentRegionDebugPath = Request.RegionDebugPath;
	Contract.ChildRegionDebugPath = TEXT("Parent/Child");
	FLayoutNegotiatedLevelCellSet& Replacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	Replacement.Level = 0;
	Replacement.Cells = {ChildCell};

	LayoutProfileSolverInternal::FSolveContext Context;
	TestTrue(TEXT("Child wall that permits empty neighbors preserves the parent domain"),
		LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(Request, Context, false));
	const TArray<LayoutProfileSolverInternal::FSolveCandidate>* Domain = Context.InitialDomains.Find(FIntVector(1, 2, 0));
	TestTrue(TEXT("Child-adjacent preserved cell retains its empty candidate"), Domain != nullptr && Domain->ContainsByPredicate(
		[](const LayoutProfileSolverInternal::FSolveCandidate& Candidate) { return Candidate.bEmpty; }));

	Request.ChildModuleCatalogs[TEXT("Parent/Child")].Modules[0].EffectiveFaceRules.NegX.OccupancyPolicy =
		ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	TestTrue(TEXT("Required child support remains solvable with filled parent content"),
		LayoutProfileSolverInternal::TryPrepareRequestSolveContextThroughRouteDomainStage(Request, Context, false));
	Domain = Context.InitialDomains.Find(FIntVector(1, 2, 0));
	TestTrue(TEXT("Child-required filled support cannot be silently left empty"), Domain != nullptr && !Domain->ContainsByPredicate(
		[](const LayoutProfileSolverInternal::FSolveCandidate& Candidate) { return Candidate.bEmpty; }));
	LayoutProfileSolverInternal::FSolveContext Local;
	FString FailureReason;
	const auto HasFilledLocalCandidate = [](const LayoutProfileSolverInternal::FSolveContext& LocalContext)
	{
		const TArray<LayoutProfileSolverInternal::FSolveCandidate>* LocalDomain = LocalContext.InitialDomains.Find(FIntVector(1, 2, 0));
		return LocalDomain && LocalDomain->ContainsByPredicate(
			[](const LayoutProfileSolverInternal::FSolveCandidate& Candidate) { return !Candidate.bEmpty; });
	};
	TestTrue(TEXT("Child-facing local support prepares without parent route proof"),
		LayoutProfileSolverInternal::TryPrepareSparseStructuralLocalSolveView(Request, {FIntVector(1, 2, 0)}, Local, FailureReason));
	TestTrue(TEXT("Exact child domain admits compatible filled local support"), HasFilledLocalCandidate(Local));
	Request.ChildModuleCatalogs[TEXT("Parent/Child")].Modules[0].EffectiveFaceRules.NegX.OccupancyPolicy =
		ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
	TestTrue(TEXT("Child-empty contract retains local topology"),
		LayoutProfileSolverInternal::TryPrepareSparseStructuralLocalSolveView(Request, {FIntVector(1, 2, 0)}, Local, FailureReason));
	TestFalse(TEXT("Local occupied claim cannot bypass actual child's empty-face contract"), HasFilledLocalCandidate(Local));
	Request.ChildModuleCatalogs[TEXT("Parent/Child")].Modules[0].EffectiveFaceRules.NegX.OccupancyPolicy =
		ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	Request.ChildModuleCatalogs[TEXT("Parent/Child")].Modules[0].AllowedYawRotationSteps.Reset();
	TestTrue(TEXT("Missing child candidates do not manufacture exterior topology"),
		LayoutProfileSolverInternal::TryPrepareSparseStructuralLocalSolveView(Request, {FIntVector(1, 2, 0)}, Local, FailureReason));
	TestFalse(TEXT("Removing child contact domain invalidates local support"), HasFilledLocalCandidate(Local));
	return true;
}
