// Copyright 2026 Spotted Loaf Studio

#include "Misc/AutomationTest.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutSolveExecution.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimeDescriptorRetentionTest,
	"PorismExtension.Layout.Runtime.DescriptorRetention.RetiresAfterOwningCallbacks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeDescriptorRetentionTest::RunTest(const FString& Parameters)
{
	auto* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	Runtime->MaxConcurrentBackgroundLayoutSolves = 1;
	Runtime->SetLayoutSolveExecutionForTesting(MakeUnique<FSynchronousLayoutSolveExecution>());
	Runtime->SetDisableAutoPumpForTesting(true);
	auto& Dispatcher = Runtime->GetOrCreateBackgroundSolveDispatcher();
	auto Submit = [&Dispatcher](uint64 Group)
	{
		FLayoutBackgroundSolveSubmission Job;
		Job.DebugName = TEXT("DescriptorRetention");
		Job.LayoutGroupId = Group;
		Job.Work = [](const FLayoutSolveCancellationToken& Token, FString&) { return !Token.IsCancellationRequested(); };
		Job.PublishOnGameThread = [](const FLayoutBackgroundSolveCompletion&) {};
		return Dispatcher.Submit(MoveTemp(Job));
	};

	const auto Old = Submit(1);
	Runtime->TombstoneFrozenSubmissionDescriptorPayload(TEXT("OldDescriptor"));
	Dispatcher.CancelGroup(Old.LayoutGroupId);
	TestTrue(TEXT("Canceled worker remains behind retention fence until drained"), Dispatcher.GetRetiredJobWatermark() < Old.JobId);
	TestTrue(TEXT("Cancellation cannot prematurely reclaim descriptor tombstone"), Runtime->IsFrozenSubmissionDescriptorTombstoned(TEXT("OldDescriptor")));

	const auto New = Submit(2);
	Dispatcher.MarkWaitingForPrerequisites(New);
	Runtime->PumpBackgroundLayoutSolves();
	TestTrue(TEXT("Unrelated later work remains pending"), Dispatcher.HasPendingGroup(New.LayoutGroupId));
	TestFalse(TEXT("Retired callbacks release tombstone without global idle"), Runtime->IsFrozenSubmissionDescriptorTombstoned(TEXT("OldDescriptor")));

	Runtime->TombstoneFrozenSubmissionDescriptorPayload(TEXT("NewDescriptor"));
	Runtime->PumpBackgroundLayoutSolves();
	TestTrue(TEXT("Waiting admitted callback retains its tombstone"), Runtime->IsFrozenSubmissionDescriptorTombstoned(TEXT("NewDescriptor")));
	Dispatcher.CancelGroup(New.LayoutGroupId);
	Runtime->PumpBackgroundLayoutSolves();
	TestFalse(TEXT("Canceled waiting callback releases its tombstone"), Runtime->IsFrozenSubmissionDescriptorTombstoned(TEXT("NewDescriptor")));
	return true;
}
#endif
