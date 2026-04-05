// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismPredictedBlockStateComponent.h"

#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/ChunkWorld.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

namespace
{
	TArray<TWeakObjectPtr<UPorismPredictedBlockStateComponent>> GRegisteredPredictedBlockStateComponents;
}

UPorismPredictedBlockStateComponent::UPorismPredictedBlockStateComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPorismPredictedBlockStateComponent::NotifyAuthoritativeBlockStateUpdated(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
{
	if (!IsValid(ChunkWorld))
	{
		return;
	}

	for (int32 Index = GRegisteredPredictedBlockStateComponents.Num() - 1; Index >= 0; --Index)
	{
		UPorismPredictedBlockStateComponent* PredictionComponent = GRegisteredPredictedBlockStateComponents[Index].Get();
		if (PredictionComponent == nullptr)
		{
			GRegisteredPredictedBlockStateComponents.RemoveAtSwap(Index);
			continue;
		}

		if (PredictionComponent->GetWorld() != ChunkWorld->GetWorld())
		{
			continue;
		}

		PredictionComponent->PredictedBlockStates.Remove(MakeKey(ChunkWorld, BlockWorldPos));
	}
}

void UPorismPredictedBlockStateComponent::BeginPlay()
{
	Super::BeginPlay();
	PruneExpiredPredictions();

	if (ShouldRegisterPredictionNotifications())
	{
		GRegisteredPredictedBlockStateComponents.AddUnique(this);
	}
}

void UPorismPredictedBlockStateComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	GRegisteredPredictedBlockStateComponents.Remove(this);
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

	if (World->GetNetMode() != NM_Standalone && GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		return false;
	}

	const AController* OwnerController = Cast<AController>(GetOwner());
	if (OwnerController != nullptr)
	{
		return OwnerController->IsLocalController();
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (OwnerPawn != nullptr)
	{
		return OwnerPawn->IsLocallyControlled();
	}

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
}

void UPorismPredictedBlockStateComponent::ClearPredictionForResolvedBlockHit(const FChunkWorldResolvedBlockHit& ResolvedHit)
{
	PredictedBlockStates.Remove(MakeKey(ResolvedHit));
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
