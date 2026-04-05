// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "UObject/Interface.h"
#include "PorismInteractableInterface.generated.h"

class APawn;

/**
 * Shared plugin-owned actor interaction contract used by the generic trace component.
 */
UINTERFACE(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API UPorismInteractableInterface : public UInterface
{
	GENERATED_BODY()
};

/**
 * Interface for actors that participate in shared interaction tracing and execution.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API IPorismInteractableInterface
{
	GENERATED_BODY()

public:
	/** Returns whether the supplied pawn may currently use the provided interaction tag on this actor. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Porism|Interaction")
	bool CanHandleInteraction(APawn* InteractingPawn, FGameplayTag InteractionTag) const;

	/** Executes the authoritative gameplay handling for the supplied interaction tag. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Porism|Interaction")
	void HandleInteractionByPawn(APawn* InteractingPawn, FGameplayTag InteractionTag);

	/** Executes the local cosmetic handling path after the authoritative interaction succeeds. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Porism|Interaction")
	void HandleLocalInteractionByPawn(APawn* InteractingPawn, FGameplayTag InteractionTag);
};
