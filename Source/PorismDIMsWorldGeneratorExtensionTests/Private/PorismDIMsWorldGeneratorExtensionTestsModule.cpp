// Copyright 2026 Spotted Loaf Studio

#include "Modules/ModuleManager.h"

/**
 * Editor-only automation test host module for the extension plugin.
 */
class FPorismDIMsWorldGeneratorExtensionTestsModule final : public IModuleInterface
{
};

IMPLEMENT_MODULE(FPorismDIMsWorldGeneratorExtensionTestsModule, PorismDIMsWorldGeneratorExtensionTests)
