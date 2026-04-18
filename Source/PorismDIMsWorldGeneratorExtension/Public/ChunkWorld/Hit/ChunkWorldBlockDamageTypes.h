// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "ChunkWorldBlockDamageTypes.generated.h"

class AChunkWorld;

/**
 * Reusable block-health result shared by chunk-world gameplay that applies integer damage or healing to one resolved block.
 */
USTRUCT(BlueprintType)
struct FChunkWorldBlockHealthDeltaResult
{
	GENERATED_BODY()

	/** True when the target was a represented block at the time the request was processed. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bHitWasRepresentedBlock = false;

	/** True when the request applied damage and reduced block health or removed the block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAppliedDamage = false;

	/** True when the request changed block health by either damage or healing. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAppliedHealthChange = false;

	/** True when the request restored health without destroying the block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAppliedHealing = false;

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

	/** Canonical block type tag for the affected block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	FGameplayTag BlockTypeName;

	/** Chunk world that owned the block when this result was produced. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	TObjectPtr<AChunkWorld> ChunkWorld = nullptr;

	/** Runtime material index for the affected block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 MaterialIndex = 0;

	/** Runtime mesh index for the affected block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 MeshIndex = 0;

	/** Health before this request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 PreviousHealth = 0;

	/** Health after this request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 NewHealth = 0;

	/** Actual damage consumed by this request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 DamageApplied = 0;

	/** Actual healing consumed by this request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	int32 HealingApplied = 0;

	/** True when the shared health schema family was available and block health could be resolved. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bHasHealthSchema = false;

	/** Timestamp in world seconds for local-only predicted writes. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	float PredictionTimeSeconds = 0.0f;
};

/**
 * Shared block-health request used after higher-level gameplay has already computed a final health delta.
 */
USTRUCT(BlueprintType)
struct FChunkWorldBlockHealthDeltaRequest
{
	GENERATED_BODY()

	/** Resolved block identity for the health-delta request. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Block|ChunkWorld|Damage")
	FChunkWorldResolvedBlockHit ResolvedHit;

	/** Final positive integer amount to apply through the caller's selected damage or healing path. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Block|ChunkWorld|Damage", meta = (ClampMin = "0", UIMin = "0"))
	int32 Amount = 0;

	/** Optional caller-owned context tag that can be used for audit or future routing. */
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "Block|ChunkWorld|Damage")
	FName RequestContextTag = NAME_None;
};

/**
 * Shared block-health request result describing which prediction and authority paths ran.
 */
USTRUCT(BlueprintType)
struct FChunkWorldBlockHealthDeltaRequestResult
{
	GENERATED_BODY()

	/** True when the request was valid enough to enter the shared processing pipeline. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAccepted = false;

	/** True when local predicted state changed and was written to the prediction cache. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bPredictionWritten = false;

	/** True when authoritative damage was applied immediately on this process. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAuthoritativeDamageApplied = false;

	/** True when authoritative healing was applied immediately on this process. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bAuthoritativeHealingApplied = false;

	/** True when immediate local feedback played for the initiating process. Damage uses this for hit feedback; healing leaves it false. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	bool bPlayedImmediateLocalFeedback = false;

	/** Final shared health result produced by either prediction or authoritative mutation. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Damage")
	FChunkWorldBlockHealthDeltaResult HealthDeltaResult;
};
