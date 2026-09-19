// Copyright 2026 Spotted Loaf Studio

#include "Layout/Editor/LayoutDirectRootGenerationToolkit.h"

#include "Editor.h"
#include "EditorModeManager.h"
#include "IDetailsView.h"
#include "Layout/Editor/LayoutDirectRootGenerationController.h"
#include "Layout/Editor/LayoutDirectRootGenerationEdMode.h"
#include "Modules/ModuleManager.h"
#include "PorismDIMsWorldGeneratorExtensionEditor.h"
#include "PropertyEditorModule.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LayoutGeneratorToolkit"

FLayoutDirectRootGenerationToolkit::~FLayoutDirectRootGenerationToolkit()
{
	if (ControllerStateChangedHandle.IsValid()
		&& FModuleManager::Get().IsModuleLoaded("PorismDIMsWorldGeneratorExtensionEditor"))
	{
		GetController().OnStateChanged().Remove(ControllerStateChangedHandle);
	}
}

FEdMode* FLayoutDirectRootGenerationToolkit::GetEditorMode() const
{
	return GLevelEditorModeTools().GetActiveMode(FLayoutDirectRootGenerationEdMode::ModeId);
}

TSharedRef<SWidget> FLayoutDirectRootGenerationToolkit::BuildToolHeader()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(5)
		[
			MakeSectionHeader(FText::FromString(TEXT("Layout Generator")), 14)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(5, 2)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("Root profile: Ctrl + Left Click terrain. Continuation profile: Ctrl + Left Click source exported entry, then target entry. Preview shows each bounded segment with root-equivalent debug markers and one route line. Generation Attempt lists segment failures. Apply Cached Solve stamps accepted segments.")))
			.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
			.AutoWrapText(true)
		];
}

TSharedRef<SWidget> FLayoutDirectRootGenerationToolkit::BuildToolSettings()
{
	if (!ControllerStateChangedHandle.IsValid())
	{
		ControllerStateChangedHandle = GetController().OnStateChanged().AddRaw(
			this,
			&FLayoutDirectRootGenerationToolkit::HandleControllerStateChanged);
	}

	if (!DetailsView.IsValid())
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		FDetailsViewArgs DetailsViewArgs;
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bHideSelectionTip = true;
		DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		DetailsViewArgs.bUpdatesFromSelection = false;
		DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
		DetailsView->SetObject(GetController().GetSettingsObject(), true);
	}

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			MakeCollapsibleSection(
				LOCTEXT("GenerationAttemptSection", "Generation Attempt"),
				SNew(SBox)
				.MinDesiredHeight(150.0f)
				[
					SAssignNew(GenerationSummaryTextBox, SMultiLineEditableTextBox)
					.Text(this, &FLayoutDirectRootGenerationToolkit::GetGenerationSummaryText)
					.IsReadOnly(true)
					.AutoWrapText(true)
				],
				false)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			MakeCollapsibleSection(
				LOCTEXT("ActionsSection", "Actions"),
				SNew(SWrapBox).UseAllottedSize(true)
				+ SWrapBox::Slot().Padding(0, 0, 8, 4)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyCachedSolveButton", "Apply Cached Solve"))
					.OnClicked_Lambda([this]()
					{
						(void)GetController().ApplyCachedSolve();
						return FReply::Handled();
					})
				]
				+ SWrapBox::Slot().Padding(0, 0, 8, 4)
				[
					SNew(SButton)
					.Text(LOCTEXT("ApplyInvalidPreviewButton", "Apply Invalid Preview"))
					.ToolTipText(LOCTEXT("ApplyInvalidPreviewTooltip", "Debug only: stamp retained failed placements without certifying the solve. Failure diagnostics remain. Missing placements are not invented; target, frozen-contract and write-safety checks still apply."))
					.OnClicked_Lambda([this]()
					{
						(void)GetController().ApplyCachedSolve(true);
						return FReply::Handled();
					})
				]
				+ SWrapBox::Slot().Padding(0, 0, 0, 4)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearCachedSolveButton", "Clear Cached Solve"))
					.OnClicked_Lambda([this]()
					{
						GetController().ClearCachedSolve();
						return FReply::Handled();
					})
				],
				true)
		]
		+ SVerticalBox::Slot().FillHeight(1.0f)
		[
			MakeCollapsibleSection(
				LOCTEXT("GenerationSettingsSection", "Generation Settings"),
				DetailsView.ToSharedRef(),
				false)
		];
}

TSharedRef<SWidget> FLayoutDirectRootGenerationToolkit::BuildToolControls()
{
	TArray<TPair<FString, FString>> Controls;
	Controls.Emplace(TEXT("Ctrl + Left Click"), TEXT("Root profile: preview hovered terrain. Continuation profile: select source then target exported entry; inspect each segment before Apply."));
	Controls.Emplace(TEXT("Apply Cached Solve"), TEXT("Stamp the most recent accepted cached result into the chunk world."));
	Controls.Emplace(TEXT("Clear Cached Solve"), TEXT("Discard the cached preview result and overlay state."));

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			MakeControlHintsSection(Controls)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 6)
		[
			MakeCollapsibleSection(
				LOCTEXT("OverlayLegendSection", "Overlay Legend"),
				SNew(SBox)
				.MinDesiredHeight(250.0f)
				[
					SNew(SMultiLineEditableTextBox)
					.Text(this, &FLayoutDirectRootGenerationToolkit::GetOverlayLegendText)
					.IsReadOnly(true)
					.AutoWrapText(true)
				],
				false)
		]
		+ SVerticalBox::Slot().AutoHeight()
		[
			MakeCollapsibleSection(
				LOCTEXT("InstructionSection", "Notes"),
				SNew(STextBlock)
				.Text(this, &FLayoutDirectRootGenerationToolkit::GetInstructionText)
				.AutoWrapText(true),
				true)
		];
}

FLayoutDirectRootGenerationController& FLayoutDirectRootGenerationToolkit::GetController() const
{
	return FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor")
		.GetLayoutDirectRootGenerationController();
}

FText FLayoutDirectRootGenerationToolkit::GetGenerationSummaryText() const
{
	return GetController().GetGenerationSummaryText();
}

void FLayoutDirectRootGenerationToolkit::HandleControllerStateChanged()
{
	if (GenerationSummaryTextBox.IsValid())
	{
		GenerationSummaryTextBox->Invalidate(EInvalidateWidgetReason::Layout);
	}
	if (GEditor != nullptr)
	{
		GEditor->RedrawLevelEditingViewports(false);
	}
}

FText FLayoutDirectRootGenerationToolkit::GetInstructionText() const
{
	return GetController().GetInstructionText();
}

FText FLayoutDirectRootGenerationToolkit::BuildOverlayLegendText()
{
	return FText::FromString(
		TEXT("How preview works\n")
		TEXT("  Root profile: Ctrl + Left Click runs one root preview.\n")
		TEXT("  Continuation profile: Ctrl + Left Click source exported entry, then compatible target entry.\n")
		TEXT("  Continuation preview keeps bounded segments. Root debug markers render for every segment with solve data.\n")
		TEXT("  Layout Generator draws editor overlays while you are previewing.\n")
		TEXT("  It does not stamp the layout or continuation into the chunk world until you use Apply Cached Solve.\n")
		TEXT("  The layer toggles in Generation Settings control which preview overlays are shown.\n")
		TEXT("\n")
		TEXT("Zone border colors\n")
		TEXT("  Green:      Perimeter zone.\n")
		TEXT("  Coral red:  Edge zone.\n")
		TEXT("  Purple:     Corner zone.\n")
		TEXT("  Blue:       Interior zone.\n")
		TEXT("  Orange:     Core zone.\n")
		TEXT("Intent sphere colors\n")
		TEXT("  Blue:       Interior.\n")
		TEXT("  Gold:       Boundary.\n")
		TEXT("  Teal:       Entry.\n")
		TEXT("  Orange:     Core.\n")
		TEXT("  Pink:       Connector.\n")
		TEXT("  Lime:       Vertical access.\n")
		TEXT("  Outer wire cube: base local zone before terrain retaining faces apply.\n")
		TEXT("  Inner wire cube: effective solver zone after terrain retaining faces apply.\n")
		TEXT("\n")
		TEXT("Other overlay colors\n")
		TEXT("  Teal face square:   terrain retaining seam face. A retaining wall module must face this planned neighbor.\n")
		TEXT("  Bright yellow pyramid: solver-classified OuterBoundary face on this cell. One pyramid per boundary face, pointing outward.\n")
		TEXT("  Red diagonal X:        unoccupied cell — no solved placement was committed here.\n")
		TEXT("  Yellow wire box:       solved root footprint bounds (encloses all cells).\n")
		TEXT("  Purple wire box:       child-region footprint bounds.\n")
		TEXT("  Teal wire box:         entry cell — small doorway-like box protruding from an outward boundary face.\n")
		TEXT("  Lime green wire box:   vertical access cell — box protruding from the ceiling face indicating level traversal.\n")
		TEXT("  Green sphere:          covered closure segment.\n")
		TEXT("  Red sphere:            uncovered closure segment.\n")
		TEXT("  Pink line/sphere:      owner side of a partition seam.\n")
		TEXT("  Blue-violet line/sphere: passive side of a partition seam.\n")
		TEXT("\n")
		TEXT("Continuation overlays\n")
		TEXT("  Pink route line:      discovered continuation centerline.\n")
		TEXT("  Yellow bounds:         fully solved segment.\n")
		TEXT("  Orange bounds/line:    retained partial segment.\n")
		TEXT("  Red bounds/line:       failed segment. Generation Attempt gives its reason.\n")
		TEXT("  Root debug markers above draw for every full or retained-partial segment. A failed segment has no solve artifact, so it draws only route diagnostics.\n")
		TEXT("\n")
		TEXT("Zone borders read final planned-cell topology and placement zones, not placed-module choices.\n"));
}

FText FLayoutDirectRootGenerationToolkit::GetOverlayLegendText() const
{
	return BuildOverlayLegendText();
}

FText FLayoutDirectRootGenerationToolkit::GetOverlayLegendTextForTesting()
{
	return BuildOverlayLegendText();
}

#undef LOCTEXT_NAMESPACE
