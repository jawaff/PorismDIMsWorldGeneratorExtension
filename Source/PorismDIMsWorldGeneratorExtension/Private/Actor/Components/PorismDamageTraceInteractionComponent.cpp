// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismDamageTraceInteractionComponent.h"

#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "Block/BlockTypeSchemaRegistry.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockDamageBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogPorismDamageTraceInteraction, Log, All);

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

bool UPorismDamageTraceInteractionComponent::ApplyDamageToCurrentBlock(int32 DamageAmount)
{
	if (DamageAmount <= 0 || !CanApplyDamageToCurrentBlock())
	{
		return false;
	}

	const FChunkWorldDamageBlockInteractionResult DamageInteractionResult = LastDamageBlockInteractionResult;
	if (GetOwner() == nullptr)
	{
		return false;
	}

	if (UPorismPredictedBlockStateComponent* CachedPredictedBlockStateComponent = GetPredictedBlockStateComponent())
	{
		return CachedPredictedBlockStateComponent->ApplyPredictedDamageAndQueueAuthoritativeFlush(DamageInteractionResult.ResolvedBlockHit, DamageAmount);
	}

	UE_LOG(
		LogPorismDamageTraceInteraction,
		Error,
		TEXT("Damage trace interaction '%s' attempted to apply block damage without a UPorismPredictedBlockStateComponent on owner '%s'."),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()));

	return false;
}

void UPorismDamageTraceInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	OnBlockInteractionStarted.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionStarted);
	OnBlockInteractionEnded.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionEnded);
	OnBlockInteractionUpdated.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionUpdated);
	OnBlockCustomDataMaterialized.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockCustomDataMaterialized);
	BindPredictedBlockStateComponent();

	RefreshDamageInteractionState(false);
}

void UPorismDamageTraceInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnBlockInteractionStarted.RemoveAll(this);
	OnBlockInteractionEnded.RemoveAll(this);
	OnBlockInteractionUpdated.RemoveAll(this);
	OnBlockCustomDataMaterialized.RemoveAll(this);
	UnbindPredictedBlockStateComponent();

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

void UPorismDamageTraceInteractionComponent::HandleAuthoritativeBlockCustomDataUpdated(const FIntVector& BlockWorldPos)
{
	if (!bHasActiveDamageBlockInteraction || LastDamageBlockInteractionResult.ResolvedBlockHit.BlockWorldPos != BlockWorldPos)
	{
		return;
	}

	const bool bHadActiveDamageInteraction = bHasActiveDamageBlockInteraction;
	const FChunkWorldDamageBlockInteractionResult PreviousDamageInteractionResult = LastDamageBlockInteractionResult;
	const bool bPreviouslyHadCustomData = PreviousDamageInteractionResult.bHasCustomData;
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
	if (bHasActiveDamageBlockInteraction && !bPreviouslyHadCustomData && LastDamageBlockInteractionResult.bHasCustomData)
	{
		OnDamageBlockCustomDataMaterialized.Broadcast(LastDamageBlockInteractionResult);
	}
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

void UPorismDamageTraceInteractionComponent::BindPredictedBlockStateComponent()
{
	if (UPorismPredictedBlockStateComponent* CachedPredictedBlockStateComponent = GetPredictedBlockStateComponent())
	{
		CachedPredictedBlockStateComponent->OnTrackedBlockStateChanged().AddUObject(this, &UPorismDamageTraceInteractionComponent::HandleTrackedBlockStateChanged);
	}
}

void UPorismDamageTraceInteractionComponent::UnbindPredictedBlockStateComponent()
{
	if (UPorismPredictedBlockStateComponent* CachedPredictedBlockStateComponent = PredictedBlockStateComponent.Get())
	{
		CachedPredictedBlockStateComponent->OnTrackedBlockStateChanged().RemoveAll(this);
	}

	PredictedBlockStateComponent.Reset();
}

void UPorismDamageTraceInteractionComponent::HandleTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
{
	if (!bHasActiveDamageBlockInteraction
		|| LastDamageBlockInteractionResult.ResolvedBlockHit.ChunkWorld != ChunkWorld
		|| LastDamageBlockInteractionResult.ResolvedBlockHit.BlockWorldPos != BlockWorldPos)
	{
		return;
	}

	HandleAuthoritativeBlockCustomDataUpdated(BlockWorldPos);
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
	if (UPorismPredictedBlockStateComponent* CachedPredictedBlockStateComponent = GetPredictedBlockStateComponent())
	{
		if (!CachedPredictedBlockStateComponent->TryGetCurrentHealthState(BlockResult.ResolvedBlockHit, CurrentHealth, bIsInvincible, bUsingPredictedHealth, CurrentHealthBlockTypeName))
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
	if (UPorismPredictedBlockStateComponent* CachedPredictedBlockStateComponent = PredictedBlockStateComponent.Get())
	{
		return CachedPredictedBlockStateComponent;
	}

	UPorismDamageTraceInteractionComponent* MutableThis = const_cast<UPorismDamageTraceInteractionComponent*>(this);
	MutableThis->PredictedBlockStateComponent = GetOwner() ? GetOwner()->FindComponentByClass<UPorismPredictedBlockStateComponent>() : nullptr;
	return MutableThis->PredictedBlockStateComponent.Get();
}
