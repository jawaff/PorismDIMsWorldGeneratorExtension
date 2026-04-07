// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorldExtended.generated.h"

class UBlockTypeSchemaComponent;
class UBlockTypeSchemaRegistry;
class UChunkWorldBlockFeedbackComponent;
class UChunkWorldBlockSwapScannerComponent;
class UChunkWorldBlockSwapComponent;
class AChunkWorldExtended;
#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChunkWorldAuthoritativeBlockCustomDataUpdated, AChunkWorldExtended*, ChunkWorld, const FIntVector&, BlockWorldPos);

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
	 * Sizes chunk-world custom-data storage from the active schema registry before Porism compiles runtime config.
	 */
	virtual SCacheKey StartGenDTs() override;

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
	 * Returns the shared server-side swap scanner used for proximity-driven mesh/actor presentation.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UChunkWorldBlockSwapScannerComponent* GetBlockSwapScannerComponent() const;

	/**
	 * Returns the replicated block swap component that owns shared hide/restore transitions.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld")
	UChunkWorldBlockSwapComponent* GetBlockSwapComponent() const;

	/** Broadcast after authoritative replicated custom-data updates are applied for one block on this chunk world. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld")
	FOnChunkWorldAuthoritativeBlockCustomDataUpdated OnAuthoritativeBlockCustomDataUpdated;

	/**
	 * Processes authoritative custom data changes to trigger things like block removal.
	 */
	void ProcessAuthoritativeCustomDataChanges(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues);

	/**
	 * Removes one block's runtime representation using the authored block-definition association.
	 */
	bool DestroyBlock(const FIntVector& BlockWorldPos, bool bRefreshChunks);

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
	/** Applies shared post-write behavior for one authoritative custom-data update, including project-side zero-health block removal. */
	void HandlePostAuthoritativeCustomDataUpdate(const FIntVector& BlockWorldPos, const SCustomDataChangeCall* ChangeCall = nullptr);

	/** Removes a block after the shared damage-family health slot commits a zero-or-lower value on the server. */
	void TryDestroyBlockFromCommittedHealth(const FIntVector& BlockWorldPos, const SCustomDataChangeCall* ChangeCall);

	/** Ensures WorldGenDef and runtime config expose enough custom-data channels for the active schema layout. */
	void EnsureSchemaCustomDataCapacity();

	/** Applies the actor-owned schema registry to the runtime schema component and adopts legacy component-authored values from older assets when needed. */
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

	/** Reusable server-side swap scanner used for proximity-driven mesh and actor presentation swaps. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", ToolTip = "Server-side swap scanner used for proximity-driven mesh and actor presentation swaps."))
	TObjectPtr<UChunkWorldBlockSwapScannerComponent> BlockSwapScannerComponent = nullptr;

	/** Reusable replicated block swap host used by shared chunk-world hide/restore transitions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld", meta = (AllowPrivateAccess = "true", ToolTip = "Replicated block swap host used by shared chunk-world hide and restore transitions."))
	TObjectPtr<UChunkWorldBlockSwapComponent> BlockSwapComponent = nullptr;
};
