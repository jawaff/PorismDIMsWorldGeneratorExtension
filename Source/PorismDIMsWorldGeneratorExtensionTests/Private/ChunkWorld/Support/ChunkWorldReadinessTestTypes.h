// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/Components/ChunkWorldReadinessFreezeComponent.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Pawn.h"
#include "ChunkWorldExtended/ChunkWorldWalker.h"
#include "ChunkWorldReadinessTestTypes.generated.h"

class AChunkWorldExtended;
class UBoxComponent;

/** Minimal walker used only to drive explicit runtime-readiness automation. */
UCLASS()
class UChunkWorldReadinessTestWalkerComponent final : public UActorComponent, public IChunkWorldWalker
{
	GENERATED_BODY()

public:
	virtual FVector GetTracingLocation_Implementation() const override;
	virtual FVector GetTracingVector_Implementation() const override { return FVector::ForwardVector; }
	virtual TArray<double> GetViewDistanceMultiplier_Implementation() const override { return { 1.0 }; }
	virtual void WalkerPositionInfo_Implementation(FChunkWorldWalkerInfo ChunkWorldWalkerInfo) override {}
};

/** Test-only blocking surface used to exercise bounded server settlement sweeps. */
UCLASS()
class AChunkWorldReadinessTestSurface final : public AActor
{
	GENERATED_BODY()

public:
	AChunkWorldReadinessTestSurface();

private:
	UPROPERTY()
	TObjectPtr<UBoxComponent> Collision;
};

/** Exposes one game-thread component advance for runtime-readiness automation. */
UCLASS()
class UChunkWorldReadinessTestFreezeComponent final : public UChunkWorldReadinessFreezeComponent
{
	GENERATED_BODY()

public:
	void Advance(float DeltaTime = 0.0f);
	void EmitRuntimeReady(AChunkWorldExtended* ChunkWorld, UObject* Walker, FGuid SessionId);
	void SetClientReadyTimeout(float TimeoutSeconds);
	void SetSettlementCollisionProfile(FName CollisionProfile);
};

/** Minimal possessed pawn with one explicit walker and readiness component. */
UCLASS()
class AChunkWorldReadinessTestPawn final : public APawn
{
	GENERATED_BODY()

public:
	AChunkWorldReadinessTestPawn();

	UChunkWorldReadinessTestWalkerComponent* GetTestWalker() const { return Walker; }
	UChunkWorldReadinessTestFreezeComponent* GetTestFreeze() const { return Freeze; }

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Root;

	UPROPERTY()
	TObjectPtr<UChunkWorldReadinessTestWalkerComponent> Walker;

	UPROPERTY()
	TObjectPtr<UChunkWorldReadinessTestFreezeComponent> Freeze;
};

/** Generic controller used to verify possession-bound acknowledgement validation. */
UCLASS()
class AChunkWorldReadinessTestController final : public AController
{
	GENERATED_BODY()
};
