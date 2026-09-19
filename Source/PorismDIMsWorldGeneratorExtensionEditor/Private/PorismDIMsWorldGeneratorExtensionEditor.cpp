// Copyright 2026 Spotted Loaf Studio

#include "PorismDIMsWorldGeneratorExtensionEditor.h"

#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "BiomeStrategyDataDetails.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldExtendedDetails.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapScannerComponent.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "EditorModeManager.h"
#include "EditorModeRegistry.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Editor/LayoutDirectRootGenerationController.h"
#include "Layout/Editor/LayoutDirectRootGenerationEdMode.h"
#include "Layout/Types/LayoutTypes.h"
#include "LayoutFaceRuleCustomization.h"
#include "LayoutModuleAssetDetails.h"
#include "LayoutModuleFaceRulesCustomization.h"
#include "LayoutProfileAssetDetails.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "PorismDIMsWorldGeneratorExtensionEditor"

void FPorismDIMsWorldGeneratorExtensionEditorModule::StartupModule()
{
	LayoutDirectRootGenerationController = MakeShared<FLayoutDirectRootGenerationController>();

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
	PropertyEditorModule.RegisterCustomClassLayout(
		AChunkWorldExtended::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChunkWorldExtendedDetails::MakeInstance));
	PropertyEditorModule.RegisterCustomClassLayout(UBlockTypeSchemaComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChunkWorldComponentDetails::MakeInstance, LOCTEXT("SchemaComponent", "Block Schema")));
	PropertyEditorModule.RegisterCustomClassLayout(UChunkWorldBlockFeedbackComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChunkWorldComponentDetails::MakeInstance, LOCTEXT("FeedbackComponent", "Block Feedback")));
	PropertyEditorModule.RegisterCustomClassLayout(UChunkWorldBlockSwapComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChunkWorldComponentDetails::MakeInstance, LOCTEXT("SwapComponent", "Block Swap")));
	PropertyEditorModule.RegisterCustomClassLayout(UChunkWorldBlockSwapScannerComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChunkWorldComponentDetails::MakeInstance, LOCTEXT("ScannerComponent", "Block Swap Scanner")));
	PropertyEditorModule.RegisterCustomClassLayout(UChunkWorldLayoutRuntimeComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FChunkWorldComponentDetails::MakeInstance, LOCTEXT("LayoutComponent", "Layout Runtime")));
	PropertyEditorModule.RegisterCustomClassLayout(
		UBiomeStrategyData::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FBiomeStrategyDataDetails::MakeInstance));
	PropertyEditorModule.RegisterCustomClassLayout(
		ULayoutModuleAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FLayoutModuleAssetDetails::MakeInstance));
	PropertyEditorModule.RegisterCustomClassLayout(
		ULayoutProfileAsset::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FLayoutProfileAssetDetails::MakeInstance));
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FLayoutFaceRule::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLayoutFaceRuleCustomization::MakeInstance));
	PropertyEditorModule.RegisterCustomPropertyTypeLayout(
		FLayoutModuleFaceRules::StaticStruct()->GetFName(),
		FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLayoutModuleFaceRulesCustomization::MakeInstance));
	PropertyEditorModule.NotifyCustomizationModuleChanged();

	FEditorModeRegistry::Get().RegisterMode<FLayoutDirectRootGenerationEdMode>(
		FLayoutDirectRootGenerationEdMode::ModeId,
		LOCTEXT("LayoutGeneratorModeName", "Layout Generator"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"),
		true);
}

void FPorismDIMsWorldGeneratorExtensionEditorModule::ShutdownModule()
{
	// Avoid touching the global level-editor mode tools once engine shutdown has
	// started; constructing them during module unload triggers the packaged-object
	// fatal seen when closing the editor with this mode available.
	if (!IsEngineExitRequested() && GLevelEditorModeTools().IsModeActive(FLayoutDirectRootGenerationEdMode::ModeId))
	{
		GLevelEditorModeTools().DeactivateMode(FLayoutDirectRootGenerationEdMode::ModeId);
	}

	if (FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyEditorModule.UnregisterCustomClassLayout(AChunkWorldExtended::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(UBlockTypeSchemaComponent::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(UChunkWorldBlockFeedbackComponent::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(UChunkWorldBlockSwapComponent::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(UChunkWorldBlockSwapScannerComponent::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(UChunkWorldLayoutRuntimeComponent::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(UBiomeStrategyData::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(ULayoutModuleAsset::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomClassLayout(ULayoutProfileAsset::StaticClass()->GetFName());
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FLayoutFaceRule::StaticStruct()->GetFName());
		PropertyEditorModule.UnregisterCustomPropertyTypeLayout(FLayoutModuleFaceRules::StaticStruct()->GetFName());
		PropertyEditorModule.NotifyCustomizationModuleChanged();
	}

	FEditorModeRegistry::Get().UnregisterMode(FLayoutDirectRootGenerationEdMode::ModeId);
	LayoutDirectRootGenerationController.Reset();
}

FLayoutDirectRootGenerationController& FPorismDIMsWorldGeneratorExtensionEditorModule::GetLayoutDirectRootGenerationController()
{
	check(LayoutDirectRootGenerationController.IsValid());
	return *LayoutDirectRootGenerationController;
}

IMPLEMENT_MODULE(FPorismDIMsWorldGeneratorExtensionEditorModule, PorismDIMsWorldGeneratorExtensionEditor)

#undef LOCTEXT_NAMESPACE
