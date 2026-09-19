// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"

/** Shared atomic cancellation flag owned by one solve group or one superseded editor preview. */
class FLayoutSolveCancellationState
{
public:
	/** Requests cooperative cancellation. Running workers should stop at checkpoints; late results are discarded. */
	void Cancel() { bCanceled = true; }

	/** Returns true after any owner has requested cancellation. */
	bool IsCancellationRequested() const { return bCanceled; }

private:
	FThreadSafeBool bCanceled = false;
};

/** Worker-visible lightweight cancellation token. */
class FLayoutSolveCancellationToken
{
public:
	FLayoutSolveCancellationToken() = default;
	explicit FLayoutSolveCancellationToken(const TSharedRef<FLayoutSolveCancellationState, ESPMode::ThreadSafe>& InState)
		: State(InState)
	{
	}

	/** Returns true when the owning source was canceled or destroyed. */
	bool IsCancellationRequested() const
	{
		const TSharedPtr<FLayoutSolveCancellationState, ESPMode::ThreadSafe> PinnedState = State.Pin();
		return !PinnedState.IsValid() || PinnedState->IsCancellationRequested();
	}

private:
	TWeakPtr<FLayoutSolveCancellationState, ESPMode::ThreadSafe> State;
};

/** Cooperative cancellation shared by worker callbacks and lower-level checkpoints. */
namespace LayoutSolveCancellation
{
	/** One runtime-owned TLS slot, shared by callers across module/DLL boundaries. */
	PORISMDIMSWORLDGENERATOREXTENSION_API const FLayoutSolveCancellationToken*& CurrentThreadToken();

	/** Temporarily exposes one dispatcher token to lower-level solver checkpoints on this worker thread. */
	class FThreadTokenScope
	{
	public:
		explicit FThreadTokenScope(const FLayoutSolveCancellationToken& InToken)
			: PreviousToken(CurrentThreadToken())
		{
			CurrentThreadToken() = &InToken;
		}

		~FThreadTokenScope()
		{
			CurrentThreadToken() = PreviousToken;
		}

	private:
		const FLayoutSolveCancellationToken* PreviousToken = nullptr;
	};

	/** Returns true when the current worker thread has a canceled layout-solve token. */
	inline bool IsCurrentThreadCancellationRequested()
	{
		const FLayoutSolveCancellationToken* Token = CurrentThreadToken();
		return Token != nullptr && Token->IsCancellationRequested();
	}
}

/** Game-thread cancellation source used by dispatcher records and layout groups. */
class FLayoutSolveCancellationSource
{
public:
	FLayoutSolveCancellationSource()
		: State(MakeShared<FLayoutSolveCancellationState, ESPMode::ThreadSafe>())
	{
	}

	/** Creates one worker token that observes this source. */
	FLayoutSolveCancellationToken CreateToken() const { return FLayoutSolveCancellationToken(State.ToSharedRef()); }

	/** Requests cooperative cancellation for waiting/running jobs. */
	void Cancel() { State->Cancel(); }

	/** Returns true after cancellation has been requested. */
	bool IsCancellationRequested() const { return State->IsCancellationRequested(); }

private:
	TSharedPtr<FLayoutSolveCancellationState, ESPMode::ThreadSafe> State;
};
