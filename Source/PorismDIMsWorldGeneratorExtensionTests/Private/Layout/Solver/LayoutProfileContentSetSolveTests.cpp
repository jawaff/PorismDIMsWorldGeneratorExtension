// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateProfileContentSetSolveTestOuter(const TCHAR* BaseName)
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileUsesUnifiedContentSetSolvePathTest,
	"PorismExtension.Layout.Profile.Solver.UsesUnifiedContentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileUsesUnifiedContentSetSolvePathTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateProfileContentSetSolveTestOuter(TEXT("LayoutProfileUnifiedContentSetSolve"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ProfileContentSetClosureTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("ProfileContentSetBoundaryModule"),
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
	Entry.Weight = 3;
	Entry.ModuleSettings.Module = Module;
	FLayoutClosureProviderIntent& ProviderIntent = Entry.ClosureProviderIntents.AddDefaulted_GetRef();
	ProviderIntent.ProviderIntentId = TEXT("OuterWallProvider");
	ProviderIntent.Zone = ELayoutPlacementZone::Perimeter;
	ProviderIntent.ClosureId = TEXT("OuterPerimeter");

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ProfileOwnedContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ProfileUsesUnifiedContentSet"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	AddOuterPerimeterClosure(Profile);

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 99, TEXT("ProfileContentSet"));
	TestEqual(TEXT("Profile-owned request compiles one content entry"), Request.ContentSetSnapshot.Entries.Num(), 1);
	TestEqual(TEXT("Profile-owned request compiles one module snapshot"), Request.ModuleCatalog.Modules.Num(), 1);

	const FLayoutSolveResult SolveResult = FLayoutProfileSolver::Solve(Profile, 99);
	TestTrue(TEXT("Profile-owned content-set solve succeeds"), SolveResult.bSucceeded);
	TestEqual(TEXT("Profile-owned content-set solve emits one closure summary"), SolveResult.ClosureCoverage.Num(), 1);
	if (!SolveResult.bSucceeded || SolveResult.ClosureCoverage.Num() == 0)
	{
		if (!SolveResult.FailureReason.IsEmpty())
		{
			AddError(SolveResult.FailureReason);
		}
		return false;
	}

	TestTrue(TEXT("Profile-owned content-set solve satisfies closure coverage"), SolveResult.ClosureCoverage[0].bSatisfied);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileOneCellModuleEntrySolveTest,
	"PorismExtension.Layout.Profile.Solver.SolvesOneCellModuleEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies public profile solve carries one compatible module entry from immutable content snapshot to solved artifact. */
bool FLayoutProfileOneCellModuleEntrySolveTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateProfileContentSetSolveTestOuter(TEXT("LayoutProfileOneCellModuleEntry"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("OneCellModuleTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("OneCellBoundaryModule"),
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
	Entry.EntryId = TEXT("OneCellModuleEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("OneCellContentSet"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("OneCellProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	const FLayoutSolveResult SolveResult = FLayoutProfileSolver::Solve(Profile, 17);
	if (!SolveResult.bSucceeded)
	{
		AddError(FString::Printf(TEXT("One-cell module-entry solve failed: %s"), *SolveResult.FailureReason));
		return false;
	}

	TestEqual(TEXT("One-cell solve plans one cell"), SolveResult.PlannedCells.Num(), 1);
	TestEqual(TEXT("One-cell solve emits one placement"), SolveResult.Placements.Num(), 1);
	if (SolveResult.Placements.Num() != 1)
	{
		return false;
	}

	TestEqual(TEXT("Solved placement retains module-entry identity"), SolveResult.Placements[0].SourceContentEntryId, Entry.EntryId);
	TestEqual(TEXT("Solved placement retains module snapshot identity"), SolveResult.Placements[0].ModuleSnapshotId.IsNone(), false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileRejectsMismatchedExplicitContentSetTest,
	"PorismExtension.Layout.Profile.Solver.RejectsMismatchedExplicitContentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies explicit request builders cannot override profile-owned structural content. */
bool FLayoutProfileRejectsMismatchedExplicitContentSetTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateProfileContentSetSolveTestOuter(TEXT("LayoutProfileMismatchedContentSet"));
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("MismatchedContentSetProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("ProfileOwnedContentSet"), {});
	ULayoutRegionContentSetAsset* MismatchedContentSet = CreateRegionContentSet(
		Outer,
		TEXT("MismatchedContentSet"),
		{});

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		MismatchedContentSet,
		Profile,
		29,
		TEXT("MismatchedContentSet"));
	const FLayoutRegionSolveResult Result = FLayoutProfileSolver::SolveRegion(Request);
	TestFalse(TEXT("Explicit mismatched content-set request fails before leaf solving"), Result.SolveResult.bSucceeded);
	TestTrue(
		TEXT("Explicit mismatched content-set failure names profile-owned authority"),
		Result.SolveResult.FailureReason.Contains(TEXT("must match profile")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileChildTemplateSnapshotTest,
	"PorismExtension.Layout.Profile.Solver.BuildsStableChildTemplateSnapshotAndRejectsCycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Verifies child templates remain snapshot-only and recursive profile cycles fail closed before execution. */
bool FLayoutProfileChildTemplateSnapshotTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateProfileContentSetSolveTestOuter(TEXT("LayoutProfileChildTemplateSnapshot"));
	ULayoutProfileAsset* ChildProfile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ChildTemplateProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("ChildTemplateEntry");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ULayoutProfileAsset* ParentProfile = CreateProfile(
		Outer,
		TEXT("ParentTemplateProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ParentProfile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ParentTemplateContentSet"),
		{ChildEntry});

	const FLayoutRegionContentSetSolveSnapshot FirstSnapshot =
		FLayoutProfileSolver::BuildContentSetSnapshot(ParentProfile->ContentSet);
	const FLayoutRegionContentSetSolveSnapshot SecondSnapshot =
		FLayoutProfileSolver::BuildContentSetSnapshot(ParentProfile->ContentSet);
	if (!TestTrue(
		TEXT("Parent child entry compiles a snapshot-only child template"),
		FirstSnapshot.Entries[0].CompiledChildRequestTemplate.IsValid()))
	{
		return false;
	}
	TestEqual(
		TEXT("Repeated child-template snapshots preserve child template identity"),
		FirstSnapshot.Entries[0].CompiledChildRequestTemplate->EffectiveSnapshotId,
		SecondSnapshot.Entries[0].CompiledChildRequestTemplate->EffectiveSnapshotId);
	TestEqual<FLayoutId>(
		TEXT("Child template preserves child content-set snapshot identity"),
		FirstSnapshot.Entries[0].ChildContentSetSnapshotId,
		ChildProfile->ContentSet->GetFName());

	ULayoutProfileAsset* CycleParent = CreateProfile(Outer, TEXT("CycleParent"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* CycleChild = CreateProfile(Outer, TEXT("CycleChild"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	FLayoutRegionContentEntry ParentToChild;
	ParentToChild.EntryId = TEXT("ParentToChild");
	ParentToChild.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ParentToChild.ChildRegionSettings.RegionProfile = CycleChild;
	FLayoutRegionContentEntry ChildToParent;
	ChildToParent.EntryId = TEXT("ChildToParent");
	ChildToParent.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildToParent.ChildRegionSettings.RegionProfile = CycleParent;
	CycleParent->ContentSet = CreateRegionContentSet(Outer, TEXT("CycleParentContent"), {ParentToChild});
	CycleChild->ContentSet = CreateRegionContentSet(Outer, TEXT("CycleChildContent"), {ChildToParent});

	const FLayoutRegionContentSetSolveSnapshot CycleSnapshot =
		FLayoutProfileSolver::BuildContentSetSnapshot(CycleParent->ContentSet);
	TestFalse(TEXT("Recursive child profile cycle fails snapshot construction before execution"), CycleSnapshot.Validation.IsValid());
	TestTrue(TEXT("Recursive child profile cycle emits a failed child-template assertion"), CycleSnapshot.ValidationAssertions.ContainsByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("ContentSetSnapshot.ChildRequestTemplatePresent") && !Assertion.bPassed;
	}));
	return true;
}
