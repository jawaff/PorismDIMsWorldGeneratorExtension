// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnComponent.h"

#include "ChunkWorldSpawnTestTypes.generated.h"

/** Supplies deterministic bounded candidates for spawn-component automation coverage. */
UCLASS()
class UChunkWorldSpawnTestSourceProvider final : public UObject, public IChunkWorldSpawnSourceProvider
{
	GENERATED_BODY()

public:
	/** Source family exposed by this controlled provider. */
	EChunkWorldSpawnSourceFamily SourceFamily = EChunkWorldSpawnSourceFamily::ExplicitFixture;

	/** Enables or disables this provider for one test. */
	bool bEnabled = true;

	/** Controls whether this provider claims it can serve a request. */
	bool bCanServe = true;

	/** Controls whether this provider permits component query caching. */
	bool bCacheable = false;

	/** Stable cache key returned when caching is enabled. */
	FString CacheKey = TEXT("SpawnTestProvider");

	/** Controlled candidate output. */
	TArray<FChunkWorldSpawnCandidate> Candidates;

	/** Number of source queries observed by this provider. */
	mutable int32 GatherCallCount = 0;

	/** Component re-entered during a controlled provider callback. */
	TWeakObjectPtr<UChunkWorldSpawnComponent> ReentrantComponent;

	/** Ticket canceled during a controlled provider callback. */
	FGuid ReentrantTicketId;

	/** Enables cancellation from gathering to exercise stable ticket-id re-entry. */
	bool bCancelTicketDuringGather = false;

	virtual EChunkWorldSpawnSourceFamily GetSpawnSourceFamily() const override { return SourceFamily; }
	virtual bool CanServeSpawnRequest(const FChunkWorldSpawnRequest& Request) const override { return bCanServe; }
	virtual bool IsSpawnSourceProviderEnabled() const override { return bEnabled; }
	virtual void GatherSpawnCandidates(const FChunkWorldSpawnRequest& Request, const FChunkWorldSpawnQueryContext& Context, TArray<FChunkWorldSpawnCandidate>& OutCandidates) const override;
	virtual bool TryBuildSpawnCandidateCacheKey(const FChunkWorldSpawnRequest& Request, FString& OutCacheKey) const override;
};

/** Exposes one explicit component advance for isolated authority-world automation coverage. */
UCLASS()
class UChunkWorldSpawnTestComponent final : public UChunkWorldSpawnComponent
{
	GENERATED_BODY()

public:
	/** Advances pending tickets and generic boundary tracking once on the test game thread. */
	void Advance();
};

/** Supplies a concrete generic controller for subject-binding lifetime coverage. */
UCLASS()
class AChunkWorldSpawnTestController final : public AController
{
	GENERATED_BODY()
};

/** Provides a transformable generic actor for explicit boundary-registration coverage. */
UCLASS()
class AChunkWorldSpawnTestActor final : public AActor
{
	GENERATED_BODY()

public:
	/** Creates a non-colliding root so tests can move this actor through the KillZ boundary. */
	AChunkWorldSpawnTestActor();

private:
	/** Root used only to make location changes observable in automation. */
	UPROPERTY()
	TObjectPtr<USceneComponent> Root = nullptr;
};
