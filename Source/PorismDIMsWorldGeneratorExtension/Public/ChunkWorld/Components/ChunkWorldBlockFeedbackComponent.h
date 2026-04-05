// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorldBlockFeedbackComponent.generated.h"

class UNiagaraSystem;
class USoundBase;

/**
 * Replicates chunk-world block feedback so nearby clients and listen-server players hear and see authoritative block hit events.
 */
UCLASS(ClassGroup = (Block), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "ChunkWorld Block Feedback Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldBlockFeedbackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * Creates a replicated feedback component that multicasts authoritative block hit and destroy feedback.
	 */
	UChunkWorldBlockFeedbackComponent();

	/**
	 * Multicasts one authoritative feedback event and applies a local distance filter before spawning sound and Niagara.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|ChunkWorld|Feedback")
	bool BroadcastFeedbackAtLocation(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem = nullptr);

protected:
	/** Maximum local playback distance for replicated block feedback. Set to zero to disable local distance filtering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Feedback", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum local playback distance for replicated block feedback. Set to zero to disable local distance filtering."))
	float FeedbackCullDistance = 5000.0f;

private:
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastBroadcastFeedback(const FVector_NetQuantize& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem);

	bool ShouldPlayFeedbackAtLocation(const FVector& WorldLocation) const;
	void PlayFeedbackAtLocation(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const;
};
