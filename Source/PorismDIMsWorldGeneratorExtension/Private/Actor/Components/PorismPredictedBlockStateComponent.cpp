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

	bool TryResolveHealthDefinitionForHealthView(
		const FChunkWorldResolvedBlockHit& ResolvedHit,
		FGameplayTag PreferredBlockTypeName,
		FGameplayTag& OutResolvedBlockTypeName,
		FBlockHealthDefinition& OutDefinition)
	{
		OutResolvedBlockTypeName = FGameplayTag();
		OutDefinition = FBlockHealthDefinition();

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
			&& UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionStruct, OutDefinition))
		{
			OutResolvedBlockTypeName = PreferredBlockTypeName;
			return true;
		}

		if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, OutResolvedBlockTypeName, DefinitionStruct))
		{
			return false;
		}

		return UBlockTypeSchemaBlueprintLibrary::TryGetBlockHealthDefinition(DefinitionStruct, OutDefinition);
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
			PendingPredictedRequestCountsByBlock.Remove(It.Key());
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
	for (const TPair<FPredictedBlockKey, FChunkWorldBlockHealthDeltaResult>& PredictedEntry : PredictedBlockStates)
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

void UPorismPredictedBlockStateComponent::HandleObservedChunkWorldSettledTransition(
	AChunkWorldExtended* ChunkWorld,
	const FChunkWorldSettledBlockTransition& Transition)
{
	if (ChunkWorld == nullptr)
	{
		return;
	}

	const FPredictedBlockKey Key = MakeKey(ChunkWorld, Transition.BlockWorldPos);
	const bool bHadPredictedState = PredictedBlockStates.Contains(Key);

	// Any replicated health-related update for the same block invalidates the local prediction.
	// Authoritative block state may legitimately raise or lower health, may batch multiple local hits,
	// or may include unrelated server-side damage/healing sources. Without an explicit authoritative
	// revision or ack token, the client cannot safely map one replicated update to a subset of queued
	// local predictions, so the entire local prediction state for that block must be discarded.
	const bool bShouldClearPrediction = bHadPredictedState && Transition.bTouchedHealth;

	if (bShouldClearPrediction)
	{
		PredictedBlockStates.Remove(Key);
		PendingPredictedRequestCountsByBlock.Remove(Key);
		SchedulePredictionPrune();
	}

	// Project-specific destruction targeting fix: once health has settled to zero or representation has
	// been removed, keep local targeting off that block even if Porism presentation data lingers briefly.
	// Only a real positive-health recovery should clear suppression again; lingering representation alone
	// must not make a destroyed overlay targetable.
	const bool bShouldSuppressTarget = Transition.bObservedRepresentationRemoved
		|| (Transition.bHasCurrentHealth && Transition.CurrentHealth <= 0);
	const bool bShouldClearSuppression = !bShouldSuppressTarget
		&& Transition.bHasCurrentHealth
		&& Transition.CurrentHealth > 0
		&& (Transition.bObservedHealthIncrease || Transition.bTouchedHealth);
	if (bShouldSuppressTarget)
	{
		SetBlockTargetSuppressed(ChunkWorld, Transition.BlockWorldPos, true);
	}
	else if (bShouldClearSuppression)
	{
		SetBlockTargetSuppressed(ChunkWorld, Transition.BlockWorldPos, false);
	}

	BroadcastTrackedBlockStateChanged(ChunkWorld, Transition.BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::ValidateHealthRequest(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const int32 Amount,
	const bool bIsHealing) const
{
	const TCHAR* RequestKind = bIsHealing ? TEXT("healing") : TEXT("damage");
	if (Amount <= 0)
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Block %s request ignored on '%s' because the amount was %d."),
			RequestKind,
			*GetNameSafe(this),
			Amount);
		return false;
	}

	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Block %s request ignored on '%s' because the resolved hit was invalid."),
			RequestKind,
			*GetNameSafe(this));
		return false;
	}

	return true;
}

bool UPorismPredictedBlockStateComponent::TryBuildPredictedHealthChangeResult(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const int32 Amount,
	const bool bIsHealing,
	const float PredictionTimeSeconds,
	FChunkWorldBlockHealthDeltaResult& OutResult) const
{
	OutResult = FChunkWorldBlockHealthDeltaResult();
	if (!ValidateHealthRequest(ResolvedHit, Amount, bIsHealing))
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
	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	if (const FChunkWorldBlockHealthDeltaResult* ExistingPredictedResult = PredictedBlockStates.Find(Key))
	{
		CurrentHealth = ExistingPredictedResult->NewHealth;
		bIsInvincible = ExistingPredictedResult->bWasInvincible;
		BlockTypeName = ExistingPredictedResult->BlockTypeName;
	}
	else if (!UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(ResolvedHit, CurrentHealth, bIsInvincible, BlockTypeName))
	{
		return false;
	}

	FGameplayTag DefinitionBlockTypeName;
	FBlockHealthDefinition HealthDefinition;
	if (!TryResolveHealthDefinitionForHealthView(ResolvedHit, BlockTypeName, DefinitionBlockTypeName, HealthDefinition))
	{
		return false;
	}

	OutResult.bHasHealthSchema = true;
	OutResult.BlockTypeName = BlockTypeName.IsValid() ? BlockTypeName : DefinitionBlockTypeName;
	OutResult.bWasInvincible = bIsInvincible;
	OutResult.PreviousHealth = CurrentHealth;

	if (bIsHealing)
	{
		const int32 ClampedHealth = FMath::Clamp(CurrentHealth, 0, HealthDefinition.MaxHealth);
		OutResult.NewHealth = FMath::Min(HealthDefinition.MaxHealth, ClampedHealth + Amount);
		OutResult.HealingApplied = OutResult.NewHealth - ClampedHealth;
		OutResult.bAppliedHealing = OutResult.HealingApplied > 0;
		OutResult.bAppliedHealthChange = OutResult.bAppliedHealing;
		return true;
	}

	OutResult.DamageApplied = Amount;
	if (bIsInvincible)
	{
		OutResult.NewHealth = CurrentHealth;
		return true;
	}

	const int32 ClampedHealth = FMath::Max(0, CurrentHealth);
	const int32 PredictedHealthAfterDamage = FMath::Max(0, ClampedHealth - Amount);

	// Local prediction may preview zero health for UI/state purposes, but destruction remains server-owned
	// until the authoritative destroy path replicates back to the client.
	OutResult.NewHealth = PredictedHealthAfterDamage;
	OutResult.bAppliedDamage = OutResult.NewHealth != ClampedHealth;
	OutResult.bAppliedHealthChange = OutResult.bAppliedDamage;
	OutResult.bDestroyed = false;
	return true;
}

bool UPorismPredictedBlockStateComponent::DidTrackedHealthStateChange(
	const FChunkWorldBlockHealthDeltaResult* PreviousResult,
	const FChunkWorldBlockHealthDeltaResult& NewResult) const
{
	if (PreviousResult == nullptr)
	{
		return NewResult.bAppliedHealthChange || NewResult.bWasInvincible;
	}

	return PreviousResult->NewHealth != NewResult.NewHealth
		|| PreviousResult->bWasInvincible != NewResult.bWasInvincible
		|| PreviousResult->BlockTypeName != NewResult.BlockTypeName
		|| PreviousResult->bDestroyed != NewResult.bDestroyed;
}

bool UPorismPredictedBlockStateComponent::TryApplyImmediateLocalDamageFeedback(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const FChunkWorldBlockHealthDeltaResult& DamageResult) const
{
	if (!DamageResult.bAppliedDamage || DamageResult.NewHealth <= 0)
	{
		return false;
	}

	UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
	if (!UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(ResolvedHit.ChunkWorld, FeedbackComponent)
		|| FeedbackComponent == nullptr)
	{
		return false;
	}

	return FeedbackComponent->RequestImmediateLocalHitFeedback(ResolvedHit);
}

void UPorismPredictedBlockStateComponent::StorePredictedHealthChangeResult(
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const FChunkWorldBlockHealthDeltaResult& DamageResult)
{
	if (!DamageResult.bUsedPredictedWrite || !DamageResult.bHitWasRepresentedBlock)
	{
		return;
	}

	PruneExpiredPredictions();

	const FPredictedBlockKey Key = MakeKey(ResolvedHit);
	int32& PendingRequestCount = PendingPredictedRequestCountsByBlock.FindOrAdd(Key);
	++PendingRequestCount;
	const FChunkWorldBlockHealthDeltaResult* PreviousResult = PredictedBlockStates.Find(Key);
	if (!DidTrackedHealthStateChange(PreviousResult, DamageResult))
	{
		return;
	}

	PredictedBlockStates.Add(Key, DamageResult);
	SchedulePredictionPrune();
	if (DamageResult.NewHealth <= 0)
	{
		SetBlockTargetSuppressed(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos, true);
	}
	BroadcastTrackedBlockStateChanged(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos);
}

bool UPorismPredictedBlockStateComponent::ApplyPredictedDamageRequest(
	const FChunkWorldBlockHealthDeltaRequest& DamageRequest,
	FChunkWorldBlockHealthDeltaRequestResult& OutResult)
{
	const auto FinishRequest = [this, &DamageRequest, &OutResult](const bool bCallSucceeded)
	{
		UpdateLastHealthRequestDebugState(
			TEXT("PredictedDamageRequest"),
			DamageRequest.ResolvedHit,
			DamageRequest.Amount,
			DamageRequest.RequestContextTag,
			false,
			OutResult,
			bCallSucceeded);
		return bCallSucceeded;
	};

	OutResult = FChunkWorldBlockHealthDeltaRequestResult();
	if (!ValidateHealthRequest(DamageRequest.ResolvedHit, DamageRequest.Amount, false))
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

	FChunkWorldBlockHealthDeltaResult PredictedResult;
	if (!TryBuildPredictedHealthChangeResult(DamageRequest.ResolvedHit, DamageRequest.Amount, false, World->GetTimeSeconds(), PredictedResult))
	{
		return FinishRequest(false);
	}

	OutResult.HealthDeltaResult = PredictedResult;
	if (!PredictedResult.bAppliedHealthChange)
	{
		return FinishRequest(true);
	}

	StorePredictedHealthChangeResult(DamageRequest.ResolvedHit, PredictedResult);
	OutResult.bPredictionWritten = true;
	OutResult.bPlayedImmediateLocalFeedback = TryApplyImmediateLocalDamageFeedback(DamageRequest.ResolvedHit, PredictedResult);
	return FinishRequest(true);
}

bool UPorismPredictedBlockStateComponent::ApplyPredictedHealingRequest(
	const FChunkWorldBlockHealthDeltaRequest& HealingRequest,
	FChunkWorldBlockHealthDeltaRequestResult& OutResult)
{
	const auto FinishRequest = [this, &HealingRequest, &OutResult](const bool bCallSucceeded)
	{
		UpdateLastHealthRequestDebugState(
			TEXT("PredictedHealingRequest"),
			HealingRequest.ResolvedHit,
			HealingRequest.Amount,
			HealingRequest.RequestContextTag,
			true,
			OutResult,
			bCallSucceeded);
		return bCallSucceeded;
	};

	OutResult = FChunkWorldBlockHealthDeltaRequestResult();
	if (!ValidateHealthRequest(HealingRequest.ResolvedHit, HealingRequest.Amount, true))
	{
		return FinishRequest(false);
	}

	OutResult.bAccepted = true;

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Predicted block healing request ignored on '%s' because prediction requests must not run on authority."),
			*GetNameSafe(this));
		return FinishRequest(false);
	}

	const UWorld* World = GetWorld();
	if (World == nullptr)
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Client block healing request ignored on '%s' because no world was available."),
			*GetNameSafe(this));
		return FinishRequest(false);
	}

	PruneExpiredPredictions();

	FChunkWorldBlockHealthDeltaResult PredictedResult;
	if (!TryBuildPredictedHealthChangeResult(HealingRequest.ResolvedHit, HealingRequest.Amount, true, World->GetTimeSeconds(), PredictedResult))
	{
		return FinishRequest(false);
	}

	OutResult.HealthDeltaResult = PredictedResult;
	if (!PredictedResult.bAppliedHealthChange)
	{
		return FinishRequest(true);
	}

	StorePredictedHealthChangeResult(HealingRequest.ResolvedHit, PredictedResult);
	OutResult.bPredictionWritten = true;
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
		FBlockHealthDefinition HealthDefinition;
		if (!TryResolveHealthDefinitionForHealthView(ResolvedHit, OutState.BlockTypeName, DefinitionBlockTypeName, HealthDefinition))
		{
			return false;
		}

		OutState.MaxHealth = HealthDefinition.MaxHealth;
		OutState.bUsingPredictedHealth = true;
		if (!OutState.BlockTypeName.IsValid())
		{
			OutState.BlockTypeName = DefinitionBlockTypeName;
		}
		return true;
	}

	FGameplayTag DefinitionBlockTypeName;
	FBlockHealthDefinition HealthDefinition;
	if (!TryResolveHealthDefinitionForHealthView(ResolvedHit, FGameplayTag(), DefinitionBlockTypeName, HealthDefinition))
	{
		return false;
	}

	OutState.MaxHealth = HealthDefinition.MaxHealth;

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
	PendingPredictedRequestCountsByBlock.Remove(MakeKey(ResolvedHit));
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
	if (const FChunkWorldBlockHealthDeltaResult* PredictedResult = PredictedBlockStates.Find(Key))
	{
		OutHealth = PredictedResult->NewHealth;
		bOutInvincible = PredictedResult->bWasInvincible;
		OutBlockTypeName = PredictedResult->BlockTypeName;
		return true;
	}

	return false;
}

bool UPorismPredictedBlockStateComponent::IsBlockTargetSuppressed(const FChunkWorldResolvedBlockHit& ResolvedHit) const
{
	if (!ResolvedHit.bHasBlock || !IsValid(ResolvedHit.ChunkWorld))
	{
		return false;
	}

	return SuppressedTargetBlocks.Contains(MakeKey(ResolvedHit));
}

bool UPorismPredictedBlockStateComponent::TryResolveTargetableBlockHit(
	const FChunkWorldResolvedBlockHit& CandidateHit,
	FChunkWorldResolvedBlockHit& OutResolvedHit) const
{
	OutResolvedHit = CandidateHit;
	if (!CandidateHit.bHasBlock || !IsValid(CandidateHit.ChunkWorld) || !IsBlockTargetSuppressed(CandidateHit))
	{
		return CandidateHit.bHasBlock;
	}

	const FIntVector SupportBlockWorldPos = CandidateHit.BlockWorldPos - FIntVector(0, 0, 1);
	if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveDirectBlockHitContextFromBlockWorldPos(
		CandidateHit.ChunkWorld,
		SupportBlockWorldPos,
		OutResolvedHit))
	{
		OutResolvedHit = FChunkWorldResolvedBlockHit();
		return false;
	}

	if (IsBlockTargetSuppressed(OutResolvedHit))
	{
		OutResolvedHit = FChunkWorldResolvedBlockHit();
		return false;
	}

	return true;
}

bool UPorismPredictedBlockStateComponent::ApplyAuthoritativeDamageRequest(
	const FChunkWorldBlockHealthDeltaRequest& DamageRequest,
	FChunkWorldBlockHealthDeltaRequestResult& OutResult)
{
	const auto FinishRequest = [this, &DamageRequest, &OutResult](const bool bCallSucceeded)
	{
		UpdateLastHealthRequestDebugState(
			TEXT("AuthoritativeDamageRequest"),
			DamageRequest.ResolvedHit,
			DamageRequest.Amount,
			DamageRequest.RequestContextTag,
			false,
			OutResult,
			bCallSucceeded);
		return bCallSucceeded;
	};

	OutResult = FChunkWorldBlockHealthDeltaRequestResult();
	if (!ValidateHealthRequest(DamageRequest.ResolvedHit, DamageRequest.Amount, false))
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

	FChunkWorldBlockHealthDeltaResult DamageResult;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(DamageRequest.ResolvedHit, DamageRequest.Amount, DamageResult))
	{
		return FinishRequest(false);
	}

	OutResult.HealthDeltaResult = DamageResult;
	if (!DamageResult.bAppliedDamage && !DamageResult.bDestroyed)
	{
		return FinishRequest(true);
	}

	OutResult.bAuthoritativeDamageApplied = DamageResult.bAppliedDamage;
	if (DamageResult.NewHealth <= 0)
	{
		SetBlockTargetSuppressed(DamageRequest.ResolvedHit.ChunkWorld, DamageRequest.ResolvedHit.BlockWorldPos, true);
	}
	if (!DamageResult.bDestroyed)
	{
		UChunkWorldBlockFeedbackComponent* FeedbackComponent = nullptr;
		if (UChunkWorldBlockHitBlueprintLibrary::GetBlockFeedbackComponentFromChunkWorld(DamageRequest.ResolvedHit.ChunkWorld, FeedbackComponent)
			&& FeedbackComponent != nullptr)
		{
			(void)FeedbackComponent->BroadcastAuthoritativeHitFeedback(DamageRequest.ResolvedHit);
		}
	}
	// Lethal damage already writes committed health custom data, and that settled
	// health callback owns the authoritative DestroyBlock() transition. Calling
	// DestroyBlock() here as well would double-fire destruction presentation for
	// the same block on authority.

	return FinishRequest(true);
}

bool UPorismPredictedBlockStateComponent::ApplyAuthoritativeHealingRequest(
	const FChunkWorldBlockHealthDeltaRequest& HealingRequest,
	FChunkWorldBlockHealthDeltaRequestResult& OutResult)
{
	const auto FinishRequest = [this, &HealingRequest, &OutResult](const bool bCallSucceeded)
	{
		UpdateLastHealthRequestDebugState(
			TEXT("AuthoritativeHealingRequest"),
			HealingRequest.ResolvedHit,
			HealingRequest.Amount,
			HealingRequest.RequestContextTag,
			true,
			OutResult,
			bCallSucceeded);
		return bCallSucceeded;
	};

	OutResult = FChunkWorldBlockHealthDeltaRequestResult();
	if (!ValidateHealthRequest(HealingRequest.ResolvedHit, HealingRequest.Amount, true))
	{
		return FinishRequest(false);
	}

	OutResult.bAccepted = true;
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		UE_LOG(
			LogPorismPredictedBlockState,
			Warning,
			TEXT("Authoritative block healing request ignored on '%s' because authoritative requests must only run on authority."),
			*GetNameSafe(this));
		return FinishRequest(false);
	}

	FChunkWorldBlockHealthDeltaResult HealingResult;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockHealingForResolvedBlockHit(HealingRequest.ResolvedHit, HealingRequest.Amount, HealingResult))
	{
		return FinishRequest(false);
	}

	OutResult.HealthDeltaResult = HealingResult;
	if (!HealingResult.bAppliedHealing)
	{
		return FinishRequest(true);
	}

	OutResult.bAuthoritativeHealingApplied = true;
	if (HealingResult.NewHealth > 0)
	{
		SetBlockTargetSuppressed(HealingRequest.ResolvedHit.ChunkWorld, HealingRequest.ResolvedHit.BlockWorldPos, false);
	}
	return FinishRequest(true);
}

void UPorismPredictedBlockStateComponent::SetBlockTargetSuppressed(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const bool bSuppressed)
{
	if (!IsValid(ChunkWorld))
	{
		return;
	}

	const FPredictedBlockKey Key = MakeKey(ChunkWorld, BlockWorldPos);
	if (bSuppressed)
	{
		SuppressedTargetBlocks.Add(Key);
		return;
	}

	SuppressedTargetBlocks.Remove(Key);
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

	ChunkWorldExtended->OnSettledBlockTransition.AddDynamic(this, &UPorismPredictedBlockStateComponent::HandleObservedChunkWorldSettledTransition);
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
			ChunkWorldInstance->OnSettledBlockTransition.RemoveDynamic(this, &UPorismPredictedBlockStateComponent::HandleObservedChunkWorldSettledTransition);
		}
	}

	ObservedChunkWorlds.Reset();
}

void UPorismPredictedBlockStateComponent::HandleActorSpawned(AActor* SpawnedActor)
{
	BindObservedChunkWorld(Cast<AChunkWorldExtended>(SpawnedActor));
}

void UPorismPredictedBlockStateComponent::UpdateLastHealthRequestDebugState(
	const TCHAR* PathName,
	const FChunkWorldResolvedBlockHit& ResolvedHit,
	const int32 RequestedAmount,
	const FName RequestContextTag,
	const bool bIsHealing,
	const FChunkWorldBlockHealthDeltaRequestResult& RequestResult,
	const bool bCallSucceeded)
{
	FLastHealthRequestDebugState& DebugState = FCString::Strncmp(PathName, TEXT("Predicted"), 9) == 0
		? LastPredictedHealthRequestDebugState
		: LastAuthoritativeHealthRequestDebugState;

	DebugState.bHasRecord = true;
	DebugState.bIsHealing = bIsHealing;
	DebugState.bCallSucceeded = bCallSucceeded;
	DebugState.WorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	DebugState.PathName = PathName;
	DebugState.ResolvedHit = ResolvedHit;
	DebugState.RequestedAmount = RequestedAmount;
	DebugState.RequestContextTag = RequestContextTag;
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
	for (const TPair<FPredictedBlockKey, int32>& PendingEntry : PendingPredictedRequestCountsByBlock)
	{
		PendingPredictionCount += PendingEntry.Value;
	}
	DrawPredictedBlockStateDebugStatsLine(Canvas, Y, FString::Printf(TEXT("Owner=%s PredictedEntries=%d PendingPredictions=%d Timeout=%.2f"), *GetNameSafe(GetOwner()), PredictedBlockStates.Num(), PendingPredictionCount, PredictionTimeoutSeconds), FColor::White);
	Snapshot += FString::Printf(TEXT(" PendingPredictions=%d Timeout=%.2f"), PendingPredictionCount, PredictionTimeoutSeconds);

	const FLastHealthRequestDebugState* LatestDebugState = nullptr;
	if (LastPredictedHealthRequestDebugState.bHasRecord && LastAuthoritativeHealthRequestDebugState.bHasRecord)
	{
		LatestDebugState = LastPredictedHealthRequestDebugState.WorldTimeSeconds >= LastAuthoritativeHealthRequestDebugState.WorldTimeSeconds
			? &LastPredictedHealthRequestDebugState
			: &LastAuthoritativeHealthRequestDebugState;
	}
	else if (LastPredictedHealthRequestDebugState.bHasRecord)
	{
		LatestDebugState = &LastPredictedHealthRequestDebugState;
	}
	else if (LastAuthoritativeHealthRequestDebugState.bHasRecord)
	{
		LatestDebugState = &LastAuthoritativeHealthRequestDebugState;
	}

	if (LatestDebugState != nullptr)
	{
		const FChunkWorldResolvedBlockHit& LastResolvedHit = LatestDebugState->ResolvedHit;
		int32 PredictedHealth = 0;
		bool bPredictedInvincible = false;
		FGameplayTag PredictedBlockTypeName;
		const bool bHasPredictedState = TryGetPredictedHealthState(LastResolvedHit, PredictedHealth, bPredictedInvincible, PredictedBlockTypeName);
		const int32 PendingPredictionsForBlock = PendingPredictedRequestCountsByBlock.FindRef(MakeKey(LastResolvedHit));

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
			TEXT(" LastTouchedBlock=%s LiveSource=%s PredictedValid=%d AuthoritativeValid=%d HasRuntimeHealth=%d PendingForBlock=%d"),
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
				TEXT("LastTouchedBlock=%s LiveSource=%s PredictedValid=%d AuthoritativeValid=%d HasRuntimeHealth=%d"),
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

	const auto DrawRequestState = [Canvas, &Y](const FLastHealthRequestDebugState& DebugState, const TCHAR* Label)
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
			FString::Printf(
				TEXT("%s t=%.2f success=%d accepted=%d kind=%s context=%s"),
				Label,
				DebugState.WorldTimeSeconds,
				DebugState.bCallSucceeded,
				DebugState.Result.bAccepted,
				DebugState.bIsHealing ? TEXT("Heal") : TEXT("Damage"),
				*DebugState.RequestContextTag.ToString()),
			HeaderColor);
		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(
				TEXT("  block=%s amount=%d predicted=%d authDamage=%d authHealing=%d feedback=%d"),
				*DebugState.ResolvedHit.BlockWorldPos.ToString(),
				DebugState.RequestedAmount,
				DebugState.Result.bPredictionWritten,
				DebugState.Result.bAuthoritativeDamageApplied,
				DebugState.Result.bAuthoritativeHealingApplied,
				DebugState.Result.bPlayedImmediateLocalFeedback),
			FColor::White);
		DrawPredictedBlockStateDebugStatsLine(
			Canvas,
			Y,
			FString::Printf(
				TEXT("  damage=%d heal=%d destroyed=%d schema=%d health=%d->%d type=%s"),
				DebugState.Result.HealthDeltaResult.bAppliedDamage,
				DebugState.Result.HealthDeltaResult.bAppliedHealing,
				DebugState.Result.HealthDeltaResult.bDestroyed,
				DebugState.Result.HealthDeltaResult.bHasHealthSchema,
				DebugState.Result.HealthDeltaResult.PreviousHealth,
				DebugState.Result.HealthDeltaResult.NewHealth,
				*DebugState.Result.HealthDeltaResult.BlockTypeName.ToString()),
			FColor::White);
	};

	DrawRequestState(LastPredictedHealthRequestDebugState, TEXT("Predicted"));
	DrawRequestState(LastAuthoritativeHealthRequestDebugState, TEXT("Authoritative"));
	Snapshot += FString::Printf(
		TEXT(" PredictedRequest=%d AuthoritativeRequest=%d"),
		LastPredictedHealthRequestDebugState.bHasRecord,
		LastAuthoritativeHealthRequestDebugState.bHasRecord);
	MaybeLogDebugStats(Snapshot);
}
