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
		PlayFeedbackAtLocation(WorldLocation, Sound, NiagaraSystem);
		MulticastBroadcastFeedback(WorldLocation, Sound, NiagaraSystem);
		return true;
	}

	PlayFeedbackAtLocation(WorldLocation, Sound, NiagaraSystem);
	return true;
}

void UChunkWorldBlockFeedbackComponent::MulticastBroadcastFeedback_Implementation(const FVector_NetQuantize& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		return;
	}

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

bool UChunkWorldBlockFeedbackComponent::ShouldSuppressDuplicateFeedback(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || FeedbackDeduplicationWindowSeconds <= 0.0f)
	{
		return false;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	const float MaxDistanceSquared = FMath::Square(FeedbackDeduplicationDistance);
	for (int32 Index = RecentFeedbackPlaybacks.Num() - 1; Index >= 0; --Index)
	{
		const FRecentChunkWorldBlockFeedback& RecentPlayback = RecentFeedbackPlaybacks[Index];
		if ((CurrentTimeSeconds - RecentPlayback.TimeSeconds) > FeedbackDeduplicationWindowSeconds)
		{
			RecentFeedbackPlaybacks.RemoveAtSwap(Index);
			continue;
		}

		if (RecentPlayback.Sound == Sound
			&& RecentPlayback.NiagaraSystem == NiagaraSystem
			&& FVector::DistSquared(RecentPlayback.WorldLocation, WorldLocation) <= MaxDistanceSquared)
		{
			return true;
		}
	}

	return false;
}

void UChunkWorldBlockFeedbackComponent::RememberFeedbackPlayback(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FRecentChunkWorldBlockFeedback& Playback = RecentFeedbackPlaybacks.AddDefaulted_GetRef();
	Playback.WorldLocation = WorldLocation;
	Playback.Sound = Sound;
	Playback.NiagaraSystem = NiagaraSystem;
	Playback.TimeSeconds = World->GetTimeSeconds();
}

void UChunkWorldBlockFeedbackComponent::PlayFeedbackAtLocation(const FVector& WorldLocation, USoundBase* Sound, UNiagaraSystem* NiagaraSystem) const
{
	if (!ShouldPlayFeedbackAtLocation(WorldLocation))
	{
		return;
	}

	if (ShouldSuppressDuplicateFeedback(WorldLocation, Sound, NiagaraSystem))
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

	RememberFeedbackPlayback(WorldLocation, Sound, NiagaraSystem);
}
