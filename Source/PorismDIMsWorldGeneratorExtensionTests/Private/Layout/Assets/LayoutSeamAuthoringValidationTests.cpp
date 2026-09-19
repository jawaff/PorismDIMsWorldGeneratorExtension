// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutAuthoredSeamIdentityPropertyTest,
	"PorismExtension.Layout.ContentSet.Validation.AuthoredSeamIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutAuthoredSeamIdentityPropertyTest::RunTest(const FString& Parameters)
{
	// Existing assets serialize this authored field as NameProperty, just like EntryId.
	const FProperty* Property = FindFProperty<FProperty>(FLayoutSeamProviderIntent::StaticStruct(),
		GET_MEMBER_NAME_CHECKED(FLayoutSeamProviderIntent, SeamIntentId));
	TestNotNull(TEXT("Authored seam IDs retain their native serialized name property"), CastField<FNameProperty>(Property));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamValidationRejectsSeamIntentOnModuleWithoutBoundaryCapableRoleTest,
	"PorismExtension.Layout.ContentSet.Validation.RejectsSeamIntentOnModuleWithoutBoundaryCapableRole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSeamValidationRejectsSeamIntentOnModuleWithoutBoundaryCapableRoleTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutTemplate_InteriorOnlySeamIntent"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutModule_InteriorOnlySeamIntent"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("InteriorOnlySeamEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Entry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Perimeter;

	FLayoutSeamProviderIntent& SeamIntent = Entry.SeamProviderIntents.AddDefaulted_GetRef();
	SeamIntent.SeamIntentId = TEXT("InteriorOnlySharedWall");
	SeamIntent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	SeamIntent.bCanOwnSeam = true;
	SeamIntent.bCanAcceptSeam = false;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ContentSet_InteriorOnlySeamIntent"),
		{Entry});

	const FLayoutValidationResult Validation = ContentSet->ValidateContentSet();
	TestFalse(
		TEXT("Content-set validation should fail when a seam intent is authored on a module without any boundary-capable role"),
		Validation.IsValid());
	TestTrue(
		TEXT("Validation should report that the module entry can never derive seam span offers"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Severity == ELayoutValidationSeverity::Error
				&& Message.Message.Contains(TEXT("can never derive seam span offers"));
		}));
	return true;
}
