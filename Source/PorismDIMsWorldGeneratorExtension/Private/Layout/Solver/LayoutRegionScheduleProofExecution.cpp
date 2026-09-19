// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "LayoutRegionSchedulePlacementBridgeTypes.h"
#include "LayoutProfileSolverInternal.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

/**
 * Private rewrite home for:
 * - `FLayoutProofExecutor`
 * - `FLayoutMergedScheduleAuditor`
 *
 * This file will own proof execution and merged contract validation once those
 * stages move off the legacy recursive scheduler path.
 */

namespace LayoutRegionScheduleSolverPrivate
{
	bool DoesBoundaryPointSatisfyCommittedAnchor(
		const FLayoutSolveBoundaryPoint& BoundaryPoint,
		const FLayoutCommittedEndpointAnchor& Anchor);
}

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
		template <typename ArrayType, typename ValueType, typename PredicateType>
		static bool ContainsEquivalentValue(
			const ArrayType& Values,
			const ValueType& ExpectedValue,
			PredicateType&& Predicate)
		{
			return Values.ContainsByPredicate(
				[&ExpectedValue, &Predicate](const auto& ExistingValue)
				{
					return Predicate(ExistingValue, ExpectedValue);
				});
		}

		template <typename LeftArrayType, typename RightArrayType, typename PredicateType>
		static bool AreUnorderedArraysEquivalent(
			const LeftArrayType& Left,
			const RightArrayType& Right,
			PredicateType&& Predicate)
		{
			if (Left.Num() != Right.Num())
			{
				return false;
			}

			TBitArray<> MatchedRightEntries(false, Right.Num());
			for (const auto& LeftValue : Left)
			{
				bool bFoundMatch = false;
				for (int32 RightIndex = 0; RightIndex < Right.Num(); ++RightIndex)
				{
					if (MatchedRightEntries[RightIndex])
					{
						continue;
					}

					if (!Predicate(LeftValue, Right[RightIndex]))
					{
						continue;
					}

					MatchedRightEntries[RightIndex] = true;
					bFoundMatch = true;
					break;
				}

				if (!bFoundMatch)
				{
					return false;
				}
			}

			return true;
		}

		template <typename LeftArrayType, typename RightArrayType, typename PredicateType>
		static bool IsUnorderedArraySubsetEquivalent(
			const LeftArrayType& Left,
			const RightArrayType& Right,
			PredicateType&& Predicate)
		{
			if (Left.Num() > Right.Num())
			{
				return false;
			}

			TBitArray<> MatchedRightEntries(false, Right.Num());
			for (const auto& LeftValue : Left)
			{
				bool bFoundMatch = false;
				for (int32 RightIndex = 0; RightIndex < Right.Num(); ++RightIndex)
				{
					if (MatchedRightEntries[RightIndex])
					{
						continue;
					}

					if (!Predicate(LeftValue, Right[RightIndex]))
					{
						continue;
					}

					MatchedRightEntries[RightIndex] = true;
					bFoundMatch = true;
					break;
				}

				if (!bFoundMatch)
				{
					return false;
				}
			}

			return true;
		}

		template <typename LeftArrayType, typename RightArrayType, typename PredicateType>
		static bool AreArraysEquivalent(
			const LeftArrayType& Left,
			const RightArrayType& Right,
			PredicateType&& Predicate)
		{
			if (Left.Num() != Right.Num())
			{
				return false;
			}

			for (int32 Index = 0; Index < Left.Num(); ++Index)
			{
				if (!Predicate(Left[Index], Right[Index]))
				{
					return false;
				}
			}
			return true;
		}

		static FLayoutProofRecord MakeProofExecutionSnapshotProofRecord(
			const FLayoutId ProofId,
			const ELayoutProofKind ProofKind,
			const FLayoutId TargetId,
			const TArray<FLayoutId>& SourceIds,
			const FString& ProofSummary)
		{
			FLayoutProofRecord Record;
			Record.ProofId = ProofId;
			Record.ProofKind = ProofKind;
			Record.TargetId = TargetId;
			Record.SourceIds = SourceIds;
			Record.ProofSummary = ProofSummary;
			return Record;
		}

		static FLayoutValidationAssertionRecord MakeProofExecutionSnapshotAssertionRecord(
			const FLayoutId AssertionId,
			const ELayoutValidationAssertionKind AssertionKind,
			const bool bPassed,
			const TArray<FLayoutId>& RelatedIds,
			const FString& FailureReason)
		{
			FLayoutValidationAssertionRecord Record;
			Record.AssertionId = AssertionId;
			Record.AssertionKind = AssertionKind;
			Record.bPassed = bPassed;
			Record.RelatedIds = RelatedIds;
			Record.FailureReason = FailureReason;
			return Record;
		}

		static FString BuildProofExecutionRequestBreadcrumb(const FLayoutRegionSolveRequest& Request)
		{
			return FString::Printf(
				TEXT(" Request '%s' (%s)."),
				*Request.RegionDebugPath,
				*Request.EffectiveSnapshotId.ToString());
		}

		static FString BuildProofExecutionProfileSnapshotBreadcrumb(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
		{
			const FString LiveProfileBreadcrumb =
				ProfileSnapshot.SourceProfile != nullptr
					? FString::Printf(TEXT(" Profile: %s."), *ProfileSnapshot.SourceProfile->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Frozen profile snapshot %s (%s).%s"),
				*ProfileSnapshot.DebugName.ToString(),
				*ProfileSnapshot.SnapshotId.ToString(),
				*LiveProfileBreadcrumb);
		}

		static FString BuildProofExecutionContentSetSnapshotBreadcrumb(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
		{
			const FString LiveContentSetBreadcrumb =
				ContentSetSnapshot.SourceContentSet != nullptr
					? FString::Printf(TEXT(" ContentSet: %s."), *ContentSetSnapshot.SourceContentSet->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Content-set snapshot %s (%s).%s"),
				*ContentSetSnapshot.DebugName.ToString(),
				*ContentSetSnapshot.SnapshotId.ToString(),
				*LiveContentSetBreadcrumb);
		}

		static FString BuildProofExecutionModuleCatalogBreadcrumb(const FLayoutModuleCatalog& ModuleCatalog)
		{
			return FString::Printf(
				TEXT(" Module-set snapshot %s (%s).%s"),
				*ModuleCatalog.DebugName.ToString(),
				*ModuleCatalog.SnapshotId.ToString(),
				*FString());
		}

		static TArray<FLayoutId> BuildProofExecutionRelatedIds(
			const FLayoutRegionSolveRequest& Request,
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			TArray<FLayoutId> RelatedIds = {
				Request.EffectiveSnapshotId,
				Request.ProfileSnapshot.SnapshotId,
				FLayoutId(*Contract.ParentRegionDebugPath),
				FLayoutId(*Contract.ChildRegionDebugPath)
			};
			if (Request.ContentSetSnapshot.SnapshotId != NAME_None)
			{
				RelatedIds.Add(Request.ContentSetSnapshot.SnapshotId);
			}
			if (Request.ModuleCatalog.SnapshotId != NAME_None)
			{
				RelatedIds.Add(Request.ModuleCatalog.SnapshotId);
			}
			return RelatedIds;
		}

		static void UpsertProofExecutionAssertion(
			TArray<FLayoutValidationAssertionRecord>& Assertions,
			const FLayoutValidationAssertionRecord& Assertion)
		{
			if (FLayoutValidationAssertionRecord* Existing =
				Assertions.FindByPredicate(
					[&Assertion](const FLayoutValidationAssertionRecord& Candidate)
					{
						return Candidate.AssertionId == Assertion.AssertionId;
					}))
			{
				*Existing = Assertion;
				return;
			}

			Assertions.Add(Assertion);
		}

		static void UpsertProofExecutionProofRecord(
			TArray<FLayoutProofRecord>& ProofRecords,
			const FLayoutProofRecord& ProofRecord)
		{
			if (FLayoutProofRecord* Existing =
				ProofRecords.FindByPredicate(
					[&ProofRecord](const FLayoutProofRecord& Candidate)
					{
						return Candidate.ProofId == ProofRecord.ProofId;
					}))
			{
				*Existing = ProofRecord;
				return;
			}

			ProofRecords.Add(ProofRecord);
		}

		static FString DescribeProofExecutionNegotiatedHostVerticalAccessResponsibility(
			const ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility)
		{
			switch (Responsibility)
			{
				case ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned:
					return TEXT("ParentOwned");
				case ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned:
					return TEXT("ChildOwned");
				case ELayoutNegotiatedHostVerticalAccessResponsibility::Composed:
					return TEXT("Composed");
				default:
					return TEXT("Unknown");
			}
		}

		static FString DescribeProofExecutionNegotiatedChildResponsibilityContract(
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			int32 ReplacementCellCount = 0;
			for (const FLayoutNegotiatedLevelCellSet& LevelCells : Contract.ReplacementVolumeByLevel)
			{
				ReplacementCellCount += LevelCells.Cells.Num();
			}

			int32 RetainedShellCellCount = 0;
			for (const FLayoutNegotiatedLevelCellSet& LevelCells : Contract.RetainedParentShellCellsByLevel)
			{
				RetainedShellCellCount += LevelCells.Cells.Num();
			}

			int32 ProofOnlyHostShellCellCount = 0;
			for (const FLayoutNegotiatedLevelCellSet& LevelCells : Contract.ProofOnlyHostAscentParentShellCellsByLevel)
			{
				ProofOnlyHostShellCellCount += LevelCells.Cells.Num();
			}

			int32 InterfaceAnchorCount = 0;
			int32 TraversalAnchorCount = 0;
			for (const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract : Contract.CommittedParentChildInterfacesByLevel)
			{
				InterfaceAnchorCount += InterfaceContract.EndpointAnchors.Num();
				TraversalAnchorCount += InterfaceContract.TraversalAnchors.Num();
			}

			int32 SeamCount = 0;
			for (const FLayoutNegotiatedLevelSeamSet& SeamSet : Contract.CommittedSiblingInterfacesByLevel)
			{
				SeamCount += SeamSet.Seams.Num();
			}

			return FString::Printf(
				TEXT("Negotiated child responsibility: parent='%s' child='%s' hostVerticalAccess=%s requiredHostProviders=%d countedParentProviders=%d countedParentCells=%d countedChildProviders=%d replacementLevels=%d replacementCells=%d retainedShellLevels=%d retainedShellCells=%d proofOnlyHostShellLevels=%d proofOnlyHostShellCells=%d interfaceLevels=%d endpointAnchors=%d traversalAnchors=%d siblingSeamLevels=%d siblingSeams=%d ingress=%s egress=%s childVerticalRouteCells=%d"),
				*Contract.ParentRegionDebugPath,
				*Contract.ChildRegionDebugPath,
				*DescribeProofExecutionNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
				Contract.RequiredHostProviderCount,
				Contract.CountedParentProviderCount,
				Contract.CountedParentVerticalAccessCells.Num(),
				Contract.CountedChildProviderRegionDebugPaths.Num(),
				Contract.ReplacementVolumeByLevel.Num(),
				ReplacementCellCount,
				Contract.RetainedParentShellCellsByLevel.Num(),
				RetainedShellCellCount,
				Contract.ProofOnlyHostAscentParentShellCellsByLevel.Num(),
				ProofOnlyHostShellCellCount,
				Contract.CommittedParentChildInterfacesByLevel.Num(),
				InterfaceAnchorCount,
				TraversalAnchorCount,
				Contract.CommittedSiblingInterfacesByLevel.Num(),
				SeamCount,
				Contract.bHasRequiredHostIngressAnchor ? *Contract.RequiredHostIngressAnchor.CommitmentId.ToString() : TEXT("<none>"),
				Contract.bHasRequiredHostEgressAnchor ? *Contract.RequiredHostEgressAnchor.CommitmentId.ToString() : TEXT("<none>"),
				Contract.RequiredChildInternalVerticalRouteCells.Num());
		}

		static bool AppendProofExecutionNegotiatedContractEvidenceToRequest(
			FLayoutRegionSolveRequest& InOutRequest,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			for (const FLayoutNegotiatedChildResponsibilityContract& Contract : InOutRequest.NegotiatedChildResponsibilityContracts)
			{
				const FString ContractDescription =
					DescribeProofExecutionNegotiatedChildResponsibilityContract(Contract);
				const FLayoutId ContractRecordId(*FString::Printf(
					TEXT("%s.NegotiatedChildResponsibility.%s"),
					*InOutRequest.RegionDebugPath,
					*Contract.ChildRegionDebugPath));
				const TArray<FLayoutId> RelatedIds =
					BuildProofExecutionRelatedIds(InOutRequest, Contract);

				FString ContractFailureReason;
				const bool bContractValid =
					LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
						Contract,
						ContractFailureReason);
				const FLayoutValidationAssertionRecord AssertionRecord =
					MakeProofExecutionSnapshotAssertionRecord(
						ContractRecordId,
						ELayoutValidationAssertionKind::ChildCommitmentContractValid,
						bContractValid,
						RelatedIds,
						bContractValid
							? FString()
							: FString::Printf(
								TEXT("Negotiated child responsibility contract is incomplete before region proof starts.\n%s%s%s%s%s\nProblem: Later proof phases would have to rediscover ownership or proceed with an invalid negotiated boundary.\nDetail: %s"),
								*ContractDescription,
								*BuildProofExecutionRequestBreadcrumb(InOutRequest),
								*BuildProofExecutionProfileSnapshotBreadcrumb(InOutRequest.ProfileSnapshot),
								*BuildProofExecutionContentSetSnapshotBreadcrumb(InOutRequest.ContentSetSnapshot),
								*BuildProofExecutionModuleCatalogBreadcrumb(InOutRequest.ModuleCatalog),
								*ContractFailureReason));
				UpsertProofExecutionAssertion(InOutRequest.ValidationAssertions, AssertionRecord);
				if (!bContractValid)
				{
					InOutRequest.ProofRecords.RemoveAll(
						[ContractRecordId](const FLayoutProofRecord& Existing)
						{
							return Existing.ProofId == ContractRecordId;
						});
					continue;
				}

				const FLayoutProofRecord ProofRecord =
					MakeProofExecutionSnapshotProofRecord(
						ContractRecordId,
						ELayoutProofKind::NormalizedCommitment,
						InOutRequest.EffectiveSnapshotId,
						RelatedIds,
						FString::Printf(
							TEXT("Validated negotiated child responsibility contract before region proof began.\n%s"),
							*ContractDescription));
				UpsertProofExecutionProofRecord(InOutRequest.ProofRecords, ProofRecord);
			}

			return true;
		}

		static bool AreProofExecutionGameplayTagContainersEquivalent(
			const FGameplayTagContainer& Left,
			const FGameplayTagContainer& Right)
		{
			return Left.HasAllExact(Right)
				&& Right.HasAllExact(Left);
		}

		static bool AreProofExecutionProofRecordsEquivalent(
			const FLayoutProofRecord& Left,
			const FLayoutProofRecord& Right)
		{
			return Left.ProofId == Right.ProofId
				&& Left.ProofKind == Right.ProofKind
				&& Left.TargetId == Right.TargetId
				&& Left.SourceIds == Right.SourceIds
				&& Left.ProofSummary == Right.ProofSummary;
		}

		static bool AreProofExecutionValidationAssertionsEquivalent(
			const FLayoutValidationAssertionRecord& Left,
			const FLayoutValidationAssertionRecord& Right)
		{
			return Left.AssertionId == Right.AssertionId
				&& Left.AssertionKind == Right.AssertionKind
				&& Left.bPassed == Right.bPassed
				&& Left.RelatedIds == Right.RelatedIds
				&& Left.FailureReason == Right.FailureReason;
		}

		static bool AreProofExecutionDroppedOptionalChildRecordsEquivalent(
			const FLayoutDroppedOptionalChildRecord& Left,
			const FLayoutDroppedOptionalChildRecord& Right)
		{
			return Left.DropDecisionId == Right.DropDecisionId
				&& Left.ParentRegionDebugPath == Right.ParentRegionDebugPath
				&& Left.ChildRegionDebugPath == Right.ChildRegionDebugPath
				&& Left.SourceContentEntryId == Right.SourceContentEntryId
				&& Left.ChildProfileSnapshotId == Right.ChildProfileSnapshotId
				&& Left.RelatedIds == Right.RelatedIds
				&& Left.FailureReason == Right.FailureReason;
		}

		static bool AreProofExecutionCommittedEndpointAnchorsEquivalent(
			const FLayoutCommittedEndpointAnchor& Left,
			const FLayoutCommittedEndpointAnchor& Right)
		{
			return Left.CommitmentId == Right.CommitmentId
				&& Left.LocalCell == Right.LocalCell
				&& Left.FaceDirection == Right.FaceDirection
				&& Left.RequiredWorldCenterBlockZ == Right.RequiredWorldCenterBlockZ
				&& Left.ConnectionTag == Right.ConnectionTag
				&& AreProofExecutionGameplayTagContainersEquivalent(Left.AllowedConnectionTags, Right.AllowedConnectionTags)
				&& AreProofExecutionGameplayTagContainersEquivalent(Left.TraversalChannels, Right.TraversalChannels)
				&& Left.bRequireMatchingYawWithFilledNeighbor == Right.bRequireMatchingYawWithFilledNeighbor;
		}

		static bool AreProofExecutionCommittedTraversalAnchorsEquivalent(
			const FLayoutCommittedTraversalAnchor& Left,
			const FLayoutCommittedTraversalAnchor& Right)
		{
			return Left.Cell == Right.Cell && Left.TraversalChannel == Right.TraversalChannel;
		}

		static bool AreProofExecutionIntVectorsEquivalent(
			const TArray<FIntVector>& Left,
			const TArray<FIntVector>& Right)
		{
			return AreArraysEquivalent(
				Left,
				Right,
				[](const FIntVector& LeftCell, const FIntVector& RightCell)
				{
					return LeftCell == RightCell;
				});
		}

		static bool AreProofExecutionSteppedTerrainSupportMapsEquivalent(
			const FLayoutSteppedTerrainSupportMap& Left,
			const FLayoutSteppedTerrainSupportMap& Right)
		{
			return Left.SharedCellHeightInBlocks == Right.SharedCellHeightInBlocks
				&& Left.MaximumObservedNeighborHeightDelta == Right.MaximumObservedNeighborHeightDelta
				&& Left.MaximumObservedSnappedLevelDelta == Right.MaximumObservedSnappedLevelDelta
				&& AreArraysEquivalent(
					Left.SupportSamples,
					Right.SupportSamples,
					[](const FLayoutSteppedTerrainSupportSample& LeftSample, const FLayoutSteppedTerrainSupportSample& RightSample)
					{
						return LeftSample.LocalCell == RightSample.LocalCell
							&& LeftSample.SupportSurfaceZ == RightSample.SupportSurfaceZ
							&& LeftSample.SnappedSupportFloorZ == RightSample.SnappedSupportFloorZ
							&& LeftSample.SnappedSupportCeilingZ == RightSample.SnappedSupportCeilingZ;
					})
				&& AreArraysEquivalent(
					Left.AdjacencySteps,
					Right.AdjacencySteps,
					[](const FLayoutSteppedTerrainAdjacencyStep& LeftStep, const FLayoutSteppedTerrainAdjacencyStep& RightStep)
					{
						return LeftStep.FromCell == RightStep.FromCell
							&& LeftStep.ToCell == RightStep.ToCell
							&& LeftStep.StepHeightBlocks == RightStep.StepHeightBlocks
							&& LeftStep.SnappedLevelDelta == RightStep.SnappedLevelDelta;
					});
		}

		static bool AreProofExecutionForcedPlacementBundleInsertionsEquivalent(
			const TArray<FLayoutForcedPlacementBundleInsertion>& Left,
			const TArray<FLayoutForcedPlacementBundleInsertion>& Right)
		{
			return AreArraysEquivalent(
				Left,
				Right,
				[](const FLayoutForcedPlacementBundleInsertion& LeftInsertion, const FLayoutForcedPlacementBundleInsertion& RightInsertion)
				{
					return LeftInsertion.BundleId == RightInsertion.BundleId
						&& LeftInsertion.AnchorCell == RightInsertion.AnchorCell
						&& LeftInsertion.ProvingCell == RightInsertion.ProvingCell;
				});
		}

		static bool AreProofExecutionRouteConstraintArraysEquivalent(
			const TArray<FLayoutRouteConstraintRecord>& Left,
			const TArray<FLayoutRouteConstraintRecord>& Right)
		{
			return AreArraysEquivalent(
				Left,
				Right,
				[](const FLayoutRouteConstraintRecord& LeftConstraint, const FLayoutRouteConstraintRecord& RightConstraint)
				{
					return LeftConstraint.ConstraintId == RightConstraint.ConstraintId
						&& LeftConstraint.Cell == RightConstraint.Cell
						&& LeftConstraint.Intent == RightConstraint.Intent
						&& LeftConstraint.bScoreAsMainRoute == RightConstraint.bScoreAsMainRoute
						&& AreArraysEquivalent(
							LeftConstraint.FaceRequirements,
							RightConstraint.FaceRequirements,
							[](const FLayoutRouteFaceRequirement& LeftRequirement, const FLayoutRouteFaceRequirement& RightRequirement)
							{
								return LeftRequirement.FaceDirection == RightRequirement.FaceDirection
									&& LeftRequirement.TraversalChannel == RightRequirement.TraversalChannel;
							});
				});
		}

		static bool AreProofExecutionCommittedEndpointAnchorArraysEquivalent(
			const TArray<FLayoutCommittedEndpointAnchor>& Left,
			const TArray<FLayoutCommittedEndpointAnchor>& Right)
		{
			return AreUnorderedArraysEquivalent(
				Left,
				Right,
				[](const FLayoutCommittedEndpointAnchor& LeftAnchor, const FLayoutCommittedEndpointAnchor& RightAnchor)
				{
					return AreProofExecutionCommittedEndpointAnchorsEquivalent(LeftAnchor, RightAnchor);
				});
		}

		static bool AreProofExecutionCommittedTraversalAnchorArraysEquivalent(
			const TArray<FLayoutCommittedTraversalAnchor>& Left,
			const TArray<FLayoutCommittedTraversalAnchor>& Right)
		{
			return AreUnorderedArraysEquivalent(
				Left,
				Right,
				[](const FLayoutCommittedTraversalAnchor& LeftAnchor, const FLayoutCommittedTraversalAnchor& RightAnchor)
				{
					return AreProofExecutionCommittedTraversalAnchorsEquivalent(LeftAnchor, RightAnchor);
				});
		}

		static bool AreProofExecutionNegotiatedLevelCellSetsEquivalent(
			const TArray<FLayoutNegotiatedLevelCellSet>& Left,
			const TArray<FLayoutNegotiatedLevelCellSet>& Right)
		{
			return AreUnorderedArraysEquivalent(
				Left,
				Right,
				[](const FLayoutNegotiatedLevelCellSet& LeftSet, const FLayoutNegotiatedLevelCellSet& RightSet)
				{
					return LeftSet.Level == RightSet.Level
						&& AreProofExecutionIntVectorsEquivalent(LeftSet.Cells, RightSet.Cells);
				});
		}

		static bool AreProofExecutionNegotiatedLevelInterfacesEquivalent(
			const TArray<FLayoutNegotiatedLevelInterfaceContract>& Left,
			const TArray<FLayoutNegotiatedLevelInterfaceContract>& Right)
		{
			return AreUnorderedArraysEquivalent(
				Left,
				Right,
				[](const FLayoutNegotiatedLevelInterfaceContract& LeftInterface, const FLayoutNegotiatedLevelInterfaceContract& RightInterface)
				{
					return LeftInterface.Level == RightInterface.Level
						&& AreProofExecutionCommittedEndpointAnchorArraysEquivalent(LeftInterface.EndpointAnchors, RightInterface.EndpointAnchors)
						&& AreProofExecutionCommittedTraversalAnchorArraysEquivalent(LeftInterface.TraversalAnchors, RightInterface.TraversalAnchors);
				});
		}

		static bool AreProofExecutionPartitionSeamRecordsEquivalent(
			const FLayoutPartitionSeamRecord& Left,
			const FLayoutPartitionSeamRecord& Right)
		{
			return Left.SeamId == Right.SeamId
				&& Left.ParentRegionDebugPath == Right.ParentRegionDebugPath
				&& Left.OwnerRegionDebugPath == Right.OwnerRegionDebugPath
				&& Left.PassiveRegionDebugPath == Right.PassiveRegionDebugPath
				&& Left.InterfaceFamily == Right.InterfaceFamily
				&& Left.OwnerFaceDirection == Right.OwnerFaceDirection
				&& Left.PassiveFaceDirection == Right.PassiveFaceDirection
				&& Left.OwnerStartCell == Right.OwnerStartCell
				&& Left.OwnerEndCell == Right.OwnerEndCell
				&& Left.PassiveStartCell == Right.PassiveStartCell
				&& Left.PassiveEndCell == Right.PassiveEndCell
				&& Left.SegmentCount == Right.SegmentCount
				&& Left.bCountsTowardClosure == Right.bCountsTowardClosure
				&& Left.ProviderId == Right.ProviderId;
		}

		static FLayoutOwnedSeamJunctionRequirement ConvertOwnedSeamJunctionRequirement(
			const FOwnedSeamJunctionRequirement& Requirement)
		{
			FLayoutOwnedSeamJunctionRequirement ConvertedRequirement;
			ConvertedRequirement.JunctionRequirementId = Requirement.JunctionRequirementId;
			ConvertedRequirement.OwnerRegionDebugPath = Requirement.OwnerRegionDebugPath;
			ConvertedRequirement.PassiveRegionDebugPath = Requirement.PassiveRegionDebugPath;
			ConvertedRequirement.AdjacencyClassId = Requirement.AdjacencyClassId;
			ConvertedRequirement.JunctionCell = Requirement.JunctionCell;
			ConvertedRequirement.ContinuingSeamId = Requirement.ContinuingSeamId;
			ConvertedRequirement.BranchSeamId = Requirement.BranchSeamId;
			ConvertedRequirement.ContinuingPassiveRegionDebugPath =
				Requirement.ContinuingPassiveRegionDebugPath;
			ConvertedRequirement.InterfaceFamily = Requirement.InterfaceFamily;
			ConvertedRequirement.ContinuingOwnerFaceDirection =
				Requirement.ContinuingOwnerFaceDirection;
			ConvertedRequirement.BranchOwnerFaceDirection = Requirement.BranchOwnerFaceDirection;
			return ConvertedRequirement;
		}

		static bool AreProofExecutionOwnedSeamJunctionRequirementsEquivalent(
			const FLayoutOwnedSeamJunctionRequirement& Left,
			const FLayoutOwnedSeamJunctionRequirement& Right)
		{
			return Left.JunctionRequirementId == Right.JunctionRequirementId
				&& Left.OwnerRegionDebugPath == Right.OwnerRegionDebugPath
				&& Left.PassiveRegionDebugPath == Right.PassiveRegionDebugPath
				&& Left.AdjacencyClassId == Right.AdjacencyClassId
				&& Left.JunctionCell == Right.JunctionCell
				&& Left.ContinuingSeamId == Right.ContinuingSeamId
				&& Left.BranchSeamId == Right.BranchSeamId
				&& Left.ContinuingPassiveRegionDebugPath == Right.ContinuingPassiveRegionDebugPath
				&& Left.InterfaceFamily.MatchesTagExact(Right.InterfaceFamily)
				&& Left.ContinuingOwnerFaceDirection == Right.ContinuingOwnerFaceDirection
				&& Left.BranchOwnerFaceDirection == Right.BranchOwnerFaceDirection;
		}

		static bool AreProofExecutionNegotiatedLevelSeamsEquivalent(
			const TArray<FLayoutNegotiatedLevelSeamSet>& Left,
			const TArray<FLayoutNegotiatedLevelSeamSet>& Right)
		{
			return AreUnorderedArraysEquivalent(
				Left,
				Right,
				[](const FLayoutNegotiatedLevelSeamSet& LeftSeams, const FLayoutNegotiatedLevelSeamSet& RightSeams)
				{
					return LeftSeams.Level == RightSeams.Level
						&& AreUnorderedArraysEquivalent(
							LeftSeams.Seams,
							RightSeams.Seams,
							[](const FLayoutPartitionSeamRecord& LeftSeam, const FLayoutPartitionSeamRecord& RightSeam)
							{
								return AreProofExecutionPartitionSeamRecordsEquivalent(LeftSeam, RightSeam);
							});
				});
		}

		static bool AreProofExecutionNegotiatedResponsibilityContractsEquivalent(
			const FLayoutNegotiatedChildResponsibilityContract& Left,
			const FLayoutNegotiatedChildResponsibilityContract& Right)
		{
			return Left.ParentRegionDebugPath == Right.ParentRegionDebugPath
				&& Left.ChildRegionDebugPath == Right.ChildRegionDebugPath
				&& AreProofExecutionNegotiatedLevelCellSetsEquivalent(Left.ReplacementVolumeByLevel, Right.ReplacementVolumeByLevel)
				&& AreProofExecutionNegotiatedLevelCellSetsEquivalent(Left.RetainedParentShellCellsByLevel, Right.RetainedParentShellCellsByLevel)
				&& AreProofExecutionNegotiatedLevelCellSetsEquivalent(Left.ProofOnlyHostAscentParentShellCellsByLevel, Right.ProofOnlyHostAscentParentShellCellsByLevel)
				&& AreProofExecutionNegotiatedLevelInterfacesEquivalent(Left.CommittedParentChildInterfacesByLevel, Right.CommittedParentChildInterfacesByLevel)
				&& AreProofExecutionNegotiatedLevelSeamsEquivalent(Left.CommittedSiblingInterfacesByLevel, Right.CommittedSiblingInterfacesByLevel)
				&& Left.HostVerticalAccessResponsibility == Right.HostVerticalAccessResponsibility
				&& Left.RequiredHostProviderCount == Right.RequiredHostProviderCount
				&& Left.bRequiresExactHostProviderCount == Right.bRequiresExactHostProviderCount
				&& Left.CountedParentProviderCount == Right.CountedParentProviderCount
				&& AreProofExecutionIntVectorsEquivalent(Left.CountedParentVerticalAccessCells, Right.CountedParentVerticalAccessCells)
				&& AreProofExecutionIntVectorsEquivalent(Left.RetainedParentRouteSupportVerticalAccessCells, Right.RetainedParentRouteSupportVerticalAccessCells)
				&& Left.CountedChildProviderRegionDebugPaths == Right.CountedChildProviderRegionDebugPaths
				&& Left.bHasRequiredHostIngressAnchor == Right.bHasRequiredHostIngressAnchor
				&& (!Left.bHasRequiredHostIngressAnchor || AreProofExecutionCommittedEndpointAnchorsEquivalent(Left.RequiredHostIngressAnchor, Right.RequiredHostIngressAnchor))
				&& Left.bHasRequiredHostEgressAnchor == Right.bHasRequiredHostEgressAnchor
				&& (!Left.bHasRequiredHostEgressAnchor || AreProofExecutionCommittedEndpointAnchorsEquivalent(Left.RequiredHostEgressAnchor, Right.RequiredHostEgressAnchor))
				&& AreProofExecutionIntVectorsEquivalent(Left.RequiredChildInternalVerticalRouteCells, Right.RequiredChildInternalVerticalRouteCells);
		}

		/**
		 * Builds the legacy schedule request consumed by the temporary synchronous
		 * proof adapter. This compatibility shape is intentionally private to this
		 * file so the rewrite can replace it with a threaded executor later.
		 */
		static FLayoutRegionSolveScheduleRequest BuildLegacySynchronousProofScheduleRequest(
			const FProofJobPlan& ProofJobPlan,
			const FNegotiatedProofScheduleContract& ProofContract)
		{
			FLayoutRegionSolveScheduleRequest ScheduleRequest;
			ScheduleRequest.RegionRequests.Add(ProofJobPlan.ParentRequest);
			for (const FLayoutRegionSolveRequest& ChildRequest : ProofJobPlan.ChildRequests)
			{
				ScheduleRequest.RegionRequests.Add(ChildRequest);
				ScheduleRequest.Dependencies.Add(
					{ProofJobPlan.ParentRequest.RegionDebugPath, ChildRequest.RegionDebugPath});
			}
			ScheduleRequest.DirectChildCommitments = ProofContract.DirectChildCommitments;
			ScheduleRequest.PlannedPartitionSeams = ProofContract.SeamPlan.PlannedPartitionSeams;
			ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative =
				ProofContract.SeamPlan.bPlannedPartitionSeamsAreAuthoritative;
			for (const FOwnedSeamJunctionRequirement& Requirement : ProofContract.SeamPlan.JunctionRequirements)
			{
				ScheduleRequest.PlannedJunctionRequirements.Add(
					ConvertOwnedSeamJunctionRequirement(Requirement));
			}
			return ScheduleRequest;
		}

		static const FLayoutRegionSolveRequest* FindChildProofRequest(
			const FProofJobPlan& ProofJobPlan,
			const FString& ChildRegionDebugPath)
		{
			return ProofJobPlan.ChildRequests.FindByPredicate(
				[&ChildRegionDebugPath](const FLayoutRegionSolveRequest& ChildRequest)
				{
					return ChildRequest.RegionDebugPath == ChildRegionDebugPath;
				});
		}

		static const FLayoutDirectChildRegionCommitment* FindDirectChildCommitment(
			const FNegotiatedProofScheduleContract& ProofContract,
			const FString& ChildRegionDebugPath)
		{
			return ProofContract.DirectChildCommitments.FindByPredicate(
				[&ChildRegionDebugPath](const FLayoutDirectChildRegionCommitment& Commitment)
				{
					return Commitment.ChildRegionDebugPath == ChildRegionDebugPath;
				});
		}

		static bool ChildRequestMayEmitNestedResponsibilityContracts(
			const FLayoutRegionSolveRequest& ChildRequest)
		{
			return ChildRequest.ContentSetSnapshot.Entries.ContainsByPredicate(
				[](const FLayoutRegionContentEntrySolveSnapshot& Entry)
				{
					return Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
				});
		}

		static bool ValidateChildResponsibilityContractsPreserved(
			const FLayoutRegionSolveRequest& ChildRequest,
			const TArray<FLayoutNegotiatedChildResponsibilityContract>& ActualContracts)
		{
			const TArray<FLayoutNegotiatedChildResponsibilityContract>& ExpectedContracts =
				ChildRequest.NegotiatedChildResponsibilityContracts;
			if (AreUnorderedArraysEquivalent(
				ActualContracts,
				ExpectedContracts,
				[](const FLayoutNegotiatedChildResponsibilityContract& Left, const FLayoutNegotiatedChildResponsibilityContract& Right)
				{
					return AreProofExecutionNegotiatedResponsibilityContractsEquivalent(Left, Right);
				}))
			{
				return true;
			}

			if (!ChildRequestMayEmitNestedResponsibilityContracts(ChildRequest))
			{
				return false;
			}

			const FString NestedChildPathPrefix =
				ChildRequest.RegionDebugPath + TEXT("/");
			for (const FLayoutNegotiatedChildResponsibilityContract& ActualContract : ActualContracts)
			{
				if (ContainsEquivalentValue(
					ExpectedContracts,
					ActualContract,
					[](const FLayoutNegotiatedChildResponsibilityContract& Left, const FLayoutNegotiatedChildResponsibilityContract& Right)
					{
						return AreProofExecutionNegotiatedResponsibilityContractsEquivalent(Left, Right);
					}))
				{
					continue;
				}

				if (!ActualContract.ParentRegionDebugPath.StartsWith(ChildRequest.RegionDebugPath)
					|| !ActualContract.ChildRegionDebugPath.StartsWith(NestedChildPathPrefix))
				{
					return false;
				}
			}

			return true;
		}

		static TArray<FLayoutOwnedSeamJunctionRequirement> BuildExpectedJunctionRequirements(
			const FNegotiatedProofScheduleContract& ProofContract)
		{
			TArray<FLayoutOwnedSeamJunctionRequirement> ExpectedRequirements;
			for (const FOwnedSeamJunctionRequirement& Requirement : ProofContract.SeamPlan.JunctionRequirements)
			{
				ExpectedRequirements.Add(ConvertOwnedSeamJunctionRequirement(Requirement));
			}
			return ExpectedRequirements;
		}

		static const FLayoutDroppedOptionalChildRecord* FindExpectedDroppedOptionalChildRecord(
			const FNegotiatedProofScheduleContract& ProofContract,
			const FLayoutId DropDecisionId)
		{
			return ProofContract.OptionalChildPlan.DroppedOptionalChildren.FindByPredicate(
				[DropDecisionId](const FLayoutDroppedOptionalChildRecord& Record)
				{
					return Record.DropDecisionId == DropDecisionId;
				});
		}

		static TArray<FString> BuildExpectedProofResultRegionPaths(
			const FNegotiatedProofScheduleContract& ProofContract)
		{
			TArray<FString> RegionPaths;
			RegionPaths.Add(ProofContract.ParentRequest.RegionDebugPath);
			for (const FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
			{
				RegionPaths.Add(ChildRequest.RegionDebugPath);
			}
			return RegionPaths;
		}

		static bool IsRecursiveVerticalAccessSummaryPopulated(
			const FLayoutRecursiveVerticalAccessSummary& Summary)
		{
			return Summary.RequiredHostProviderCount > 0
				|| Summary.CountedParentProviderCount > 0
				|| !Summary.CountedParentVerticalAccessCells.IsEmpty()
				|| !Summary.RetainedParentRouteSupportVerticalAccessCells.IsEmpty()
				|| !Summary.CountedChildRegionDebugPaths.IsEmpty()
				|| !Summary.LocalOnlyChildRegionDebugPaths.IsEmpty()
				|| !Summary.UnusableContributingChildRegionDebugPaths.IsEmpty()
				|| !Summary.ExtraContributingChildRegionDebugPaths.IsEmpty()
				|| !Summary.FailureReason.IsEmpty();
		}

		static FLayoutRecursiveVerticalAccessSummary BuildExpectedRecursiveVerticalAccessSummary(
			const FNegotiatedProofScheduleContract& ProofContract)
		{
			return ProofContract.PlannedRecursiveVerticalAccessSummary;
		}

		/** Accepts exact host-cell refinement only within frozen module-admitted groups. */
		static bool IsProofSelectedParentHostRefinementValid(
			const FNegotiatedProofScheduleContract& ProofContract,
			const FLayoutRecursiveVerticalAccessSummary& ActualSummary)
		{
			TSet<FIntVector> ActualParentHostCells;
			for (const FIntVector& Cell : ActualSummary.CountedParentVerticalAccessCells)
			{
				ActualParentHostCells.Add(Cell);
			}
			for (const FIntVector& Cell : ActualSummary.RetainedParentRouteSupportVerticalAccessCells)
			{
				ActualParentHostCells.Add(Cell);
			}
			TSet<FIntVector> ExpectedParentHostCells;
			for (const FIntVector& Cell :
				ProofContract.PlannedRecursiveVerticalAccessSummary.CountedParentVerticalAccessCells)
			{
				ExpectedParentHostCells.Add(Cell);
			}
			for (const FIntVector& Cell :
				ProofContract.PlannedRecursiveVerticalAccessSummary.RetainedParentRouteSupportVerticalAccessCells)
			{
				ExpectedParentHostCells.Add(Cell);
			}
			const TArray<FLayoutPlannedCell>& ParentPlannedCells =
				!ProofContract.ParentRequest.PrecomputedPlannedCells.IsEmpty()
					? ProofContract.ParentRequest.PrecomputedPlannedCells
					: ProofContract.ParentRequest.PlannedCells;
			for (const FIntVector& Cell : ActualParentHostCells)
			{
				const bool bInFrozenGroup =
					ProofContract.ParentRequest.VerticalAccessHostGroups.ContainsByPredicate(
						[&Cell](const FLayoutVerticalAccessHostGroup& Group)
						{
							return Group.Options.ContainsByPredicate(
								[&Cell](const FLayoutVerticalAccessHostOption& Option)
								{
									return Option.LowerCell == Cell;
								});
						});
				FString AdmissionFailure;
				if (!bInFrozenGroup
					&& !LayoutProfileSolverInternal::DoesPreparedVerticalAccessHostAdmitCandidatePair(
						ProofContract.ParentRequest,
						ParentPlannedCells,
						Cell,
						AdmissionFailure))
				{
					return false;
				}
			}
			return ActualSummary.CountedParentVerticalAccessCells.Num()
					>= ActualSummary.CountedParentProviderCount
				&& ActualParentHostCells.Num() >= ExpectedParentHostCells.Num();
		}

		/** Rebinds parent-owned host cells to the already-validated proof-selected host assignment. */
		static TArray<FLayoutNegotiatedChildResponsibilityContract> BuildExpectedParentResponsibilityContractsAfterHostRefinement(
			const FNegotiatedProofScheduleContract& ProofContract,
			const FLayoutRecursiveVerticalAccessSummary& ActualSummary)
		{
			TArray<FLayoutNegotiatedChildResponsibilityContract> ExpectedContracts =
				ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts;
			for (FLayoutNegotiatedChildResponsibilityContract& Contract : ExpectedContracts)
			{
				if (Contract.HostVerticalAccessResponsibility
						!= ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned
					&& Contract.HostVerticalAccessResponsibility
						!= ELayoutNegotiatedHostVerticalAccessResponsibility::Composed)
				{
					continue;
				}

				Contract.CountedParentProviderCount = ActualSummary.CountedParentProviderCount;
				Contract.CountedParentVerticalAccessCells = ActualSummary.CountedParentVerticalAccessCells;
				Contract.RetainedParentRouteSupportVerticalAccessCells =
					ActualSummary.RetainedParentRouteSupportVerticalAccessCells;
			}
			return ExpectedContracts;
		}

		static TArray<FString> BuildExpectedProofChildRegionPaths(
			const FNegotiatedProofScheduleContract& ProofContract)
		{
			TArray<FString> ChildRegionPaths;
			ChildRegionPaths.Reserve(ProofContract.ChildRequests.Num());
			for (const FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
			{
				ChildRegionPaths.Add(ChildRequest.RegionDebugPath);
			}

			ChildRegionPaths.Sort();
			return ChildRegionPaths;
		}

		static bool AreRecursiveVerticalAccessChildPathsValid(
			const TArray<FString>& SummaryPaths,
			const TArray<FString>& ExpectedChildPaths)
		{
			for (const FString& SummaryPath : SummaryPaths)
			{
				if (!ExpectedChildPaths.Contains(SummaryPath))
				{
					return false;
				}
			}

			return true;
		}

		static void NormalizeRegionResultRequestCarriedDiagnostics(
			FLayoutRegionSolveResult& InOutRegionResult,
			const FLayoutRegionSolveRequest& Request)
		{
			InOutRegionResult.ProofRecords = Request.ProofRecords;
			InOutRegionResult.ValidationAssertions = Request.ValidationAssertions;
		}

		static void NormalizeScheduleResultRequestCarriedDiagnostics(
			FLayoutRegionSolveScheduleResult& InOutScheduleResult,
			const FLayoutRegionSolveRequest& ParentRequest,
			const TArray<FLayoutRegionSolveRequest>& ChildRequests)
		{
			if (FLayoutRegionSolveResult* ParentResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&ParentRequest](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == ParentRequest.RegionDebugPath;
					}))
			{
				NormalizeRegionResultRequestCarriedDiagnostics(
					*ParentResult,
					ParentRequest);
			}

			for (const FLayoutRegionSolveRequest& ChildRequest : ChildRequests)
			{
				FLayoutRegionSolveResult* ChildResult =
					InOutScheduleResult.RegionResults.FindByPredicate(
						[&ChildRequest](const FLayoutRegionSolveResult& Result)
						{
							return Result.RegionDebugPath == ChildRequest.RegionDebugPath;
						});
				if (ChildResult == nullptr)
				{
					continue;
				}

				NormalizeRegionResultRequestCarriedDiagnostics(
					*ChildResult,
					ChildRequest);
			}
		}

		static void NormalizeScheduleResultRequestCarriedDiagnostics(
			FLayoutRegionSolveScheduleResult& InOutScheduleResult,
			const FProofJobPlan& ProofJobPlan)
		{
			NormalizeScheduleResultRequestCarriedDiagnostics(
				InOutScheduleResult,
				ProofJobPlan.ParentRequest,
				ProofJobPlan.ChildRequests);
		}

		static void NormalizeMergedSolveResultRequestOwnedSurface(
			FLayoutRegionSolveScheduleResult& InOutScheduleResult,
			const FLayoutRegionSolveRequest& ParentRequest)
		{
			const FIntVector ContentSetSharedCellSize =
				ParentRequest.ContentSetSnapshot.SharedCellSizeInBlocks;
			const FIntVector ResolvedCatalogSharedCellSize =
				ParentRequest.ModuleCatalog.SharedCellSizeInBlocks;
			const FIntVector RequestSharedCellSize =
				ContentSetSharedCellSize != FIntVector::ZeroValue
					? ContentSetSharedCellSize
					: ResolvedCatalogSharedCellSize;
			if (RequestSharedCellSize != FIntVector::ZeroValue)
			{
				InOutScheduleResult.MergedSolveResult.SharedCellSizeInBlocks =
					RequestSharedCellSize;
			}

			InOutScheduleResult.MergedSolveResult.TemplatePlacementZOffsetBlocks =
				ParentRequest.TemplatePlacementZOffsetBlocks;
			InOutScheduleResult.MergedSolveResult.RootPlacementKind =
				ParentRequest.RootPlacementKind;
			if (const FLayoutRegionSolveResult* RootResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[](const FLayoutRegionSolveResult& Result)
					{
						return Result.SourceContentEntryId.IsNone();
					}))
			{
				InOutScheduleResult.MergedSolveResult.ResolvedTerrainAlignmentLevel =
					RootResult->SolveResult.ResolvedTerrainAlignmentLevel;
			}
			InOutScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy =
				ParentRequest.WorldBindingPlacementPolicy;
			InOutScheduleResult.MergedSolveResult.ValidationAssertions =
				ParentRequest.ValidationAssertions;
			InOutScheduleResult.MergedSolveResult.SteppedTerrainSupportMap =
				ParentRequest.SteppedTerrainSupportMap;
			if (InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
			{
				InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions =
					ParentRequest.ForcedPlacementBundleInsertions;
			}
			if (InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
			{
				InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints =
					ParentRequest.RequiredRouteConstraints;
			}
		}

		static bool AreArraysDisjoint(
			const TArray<FString>& Left,
			const TArray<FString>& Right)
		{
			for (const FString& LeftValue : Left)
			{
				if (Right.Contains(LeftValue))
				{
					return false;
				}
			}

			return true;
		}

		static bool ValidateDroppedOptionalChildResidualCells(
			const FNegotiatedProofScheduleContract& ProofContract,
			const TArray<FLayoutResidualCellRecord>& ResidualCells,
			const TCHAR* UnexpectedDropFailureReason,
			const TCHAR* AttributionFailureReason,
			FString& OutFailureReason)
		{
			for (const FLayoutResidualCellRecord& ResidualCell : ResidualCells)
			{
				if (ResidualCell.Source != ELayoutResidualCellSource::DroppedOptionalChild)
				{
					continue;
				}

				const FLayoutDroppedOptionalChildRecord* const ExpectedDropRecord =
					FindExpectedDroppedOptionalChildRecord(
						ProofContract,
						ResidualCell.RelatedDropDecisionId);
				if (ExpectedDropRecord == nullptr)
				{
					OutFailureReason = UnexpectedDropFailureReason;
					return false;
				}

				if (ResidualCell.SourceRegionDebugPath != ExpectedDropRecord->ChildRegionDebugPath
					|| ResidualCell.SourceContentEntryId != ExpectedDropRecord->SourceContentEntryId)
				{
					OutFailureReason = AttributionFailureReason;
					return false;
				}
			}

			return true;
		}

		/**
		 * Rewrite-owned merged auditor boundary. This currently performs the exact
		 * frozen-contract preservation pass after compatibility execution while
		 * richer merged-audit responsibilities remain a later phase.
		 */
		struct FLayoutMergedScheduleAuditor
		{
			static bool Validate(
				const FRecursiveScheduleSolveContext& SolveContext,
				const FNegotiatedProofScheduleContract& ProofContract,
				const FLayoutRegionSolveScheduleResult& ScheduleResult,
				FString& OutFailureReason);
		};

		static bool TryBuildIndependentProofScheduleResult(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FProofJobPlan& ProofJobPlan,
			const FNegotiatedProofScheduleContract& ProofContract,
			FLayoutRegionSolveScheduleResult& OutScheduleResult,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();

			FProofJobPlan EffectiveProofJobPlan = ProofJobPlan;
			if (!AppendProofExecutionNegotiatedContractEvidenceToRequest(
				EffectiveProofJobPlan.ParentRequest,
				OutFailureReason))
			{
				return false;
			}
			FLayoutRegionSolveResult IndependentParentResult =
				FLayoutProfileSolver::SolveRegion(EffectiveProofJobPlan.ParentRequest);
			if (!IndependentParentResult.SolveResult.bSucceeded)
			{
				OutFailureReason = IndependentParentResult.SolveResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Independent proof execution failed for parent region '%s'."),
						*EffectiveProofJobPlan.ParentRequest.RegionDebugPath)
					: IndependentParentResult.SolveResult.FailureReason;
				OutScheduleResult.MergedSolveResult.Placements = IndependentParentResult.SolveResult.Placements;
				OutScheduleResult.MergedSolveResult.PlannedCells = IndependentParentResult.SolveResult.PlannedCells;
				OutScheduleResult.MergedSolveResult.FootprintSize = IndependentParentResult.SolveResult.FootprintSize;
				OutScheduleResult.RegionResults.Add(IndependentParentResult);
				return false;
			}

			FString ParentAnchorFailureReason;
			if (!LayoutProfileSolverInternal::ValidateCommittedEndpointAnchorCoverageOnPlacementsForSchedule(
				EffectiveProofJobPlan.ParentRequest.RegionDebugPath,
				EffectiveProofJobPlan.ParentRequest.RegionCellOffset,
				IndependentParentResult.SolveResult.Placements,
				EffectiveProofJobPlan.ParentRequest.CommittedEndpointAnchors,
				ParentAnchorFailureReason))
			{
				ParentAnchorFailureReason += FString::Printf(
					TEXT(" PlannedEntries=[%s]."),
					*FString::JoinBy(
						EffectiveProofJobPlan.ParentRequest.PlannedCells.FilterByPredicate(
							[](const FLayoutPlannedCell& Cell){ return Cell.Intent == ELayoutCellIntent::Entry; }),
						TEXT("|"),
						[](const FLayoutPlannedCell& Cell){ return Cell.Cell.ToString(); }));
				bool bAllCommittedAnchorsHaveExportedBoundaryCoverage = true;
				for (const FLayoutCommittedEndpointAnchor& Anchor : EffectiveProofJobPlan.ParentRequest.CommittedEndpointAnchors)
				{
					const bool bHasBoundaryCoverage = IndependentParentResult.ExportedBoundaryPoints.ContainsByPredicate(
						[&Anchor](const FLayoutSolveBoundaryPoint& BoundaryPoint)
						{
							return LayoutRegionScheduleSolverPrivate::DoesBoundaryPointSatisfyCommittedAnchor(BoundaryPoint, Anchor);
						});
					if (!bHasBoundaryCoverage)
					{
						bAllCommittedAnchorsHaveExportedBoundaryCoverage = false;
						break;
					}
				}

				if (!bAllCommittedAnchorsHaveExportedBoundaryCoverage)
				{
					OutFailureReason = ParentAnchorFailureReason;
					return false;
				}
			}

			TArray<FLayoutRegionSolveResult> IndependentChildResults;
			IndependentChildResults.Reserve(EffectiveProofJobPlan.StableMergeOrder.Num());

			// Freeze every child from parent result only. Parent CSP already
			// validated child boundaries collectively; sibling faces remain
			// unplanned until post-merge seam resolution.
			struct FPreFrozenChildRequest
			{
				FString ChildRegionDebugPath;
				FLayoutRegionSolveRequest EffectiveRequest;
			};
			TArray<FPreFrozenChildRequest> PreFrozenChildren;
			PreFrozenChildren.Reserve(EffectiveProofJobPlan.StableMergeOrder.Num());

			for (const FString& ChildRegionDebugPath : EffectiveProofJobPlan.StableMergeOrder)
			{
				FLayoutRegionSolveRequest* ChildProofRequest =
					EffectiveProofJobPlan.ChildRequests.FindByPredicate(
						[&ChildRegionDebugPath](const FLayoutRegionSolveRequest& Request)
						{
							return Request.RegionDebugPath == ChildRegionDebugPath;
						});
				if (ChildProofRequest == nullptr)
				{
					OutFailureReason = FString::Printf(
						TEXT("Independent proof execution could not find child request for region '%s'."),
						*ChildRegionDebugPath);
					return false;
				}

				FLayoutRegionSolveRequest EffectiveChildProofRequest = *ChildProofRequest;
				const FLayoutDirectChildRegionCommitment* DirectChildCommitment =
					FindDirectChildCommitment(ProofContract, ChildRegionDebugPath);
				if (DirectChildCommitment != nullptr)
				{
					FLayoutDirectChildRegionCommitment MutableCommitment = *DirectChildCommitment;
					if (MutableCommitment.FrozenBoundaryPoints.IsEmpty())
					{
						OutFailureReason = FString::Printf(
							TEXT("Independent child proof for region '%s' is missing its pre-parent boundary certificate."),
							*ChildRegionDebugPath);
						return false;
					}
					FString ChildRequestFailureReason;
					if (!LayoutProfileSolverInternal::BuildIndependentCommittedChildProofRequestFromHandoff(
							MutableCommitment,
							*ChildProofRequest,
							EffectiveChildProofRequest,
							ChildRequestFailureReason))
					{
						OutFailureReason = ChildRequestFailureReason;
						return false;
					}
				}
				if (!AppendProofExecutionNegotiatedContractEvidenceToRequest(
					EffectiveChildProofRequest,
					OutFailureReason))
				{
					return false;
				}
				*ChildProofRequest = EffectiveChildProofRequest;

				PreFrozenChildren.Add({
					ChildRegionDebugPath,
					MoveTemp(EffectiveChildProofRequest)
				});
			}

			// Prove pre-frozen children inline on the current solve worker.
			// Local project fix: the outer root solve already runs under the runtime
			// dispatcher, whose mutable job table is game-thread-only. Nesting another
			// dispatcher here asserted on live worker threads while still executing
			// synchronously. Dedicated child-task dispatch belongs at a thread-safe boundary.
			for (const FPreFrozenChildRequest& PreFrozenChild : PreFrozenChildren)
			{
				FLayoutRegionSolveResult ChildResult;
				FString ChildFailureReason;
				if (!LayoutProfileSolverInternal::BuildIndependentChildProofResult(
					PreFrozenChild.EffectiveRequest,
					ChildResult,
					ChildFailureReason))
				{
					OutFailureReason = MoveTemp(ChildFailureReason);
					return false;
				}
				IndependentChildResults.Add(MoveTemp(ChildResult));
			}

			FString IndependentScheduleFailureReason;
			if (!LayoutProfileSolverInternal::BuildIndependentProofScheduleResultFromContract(
				ProofContract,
				MoveTemp(IndependentParentResult),
				MoveTemp(IndependentChildResults),
				OutScheduleResult,
				IndependentScheduleFailureReason))
			{
				OutFailureReason = IndependentScheduleFailureReason;
				return false;
			}

			OutScheduleResult.PlannedJunctionRequirements =
				BuildExpectedJunctionRequirements(ProofContract);
			OutScheduleResult.bUsedIndependentDeferredProofSplit = true;
			NormalizeScheduleResultRequestCarriedDiagnostics(
				OutScheduleResult,
				EffectiveProofJobPlan);
			NormalizeMergedSolveResultRequestOwnedSurface(
				OutScheduleResult,
				EffectiveProofJobPlan.ParentRequest);
			if (!ApplyPostStructuralScheduleHandoffs(
				OutScheduleResult,
				ProofContract,
				OutFailureReason))
			{
				return false;
			}

			FString ValidationFailureReason;
			if (!FLayoutMergedScheduleAuditor::Validate(
				SolveContext,
				ProofContract,
				OutScheduleResult,
				ValidationFailureReason))
			{
				OutFailureReason = ValidationFailureReason;
				return false;
			}

			return true;
		}

		static FLayoutRegionSolveScheduleResult BuildFailedIndependentProofScheduleResult(
			const FString& FailureReason)
		{
			FLayoutRegionSolveScheduleResult FailedScheduleResult;
			FailedScheduleResult.bSucceeded = false;
			FailedScheduleResult.FailureReason = FailureReason;
			FailedScheduleResult.MergedSolveResult.bSucceeded = false;
			FailedScheduleResult.MergedSolveResult.FailureReason = FailureReason;
			return FailedScheduleResult;
		}

		/**
		 * Rewrite-owned proof executor boundary. It executes negotiated proof
		 * directly from the frozen proof contract instead of delegating the solve
		 * to the temporary legacy synchronous adapter.
		 */
		struct FLayoutProofExecutor
		{
			static FLayoutRegionSolveScheduleRequest BuildCompatibilityScheduleRequest(
				const FNegotiatedProofScheduleContract& ProofContract)
			{
				return BuildLegacySynchronousProofScheduleRequest(
					BuildProofJobPlan(ProofContract),
					ProofContract);
			}

			static FLayoutRegionSolveScheduleResult Execute(
				const FRecursiveScheduleSolveContext& SolveContext,
				const FNegotiatedProofScheduleContract& ProofContract)
			{
				const FProofJobPlan ProofJobPlan = BuildProofJobPlan(ProofContract);
				FLayoutRegionSolveScheduleResult IndependentScheduleResult;
				FString IndependentFailureReason;
				if (TryBuildIndependentProofScheduleResult(
					SolveContext,
					ProofJobPlan,
					ProofContract,
					IndependentScheduleResult,
					IndependentFailureReason))
				{
					return IndependentScheduleResult;
				}

				if (!IndependentScheduleResult.MergedSolveResult.Placements.IsEmpty())
				{
					// Preserve partial placements from the parent proof so callers
					// can debug-draw or partially apply the solve.
					IndependentScheduleResult.bSucceeded = false;
					IndependentScheduleResult.FailureReason = IndependentFailureReason;
					IndependentScheduleResult.MergedSolveResult.bSucceeded = false;
					IndependentScheduleResult.MergedSolveResult.FailureReason = IndependentFailureReason;
					return IndependentScheduleResult;
				}

				return BuildFailedIndependentProofScheduleResult(
					IndependentFailureReason.IsEmpty()
						? TEXT("Independent proof execution failed.")
						: IndependentFailureReason);
			}
		};
	}

	FLayoutRegionSolveScheduleRequest BuildProofExecutionCompatibilityScheduleRequest(
		const FNegotiatedProofScheduleContract& ProofContract)
	{
		return FLayoutProofExecutor::BuildCompatibilityScheduleRequest(ProofContract);
	}

	bool BuildIndependentProofScheduleResultFromPreparedJobPlan(
		const FProofJobPlan& PreparedProofJobPlan,
		const FNegotiatedProofScheduleContract& ProofContract,
		FLayoutRegionSolveResult&& ParentProofResult,
		TArray<FLayoutRegionSolveResult>&& ChildProofResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason)
	{
		return BuildIndependentProofScheduleResultFromPreparedRequests(
			PreparedProofJobPlan.ParentRequest,
			PreparedProofJobPlan.ChildRequests,
			ProofContract,
			MoveTemp(ParentProofResult),
			MoveTemp(ChildProofResults),
			OutScheduleResult,
			OutFailureReason);
	}

	bool BuildIndependentProofScheduleResultFromPreparedRequests(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutRegionSolveRequest>& ChildRequests,
		const FNegotiatedProofScheduleContract& ProofContract,
		FLayoutRegionSolveResult&& ParentProofResult,
		TArray<FLayoutRegionSolveResult>&& ChildProofResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason)
	{
		if (!LayoutProfileSolverInternal::BuildIndependentProofScheduleResultFromContract(
				ProofContract,
				MoveTemp(ParentProofResult),
				MoveTemp(ChildProofResults),
				OutScheduleResult,
				OutFailureReason))
		{
			return false;
		}

		OutScheduleResult.PlannedJunctionRequirements =
			BuildExpectedJunctionRequirements(ProofContract);
		OutScheduleResult.bUsedIndependentDeferredProofSplit = true;
		NormalizeScheduleResultRequestCarriedDiagnostics(
			OutScheduleResult,
			ParentRequest,
			ChildRequests);
		NormalizeMergedSolveResultRequestOwnedSurface(
			OutScheduleResult,
			ParentRequest);
		if (!ApplyPostStructuralScheduleHandoffs(
				OutScheduleResult,
				ProofContract,
				OutFailureReason))
		{
			return false;
		}

		FRecursiveScheduleSolveContext ValidationContext;
		return FLayoutMergedScheduleAuditor::Validate(
			ValidationContext,
			ProofContract,
			OutScheduleResult,
			OutFailureReason);
	}

	FLayoutRegionSolveScheduleResult ExecuteNegotiatedProofSchedule(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiatedProofScheduleContract& ProofContract)
	{
		return FLayoutProofExecutor::Execute(SolveContext, ProofContract);
	}

	bool ValidateNegotiatedScheduleResult(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiatedProofScheduleContract& ProofContract,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason)
	{
		return FLayoutMergedScheduleAuditor::Validate(
			SolveContext,
			ProofContract,
			ScheduleResult,
			OutFailureReason);
	}

	bool FLayoutMergedScheduleAuditor::Validate(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiatedProofScheduleContract& ProofContract,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Merge_Audit, STAT_PorismLayout_MergeAudit);
		static_cast<void>(SolveContext);
		if (!ScheduleResult.bSucceeded)
		{
			OutFailureReason = ScheduleResult.FailureReason.IsEmpty()
				? TEXT("Negotiated proof schedule failed.")
				: ScheduleResult.FailureReason;
			return false;
		}

		const FLayoutRegionSolveResult* const ParentResult = ScheduleResult.RegionResults.FindByPredicate(
			[&ProofContract](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == ProofContract.ParentRequest.RegionDebugPath;
			});
		if (ParentResult == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiated proof schedule did not return the parent result for region '%s'."),
				*ProofContract.ParentRequest.RegionDebugPath);
			return false;
		}

		if (!AreUnorderedArraysEquivalent(
			ScheduleResult.RegionResults,
			BuildExpectedProofResultRegionPaths(ProofContract),
			[](const FLayoutRegionSolveResult& Result, const FString& ExpectedRegionPath)
			{
				return Result.RegionDebugPath == ExpectedRegionPath;
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the proved region-result set.");
			return false;
		}

		if (!AreUnorderedArraysEquivalent(
			ScheduleResult.PlannedJunctionRequirements,
			BuildExpectedJunctionRequirements(ProofContract),
			[](const FLayoutOwnedSeamJunctionRequirement& Left, const FLayoutOwnedSeamJunctionRequirement& Right)
			{
				return AreProofExecutionOwnedSeamJunctionRequirementsEquivalent(Left, Right);
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the planned owner-side junction requirement set.");
			return false;
		}

		if (ProofContract.SeamPlan.bPlannedPartitionSeamsAreAuthoritative
			&& !AreUnorderedArraysEquivalent(
				ScheduleResult.MergedSolveResult.PartitionSeams,
				ProofContract.SeamPlan.PlannedPartitionSeams,
				[](const FLayoutPartitionSeamRecord& Left, const FLayoutPartitionSeamRecord& Right)
				{
					return AreProofExecutionPartitionSeamRecordsEquivalent(Left, Right);
				}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the authoritative planned seam set.");
			return false;
		}

		{
			const FLayoutRecursiveVerticalAccessSummary ExpectedSummary =
				BuildExpectedRecursiveVerticalAccessSummary(ProofContract);
			const bool bExpectedSummaryPopulated =
				IsRecursiveVerticalAccessSummaryPopulated(ExpectedSummary);
			const bool bActualSummaryPopulated =
				IsRecursiveVerticalAccessSummaryPopulated(
					ScheduleResult.RecursiveVerticalAccessSummary);
			const TArray<FString> ExpectedChildRegionPaths =
				BuildExpectedProofChildRegionPaths(ProofContract);
			const bool bParentHostCellsMatchFrozenSummary =
				AreProofExecutionIntVectorsEquivalent(
					ScheduleResult.RecursiveVerticalAccessSummary.CountedParentVerticalAccessCells,
					ExpectedSummary.CountedParentVerticalAccessCells)
				&& AreProofExecutionIntVectorsEquivalent(
					ScheduleResult.RecursiveVerticalAccessSummary.RetainedParentRouteSupportVerticalAccessCells,
					ExpectedSummary.RetainedParentRouteSupportVerticalAccessCells);
			const bool bParentHostRefinementValid =
				!bParentHostCellsMatchFrozenSummary
				&& IsProofSelectedParentHostRefinementValid(
					ProofContract,
					ScheduleResult.RecursiveVerticalAccessSummary);
			if (bExpectedSummaryPopulated != bActualSummaryPopulated
				|| ScheduleResult.RecursiveVerticalAccessSummary.RequiredHostProviderCount != ExpectedSummary.RequiredHostProviderCount
				|| ScheduleResult.RecursiveVerticalAccessSummary.ResolvedHostProviderCount != ExpectedSummary.ResolvedHostProviderCount
				|| ScheduleResult.RecursiveVerticalAccessSummary.CountedParentProviderCount != ExpectedSummary.CountedParentProviderCount
				|| (!bParentHostCellsMatchFrozenSummary && !bParentHostRefinementValid)
				|| !AreUnorderedArraysEquivalent(
					ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths,
					ExpectedSummary.CountedChildRegionDebugPaths,
					[](const FString& Left, const FString& Right)
					{
						return Left == Right;
					})
				|| !AreUnorderedArraysEquivalent(
					ScheduleResult.RecursiveVerticalAccessSummary.LocalOnlyChildRegionDebugPaths,
					ExpectedSummary.LocalOnlyChildRegionDebugPaths,
					[](const FString& Left, const FString& Right)
					{
						return Left == Right;
					})
				|| !AreUnorderedArraysEquivalent(
					ScheduleResult.RecursiveVerticalAccessSummary.UnusableContributingChildRegionDebugPaths,
					ExpectedSummary.UnusableContributingChildRegionDebugPaths,
					[](const FString& Left, const FString& Right)
					{
						return Left == Right;
					})
				|| !AreUnorderedArraysEquivalent(
					ScheduleResult.RecursiveVerticalAccessSummary.ExtraContributingChildRegionDebugPaths,
					ExpectedSummary.ExtraContributingChildRegionDebugPaths,
					[](const FString& Left, const FString& Right)
					{
						return Left == Right;
					})
				|| ScheduleResult.RecursiveVerticalAccessSummary.FailureReason != ExpectedSummary.FailureReason
				|| !AreRecursiveVerticalAccessChildPathsValid(
					ScheduleResult.RecursiveVerticalAccessSummary.LocalOnlyChildRegionDebugPaths,
					ExpectedChildRegionPaths)
				|| !AreRecursiveVerticalAccessChildPathsValid(
					ScheduleResult.RecursiveVerticalAccessSummary.UnusableContributingChildRegionDebugPaths,
					ExpectedChildRegionPaths)
				|| !AreRecursiveVerticalAccessChildPathsValid(
					ScheduleResult.RecursiveVerticalAccessSummary.ExtraContributingChildRegionDebugPaths,
					ExpectedChildRegionPaths)
				|| !AreArraysDisjoint(
					ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths,
					ScheduleResult.RecursiveVerticalAccessSummary.LocalOnlyChildRegionDebugPaths)
				|| !AreArraysDisjoint(
					ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths,
					ScheduleResult.RecursiveVerticalAccessSummary.UnusableContributingChildRegionDebugPaths)
				|| !AreArraysDisjoint(
					ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths,
					ScheduleResult.RecursiveVerticalAccessSummary.ExtraContributingChildRegionDebugPaths))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the recursive vertical-access ownership summary after negotiation finished. expected(required=%d resolved=%d parent=%d counted=[%s] retained=[%s]) actual(required=%d resolved=%d parent=%d counted=[%s] retained=[%s])."),
					ExpectedSummary.RequiredHostProviderCount,
					ExpectedSummary.ResolvedHostProviderCount,
					ExpectedSummary.CountedParentProviderCount,
					*FString::JoinBy(ExpectedSummary.CountedParentVerticalAccessCells, TEXT(","), [](const FIntVector& Cell) { return Cell.ToString(); }),
					*FString::JoinBy(ExpectedSummary.RetainedParentRouteSupportVerticalAccessCells, TEXT(","), [](const FIntVector& Cell) { return Cell.ToString(); }),
					ScheduleResult.RecursiveVerticalAccessSummary.RequiredHostProviderCount,
					ScheduleResult.RecursiveVerticalAccessSummary.ResolvedHostProviderCount,
					ScheduleResult.RecursiveVerticalAccessSummary.CountedParentProviderCount,
					*FString::JoinBy(ScheduleResult.RecursiveVerticalAccessSummary.CountedParentVerticalAccessCells, TEXT(","), [](const FIntVector& Cell) { return Cell.ToString(); }),
					*FString::JoinBy(ScheduleResult.RecursiveVerticalAccessSummary.RetainedParentRouteSupportVerticalAccessCells, TEXT(","), [](const FIntVector& Cell) { return Cell.ToString(); }));
				return false;
			}
		}

		for (const FLayoutDirectChildRegionCommitment& DirectCommitment :
			ProofContract.DirectChildCommitments)
		{
			for (const FLayoutZoneFeatureProviderCommitment& ExpectedCommitment :
				DirectCommitment.ZoneFeatureProviderCommitments)
			{
				const auto HasExactCommitment = [&ExpectedCommitment](
					const TArray<FLayoutZoneFeatureProviderCommitment>& Commitments,
					const FIntVector& ExpectedCell)
				{
					return Commitments.ContainsByPredicate(
						[&ExpectedCommitment, &ExpectedCell](const FLayoutZoneFeatureProviderCommitment& Actual)
						{
							return Actual.ProviderCommitmentId == ExpectedCommitment.ProviderCommitmentId
								&& Actual.RequirementId == ExpectedCommitment.RequirementId
								&& Actual.SourceRegionDebugPath == ExpectedCommitment.SourceRegionDebugPath
								&& Actual.SourceContentEntryId == ExpectedCommitment.SourceContentEntryId
								&& Actual.Cell == ExpectedCell
								&& Actual.ModuleLevelIndex == ExpectedCommitment.ModuleLevelIndex
								&& Actual.TerrainStageIndex == ExpectedCommitment.TerrainStageIndex;
						});
				};
				if (!HasExactCommitment(
						ParentResult->SolveResult.ZoneFeatureProviderCommitments,
						ExpectedCommitment.Cell)
					|| !HasExactCommitment(
						ScheduleResult.MergedSolveResult.ZoneFeatureProviderCommitments,
						ExpectedCommitment.Cell + ParentResult->RegionCellOffset))
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated proof schedule changed direct-child zone-feature commitment '%s'."),
						*ExpectedCommitment.ProviderCommitmentId.ToString());
					return false;
				}
			}
		}

		if (!IsUnorderedArraySubsetEquivalent(
			ScheduleResult.MergedSolveResult.DroppedOptionalChildren,
			ProofContract.OptionalChildPlan.DroppedOptionalChildren,
			[](const FLayoutDroppedOptionalChildRecord& Left, const FLayoutDroppedOptionalChildRecord& Right)
			{
				return AreProofExecutionDroppedOptionalChildRecordsEquivalent(Left, Right);
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule introduced unexpected merged optional-drop records after negotiation finished.");
			return false;
		}

		if (!ValidateDroppedOptionalChildResidualCells(
			ProofContract,
			ScheduleResult.MergedSolveResult.ResidualUnoccupiedCells,
			TEXT("Negotiated proof schedule introduced dropped-child residual cells for an unexpected optional-drop decision."),
			TEXT("Negotiated proof schedule changed dropped-child residual attribution after negotiation finished."),
			OutFailureReason))
		{
			return false;
		}

		const TArray<FLayoutNegotiatedChildResponsibilityContract> ExpectedParentContracts =
			BuildExpectedParentResponsibilityContractsAfterHostRefinement(
				ProofContract,
				ScheduleResult.RecursiveVerticalAccessSummary);
		if (!AreUnorderedArraysEquivalent(
			ParentResult->NegotiatedChildResponsibilityContracts,
			ExpectedParentContracts,
			[](const FLayoutNegotiatedChildResponsibilityContract& Left, const FLayoutNegotiatedChildResponsibilityContract& Right)
			{
				return AreProofExecutionNegotiatedResponsibilityContractsEquivalent(Left, Right);
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the parent responsibility-contract set.");
			return false;
		}

		if (!AreUnorderedArraysEquivalent(
			ParentResult->CommittedEndpointAnchors,
			ProofContract.ParentRequest.CommittedEndpointAnchors,
			[](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
			{
				return AreProofExecutionCommittedEndpointAnchorsEquivalent(Left, Right);
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the parent root-endpoint commitment set.");
			return false;
		}

		if (ParentResult->bDroppedAsOptionalChild
			|| !ParentResult->OptionalDropDecisionId.IsNone()
			|| !ParentResult->OptionalDropReason.IsEmpty())
		{
			OutFailureReason = TEXT("Negotiated proof schedule unexpectedly treated the proved parent result as an optional drop.");
			return false;
		}

		if (!ParentResult->SolveResult.DroppedOptionalChildren.IsEmpty())
		{
			OutFailureReason = TEXT("Negotiated proof schedule introduced optional-drop records on the proved parent result after negotiation finished.");
			return false;
		}

		if (ParentResult->RegionCellOffset != ProofContract.ParentRequest.RegionCellOffset)
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the parent region-cell offset.");
			return false;
		}

		if (ParentResult->SourceContentEntryId != ProofContract.ParentRequest.SourceContentEntryId)
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the parent source content-entry id.");
			return false;
		}

		if (!IsUnorderedArraySubsetEquivalent(
			ProofContract.ParentRequest.ProofRecords,
			ParentResult->ProofRecords,
			[](const FLayoutProofRecord& Left, const FLayoutProofRecord& Right)
			{
				return AreProofExecutionProofRecordsEquivalent(Left, Right);
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule dropped request-carried parent proof records.");
			return false;
		}

		if (!IsUnorderedArraySubsetEquivalent(
			ProofContract.ParentRequest.ValidationAssertions,
			ParentResult->ValidationAssertions,
			[](const FLayoutValidationAssertionRecord& Left, const FLayoutValidationAssertionRecord& Right)
			{
				return AreProofExecutionValidationAssertionsEquivalent(Left, Right);
			}))
		{
			OutFailureReason = TEXT("Negotiated proof schedule dropped request-carried parent validation assertions.");
			return false;
		}

		if (!AreProofExecutionSteppedTerrainSupportMapsEquivalent(
			ParentResult->SteppedTerrainSupportMap,
			ProofContract.ParentRequest.SteppedTerrainSupportMap))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the request-carried parent stepped terrain support map.");
			return false;
		}

		if (!AreProofExecutionForcedPlacementBundleInsertionsEquivalent(
			ParentResult->ForcedPlacementBundleInsertions,
			ProofContract.ParentRequest.ForcedPlacementBundleInsertions))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the request-carried parent forced placement bundle insertions.");
			return false;
		}

		if (!AreProofExecutionRouteConstraintArraysEquivalent(
			ParentResult->RequiredRouteConstraints,
			ProofContract.ParentRequest.RequiredRouteConstraints))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the request-carried parent required route constraints.");
			return false;
		}

		if (!AreProofExecutionSteppedTerrainSupportMapsEquivalent(
			ScheduleResult.MergedSolveResult.SteppedTerrainSupportMap,
			ProofContract.ParentRequest.SteppedTerrainSupportMap))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the request-carried merged root stepped terrain support map.");
			return false;
		}

		if (!AreProofExecutionForcedPlacementBundleInsertionsEquivalent(
			ScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions,
			ProofContract.ParentRequest.ForcedPlacementBundleInsertions))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the request-carried merged root forced placement bundle insertions.");
			return false;
		}

		if (!AreProofExecutionRouteConstraintArraysEquivalent(
			ScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints,
			ProofContract.ParentRequest.RequiredRouteConstraints))
		{
			OutFailureReason = TEXT("Negotiated proof schedule changed the request-carried merged root required route constraints.");
			return false;
		}

		for (const FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
		{
			const FLayoutRegionSolveResult* const ChildResult = ScheduleResult.RegionResults.FindByPredicate(
				[&ChildRequest](const FLayoutRegionSolveResult& Result)
				{
					return Result.RegionDebugPath == ChildRequest.RegionDebugPath;
				});
			if (ChildResult == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule did not return the child result for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!ValidateChildResponsibilityContractsPreserved(
				ChildRequest,
				ChildResult->NegotiatedChildResponsibilityContracts))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the child responsibility-contract set for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			const FLayoutDirectChildRegionCommitment* const DirectCommitment =
				FindDirectChildCommitment(ProofContract, ChildRequest.RegionDebugPath);
			TArray<FLayoutCommittedEndpointAnchor> ExpectedEndpointCommitments =
				DirectCommitment != nullptr
					? DirectCommitment->EndpointCommitments
					: TArray<FLayoutCommittedEndpointAnchor>();
			if (DirectCommitment != nullptr
				&& LayoutRegionScheduleSolverPrivate::HasParentOwnedAscentObligation(DirectCommitment->NegotiatedResponsibilityContract))
			{
				for (FLayoutCommittedEndpointAnchor& ExpectedAnchor : ExpectedEndpointCommitments)
				{
					if (ExpectedAnchor.FaceDirection == ELayoutFaceDirection::PosZ
						|| ExpectedAnchor.FaceDirection == ELayoutFaceDirection::NegZ)
					{
						ExpectedAnchor.TraversalChannels.Reset();
					}
				}
			}
			if (!AreUnorderedArraysEquivalent(
				ChildResult->CommittedEndpointAnchors,
				ExpectedEndpointCommitments,
				[](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
				{
					return AreProofExecutionCommittedEndpointAnchorsEquivalent(Left, Right);
				}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the committed child-anchor set for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (ChildResult->bDroppedAsOptionalChild
				|| !ChildResult->OptionalDropDecisionId.IsNone()
				|| !ChildResult->OptionalDropReason.IsEmpty())
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule unexpectedly treated proved region '%s' as an optional drop."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!ChildResult->SolveResult.DroppedOptionalChildren.IsEmpty())
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule introduced optional-drop records on proved region '%s' after negotiation finished."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!ValidateDroppedOptionalChildResidualCells(
				ProofContract,
				ChildResult->SolveResult.ResidualUnoccupiedCells,
				TEXT("Negotiated proof schedule introduced dropped-child residual cells on a proved child result for an unexpected optional-drop decision."),
				TEXT("Negotiated proof schedule changed dropped-child residual attribution on a proved child result after negotiation finished."),
				OutFailureReason))
			{
				return false;
			}

			if (ChildResult->RegionCellOffset != ChildRequest.RegionCellOffset)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the region-cell offset for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (ChildResult->SourceContentEntryId != ChildRequest.SourceContentEntryId)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the source content-entry id for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!AreProofExecutionSteppedTerrainSupportMapsEquivalent(
				ChildResult->SteppedTerrainSupportMap,
				ChildRequest.SteppedTerrainSupportMap))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the request-carried stepped terrain support map for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!AreProofExecutionForcedPlacementBundleInsertionsEquivalent(
				ChildResult->ForcedPlacementBundleInsertions,
				ChildRequest.ForcedPlacementBundleInsertions))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the request-carried forced placement bundle insertions for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!AreProofExecutionRouteConstraintArraysEquivalent(
				ChildResult->RequiredRouteConstraints,
				ChildRequest.RequiredRouteConstraints))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule changed the request-carried required route constraints for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!IsUnorderedArraySubsetEquivalent(
				ChildRequest.ProofRecords,
				ChildResult->ProofRecords,
				[](const FLayoutProofRecord& Left, const FLayoutProofRecord& Right)
				{
					return AreProofExecutionProofRecordsEquivalent(Left, Right);
				}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule dropped request-carried proof records for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}

			if (!IsUnorderedArraySubsetEquivalent(
				ChildRequest.ValidationAssertions,
				ChildResult->ValidationAssertions,
				[](const FLayoutValidationAssertionRecord& Left, const FLayoutValidationAssertionRecord& Right)
				{
					return AreProofExecutionValidationAssertionsEquivalent(Left, Right);
				}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated proof schedule dropped request-carried validation assertions for region '%s'."),
					*ChildRequest.RegionDebugPath);
				return false;
			}
		}

		if (!ValidateDroppedOptionalChildResidualCells(
			ProofContract,
			ParentResult->SolveResult.ResidualUnoccupiedCells,
			TEXT("Negotiated proof schedule introduced dropped-child residual cells on the proved parent result for an unexpected optional-drop decision."),
			TEXT("Negotiated proof schedule changed dropped-child residual attribution on the proved parent result after negotiation finished."),
			OutFailureReason))
		{
			return false;
		}

		OutFailureReason.Reset();
		return true;
	}
}

namespace LayoutRegionScheduleSolverPrivate
{
	void BuildIndependentChildProofValidationRequest(
		const FAutomaticChildPlacement& Placement,
		FLayoutRegionSolveRequest& OutChildRequest);
	TArray<FLayoutSolveBoundaryPoint> FreezeChildIncomingBoundaryPointsFromParentResult(
		const FLayoutRegionSolveResult& ParentProofResult,
		const FAutomaticChildPlacement& Placement,
		const TArray<FLayoutRegionSolveResult>* SiblingResults = nullptr);
	bool BuildIndependentChildProofResult(
		const FLayoutRegionSolveRequest& ChildProofRequest,
		FLayoutRegionSolveResult& OutChildProofResult,
		FString& OutFailureReason);
	bool RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
		const TArray<FLayoutPlannedCell>& DerivedParentPlannedCells,
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString* OutFailureReason);
	bool ValidateCommittedEndpointAnchorCoverageOnPlacements(
		const FString& RegionDebugPath,
		const FIntVector& RegionOffset,
		const TArray<FLayoutPlacedModule>& Placements,
		const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
		FString& OutFailureReason);
	bool BuildIndependentDeferredProofScheduleResult(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutRegionSolveRequest>& ChildRequests,
		const FCommittedRecursiveScheduleState& CandidateSchedule,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FLayoutRegionSolveResult&& ParentResult,
		TArray<FLayoutRegionSolveResult>&& ChildResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason,
		bool bApplyPostStructuralHandoffs = true);
	bool ContentSetHasChildRegionEntries(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot);
	const FLayoutPlacedModule* FindPlacementAtCellForMergedTraversalAudit(
		const TArray<FLayoutPlacedModule>& Placements,
		const FIntVector& WorldCell);
	bool DoesPlacementExposeCommittedAnchorOnFaceForMergedTraversalAudit(
		const FLayoutPlacedModule& Placement,
		const FIntVector& WorldCell,
		const FLayoutCommittedEndpointAnchor& Anchor);
	FLayoutSolveBoundaryPoint TranslateBoundaryPoint(
		const FLayoutSolveBoundaryPoint& BoundaryPoint,
		const FIntVector& RegionOffset);
	FIntVector TranslateCell(const FIntVector& Cell, const FIntVector& RegionOffset);
	bool DoesBoundaryPointSatisfyCommittedAnchor(
		const FLayoutSolveBoundaryPoint& BoundaryPoint,
		const FLayoutCommittedEndpointAnchor& Anchor);
	void AccumulatePropagationStats(
		FLayoutSolverPropagationStats& InOutStats,
		const FLayoutSolverPropagationStats& AdditionalStats);
	void AppendTranslatedSolveResultArtifacts(
		FLayoutSolveResult& InOutMergedSolveResult,
		const FLayoutSolveResult& RegionSolveResult,
		const FIntVector& RegionOffset,
		const FLayoutChildStageMappingResult* StageMapping);
	void SuppressPassiveSeamPlacementsOnRegionResults(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult);

	void BuildIndependentChildProofValidationRequest(
		const FAutomaticChildPlacement& Placement,
		FLayoutRegionSolveRequest& OutChildRequest)
	{
		OutChildRequest = Placement.ChildRequest;
		OutChildRequest.bDeferClosureValidationToSchedule = false;
		OutChildRequest.bDeferTraversalValidationToSchedule = false;
		OutChildRequest.CommittedEndpointAnchors = Placement.DirectChildCommitment.EndpointCommitments;
		if (HasParentOwnedAscentObligation(Placement.NegotiatedResponsibilityContract))
		{
			OutChildRequest.bDeferTraversalValidationToSchedule = true;
			OutChildRequest.CommittedTraversalAnchors.Reset();
			for (FLayoutCommittedEndpointAnchor& Anchor : OutChildRequest.CommittedEndpointAnchors)
			{
				// Parent-owned host ascent relaxes only vertical child endpoints;
				// lateral child entries remain exact merged traversal contracts.
				if (Anchor.FaceDirection == ELayoutFaceDirection::PosZ
					|| Anchor.FaceDirection == ELayoutFaceDirection::NegZ)
				{
					Anchor.TraversalChannels.Reset();
				}
			}
		}
		if (OutChildRequest.NegotiatedChildResponsibilityContracts.IsEmpty()
			&& !Placement.NegotiatedResponsibilityContract.ChildRegionDebugPath.IsEmpty())
		{
			OutChildRequest.NegotiatedChildResponsibilityContracts.Add(
				Placement.NegotiatedResponsibilityContract);
		}
		// Child room content sets typically have few Core-compatible
		// modules (often just 1).  Relax Core→Interior so the center cell
		// can use Interior modules (e.g. Floor) which are more likely to be
		// face-rule compatible with adjacent boundary cells.
		for (FLayoutPlannedCell& PlannedCell : OutChildRequest.PlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Core)
			{
				PlannedCell.Intent = ELayoutCellIntent::Interior;
			}
		}
		if (!Placement.CertifiedChildIncomingBoundaryPoints.IsEmpty())
		{
			OutChildRequest.IncomingBoundaryPoints =
				Placement.CertifiedChildIncomingBoundaryPoints;
		}
		if (!Placement.ChildDomainRestrictions.IsEmpty())
		{
			OutChildRequest.CandidateDomainRestrictions =
				Placement.ChildDomainRestrictions;
		}
		if (!Placement.BoundaryCertificateId.IsNone())
		{
			OutChildRequest.CandidateDomainCertificateId =
				Placement.BoundaryCertificateId;
		}
	}

	void PreserveIndependentChildProofFailure(
		const FLayoutRegionSolveRequest& ChildProofRequest,
		const FName Phase,
		const FString& FailureReason,
		FLayoutRegionSolveResult& InOutChildProofResult)
	{
		if (InOutChildProofResult.SolveResult.RegionalFailure.IsSet())
		{
			return;
		}

		FLayoutRegionalFailureRecord& Failure =
			InOutChildProofResult.SolveResult.RegionalFailure;
		Failure.Scope = ELayoutRegionalFailureScope::Child;
		Failure.Phase = Phase;
		Failure.ParentRegionDebugPath = ChildProofRequest.SourceParentRegionDebugPath;
		Failure.RegionDebugPath = ChildProofRequest.RegionDebugPath;
		Failure.SourceContentEntryId = ChildProofRequest.SourceContentEntryId;
		Failure.PreparationFailureKind =
			InOutChildProofResult.SolveResult.PreparationFailureKind;
		Failure.CandidateAttemptCount =
			InOutChildProofResult.SolveResult.PropagationStats.CandidateAttemptCount;
		Failure.FirstCause = FailureReason;
	}

	bool BuildIndependentChildProofResult(
		const FLayoutRegionSolveRequest& ChildProofRequest,
		FLayoutRegionSolveResult& OutChildProofResult,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_ChildProof, STAT_PorismLayout_ChildProof);
		INC_DWORD_STAT(STAT_PorismLayout_ChildProofCalls);
		OutFailureReason.Reset();
		if (!ContentSetHasChildRegionEntries(ChildProofRequest.ContentSetSnapshot))
		{
			OutChildProofResult = FLayoutProfileSolver::SolveRegion(ChildProofRequest);
			if (OutChildProofResult.SolveResult.bSucceeded)
			{
				return true;
			}

			TArray<FString> EntryCertificateRows;
			for (const FLayoutPlannedCell& PlannedCell : ChildProofRequest.PlannedCells)
			{
				if (PlannedCell.Intent != ELayoutCellIntent::Entry)
				{
					continue;
				}
				const FLayoutCellCandidateDomainRestriction* Restriction =
					ChildProofRequest.CandidateDomainRestrictions.FindByPredicate(
						[&PlannedCell](const FLayoutCellCandidateDomainRestriction& CandidateRestriction)
						{
							return CandidateRestriction.Cell == PlannedCell.Cell;
						});
				EntryCertificateRows.Add(FString::Printf(
					TEXT("%s=%s"),
					*PlannedCell.Cell.ToString(),
					Restriction != nullptr
						? *FString::JoinBy(Restriction->AllowedCandidates, TEXT(","), [](const FLayoutCandidateVariantIdentity& Identity)
						{
							return FString::Printf(TEXT("%s/Yaw%d"), *Identity.ModuleSnapshotId.ToString(), Identity.YawRotationSteps);
						})
						: TEXT("<none>")));
			}
			OutFailureReason = OutChildProofResult.SolveResult.FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Independent child proof failed for region '%s' under parent '%s'. Certified entries before proof=[%s]."),
					*ChildProofRequest.RegionDebugPath,
					ChildProofRequest.SourceParentRegionDebugPath.IsEmpty() ? TEXT("<empty>") : *ChildProofRequest.SourceParentRegionDebugPath,
					*FString::Join(EntryCertificateRows, TEXT(";")))
				: FString::Printf(
					TEXT("Independent child proof failed for region '%s' under parent '%s'. Certified entries before proof=[%s]. %s"),
					*ChildProofRequest.RegionDebugPath,
					ChildProofRequest.SourceParentRegionDebugPath.IsEmpty() ? TEXT("<empty>") : *ChildProofRequest.SourceParentRegionDebugPath,
					*FString::Join(EntryCertificateRows, TEXT(";")),
					*OutChildProofResult.SolveResult.FailureReason);
			PreserveIndependentChildProofFailure(
				ChildProofRequest,
				TEXT("ChildProof"),
				OutFailureReason,
				OutChildProofResult);
			return false;
		}

		const FLayoutRegionSolveScheduleResult ChildScheduleResult =
			FLayoutProfileSolver::SolveRegionTree(ChildProofRequest);
		if (!ChildScheduleResult.bSucceeded)
		{
			OutChildProofResult.RegionDebugPath = ChildProofRequest.RegionDebugPath;
			OutChildProofResult.RegionCellOffset = ChildProofRequest.RegionCellOffset;
			OutChildProofResult.SourceContentEntryId = ChildProofRequest.SourceContentEntryId;
			OutChildProofResult.SolveResult = ChildScheduleResult.MergedSolveResult;
			OutFailureReason = ChildScheduleResult.FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Independent child subtree proof failed for region '%s'."),
					*ChildProofRequest.RegionDebugPath)
				: FString::Printf(
					TEXT("Independent child subtree proof failed for region '%s'. %s"),
					*ChildProofRequest.RegionDebugPath,
					*ChildScheduleResult.FailureReason);
			PreserveIndependentChildProofFailure(
				ChildProofRequest,
				TEXT("ChildSubtreeProof"),
				OutFailureReason,
				OutChildProofResult);
			return false;
		}

		const FLayoutRegionSolveResult* RootChildResult = ChildScheduleResult.RegionResults.FindByPredicate(
			[&ChildProofRequest](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == ChildProofRequest.RegionDebugPath
					&& Result.SourceContentEntryId == ChildProofRequest.SourceContentEntryId;
			});
		if (RootChildResult == nullptr)
		{
			OutChildProofResult.RegionDebugPath = ChildProofRequest.RegionDebugPath;
			OutChildProofResult.RegionCellOffset = ChildProofRequest.RegionCellOffset;
			OutChildProofResult.SourceContentEntryId = ChildProofRequest.SourceContentEntryId;
			OutFailureReason = FString::Printf(
				TEXT("Independent child subtree proof succeeded for region '%s', but the root child result could not be located in the subtree schedule."),
				*ChildProofRequest.RegionDebugPath);
			PreserveIndependentChildProofFailure(
				ChildProofRequest,
				TEXT("ChildSubtreeMerge"),
				OutFailureReason,
				OutChildProofResult);
			return false;
		}

		OutChildProofResult = *RootChildResult;
		FLayoutSolveResult MergedChildSolveResult = ChildScheduleResult.MergedSolveResult;
		// Collapsed subtree artifacts still represent this root child proof. Preserve
		// its consumed domain certificate so the enclosing schedule can audit the
		// inherited boundary restrictions rather than mistaking merge metadata loss
		// for an unconstrained solve.
		MergedChildSolveResult.CandidateDomainCertificateId =
			RootChildResult->SolveResult.CandidateDomainCertificateId;
		MergedChildSolveResult.CandidateDomainRestrictionIds =
			RootChildResult->SolveResult.CandidateDomainRestrictionIds;
		OutChildProofResult.SolveResult = MoveTemp(MergedChildSolveResult);
		OutChildProofResult.RegionCellOffset = ChildProofRequest.RegionCellOffset;
		OutChildProofResult.SourceContentEntryId = ChildProofRequest.SourceContentEntryId;
		return true;
	}

	bool ValidateCommittedEndpointAnchorCoverageOnPlacements(
		const FString& RegionDebugPath,
		const FIntVector& RegionOffset,
		const TArray<FLayoutPlacedModule>& Placements,
		const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
		FString& OutFailureReason)
	{
		for (const FLayoutCommittedEndpointAnchor& Anchor : Anchors)
		{
			const FIntVector WorldCell = Anchor.LocalCell + RegionOffset;
			const FLayoutPlacedModule* Placement = FindPlacementAtCellForMergedTraversalAudit(
				Placements,
				WorldCell);
			if (Placement == nullptr
				|| !DoesPlacementExposeCommittedAnchorOnFaceForMergedTraversalAudit(
					*Placement,
					WorldCell,
					Anchor))
			{
				TArray<FString> EntryPlacementDetails;
				for (const FLayoutPlacedModule& CandidatePlacement : Placements)
				{
					if (CandidatePlacement.Intent == ELayoutCellIntent::Entry
						&& EntryPlacementDetails.Num() < 8)
					{
						EntryPlacementDetails.Add(FString::Printf(
							TEXT("%s:%s:yaw%d"),
							*CandidatePlacement.Cell.ToString(),
							*CandidatePlacement.ModuleSnapshotId.ToString(),
							CandidatePlacement.YawRotationSteps));
					}
				}
				FIntVector MinPlacementCell(MAX_int32, MAX_int32, MAX_int32);
				FIntVector MaxPlacementCell(MIN_int32, MIN_int32, MIN_int32);
				for (const FLayoutPlacedModule& CandidatePlacement : Placements)
				{
					MinPlacementCell.X = FMath::Min(MinPlacementCell.X, CandidatePlacement.Cell.X);
					MinPlacementCell.Y = FMath::Min(MinPlacementCell.Y, CandidatePlacement.Cell.Y);
					MinPlacementCell.Z = FMath::Min(MinPlacementCell.Z, CandidatePlacement.Cell.Z);
					MaxPlacementCell.X = FMath::Max(MaxPlacementCell.X, CandidatePlacement.Cell.X);
					MaxPlacementCell.Y = FMath::Max(MaxPlacementCell.Y, CandidatePlacement.Cell.Y);
					MaxPlacementCell.Z = FMath::Max(MaxPlacementCell.Z, CandidatePlacement.Cell.Z);
				}
				OutFailureReason = FString::Printf(
					TEXT("Region '%s' accepted committed endpoint anchor '%s' at world cell %s on face %s, but the realized solve did not expose compatible face coverage there. Placement=%s yaw=%d source=%s. EntryPlacements=[%s] placementBounds=%s..%s."),
					*RegionDebugPath,
					*Anchor.CommitmentId.ToString(),
					*WorldCell.ToString(),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Anchor.FaceDirection)),
					Placement != nullptr ? *Placement->ModuleSnapshotId.ToString() : TEXT("<none>"),
					Placement != nullptr ? Placement->YawRotationSteps : INDEX_NONE,
					Placement != nullptr ? *Placement->SourceContentEntryId.ToString() : TEXT("<none>"),
					*FString::Join(EntryPlacementDetails, TEXT("|")),
					Placements.IsEmpty() ? TEXT("<none>") : *MinPlacementCell.ToString(),
					Placements.IsEmpty() ? TEXT("<none>") : *MaxPlacementCell.ToString());
				return false;
			}
		}

		return true;
	}

	static bool ValidateIndependentChildCommitmentsAgainstParentResult(
		const FLayoutRegionSolveResult& ParentResult,
		const FAutomaticChildPlacement& Placement,
		FString& OutFailureReason)
	{
		TSet<FLayoutId> SatisfiedCommitmentIds;
		const FIntVector InverseRegionOffset(
			-Placement.RegionCellOffset.X,
			-Placement.RegionCellOffset.Y,
			-Placement.RegionCellOffset.Z);
		for (const FLayoutSolveBoundaryPoint& BoundaryPoint : ParentResult.ExportedBoundaryPoints)
		{
			FLayoutSolveBoundaryPoint TranslatedBoundaryPoint =
				TranslateBoundaryPoint(BoundaryPoint, InverseRegionOffset);
			TranslatedBoundaryPoint.bRequiresBoundaryFacing = true;
			for (const FLayoutCommittedEndpointAnchor& Anchor : Placement.DirectChildCommitment.EndpointCommitments)
			{
				if (SatisfiedCommitmentIds.Contains(Anchor.CommitmentId))
				{
					continue;
				}
				if (!DoesBoundaryPointSatisfyCommittedAnchor(TranslatedBoundaryPoint, Anchor))
				{
					continue;
				}
				SatisfiedCommitmentIds.Add(Anchor.CommitmentId);
			}
		}

		for (const FLayoutCommittedEndpointAnchor& Anchor : Placement.DirectChildCommitment.EndpointCommitments)
		{
			if (SatisfiedCommitmentIds.Contains(Anchor.CommitmentId))
			{
				continue;
			}

			// Fallback: confirm the parent placement at the anchor contact cell exposes a
			// compatible committed face. Parent-owned retained-shell seams are covered by
			// the commitment itself, not a realized parent placement.
			const bool bParentOwnedCommittedSeam =
				Placement.NegotiatedResponsibilityContract.HostVerticalAccessResponsibility
					== ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
			const FIntVector ParentPlacementCell =
				Placement.RegionCellOffset + Anchor.LocalCell + FLayoutDirectionUtils::ToCellDelta(Anchor.FaceDirection);
			const FLayoutPlacedModule* ParentPlacement = FindPlacementAtCellForMergedTraversalAudit(
				ParentResult.SolveResult.Placements,
				ParentPlacementCell);
			FLayoutCommittedEndpointAnchor ParentFacingAnchor = Anchor;
			ParentFacingAnchor.FaceDirection =
				FLayoutDirectionUtils::GetOpposite(Anchor.FaceDirection);
			const bool bCoveredByPlacement = ParentPlacement != nullptr
				&& DoesPlacementExposeCommittedAnchorOnFaceForMergedTraversalAudit(
					*ParentPlacement,
					ParentPlacementCell,
					ParentFacingAnchor);
			// Parent-owned retained-shell seams are covered by the commitment
			// itself when no realized parent placement exists at the contact cell.
			// When a placement DOES exist, it must expose the anchor — the
			// commitment alone is not a substitute for a mismatched placement.
			if (bCoveredByPlacement
				|| (bParentOwnedCommittedSeam && ParentPlacement == nullptr))
			{
				SatisfiedCommitmentIds.Add(Anchor.CommitmentId);
				continue;
			}

			TArray<FString> BoundaryDetails;
			for (const FLayoutSolveBoundaryPoint& BoundaryPoint : ParentResult.ExportedBoundaryPoints)
			{
				const FLayoutSolveBoundaryPoint TranslatedBoundaryPoint =
					TranslateBoundaryPoint(BoundaryPoint, InverseRegionOffset);
				const FIntVector DependentCell = TranslatedBoundaryPoint.LocalCell
					+ FLayoutDirectionUtils::ToCellDelta(
						TranslatedBoundaryPoint.FaceDirection);
				if (DependentCell == Anchor.LocalCell
					|| TranslatedBoundaryPoint.LocalCell == Anchor.LocalCell)
				{
					BoundaryDetails.Add(FString::Printf(
						TEXT("local=%s dependent=%s face=%s connection=%s allowed=%s traversal=%s source=%s"),
						*TranslatedBoundaryPoint.LocalCell.ToString(),
						*DependentCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
							static_cast<int64>(TranslatedBoundaryPoint.FaceDirection)),
						TranslatedBoundaryPoint.ConnectionTag.IsValid()
							? *TranslatedBoundaryPoint.ConnectionTag.ToString()
							: TEXT("<none>"),
						*TranslatedBoundaryPoint.AllowedConnectionTags.ToString(),
						*TranslatedBoundaryPoint.ConnectedTraversalChannels.ToString(),
						*TranslatedBoundaryPoint.SourceRegionDebugPath));
				}
			}
			FString PlacementDetail = TEXT("<none>");
			if (ParentPlacement != nullptr)
			{
				PlacementDetail = FString::Printf(
					TEXT("module=%s yaw=%d compatible=%d"),
					*ParentPlacement->ModuleSnapshotId.ToString(),
					ParentPlacement->YawRotationSteps,
					bCoveredByPlacement ? 1 : 0);
			}
			OutFailureReason = FString::Printf(
				TEXT("Independent deferred child proof could not confirm that parent region '%s' exported a boundary point satisfying committed child anchor '%s' for region '%s'. Anchor local=%s face=%s connection=%s traversal=%s. Parent placement: %s. Nearby exported boundaries: %s"),
				*Placement.DirectChildCommitment.ParentRegionDebugPath,
				*Anchor.CommitmentId.ToString(),
				*Placement.ChildRegionDebugPath,
				*Anchor.LocalCell.ToString(),
				*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
					static_cast<int64>(Anchor.FaceDirection)),
				Anchor.ConnectionTag.IsValid()
					? *Anchor.ConnectionTag.ToString()
					: TEXT("<none>"),
				*Anchor.TraversalChannels.ToString(),
				*PlacementDetail,
				BoundaryDetails.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(BoundaryDetails, TEXT(" | ")));
			return false;
		}

		return true;
	}

	bool BuildIndependentDeferredProofScheduleResult(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutRegionSolveRequest>& ChildRequests,
		const FCommittedRecursiveScheduleState& CandidateSchedule,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FLayoutRegionSolveResult&& ParentResult,
		TArray<FLayoutRegionSolveResult>&& ChildResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason,
		bool bApplyPostStructuralHandoffs)
	{
		OutFailureReason.Reset();
		FCommittedVerticalAccessOwnership EffectiveVerticalAccessOwnership =
			VerticalAccessOwnership;
		const bool bHasNegotiatedParentHostAuthority =
			EffectiveVerticalAccessOwnership.RequiredHostProviderCount > 0
			|| EffectiveVerticalAccessOwnership.CountedParentProviderCount > 0
			|| !EffectiveVerticalAccessOwnership.RetainedParentRouteSupportVerticalAccessCells.IsEmpty();
		if (bHasNegotiatedParentHostAuthority
			&& !RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
				ParentResult.SolveResult.PlannedCells,
				EffectiveVerticalAccessOwnership,
				&OutFailureReason))
		{
			return false;
		}
		for (FLayoutNegotiatedChildResponsibilityContract& Contract :
			ParentResult.NegotiatedChildResponsibilityContracts)
		{
			if (Contract.HostVerticalAccessResponsibility
					== ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned
				|| Contract.HostVerticalAccessResponsibility
					== ELayoutNegotiatedHostVerticalAccessResponsibility::Composed)
			{
				Contract.CountedParentProviderCount =
					EffectiveVerticalAccessOwnership.CountedParentProviderCount;
				Contract.CountedParentVerticalAccessCells =
					EffectiveVerticalAccessOwnership.CountedParentVerticalAccessCells;
				Contract.RetainedParentRouteSupportVerticalAccessCells =
					EffectiveVerticalAccessOwnership.RetainedParentRouteSupportVerticalAccessCells;
			}
		}

		OutScheduleResult = FLayoutRegionSolveScheduleResult();
		for (const FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
		{
			if (Placement.DirectChildCommitment.StageMapping.IsValid())
			{
				OutScheduleResult.ChildStageMappings.Add(
					Placement.DirectChildCommitment.StageMapping);
				OutScheduleResult.MergedSolveResult.ChildStageMappingIds.AddUnique(
					Placement.DirectChildCommitment.StageMapping.MappingId);
			}
		}
		OutScheduleResult.ChildStageMappings.Sort([](
			const FLayoutChildStageMappingResult& Left,
			const FLayoutChildStageMappingResult& Right)
		{
			return Left.MappingId.LexicalLess(Right.MappingId);
		});
		OutScheduleResult.MergedSolveResult.ChildStageMappingIds.Sort();
		OutScheduleResult.RegionResults.Add(MoveTemp(ParentResult));
		for (FLayoutRegionSolveResult& ChildResult : ChildResults)
		{
			OutScheduleResult.RegionResults.Add(MoveTemp(ChildResult));
		}

		const FLayoutRegionSolveResult* RootRegionResult = OutScheduleResult.RegionResults.FindByPredicate(
			[&ParentRequest](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == ParentRequest.RegionDebugPath;
			});
		if (RootRegionResult == nullptr)
		{
			OutFailureReason = TEXT("Independent deferred proof did not retain the parent residual result.");
			return false;
		}

		for (const FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
		{
			if (!ValidateIndependentChildCommitmentsAgainstParentResult(
					*RootRegionResult,
					Placement,
					OutFailureReason))
			{
				return false;
			}
		}

		TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath;
		RequestsByPath.Add(ParentRequest.RegionDebugPath, &ParentRequest);
		for (const FLayoutRegionSolveRequest& ChildRequest : ChildRequests)
		{
			RequestsByPath.Add(ChildRequest.RegionDebugPath, &ChildRequest);
		}
		for (const TPair<FString, const FLayoutRegionSolveRequest*>& RequestPair : RequestsByPath)
		{
			const FLayoutRegionSolveResult* MatchingResult =
				OutScheduleResult.RegionResults.FindByPredicate(
					[&RequestPair](const FLayoutRegionSolveResult& Result)
					{
						return Result.RegionDebugPath == RequestPair.Key;
					});
			if (MatchingResult == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Regional proof '%s' did not return a solved result."),
					*RequestPair.Key);
				return false;
			}
			TArray<FLayoutId> MissingRestrictionIds;
			for (const FLayoutCellCandidateDomainRestriction& Restriction :
				RequestPair.Value->CandidateDomainRestrictions)
			{
				if (!MatchingResult->SolveResult.CandidateDomainRestrictionIds.Contains(
					Restriction.RestrictionId))
				{
					MissingRestrictionIds.Add(Restriction.RestrictionId);
				}
			}
			if (!MissingRestrictionIds.IsEmpty())
			{
				OutFailureReason = FString::Printf(
					TEXT("Regional proof '%s' did not consume inherited candidate-domain restrictions [%s]. Request certificate='%s' returned certificate='%s'."),
					*RequestPair.Key,
					*FString::JoinBy(MissingRestrictionIds, TEXT(","), [](const FLayoutId& Id){ return Id.ToString(); }),
					*RequestPair.Value->CandidateDomainCertificateId.ToString(),
					*MatchingResult->SolveResult.CandidateDomainCertificateId.ToString());
				return false;
			}
		}

		for (FLayoutRegionSolveResult& RegionResult : OutScheduleResult.RegionResults)
		{
			OutScheduleResult.MergedSolveResult.Messages.Append(RegionResult.SolveResult.Messages);
			AccumulatePropagationStats(
				OutScheduleResult.MergedSolveResult.PropagationStats,
				RegionResult.SolveResult.PropagationStats);
			const FAutomaticChildPlacement* RegionPlacement = CandidateSchedule.Placements.FindByPredicate(
				[&RegionResult](const FAutomaticChildPlacement& Placement)
				{
					return Placement.ChildRegionDebugPath == RegionResult.RegionDebugPath;
				});
			AppendTranslatedSolveResultArtifacts(
				OutScheduleResult.MergedSolveResult,
				RegionResult.SolveResult,
				RegionResult.RegionCellOffset,
				RegionPlacement != nullptr ? &RegionPlacement->DirectChildCommitment.StageMapping : nullptr);
			if (OutScheduleResult.MergedSolveResult.Seed == 0 && RegionResult.RegionDebugPath == ParentRequest.RegionDebugPath)
			{
				OutScheduleResult.MergedSolveResult.Seed = RegionResult.SolveResult.Seed;
				OutScheduleResult.MergedSolveResult.FootprintSize = RegionResult.SolveResult.FootprintSize;
			}
		}

		TMap<FString, int32> RegionResultIndexByPath;
		for (int32 ResultIndex = 0; ResultIndex < OutScheduleResult.RegionResults.Num(); ++ResultIndex)
		{
			RegionResultIndexByPath.Add(OutScheduleResult.RegionResults[ResultIndex].RegionDebugPath, ResultIndex);
		}

		TMap<FString, TArray<FString>> ChildrenByParent;
		for (const FLayoutRegionSolveRequest& ChildRequest : ChildRequests)
		{
			ChildrenByParent.FindOrAdd(ParentRequest.RegionDebugPath).Add(ChildRequest.RegionDebugPath);
		}

		FString DeferredClosureFailureReason;
		if (!LayoutProfileSolverInternal::ReevaluateDeferredClosureCoverageForRegion(
				ParentRequest.RegionDebugPath,
				RequestsByPath,
				RegionResultIndexByPath,
				ChildrenByParent,
				OutScheduleResult,
				DeferredClosureFailureReason))
		{
			OutFailureReason = DeferredClosureFailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Independent deferred proof failed closure coverage audit for parent region '%s'."),
					*ParentRequest.RegionDebugPath)
				: DeferredClosureFailureReason;
			return false;
		}

		OutScheduleResult.MergedSolveResult.PartitionSeams = CandidateSchedule.PlannedPartitionSeams;

		OutScheduleResult.RecursiveVerticalAccessSummary.RequiredHostProviderCount = EffectiveVerticalAccessOwnership.RequiredHostProviderCount;
		OutScheduleResult.RecursiveVerticalAccessSummary.ResolvedHostProviderCount = EffectiveVerticalAccessOwnership.ResolvedHostProviderCount;
		OutScheduleResult.RecursiveVerticalAccessSummary.CountedParentProviderCount = EffectiveVerticalAccessOwnership.CountedParentProviderCount;
		OutScheduleResult.RecursiveVerticalAccessSummary.CountedParentVerticalAccessCells = EffectiveVerticalAccessOwnership.CountedParentVerticalAccessCells;
		OutScheduleResult.RecursiveVerticalAccessSummary.RetainedParentRouteSupportVerticalAccessCells = EffectiveVerticalAccessOwnership.RetainedParentRouteSupportVerticalAccessCells;
		OutScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths = EffectiveVerticalAccessOwnership.CountedChildRegionDebugPaths;
		OutScheduleResult.RecursiveVerticalAccessSummary.LocalOnlyChildRegionDebugPaths = EffectiveVerticalAccessOwnership.LocalOnlyChildRegionDebugPaths;
		OutScheduleResult.RecursiveVerticalAccessSummary.UnusableContributingChildRegionDebugPaths = EffectiveVerticalAccessOwnership.UnusableContributingChildRegionDebugPaths;
		OutScheduleResult.RecursiveVerticalAccessSummary.ExtraContributingChildRegionDebugPaths = EffectiveVerticalAccessOwnership.ExtraContributingChildRegionDebugPaths;
		OutScheduleResult.RecursiveVerticalAccessSummary.FailureReason = EffectiveVerticalAccessOwnership.FailureReason;

		if (bApplyPostStructuralHandoffs)
		{
			LayoutRegionScheduleSolverFacade::FNegotiatedProofScheduleContract ProofContract;
			ProofContract.ParentRequest = ParentRequest;
			ProofContract.ChildRequests = ChildRequests;
			ProofContract.SeamPlan.PlannedPartitionSeams = CandidateSchedule.PlannedPartitionSeams;
			ProofContract.SeamPlan.bPlannedPartitionSeamsAreAuthoritative = true;
			ProofContract.PlannedRecursiveVerticalAccessSummary = OutScheduleResult.RecursiveVerticalAccessSummary;
			for (const FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
			{
				const FLayoutNegotiatedChildResponsibilityContract* ActualContract =
					RootRegionResult->NegotiatedChildResponsibilityContracts.FindByPredicate(
						[&Placement](const FLayoutNegotiatedChildResponsibilityContract& Contract)
						{
							return Contract.ChildRegionDebugPath == Placement.ChildRegionDebugPath;
						});
				if (!Placement.DirectChildCommitment.ChildRegionDebugPath.IsEmpty())
				{
					FLayoutDirectChildRegionCommitment DirectCommitment =
						Placement.DirectChildCommitment;
					if (ActualContract != nullptr)
					{
						DirectCommitment.NegotiatedResponsibilityContract = *ActualContract;
					}
					ProofContract.DirectChildCommitments.Add(MoveTemp(DirectCommitment));
				}
				if (ActualContract != nullptr)
				{
					ProofContract.ResponsibilityContracts.Add(*ActualContract);
				}
				else if (!Placement.NegotiatedResponsibilityContract.ChildRegionDebugPath.IsEmpty())
				{
					ProofContract.ResponsibilityContracts.Add(
						Placement.NegotiatedResponsibilityContract);
				}
			}
			if (!LayoutRegionScheduleSolverFacade::ApplyPostStructuralScheduleHandoffs(
					OutScheduleResult,
					ProofContract,
					OutFailureReason))
			{
				return false;
			}
		}

		OutScheduleResult.bSucceeded = true;
		OutScheduleResult.FailureReason.Reset();
		OutScheduleResult.MergedSolveResult.bSucceeded = true;
		OutScheduleResult.MergedSolveResult.FailureReason.Reset();
		return true;
	}
}

#if WITH_AUTOMATION_TESTS
bool LayoutProfileSolverInternal::DoesProofSelectedParentHostRefinementPreserveResponsibilityContractForTests(
	const FLayoutNegotiatedChildResponsibilityContract& NegotiatedContract,
	const FLayoutRecursiveVerticalAccessSummary& ActualSummary,
	const FLayoutNegotiatedChildResponsibilityContract& ActualContract)
{
	LayoutRegionScheduleSolverFacade::FNegotiatedProofScheduleContract ProofContract;
	ProofContract.ParentRequest.NegotiatedChildResponsibilityContracts.Add(NegotiatedContract);
	const TArray<FLayoutNegotiatedChildResponsibilityContract> ExpectedContracts =
		LayoutRegionScheduleSolverFacade::BuildExpectedParentResponsibilityContractsAfterHostRefinement(
			ProofContract,
			ActualSummary);
	return ExpectedContracts.Num() == 1
		&& LayoutRegionScheduleSolverFacade::AreProofExecutionNegotiatedResponsibilityContractsEquivalent(
			ExpectedContracts[0],
			ActualContract);
}
#endif

bool LayoutProfileSolverInternal::BuildIndependentChildProofResult(
	const FLayoutRegionSolveRequest& ChildProofRequest,
	FLayoutRegionSolveResult& OutChildProofResult,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::BuildIndependentChildProofResult(
		ChildProofRequest,
		OutChildProofResult,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::PrepareIndependentProofRequestForExecution(
	FLayoutRegionSolveRequest& InOutRequest,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	LayoutRegionScheduleSolverFacade::AppendNegotiatedContractProofDiagnosticsToRequest(
		InOutRequest);
	return true;
}

bool LayoutProfileSolverInternal::ValidateCommittedEndpointAnchorCoverageOnPlacementsForSchedule(
	const FString& RegionDebugPath,
	const FIntVector& RegionOffset,
	const TArray<FLayoutPlacedModule>& Placements,
	const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::ValidateCommittedEndpointAnchorCoverageOnPlacements(
		RegionDebugPath,
		RegionOffset,
		Placements,
		Anchors,
		OutFailureReason);
}

TArray<FLayoutSolveBoundaryPoint> LayoutProfileSolverInternal::FreezeChildIncomingBoundaryPointsFromParentResult(
	const FLayoutRegionSolveResult& ParentProofResult,
	const FLayoutDirectChildRegionCommitment& DirectChildCommitment,
	const FLayoutRegionSolveRequest& ChildProofRequest,
	const TArray<FLayoutRegionSolveResult>* SiblingResults)
{
	LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement Placement;
	Placement.ChildRequest = ChildProofRequest;
	Placement.ChildRegionDebugPath = ChildProofRequest.RegionDebugPath;
	Placement.RegionCellOffset = ChildProofRequest.RegionCellOffset;
	Placement.DirectChildCommitment = DirectChildCommitment;
	Placement.NegotiatedResponsibilityContract = DirectChildCommitment.NegotiatedResponsibilityContract;
	return LayoutRegionScheduleSolverPrivate::FreezeChildIncomingBoundaryPointsFromParentResult(
		ParentProofResult,
		Placement,
		SiblingResults);
}

bool LayoutProfileSolverInternal::BuildIndependentCommittedChildProofRequestFromHandoff(
	const FLayoutDirectChildRegionCommitment& DirectChildCommitment,
	const FLayoutRegionSolveRequest& ChildProofRequest,
	FLayoutRegionSolveRequest& OutChildProofRequest,
	FString& OutFailureReason)
{
	LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement Placement;
	Placement.ChildRequest = ChildProofRequest;
	Placement.ChildRegionDebugPath = ChildProofRequest.RegionDebugPath;
	Placement.RegionCellOffset = ChildProofRequest.RegionCellOffset;
	Placement.DirectChildCommitment = DirectChildCommitment;
	Placement.NegotiatedResponsibilityContract = DirectChildCommitment.NegotiatedResponsibilityContract;
	LayoutRegionScheduleSolverPrivate::BuildIndependentChildProofValidationRequest(
		Placement,
		OutChildProofRequest);
	if (DirectChildCommitment.StageMapping.IsValid())
	{
		// Local project fix: committed mapping owns child topology at task handoff;
		// mutable proof-request planning may not renegotiate certified cell intents.
		OutChildProofRequest.PlannedCells =
			DirectChildCommitment.StageMapping.ChildLocalPlannedCells;
		OutChildProofRequest.PrecomputedPlannedCells =
			OutChildProofRequest.PlannedCells;
		OutChildProofRequest.bHasFinalizedSteppedTerrainIntents = true;
	}
	if (!DirectChildCommitment.ParentRegionDebugPath.IsEmpty())
	{
		OutChildProofRequest.SourceParentRegionDebugPath =
			DirectChildCommitment.ParentRegionDebugPath;
	}
	// Consume only the frozen handoff; the child proof never queries the live parent result.
	OutChildProofRequest.IncomingBoundaryPoints = DirectChildCommitment.FrozenBoundaryPoints;
	return true;
}

bool LayoutProfileSolverInternal::BuildIndependentProofScheduleResultFromContract(
	const LayoutRegionScheduleSolverFacade::FNegotiatedProofScheduleContract& ProofContract,
	FLayoutRegionSolveResult&& ParentProofResult,
	TArray<FLayoutRegionSolveResult>&& ChildProofResults,
	FLayoutRegionSolveScheduleResult& OutScheduleResult,
	FString& OutFailureReason)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Merge_BuildSchedule, STAT_PorismLayout_MergeAudit);
	LayoutRegionScheduleSolverPrivate::FCommittedRecursiveScheduleState CandidateSchedule;
	CandidateSchedule.PlannedPartitionSeams = ProofContract.SeamPlan.PlannedPartitionSeams;
	for (const FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
	{
		LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement& Placement = CandidateSchedule.Placements.AddDefaulted_GetRef();
		Placement.ChildRequest = ChildRequest;
		Placement.ChildRegionDebugPath = ChildRequest.RegionDebugPath;
		Placement.RegionCellOffset = ChildRequest.RegionCellOffset;
		if (const FLayoutDirectChildRegionCommitment* DirectCommitment = ProofContract.DirectChildCommitments.FindByPredicate(
			[&ChildRequest](const FLayoutDirectChildRegionCommitment& Commitment)
			{
				return Commitment.ChildRegionDebugPath == ChildRequest.RegionDebugPath;
			}))
		{
			Placement.DirectChildCommitment = *DirectCommitment;
			Placement.NegotiatedResponsibilityContract = DirectCommitment->NegotiatedResponsibilityContract;
		}
		else if (const FLayoutNegotiatedChildResponsibilityContract* ResponsibilityContract = ProofContract.ResponsibilityContracts.FindByPredicate(
			[&ChildRequest](const FLayoutNegotiatedChildResponsibilityContract& Contract)
			{
				return Contract.ChildRegionDebugPath == ChildRequest.RegionDebugPath;
			}))
		{
			Placement.NegotiatedResponsibilityContract = *ResponsibilityContract;
		}
	}

	LayoutRegionScheduleSolverPrivate::FCommittedVerticalAccessOwnership VerticalAccessOwnership;
	VerticalAccessOwnership.RequiredHostProviderCount = ProofContract.PlannedRecursiveVerticalAccessSummary.RequiredHostProviderCount;
	VerticalAccessOwnership.ResolvedHostProviderCount = ProofContract.PlannedRecursiveVerticalAccessSummary.ResolvedHostProviderCount;
	VerticalAccessOwnership.CountedParentProviderCount = ProofContract.PlannedRecursiveVerticalAccessSummary.CountedParentProviderCount;
	VerticalAccessOwnership.CountedParentVerticalAccessCells = ProofContract.PlannedRecursiveVerticalAccessSummary.CountedParentVerticalAccessCells;
	VerticalAccessOwnership.RetainedParentRouteSupportVerticalAccessCells = ProofContract.PlannedRecursiveVerticalAccessSummary.RetainedParentRouteSupportVerticalAccessCells;
	VerticalAccessOwnership.CountedChildRegionDebugPaths = ProofContract.PlannedRecursiveVerticalAccessSummary.CountedChildRegionDebugPaths;
	VerticalAccessOwnership.LocalOnlyChildRegionDebugPaths = ProofContract.PlannedRecursiveVerticalAccessSummary.LocalOnlyChildRegionDebugPaths;
	VerticalAccessOwnership.UnusableContributingChildRegionDebugPaths = ProofContract.PlannedRecursiveVerticalAccessSummary.UnusableContributingChildRegionDebugPaths;
	VerticalAccessOwnership.ExtraContributingChildRegionDebugPaths = ProofContract.PlannedRecursiveVerticalAccessSummary.ExtraContributingChildRegionDebugPaths;
	VerticalAccessOwnership.FailureReason = ProofContract.PlannedRecursiveVerticalAccessSummary.FailureReason;

	const bool bBuiltSchedule = LayoutRegionScheduleSolverPrivate::BuildIndependentDeferredProofScheduleResult(
		ProofContract.ParentRequest,
		ProofContract.ChildRequests,
		CandidateSchedule,
		VerticalAccessOwnership,
		MoveTemp(ParentProofResult),
		MoveTemp(ChildProofResults),
		OutScheduleResult,
		OutFailureReason,
		false);
	if (bBuiltSchedule)
	{
		OutScheduleResult.MergedSolveResult.DroppedOptionalChildren =
			ProofContract.OptionalChildPlan.DroppedOptionalChildren;
	}
	return bBuiltSchedule;
}

bool LayoutProfileSolverInternal::BuildIndependentChildProofResultForTests(
	const FLayoutRegionSolveRequest& ChildProofRequest,
	FLayoutRegionSolveResult& OutChildProofResult,
	FString& OutFailureReason)
{
	return BuildIndependentChildProofResult(
		ChildProofRequest,
		OutChildProofResult,
		OutFailureReason);
}
