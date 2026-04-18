// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/Components/PorismTraceInteractionComponent.h"
#include "Actor/Interaction/PorismHealthTraceInteractionTypes.h"
#include "PorismHealthInteractionComponent.generated.h"

class UPorismPredictedBlockStateComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldHealthBlockInteractionChanged, const FChunkWorldHealthBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldHealthBlockInteractionUpdated, const FChunkWorldHealthBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldHealthBlockCustomDataInitialized, const FChunkWorldHealthBlockInteractionResult&, Result);

/**
 * Shared interaction component that builds on the generic trace loop with optional health-aware block payloads.
 */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Porism Health Interaction Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismHealthInteractionComponent : public UPorismTraceInteractionComponent
{
	GENERATED_BODY()

public:
	/** Creates a health-aware interaction component that only selects blocks when they support the shared health schema family. */
	UPorismHealthInteractionComponent();

	/** Returns the last health-aware block interaction payload. Pair this with HasActiveHealthBlockInteraction() before treating it as current state. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Health")
	FChunkWorldHealthBlockInteractionResult GetLastHealthBlockInteractionResult() const;

	/** Returns whether a health-aware block interaction target is currently active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Health")
	bool HasActiveHealthBlockInteraction() const { return FocusedHealthState.bIsActive; }

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Health")
	FOnChunkWorldHealthBlockInteractionChanged OnHealthBlockInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Health")
	FOnChunkWorldHealthBlockInteractionChanged OnHealthBlockInteractionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Health")
	FOnChunkWorldHealthBlockInteractionUpdated OnHealthBlockInteractionUpdated;

	/** Broadcast when the focused health-aware block has runtime custom data initialized. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Health")
	FOnChunkWorldHealthBlockCustomDataInitialized OnHealthBlockCustomDataInitialized;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual bool ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const override;
	virtual bool DidBlockInteractionResultChange(bool bHadPreviousResult, const FChunkWorldBlockInteractionResult& PreviousResult, bool bHasNewResult, const FChunkWorldBlockInteractionResult& NewResult) const override;

	/** If true, draws a persistent on-screen stats block for the current health interaction target and payload state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "If true, draws a persistent on-screen stats block for the current health interaction target and payload state."))
	bool bShowDebugStats = false;

private:
	struct FFocusedHealthBlockState
	{
		bool bIsActive = false;
		bool bHasAnnouncedInitialized = false;
		TWeakObjectPtr<AChunkWorld> ChunkWorld;
		FIntVector BlockWorldPos = FIntVector::ZeroValue;
		FChunkWorldHealthBlockInteractionResult Payload;
	};

	FFocusedHealthBlockState FocusedHealthState;
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
	void RefreshFocusedHealthState();
	void ResetFocusedHealthState();
	bool IsSameFocusedBlock(const FFocusedHealthBlockState& State, AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos) const;
	bool HasInitializedDisplayData(const FChunkWorldHealthBlockInteractionResult& Result) const;
	bool DidHealthPayloadChange(const FChunkWorldHealthBlockInteractionResult& PreviousResult, const FChunkWorldHealthBlockInteractionResult& NewResult) const;
	void EmitHealthStateTransition(const FFocusedHealthBlockState& PreviousState, FFocusedHealthBlockState& NewState);
	void BindPredictedBlockStateComponent();
	void UnbindPredictedBlockStateComponent();
	void DrawDebugStats(class UCanvas* Canvas, class APlayerController* DebugOwner);
	bool ShouldDrawDebugStatsForPlayer(const class APlayerController* DebugOwner) const;
	void MaybeLogDebugStats(const FString& Snapshot);
	bool TryBuildHealthBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult, FChunkWorldHealthBlockInteractionResult& OutResult) const;
	UPorismPredictedBlockStateComponent* GetPredictedBlockStateComponent() const;

	FDelegateHandle DebugDrawDelegateHandle;
	float LastDebugStatsLogTimeSeconds = -1000.0f;
	FString LastDebugStatsLogSnapshot;
};
