// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"

#include "ChunkWorldPooledSwapActorInterface.generated.h"

/**
 * Optional swap-actor pooling contract for project-owned actors that need explicit reset/reinitialize hooks.
 */
UINTERFACE(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldPooledSwapActorInterface : public UInterface
{
	GENERATED_BODY()
};

class PORISMDIMSWORLDGENERATOREXTENSION_API IChunkWorldPooledSwapActorInterface
{
	GENERATED_BODY()

public:
	/**
	 * Reinitializes the actor for one new block swap assignment after the scanner acquires it from the pool.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	void PrepareForSwapUse(FIntVector BlockWorldPos, FTransform SwapTransform);

	/**
	 * Resets any project-owned transient state before the scanner returns the actor to the pool.
	 */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Block|ChunkWorld|Swap")
	void ResetForSwapPool();
};
