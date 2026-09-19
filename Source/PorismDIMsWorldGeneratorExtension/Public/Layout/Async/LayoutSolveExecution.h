// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

/** Polymorphic execution strategy for layout background-solve work. */
class PORISMDIMSWORLDGENERATOREXTENSION_API ILayoutSolveExecution
{
public:
	virtual ~ILayoutSolveExecution() = default;

	/** Enqueue work. OnComplete fires from the calling thread during ProcessCompletions.
	 *  The executor wraps Work in try/catch so unhandled exceptions are never silently
	 *  swallowed — they are reported through OnComplete with bWorkSucceeded=false. */
	virtual void Enqueue(
		FLayoutBackgroundSolveWork Work,
		FLayoutSolveCancellationToken CancellationToken,
		TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete) = 0;

	/** Fire pending OnComplete callbacks on the calling thread.
	 *  Async: no-op (completions drain through the existing shared completion-state queue).
	 *  Sync:  runs all queued work items inline and fires callbacks immediately. */
	virtual void ProcessCompletions() = 0;

	/** Returns true when this executor processes work synchronously on the calling thread.
	 *  Synchronous executors can safely drain the full lifecycle chain inside Submit() without
	 *  blocking on background threads or spinning in a busy-wait loop. */
	virtual bool IsSynchronous() const { return false; }
};

/** Synchronous executor that runs work inline during ProcessCompletions — no threads, no blocking. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FSynchronousLayoutSolveExecution : public ILayoutSolveExecution
{
public:
	virtual void Enqueue(
		FLayoutBackgroundSolveWork Work,
		FLayoutSolveCancellationToken CancellationToken,
		TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete) override;

	virtual void ProcessCompletions() override;

	virtual bool IsSynchronous() const override { return true; }

private:
	struct FQueuedWork
	{
		FLayoutBackgroundSolveWork Work;
		FLayoutSolveCancellationToken CancellationToken;
		TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete;
	};
	TArray<FQueuedWork> QueuedWork;
};

/** Creates the production async executor that launches work on UE::Tasks background threads. */
PORISMDIMSWORLDGENERATOREXTENSION_API TUniquePtr<ILayoutSolveExecution> CreateAsyncLayoutSolveExecution(
	TSharedRef<FSharedCompletionState, ESPMode::ThreadSafe> CompletionState);
