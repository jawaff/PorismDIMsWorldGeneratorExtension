// Copyright 2026 Spotted Loaf Studio

#include "LayoutProfileAssetDetails.h"

#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"
#include "Misc/Paths.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LayoutProfileAssetDetails"

TSharedRef<IDetailCustomization> FLayoutProfileAssetDetails::MakeInstance()
{
	return MakeShared<FLayoutProfileAssetDetails>();
}

void FLayoutProfileAssetDetails::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	TArray<TWeakObjectPtr<UObject>> CustomizedObjects;
	DetailBuilder.GetObjectsBeingCustomized(CustomizedObjects);
	for (const TWeakObjectPtr<UObject>& Object : CustomizedObjects)
	{
		if (ULayoutProfileAsset* Profile = Cast<ULayoutProfileAsset>(Object.Get()))
		{
			CustomizedProfile = Profile;
			break;
		}
	}

	if (!CustomizedProfile.IsValid())
	{
		return;
	}

	if (ExportPath.IsEmpty())
	{
		ExportPath = BuildDefaultExportPath();
	}

	IDetailCategoryBuilder& ValidationCategory = DetailBuilder.EditCategory(
		TEXT("Layout|Validation"),
		LOCTEXT("ValidationCategory", "Validation"));
	ValidationCategory.SetSortOrder(0);

	IDetailCategoryBuilder& MigrationCategory = DetailBuilder.EditCategory(
		TEXT("Layout|Migration"),
		LOCTEXT("ClosureMigrationCategory", "Closure Authoring"));
	MigrationCategory.SetSortOrder(1);

	MigrationCategory.AddCustomRow(LOCTEXT("ClosureMigrationSummarySearch", "Closure Migration"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClosureMigrationHelp", "Use explicit ClosureRequirements only for scenario-driving perimeter or sealed-shell profiles. SolvedFootprint follows the early committed footprint choice and uses InsetCells plus MinLevel/MaxLevel. ExplicitLocalBounds uses exact MinCells/MaxCells coordinates, and its Z range already defines the levels."))
				.AutoWrapText(true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &FLayoutProfileAssetDetails::GetClosureMigrationSummaryText)
				.AutoWrapText(true)
			]
		];

	MigrationCategory.AddCustomRow(LOCTEXT("ClosureMigrationGenerateSearch", "Generate Perimeter Closure Draft"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("GeneratePerimeterClosureDraftLabel", "Generate Perimeter Closure Draft"))
				.ToolTipText(LOCTEXT("GeneratePerimeterClosureDraftToolTip", "Creates or refreshes a hard OuterPerimeter closure requirement using SolvedFootprint mode, InsetCells=0, and levels 0 through LevelCount - 1. Validation remains part of the normal asset validation path."))
				.OnClicked(this, &FLayoutProfileAssetDetails::GeneratePerimeterClosureDraft)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("ClosureValidationNote", "Closure authoring checks run through the profile's normal asset validation. Use Validate Layout Profile or normal asset validation to confirm the contract is clean."))
				.AutoWrapText(true)
			]
		];

	IDetailCategoryBuilder& Category = DetailBuilder.EditCategory(
		TEXT("Layout|Testing"),
		LOCTEXT("JsonFixtureExportCategory", "JSON Fixture Export"));
	Category.SetSortOrder(2);

	Category.AddCustomRow(LOCTEXT("JsonFixturePathSearch", "Output Path"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("OutputPathLabel", "Output Path"))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(360.0f)
		[
			SNew(SEditableTextBox)
			.Text(FText::FromString(ExportPath))
			.ToolTipText(LOCTEXT("OutputPathToolTip", "Project-relative or absolute JSON fixture path. Relative paths are resolved from the project directory."))
			.OnTextCommitted(this, &FLayoutProfileAssetDetails::OnExportPathCommitted)
		];

	Category.AddCustomRow(LOCTEXT("JsonFixtureExportSearch", "Export JSON Fixture"))
		.WholeRowContent()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SButton)
				.Text(LOCTEXT("ExportButtonLabel", "Export JSON Fixture"))
				.ToolTipText(LOCTEXT("ExportButtonToolTip", "Exports this profile plus its preferred authored content source into one solver-focused JSON fixture file. If the profile references a unified content set, that path is used first. Otherwise the selected module set fallback is exported."))
				.OnClicked(this, &FLayoutProfileAssetDetails::ExportFixture)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(this, &FLayoutProfileAssetDetails::GetStatusText)
				.AutoWrapText(true)
			]
		];
}

FReply FLayoutProfileAssetDetails::GeneratePerimeterClosureDraft()
{
	ULayoutProfileAsset* Profile = CustomizedProfile.Get();
	if (Profile == nullptr)
	{
		LastStatus = TEXT("Migration failed: no layout profile is selected.");
		return FReply::Handled();
	}

	Profile->GeneratePerimeterClosureDraftInEditor();
	LastStatus = FString::Printf(
		TEXT("Generated or refreshed the OuterPerimeter closure draft for %s. Closure validation stays part of the profile's normal asset validation."),
		*Profile->GetName());
	return FReply::Handled();
}

FReply FLayoutProfileAssetDetails::ExportFixture()
{
	ULayoutProfileAsset* Profile = CustomizedProfile.Get();
	if (Profile == nullptr)
	{
		LastStatus = TEXT("Export failed: no layout profile is selected.");
		return FReply::Handled();
	}
	if (ExportPath.IsEmpty())
	{
		ExportPath = BuildDefaultExportPath();
	}

	const FString AbsolutePath = FPaths::IsRelative(ExportPath)
		? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), ExportPath)
		: FPaths::ConvertRelativePathToFull(ExportPath);

	FString Error;
	if (Profile->ContentSet != nullptr)
	{
		if (!FLayoutProfileJsonFixture::ExportToFile(Profile, Profile->ContentSet, AbsolutePath, Error))
		{
			LastStatus = FString::Printf(TEXT("Export failed: %s"), *Error);
			return FReply::Handled();
		}
	}

	LastStatus = FString::Printf(TEXT("Exported JSON fixture to %s"), *AbsolutePath);
	return FReply::Handled();
}

void FLayoutProfileAssetDetails::OnExportPathCommitted(const FText& NewText, ETextCommit::Type CommitType)
{
	ExportPath = NewText.ToString();
}

FText FLayoutProfileAssetDetails::GetStatusText() const
{
	return LastStatus.IsEmpty() ? FText::GetEmpty() : FText::FromString(LastStatus);
}

FText FLayoutProfileAssetDetails::GetClosureMigrationSummaryText() const
{
	const ULayoutProfileAsset* Profile = CustomizedProfile.Get();
	if (Profile == nullptr)
	{
		return LOCTEXT("ClosureSummaryNoProfile", "No layout profile is currently selected.");
	}

	if (Profile->ClosureRequirements.Num() == 0)
	{
		return LOCTEXT("ClosureSummaryMissing", "No explicit closure requirements are authored yet. This is fine for open/simple profiles, but perimeter and sealed-shell roots should author explicit closure requirements.");
	}

	const FLayoutClosureRequirement& FirstRequirement = Profile->ClosureRequirements[0];
	const FString ModeName = StaticEnum<ELayoutBoundsPolicyMode>()->GetNameStringByValue(static_cast<int64>(FirstRequirement.BoundsPolicy.Mode));
	const FString Summary = FString::Printf(
		TEXT("%d explicit closure requirement(s) authored. First closure '%s' uses %s mode. In SolvedFootprint mode, MinLevel/MaxLevel clamp the early committed footprint band. In ExplicitLocalBounds mode, MinCells.Z and MaxCells.Z already define the vertical extent."),
		Profile->ClosureRequirements.Num(),
		*FirstRequirement.ClosureId.ToString(),
		*ModeName);
	return FText::FromString(Summary);
}

FString FLayoutProfileAssetDetails::BuildDefaultExportPath() const
{
	const FString ProfileName = CustomizedProfile.IsValid() ? CustomizedProfile->GetName() : FString(TEXT("LayoutProfile"));
	const FString ContentSourceName = CustomizedProfile.IsValid() && CustomizedProfile->ContentSet != nullptr
		? CustomizedProfile->ContentSet->GetName()
		: FString(TEXT("ContentSource"));
	const FString FileName = FPaths::MakeValidFileName(FString::Printf(TEXT("%s_%s.json"), *ProfileName, *ContentSourceName));
	return FString::Printf(TEXT("BlueprintExports/LayoutFixtures/%s"), *FileName);
}

#undef LOCTEXT_NAMESPACE
