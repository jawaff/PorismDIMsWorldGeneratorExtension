// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformTime.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

namespace LayoutSolveExecution
{
	/** Execution-only work evidence. Never serialized into a request, proof or cache entry.
	 * A lifecycle may transfer this ledger between serial stages after predecessor completion. */
	struct FWorkLedger
	{
		double DeadlineSeconds = 0.0;
		uint64 MaxWorkUnits = MAX_int32;
		uint64 UsedWorkUnits = 0;
		bool bWorkBudgetExceeded = false;
		/** Temporary improvement ceilings; reaching them does not expire the owning invocation. */
		uint64 OptionalWorkCeiling = MAX_uint64;
		double OptionalDeadlineSeconds = 0.0;
		uint64 RankedNodes = 0;
		uint64 EntryNodes = 0;
		uint64 CandidateAttempts = 0;
		uint64 HostAssignments = 0;
		uint64 EntryAssignments = 0;
		uint64 WitnessRefreshes = 0;
		uint64 VariantBuilds = 0;
		uint64 VariantReuses = 0;
		uint64 DomainBuilds = 0;
		uint64 SparseWork = 0;
		uint64 PrewarmStages = 0;
		/** Captured on the game thread; empty disables request-scoped diagnostics.
		 * Serial stage ownership also protects these bounded, execution-only summaries. */
		FString DiagnosticContext;
		uint8 DiagnosticStopReasonsLogged = 0;
		uint64 DiagnosticRegionCount = 0;
		double DiagnosticSlowestRegionSeconds = 0.0;
		FString DiagnosticSlowestRegionReport;
	};

	/** One runtime TLS slot, including calls from the tests module. Nested synchronous preparation borrows it. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FWorkLedger*& CurrentThreadLedger();

	/** Starts a scheduling invocation only when none exists; nested requests cannot reset its deadline or work. */
	class FScope
	{
	public:
		explicit FScope(const double MaxDurationSeconds, const int32 MaxWorkUnits = MAX_int32)
			: Previous(CurrentThreadLedger())
		{
			if (Previous == nullptr)
			{
				Owned.MaxWorkUnits = FMath::Max(1, MaxWorkUnits);
				Owned.DeadlineSeconds = MaxDurationSeconds > 0.0
					? FPlatformTime::Seconds() + MaxDurationSeconds : 0.0;
				CurrentThreadLedger() = &Owned;
			}
		}
		/** Installs explicit operation state, restoring prior TLS state on exit. Synchronous
		 * dispatch may enter while another invocation's scope is active. The owner must
		 * serialize access; this scope never grants concurrent workers a shared mutable ledger. */
		explicit FScope(FWorkLedger& OperationLedger)
			: Previous(CurrentThreadLedger())
		{
			CurrentThreadLedger() = &OperationLedger;
		}
		~FScope() { CurrentThreadLedger() = Previous; }
		FScope(const FScope&) = delete;
		FScope& operator=(const FScope&) = delete;

	private:
		FWorkLedger Owned;
		FWorkLedger* Previous;
	};

	/** Limits an attempt to remaining allowance divided by AllowanceDivisor (half by default).
	 * The remainder stays available for recovery or publication. Nested improvements
	 * can only tighten ceilings; actual work remains charged after scope exit.
	 */
	class FOptionalImprovementScope
	{
	public:
		explicit FOptionalImprovementScope(const uint32 AllowanceDivisor = 2) : Ledger(CurrentThreadLedger())
		{
			check(AllowanceDivisor > 0);
			if (Ledger == nullptr) return;
			PreviousWork = Ledger->OptionalWorkCeiling;
			PreviousDeadline = Ledger->OptionalDeadlineSeconds;
			const uint64 Remaining = Ledger->MaxWorkUnits > Ledger->UsedWorkUnits ? Ledger->MaxWorkUnits - Ledger->UsedWorkUnits : 0;
			Ledger->OptionalWorkCeiling = FMath::Min(PreviousWork, Ledger->UsedWorkUnits + Remaining / AllowanceDivisor);
			if (Ledger->DeadlineSeconds > 0.0)
			{
				const double Now = FPlatformTime::Seconds();
				const double Deadline = Now + FMath::Max(0.0, Ledger->DeadlineSeconds - Now) / AllowanceDivisor;
				Ledger->OptionalDeadlineSeconds = PreviousDeadline > 0.0 ? FMath::Min(PreviousDeadline, Deadline) : Deadline;
			}
		}
		~FOptionalImprovementScope()
		{
			if (Ledger == nullptr) return;
			Ledger->OptionalWorkCeiling = PreviousWork;
			Ledger->OptionalDeadlineSeconds = PreviousDeadline;
		}
		FOptionalImprovementScope(const FOptionalImprovementScope&) = delete;
		FOptionalImprovementScope& operator=(const FOptionalImprovementScope&) = delete;
	private:
		FWorkLedger* Ledger;
		uint64 PreviousWork = MAX_uint64;
		double PreviousDeadline = 0.0;
	};

	/** Checks cancellation independently from content feasibility, including before any CSP/Entry attempt. */
	inline bool ShouldStop()
	{
		const FWorkLedger* Ledger = CurrentThreadLedger();
		return LayoutSolveCancellation::IsCurrentThreadCancellationRequested()
			|| (Ledger != nullptr && (Ledger->bWorkBudgetExceeded
				|| (Ledger->DeadlineSeconds > 0.0 && FPlatformTime::Seconds() >= Ledger->DeadlineSeconds)
				|| Ledger->UsedWorkUnits >= Ledger->OptionalWorkCeiling
				|| (Ledger->OptionalDeadlineSeconds > 0.0 && FPlatformTime::Seconds() >= Ledger->OptionalDeadlineSeconds)));
	}

	/** Reports terminal execution failure without replacing it with a later content-rejection diagnostic. */
	inline bool Checkpoint(FString& OutFailureReason)
	{
		if (!ShouldStop()) return true;
		// Sample the existing priority once so a clock/token change cannot mismatch the reason and diagnostic kind.
		const uint8 StopKind = LayoutSolveCancellation::IsCurrentThreadCancellationRequested() ? 1
			: CurrentThreadLedger() != nullptr && CurrentThreadLedger()->bWorkBudgetExceeded ? 2
			: CurrentThreadLedger() != nullptr && CurrentThreadLedger()->DeadlineSeconds > 0.0
				&& FPlatformTime::Seconds() >= CurrentThreadLedger()->DeadlineSeconds ? 4 : 8;
		OutFailureReason = StopKind == 1 ? TEXT("Layout preparation canceled under the inherited execution budget.")
			: StopKind == 2 ? TEXT("Layout preparation exhausted the inherited work budget.")
			: StopKind == 4 ? TEXT("Layout preparation exceeded the inherited execution deadline.")
			: TEXT("Optional improvement exhausted its reserved execution allowance.");
		if (auto* Ledger = CurrentThreadLedger(); Ledger && !Ledger->DiagnosticContext.IsEmpty())
		{
			const double Now = FPlatformTime::Seconds();
			// One row per stop class, before optional scopes unwind and hide their spent allowance.
			if ((Ledger->DiagnosticStopReasonsLogged & StopKind) == 0)
			{
				Ledger->DiagnosticStopReasonsLogged |= StopKind;
				UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s event=budget-stop kind=%u used=%llu max=%llu optionalCeiling=%llu remainingMs=%.3f optionalRemainingMs=%.3f hosts=%llu witnesses=%llu domains=%llu candidates=%llu reason=%s"),
					*Ledger->DiagnosticContext, static_cast<uint32>(StopKind), Ledger->UsedWorkUnits, Ledger->MaxWorkUnits,
					Ledger->OptionalWorkCeiling, Ledger->DeadlineSeconds > 0.0 ? (Ledger->DeadlineSeconds - Now) * 1000.0 : -1.0,
					Ledger->OptionalDeadlineSeconds > 0.0 ? (Ledger->OptionalDeadlineSeconds - Now) * 1000.0 : -1.0,
					Ledger->HostAssignments, Ledger->WitnessRefreshes, Ledger->DomainBuilds, Ledger->CandidateAttempts, *OutFailureReason);
			}
		}
		return false;
	}

	/** Charges attempted search/preparation work, including rejection. Counters retain each phase's meaning. */
	inline bool Charge(uint64 FWorkLedger::* Counter, FString& OutFailureReason)
	{
		if (!Checkpoint(OutFailureReason)) return false;
		if (auto* Ledger = CurrentThreadLedger())
		{
			if (Ledger->UsedWorkUnits >= Ledger->MaxWorkUnits)
			{
				Ledger->bWorkBudgetExceeded = true;
				return Checkpoint(OutFailureReason);
			}
			++Ledger->UsedWorkUnits;
			++(Ledger->*Counter);
		}
		return true;
	}
}
