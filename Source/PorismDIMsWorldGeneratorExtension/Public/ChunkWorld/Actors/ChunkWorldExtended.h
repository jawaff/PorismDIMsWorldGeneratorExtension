// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldExtended.generated.h"

class UBlockTypeSchemaComponent;
class UBlockTypeSchemaRegistry;
class UChunkWorldBlockFeedbackComponent;
class UChunkWorldBlockSwapComponent;
#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldAuthoritativeBlockCustomDataUpdated, const FIntVector&, BlockWorldPos);

/**
 * Basic extension chunk world that hosts the reusable block type schema component used by project-specific block systems.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldExtended : public AChunkWorld
{
	GENERATED_BODY()

public:
	/**
	 * Creates a chunk world that includes the reusable block type schema component.
	 */
	AChunkWorldExtended();

	/**
	 * Starts generation and then rebuilds the schema lookup maps once the world indexes are available.
	 */
	virtual void StartGen() override;

	/**
	 * Returns the reusable schema component that owns the block-definition lookup maps.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UBlockTypeSchemaComponent* GetBlockTypeSchemaComponent() const;

	/**
	 * Returns the schema registry assigned to this chunk world.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UBlockTypeSchemaRegistry* GetBlockTypeSchemaRegistry() const { return BlockTypeSchemaRegistry; }

	/**
	 * Returns the replicated feedback component that multicasts block hit and destroy feedback.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UChunkWorldBlockFeedbackComponent* GetBlockFeedbackComponent() const;

	/**
	 * Returns the replicated block swap component that owns shared hide/restore transitions.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UChunkWorldBlockSwapComponent* GetBlockSwapComponent() const;

	/** Broadcast after authoritative replicated custom-data updates are applied for one block on this chunk world. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld")
	FOnChunkWorldAuthoritativeBlockCustomDataUpdated OnAuthoritativeBlockCustomDataUpdated;

protected:
	/** Retires local predicted block state and emits a shared update event after authoritative custom-data replication is applied. */
	virtual void WriteCustomDataValuesAndUpdate(const TArray<SCustomDataChangeCall>& NetCustomDataChangeCalls) override;

#if WITH_EDITOR
	/** Keeps the runtime schema component synced to the single actor-level editable registry property. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Keeps the runtime schema component synced after asset load. */
	virtual void PostLoad() override;

private:
	/** Applies the actor-owned schema registry to the runtime schema component so details panels expose one source of truth. */
	void SyncBlockTypeSchemaRegistry();

	/** Single actor-level schema registry entry shown in details panels for chunk world setup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", NoClear, ToolTip = "Schema registry asset used by this chunk world's block schema component."))
	TObjectPtr<UBlockTypeSchemaRegistry> BlockTypeSchemaRegistry = nullptr;

	/**
	 * Reusable schema component that resolves block type schemas and owns the runtime lookup maps.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", ToolTip = "Reusable schema component that resolves block type schemas."))
	TObjectPtr<UBlockTypeSchemaComponent> BlockTypeSchemaComponent = nullptr;

	/** Reusable replicated block feedback component used by shared damage and destroy helpers. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", ToolTip = "Replicated feedback component used by shared block hit and destroy helpers."))
	TObjectPtr<UChunkWorldBlockFeedbackComponent> BlockFeedbackComponent = nullptr;

	/** Reusable replicated block swap host used by shared chunk-world hide/restore transitions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", ToolTip = "Replicated block swap host used by shared chunk-world hide and restore transitions."))
	TObjectPtr<UChunkWorldBlockSwapComponent> BlockSwapComponent = nullptr;
};
