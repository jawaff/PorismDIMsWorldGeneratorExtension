// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnTypes.h"

#include "ChunkWorldSpawnSourceProvider.generated.h"

/** Defines a bounded candidate-region provider consumed by UChunkWorldSpawnComponent. */
UINTERFACE(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldSpawnSourceProvider : public UInterface
{
	GENERATED_BODY()
};

/** Supplies generic candidate regions only; the component retains ticket, placement, and actor-execution ownership. */
class PORISMDIMSWORLDGENERATOREXTENSION_API IChunkWorldSpawnSourceProvider
{
	GENERATED_BODY()

public:
	/** Returns the one source family this provider contributes. */
	virtual EChunkWorldSpawnSourceFamily GetSpawnSourceFamily() const = 0;

	/** Returns whether this provider can answer the supplied request without actor spawning or unbounded search. */
	virtual bool CanServeSpawnRequest(const FChunkWorldSpawnRequest& Request) const = 0;

	/** Returns whether explicit component registration currently enables this provider. */
	virtual bool IsSpawnSourceProviderEnabled() const = 0;

	/** Appends at most QueryContext.MaximumCandidates candidate regions for the supplied request. */
	virtual void GatherSpawnCandidates(
		const FChunkWorldSpawnRequest& Request,
		const FChunkWorldSpawnQueryContext& QueryContext,
		TArray<FChunkWorldSpawnCandidate>& OutCandidates) const = 0;

	/** Returns a stable key for cache-safe query data, or false when this provider's live candidates must not be cached. */
	virtual bool TryBuildSpawnCandidateCacheKey(const FChunkWorldSpawnRequest& Request, FString& OutCacheKey) const
	{
		OutCacheKey.Reset();
		return false;
	}
};
