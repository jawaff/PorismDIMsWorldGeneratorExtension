// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "PorismInteractionTraceViewProviderInterface.generated.h"

/**
 * Optional actor-side view provider used by shared interaction components when a project needs a custom trace origin.
 */
UINTERFACE(BlueprintType)
class UPorismInteractionTraceViewProviderInterface : public UInterface
{
	GENERATED_BODY()
};

class PORISMDIMSWORLDGENERATOREXTENSION_API IPorismInteractionTraceViewProviderInterface
{
	GENERATED_BODY()

public:
	/**
	 * Resolves the trace view used by shared interaction components.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Porism|Interaction")
	bool ProvideInteractionTraceView(FVector& OutViewLocation, FRotator& OutViewRotation) const;
};
