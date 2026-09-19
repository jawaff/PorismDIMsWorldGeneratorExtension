// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "LayoutRegionScheduleSearchRuntime.h"
#include "LayoutSolveExecutionBudget.h"
#include "Layout/Contracts/LayoutContractPipeline.h"

#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/ScopeExit.h"

namespace LayoutRegionScheduleSolverPrivate
{
	FString DescribeNegotiatedHostVerticalAccessResponsibility(
		ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility);

	void QueueDeferredCompletePlacementCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const FString& RootRegionDebugPath,
		const FIntPoint& ParentFootprintSize,
		int32 Score,
		const TArray<FAutomaticChildPlacement>& Placements,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FSkippedOptionalChildPlacement>& SkippedOptionalPlacements,
		const TArray<FLayoutValidationMessage>& Warnings,
		bool bHasPreparedParentProofRequest,
		const FLayoutRegionSolveRequest* PreparedParentProofRequest,
		TArray<FDeferredCompletePlacementCandidate>& InOutCandidates,
		TSet<FString>& InOutCandidateKeys);

	FString BuildSchedulerPlacementStateKey(
		int32 DemandIndex,
		const TArray<FAutomaticChildPlacement>& Placements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership);

	FIntVector TranslateCell(const FIntVector& Cell, const FIntVector& Offset);
	FLayoutPlannedCell TranslatePlannedCell(const FLayoutPlannedCell& PlannedCell, const FIntVector& Offset);

	FIntVector ResolveMappedChildCell(
		const FLayoutChildStageMappingResult& StageMapping,
		const FIntVector& SourceChildCell)
	{
		if (const FLayoutChildStageMappedCell* MappedCell = StageMapping.Cells.FindByPredicate(
			[&SourceChildCell](const FLayoutChildStageMappedCell& Candidate)
			{
				return Candidate.SourceChildCell == SourceChildCell;
			}))
		{
			return MappedCell->MappedChildCell;
		}
		return SourceChildCell;
	}

	FIntVector ResolveMappedParentCell(
		const FLayoutChildStageMappingResult& StageMapping,
		const FIntVector& SourceChildCell,
		const FIntVector& FallbackOffset)
	{
		if (const FLayoutChildStageMappedCell* MappedCell = StageMapping.Cells.FindByPredicate(
			[&SourceChildCell](const FLayoutChildStageMappedCell& Candidate)
			{
				return Candidate.SourceChildCell == SourceChildCell;
			}))
		{
			return MappedCell->ParentCell;
		}
		return TranslateCell(SourceChildCell, FallbackOffset);
	}

	bool BuildCommittedPartitionSeams(
		const FString& ParentRegionDebugPath,
		const TArray<FAutomaticChildPlacement>& Placements,
		TArray<FLayoutPartitionSeamRecord>& OutPlannedSeams,
		FString* OutFailureReason);

	bool ApplyOwnedSeamUsageRestrictions(
		const FString& ParentRegionDebugPath,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		FLayoutRegionSolveRequest& InOutParentRequest,
		TArray<FAutomaticChildPlacement>& InOutPlacements,
		FString& OutFailureReason);

	void ApplyCommittedSiblingDoorTraversalToPlacements(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		TArray<FAutomaticChildPlacement>& InOutPlacements);

	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnership(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedParentCells,
		const TArray<FAutomaticChildPlacement>& Placements);

	bool TryBuildParentPlannedCellsWithReservedChildren(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ProtectedTraversalCells,
		int32 EntrySelectionSeed,
		bool bAllowExteriorEntryRelocation,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FLayoutVerticalAccessHostGroup>& VerticalAccessHostGroups,
		TArray<FLayoutPlannedCell>& OutParentPlannedCells,
		TArray<FLayoutVerticalAccessHostGroup>* OutFilteredHostGroups,
		FString* OutFailureReason);

	bool RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
		const TArray<FLayoutPlannedCell>& DerivedParentPlannedCells,
		FCommittedVerticalAccessOwnership& InOutVerticalAccessOwnership,
		FString* OutFailureReason);
	bool BuildParentValidationRequest(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections,
		const TArray<FAutomaticChildPlacement>& CandidatePlacements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		bool bAuthorityCellsRemainSelectable,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FString& OutValidationFailureReason,
		FLayoutRegionSolveRequest& OutParentRequest,
		FCommittedVerticalAccessOwnership* OutEffectiveVerticalAccessOwnership);

	bool TryRefreshPlacementBackedNegotiatedContract(
		const LayoutRegionScheduleSolverFacade::FRecursiveScheduleSolveContext& SolveContext,
		const LayoutRegionScheduleSolverFacade::FCompiledStructuralInputs& StructuralInputs,
		FAutomaticChildPlacement& InOutPlacement,
		LayoutRegionScheduleSolverFacade::FNegotiatedDemandResult& OutDemandResult,
		FString& OutFailureReason);

	void ApplyCommittedVerticalAccessOwnershipToNegotiatedContract(
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FString& ChildRegionDebugPath,
		FLayoutNegotiatedChildResponsibilityContract& InOutContract);

	bool PropagateRemainingPlacementDemandFeasibility(
		const FString& SchedulerStateKey,
		const FLayoutRegionSolveRequest& RootRequest,
		const FRecursivePlacementDemandSearcher& Searcher,
		int32 NextDemandIndex,
		FRecursiveChildPlacementSchedulerState& InOutSchedulerState,
		FString& OutFailureReason);

	FString DescribeChildPlacementContext(
		const FLayoutRegionSolveRequest& RootRequest,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot);

	FString IndentMultiline(const FString& Text, const FString& Prefix);

	bool CanRequiredDemandStillFitPartialState(
		const FString& SchedulerStateKey,
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FChildPlacementDemand>& PlacementDemands,
		int32 DemandIndex,
		const FChildPlacementDemand& Demand,
		const FPreparedChildPlacementSource& PreparedSource,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TSet<FIntVector>& CurrentReservedParentCells,
		TMap<FString, FEntryCommitmentOptionMemo>& InOutEntryCommitmentOptionMemo,
		FString& OutFailureReason);

	void AddUniqueRejectedCandidateReason(
		const FString& Reason,
		TArray<FString>& InOutRejectedReasons);

	int32 ScoreSharedSiblingSeamOpportunity(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FIntVector& CandidateOffset,
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements);

	int32 GetNearestCellDistance(
		const FIntVector& SourceCell,
		const TArray<FIntVector>& TargetCells);

	int32 GetManhattanDistance(const FIntVector& A, const FIntVector& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y) + FMath::Abs(A.Z - B.Z);
	}

	FLayoutId BuildEntryAnchorTemplateCommitmentId(int32 EntryOrdinal);

	FLayoutId BuildChildEntryAnchorCommitmentId(const FString& ChildRegionDebugPath, int32 EntryOrdinal);

	int32 GetParentContactIntentScore(ELayoutCellIntent Intent);

	bool ShouldTrackDetailedRecursiveSchedulerPerf();

	int32 CountNonDoorSharedParentChildFaces(
		const TArray<FSharedParentChildFace>& SharedParentChildFaces)
	{
		TSet<uint32> UniqueSharedFaces;
		for (const FSharedParentChildFace& SharedFace : SharedParentChildFaces)
		{
			if (SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor)
			{
				continue;
			}

			UniqueSharedFaces.Add(HashCombineFast(
				GetTypeHash(SharedFace.ParentCell),
				static_cast<uint32>(SharedFace.FaceDirection)));
		}

		return UniqueSharedFaces.Num();
	}

	void AccumulateTraversalBoundaryBurden(
		const TArray<FLayoutSolveBoundaryPoint>& ParentTranslatedBoundaryPoints,
		int32& OutTraversalBoundaryPointCount,
		int32& OutTraversalBoundaryCellCount,
		int32& OutMultiTraversalBoundaryCellCount,
		int32& OutBoundaryFacingTraversalPointCount)
	{
		OutTraversalBoundaryPointCount = 0;
		OutTraversalBoundaryCellCount = 0;
		OutMultiTraversalBoundaryCellCount = 0;
		OutBoundaryFacingTraversalPointCount = 0;

		TMap<FIntVector, int32> TraversalPointCountByCell;
		for (const FLayoutSolveBoundaryPoint& BoundaryPoint :
			ParentTranslatedBoundaryPoints)
		{
			if (BoundaryPoint.ConnectedTraversalChannels.IsEmpty())
			{
				continue;
			}

			++OutTraversalBoundaryPointCount;
			if (BoundaryPoint.bRequiresBoundaryFacing)
			{
				++OutBoundaryFacingTraversalPointCount;
			}
			TraversalPointCountByCell.FindOrAdd(BoundaryPoint.LocalCell) += 1;
		}

		OutTraversalBoundaryCellCount = TraversalPointCountByCell.Num();
		for (const TPair<FIntVector, int32>& Entry :
			TraversalPointCountByCell)
		{
			if (Entry.Value > 1)
			{
				++OutMultiTraversalBoundaryCellCount;
			}
		}
	}

	int32 CountNegotiatedInterfaceTraversalAnchors(
		const TArray<FLayoutNegotiatedLevelInterfaceContract>& InterfaceContracts)
	{
		int32 Count = 0;
		for (const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract :
			InterfaceContracts)
		{
			Count += InterfaceContract.TraversalAnchors.Num();
		}
		return Count;
	}

	FChildPlacementSelectionFeatures BuildChildPlacementSelectionFeatures(
		const FAutomaticChildPlacement& Placement,
		const FLayoutRegionSolveRequest& RootRequest)
	{
		FChildPlacementSelectionFeatures Features;
		Features.bUsesDirectContact = !Placement.ParentCommittedEntryCells.IsEmpty();
		Features.ParentBoundaryTouchCount =
			Placement.ScoreBreakdown.ParentBoundaryTouchCount;
		Features.ParentChildSeamScore =
			Placement.ScoreBreakdown.ParentChildSeamScore;
		for (const FLayoutPlannedCell& TranslatedCell :
			Placement.ParentTranslatedPlannedCells)
		{
			if (TranslatedCell.Cell.X == 0
				|| TranslatedCell.Cell.Y == 0
				|| TranslatedCell.Cell.X == RootRequest.FootprintSize.X - 1
				|| TranslatedCell.Cell.Y == RootRequest.FootprintSize.Y - 1)
			{
				++Features.ParentExteriorReservedCellCount;
			}
		}
		if (const FLayoutRegionContentEntrySolveSnapshot* Entry =
				RootRequest.ContentSetSnapshot.Entries.FindByPredicate(
					[&Placement](const FLayoutRegionContentEntrySolveSnapshot& Candidate)
					{
						return Candidate.EntryId == Placement.EntryId;
					}))
		{
			Features.AuthoredPlacementZone = Entry->ChildPlacementZone;
		}
		Features.ParentContactCount = Placement.ParentCommittedEntryCells.Num();
		Features.ParentCommittedTraversalAnchorCount =
			Placement.ParentCommittedTraversalAnchors.Num();
		Features.ReservedParentCellCount =
			Placement.ParentPlanReservedCells.Num();
		Features.NonDoorSharedParentChildFaceCount =
			CountNonDoorSharedParentChildFaces(Placement.SharedParentChildFaces);
		AccumulateTraversalBoundaryBurden(
			Placement.ParentTranslatedBoundaryPoints,
			Features.TraversalBoundaryPointCount,
			Features.TraversalBoundaryCellCount,
			Features.MultiTraversalBoundaryCellCount,
			Features.BoundaryFacingTraversalPointCount);
		Features.NegotiatedHostVerticalAccessResponsibility =
			Placement.NegotiatedResponsibilityContract
				.HostVerticalAccessResponsibility;
		Features.bHasRequiredHostIngressAnchor =
			Placement.NegotiatedResponsibilityContract
				.bHasRequiredHostIngressAnchor;
		Features.bHasRequiredHostEgressAnchor =
			Placement.NegotiatedResponsibilityContract
				.bHasRequiredHostEgressAnchor;
		Features.RequiredChildGenerallyConnectableAnchorPairId =
			Placement.NegotiatedResponsibilityContract
				.RequiredChildGenerallyConnectableAnchorPairId;
		Features.NegotiatedRouteCellCount =
			Placement.NegotiatedResponsibilityContract
				.RequiredChildInternalVerticalRouteCells.Num();
		Features.NegotiatedRouteSpanLevelCount =
			Placement.NegotiatedResponsibilityContract
				.RequiredChildInternalVerticalSpanLevels.Num();
		Features.NegotiatedInterfaceTraversalAnchorCount =
			CountNegotiatedInterfaceTraversalAnchors(
				Placement.NegotiatedResponsibilityContract
					.CommittedParentChildInterfacesByLevel);
		return Features;
	}

	FResolvedCommitmentScoreBreakdown BuildResolvedCommitmentScoreBreakdown(
		const TArray<FResolvedChildEntryCommitmentCandidate::FScoreBreakdown>& EntryScoreBreakdowns,
		int32 ResolveAdjustmentTotal);

	bool IsCellOnFootprintPerimeter(
		const FIntVector& LocalCell,
		const FIntPoint& FootprintSize);

	bool TryGetSharedOverlapFaceDirections(
		const FIntVector& FirstCell,
		const FIntPoint& FirstFootprintSize,
		const FIntVector& SecondCell,
		const FIntPoint& SecondFootprintSize,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection);

	bool DoChildSeamCapabilitiesSupportSpecificSharedInterface(
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		ELayoutFaceDirection FirstFaceDirection,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& RequiredInterfaceFamily);

	bool TryChooseSpecificSharedInterfaceOwner(
		const FString& FirstRegionPath,
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		ELayoutFaceDirection FirstFaceDirection,
		const FString& SecondRegionPath,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& RequiredInterfaceFamily,
		FString& OutOwnerRegionPath,
		FString& OutPassiveRegionPath);

	uint32 BuildChildCandidateTieBreakHash(
		int32 RootSeed,
		FName EntryId,
		const FIntVector& CandidateOffset,
		int32 InstanceOrdinal,
		int32 VariantIndex);

	FString BuildRejectedChildOffsetMessage(
		const FIntVector& CandidateOffset,
		const TArray<FIntVector>& CandidateParentContactCells,
		const FParentProbeEvaluation& ParentProbeEvaluation);

	void UpdateBestRejectedCandidateReason(
		int32 CandidateScore,
		const FString& RejectedMessage,
		int32& InOutBestRejectedCandidateScore,
		FString& InOutBestRejectedCandidateReason);

	int32 CountAttachedSeamSupportRun(
		const TSet<FIntVector>& PlannedCells,
		const FIntVector& AnchorCell,
		ELayoutFaceDirection FaceDirection);

	bool DoesChildPlacementRespectProtectedParentEntryNeighbors(
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TSet<FIntVector>& ChildPlannedCellSet,
		const FIntVector& CandidateOffset,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		FString* OutFailureReason);

	FResolvedChildNoParentEntryOption ResolveChildEntryWithoutDirectParentContact(
		const FIntVector& CandidateEntryCell,
		const FLayoutChildCapabilityEndpoint& Capability,
		const FIntVector& CandidateOffset,
		const FLayoutChildStageMappingResult& StageMapping,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const FChildPlacementDemand& Demand,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements);

	FString BuildChildEntryCommitmentOptionMemoKey(
		const FString& SchedulerStateKey,
		FName EntryId,
		int32 VariantIndex,
		const FIntVector& CandidateOffset);

	bool CollectChildEntryCandidateOptionsByEntry(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& OutCandidateOptionsByEntry,
		FString* OutFailureReason = nullptr);

	bool CollectChildEntryCommitmentOptionsForOffsetCached(
		const FString& MemoKey,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TMap<FString, FEntryCommitmentOptionMemo>& InOutMemo,
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>*& OutCandidateOptionsByEntry,
		FString* OutFailureReason)
	{
		if (const FEntryCommitmentOptionMemo* ExistingMemo = InOutMemo.Find(MemoKey))
		{
			OutCandidateOptionsByEntry = &ExistingMemo->CandidateOptionsByEntry;
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = ExistingMemo->FailureReason;
			}
			return ExistingMemo->bSucceeded;
		}

		FEntryCommitmentOptionMemo& NewMemo = InOutMemo.Add(MemoKey);
		NewMemo.bSucceeded = CollectChildEntryCandidateOptionsByEntry(
			EntrySnapshot,
			InterfaceSummary,
			NormalizedCandidate,
			ParentPlannedCellSet,
			ReservedParentCells,
			ParentProtectedTraversalCells,
			ParentPriorityTargets,
			ResolveEntryWithoutParentContact,
			NewMemo.CandidateOptionsByEntry,
			&NewMemo.FailureReason);
		OutCandidateOptionsByEntry = &NewMemo.CandidateOptionsByEntry;
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = NewMemo.FailureReason;
		}
		return NewMemo.bSucceeded;
	}

	bool CollectChildEntryCommitmentOptionsForOffsetCached(
		const FString& MemoKey,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const FString& ChildRegionDebugPath,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TMap<FString, FEntryCommitmentOptionMemo>& InOutMemo,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& OutCandidateOptionsByEntry,
		FString* OutFailureReason);

	bool CollectChildEntryCommitmentOptionsForOffset(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const FString& ChildRegionDebugPath,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& OutCandidateOptionsByEntry,
		FString* OutFailureReason);

	bool BuildChildEntryCandidatesForEntryCell(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const TArray<FLayoutChildCapabilityEndpoint>& EntryCapabilities,
		const TArray<FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate::FParentContactOpportunity>& ParentContactOpportunities,
		const FIntVector& ChildEntryCell,
		const int32 EntryOrdinal,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TArray<FResolvedChildEntryCommitmentCandidate>& OutEntryCandidates,
		FString* OutFailureReason = nullptr)
	{
		OutEntryCandidates.Reset();
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		for (const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate::FParentContactOpportunity& Opportunity : ParentContactOpportunities)
		{
			if (Opportunity.ChildEntryCell != ChildEntryCell
				|| ReservedParentCells.Contains(Opportunity.ParentContactCell))
			{
				continue;
			}

			if (!EntrySnapshot.bChildContributesHostVerticalAccess
				&& Opportunity.ParentIntent == ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}

			if (!EntrySnapshot.bChildContributesHostVerticalAccess
				&& Opportunity.ParentCapabilitySupportScore <= 1)
			{
				continue;
			}

			const int32 IntentScore = GetParentContactIntentScore(Opportunity.ParentIntent);
			if (IntentScore <= (MIN_int32 / 8)
				|| !ParentPlannedCellSet.Contains(Opportunity.ParentContactCell))
			{
				continue;
			}

			int32 ParentSupportNeighborCount = 0;
			static const ELayoutFaceDirection ParentSupportDirections[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY
			};
			for (const ELayoutFaceDirection Direction : ParentSupportDirections)
			{
				const FIntVector NeighborCell =
					Opportunity.ParentContactCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (!ParentPlannedCellSet.Contains(NeighborCell)
					|| ReservedParentCells.Contains(NeighborCell))
				{
					continue;
				}

				++ParentSupportNeighborCount;
			}

			const int32 ProtectedTraversalPenalty =
				Opportunity.bProtectedTraversalCell ? 250 : 0;
			const int32 AnchoredIntentPenalty =
				(Opportunity.ParentIntent == ELayoutCellIntent::Entry || Opportunity.ParentIntent == ELayoutCellIntent::VerticalAccess)
					? 125
					: 0;

			FResolvedChildEntryCommitmentCandidate Candidate;
			Candidate.ParentContactCell = Opportunity.ParentContactCell;
			Candidate.ParentIntent = Opportunity.ParentIntent;
			Candidate.Anchor.CommitmentId = BuildEntryAnchorTemplateCommitmentId(EntryOrdinal);
			Candidate.Anchor.LocalCell = ChildEntryCell;
			Candidate.Anchor.FaceDirection = Opportunity.FaceDirection;
			Candidate.Anchor.ConnectionTag = Opportunity.ConnectionTag;
			Candidate.Anchor.AllowedConnectionTags = Opportunity.AllowedConnectionTags;
			Candidate.Anchor.TraversalChannels = Opportunity.TraversalChannels;
			Candidate.Anchor.bRequireMatchingYawWithFilledNeighbor = false;
			Candidate.ChildLocalCell = ChildEntryCell;
			Candidate.FaceDirection = Opportunity.FaceDirection;
			Candidate.BoundaryWitnessId = Opportunity.BoundaryWitnessId;
			const int32 ParentCapabilitySupportContribution =
				Opportunity.ParentCapabilitySupportScore * 40;
			const int32 ParentSupportContribution =
				ParentSupportNeighborCount * 35;
			const int32 PriorityDistancePenalty =
				GetNearestCellDistance(Opportunity.ParentContactCell, ParentPriorityTargets) * 10;
			Candidate.Score =
				IntentScore
				+ ParentCapabilitySupportContribution
				+ ParentSupportContribution
				- ProtectedTraversalPenalty
				- AnchoredIntentPenalty
				- PriorityDistancePenalty;
			Candidate.ScoreBreakdown.bHasDiagnostics =
				ShouldTrackDetailedRecursiveSchedulerPerf();
			Candidate.ScoreBreakdown.IntentScore = IntentScore;
			Candidate.ScoreBreakdown.ParentCapabilitySupportContribution =
				ParentCapabilitySupportContribution;
			Candidate.ScoreBreakdown.ParentSupportContribution =
				ParentSupportContribution;
			Candidate.ScoreBreakdown.ProtectedTraversalPenalty =
				-ProtectedTraversalPenalty;
			Candidate.ScoreBreakdown.AnchoredIntentPenalty =
				-AnchoredIntentPenalty;
			Candidate.ScoreBreakdown.PriorityDistancePenalty =
				-PriorityDistancePenalty;
			Candidate.ScoreBreakdown.TotalScore = Candidate.Score;
			OutEntryCandidates.Add(Candidate);
		}

		const bool bHasNonVerticalAccessDirectParentContact = OutEntryCandidates.ContainsByPredicate(
			[](const FResolvedChildEntryCommitmentCandidate& Candidate)
			{
				return Candidate.bUsesDirectParentContact
					&& Candidate.ParentIntent != ELayoutCellIntent::VerticalAccess;
			});
		if (!EntrySnapshot.bChildContributesHostVerticalAccess && bHasNonVerticalAccessDirectParentContact)
		{
			OutEntryCandidates.RemoveAll(
				[](const FResolvedChildEntryCommitmentCandidate& Candidate)
				{
					return Candidate.bUsesDirectParentContact
						&& Candidate.ParentIntent == ELayoutCellIntent::VerticalAccess;
				});
		}

		FResolvedChildNoParentEntryOption NoParentResolution;
		for (const FLayoutChildCapabilityEndpoint& Capability : EntryCapabilities)
		{
			if (Capability.FaceDirection == ELayoutFaceDirection::PosZ
				|| Capability.FaceDirection == ELayoutFaceDirection::NegZ)
			{
				continue;
			}

			const FResolvedChildNoParentEntryOption CandidateResolution =
				ResolveEntryWithoutParentContact(ChildEntryCell, Capability);
			if (CandidateResolution.ResolutionMode != FResolvedChildEntryCommitmentCandidate::EResolutionMode::DirectParentContact)
			{
				NoParentResolution = CandidateResolution;
				break;
			}
		}

		if (NoParentResolution.ResolutionMode != FResolvedChildEntryCommitmentCandidate::EResolutionMode::DirectParentContact)
		{
			FResolvedChildEntryCommitmentCandidate& Candidate = OutEntryCandidates.AddDefaulted_GetRef();
			Candidate.bUsesDirectParentContact = false;
			Candidate.ResolutionMode = NoParentResolution.ResolutionMode;
			Candidate.ChildLocalCell = ChildEntryCell;
			Candidate.FaceDirection = NoParentResolution.FaceDirection;
			Candidate.Anchor.CommitmentId = BuildEntryAnchorTemplateCommitmentId(EntryOrdinal);
			Candidate.Anchor.LocalCell = ChildEntryCell;
			Candidate.Anchor.FaceDirection = Candidate.FaceDirection;
			Candidate.Anchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
			Candidate.Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);
			Candidate.Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
			Candidate.Anchor.TraversalChannels.Reset();
			Candidate.Score =
				NoParentResolution.ResolutionMode == FResolvedChildEntryCommitmentCandidate::EResolutionMode::SiblingSharedDoor
					? 175
					: 100;
			Candidate.ScoreBreakdown.bHasDiagnostics =
				ShouldTrackDetailedRecursiveSchedulerPerf();
			Candidate.ScoreBreakdown.NoParentResolutionScore = Candidate.Score;
			Candidate.ScoreBreakdown.TotalScore = Candidate.Score;
		}

		const bool bHasUnprotectedDirectParentContact = OutEntryCandidates.ContainsByPredicate(
			[&ParentProtectedTraversalCells](const FResolvedChildEntryCommitmentCandidate& Candidate)
			{
				return Candidate.bUsesDirectParentContact
					&& !ParentProtectedTraversalCells.Contains(Candidate.ParentContactCell);
			});
		if (bHasUnprotectedDirectParentContact)
		{
			OutEntryCandidates.RemoveAll(
				[&ParentProtectedTraversalCells](const FResolvedChildEntryCommitmentCandidate& Candidate)
				{
					return Candidate.bUsesDirectParentContact
						&& ParentProtectedTraversalCells.Contains(Candidate.ParentContactCell);
				});
		}

		if (OutEntryCandidates.IsEmpty())
		{
			if (NoParentResolution.ResolutionMode != FResolvedChildEntryCommitmentCandidate::EResolutionMode::DirectParentContact)
			{
				return true;
			}

			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Child entry at local cell %s could not find any compatible parent contact cell.\nEntry Snapshot: %s\nProblem: No parent Entry, Connector, VerticalAccess, Interior, or Core cell adjacent to the translated child entry satisfied the child endpoint capability contract.\nFix: Move or rotate the child so its entry can face a traversable parent cell, or broaden the child entry capability/parent corridor layout."),
					*ChildEntryCell.ToString(),
					*EntrySnapshot.EntryId.ToString());
			}
			return false;
		}

		OutEntryCandidates.Sort([](const FResolvedChildEntryCommitmentCandidate& Left, const FResolvedChildEntryCommitmentCandidate& Right)
		{
			if (Left.Score != Right.Score)
			{
				return Left.Score > Right.Score;
			}
			if (Left.ParentContactCell.Y != Right.ParentContactCell.Y)
			{
				return Left.ParentContactCell.Y < Right.ParentContactCell.Y;
			}
			return Left.ParentContactCell.X < Right.ParentContactCell.X;
		});

		static constexpr int32 MaxDirectParentContactCandidatesPerEntry = 32;
		if (OutEntryCandidates.Num() > MaxDirectParentContactCandidatesPerEntry)
		{
			TArray<FResolvedChildEntryCommitmentCandidate> TrimmedCandidates;
			TrimmedCandidates.Reserve(MaxDirectParentContactCandidatesPerEntry + 2);

			int32 DirectParentContactCount = 0;
			for (const FResolvedChildEntryCommitmentCandidate& Candidate : OutEntryCandidates)
			{
				if (!Candidate.bUsesDirectParentContact)
				{
					TrimmedCandidates.Add(Candidate);
					continue;
				}

				if (DirectParentContactCount < MaxDirectParentContactCandidatesPerEntry)
				{
					TrimmedCandidates.Add(Candidate);
					++DirectParentContactCount;
				}
			}

			OutEntryCandidates = MoveTemp(TrimmedCandidates);
		}

		return true;
	}

	bool CollectChildEntryCandidateOptionsByEntry(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& OutCandidateOptionsByEntry,
		FString* OutFailureReason)
	{
		OutCandidateOptionsByEntry.Reset();
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		if (InterfaceSummary.EntryCells.IsEmpty()
			&& NormalizedCandidate.ParentContactOpportunities.IsEmpty())
		{
			return true;
		}

		if (InterfaceSummary.EntryCapabilities.IsEmpty())
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Child entry commitments could not be built for '%s'.\nProblem: The child interface summary exposes no entry-capable endpoint contracts.\nFix: Add an entry-capable module/endpoint to the child content set or reduce the child profile entry requirement."),
					*EntrySnapshot.EntryId.ToString());
			}
			return false;
		}

		int32 EntryOrdinal = 0;

		// Collect all cells that need entry-commitment candidates: the
		// profile's configured entry cells plus any cells where the parent
		// probes from a direction not covered by the profile's entry layout.
		// This ensures the committed endpoint anchor points to the actual
		// cell facing the parent ingress, not just a pre-configured position.
		TArray<FIntVector> EffectiveEntryCells = InterfaceSummary.EntryCells;
		for (const auto& Opportunity : NormalizedCandidate.ParentContactOpportunities)
		{
			EffectiveEntryCells.AddUnique(Opportunity.ChildEntryCell);
		}

		for (const FIntVector& ChildEntryCell : EffectiveEntryCells)
		{
			TArray<FLayoutChildCapabilityEndpoint> EntryCapabilitiesForCell =
				InterfaceSummary.EntryCapabilities.FilterByPredicate(
					[&ChildEntryCell](const FLayoutChildCapabilityEndpoint& Capability)
					{
						return Capability.LocalCell.Z == ChildEntryCell.Z;
					});
			if (EntryCapabilitiesForCell.IsEmpty())
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Child entry commitments could not be built for '%s' at local cell %s.\nProblem: The child interface summary exposes no entry-capable endpoint contract on level %d for that entry cell.\nFix: Keep entry-capable endpoint offers on the same covered level as the authored entry cell, or move the entry cell onto a level that already exports a compatible host-facing endpoint."),
						*EntrySnapshot.EntryId.ToString(),
						*ChildEntryCell.ToString(),
						ChildEntryCell.Z);
				}
				return false;
			}

			TArray<FResolvedChildEntryCommitmentCandidate> EntryCandidates;
			if (!BuildChildEntryCandidatesForEntryCell(
				EntrySnapshot,
				EntryCapabilitiesForCell,
				NormalizedCandidate.ParentContactOpportunities,
				ChildEntryCell,
				EntryOrdinal,
				ParentPlannedCellSet,
				ReservedParentCells,
				ParentProtectedTraversalCells,
				ParentPriorityTargets,
				ResolveEntryWithoutParentContact,
				EntryCandidates,
				OutFailureReason))
			{
				return false;
			}

			OutCandidateOptionsByEntry.Add(MoveTemp(EntryCandidates));
			++EntryOrdinal;
		}

		return true;
	}

	void StampChildRegionDebugPathOnEntryCommitmentCandidates(
		const FString& ChildRegionDebugPath,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& InOutCandidateOptionsByEntry)
	{
		for (int32 EntryOrdinal = 0; EntryOrdinal < InOutCandidateOptionsByEntry.Num(); ++EntryOrdinal)
		{
			const FLayoutId StampedCommitmentId =
				BuildChildEntryAnchorCommitmentId(ChildRegionDebugPath, EntryOrdinal);
			for (FResolvedChildEntryCommitmentCandidate& Candidate : InOutCandidateOptionsByEntry[EntryOrdinal])
			{
				Candidate.Anchor.CommitmentId = StampedCommitmentId;
			}
		}
	}

	bool CollectChildEntryCommitmentOptionsForOffset(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const FString& ChildRegionDebugPath,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& OutCandidateOptionsByEntry,
		FString* OutFailureReason)
	{
		const bool bCollected = CollectChildEntryCandidateOptionsByEntry(
			EntrySnapshot,
			InterfaceSummary,
			NormalizedCandidate,
			ParentPlannedCellSet,
			ReservedParentCells,
			ParentProtectedTraversalCells,
			ParentPriorityTargets,
			ResolveEntryWithoutParentContact,
			OutCandidateOptionsByEntry,
			OutFailureReason);
		if (bCollected)
		{
			StampChildRegionDebugPathOnEntryCommitmentCandidates(
				ChildRegionDebugPath,
				OutCandidateOptionsByEntry);
		}

		return bCollected;
	}

	FString BuildChildEntryCommitmentOptionMemoKey(
		const FString& SchedulerStateKey,
		const FName EntryId,
		const int32 VariantIndex,
		const FIntVector& CandidateOffset)
	{
		return FString::Printf(
			TEXT("%s|Entry=%s|Variant=%d|Offset=%s"),
			*SchedulerStateKey,
			*EntryId.ToString(),
			VariantIndex,
			*CandidateOffset.ToString());
	}

	bool CollectChildEntryCommitmentOptionsForOffsetCached(
		const FString& MemoKey,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const FString& ChildRegionDebugPath,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		TMap<FString, FEntryCommitmentOptionMemo>& InOutMemo,
		TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& OutCandidateOptionsByEntry,
		FString* OutFailureReason)
	{
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>* CachedCandidateOptionsByEntry = nullptr;
		const bool bCollected = CollectChildEntryCommitmentOptionsForOffsetCached(
			MemoKey,
			EntrySnapshot,
			InterfaceSummary,
			NormalizedCandidate,
			ParentPlannedCellSet,
			ReservedParentCells,
			ParentProtectedTraversalCells,
			ParentPriorityTargets,
			ResolveEntryWithoutParentContact,
			InOutMemo,
			CachedCandidateOptionsByEntry,
			OutFailureReason);
		if (CachedCandidateOptionsByEntry == nullptr)
		{
			OutCandidateOptionsByEntry.Reset();
			return false;
		}

		OutCandidateOptionsByEntry = *CachedCandidateOptionsByEntry;
		StampChildRegionDebugPathOnEntryCommitmentCandidates(
			ChildRegionDebugPath,
			OutCandidateOptionsByEntry);
		return bCollected;
	}

	bool ResolveChildEntryCommitmentOptions(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& CandidateOptionsByEntry,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments,
		TArray<FLayoutCommittedEndpointAnchor>& OutCommitments,
		TArray<FIntVector>& OutParentContactCells,
		TArray<FLayoutSolveBoundaryPoint>& OutParentTranslatedBoundaryPoints,
		int32& OutScore,
		FResolvedCommitmentScoreBreakdown* OutBestResolvedScoreBreakdown,
		TArray<FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination>* OutSuccessfulCommitmentCombinations,
		FString* OutFailureReason,
		FRecursiveSchedulerPerfMetrics* PerfMetrics = nullptr);

	bool CanResolveAnyChildEntryCommitmentOptions(
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& CandidateOptionsByEntry,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments);

	void DeduplicateResolvedChildEntryCommitmentCandidates(
		TArray<FResolvedChildEntryCommitmentCandidate>& InOutCandidates);

	uint32 GetStableGameplayTagContainerHash(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		uint32 Hash = 0;
		for (const FGameplayTag& Tag : SortedTags)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Tag));
		}
		return Hash;
	}

	bool operator==(const FCommittedEndpointAnchorSignature& Left, const FCommittedEndpointAnchorSignature& Right)
	{
		return Left.LocalCell == Right.LocalCell
			&& Left.FaceDirection == Right.FaceDirection
			&& Left.ConnectionTag == Right.ConnectionTag
			&& Left.AllowedConnectionTags == Right.AllowedConnectionTags
			&& Left.TraversalChannels == Right.TraversalChannels
			&& Left.bRequireMatchingYawWithFilledNeighbor == Right.bRequireMatchingYawWithFilledNeighbor;
	}

	uint32 GetTypeHash(const FCommittedEndpointAnchorSignature& Signature)
	{
		uint32 Hash = GetTypeHash(Signature.LocalCell);
		Hash = HashCombineFast(Hash, static_cast<uint32>(Signature.FaceDirection));
		Hash = HashCombineFast(Hash, GetTypeHash(Signature.ConnectionTag));
		Hash = HashCombineFast(Hash, GetStableGameplayTagContainerHash(Signature.AllowedConnectionTags));
		Hash = HashCombineFast(Hash, GetStableGameplayTagContainerHash(Signature.TraversalChannels));
		Hash = HashCombineFast(Hash, Signature.bRequireMatchingYawWithFilledNeighbor ? 1u : 0u);
		return Hash;
	}

	bool operator==(const FResolvedChildEntryCommitmentCandidateSignature& Left, const FResolvedChildEntryCommitmentCandidateSignature& Right)
	{
		return Left.bUsesDirectParentContact == Right.bUsesDirectParentContact
			&& Left.ResolutionMode == Right.ResolutionMode
			&& Left.ChildLocalCell == Right.ChildLocalCell
			&& Left.Anchor == Right.Anchor
			&& Left.ParentContactCell == Right.ParentContactCell;
	}

	uint32 GetTypeHash(const FResolvedChildEntryCommitmentCandidateSignature& Signature)
	{
		uint32 Hash = Signature.bUsesDirectParentContact ? 1u : 0u;
		Hash = HashCombineFast(Hash, static_cast<uint32>(Signature.ResolutionMode));
		Hash = HashCombineFast(Hash, GetTypeHash(Signature.ChildLocalCell));
		Hash = HashCombineFast(Hash, GetTypeHash(Signature.Anchor));
		Hash = HashCombineFast(Hash, GetTypeHash(Signature.ParentContactCell));
		return Hash;
	}

	bool operator==(const FSuccessfulCommitmentCombinationSignature& Left, const FSuccessfulCommitmentCombinationSignature& Right)
	{
		return Left.Commitments == Right.Commitments
			&& Left.ParentContactCells == Right.ParentContactCells
			&& Left.NoParentResolutionEntries == Right.NoParentResolutionEntries;
	}

	uint32 GetTypeHash(const FSuccessfulCommitmentCombinationSignature& Signature)
	{
		uint32 Hash = 0;
		for (const FCommittedEndpointAnchorSignature& Commitment : Signature.Commitments)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Commitment));
		}
		for (const FIntVector& ParentContactCell : Signature.ParentContactCells)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(ParentContactCell));
		}
		for (const FResolvedChildEntryCommitmentCandidateSignature& Entry : Signature.NoParentResolutionEntries)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Entry));
		}
		return Hash;
	}

	FCommittedEndpointAnchorSignature BuildCommittedEndpointAnchorSignature(
		const FLayoutCommittedEndpointAnchor& Commitment)
	{
		FCommittedEndpointAnchorSignature Signature;
		Signature.LocalCell = Commitment.LocalCell;
		Signature.FaceDirection = Commitment.FaceDirection;
		Signature.ConnectionTag = Commitment.ConnectionTag;
		Signature.AllowedConnectionTags = Commitment.AllowedConnectionTags;
		Signature.TraversalChannels = Commitment.TraversalChannels;
		Signature.bRequireMatchingYawWithFilledNeighbor =
			Commitment.bRequireMatchingYawWithFilledNeighbor;
		return Signature;
	}

	FResolvedChildEntryCommitmentCandidateSignature BuildResolvedChildEntryCommitmentCandidateSignature(
		const FResolvedChildEntryCommitmentCandidate& Candidate)
	{
		FResolvedChildEntryCommitmentCandidateSignature Signature;
		Signature.bUsesDirectParentContact = Candidate.bUsesDirectParentContact;
		Signature.ResolutionMode = Candidate.ResolutionMode;
		Signature.ChildLocalCell = Candidate.ChildLocalCell;
		Signature.Anchor = BuildCommittedEndpointAnchorSignature(Candidate.Anchor);
		Signature.ParentContactCell = Candidate.ParentContactCell;
		return Signature;
	}

	FSuccessfulCommitmentCombinationSignature BuildSuccessfulCommitmentCombinationSignature(
		const TArray<FLayoutCommittedEndpointAnchor>& Commitments,
		const TArray<FIntVector>& ParentContactCells,
		const TArray<FResolvedChildEntryCommitmentCandidate>& NoParentResolutionEntries)
	{
		FSuccessfulCommitmentCombinationSignature Signature;
		Signature.Commitments.Reserve(Commitments.Num());
		for (const FLayoutCommittedEndpointAnchor& Commitment : Commitments)
		{
			Signature.Commitments.Add(BuildCommittedEndpointAnchorSignature(Commitment));
		}
		Signature.ParentContactCells = ParentContactCells;
		Signature.NoParentResolutionEntries.Reserve(NoParentResolutionEntries.Num());
		for (const FResolvedChildEntryCommitmentCandidate& Entry : NoParentResolutionEntries)
		{
			Signature.NoParentResolutionEntries.Add(
				BuildResolvedChildEntryCommitmentCandidateSignature(Entry));
		}
		return Signature;
	}

	FString BuildRuntimeTagsToStableKey(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		TArray<FString> Parts;
		Parts.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			Parts.Add(Tag.ToString());
		}

		return FString::Join(Parts, TEXT("|"));
	}

	FString BuildCommittedEndpointAnchorGeometryKey(
		const TArray<FLayoutCommittedEndpointAnchor>& Commitments)
	{
		TArray<FString> CommitmentParts;
		CommitmentParts.Reserve(Commitments.Num());
		for (const FLayoutCommittedEndpointAnchor& Commitment : Commitments)
		{
			CommitmentParts.Add(FString::Printf(
				TEXT("%s:%d:%s:%s:%s:%d"),
				*Commitment.LocalCell.ToString(),
				static_cast<int32>(Commitment.FaceDirection),
				Commitment.ConnectionTag.IsValid() ? *Commitment.ConnectionTag.ToString() : TEXT("<none>"),
				*BuildRuntimeTagsToStableKey(Commitment.AllowedConnectionTags),
				*BuildRuntimeTagsToStableKey(Commitment.TraversalChannels),
				Commitment.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
		}
		CommitmentParts.Sort();
		return CommitmentParts.IsEmpty() ? TEXT("<none>") : FString::Join(CommitmentParts, TEXT(","));
	}

	FString BuildParentContactCellsKey(const TArray<FIntVector>& ParentContactCells)
	{
		TArray<FString> Parts;
		Parts.Reserve(ParentContactCells.Num());
		for (const FIntVector& Cell : ParentContactCells)
		{
			Parts.Add(Cell.ToString());
		}
		Parts.Sort();
		return Parts.IsEmpty() ? TEXT("<none>") : FString::Join(Parts, TEXT(","));
	}

	FString BuildResolvedChildEntryCommitmentCandidateGeometryKey(
		const TArray<FResolvedChildEntryCommitmentCandidate>& Candidates)
	{
		TArray<FString> CandidateParts;
		CandidateParts.Reserve(Candidates.Num());
		for (const FResolvedChildEntryCommitmentCandidate& Candidate : Candidates)
		{
			TArray<FLayoutCommittedEndpointAnchor> SingleCommitment;
			SingleCommitment.Add(Candidate.Anchor);
			CandidateParts.Add(FString::Printf(
				TEXT("%d:%d:%s:%s:%s"),
				Candidate.bUsesDirectParentContact ? 1 : 0,
				static_cast<int32>(Candidate.ResolutionMode),
				*Candidate.ChildLocalCell.ToString(),
				*Candidate.ParentContactCell.ToString(),
				*BuildCommittedEndpointAnchorGeometryKey(SingleCommitment)));
		}
		CandidateParts.Sort();
		return CandidateParts.IsEmpty() ? TEXT("<none>") : FString::Join(CandidateParts, TEXT(","));
	}

	FString BuildSuccessfulCommitmentCombinationKey(
		const TArray<FLayoutCommittedEndpointAnchor>& Commitments,
		const TArray<FIntVector>& ParentContactCells,
		const TArray<FResolvedChildEntryCommitmentCandidate>& NoParentResolutionEntries)
	{
		return FString::Printf(
			TEXT("Commitments=%s|Contacts=%s|NoParent=%s"),
			*BuildCommittedEndpointAnchorGeometryKey(Commitments),
			*BuildParentContactCellsKey(ParentContactCells),
			*BuildResolvedChildEntryCommitmentCandidateGeometryKey(NoParentResolutionEntries));
	}

	bool ShouldTrackDetailedRecursiveSchedulerPerf()
	{
		static const bool bShouldTrackDetailedPerf =
			!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty();
		return bShouldTrackDetailedPerf;
	}

	FRecursiveSchedulerCombinationFamilyKey BuildRecursiveSchedulerCombinationFamilyKey(
		const int32 EntryCount,
		const int32 ParentContactCount,
		const int32 DirectContactCount,
		const int32 SiblingSharedDoorCount,
		const int32 ParentSharedDoorCount)
	{
		FRecursiveSchedulerCombinationFamilyKey FamilyKey;
		FamilyKey.EntryCount = EntryCount;
		FamilyKey.ParentContactCount = ParentContactCount;
		FamilyKey.DirectContactCount = DirectContactCount;
		FamilyKey.SiblingSharedDoorCount = SiblingSharedDoorCount;
		FamilyKey.ParentSharedDoorCount = ParentSharedDoorCount;
		return FamilyKey;
	}

	FRecursiveSchedulerCombinationFamilyStats& FindOrAddRecursiveSchedulerCombinationFamilyStats(
		FRecursiveSchedulerPerfMetrics& PerfMetrics,
		const FRecursiveSchedulerCombinationFamilyKey& FamilyKey,
		const int32 EntryCount,
		const int32 ParentContactCount,
		const int32 DirectContactCount,
		const int32 SiblingSharedDoorCount,
		const int32 ParentSharedDoorCount)
	{
		FRecursiveSchedulerCombinationFamilyStats& FamilyStats =
			PerfMetrics.CombinationFamilyStatsByKey.FindOrAdd(FamilyKey);
		FamilyStats.EntryCount = EntryCount;
		FamilyStats.ParentContactCount = ParentContactCount;
		FamilyStats.DirectContactCount = DirectContactCount;
		FamilyStats.SiblingSharedDoorCount = SiblingSharedDoorCount;
		FamilyStats.ParentSharedDoorCount = ParentSharedDoorCount;
		return FamilyStats;
	}

	FString ExtractRecursiveSchedulerFailureFirstLine(const FString& FailureReason)
	{
		int32 NewlineIndex = INDEX_NONE;
		if (FailureReason.FindChar(TEXT('\n'), NewlineIndex))
		{
			return FailureReason.Left(NewlineIndex);
		}
		return FailureReason;
	}

	template <typename TPairArrayBuilder>
	FString BuildTopRecursiveSchedulerFailureSample(
		const int32 MaxLoggedKinds,
		TPairArrayBuilder&& PairArrayBuilder)
	{
		TArray<TPair<FString, int32>> RankedFailures = PairArrayBuilder();
		RankedFailures.Sort([](const TPair<FString, int32>& Left, const TPair<FString, int32>& Right)
		{
			if (Left.Value != Right.Value)
			{
				return Left.Value > Right.Value;
			}
			return Left.Key < Right.Key;
		});

		TArray<FString> Parts;
		for (int32 Index = 0; Index < RankedFailures.Num() && Index < MaxLoggedKinds; ++Index)
		{
			Parts.Add(FString::Printf(
				TEXT("%s(%d)"),
				*RankedFailures[Index].Key,
				RankedFailures[Index].Value));
		}

		return Parts.IsEmpty()
			? FString(TEXT("<none>"))
			: FString::Join(Parts, TEXT(", "));
	}

	FString DescribeRecursiveSchedulerTimeoutSummary(
		const FRecursivePlacementDemandSearcher& Searcher,
		const int32 DemandIndex,
		const int32 CurrentScore,
		const FString& ExistingFailureReason)
	{
		const FRecursiveChildPlacementSchedulerState& SchedulerState =
			Searcher.SchedulerState;
		const FRecursiveSchedulerPerfMetrics& PerfMetrics =
			SchedulerState.PerfMetrics;
		const double ElapsedSeconds =
			FPlatformTime::Seconds() - PerfMetrics.StartSeconds;

		const FString FullProofFailureSample =
			BuildTopRecursiveSchedulerFailureSample(
				3,
				[&PerfMetrics]()
				{
					TArray<TPair<FString, int32>> Pairs;
					Pairs.Reserve(PerfMetrics.FullProofFailureStatsByKind.Num());
					for (const TPair<FLayoutId, FRecursiveSchedulerFullProofFailureStats>& Pair :
						PerfMetrics.FullProofFailureStatsByKind)
					{
						const FString Sample =
							Pair.Value.SampleFirstLine.IsEmpty()
								? Pair.Key.ToString()
								: Pair.Value.SampleFirstLine;
						Pairs.Emplace(Sample.Left(120), Pair.Value.Count);
					}
					return Pairs;
				});
		const FString StructuralRejectSample =
			BuildTopRecursiveSchedulerFailureSample(
				3,
				[&PerfMetrics]()
				{
					TArray<TPair<FString, int32>> Pairs;
					Pairs.Reserve(
						PerfMetrics.StructuralRejectFailureStatsByFirstLine.Num());
					for (const TPair<FString, FRecursiveSchedulerFullProofFailureStats>& Pair :
						PerfMetrics.StructuralRejectFailureStatsByFirstLine)
					{
						Pairs.Emplace(Pair.Key.Left(120), Pair.Value.Count);
					}
					return Pairs;
				});

		FString TimeoutSummary = FString::Printf(
			TEXT("Recursive child-placement scheduling exceeded the solve budget.\n  Region: %s\n  Elapsed: %.2fs / %.2fs\n  Demand Index: %d / %d\n  Current Score: %d\n  Current Placements: %d\n  Search States Visited: %d\n  Parent Probe Calls: %d\n  Parent Probe Memo Hits: %d\n  Parent Full Proof Calls: %d\n  Structural Rejects: %d\n  Enumerated Commitment Combinations: %d\n  Full Proof Rejections: %s\n  Structural Reject Samples: %s"),
			*Searcher.RootRequest.RegionDebugPath,
			ElapsedSeconds,
			Searcher.RootRequest.ExecutionSettings.MaxSolveDurationSeconds,
			DemandIndex,
			Searcher.PlacementDemands.Num(),
			CurrentScore,
			SchedulerState.CurrentPlacements.Num(),
			PerfMetrics.SearchStatesVisited,
			PerfMetrics.ParentProbeCalls,
			PerfMetrics.ParentProbeMemoHits,
			PerfMetrics.ParentProbeFullSolveCalls,
			PerfMetrics.ParentProbeStructuralRejects,
			PerfMetrics.EnumeratedCommitmentCombinationCount,
			*FullProofFailureSample,
			*StructuralRejectSample);
		if (!ExistingFailureReason.IsEmpty())
		{
			TimeoutSummary += TEXT("\nLast observed downstream failure:\n");
			TimeoutSummary += ExistingFailureReason;
		}
		return TimeoutSummary;
	}

	bool ShouldAbortRecursiveSchedulerForTimeout(
		const FRecursivePlacementDemandSearcher& Searcher,
		const int32 DemandIndex,
		const int32 CurrentScore,
		FString& OutSearchFailureReason)
	{
		if (!LayoutSolveExecution::Checkpoint(OutSearchFailureReason)) return true;
		const double MaxSolveDurationSeconds =
			Searcher.RootRequest.ExecutionSettings.MaxSolveDurationSeconds;
		if (MaxSolveDurationSeconds <= 0.0)
		{
			return false;
		}

		const double ElapsedSeconds =
			FPlatformTime::Seconds() - Searcher.SchedulerState.PerfMetrics.StartSeconds;
		if (ElapsedSeconds < MaxSolveDurationSeconds)
		{
			return false;
		}

		TRACE_BOOKMARK(TEXT("Layout_SchedulerTimeout region=%s seed=%d demand=%d elapsedMs=%.3f budgetMs=%.3f probes=%d"),
			*Searcher.RootRequest.RegionDebugPath.Left(128), Searcher.RootRequest.Seed, DemandIndex, ElapsedSeconds * 1000.0,
			MaxSolveDurationSeconds * 1000.0, Searcher.SchedulerState.PerfMetrics.ParentProbeCalls);
		OutSearchFailureReason = DescribeRecursiveSchedulerTimeoutSummary(
			Searcher,
			DemandIndex,
			CurrentScore,
			OutSearchFailureReason);
		return true;
	}

	FLayoutId ClassifyRecursiveSchedulerFullProofFailureKind(const FParentProbeEvaluation& Evaluation)
	{
		const FString FirstLine = ExtractRecursiveSchedulerFailureFirstLine(Evaluation.FailureReason);
		if (FirstLine.Contains(TEXT("Propagation found no legal candidates"), ESearchCase::IgnoreCase))
		{
			return TEXT("PropagationNoCandidates");
		}
		if (FirstLine.Contains(TEXT("Required traversal anchor could not be connected"), ESearchCase::IgnoreCase))
		{
			return TEXT("ReachabilityFallbackAnchor");
		}
		if (FirstLine.Contains(TEXT("would face protected parent entry cell"), ESearchCase::IgnoreCase))
		{
			return TEXT("ProtectedParentEntryBoundary");
		}
		if (FirstLine.Contains(TEXT("boundary-facing side is not allowed on the region boundary"), ESearchCase::IgnoreCase))
		{
			return TEXT("BoundaryFacingConstraint");
		}
		if (FirstLine.Contains(TEXT("committed endpoint anchor"), ESearchCase::IgnoreCase))
		{
			return TEXT("CommittedAnchorCoverage");
		}
		if (FirstLine.Contains(TEXT("Layout vertical region solve failed"), ESearchCase::IgnoreCase))
		{
			return TEXT("OtherSolveFailure");
		}
		return TEXT("OtherFullProofFailure");
	}

	void RecordRecursiveSchedulerFullProofFailure(
		FRecursiveSchedulerPerfMetrics& PerfMetrics,
		const FParentProbeEvaluation& Evaluation)
	{
		if (!Evaluation.bRanImmediateFullParentProof || Evaluation.bSucceeded)
		{
			return;
		}

		const FLayoutId FailureKind = ClassifyRecursiveSchedulerFullProofFailureKind(Evaluation);
		FRecursiveSchedulerFullProofFailureStats& FailureStats =
			PerfMetrics.FullProofFailureStatsByKind.FindOrAdd(FailureKind);
		++FailureStats.Count;
		if (FailureStats.SampleFirstLine.IsEmpty())
		{
			FailureStats.SampleFirstLine =
				ExtractRecursiveSchedulerFailureFirstLine(Evaluation.FailureReason);
		}
	}

	void RecordRecursiveSchedulerStructuralRejectFailure(
		FRecursiveSchedulerPerfMetrics& PerfMetrics,
		const FString& FailureReason)
	{
		FString FailureSummary = ExtractRecursiveSchedulerFailureFirstLine(
			FailureReason).TrimStartAndEnd();
		if (FailureSummary.Equals(
			TEXT("Scheduler traversal feasibility rejected the parent plan before final validation."),
			ESearchCase::CaseSensitive))
		{
			FString IgnoredFirstLine;
			FString RemainingLines;
			if (FailureReason.Split(TEXT("\n"), &IgnoredFirstLine, &RemainingLines))
			{
				FString StructuralDetailLine;
				if (RemainingLines.Split(TEXT("\n"), &StructuralDetailLine, nullptr))
				{
					FailureSummary = StructuralDetailLine.TrimStartAndEnd();
				}
				else if (!RemainingLines.IsEmpty())
				{
					FailureSummary = RemainingLines.TrimStartAndEnd();
				}
			}
		}

		FailureSummary = FailureSummary.Left(160);
		const FString NormalizedFailureFirstLine =
			FailureSummary.IsEmpty() ? TEXT("<empty>") : FailureSummary;
		FRecursiveSchedulerFullProofFailureStats& FailureStats =
			PerfMetrics.StructuralRejectFailureStatsByFirstLine.FindOrAdd(
				NormalizedFailureFirstLine);
		++FailureStats.Count;
		if (FailureStats.SampleFirstLine.IsEmpty())
		{
			FailureStats.SampleFirstLine = NormalizedFailureFirstLine;
		}
	}

	void SearchChildEntryCommitmentAssignments(
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& CandidateOptionsByEntry,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments,
		const int32 EntryIndex,
		const int32 AccumulatedScore,
		FChildEntryCommitmentResolutionState& InOutResolutionState,
		TArray<FLayoutCommittedEndpointAnchor>& OutCommitments,
		TArray<FIntVector>& OutParentContactCells,
		TArray<FLayoutSolveBoundaryPoint>& OutParentTranslatedBoundaryPoints,
		int32& OutScore)
	{
		if (InOutResolutionState.bStopSearch)
		{
			return;
		}

		if (EntryIndex >= CandidateOptionsByEntry.Num())
		{
			int32 ScoreAdjustment = 0;
			TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
			const double CallbackStartSeconds =
				ShouldTrackDetailedRecursiveSchedulerPerf()
					? FPlatformTime::Seconds()
					: 0.0;
			const bool bResolvedCommitmentsSucceeded = EvaluateResolvedCommitments(
				InOutResolutionState.CurrentCommitments,
				InOutResolutionState.CurrentParentContactCells,
				InOutResolutionState.CurrentNoParentResolutionEntries,
				ScoreAdjustment,
				ParentTranslatedBoundaryPoints);
			if (ShouldTrackDetailedRecursiveSchedulerPerf())
			{
				InOutResolutionState.CallbackEvaluationSeconds +=
					(FPlatformTime::Seconds() - CallbackStartSeconds);
			}
			if (!bResolvedCommitmentsSucceeded)
			{
				return;
			}

				const FResolvedCommitmentScoreBreakdown ScoreBreakdown =
					BuildResolvedCommitmentScoreBreakdown(
						InOutResolutionState.CurrentEntryScoreBreakdowns,
						ScoreAdjustment);
					const int32 TotalScore =
						AccumulatedScore
						+ ScoreAdjustment
						- ScoreBreakdown.DirectParentSupportSaturationPenalty;
					InOutResolutionState.bResolvedAnyCommitmentCombination = true;
					const FSuccessfulCommitmentCombinationSignature CombinationKey =
						BuildSuccessfulCommitmentCombinationSignature(
							InOutResolutionState.CurrentCommitments,
							InOutResolutionState.CurrentParentContactCells,
							InOutResolutionState.CurrentNoParentResolutionEntries);
			InOutResolutionState.SuccessfulCommitmentCombinationKeys.Add(CombinationKey);
			if (InOutResolutionState.bTrackSuccessfulCommitmentCombinations)
			{
				FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination* ExistingCombination =
					InOutResolutionState.SuccessfulCommitmentCombinationsByKey.Find(CombinationKey);
				const bool bShouldStoreCombination =
					ExistingCombination == nullptr || TotalScore > ExistingCombination->TotalScore;
				if (bShouldStoreCombination)
				{
					FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& CombinationToStore =
						InOutResolutionState.SuccessfulCommitmentCombinationsByKey.FindOrAdd(CombinationKey);
					CombinationToStore.CanonicalKey.Reset();
					CombinationToStore.TotalScore = TotalScore;
					CombinationToStore.Commitments = InOutResolutionState.CurrentCommitments;
					CombinationToStore.ParentContactCells = InOutResolutionState.CurrentParentContactCells;
					CombinationToStore.NoParentResolutionEntries =
						InOutResolutionState.CurrentNoParentResolutionEntries;
					CombinationToStore.ScoreBreakdown = ScoreBreakdown;
				}
			}
			if (OutCommitments.IsEmpty() || TotalScore > InOutResolutionState.BestResolvedScore)
			{
				InOutResolutionState.BestResolvedScore = TotalScore;
				InOutResolutionState.BestResolvedScoreBreakdown = ScoreBreakdown;
				OutCommitments = InOutResolutionState.CurrentCommitments;
				OutParentContactCells = InOutResolutionState.CurrentParentContactCells;
				OutParentTranslatedBoundaryPoints = MoveTemp(ParentTranslatedBoundaryPoints);
				OutScore = TotalScore;
			}

			const int32 MaxSuccessfulCommitmentCombinationsToEvaluate =
				CandidateOptionsByEntry.Num() > 1 ? 256 : 1;
			if (InOutResolutionState.SuccessfulCommitmentCombinationKeys.Num()
				>= MaxSuccessfulCommitmentCombinationsToEvaluate)
			{
				// Single-entry children can stop after the first feasible commitment, but
				// multi-entry children need to compare a broader set of feasible contact
				// combinations so commitment does not depend on whichever local contacts
				// happened to be evaluated first.
				InOutResolutionState.bStopSearch = true;
			}
			return;
		}

		for (const FResolvedChildEntryCommitmentCandidate& Candidate : CandidateOptionsByEntry[EntryIndex])
		{
			if (InOutResolutionState.bStopSearch)
			{
				return;
			}

			if (!Candidate.bUsesDirectParentContact)
			{
				InOutResolutionState.CurrentCommitments.Add(Candidate.Anchor);
				InOutResolutionState.CurrentNoParentResolutionEntries.Add(Candidate);
				InOutResolutionState.CurrentEntryScoreBreakdowns.Add(
					Candidate.ScoreBreakdown);
				SearchChildEntryCommitmentAssignments(
					CandidateOptionsByEntry,
					EvaluateResolvedCommitments,
					EntryIndex + 1,
					AccumulatedScore + Candidate.Score,
					InOutResolutionState,
					OutCommitments,
					OutParentContactCells,
					OutParentTranslatedBoundaryPoints,
					OutScore);
				InOutResolutionState.CurrentEntryScoreBreakdowns.Pop();
				InOutResolutionState.CurrentNoParentResolutionEntries.Pop();
				InOutResolutionState.CurrentCommitments.Pop();
				continue;
			}

			if (InOutResolutionState.UsedParentContactCells.Contains(Candidate.ParentContactCell))
			{
				continue;
			}

			InOutResolutionState.UsedParentContactCells.Add(Candidate.ParentContactCell);
			InOutResolutionState.CurrentCommitments.Add(Candidate.Anchor);
			InOutResolutionState.CurrentParentContactCells.Add(Candidate.ParentContactCell);
			InOutResolutionState.CurrentEntryScoreBreakdowns.Add(
				Candidate.ScoreBreakdown);
			SearchChildEntryCommitmentAssignments(
				CandidateOptionsByEntry,
				EvaluateResolvedCommitments,
				EntryIndex + 1,
				AccumulatedScore + Candidate.Score,
				InOutResolutionState,
				OutCommitments,
				OutParentContactCells,
				OutParentTranslatedBoundaryPoints,
				OutScore);
			InOutResolutionState.CurrentEntryScoreBreakdowns.Pop();
			InOutResolutionState.CurrentParentContactCells.Pop();
			InOutResolutionState.CurrentCommitments.Pop();
			InOutResolutionState.UsedParentContactCells.Remove(Candidate.ParentContactCell);
		}
	}

	// Explicit propagation only needs to know whether at least one commitment set
	// survives its lightweight feasibility predicate, so it can stop on the first hit.
	bool SearchAnyChildEntryCommitmentAssignment(
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& CandidateOptionsByEntry,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments,
		const int32 EntryIndex,
		TSet<FIntVector>& InOutUsedParentContactCells,
		TArray<FLayoutCommittedEndpointAnchor>& InOutCommitments,
		TArray<FIntVector>& InOutParentContactCells,
		TArray<FResolvedChildEntryCommitmentCandidate>& InOutNoParentResolutionEntries)
	{
		if (EntryIndex >= CandidateOptionsByEntry.Num())
		{
			int32 ScoreAdjustment = 0;
			TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
			return EvaluateResolvedCommitments(
				InOutCommitments,
				InOutParentContactCells,
				InOutNoParentResolutionEntries,
				ScoreAdjustment,
				ParentTranslatedBoundaryPoints);
		}

		for (const FResolvedChildEntryCommitmentCandidate& Candidate : CandidateOptionsByEntry[EntryIndex])
		{
			if (!Candidate.bUsesDirectParentContact)
			{
				InOutCommitments.Add(Candidate.Anchor);
				InOutNoParentResolutionEntries.Add(Candidate);
				const bool bResolved = SearchAnyChildEntryCommitmentAssignment(
					CandidateOptionsByEntry,
					EvaluateResolvedCommitments,
					EntryIndex + 1,
					InOutUsedParentContactCells,
					InOutCommitments,
					InOutParentContactCells,
					InOutNoParentResolutionEntries);
				InOutNoParentResolutionEntries.Pop();
				InOutCommitments.Pop();
				if (bResolved)
				{
					return true;
				}
				continue;
			}

			if (InOutUsedParentContactCells.Contains(Candidate.ParentContactCell))
			{
				continue;
			}

			InOutUsedParentContactCells.Add(Candidate.ParentContactCell);
			InOutCommitments.Add(Candidate.Anchor);
			InOutParentContactCells.Add(Candidate.ParentContactCell);
			const bool bResolved = SearchAnyChildEntryCommitmentAssignment(
				CandidateOptionsByEntry,
				EvaluateResolvedCommitments,
				EntryIndex + 1,
				InOutUsedParentContactCells,
				InOutCommitments,
				InOutParentContactCells,
				InOutNoParentResolutionEntries);
			InOutParentContactCells.Pop();
			InOutCommitments.Pop();
			InOutUsedParentContactCells.Remove(Candidate.ParentContactCell);
			if (bResolved)
			{
				return true;
			}
		}

		return false;
	}

	bool CanResolveAnyChildEntryCommitmentOptions(
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& CandidateOptionsByEntry,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments)
	{
		if (CandidateOptionsByEntry.IsEmpty())
		{
			return true;
		}

		TSet<FIntVector> UsedParentContactCells;
		TArray<FLayoutCommittedEndpointAnchor> Commitments;
		TArray<FIntVector> ParentContactCells;
		TArray<FResolvedChildEntryCommitmentCandidate> NoParentResolutionEntries;
		return SearchAnyChildEntryCommitmentAssignment(
			CandidateOptionsByEntry,
			EvaluateResolvedCommitments,
			0,
			UsedParentContactCells,
			Commitments,
			ParentContactCells,
			NoParentResolutionEntries);
	}

	bool ResolveChildEntryCommitmentOptions(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>& CandidateOptionsByEntry,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments,
		TArray<FLayoutCommittedEndpointAnchor>& OutCommitments,
		TArray<FIntVector>& OutParentContactCells,
		TArray<FLayoutSolveBoundaryPoint>& OutParentTranslatedBoundaryPoints,
		int32& OutScore,
		FResolvedCommitmentScoreBreakdown* OutBestResolvedScoreBreakdown,
		TArray<FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination>* OutSuccessfulCommitmentCombinations,
		FString* OutFailureReason,
		FRecursiveSchedulerPerfMetrics* PerfMetrics)
	{
		if (CandidateOptionsByEntry.IsEmpty())
		{
			return true;
		}

		const bool bTrackSuccessfulCommitmentCombinations =
			OutSuccessfulCommitmentCombinations != nullptr;
		const bool bTrackDetailedPerf =
			PerfMetrics != nullptr && ShouldTrackDetailedRecursiveSchedulerPerf();
		const double ResolutionStartSeconds =
			bTrackDetailedPerf ? FPlatformTime::Seconds() : 0.0;

		if (CandidateOptionsByEntry.Num() == 1)
		{
			double CallbackEvaluationSeconds = 0.0;
			bool bResolvedAnyCandidate = false;
			FResolvedCommitmentScoreBreakdown BestScoreBreakdown;
			TArray<FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination> SuccessfulCombinations;
			for (const FResolvedChildEntryCommitmentCandidate& Candidate : CandidateOptionsByEntry[0])
			{
				TArray<FLayoutCommittedEndpointAnchor> CandidateCommitments;
				CandidateCommitments.Reserve(1);
				CandidateCommitments.Add(Candidate.Anchor);

				TArray<FIntVector> CandidateParentContactCells;
				if (Candidate.bUsesDirectParentContact)
				{
					CandidateParentContactCells.Reserve(1);
					CandidateParentContactCells.Add(Candidate.ParentContactCell);
				}

				TArray<FResolvedChildEntryCommitmentCandidate> CandidateNoParentResolutionEntries;
				if (!Candidate.bUsesDirectParentContact)
				{
					CandidateNoParentResolutionEntries.Reserve(1);
					CandidateNoParentResolutionEntries.Add(Candidate);
				}

				int32 ScoreAdjustment = 0;
				TArray<FLayoutSolveBoundaryPoint> ParentTranslatedBoundaryPoints;
				const double CallbackStartSeconds =
					bTrackDetailedPerf ? FPlatformTime::Seconds() : 0.0;
				const bool bResolvedCommitmentsSucceeded = EvaluateResolvedCommitments(
					CandidateCommitments,
					CandidateParentContactCells,
					CandidateNoParentResolutionEntries,
					ScoreAdjustment,
					ParentTranslatedBoundaryPoints);
				if (bTrackDetailedPerf)
				{
					CallbackEvaluationSeconds +=
						(FPlatformTime::Seconds() - CallbackStartSeconds);
				}
				if (!bResolvedCommitmentsSucceeded)
				{
					continue;
				}

				const FResolvedCommitmentScoreBreakdown CandidateScoreBreakdown =
					BuildResolvedCommitmentScoreBreakdown(
						TArray<FResolvedChildEntryCommitmentCandidate::FScoreBreakdown>{
							Candidate.ScoreBreakdown},
						ScoreAdjustment);
				const int32 CandidateTotalScore =
					Candidate.Score
					+ ScoreAdjustment
					- CandidateScoreBreakdown.DirectParentSupportSaturationPenalty;
				if (!bResolvedAnyCandidate || CandidateTotalScore > OutScore)
				{
					bResolvedAnyCandidate = true;
					OutScore = CandidateTotalScore;
					OutCommitments = CandidateCommitments;
					OutParentContactCells = CandidateParentContactCells;
					OutParentTranslatedBoundaryPoints = ParentTranslatedBoundaryPoints;
					BestScoreBreakdown = CandidateScoreBreakdown;
				}

				if (bTrackSuccessfulCommitmentCombinations)
				{
					FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& Combination =
						SuccessfulCombinations.AddDefaulted_GetRef();
					Combination.TotalScore = CandidateTotalScore;
					Combination.Commitments = CandidateCommitments;
					Combination.ParentContactCells = CandidateParentContactCells;
					Combination.NoParentResolutionEntries = CandidateNoParentResolutionEntries;
					Combination.ScoreBreakdown = CandidateScoreBreakdown;
				}
			}

			if (bResolvedAnyCandidate)
			{
				if (OutBestResolvedScoreBreakdown != nullptr)
				{
					*OutBestResolvedScoreBreakdown = BestScoreBreakdown;
				}
				if (OutSuccessfulCommitmentCombinations != nullptr)
				{
					OutSuccessfulCommitmentCombinations->Reset();
					SuccessfulCombinations.Sort(
						[](const FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& Left,
							const FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& Right)
						{
							return Left.TotalScore > Right.TotalScore;
						});
					*OutSuccessfulCommitmentCombinations = MoveTemp(SuccessfulCombinations);
				}
				if (bTrackDetailedPerf)
				{
					PerfMetrics->CombinationEnumerationSeconds +=
						(FPlatformTime::Seconds() - ResolutionStartSeconds) - CallbackEvaluationSeconds;
				}
				return true;
			}

			if (OutFailureReason != nullptr && OutFailureReason->IsEmpty())
			{
				*OutFailureReason = FString::Printf(
					TEXT("Child entry commitments for '%s' could not be resolved together.\nProblem: Individual parent contacts existed, but no non-conflicting set of contacts survived parent-probe validation.\nFix: Move or rotate the child, adjust the parent circulation, or relax competing child contact demands."),
					*EntrySnapshot.EntryId.ToString());
			}
			if (bTrackDetailedPerf)
			{
				PerfMetrics->CombinationEnumerationSeconds +=
					(FPlatformTime::Seconds() - ResolutionStartSeconds) - CallbackEvaluationSeconds;
			}
			return false;
		}

		FChildEntryCommitmentResolutionState ResolutionState;
		ResolutionState.bTrackSuccessfulCommitmentCombinations =
			bTrackSuccessfulCommitmentCombinations;
		SearchChildEntryCommitmentAssignments(
			CandidateOptionsByEntry,
			EvaluateResolvedCommitments,
			0,
			0,
			ResolutionState,
			OutCommitments,
			OutParentContactCells,
			OutParentTranslatedBoundaryPoints,
			OutScore);
		if (OutBestResolvedScoreBreakdown != nullptr)
		{
			*OutBestResolvedScoreBreakdown =
				ResolutionState.BestResolvedScoreBreakdown;
		}
		if (bTrackDetailedPerf)
		{
			PerfMetrics->CombinationEnumerationSeconds +=
				(FPlatformTime::Seconds() - ResolutionStartSeconds) - ResolutionState.CallbackEvaluationSeconds;
		}
		if (OutSuccessfulCommitmentCombinations != nullptr)
		{
			OutSuccessfulCommitmentCombinations->Reset();
			const int32 SuccessfulCombinationCount =
				ResolutionState.SuccessfulCommitmentCombinationsByKey.Num();
			OutSuccessfulCommitmentCombinations->Reserve(SuccessfulCombinationCount);
			const bool bNeedsCanonicalSort = SuccessfulCombinationCount > 1;
			for (const TPair<FSuccessfulCommitmentCombinationSignature, FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination>& Pair :
				ResolutionState.SuccessfulCommitmentCombinationsByKey)
			{
				FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination Combination = Pair.Value;
				if (bNeedsCanonicalSort && Combination.CanonicalKey.IsEmpty())
				{
					Combination.CanonicalKey = BuildSuccessfulCommitmentCombinationKey(
						Combination.Commitments,
						Combination.ParentContactCells,
						Combination.NoParentResolutionEntries);
				}
				OutSuccessfulCommitmentCombinations->Add(MoveTemp(Combination));
			}
			if (bNeedsCanonicalSort)
			{
				OutSuccessfulCommitmentCombinations->Sort(
					[](const FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& Left,
						const FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& Right)
					{
						if (Left.TotalScore != Right.TotalScore)
						{
							return Left.TotalScore > Right.TotalScore;
						}

						return Left.CanonicalKey < Right.CanonicalKey;
					});
			}
			static constexpr int32 MaxStoredSuccessfulCommitmentCombinations = 128;
			if (OutSuccessfulCommitmentCombinations->Num() > MaxStoredSuccessfulCommitmentCombinations)
			{
				OutSuccessfulCommitmentCombinations->SetNum(
					MaxStoredSuccessfulCommitmentCombinations,
					EAllowShrinking::No);
			}
		}

		if (!ResolutionState.bResolvedAnyCommitmentCombination && OutFailureReason != nullptr && OutFailureReason->IsEmpty())
		{
			*OutFailureReason = FString::Printf(
				TEXT("Child entry commitments for '%s' could not be resolved together.\nProblem: Individual parent contacts existed, but no non-conflicting set of contacts survived parent-probe validation.\nFix: Move or rotate the child, adjust the parent circulation, or relax competing child contact demands."),
				*EntrySnapshot.EntryId.ToString());
		}

		return ResolutionState.bResolvedAnyCommitmentCombination;
	}

	bool BuildChildEntryCommitmentsForOffset(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate,
		const FString& ChildRegionDebugPath,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)>& ResolveEntryWithoutParentContact,
		const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)>& EvaluateResolvedCommitments,
		TArray<FLayoutCommittedEndpointAnchor>& OutCommitments,
		TArray<FIntVector>& OutParentContactCells,
		TArray<FLayoutSolveBoundaryPoint>& OutParentTranslatedBoundaryPoints,
		int32& OutScore,
		FString* OutFailureReason)
	{
		OutCommitments.Reset();
		OutParentContactCells.Reset();
		OutParentTranslatedBoundaryPoints.Reset();
		OutScore = 0;
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		TArray<TArray<FResolvedChildEntryCommitmentCandidate>> CandidateOptionsByEntry;
		if (!CollectChildEntryCommitmentOptionsForOffset(
			EntrySnapshot,
			InterfaceSummary,
			NormalizedCandidate,
			ChildRegionDebugPath,
			ParentPlannedCellSet,
			ReservedParentCells,
			ParentProtectedTraversalCells,
			ParentPriorityTargets,
			ResolveEntryWithoutParentContact,
			CandidateOptionsByEntry,
			OutFailureReason))
		{
			return false;
		}

		return ResolveChildEntryCommitmentOptions(
			EntrySnapshot,
			CandidateOptionsByEntry,
			EvaluateResolvedCommitments,
			OutCommitments,
			OutParentContactCells,
			OutParentTranslatedBoundaryPoints,
			OutScore,
			nullptr,
			nullptr,
			OutFailureReason,
			nullptr);
	}

	TArray<FLayoutCommittedTraversalAnchor> BuildCommittedTraversalAnchorsForParentContacts(
		const TArray<FLayoutCommittedEndpointAnchor>& DirectParentContactCommitments,
		const TArray<FIntVector>& CandidateParentContactCells);

	FResolvedCommitmentScoreBreakdown BuildResolvedCommitmentScoreBreakdown(
		const TArray<FResolvedChildEntryCommitmentCandidate::FScoreBreakdown>& EntryScoreBreakdowns,
		const int32 ResolveAdjustmentTotal)
	{
		FResolvedCommitmentScoreBreakdown Breakdown;
		Breakdown.ResolveAdjustmentTotal = ResolveAdjustmentTotal;
		TArray<int32, TInlineAllocator<8>> DirectParentSupportBonuses;
		for (const FResolvedChildEntryCommitmentCandidate::FScoreBreakdown& EntryBreakdown :
			EntryScoreBreakdowns)
		{
			Breakdown.bHasDiagnostics |= EntryBreakdown.bHasDiagnostics;
			Breakdown.EntryAccumulatedScore += EntryBreakdown.TotalScore;
			Breakdown.EntryIntentScore += EntryBreakdown.IntentScore;
			Breakdown.EntryParentCapabilitySupportContribution +=
				EntryBreakdown.ParentCapabilitySupportContribution;
			Breakdown.EntryParentSupportContribution +=
				EntryBreakdown.ParentSupportContribution;
			Breakdown.EntryProtectedTraversalPenalty +=
				EntryBreakdown.ProtectedTraversalPenalty;
			Breakdown.EntryAnchoredIntentPenalty +=
				EntryBreakdown.AnchoredIntentPenalty;
			Breakdown.EntryPriorityDistancePenalty +=
				EntryBreakdown.PriorityDistancePenalty;
			Breakdown.EntryNoParentResolutionScore +=
				EntryBreakdown.NoParentResolutionScore;
			if (EntryBreakdown.NoParentResolutionScore != 0)
			{
				++Breakdown.NoParentEntryCount;
			}
			else
			{
				++Breakdown.DirectParentEntryCount;
				DirectParentSupportBonuses.Add(
					EntryBreakdown.ParentCapabilitySupportContribution
					+ EntryBreakdown.ParentSupportContribution);
			}
		}

		static constexpr int32 SupportedDirectParentEntryBudget = 3;
		if (DirectParentSupportBonuses.Num() > SupportedDirectParentEntryBudget)
		{
			DirectParentSupportBonuses.Sort(
				[](const int32 Left, const int32 Right)
				{
					return Left > Right;
				});
			for (int32 Index = SupportedDirectParentEntryBudget;
				Index < DirectParentSupportBonuses.Num();
				++Index)
			{
				Breakdown.DirectParentSupportSaturationPenalty +=
					DirectParentSupportBonuses[Index];
			}
		}

		return Breakdown;
	}

	bool CanChildReservationShareSiblingSeamCell(
		const FLayoutPlannedCell& ChildPlannedCell,
		const FIntVector& CandidateOffset,
		const FIntPoint& FootprintSize,
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		FString* OutFailureReason = nullptr,
		const FIntVector* ExplicitParentCell = nullptr);

	bool CanAnyRemainingPeerFitStructurally(
		const FPreparedChildPlacementSource& PreparedSource,
		const FPreparedChildPlacementSource::FPlanningVariant& AcceptedVariant,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TSet<FIntVector>& CurrentReservedParentCells,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& AcceptedCandidateParentPlanReservedCells,
		const FIntVector& AcceptedCandidateOffset,
		const FLayoutChildStageMappingResult& AcceptedStageMapping,
		const FChildPlacementDemand& AcceptedDemand);

	bool CanChildReservationShareSiblingSeamCell(
		const FLayoutPlannedCell& ChildPlannedCell,
		const FIntVector& CandidateOffset,
		const FIntPoint& FootprintSize,
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		FString* OutFailureReason,
		const FIntVector* ExplicitParentCell);

	int32 GetRequiredHostVerticalAccessProviderCount(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells);

	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnershipForProspectiveCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& CurrentReservedParentCells,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const FString& CandidateChildRegionDebugPath,
		bool bCandidateSupportsVerticalAccess,
		bool bCandidateContributesHostVerticalAccess,
		const TArray<FIntVector>& CandidateVerticalAccessLocalCells,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const TArray<FLayoutPlannedCell>& CandidateChildPlannedCells,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces);

	ELayoutNegotiatedHostVerticalAccessResponsibility DetermineNegotiatedHostVerticalAccessResponsibility(
		const FString& ChildRegionDebugPath,
		const FCommittedVerticalAccessOwnership& Ownership);

	bool TryPromoteChildStageMappingForCommittedRoute(
		FLayoutChildStageMappingResult& InOutMapping,
		const TArray<FLayoutCommittedEndpointAnchor>& Commitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessCells,
		TArray<FIntVector>& OutRequiredVerticalRouteCells,
		FString& OutFailureReason);

	bool TrySelectNegotiatedHostVerticalAccessAnchors(
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessCells,
		FLayoutCommittedEndpointAnchor& OutIngressAnchor,
		FLayoutCommittedEndpointAnchor& OutEgressAnchor,
		TArray<FIntVector>& OutRequiredChildRouteCells,
		FString& OutFailureReason);

	void BuildSuccessfulChildPlacementCandidatesForDemand(
		const FString& SchedulerStateKey,
		int32 DemandIndex,
		const FChildPlacementDemand& Demand,
		const FPreparedChildPlacementSource& PreparedSource,
		const FRecursivePlacementDemandSearcher& Searcher,
		FRecursiveChildPlacementSchedulerState& InOutSchedulerState,
		FChildPlacementCandidateBuildResult& OutResult);

	bool SearchPlacementDemand(
		int32 DemandIndex,
		int32 CurrentScore,
		const FString& SchedulerStateKey,
		FRecursivePlacementDemandSearcher& Searcher,
		FString& OutSearchFailureReason);

	bool IsCanonicalChildPlacementChoiceLess(
		const FCanonicalChildPlacementChoice& Left,
		const FCanonicalChildPlacementChoice& Right);

	FAcceptedChildPlacementSearchResult SearchWithAcceptedChildPlacement(
		const FPreparedChildPlacementSource& PreparedSource,
		const FPreparedChildPlacementSource::FPlanningVariant& SelectedVariant,
		const FChildPlacementDemand& Demand,
		const FScoredChildOffset& Candidate,
		int32 DemandIndex,
		int32 CurrentScore,
		FRecursivePlacementDemandSearcher& Searcher);

	bool ShouldPruneFailedSchedulerState(
		const FString& SchedulerStateKey,
		const int32 CurrentScore,
		const TMap<FString, int32>& FailedSchedulerStateBestScore,
		FString& OutSearchFailureReason)
	{
		if (const int32* SeenFailureScore = FailedSchedulerStateBestScore.Find(SchedulerStateKey))
		{
			if (CurrentScore <= *SeenFailureScore)
			{
				OutSearchFailureReason =
					TEXT("A previously explored recursive child-placement state failed again with no better score.");
				return true;
			}
		}

		return false;
	}

	bool FRecursivePlacementDemandSearcher::Search(
		const int32 DemandIndex,
		const int32 CurrentScore,
		FString& OutSearchFailureReason)
	{
		if (ShouldAbortRecursiveSchedulerForTimeout(
			*this,
			DemandIndex,
			CurrentScore,
			OutSearchFailureReason))
		{
			return false;
		}

		++SchedulerState.PerfMetrics.SearchStatesVisited;
		if (DemandIndex >= PlacementDemands.Num())
		{
			QueueDeferredCompletePlacementCandidate(
				RootRequest,
				RootRequest.RegionDebugPath,
				ParentFootprintSize,
				CurrentScore,
				SchedulerState.CurrentPlacements,
				SchedulerState.CurrentVerticalAccessOwnership,
				SchedulerState.CurrentSkippedOptionalPlacements,
				SchedulerState.CurrentWarnings,
				SchedulerState.bHasCurrentPreparedParentProofRequest,
				SchedulerState.bHasCurrentPreparedParentProofRequest
					? &SchedulerState.CurrentPreparedParentProofRequest
					: nullptr,
				SchedulerState.DeferredCompletePlacementCandidates,
				SchedulerState.DeferredCompletePlacementCandidateKeys);
			return true;
		}

		const FString SchedulerStateKey =
			BuildSchedulerPlacementStateKey(
				DemandIndex,
				SchedulerState.CurrentPlacements,
				SchedulerState.CurrentPlannedPartitionSeams,
				SchedulerState.CurrentVerticalAccessOwnership);
		if (ShouldPruneFailedSchedulerState(
			SchedulerStateKey,
			CurrentScore,
			SchedulerState.FailedSchedulerStateBestScore,
			OutSearchFailureReason))
		{
			return false;
		}

		return SearchPlacementDemand(
			DemandIndex,
			CurrentScore,
			SchedulerStateKey,
			*this,
			OutSearchFailureReason);
	}

	bool ShouldSkipSymmetricSameSourcePlacement(
		const FPreparedChildPlacementSource& PreparedSource,
		const FChildPlacementDemand& Demand,
		const FScoredChildOffset& Candidate,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TArray<FCanonicalChildPlacementChoice>& LastChosenPlacementBySource)
	{
		if (Demand.RequiredPlacementZoneComponentIndex != INDEX_NONE
			|| Demand.InstanceOrdinal <= 0
			|| !LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
		{
			return false;
		}

		const FAutomaticChildPlacement* ExistingSameSourcePlacement =
			CurrentPlacements.FindByPredicate([&PreparedSource](const FAutomaticChildPlacement& ExistingPlacement)
			{
				return ExistingPlacement.EntryId == PreparedSource.EntrySnapshot->EntryId;
			});
		const bool bPlacementUsesAsymmetricRecursiveAttachment =
			Candidate.ParentContactCells.IsEmpty()
			|| !Candidate.SharedParentChildFaces.IsEmpty()
			|| (ExistingSameSourcePlacement != nullptr
				&& (ExistingSameSourcePlacement->ParentCommittedEntryCells.IsEmpty()
					|| !ExistingSameSourcePlacement->SharedParentChildFaces.IsEmpty()));
		const FCanonicalChildPlacementChoice& LastChoice = LastChosenPlacementBySource[Demand.SourceIndex];
		const FCanonicalChildPlacementChoice CurrentChoice
		{
			true,
			Candidate.Offset,
			Candidate.VariantIndex
		};
		return !bPlacementUsesAsymmetricRecursiveAttachment
			&& LastChoice.bSet
			&& IsCanonicalChildPlacementChoiceLess(CurrentChoice, LastChoice);
	}

	void UpdateBestDownstreamFailureReason(
		const FChildPlacementDemand& Demand,
		const FScoredChildOffset& Candidate,
		const FAcceptedChildPlacementSearchResult& DownstreamSearchResult,
		FString& InOutBestDownstreamFailureReason)
	{
		if (!InOutBestDownstreamFailureReason.IsEmpty() || DownstreamSearchResult.FailureReason.IsEmpty())
		{
			return;
		}

		InOutBestDownstreamFailureReason = FString::Printf(
			TEXT("After accepting child placement:\n  Region: %s\n  Offset: %s\n  Variant: %d\n  Parent Contact Cells: %s\n%s"),
			*Demand.ChildRegionDebugPath,
			*Candidate.Offset.ToString(),
			Candidate.VariantIndex,
			*(Candidate.ParentContactCells.IsEmpty()
				? FString(TEXT("<none>"))
				: FString::JoinBy(Candidate.ParentContactCells, TEXT(", "), [](const FIntVector& Cell)
				{
					return Cell.ToString();
				})),
			*DownstreamSearchResult.FailureReason);
	}

	/** Builds one exact parent-owned hard-feature commitment for each requirement credited by a selected direct child. */
	void BuildDirectChildZoneFeatureProviderCommitments(
		const FLayoutRegionSolveRequest& RootRequest,
		const FPreparedChildPlacementSource& PreparedSource,
		const FLayoutChildStageMappingResult& StageMapping,
		TArray<FLayoutZoneFeatureProviderCommitment>& OutCommitments)
	{
		OutCommitments.Reset();
		check(PreparedSource.EntrySnapshot != nullptr);
		const FLayoutChildStageMappedCell* ProviderRoot = StageMapping.Cells.FindByPredicate(
			[](const FLayoutChildStageMappedCell& Cell)
			{
				return Cell.SourceChildCell == FIntVector::ZeroValue;
			});
		if (ProviderRoot == nullptr && !StageMapping.Cells.IsEmpty())
		{
			ProviderRoot = &StageMapping.Cells[0];
		}
		check(ProviderRoot != nullptr);

		TSet<FLayoutId> SeenRequirementIds;
		for (const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice :
			PreparedSource.HardFeatureProviderChoices)
		{
			if (SeenRequirementIds.Contains(Choice.RequirementId))
			{
				continue;
			}
			SeenRequirementIds.Add(Choice.RequirementId);

			FLayoutZoneFeatureProviderCommitment& Commitment =
				OutCommitments.AddDefaulted_GetRef();
			Commitment.RequirementId = Choice.RequirementId;
			Commitment.ProviderCommitmentId =
				LayoutZoneFeatureDemand::BuildProviderCommitmentId(
					RootRequest.EffectiveSnapshotId,
					RootRequest.RegionDebugPath,
					Choice.RequirementId,
					Choice.SourceContentEntryId,
					ProviderRoot->ParentCell,
					ProviderRoot->ModuleLevelIndex,
					ProviderRoot->TerrainStageIndex);
			Commitment.SourceRegionDebugPath = RootRequest.RegionDebugPath;
			Commitment.SourceContentEntryId = Choice.SourceContentEntryId;
			Commitment.Cell = ProviderRoot->ParentCell;
			Commitment.ModuleLevelIndex = ProviderRoot->ModuleLevelIndex;
			Commitment.TerrainStageIndex = ProviderRoot->TerrainStageIndex;
		}
		OutCommitments.Sort([](
			const FLayoutZoneFeatureProviderCommitment& Left,
			const FLayoutZoneFeatureProviderCommitment& Right)
		{
			return Left.ProviderCommitmentId.LexicalLess(
				Right.ProviderCommitmentId);
		});
	}

	bool FinalizeMappedChildVerticalAccess(FAutomaticChildPlacement& Placement, FString& OutFailureReason)
	{
		if (Placement.ChildRequest.bHasFinalizedSteppedTerrainIntents || !Placement.StageMapping.IsValid()) return true;
		TArray<FLayoutPlannedCell> ChildPlan = Placement.ChildRequest.PlannedCells;
		TArray<FLayoutVerticalAccessHostGroup> ChildHosts;
		if (!LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
			Placement.ChildRequest, ChildPlan, ChildHosts, OutFailureReason)) return false;
		if (ChildPlan.Num() != Placement.StageMapping.Cells.Num())
		{
			OutFailureReason = TEXT("Child stair preparation changed certified mapped geometry.");
			return false;
		}
		for (int32 Index = 0; Index < ChildPlan.Num(); ++Index)
		{
			if (ChildPlan[Index].Cell != Placement.StageMapping.Cells[Index].MappedChildCell)
			{
				OutFailureReason = TEXT("Child stair preparation reordered certified mapped geometry.");
				return false;
			}
		}
		Placement.ChildRequest.PlannedCells = ChildPlan;
		Placement.ChildRequest.PrecomputedPlannedCells = ChildPlan;
		Placement.ChildRequest.VerticalAccessHostGroups = ChildHosts;
		Placement.StageMapping.ChildLocalPlannedCells = MoveTemp(ChildPlan);
		Placement.StageMapping.ChildLocalVerticalAccessHostGroups = MoveTemp(ChildHosts);
		Placement.ChildVerticalAccessLocalCells.Reset();
		for (int32 Index = 0; Index < Placement.StageMapping.Cells.Num(); ++Index)
		{
			const auto& Cell = Placement.StageMapping.ChildLocalPlannedCells[Index];
			Placement.StageMapping.ParentTranslatedPlannedCells[Index] = Cell;
			Placement.StageMapping.ParentTranslatedPlannedCells[Index].Cell = Placement.StageMapping.Cells[Index].ParentCell;
			if (Cell.Intent == ELayoutCellIntent::VerticalAccess) Placement.ChildVerticalAccessLocalCells.Add(Cell.Cell);
		}
		Placement.ParentTranslatedPlannedCells = Placement.StageMapping.ParentTranslatedPlannedCells;
		Placement.DirectChildCommitment.StageMapping = Placement.StageMapping;
		Placement.ChildRequest.bHasFinalizedSteppedTerrainIntents = true;
		return true;
	}

	FAcceptedChildPlacementSearchResult SearchWithAcceptedChildPlacement(
		const FPreparedChildPlacementSource& PreparedSource,
		const FPreparedChildPlacementSource::FPlanningVariant& SelectedVariant,
		const FChildPlacementDemand& Demand,
		const FScoredChildOffset& Candidate,
		const int32 DemandIndex,
		const int32 CurrentScore,
		FRecursivePlacementDemandSearcher& Searcher)
	{
		FRecursiveChildPlacementSchedulerState& SchedulerState = Searcher.SchedulerState;
		FAcceptedChildPlacementSearchResult Result;

		const int32 PlacementIndex = SchedulerState.CurrentPlacements.Num();
		FAutomaticChildPlacement& Placement = SchedulerState.CurrentPlacements.AddDefaulted_GetRef();
		Placement.EntryId = PreparedSource.EntrySnapshot->EntryId;
		Placement.InstanceOrdinal = Demand.InstanceOrdinal;
		Placement.ChildRegionDebugPath = Demand.ChildRegionDebugPath;
		Placement.RegionCellOffset = Candidate.Offset;
		Placement.StageMapping = Candidate.StageMapping;
		Placement.ParentTranslatedBoundaryPoints = Candidate.ParentTranslatedBoundaryPoints;
		Placement.BoundaryCertificateId = Candidate.BoundaryCertificateId;
		Placement.ParentDomainRestrictions = Candidate.ParentDomainRestrictions;
		Placement.ChildDomainRestrictions = Candidate.ChildDomainRestrictions;
		Placement.CertifiedChildIncomingBoundaryPoints = Candidate.ChildIncomingBoundaryPoints;
		Placement.ChildRequest = SelectedVariant.ChildRequestTemplate;
		Placement.ChildCapabilityEnvelope = SelectedVariant.CapabilityEnvelope;
		Placement.ChildRequest.RegionDebugPath = Demand.ChildRegionDebugPath;
		Placement.ChildRequest.RegionCellOffset = Placement.RegionCellOffset;
		if (Demand.bSharedFeatureChoiceSlot)
		{
			// Selected hard-provider slots are required once frozen; authored
			// optional policy applies only before this counted choice settles.
			Placement.ChildRequest.bSourceContentEntryOptional = false;
		}
		/* !Placement.ChildRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Placement.ChildRequest.FootprintSize = SelectedVariant.FootprintSize;
		Placement.ChildRequest.PlannedCells = Placement.StageMapping.ChildLocalPlannedCells;
		Placement.ChildRequest.PrecomputedPlannedCells = Placement.ChildRequest.PlannedCells;
		Placement.ChildRequest.CandidateDomainRestrictions = Placement.ChildDomainRestrictions;
		Placement.ChildRequest.CandidateDomainCertificateId = Placement.BoundaryCertificateId;
		Placement.ChildRequest.IncomingBoundaryPoints = Placement.CertifiedChildIncomingBoundaryPoints;
		// Parent certification freezes child planned intents for every stage class.
		Placement.ChildRequest.bHasFinalizedSteppedTerrainIntents = true;
		const bool bLocalFlat = Placement.StageMapping.StageClass == ELayoutChildPlacementStageClass::LocalFlat;
		if (Placement.StageMapping.StageClass == ELayoutChildPlacementStageClass::InheritedStage
			|| Placement.StageMapping.StageClass == ELayoutChildPlacementStageClass::CrossStage
			|| bLocalFlat)
		{
			Placement.ChildRequest.bHasFinalizedSteppedTerrainIntents = false;
			Placement.ChildRequest.bHasSelectedModePlan = true;
			Placement.ChildRequest.SelectedModePlan.EnvironmentMode = bLocalFlat
				? ELayoutContractEnvironmentMode::NonSteppedWorldPlacement
				: ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
			Placement.ChildRequest.SelectedModePlan.bUsesSteppedTerrainTopology = !bLocalFlat;
			if (bLocalFlat)
			{
				Placement.ChildRequest.SelectedModePlan.ModePlanId =
					FLayoutContractPipeline::BuildModePlanId(Placement.ChildRequest.SelectedModePlan);
			}
			Placement.ChildRequest.PrecomputedFrozenTerrainContract =
				Placement.StageMapping.ChildLocalTerrainContract;
			Placement.ChildRequest.SteppedTerrainSupportMap =
				Placement.StageMapping.ChildLocalSteppedTerrainSupportMap;
			Placement.ChildRequest.PrecomputedSteppedTerrainSupportMap =
				Placement.StageMapping.ChildLocalSteppedTerrainSupportMap;
			// Independent child stairs do not determine parent feasibility. Prepare them
			// for complete candidate validation, not every speculative placement/Entry offer.
			Placement.ChildRequest.VerticalAccessHostGroups.Reset();
			if ((PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
				|| Candidate.bAllowsChildTraversalBridgeForCommittedContacts)
				&& !FinalizeMappedChildVerticalAccess(Placement, Result.FailureReason))
			{
				SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
				return Result;
			}
		}
		Placement.DirectChildCommitment.ParentRegionDebugPath = Searcher.RootRequest.RegionDebugPath;
		Placement.DirectChildCommitment.ChildRegionDebugPath = Demand.ChildRegionDebugPath;
		Placement.DirectChildCommitment.StageMapping = Placement.StageMapping;
		Placement.DirectChildCommitment.BoundaryWitnessIds = Candidate.BoundaryWitnessIds;
		Placement.DirectChildCommitment.FrozenBoundaryPoints =
			Placement.CertifiedChildIncomingBoundaryPoints;
		BuildDirectChildZoneFeatureProviderCommitments(
			Searcher.RootRequest,
			PreparedSource,
			Placement.StageMapping,
			Placement.DirectChildCommitment.ZoneFeatureProviderCommitments);
		Placement.DirectChildCommitment.EndpointCommitments = Candidate.Commitments;
		for (FLayoutCommittedEndpointAnchor& Commitment : Placement.DirectChildCommitment.EndpointCommitments)
		{
			Commitment.LocalCell = ResolveMappedChildCell(
				Placement.StageMapping,
				Commitment.LocalCell);
		}
		Placement.ChildRequest.CommittedEndpointAnchors = Placement.DirectChildCommitment.EndpointCommitments;
		Placement.ChildRequest.bUseSuppliedChildCapabilityEnvelope = true;
		Placement.ChildRequest.SuppliedChildCapabilityEnvelope = Placement.ChildCapabilityEnvelope;
		Placement.ChildRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath = Demand.ChildRegionDebugPath;
		Placement.bSupportsVerticalAccess = SelectedVariant.InterfaceSummary.bSupportsVerticalAccess;
		Placement.bContributesHostVerticalAccess = PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess;
		Placement.ChildVerticalAccessLocalCells.Reset();
		for (const FIntVector& VerticalAccessCell : SelectedVariant.InterfaceSummary.VerticalAccessCells)
		{
			Placement.ChildVerticalAccessLocalCells.AddUnique(ResolveMappedChildCell(
				Placement.StageMapping,
				VerticalAccessCell));
		}
		Placement.BoundaryWitnessIds = Candidate.BoundaryWitnessIds;
		Placement.ParentCommittedEntryCells = Candidate.ParentContactCells;
		Placement.ParentDirectContactCommitments = Candidate.DirectParentContactCommitments;
		Placement.ParentCommittedTraversalAnchors = Candidate.ParentCommittedTraversalAnchors;
		Placement.bAllowsChildTraversalBridgeForCommittedContacts =
			Candidate.bAllowsChildTraversalBridgeForCommittedContacts;
		Placement.ParentPlanReservedCells = Candidate.ParentPlanReservedCells;
		Placement.SharedParentChildFaces = Candidate.SharedParentChildFaces;
		Placement.ScoreBreakdown = Candidate.ScoreBreakdown;

		TSet<FIntVector> SharedParentChildCells;
		for (const FSharedParentChildFace& SharedFace : Placement.SharedParentChildFaces)
		{
			SharedParentChildCells.Add(SharedFace.ParentCell);
		}

		TSet<FIntVector> ReservedParentCellSet;
		for (const FIntVector& ReservedCell : Placement.ParentPlanReservedCells)
		{
			ReservedParentCellSet.Add(ReservedCell);
		}

		TArray<FIntVector> AddedReservedCells;
		Placement.ParentTranslatedPlannedCells = Placement.StageMapping.ParentTranslatedPlannedCells;
		for (const FLayoutPlannedCell& ParentTranslatedCell : Placement.ParentTranslatedPlannedCells)
		{
			if (!SharedParentChildCells.Contains(ParentTranslatedCell.Cell))
			{
				ReservedParentCellSet.Add(ParentTranslatedCell.Cell);
			}
			if (!SchedulerState.CurrentReservedParentCells.Contains(ParentTranslatedCell.Cell))
			{
				SchedulerState.CurrentReservedParentCells.Add(ParentTranslatedCell.Cell);
				AddedReservedCells.Add(ParentTranslatedCell.Cell);
			}
		}
		Placement.ParentPlanReservedCells.Reset();
		Placement.ParentPlanReservedCells.Reserve(ReservedParentCellSet.Num());
		for (const FIntVector& ReservedCell : ReservedParentCellSet)
		{
			Placement.ParentPlanReservedCells.Add(ReservedCell);
		}
		Placement.ParentPlanReservedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.Z < Right.Z;
		});
		for (const FIntVector& ChildSupportCell : SelectedVariant.StructuralSupportLocalCells)
		{
			const FLayoutChildStageMappedCell* MappedSupportCell =
				Placement.StageMapping.Cells.FindByPredicate(
					[&ChildSupportCell](const FLayoutChildStageMappedCell& MappedCell)
					{
						return MappedCell.SourceChildCell == ChildSupportCell;
					});
			if (MappedSupportCell != nullptr)
			{
				Placement.ParentTranslatedSupportingCells.Add(MappedSupportCell->ParentCell);
			}
		}

		const FCanonicalChildPlacementChoice PreviousChoice =
			SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex)
				? SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex]
				: FCanonicalChildPlacementChoice();
		const TArray<FLayoutPartitionSeamRecord> PreviousPlannedPartitionSeams =
			SchedulerState.CurrentPlannedPartitionSeams;
		const FCommittedVerticalAccessOwnership PreviousVerticalAccessOwnership =
			SchedulerState.CurrentVerticalAccessOwnership;
		const bool bHadPreviousPreparedParentProofRequest =
			SchedulerState.bHasCurrentPreparedParentProofRequest;
		const FLayoutRegionSolveRequest PreviousPreparedParentProofRequest =
			SchedulerState.CurrentPreparedParentProofRequest;
		if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
		{
			SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex].bSet = true;
			SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex].Offset = Candidate.Offset;
			SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex].VariantIndex = Candidate.VariantIndex;
		}
		if (!BuildCommittedPartitionSeams(
			Searcher.RootRequest.RegionDebugPath,
			SchedulerState.CurrentPlacements,
			SchedulerState.CurrentPlannedPartitionSeams,
			&Result.FailureReason))
		{
			for (const FIntVector& AddedCell : AddedReservedCells)
			{
				SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
			}
			if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
			{
				SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
			}
			SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
			SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
			SchedulerState.bHasCurrentPreparedParentProofRequest =
				bHadPreviousPreparedParentProofRequest;
			SchedulerState.CurrentPreparedParentProofRequest =
				PreviousPreparedParentProofRequest;
			SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
			return Result;
		}
		ApplyCommittedSiblingDoorTraversalToPlacements(
			SchedulerState.CurrentPlannedPartitionSeams,
			SchedulerState.CurrentPlacements);
		// Freeze the accepted child placement into the same negotiated contract
		// surface the placement-backed bridge uses before rebuilding any low-level
		// parent fill. Parent validation should consume the frozen contract, not a
		// parallel ownership side channel.
		LayoutRegionScheduleSolverFacade::FNegotiatedDemandResult RefreshedDemandResult;
		if (!TryRefreshPlacementBackedNegotiatedContract(
			Searcher.PreparedSolveContext,
			Searcher.PreparedStructuralInputs,
			Placement,
			RefreshedDemandResult,
			Result.FailureReason))
		{
			for (const FIntVector& AddedCell : AddedReservedCells)
			{
				SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
			}
			if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
			{
				SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
			}
			SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
			SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
			SchedulerState.bHasCurrentPreparedParentProofRequest =
				bHadPreviousPreparedParentProofRequest;
			SchedulerState.CurrentPreparedParentProofRequest =
				PreviousPreparedParentProofRequest;
			SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
			return Result;
		}
		SchedulerState.CurrentVerticalAccessOwnership =
			DetermineCommittedVerticalAccessOwnership(
				Searcher.RootRequest,
				Searcher.ParentPlannedCells,
				SchedulerState.CurrentReservedParentCells,
				SchedulerState.CurrentPlacements);
		TArray<FLayoutPlannedCell> DerivedParentPlannedCells;
		if (!TryBuildParentPlannedCellsWithReservedChildren(
			Searcher.ParentPlannedCells,
			Searcher.ParentPlannedCellSet,
			Searcher.ParentFootprintSize,
			SchedulerState.CurrentReservedParentCells,
			Searcher.ParentProtectedTraversalCells,
			Searcher.RootRequest.Seed,
			Searcher.RootRequest.IncomingBoundaryPoints.IsEmpty(),
			SchedulerState.CurrentVerticalAccessOwnership,
			Searcher.RootRequest.VerticalAccessHostGroups,
			DerivedParentPlannedCells,
			nullptr,
			&Result.FailureReason)
			|| !RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
				DerivedParentPlannedCells,
				SchedulerState.CurrentVerticalAccessOwnership,
				&Result.FailureReason))
		{
			for (const FIntVector& AddedCell : AddedReservedCells)
			{
				SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
			}
			if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
			{
				SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
			}
			SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
			SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
			SchedulerState.bHasCurrentPreparedParentProofRequest =
				bHadPreviousPreparedParentProofRequest;
			SchedulerState.CurrentPreparedParentProofRequest =
				PreviousPreparedParentProofRequest;
			SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
			return Result;
		}
		for (FAutomaticChildPlacement& CurrentPlacement : SchedulerState.CurrentPlacements)
		{
			// Child search refreshes placement-backed contracts before the derived
			// parent plan can settle final host-ascent ownership. Re-apply the
			// committed ownership summary after the derived plan pass so parent-owned
			// side rooms do not carry a stale zero-provider contract into validation.
			ApplyCommittedVerticalAccessOwnershipToNegotiatedContract(
				SchedulerState.CurrentVerticalAccessOwnership,
				CurrentPlacement.ChildRegionDebugPath,
				CurrentPlacement.NegotiatedResponsibilityContract);
			CurrentPlacement.DirectChildCommitment.NegotiatedResponsibilityContract =
				CurrentPlacement.NegotiatedResponsibilityContract;
			CurrentPlacement.ChildRequest.NegotiatedChildResponsibilityContracts = {
				CurrentPlacement.NegotiatedResponsibilityContract};
		}
		Placement.SelectionFeatures =
			BuildChildPlacementSelectionFeatures(
				Placement,
				Searcher.RootRequest);
		if (!BuildParentValidationRequest(
				Searcher.RootRequest,
				Searcher.ParentPlannedCells,
				Searcher.ParentPlannedCellSet,
				Searcher.ParentPlannedCellIntents,
				Searcher.ParentFootprintSize,
				Searcher.ParentProtectedTraversalCells,
				Searcher.ParentVerticalAccessIngressDirections,
				SchedulerState.CurrentPlacements,
				SchedulerState.CurrentPlannedPartitionSeams,
				false,
				SchedulerState.CurrentVerticalAccessOwnership,
				Result.FailureReason,
				SchedulerState.CurrentPreparedParentProofRequest,
				nullptr))
		{
			for (const FIntVector& AddedCell : AddedReservedCells)
			{
				SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
			}
			if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
			{
				SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
			}
			SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
			SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
			SchedulerState.bHasCurrentPreparedParentProofRequest =
				bHadPreviousPreparedParentProofRequest;
			SchedulerState.CurrentPreparedParentProofRequest =
				PreviousPreparedParentProofRequest;
			SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
			return Result;
		}
		{
			FLayoutRegionSolveRequest SeamProbeRequest =
				SchedulerState.CurrentPreparedParentProofRequest;
			TArray<FAutomaticChildPlacement> SeamProbePlacements =
				SchedulerState.CurrentPlacements;
			if (!ApplyOwnedSeamUsageRestrictions(
				Searcher.RootRequest.RegionDebugPath,
				SchedulerState.CurrentPlannedPartitionSeams,
				SeamProbeRequest,
				SeamProbePlacements,
				Result.FailureReason))
			{
				for (const FIntVector& AddedCell : AddedReservedCells)
				{
					SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
				}
				if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
				{
					SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
				}
				SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
				SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
				SchedulerState.bHasCurrentPreparedParentProofRequest =
					bHadPreviousPreparedParentProofRequest;
				SchedulerState.CurrentPreparedParentProofRequest =
					PreviousPreparedParentProofRequest;
				SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
				return Result;
			}
		}
		SchedulerState.bHasCurrentPreparedParentProofRequest = true;

		const FString NextSchedulerStateKey =
			BuildSchedulerPlacementStateKey(
				DemandIndex + 1,
				SchedulerState.CurrentPlacements,
				SchedulerState.CurrentPlannedPartitionSeams,
				SchedulerState.CurrentVerticalAccessOwnership);
		if (!PropagateRemainingPlacementDemandFeasibility(
			NextSchedulerStateKey,
			Searcher.RootRequest,
			Searcher,
			DemandIndex + 1,
			SchedulerState,
			Result.FailureReason))
		{
			for (const FIntVector& AddedCell : AddedReservedCells)
			{
				SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
			}
			if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
			{
				SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
			}
			SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
			SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
			SchedulerState.bHasCurrentPreparedParentProofRequest =
				bHadPreviousPreparedParentProofRequest;
			SchedulerState.CurrentPreparedParentProofRequest =
				PreviousPreparedParentProofRequest;
			SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);
			return Result;
		}

		Result.bFoundDownstreamSolution = Searcher.Search(
			DemandIndex + 1,
			CurrentScore + Candidate.Score,
			Result.FailureReason);

		for (const FIntVector& AddedCell : AddedReservedCells)
		{
			SchedulerState.CurrentReservedParentCells.Remove(AddedCell);
		}
		if (SchedulerState.LastChosenPlacementBySource.IsValidIndex(Demand.SourceIndex))
		{
			SchedulerState.LastChosenPlacementBySource[Demand.SourceIndex] = PreviousChoice;
		}
		SchedulerState.CurrentPlannedPartitionSeams = PreviousPlannedPartitionSeams;
		SchedulerState.CurrentVerticalAccessOwnership = PreviousVerticalAccessOwnership;
		SchedulerState.bHasCurrentPreparedParentProofRequest =
			bHadPreviousPreparedParentProofRequest;
		SchedulerState.CurrentPreparedParentProofRequest =
			PreviousPreparedParentProofRequest;
		SchedulerState.CurrentPlacements.RemoveAt(PlacementIndex);

		return Result;
	}

	TArray<FLayoutCommittedTraversalAnchor> BuildCommittedTraversalAnchorsForParentContacts(
		const TArray<FLayoutCommittedEndpointAnchor>& DirectParentContactCommitments,
		const TArray<FIntVector>& CandidateParentContactCells)
	{
		TArray<FLayoutCommittedTraversalAnchor> Result;
		const int32 SharedCount = FMath::Min(DirectParentContactCommitments.Num(), CandidateParentContactCells.Num());
		Result.Reserve(SharedCount);

		for (int32 Index = 0; Index < SharedCount; ++Index)
		{
			const FLayoutCommittedEndpointAnchor& Commitment = DirectParentContactCommitments[Index];
			FLayoutCommittedTraversalAnchor& Anchor = Result.AddDefaulted_GetRef();
			Anchor.Cell = CandidateParentContactCells[Index];

			TArray<FGameplayTag> SortedTraversalTags;
			Commitment.TraversalChannels.GetGameplayTagArray(SortedTraversalTags);
			SortedTraversalTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
			{
				return Left.ToString() < Right.ToString();
			});
			if (!SortedTraversalTags.IsEmpty())
			{
				Anchor.TraversalChannel = SortedTraversalTags[0];
			}
		}

		return Result;
	}

	FResolvedChildNoParentEntryOption ResolveChildEntryWithoutDirectParentContact(
		const FIntVector& CandidateEntryCell,
		const FLayoutChildCapabilityEndpoint& Capability,
		const FIntVector& CandidateOffset,
		const FLayoutChildStageMappingResult& StageMapping,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const FChildPlacementDemand& Demand,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements)
	{
		FResolvedChildNoParentEntryOption Result;
		const FIntVector SharedParentCell = ResolveMappedParentCell(
			StageMapping,
			CandidateEntryCell,
			CandidateOffset);
		for (const FAutomaticChildPlacement& ExistingPlacement : CurrentPlacements)
		{
			for (int32 ExistingCellIndex = 0;
				ExistingCellIndex < ExistingPlacement.ChildRequest.PlannedCells.Num();
				++ExistingCellIndex)
			{
				const FLayoutPlannedCell& ExistingChildPlannedCell =
					ExistingPlacement.ChildRequest.PlannedCells[ExistingCellIndex];
				const FIntVector ExistingParentCell =
					ExistingPlacement.ParentTranslatedPlannedCells.IsValidIndex(ExistingCellIndex)
						? ExistingPlacement.ParentTranslatedPlannedCells[ExistingCellIndex].Cell
						: ExistingChildPlannedCell.Cell + ExistingPlacement.RegionCellOffset;
				if (ExistingParentCell != SharedParentCell)
				{
					continue;
				}
				if (!IsCellOnFootprintPerimeter(
					ExistingChildPlannedCell.Cell,
					ExistingPlacement.ChildRequest.FootprintSize))
				{
					continue;
				}
				ELayoutFaceDirection CandidateOverlapDirection = ELayoutFaceDirection::PosX;
				ELayoutFaceDirection ExistingOverlapDirection = ELayoutFaceDirection::PosX;
				if (!TryGetSharedOverlapFaceDirections(
					CandidateEntryCell,
					PlanningVariant.FootprintSize,
					ExistingChildPlannedCell.Cell,
					ExistingPlacement.ChildRequest.FootprintSize,
					CandidateOverlapDirection,
					ExistingOverlapDirection))
				{
					continue;
				}
				if (CandidateOverlapDirection != Capability.FaceDirection)
				{
					continue;
				}
				if (!DoChildSeamCapabilitiesSupportSpecificSharedInterface(
					PlanningVariant.CapabilityEnvelope,
					CandidateOverlapDirection,
					ExistingPlacement.ChildCapabilityEnvelope,
					ExistingOverlapDirection,
					LayoutGameplayTags::InterfacePartitionDoor))
				{
					continue;
				}

				FString DoorSeamOwnerRegionPath;
				FString DoorSeamPassiveRegionPath;
				if (!TryChooseSpecificSharedInterfaceOwner(
					Demand.ChildRegionDebugPath,
					PlanningVariant.CapabilityEnvelope,
					CandidateOverlapDirection,
					ExistingPlacement.ChildRegionDebugPath,
					ExistingPlacement.ChildCapabilityEnvelope,
					ExistingOverlapDirection,
					LayoutGameplayTags::InterfacePartitionDoor,
					DoorSeamOwnerRegionPath,
					DoorSeamPassiveRegionPath))
				{
					continue;
				}

				if (DoorSeamOwnerRegionPath != Demand.ChildRegionDebugPath)
				{
					continue;
				}

				Result.ResolutionMode = FResolvedChildEntryCommitmentCandidate::EResolutionMode::SiblingSharedDoor;
				Result.FaceDirection = CandidateOverlapDirection;
				return Result;
			}
		}

		// A parent-owned shared door seam remains an optional seam capability.
		// It must not satisfy a required child entry commitment by itself, or the scheduler can
		// incorrectly force shell-door anchors onto the parent perimeter.

		return Result;
	}

	/** Matches one mapped child value against its counted parent-zone slot. */
	bool DoesMappedChildPlacementMatchRequiredZone(
		const FChildPlacementDemand& Demand,
		const FIntPoint& ParentFootprintSize,
		const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& Candidate)
	{
		const bool bRequiresExteriorZone =
			Demand.RequiredPlacementZone == ELayoutPlacementZone::Perimeter
			|| Demand.RequiredPlacementZone == ELayoutPlacementZone::Edge
			|| Demand.RequiredPlacementZone == ELayoutPlacementZone::Corner;
		if (Candidate.bDisplacesRelocatableParentEntry && !bRequiresExteriorZone)
		{
			return false;
		}

		TSet<FIntVector> CertifiedSharedParentCells;
		const TSet<FIntVector>* ExemptTranslatedCells = nullptr;
		if (Demand.RequiredPlacementZone == ELayoutPlacementZone::Interior
			&& !Candidate.SharedParentChildFaces.IsEmpty())
		{
			for (const FSharedParentChildFace& SharedFace : Candidate.SharedParentChildFaces)
			{
				CertifiedSharedParentCells.Add(SharedFace.ParentCell);
			}
			ExemptTranslatedCells = &CertifiedSharedParentCells;
		}
		if (!DoesTranslatedChildPlanMatchPlacementZone(
			Candidate.StageMapping.ParentTranslatedPlannedCells,
			FIntVector::ZeroValue,
			ParentFootprintSize,
			Demand.RequiredPlacementZone,
			ExemptTranslatedCells,
			nullptr))
		{
			return false;
		}
		if (Demand.RequiredPlacementZoneComponentIndex == INDEX_NONE)
		{
			return true;
		}

		return Candidate.StageMapping.ParentTranslatedPlannedCells.ContainsByPredicate(
			[&](const FLayoutPlannedCell& PlannedCell)
			{
				const int32 MaxX = ParentFootprintSize.X - 1;
				const int32 MaxY = ParentFootprintSize.Y - 1;
				if (Demand.RequiredPlacementZone == ELayoutPlacementZone::Corner)
				{
					switch (Demand.RequiredPlacementZoneComponentIndex)
					{
					case 0: return PlannedCell.Cell.X == 0 && PlannedCell.Cell.Y == 0;
					case 1: return PlannedCell.Cell.X == MaxX && PlannedCell.Cell.Y == 0;
					case 2: return PlannedCell.Cell.X == MaxX && PlannedCell.Cell.Y == MaxY;
					case 3: return PlannedCell.Cell.X == 0 && PlannedCell.Cell.Y == MaxY;
					default: return false;
					}
				}
				if (Demand.RequiredPlacementZone == ELayoutPlacementZone::Edge)
				{
					switch (Demand.RequiredPlacementZoneComponentIndex)
					{
					case 0: return PlannedCell.Cell.Y == 0 && PlannedCell.Cell.X > 0 && PlannedCell.Cell.X < MaxX;
					case 1: return PlannedCell.Cell.X == MaxX && PlannedCell.Cell.Y > 0 && PlannedCell.Cell.Y < MaxY;
					case 2: return PlannedCell.Cell.Y == MaxY && PlannedCell.Cell.X > 0 && PlannedCell.Cell.X < MaxX;
					case 3: return PlannedCell.Cell.X == 0 && PlannedCell.Cell.Y > 0 && PlannedCell.Cell.Y < MaxY;
					default: return false;
					}
				}
				return false;
			});
	}

	bool CanRequiredDemandStillFitPartialState(
		const FString& SchedulerStateKey,
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FChildPlacementDemand>& PlacementDemands,
		const int32 DemandIndex,
		const FChildPlacementDemand& Demand,
		const FPreparedChildPlacementSource& PreparedSource,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TSet<FIntVector>& CurrentReservedParentCells,
		TMap<FString, FEntryCommitmentOptionMemo>& InOutEntryCommitmentOptionMemo,
		FString& OutFailureReason)
	{
		int32 CandidateOffsetsExamined = 0;
		int32 ParentPlanRejectCount = 0;
		int32 EntryNegotiationRejectCount = 0;
		FString BestRejectedCandidateReason;

		bool bHasRemainingPeerDemand = false;
		for (int32 RemainingDemandIndex = DemandIndex + 1; RemainingDemandIndex < PlacementDemands.Num(); ++RemainingDemandIndex)
		{
			if (PlacementDemands[RemainingDemandIndex].SourceIndex == Demand.SourceIndex)
			{
				bHasRemainingPeerDemand = true;
				break;
			}
		}

		for (int32 VariantIndex = 0; VariantIndex < PreparedSource.PlanningVariants.Num(); ++VariantIndex)
		{
			const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant =
				PreparedSource.PlanningVariants[VariantIndex];
			for (const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate : PlanningVariant.NormalizedCandidates)
			{
				++CandidateOffsetsExamined;
				if (!DoesMappedChildPlacementMatchRequiredZone(
					Demand,
					RootRequest.FootprintSize,
					NormalizedCandidate))
				{
					++ParentPlanRejectCount;
					continue;
				}
				const FIntVector& CandidateOffset = NormalizedCandidate.Offset;
				const TArray<FIntVector>& CandidateParentPlanReservedCells = NormalizedCandidate.ParentPlanReservedCells;
				const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces = NormalizedCandidate.SharedParentChildFaces;

				bool bFitsParentPlan = true;
				for (const FLayoutPlannedCell& ChildPlannedCell : PlanningVariant.PlannedCells)
				{
					const FIntVector ParentCell = ResolveMappedParentCell(
						NormalizedCandidate.StageMapping,
						ChildPlannedCell.Cell,
						CandidateOffset);
					if (CurrentReservedParentCells.Contains(ParentCell)
						&& !CanChildReservationShareSiblingSeamCell(
							ChildPlannedCell,
							CandidateOffset,
							PlanningVariant.FootprintSize,
							PlanningVariant.CapabilityEnvelope,
							CurrentPlacements,
							nullptr,
							&ParentCell))
					{
						bFitsParentPlan = false;
						break;
					}
				}
				if (!bFitsParentPlan)
				{
					++ParentPlanRejectCount;
					if (BestRejectedCandidateReason.IsEmpty())
					{
						BestRejectedCandidateReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Explicit Propagation\nRejection Detail:\n  The candidate footprint collides with already committed child reservations without a valid sibling shared seam."),
							*CandidateOffset.ToString());
					}
					continue;
				}

				if (bHasRemainingPeerDemand
					&& PreparedSource.EntrySnapshot->ChildPlacementZone == ELayoutPlacementZone::Interior
					&& CandidateSharedParentChildFaces.IsEmpty())
				{
					++ParentPlanRejectCount;
					if (BestRejectedCandidateReason.IsEmpty())
					{
						BestRejectedCandidateReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Explicit Propagation\nRejection Detail:\n  Additional required copies of this interior child still remain, and this candidate uses no parent-child shared wall seam to preserve space for them."),
							*CandidateOffset.ToString());
					}
					continue;
				}

				if (bHasRemainingPeerDemand
					&& !CanAnyRemainingPeerFitStructurally(
						PreparedSource,
						PlanningVariant,
						CurrentPlacements,
						CurrentReservedParentCells,
						ParentPlannedCellSet,
						ParentPlannedCellIntents,
						ParentProtectedTraversalCells,
						CandidateParentPlanReservedCells,
						CandidateOffset,
						NormalizedCandidate.StageMapping,
						Demand))
				{
					++ParentPlanRejectCount;
					if (BestRejectedCandidateReason.IsEmpty())
					{
						BestRejectedCandidateReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Explicit Propagation\nRejection Detail:\n  Accepting this candidate would strand the remaining required copy of '%s'."),
							*CandidateOffset.ToString(),
							*PreparedSource.EntrySnapshot->EntryId.ToString());
					}
					continue;
				}

				const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)> CanResolveEntryWithoutDirectParentContact =
					[&](
						const FIntVector& CandidateEntryCell,
						const FLayoutChildCapabilityEndpoint& Capability) -> FResolvedChildNoParentEntryOption
					{
						return ResolveChildEntryWithoutDirectParentContact(
							CandidateEntryCell,
							Capability,
							CandidateOffset,
							NormalizedCandidate.StageMapping,
							PlanningVariant,
							Demand,
							CurrentPlacements);
					};

				const TArray<TArray<FResolvedChildEntryCommitmentCandidate>>* CandidateOptionsByEntry = nullptr;
				FString CommitmentFailureReason;
				const FString EntryOptionMemoKey = BuildChildEntryCommitmentOptionMemoKey(
					SchedulerStateKey,
					PreparedSource.EntrySnapshot->EntryId,
					VariantIndex,
					CandidateOffset);
				if (!CollectChildEntryCommitmentOptionsForOffsetCached(
					EntryOptionMemoKey,
					*PreparedSource.EntrySnapshot,
					PlanningVariant.InterfaceSummary,
					NormalizedCandidate,
					ParentPlannedCellSet,
					CurrentReservedParentCells,
					ParentProtectedTraversalCells,
					ParentPriorityTargets,
					CanResolveEntryWithoutDirectParentContact,
					InOutEntryCommitmentOptionMemo,
					CandidateOptionsByEntry,
					&CommitmentFailureReason))
				{
					++EntryNegotiationRejectCount;
					if (BestRejectedCandidateReason.IsEmpty() && !CommitmentFailureReason.IsEmpty())
					{
						BestRejectedCandidateReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Explicit Propagation\nRejection Detail:\n%s"),
							*CandidateOffset.ToString(),
							*IndentMultiline(CommitmentFailureReason, TEXT("  ")));
					}
					continue;
				}

				const TFunction<bool(
					const TArray<FLayoutCommittedEndpointAnchor>&,
					const TArray<FIntVector>&,
					const TArray<FResolvedChildEntryCommitmentCandidate>&,
					int32&,
					TArray<FLayoutSolveBoundaryPoint>&)> EvaluateCommitmentCombination =
					[&](
						const TArray<FLayoutCommittedEndpointAnchor>&,
						const TArray<FIntVector>& ResolvedParentContactCells,
						const TArray<FResolvedChildEntryCommitmentCandidate>&,
						int32& OutScoreAdjustment,
						TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints) -> bool
					{
						if (bHasRemainingPeerDemand
							&& !ResolvedParentContactCells.IsEmpty()
							&& !ResolvedParentContactCells.ContainsByPredicate(
								[&](const FIntVector& ParentContactCell)
								{
									return !ParentProtectedTraversalCells.Contains(ParentContactCell);
								}))
						{
							return false;
						}

						OutScoreAdjustment = 0;
						OutBoundaryPoints.Reset();
						return true;
					};
				if (CandidateOptionsByEntry == nullptr
					|| !CanResolveAnyChildEntryCommitmentOptions(
						*CandidateOptionsByEntry,
					EvaluateCommitmentCombination))
				{
					++EntryNegotiationRejectCount;
					if (CommitmentFailureReason.IsEmpty())
					{
						CommitmentFailureReason = FString::Printf(
							TEXT("Child entry commitments for '%s' could not be resolved together.\nProblem: Individual parent contacts existed, but no non-conflicting set of contacts survived explicit propagation validation.\nFix: Move or rotate the child, adjust the parent circulation, or relax competing child contact demands."),
							*PreparedSource.EntrySnapshot->EntryId.ToString());
					}
					if (BestRejectedCandidateReason.IsEmpty() && !CommitmentFailureReason.IsEmpty())
					{
						BestRejectedCandidateReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Explicit Propagation\nRejection Detail:\n%s"),
							*CandidateOffset.ToString(),
							*IndentMultiline(CommitmentFailureReason, TEXT("  ")));
					}
					continue;
				}

				OutFailureReason.Reset();
				return true;
			}
		}

		OutFailureReason = FString::Printf(
			TEXT("Explicit propagation rejected the partial recursive schedule because a remaining required child no longer has any entry-feasible translated placement.\n%s\nProblem: After the current child commitments were accepted, every remaining candidate for this required child either collided with committed reservations or lost all admissible entry/interface resolutions before deeper parent probing.\nCandidate Summary: examined=%d parentPlanRejected=%d entryNegotiationRejected=%d\n%s\nFix: Preserve at least one translated footprint with viable parent/sibling entry commitments for each remaining required child before continuing deeper recursive search."),
			*DescribeChildPlacementContext(RootRequest, *PreparedSource.EntrySnapshot),
			CandidateOffsetsExamined,
			ParentPlanRejectCount,
			EntryNegotiationRejectCount,
			BestRejectedCandidateReason.IsEmpty()
				? TEXT("Best Rejected Placement:\n  <none recorded>")
				: *BestRejectedCandidateReason);
		return false;
	}

	bool PropagateRemainingPlacementDemandFeasibility(
		const FString& SchedulerStateKey,
		const FLayoutRegionSolveRequest& RootRequest,
		const FRecursivePlacementDemandSearcher& Searcher,
		const int32 NextDemandIndex,
		FRecursiveChildPlacementSchedulerState& InOutSchedulerState,
		FString& OutFailureReason)
	{
		if (NextDemandIndex >= Searcher.PlacementDemands.Num())
		{
			OutFailureReason.Reset();
			return true;
		}

		if (const FRemainingDemandPropagationMemo* ExistingMemo =
			InOutSchedulerState.RemainingDemandPropagationMemo.Find(SchedulerStateKey))
		{
			OutFailureReason = ExistingMemo->FailureReason;
			return ExistingMemo->bSucceeded;
		}

		for (int32 RemainingDemandIndex = NextDemandIndex;
			RemainingDemandIndex < Searcher.PlacementDemands.Num();
			++RemainingDemandIndex)
		{
			const FChildPlacementDemand& RemainingDemand = Searcher.PlacementDemands[RemainingDemandIndex];
			if (RemainingDemand.bOptional)
			{
				continue;
			}
			if (!Searcher.PreparedSources.IsValidIndex(RemainingDemand.SourceIndex))
			{
				OutFailureReason = TEXT("Explicit propagation could not inspect a remaining required child because its prepared scheduling source was missing.");
				FRemainingDemandPropagationMemo& FailedMemo =
					InOutSchedulerState.RemainingDemandPropagationMemo.Add(SchedulerStateKey);
				FailedMemo.bSucceeded = false;
				FailedMemo.FailureReason = OutFailureReason;
				return false;
			}

			const FPreparedChildPlacementSource& RemainingSource =
				Searcher.PreparedSources[RemainingDemand.SourceIndex];
			if (!RemainingSource.bPreparationSucceeded)
			{
				OutFailureReason = RemainingSource.PreparationFailureReason.IsEmpty()
					? TEXT("Explicit propagation rejected the partial recursive schedule because a remaining required child has no prepared topology or boundary domain.")
					: RemainingSource.PreparationFailureReason;
				FRemainingDemandPropagationMemo& FailedMemo =
					InOutSchedulerState.RemainingDemandPropagationMemo.Add(SchedulerStateKey);
				FailedMemo.bSucceeded = false;
				FailedMemo.FailureReason = OutFailureReason;
				return false;
			}

			if (!CanRequiredDemandStillFitPartialState(
				SchedulerStateKey,
				RootRequest,
				Searcher.PlacementDemands,
				RemainingDemandIndex,
				RemainingDemand,
				RemainingSource,
				Searcher.ParentPlannedCellSet,
				Searcher.ParentPlannedCellIntents,
				Searcher.ParentProtectedTraversalCells,
				Searcher.ParentPriorityTargets,
				InOutSchedulerState.CurrentPlacements,
				InOutSchedulerState.CurrentReservedParentCells,
				InOutSchedulerState.EntryCommitmentOptionMemo,
				OutFailureReason))
			{
				// This propagation pass is an advisory pruning check. It does not run the
				// full sibling/child-entry search and can false-negative valid authored
				// castle layouts where a remaining child attaches through a later seam or
				// alternate commitment family. Keep the complete recursive search as the
				// authority instead of rejecting the partial schedule here.
				OutFailureReason.Reset();
				continue;
			}
		}

		FRemainingDemandPropagationMemo& SuccessMemo =
			InOutSchedulerState.RemainingDemandPropagationMemo.Add(SchedulerStateKey);
		SuccessMemo.bSucceeded = true;
		SuccessMemo.FailureReason.Reset();
		OutFailureReason.Reset();
		return true;
	}

	void RecordFailedChildEntryNegotiationForCandidate(
		const FIntVector& CandidateOffset,
		const FString& CommitmentFailureReason,
		FChildPlacementCandidateBuildResult& OutResult)
	{
		++OutResult.EntryNegotiationRejectCount;
		if (!OutResult.BestRejectedCandidateReason.IsEmpty()
			&& OutResult.RejectedCandidateReasons.Num() >= 12)
		{
			return;
		}

		const FString RejectedMessage = CommitmentFailureReason.IsEmpty()
			? FString()
			: FString::Printf(
				TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Child Entry Negotiation\nRejection Detail:\n%s"),
				*CandidateOffset.ToString(),
				*IndentMultiline(CommitmentFailureReason, TEXT("  ")));
		if (OutResult.BestRejectedCandidateReason.IsEmpty() && !RejectedMessage.IsEmpty())
		{
			OutResult.BestRejectedCandidateReason = RejectedMessage;
		}
		AddUniqueRejectedCandidateReason(
			RejectedMessage,
			OutResult.RejectedCandidateReasons);
	}

	void BuildPlacementBridgeParentProbeCandidate(
		const FString& SchedulerStateKey,
		const int32 VariantIndex,
		const FString& ChildRegionDebugPath,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const bool bChildContributesHostVerticalAccess,
		const FIntVector& CandidateOffset,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutCommittedEndpointAnchor>& DirectParentContactCommitments,
		const TArray<FIntVector>& CandidateParentContactCells,
		const TArray<FLayoutCommittedTraversalAnchor>& CandidateParentTraversalAnchors,
		const bool bAllowsChildTraversalBridgeForCommittedContacts,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces,
		const FLayoutId CandidateBoundaryCertificateId,
		const TArray<FLayoutCellCandidateDomainRestriction>& CandidateParentDomainRestrictions,
		const bool bDeferChildTraversalValidationToSchedule,
		const bool bRequireStructuralFeasibilityPrecheck,
		const bool bRunImmediateFullParentProof,
		const bool bDeferFullParentProofToCompleteValidation,
		FPlacementBridgeParentProbeCandidate& OutCandidate)
	{
		OutCandidate = FPlacementBridgeParentProbeCandidate();
		OutCandidate.SchedulerStateKey = SchedulerStateKey;
		OutCandidate.VariantIndex = VariantIndex;
		OutCandidate.PlanningVariant.ChildRegionDebugPath = ChildRegionDebugPath;
		OutCandidate.PlanningVariant.FootprintSize = PlanningVariant.FootprintSize;
		OutCandidate.PlanningVariant.MaxLocalLevel = PlanningVariant.MaxLocalLevel;
		OutCandidate.PlanningVariant.PlannedCells = PlanningVariant.PlannedCells;
		OutCandidate.PlanningVariant.StructuralSupportLocalCells =
			PlanningVariant.StructuralSupportLocalCells;
		OutCandidate.PlanningVariant.InterfaceSummary.FootprintCells = PlanningVariant.InterfaceSummary.FootprintCells;
		OutCandidate.PlanningVariant.InterfaceSummary.EntryCells = PlanningVariant.InterfaceSummary.EntryCells;
		OutCandidate.PlanningVariant.InterfaceSummary.EntryCapabilities = PlanningVariant.InterfaceSummary.EntryCapabilities;
		OutCandidate.PlanningVariant.InterfaceSummary.ParentShareableEntryOffers = PlanningVariant.InterfaceSummary.ParentShareableEntryOffers;
		OutCandidate.PlanningVariant.InterfaceSummary.BoundarySpanCapabilities = PlanningVariant.InterfaceSummary.BoundarySpanCapabilities;
		OutCandidate.PlanningVariant.InterfaceSummary.ParentShareableSpanOffers = PlanningVariant.InterfaceSummary.ParentShareableSpanOffers;
		OutCandidate.PlanningVariant.InterfaceSummary.SeamCapabilities = PlanningVariant.InterfaceSummary.SeamCapabilities;
		OutCandidate.PlanningVariant.InterfaceSummary.ParentShareableSeamOffers = PlanningVariant.InterfaceSummary.ParentShareableSeamOffers;
		OutCandidate.PlanningVariant.InterfaceSummary.SiblingShareableSeamOffers = PlanningVariant.InterfaceSummary.SiblingShareableSeamOffers;
		OutCandidate.PlanningVariant.InterfaceSummary.bHasExplicitEntryCapabilities = PlanningVariant.InterfaceSummary.bHasExplicitEntryCapabilities;
		OutCandidate.PlanningVariant.InterfaceSummary.bSupportsVerticalAccess = PlanningVariant.InterfaceSummary.bSupportsVerticalAccess;
		OutCandidate.PlanningVariant.InterfaceSummary.VerticalAccessCells = PlanningVariant.InterfaceSummary.VerticalAccessCells;
		OutCandidate.PlanningVariant.InterfaceSummary.BoundaryFaces.Reserve(PlanningVariant.InterfaceSummary.BoundaryFaces.Num());
		for (const FChildPlanningVariantInterfaceSummary::FBoundaryFace& BoundaryFace : PlanningVariant.InterfaceSummary.BoundaryFaces)
		{
			FPlacementBridgeChildInterfaceSummary::FBoundaryFace& NewBoundaryFace =
				OutCandidate.PlanningVariant.InterfaceSummary.BoundaryFaces.AddDefaulted_GetRef();
			NewBoundaryFace.LocalCell = BoundaryFace.LocalCell;
			NewBoundaryFace.FaceDirection = BoundaryFace.FaceDirection;
			NewBoundaryFace.Intent = BoundaryFace.Intent;
			NewBoundaryFace.ConnectionTag = BoundaryFace.ConnectionTag;
			NewBoundaryFace.AllowedConnectionTags =
				BoundaryFace.AllowedConnectionTags;
			NewBoundaryFace.TraversalChannels =
				BoundaryFace.TraversalChannels;
		}
		OutCandidate.PlanningVariant.bChildContributesHostVerticalAccess = bChildContributesHostVerticalAccess;
		OutCandidate.CandidateOffset = CandidateOffset;
		OutCandidate.CandidateCommitments = CandidateCommitments;
		OutCandidate.DirectParentContactCommitments = DirectParentContactCommitments;
		OutCandidate.CandidateParentContactCells = CandidateParentContactCells;
		OutCandidate.CandidateParentTraversalAnchors = CandidateParentTraversalAnchors;
		OutCandidate.bAllowsChildTraversalBridgeForCommittedContacts = bAllowsChildTraversalBridgeForCommittedContacts;
		OutCandidate.CandidateParentPlanReservedCells = CandidateParentPlanReservedCells;
		OutCandidate.CandidateSharedParentChildFaces = CandidateSharedParentChildFaces;
		OutCandidate.CandidateBoundaryCertificateId = CandidateBoundaryCertificateId;
		OutCandidate.CandidateParentDomainRestrictions = CandidateParentDomainRestrictions;
		OutCandidate.bDeferChildTraversalValidationToSchedule = bDeferChildTraversalValidationToSchedule;
		OutCandidate.bRequireStructuralFeasibilityPrecheck = bRequireStructuralFeasibilityPrecheck;
		OutCandidate.bRunImmediateFullParentProof = bRunImmediateFullParentProof;
		OutCandidate.bDeferFullParentProofToCompleteValidation = bDeferFullParentProofToCompleteValidation;
	}

	bool EvaluatePreparedParentProbeForChildCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections,
		const FString& SchedulerStateKey,
		const int32 VariantIndex,
		const FString& ChildRegionDebugPath,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const bool bChildContributesHostVerticalAccess,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TSet<FIntVector>& CurrentReservedParentCells,
		TMap<FString, FParentStructuralFeasibilityMemo>& InOutParentStructuralFeasibilityMemo,
		TMap<FString, FParentProbeEvaluationMemo>& InOutParentProbeEvaluationMemo,
		TMap<FString, FParentProbeEvaluationMemo>* SharedParentProbeSolveMemo,
		FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
		const FIntVector& CandidateOffset,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutCommittedEndpointAnchor>& DirectParentContactCommitments,
		const TArray<FIntVector>& CandidateParentContactCells,
		const TArray<FLayoutCommittedTraversalAnchor>& CandidateParentTraversalAnchors,
		const bool bAllowsChildTraversalBridgeForCommittedContacts,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces,
		const FLayoutId CandidateBoundaryCertificateId,
		const TArray<FLayoutCellCandidateDomainRestriction>& CandidateParentDomainRestrictions,
		const bool bDeferChildTraversalValidationToSchedule,
		const bool bRequireStructuralFeasibilityPrecheck,
		const bool bRunImmediateFullParentProof,
		const bool bDeferFullParentProofToCompleteValidation,
		FPreparedChildParentProofCandidate& InOutPreparedParentProofCandidate,
		FParentProbeEvaluation& OutEvaluation,
		const FPreparedParentProbeResidualPlan* PreparedResidualPlan = nullptr)
	{
		PORISM_LAYOUT_PROBE_SCOPE(Layout_Probe_Parent, STAT_PorismLayout_ParentProbe);
		INC_DWORD_STAT(STAT_PorismLayout_ParentProbeCalls);
		const double ParentProbeStartSeconds = FPlatformTime::Seconds();
		++InOutPerfMetrics.ParentProbeCalls;
		TRACE_BOOKMARK(TEXT("Layout_ProbeBegin region=%s child=%s seed=%d probe=%d state=%08x variant=%d cell=(%d,%d,%d) contacts=%d prepared=%d residualReused=%d structural=%d fullProof=%d deferred=%d schedulerElapsedMs=%.3f schedulerBudgetMs=%.3f"),
			*RootRequest.RegionDebugPath.Left(128), *ChildRegionDebugPath.Left(128), RootRequest.Seed, InOutPerfMetrics.ParentProbeCalls,
			GetTypeHash(SchedulerStateKey), VariantIndex, CandidateOffset.X, CandidateOffset.Y, CandidateOffset.Z,
			CandidateParentContactCells.Num(), !InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.EvaluationMemoKey.IsEmpty(),
			PreparedResidualPlan != nullptr, bRequireStructuralFeasibilityPrecheck, bRunImmediateFullParentProof,
			bDeferFullParentProofToCompleteValidation, (ParentProbeStartSeconds - InOutPerfMetrics.StartSeconds) * 1000.0,
			RootRequest.ExecutionSettings.MaxSolveDurationSeconds * 1000.0);

		FPlacementBridgeSolveContext SolveContext;
		SolveContext.RootRequest = &RootRequest;
		SolveContext.ParentPlannedCells = &ParentPlannedCells;
		SolveContext.ParentFootprintSize = ParentFootprintSize;
		SolveContext.ParentPlannedCellSet = &ParentPlannedCellSet;
		SolveContext.ParentPlannedCellIntents = &ParentPlannedCellIntents;
		SolveContext.ParentProtectedTraversalCells = &ParentProtectedTraversalCells;
		SolveContext.ParentVerticalAccessIngressDirections = &ParentVerticalAccessIngressDirections;

		FPlacementBridgeMutableStateView MutableState;
		MutableState.CurrentPlacements = &CurrentPlacements;
		MutableState.CurrentReservedParentCells = &CurrentReservedParentCells;

		if (InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.EvaluationMemoKey.IsEmpty())
		{
			FPlacementBridgeParentProbeCandidate Candidate;
			BuildPlacementBridgeParentProbeCandidate(
				SchedulerStateKey,
				VariantIndex,
				ChildRegionDebugPath,
				PlanningVariant,
				bChildContributesHostVerticalAccess,
				CandidateOffset,
				CandidateCommitments,
				DirectParentContactCommitments,
				CandidateParentContactCells,
				CandidateParentTraversalAnchors,
				bAllowsChildTraversalBridgeForCommittedContacts,
				CandidateParentPlanReservedCells,
				CandidateSharedParentChildFaces,
				CandidateBoundaryCertificateId,
				CandidateParentDomainRestrictions,
				bDeferChildTraversalValidationToSchedule,
				bRequireStructuralFeasibilityPrecheck,
				bRunImmediateFullParentProof,
				bDeferFullParentProofToCompleteValidation,
				Candidate);
			if (!TryPrepareParentProbeCandidate(
				SolveContext,
				MutableState,
				Candidate,
				InOutPreparedParentProofCandidate.PreparedParentProbeCandidate,
				OutEvaluation,
				PreparedResidualPlan))
			{
				InOutPerfMetrics.ParentProbeSeconds +=
					(FPlatformTime::Seconds() - ParentProbeStartSeconds);
				TRACE_BOOKMARK(TEXT("Layout_ProbeEnd region=%s probe=%d ok=0 stage=PrepareCandidate elapsedMs=%.3f failure=%s"),
					*RootRequest.RegionDebugPath.Left(128), InOutPerfMetrics.ParentProbeCalls,
					(FPlatformTime::Seconds() - ParentProbeStartSeconds) * 1000.0, *OutEvaluation.FailureReason.Left(256));
				return false;
			}
		}

		InOutPreparedParentProofCandidate.DirectParentContactCommitments = DirectParentContactCommitments;
		InOutPreparedParentProofCandidate.EffectiveSharedParentChildFaces = CandidateSharedParentChildFaces;
		InOutPreparedParentProofCandidate.bAllowsChildTraversalBridgeForCommittedContacts =
			bAllowsChildTraversalBridgeForCommittedContacts;

		InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.Candidate.bRunImmediateFullParentProof =
			bRunImmediateFullParentProof;
		InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.Candidate.bRequireStructuralFeasibilityPrecheck =
			bRequireStructuralFeasibilityPrecheck;
		InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.Candidate.bDeferFullParentProofToCompleteValidation =
			bDeferFullParentProofToCompleteValidation;
		InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.EvaluationMemoKey =
			BuildParentProbeEvaluationMemoKey(
				InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.Candidate);

		const bool bSucceeded = EvaluatePreparedParentProbeCandidate(
			SolveContext,
			MutableState,
			InOutPreparedParentProofCandidate.PreparedParentProbeCandidate,
			InOutParentStructuralFeasibilityMemo,
			InOutParentProbeEvaluationMemo,
			SharedParentProbeSolveMemo,
			OutEvaluation);

		if (OutEvaluation.bUsedLocalMemo || OutEvaluation.bUsedSharedSolveMemo)
		{
			++InOutPerfMetrics.ParentProbeMemoHits;
		}
		if (!OutEvaluation.bUsedLocalMemo && !OutEvaluation.bUsedSharedSolveMemo)
		{
			InOutPerfMetrics.ParentStructuralPrecheckSeconds +=
				OutEvaluation.StructuralFeasibilitySeconds;
		}
		if (OutEvaluation.bFailedStructuralFeasibility)
		{
			++InOutPerfMetrics.ParentProbeStructuralRejects;
			if (ShouldTrackDetailedRecursiveSchedulerPerf())
			{
				RecordRecursiveSchedulerStructuralRejectFailure(
					InOutPerfMetrics,
					OutEvaluation.FailureReason);
			}
		}
		if (OutEvaluation.bExecutedImmediateFullParentProofSolve)
		{
			++InOutPerfMetrics.ParentProbeFullSolveCalls;
		}
		if (OutEvaluation.bRanImmediateFullParentProof)
		{
			InOutPerfMetrics.ParentProbeSolveSeconds += OutEvaluation.ParentProofSolveSeconds;
		}
		if (ShouldTrackDetailedRecursiveSchedulerPerf())
		{
			RecordRecursiveSchedulerFullProofFailure(InOutPerfMetrics, OutEvaluation);
		}
		if (bSucceeded)
		{
			TSet<FIntVector> FinalParentPlannedCellSet;
			const TArray<FLayoutPlannedCell>& FinalParentPlannedCells =
				InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.PreparedProofRequest.PlannedCells.IsEmpty()
					? ParentPlannedCells
					: InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.PreparedProofRequest.PlannedCells;
			for (const FLayoutPlannedCell& PlannedCell : FinalParentPlannedCells)
			{
				FinalParentPlannedCellSet.Add(PlannedCell.Cell);
			}
			RefreshCertifiedSharedParentChildFaceRunLengths(
				InOutPreparedParentProofCandidate.EffectiveSharedParentChildFaces,
				&FinalParentPlannedCellSet);
		}

		InOutPerfMetrics.ParentProbeSeconds += (FPlatformTime::Seconds() - ParentProbeStartSeconds);
		TRACE_BOOKMARK(TEXT("Layout_ProbeEnd region=%s probe=%d ok=%d key=%08x localMemo=%d sharedMemo=%d structuralReject=%d fullSolve=%d elapsedMs=%.3f failure=%s"),
			*RootRequest.RegionDebugPath.Left(128), InOutPerfMetrics.ParentProbeCalls, bSucceeded,
			GetTypeHash(InOutPreparedParentProofCandidate.PreparedParentProbeCandidate.EvaluationMemoKey),
			OutEvaluation.bUsedLocalMemo, OutEvaluation.bUsedSharedSolveMemo, OutEvaluation.bFailedStructuralFeasibility,
			OutEvaluation.bExecutedImmediateFullParentProofSolve, (FPlatformTime::Seconds() - ParentProbeStartSeconds) * 1000.0,
			*OutEvaluation.FailureReason.Left(256));
		return bSucceeded;
	}

	bool EvaluateParentProbeForChildCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections,
		const FString& SchedulerStateKey,
		const int32 VariantIndex,
		const FString& ChildRegionDebugPath,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const bool bChildContributesHostVerticalAccess,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TSet<FIntVector>& CurrentReservedParentCells,
		TMap<FString, FParentStructuralFeasibilityMemo>& InOutParentStructuralFeasibilityMemo,
		TMap<FString, FParentProbeEvaluationMemo>& InOutParentProbeEvaluationMemo,
		TMap<FString, FParentProbeEvaluationMemo>* SharedParentProbeSolveMemo,
		FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
		const FIntVector& CandidateOffset,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutCommittedEndpointAnchor>& DirectParentContactCommitments,
		const TArray<FIntVector>& CandidateParentContactCells,
		const TArray<FLayoutCommittedTraversalAnchor>& CandidateParentTraversalAnchors,
		const bool bAllowsChildTraversalBridgeForCommittedContacts,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces,
		const FLayoutId CandidateBoundaryCertificateId,
		const TArray<FLayoutCellCandidateDomainRestriction>& CandidateParentDomainRestrictions,
		const bool bDeferChildTraversalValidationToSchedule,
		const bool bRequireStructuralFeasibilityPrecheck,
		const bool bRunImmediateFullParentProof,
		const bool bDeferFullParentProofToCompleteValidation,
		FParentProbeEvaluation& OutEvaluation,
		FPreparedChildParentProofCandidate* OutPreparedParentProofCandidate = nullptr,
		const FPreparedParentProbeResidualPlan* PreparedResidualPlan = nullptr)
	{
		FPreparedChildParentProofCandidate PreparedParentProofCandidate;
		const bool bSucceeded = EvaluatePreparedParentProbeForChildCandidate(
			RootRequest,
			ParentPlannedCells,
			ParentFootprintSize,
			ParentPlannedCellSet,
			ParentPlannedCellIntents,
			ParentProtectedTraversalCells,
			ParentVerticalAccessIngressDirections,
			SchedulerStateKey,
			VariantIndex,
			ChildRegionDebugPath,
			PlanningVariant,
			bChildContributesHostVerticalAccess,
			CurrentPlacements,
			CurrentReservedParentCells,
			InOutParentStructuralFeasibilityMemo,
			InOutParentProbeEvaluationMemo,
			SharedParentProbeSolveMemo,
			InOutPerfMetrics,
			CandidateOffset,
			CandidateCommitments,
			DirectParentContactCommitments,
			CandidateParentContactCells,
			CandidateParentTraversalAnchors,
			bAllowsChildTraversalBridgeForCommittedContacts,
			CandidateParentPlanReservedCells,
			CandidateSharedParentChildFaces,
			CandidateBoundaryCertificateId,
			CandidateParentDomainRestrictions,
			bDeferChildTraversalValidationToSchedule,
			bRequireStructuralFeasibilityPrecheck,
			bRunImmediateFullParentProof,
			bDeferFullParentProofToCompleteValidation,
			PreparedParentProofCandidate,
			OutEvaluation,
			PreparedResidualPlan);
		if (bSucceeded && OutPreparedParentProofCandidate != nullptr)
		{
			*OutPreparedParentProofCandidate = MoveTemp(PreparedParentProofCandidate);
		}
		return bSucceeded;
	}

	void OrderSeededChildPlacementCandidatesWithLocalSnap(
		TArray<FCheapChildPlacementCandidateOrder>& InOutCandidates)
	{
		TArray<FCheapChildPlacementCandidateOrder> SeededCandidates = MoveTemp(InOutCandidates);
		SeededCandidates.Sort([](const FCheapChildPlacementCandidateOrder& Left, const FCheapChildPlacementCandidateOrder& Right)
		{
			return Left.TieBreakHash < Right.TieBreakHash;
		});

		TSet<int32> EmittedCandidateIndices;
		InOutCandidates.Reset(SeededCandidates.Num());
		for (const FCheapChildPlacementCandidateOrder& CoarseCandidate : SeededCandidates)
		{
			if (EmittedCandidateIndices.Contains(CoarseCandidate.CandidateIndex))
			{
				continue;
			}

			const FCheapChildPlacementCandidateOrder* SnapCandidate = &CoarseCandidate;
			for (const FCheapChildPlacementCandidateOrder& NearbyCandidate : SeededCandidates)
			{
				if (EmittedCandidateIndices.Contains(NearbyCandidate.CandidateIndex)
					|| GetManhattanDistance(CoarseCandidate.Offset, NearbyCandidate.Offset) > 1
					|| NearbyCandidate.MagnetismScore < SnapCandidate->MagnetismScore)
				{
					continue;
				}
				if (NearbyCandidate.MagnetismScore > SnapCandidate->MagnetismScore
					|| NearbyCandidate.TieBreakHash < SnapCandidate->TieBreakHash)
				{
					SnapCandidate = &NearbyCandidate;
				}
			}

			InOutCandidates.Add(*SnapCandidate);
			EmittedCandidateIndices.Add(SnapCandidate->CandidateIndex);
			if (SnapCandidate->CandidateIndex != CoarseCandidate.CandidateIndex)
			{
				InOutCandidates.Add(CoarseCandidate);
				EmittedCandidateIndices.Add(CoarseCandidate.CandidateIndex);
			}
		}
	}

	void BuildCheapChildPlacementEvaluationOrder(
		const int32 RootSeed,
		const int32 VariantIndex,
		const FPreparedChildPlacementSource& PreparedSource,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const TArray<FIntVector>& ParentPriorityTargets,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const FChildPlacementDemand& Demand,
		const bool bHasRemainingPeerDemand,
		TArray<FCheapChildPlacementCandidateOrder>& OutCandidateEvaluationOrder)
	{
		OutCandidateEvaluationOrder.Reset();
		OutCandidateEvaluationOrder.Reserve(PlanningVariant.NormalizedCandidates.Num());
		for (int32 CandidateIndex = 0; CandidateIndex < PlanningVariant.NormalizedCandidates.Num(); ++CandidateIndex)
		{
			const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate =
				PlanningVariant.NormalizedCandidates[CandidateIndex];

			const int32 MagnetismScore =
				NormalizedCandidate.ParentOwnedSeamOpportunityScore
				+ ScoreSharedSiblingSeamOpportunity(
					NormalizedCandidate.StageMapping.ParentTranslatedPlannedCells,
					FIntVector::ZeroValue,
					PlanningVariant.CapabilityEnvelope,
					CurrentPlacements);
			int32 CheapScore =
				MagnetismScore
				+ (NormalizedCandidate.ParentContactOpportunities.Num() * 25)
				- (GetNearestCellDistance(NormalizedCandidate.Offset, ParentPriorityTargets) * 10);

			if (bHasRemainingPeerDemand
				&& PreparedSource.EntrySnapshot->ChildPlacementZone == ELayoutPlacementZone::Interior
				&& NormalizedCandidate.SharedParentChildFaces.IsEmpty())
			{
				CheapScore -= 4000;
			}

			FCheapChildPlacementCandidateOrder& Order = OutCandidateEvaluationOrder.AddDefaulted_GetRef();
			Order.CandidateIndex = CandidateIndex;
			Order.Offset = NormalizedCandidate.Offset;
			Order.Score = CheapScore;
			Order.MagnetismScore = MagnetismScore;
			Order.TieBreakHash = HashCombineFast(
				BuildChildCandidateTieBreakHash(
					RootSeed,
					PreparedSource.EntrySnapshot->EntryId,
					NormalizedCandidate.Offset,
					Demand.InstanceOrdinal,
					VariantIndex),
				GetTypeHash(NormalizedCandidate.BoundaryCertificateId));
		}

		OrderSeededChildPlacementCandidatesWithLocalSnap(OutCandidateEvaluationOrder);
	}

	void TrimCheapChildPlacementEvaluationOrder(
		const bool bAllowTrimming,
		const bool bPartialPlacementStep,
		const int32 PlacementDemandCount,
		TArray<FCheapChildPlacementCandidateOrder>& InOutCandidateEvaluationOrder)
	{
		if (!bAllowTrimming)
		{
			return;
		}
		if (bPartialPlacementStep && PlacementDemandCount > 2 && InOutCandidateEvaluationOrder.Num() > 12)
		{
			InOutCandidateEvaluationOrder.SetNum(12);
		}
		else if (bPartialPlacementStep && PlacementDemandCount == 2 && InOutCandidateEvaluationOrder.Num() > 64)
		{
			InOutCandidateEvaluationOrder.SetNum(16);
		}
		else if (bPartialPlacementStep && PlacementDemandCount == 2 && InOutCandidateEvaluationOrder.Num() > 24)
		{
			InOutCandidateEvaluationOrder.SetNum(24);
		}
		else if (!bPartialPlacementStep && PlacementDemandCount == 2 && InOutCandidateEvaluationOrder.Num() > 48)
		{
			// Two-demand schedules already explore candidates in stable cheap-score order and
			// the final demand returns on the first validated completion. Trimming the long
			// tail here removes a large amount of entry-resolution churn without changing the
			// early high-value candidates that actually decide these fixtures.
			InOutCandidateEvaluationOrder.SetNum(48);
		}
	}

	void BuildSuccessfulChildPlacementCandidatesForDemand(
		const FString& SchedulerStateKey,
		const int32 DemandIndex,
		const FChildPlacementDemand& Demand,
		const FPreparedChildPlacementSource& PreparedSource,
		const FRecursivePlacementDemandSearcher& Searcher,
		FRecursiveChildPlacementSchedulerState& InOutSchedulerState,
		FChildPlacementCandidateBuildResult& OutResult)
	{
		SCOPED_NAMED_EVENT(Layout_Recursive_CandidateEvaluation, FColor::Cyan);
		TRACE_BOOKMARK(TEXT("Layout_CandidatesBegin region=%s seed=%d demand=%d state=%08x variants=%d"),
			*Searcher.RootRequest.RegionDebugPath.Left(128), Searcher.RootRequest.Seed, DemandIndex,
			GetTypeHash(SchedulerStateKey), PreparedSource.PlanningVariants.Num());
		ON_SCOPE_EXIT
		{
			TRACE_BOOKMARK(TEXT("Layout_CandidatesEnd region=%s demand=%d state=%08x accepted=%d examined=%d planRejects=%d entryRejects=%d probeRejects=%d"),
				*Searcher.RootRequest.RegionDebugPath.Left(128), DemandIndex, GetTypeHash(SchedulerStateKey),
				OutResult.SuccessfulCandidates.Num(), OutResult.CandidateOffsetsExamined, OutResult.ParentPlanRejectCount,
				OutResult.EntryNegotiationRejectCount, OutResult.ProbeRejectCount);
		};
		const FLayoutRegionSolveRequest& RootRequest = Searcher.RootRequest;
		const TArray<FLayoutPlannedCell>& ParentPlannedCells = Searcher.ParentPlannedCells;
		const FIntPoint& ParentFootprintSize = Searcher.ParentFootprintSize;
		const TSet<FIntVector>& ParentPlannedCellSet = Searcher.ParentPlannedCellSet;
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents = Searcher.ParentPlannedCellIntents;
		const TArray<FIntVector>& ParentPriorityTargets = Searcher.ParentPriorityTargets;
		const TSet<FIntVector>& ParentProtectedTraversalCells = Searcher.ParentProtectedTraversalCells;
		const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections =
			Searcher.ParentVerticalAccessIngressDirections;
		const TArray<FChildPlacementDemand>& PlacementDemands = Searcher.PlacementDemands;
		const TArray<FAutomaticChildPlacement>& CurrentPlacements = InOutSchedulerState.CurrentPlacements;
		const TSet<FIntVector>& CurrentReservedParentCells = InOutSchedulerState.CurrentReservedParentCells;
		TMap<FString, FEntryCommitmentOptionMemo>& InOutEntryCommitmentOptionMemo =
			InOutSchedulerState.EntryCommitmentOptionMemo;
		TMap<FString, FParentStructuralFeasibilityMemo>& InOutParentStructuralFeasibilityMemo =
			InOutSchedulerState.ParentStructuralFeasibilityMemo;
		TMap<FString, FParentProbeEvaluationMemo>& InOutParentProbeEvaluationMemo =
			InOutSchedulerState.ParentProbeEvaluationMemo;
		TMap<FString, FParentProbeEvaluationMemo>* SharedParentProbeSolveMemo =
			InOutSchedulerState.SharedParentProbeSolveMemo;
		FRecursiveSchedulerPerfMetrics& InOutPerfMetrics = InOutSchedulerState.PerfMetrics;

		OutResult = FChildPlacementCandidateBuildResult();
		const bool bPartialPlacementStep = (DemandIndex + 1) < PlacementDemands.Num();

		for (int32 VariantIndex = 0; VariantIndex < PreparedSource.PlanningVariants.Num(); ++VariantIndex)
		{
			const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant =
				PreparedSource.PlanningVariants[VariantIndex];
			OutResult.CandidateOffsetsExamined += PlanningVariant.StaticCandidateOffsetCount;
			OutResult.ParentPlanRejectCount += PlanningVariant.StaticParentPlanRejectCount;
			OutResult.StageMappingRejectCount += PlanningVariant.StaticStageMappingRejectCount;
			if (OutResult.BestRejectedCandidateReason.IsEmpty()
				&& !PlanningVariant.FirstStageMappingFailureReason.IsEmpty())
			{
				OutResult.BestRejectedCandidateReason = FString::Printf(
					TEXT("Failed During: Child Stage Mapping\nRejection Detail:\n  %s"),
					*PlanningVariant.FirstStageMappingFailureReason);
			}
			if (OutResult.BestRejectedCandidateReason.IsEmpty()
				&& !PlanningVariant.FirstBoundaryDomainFailureReason.IsEmpty())
			{
				OutResult.BestRejectedCandidateReason = FString::Printf(
					TEXT("Failed During: Joint Child Boundary And Shared Seam Domain\nRejection Detail:\n  %s"),
					*PlanningVariant.FirstBoundaryDomainFailureReason);
			}
			if (OutResult.BestRejectedCandidateReason.IsEmpty() && !PlanningVariant.FirstParentPlanFailureReason.IsEmpty())
				OutResult.BestRejectedCandidateReason = PlanningVariant.FirstParentPlanFailureReason;
			OutResult.PlacementZoneRejectCount += PlanningVariant.StaticPlacementZoneRejectCount;
			OutResult.LevelPlacementRejectCount += PlanningVariant.StaticLevelPlacementRejectCount;

			const bool bDeferFullParentProofToCompleteValidation =
				!bPartialPlacementStep
				&& PlacementDemands.Num() > 1;

			bool bHasRemainingPeerDemand = false;
			for (int32 RemainingDemandIndex = DemandIndex + 1; RemainingDemandIndex < PlacementDemands.Num(); ++RemainingDemandIndex)
			{
				if (PlacementDemands[RemainingDemandIndex].SourceIndex == Demand.SourceIndex)
				{
					bHasRemainingPeerDemand = true;
					break;
				}
			}

			TArray<FCheapChildPlacementCandidateOrder> CandidateEvaluationOrder;
			BuildCheapChildPlacementEvaluationOrder(
				RootRequest.Seed,
				VariantIndex,
				PreparedSource,
				PlanningVariant,
				ParentPriorityTargets,
				CurrentPlacements,
				Demand,
				bHasRemainingPeerDemand,
				CandidateEvaluationOrder);
			if (const FLayoutPreparedChildPlacementHint* PreparedHint =
					RootRequest.PreparedChildPlacementHints.FindByPredicate(
						[&](const FLayoutPreparedChildPlacementHint& Hint)
						{
							return Hint.SourceContentEntryId
									== PreparedSource.EntrySnapshot->EntryId
								&& Hint.InstanceOrdinal == Demand.InstanceOrdinal;
						}))
			{
				CandidateEvaluationOrder.StableSort([&](
					const FCheapChildPlacementCandidateOrder& Left,
					const FCheapChildPlacementCandidateOrder& Right)
				{
					const auto GetHintRank = [&](
						const FCheapChildPlacementCandidateOrder& Candidate)
					{
						const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& Value =
							PlanningVariant.NormalizedCandidates[Candidate.CandidateIndex];
						if (Value.Offset != PreparedHint->RegionCellOffset)
						{
							return 2;
						}
						return Value.StageMapping.MappingId == PreparedHint->StageMappingId
							? 0
							: 1;
					};
					return GetHintRank(Left) < GetHintRank(Right);
				});
			}
			TrimCheapChildPlacementEvaluationOrder(
				Demand.bOptional && !Demand.bSharedFeatureChoiceSlot,
				bPartialPlacementStep,
				PlacementDemands.Num(),
				CandidateEvaluationOrder);

			if (PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
				&& GetRequiredHostVerticalAccessProviderCount(RootRequest, ParentPlannedCells) > 0
				&& !PlanningVariant.InterfaceSummary.bSupportsVerticalAccess)
			{
				++OutResult.ProbeRejectCount;
				const FString RejectedMessage = FString::Printf(
					TEXT("Best Rejected Placement:\nOffset: <variant has no usable host vertical access>\nParent Contact Cells: <none>\nFailed During: Host Vertical Access Contribution\nRejection Detail:\n  Child entry '%s' is marked child_contributes_host_vertical_access, but this planning variant exposes no VerticalAccess cells.\n  Problem: The host region still requires composed vertical-access support, so this child cannot replace the host ascent contract on any accepted placement.\n  Fix: Author usable child vertical-access cells for this child profile, stop marking this child entry as host-contributing, or keep enough parent-owned stair capacity to satisfy the host requirement without this child."),
					*PreparedSource.EntrySnapshot->EntryId.ToString());
				if (OutResult.BestRejectedCandidateReason.IsEmpty())
				{
					OutResult.BestRejectedCandidateReason = RejectedMessage;
				}
				AddUniqueRejectedCandidateReason(
					RejectedMessage,
					OutResult.RejectedCandidateReasons);
				continue;
			}

			for (const FCheapChildPlacementCandidateOrder& CandidateOrder : CandidateEvaluationOrder)
			{
				const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate =
					PlanningVariant.NormalizedCandidates[CandidateOrder.CandidateIndex];
				if (!DoesMappedChildPlacementMatchRequiredZone(
					Demand,
					ParentFootprintSize,
					NormalizedCandidate))
				{
					++OutResult.ParentPlanRejectCount;
					continue;
				}
				const FIntVector& CandidateOffset = NormalizedCandidate.Offset;
				const TArray<FIntVector>& CandidateParentPlanReservedCells = NormalizedCandidate.ParentPlanReservedCells;
				const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces = NormalizedCandidate.SharedParentChildFaces;

				bool bFitsParentPlan = true;
				FString ParentPlanCompatibilityFailureReason;
				for (const FLayoutPlannedCell& ChildPlannedCell : PlanningVariant.PlannedCells)
				{
					const FIntVector ParentCell = ResolveMappedParentCell(
						NormalizedCandidate.StageMapping,
						ChildPlannedCell.Cell,
						CandidateOffset);
					// Preserve frozen direct-parent egress when later siblings claim nearby shared shell.
					const FAutomaticChildPlacement* const OccludedPlacement =
						CurrentPlacements.FindByPredicate(
							[&ParentCell](const FAutomaticChildPlacement& ExistingPlacement)
							{
								return ExistingPlacement.ParentCommittedEntryCells.Contains(ParentCell);
							});
					if (OccludedPlacement != nullptr)
					{
						bFitsParentPlan = false;
						ParentPlanCompatibilityFailureReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Parent Plan Compatibility\nRejection Detail:\n  Parent cell %s is the frozen direct-parent Entry contact for sibling '%s'.\n  Problem: Occupying that cell would turn the sibling's parent-facing Entry into a blocked shared-shell contact.\n  Fix: Move or rotate this child so the sibling retains one traversable parent egress."),
							*CandidateOffset.ToString(),
							*ParentCell.ToString(),
							*OccludedPlacement->ChildRegionDebugPath);
						break;
					}

					FString SiblingSeamFailureReason;
					if (CurrentReservedParentCells.Contains(ParentCell)
						&& !CanChildReservationShareSiblingSeamCell(
							ChildPlannedCell,
							CandidateOffset,
							PlanningVariant.FootprintSize,
							PlanningVariant.CapabilityEnvelope,
							CurrentPlacements,
							&SiblingSeamFailureReason,
							&ParentCell))
					{
						bFitsParentPlan = false;
						ParentPlanCompatibilityFailureReason = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Parent Plan Compatibility\nRejection Detail:\n  Parent cell %s is already reserved by another child and the overlap is not a valid sibling shared wall seam.\n  Sibling seam detail: %s"),
							*CandidateOffset.ToString(),
							*ParentCell.ToString(),
							*IndentMultiline(
								SiblingSeamFailureReason.IsEmpty()
									? TEXT("No additional sibling seam detail was reported.")
									: SiblingSeamFailureReason,
								TEXT("  ")));
						break;
					}
				}

				if (!bFitsParentPlan)
				{
					if (OutResult.BestRejectedCandidateReason.IsEmpty())
					{
						OutResult.BestRejectedCandidateReason = ParentPlanCompatibilityFailureReason;
					}
					AddUniqueRejectedCandidateReason(
						ParentPlanCompatibilityFailureReason,
						OutResult.RejectedCandidateReasons);
					++OutResult.ParentPlanRejectCount;
					continue;
				}

				if (bHasRemainingPeerDemand
					&& PreparedSource.EntrySnapshot->ChildPlacementZone == ELayoutPlacementZone::Interior
					&& CandidateSharedParentChildFaces.IsEmpty())
				{
					const FString NoParentSeamReason = FString::Printf(
						TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Parent Plan Compatibility\nRejection Detail:\n  This placement uses no parent-child shared wall faces.\n  Problem: Additional required copies of this interior child entry remain, and a non-sharing placement would strand too much interior area for the remaining placements.\n  Fix: Move or rotate the child so it shares a compatible parent wall seam before consuming interior space needed by sibling children."),
						*CandidateOffset.ToString());
					if (OutResult.BestRejectedCandidateReason.IsEmpty())
					{
						OutResult.BestRejectedCandidateReason = NoParentSeamReason;
					}
					AddUniqueRejectedCandidateReason(
						NoParentSeamReason,
						OutResult.RejectedCandidateReasons);
					++OutResult.ParentPlanRejectCount;
					continue;
				}

				if (bHasRemainingPeerDemand
					&& !CanAnyRemainingPeerFitStructurally(
						PreparedSource,
						PlanningVariant,
						CurrentPlacements,
						CurrentReservedParentCells,
						ParentPlannedCellSet,
						ParentPlannedCellIntents,
						ParentProtectedTraversalCells,
						CandidateParentPlanReservedCells,
						CandidateOffset,
						NormalizedCandidate.StageMapping,
						Demand))
				{
					const FString RemainingPeerFailureReason = FString::Printf(
						TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Parent Plan Compatibility\nRejection Detail:\n  Accepting this child placement leaves no structurally valid translated placement for the remaining required copy of '%s'.\n  Problem: The first child consumed parent space or seam opportunities that the remaining identical child still needs.\n  Fix: Move or rotate the child so at least one remaining peer footprint still fits before deeper entry/traversal negotiation."),
						*CandidateOffset.ToString(),
						*PreparedSource.EntrySnapshot->EntryId.ToString());
					if (OutResult.BestRejectedCandidateReason.IsEmpty())
					{
						OutResult.BestRejectedCandidateReason = RemainingPeerFailureReason;
					}
					AddUniqueRejectedCandidateReason(
						RemainingPeerFailureReason,
						OutResult.RejectedCandidateReasons);
					++OutResult.ParentPlanRejectCount;
					continue;
				}

				FScoredChildOffset Candidate;
				Candidate.VariantIndex = VariantIndex;
				Candidate.Offset = CandidateOffset;
				Candidate.StageMapping = NormalizedCandidate.StageMapping;
				Candidate.BoundaryCertificateId = NormalizedCandidate.BoundaryCertificateId;
				Candidate.ParentDomainRestrictions = NormalizedCandidate.ParentDomainRestrictions;
				Candidate.ChildDomainRestrictions = NormalizedCandidate.ChildDomainRestrictions;
				Candidate.ChildIncomingBoundaryPoints = NormalizedCandidate.ChildIncomingBoundaryPoints;
				// Static structural commitments must survive even when this child has no
				// Entry options and commitment resolution therefore has no callback.
				Candidate.ParentPlanReservedCells = CandidateParentPlanReservedCells;
				Candidate.SharedParentChildFaces = CandidateSharedParentChildFaces;
				for (const FSharedParentChildFace& SharedFace : Candidate.SharedParentChildFaces)
				{
					if (!SharedFace.ParentSeamCapabilityId.IsNone())
					{
						Candidate.BoundaryWitnessIds.AddUnique(SharedFace.ParentSeamCapabilityId);
					}
					if (!SharedFace.ChildSeamCapabilityId.IsNone())
					{
						Candidate.BoundaryWitnessIds.AddUnique(SharedFace.ChildSeamCapabilityId);
					}
					if (!SharedFace.ReciprocalDomainWitnessId.IsNone())
					{
						Candidate.BoundaryWitnessIds.AddUnique(SharedFace.ReciprocalDomainWitnessId);
					}
				}
				Candidate.BoundaryWitnessIds.Sort();
				Candidate.Score = NormalizedCandidate.ParentOwnedSeamOpportunityScore
					+ ScoreSharedSiblingSeamOpportunity(
						NormalizedCandidate.StageMapping.ParentTranslatedPlannedCells,
						FIntVector::ZeroValue,
						PlanningVariant.CapabilityEnvelope,
						CurrentPlacements);
				Candidate.TieBreakHash = HashCombineFast(
					BuildChildCandidateTieBreakHash(
						RootRequest.Seed,
						PreparedSource.EntrySnapshot->EntryId,
						CandidateOffset,
						Demand.InstanceOrdinal,
						VariantIndex),
					GetTypeHash(NormalizedCandidate.BoundaryCertificateId));
				const bool bTrackCandidateScoreBreakdown =
					ShouldTrackDetailedRecursiveSchedulerPerf();
				bool bForceFullParentProbeForResolvedCombinationSelection = false;
				bool bRequireStructuralFeasibilityForTrackedResolvedCombinations = false;
				TMap<FSuccessfulCommitmentCombinationSignature, FPreparedChildParentProofCandidate>
					PreparedParentProofCandidatesBySignature;
				const auto ApplyAcceptedPreparedParentProofCandidate =
					[&](
						const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
						const TArray<FIntVector>& CandidateParentContactCells,
						const FPreparedChildParentProofCandidate& PreparedParentProofCandidate,
						const FParentProbeEvaluation& ParentProbeEvaluation,
						int32& InOutScoreAdjustment,
						TArray<FLayoutSolveBoundaryPoint>& InOutBoundaryPoints)
				{
					Candidate.NegotiatedHostVerticalAccessResponsibility =
						PreparedParentProofCandidate.NegotiatedHostVerticalAccessResponsibility;
					Candidate.bHasRequiredHostIngressAnchor =
						PreparedParentProofCandidate.bHasRequiredHostIngressAnchor;
					Candidate.RequiredHostIngressAnchor =
						PreparedParentProofCandidate.RequiredHostIngressAnchor;
					Candidate.bHasRequiredHostEgressAnchor =
						PreparedParentProofCandidate.bHasRequiredHostEgressAnchor;
					Candidate.RequiredHostEgressAnchor =
						PreparedParentProofCandidate.RequiredHostEgressAnchor;
					Candidate.RequiredChildInternalVerticalRouteCells =
						PreparedParentProofCandidate.RequiredChildInternalVerticalRouteCells;
					FPlacementBridgeSolveContext PreparedProofSolveContext;
					PreparedProofSolveContext.RootRequest = &RootRequest;
					PreparedProofSolveContext.ParentPlannedCells = &ParentPlannedCells;
					PreparedProofSolveContext.ParentFootprintSize =
						ParentFootprintSize;
					PreparedProofSolveContext.ParentPlannedCellSet =
						&ParentPlannedCellSet;
					PreparedProofSolveContext.ParentPlannedCellIntents =
						&ParentPlannedCellIntents;
					PreparedProofSolveContext.ParentPriorityTargets =
						&ParentPriorityTargets;
					PreparedProofSolveContext.ParentProtectedTraversalCells =
						&ParentProtectedTraversalCells;
					PreparedProofSolveContext
						.ParentVerticalAccessIngressDirections =
						&ParentVerticalAccessIngressDirections;
					FPlacementBridgeMutableStateView PreparedProofMutableState;
					PreparedProofMutableState.CurrentPlacements = &CurrentPlacements;
					PreparedProofMutableState.CurrentReservedParentCells =
						&CurrentReservedParentCells;
					BuildPreparedParentProofRequest(
						PreparedProofSolveContext,
						PreparedProofMutableState,
						PreparedParentProofCandidate.PreparedParentProbeCandidate,
						ParentProbeEvaluation,
						Candidate.DeferredPreparedParentProofRequest);
					Candidate.bHasDeferredPreparedParentProofRequest = true;

					InOutScoreAdjustment = ParentProbeEvaluation.ScoreAdjustment;
					if (bTrackCandidateScoreBreakdown)
					{
						Candidate.ScoreBreakdown = FChildPlacementScoreBreakdown{};
						Candidate.ScoreBreakdown.bHasDiagnostics = true;
						Candidate.ScoreBreakdown.ProbeRouteConstraintScore =
							ParentProbeEvaluation.ScoreAdjustment;
						Candidate.ScoreBreakdown.AdjustmentTotal =
							ParentProbeEvaluation.ScoreAdjustment;
					}
					if (!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
						&& PlanningVariant.MaxLocalLevel == 0
						&& CandidateOffset.Z > 0)
					{
						const int32 VerticalLevelPenalty = CandidateOffset.Z * 250;
						InOutScoreAdjustment -= VerticalLevelPenalty;
						if (bTrackCandidateScoreBreakdown)
						{
							Candidate.ScoreBreakdown.VerticalLevelPenalty =
								-VerticalLevelPenalty;
							Candidate.ScoreBreakdown.AdjustmentTotal -= VerticalLevelPenalty;
						}
					}
					int32 ParentBoundaryTouchCount = 0;
					int32 ParentShellAdjacencyCount = 0;
					if (!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
						&& PlanningVariant.MaxLocalLevel == 0)
					{
						for (const FLayoutPlannedCell& ChildPlannedCell : PlanningVariant.PlannedCells)
						{
							const FIntVector ParentCell = ResolveMappedParentCell(
								NormalizedCandidate.StageMapping,
								ChildPlannedCell.Cell,
								CandidateOffset);
							if (ParentCell.X <= 0
								|| ParentCell.Y <= 0
								|| ParentCell.X >= (ParentFootprintSize.X - 1)
								|| ParentCell.Y >= (ParentFootprintSize.Y - 1))
							{
								++ParentBoundaryTouchCount;
							}
							else if (ParentCell.X <= 1
								|| ParentCell.Y <= 1
								|| ParentCell.X >= (ParentFootprintSize.X - 2)
								|| ParentCell.Y >= (ParentFootprintSize.Y - 2))
							{
								++ParentShellAdjacencyCount;
							}
						}
						Candidate.bTouchesParentOuterShell =
							(ParentBoundaryTouchCount > 0 || ParentShellAdjacencyCount > 0);

						if (ParentBoundaryTouchCount > 0)
						{
							const int32 BoundaryTouchPenalty =
								ParentBoundaryTouchCount * 125;
							InOutScoreAdjustment -= BoundaryTouchPenalty;
							if (bTrackCandidateScoreBreakdown)
							{
								Candidate.ScoreBreakdown.ParentBoundaryTouchCount =
									ParentBoundaryTouchCount;
								Candidate.ScoreBreakdown.BoundaryTouchPenalty =
									-BoundaryTouchPenalty;
								Candidate.ScoreBreakdown.AdjustmentTotal -=
									BoundaryTouchPenalty;
							}
						}
						if (ParentShellAdjacencyCount > 0)
						{
							const int32 ShellAdjacencyPenalty =
								ParentShellAdjacencyCount * 60;
							InOutScoreAdjustment -= ShellAdjacencyPenalty;
							if (bTrackCandidateScoreBreakdown)
							{
								Candidate.ScoreBreakdown.ParentShellAdjacencyCount =
									ParentShellAdjacencyCount;
								Candidate.ScoreBreakdown.ShellAdjacencyPenalty =
									-ShellAdjacencyPenalty;
								Candidate.ScoreBreakdown.AdjustmentTotal -=
									ShellAdjacencyPenalty;
							}
						}
					}
					const int32 ParentChildSeamScore = ScoreSharedParentChildSeamOpportunity(
						PreparedParentProofCandidate.EffectiveSharedParentChildFaces);
					const bool bInteriorChildTouchesParentBoundary =
						PreparedSource.EntrySnapshot->ChildPlacementZone
							== ELayoutPlacementZone::Interior
						&& ParentBoundaryTouchCount > 0;
					const int32 EffectiveParentChildSeamScore =
						bInteriorChildTouchesParentBoundary ? 0 : ParentChildSeamScore;
					InOutScoreAdjustment += EffectiveParentChildSeamScore;
					if (bTrackCandidateScoreBreakdown)
					{
						Candidate.ScoreBreakdown.ParentChildSeamScore =
							EffectiveParentChildSeamScore;
						Candidate.ScoreBreakdown.AdjustmentTotal +=
							EffectiveParentChildSeamScore;
					}
					const int32 SiblingSeamScore = ScoreSharedSiblingSeamOpportunity(
						NormalizedCandidate.StageMapping.ParentTranslatedPlannedCells,
						FIntVector::ZeroValue,
						PlanningVariant.CapabilityEnvelope,
						CurrentPlacements);
					const int32 EffectiveSiblingSeamScore =
						PreparedSource.EntrySnapshot->ChildPlacementZone
							== ELayoutPlacementZone::Interior
						? 0
						: SiblingSeamScore;
					InOutScoreAdjustment += EffectiveSiblingSeamScore;
					if (bTrackCandidateScoreBreakdown)
					{
						Candidate.ScoreBreakdown.SiblingSeamScore = EffectiveSiblingSeamScore;
						Candidate.ScoreBreakdown.AdjustmentTotal += EffectiveSiblingSeamScore;
					}

					InOutBoundaryPoints = ParentProbeEvaluation.ParentTranslatedBoundaryPoints;
					Candidate.ParentTranslatedBoundaryPoints = ParentProbeEvaluation.ParentTranslatedBoundaryPoints;
					Candidate.DirectParentContactCommitments =
						PreparedParentProofCandidate.DirectParentContactCommitments;
					Candidate.ParentCommittedTraversalAnchors =
						ParentProbeEvaluation.ParentCommittedTraversalAnchors;
					Candidate.bAllowsChildTraversalBridgeForCommittedContacts =
						PreparedParentProofCandidate.bAllowsChildTraversalBridgeForCommittedContacts;
					if (PreparedParentProofCandidate.bSiblingOnlyNoContactAttachment)
					{
						Candidate.DirectParentContactCommitments.Reset();
						Candidate.ParentCommittedTraversalAnchors.Reset();
						Candidate.bAllowsChildTraversalBridgeForCommittedContacts = false;
					}

					Candidate.ParentPlanReservedCells = CandidateParentPlanReservedCells;
					Candidate.SharedParentChildFaces =
						PreparedParentProofCandidate.EffectiveSharedParentChildFaces;
					Candidate.BoundaryWitnessIds.Reset();
					for (const FLayoutCommittedEndpointAnchor& Commitment : CandidateCommitments)
					{
						bool bFoundDirectWitness = false;
						for (const auto& Opportunity : NormalizedCandidate.ParentContactOpportunities)
						{
							if (Opportunity.ChildEntryCell == Commitment.LocalCell
								&& Opportunity.FaceDirection == Commitment.FaceDirection
								&& CandidateParentContactCells.Contains(Opportunity.ParentContactCell)
								&& Opportunity.BoundaryWitnessId != NAME_None)
							{
								Candidate.BoundaryWitnessIds.AddUnique(Opportunity.BoundaryWitnessId);
								bFoundDirectWitness = true;
							}
						}
						if (!bFoundDirectWitness)
						{
							Candidate.BoundaryWitnessIds.AddUnique(FLayoutId(*FString::Printf(
								TEXT("%s.Boundary.%s.%d.%s"),
								*PreparedSource.EntrySnapshot->EntryId.ToString(),
								*Commitment.LocalCell.ToString(),
								static_cast<int32>(Commitment.FaceDirection),
								Commitment.ConnectionTag.IsValid()
									? *Commitment.ConnectionTag.ToString()
									: TEXT("None"))));
						}
					}
					for (const FSharedParentChildFace& SharedFace :
						PreparedParentProofCandidate.EffectiveSharedParentChildFaces)
					{
						if (!SharedFace.ParentSeamCapabilityId.IsNone())
						{
							Candidate.BoundaryWitnessIds.AddUnique(SharedFace.ParentSeamCapabilityId);
						}
						if (!SharedFace.ChildSeamCapabilityId.IsNone())
						{
							Candidate.BoundaryWitnessIds.AddUnique(SharedFace.ChildSeamCapabilityId);
						}
						if (!SharedFace.ReciprocalDomainWitnessId.IsNone())
						{
							Candidate.BoundaryWitnessIds.AddUnique(
								SharedFace.ReciprocalDomainWitnessId);
						}
						Candidate.BoundaryWitnessIds.AddUnique(FLayoutId(*FString::Printf(
							TEXT("%s.SeamBoundary.%s.%d.%s"),
							*PreparedSource.EntrySnapshot->EntryId.ToString(),
							*SharedFace.ChildLocalCell.ToString(),
							static_cast<int32>(SharedFace.FaceDirection),
							SharedFace.InterfaceFamily.IsValid()
								? *SharedFace.InterfaceFamily.ToString()
								: TEXT("None"))));
					}
					Candidate.BoundaryWitnessIds.Sort();
				};
				const auto FinalizeCandidateScoreBreakdown =
					[&]()
				{
					if (!bTrackCandidateScoreBreakdown
						|| !Candidate.ScoreBreakdown.bHasDiagnostics)
					{
						return;
					}

					Candidate.ScoreBreakdown.FinalScore = Candidate.Score;
					Candidate.ScoreBreakdown.PreAdjustmentResolvedScore =
						Candidate.Score - Candidate.ScoreBreakdown.AdjustmentTotal;
				};
				struct FChildParentProofPreparationInputs
				{
					const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments;
					const TArray<FIntVector>& CandidateParentContactCells;
					const TArray<FLayoutCommittedEndpointAnchor>& DirectParentContactCommitments;
					const TArray<FSharedParentChildFace>& EffectiveSharedParentChildFaces;
					const TArray<FLayoutCommittedTraversalAnchor>& SelectedParentTraversalAnchors;
					const FSuccessfulCommitmentCombinationSignature& CommitmentCombinationSignature;
					bool bHasSiblingOnlyNoParentEntry = false;
					bool bCanDeferRecursiveParentProbeFailure = false;
					bool bAllowsChildTraversalBridgeForCommittedContacts = false;
				};
				const auto TryPrepareChildParentProofCandidate =
					[&](
						const FChildParentProofPreparationInputs& Inputs,
						int32& OutScoreAdjustment,
						TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints,
						bool& OutRejectedByStructuralPrecheck,
						bool& OutReachedFullParentProof,
						bool& OutSurvivedFullParentProof)
				{
					OutRejectedByStructuralPrecheck = false;
					OutReachedFullParentProof = false;
					OutSurvivedFullParentProof = false;
					TSet<FIntVector> PrecheckReservedCells;
					TSet<FIntVector> PrecheckProtectedTraversalCells = ParentProtectedTraversalCells;
					for (const FAutomaticChildPlacement& ExistingPlacement : CurrentPlacements)
					{
						for (const FIntVector& ReservedCell : ExistingPlacement.ParentPlanReservedCells)
						{
							PrecheckReservedCells.Add(ReservedCell);
						}
						for (const FIntVector& ParentContactCell : ExistingPlacement.ParentCommittedEntryCells)
						{
							PrecheckProtectedTraversalCells.Add(ParentContactCell);
						}
					}
					for (const FIntVector& ReservedCell : CandidateParentPlanReservedCells)
					{
						PrecheckReservedCells.Add(ReservedCell);
					}
					for (const FIntVector& ParentContactCell : Inputs.CandidateParentContactCells)
					{
						PrecheckProtectedTraversalCells.Add(ParentContactCell);
					}
					FCommittedVerticalAccessOwnership ProspectiveVerticalAccessOwnership =
						DetermineCommittedVerticalAccessOwnershipForProspectiveCandidate(
							RootRequest,
							ParentPlannedCells,
							CurrentReservedParentCells,
							CurrentPlacements,
							Demand.ChildRegionDebugPath,
							PlanningVariant.InterfaceSummary.bSupportsVerticalAccess,
							PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess,
							PlanningVariant.InterfaceSummary.VerticalAccessCells,
							CandidateParentPlanReservedCells,
							PlanningVariant.PlannedCells,
							Inputs.CandidateCommitments,
							Inputs.EffectiveSharedParentChildFaces);
					const ELayoutNegotiatedHostVerticalAccessResponsibility NegotiatedVerticalAccessResponsibility =
						DetermineNegotiatedHostVerticalAccessResponsibility(
							Demand.ChildRegionDebugPath,
							ProspectiveVerticalAccessOwnership);
					Candidate.NegotiatedHostVerticalAccessResponsibility = NegotiatedVerticalAccessResponsibility;
					Candidate.RequiredChildInternalVerticalRouteCells.Reset();
					Candidate.bHasRequiredHostIngressAnchor = false;
					Candidate.bHasRequiredHostEgressAnchor = false;
					if (NegotiatedVerticalAccessResponsibility != ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned)
					{
						FLayoutCommittedEndpointAnchor SelectedIngressAnchor;
						FLayoutCommittedEndpointAnchor SelectedEgressAnchor;
						TArray<FIntVector> SelectedRouteCells;
						FString AnchorNegotiationFailureReason;
						if (!TrySelectNegotiatedHostVerticalAccessAnchors(
							Inputs.CandidateCommitments,
							PlanningVariant.PlannedCells,
							PlanningVariant.InterfaceSummary.VerticalAccessCells,
							SelectedIngressAnchor,
							SelectedEgressAnchor,
							SelectedRouteCells,
							AnchorNegotiationFailureReason))
						{
							++OutResult.EntryNegotiationRejectCount;
							const FString NegotiationMode =
								NegotiatedVerticalAccessResponsibility == ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned
									? TEXT("ChildOwned")
									: TEXT("Composed");
							const FString AnchorNegotiationFailure = FString::Printf(
								TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: %s\nFailed During: Host Vertical Access Anchor Negotiation\nRejection Detail:\n  Negotiation classified this child placement as %s host vertical-access support, but it could not commit distinct lower and upper host-facing anchors.\n  Detail: %s\n  Problem: Without explicit ingress/egress anchor commitments, the residual parent solve would still have to rediscover who owns the cross-level ascent obligation.\n  Fix: Author the child with host-facing door/entry contacts on the lower and upper covered levels, or keep the ascent parent-owned for this placement."),
								*CandidateOffset.ToString(),
								*(Inputs.CandidateParentContactCells.IsEmpty()
									? FString(TEXT("<none>"))
									: FString::JoinBy(Inputs.CandidateParentContactCells, TEXT(", "), [](const FIntVector& Cell)
									{
										return Cell.ToString();
									})),
								*NegotiationMode,
								*AnchorNegotiationFailureReason);
							UpdateBestRejectedCandidateReason(
								Candidate.Score,
								AnchorNegotiationFailure,
								OutResult.BestRejectedCandidateScore,
								OutResult.BestRejectedCandidateReason);
							AddUniqueRejectedCandidateReason(
								AnchorNegotiationFailure,
								OutResult.RejectedCandidateReasons);
							return false;
						}

						Candidate.bHasRequiredHostIngressAnchor = true;
						Candidate.RequiredHostIngressAnchor = SelectedIngressAnchor;
						Candidate.bHasRequiredHostEgressAnchor = true;
						Candidate.RequiredHostEgressAnchor = SelectedEgressAnchor;
						Candidate.RequiredChildInternalVerticalRouteCells = MoveTemp(SelectedRouteCells);
					}

					FPreparedParentProbeResidualPlan PreparedResidualPlan;
					TArray<FLayoutPlannedCell>& PrecheckParentPlannedCells =
						PreparedResidualPlan.PlannedCells;
					FString ParentPlanConnectivityFailureReason;
					if (!TryBuildParentPlannedCellsWithReservedChildren(
						ParentPlannedCells,
						ParentPlannedCellSet,
						ParentFootprintSize,
						PrecheckReservedCells,
						PrecheckProtectedTraversalCells,
						RootRequest.Seed,
						RootRequest.IncomingBoundaryPoints.IsEmpty(),
						ProspectiveVerticalAccessOwnership,
						RootRequest.VerticalAccessHostGroups,
						PrecheckParentPlannedCells,
						&PreparedResidualPlan.FilteredVerticalAccessHostGroups,
						&ParentPlanConnectivityFailureReason))
					{
						++OutResult.ProbeRejectCount;
						FParentProbeEvaluation ParentProbeEvaluation;
						ParentProbeEvaluation.FailureReason = ParentPlanConnectivityFailureReason;
						const FString RejectedMessage = BuildRejectedChildOffsetMessage(
							CandidateOffset,
							Inputs.CandidateParentContactCells,
							ParentProbeEvaluation);
						UpdateBestRejectedCandidateReason(
							Candidate.Score,
							RejectedMessage,
							OutResult.BestRejectedCandidateScore,
							OutResult.BestRejectedCandidateReason);
						AddUniqueRejectedCandidateReason(
							RejectedMessage,
							OutResult.RejectedCandidateReasons);
						return false;
					}

					if (!RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
						PrecheckParentPlannedCells,
						ProspectiveVerticalAccessOwnership,
						&ParentPlanConnectivityFailureReason))
					{
						++OutResult.ProbeRejectCount;
						FParentProbeEvaluation ParentProbeEvaluation;
						ParentProbeEvaluation.FailureReason = ParentPlanConnectivityFailureReason;
						const FString RejectedMessage = BuildRejectedChildOffsetMessage(
							CandidateOffset,
							Inputs.CandidateParentContactCells,
							ParentProbeEvaluation);
						UpdateBestRejectedCandidateReason(
							Candidate.Score,
							RejectedMessage,
							OutResult.BestRejectedCandidateScore,
							OutResult.BestRejectedCandidateReason);
						AddUniqueRejectedCandidateReason(
							RejectedMessage,
							OutResult.RejectedCandidateReasons);
						return false;
					}
					PreparedResidualPlan.VerticalAccessOwnership =
						ProspectiveVerticalAccessOwnership;

					TMap<FIntVector, ELayoutCellIntent> PrecheckParentPlannedCellIntents;
					for (const FLayoutPlannedCell& PlannedCell : PrecheckParentPlannedCells)
					{
						PrecheckParentPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
					}

					for (const FIntVector& ParentContactCell : Inputs.CandidateParentContactCells)
					{
						if (PrecheckParentPlannedCellIntents.Contains(ParentContactCell))
						{
							continue;
						}

						++OutResult.ProbeRejectCount;
						const FString RejectedMessage = FString::Printf(
							TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: %s\nFailed During: Child Entry Negotiation\nRejection Detail:\n  Parent contact cell %s does not survive into the derived residual parent plan.\n  Problem: Negotiation committed a child entry contact on a parent cell that is removed by the accepted child reservation/ownership split, so the later parent proof cannot realize the anchor.\n  Fix: Move or rotate the child so the committed contact lands on a surviving residual parent cell, or change the responsibility split so this face is negotiated as child-owned/seam-only instead."),
							*CandidateOffset.ToString(),
							*(Inputs.CandidateParentContactCells.IsEmpty()
								? FString(TEXT("<none>"))
								: FString::JoinBy(Inputs.CandidateParentContactCells, TEXT(", "), [](const FIntVector& Cell)
								{
									return Cell.ToString();
								})),
							*ParentContactCell.ToString());
						UpdateBestRejectedCandidateReason(
							Candidate.Score,
							RejectedMessage,
							OutResult.BestRejectedCandidateScore,
							OutResult.BestRejectedCandidateReason);
						AddUniqueRejectedCandidateReason(
							RejectedMessage,
							OutResult.RejectedCandidateReasons);
						return false;
					}

					const bool bParentRequiresHostVerticalAccess =
						RootRequest.ProfileSnapshot.VerticalAccessCountMode != ELayoutCountConstraintMode::None
						&& FMath::Max(
							RootRequest.ProfileSnapshot.VerticalAccessCount,
							RootRequest.ProfileSnapshot.MaxVerticalAccessCount) > 0;
					const bool bAllowsChildTraversalBridgeForCommittedContacts =
						!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
						&& PlanningVariant.MaxLocalLevel == 0
						&& Inputs.CandidateParentContactCells.Num() > 1;

					if (!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
						&& !Inputs.CandidateParentContactCells.IsEmpty())
					{
						auto IsRootConnectedCorridorIntent = [bParentRequiresHostVerticalAccess](const ELayoutCellIntent Intent) -> bool
						{
							switch (Intent)
							{
							case ELayoutCellIntent::Boundary:
							case ELayoutCellIntent::Entry:
							case ELayoutCellIntent::Core:
							case ELayoutCellIntent::Interior:
							case ELayoutCellIntent::Connector:
								return true;
							case ELayoutCellIntent::VerticalAccess:
								return bParentRequiresHostVerticalAccess;
							default:
								return false;
							}
						};

						TSet<FIntVector> RootConnectedCorridorCells;
						TArray<FIntVector> CorridorFrontier;
						for (const TPair<FIntVector, ELayoutCellIntent>& Pair : PrecheckParentPlannedCellIntents)
						{
							if (Pair.Value != ELayoutCellIntent::Entry)
							{
								continue;
							}

							RootConnectedCorridorCells.Add(Pair.Key);
							CorridorFrontier.Add(Pair.Key);
						}

						for (int32 FrontierIndex = 0; FrontierIndex < CorridorFrontier.Num(); ++FrontierIndex)
						{
							const FIntVector CurrentCell = CorridorFrontier[FrontierIndex];
							for (const ELayoutFaceDirection Direction :
								{ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
							{
								const FIntVector NeighborCell =
									CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
								const ELayoutCellIntent* NeighborIntent =
									PrecheckParentPlannedCellIntents.Find(NeighborCell);
								if (NeighborIntent == nullptr
									|| !IsRootConnectedCorridorIntent(*NeighborIntent)
									|| RootConnectedCorridorCells.Contains(NeighborCell))
								{
									continue;
								}

								RootConnectedCorridorCells.Add(NeighborCell);
								CorridorFrontier.Add(NeighborCell);
							}
						}

						static const ELayoutFaceDirection LateralSupportDirections[] =
						{
							ELayoutFaceDirection::PosX,
							ELayoutFaceDirection::NegX,
							ELayoutFaceDirection::PosY,
							ELayoutFaceDirection::NegY
						};

						bool bHasAnyRootConnectedCorridorContact = false;
						for (const FIntVector& ParentContactCell : Inputs.CandidateParentContactCells)
						{
							bool bHasNonVerticalSupportNeighbor = false;
							bool bTouchesRootConnectedCorridor = RootConnectedCorridorCells.Contains(ParentContactCell);
							for (const ELayoutFaceDirection Direction : LateralSupportDirections)
							{
								const FIntVector NeighborCell =
									ParentContactCell + FLayoutDirectionUtils::ToCellDelta(Direction);
								const ELayoutCellIntent* NeighborIntent =
									PrecheckParentPlannedCellIntents.Find(NeighborCell);
								if (NeighborIntent == nullptr
									|| (!bParentRequiresHostVerticalAccess && *NeighborIntent == ELayoutCellIntent::VerticalAccess))
								{
									continue;
								}

								bHasNonVerticalSupportNeighbor = true;
								if (RootConnectedCorridorCells.Contains(NeighborCell))
								{
									bTouchesRootConnectedCorridor = true;
								}
							}

							bHasAnyRootConnectedCorridorContact =
								bHasAnyRootConnectedCorridorContact || bTouchesRootConnectedCorridor;
							// Non-contributing child rooms can attach to surviving side cells when the
							// parent still owns the host vertical-access route; requiring every room
							// contact to land on the entry corridor over-rejects valid authored castles.
							const bool bRequiresRootConnectedCorridorForThisContact =
								!bParentRequiresHostVerticalAccess
								&& !bAllowsChildTraversalBridgeForCommittedContacts;
							if (!bHasNonVerticalSupportNeighbor
								|| (bRequiresRootConnectedCorridorForThisContact && !bTouchesRootConnectedCorridor))
							{
								++OutResult.ProbeRejectCount;
								const FString RejectedMessage = FString::Printf(
									TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: %s\nFailed During: Child Entry Negotiation\nRejection Detail:\n  Parent contact cell %s does not touch the surviving root-connected parent corridor after child reservation.\n  Problem: This child does not contribute host vertical access, so its direct parent contact must land on or immediately touch the same surviving corridor component that still reaches the parent entry, rather than hanging off an isolated pocket or stair-only branch.\n  Fix: Move or rotate the child so its committed parent contact lands on the surviving corridor component, or author the child to participate in host vertical-access responsibility."),
									*CandidateOffset.ToString(),
									*(Inputs.CandidateParentContactCells.IsEmpty()
										? FString(TEXT("<none>"))
										: FString::JoinBy(Inputs.CandidateParentContactCells, TEXT(", "), [](const FIntVector& Cell)
										{
											return Cell.ToString();
										})),
									*ParentContactCell.ToString());
								UpdateBestRejectedCandidateReason(
									Candidate.Score,
									RejectedMessage,
									OutResult.BestRejectedCandidateScore,
									OutResult.BestRejectedCandidateReason);
								AddUniqueRejectedCandidateReason(
									RejectedMessage,
									OutResult.RejectedCandidateReasons);
								return false;
							}
						}

						if (bAllowsChildTraversalBridgeForCommittedContacts
							&& !bHasAnyRootConnectedCorridorContact)
						{
							++OutResult.ProbeRejectCount;
							const FString RejectedMessage = FString::Printf(
								TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: %s\nFailed During: Child Entry Negotiation\nRejection Detail:\n  None of the committed parent contact cells touch the surviving root-connected parent corridor after child reservation.\n  Problem: A one-level multi-entry child may bridge disconnected parent pockets, but at least one committed parent contact still has to land on the surviving root-connected parent corridor so merged traversal can enter the child from the parent route graph.\n  Fix: Move or rotate the child so at least one committed contact lands on the surviving corridor component, or reduce the child entry commitment set for this placement."),
								*CandidateOffset.ToString(),
								*(Inputs.CandidateParentContactCells.IsEmpty()
									? FString(TEXT("<none>"))
									: FString::JoinBy(Inputs.CandidateParentContactCells, TEXT(", "), [](const FIntVector& Cell)
									{
										return Cell.ToString();
									})));
							UpdateBestRejectedCandidateReason(
								Candidate.Score,
								RejectedMessage,
								OutResult.BestRejectedCandidateScore,
								OutResult.BestRejectedCandidateReason);
							AddUniqueRejectedCandidateReason(
								RejectedMessage,
								OutResult.RejectedCandidateReasons);
							return false;
						}

						TArray<FLayoutCommittedTraversalAnchor> SelectedParentTraversalAnchors =
							Inputs.SelectedParentTraversalAnchors;
						if (bAllowsChildTraversalBridgeForCommittedContacts)
						{
							SelectedParentTraversalAnchors.Reset();
						}
						else if (SelectedParentTraversalAnchors.Num() > 1)
						{
							int32 BestTraversalAnchorIndex = 0;
							int32 BestTraversalAnchorScore = MIN_int32;
							for (int32 ContactIndex = 0; ContactIndex < Inputs.CandidateParentContactCells.Num(); ++ContactIndex)
							{
								const FIntVector& ParentContactCell = Inputs.CandidateParentContactCells[ContactIndex];
								int32 TraversalAnchorScore =
									RootConnectedCorridorCells.Contains(ParentContactCell) ? 100 : 0;
								for (const ELayoutFaceDirection Direction : LateralSupportDirections)
								{
									const FIntVector NeighborCell =
										ParentContactCell + FLayoutDirectionUtils::ToCellDelta(Direction);
									const ELayoutCellIntent* NeighborIntent =
										PrecheckParentPlannedCellIntents.Find(NeighborCell);
									if (NeighborIntent == nullptr || !IsRootConnectedCorridorIntent(*NeighborIntent))
									{
										continue;
									}

									TraversalAnchorScore += RootConnectedCorridorCells.Contains(NeighborCell) ? 25 : 5;
								}

								if (TraversalAnchorScore > BestTraversalAnchorScore)
								{
									BestTraversalAnchorScore = TraversalAnchorScore;
									BestTraversalAnchorIndex = ContactIndex;
								}
							}

							if (SelectedParentTraversalAnchors.IsValidIndex(BestTraversalAnchorIndex))
							{
								const FLayoutCommittedTraversalAnchor BestTraversalAnchor =
									SelectedParentTraversalAnchors[BestTraversalAnchorIndex];
								SelectedParentTraversalAnchors.Reset();
								SelectedParentTraversalAnchors.Add(BestTraversalAnchor);
							}
						}

						auto IsStructurallyTraversableIntent = [](const ELayoutCellIntent Intent) -> bool
						{
							switch (Intent)
							{
							case ELayoutCellIntent::Boundary:
							case ELayoutCellIntent::Entry:
							case ELayoutCellIntent::Core:
							case ELayoutCellIntent::Interior:
							case ELayoutCellIntent::Connector:
							case ELayoutCellIntent::VerticalAccess:
								return true;
							default:
								return false;
							}
						};

						auto BuildStructuralDistanceMap =
							[&PrecheckParentPlannedCellIntents, &IsStructurallyTraversableIntent](
								const TArray<FIntVector>& StartCells,
								TMap<FIntVector, int32>& OutDistances)
						{
							OutDistances.Reset();
							TArray<FIntVector> Frontier;
							Frontier.Reserve(StartCells.Num());
							for (const FIntVector& StartCell : StartCells)
							{
								const ELayoutCellIntent* StartIntent =
									PrecheckParentPlannedCellIntents.Find(StartCell);
								if (StartIntent == nullptr
									|| !IsStructurallyTraversableIntent(*StartIntent)
									|| OutDistances.Contains(StartCell))
								{
									continue;
								}

								OutDistances.Add(StartCell, 0);
								Frontier.Add(StartCell);
							}

							static const ELayoutFaceDirection ConnectivityDirections[] =
							{
								ELayoutFaceDirection::PosX,
								ELayoutFaceDirection::NegX,
								ELayoutFaceDirection::PosY,
								ELayoutFaceDirection::NegY,
								ELayoutFaceDirection::PosZ,
								ELayoutFaceDirection::NegZ
							};

							for (int32 FrontierIndex = 0; FrontierIndex < Frontier.Num(); ++FrontierIndex)
							{
								const FIntVector CurrentCell = Frontier[FrontierIndex];
								const int32 CurrentDistance = OutDistances.FindChecked(CurrentCell);
								for (const ELayoutFaceDirection Direction : ConnectivityDirections)
								{
									const FIntVector NeighborCell =
										CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
									const ELayoutCellIntent* NeighborIntent =
										PrecheckParentPlannedCellIntents.Find(NeighborCell);
									if (NeighborIntent == nullptr
										|| !IsStructurallyTraversableIntent(*NeighborIntent)
										|| OutDistances.Contains(NeighborCell))
									{
										continue;
									}

									OutDistances.Add(NeighborCell, CurrentDistance + 1);
									Frontier.Add(NeighborCell);
								}
							}
						};

						if (!SelectedParentTraversalAnchors.IsEmpty())
						{
							TArray<FIntVector> RootEntryCells;
							for (const TPair<FIntVector, ELayoutCellIntent>& Pair : PrecheckParentPlannedCellIntents)
							{
								if (Pair.Value == ELayoutCellIntent::Entry)
								{
									RootEntryCells.Add(Pair.Key);
								}
							}

							TMap<FIntVector, int32> RootDistanceByCell;
							BuildStructuralDistanceMap(RootEntryCells, RootDistanceByCell);

							TMap<FIntVector, int32> ParentProviderDistanceByCell;
							BuildStructuralDistanceMap(
								ProspectiveVerticalAccessOwnership.CountedParentVerticalAccessCells,
								ParentProviderDistanceByCell);

							int32 BestRootDistance = MAX_int32;
							int32 BestProviderDistance = MAX_int32;
							for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : SelectedParentTraversalAnchors)
							{
								if (const int32* RootDistance = RootDistanceByCell.Find(TraversalAnchor.Cell))
								{
									BestRootDistance = FMath::Min(BestRootDistance, *RootDistance);
								}
								if (const int32* ProviderDistance = ParentProviderDistanceByCell.Find(TraversalAnchor.Cell))
								{
									BestProviderDistance = FMath::Min(BestProviderDistance, *ProviderDistance);
								}
							}

							if (BestRootDistance != MAX_int32)
							{
								const int32 RootDistancePenalty =
									BestRootDistance * 45;
								OutScoreAdjustment -= RootDistancePenalty;
								if (bTrackCandidateScoreBreakdown)
								{
									Candidate.ScoreBreakdown.RootDistancePenalty =
										-RootDistancePenalty;
									Candidate.ScoreBreakdown.AdjustmentTotal -=
										RootDistancePenalty;
								}
							}
							if (BestProviderDistance != MAX_int32)
							{
								const int32 ProviderDistancePenalty =
									BestProviderDistance * 30;
								OutScoreAdjustment -= ProviderDistancePenalty;
								if (bTrackCandidateScoreBreakdown)
								{
									Candidate.ScoreBreakdown.ProviderDistancePenalty =
										-ProviderDistancePenalty;
									Candidate.ScoreBreakdown.AdjustmentTotal -=
										ProviderDistancePenalty;
								}
							}
						}
					}

					FString ProtectedParentEntryFailureReason;
					if (!DoesChildPlacementRespectProtectedParentEntryNeighbors(
						PlanningVariant.PlannedCells,
						PlanningVariant.PlannedCellSet,
						CandidateOffset,
						ParentPlannedCellIntents,
						Inputs.DirectParentContactCommitments,
						&ProtectedParentEntryFailureReason))
					{
						++OutResult.ProbeRejectCount;
						FParentProbeEvaluation ParentProbeEvaluation;
						ParentProbeEvaluation.FailureReason = ProtectedParentEntryFailureReason;
						const FString RejectedMessage = BuildRejectedChildOffsetMessage(
							CandidateOffset,
							Inputs.CandidateParentContactCells,
							ParentProbeEvaluation);
						UpdateBestRejectedCandidateReason(
							Candidate.Score,
							RejectedMessage,
							OutResult.BestRejectedCandidateScore,
							OutResult.BestRejectedCandidateReason);
						AddUniqueRejectedCandidateReason(
							RejectedMessage,
							OutResult.RejectedCandidateReasons);
						return false;
					}

					const bool bRunImmediateFullParentProof =
						bForceFullParentProbeForResolvedCombinationSelection;
					const bool bDeferFullParentProofToCompleteValidationForCandidate =
						bDeferFullParentProofToCompleteValidation
						&& !bRunImmediateFullParentProof;

					FParentProbeEvaluation ParentProbeEvaluation;
					FPreparedChildParentProofCandidate PreparedParentProofCandidate;
					const bool bRequireStructuralFeasibilityPrecheck =
						bRequireStructuralFeasibilityForTrackedResolvedCombinations
						&& !bRunImmediateFullParentProof;
					++InOutPerfMetrics.ParentResidualPlanReuseCount;
					const bool bParentProbeSucceeded = EvaluateParentProbeForChildCandidate(
						RootRequest,
						ParentPlannedCells,
						ParentFootprintSize,
						ParentPlannedCellSet,
						ParentPlannedCellIntents,
						ParentProtectedTraversalCells,
						ParentVerticalAccessIngressDirections,
						SchedulerStateKey,
						VariantIndex,
						Demand.ChildRegionDebugPath,
						PlanningVariant,
						PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess,
						CurrentPlacements,
						CurrentReservedParentCells,
						InOutParentStructuralFeasibilityMemo,
						InOutParentProbeEvaluationMemo,
						SharedParentProbeSolveMemo,
						InOutPerfMetrics,
						CandidateOffset,
						Inputs.CandidateCommitments,
						Inputs.DirectParentContactCommitments,
						Inputs.CandidateParentContactCells,
						Inputs.SelectedParentTraversalAnchors,
						Inputs.bAllowsChildTraversalBridgeForCommittedContacts,
						CandidateParentPlanReservedCells,
						Inputs.EffectiveSharedParentChildFaces,
						NormalizedCandidate.BoundaryCertificateId,
						NormalizedCandidate.ParentDomainRestrictions,
						Inputs.bHasSiblingOnlyNoParentEntry,
						bRequireStructuralFeasibilityPrecheck,
						bRunImmediateFullParentProof,
						bDeferFullParentProofToCompleteValidationForCandidate,
						ParentProbeEvaluation,
						&PreparedParentProofCandidate,
						&PreparedResidualPlan);
					OutRejectedByStructuralPrecheck =
						!bParentProbeSucceeded && ParentProbeEvaluation.bFailedStructuralFeasibility;
					OutReachedFullParentProof = ParentProbeEvaluation.bRanImmediateFullParentProof;
					OutSurvivedFullParentProof =
						ParentProbeEvaluation.bRanImmediateFullParentProof && bParentProbeSucceeded;
					if (!bParentProbeSucceeded)
					{
						if (Inputs.bCanDeferRecursiveParentProbeFailure
							&& !ParentProbeEvaluation.bFailedDuringChildProbe)
						{
							ParentProbeEvaluation.bSucceeded = true;
							ParentProbeEvaluation.ScoreAdjustment = 0;
						}
						else
						{
							++OutResult.ProbeRejectCount;
							const FString RejectedMessage = BuildRejectedChildOffsetMessage(
								CandidateOffset,
								Inputs.CandidateParentContactCells,
								ParentProbeEvaluation);
							UpdateBestRejectedCandidateReason(
								Candidate.Score,
								RejectedMessage,
								OutResult.BestRejectedCandidateScore,
								OutResult.BestRejectedCandidateReason);
							AddUniqueRejectedCandidateReason(
								RejectedMessage,
								OutResult.RejectedCandidateReasons);
							return false;
						}
					}

					PreparedParentProofCandidate.bCanDeferRecursiveParentProbeFailure =
						Inputs.bCanDeferRecursiveParentProbeFailure;
					PreparedParentProofCandidate.bSiblingOnlyNoContactAttachment =
						Inputs.CandidateCommitments.IsEmpty()
						&& Inputs.CandidateParentContactCells.IsEmpty()
						&& !CurrentPlacements.IsEmpty();
					PreparedParentProofCandidate.NegotiatedHostVerticalAccessResponsibility =
						NegotiatedVerticalAccessResponsibility;
					PreparedParentProofCandidate.bHasRequiredHostIngressAnchor =
						Candidate.bHasRequiredHostIngressAnchor;
					PreparedParentProofCandidate.RequiredHostIngressAnchor =
						Candidate.RequiredHostIngressAnchor;
					PreparedParentProofCandidate.bHasRequiredHostEgressAnchor =
						Candidate.bHasRequiredHostEgressAnchor;
					PreparedParentProofCandidate.RequiredHostEgressAnchor =
						Candidate.RequiredHostEgressAnchor;
					PreparedParentProofCandidate.RequiredChildInternalVerticalRouteCells =
						Candidate.RequiredChildInternalVerticalRouteCells;
					++InOutPerfMetrics.PreparedParentProofCandidateStoreCount;
					PreparedParentProofCandidatesBySignature.FindOrAdd(
						Inputs.CommitmentCombinationSignature) = PreparedParentProofCandidate;

					ApplyAcceptedPreparedParentProofCandidate(
						Inputs.CandidateCommitments,
						Inputs.CandidateParentContactCells,
						PreparedParentProofCandidate,
						ParentProbeEvaluation,
						OutScoreAdjustment,
						OutBoundaryPoints);
					return true;
				};

				const TFunction<bool(const TArray<FLayoutCommittedEndpointAnchor>&, const TArray<FIntVector>&, const TArray<FResolvedChildEntryCommitmentCandidate>&, int32&, TArray<FLayoutSolveBoundaryPoint>&)> EvaluateCommitmentCombination =
					[&](
						const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
						const TArray<FIntVector>& CandidateParentContactCells,
						const TArray<FResolvedChildEntryCommitmentCandidate>& NoParentResolutionEntries,
						int32& OutScoreAdjustment,
						TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints)
					{
						TArray<FSharedParentChildFace> EffectiveSharedParentChildFaces = CandidateSharedParentChildFaces;
						TSet<FLayoutId> NoParentResolutionCommitmentIds;
						for (const FResolvedChildEntryCommitmentCandidate& NoParentResolutionEntry : NoParentResolutionEntries)
						{
							NoParentResolutionCommitmentIds.Add(NoParentResolutionEntry.Anchor.CommitmentId);
							if (NoParentResolutionEntry.ResolutionMode
								!= FResolvedChildEntryCommitmentCandidate::EResolutionMode::SiblingSharedDoor)
							{
								continue;
							}

							EffectiveSharedParentChildFaces.RemoveAll(
								[&](const FSharedParentChildFace& SharedFace)
								{
									return SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
										&& SharedFace.ChildLocalCell == NoParentResolutionEntry.ChildLocalCell
										&& SharedFace.FaceDirection == NoParentResolutionEntry.FaceDirection;
								});
						}
						TArray<FLayoutCommittedEndpointAnchor> DirectParentContactCommitments;
						DirectParentContactCommitments.Reserve(CandidateParentContactCells.Num());
						for (const FLayoutCommittedEndpointAnchor& CandidateCommitment : CandidateCommitments)
						{
							if (!NoParentResolutionCommitmentIds.Contains(CandidateCommitment.CommitmentId))
							{
								DirectParentContactCommitments.Add(CandidateCommitment);
							}
						}
						TArray<FLayoutCommittedTraversalAnchor> SelectedParentTraversalAnchors =
							BuildCommittedTraversalAnchorsForParentContacts(
								DirectParentContactCommitments,
								CandidateParentContactCells);
						const bool bTrackDetailedRecursivePerf =
							ShouldTrackDetailedRecursiveSchedulerPerf();
						FRecursiveSchedulerCombinationFamilyStats* CombinationFamilyStats = nullptr;
						if (bTrackDetailedRecursivePerf)
						{
							int32 SiblingSharedDoorCount = 0;
							int32 ParentSharedDoorCount = 0;
							for (const FResolvedChildEntryCommitmentCandidate& NoParentResolutionEntry :
								NoParentResolutionEntries)
							{
								if (NoParentResolutionEntry.ResolutionMode
									== FResolvedChildEntryCommitmentCandidate::EResolutionMode::SiblingSharedDoor)
								{
									++SiblingSharedDoorCount;
								}
								else if (NoParentResolutionEntry.ResolutionMode
									== FResolvedChildEntryCommitmentCandidate::EResolutionMode::ParentSharedDoor)
								{
									++ParentSharedDoorCount;
								}
							}

							const FRecursiveSchedulerCombinationFamilyKey CombinationFamilyKey =
								BuildRecursiveSchedulerCombinationFamilyKey(
									CandidateCommitments.Num(),
									CandidateParentContactCells.Num(),
									DirectParentContactCommitments.Num(),
									SiblingSharedDoorCount,
									ParentSharedDoorCount);
							CombinationFamilyStats =
								&FindOrAddRecursiveSchedulerCombinationFamilyStats(
									InOutPerfMetrics,
									CombinationFamilyKey,
									CandidateCommitments.Num(),
									CandidateParentContactCells.Num(),
									DirectParentContactCommitments.Num(),
									SiblingSharedDoorCount,
									ParentSharedDoorCount);
							++InOutPerfMetrics.EnumeratedCommitmentCombinationCount;
							++CombinationFamilyStats->EnumeratedCount;
						}

						const bool bSiblingOnlyNoContactAttachment =
							CandidateCommitments.IsEmpty()
							&& CandidateParentContactCells.IsEmpty()
							&& !CurrentPlacements.IsEmpty();
						const bool bHasSiblingOnlyNoParentEntry =
							NoParentResolutionEntries.ContainsByPredicate(
								[](const FResolvedChildEntryCommitmentCandidate& Entry)
								{
									return Entry.ResolutionMode
										== FResolvedChildEntryCommitmentCandidate::EResolutionMode::SiblingSharedDoor;
								});
						const bool bCanDeferRecursiveParentProbeFailure =
							!bPartialPlacementStep
							&& CandidateParentContactCells.IsEmpty()
							&& !CurrentPlacements.IsEmpty()
							&& (CandidateCommitments.IsEmpty() || bHasSiblingOnlyNoParentEntry);
						if (bSiblingOnlyNoContactAttachment)
						{
							EffectiveSharedParentChildFaces.RemoveAll(
								[](const FSharedParentChildFace& SharedFace)
								{
									return SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor;
								});
						}
						const FSuccessfulCommitmentCombinationSignature CommitmentCombinationSignature =
							BuildSuccessfulCommitmentCombinationSignature(
								CandidateCommitments,
								CandidateParentContactCells,
								NoParentResolutionEntries);
						if (bForceFullParentProbeForResolvedCombinationSelection)
						{
							if (const FPreparedChildParentProofCandidate* ExistingPreparedParentProofCandidate =
								PreparedParentProofCandidatesBySignature.Find(CommitmentCombinationSignature))
							{
								++InOutPerfMetrics.PreparedParentProofCandidateReuseCount;
								FPreparedChildParentProofCandidate PreparedParentProofCandidate =
									*ExistingPreparedParentProofCandidate;
								FParentProbeEvaluation ParentProbeEvaluation;
								if (!EvaluatePreparedParentProbeForChildCandidate(
									RootRequest,
									ParentPlannedCells,
									ParentFootprintSize,
									ParentPlannedCellSet,
									ParentPlannedCellIntents,
									ParentProtectedTraversalCells,
									ParentVerticalAccessIngressDirections,
									SchedulerStateKey,
									VariantIndex,
									Demand.ChildRegionDebugPath,
									PlanningVariant,
									PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess,
									CurrentPlacements,
									CurrentReservedParentCells,
									InOutParentStructuralFeasibilityMemo,
									InOutParentProbeEvaluationMemo,
									SharedParentProbeSolveMemo,
									InOutPerfMetrics,
									CandidateOffset,
									CandidateCommitments,
									PreparedParentProofCandidate.DirectParentContactCommitments,
									CandidateParentContactCells,
									PreparedParentProofCandidate.PreparedParentProbeCandidate.Candidate.CandidateParentTraversalAnchors,
									PreparedParentProofCandidate.bAllowsChildTraversalBridgeForCommittedContacts,
									CandidateParentPlanReservedCells,
									PreparedParentProofCandidate.EffectiveSharedParentChildFaces,
									NormalizedCandidate.BoundaryCertificateId,
									NormalizedCandidate.ParentDomainRestrictions,
									bHasSiblingOnlyNoParentEntry,
									false,
									true,
									false,
									PreparedParentProofCandidate,
									ParentProbeEvaluation))
								{
									if (bTrackDetailedRecursivePerf
										&& ParentProbeEvaluation.bFailedStructuralFeasibility)
									{
										++InOutPerfMetrics.StructuralRejectedCommitmentCombinationCount;
										++CombinationFamilyStats->StructuralRejectCount;
									}
									if (bTrackDetailedRecursivePerf
										&& ParentProbeEvaluation.bRanImmediateFullParentProof)
									{
										++InOutPerfMetrics.FullProofReachedCommitmentCombinationCount;
										++CombinationFamilyStats->FullProofAttemptCount;
										FRecursiveSchedulerProofOffsetStats& OffsetStats =
											InOutPerfMetrics.ProofAttemptOffsetStats.FindOrAdd(CandidateOffset);
										++OffsetStats.FullProofAttemptCount;
									}
									if (PreparedParentProofCandidate.bCanDeferRecursiveParentProbeFailure
										&& !ParentProbeEvaluation.bFailedDuringChildProbe)
									{
										ParentProbeEvaluation.bSucceeded = true;
										ParentProbeEvaluation.ScoreAdjustment = 0;
									}
									else
									{
										++OutResult.ProbeRejectCount;
										const FString RejectedMessage = BuildRejectedChildOffsetMessage(
											CandidateOffset,
											CandidateParentContactCells,
											ParentProbeEvaluation);
										UpdateBestRejectedCandidateReason(
											Candidate.Score,
											RejectedMessage,
											OutResult.BestRejectedCandidateScore,
											OutResult.BestRejectedCandidateReason);
										AddUniqueRejectedCandidateReason(
											RejectedMessage,
											OutResult.RejectedCandidateReasons);
										return false;
									}
								}
								else if (bTrackDetailedRecursivePerf
									&& ParentProbeEvaluation.bRanImmediateFullParentProof)
								{
									++InOutPerfMetrics.FullProofReachedCommitmentCombinationCount;
									++CombinationFamilyStats->FullProofAttemptCount;
									FRecursiveSchedulerProofOffsetStats& OffsetStats =
										InOutPerfMetrics.ProofAttemptOffsetStats.FindOrAdd(CandidateOffset);
									++OffsetStats.FullProofAttemptCount;
									if (ParentProbeEvaluation.bSucceeded)
									{
										++InOutPerfMetrics.FullProofSurvivedCommitmentCombinationCount;
										++CombinationFamilyStats->FullProofSuccessCount;
										++OffsetStats.FullProofSuccessCount;
									}
								}

								ApplyAcceptedPreparedParentProofCandidate(
									CandidateCommitments,
									CandidateParentContactCells,
									PreparedParentProofCandidate,
									ParentProbeEvaluation,
									OutScoreAdjustment,
									OutBoundaryPoints);
								return true;
							}
						}

						const bool bParentStillOwnsHostVerticalAccess =
							GetRequiredHostVerticalAccessProviderCount(RootRequest, ParentPlannedCells) > 0;
						const bool bAllowsChildTraversalBridgeForCommittedContacts =
							!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
							&& PlanningVariant.MaxLocalLevel == 0
							&& CandidateParentContactCells.Num() > 1
							&& !bParentStillOwnsHostVerticalAccess;

						const FChildParentProofPreparationInputs PreparationInputs
						{
							CandidateCommitments,
							CandidateParentContactCells,
							DirectParentContactCommitments,
							EffectiveSharedParentChildFaces,
							SelectedParentTraversalAnchors,
							CommitmentCombinationSignature,
							bHasSiblingOnlyNoParentEntry,
							bCanDeferRecursiveParentProbeFailure,
							bAllowsChildTraversalBridgeForCommittedContacts
						};
						bool bRejectedByStructuralPrecheck = false;
						bool bReachedFullParentProof = false;
						bool bSurvivedFullParentProof = false;
						const bool bPreparedCandidateSucceeded = TryPrepareChildParentProofCandidate(
							PreparationInputs,
							OutScoreAdjustment,
							OutBoundaryPoints,
							bRejectedByStructuralPrecheck,
							bReachedFullParentProof,
							bSurvivedFullParentProof);
						if (bTrackDetailedRecursivePerf && bRejectedByStructuralPrecheck)
						{
							++InOutPerfMetrics.StructuralRejectedCommitmentCombinationCount;
							++CombinationFamilyStats->StructuralRejectCount;
						}
						if (bTrackDetailedRecursivePerf && bReachedFullParentProof)
						{
							++InOutPerfMetrics.FullProofReachedCommitmentCombinationCount;
							++CombinationFamilyStats->FullProofAttemptCount;
							FRecursiveSchedulerProofOffsetStats& OffsetStats =
								InOutPerfMetrics.ProofAttemptOffsetStats.FindOrAdd(CandidateOffset);
							++OffsetStats.FullProofAttemptCount;
							if (bSurvivedFullParentProof)
							{
								++InOutPerfMetrics.FullProofSurvivedCommitmentCombinationCount;
								++CombinationFamilyStats->FullProofSuccessCount;
								++OffsetStats.FullProofSuccessCount;
							}
						}
						if (bPreparedCandidateSucceeded && bHasSiblingOnlyNoParentEntry)
						{
							// An exact sibling Door contributes a cross-region traversal edge;
							// prefer it over an otherwise equivalent extra parent contact.
							static constexpr int32 SiblingDoorTraversalScore = 1000;
							OutScoreAdjustment += SiblingDoorTraversalScore;
							if (bTrackCandidateScoreBreakdown)
							{
								Candidate.ScoreBreakdown.SiblingSeamScore +=
									SiblingDoorTraversalScore;
								Candidate.ScoreBreakdown.AdjustmentTotal +=
									SiblingDoorTraversalScore;
							}
						}
						return bPreparedCandidateSucceeded;
					};

				const TFunction<FResolvedChildNoParentEntryOption(const FIntVector&, const FLayoutChildCapabilityEndpoint&)> CanResolveEntryWithoutDirectParentContact =
					[&](const FIntVector& CandidateEntryCell, const FLayoutChildCapabilityEndpoint& Capability)
					-> FResolvedChildNoParentEntryOption
				{
					return ResolveChildEntryWithoutDirectParentContact(
						CandidateEntryCell,
						Capability,
						CandidateOffset,
						NormalizedCandidate.StageMapping,
						PlanningVariant,
						Demand,
						CurrentPlacements);
				};

				FString CommitmentFailureReason;
				const double EntryNegotiationStartSeconds = FPlatformTime::Seconds();
				++InOutPerfMetrics.EntryNegotiationCalls;
				TArray<TArray<FResolvedChildEntryCommitmentCandidate>> CandidateOptionsByEntry;
				const FString EntryOptionMemoKey = BuildChildEntryCommitmentOptionMemoKey(
					SchedulerStateKey,
					PreparedSource.EntrySnapshot->EntryId,
					VariantIndex,
					CandidateOffset);
				const double EntryOptionCollectionStartSeconds = FPlatformTime::Seconds();
				++InOutPerfMetrics.EntryOptionCollectionCalls;
				if (!CollectChildEntryCommitmentOptionsForOffsetCached(
					EntryOptionMemoKey,
					*PreparedSource.EntrySnapshot,
					PlanningVariant.InterfaceSummary,
					NormalizedCandidate,
					Demand.ChildRegionDebugPath,
					ParentPlannedCellSet,
					CurrentReservedParentCells,
					ParentProtectedTraversalCells,
					ParentPriorityTargets,
					CanResolveEntryWithoutDirectParentContact,
					InOutEntryCommitmentOptionMemo,
					CandidateOptionsByEntry,
					&CommitmentFailureReason))
				{
					InOutPerfMetrics.EntryOptionCollectionSeconds += (FPlatformTime::Seconds() - EntryOptionCollectionStartSeconds);
					InOutPerfMetrics.EntryNegotiationSeconds += (FPlatformTime::Seconds() - EntryNegotiationStartSeconds);
					RecordFailedChildEntryNegotiationForCandidate(
						CandidateOffset,
						CommitmentFailureReason,
						OutResult);
					continue;
				}
				InOutPerfMetrics.EntryOptionCollectionSeconds += (FPlatformTime::Seconds() - EntryOptionCollectionStartSeconds);
				const double EntryCommitmentResolutionStartSeconds = FPlatformTime::Seconds();
				++InOutPerfMetrics.EntryCommitmentResolutionCalls;
				TArray<FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination> SuccessfulCommitmentCombinations;
				const bool bHasAlternativeEntryContracts =
					CandidateOptionsByEntry.ContainsByPredicate(
						[](const TArray<FResolvedChildEntryCommitmentCandidate>& Options)
						{
							return Options.Num() > 1;
						});
				const bool bMayNeedResolvedCombinationConfirmation =
					!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
					&& PlanningVariant.MaxLocalLevel == 0
					&& (CandidateOptionsByEntry.Num() > 1
						|| (bHasAlternativeEntryContracts
							&& (bHasRemainingPeerDemand || !CurrentPlacements.IsEmpty())));
				bRequireStructuralFeasibilityForTrackedResolvedCombinations =
					bMayNeedResolvedCombinationConfirmation;
				// Multi-entry confirmation previously resolved the same commitment space twice
				// before the forced parent-proof sweep. Track combinations on the first pass
				// for the clear multi-entry cases so the confirmation loop can reuse that
				// result set instead of re-running a second non-forced resolve.
				FResolvedCommitmentScoreBreakdown ResolvedCommitmentScoreBreakdown;
				if (!ResolveChildEntryCommitmentOptions(
					*PreparedSource.EntrySnapshot,
					CandidateOptionsByEntry,
					EvaluateCommitmentCombination,
					Candidate.Commitments,
					Candidate.ParentContactCells,
					Candidate.ParentTranslatedBoundaryPoints,
					Candidate.Score,
					ShouldTrackDetailedRecursiveSchedulerPerf()
						? &ResolvedCommitmentScoreBreakdown
						: nullptr,
					bMayNeedResolvedCombinationConfirmation
						? &SuccessfulCommitmentCombinations
						: nullptr,
					&CommitmentFailureReason,
					ShouldTrackDetailedRecursiveSchedulerPerf() ? &InOutPerfMetrics : nullptr))
				{
					InOutPerfMetrics.EntryCommitmentResolutionSeconds += (FPlatformTime::Seconds() - EntryCommitmentResolutionStartSeconds);
					InOutPerfMetrics.EntryNegotiationSeconds += (FPlatformTime::Seconds() - EntryNegotiationStartSeconds);
					RecordFailedChildEntryNegotiationForCandidate(
						CandidateOffset,
						CommitmentFailureReason,
						OutResult);
					continue;
				}
				InOutPerfMetrics.SuccessfulCommitmentCombinationCount +=
					SuccessfulCommitmentCombinations.Num();
					if (ResolvedCommitmentScoreBreakdown.bHasDiagnostics)
					{
						Candidate.ScoreBreakdown.bHasDiagnostics = true;
						Candidate.ScoreBreakdown.PreAdjustmentEntryAccumulatedScore =
							ResolvedCommitmentScoreBreakdown.EntryAccumulatedScore;
						Candidate.ScoreBreakdown.PreAdjustmentResolveAdjustment =
							ResolvedCommitmentScoreBreakdown.ResolveAdjustmentTotal;
						Candidate.ScoreBreakdown.DirectParentSupportSaturationPenalty =
							ResolvedCommitmentScoreBreakdown.DirectParentSupportSaturationPenalty;
						Candidate.ScoreBreakdown.EntryIntentScore =
							ResolvedCommitmentScoreBreakdown.EntryIntentScore;
					Candidate.ScoreBreakdown.EntryParentCapabilitySupportContribution =
						ResolvedCommitmentScoreBreakdown.EntryParentCapabilitySupportContribution;
					Candidate.ScoreBreakdown.EntryParentSupportContribution =
						ResolvedCommitmentScoreBreakdown.EntryParentSupportContribution;
					Candidate.ScoreBreakdown.EntryProtectedTraversalPenalty =
						ResolvedCommitmentScoreBreakdown.EntryProtectedTraversalPenalty;
					Candidate.ScoreBreakdown.EntryAnchoredIntentPenalty =
						ResolvedCommitmentScoreBreakdown.EntryAnchoredIntentPenalty;
					Candidate.ScoreBreakdown.EntryPriorityDistancePenalty =
						ResolvedCommitmentScoreBreakdown.EntryPriorityDistancePenalty;
					Candidate.ScoreBreakdown.EntryNoParentResolutionScore =
						ResolvedCommitmentScoreBreakdown.EntryNoParentResolutionScore;
					Candidate.ScoreBreakdown.DirectParentEntryCount =
						ResolvedCommitmentScoreBreakdown.DirectParentEntryCount;
					Candidate.ScoreBreakdown.NoParentEntryCount =
						ResolvedCommitmentScoreBreakdown.NoParentEntryCount;
				}

				const bool bNeedsResolvedCombinationConfirmation =
					bMayNeedResolvedCombinationConfirmation
					&& SuccessfulCommitmentCombinations.Num() > 1;
				if (bNeedsResolvedCombinationConfirmation)
				{
					const FScoredChildOffset CandidateBeforeConfirmation = Candidate;
					TArray<FScoredChildOffset> ConfirmedCandidates;
					static constexpr int32 MaxResolvedCombinationConfirmations = 24;
					const int32 ConfirmationCount = FMath::Min(
						MaxResolvedCombinationConfirmations,
						SuccessfulCommitmentCombinations.Num());
					bForceFullParentProbeForResolvedCombinationSelection =
						CandidateOptionsByEntry.Num() > 1;
					for (int32 CombinationIndex = 0;
						CombinationIndex < ConfirmationCount;
						++CombinationIndex)
					{
						Candidate = CandidateBeforeConfirmation;
						const FChildEntryCommitmentResolutionState::FSuccessfulCommitmentCombination& Combination =
							SuccessfulCommitmentCombinations[CombinationIndex];
						int32 ConfirmedScoreAdjustment = 0;
						TArray<FLayoutSolveBoundaryPoint> ConfirmedBoundaryPoints;
						if (!EvaluateCommitmentCombination(
								Combination.Commitments,
								Combination.ParentContactCells,
								Combination.NoParentResolutionEntries,
								ConfirmedScoreAdjustment,
								ConfirmedBoundaryPoints))
						{
							continue;
						}

						Candidate.Commitments = Combination.Commitments;
						Candidate.ParentContactCells = Combination.ParentContactCells;
						Candidate.ParentTranslatedBoundaryPoints = MoveTemp(ConfirmedBoundaryPoints);
						Candidate.Score = Combination.TotalScore + ConfirmedScoreAdjustment;
						if (Combination.ScoreBreakdown.bHasDiagnostics)
						{
							Candidate.ScoreBreakdown.bHasDiagnostics = true;
							Candidate.ScoreBreakdown.PreAdjustmentEntryAccumulatedScore = Combination.ScoreBreakdown.EntryAccumulatedScore;
							Candidate.ScoreBreakdown.PreAdjustmentResolveAdjustment = Combination.ScoreBreakdown.ResolveAdjustmentTotal;
							Candidate.ScoreBreakdown.DirectParentSupportSaturationPenalty = Combination.ScoreBreakdown.DirectParentSupportSaturationPenalty;
							Candidate.ScoreBreakdown.EntryIntentScore = Combination.ScoreBreakdown.EntryIntentScore;
							Candidate.ScoreBreakdown.EntryParentCapabilitySupportContribution = Combination.ScoreBreakdown.EntryParentCapabilitySupportContribution;
							Candidate.ScoreBreakdown.EntryParentSupportContribution = Combination.ScoreBreakdown.EntryParentSupportContribution;
							Candidate.ScoreBreakdown.EntryProtectedTraversalPenalty = Combination.ScoreBreakdown.EntryProtectedTraversalPenalty;
							Candidate.ScoreBreakdown.EntryAnchoredIntentPenalty = Combination.ScoreBreakdown.EntryAnchoredIntentPenalty;
							Candidate.ScoreBreakdown.EntryPriorityDistancePenalty = Combination.ScoreBreakdown.EntryPriorityDistancePenalty;
							Candidate.ScoreBreakdown.EntryNoParentResolutionScore = Combination.ScoreBreakdown.EntryNoParentResolutionScore;
							Candidate.ScoreBreakdown.DirectParentEntryCount = Combination.ScoreBreakdown.DirectParentEntryCount;
							Candidate.ScoreBreakdown.NoParentEntryCount = Combination.ScoreBreakdown.NoParentEntryCount;
						}

						TArray<FIntVector> CombinationVerticalRouteCells;
						FString CombinationRouteFailure;
						if (!TryPromoteChildStageMappingForCommittedRoute(
								Candidate.StageMapping,
								Candidate.Commitments,
								PlanningVariant.PlannedCells,
								PlanningVariant.InterfaceSummary.VerticalAccessCells,
								CombinationVerticalRouteCells,
								CombinationRouteFailure))
						{
							continue;
						}
						if (Candidate.RequiredChildInternalVerticalRouteCells.IsEmpty())
						{
							Candidate.RequiredChildInternalVerticalRouteCells =
								MoveTemp(CombinationVerticalRouteCells);
						}
						FinalizeCandidateScoreBreakdown();
						ConfirmedCandidates.Add(Candidate);
					}
					bForceFullParentProbeForResolvedCombinationSelection = false;

					if (ConfirmedCandidates.IsEmpty())
					{
						InOutPerfMetrics.EntryCommitmentResolutionSeconds += (FPlatformTime::Seconds() - EntryCommitmentResolutionStartSeconds);
						InOutPerfMetrics.EntryNegotiationSeconds += (FPlatformTime::Seconds() - EntryNegotiationStartSeconds);
						RecordFailedChildEntryNegotiationForCandidate(
							CandidateOffset,
							CommitmentFailureReason.IsEmpty()
								? TEXT("No structurally distinct child Entry contract survived bounded confirmation.")
								: CommitmentFailureReason,
							OutResult);
						continue;
					}

					InOutPerfMetrics.EntryCommitmentResolutionSeconds += (FPlatformTime::Seconds() - EntryCommitmentResolutionStartSeconds);
					InOutPerfMetrics.EntryNegotiationSeconds += (FPlatformTime::Seconds() - EntryNegotiationStartSeconds);
					OutResult.SuccessfulCandidates.Append(MoveTemp(ConfirmedCandidates));
					continue;
				}
				TArray<FIntVector> CrossStageVerticalRouteCells;
				FString CrossStageFailureReason;
				if (!TryPromoteChildStageMappingForCommittedRoute(
					Candidate.StageMapping,
					Candidate.Commitments,
					PlanningVariant.PlannedCells,
					PlanningVariant.InterfaceSummary.VerticalAccessCells,
					CrossStageVerticalRouteCells,
					CrossStageFailureReason))
				{
					InOutPerfMetrics.EntryCommitmentResolutionSeconds +=
						(FPlatformTime::Seconds() - EntryCommitmentResolutionStartSeconds);
					InOutPerfMetrics.EntryNegotiationSeconds +=
						(FPlatformTime::Seconds() - EntryNegotiationStartSeconds);
					RecordFailedChildEntryNegotiationForCandidate(
						CandidateOffset,
						CrossStageFailureReason,
						OutResult);
					continue;
				}
				if (Candidate.RequiredChildInternalVerticalRouteCells.IsEmpty())
				{
					Candidate.RequiredChildInternalVerticalRouteCells =
						MoveTemp(CrossStageVerticalRouteCells);
				}

				FinalizeCandidateScoreBreakdown();
				InOutPerfMetrics.EntryCommitmentResolutionSeconds += (FPlatformTime::Seconds() - EntryCommitmentResolutionStartSeconds);
				InOutPerfMetrics.EntryNegotiationSeconds += (FPlatformTime::Seconds() - EntryNegotiationStartSeconds);

				OutResult.SuccessfulCandidates.Add(MoveTemp(Candidate));
			}
		}

		if (!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess)
		{
			const bool bShouldPreferTrueInteriorCandidates =
				OutResult.SuccessfulCandidates.ContainsByPredicate(
					[](const FScoredChildOffset& Candidate)
					{
						return !Candidate.bTouchesParentOuterShell;
					});
			if (bShouldPreferTrueInteriorCandidates)
			{
				OutResult.SuccessfulCandidates.RemoveAll(
					[](const FScoredChildOffset& Candidate)
					{
						return Candidate.bTouchesParentOuterShell;
					});
			}
		}

		SortAndCapSuccessfulChildPlacementCandidates(
			OutResult.SuccessfulCandidates,
			Demand.bOptional && !Demand.bSharedFeatureChoiceSlot,
			bPartialPlacementStep,
			PlacementDemands.Num(),
			&InOutPerfMetrics);
	}

	void RecordChildPlacementSearchFailure(
		const FLayoutRegionSolveRequest& RootRequest,
		const FPreparedChildPlacementSource& PreparedSource,
		const FChildPlacementDemand& Demand,
		const FName Phase,
		const FString& Reason,
		FRecursiveChildPlacementSchedulerState& InOutSchedulerState,
		const ELayoutSolvePreparationFailureKind PreparationFailureKind = ELayoutSolvePreparationFailureKind::None,
		const int32 CandidateAttemptCount = 0)
	{
		FLayoutRegionalFailureRecord Record;
		Record.Scope = ELayoutRegionalFailureScope::Child;
		Record.Phase = Phase;
		Record.ParentRegionDebugPath = RootRequest.RegionDebugPath;
		Record.RegionDebugPath = Demand.ChildRegionDebugPath;
		Record.SourceContentEntryId = PreparedSource.EntrySnapshot != nullptr
			? PreparedSource.EntrySnapshot->EntryId
			: NAME_None;
		Record.PreparationFailureKind = PreparationFailureKind;
		Record.CandidateAttemptCount = CandidateAttemptCount;
		Record.FirstCause = Reason;
		if (!InOutSchedulerState.FirstCausalFailure.IsSet())
		{
			InOutSchedulerState.FirstCausalFailure = MoveTemp(Record);
		}
		else if (!InOutSchedulerState.BestDownstreamFailure.IsSet()
			&& Reason != InOutSchedulerState.FirstCausalFailure.FirstCause)
		{
			InOutSchedulerState.BestDownstreamFailure = MoveTemp(Record);
		}
	}

	bool HandleNoSuccessfulChildPlacementCandidates(
		const FPreparedChildPlacementSource& PreparedSource,
		const FChildPlacementDemand& Demand,
		const FString& SchedulerStateKey,
		const int32 CurrentScore,
		const FChildPlacementCandidateBuildResult& CandidateBuildResult,
		FRecursivePlacementDemandSearcher& Searcher,
		const int32 DemandIndex,
		FString& OutSearchFailureReason)
	{
		FRecursiveChildPlacementSchedulerState& SchedulerState = Searcher.SchedulerState;
		const FString RejectedSummary = CandidateBuildResult.RejectedCandidateReasons.IsEmpty()
			? (CandidateBuildResult.BestRejectedCandidateReason.IsEmpty()
				? TEXT("Best Rejected Placement:\n  <none recorded>")
				: CandidateBuildResult.BestRejectedCandidateReason)
			: FString::Join(CandidateBuildResult.RejectedCandidateReasons, TEXT("\n\n---\n\n"));

		if (Demand.bSharedFeatureChoiceSlot && Demand.bOptional)
		{
			return Searcher.Search(
				DemandIndex + 1,
				CurrentScore,
				OutSearchFailureReason);
		}

		if (Demand.bOptional)
		{
			const int32 WarningStart = SchedulerState.CurrentWarnings.Num();
			const int32 SkippedOptionalStart = SchedulerState.CurrentSkippedOptionalPlacements.Num();
			FLayoutValidationMessage& Warning = SchedulerState.CurrentWarnings.AddDefaulted_GetRef();
			Warning.Severity = ELayoutValidationSeverity::Warning;
			Warning.Message = FString::Printf(
				TEXT("Skipped optional child entry because no compatible parent-local placement survived scheduling.\n%s\nProblem: No translated child footprint both matched the requested placement zone and survived child/parent probe validation.\n%s\nFix: Update the parent content set entry placement zone, child profile boundary/entry modules, or parent footprint so the child can occupy a compatible span."),
				*DescribeChildPlacementContext(Searcher.RootRequest, *PreparedSource.EntrySnapshot),
				*RejectedSummary);
			FSkippedOptionalChildPlacement& SkippedOptionalPlacement =
				SchedulerState.CurrentSkippedOptionalPlacements.AddDefaulted_GetRef();
			SkippedOptionalPlacement.ChildRegionDebugPath = Demand.ChildRegionDebugPath;
			SkippedOptionalPlacement.ChildRequest = PreparedSource.ChildRequestTemplate;
			SkippedOptionalPlacement.ChildRequest.RegionDebugPath = Demand.ChildRegionDebugPath;
			if (SkippedOptionalPlacement.ChildRequest.SourceParentRegionDebugPath.IsEmpty())
			{
				SkippedOptionalPlacement.ChildRequest.SourceParentRegionDebugPath = Searcher.RootRequest.RegionDebugPath;
			}
			SkippedOptionalPlacement.FailureReason = Warning.Message;
			const bool bFoundDownstreamSolution =
				Searcher.Search(DemandIndex + 1, CurrentScore, OutSearchFailureReason);
			SchedulerState.CurrentWarnings.SetNum(WarningStart);
			SchedulerState.CurrentSkippedOptionalPlacements.SetNum(SkippedOptionalStart);
			return bFoundDownstreamSolution;
		}

		OutSearchFailureReason = FString::Printf(
			TEXT("Required child entry could not be placed into the parent plan.\n%s\nProblem: No mapped child value both matched the requested placement zone and level placement policy and survived child/parent probe validation.\nCandidate Summary: examined=%d successful=%d parentPlanRejected=%d stageMappingRejected=%d placementZoneRejected=%d levelPlacementRejected=%d entryNegotiationRejected=%d probeRejected=%d\n%s\nFix: Update the parent content set entry placement zone or level placement policy, child profile boundary/entry modules, or parent footprint so the child can occupy a compatible mapped span."),
			*DescribeChildPlacementContext(Searcher.RootRequest, *PreparedSource.EntrySnapshot),
			CandidateBuildResult.CandidateOffsetsExamined,
			0,
			CandidateBuildResult.ParentPlanRejectCount,
			CandidateBuildResult.StageMappingRejectCount,
			CandidateBuildResult.PlacementZoneRejectCount,
			CandidateBuildResult.LevelPlacementRejectCount,
			CandidateBuildResult.EntryNegotiationRejectCount,
			CandidateBuildResult.ProbeRejectCount,
			*RejectedSummary);
		RecordChildPlacementSearchFailure(
			Searcher.RootRequest,
			PreparedSource,
			Demand,
			TEXT("ChildPlacementDomain"),
			OutSearchFailureReason,
			SchedulerState);
		int32& FailedStateBestScore = SchedulerState.FailedSchedulerStateBestScore.FindOrAdd(SchedulerStateKey);
		FailedStateBestScore = FMath::Max(FailedStateBestScore, CurrentScore);
		return false;
	}

	bool HandlePreparedChildTopologyFailureForDemand(
		const FPreparedChildPlacementSource& PreparedSource,
		const FChildPlacementDemand& Demand,
		const int32 CurrentScore,
		FRecursivePlacementDemandSearcher& Searcher,
		const int32 DemandIndex,
		FString& OutSearchFailureReason)
	{
		FRecursiveChildPlacementSchedulerState& SchedulerState = Searcher.SchedulerState;
		if ((Demand.bSharedFeatureChoiceSlot || !Demand.bOptional)
			&& PreparedSource.PreparationFailureKind != ELayoutSolvePreparationFailureKind::None)
		{
			if (SchedulerState.PreparationFailureKind == ELayoutSolvePreparationFailureKind::None)
			{
				SchedulerState.PreparationFailureKind =
					PreparedSource.PreparationFailureKind;
			}
			SchedulerState.PreparationCandidateAttemptCount +=
				PreparedSource.PreparationCandidateAttemptCount;
		}
		if (Demand.bSharedFeatureChoiceSlot && Demand.bOptional)
		{
			return Searcher.Search(
				DemandIndex + 1,
				CurrentScore,
				OutSearchFailureReason);
		}

		if (Demand.bOptional)
		{
			const int32 WarningStart = SchedulerState.CurrentWarnings.Num();
			const int32 SkippedOptionalStart = SchedulerState.CurrentSkippedOptionalPlacements.Num();
			FLayoutValidationMessage& Warning = SchedulerState.CurrentWarnings.AddDefaulted_GetRef();
			Warning.Severity = ELayoutValidationSeverity::Warning;
			Warning.Message = FString::Printf(
				TEXT("Skipped optional child entry because child topology or boundary preparation failed.\n%s\nProblem: The child profile could not produce a usable planned-cell footprint and reciprocal boundary domain for scheduling.\nPreparation Detail:\n%s\nFix: Update the child profile or its content set so it can produce a valid prepared topology and boundary contract."),
				*DescribeChildPlacementContext(Searcher.RootRequest, *PreparedSource.EntrySnapshot),
				*IndentMultiline(
					PreparedSource.PreparationFailureReason.IsEmpty()
						? TEXT("No additional failure detail was reported.")
						: PreparedSource.PreparationFailureReason,
					TEXT("  ")));
			FSkippedOptionalChildPlacement& SkippedOptionalPlacement =
				SchedulerState.CurrentSkippedOptionalPlacements.AddDefaulted_GetRef();
			SkippedOptionalPlacement.ChildRegionDebugPath = Demand.ChildRegionDebugPath;
			SkippedOptionalPlacement.ChildRequest = PreparedSource.ChildRequestTemplate;
			SkippedOptionalPlacement.ChildRequest.RegionDebugPath = Demand.ChildRegionDebugPath;
			if (SkippedOptionalPlacement.ChildRequest.SourceParentRegionDebugPath.IsEmpty())
			{
				SkippedOptionalPlacement.ChildRequest.SourceParentRegionDebugPath = Searcher.RootRequest.RegionDebugPath;
			}
			SkippedOptionalPlacement.FailureReason = Warning.Message;
			const bool bFoundDownstreamSolution =
				Searcher.Search(DemandIndex + 1, CurrentScore, OutSearchFailureReason);
			SchedulerState.CurrentWarnings.SetNum(WarningStart);
			SchedulerState.CurrentSkippedOptionalPlacements.SetNum(SkippedOptionalStart);
			return bFoundDownstreamSolution;
		}

		OutSearchFailureReason = FString::Printf(
			TEXT("Required child entry could not be placed because child topology or boundary preparation failed.\n%s\nProblem: The child profile could not produce a usable planned-cell footprint and reciprocal boundary domain for scheduling.\nPreparation Detail:\n%s\nFix: Update the child profile or its content set so it can produce a valid prepared topology and boundary contract."),
			*DescribeChildPlacementContext(Searcher.RootRequest, *PreparedSource.EntrySnapshot),
			*IndentMultiline(
				PreparedSource.PreparationFailureReason.IsEmpty()
					? TEXT("No additional failure detail was reported.")
					: PreparedSource.PreparationFailureReason,
				TEXT("  ")));
		RecordChildPlacementSearchFailure(
			Searcher.RootRequest,
			PreparedSource,
			Demand,
			TEXT("ChildPreparation"),
			OutSearchFailureReason,
			SchedulerState,
			PreparedSource.PreparationFailureKind,
			PreparedSource.PreparationCandidateAttemptCount);
		return false;
	}

	bool FinalizeFailedChildPlacementDemandSearch(
		const FPreparedChildPlacementSource& PreparedSource,
		const FChildPlacementDemand& Demand,
		const FString& SchedulerStateKey,
		const int32 CurrentScore,
		const FChildPlacementCandidateBuildResult& CandidateBuildResult,
		const FString& BestDownstreamFailureReason,
		FRecursivePlacementDemandSearcher& Searcher,
		FString& OutSearchFailureReason)
	{
		FRecursiveChildPlacementSchedulerState& SchedulerState = Searcher.SchedulerState;
		OutSearchFailureReason = BestDownstreamFailureReason.IsEmpty()
			? FString::Printf(
				TEXT("Required child entry could not be placed into the parent plan.\n%s\nProblem: No translated child footprint both matched the requested placement zone and survived child/parent probe validation.\n%s\nFix: Update the parent content set entry placement zone, child profile boundary/entry modules, or parent footprint so the child can occupy a compatible span."),
				*DescribeChildPlacementContext(Searcher.RootRequest, *PreparedSource.EntrySnapshot),
				CandidateBuildResult.BestRejectedCandidateReason.IsEmpty()
					? TEXT("Best Rejected Placement:\n  <none recorded>")
					: *CandidateBuildResult.BestRejectedCandidateReason)
			: BestDownstreamFailureReason;
		RecordChildPlacementSearchFailure(
			Searcher.RootRequest,
			PreparedSource,
			Demand,
			TEXT("ChildPlacementDownstream"),
			OutSearchFailureReason,
			SchedulerState);
		int32& FailedStateBestScore = SchedulerState.FailedSchedulerStateBestScore.FindOrAdd(SchedulerStateKey);
		FailedStateBestScore = FMath::Max(FailedStateBestScore, CurrentScore);
		return false;
	}

	FSuccessfulCandidateSearchResult SearchSuccessfulChildPlacementCandidates(
		const FPreparedChildPlacementSource& PreparedSource,
		const FChildPlacementDemand& Demand,
		const TArray<FScoredChildOffset>& SuccessfulCandidates,
		int32 DemandIndex,
		int32 CurrentScore,
		FRecursivePlacementDemandSearcher& Searcher)
	{
		FRecursiveChildPlacementSchedulerState& SchedulerState = Searcher.SchedulerState;
		FSuccessfulCandidateSearchResult Result;
		const bool bFinalDemand = DemandIndex + 1 == Searcher.PlacementDemands.Num();
		int32 CompletedSchedulesFromThisBranch = 0;
		for (const FScoredChildOffset& Candidate : SuccessfulCandidates)
		{
			if (Searcher.MaxDeferredCandidateCount > 0
				&& SchedulerState.DeferredCompletePlacementCandidates.Num()
					>= Searcher.MaxDeferredCandidateCount)
			{
				break;
			}
			// Candidate order already combines seam magnetism with seeded randomness.
			// Sample four completions per branch; widen only if all 24 diverse deferred candidates prove insufficient.
			if (bFinalDemand
				&& Searcher.PlacementDemands.Num() > 1
				&& CompletedSchedulesFromThisBranch >= 4)
			{
				break;
			}
			if (ShouldAbortRecursiveSchedulerForTimeout(
				Searcher,
				DemandIndex,
				CurrentScore,
				Result.BestDownstreamFailureReason))
			{
				return Result;
			}

			if (ShouldSkipSymmetricSameSourcePlacement(
				PreparedSource,
				Demand,
				Candidate,
				SchedulerState.CurrentPlacements,
				SchedulerState.LastChosenPlacementBySource))
			{
				continue;
			}

			check(PreparedSource.PlanningVariants.IsValidIndex(Candidate.VariantIndex));
			const FPreparedChildPlacementSource::FPlanningVariant& SelectedVariant =
				PreparedSource.PlanningVariants[Candidate.VariantIndex];

			const FAcceptedChildPlacementSearchResult DownstreamSearchResult =
				SearchWithAcceptedChildPlacement(
					PreparedSource,
					SelectedVariant,
					Demand,
					Candidate,
					DemandIndex,
					CurrentScore,
					Searcher);
			Result.bFoundDownstreamSolution =
				Result.bFoundDownstreamSolution || DownstreamSearchResult.bFoundDownstreamSolution;
			if (DownstreamSearchResult.bFoundDownstreamSolution)
			{
				++CompletedSchedulesFromThisBranch;
			}
			UpdateBestDownstreamFailureReason(
				Demand,
				Candidate,
				DownstreamSearchResult,
				Result.BestDownstreamFailureReason);
		}

		return Result;
	}

	bool SearchPlacementDemand(
		const int32 DemandIndex,
		const int32 CurrentScore,
		const FString& SchedulerStateKey,
		FRecursivePlacementDemandSearcher& Searcher,
		FString& OutSearchFailureReason)
	{
		if (ShouldAbortRecursiveSchedulerForTimeout(
			Searcher,
			DemandIndex,
			CurrentScore,
			OutSearchFailureReason))
		{
			return false;
		}

		FRecursiveChildPlacementSchedulerState& SchedulerState = Searcher.SchedulerState;
		const FChildPlacementDemand& Demand = Searcher.PlacementDemands[DemandIndex];
		TArray<int32> SourceIndices = Demand.AlternativeSourceIndices;
		if (SourceIndices.IsEmpty())
		{
			SourceIndices.Add(Demand.SourceIndex);
		}

		const FPreparedChildPlacementSource* FirstPreparedSource = nullptr;
		FChildPlacementCandidateBuildResult FirstCandidateBuildResult;
		bool bBuiltAnyCandidates = false;
		bool bHadSuccessfulCandidates = false;
		FString BestDownstreamFailureReason;
		for (const int32 SourceIndex : SourceIndices)
		{
			if (!Searcher.PreparedSources.IsValidIndex(SourceIndex))
			{
				continue;
			}

			const FPreparedChildPlacementSource& PreparedSource =
				Searcher.PreparedSources[SourceIndex];
			check(PreparedSource.EntrySnapshot != nullptr);
			if (FirstPreparedSource == nullptr)
			{
				FirstPreparedSource = &PreparedSource;
			}
			if (!PreparedSource.bPreparationSucceeded)
			{
				continue;
			}

			FChildPlacementDemand SourceDemand = Demand;
			SourceDemand.SourceIndex = SourceIndex;
			FChildPlacementCandidateBuildResult CandidateBuildResult;
			BuildSuccessfulChildPlacementCandidatesForDemand(
				SchedulerStateKey,
				DemandIndex,
				SourceDemand,
				PreparedSource,
				Searcher,
				SchedulerState,
				CandidateBuildResult);
			if (!bBuiltAnyCandidates)
			{
				FirstCandidateBuildResult = CandidateBuildResult;
				bBuiltAnyCandidates = true;
			}
			if (CandidateBuildResult.SuccessfulCandidates.IsEmpty())
			{
				continue;
			}
			if (Demand.bOptional && !Demand.bSharedFeatureChoiceSlot)
			{
				// Local project fix: authored optional content remains absent when
				// parent proof does not need it. Failed opportunities still use typed drop handling.
				return Searcher.Search(
					DemandIndex + 1,
					CurrentScore,
					OutSearchFailureReason);
			}

			bHadSuccessfulCandidates = true;
			const FSuccessfulCandidateSearchResult SuccessfulCandidateSearchResult =
				SearchSuccessfulChildPlacementCandidates(
					PreparedSource,
					SourceDemand,
					CandidateBuildResult.SuccessfulCandidates,
					DemandIndex,
					CurrentScore,
					Searcher);
			if (SuccessfulCandidateSearchResult.bFoundDownstreamSolution)
			{
				return true;
			}
			if (BestDownstreamFailureReason.IsEmpty())
			{
				BestDownstreamFailureReason =
					SuccessfulCandidateSearchResult.BestDownstreamFailureReason;
			}
		}

		check(FirstPreparedSource != nullptr);
		if (bHadSuccessfulCandidates && Demand.bOptional)
		{
			const FString PreviousFailureReason = OutSearchFailureReason;
			if (Searcher.Search(
					DemandIndex + 1,
					CurrentScore,
					OutSearchFailureReason))
			{
				return true;
			}
			if (OutSearchFailureReason.IsEmpty())
			{
				OutSearchFailureReason = PreviousFailureReason;
			}
		}

		if (!bBuiltAnyCandidates)
		{
			return HandlePreparedChildTopologyFailureForDemand(
				*FirstPreparedSource,
				Demand,
				CurrentScore,
				Searcher,
				DemandIndex,
				OutSearchFailureReason);
		}
		if (!bHadSuccessfulCandidates)
		{
			return HandleNoSuccessfulChildPlacementCandidates(
				*FirstPreparedSource,
				Demand,
				SchedulerStateKey,
				CurrentScore,
				FirstCandidateBuildResult,
				Searcher,
				DemandIndex,
				OutSearchFailureReason);
		}

		return FinalizeFailedChildPlacementDemandSearch(
			*FirstPreparedSource,
			Demand,
			SchedulerStateKey,
			CurrentScore,
			FirstCandidateBuildResult,
			BestDownstreamFailureReason,
			Searcher,
			OutSearchFailureReason);
	}

	void SortAndCapSuccessfulChildPlacementCandidates(
		TArray<FScoredChildOffset>& InOutSuccessfulCandidates,
		const bool bAllowTrimming,
		const bool bPartialPlacementStep,
		const int32 PlacementDemandCount,
		FRecursiveSchedulerPerfMetrics* PerfMetrics)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Recursive_CandidateNormalization, STAT_PorismLayout_CandidateNormalization);
		const int32 PreNormalizationCount = InOutSuccessfulCandidates.Num();
		InOutSuccessfulCandidates.Sort([](const FScoredChildOffset& Left, const FScoredChildOffset& Right)
		{
			if (Left.Score != Right.Score)
			{
				return Left.Score > Right.Score;
			}

			return Left.TieBreakHash < Right.TieBreakHash;
		});

		TSet<FString> SeenStructuralSignatures;
		InOutSuccessfulCandidates.RemoveAll([&SeenStructuralSignatures](const FScoredChildOffset& Candidate)
		{
			if (Candidate.StageMapping.MappingId == NAME_None)
			{
				return false;
			}
			FString StructuralSignature;
			if (Candidate.SharedParentChildFaces.IsEmpty())
			{
				TArray<FString> LocalContractParts;
				LocalContractParts.Add(FString::Printf(
					TEXT("Variant=%d:StageClass=%d"),
					Candidate.VariantIndex,
					static_cast<int32>(Candidate.StageMapping.StageClass)));
				for (const FLayoutPlannedCell& PlannedCell : Candidate.StageMapping.ChildLocalPlannedCells)
				{
					LocalContractParts.Add(FString::Printf(
						TEXT("Cell=%s:%d:%d:%d:%u%s"),
						*PlannedCell.Cell.ToString(),
						static_cast<int32>(PlannedCell.Intent),
						PlannedCell.ModuleLevelIndex,
						PlannedCell.bIsBridgeCell ? 1 : 0,
						PlannedCell.TerrainSeamFaceMask,
						PlannedCell.bIsTopBridgeOffer ? TEXT(":TopBridgeOffer") : TEXT("")));
				}
				for (const FLayoutCommittedEndpointAnchor& Commitment : Candidate.Commitments)
				{
					LocalContractParts.Add(FString::Printf(
						TEXT("Anchor=%s:%d:%s:%s"),
						*Commitment.LocalCell.ToString(),
						static_cast<int32>(Commitment.FaceDirection),
						*Commitment.ConnectionTag.ToString(),
						*Commitment.TraversalChannels.ToStringSimple()));
				}
				for (const FLayoutCellCandidateDomainRestriction& Restriction : Candidate.ChildDomainRestrictions)
				{
					LocalContractParts.Add(FString::Printf(
						TEXT("Domain=%s:%s"),
						*Restriction.Cell.ToString(),
						*FString::JoinBy(
							Restriction.AllowedCandidates,
							TEXT(","),
							[](const FLayoutCandidateVariantIdentity& Identity)
							{
								return FString::Printf(
									TEXT("%s@%d"),
									*Identity.ModuleSnapshotId.ToString(),
									Identity.YawRotationSteps);
							})));
				}
				LocalContractParts.Sort();
				StructuralSignature = FString::Join(LocalContractParts, TEXT("|"));
			}
			else
			{
				StructuralSignature = FString::Printf(
					TEXT("%s|%s"),
					*Candidate.StageMapping.MappingId.ToString(),
					Candidate.BoundaryCertificateId.IsNone()
						? TEXT("<none>")
						: *Candidate.BoundaryCertificateId.ToString());
			}
			if (SeenStructuralSignatures.Contains(StructuralSignature))
			{
				return true;
			}
			SeenStructuralSignatures.Add(StructuralSignature);
			return false;
		});
		const int32 PostCollapseCount = InOutSuccessfulCandidates.Num();
		if (PerfMetrics != nullptr)
		{
			PerfMetrics->CandidateNormalizationCollapsedCount +=
				FMath::Max(0, PreNormalizationCount - PostCollapseCount);
		}

		if (bAllowTrimming && bPartialPlacementStep && PlacementDemandCount > 2 && InOutSuccessfulCandidates.Num() > 12)
		{
			// Once an intermediate child-placement step has already established local
			// feasibility, keep only the highest-value partial candidates. The completed
			// placement set still pays the full parent probe cost later.
			InOutSuccessfulCandidates.SetNum(12);
		}
		else if (bAllowTrimming && bPartialPlacementStep && PlacementDemandCount == 2 && InOutSuccessfulCandidates.Num() > 128)
		{
			// Two-child authored fixtures need enough breadth for the first child to
			// leave a valid sibling footprint, but an unbounded beam can turn one
			// comparison fixture into a minutes-long exhaustive search.
			InOutSuccessfulCandidates.SetNum(128);
		}

		if (PerfMetrics != nullptr)
		{
			PerfMetrics->CandidateNormalizationBeamTrimmedCount += FMath::Max(0, PostCollapseCount - InOutSuccessfulCandidates.Num());
			PerfMetrics->CandidateNormalizationRetainedCount += InOutSuccessfulCandidates.Num();
		}
	}
}
