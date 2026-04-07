// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "ChunkWorldBlockSwapTypes.generated.h"

class AActor;
class UChunkWorldBlockSwapComponent;

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
struct PORISMDIMSWORLDGENERATOREXTENSION_API FReplicatedChunkWorldSwapItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	/** Semantic block world position whose represented mesh instance is temporarily parked. */
	UPROPERTY()
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Block type tag used for diagnostics and future extension hooks. */
	UPROPERTY()
	FGameplayTag BlockTypeName;

	/** Replicated actor currently presenting this parked voxel when the actor channel is available. */
	UPROPERTY()
	TObjectPtr<AActor> SwapActor = nullptr;
};

/** Replicated active swap array that forwards add/change/remove events into the owning swap component. */
USTRUCT()
struct PORISMDIMSWORLDGENERATOREXTENSION_API FReplicatedChunkWorldSwapArray : public FFastArraySerializer
{
	GENERATED_BODY()

	/** Performs fast-array delta replication for the active swap list. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms);

	void PreReplicatedRemove(const TArrayView<int32> RemovedIndices, int32 FinalSize);
	void PostReplicatedAdd(const TArrayView<int32> AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32> ChangedIndices, int32 FinalSize);

	/** Finds the current array item for the supplied block position. */
	FReplicatedChunkWorldSwapItem* FindByBlockWorldPos(const FIntVector& BlockWorldPos);

	/** Const lookup for the current array item for the supplied block position. */
	const FReplicatedChunkWorldSwapItem* FindByBlockWorldPos(const FIntVector& BlockWorldPos) const;

	/** Active authoritative swap entries. */
	UPROPERTY()
	TArray<FReplicatedChunkWorldSwapItem> Items;

	/** Runtime back-pointer used by client replication callbacks. */
	TObjectPtr<UChunkWorldBlockSwapComponent> Owner = nullptr;
};

template <>
struct TStructOpsTypeTraits<FReplicatedChunkWorldSwapArray> : public TStructOpsTypeTraitsBase2<FReplicatedChunkWorldSwapArray>
{
	enum
	{
		WithNetDeltaSerializer = true,
	};
};
