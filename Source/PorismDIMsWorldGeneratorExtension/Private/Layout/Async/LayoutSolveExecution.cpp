// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutSolveExecution.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Tasks/Task.h"

// ---- FAsyncLayoutSolveExecution ----

class FAsyncLayoutSolveExecution : public ILayoutSolveExecution
{
public:
	explicit FAsyncLayoutSolveExecution(
		TSharedRef<FSharedCompletionState, ESPMode::ThreadSafe> InCompletionState)
		: CompletionState(MoveTemp(InCompletionState))
	{
	}

	virtual void Enqueue(
		FLayoutBackgroundSolveWork Work,
		FLayoutSolveCancellationToken CancellationToken,
		TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete) override
	{
		UE::Tasks::Launch(UE_SOURCE_LOCATION, [this,
			Work = MoveTemp(Work),
			CancellationToken,
			OnComplete = MoveTemp(OnComplete)
		]() mutable
		{
			FLayoutBackgroundSolveCompletion Completion;

			if (CancellationToken.IsCancellationRequested())
			{
				Completion.bCanceled = true;
				Completion.FailureReason = TEXT("Canceled before start.");
				OnComplete(MoveTemp(Completion));
				return;
			}

			try
			{
				LayoutSolveCancellation::FThreadTokenScope TokenScope(CancellationToken);
				Completion.bWorkSucceeded = Work(CancellationToken, Completion.FailureReason);
			}
			catch (...)
			{
				Completion.bWorkSucceeded = false;
				Completion.FailureReason = TEXT("Unhandled exception in layout solve work.");
			}

			Completion.bCanceled = CancellationToken.IsCancellationRequested();
			OnComplete(MoveTemp(Completion));
		});
	}

	virtual void ProcessCompletions() override
	{
		// Completions drain through the dispatcher's shared completion-state queue
		// during DrainCompletions().  This no-op keeps the interface uniform.
	}

private:
	TSharedRef<FSharedCompletionState, ESPMode::ThreadSafe> CompletionState;
};

// ---- FSynchronousLayoutSolveExecution ----

void FSynchronousLayoutSolveExecution::Enqueue(
	FLayoutBackgroundSolveWork Work,
	FLayoutSolveCancellationToken CancellationToken,
	TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete)
{
	QueuedWork.Add({MoveTemp(Work), CancellationToken, MoveTemp(OnComplete)});
}

void FSynchronousLayoutSolveExecution::ProcessCompletions()
{
	for (FQueuedWork& Item : QueuedWork)
	{
		FLayoutBackgroundSolveCompletion Completion;

		if (Item.CancellationToken.IsCancellationRequested())
		{
			Completion.bCanceled = true;
			Completion.FailureReason = TEXT("Canceled before start.");
			Item.OnComplete(MoveTemp(Completion));
			continue;
		}

		try
		{
			LayoutSolveCancellation::FThreadTokenScope TokenScope(Item.CancellationToken);
			Completion.bWorkSucceeded = Item.Work(Item.CancellationToken, Completion.FailureReason);
		}
		catch (...)
		{
			Completion.bWorkSucceeded = false;
			Completion.FailureReason = TEXT("Unhandled exception in layout solve work.");
		}

		Completion.bCanceled = Item.CancellationToken.IsCancellationRequested();
		Item.OnComplete(MoveTemp(Completion));
	}
	QueuedWork.Reset();
}

TUniquePtr<ILayoutSolveExecution> CreateAsyncLayoutSolveExecution(
	TSharedRef<FSharedCompletionState, ESPMode::ThreadSafe> CompletionState)
{
	return MakeUnique<FAsyncLayoutSolveExecution>(MoveTemp(CompletionState));
}
