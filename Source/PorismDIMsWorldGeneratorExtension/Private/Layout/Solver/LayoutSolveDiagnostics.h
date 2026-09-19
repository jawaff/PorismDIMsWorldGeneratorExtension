// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Solver/LayoutSolveExecutionBudget.h"

namespace LayoutSolveExecution
{
	/** Logs one serial phase using captured strings, never UObjects or frozen payloads.
	 * Finish before handing the ledger to another stage; early exits report incomplete work.
	 * Empty context does no timing/logging. Counter deltas retain the ledger's original meanings. */
	class FDiagnosticScope
	{
	public:
		FDiagnosticScope(const FString& InContext, const TCHAR* InPhase,
			const FString* InFailure = nullptr, double QueuedAtSeconds = 0.0, FWorkLedger* InLedger = nullptr)
			: Context(InContext), Phase(InPhase), Failure(InFailure), Ledger(InLedger)
		{
			if (Context.IsEmpty()) return;
			StartSeconds = FPlatformTime::Seconds();
			if (Ledger) Before = *Ledger;
			UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s phase=%s event=begin budgetBound=%d deadlineEnabled=%d queueWaitMs=%.3f remainingMs=%.3f used=%llu max=%llu"),
				*Context, Phase, Ledger != nullptr, Ledger && Ledger->DeadlineSeconds > 0.0, QueuedAtSeconds > 0.0 ? (StartSeconds - QueuedAtSeconds) * 1000.0 : 0.0,
				RemainingMilliseconds(StartSeconds), Ledger ? Ledger->UsedWorkUnits : 0, Ledger ? Ledger->MaxWorkUnits : 0);
		}
		~FDiagnosticScope() { Finish(false); }
		FDiagnosticScope(const FDiagnosticScope&) = delete;
		FDiagnosticScope& operator=(const FDiagnosticScope&) = delete;

		/** Emits once, after the phase's existing completion/checkpoint logic. No budget checks or charges. */
		void Finish(bool bSucceeded)
		{
			if (Context.IsEmpty() || bFinished) return;
			bFinished = true;
			const double Now = FPlatformTime::Seconds();
			UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s phase=%s event=end succeeded=%d elapsedMs=%.3f remainingMs=%.3f canceled=%d workExceeded=%d deadlineExpired=%d optionalWorkReached=%d optionalDeadlineExpired=%d failure=%s"),
				*Context, Phase, bSucceeded, (Now - StartSeconds) * 1000.0, RemainingMilliseconds(Now),
				Ledger && LayoutSolveCancellation::IsCurrentThreadCancellationRequested(), Ledger && Ledger->bWorkBudgetExceeded,
				Ledger && Ledger->DeadlineSeconds > 0.0 && Now >= Ledger->DeadlineSeconds,
				Ledger && Ledger->UsedWorkUnits >= Ledger->OptionalWorkCeiling,
				Ledger && Ledger->OptionalDeadlineSeconds > 0.0 && Now >= Ledger->OptionalDeadlineSeconds,
				Failure ? *Failure->Left(1024).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" ")) : TEXT(""));
			if (!Ledger) return;
			UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s phase=%s event=work usedStart=%llu usedEnd=%llu max=%llu deltaUsed=%llu deltaHosts=%llu deltaRanked=%llu deltaEntries=%llu deltaEntryNodes=%llu deltaWitnesses=%llu deltaVariantBuilds=%llu deltaVariantReuses=%llu deltaDomains=%llu deltaCandidates=%llu deltaSparse=%llu deltaPrewarm=%llu regionReports=%llu slowestRegion={%s}"),
				*Context, Phase, Before.UsedWorkUnits, Ledger->UsedWorkUnits, Ledger->MaxWorkUnits,
				Ledger->UsedWorkUnits - Before.UsedWorkUnits, Ledger->HostAssignments - Before.HostAssignments,
				Ledger->RankedNodes - Before.RankedNodes, Ledger->EntryAssignments - Before.EntryAssignments,
				Ledger->EntryNodes - Before.EntryNodes, Ledger->WitnessRefreshes - Before.WitnessRefreshes,
				Ledger->VariantBuilds - Before.VariantBuilds, Ledger->VariantReuses - Before.VariantReuses,
				Ledger->DomainBuilds - Before.DomainBuilds, Ledger->CandidateAttempts - Before.CandidateAttempts,
				Ledger->SparseWork - Before.SparseWork, Ledger->PrewarmStages - Before.PrewarmStages,
				Ledger->DiagnosticRegionCount, *Ledger->DiagnosticSlowestRegionReport);
		}

	private:
		double RemainingMilliseconds(double Now) const
		{
			return Ledger && Ledger->DeadlineSeconds > 0.0 ? (Ledger->DeadlineSeconds - Now) * 1000.0 : -1.0;
		}
		FString Context;
		const TCHAR* Phase;
		const FString* Failure;
		FWorkLedger* Ledger;
		FWorkLedger Before;
		double StartSeconds = 0.0;
		bool bFinished = false;
	};
}
