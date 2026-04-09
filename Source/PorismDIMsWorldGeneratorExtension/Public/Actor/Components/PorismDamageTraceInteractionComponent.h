// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/Components/PorismTraceInteractionComponent.h"
#include "Actor/Interaction/PorismDamageTraceInteractionTypes.h"
#include "PorismDamageTraceInteractionComponent.generated.h"

class UPorismPredictedBlockStateComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldDamageBlockInteractionChanged, const FChunkWorldDamageBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldDamageBlockInteractionUpdated, const FChunkWorldDamageBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldDamageBlockCustomDataInitialized, const FChunkWorldDamageBlockInteractionResult&, Result);

/**
 * Shared interaction component that builds on the generic trace loop with optional health-aware block payloads and damage application helpers.
 */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Porism Damage Trace Interaction Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismDamageTraceInteractionComponent : public UPorismTraceInteractionComponent
{
	GENERATED_BODY()

public:
	/** Creates a damage-aware interaction component that only selects blocks when they support the shared damage schema family. */
	UPorismDamageTraceInteractionComponent();

	/** Returns the last health-aware block interaction payload. Pair this with HasActiveDamageBlockInteraction() before treating it as current state. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Damage")
	FChunkWorldDamageBlockInteractionResult GetLastDamageBlockInteractionResult() const;

	/** Returns whether a damage-capable block interaction target is currently active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	bool HasActiveDamageBlockInteraction() const { return FocusedDamageState.bIsActive; }

	/** Returns whether the current active block target can accept shared damage under the health schema family. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Damage")
	bool CanApplyDamageToCurrentBlock() const;

	/** Applies damage to the current active block target when it supports the shared health schema family. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Damage")
	bool ApplyDamageToCurrentBlock(int32 DamageAmount);

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Damage")
	FOnChunkWorldDamageBlockInteractionChanged OnDamageBlockInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Damage")
	FOnChunkWorldDamageBlockInteractionChanged OnDamageBlockInteractionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Damage")
	FOnChunkWorldDamageBlockInteractionUpdated OnDamageBlockInteractionUpdated;

	/** Broadcast when the focused damage-capable block has runtime custom data initialized. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Damage")
	FOnChunkWorldDamageBlockCustomDataInitialized OnDamageBlockCustomDataInitialized;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const override;
	virtual bool DidBlockInteractionResultChange(bool bHadPreviousResult, const FChunkWorldBlockInteractionResult& PreviousResult, bool bHasNewResult, const FChunkWorldBlockInteractionResult& NewResult) const override;

private:
	struct FFocusedDamageBlockState
	{
		bool bIsActive = false;
		bool bHasAnnouncedInitialized = false;
		TWeakObjectPtr<AChunkWorld> ChunkWorld;
		FIntVector BlockWorldPos = FIntVector::ZeroValue;
		FChunkWorldDamageBlockInteractionResult Payload;
	};

	FFocusedDamageBlockState FocusedDamageState;
	TWeakObjectPtr<UPorismPredictedBlockStateComponent> PredictedBlockStateComponent;

	UFUNCTION()
	void HandleBlockInteractionStarted(const FChunkWorldBlockInteractionResult& Result);

	UFUNCTION()
	void HandleBlockInteractionEnded(const FChunkWorldBlockInteractionResult& Result);

	UFUNCTION()
	void HandleBlockInteractionUpdated(const FChunkWorldBlockInteractionResult& Result);

	UFUNCTION()
	void HandleBlockCustomDataInitialized(const FChunkWorldBlockInteractionResult& Result);

	void HandleTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos);
	void RefreshFocusedDamageState();
	void ResetFocusedDamageState();
	bool IsSameFocusedBlock(const FFocusedDamageBlockState& State, AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos) const;
	bool HasInitializedDisplayData(const FChunkWorldDamageBlockInteractionResult& Result) const;
	bool DidDamagePayloadChange(const FChunkWorldDamageBlockInteractionResult& PreviousResult, const FChunkWorldDamageBlockInteractionResult& NewResult) const;
	void EmitDamageStateTransition(const FFocusedDamageBlockState& PreviousState, FFocusedDamageBlockState& NewState);
	void BindPredictedBlockStateComponent();
	void UnbindPredictedBlockStateComponent();
	bool TryBuildDamageBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult, FChunkWorldDamageBlockInteractionResult& OutResult) const;
	UPorismPredictedBlockStateComponent* GetPredictedBlockStateComponent() const;
};
