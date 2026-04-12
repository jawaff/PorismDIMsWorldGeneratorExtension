// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ChunkWorld/ChunkWorldBlockSwapTypes.h"
#include "ChunkWorldBlockSwapReplicationProxy.generated.h"

class AChunkWorldExtended;
class UChunkWorldBlockSwapComponent;

/** Lightweight replicated courier that delivers swap enter/change/exit events independently of Porism's chunk-world actor transport. */
UCLASS(NotBlueprintable)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldBlockSwapReplicationProxy : public AActor
{
	GENERATED_BODY()

public:
	AChunkWorldBlockSwapReplicationProxy();

	/** Binds the proxy to the authoritative chunk world that owns the swap presentation runtime. */
	void InitializeForChunkWorld(AChunkWorldExtended* InChunkWorld);

	/** Broadcasts one authoritative swap-enter event to remote clients. */
	void BroadcastSwapEntered(const FReplicatedChunkWorldSwapItem& SwapItem);

	/** Broadcasts one authoritative swap-change event to remote clients. */
	void BroadcastSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem);

	/** Broadcasts one authoritative swap-exit event to remote clients. */
	void BroadcastSwapExited(const FReplicatedChunkWorldSwapItem& SwapItem);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	enum class EQueuedSwapEventType : uint8
	{
		Entered,
		Changed,
		Exited,
	};

	struct FQueuedSwapEvent
	{
		EQueuedSwapEventType Type = EQueuedSwapEventType::Entered;
		FReplicatedChunkWorldSwapItem SwapItem;
	};

	UFUNCTION()
	void OnRep_ChunkWorld();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSwapEntered(const FReplicatedChunkWorldSwapItem& SwapItem);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSwapExited(const FReplicatedChunkWorldSwapItem& SwapItem);

	void HandleClientSwapEvent(EQueuedSwapEventType EventType, const FReplicatedChunkWorldSwapItem& SwapItem);
	void DrainQueuedSwapEvents();
	UChunkWorldBlockSwapComponent* GetSwapComponent() const;

	/** Replicated owning chunk world used to route client-delivered swap events into the local presentation component. */
	UPROPERTY(ReplicatedUsing = OnRep_ChunkWorld)
	TObjectPtr<AChunkWorldExtended> ChunkWorld = nullptr;

	/** Swap events received before the owning chunk world reference resolves on the client. */
	TArray<FQueuedSwapEvent> PendingSwapEvents;
};
