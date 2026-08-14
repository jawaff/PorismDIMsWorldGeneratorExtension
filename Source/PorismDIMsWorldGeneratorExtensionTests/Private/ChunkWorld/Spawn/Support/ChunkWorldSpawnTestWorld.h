// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"

namespace ChunkWorldSpawnTest
{
	/** Creates a transient game world suitable for authority-only spawn-component automation coverage. */
	inline UWorld* CreateWorld(FWorldContext*& OutWorldContext)
	{
		OutWorldContext = nullptr;
		if (GEngine == nullptr)
		{
			return nullptr;
		}

		const FName WorldName = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("ChunkWorldSpawnAutomationWorld"));
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, WorldName, GetTransientPackage(), true);
		if (World == nullptr)
		{
			return nullptr;
		}

		OutWorldContext = &GEngine->CreateNewWorldContext(EWorldType::Game);
		OutWorldContext->SetCurrentWorld(World);
		World->BeginPlay();
		return World;
	}

	/** Destroys one transient game world and its associated engine context. */
	inline void DestroyWorld(UWorld* World, FWorldContext* WorldContext)
	{
		if (GEngine != nullptr && WorldContext != nullptr)
		{
			GEngine->DestroyWorldContext(WorldContext->World());
		}
		if (World != nullptr)
		{
			World->DestroyWorld(false);
		}
	}
}
