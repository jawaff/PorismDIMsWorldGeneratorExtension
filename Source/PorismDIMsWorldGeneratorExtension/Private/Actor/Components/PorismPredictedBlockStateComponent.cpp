// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismPredictedBlockStateComponent.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldHitBlueprintLibrary.h"
#include "ChunkWorld/ChunkWorld.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Engine/World.h"
#include "TimerManager.h"

UPorismPredictedBlockStateComponent::UPorismPredictedBlockStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UPorismPredictedBlockStateComponent::BeginPlay()
{
	Super::BeginPlay();
	PruneExpiredPredictions();

	if (ShouldRegisterPredictionNotifications())
	{
		BindObservedChunkWorlds();
	}
}

void UPorismPredictedBlockStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindObservedChunkWorlds();
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingDamageFlushHandle);
	}

	Super::EndPlay(EndPlayReason);
}

UPorismPredictedBlockStateComponent::FPredictedBlockKey UPorismPredictedBlockStateComponent::MakeKey(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	FPredictedBlockKey Key;
	Key.ChunkWorld = ResolvedHit.ChunkWorld;
	Key.BlockWorldPos = ResolvedHit.BlockWorldPos;
	return Key;
}

UPorismPredictedBlockStateComponent::FPredictedBlockKey UPorismPredictedBlockStateComponent::MakeKey(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
{
	FPredictedBlockKey Key;
	Key.ChunkWorld = ChunkWorld;
	Key.BlockWorldPos = BlockWorldPos;
	return Key;
}

void UPorismPredictedBlockStateComponent::PruneExpiredPredictions()
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	for (auto It = PredictedBlockStates.CreateIterator(); It; ++It)
	{
		const bool bExpired = PredictionTimeoutSeconds > 0.0f && (Now - It.Value().PredictionTimeSeconds) >= PredictionTimeoutSeconds;
		if (bExpired || !It.Key().ChunkWorld.IsValid())
		{
			It.RemoveCurrent();
		}
	}
}

bool UPorismPredictedBlockStateComponent::ShouldRegisterPredictionNotifications() const
{
	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	// Keep the authoritative update observer path simple and reliable for all non-dedicated worlds.
	// This avoids ownership/possession timing gaps on listen servers where the host pawn can miss
	// interaction refresh events if registration depends on IsLocalController/IsLocallyControlled
	// during BeginPlay.
	return true;
}

void UPorismPredictedBlockStateComponent::HandleObservedChunkWorldAuthoritativeBlockCustomDataUpdated(AChunkWorldExtended* ChunkWorld, const FIntVector& BlockWorldPos)
{
	if (ChunkWorld == nullptr)
	{
		return;
	}

	PredictedBlockStates.Remove(MakeKey(ChunkWorld, BlockWorldPos));
	PendingAuthoritativeDamageByBlock.Remove(MakeKey(ChunkWorld, BlockWorldPos));
	BroadcastTrackedBlockStateChanged(ChunkWorld, BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::TryApplyPredictedDamageForResolvedBlockHit(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	int32 DamageAmount,
	float PredictionTimeSeconds,
	FChunkWorldBlockDamageResult& OutResult) const
{
	OutResult = FChunkWorldBlockDamageResult();
	if (DamageAmount <= 0 || !ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	OutResult.bHitWasRepresentedBlock = true;
	OutResult.bUsedPredictedWrite = true;
	OutResult.ChunkWorld = ResolvedHit.ChunkWorld;
	OutResult.BlockWorldPos = ResolvedHit.BlockWorldPos;
	OutResult.MaterialIndex = ResolvedHit.MaterialIndex;
	OutResult.MeshIndex = ResolvedHit.MeshIndex;
	OutResult.PredictionTimeSeconds = PredictionTimeSeconds;

	FGameplayTag BlockTypeName;
	int32 CurrentHealth = 0;
	bool bIsInvincible = false;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(ResolvedHit, CurrentHealth, bIsInvincible, BlockTypeName))
	{
		return false;
	}

	OutResult.bHasDamageSchema = true;
	OutResult.BlockTypeName = BlockTypeName;
	OutResult.bWasInvincible = bIsInvincible;
	OutResult.PreviousHealth = CurrentHealth;
	OutResult.DamageApplied = DamageAmount;

	if (bIsInvincible)
	{
		OutResult.NewHealth = CurrentHealth;
		return true;
	}

	const int32 ClampedHealth = FMath::Max(0, CurrentHealth);
	OutResult.NewHealth = FMath::Max(0, ClampedHealth - DamageAmount);
	OutResult.bAppliedDamage = OutResult.NewHealth != ClampedHealth;
	OutResult.bDestroyed = OutResult.NewHealth <= 0 && ClampedHealth > 0;
	return true;
}

void UPorismPredictedBlockStateComponent::StorePredictedDamageResult(const FChunkWorldResolvedBlockHit& ResolvedHit, const FChunkWorldBlockDamageResult& DamageResult)
{
	if (!DamageResult.bUsedPredictedWrite || !DamageResult.bHitWasRepresentedBlock)
	{
		return;
	}

	PruneExpiredPredictions();

	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	PredictedBlockStates.Add(Key, DamageResult);
	BroadcastTrackedBlockStateChanged(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::ApplyPredictedDamageAndQueueAuthoritativeFlush(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 DamageAmount)
{
	if (DamageAmount <= 0 || !ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		FChunkWorldBlockDamageResult DamageResult;
		const bool bAppliedDamage = UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, DamageAmount, DamageResult);
		if (bAppliedDamage)
		{
			BroadcastTrackedBlockStateChanged(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos);
		}
		return bAppliedDamage;
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return false;
	}

	FChunkWorldBlockDamageResult PredictedResult;
	if (!TryApplyPredictedDamageForResolvedBlockHit(
		ResolvedHit,
		DamageAmount,
		World->GetTimeSeconds(),
		PredictedResult))
	{
		return false;
	}

	StorePredictedDamageResult(ResolvedHit, PredictedResult);
	QueueAuthoritativeDamage(ResolvedHit, DamageAmount);
	return true;
}

void UPorismPredictedBlockStateComponent::ClearPredictionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	PredictedBlockStates.Remove(MakeKey(ResolvedHit));
	BroadcastTrackedBlockStateChanged(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::TryGetCurrentHealthState(const FChunkWorldResolvedBlockHit& ResolvedHit, int32& OutHealth, bool& bOutInvincible, bool& bOutUsingPrediction, FGameplayTag& OutBlockTypeName)
{
	PruneExpiredPredictions();

	bOutUsingPrediction = false;
	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	if (const FChunkWorldBlockDamageResult* PredictedResult = PredictedBlockStates.Find(Key))
	{
		OutHealth = PredictedResult->NewHealth;
		bOutInvincible = PredictedResult->bWasInvincible;
		OutBlockTypeName = PredictedResult->BlockTypeName;
		bOutUsingPrediction = true;
		return true;
	}

	return UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(ResolvedHit, OutHealth, bOutInvincible, OutBlockTypeName);
}

void UPorismPredictedBlockStateComponent::QueueAuthoritativeDamage(const FChunkWorldResolvedBlockHit& ResolvedHit, int32 DamageAmount)
{
	if (DamageAmount <= 0 || !IsValid(ResolvedHit.ChunkWorld))
	{
		return;
	}

	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	FQueuedPredictedBlockDamage& PendingDamage = PendingAuthoritativeDamageByBlock.FindOrAdd(Key);
	PendingDamage.ChunkWorld = ResolvedHit.ChunkWorld;
	PendingDamage.BlockWorldPos = ResolvedHit.BlockWorldPos;
	PendingDamage.DamageAmount += DamageAmount;

	UWorld* World = GetWorld();
	if (World == nullptr || World->GetTimerManager().IsTimerActive(PendingDamageFlushHandle))
	{
		return;
	}

	World->GetTimerManager().SetTimer(
		PendingDamageFlushHandle,
		this,
		&UPorismPredictedBlockStateComponent::FlushQueuedPredictedDamage,
		FMath::Max(0.0f, PendingDamageFlushDelaySeconds),
		false);
}

void UPorismPredictedBlockStateComponent::FlushQueuedPredictedDamage()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PendingDamageFlushHandle);
	}

	if (PendingAuthoritativeDamageByBlock.IsEmpty())
	{
		return;
	}

	TArray<FQueuedPredictedBlockDamage> QueuedDamageEntries;
	QueuedDamageEntries.Reserve(PendingAuthoritativeDamageByBlock.Num());
	for (const TPair<FPredictedBlockKey, FQueuedPredictedBlockDamage>& PendingEntry : PendingAuthoritativeDamageByBlock)
	{
		if (PendingEntry.Value.DamageAmount > 0 && IsValid(PendingEntry.Value.ChunkWorld))
		{
			QueuedDamageEntries.Add(PendingEntry.Value);
		}
	}

	PendingAuthoritativeDamageByBlock.Reset();
	if (QueuedDamageEntries.IsEmpty())
	{
		return;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		ServerFlushQueuedPredictedDamage_Implementation(QueuedDamageEntries);
		return;
	}

	ServerFlushQueuedPredictedDamage(QueuedDamageEntries);
}

void UPorismPredictedBlockStateComponent::BroadcastTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
{
	if (!IsValid(ChunkWorld))
	{
		return;
	}

	TrackedBlockStateChangedEvent.Broadcast(ChunkWorld, BlockWorldPos);
}

void UPorismPredictedBlockStateComponent::BindObservedChunkWorld(AChunkWorld* ChunkWorld)
{
	AChunkWorldExtended* ChunkWorldExtended = Cast<AChunkWorldExtended>(ChunkWorld);
	if (ChunkWorldExtended == nullptr || ObservedChunkWorlds.Contains(ChunkWorldExtended))
	{
		return;
	}

	ChunkWorldExtended->OnAuthoritativeBlockCustomDataUpdated.AddDynamic(this, &UPorismPredictedBlockStateComponent::HandleObservedChunkWorldAuthoritativeBlockCustomDataUpdated);
	ObservedChunkWorlds.Add(ChunkWorldExtended);
}

void UPorismPredictedBlockStateComponent::BindObservedChunkWorlds()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	for (TActorIterator<AChunkWorldExtended> It(World); It; ++It)
	{
		BindObservedChunkWorld(*It);
	}

	ActorSpawnedHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateUObject(this, &UPorismPredictedBlockStateComponent::HandleActorSpawned));
}

void UPorismPredictedBlockStateComponent::UnbindObservedChunkWorlds()
{
	if (UWorld* World = GetWorld(); World != nullptr && ActorSpawnedHandle.IsValid())
	{
		World->RemoveOnActorSpawnedHandler(ActorSpawnedHandle);
		ActorSpawnedHandle.Reset();
	}

	for (const TWeakObjectPtr<AChunkWorldExtended>& ChunkWorld : ObservedChunkWorlds)
	{
		if (AChunkWorldExtended* ChunkWorldInstance = ChunkWorld.Get())
		{
			ChunkWorldInstance->OnAuthoritativeBlockCustomDataUpdated.RemoveDynamic(this, &UPorismPredictedBlockStateComponent::HandleObservedChunkWorldAuthoritativeBlockCustomDataUpdated);
		}
	}

	ObservedChunkWorlds.Reset();
}

void UPorismPredictedBlockStateComponent::HandleActorSpawned(AActor* SpawnedActor)
{
	BindObservedChunkWorld(Cast<AChunkWorldExtended>(SpawnedActor));
}

void UPorismPredictedBlockStateComponent::ServerFlushQueuedPredictedDamage_Implementation(const TArray<FQueuedPredictedBlockDamage>& QueuedDamageEntries)
{
	for (const FQueuedPredictedBlockDamage& PendingDamage : QueuedDamageEntries)
	{
		if (PendingDamage.DamageAmount <= 0 || !IsValid(PendingDamage.ChunkWorld))
		{
			continue;
		}

		FChunkWorldResolvedBlockHit ResolvedHit;
		if (!UChunkWorldHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(PendingDamage.ChunkWorld, PendingDamage.BlockWorldPos, ResolvedHit))
		{
			continue;
		}

		FChunkWorldBlockDamageResult DamageResult;
		(void)UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, PendingDamage.DamageAmount, DamageResult);
	}
}
