// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "PorismTraceInteractionTypes.generated.h"

/**
 * Identifies the currently resolved interaction target kind for the shared trace component.
 */
UENUM(BlueprintType)
enum class EPorismTraceInteractionTargetType : uint8
{
	None,
	Actor,
	Block
};

/**
 * Generic trace-target payload used by the shared interaction trace component.
 */
USTRUCT(BlueprintType)
struct FPorismTraceInteractionResult
{
	GENERATED_BODY()

	/** True when the trace resolved any hit at all. */
	UPROPERTY(BlueprintReadOnly, Category = "Porism|Interaction")
	bool bHasHit = false;

	/** True when the hit actor passed the configured interactable-interface checks. */
	UPROPERTY(BlueprintReadOnly, Category = "Porism|Interaction")
	bool bIsInteractable = false;

	/** Final target kind selected by the trace arbitration pass. */
	UPROPERTY(BlueprintReadOnly, Category = "Porism|Interaction")
	EPorismTraceInteractionTargetType TargetType = EPorismTraceInteractionTargetType::None;

	/** Raw hit result preserved for gameplay and debug consumers. */
	UPROPERTY(BlueprintReadOnly, Category = "Porism|Interaction")
	FHitResult Hit;

	/** Interactable actor selected by the actor branch when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Porism|Interaction")
	TObjectPtr<AActor> InteractableActor = nullptr;
};

/**
 * Block-specific trace payload used when the shared interaction trace resolves a chunk-world block target.
 */
USTRUCT(BlueprintType)
struct FChunkWorldBlockInteractionResult
{
	GENERATED_BODY()

	/** True when the trace resolved a valid represented block and block type tag. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Interaction")
	bool bHasBlock = false;

	/** Raw hit result that produced the block target. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Interaction")
	FHitResult Hit;

	/** Shared resolved chunk-world block hit context reused by other systems. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Interaction")
	FChunkWorldResolvedBlockHit ResolvedBlockHit;

	/** Canonical block identity for the resolved block target. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Interaction")
	FGameplayTag BlockTypeName;
};
