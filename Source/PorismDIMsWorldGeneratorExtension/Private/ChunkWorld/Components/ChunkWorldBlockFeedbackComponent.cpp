// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "Camera/PlayerCameraManager.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldBlockFeedback, Log, All);

UChunkWorldBlockFeedbackComponent::UChunkWorldBlockFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UChunkWorldBlockFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AChunkWorldExtended* ChunkWorld = Cast<AChunkWorldExtended>(GetOwner()))
	{
		ChunkWorld->OnSettledBlockTransition.AddDynamic(this, &UChunkWorldBlockFeedbackComponent::HandleSettledBlockTransition);
	}
}

void UChunkWorldBlockFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AChunkWorldExtended* ChunkWorld = Cast<AChunkWorldExtended>(GetOwner()))
	{
		ChunkWorld->OnSettledBlockTransition.RemoveDynamic(this, &UChunkWorldBlockFeedbackComponent::HandleSettledBlockTransition);
	}

	Super::EndPlay(EndPlayReason);
}

bool UChunkWorldBlockFeedbackComponent::RequestImmediateLocalHitFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!CanRequestImmediateLocalFeedback(ResolvedHit))
	{
		return false;
	}

	USoundBase* Sound = nullptr;
	if (!TryResolveHitFeedbackAssets(ResolvedHit, Sound))
	{
		return false;
	}

	const bool bPlayedFeedback = PlayBlockFeedback(
		ResolvedHit.ChunkWorld,
		ResolvedHit.BlockWorldPos,
		EChunkWorldBlockFeedbackKind::Hit,
		ResolveFeedbackLocation(ResolvedHit),
		Sound,
		nullptr);
	if (bPlayedFeedback)
	{
		RememberPredictedHitFeedbackToken(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos);
	}

	return bPlayedFeedback;
}

bool UChunkWorldBlockFeedbackComponent::RequestImmediateLocalDestroyFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!CanRequestImmediateLocalFeedback(ResolvedHit))
	{
		return false;
	}

	USoundBase* Sound = nullptr;
	UNiagaraSystem* NiagaraSystem = nullptr;
	if (!TryResolveDestroyFeedbackAssets(ResolvedHit, Sound, NiagaraSystem))
	{
		return false;
	}

	return PlayBlockFeedback(
		ResolvedHit.ChunkWorld,
		ResolvedHit.BlockWorldPos,
		EChunkWorldBlockFeedbackKind::Destroy,
		ResolveFeedbackLocation(ResolvedHit),
		Sound,
		NiagaraSystem);
}

bool UChunkWorldBlockFeedbackComponent::BroadcastAuthoritativeHitFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!CanBroadcastAuthoritativeFeedback(ResolvedHit))
	{
		return false;
	}

	USoundBase* Sound = nullptr;
	if (!TryResolveHitFeedbackAssets(ResolvedHit, Sound))
	{
		return false;
	}

	const FVector FeedbackLocation = ResolveFeedbackLocation(ResolvedHit);
	const bool bPlayedLocally = PlayBlockFeedback(
		ResolvedHit.ChunkWorld,
		ResolvedHit.BlockWorldPos,
		EChunkWorldBlockFeedbackKind::Hit,
		FeedbackLocation,
		Sound,
		nullptr);
	return bPlayedLocally || Sound != nullptr;
}

bool UChunkWorldBlockFeedbackComponent::BroadcastAuthoritativeDestroyFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	if (!CanBroadcastAuthoritativeFeedback(ResolvedHit))
	{
		return false;
	}

	USoundBase* Sound = nullptr;
	UNiagaraSystem* NiagaraSystem = nullptr;
	if (!TryResolveDestroyFeedbackAssets(ResolvedHit, Sound, NiagaraSystem))
	{
		return false;
	}

	const FVector FeedbackLocation = ResolveFeedbackLocation(ResolvedHit);
	const bool bPlayedLocally = PlayBlockFeedback(
		ResolvedHit.ChunkWorld,
		ResolvedHit.BlockWorldPos,
		EChunkWorldBlockFeedbackKind::Destroy,
		FeedbackLocation,
		Sound,
		NiagaraSystem);
	return bPlayedLocally || Sound != nullptr || NiagaraSystem != nullptr;
}

void UChunkWorldBlockFeedbackComponent::HandleSettledBlockTransition(
	AChunkWorldExtended* ChunkWorld,
	const FChunkWorldSettledBlockTransition& Transition)
{
	if (ChunkWorld == nullptr || ChunkWorld != GetOwner())
	{
		return;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		return;
	}

	if (Transition.bObservedRepresentationRemoved)
	{
		ClearPredictedHitFeedbackToken(ChunkWorld, Transition.BlockWorldPos);

		USoundBase* Sound = nullptr;
		UNiagaraSystem* NiagaraSystem = nullptr;
		if (!TryResolveDestroyFeedbackAssetsForBlockType(
			Transition.PreviousResolvedHit.BlockTypeSchemaComponent.Get(),
			Transition.PreviousBlockTypeName,
			Sound,
			NiagaraSystem))
		{
			return;
		}

		const FVector WorldLocation = !Transition.PreviousResolvedHit.RepresentativeWorldPos.IsNearlyZero()
			? Transition.PreviousResolvedHit.RepresentativeWorldPos
			: ChunkWorld->BlockWorldPosToUEWorldPos(Transition.BlockWorldPos);
		(void)PlayBlockFeedback(
			ChunkWorld,
			Transition.BlockWorldPos,
			EChunkWorldBlockFeedbackKind::Destroy,
			WorldLocation,
			Sound,
			NiagaraSystem);
		return;
	}

	if (Transition.bObservedHealthDecrease && Transition.CurrentResolvedHit.bHasBlock)
	{
		if (ShouldSuppressSettledHitFromPredictedToken(ChunkWorld, Transition.BlockWorldPos))
		{
			return;
		}

		USoundBase* Sound = nullptr;
		if (!TryResolveHitFeedbackAssets(Transition.CurrentResolvedHit, Sound))
		{
			return;
		}

		(void)PlayBlockFeedback(
			ChunkWorld,
			Transition.BlockWorldPos,
			EChunkWorldBlockFeedbackKind::Hit,
			ResolveFeedbackLocation(Transition.CurrentResolvedHit),
			Sound,
			nullptr);
	}
}

bool UChunkWorldBlockFeedbackComponent::ShouldSuppressSettledHitFromPredictedToken(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos) const
{
	PruneExpiredPredictedHitFeedbackTokens();

	for (int32 Index = PredictedHitFeedbackTokens.Num() - 1; Index >= 0; --Index)
	{
		const FPredictedChunkWorldHitFeedbackToken& Token = PredictedHitFeedbackTokens[Index];
		if (Token.ChunkWorld == ChunkWorld && Token.BlockWorldPos == BlockWorldPos)
		{
			PredictedHitFeedbackTokens.RemoveAtSwap(Index);
			return true;
		}
	}

	return false;
}

void UChunkWorldBlockFeedbackComponent::RememberPredictedHitFeedbackToken(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || PredictedHitSuppressionWindowSeconds <= 0.0f)
	{
		return;
	}

	PruneExpiredPredictedHitFeedbackTokens();
	ClearPredictedHitFeedbackToken(ChunkWorld, BlockWorldPos);

	FPredictedChunkWorldHitFeedbackToken& Token = PredictedHitFeedbackTokens.AddDefaulted_GetRef();
	Token.ChunkWorld = ChunkWorld;
	Token.BlockWorldPos = BlockWorldPos;
	Token.TimeSeconds = World->GetTimeSeconds();
}

void UChunkWorldBlockFeedbackComponent::ClearPredictedHitFeedbackToken(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos) const
{
	for (int32 Index = PredictedHitFeedbackTokens.Num() - 1; Index >= 0; --Index)
	{
		const FPredictedChunkWorldHitFeedbackToken& Token = PredictedHitFeedbackTokens[Index];
		if (Token.ChunkWorld == ChunkWorld && Token.BlockWorldPos == BlockWorldPos)
		{
			PredictedHitFeedbackTokens.RemoveAtSwap(Index);
		}
	}
}

void UChunkWorldBlockFeedbackComponent::PruneExpiredPredictedHitFeedbackTokens() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	for (int32 Index = PredictedHitFeedbackTokens.Num() - 1; Index >= 0; --Index)
	{
		const FPredictedChunkWorldHitFeedbackToken& Token = PredictedHitFeedbackTokens[Index];
		if (!Token.ChunkWorld.IsValid()
			|| PredictedHitSuppressionWindowSeconds <= 0.0f
			|| (CurrentTimeSeconds - Token.TimeSeconds) > PredictedHitSuppressionWindowSeconds)
		{
			PredictedHitFeedbackTokens.RemoveAtSwap(Index);
		}
	}
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

bool UChunkWorldBlockFeedbackComponent::ShouldSuppressDuplicateFeedback(
	AChunkWorld* ChunkWorld,
	const FIntVector& BlockWorldPos,
	const EChunkWorldBlockFeedbackKind Kind,
	const FVector& WorldLocation,
	USoundBase* Sound,
	UNiagaraSystem* NiagaraSystem) const
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

		if (RecentPlayback.ChunkWorld == ChunkWorld
			&& RecentPlayback.BlockWorldPos == BlockWorldPos
			&& RecentPlayback.Kind == Kind
			&& RecentPlayback.Sound == Sound
			&& RecentPlayback.NiagaraSystem == NiagaraSystem
			&& FVector::DistSquared(RecentPlayback.WorldLocation, WorldLocation) <= MaxDistanceSquared)
		{
			return true;
		}
	}

	return false;
}

void UChunkWorldBlockFeedbackComponent::RememberFeedbackPlayback(
	AChunkWorld* ChunkWorld,
	const FIntVector& BlockWorldPos,
	const EChunkWorldBlockFeedbackKind Kind,
	const FVector& WorldLocation,
	USoundBase* Sound,
	UNiagaraSystem* NiagaraSystem) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	FRecentChunkWorldBlockFeedback& Playback = RecentFeedbackPlaybacks.AddDefaulted_GetRef();
	Playback.ChunkWorld = ChunkWorld;
	Playback.BlockWorldPos = BlockWorldPos;
	Playback.Kind = Kind;
	Playback.WorldLocation = WorldLocation;
	Playback.Sound = Sound;
	Playback.NiagaraSystem = NiagaraSystem;
	Playback.TimeSeconds = World->GetTimeSeconds();
}

bool UChunkWorldBlockFeedbackComponent::TryResolveHitFeedbackAssets(const FChunkWorldResolvedBlockHit& ResolvedHit, USoundBase*& OutSound) const
{
	OutSound = nullptr;

	FGameplayTag BlockTypeName;
	FInstancedStruct DefinitionStruct;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, BlockTypeName, DefinitionStruct))
	{
		return false;
	}

	FBlockHealthDefinition HealthDefinition;
	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionStruct, HealthDefinition))
	{
		return false;
	}

	OutSound = HealthDefinition.HitSound;
	return OutSound != nullptr;
}

bool UChunkWorldBlockFeedbackComponent::TryResolveDestroyFeedbackAssets(const FChunkWorldResolvedBlockHit& ResolvedHit, USoundBase*& OutSound, UNiagaraSystem*& OutNiagaraSystem) const
{
	OutSound = nullptr;
	OutNiagaraSystem = nullptr;

	FGameplayTag BlockTypeName;
	FInstancedStruct DefinitionStruct;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, BlockTypeName, DefinitionStruct))
	{
		return false;
	}

	FBlockDefinitionBase BlockDefinition;
	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockDefinitionBase(DefinitionStruct, BlockDefinition))
	{
		return false;
	}

	OutSound = BlockDefinition.DestroyedSound;
	OutNiagaraSystem = BlockDefinition.DestroyedEffect;
	return OutSound != nullptr || OutNiagaraSystem != nullptr;
}

bool UChunkWorldBlockFeedbackComponent::TryResolveDestroyFeedbackAssetsForBlockType(
	UBlockTypeSchemaComponent* SchemaComponent,
	const FGameplayTag BlockTypeName,
	USoundBase*& OutSound,
	UNiagaraSystem*& OutNiagaraSystem) const
{
	OutSound = nullptr;
	OutNiagaraSystem = nullptr;

	FInstancedStruct DefinitionStruct;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(SchemaComponent, BlockTypeName, DefinitionStruct))
	{
		return false;
	}

	FBlockDefinitionBase BlockDefinition;
	if (!UBlockTypeSchemaBlueprintLibrary::TryGetBlockDefinitionBase(DefinitionStruct, BlockDefinition))
	{
		return false;
	}

	OutSound = BlockDefinition.DestroyedSound;
	OutNiagaraSystem = BlockDefinition.DestroyedEffect;
	return OutSound != nullptr || OutNiagaraSystem != nullptr;
}

FVector UChunkWorldBlockFeedbackComponent::ResolveFeedbackLocation(const FChunkWorldResolvedBlockHit& ResolvedHit) const
{
	if (!ResolvedHit.RepresentativeWorldPos.IsNearlyZero())
	{
		return ResolvedHit.RepresentativeWorldPos;
	}

	if (IsValid(ResolvedHit.ChunkWorld))
	{
		return ResolvedHit.ChunkWorld->BlockWorldPosToUEWorldPos(ResolvedHit.BlockWorldPos);
	}

	return FVector::ZeroVector;
}

bool UChunkWorldBlockFeedbackComponent::CanRequestImmediateLocalFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit) const
{
	const UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		UE_LOG(
			LogChunkWorldBlockFeedback,
			Warning,
			TEXT("Immediate local block feedback request ignored on '%s' because the world cannot play local feedback."),
			*GetNameSafe(this));
		return false;
	}

	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		UE_LOG(
			LogChunkWorldBlockFeedback,
			Warning,
			TEXT("Immediate local block feedback request ignored on '%s' because the resolved hit is invalid."),
			*GetNameSafe(this));
		return false;
	}

	return true;
}

bool UChunkWorldBlockFeedbackComponent::CanBroadcastAuthoritativeFeedback(const FChunkWorldResolvedBlockHit& ResolvedHit) const
{
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		UE_LOG(
			LogChunkWorldBlockFeedback,
			Warning,
			TEXT("Authoritative block feedback broadcast ignored on '%s' because the resolved hit is invalid."),
			*GetNameSafe(this));
		return false;
	}

	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		UE_LOG(
			LogChunkWorldBlockFeedback,
			Warning,
			TEXT("Authoritative block feedback broadcast ignored on '%s' because the caller does not have authority."),
			*GetNameSafe(this));
		return false;
	}

	return true;
}

bool UChunkWorldBlockFeedbackComponent::PlayBlockFeedback(
	AChunkWorld* ChunkWorld,
	const FIntVector& BlockWorldPos,
	const EChunkWorldBlockFeedbackKind Kind,
	const FVector& WorldLocation,
	USoundBase* Sound,
	UNiagaraSystem* NiagaraSystem) const
{
	if ((Sound == nullptr && NiagaraSystem == nullptr) || !ShouldPlayFeedbackAtLocation(WorldLocation))
	{
		return false;
	}

	if (ShouldSuppressDuplicateFeedback(ChunkWorld, BlockWorldPos, Kind, WorldLocation, Sound, NiagaraSystem))
	{
		return false;
	}

	if (Sound != nullptr)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, WorldLocation);
	}

	if (NiagaraSystem != nullptr)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, NiagaraSystem, WorldLocation);
	}

	RememberFeedbackPlayback(ChunkWorld, BlockWorldPos, Kind, WorldLocation, Sound, NiagaraSystem);
	return true;
}
