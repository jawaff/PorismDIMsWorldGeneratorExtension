// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Support/ChunkWorldReadinessTestTypes.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Components/BoxComponent.h"

FVector UChunkWorldReadinessTestWalkerComponent::GetTracingLocation_Implementation() const
{
	return GetOwner() != nullptr ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
}

void UChunkWorldReadinessTestFreezeComponent::Advance(const float DeltaTime)
{
	TickComponent(DeltaTime, LEVELTICK_All, nullptr);
}

void UChunkWorldReadinessTestFreezeComponent::EmitRuntimeReady(AChunkWorldExtended* ChunkWorld, UObject* Walker, const FGuid SessionId)
{
	if (ChunkWorld != nullptr)
	{
		ChunkWorld->OnRuntimeWalkerReady.Broadcast(ChunkWorld, Walker, SessionId);
	}
}

void UChunkWorldReadinessTestFreezeComponent::SetClientReadyTimeout(const float TimeoutSeconds)
{
	RuntimeClientReadyTimeoutSeconds = TimeoutSeconds;
}

void UChunkWorldReadinessTestFreezeComponent::SetSettlementCollisionProfile(const FName CollisionProfile)
{
	RuntimeSettlementCollisionProfile = CollisionProfile;
}

AChunkWorldReadinessTestSurface::AChunkWorldReadinessTestSurface()
{
	Collision = CreateDefaultSubobject<UBoxComponent>(TEXT("Collision"));
	RootComponent = Collision;
	Collision->SetCollisionProfileName(TEXT("BlockAll"));
	Collision->SetBoxExtent(FVector(1000.0f, 1000.0f, 20.0f));
}

AChunkWorldReadinessTestPawn::AChunkWorldReadinessTestPawn()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Walker = CreateDefaultSubobject<UChunkWorldReadinessTestWalkerComponent>(TEXT("Walker"));
	Freeze = CreateDefaultSubobject<UChunkWorldReadinessTestFreezeComponent>(TEXT("Freeze"));
}
