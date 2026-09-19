// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationNormalizesNamedFaceRuleDirectionsTest,
	"PorismExtension.Layout.Module.Validation.NormalizesNamedFaceRuleDirections",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationNormalizesNamedFaceRuleDirectionsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_NormalizesFaceDirections"));
	Module->FaceRules.PosX.Direction = ELayoutFaceDirection::NegZ;
	Module->FaceRules.NegX.Direction = ELayoutFaceDirection::PosZ;
	Module->FaceRules.PosY.Direction = ELayoutFaceDirection::NegX;
	Module->FaceRules.NegY.Direction = ELayoutFaceDirection::PosX;
	Module->FaceRules.PosZ.Direction = ELayoutFaceDirection::NegY;
	Module->FaceRules.NegZ.Direction = ELayoutFaceDirection::PosY;

	Module->NormalizeFaceRuleDirections();

	TestEqual(TEXT("PosX slot normalizes to PosX"), Module->FaceRules.PosX.Direction, ELayoutFaceDirection::PosX);
	TestEqual(TEXT("NegX slot normalizes to NegX"), Module->FaceRules.NegX.Direction, ELayoutFaceDirection::NegX);
	TestEqual(TEXT("PosY slot normalizes to PosY"), Module->FaceRules.PosY.Direction, ELayoutFaceDirection::PosY);
	TestEqual(TEXT("NegY slot normalizes to NegY"), Module->FaceRules.NegY.Direction, ELayoutFaceDirection::NegY);
	TestEqual(TEXT("PosZ slot normalizes to PosZ"), Module->FaceRules.PosZ.Direction, ELayoutFaceDirection::PosZ);
	TestEqual(TEXT("NegZ slot normalizes to NegZ"), Module->FaceRules.NegZ.Direction, ELayoutFaceDirection::NegZ);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationRejectsInvalidTemplateDimensionsTest,
	"PorismExtension.Layout.Module.Validation.RejectsInvalidTemplateDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationRejectsInvalidTemplateDimensionsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_InvalidDimensions"), FIntVector::ZeroValue);
	TestNotNull(TEXT("Template is created"), Template);

	TArray<FLayoutFaceRule> FaceRules = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);

	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_InvalidDimensions"),
		Template,
		{ELayoutCellIntent::Boundary},
		FaceRules);
	Template->SizeInBlocks = FIntVector::ZeroValue;

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestFalse(TEXT("Module with invalid template dimensions is rejected"), ValidationResult.IsValid());

	bool bFoundInvalidDimensionMessage = false;
	for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
	{
		if (Message.Message.Contains(TEXT("LayoutModule_InvalidDimensions"))
			&& Message.Message.Contains(TEXT("invalid template dimensions")))
		{
			bFoundInvalidDimensionMessage = true;
			break;
		}
	}

	TestTrue(TEXT("Validation reports the invalid template dimensions"), bFoundInvalidDimensionMessage);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationUsesSingleOccupiedCellContractTest,
	"PorismExtension.Layout.Module.Validation.UsesSingleOccupiedCellContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationUsesSingleOccupiedCellContractTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_SingleOccupiedCell"), FIntVector(16, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_SingleOccupiedCell"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Leaf modules remain valid when the template is non-square"), ValidationResult.IsValid());
	TestEqual(TEXT("Leaf module effective cell size derives directly from the template size"), Module->GetEffectiveCellSizeInBlocks(), FIntVector(16, 8, 8));
	TestEqual(TEXT("Leaf module effective template dimensions derive directly from the template size"), Module->GetEffectiveTemplateDimensionsBlocks(), FIntVector(16, 8, 8));
	const TArray<FIntVector> OccupiedCells = Module->GetOccupiedLocalCells();
	TestEqual(TEXT("Leaf modules always expose exactly one occupied local cell"), OccupiedCells.Num(), 1);
	if (OccupiedCells.Num() == 1)
	{
		TestEqual(TEXT("Leaf module occupied local cell stays at the origin"), OccupiedCells[0], FIntVector::ZeroValue);
	}
	TestEqual(TEXT("Leaf module occupied bounds stay one cell regardless of template size"), Module->GetOccupiedBoundsCells(), FIntVector(1, 1, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationSupportsConnectionTagDuringFaceMigrationTest,
	"PorismExtension.Layout.Module.Validation.SupportsConnectionTagDuringFaceMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationSupportsConnectionTagDuringFaceMigrationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_ConnectionTag"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_ConnectionTag"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
	});
	Module->Roles = {ELayoutModuleRole::Boundary};

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	if (!ValidationResult.IsValid())
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			AddInfo(Message.Message);
		}
	}

	TestTrue(TEXT("New connection tag fields satisfy face validation"), ValidationResult.IsValid());
	for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
	{
		TestFalse(TEXT("New connection-only authoring does not emit connection-tag warnings"), Message.Message.Contains(TEXT("Connection Tag")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationAllowsAnyNeighborWithoutAllowedTagsTest,
	"PorismExtension.Layout.Module.Validation.AllowsAnyNeighborWithoutAllowedTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationAllowsAnyNeighborWithoutAllowedTagsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_AllowsAnyWithoutTags"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_AllowsAnyWithoutTags"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	if (!ValidationResult.IsValid())
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			AddInfo(Message.Message);
		}
	}

	TestTrue(TEXT("AllowsAnyNeighbor does not require allowed-neighbor tags"), ValidationResult.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationWarnsForEntryConnectionWithoutTraversalChannelTest,
	"PorismExtension.Layout.Module.Validation.WarnsForEntryConnectionWithoutTraversalChannel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationWarnsForEntryConnectionWithoutTraversalChannelTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_EntryNoTraversalChannel"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_EntryNoTraversalChannel"),
		Template,
		{ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
	});
	Module->Roles = {ELayoutModuleRole::Entry};

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Entry connection without traversal channels is a migration warning, not a hard failure"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation warns that entry connection tags need Connected Traversal Channels"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("Entry"))
			&& (Message.Message.Contains(TEXT("traversal")) || Message.Message.Contains(TEXT("Connected Traversal Channels")));
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationAcceptsBridgedExteriorEntryRootTest,
	"PorismExtension.Layout.Module.Validation.AcceptsBridgedExteriorEntryRoot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationAcceptsBridgedExteriorEntryRootTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_BridgedExteriorEntryRoot"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_BridgedExteriorEntryRoot"),
		Template,
		{ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, GroundWalkable),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});
	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Bridged exterior entry root remains valid"), ValidationResult.IsValid());
	TestFalse(TEXT("Bridged exterior entry root avoids the missing external-root warning"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("explicit external root"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationRequiresVerticalAccessRoleForTraversablePosZTest,
	"PorismExtension.Layout.Module.Validation.RequiresVerticalAccessRoleForTraversablePosZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationRequiresVerticalAccessRoleForTraversablePosZTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_PosZTraversalNeedsVerticalAccess"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_PosZTraversalNeedsVerticalAccess"),
		Template,
		{},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		},
		MakeTags({LayoutGameplayTags::TraversalPrimary}));
	Module->Roles = {ELayoutModuleRole::Interior};

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestFalse(TEXT("Traversable PosZ without VerticalAccess role is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports missing VerticalAccess role"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("VerticalAccess"))
			&& (Message.Message.Contains(TEXT("vertical traversal")) || Message.Message.Contains(TEXT("PosZ")));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationRejectsVerticalAccessRoleWithoutTraversablePosZTest,
	"PorismExtension.Layout.Module.Validation.RejectsVerticalAccessRoleWithoutTraversablePosZ",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationRejectsVerticalAccessRoleWithoutTraversablePosZTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_VerticalAccessNeedsPosZTraversal"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_VerticalAccessNeedsPosZTraversal"),
		Template,
		{},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	Module->Roles = {ELayoutModuleRole::VerticalAccess};

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestFalse(TEXT("VerticalAccess role without traversable PosZ is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports non-traversable PosZ"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("VerticalAccess"))
			&& (Message.Message.Contains(TEXT("traversable top face")) || Message.Message.Contains(TEXT("upward traversal on PosZ")));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationAcceptsTraversablePosZForVerticalAccessRoleTest,
	"PorismExtension.Layout.Module.Validation.AcceptsTraversablePosZForVerticalAccessRole",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationAcceptsTraversablePosZForVerticalAccessRoleTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	const FGameplayTagContainer GroundTraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_TraversablePosZ"), FIntVector(8, 8, 8));
	const FLayoutFaceRule PosZ = MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, GroundTraversalChannels);
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_ExplicitTraversablePosZ"),
		Template,
		{},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
			PosZ,
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor)
		},
		GroundTraversalChannels);
	Module->Roles = {ELayoutModuleRole::VerticalAccess};

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	if (!ValidationResult.IsValid())
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			AddInfo(Message.Message);
		}
	}

	TestTrue(TEXT("Connected traversal channels on PosZ satisfy VerticalAccess role validation"), ValidationResult.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationWarnsWhenMinWalkableFacesExceedsAvailableFacesTest,
	"PorismExtension.Layout.Module.Validation.WarnsWhenMinWalkableFacesExceedsAvailableFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationWarnsWhenMinWalkableFacesExceedsAvailableFacesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_MinWalkableWarns"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_MinWalkableWarns"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		MakeTags({LayoutGameplayTags::TraversalPrimary}));
	Module->MinWalkableFaces = 2;

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("MinWalkableFaces above available traversable faces is capped, not rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports the effective MinWalkableFaces cap"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("MinWalkableFaces"))
			&& (Message.Message.Contains(TEXT("clamp")) || Message.Message.Contains(TEXT("cap")));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationExpandsHorizontalFaceSymmetryTest,
	"PorismExtension.Layout.Module.Validation.ExpandsHorizontalFaceSymmetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationExpandsHorizontalFaceSymmetryTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_RadialSymmetry"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_RadialSymmetry"),
		Template,
		{ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		});
	Module->FaceSymmetryMode = ELayoutFaceSymmetryMode::RadialHorizontalFromPosX;

	const FLayoutModuleFaceRules EffectiveRules = Module->GetEffectiveFaceRules();
	const FLayoutFaceRule* NegY = EffectiveRules.FindRule(ELayoutFaceDirection::NegY);
	TestNotNull(TEXT("NegY effective face exists"), NegY);
	if (NegY != nullptr)
	{
		TestTrue(TEXT("NegY inherits the PosX connection tag"), NegY->ConnectionTag.MatchesTagExact(LayoutGameplayTags::FaceSolid));
		TestEqual(TEXT("NegY keeps its target direction after inference"), NegY->Direction, ELayoutFaceDirection::NegY);
	}

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Radial symmetry can validate with inferred horizontal faces"), ValidationResult.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationSquareModulesIgnoreAuthoredYawRestrictionsTest,
	"PorismExtension.Layout.Module.Validation.SquareModulesIgnoreAuthoredYawRestrictions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationNonSquareTemplatesStillRotateFreelyTest,
	"PorismExtension.Layout.Module.Validation.NonSquareTemplatesStillRotateFreely",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationLeafModulesNoLongerEmitLegacyLeafShapeWarningsTest,
	"PorismExtension.Layout.Module.Validation.LeafModulesNoLongerEmitLegacyLeafShapeWarnings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationLeafModulesUseTemplateDimensionsDirectlyTest,
	"PorismExtension.Layout.Module.Validation.LeafModulesUseTemplateDimensionsDirectly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationWarnsHorizontalMatchingYawAuthoringDeprecatedTest,
	"PorismExtension.Layout.Module.Validation.WarnsHorizontalMatchingYawAuthoringDeprecated",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationUsesOneCellOccupiedContractForTemplateDimensionsTest,
	"PorismExtension.Layout.Module.Validation.UsesOneCellOccupiedContractForTemplateDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationSquareModulesIgnoreAuthoredYawRestrictionsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CustomYaw"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CustomYaw"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	const TArray<int32> Steps = Module->GetEffectiveYawRotationSteps();
	TestEqual(TEXT("Square-cell modules always expose all four yaw rotations"), Steps.Num(), 4);
	TestEqual(TEXT("First yaw step is authored orientation"), Steps[0], 0);
	TestEqual(TEXT("Second yaw step is quarter turn"), Steps[1], 1);
	TestEqual(TEXT("Third yaw step is half turn"), Steps[2], 2);
	TestEqual(TEXT("Fourth yaw step is three-quarter turn"), Steps[3], 3);
	return true;
}

bool FLayoutModuleValidationNonSquareTemplatesStillRotateFreelyTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_NonSquareYaw"), FIntVector(16, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_NonSquareYaw"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	const TArray<int32> Steps = Module->GetEffectiveYawRotationSteps();
	TestEqual(TEXT("Non-square modules still expose all four effective yaw steps on the active path"), Steps.Num(), 4);
	TestEqual(TEXT("First yaw step remains the authored orientation"), Steps[0], 0);
	TestEqual(TEXT("Second yaw step is quarter turn"), Steps[1], 1);
	TestEqual(TEXT("Third yaw step is half turn"), Steps[2], 2);
	TestEqual(TEXT("Fourth yaw step is three-quarter turn"), Steps[3], 3);

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Non-square templates remain valid leaf modules"), ValidationResult.IsValid());
	TestFalse(TEXT("Leaf modules no longer warn about legacy non-square leaf metrics"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("legacy compatibility surface"));
	}));
	return true;
}

bool FLayoutModuleValidationLeafModulesNoLongerEmitLegacyLeafShapeWarningsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_NoLegacyWarnings"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_NoLegacyWarnings"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Standard leaf modules remain valid"), ValidationResult.IsValid());
	TestFalse(TEXT("Leaf validation no longer emits multi-cell or redundant-template legacy warnings"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("legacy compatibility surface"))
			|| Message.Message.Contains(TEXT("redundant compatibility surface"))
			|| Message.Message.Contains(TEXT("LayoutCompositeModuleAsset"));
	}));
	return true;
}

bool FLayoutModuleValidationLeafModulesUseTemplateDimensionsDirectlyTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_DirectTemplateDimensions"), FIntVector(12, 10, 6));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_DirectTemplateDimensions"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	SetModuleTemplateSize(Module, FIntVector(18, 14, 7));
	TestEqual(TEXT("Effective template dimensions update directly from the template"), Module->GetEffectiveTemplateDimensionsBlocks(), FIntVector(18, 14, 7));
	TestEqual(TEXT("Effective cell size matches the template dimensions"), Module->GetEffectiveCellSizeInBlocks(), FIntVector(18, 14, 7));
	return true;
}

bool FLayoutModuleValidationWarnsHorizontalMatchingYawAuthoringDeprecatedTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_HorizontalMatchingYaw"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_HorizontalMatchingYaw"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	Module->FaceRules.PosX.bRequireMatchingYawWithFilledNeighbor = true;

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Horizontal matching-yaw authoring remains a warning, not a hard failure"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation warns that horizontal matching-yaw authoring is deprecated"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Warning
			&& Message.Message.Contains(TEXT("Horizontal face PosX authors a deprecated matching-yaw requirement"));
	}));
	return true;
}

bool FLayoutModuleValidationUsesOneCellOccupiedContractForTemplateDimensionsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_OneCellOccupiedContract"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_OneCellOccupiedContract"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("One-cell occupied contract validates cleanly"), ValidationResult.IsValid());
	TestFalse(TEXT("Validation no longer reports any stale raw-bounds mismatch"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("bounds"))
			|| Message.Message.Contains(TEXT("footprint"));
	}));
	TestEqual(TEXT("Occupied bounds stay one cell"), Module->GetOccupiedBoundsCells(), FIntVector(1, 1, 1));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutModuleValidationAllowsSymmetricEntryModulesTest,
	"PorismExtension.Layout.Module.Validation.AllowsSymmetricEntryModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutModuleValidationAllowsSymmetricEntryModulesTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	const FGameplayTagContainer GroundWalkable = MakeTags({LayoutGameplayTags::TraversalPrimary});
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_SymmetricEntry"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_SymmetricEntry"),
		Template,
		{ELayoutCellIntent::Entry, ELayoutCellIntent::Boundary},
		{
			MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor, GroundWalkable),
			MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceOpen}), FGameplayTagContainer(), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
		},
		GroundWalkable);
	Module->FaceSymmetryMode = ELayoutFaceSymmetryMode::RadialHorizontalFromPosX;

	const FLayoutValidationResult ValidationResult = Module->ValidateModule();
	TestTrue(TEXT("Symmetric modules may support Entry when they expose an explicit exterior entry root"), ValidationResult.IsValid());
	TestFalse(TEXT("Symmetric entry modules do not emit a broad symmetry warning"), ValidationResult.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("Face symmetry is enabled"));
	}));
	return true;
}
