// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

DECLARE_LOG_CATEGORY_EXTERN(LogPorismDIMsWorldGeneratorExtension, Log, All);

/**
 * Runtime module for project-specific extensions layered on top of PorismDIMsWorldGenerator.
 */
class FPorismDIMsWorldGeneratorExtensionModule : public IModuleInterface
{
public:
	/**
	 * Initializes extension hooks after the base Porism module is available.
	 */
	virtual void StartupModule() override;

	/**
	 * Releases extension hooks before shutdown.
	 */
	virtual void ShutdownModule() override;
};
