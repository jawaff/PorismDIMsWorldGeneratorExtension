// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutSolveExecution.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

#include "Async/TaskGraphInterfaces.h"

namespace
{
	void IncrementTerminalCount(int32& Count)
	{
		if (Count < MAX_int32)
		{
			++Count;
		}
	}
}

FLayoutBackgroundSolveDispatcher::FLayoutBackgroundSolveDispatcher(const FLayoutBackgroundSolveSettings& InSettings)
	: Settings(InSettings)
	, SharedCompletionState(MakeShared<FSharedCompletionState, ESPMode::ThreadSafe>())
{
	Execution = CreateAsyncLayoutSolveExecution(SharedCompletionState);
}

FLayoutBackgroundSolveDispatcher::~FLayoutBackgroundSolveDispatcher()
{
	SharedCompletionState->bAcceptCompletions = false;
	for (TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		if (Pair.Value.CancellationSource.IsValid())
		{
			Pair.Value.CancellationSource->Cancel();
		}
	}
}

void FLayoutBackgroundSolveDispatcher::SetSettings(const FLayoutBackgroundSolveSettings& InSettings)
{
	Settings = InSettings;
}

void FLayoutBackgroundSolveDispatcher::SetExecution(TUniquePtr<ILayoutSolveExecution> InExecution)
{
	Execution = MoveTemp(InExecution);
}

FLayoutBackgroundSolveHandle FLayoutBackgroundSolveDispatcher::Submit(FLayoutBackgroundSolveSubmission&& Submission)
{
	check(IsInGameThread());
	if (!Submission.Work)
	{
		return FLayoutBackgroundSolveHandle();
	}

	RetireTerminalJobs();
	FJobRecord Record;
	Record.Handle.GenerationId = NextGenerationId;
	Record.Handle.LayoutGroupId = Submission.LayoutGroupId;
	Record.Handle.JobId = NextJobId++;
	Record.Handle.Tier = Submission.Tier;
	Record.DebugName = MoveTemp(Submission.DebugName);
	Record.SubmitSequence = NextSubmitSequence++;
	Record.Priority = Submission.Priority;
	Record.LifecycleStage = Submission.LifecycleStage;
	Record.CancellationSource = Submission.CancellationSource.IsValid()
		? Submission.CancellationSource
		: MakeShared<FLayoutSolveCancellationSource, ESPMode::ThreadSafe>();
	Record.Work = MoveTemp(Submission.Work);
	Record.PublishOnGameThread = MoveTemp(Submission.PublishOnGameThread);

	const FLayoutBackgroundSolveHandle Handle = Record.Handle;
	JobTable.Add(Handle.JobId, MoveTemp(Record));
	DispatchEligibleJobs();

	// When the executor is synchronous, drain the full lifecycle chain inside Submit()
	// so callers see accepted/rejected outcomes without relying on editor ticks or manual
	// PumpBackgroundLayoutSolves.  The loop runs Tick() until the dispatcher is idle,
	// bounded by a fixed iteration cap as a safety valve.
	if (Execution.IsValid() && Execution->IsSynchronous() && !bDisableAutoPump)
	{
		for (int32 AutoPumpIteration = 0; AutoPumpIteration < 1000; ++AutoPumpIteration)
		{
			Tick();
			const FLayoutBackgroundSolveDiagnosticsSnapshot Diag = GetDiagnosticsSnapshot();
			if (Diag.Running == 0
				&& Diag.WaitingForDispatch == 0
				&& Diag.WaitingForPrerequisites == 0
				&& Diag.CompletedAwaitingPublish == 0)
			{
				break;
			}
		}
	}
	return Handle;
}

void FLayoutBackgroundSolveDispatcher::Tick()
{
	check(IsInGameThread());
	Execution->ProcessCompletions();
	DrainCompletions();
	RetireTerminalJobs();
	DispatchEligibleJobs();
	RetireTerminalJobs();
}

void FLayoutBackgroundSolveDispatcher::Cancel(const FLayoutBackgroundSolveHandle& Handle)
{
	check(IsInGameThread());
	FJobRecord* const Record = JobTable.Find(Handle.JobId);
	if (Record == nullptr || Record->Handle.GenerationId != Handle.GenerationId)
	{
		return;
	}

	if (Record->CancellationSource.IsValid())
	{
		Record->CancellationSource->Cancel();
	}

	if (Record->State == ELayoutBackgroundSolveJobState::WaitingForDispatch
		|| Record->State == ELayoutBackgroundSolveJobState::WaitingForPrerequisites)
	{
		Record->State = ELayoutBackgroundSolveJobState::CanceledByGroup;
	}
	RetireTerminalJobs();
}

void FLayoutBackgroundSolveDispatcher::CancelGroup(const uint64 LayoutGroupId)
{
	check(IsInGameThread());
	for (TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		FJobRecord& Record = Pair.Value;
		if (Record.Handle.LayoutGroupId != LayoutGroupId || !IsCancelableState(Record.State))
		{
			continue;
		}

		if (Record.CancellationSource.IsValid())
		{
			Record.CancellationSource->Cancel();
		}

		if (Record.State == ELayoutBackgroundSolveJobState::WaitingForDispatch
			|| Record.State == ELayoutBackgroundSolveJobState::WaitingForPrerequisites)
		{
			Record.State = ELayoutBackgroundSolveJobState::CanceledByGroup;
		}
	}
	RetireTerminalJobs();
}

void FLayoutBackgroundSolveDispatcher::CancelAll()
{
	check(IsInGameThread());
	for (TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		FJobRecord& Record = Pair.Value;
		if (Record.CancellationSource.IsValid())
		{
			Record.CancellationSource->Cancel();
		}
		Record.State = ELayoutBackgroundSolveJobState::CanceledByGroup;
		Record.PublishOnGameThread = nullptr;
		Record.Work = nullptr;
	}
	RetireTerminalJobs();
}

bool FLayoutBackgroundSolveDispatcher::HasPendingGroup(const uint64 LayoutGroupId) const
{
	check(IsInGameThread());
	for (const TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		const FJobRecord& Record = Pair.Value;
		if (Record.Handle.LayoutGroupId == LayoutGroupId
			&& (IsCancelableState(Record.State) || Record.State == ELayoutBackgroundSolveJobState::CompletedAwaitingPublish))
		{
			return true;
		}
	}
	return false;
}

bool FLayoutBackgroundSolveDispatcher::HasRetainedGroup(const uint64 LayoutGroupId) const
{
	check(IsInGameThread());
	for (const auto& Pair : JobTable)
		if (Pair.Value.Handle.LayoutGroupId == LayoutGroupId) return true;
	return false;
}

bool FLayoutBackgroundSolveDispatcher::HasRunningGroup(const uint64 LayoutGroupId) const
{
	check(IsInGameThread());
	for (const auto& Pair : JobTable)
		if (Pair.Value.Handle.LayoutGroupId == LayoutGroupId && Pair.Value.bWorkerInFlight) return true;
	return false;
}

uint64 FLayoutBackgroundSolveDispatcher::GetRetiredJobWatermark() const
{
	check(IsInGameThread());
	uint64 Watermark = GetLatestSubmittedJobId();
	for (const auto& Pair : JobTable) Watermark = FMath::Min(Watermark, Pair.Key - 1);
	return Watermark;
}

void FLayoutBackgroundSolveDispatcher::UpdateWaitingGroupPriorities(const TMap<uint64, int32>& Priorities)
{
	check(IsInGameThread());
	for (TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		FJobRecord& Record = Pair.Value;
		if (Record.State == ELayoutBackgroundSolveJobState::WaitingForDispatch
			|| Record.State == ELayoutBackgroundSolveJobState::WaitingForPrerequisites)
		{
			if (const int32* Priority = Priorities.Find(Record.Handle.LayoutGroupId))
			{
				Record.Priority = *Priority;
			}
		}
	}
}

void FLayoutBackgroundSolveDispatcher::MarkWaitingForPrerequisites(const FLayoutBackgroundSolveHandle& Handle)
{
	check(IsInGameThread());
	if (FJobRecord* const Record = JobTable.Find(Handle.JobId))
	{
		if (Record->Handle.GenerationId == Handle.GenerationId
			&& Record->State == ELayoutBackgroundSolveJobState::WaitingForDispatch)
		{
			Record->State = ELayoutBackgroundSolveJobState::WaitingForPrerequisites;
		}
	}
}

void FLayoutBackgroundSolveDispatcher::MarkPrerequisitesSatisfied(const FLayoutBackgroundSolveHandle& Handle)
{
	check(IsInGameThread());
	if (FJobRecord* const Record = JobTable.Find(Handle.JobId))
	{
		if (Record->Handle.GenerationId == Handle.GenerationId
			&& Record->State == ELayoutBackgroundSolveJobState::WaitingForPrerequisites)
		{
			Record->State = ELayoutBackgroundSolveJobState::WaitingForDispatch;
		}
	}
}

void FLayoutBackgroundSolveDispatcher::Expire(const FLayoutBackgroundSolveHandle& Handle)
{
	check(IsInGameThread());
	FJobRecord* const Record = JobTable.Find(Handle.JobId);
	if (Record == nullptr || Record->Handle.GenerationId != Handle.GenerationId || !IsCancelableState(Record->State))
	{
		return;
	}
	if (Record->CancellationSource.IsValid())
	{
		Record->CancellationSource->Cancel();
	}
	Record->State = ELayoutBackgroundSolveJobState::Expired;
	Record->PublishOnGameThread = nullptr;
	RetireTerminalJobs();
}

FLayoutBackgroundSolveDiagnosticsSnapshot FLayoutBackgroundSolveDispatcher::GetDiagnosticsSnapshot() const
{
	FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = RetiredDiagnostics;
	Snapshot.RetainedJobRecords = JobTable.Num();
	TSet<uint64> RunningGroupIds;
	for (const TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		Snapshot.InFlightWorkers += Pair.Value.bWorkerInFlight ? 1 : 0;
		switch (Pair.Value.State)
		{
		case ELayoutBackgroundSolveJobState::WaitingForDispatch:
			++Snapshot.WaitingForDispatch;
			break;
		case ELayoutBackgroundSolveJobState::WaitingForPrerequisites:
			++Snapshot.WaitingForPrerequisites;
			break;
		case ELayoutBackgroundSolveJobState::Running:
			++Snapshot.Running;
			RunningGroupIds.Add(Pair.Value.Handle.LayoutGroupId);
			break;
		case ELayoutBackgroundSolveJobState::CompletedAwaitingPublish:
			++Snapshot.CompletedAwaitingPublish;
			break;
		case ELayoutBackgroundSolveJobState::PublishedAccepted:
			IncrementTerminalCount(Snapshot.PublishedAccepted);
			break;
		case ELayoutBackgroundSolveJobState::PublishedRejected:
			IncrementTerminalCount(Snapshot.PublishedRejected);
			break;
		case ELayoutBackgroundSolveJobState::CanceledByGroup:
			IncrementTerminalCount(Snapshot.Canceled);
			break;
		case ELayoutBackgroundSolveJobState::Expired:
			IncrementTerminalCount(Snapshot.Expired);
			break;
		case ELayoutBackgroundSolveJobState::DiscardedLateCompletion:
			IncrementTerminalCount(Snapshot.DiscardedLateCompletions);
			break;
		}
	}
	Snapshot.DiscardedLateCompletions = static_cast<int32>(FMath::Min<int64>(
		MAX_int32, static_cast<int64>(Snapshot.DiscardedLateCompletions) + DiscardedLateCompletionCount));
	Snapshot.InProgressLayoutGroups = RunningGroupIds.Num();
	return Snapshot;
}

void FLayoutBackgroundSolveDispatcher::DispatchEligibleJobs()
{
	const int32 MaxConcurrent = Settings.ResolveMaxConcurrentBackgroundLayoutSolves();
	int32 RunningCount = 0;
	int32 WaitingCount = 0;
	int32 CompletedCount = 0;
	TArray<FJobRecord*> EligibleRecords;
	for (TPair<uint64, FJobRecord>& Pair : JobTable)
	{
		FJobRecord& Record = Pair.Value;
		if (Record.bWorkerInFlight)
		{
			++RunningCount;
		}
		else if (Record.State == ELayoutBackgroundSolveJobState::WaitingForDispatch)
		{
			++WaitingCount;
			EligibleRecords.Add(&Record);
		}
		else if (Record.State == ELayoutBackgroundSolveJobState::CompletedAwaitingPublish)
		{
			++CompletedCount;
		}
	}

	int32 AvailableSlots = FMath::Max(0, MaxConcurrent - RunningCount);

	if (AvailableSlots <= 0 || EligibleRecords.IsEmpty())
	{
		return;
	}

	EligibleRecords.Sort([](const FJobRecord& A, const FJobRecord& B)
	{
		if (A.Handle.Tier != B.Handle.Tier)
		{
			return static_cast<uint8>(A.Handle.Tier) < static_cast<uint8>(B.Handle.Tier);
		}
		if (A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return A.SubmitSequence < B.SubmitSequence;
	});

	// Chain-completion bias: when a lifecycle chain already has a Running job,
	// prefer waiting jobs from that same chain (LayoutGroupId) over new chains
	// so one solve's prewarm→preflight→solve completes before the next starts.
	{
		TSet<uint64> RunningGroupIds;
		for (TPair<uint64, FJobRecord>& Pair : JobTable)
		{
			if (Pair.Value.State == ELayoutBackgroundSolveJobState::Running)
			{
				RunningGroupIds.Add(Pair.Value.Handle.LayoutGroupId);
			}
		}
		if (RunningGroupIds.Num() > 0)
		{
			EligibleRecords.StableSort([&RunningGroupIds](const FJobRecord& A, const FJobRecord& B)
			{
				if (A.Handle.Tier != B.Handle.Tier)
				{
					return static_cast<uint8>(A.Handle.Tier) < static_cast<uint8>(B.Handle.Tier);
				}
				const bool bAInProgress = RunningGroupIds.Contains(A.Handle.LayoutGroupId);
				const bool bBInProgress = RunningGroupIds.Contains(B.Handle.LayoutGroupId);
				if (bAInProgress != bBInProgress)
				{
					return bAInProgress;  // in-progress chains sort before new chains
				}
				if (A.Priority != B.Priority)
				{
					return A.Priority > B.Priority;
				}
				return A.SubmitSequence < B.SubmitSequence;
			});
		}
	}

	for (FJobRecord* const Record : EligibleRecords)
	{
		if (AvailableSlots <= 0)
		{
			break;
		}

		if (Record == nullptr || Record->State != ELayoutBackgroundSolveJobState::WaitingForDispatch)
		{
			continue;
		}

		if (Record->CancellationSource.IsValid() && Record->CancellationSource->IsCancellationRequested())
		{
			Record->State = ELayoutBackgroundSolveJobState::CanceledByGroup;
			continue;
		}

		Record->State = ELayoutBackgroundSolveJobState::Running;
		Record->bWorkerInFlight = true;
		--AvailableSlots;

		const FLayoutBackgroundSolveHandle Handle = Record->Handle;
		const FString DebugName = Record->DebugName;
		const uint64 SubmitSequence = Record->SubmitSequence;
		const int32 Priority = Record->Priority;
		const ELayoutBackgroundSolveLifecycleStage LifecycleStage = Record->LifecycleStage;
		FLayoutBackgroundSolveWork UnprofiledWork = MoveTemp(Record->Work);
		const FLayoutBackgroundSolveWork Work = [UnprofiledWork = MoveTemp(UnprofiledWork), DebugName](
			const FLayoutSolveCancellationToken& Token,
			FString& FailureReason)
		{
			SCOPED_NAMED_EVENT_FSTRING(DebugName, FColor::Cyan);
			return UnprofiledWork(Token, FailureReason);
		};
		const TSharedPtr<FLayoutSolveCancellationSource, ESPMode::ThreadSafe> CancellationSource = Record->CancellationSource;
		const FLayoutSolveCancellationToken CancellationToken = CancellationSource.IsValid()
			? CancellationSource->CreateToken()
			: FLayoutSolveCancellationToken();

		Execution->Enqueue(
			Work,
			CancellationToken,
			[CompletionState = SharedCompletionState, Handle, DebugName, SubmitSequence, Priority, LifecycleStage](
				FLayoutBackgroundSolveCompletion Completion) mutable
			{
				Completion.Handle = Handle;
				Completion.DebugName = DebugName;
				Completion.SubmitSequence = SubmitSequence;
				Completion.Priority = Priority;
				Completion.LifecycleStage = LifecycleStage;
				CompletionState->EnqueueCompletion(MoveTemp(Completion));
			});
	}
}

void FLayoutBackgroundSolveDispatcher::DrainCompletions()
{
	TArray<FLayoutBackgroundSolveCompletion> Completions = SharedCompletionState->Drain();
	if (Completions.IsEmpty())
	{
		return;
	}

	Completions.Sort([](const FLayoutBackgroundSolveCompletion& A, const FLayoutBackgroundSolveCompletion& B)
	{
		if (A.Handle.Tier != B.Handle.Tier)
		{
			return static_cast<uint8>(A.Handle.Tier) < static_cast<uint8>(B.Handle.Tier);
		}
		if (A.Priority != B.Priority)
		{
			return A.Priority > B.Priority;
		}
		return A.SubmitSequence < B.SubmitSequence;
	});

	for (const FLayoutBackgroundSolveCompletion& Completion : Completions)
	{
		FJobRecord* const Record = JobTable.Find(Completion.Handle.JobId);
		if (Record == nullptr || Record->Handle.GenerationId != Completion.Handle.GenerationId)
		{
			IncrementTerminalCount(DiscardedLateCompletionCount);
			continue;
		}
		Record->bWorkerInFlight = false;
		if (Record->State != ELayoutBackgroundSolveJobState::Running)
		{
			IncrementTerminalCount(DiscardedLateCompletionCount);
			continue;
		}

		Record->State = ELayoutBackgroundSolveJobState::CompletedAwaitingPublish;
		if (Completion.bCanceled
			|| (Record->CancellationSource.IsValid() && Record->CancellationSource->IsCancellationRequested()))
		{
			Record->State = ELayoutBackgroundSolveJobState::CanceledByGroup;
			continue;
		}


		// Lifecycle publication may submit jobs and relocate JobTable, including this
		// record and its callable. Own the callback across reentry; never dereference
		// Record afterward or overwrite a state settled by reentrant processing.
		FLayoutBackgroundSolvePublish Publish = MoveTemp(Record->PublishOnGameThread);
		if (Publish)
		{
			Publish(Completion);
		}
		FJobRecord* const CurrentRecord = JobTable.Find(Completion.Handle.JobId);
		if (CurrentRecord == nullptr
			|| CurrentRecord->Handle.GenerationId != Completion.Handle.GenerationId
			|| CurrentRecord->State != ELayoutBackgroundSolveJobState::CompletedAwaitingPublish)
		{
			continue;
		}

		CurrentRecord->State = Completion.bWorkSucceeded
			? ELayoutBackgroundSolveJobState::PublishedAccepted
			: ELayoutBackgroundSolveJobState::PublishedRejected;

	}
}

void FLayoutBackgroundSolveDispatcher::RetireTerminalJobs()
{
	for (auto It = JobTable.CreateIterator(); It; ++It)
	{
		const FJobRecord& Record = It.Value();
		if (Record.bWorkerInFlight)
		{
			continue;
		}
		switch (Record.State)
		{
		case ELayoutBackgroundSolveJobState::PublishedAccepted:
			IncrementTerminalCount(RetiredDiagnostics.PublishedAccepted);
			break;
		case ELayoutBackgroundSolveJobState::PublishedRejected:
			IncrementTerminalCount(RetiredDiagnostics.PublishedRejected);
			break;
		case ELayoutBackgroundSolveJobState::CanceledByGroup:
			IncrementTerminalCount(RetiredDiagnostics.Canceled);
			break;
		case ELayoutBackgroundSolveJobState::Expired:
			IncrementTerminalCount(RetiredDiagnostics.Expired);
			break;
		case ELayoutBackgroundSolveJobState::DiscardedLateCompletion:
			IncrementTerminalCount(RetiredDiagnostics.DiscardedLateCompletions);
			break;
		default:
			continue;
		}
		It.RemoveCurrent();
	}
}

bool FLayoutBackgroundSolveDispatcher::IsCancelableState(const ELayoutBackgroundSolveJobState State)
{
	return State == ELayoutBackgroundSolveJobState::WaitingForDispatch
		|| State == ELayoutBackgroundSolveJobState::WaitingForPrerequisites
		|| State == ELayoutBackgroundSolveJobState::Running;
}
