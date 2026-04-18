// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismHealthInteractionComponent.h"

#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
DEFINE_LOG_CATEGORY_STATIC(LogPorismHealthTraceInteraction, Log, All);

namespace
{
	void DrawHealthTraceInteractionDebugStatsLine(UCanvas* Canvas, float& InOutY, const FString& Message, const FColor& Color)
	{
		if (Canvas == nullptr)
		{
			return;
		}

		Canvas->SetDrawColor(Color);
		Canvas->DrawText(GEngine->GetSmallFont(), Message, 48.0f, InOutY);
		InOutY += 14.0f;
	}
}

UPorismHealthInteractionComponent::UPorismHealthInteractionComponent()
{
}

FChunkWorldHealthBlockInteractionResult UPorismHealthInteractionComponent::GetLastHealthBlockInteractionResult() const
{
	return FocusedHealthState.Payload;
}

void UPorismHealthInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		DebugDrawDelegateHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UPorismHealthInteractionComponent::DrawDebugStats));
	}

	OnBlockInteractionStarted.AddDynamic(this, &UPorismHealthInteractionComponent::HandleBlockInteractionStarted);
	OnBlockInteractionEnded.AddDynamic(this, &UPorismHealthInteractionComponent::HandleBlockInteractionEnded);
	OnBlockInteractionUpdated.AddDynamic(this, &UPorismHealthInteractionComponent::HandleBlockInteractionUpdated);
	OnBlockCustomDataInitialized.AddDynamic(this, &UPorismHealthInteractionComponent::HandleBlockCustomDataInitialized);
	BindPredictedBlockStateComponent();
	RefreshFocusedHealthState();
}

void UPorismHealthInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (DebugDrawDelegateHandle.IsValid())
	{
		UDebugDrawService::Unregister(DebugDrawDelegateHandle);
		DebugDrawDelegateHandle.Reset();
	}

	OnBlockInteractionStarted.RemoveAll(this);
	OnBlockInteractionEnded.RemoveAll(this);
	OnBlockInteractionUpdated.RemoveAll(this);
	OnBlockCustomDataInitialized.RemoveAll(this);
	UnbindPredictedBlockStateComponent();

	const FFocusedHealthBlockState PreviousState = FocusedHealthState;
	ResetFocusedHealthState();
	if (PreviousState.bIsActive)
	{
		OnHealthBlockInteractionEnded.Broadcast(PreviousState.Payload);
	}

	Super::EndPlay(EndPlayReason);
}

bool UPorismHealthInteractionComponent::ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const
{
	return Super::ShouldAcceptBlockInteractionResult(BlockResult);
}

bool UPorismHealthInteractionComponent::DidBlockInteractionResultChange(
	const bool bHadPreviousResult,
	const FChunkWorldBlockInteractionResult& PreviousResult,
	const bool bHasNewResult,
	const FChunkWorldBlockInteractionResult& NewResult) const
{
	return Super::DidBlockInteractionResultChange(bHadPreviousResult, PreviousResult, bHasNewResult, NewResult);
}

void UPorismHealthInteractionComponent::HandleBlockInteractionStarted(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedHealthState();
}

void UPorismHealthInteractionComponent::HandleBlockInteractionEnded(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedHealthState();
}

void UPorismHealthInteractionComponent::HandleBlockInteractionUpdated(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedHealthState();
}

void UPorismHealthInteractionComponent::HandleBlockCustomDataInitialized(const FChunkWorldBlockInteractionResult& Result)
{
	RefreshFocusedHealthState();
}

void UPorismHealthInteractionComponent::HandleTrackedBlockStateChanged(AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos)
{
	const FChunkWorldBlockInteractionResult CurrentBlockResult = GetLastBlockInteractionResult();
	const bool bHasActiveInteraction = HasActiveBlockInteraction();
	const bool bMatchesFocusedHealthState = IsSameFocusedBlock(FocusedHealthState, ChunkWorld, BlockWorldPos);
	const bool bAccepted = (bHasActiveInteraction
		&& CurrentBlockResult.bHasBlock
		&& CurrentBlockResult.ResolvedBlockHit.ChunkWorld == ChunkWorld
		&& CurrentBlockResult.ResolvedBlockHit.BlockWorldPos == BlockWorldPos)
		|| bMatchesFocusedHealthState;

	if (!bAccepted)
	{
		return;
	}

	RefreshFocusedHealthState();
}

void UPorismHealthInteractionComponent::RefreshFocusedHealthState()
{
	const FFocusedHealthBlockState PreviousState = FocusedHealthState;
	FFocusedHealthBlockState NewState;

	const FChunkWorldBlockInteractionResult BlockResult = GetLastBlockInteractionResult();
	FChunkWorldHealthBlockInteractionResult HealthPayload;
	const bool bBuiltHealthPayload = HasActiveBlockInteraction() && TryBuildHealthBlockInteractionResult(BlockResult, HealthPayload);
	if (bBuiltHealthPayload)
	{
		NewState.bIsActive = true;
		NewState.ChunkWorld = HealthPayload.ResolvedBlockHit.ChunkWorld;
		NewState.BlockWorldPos = HealthPayload.ResolvedBlockHit.BlockWorldPos;
		NewState.Payload = HealthPayload;
		NewState.bHasAnnouncedInitialized = PreviousState.bIsActive
			&& IsSameFocusedBlock(PreviousState, HealthPayload.ResolvedBlockHit.ChunkWorld, HealthPayload.ResolvedBlockHit.BlockWorldPos)
			&& PreviousState.bHasAnnouncedInitialized;
	}

	EmitHealthStateTransition(PreviousState, NewState);
	FocusedHealthState = NewState;
}

void UPorismHealthInteractionComponent::ResetFocusedHealthState()
{
	FocusedHealthState = FFocusedHealthBlockState();
}

bool UPorismHealthInteractionComponent::IsSameFocusedBlock(
	const FFocusedHealthBlockState& State,
	AChunkWorld* ChunkWorld,
	const FIntVector& BlockWorldPos) const
{
	return State.bIsActive && State.ChunkWorld.Get() == ChunkWorld && State.BlockWorldPos == BlockWorldPos;
}

bool UPorismHealthInteractionComponent::HasInitializedDisplayData(const FChunkWorldHealthBlockInteractionResult& Result) const
{
	return Result.bHasCustomData;
}

bool UPorismHealthInteractionComponent::DidHealthPayloadChange(
	const FChunkWorldHealthBlockInteractionResult& PreviousResult,
	const FChunkWorldHealthBlockInteractionResult& NewResult) const
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

void UPorismHealthInteractionComponent::EmitHealthStateTransition(
	const FFocusedHealthBlockState& PreviousState,
	FFocusedHealthBlockState& NewState)
{
	const bool bHadPrevious = PreviousState.bIsActive;
	const bool bHasNew = NewState.bIsActive;
	const bool bSameBlock = bHadPrevious
		&& bHasNew
		&& PreviousState.ChunkWorld == NewState.ChunkWorld
		&& PreviousState.BlockWorldPos == NewState.BlockWorldPos;

	if (bHadPrevious && (!bHasNew || !bSameBlock))
	{
		OnHealthBlockInteractionEnded.Broadcast(PreviousState.Payload);
	}

	if (!bHasNew)
	{
		return;
	}

	if (!bHadPrevious || !bSameBlock)
	{
		OnHealthBlockInteractionStarted.Broadcast(NewState.Payload);
		if (!NewState.bHasAnnouncedInitialized && HasInitializedDisplayData(NewState.Payload))
		{
			OnHealthBlockCustomDataInitialized.Broadcast(NewState.Payload);
			NewState.bHasAnnouncedInitialized = true;
		}
		return;
	}

	if (DidHealthPayloadChange(PreviousState.Payload, NewState.Payload))
	{
		OnHealthBlockInteractionUpdated.Broadcast(NewState.Payload);
	}

	if (!PreviousState.bHasAnnouncedInitialized && HasInitializedDisplayData(NewState.Payload))
	{
		OnHealthBlockCustomDataInitialized.Broadcast(NewState.Payload);
		NewState.bHasAnnouncedInitialized = true;
	}
}

void UPorismHealthInteractionComponent::BindPredictedBlockStateComponent()
{
	if (UPorismPredictedBlockStateComponent* PredictionComponent = GetPredictedBlockStateComponent())
	{
		PredictionComponent->OnTrackedBlockStateChanged().AddUObject(this, &UPorismHealthInteractionComponent::HandleTrackedBlockStateChanged);
	}
}

void UPorismHealthInteractionComponent::UnbindPredictedBlockStateComponent()
{
	if (UPorismPredictedBlockStateComponent* PredictionComponent = PredictedBlockStateComponent.Get())
	{
		PredictionComponent->OnTrackedBlockStateChanged().RemoveAll(this);
	}

	PredictedBlockStateComponent.Reset();
}

bool UPorismHealthInteractionComponent::ShouldDrawDebugStatsForPlayer(const APlayerController* DebugOwner) const
{
	if (!bShowDebugStats || DebugOwner == nullptr || GetOwner() == nullptr)
	{
		return false;
	}

	const APawn* ControlledPawn = DebugOwner->GetPawn();
	return ControlledPawn != nullptr && (ControlledPawn == GetOwner() || ControlledPawn == GetOwner()->GetOwner());
}

void UPorismHealthInteractionComponent::MaybeLogDebugStats(const FString& Snapshot)
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
	UE_LOG(LogPorismHealthTraceInteraction, Log, TEXT("%s"), *Snapshot);
}

void UPorismHealthInteractionComponent::DrawDebugStats(UCanvas* Canvas, APlayerController* DebugOwner)
{
	if (!ShouldDrawDebugStatsForPlayer(DebugOwner) || Canvas == nullptr)
	{
		return;
	}

	const FPorismTraceInteractionResult& TraceResult = GetLastTraceResult();
	const FChunkWorldBlockInteractionResult BlockResult = GetLastBlockInteractionResult();
	const FChunkWorldHealthBlockInteractionResult HealthResult = GetLastHealthBlockInteractionResult();
	FString Snapshot = FString::Printf(
		TEXT("Porism Health Trace Interaction [%s] Owner=%s TargetType=%d HasTarget=%d HasBlock=%d HasHealthBlock=%d BoundPrediction=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		static_cast<int32>(TraceResult.TargetType),
		HasValidInteractionTarget(),
		HasActiveBlockInteraction(),
		HasActiveHealthBlockInteraction(),
		GetPredictedBlockStateComponent() != nullptr);

	float Y = 164.0f;
	DrawHealthTraceInteractionDebugStatsLine(Canvas, Y, FString::Printf(TEXT("Porism Health Trace Interaction [%s]"), *GetNameSafe(this)), FColor::Cyan);
	DrawHealthTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("Owner=%s TargetType=%d HasTarget=%d HasBlock=%d HasHealthBlock=%d BoundPrediction=%d"),
			*GetNameSafe(GetOwner()),
			static_cast<int32>(TraceResult.TargetType),
			HasValidInteractionTarget(),
			HasActiveBlockInteraction(),
			HasActiveHealthBlockInteraction(),
			GetPredictedBlockStateComponent() != nullptr),
		FColor::White);

	if (!HasActiveHealthBlockInteraction())
	{
		DrawHealthTraceInteractionDebugStatsLine(Canvas, Y, TEXT("No active health-aware block interaction."), FColor::Silver);
		Snapshot += TEXT(" NoActiveHealthBlockInteraction");
		MaybeLogDebugStats(Snapshot);
		return;
	}

	DrawHealthTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("Block=%s Type=%s ResolveSource=%d"),
			*HealthResult.ResolvedBlockHit.BlockWorldPos.ToString(),
			*HealthResult.BlockTypeName.ToString(),
			static_cast<int32>(HealthResult.ResolvedBlockHit.ResolveSource)),
		FColor::White);
	DrawHealthTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("Health=%d/%d UsingPredictedHealth=%d HasAuthoritativeHealth=%d HasCustomData=%d Destructible=%d Invincible=%d"),
			HealthResult.CurrentHealth,
			HealthResult.MaxHealth,
			HealthResult.bUsingPredictedHealth,
			HealthResult.bHasAuthoritativeHealth,
			HealthResult.bHasCustomData,
			HealthResult.bIsDestructible,
			HealthResult.bIsInvincible),
		FColor::White);
	DrawHealthTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("HitActor=%s HitComponent=%s ImpactPoint=%s"),
				*GetNameSafe(BlockResult.Hit.GetActor()),
				*GetNameSafe(BlockResult.Hit.GetComponent()),
				*BlockResult.Hit.ImpactPoint.ToString()),
		FColor::White);
	Snapshot += FString::Printf(
		TEXT(" Block=%s Type=%s ResolveSource=%d Health=%d/%d UsingPredictedHealth=%d HasAuthoritativeHealth=%d HasCustomData=%d Destructible=%d Invincible=%d HitActor=%s HitComponent=%s"),
		*HealthResult.ResolvedBlockHit.BlockWorldPos.ToString(),
		*HealthResult.BlockTypeName.ToString(),
		static_cast<int32>(HealthResult.ResolvedBlockHit.ResolveSource),
		HealthResult.CurrentHealth,
		HealthResult.MaxHealth,
		HealthResult.bUsingPredictedHealth,
		HealthResult.bHasAuthoritativeHealth,
		HealthResult.bHasCustomData,
		HealthResult.bIsDestructible,
		HealthResult.bIsInvincible,
		*GetNameSafe(BlockResult.Hit.GetActor()),
		*GetNameSafe(BlockResult.Hit.GetComponent()));
	MaybeLogDebugStats(Snapshot);
}

bool UPorismHealthInteractionComponent::TryBuildHealthBlockInteractionResult(
	const FChunkWorldBlockInteractionResult& BlockResult,
	FChunkWorldHealthBlockInteractionResult& OutResult) const
{
	OutResult = FChunkWorldHealthBlockInteractionResult();
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

	// Hide blocks that have already settled to authoritative zero health so interaction/highlight UI does not
	// keep targeting stale destruction windows while representation removal catches up on clients.
	if (!OutResult.bUsingPredictedHealth && OutResult.bHasAuthoritativeHealth && OutResult.CurrentHealth <= 0)
	{
		OutResult = FChunkWorldHealthBlockInteractionResult();
		return false;
	}

	return true;
}

UPorismPredictedBlockStateComponent* UPorismHealthInteractionComponent::GetPredictedBlockStateComponent() const
{
	if (UPorismPredictedBlockStateComponent* CachedPredictedBlockStateComponent = PredictedBlockStateComponent.Get())
	{
		return CachedPredictedBlockStateComponent;
	}

	UPorismHealthInteractionComponent* MutableThis = const_cast<UPorismHealthInteractionComponent*>(this);
	MutableThis->PredictedBlockStateComponent = GetOwner() ? GetOwner()->FindComponentByClass<UPorismPredictedBlockStateComponent>() : nullptr;
	return MutableThis->PredictedBlockStateComponent.Get();
}
