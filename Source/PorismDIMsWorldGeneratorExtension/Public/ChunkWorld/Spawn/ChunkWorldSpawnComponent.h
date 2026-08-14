// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnOriginProvider.h"
#include "ChunkWorld/Spawn/ChunkWorldSpawnSourceProvider.h"

#include "ChunkWorldSpawnComponent.generated.h"

class AActor;
class AChunkWorldExtended;

DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkWorldSubjectOutOfWorld, const FChunkWorldOutOfWorldEvent&);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnChunkWorldSpawnResolvedNative, const FChunkWorldSpawnResult&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChunkWorldSpawnResolved, const FChunkWorldSpawnResult&, Result);

/** Coordinates generic server-authoritative location tickets from deterministic provider bounds. */
UCLASS(ClassGroup = (Porism), BlueprintType, meta = (BlueprintSpawnableComponent, DisplayName = "Chunk World Spawn Component"))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldSpawnComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates an inert coordinator that begins no policy or world-wide polling work by default. */
	UChunkWorldSpawnComponent();

	/** Returns the chunk-world owner that hosts this generic coordinator. */
	AChunkWorldExtended* GetChunkWorld() const;

	/** Creates a server-authoritative pending location ticket with optional weighted subject definitions. */
	UFUNCTION(BlueprintCallable, Category = "Chunk World|Spawn")
	FChunkWorldSpawnTicketHandle SubmitSpawnRequest(const FChunkWorldSpawnRequest& Request);

	/** Cancels a non-terminal ticket owned by this component. */
	bool CancelSpawnTicket(FChunkWorldSpawnTicketHandle Ticket);

	/** Returns the current state of a component-owned ticket. Unknown handles report Failed. */
	EChunkWorldSpawnTicketState GetSpawnTicketState(FChunkWorldSpawnTicketHandle Ticket) const;

	/** Copies the settled result for one component-owned ticket when a result exists. */
	bool GetSpawnTicketResult(FChunkWorldSpawnTicketHandle Ticket, FChunkWorldSpawnResult& OutResult) const;

	/** Registers one explicit candidate provider. Duplicate registration is rejected. */
	bool RegisterSpawnSourceProvider(TScriptInterface<IChunkWorldSpawnSourceProvider> InProvider);

	/** Unregisters one exact candidate provider. */
	bool UnregisterSpawnSourceProvider(TScriptInterface<IChunkWorldSpawnSourceProvider> InProvider);

	/** Registers the optional game-owned tagged origin provider used by authority policy evaluation. */
	bool RegisterSpawnOriginProvider(TScriptInterface<IChunkWorldSpawnOriginProvider> InProvider);

	/** Unregisters the exact optional origin provider. */
	bool UnregisterSpawnOriginProvider(TScriptInterface<IChunkWorldSpawnOriginProvider> InProvider);

	/** Broadcast once on authority when a ticket resolves an approved location; game code owns actor creation. */
	UPROPERTY(BlueprintAssignable, Category = "Chunk World|Spawn")
	FOnChunkWorldSpawnResolved OnSpawnResolved;

	/** Native authority-side mirror of the resolved location event for C++ consumers and automation. */
	FOnChunkWorldSpawnResolvedNative OnSpawnResolvedNative;

	/** Broadcast when a game-registered generic actor crosses the generic out-of-world boundary. */
	FOnChunkWorldSubjectOutOfWorld OnSubjectOutOfWorld;

	/** Registers a game-created actor for bounded generic out-of-world observation. */
	UFUNCTION(BlueprintCallable, Category = "Chunk World|Spawn")
	bool RegisterTrackedSpawnActor(AActor* Actor);

	/** Stops generic out-of-world observation for an actor previously registered by game code. */
	UFUNCTION(BlueprintCallable, Category = "Chunk World|Spawn")
	bool UnregisterTrackedSpawnActor(AActor* Actor);

protected:
	/** Advances bounded pending tickets through deterministic source selection and location approval. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Clears registrations and cancels non-terminal tickets before the owning world tears down. */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** Compact component-owned state retained for one request ticket. */
	struct FSpawnTicketRecord
	{
		FChunkWorldSpawnRequest Request;
		FChunkWorldSpawnResult Result;
		EChunkWorldSpawnTicketState State = EChunkWorldSpawnTicketState::Pending;
		int32 SourceFamilyIndex = 0;
		bool bHasSelectedCandidate = false;
		FChunkWorldSpawnCandidate SelectedCandidate;
		FInstancedStruct SelectedSubjectDefinition;
		/** Monotonic terminal ordering used to evict the oldest record deterministically within one game tick. */
		uint64 TerminalSequence = 0;
	};

	/** Retains one short-lived provider-query result so equivalent pending tickets do not repeat an expensive worldgen query. */
	struct FCandidateQueryCacheEntry
	{
		TArray<FChunkWorldSpawnCandidate> Candidates;
		double ExpiryTimeSeconds = 0.0;
	};

	/** Retains only the generic actor state needed to emit one out-of-world edge event per threshold crossing. */
	struct FTrackedSpawnActor
	{
		TWeakObjectPtr<AActor> Actor;
		bool bWasBelowKillZ = false;
	};

	/** Returns true when this call runs on the authority game thread for the owning chunk world. */
	bool IsAuthorityGameThread() const;

	/** Returns true when at least one ticket still requires location resolution or cancellation handling. */
	bool HasNonTerminalTickets() const;

	/** Enables component ticking only while a ticket or game-registered actor needs bounded game-thread processing. */
	void RefreshRuntimeTick();

	/** Advances all pending tickets through deterministic source resolution and location publication. */
	void UpdateSpawnTickets();

	/** Advances one pending ticket by stable id so provider callbacks cannot retain an invalid ticket-record reference. */
	void UpdateSpawnTicket(const FGuid& TicketId);

	/** Selects one compatible source/definition pair after re-finding its ticket around provider callbacks. */
	bool SelectCandidateForCurrentFamily(const FGuid& TicketId, FString& OutFailureReason);

	/** Resolves one deterministic biome-backed transform and preserves request distance constraints without requiring loaded collision. */
	bool ResolveBiomePlacement(const FSpawnTicketRecord& TicketRecord, FTransform& OutTransform, FString& OutFailureReason) const;

	/** Builds a cache key only when every enabled provider for this request family explicitly guarantees cache-safe query data. */
	bool TryBuildCandidateCacheKey(const FChunkWorldSpawnRequest& Request, EChunkWorldSpawnSourceFamily SourceFamily, FString& OutCacheKey) const;

	/** Clears provider-query cache entries after registration changes or teardown. */
	void InvalidateCandidateQueryCache();

	/** Removes oldest terminal ticket records for retention and optionally reserves room for one new record. */
	void PruneTerminalTickets(const FGuid& ProtectedTicketId = FGuid(), bool bReserveCapacityForNewTicket = false);

	/** Transitions one ticket to an explicit terminal cancellation and records diagnostics. */
	void CancelTicket(const FGuid& TicketId, EChunkWorldSpawnFailureCategory FailureCategory, const FString& DebugReason);

	/** Transitions one ticket to an explicit terminal failure and records diagnostics. */
	void FailTicket(const FGuid& TicketId, EChunkWorldSpawnFailureCategory FailureCategory, const FString& DebugReason);

	/** Validates the active binding shape and request-owner lifetime before or during ticket processing. */
	bool IsPendingRequestValid(const FChunkWorldSpawnRequest& Request, EChunkWorldSpawnFailureCategory& OutFailureCategory, FString& OutFailureReason) const;

	/** Observes only game-registered generic actors and emits one event when they cross the active KillZ. */
	void UpdateOutOfWorldTracking();

	/** Creates one failed ticket with a stable handle and explicit diagnostic data. */
	FChunkWorldSpawnTicketHandle CreateFailedTicket(
		const FChunkWorldSpawnRequest& Request,
		EChunkWorldSpawnFailureCategory FailureCategory,
		const FString& DebugReason);

	/** Finds a ticket record without creating new state. */
	const FSpawnTicketRecord* FindTicket(FChunkWorldSpawnTicketHandle Ticket) const;

	/** Finds a ticket record without creating new state. */
	FSpawnTicketRecord* FindTicket(FChunkWorldSpawnTicketHandle Ticket);

	/** Removes invalid weak object registrations before registration operations inspect them. */
	void PruneInvalidRegistrations();

	/** Explicit candidate providers registered with this component. */
	TArray<TWeakObjectPtr<UObject>> RegisteredSourceProviders;

	/** One game-owned tagged origin provider retained for direct provider registration and future separately-scoped policy work. */
	TWeakObjectPtr<UObject> RegisteredOriginProvider;

	/** Maximum providers registered with this coordinator so one bounded query cannot grow with unbounded component registration. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum source providers registered with this coordinator. Combined with per-provider candidate limits, this bounds source-selection cost."))
	int32 MaximumRegisteredSourceProviders = 32;

	/** Maximum candidates one provider may contribute during one bounded source-family query. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum candidates one source provider may contribute to one ticket family query. This bounds source selection cost."))
	int32 MaximumCandidatesPerProvider = 32;

	/** Number of seconds an equivalent provider query may be reused when every contributing provider supplies an explicit cache-validity key. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Seconds that an equivalent provider query may be reused only when every contributing provider supplies an explicit cache-validity key. Zero disables cross-ticket caching."))
	float CandidateQueryCacheSeconds = 0.0f;

	/** Maximum short-lived provider-query entries retained by this component. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum short-lived provider-query entries retained by this component. Oldest-expiring entries are removed first."))
	int32 MaximumCandidateQueryCacheEntries = 32;

	/** Maximum game-registered actors retained for out-of-world observation. Oldest entries are discarded first. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum generic actors retained for KillZ observation. This prevents unbounded tracking when gameplay spawns many short-lived subjects."))
	int32 MaximumTrackedExecutionActors = 128;

	/** Maximum component ticket records, including pending work and terminal records retained for polling. New requests fail once pending work occupies this bound. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum spawn ticket records, including pending work and terminal results retained for polling. New requests are rejected when pending work occupies this bound."))
	int32 MaximumSpawnTicketRecords = 128;

	/** Maximum terminal ticket results retained for polling. Oldest terminal records are removed before a new terminal result is stored. */
	UPROPERTY(EditAnywhere, Category = "Chunk World|Spawn", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Maximum completed, failed, or canceled ticket results retained for polling. Pending tickets are never evicted by this limit."))
	int32 MaximumRetainedTerminalTickets = 128;

	/** Ticket records owned solely by this component. */
	TMap<FGuid, FSpawnTicketRecord> TicketRecords;

	/** Short-lived bounded provider-query cache keyed by the world-local request filter and requested source family. */
	TMap<FString, FCandidateQueryCacheEntry> CandidateQueryCache;

	/** Generic actor references explicitly registered by game code. */
	TArray<FTrackedSpawnActor> TrackedExecutionActors;

	/** Increments once for every settled ticket so equal world times cannot randomize eviction order. */
	uint64 NextTerminalTicketSequence = 0;

};
