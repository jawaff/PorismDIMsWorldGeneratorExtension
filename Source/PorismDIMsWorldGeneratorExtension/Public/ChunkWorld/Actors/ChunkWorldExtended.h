// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "ChunkWorld/ChunkWorld.h"
#include "UObject/ObjectKey.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "ChunkWorldExtended.generated.h"

class UBlockTypeSchemaComponent;
class UBlockTypeSchemaRegistry;
class AChunkWorldBlockSwapReplicationProxy;
class UChunkWorldBlockFeedbackComponent;
class UChunkWorldBlockSwapScannerComponent;
class UChunkWorldBlockSwapComponent;
enum class EBlockDestructionPresentationNetMode : uint8;
struct FChunkWorldBlockDestructionRequest;
#if WITH_EDITOR
struct FPropertyChangedEvent;
#endif

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnChunkWorldBlockCustomDataChanged, AChunkWorldExtended*, ChunkWorld, const FIntVector&, BlockWorldPos, bool, bTouchedHealth);

/**
 * Settled local view of one replicated block transition after the current replication batch has already been applied.
 * This is the shared client-side observation contract for both block-feedback playback and prediction reconciliation.
 */
USTRUCT(BlueprintType)
struct FChunkWorldSettledBlockTransition
{
	GENERATED_BODY()

	/** Block that changed in the settled local view. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** True when the current batch touched the shared health custom-data channel for this block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bTouchedHealth = false;

	/** True when the settled local health view changed across this batch, whether by damage or healing. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bObservedHealthChange = false;

	/** True when the settled local health view decreased across this batch. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bObservedHealthDecrease = false;

	/** True when the settled local representation changed from represented to not represented across this batch. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bObservedRepresentationRemoved = false;

	/** True when the block is still represented after the settled local batch has finished applying. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bIsRepresentedAfterTransition = false;

	/** True when the transition captured a previous local health value before the replicated batch applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bHasPreviousHealth = false;

	/** Local health value observed before the replicated batch applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	int32 PreviousHealth = 0;

	/** True when the transition captured a current local health value after the replicated batch applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	bool bHasCurrentHealth = false;

	/** Local health value observed after the replicated batch applied. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	int32 CurrentHealth = 0;

	/** Previously represented block context captured before removal when available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	FChunkWorldResolvedBlockHit PreviousResolvedHit;

	/** Current represented block context captured after the settled batch when available. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	FChunkWorldResolvedBlockHit CurrentResolvedHit;

	/** Previous block type captured before removal when available so subscribers can resolve authored data after the block is gone. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld")
	FGameplayTag PreviousBlockTypeName;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnChunkWorldSettledBlockTransition, AChunkWorldExtended*, ChunkWorld, const FChunkWorldSettledBlockTransition&, Transition);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldReady, AChunkWorldExtended*, ChunkWorld);

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

	/** Ticks world-owned startup readiness after Porism updates walker streaming state. */
	virtual void Tick(float DeltaTime) override;

	/** Spawns any networked runtime helpers owned by the chunk world. */
	virtual void BeginPlay() override;

	/** Cleans up any networked runtime helpers owned by the chunk world. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

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
	 * Returns the shared feedback component that handles immediate local playback and settled transition subscriptions.
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

	/** Returns the dedicated replicated courier that transports swap presentation events for this chunk world. */
	AChunkWorldBlockSwapReplicationProxy* GetBlockSwapReplicationProxy() const;

	/** Returns true once every registered chunk walker has reached the finest ready chunk around its tracked position. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ChunkWorld|Startup")
	bool IsWorldReady() const { return bWorldReady; }

	/** Returns true when the supplied walker is currently registered with this chunk world. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ChunkWorld|Startup")
	bool HasRegisteredChunkWorldWalker(const UObject* WorldLoader) const;

	/** Returns true when the supplied walker participated in the startup-ready transition for this chunk world. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ChunkWorld|Startup")
	bool WasChunkWorldWalkerIncludedInStartupReady(const UObject* WorldLoader) const;

	/** Adds a world loader and restarts aggregate startup readiness tracking for this chunk world. */
	void AddChunkWorldWalker(UObject* NewWorldLoader);

	/** Removes a world loader and recomputes aggregate startup readiness for the remaining walkers. */
	void RemoveChunkWorldWalker(UObject* WorldLoaderToRemove);

	/** Replaces the active world-loader set and recomputes aggregate startup readiness. */
	void SetChunkWorldWalkers(TArray<UObject*> NewWorldLoaders);

	/** Legacy narrower custom-data event kept only as a compatibility surface. New listeners should use `OnSettledBlockTransition` instead. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld", meta = (DeprecatedProperty, DeprecationMessage = "Use OnSettledBlockTransition instead so listeners observe the full settled replicated block transition payload."))
	FOnChunkWorldBlockCustomDataChanged OnBlockCustomDataChanged;

	/** Broadcast after one locally settled replicated block transition is observed on this chunk world. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld")
	FOnChunkWorldSettledBlockTransition OnSettledBlockTransition;

	/** Broadcast when every currently registered walker has reached a startup-safe ready chunk on this world. */
	UPROPERTY(BlueprintAssignable, Category = "ChunkWorld|Startup")
	FOnChunkWorldReady OnWorldReady;

	/**
	 * Processes one server-committed block custom-data write after the schema component has already stored the slot values.
	 */
	void HandleBlockCustomDataCommit(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues);

	/**
	 * Removes one block's runtime representation using the authored block-definition association.
	 */
	bool DestroyBlock(const FIntVector& BlockWorldPos, bool bRefreshChunks);

protected:
	/** Retires local predicted block state and emits a shared update event after authoritative custom-data replication is applied. */
	virtual void WriteCustomDataValuesAndUpdate(const TArray<SCustomDataChangeCall>& NetCustomDataChangeCalls) override;

	/** Observes replicated material changes so reusable settled-transition subscribers can react after local apply. */
	virtual void WriteBlockValuesAndUpdate(const TArray<SBlockChangeCall>& NetBlockChangeCalls, bool bRefreshChunks) override;

	/** Observes replicated mesh changes so reusable settled-transition subscribers can react after local apply. */
	virtual void WriteMeshDataAndUpdate(const TArray<SMeshChangeCall>& NetMeshChangeCalls, bool bRefreshChunks) override;

#if WITH_EDITOR
	/** Keeps the runtime schema component synced to the single actor-level editable registry property. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Keeps the runtime schema component synced after asset load. */
	virtual void PostLoad() override;

private:
	struct FChunkWorldWalkerReadyState
	{
		TWeakObjectPtr<UObject> Walker;
		bool bHasReceivedReadyInfo = false;
		bool bIsReady = false;
		int32 ReadyDetailLevel = INDEX_NONE;
		FChunkWorldWalkerInfo LastWalkerInfo;
	};

	struct FDeferredBlockCustomDataChange
	{
		bool bTouchedHealth = false;
		bool bObservedReplicatedHealthChange = false;
		bool bObservedReplicatedHealthDecrease = false;
		bool bObservedReplicatedRepresentationRemoved = false;
		bool bHasPreviousHealth = false;
		int32 PreviousHealth = 0;
		bool bHasCurrentHealth = false;
		int32 CurrentHealth = 0;
		FChunkWorldResolvedBlockHit PreviousResolvedHit;
		FGameplayTag PreviousBlockTypeName;
	};

	/** Applies server-only post-commit handling for one block after custom-data changes are committed locally. */
	void HandleCommittedBlockCustomData(const FIntVector& BlockWorldPos, const TArray<int32>& PackedValues);

	/** Queues one block custom-data change so observers refresh against the settled chunk-world state on the next tick. */
	void QueueBlockCustomDataChanged(const FIntVector& BlockWorldPos, bool bTouchedHealth, const TCHAR* SourceLabel);

	/** Flushes queued block custom-data changes after the current mutation stack has settled. */
	void FlushDeferredBlockCustomDataChanges();

	/** Builds one reusable settled block transition payload from the coalesced deferred observation state. */
	bool BuildSettledBlockTransition(const FIntVector& BlockWorldPos, const FDeferredBlockCustomDataChange& DeferredChange, FChunkWorldSettledBlockTransition& OutTransition) const;

	/** Notifies observers that one block's runtime custom-data view changed. */
	void NotifyBlockCustomDataChanged(const FIntVector& BlockWorldPos, bool bTouchedHealth, const TCHAR* SourceLabel);

	/** Returns true when the current local block state is still represented by material or mesh data. */
	bool IsRepresentedBlockAt(const FIntVector& BlockWorldPos) const;

	/** Reads one locally settled runtime health value for transition observation. */
	bool TryGetObservedRuntimeHealth(const FIntVector& BlockWorldPos, int32& OutHealth) const;

	/** Captures one previously represented block context before a removal write applies. */
	bool TryBuildPreviousResolvedHit(const FIntVector& BlockWorldPos, FChunkWorldResolvedBlockHit& OutResolvedHit, FGameplayTag& OutBlockTypeName) const;

	/** Spawns and triggers one authored destruction presentation actor when the destroyed block uses the shared damage definition family. */
	void TrySpawnDestructionActorForDestroyedBlock(const FIntVector& BlockWorldPos, const FChunkWorldResolvedBlockHit* DestroyedFeedbackHit, const FGameplayTag& BlockTypeName);

	/** Spawns one destruction presentation actor from an already resolved class/request pair, forcing the authored network delivery policy. */
	void SpawnResolvedDestructionActor(
		UClass* DestructionActorClass,
		const FChunkWorldBlockDestructionRequest& Request,
		EBlockDestructionPresentationNetMode PresentationNetMode);

	/** Merges one observed settled replicated health transition into the next deferred transition flush. */
	void QueueObservedReplicatedHealthTransition(const FIntVector& BlockWorldPos, int32 PreviousHealth, int32 CurrentHealth);

	/** Merges one observed settled replicated representation removal into the next deferred transition flush. */
	void QueueObservedReplicatedRepresentationRemoved(const FIntVector& BlockWorldPos, const FChunkWorldResolvedBlockHit* PreviousResolvedHit, const FGameplayTag& PreviousBlockTypeName);

	/** Removes a block after a committed health write leaves the shared damage-family health slot at zero or lower on the server. */
	void TryDestroyBlockFromCommittedHealth(const FIntVector& BlockWorldPos, bool bTouchedHealth);

	/** Spawns the dedicated replicated swap courier used for multiplayer mesh parking events. */
	void EnsureBlockSwapReplicationProxy();

	/** Ensures WorldGenDef and runtime config expose enough custom-data channels for the active schema layout. */
	void EnsureSchemaCustomDataCapacity();

	/** Applies the actor-owned schema registry to the runtime schema component and adopts legacy component-authored values from older assets when needed. */
	void SyncBlockTypeSchemaRegistry();

	/** Records one walker update dispatched on the game thread and refreshes aggregate startup readiness. */
	void HandlePendingWalkerInfo(UObject* Walker, const FChunkWorldWalkerInfo& Info);

	/** Returns true when the reported walker chunk is the finest currently ready detail layer for this world. */
	bool IsWalkerReadyForWorld(const FChunkWorldWalkerInfo& Info) const;

	/** Recomputes aggregate startup readiness from the currently registered walker set. */
	void RefreshWorldReadyState();

	/** Drops stale walker entries that are no longer registered or no longer valid. */
	void PruneWalkerReadyStates();

	/** Resets aggregate startup readiness when generation or registration restarts. */
	void ResetWorldReadyStateTracking();

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

	/** Dedicated replicated courier used to transport live swap presentation events outside the Porism chunk-world actor path. */
	UPROPERTY(Transient)
	TObjectPtr<AChunkWorldBlockSwapReplicationProxy> BlockSwapReplicationProxy = nullptr;

	/** Deferred block custom-data notifications coalesced until the next tick so reads see settled runtime state. */
	TMap<FIntVector, FDeferredBlockCustomDataChange> DeferredBlockCustomDataChanges;

	/** True while one next-tick flush has already been scheduled for deferred block custom-data notifications. */
	bool bHasDeferredBlockCustomDataFlushQueued = false;

	/** Per-walker startup readiness observations keyed by the registered walker object. */
	TMap<FObjectKey, FChunkWorldWalkerReadyState> WalkerReadyStates;

	/** Snapshot of walkers that were part of the startup-ready transition when this world first became ready. */
	TSet<FObjectKey> StartupReadyWalkerKeys;

	/** True while every currently registered walker is ready to safely use this chunk world. */
	bool bWorldReady = false;

	/** True only while the initial startup-ready handshake is still being observed. */
	bool bStartupWorldReadyTrackingActive = false;
};
