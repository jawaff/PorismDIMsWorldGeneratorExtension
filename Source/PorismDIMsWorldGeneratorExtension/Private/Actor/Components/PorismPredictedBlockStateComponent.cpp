// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismPredictedBlockStateComponent.h"

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPorismPredictedBlockState, Log, All);

namespace
{
	bool TryResolveDamageDefinitionForHealthView(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		FGameplayTag PreferredBlockTypeName,
		FGameplayTag& OutResolvedBlockTypeName,
		FBlockDamageDefinition& OutDefinition)
	{
		OutResolvedBlockTypeName = FGameplayTag();
		OutDefinition = FBlockDamageDefinition();

		if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.BlockTypeSchemaComponent.Get()))
		{
			return false;
		}

		FInstancedStruct DefinitionStruct;
		if (PreferredBlockTypeName.IsValid()
			&& UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForBlockTypeName(
				ResolvedHit.BlockTypeSchemaComponent.Get(),
				PreferredBlockTypeName,
				DefinitionStruct)
			&& UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionStruct, OutDefinition))
		{
			OutResolvedBlockTypeName = PreferredBlockTypeName;
			return true;
		}

		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutResolvedBlockTypeName, DefinitionStruct))
		{
			return false;
		}

		return UBlockTypeSchemaBlueprintLibrary::TryGetBlockDamageDefinition(DefinitionStruct, OutDefinition);
	}
}

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

	return World->GetNetMode() != NM_DedicatedServer;
}

void UPorismPredictedBlockStateComponent::HandleObservedChunkWorldBlockCustomDataChanged(
	AChunkWorldExtended* ChunkWorld,
	const FIntVector& BlockWorldPos,
	const bool bTouchedHealth)
{
	if (ChunkWorld == nullptr)
	{
		return;
	}

	const FPredictedBlockKey Key = MakeKey(ChunkWorld, BlockWorldPos);
	const bool bHadPredictedState = PredictedBlockStates.Contains(Key);
	const bool bHadPendingDamage = PendingAuthoritativeDamageByBlock.Contains(Key);
	const FChunkWorldBlockDamageResult* PredictedResult = PredictedBlockStates.Find(Key);

	int32 AuthoritativeHealth = 0;
	int32 MaxHealth = 0;
	bool bIsInvincible = false;
	bool bHasRuntimeHealth = false;
	FGameplayTag BlockTypeName;
	const bool bResolvedAuthoritativeHealth = bTouchedHealth
		&& UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForBlockWorldPos(
			ChunkWorld,
			BlockWorldPos,
			AuthoritativeHealth,
			MaxHealth,
			bIsInvincible,
			bHasRuntimeHealth,
			BlockTypeName);

	const bool bShouldClearPrediction = bHadPredictedState
		&& bResolvedAuthoritativeHealth
		&& bHasRuntimeHealth
		&& (PredictedResult == nullptr || AuthoritativeHealth <= PredictedResult->NewHealth);
	const bool bShouldClearPendingDamage = bHadPendingDamage && (!bHadPredictedState || bShouldClearPrediction);

	if (bShouldClearPrediction)
	{
		PredictedBlockStates.Remove(Key);
	}

	if (bShouldClearPendingDamage)
	{
		PendingAuthoritativeDamageByBlock.Remove(Key);
	}

	BroadcastTrackedBlockStateChanged(ChunkWorld, BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::ValidateDamageRequest(const FChunkWorldBlockDamageRequest& DamageRequest) const
{
	if (DamageRequest.DamageAmount <= 0)
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Block damage request ignored on '%s' because the damage amount was %d."),
			*GetNameSafe(this),
			DamageRequest.DamageAmount);
		return false;
	}

	if (!DamageRequest.ResolvedHit.bHasBlock || !IsValid(DamageRequest.ResolvedHit.ChunkWorld))
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Block damage request ignored on '%s' because the resolved hit was invalid."),
			*GetNameSafe(this));
		return false;
	}

	return true;
}

bool UPorismPredictedBlockStateComponent::TryBuildPredictedDamageResult(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	const float PredictionTimeSeconds,
	FChunkWorldBlockDamageResult& OutResult) const
{
	OutResult = FChunkWorldBlockDamageResult();
	if (!ValidateDamageRequest(DamageRequest))
	{
		return false;
	}

	const FChunkWorldResolvedBlockHit& ResolvedHit = DamageRequest.ResolvedHit;
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
	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	if (const FChunkWorldBlockDamageResult* ExistingPredictedResult = PredictedBlockStates.Find(Key))
	{
		CurrentHealth = ExistingPredictedResult->NewHealth;
		bIsInvincible = ExistingPredictedResult->bWasInvincible;
		BlockTypeName = ExistingPredictedResult->BlockTypeName;
	}
	else if (!UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(ResolvedHit, CurrentHealth, bIsInvincible, BlockTypeName))
	{
		return false;
	}

	OutResult.bHasDamageSchema = true;
	OutResult.BlockTypeName = BlockTypeName;
	OutResult.bWasInvincible = bIsInvincible;
	OutResult.PreviousHealth = CurrentHealth;
	OutResult.DamageApplied = DamageRequest.DamageAmount;

	if (bIsInvincible)
	{
		OutResult.NewHealth = CurrentHealth;
		return true;
	}

	const int32 ClampedHealth = FMath::Max(0, CurrentHealth);
	OutResult.NewHealth = FMath::Max(0, ClampedHealth - DamageRequest.DamageAmount);
	OutResult.bAppliedDamage = OutResult.NewHealth != ClampedHealth;
	OutResult.bDestroyed = OutResult.NewHealth <= 0 && ClampedHealth > 0;
	return true;
}

bool UPorismPredictedBlockStateComponent::DidTrackedHealthStateChange(
	const FChunkWorldBlockDamageResult* PreviousResult,
	const FChunkWorldBlockDamageResult& NewResult) const
{
	if (PreviousResult == nullptr)
	{
		return NewResult.bAppliedDamage || NewResult.bWasInvincible;
	}

	return PreviousResult->NewHealth != NewResult.NewHealth
		|| PreviousResult->bWasInvincible != NewResult.bWasInvincible
		|| PreviousResult->BlockTypeName != NewResult.BlockTypeName
		|| PreviousResult->bDestroyed != NewResult.bDestroyed;
}

bool UPorismPredictedBlockStateComponent::TryApplyImmediateLocalFeedback(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	const FChunkWorldBlockDamageResult& DamageResult) const
{
	if (!DamageRequest.bAllowImmediateLocalFeedback || !DamageResult.bAppliedDamage)
	{
		return false;
	}

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	if (!UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(DamageRequest.ResolvedHit.ChunkWorld, FeedbackComponent)
		|| FeedbackComponent == nullptr)
	{
		return false;
	}

	if (DamageResult.bDestroyed)
	{
		return FeedbackComponent->RequestImmediateLocalDestroyFeedback(DamageRequest.ResolvedHit);
	}

	return FeedbackComponent->RequestImmediateLocalHitFeedback(DamageRequest.ResolvedHit);
}

void UPorismPredictedBlockStateComponent::StorePredictedDamageResult(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	const FChunkWorldBlockDamageResult& DamageResult)
{
	if (!DamageResult.bUsedPredictedWrite || !DamageResult.bHitWasRepresentedBlock)
	{
		return;
	}

	PruneExpiredPredictions();

	const FPredictedBlockKey Key = MakeKey(DamageRequest.ResolvedHit);
	const FChunkWorldBlockDamageResult* PreviousResult = PredictedBlockStates.Find(Key);
	if (!DidTrackedHealthStateChange(PreviousResult, DamageResult))
	{
		return;
	}

	PredictedBlockStates.Add(Key, DamageResult);
	BroadcastTrackedBlockStateChanged(DamageRequest.ResolvedHit.ChunkWorld, DamageRequest.ResolvedHit.BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::ApplyBlockDamageRequest(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	FChunkWorldBlockDamageRequestResult& OutResult)
{
	OutResult = FChunkWorldBlockDamageRequestResult();
	if (!ValidateDamageRequest(DamageRequest))
	{
		return false;
	}

	OutResult.bAccepted = true;

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		return ApplyAuthoritativeDamageRequest(DamageRequest, OutResult);
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Client block damage request ignored on '%s' because no world was available."),
			*GetNameSafe(this));
		return false;
	}

	PruneExpiredPredictions();

	FChunkWorldBlockDamageResult PredictedResult;
	if (!TryBuildPredictedDamageResult(DamageRequest, World->GetTimeSeconds(), PredictedResult))
	{
		return false;
	}

	OutResult.DamageResult = PredictedResult;
	if (!PredictedResult.bAppliedDamage)
	{
		return true;
	}

	StorePredictedDamageResult(DamageRequest, PredictedResult);
	OutResult.bPredictionWritten = true;
	OutResult.bPlayedImmediateLocalFeedback = TryApplyImmediateLocalFeedback(DamageRequest, PredictedResult);
	QueueAuthoritativeDamage(DamageRequest);
	OutResult.bQueuedAuthoritativeFlush = true;
	return true;
}

bool UPorismPredictedBlockStateComponent::ApplyPredictedDamageAndQueueAuthoritativeFlush(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const int32 DamageAmount)
{
	FChunkWorldBlockDamageRequest DamageRequest;
	DamageRequest.ResolvedHit = ResolvedHit;
	DamageRequest.DamageAmount = DamageAmount;
	DamageRequest.bAllowImmediateLocalFeedback = true;

	FChunkWorldBlockDamageRequestResult RequestResult;
	return ApplyBlockDamageRequest(DamageRequest, RequestResult);
}

bool UPorismPredictedBlockStateComponent::TryGetHealthState(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	FPorismBlockHealthState& OutState)
{
	OutState = FPorismBlockHealthState();
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.BlockTypeSchemaComponent.Get()))
	{
		return false;
	}

	OutState.bHasCustomData = ResolvedHit.BlockTypeSchemaComponent->IsBlockCustomDataInitialized(ResolvedHit.BlockWorldPos);

	if (TryGetPredictedHealthState(ResolvedHit, OutState.CurrentHealth, OutState.bIsInvincible, OutState.BlockTypeName))
	{
		FGameplayTag DefinitionBlockTypeName;
		FBlockDamageDefinition DamageDefinition;
		if (!TryResolveDamageDefinitionForHealthView(ResolvedHit, OutState.BlockTypeName, DefinitionBlockTypeName, DamageDefinition))
		{
			return false;
		}

		OutState.MaxHealth = DamageDefinition.MaxHealth;
		OutState.bUsingPredictedHealth = true;
		if (!OutState.BlockTypeName.IsValid())
		{
			OutState.BlockTypeName = DefinitionBlockTypeName;
		}
		return true;
	}

	FGameplayTag DefinitionBlockTypeName;
	FBlockDamageDefinition DamageDefinition;
	if (!TryResolveDamageDefinitionForHealthView(ResolvedHit, FGameplayTag(), DefinitionBlockTypeName, DamageDefinition))
	{
		return false;
	}

	OutState.MaxHealth = DamageDefinition.MaxHealth;

	if (!UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForResolvedBlockHit(
		ResolvedHit,
		OutState.CurrentHealth,
		OutState.MaxHealth,
		OutState.bIsInvincible,
		OutState.bHasAuthoritativeHealth,
		OutState.BlockTypeName))
	{
		return false;
	}

	if (!OutState.BlockTypeName.IsValid())
	{
		OutState.BlockTypeName = DefinitionBlockTypeName;
	}

	return true;
}

void UPorismPredictedBlockStateComponent::ClearPredictionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	const bool bRemovedPrediction = PredictedBlockStates.Remove(MakeKey(ResolvedHit)) > 0;
	if (bRemovedPrediction)
	{
		BroadcastTrackedBlockStateChanged(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos);
	}
}

bool UPorismPredictedBlockStateComponent::TryGetPredictedHealthState(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	int32& OutHealth,
	bool& bOutInvincible,
	FGameplayTag& OutBlockTypeName)
{
	PruneExpiredPredictions();
	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	if (const FChunkWorldBlockDamageResult* PredictedResult = PredictedBlockStates.Find(Key))
	{
		OutHealth = PredictedResult->NewHealth;
		bOutInvincible = PredictedResult->bWasInvincible;
		OutBlockTypeName = PredictedResult->BlockTypeName;
		return true;
	}

	return false;
}

void UPorismPredictedBlockStateComponent::QueueAuthoritativeDamage(const FChunkWorldBlockDamageRequest& DamageRequest)
{
	if (!ValidateDamageRequest(DamageRequest))
	{
		return;
	}

	const FPredictedBlockKey Key = MakeKey(DamageRequest.ResolvedHit);
	FQueuedPredictedBlockDamage& PendingDamage = PendingAuthoritativeDamageByBlock.FindOrAdd(Key);
	PendingDamage.ChunkWorld = DamageRequest.ResolvedHit.ChunkWorld;
	PendingDamage.BlockWorldPos = DamageRequest.ResolvedHit.BlockWorldPos;
	PendingDamage.DamageAmount += DamageRequest.DamageAmount;
	if (PendingDamage.RequestContextTag.IsNone())
	{
		PendingDamage.RequestContextTag = DamageRequest.RequestContextTag;
	}

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

bool UPorismPredictedBlockStateComponent::ApplyAuthoritativeDamageRequest(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	FChunkWorldBlockDamageRequestResult& OutResult)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Authoritative block damage path ignored on '%s' because the owner does not have authority."),
			*GetNameSafe(this));
		return false;
	}

	FChunkWorldBlockDamageResult DamageResult;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(DamageRequest.ResolvedHit, DamageRequest.DamageAmount, DamageResult))
	{
		return false;
	}

	OutResult.DamageResult = DamageResult;
	if (!DamageResult.bAppliedDamage)
	{
		return true;
	}

	OutResult.bAuthoritativeDamageApplied = true;
	if (DamageResult.bDestroyed)
	{
		if (AChunkWorldExtended* ChunkWorld = Cast<AChunkWorldExtended>(DamageRequest.ResolvedHit.ChunkWorld))
		{
			(void)ChunkWorld->DestroyBlock(DamageRequest.ResolvedHit.BlockWorldPos, true);
		}
	}
	else
	{
		UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
		if (UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(DamageRequest.ResolvedHit.ChunkWorld, FeedbackComponent)
			&& FeedbackComponent != nullptr)
		{
			(void)FeedbackComponent->BroadcastAuthoritativeHitFeedback(DamageRequest.ResolvedHit);
		}
	}

	return true;
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

	ChunkWorldExtended->OnBlockCustomDataChanged.AddDynamic(this, &UPorismPredictedBlockStateComponent::HandleObservedChunkWorldBlockCustomDataChanged);
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
			ChunkWorldInstance->OnBlockCustomDataChanged.RemoveDynamic(this, &UPorismPredictedBlockStateComponent::HandleObservedChunkWorldBlockCustomDataChanged);
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
		if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(PendingDamage.ChunkWorld, PendingDamage.BlockWorldPos, ResolvedHit))
		{
			continue;
		}

		FChunkWorldBlockDamageRequest DamageRequest;
		DamageRequest.ResolvedHit = ResolvedHit;
		DamageRequest.DamageAmount = PendingDamage.DamageAmount;
		DamageRequest.RequestContextTag = PendingDamage.RequestContextTag;

		FChunkWorldBlockDamageRequestResult RequestResult;
		(void)ApplyAuthoritativeDamageRequest(DamageRequest, RequestResult);
	}
}
