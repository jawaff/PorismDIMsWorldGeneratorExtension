// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
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

	UObject* CreateResidualPlacementTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	ULayoutModuleAsset* CreateBoundaryOnlyModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Boundary},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}

	ULayoutModuleAsset* CreateInteriorScatterModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	}

	ULayoutModuleAsset* CreateEntryScatterModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Entry},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				FGameplayTagContainer(),
				FGameplayTagContainer()));
	}

	ULayoutModuleAsset* CreateDualContextEntryModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenAndEntryTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer OpenSolidAndEntryTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid, LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
		const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Interior},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), OpenAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	FInstancedStruct MakeSparseRule(
		const FName RuleId,
		ULayoutRegionContentSetAsset* ContentSet,
		const int32 Count,
		const ELayoutPlacementZone PlacementZone)
	{
		FLayoutSparseExactPlacementRule Rule;
		Rule.RuleId = RuleId;
		Rule.CandidateSource = ELayoutSparseCandidateSource::ExistingResiduals;
		Rule.ContentSet = ContentSet;
		Rule.Count = Count;
		Rule.PlacementZone = PlacementZone;
		return FInstancedStruct::Make(Rule);
	}

	FLayoutRegionSolveRequest BuildSingleCellStandaloneRequest(
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

	FLayoutRegionSolveRequest BuildSingleCellRequest(
		ULayoutRegionContentSetAsset* ContentSet,
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const FIntVector& Cell,
		const ELayoutCellIntent Intent)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FIntPoint(FMath::Max(1, Cell.X + 1), FMath::Max(1, Cell.Y + 1));
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = Intent;
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResidualPlacementUsesDroppedChildResidualCellsTest,
	"PorismExtension.Layout.Solver.ResidualPlacement.UsesDroppedChildResidualCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutResidualPlacementSkipsCellsAlreadyOccupiedByBundleMetadataTest,
	"PorismExtension.Layout.Solver.ResidualPlacement.SkipsCellsAlreadyOccupiedByBundleMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutResidualPlacementUsesDroppedChildResidualCellsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateResidualPlacementTestOuter(TEXT("LayoutResidualPlacementDroppedChild"));
	ULayoutProfileAsset* ChildProfile = CreateProfile(Outer, TEXT("ResidualChildProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, false);
	ULayoutModuleAsset* ParentModule = CreateBoundaryOnlyModule(Outer, TEXT("ResidualParent"));
	ULayoutModuleAsset* FailingChildModule = CreateDualContextEntryModule(Outer, TEXT("ResidualChildFailure"));
	ULayoutModuleAsset* EntryScatterModule = CreateEntryScatterModule(Outer, TEXT("ResidualEntryScatter"));

	FLayoutRegionContentEntry ParentEntry;
	ParentEntry.EntryId = TEXT("ParentBoundary");
	ParentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ParentEntry.ModuleSettings.Module = ParentModule;

	FLayoutRegionContentEntry OptionalChildEntry;
	OptionalChildEntry.EntryId = TEXT("OptionalAnnex");
	OptionalChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	OptionalChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	OptionalChildEntry.ChildRegionSettings.bOptional = true;

	ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ResidualParentContentSet"),
		{ParentEntry, OptionalChildEntry});

	FLayoutRegionContentEntry ChildFailureEntry;
	ChildFailureEntry.EntryId = TEXT("ChildFailureModule");
	ChildFailureEntry.ContentKind = ELayoutRegionContentKind::Module;
	ChildFailureEntry.ModuleSettings.Module = FailingChildModule;
	ChildFailureEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	ChildFailureEntry.ModuleSettings.SpecificLevel = 1;
	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(Outer, TEXT("ResidualChildContentSet"), {ChildFailureEntry});
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry SparseEntry;
	SparseEntry.EntryId = TEXT("ResidualDroppedEntryScatter");
	SparseEntry.ContentKind = ELayoutRegionContentKind::Module;
	SparseEntry.ModuleSettings.Module = EntryScatterModule;
	ULayoutRegionContentSetAsset* SparseContentSet = CreateRegionContentSet(Outer, TEXT("ResidualScheduleSparseContentSet"), {SparseEntry});

	ULayoutProfileAsset* ParentProfile = CreateProfile(Outer, TEXT("ResidualParentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ParentProfile->ContentSet = ParentContentSet;
	ParentProfile->SparsePlacementRules.Add(MakeSparseRule(TEXT("DroppedChildReuse"), SparseContentSet, 1, ELayoutPlacementZone::Any));

	FLayoutRegionSolveRequest ParentRequest = BuildSingleCellRequest(
		ParentContentSet,
		ParentProfile,
		901,
		TEXT("Parent"),
		FIntVector(0, 0, 0),
		ELayoutCellIntent::Boundary);
	FLayoutRegionSolveRequest ChildRequest = BuildSingleCellRequest(
		ChildContentSet,
		ChildProfile,
		902,
		TEXT("OptionalChild"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Entry);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests = {ParentRequest, ChildRequest};
	ScheduleRequest.Dependencies.Add({TEXT("Parent"), TEXT("OptionalChild")});

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	TestTrue(TEXT("Schedule succeeds after dropping the child and reusing its residual cell"), ScheduleResult.bSucceeded);
	if (!ScheduleResult.bSucceeded)
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	TestEqual(TEXT("Dropped-child record remains visible"), ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num(), 1);
	TestEqual(TEXT("Dropped-child residual cell is consumed by sparse placement"), ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.Num(), 0);
	TestEqual(TEXT("Sparse pass records one commitment"), ScheduleResult.MergedSolveResult.SparsePlacementCommitments.Num(), 1);
	TestEqual(TEXT("Merged placements include the parent module and one sparse placement"), ScheduleResult.MergedSolveResult.Placements.Num(), 2);
	if (ScheduleResult.MergedSolveResult.SparsePlacementCommitments.Num() != 1)
	{
		return false;
	}

	const FLayoutSparsePlacementCommitment& Commitment = ScheduleResult.MergedSolveResult.SparsePlacementCommitments[0];
	TestEqual(TEXT("Sparse placement uses the parent's rule"), Commitment.RuleId, FName(TEXT("DroppedChildReuse")));
	TestEqual(TEXT("Sparse placement keeps the parent's region path"), Commitment.SourceRegionDebugPath, FString(TEXT("Parent")));
	TestEqual(TEXT("Sparse placement reuses the dropped child cell"), Commitment.Cell, FIntVector(1, 0, 0));
	TestEqual(TEXT("Sparse placement uses the sparse entry id"), Commitment.SourceContentEntryId, FName(TEXT("ResidualDroppedEntryScatter")));
	return true;
}

bool FLayoutResidualPlacementSkipsCellsAlreadyOccupiedByBundleMetadataTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateResidualPlacementTestOuter(TEXT("LayoutResidualPlacementBundleOccupied"));
	ULayoutModuleAsset* ScatterModule = CreateInteriorScatterModule(Outer, TEXT("BundleOccupiedScatter"));
	ULayoutModuleAsset* AnchorModule = CreateBoundaryOnlyModule(Outer, TEXT("BundleOccupiedAnchor"));

	FLayoutRegionContentEntry SparseEntry;
	SparseEntry.EntryId = TEXT("ResidualScatter");
	SparseEntry.ContentKind = ELayoutRegionContentKind::Module;
	SparseEntry.ModuleSettings.Module = ScatterModule;
	ULayoutRegionContentSetAsset* SparseContentSet = CreateRegionContentSet(
		Outer,
		TEXT("BundleOccupiedSparseContentSet"),
		{SparseEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("BundleOccupiedProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);
	Profile->SparsePlacementRules.Add(MakeSparseRule(TEXT("InteriorScatter"), SparseContentSet, 1, ELayoutPlacementZone::Any));

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.Seed = 913;
	SolveResult.FootprintSize = FIntPoint(1, 1);

	FLayoutPlacedModule& ExistingPlacement = SolveResult.Placements.AddDefaulted_GetRef();
	ExistingPlacement.Cell = FIntVector::ZeroValue;
	ExistingPlacement.Intent = ELayoutCellIntent::Boundary;
	ExistingPlacement.Module = AnchorModule;
	ExistingPlacement.YawRotationSteps = 0;
	ExistingPlacement.BundleBoundsCells = FIntVector(1, 1, 2);
	ExistingPlacement.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 0, 1)};

	FLayoutResidualCellRecord& ResidualCell = SolveResult.ResidualUnoccupiedCells.AddDefaulted_GetRef();
	ResidualCell.Cell = FIntVector(0, 0, 1);
	ResidualCell.Intent = ELayoutCellIntent::Interior;
	ResidualCell.Source = ELayoutResidualCellSource::UnoccupiedPlannedCell;
	ResidualCell.SourceRegionDebugPath = TEXT("BundleOccupied");

	FString FailureReason;
	const bool bApplied = LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
		FLayoutProfileSolver::BuildProfileSnapshot(Profile),
		TEXT("BundleOccupied"),
		913,
		SolveResult,
		FailureReason);
	TestTrue(TEXT("Soft sparse placement evaluation still succeeds when the only residual cell is already occupied by an existing bundle"), bApplied);
	if (!bApplied)
	{
		AddError(FailureReason);
		return false;
	}

	TestEqual(TEXT("No sparse placements are added on a residual cell already occupied by the existing bundle"), SolveResult.SparsePlacementCommitments.Num(), 0);
	TestEqual(TEXT("The original structural placement remains the only placement"), SolveResult.Placements.Num(), 1);
	TestEqual(TEXT("The occupied residual cell remains unconsumed"), SolveResult.ResidualUnoccupiedCells.Num(), 1);
	TestTrue(TEXT("Best-effort underfill remains diagnostic without rejecting structural proof"), ContainsValidationMessageSubstring(
		SolveResult.Messages,
		TEXT("requested at least 1 placements but found no eligible residual capacity")));

	return true;
}
