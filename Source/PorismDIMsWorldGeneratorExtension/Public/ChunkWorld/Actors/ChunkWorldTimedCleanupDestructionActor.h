// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChunkWorld/Actors/ChunkWorldDestructionActorInterface.h"

#include "ChunkWorldTimedCleanupDestructionActor.generated.h"

class USceneComponent;

/**
 * Minimal reusable destruction actor that only accepts one destruction trigger and destroys itself after a configurable duration.
 * Use this as a generic base when a block needs transient actor presentation without Chaos or project-specific behavior.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldTimedCleanupDestructionActor
	: public AActor
	, public IChunkWorldDestructionActorInterface
{
	GENERATED_BODY()

public:
	/** Creates a lightweight destruction actor with a shared scene root and timed self-cleanup behavior. */
	AChunkWorldTimedCleanupDestructionActor();

	/** Framework-owned trigger entry point for one block-destruction presentation request; Blueprint overrides must call parent. */
	virtual void TriggerBlockDestruction_Implementation(const FChunkWorldBlockDestructionRequest& Request) override;

	/**
	 * Framework-owned trigger entry point used by the shared chunk-world destruction pipeline.
	 * This always runs the reusable base startup path even when a Blueprint subclass also implements the interface event.
	 */
	void ExecuteFrameworkDestructionTrigger(const FChunkWorldBlockDestructionRequest& Request);

	/** Returns the most recent destruction request accepted by this actor. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Destruction")
	const FChunkWorldBlockDestructionRequest& GetLastDestructionRequest() const { return LastDestructionRequest; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Local per-process presentation hook that fires after the request is accepted and the actor transform is updated. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Block|ChunkWorld|Destruction")
	void ReceiveDestructionTriggered(const FChunkWorldBlockDestructionRequest& Request);

	/** Shared root used by subclasses that want to attach temporary presentation components. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction", meta = (AllowPrivateAccess = "true", ToolTip = "Scene root used by the generic timed cleanup destruction actor so subclasses can attach transient presentation components."))
	TObjectPtr<USceneComponent> DestructionRoot = nullptr;

	/** Lifetime used after the destruction trigger before this transient actor destroys itself. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|ChunkWorld|Destruction", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Lifetime used after the destruction trigger before this transient destruction actor destroys itself."))
	float CleanupDelaySeconds = 10.0f;

	/** Most recent accepted destruction request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	FChunkWorldBlockDestructionRequest LastDestructionRequest;

	/** True once the actor has already accepted its one destruction request. */
	UPROPERTY(BlueprintReadOnly, Category = "Block|ChunkWorld|Destruction")
	bool bHasTriggeredDestruction = false;

private:
	/** Accepts one destruction trigger, optionally recording the replicated trigger state on the authority first. */
	void AcceptDestructionTrigger(const FChunkWorldBlockDestructionRequest& Request, bool bRecordReplicatedTriggerState);

	/** Replays the authoritative trigger request locally once the replicated trigger state arrives for replicated-actor delivery. */
	UFUNCTION()
	void OnRep_ReplicatedTriggerState();

	/** Shared one-shot replicated trigger payload so replicated actor instances can run the same local presentation start path once. */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedTriggerState)
	FChunkWorldReplicatedDestructionTriggerState ReplicatedTriggerState;
};
