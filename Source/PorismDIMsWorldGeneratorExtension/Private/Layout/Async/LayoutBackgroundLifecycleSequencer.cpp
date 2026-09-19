// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutBackgroundLifecycleSequencer.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Layout/Solver/LayoutSolveDiagnostics.h"

namespace
{
	/** Only a completed predecessor may queue the next user of this execution-only ledger.
	 * Do not use this wrapper for concurrent jobs; neither cancellation nor proof identity owns the ledger. */
	void BindSerialOperationBudget(FLayoutBackgroundSolveSubmission& Submission,
		const TSharedRef<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe>& Ledger)
	{
		FLayoutBackgroundSolveWork Work = MoveTemp(Submission.Work);
		const TCHAR* Phase = Submission.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::ManifestPrewarm ? TEXT("prewarm")
			: Submission.LifecycleStage == ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight ? TEXT("preflight") : TEXT("solve");
		const double QueuedAtSeconds = Ledger->DiagnosticContext.IsEmpty() ? 0.0 : FPlatformTime::Seconds();
		Submission.Work = [Work = MoveTemp(Work), Ledger, QueuedAtSeconds, Phase](const FLayoutSolveCancellationToken& Token, FString& Failure)
		{
			LayoutSolveCancellation::FThreadTokenScope CancellationScope(Token);
			LayoutSolveExecution::FScope Scope(*Ledger);
			LayoutSolveExecution::FDiagnosticScope Diagnostics(Ledger->DiagnosticContext, Phase, &Failure, QueuedAtSeconds, &Ledger.Get());
			if (!LayoutSolveExecution::Checkpoint(Failure)) return false;
			const bool bSucceeded = Work && Work(Token, Failure);
			// A late content rejection must not hide cancellation or operation expiry.
			const bool bCompleted = LayoutSolveExecution::Checkpoint(Failure) && bSucceeded;
			Diagnostics.Finish(bCompleted);
			return bCompleted;
		};
		if (!Ledger->DiagnosticContext.IsEmpty() && Submission.PublishOnGameThread)
		{
			// Publication reads captured identity only: another serial stage may already own the ledger.
			Submission.PublishOnGameThread = [Publish = MoveTemp(Submission.PublishOnGameThread),
				Context = Ledger->DiagnosticContext, Phase](const FLayoutBackgroundSolveCompletion& Completion)
			{
				UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s phase=%s event=publication succeeded=%d canceled=%d reason=%s"),
					*Context, Phase, Completion.bWorkSucceeded, Completion.bCanceled,
					*Completion.FailureReason.Left(1024).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" ")));
				Publish(Completion);
			};
		}
	}
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmSubmission(
	FString DebugName,
	const uint64 LayoutGroupId,
	const int32 Priority,
	FLayoutManifestPrewarmInput PrewarmInput,
	FLayoutManifestPrewarmStageComplete OnComplete)
{
	TSharedRef<FLayoutManifestPrewarmResult, ESPMode::ThreadSafe> SharedResult =
		MakeShared<FLayoutManifestPrewarmResult, ESPMode::ThreadSafe>();

	FLayoutBackgroundSolveSubmission Submission;
	Submission.DebugName = MoveTemp(DebugName);
	Submission.LayoutGroupId = LayoutGroupId;
	Submission.Tier = PrewarmInput.Kind == ELayoutManifestPrewarmKind::Continuation
		? ELayoutBackgroundSolveJobTier::Continuation
		: ELayoutBackgroundSolveJobTier::NearRoot;
	Submission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::ManifestPrewarm;
	Submission.Priority = Priority;
	Submission.Work = [PrewarmInput = MoveTemp(PrewarmInput), SharedResult](
		const FLayoutSolveCancellationToken& CancellationToken,
		FString& OutFailureReason) mutable
	{
		if (CancellationToken.IsCancellationRequested())
		{
			OutFailureReason = TEXT("Manifest prewarm was canceled before execution.");
			return false;
		}

		*SharedResult = FLayoutManifestPrewarmSequence::RunPrewarm(PrewarmInput);
		if (!SharedResult->bHasFrozenRequestManifest)
		{
			OutFailureReason = SharedResult->FailureReason.IsEmpty()
				? TEXT("Manifest prewarm did not produce a frozen request manifest.")
				: SharedResult->FailureReason;
			return false;
		}
		return true;
	};
	Submission.PublishOnGameThread = [SharedResult, OnComplete = MoveTemp(OnComplete)](
		const FLayoutBackgroundSolveCompletion& Completion)
	{
		if (OnComplete)
		{
			OnComplete(Completion, *SharedResult);
		}
	};
	return Submission;
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildAdmissibilityPreflightSubmission(
	FString DebugName,
	const uint64 LayoutGroupId,
	const int32 Priority,
	FLayoutBackgroundAdmissibilityPreflightInput PreflightInput,
	FLayoutAdmissibilityPreflightStageComplete OnComplete)
{
	TSharedRef<FLayoutBackgroundAdmissibilityPreflightResult, ESPMode::ThreadSafe> SharedResult =
		MakeShared<FLayoutBackgroundAdmissibilityPreflightResult, ESPMode::ThreadSafe>();

	FLayoutBackgroundSolveSubmission Submission;
	Submission.DebugName = MoveTemp(DebugName);
	Submission.LayoutGroupId = LayoutGroupId;
	Submission.Tier = PreflightInput.Kind == ELayoutBackgroundAdmissibilityPreflightKind::Continuation
		? ELayoutBackgroundSolveJobTier::Continuation
		: ELayoutBackgroundSolveJobTier::NearRoot;
	Submission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight;
	Submission.Priority = Priority;
	Submission.Work = [PreflightInput = MoveTemp(PreflightInput), SharedResult](
		const FLayoutSolveCancellationToken& CancellationToken,
		FString& OutFailureReason) mutable
	{
		if (CancellationToken.IsCancellationRequested())
		{
			OutFailureReason = TEXT("Admissibility preflight was canceled before execution.");
			return false;
		}

		*SharedResult = FLayoutBackgroundAdmissibilityPreflight::Run(PreflightInput);
		if (!SharedResult->bAdmissible)
		{
			OutFailureReason = SharedResult->FailureReason.IsEmpty()
				? TEXT("Admissibility preflight rejected the candidate.")
				: SharedResult->FailureReason;
			return false;
		}
		return true;
	};
	Submission.PublishOnGameThread = [SharedResult, OnComplete = MoveTemp(OnComplete)](
		const FLayoutBackgroundSolveCompletion& Completion)
	{
		if (OnComplete)
		{
			OnComplete(Completion, *SharedResult);
		}
	};
	return Submission;
}

bool FLayoutBackgroundLifecycleSequencer::TryBuildAdmissibilityPreflightSubmissionFromPrewarmResult(
	FString DebugName,
	const uint64 LayoutGroupId,
	const int32 Priority,
	const FLayoutManifestPrewarmResult& PrewarmResult,
	const ELayoutManifestPrewarmKind PrewarmKind,
	FLayoutBackgroundSolveSubmission& OutSubmission,
	FString& OutFailureReason,
	FLayoutAdmissibilityPreflightStageComplete OnComplete)
{
	OutSubmission = FLayoutBackgroundSolveSubmission();
	FLayoutBackgroundAdmissibilityPreflightInput PreflightInput;
	if (!FLayoutManifestPrewarmSequence::TryBuildPreflightInput(
			PrewarmResult,
			PrewarmKind,
			PreflightInput,
			OutFailureReason))
	{
		return false;
	}

	OutSubmission = BuildAdmissibilityPreflightSubmission(
		MoveTemp(DebugName),
		LayoutGroupId,
		Priority,
		MoveTemp(PreflightInput),
		MoveTemp(OnComplete));
	return true;
}

bool FLayoutBackgroundLifecycleSequencer::TryBuildAdmissibilityPreflightSubmissionFromPrewarmCompletion(
	const FLayoutBackgroundSolveCompletion& PrewarmCompletion,
	const FLayoutManifestPrewarmResult& PrewarmResult,
	FString DebugName,
	const uint64 LayoutGroupId,
	const int32 Priority,
	const ELayoutManifestPrewarmKind PrewarmKind,
	FLayoutBackgroundSolveSubmission& OutSubmission,
	FString& OutFailureReason,
	FLayoutAdmissibilityPreflightStageComplete OnComplete)
{
	OutSubmission = FLayoutBackgroundSolveSubmission();
	OutFailureReason.Reset();
	if (PrewarmCompletion.LifecycleStage != ELayoutBackgroundSolveLifecycleStage::ManifestPrewarm)
	{
		OutFailureReason = TEXT("Cheap admissibility sequencing requires a manifest-prewarm completion.");
		return false;
	}
	if (PrewarmCompletion.bCanceled)
	{
		OutFailureReason = TEXT("Cheap admissibility sequencing rejected a canceled manifest-prewarm completion.");
		return false;
	}
	if (!PrewarmCompletion.bWorkSucceeded)
	{
		OutFailureReason = PrewarmCompletion.FailureReason.IsEmpty()
			? TEXT("Cheap admissibility sequencing rejected a failed manifest-prewarm completion.")
			: PrewarmCompletion.FailureReason;
		return false;
	}

	return TryBuildAdmissibilityPreflightSubmissionFromPrewarmResult(
		MoveTemp(DebugName),
		LayoutGroupId,
		Priority,
		PrewarmResult,
		PrewarmKind,
		OutSubmission,
		OutFailureReason,
		MoveTemp(OnComplete));
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightSubmission(
	FLayoutBackgroundSolveDispatcher* Dispatcher,
	FString PrewarmDebugName,
	FString PreflightDebugName,
	const uint64 LayoutGroupId,
	const int32 PrewarmPriority,
	const int32 PreflightPriority,
	FLayoutManifestPrewarmInput PrewarmInput,
	FLayoutManifestPrewarmStageComplete OnPrewarmComplete,
	FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete)
{
	const ELayoutManifestPrewarmKind PrewarmKind = PrewarmInput.Kind;
	return BuildManifestPrewarmSubmission(
		MoveTemp(PrewarmDebugName),
		LayoutGroupId,
		PrewarmPriority,
		MoveTemp(PrewarmInput),
		[
			Dispatcher,
			PreflightDebugName = MoveTemp(PreflightDebugName),
			LayoutGroupId,
			PreflightPriority,
			PrewarmKind,
			OnPrewarmComplete = MoveTemp(OnPrewarmComplete),
			OnPreflightComplete = MoveTemp(OnPreflightComplete)](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutManifestPrewarmResult& PrewarmResult) mutable
		{
			if (OnPrewarmComplete)
			{
				OnPrewarmComplete(Completion, PrewarmResult);
			}

			FLayoutBackgroundSolveSubmission PreflightSubmission;
			FString FailureReason;
			if (!TryBuildAdmissibilityPreflightSubmissionFromPrewarmCompletion(
					Completion,
					PrewarmResult,
					PreflightDebugName,
					LayoutGroupId,
					PreflightPriority,
					PrewarmKind,
					PreflightSubmission,
					FailureReason,
					MoveTemp(OnPreflightComplete)))
			{
				return;
			}

			Dispatcher->Submit(MoveTemp(PreflightSubmission));
		});
}

bool FLayoutBackgroundLifecycleSequencer::TryBuildSolveSubmissionFromPreflightCompletion(
	const FLayoutBackgroundSolveCompletion& PreflightCompletion,
	const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
	FLayoutBackgroundSolveSubmission AlreadyFrozenSolveSubmission,
	FLayoutBackgroundSolveSubmission& OutSubmission,
	FString& OutFailureReason)
{
	OutSubmission = FLayoutBackgroundSolveSubmission();
	OutFailureReason.Reset();
	if (PreflightCompletion.LifecycleStage != ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight)
	{
		OutFailureReason = TEXT("Solve sequencing requires an admissibility-preflight completion.");
		return false;
	}
	if (PreflightCompletion.bCanceled)
	{
		OutFailureReason = TEXT("Solve sequencing rejected a canceled admissibility-preflight completion.");
		return false;
	}
	if (!PreflightCompletion.bWorkSucceeded)
	{
		OutFailureReason = PreflightCompletion.FailureReason.IsEmpty()
			? TEXT("Solve sequencing rejected a failed admissibility-preflight completion.")
			: PreflightCompletion.FailureReason;
		return false;
	}
	if (!PreflightResult.bAdmissible)
	{
		OutFailureReason = PreflightResult.FailureReason.IsEmpty()
			? TEXT("Solve sequencing rejected a non-admissible preflight result.")
			: PreflightResult.FailureReason;
		return false;
	}
	if (!AlreadyFrozenSolveSubmission.Work)
	{
		OutFailureReason = TEXT("Solve sequencing requires an already-frozen solve submission with worker body.");
		return false;
	}

	AlreadyFrozenSolveSubmission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Solve;
	OutSubmission = MoveTemp(AlreadyFrozenSolveSubmission);
	return true;
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildAdmissibilityPreflightThenSolveSubmission(
	FLayoutBackgroundSolveDispatcher* Dispatcher,
	FString PreflightDebugName,
	const uint64 LayoutGroupId,
	const int32 PreflightPriority,
	FLayoutBackgroundAdmissibilityPreflightInput PreflightInput,
	FLayoutBackgroundSolveSubmission AlreadyFrozenSolveSubmission,
	FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete)
{
	return BuildAdmissibilityPreflightSubmission(
		MoveTemp(PreflightDebugName),
		LayoutGroupId,
		PreflightPriority,
		MoveTemp(PreflightInput),
		[
			Dispatcher,
			AlreadyFrozenSolveSubmission = MoveTemp(AlreadyFrozenSolveSubmission),
			OnPreflightComplete = MoveTemp(OnPreflightComplete)](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult) mutable
		{
			if (OnPreflightComplete)
			{
				OnPreflightComplete(Completion, PreflightResult);
			}

			FLayoutBackgroundSolveSubmission SolveSubmission;
			FString FailureReason;
			if (!TryBuildSolveSubmissionFromPreflightCompletion(
					Completion,
					PreflightResult,
					MoveTemp(AlreadyFrozenSolveSubmission),
					SolveSubmission,
					FailureReason))
			{
				return;
			}

			Dispatcher->Submit(MoveTemp(SolveSubmission));
		});
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildAdmissibilityPreflightThenSolveSubmission(
	FLayoutBackgroundSolveDispatcher* Dispatcher,
	FString PreflightDebugName,
	const uint64 LayoutGroupId,
	const int32 PreflightPriority,
	FLayoutBackgroundAdmissibilityPreflightInput PreflightInput,
	FLayoutFrozenSolveSubmissionFactory SolveSubmissionFactory,
	FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete)
{
	return BuildAdmissibilityPreflightSubmission(
		MoveTemp(PreflightDebugName),
		LayoutGroupId,
		PreflightPriority,
		MoveTemp(PreflightInput),
		[
			Dispatcher,
			SolveSubmissionFactory = MoveTemp(SolveSubmissionFactory),
			OnPreflightComplete = MoveTemp(OnPreflightComplete)](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult) mutable
		{
			if (OnPreflightComplete)
			{
				OnPreflightComplete(Completion, PreflightResult);
			}

			FLayoutBackgroundSolveSubmission SolveSubmission;
			FString FailureReason;
			if (Completion.LifecycleStage != ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight)
			{
				FailureReason = TEXT("Deferred solve sequencing requires an admissibility-preflight completion.");
				return;
			}
			if (Completion.bCanceled || !Completion.bWorkSucceeded || !PreflightResult.bAdmissible)
			{
				return;
			}
			if (!SolveSubmissionFactory || !SolveSubmissionFactory(Completion, PreflightResult, SolveSubmission, FailureReason))
			{
				UE_LOG(LogTemp, Warning, TEXT("[LayoutPipeline] Solve factory rejected: %s"), *FailureReason);
				return;
			}
			if (!SolveSubmission.Work)
			{
				UE_LOG(LogTemp, Warning, TEXT("[LayoutPipeline] Solve factory produced no work body"));
				return;
			}

			SolveSubmission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Solve;
			Dispatcher->Submit(MoveTemp(SolveSubmission));
		});
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
	FLayoutBackgroundSolveDispatcher* Dispatcher,
	FString PrewarmDebugName,
	FString PreflightDebugName,
	const uint64 LayoutGroupId,
	const int32 PrewarmPriority,
	const int32 PreflightPriority,
	FLayoutManifestPrewarmInput PrewarmInput,
	FLayoutBackgroundSolveSubmission AlreadyFrozenSolveSubmission,
	FLayoutManifestPrewarmStageComplete OnPrewarmComplete,
	FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete,
	FLayoutLifecycleTerminalFailure OnTerminalFailure,
	TSharedPtr<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe> OperationLedger)
{
	FLayoutFrozenSolveSubmissionFactory Factory = [Frozen = MoveTemp(AlreadyFrozenSolveSubmission)](
		const FLayoutBackgroundSolveCompletion& Completion,
		const FLayoutBackgroundAdmissibilityPreflightResult& Result,
		FLayoutBackgroundSolveSubmission& OutSubmission,
		FString& Failure) mutable
	{
		return TryBuildSolveSubmissionFromPreflightCompletion(
			Completion, Result, MoveTemp(Frozen), OutSubmission, Failure);
	};
	return BuildManifestPrewarmThenPreflightThenSolveSubmission(
		Dispatcher, MoveTemp(PrewarmDebugName), MoveTemp(PreflightDebugName), LayoutGroupId,
		PrewarmPriority, PreflightPriority, MoveTemp(PrewarmInput), MoveTemp(Factory),
		MoveTemp(OnPrewarmComplete), MoveTemp(OnPreflightComplete), MoveTemp(OnTerminalFailure),
		MoveTemp(OperationLedger));
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
	FLayoutBackgroundSolveDispatcher* Dispatcher,
	FString PrewarmDebugName,
	FString PreflightDebugName,
	const uint64 LayoutGroupId,
	const int32 PrewarmPriority,
	const int32 PreflightPriority,
	FLayoutManifestPrewarmInput PrewarmInput,
	FLayoutFrozenSolveSubmissionFactory SolveSubmissionFactory,
	FLayoutManifestPrewarmStageComplete OnPrewarmComplete,
	FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete,
	FLayoutLifecycleTerminalFailure OnTerminalFailure,
	TSharedPtr<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe> OperationLedger,
	FString DiagnosticContext)
{
	if (!OperationLedger)
	{
		const FLayoutSolverExecutionSettings& Settings = PrewarmInput.bHasPreSubmitSnapshot
			? PrewarmInput.PreSubmitSnapshot.WorkerSolvePacket.RequestManifest.ExecutionSettings
			: PrewarmInput.FrozenRequestManifest.ExecutionSettings;
		OperationLedger = MakeShared<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe>();
		OperationLedger->MaxWorkUnits = FMath::Max(1, Settings.MaxCandidateAttempts);
		OperationLedger->DeadlineSeconds = Settings.MaxSolveDurationSeconds > 0.0
			? FPlatformTime::Seconds() + Settings.MaxSolveDurationSeconds : 0.0;
	}
	if (!DiagnosticContext.IsEmpty()) OperationLedger->DiagnosticContext = MoveTemp(DiagnosticContext);
	const ELayoutManifestPrewarmKind PrewarmKind = PrewarmInput.Kind;
	const TSharedRef<FLayoutLifecycleTerminalFailure, ESPMode::ThreadSafe> SharedTerminalFailure =
		MakeShared<FLayoutLifecycleTerminalFailure, ESPMode::ThreadSafe>(MoveTemp(OnTerminalFailure));
	FLayoutBackgroundSolveSubmission PrewarmSubmission = BuildManifestPrewarmSubmission(
		MoveTemp(PrewarmDebugName),
		LayoutGroupId,
		PrewarmPriority,
		MoveTemp(PrewarmInput),
		[
			Dispatcher,
			PreflightDebugName = MoveTemp(PreflightDebugName),
			LayoutGroupId,
			PreflightPriority,
			PrewarmKind,
			OperationLedger,
			SolveSubmissionFactory = MoveTemp(SolveSubmissionFactory),
			OnPrewarmComplete = MoveTemp(OnPrewarmComplete),
			OnPreflightComplete = MoveTemp(OnPreflightComplete),
			SharedTerminalFailure](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FLayoutManifestPrewarmResult& PrewarmResult) mutable
		{
			if (OnPrewarmComplete)
			{
				OnPrewarmComplete(Completion, PrewarmResult);
			}

			FLayoutBackgroundSolveSubmission PreflightSubmission;
			FString FailureReason;
			if (!TryBuildAdmissibilityPreflightSubmissionFromPrewarmCompletion(
					Completion,
					PrewarmResult,
					MoveTemp(PreflightDebugName),
					LayoutGroupId,
					PreflightPriority,
					PrewarmKind,
					PreflightSubmission,
					FailureReason,
					[
						Dispatcher,
						OperationLedger,
						SolveSubmissionFactory = MoveTemp(SolveSubmissionFactory),
						OnPreflightComplete = MoveTemp(OnPreflightComplete),
						SharedTerminalFailure](
						const FLayoutBackgroundSolveCompletion& PreflightCompletion,
						const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult) mutable
					{
						if (OnPreflightComplete)
						{
							OnPreflightComplete(PreflightCompletion, PreflightResult);
						}
						if (PreflightCompletion.bCanceled || !PreflightCompletion.bWorkSucceeded || !PreflightResult.bAdmissible)
						{
							if (*SharedTerminalFailure)
							{
								(*SharedTerminalFailure)(
									PreflightCompletion,
									!PreflightCompletion.FailureReason.IsEmpty()
										? PreflightCompletion.FailureReason
										: PreflightResult.FailureReason);
							}
							return;
						}

						FLayoutBackgroundSolveSubmission SolveSubmission;
						FString SolveFailureReason;
						if (!SolveSubmissionFactory || !SolveSubmissionFactory(PreflightCompletion, PreflightResult, SolveSubmission, SolveFailureReason))
						{
							UE_LOG(LogTemp, Warning, TEXT("[LayoutPipeline] Solve factory rejected: %s"), *SolveFailureReason);
							if (*SharedTerminalFailure)
							{
								(*SharedTerminalFailure)(PreflightCompletion, SolveFailureReason);
							}
							return;
						}
						if (!SolveSubmission.Work)
						{
							const FString MissingWorkFailureReason = TEXT("Solve factory produced no work body.");
							UE_LOG(LogTemp, Warning, TEXT("[LayoutPipeline] %s"), *MissingWorkFailureReason);
							if (*SharedTerminalFailure)
							{
								(*SharedTerminalFailure)(PreflightCompletion, MissingWorkFailureReason);
							}
							return;
						}
						SolveSubmission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Solve;
						BindSerialOperationBudget(SolveSubmission, OperationLedger.ToSharedRef());
						Dispatcher->Submit(MoveTemp(SolveSubmission));
					}))
			{
				if (*SharedTerminalFailure)
				{
					(*SharedTerminalFailure)(Completion, FailureReason);
				}
				return;
			}

			BindSerialOperationBudget(PreflightSubmission, OperationLedger.ToSharedRef());
			Dispatcher->Submit(MoveTemp(PreflightSubmission));
		});
	BindSerialOperationBudget(PrewarmSubmission, OperationLedger.ToSharedRef());
	return PrewarmSubmission;
}

FLayoutBackgroundSolveSubmission FLayoutBackgroundLifecycleSequencer::BuildChildProofSubmission(
	FString DebugName,
	uint64 LayoutGroupId,
	int32 Priority,
	const FLayoutChildSolveHandoff& ChildHandoff,
	const FLayoutRegionSolveRequest& ChildProofRequest,
	TFunction<void(const FLayoutBackgroundSolveCompletion& Completion, const FLayoutRegionSolveResult& ChildResult)> OnComplete)
{
	TSharedRef<FLayoutRegionSolveResult, ESPMode::ThreadSafe> SharedResult =
		MakeShared<FLayoutRegionSolveResult, ESPMode::ThreadSafe>();

	FLayoutBackgroundSolveSubmission Submission;
	Submission.DebugName = MoveTemp(DebugName);
	Submission.LayoutGroupId = LayoutGroupId;
	Submission.Tier = ELayoutBackgroundSolveJobTier::ParentResume;
	Submission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::ChildProof;
	Submission.Priority = Priority;
	Submission.Work = [
		ChildHandoff,
		ChildProofRequest,
		SharedResult](
		const FLayoutSolveCancellationToken& CancellationToken,
		FString& OutFailureReason) mutable
	{
		if (CancellationToken.IsCancellationRequested())
		{
			OutFailureReason = TEXT("Child proof canceled.");
			return false;
		}

		FString HandoffFailureReason;
		if (!ChildHandoff.ValidateNoLiveObjectCarriers(HandoffFailureReason))
		{
			SharedResult->RegionDebugPath = ChildProofRequest.RegionDebugPath;
			SharedResult->SourceContentEntryId = ChildProofRequest.SourceContentEntryId;
			FLayoutRegionalFailureRecord& Failure =
				SharedResult->SolveResult.RegionalFailure;
			Failure.Scope = ELayoutRegionalFailureScope::Child;
			Failure.Phase = TEXT("ChildHandoffValidation");
			Failure.ParentRegionDebugPath = ChildHandoff.ParentRegionDebugPath;
			Failure.RegionDebugPath = ChildProofRequest.RegionDebugPath;
			Failure.SourceContentEntryId = ChildProofRequest.SourceContentEntryId;
			Failure.BranchId = ChildHandoff.CertifiedWitnessBundle.BranchId;
			Failure.CertificateId = ChildHandoff.ProofCertificate.CertificateId;
			Failure.FirstCause = HandoffFailureReason;
			OutFailureReason = HandoffFailureReason;
			return false;
		}

		FLayoutRegionSolveResult ChildResult;
		FString ChildFailureReason;
		if (!LayoutProfileSolverInternal::BuildIndependentChildProofResult(
				ChildProofRequest,
				ChildResult,
				ChildFailureReason))
		{
			OutFailureReason = ChildFailureReason;
			*SharedResult = MoveTemp(ChildResult);
			if (SharedResult->SolveResult.RegionalFailure.IsSet())
			{
				SharedResult->SolveResult.RegionalFailure.BranchId =
					ChildHandoff.CertifiedWitnessBundle.BranchId;
				SharedResult->SolveResult.RegionalFailure.CertificateId =
					ChildHandoff.ProofCertificate.CertificateId;
			}
			return false;
		}

		*SharedResult = MoveTemp(ChildResult);
		return true;
	};
	Submission.PublishOnGameThread = [SharedResult, OnComplete = MoveTemp(OnComplete)](
		const FLayoutBackgroundSolveCompletion& Completion)
	{
		if (OnComplete)
		{
			OnComplete(Completion, *SharedResult);
		}
	};

	return Submission;
}

