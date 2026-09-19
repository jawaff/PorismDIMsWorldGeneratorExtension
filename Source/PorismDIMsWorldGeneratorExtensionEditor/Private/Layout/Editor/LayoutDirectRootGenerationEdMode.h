// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorldTools/ChunkWorldEdModeBase.h"

class FLayoutDirectRootGenerationController;
class FModeToolkit;

/**
 * Editor mode for direct root-layout generation.
 * Uses the shared ChunkWorld toolkit pattern so the tool appears in the same
 * docked side-panel flow as the Porism template utility.
 */
class PORISMDIMSWORLDGENERATOREXTENSIONEDITOR_API FLayoutDirectRootGenerationEdMode : public FChunkWorldEdModeBase
{
public:
	static const FEditorModeID ModeId;

	FLayoutDirectRootGenerationEdMode();

	virtual void RegisterModules() override;
	virtual TSharedPtr<FModeToolkit> CreateToolkitInstance() override;
	virtual void Enter() override;
	virtual void Exit() override;
	virtual void Tick(FEditorViewportClient* ViewportClient, float DeltaTime) override;
	virtual bool MouseMove(FEditorViewportClient* ViewportClient, FViewport* Viewport, int32 x, int32 y) override;
	virtual bool InputKey(FEditorViewportClient* ViewportClient, FViewport* Viewport, FKey Key, EInputEvent Event) override;
	virtual void Render(const FSceneView* View, FViewport* Viewport, FPrimitiveDrawInterface* PDI) override;
	virtual bool IsCompatibleWith(FEditorModeID OtherModeID) const override;

	/** Headless shortcut seam that mirrors Ctrl + Left Click handling without requiring a live viewport. */
	bool HandlePreviewShortcutForTesting(FKey Key, EInputEvent Event, bool bCtrlPressed);

private:
	/** Returns the shared controller that owns all editor-facing direct-root generation state. */
	FLayoutDirectRootGenerationController& GetController() const;
};
