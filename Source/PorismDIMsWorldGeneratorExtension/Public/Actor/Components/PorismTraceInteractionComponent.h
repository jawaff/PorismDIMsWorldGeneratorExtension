// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Actor/Interaction/PorismTraceInteractionTypes.h"
#include "PorismTraceInteractionComponent.generated.h"

class AChunkWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPorismTraceInteractionUpdated, const FPorismTraceInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPorismActorInteractionChanged, AActor*, Actor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldBlockInteractionChanged, const FChunkWorldBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldBlockInteractionUpdated, const FChunkWorldBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldBlockCustomDataMaterialized, const FChunkWorldBlockInteractionResult&, Result);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPorismInteractionResult, const FPorismTraceInteractionResult&, Result);

/**
 * Shared actor-component trace entry point that resolves actor and chunk-world block targets without project-specific gameplay logic.
 */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Porism Trace Interaction Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismTraceInteractionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates a reusable trace component that only updates on its timer or explicit trace requests. */
	UPorismTraceInteractionComponent();

	/** Returns the most recent generic trace result chosen by the interaction arbitration pass. */
	UFUNCTION(BlueprintCallable, Category = "Porism|Interaction")
	const FPorismTraceInteractionResult& GetLastTraceResult() const { return LastTraceResult; }

	/** Returns the last block-specific trace result. Pair this with HasActiveBlockInteraction() before treating it as current state. */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Interaction")
	FChunkWorldBlockInteractionResult GetLastBlockInteractionResult() const;

	/** Returns whether a block interaction target is currently active. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Interaction")
	bool HasActiveBlockInteraction() const { return bHasActiveBlockInteraction; }

	/** Returns the currently selected actor target when the trace resolved an interactable actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Interaction")
	AActor* GetCurrentBestInteractableActor() const { return LastTraceResult.TargetType == EPorismTraceInteractionTargetType::Actor ? LastTraceResult.InteractableActor : nullptr; }

	/** Returns whether the trace currently has any valid interaction target at all. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Interaction")
	bool HasValidInteractionTarget() const { return LastTraceResult.TargetType != EPorismTraceInteractionTargetType::None; }

	/** Returns the currently effective trace distance including optional third-person bonus. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Interaction")
	float GetTraceDistance() const { return GetEffectiveTraceDistance(); }

	/** Returns the base trace distance before any optional third-person bonus is applied. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Interaction")
	float GetBaseTraceDistance() const { return TraceDistance; }

	/** Sets the base trace distance and optionally performs an immediate refresh trace. */
	UFUNCTION(BlueprintCallable, Category = "Porism|Interaction")
	void SetTraceDistance(float NewTraceDistance, bool bForceImmediateTrace = true);

	/** Returns whether the third-person trace distance bonus is currently enabled. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Porism|Interaction")
	bool IsUsingThirdPersonTraceDistance() const { return bUseThirdPersonTraceDistance; }

	/** Toggles the third-person trace distance bonus and optionally performs an immediate refresh trace. */
	UFUNCTION(BlueprintCallable, Category = "Porism|Interaction")
	void SetUseThirdPersonTraceDistance(bool bInUseThirdPersonTraceDistance, bool bForceImmediateTrace = true);

	/** Forces one immediate trace outside the timer cadence. */
	UFUNCTION(BlueprintCallable, Category = "Porism|Interaction")
	void ForceTrace();

	/** Updates the actor and block trace channels used by the interaction trace. */
	UFUNCTION(BlueprintCallable, Category = "Porism|Interaction")
	void SetTraceChannels(TEnumAsByte<ECollisionChannel> NewTraceChannel, TEnumAsByte<ECollisionChannel> NewBlockTraceChannel);

	/** Executes interaction against the current actor target when the actor implements the shared interactable interface. */
	UFUNCTION(BlueprintCallable, Category = "Porism|Interaction")
	void Interact(FGameplayTag InteractionTag);

	UPROPERTY(BlueprintAssignable, Category = "Porism|Interaction")
	FOnPorismTraceInteractionUpdated OnTraceInteractionUpdated;

	UPROPERTY(BlueprintAssignable, Category = "Porism|Interaction")
	FOnPorismInteractionResult OnInteractionSucceeded;

	UPROPERTY(BlueprintAssignable, Category = "Porism|Interaction|Actor")
	FOnPorismActorInteractionChanged OnActorInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Porism|Interaction|Actor")
	FOnPorismActorInteractionChanged OnActorInteractionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Interaction")
	FOnChunkWorldBlockInteractionChanged OnBlockInteractionStarted;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Interaction")
	FOnChunkWorldBlockInteractionChanged OnBlockInteractionEnded;

	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Interaction")
	FOnChunkWorldBlockInteractionUpdated OnBlockInteractionUpdated;

	/** Broadcast when the currently focused block has materialized custom data for the first time observed by this component. */
	UPROPERTY(BlueprintAssignable, Category = "Block|ChunkWorld|Interaction")
	FOnChunkWorldBlockCustomDataMaterialized OnBlockCustomDataMaterialized;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Resolves the view point used by the trace. Override this in project code when the owner uses a custom camera source. */
	virtual bool GetInteractionTraceView(FVector& OutLocation, FRotator& OutRotation) const;

	/** Returns whether one resolved block result should be eligible to win arbitration as the active interaction target. */
	virtual bool ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const;

	/** Returns whether the block interaction payload changed enough to warrant transition/update delegates. */
	virtual bool DidBlockInteractionResultChange(bool bHadPreviousResult, const FChunkWorldBlockInteractionResult& PreviousResult, bool bHasNewResult, const FChunkWorldBlockInteractionResult& NewResult) const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ClampMin = "0.01", UIMin = "0.01", ToolTip = "How often the component refreshes its interaction trace while active."))
	float TraceInterval = 0.1f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Base interaction trace distance before any optional third-person bonus is added."))
	float TraceDistance = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Additional distance applied when third-person trace distance mode is enabled."))
	float ThirdPersonTraceDistanceBonus = 150.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ToolTip = "Collision channel used for actor-oriented interaction tracing."))
	TEnumAsByte<ECollisionChannel> TraceChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Interaction", meta = (ToolTip = "Optional separate collision channel used for chunk-world block tracing."))
	TEnumAsByte<ECollisionChannel> BlockTraceChannel;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ToolTip = "If true, the interaction trace uses complex per-triangle collision instead of simple collision."))
	bool bTraceComplex = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ToolTip = "If true, the component ignores its owner during tracing."))
	bool bIgnoreOwner = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ToolTip = "If true, only a locally controlled owning pawn runs the trace. Disable for debugging or non-pawn owners."))
	bool bOnlyOwner = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction|Debug")
	bool bDebugDrawTrace = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction|Debug", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugDrawDuration = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction|Debug", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float DebugDrawThickness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "If true, draws a block-sized debug cube around the resolved block target. Green means success, red means lookup failure."))
	bool bDebugDrawBlockLookup = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ToolTip = "Temporary diagnostic toggle. Logs which step in shared block-hit lookup failed or succeeded."))
	bool bLogBlockLookupDiagnostics = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Debug", meta = (ClampMin = "1.0", UIMin = "1.0", ToolTip = "Multiplier applied to the debug cube size so it draws slightly larger than the represented block mesh."))
	float DebugBlockCubeScale = 1.05f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Porism|Interaction", meta = (ToolTip = "Optional interfaces that mark actor hits as interactable for the generic actor branch. The shared Porism interactable interface is included by default."))
	TArray<TSubclassOf<UInterface>> InteractableInterfaces;

private:
	FTimerHandle TraceHandle;
	FPorismTraceInteractionResult LastTraceResult;
	FChunkWorldBlockInteractionResult LastBlockInteractionResult;
	bool bHasActiveBlockInteraction = false;
	bool bUseThirdPersonTraceDistance = false;
	float ActiveTraceDistance = 0.0f;
	FName LastTraceGateReason = NAME_None;
	mutable bool bHasLastBlockDebugDraw = false;
	mutable FIntVector LastBlockDebugDrawPos = FIntVector::ZeroValue;
	mutable FColor LastBlockDebugDrawColor = FColor::Transparent;
	mutable double LastBlockDebugDrawTimeSeconds = 0.0;

	void PerformTrace();
	void RefreshActiveTraceDistance();
	float GetEffectiveTraceDistance() const;
	void LogTraceGateState(FName Reason);
	bool CanDrawBlockLookupDebug(const AChunkWorld* ChunkWorld) const;
	void DrawDebugBlockLookupCube(const AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FColor& Color) const;
	void BroadcastInteractionSuccess(const FPorismTraceInteractionResult& Result);
	void BroadcastInteractionTransition(const FPorismTraceInteractionResult& PreviousResult, const FPorismTraceInteractionResult& NewResult, bool bHadPreviousBlockResult, const FChunkWorldBlockInteractionResult& PreviousBlockResult, bool bHasNewBlockResult, const FChunkWorldBlockInteractionResult& NewBlockResult);
	bool IsInteractableActor(AActor* Actor) const;
	bool TryBuildActorResult(const FHitResult& Hit, FPorismTraceInteractionResult& InOutResult) const;
	bool TryBuildBlockResult(const FVector& TraceDirection, const FHitResult& Hit, FChunkWorldBlockInteractionResult& InOutResult) const;
	void LogBlockLookupDiagnostic(const TCHAR* Stage, const FHitResult* Hit, const FIntVector* BlockWorldPos, const TCHAR* Extra = TEXT("")) const;
	bool IsResultCloser(const FVector& TraceStart, const FPorismTraceInteractionResult& CandidateResult, const FPorismTraceInteractionResult& CurrentBestResult) const;
	bool IsBlockResultCloser(const FVector& TraceStart, const FChunkWorldBlockInteractionResult& CandidateResult, const FPorismTraceInteractionResult& CurrentBestResult) const;
	bool ShouldRunTrace() const;
	bool CanInteractWithActor(AActor* Actor, const FGameplayTag& InteractionTag) const;
	void ExecuteActorInteraction(AActor* Actor, const FGameplayTag& InteractionTag);
	void EvaluateBlockCustomDataMaterialization(const FChunkWorldBlockInteractionResult& BlockResult);
	void ResetTrackedBlockCustomDataMaterialization();

	UFUNCTION(Server, Reliable)
	void ServerInteractActor(FGameplayTag InteractionTag, AActor* TargetActor);

	UFUNCTION(Client, Reliable)
	void ClientConfirmActorInteraction(AActor* TargetActor, FGameplayTag InteractionTag);

	FIntVector LastMaterializationTrackedBlockWorldPos = FIntVector::ZeroValue;
	bool bHasTrackedMaterializationBlock = false;
	bool bWasTrackedBlockCustomDataMaterialized = false;
};
