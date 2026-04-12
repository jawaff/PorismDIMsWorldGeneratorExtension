// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/ChunkWorldStandalonePlayerCharacterBase.h"
#include "ChunkWorldDamagePlayerCharacter.generated.h"

class UPorismHealthInteractionComponent;
class UPorismPredictedBlockStateComponent;

/**
 * Reusable standalone player character for chunk-world projects that need health-aware interaction, prediction, and startup-safe loading.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldDamagePlayerCharacter : public AChunkWorldStandalonePlayerCharacterBase
{
	GENERATED_BODY()

public:
	/** Creates a reusable standalone player character configured for damage-capable chunk-world interaction. */
	AChunkWorldDamagePlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Returns the damage-aware interaction component used for health-capable chunk-world targeting. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismHealthInteractionComponent* GetDamageTraceInteractionComponent() const { return DamageTraceInteractionComponent; }

	/** Returns the shared predicted block state component used for local block-damage prediction and reconciliation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismPredictedBlockStateComponent* GetPredictedBlockStateComponent() const { return PredictedBlockStateComponent; }

protected:
	/** Damage-aware interaction component used for chunk-world block targeting under the shared damage schema family. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Damage-aware interaction component used for chunk-world block targeting under the shared damage schema family."))
	TObjectPtr<UPorismHealthInteractionComponent> DamageTraceInteractionComponent = nullptr;

	/** Shared predicted block state component used for local damage prediction and authoritative reconciliation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Shared predicted block state component used for local damage prediction and authoritative reconciliation."))
	TObjectPtr<UPorismPredictedBlockStateComponent> PredictedBlockStateComponent = nullptr;
};
