// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/ChunkWorldStandalonePlayerCharacterBase.h"
#include "ChunkWorldHealthPlayerCharacter.generated.h"

class UPorismHealthInteractionComponent;
class UPorismPredictedBlockStateComponent;

/**
 * Reusable standalone player character for chunk-world projects that need health-aware interaction, prediction, and startup-safe loading.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldHealthPlayerCharacter : public AChunkWorldStandalonePlayerCharacterBase
{
	GENERATED_BODY()

public:
	/** Creates a reusable standalone player character configured for health-aware chunk-world interaction. */
	AChunkWorldHealthPlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Returns the health-aware interaction component used for chunk-world block targeting. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismHealthInteractionComponent* GetHealthTraceInteractionComponent() const { return HealthTraceInteractionComponent; }

	/** Returns the shared predicted block state component used for local block-health prediction and reconciliation. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismPredictedBlockStateComponent* GetPredictedBlockStateComponent() const { return PredictedBlockStateComponent; }

protected:
	/** Health-aware interaction component used for chunk-world block targeting under the shared health schema family. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Health-aware interaction component used for chunk-world block targeting under the shared health schema family."))
	TObjectPtr<UPorismHealthInteractionComponent> HealthTraceInteractionComponent = nullptr;

	/** Shared predicted block state component used for local health prediction and authoritative reconciliation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Shared predicted block state component used for local health prediction and authoritative reconciliation."))
	TObjectPtr<UPorismPredictedBlockStateComponent> PredictedBlockStateComponent = nullptr;
};
