// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositePreparedPrefixSeedsUpperShadowBeforeRouteDomainsTest,
	"PorismExtension.Layout.Solver.Composite.PreparedPrefixSeedsUpperShadowBeforeRouteDomains",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositePreparedPrefixSeedsUpperShadowBeforeRouteDomainsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutComposite_PreparedPrefix"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutTemplate_CompositePreparedPrefix"),
		FIntVector(8, 8, 8));

	ULayoutModuleAsset* LowerStair = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositePreparedPrefix_LowerStair"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});
	ULayoutModuleAsset* UpperLanding = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositePreparedPrefix_UpperLanding"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}))
		});
	ULayoutModuleAsset* UpperNeighbor = CreateModule(
		Outer,
		TEXT("LayoutModule_CompositePreparedPrefix_UpperNeighbor"),
		Template,
		{ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceEntry}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		});

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(
		Outer,
		TEXT("LayoutComposite_PreparedPrefix"));
	FLayoutCompositeModuleCell& LowerCell = Composite->Cells.AddDefaulted_GetRef();
	LowerCell.Module = LowerStair;
	LowerCell.LocalCell = FIntVector(0, 0, 0);
	FLayoutCompositeModuleCell& UpperCell = Composite->Cells.AddDefaulted_GetRef();
	UpperCell.Module = UpperLanding;
	UpperCell.LocalCell = FIntVector(0, 0, 1);

	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Prepared-prefix composite validates"), CompositeValidation.IsValid()))
	{
		return false;
	}

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("CompositePreparedPrefix");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;
	FLayoutRegionContentEntry NeighborEntry;
	NeighborEntry.EntryId = TEXT("CompositePreparedPrefixNeighbor");
	NeighborEntry.ContentKind = ELayoutRegionContentKind::Module;
	NeighborEntry.ModuleSettings.Module = UpperNeighbor;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_CompositePreparedPrefix"),
		{CompositeEntry, NeighborEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_CompositePreparedPrefix"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		0,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		6021,
		TEXT("CompositePreparedPrefix"));
	Request.FootprintSize = FIntPoint(2, 1);
	Request.PlannedCells.Reset();
	for (const TPair<FIntVector, ELayoutCellIntent>& Planned : {
		TPair<FIntVector, ELayoutCellIntent>(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess),
		TPair<FIntVector, ELayoutCellIntent>(FIntVector(0, 0, 1), ELayoutCellIntent::Interior),
		TPair<FIntVector, ELayoutCellIntent>(FIntVector(1, 0, 1), ELayoutCellIntent::Entry)})
	{
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = Planned.Key;
		PlannedCell.ModuleLevelIndex = Planned.Key.Z;
		PlannedCell.Intent = Planned.Value;
	}
	for (FLayoutModuleSolveSnapshot& Snapshot : Request.ModuleCatalog.Modules)
	{
		Snapshot.AllowedYawRotationSteps = Snapshot.SourceCompositeModule != nullptr
			? TArray<int32>{0}
			: TArray<int32>{0, 1};
	}

	LayoutProfileSolverInternal::FSolveContext Context =
		LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(Request);
	if (!TestTrue(
		TEXT("Prepared-prefix request reaches route-domain completion"),
		LayoutProfileSolverInternal::PrepareSolveContextThroughRouteDomainStageForTests(Context)))
	{
		if (!Context.Result.FailureReason.IsEmpty())
		{
			AddError(Context.Result.FailureReason);
		}
		return false;
	}

	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* RootPlacement =
		Context.Placements.Find(FIntVector(0, 0, 0));
	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ShadowPlacement =
		Context.Placements.Find(FIntVector(0, 0, 1));
	TestNotNull(TEXT("Deterministic composite root is fixed before route domains finish"), RootPlacement);
	TestNotNull(TEXT("Deterministic upper shadow is fixed before route domains finish"), ShadowPlacement);
	if (RootPlacement == nullptr || ShadowPlacement == nullptr)
	{
		return false;
	}
	TestTrue(TEXT("Prepared upper occupancy remains a non-root shadow"), !ShadowPlacement->bBundleRoot);
	TestEqual(TEXT("Prepared upper occupancy points to lower composite root"), ShadowPlacement->BundleRootCell, FIntVector(0, 0, 0));

	const TArray<LayoutProfileSolverInternal::FSolveCandidate>* NeighborDomain =
		Context.InitialDomains.Find(FIntVector(1, 0, 1));
	if (!TestNotNull(TEXT("Adjacent upper-level cell keeps a prepared domain"), NeighborDomain))
	{
		return false;
	}
	TestEqual(TEXT("Fixed shadow prunes incompatible adjacent yaw before CSP search"), NeighborDomain->Num(), 1);
	if (NeighborDomain->Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Prepared neighbor retains the yaw compatible with the immutable upper leaf"), (*NeighborDomain)[0].YawRotationSteps, 0);
	if ((*NeighborDomain)[0].YawRotationSteps != 0)
	{
		return false;
	}

	const FLayoutRegionSolveResult SolveResult = FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Full solve succeeds with shadow-local route constraints"), SolveResult.SolveResult.bSucceeded))
	{
		AddError(SolveResult.SolveResult.FailureReason);
		return false;
	}
	TestTrue(TEXT("Solved result exports the composite root once"), SolveResult.SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(0, 0, 0)
			&& Placement.OccupiedLocalCells.Num() == 2;
	}));
	TestFalse(TEXT("Solved result does not export the upper shadow as an independent root"), SolveResult.SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(0, 0, 1);
	}));
	return true;
}
