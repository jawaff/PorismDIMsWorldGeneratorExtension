// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldBlockSwapReplicationProxy.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Components/ChunkWorldBlockSwapComponent.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldBlockSwapReplicationProxy, Log, All);

AChunkWorldBlockSwapReplicationProxy::AChunkWorldBlockSwapReplicationProxy()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
}

void AChunkWorldBlockSwapReplicationProxy::InitializeForChunkWorld(AChunkWorldExtended* InChunkWorld)
{
	ChunkWorld = InChunkWorld;
}

void AChunkWorldBlockSwapReplicationProxy::BroadcastSwapEntered(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(
		LogChunkWorldBlockSwapReplicationProxy,
		Log,
		TEXT("SwapProxy ServerEntered Proxy=%s ChunkWorld=%s Block=%s SwapActor=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ChunkWorld.Get()),
		*SwapItem.BlockWorldPos.ToString(),
		*GetNameSafe(SwapItem.SwapActor.Get()));
	MulticastSwapEntered(SwapItem);
}

void AChunkWorldBlockSwapReplicationProxy::BroadcastSwapChanged(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(
		LogChunkWorldBlockSwapReplicationProxy,
		Log,
		TEXT("SwapProxy ServerChanged Proxy=%s ChunkWorld=%s Block=%s SwapActor=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ChunkWorld.Get()),
		*SwapItem.BlockWorldPos.ToString(),
		*GetNameSafe(SwapItem.SwapActor.Get()));
	MulticastSwapChanged(SwapItem);
}

void AChunkWorldBlockSwapReplicationProxy::BroadcastSwapExited(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(
		LogChunkWorldBlockSwapReplicationProxy,
		Log,
		TEXT("SwapProxy ServerExited Proxy=%s ChunkWorld=%s Block=%s SwapActor=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ChunkWorld.Get()),
		*SwapItem.BlockWorldPos.ToString(),
		*GetNameSafe(SwapItem.SwapActor.Get()));
	MulticastSwapExited(SwapItem);
}

void AChunkWorldBlockSwapReplicationProxy::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AChunkWorldBlockSwapReplicationProxy, ChunkWorld);
}

void AChunkWorldBlockSwapReplicationProxy::OnRep_ChunkWorld()
{
	DrainQueuedSwapEvents();
}

void AChunkWorldBlockSwapReplicationProxy::MulticastSwapEntered_Implementation(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (HasAuthority())
	{
		return;
	}

	HandleClientSwapEvent(EQueuedSwapEventType::Entered, SwapItem);
}

void AChunkWorldBlockSwapReplicationProxy::MulticastSwapChanged_Implementation(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (HasAuthority())
	{
		return;
	}

	HandleClientSwapEvent(EQueuedSwapEventType::Changed, SwapItem);
}

void AChunkWorldBlockSwapReplicationProxy::MulticastSwapExited_Implementation(const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (HasAuthority())
	{
		return;
	}

	HandleClientSwapEvent(EQueuedSwapEventType::Exited, SwapItem);
}

void AChunkWorldBlockSwapReplicationProxy::HandleClientSwapEvent(EQueuedSwapEventType EventType, const FReplicatedChunkWorldSwapItem& SwapItem)
{
	if (UChunkWorldBlockSwapComponent* SwapComponent = GetSwapComponent())
	{
		UE_LOG(
			LogChunkWorldBlockSwapReplicationProxy,
			Log,
			TEXT("SwapProxy ClientReceived Proxy=%s ChunkWorld=%s Event=%d Block=%s SwapActor=%s"),
			*GetNameSafe(this),
			*GetNameSafe(ChunkWorld.Get()),
			static_cast<int32>(EventType),
			*SwapItem.BlockWorldPos.ToString(),
			*GetNameSafe(SwapItem.SwapActor.Get()));

		switch (EventType)
		{
		case EQueuedSwapEventType::Entered:
			SwapComponent->HandleNetworkSwapEntered(SwapItem);
			break;
		case EQueuedSwapEventType::Changed:
			SwapComponent->HandleNetworkSwapChanged(SwapItem);
			break;
		case EQueuedSwapEventType::Exited:
			SwapComponent->HandleNetworkSwapExited(SwapItem);
			break;
		default:
			break;
		}
		return;
	}

	FQueuedSwapEvent& PendingEvent = PendingSwapEvents.AddDefaulted_GetRef();
	PendingEvent.Type = EventType;
	PendingEvent.SwapItem = SwapItem;
	UE_LOG(
		LogChunkWorldBlockSwapReplicationProxy,
		Log,
		TEXT("SwapProxy DeferredClientEvent Proxy=%s Event=%d Block=%s Pending=%d"),
		*GetNameSafe(this),
		static_cast<int32>(EventType),
		*SwapItem.BlockWorldPos.ToString(),
		PendingSwapEvents.Num());
}

void AChunkWorldBlockSwapReplicationProxy::DrainQueuedSwapEvents()
{
	if (PendingSwapEvents.IsEmpty())
	{
		return;
	}

	UChunkWorldBlockSwapComponent* SwapComponent = GetSwapComponent();
	if (SwapComponent == nullptr)
	{
		return;
	}

	TArray<FQueuedSwapEvent> QueuedEvents = MoveTemp(PendingSwapEvents);
	PendingSwapEvents.Reset();
	for (const FQueuedSwapEvent& PendingEvent : QueuedEvents)
	{
		switch (PendingEvent.Type)
		{
		case EQueuedSwapEventType::Entered:
			SwapComponent->HandleNetworkSwapEntered(PendingEvent.SwapItem);
			break;
		case EQueuedSwapEventType::Changed:
			SwapComponent->HandleNetworkSwapChanged(PendingEvent.SwapItem);
			break;
		case EQueuedSwapEventType::Exited:
			SwapComponent->HandleNetworkSwapExited(PendingEvent.SwapItem);
			break;
		default:
			break;
		}
	}
}

UChunkWorldBlockSwapComponent* AChunkWorldBlockSwapReplicationProxy::GetSwapComponent() const
{
	return ChunkWorld != nullptr ? ChunkWorld->GetBlockSwapComponent() : nullptr;
}
