// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ChunkWorldPlayerCharacters.generated.h"

class UChunkWorldProximityComponent;
class UPorismDamageTraceInteractionComponent;
class UPorismPredictedBlockStateComponent;
class UPorismTraceInteractionComponent;

/**
 * Minimal reusable player-facing character base for chunk-world projects that need generic interaction and swap proximity.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldPlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** Creates a minimal reusable character configured for baseline chunk-world interaction. */
	AChunkWorldPlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Returns the generic trace interaction component used for baseline chunk-world interaction. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismTraceInteractionComponent* GetTraceInteractionComponent() const { return TraceInteractionComponent; }

	/** Returns the shared proximity component used by the chunk-world swap scanner. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UChunkWorldProximityComponent* GetChunkWorldProximityComponent() const { return ChunkWorldProximityComponent; }

protected:
	/** Generic interaction component used for chunk-world block targeting without damage-specific features. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPorismTraceInteractionComponent> TraceInteractionComponent = nullptr;

	/** Shared proximity source used by plugin-owned chunk-world swap scanning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UChunkWorldProximityComponent> ChunkWorldProximityComponent = nullptr;
};

/**
 * Reusable player-facing character base for chunk-world projects that need health-aware interaction, predicted block state, and swap proximity.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldDamagePlayerCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	/** Creates a reusable character configured for damage-capable chunk-world interaction. */
	AChunkWorldDamagePlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Returns the damage-aware interaction component used for health-capable chunk-world targeting. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismDamageTraceInteractionComponent* GetDamageTraceInteractionComponent() const { return DamageTraceInteractionComponent; }

	/** Returns the shared predicted block state component used for local block-damage prediction and reconciliation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismPredictedBlockStateComponent* GetPredictedBlockStateComponent() const { return PredictedBlockStateComponent; }

	/** Returns the shared proximity component used by the chunk-world swap scanner. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UChunkWorldProximityComponent* GetChunkWorldProximityComponent() const { return ChunkWorldProximityComponent; }

protected:
	/** Damage-aware interaction component used for chunk-world block targeting under the shared damage schema family. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPorismDamageTraceInteractionComponent> DamageTraceInteractionComponent = nullptr;

	/** Shared predicted block state component used for local damage prediction and authoritative reconciliation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UPorismPredictedBlockStateComponent> PredictedBlockStateComponent = nullptr;

	/** Shared proximity source used by plugin-owned chunk-world swap scanning. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UChunkWorldProximityComponent> ChunkWorldProximityComponent = nullptr;
};
