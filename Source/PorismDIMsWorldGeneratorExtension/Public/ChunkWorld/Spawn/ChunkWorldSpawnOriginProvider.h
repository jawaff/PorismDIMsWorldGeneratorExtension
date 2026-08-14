// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnTypes.h"

#include "ChunkWorldSpawnOriginProvider.generated.h"

/** Defines the project-owned source of tagged origin snapshots for optional spawn policies. */
UINTERFACE(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldSpawnOriginProvider : public UInterface
{
	GENERATED_BODY()
};

/** Supplies opaque tagged world locations without requiring the spawn plugin to identify project-specific classes. */
class PORISMDIMSWORLDGENERATOREXTENSION_API IChunkWorldSpawnOriginProvider
{
	GENERATED_BODY()

public:
	/** Appends origin snapshots that satisfy the supplied project-authored tag query. */
	virtual void GatherSpawnOrigins(
		const FGameplayTagQuery& RequiredTags,
		TArray<FChunkWorldSpawnOrigin>& OutOrigins) const = 0;
};
