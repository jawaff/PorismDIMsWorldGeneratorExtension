// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationIncludesContentSetValidationTest,
	"PorismExtension.Layout.Profile.Validation.IncludesContentSetValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileValidationIncludesContentSetValidationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_ContentSetValidation"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		1,
		0,
		false);

	FLayoutRegionContentEntry FirstEntry;
	FirstEntry.EntryId = TEXT("DuplicateEntry");
	FirstEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	FirstEntry.Weight = 1;

	FLayoutRegionContentEntry DuplicateEntry = FirstEntry;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutRegionContentSet_InvalidDuplicates"),
		{FirstEntry, DuplicateEntry});
	Profile->ContentSet = ContentSet;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when the referenced content set is invalid"), Validation.IsValid());
	TestTrue(TEXT("Profile validation should include content-set duplicate-entry errors"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("EntryId"))
			&& (Message.Message.Contains(TEXT("duplicate")) || Message.Message.Contains(TEXT("duplicates")));
	}));
	return true;
}
