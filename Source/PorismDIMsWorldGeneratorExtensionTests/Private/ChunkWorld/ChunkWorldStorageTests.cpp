// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Misc/AutomationTest.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldStorageNameTest,
	"PorismExtension.ChunkWorld.Storage.NameValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldStorageNameTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Default storage identity"), GetDefault<AChunkWorldExtended>()->GetWorldStorageName(), FString(TEXT("Default")));
	FString Error = TEXT("previous error");
	for (const TCHAR* Name : {TEXT("Default"), TEXT("default"), TEXT("Campaign01"), TEXT("World_2-test"), TEXT("COM10"), TEXT("LPT10")})
	{
		TestTrue(FString::Printf(TEXT("Accept %s"), Name), AChunkWorldExtended::ValidateWorldStorageName(Name, Error));
		TestTrue(TEXT("Successful validation clears the previous error"), Error.IsEmpty());
	}
	TestTrue(TEXT("64 characters accepted"), AChunkWorldExtended::ValidateWorldStorageName(FString::ChrN(64, TEXT('A')), Error));
	TestFalse(TEXT("65 characters rejected"), AChunkWorldExtended::ValidateWorldStorageName(FString::ChrN(65, TEXT('A')), Error));
	for (const TCHAR* Name : {TEXT(""), TEXT(".."), TEXT("../World"), TEXT("C:\\World"), TEXT("World/Other"),
		TEXT("World."), TEXT("World "), TEXT(" World"), TEXT("CON"), TEXT("prn"), TEXT("Aux"), TEXT("nul"),
		TEXT("com1"), TEXT("COM9"), TEXT("LPT1"), TEXT("lpt9"), TEXT("World\u00e9")})
	{
		TestFalse(FString::Printf(TEXT("Reject invalid identity '%s'"), Name), AChunkWorldExtended::ValidateWorldStorageName(Name, Error));
		TestFalse(TEXT("Rejection explains the failure"), Error.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldStorageNameLifecycleTest,
	"PorismExtension.ChunkWorld.Storage.NameLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldStorageNameLifecycleTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutWorldTestUtilities;
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("World exists"), Harness.World)) return false;
	FStrProperty* Property = FindFProperty<FStrProperty>(Harness.World->GetClass(), TEXT("WorldStorageName"));
	if (!TestNotNull(TEXT("Storage identity is an authored string"), Property)) return false;
	TestFalse(TEXT("Editor cannot change identity while running"), static_cast<UObject*>(Harness.World)->CanEditChange(Property));
	{
		TGuardValue<bool> Stopped(Harness.World->GeneratorIsRunning, false);
		TestTrue(TEXT("Editor can author identity while stopped"), static_cast<UObject*>(Harness.World)->CanEditChange(Property));
	}

	const FString PreviousSaveTarget = Harness.World->SaveTarget;
	const FString PreviousCacheTarget = Harness.World->CacheTarget;
	Property->SetPropertyValue_InContainer(Harness.World, TEXT("../OtherWorld"));
	AddExpectedError(TEXT("Storage name may contain only ASCII"), EAutomationExpectedErrorFlags::Contains, 1);
	Harness.World->StartGen();
	TestTrue(TEXT("Invalid replacement configuration leaves the existing run alone"), Harness.World->IsRunning());
	TestEqual(TEXT("Rejected identity cannot redirect saves"), Harness.World->SaveTarget, PreviousSaveTarget);
	TestEqual(TEXT("Rejected identity cannot redirect cache"), Harness.World->CacheTarget, PreviousCacheTarget);
	return true;
}
