// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateOptionalChildDropTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	ULayoutModuleAsset* CreateOpenBoundaryModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Interior},
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

	ULayoutModuleAsset* CreateInteriorOnlyModule(UObject* Outer, const TCHAR* Prefix)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), Prefix), FIntVector(8, 8, 8));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), Prefix),
			Template,
			{ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor));
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

	ULayoutRegionContentSetAsset* CreateParentContentSet(
		UObject* Outer,
		const TCHAR* Name,
		ULayoutModuleAsset* ParentModule,
		ULayoutProfileAsset* ChildProfile,
		const bool bOptional)
	{
		FLayoutRegionContentEntry ParentEntry;
		ParentEntry.EntryId = TEXT("ParentShell");
		ParentEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentEntry.ModuleSettings.Module = ParentModule;

		FLayoutRegionContentEntry ChildEntry;
		ChildEntry.EntryId = TEXT("OptionalAnnex");
		ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
		ChildEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Any;
		ChildEntry.ChildRegionSettings.bOptional = bOptional;

		return CreateRegionContentSet(Outer, Name, {ParentEntry, ChildEntry});
	}

	ULayoutRegionContentSetAsset* CreateChildContentSet(
		UObject* Outer,
		const TCHAR* Name,
		ULayoutModuleAsset* ChildModule)
	{
		FLayoutRegionContentEntry ChildEntry;
		ChildEntry.EntryId = TEXT("ChildFailureModule");
		ChildEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildEntry.ModuleSettings.Module = ChildModule;
		ChildEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		ChildEntry.ModuleSettings.SpecificLevel = 1;
		return CreateRegionContentSet(Outer, Name, {ChildEntry});
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
		Request.FootprintSize = FIntPoint(1, 1);
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = Intent;
		return Request;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOptionalChildDropRecordsResidualCellsTest,
	"PorismExtension.Layout.Solver.OptionalChildDrop.RecordsResidualCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOptionalChildDropRecordsResidualCellsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateOptionalChildDropTestOuter(TEXT("LayoutOptionalChildDrop"));

	ULayoutProfileAsset* ChildProfile = CreateProfile(Outer, TEXT("OptionalChildProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, false);
	ULayoutModuleAsset* ParentModule = CreateOpenBoundaryModule(Outer, TEXT("OptionalParent"));
	ULayoutModuleAsset* FailingChildModule = CreateDualContextEntryModule(Outer, TEXT("OptionalChildFailure"));
	ULayoutRegionContentSetAsset* ChildContentSet = CreateChildContentSet(Outer, TEXT("OptionalChildContentSet"), FailingChildModule);
	ChildProfile->ContentSet = ChildContentSet;
	ULayoutRegionContentSetAsset* ParentContentSet = CreateParentContentSet(Outer, TEXT("OptionalParentContentSet"), ParentModule, ChildProfile, true);

	ULayoutProfileAsset* ParentProfile = CreateProfile(Outer, TEXT("OptionalParentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ParentProfile->ContentSet = ParentContentSet;

	FLayoutRegionSolveRequest ParentRequest = BuildSingleCellRequest(
		ParentContentSet,
		ParentProfile,
		601,
		TEXT("Parent"),
		FIntVector(0, 0, 0),
		ELayoutCellIntent::Boundary);
	FLayoutRegionSolveRequest ChildRequest = BuildSingleCellRequest(
		ChildContentSet,
		ChildProfile,
		602,
		TEXT("OptionalChild"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Entry);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests = {ParentRequest, ChildRequest};
	ScheduleRequest.Dependencies.Add({TEXT("Parent"), TEXT("OptionalChild")});

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	TestTrue(TEXT("Schedule succeeds by dropping the optional child"), ScheduleResult.bSucceeded);
	TestEqual(TEXT("Merged solve records one dropped optional child"), ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num(), 1);
	TestEqual(TEXT("Merged solve records one residual cell for the dropped child"), ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.Num(), 1);
	if (!ScheduleResult.bSucceeded
		|| ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Num() != 1
		|| ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells.Num() != 1)
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	const FLayoutDroppedOptionalChildRecord& DropRecord = ScheduleResult.MergedSolveResult.DroppedOptionalChildren[0];
	TestEqual(TEXT("Dropped child keeps the parent debug path"), DropRecord.ParentRegionDebugPath, FString(TEXT("Parent")));
	TestEqual(TEXT("Dropped child keeps the child debug path"), DropRecord.ChildRegionDebugPath, FString(TEXT("OptionalChild")));
	TestEqual(TEXT("Dropped child keeps the content-entry id"), DropRecord.SourceContentEntryId, FName(TEXT("OptionalAnnex")));
	TestFalse(TEXT("Dropped child reports a non-empty blocking reason"), DropRecord.FailureReason.IsEmpty());

	const FLayoutResidualCellRecord& ResidualCell = ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells[0];
	TestEqual(TEXT("Residual cell keeps the child cell"), ResidualCell.Cell, FIntVector(1, 0, 0));
	TestEqual(TEXT("Residual cell keeps the entry intent"), ResidualCell.Intent, ELayoutCellIntent::Entry);
	TestEqual(TEXT("Residual cell is tagged as coming from a dropped optional child"), ResidualCell.Source, ELayoutResidualCellSource::DroppedOptionalChild);
	TestEqual(TEXT("Residual cell keeps the dropped child region path"), ResidualCell.SourceRegionDebugPath, FString(TEXT("OptionalChild")));
	TestEqual(TEXT("Residual cell keeps the dropped child content-entry id"), ResidualCell.SourceContentEntryId, FName(TEXT("OptionalAnnex")));
	TestEqual(TEXT("Residual cell links back to the drop decision id"), ResidualCell.RelatedDropDecisionId, DropRecord.DropDecisionId);

	const FLayoutRegionSolveResult* DroppedChildResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& Result)
	{
		return Result.RegionDebugPath == TEXT("OptionalChild");
	});
	TestNotNull(TEXT("Dropped child still appears in the ordered schedule results"), DroppedChildResult);
	if (DroppedChildResult == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Dropped child result is marked as dropped"), DroppedChildResult->bDroppedAsOptionalChild);
	TestEqual(TEXT("Dropped child result keeps the content-entry id"), DroppedChildResult->SourceContentEntryId, FName(TEXT("OptionalAnnex")));
	TestEqual(TEXT("Dropped child result keeps the drop-decision id"), DroppedChildResult->OptionalDropDecisionId, DropRecord.DropDecisionId);
	TestTrue(TEXT("Merged placements only include the parent's module"), ScheduleResult.MergedSolveResult.Placements.Num() == 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOptionalChildDropRejectsRequiredChildFailureTest,
	"PorismExtension.Layout.Solver.OptionalChildDrop.RejectsRequiredChildFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOptionalChildDropRejectsRequiredChildFailureTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateOptionalChildDropTestOuter(TEXT("LayoutRequiredChildFailure"));

	ULayoutProfileAsset* ChildProfile = CreateProfile(Outer, TEXT("RequiredChildProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, false);
	ULayoutModuleAsset* ParentModule = CreateOpenBoundaryModule(Outer, TEXT("RequiredParent"));
	ULayoutModuleAsset* FailingChildModule = CreateDualContextEntryModule(Outer, TEXT("RequiredChildFailure"));
	ULayoutRegionContentSetAsset* ChildContentSet = CreateChildContentSet(Outer, TEXT("RequiredChildContentSet"), FailingChildModule);
	ChildProfile->ContentSet = ChildContentSet;
	ULayoutRegionContentSetAsset* ParentContentSet = CreateParentContentSet(Outer, TEXT("RequiredParentContentSet"), ParentModule, ChildProfile, false);

	ULayoutProfileAsset* ParentProfile = CreateProfile(Outer, TEXT("RequiredParentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ParentProfile->ContentSet = ParentContentSet;

	FLayoutRegionSolveRequest ParentRequest = BuildSingleCellRequest(
		ParentContentSet,
		ParentProfile,
		701,
		TEXT("Parent"),
		FIntVector(0, 0, 0),
		ELayoutCellIntent::Boundary);
	FLayoutRegionSolveRequest ChildRequest = BuildSingleCellRequest(
		ChildContentSet,
		ChildProfile,
		702,
		TEXT("RequiredChild"),
		FIntVector(1, 0, 0),
		ELayoutCellIntent::Entry);

	FLayoutRegionSolveScheduleRequest ScheduleRequest;
	ScheduleRequest.RegionRequests = {ParentRequest, ChildRequest};
	ScheduleRequest.Dependencies.Add({TEXT("Parent"), TEXT("RequiredChild")});

	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	TestFalse(TEXT("Schedule fails when the child entry is not optional"), ScheduleResult.bSucceeded);
	TestTrue(TEXT("No optional child drop record is emitted"), ScheduleResult.MergedSolveResult.DroppedOptionalChildren.IsEmpty());
	return true;
}
