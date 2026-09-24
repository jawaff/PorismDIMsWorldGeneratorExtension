// Copyright 2026 Spotted Loaf Studio

#include "Actor/Components/ChunkWorldReadinessFreezeComponent.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "ChunkWorldExtended/ChunkWorldWalker.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/CollisionProfile.h"
#include "GameFramework/Character.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/MovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogChunkWorldReadinessFreeze, Log, All);

namespace ChunkWorldReadinessFreeze
{
	EMovementMode ResolveCharacterMovementRestoreMode(const UCharacterMovementComponent* CharacterMovement, const EMovementMode CapturedMode)
	{
		if (CapturedMode != MOVE_None)
		{
			return CapturedMode;
		}
		return CharacterMovement != nullptr && CharacterMovement->DefaultLandMovementMode != MOVE_None
			? static_cast<EMovementMode>(CharacterMovement->DefaultLandMovementMode)
			: MOVE_Walking;
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsFiniteRegion(const FChunkWorldSpawnRegion& Region)
	{
		return IsFiniteVector(Region.Minimum)
			&& IsFiniteVector(Region.Maximum)
			&& Region.Minimum.X <= Region.Maximum.X
			&& Region.Minimum.Y <= Region.Maximum.Y
			&& Region.Minimum.Z <= Region.Maximum.Z;
	}
}

UChunkWorldReadinessFreezeComponent::UChunkWorldReadinessFreezeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	SetIsReplicatedByDefault(true);
}

void UChunkWorldReadinessFreezeComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoApplyOnBeginPlay)
	{
		ApplyStartupFreeze();
	}
}

void UChunkWorldReadinessFreezeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwner() != nullptr && GetOwner()->HasAuthority() && RuntimeSession.SessionId.IsValid() && !IsRuntimeSessionTerminal())
	{
		SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Canceled, EChunkWorldRuntimeReadinessFailure::OwnerTornDown, TEXT("Owner teardown."));
	}

	for (FObservedChunkWorldState& State : ObservedChunkWorldStates)
	{
		if (AChunkWorldExtended* ChunkWorld = State.ChunkWorld.Get())
		{
			ChunkWorld->OnWorldReady.RemoveDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleObservedChunkWorldReady);
		}
	}
	StopLocalRuntimeWalkerTracking();
	StopObservingLayoutWrites();
	ObservedChunkWorldStates.Reset();
	SetComponentTickEnabled(false);
	Super::EndPlay(EndPlayReason);
}

void UChunkWorldReadinessFreezeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UChunkWorldReadinessFreezeComponent, RuntimeSession);
}

void UChunkWorldReadinessFreezeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bStartupFreezeActive)
	{
		StartupFreezeElapsedSeconds += DeltaTime;
		RefreshObservedChunkWorlds();
		if (!bHasResolvedStartupChunkWorlds && !bLoggedMissingRegistrationWarning
			&& StartupFreezeElapsedSeconds >= FMath::Max(0.0f, MissingRegistrationWarningDelaySeconds))
		{
			UE_LOG(LogChunkWorldReadinessFreeze, Warning, TEXT("Startup freeze Owner=%s still has no registered chunk walker after %.2fs."), *GetNameSafe(GetOwner()), StartupFreezeElapsedSeconds);
			bLoggedMissingRegistrationWarning = true;
		}
		TryFinishStartupFreeze();
	}

	if (bRuntimeFreezeActive && GetOwner() != nullptr && GetOwner()->HasAuthority() && !IsRuntimeSessionTerminal())
	{
		AChunkWorldExtended* RuntimeChunkWorld = RuntimeSession.ChunkWorld.Get();
		if (!IsValid(RuntimeChunkWorld) || RuntimeChunkWorld->IsActorBeingDestroyed() || RuntimeChunkWorld->GetWorld() != GetWorld())
		{
			SetRuntimeTerminal(
				EChunkWorldRuntimeReadinessState::Failed,
				EChunkWorldRuntimeReadinessFailure::WorldTornDown,
				TEXT("Runtime readiness chunk world was torn down."));
		}
	}

	if (bRuntimeFreezeActive && !IsRuntimeSessionTerminal() && !HasBoundRuntimeWalker())
	{
		StartLocalRuntimeWalkerTracking();
	}
	if (bRuntimeFreezeActive && GetOwner() != nullptr && GetOwner()->HasAuthority()
		&& RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForServerReady && !HasBoundRuntimeWalker())
	{
		RuntimeSession.State = EChunkWorldRuntimeReadinessState::WaitingForWalker;
	}

	if (bRuntimeFreezeActive && GetOwner() != nullptr && GetOwner()->HasAuthority() && !IsRuntimeSessionTerminal())
	{
		TrySettleRuntimeSession();
	}
	if (bRuntimeFreezeActive && GetOwner() != nullptr && GetOwner()->HasAuthority()
		&& RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForClientReady)
	{
		const double ElapsedSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() - RuntimeSessionStartTimeSeconds : 0.0;
		if (ElapsedSeconds >= FMath::Max(0.0f, RuntimeClientReadyTimeoutSeconds))
		{
			SetRuntimeTerminal(
				EChunkWorldRuntimeReadinessState::Failed,
				EChunkWorldRuntimeReadinessFailure::ClientAcknowledgementTimeout,
				TEXT("Owning client did not acknowledge local finest detail before timeout."));
		}
	}

	TryAcknowledgeLocalCoverage();
	if (bStartupFreezeActive || bRuntimeFreezeActive)
	{
		MaintainFrozenTransform();
	}
	RefreshFreezeTick();
}

FGuid UChunkWorldReadinessFreezeComponent::StartRuntimeReadinessSession(
	AChunkWorldExtended* ChunkWorld,
	const FChunkWorldSpawnRegion& CandidateRegion,
	const bool bRequireOwningClientReady)
{
	if (bStartingRuntimeSession || GetOwner() == nullptr || !GetOwner()->HasAuthority() || ChunkWorld == nullptr || ChunkWorld->GetWorld() != GetWorld()
		|| !ChunkWorldReadinessFreeze::IsFiniteRegion(CandidateRegion)
		|| (RuntimeSession.SessionId.IsValid() && !IsRuntimeSessionTerminal() && !bAutomaticStartupSession))
	{
		return FGuid();
	}

	TGuardValue<bool> StartingScope(bStartingRuntimeSession, true);
	if (bAutomaticStartupSession && RuntimeSession.SessionId.IsValid() && !IsRuntimeSessionTerminal())
		SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Canceled, EChunkWorldRuntimeReadinessFailure::None,
			TEXT("Automatic startup session superseded by an explicit spawn request."));
	bAutomaticStartupSession = false;
	StopLocalRuntimeWalkerTracking();
	if (!bStartupFreezeActive) StopObservingLayoutWrites();
	if (!bFreezeStateCaptured)
	{
		FrozenActorTransform = GetOwner()->GetActorTransform();
		CacheAndApplyFreezeState();
		bFreezeStateCaptured = true;
	}
	bRuntimeFreezeActive = true;
	RuntimeSession = FChunkWorldRuntimeReadinessSession();
	RuntimeSession.SessionId = FGuid::NewGuid();
	RuntimeSession.ReadinessId = FGuid::NewGuid();
	RuntimeSession.ChunkWorld = ChunkWorld;
	RuntimeSession.CandidateRegion = CandidateRegion;
	RuntimeSession.bRequiresOwningClientReady = bRequireOwningClientReady;
	RuntimeSession.State = EChunkWorldRuntimeReadinessState::WaitingForWalker;
	RuntimeSessionStartTimeSeconds = GetWorld() != nullptr ? GetWorld()->GetTimeSeconds() : 0.0;
	bLocalClientReadyBroadcast = false;
	ObserveLayoutWrites(ChunkWorld);
	for (const auto& State : ObservedChunkWorldStates) ObserveLayoutWrites(State.ChunkWorld.Get());

	RefreshOwnerWalkers();
	UObject* RegisteredWalker = nullptr;
	for (const TWeakObjectPtr<UObject>& Walker : OwnerWalkers)
	{
		if (Walker.IsValid() && ChunkWorld->HasRegisteredChunkWorldWalker(Walker.Get()))
		{
			RegisteredWalker = Walker.Get();
			break;
		}
	}
	RuntimeSession.TrackedWalker = RegisteredWalker;
	NotifyRuntimeStartedOnce();
	if (IsRuntimeSessionTerminal()) return RuntimeSession.SessionId;
	if (RegisteredWalker != nullptr)
	{
		StartLocalRuntimeWalkerTracking();
		if (IsRuntimeSessionTerminal())
		{
			return RuntimeSession.SessionId;
		}
	}
	RefreshFreezeTick();
	return RuntimeSession.SessionId;
}

bool UChunkWorldReadinessFreezeComponent::CancelRuntimeReadinessSession(const FGuid SessionId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || !SessionId.IsValid() || RuntimeSession.SessionId != SessionId || IsRuntimeSessionTerminal())
	{
		return false;
	}
	SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Canceled, EChunkWorldRuntimeReadinessFailure::None, TEXT("Canceled by gameplay."));
	return true;
}

bool UChunkWorldReadinessFreezeComponent::AcknowledgeOwningClientRuntimeReady(AController* Controller, const FGuid SessionId, const FGuid ReadinessId)
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority())
	{
		return false;
	}

	const bool bMatchingSession = SessionId.IsValid() && RuntimeSession.SessionId == SessionId
		&& ReadinessId.IsValid() && RuntimeSession.ReadinessId == ReadinessId;
	if (IsRuntimeSessionTerminal())
	{
		return bMatchingSession;
	}

	if (!bMatchingSession)
	{
		return false;
	}

	APawn* OwnerPawn = Cast<APawn>(GetOwner());
	if (!RuntimeSession.bRequiresOwningClientReady || Controller == nullptr || OwnerPawn == nullptr
		|| Controller->GetPawn() != OwnerPawn || RuntimeSession.ChunkWorld == nullptr
		|| !ChunkWorldReadinessFreeze::IsFiniteRegion(RuntimeSession.CandidateRegion) || !HasBoundRuntimeWalker())
	{
		SetRuntimeTerminal(
			EChunkWorldRuntimeReadinessState::Failed,
			EChunkWorldRuntimeReadinessFailure::InvalidClientAcknowledgement,
			TEXT("Rejected invalid owning-client runtime readiness acknowledgement."));
		return false;
	}

	if (RuntimeSession.bOwningClientReady)
	{
		return true;
	}

	RuntimeSession.bOwningClientReady = true;
	TrySettleRuntimeSession();
	return true;
}

void UChunkWorldReadinessFreezeComponent::ApplyStartupFreeze()
{
	if (bStartupFreezeActive)
	{
		return;
	}
	RefreshOwnerWalkers();
	if (OwnerWalkers.IsEmpty() || GetOwner() == nullptr)
	{
		UE_LOG(LogChunkWorldReadinessFreeze, Warning, TEXT("Startup freeze Owner=%s was not applied because no chunk walker exists."), *GetNameSafe(GetOwner()));
		return;
	}
	if (!bFreezeStateCaptured)
	{
		FrozenActorTransform = GetOwner()->GetActorTransform();
		FrozenMovementStates.Reset();
		FrozenPrimitiveStates.Reset();
		CacheAndApplyFreezeState();
		bFreezeStateCaptured = true;
	}
	ObservedChunkWorldStates.Reset();
	StartupFreezeElapsedSeconds = 0.0f;
	bHasResolvedStartupChunkWorlds = false;
	bLoggedMissingRegistrationWarning = false;
	bStartupFreezeActive = true;
	RefreshFreezeTick();
	OnStartupFreezeApplied.Broadcast();
}

void UChunkWorldReadinessFreezeComponent::TryFinishStartupFreeze()
{
	if (!bStartupFreezeActive || !GetOwner() || !AreObservedChunkWorldsReady()) return;
	if (!GetOwner()->HasAuthority())
	{
		if (RuntimeSession.State == EChunkWorldRuntimeReadinessState::Settled) ReleaseStartupFreeze();
		return;
	}
	if (RuntimeSession.SessionId.IsValid())
	{
		if (RuntimeSession.State == EChunkWorldRuntimeReadinessState::Settled) ReleaseStartupFreeze();
		return;
	}
	APawn* Pawn = Cast<APawn>(GetOwner());
	// Wait for possession so an initial player cannot accidentally start a server-only session.
	if (Pawn && !Pawn->GetController()) return;
	AChunkWorldExtended* PrimaryWorld = ObservedChunkWorldStates[0].ChunkWorld.Get();
	if (!PrimaryWorld || !PrimaryWorld->WorldGenDef) return;
	FVector BoundsOrigin, Extent;
	GetOwner()->GetActorBounds(true, BoundsOrigin, Extent);
	for (const auto& State : ObservedChunkWorldStates)
	{
		AChunkWorldExtended* World = State.ChunkWorld.Get();
		if (!World || !World->WorldGenDef) return;
		const FVector ChunkExtent = (World->BlockWorldPosToUEWorldPos(World->WorldGenDef->ChunkBlockSize)
			- World->BlockWorldPosToUEWorldPos(FIntVector::ZeroValue)).GetAbs();
		Extent.X = FMath::Max(Extent.X, ChunkExtent.X);
		Extent.Y = FMath::Max(Extent.Y, ChunkExtent.Y);
	}
	FChunkWorldSpawnRegion Region;
	Region.Minimum = FrozenActorTransform.GetLocation() - Extent;
	Region.Maximum = FrozenActorTransform.GetLocation() + Extent;
	Region.Minimum.Z -= FMath::Max(0.0f, RuntimeSettlementDownwardScanDistance);
	Region.Maximum.Z += FMath::Max(0.0f, RuntimeSettlementUpwardScanDistance);
	bAutomaticStartupSession = StartRuntimeReadinessSession(PrimaryWorld, Region,
		Pawn && Cast<APlayerController>(Pawn->GetController())).IsValid();
}

void UChunkWorldReadinessFreezeComponent::ReleaseStartupFreeze()
{
	if (!bStartupFreezeActive)
	{
		return;
	}
	for (FObservedChunkWorldState& State : ObservedChunkWorldStates)
	{
		if (AChunkWorldExtended* ChunkWorld = State.ChunkWorld.Get())
		{
			ChunkWorld->OnWorldReady.RemoveDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleObservedChunkWorldReady);
		}
	}
	ObservedChunkWorldStates.Reset();
	bStartupFreezeActive = false;
	if (!bRuntimeFreezeActive && bFreezeStateCaptured)
	{
		RestoreFrozenState();
		bFreezeStateCaptured = false;
	}
	RefreshFreezeTick();
	OnStartupFreezeReleased.Broadcast();
}

void UChunkWorldReadinessFreezeComponent::RefreshOwnerWalkers()
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
	TInlineComponentArray<UActorComponent*> Components(Owner);
	for (UActorComponent* Component : Components)
	{
		if (IsValid(Component) && Component->GetClass()->ImplementsInterface(UChunkWorldWalker::StaticClass()))
		{
			OwnerWalkers.AddUnique(Component);
		}
	}
}

void UChunkWorldReadinessFreezeComponent::CacheAndApplyFreezeState()
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
		FMovementFreezeState& State = FrozenMovementStates.AddDefaulted_GetRef();
		State.MovementComponent = MovementComponent;
		State.bWasActive = MovementComponent->IsActive();
		State.bTickEnabled = MovementComponent->IsComponentTickEnabled();
		if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(MovementComponent))
		{
			State.bIsCharacterMovement = true;
			State.CharacterMovementMode = CharacterMovement->MovementMode;
			State.CustomMovementMode = CharacterMovement->CustomMovementMode;
			State.GravityScale = CharacterMovement->GravityScale;
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
		FPrimitiveFreezeState& State = FrozenPrimitiveStates.AddDefaulted_GetRef();
		State.PrimitiveComponent = PrimitiveComponent;
		State.bWasSimulatingPhysics = true;
		State.bHadGravityEnabled = PrimitiveComponent->IsGravityEnabled();
		State.LinearVelocity = PrimitiveComponent->GetPhysicsLinearVelocity();
		State.AngularVelocityDegrees = PrimitiveComponent->GetPhysicsAngularVelocityInDegrees();
		PrimitiveComponent->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PrimitiveComponent->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		PrimitiveComponent->SetEnableGravity(false);
		PrimitiveComponent->SetSimulatePhysics(false);
	}
}

void UChunkWorldReadinessFreezeComponent::RestoreFrozenState()
{
	for (const FMovementFreezeState& State : FrozenMovementStates)
	{
		UMovementComponent* MovementComponent = State.MovementComponent.Get();
		if (!IsValid(MovementComponent))
		{
			continue;
		}
		if (State.bIsCharacterMovement)
		{
			if (UCharacterMovementComponent* CharacterMovement = Cast<UCharacterMovementComponent>(MovementComponent))
			{
				const EMovementMode RestoredMode = ChunkWorldReadinessFreeze::ResolveCharacterMovementRestoreMode(CharacterMovement, State.CharacterMovementMode);
				CharacterMovement->SetMovementMode(RestoredMode, RestoredMode == State.CharacterMovementMode ? State.CustomMovementMode : 0);
				CharacterMovement->GravityScale = State.GravityScale;
			}
		}
		MovementComponent->SetComponentTickEnabled(State.bTickEnabled);
		if (State.bWasActive)
		{
			MovementComponent->Activate(true);
		}
		else
		{
			MovementComponent->Deactivate();
		}
	}
	for (const FPrimitiveFreezeState& State : FrozenPrimitiveStates)
	{
		UPrimitiveComponent* PrimitiveComponent = State.PrimitiveComponent.Get();
		if (!IsValid(PrimitiveComponent))
		{
			continue;
		}
		PrimitiveComponent->SetEnableGravity(State.bHadGravityEnabled);
		PrimitiveComponent->SetSimulatePhysics(State.bWasSimulatingPhysics);
		if (State.bWasSimulatingPhysics)
		{
			PrimitiveComponent->SetPhysicsLinearVelocity(State.LinearVelocity);
			PrimitiveComponent->SetPhysicsAngularVelocityInDegrees(State.AngularVelocityDegrees);
		}
	}
	FrozenMovementStates.Reset();
	FrozenPrimitiveStates.Reset();
}

void UChunkWorldReadinessFreezeComponent::MaintainFrozenTransform() const
{
	if (!bPinActorTransformWhileFrozen || GetOwner() == nullptr)
	{
		return;
	}
	if (!GetOwner()->GetActorTransform().Equals(FrozenActorTransform, 0.01f))
	{
		GetOwner()->SetActorTransform(FrozenActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void UChunkWorldReadinessFreezeComponent::RefreshObservedChunkWorlds()
{
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return;
	}
	TArray<AActor*> ChunkWorldActors;
	UGameplayStatics::GetAllActorsOfClass(World, AChunkWorldExtended::StaticClass(), ChunkWorldActors);
	for (AActor* Actor : ChunkWorldActors)
	{
		UpdateObservedChunkWorldState(Cast<AChunkWorldExtended>(Actor));
	}
	for (int32 Index = ObservedChunkWorldStates.Num() - 1; Index >= 0; --Index)
	{
		if (!ObservedChunkWorldStates[Index].ChunkWorld.IsValid())
		{
			ObservedChunkWorldStates.RemoveAt(Index);
		}
	}
}

bool UChunkWorldReadinessFreezeComponent::AreObservedChunkWorldsReady() const
{
	if (!bHasResolvedStartupChunkWorlds || ObservedChunkWorldStates.IsEmpty())
	{
		return false;
	}
	for (const FObservedChunkWorldState& State : ObservedChunkWorldStates)
	{
		// Late walkers now receive their own runtime handshake instead of relying on the startup latch.
		if (!State.ChunkWorld.IsValid() || !State.bReady)
		{
			return false;
		}
	}
	return true;
}

void UChunkWorldReadinessFreezeComponent::UpdateObservedChunkWorldState(AChunkWorldExtended* ChunkWorld)
{
	if (ChunkWorld == nullptr)
	{
		return;
	}
	bool bRelevant = false;
	const bool bReady = ChunkWorld->IsWorldReady();
	for (const TWeakObjectPtr<UObject>& Walker : OwnerWalkers)
	{
		if (!Walker.IsValid())
		{
			continue;
		}
		const bool bWalkerRegistered = ChunkWorld->HasRegisteredChunkWorldWalker(Walker.Get());
		const bool bIncluded = ChunkWorld->WasChunkWorldWalkerIncludedInStartupReady(Walker.Get());
		bRelevant |= bWalkerRegistered || bIncluded;
	}
	const int32 ExistingIndex = ObservedChunkWorldStates.IndexOfByPredicate([ChunkWorld](const FObservedChunkWorldState& State) { return State.ChunkWorld.Get() == ChunkWorld; });
	if (!bRelevant)
	{
		if (ExistingIndex != INDEX_NONE)
		{
			ChunkWorld->OnWorldReady.RemoveDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleObservedChunkWorldReady);
			if (ChunkWorld != RuntimeSession.ChunkWorld)
				if (auto* Layout = ChunkWorld->GetLayoutRuntimeComponent())
				{
					Layout->SetStartupCoverageRequired(this, false);
					Layout->OnAutomaticLayoutWriteAttempt.RemoveAll(this);
					ObservedLayoutWriters.Remove(Layout);
				}
			ObservedChunkWorldStates.RemoveAt(ExistingIndex);
		}
		return;
	}
	bHasResolvedStartupChunkWorlds = true;
	FObservedChunkWorldState* State = ExistingIndex != INDEX_NONE ? &ObservedChunkWorldStates[ExistingIndex] : &ObservedChunkWorldStates.AddDefaulted_GetRef();
	State->ChunkWorld = ChunkWorld;
	State->bReady = bReady;
	if (!IsRuntimeSessionTerminal()) ObserveLayoutWrites(ChunkWorld);
	if (ExistingIndex == INDEX_NONE)
	{
		ChunkWorld->OnWorldReady.AddUniqueDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleObservedChunkWorldReady);
	}
}

void UChunkWorldReadinessFreezeComponent::StartLocalRuntimeWalkerTracking()
{
	if (!RuntimeSession.SessionId.IsValid() || RuntimeSession.ChunkWorld == nullptr)
	{
		return;
	}
	RefreshOwnerWalkers();
	AChunkWorldExtended* ChunkWorld = RuntimeSession.ChunkWorld;
	ObserveLayoutWrites(ChunkWorld);
	ChunkWorld->OnRuntimeWalkerReady.AddUniqueDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleRuntimeWalkerReady);

	UObject* SelectedWalker = RuntimeSession.TrackedWalker;
	if (SelectedWalker == nullptr && GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		for (const TWeakObjectPtr<UObject>& Walker : OwnerWalkers)
		{
			if (Walker.IsValid() && ChunkWorld->HasRegisteredChunkWorldWalker(Walker.Get()))
			{
				SelectedWalker = Walker.Get();
				break;
			}
		}
	}

	if (SelectedWalker == nullptr || !ChunkWorld->HasRegisteredChunkWorldWalker(SelectedWalker))
	{
		return;
	}

	if (RuntimeTrackedWalker.Get() == SelectedWalker && RuntimeTrackedChunkWorld.Get() == ChunkWorld
		&& RuntimeTrackedReadinessId == RuntimeSession.ReadinessId)
	{
		return;
	}

	if (RuntimeTrackedChunkWorld.IsValid())
	{
		StopLocalRuntimeWalkerTracking();
		ChunkWorld->OnRuntimeWalkerReady.AddUniqueDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleRuntimeWalkerReady);
	}

	bLocalClientReadyBroadcast = false;
	RuntimeTrackedWalker = SelectedWalker;
	RuntimeTrackedChunkWorld = ChunkWorld;
	RuntimeTrackedReadinessId = RuntimeSession.ReadinessId;
	if (!ChunkWorld->StartRuntimeReadinessTracking(SelectedWalker, RuntimeSession.ReadinessId))
	{
		if (GetOwner() != nullptr && GetOwner()->HasAuthority())
		{
			SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Failed, EChunkWorldRuntimeReadinessFailure::WalkerUnavailable, TEXT("Chunk world rejected runtime walker tracking."));
		}
		return;
	}

	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		RuntimeSession.TrackedWalker = SelectedWalker;
		if (RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForWalker)
		{
			RuntimeSession.State = EChunkWorldRuntimeReadinessState::WaitingForServerReady;
		}
	}
}

void UChunkWorldReadinessFreezeComponent::StopLocalRuntimeWalkerTracking()
{
	bLocalNativeWalkerReady = false;
	if (AChunkWorldExtended* ChunkWorld = RuntimeTrackedChunkWorld.Get())
	{
		ChunkWorld->OnRuntimeWalkerReady.RemoveDynamic(this, &UChunkWorldReadinessFreezeComponent::HandleRuntimeWalkerReady);
		if (RuntimeTrackedWalker.IsValid())
		{
			ChunkWorld->StopRuntimeReadinessTracking(RuntimeTrackedWalker.Get(), RuntimeTrackedReadinessId);
		}
	}
	RuntimeTrackedWalker.Reset();
	RuntimeTrackedChunkWorld.Reset();
	RuntimeTrackedReadinessId.Invalidate();
}

void UChunkWorldReadinessFreezeComponent::HandleRuntimeWalkerReady(AChunkWorldExtended* ChunkWorld, UObject* Walker, const FGuid ReadinessId)
{
	if (ChunkWorld == nullptr || ReadinessId != RuntimeSession.ReadinessId || Walker != RuntimeTrackedWalker.Get())
	{
		return;
	}
	bLocalNativeWalkerReady = true;
	TryAcknowledgeLocalCoverage();
	if (GetOwner() != nullptr && GetOwner()->HasAuthority())
	{
		RuntimeSession.bServerWalkerReady = true;
		TrySettleRuntimeSession();
	}
}

bool UChunkWorldReadinessFreezeComponent::IsRequiredCoverageReady(FString& OutFailure)
{
	OutFailure.Reset();
	bool bReady = !ObservedLayoutWriters.IsEmpty();
	for (const auto& Writer : ObservedLayoutWriters)
	{
		if (!Writer.IsValid()) { OutFailure = TEXT("A required terrain world was removed."); return false; }
		bReady &= Writer->IsStartupCoverageReady(this);
	}
	return bReady;
}

void UChunkWorldReadinessFreezeComponent::TryAcknowledgeLocalCoverage()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	if (bLocalClientReadyBroadcast || !bLocalNativeWalkerReady || IsRuntimeSessionTerminal()
		|| !Pawn || !Pawn->IsLocallyControlled() || !RuntimeSession.bRequiresOwningClientReady) return;
	FString Failure;
	if (!IsRequiredCoverageReady(Failure)) return;
	bLocalClientReadyBroadcast = true;
	OnOwningClientRuntimeReady.Broadcast(RuntimeSession.SessionId, RuntimeSession.ReadinessId);
}

void UChunkWorldReadinessFreezeComponent::ObserveLayoutWrites(AChunkWorldExtended* ChunkWorld)
{
	if (!GetOwner() || !ChunkWorld) return;
	UChunkWorldLayoutRuntimeComponent* Layout = ChunkWorld->GetLayoutRuntimeComponent();
	if (!Layout) return;
	Layout->SetStartupCoverageRequired(this, true, OwnerWalkers);
	if (ObservedLayoutWriters.Contains(Layout)) return;
	ObservedLayoutWriters.Add(Layout);
	if (GetOwner()->HasAuthority())
		Layout->OnAutomaticLayoutWriteAttempt.AddUObject(this, &UChunkWorldReadinessFreezeComponent::HandleAutomaticLayoutWrite);
}

void UChunkWorldReadinessFreezeComponent::StopObservingLayoutWrites()
{
	for (const auto& Writer : ObservedLayoutWriters)
		if (UChunkWorldLayoutRuntimeComponent* Layout = Writer.Get())
		{
			Layout->OnAutomaticLayoutWriteAttempt.RemoveAll(this);
			Layout->SetStartupCoverageRequired(this, false);
		}
	ObservedLayoutWriters.Reset();
}

void UChunkWorldReadinessFreezeComponent::HandleAutomaticLayoutWrite(AChunkWorldExtended* ChunkWorld, const FBox& WorldBounds)
{
	if (!bRuntimeFreezeActive || IsRuntimeSessionTerminal() || !GetOwner() || !GetOwner()->HasAuthority()) return;
	if (ChunkWorld != RuntimeSession.ChunkWorld && !ObservedChunkWorldStates.ContainsByPredicate(
		[ChunkWorld](const FObservedChunkWorldState& State) { return State.ChunkWorld == ChunkWorld; })) return;
	if (WorldBounds.Intersect(FBox(RuntimeSession.CandidateRegion.Minimum, RuntimeSession.CandidateRegion.Maximum))
		|| (ChunkWorld->GetLayoutRuntimeComponent() && ChunkWorld->GetLayoutRuntimeComponent()->DoesWriteAffectStartupCoverage(this, WorldBounds)))
		RearmRuntimeReadiness();
}

void UChunkWorldReadinessFreezeComponent::RearmRuntimeReadiness()
{
	StopLocalRuntimeWalkerTracking();
	RuntimeSession.ReadinessId = FGuid::NewGuid();
	RuntimeSession.bServerWalkerReady = false;
	RuntimeSession.bOwningClientReady = false;
	bLocalClientReadyBroadcast = false;
	RuntimeSession.State = EChunkWorldRuntimeReadinessState::WaitingForServerReady;
	StartLocalRuntimeWalkerTracking();
}

void UChunkWorldReadinessFreezeComponent::TrySettleRuntimeSession()
{
	if (GetOwner() == nullptr || !GetOwner()->HasAuthority() || IsRuntimeSessionTerminal()) return;
	AChunkWorldExtended* ChunkWorld = RuntimeSession.ChunkWorld;
	if (!ChunkWorld) return;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const UChunkWorldLayoutRuntimeComponent* Layout = ChunkWorld->GetLayoutRuntimeComponent();
	const FBox Region(RuntimeSession.CandidateRegion.Minimum, RuntimeSession.CandidateRegion.Maximum);
	FString CoverageFailure;
	bool bPendingLayouts = !IsRequiredCoverageReady(CoverageFailure);
	if (!CoverageFailure.IsEmpty())
	{
		SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Failed, EChunkWorldRuntimeReadinessFailure::RequiredCoverageFailed, CoverageFailure);
		return;
	}
	bPendingLayouts |= Layout && Layout->HasPendingAutomaticLayoutWork(Region);
	if (bStartupFreezeActive)
	{
		for (const auto& State : ObservedChunkWorldStates)
		{
			AChunkWorldExtended* StartupWorld = State.ChunkWorld.Get();
			if (StartupWorld && StartupWorld != ChunkWorld && StartupWorld->GetLayoutRuntimeComponent())
				bPendingLayouts |= StartupWorld->GetLayoutRuntimeComponent()->HasPendingAutomaticLayoutWork(Region);
		}
	}
	if (bPendingLayouts)
	{
		RuntimeSession.State = EChunkWorldRuntimeReadinessState::WaitingForLayouts;
		return;
	}
	if (RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForLayouts)
	{
		RearmRuntimeReadiness();
		return;
	}
	if (!RuntimeSession.bServerWalkerReady) return;
	if (RuntimeSession.bRequiresOwningClientReady && !RuntimeSession.bOwningClientReady)
	{
		if (RuntimeSession.State != EChunkWorldRuntimeReadinessState::WaitingForClientReady)
			RuntimeSessionStartTimeSeconds = Now;
		RuntimeSession.State = EChunkWorldRuntimeReadinessState::WaitingForClientReady;
		return;
	}
	FTransform SettledTransform;
	EChunkWorldRuntimeReadinessFailure Failure = EChunkWorldRuntimeReadinessFailure::NoSettledSurface;
	FString FailureReason;
	if (!TryBuildRuntimeSettledTransform(SettledTransform, Failure, FailureReason))
	{
		SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Failed, Failure, FailureReason);
		return;
	}
	SetRuntimeTerminal(EChunkWorldRuntimeReadinessState::Settled, EChunkWorldRuntimeReadinessFailure::None, TEXT("Server settled runtime readiness."), &SettledTransform);
}

bool UChunkWorldReadinessFreezeComponent::HasBoundRuntimeWalker() const
{
	AChunkWorldExtended* ChunkWorld = RuntimeTrackedChunkWorld.Get();
	if (ChunkWorld == nullptr || RuntimeTrackedReadinessId != RuntimeSession.ReadinessId)
	{
		return false;
	}
	return RuntimeTrackedWalker.IsValid() && ChunkWorld->HasRegisteredChunkWorldWalker(RuntimeTrackedWalker.Get());
}

bool UChunkWorldReadinessFreezeComponent::TryBuildRuntimeSettledTransform(
	FTransform& OutTransform,
	EChunkWorldRuntimeReadinessFailure& OutFailure,
	FString& OutFailureReason) const
{
	OutFailure = EChunkWorldRuntimeReadinessFailure::NoSettledSurface;
	OutFailureReason.Reset();
	AActor* Owner = GetOwner();
	UWorld* World = GetWorld();
	if (Owner == nullptr || World == nullptr || !ChunkWorldReadinessFreeze::IsFiniteRegion(RuntimeSession.CandidateRegion))
	{
		OutFailureReason = TEXT("Runtime settlement requires valid owner, world, and candidate region.");
		return false;
	}
	const FVector Location = FrozenActorTransform.GetLocation();
	if (!ChunkWorldReadinessFreeze::IsFiniteVector(Location) || Location.X < RuntimeSession.CandidateRegion.Minimum.X || Location.X > RuntimeSession.CandidateRegion.Maximum.X
		|| Location.Y < RuntimeSession.CandidateRegion.Minimum.Y || Location.Y > RuntimeSession.CandidateRegion.Maximum.Y)
	{
		OutFailureReason = TEXT("Frozen provisional location is outside candidate region.");
		return false;
	}
	const UCapsuleComponent* Capsule = Owner->FindComponentByClass<UCapsuleComponent>();
	const float Radius = Capsule != nullptr ? Capsule->GetScaledCapsuleRadius() : 0.0f;
	const float HalfHeight = Capsule != nullptr ? Capsule->GetScaledCapsuleHalfHeight() : 0.0f;
	const FCollisionShape Shape = Capsule != nullptr ? FCollisionShape::MakeCapsule(Radius, HalfHeight) : FCollisionShape::MakeSphere(1.0f);
	const double StartZ = FMath::Min<double>(RuntimeSession.CandidateRegion.Maximum.Z, Location.Z + FMath::Max(0.0f, RuntimeSettlementUpwardScanDistance));
	const double EndZ = FMath::Max<double>(RuntimeSession.CandidateRegion.Minimum.Z, Location.Z - FMath::Max(0.0f, RuntimeSettlementDownwardScanDistance));
	if (!FMath::IsFinite(StartZ) || !FMath::IsFinite(EndZ) || StartZ <= EndZ)
	{
		OutFailureReason = TEXT("Candidate region does not contain a valid bounded settlement scan.");
		return false;
	}
	ECollisionChannel TraceChannel;
	FCollisionResponseParams TraceResponses;
	if (!UCollisionProfile::GetChannelAndResponseParams(RuntimeSettlementCollisionProfile, TraceChannel, TraceResponses))
	{
		OutFailure = EChunkWorldRuntimeReadinessFailure::InvalidTraceProfile;
		OutFailureReason = FString::Printf(TEXT("Runtime settlement collision profile '%s' is invalid."), *RuntimeSettlementCollisionProfile.ToString());
		return false;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ChunkWorldRuntimeSettlement), false, Owner);
	FHitResult Hit;
	if (!World->SweepSingleByProfile(Hit, FVector(Location.X, Location.Y, StartZ), FVector(Location.X, Location.Y, EndZ), FQuat::Identity, RuntimeSettlementCollisionProfile, Shape, Params) || !Hit.bBlockingHit)
	{
		OutFailureReason = TEXT("No blocking surface found inside bounded candidate region.");
		return false;
	}
	const FVector SettledLocation(Location.X, Location.Y, Hit.ImpactPoint.Z + HalfHeight + RuntimeSettlementZOffset);
	if (!ChunkWorldReadinessFreeze::IsFiniteVector(SettledLocation) || SettledLocation.Z < RuntimeSession.CandidateRegion.Minimum.Z || SettledLocation.Z > RuntimeSession.CandidateRegion.Maximum.Z)
	{
		OutFailureReason = TEXT("Settled transform falls outside bounded candidate region.");
		return false;
	}
	OutTransform = FrozenActorTransform;
	OutTransform.SetLocation(SettledLocation);
	return true;
}

void UChunkWorldReadinessFreezeComponent::SetRuntimeTerminal(
	const EChunkWorldRuntimeReadinessState TerminalState,
	const EChunkWorldRuntimeReadinessFailure Failure,
	const FString& DebugReason,
	const FTransform* SettledTransform)
{
	StopLocalRuntimeWalkerTracking();
	StopObservingLayoutWrites();
	RuntimeSession.State = TerminalState;
	RuntimeSession.Failure = Failure;
	RuntimeSession.DebugReason = DebugReason;
	if (SettledTransform != nullptr)
	{
		RuntimeSession.SettledTransform = *SettledTransform;
		FrozenActorTransform = *SettledTransform;
		if (GetOwner() != nullptr)
		{
			GetOwner()->SetActorTransform(*SettledTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
		bRuntimeFreezeActive = false;
		if (bStartupFreezeActive)
		{
			ReleaseStartupFreeze();
		}
		else if (bFreezeStateCaptured)
		{
			RestoreFrozenState();
			bFreezeStateCaptured = false;
		}
	}
	else
	{
		bRuntimeFreezeActive = true;
	}
	NotifyRuntimeTerminalOnce();
	RefreshFreezeTick();
}

void UChunkWorldReadinessFreezeComponent::RefreshFreezeTick()
{
	SetComponentTickEnabled(bStartupFreezeActive || bRuntimeFreezeActive);
}

bool UChunkWorldReadinessFreezeComponent::IsRuntimeFreezeActive() const
{
	return RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForWalker
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForServerReady
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForClientReady
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::WaitingForLayouts
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::Failed
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::Canceled;
}

bool UChunkWorldReadinessFreezeComponent::IsRuntimeSessionTerminal() const
{
	return RuntimeSession.State == EChunkWorldRuntimeReadinessState::Settled
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::Failed
		|| RuntimeSession.State == EChunkWorldRuntimeReadinessState::Canceled;
}

void UChunkWorldReadinessFreezeComponent::NotifyRuntimeStartedOnce()
{
	if (!bRuntimeFreezeActive || IsRuntimeSessionTerminal() || !RuntimeSession.SessionId.IsValid()
		|| LastRuntimeStartedNotificationId == RuntimeSession.SessionId) return;
	LastRuntimeStartedNotificationId = RuntimeSession.SessionId;
	const FChunkWorldRuntimeReadinessSession Snapshot = RuntimeSession;
	OnRuntimeReadinessStarted.Broadcast(Snapshot);
}

void UChunkWorldReadinessFreezeComponent::NotifyRuntimeTerminalOnce()
{
	if (!IsRuntimeSessionTerminal() || !RuntimeSession.SessionId.IsValid() || LastRuntimeTerminalNotificationId == RuntimeSession.SessionId)
	{
		return;
	}
	LastRuntimeTerminalNotificationId = RuntimeSession.SessionId;
	// A listener may start another operation; remaining listeners still receive this terminal result.
	const FChunkWorldRuntimeReadinessSession Snapshot = RuntimeSession;
	if (Snapshot.State == EChunkWorldRuntimeReadinessState::Settled)
	{
		OnRuntimeReadinessSettled.Broadcast(Snapshot);
	}
	else
	{
		OnRuntimeReadinessFailed.Broadcast(Snapshot);
	}
}

void UChunkWorldReadinessFreezeComponent::HandleObservedChunkWorldReady(AChunkWorldExtended* ChunkWorld)
{
	UpdateObservedChunkWorldState(ChunkWorld);
	TryFinishStartupFreeze();
}

void UChunkWorldReadinessFreezeComponent::OnRep_RuntimeSession()
{
	if (!RuntimeSession.SessionId.IsValid())
	{
		return;
	}
	if (IsRuntimeSessionTerminal())
	{
		StopLocalRuntimeWalkerTracking();
		StopObservingLayoutWrites();
	}
	else
	{
		StartLocalRuntimeWalkerTracking();
	}
	if (RuntimeSession.State == EChunkWorldRuntimeReadinessState::Settled)
	{
		FrozenActorTransform = RuntimeSession.SettledTransform;
		if (GetOwner() != nullptr)
		{
			GetOwner()->SetActorTransform(FrozenActorTransform, false, nullptr, ETeleportType::TeleportPhysics);
		}
		bRuntimeFreezeActive = false;
		TryFinishStartupFreeze();
		if (!bStartupFreezeActive && bFreezeStateCaptured)
		{
			RestoreFrozenState();
			bFreezeStateCaptured = false;
		}
	}
	else if (IsRuntimeFreezeActive())
	{
		if (!bFreezeStateCaptured && GetOwner() != nullptr)
		{
			FrozenActorTransform = GetOwner()->GetActorTransform();
			CacheAndApplyFreezeState();
			bFreezeStateCaptured = true;
		}
		bRuntimeFreezeActive = true;
	}
	NotifyRuntimeStartedOnce();
	NotifyRuntimeTerminalOnce();
	RefreshFreezeTick();
}
