// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismTraceInteractionComponent.h"

#include "Actor/Interaction/PorismInteractableInterface.h"
#include "Actor/Interaction/PorismInteractionTraceViewProviderInterface.h"
#include "ChunkWorld/ChunkWorld.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "ChunkWorld/Components/BlockTypeSchemaComponent.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "PorismDIMsWorldGeneratorExtension.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPorismTraceInteraction, Log, All);

UPorismTraceInteractionComponent::UPorismTraceInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	InteractableInterfaces.AddUnique(UPorismInteractableInterface::StaticClass());

	TraceChannel = ECC_Visibility;
	BlockTraceChannel = ECC_Visibility;
	RefreshActiveTraceDistance();
}

FChunkWorldBlockInteractionResult UPorismTraceInteractionComponent::GetLastBlockInteractionResult() const
{
	return LastBlockInteractionResult;
}

void UPorismTraceInteractionComponent::BeginPlay()
{
	Super::BeginPlay();

	RefreshActiveTraceDistance();
	GetWorld()->GetTimerManager().SetTimer(TraceHandle, this, &UPorismTraceInteractionComponent::PerformTrace, TraceInterval, true);
}

void UPorismTraceInteractionComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (TraceHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(TraceHandle);
	}

	bHasLastBlockDebugDraw = false;
	LastBlockDebugDrawTimeSeconds = 0.0;
	ResetTrackedBlockCustomDataInitialization();

	if (LastTraceResult.TargetType != EPorismTraceInteractionTargetType::None)
	{
		const FPorismTraceInteractionResult PreviousResult = LastTraceResult;
		const FChunkWorldBlockInteractionResult PreviousBlockResult = LastBlockInteractionResult;
		const bool bHadPreviousBlockResult = bHasActiveBlockInteraction;
		LastTraceResult = FPorismTraceInteractionResult();
		bHasActiveBlockInteraction = false;
		BroadcastInteractionTransition(PreviousResult, LastTraceResult, bHadPreviousBlockResult, PreviousBlockResult, false, LastBlockInteractionResult);
		OnTraceInteractionUpdated.Broadcast(LastTraceResult);
	}

	Super::EndPlay(EndPlayReason);
}

bool UPorismTraceInteractionComponent::GetInteractionTraceView(FVector& OutLocation, FRotator& OutRotation) const
{
	const AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return false;
	}

	if (Owner->GetClass()->ImplementsInterface(UPorismInteractionTraceViewProviderInterface::StaticClass()))
	{
		return IPorismInteractionTraceViewProviderInterface::Execute_ProvideInteractionTraceView(const_cast<AActor*>(Owner), OutLocation, OutRotation);
	}

	if (const APawn* PawnOwner = Cast<APawn>(Owner))
	{
		if (const AController* OwnerController = PawnOwner->GetController())
		{
			OwnerController->GetPlayerViewPoint(OutLocation, OutRotation);
			return true;
		}
	}

	Owner->GetActorEyesViewPoint(OutLocation, OutRotation);
	return true;
}

void UPorismTraceInteractionComponent::ForceTrace()
{
	PerformTrace();
}

void UPorismTraceInteractionComponent::SetTraceChannels(TEnumAsByte<ECollisionChannel> NewTraceChannel, TEnumAsByte<ECollisionChannel> NewBlockTraceChannel)
{
	TraceChannel = NewTraceChannel;
	BlockTraceChannel = NewBlockTraceChannel;
}

bool UPorismTraceInteractionComponent::ShouldAcceptBlockInteractionResult(const FChunkWorldBlockInteractionResult& BlockResult) const
{
	return BlockResult.bHasBlock;
}

void UPorismTraceInteractionComponent::Interact(FGameplayTag InteractionTag)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr)
	{
		return;
	}

	if (LastTraceResult.TargetType == EPorismTraceInteractionTargetType::Actor && LastTraceResult.InteractableActor != nullptr)
	{
		if (PawnOwner->HasAuthority())
		{
			ExecuteActorInteraction(LastTraceResult.InteractableActor, InteractionTag);
			return;
		}

		ServerInteractActor(InteractionTag, LastTraceResult.InteractableActor);
	}
}

void UPorismTraceInteractionComponent::SetTraceDistance(float NewTraceDistance, bool bForceImmediateTrace)
{
	TraceDistance = FMath::Max(0.0f, NewTraceDistance);
	RefreshActiveTraceDistance();

	if (bForceImmediateTrace)
	{
		PerformTrace();
	}
}

void UPorismTraceInteractionComponent::SetUseThirdPersonTraceDistance(bool bInUseThirdPersonTraceDistance, bool bForceImmediateTrace)
{
	bUseThirdPersonTraceDistance = bInUseThirdPersonTraceDistance;
	RefreshActiveTraceDistance();

	if (bForceImmediateTrace)
	{
		PerformTrace();
	}
}

bool UPorismTraceInteractionComponent::CanDrawBlockLookupDebug(const AChunkWorld* ChunkWorld) const
{
	const UWorld* World = GetWorld();
	const AActor* Owner = GetOwner();
	if (!bDebugDrawBlockLookup || !IsValid(World) || !World->IsGameWorld() || !IsValid(Owner) || !IsValid(ChunkWorld))
	{
		return false;
	}

	if (Owner->IsActorBeingDestroyed() || ChunkWorld->IsActorBeingDestroyed())
	{
		return false;
	}

	return ChunkWorld->PrimLayer != nullptr;
}

void UPorismTraceInteractionComponent::DrawDebugBlockLookupCube(const AChunkWorld* ChunkWorld, const FIntVector& BlockWorldPos, const FColor& Color) const
{
	UWorld* World = GetWorld();
	if (!CanDrawBlockLookupDebug(ChunkWorld) || World == nullptr)
	{
		return;
	}

	const double CurrentTimeSeconds = World->GetTimeSeconds();
	const double MinRedrawIntervalSeconds = FMath::Max(0.05, static_cast<double>(FMath::Min(DebugDrawDuration, TraceInterval)));
	if (bHasLastBlockDebugDraw
		&& LastBlockDebugDrawPos == BlockWorldPos
		&& LastBlockDebugDrawColor == Color
		&& (CurrentTimeSeconds - LastBlockDebugDrawTimeSeconds) < MinRedrawIntervalSeconds)
	{
		return;
	}

	bHasLastBlockDebugDraw = true;
	LastBlockDebugDrawPos = BlockWorldPos;
	LastBlockDebugDrawColor = Color;
	LastBlockDebugDrawTimeSeconds = CurrentTimeSeconds;

	const FVector CubeCenter = ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos);
	const float HalfExtent = static_cast<float>(ChunkWorld->PrimLayer->BlockSize) * 0.5f * FMath::Max(1.0f, DebugBlockCubeScale);
	DrawDebugBox(World, CubeCenter, FVector(HalfExtent), Color, false, DebugDrawDuration, 0, DebugDrawThickness);
}

void UPorismTraceInteractionComponent::LogBlockLookupDiagnostic(const TCHAR* Stage, const FHitResult* Hit, const FIntVector* BlockWorldPos, const TCHAR* Extra) const
{
	if (!bLogBlockLookupDiagnostics)
	{
		return;
	}

	const FString HitActorName = Hit ? GetNameSafe(Hit->GetActor()) : TEXT("None");
	const FString HitComponentName = Hit ? GetNameSafe(Hit->GetComponent()) : TEXT("None");
	const FString ImpactPointString = Hit ? Hit->ImpactPoint.ToString() : TEXT("None");
	const FString TraceStartString = Hit ? Hit->TraceStart.ToString() : TEXT("None");
	const FString BlockPosString = BlockWorldPos ? BlockWorldPos->ToString() : TEXT("None");
	const int32 HitItem = Hit ? Hit->Item : INDEX_NONE;
	const int32 FaceIndex = Hit ? Hit->FaceIndex : INDEX_NONE;

	UE_LOG(
		LogPorismTraceInteraction,
		Warning,
		TEXT("BlockLookupDiagnostic Stage=%s Owner=%s HitActor=%s HitComponent=%s ImpactPoint=%s TraceStart=%s HitItem=%d FaceIndex=%d BlockWorldPos=%s %s"),
		Stage,
		*GetNameSafe(GetOwner()),
		*HitActorName,
		*HitComponentName,
		*ImpactPointString,
		*TraceStartString,
		HitItem,
		FaceIndex,
		*BlockPosString,
		Extra ? Extra : TEXT(""));
}

void UPorismTraceInteractionComponent::PerformTrace()
{
	if (!ShouldRunTrace())
	{
		return;
	}

	FVector EyeLocation = FVector::ZeroVector;
	FRotator EyeRotation = FRotator::ZeroRotator;
	if (!GetInteractionTraceView(EyeLocation, EyeRotation))
	{
		return;
	}

	const FVector TraceStart = EyeLocation;
	const FVector TraceEnd = TraceStart + EyeRotation.Vector() * ActiveTraceDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(PorismTraceInteraction), bTraceComplex);
	if (bIgnoreOwner)
	{
		QueryParams.AddIgnoredActor(GetOwner());
	}

	FHitResult ActorHit;
	const bool bActorHit = GetWorld()->LineTraceSingleByChannel(ActorHit, TraceStart, TraceEnd, TraceChannel, QueryParams);

	FHitResult BlockHit;
	bool bBlockHit = false;
	if (BlockTraceChannel == TraceChannel)
	{
		BlockHit = ActorHit;
		bBlockHit = bActorHit;
	}
	else
	{
		bBlockHit = GetWorld()->LineTraceSingleByChannel(BlockHit, TraceStart, TraceEnd, BlockTraceChannel, QueryParams);
	}

	if (bDebugDrawTrace)
	{
		const FVector ActorDebugTraceEnd = bActorHit ? ActorHit.ImpactPoint : TraceEnd;
		DrawDebugLine(GetWorld(), TraceStart, ActorDebugTraceEnd, bActorHit ? FColor::Green : FColor::Red, false, DebugDrawDuration, 0, DebugDrawThickness);
		if (bActorHit)
		{
			DrawDebugSphere(GetWorld(), ActorHit.ImpactPoint, 6.0f, 12, FColor::Green, false, DebugDrawDuration);
		}

		if (BlockTraceChannel != TraceChannel)
		{
			const FVector BlockDebugTraceEnd = bBlockHit ? BlockHit.ImpactPoint : TraceEnd;
			DrawDebugLine(GetWorld(), TraceStart, BlockDebugTraceEnd, bBlockHit ? FColor::Cyan : FColor::Blue, false, DebugDrawDuration, 0, DebugDrawThickness);
			if (bBlockHit)
			{
				DrawDebugSphere(GetWorld(), BlockHit.ImpactPoint, 5.0f, 12, FColor::Cyan, false, DebugDrawDuration);
			}
		}
	}

	FPorismTraceInteractionResult NewResult;
	FChunkWorldBlockInteractionResult NewBlockResult;
	bool bHasNewBlockResult = false;
	if (bActorHit && (!bBlockHit || FVector::DistSquared(TraceStart, ActorHit.ImpactPoint) <= FVector::DistSquared(TraceStart, BlockHit.ImpactPoint)))
	{
		NewResult.bHasHit = true;
		NewResult.Hit = ActorHit;
		NewResult.InteractableActor = ActorHit.GetActor();
	}

	FPorismTraceInteractionResult ActorResult;
	if (bActorHit)
	{
		TryBuildActorResult(ActorHit, ActorResult);
	}

	FChunkWorldBlockInteractionResult BlockResult;
	if (bBlockHit)
	{
		TryBuildBlockResult(EyeRotation.Vector(), BlockHit, BlockResult);
	}

	if (ActorResult.TargetType != EPorismTraceInteractionTargetType::None && IsResultCloser(TraceStart, ActorResult, NewResult))
	{
		NewResult = ActorResult;
	}

	if (ShouldAcceptBlockInteractionResult(BlockResult) && IsBlockResultCloser(TraceStart, BlockResult, NewResult))
	{
		NewResult = FPorismTraceInteractionResult();
		NewResult.bHasHit = true;
		NewResult.Hit = BlockResult.Hit;
		NewResult.TargetType = EPorismTraceInteractionTargetType::Block;
		NewBlockResult = BlockResult;
		bHasNewBlockResult = true;
	}

	const bool bChanged = LastTraceResult.TargetType != NewResult.TargetType
		|| LastTraceResult.InteractableActor != NewResult.InteractableActor
		|| LastTraceResult.bHasHit != NewResult.bHasHit;
	const bool bBlockChanged = DidBlockInteractionResultChange(bHasActiveBlockInteraction, LastBlockInteractionResult, bHasNewBlockResult, NewBlockResult);

	const FPorismTraceInteractionResult PreviousResult = LastTraceResult;
	const FChunkWorldBlockInteractionResult PreviousBlockResult = LastBlockInteractionResult;
	const bool bHadPreviousBlockResult = bHasActiveBlockInteraction;

	LastTraceResult = NewResult;
	if (bHasNewBlockResult)
	{
		LastBlockInteractionResult = NewBlockResult;
	}
	bHasActiveBlockInteraction = bHasNewBlockResult;

	if (bChanged || bBlockChanged)
	{
		BroadcastInteractionTransition(PreviousResult, LastTraceResult, bHadPreviousBlockResult, PreviousBlockResult, bHasActiveBlockInteraction, LastBlockInteractionResult);
		if (bChanged)
		{
			OnTraceInteractionUpdated.Broadcast(LastTraceResult);
		}
		if (bBlockChanged && bHasActiveBlockInteraction)
		{
			OnBlockInteractionUpdated.Broadcast(LastBlockInteractionResult);
		}
	}

	if (bHasActiveBlockInteraction)
	{
		EvaluateBlockCustomDataInitialization(LastBlockInteractionResult);
	}
	else
	{
		ResetTrackedBlockCustomDataInitialization();
	}
}

bool UPorismTraceInteractionComponent::TryBuildActorResult(const FHitResult& Hit, FPorismTraceInteractionResult& InOutResult) const
{
	AActor* HitActor = Hit.GetActor();
	if (!IsInteractableActor(HitActor))
	{
		return false;
	}

	InOutResult.bHasHit = true;
	InOutResult.Hit = Hit;
	InOutResult.InteractableActor = HitActor;
	InOutResult.bIsInteractable = true;
	InOutResult.TargetType = EPorismTraceInteractionTargetType::Actor;
	return true;
}

bool UPorismTraceInteractionComponent::TryBuildBlockResult(const FVector& TraceDirection, const FHitResult& Hit, FChunkWorldBlockInteractionResult& InOutResult) const
{
	FChunkWorldResolvedBlockHit ResolvedHit;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, TraceDirection, ResolvedHit) || !ResolvedHit.bHasBlock)
	{
		AChunkWorld* ApproximateChunkWorld = nullptr;
		if (bDebugDrawBlockLookup && UChunkWorldBlockHitBlueprintLibrary::GetChunkWorldFromHitResult(Hit, ApproximateChunkWorld) && IsValid(ApproximateChunkWorld))
		{
			const FIntVector ApproximateBlockWorldPos = ApproximateChunkWorld->UEWorldPosToBlockWorldPos(Hit.ImpactPoint);
			DrawDebugBlockLookupCube(ApproximateChunkWorld, ApproximateBlockWorldPos, FColor::Red);
			LogBlockLookupDiagnostic(TEXT("ResolveBlockHitFailed"), &Hit, &ApproximateBlockWorldPos);
		}
		else
		{
			LogBlockLookupDiagnostic(TEXT("ResolveBlockHitFailed"), &Hit, nullptr);
		}
		return false;
	}

	FGameplayTag BlockTypeName;
	FInstancedStruct Definition;
	if (!UChunkWorldBlockHitBlueprintLibrary::TryGetBlockDefinitionForResolvedBlockHit(ResolvedHit, BlockTypeName, Definition) || !BlockTypeName.IsValid())
	{
		DrawDebugBlockLookupCube(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos, FColor::Red);
		LogBlockLookupDiagnostic(TEXT("ResolveBlockTypeFailed"), &Hit, &ResolvedHit.BlockWorldPos);
		return false;
	}

	InOutResult.bHasBlock = true;
	InOutResult.Hit = Hit;
	InOutResult.ResolvedBlockHit = ResolvedHit;
	InOutResult.BlockTypeName = BlockTypeName;

	DrawDebugBlockLookupCube(ResolvedHit.ChunkWorld, ResolvedHit.BlockWorldPos, FColor::Green);
	LogBlockLookupDiagnostic(
		TEXT("BlockLookupSuccess"),
		&Hit,
		&ResolvedHit.BlockWorldPos,
		*FString::Printf(
			TEXT("BlockTypeName=%s ResolveSource=%d MaterialIndex=%d MeshIndex=%d RepresentativeWorldPos=%s"),
			*BlockTypeName.ToString(),
			static_cast<int32>(ResolvedHit.ResolveSource),
			ResolvedHit.MaterialIndex,
			ResolvedHit.MeshIndex,
			*ResolvedHit.RepresentativeWorldPos.ToString()));
	return true;
}

void UPorismTraceInteractionComponent::RefreshActiveTraceDistance()
{
	ActiveTraceDistance = GetEffectiveTraceDistance();
}

float UPorismTraceInteractionComponent::GetEffectiveTraceDistance() const
{
	return TraceDistance + (bUseThirdPersonTraceDistance ? FMath::Max(0.0f, ThirdPersonTraceDistanceBonus) : 0.0f);
}

void UPorismTraceInteractionComponent::LogTraceGateState(FName Reason)
{
	if (LastTraceGateReason == Reason)
	{
		return;
	}

	LastTraceGateReason = Reason;
}

void UPorismTraceInteractionComponent::BroadcastInteractionSuccess(const FPorismTraceInteractionResult& Result)
{
	OnInteractionSucceeded.Broadcast(Result);
}

void UPorismTraceInteractionComponent::BroadcastInteractionTransition(const FPorismTraceInteractionResult& PreviousResult, const FPorismTraceInteractionResult& NewResult, bool bHadPreviousBlockResult, const FChunkWorldBlockInteractionResult& PreviousBlockResult, bool bHasNewBlockResult, const FChunkWorldBlockInteractionResult& NewBlockResult)
{
	const bool bActorChanged = PreviousResult.TargetType == EPorismTraceInteractionTargetType::Actor
		&& (NewResult.TargetType != EPorismTraceInteractionTargetType::Actor || PreviousResult.InteractableActor != NewResult.InteractableActor);
	if (bActorChanged && PreviousResult.InteractableActor)
	{
		OnActorInteractionEnded.Broadcast(PreviousResult.InteractableActor);
	}

	const bool bBlockChanged = bHadPreviousBlockResult
		&& (!bHasNewBlockResult || PreviousBlockResult.ResolvedBlockHit.BlockWorldPos != NewBlockResult.ResolvedBlockHit.BlockWorldPos);
	if (bBlockChanged)
	{
		OnBlockInteractionEnded.Broadcast(PreviousBlockResult);
	}

	const bool bNewActor = NewResult.TargetType == EPorismTraceInteractionTargetType::Actor
		&& (PreviousResult.TargetType != EPorismTraceInteractionTargetType::Actor || PreviousResult.InteractableActor != NewResult.InteractableActor);
	if (bNewActor && NewResult.InteractableActor)
	{
		OnActorInteractionStarted.Broadcast(NewResult.InteractableActor);
	}

	const bool bNewBlock = bHasNewBlockResult
		&& (!bHadPreviousBlockResult || PreviousBlockResult.ResolvedBlockHit.BlockWorldPos != NewBlockResult.ResolvedBlockHit.BlockWorldPos);
	if (bNewBlock)
	{
		OnBlockInteractionStarted.Broadcast(NewBlockResult);
	}
}

bool UPorismTraceInteractionComponent::IsInteractableActor(AActor* Actor) const
{
	if (Actor == nullptr)
	{
		return false;
	}

	if (Actor->GetClass()->ImplementsInterface(UPorismInteractableInterface::StaticClass()))
	{
		return true;
	}

	for (const TSubclassOf<UInterface>& InterfaceClass : InteractableInterfaces)
	{
		if (*InterfaceClass && Actor->GetClass()->ImplementsInterface(InterfaceClass))
		{
			return true;
		}
	}

	return false;
}

bool UPorismTraceInteractionComponent::CanInteractWithActor(AActor* Actor, const FGameplayTag& InteractionTag) const
{
	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr || !IsInteractableActor(Actor))
	{
		return false;
	}

	if (!Actor->GetClass()->ImplementsInterface(UPorismInteractableInterface::StaticClass()))
	{
		return true;
	}

	return IPorismInteractableInterface::Execute_CanHandleInteraction(Actor, const_cast<APawn*>(PawnOwner), InteractionTag);
}

void UPorismTraceInteractionComponent::ExecuteActorInteraction(AActor* Actor, const FGameplayTag& InteractionTag)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr || Actor == nullptr || !CanInteractWithActor(Actor, InteractionTag))
	{
		return;
	}

	if (Actor->GetClass()->ImplementsInterface(UPorismInteractableInterface::StaticClass()))
	{
		IPorismInteractableInterface::Execute_HandleInteractionByPawn(Actor, PawnOwner, InteractionTag);
	}

	if (PawnOwner->IsLocallyControlled())
	{
		if (Actor->GetClass()->ImplementsInterface(UPorismInteractableInterface::StaticClass()))
		{
			IPorismInteractableInterface::Execute_HandleLocalInteractionByPawn(Actor, PawnOwner, InteractionTag);
		}
		BroadcastInteractionSuccess(LastTraceResult);
	}
	else
	{
		ClientConfirmActorInteraction(Actor, InteractionTag);
	}
}

void UPorismTraceInteractionComponent::EvaluateBlockCustomDataInitialization(const FChunkWorldBlockInteractionResult& BlockResult)
{
	if (!BlockResult.bHasBlock || !BlockResult.ResolvedBlockHit.bHasBlock || !IsValid(BlockResult.ResolvedBlockHit.BlockTypeSchemaComponent))
	{
		ResetTrackedBlockCustomDataInitialization();
		return;
	}

	const FIntVector& BlockWorldPos = BlockResult.ResolvedBlockHit.BlockWorldPos;
	const bool bIsNewTrackedBlock = !bHasTrackedInitializedBlock || LastInitializedTrackedBlockWorldPos != BlockWorldPos;
	const bool bIsInitialized = BlockResult.ResolvedBlockHit.BlockTypeSchemaComponent->IsBlockCustomDataInitialized(BlockWorldPos);

	LastInitializedTrackedBlockWorldPos = BlockWorldPos;
	bHasTrackedInitializedBlock = true;

	if (bIsNewTrackedBlock)
	{
		bWasTrackedBlockCustomDataInitialized = bIsInitialized;
		if (bIsInitialized)
		{
			OnBlockCustomDataInitialized.Broadcast(BlockResult);
		}
		return;
	}

	if (!bWasTrackedBlockCustomDataInitialized && bIsInitialized)
	{
		bWasTrackedBlockCustomDataInitialized = true;
		OnBlockCustomDataInitialized.Broadcast(BlockResult);
		return;
	}

	bWasTrackedBlockCustomDataInitialized = bIsInitialized;
}

void UPorismTraceInteractionComponent::ResetTrackedBlockCustomDataInitialization()
{
	bHasTrackedInitializedBlock = false;
	bWasTrackedBlockCustomDataInitialized = false;
	LastInitializedTrackedBlockWorldPos = FIntVector::ZeroValue;
}

bool UPorismTraceInteractionComponent::IsResultCloser(const FVector& TraceStart, const FPorismTraceInteractionResult& CandidateResult, const FPorismTraceInteractionResult& CurrentBestResult) const
{
	if (CandidateResult.TargetType == EPorismTraceInteractionTargetType::None || !CandidateResult.bHasHit)
	{
		return false;
	}

	if (CurrentBestResult.TargetType == EPorismTraceInteractionTargetType::None || !CurrentBestResult.bHasHit)
	{
		return true;
	}

	return FVector::DistSquared(TraceStart, CandidateResult.Hit.ImpactPoint) < FVector::DistSquared(TraceStart, CurrentBestResult.Hit.ImpactPoint);
}

bool UPorismTraceInteractionComponent::IsBlockResultCloser(const FVector& TraceStart, const FChunkWorldBlockInteractionResult& CandidateResult, const FPorismTraceInteractionResult& CurrentBestResult) const
{
	if (!CandidateResult.bHasBlock)
	{
		return false;
	}

	if (CurrentBestResult.TargetType == EPorismTraceInteractionTargetType::None || !CurrentBestResult.bHasHit)
	{
		return true;
	}

	return FVector::DistSquared(TraceStart, CandidateResult.Hit.ImpactPoint) < FVector::DistSquared(TraceStart, CurrentBestResult.Hit.ImpactPoint);
}

bool UPorismTraceInteractionComponent::DidBlockInteractionResultChange(bool bHadPreviousResult, const FChunkWorldBlockInteractionResult& PreviousResult, bool bHasNewResult, const FChunkWorldBlockInteractionResult& NewResult) const
{
	return bHadPreviousResult != bHasNewResult
		|| PreviousResult.bHasBlock != NewResult.bHasBlock
		|| PreviousResult.ResolvedBlockHit.BlockWorldPos != NewResult.ResolvedBlockHit.BlockWorldPos
		|| PreviousResult.BlockTypeName != NewResult.BlockTypeName;
}

bool UPorismTraceInteractionComponent::ShouldRunTrace() const
{
	if (GetWorld() == nullptr)
	{
		const_cast<UPorismTraceInteractionComponent*>(this)->LogTraceGateState(TEXT("NoWorld"));
		return false;
	}

	if (!bOnlyOwner)
	{
		const_cast<UPorismTraceInteractionComponent*>(this)->LogTraceGateState(TEXT("RunningAllOwners"));
		return true;
	}

	const APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr)
	{
		const_cast<UPorismTraceInteractionComponent*>(this)->LogTraceGateState(TEXT("NoPawnOwner"));
		return false;
	}

	if (!PawnOwner->IsLocallyControlled())
	{
		const_cast<UPorismTraceInteractionComponent*>(this)->LogTraceGateState(TEXT("NotLocallyControlled"));
		return false;
	}

	const_cast<UPorismTraceInteractionComponent*>(this)->LogTraceGateState(TEXT("RunningLocalOwner"));
	return true;
}

void UPorismTraceInteractionComponent::ServerInteractActor_Implementation(FGameplayTag InteractionTag, AActor* TargetActor)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr || TargetActor == nullptr)
	{
		return;
	}

	if (!CanInteractWithActor(TargetActor, InteractionTag))
	{
		return;
	}

	ExecuteActorInteraction(TargetActor, InteractionTag);
}

void UPorismTraceInteractionComponent::ClientConfirmActorInteraction_Implementation(AActor* TargetActor, FGameplayTag InteractionTag)
{
	APawn* PawnOwner = Cast<APawn>(GetOwner());
	if (PawnOwner == nullptr || TargetActor == nullptr || !IsInteractableActor(TargetActor))
	{
		return;
	}

	if (TargetActor->GetClass()->ImplementsInterface(UPorismInteractableInterface::StaticClass()))
	{
		IPorismInteractableInterface::Execute_HandleLocalInteractionByPawn(TargetActor, PawnOwner, InteractionTag);
	}

	FPorismTraceInteractionResult Result = LastTraceResult;
	Result.TargetType = EPorismTraceInteractionTargetType::Actor;
	Result.InteractableActor = TargetActor;
	Result.bIsInteractable = true;
	BroadcastInteractionSuccess(Result);
}
