// Copyright 2026 Spotted Loaf Studio

#include "Layout/Editor/LayoutDirectRootGenerationEdMode.h"

#include "Layout/Editor/LayoutDirectRootGenerationToolkit.h"
#include "Layout/Editor/LayoutDirectRootGenerationController.h"
#include "Modules/ModuleManager.h"
#include "PorismDIMsWorldGeneratorExtensionEditor.h"

const FEditorModeID FLayoutDirectRootGenerationEdMode::ModeId = TEXT("EM_PorismLayoutDirectRootGeneration");

FLayoutDirectRootGenerationEdMode::FLayoutDirectRootGenerationEdMode()
{
}

void FLayoutDirectRootGenerationEdMode::RegisterModules()
{
}

TSharedPtr<FModeToolkit> FLayoutDirectRootGenerationEdMode::CreateToolkitInstance()
{
	return MakeShareable(new FLayoutDirectRootGenerationToolkit());
}

void FLayoutDirectRootGenerationEdMode::Enter()
{
	FChunkWorldEdModeBase::Enter();
	GetController().SetToolActive(true);
}

void FLayoutDirectRootGenerationEdMode::Exit()
{
	GetController().SetToolActive(false);
	FChunkWorldEdModeBase::Exit();
}

void FLayoutDirectRootGenerationEdMode::Tick(FEditorViewportClient* const ViewportClient, const float DeltaTime)
{
	FChunkWorldEdModeBase::Tick(ViewportClient, DeltaTime);
	if (ViewportClient != nullptr && ViewportClient == GCurrentLevelEditingViewportClient)
	{
		GetController().UpdateHoverFromViewport(ViewportClient, ViewportClient->Viewport);
	}
}

bool FLayoutDirectRootGenerationEdMode::MouseMove(
	FEditorViewportClient* const ViewportClient,
	FViewport* const Viewport,
	const int32 x,
	const int32 y)
{
	FChunkWorldEdModeBase::MouseMove(ViewportClient, Viewport, x, y);
	return GetController().UpdateHoverFromViewport(ViewportClient, Viewport);
}

bool FLayoutDirectRootGenerationEdMode::InputKey(
	FEditorViewportClient* const ViewportClient,
	FViewport* const Viewport,
	const FKey Key,
	const EInputEvent Event)
{
	if (ViewportClient == GCurrentLevelEditingViewportClient
		&& Key == EKeys::LeftMouseButton
		&& Event == IE_Pressed
		&& Viewport != nullptr
		&& Viewport->KeyState(EKeys::LeftControl))
	{
		(void)GetController().UpdateHoverFromViewport(ViewportClient, Viewport);
		(void)GetController().HandlePrimaryActionAtHoveredLocation();
		return true;
	}
	if (ViewportClient == GCurrentLevelEditingViewportClient
		&& Key == EKeys::Escape
		&& Event == IE_Pressed)
	{
		GetController().ClearCachedSolve();
		return true;
	}

	return FChunkWorldEdModeBase::InputKey(ViewportClient, Viewport, Key, Event);
}

bool FLayoutDirectRootGenerationEdMode::HandlePreviewShortcutForTesting(
	const FKey Key,
	const EInputEvent Event,
	const bool bCtrlPressed)
{
	if (Key == EKeys::LeftMouseButton
		&& Event == IE_Pressed
		&& bCtrlPressed)
	{
		(void)GetController().HandlePrimaryActionAtHoveredLocation();
		return true;
	}

	return false;
}

void FLayoutDirectRootGenerationEdMode::Render(const FSceneView* const View, FViewport* const Viewport, FPrimitiveDrawInterface* const PDI)
{
	FChunkWorldEdModeBase::Render(View, Viewport, PDI);
	GetController().Render(View, PDI);
}

bool FLayoutDirectRootGenerationEdMode::IsCompatibleWith(const FEditorModeID OtherModeID) const
{
	return OtherModeID != NAME_None;
}

FLayoutDirectRootGenerationController& FLayoutDirectRootGenerationEdMode::GetController() const
{
	return FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor")
		.GetLayoutDirectRootGenerationController();
}
