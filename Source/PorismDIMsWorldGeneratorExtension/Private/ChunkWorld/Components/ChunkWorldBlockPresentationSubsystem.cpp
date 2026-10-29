// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockPresentationSubsystem.h"
#include "ChunkWorld/ChunkWorld.h"
#include "Engine/World.h"

void UChunkWorldBlockPresentationSubsystem::Publish(const FChunkWorldBlockPresentation& Event)
{
	check(IsInGameThread());
	if (GetWorld()->GetNetMode() != NM_Client && IsValid(Event.Result.ChunkWorld)
		&& Event.Result.ChunkWorld->HasAuthority() && Event.Result.ChunkWorld->GetWorld() == GetWorld()
		&& !Event.Point.ContainsNaN() && !Event.Normal.ContainsNaN()) OnCommitted.Broadcast(Event);
}

bool UChunkWorldBlockPresentationSubsystem::HasConsumer(const UWorld* World)
{
	const auto* Sink = World ? World->GetSubsystem<UChunkWorldBlockPresentationSubsystem>() : nullptr;
	return Sink && (Sink->bClaimed || Sink->OnCommitted.IsBound());
}

void UChunkWorldBlockPresentationSubsystem::Deinitialize()
{
	OnCommitted.Clear();
	Super::Deinitialize();
}
