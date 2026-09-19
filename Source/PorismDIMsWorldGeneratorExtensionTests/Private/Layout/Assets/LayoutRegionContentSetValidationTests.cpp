// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Misc/AutomationTest.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsDuplicateEntryIdsTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsDuplicateEntryIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsChildEntryWithoutProfileTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsChildEntryWithoutProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsInvalidChildSpecificLevelTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsInvalidChildSpecificLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsNonLayoutProvidedZoneFeatureTagTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsNonLayoutProvidedZoneFeatureTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingSingleLevelChildTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingSingleLevelChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutVerticalAccessContentTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingChildWithoutVerticalAccessContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutBoundaryEntryContentTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingChildWithoutBoundaryEntryContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutDistinctBoundaryEntryLevelsTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingChildWithoutDistinctBoundaryEntryLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutVerticalAccessCoverageAcrossBoundaryEntryLevelsTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingChildWithoutVerticalAccessCoverageAcrossBoundaryEntryLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutSharedTraversalChannelAcrossBoundaryEntryLevelsTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingChildWithoutSharedTraversalChannelAcrossBoundaryEntryLevels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutOnePairThatHasBothTraversalAndVerticalCoverageTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsHostContributingChildWithoutOnePairThatHasBothTraversalAndVerticalCoverage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsModuleEntryWithLeafAndCompositeSourcesTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsModuleEntryWithLeafAndCompositeSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationLoadsLeafModuleTemplateDimensionsFromSoftReferenceTest,
	"PorismExtension.Layout.ContentSet.Validation.LoadsLeafModuleTemplateDimensionsFromSoftReference",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationAcceptsCompositeModuleEntryTest,
	"PorismExtension.Layout.ContentSet.Validation.AcceptsCompositeModuleEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationRejectsCompositeModuleEntryThatViolatesEstablishedSharedCellSizeTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsCompositeModuleEntryThatViolatesEstablishedSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	ULayoutCompositeModuleAsset* CreateValidationCompositeModule(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<FLayoutCompositeModuleCell>& Cells)
	{
		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, FName(Name));
		Composite->Cells = Cells;
		return Composite;
	}

	FLayoutCompositeModuleCell MakeValidationCompositeCell(
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

bool FLayoutRegionContentSetValidationRejectsDuplicateEntryIdsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("ContentSetValidationModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry FirstEntry;
	FirstEntry.EntryId = TEXT("OuterWall");
	FirstEntry.ContentKind = ELayoutRegionContentKind::Module;
	FirstEntry.ModuleSettings.Module = Module;

	FLayoutRegionContentEntry DuplicateEntry = FirstEntry;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_DuplicateEntryIds"),
		{FirstEntry, DuplicateEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when entry ids are duplicated"), Validation.IsValid());
	TestTrue(TEXT("Validation should report duplicate entry ids"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("EntryId"))
			&& (Message.Message.Contains(TEXT("duplicate")) || Message.Message.Contains(TEXT("duplicates")));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsChildEntryWithoutProfileTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("ChildWithoutProfile");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("ContentSet_ChildWithoutProfile"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a child entry has no profile"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing child region profile"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("references no child region profile"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsInvalidChildSpecificLevelTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("ContentSetValidationChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("ChildSpecificLevel");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	ChildEntry.ChildRegionSettings.SpecificLevel = -1;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("ContentSet_InvalidChildSpecificLevel"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a child entry uses a negative SpecificLevel"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the invalid child specific level"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("invalid child specific level"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsNonLayoutProvidedZoneFeatureTagTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationFeatureTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("ContentSetValidationFeatureModule"),
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
	Entry.EntryId = TEXT("FeatureProvider");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Entry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::TraversalPrimary);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_NonLayoutProvidedZoneFeature"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when ProvidedZoneFeatures uses a non-layout tag"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the non-layout provided feature tag"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("provides a non-layout feature tag"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingSingleLevelChildTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationSingleLevelVerticalTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSingleLevelVerticalModule"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry VerticalEntry;
	VerticalEntry.EntryId = TEXT("Vertical");
	VerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	VerticalEntry.ModuleSettings.Module = VerticalAccessModule;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationSingleLevelChildContent"),
		{VerticalEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationSingleLevelChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildSingleLevel");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildSingleLevel"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a host-contributing child profile is single-level"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the single-level host-contributing child"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("cannot contribute host vertical access from a single-level child profile"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutVerticalAccessContentTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationBoundaryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* BoundaryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationBoundaryOnlyModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry BoundaryEntry;
	BoundaryEntry.EntryId = TEXT("Boundary");
	BoundaryEntry.ContentKind = ELayoutRegionContentKind::Module;
	BoundaryEntry.ModuleSettings.Module = BoundaryModule;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationNoVerticalChildContent"),
		{BoundaryEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationNoVerticalChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		2,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildNoVerticalContent");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildNoVerticalContent"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a host-contributing child has no VerticalAccess-capable content"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing VerticalAccess-capable child content"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("has no VerticalAccess-capable content"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutBoundaryEntryContentTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationInteriorEntryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* InteriorEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationInteriorEntryOnlyModule"),
		Template,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry VerticalEntry;
	VerticalEntry.EntryId = TEXT("InteriorEntry");
	VerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	VerticalEntry.ModuleSettings.Module = InteriorEntryModule;
	VerticalEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationNoBoundaryEntryChildContent"),
		{VerticalEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationNoBoundaryEntryChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		2,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildNoBoundaryEntryContent");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildNoBoundaryEntryContent"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a host-contributing child has no boundary-reachable Entry-capable content"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing boundary-reachable Entry-capable child content"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("has no boundary-reachable Entry-capable content"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutDistinctBoundaryEntryLevelsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationGroundEntryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* GroundEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationGroundEntryModule"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry GroundEntry;
	GroundEntry.EntryId = TEXT("GroundDoor");
	GroundEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundEntry.ModuleSettings.Module = GroundEntryModule;
	GroundEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	GroundEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	UChunkStructureTemplate* VerticalTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationVerticalTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* VerticalModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationVerticalSupportModule"),
		VerticalTemplate,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry VerticalEntry;
	VerticalEntry.EntryId = TEXT("VerticalSupport");
	VerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	VerticalEntry.ModuleSettings.Module = VerticalModule;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationSingleBoundaryEntryLevelChildContent"),
		{GroundEntry, VerticalEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationSingleBoundaryEntryLevelChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		2,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildSingleBoundaryEntryLevel");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildSingleBoundaryEntryLevel"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a host-contributing child can only expose boundary entry content on one level"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing distinct boundary-entry levels"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("does not expose boundary-reachable Entry-capable content on at least two distinct levels"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutVerticalAccessCoverageAcrossBoundaryEntryLevelsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationSplitBoundaryEntryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* BoundaryEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSplitBoundaryEntryModule"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry GroundEntry;
	GroundEntry.EntryId = TEXT("GroundDoor");
	GroundEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundEntry.ModuleSettings.Module = BoundaryEntryModule;
	GroundEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	GroundEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	FLayoutRegionContentEntry TopEntry;
	TopEntry.EntryId = TEXT("TopDoor");
	TopEntry.ContentKind = ELayoutRegionContentKind::Module;
	TopEntry.ModuleSettings.Module = BoundaryEntryModule;
	TopEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	TopEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;

	UChunkStructureTemplate* VerticalTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationGroundVerticalTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* GroundVerticalModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationGroundVerticalModule"),
		VerticalTemplate,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry GroundVerticalEntry;
	GroundVerticalEntry.EntryId = TEXT("GroundVertical");
	GroundVerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundVerticalEntry.ModuleSettings.Module = GroundVerticalModule;
	GroundVerticalEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationNoVerticalCoverageSpanChildContent"),
		{GroundEntry, TopEntry, GroundVerticalEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationNoVerticalCoverageSpanChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		3,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildNoVerticalCoverageSpan");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildNoVerticalCoverageSpan"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a host-contributing child cannot cover the span between its lower and upper boundary-entry levels with VerticalAccess-capable content"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing VerticalAccess-capable coverage span"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("has no VerticalAccess-capable coverage across the span between its possible lower and upper boundary-entry levels"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutSharedTraversalChannelAcrossBoundaryEntryLevelsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationTraversalSplitEntryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* PrimaryBoundaryEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationPrimaryBoundaryEntryModule"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* SecondaryBoundaryEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSecondaryBoundaryEntryModule"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalSecondary})));

	FLayoutRegionContentEntry GroundEntry;
	GroundEntry.EntryId = TEXT("GroundDoor");
	GroundEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundEntry.ModuleSettings.Module = PrimaryBoundaryEntryModule;
	GroundEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	GroundEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	FLayoutRegionContentEntry TopEntry;
	TopEntry.EntryId = TEXT("TopDoor");
	TopEntry.ContentKind = ELayoutRegionContentKind::Module;
	TopEntry.ModuleSettings.Module = SecondaryBoundaryEntryModule;
	TopEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	TopEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;

	UChunkStructureTemplate* VerticalTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationTallVerticalTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* TallVerticalLeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationTallVerticalLeafModule"),
		VerticalTemplate,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutCompositeModuleAsset* TallVerticalCompositeModule = CreateValidationCompositeModule(
		Outer,
		TEXT("ContentSetValidationTallVerticalCompositeModule"),
		{
			MakeValidationCompositeCell(TallVerticalLeafModule, FIntVector(0, 0, 0)),
			MakeValidationCompositeCell(TallVerticalLeafModule, FIntVector(0, 0, 1)),
			MakeValidationCompositeCell(TallVerticalLeafModule, FIntVector(0, 0, 2))
		});

	FLayoutRegionContentEntry TallVerticalEntry;
	TallVerticalEntry.EntryId = TEXT("TallVertical");
	TallVerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	TallVerticalEntry.ModuleSettings.CompositeModule = TallVerticalCompositeModule;
	TallVerticalEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationNoSharedTraversalChannelChildContent"),
		{GroundEntry, TopEntry, TallVerticalEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationNoSharedTraversalChannelChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		3,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildNoSharedTraversalChannel");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildNoSharedTraversalChannel"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when a host-contributing child has no shared traversal channel across any possible lower and upper boundary-entry levels"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing shared traversal channel across boundary-entry levels"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("exposes no shared traversal channel across any possible lower and upper boundary-entry level pair"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsHostContributingChildWithoutOnePairThatHasBothTraversalAndVerticalCoverageTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationMixedPairEntryTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* PrimaryBoundaryEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationMixedPairPrimaryBoundaryEntryModule"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* SecondaryBoundaryEntryModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationMixedPairSecondaryBoundaryEntryModule"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalSecondary})));

	FLayoutRegionContentEntry GroundPrimaryEntry;
	GroundPrimaryEntry.EntryId = TEXT("GroundPrimaryDoor");
	GroundPrimaryEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundPrimaryEntry.ModuleSettings.Module = PrimaryBoundaryEntryModule;
	GroundPrimaryEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	GroundPrimaryEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	FLayoutRegionContentEntry MidSecondaryEntry;
	MidSecondaryEntry.EntryId = TEXT("MidSecondaryDoor");
	MidSecondaryEntry.ContentKind = ELayoutRegionContentKind::Module;
	MidSecondaryEntry.ModuleSettings.Module = SecondaryBoundaryEntryModule;
	MidSecondaryEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	MidSecondaryEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	MidSecondaryEntry.ModuleSettings.SpecificLevel = 1;

	FLayoutRegionContentEntry TopPrimaryEntry;
	TopPrimaryEntry.EntryId = TEXT("TopPrimaryDoor");
	TopPrimaryEntry.ContentKind = ELayoutRegionContentKind::Module;
	TopPrimaryEntry.ModuleSettings.Module = PrimaryBoundaryEntryModule;
	TopPrimaryEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;
	TopPrimaryEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::TopLevelOnly;

	UChunkStructureTemplate* VerticalTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationMixedPairVerticalTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* ShortVerticalLeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationMixedPairShortVerticalLeafModule"),
		VerticalTemplate,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutCompositeModuleAsset* GroundToMidVerticalCompositeModule = CreateValidationCompositeModule(
		Outer,
		TEXT("ContentSetValidationMixedPairGroundToMidVerticalCompositeModule"),
		{
			MakeValidationCompositeCell(ShortVerticalLeafModule, FIntVector(0, 0, 0)),
			MakeValidationCompositeCell(ShortVerticalLeafModule, FIntVector(0, 0, 1))
		});

	FLayoutRegionContentEntry GroundToMidVerticalEntry;
	GroundToMidVerticalEntry.EntryId = TEXT("GroundToMidVertical");
	GroundToMidVerticalEntry.ContentKind = ELayoutRegionContentKind::Module;
	GroundToMidVerticalEntry.ModuleSettings.CompositeModule = GroundToMidVerticalCompositeModule;
	GroundToMidVerticalEntry.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;

	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSetValidationNoOnePairWithTraversalAndCoverageChildContent"),
		{GroundPrimaryEntry, MidSecondaryEntry, TopPrimaryEntry, GroundToMidVerticalEntry});

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("ContentSetValidationNoOnePairWithTraversalAndCoverageChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		3,
		0,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("HostVerticalChildNoOnePairWithTraversalAndCoverage");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.Weight = 1;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.bContributesHostVerticalAccess = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_HostVerticalChildNoOnePairWithTraversalAndCoverage"),
		{ChildEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when traversal overlap and vertical span coverage exist only on different lower/upper boundary-entry pairs"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the missing single lower/upper pair that has both traversal overlap and vertical coverage"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("no possible lower and upper boundary-entry level pair has both shared traversal channels and full VerticalAccess coverage across the same span"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationRejectsModuleEntryWithLeafAndCompositeSourcesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationLeafAndCompositeTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationLeafAndCompositeLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	ULayoutCompositeModuleAsset* CompositeModule = CreateValidationCompositeModule(
		Outer,
		TEXT("ContentSetValidationLeafAndCompositeComposite"),
		{
			MakeValidationCompositeCell(LeafModule, FIntVector(0, 0, 0))
		});

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("AmbiguousModuleSource");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_LeafAndCompositeSources"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation rejects module entries that author both leaf and composite sources"), Validation.IsValid());
	TestTrue(TEXT("Validation reports that one module entry authored both source references"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("references both a module and a composite module"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationLoadsLeafModuleTemplateDimensionsFromSoftReferenceTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationSoftReferencedTemplate"), FIntVector(12, 12, 12));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSoftReferencedLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	LeafModule->Template.ResetWeakPtr();

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SoftReferencedBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_SoftReferencedLeafModule"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Content-set validation reloads valid leaf template dimensions from the module soft reference"), Validation.IsValid());
	TestFalse(TEXT("Content-set validation no longer reports a bogus 0,0,0 template-dimensions error for a valid soft-referenced leaf module"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("TemplateDimensionsBlocks: X=0 Y=0 Z=0"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationAcceptsCompositeModuleEntryTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationCompositeTemplate"), FIntVector(12, 12, 12));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationCompositeLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	// Mark faces as boundary-facing so the composite validation recognizes this cell
	// as contributing a real exterior boundary face (required for Boundary-capable cells).
	LeafModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LeafModule->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LeafModule->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LeafModule->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LeafModule->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LeafModule->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutCompositeModuleAsset* CompositeModule = CreateValidationCompositeModule(
		Outer,
		TEXT("ContentSetValidationCompositeEntry"),
		{
			MakeValidationCompositeCell(LeafModule, FIntVector(0, 0, 0))
		});

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeEntry"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Content-set validation accepts a valid composite-backed module entry"), Validation.IsValid());
	TestEqual(TEXT("Composite-backed content set resolves the shared cell size from its referenced leaf modules"), ContentSet->GetSharedCellSizeInBlocks(), Template->SizeInBlocks);
	return true;
}

bool FLayoutRegionContentSetValidationRejectsCompositeModuleEntryThatViolatesEstablishedSharedCellSizeTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* SmallTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationSharedCellSizeSmallTemplate"), FIntVector(8, 8, 8));
	UChunkStructureTemplate* LargeTemplate = CreateTemplate(Outer, TEXT("ContentSetValidationSharedCellSizeLargeTemplate"), FIntVector(16, 16, 16));

	ULayoutModuleAsset* SmallLeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSharedCellSizeSmallLeaf"),
		SmallTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	SmallLeafModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	SmallLeafModule->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	SmallLeafModule->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	SmallLeafModule->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	SmallLeafModule->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	SmallLeafModule->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutModuleAsset* LargeLeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSharedCellSizeLargeLeaf"),
		LargeTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	LargeLeafModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LargeLeafModule->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LargeLeafModule->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LargeLeafModule->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LargeLeafModule->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	LargeLeafModule->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutCompositeModuleAsset* LargeCompositeModule = CreateValidationCompositeModule(
		Outer,
		TEXT("ContentSetValidationSharedCellSizeComposite"),
		{
			MakeValidationCompositeCell(LargeLeafModule, FIntVector(0, 0, 0))
		});

	FLayoutRegionContentEntry SmallLeafEntry;
	SmallLeafEntry.EntryId = TEXT("SmallLeaf");
	SmallLeafEntry.ContentKind = ELayoutRegionContentKind::Module;
	SmallLeafEntry.ModuleSettings.Module = SmallLeafModule;

	FLayoutRegionContentEntry LargeCompositeEntry;
	LargeCompositeEntry.EntryId = TEXT("LargeComposite");
	LargeCompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	LargeCompositeEntry.ModuleSettings.CompositeModule = LargeCompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_CompositeViolatesEstablishedSharedCellSize"),
		{SmallLeafEntry, LargeCompositeEntry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Content-set validation no longer rejects composite-backed entries just because sibling content resolves a different effective cell size"), Validation.IsValid());
	TestFalse(TEXT("Validation no longer reports the composite-specific established shared cell size failure on the content-set surface"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("violates the established shared cell size contract"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationGetterResolvesDerivedModuleSharedCellSizeTest,
	"PorismExtension.Layout.ContentSet.Validation.GetterResolvesDerivedModuleSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationDoesNotWarnWhenDerivedSharedCellSizeExistsTest,
	"PorismExtension.Layout.ContentSet.Validation.DoesNotWarnWhenDerivedSharedCellSizeExists",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationAcceptsChildOnlyContentWithoutSharedCellSizeFieldTest,
	"PorismExtension.Layout.ContentSet.Validation.AcceptsChildOnlyContentWithoutSharedCellSizeField",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationWarnsWhenLeafEntryUsesLegacyMultiCellShapeTest,
	"PorismExtension.Layout.ContentSet.Validation.WarnsWhenLeafEntryUsesLegacyMultiCellShape",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRegionContentSetValidationDefersStaleOneCellBoundsMismatchToLeafValidationTest,
	"PorismExtension.Layout.ContentSet.Validation.DefersStaleOneCellBoundsMismatchToLeafValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRegionContentSetValidationGetterResolvesDerivedModuleSharedCellSizeTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationExplicitSharedCellSizeTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationExplicitSharedCellSizeLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("LeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_ExplicitSharedCellSize"),
		{Entry});

	TestEqual(TEXT("Content-set getter resolves the derived module shared cell size"), ContentSet->GetSharedCellSizeInBlocks(), FIntVector(16, 16, 16));

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Content-set validation stays valid when one derived module source proves the lattice"), Validation.IsValid());
	TestFalse(TEXT("Validation no longer reports the stale intra-content-set module cell size mismatch"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("mixes incompatible module cell sizes"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationDoesNotWarnWhenDerivedSharedCellSizeExistsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationSharedCellSizeFallbackTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationSharedCellSizeFallbackLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("LeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_SharedCellSizeFallback"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Derived shared cell size does not invalidate the content set"), Validation.IsValid());
	TestFalse(TEXT("Content-set validation does not warn about missing authored shared cell size fields"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("still derives shared cell size"))
			&& Message.Message.Contains(TEXT("SharedCellSizeInBlocks"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationAcceptsChildOnlyContentWithoutSharedCellSizeFieldTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* ChildProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("ContentSetValidationInvalidCompatibilitySharedCellSizeChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("ChildBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	Entry.Weight = 1;
	Entry.ChildRegionSettings.RegionProfile = ChildProfile;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("ContentSet_InvalidCompatibilitySharedCellSize"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Content-set validation accepts child-only content when no authored shared-cell-size field remains"), Validation.IsValid());
	return true;
}

bool FLayoutRegionContentSetValidationWarnsWhenLeafEntryUsesLegacyMultiCellShapeTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationLegacyMultiCellLeafTemplate"), FIntVector(16, 8, 8));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationLegacyMultiCellLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("LeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_LegacyMultiCellLeaf"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Single-cell leaf entries no longer emit fake legacy multi-cell warnings"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("legacy multi-cell leaf module"))
			|| Message.Message.Contains(TEXT("CompositeModule"));
	}));
	return true;
}

bool FLayoutRegionContentSetValidationDefersStaleOneCellBoundsMismatchToLeafValidationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("ContentSetValidationStaleOneCellBoundsTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("ContentSetValidationStaleOneCellBoundsLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("StaleOneCellLeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_StaleOneCellBoundsLeaf"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestTrue(TEXT("Stale one-cell bounds drift is now treated as compatibility-only at the content-set surface"), Validation.IsValid());
	TestFalse(TEXT("Validation no longer reports a leaf-owned template-dimension mismatch when the occupied contract is already one cell"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("template dimensions do not match base cell size and bounds"));
	}));
	TestFalse(TEXT("Content-set validation no longer adds a second fake legacy multi-cell leaf warning when occupancy is already one cell"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("legacy multi-cell leaf module"));
	}));
	return true;
}
