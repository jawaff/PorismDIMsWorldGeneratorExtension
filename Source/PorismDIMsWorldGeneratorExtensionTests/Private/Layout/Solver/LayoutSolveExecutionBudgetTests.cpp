// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Misc/AutomationTest.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleDeferredValidation.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutSolveExecutionBudget.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutSolveDiagnostics.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPreparationDeadlineTest,
	"PorismExtension.Layout.Solver.ExecutionBudget.PreEntryDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPreparationDeadlineTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("DeadlineControl");
	Request.ExecutionSettings.MaxSolveDurationSeconds = 5.0f;
	Request.ProfileSnapshot.LevelCount = 2;
	for (int32 GroupIndex = 0; GroupIndex < 5; ++GroupIndex)
	{
		auto& Group = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
		Group.GroupId = FLayoutId(*FString::Printf(TEXT("Group%d"), GroupIndex));
		// Every tuple overlaps. No Entry/CSP work can consume the old local attempt cap.
		for (int32 OptionIndex = 0; OptionIndex < 1024; ++OptionIndex)
		{
			auto& Option = Group.Options.AddDefaulted_GetRef();
			Option.LowerCell = FIntVector::ZeroValue;
			Option.UpperCell = FIntVector(0, 0, 1);
		}
	}
	LayoutSolveExecution::FScope Scope(5.0);
	auto* Ledger = LayoutSolveExecution::CurrentThreadLedger();
	Ledger->DeadlineSeconds = FPlatformTime::Seconds() - 1.0;
	FLayoutRegionSolveRequest OutRequest;
	FString Failure;
	TestFalse(TEXT("Freeze honors inherited expiry before Entry preparation"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request, false, {}, {}, OutRequest, Failure));
	TestTrue(TEXT("Freeze reports deadline, not content rejection"), Failure.Contains(TEXT("deadline")));
	const auto Result = LayoutProfileSolverInternal::SolveVerticalAccessHostAlternativesForTests(Request, OutRequest);
	TestFalse(TEXT("Final host search cannot reset inherited expiry"), Result.bSucceeded);
	TestTrue(TEXT("Ranked enumeration reports deadline"), Result.FailureReason.Contains(TEXT("deadline")));
	TestEqual(TEXT("Expired work never builds domains"), Ledger->DomainBuilds, uint64(0));
	TestEqual(TEXT("Expired work never starts Entry search"), Ledger->EntryAssignments, uint64(0));

	// Exercise expiry during a real formerly uncharged Cartesian scan, not only at entry.
	Ledger->DeadlineSeconds = FPlatformTime::Seconds() + 0.01;
	const double Start = FPlatformTime::Seconds();
	TestFalse(TEXT("All rejected host tuples terminate cooperatively"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request, false, {}, {}, OutRequest, Failure));
	const double Duration = FPlatformTime::Seconds() - Start;
	TestTrue(TEXT("Host rejection loop observes deadline"), Failure.Contains(TEXT("deadline")));
	TestTrue(TEXT("Pre-entry host work is recorded"), Ledger->HostAssignments > 0);
	TestEqual(TEXT("Rejected tuples did not become Entry attempts"), Ledger->EntryAssignments, uint64(0));
	TestTrue(TEXT("10ms deadline does not become an unbounded preparation scan"), Duration < 0.1);
	AddInfo(FString::Printf(TEXT("Pre-entry termination %.3fms; host tuples=%llu"), Duration * 1000.0,
		static_cast<unsigned long long>(Ledger->HostAssignments)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPreparationCancellationTest,
	"PorismExtension.Layout.Solver.ExecutionBudget.NestedCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPreparationCancellationTest::RunTest(const FString& Parameters)
{
	LayoutSolveExecution::FScope Scope(1.0);
	auto* Ledger = LayoutSolveExecution::CurrentThreadLedger();
	const double Deadline = Ledger->DeadlineSeconds;
	{
		LayoutSolveExecution::FScope Nested(100.0);
		TestTrue(TEXT("Nested requests borrow the same mutable ledger"), Ledger == LayoutSolveExecution::CurrentThreadLedger());
		TestEqual(TEXT("Nested request cannot extend deadline"), Ledger->DeadlineSeconds, Deadline);
		++LayoutSolveExecution::CurrentThreadLedger()->HostAssignments;
	}
	TestEqual(TEXT("Nested work returns to caller"), Ledger->HostAssignments, uint64(1));
	FLayoutSolveCancellationSource Source;
	const auto Token = Source.CreateToken();
	LayoutSolveCancellation::FThreadTokenScope TokenScope(Token);
	Source.Cancel();
	FLayoutRegionSolveRequest Request;
	FLayoutRegionSolveRequest OutRequest;
	FString Failure;
	TestFalse(TEXT("Canceled freeze rejects before preparation"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request, false, {}, {}, OutRequest, Failure));
	TestTrue(TEXT("Cancellation stays distinct from invalid content"), Failure.Contains(TEXT("canceled")));
	const auto Result = LayoutProfileSolverInternal::SolveVerticalAccessHostAlternativesForTests(Request, OutRequest);
	TestFalse(TEXT("Canceled final search has no successful result"), Result.bSucceeded);
	TestTrue(TEXT("Canceled final search emits no placements"), Result.Placements.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPreparationWorkBudgetTest,
	"PorismExtension.Layout.Solver.ExecutionBudget.RejectedWork",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPreparationWorkBudgetTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	for (int32 GroupIndex = 0; GroupIndex < 3; ++GroupIndex)
	{
		auto& Group = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
		Group.GroupId = FLayoutId(*FString::Printf(TEXT("Group%d"), GroupIndex));
		for (int32 Index = 0; Index < 128; ++Index)
		{
			auto& Option = Group.Options.AddDefaulted_GetRef();
			Option.LowerCell = FIntVector::ZeroValue;
			Option.UpperCell = FIntVector(0, 0, 1);
		}
	}
	for (bool bFreeze : {true, false})
	{
		LayoutSolveExecution::FScope Scope(0.0, 32);
		FLayoutRegionSolveRequest OutRequest;
		FString Failure;
		if (bFreeze)
		{
			TestFalse(TEXT("Rejected pre-entry tuples exhaust inherited work"),
				LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(Request, false, {}, {}, OutRequest, Failure));
		}
		else
		{
			const auto Result = LayoutProfileSolverInternal::SolveVerticalAccessHostAlternativesForTests(Request, OutRequest);
			TestFalse(TEXT("Ranked prefix rejections exhaust inherited work"), Result.bSucceeded);
			Failure = Result.FailureReason;
		}
		const auto& Work = *LayoutSolveExecution::CurrentThreadLedger();
		TestTrue(TEXT("Work exhaustion distinct from invalid content"), Failure.Contains(TEXT("work budget")));
		TestEqual(TEXT("No fresh nested work allowance"), Work.UsedWorkUnits, uint64(32));
		TestEqual(TEXT("Known prefix conflicts avoid domain construction"), Work.DomainBuilds, uint64(0));
		TestEqual(TEXT("Pre-entry rejections are not Entry attempts"), Work.EntryAssignments, uint64(0));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutDeferredPreparationStopTest,
	"PorismExtension.Layout.Solver.ExecutionBudget.DeferredCallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDeferredPreparationStopTest::RunTest(const FString& Parameters)
{
	using namespace LayoutRegionScheduleSolverPrivate;
	LayoutSolveExecution::FScope Scope(5.0, 32);
	auto* Work = LayoutSolveExecution::CurrentThreadLedger();
	TArray<FPlacementBridgeDeferredValidationCandidate> Candidates;
	Candidates.SetNum(3);
	int32 Calls = 0;
	const auto Result = ResolveDeferredCompletePlacementCandidates(TEXT("DeadlineCallback"), Candidates, 3, nullptr, nullptr,
		[&](FCommittedRecursiveScheduleState&, FString& Failure)
		{
			++Calls;
			LayoutSolveExecution::FScope Nested(500.0, 50000);
			TestTrue(TEXT("Structural callback inherits invocation ledger"), LayoutSolveExecution::CurrentThreadLedger() == Work);
			Work->DeadlineSeconds = FPlatformTime::Seconds() - 1.0;
			TestFalse(TEXT("Preparation checkpoint observes inherited expiry"), LayoutSolveExecution::Checkpoint(Failure));
			return true; // Even a late success must not grant publication authority.
		});
	TestEqual(TEXT("No remaining deferred callback runs after expiry"), Calls, 1);
	TestFalse(TEXT("Expired callback success has no apply authority"), Result.bSucceeded);
	TestTrue(TEXT("Terminal deadline is retained"), Result.FailureReason.Contains(TEXT("deadline")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutOptionalImprovementBudgetTest,
	"PorismExtension.Layout.Solver.ExecutionBudget.OptionalImprovement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Improvement stops preserve actual charges and cannot expire, extend, or reset the owning invocation. */
bool FLayoutOptionalImprovementBudgetTest::RunTest(const FString& Parameters)
{
	using namespace LayoutSolveExecution;
	FScope Scope(60.0, 12);
	auto* Ledger = CurrentThreadLedger();
	FString Failure;
	{
		FOptionalImprovementScope Optional;
		const auto Ceiling = Ledger->OptionalWorkCeiling;
		{
			FOptionalImprovementScope Nested;
			TestTrue(TEXT("Nested scope cannot enlarge allowance"), Ledger->OptionalWorkCeiling <= Ceiling);
			while (Charge(&FWorkLedger::SparseWork, Failure)) {}
			TestTrue(TEXT("Soft stop remains distinct"), Failure.Contains(TEXT("Optional improvement")));
			TestFalse(TEXT("Soft stop cannot expire invocation"), Ledger->bWorkBudgetExceeded);
		}
		TestEqual(TEXT("Nested scope restores outer ceiling"), Ledger->OptionalWorkCeiling, Ceiling);
	}
	TestEqual(TEXT("Spent work survives scope exit"), Ledger->UsedWorkUnits, uint64(6));
	TestTrue(TEXT("Proved result retains publication allowance"), Checkpoint(Failure));
	{
		FOptionalImprovementScope Optional;
		Ledger->OptionalDeadlineSeconds = FPlatformTime::Seconds() - 1.0;
		TestFalse(TEXT("Soft deadline stops improvements"), Checkpoint(Failure));
	}
	TestTrue(TEXT("Soft deadline does not poison caller"), Checkpoint(Failure));
	{
		FOptionalImprovementScope Optional;
		Ledger->DeadlineSeconds = FPlatformTime::Seconds() - 1.0;
	}
	TestFalse(TEXT("Actual expiry still blocks publication"), Checkpoint(Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutExecutionDiagnosticsTest,
	"PorismExtension.Layout.Solver.ExecutionBudget.Diagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutExecutionDiagnosticsTest::RunTest(const FString& Parameters)
{
	using namespace LayoutSolveExecution;
	const FString Context = TEXT("origin=test request=diagnostic-budget");
	for (const TCHAR* Suffix : {TEXT("phase=work event=begin"), TEXT("phase=work event=end succeeded=0"),
		TEXT("phase=work event=work usedStart=0 usedEnd=6 max=6 deltaUsed=6"),
		TEXT("event=budget-stop kind=8"), TEXT("event=budget-stop kind=2")})
		AddExpectedMessagePlain(TEXT("[LayoutSolveDiag] ") + Context + TEXT(" ") + Suffix, ELogVerbosity::Display);
	for (bool bEnabled : {false, true})
	{
		FScope Scope(0.0, 6);
		auto* Ledger = CurrentThreadLedger();
		Ledger->DiagnosticContext = bEnabled ? Context : FString();
		FString Failure;
		FDiagnosticScope Diagnostics(Ledger->DiagnosticContext, TEXT("work"), &Failure, 0.0, Ledger);
		{
			FOptionalImprovementScope Optional;
			while (Charge(&FWorkLedger::WitnessRefreshes, Failure)) {}
			TestEqual(TEXT("Optional scope consumes only its original half allowance"), Ledger->UsedWorkUnits, uint64(3));
			TestFalse(TEXT("Repeated stop remains rejected"), Checkpoint(Failure));
		}
		while (Charge(&FWorkLedger::WitnessRefreshes, Failure)) {}
		Diagnostics.Finish(false);
		Diagnostics.Finish(false);
		TestEqual(TEXT("Diagnostics never charge or reset work"), Ledger->UsedWorkUnits, uint64(6));
		TestEqual(TEXT("Witness accounting remains exact"), Ledger->WitnessRefreshes, uint64(6));
		TestEqual(TEXT("Diagnostics do not alter deadline"), Ledger->DeadlineSeconds, 0.0);
		TestEqual(TEXT("Only enabled diagnostics track distinct stop classes"), Ledger->DiagnosticStopReasonsLogged, uint8(bEnabled ? 10 : 0));
	}
	for (bool bCancel : {false, true})
	{
		FScope Scope(0.0, 6);
		auto* Ledger = CurrentThreadLedger();
		Ledger->DiagnosticContext = Context;
		Ledger->DeadlineSeconds = FPlatformTime::Seconds() - 1.0;
		FLayoutSolveCancellationSource Cancellation;
		const auto Token = Cancellation.CreateToken();
		LayoutSolveCancellation::FThreadTokenScope TokenScope(Token);
		if (bCancel) Cancellation.Cancel();
		AddExpectedMessagePlain(TEXT("[LayoutSolveDiag] ") + Context +
			(bCancel ? TEXT(" event=budget-stop kind=1") : TEXT(" event=budget-stop kind=4")), ELogVerbosity::Display);
		FString Failure;
		TestFalse(TEXT("Expired/canceled checkpoint stays rejected"), Checkpoint(Failure));
		TestFalse(TEXT("Repeated stop is logged only once"), Checkpoint(Failure));
		TestTrue(TEXT("Stop priority stays unchanged"), Failure.Contains(bCancel ? TEXT("canceled") : TEXT("deadline")));
		TestEqual(TEXT("Stop diagnostics consume no work"), Ledger->UsedWorkUnits, uint64(0));
	}
	TestTrue(TEXT("Expected diagnostics emitted exactly once"), HasMetExpectedMessages(ELogVerbosity::Display));
	return true;
}
