// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "LayoutRegionSchedulePlacementBridgeContext.h"

/**
 * Deferred complete-validation interface for the extracted placement-bridge hot path.
 */
namespace LayoutRegionScheduleSolverPrivate
{
	struct FRecursiveSchedulerPerfMetrics;

	/**
	 * Queue-time aggregate features for deferred candidate ordering and diagnostics.
	 *
	 * Keep these features explicit so queue policy stops depending on ad-hoc
	 * schedule walks and the next upstream selection cut has a first-class
	 * carrier to extend.
	 */
	struct FDeferredValidationParentRouteDemandFeatures
	{
		int32 RequiredRouteConstraintCount = 0;
		int32 MainRouteConstraintCount = 0;
		int32 BoundaryRouteConstraintCount = 0;
		int32 InteriorRouteConstraintCount = 0;
		int32 ReservedRouteConstraintCount = 0;
		int32 ReservedMainRouteConstraintCount = 0;
		int32 ReservedBoundaryRouteConstraintCount = 0;
		int32 ReservedInteriorRouteConstraintCount = 0;
		int32 ReservedRouteFaceRequirementCount = 0;
		int32 ReservedRouteContactAdjacencyCount = 0;
		int32 ReservedRouteTraversalAdjacencyCount = 0;
	};

	struct FDeferredValidationCandidateSelectionFeatures
	{
		int32 WarningCount = 0;
		int32 SkippedOptionalCount = 0;
		FDeferredValidationParentRouteDemandFeatures ParentRouteDemand;
		TArray<FChildPlacementSelectionFeatures, TInlineAllocator<2>>
			Placements;
	};

	/** Deferred-validation candidate kept after recursive search succeeds structurally. */
	struct FPlacementBridgeDeferredValidationCandidate
	{
		int32 Score = MIN_int32 / 4;
		int32 RawScore = MIN_int32 / 4;
		int32 OrderingPenalty = 0;
		uint32 TieBreakHash = 0;
		FDeferredValidationCandidateSelectionFeatures SelectionFeatures;
		FCommittedRecursiveScheduleState Schedule;
	};

	/** Memo entry used by deferred complete-validation dedupe. */
	struct FPlacementBridgeDeferredValidationMemoEntry
	{
		bool bSucceeded = false;
		FString FailureReason;
	};

	/** Diagnostic row describing one retained deferred-validation candidate. */
	struct FDeferredValidationCandidateOrderingRow
	{
		int32 Score = MIN_int32 / 4;
		int32 RawScore = MIN_int32 / 4;
		int32 OrderingPenalty = 0;
		uint32 TieBreakHash = 0;
		int32 PlacementCount = 0;
		int32 DirectPlacementCount = 0;
		int32 SiblingOnlyPlacementCount = 0;
		int32 ParentContactCount = 0;
		int32 TraversalBoundaryPointCount = 0;
		int32 TraversalBoundaryCellCount = 0;
		int32 MultiTraversalBoundaryCellCount = 0;
		int32 BoundaryFacingTraversalPointCount = 0;
		int32 NegotiatedRouteCellCount = 0;
		int32 NegotiatedRouteSpanLevelCount = 0;
		int32 NegotiatedInterfaceTraversalAnchorCount = 0;
		int32 RequiredRouteConstraintCount = 0;
		int32 MainRouteConstraintCount = 0;
		int32 BoundaryRouteConstraintCount = 0;
		int32 InteriorRouteConstraintCount = 0;
		int32 ReservedRouteConstraintCount = 0;
		int32 ReservedMainRouteConstraintCount = 0;
		int32 ReservedBoundaryRouteConstraintCount = 0;
		int32 ReservedInteriorRouteConstraintCount = 0;
		int32 ReservedRouteFaceRequirementCount = 0;
		int32 ReservedRouteContactAdjacencyCount = 0;
		int32 ReservedRouteTraversalAdjacencyCount = 0;
		int32 WarningCount = 0;
		int32 SkippedOptionalCount = 0;
		bool bHasScoreBreakdown = false;
		int32 PreAdjustmentResolvedScore = 0;
		int32 PreAdjustmentEntryAccumulatedScore = 0;
		int32 PreAdjustmentResolveAdjustment = 0;
		int32 DirectParentSupportSaturationPenalty = 0;
		int32 EntryIntentScore = 0;
		int32 EntryParentCapabilitySupportContribution = 0;
		int32 EntryParentSupportContribution = 0;
		int32 EntryProtectedTraversalPenalty = 0;
		int32 EntryAnchoredIntentPenalty = 0;
		int32 EntryPriorityDistancePenalty = 0;
		int32 EntryNoParentResolutionScore = 0;
		int32 DirectParentEntryCount = 0;
		int32 NoParentEntryCount = 0;
		int32 AdjustmentTotal = 0;
		int32 ProbeRouteConstraintScore = 0;
		int32 VerticalLevelPenalty = 0;
		int32 BoundaryTouchPenalty = 0;
		int32 ShellAdjacencyPenalty = 0;
		int32 ParentChildSeamScore = 0;
		int32 RequiredCopiesNoSeamPenalty = 0;
		int32 SiblingSeamScore = 0;
		int32 RootDistancePenalty = 0;
		int32 ProviderDistancePenalty = 0;
		int32 ParentBoundaryTouchCount = 0;
		int32 ParentShellAdjacencyCount = 0;
		FString CommittedContactFaceSignature;
		FString NormalizedContactSignature;
		FString NormalizedTraversalAnchorSignature;
		FString NegotiatedRouteDemandSignature;
		FString PlacementSignature;
	};

	/** Diagnostic summary for deferred candidate ordering under the existing perf flag. */
	struct FDeferredValidationQueuedFamilySummary
	{
		int32 ParentContactCount = 0;
		int32 CandidateCount = 0;
		int32 BestRank = INDEX_NONE;
		FDeferredValidationCandidateOrderingRow BestCandidate;
	};

	/** Diagnostic summary for deferred candidate ordering under the existing perf flag. */
	struct FDeferredValidationCandidateOrderingSummary
	{
		int32 QueuedCount = 0;
		int32 RetainedCount = 0;
		int32 DistinctRetainedScoreCount = 0;
		int32 TopScoreTieCount = 0;
		int32 BestScore = MIN_int32 / 4;
		int32 CutoffScore = MIN_int32 / 4;
		int32 ValidatedCandidateCount = 0;
		int32 SucceededRank = INDEX_NONE;
		bool bSucceeded = false;
		FDeferredValidationCandidateOrderingRow SuccessfulCandidate;
		TArray<FDeferredValidationCandidateOrderingRow> TopCandidates;
		TArray<FDeferredValidationQueuedFamilySummary> QueuedFamiliesByParentContact;
	};

	/** Final resolution surface returned after deferred validation inspects queued schedules. */
	struct FPlacementBridgeDeferredValidationResult
	{
		bool bSucceeded = false;
		int32 BestScore = MIN_int32 / 4;
		FCommittedRecursiveScheduleState BestSchedule;
		FString FailureReason;
	};

	/** Queues one structurally valid candidate for later deferred complete validation. */
	void QueueDeferredCompletePlacementCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const FString& ParentRegionDebugPath,
		const FIntPoint& ParentFootprintSize,
		int32 Score,
		const TArray<FAutomaticChildPlacement>& Placements,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FSkippedOptionalChildPlacement>& SkippedOptionalPlacements,
		const TArray<FLayoutValidationMessage>& Warnings,
		bool bHasPreparedParentProofRequest,
		const FLayoutRegionSolveRequest* PreparedParentProofRequest,
		TArray<FPlacementBridgeDeferredValidationCandidate>& InOutCandidates,
		TSet<FString>& InOutCandidateKeys);

	/** Resolves the surviving deferred-validation candidates with the supplied proof callback. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FPlacementBridgeDeferredValidationResult ResolveDeferredCompletePlacementCandidates(
		const FString& RegionDebugPath,
		TArray<FPlacementBridgeDeferredValidationCandidate>& InOutCandidates,
		int32 MaxCandidateCount,
		FDeferredValidationCandidateOrderingSummary* OutOrderingSummary,
		FRecursiveSchedulerPerfMetrics* InOutPerfMetrics,
		TFunctionRef<bool(FCommittedRecursiveScheduleState&, FString&)> ValidateCandidate);

	/** Emits the retained deferred-validation ordering summary under the existing recursive perf flag. */
	void LogDeferredValidationCandidateOrderingProfile(
		const FString& RegionDebugPath,
		const FDeferredValidationCandidateOrderingSummary& Summary);

}
