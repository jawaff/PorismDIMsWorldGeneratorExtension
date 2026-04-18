// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/Interaction/PorismTraceInteractionTypes.h"
#include "PorismHealthTraceInteractionTypes.generated.h"

/**
 * Health-aware block interaction payload used when a block supports the shared health schema family.
 */
USTRUCT(BlueprintType)
struct FChunkWorldHealthBlockInteractionResult : public FChunkWorldBlockInteractionResult
{
	GENERATED_BODY()

	/** True when the resolved block supports the shared health schema family. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	bool bSupportsHealth = false;

	/** True when current health is coming from a live local prediction instead of authoritative custom data. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	bool bUsingPredictedHealth = false;

	/** True when runtime custom data has already been initialized for this block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	bool bHasCustomData = false;

	/** True when current health is backed by authoritative runtime state, even if full custom-data reconstruction is not yet available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	bool bHasAuthoritativeHealth = false;

	/** True when the block can currently accept shared damage under the health schema family. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	bool bIsDestructible = false;

	/** True when the block currently ignores damage requests under the shared health schema family. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	bool bIsInvincible = false;

	/** Current health for the resolved block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	int32 CurrentHealth = 0;

	/** Maximum health for the resolved block type. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Health")
	int32 MaxHealth = 0;
};
