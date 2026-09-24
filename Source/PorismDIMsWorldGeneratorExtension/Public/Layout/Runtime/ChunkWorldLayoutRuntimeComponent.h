// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "ChunkWorld/ChunkWorldLifecycleTypes.h"
#include "Components/ActorComponent.h"
#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutRootSpacing.h"
#include "Layout/Types/LayoutTypes.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"

#include "Layout/Async/LayoutSolveExecution.h"
#include "ChunkWorldLayoutRuntimeComponent.generated.h"

class AChunkWorldExtended;
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnAutomaticLayoutWriteAttempt, AChunkWorldExtended*, const FBox&);

class ULayoutProfileAsset;
class ULayoutWorldBindingAsset;
class FLayoutActiveBiomeSampler;
class FLayoutPlanningAreaQueue;
struct FLayoutStartupCoverage;
struct FLayoutDiscoveryInputs;
struct FLayoutFrozenSubmissionDescriptorSeed;
struct FLayoutActiveBiomeNoiseSnapshot;
namespace LayoutWorldBindingSitePlanner { struct FSitePlanningSnapshot; }
struct FLayoutFrozenTerrainContract;
struct FLayoutPreparedContinuation;
struct FLayoutRealizationWritePlan;
struct FLayoutWorldBindingRuntimeView;
struct FLayoutRegionSolveScheduleResult;
class ILayoutSolveExecution;
struct FLayoutWorkerSolvePacket;
class FLayoutBackgroundSolveDispatcher;
struct FLayoutBackgroundSolveHandle;
struct FLayoutBackgroundSolveSubmission;
class FLayoutBackgroundSolveDispatcher;
class FLayoutFrozenSubmissionStore;

/** Published explicit continuation segment result consumed by editor preview and cached apply. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutExplicitContinuationSegmentSolveResult
{
	FLayoutContinuationSegmentDescriptor Descriptor;
	bool bSucceeded = false;
	bool bIsPartial = false;
	FResolvedLayoutConnectorRecord ConnectorRecord;
	FLayoutRegionSolveScheduleResult ScheduleResult;
	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FLayoutContinuationPreviewGeometry PreviewGeometry;
	FString FailureReason;
};

/** Published explicit continuation result consumed by editor preview and cached apply. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutExplicitContinuationSolveResult
{
	/** Route metadata and independently solved segment artifacts. */
	FLayoutContinuationRouteRecord Route;
	TArray<FLayoutExplicitContinuationSegmentSolveResult> Segments;

	/** True only when every continuation cell solved. */
	bool bSucceeded = false;

	/** True when retained solved placements can be previewed and explicitly applied as a partial continuation. */
	bool bIsPartial = false;

	/** Empty for a full solve; partial or rejected solve reason otherwise. */
	FString FailureReason;
};

/** Route-owned endpoint reservation state shared by independently submitted continuation segments. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationRouteReservationState
{
	FString EdgeKey;
	/** Automatic ownership counts once across preparation, segments and canceled captures. */
	bool bAutomatic = false;
	/** Endpoint's Created lifetime survives discovery completion, but not native unload/recreation. */
	FLayoutCreatedChunkIdentity CreationOrigin;
	bool bReleased = false;
	TSet<uint64> SegmentKeys;
	TSet<uint64> CommittedSegmentKeys;
	TSet<uint64> FailedSegmentKeys;
	/** Cancellation settles omitted work without charging a wholly failed route attempt. */
	bool bCanceled = false;
	/** A partial route consumes its endpoint allowance on the first applied segment, only once. */
	bool bCapacityConsumed = false;
	/** Prepared automatic segments await their single lifecycle handoff, not a retry. */
	bool bAwaitingSubmission = false;

	/** Failed segments are terminal omissions; queued/ready segments keep the route alive. */
	bool IsSettled() const
	{
		if (SegmentKeys.IsEmpty()) return false;
		for (const uint64 Key : SegmentKeys)
		{
			if (!CommittedSegmentKeys.Contains(Key) && !FailedSegmentKeys.Contains(Key)) return false;
		}
		return true;
	}
};

/**
 * Cached terrain-fit result captured during explicit-root preview so editor
 * apply can replay the same resolved anchor and terrain writes without
 * reopening a different runtime terrain-fit decision.
 */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutExplicitRootPreviewTerrainFit
{
	/** True when preview resolved one terrain-fit result that should be replayed on apply. */
	bool bHasResolvedTerrainFit = false;

	/** Resolved terrain-fit anchor captured during preview. */
	FIntVector TerrainAnchorBlockWorldPos = FIntVector::ZeroValue;

	/** Structured terrain-fit outcome captured during preview. */
	ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind =
		ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** Terrain writes that must be replayed before placement stamping on apply. */
	TArray<FIntVector> TerrainWritePositions;

	/** Material payload paired one-for-one with TerrainWritePositions. */
	TArray<int32> TerrainWriteMaterials;

	/** Count of cached terrain writes sourced from foundation fill before preview/apply merge. */
	int32 FoundationFillWriteCount = 0;

	/** Count of cached terrain writes sourced from perimeter ramp transitions before preview/apply merge. */
	int32 PerimeterTransitionWriteCount = 0;

	/** True when preview sampling detected perimeter transitions beyond the accepted ordinary-root depth. */
	bool bHasExcessivePerimeterTransitions = false;

	/** Positive depth when terrain alignment moved the footprint below the originally requested anchor. */
	int32 DownwardTerrainAdjustmentDepth = 0;

	/** Lowest Z written by the cached terrain-fit preview payload, or INDEX_NONE when no writes were generated. */
	int32 TerrainWriteMinZ = INDEX_NONE;

	/** Highest Z written by the cached terrain-fit preview payload, or INDEX_NONE when no writes were generated. */
	int32 TerrainWriteMaxZ = INDEX_NONE;
};

/**
 * Chunk-load-aware layout runtime bridge. Discovers deterministic sites inside
 * streamed Porism regions, caches solved records, and realizes them only after
 * the relevant chunks have already entered the loaded world window.
 */

UCLASS(ClassGroup = (Layout), BlueprintType, meta = (BlueprintSpawnableComponent))
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldLayoutRuntimeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Creates the runtime component with ticking enabled for deferred chunk-load processing. */
	UChunkWorldLayoutRuntimeComponent();

	/** Cancels background layout jobs before dispatcher storage is released. */
	virtual ~UChunkWorldLayoutRuntimeComponent() override;

	/** Cancels one submitted background layout solve without blocking the game thread. */
	void CancelBackgroundLayoutSolve(const FLayoutBackgroundSolveHandle& Handle);

	/** Ticks deferred chunk-load discovery and realization after worker-thread chunk events are queued. */
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Binds world-scoped editor debug drawing for this component's registered lifetime. */
	virtual void OnRegister() override;
	/** Removes the editor draw delegate before reconstruction or destruction. */
	virtual void OnUnregister() override;

#if WITH_EDITOR
	/** Applies planning enable/cadence changes before editor component reconstruction. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Returns the authored world bindings used for ordinary root discovery and direct world-facing selection. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Layout")
	TArray<ULayoutWorldBindingAsset*> GetLayoutWorldBindings() const;

	/** Returns the owning world's shared HUD switch; false without a chunk-world owner. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Layout|Planning|Diagnostics")
	bool GetDebugGenerationStats() const;

	/** Returns the owning world's opt-in traces/probes flag; stats alone never enables probes. */
	bool GetDetailedDiagnostics() const;

	/** Resolves the active background layout solve concurrency cap. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Layout|Async")
	int32 ResolveMaxConcurrentBackgroundLayoutSolves() const;

	/** Returns the current durable background-solve diagnostics snapshot. */
	FLayoutBackgroundSolveDiagnosticsSnapshot GetBackgroundSolveDiagnostics() const;

	/** Replaces the authored world bindings used for ordinary root discovery and direct world-facing selection. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void SetLayoutWorldBindings(const TArray<ULayoutWorldBindingAsset*>& InLayoutWorldBindings);

	/** Sets the owning world's shared HUD switch; no-op without an owner. Does not enable detailed diagnostics. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning|Diagnostics")
	void SetDebugGenerationStats(bool bInDebugGenerationStats);

	/** Republishes cached generation stats to the screen and log for editor/runtime troubleshooting. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning|Diagnostics")
	void ReportCachedDebugGenerationStats() const;

	/** Returns the server-side planning-window store owned by this chunk-world integration component. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	ULayoutPlanningWindowStore* GetLayoutPlanningWindowStore();

	/** Returns deduplicated possessed-player and eligible same-world editor-camera block positions. No camera produces no synthetic origin. */
	TArray<FIntVector> CollectPlanningWindowCenters() const;

	/** Converts accepted planned records into the existing resolved-site cache used by chunk realization. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	int32 ImportAcceptedPlannedLayoutSiteRecordsForRealization();

	/** Publishes continuation endpoints for a solved root; an invalid root clears older ports under the
	 * same key. Direct tools may supply a binding without changing runtime binding configuration. */
	void PublishResolvedRootContinuationEndpoints(
		const FString& RootRecordKey,
		const FIntPoint& ReservationKey,
		const FResolvedLayoutSiteRecord& SiteRecord,
		const ULayoutWorldBindingAsset* ExplicitWorldBindingOverride = nullptr);

	/** Returns cached root records. Committed automatic roots retain status, identity and footprint metrics,
	 * not solve/replay payloads; explicit previews retain their user-owned Apply/Clear lifetime. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Layout")
	TArray<FResolvedLayoutSiteRecord> GetResolvedLayoutSiteRecords() const;

	/** Returns a snapshot of the currently cached resolved connector records. */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Layout")
	TArray<FResolvedLayoutConnectorRecord> GetResolvedLayoutConnectorRecords() const;

	/** Finds one deterministic compatible exported continuation endpoint near a hovered block. */
	bool TryFindContinuationEndpointAtBlock(
		const FIntVector& HoveredBlockWorldPos,
		const ULayoutWorldBindingAsset* WorldBinding,
		const ULayoutProfileAsset* ContinuationProfile,
		FResolvedLayoutConnectorEndpoint& OutEndpoint,
		FString& OutFailureReason) const;

	/** Resolves, validates, and reserves one editor-selected endpoint pair before async continuation submission. */
	bool TryReserveExplicitContinuationEndpointPair(
		const FResolvedLayoutConnectorEndpoint& StartEndpoint,
		const FResolvedLayoutConnectorEndpoint& EndEndpoint,
		const ULayoutWorldBindingAsset* WorldBinding,
		const ULayoutProfileAsset* ContinuationProfile,
		int32 SolveSeed,
		FResolvedLayoutConnectorRecord& OutConnectorRecord,
		FString& OutEdgeKey,
		FString& OutFailureReason);

	/** Enqueues one ledger-reserved explicit continuation solve through shared async continuation lifecycle. */
	FLayoutBackgroundSolveHandle SubmitExplicitContinuationLayoutSolve(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const ULayoutWorldBindingAsset* WorldBinding,
		const FString& EdgeKey,
		TFunction<void(const FLayoutExplicitContinuationSolveResult& Result)> OnCompleted);

	/** Cancels one explicit continuation preview and releases its ledger edge when it never commits. */
	void CancelExplicitContinuationLayoutSolve(const FResolvedLayoutConnectorRecord& ConnectorRecord);

	/** Applies one accepted explicit continuation through existing connector realization gates. */
	bool TryApplySolvedExplicitConnector(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FString* OutFailureReason = nullptr);

#if WITH_AUTOMATION_TESTS
	/** Pumps the async explicit-root path for legacy deterministic coverage without exposing a production synchronous route. */
	bool TrySolveExplicitRootLayoutSite(
		const FIntVector& SiteCenterBlockWorldPos,
		ULayoutProfileAsset* LayoutProfile,
		int32 SolveSeed,
		FResolvedLayoutSiteRecord& OutSiteRecord,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString* OutFailureReason = nullptr);

	/** Pumps the binding-aware async explicit-root path for legacy deterministic coverage without exposing a production synchronous route. */
	bool TrySolveExplicitRootLayoutSite(
		const FIntVector& RequestedSiteCenterBlockWorldPos,
		const ULayoutWorldBindingAsset* WorldBinding,
		ULayoutProfileAsset* LayoutProfile,
		int32 SolveSeed,
		FResolvedLayoutSiteRecord& OutSiteRecord,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString* OutFailureReason = nullptr);

#endif
	/**
	 * Enqueues one explicit root layout solve after bounded game-thread request capture.
	 * Completion publishes on the game thread after worker proof finishes; callers must not block the game thread waiting for it.
	 */
	FLayoutBackgroundSolveHandle SubmitExplicitRootLayoutSiteSolve(
		const FIntVector& SiteCenterBlockWorldPos,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		int32 SolveSeed,
		TFunction<void(bool bSucceeded, const FResolvedLayoutSiteRecord& SiteRecord, const FLayoutRegionSolveScheduleResult& ScheduleResult, const FString& FailureReason)> OnCompleted,
		const FLayoutWorldBindingSiteFrontendSelection* FrontendSelection = nullptr);

#if WITH_AUTOMATION_TESTS
	/** Pumps the resolved-view async explicit-root path for legacy deterministic coverage without exposing a production synchronous route. */
	bool TrySolveExplicitRootLayoutSite(
		const FIntVector& SiteCenterBlockWorldPos,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		int32 SolveSeed,
		FResolvedLayoutSiteRecord& OutSiteRecord,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString* OutFailureReason = nullptr,
		const FLayoutWorldBindingSiteFrontendSelection* FrontendSelection = nullptr);

#endif
	/**
	 * Caches one solved explicit site record so later connector planning or chunk-gated realization can use it.
	 * This keeps preview and apply separate: callers solve first, then opt into caching/realization explicitly.
	 * Requires a producer-owned RootSolveId; missing identity is rejected before cache mutation.
	 */
	bool TryCacheExplicitRootLayoutSite(
		const FResolvedLayoutSiteRecord& SiteRecord,
		FIntPoint& OutReservationKey,
		bool bReplaceCommittedRecord = false);

	/**
	 * Applies one solved explicit site record immediately through the normal cache, connector, and realization
	 * path. This is intended for editor-driven direct-root generation after a preview solve has already succeeded.
	 * World placement requires frozen terrain authority. Standalone placement prepares no-terrain-write
	 * authority from producer root/artifact IDs and placement snapshots; template anchors always come from
	 * a validated write plan. bAllowInvalidPreview is an explicit manual debugging
	 * override for retained failed placements, not solve certification. Cached write-plan, target,
	 * frozen-contract, producer-owned root identity, lifetime and mutation guards remain mandatory.
	 */
	bool TryApplySolvedExplicitRootLayoutSite(
		const FResolvedLayoutSiteRecord& SiteRecord,
		FString* OutFailureReason = nullptr,
		bool bReplaceCommittedRecord = false,
		const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride = nullptr,
		const ULayoutWorldBindingAsset* ExplicitWorldBindingOverride = nullptr,
		bool bAllowInvalidPreview = false);

	/** Resolves the accepted frozen terrain contract for a solved site from the internal cache by root solve id. */
	bool TryGetAcceptedFrozenTerrainContract(
		const FResolvedLayoutSiteRecord& SiteRecord,
		FLayoutFrozenTerrainContract& OutFrozenTerrainContract,
		FString* OutFailureReason = nullptr) const;

	/** Clears cached site records. Committed records are preserved unless explicitly requested otherwise. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void ResetResolvedLayoutSiteRecords(bool bResetCommittedSites);

	/** With true, ends the layout generation lifetime: cancels all jobs and releases records, payloads, spacing and stamp/restore history. False removes only uncommitted cached records. Disk saves/cache are untouched. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void ResetResolvedLayoutRecords(bool bResetCommittedRecords);

	/**
	 * Clears observed loaded-chunk state across one generation restart while preserving solved layout records.
	 * Use this when StartGen must rebuild runtime chunk state but cached explicit-root records should remain pending for fresh post-restart observation.
	 */
	void ResetObservedChunkLoadStateForGenerationRestart();

	/** Drains queued chunk observations and runs planning import plus eligible site/connector realization once. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	void ProcessQueuedLayoutWorkNow();

	/** Game-thread read-only spawn gate over event-owned automatic work.
	 * Uses a finite world-space box, conservative authored write reach and existing lifetime owners.
	 * ReadinessLODCount limits discovery and unsolved owner obligations, not known overlapping writes.
	 * Required-layer queued Created intent blocks until drained; Updated/distant/duplicate events do not.
	 * Explicit previews and terminal omissions do not block. Invalid bounds fail closed on authority. */
	bool HasPendingAutomaticLayoutWork(const FBox& WorldBounds) const;

	/** Joins/leaves readiness observation for this consumer's walkers. Required-layer priority outlives observation. */
	void SetStartupCoverageRequired(UObject* Consumer, bool bRequired, TConstArrayView<TWeakObjectPtr<UObject>> Walkers = {});
	/** Tests admitted work in this consumer's configured finest-N-LOD envelopes, independently of other players.
	 * Retired no-write attempts and omitted continuation segments do not block. Native receipts and
	 * actor settlement remain separate requirements; no hypothetical chunk keys are required. Game thread only. */
	bool IsStartupCoverageReady(UObject* Consumer);
	/** True when a world-space write from any LOD intersects this consumer's configured readiness footprint. */
	bool DoesWriteAffectStartupCoverage(UObject* Consumer, const FBox& WorldBounds) const;

	/** Game-thread write-attempt boundary, including failures that may have partially written.
	 * Receivers invalidate old native receipts; this is not a native collision-complete signal. */
	FOnAutomaticLayoutWriteAttempt OnAutomaticLayoutWriteAttempt;

	/** Queues loaded coverage from any thread using a zero-based native layer index; grants no Created authority. */
	void QueueObservedLoadedChunk(const FIntVector& ChunkBlockWorldPos, int32 DetailLevel);

	/** Queues lifecycle intent from any thread; Event.DetailLevel must already be a zero-based native layer index. */
	void QueueObservedChunkLifecycle(const FChunkWorldObservedChunkLifecycleEvent& Event);

	/** Thread-safe unload handoff using a zero-based native layer index; preserves root stamp protection. */
	void QueueObservedUnloadedChunk(const FIntVector& ChunkBlockWorldPos, int32 DetailLevel);

	/** Returns true when one cached site should still be considered for realization work. */
	bool ShouldAttemptRealization(const FResolvedLayoutSiteRecord& SiteRecord) const;

	/** Returns true when one cached connector should still be considered for realization work. */
	bool ShouldAttemptConnectorRealization(const FResolvedLayoutConnectorRecord& ConnectorRecord) const;

	/** Allows distinct stable layout roots to share fresh chunks while keeping each root idempotent; saved chunks remain immutable. */
	bool CanStampRequiredChunkOrigins(
		const TSet<FIntVector>& RequiredChunkOrigins,
		FLayoutId ArtifactId,
		FLayoutId RootSolveId,
		FString& OutFailureReason) const;

	/** Marks every chunk origin in the required set as stamped by this artifact after successful writes. */
	void MarkRequiredChunkOriginsStamped(
		const TSet<FIntVector>& RequiredChunkOrigins,
		FLayoutId ArtifactId,
		FLayoutId RootSolveId);

	/** Computes the footprint origin used when converting solved cell coordinates into block-world anchors. */
	static FIntVector ComputeFootprintMinBlockWorldPos(const FResolvedLayoutSiteRecord& SiteRecord, const FIntVector& SharedCellSizeInBlocks);

	/** Computes the block-world anchor for one placed module inside a cached site record. */
	static FIntVector ComputePlacementAnchorBlockWorldPos(
		const FResolvedLayoutSiteRecord& SiteRecord,
		const FLayoutPlacedModule& Placement,
		const FIntVector& SharedCellSizeInBlocks);

#if WITH_AUTOMATION_TESTS
	/** Returns latest game-thread selected-site terrain preparation duration for benchmark automation. */
	static double GetLastSelectedSiteTerrainMillisecondsForTesting();

	/** Mirrors runtime terrain-footprint sizing for one solved placement in focused automation coverage. */
	static FIntPoint ResolvePlacementTerrainFootprintSizeInBlocksForTesting(
		const FLayoutPlacedModule& Placement,
		const FIntVector& SharedCellSizeInBlocks);

	/** Injects one cached site record without requiring biome-driven streamed discovery. */
	void AddResolvedLayoutSiteRecordForTesting(const FIntPoint& ReservationKey, const FResolvedLayoutSiteRecord& SiteRecord);

	/** Injects one cached connector record without requiring endpoint-pair discovery. */
	void AddResolvedConnectorRecordForTesting(uint64 ConnectorKey, const FResolvedLayoutConnectorRecord& ConnectorRecord);

	/** Returns one cached connector frozen terrain contract when refresh publication preserved one for the supplied record. */
	bool TryGetResolvedConnectorFrozenTerrainContractForTesting(const FResolvedLayoutConnectorRecord& ConnectorRecord, FLayoutFrozenTerrainContract& OutFrozenTerrainContract) const;

	/** Injects accepted connector terrain authority for a test record key. */
	void InjectResolvedConnectorFrozenTerrainContractForTesting(
		uint64 ConnectorKey,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract);

	/** Injects one planning-store key mapping for an existing reservation so rollback paths can preserve planning ownership. */
	void AddPlanningRecordKeyForTesting(const FIntPoint& ReservationKey, const FString& PlanningRecordKey);

	/** Returns the cached planning-store key for one reservation, or empty when none exists. */
	FString GetPlanningRecordKeyForTesting(const FIntPoint& ReservationKey) const;

	/** Rebuilds cached continuation records from currently injected site records. */
	void RefreshConnectorRecordsForTesting();

	/** Marks one finest-detail chunk origin as observed and newly eligible for realization. */
	void AddObservedLoadedChunkOriginForTesting(const FIntVector& ChunkBlockWorldPos);

	/** Marks one finest-detail chunk origin as both observed and freshly created. */
	void AddFreshCreatedChunkOriginForTesting(const FIntVector& ChunkBlockWorldPos);

	/** Injects one stable-root artifact stamp or loaded-from-save sentinel for testing. */
	void AddChunkStampMarkForTesting(const FIntVector& ChunkOrigin, FLayoutId ArtifactId, FLayoutId RootSolveId);

	/** Returns number of distinct stable roots recorded on one chunk. */
	int32 GetChunkStampedRootCountForTesting(const FIntVector& ChunkOrigin) const;

	/** Drains queued chunk observations and runs standard realization passes once. */
	void RunQueuedLayoutWorkForTesting();

	/** Runs standard site then connector realization passes on currently injected records. */
	void RunSiteAndConnectorRealizationForTesting();

	/** Realizes cached connector records without refreshing endpoint-derived connectors first. */
	void RunConnectorRealizationForTesting();

	/** Realizes one connector record directly without the higher-level warning logger. */
	bool TryRealizeConnectorForTesting(FResolvedLayoutConnectorRecord& ConnectorRecord, FString* OutFailureReason = nullptr);

	/** Realizes one site record directly without the higher-level warning logger. */
	bool TryRealizeSiteForTesting(FResolvedLayoutSiteRecord& SiteRecord, FString* OutFailureReason = nullptr);

	/** Realizes one site with an explicit frozen-terrain-contract override for perimeter test fixtures. */
	bool TryRealizeSiteWithContractForTesting(FResolvedLayoutSiteRecord& SiteRecord, const FLayoutFrozenTerrainContract& FrozenTerrainContract, FString* OutFailureReason = nullptr);

	/** Realizes a stored site record by reservation key with a contract override. */
	bool TryRealizeSiteRecordByKeyForTesting(const FIntPoint& ReservationKey, const FLayoutFrozenTerrainContract& FrozenTerrainContract, FString* OutFailureReason = nullptr);

	/** Injects one connector record into the next explicit-root apply pass after connector rebuilding. */
	void InjectExplicitApplyConnectorForTesting(const FResolvedLayoutConnectorRecord& ConnectorRecord);

	/** Injects the accepted frozen terrain contract for an already-injected connector record. */
	void InjectExplicitApplyConnectorFrozenTerrainContractForTesting(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract);

	/** Injects an accepted root realization write plan so tests can verify stale metadata fails before mutation. */
	void InjectRootRealizationWritePlanForTesting(
		FLayoutId RootSolveId,
		const FLayoutRealizationWritePlan& WritePlan);

	/** Forces the next explicit-root apply connector follow-up to fail with one supplied reason after site commit. */
	void SetNextExplicitApplyConnectorFailureReasonForTesting(const FString& FailureReason);

	/** Holds explicit-preview worker bodies until the supplied gate becomes true or cancellation wins. */
	void SetExplicitPreviewWorkGateForTesting(TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Gate);
#endif

protected:
	/** Authored world bindings used for ordinary root discovery and direct world-facing selection. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (AllowPrivateAccess = "true", ToolTip = "Authored world bindings used for ordinary root discovery and direct world-facing selection. This is the authoritative world-facing root contract instead of legacy planning or host-biome row tables."))
	TArray<TObjectPtr<ULayoutWorldBindingAsset>> LayoutWorldBindings;

	/** Fallback seed used for deterministic site scoring when no owning ChunkWorld seed is available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (AllowPrivateAccess = "true", ToolTip = "Fallback seed used for deterministic site scoring when no owning ChunkWorld seed is available. The owning ChunkWorld seed is preferred for runtime planning."))
	int32 LayoutWorldSeed = 1337;

	/** Server-side planning store owned by this component; created lazily so existing chunk-center behavior remains unchanged. */
	UPROPERTY(Transient, BlueprintReadOnly, Category = "Layout|Planning", meta = (AllowPrivateAccess = "true", ToolTip = "Server-side planning store shared by this chunk world's automatic layout work and explicit layout tools; created when first needed."))
	TObjectPtr<ULayoutPlanningWindowStore> PlanningWindowStore = nullptr;

	/** Enables best-effort automatic planning in eligible loaded terrain at every LOD. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Planning", meta = (AllowPrivateAccess = "true", DisplayName = "Enable Automatic Layout Planning", ToolTip = "Plan in eligible Created terrain across loaded LODs. Characters and the enabled editor camera set priority; ready accepted layouts apply on completion or coverage changes."))
	bool bEnablePlanningWindowRuntimeUpdates = true;

	/** Number of finest native layers whose admitted layout work holds readiness. World-scoped;
	 * known overlapping writes from any layer still settle before release. Does not limit generation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Readiness", meta = (AllowPrivateAccess = "true", ClampMin = "1", UIMin = "1", DisplayName = "Readiness LOD Count", ToolTip = "Wait for layout work in the finest N LODs around each walker: 1 waits for the finest, 2 includes the second-finest and its larger coverage. Clamped to 1 through this world's layer count. Required regions receive discovery priority; other LODs continue after release. Known overlapping writes still settle regardless of their origin LOD. Does not change native terrain readiness or generation."))
	int32 ReadinessLODCount = 1;

	/** World-shared working and unfinished-owner limits; compact native loaded metadata is separate. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Planning", meta = (AllowPrivateAccess = "true", ClampMin = "1", UIMin = "1", ToolTip = "Maximum working chunks, and separately unfinished automatic root/route owners, shared by all characters and the editor camera. Compact currently-loaded metadata is separate. Not a RAM byte limit; reduce for a smaller working set."))
	int32 MaxCachedPlanningChunks = 256;

	/** Adds an eligible editor camera without replacing character centers. Disable for character-only testing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (AllowPrivateAccess = "true", ToolTip = "Add the perspective editor camera belonging to this world to character planning priorities. Disable for character-only testing. Has no effect in packaged games."))
	bool bFollowEditorCamera = true;

	/** Cadence for queued-task priority refresh; discovery and realization do not wait for this interval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Planning", meta = (AllowPrivateAccess = "true", ClampMin = "0.1", UIMin = "0.1", ToolTip = "Seconds between queued-task priority refreshes. Discovery admits one bounded batch per update after prior work settles, subject to shared capacity. Completion and chunk readiness apply accepted layouts without waiting for this interval."))
	float PlanningWindowUpdateIntervalSeconds = 1.0f;

	/** Maximum layout solve tasks allowed to run at once. 0 uses a safe automatic value based on CPU cores. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Async", meta = (AllowPrivateAccess = "true", ClampMin = "0", ToolTip = "Maximum layout solve tasks allowed to run at once. 0 uses a safe automatic value based on CPU cores."))
	int32 MaxConcurrentBackgroundLayoutSolves = 0;

	/** Maximum async candidate solve attempts for one root, child, or continuation group before the group is dropped from active work. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Async", meta = (AllowPrivateAccess = "true", ClampMin = "1", ToolTip = "Maximum async candidate solve attempts for one root, child, or continuation group before the group is dropped from active work. This bounds retry scheduling only and does not replace any binding-authored solve budget."))
	int32 MaxLayoutSolveCandidateAttempts = 5;


private:
	struct FObservedChunkLoad
	{
		FIntVector ChunkBlockWorldPos = FIntVector::ZeroValue;
		int32 DetailLevel = INDEX_NONE;
		EChunkWorldChunkLifecycleEventType EventType = EChunkWorldChunkLifecycleEventType::Updated;
		bool bUnloaded = false;
		/** A Delete earlier in this batch ends prior Created authority even if Updated follows it. */
		bool bResetBeforeObservation = false;
	};

	/** Per-chunk idempotency marker keyed by stable layout root identity. */
	struct FLayoutChunkStampMark
	{
		bool bLoadedFromSave = false;
		TMap<FLayoutId, FLayoutId> StampedArtifactIdsByRootSolveId;
	};

	struct FDebugGenerationStatsLine
	{
		FString Message;
		FColor Color = FColor::Cyan;
	};

	/** Processes one queued chunk observation on the game thread. */
	void HandleObservedLoadedChunk(const FObservedChunkLoad& ObservedChunk);

	/** Records native coverage without inferring freshness from Updated or from generated material reads. */
	void RecordLoadedChunk(const FIntVector& Position, int32 DetailLevel, bool bCreated);
	/** Explicit preparation records the native finest chunk containing this canonical coverage tile. */
	void RecordFinestLoadedChunk(const FIntVector& Position, bool bCreated = false);
	/** Checks complete canonical tiles against current loaded-layer coverage; Created is non-consumable. */
	bool AreRequiredChunkOriginsLoaded(const TSet<FIntVector>& Origins, bool bRequireCreated = false) const;
	/** Counts native origin/LOD observations, not finest-grid expansions. */
	int32 GetObservedLoadedChunkCount() const;
	/** Game-thread material snapshot through the finest observed layer per position; no generated-data fallback. */
	TArray<int32> ReadLoadedTerrainMaterials(const TArray<FIntVector>& Positions) const;

	/** Drains queued chunk observations and runs the normal site/connector realization passes. */
	void ProcessQueuedLayoutWork();

	/** Runs the normal site/connector realization passes and retires any fresh-created chunk origins consumed this pass. */
	void RunEligibleRealizationPasses(bool bRefreshConnectors);

	/** Caches one generation stats line and publishes it immediately when stats are enabled. */
	void RecordLayoutSolvePropagationStats(const FString& ContextText, const FLayoutSolveResult& SolveResult, const FColor& Color);

	/** Publishes the most recent cached generation stats lines to the screen. */
	void PublishCachedDebugGenerationStatsOnScreen() const;

	/** Rebinds the current biome table's row-change event without owning the asset or polling its contents. */
	void RefreshPlanningBiomeTableSubscription();
	TWeakObjectPtr<class UDataTable> PlanningBiomeTable;
	FDelegateHandle PlanningBiomeTableChangedHandle;

	/** Shared text for runtime screen messages and the editor canvas. */
	FString BuildPlanningWindowDebugStatusMessage() const;
#if WITH_EDITOR
	/** Invalidates unaccepted automatic work when a referenced binding/profile/content asset changes.
	 * Traverses resident layout references only on edit events; unrelated assets and explicit previews stay untouched. */
	void HandlePlanningAssetChanged(UObject* Object, FPropertyChangedEvent& Event);

	/** Draws plain stats text only into this component's editor-world viewport; never changes viewport flags or input. */
	void DrawEditorDebugGenerationStats(class UCanvas* Canvas, class APlayerController* PlayerController);
	FDelegateHandle EditorDebugDrawHandle;
	mutable double EditorDebugStatsVisibleUntil = 0.0;
	bool bReportedEditorStatsDraw = false;
	friend class FChunkWorldRuntimeReadinessLODCountTest;
	friend class FLayoutContinuationSettlementTest;
	friend class FLayoutRootSpacingRuntimeTest;
	friend class FLayoutPlanningAreaQueueTest;
	friend class FLayoutRuntimeDescriptorRetentionTest;
	friend class FLayoutRuntimePlanningWindowPreparationTest;
	friend class FLayoutRuntimeCreatedLifetimeTest;
	friend class FLayoutRuntimePlanningWindowEditorDebugStatsTest;
#endif

	/** Logs the most recent cached generation stats lines so editor-only generation has an observable diagnostics path. */
	void LogCachedDebugGenerationStats() const;

	/** Detects direct property toggles and republishes cached generation stats when the toggle turns on. */
	void RefreshDebugGenerationStatsToggle();

	/** Creates the planning-window store on demand and returns the owned instance. */
	ULayoutPlanningWindowStore* GetOrCreateLayoutPlanningWindowStore();

	/** Cancels unaccepted automatic work and reopens area discovery after binding selection or referenced-input changes.
	 * Keeps published frozen results, placed-root exclusions, freshness and explicit previews;
	 * configuration changes never grant the generation-start loaded-target exception. */
	void InvalidateAutomaticPlanningInputs();

	/** Settles one area-owned attempt; duplicate/late callbacks cannot charge another attempt. */
	void FinishPlanningAreaAttempt(const FString& RecordKey, bool bFailed, bool bCanceled = false);

	/** Updates loaded-chunk admission and retention from shared centers; editor housekeeping uses an advancing clock. */
	void UpdateLoadedChunkPlanning();
	/** Builds the current loaded XY union for existing spacing/continuation influence helpers. */
	void CollectLoadedPlanningBounds(TArray<FIntPoint>& Mins, TArray<FIntPoint>& Maxs) const;
	/** Requires current Created coverage at any LOD and excludes explicit restored terrain at the position. */
	bool IsAutomaticPlanningPositionEligible(FIntVector Position) const;

	/** Counts existing automatic owners, including waiting results and canceled captures awaiting retirement. */
	int32 CountAutomaticPlanningWork() const;

	/** Applies ready work before evicting one farther unstarted or solved-waiting owner. Running groups finish. */
	bool MakeAutomaticPlanningRoom(FIntVector IncomingCenter);

	/** Cancels a whole automatic route and releases unplaced segment payloads, preserving committed capacity. */
	void RetireAutomaticContinuationRoute(FLayoutId RouteId);

	/** Fences and drops one unplaced automatic root; its handle remains counted until worker retirement. */
	void RetireAutomaticRoot(const FString& RecordKey);

	/** Cancels whole automatic root groups without charging area failures. */
	void CancelAutomaticRootWork();

	/** Captures one selected area; empty/excluded areas settle synchronously. Returned work has no UObject reads. */
	TOptional<FLayoutBackgroundSolveSubmission> PreparePlanningAreaDiscovery(
		FIntPoint Area, FIntVector Center, FIntPoint Min, FIntPoint Max, uint64 GroupId);

	/** Selects up to four areas for one worker task, with bounded capture and ordered per-area publication.
	 * Each scan retains its own lifetime, cursor and failure allowance. No completion recursively selects work. */
	void SubmitPlanningAreaDiscovery(const TArray<FIntVector>& Centers);

	/** Cancels the selected batch and returns unfinished coverage without spending its failure allowance. */
	void CancelPlanningAreaDiscovery();

	/** Prunes endpoint and route influence outside loaded bounds plus family reach. Explicit previews retain their endpoints until Apply or Clear. */
	void PruneContinuationEndpointsOutsideExpandedWindows(
		const TArray<FIntPoint>& MinBlockXYs,
		const TArray<FIntPoint>& MaxBlockXYs);

	/** Removes a frozen descriptor payload from the transient store without changing lifecycle metadata. */
	void RemoveFrozenSubmissionDescriptorPayload(FLayoutId DescriptorId);

	/** Tombstones and removes a descriptor payload without clearing region-specific active metadata. */
	void TombstoneFrozenSubmissionDescriptorPayload(FLayoutId DescriptorId);

	/** Returns true when a descriptor id belongs to a canceled or superseded group-lifetime tombstone. */
	bool IsFrozenSubmissionDescriptorTombstoned(FLayoutId DescriptorId) const;

	/** Removes root descriptor payload after accepted publication while preserving the publish lifecycle metadata. */
	void RemovePlanningRootFrozenSubmissionDescriptorPayload(FLayoutId DescriptorId);

	/** Tombstones the requested attempt; an explicit fallback cannot retire a newer descriptor on the same record. */
	void TombstonePlanningRootFrozenSubmissionDescriptor(const FString& StableRecordKey, FLayoutId FallbackDescriptorId = NAME_None);

	/** Rejects the current root attempt and releases pending bookkeeping. Explicit descriptor identity fences stale callbacks. */
	bool RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(const FString& StableRecordKey, const FString& RejectionReason, FLayoutId FallbackDescriptorId = NAME_None, ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None);

	/** Removes connector descriptor payload and metadata together so superseded attempts cannot publish later. */
	void TombstoneConnectorFrozenSubmissionDescriptor(uint64 ConnectorKey, FLayoutId DescriptorId);

	/** Returns the component-owned dispatcher, creating it on the game thread when first needed. */
	FLayoutBackgroundSolveDispatcher& GetOrCreateBackgroundSolveDispatcher();

	/** Builds one current async settings snapshot for dispatcher and future scouting/retry orchestration. */
	FLayoutBackgroundSolveSettings BuildBackgroundSolveSettings() const;

public:

	/** Drains completed background solves and launches eligible queued jobs. */
	void PumpBackgroundLayoutSolves();

	/** Sets the executor to use when creating the background solve dispatcher for testing. */
	void SetLayoutSolveExecutionForTesting(TUniquePtr<ILayoutSolveExecution> Executor);

	/** Disables the sync auto-pump for tests that need to observe pre-pump state
	 *  (e.g. timeout tests that verify solve-budget rejection before the chain runs). */
	void SetDisableAutoPumpForTesting(const bool bDisable);

	/** Submits a root solve through the full lifecycle chain
	 *  (prewarm -> preflight -> solve) into the supplied dispatcher.
	 *  Publishes terminal prewarm/preflight rejection through the solve submission's
	 *  game-thread callback with bWorkSucceeded=false; solve work does not run then.
	 *  When SharedWorkerPacket is provided, the prewarm's structural-contract
	 *  derivation updates the shared packet before the solve work runs. */
	static FLayoutBackgroundSolveHandle SubmitRootSolveViaLifecycleSequencer(
		FLayoutBackgroundSolveDispatcher& Dispatcher,
		const FString& DebugNamePrefix,
		uint64 LayoutGroupId,
		int32 Priority,
		FLayoutWorkerSolvePacket& WorkerSolvePacket,
		FLayoutBackgroundSolveSubmission&& SolveSubmission,
		TSharedPtr<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> SharedWorkerPacket = nullptr);

private:

	/** Converts one planning-window dimension setting into base-block units for runtime sampling. */
	int32 ResolvePlanningWindowDimensionInBlocks(int32 Value, ELayoutPlanningWindowUnit Unit) const;

	/** Admits prepared root data into the existing prewarm/preflight/solve chain on the game thread.
	 * Preparation is outside this chain's deadline; publication keeps frozen descriptor authority. */
	bool SubmitPreparedPlanningRoot(const FPlannedLayoutSiteRecord& PendingRecord,
		const FPlannedLayoutSiteRecord& ExistingRecord, bool bRetryExistingRecord,
		FLayoutFrozenSubmissionDescriptorSeed PreparedInputs, FString DiagnosticContext, int32 Priority);

	/** Consumes captured candidate order on the game thread, rechecking Created coverage and spacing. */
	int32 AdmitPlanningSitesFromWorldBinding(const ULayoutWorldBindingAsset* WorldBinding,
		const LayoutWorldBindingSitePlanner::FSitePlanningSnapshot& Inputs,
		const TArray<FPlannedLayoutSiteRecord>& PendingRecords,
		TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> NoiseSnapshot);

	/** Returns the active ChunkWorld seed when available, otherwise the component fallback seed. */
	int32 ResolveLayoutWorldSeed() const;

	/** Evicts compact exclusions outside candidate footprint/clearance influence, then releases associated
	 * automatic solve payloads and realized metadata. Continuation and explicit preview ownership survive. */
	void PruneRootSpacingReservations(const TArray<FIntPoint>& Mins, const TArray<FIntPoint>& Maxs);

	/** Captures actual solved footprints, not conservative discovery spacing envelopes, across all bindings. */
	TArray<FLayoutRootSpacingReservation> CaptureContinuationRootFootprints() const;

	/** Checks the same binding-wide authored-footprint gap for pending, in-flight and retained placed roots. */
	bool CanPlanOrdinaryRootSiteForBinding(
		const ULayoutWorldBindingAsset* WorldBinding,
		const FLayoutRootSpacingReservation& Bounds,
		const FString& IgnoredStableRecordKey = FString()) const;

	/** Recomputes loaded eligibility only after committed-root or chunk readiness changes. Never upgrades a solved-only root. */
	void RefreshPlacedContinuationRootReadiness();

	/** Tests automatic route ownership and both committed roots' current loaded state; explicit previews keep their own lifetime. */
	bool IsContinuationRouteEligible(FLayoutId RouteId) const;

	/** Cancels only invalid automatic routes, preserving explicit previews and already committed segments. */
	void RetireInvalidAutomaticContinuations(bool bRetireAll = false);

	/** Discovers changed endpoint inputs without replacing active lifecycle groups; explicit requests bypass the dirty check. */
	void RefreshConnectorRecords(bool bForceRefresh = false);

	/** Removes pending request ownership before invoking its callback, allowing safe reentrant submission. */
	void CompleteExplicitContinuationRequest(uint64 ConnectorKey, const FLayoutExplicitContinuationSolveResult& Result);

	/** Registers one reserved root edge with all independently submitted continuation segments. */
	void RegisterContinuationRouteReservation(FLayoutId RouteId, const FString& EdgeKey, const TArray<uint64>& SegmentKeys);

	/** Releases every uncommitted segment reservation owned by one route. */
	void ReleaseContinuationRouteReservation(FLayoutId RouteId);

	/** Releases one uncommitted ledger reservation tracked by a connector record. */
	void ReleaseContinuationReservationForConnector(uint64 ConnectorKey, bool bFailed = false);

	/** Consumes one tracked ledger reservation after connector realization commits. */
	bool ConsumeContinuationReservationForConnector(uint64 ConnectorKey);

	/** Realizes ready sites using current Created authority and per-root stamp integrity. */
	void TryRealizeEligibleSites(bool bStartupOnly = false);

	/**
	 * Converts one world-binding-driven auto-discovered site realization failure into
	 * rejected planning-store backoff so runtime discovery can skip trivial retries
	 * without leaving one dead resolved-site record in the active cache.
	 */
	bool RejectAutoDiscoveredResolvedSiteAfterRealizationFailure(
		const FString& SiteRecordKey,
		const FResolvedLayoutSiteRecord& SiteRecord,
		const FString& RejectionReason);

	/** Realizes ready connectors without consuming eligibility needed by other roots or connectors. */
	void TryRealizeEligibleConnectors(bool bStartupOnly = false);

	/** Returns true when one solved site should only realize after a fresh chunk-creation event. */
	bool RequiresFreshCreatedChunkRealizationGate(const FResolvedLayoutSiteRecord& SiteRecord) const;

	/** Returns true when one solved connector should only realize after a fresh chunk-creation event. */
	bool RequiresFreshCreatedChunkRealizationGate(const FResolvedLayoutConnectorRecord& ConnectorRecord) const;

	/** Every target/support chunk must have current Created authority and no explicit restore mark. */
	bool HasFreshCreatedChunkEligibility(const TSet<FIntVector>& RequiredChunkOrigins) const;

	/** Returns true when every chunk touched by the solved site has already entered the observed loaded-chunk window. */
	bool AreRequiredChunksObserved(const FResolvedLayoutSiteRecord& SiteRecord) const;

	/** Collects the finest-detail chunk origins touched by one solved site. */
	TSet<FIntVector> CollectRequiredChunkOrigins(const FResolvedLayoutSiteRecord& SiteRecord) const;

	/** Returns true when every chunk touched by the solved connector has already entered the observed loaded-chunk window. */
	bool AreRequiredChunksObserved(const FResolvedLayoutConnectorRecord& ConnectorRecord) const;

	/** Collects the finest-detail chunk origins touched by one solved connector. */
	TSet<FIntVector> CollectRequiredChunkOrigins(const FResolvedLayoutConnectorRecord& ConnectorRecord) const;

	/** Stamps through frozen write-plan authority. Standalone records prepare a standard no-terrain-write contract; world placement replays its accepted contract. */
	bool TryRealizeSite(
		FResolvedLayoutSiteRecord& SiteRecord,
		FString* OutFailureReason = nullptr,
		const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride = nullptr);

	/** Stamps one solved connector into the owning chunk world. Emits an optional failure reason when realization cannot proceed. */
	bool TryRealizeConnector(
		FResolvedLayoutConnectorRecord& ConnectorRecord,
		FString* OutFailureReason = nullptr,
		const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride = nullptr);

	/** Returns the owning extension chunk world, or nullptr when the component is detached. */
	class AChunkWorldExtended* GetOwningChunkWorld() const;

	/** Publishes conservative world-space write coverage after an automatic application attempt. */
	void NotifyAutomaticLayoutWriteAttempt(const TSet<FIntVector>& RequiredChunkOrigins);

	/** Prevents reentrant readiness queries from observing a partial game-thread handoff. */
	bool bProcessingQueuedLayoutWork = false;

	/** Queued chunk observations forwarded from worker-thread chunk events. */
	mutable FCriticalSection PendingChunkLoadsMutex;
	TMap<TTuple<int32, FIntVector>, FObservedChunkLoad> PendingChunkLoads;
	// Received count shares the pending-event lock; remaining diagnostic state is game-thread only.
	uint64 ReceivedChunkEvents = 0;
	uint64 ProcessedChunkObservations = 0;
	uint64 LastReportedChunkEvents = 0;
	uint64 LastReportedChunkObservations = 0;
	double NextBookkeepingDiagnosticTime = 0.0;
	FString LastBookkeepingDiagnostic;
	// Game-thread-only, constant-size span aggregates; Detailed Diagnostics prints mean/max every five seconds.
	struct FDiscoveryTiming
	{
		uint64 Calls = 0;
		double TotalMs = 0.0, MaxMs = 0.0;
		void Record(const double Ms) { ++Calls; TotalMs += Ms; MaxMs = FMath::Max(MaxMs, Ms); }
		FString Describe() const { return FString::Printf(TEXT("n=%llu meanMs=%.3f maxMs=%.3f"), Calls, Calls ? TotalMs / Calls : 0.0, MaxMs); }
	};
	FDiscoveryTiming DiscoverySelectionTiming, DiscoveryCaptureTiming, DiscoveryPublicationTiming;
	uint64 CompletedDiscoveryAreas = 0;

	/** Cached deterministic site records keyed by stable root identity. Reservation cells remain site metadata, not cache identity. */
	UPROPERTY(Transient)
	TMap<FString, FResolvedLayoutSiteRecord> ResolvedSiteRecords;

	/** Stable planning-store keys for resolved site records that came from planning-window discovery. */
	TMap<FString, FString> PlanningRecordKeysBySiteRecordKey;

	/** Authored XY exclusions outlive heavy solve payloads only while relevant to candidate coverage. */
	TMap<FString, FLayoutRootSpacingReservation> RootSpacingReservations;

	/** Cached deterministic connector records keyed by stable paired-site identity. Keep this GC-tracked so cached connector carriers survive deferred realization. */
	UPROPERTY(Transient)
	TMap<uint64, FResolvedLayoutConnectorRecord> ResolvedConnectorRecords;

	/** Frozen terrain contracts keyed by root solve id so explicit cached apply can replay accepted terrain/write authority. */
	TMap<FLayoutId, FLayoutFrozenTerrainContract> ResolvedRootFrozenTerrainContracts;

	/** Realization write plans keyed by root solve id so cached apply consumes accepted realization-prep output. */
	TMap<FLayoutId, TSharedPtr<FLayoutRealizationWritePlan>> ResolvedRootRealizationWritePlans;

	/** Frozen terrain contracts keyed by same stable connector identity so continuation realization can replay accepted surface-path terrain writes. */
	TMap<uint64, FLayoutFrozenTerrainContract> ResolvedConnectorFrozenTerrainContracts;

	/** Explicit editor connector keys already committed through the cached apply path. */
	TSet<uint64> ExplicitConnectorKeys;

	/** Accepted editor-preview segment keys held until explicit apply or cancellation. */
	TSet<uint64> ExplicitPreviewConnectorKeys;

	/** Prepared explicit continuation routes awaiting shared lifecycle submission. */
	TMap<uint64, TSharedPtr<FLayoutPreparedContinuationRoute>> PendingExplicitPreparedRoutes;

	/** Compact required-chunk coverage published only by committed roots; reclaimed with retained endpoint/route influence. */
	TMap<FString, TSet<FIntVector>> PlacedContinuationRootChunks;
	TSet<FString> LoadedContinuationRootKeys;
	bool bContinuationReadinessDirty = false;

	/** Route-owned segment data retained until pending segments settle, never for failed-segment retries. */
	TMap<FLayoutId, FLayoutPreparedContinuationRoute> RetainedPreparedContinuationRoutesById;

	/** Prepared geometry keyed by explicit segment runtime identity until aggregate publication completes. */
	TMap<uint64, FLayoutPreparedContinuation> PendingExplicitPreparedSegmentsByConnectorKey;

	/** Segment keys and compact route metadata keyed by explicit root-edge identity. */
	TMap<uint64, TArray<uint64>> ExplicitRouteSegmentKeysByConnectorKey;
	TMap<uint64, FLayoutContinuationRouteRecord> ExplicitRoutesByConnectorKey;

	/** Explicit preview callbacks completed by the shared continuation publication path. */
	TMap<uint64, TFunction<void(const FLayoutExplicitContinuationSolveResult&)>> ExplicitContinuationCompletionCallbacks;

	/** Authoritative planning-window edge reservations keyed by runtime connector identity. */
	TMap<uint64, FString> ContinuationEdgeKeysByConnectorKey;

	/** Route identity keyed by segment runtime connector identity. */
	TMap<uint64, FLayoutId> ContinuationRouteIdsByConnectorKey;

	/** Route-owned reservation state. First committed segment consumes capacity once; valid siblings retain their lifetime. */
	TMap<FLayoutId, FLayoutContinuationRouteReservationState> ContinuationRouteReservations;

	/** Monotonic generation for connector refreshes so late background continuation results cannot republish stale graphs. */
	uint64 ConnectorRefreshGeneration = 0;
	uint64 LastConnectorEndpointRevision = 0;
	bool bRefreshingConnectors = false;
	bool bLastAutomaticContinuationEnabled = false;

	/** Game-thread-scoped priority centers; never retained by worker jobs. */
	TArray<FIntVector> PlanningPriorityCenters;
	TSharedPtr<FLayoutPlanningAreaQueue> PlanningAreaQueue;
	TSharedPtr<FLayoutStartupCoverage> StartupCoverage;
	/** Immutable static discovery inputs; existing input/generation invalidation fences outstanding users. */
	TSharedPtr<const FLayoutDiscoveryInputs, ESPMode::ThreadSafe> CachedDiscoveryInputs;
	/** Rebuilds only when effective native demand changes, using configuration rather than chunk pointers. */
	/** Resolves the coarsest required zero-based layer; authored counts clamp to the running world's layers. */
	int32 GetFirstReadinessDetailLevel() const;
	void RefreshStartupCoverage();
	void InvalidateStartupCoverage();
	FVector GetAutomaticLayoutReachInBlocks() const;
	int32 ComputeAutomaticPlanningPriority(const FIntVector& Center, TConstArrayView<FIntVector> Centers) const;
	TMap<FString, FIntPoint> PlanningAreasByRecordKey;
	struct FPlanningAreaDiscoveryScan
	{
		FIntPoint Area = FIntPoint::ZeroValue;
		FIntVector Center = FIntVector::ZeroValue;
		uint64 ScanId = 0;
	};
	struct FPlanningAreaDiscoveryJob
	{
		TArray<FPlanningAreaDiscoveryScan> Scans;
		FLayoutBackgroundSolveHandle Handle;
	};
	TOptional<FPlanningAreaDiscoveryJob> PendingPlanningAreaDiscovery;
	/** A completed area job lends its already-admitted slot to one root during the game-thread handoff. */
	uint64 CompletingPlanningAreaGroupId = 0;
	TOptional<FIntPoint> ActivePlanningArea;
	bool bPlanningAreaDeferred = false;
	int32 PlanningAreaAdmissions = 0;
	/** Previous retained owner count wakes parked capacity waits only when capacity changes. */
	int32 LastAutomaticWorkCount = 0;
	uint64 EvictedAutomaticWork = 0;

	/** One game-thread snapshot shared by discovery, priority, and retention during a runtime work pass. */
	TOptional<TArray<FIntVector>> PlanningCenterSnapshot;

	/** Automatic root owners, retained through solved-waiting state and canceled-worker retirement. */
	TMap<FString, FLayoutBackgroundSolveHandle> PendingPlanningWindowSolveHandlesByRecordKey;

	/** Creation authority retained with automatic owners through solved-waiting and canceled capture retirement. */
	TMap<FString, FLayoutCreatedChunkIdentity> AutomaticRootCreationOrigins;
	TMap<uint64, FLayoutCreatedChunkIdentity> AutomaticPreparationCreationOrigins;

	/** Captures finest-available Created authority without inventing eligibility from loaded coverage. */
	FLayoutCreatedChunkIdentity CaptureCreatedChunkIdentity(const FIntVector Position) const;
	/** Automatic-only root fence; explicit preview/apply does not consult this ownership. */
	bool IsAutomaticRootOriginCurrent(const FString& RecordKey) const;
	/** Checks the prepared writable cells before automatic segment solving, distinct from support reads. */
	bool IsPreparedContinuationCreatedEligible(const FLayoutPreparedContinuation& Prepared) const;

	/** Planned site centers for queued planning-window root proof jobs keyed by planned-site record key. */
	TMap<FString, FIntVector> PendingPlanningWindowSolveCentersByRecordKey;

	/** Currently queued connector continuation proof jobs, canceled when a newer refresh supersedes them. */
	TArray<FLayoutBackgroundSolveHandle> PendingConnectorRefreshSolveHandles;

	/** Admitted route preparation owns its reservation until cancellation or segment handoff. */
	TMap<uint64, FLayoutBackgroundSolveHandle> PendingAutomaticContinuationPreparations;

	/** Captures resident inputs, then delegates terrain/routing/segment work to the existing dispatcher. */
	void SubmitAutomaticContinuationPreparation(const FResolvedLayoutConnectorRecord& RouteRecord);

	/** Connector descriptor ids keyed by stable connector key for runtime-owned tombstone cleanup. */
	TMap<uint64, FLayoutId> PendingConnectorFrozenSubmissionDescriptorIdsByKey;

	/** Connector descriptor generations keyed by stable connector key for stale lookup rejection. */
	TMap<uint64, uint64> PendingConnectorFrozenSubmissionGenerationsByKey;

	/** Connector descriptor attempt indices keyed by stable connector key for stale lookup rejection. */
	TMap<uint64, int32> PendingConnectorFrozenSubmissionAttemptIndicesByKey;

	/** Connector descriptor audit hashes keyed by stable connector key for stale lookup rejection. */
	TMap<uint64, int32> PendingConnectorFrozenSubmissionAuditHashesByKey;

	/** Descriptor tombstones fenced by the latest admitted job; reclaimed after all potentially stale callbacks retire. */
	TMap<FLayoutId, uint64> FrozenSubmissionTombstoneFences;

	/** Transient pointer-free solve descriptor payloads consumed by submit before worker enqueue. */
	TSharedPtr<FLayoutFrozenSubmissionStore> FrozenSubmissionStore;

	/** Current origin/LOD directory. Created authority is owned here, independently of working-cache admission. */
	TArray<FLayoutLoadedChunkLayer> ObservedChunkLayers;
	/** Base-grid coverage tile size captured with observations; not a planning range or LOD quota. */
	FIntVector ObservedCoverageTileSize = FIntVector::ZeroValue;

	/** Explicit restore evidence and per-root artifact protection by canonical coverage-tile origin. */
	TMap<FIntVector, FLayoutChunkStampMark> StampedChunkOrigins;

#if WITH_AUTOMATION_TESTS
	/** One-shot injected connector used to exercise explicit-apply post-site connector behavior. */
	TOptional<FResolvedLayoutConnectorRecord> ExplicitApplyInjectedConnectorForTesting;

	/** One-shot explicit-apply connector failure reason consumed after the site commit boundary. */
	FString NextExplicitApplyConnectorFailureReasonForTesting;

	/** Optional gate that can hold explicit-preview worker bodies so automation can force superseded completions. */
	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> ExplicitPreviewWorkGateForTesting;
#endif

	/** Most recent layout solve propagation stats, republished when debug stats are enabled after generation. */
	TArray<FDebugGenerationStatsLine> LastDebugGenerationStatsLines;

	/** Last observed property value so details-panel toggles can publish cached stats without requiring a setter call. */
	bool bLastObservedDebugGenerationStats = false;

	/** Sets the executor to use when creating the background solve dispatcher for testing. */

	/** Component-owned durable background layout solve dispatcher. */
	TSharedPtr<FLayoutBackgroundSolveDispatcher> BackgroundSolveDispatcher;

	/** Testing executor injected before the dispatcher is created. */
	TUniquePtr<ILayoutSolveExecution> TestingExecutor;

	/** Event-driven automatic planning and realization maintenance. */
	bool bAutomaticPlanningActive = false;
	bool bLoadedInfluenceDirty = true;
	bool bRealizationDirty = true;
	double LastPlanningWindowUpdateTimeSeconds = TNumericLimits<double>::Lowest();
};
