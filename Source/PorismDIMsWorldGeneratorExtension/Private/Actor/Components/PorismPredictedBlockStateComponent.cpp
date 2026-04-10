// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismPredictedBlockStateComponent.h"

#include "Block/BlockTypeSchemaBlueprintLibrary.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "ChunkWorld/Components/ChunkWorldBlockFeedbackComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPorismPredictedBlockState, Log, All);

namespace
{
	void DrawPredictedBlockStateDebugStatsLine(UCanvas* Canvas, float& InOutY, const FString& Message, const FColor& Color)
	{
		if (Canvas == nullptr)
		{
			return;
		}

		Canvas->SetDrawColor(Color);
		Canvas->DrawText(GEngine->GetSmallFont(), Message, 48.0f, InOutY);
		InOutY += 14.0f;
	}

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
	SchedulePredictionPrune();

	if (GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		DebugDrawDelegateHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UPorismPredictedBlockStateComponent::DrawDebugStats));
	}

	if (ShouldRegisterPredictionNotifications())
	{
		BindObservedChunkWorlds();
	}
}

void UPorismPredictedBlockStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(PredictionPruneTimerHandle);
	}

	if (DebugDrawDelegateHandle.IsValid())
	{
		UDebugDrawService::Unregister(DebugDrawDelegateHandle);
		DebugDrawDelegateHandle.Reset();
	}

	UnbindObservedChunkWorlds();
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
	TArray<FPredictedBlockKey, TInlineAllocator<4>> ExpiredKeys;
	for (auto It = PredictedBlockStates.CreateIterator(); It; ++It)
	{
		const bool bExpired = PredictionTimeoutSeconds > 0.0f && (Now - It.Value().PredictionTimeSeconds) >= PredictionTimeoutSeconds;
		if (bExpired || !It.Key().ChunkWorld.IsValid())
		{
			if (It.Key().ChunkWorld.IsValid())
			{
				ExpiredKeys.Add(It.Key());
			}
			PendingPredictedDamageRequestsByBlock.Remove(It.Key());
			It.RemoveCurrent();
		}
	}

	for (const FPredictedBlockKey& ExpiredKey : ExpiredKeys)
	{
		BroadcastTrackedBlockStateChanged(ExpiredKey.ChunkWorld.Get(), ExpiredKey.BlockWorldPos);
	}

	SchedulePredictionPrune();
}

void UPorismPredictedBlockStateComponent::SchedulePredictionPrune()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	World->GetTimerManager().ClearTimer(PredictionPruneTimerHandle);
	if (PredictionTimeoutSeconds <= 0.0f || PredictedBlockStates.IsEmpty())
	{
		return;
	}

	float EarliestExpirySeconds = TNumericLimits<float>::Max();
	for (const TPair<FPredictedBlockKey, FChunkWorldBlockDamageResult>& PredictedEntry : PredictedBlockStates)
	{
		if (!PredictedEntry.Key.ChunkWorld.IsValid())
		{
			continue;
		}

		EarliestExpirySeconds = FMath::Min(
			EarliestExpirySeconds,
			PredictedEntry.Value.PredictionTimeSeconds + PredictionTimeoutSeconds);
	}

	if (!FMath::IsFinite(EarliestExpirySeconds))
	{
		return;
	}

	const float DelaySeconds = FMath::Max(0.01f, EarliestExpirySeconds - World->GetTimeSeconds());
	World->GetTimerManager().SetTimer(
		PredictionPruneTimerHandle,
		this,
		&UPorismPredictedBlockStateComponent::HandlePredictionPruneTimer,
		DelaySeconds,
		false);
}

void UPorismPredictedBlockStateComponent::HandlePredictionPruneTimer()
{
	PruneExpiredPredictions();
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

	// Any replicated health-related update for the same block invalidates the local prediction.
	// Authoritative block state may legitimately raise or lower health, may batch multiple local hits,
	// or may include unrelated server-side damage/healing sources. Without an explicit authoritative
	// revision or ack token, the client cannot safely map one replicated update to a subset of queued
	// local predictions, so the entire local prediction state for that block must be discarded.
	const bool bShouldClearPrediction = bHadPredictedState && bTouchedHealth;

	if (bShouldClearPrediction)
	{
		PredictedBlockStates.Remove(Key);
		PendingPredictedDamageRequestsByBlock.Remove(Key);
		SchedulePredictionPrune();
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
	const int32 PredictedHealthAfterDamage = FMath::Max(0, ClampedHealth - DamageRequest.DamageAmount);

	// Local prediction may preview zero health for UI/state purposes, but destruction remains server-owned
	// until the authoritative destroy path replicates back to the client.
	OutResult.NewHealth = PredictedHealthAfterDamage;
	OutResult.bAppliedDamage = OutResult.NewHealth != ClampedHealth;
	OutResult.bDestroyed = false;
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
	if (!DamageResult.bAppliedDamage || DamageResult.NewHealth <= 0)
	{
		return false;
	}

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	if (!UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(DamageRequest.ResolvedHit.ChunkWorld, FeedbackComponent)
		|| FeedbackComponent == nullptr)
	{
		return false;
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
	PendingPredictedDamageRequestsByBlock.FindOrAdd(Key).Add(DamageRequest);
	const FChunkWorldBlockDamageResult* PreviousResult = PredictedBlockStates.Find(Key);
	if (!DidTrackedHealthStateChange(PreviousResult, DamageResult))
	{
		return;
	}

	PredictedBlockStates.Add(Key, DamageResult);
	SchedulePredictionPrune();
	BroadcastTrackedBlockStateChanged(DamageRequest.ResolvedHit.ChunkWorld, DamageRequest.ResolvedHit.BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::ApplyPredictedDamageRequest(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	FChunkWorldBlockDamageRequestResult& OutResult)
{
	const auto FinishRequest = [this, &DamageRequest, &OutResult](const bool bCallSucceeded)
	{
		UpdateLastDamageRequestDebugState(TEXT("PredictedDamageRequest"), DamageRequest, OutResult, bCallSucceeded);
		return bCallSucceeded;
	};

	OutResult = FChunkWorldBlockDamageRequestResult();
	if (!ValidateDamageRequest(DamageRequest))
	{
		return FinishRequest(false);
	}

	OutResult.bAccepted = true;

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Predicted block damage request ignored on '%s' because prediction requests must not run on authority."),
			*GetNameSafe(this));
		return FinishRequest(false);
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Client block damage request ignored on '%s' because no world was available."),
			*GetNameSafe(this));
		return FinishRequest(false);
	}

	PruneExpiredPredictions();

	FChunkWorldBlockDamageResult PredictedResult;
	if (!TryBuildPredictedDamageResult(DamageRequest, World->GetTimeSeconds(), PredictedResult))
	{
		return FinishRequest(false);
	}

	OutResult.DamageResult = PredictedResult;
	if (!PredictedResult.bAppliedDamage)
	{
		return FinishRequest(true);
	}

	StorePredictedDamageResult(DamageRequest, PredictedResult);
	OutResult.bPredictionWritten = true;
	OutResult.bPlayedImmediateLocalFeedback = TryApplyImmediateLocalFeedback(DamageRequest, PredictedResult);
	return FinishRequest(true);
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
	PendingPredictedDamageRequestsByBlock.Remove(MakeKey(ResolvedHit));
	SchedulePredictionPrune();
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

bool UPorismPredictedBlockStateComponent::ApplyAuthoritativeDamageRequest(
	const FChunkWorldBlockDamageRequest& DamageRequest,
	FChunkWorldBlockDamageRequestResult& OutResult)
{
	const auto FinishRequest = [this, &DamageRequest, &OutResult](const bool bCallSucceeded)
	{
		UpdateLastDamageRequestDebugState(TEXT("AuthoritativeDamageRequest"), DamageRequest, OutResult, bCallSucceeded);
		return bCallSucceeded;
	};

	OutResult = FChunkWorldBlockDamageRequestResult();
	if (!ValidateDamageRequest(DamageRequest))
	{
		return FinishRequest(false);
	}

	OutResult.bAccepted = true;
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Authoritative block damage request ignored on '%s' because authoritative requests must only run on authority."),
			*GetNameSafe(this));
		return FinishRequest(false);
	}

	FChunkWorldBlockDamageResult DamageResult;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(DamageRequest.ResolvedHit, DamageRequest.DamageAmount, DamageResult))
	{
		return FinishRequest(false);
	}

	OutResult.DamageResult = DamageResult;
	if (!DamageResult.bAppliedDamage && !DamageResult.bDestroyed)
	{
		return FinishRequest(true);
	}

	OutResult.bAuthoritativeDamageApplied = DamageResult.bAppliedDamage;
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

	return FinishRequest(true);
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

void UPorismPredictedBlockStateComponent::UpdateLastDamageRequestDebugState(
	const TCHAR* PathName,
	const FChunkWorldBlockDamageRequest& DamageRequest,
	const FChunkWorldBlockDamageRequestResult& RequestResult,
	const bool bCallSucceeded)
{
	FLastDamageRequestDebugState& DebugState = FCString::Strcmp(PathName, TEXT("PredictedDamageRequest")) == 0
		? LastPredictedDamageRequestDebugState
		: LastAuthoritativeDamageRequestDebugState;

	DebugState.bHasRecord = true;
	DebugState.bCallSucceeded = bCallSucceeded;
	DebugState.WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	DebugState.PathName = PathName;
	DebugState.Request = DamageRequest;
	DebugState.Result = RequestResult;
}

bool UPorismPredictedBlockStateComponent::ShouldDrawDebugStatsForPlayer(const APlayerController* DebugOwner) const
{
	if (!bShowDebugStats || DebugOwner == nullptr || GetOwner() == nullptr)
	{
		return false;
	}

	const APawn* ControlledPawn = DebugOwner->GetPawn();
	return ControlledPawn != nullptr && (ControlledPawn == GetOwner() || ControlledPawn == GetOwner()->GetOwner());
}

void UPorismPredictedBlockStateComponent::MaybeLogDebugStats(const FString& Snapshot)
{
	const UWorld* World = GetWorld();
	if (World == nullptr || !bShowDebugStats)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	constexpr float MinLogIntervalSeconds = 1.0f;
	if (Snapshot == LastDebugStatsLogSnapshot && (Now - LastDebugStatsLogTimeSeconds) < MinLogIntervalSeconds)
	{
		return;
	}

	LastDebugStatsLogSnapshot = Snapshot;
	LastDebugStatsLogTimeSeconds = Now;
	UE_LOG(LogPorismPredictedBlockState, Log, TEXT("%s"), *Snapshot);
}

void UPorismPredictedBlockStateComponent::DrawDebugStats(UCanvas* Canvas, APlayerController* DebugOwner)
{
	if (!ShouldDrawDebugStatsForPlayer(DebugOwner) || Canvas == nullptr)
	{
		return;
	}

	PruneExpiredPredictions();

	FString Snapshot = FString::Printf(
		TEXT("Porism Predicted Block State [%s] Owner=%s PredictedEntries=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		PredictedBlockStates.Num());

	float Y = 48.0f;
	DrawPredictedBlockStateDebugStatsLine(Canvas, Y, FString::Printf(TEXT("Porism Predicted Block State [%s]"), *GetNameSafe(this)), FColor::Cyan);
	int32 PendingPredictionCount = 0;
	for (const TPair<FPredictedBlockKey, TArray<FChunkWorldBlockDamageRequest>>& PendingEntry : PendingPredictedDamageRequestsByBlock)
	{
		PendingPredictionCount += PendingEntry.Value.Num();
	}
	DrawPredictedBlockStateDebugStatsLine(Canvas, Y, FString::Printf(TEXT("Owner=%s PredictedEntries=%d PendingPredictions=%d Timeout=%.2f"), *GetNameSafe(GetOwner()), PredictedBlockStates.Num(), PendingPredictionCount, PredictionTimeoutSeconds), FColor::White);
	Snapshot += FString::Printf(TEXT(" PendingPredictions=%d Timeout=%.2f"), PendingPredictionCount, PredictionTimeoutSeconds);

	const FLastDamageRequestDebugState* LatestDebugState = nullptr;
	if (LastPredictedDamageRequestDebugState.bHasRecord && LastAuthoritativeDamageRequestDebugState.bHasRecord)
	{
		LatestDebugState = LastPredictedDamageRequestDebugState.WorldTimeSeconds >= LastAuthoritativeDamageRequestDebugState.WorldTimeSeconds
			? &LastPredictedDamageRequestDebugState
			: &LastAuthoritativeDamageRequestDebugState;
	}
	else if (LastPredictedDamageRequestDebugState.bHasRecord)
	{
		LatestDebugState = &LastPredictedDamageRequestDebugState;
	}
	else if (LastAuthoritativeDamageRequestDebugState.bHasRecord)
	{
		LatestDebugState = &LastAuthoritativeDamageRequestDebugState;
	}

	if (LatestDebugState != nullptr)
	{
		const FChunkWorldResolvedBlockHit& LastResolvedHit = LatestDebugState->Request.ResolvedHit;
		int32 PredictedHealth = 0;
		bool bPredictedInvincible = false;
		FGameplayTag PredictedBlockTypeName;
		const bool bHasPredictedState = TryGetPredictedHealthState(LastResolvedHit, PredictedHealth, bPredictedInvincible, PredictedBlockTypeName);
		const int32 PendingPredictionsForBlock = PendingPredictedDamageRequestsByBlock.FindRef(MakeKey(LastResolvedHit)).Num();

		int32 AuthoritativeHealth = 0;
		int32 AuthoritativeMaxHealth = 0;
		bool bAuthoritativeInvincible = false;
		bool bHasRuntimeHealth = false;
		FGameplayTag AuthoritativeBlockTypeName;
		const bool bHasAuthoritativeState = IsValid(LastResolvedHit.ChunkWorld)
			&& UChunkWorldBlockDamageBlueprintLibrary::TryGetRuntimeBlockHealthStateForBlockWorldPos(
				LastResolvedHit.ChunkWorld,
				LastResolvedHit.BlockWorldPos,
				AuthoritativeHealth,
				AuthoritativeMaxHealth,
				bAuthoritativeInvincible,
				bHasRuntimeHealth,
				AuthoritativeBlockTypeName);

		FChunkWorldResolvedBlockHit CurrentResolvedHit;
		const bool bBlockStillResolves = IsValid(LastResolvedHit.ChunkWorld)
			&& UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromBlockWorldPos(LastResolvedHit.ChunkWorld, LastResolvedHit.BlockWorldPos, CurrentResolvedHit);

		const FString LiveStateSource = bHasPredictedState
			? TEXT("Predicted")
			: (bHasAuthoritativeState
				? (bHasRuntimeHealth ? TEXT("Authoritative") : TEXT("AuthoritativeFallback"))
				: (bBlockStillResolves ? TEXT("Unavailable") : TEXT("DestroyedOrRemoved")));
		Snapshot += FString::Printf(
			TEXT(" LastDamagedBlock=%s LiveSource=%s PredictedValid=%d AuthoritativeValid=%d HasRuntimeHealth=%d PendingForBlock=%d"),
			*LastResolvedHit.BlockWorldPos.ToString(),
			*LiveStateSource,
			bHasPredictedState,
			bHasAuthoritativeState,
			bHasRuntimeHealth,
			PendingPredictionsForBlock);

		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(
				TEXT("LastDamagedBlock=%s LiveSource=%s PredictedValid=%d AuthoritativeValid=%d HasRuntimeHealth=%d"),
				*LastResolvedHit.BlockWorldPos.ToString(),
				*LiveStateSource,
				bHasPredictedState,
				bHasAuthoritativeState,
				bHasRuntimeHealth),
			FColor::Yellow);
		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(
				TEXT("  pending=%d live predicted=%s live authoritative=%s"),
				PendingPredictionsForBlock,
				bHasPredictedState
					? *FString::Printf(TEXT("%d inv=%d type=%s"), PredictedHealth, bPredictedInvincible, *PredictedBlockTypeName.ToString())
					: TEXT("none"),
				bHasAuthoritativeState
					? *FString::Printf(TEXT("%d/%d inv=%d type=%s"), AuthoritativeHealth, AuthoritativeMaxHealth, bAuthoritativeInvincible, *AuthoritativeBlockTypeName.ToString())
					: TEXT("none")),
			FColor::White);
	}

	const auto DrawRequestState = [Canvas, &Y](const FLastDamageRequestDebugState& DebugState, const TCHAR* Label)
	{
		if (!DebugState.bHasRecord)
		{
			DrawPredictedBlockStateDebugStatsLine(Canvas, Y, FString::Printf(TEXT("%s: none"), Label), FColor::Silver);
			return;
		}

		const FColor HeaderColor = DebugState.bCallSucceeded ? FColor::Green : FColor::Red;
		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(TEXT("%s t=%.2f success=%d accepted=%d context=%s"), Label, DebugState.WorldTimeSeconds, DebugState.bCallSucceeded, DebugState.Result.bAccepted, *DebugState.Request.RequestContextTag.ToString()),
			HeaderColor);
		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(
				TEXT("  block=%s damage=%d predicted=%d authoritative=%d feedback=%d"),
				*DebugState.Request.ResolvedHit.BlockWorldPos.ToString(),
				DebugState.Request.DamageAmount,
				DebugState.Result.bPredictionWritten,
				DebugState.Result.bAuthoritativeDamageApplied,
				DebugState.Result.bPlayedImmediateLocalFeedback),
			FColor::White);
		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(
				TEXT("  applied=%d destroyed=%d schema=%d health=%d->%d type=%s"),
				DebugState.Result.DamageResult.bAppliedDamage,
				DebugState.Result.DamageResult.bDestroyed,
				DebugState.Result.DamageResult.bHasDamageSchema,
				DebugState.Result.DamageResult.PreviousHealth,
				DebugState.Result.DamageResult.NewHealth,
				*DebugState.Result.DamageResult.BlockTypeName.ToString()),
			FColor::White);
	};

	DrawRequestState(LastPredictedDamageRequestDebugState, TEXT("Predicted"));
	DrawRequestState(LastAuthoritativeDamageRequestDebugState, TEXT("Authoritative"));
	Snapshot += FString::Printf(
		TEXT(" PredictedRequest=%d AuthoritativeRequest=%d"),
		LastPredictedDamageRequestDebugState.bHasRecord,
		LastAuthoritativeDamageRequestDebugState.bHasRecord);
	MaybeLogDebugStats(Snapshot);
}
