// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldDisposableStorageTest,
	"PorismExtension.ChunkWorld.Storage.DisposableLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldDisposableStorageTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutWorldTestUtilities;
	FLayoutWorldTestHarness Harness;
	Harness.World = GEditor->GetEditorWorldContext().World()->SpawnActor<AChunkWorldExtended>();
	Harness.RuntimeComponent = Harness.World->GetLayoutRuntimeComponent();
	Harness.World->AutoSave = false;
	Harness.World->UseCache = false;
	// Even a pre-fix run must not open an existing world cache.
	Harness.World->CacheTarget = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()
		/ TEXT("Automation/StorageRed") / FGuid::NewGuid().ToString(EGuidFormats::Digits)) + TEXT("/");
	FLayoutTestWorldSupport::InitializeTransientChunkWorld(Harness.World, FIntVector(16), TEXT("StorageBiome"));
	const FString Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("ChunkWorldSessions")) + TEXT("/");
	if (!TestTrue(TEXT("Native saves use an owned per-run directory"), Harness.World->SaveTarget.StartsWith(Root))
		|| !TestTrue(TEXT("Native caches use an owned per-run directory"), Harness.World->CacheTarget.StartsWith(Root))) return false;
	TestTrue(TEXT("Disk-backed eviction is enabled regardless of authored settings"), Harness.World->AutoSave);
	TestTrue(TEXT("Native generated cache remains enabled"), Harness.World->UseCache);
	const FString FirstSave = Harness.World->SaveTarget;
	const FString FirstCache = Harness.World->CacheTarget;
	TestTrue(TEXT("Native cache filename concatenation has its separator"), FirstCache.EndsWith(TEXT("/")));
	TestNotEqual(TEXT("Generated cache and edited saves are separate stores"), FirstSave, FirstCache);

	// Exercise the production edit/read path across native save-store eviction, not merely path assignment.
	const FIntVector Position(2, 3, 4);
	Harness.World->SetBlockValueByBlockWorldPos(Position, SinfullMaterial, false);
	TestTrue(TEXT("Edited block has a resident save file"), Harness.World->DriveSave->GetCacheFileCount() > 0);
	Harness.World->DriveSave->CleanUp();
	TestEqual(TEXT("Save file evicted from RAM"), Harness.World->DriveSave->GetCacheFileCount(), 0);
	TestEqual(TEXT("Re-entry restores the edited voxel from disk"),
		Harness.World->GetBlockValueByBlockWorldPos(Position, ERessourceType::MaterialIndex), SinfullMaterial);
	TestEqual(TEXT("Unload/re-entry preserves save namespace"), Harness.World->SaveTarget, FirstSave);
	TestEqual(TEXT("Unload/re-entry preserves cache namespace"), Harness.World->CacheTarget, FirstCache);

	FLayoutWorldTestHarness Other = CreateChunkWorldHarness(GetTransientPackage());
	const FString OtherSave = Other.World->SaveTarget;
	TestNotEqual(TEXT("Concurrent worlds cannot share stores"), OtherSave, FirstSave);
	const FString Sentinel = OtherSave / TEXT("keep.txt");
	TestTrue(TEXT("Other world sentinel created"), FFileHelper::SaveStringToFile(TEXT("keep"), *Sentinel));
	// Writable inherited properties cannot redirect cleanup to another generator's payload.
	Harness.World->SaveTarget = OtherSave;
	Harness.World->CacheTarget = Other.World->CacheTarget;
	Harness.World->AutoSave = false;
	Harness.World->StopGen();
	TestFalse(TEXT("Stopped run save removed"), IFileManager::Get().DirectoryExists(*FirstSave));
	TestFalse(TEXT("Stopped run cache removed"), IFileManager::Get().DirectoryExists(*FirstCache));
	TestTrue(TEXT("Other generator survives cleanup"), IFileManager::Get().FileExists(*Sentinel));
	Harness.World->StopGen();
	Harness.World->StartGen();
	TestTrue(TEXT("Same actor can start another run"), Harness.World->IsRunning());
	TestNotEqual(TEXT("Restart never reuses previous namespace"), Harness.World->SaveTarget, FirstSave);
	TestNotEqual(TEXT("Restart never reuses generated cache"), Harness.World->CacheTarget, FirstCache);
	TestNotEqual(TEXT("New run does not recover preceding run edits"),
		Harness.World->GetBlockValueByBlockWorldPos(Position, ERessourceType::MaterialIndex), SinfullMaterial);
	const FString RestartSave = Harness.World->SaveTarget;
	Harness.World->StartGen();
	TestTrue(TEXT("Explicit running restart succeeds"), Harness.World->IsRunning());
	TestFalse(TEXT("Explicit restart retires preceding run"), IFileManager::Get().DirectoryExists(*RestartSave));
	TestNotEqual(TEXT("Explicit restart allocates fresh storage"), Harness.World->SaveTarget, RestartSave);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldStorageShutdownSafetyTest,
	"PorismExtension.ChunkWorld.Storage.ShutdownSafety",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldStorageShutdownSafetyTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutWorldTestUtilities;
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	const FString Save = Harness.World->SaveTarget;
	const FString Directory = FPaths::GetPath(FPaths::GetPath(Save));
	const FString Marker = Directory / TEXT(".owner");
	FString Token;
	if (!TestTrue(TEXT("Ownership marker exists"), FFileHelper::LoadFileToString(Token, *Marker))) return false;

	// Fault-inject the native StopGen early-return condition while its worker pool still exists.
	Harness.World->GeneratorShouldBeOnline = false;
	AddExpectedError(TEXT("native writer shutdown is unconfirmed"), EAutomationExpectedErrorFlags::Contains, 1);
	Harness.World->StopGen();
	TestTrue(TEXT("Unconfirmed shutdown keeps live-writer payload"), IFileManager::Get().DirectoryExists(*Save));
	TestNotNull(TEXT("Native workers were not closed by early-return stop"), Harness.World->ThreadPool);
	Harness.World->GeneratorShouldBeOnline = true;

	TestTrue(TEXT("Ownership mismatch injected"), FFileHelper::SaveStringToFile(TEXT("not-owned"), *Marker));
	AddExpectedError(TEXT("Session cleanup failed"), EAutomationExpectedErrorFlags::Contains, 1);
	Harness.World->StopGen();
	TestNull(TEXT("Native workers closed before filesystem cleanup"), Harness.World->ThreadPool);
	TestTrue(TEXT("Unowned payload is retained"), IFileManager::Get().DirectoryExists(*Save));
	TestTrue(TEXT("Restore this test's ownership marker"), FFileHelper::SaveStringToFile(Token, *Marker));
#if PLATFORM_WINDOWS
	// A directory junction needs no symlink privilege and must never redirect recursive deletion.
	FLayoutWorldTestHarness Other = CreateChunkWorldHarness(GetTransientPackage());
	const FString Sentinel = Other.World->SaveTarget / TEXT("keep.txt");
	TestTrue(TEXT("Junction target sentinel created"), FFileHelper::SaveStringToFile(TEXT("keep"), *Sentinel));
	FString Link = Save / TEXT("Escape");
	FString Target = Other.World->SaveTarget;
	FPaths::MakePlatformFilename(Link);
	FPaths::MakePlatformFilename(Target);
	int32 ExitCode = -1;
	const FString Command = FString::Printf(TEXT("/d /c mklink /J \"%s\" \"%s\""), *Link, *Target);
	FPlatformProcess::ExecProcess(TEXT("cmd.exe"), *Command, &ExitCode, nullptr, nullptr);
	if (!TestEqual(TEXT("Test junction created"), ExitCode, 0)) return false;
	AddExpectedError(TEXT("Session cleanup failed"), EAutomationExpectedErrorFlags::Contains, 1);
	Harness.World->StopGen();
	TestTrue(TEXT("Junction target remains untouched"), IFileManager::Get().FileExists(*Sentinel));
	TestTrue(TEXT("Failed cleanup retains ownership for retry"), IFileManager::Get().FileExists(*Marker));
	TestTrue(TEXT("Remove only the test junction, not its target"), FPlatformFileManager::Get().GetPlatformPhysical().DeleteDirectory(*Link));
#endif
	Harness.World->StopGen();
	TestFalse(TEXT("Confirmed owned shutdown removes payload"), IFileManager::Get().DirectoryExists(*Directory));
	Harness.World->StopGen();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldStorageSettingsTest,
	"PorismExtension.ChunkWorld.Storage.DerivedSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldStorageSettingsTest::RunTest(const FString& Parameters)
{
	const AChunkWorldExtended* Defaults = GetDefault<AChunkWorldExtended>();
	for (const TCHAR* Name : {TEXT("SaveTarget"), TEXT("CacheTarget"), TEXT("AutoSave"), TEXT("UseCache")})
	{
		FProperty* Property = FindFProperty<FProperty>(Defaults->GetClass(), Name);
		if (TestNotNull(TEXT("Native storage property exists"), Property))
			TestFalse(TEXT("Disposable storage policy cannot be edited into persistence"), static_cast<const UObject*>(Defaults)->CanEditChange(Property));
	}
	TestNull(TEXT("Named persistent storage policy removed"), FindFProperty<FProperty>(Defaults->GetClass(), TEXT("WorldStorageName")));
	return true;
}
