// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismDamageTraceInteractionComponent.h"

#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Blueprint/ChunkWorldHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "GameFramework/Pawn.h"

UPorismDamageTraceInteractionComponent::UPorismDamageTraceInteractionComponent()
{
}

FChunkWorldDamageBlockInteractionResult UPorismDamageTraceInteractionComponent::GetLastDamageBlockInteractionResult() const
{
	return LastDamageBlockInteractionResult;
}

bool UPorismDamageTraceInteractionComponent::CanApplyDamageToCurrentBlock() const
{
	return bHasActiveDamageBlockInteraction
		&& LastDamageBlockInteractionResult.bSupportsHealth
		&& LastDamageBlockInteractionResult.bIsDestructible;
}

bool UPorismDamageTraceInteractionComponent::ApplyDamageToCurrentBlock(int32 DamageAmount, int32& OutNewHealth)
{
	OutNewHealth = 0;
	if (DamageAmount <= 0 || !CanApplyDamageToCurrentBlock())
	{
		return false;
	}

	const FChunkWorldDamageBlockInteractionResult DamageInteractionResult = LastDamageBlockInteractionResult;
	const FVector TraceVector = DamageInteractionResult.Hit.TraceEnd - DamageInteractionResult.Hit.TraceStart;
	const FVector TraceDirection = TraceVector.IsNearlyZero() ? FVector::ZeroVector : TraceVector.GetSafeNormal();
	if (TraceDirection.IsNearlyZero())
	{
		return false;
	}

	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr)
	{
		return false;
	}

	if (!PawnOwner->HasAuthority())
	{
		if (UPorismPredictedBlockStateComponent* PredictedBlockStateComponent = GetPredictedBlockStateComponent())
		{
			FChunkWorldBlockDamageResult PredictedResult;
			const float PredictionTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
			if (UChunkWorldBlockDamageBlueprintLibrary::TryApplyPredictedBlockDamageForResolvedBlockHit(
				DamageInteractionResult.ResolvedBlockHit,
				DamageAmount,
				PredictionTimeSeconds,
				PredictedResult))
			{
				PredictedBlockStateComponent->StorePredictedDamageResult(DamageInteractionResult.ResolvedBlockHit, PredictedResult);
				if (PredictedResult.bAppliedDamage)
				{
					if (PredictedResult.bDestroyed)
					{
						(void)UChunkWorldHitBlueprintLibrary::TryBroadcastDestroyedFeedbackForResolvedBlockHit(DamageInteractionResult.ResolvedBlockHit);
					}
					else
					{
						(void)UChunkWorldBlockDamageBlueprintLibrary::TryBroadcastHitFeedbackForResolvedBlockHit(DamageInteractionResult.ResolvedBlockHit);
					}
				}
				OutNewHealth = PredictedResult.NewHealth;
				RefreshDamageInteractionState(true);
			}
		}

		ServerApplyDamageToCurrentBlock(DamageInteractionResult.Hit, FVector_NetQuantizeNormal(TraceDirection), DamageAmount);
		return true;
	}

	FChunkWorldBlockDamageResult DamageResult;
	if (!UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(DamageInteractionResult.ResolvedBlockHit, DamageAmount, DamageResult))
	{
		return false;
	}

	OutNewHealth = DamageResult.NewHealth;
	RefreshDamageInteractionState(true);
	return true;
}

void UPorismDamageTraceInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	OnBlockInteractionStarted.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionStarted);
	OnBlockInteractionEnded.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionEnded);
	OnBlockInteractionUpdated.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionUpdated);
	OnBlockCustomDataMaterialized.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockCustomDataMaterialized);

	RefreshDamageInteractionState(false);
}

void UPorismDamageTraceInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnBlockInteractionStarted.RemoveAll(this);
	OnBlockInteractionEnded.RemoveAll(this);
	OnBlockInteractionUpdated.RemoveAll(this);
	OnBlockCustomDataMaterialized.RemoveAll(this);

	const bool bHadActiveDamageInteraction = bHasActiveDamageBlockInteraction;
	const FChunkWorldDamageBlockInteractionResult PreviousDamageInteractionResult = LastDamageBlockInteractionResult;
	LastDamageBlockInteractionResult = FChunkWorldDamageBlockInteractionResult();
	bHasActiveDamageBlockInteraction = false;

	if (bHadActiveDamageInteraction)
	{
		OnDamageBlockInteractionEnded.Broadcast(PreviousDamageInteractionResult);
	}

	Super::EndPlay(EndPlayReason);
}

bool UPorismDamageTraceInteractionComponent::ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const
{
	FChunkWorldDamageBlockInteractionResult DamageBlockResult;
	return TryBuildDamageBlockInteractionResult(BlockResult, DamageBlockResult);
}

bool UPorismDamageTraceInteractionComponent::DidBlockInteractionResultChange(bool bHadPreviousResult, const FChunkWorldBlockInteractionResult& PreviousResult, bool bHasNewResult, const FChunkWorldBlockInteractionResult& NewResult) const
{
	FChunkWorldDamageBlockInteractionResult PreviousDamageBlockResult;
	const bool bHadPreviousDamageResult = bHadPreviousResult && TryBuildDamageBlockInteractionResult(PreviousResult, PreviousDamageBlockResult);

	FChunkWorldDamageBlockInteractionResult NewDamageBlockResult;
	const bool bHasNewDamageResult = bHasNewResult && TryBuildDamageBlockInteractionResult(NewResult, NewDamageBlockResult);

	return bHadPreviousDamageResult != bHasNewDamageResult
		|| PreviousDamageBlockResult.ResolvedBlockHit.BlockWorldPos != NewDamageBlockResult.ResolvedBlockHit.BlockWorldPos
		|| PreviousDamageBlockResult.bIsDestructible != NewDamageBlockResult.bIsDestructible
		|| PreviousDamageBlockResult.bHasCustomData != NewDamageBlockResult.bHasCustomData
		|| PreviousDamageBlockResult.bUsingPredictedHealth != NewDamageBlockResult.bUsingPredictedHealth
		|| PreviousDamageBlockResult.CurrentHealth != NewDamageBlockResult.CurrentHealth
		|| PreviousDamageBlockResult.MaxHealth != NewDamageBlockResult.MaxHealth;
}

void UPorismDamageTraceInteractionComponent::HandleBlockInteractionStarted(const FChunkWorldBlockInteractionResult& Result)
{
	const bool bHadActiveDamageInteraction = bHasActiveDamageBlockInteraction;
	const FChunkWorldDamageBlockInteractionResult PreviousDamageInteractionResult = LastDamageBlockInteractionResult;
	RefreshDamageInteractionState(false);

	if (!bHadActiveDamageInteraction && bHasActiveDamageBlockInteraction)
	{
		OnDamageBlockInteractionStarted.Broadcast(LastDamageBlockInteractionResult);
	}
	else if (bHadActiveDamageInteraction
		&& bHasActiveDamageBlockInteraction
		&& PreviousDamageInteractionResult.ResolvedBlockHit.BlockWorldPos != LastDamageBlockInteractionResult.ResolvedBlockHit.BlockWorldPos)
	{
		OnDamageBlockInteractionEnded.Broadcast(PreviousDamageInteractionResult);
		OnDamageBlockInteractionStarted.Broadcast(LastDamageBlockInteractionResult);
	}
}

void UPorismDamageTraceInteractionComponent::HandleBlockInteractionEnded(const FChunkWorldBlockInteractionResult& Result)
{
	if (!bHasActiveDamageBlockInteraction)
	{
		return;
	}

	const FChunkWorldDamageBlockInteractionResult PreviousDamageInteractionResult = LastDamageBlockInteractionResult;
	LastDamageBlockInteractionResult = FChunkWorldDamageBlockInteractionResult();
	bHasActiveDamageBlockInteraction = false;
	OnDamageBlockInteractionEnded.Broadcast(PreviousDamageInteractionResult);
}

void UPorismDamageTraceInteractionComponent::HandleBlockInteractionUpdated(const FChunkWorldBlockInteractionResult& Result)
{
	const bool bHadActiveDamageInteraction = bHasActiveDamageBlockInteraction;
	const FChunkWorldDamageBlockInteractionResult PreviousDamageInteractionResult = LastDamageBlockInteractionResult;
	RefreshDamageInteractionState(false);

	if (!bHasActiveDamageBlockInteraction)
	{
		if (bHadActiveDamageInteraction)
		{
			OnDamageBlockInteractionEnded.Broadcast(PreviousDamageInteractionResult);
		}
		return;
	}

	if (!bHadActiveDamageInteraction)
	{
		OnDamageBlockInteractionStarted.Broadcast(LastDamageBlockInteractionResult);
		return;
	}

	if (PreviousDamageInteractionResult.ResolvedBlockHit.BlockWorldPos != LastDamageBlockInteractionResult.ResolvedBlockHit.BlockWorldPos)
	{
		OnDamageBlockInteractionEnded.Broadcast(PreviousDamageInteractionResult);
		OnDamageBlockInteractionStarted.Broadcast(LastDamageBlockInteractionResult);
		return;
	}

	OnDamageBlockInteractionUpdated.Broadcast(LastDamageBlockInteractionResult);
}

void UPorismDamageTraceInteractionComponent::HandleBlockCustomDataMaterialized(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshDamageInteractionState(false);
	if (bHasActiveDamageBlockInteraction)
	{
		OnDamageBlockCustomDataMaterialized.Broadcast(LastDamageBlockInteractionResult);
	}
}

void UPorismDamageTraceInteractionComponent::ServerApplyDamageToCurrentBlock_Implementation(const FHitResult& BlockHit, const FVector_NetQuantizeNormal& TraceDirection, int32 DamageAmount)
{
	if (DamageAmount <= 0)
	{
		return;
	}

	FChunkWorldResolvedBlockHit ResolvedHit;
	if (!UChunkWorldHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(BlockHit, TraceDirection, ResolvedHit) || !ResolvedHit.bHasBlock)
	{
		return;
	}

	FChunkWorldBlockDamageResult DamageResult;
	(void)UChunkWorldBlockDamageBlueprintLibrary::TryApplyBlockDamageForResolvedBlockHit(ResolvedHit, DamageAmount, DamageResult);
}

void UPorismDamageTraceInteractionComponent::RefreshDamageInteractionState(bool bBroadcastUpdate)
{
	const FChunkWorldBlockInteractionResult BlockInteractionResult = GetLastBlockInteractionResult();
	FChunkWorldDamageBlockInteractionResult NewDamageInteractionResult;
	const bool bHasNewDamageInteraction = TryBuildDamageBlockInteractionResult(BlockInteractionResult, NewDamageInteractionResult);
	if (!bHasNewDamageInteraction)
	{
		LastDamageBlockInteractionResult = FChunkWorldDamageBlockInteractionResult();
		bHasActiveDamageBlockInteraction = false;
		return;
	}

	LastDamageBlockInteractionResult = NewDamageInteractionResult;
	bHasActiveDamageBlockInteraction = true;

	if (bBroadcastUpdate)
	{
		OnDamageBlockInteractionUpdated.Broadcast(LastDamageBlockInteractionResult);
	}
}

bool UPorismDamageTraceInteractionComponent::TryBuildDamageBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult, FChunkWorldDamageBlockInteractionResult& OutResult) const
{
	OutResult = FChunkWorldDamageBlockInteractionResult();
	if (!BlockResult.bHasBlock || !BlockResult.ResolvedBlockHit.bHasBlock || !IsValid(BlockResult.ResolvedBlockHit.BlockTypeSchemaComponent))
	{
		return false;
	}

	static_cast<FChunkWorldBlockInteractionResult&>(OutResult) = BlockResult;

	int32 CurrentHealth = 0;
	bool bIsInvincible = false;
	bool bUsingPredictedHealth = false;
	FGameplayTag CurrentHealthBlockTypeName;
	if (UPorismPredictedBlockStateComponent* PredictedBlockStateComponent = GetPredictedBlockStateComponent())
	{
		if (!PredictedBlockStateComponent->TryGetCurrentHealthState(BlockResult.ResolvedBlockHit, CurrentHealth, bIsInvincible, bUsingPredictedHealth, CurrentHealthBlockTypeName))
		{
			return false;
		}
	}
	else
	{
		if (!UChunkWorldBlockDamageBlueprintLibrary::TryGetCurrentBlockHealthStateForResolvedBlockHit(BlockResult.ResolvedBlockHit, CurrentHealth, bIsInvincible, CurrentHealthBlockTypeName))
		{
			return false;
		}
	}

	FGameplayTag DefinitionBlockTypeName;
	FBlockDamageDefinition DamageDefinition;
	if (!BlockResult.ResolvedBlockHit.BlockTypeSchemaComponent->GetBlockDefinitionForBlockWorldPos(BlockResult.ResolvedBlockHit.BlockWorldPos, DefinitionBlockTypeName, DamageDefinition))
	{
		return false;
	}

	FGameplayTag CustomDataBlockTypeName;
	FInstancedStruct StoredCustomData;
	const bool bHasCustomData = BlockResult.ResolvedBlockHit.BlockTypeSchemaComponent->GetBlockCustomDataForBlockWorldPos(
		BlockResult.ResolvedBlockHit.BlockWorldPos,
		CustomDataBlockTypeName,
		StoredCustomData);

	OutResult.bSupportsHealth = true;
	OutResult.bUsingPredictedHealth = bUsingPredictedHealth;
	OutResult.bHasCustomData = bHasCustomData;
	OutResult.bIsDestructible = !bIsInvincible;
	OutResult.CurrentHealth = CurrentHealth;
	OutResult.MaxHealth = DamageDefinition.MaxHealth;
	return true;
}

UPorismPredictedBlockStateComponent* UPorismDamageTraceInteractionComponent::GetPredictedBlockStateComponent() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UPorismPredictedBlockStateComponent>() : nullptr;
}
