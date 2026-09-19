// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutClosureValidationRejectsDuplicateClosureIdsTest,
	"PorismExtension.Layout.Profile.Validation.RejectsDuplicateClosureIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutClosureValidationRejectsDuplicateClosureIdsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_DuplicateClosures"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		1,
		true);

	FLayoutClosureRequirement& FirstRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
	FirstRequirement.ClosureId = TEXT("OuterPerimeter");
	FirstRequirement.Zone = ELayoutPlacementZone::Perimeter;
	FirstRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
	FirstRequirement.BoundsPolicy.MinLevel = 0;
	FirstRequirement.BoundsPolicy.MaxLevel = 1;
	FirstRequirement.MinThicknessCells = 1;

	FLayoutClosureRequirement& DuplicateRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
	DuplicateRequirement = FirstRequirement;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when closure ids are duplicated"), Validation.IsValid());
	TestTrue(TEXT("Validation should report duplicate closure ids"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("duplicates ClosureId"));
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutClosureDraftGenerationBuildsPerimeterClosureTest,
	"PorismExtension.Layout.Profile.Migration.GeneratePerimeterClosureDraft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutClosureDraftGenerationBuildsPerimeterClosureTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_PerimeterDraft"),
		FIntPoint(6, 6),
		FIntPoint(8, 8),
		3,
		1,
		true);

	Profile->GeneratePerimeterClosureDraftInEditor();
	TestEqual(TEXT("Draft generation creates one closure requirement"), Profile->ClosureRequirements.Num(), 1);
	if (Profile->ClosureRequirements.Num() == 0)
	{
		return false;
	}

	const FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements[0];
	TestEqual(TEXT("Draft closure id uses the expected default"), ClosureRequirement.ClosureId, FName(TEXT("OuterPerimeter")));
	TestEqual(TEXT("Draft uses perimeter zone"), ClosureRequirement.Zone, ELayoutPlacementZone::Perimeter);
	TestEqual(TEXT("Draft uses solved-footprint bounds mode"), ClosureRequirement.BoundsPolicy.Mode, ELayoutBoundsPolicyMode::SolvedFootprint);
	TestEqual(TEXT("Draft spans from level zero"), ClosureRequirement.BoundsPolicy.MinLevel, 0);
	TestEqual(TEXT("Draft spans every current solved level"), ClosureRequirement.BoundsPolicy.MaxLevel, 2);

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestTrue(TEXT("Generated perimeter closure draft validates"), Validation.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutClosureProviderIntentValidationRejectsDuplicateEntryIdsTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsDuplicateClosureProviderIntentIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutClosureProviderIntentValidationRejectsDuplicateEntryIdsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_DuplicateClosureProviderIntents"),
		CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_DuplicateClosureProviderIntents"), FIntVector(8, 8, 8)),
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("Gate");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	FLayoutClosureProviderIntent& FirstIntent = Entry.ClosureProviderIntents.AddDefaulted_GetRef();
	FirstIntent.ProviderIntentId = TEXT("GatePerimeterProvider");
	FirstIntent.Zone = ELayoutPlacementZone::Perimeter;
	FirstIntent.ClosureId = TEXT("OuterPerimeter");

	FLayoutClosureProviderIntent& DuplicateIntent = Entry.ClosureProviderIntents.AddDefaulted_GetRef();
	DuplicateIntent = FirstIntent;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("ContentSet_DuplicateClosureProviderIntents"), {Entry});
	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(TEXT("Content-set validation should fail when closure provider intent ids are duplicated"), Validation.IsValid());
	TestTrue(TEXT("Validation should report duplicate closure provider intent ids"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("duplicate ProviderIntentId"));
	}));

	return true;
}
