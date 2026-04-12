// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ChunkWorldBlockSwapTypes.generated.h"

class AActor;

/** Generic swap request payload emitted when a block enters or leaves actor presentation. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FChunkWorldBlockSwapRequest
{
	GENERATED_BODY()

	/** Block position whose represented voxel is entering or leaving actor swap presentation. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Swap")
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Semantic block type resolved from the shared block schema. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Swap")
	FGameplayTag BlockTypeName;

	/** Actor class configured by shared block schema authoring for this swap request when available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Swap")
	TSoftClassPtr<AActor> SwapActorClass;

	/** True when the block is swapping out to actor presentation, false when the block is being restored. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Swap")
	bool bEntering = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldBlockSwapRequest, const FChunkWorldBlockSwapRequest&, Request);

/** One replicated active swap entry for a currently parked voxel and its actor presentation. */
USTRUCT()
struct PORISMDIMSWORLDGENERATOREXTENSION_API FReplicatedChunkWorldSwapItem
{
	GENERATED_BODY()

	/** Semantic block world position whose represented mesh instance is temporarily parked. */
	UPROPERTY()
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Block type tag used for diagnostics and future extension hooks. */
	UPROPERTY()
	FGameplayTag BlockTypeName;

	/** Authoritative mesh id observed when the block first entered swap presentation. */
	UPROPERTY()
	int32 MeshId = INDEX_NONE;

	/** Presentation transform resolved by the server when the block entered swap presentation. */
	UPROPERTY()
	FTransform PresentationTransform = FTransform::Identity;

	/** Replicated actor currently presenting this parked voxel when the actor channel is available. */
	UPROPERTY()
	TObjectPtr<AActor> SwapActor = nullptr;
};
