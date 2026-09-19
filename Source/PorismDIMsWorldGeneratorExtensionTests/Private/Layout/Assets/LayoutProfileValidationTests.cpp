// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsZoneFeatureRequirementWithoutContentSetTest,
	"PorismExtension.Layout.Profile.Validation.RejectsZoneFeatureRequirementWithoutContentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsNonLayoutZoneFeatureRequirementTagTest,
	"PorismExtension.Layout.Profile.Validation.RejectsNonLayoutZoneFeatureRequirementTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsInvalidReservedOpenRuleTest,
	"PorismExtension.Layout.Profile.Validation.RejectsInvalidReservedOpenRule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationWarnsForDisabledTerrainSeamFaceRulesTest,
	"PorismExtension.Layout.Profile.Validation.WarnsForDisabledTerrainSeamFaceRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationDeduplicatesDisabledTerrainSeamCompositeLeavesTest,
	"PorismExtension.Layout.Profile.Validation.DeduplicatesDisabledTerrainSeamCompositeLeaves",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationAcceptsSingleLevelSteppedTerrainCapabilityWithJoiningContentTest,
	"PorismExtension.Layout.Profile.Validation.AcceptsSingleLevelSteppedTerrainCapabilityWithJoiningContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationWarnsOnlyForSingleLevelProfilesWithoutSteppedTerrainSupportTest,
	"PorismExtension.Layout.Profile.Validation.WarnsOnlyForSingleLevelProfilesWithoutSteppedTerrainSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationSteppedTerrainVerticalAccessDependsOnTraversalRequirementsTest,
	"PorismExtension.Layout.Profile.Validation.SteppedTerrainVerticalAccessDependsOnTraversalRequirements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationStrictMultiLevelAllowsTopologyResolvedVerticalAccessTest,
	"PorismExtension.Layout.Profile.Validation.StrictMultiLevelAllowsTopologyResolvedVerticalAccess",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationAcceptsSteppedTerrainCapabilityWithVerticalAccessContentTest,
	"PorismExtension.Layout.Profile.Validation.AcceptsSteppedTerrainCapabilityWithVerticalAccessContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsContinuationEntryLevelWithoutContentSetTest,
	"PorismExtension.Layout.Profile.Validation.RejectsContinuationEntryLevelWithoutContentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsContinuationEntryLevelWithoutEntryContentTest,
	"PorismExtension.Layout.Profile.Validation.RejectsContinuationEntryLevelWithoutEntryContent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsContinuationEntryLevelWithoutReachabilityTest,
	"PorismExtension.Layout.Profile.Validation.RejectsContinuationEntryLevelWithoutReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsEvenContinuationCorridorWidthTest,
	"PorismExtension.Layout.Profile.Validation.RejectsEvenContinuationCorridorWidth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationAcceptsHigherContinuationEntryLevelWithoutUpperBoundaryOnlyFillTest,
	"PorismExtension.Layout.Profile.Validation.AcceptsHigherContinuationEntryLevelWithoutUpperBoundaryOnlyFill",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileValidationRejectsInvalidTypedSparseRulesTest,
	"PorismExtension.Layout.Profile.Validation.RejectsInvalidTypedSparseRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileValidationRejectsZoneFeatureRequirementWithoutContentSetTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ZoneFeaturesNeedContentSet"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		1,
		0,
		false);

	FLayoutZoneFeatureRequirement& Requirement = Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("NeedsContentSet");
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MinCount = 1;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when zone feature requirements exist without a content set"), Validation.IsValid());
	TestTrue(TEXT("Validation should report that zone feature requirements need a content set"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("Zone feature requirements require a content set"));
	}));
	return true;
}

bool FLayoutProfileValidationRejectsNonLayoutZoneFeatureRequirementTagTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_NonLayoutZoneFeatureTag"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		1,
		0,
		false);

	FLayoutZoneFeatureRequirement& Requirement = Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("NonLayoutTag");
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::TraversalPrimary);
	Requirement.MinCount = 1;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when a zone feature requirement uses a non-layout tag"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the non-layout feature tag"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("uses a non-layout feature tag"));
	}));
	return true;
}

bool FLayoutProfileValidationRejectsInvalidReservedOpenRuleTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_InvalidReservedOpenRule"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);

	FLayoutReservedOpenSpaceRule& Rule = Profile->ReservedOpenSpaceRules.AddDefaulted_GetRef();
	Rule.RuleId = TEXT("ReservedOpenInvalid");
	Rule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	Rule.SpecificLevel = 3;
	Rule.ReservedPercent = 125.0f;
	Rule.MinReservedCells = 3;
	Rule.MaxReservedCells = 1;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when a reserved-open rule uses invalid authored values"), Validation.IsValid());
	TestTrue(TEXT("Validation should report the invalid reserved-open specific level"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("ReservedOpenSpaceRules[0]"))
			&& Message.Message.Contains(TEXT("SpecificLevel"));
	}));
	TestTrue(TEXT("Validation should report the invalid reserved-open percentage"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("ReservedPercent"));
	}));
	TestTrue(TEXT("Validation should report the invalid reserved-open min/max cell range"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("MinReservedCells"))
			&& Message.Message.Contains(TEXT("MaxReservedCells"));
	}));
	return true;
}


bool FLayoutProfileValidationWarnsForDisabledTerrainSeamFaceRulesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_DisabledTerrainSeamsWarn"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bEnableTerrainSeams = false;

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_DisabledTerrainSeamsWarn"), FIntVector(8, 8, 8));
	TArray<FLayoutFaceRule> Faces = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	Faces[0].BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam;
	Faces[0].OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutModule_DisabledTerrainSeamsWarn"),
		Template,
		{ELayoutCellIntent::Interior},
		Faces);
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("TerrainSeamModule");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("LayoutContentSet_DisabledTerrainSeamsWarn"), {Entry});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestTrue(TEXT("Disabled terrain seams produce warning without invalidating profile when other requirements are satisfied"), Validation.IsValid());
	TestEqual(TEXT("One disabled terrain seam module warning is emitted"), Validation.Messages.FilterByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("MustFaceTerrainSeam"));
	}).Num(), 1);
	return true;
}

bool FLayoutProfileValidationDeduplicatesDisabledTerrainSeamCompositeLeavesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_DisabledTerrainSeamsComposite"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bEnableTerrainSeams = false;
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_DisabledTerrainSeamsComposite"), FIntVector(8, 8, 8));
	TArray<FLayoutFaceRule> Faces = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	Faces[0].BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam;
	Faces[0].OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	ULayoutModuleAsset* Module = CreateModule(Outer, TEXT("LayoutModule_DisabledTerrainSeamsComposite"), Template, {ELayoutCellIntent::Interior}, Faces);
	ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_DisabledTerrainSeams"));
	FLayoutCompositeModuleCell& FirstCell = Composite->Cells.AddDefaulted_GetRef();
	FirstCell.Module = Module;
	FirstCell.LocalCell = FIntVector::ZeroValue;
	FLayoutCompositeModuleCell& SecondCell = Composite->Cells.AddDefaulted_GetRef();
	SecondCell.Module = Module;
	SecondCell.LocalCell = FIntVector(1, 0, 0);
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("TerrainSeamComposite");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = Composite;
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("LayoutContentSet_DisabledTerrainSeamsComposite"), {Entry});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestEqual(TEXT("Repeated composite leaf emits one disabled terrain seam warning"), Validation.Messages.FilterByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("MustFaceTerrainSeam"));
	}).Num(), 1);
	return true;
}

bool FLayoutProfileValidationAcceptsSingleLevelSteppedTerrainCapabilityWithJoiningContentTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_SingleLevelSteppedCapability"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_SingleLevelSteppedJoining"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* JoiningModule = CreateModule(
		Outer,
		TEXT("LayoutModule_SingleLevelSteppedJoining"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("JoiningContent");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = JoiningModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_SingleLevelSteppedJoining"),
		{ModuleEntry});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestTrue(TEXT("Profile validation should accept single-level stepped terrain when module content can fill pseudo joining cells"), Validation.IsValid());
	TestFalse(TEXT("Validation should not report the removed multi-level-only stepped requirement"), ContainsValidationMessageSubstring(
		Validation.Messages,
		TEXT("Stepped-terrain-capable profiles must be multi-level")));
	return true;
}

bool FLayoutProfileValidationWarnsOnlyForSingleLevelProfilesWithoutSteppedTerrainSupportTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_VerticalAccessWarningDependsOnSteppedSupport"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		1,
		0,
		false);
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;

	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutTemplate_VerticalAccessWarningDependsOnSteppedSupport"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer,
		TEXT("LayoutModule_VerticalAccessWarningDependsOnSteppedSupport"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("VerticalAccess");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = VerticalAccessModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_VerticalAccessWarningDependsOnSteppedSupport"),
		{ModuleEntry});

	const FLayoutValidationResult NonSteppedValidation = Profile->ValidateProfile();
	TestTrue(TEXT("Single-level non-stepped profile with reserved VerticalAccess remains valid"), NonSteppedValidation.IsValid());
	TestTrue(TEXT("Single-level non-stepped profile emits the VerticalAccess reservation warning"), NonSteppedValidation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("Vertical access cells are reserved for a single-level profile"));
	}));

	Profile->bSupportsSteppedTerrainSolve = true;
	const FLayoutValidationResult SteppedValidation = Profile->ValidateProfile();
	TestTrue(TEXT("Single-level stepped profile with VerticalAccess content remains valid"), SteppedValidation.IsValid());
	TestFalse(TEXT("Single-level stepped profile does not emit the non-stepped VerticalAccess reservation warning"), SteppedValidation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("Vertical access cells are reserved for a single-level profile"));
	}));
	return true;
}

bool FLayoutProfileValidationSteppedTerrainVerticalAccessDependsOnTraversalRequirementsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_SteppedCapabilityNeedsVerticalAccess"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_SteppedCapabilityNoVerticalAccess"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* BoundaryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_SteppedCapabilityBoundaryOnly"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("BoundaryOnly");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = BoundaryModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_SteppedCapabilityNoVerticalAccess"),
		{ModuleEntry});

	const FLayoutValidationResult OptionalTraversalValidation = Profile->ValidateProfile();
	TestTrue(
		TEXT("Multi-level stepped terrain remains valid without VerticalAccess content when traversal reachability and vertical-access counts are disabled"),
		OptionalTraversalValidation.IsValid());
	TestFalse(
		TEXT("Optional traversal does not report missing VerticalAccess content"),
		ContainsValidationMessageSubstring(
			OptionalTraversalValidation.Messages,
			TEXT("Stepped-terrain-capable profiles require VerticalAccess-capable content when traversal or authored vertical-access counts require it")));

	Profile->bRequireAllTraversalChannelsReachable = true;
	const FLayoutValidationResult RequiredTraversalValidation = Profile->ValidateProfile();
	TestFalse(
		TEXT("Stepped terrain with required traversal still rejects missing VerticalAccess content"),
		RequiredTraversalValidation.IsValid());
	TestTrue(
		TEXT("Required traversal reports missing VerticalAccess content"),
		ContainsValidationMessageSubstring(
			RequiredTraversalValidation.Messages,
			TEXT("Stepped-terrain-capable profiles require VerticalAccess-capable content when traversal or authored vertical-access counts require it")));
	return true;
}

/** Confirms topology proof, not static profile capability, decides whether strict traversal needs ascent. */
bool FLayoutProfileValidationStrictMultiLevelAllowsTopologyResolvedVerticalAccessTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("StrictMultiLevelTopologyResolvedVerticalAccess"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bRequireAllTraversalChannelsReachable = true;

	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	TestTrue(TEXT("Strict multi-level None defers ascent demand to topology proof"), Profile->ValidateProfile().IsValid());

	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Range;
	Profile->MinVerticalAccessCount = 0;
	Profile->MaxVerticalAccessCount = 2;
	TestTrue(TEXT("Strict multi-level zero-minimum Range defers additional ascent to topology proof"), Profile->ValidateProfile().IsValid());

	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	TestTrue(TEXT("Strict multi-level positive Exact remains accepted"), Profile->ValidateProfile().IsValid());

	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Range;
	Profile->MinVerticalAccessCount = 1;
	Profile->MaxVerticalAccessCount = 2;
	TestTrue(TEXT("Strict multi-level positive Range remains accepted"), Profile->ValidateProfile().IsValid());

	Profile->bRequireAllTraversalChannelsReachable = false;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	TestTrue(TEXT("Optional multi-level None remains accepted"), Profile->ValidateProfile().IsValid());
	return true;
}

bool FLayoutProfileValidationAcceptsSteppedTerrainCapabilityWithVerticalAccessContentTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_SteppedCapabilityWithVerticalAccess"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_SteppedCapabilityVerticalAccess"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer,
		TEXT("LayoutModule_SteppedCapabilityVerticalAccess"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("VerticalAccess");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = VerticalAccessModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_SteppedCapabilityVerticalAccess"),
		{ModuleEntry});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestTrue(TEXT("Profile validation should accept stepped terrain capability when multi-level content includes VerticalAccess-capable modules"), Validation.IsValid());
	return true;
}

bool FLayoutProfileValidationRejectsEvenContinuationCorridorWidthTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_EvenContinuationWidth"),
		FIntPoint(2, 1),
		FIntPoint(2, 2),
		1,
		0,
		false);
	Profile->ContinuationEntryLevel = 0;
	Profile->bRequireAllTraversalChannelsReachable = true;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Continuation profile validation rejects an even corridor width"), Validation.IsValid());
	TestTrue(TEXT("Continuation profile validation names the odd-width requirement"),
		ContainsValidationMessageSubstring(Validation.Messages, TEXT("odd minimum-footprint corridor width")));
	return true;
}

bool FLayoutProfileValidationRejectsContinuationEntryLevelWithoutContentSetTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ContinuationEntryNeedsContentSet"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);
	Profile->ContinuationEntryLevel = 1;
	Profile->bRequireAllTraversalChannelsReachable = true;

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when an explicit continuation entry level is authored without a content set"), Validation.IsValid());
	TestTrue(TEXT("Validation should report that continuation entry level needs a content set"), ContainsValidationMessageSubstring(
		Validation.Messages,
		TEXT("explicit continuation-entry level require a content set")));
	return true;
}

bool FLayoutProfileValidationRejectsContinuationEntryLevelWithoutEntryContentTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_ContinuationEntryNeedsEntryContent"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);
	Profile->ContinuationEntryLevel = 1;
	Profile->bRequireAllTraversalChannelsReachable = true;

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_ContinuationEntryNoEntry"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* BoundaryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_ContinuationEntryBoundaryOnly"),
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
	BoundaryEntry.EntryId = TEXT("ContinuationEntryBoundaryOnly");
	BoundaryEntry.ContentKind = ELayoutRegionContentKind::Module;
	BoundaryEntry.ModuleSettings.Module = BoundaryModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_ContinuationEntryNoEntry"),
		{BoundaryEntry});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when an explicit continuation entry level is authored without entry-capable content"), Validation.IsValid());
	TestTrue(TEXT("Validation should report that continuation entry level needs entry-capable content"), ContainsValidationMessageSubstring(
		Validation.Messages,
		TEXT("explicit continuation-entry level require Entry-capable content")));
	return true;
}

bool FLayoutProfileValidationRejectsContinuationEntryLevelWithoutReachabilityTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_ContinuationEntryNeedsReachability"),
		FIntPoint(4, 4),
		FIntPoint(4, 4),
		2,
		0,
		false);
	Profile->ContinuationEntryLevel = 1;

	ULayoutModuleAsset* EntryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_ContinuationEntryNeedsReachability"),
		CreateTemplate(Outer, TEXT("LayoutTemplate_ContinuationEntryNeedsReachability"), FIntVector(8, 8, 8)),
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeTags({LayoutGameplayTags::TraversalPrimary}));

	FLayoutRegionContentEntry EntryContent;
	EntryContent.EntryId = TEXT("ContinuationEntryReachabilityEntry");
	EntryContent.ContentKind = ELayoutRegionContentKind::Module;
	EntryContent.ModuleSettings.Module = EntryModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_ContinuationEntryNeedsReachability"),
		{EntryContent});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Profile validation should fail when continuation entry level lacks required traversal reachability"), Validation.IsValid());
	TestTrue(TEXT("Validation should report continuation reachability requirement"), ContainsValidationMessageSubstring(
		Validation.Messages,
		TEXT("explicit continuation-entry level require all traversal channels to be reachable")));
	return true;
}

bool FLayoutProfileValidationAcceptsHigherContinuationEntryLevelWithoutUpperBoundaryOnlyFillTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_HigherContinuationEntryNeedsUpperBoundaryOnly"),
		FIntPoint(5, 3),
		FIntPoint(5, 3),
		2,
		2,
		false);
	Profile->ContinuationEntryLevel = 1;
	Profile->bRequireAllTraversalChannelsReachable = true;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;

	ULayoutModuleAsset* EntryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_HigherContinuationEntryNeedsUpperBoundaryOnly"),
		CreateTemplate(Outer, TEXT("LayoutTemplate_HigherContinuationEntryNeedsUpperBoundaryOnly"), FIntVector(8, 8, 8)),
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeTags({LayoutGameplayTags::TraversalPrimary}));

	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer,
		TEXT("LayoutModule_HigherContinuationVerticalAccess"),
		CreateTemplate(Outer, TEXT("LayoutTemplate_HigherContinuationVerticalAccess"), FIntVector(8, 8, 8)),
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})),
		MakeTags({LayoutGameplayTags::TraversalPrimary}));

	FLayoutRegionContentEntry EntryContent;
	EntryContent.EntryId = TEXT("HigherContinuationEntryEntry");
	EntryContent.ContentKind = ELayoutRegionContentKind::Module;
	EntryContent.ModuleSettings.Module = EntryModule;
	FLayoutRegionContentEntry VerticalAccessContent;
	VerticalAccessContent.EntryId = TEXT("HigherContinuationVerticalAccess");
	VerticalAccessContent.ContentKind = ELayoutRegionContentKind::Module;
	VerticalAccessContent.ModuleSettings.Module = VerticalAccessModule;
	Profile->ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_HigherContinuationEntryNeedsUpperBoundaryOnly"),
		{EntryContent, VerticalAccessContent});

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	if (!Validation.IsValid())
	{
		for (const FLayoutValidationMessage& Message : Validation.Messages)
		{
			AddInfo(Message.Message);
		}
	}
	TestTrue(TEXT("Higher continuation entry levels no longer require upper boundary-only fill at profile validation time"), Validation.IsValid());
	TestTrue(TEXT("Validation no longer reports the old upper boundary-only requirement"), Validation.Messages.FilterByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("upper boundary-only fill"));
	}).IsEmpty());
	return true;
}

bool FLayoutProfileValidationRejectsInvalidTypedSparseRulesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_InvalidTypedSparseRules"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	Profile->SparsePlacementRules.AddDefaulted();

	FLayoutSparseExactPlacementRule ExactRule;
	ExactRule.RuleId = TEXT("DuplicateRule");
	ExactRule.ContentSet = Profile->ContentSet;
	ExactRule.Count = 0;
	Profile->SparsePlacementRules.Add(FInstancedStruct::Make(ExactRule));

	FLayoutSparsePreserveTerrainRule PreserveRule;
	PreserveRule.RuleId = TEXT("DuplicateRule");
	PreserveRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	PreserveRule.SpecificLevel = 3;
	Profile->SparsePlacementRules.Add(FInstancedStruct::Make(PreserveRule));

	FLayoutSparsePreserveTerrainRule ShadowedRule;
	ShadowedRule.RuleId = TEXT("ShadowedRule");
	Profile->SparsePlacementRules.Add(FInstancedStruct::Make(ShadowedRule));

	const FLayoutValidationResult Validation = Profile->ValidateProfile();
	TestFalse(TEXT("Invalid typed sparse rules reject profile validation"), Validation.IsValid());
	TestTrue(TEXT("Empty instanced rule type is reported"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("supported non-empty sparse rule type"));
	}));
	TestTrue(TEXT("Exact count error is reported"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("Count must be greater than zero"));
	}));
	TestTrue(TEXT("Duplicate typed rule id is reported"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("duplicates RuleId 'DuplicateRule'"));
	}));
	TestTrue(TEXT("Typed rule authored level error is reported"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("SpecificLevel must be between 0 and 0"));
	}));
	TestTrue(TEXT("Fully shadowed preserve scope is reported"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("fully shadowed"));
	}));
	return true;
}
