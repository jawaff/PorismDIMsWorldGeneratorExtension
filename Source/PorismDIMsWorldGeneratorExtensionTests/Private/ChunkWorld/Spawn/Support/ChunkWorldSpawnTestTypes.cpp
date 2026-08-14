// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Spawn/Support/ChunkWorldSpawnTestTypes.h"

void UChunkWorldSpawnTestSourceProvider::GatherSpawnCandidates(
	const FChunkWorldSpawnRequest& Request,
	const FChunkWorldSpawnQueryContext& Context,
	TArray<FChunkWorldSpawnCandidate>& OutCandidates) const
{
	++GatherCallCount;
	OutCandidates.Append(Candidates);

	if (bCancelTicketDuringGather && ReentrantComponent.IsValid() && ReentrantTicketId.IsValid())
	{
		FChunkWorldSpawnTicketHandle Ticket;
		Ticket.TicketId = ReentrantTicketId;
		ReentrantComponent->CancelSpawnTicket(Ticket);
	}
}

bool UChunkWorldSpawnTestSourceProvider::TryBuildSpawnCandidateCacheKey(const FChunkWorldSpawnRequest& Request, FString& OutCacheKey) const
{
	if (!bCacheable)
	{
		OutCacheKey.Reset();
		return false;
	}

	OutCacheKey = CacheKey;
	return true;
}

void UChunkWorldSpawnTestComponent::Advance()
{
	TickComponent(0.0f, LEVELTICK_All, nullptr);
}

AChunkWorldSpawnTestActor::AChunkWorldSpawnTestActor()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}
