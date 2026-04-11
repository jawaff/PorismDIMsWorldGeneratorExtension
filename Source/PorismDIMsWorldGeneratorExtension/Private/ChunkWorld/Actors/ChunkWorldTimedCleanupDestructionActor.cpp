// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldTimedCleanupDestructionActor.h"

#include "Components/SceneComponent.h"

AChunkWorldTimedCleanupDestructionActor::AChunkWorldTimedCleanupDestructionActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	DestructionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DestructionRoot"));
	SetRootComponent(DestructionRoot);

	InitialLifeSpan = 0.0f;
}

void AChunkWorldTimedCleanupDestructionActor::TriggerBlockDestruction_Implementation(const FChunkWorldBlockDestructionRequest& Request)
{
	if (bHasTriggeredDestruction)
	{
		return;
	}

	bHasTriggeredDestruction = true;
	LastDestructionRequest = Request;

	SetActorTransform(Request.SpawnTransform);
	ReceiveDestructionTriggered(Request);

	if (CleanupDelaySeconds > 0.0f)
	{
		SetLifeSpan(CleanupDelaySeconds);
		return;
	}

	Destroy();
}
