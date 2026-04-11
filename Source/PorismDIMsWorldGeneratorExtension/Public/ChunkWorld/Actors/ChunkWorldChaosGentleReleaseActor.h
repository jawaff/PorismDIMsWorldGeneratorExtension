// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorld/Actors/ChunkWorldChaosDestructionPresentationActor.h"

#include "ChunkWorldChaosGentleReleaseActor.generated.h"

/**
 * Minimal destruction presentation tuned to simply unlock a pre-fractured collection and let gravity create a gentle separation.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API AChunkWorldChaosGentleReleaseActor
	: public AChunkWorldChaosDestructionPresentationActor
{
	GENERATED_BODY()

public:
	/** Creates a destruction presentation actor configured for unlock-only gentle separation. */
	AChunkWorldChaosGentleReleaseActor();
};
