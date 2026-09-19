// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FLayoutDirectRootGenerationController;

/**
 * Editor module for biome strategy asset authoring helpers.
 */
class PORISMDIMSWORLDGENERATOREXTENSIONEDITOR_API FPorismDIMsWorldGeneratorExtensionEditorModule : public IModuleInterface
{
public:
	/** Registers editor-only detail customizations. */
	virtual void StartupModule() override;

	/** Unregisters editor-only detail customizations. */
	virtual void ShutdownModule() override;

	/** Returns the shared controller that powers the direct root-layout generation mode and toolkit. */
	FLayoutDirectRootGenerationController& GetLayoutDirectRootGenerationController();

	/** Shared controller used by the direct root-layout generation tab and editor mode. */
	TSharedPtr<FLayoutDirectRootGenerationController> LayoutDirectRootGenerationController;
};
