// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorldExtended/ChunkWorldNetManager.h"
#include "ChunkWorldExtended/ChunkWorldNetDefinition.h"
#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestWorld.h"
#include "Misc/AutomationTest.h"
#include "UObject/UnrealType.h"

namespace
{
	TArray<uint8> Payload(const FString& Text)
	{
		FTCHARToUTF8 UTF8(*Text);
		return TArray<uint8>(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldNetworkDefinitionLifetimeTest,
	"PorismExtension.ChunkWorld.Network.DefinitionLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldNetworkDefinitionLifetimeTest::RunTest(const FString& Parameters)
{
	FWorldContext* Context = nullptr;
	UWorld* World = ChunkWorldSpawnTest::CreateWorld(Context);
	if (!TestNotNull(TEXT("World exists"), World)) return false;
	AChunkWorldBase* Generator = World->SpawnActor<AChunkWorldBase>();
	AChunkWorldNetManager* Manager = World->SpawnActor<AChunkWorldNetManager>();
	const int32 Id = Manager->RegisterWorld(Generator);
	Manager->NetMode = NM_Client;
	Manager->PendingWorldGenDefs.Add({Id, Payload(FString(TEXT("BaseBlockSize")) + TCHAR(1) + TEXT("75") + TCHAR(1))});
	Manager->Tick(0.0f);
	UWorldGenDef* First = Generator->WorldGenDef;
	TestNotNull(TEXT("Production receiver adopts definition"), First);
	if (First)
	{
		TestEqual(TEXT("Configuration survives receipt"), First->BaseBlockSize, 75);
		TestFalse(TEXT("Received definition does not acquire a global GC root"), First->IsRooted());
	}
	Manager->PendingWorldGenDefs.Add({Id, Payload(FString(TEXT("BaseBlockSize")) + TCHAR(1) + TEXT("125") + TCHAR(1))});
	Manager->Tick(0.0f);
	UWorldGenDef* Second = Generator->WorldGenDef;
	TestTrue(TEXT("Replacement adopts a distinct definition"), Second && Second != First);
	TestEqual(TEXT("Replacement configuration survives"), Second ? Second->BaseBlockSize : 0, 125);
	if (Second) TestFalse(TEXT("Replacement does not acquire a global GC root"), Second->IsRooted());

	Generator->StopGen();
	TestNull(TEXT("Stop releases an adopted definition even before startup"), Generator->WorldGenDef.Get());
	UWorldGenDef* Borrowed = NewObject<UWorldGenDef>(World);
	Generator->WorldGenDef = Borrowed;
	Generator->StopGen();
	TestEqual(TEXT("Stop preserves borrowed definitions"), Generator->WorldGenDef.Get(), Borrowed);
	TWeakObjectPtr<UWorldGenDef> WeakFirst = First;
	TWeakObjectPtr<UWorldGenDef> WeakSecond = Second;
	TWeakObjectPtr<UWorld> WeakWorld = World;
	ChunkWorldSpawnTest::DestroyWorld(World, Context);
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Replaced definition is collectible"), WeakFirst.IsValid());
	TestFalse(TEXT("Stopped definition is collectible"), WeakSecond.IsValid());
	TestFalse(TEXT("Stopped network world is collectible"), WeakWorld.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FChunkWorldNetworkRuntimeFieldsTest,
	"PorismExtension.ChunkWorld.Network.RuntimeFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldNetworkRuntimeFieldsTest::RunTest(const FString& Parameters)
{
	for (const FName Name : {FName(TEXT("WorldGenRun")), FName(TEXT("GenOnlyTestRun"))})
	{
		const FProperty* Property = FindFProperty<FProperty>(UWorldGenDef::StaticClass(), Name);
		TestTrue(*FString::Printf(TEXT("%s is excluded from config persistence and network export"), *Name.ToString()),
			Property && Property->HasAnyPropertyFlags(CPF_Transient));
	}
	for (const FName Name : {FName(TEXT("DomainRun")), FName(TEXT("DualSwitchRun")), FName(TEXT("GenARun")), FName(TEXT("GenBRun"))})
	{
		const FProperty* Property = FindFProperty<FProperty>(FBiomeDualData::StaticStruct(), Name);
		TestTrue(*FString::Printf(TEXT("Nested %s is runtime-only"), *Name.ToString()),
			Property && Property->HasAnyPropertyFlags(CPF_Transient));
	}
	return true;
}
