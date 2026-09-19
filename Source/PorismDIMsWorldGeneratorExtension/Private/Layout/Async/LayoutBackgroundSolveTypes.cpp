// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundSolveTypes.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

#include "HAL/PlatformMisc.h"

const FLayoutSolveCancellationToken*& LayoutSolveCancellation::CurrentThreadToken()
{
	static thread_local const FLayoutSolveCancellationToken* Token = nullptr;
	return Token;
}

int32 FLayoutBackgroundSolveSettings::ResolveMaxConcurrentBackgroundLayoutSolves() const
{
	if (MaxConcurrentBackgroundLayoutSolves > 0)
	{
		return FMath::Max(1, MaxConcurrentBackgroundLayoutSolves);
	}

	const int32 LogicalCoreCount = FMath::Max(1, FPlatformMisc::NumberOfCoresIncludingHyperthreads());
	return FMath::Clamp(LogicalCoreCount - 2, 1, 4);
}

int32 FLayoutBackgroundSolveSettings::ResolveMaxLayoutSolveCandidateAttempts() const
{
	return FMath::Max(1, MaxLayoutSolveCandidateAttempts);
}

FString FLayoutBackgroundSolveDiagnosticsSnapshot::ToDebugString() const
{
	return FString::Printf(
		TEXT("Layout Async: Groups=%d Running=%d Queued=%d AwaitingPublish=%d | Cumulative stages: Succeeded=%d Failed=%d (not placements)"),
		InProgressLayoutGroups,
		Running,
		WaitingForDispatch,
		CompletedAwaitingPublish,
		PublishedAccepted,
		PublishedRejected);
}
