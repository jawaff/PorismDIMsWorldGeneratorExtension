// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
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
	 * Applies one local-only predicted damage request and updates tracked prediction state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool ApplyPredictedDamageRequest(const FChunkWorldBlockHealthDeltaRequest& DamageRequest, FChunkWorldBlockHealthDeltaRequestResult& OutResult);

	/**
	 * Applies one authoritative server-side damage request through the shared block damage library.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool ApplyAuthoritativeDamageRequest(const FChunkWorldBlockHealthDeltaRequest& DamageRequest, FChunkWorldBlockHealthDeltaRequestResult& OutResult);

	/**
	 * Applies one local-only predicted healing request and updates tracked prediction state.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool ApplyPredictedHealingRequest(const FChunkWorldBlockHealthDeltaRequest& HealingRequest, FChunkWorldBlockHealthDeltaRequestResult& OutResult);

	/**
	 * Applies one authoritative server-side healing request through the shared block damage library.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool ApplyAuthoritativeHealingRequest(const FChunkWorldBlockHealthDeltaRequest& HealingRequest, FChunkWorldBlockHealthDeltaRequestResult& OutResult);

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

	/**
	 * Returns true when the local prediction/settled-state view says this block should not be targeted anymore.
	 * This lets gameplay ignore overlays that still linger visually after destruction has already committed.
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Prediction")
	bool IsBlockTargetSuppressed(const FChunkWorldResolvedBlockHit& ResolvedHit) const;

	/**
	 * Retargets a suppressed block to a still-live support block below it when possible.
	 * Used by interaction and damage paths so they do not keep hitting destroyed overlay cells.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool TryResolveTargetableBlockHit(const FChunkWorldResolvedBlockHit& CandidateHit, FChunkWorldResolvedBlockHit& OutResolvedHit) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Prediction", meta = (ClampMin = "0.01", UIMin = "0.01", ToolTip = "How long one predicted block state stays alive without an authoritative update."))
	float PredictionTimeoutSeconds = 2.0f;

	/** If true, draws a persistent on-screen stats block for local prediction and authoritative health activity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "If true, draws a persistent on-screen stats block for local prediction and authoritative health activity."))
	bool bShowDebugStats = false;

private:
	/** Lightweight debug snapshot for the most recent predicted or authoritative health request. */
	struct FLastHealthRequestDebugState
	{
		bool bHasRecord = false;
		bool bIsHealing = false;
		bool bCallSucceeded = false;
		float WorldTimeSeconds = 0.0f;
		FString PathName;
		FChunkWorldResolvedBlockHit ResolvedHit;
		int32 RequestedAmount = 0;
		FName RequestContextTag = NAME_None;
		FChunkWorldBlockHealthDeltaRequestResult Result;
	};

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
	void HandleObservedChunkWorldSettledTransition(AChunkWorldExtended* ChunkWorld, const FChunkWorldSettledBlockTransition& Transition);

	void PruneExpiredPredictions();
	void SchedulePredictionPrune();
	void HandlePredictionPruneTimer();
	bool ShouldRegisterPredictionNotifications() const;
	bool ValidateHealthRequest(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 Amount, bool bIsHealing) const;
	bool TryBuildPredictedHealthChangeResult(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 Amount, bool bIsHealing, float PredictionTimeSeconds, FChunkWorldBlockHealthDeltaResult& OutResult) const;
	bool DidTrackedHealthStateChange(const FChunkWorldBlockHealthDeltaResult* PreviousResult, const FChunkWorldBlockHealthDeltaResult& NewResult) const;
	bool TryApplyImmediateLocalDamageFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit, const FChunkWorldBlockHealthDeltaResult& DamageResult) const;
	void StorePredictedHealthChangeResult(const FChunkWorldResolvedBlockHit& ResolvedHit, const FChunkWorldBlockHealthDeltaResult& DamageResult);
	void SetBlockTargetSuppressed(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, bool bSuppressed);
	void BroadcastTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);
	void BindObservedChunkWorld(AChunkWorld* ChunkWorld);
	void BindObservedChunkWorlds();
	void UnbindObservedChunkWorlds();
	void HandleActorSpawned(AActor* SpawnedActor);
	void UpdateLastHealthRequestDebugState(
		const TCHAR* PathName,
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		int32 RequestedAmount,
		FName RequestContextTag,
		bool bIsHealing,
		const FChunkWorldBlockHealthDeltaRequestResult& RequestResult,
		bool bCallSucceeded);
	void DrawDebugStats(class UCanvas* Canvas, class APlayerController* DebugOwner);
	bool ShouldDrawDebugStatsForPlayer(const class APlayerController* DebugOwner) const;
	void MaybeLogDebugStats(const FString& Snapshot);
	static FPredictedBlockKey MakeKey(const FChunkWorldResolvedBlockHit& ResolvedHit);
	static FPredictedBlockKey MakeKey(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);

	TMap<FPredictedBlockKey, FChunkWorldBlockHealthDeltaResult> PredictedBlockStates;
	TMap<FPredictedBlockKey, int32> PendingPredictedRequestCountsByBlock;
	TSet<FPredictedBlockKey> SuppressedTargetBlocks;
	FPorismTrackedBlockStateChangedSignature TrackedBlockStateChangedEvent;
	TArray<TWeakObjectPtr<class AChunkWorldExtended>> ObservedChunkWorlds;
	FDelegateHandle ActorSpawnedHandle;
	FDelegateHandle DebugDrawDelegateHandle;
	FTimerHandle PredictionPruneTimerHandle;
	FLastHealthRequestDebugState LastPredictedHealthRequestDebugState;
	FLastHealthRequestDebugState LastAuthoritativeHealthRequestDebugState;
	float LastDebugStatsLogTimeSeconds = -1000.0f;
	FString LastDebugStatsLogSnapshot;
};
