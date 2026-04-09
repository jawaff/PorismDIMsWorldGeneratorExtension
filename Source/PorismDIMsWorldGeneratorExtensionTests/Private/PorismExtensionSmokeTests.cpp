// Copyright 2026 Spotted Loaf Studio

#include "Misc/AutomationTest.h"

#include "Modules/ModuleManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPorismExtensionSmokeTest,
	"PorismExtension.Smoke.TestModuleLoads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FPorismExtensionSmokeTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("The runtime extension module is loaded for plugin-local automation tests."),
		FModuleManager::Get().IsModuleLoaded(TEXT("PorismDIMsWorldGeneratorExtension")));
	return true;
}
