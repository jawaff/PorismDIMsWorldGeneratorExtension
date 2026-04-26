// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldTimedCleanupDestructionActor.h"

#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"

AChunkWorldTimedCleanupDestructionActor::AChunkWorldTimedCleanupDestructionActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	DestructionRoot = CreateDefaultSubobject<USceneComponent>(TEXT("DestructionRoot"));
	SetRootComponent(DestructionRoot);

	InitialLifeSpan = 0.0f;
}

void AChunkWorldTimedCleanupDestructionActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AChunkWorldTimedCleanupDestructionActor, ReplicatedTriggerState);
}

void AChunkWorldTimedCleanupDestructionActor::TriggerBlockDestruction_Implementation(const FChunkWorldBlockDestructionRequest& Request)
{
	ExecuteFrameworkDestructionTrigger(Request);
}

void AChunkWorldTimedCleanupDestructionActor::ExecuteFrameworkDestructionTrigger(const FChunkWorldBlockDestructionRequest& Request)
{
	AcceptDestructionTrigger(Request, HasAuthority());
}

void AChunkWorldTimedCleanupDestructionActor::AcceptDestructionTrigger(const FChunkWorldBlockDestructionRequest& Request, bool bRecordReplicatedTriggerState)
{
	if (bHasTriggeredDestruction)
	{
		return;
	}

	bHasTriggeredDestruction = true;
	LastDestructionRequest = Request;

	if (bRecordReplicatedTriggerState && HasAuthority())
	{
		ReplicatedTriggerState.Request = Request;
		++ReplicatedTriggerState.TriggerSerial;
		ForceNetUpdate();
	}

	SetActorTransform(Request.SpawnTransform);
	ReceiveDestructionTriggered(Request);

	if (CleanupDelaySeconds > 0.0f)
	{
		SetLifeSpan(CleanupDelaySeconds);
		return;
	}

	Destroy();
}

void AChunkWorldTimedCleanupDestructionActor::OnRep_ReplicatedTriggerState()
{
	if (ReplicatedTriggerState.TriggerSerial <= 0)
	{
		return;
	}

	AcceptDestructionTrigger(ReplicatedTriggerState.Request, false);
}
