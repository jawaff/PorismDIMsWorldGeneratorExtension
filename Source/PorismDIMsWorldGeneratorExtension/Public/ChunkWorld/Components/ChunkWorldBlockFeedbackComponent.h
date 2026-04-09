// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/Hit/ChunkWorldBlockHitTypes.h"
#include "ChunkWorldBlockFeedbackComponent.generated.h"

class UNiagaraSystem;
class USoundBase;

UENUM()
enum class EChunkWorldBlockFeedbackKind : uint8
{
	Hit,
	Destroy
};

/** Lightweight local playback cache entry used to suppress predicted/authoritative duplicate feedback bursts. */
struct FRecentChunkWorldBlockFeedback
{
	TWeakObjectPtr<AChunkWorld> ChunkWorld;
	FIntVector BlockWorldPos = FIntVector::ZeroValue;
	FVector WorldLocation = FVector::ZeroVector;
	TObjectPtr<USoundBase> Sound = nullptr;
	TObjectPtr<UNiagaraSystem> NiagaraSystem = nullptr;
	EChunkWorldBlockFeedbackKind Kind = EChunkWorldBlockFeedbackKind::Hit;
	float TimeSeconds = 0.0f;
};

/**
 * Replicates chunk-world block feedback so nearby clients and listen-server players hear and see authoritative block hit events.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "ChunkWorld Block Feedback Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Creates a replicated feedback component that owns immediate-local and authoritative block feedback delivery.
	 */
	UChunkWorldBlockFeedbackComponent();

	/**
	 * Plays local-only predicted hit feedback for the initiating process when the caller has a valid resolved block.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Feedback")
	bool RequestImmediateLocalHitFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit);

	/**
	 * Plays local-only predicted destroy feedback for the initiating process when the caller has a valid resolved block.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Feedback")
	bool RequestImmediateLocalDestroyFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit);

	/**
	 * Broadcasts one authoritative non-lethal block hit feedback event to every process, including the server.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Feedback")
	bool BroadcastAuthoritativeHitFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit);

	/**
	 * Broadcasts one authoritative destroyed-block feedback event to every process, including the server.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Feedback")
	bool BroadcastAuthoritativeDestroyFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit);

protected:
	/** Maximum local playback distance for replicated block feedback. Set to zero to disable local distance filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Feedback", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum local playback distance for replicated block feedback. Set to zero to disable local distance filtering."))
	float FeedbackCullDistance = 5000.0f;

	/** Time window used to suppress the same local block feedback from playing twice when predicted and authoritative paths both request it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Feedback", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Time window used to suppress the same local block feedback from playing twice when predicted and authoritative paths both request it."))
	float FeedbackDeduplicationWindowSeconds = 0.15f;

	/** Distance tolerance used to match repeated block feedback requests for the same local event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Feedback", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Distance tolerance used to match repeated block feedback requests for the same local event."))
	float FeedbackDeduplicationDistance = 32.0f;

private:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBroadcastFeedback(const FVector_NetQuantize& WorldLocation, const FIntVector& BlockWorldPos, EChunkWorldBlockFeedbackKind Kind, USoundBase* Sound, UNiagaraSystem* NiagaraSystem);

	bool ShouldPlayFeedbackAtLocation(const FVector& WorldLocation) const;
	bool ShouldSuppressDuplicateFeedback(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, EChunkWorldBlockFeedbackKind Kind, const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const;
	void RememberFeedbackPlayback(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, EChunkWorldBlockFeedbackKind Kind, const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const;
	bool TryResolveHitFeedbackAssets(const FChunkWorldResolvedBlockHit& ResolvedHit, USoundBase*& OutSound) const;
	bool TryResolveDestroyFeedbackAssets(const FChunkWorldResolvedBlockHit& ResolvedHit, USoundBase*& OutSound, UNiagaraSystem*& OutNiagaraSystem) const;
	FVector ResolveFeedbackLocation(const FChunkWorldResolvedBlockHit& ResolvedHit) const;
	bool CanRequestImmediateLocalFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit) const;
	bool CanBroadcastAuthoritativeFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit) const;
	bool PlayBlockFeedback(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, EChunkWorldBlockFeedbackKind Kind, const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const;

	/** Local recent-playback cache used only to suppress short-lived duplicate feedback requests on this machine. */
	mutable TArray<FRecentChunkWorldBlockFeedback> RecentFeedbackPlaybacks;
};
