// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Async/LayoutManifestPrewarm.h"
#include "Layout/Async/LayoutParentResumeArtifact.h"
#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutSolveExecution { struct FWorkLedger; }

/** Completion callback for one manifest-prewarm stage that already had frozen manifest input. */
using FLayoutManifestPrewarmStageComplete = TFunction<void(
	const FLayoutBackgroundSolveCompletion& Completion,
	const FLayoutManifestPrewarmResult& PrewarmResult)>;

/** Completion callback for one cheap admissibility-preflight stage. */
using FLayoutAdmissibilityPreflightStageComplete = TFunction<void(
	const FLayoutBackgroundSolveCompletion& Completion,
	const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult)>;

/** Reports one terminal lifecycle rejection when no solve submission can be queued. */
using FLayoutLifecycleTerminalFailure = TFunction<void(
	const FLayoutBackgroundSolveCompletion& Completion,
	const FString& FailureReason)>;

/** Builds one solve submission after successful scout/preflight publication has finalized descriptor storage. */
using FLayoutFrozenSolveSubmissionFactory = TFunction<bool(
	const FLayoutBackgroundSolveCompletion& PreflightCompletion,
	const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
	FLayoutBackgroundSolveSubmission& OutSubmission,
	FString& OutFailureReason)>;

/** Builds background-only submissions for manifest-prewarm and admissibility-preflight lifecycle stages. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutBackgroundLifecycleSequencer
{
public:
	/** Creates one fail-closed manifest-prewarm submission from already-frozen manifest input. */
	static FLayoutBackgroundSolveSubmission BuildManifestPrewarmSubmission(
		FString DebugName,
		uint64 LayoutGroupId,
		int32 Priority,
		FLayoutManifestPrewarmInput PrewarmInput,
		FLayoutManifestPrewarmStageComplete OnComplete = FLayoutManifestPrewarmStageComplete());

	/** Creates one fail-closed cheap-admissibility submission from already-frozen preflight input. */
	static FLayoutBackgroundSolveSubmission BuildAdmissibilityPreflightSubmission(
		FString DebugName,
		uint64 LayoutGroupId,
		int32 Priority,
		FLayoutBackgroundAdmissibilityPreflightInput PreflightInput,
		FLayoutAdmissibilityPreflightStageComplete OnComplete = FLayoutAdmissibilityPreflightStageComplete());

	/** Converts one successful prewarm result into a cheap-admissibility submission without touching hot-submit state. */
	static bool TryBuildAdmissibilityPreflightSubmissionFromPrewarmResult(
		FString DebugName,
		uint64 LayoutGroupId,
		int32 Priority,
		const FLayoutManifestPrewarmResult& PrewarmResult,
		ELayoutManifestPrewarmKind PrewarmKind,
		FLayoutBackgroundSolveSubmission& OutSubmission,
		FString& OutFailureReason,
		FLayoutAdmissibilityPreflightStageComplete OnComplete = FLayoutAdmissibilityPreflightStageComplete());

	/** Converts a published successful prewarm completion into the next cheap-admissibility submission. */
	static bool TryBuildAdmissibilityPreflightSubmissionFromPrewarmCompletion(
		const FLayoutBackgroundSolveCompletion& PrewarmCompletion,
		const FLayoutManifestPrewarmResult& PrewarmResult,
		FString DebugName,
		uint64 LayoutGroupId,
		int32 Priority,
		ELayoutManifestPrewarmKind PrewarmKind,
		FLayoutBackgroundSolveSubmission& OutSubmission,
		FString& OutFailureReason,
		FLayoutAdmissibilityPreflightStageComplete OnComplete = FLayoutAdmissibilityPreflightStageComplete());

	/** Creates a manifest-prewarm submission whose publish consumer queues cheap preflight after successful prewarm. */
	static FLayoutBackgroundSolveSubmission BuildManifestPrewarmThenPreflightSubmission(
		FLayoutBackgroundSolveDispatcher* Dispatcher,
		FString PrewarmDebugName,
		FString PreflightDebugName,
		uint64 LayoutGroupId,
		int32 PrewarmPriority,
		int32 PreflightPriority,
		FLayoutManifestPrewarmInput PrewarmInput,
		FLayoutManifestPrewarmStageComplete OnPrewarmComplete = FLayoutManifestPrewarmStageComplete(),
		FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete = FLayoutAdmissibilityPreflightStageComplete());

	/** Converts successful cheap preflight completion into one already-frozen solve submission. */
	static bool TryBuildSolveSubmissionFromPreflightCompletion(
		const FLayoutBackgroundSolveCompletion& PreflightCompletion,
		const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
		FLayoutBackgroundSolveSubmission AlreadyFrozenSolveSubmission,
		FLayoutBackgroundSolveSubmission& OutSubmission,
		FString& OutFailureReason);

	/** Creates cheap-preflight submission whose publish consumer queues a caller-provided already-frozen solve. */
	static FLayoutBackgroundSolveSubmission BuildAdmissibilityPreflightThenSolveSubmission(
		FLayoutBackgroundSolveDispatcher* Dispatcher,
		FString PreflightDebugName,
		uint64 LayoutGroupId,
		int32 PreflightPriority,
		FLayoutBackgroundAdmissibilityPreflightInput PreflightInput,
		FLayoutBackgroundSolveSubmission AlreadyFrozenSolveSubmission,
		FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete = FLayoutAdmissibilityPreflightStageComplete());

	/** Creates cheap-preflight submission whose publish consumer finalizes descriptor storage, then queues a lookup-built solve. */
	static FLayoutBackgroundSolveSubmission BuildAdmissibilityPreflightThenSolveSubmission(
		FLayoutBackgroundSolveDispatcher* Dispatcher,
		FString PreflightDebugName,
		uint64 LayoutGroupId,
		int32 PreflightPriority,
		FLayoutBackgroundAdmissibilityPreflightInput PreflightInput,
		FLayoutFrozenSolveSubmissionFactory SolveSubmissionFactory,
		FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete = FLayoutAdmissibilityPreflightStageComplete());

	/** Queues serial prewarm, preflight and frozen solve stages under one execution allowance.
	 * An optional caller-owned ledger includes pre-submit work. It must have no concurrent users;
	 * stage completion transfers exclusive access, and frozen requests/proofs never own this state. */
	static FLayoutBackgroundSolveSubmission BuildManifestPrewarmThenPreflightThenSolveSubmission(
		FLayoutBackgroundSolveDispatcher* Dispatcher,
		FString PrewarmDebugName,
		FString PreflightDebugName,
		uint64 LayoutGroupId,
		int32 PrewarmPriority,
		int32 PreflightPriority,
		FLayoutManifestPrewarmInput PrewarmInput,
		FLayoutBackgroundSolveSubmission AlreadyFrozenSolveSubmission,
		FLayoutManifestPrewarmStageComplete OnPrewarmComplete = FLayoutManifestPrewarmStageComplete(),
		FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete = FLayoutAdmissibilityPreflightStageComplete(),
		FLayoutLifecycleTerminalFailure OnTerminalFailure = FLayoutLifecycleTerminalFailure(),
		TSharedPtr<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe> OperationLedger = nullptr);

	/** Queues serial prewarm, preflight and lookup-built solve under the same execution-only
	 * ledger contract as the frozen-solve overload; queued time consumes the absolute deadline.
	 * Nonempty DiagnosticContext enables bounded, request-linked logs without changing frozen inputs. */
	static FLayoutBackgroundSolveSubmission BuildManifestPrewarmThenPreflightThenSolveSubmission(
		FLayoutBackgroundSolveDispatcher* Dispatcher,
		FString PrewarmDebugName,
		FString PreflightDebugName,
		uint64 LayoutGroupId,
		int32 PrewarmPriority,
		int32 PreflightPriority,
		FLayoutManifestPrewarmInput PrewarmInput,
		FLayoutFrozenSolveSubmissionFactory SolveSubmissionFactory,
		FLayoutManifestPrewarmStageComplete OnPrewarmComplete = FLayoutManifestPrewarmStageComplete(),
		FLayoutAdmissibilityPreflightStageComplete OnPreflightComplete = FLayoutAdmissibilityPreflightStageComplete(),
		FLayoutLifecycleTerminalFailure OnTerminalFailure = FLayoutLifecycleTerminalFailure(),
		TSharedPtr<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe> OperationLedger = nullptr,
		FString DiagnosticContext = FString());

	/** Builds one child proof submission from a frozen child solve handoff. */
	static FLayoutBackgroundSolveSubmission BuildChildProofSubmission(
		FString DebugName,
		uint64 LayoutGroupId,
		int32 Priority,
		const FLayoutChildSolveHandoff& ChildHandoff,
		const FLayoutRegionSolveRequest& ChildProofRequest,
		TFunction<void(const FLayoutBackgroundSolveCompletion& Completion, const FLayoutRegionSolveResult& ChildResult)> OnComplete);
};
