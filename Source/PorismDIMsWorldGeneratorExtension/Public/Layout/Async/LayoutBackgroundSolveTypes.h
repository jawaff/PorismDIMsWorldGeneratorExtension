// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

/** Priority tier used by the durable background layout-solve dispatcher.
 *  Lower numeric tier = dispatched first.  ExplicitPreview is the highest editor-only
 *  priority so interactive tools are never starved by background pipelines. */
enum class ELayoutBackgroundSolveJobTier : uint8
{
	ExplicitPreview = 0,
	InProgressLayoutGroup = 1,
	ParentResume = 2,
	Continuation = 3,
	NearRoot = 4,
	Warmup = 5
};

/** Pipeline stage carried through the generic dispatcher for prewarm/preflight/solve sequencing diagnostics. */
enum class ELayoutBackgroundSolveLifecycleStage : uint8
{
	Solve,
	ManifestPrewarm,
	AdmissibilityPreflight,
	ParentResume,
	ChildProof,
	/** Immutable placement preparation, before the automatic binding deadline begins. */
	Preparation
};

/** Lifecycle state for one durable background layout-solve job record. */
enum class ELayoutBackgroundSolveJobState : uint8
{
	WaitingForDispatch,
	WaitingForPrerequisites,
	Running,
	CompletedAwaitingPublish,
	PublishedAccepted,
	PublishedRejected,
	CanceledByGroup,
	Expired,
	DiscardedLateCompletion
};

/** Opaque caller handle for one scheduled background layout-solve job. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundSolveHandle
{
	/** Planning generation that owns this job. */
	uint64 GenerationId = 0;

	/** Logical layout group used for cancellation and parent/resume ownership. */
	uint64 LayoutGroupId = 0;

	/** Durable dispatcher job id. */
	uint64 JobId = 0;

	/** Scheduling tier captured at submission time for deterministic draining. */
	ELayoutBackgroundSolveJobTier Tier = ELayoutBackgroundSolveJobTier::Warmup;

	/** Returns true when this handle references one submitted job. */
	bool IsValid() const { return GenerationId != 0 && JobId != 0; }

	/** Clears this handle so late completions cannot be consumed accidentally. */
	void Reset() { *this = FLayoutBackgroundSolveHandle(); }

	friend bool operator==(const FLayoutBackgroundSolveHandle& A, const FLayoutBackgroundSolveHandle& B)
	{
		return A.GenerationId == B.GenerationId
			&& A.LayoutGroupId == B.LayoutGroupId
			&& A.JobId == B.JobId
			&& A.Tier == B.Tier;
	}
};

/** Runtime/editor async solve settings. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundSolveSettings
{
	/** Maximum layout solve tasks allowed to run at once. 0 uses safe CPU-based auto resolution. */
	int32 MaxConcurrentBackgroundLayoutSolves = 0;

	/** Maximum async candidate attempts for one root, child, or continuation group before the scheduler drops that group. */
	int32 MaxLayoutSolveCandidateAttempts = 5;

	/** Resolves 0 to Clamp(LogicalCores - 2, 1, 4); explicit values clamp to at least 1. */
	int32 ResolveMaxConcurrentBackgroundLayoutSolves() const;

	/** Resolves the async candidate-attempt cap to at least 1. */
	int32 ResolveMaxLayoutSolveCandidateAttempts() const;

};

/** Snapshot of dispatcher counters for diagnostics and automation. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundSolveDiagnosticsSnapshot
{
	/** Live retained records, including expired jobs whose workers have not yet completed. */
	int32 RetainedJobRecords = 0;

	/** Occupied execution slots, including canceled/expired workers until their completion is drained. */
	int32 InFlightWorkers = 0;

	int32 Running = 0;
	int32 WaitingForDispatch = 0;
	int32 WaitingForPrerequisites = 0;
	int32 CompletedAwaitingPublish = 0;
	int32 PublishedAccepted = 0;
	int32 PublishedRejected = 0;
	int32 Canceled = 0;
	int32 Expired = 0;
	int32 DiscardedLateCompletions = 0;

	/** Number of distinct LayoutGroupIds that have at least one Running job. */
	int32 InProgressLayoutGroups = 0;

	/** Formats one compact stats line for the existing generation-stats screen. */
	FString ToDebugString() const;
};


/** Worker callback for one immutable background layout-solve job. */
using FLayoutBackgroundSolveWork = TFunction<bool(const FLayoutSolveCancellationToken& CancellationToken, FString& OutFailureReason)>;

/** Game-thread publication callback for one completed background layout-solve job. */
using FLayoutBackgroundSolvePublish = TFunction<void(const struct FLayoutBackgroundSolveCompletion& Completion)>;

/** Submission payload copied into the durable dispatcher job table. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundSolveSubmission
{
	FString DebugName;
	uint64 LayoutGroupId = 0;
	ELayoutBackgroundSolveJobTier Tier = ELayoutBackgroundSolveJobTier::Warmup;
	ELayoutBackgroundSolveLifecycleStage LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Solve;
	int32 Priority = 0;
	TSharedPtr<FLayoutSolveCancellationSource, ESPMode::ThreadSafe> CancellationSource;
	FLayoutBackgroundSolveWork Work;
	FLayoutBackgroundSolvePublish PublishOnGameThread;
};

/** Completion envelope produced by workers and drained deterministically on the game thread. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundSolveCompletion
{
	FLayoutBackgroundSolveHandle Handle;
	FString DebugName;
	FString FailureReason;
	uint64 SubmitSequence = 0;
	int32 Priority = 0;
	ELayoutBackgroundSolveLifecycleStage LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Solve;
	bool bWorkSucceeded = false;
	bool bCanceled = false;
	FLayoutBackgroundSolvePublish PublishOnGameThread;
};

struct FSharedCompletionState
	{
		FCriticalSection Mutex;
		TArray<FLayoutBackgroundSolveCompletion> Completions;
		FThreadSafeBool bAcceptCompletions = true;

		void EnqueueCompletion(FLayoutBackgroundSolveCompletion&& Completion)
		{
			if (!bAcceptCompletions)
			{
				return;
			}
			FScopeLock Lock(&Mutex);
			Completions.Add(MoveTemp(Completion));
		}

		TArray<FLayoutBackgroundSolveCompletion> Drain()
		{
			FScopeLock Lock(&Mutex);
			TArray<FLayoutBackgroundSolveCompletion> Result;
			Swap(Result, Completions);
			return Result;
		}
	};
