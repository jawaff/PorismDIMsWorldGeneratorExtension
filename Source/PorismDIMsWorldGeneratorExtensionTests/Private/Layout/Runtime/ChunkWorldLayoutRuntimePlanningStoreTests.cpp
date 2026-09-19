// Copyright 2026 Spotted Loaf Studio

#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldLayoutRuntimeComponentOwnsPlanningWindowStoreTest,
	"PorismExtension.Layout.Runtime.OwnsPlanningWindowStore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldLayoutRuntimeComponentOwnsPlanningWindowStoreTest::RunTest(const FString& Parameters)
{
	UChunkWorldLayoutRuntimeComponent* const RuntimeComponent =
		NewObject<UChunkWorldLayoutRuntimeComponent>(GetTransientPackage());

	ULayoutPlanningWindowStore* const FirstStore = RuntimeComponent->GetLayoutPlanningWindowStore();
	ULayoutPlanningWindowStore* const SecondStore = RuntimeComponent->GetLayoutPlanningWindowStore();

	TestNotNull(TEXT("Runtime component creates a planning-window store"), FirstStore);
	TestEqual(TEXT("Runtime component reuses the same planning-window store"), SecondStore, FirstStore);
	TestTrue(TEXT("Planning-window store is owned by the runtime component"), FirstStore->GetOuter() == RuntimeComponent);

	return true;
}
