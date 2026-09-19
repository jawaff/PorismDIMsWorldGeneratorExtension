// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "LayoutRegionSchedulePlacementBridgeContext.h"

/**
 * Canonical parent-proof interface for the extracted placement-bridge hot path.
 */
namespace LayoutRegionScheduleSolverPrivate
{
	/** One parent-probe candidate assembled by the bridge before proof execution. */
	struct FPlacementBridgeParentProbeCandidate
	{
		FString SchedulerStateKey;
		int32 VariantIndex = INDEX_NONE;
		FPlacementBridgePlanningVariantView PlanningVariant;
		FIntVector CandidateOffset = FIntVector::ZeroValue;
		TArray<FLayoutCommittedEndpointAnchor> CandidateCommitments;
		TArray<FLayoutCommittedEndpointAnchor> DirectParentContactCommitments;
		TArray<FIntVector> CandidateParentContactCells;
		TArray<FLayoutCommittedTraversalAnchor> CandidateParentTraversalAnchors;
		bool bAllowsChildTraversalBridgeForCommittedContacts = false;
		TArray<FIntVector> CandidateParentPlanReservedCells;
		TArray<FSharedParentChildFace> CandidateSharedParentChildFaces;
		/** Exact boundary certificate proving which adjacent parent cells may remain empty. */
		FLayoutId CandidateBoundaryCertificateId;
		/** Filled parent domains required by the exact child boundary certificate. */
		TArray<FLayoutCellCandidateDomainRestriction> CandidateParentDomainRestrictions;
		bool bDeferChildTraversalValidationToSchedule = false;
		bool bRequireStructuralFeasibilityPrecheck = false;
		bool bRunImmediateFullParentProof = false;
		bool bDeferFullParentProofToCompleteValidation = false;
	};

	/** Result surface returned by canonical parent-proof evaluation. */
	struct FPlacementBridgeParentProbeResult
	{
		bool bSucceeded = false;
		bool bFailedDuringChildProbe = false;
		bool bUsedLocalMemo = false;
		bool bUsedSharedSolveMemo = false;
		bool bRanImmediateFullParentProof = false;
		bool bExecutedImmediateFullParentProofSolve = false;
		bool bFailedStructuralFeasibility = false;
		double StructuralFeasibilitySeconds = 0.0;
		double ParentProofSolveSeconds = 0.0;
		int32 ScoreAdjustment = 0;
		FString FailureReason;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedFutureTerraceBoundaryPoints;
		TArray<FLayoutCommittedTraversalAnchor> ParentCommittedTraversalAnchors;
	};

	/** Memo entry used by bridge orchestration and any shared canonical parent-proof cache. */
	struct FPlacementBridgeParentProbeMemoEntry
	{
		bool bSucceeded = false;
		FPlacementBridgeParentProbeResult Result;
	};

	/** Memo entry for the scheduler-local structural feasibility precheck. */
	struct FPlacementBridgeStructuralFeasibilityMemoEntry
	{
		bool bSucceeded = false;
		FString FailureReason;
	};

	/**
	 * Reusable parent-probe preparation output. This holds the expensive
	 * geometry, residual-plan, and proof-request staging data so proof policy can
	 * be decided separately from preparation.
	 */
	struct FPreparedParentProbeCandidate
	{
		FString EvaluationMemoKey;
		FPlacementBridgeParentProbeCandidate Candidate;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedFutureTerraceBoundaryPoints;
		TArray<FLayoutCommittedTraversalAnchor> ParentCommittedTraversalAnchors;
		FCommittedVerticalAccessOwnership VerticalAccessOwnership;
		FLayoutRegionSolveRequest PreparedProofRequest;
		FString StructuralMemoKey;
	};

	/** Exact residual-parent plan already settled while checking the same child commitment. */
	struct FPreparedParentProbeResidualPlan
	{
		FCommittedVerticalAccessOwnership VerticalAccessOwnership;
		TArray<FLayoutPlannedCell> PlannedCells;
		TArray<FLayoutVerticalAccessHostGroup> FilteredVerticalAccessHostGroups;
	};

	/** Builds the local evaluation fingerprint for one parent-probe candidate. */
	FString BuildParentProbeEvaluationMemoKey(
		const FPlacementBridgeParentProbeCandidate& Candidate);

	/** Builds the canonical solve-request fingerprint for shared parent-proof reuse, including failed probes. */
	FString BuildCanonicalParentProbeSolveRequestMemoKey(
		const FLayoutRegionSolveRequest& ParentProbeRequest);

	/** Derives the deterministic seed used by canonical shared parent-proof solve requests. */
	int32 BuildCanonicalParentProbeSolveSeed(
		const FString& ParentProbeSolveMemoKey);

	/**
	 * Builds the reusable parent-probe preparation state for one candidate
	 * without running the final parent proof.
	 */
	bool TryPrepareParentProbeCandidate(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPlacementBridgeParentProbeCandidate& Candidate,
		FPreparedParentProbeCandidate& OutPreparedCandidate,
		FPlacementBridgeParentProbeResult& OutResult,
		const FPreparedParentProbeResidualPlan* PreparedResidualPlan = nullptr);

	/**
	 * Runs the reusable structural feasibility gate for a prepared candidate
	 * before the final parent proof request is assembled.
	 */
	bool EvaluatePreparedParentProbeStructuralFeasibility(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPreparedParentProbeCandidate& PreparedCandidate,
		TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
		FPlacementBridgeParentProbeResult& InOutResult);

	/**
	 * Evaluates a previously prepared parent-probe candidate, including local
	 * memo reuse, structural feasibility, and optional immediate proof execution.
	 */
	bool EvaluatePreparedParentProbeCandidate(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPreparedParentProbeCandidate& PreparedCandidate,
		TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>& InOutLocalEvaluationMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>* SharedSolveMemo,
		FPlacementBridgeParentProbeResult& OutResult);

	/**
	 * Builds the final parent-proof solve request from a prepared candidate after
	 * proof policy has decided a full solve is required.
	 */
	void BuildPreparedParentProofRequest(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPreparedParentProbeCandidate& PreparedCandidate,
		const FPlacementBridgeParentProbeResult& ProbeResult,
		FLayoutRegionSolveRequest& OutRequest);

	/**
	 * Runs the final parent proof for a prepared request, including shared solve
	 * memo reuse and committed-anchor coverage validation.
	 */
	bool RunPreparedParentProof(
		const FPlacementBridgeSolveContext& SolveContext,
		const FLayoutRegionSolveRequest& PreparedRequest,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>* SharedSolveMemo,
		FPlacementBridgeParentProbeResult& InOutResult);

	/**
	 * Evaluates one assembled parent-probe candidate against the immutable bridge
	 * context and the current mutable schedule state.
	 */
	bool EvaluateParentProbe(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPlacementBridgeParentProbeCandidate& Candidate,
		TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>& InOutLocalEvaluationMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>* SharedSolveMemo,
		FPlacementBridgeParentProbeResult& OutResult);
}
