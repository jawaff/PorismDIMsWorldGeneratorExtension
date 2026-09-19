// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapScannerComponent.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Misc/AutomationTest.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyPath.h"
#include "UObject/UnrealType.h"

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldDebugControlsTest,
	"PorismExtension.ChunkWorld.DebugControls.SharedState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldDebugControlsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutWorldTestUtilities;
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("World exists"), Harness.World)
		|| !TestNotNull(TEXT("Layout component exists"), Harness.RuntimeComponent)) return false;
	Harness.World->ShowDebugData = true;
	TestTrue(TEXT("Native stats switch also enables layout stats"), Harness.RuntimeComponent->GetDebugGenerationStats());
	Harness.RuntimeComponent->SetDebugGenerationStats(false);
	TestFalse(TEXT("Layout setter changes the shared stats switch"), Harness.World->ShowDebugData);
	UChunkWorldLayoutRuntimeComponent* Ownerless = NewObject<UChunkWorldLayoutRuntimeComponent>();
	Ownerless->SetDebugGenerationStats(true);
	TestFalse(TEXT("Ownerless component has no independent stats state"), Ownerless->GetDebugGenerationStats());
	const FBoolProperty* Detailed = FindFProperty<FBoolProperty>(Harness.World->GetClass(), TEXT("bDetailedDiagnostics"));
	if (!TestNotNull(TEXT("Detailed diagnostics property exists"), Detailed)) return false;
	TestFalse(TEXT("Detailed diagnostics defaults off"), Detailed->GetPropertyValue_InContainer(Harness.World));
	TestEqual(TEXT("Diagnostics has unified display name"), Detailed->GetMetaData(TEXT("DisplayName")), FString(TEXT("Detailed Diagnostics")));
	for (const TCHAR* Name : {TEXT("ShowDebugData"), TEXT("bDetailedDiagnostics")})
	{
		FResolvedLayoutSiteRecord Marker;
		Marker.RootSolveId = TEXT("DebugControls.Marker");
		Marker.SiteCenterBlockWorldPos = FIntVector(111, 222, 333);
		Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(111, 222), Marker);
		FPropertyChangedEvent Event(FindFProperty<FProperty>(Harness.World->GetClass(), Name), EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Harness.World)->PostEditChangeProperty(Event);
		TestTrue(TEXT("Diagnostics edit leaves terrain running"), Harness.World->IsRunning());
		TestEqual(TEXT("Diagnostics edit cannot restart/clear layout records"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 1);
	}
	// Details edits call PreEditChange before assigning the reflected value. Testing
	// PostEditChange alone misses actor-level component unregistration.
	for (const TCHAR* Name : {TEXT("ShowDebugData"), TEXT("bDetailedDiagnostics")})
	{
		FBoolProperty* Property = FindFProperty<FBoolProperty>(Harness.World->GetClass(), Name);
		if (!TestNotNull(TEXT("Debug switch has reflected boolean storage"), Property)) return false;
		for (const bool Enabled : {true, false, true})
		{
			Harness.RuntimeComponent->RegisterComponent();
			TestTrue(TEXT("Runtime registered before Details edit"), Harness.RuntimeComponent->IsRegistered());
			static_cast<UObject*>(Harness.World)->PreEditChange(Property);
			TestTrue(TEXT("Debug pre-edit must not unregister layout runtime"), Harness.RuntimeComponent->IsRegistered());
			Property->SetPropertyValue_InContainer(Harness.World, Enabled);
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			static_cast<UObject*>(Harness.World)->PostEditChangeProperty(Event);
			TestTrue(TEXT("Debug edit preserves runtime registration"), Harness.RuntimeComponent->IsRegistered());
			TestTrue(TEXT("Debug edit leaves terrain running"), Harness.World->IsRunning());
			TestEqual(TEXT("Debug edit retains existing layout state"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 1);
		}
	}
	Harness.RuntimeComponent->SetDebugGenerationStats(false);
	Harness.World->SetDetailedDiagnostics(true);
	TestTrue(TEXT("Detailed planning diagnostics use the actor flag"), Harness.RuntimeComponent->GetDetailedDiagnostics());
	TestFalse(TEXT("Detailed diagnostics cannot enable stats"), Harness.RuntimeComponent->GetDebugGenerationStats());
	Harness.RuntimeComponent->SetDebugGenerationStats(true);
	Harness.World->SetDetailedDiagnostics(false);
	TestTrue(TEXT("Stats can remain on without probes"), Harness.RuntimeComponent->GetDebugGenerationStats());
	TestFalse(TEXT("Stats cannot enable probes"), Harness.RuntimeComponent->GetDetailedDiagnostics());

	Harness.World->StartGen();
	TestEqual(TEXT("Explicit restart still clears records"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldConfigurationLifecycleTest,
	"PorismExtension.ChunkWorld.DebugControls.ConfigurationLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldConfigurationLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutWorldTestUtilities;
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	for (const TCHAR* Name : {TEXT("ShowDebugChunkLines"), TEXT("DebugTemplateCenterViewDistance")})
	{
		Harness.RuntimeComponent->RegisterComponent();
		FProperty* Property = FindFProperty<FProperty>(Harness.World->GetClass(), Name);
		if (!TestNotNull(TEXT("Native setting exists"), Property)) return false;
		static_cast<UObject*>(Harness.World)->PreEditChange(Property);
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Harness.World)->PostEditChangeProperty(Event);
		TestTrue(*FString::Printf(TEXT("%s edit restores layout registration and HUD lifecycle"), Name), Harness.RuntimeComponent->IsRegistered());
		TestTrue(TEXT("Configuration edit leaves terrain running"), Harness.World->IsRunning());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldDebugControlsDetailsTest,
	"PorismExtension.ChunkWorld.DebugControls.Details",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldDebugControlsDetailsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutWorldTestUtilities;
	FModuleManager::LoadModuleChecked<IModuleInterface>(TEXT("PorismDIMsWorldGeneratorExtensionEditor"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	FPropertyEditorModule& Module = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bUpdatesFromSelection = false;
	TSharedRef<IDetailsView> View = Module.CreateDetailView(Args);
	View->SetObject(Harness.World, true);
	const auto CountProperty = [&View](FName Name)
	{
		int32 Count = 0;
		for (const FPropertyPath& Path : View->GetPropertiesInOrderDisplayed())
		{
			if (Path.GetLeafMostProperty().Property.IsValid() && Path.GetLeafMostProperty().Property->GetFName() == Name) ++Count;
		}
		return Count;
	};
	for (FName Name : {FName(TEXT("ShowDebugData")), FName(TEXT("bDetailedDiagnostics")),
		FName(TEXT("ShowDebugChunkLines")), FName(TEXT("ShowDebugChunkCollision")),
		FName(TEXT("ShowDebugTemplateCenterBlocks")), FName(TEXT("DebugColors"))})
	{
		TestEqual(*FString::Printf(TEXT("Exactly one Details row for %s"), *Name.ToString()), CountProperty(Name), 1);
	}
	TestEqual(TEXT("No duplicate component stats checkbox"), CountProperty(TEXT("bDebugGenerationStats")), 0);
	TestEqual(TEXT("Template lifetime hidden when markers disabled"), CountProperty(TEXT("DebugTemplateCenterLifetime")), 0);
	Harness.World->ShowDebugTemplateCenterBlocks = true;
	View->ForceRefresh();
	TestEqual(TEXT("Unused template lifetime stays hidden with markers enabled"), CountProperty(TEXT("DebugTemplateCenterLifetime")), 0);
	for (FName Name : {FName(TEXT("BlockTypeSchemaComponent")), FName(TEXT("BlockFeedbackComponent")),
		FName(TEXT("BlockSwapScannerComponent")), FName(TEXT("BlockSwapComponent")), FName(TEXT("LayoutRuntimeComponent")),
		FName(TEXT("bEnablePlanningWindowRuntimeUpdates"))})
	{
		TestEqual(*FString::Printf(TEXT("Actor does not inline component settings: %s"), *Name.ToString()), CountProperty(Name), 0);
	}
	TestEqual(TEXT("Actor retains authoritative schema setting"), CountProperty(TEXT("BlockTypeSchemaRegistry")), 1);
	View->SetObject(Harness.RuntimeComponent, true);
	for (FName Name : {FName(TEXT("bEnablePlanningWindowRuntimeUpdates")), FName(TEXT("bFollowEditorCamera")),
		FName(TEXT("MaxCachedPlanningChunks")), FName(TEXT("MaxConcurrentBackgroundLayoutSolves"))})
	{
		TestEqual(*FString::Printf(TEXT("Component selection retains one editable row: %s"), *Name.ToString()), CountProperty(Name), 1);
	}
	View->SetObject(Harness.World->GetBlockFeedbackComponent(), true);
	TestEqual(TEXT("Feedback component retains settings"), CountProperty(TEXT("FeedbackCullDistance")), 1);
	View->SetObject(Harness.World->GetBlockSwapComponent(), true);
	TestEqual(TEXT("Swap component retains settings"), CountProperty(TEXT("ParkingCellSize")), 1);
	View->SetObject(Harness.World->GetBlockSwapScannerComponent(), true);
	TestEqual(TEXT("Scanner component retains scan settings"), CountProperty(TEXT("SwapScanInterval")), 1);
	TestEqual(TEXT("Scanner component retains pooling settings"), CountProperty(TEXT("bEnableSwapActorPooling")), 1);
	View->SetObject(nullptr);
	return true;
}
#endif
