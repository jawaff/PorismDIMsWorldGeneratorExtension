// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/PorismStartupFreezeComponent.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldExtended/ChunkWorldWalker.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/MovementComponent.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogPorismStartupFreeze, Log, All);

UPorismStartupFreezeComponent::UPorismStartupFreezeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPorismStartupFreezeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (bAutoApplyOnBeginPlay)
	{
		ApplyStartupFreeze();
	}
}

void UPorismStartupFreezeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (FObservedChunkWorldState& ObservedState : ObservedChunkWorldStates)
	{
		if (AChunkWorldExtended* ChunkWorld = ObservedState.ChunkWorld.Get())
		{
			ChunkWorld->OnWorldReady.RemoveDynamic(this, &UPorismStartupFreezeComponent::HandleObservedChunkWorldReady);
		}
	}

	ObservedChunkWorldStates.Reset();
	SetComponentTickEnabled(false);

	Super::EndPlay(EndPlayReason);
}

void UPorismStartupFreezeComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bStartupFreezeActive)
	{
		return;
	}

	StartupFreezeElapsedSeconds += DeltaTime;
	MaintainFrozenTransform();
	RefreshObservedChunkWorlds();

	if (!bHasResolvedStartupChunkWorlds
		&& !bLoggedMissingRegistrationWarning
		&& StartupFreezeElapsedSeconds >= FMath::Max(0.0f, MissingRegistrationWarningDelaySeconds))
	{
		UE_LOG(
			LogPorismStartupFreeze,
			Warning,
			TEXT("Startup freeze on Owner=%s is still waiting for one of its chunk walkers to register with a chunk world after %.2fs."),
			*GetNameSafe(GetOwner()),
			StartupFreezeElapsedSeconds);
		bLoggedMissingRegistrationWarning = true;
	}

	if (AreObservedChunkWorldsReady())
	{
		ReleaseStartupFreeze();
	}
}

void UPorismStartupFreezeComponent::ApplyStartupFreeze()
{
	if (bStartupFreezeActive)
	{
		return;
	}

	RefreshOwnerWalkers();
	if (OwnerWalkers.IsEmpty())
	{
		UE_LOG(
			LogPorismStartupFreeze,
			Warning,
			TEXT("Startup freeze on Owner=%s was not applied because the actor exposes no chunk walkers."),
			*GetNameSafe(GetOwner()));
		return;
	}

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	FrozenActorTransform = Owner->GetActorTransform();
	FrozenMovementStates.Reset();
	FrozenPrimitiveStates.Reset();
	ObservedChunkWorldStates.Reset();
	StartupFreezeElapsedSeconds = 0.0f;
	bHasResolvedStartupChunkWorlds = false;
	bLoggedMissingRegistrationWarning = false;
	bStartupFreezeActive = true;

	CacheAndApplyFreezeState();
	SetComponentTickEnabled(true);
	OnStartupFreezeApplied.Broadcast();

	UE_LOG(
		LogPorismStartupFreeze,
		Log,
		TEXT("Startup freeze applied to Owner=%s WalkerCount=%d"),
		*GetNameSafe(Owner),
		OwnerWalkers.Num());
}

void UPorismStartupFreezeComponent::ReleaseStartupFreeze()
{
	if (!bStartupFreezeActive)
	{
		return;
	}

	for (FObservedChunkWorldState& ObservedState : ObservedChunkWorldStates)
	{
		if (AChunkWorldExtended* ChunkWorld = ObservedState.ChunkWorld.Get())
		{
			ChunkWorld->OnWorldReady.RemoveDynamic(this, &UPorismStartupFreezeComponent::HandleObservedChunkWorldReady);
		}
	}

	RestoreFrozenState();
	ObservedChunkWorldStates.Reset();
	bStartupFreezeActive = false;
	SetComponentTickEnabled(false);
	OnStartupFreezeReleased.Broadcast();

	UE_LOG(
		LogPorismStartupFreeze,
		Log,
		TEXT("Startup freeze released for Owner=%s after %.2fs"),
		*GetNameSafe(GetOwner()),
		StartupFreezeElapsedSeconds);
}

void UPorismStartupFreezeComponent::RefreshOwnerWalkers()
{
	OwnerWalkers.Reset();

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	if (Owner->GetClass()->ImplementsInterface(UChunkWorldWalker::StaticClass()))
	{
		OwnerWalkers.Add(Owner);
	}

	TInlineComponentArray<UActorComponent*> OwnerComponents(Owner);
	for (UActorComponent* Component : OwnerComponents)
	{
		if (IsValid(Component) && Component->GetClass()->ImplementsInterface(UChunkWorldWalker::StaticClass()))
		{
			OwnerWalkers.AddUnique(Component);
		}
	}
}

void UPorismStartupFreezeComponent::CacheAndApplyFreezeState()
{
	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	TInlineComponentArray<UMovementComponent*> MovementComponents(Owner);
	for (UMovementComponent* MovementComponent : MovementComponents)
	{
		if (!IsValid(MovementComponent))
		{
			continue;
		}

		FMovementFreezeState& FreezeState = FrozenMovementStates.AddDefaulted_GetRef();
		FreezeState.MovementComponent = MovementComponent;
		FreezeState.bWasActive = MovementComponent->IsActive();
		FreezeState.bTickEnabled = MovementComponent->IsComponentTickEnabled();

		if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(MovementComponent))
		{
			FreezeState.bIsCharacterMovement = true;
			FreezeState.CharacterMovementMode = CharacterMovement->MovementMode;
			FreezeState.CustomMovementMode = CharacterMovement->CustomMovementMode;
			FreezeState.GravityScale = CharacterMovement->GravityScale;
			CharacterMovement->StopMovementImmediately();
			CharacterMovement->DisableMovement();
		}
		else
		{
			MovementComponent->StopMovementImmediately();
			MovementComponent->Deactivate();
		}

		MovementComponent->SetComponentTickEnabled(false);
	}

	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents(Owner);
	for (UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
	{
		if (!IsValid(PrimitiveComponent) || !PrimitiveComponent->IsSimulatingPhysics())
		{
			continue;
		}

		FPrimitiveFreezeState& FreezeState = FrozenPrimitiveStates.AddDefaulted_GetRef();
		FreezeState.PrimitiveComponent = PrimitiveComponent;
		FreezeState.bWasSimulatingPhysics = PrimitiveComponent->IsSimulatingPhysics();
		FreezeState.bHadGravityEnabled = PrimitiveComponent->IsGravityEnabled();
		FreezeState.LinearVelocity = PrimitiveComponent->GetPhysicsLinearVelocity();
		FreezeState.AngularVelocityDegrees = PrimitiveComponent->GetPhysicsAngularVelocityInDegrees();

		PrimitiveComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PrimitiveComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		PrimitiveComponent->SetEnableGravity(false);
		PrimitiveComponent->SetSimulatePhysics(false);
	}
}

void UPorismStartupFreezeComponent::RestoreFrozenState()
{
	for (const FMovementFreezeState& FreezeState : FrozenMovementStates)
	{
		UMovementComponent* MovementComponent = FreezeState.MovementComponent.Get();
		if (!IsValid(MovementComponent))
		{
			continue;
		}

		if (FreezeState.bIsCharacterMovement)
		{
			if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(MovementComponent))
			{
				CharacterMovement->SetMovementMode(FreezeState.CharacterMovementMode, FreezeState.CustomMovementMode);
				CharacterMovement->GravityScale = FreezeState.GravityScale;
			}
		}

		MovementComponent->SetComponentTickEnabled(FreezeState.bTickEnabled);
		if (FreezeState.bWasActive)
		{
			MovementComponent->Activate(true);
		}
		else
		{
			MovementComponent->Deactivate();
		}
	}

	for (const FPrimitiveFreezeState& FreezeState : FrozenPrimitiveStates)
	{
		UPrimitiveComponent* PrimitiveComponent = FreezeState.PrimitiveComponent.Get();
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}

		PrimitiveComponent->SetEnableGravity(FreezeState.bHadGravityEnabled);
		PrimitiveComponent->SetSimulatePhysics(FreezeState.bWasSimulatingPhysics);
		if (FreezeState.bWasSimulatingPhysics)
		{
			PrimitiveComponent->SetPhysicsLinearVelocity(FreezeState.LinearVelocity);
			PrimitiveComponent->SetPhysicsAngularVelocityInDegrees(FreezeState.AngularVelocityDegrees);
		}
	}

	FrozenMovementStates.Reset();
	FrozenPrimitiveStates.Reset();
}

void UPorismStartupFreezeComponent::MaintainFrozenTransform() const
{
	if (!bPinActorTransformWhileFrozen)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (Owner == nullptr)
	{
		return;
	}

	if (!Owner->GetActorTransform().Equals(FrozenActorTransform, 0.01f))
	{
		Owner->SetActorTransform(FrozenActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UPorismStartupFreezeComponent::RefreshObservedChunkWorlds()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}

	TArray<AActor*> ChunkWorldActors;
	UGameplayStatics::GetAllActorsOfClass(World, AChunkWorldExtended::StaticClass(), ChunkWorldActors);
	for (AActor* ChunkWorldActor : ChunkWorldActors)
	{
		UpdateObservedChunkWorldState(Cast<AChunkWorldExtended>(ChunkWorldActor));
	}

	for (int32 Index = ObservedChunkWorldStates.Num() - 1; Index >= 0; --Index)
	{
		if (!ObservedChunkWorldStates[Index].ChunkWorld.IsValid())
		{
			ObservedChunkWorldStates.RemoveAt(Index);
		}
	}
}

bool UPorismStartupFreezeComponent::AreObservedChunkWorldsReady() const
{
	if (!bHasResolvedStartupChunkWorlds)
	{
		return false;
	}

	for (const FObservedChunkWorldState& ObservedState : ObservedChunkWorldStates)
	{
		if (!ObservedState.ChunkWorld.IsValid() || ObservedState.bHasLateRegistrationConflict || !ObservedState.bReady)
		{
			return false;
		}
	}

	return ObservedChunkWorldStates.Num() > 0;
}

void UPorismStartupFreezeComponent::UpdateObservedChunkWorldState(AChunkWorldExtended* ChunkWorld)
{
	if (ChunkWorld == nullptr)
	{
		return;
	}

	bool bRelevantWorld = false;
	bool bReady = ChunkWorld->IsWorldReady();
	bool bHasLateRegistrationConflict = false;

	for (const TWeakObjectPtr<UObject>& Walker : OwnerWalkers)
	{
		UObject* WalkerObject = Walker.Get();
		if (!IsValid(WalkerObject))
		{
			continue;
		}

		const bool bWalkerRegistered = ChunkWorld->HasRegisteredChunkWorldWalker(WalkerObject);
		const bool bWalkerIncludedInStartupReady = ChunkWorld->WasChunkWorldWalkerIncludedInStartupReady(WalkerObject);
		if (bWalkerRegistered || bWalkerIncludedInStartupReady)
		{
			bRelevantWorld = true;
		}

		if (bWalkerRegistered && bReady && !bWalkerIncludedInStartupReady)
		{
			bHasLateRegistrationConflict = true;
		}
	}

	const int32 ExistingIndex = ObservedChunkWorldStates.IndexOfByPredicate(
		[ChunkWorld](const FObservedChunkWorldState& State)
		{
			return State.ChunkWorld.Get() == ChunkWorld;
		});

	if (!bRelevantWorld)
	{
		if (ExistingIndex != INDEX_NONE)
		{
			ChunkWorld->OnWorldReady.RemoveDynamic(this, &UPorismStartupFreezeComponent::HandleObservedChunkWorldReady);
			ObservedChunkWorldStates.RemoveAt(ExistingIndex);
		}
		return;
	}

	bHasResolvedStartupChunkWorlds = true;

	FObservedChunkWorldState* ObservedState = ExistingIndex != INDEX_NONE
		? &ObservedChunkWorldStates[ExistingIndex]
		: &ObservedChunkWorldStates.AddDefaulted_GetRef();

	ObservedState->ChunkWorld = ChunkWorld;
	ObservedState->bReady = bReady;
	ObservedState->bHasLateRegistrationConflict = bHasLateRegistrationConflict;

	if (ExistingIndex == INDEX_NONE)
	{
		ChunkWorld->OnWorldReady.AddUniqueDynamic(this, &UPorismStartupFreezeComponent::HandleObservedChunkWorldReady);
		UE_LOG(
			LogPorismStartupFreeze,
			Log,
			TEXT("Startup freeze on Owner=%s is now observing ChunkWorld=%s Ready=%d"),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(ChunkWorld),
			bReady ? 1 : 0);
	}

	if (bHasLateRegistrationConflict && !ObservedState->bLoggedLateRegistrationConflict)
	{
		ObservedState->bLoggedLateRegistrationConflict = true;
		UE_LOG(
			LogPorismStartupFreeze,
			Error,
			TEXT("Startup freeze on Owner=%s detected late chunk walker registration for ready ChunkWorld=%s. The actor will remain frozen until startup ordering is corrected."),
			*GetNameSafe(GetOwner()),
			*GetNameSafe(ChunkWorld));
	}
	else if (!bHasLateRegistrationConflict)
	{
		ObservedState->bLoggedLateRegistrationConflict = false;
	}
}

void UPorismStartupFreezeComponent::HandleObservedChunkWorldReady(AChunkWorldExtended* ChunkWorld)
{
	UpdateObservedChunkWorldState(ChunkWorld);
	if (AreObservedChunkWorldsReady())
	{
		ReleaseStartupFreeze();
	}
}
