// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

class ILayoutSolveExecution;

/** Durable UE::Tasks dispatcher for all production layout solve/proof work. */

class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundSolveDispatcher
{
public:
	/** Creates one dispatcher using the supplied settings. */
	explicit FLayoutBackgroundSolveDispatcher(const FLayoutBackgroundSolveSettings& InSettings);

	/** Cancels all known work and prevents future late completions from publishing through this instance. */
	~FLayoutBackgroundSolveDispatcher();

	/** Updates concurrency settings; existing running jobs continue and future launches use the new cap. */
	void SetSettings(const FLayoutBackgroundSolveSettings& InSettings);

	/** Submits one immutable worker job into the durable table and attempts dispatch. */
	FLayoutBackgroundSolveHandle Submit(FLayoutBackgroundSolveSubmission&& Submission);

	/** Drains worker completions, publishes valid results, then launches eligible waiting jobs. */
	void Tick();

	/** Requests cancellation for one handle. Waiting jobs are marked canceled; running jobs observe their token. */
	void Cancel(const FLayoutBackgroundSolveHandle& Handle);

	/** Requests cancellation for every waiting/running job in one logical layout group. */
	void CancelGroup(uint64 LayoutGroupId);

	/** Ends a world lifetime: discard all callbacks, cancel workers and retain their slots until completion. Job ids remain monotonic. */
	void CancelAll();

	/** Includes successor stages and completions awaiting publication, even after the original handle retires. */
	bool HasPendingGroup(uint64 LayoutGroupId) const;

	/** Includes canceled workers and publication captures until the entire group retires. */
	bool HasRetainedGroup(uint64 LayoutGroupId) const;

	/** True while a group owns an executing worker; ordinary cache pressure must not cancel it. */
	bool HasRunningGroup(uint64 LayoutGroupId) const;

	/** Latest submitted job id, used to fence external cleanup against callbacks already admitted here. */
	uint64 GetLatestSubmittedJobId() const { return NextJobId - 1; }

	/** All job ids at or below this value have retired, including their workers and publication callbacks. */
	uint64 GetRetiredJobWatermark() const;

	/** Refreshes priorities of queued stages in matching groups; running jobs and tier ordering remain unchanged. */
	void UpdateWaitingGroupPriorities(const TMap<uint64, int32>& Priorities);

	/** Marks one waiting job as prerequisite-blocked so it no longer occupies dispatch eligibility. */
	void MarkWaitingForPrerequisites(const FLayoutBackgroundSolveHandle& Handle);

	/** Marks one prerequisite-blocked job as dispatch-eligible again. */
	void MarkPrerequisitesSatisfied(const FLayoutBackgroundSolveHandle& Handle);

	/** Expires one waiting/running job that left its owning planning window. */
	void Expire(const FLayoutBackgroundSolveHandle& Handle);

	/** Returns live queue/worker counts and cumulative terminal counts without retaining finished job payloads. */
	FLayoutBackgroundSolveDiagnosticsSnapshot GetDiagnosticsSnapshot() const;

	/** Injects a custom execution strategy. Must be called before first Tick. */
	void SetExecution(TUniquePtr<ILayoutSolveExecution> InExecution);

	/** Disables the synchronous auto-pump loop in Submit(). Use when a test
	 *  relies on the solve budget timeout to reject before the lifecycle chain
	 *  completes (e.g. RejectsSolveWhenBindingTimeoutIsExceeded). */
	void SetDisableAutoPumpForTesting(const bool bDisable) { bDisableAutoPump = bDisable; }

	/** Lock-free completion-state queue shared with async executor. */

private:

	/** Game-thread record retained only while queued, publishing, or awaiting an executing worker's completion. */
	struct FJobRecord
	{
		FLayoutBackgroundSolveHandle Handle;
		FString DebugName;
		uint64 SubmitSequence = 0;
		int32 Priority = 0;
		ELayoutBackgroundSolveLifecycleStage LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Solve;
		ELayoutBackgroundSolveJobState State = ELayoutBackgroundSolveJobState::WaitingForDispatch;
		/** Expiration changes State immediately but cannot release the worker slot before completion arrives. */
		bool bWorkerInFlight = false;
		TSharedPtr<FLayoutSolveCancellationSource, ESPMode::ThreadSafe> CancellationSource;
		FLayoutBackgroundSolveWork Work;
		FLayoutBackgroundSolvePublish PublishOnGameThread;
	};

	/** Launches eligible waiting jobs up to the configured concurrency cap. */
	void DispatchEligibleJobs();

	/** Publishes on the game thread. Callbacks may submit more jobs: own each callback
	 * across invocation and reacquire its record before settlement because JobTable can relocate.
	 */
	void DrainCompletions();

	/** Releases terminal records/captures, preserving only saturated diagnostic counters. In-flight workers stay tracked. */
	void RetireTerminalJobs();

	/** Returns true if a job state is still waiting or running. */
	static bool IsCancelableState(ELayoutBackgroundSolveJobState State);

	FLayoutBackgroundSolveSettings Settings;
	TMap<uint64, FJobRecord> JobTable;
	TUniquePtr<ILayoutSolveExecution> Execution;
	TSharedRef<FSharedCompletionState, ESPMode::ThreadSafe> SharedCompletionState;
	uint64 NextGenerationId = 1;
	uint64 NextJobId = 1;
	uint64 NextSubmitSequence = 1;
	int32 DiscardedLateCompletionCount = 0;
	FLayoutBackgroundSolveDiagnosticsSnapshot RetiredDiagnostics;
	bool bDisableAutoPump = false;
};
