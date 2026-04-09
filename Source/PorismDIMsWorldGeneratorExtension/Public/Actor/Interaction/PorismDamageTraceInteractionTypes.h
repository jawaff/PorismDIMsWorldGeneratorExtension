// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/Interaction/PorismTraceInteractionTypes.h"
#include "PorismDamageTraceInteractionTypes.generated.h"

/**
 * Damage-aware block interaction payload used when a block supports the shared health schema family.
 */
USTRUCT(BlueprintType)
struct FChunkWorldDamageBlockInteractionResult : public FChunkWorldBlockInteractionResult
{
	GENERATED_BODY()

	/** True when the resolved block supports the shared health schema family. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bSupportsHealth = false;

	/** True when current health is coming from a live local prediction instead of authoritative custom data. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bUsingPredictedHealth = false;

	/** True when runtime custom data has already been initialized for this block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bHasCustomData = false;

	/** True when current health is backed by authoritative runtime state, even if full custom-data reconstruction is not yet available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bHasAuthoritativeHealth = false;

	/** True when the block can currently accept shared damage under the health schema family. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bIsDestructible = false;

	/** True when the block currently ignores damage requests under the shared health schema family. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bIsInvincible = false;

	/** Current health for the resolved block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 CurrentHealth = 0;

	/** Maximum health for the resolved block type. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 MaxHealth = 0;
};
