// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/Hit/ChunkWorldBlockDamageTypes.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "PorismPredictedBlockStateComponent.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FPorismTrackedBlockStateChangedSignature, AChunkWorld* /*ChunkWorld*/, const FIntVector& /*BlockWorldPos*/);

USTRUCT(BlueprintType)
struct FPorismBlockHealthState
{
	GENERATED_BODY()

	/** True when the current health view is coming from a live local prediction. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	bool bUsingPredictedHealth = false;

	/** True when runtime custom data has already been initialized for this block. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	bool bHasCustomData = false;

	/** True when the current health view is backed directly by initialized authoritative runtime state. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	bool bHasAuthoritativeHealth = false;

	/** True when the block currently ignores shared damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	bool bIsInvincible = false;

	/** Current health from either prediction, initialized runtime custom data, or authored max-health fallback before initialization. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	int32 CurrentHealth = 0;

	/** Maximum health from the authored damage definition. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	int32 MaxHealth = 0;

	/** Resolved block type for this health view. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Prediction")
	FGameplayTag BlockTypeName;
};

USTRUCT()
struct FQueuedPredictedBlockDamage
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<AChunkWorld> ChunkWorld = nullptr;

	UPROPERTY()
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	UPROPERTY()
	int32 DamageAmount = 0;

	UPROPERTY()
	FName RequestContextTag = NAME_None;
};

/**
 * Local-only prediction cache for block health state keyed by resolved chunk world block hits.
 */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Porism Predicted Block State Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismPredictedBlockStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPorismPredictedBlockStateComponent();

	/** Returns the shared notification fired whenever one tracked block changes predicted or authoritative state. */
	FPorismTrackedBlockStateChangedSignature& OnTrackedBlockStateChanged() { return TrackedBlockStateChangedEvent; }

	/**
	 * Applies one shared block-damage request through either the authoritative or client-predicted path.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool ApplyBlockDamageRequest(const FChunkWorldBlockDamageRequest& DamageRequest, FChunkWorldBlockDamageRequestResult& OutResult);

	/**
	 * Compatibility wrapper for older callers that only provide a resolved block hit plus damage amount.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool ApplyPredictedDamageAndQueueAuthoritativeFlush(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 DamageAmount);

	/**
	 * Clears a stored prediction because authoritative state for the same block has arrived.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	void ClearPredictionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit);

	/**
	 * Returns the current block health view for a resolved block using prediction first and chunk-world damage fallback second.
	 * When runtime custom data is not initialized yet, the returned health view falls back to the authored max-health value.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool TryGetHealthState(const FChunkWorldResolvedBlockHit& ResolvedHit, FPorismBlockHealthState& OutState);

	/**
	 * Returns one stored predicted health state for a resolved block when the client has an active local prediction.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool TryGetPredictedHealthState(const FChunkWorldResolvedBlockHit& ResolvedHit, int32& OutHealth, bool& bOutInvincible, FGameplayTag& OutBlockTypeName);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Prediction", meta = (ClampMin = "0.01", UIMin = "0.01", ToolTip = "How long one predicted block state stays alive without an authoritative update."))
	float PredictionTimeoutSeconds = 10.0f;

	/** Delay used to accumulate repeated local block damage requests before sending one authoritative flush to the server. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Prediction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay used to accumulate repeated local block damage requests before sending one authoritative flush to the server."))
	float PendingDamageFlushDelaySeconds = 0.05f;

private:
	struct FPredictedBlockKey
	{
		TWeakObjectPtr<AChunkWorld> ChunkWorld;
		FIntVector BlockWorldPos = FIntVector::ZeroValue;

		friend bool operator==(const FPredictedBlockKey& A, const FPredictedBlockKey& B)
		{
			return A.ChunkWorld == B.ChunkWorld && A.BlockWorldPos == B.BlockWorldPos;
		}

		friend uint32 GetTypeHash(const FPredictedBlockKey& Key)
		{
			return HashCombine(GetTypeHash(Key.ChunkWorld.Get()), GetTypeHash(Key.BlockWorldPos));
		}
	};

	UFUNCTION()
	void HandleObservedChunkWorldBlockCustomDataChanged(AChunkWorldExtended* ChunkWorld, const FIntVector& BlockWorldPos, bool bTouchedHealth);

	void PruneExpiredPredictions();
	bool ShouldRegisterPredictionNotifications() const;
	bool ValidateDamageRequest(const FChunkWorldBlockDamageRequest& DamageRequest) const;
	bool TryBuildPredictedDamageResult(const FChunkWorldBlockDamageRequest& DamageRequest, float PredictionTimeSeconds, FChunkWorldBlockDamageResult& OutResult) const;
	bool DidTrackedHealthStateChange(const FChunkWorldBlockDamageResult* PreviousResult, const FChunkWorldBlockDamageResult& NewResult) const;
	bool TryApplyImmediateLocalFeedback(const FChunkWorldBlockDamageRequest& DamageRequest, const FChunkWorldBlockDamageResult& DamageResult) const;
	void StorePredictedDamageResult(const FChunkWorldBlockDamageRequest& DamageRequest, const FChunkWorldBlockDamageResult& DamageResult);
	void QueueAuthoritativeDamage(const FChunkWorldBlockDamageRequest& DamageRequest);
	void FlushQueuedPredictedDamage();
	bool ApplyAuthoritativeDamageRequest(const FChunkWorldBlockDamageRequest& DamageRequest, FChunkWorldBlockDamageRequestResult& OutResult);
	void BroadcastTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);
	void BindObservedChunkWorld(AChunkWorld* ChunkWorld);
	void BindObservedChunkWorlds();
	void UnbindObservedChunkWorlds();
	void HandleActorSpawned(AActor* SpawnedActor);
	static FPredictedBlockKey MakeKey(const FChunkWorldResolvedBlockHit& ResolvedHit);
	static FPredictedBlockKey MakeKey(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);

	UFUNCTION(Server, Reliable)
	void ServerFlushQueuedPredictedDamage(const TArray<FQueuedPredictedBlockDamage>& QueuedDamageEntries);

	TMap<FPredictedBlockKey, FChunkWorldBlockDamageResult> PredictedBlockStates;
	TMap<FPredictedBlockKey, FQueuedPredictedBlockDamage> PendingAuthoritativeDamageByBlock;
	FTimerHandle PendingDamageFlushHandle;
	FPorismTrackedBlockStateChangedSignature TrackedBlockStateChangedEvent;
	TArray<TWeakObjectPtr<class AChunkWorldExtended>> ObservedChunkWorlds;
	FDelegateHandle ActorSpawnedHandle;
};
