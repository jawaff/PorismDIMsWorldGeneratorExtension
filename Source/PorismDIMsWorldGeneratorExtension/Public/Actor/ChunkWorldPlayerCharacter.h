// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Actor/ChunkWorldStandalonePlayerCharacterBase.h"
#include "ChunkWorldPlayerCharacter.generated.h"

class UPorismTraceInteractionComponent;

/**
 * Minimal reusable standalone player character for chunk-world projects that need generic interaction and startup-safe loading.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldPlayerCharacter : public AChunkWorldStandalonePlayerCharacterBase
{
	GENERATED_BODY()

public:
	/** Creates a reusable standalone player character configured for generic chunk-world interaction. */
	AChunkWorldPlayerCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** Returns the generic trace interaction component used for baseline chunk-world interaction. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Block|ChunkWorld|Character")
	UPorismTraceInteractionComponent* GetTraceInteractionComponent() const { return TraceInteractionComponent; }

protected:
	/** Generic interaction component used for chunk-world block targeting without damage-specific features. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block|ChunkWorld|Character", meta = (AllowPrivateAccess = "true", ToolTip = "Generic interaction component used for chunk-world block targeting without damage-specific features."))
	TObjectPtr<UPorismTraceInteractionComponent> TraceInteractionComponent = nullptr;
};
