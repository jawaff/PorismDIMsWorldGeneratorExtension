// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismDamageTraceInteractionComponent.h"

#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
DEFINE_LOG_CATEGORY_STATIC(LogPorismDamageTraceInteraction, Log, All);

UPorismDamageTraceInteractionComponent::UPorismDamageTraceInteractionComponent()
{
}

FChunkWorldDamageBlockInteractionResult UPorismDamageTraceInteractionComponent::GetLastDamageBlockInteractionResult() const
{
	return FocusedDamageState.Payload;
}

bool UPorismDamageTraceInteractionComponent::CanApplyDamageToCurrentBlock() const
{
	return FocusedDamageState.bIsActive
		&& FocusedDamageState.Payload.bSupportsHealth
		&& FocusedDamageState.Payload.bIsDestructible;
}

bool UPorismDamageTraceInteractionComponent::ApplyDamageToCurrentBlock(const int32 DamageAmount)
{
	if (DamageAmount <= 0 || !CanApplyDamageToCurrentBlock())
	{
		return false;
	}

	if (GetOwner() == nullptr)
	{
		return false;
	}

	if (UPorismPredictedBlockStateComponent* PredictionComponent = GetPredictedBlockStateComponent())
	{
		FChunkWorldBlockDamageRequest DamageRequest;
		DamageRequest.ResolvedHit = FocusedDamageState.Payload.ResolvedBlockHit;
		DamageRequest.DamageAmount = DamageAmount;
		DamageRequest.RequestContextTag = TEXT("TraceInteraction");
		DamageRequest.bAllowImmediateLocalFeedback = true;

		FChunkWorldBlockDamageRequestResult RequestResult;
		return PredictionComponent->ApplyBlockDamageRequest(DamageRequest, RequestResult);
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
	OnBlockCustomDataInitialized.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockCustomDataInitialized);
	BindPredictedBlockStateComponent();
	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	OnBlockInteractionStarted.RemoveAll(this);
	OnBlockInteractionEnded.RemoveAll(this);
	OnBlockInteractionUpdated.RemoveAll(this);
	OnBlockCustomDataInitialized.RemoveAll(this);
	UnbindPredictedBlockStateComponent();

	const FFocusedDamageBlockState PreviousState = FocusedDamageState;
	ResetFocusedDamageState();
	if (PreviousState.bIsActive)
	{
		OnDamageBlockInteractionEnded.Broadcast(PreviousState.Payload);
	}

	Super::EndPlay(EndPlayReason);
}

bool UPorismDamageTraceInteractionComponent::ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const
{
	return Super::ShouldAcceptBlockInteractionResult(BlockResult);
}

bool UPorismDamageTraceInteractionComponent::DidBlockInteractionResultChange(
	const bool bHadPreviousResult,
	const FChunkWorldBlockInteractionResult& PreviousResult,
	const bool bHasNewResult,
	const FChunkWorldBlockInteractionResult& NewResult) const
{
	return Super::DidBlockInteractionResultChange(bHadPreviousResult, PreviousResult, bHasNewResult, NewResult);
}

void UPorismDamageTraceInteractionComponent::HandleBlockInteractionStarted(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::HandleBlockInteractionEnded(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::HandleBlockInteractionUpdated(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::HandleBlockCustomDataInitialized(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::HandleTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
{
	const FChunkWorldBlockInteractionResult CurrentBlockResult = GetLastBlockInteractionResult();
	const bool bHasActiveInteraction = HasActiveBlockInteraction();
	const bool bMatchesFocusedDamageState = IsSameFocusedBlock(FocusedDamageState, ChunkWorld, BlockWorldPos);
	const bool bAccepted = (bHasActiveInteraction
		&& CurrentBlockResult.bHasBlock
		&& CurrentBlockResult.ResolvedBlockHit.ChunkWorld == ChunkWorld
		&& CurrentBlockResult.ResolvedBlockHit.BlockWorldPos == BlockWorldPos)
		|| bMatchesFocusedDamageState;

	if (!bAccepted)
	{
		return;
	}

	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::RefreshFocusedDamageState()
{
	const FFocusedDamageBlockState PreviousState = FocusedDamageState;
	FFocusedDamageBlockState NewState;

	const FChunkWorldBlockInteractionResult BlockResult = GetLastBlockInteractionResult();
	FChunkWorldDamageBlockInteractionResult DamagePayload;
	const bool bBuiltDamagePayload = HasActiveBlockInteraction() && TryBuildDamageBlockInteractionResult(BlockResult, DamagePayload);
	if (bBuiltDamagePayload)
	{
		NewState.bIsActive = true;
		NewState.ChunkWorld = DamagePayload.ResolvedBlockHit.ChunkWorld;
		NewState.BlockWorldPos = DamagePayload.ResolvedBlockHit.BlockWorldPos;
		NewState.Payload = DamagePayload;
		NewState.bHasAnnouncedInitialized = PreviousState.bIsActive
			&& IsSameFocusedBlock(PreviousState, DamagePayload.ResolvedBlockHit.ChunkWorld, DamagePayload.ResolvedBlockHit.BlockWorldPos)
			&& PreviousState.bHasAnnouncedInitialized;
	}

	EmitDamageStateTransition(PreviousState, NewState);
	FocusedDamageState = NewState;
}

void UPorismDamageTraceInteractionComponent::ResetFocusedDamageState()
{
	FocusedDamageState = FFocusedDamageBlockState();
}

bool UPorismDamageTraceInteractionComponent::IsSameFocusedBlock(
	const FFocusedDamageBlockState& State,
	AChunkWorld* ChunkWorld,
	const FIntVector& BlockWorldPos) const
{
	return State.bIsActive && State.ChunkWorld.Get() == ChunkWorld && State.BlockWorldPos == BlockWorldPos;
}

bool UPorismDamageTraceInteractionComponent::HasInitializedDisplayData(const FChunkWorldDamageBlockInteractionResult& Result) const
{
	return Result.bHasCustomData;
}

bool UPorismDamageTraceInteractionComponent::DidDamagePayloadChange(
	const FChunkWorldDamageBlockInteractionResult& PreviousResult,
	const FChunkWorldDamageBlockInteractionResult& NewResult) const
{
	return PreviousResult.ResolvedBlockHit.ChunkWorld != NewResult.ResolvedBlockHit.ChunkWorld
		|| PreviousResult.ResolvedBlockHit.BlockWorldPos != NewResult.ResolvedBlockHit.BlockWorldPos
		|| PreviousResult.CurrentHealth != NewResult.CurrentHealth
		|| PreviousResult.MaxHealth != NewResult.MaxHealth
		|| PreviousResult.bHasCustomData != NewResult.bHasCustomData
		|| PreviousResult.bHasAuthoritativeHealth != NewResult.bHasAuthoritativeHealth
		|| PreviousResult.bUsingPredictedHealth != NewResult.bUsingPredictedHealth
		|| PreviousResult.bIsDestructible != NewResult.bIsDestructible
		|| PreviousResult.bIsInvincible != NewResult.bIsInvincible
		|| PreviousResult.BlockTypeName != NewResult.BlockTypeName;
}

void UPorismDamageTraceInteractionComponent::EmitDamageStateTransition(
	const FFocusedDamageBlockState& PreviousState,
	FFocusedDamageBlockState& NewState)
{
	const bool bHadPrevious = PreviousState.bIsActive;
	const bool bHasNew = NewState.bIsActive;
	const bool bSameBlock = bHadPrevious
		&& bHasNew
		&& PreviousState.ChunkWorld == NewState.ChunkWorld
		&& PreviousState.BlockWorldPos == NewState.BlockWorldPos;

	if (bHadPrevious && (!bHasNew || !bSameBlock))
	{
		OnDamageBlockInteractionEnded.Broadcast(PreviousState.Payload);
	}

	if (!bHasNew)
	{
		return;
	}

	if (!bHadPrevious || !bSameBlock)
	{
		OnDamageBlockInteractionStarted.Broadcast(NewState.Payload);
		if (!NewState.bHasAnnouncedInitialized && HasInitializedDisplayData(NewState.Payload))
		{
			OnDamageBlockCustomDataInitialized.Broadcast(NewState.Payload);
			NewState.bHasAnnouncedInitialized = true;
		}
		return;
	}

	if (DidDamagePayloadChange(PreviousState.Payload, NewState.Payload))
	{
		OnDamageBlockInteractionUpdated.Broadcast(NewState.Payload);
	}

	if (!PreviousState.bHasAnnouncedInitialized && HasInitializedDisplayData(NewState.Payload))
	{
		OnDamageBlockCustomDataInitialized.Broadcast(NewState.Payload);
		NewState.bHasAnnouncedInitialized = true;
	}
}

void UPorismDamageTraceInteractionComponent::BindPredictedBlockStateComponent()
{
	if (UPorismPredictedBlockStateComponent* PredictionComponent = GetPredictedBlockStateComponent())
	{
		PredictionComponent->OnTrackedBlockStateChanged().AddUObject(this, &UPorismDamageTraceInteractionComponent::HandleTrackedBlockStateChanged);
	}
}

void UPorismDamageTraceInteractionComponent::UnbindPredictedBlockStateComponent()
{
	if (UPorismPredictedBlockStateComponent* PredictionComponent = PredictedBlockStateComponent.Get())
	{
		PredictionComponent->OnTrackedBlockStateChanged().RemoveAll(this);
	}

	PredictedBlockStateComponent.Reset();
}

bool UPorismDamageTraceInteractionComponent::TryBuildDamageBlockInteractionResult(
	const FChunkWorldBlockInteractionResult& BlockResult,
	FChunkWorldDamageBlockInteractionResult& OutResult) const
{
	OutResult = FChunkWorldDamageBlockInteractionResult();
	if (!BlockResult.bHasBlock || !BlockResult.ResolvedBlockHit.bHasBlock || !IsValid(BlockResult.ResolvedBlockHit.BlockTypeSchemaComponent.Get()))
	{
		return false;
	}

	static_cast<FChunkWorldBlockInteractionResult&>(OutResult) = BlockResult;

	FPorismBlockHealthState HealthState;
	UPorismPredictedBlockStateComponent* PredictionComponent = GetPredictedBlockStateComponent();
	if (PredictionComponent == nullptr || !PredictionComponent->TryGetHealthState(BlockResult.ResolvedBlockHit, HealthState))
	{
		return false;
	}
	OutResult.BlockTypeName = HealthState.BlockTypeName;
	OutResult.bSupportsHealth = true;
	OutResult.bUsingPredictedHealth = HealthState.bUsingPredictedHealth;
	OutResult.bHasCustomData = HealthState.bHasCustomData;
	OutResult.bHasAuthoritativeHealth = HealthState.bHasAuthoritativeHealth;
	OutResult.bIsInvincible = HealthState.bIsInvincible;
	OutResult.bIsDestructible = !HealthState.bIsInvincible;
	OutResult.CurrentHealth = HealthState.CurrentHealth;
	OutResult.MaxHealth = HealthState.MaxHealth;
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
