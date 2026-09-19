// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "LayoutRegionScheduleDeferredValidation.h"
#include "LayoutRegionScheduleSolverFacade.h"
#include "LayoutProfileSolverInternal.h"
#include "LayoutRegionScheduleParentProbe.h"

/**
 * Shared recursive-scheduler runtime carriers for the extracted bridge split.
 *
 * This header intentionally owns only the scheduler/search state shape so the
 * bridge can stop being the only place where extraction-safe runtime types
 * exist before behavior moves into a dedicated runtime implementation file.
 */
namespace LayoutRegionScheduleSolverPrivate
{
	using FParentProbeEvaluation = FPlacementBridgeParentProbeResult;
	using FParentProbeEvaluationMemo = FPlacementBridgeParentProbeMemoEntry;
	using FParentStructuralFeasibilityMemo = FPlacementBridgeStructuralFeasibilityMemoEntry;
	using FPlacementValidationMemo = FPlacementBridgeDeferredValidationMemoEntry;
	using FDeferredCompletePlacementCandidate = FPlacementBridgeDeferredValidationCandidate;
	using FChildPlanningVariantInterfaceSummary = FPlacementBridgeChildInterfaceSummary;

	struct FCanonicalChildPlacementChoice
	{
		bool bSet = false;
		FIntVector Offset = FIntVector::ZeroValue;
		int32 VariantIndex = INDEX_NONE;
	};

	struct FChildPlacementDemand
	{
		int32 SourceIndex = INDEX_NONE;
		/** Interchangeable prepared child sources for one counted feature slot. */
		TArray<int32> AlternativeSourceIndices;
		int32 InstanceOrdinal = 0;
		bool bOptional = false;
		/** True when absence delegates this slot's residual count to parent module search, not authored optional drop policy. */
		bool bSharedFeatureChoiceSlot = false;
		/** Parent placement zone required by the counted feature slot. */
		ELayoutPlacementZone RequiredPlacementZone = ELayoutPlacementZone::Any;
		/** Stable disconnected zone-component index required when demand count covers every canonical component. */
		int32 RequiredPlacementZoneComponentIndex = INDEX_NONE;
		FName RequirementId;
		FString ChildRegionDebugPath;
	};

	/**
	 * Runtime-owned prepared parent-proof bundle captured during commitment
	 * evaluation so confirmation can reuse parent-probe preparation instead of
	 * rebuilding the same bridge-shaped inputs.
	 */
	struct FPreparedChildParentProofCandidate
	{
		FPreparedParentProbeCandidate PreparedParentProbeCandidate;
		TArray<FLayoutCommittedEndpointAnchor> DirectParentContactCommitments;
		TArray<FSharedParentChildFace> EffectiveSharedParentChildFaces;
		bool bAllowsChildTraversalBridgeForCommittedContacts = false;
		bool bSiblingOnlyNoContactAttachment = false;
		bool bCanDeferRecursiveParentProbeFailure = false;
		ELayoutNegotiatedHostVerticalAccessResponsibility NegotiatedHostVerticalAccessResponsibility =
			ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
		bool bHasRequiredHostIngressAnchor = false;
		FLayoutCommittedEndpointAnchor RequiredHostIngressAnchor;
		bool bHasRequiredHostEgressAnchor = false;
		FLayoutCommittedEndpointAnchor RequiredHostEgressAnchor;
		TArray<FIntVector> RequiredChildInternalVerticalRouteCells;
	};

	struct FScoredChildOffset
	{
		int32 VariantIndex = INDEX_NONE;
		FIntVector Offset = FIntVector::ZeroValue;
		FLayoutChildStageMappingResult StageMapping;
		TArray<FLayoutCommittedEndpointAnchor> Commitments;
		/** Stable reciprocal boundary-domain witnesses selected for direct parent contacts. */
		TArray<FLayoutId> BoundaryWitnessIds;
		FLayoutId BoundaryCertificateId;
		TArray<FLayoutCellCandidateDomainRestriction> ParentDomainRestrictions;
		TArray<FLayoutCellCandidateDomainRestriction> ChildDomainRestrictions;
		TArray<FLayoutSolveBoundaryPoint> ChildIncomingBoundaryPoints;
		TArray<FLayoutCommittedEndpointAnchor> DirectParentContactCommitments;
		TArray<FIntVector> ParentContactCells;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
		TArray<FLayoutCommittedTraversalAnchor> ParentCommittedTraversalAnchors;
		bool bAllowsChildTraversalBridgeForCommittedContacts = false;
		bool bTouchesParentOuterShell = false;
		TArray<FIntVector> ParentPlanReservedCells;
		TArray<FSharedParentChildFace> SharedParentChildFaces;
		ELayoutNegotiatedHostVerticalAccessResponsibility NegotiatedHostVerticalAccessResponsibility =
			ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
		bool bHasRequiredHostIngressAnchor = false;
		FLayoutCommittedEndpointAnchor RequiredHostIngressAnchor;
		bool bHasRequiredHostEgressAnchor = false;
		FLayoutCommittedEndpointAnchor RequiredHostEgressAnchor;
		TArray<FIntVector> RequiredChildInternalVerticalRouteCells;
		/** Exact parent-only proof request prepared during candidate evaluation for reuse if this offset is later accepted. */
		bool bHasDeferredPreparedParentProofRequest = false;
		/** Request-local parent proof surface paired with this candidate's accepted support/interface contract. */
		FLayoutRegionSolveRequest DeferredPreparedParentProofRequest;
		int32 Score = MIN_int32 / 4;
		uint32 TieBreakHash = 0;
		FChildPlacementScoreBreakdown ScoreBreakdown;
	};

	struct FCheapChildPlacementCandidateOrder
	{
		int32 CandidateIndex = INDEX_NONE;
		FIntVector Offset = FIntVector::ZeroValue;
		int32 Score = MIN_int32 / 4;
		int32 MagnetismScore = 0;
		uint32 TieBreakHash = 0;
	};

	/** Scores only exact reciprocal parent/child seam witnesses for cheap and final placement ordering. */
	PORISMDIMSWORLDGENERATOREXTENSION_API int32 ScoreSharedParentChildSeamOpportunity(
		const TArray<FSharedParentChildFace>& SharedParentChildFaces);

	/** Applies seeded coarse ordering, replacing only nearby coarse values with a stronger exact-seam snap. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void OrderSeededChildPlacementCandidatesWithLocalSnap(
		TArray<FCheapChildPlacementCandidateOrder>& InOutCandidates);

	struct FRemainingDemandPropagationMemo
	{
		bool bSucceeded = false;
		FString FailureReason;
	};

	/** Aggregated proof-attempt counts for one resolved commitment family shape. */
	struct FRecursiveSchedulerCombinationFamilyStats
	{
		int32 EntryCount = 0;
		int32 ParentContactCount = 0;
		int32 DirectContactCount = 0;
		int32 SiblingSharedDoorCount = 0;
		int32 ParentSharedDoorCount = 0;
		int32 EnumeratedCount = 0;
		int32 StructuralRejectCount = 0;
		int32 FullProofAttemptCount = 0;
		int32 FullProofSuccessCount = 0;
	};

	/** Structural key for one resolved commitment family shape. */
	struct FRecursiveSchedulerCombinationFamilyKey
	{
		int32 EntryCount = 0;
		int32 ParentContactCount = 0;
		int32 DirectContactCount = 0;
		int32 SiblingSharedDoorCount = 0;
		int32 ParentSharedDoorCount = 0;

		friend bool operator==(
			const FRecursiveSchedulerCombinationFamilyKey& Left,
			const FRecursiveSchedulerCombinationFamilyKey& Right)
		{
			return Left.EntryCount == Right.EntryCount
				&& Left.ParentContactCount == Right.ParentContactCount
				&& Left.DirectContactCount == Right.DirectContactCount
				&& Left.SiblingSharedDoorCount == Right.SiblingSharedDoorCount
				&& Left.ParentSharedDoorCount == Right.ParentSharedDoorCount;
		}

		friend uint32 GetTypeHash(const FRecursiveSchedulerCombinationFamilyKey& Key)
		{
			uint32 Hash = ::GetTypeHash(Key.EntryCount);
			Hash = HashCombine(Hash, ::GetTypeHash(Key.ParentContactCount));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.DirectContactCount));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.SiblingSharedDoorCount));
			Hash = HashCombine(Hash, ::GetTypeHash(Key.ParentSharedDoorCount));
			return Hash;
		}
	};

	/** Aggregated proof-attempt counts for one child offset. */
	struct FRecursiveSchedulerProofOffsetStats
	{
		int32 FullProofAttemptCount = 0;
		int32 FullProofSuccessCount = 0;
	};

	/** Aggregated failure counts for one immediate full-proof rejection kind. */
	struct FRecursiveSchedulerFullProofFailureStats
	{
		int32 Count = 0;
		FString SampleFirstLine;
	};

	/** Last per-candidate deferred validation outcome recorded under the perf flag. */
	struct FRecursiveSchedulerDeferredValidationOutcome
	{
		bool bSucceeded = false;
		bool bMemoHit = false;
		FLayoutId ResultKind;
		double ParentSolveSeconds = 0.0;
		double ChildSolveSeconds = 0.0;
		int32 PreparedPlanRouteConstraintCount = 0;
		int32 PreparedPlanMainRouteConstraintCount = 0;
		int32 PreparedPlanBoundaryRouteConstraintCount = 0;
		int32 PreparedPlanInteriorRouteConstraintCount = 0;
		int32 PreparedPlanBoundaryMultiFaceConstraintCount = 0;
		int32 PreparedPlanRouteFaceRequirementCount = 0;
		int32 PreparedPlanRequiredRouteReservationCount = 0;
		int32 PreparedPlanReachabilityBranchReservationCount = 0;
		int32 PreparedPlanLiveDomainConstrainedCellCount = 0;
		int32 PreparedPlanLiveDomainBoundaryConstrainedCellCount = 0;
		int32 PreparedPlanLiveDomainMultiFaceBoundaryConstrainedCellCount = 0;
		int32 PreparedPlanLiveDomainEligibleCandidateCount = 0;
		int32 PreparedPlanLiveDomainEliminatedCandidateCount = 0;
		int32 PreparedPlanLiveDomainTightestRemainingSize = 0;
		int32 PreparedPlanLiveDomainSingleRemainingCandidateCellCount = 0;
		int32 PreparedPlanLiveDomainAtMostFourRemainingCandidateCellCount = 0;
		bool bPreparedPlanLiveDomainFailedConstraint = false;
		FIntVector PreparedPlanLiveDomainFailedConstraintCell =
			FIntVector::ZeroValue;
		int32 PreparedPlanLiveDomainFailedConstraintEligibleCandidateCount = 0;
		int32 PreparedPlanLiveDomainFailedConstraintRequiredFaceCount = 0;
		ELayoutRouteDomainFailureKind
			PreparedPlanLiveDomainFailedConstraintDominantFailureKind =
				ELayoutRouteDomainFailureKind::None;
		int32 PreparedPlanLiveDomainFailedConstraintDominantFailureCount = 0;
		bool bPreparedPlanRepairModeRouteDomainAttempted = false;
		bool bPreparedPlanRepairModeRouteDomainSucceeded = false;
		bool bPreparedPlanRepairModeLiveDomainFailedConstraint = false;
		FIntVector PreparedPlanRepairModeLiveDomainFailedConstraintCell =
			FIntVector::ZeroValue;
		int32 PreparedPlanRepairModeLiveDomainFailedConstraintEligibleCandidateCount = 0;
		int32 PreparedPlanRepairModeLiveDomainFailedConstraintRequiredFaceCount = 0;
		ELayoutRouteDomainFailureKind
			PreparedPlanRepairModeLiveDomainFailedConstraintDominantFailureKind =
				ELayoutRouteDomainFailureKind::None;
		int32 PreparedPlanRepairModeLiveDomainFailedConstraintDominantFailureCount =
			0;
		int32 PreparedPlanSearchPrefixPlacedCellCount = 0;
		int32 PreparedPlanSearchPrefixForcedPlacementCellCount = 0;
		bool bPreparedPlanSearchPrefixSolvedAfterForcedPlacements = false;
		bool bPreparedPlanSearchPrefixNoCellSelectable = false;
		int32 PreparedPlanSearchPrefixConstrainedCellCount = 0;
		int32 PreparedPlanSearchPrefixEligibleCandidateCount = 0;
		int32 PreparedPlanSearchPrefixTightestRemainingSize = 0;
		int32 PreparedPlanSearchPrefixSingleRemainingCandidateCellCount = 0;
		int32 PreparedPlanSearchPrefixAtMostFourRemainingCandidateCellCount = 0;
		FIntVector PreparedPlanSearchPrefixSelectedCell = FIntVector::ZeroValue;
		ELayoutCellIntent PreparedPlanSearchPrefixSelectedIntent =
			ELayoutCellIntent::Interior;
		int32 PreparedPlanSearchPrefixSelectedDomainSize = 0;
		FLayoutId PreparedPlanSearchPrefixDominantFailureKind = NAME_None;
		int32 PreparedPlanSearchPrefixDominantFailureCount = 0;
		bool bPreparedPlanRepairModeSearchPrefixAttempted = false;
		bool bPreparedPlanRepairModeSearchPrefixSucceeded = false;
		int32 PreparedPlanRepairModeSearchPrefixPlacedCellCount = 0;
		int32 PreparedPlanRepairModeSearchPrefixForcedPlacementCellCount = 0;
		bool bPreparedPlanRepairModeSearchPrefixSolvedAfterForcedPlacements =
			false;
		bool bPreparedPlanRepairModeSearchPrefixNoCellSelectable = false;
		int32 PreparedPlanRepairModeSearchPrefixConstrainedCellCount = 0;
		int32 PreparedPlanRepairModeSearchPrefixEligibleCandidateCount = 0;
		int32 PreparedPlanRepairModeSearchPrefixTightestRemainingSize = 0;
		int32 PreparedPlanRepairModeSearchPrefixSingleRemainingCandidateCellCount =
			0;
		int32 PreparedPlanRepairModeSearchPrefixAtMostFourRemainingCandidateCellCount =
			0;
		FIntVector PreparedPlanRepairModeSearchPrefixSelectedCell =
			FIntVector::ZeroValue;
		ELayoutCellIntent PreparedPlanRepairModeSearchPrefixSelectedIntent =
			ELayoutCellIntent::Interior;
		int32 PreparedPlanRepairModeSearchPrefixSelectedDomainSize = 0;
		FLayoutId PreparedPlanRepairModeSearchPrefixDominantFailureKind = NAME_None;
		int32 PreparedPlanRepairModeSearchPrefixDominantFailureCount = 0;
		int32 ParentProofLiveDomainConstrainedCellCount = 0;
		int32 ParentProofLiveDomainBoundaryConstrainedCellCount = 0;
		int32 ParentProofLiveDomainMultiFaceBoundaryConstrainedCellCount = 0;
		int32 ParentProofLiveDomainEligibleCandidateCount = 0;
		int32 ParentProofLiveDomainEliminatedCandidateCount = 0;
		int32 ParentProofLiveDomainTightestRemainingSize = 0;
		int32 ParentProofLiveDomainSingleRemainingCandidateCellCount = 0;
		int32 ParentProofLiveDomainAtMostFourRemainingCandidateCellCount = 0;
		bool bParentProofLiveDomainFailedConstraint = false;
		FIntVector ParentProofLiveDomainFailedConstraintCell =
			FIntVector::ZeroValue;
		int32 ParentProofLiveDomainFailedConstraintEligibleCandidateCount = 0;
		int32 ParentProofLiveDomainFailedConstraintRequiredFaceCount = 0;
		ELayoutRouteDomainFailureKind
			ParentProofLiveDomainFailedConstraintDominantFailureKind =
				ELayoutRouteDomainFailureKind::None;
		int32 ParentProofLiveDomainFailedConstraintDominantFailureCount = 0;
		/** Off-by-default deferred parent-plan summary for hotspot diagnosis. */
		int32 ChildBundleSupportedCellCount = 0;
		int32 ChildBundleSupportedUpperCellCount = 0;
		int32 ChildBundleSupportedUpperInteriorLikeCellCount = 0;
		int32 ChildBundleSupportedTopLevelCellCount = 0;
		int32 ChildBundleSupportedTopLevelInteriorLikeCellCount = 0;
		FString FailureFirstLine;
	};

	struct FRecursiveSchedulerPerfMetrics
	{
		double StartSeconds = FPlatformTime::Seconds();
		double SetupSeconds = 0.0;
		double PreparedSourceBuildSeconds = 0.0;
		double ChildTemplateBuildSeconds = 0.0;
		double ChildPlanningVariantBuildSeconds = 0.0;
		double CandidateNormalizationSeconds = 0.0;
		double DemandPreparationSeconds = 0.0;
		double EntryNegotiationSeconds = 0.0;
		double EntryOptionCollectionSeconds = 0.0;
		double CombinationEnumerationSeconds = 0.0;
		double EntryCommitmentResolutionSeconds = 0.0;
		double ParentProbeSeconds = 0.0;
		double ParentStructuralPrecheckSeconds = 0.0;
		double ParentProbeSolveSeconds = 0.0;
		double CompleteValidationSeconds = 0.0;
		double CompleteValidationPrepSeconds = 0.0;
		double CompleteValidationSolveSeconds = 0.0;
		double CompleteValidationAuditSeconds = 0.0;
		double CompleteValidationIndependentParentSolveSeconds = 0.0;
		double CompleteValidationIndependentChildSolveSeconds = 0.0;
		int32 SearchStatesVisited = 0;
		int32 PreparedChildSourceCount = 0;
		int32 ChildPlanningVariantCount = 0;
		int32 CandidateNormalizationCollapsedCount = 0;
		int32 CandidateNormalizationBeamTrimmedCount = 0;
		int32 CandidateNormalizationRetainedCount = 0;
		int32 EntryNegotiationCalls = 0;
		int32 EntryOptionCollectionCalls = 0;
		int32 EntryCommitmentResolutionCalls = 0;
		int32 ParentProbeCalls = 0;
		int32 ParentProbeMemoHits = 0;
		int32 ParentProbeFullSolveCalls = 0;
		int32 ParentProbeStructuralRejects = 0;
		/** Residual plans reused from commitment checks instead of being settled twice. */
		int32 ParentResidualPlanReuseCount = 0;
		int32 EnumeratedCommitmentCombinationCount = 0;
		int32 StructuralRejectedCommitmentCombinationCount = 0;
		int32 FullProofReachedCommitmentCombinationCount = 0;
		int32 FullProofSurvivedCommitmentCombinationCount = 0;
		int32 SuccessfulCommitmentCombinationCount = 0;
		int32 PreparedParentProofCandidateStoreCount = 0;
		int32 PreparedParentProofCandidateReuseCount = 0;
		int32 CompleteValidationCalls = 0;
		int32 CompleteValidationMemoHits = 0;
		int32 CompleteValidationIndependentSplitCalls = 0;
		int32 CompleteValidationCoarseKeyCount = 0;
		int32 CompleteValidationCoarseKeyRepeatCount = 0;
		int32 CompleteValidationParentRequestKeyCount = 0;
		int32 CompleteValidationParentRequestRepeatCount = 0;
		int32 CompleteValidationBuildParentRequestFailures = 0;
		int32 CompleteValidationParentProofFailures = 0;
		int32 CompleteValidationParentAnchorFailures = 0;
		int32 CompleteValidationChildBoundaryFailures = 0;
		int32 CompleteValidationChildProofFailures = 0;
		int32 CompleteValidationIndependentScheduleBuildFailures = 0;
		int32 CompleteValidationScheduleSolveFailures = 0;
		int32 CompleteValidationMergedTraversalFailures = 0;
		int32 CompleteValidationSuccesses = 0;
		TMap<FRecursiveSchedulerCombinationFamilyKey, FRecursiveSchedulerCombinationFamilyStats>
			CombinationFamilyStatsByKey;
		TMap<FIntVector, FRecursiveSchedulerProofOffsetStats> ProofAttemptOffsetStats;
		TMap<FLayoutId, FRecursiveSchedulerFullProofFailureStats> FullProofFailureStatsByKind;
		TMap<FString, FRecursiveSchedulerFullProofFailureStats> StructuralRejectFailureStatsByFirstLine;
		TMap<FString, FRecursiveSchedulerFullProofFailureStats> CompleteValidationParentProofFailureStatsByFirstLine;
		FRecursiveSchedulerDeferredValidationOutcome LastCompleteValidationOutcome;
	};

	struct FAcceptedChildPlacementSearchResult
	{
		bool bFoundDownstreamSolution = false;
		FString FailureReason;
	};

	struct FSuccessfulCandidateSearchResult
	{
		bool bFoundDownstreamSolution = false;
		FString BestDownstreamFailureReason;
	};

	struct FChildPlacementCandidateBuildResult
	{
		TArray<FScoredChildOffset> SuccessfulCandidates;
		TArray<FString> RejectedCandidateReasons;
		FString BestRejectedCandidateReason;
		int32 BestRejectedCandidateScore = MIN_int32 / 4;
		int32 CandidateOffsetsExamined = 0;
		int32 ParentPlanRejectCount = 0;
		int32 StageMappingRejectCount = 0;
		int32 PlacementZoneRejectCount = 0;
		int32 LevelPlacementRejectCount = 0;
		int32 EntryNegotiationRejectCount = 0;
		int32 ProbeRejectCount = 0;
	};

	struct FPlacementBridgeSharedMemoStore
	{
		// Share canonical parent-proof solves across bounded root retries so
		// repeated single-state attempts stop paying the full probe solve cost.
		TMap<FString, FParentProbeEvaluationMemo> ParentProbeSolveMemo;
		TMap<FString, FPlacementValidationMemo> CompletePlacementValidationMemo;
	};

	struct FResolvedChildEntryCommitmentCandidate
	{
		struct FScoreBreakdown
		{
			bool bHasDiagnostics = false;
			int32 IntentScore = 0;
			int32 ParentCapabilitySupportContribution = 0;
			int32 ParentSupportContribution = 0;
			int32 ProtectedTraversalPenalty = 0;
			int32 AnchoredIntentPenalty = 0;
			int32 PriorityDistancePenalty = 0;
			int32 NoParentResolutionScore = 0;
			int32 TotalScore = 0;
		};

		enum class EResolutionMode : uint8
		{
			DirectParentContact,
			ParentSharedDoor,
			SiblingSharedDoor
		};

		bool bUsesDirectParentContact = true;
		EResolutionMode ResolutionMode = EResolutionMode::DirectParentContact;
		FIntVector ChildLocalCell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		FLayoutCommittedEndpointAnchor Anchor;
		FIntVector ParentContactCell = FIntVector::ZeroValue;
		ELayoutCellIntent ParentIntent = ELayoutCellIntent::Boundary;
		FLayoutId BoundaryWitnessId;
		int32 Score = MIN_int32 / 4;
		FScoreBreakdown ScoreBreakdown;
	};

	struct FResolvedCommitmentScoreBreakdown
	{
		bool bHasDiagnostics = false;
		int32 EntryAccumulatedScore = 0;
		int32 EntryIntentScore = 0;
		int32 EntryParentCapabilitySupportContribution = 0;
		int32 EntryParentSupportContribution = 0;
		int32 EntryProtectedTraversalPenalty = 0;
		int32 EntryAnchoredIntentPenalty = 0;
		int32 EntryPriorityDistancePenalty = 0;
		int32 EntryNoParentResolutionScore = 0;
		int32 DirectParentEntryCount = 0;
		int32 NoParentEntryCount = 0;
		int32 DirectParentSupportSaturationPenalty = 0;
		int32 ResolveAdjustmentTotal = 0;
	};

	struct FCommittedEndpointAnchorSignature
	{
		FIntVector LocalCell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		FGameplayTag ConnectionTag;
		FGameplayTagContainer AllowedConnectionTags;
		FGameplayTagContainer TraversalChannels;
		bool bRequireMatchingYawWithFilledNeighbor = false;

		friend bool operator==(const FCommittedEndpointAnchorSignature& Left, const FCommittedEndpointAnchorSignature& Right);
		friend uint32 GetTypeHash(const FCommittedEndpointAnchorSignature& Signature);
	};

	struct FResolvedChildEntryCommitmentCandidateSignature
	{
		bool bUsesDirectParentContact = true;
		FResolvedChildEntryCommitmentCandidate::EResolutionMode ResolutionMode =
			FResolvedChildEntryCommitmentCandidate::EResolutionMode::DirectParentContact;
		FIntVector ChildLocalCell = FIntVector::ZeroValue;
		FCommittedEndpointAnchorSignature Anchor;
		FIntVector ParentContactCell = FIntVector::ZeroValue;

		friend bool operator==(const FResolvedChildEntryCommitmentCandidateSignature& Left, const FResolvedChildEntryCommitmentCandidateSignature& Right);
		friend uint32 GetTypeHash(const FResolvedChildEntryCommitmentCandidateSignature& Signature);
	};

	struct FSuccessfulCommitmentCombinationSignature
	{
		TArray<FCommittedEndpointAnchorSignature> Commitments;
		TArray<FIntVector> ParentContactCells;
		TArray<FResolvedChildEntryCommitmentCandidateSignature> NoParentResolutionEntries;

		friend bool operator==(const FSuccessfulCommitmentCombinationSignature& Left, const FSuccessfulCommitmentCombinationSignature& Right);
		friend uint32 GetTypeHash(const FSuccessfulCommitmentCombinationSignature& Signature);
	};

	struct FEntryCommitmentOptionMemo
	{
		bool bSucceeded = false;
		FString FailureReason;
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>> CandidateOptionsByEntry;
	};

	/** Mutable recursive-scheduler state threaded through search and deferred validation. */
	struct FRecursiveChildPlacementSchedulerState
	{
		TArray<FAutomaticChildPlacement> CurrentPlacements;
		TArray<FSkippedOptionalChildPlacement> CurrentSkippedOptionalPlacements;
		TArray<FLayoutPartitionSeamRecord> CurrentPlannedPartitionSeams;
		FCommittedVerticalAccessOwnership CurrentVerticalAccessOwnership;
		TArray<FLayoutValidationMessage> CurrentWarnings;
		/** Exact parent-only proof request for the currently committed schedule prefix, reused by deferred validation when available. */
		bool bHasCurrentPreparedParentProofRequest = false;
		/** Prepared parent proof request that tracks the current accepted placement prefix without later coarse reconstruction. */
		FLayoutRegionSolveRequest CurrentPreparedParentProofRequest;
		TSet<FIntVector> CurrentReservedParentCells;
		TArray<FCanonicalChildPlacementChoice> LastChosenPlacementBySource;
		FCommittedRecursiveScheduleState BestSchedule;
		TArray<FDeferredCompletePlacementCandidate> DeferredCompletePlacementCandidates;
		TSet<FString> DeferredCompletePlacementCandidateKeys;
		TMap<FString, int32> FailedSchedulerStateBestScore;
		TMap<FString, FPlacementValidationMemo> CompletePlacementValidationMemo;
		TSet<FString> CompletePlacementValidationCoarseKeysSeen;
		TSet<FString> CompletePlacementValidationParentRequestKeysSeen;
		TMap<FString, FPlacementValidationMemo>* SharedCompletePlacementValidationMemo = nullptr;
		TMap<FString, FParentProbeEvaluationMemo>* SharedParentProbeSolveMemo = nullptr;
		TMap<FString, FEntryCommitmentOptionMemo> EntryCommitmentOptionMemo;
		TMap<FString, FParentProbeEvaluationMemo> ParentProbeEvaluationMemo;
		TMap<FString, FParentStructuralFeasibilityMemo> ParentStructuralFeasibilityMemo;
		TMap<FString, FRemainingDemandPropagationMemo> RemainingDemandPropagationMemo;
		FString SearchFailureReason;
		/** First child placement cause retained before downstream parent audits can replace display text. */
		FLayoutRegionalFailureRecord FirstCausalFailure;
		/** Best later child placement cause retained separately from FirstCausalFailure. */
		FLayoutRegionalFailureRecord BestDownstreamFailure;
		ELayoutSolvePreparationFailureKind PreparationFailureKind = ELayoutSolvePreparationFailureKind::None;
		int32 PreparationCandidateAttemptCount = 0;
		FRecursiveSchedulerPerfMetrics PerfMetrics;
	};

	struct FPreparedChildPlacementSource
	{
		struct FEntryCountDomain
		{
			int32 MinCount = 0;
			int32 MaxCount = 0;
			TArray<int32> AdmissibleCounts;
		};

		struct FPlanningVariant
		{
			struct FNormalizedCandidate
			{
				struct FParentContactOpportunity
				{
					FIntVector ChildEntryCell = FIntVector::ZeroValue;
					ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
					FGameplayTag ConnectionTag;
					FGameplayTagContainer AllowedConnectionTags;
					FGameplayTagContainer TraversalChannels;
					FIntVector ParentContactCell = FIntVector::ZeroValue;
					ELayoutCellIntent ParentIntent = ELayoutCellIntent::Interior;
					int32 ParentCapabilitySupportScore = 0;
					bool bProtectedTraversalCell = false;
					FLayoutId BoundaryWitnessId;
				};

				FIntVector Offset = FIntVector::ZeroValue;
				/** True when this value occupies an authored exterior Entry that later parent settlement must relocate. */
				bool bDisplacesRelocatableParentEntry = false;
				FLayoutChildStageMappingResult StageMapping;
				TArray<FIntVector> ParentPlanReservedCells;
				TArray<FSharedParentChildFace> SharedParentChildFaces;
				TArray<FParentContactOpportunity> ParentContactOpportunities;
				/** Parent cell domains certified against this child boundary before proof. */
				TArray<FLayoutCellCandidateDomainRestriction> ParentDomainRestrictions;
				/** Child boundary cell domains frozen into the independent child request. */
				TArray<FLayoutCellCandidateDomainRestriction> ChildDomainRestrictions;
				/** Child-local incoming boundaries frozen from the selected reciprocal signatures. */
				TArray<FLayoutSolveBoundaryPoint> ChildIncomingBoundaryPoints;
				/** Stable certificate for the exact transform and reciprocal boundary domains. */
				FLayoutId BoundaryCertificateId;
				int32 ParentOwnedSeamOpportunityScore = 0;
			};

			FLayoutRegionSolveRequest ChildRequestTemplate;
			int32 YawRotationSteps = 0;
			FIntPoint FootprintSize = FIntPoint::ZeroValue;
			int32 MaxLocalLevel = 0;
			TArray<FLayoutPlannedCell> PlannedCells;
			TSet<FIntVector> PlannedCellSet;
			/** Filled child-local cells compiled from capability bundles for parent structural support. */
			TArray<FIntVector> StructuralSupportLocalCells;
			FLayoutChildCapabilityEnvelope CapabilityEnvelope;
			FChildPlanningVariantInterfaceSummary InterfaceSummary;
			TArray<FNormalizedCandidate> NormalizedCandidates;
			int32 StaticCandidateOffsetCount = 0;
			int32 StaticParentPlanRejectCount = 0;
			int32 StaticBoundaryDomainRejectCount = 0;
			/** Fewest-shared-face boundary failure, preferred over perimeter-overlap noise. */
			int32 BestBoundaryDomainFailureSharedFaceCount = MAX_int32;
			/** True once diagnostics capture a transform satisfying its zone without seam exemptions. */
			bool bBestBoundaryDomainFailureMatchesZoneWithoutSharedSeam = false;
			/** Minimum XY distance from captured child cells to parent perimeter. */
			int32 BestBoundaryDomainFailurePerimeterClearance = MIN_int32;
			FString FirstBoundaryDomainFailureReason;
			int32 StaticStageMappingRejectCount = 0;
			/** First static protected-cell/volume rejection, retained before dynamic probes exist. */
			FString FirstParentPlanFailureReason;
			FString FirstStageMappingFailureReason;
			int32 StaticPlacementZoneRejectCount = 0;
			int32 StaticLevelPlacementRejectCount = 0;
		};

		const FLayoutRegionContentEntrySolveSnapshot* EntrySnapshot = nullptr;
		/** Pointer-free hard-demand choices this direct-child source may satisfy before placement. */
		TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary> HardFeatureProviderChoices;
		FEntryCountDomain EntryCountDomain;
		FLayoutRegionSolveRequest ChildRequestTemplate;
		FIntPoint ChildFootprintSize = FIntPoint::ZeroValue;
		TArray<FLayoutPlannedCell> ChildPlannedCells;
		TArray<FPlanningVariant> PlanningVariants;
		ELayoutSolvePreparationFailureKind PreparationFailureKind = ELayoutSolvePreparationFailureKind::None;
		int32 PreparationCandidateAttemptCount = 0;
		FString PreparationFailureReason;
		bool bPreparationSucceeded = false;
		int32 RequiredInstanceCount = 1;
	};

	struct FResolvedChildNoParentEntryOption
	{
		FResolvedChildEntryCommitmentCandidate::EResolutionMode ResolutionMode =
			FResolvedChildEntryCommitmentCandidate::EResolutionMode::DirectParentContact;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
	};

	/** Mutable state for resolving one child entry/contact commitment combination set. */
	struct FChildEntryCommitmentResolutionState
	{
		struct FSuccessfulCommitmentCombination
		{
			int32 TotalScore = MIN_int32 / 4;
			FString CanonicalKey;
			TArray<FLayoutCommittedEndpointAnchor> Commitments;
			TArray<FIntVector> ParentContactCells;
			TArray<FResolvedChildEntryCommitmentCandidate> NoParentResolutionEntries;
			FResolvedCommitmentScoreBreakdown ScoreBreakdown;
		};

		int32 BestResolvedScore = MIN_int32 / 4;
		bool bResolvedAnyCommitmentCombination = false;
		bool bStopSearch = false;
		bool bTrackSuccessfulCommitmentCombinations = false;
		double CallbackEvaluationSeconds = 0.0;
		TArray<FLayoutCommittedEndpointAnchor> CurrentCommitments;
		TArray<FIntVector> CurrentParentContactCells;
		TArray<FResolvedChildEntryCommitmentCandidate> CurrentNoParentResolutionEntries;
		TArray<FResolvedChildEntryCommitmentCandidate::FScoreBreakdown> CurrentEntryScoreBreakdowns;
		TSet<FIntVector> UsedParentContactCells;
		TSet<FSuccessfulCommitmentCombinationSignature> SuccessfulCommitmentCombinationKeys;
		TMap<FSuccessfulCommitmentCombinationSignature, FSuccessfulCommitmentCombination> SuccessfulCommitmentCombinationsByKey;
		FResolvedCommitmentScoreBreakdown BestResolvedScoreBreakdown;
	};

	/** Named recursive search context so child-placement demand recursion is explicit. */
	struct FRecursivePlacementDemandSearcher
	{
		const FLayoutRegionSolveRequest& RootRequest;
		const TArray<FLayoutPlannedCell>& ParentPlannedCells;
		const FIntPoint& ParentFootprintSize;
		const TSet<FIntVector>& ParentPlannedCellSet;
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents;
		const TArray<FIntVector>& ParentPriorityTargets;
		const TSet<FIntVector>& ParentProtectedTraversalCells;
		const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections;
		const LayoutRegionScheduleSolverFacade::FRecursiveScheduleSolveContext& PreparedSolveContext;
		const LayoutRegionScheduleSolverFacade::FCompiledStructuralInputs& PreparedStructuralInputs;
		const TArray<FChildPlacementDemand>& PlacementDemands;
		const TArray<FPreparedChildPlacementSource>& PreparedSources;
		FRecursiveChildPlacementSchedulerState& SchedulerState;
		int32 MaxDeferredCandidateCount = 0;

		bool Search(int32 DemandIndex, int32 CurrentScore, FString& OutSearchFailureReason);
	};

	/** Settles parent Entry/host authority against child exclusions; optional final proof avoids later re-selection. */
	bool TryFreezeParentAuthorityForPlacements(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FAutomaticChildPlacement>& Placements,
		FLayoutRegionSolveRequest& InOutParentRequest,
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString& OutFailureReason,
		FLayoutRegionSolveResult* OutFinalProof = nullptr);

	/** Finalizes pending child-owned stair alternatives without changing certified geometry or boundaries.
	 * Independent children defer this work until complete-candidate validation; shared-ascent children
	 * prepare before negotiation. Synchronizes request and mapping authority only on success. */
	bool FinalizeMappedChildVerticalAccess(FAutomaticChildPlacement& Placement, FString& OutFailureReason);

	/** Named deferred validator so final candidate proofing is explicit. */
	struct FDeferredCompletePlacementValidator
	{
		const FLayoutRegionSolveRequest& RootRequest;
		const TArray<FLayoutPlannedCell>& ParentPlannedCells;
		const TSet<FIntVector>& ParentPlannedCellSet;
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents;
		const FIntPoint& ParentFootprintSize;
		const TSet<FIntVector>& ParentProtectedTraversalCells;
		const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections;
		FRecursiveChildPlacementSchedulerState& SchedulerState;
		double SharedDeadlineSeconds = 0.0;

		bool operator()(FCommittedRecursiveScheduleState& CandidateSchedule, FString& OutValidationFailureReason) const;
	};

	/** Matches a translated child plan against one parent-relative placement zone. */
	bool DoesTranslatedChildPlanMatchPlacementZone(
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const FIntVector& Offset,
		const FIntPoint& ParentFootprintSize,
		ELayoutPlacementZone PlacementZone,
		const TSet<FIntVector>* ExemptTranslatedCells,
		FString* OutFailureReason);

	void SortAndCapSuccessfulChildPlacementCandidates(
		TArray<FScoredChildOffset>& InOutSuccessfulCandidates,
		bool bAllowTrimming,
		bool bPartialPlacementStep,
		int32 PlacementDemandCount,
		FRecursiveSchedulerPerfMetrics* PerfMetrics);
}
