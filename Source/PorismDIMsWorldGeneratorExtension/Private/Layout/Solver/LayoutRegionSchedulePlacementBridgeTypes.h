// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"

namespace LayoutRegionScheduleSolverPrivate
{
	/** Default ParentOwned alone grants no permission to relax child traversal obligations. */
	inline bool HasParentOwnedAscentObligation(const FLayoutNegotiatedChildResponsibilityContract& Contract)
	{
		return Contract.HostVerticalAccessResponsibility == ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned
			&& (Contract.RequiredHostProviderCount > 0 || Contract.CountedParentProviderCount > 0
				|| !Contract.CountedParentVerticalAccessCells.IsEmpty()
				|| !Contract.RetainedParentRouteSupportVerticalAccessCells.IsEmpty());
	}

	enum class ECommittedVerticalAccessOwnerKind : uint8
	{
		None,
		Parent,
		ChildRegion
	};

	struct FSharedParentChildFace
	{
		FIntVector ParentCell = FIntVector::ZeroValue;
		FIntVector ChildLocalCell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		FGameplayTag InterfaceFamily;
		/** Exact capability ownership admitted for the parent side of this face. */
		bool bParentCanOwnSeam = true;
		/** Exact capability ownership admitted for the child side of this face. */
		bool bChildCanOwnSeam = false;
		/** Longest finalized parent-side support run attached to this face. */
		int32 ParentOwnerSupportRunLength = 1;
		/** Longest finalized child-side support run attached to this face. */
		int32 ChildOwnerSupportRunLength = 1;
		/** Stable parent seam capability selected into the reciprocal boundary witness, whether owning or accepting. */
		FLayoutId ParentSeamCapabilityId;
		/** Stable child seam capability selected into the reciprocal boundary witness, whether owning or accepting. */
		FLayoutId ChildSeamCapabilityId;
		/** Stable exact module/yaw pair-domain witness used by scoring, memoization, and committed ownership. */
		FLayoutId ReciprocalDomainWitnessId;
		/** Exact child-side connection tag consumed by parent proof on this shared face. */
		FGameplayTag ConnectionTag;
		/** Exact child-side allowed tags consumed by parent proof on this shared face. */
		FGameplayTagContainer AllowedConnectionTags;
		/** Exact child-side traversal channels consumed by parent proof on this shared face. */
		FGameplayTagContainer TraversalChannels;
		/** Exact child-side yaw requirement consumed by parent proof on this shared face. */
		bool bRequireMatchingYawWithFilledNeighbor = false;
	};

	/** Builds the exact memo identity for one certified shared parent/child face. */
	inline FString BuildSharedParentChildFaceIdentityPart(const FSharedParentChildFace& Face)
	{
		return FString::Printf(
			TEXT("%s:%s:%d:%s:%d:%d:%d:%d:%s:%s:%s:%s:%d"),
			*Face.ParentCell.ToString(),
			*Face.ChildLocalCell.ToString(),
			static_cast<int32>(Face.FaceDirection),
			Face.InterfaceFamily.IsValid() ? *Face.InterfaceFamily.ToString() : TEXT("<none>"),
			Face.bParentCanOwnSeam ? 1 : 0,
			Face.bChildCanOwnSeam ? 1 : 0,
			Face.ParentOwnerSupportRunLength,
			Face.ChildOwnerSupportRunLength,
			Face.ParentSeamCapabilityId.IsNone() ? TEXT("<none>") : *Face.ParentSeamCapabilityId.ToString(),
			Face.ChildSeamCapabilityId.IsNone() ? TEXT("<none>") : *Face.ChildSeamCapabilityId.ToString(),
			Face.ReciprocalDomainWitnessId.IsNone() ? TEXT("<none>") : *Face.ReciprocalDomainWitnessId.ToString(),
			Face.ConnectionTag.IsValid() ? *Face.ConnectionTag.ToString() : TEXT("<none>"),
			Face.bRequireMatchingYawWithFilledNeighbor ? 1 : 0);
	}

	/** Refreshes contiguous run lengths from exact certified faces, optionally constrained by finalized parent cells. */
	inline void RefreshCertifiedSharedParentChildFaceRunLengths(
		TArray<FSharedParentChildFace>& Faces,
		const TSet<FIntVector>* ValidParentCells = nullptr)
	{
		for (FSharedParentChildFace& Anchor : Faces)
		{
			if (Anchor.ReciprocalDomainWitnessId.IsNone()
				|| (ValidParentCells != nullptr && !ValidParentCells->Contains(Anchor.ParentCell)))
			{
				Anchor.ParentOwnerSupportRunLength = 0;
				Anchor.ChildOwnerSupportRunLength = 0;
				continue;
			}

			const FIntVector RunDelta =
				Anchor.FaceDirection == ELayoutFaceDirection::PosX
					|| Anchor.FaceDirection == ELayoutFaceDirection::NegX
					? FIntVector(0, 1, 0)
					: FIntVector(1, 0, 0);
			const auto ContainsCertifiedFace = [&](const FIntVector& ParentCell)
			{
				return (ValidParentCells == nullptr || ValidParentCells->Contains(ParentCell))
					&& Faces.ContainsByPredicate(
						[&](const FSharedParentChildFace& Candidate)
						{
							return !Candidate.ReciprocalDomainWitnessId.IsNone()
								&& Candidate.ParentCell == ParentCell
								&& Candidate.FaceDirection == Anchor.FaceDirection
								&& Candidate.InterfaceFamily == Anchor.InterfaceFamily;
						});
			};

			int32 RunLength = 1;
			for (FIntVector Cell = Anchor.ParentCell + RunDelta;
				ContainsCertifiedFace(Cell);
				Cell += RunDelta)
			{
				++RunLength;
			}
			for (FIntVector Cell = Anchor.ParentCell - RunDelta;
				ContainsCertifiedFace(Cell);
				Cell -= RunDelta)
			{
				++RunLength;
			}
			Anchor.ParentOwnerSupportRunLength = RunLength;
			Anchor.ChildOwnerSupportRunLength = RunLength;
		}
	}

	/** Captures placement-score components for deferred ranking diagnostics. */
	struct FChildPlacementScoreBreakdown
	{
		bool bHasDiagnostics = false;
		int32 FinalScore = MIN_int32 / 4;
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
	};

	/** Queue-time selection features computed when a child placement is accepted. */
	struct FChildPlacementSelectionFeatures
	{
		bool bUsesDirectContact = false;
		ELayoutPlacementZone AuthoredPlacementZone = ELayoutPlacementZone::Any;
		int32 ParentBoundaryTouchCount = 0;
		int32 ParentExteriorReservedCellCount = 0;
		int32 ParentChildSeamScore = 0;
		int32 ParentContactCount = 0;
		int32 ParentCommittedTraversalAnchorCount = 0;
		int32 ReservedParentCellCount = 0;
		int32 NonDoorSharedParentChildFaceCount = 0;
		int32 TraversalBoundaryPointCount = 0;
		int32 TraversalBoundaryCellCount = 0;
		int32 MultiTraversalBoundaryCellCount = 0;
		int32 BoundaryFacingTraversalPointCount = 0;
		ELayoutNegotiatedHostVerticalAccessResponsibility
			NegotiatedHostVerticalAccessResponsibility =
				ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
		bool bHasRequiredHostIngressAnchor = false;
		bool bHasRequiredHostEgressAnchor = false;
		FLayoutId RequiredChildGenerallyConnectableAnchorPairId;
		int32 NegotiatedRouteCellCount = 0;
		int32 NegotiatedRouteSpanLevelCount = 0;
		int32 NegotiatedInterfaceTraversalAnchorCount = 0;
	};

	struct FAutomaticChildPlacement
	{
		FName EntryId;
		int32 InstanceOrdinal = INDEX_NONE;
		FString ChildRegionDebugPath;
		FIntVector RegionCellOffset = FIntVector::ZeroValue;
		FLayoutChildStageMappingResult StageMapping;
		TArray<FLayoutPlannedCell> ParentTranslatedPlannedCells;
		/** Translated child filled cells that can structurally support surviving parent cells. */
		TArray<FIntVector> ParentTranslatedSupportingCells;
		TArray<FIntVector> ParentPlanReservedCells;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
		TArray<FLayoutSolveBoundaryPoint> ParentTranslatedFutureTerraceBoundaryPoints;
		/** Stable reciprocal boundary-domain witnesses selected before parent/child proof. */
		TArray<FLayoutId> BoundaryWitnessIds;
		/** Stable certificate for exact transform-owned parent and child boundary domains. */
		FLayoutId BoundaryCertificateId;
		/** Parent candidate domains certified before residual-parent proof. */
		TArray<FLayoutCellCandidateDomainRestriction> ParentDomainRestrictions;
		/** Child boundary candidate domains frozen before child proof. */
		TArray<FLayoutCellCandidateDomainRestriction> ChildDomainRestrictions;
		/** Child-local boundary points derived from the same reciprocal signature certificate. */
		TArray<FLayoutSolveBoundaryPoint> CertifiedChildIncomingBoundaryPoints;
		TArray<FIntVector> ParentCommittedEntryCells;
		TArray<FLayoutCommittedEndpointAnchor> ParentDirectContactCommitments;
		TArray<FLayoutCommittedTraversalAnchor> ParentCommittedTraversalAnchors;
		bool bAllowsChildTraversalBridgeForCommittedContacts = false;
		FLayoutDirectChildRegionCommitment DirectChildCommitment;
		FLayoutNegotiatedChildResponsibilityContract NegotiatedResponsibilityContract;
		FLayoutRegionSolveRequest ChildRequest;
		FLayoutChildCapabilityEnvelope ChildCapabilityEnvelope;
		bool bSupportsVerticalAccess = false;
		bool bContributesHostVerticalAccess = false;
		TArray<FIntVector> ChildVerticalAccessLocalCells;
		TArray<FSharedParentChildFace> SharedParentChildFaces;
		FChildPlacementScoreBreakdown ScoreBreakdown;
		FChildPlacementSelectionFeatures SelectionFeatures;
	};

	struct FSkippedOptionalChildPlacement
	{
		FString ChildRegionDebugPath;
		FLayoutRegionSolveRequest ChildRequest;
		FString FailureReason;
	};

	struct FCommittedVerticalAccessOwnership
	{
		int32 RequiredHostProviderCount = 0;
		bool bRequiresExactHostProviderCount = false;
		int32 PotentialParentProviderCount = 0;
		/** Total authored host-provider groups satisfied after contract-aware composed ownership is resolved. */
		int32 ResolvedHostProviderCount = 0;
		int32 CountedParentProviderCount = 0;
		/** Exact representative cells for retained parent-provider groups that still count toward authored host vertical-access ownership. */
		TArray<FIntVector> CountedParentVerticalAccessCells;
		/** Retained parent vertical-access cells preserved only for route-support or child handoff traversal. */
		TArray<FIntVector> RetainedParentRouteSupportVerticalAccessCells;
		TArray<FString> CountedChildRegionDebugPaths;
		TArray<FString> LocalOnlyChildRegionDebugPaths;
		TArray<FString> UnusableContributingChildRegionDebugPaths;
		TArray<FString> ExtraContributingChildRegionDebugPaths;
		ECommittedVerticalAccessOwnerKind OwnerKind = ECommittedVerticalAccessOwnerKind::None;
		FString ChildRegionDebugPath;
		FString FailureReason;
	};

	/** Certified regional proof outputs retained for one exact accepted branch. */
	struct FCertifiedRegionalProofResults
	{
		bool bIsSet = false;
		FString ValidationKey;
		FLayoutRegionSolveResult ParentProofResult;
		TArray<FLayoutRegionSolveResult> ChildProofResults;

		/** Returns true when one successful parent proof and its child proof set are available for final reduction. */
		bool IsSet() const
		{
			return bIsSet
				&& !ValidationKey.IsEmpty()
				&& ParentProofResult.SolveResult.bSucceeded;
		}
	};

	struct FCommittedRecursiveScheduleState
	{
		TArray<FAutomaticChildPlacement> Placements;
		TArray<FSkippedOptionalChildPlacement> SkippedOptionalPlacements;
		TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams;
		FCommittedVerticalAccessOwnership VerticalAccessOwnership;
		TArray<FLayoutValidationMessage> Warnings;
		/** Exact parent-proof request captured from the accepted schedule when deferred validation can reuse it directly. */
		bool bHasPreparedParentProofRequest = false;
		/** Prepared parent-only proof request kept in sync with the accepted schedule to avoid lossy rebuilds later. */
		FLayoutRegionSolveRequest PreparedParentProofRequest;
		bool bUsedIndependentDeferredProofSplit = false;
		/** Exact successful regional proof outputs moved into final merge rather than solved again. */
		FCertifiedRegionalProofResults CertifiedRegionalProofResults;
	};
}
