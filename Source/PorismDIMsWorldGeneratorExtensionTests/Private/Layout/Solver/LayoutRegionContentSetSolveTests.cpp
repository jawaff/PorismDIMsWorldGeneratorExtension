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

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateContentSetSolveTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	void AddOuterPerimeterClosure(ULayoutProfileAsset* Profile)
	{
		FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
		ClosureRequirement.ClosureId = TEXT("OuterPerimeter");
		ClosureRequirement.Zone = ELayoutPlacementZone::Perimeter;
		ClosureRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
		ClosureRequirement.BoundsPolicy.InsetCells = 0;
		ClosureRequirement.BoundsPolicy.MinLevel = 0;
		ClosureRequirement.BoundsPolicy.MaxLevel = 0;
		ClosureRequirement.MinThicknessCells = 1;
			}

	ULayoutCompositeModuleAsset* CreateContentSetSolveComposite(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<FLayoutCompositeModuleCell>& Cells)
	{
		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, FName(Name));
		Composite->Cells = Cells;
		return Composite;
	}

	FLayoutCompositeModuleCell MakeContentSetSolveCompositeCell(
		ULayoutModuleAsset* Module,
		const FIntVector LocalCell,
		const int32 RelativeYawRotationSteps = 0)
	{
		FLayoutCompositeModuleCell Cell;
		Cell.Module = Module;
		Cell.LocalCell = LocalCell;
		Cell.RelativeYawRotationSteps = RelativeYawRotationSteps;
		return Cell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetModuleEntryDrivesClosureProvidersTest,
	"PorismExtension.Layout.ContentSet.Solver.ModuleEntryDrivesClosureProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetCompositeModuleEntryBuildsUnifiedSnapshotTest,
	"PorismExtension.Layout.ContentSet.Solver.CompositeModuleEntryBuildsUnifiedSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetCompositeModuleEntrySnapshotUsesEstablishedSharedCellSizeValidationTest,
	"PorismExtension.Layout.ContentSet.Solver.CompositeModuleEntrySnapshotUsesEstablishedSharedCellSizeValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetLeafModuleEntrySnapshotIgnoresStaleCompatibilitySharedCellSizeTest,
	"PorismExtension.Layout.ContentSet.Solver.LeafModuleEntrySnapshotIgnoresStaleCompatibilitySharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetCompositeModuleEntryGuardlessSimpleCompositeSolveSucceedsTest,
	"PorismExtension.Layout.ContentSet.Solver.GuardlessSimpleCompositeSolveSucceeds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetCompositeRejectsSecondaryIntentMismatchTest,
	"PorismExtension.Layout.ContentSet.Solver.CompositeRejectsSecondaryIntentMismatch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetCompositeVerticalAscentIndexesOnlyRootAnchorIntentsTest,
	"PorismExtension.Layout.ContentSet.Solver.CompositeVerticalAscentIndexesOnlyRootAnchorIntents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionContentSetModuleEntryDrivesClosureProvidersTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetClosureSolve"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetClosureTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("ContentSetClosureBoundaryModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("OuterWall");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.Weight = 5;
	Entry.ModuleSettings.Module = Module;
	FLayoutClosureProviderIntent& ProviderIntent = Entry.ClosureProviderIntents.AddDefaulted_GetRef();
	ProviderIntent.ProviderIntentId = TEXT("OuterWallProvider");
	ProviderIntent.Zone = ELayoutPlacementZone::Perimeter;
	ProviderIntent.ClosureId = TEXT("OuterPerimeter");

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_ModuleClosure"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetClosureProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	AddOuterPerimeterClosure(Profile);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 41, TEXT("ContentSetClosure"));
	TestEqual(TEXT("Content-set snapshot keeps one entry"), Request.ContentSetSnapshot.Entries.Num(), 1);
	TestEqual(TEXT("Module-solve snapshot keeps one compiled module"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ContentSetSnapshot.Entries.Num() == 0 || Request.ModuleCatalog.Modules.Num() == 0)
	{
		return false;
	}

	TestEqual(TEXT("Compiled module snapshot keeps entry-level provider intent"), Request.ModuleCatalog.Modules[0].ClosureProviderIntents.Num(), 1);
	const FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(TEXT("Solve succeeds from the unified content-set path"), RegionResult.SolveResult.bSucceeded);
	TestEqual(TEXT("One closure summary is emitted"), RegionResult.SolveResult.ClosureCoverage.Num(), 1);
	if (!RegionResult.SolveResult.bSucceeded || RegionResult.SolveResult.ClosureCoverage.Num() == 0)
	{
		if (!RegionResult.SolveResult.FailureReason.IsEmpty())
		{
			AddError(RegionResult.SolveResult.FailureReason);
		}
		return false;
	}

	TestTrue(TEXT("Closure is satisfied"), RegionResult.SolveResult.ClosureCoverage[0].bSatisfied);
	TestTrue(TEXT("Covered segments cite the content-entry provider intent"), RegionResult.SolveResult.ClosureSegments.ContainsByPredicate([](const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		return Segment.bCovered && Segment.ProviderId.ToString().Contains(TEXT("OuterWallProvider"));
	}));
	return true;
}

bool FLayoutRegionContentSetCompositeModuleEntryBuildsUnifiedSnapshotTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetCompositeSnapshot"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetCompositeTemplate"), FIntVector(8, 8, 8));

	FLayoutInternalAccessLink RoomInternalLink;
	RoomInternalLink.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
	RoomInternalLink.ToTraversalChannel = LayoutGameplayTags::TraversalSecondary;
	RoomInternalLink.bBidirectional = true;

	ULayoutModuleAsset* RoomLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeRoomLeaf"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, MakeTags({LayoutGameplayTags::TraversalSecondary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		FGameplayTagContainer(),
		{RoomInternalLink});

	ULayoutCompositeModuleAsset* CompositeModule = CreateContentSetSolveComposite(
		Outer,
		TEXT("ContentSetCompositeSnapshotModule"),
		{
			MakeContentSetSolveCompositeCell(RoomLeaf, FIntVector(0, 0, 0)),
			MakeContentSetSolveCompositeCell(RoomLeaf, FIntVector(1, 0, 0))
		});

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeRoomPair");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeSnapshot"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetCompositeProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 77, TEXT("ContentSetComposite"));
	TestEqual(TEXT("Composite-backed content set still compiles one content-set entry"), Request.ContentSetSnapshot.Entries.Num(), 1);
	TestEqual(TEXT("Composite-backed content set compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	TestTrue(TEXT("Compiled module snapshot preserves the composite source"), Snapshot.SourceCompositeModule == CompositeModule);
	TestEqual(TEXT("Composite-backed content set preserves both occupied local cells"), Snapshot.OccupiedLocalCells.Num(), 2);
	TestEqual(TEXT("Composite-backed content set carries localized and glued traversal links"), Snapshot.DerivedInternalTraversalLinks.Num(), 3);
	for (const FLayoutLocalCellFaceRuleSnapshot& CellSnapshot : Snapshot.GeneratedLocalCellFaceRules)
	{
		TestEqual(TEXT("Composite local-cell descriptor preserves the leaf template path"), CellSnapshot.TemplatePath, RoomLeaf->Template.ToSoftObjectPath());
		TestEqual(TEXT("Composite local-cell descriptor preserves the authored relative yaw"), CellSnapshot.RelativeYawRotationSteps, 0);
	}
	return true;
}

bool FLayoutRegionContentSetCompositeModuleEntrySnapshotUsesEstablishedSharedCellSizeValidationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetCompositeSharedCellSizeSnapshot"));
	UChunkStructureTemplate* SmallTemplate = CreateTemplate(Outer, TEXT("ContentSetCompositeSharedCellSizeSmallTemplate"), FIntVector(8, 8, 8));
	UChunkStructureTemplate* LargeTemplate = CreateTemplate(Outer, TEXT("ContentSetCompositeSharedCellSizeLargeTemplate"), FIntVector(16, 16, 16));

	ULayoutModuleAsset* SmallLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeSharedCellSizeSmallLeaf"),
		SmallTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	ULayoutModuleAsset* LargeLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeSharedCellSizeLargeLeaf"),
		LargeTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	ULayoutCompositeModuleAsset* LargeComposite = CreateContentSetSolveComposite(
		Outer,
		TEXT("ContentSetCompositeSharedCellSizeLargeComposite"),
		{
			MakeContentSetSolveCompositeCell(LargeLeaf, FIntVector(0, 0, 0))
		});

	FLayoutRegionContentEntry SmallLeafEntry;
	SmallLeafEntry.EntryId = TEXT("SmallLeaf");
	SmallLeafEntry.ContentKind = ELayoutRegionContentKind::Module;
	SmallLeafEntry.ModuleSettings.Module = SmallLeaf;

	FLayoutRegionContentEntry LargeCompositeEntry;
	LargeCompositeEntry.EntryId = TEXT("LargeComposite");
	LargeCompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	LargeCompositeEntry.ModuleSettings.CompositeModule = LargeComposite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeSharedCellSizeSnapshot"),
		{SmallLeafEntry, LargeCompositeEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetCompositeSharedCellSizeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 91, TEXT("ContentSetCompositeSharedCellSize"));
	TestEqual(TEXT("Content-set snapshot still compiles both entries"), Request.ContentSetSnapshot.Entries.Num(), 2);
	TestEqual(TEXT("Module-set snapshot still compiles both entries"), Request.ModuleCatalog.Modules.Num(), 2);
	if (Request.ModuleCatalog.Modules.Num() != 2)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot* CompositeSnapshot = Request.ModuleCatalog.Modules.FindByPredicate([](const FLayoutModuleSolveSnapshot& Snapshot)
	{
		return Snapshot.SourceCompositeModule != nullptr;
	});
	if (!TestNotNull(TEXT("Composite-backed content entry still produces one composite module snapshot"), CompositeSnapshot))
	{
		return false;
	}

	TestFalse(TEXT("Composite-backed module snapshot validation fails when it violates the established shared cell size"), CompositeSnapshot->Validation.IsValid());
	TestTrue(TEXT("Composite-backed module snapshot reports the established shared cell size contract failure"), CompositeSnapshot->Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("violates the established shared cell size contract"));
	}));
	return true;
}

bool FLayoutRegionContentSetLeafModuleEntrySnapshotIgnoresStaleCompatibilitySharedCellSizeTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetLeafSharedCellSizeSnapshot"));
	UChunkStructureTemplate* LargeTemplate = CreateTemplate(Outer, TEXT("ContentSetLeafSharedCellSizeLargeTemplate"), FIntVector(16, 16, 16));

	ULayoutModuleAsset* LargeLeaf = CreateModule(
		Outer,
		TEXT("ContentSetLeafSharedCellSizeLargeLeaf"),
		LargeTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutRegionContentEntry LargeLeafEntry;
	LargeLeafEntry.EntryId = TEXT("LargeLeaf");
	LargeLeafEntry.ContentKind = ELayoutRegionContentKind::Module;
	LargeLeafEntry.ModuleSettings.Module = LargeLeaf;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_LeafSharedCellSizeSnapshot"),
		{LargeLeafEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetLeafSharedCellSizeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 97, TEXT("ContentSetLeafSharedCellSize"));
	TestEqual(TEXT("Content-set snapshot prefers the derived module shared cell size over the stale compatibility field"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, FIntVector(16, 16, 16));
	TestEqual(TEXT("Leaf-backed content set still compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	TestEqual(TEXT("Leaf-backed content entry snapshot keeps the derived leaf shared cell size"), Snapshot.CellSizeInBlocks, FIntVector(16, 16, 16));
	TestTrue(TEXT("Leaf-backed module snapshot validation stays passing when the stale compatibility field is ignored"), Snapshot.Validation.IsValid());
	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = Snapshot.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("ModuleSnapshot.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Leaf-backed module snapshot records a shared-cell-size snapshot assertion"), SharedCellSizeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Leaf-backed module snapshot shared-cell-size assertion uses the snapshot shared-cell-size kind"), SharedCellSizeAssertion->AssertionKind, ELayoutValidationAssertionKind::SnapshotSharedCellSizeContractValid);
	TestTrue(TEXT("Leaf-backed module snapshot shared-cell-size assertion stays passing when the stale compatibility field is ignored"), SharedCellSizeAssertion->bPassed);
	return true;
}

bool FLayoutRegionContentSetCompositeModuleEntryGuardlessSimpleCompositeSolveSucceedsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetCompositeSolveProbe"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetCompositeSolveTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* RoomLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeSolveRoomLeaf"),
		Template,
		{
			ELayoutCellIntent::Boundary,
			ELayoutCellIntent::Interior
		},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});
	// Mark faces as boundary-facing so composite validation recognizes the
	// boundary-capable leaf contributes real exterior boundary faces.
	RoomLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	RoomLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	RoomLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	RoomLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	RoomLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	RoomLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutCompositeModuleAsset* CompositeModule = CreateContentSetSolveComposite(
		Outer,
		TEXT("ContentSetCompositeSolveModule"),
		{
			MakeContentSetSolveCompositeCell(RoomLeaf, FIntVector(0, 0, 0)),
			MakeContentSetSolveCompositeCell(RoomLeaf, FIntVector(1, 0, 0))
		});

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeRoomPair");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeSolve"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetCompositeSolveProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 91, TEXT("ContentSetCompositeSolve"));
	TestTrue(
		TEXT("Public request now records a passing live composite bundle contract assertion"),
		Request.ValidationAssertions.ContainsByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == FLayoutId(TEXT("RegionRequest.LiveCompositeBundlePlacementSupported"))
				&& Assertion.bPassed;
		}));

	const FLayoutRegionSolveResult PublicResult = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(TEXT("Public simple same-level composite solve now succeeds through the live root solver"), PublicResult.SolveResult.bSucceeded);
	if (!PublicResult.SolveResult.bSucceeded)
	{
		AddError(PublicResult.SolveResult.FailureReason);
		return false;
	}

	TestEqual(TEXT("Public simple composite solve publishes one placed bundle"), PublicResult.SolveResult.Placements.Num(), 1);

	FLayoutRegionSolveRequest ShiftedSameStageRequest = Request;
	ShiftedSameStageRequest.FootprintSize = FIntPoint(3, 1);
	ShiftedSameStageRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	ShiftedSameStageRequest.ProfileSnapshot.LevelCount = 2;
	ShiftedSameStageRequest.bHasSelectedModePlan = true;
	ShiftedSameStageRequest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ShiftedSameStageRequest.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	ShiftedSameStageRequest.PrecomputedPlannedCells.Reset();
	ShiftedSameStageRequest.PrecomputedActiveCells.Reset();
	ShiftedSameStageRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 8;
	ShiftedSameStageRequest.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta = 1;
	for (int32 X = 0; X < 3; ++X)
	{
		FLayoutSteppedTerrainSupportSample& Support =
			ShiftedSameStageRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		Support.LocalCell = FIntVector(X, 0, 0);
		Support.SnappedSupportFloorZ = X == 0 ? 8 : 16;
		Support.SnappedSupportCeilingZ = Support.SnappedSupportFloorZ + 8;
		Support.SupportSurfaceZ = Support.SnappedSupportFloorZ;
	}
	ShiftedSameStageRequest.PrecomputedSteppedTerrainSupportMap = ShiftedSameStageRequest.SteppedTerrainSupportMap;
	for (int32 X = 1; X <= 2; ++X)
	{
		FLayoutPlannedCell& FinalizedCell = ShiftedSameStageRequest.PrecomputedPlannedCells.AddDefaulted_GetRef();
		FinalizedCell.Cell = FIntVector(X, 0, 1);
		FinalizedCell.ModuleLevelIndex = 0;
		FinalizedCell.Intent = ELayoutCellIntent::Boundary;
		FLayoutContractActiveCellRecord& ActiveCell = ShiftedSameStageRequest.PrecomputedActiveCells.AddDefaulted_GetRef();
		ActiveCell.Cell = FinalizedCell.Cell;
		FLayoutContractActiveCellRecord& FrozenActiveCell =
			ShiftedSameStageRequest.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef();
		FrozenActiveCell.Cell = FinalizedCell.Cell;
		FLayoutTerrainCellContractRecord& FrozenCellContract =
			ShiftedSameStageRequest.PrecomputedFrozenTerrainContract.CellContracts.AddDefaulted_GetRef();
		FrozenCellContract.Cell = FinalizedCell.Cell;
		FrozenCellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
	}
	for (int32 X = 0; X < 3; ++X)
	{
		FLayoutFrozenTerrainStageCellRecord& StageRecord =
			ShiftedSameStageRequest.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		StageRecord.FootprintCellXY = FIntPoint(X, 0);
		StageRecord.TerrainStageIndex = X == 0 ? 0 : 1;
		StageRecord.VerticalShiftBlocks = X == 0 ? 0 : 8;
		StageRecord.ResolvedStageBaseBlockWorldZ = X == 0 ? 8 : 16;
	}
	ShiftedSameStageRequest.bHasFinalizedSteppedTerrainIntents = true;

	const FLayoutRegionSolveResult ShiftedSameStageResult = FLayoutProfileSolver::SolveRegion(ShiftedSameStageRequest);
	TestTrue(TEXT("Public solver accepts a composite in frozen raised same-stage cells"), ShiftedSameStageResult.SolveResult.bSucceeded);
	if (!ShiftedSameStageResult.SolveResult.bSucceeded)
	{
		AddError(ShiftedSameStageResult.SolveResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Frozen raised same-stage solve exports one composite root"), ShiftedSameStageResult.SolveResult.Placements.Num(), 1);
	TestTrue(TEXT("Frozen raised same-stage solve preserves authored level while using shifted physical Z"),
		ShiftedSameStageResult.SolveResult.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell.Z == 1 && Cell.ModuleLevelIndex == 0;
		}));

	ULayoutModuleAsset* VerticalLeaf = CreateModule(
		Outer,
		TEXT("ContentSetVerticalRaisedSameStageLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});
	for (FLayoutFaceRule* FaceRule : {&VerticalLeaf->FaceRules.PosX, &VerticalLeaf->FaceRules.NegX, &VerticalLeaf->FaceRules.PosY, &VerticalLeaf->FaceRules.NegY, &VerticalLeaf->FaceRules.PosZ, &VerticalLeaf->FaceRules.NegZ})
	{
		FaceRule->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	}
	ULayoutCompositeModuleAsset* VerticalComposite = CreateContentSetSolveComposite(
		Outer,
		TEXT("ContentSetVerticalRaisedSameStageComposite"),
		{
			MakeContentSetSolveCompositeCell(VerticalLeaf, FIntVector(0, 0, 0)),
			MakeContentSetSolveCompositeCell(VerticalLeaf, FIntVector(0, 0, 1))
		});
	FLayoutRegionContentEntry VerticalEntry;
	VerticalEntry.EntryId = TEXT("VerticalRaisedSameStageComposite");
	VerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	VerticalEntry.ModuleSettings.CompositeModule = VerticalComposite;
	ULayoutRegionContentSetAsset* VerticalContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetVerticalRaisedSameStage"),
		{VerticalEntry});
	ULayoutProfileAsset* VerticalProfile = CreateProfile(
		Outer,
		TEXT("ContentSetVerticalRaisedSameStageProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		3,
		0,
		false);
	VerticalProfile->ContentSet = VerticalContentSet;
	FLayoutRegionSolveRequest VerticalRequest =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(VerticalContentSet, VerticalProfile, 92, TEXT("VerticalRaisedSameStage"));
	VerticalRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	VerticalRequest.bHasSelectedModePlan = true;
	VerticalRequest.SelectedModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	VerticalRequest.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	VerticalRequest.PrecomputedPlannedCells.Reset();
	VerticalRequest.PrecomputedActiveCells.Reset();
	VerticalRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 8;
	VerticalRequest.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta = 1;
	for (int32 X = 0; X < 2; ++X)
	{
		FLayoutSteppedTerrainSupportSample& Support =
			VerticalRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		Support.LocalCell = FIntVector(X, 0, 0);
		Support.SnappedSupportFloorZ = X == 0 ? 8 : 16;
		Support.SnappedSupportCeilingZ = Support.SnappedSupportFloorZ + 8;
		Support.SupportSurfaceZ = Support.SnappedSupportFloorZ;
	}
	VerticalRequest.PrecomputedSteppedTerrainSupportMap = VerticalRequest.SteppedTerrainSupportMap;
	for (int32 LocalZ = 0; LocalZ < 2; ++LocalZ)
	{
		FLayoutPlannedCell& FinalizedCell = VerticalRequest.PrecomputedPlannedCells.AddDefaulted_GetRef();
		FinalizedCell.Cell = FIntVector(1, 0, LocalZ + 1);
		FinalizedCell.ModuleLevelIndex = LocalZ;
		FinalizedCell.Intent = ELayoutCellIntent::Boundary;
		FLayoutContractActiveCellRecord& ActiveCell = VerticalRequest.PrecomputedActiveCells.AddDefaulted_GetRef();
		ActiveCell.Cell = FinalizedCell.Cell;
		FLayoutContractActiveCellRecord& FrozenActiveCell =
			VerticalRequest.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef();
		FrozenActiveCell.Cell = FinalizedCell.Cell;
		FLayoutTerrainCellContractRecord& FrozenCellContract =
			VerticalRequest.PrecomputedFrozenTerrainContract.CellContracts.AddDefaulted_GetRef();
		FrozenCellContract.Cell = FinalizedCell.Cell;
		FrozenCellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
	}
	for (int32 X = 0; X < 2; ++X)
	{
		FLayoutFrozenTerrainStageCellRecord& StageRecord =
			VerticalRequest.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		StageRecord.FootprintCellXY = FIntPoint(X, 0);
		StageRecord.TerrainStageIndex = X == 0 ? 0 : 1;
		StageRecord.VerticalShiftBlocks = X == 0 ? 0 : 8;
		StageRecord.ResolvedStageBaseBlockWorldZ = X == 0 ? 8 : 16;
	}
	VerticalRequest.bHasFinalizedSteppedTerrainIntents = true;
	const FLayoutRegionSolveResult VerticalResult = FLayoutProfileSolver::SolveRegion(VerticalRequest);
	TestTrue(TEXT("Public solver accepts a multi-level composite wholly inside one frozen raised terrain stage"), VerticalResult.SolveResult.bSucceeded);
	if (!VerticalResult.SolveResult.bSucceeded)
	{
		AddError(VerticalResult.SolveResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Raised multi-level solve exports one vertical composite root"), VerticalResult.SolveResult.Placements.Num(), 1);
	TestTrue(TEXT("Raised multi-level solve keeps both physical shifted cells and authored levels"),
		VerticalResult.SolveResult.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FIntVector(1, 0, 1) && Cell.ModuleLevelIndex == 0;
		}) && VerticalResult.SolveResult.PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell == FIntVector(1, 0, 2) && Cell.ModuleLevelIndex == 1;
		}));
	return PublicResult.SolveResult.bSucceeded;
}

bool FLayoutRegionContentSetCompositeRejectsSecondaryIntentMismatchTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetCompositeSecondaryIntent"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetCompositeSecondaryIntentTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* BoundaryLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeSecondaryIntentBoundaryLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	for (FLayoutFaceRule* FaceRule : {&BoundaryLeaf->FaceRules.PosX, &BoundaryLeaf->FaceRules.NegX, &BoundaryLeaf->FaceRules.PosY, &BoundaryLeaf->FaceRules.NegY, &BoundaryLeaf->FaceRules.PosZ, &BoundaryLeaf->FaceRules.NegZ})
	{
		FaceRule->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	}
	ULayoutModuleAsset* InteriorLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeSecondaryIntentInteriorLeaf"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutCompositeModuleAsset* CompositeModule = CreateContentSetSolveComposite(
		Outer,
		TEXT("ContentSetCompositeSecondaryIntentModule"),
		{
			MakeContentSetSolveCompositeCell(BoundaryLeaf, FIntVector(0, 0, 0)),
			MakeContentSetSolveCompositeCell(InteriorLeaf, FIntVector(1, 0, 0))
		});
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeSecondaryIntent");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeSecondaryIntent"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetCompositeSecondaryIntentProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutRegionSolveResult Result = FLayoutProfileSolver::SolveRegion(
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			311,
			TEXT("ContentSetCompositeSecondaryIntent")));
	TestFalse(TEXT("Composite root cannot use an interior-only shadow leaf for a finalized boundary cell"), Result.SolveResult.bSucceeded);
	TestTrue(TEXT("Composite rejection identifies the secondary local-cell compiled-intent mismatch"), Result.SolveResult.FailureReason.Contains(TEXT("does not support compiled intent mask")));
	return !Result.SolveResult.bSucceeded;
}

bool FLayoutRegionContentSetCompositeVerticalAscentIndexesOnlyRootAnchorIntentsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateContentSetSolveTestOuter(TEXT("LayoutRegionContentSetCompositeVerticalAscentDomains"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetCompositeVerticalAscentTemplate"), FIntVector(8, 8, 8));

	ULayoutModuleAsset* StairsLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeVerticalAscentStairsLeaf"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}))
		});

	ULayoutModuleAsset* StairHoleLeaf = CreateModule(
		Outer,
		TEXT("ContentSetCompositeVerticalAscentHoleLeaf"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutCompositeModuleAsset* CompositeModule = CreateContentSetSolveComposite(
		Outer,
		TEXT("ContentSetCompositeVerticalAscentModule"),
		{
			MakeContentSetSolveCompositeCell(StairsLeaf, FIntVector(0, 0, 0)),
			MakeContentSetSolveCompositeCell(StairHoleLeaf, FIntVector(0, 0, 1)),
			MakeContentSetSolveCompositeCell(StairHoleLeaf, FIntVector(0, 0, 2))
		});

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeVerticalAscent");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeVerticalAscent"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ContentSetCompositeVerticalAscentProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		3,
		0,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, 109, TEXT("ContentSetCompositeVerticalAscent"));
	TestEqual(TEXT("Composite vertical-ascent request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);
	if (Request.ModuleCatalog.Modules.Num() != 1)
	{
		return false;
	}

	const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
	TestTrue(TEXT("Composite vertical-ascent snapshot still reports the union of covered intents"), Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess) && Snapshot.SupportsIntent(ELayoutCellIntent::Interior));
	TestTrue(TEXT("Composite vertical-ascent snapshot root anchor keeps the lower vertical-access intent"), Snapshot.SupportsRootIntent(ELayoutCellIntent::VerticalAccess));
	TestFalse(TEXT("Composite vertical-ascent snapshot root anchor does not claim the upper interior shadow intent"), Snapshot.SupportsRootIntent(ELayoutCellIntent::Interior));

	/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	Request.FootprintSize = FIntPoint(1, 1);
	Request.PlannedCells.Reset();
	FLayoutPlannedCell& LowerCell = Request.PlannedCells.AddDefaulted_GetRef();
	LowerCell.Cell = FIntVector(0, 0, 0);
	LowerCell.Intent = ELayoutCellIntent::VerticalAccess;
	FLayoutPlannedCell& UpperCell = Request.PlannedCells.AddDefaulted_GetRef();
	UpperCell.Cell = FIntVector(0, 0, 1);
	UpperCell.Intent = ELayoutCellIntent::Interior;
	FLayoutPlannedCell& TopCell = Request.PlannedCells.AddDefaulted_GetRef();
	TopCell.Cell = FIntVector(0, 0, 2);
	TopCell.Intent = ELayoutCellIntent::Interior;

	const FLayoutIndexedDomainSnapshot IndexedSnapshot = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	TestTrue(TEXT("Indexed domain snapshot builds successfully for the vertical-ascent composite probe"), IndexedSnapshot.bSucceeded);
	if (!IndexedSnapshot.bSucceeded)
	{
		AddError(IndexedSnapshot.FailureReason);
		return false;
	}

	const FLayoutIndexedCellDomain* LowerDomain = IndexedSnapshot.CellDomains.FindByPredicate([](const FLayoutIndexedCellDomain& Domain)
	{
		return Domain.Cell == FIntVector(0, 0, 0);
	});
	const FLayoutIndexedCellDomain* UpperDomain = IndexedSnapshot.CellDomains.FindByPredicate([](const FLayoutIndexedCellDomain& Domain)
	{
		return Domain.Cell == FIntVector(0, 0, 1);
	});
	if (!TestNotNull(TEXT("Lower vertical-access domain exists"), LowerDomain)
		|| !TestNotNull(TEXT("Upper interior domain exists"), UpperDomain))
	{
		return false;
	}

	const auto DomainContainsCompositeCandidate = [&IndexedSnapshot, CompositeModule](const FLayoutIndexedCellDomain& Domain)
	{
		return Domain.OrderedCandidateIndices.ContainsByPredicate([&IndexedSnapshot, CompositeModule](const int32 CandidateIndex)
		{
			return IndexedSnapshot.Candidates.IsValidIndex(CandidateIndex)
				&& IndexedSnapshot.Candidates[CandidateIndex].ModuleDebugName == CompositeModule->GetFName();
		});
	};

	TestTrue(TEXT("Lower vertical-access domain keeps the composite root candidate"), DomainContainsCompositeCandidate(*LowerDomain));
	TestFalse(TEXT("Upper interior domain no longer indexes the composite as a root candidate"), DomainContainsCompositeCandidate(*UpperDomain));

	const FLayoutRegionSolveResult SolveResult = FLayoutProfileSolver::SolveRegion(Request);
	TestTrue(TEXT("Unified three-level solve accepts one atomic vertical composite root"), SolveResult.SolveResult.bSucceeded);
	if (!SolveResult.SolveResult.bSucceeded)
	{
		AddError(SolveResult.SolveResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Unified three-level solve exports one composite root placement"), SolveResult.SolveResult.Placements.Num(), 1);
	TestEqual(TEXT("Unified three-level solve preserves every composite occupied cell"), SolveResult.SolveResult.Placements[0].OccupiedLocalCells.Num(), 3);
	return true;
}
