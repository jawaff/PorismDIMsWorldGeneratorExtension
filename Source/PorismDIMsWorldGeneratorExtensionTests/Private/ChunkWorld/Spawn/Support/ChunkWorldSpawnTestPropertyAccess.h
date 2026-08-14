// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"
#include "UObject/UnrealType.h"

namespace ChunkWorldSpawnTest
{
	/** Sets one private integer component setting for isolated bounded-runtime automation coverage. */
	inline bool SetSpawnComponentIntProperty(UChunkWorldSpawnComponent& Component, const FName PropertyName, const int32 Value)
	{
		FIntProperty* const Property = FindFProperty<FIntProperty>(Component.GetClass(), PropertyName);
		if (Property == nullptr)
		{
			return false;
		}

		Property->SetPropertyValue_InContainer(&Component, Value);
		return true;
	}

	/** Sets one private floating-point component setting for isolated bounded-runtime automation coverage. */
	inline bool SetSpawnComponentFloatProperty(UChunkWorldSpawnComponent& Component, const FName PropertyName, const float Value)
	{
		FFloatProperty* const Property = FindFProperty<FFloatProperty>(Component.GetClass(), PropertyName);
		if (Property == nullptr)
		{
			return false;
		}

		Property->SetPropertyValue_InContainer(&Component, Value);
		return true;
	}
}
