// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ChunkWorldBlockDamageTypes.generated.h"

class AChunkWorld;

/**
 * Reusable block-damage result shared by chunk-world gameplay that applies integer damage to one resolved block.
 */
USTRUCT(BlueprintType)
struct FChunkWorldBlockDamageResult
{
	GENERATED_BODY()

	/** True when the target was a represented block at the time the request was processed. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bHitWasRepresentedBlock = false;

	/** True when the request changed block health or removed the block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAppliedDamage = false;

	/** True when the block was invincible and the request exited without mutating health. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bWasInvincible = false;

	/** True when this request used local-only predicted values instead of mutating authoritative runtime state. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bUsedPredictedWrite = false;

	/** True when the block was destroyed by this damage application. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bDestroyed = false;

	/** Resolved block world position. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Canonical block type tag for the damaged block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	FGameplayTag BlockTypeName;

	/** Chunk world that owned the block when this result was produced. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	TObjectPtr<AChunkWorld> ChunkWorld = nullptr;

	/** Runtime material index for the damaged block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 MaterialIndex = 0;

	/** Runtime mesh index for the damaged block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 MeshIndex = 0;

	/** Health before this damage request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 PreviousHealth = 0;

	/** Health after this damage request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 NewHealth = 0;

	/** Actual damage consumed by this request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 DamageApplied = 0;

	/** True when the shared damage schema family was available and block health could be resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bHasDamageSchema = false;

	/** Timestamp in world seconds for local-only predicted writes. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	float PredictionTimeSeconds = 0.0f;
};
