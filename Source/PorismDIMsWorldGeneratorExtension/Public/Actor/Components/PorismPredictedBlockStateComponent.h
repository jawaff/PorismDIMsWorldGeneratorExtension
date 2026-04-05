// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/Hit/ChunkWorldBlockDamageTypes.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "PorismPredictedBlockStateComponent.generated.h"

/**
 * Local-only prediction cache for block health state keyed by resolved chunk world block hits.
 */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Porism Predicted Block State Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismPredictedBlockStateComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPorismPredictedBlockStateComponent();

	/**
	 * Retires prediction state for one authoritative block update on the supplied chunk world.
	 */
	static void NotifyAuthoritativeBlockStateUpdated(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);

	/**
	 * Stores one new predicted damage result for later UI/gameplay reads.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	void StorePredictedDamageResult(const FChunkWorldResolvedBlockHit& ResolvedHit, const FChunkWorldBlockDamageResult& DamageResult);

	/**
	 * Clears a stored prediction because authoritative state for the same block has arrived.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	void ClearPredictionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit);

	/**
	 * Returns the current health state for a resolved block, preferring a live prediction and falling back to authoritative custom data.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Prediction")
	bool TryGetCurrentHealthState(const FChunkWorldResolvedBlockHit& ResolvedHit, int32& OutHealth, bool& bOutInvincible, bool& bOutUsingPrediction, FGameplayTag& OutBlockTypeName);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Prediction", meta = (ClampMin = "0.01", UIMin = "0.01", ToolTip = "How long one predicted block state stays alive without an authoritative update."))
	float PredictionTimeoutSeconds = 10.0f;

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

	void PruneExpiredPredictions();
	bool ShouldRegisterPredictionNotifications() const;
	static FPredictedBlockKey MakeKey(const FChunkWorldResolvedBlockHit& ResolvedHit);
	static FPredictedBlockKey MakeKey(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);

	TMap<FPredictedBlockKey, FChunkWorldBlockDamageResult> PredictedBlockStates;
};
