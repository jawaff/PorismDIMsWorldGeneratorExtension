// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

UChunkWorldBlockFeedbackComponent::UChunkWorldBlockFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

bool UChunkWorldBlockFeedbackComponent::BroadcastFeedbackAtLocation(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem)
{
	if (Sound == nullptr && NiagaraSystem == nullptr)
	{
		return false;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		MulticastBroadcastFeedback(WorldLocation, Sound, NiagaraSystem);
		return true;
	}

	PlayFeedbackAtLocation(WorldLocation, Sound, NiagaraSystem);
	return true;
}

void UChunkWorldBlockFeedbackComponent::MulticastBroadcastFeedback_Implementation(const FVector_NetQuantize& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem)
{
	PlayFeedbackAtLocation(WorldLocation, Sound, NiagaraSystem);
}

bool UChunkWorldBlockFeedbackComponent::ShouldPlayFeedbackAtLocation(const FVector& WorldLocation) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (FeedbackCullDistance <= 0.0f)
	{
		return true;
	}

	const APlayerController* LocalPlayerController = UGameplayStatics::GetPlayerController(this, 0);
	if (LocalPlayerController == nullptr)
	{
		return true;
	}

	FVector ListenerLocation = FVector::ZeroVector;
	if (const APlayerCameraManager* CameraManager = LocalPlayerController->PlayerCameraManager)
	{
		ListenerLocation = CameraManager->GetCameraLocation();
	}
	else if (const APawn* ControlledPawn = LocalPlayerController->GetPawn())
	{
		ListenerLocation = ControlledPawn->GetActorLocation();
	}
	else
	{
		ListenerLocation = LocalPlayerController->GetFocalLocation();
	}

	return FVector::DistSquared(ListenerLocation, WorldLocation) <= FMath::Square(FeedbackCullDistance);
}

void UChunkWorldBlockFeedbackComponent::PlayFeedbackAtLocation(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const
{
	if (!ShouldPlayFeedbackAtLocation(WorldLocation))
	{
		return;
	}

	if (Sound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, WorldLocation);
	}

	if (NiagaraSystem != nullptr)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, NiagaraSystem, WorldLocation);
	}
}
