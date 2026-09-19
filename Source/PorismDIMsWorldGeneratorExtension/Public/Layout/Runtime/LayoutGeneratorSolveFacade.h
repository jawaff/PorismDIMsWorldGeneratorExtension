// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutTypes.h"

class UChunkWorldLayoutRuntimeComponent;
class ULayoutProfileAsset;
class ULayoutWorldBindingAsset;
struct FLayoutRegionContentEntry;

/** Tracks one async generator preview request so superseded completions cannot publish stale data. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorSolveAsyncState
{
	/** Latest submitted background solve handle. */
	FLayoutBackgroundSolveHandle ActiveHandle;

	/** Monotonic preview generation used for latest-only publication. */
	uint64 ActiveGeneration = 0;
};

/** Explicit layout-generator solve request routed through the shared runtime contract path. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorSolveRequest
{
	/** Runtime component that owns current world bindings, chunk world access, and realization cache policy. */
	UChunkWorldLayoutRuntimeComponent* RuntimeComponent = nullptr;

	/** Requested block-world site center. Binding-aware calls may snap this to the lattice before solving. */
	FIntVector RequestedSiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Optional world binding whose placement/terrain policy should drive this explicit solve. */
	const ULayoutWorldBindingAsset* WorldBinding = nullptr;

	/** Explicit root profile selected by editor or generator tooling. */
	ULayoutProfileAsset* LayoutProfile = nullptr;

	/** Deterministic solve seed requested by the tool. */
	int32 SolveSeed = 0;
};

/** Explicit layout-generator continuation request selected from two exported endpoints. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorContinuationSolveRequest
{
	/** Runtime component that owns endpoint caches, background lifecycle, and realization. */
	UChunkWorldLayoutRuntimeComponent* RuntimeComponent = nullptr;

	/** User-selected exported continuation endpoints. */
	FResolvedLayoutConnectorEndpoint StartEndpoint;
	FResolvedLayoutConnectorEndpoint EndEndpoint;

	/** Binding and continuation profile selected by Layout Generator. */
	const ULayoutWorldBindingAsset* WorldBinding = nullptr;
	ULayoutProfileAsset* ContinuationProfile = nullptr;

	/** Deterministic tool solve seed. */
	int32 SolveSeed = 0;
};

/** Explicit layout-generator solve result plus frozen-terrain artifact replayed by preview/apply. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorSolveResult
{
	/** True when the runtime solve succeeded. */
	bool bSucceeded = false;

	/** Stable failure reason when request construction or solving fails. */
	FString FailureReason;

	/** Solved site record returned by the runtime component. */
	FResolvedLayoutSiteRecord SiteRecord;

	/** Full schedule result returned by low-level region solving. */
	FLayoutRegionSolveScheduleResult ScheduleResult;

	/** Frozen terrain artifact replayed by preview/apply/runtime without reopening terrain-fit decisions. */
	FLayoutFrozenTerrainContract FrozenTerrainContract;

	/** Total wall-clock solve time in seconds. */
	double TotalSolveSeconds = 0.0;
};

/** One bounded continuation segment returned to Layout Generator preview/apply. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorContinuationSegmentSolveResult
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

/** Explicit two-endpoint continuation preview result returned by shared runtime lifecycle. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorContinuationSolveResult
{
	/** Route metadata and independently solved segment artifacts. */
	FLayoutContinuationRouteRecord Route;
	TArray<FLayoutGeneratorContinuationSegmentSolveResult> Segments;

	/** True when preparation, prewarm, preflight, and solve all accepted the continuation. */
	bool bSucceeded = false;

	/** Stable failure reason from endpoint validation, preparation, or async solve. */
	FString FailureReason;

	/** True when retained placements are safe to preview and explicitly apply, but full solve rejected. */
	bool bIsPartial = false;

	/** Total wall-clock solve time in seconds. */
	double TotalSolveSeconds = 0.0;
};

/** Facade that lets editor/generator tooling call shared explicit-root and continuation solve pathways. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutGeneratorSolveFacade
{
public:
	/** Resolves a result's frozen asset identity for editor consumers without exposing runtime internals.
	 * Returns null for changed or ambiguous carriers; frozen templates remain authoritative.
	 */
	static const FLayoutRegionContentEntry* FindFrozenPlacementSourceEntry(
		const ULayoutRegionContentSetAsset* ContentSet, const FLayoutPlacedModule& Placement);

	/** Enqueues one explicit root solve and publishes the result on the game thread without blocking. */
	static FLayoutBackgroundSolveHandle SubmitExplicitRoot(
		const FLayoutGeneratorSolveRequest& Request,
		TFunction<void(const FLayoutGeneratorSolveResult& Result)> OnCompleted,
		FLayoutGeneratorSolveAsyncState* AsyncState = nullptr);

	/** Enqueues one explicit two-endpoint continuation solve through the shared runtime lifecycle. */
	static FLayoutBackgroundSolveHandle SubmitExplicitContinuation(
		const FLayoutGeneratorContinuationSolveRequest& Request,
		TFunction<void(const FLayoutGeneratorContinuationSolveResult& Result)> OnCompleted,
		FLayoutGeneratorSolveAsyncState* AsyncState = nullptr);

	/** Cancels one in-flight async generator solve through its owning runtime dispatcher. */
	static void CancelSubmittedSolve(
		UChunkWorldLayoutRuntimeComponent* RuntimeComponent,
		FLayoutGeneratorSolveAsyncState& AsyncState);
};
