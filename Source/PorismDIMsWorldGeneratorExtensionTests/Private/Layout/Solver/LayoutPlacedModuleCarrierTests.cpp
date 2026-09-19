// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreatePlacedModuleCarrierTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	FInstancedStruct MakePlacedModuleCarrierSparseRule(
		const FName RuleId,
		ULayoutRegionContentSetAsset* ContentSet)
	{
		FLayoutSparseExactPlacementRule Rule;
		Rule.RuleId = RuleId;
		Rule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
		Rule.ContentSet = ContentSet;
		Rule.Count = 1;
		Rule.PlacementZone = ELayoutPlacementZone::Any;
		return FInstancedStruct::Make(Rule);
	}

	FLayoutRegionSolveRequest BuildSingleCellCarrierRequest(
		ULayoutRegionContentSetAsset* ContentSet,
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const ELayoutCellIntent Intent)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FIntPoint(1, 1);
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = Intent;
		return Request;
	}

	FLayoutId BuildRequestModuleBundleId(
		const FLayoutRegionSolveRequest& Request,
		const int32 ModuleIndex)
	{
		return FLayoutId(*FString::Printf(
			TEXT("%s.Bundle.%d.%s"),
			*Request.EffectiveSnapshotId.ToString(),
			ModuleIndex,
			*Request.ModuleCatalog.Modules[ModuleIndex].SnapshotId.ToString()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierPreservesOccupiedCellsOnStandaloneSolveTest,
	"PorismExtension.Layout.Solver.Profile.PlacedModulePreservesOccupiedCellsOnStandaloneSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierPreservesOccupiedCellsOnSparsePlacementTest,
	"PorismExtension.Layout.Solver.ResidualPlacement.PlacedModulePreservesOccupiedCellsOnSparsePlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierBuildsExportedBoundaryPointsFromCompositeBundleMetadataTest,
	"PorismExtension.Layout.Solver.Profile.ExportedBoundaryPointsUseCompositeBundleMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationPreservesVariantBackedPlacementTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationPreservesVariantBackedPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationPreservesNonVerticalVariantBackedPlacementTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationPreservesNonVerticalVariantBackedPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationMarksNonVerticalUpperPlannedFaceTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationMarksNonVerticalUpperPlannedFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationDoesNotPropagateAcrossStructuralOverlapTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationDoesNotPropagateAcrossStructuralOverlap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierFinalAdjacencyAuditAcceptsVariantBackedPlacementTest,
	"PorismExtension.Layout.Solver.Profile.FinalAdjacencyAuditAcceptsVariantBackedPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierFinalAdjacencyAuditAcceptsCompositeBundleLocalFacesTest,
	"PorismExtension.Layout.Solver.Profile.FinalAdjacencyAuditAcceptsCompositeBundleLocalFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationUsesVariantBackedTopFaceTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationUsesVariantBackedTopFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationUsesShadowLocalTopFaceTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationUsesShadowLocalTopFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierVerticalContinuationCarriesCurrentLevelBundleShadowRowTest,
	"PorismExtension.Layout.Solver.Profile.VerticalContinuationCarriesCurrentLevelBundleShadowRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierFinalAdjacencyAuditSkipsVariantBackedNonVerticalCrossLevelNeighborTest,
	"PorismExtension.Layout.Solver.Profile.FinalAdjacencyAuditSkipsVariantBackedNonVerticalCrossLevelNeighbor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierFinalAdjacencyAuditKeepsContractBackedNonVerticalStackSeparateTest,
	"PorismExtension.Layout.Solver.Profile.FinalAdjacencyAuditKeepsContractBackedNonVerticalStackSeparate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierReachabilityPrefersVariantBackedCandidateAttachmentTest,
	"PorismExtension.Layout.Solver.Profile.ReachabilityPrefersVariantBackedCandidateAttachment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierPlacedReachabilityUsesCompositeDerivedTraversalLinksTest,
	"PorismExtension.Layout.Solver.Profile.PlacedReachabilityUsesCompositeDerivedTraversalLinks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierExportsOnlyBundleRootPlacementsTest,
	"PorismExtension.Layout.Solver.Profile.ExportsOnlyBundleRootPlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierExportsLeafBundleBoundsFromOccupiedFootprintTest,
	"PorismExtension.Layout.Solver.Profile.ExportsLeafBundleBoundsFromOccupiedFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierRejectsProjectedBundleCellConflictTest,
	"PorismExtension.Layout.Solver.Profile.RejectsProjectedBundleCellConflict",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierRejectsProjectedBundleCellWithoutExternalPolicyTest,
	"PorismExtension.Layout.Solver.Profile.RejectsProjectedBundleCellWithoutExternalPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierAllowsProjectedBundleCellOnExternalPlannedNeighborFaceTest,
	"PorismExtension.Layout.Solver.Profile.AllowsProjectedBundleCellOnExternalPlannedNeighborFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSparseFixedNeighborsPreserveBundleLocalCellsTest,
	"PorismExtension.Layout.Solver.Profile.SparseFixedNeighborsPreserveBundleLocalCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSparseFixedNeighborsUseCompositeBoundsWithoutBundleBoundsTest,
	"PorismExtension.Layout.Solver.Profile.SparseFixedNeighborsUseCompositeBoundsWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSparseFixedNeighborsRebuildLeafOccupiedCellsWithoutCarrierTest,
	"PorismExtension.Layout.Solver.Profile.SparseFixedNeighborsRebuildLeafOccupiedCellsWithoutCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSolveResultOccupiedWorldCellsRebuildLeafOccupiedCellsWithoutCarrierTest,
	"PorismExtension.Layout.Solver.Profile.SolveResultOccupiedWorldCellsRebuildLeafOccupiedCellsWithoutCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierExportedBoundaryPointsUseCompositeBoundsWithoutBundleBoundsTest,
	"PorismExtension.Layout.Solver.Profile.ExportedBoundaryPointsUseCompositeBoundsWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierShadowPlacementUsesLocalBundleCellFaceRulesTest,
	"PorismExtension.Layout.Solver.Profile.ShadowPlacementUsesLocalBundleCellFaceRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierForwardCheckUsesShadowBundleNeighborsTest,
	"PorismExtension.Layout.Solver.Profile.ForwardCheckUsesShadowBundleNeighbors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSolveContextPreservesForcedBundleInsertionsTest,
	"PorismExtension.Layout.Solver.Profile.SolveContextPreservesForcedBundleInsertions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSolveContextPreservesRequiredRouteConstraintsTest,
	"PorismExtension.Layout.Solver.Profile.SolveContextPreservesRequiredRouteConstraints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierForcedBundleInsertionAnchorsSelectedBundleTest,
	"PorismExtension.Layout.Solver.Profile.ForcedBundleInsertionAnchorsSelectedBundle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierRequiredRouteConstraintFiltersRootDomainTest,
	"PorismExtension.Layout.Solver.Profile.RequiredRouteConstraintFiltersRootDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlacedModuleCarrierPreservesOccupiedCellsOnStandaloneSolveTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierStandalone"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierStandaloneTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierStandaloneModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierStandaloneProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("StandaloneModule");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("PlacedCarrierStandaloneContentSet"), {Entry});

	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(
		FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 903, TEXT("PlacedCarrier/Standalone")));
	TestTrue(TEXT("Standalone solve succeeds"), RegionResult.SolveResult.bSucceeded);
	TestEqual(TEXT("Standalone solve produces one placement"), RegionResult.SolveResult.Placements.Num(), 1);
	if (!RegionResult.SolveResult.bSucceeded || RegionResult.SolveResult.Placements.Num() != 1)
	{
		if (!RegionResult.SolveResult.FailureReason.IsEmpty())
		{
			AddError(RegionResult.SolveResult.FailureReason);
		}
		return false;
	}

	const FLayoutPlacedModule& Placement = RegionResult.SolveResult.Placements[0];
	TestEqual(TEXT("Placement keeps the authored content identity"), Placement.SourceContentEntryId, Entry.EntryId);
	TestTrue(TEXT("Placement keeps the authored module snapshot identity"),
		Placement.ModuleSnapshotId == FLayoutProfileSolver::BuildModuleSnapshot(Module).SnapshotId);
	TestNull(TEXT("Standalone solve exports no live module pointer"), Placement.Module.Get());
	TestNull(TEXT("Standalone module placement does not report a composite source"), Placement.CompositeModule.Get());
	TestEqual(TEXT("Standalone module placement preserves one occupied local cell"), Placement.OccupiedLocalCells.Num(), 1);
	if (Placement.OccupiedLocalCells.Num() == 1)
	{
		TestEqual(TEXT("Standalone module placement preserves the canonical occupied local cell"), Placement.OccupiedLocalCells[0], FIntVector::ZeroValue);
	}

	return true;
}

bool FLayoutPlacedModuleCarrierSolveContextPreservesForcedBundleInsertionsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierForcedBundleInsertions"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierForcedInsertionTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierForcedInsertionModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("ForcedInsertionEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierForcedInsertionContentSet"),
		{ModuleEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierForcedInsertionProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionSolveRequest Request = BuildSingleCellCarrierRequest(
		ContentSet,
		Profile,
		904,
		TEXT("PlacedCarrier/ForcedInsertion"),
		ELayoutCellIntent::Boundary);
	FLayoutForcedPlacementBundleInsertion& ForcedInsertion =
		Request.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	ForcedInsertion.BundleId = TEXT("SteppedSupportBundle");
	ForcedInsertion.AnchorCell = FIntVector(1, 0, 0);
	ForcedInsertion.ProvingCell = FIntVector(0, 0, 0);

	const LayoutProfileSolverInternal::FSolveContext Context =
		LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(Request);

	if (!TestEqual(
		TEXT("Solve context preserves one forced bundle insertion from the frozen request carrier"),
		Context.ForcedPlacementBundleInsertions.Num(),
		1))
	{
		return false;
	}

	TestEqual(
		TEXT("Solve context preserves the forced bundle id"),
		Context.ForcedPlacementBundleInsertions[0].BundleId,
		FLayoutId(TEXT("SteppedSupportBundle")));
	TestEqual(
		TEXT("Solve context preserves the forced bundle anchor cell"),
		Context.ForcedPlacementBundleInsertions[0].AnchorCell,
		FIntVector(1, 0, 0));
	TestEqual(
		TEXT("Solve context preserves the forced bundle proving cell"),
		Context.ForcedPlacementBundleInsertions[0].ProvingCell,
		FIntVector(0, 0, 0));
	return true;
}

bool FLayoutPlacedModuleCarrierSolveContextPreservesRequiredRouteConstraintsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierRouteConstraints"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierRouteConstraintTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierRouteConstraintModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("PlacedCarrierRouteConstraintModuleEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierRouteConstraintContentSet"),
		{ModuleEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierRouteConstraintProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionSolveRequest Request = BuildSingleCellCarrierRequest(
		ContentSet,
		Profile,
		905,
		TEXT("PlacedCarrier/RequiredRouteConstraint"),
		ELayoutCellIntent::Boundary);
	FLayoutRouteConstraintRecord& RouteConstraint =
		Request.RequiredRouteConstraints.AddDefaulted_GetRef();
	RouteConstraint.ConstraintId = TEXT("PlacedCarrier.RequiredRoute");
	RouteConstraint.Cell = FIntVector::ZeroValue;
	RouteConstraint.Intent = ELayoutCellIntent::Boundary;
	RouteConstraint.bScoreAsMainRoute = true;
	FLayoutRouteFaceRequirement& FaceRequirement = RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
	FaceRequirement.FaceDirection = ELayoutFaceDirection::PosX;
	FaceRequirement.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const LayoutProfileSolverInternal::FSolveContext Context =
		LayoutProfileSolverInternal::BuildSolveContextFromRequestForTests(Request);

	if (!TestEqual(
		TEXT("Solve context preserves one required route constraint from the frozen request carrier"),
		Context.RequiredRouteConstraints.Num(),
		1))
	{
		return false;
	}

	TestEqual(
		TEXT("Solve context preserves the required route constraint id"),
		Context.RequiredRouteConstraints[0].ConstraintId,
		FLayoutId(TEXT("PlacedCarrier.RequiredRoute")));
	TestEqual(
		TEXT("Solve context preserves the required route constraint cell"),
		Context.RequiredRouteConstraints[0].Cell,
		FIntVector::ZeroValue);
	TestEqual(
		TEXT("Solve context preserves the required route face count"),
		Context.RequiredRouteConstraints[0].FaceRequirements.Num(),
		1);
	TestEqual(
		TEXT("Solve context preserves the required route face direction"),
		Context.RequiredRouteConstraints[0].FaceRequirements[0].FaceDirection,
		ELayoutFaceDirection::PosX);
	TestTrue(
		TEXT("Solve context preserves the required route traversal channel"),
		Context.RequiredRouteConstraints[0].FaceRequirements[0].TraversalChannel == LayoutGameplayTags::TraversalPrimary);
	return true;
}

bool FLayoutPlacedModuleCarrierForcedBundleInsertionAnchorsSelectedBundleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierForcedBundleInsertionSolve"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierForcedBundleInsertionSolveTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* PreferredModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierForcedBundlePreferredModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutModuleAsset* ForcedModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierForcedBundleForcedModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry PreferredEntry;
	PreferredEntry.EntryId = TEXT("PreferredModule");
	PreferredEntry.ContentKind = ELayoutRegionContentKind::Module;
	PreferredEntry.Weight = 100;
	PreferredEntry.ModuleSettings.Module = PreferredModule;
	FLayoutRegionContentEntry ForcedEntry;
	ForcedEntry.EntryId = TEXT("ForcedModule");
	ForcedEntry.ContentKind = ELayoutRegionContentKind::Module;
	ForcedEntry.Weight = 1;
	ForcedEntry.ModuleSettings.Module = ForcedModule;

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierForcedBundleInsertionSolveProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierForcedBundleInsertionSolveContentSet"),
		{PreferredEntry, ForcedEntry});

	FLayoutRegionSolveRequest BaselineRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		905,
		TEXT("PlacedCarrier/ForcedBundleInsertion/Baseline"));
	/* !BaselineRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	BaselineRequest.FootprintSize = FIntPoint(1, 1);
	{
		FLayoutPlannedCell& PlannedCell = BaselineRequest.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
	}

	const FLayoutRegionSolveResult BaselineResult = FLayoutProfileSolver::SolveRegion(BaselineRequest);
	TestTrue(TEXT("Baseline solve succeeds before forced insertion"), BaselineResult.SolveResult.bSucceeded);
	if (!BaselineResult.SolveResult.bSucceeded || !TestEqual(TEXT("Baseline solve produces one placement"), BaselineResult.SolveResult.Placements.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Baseline solve selects the higher-weight preferred module"),
		BaselineResult.SolveResult.Placements[0].SourceContentEntryId,
		PreferredEntry.EntryId);

	FLayoutRegionSolveRequest ForcedRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		905,
		TEXT("PlacedCarrier/ForcedBundleInsertion/Forced"));
	/* !ForcedRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	ForcedRequest.FootprintSize = FIntPoint(1, 1);
	{
		FLayoutPlannedCell& PlannedCell = ForcedRequest.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
	}
	FLayoutForcedPlacementBundleInsertion& ForcedInsertion =
		ForcedRequest.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	ForcedInsertion.BundleId = BuildRequestModuleBundleId(ForcedRequest, 1);
	ForcedInsertion.AnchorCell = FIntVector::ZeroValue;
	ForcedInsertion.ProvingCell = FIntVector::ZeroValue;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(ForcedRequest);

	const FLayoutRegionSolveResult ForcedResult = FLayoutProfileSolver::SolveRegion(ForcedRequest);
	TestTrue(TEXT("Forced bundle insertion solve succeeds"), ForcedResult.SolveResult.bSucceeded);
	if (!ForcedResult.SolveResult.bSucceeded || !TestEqual(TEXT("Forced bundle insertion solve produces one placement"), ForcedResult.SolveResult.Placements.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Forced bundle insertion redirects the root solve onto the anchored lower-weight module"),
		ForcedResult.SolveResult.Placements[0].SourceContentEntryId,
		ForcedEntry.EntryId);
	TestTrue(TEXT("Forced bundle insertion request keeps a passing request assertion when the selected bundle exists on the request snapshots"), ForcedRequest.ValidationAssertions.ContainsByPredicate(
		[](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.ForcedPlacementBundleInsertionContractValid")
				&& Assertion.bPassed;
		}));
	return true;
}

bool FLayoutPlacedModuleCarrierRequiredRouteConstraintFiltersRootDomainTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierRequiredRouteConstraintSolve"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierRequiredRouteConstraintSolveTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* PreferredButBlockedModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierRequiredRouteBlockedModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutModuleAsset* RoutedModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierRequiredRouteRoutedModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	FLayoutRegionContentEntry PreferredEntry;
	PreferredEntry.EntryId = TEXT("PlacedCarrierRequiredRoutePreferred");
	PreferredEntry.ContentKind = ELayoutRegionContentKind::Module;
	PreferredEntry.Weight = 100;
	PreferredEntry.ModuleSettings.Module = PreferredButBlockedModule;
	FLayoutRegionContentEntry RoutedEntry;
	RoutedEntry.EntryId = TEXT("PlacedCarrierRequiredRouteRouted");
	RoutedEntry.ContentKind = ELayoutRegionContentKind::Module;
	RoutedEntry.Weight = 1;
	RoutedEntry.ModuleSettings.Module = RoutedModule;
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierRequiredRouteConstraintSolveProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierRequiredRouteConstraintSolveContent"),
		{PreferredEntry, RoutedEntry});

	FLayoutRegionSolveRequest BaselineRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		906,
		TEXT("PlacedCarrier/RequiredRouteConstraint/Baseline"));
	/* !BaselineRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	BaselineRequest.FootprintSize = FIntPoint(1, 1);
	{
		FLayoutPlannedCell& PlannedCell = BaselineRequest.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = ELayoutCellIntent::Boundary;
	}

	const FLayoutRegionSolveResult BaselineResult = FLayoutProfileSolver::SolveRegion(BaselineRequest);
	TestTrue(TEXT("Baseline solve succeeds before required route constraints"), BaselineResult.SolveResult.bSucceeded);
	if (!BaselineResult.SolveResult.bSucceeded || !TestEqual(TEXT("Baseline solve produces one placement"), BaselineResult.SolveResult.Placements.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Baseline solve selects the higher-weight blocked module"), BaselineResult.SolveResult.Placements[0].SourceContentEntryId, PreferredEntry.EntryId);

	FLayoutRegionSolveRequest RoutedRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		906,
		TEXT("PlacedCarrier/RequiredRouteConstraint/Required"));
	/* !RoutedRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RoutedRequest.FootprintSize = FIntPoint(1, 1);
	{
		FLayoutPlannedCell& PlannedCell = RoutedRequest.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = ELayoutCellIntent::Boundary;
	}
	FLayoutRouteConstraintRecord& RouteConstraint =
		RoutedRequest.RequiredRouteConstraints.AddDefaulted_GetRef();
	RouteConstraint.ConstraintId = TEXT("PlacedCarrier.RequiredRouteConstraint.PosX");
	RouteConstraint.Cell = FIntVector::ZeroValue;
	RouteConstraint.Intent = ELayoutCellIntent::Boundary;
	RouteConstraint.bScoreAsMainRoute = true;
	FLayoutRouteFaceRequirement& FaceRequirement = RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
	FaceRequirement.FaceDirection = ELayoutFaceDirection::PosX;
	FaceRequirement.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(RoutedRequest);

	const FLayoutRegionSolveResult RoutedResult = FLayoutProfileSolver::SolveRegion(RoutedRequest);
	TestTrue(TEXT("Required route constraint solve succeeds"), RoutedResult.SolveResult.bSucceeded);
	if (!RoutedResult.SolveResult.bSucceeded || !TestEqual(TEXT("Required route constraint solve produces one placement"), RoutedResult.SolveResult.Placements.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("Required route constraint filters the root domain onto the routed module"), RoutedResult.SolveResult.Placements[0].SourceContentEntryId, RoutedEntry.EntryId);
	TestTrue(TEXT("Required route constraint request keeps a passing request assertion when the constrained cell and traversal face are valid"), RoutedRequest.ValidationAssertions.ContainsByPredicate(
		[](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.RequiredRouteConstraintContractValid")
				&& Assertion.bPassed;
		}));
	return true;
}

bool FLayoutPlacedModuleCarrierPreservesOccupiedCellsOnSparsePlacementTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierSparse"));

	UChunkStructureTemplate* BoundaryTemplate = CreateTemplate(Outer, TEXT("PlacedCarrierSparseBoundaryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* BoundaryModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierSparseBoundaryModule"),
		BoundaryTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	UChunkStructureTemplate* ScatterTemplate = CreateTemplate(Outer, TEXT("PlacedCarrierSparseScatterTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* ScatterModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierSparseScatterModule"),
		ScatterTemplate,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry StructuralEntry;
	StructuralEntry.EntryId = TEXT("UnusedBoundaryOnly");
	StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
	StructuralEntry.ModuleSettings.Module = BoundaryModule;
	ULayoutRegionContentSetAsset* StructuralContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierSparseStructuralContentSet"),
		{StructuralEntry});

	FLayoutRegionContentEntry SparseEntry;
	SparseEntry.EntryId = TEXT("ResidualScatter");
	SparseEntry.ContentKind = ELayoutRegionContentKind::Module;
	SparseEntry.ModuleSettings.Module = ScatterModule;
	ULayoutRegionContentSetAsset* SparseContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierSparseContentSet"),
		{SparseEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierSparseProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false,
		true,
		1);
	Profile->ContentSet = StructuralContentSet;
	Profile->SparsePlacementRules.Add(MakePlacedModuleCarrierSparseRule(TEXT("InteriorScatter"), SparseContentSet));

	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(
		BuildSingleCellCarrierRequest(
			StructuralContentSet,
			Profile,
			907,
			TEXT("PlacedCarrier/Sparse"),
			ELayoutCellIntent::Interior));
	TestTrue(TEXT("Sparse placement solve succeeds"), RegionResult.SolveResult.bSucceeded);
	TestEqual(TEXT("Sparse placement solve produces one placement"), RegionResult.SolveResult.Placements.Num(), 1);
	if (!RegionResult.SolveResult.bSucceeded || RegionResult.SolveResult.Placements.Num() != 1)
	{
		if (!RegionResult.SolveResult.FailureReason.IsEmpty())
		{
			AddError(RegionResult.SolveResult.FailureReason);
		}
		return false;
	}

	const FLayoutPlacedModule& Placement = RegionResult.SolveResult.Placements[0];
	TestEqual(TEXT("Sparse placement continues to resolve one occupied cell"), Placement.Intent, ELayoutCellIntent::Interior);
	TestNull(TEXT("Sparse placement does not report a composite source"), Placement.CompositeModule.Get());
	TestEqual(TEXT("Sparse placement preserves one occupied local cell"), Placement.OccupiedLocalCells.Num(), 1);
	if (Placement.OccupiedLocalCells.Num() == 1)
	{
		TestEqual(TEXT("Sparse placement preserves the canonical occupied local cell"), Placement.OccupiedLocalCells[0], FIntVector::ZeroValue);
	}

	return true;
}

bool FLayoutPlacedModuleCarrierBuildsExportedBoundaryPointsFromCompositeBundleMetadataTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierBoundaryExport"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierCompositeTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* LowerLeaf = CreateModule(
		Outer,
		TEXT("PlacedCarrierCompositeLowerLeaf"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutModuleAsset* UpperLeaf = CreateModule(
		Outer,
		TEXT("PlacedCarrierCompositeUpperLeaf"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutCompositeModuleCell LowerCell;
	LowerCell.Module = LowerLeaf;
	LowerCell.LocalCell = FIntVector(0, 0, 0);
	LowerCell.RelativeYawRotationSteps = 0;

	FLayoutCompositeModuleCell UpperCell;
	UpperCell.Module = UpperLeaf;
	UpperCell.LocalCell = FIntVector(0, 0, 1);
	UpperCell.RelativeYawRotationSteps = 0;

	ULayoutCompositeModuleAsset* CompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierCompositeModule"));
	CompositeModule->Cells = {LowerCell, UpperCell};

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositePair");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierCompositeContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierCompositeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);

	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		911,
		TEXT("PlacedCarrier/CompositeBoundaryExport"));
	TestEqual(TEXT("Composite request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector::ZeroValue;
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.CompositeModule = CompositeModule;
	Placement.YawRotationSteps = 0;
	Placement.SourceContentEntryId = Entry.EntryId;
	Placement.OccupiedLocalCells = Snapshot.OccupiedLocalCells;

	const TArray<FLayoutSolveBoundaryPoint> BoundaryPoints =
		LayoutProfileSolverInternal::BuildExportedBoundaryPointsFromSolveResultForTests(Request, SolveResult);

	TestEqual(TEXT("Composite bundle exports every exposed side face plus top and bottom"), BoundaryPoints.Num(), 10);
	TestTrue(TEXT("Composite bundle exports boundary points for the upper occupied cell"), BoundaryPoints.ContainsByPredicate(
		[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
		{
			return BoundaryPoint.LocalCell == FIntVector(0, 0, 1);
		}));
	TestTrue(TEXT("Composite bundle exports the upper top face"), BoundaryPoints.ContainsByPredicate(
		[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
		{
			return BoundaryPoint.LocalCell == FIntVector(0, 0, 1)
				&& BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosZ;
		}));
	TestTrue(TEXT("Composite bundle exports the lower bottom face"), BoundaryPoints.ContainsByPredicate(
		[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
		{
			return BoundaryPoint.LocalCell == FIntVector(0, 0, 0)
				&& BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegZ;
		}));

	return true;
}

bool FLayoutPlacedModuleCarrierVerticalContinuationPreservesVariantBackedPlacementTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierVerticalContinuationVariantContract"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierVerticalContinuationVariantContractTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierVerticalContinuationVariantContractModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}))
		});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Vertical continuation contract snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	{
		FLayoutPlannedCell& LowerCell = PlannedCells.AddDefaulted_GetRef();
		LowerCell.Cell = FIntVector(0, 0, 0);
		LowerCell.Intent = ELayoutCellIntent::VerticalAccess;

		FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
		UpperCell.Cell = FIntVector(0, 0, 1);
		UpperCell.Intent = ELayoutCellIntent::VerticalAccess;
	}

	TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> SolvedPlacements;
	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement = SolvedPlacements.Add(FIntVector(0, 0, 0));
	Placement.YawRotationSteps = 0;
	Placement.VariantIndex = 0;
	Placement.bEmpty = false;
	Placement.ModuleSnapshotIndex = 0;
	Placement.ModuleSnapshotId = Snapshot.SnapshotId;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> Continuations =
		LayoutProfileSolverInternal::BuildVerticalContinuationFixedNeighborsForTests(PlannedCells, SolvedPlacements, 1, {Snapshot});

	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ContinuedPlacement = Continuations.Find(FIntVector(0, 0, 0));
	TestNotNull(TEXT("Vertical continuation keeps the lower solved placement when the snapshot-backed variant exports an upward vertical-access contract"), ContinuedPlacement);
	if (ContinuedPlacement == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Vertical continuation preserves the solved module snapshot id"), ContinuedPlacement->ModuleSnapshotId, Snapshot.SnapshotId);
	TestEqual(TEXT("Vertical continuation preserves the solved variant index"), ContinuedPlacement->VariantIndex, 0);
	return true;
}

bool FLayoutPlacedModuleCarrierVerticalContinuationPreservesNonVerticalVariantBackedPlacementTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierVerticalContinuationNonVerticalVariantContract"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierVerticalContinuationNonVerticalVariantContractTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierVerticalContinuationNonVerticalVariantContractModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}))
		});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Non-vertical continuation contract snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	{
		FLayoutPlannedCell& LowerCell = PlannedCells.AddDefaulted_GetRef();
		LowerCell.Cell = FIntVector(0, 0, 0);
		LowerCell.Intent = ELayoutCellIntent::Boundary;

		FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
		UpperCell.Cell = FIntVector(0, 0, 1);
		UpperCell.Intent = ELayoutCellIntent::Boundary;
	}

	TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> SolvedPlacements;
	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement = SolvedPlacements.Add(FIntVector(0, 0, 0));
	Placement.YawRotationSteps = 0;
	Placement.VariantIndex = 0;
	Placement.bEmpty = false;
	Placement.ModuleSnapshotIndex = 0;
	Placement.ModuleSnapshotId = Snapshot.SnapshotId;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> Continuations =
		LayoutProfileSolverInternal::BuildVerticalContinuationFixedNeighborsForTests(
			PlannedCells,
			SolvedPlacements,
			1,
			{Snapshot});

	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ContinuedPlacement =
		Continuations.Find(FIntVector(0, 0, 0));
	TestNotNull(
		TEXT("Vertical continuation keeps the lower solved placement when a non-vertical planned cell still exports a real upward vertical-access contract"),
		ContinuedPlacement);
	if (ContinuedPlacement == nullptr)
	{
		return false;
	}

	TestEqual(
		TEXT("Non-vertical upward continuation preserves the solved module snapshot id"),
		ContinuedPlacement->ModuleSnapshotId,
		Snapshot.SnapshotId);
	TestEqual(
		TEXT("Non-vertical upward continuation preserves the solved variant index"),
		ContinuedPlacement->VariantIndex,
		0);
	return true;
}

bool FLayoutPlacedModuleCarrierVerticalContinuationMarksNonVerticalUpperPlannedFaceTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	{
		FLayoutPlannedCell& LowerCell = PlannedCells.AddDefaulted_GetRef();
		LowerCell.Cell = FIntVector(2, 1, 0);
		LowerCell.Intent = ELayoutCellIntent::Boundary;

		FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
		UpperCell.Cell = FIntVector(2, 1, 1);
		UpperCell.Intent = ELayoutCellIntent::Boundary;
	}

	const TMap<FIntVector, uint8> UpwardContinuationFaces =
		LayoutProfileSolverInternal::BuildVerticalContinuationUpwardContinuationFaceMasksForTests(
			PlannedCells,
			0);

	const uint8* LowerFaceMask = UpwardContinuationFaces.Find(FIntVector(2, 1, 0));
	TestNotNull(TEXT("Rebuilt continuation marks the lower non-vertical planned cell when an unresolved planned cell still exists directly above it"), LowerFaceMask);
	if (LowerFaceMask == nullptr)
	{
		return false;
	}

	const uint8 PosZMask =
		static_cast<uint8>(
			1u << static_cast<uint8>(ELayoutFaceDirection::PosZ));
	TestEqual(TEXT("Rebuilt continuation records the exact upward planned-neighbor face instead of leaving the non-vertical column on the reserved-external fallback"), *LowerFaceMask, PosZMask);
	return true;
}

bool FLayoutPlacedModuleCarrierVerticalContinuationDoesNotPropagateAcrossStructuralOverlapTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierNonVerticalUpwardTraversal"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierNonVerticalUpwardTraversalTemplate"), FIntVector(8, 8, 8));
	FLayoutInternalAccessLink UpwardAccess;
	UpwardAccess.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	UpwardAccess.ToTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	UpwardAccess.bBidirectional = true;

	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierNonVerticalUpwardTraversalModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}))
		},
		FGameplayTagContainer(),
		{UpwardAccess});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Non-vertical upward traversal snapshot validation succeeds"), Snapshot.Validation.IsValid());
	TestTrue(FString::Printf(TEXT("Non-vertical upward traversal snapshot exports a vertical-access contract (count=%d)"), Snapshot.DerivedVerticalAccessContracts.Num()), !Snapshot.DerivedVerticalAccessContracts.IsEmpty());
	if (!Snapshot.Validation.IsValid() || Snapshot.DerivedVerticalAccessContracts.IsEmpty())
	{
		return false;
	}

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.FootprintSize = FIntPoint(3, 3);
	Context.Result.FootprintSize = Context.FootprintSize;
	Context.ModuleSnapshots.Add(Snapshot);

	FLayoutPlannedCell& LowerCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	LowerCell.Cell = FIntVector(1, 1, 0);
	LowerCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(LowerCell.Cell, LowerCell.Intent);

	FLayoutPlannedCell& UpperCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(1, 1, 1);
	UpperCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(UpperCell.Cell, UpperCell.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleSnapshotId = Snapshot.SnapshotId;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.DerivedInternalTraversalLinks = Snapshot.DerivedInternalTraversalLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement =
		Context.Placements.Add(FIntVector(1, 1, 0));
	Placement.YawRotationSteps = 0;
	Placement.VariantIndex = 0;
	Placement.bEmpty = false;
	Placement.ModuleSnapshotIndex = 0;
	Placement.ModuleSnapshotId = Snapshot.SnapshotId;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	const LayoutProfileSolverInternal::FSolveCellFaceInterfaceSet* LowerInterfaces =
		Context.CompiledFaceInterfaces.Find(LowerCell.Cell);
	TestNotNull(TEXT("Lower non-vertical cell has compiled face interfaces"), LowerInterfaces);
	if (LowerInterfaces != nullptr)
	{
		TestEqual(
			TEXT("Lower non-vertical cell classifies its upper cell as structural overlap"),
			LowerInterfaces->Faces[static_cast<int32>(ELayoutFaceDirection::PosZ)].NeighborKind,
			LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::StructuralVerticalOverlap);
	}
	const TSet<FGameplayTag> CurrentLevelActiveAreas = {
		LayoutGameplayTags::TraversalPrimary};
	const TSet<FGameplayTag> NextLevelActiveAreas =
		LayoutProfileSolverInternal::BuildNextLevelActiveWalkableAreasForTests(
			Context,
			CurrentLevelActiveAreas,
			0);

	TestFalse(TEXT("Structural vertical overlap does not propagate next-level traversal despite a module vertical-access contract"), NextLevelActiveAreas.Contains(LayoutGameplayTags::TraversalPrimary));
	return !NextLevelActiveAreas.Contains(LayoutGameplayTags::TraversalPrimary);
}

bool FLayoutPlacedModuleCarrierVerticalContinuationCarriesCurrentLevelBundleShadowRowTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierVerticalContinuationShadowRow"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierVerticalContinuationShadowRowTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierVerticalContinuationShadowRowModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}))
		});

	const FLayoutModuleSolveSnapshot Snapshot =
		FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(
		TEXT("Shadow-row continuation contract snapshot validation succeeds"),
		Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	{
		FLayoutPlannedCell& LowerCell = PlannedCells.AddDefaulted_GetRef();
		LowerCell.Cell = FIntVector(0, 0, 0);
		LowerCell.Intent = ELayoutCellIntent::VerticalAccess;

		FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
		UpperCell.Cell = FIntVector(0, 0, 1);
		UpperCell.Intent = ELayoutCellIntent::Interior;
	}

	TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> SolvedPlacements;
	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& RootPlacement = SolvedPlacements.Add(FIntVector(0, 0, 0));
	RootPlacement.YawRotationSteps = 0;
	RootPlacement.VariantIndex = 0;
	RootPlacement.bEmpty = false;
	RootPlacement.ModuleSnapshotIndex = 0;
	RootPlacement.ModuleSnapshotId = Snapshot.SnapshotId;
	RootPlacement.BundleRootCell = FIntVector(0, 0, 0);
	RootPlacement.LocalBundleCell = FIntVector(0, 0, 0);
	RootPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& ShadowPlacement = SolvedPlacements.Add(FIntVector(0, 0, 1));
	ShadowPlacement.YawRotationSteps = 0;
	ShadowPlacement.VariantIndex = 0;
	ShadowPlacement.bEmpty = false;
	ShadowPlacement.ModuleSnapshotIndex = 0;
	ShadowPlacement.ModuleSnapshotId = Snapshot.SnapshotId;
	ShadowPlacement.BundleRootCell = FIntVector(0, 0, 0);
	ShadowPlacement.LocalBundleCell = FIntVector(0, 0, 1);
	ShadowPlacement.bBundleRoot = false;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> Continuations =
		LayoutProfileSolverInternal::BuildVerticalContinuationFixedNeighborsForTests(
			PlannedCells,
			SolvedPlacements,
			1,
			{Snapshot});

	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ContinuedRootPlacement = Continuations.Find(FIntVector(0, 0, 0));
	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ContinuedShadowPlacement = Continuations.Find(FIntVector(0, 0, 1));
	TestNotNull(TEXT("Vertical continuation keeps the lower bundle root placement from the real upward contract instead of the lower intent enum"), ContinuedRootPlacement);
	TestNotNull(TEXT("Vertical continuation also carries the current-level bundle shadow row"), ContinuedShadowPlacement);
	if (ContinuedRootPlacement == nullptr || ContinuedShadowPlacement == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Carried shadow row stays non-root"), !ContinuedShadowPlacement->bBundleRoot);
	TestEqual(TEXT("Carried shadow row keeps the original bundle root cell"), ContinuedShadowPlacement->BundleRootCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Carried shadow row keeps its local bundle cell"), ContinuedShadowPlacement->LocalBundleCell, FIntVector(0, 0, 1));
	return true;
}

bool FLayoutPlacedModuleCarrierFinalAdjacencyAuditAcceptsVariantBackedPlacementTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierAdjacencyAudit"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierAdjacencyTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierAdjacencyModule"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierContinuationTopFaceCompositeCarrier"));
	Snapshot.SourceModule = nullptr;

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::Interior;
	Context.PlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;
	Context.ModuleSnapshots.Add(Snapshot);

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement = Context.Placements.Add(FIntVector::ZeroValue);
	Placement.YawRotationSteps = 0;
	Placement.VariantIndex = 0;
	Placement.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateSolvedPlacementAdjacencyForTests(Context, FailureReason);
	TestTrue(TEXT("Final adjacency audit accepts an occupied variant-backed placement without a live module pointer"), bValid);
	if (!bValid && !FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}

	return bValid;
}

bool FLayoutPlacedModuleCarrierFinalAdjacencyAuditAcceptsCompositeBundleLocalFacesTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierCompositeAdjacencyAudit"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierCompositeAdjacencyTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* RoomLeaf = CreateModule(
		Outer,
		TEXT("PlacedCarrierCompositeAdjacencyRoomLeaf"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	ULayoutCompositeModuleAsset* CompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierCompositeAdjacencyComposite"));
	FLayoutCompositeModuleCell& RootCell = CompositeModule->Cells.AddDefaulted_GetRef();
	RootCell.LocalCell = FIntVector(0, 0, 0);
	RootCell.Module = RoomLeaf;
	RootCell.RelativeYawRotationSteps = 0;
	FLayoutCompositeModuleCell& ShadowCell = CompositeModule->Cells.AddDefaulted_GetRef();
	ShadowCell.LocalCell = FIntVector(1, 0, 0);
	ShadowCell.Module = RoomLeaf;
	ShadowCell.RelativeYawRotationSteps = 0;

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeRoomPair");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierCompositeAdjacencyContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierCompositeAdjacencyProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);

	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveRequest Request =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 1003, TEXT("PlacedCarrier/CompositeAdjacency"));
	TestEqual(TEXT("Composite-backed request builds one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	TestTrue(TEXT("Composite-backed request preserves the composite source"), Snapshot.SourceCompositeModule == CompositeModule);

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(2, 1);
	Context.ModuleSnapshots.Add(Snapshot);

	FLayoutPlannedCell& PlannedRoot = Context.Result.PlannedCells.AddDefaulted_GetRef();
	PlannedRoot.Cell = FIntVector(0, 0, 0);
	PlannedRoot.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(PlannedRoot.Cell, PlannedRoot.Intent);

	FLayoutPlannedCell& PlannedShadow = Context.Result.PlannedCells.AddDefaulted_GetRef();
	PlannedShadow.Cell = FIntVector(1, 0, 0);
	PlannedShadow.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(PlannedShadow.Cell, PlannedShadow.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& RootPlacement = Context.Placements.Add(FIntVector(0, 0, 0));
	RootPlacement.YawRotationSteps = 0;
	RootPlacement.VariantIndex = 0;
	RootPlacement.bEmpty = false;
	RootPlacement.BundleRootCell = FIntVector(0, 0, 0);
	RootPlacement.LocalBundleCell = FIntVector(0, 0, 0);
	RootPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& PlacedShadow = Context.Placements.Add(FIntVector(1, 0, 0));
	PlacedShadow.YawRotationSteps = 0;
	PlacedShadow.VariantIndex = 0;
	PlacedShadow.bEmpty = false;
	PlacedShadow.BundleRootCell = FIntVector(0, 0, 0);
	PlacedShadow.LocalBundleCell = FIntVector(1, 0, 0);
	PlacedShadow.bBundleRoot = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateSolvedPlacementAdjacencyForTests(Context, FailureReason);
	TestTrue(TEXT("Final adjacency audit accepts a composite-backed bundle through local exposed faces and internal glue suppression"), bValid);
	if (!bValid && !FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}

	return bValid;
}

bool FLayoutPlacedModuleCarrierVerticalContinuationUsesVariantBackedTopFaceTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierContinuationTopFace"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierContinuationTopFaceTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierContinuationTopFaceModule"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	{
		FLayoutPlannedCell& LowerCell = PlannedCells.AddDefaulted_GetRef();
		LowerCell.Cell = FIntVector(0, 0, 0);
		LowerCell.Intent = ELayoutCellIntent::Interior;

		FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
		UpperCell.Cell = FIntVector(0, 0, 1);
		UpperCell.Intent = ELayoutCellIntent::Interior;
	}

	TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> SolvedPlacements;
	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement = SolvedPlacements.Add(FIntVector(0, 0, 0));
	Placement.YawRotationSteps = 0;
	Placement.VariantIndex = 0;
	Placement.bEmpty = false;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> Continuations =
		LayoutProfileSolverInternal::BuildVerticalContinuationFixedNeighborsForTests(
			PlannedCells,
			SolvedPlacements,
			1,
			{Snapshot});

	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ContinuedPlacement = Continuations.Find(FIntVector(0, 0, 0));
	TestNotNull(TEXT("Variant-backed non-vertical placement is preserved when only the snapshot-backed composite-like variant carries the filled top-face requirement"), ContinuedPlacement);
	if (ContinuedPlacement == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Continuation preserves the solved variant index"), ContinuedPlacement->VariantIndex, 0);
	return true;
}

bool FLayoutPlacedModuleCarrierVerticalContinuationUsesShadowLocalTopFaceTest::RunTest(const FString& Parameters)
{
	FLayoutModuleSolveSnapshot Snapshot;
	Snapshot.AllowedYawRotationSteps = {0};
	Snapshot.BoundsCells = FIntVector(1, 2, 1);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};

	FLayoutLocalCellFaceRuleSnapshot& RootCellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	RootCellSnapshot.LocalCell = FIntVector(0, 0, 0);
	RootCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosZ,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutLocalCellFaceRuleSnapshot& ShadowCellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	ShadowCellSnapshot.LocalCell = FIntVector(0, 1, 0);
	ShadowCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosZ,
		LayoutGameplayTags::FaceSolid,
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor));

	TArray<FLayoutPlannedCell> PlannedCells;
	{
		FLayoutPlannedCell& LowerCell = PlannedCells.AddDefaulted_GetRef();
		LowerCell.Cell = FIntVector(0, 1, 0);
		LowerCell.Intent = ELayoutCellIntent::Interior;

		FLayoutPlannedCell& UpperCell = PlannedCells.AddDefaulted_GetRef();
		UpperCell.Cell = FIntVector(0, 1, 1);
		UpperCell.Intent = ELayoutCellIntent::Interior;
	}

	TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> SolvedPlacements;
	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& ShadowPlacement = SolvedPlacements.Add(FIntVector(0, 1, 0));
	ShadowPlacement.YawRotationSteps = 0;
	ShadowPlacement.VariantIndex = 0;
	ShadowPlacement.bEmpty = false;
	ShadowPlacement.BundleRootCell = FIntVector(0, 0, 0);
	ShadowPlacement.LocalBundleCell = FIntVector(0, 1, 0);
	ShadowPlacement.bBundleRoot = false;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> Continuations =
		LayoutProfileSolverInternal::BuildVerticalContinuationFixedNeighborsForTests(
			PlannedCells,
			SolvedPlacements,
			1,
			{Snapshot});

	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ContinuedPlacement = Continuations.Find(FIntVector(0, 1, 0));
	TestNotNull(TEXT("Vertical continuation keeps the lower shadow cell when only its local top face requires a filled upper neighbor"), ContinuedPlacement);
	if (ContinuedPlacement == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Shadow-local continuation keeps the solved variant index"), ContinuedPlacement->VariantIndex, 0);
	TestEqual(TEXT("Shadow-local continuation keeps the shadow local bundle cell"), ContinuedPlacement->LocalBundleCell, FIntVector(0, 1, 0));
	TestTrue(TEXT("Shadow-local continuation remains a non-root carried placement"), !ContinuedPlacement->bBundleRoot);
	return true;
}

bool FLayoutPlacedModuleCarrierFinalAdjacencyAuditSkipsVariantBackedNonVerticalCrossLevelNeighborTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierCrossLevelSkip"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierCrossLevelSkipTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* LowerModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierCrossLevelLowerModule"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	ULayoutModuleAsset* UpperModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierCrossLevelUpperModule"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	const FLayoutModuleSolveSnapshot LowerSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(LowerModule, 1);
	FLayoutModuleSolveSnapshot UpperSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(UpperModule, 1);
	TestTrue(TEXT("Lower snapshot validation succeeds"), LowerSnapshot.Validation.IsValid());
	TestTrue(TEXT("Upper snapshot validation succeeds"), UpperSnapshot.Validation.IsValid());
	if (!LowerSnapshot.Validation.IsValid() || !UpperSnapshot.Validation.IsValid())
	{
		return false;
	}

	UpperSnapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierCrossLevelUpperCompositeCarrier"));
	UpperSnapshot.SourceModule = nullptr;

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 1);
	Context.ModuleSnapshots = {LowerSnapshot, UpperSnapshot};

	FLayoutPlannedCell& LowerCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	LowerCell.Cell = FIntVector(0, 0, 0);
	LowerCell.Intent = ELayoutCellIntent::Interior;
	Context.PlannedCellIntents.Add(LowerCell.Cell, LowerCell.Intent);

	FLayoutPlannedCell& UpperCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(0, 0, 1);
	UpperCell.Intent = ELayoutCellIntent::Interior;
	Context.PlannedCellIntents.Add(UpperCell.Cell, UpperCell.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& LowerVariant = Context.Variants.AddDefaulted_GetRef();
	LowerVariant.ModuleSnapshotIndex = 0;
	LowerVariant.ModuleDebugName = LowerSnapshot.DebugName;
	LowerVariant.YawRotationSteps = 0;
	LowerVariant.WorldFaceRules = LowerSnapshot.EffectiveFaceRules;
	LowerVariant.WorldSpanOffers = LowerSnapshot.DerivedSpanOffers;
	LowerVariant.ClosureProviderIntents = LowerSnapshot.ClosureProviderIntents;
	LowerVariant.TraversalChannels = LowerSnapshot.TraversalChannels;
	LowerVariant.InternalAccessLinks = LowerSnapshot.InternalAccessLinks;
	LowerVariant.VerticalAccessContracts = LowerSnapshot.DerivedVerticalAccessContracts;
	LowerVariant.Roles = LowerSnapshot.Roles;
	LowerVariant.SupportedCellIntents = LowerSnapshot.SupportedCellIntents;
	LowerVariant.MinTraversableNeighborFaces = LowerSnapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& UpperVariant = Context.Variants.AddDefaulted_GetRef();
	UpperVariant.ModuleSnapshotIndex = 1;
	UpperVariant.ModuleDebugName = UpperSnapshot.DebugName;
	UpperVariant.YawRotationSteps = 0;
	UpperVariant.WorldFaceRules = UpperSnapshot.EffectiveFaceRules;
	UpperVariant.WorldSpanOffers = UpperSnapshot.DerivedSpanOffers;
	UpperVariant.ClosureProviderIntents = UpperSnapshot.ClosureProviderIntents;
	UpperVariant.TraversalChannels = UpperSnapshot.TraversalChannels;
	UpperVariant.InternalAccessLinks = UpperSnapshot.InternalAccessLinks;
	UpperVariant.VerticalAccessContracts = UpperSnapshot.DerivedVerticalAccessContracts;
	UpperVariant.Roles = UpperSnapshot.Roles;
	UpperVariant.SupportedCellIntents = UpperSnapshot.SupportedCellIntents;
	UpperVariant.MinTraversableNeighborFaces = UpperSnapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& LowerPlacement = Context.Placements.Add(FIntVector(0, 0, 0));
	LowerPlacement.YawRotationSteps = 0;
	LowerPlacement.VariantIndex = 0;
	LowerPlacement.bEmpty = false;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& UpperPlacement = Context.Placements.Add(FIntVector(0, 0, 1));
	UpperPlacement.YawRotationSteps = 0;
	UpperPlacement.VariantIndex = 1;
	UpperPlacement.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateSolvedPlacementAdjacencyForTests(Context, FailureReason);
	TestTrue(TEXT("Final adjacency audit skips the non-vertical cross-level neighbor check even when the supporting upper placement is variant-backed"), bValid);
	if (!bValid && !FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}

	return bValid;
}

bool FLayoutPlacedModuleCarrierFinalAdjacencyAuditKeepsContractBackedNonVerticalStackSeparateTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierCrossLevelContractAudit"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierCrossLevelContractAuditTemplate"), FIntVector(8, 8, 8));
	FLayoutInternalAccessLink UpwardAccess;
	UpwardAccess.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	UpwardAccess.ToTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	UpwardAccess.bBidirectional = true;

	ULayoutModuleAsset* LowerModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierCrossLevelContractLowerModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}))
		},
		FGameplayTagContainer(),
		{UpwardAccess});

	ULayoutModuleAsset* UpperModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierCrossLevelContractUpperModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});

	const FLayoutModuleSolveSnapshot LowerSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(LowerModule, 1);
	FLayoutModuleSolveSnapshot UpperSnapshot = FLayoutProfileSolver::BuildModuleSnapshot(UpperModule, 1);
	TestTrue(TEXT("Lower contract-audit snapshot validation succeeds"), LowerSnapshot.Validation.IsValid());
	TestTrue(TEXT("Lower contract-audit snapshot exports an upward contract"), !LowerSnapshot.DerivedVerticalAccessContracts.IsEmpty());
	TestTrue(TEXT("Upper contract-audit snapshot validation succeeds"), UpperSnapshot.Validation.IsValid());
	if (!LowerSnapshot.Validation.IsValid() || LowerSnapshot.DerivedVerticalAccessContracts.IsEmpty() || !UpperSnapshot.Validation.IsValid())
	{
		return false;
	}

	UpperSnapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierCrossLevelContractUpperCompositeCarrier"));
	UpperSnapshot.SourceModule = nullptr;

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 1);
	Context.ModuleSnapshots = {LowerSnapshot, UpperSnapshot};

	FLayoutPlannedCell& LowerCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	LowerCell.Cell = FIntVector(0, 0, 0);
	LowerCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(LowerCell.Cell, LowerCell.Intent);

	FLayoutPlannedCell& UpperCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(0, 0, 1);
	UpperCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(UpperCell.Cell, UpperCell.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& LowerVariant = Context.Variants.AddDefaulted_GetRef();
	LowerVariant.ModuleSnapshotIndex = 0;
	LowerVariant.ModuleDebugName = LowerSnapshot.DebugName;
	LowerVariant.YawRotationSteps = 0;
	LowerVariant.WorldFaceRules = LowerSnapshot.EffectiveFaceRules;
	LowerVariant.WorldSpanOffers = LowerSnapshot.DerivedSpanOffers;
	LowerVariant.ClosureProviderIntents = LowerSnapshot.ClosureProviderIntents;
	LowerVariant.TraversalChannels = LowerSnapshot.TraversalChannels;
	LowerVariant.InternalAccessLinks = LowerSnapshot.InternalAccessLinks;
	LowerVariant.VerticalAccessContracts = LowerSnapshot.DerivedVerticalAccessContracts;
	LowerVariant.Roles = LowerSnapshot.Roles;
	LowerVariant.SupportedCellIntents = LowerSnapshot.SupportedCellIntents;
	LowerVariant.MinTraversableNeighborFaces = LowerSnapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& UpperVariant = Context.Variants.AddDefaulted_GetRef();
	UpperVariant.ModuleSnapshotIndex = 1;
	UpperVariant.ModuleDebugName = UpperSnapshot.DebugName;
	UpperVariant.YawRotationSteps = 0;
	UpperVariant.WorldFaceRules = UpperSnapshot.EffectiveFaceRules;
	UpperVariant.WorldSpanOffers = UpperSnapshot.DerivedSpanOffers;
	UpperVariant.ClosureProviderIntents = UpperSnapshot.ClosureProviderIntents;
	UpperVariant.TraversalChannels = UpperSnapshot.TraversalChannels;
	UpperVariant.InternalAccessLinks = UpperSnapshot.InternalAccessLinks;
	UpperVariant.VerticalAccessContracts = UpperSnapshot.DerivedVerticalAccessContracts;
	UpperVariant.Roles = UpperSnapshot.Roles;
	UpperVariant.SupportedCellIntents = UpperSnapshot.SupportedCellIntents;
	UpperVariant.MinTraversableNeighborFaces = UpperSnapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& LowerPlacement = Context.Placements.Add(FIntVector(0, 0, 0));
	LowerPlacement.YawRotationSteps = 0;
	LowerPlacement.VariantIndex = 0;
	LowerPlacement.bEmpty = false;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& UpperPlacement = Context.Placements.Add(FIntVector(0, 0, 1));
	UpperPlacement.YawRotationSteps = 0;
	UpperPlacement.VariantIndex = 1;
	UpperPlacement.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	const LayoutProfileSolverInternal::FSolveCellFaceInterfaceSet* LowerInterfaces =
		Context.CompiledFaceInterfaces.Find(LowerCell.Cell);
	TestNotNull(TEXT("Contract-backed lower cell has compiled face interfaces"), LowerInterfaces);
	if (LowerInterfaces == nullptr)
	{
		return false;
	}
	TestEqual(
		TEXT("Placed upper cell compiles as a filled neighbor before non-VerticalAccess audit policy is applied"),
		LowerInterfaces->Faces[static_cast<int32>(ELayoutFaceDirection::PosZ)].NeighborKind,
		LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::FixedFilledNeighbor);

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateSolvedPlacementAdjacencyForTests(Context, FailureReason);
	TestTrue(TEXT("Final adjacency audit keeps a contract-backed non-VerticalAccess stack outside ordinary filled-neighbor face matching"), bValid);
	if (!bValid && !FailureReason.IsEmpty())
	{
		AddError(FailureReason);
	}
	return bValid;
}

bool FLayoutPlacedModuleCarrierReachabilityPrefersVariantBackedCandidateAttachmentTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierReachability"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierReachabilityTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* CorridorModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierReachabilityCorridor"),
		Template,
		{ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(CorridorModule, 1);
	TestTrue(TEXT("Reachability snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierReachabilityCompositeCarrier"));
	Snapshot.SourceModule = nullptr;

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	Context.ModuleSnapshots.Add(Snapshot);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& FixedNeighborPlacement =
		Context.FixedNeighborPlacements.Add(FIntVector::ZeroValue);
	FixedNeighborPlacement.YawRotationSteps = 0;
	FixedNeighborPlacement.VariantIndex = 0;
	FixedNeighborPlacement.bEmpty = false;

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.YawRotationSteps = 0;
	Candidate.VariantIndex = 0;
	Candidate.bEmpty = false;

	TSet<LayoutProfileSolverInternal::FWalkableNodeKey> ReachableNodes;
	ReachableNodes.Add({FIntVector::ZeroValue, LayoutGameplayTags::TraversalPrimary});

	const int32 Preference = LayoutProfileSolverInternal::GetCandidateReachabilityPreferenceForTests(
		Context,
		FIntVector(1, 0, 0),
		Candidate,
		ReachableNodes);

	TestEqual(
		TEXT("Variant-backed candidate attaches required reachability through the variant-backed fixed neighbor"),
		Preference,
		2);

	return true;
}

bool FLayoutPlacedModuleCarrierExportsOnlyBundleRootPlacementsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierBundleRoot"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierBundleRootTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierBundleRootModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Bundle-root snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierBundleRootCompositeCarrier"));
	Snapshot.SourceModule = nullptr;
	Snapshot.BoundsCells = FIntVector(1, 2, 1);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};
	Snapshot.GeneratedLocalCellFaceRules.Reset();
	FLayoutLocalCellFaceRuleSnapshot& RootCellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	RootCellSnapshot.LocalCell = FIntVector(0, 0, 0);
	RootCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::NegX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::TraversalPrimary})));
	FLayoutLocalCellFaceRuleSnapshot& ShadowCellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	ShadowCellSnapshot.LocalCell = FIntVector(0, 1, 0);
	ShadowCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::TraversalSecondary})));
	Snapshot.DerivedInternalTraversalLinks.Reset();
	FLayoutDerivedInternalTraversalLink& DerivedBridge =
		Snapshot.DerivedInternalTraversalLinks.AddDefaulted_GetRef();
	DerivedBridge.LinkId = TEXT("PlacedCarrierBundleRootBridge");
	DerivedBridge.FromLocalCell = FIntVector(0, 0, 0);
	DerivedBridge.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	DerivedBridge.ToLocalCell = FIntVector(0, 1, 0);
	DerivedBridge.ToTraversalChannel = LayoutGameplayTags::TraversalSecondary;
	DerivedBridge.bBidirectional = true;

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 2);
	Context.ModuleSnapshots.Add(Snapshot);

	FLayoutPlannedCell& RootPlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	RootPlannedCell.Cell = FIntVector(0, 0, 0);
	RootPlannedCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(RootPlannedCell.Cell, RootPlannedCell.Intent);

	FLayoutPlannedCell& ShadowPlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	ShadowPlannedCell.Cell = FIntVector(0, 1, 0);
	ShadowPlannedCell.Intent = ELayoutCellIntent::Interior;
	Context.PlannedCellIntents.Add(ShadowPlannedCell.Cell, ShadowPlannedCell.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.SourceContentEntryId = FName(TEXT("BundleRootEntry"));
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& RootPlacement =
		Context.Placements.Add(FIntVector(0, 0, 0));
	RootPlacement.YawRotationSteps = 0;
	RootPlacement.VariantIndex = 0;
	RootPlacement.bEmpty = false;
	RootPlacement.BundleRootCell = FIntVector(0, 0, 0);
	RootPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& ShadowPlacement =
		Context.Placements.Add(FIntVector(0, 1, 0));
	ShadowPlacement.YawRotationSteps = 0;
	ShadowPlacement.VariantIndex = 0;
	ShadowPlacement.bEmpty = false;
	ShadowPlacement.BundleRootCell = FIntVector(0, 0, 0);
	ShadowPlacement.bBundleRoot = false;

	const FLayoutSolveResult Result = LayoutProfileSolverInternal::BuildSolveResultFromContextPlacementsForTests(Context);
	TestEqual(TEXT("Only the bundle root placement is exported"), Result.Placements.Num(), 1);
	if (Result.Placements.Num() != 1)
	{
		return false;
	}

	const FLayoutPlacedModule& Placement = Result.Placements[0];
	TestEqual(TEXT("Exported placement stays on the bundle root cell"), Placement.Cell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Exported placement keeps the full occupied local-cell set"), Placement.OccupiedLocalCells.Num(), 2);
	TestTrue(TEXT("Exported placement preserves bundle snapshot identity"), Placement.ModuleSnapshotId == Snapshot.SnapshotId);
	TestEqual(TEXT("Exported placement preserves bundle catalog index"), Placement.ModuleSnapshotIndex, 0);
	TestNull(TEXT("Exported placement carries no live composite pointer"), Placement.CompositeModule.Get());
	TestEqual(TEXT("Exported placement preserves source entry id"), Placement.SourceContentEntryId, FName(TEXT("BundleRootEntry")));
	TestEqual(TEXT("Exported placement preserves per-cell face snapshots for the whole bundle"), Placement.LocalCellFaceRules.Num(), 2);
	TestEqual(TEXT("Exported placement preserves derived traversal bridges for the whole bundle"), Placement.DerivedInternalTraversalLinks.Num(), 1);

	return true;
}

bool FLayoutPlacedModuleCarrierExportsLeafBundleBoundsFromOccupiedFootprintTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierLeafBundleBounds"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierLeafBundleBoundsTemplate"), FIntVector(16, 16, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierLeafBundleBoundsModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(2, 2);
	Context.ModuleSnapshots.Add(Snapshot);

	FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector(10, 20, 0);
	PlannedCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& RootPlacement = Context.Placements.Add(PlannedCell.Cell);
	RootPlacement.YawRotationSteps = 0;
	RootPlacement.VariantIndex = 0;
	RootPlacement.bEmpty = false;
	RootPlacement.BundleRootCell = PlannedCell.Cell;
	RootPlacement.bBundleRoot = true;

	const FLayoutSolveResult Result = LayoutProfileSolverInternal::BuildSolveResultFromContextPlacementsForTests(Context);
	TestEqual(TEXT("One leaf placement is exported"), Result.Placements.Num(), 1);
	if (Result.Placements.Num() != 1)
	{
		return false;
	}

	const FLayoutPlacedModule& Placement = Result.Placements[0];
	TestEqual(TEXT("Exported leaf placement preserves the one-cell occupied contract"), Placement.OccupiedLocalCells.Num(), 1);
	TestEqual(TEXT("Exported leaf placement bundle bounds now stay at one cell"), Placement.BundleBoundsCells, FIntVector(1, 1, 1));

	return true;
}

bool FLayoutPlacedModuleCarrierPlacedReachabilityUsesCompositeDerivedTraversalLinksTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierDerivedReachability"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierDerivedReachabilityTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* RoomLeaf = CreateModule(
		Outer,
		TEXT("PlacedCarrierDerivedReachabilityLeaf"),
		Template,
		{ELayoutCellIntent::Entry, ELayoutCellIntent::Interior},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceEntry,
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	ULayoutCompositeModuleAsset* CompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierDerivedReachabilityComposite"));
	FLayoutCompositeModuleCell& RootCell = CompositeModule->Cells.AddDefaulted_GetRef();
	RootCell.LocalCell = FIntVector(0, 0, 0);
	RootCell.Module = RoomLeaf;
	RootCell.RelativeYawRotationSteps = 0;
	FLayoutCompositeModuleCell& ShadowCell = CompositeModule->Cells.AddDefaulted_GetRef();
	ShadowCell.LocalCell = FIntVector(1, 0, 0);
	ShadowCell.Module = RoomLeaf;
	ShadowCell.RelativeYawRotationSteps = 0;

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeReachabilityPair");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierDerivedReachabilityContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierDerivedReachabilityProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);

	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveRequest Request =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 1017, TEXT("PlacedCarrier/DerivedReachability"));
	TestEqual(TEXT("Composite reachability request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	TestTrue(TEXT("Composite reachability snapshot carries derived traversal bridges"), Snapshot.DerivedInternalTraversalLinks.Num() > 0);
	if (Snapshot.DerivedInternalTraversalLinks.IsEmpty())
	{
		return false;
	}

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(2, 1);
	Context.ModuleSnapshots.Add(Snapshot);

	FLayoutPlannedCell& PlannedRoot = Context.Result.PlannedCells.AddDefaulted_GetRef();
	PlannedRoot.Cell = FIntVector(0, 0, 0);
	PlannedRoot.Intent = ELayoutCellIntent::Entry;
	Context.PlannedCellIntents.Add(PlannedRoot.Cell, PlannedRoot.Intent);

	FLayoutPlannedCell& PlannedShadow = Context.Result.PlannedCells.AddDefaulted_GetRef();
	PlannedShadow.Cell = FIntVector(1, 0, 0);
	PlannedShadow.Intent = ELayoutCellIntent::Interior;
	Context.PlannedCellIntents.Add(PlannedShadow.Cell, PlannedShadow.Intent);

	LayoutProfileSolverInternal::BuildOrientedVariantsForTests(Context);
	TestEqual(
		TEXT("Composite reachability context builds every allowed oriented variant"),
		Context.Variants.Num(),
		Snapshot.AllowedYawRotationSteps.Num());
	const int32 UnrotatedVariantIndex = Context.Variants.IndexOfByPredicate(
		[](const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant)
		{
			return Variant.YawRotationSteps == 0;
		});
	TestTrue(TEXT("Composite reachability context includes the unrotated variant"), UnrotatedVariantIndex != INDEX_NONE);
	if (UnrotatedVariantIndex == INDEX_NONE)
	{
		return false;
	}

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& RootPlacement = Context.Placements.Add(FIntVector(0, 0, 0));
	RootPlacement.YawRotationSteps = 0;
	RootPlacement.VariantIndex = UnrotatedVariantIndex;
	RootPlacement.bEmpty = false;
	RootPlacement.BundleRootCell = FIntVector(0, 0, 0);
	RootPlacement.LocalBundleCell = FIntVector(0, 0, 0);
	RootPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& UpperPlacement = Context.Placements.Add(FIntVector(1, 0, 0));
	UpperPlacement.YawRotationSteps = 0;
	UpperPlacement.VariantIndex = UnrotatedVariantIndex;
	UpperPlacement.bEmpty = false;
	UpperPlacement.BundleRootCell = FIntVector(0, 0, 0);
	UpperPlacement.LocalBundleCell = FIntVector(1, 0, 0);
	UpperPlacement.bBundleRoot = false;

	const TSet<LayoutProfileSolverInternal::FWalkableNodeKey> ReachableNodes =
		LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(Context);

	TestTrue(
		TEXT("Placed reachability keeps the entry-root bundle cell reachable"),
		ReachableNodes.Contains({FIntVector(0, 0, 0), LayoutGameplayTags::TraversalPrimary}));
	TestTrue(
		TEXT("Placed reachability follows the composite-derived traversal bridge onto the shadow bundle cell"),
		ReachableNodes.Contains({FIntVector(1, 0, 0), LayoutGameplayTags::TraversalPrimary}));

	return true;
}

bool FLayoutPlacedModuleCarrierRejectsProjectedBundleCellConflictTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierProjectedConflict"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierProjectedConflictTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierProjectedConflictModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Projected-conflict snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierProjectedConflictCompositeCarrier"));
	Snapshot.SourceModule = nullptr;
	Snapshot.BoundsCells = FIntVector(1, 2, 1);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};
	if (!TestEqual(TEXT("Leaf snapshot supplies one local-cell descriptor"), Snapshot.GeneratedLocalCellFaceRules.Num(), 1))
	{
		return false;
	}
	FLayoutLocalCellFaceRuleSnapshot ShadowCellSnapshot = Snapshot.GeneratedLocalCellFaceRules[0];
	ShadowCellSnapshot.LocalCell = FIntVector(0, 1, 0);
	Snapshot.GeneratedLocalCellFaceRules.Add(ShadowCellSnapshot);

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 2);
	Context.ModuleSnapshots.Add(Snapshot);
	Context.PlannedCellIntents.Add(FIntVector(0, 0, 0), ELayoutCellIntent::Boundary);
	Context.PlannedCellIntents.Add(FIntVector(0, 1, 0), ELayoutCellIntent::Interior);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.SourceContentEntryId = FName(TEXT("ProjectedConflictEntry"));
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& ConflictPlacement =
		Context.FixedNeighborPlacements.Add(FIntVector(0, 1, 0));
	ConflictPlacement.YawRotationSteps = 0;
	ConflictPlacement.VariantIndex = 0;
	ConflictPlacement.bEmpty = false;
	ConflictPlacement.BundleRootCell = FIntVector(0, 1, 0);
	ConflictPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.YawRotationSteps = 0;
	Candidate.VariantIndex = 0;
	Candidate.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bCompatible = LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		Context,
		FIntVector(0, 0, 0),
		Candidate,
		FailureReason);

	TestFalse(TEXT("Projected occupied bundle cells cannot overlap an existing occupied placement"), bCompatible);
	TestTrue(
		FString::Printf(TEXT("Failure points at the projected occupied bundle conflict: %s"), *FailureReason),
		FailureReason.Contains(TEXT("projected occupied bundle cell")) && FailureReason.Contains(TEXT("already owned")));

	return !bCompatible;
}

bool FLayoutPlacedModuleCarrierRejectsProjectedBundleCellWithoutExternalPolicyTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierProjectedExternalLevel"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierProjectedExternalLevelTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierProjectedExternalLevelModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Projected-external-level snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierProjectedExternalLevelCompositeCarrier"));
	Snapshot.SourceModule = nullptr;
	Snapshot.BoundsCells = FIntVector(1, 1, 2);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 0, 1)};
	if (!TestEqual(TEXT("Leaf snapshot supplies one local-cell descriptor"), Snapshot.GeneratedLocalCellFaceRules.Num(), 1))
	{
		return false;
	}
	FLayoutLocalCellFaceRuleSnapshot UpperCellSnapshot = Snapshot.GeneratedLocalCellFaceRules[0];
	UpperCellSnapshot.LocalCell = FIntVector(0, 0, 1);
	Snapshot.GeneratedLocalCellFaceRules.Add(UpperCellSnapshot);

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 1);
	Context.ModuleSnapshots.Add(Snapshot);
	Context.PlannedCellIntents.Add(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess);
	Context.ExternalReservedNeighborFaceMasks.Add(
		FIntVector(0, 0, 0),
		static_cast<uint8>(
			1u << static_cast<uint8>(ELayoutFaceDirection::PosZ)));

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.SourceContentEntryId = FName(TEXT("ProjectedExternalLevelEntry"));
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.YawRotationSteps = 0;
	Candidate.VariantIndex = 0;
	Candidate.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bCompatible = LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		Context,
		FIntVector(0, 0, 0),
		Candidate,
		FailureReason);

	TestFalse(TEXT("Projected occupied bundle cells reject an external level without frozen finalized policy metadata"), bCompatible);
	TestTrue(FString::Printf(TEXT("External-level rejection names missing frozen finalized policy authority: %s"), *FailureReason), FailureReason.Contains(TEXT("lacks frozen finalized policy authority")));

	return !bCompatible;
}

bool FLayoutPlacedModuleCarrierAllowsProjectedBundleCellOnExternalPlannedNeighborFaceTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreatePlacedModuleCarrierTestOuter(TEXT("LayoutPlacedModuleCarrierProjectedExternalPlannedFace"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierProjectedExternalPlannedFaceTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("PlacedCarrierProjectedExternalPlannedFaceModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosX,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegX,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegY,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::PosZ,
				LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(
				ELayoutFaceDirection::NegZ,
				LayoutGameplayTags::FaceSolid,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestTrue(TEXT("Projected-external-planned-neighbor snapshot validation succeeds"), Snapshot.Validation.IsValid());
	if (!Snapshot.Validation.IsValid())
	{
		return false;
	}

	Snapshot.SourceCompositeModule = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("PlacedCarrierProjectedExternalPlannedFaceCompositeCarrier"));
	Snapshot.SourceModule = nullptr;
	Snapshot.BoundsCells = FIntVector(1, 1, 2);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 0, 1)};
	if (!TestEqual(TEXT("Leaf snapshot supplies one local-cell descriptor"), Snapshot.GeneratedLocalCellFaceRules.Num(), 1))
	{
		return false;
	}
	FLayoutLocalCellFaceRuleSnapshot UpperCellSnapshot = Snapshot.GeneratedLocalCellFaceRules[0];
	UpperCellSnapshot.LocalCell = FIntVector(0, 0, 1);
	Snapshot.GeneratedLocalCellFaceRules.Add(UpperCellSnapshot);

	LayoutProfileSolverInternal::FSolveContext Context;
	Context.Result.FootprintSize = FIntPoint(1, 1);
	Context.ModuleSnapshots.Add(Snapshot);
	Context.PlannedCellIntents.Add(FIntVector(0, 0, 0), ELayoutCellIntent::VerticalAccess);
	Context.ExternalPlannedNeighborFaceMasks.Add(
		FIntVector(0, 0, 0),
		static_cast<uint8>(
			1u << static_cast<uint8>(ELayoutFaceDirection::PosZ)));

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = Snapshot.DebugName;
	Variant.SourceContentEntryId = FName(TEXT("ProjectedExternalPlannedFaceEntry"));
	Variant.YawRotationSteps = 0;
	Variant.WorldFaceRules = Snapshot.EffectiveFaceRules;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.ClosureProviderIntents = Snapshot.ClosureProviderIntents;
	Variant.TraversalChannels = Snapshot.TraversalChannels;
	Variant.InternalAccessLinks = Snapshot.InternalAccessLinks;
	Variant.VerticalAccessContracts = Snapshot.DerivedVerticalAccessContracts;
	Variant.Roles = Snapshot.Roles;
	Variant.SupportedCellIntents = Snapshot.SupportedCellIntents;
	Variant.MinTraversableNeighborFaces = Snapshot.MinTraversableNeighborFaces;

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.YawRotationSteps = 0;
	Candidate.VariantIndex = 0;
	Candidate.bEmpty = false;

	LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
	FString FailureReason;
	const bool bCompatible = LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		Context,
		FIntVector(0, 0, 0),
		Candidate,
		FailureReason);

	TestFalse(TEXT("Projected occupied bundle cells reject external planned-neighbor carrier faces without frozen finalized policy metadata"), bCompatible);
	TestTrue(FString::Printf(TEXT("External planned-neighbor rejection names missing frozen finalized policy authority: %s"), *FailureReason), FailureReason.Contains(TEXT("lacks frozen finalized policy authority")));

	return !bCompatible;
}

bool FLayoutPlacedModuleCarrierSparseFixedNeighborsPreserveBundleLocalCellsTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	SolveResult.FootprintSize = FIntPoint(1, 2);

	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.YawRotationSteps = 0;
	Placement.BundleBoundsCells = FIntVector(1, 2, 1);
	Placement.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};
	Placement.CompositeModule = NewObject<ULayoutCompositeModuleAsset>(
		GetTransientPackage(),
		TEXT("PlacedCarrierSparseFixedNeighborCompositeCarrier"));

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> FixedPlacements =
		LayoutProfileSolverInternal::BuildSparseFixedNeighborPlacementsForTests(SolveResult);

	TestEqual(TEXT("Sparse fixed-neighbor seeding emits one row per occupied bundle cell"), FixedPlacements.Num(), 2);
	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* RootPlacement = FixedPlacements.Find(FIntVector(0, 0, 0));
	const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* ShadowPlacement = FixedPlacements.Find(FIntVector(0, 1, 0));
	TestNotNull(TEXT("Root fixed-neighbor placement exists"), RootPlacement);
	TestNotNull(TEXT("Shadow fixed-neighbor placement exists"), ShadowPlacement);
	if (RootPlacement == nullptr || ShadowPlacement == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Root fixed-neighbor placement stays marked as bundle root"), RootPlacement->bBundleRoot);
	TestFalse(TEXT("Shadow fixed-neighbor placement is not marked as bundle root"), ShadowPlacement->bBundleRoot);
	TestEqual(TEXT("Both fixed-neighbor placements preserve the same bundle root cell"), RootPlacement->BundleRootCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Shadow fixed-neighbor placement preserves the same bundle root cell"), ShadowPlacement->BundleRootCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Root fixed-neighbor placement keeps local bundle cell zero"), RootPlacement->LocalBundleCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Shadow fixed-neighbor placement keeps its occupied local cell"), ShadowPlacement->LocalBundleCell, FIntVector(0, 1, 0));

	return true;
}

bool FLayoutPlacedModuleCarrierSparseFixedNeighborsUseCompositeBoundsWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	SolveResult.FootprintSize = FIntPoint(1, 2);

	ULayoutCompositeModuleAsset* CompositeModule = NewObject<ULayoutCompositeModuleAsset>(
		GetTransientPackage(),
		TEXT("PlacedCarrierSparseFixedNeighborCompositeBoundsCarrier"));
	FLayoutCompositeModuleCell& RootCell = CompositeModule->Cells.AddDefaulted_GetRef();
	RootCell.LocalCell = FIntVector(0, 0, 0);
	FLayoutCompositeModuleCell& ShadowCell = CompositeModule->Cells.AddDefaulted_GetRef();
	ShadowCell.LocalCell = FIntVector(0, 1, 0);

	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(10, 20, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.YawRotationSteps = 1;
	Placement.BundleBoundsCells = FIntVector::ZeroValue;
	Placement.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};
	Placement.CompositeModule = CompositeModule;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> FixedPlacements =
		LayoutProfileSolverInternal::BuildSparseFixedNeighborPlacementsForTests(SolveResult);

	TestEqual(TEXT("Sparse fixed-neighbor seeding still emits one row per occupied composite cell when BundleBoundsCells is absent"), FixedPlacements.Num(), 2);
	TestTrue(TEXT("Sparse fixed-neighbor seeding keeps the rotated root-covered world cell from the composite bounds"), FixedPlacements.Contains(FIntVector(11, 20, 0)));
	TestTrue(TEXT("Sparse fixed-neighbor seeding keeps the rotated shadow-covered world cell from the composite bounds"), FixedPlacements.Contains(FIntVector(10, 20, 0)));
	TestFalse(TEXT("Sparse fixed-neighbor seeding no longer falls back to a fake 1x1 footprint when BundleBoundsCells is absent"), FixedPlacements.Contains(FIntVector(9, 20, 0)));

	return true;
}

bool FLayoutPlacedModuleCarrierSparseFixedNeighborsRebuildLeafOccupiedCellsWithoutCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	SolveResult.FootprintSize = FIntPoint(2, 2);

	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(
		GetTransientPackage(),
		TEXT("PlacedCarrierSparseFixedNeighborLeafThinCarrier"));
	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(10, 20, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.YawRotationSteps = 1;

	const TMap<FIntVector, LayoutProfileSolverInternal::FSolveContext::FSolvePlacement> FixedPlacements =
		LayoutProfileSolverInternal::BuildSparseFixedNeighborPlacementsForTests(SolveResult);

	TestEqual(TEXT("Sparse fixed-neighbor seeding falls back to one occupied leaf cell from the live module when OccupiedLocalCells are absent"), FixedPlacements.Num(), 1);
	TestTrue(TEXT("Sparse fixed-neighbor seeding keeps the rotated first occupied world cell from the leaf occupancy footprint"), FixedPlacements.Contains(FIntVector(10, 20, 0)));
	TestFalse(TEXT("Sparse fixed-neighbor seeding no longer fabricates a second occupied world cell from removed leaf-shape fields"), FixedPlacements.Contains(FIntVector(10, 21, 0)));
	TestFalse(TEXT("Sparse fixed-neighbor seeding no longer rotates against the larger raw BoundsCells rectangle"), FixedPlacements.Contains(FIntVector(11, 20, 0)));

	return true;
}

bool FLayoutPlacedModuleCarrierSolveResultOccupiedWorldCellsRebuildLeafOccupiedCellsWithoutCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutSolveResult SolveResult;
	SolveResult.FootprintSize = FIntPoint(2, 2);

	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(
		GetTransientPackage(),
		TEXT("PlacedCarrierOccupiedWorldCellsLeafThinCarrier"));
	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(10, 20, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.YawRotationSteps = 1;

	const TSet<FIntVector> OccupiedWorldCells =
		LayoutProfileSolverInternal::BuildSolveResultOccupiedWorldCellsForTests(SolveResult);

	TestEqual(TEXT("Solve-result occupied world-cell reconstruction falls back to one thin-carrier leaf cell"), OccupiedWorldCells.Num(), 1);
	TestTrue(TEXT("Solve-result occupied world-cell reconstruction keeps the rotated first occupied world cell"), OccupiedWorldCells.Contains(FIntVector(10, 20, 0)));
	TestFalse(TEXT("Solve-result occupied world-cell reconstruction no longer fabricates a second occupied cell from removed leaf-shape fields"), OccupiedWorldCells.Contains(FIntVector(10, 21, 0)));
	TestFalse(TEXT("Solve-result occupied world-cell reconstruction no longer rotates against the larger raw BoundsCells rectangle"), OccupiedWorldCells.Contains(FIntVector(11, 20, 0)));

	return true;
}

bool FLayoutPlacedModuleCarrierExportedBoundaryPointsUseCompositeBoundsWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePlacedModuleCarrierTestOuter(TEXT("PlacedCarrierCompositeBoundaryExportNoBundleBounds"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("PlacedCarrierCompositeBoundaryExportNoBundleBoundsTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("PlacedCarrierCompositeBoundaryExportNoBundleBoundsLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutCompositeModuleCell LowerCell;
	LowerCell.Module = LeafModule;
	LowerCell.LocalCell = FIntVector(0, 0, 0);
	FLayoutCompositeModuleCell UpperCell;
	UpperCell.Module = LeafModule;
	UpperCell.LocalCell = FIntVector(0, 1, 0);

	ULayoutCompositeModuleAsset* CompositeModule = NewObject<ULayoutCompositeModuleAsset>(
		Outer,
		TEXT("PlacedCarrierCompositeBoundaryExportNoBundleBoundsComposite"));
	CompositeModule->Cells = {LowerCell, UpperCell};

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositePairNoBundleBounds");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("PlacedCarrierCompositeBoundaryExportNoBundleBoundsContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("PlacedCarrierCompositeBoundaryExportNoBundleBoundsProfile"),
		FIntPoint(1, 2),
		FIntPoint(1, 2),
		1,
		0,
		false);

	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		912,
		TEXT("PlacedCarrier/CompositeBoundaryExportNoBundleBounds"));
	TestEqual(TEXT("Composite request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	FLayoutPlacedModule& Placement = SolveResult.Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(10, 20, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.CompositeModule = CompositeModule;
	Placement.YawRotationSteps = 1;
	Placement.SourceContentEntryId = Entry.EntryId;
	Placement.BundleBoundsCells = FIntVector::ZeroValue;
	Placement.OccupiedLocalCells = Snapshot.OccupiedLocalCells;

	const TArray<FLayoutSolveBoundaryPoint> BoundaryPoints =
		LayoutProfileSolverInternal::BuildExportedBoundaryPointsFromSolveResultForTests(Request, SolveResult);

	TestTrue(TEXT("Boundary-point export includes the first rotated composite-covered world cell"), BoundaryPoints.ContainsByPredicate(
		[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
		{
			return BoundaryPoint.LocalCell == FIntVector(11, 20, 0);
		}));
	TestTrue(TEXT("Boundary-point export includes the second rotated composite-covered world cell"), BoundaryPoints.ContainsByPredicate(
		[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
		{
			return BoundaryPoint.LocalCell == FIntVector(10, 20, 0);
		}));
	TestFalse(TEXT("Boundary-point export no longer falls back to a fake 1x1 footprint when BundleBoundsCells is absent"), BoundaryPoints.ContainsByPredicate(
		[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
		{
			return BoundaryPoint.LocalCell == FIntVector(9, 20, 0);
		}));

	return true;
}

bool FLayoutPlacedModuleCarrierShadowPlacementUsesLocalBundleCellFaceRulesTest::RunTest(const FString& Parameters)
{
	LayoutProfileSolverInternal::FSolveContext Context;

	FLayoutModuleSolveSnapshot& Snapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
	Snapshot.BoundsCells = FIntVector(1, 2, 1);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};

	FLayoutLocalCellFaceRuleSnapshot& RootCellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	RootCellSnapshot.LocalCell = FIntVector(0, 0, 0);
	RootCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutLocalCellFaceRuleSnapshot& ShadowCellSnapshot = Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	ShadowCellSnapshot.LocalCell = FIntVector(0, 1, 0);
	ShadowCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceEntry,
		MakeTags({LayoutGameplayTags::FaceEntry}),
		ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor,
		MakeTags({LayoutGameplayTags::TraversalSecondary})));

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleDebugName = FName(TEXT("ShadowFaceVariant"));
	Variant.YawRotationSteps = 0;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement RootPlacement;
	RootPlacement.VariantIndex = 0;
	RootPlacement.bEmpty = false;
	RootPlacement.BundleRootCell = FIntVector(0, 0, 0);
	RootPlacement.LocalBundleCell = FIntVector(0, 0, 0);
	RootPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement ShadowPlacement;
	ShadowPlacement.VariantIndex = 0;
	ShadowPlacement.bEmpty = false;
	ShadowPlacement.BundleRootCell = FIntVector(0, 0, 0);
	ShadowPlacement.LocalBundleCell = FIntVector(0, 1, 0);
	ShadowPlacement.bBundleRoot = false;

	FLayoutFaceRule RootFaceRule;
	FLayoutFaceRule ShadowFaceRule;
	const bool bRootHasFaceRule = LayoutProfileSolverInternal::TryGetPlacementFaceRuleForTests(
		Context,
		RootPlacement,
		ELayoutFaceDirection::PosX,
		RootFaceRule);
	const bool bShadowHasFaceRule = LayoutProfileSolverInternal::TryGetPlacementFaceRuleForTests(
		Context,
		ShadowPlacement,
		ELayoutFaceDirection::PosX,
		ShadowFaceRule);

	TestTrue(TEXT("Root placement resolves a local-cell face rule"), bRootHasFaceRule);
	TestTrue(TEXT("Shadow placement resolves a local-cell face rule"), bShadowHasFaceRule);
	if (!bRootHasFaceRule || !bShadowHasFaceRule)
	{
		return false;
	}

	TestTrue(TEXT("Root placement keeps the root cell traversal channel"), RootFaceRule.ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary));
	TestTrue(TEXT("Shadow placement switches to the shadow cell traversal channel"), ShadowFaceRule.ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalSecondary));
	TestTrue(TEXT("Shadow placement keeps the shadow cell connection tag"), ShadowFaceRule.ConnectionTag == FGameplayTag(LayoutGameplayTags::FaceEntry));
	TestEqual(TEXT("Shadow placement keeps the shadow cell occupancy"), ShadowFaceRule.OccupancyPolicy, ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor);

	return true;
}



bool FLayoutPlacedModuleCarrierForwardCheckUsesShadowBundleNeighborsTest::RunTest(const FString& Parameters)
{
	LayoutProfileSolverInternal::FSolveContext Context;

	FLayoutModuleSolveSnapshot& BundleSnapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
	BundleSnapshot.BoundsCells = FIntVector(1, 2, 1);
	BundleSnapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};
	BundleSnapshot.SupportedCellIntents = {ELayoutCellIntent::Boundary};

	FLayoutLocalCellFaceRuleSnapshot& BundleRootCellSnapshot = BundleSnapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	BundleRootCellSnapshot.LocalCell = FIntVector(0, 0, 0);
	BundleRootCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutLocalCellFaceRuleSnapshot& BundleShadowCellSnapshot = BundleSnapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
	BundleShadowCellSnapshot.LocalCell = FIntVector(0, 1, 0);
	BundleShadowCellSnapshot.ExposedFaceRules.Add(MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceSolid,
		FGameplayTagContainer(),
		ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutModuleSolveSnapshot& NeighborSnapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
	NeighborSnapshot.BoundsCells = FIntVector(1, 1, 1);
	NeighborSnapshot.OccupiedLocalCells = {FIntVector::ZeroValue};
	NeighborSnapshot.SupportedCellIntents = {ELayoutCellIntent::Boundary};

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& BundleVariant = Context.Variants.AddDefaulted_GetRef();
	BundleVariant.ModuleSnapshotIndex = 0;
	BundleVariant.ModuleDebugName = FName(TEXT("ForwardCheckBundleVariant"));
	BundleVariant.YawRotationSteps = 0;
	Context.VariantIndicesByIntent.FindOrAdd(ELayoutCellIntent::Boundary).Add(0);

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& NeighborVariant = Context.Variants.AddDefaulted_GetRef();
	NeighborVariant.ModuleSnapshotIndex = 1;
	NeighborVariant.ModuleDebugName = FName(TEXT("ForwardCheckNeighborVariant"));
	NeighborVariant.YawRotationSteps = 0;
	NeighborVariant.WorldFaceRules.SetRule(MakeConnectionFaceRule(
		ELayoutFaceDirection::NegX,
		LayoutGameplayTags::FaceOpen,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	Context.VariantIndicesByIntent.FindOrAdd(ELayoutCellIntent::Boundary).Add(1);

	FLayoutPlannedCell& RootPlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	RootPlannedCell.Cell = FIntVector(0, 0, 0);
	RootPlannedCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(RootPlannedCell.Cell, RootPlannedCell.Intent);

	FLayoutPlannedCell& ShadowPlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	ShadowPlannedCell.Cell = FIntVector(0, 1, 0);
	ShadowPlannedCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(ShadowPlannedCell.Cell, ShadowPlannedCell.Intent);

	FLayoutPlannedCell& ShadowNeighborPlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
	ShadowNeighborPlannedCell.Cell = FIntVector(1, 1, 0);
	ShadowNeighborPlannedCell.Intent = ELayoutCellIntent::Boundary;
	Context.PlannedCellIntents.Add(ShadowNeighborPlannedCell.Cell, ShadowNeighborPlannedCell.Intent);

	LayoutProfileSolverInternal::FSolveCandidate BundleCandidate;
	BundleCandidate.YawRotationSteps = 0;
	BundleCandidate.VariantIndex = 0;
	BundleCandidate.bEmpty = false;

	const TArray<FIntVector> PlacedCells = LayoutProfileSolverInternal::CommitOccupiedCandidateBundleForTests(
		Context,
		FIntVector(0, 0, 0),
		BundleCandidate);

	FString FailureReason;
	const bool bForwardCheckPassed = LayoutProfileSolverInternal::ForwardCheckPlacedCellsForTests(
		Context,
		PlacedCells,
		FailureReason);

	TestFalse(TEXT("Forward checking rejects an unsolved neighbor that only touches the bundle shadow cell"), bForwardCheckPassed);
	return !bForwardCheckPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierSteppedCompositeStageAdmissionTest,
	"PorismExtension.Layout.Solver.Profile.SteppedCompositeStageAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlacedModuleCarrierSteppedCompositeStageAdmissionTest::RunTest(const FString& Parameters)
{
	auto BuildContext = [](const int32 ShadowTerrainStage)
	{
		LayoutProfileSolverInternal::FSolveContext Context;
		Context.Result.FootprintSize = FIntPoint(2, 1);
		Context.bUsesSteppedTerrainContract = true;

		FLayoutModuleSolveSnapshot& Snapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
		Snapshot.BoundsCells = FIntVector(2, 1, 1);
		Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(1, 0, 0)};
		Snapshot.PlacementZone = ELayoutPlacementZone::Any;
		Snapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
		for (const FIntVector& LocalCell : Snapshot.OccupiedLocalCells)
		{
			FLayoutLocalCellFaceRuleSnapshot& LocalCellSnapshot =
				Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
			LocalCellSnapshot.LocalCell = LocalCell;
			LocalCellSnapshot.SupportedCellIntents = {ELayoutCellIntent::Boundary};
		}

		for (const FIntVector& PlannedCell : Snapshot.OccupiedLocalCells)
		{
			Context.PlannedCellIntents.Add(PlannedCell, ELayoutCellIntent::Boundary);
			FLayoutPlannedCell& ResultCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
			ResultCell.Cell = PlannedCell;
			ResultCell.Intent = ELayoutCellIntent::Boundary;
			LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord& ViewRecord =
				Context.FinalizedCellsByPhysicalCell.Add(PlannedCell);
			ViewRecord.PlannedCellIndex = Context.Result.PlannedCells.Num() - 1;
			ViewRecord.bHasAuthoredSource = true;
			ViewRecord.SourceAuthoredCell = PlannedCell;
			ViewRecord.bHasTerrainStage = true;
			ViewRecord.TerrainStageIndex = PlannedCell.X == 0 ? 0 : ShadowTerrainStage;
		}

		LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant =
			Context.Variants.AddDefaulted_GetRef();
		Variant.ModuleSnapshotIndex = 0;
		Variant.ModuleSnapshotId = TEXT("SteppedCompositeStageSnapshot");
		Variant.ModuleDebugName = TEXT("SteppedCompositeStage");
		Variant.YawRotationSteps = 0;
		Variant.PlacementZone = ELayoutPlacementZone::Any;
		Variant.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
		Variant.SupportedCellIntents = {ELayoutCellIntent::Boundary};
		Context.VariantIndicesByIntent.FindOrAdd(ELayoutCellIntent::Boundary).Add(0);
		LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
		return Context;
	};

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.VariantIndex = 0;
	Candidate.YawRotationSteps = 0;
	Candidate.bEmpty = false;

	LayoutProfileSolverInternal::FSolveContext SameStageContext = BuildContext(0);
	FString FailureReason;
	TestTrue(TEXT("Composite occupies one shifted terrain stage when every frozen cell has matching stage authority"), LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		SameStageContext,
		FIntVector::ZeroValue,
		Candidate,
		FailureReason));

	LayoutProfileSolverInternal::FSolveContext IncompleteDescriptorContext = BuildContext(0);
	IncompleteDescriptorContext.ModuleSnapshots[0].GeneratedLocalCellFaceRules.Pop();
	FailureReason.Reset();
	TestFalse(TEXT("Composite rejects a missing frozen shadow descriptor"), LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		IncompleteDescriptorContext,
		FIntVector::ZeroValue,
		Candidate,
		FailureReason));
	TestTrue(TEXT("Missing composite descriptor rejection identifies the local cell"), FailureReason.Contains(TEXT("missing its frozen local-cell descriptor")));

	LayoutProfileSolverInternal::FSolveContext CrossStageContext = BuildContext(1);
	FailureReason.Reset();
	TestFalse(TEXT("Composite rejects occupancy spanning frozen terrain stages"), LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
		CrossStageContext,
		FIntVector::ZeroValue,
		Candidate,
		FailureReason));
	TestTrue(TEXT("Cross-stage rejection reports frozen terrain-stage conflict"), FailureReason.Contains(TEXT("crosses frozen terrain stages")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierValidatesSecondaryCellZoneAndAuthoredLevelTest,
	"PorismExtension.Layout.Solver.Profile.ValidatesSecondaryCellZoneAndAuthoredLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlacedModuleCarrierValidatesSecondaryCellZoneAndAuthoredLevelTest::RunTest(const FString& Parameters)
{
	auto BuildContext = [](const int32 ShadowModuleLevel)
	{
		LayoutProfileSolverInternal::FSolveContext Context;
		Context.Result.FootprintSize = FIntPoint(4, 4);

		FLayoutModuleSolveSnapshot& Snapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
		Snapshot.BoundsCells = FIntVector(2, 1, 1);
		Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(1, 0, 0)};
		Snapshot.PlacementZone = ELayoutPlacementZone::Interior;
		Snapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		Snapshot.SpecificLevel = 1;
		for (const FIntVector& LocalCell : Snapshot.OccupiedLocalCells)
		{
			FLayoutLocalCellFaceRuleSnapshot& LocalCellSnapshot =
				Snapshot.GeneratedLocalCellFaceRules.AddDefaulted_GetRef();
			LocalCellSnapshot.LocalCell = LocalCell;
			LocalCellSnapshot.SupportedCellIntents = {ELayoutCellIntent::Interior};
		}

		for (int32 Y = 0; Y < 4; ++Y)
		{
			for (int32 X = 0; X < 4; ++X)
			{
				const FIntVector Cell(X, Y, 0);
				Context.PlannedCellIntents.Add(Cell, ELayoutCellIntent::Interior);
				FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
				PlannedCell.Cell = Cell;
				PlannedCell.Intent = ELayoutCellIntent::Interior;
				PlannedCell.PlacementZone = Cell.X == 0 || Cell.X == 3 || Cell.Y == 0 || Cell.Y == 3
					? ELayoutPlacementZone::Edge
					: ELayoutPlacementZone::Interior;
				PlannedCell.ModuleLevelIndex = Cell == FIntVector(2, 1, 0)
					? ShadowModuleLevel
					: 1;
				LayoutProfileSolverInternal::FLayoutFinalizedCellViewRecord& ViewRecord =
					Context.FinalizedCellsByPhysicalCell.Add(Cell);
				ViewRecord.PlannedCellIndex = Context.Result.PlannedCells.Num() - 1;
				ViewRecord.bHasAuthoredSource = true;
				ViewRecord.SourceAuthoredCell = FIntVector(Cell.X, Cell.Y, PlannedCell.ModuleLevelIndex);
			}
		}

		LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant =
			Context.Variants.AddDefaulted_GetRef();
		Variant.ModuleSnapshotIndex = 0;
		Variant.ModuleSnapshotId = TEXT("CompositeSecondaryPolicySnapshot");
		Variant.ModuleDebugName = TEXT("CompositeSecondaryPolicy");
		Variant.YawRotationSteps = 0;
		Variant.PlacementZone = ELayoutPlacementZone::Interior;
		Variant.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		Variant.SpecificLevel = 1;
		Variant.SupportedCellIntents = {ELayoutCellIntent::Interior};
		Context.VariantIndicesByIntent.FindOrAdd(ELayoutCellIntent::Interior).Add(0);
		LayoutProfileSolverInternal::RefreshCompiledFaceInterfacesForTests(Context);
		return Context;
	};

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.VariantIndex = 0;
	Candidate.YawRotationSteps = 0;
	Candidate.bEmpty = false;
	FString FailureReason;

	LayoutProfileSolverInternal::FSolveContext CompatibleContext = BuildContext(1);
	TestTrue(TEXT("Composite admits only when root and secondary cells share interior zone and authored level policy"),
		LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
			CompatibleContext, FIntVector(1, 1, 0), Candidate, FailureReason));

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& CarriedShadow =
		CompatibleContext.FixedNeighborPlacements.Add(FIntVector(1, 1, 0));
	CarriedShadow.YawRotationSteps = 0;
	CarriedShadow.VariantIndex = 0;
	CarriedShadow.ModuleSnapshotIndex = 0;
	CarriedShadow.ModuleSnapshotId = TEXT("CompositeSecondaryPolicySnapshot");
	CarriedShadow.BundleRootCell = FIntVector(1, 1, -1);
	CarriedShadow.LocalBundleCell = FIntVector(0, 0, 1);
	CarriedShadow.bBundleRoot = false;
	CarriedShadow.bEmpty = false;
	FailureReason.Reset();
	TestFalse(TEXT("Composite candidate cannot replace a carried non-root shadow row at its own projected root cell"),
		LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
			CompatibleContext, FIntVector(1, 1, 0), Candidate, FailureReason));
	TestTrue(TEXT("Carried shadow re-selection reports existing bundle ownership"), FailureReason.Contains(TEXT("already owned")));
	CompatibleContext.FixedNeighborPlacements.Reset();

	FailureReason.Reset();
	TestFalse(TEXT("Composite rejects when its projected secondary cell reaches edge zone"),
		LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
			CompatibleContext, FIntVector(2, 1, 0), Candidate, FailureReason));
	TestTrue(TEXT("Secondary zone rejection identifies placement-zone policy"), FailureReason.Contains(TEXT("placement zone")));

	LayoutProfileSolverInternal::FSolveContext WrongLevelContext = BuildContext(0);
	FailureReason.Reset();
	TestFalse(TEXT("Composite rejects when its projected secondary cell has wrong authored module level"),
		LayoutProfileSolverInternal::IsCandidateCompatibleForTests(
			WrongLevelContext, FIntVector(1, 1, 0), Candidate, FailureReason));
	TestTrue(TEXT("Secondary authored-level rejection identifies level policy"), FailureReason.Contains(TEXT("authored level policy")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierRollbackRemovesWholeBundleTest,
	"PorismExtension.Layout.Solver.Profile.RollbackRemovesWholeBundle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlacedModuleCarrierRollbackRemovesWholeBundleTest::RunTest(const FString& Parameters)
{
	LayoutProfileSolverInternal::FSolveContext Context;
	FLayoutModuleSolveSnapshot& Snapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
	Snapshot.BoundsCells = FIntVector(2, 1, 1);
	Snapshot.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(1, 0, 0)};

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.ModuleSnapshotId = TEXT("RollbackBundleSnapshot");
	Variant.YawRotationSteps = 0;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& UnrelatedPlacement =
		Context.Placements.Add(FIntVector(9, 0, 0));
	UnrelatedPlacement.bEmpty = false;
	UnrelatedPlacement.VariantIndex = 0;
	UnrelatedPlacement.BundleRootCell = FIntVector(9, 0, 0);
	UnrelatedPlacement.bBundleRoot = true;

	LayoutProfileSolverInternal::FSolveCandidate Candidate;
	Candidate.VariantIndex = 0;
	Candidate.YawRotationSteps = 0;
	Candidate.bEmpty = false;
	const TArray<FIntVector> PlacedCells =
		LayoutProfileSolverInternal::CommitOccupiedCandidateBundleForTests(
			Context,
			FIntVector::ZeroValue,
			Candidate);
	TestEqual(TEXT("Atomic bundle commit returns every occupied world cell"), PlacedCells.Num(), 2);
	TestTrue(TEXT("Atomic bundle commit adds its root cell"), Context.Placements.Contains(FIntVector(0, 0, 0)));
	TestTrue(TEXT("Atomic bundle commit adds its shadow cell"), Context.Placements.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Atomic bundle commit preserves unrelated placement"), Context.Placements.Contains(FIntVector(9, 0, 0)));

	LayoutProfileSolverInternal::RollbackOccupiedCandidateBundleForTests(Context, PlacedCells);
	TestFalse(TEXT("Atomic bundle rollback removes root cell"), Context.Placements.Contains(FIntVector(0, 0, 0)));
	TestFalse(TEXT("Atomic bundle rollback removes shadow cell"), Context.Placements.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Atomic bundle rollback preserves unrelated placement"), Context.Placements.Contains(FIntVector(9, 0, 0)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPlacedModuleCarrierUpwardContinuationFallbackClassifiesPosZAsExternalTest,
	"PorismExtension.Layout.Carrier.UpwardContinuationFallbackClassifiesPosZAsExternal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlacedModuleCarrierUpwardContinuationFallbackClassifiesPosZAsExternalTest::RunTest(const FString& Parameters)
{
	using namespace LayoutProfileSolverInternal;

	// Simulate a prewarm-fed explicit-root path where FullPlannedCellSet
	// is truncated to a single Z level but the profile has multiple levels.
	const int32 ProfileLevelCount = 3;

	// Build planned cells for Z=0 only (simulating truncated request).
	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.Intent = ELayoutCellIntent::Boundary;
		}
	}

	// The set-based upward continuation function will find no masks
	// because Z+1 cells are not in the truncated planned set.
	TMap<FIntVector, uint8> Masks =
		BuildVerticalContinuationUpwardContinuationFaceMasksForTests(
			PlannedCells,
			0);
	TestEqual(TEXT("Set-based mask builder returns no masks for truncated planned cells"), Masks.Num(), 0);

	// Apply the fallback: use profile LevelCount to mark PosZ on every
	// level cell whose Z+1 is within level bounds.
	const uint8 PosZBit = static_cast<uint8>(
		1u << static_cast<uint8>(ELayoutFaceDirection::PosZ));
	for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
	{
		if (PlannedCell.Cell.Z + 1 < ProfileLevelCount)
		{
			Masks.FindOrAdd(PlannedCell.Cell) |= PosZBit;
		}
	}
	TestTrue(TEXT("Fallback produces masks for all level-0 boundary cells"), Masks.Num() > 0);
	for (const auto& MaskEntry : Masks)
	{
		TestEqual(FString::Printf(TEXT("Cell %s has PosZ continuation mask"), *MaskEntry.Key.ToString()),
			MaskEntry.Value & PosZBit, PosZBit);
	}

	// Now verify that RefreshCompiledFaceInterfaces classifies PosZ
	// as ExternalPlannedNeighbor when the mask is present.
	FSolveContext Context;
	Context.FootprintSize = FIntPoint(3, 3);
	Context.ExternalPlannedNeighborFaceMasks = Masks;
	for (const FLayoutPlannedCell& Cell : PlannedCells)
	{
		Context.PlannedCellIntents.Add(Cell.Cell, Cell.Intent);
	}
	Context.Result.PlannedCells = PlannedCells;

	RefreshCompiledFaceInterfacesForTests(Context);

	const FIntVector TestCell(1, 1, 0);
	const FSolveCellFaceInterfaceSet* InterfaceSet =
		Context.CompiledFaceInterfaces.Find(TestCell);
	TestNotNull(TEXT("Face interface set exists for test cell"), InterfaceSet);
	if (InterfaceSet == nullptr)
	{
		return false;
	}

	const int32 PosZIndex = static_cast<int32>(ELayoutFaceDirection::PosZ);
	const ESolveCellFaceNeighborKind PosZNeighborKind =
		InterfaceSet->Faces[PosZIndex].NeighborKind;
	TestEqual(
		TEXT("PosZ neighbor is classified as ExternalPlannedNeighbor, not OuterBoundary"),
		PosZNeighborKind,
		ESolveCellFaceNeighborKind::ExternalPlannedNeighbor);

	return true;
}
