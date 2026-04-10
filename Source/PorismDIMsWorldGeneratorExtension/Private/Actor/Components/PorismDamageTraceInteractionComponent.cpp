// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismDamageTraceInteractionComponent.h"

#include "Actor/Components/PorismPredictedBlockStateComponent.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "Debug/DebugDrawService.h"
#include "Engine/Canvas.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
DEFINE_LOG_CATEGORY_STATIC(LogPorismDamageTraceInteraction, Log, All);

namespace
{
	void DrawDamageTraceInteractionDebugStatsLine(UCanvas* Canvas, float& InOutY, const FString& Message, const FColor& Color)
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

UPorismDamageTraceInteractionComponent::UPorismDamageTraceInteractionComponent()
{
}

FChunkWorldDamageBlockInteractionResult UPorismDamageTraceInteractionComponent::GetLastDamageBlockInteractionResult() const
{
	return FocusedDamageState.Payload;
}

void UPorismDamageTraceInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetWorld() != nullptr && GetWorld()->GetNetMode() != NM_DedicatedServer)
	{
		DebugDrawDelegateHandle = UDebugDrawService::Register(TEXT("Game"), FDebugDrawDelegate::CreateUObject(this, &UPorismDamageTraceInteractionComponent::DrawDebugStats));
	}

	OnBlockInteractionStarted.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionStarted);
	OnBlockInteractionEnded.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionEnded);
	OnBlockInteractionUpdated.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockInteractionUpdated);
	OnBlockCustomDataInitialized.AddDynamic(this, &UPorismDamageTraceInteractionComponent::HandleBlockCustomDataInitialized);
	BindPredictedBlockStateComponent();
	RefreshFocusedDamageState();
}

void UPorismDamageTraceInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
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

bool UPorismDamageTraceInteractionComponent::ShouldDrawDebugStatsForPlayer(const APlayerController* DebugOwner) const
{
	if (!bShowDebugStats || DebugOwner == nullptr || GetOwner() == nullptr)
	{
		return false;
	}

	const APawn* ControlledPawn = DebugOwner->GetPawn();
	return ControlledPawn != nullptr && (ControlledPawn == GetOwner() || ControlledPawn == GetOwner()->GetOwner());
}

void UPorismDamageTraceInteractionComponent::MaybeLogDebugStats(const FString& Snapshot)
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
	UE_LOG(LogPorismDamageTraceInteraction, Log, TEXT("%s"), *Snapshot);
}

void UPorismDamageTraceInteractionComponent::DrawDebugStats(UCanvas* Canvas, APlayerController* DebugOwner)
{
	if (!ShouldDrawDebugStatsForPlayer(DebugOwner) || Canvas == nullptr)
	{
		return;
	}

	const FPorismTraceInteractionResult& TraceResult = GetLastTraceResult();
	const FChunkWorldBlockInteractionResult BlockResult = GetLastBlockInteractionResult();
	const FChunkWorldDamageBlockInteractionResult DamageResult = GetLastDamageBlockInteractionResult();
	FString Snapshot = FString::Printf(
		TEXT("Porism Damage Trace Interaction [%s] Owner=%s TargetType=%d HasTarget=%d HasBlock=%d HasDamageBlock=%d BoundPrediction=%d"),
		*GetNameSafe(this),
		*GetNameSafe(GetOwner()),
		static_cast<int32>(TraceResult.TargetType),
		HasValidInteractionTarget(),
		HasActiveBlockInteraction(),
		HasActiveDamageBlockInteraction(),
		GetPredictedBlockStateComponent() != nullptr);

	float Y = 164.0f;
	DrawDamageTraceInteractionDebugStatsLine(Canvas, Y, FString::Printf(TEXT("Porism Damage Trace Interaction [%s]"), *GetNameSafe(this)), FColor::Cyan);
	DrawDamageTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("Owner=%s TargetType=%d HasTarget=%d HasBlock=%d HasDamageBlock=%d BoundPrediction=%d"),
			*GetNameSafe(GetOwner()),
			static_cast<int32>(TraceResult.TargetType),
			HasValidInteractionTarget(),
				HasActiveBlockInteraction(),
				HasActiveDamageBlockInteraction(),
			GetPredictedBlockStateComponent() != nullptr),
		FColor::White);

	if (!HasActiveDamageBlockInteraction())
	{
		DrawDamageTraceInteractionDebugStatsLine(Canvas, Y, TEXT("No active damage-capable block interaction."), FColor::Silver);
		Snapshot += TEXT(" NoActiveDamageBlockInteraction");
		MaybeLogDebugStats(Snapshot);
		return;
	}

	DrawDamageTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("Block=%s Type=%s ResolveSource=%d"),
			*DamageResult.ResolvedBlockHit.BlockWorldPos.ToString(),
			*DamageResult.BlockTypeName.ToString(),
			static_cast<int32>(DamageResult.ResolvedBlockHit.ResolveSource)),
		FColor::White);
	DrawDamageTraceInteractionDebugStatsLine(
		Canvas,
		Y,
		FString::Printf(
			TEXT("Health=%d/%d UsingPredictedHealth=%d HasAuthoritativeHealth=%d HasCustomData=%d Destructible=%d Invincible=%d"),
			DamageResult.CurrentHealth,
			DamageResult.MaxHealth,
			DamageResult.bUsingPredictedHealth,
			DamageResult.bHasAuthoritativeHealth,
			DamageResult.bHasCustomData,
			DamageResult.bIsDestructible,
			DamageResult.bIsInvincible),
		FColor::White);
	DrawDamageTraceInteractionDebugStatsLine(
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
		*DamageResult.ResolvedBlockHit.BlockWorldPos.ToString(),
		*DamageResult.BlockTypeName.ToString(),
		static_cast<int32>(DamageResult.ResolvedBlockHit.ResolveSource),
		DamageResult.CurrentHealth,
		DamageResult.MaxHealth,
		DamageResult.bUsingPredictedHealth,
		DamageResult.bHasAuthoritativeHealth,
		DamageResult.bHasCustomData,
		DamageResult.bIsDestructible,
		DamageResult.bIsInvincible,
		*GetNameSafe(BlockResult.Hit.GetActor()),
		*GetNameSafe(BlockResult.Hit.GetComponent()));
	MaybeLogDebugStats(Snapshot);
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
