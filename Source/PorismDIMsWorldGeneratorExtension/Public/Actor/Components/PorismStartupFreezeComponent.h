// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PorismStartupFreezeComponent.generated.h"

class AChunkWorldExtended;
class UMovementComponent;
class UPrimitiveComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPorismStartupFreezeSignature);

/**
 * Startup-only actor freeze that waits for registered chunk walkers to participate in chunk-world readiness before releasing movement.
 */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Porism Startup Freeze Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismStartupFreezeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates a startup-only freeze component that disables itself once relevant chunk worlds are ready. */
	UPorismStartupFreezeComponent();

	/** Returns true while this component is still holding the owner in its startup-frozen state. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ChunkWorld|Startup")
	bool IsStartupFreezeActive() const { return bStartupFreezeActive; }

	/** Returns true once this component has discovered at least one relevant chunk world for the owner's walkers. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ChunkWorld|Startup")
	bool HasResolvedStartupChunkWorlds() const { return bHasResolvedStartupChunkWorlds; }

	/** Applies the startup freeze immediately when the owner has at least one chunk walker. */
	UFUNCTION(BlueprintCallable, Category = "ChunkWorld|Startup")
	void ApplyStartupFreeze();

	/** Releases the startup freeze and restores any movement or physics state captured when the freeze began. */
	UFUNCTION(BlueprintCallable, Category = "ChunkWorld|Startup")
	void ReleaseStartupFreeze();

	/** Broadcast after startup freeze has been applied to the owner. */
	UPROPERTY(BlueprintAssignable, Category = "ChunkWorld|Startup")
	FPorismStartupFreezeSignature OnStartupFreezeApplied;

	/** Broadcast after startup freeze has been released from the owner. */
	UPROPERTY(BlueprintAssignable, Category = "ChunkWorld|Startup")
	FPorismStartupFreezeSignature OnStartupFreezeReleased;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** If true, startup freeze is applied automatically during BeginPlay when the owner exposes at least one chunk walker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Startup", meta = (ToolTip = "If true, startup freeze is applied automatically during BeginPlay when the owner exposes at least one chunk walker."))
	bool bAutoApplyOnBeginPlay = true;

	/** If true, the owner's actor transform is pinned each tick while startup freeze is active so custom movement code cannot drift before readiness. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Startup", meta = (ToolTip = "If true, the owner's actor transform is pinned each tick while startup freeze is active so custom movement code cannot drift before readiness."))
	bool bPinActorTransformWhileFrozen = true;

	/** Delay before the component warns that none of the owner's walkers have registered with a chunk world yet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Startup", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Delay before the component warns that none of the owner's walkers have registered with a chunk world yet."))
	float MissingRegistrationWarningDelaySeconds = 2.0f;

private:
	struct FObservedChunkWorldState
	{
		TWeakObjectPtr<AChunkWorldExtended> ChunkWorld;
		bool bReady = false;
		bool bHasLateRegistrationConflict = false;
		bool bLoggedLateRegistrationConflict = false;
	};

	struct FMovementFreezeState
	{
		TWeakObjectPtr<UMovementComponent> MovementComponent;
		bool bWasActive = false;
		bool bTickEnabled = false;
		bool bIsCharacterMovement = false;
		EMovementMode CharacterMovementMode = MOVE_None;
		uint8 CustomMovementMode = 0;
		float GravityScale = 1.0f;
	};

	struct FPrimitiveFreezeState
	{
		TWeakObjectPtr<UPrimitiveComponent> PrimitiveComponent;
		bool bWasSimulatingPhysics = false;
		bool bHadGravityEnabled = false;
		FVector LinearVelocity = FVector::ZeroVector;
		FVector AngularVelocityDegrees = FVector::ZeroVector;
	};

	/** Rebuilds the owner walker list from the owner actor and its components. */
	void RefreshOwnerWalkers();

	/** Captures movement state and disables motion so the owner remains in place during startup. */
	void CacheAndApplyFreezeState();

	/** Restores movement state captured when startup freeze was applied. */
	void RestoreFrozenState();

	/** Pins the owner transform while startup freeze is active so drift cannot occur between readiness checks. */
	void MaintainFrozenTransform() const;

	/** Discovers relevant chunk worlds for the owner's walkers and keeps readiness bindings in sync. */
	void RefreshObservedChunkWorlds();

	/** Returns true when every observed relevant chunk world is ready and there are no late-registration conflicts. */
	bool AreObservedChunkWorldsReady() const;

	/** Updates one observed chunk world binding from the current world and walker registration state. */
	void UpdateObservedChunkWorldState(AChunkWorldExtended* ChunkWorld);

	/** Receives chunk-world startup ready signals for the owner's observed chunk worlds. */
	UFUNCTION()
	void HandleObservedChunkWorldReady(AChunkWorldExtended* ChunkWorld);

	/** Owner transform captured when startup freeze begins so the actor can be pinned in place. */
	FTransform FrozenActorTransform = FTransform::Identity;

	/** Startup-only movement state captured from the owner. */
	TArray<FMovementFreezeState> FrozenMovementStates;

	/** Startup-only physics state captured from the owner. */
	TArray<FPrimitiveFreezeState> FrozenPrimitiveStates;

	/** Walker objects owned by this actor that drive chunk-world registration. */
	TArray<TWeakObjectPtr<UObject>> OwnerWalkers;

	/** Chunk worlds currently observed for this actor's startup readiness. */
	TArray<FObservedChunkWorldState> ObservedChunkWorldStates;

	/** Elapsed time since startup freeze began, used for delayed registration warnings. */
	float StartupFreezeElapsedSeconds = 0.0f;

	/** True once startup freeze has been applied and has not yet been released. */
	bool bStartupFreezeActive = false;

	/** True once at least one relevant chunk world has been discovered for this actor. */
	bool bHasResolvedStartupChunkWorlds = false;

	/** True once the missing-registration warning has already been emitted for the current startup freeze session. */
	bool bLoggedMissingRegistrationWarning = false;
};
