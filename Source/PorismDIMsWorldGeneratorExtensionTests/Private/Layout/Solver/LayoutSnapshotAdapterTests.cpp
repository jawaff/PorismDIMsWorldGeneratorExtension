// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;
	using namespace PorismLayoutTestUtilities;
	const FLayoutId SnapshotPlacementPolicyId(TEXT("SnapshotPolicy"));
	const FLayoutId SnapshotCandidateId(TEXT("SnapshotCandidate"));
	const FLayoutId SnapshotSolveId(TEXT("SnapshotSolve"));
	const FLayoutId SnapshotContentSetPlacementPolicyId(TEXT("SnapshotContentSetPolicy"));
	const FLayoutId SnapshotContentSetCandidateId(TEXT("SnapshotContentSetCandidate"));
	const FLayoutId SnapshotContentSetSolveId(TEXT("SnapshotContentSetSolve"));
	const int32 SnapshotTemplatePlacementZOffsetBlocks = -2;
	constexpr float SnapshotMaxSolveDurationSeconds = 0.125f;

	UObject* CreateSnapshotTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
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
	FLayoutSnapshotAdapterBuildsStandaloneRequestTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.BuildsStandaloneRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterLeafSnapshotUsesEstablishedSharedCellSizeTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.LeafSnapshotUsesEstablishedSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterLeafSnapshotBoundsUseOccupiedContractTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.LeafSnapshotBoundsUseOccupiedContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSnapshotAdapterBuildsStandaloneRequestTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotAdapter"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotTemplate_Generic"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotModule_Generic"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotProfile_Generic"),
		FIntPoint(2, 2),
		FIntPoint(3, 3),
		2,
		1,
		false);
	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("SnapshotVerticalAccessModule");
	ModuleEntry.Weight = 5;
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotContentSet_Generic"),
		{ModuleEntry});
	FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
	ClosureRequirement.ClosureId = TEXT("OuterPerimeter");
	ClosureRequirement.Zone = ELayoutPlacementZone::Perimeter;
	ClosureRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
	ClosureRequirement.BoundsPolicy.MinLevel = 0;
	ClosureRequirement.BoundsPolicy.MaxLevel = 0;
	ClosureRequirement.MinThicknessCells = 1;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutSolverExecutionSettings ExecutionSettings;
	ExecutionSettings.MaxSolveDurationSeconds = SnapshotMaxSolveDurationSeconds;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		77,
		TEXT("SnapshotStandalone"),
		ExecutionSettings,
		SnapshotPlacementPolicyId,
		SnapshotCandidateId,
		SnapshotTemplatePlacementZOffsetBlocks,
		SnapshotSolveId);

	TestEqual(TEXT("Request seed preserved"), Request.Seed, 77);
	TestEqual(TEXT("Snapshot request preserves the supplied root placement policy id"), Request.RootPlacementPolicyId, SnapshotPlacementPolicyId);
	TestEqual(TEXT("Snapshot request preserves the supplied root candidate id"), Request.RootCandidateId, SnapshotCandidateId);
	TestEqual(TEXT("Snapshot request preserves the supplied root solve id"), Request.RootSolveId, SnapshotSolveId);
	TestEqual(TEXT("Snapshot request preserves the supplied template placement offset"), Request.TemplatePlacementZOffsetBlocks, SnapshotTemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Snapshot request preserves the supplied root timeout budget"), Request.ExecutionSettings.MaxSolveDurationSeconds, SnapshotMaxSolveDurationSeconds);
	TestEqual(TEXT("Module-set standalone request stays non-world-facing by default"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Module-set standalone request keeps the default placement policy kind"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Module-set standalone request ignores profile-owned terrain search start Z"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, 0);
	TestEqual(TEXT("Module-set standalone request ignores profile-owned terrain search depth"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, 0);
	TestEqual(TEXT("Module-set standalone request retains default terrain sample spacing"), Request.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, FLayoutWorldBindingPlacementPolicy().TerrainSampleGridSpacing);
	TestTrue(TEXT("Effective snapshot id initialized"), Request.EffectiveSnapshotId != NAME_None);
	TestEqual<FLayoutId>(TEXT("Profile snapshot id mirrors source profile"), Request.ProfileSnapshot.SnapshotId, Profile->GetFName());
	TestEqual<FLayoutId>(TEXT("Module-set snapshot id mirrors source module set"), Request.ModuleCatalog.SnapshotId, Profile->ContentSet->GetFName());
	TestEqual(TEXT("Module snapshot count mirrors module set"), Request.ModuleCatalog.Modules.Num(), 1);
	TestEqual<FLayoutId>(TEXT("Module snapshot id mirrors source module"), Request.ModuleCatalog.Modules[0].SnapshotId, Module->GetFName());
	TestEqual(TEXT("Module snapshot bounds default to one cell"), Request.ModuleCatalog.Modules[0].BoundsCells, FIntVector(1, 1, 1));
	TestEqual(TEXT("Module snapshot compiles one occupied local cell"), Request.ModuleCatalog.Modules[0].OccupiedLocalCells.Num(), 1);
	TestEqual(TEXT("Module snapshot compiles one local face snapshot"), Request.ModuleCatalog.Modules[0].GeneratedLocalCellFaceRules.Num(), 1);
	const FIntVector ModuleSharedCellSize = Module->GetEffectiveCellSizeInBlocks();
	TestEqual(TEXT("Snapshot request preserves shared cell size on the module-set snapshot"), Request.ModuleCatalog.SharedCellSizeInBlocks, ModuleSharedCellSize);
	TestEqual(TEXT("Snapshot request preserves shared cell size on the compatibility content-set snapshot"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, ModuleSharedCellSize);
	TestEqual(TEXT("Profile snapshot minimum footprint copied"), Request.ProfileSnapshot.MinimumFootprintInCells, Profile->MinimumFootprintInCells);
	TestEqual(TEXT("Profile snapshot maximum footprint copied"), Request.ProfileSnapshot.MaximumFootprintInCells, Profile->MaximumFootprintInCells);
	TestEqual(TEXT("Profile snapshot closure requirements copied"), Request.ProfileSnapshot.ClosureRequirements.Num(), 1);
	TestEqual(TEXT("Module snapshot weight copied"), Request.ModuleCatalog.Modules[0].Weight, 5);
	TestTrue(TEXT("Module snapshot derived endpoint offers populated"), Request.ModuleCatalog.Modules[0].DerivedEndpointOffers.Num() > 0);
	TestTrue(TEXT("Module snapshot derived span offers populated"), Request.ModuleCatalog.Modules[0].DerivedSpanOffers.Num() > 0);
	TestTrue(TEXT("Module snapshot derived vertical-access contracts populated"), Request.ModuleCatalog.Modules[0].DerivedVerticalAccessContracts.Num() > 0);
	TestTrue(TEXT("Profile snapshot proof records populated"), Request.ProfileSnapshot.ProofRecords.Num() > 0);
	TestTrue(TEXT("Module-set snapshot proof records populated"), Request.ModuleCatalog.ProofRecords.Num() > 0);
	TestTrue(TEXT("Request proof records populated"), Request.ProofRecords.Num() >= 2);
	TestTrue(TEXT("Request validation assertions populated"), Request.ValidationAssertions.Num() >= 3);
	TestTrue(TEXT("Profile snapshot validation remains valid"), Request.ProfileSnapshot.Validation.IsValid());
	TestTrue(TEXT("Module-set snapshot validation remains valid"), Request.ModuleCatalog.Validation.IsValid());
	TestEqual(TEXT("Execution context normalizes shared cell size from the module-set standalone request"), BuildSolveContext(Request).SharedCellSizeInBlocks, ModuleSharedCellSize);
	TestEqual(TEXT("Execution context preserves the module-set request root placement policy id"), BuildSolveContext(Request).PublicationMetadata.RootPlacementPolicyId, SnapshotPlacementPolicyId);
	TestEqual(TEXT("Execution context preserves the module-set request root candidate id"), BuildSolveContext(Request).PublicationMetadata.RootCandidateId, SnapshotCandidateId);
	TestEqual(TEXT("Execution context preserves the module-set request root solve id"), BuildSolveContext(Request).PublicationMetadata.RootSolveId, SnapshotSolveId);
	TestEqual(TEXT("Execution context preserves the module-set request template placement offset"), BuildSolveContext(Request).TemplatePlacementZOffsetBlocks, SnapshotTemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Execution context preserves the module-set request timeout budget on the frozen root request"), BuildSolveContext(Request).RootRequest.ExecutionSettings.MaxSolveDurationSeconds, SnapshotMaxSolveDurationSeconds);
	TestEqual(TEXT("Execution context keeps the default module-set standalone terrain search start Z"), BuildSolveContext(Request).WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, 0);
	TestEqual(
		TEXT("Scheduled module-set solve result preserves shared cell size without rereading live assets"),
		FLayoutProfileSolver::SolveRegionTree(Request).MergedSolveResult.SharedCellSizeInBlocks,
		ModuleSharedCellSize);
	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Module-set request records a shared cell size request-contract assertion"), SharedCellSizeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Shared cell size request-contract assertion uses the request-contract kind"), SharedCellSizeAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestTrue(TEXT("Module-set request shared cell size request-contract assertion passes"), SharedCellSizeAssertion->bPassed);
	const FLayoutValidationAssertionRecord* RootPublicationAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.RootPublicationIdentityContractValid");
	});
	if (!TestNotNull(TEXT("Module-set request records a root publication identity request-contract assertion"), RootPublicationAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Module-set request root publication identity assertion passes"), RootPublicationAssertion->bPassed);
	const FLayoutValidationAssertionRecord* TemplatePlacementOffsetAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.TemplatePlacementOffsetContractValid");
	});
	if (!TestNotNull(TEXT("Module-set request records a template placement offset request-contract assertion"), TemplatePlacementOffsetAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Module-set request template placement offset assertion passes"), TemplatePlacementOffsetAssertion->bPassed);

	return true;
}

bool FLayoutSnapshotAdapterLeafSnapshotUsesEstablishedSharedCellSizeTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotAdapterSharedCellSize"));

	UChunkStructureTemplate* LargeTemplate = CreateTemplate(Outer, TEXT("SnapshotTemplate_SharedCellSizeLarge"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotModule_SharedCellSizeLarge"),
		LargeTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotProfile_SharedCellSizeMismatch"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SharedCellSizeLeaf");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("SnapshotContentSet_SharedCellSize"), {Entry});

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		133,
		TEXT("SnapshotSharedCellSizeMismatch"));
	TestEqual(TEXT("Snapshot request preserves the derived leaf shared cell size"), Request.ModuleCatalog.SharedCellSizeInBlocks, FIntVector(16, 16, 16));
	TestEqual(TEXT("Module-set request still compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& ModuleSnapshot = Request.ModuleCatalog.Modules[0];
	TestEqual(TEXT("Leaf module snapshot keeps the derived leaf shared cell size"), ModuleSnapshot.CellSizeInBlocks, FIntVector(16, 16, 16));
	TestTrue(TEXT("Leaf module snapshot validation stays passing when the stale compatibility field is ignored"), ModuleSnapshot.Validation.IsValid());
	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = ModuleSnapshot.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("ModuleSnapshot.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Leaf module snapshot records a shared-cell-size snapshot assertion"), SharedCellSizeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Leaf module snapshot shared-cell-size assertion uses the snapshot shared-cell-size kind"), SharedCellSizeAssertion->AssertionKind, ELayoutValidationAssertionKind::SnapshotSharedCellSizeContractValid);
	TestTrue(TEXT("Leaf module snapshot shared-cell-size assertion stays passing when the stale compatibility field is ignored"), SharedCellSizeAssertion->bPassed);
	return true;
}

bool FLayoutSnapshotAdapterLeafSnapshotBoundsUseOccupiedContractTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotAdapterOccupiedBounds"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotTemplate_OccupiedBounds"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotModule_OccupiedBounds"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotProfile_OccupiedBounds"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("OccupiedBoundsLeaf");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("SnapshotContentSet_OccupiedBounds"), {Entry});

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		137,
		TEXT("SnapshotOccupiedBounds"));
	TestEqual(TEXT("Module-set request still compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& ModuleSnapshot = Request.ModuleCatalog.Modules[0];
	TestEqual(TEXT("Module snapshot occupied-cell bounds now follow the occupied contract instead of stale raw bounds"), ModuleSnapshot.BoundsCells, FIntVector(1, 1, 1));
	TestEqual(TEXT("Module snapshot still preserves the one occupied local cell"), ModuleSnapshot.OccupiedLocalCells.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterSolvesWithoutLiveProfileTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.SolvesWithoutLiveProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterPreservesResolvedTerrainAlignmentLevelTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.PreservesResolvedTerrainAlignmentLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterBuildsContentSetStandaloneRequestTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.BuildsContentSetStandaloneRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterContentSetRequestPreservesExplicitSharedCellSizeOverrideTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.ContentSetRequestPreservesExplicitSharedCellSizeOverride",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterContentSetRequestIgnoresStaleModuleSnapshotSharedCellSizeTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.ContentSetRequestIgnoresStaleModuleSnapshotSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterChildOnlyContentSetRequestIgnoresCompatibilitySharedCellSizeFieldTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.ChildOnlyContentSetRequestIgnoresCompatibilitySharedCellSizeField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterBuildsRootAnchoredLiveCompositeBundleRequestTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.BuildsRootAnchoredLiveCompositeBundleRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsLiveCompositeBundleWithoutRootAnchorContractTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsLiveCompositeBundleWithoutRootAnchorContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsMissingEffectiveSnapshotIdWithoutLiveUObjectBreadcrumbsTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsMissingEffectiveSnapshotIdWithoutLiveUObjectBreadcrumbs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterDefaultsRootIdsFromRegionPathTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.DefaultsRootIdsFromRegionPath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsTemplatePlacementOffsetWithoutSharedCellSizeTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsTemplatePlacementOffsetWithoutSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsSteppedSolveWithoutSupportMapTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsSteppedSolveWithoutSupportMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterAllowsFinalizedFlatFallbackWithoutSteppedSupportTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.AllowsFinalizedFlatFallbackWithoutSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsSteppedSupportOutsideSuppliedPlannedCellsTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsSteppedSupportOutsideSuppliedPlannedCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsSteppedSupportMissingSuppliedPlannedCellTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsSteppedSupportMissingSuppliedPlannedCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsRequiredRouteConstraintOutsidePlannedCellsTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsRequiredRouteConstraintOutsidePlannedCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsConflictingRequiredRouteFaceChannelsTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsConflictingRequiredRouteFaceChannels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterPreservesSteppedTerrainSupportMapTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.PreservesSteppedTerrainSupportMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsSteppedPolicyWithoutSteppedCapableFrozenProfileTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsSteppedPolicyWithoutSteppedCapableFrozenProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsForcedPlacementBundleInsertionWithoutMatchingBundleTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsForcedPlacementBundleInsertionWithoutMatchingBundle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSnapshotAdapterRejectsConflictingForcedPlacementBundleInsertionsOnSameAnchorTest,
	"PorismExtension.Layout.Solver.SnapshotAdapter.RejectsConflictingForcedPlacementBundleInsertionsOnSameAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSnapshotAdapterSolvesWithoutLiveProfileTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotOnlySolve"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotOnlyTemplate_Generic"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotOnlyModule_Generic"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotOnlyProfile_Generic"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		1,
		false);
	FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
	ClosureRequirement.ClosureId = TEXT("OuterPerimeter");
	ClosureRequirement.Zone = ELayoutPlacementZone::Perimeter;
	ClosureRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
	ClosureRequirement.BoundsPolicy.MinLevel = 0;
	ClosureRequirement.BoundsPolicy.MaxLevel = 0;
	ClosureRequirement.MinThicknessCells = 1;
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotOnlyModule");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("SnapshotOnlyContentSet"), {Entry});
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		91,
		TEXT("SnapshotOnly"));

	Request.ProfileSnapshot.SourceProfile = nullptr;

	const FLayoutRegionSolveResult Result = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(TEXT("Snapshot-only solve succeeds without live profile/module-set assets"), Result.SolveResult.bSucceeded);
	if (!Result.SolveResult.bSucceeded)
	{
		AddError(Result.SolveResult.FailureReason);
	}
	TestTrue(TEXT("Snapshot-only solve produces placements"), Result.SolveResult.Placements.Num() > 0);
	TestTrue(TEXT("Snapshot-only solve still exports boundary points from derived endpoint offers"), Result.ExportedBoundaryPoints.Num() > 0);
	TestEqual(TEXT("Snapshot-only solve keeps the non-world-facing root placement kind"), Result.SolveResult.RootPlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Snapshot-only solve keeps the default world-binding placement policy onto the solve result"), Result.SolveResult.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, 0);

	return true;
}

bool FLayoutSnapshotAdapterPreservesResolvedTerrainAlignmentLevelTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotAlignmentLevel"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotAlignmentTemplate_Generic"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotAlignmentModule_Generic"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotAlignmentProfile_Generic"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("AlignmentModule");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("SnapshotAlignmentContentSet"), {Entry});
	FLayoutRegionSolveRequest OrdinaryRootRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		117,
		TEXT("SnapshotAlignmentOrdinaryRoot"));
	OrdinaryRootRequest.ProfileSnapshot.SourceProfile = nullptr;
	OrdinaryRootRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;

	const FLayoutRegionSolveResult OrdinaryRootResult =
		FLayoutProfileSolver::SolveRegion(OrdinaryRootRequest);
	TestTrue(TEXT("Ordinary-root snapshot solve succeeds"), OrdinaryRootResult.SolveResult.bSucceeded);
	TestEqual(
		TEXT("Ordinary-root snapshot solve preserves the lowest realized alignment level"),
		OrdinaryRootResult.SolveResult.ResolvedTerrainAlignmentLevel,
		0);

	FLayoutRegionSolveRequest ContinuationRequest = OrdinaryRootRequest;
	ContinuationRequest.Seed = 118;
	ContinuationRequest.RegionDebugPath = TEXT("SnapshotAlignmentContinuation");
	ContinuationRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	ContinuationRequest.RootContinuationSelection.FamilyId = TEXT("SurfacePathFamily");
	ContinuationRequest.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	ContinuationRequest.RootContinuationSelection.ResolvedEntryLevel = 2;

	const FLayoutRegionSolveResult ContinuationResult =
		FLayoutProfileSolver::SolveRegion(ContinuationRequest);
	TestTrue(TEXT("Continuation-aligned snapshot solve succeeds"), ContinuationResult.SolveResult.bSucceeded);
	TestEqual(
		TEXT("Continuation-aligned snapshot solve preserves the pre-resolved continuation entry level"),
		ContinuationResult.SolveResult.ResolvedTerrainAlignmentLevel,
		2);

	const FGameplayTagContainer HigherDeckWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const auto MakeClosedFace = [&](const ELayoutFaceDirection Direction)
	{
		return MakeFaceRule(
			Direction,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor);
	};
	const auto MakeEntryFace = [&](const ELayoutFaceDirection Direction)
	{
		return MakeFaceRule(
			Direction,
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			HigherDeckWalkable);
	};
	const auto MakeConnectorFace = [&](const ELayoutFaceDirection Direction)
	{
		return MakeFaceRule(
			Direction,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			HigherDeckWalkable);
	};
	const auto MakeWalkwayFace = [&](const ELayoutFaceDirection Direction)
	{
		return MakeFaceRule(
			Direction,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	};
	const auto BuildHorizontalFaces = [&](const TArray<ELayoutFaceDirection>& OpenDirections, const bool bUseEntryFaces)
	{
		TArray<FLayoutFaceRule> FaceRules;
		const auto AddFaceRule = [&](const ELayoutFaceDirection Direction)
		{
			if (OpenDirections.Contains(Direction))
			{
				FaceRules.Add(MakeConnectorFace(Direction));
			}
			else if (bUseEntryFaces && OpenDirections.Contains(FLayoutDirectionUtils::GetOpposite(Direction)))
			{
				// The endpoint opens outward to its incoming commitment and inward to the route.
				FLayoutFaceRule EndpointFace = MakeEntryFace(Direction);
				EndpointFace.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
				FaceRules.Add(EndpointFace);
			}
			else
			{
				FaceRules.Add(MakeClosedFace(Direction));
			}
		};

		AddFaceRule(ELayoutFaceDirection::PosX);
		AddFaceRule(ELayoutFaceDirection::NegX);
		AddFaceRule(ELayoutFaceDirection::PosY);
		AddFaceRule(ELayoutFaceDirection::NegY);
		FaceRules.Add(MakeWalkwayFace(ELayoutFaceDirection::PosZ));
		FaceRules.Add(MakeWalkwayFace(ELayoutFaceDirection::NegZ));
		return FaceRules;
	};

	ULayoutModuleAsset* HigherDeckEntryEast = CreateModule(
		Outer,
		TEXT("SnapshotAlignmentModule_HigherDeckEntryEast"),
		Template,
		{ELayoutCellIntent::Entry},
		BuildHorizontalFaces({ELayoutFaceDirection::PosX}, true),
		HigherDeckWalkable);
	ULayoutModuleAsset* HigherDeckEntryWest = CreateModule(
		Outer,
		TEXT("SnapshotAlignmentModule_HigherDeckEntryWest"),
		Template,
		{ELayoutCellIntent::Entry},
		BuildHorizontalFaces({ELayoutFaceDirection::NegX}, true),
		HigherDeckWalkable);
	ULayoutModuleAsset* HigherDeckStraightConnector = CreateModule(
		Outer,
		TEXT("SnapshotAlignmentModule_HigherDeckStraightConnector"),
		Template,
		{ELayoutCellIntent::Connector, ELayoutCellIntent::Boundary},
		BuildHorizontalFaces({ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX}, false),
		HigherDeckWalkable);

	TArray<FLayoutRegionContentEntry> HigherDeckEntries;
	auto AddHigherDeckEntry = [&](const FName EntryId, ULayoutModuleAsset* EntryModule, const int32 Weight)
	{
		FLayoutRegionContentEntry& Entry = HigherDeckEntries.AddDefaulted_GetRef();
		Entry.EntryId = EntryId;
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = EntryModule;
		Entry.Weight = Weight;
	};
	AddHigherDeckEntry(TEXT("HigherDeckEntryEast"), HigherDeckEntryEast, 1);
	AddHigherDeckEntry(TEXT("HigherDeckEntryWest"), HigherDeckEntryWest, 1);
	AddHigherDeckEntry(TEXT("HigherDeckConnector"), HigherDeckStraightConnector, 1);

	ULayoutRegionContentSetAsset* HigherDeckContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotAlignmentContentSet_HigherDeckContinuation"),
		HigherDeckEntries);

	ULayoutProfileAsset* HigherDeckProfile = CreateProfile(
		Outer,
		TEXT("SnapshotAlignmentProfile_HigherDeckContinuation"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		2,
		0,
		false);
	HigherDeckProfile->ContentSet = HigherDeckContentSet;
	HigherDeckProfile->ContinuationEntryLevel = 1;
	HigherDeckProfile->bRequireAllTraversalChannelsReachable = true;

	FLayoutRegionSolveRequest HigherDeckContinuationRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		HigherDeckProfile,
		119,
		TEXT("SnapshotAlignmentHigherDeckContinuation"));
	/* !HigherDeckContinuationRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	HigherDeckContinuationRequest.FootprintSize = FIntPoint(3, 3);
	HigherDeckContinuationRequest.PlannedCells =
	{
		{FIntVector(0, 1, 1), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::Connector},
		{FIntVector(2, 1, 1), ELayoutCellIntent::Entry}
	};
	HigherDeckContinuationRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	HigherDeckContinuationRequest.RootContinuationSelection.FamilyId = TEXT("HigherDeckSurfacePathFamily");
	HigherDeckContinuationRequest.RootContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	HigherDeckContinuationRequest.RootContinuationSelection.ResolvedEntryLevel = 1;

	const auto AddVerticalSupportBoundaryPoint = [&HigherDeckContinuationRequest](const FIntVector& SupportedCell)
	{
		FLayoutSolveBoundaryPoint& BoundaryPoint = HigherDeckContinuationRequest.IncomingBoundaryPoints.AddDefaulted_GetRef();
		BoundaryPoint.LocalCell = SupportedCell + FIntVector(0, 0, -1);
		BoundaryPoint.FaceDirection = ELayoutFaceDirection::PosZ;
		BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
		BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		BoundaryPoint.bRepresentsFilledNeighbor = true;
		BoundaryPoint.bRequiresBoundaryFacing = false;
		BoundaryPoint.SourceRegionDebugPath = TEXT("SnapshotAlignmentHigherDeckSupport");
		BoundaryPoint.SourceCell = BoundaryPoint.LocalCell;
	};

	const auto AddContinuationEndpointBoundaryPoint = [&HigherDeckContinuationRequest](
		const FIntVector& ConnectorLocalCell,
		const ELayoutFaceDirection ConnectorFacingDirection,
		const FLayoutId CommitmentId)
	{
		FLayoutSolveBoundaryPoint& BoundaryPoint = HigherDeckContinuationRequest.IncomingBoundaryPoints.AddDefaulted_GetRef();
		BoundaryPoint.LocalCell = ConnectorLocalCell + FLayoutDirectionUtils::ToCellDelta(ConnectorFacingDirection);
		BoundaryPoint.FaceDirection = FLayoutDirectionUtils::GetOpposite(ConnectorFacingDirection);
		BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceEntry;
		BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		BoundaryPoint.bRepresentsFilledNeighbor = false;
		BoundaryPoint.SourceRegionDebugPath = TEXT("SnapshotAlignmentHigherDeckContinuation");
		BoundaryPoint.SourceCell = BoundaryPoint.LocalCell;
		BoundaryPoint.CommitmentId = CommitmentId;
	};

	const FLayoutId StartCommitmentId(TEXT("HigherDeckContinuation.Start"));
	const FLayoutId EndCommitmentId(TEXT("HigherDeckContinuation.End"));
	AddContinuationEndpointBoundaryPoint(FIntVector(0, 1, 1), ELayoutFaceDirection::NegX, StartCommitmentId);
	AddContinuationEndpointBoundaryPoint(FIntVector(2, 1, 1), ELayoutFaceDirection::PosX, EndCommitmentId);

	AddVerticalSupportBoundaryPoint(FIntVector(0, 1, 1));
	AddVerticalSupportBoundaryPoint(FIntVector(1, 1, 1));
	AddVerticalSupportBoundaryPoint(FIntVector(2, 1, 1));

	const FLayoutRegionSolveResult HigherDeckContinuationResult =
		FLayoutProfileSolver::SolveRegion(HigherDeckContinuationRequest);
	TestTrue(
		*FString::Printf(
			TEXT("Higher-deck continuation solve succeeds without upper boundary-only fill. FailureReason=%s"),
			HigherDeckContinuationResult.SolveResult.FailureReason.IsEmpty()
				? TEXT("<empty>")
				: *HigherDeckContinuationResult.SolveResult.FailureReason),
		HigherDeckContinuationResult.SolveResult.bSucceeded);
	TestEqual(
		TEXT("Higher-deck continuation solve preserves the resolved continuation entry level"),
		HigherDeckContinuationResult.SolveResult.ResolvedTerrainAlignmentLevel,
		1);
	TestTrue(TEXT("Higher-deck continuation solve preserves the upper-level connector intent on the interior route cell"), HigherDeckContinuationResult.SolveResult.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 1, 1)
			&& Placement.Intent == ELayoutCellIntent::Connector;
	}));

	return true;
}

bool FLayoutSnapshotAdapterBuildsContentSetStandaloneRequestTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotContentSetAdapter"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotContentSetTemplate_Generic"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotContentSetModule_Generic"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotContentSetWall");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 7;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotContentSet_Generic"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotContentSetProfile_Generic"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		1,
		false);
	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		109,
		TEXT("SnapshotContentSetStandalone"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		SnapshotTemplatePlacementZOffsetBlocks,
		SnapshotContentSetSolveId);
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(Request);

	TestEqual(TEXT("Content-set request seed preserved"), Request.Seed, 109);
	TestEqual(TEXT("Content-set request preserves the supplied root placement policy id"), Request.RootPlacementPolicyId, SnapshotContentSetPlacementPolicyId);
	TestEqual(TEXT("Content-set request preserves the supplied root candidate id"), Request.RootCandidateId, SnapshotContentSetCandidateId);
	TestEqual(TEXT("Content-set request preserves the supplied root solve id"), Request.RootSolveId, SnapshotContentSetSolveId);
	TestEqual(TEXT("Content-set request preserves the supplied template placement offset"), Request.TemplatePlacementZOffsetBlocks, SnapshotTemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Content-set standalone request stays non-world-facing by default"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Content-set standalone request ignores profile-owned terrain search start Z"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, 0);
	TestEqual(TEXT("Content-set request compiles one content entry"), Request.ContentSetSnapshot.Entries.Num(), 1);
	TestEqual(TEXT("Content-set request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ContentSetSnapshot.Entries.Num() != 1 || Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}
	const FIntVector ContentModuleSharedCellSize = Module->GetEffectiveCellSizeInBlocks();
	TestEqual(TEXT("Content-set request preserves shared cell size on the content-set snapshot"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, ContentModuleSharedCellSize);
	TestEqual(TEXT("Content-set request preserves shared cell size on the module-set snapshot"), Request.ModuleCatalog.SharedCellSizeInBlocks, ContentModuleSharedCellSize);
	TestEqual(TEXT("Content-set request keeps the module-backed content entry id"), Request.ContentSetSnapshot.Entries[0].EntryId, Entry.EntryId);
	TestEqual(TEXT("Execution context normalizes shared cell size from the content-set standalone request"), SolveContext.SharedCellSizeInBlocks, ContentModuleSharedCellSize);
	TestEqual(TEXT("Execution context preserves the content-set request root placement policy id"), SolveContext.PublicationMetadata.RootPlacementPolicyId, SnapshotContentSetPlacementPolicyId);
	TestEqual(TEXT("Execution context preserves the content-set request root candidate id"), SolveContext.PublicationMetadata.RootCandidateId, SnapshotContentSetCandidateId);
	TestEqual(TEXT("Execution context preserves the content-set request root solve id"), SolveContext.PublicationMetadata.RootSolveId, SnapshotContentSetSolveId);
	TestEqual(TEXT("Execution context preserves the content-set request template placement offset"), SolveContext.TemplatePlacementZOffsetBlocks, SnapshotTemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Execution context keeps the default content-set standalone terrain search depth"), SolveContext.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, 0);
	TestEqual(
		TEXT("Scheduled content-set solve result preserves shared cell size without rereading live assets"),
		FLayoutProfileSolver::SolveRegionTree(Request).MergedSolveResult.SharedCellSizeInBlocks,
		ContentModuleSharedCellSize);
	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Content-set request records a shared cell size request-contract assertion"), SharedCellSizeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Content-set request shared cell size assertion uses the request-contract kind"), SharedCellSizeAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestTrue(TEXT("Content-set request shared cell size request-contract assertion passes"), SharedCellSizeAssertion->bPassed);
	const FLayoutValidationAssertionRecord* RootPublicationAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.RootPublicationIdentityContractValid");
	});
	if (!TestNotNull(TEXT("Content-set request records a root publication identity request-contract assertion"), RootPublicationAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Content-set request root publication identity assertion passes"), RootPublicationAssertion->bPassed);
	const FLayoutValidationAssertionRecord* TemplatePlacementOffsetAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.TemplatePlacementOffsetContractValid");
	});
	if (!TestNotNull(TEXT("Content-set request records a template placement offset request-contract assertion"), TemplatePlacementOffsetAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Content-set request template placement offset assertion passes"), TemplatePlacementOffsetAssertion->bPassed);

	return true;
}

bool FLayoutSnapshotAdapterContentSetRequestPreservesExplicitSharedCellSizeOverrideTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotContentSetOverride"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotContentSetOverrideTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotContentSetOverrideModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotContentSetOverrideWall");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 7;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotContentSet_ExplicitOverride"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotContentSetProfile_ExplicitOverride"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		1,
		false);
	Profile->ContentSet = ContentSet;
	const FIntVector SharedCellSizeOverride(8, 8, 6);
	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		110,
		TEXT("SnapshotContentSetExplicitOverride"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		SnapshotTemplatePlacementZOffsetBlocks,
		SnapshotContentSetSolveId,
		&SharedCellSizeOverride);
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(Request);

	TestEqual(TEXT("Content-set request preserves the explicit request-owned shared-cell override on the content-set snapshot"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, SharedCellSizeOverride);
	TestEqual(TEXT("Content-set request preserves the explicit request-owned shared-cell override on the module-set snapshot"), Request.ModuleCatalog.SharedCellSizeInBlocks, SharedCellSizeOverride);
	TestEqual(TEXT("Execution context normalizes shared cell size from the explicit request-owned override"), SolveContext.SharedCellSizeInBlocks, SharedCellSizeOverride);
	TestEqual(TEXT("Execution context still preserves the request-owned template placement offset when the explicit shared-cell override is present"), SolveContext.TemplatePlacementZOffsetBlocks, SnapshotTemplatePlacementZOffsetBlocks);
	TestEqual(
		TEXT("Scheduled content-set solve result preserves the explicit request-owned shared-cell override"),
		FLayoutProfileSolver::SolveRegionTree(Request).MergedSolveResult.SharedCellSizeInBlocks,
		SharedCellSizeOverride);

	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Explicit shared-cell override request records the shared-cell-size request-contract assertion"), SharedCellSizeAssertion))
	{
		return false;
	}

	TestTrue(TEXT("Explicit shared-cell override request keeps the shared-cell-size request-contract assertion passing"), SharedCellSizeAssertion->bPassed);
	return true;
}

bool FLayoutSnapshotAdapterContentSetRequestIgnoresStaleModuleSnapshotSharedCellSizeTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotContentSetStaleModuleSharedCellSize"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotTemplate_ContentSetStaleModuleSharedCellSize"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotModule_ContentSetStaleModuleSharedCellSize"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotContentEntry_StaleModuleSharedCellSize");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotContentSet_StaleModuleSharedCellSize"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotProfile_StaleModuleSharedCellSize"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		1,
		false);

	Profile->ContentSet = ContentSet;
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		211,
		TEXT("SnapshotContentSetStaleModuleSharedCellSize"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		SnapshotTemplatePlacementZOffsetBlocks,
		SnapshotContentSetSolveId);

	Request.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(24, 24, 24);
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(Request);
	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = SolveContext.RootRequest.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});

	if (!TestNotNull(TEXT("Content-set request still records the shared-cell-size request-contract assertion"), SharedCellSizeAssertion))
	{
		return false;
	}

	TestTrue(TEXT("Shared-cell-size request-contract assertion stays passing when the stale module snapshot diverges"), SharedCellSizeAssertion->bPassed);
	TestEqual(TEXT("Execution context keeps the content-set shared cell size when the module snapshot is stale"), SolveContext.SharedCellSizeInBlocks, Request.ContentSetSnapshot.SharedCellSizeInBlocks);
	TestEqual(
		TEXT("Scheduled content-set solve result keeps the content-set shared cell size when the module snapshot is stale"),
		FLayoutProfileSolver::SolveRegionTree(Request).MergedSolveResult.SharedCellSizeInBlocks,
		Request.ContentSetSnapshot.SharedCellSizeInBlocks);
	TestEqual(TEXT("Execution context keeps the request template placement offset when request shared metrics stay usable"), SolveContext.TemplatePlacementZOffsetBlocks, SnapshotTemplatePlacementZOffsetBlocks);

	return true;
}

bool FLayoutSnapshotAdapterChildOnlyContentSetRequestIgnoresCompatibilitySharedCellSizeFieldTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotChildOnlyContentSetCompatibilitySharedCell"));

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("SnapshotChildOnlyChildProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("SnapshotChildOnlyEntry");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotContentSet_ChildOnlyCompatibilitySharedCell"),
		{ChildEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotProfile_ChildOnlyCompatibilitySharedCell"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);

	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		311,
		TEXT("SnapshotChildOnlyCompatibilitySharedCell"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		0,
		SnapshotContentSetSolveId);
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(Request);

	TestEqual(TEXT("Child-only content-set request no longer freezes the compatibility shared cell size on the content-set snapshot"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, FIntVector::ZeroValue);
	TestEqual(TEXT("Child-only content-set request no longer freezes the compatibility shared cell size on the module-set snapshot"), Request.ModuleCatalog.SharedCellSizeInBlocks, FIntVector::ZeroValue);
	TestEqual(TEXT("Execution context keeps zero shared metrics when child-only content has no derived structural source and no explicit override"), SolveContext.SharedCellSizeInBlocks, FIntVector::ZeroValue);

	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Child-only request still records the shared-cell-size request-contract assertion"), SharedCellSizeAssertion))
	{
		return false;
	}

	TestTrue(TEXT("Child-only request shared-cell-size assertion still passes when both frozen snapshots intentionally carry zero metrics"), SharedCellSizeAssertion->bPassed);
	return true;
}

bool FLayoutSnapshotAdapterRejectsSteppedSolveWithoutSupportMapTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotSteppedSupportMissing"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotSteppedSupportMissingTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotSteppedSupportMissingModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotSteppedSupportMissingEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotSteppedSupportMissingContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotSteppedSupportMissingProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutWorldBindingPlacementPolicy SteppedPlacementPolicy;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		311,
		TEXT("SnapshotSteppedSupportMissing"),
		FLayoutSolverExecutionSettings(),
		TEXT("SteppedSupportPolicy"),
		TEXT("SteppedSupportCandidate"),
		0,
		TEXT("SteppedSupportSolve"),
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		SteppedPlacementPolicy);

	const FLayoutValidationAssertionRecord* SteppedSupportAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportContractValid");
	});
	if (!TestNotNull(TEXT("Stepped request records a stepped-terrain-support request-contract assertion"), SteppedSupportAssertion))
	{
		return false;
	}

	TestEqual(TEXT("Stepped-terrain-support assertion uses the request-contract kind"), SteppedSupportAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestFalse(TEXT("Stepped-terrain-support assertion fails when no support map is preserved"), SteppedSupportAssertion->bPassed);
	TestTrue(TEXT("Stepped-terrain-support assertion reports the missing support map"), SteppedSupportAssertion->FailureReason.Contains(TEXT("does not carry a usable stepped terrain support map")));
	TestTrue(TEXT("Stepped-terrain-support assertion reports the frozen profile name"), SteppedSupportAssertion->FailureReason.Contains(TEXT("SnapshotSteppedSupportMissingProfile")));
	TestTrue(TEXT("Stepped-terrain-support assertion reports the live profile path when one is still available"), SteppedSupportAssertion->FailureReason.Contains(Profile->GetPathName()));
	TestTrue(TEXT("Stepped-terrain-support assertion references the frozen profile snapshot id"), SteppedSupportAssertion->RelatedIds.Contains(Request.ProfileSnapshot.SnapshotId));
	TestTrue(TEXT("Stepped-terrain-support assertion references the request candidate id"), SteppedSupportAssertion->RelatedIds.Contains(Request.RootCandidateId));
	TestTrue(TEXT("Stepped-terrain-support assertion references the request solve id"), SteppedSupportAssertion->RelatedIds.Contains(Request.RootSolveId));
	return true;
}

bool FLayoutSnapshotAdapterAllowsFinalizedFlatFallbackWithoutSteppedSupportTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* const Profile = CreateProfileWithUniversalContentSet(
		CreateSnapshotTestOuter(TEXT("LayoutSnapshotFlatFallback")),
		TEXT("LayoutSnapshotFlatFallbackProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		317,
		TEXT("SnapshotFlatFallback"));
	Request.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	Request.bHasSelectedModePlan = true;
	Request.SelectedModePlan.bUsesSteppedTerrainTopology = false;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(Request);

	const FLayoutValidationAssertionRecord* const SupportAssertion =
		Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportContractValid");
		});
	if (!TestNotNull(TEXT("Finalized flat fallback keeps stepped-support assertion"), SupportAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Finalized flat fallback does not require discarded stepped support"), SupportAssertion->bPassed);
	return true;
}

bool FLayoutSnapshotAdapterPreservesSteppedTerrainSupportMapTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotSteppedSupportPresent"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotSteppedSupportPresentTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotSteppedSupportPresentModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotSteppedSupportPresentEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotSteppedSupportPresentContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotSteppedSupportPresentProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutWorldBindingPlacementPolicy SteppedPlacementPolicy;

	FLayoutSteppedTerrainSupportMap SupportMap;
	SupportMap.SharedCellHeightInBlocks = 16;
	FLayoutSteppedTerrainSupportSample& FirstSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstSupportSample.SupportSurfaceZ = 96;
	FLayoutSteppedTerrainSupportSample& SecondSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondSupportSample.SupportSurfaceZ = 112;
	FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep = SupportMap.AdjacencySteps.AddDefaulted_GetRef();
	AdjacencyStep.FromCell = FIntVector(0, 0, 0);
	AdjacencyStep.ToCell = FIntVector(1, 0, 0);
	AdjacencyStep.StepHeightBlocks = 16;
	SupportMap.MaximumObservedNeighborHeightDelta = 16;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		312,
		TEXT("SnapshotSteppedSupportPresent"),
		FLayoutSolverExecutionSettings(),
		TEXT("SteppedSupportPolicy"),
		TEXT("SteppedSupportCandidate"),
		0,
		TEXT("SteppedSupportSolve"),
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		SteppedPlacementPolicy,
		&SupportMap);

	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(2, 1);
	Request.PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	Request.PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});
	Request.ProfileSnapshot.SourceProfile = nullptr;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(Request);

	TestEqual(TEXT("Stepped request preserves the shared cell height on the support map"), Request.SteppedTerrainSupportMap.SharedCellHeightInBlocks, SupportMap.SharedCellHeightInBlocks);
	TestEqual(TEXT("Stepped request preserves the support sample count"), Request.SteppedTerrainSupportMap.SupportSamples.Num(), SupportMap.SupportSamples.Num());
	TestEqual(TEXT("Stepped request preserves the adjacency step count"), Request.SteppedTerrainSupportMap.AdjacencySteps.Num(), SupportMap.AdjacencySteps.Num());
	TestEqual(TEXT("Stepped request preserves the maximum observed neighbor delta"), Request.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta, SupportMap.MaximumObservedNeighborHeightDelta);

	const FLayoutValidationAssertionRecord* SteppedSupportAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportContractValid");
	});
	if (!TestNotNull(TEXT("Stepped request with support map records a stepped-terrain-support assertion"), SteppedSupportAssertion))
	{
		return false;
	}

	TestTrue(TEXT("Stepped-terrain-support assertion passes when the request preserves a support map"), SteppedSupportAssertion->bPassed);

	const FLayoutValidationAssertionRecord* SteppedCoverageAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
	});
	if (!TestNotNull(TEXT("Stepped request with supplied planned cells records a stepped-terrain-support-coverage assertion"), SteppedCoverageAssertion))
	{
		return false;
	}

	TestTrue(TEXT("Stepped-terrain-support-coverage assertion passes when the support map covers every supplied planned cell"), SteppedCoverageAssertion->bPassed);

	Request.PlannedCells[1].Cell.Z = 1;
	Request.PlannedCells[1].ModuleLevelIndex = 0;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(Request);
	SteppedCoverageAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
	});
	TestTrue(
		TEXT("Stepped support covers a terrain column after its authored cell shifts vertically"),
		SteppedCoverageAssertion != nullptr && SteppedCoverageAssertion->bPassed);
	return true;
}

bool FLayoutSnapshotAdapterRejectsSteppedPolicyWithoutSteppedCapableFrozenProfileTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotSteppedCapabilityMissing"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotSteppedCapabilityMissingTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotSteppedCapabilityMissingModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotSteppedCapabilityMissingEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotSteppedCapabilityMissingContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotSteppedCapabilityMissingProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutWorldBindingPlacementPolicy SteppedPlacementPolicy;

	FLayoutSteppedTerrainSupportMap SupportMap;
	SupportMap.SharedCellHeightInBlocks = 16;
	FLayoutSteppedTerrainSupportSample& FirstSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstSupportSample.SupportSurfaceZ = 96;
	FLayoutSteppedTerrainSupportSample& SecondSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	SecondSupportSample.LocalCell = FIntVector(1, 0, 0);
	SecondSupportSample.SupportSurfaceZ = 112;
	FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep = SupportMap.AdjacencySteps.AddDefaulted_GetRef();
	AdjacencyStep.FromCell = FIntVector(0, 0, 0);
	AdjacencyStep.ToCell = FIntVector(1, 0, 0);
	AdjacencyStep.StepHeightBlocks = 16;
	SupportMap.MaximumObservedNeighborHeightDelta = 16;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		312,
		TEXT("SnapshotSteppedCapabilityMissing"),
		FLayoutSolverExecutionSettings(),
		TEXT("SteppedCapabilityPolicy"),
		TEXT("SteppedCapabilityCandidate"),
		0,
		TEXT("SteppedCapabilitySolve"),
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		SteppedPlacementPolicy,
		&SupportMap);

	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(2, 1);
	Request.PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	Request.PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(Request);

	TestFalse(TEXT("Frozen stepped-capability snapshot stays disabled when the source profile does not opt in"), Request.ProfileSnapshot.bSupportsSteppedTerrainSolve);

	const FLayoutValidationAssertionRecord* CapabilityAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedProfileCapabilityContractValid");
	});
	if (!TestNotNull(TEXT("Stepped-capability snapshot request records a frozen-profile capability assertion"), CapabilityAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Frozen-profile capability assertion fails when stepped policy targets a non-stepped frozen profile"), CapabilityAssertion->bPassed);
	TestTrue(TEXT("Frozen-profile capability assertion reports the profile name"), CapabilityAssertion->FailureReason.Contains(TEXT("SnapshotSteppedCapabilityMissingProfile")));
	TestTrue(TEXT("Frozen-profile capability assertion reports missing stepped capability"), CapabilityAssertion->FailureReason.Contains(TEXT("does not declare stepped-terrain capability")));
	TestTrue(TEXT("Frozen-profile capability assertion references the frozen profile snapshot id"), CapabilityAssertion->RelatedIds.Contains(Request.ProfileSnapshot.SnapshotId));
	TestTrue(TEXT("Frozen-profile capability assertion references the request candidate id"), CapabilityAssertion->RelatedIds.Contains(Request.RootCandidateId));

	const FLayoutValidationAssertionRecord* SteppedSupportAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportContractValid");
	});
	if (!TestNotNull(TEXT("Stepped-capability snapshot request still records the stepped-support assertion"), SteppedSupportAssertion))
	{
		return false;
	}

	TestTrue(TEXT("Stepped-capability snapshot request still preserves a passing stepped-support assertion"), SteppedSupportAssertion->bPassed);
	return true;
}

bool FLayoutSnapshotAdapterRejectsSteppedSupportOutsideSuppliedPlannedCellsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotSteppedSupportCoverage"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotSteppedSupportCoverageTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotSteppedSupportCoverageModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotSteppedSupportCoverageEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotSteppedSupportCoverageContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotSteppedSupportCoverageProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutWorldBindingPlacementPolicy SteppedPlacementPolicy;

	FLayoutSteppedTerrainSupportMap SupportMap;
	SupportMap.SharedCellHeightInBlocks = 16;
	FLayoutSteppedTerrainSupportSample& FirstSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstSupportSample.SupportSurfaceZ = 96;
	FLayoutSteppedTerrainSupportSample& UnsupportedSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	UnsupportedSupportSample.LocalCell = FIntVector(1, 0, 0);
	UnsupportedSupportSample.SupportSurfaceZ = 112;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		313,
		TEXT("SnapshotSteppedSupportCoverage"),
		FLayoutSolverExecutionSettings(),
		TEXT("SteppedSupportCoveragePolicy"),
		TEXT("SteppedSupportCoverageCandidate"),
		0,
		TEXT("SteppedSupportCoverageSolve"),
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		SteppedPlacementPolicy,
		&SupportMap);
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	Request.PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	Request.ProfileSnapshot.SourceProfile = nullptr;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(Request);

	const FLayoutValidationAssertionRecord* SteppedCoverageAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
	});
	if (!TestNotNull(TEXT("Coverage-mismatched stepped request records a stepped-terrain-support-coverage assertion"), SteppedCoverageAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Stepped-terrain-support-coverage assertion fails when the support map includes one local cell outside the supplied planned-cell set"), SteppedCoverageAssertion->bPassed);
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion reports the unsupported support cell"), SteppedCoverageAssertion->FailureReason.Contains(TEXT("X=1 Y=0 Z=0")));
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion reports the frozen profile name"), SteppedCoverageAssertion->FailureReason.Contains(TEXT("SnapshotSteppedSupportCoverageProfile")));
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion references the frozen profile snapshot id"), SteppedCoverageAssertion->RelatedIds.Contains(Request.ProfileSnapshot.SnapshotId));
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion references the request candidate id"), SteppedCoverageAssertion->RelatedIds.Contains(Request.RootCandidateId));
	return true;
}

bool FLayoutSnapshotAdapterRejectsSteppedSupportMissingSuppliedPlannedCellTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotSteppedSupportMissingCoverage"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotSteppedSupportMissingCoverageTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotSteppedSupportMissingCoverageModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotSteppedSupportMissingCoverageEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotSteppedSupportMissingCoverageContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotSteppedSupportMissingCoverageProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutWorldBindingPlacementPolicy SteppedPlacementPolicy;

	FLayoutSteppedTerrainSupportMap SupportMap;
	SupportMap.SharedCellHeightInBlocks = 16;
	FLayoutSteppedTerrainSupportSample& FirstSupportSample = SupportMap.SupportSamples.AddDefaulted_GetRef();
	FirstSupportSample.LocalCell = FIntVector(0, 0, 0);
	FirstSupportSample.SupportSurfaceZ = 96;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		314,
		TEXT("SnapshotSteppedSupportMissingCoverage"),
		FLayoutSolverExecutionSettings(),
		TEXT("SteppedSupportMissingCoveragePolicy"),
		TEXT("SteppedSupportMissingCoverageCandidate"),
		0,
		TEXT("SteppedSupportMissingCoverageSolve"),
		ELayoutWorldBindingPlacementKind::OrdinaryRoot,
		SteppedPlacementPolicy,
		&SupportMap);
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(2, 1);
	Request.PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	Request.PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});
	Request.ProfileSnapshot.SourceProfile = nullptr;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(Request);

	const FLayoutValidationAssertionRecord* SteppedCoverageAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
	});
	if (!TestNotNull(TEXT("Coverage-mismatched stepped request records a stepped-terrain-support-coverage assertion"), SteppedCoverageAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Stepped-terrain-support-coverage assertion fails when the support map misses a supplied planned cell"), SteppedCoverageAssertion->bPassed);
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion reports the missing planned cell"), SteppedCoverageAssertion->FailureReason.Contains(TEXT("X=1 Y=0 Z=0")));
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion reports the frozen profile name"), SteppedCoverageAssertion->FailureReason.Contains(TEXT("SnapshotSteppedSupportMissingCoverageProfile")));
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion references the frozen profile snapshot id"), SteppedCoverageAssertion->RelatedIds.Contains(Request.ProfileSnapshot.SnapshotId));
	TestTrue(TEXT("Stepped-terrain-support-coverage assertion references the request candidate id"), SteppedCoverageAssertion->RelatedIds.Contains(Request.RootCandidateId));
	return true;
}

bool FLayoutSnapshotAdapterRejectsForcedPlacementBundleInsertionWithoutMatchingBundleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotForcedBundleInsertionContract"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotForcedBundleInsertionTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotForcedBundleInsertionModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotForcedBundleInsertionProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotForcedBundleInsertionEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotForcedBundleInsertionContentSet"),
		{Entry});

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		9031,
		TEXT("SnapshotForcedBundleInsertion"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;

	FLayoutForcedPlacementBundleInsertion& Insertion =
		Request.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	Insertion.BundleId = TEXT("SnapshotForcedBundleInsertion.InvalidBundle");
	Insertion.AnchorCell = FIntVector::ZeroValue;
	Insertion.ProvingCell = FIntVector::ZeroValue;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(Request);

	const FLayoutValidationAssertionRecord* ForcedInsertionAssertion =
		Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.ForcedPlacementBundleInsertionContractValid");
		});
	if (!TestNotNull(TEXT("Forced insertion request records a request-contract assertion"), ForcedInsertionAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Forced insertion request-contract assertion fails when the bundle id is not part of the frozen request snapshots"), ForcedInsertionAssertion->bPassed);
	TestTrue(TEXT("Forced insertion request-contract assertion reports the missing bundle id"), ForcedInsertionAssertion->FailureReason.Contains(TEXT("does not exist in the frozen module snapshot set")));
	TestTrue(TEXT("Forced insertion request-contract assertion reports the request region path"), ForcedInsertionAssertion->FailureReason.Contains(TEXT("SnapshotForcedBundleInsertion")));
	TestTrue(TEXT("Forced insertion request-contract assertion reports the snapshot name"), ForcedInsertionAssertion->FailureReason.Contains(TEXT("SnapshotForcedBundleInsertion")));
	TestTrue(TEXT("Forced insertion request-contract assertion references the request snapshot id"), ForcedInsertionAssertion->RelatedIds.Contains(Request.EffectiveSnapshotId));
	TestTrue(TEXT("Forced insertion request-contract assertion references the request root solve id"), ForcedInsertionAssertion->RelatedIds.Contains(Request.RootSolveId));
	TestEqual(TEXT("The request still exposes the deterministic valid bundle id for the only module snapshot"), BuildRequestModuleBundleId(Request, 0), FLayoutId(*FString::Printf(
		TEXT("%s.Bundle.0.%s"),
		*Request.EffectiveSnapshotId.ToString(),
		*Request.ModuleCatalog.Modules[0].SnapshotId.ToString())));
	return true;
}

bool FLayoutSnapshotAdapterRejectsConflictingForcedPlacementBundleInsertionsOnSameAnchorTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotForcedBundleInsertionAnchorConflict"));

	UChunkStructureTemplate* TemplateA = CreateTemplate(Outer, TEXT("SnapshotForcedBundleInsertionConflictTemplateA"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* ModuleA = CreateModule(
		Outer,
		TEXT("SnapshotForcedBundleInsertionConflictModuleA"),
		TemplateA,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	UChunkStructureTemplate* TemplateB = CreateTemplate(Outer, TEXT("SnapshotForcedBundleInsertionConflictTemplateB"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* ModuleB = CreateModule(
		Outer,
		TEXT("SnapshotForcedBundleInsertionConflictModuleB"),
		TemplateB,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotForcedBundleInsertionConflictProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	FLayoutRegionContentEntry EntryA;
	EntryA.EntryId = TEXT("SnapshotForcedBundleInsertionConflictA");
	EntryA.ContentKind = ELayoutRegionContentKind::Module;
	EntryA.ModuleSettings.Module = ModuleA;
	FLayoutRegionContentEntry EntryB;
	EntryB.EntryId = TEXT("SnapshotForcedBundleInsertionConflictB");
	EntryB.ContentKind = ELayoutRegionContentKind::Module;
	EntryB.ModuleSettings.Module = ModuleB;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotForcedBundleInsertionConflictContentSet"),
		{EntryA, EntryB});

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		9032,
		TEXT("SnapshotForcedBundleInsertionConflict"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;

	FLayoutForcedPlacementBundleInsertion& FirstInsertion =
		Request.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	FirstInsertion.BundleId = BuildRequestModuleBundleId(Request, 0);
	FirstInsertion.AnchorCell = FIntVector::ZeroValue;
	FirstInsertion.ProvingCell = FIntVector::ZeroValue;

	FLayoutForcedPlacementBundleInsertion& SecondInsertion =
		Request.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
	SecondInsertion.BundleId = BuildRequestModuleBundleId(Request, 1);
	SecondInsertion.AnchorCell = FIntVector::ZeroValue;
	SecondInsertion.ProvingCell = FIntVector::ZeroValue;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(Request);

	const FLayoutValidationAssertionRecord* ForcedInsertionAssertion =
		Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.ForcedPlacementBundleInsertionContractValid");
		});
	if (!TestNotNull(TEXT("Conflicting forced insertion request records a request-contract assertion"), ForcedInsertionAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Forced insertion request-contract assertion fails when two valid bundles target the same anchor cell"), ForcedInsertionAssertion->bPassed);
	TestTrue(TEXT("Forced insertion request-contract assertion reports the conflicting anchor cell"), ForcedInsertionAssertion->FailureReason.Contains(TEXT("conflicting forced placement bundle insertions on anchor cell")));
	return true;
}

bool FLayoutSnapshotAdapterRejectsRequiredRouteConstraintOutsidePlannedCellsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotRequiredRouteConstraintContract"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotRequiredRouteConstraintTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotRequiredRouteConstraintModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotRequiredRouteConstraintProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("RequiredRouteModule");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("SnapshotRequiredRouteContentSet"), {Entry});
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		9032,
		TEXT("SnapshotRequiredRouteConstraint"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::Boundary;

	FLayoutRouteConstraintRecord& RouteConstraint = Request.RequiredRouteConstraints.AddDefaulted_GetRef();
	RouteConstraint.ConstraintId = TEXT("Snapshot.RequiredRoute.InvalidCell");
	RouteConstraint.Cell = FIntVector(1, 0, 0);
	RouteConstraint.Intent = ELayoutCellIntent::Boundary;
	FLayoutRouteFaceRequirement& Requirement = RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
	Requirement.FaceDirection = ELayoutFaceDirection::PosX;
	Requirement.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(Request);

	const FLayoutValidationAssertionRecord* RouteConstraintAssertion =
		Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.RequiredRouteConstraintContractValid");
		});
	if (!TestNotNull(TEXT("Required route constraint request records a request-contract assertion"), RouteConstraintAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Required route constraint assertion fails when the constrained cell is outside the supplied planned-cell set"), RouteConstraintAssertion->bPassed);
	TestTrue(TEXT("Required route constraint assertion reports the missing planned cell"), RouteConstraintAssertion->FailureReason.Contains(TEXT("not present in the supplied planned-cell set")));
	TestTrue(TEXT("Required route constraint assertion reports the request region path"), RouteConstraintAssertion->FailureReason.Contains(TEXT("SnapshotRequiredRouteConstraint")));
	TestTrue(TEXT("Required route constraint assertion references the request snapshot id"), RouteConstraintAssertion->RelatedIds.Contains(Request.EffectiveSnapshotId));
	TestTrue(TEXT("Required route constraint assertion references the route constraint id"), RouteConstraintAssertion->RelatedIds.Contains(RouteConstraint.ConstraintId));
	return true;
}

bool FLayoutSnapshotAdapterRejectsConflictingRequiredRouteFaceChannelsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotRequiredRouteConstraintConflict"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotRequiredRouteConstraintConflictTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotRequiredRouteConstraintConflictModule"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary, LayoutGameplayTags::TraversalSecondary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary, LayoutGameplayTags::TraversalSecondary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary, LayoutGameplayTags::TraversalSecondary})));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotRequiredRouteConstraintConflictProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		9033,
		TEXT("SnapshotRequiredRouteConstraintConflict"));
	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector::ZeroValue;
	PlannedCell.Intent = ELayoutCellIntent::Interior;

	FLayoutRouteConstraintRecord& RouteConstraint = Request.RequiredRouteConstraints.AddDefaulted_GetRef();
	RouteConstraint.ConstraintId = TEXT("Snapshot.RequiredRoute.ConflictingFaceChannel");
	RouteConstraint.Cell = FIntVector::ZeroValue;
	RouteConstraint.Intent = ELayoutCellIntent::Interior;
	FLayoutRouteFaceRequirement& FirstRequirement = RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
	FirstRequirement.FaceDirection = ELayoutFaceDirection::PosX;
	FirstRequirement.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	FLayoutRouteFaceRequirement& SecondRequirement = RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
	SecondRequirement.FaceDirection = ELayoutFaceDirection::PosX;
	SecondRequirement.TraversalChannel = LayoutGameplayTags::TraversalSecondary;

	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(Request);

	const FLayoutValidationAssertionRecord* RouteConstraintAssertion =
		Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.RequiredRouteConstraintContractValid");
		});
	if (!TestNotNull(TEXT("Conflicting required route constraint request records a request-contract assertion"), RouteConstraintAssertion))
	{
		return false;
	}

	TestFalse(TEXT("Required route constraint assertion fails when one constrained face preserves conflicting traversal channels"), RouteConstraintAssertion->bPassed);
	TestTrue(TEXT("Required route constraint assertion reports the conflicting constrained face"), RouteConstraintAssertion->FailureReason.Contains(TEXT("conflicting required route face requirements")));
	return true;
}

bool FLayoutSnapshotAdapterDefaultsRootIdsFromRegionPathTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotDefaultRootIds"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotDefaultRootIdsTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotDefaultRootIdsModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Entry,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotDefaultRootIdsEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotDefaultRootIdsContentSet"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotDefaultRootIdsProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		1,
		false);
	Profile->ContentSet = ContentSet;

	const FString SnapshotRegionPath = TEXT("Standalone/DefaultRootIds");
	const FLayoutRegionSolveRequest SnapshotRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile,
		211,
		SnapshotRegionPath);
	TestEqual(TEXT("Snapshot standalone request defaults the root candidate id from the region path"), SnapshotRequest.RootCandidateId, FLayoutId(*SnapshotRegionPath));
	TestEqual(TEXT("Snapshot standalone request defaults the root solve id from the region path"), SnapshotRequest.RootSolveId, FLayoutId(*SnapshotRegionPath));
	TestEqual(TEXT("Snapshot execution context preserves the defaulted root candidate id"), BuildSolveContext(SnapshotRequest).PublicationMetadata.RootCandidateId, FLayoutId(*SnapshotRegionPath));
	TestEqual(TEXT("Snapshot execution context preserves the defaulted root solve id"), BuildSolveContext(SnapshotRequest).PublicationMetadata.RootSolveId, FLayoutId(*SnapshotRegionPath));

	const FString ContentSetRegionPath = TEXT("Standalone/ContentSetDefaultRootIds");
	const FLayoutRegionSolveRequest ContentSetRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		223,
		ContentSetRegionPath);
	TestEqual(TEXT("Content-set standalone request defaults the root candidate id from the region path"), ContentSetRequest.RootCandidateId, FLayoutId(*ContentSetRegionPath));
	TestEqual(TEXT("Content-set standalone request defaults the root solve id from the region path"), ContentSetRequest.RootSolveId, FLayoutId(*ContentSetRegionPath));
	TestEqual(TEXT("Content-set execution context preserves the defaulted root candidate id"), BuildSolveContext(ContentSetRequest).PublicationMetadata.RootCandidateId, FLayoutId(*ContentSetRegionPath));
	TestEqual(TEXT("Content-set execution context preserves the defaulted root solve id"), BuildSolveContext(ContentSetRequest).PublicationMetadata.RootSolveId, FLayoutId(*ContentSetRegionPath));

	const FString ProfileRegionPath = TEXT("Standalone/ProfileDefaultRootIds");
	const FLayoutRegionSolveRequest ProfileRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		227,
		ProfileRegionPath);
	TestEqual(TEXT("Profile standalone request defaults the root candidate id from the region path"), ProfileRequest.RootCandidateId, FLayoutId(*ProfileRegionPath));
	TestEqual(TEXT("Profile standalone request defaults the root solve id from the region path"), ProfileRequest.RootSolveId, FLayoutId(*ProfileRegionPath));
	TestEqual(TEXT("Profile execution context preserves the defaulted root candidate id"), BuildSolveContext(ProfileRequest).PublicationMetadata.RootCandidateId, FLayoutId(*ProfileRegionPath));
	TestEqual(TEXT("Profile execution context preserves the defaulted root solve id"), BuildSolveContext(ProfileRequest).PublicationMetadata.RootSolveId, FLayoutId(*ProfileRegionPath));

	return true;
}

bool FLayoutSnapshotAdapterBuildsRootAnchoredLiveCompositeBundleRequestTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotCompositeRequestGuard"));

	UChunkStructureTemplate* LowerTemplate = CreateTemplate(Outer, TEXT("SnapshotCompositeLowerTemplate"), FIntVector(16, 16, 16));
	UChunkStructureTemplate* UpperTemplate = CreateTemplate(Outer, TEXT("SnapshotCompositeUpperTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* LowerModule = CreateModule(
		Outer,
		TEXT("SnapshotCompositeLowerModule"),
		LowerTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* UpperModule = CreateModule(
		Outer,
		TEXT("SnapshotCompositeUpperModule"),
		UpperTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("SnapshotCompositeModule"));
	{
		FLayoutCompositeModuleCell& LowerCell = Composite->Cells.AddDefaulted_GetRef();
		LowerCell.Module = LowerModule;
		LowerCell.LocalCell = FIntVector(0, 0, 0);
		LowerCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& UpperCell = Composite->Cells.AddDefaulted_GetRef();
		UpperCell.Module = UpperModule;
		UpperCell.LocalCell = FIntVector(0, 0, 1);
		UpperCell.RelativeYawRotationSteps = 0;
	}

	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Synthetic composite request guard fixture validates the composite authoring"), CompositeValidation.IsValid()))
	{
		return false;
	}

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotCompositeEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 3;
	Entry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotCompositeContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotCompositeProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		211,
		TEXT("SnapshotCompositeStandalone"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		0,
		SnapshotContentSetSolveId);

	TestEqual(TEXT("Composite request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}
	TestNotNull(TEXT("Composite request keeps the composite source on the module snapshot"), Request.ModuleCatalog.Modules[0].SourceCompositeModule.Get());
	TestTrue(
		TEXT("Composite request keeps the occupied local root cell required by the live bundle contract"),
		Request.ModuleCatalog.Modules[0].OccupiedLocalCells.Contains(FIntVector::ZeroValue));
	TestTrue(
		TEXT("Composite request keeps non-empty root anchor intents required by the live bundle contract"),
		!Request.ModuleCatalog.Modules[0].RootSupportedCellIntents.IsEmpty());
	const FLayoutValidationAssertionRecord* CompositeGuardAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.LiveCompositeBundlePlacementSupported");
	});
	if (!TestNotNull(TEXT("Composite request records the live composite bundle request-contract assertion"), CompositeGuardAssertion))
	{
		return false;
	}

	TestEqual(TEXT("Composite request guard assertion uses the request-contract kind"), CompositeGuardAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestTrue(TEXT("Composite request guard assertion now passes on the live bundle-aware placement contract surface"), CompositeGuardAssertion->bPassed);
	TestTrue(
		TEXT("Composite request guard assertion keeps an empty failure reason when the bundle contract is present"),
		CompositeGuardAssertion->FailureReason.IsEmpty());

	return true;
}

bool FLayoutSnapshotAdapterRejectsLiveCompositeBundleWithoutRootAnchorContractTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotRejectsLiveCompositeRootAnchorContract"));
	UChunkStructureTemplate* LowerTemplate = CreateTemplate(Outer, TEXT("SnapshotCompositeInvalidLowerTemplate"), FIntVector(16, 16, 16));
	UChunkStructureTemplate* UpperTemplate = CreateTemplate(Outer, TEXT("SnapshotCompositeInvalidUpperTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* LowerModule = CreateModule(
		Outer,
		TEXT("SnapshotCompositeInvalidLowerModule"),
		LowerTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* UpperModule = CreateModule(
		Outer,
		TEXT("SnapshotCompositeInvalidUpperModule"),
		UpperTemplate,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior,
			ELayoutCellIntent::VerticalAccess
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("SnapshotCompositeInvalidPair"));
	{
		FLayoutCompositeModuleCell& FirstCell = Composite->Cells.AddDefaulted_GetRef();
		FirstCell.Module = LowerModule;
		FirstCell.LocalCell = FIntVector(0, 0, 0);
		FirstCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& SecondCell = Composite->Cells.AddDefaulted_GetRef();
		SecondCell.Module = UpperModule;
		SecondCell.LocalCell = FIntVector(0, 0, 1);
		SecondCell.RelativeYawRotationSteps = 0;
	}

	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Synthetic invalid composite request guard fixture validates the composite authoring"), CompositeValidation.IsValid()))
	{
		return false;
	}

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotCompositeInvalidEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 3;
	Entry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotCompositeInvalidContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotCompositeInvalidProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		212,
		TEXT("SnapshotCompositeInvalidStandalone"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		0,
		SnapshotContentSetSolveId);

	TestEqual(TEXT("Invalid composite request still starts with one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}
	Request.ModuleCatalog.Modules[0].RootSupportedCellIntents.Reset();
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneLiveCompositeBundleAssertions(Request);

	const FLayoutValidationAssertionRecord* CompositeGuardAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.LiveCompositeBundlePlacementSupported");
	});
	if (!TestNotNull(TEXT("Mutated composite request still records the live composite bundle request-contract assertion"), CompositeGuardAssertion))
	{
		return false;
	}

	TestEqual(TEXT("Mutated composite request guard assertion uses the request-contract kind"), CompositeGuardAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestFalse(TEXT("Mutated composite request guard assertion fails once the frozen request drops root-supported intents"), CompositeGuardAssertion->bPassed);
	TestTrue(TEXT("Mutated composite request guard assertion reports the missing root-anchor contract"), CompositeGuardAssertion->FailureReason.Contains(TEXT("RootSupportedCellIntents")));
	TestTrue(TEXT("Mutated composite request guard assertion reports the request region path"), CompositeGuardAssertion->FailureReason.Contains(TEXT("SnapshotCompositeInvalidStandalone")));
	TestTrue(TEXT("Mutated composite request guard assertion reports the frozen module-set snapshot id"), CompositeGuardAssertion->FailureReason.Contains(Request.ModuleCatalog.SnapshotId.ToString()));
	TestTrue(TEXT("Mutated composite request guard assertion reports the live composite module path when one is still available"), CompositeGuardAssertion->FailureReason.Contains(Composite->GetPathName()));
	TestTrue(TEXT("Mutated composite request guard assertion references the request snapshot id"), CompositeGuardAssertion->RelatedIds.Contains(Request.EffectiveSnapshotId));
	TestTrue(TEXT("Mutated composite request guard assertion references the module-set snapshot id"), CompositeGuardAssertion->RelatedIds.Contains(Request.ModuleCatalog.SnapshotId));
	TestTrue(TEXT("Mutated composite request guard assertion references the invalid module snapshot id"), CompositeGuardAssertion->RelatedIds.Contains(Request.ModuleCatalog.Modules[0].SnapshotId));
	return true;
}

bool FLayoutSnapshotAdapterRejectsMissingEffectiveSnapshotIdWithoutLiveUObjectBreadcrumbsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotRejectsMissingEffectiveSnapshotId"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("SnapshotMissingEffectiveIdTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("SnapshotMissingEffectiveIdModule"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SnapshotMissingEffectiveIdEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 2;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("SnapshotMissingEffectiveIdContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotMissingEffectiveIdProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		213,
		TEXT("SnapshotMissingEffectiveIdStandalone"),
		FLayoutSolverExecutionSettings(),
		SnapshotContentSetPlacementPolicyId,
		SnapshotContentSetCandidateId,
		0,
		SnapshotContentSetSolveId);

	Request.EffectiveSnapshotId = NAME_None;
	Request.ProfileSnapshot.SourceProfile = nullptr;
	Request.ContentSetSnapshot.SourceContentSet = nullptr;
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSnapshotContractAssertions(Request);

	const FLayoutValidationAssertionRecord* SnapshotContractAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SnapshotContractInitialized");
	});
	if (!TestNotNull(TEXT("Mutated request still records the snapshot-contract assertion"), SnapshotContractAssertion))
	{
		return false;
	}

	TestEqual(TEXT("Snapshot-contract assertion keeps the snapshot-contract assertion kind"), SnapshotContractAssertion->AssertionKind, ELayoutValidationAssertionKind::SnapshotContractInitialized);
	TestFalse(TEXT("Snapshot-contract assertion fails once the effective snapshot id is cleared"), SnapshotContractAssertion->bPassed);
	TestTrue(TEXT("Snapshot-contract assertion reports the request region path"), SnapshotContractAssertion->FailureReason.Contains(TEXT("SnapshotMissingEffectiveIdStandalone")));
	TestTrue(TEXT("Snapshot-contract assertion reports the frozen profile snapshot id without a live profile UObject"), SnapshotContractAssertion->FailureReason.Contains(Request.ProfileSnapshot.SnapshotId.ToString()));
	TestTrue(TEXT("Snapshot-contract assertion reports the frozen content-set snapshot id without a live content-set UObject"), SnapshotContractAssertion->FailureReason.Contains(Request.ContentSetSnapshot.SnapshotId.ToString()));
	TestTrue(TEXT("Snapshot-contract assertion reports the frozen module-set snapshot id without a live module-set UObject"), SnapshotContractAssertion->FailureReason.Contains(Request.ModuleCatalog.SnapshotId.ToString()));
	TestTrue(TEXT("Snapshot-contract assertion references the frozen profile snapshot id"), SnapshotContractAssertion->RelatedIds.Contains(Request.ProfileSnapshot.SnapshotId));
	TestTrue(TEXT("Snapshot-contract assertion references the frozen content-set snapshot id"), SnapshotContractAssertion->RelatedIds.Contains(Request.ContentSetSnapshot.SnapshotId));
	TestTrue(TEXT("Snapshot-contract assertion references the frozen module-set snapshot id"), SnapshotContractAssertion->RelatedIds.Contains(Request.ModuleCatalog.SnapshotId));
	return true;
}

bool FLayoutSnapshotAdapterRejectsTemplatePlacementOffsetWithoutSharedCellSizeTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateSnapshotTestOuter(TEXT("LayoutSnapshotRejectsTemplatePlacementOffset"));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("SnapshotProfile_NoContentSet"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		11,
		TEXT("SnapshotNoSharedMetrics"),
		FLayoutSolverExecutionSettings(),
		NAME_None,
		NAME_None,
		5,
		NAME_None);

	const FLayoutValidationAssertionRecord* TemplatePlacementOffsetAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.TemplatePlacementOffsetContractValid");
	});
	if (!TestNotNull(TEXT("Profile-root request without shared cell metrics records a template placement offset request-contract assertion"), TemplatePlacementOffsetAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Template placement offset request-contract assertion uses the request-contract kind"), TemplatePlacementOffsetAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestFalse(TEXT("Template placement offset request-contract assertion fails when shared cell size is missing"), TemplatePlacementOffsetAssertion->bPassed);
	TestTrue(TEXT("Template placement offset request-contract assertion reports the request region path"), TemplatePlacementOffsetAssertion->FailureReason.Contains(TEXT("SnapshotNoSharedMetrics")));
	TestTrue(TEXT("Template placement offset request-contract assertion reports the request snapshot id"), TemplatePlacementOffsetAssertion->FailureReason.Contains(Request.EffectiveSnapshotId.ToString()));
	TestTrue(TEXT("Template placement offset request-contract assertion references the request snapshot id"), TemplatePlacementOffsetAssertion->RelatedIds.Contains(Request.EffectiveSnapshotId));

	return true;
}
