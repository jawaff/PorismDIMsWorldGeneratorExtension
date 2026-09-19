// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "LayoutRegionRequestSnapshotBuilder.h"
#include "LayoutProfileSolverInternal.h"

/**
 * Private rewrite home for `FLayoutProofContractBuilder`.
 *
 * This file will package immutable proof contracts and thread-ready proof job
 * metadata from negotiated structural state.
 */

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
		static bool DoesContractMatch(
			const FLayoutNegotiatedChildResponsibilityContract& Left,
			const FLayoutNegotiatedChildResponsibilityContract& Right)
		{
			return Left.ParentRegionDebugPath == Right.ParentRegionDebugPath
				&& Left.ChildRegionDebugPath == Right.ChildRegionDebugPath;
		}

		static bool DoesBoundedParentProofEvidenceMatch(
			const FBoundedParentProofEvidence& Left,
			const FBoundedParentProofEvidence& Right)
		{
			return Left.ChildRegionDebugPath == Right.ChildRegionDebugPath;
		}

		static void AppendNegotiatedContractIfMissing(
			TArray<FLayoutNegotiatedChildResponsibilityContract>& InOutContracts,
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			if (!InOutContracts.ContainsByPredicate(
				[&Contract](const FLayoutNegotiatedChildResponsibilityContract& Existing)
				{
					return DoesContractMatch(Existing, Contract);
				}))
			{
				InOutContracts.Add(Contract);
			}
		}

		static void AppendBoundedParentProofEvidenceIfMissing(
			TArray<FBoundedParentProofEvidence>& InOutEvidenceRecords,
			const FBoundedParentProofEvidence& Evidence)
		{
			if (!InOutEvidenceRecords.ContainsByPredicate(
				[&Evidence](const FBoundedParentProofEvidence& Existing)
				{
					return DoesBoundedParentProofEvidenceMatch(Existing, Evidence);
				}))
			{
				InOutEvidenceRecords.Add(Evidence);
			}
		}

		static void AppendEndpointCommitmentIfMissing(
			TArray<FLayoutCommittedEndpointAnchor>& InOutCommitments,
			const FLayoutCommittedEndpointAnchor& Commitment)
		{
			if (!InOutCommitments.ContainsByPredicate(
				[&Commitment](const FLayoutCommittedEndpointAnchor& Existing)
				{
					return Existing.CommitmentId == Commitment.CommitmentId
						&& Existing.LocalCell == Commitment.LocalCell
						&& Existing.FaceDirection == Commitment.FaceDirection
						&& Existing.ConnectionTag == Commitment.ConnectionTag
						&& Existing.AllowedConnectionTags.HasAllExact(Commitment.AllowedConnectionTags)
						&& Commitment.AllowedConnectionTags.HasAllExact(Existing.AllowedConnectionTags)
						&& Existing.TraversalChannels.HasAllExact(Commitment.TraversalChannels)
						&& Commitment.TraversalChannels.HasAllExact(Existing.TraversalChannels)
						&& Existing.bRequireMatchingYawWithFilledNeighbor == Commitment.bRequireMatchingYawWithFilledNeighbor;
				}))
			{
				InOutCommitments.Add(Commitment);
			}
		}

		// Synthetic SharedHost anchors widen host-ascent proof from retained
		// parent-facing shell contacts, but they are not direct child endpoint
		// obligations and must not be re-exported as proof-request commitments.
		static bool IsProofOnlySharedHostAnchor(
			const FLayoutCommittedEndpointAnchor& Anchor)
		{
			return Anchor.CommitmentId.ToString().Contains(TEXT(".SharedHost."));
		}

		static FLayoutProofRecord MakeProofContractSnapshotProofRecord(
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

		static FLayoutValidationAssertionRecord MakeProofContractSnapshotAssertionRecord(
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

		static void AppendForcedPlacementBundleInsertionIfMissing(
			TArray<FLayoutForcedPlacementBundleInsertion>& InOutInsertions,
			const FLayoutForcedPlacementBundleInsertion& Insertion)
		{
			if (!InOutInsertions.ContainsByPredicate(
				[&Insertion](const FLayoutForcedPlacementBundleInsertion& Existing)
				{
					return Existing.BundleId == Insertion.BundleId
						&& Existing.AnchorCell == Insertion.AnchorCell
						&& Existing.ProvingCell == Insertion.ProvingCell;
				}))
			{
				InOutInsertions.Add(Insertion);
			}
		}

		static void AppendRootTerrainSteppedInsertionPlansToParentRequest(
			FLayoutRegionSolveRequest& ParentRequest,
			const FTerrainSteppedPreparedSolveContract& PreparedSolveContract)
		{
			for (const FTerrainSteppedInsertionPlan& InsertionPlan : PreparedSolveContract.InsertionPlans)
			{
				FLayoutForcedPlacementBundleInsertion Insertion;
				Insertion.BundleId = InsertionPlan.SelectedRootPlacementBundle.BundleId;
				Insertion.AnchorCell = InsertionPlan.BundleAnchorCell;
				Insertion.ProvingCell = InsertionPlan.SupportPlan.SelectedSupportingRootVerticalAccessCell;
				AppendForcedPlacementBundleInsertionIfMissing(
					ParentRequest.ForcedPlacementBundleInsertions,
					Insertion);
			}
		}

		static void AppendRequiredRouteConstraintIfMissing(
			TArray<FLayoutRouteConstraintRecord>& InOutRouteConstraints,
			const FLayoutRouteConstraintRecord& RouteConstraint)
		{
			FLayoutRouteConstraintRecord* ExistingConstraint =
				InOutRouteConstraints.FindByPredicate(
					[&RouteConstraint](const FLayoutRouteConstraintRecord& Existing)
					{
						return Existing.Cell == RouteConstraint.Cell;
					});
			if (ExistingConstraint == nullptr)
			{
				InOutRouteConstraints.Add(RouteConstraint);
				return;
			}

			if (ExistingConstraint->ConstraintId == NAME_None)
			{
				ExistingConstraint->ConstraintId = RouteConstraint.ConstraintId;
			}
			ExistingConstraint->bScoreAsMainRoute =
				ExistingConstraint->bScoreAsMainRoute || RouteConstraint.bScoreAsMainRoute;
			if (ExistingConstraint->Intent != RouteConstraint.Intent)
			{
				InOutRouteConstraints.Add(RouteConstraint);
				return;
			}
			for (const FLayoutRouteFaceRequirement& Requirement : RouteConstraint.FaceRequirements)
			{
				FLayoutRouteFaceRequirement* ExistingRequirement =
					ExistingConstraint->FaceRequirements.FindByPredicate(
						[&Requirement](const FLayoutRouteFaceRequirement& Existing)
						{
							return Existing.FaceDirection == Requirement.FaceDirection;
						});
				if (ExistingRequirement != nullptr)
				{
					if (ExistingRequirement->TraversalChannel != Requirement.TraversalChannel)
					{
						ExistingConstraint->FaceRequirements.Add(Requirement);
					}
				}
				else
				{
					ExistingConstraint->FaceRequirements.Add(Requirement);
				}
			}
		}

		static void AppendRootTerrainSteppedRouteConstraintsToParentRequest(
			FLayoutRegionSolveRequest& ParentRequest,
			const FTerrainSteppedPreparedSolveContract& PreparedSolveContract)
		{
			for (const FLayoutRouteConstraintRecord& RouteConstraint : PreparedSolveContract.RouteConstraints)
			{
				AppendRequiredRouteConstraintIfMissing(
					ParentRequest.RequiredRouteConstraints,
					RouteConstraint);
			}
		}

		static FString BuildProofContractRequestBreadcrumb(const FLayoutRegionSolveRequest& Request)
		{
			return FString::Printf(
				TEXT(" Request '%s' (%s)."),
				*Request.RegionDebugPath,
				*Request.EffectiveSnapshotId.ToString());
		}

		static FString BuildProofContractProfileSnapshotBreadcrumb(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
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

		static FString BuildProofContractContentSetSnapshotBreadcrumb(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
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

		static FString BuildProofContractModuleCatalogBreadcrumb(const FLayoutModuleCatalog& ModuleCatalog)
		{
			return FString::Printf(
				TEXT(" Module-set snapshot %s (%s).%s"),
				*ModuleCatalog.DebugName.ToString(),
				*ModuleCatalog.SnapshotId.ToString(),
				*FString());
		}

		static TArray<FLayoutId> BuildProofContractRelatedIds(
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

		static void UpsertProofContractAssertion(
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

		static void UpsertProofContractProofRecord(
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

		static FString DescribeProofContractNegotiatedHostVerticalAccessResponsibility(
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

		static FString DescribeProofContractNegotiatedChildResponsibilityContract(
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
				*DescribeProofContractNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
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

		void AppendNegotiatedContractProofDiagnosticsToRequestImpl(
			FLayoutRegionSolveRequest& InOutRequest)
		{
			for (const FLayoutNegotiatedChildResponsibilityContract& Contract : InOutRequest.NegotiatedChildResponsibilityContracts)
			{
				const FString ContractDescription =
					DescribeProofContractNegotiatedChildResponsibilityContract(Contract);
				const FLayoutId ContractRecordId(*FString::Printf(
					TEXT("%s.NegotiatedChildResponsibility.%s"),
					*InOutRequest.RegionDebugPath,
					*Contract.ChildRegionDebugPath));
				const TArray<FLayoutId> RelatedIds =
					BuildProofContractRelatedIds(InOutRequest, Contract);

				FString ContractFailureReason;
				const bool bContractValid =
					LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
						Contract,
						ContractFailureReason);
				const FLayoutValidationAssertionRecord AssertionRecord =
					MakeProofContractSnapshotAssertionRecord(
						ContractRecordId,
						ELayoutValidationAssertionKind::ChildCommitmentContractValid,
						bContractValid,
						RelatedIds,
						bContractValid
							? FString()
							: FString::Printf(
								TEXT("Negotiated child responsibility contract is incomplete before region proof starts.\n%s%s%s%s%s\nProblem: Later proof phases would have to rediscover ownership or proceed with an invalid negotiated boundary.\nDetail: %s"),
								*ContractDescription,
								*BuildProofContractRequestBreadcrumb(InOutRequest),
								*BuildProofContractProfileSnapshotBreadcrumb(InOutRequest.ProfileSnapshot),
								*BuildProofContractContentSetSnapshotBreadcrumb(InOutRequest.ContentSetSnapshot),
								*BuildProofContractModuleCatalogBreadcrumb(InOutRequest.ModuleCatalog),
								*ContractFailureReason));
				UpsertProofContractAssertion(InOutRequest.ValidationAssertions, AssertionRecord);

				const FLayoutProofRecord ValidationProofRecord =
					MakeProofContractSnapshotProofRecord(
						ContractRecordId,
						ELayoutProofKind::NormalizedCommitment,
						InOutRequest.EffectiveSnapshotId,
						RelatedIds,
						FString::Printf(
							TEXT("Validated negotiated child responsibility contract before region proof began.\n%s"),
							*ContractDescription));
				if (!bContractValid)
				{
					InOutRequest.ProofRecords.RemoveAll(
						[ContractRecordId](const FLayoutProofRecord& Existing)
						{
							return Existing.ProofId == ContractRecordId;
						});
				}
				else
				{
					UpsertProofContractProofRecord(InOutRequest.ProofRecords, ValidationProofRecord);
				}

				TArray<FLayoutId> SourceIds;
				if (!Contract.ParentRegionDebugPath.IsEmpty())
				{
					SourceIds.Add(FLayoutId(*Contract.ParentRegionDebugPath));
				}
				if (!Contract.ChildRegionDebugPath.IsEmpty())
				{
					SourceIds.Add(FLayoutId(*Contract.ChildRegionDebugPath));
				}

				const FLayoutProofRecord SummaryProofRecord =
					MakeProofContractSnapshotProofRecord(
						FLayoutId(*FString::Printf(
							TEXT("%s.%s.NegotiatedResponsibility"),
							Contract.ParentRegionDebugPath.IsEmpty()
								? TEXT("Root")
								: *Contract.ParentRegionDebugPath,
							Contract.ChildRegionDebugPath.IsEmpty()
								? TEXT("Child")
								: *Contract.ChildRegionDebugPath)),
						ELayoutProofKind::NormalizedCommitment,
						FLayoutId(*Contract.ChildRegionDebugPath),
						SourceIds,
						ContractDescription);
				if (!InOutRequest.ProofRecords.ContainsByPredicate(
					[&SummaryProofRecord](const FLayoutProofRecord& Existing)
					{
						return Existing.ProofId == SummaryProofRecord.ProofId
							&& Existing.ProofKind == SummaryProofRecord.ProofKind
							&& Existing.TargetId == SummaryProofRecord.TargetId
							&& Existing.SourceIds == SummaryProofRecord.SourceIds
							&& Existing.ProofSummary == SummaryProofRecord.ProofSummary;
					}))
				{
					InOutRequest.ProofRecords.Add(SummaryProofRecord);
				}
			}
		}

		static TArray<FLayoutCommittedEndpointAnchor> BuildChildEndpointCommitments(
			const FNegotiatedDemandResult& NegotiatedDemandResult)
		{
			TArray<FLayoutCommittedEndpointAnchor> Commitments;
			for (const FLayoutCommittedEndpointAnchor& EndpointCommitment :
				NegotiatedDemandResult.ResponsibilitySet.ContactSet.EndpointCommitments)
			{
				if (IsProofOnlySharedHostAnchor(EndpointCommitment))
				{
					continue;
				}

				AppendEndpointCommitmentIfMissing(
					Commitments,
					EndpointCommitment);
			}
			const FLayoutNegotiatedChildResponsibilityContract& Contract =
				NegotiatedDemandResult.ResponsibilitySet.ResponsibilityContract;

			for (const FLayoutNegotiatedLevelInterfaceContract& LevelInterface : Contract.CommittedParentChildInterfacesByLevel)
			{
				for (const FLayoutCommittedEndpointAnchor& EndpointAnchor : LevelInterface.EndpointAnchors)
				{
					if (IsProofOnlySharedHostAnchor(EndpointAnchor))
					{
						continue;
					}

					AppendEndpointCommitmentIfMissing(Commitments, EndpointAnchor);
				}
			}
			return Commitments;
		}

		static FLayoutRecursiveVerticalAccessSummary BuildPlannedRecursiveVerticalAccessSummary(
			const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
		{
			FLayoutRecursiveVerticalAccessSummary Summary;
			for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
			{
				if (!Result.bSucceeded)
				{
					continue;
				}

				const FLayoutNegotiatedChildResponsibilityContract& Contract =
					Result.ResponsibilitySet.ResponsibilityContract;
				Summary.RequiredHostProviderCount = FMath::Max(
					Summary.RequiredHostProviderCount,
					Contract.RequiredHostProviderCount);
				Summary.bRequiresExactHostProviderCount =
					Summary.bRequiresExactHostProviderCount
					|| Contract.bRequiresExactHostProviderCount;
				switch (Contract.HostVerticalAccessResponsibility)
				{
				case ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned:
				case ELayoutNegotiatedHostVerticalAccessResponsibility::Composed:
					Summary.ResolvedHostProviderCount +=
						Contract.CountedParentProviderCount;
					break;
				case ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned:
					Summary.ResolvedHostProviderCount +=
						Contract.CountedChildProviderRegionDebugPaths.Num();
					break;
				default:
					break;
				}
				Summary.CountedParentProviderCount = FMath::Max(
					Summary.CountedParentProviderCount,
					Contract.CountedParentProviderCount);
				for (const FIntVector& ParentLocalCell :
					Contract.CountedParentVerticalAccessCells)
				{
					Summary.CountedParentVerticalAccessCells.AddUnique(
						ParentLocalCell);
				}
				for (const FIntVector& RouteSupportCell :
					Contract.RetainedParentRouteSupportVerticalAccessCells)
				{
					Summary.RetainedParentRouteSupportVerticalAccessCells.AddUnique(
						RouteSupportCell);
				}
				for (const FString& RegionPath : Contract.CountedChildProviderRegionDebugPaths)
				{
					Summary.CountedChildRegionDebugPaths.AddUnique(RegionPath);
				}
			}

			Summary.CountedParentVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.Z != Right.Z)
				{
					return Left.Z < Right.Z;
				}
				if (Left.Y != Right.Y)
				{
					return Left.Y < Right.Y;
				}
				return Left.X < Right.X;
			});
			Summary.RetainedParentRouteSupportVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.Z != Right.Z)
				{
					return Left.Z < Right.Z;
				}
				if (Left.Y != Right.Y)
				{
					return Left.Y < Right.Y;
				}
				return Left.X < Right.X;
			});
			Summary.CountedChildRegionDebugPaths.Sort();
			return Summary;
		}

		void ApplyExactJunctionWitnessCarriersToChildRequests(
			const FNegotiatedSeamPlan& SeamPlan,
			TArray<FLayoutRegionSolveRequest>& InOutChildRequests)
		{
			for (FLayoutRegionSolveRequest& ChildRequest : InOutChildRequests)
			{
				for (const FOwnedSeamJunctionRequirement& JunctionRequirement : SeamPlan.JunctionRequirements)
				{
					if (JunctionRequirement.JunctionRequirementId.IsNone())
					{
						continue;
					}
					if (JunctionRequirement.OwnerRegionDebugPath == ChildRequest.RegionDebugPath
						|| JunctionRequirement.PassiveRegionDebugPath == ChildRequest.RegionDebugPath
						|| JunctionRequirement.ContinuingPassiveRegionDebugPath == ChildRequest.RegionDebugPath)
					{
						ChildRequest.CertifiedDelegatedJunctionWitnessIds.AddUnique(JunctionRequirement.JunctionRequirementId);
					}
				}
				ChildRequest.CertifiedDelegatedJunctionWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
				{
					return Left.LexicalLess(Right);
				});
			}
		}

		TArray<FLayoutProducedParentBranchCertificateArtifact> BuildParentBranchCertificates(
			const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
		{
			TArray<FLayoutProducedParentBranchCertificateArtifact> Certificates;
			for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
			{
				if (Result.bSucceeded && Result.ParentBranchCertificate.bHasProducedArtifact)
				{
					Certificates.Add(Result.ParentBranchCertificate);
				}
			}
			Certificates.Sort([](
				const FLayoutProducedParentBranchCertificateArtifact& Left,
				const FLayoutProducedParentBranchCertificateArtifact& Right)
			{
				if (Left.StableChildId != Right.StableChildId)
				{
					return Left.StableChildId < Right.StableChildId;
				}
				return Left.ArtifactId.LexicalLess(Right.ArtifactId);
			});
			return Certificates;
		}

		/**
		 * Local rewrite-owned builder that freezes negotiated structural state into
		 * plain-data proof inputs and deterministic proof-job metadata.
		 */
		struct FLayoutProofContractBuilder
		{
			static FNegotiatedProofScheduleContract Build(
				const FRecursiveScheduleSolveContext& SolveContext,
				const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults,
				const FNegotiatedSeamPlan& CommittedSeamPlan,
				const FTerrainSteppedPreparedSolveContract* RootTerrainSteppedPreparedSolveContract)
			{
				FNegotiatedProofScheduleContract Contract;
				Contract.ParentRequest = SolveContext.RootRequest;
				Contract.RootExternalEndpointCommitments = SolveContext.RootExternalEndpointCommitments;
				Contract.MaxConcurrentChildProofJobs = SolveContext.MaxConcurrentChildProofJobs;
				Contract.PublicationMetadata = SolveContext.PublicationMetadata;
				if (RootTerrainSteppedPreparedSolveContract != nullptr)
				{
					Contract.RootTerrainSteppedPreparedSolveContract = *RootTerrainSteppedPreparedSolveContract;
					AppendRootTerrainSteppedInsertionPlansToParentRequest(
						Contract.ParentRequest,
						Contract.RootTerrainSteppedPreparedSolveContract);
					AppendRootTerrainSteppedRouteConstraintsToParentRequest(
						Contract.ParentRequest,
						Contract.RootTerrainSteppedPreparedSolveContract);
				}
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneForcedPlacementBundleInsertionAssertions(
					Contract.ParentRequest);
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneRequiredRouteConstraintAssertions(
					Contract.ParentRequest);
				Contract.ChildRequests = BuildNegotiatedChildProofRequests(
					SolveContext,
					NegotiatedDemandResults);
				Contract.DirectChildCommitments = BuildNegotiatedDirectChildCommitments(NegotiatedDemandResults);
				Contract.ResponsibilityContracts = BuildNegotiatedResponsibilityContracts(NegotiatedDemandResults);
				Contract.BoundedParentProofEvidenceRecords =
					BuildBoundedParentProofEvidenceRecords(NegotiatedDemandResults);
				Contract.ParentBranchCertificates = BuildParentBranchCertificates(NegotiatedDemandResults);
				Contract.PlannedRecursiveVerticalAccessSummary =
					BuildPlannedRecursiveVerticalAccessSummary(NegotiatedDemandResults);
				for (const FLayoutNegotiatedChildResponsibilityContract& ResponsibilityContract : Contract.ResponsibilityContracts)
				{
					AppendNegotiatedContractIfMissing(
						Contract.ParentRequest.NegotiatedChildResponsibilityContracts,
						ResponsibilityContract);
				}
				AppendNegotiatedContractProofDiagnosticsToRequestImpl(
					Contract.ParentRequest);
				// Exact child placement already settled seam ownership. Proof consumes
				// that committed carrier instead of rediscovering geometry a second time.
				Contract.SeamPlan = CommittedSeamPlan;
				ApplyExactJunctionWitnessCarriersToChildRequests(Contract.SeamPlan, Contract.ChildRequests);
				Contract.OptionalChildPlan = BuildOptionalChildDecisionPlan(SolveContext, NegotiatedDemandResults);
				return Contract;
			}

			static FProofJobPlan BuildJobPlan(
				const FNegotiatedProofScheduleContract& ProofContract)
			{
				FProofJobPlan JobPlan;
				JobPlan.ParentRequest = ProofContract.ParentRequest;
				JobPlan.ChildRequests = ProofContract.ChildRequests;
				JobPlan.PublicationMetadata = ProofContract.PublicationMetadata;
				JobPlan.RootTerrainSteppedPreparedSolveContract =
					ProofContract.RootTerrainSteppedPreparedSolveContract;
				JobPlan.ParentBranchCertificates = ProofContract.ParentBranchCertificates;
				JobPlan.MaxConcurrentChildProofJobs = FMath::Max(1, ProofContract.MaxConcurrentChildProofJobs);
				for (const FLayoutRegionSolveRequest& ChildRequest : ProofContract.ChildRequests)
				{
					JobPlan.StableMergeOrder.Add(ChildRequest.RegionDebugPath);
				}
				JobPlan.StableMergeOrder.Sort();
				return JobPlan;
			}
		};
	}

	void AppendNegotiatedContractProofDiagnosticsToRequest(
		FLayoutRegionSolveRequest& InOutRequest)
	{
		AppendNegotiatedContractProofDiagnosticsToRequestImpl(InOutRequest);
	}

	FNegotiatedProofScheduleContract BuildNegotiatedProofScheduleContract(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults,
		const FNegotiatedSeamPlan& CommittedSeamPlan,
		const FTerrainSteppedPreparedSolveContract* RootTerrainSteppedPreparedSolveContract)
	{
		return FLayoutProofContractBuilder::Build(
			SolveContext,
			NegotiatedDemandResults,
			CommittedSeamPlan,
			RootTerrainSteppedPreparedSolveContract);
	}

#if WITH_AUTOMATION_TESTS
	FNegotiatedProofScheduleContract BuildNegotiatedProofScheduleContract(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		return BuildNegotiatedProofScheduleContract(
			SolveContext,
			NegotiatedDemandResults,
			BuildNegotiatedSeamPlan(SolveContext, NegotiatedDemandResults),
			nullptr);
	}

	FNegotiatedProofScheduleContract BuildNegotiatedProofScheduleContract(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults,
		const FTerrainSteppedPreparedSolveContract* RootTerrainSteppedPreparedSolveContract)
	{
		return BuildNegotiatedProofScheduleContract(
			SolveContext,
			NegotiatedDemandResults,
			BuildNegotiatedSeamPlan(SolveContext, NegotiatedDemandResults),
			RootTerrainSteppedPreparedSolveContract);
	}
#endif

	const FLayoutRegionContentEntrySolveSnapshot* FindContentEntrySnapshotById(
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FName ContentEntryId)
	{
		return ContentSetSnapshot.Entries.FindByPredicate(
			[ContentEntryId](const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
			{
				return EntrySnapshot.EntryId == ContentEntryId;
			});
	}

	bool DoesPotentialPlacementZoneOverlapRequirementZone(
		const ELayoutPlacementZone EntryPlacementZone,
		const ELayoutPlacementZone RequirementZone)
	{
		if (RequirementZone == ELayoutPlacementZone::Any || EntryPlacementZone == ELayoutPlacementZone::Any)
		{
			return true;
		}

		if (RequirementZone == EntryPlacementZone)
		{
			return true;
		}

		switch (RequirementZone)
		{
		case ELayoutPlacementZone::Perimeter:
			return EntryPlacementZone == ELayoutPlacementZone::Edge
				|| EntryPlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Edge:
		case ELayoutPlacementZone::Corner:
			return EntryPlacementZone == ELayoutPlacementZone::Perimeter;
		case ELayoutPlacementZone::Interior:
			return EntryPlacementZone == ELayoutPlacementZone::Core;
		case ELayoutPlacementZone::Core:
			return EntryPlacementZone == ELayoutPlacementZone::Interior;
		case ELayoutPlacementZone::Any:
		default:
			return false;
		}
	}

	bool DoesProvidedZoneFeatureSetMatchRequirement(
		const FGameplayTagContainer& ProvidedFeatures,
		const FLayoutZoneFeatureRequirement& Requirement)
	{
		switch (Requirement.MatchMode)
		{
		case ELayoutZoneFeatureMatchMode::All:
			return ProvidedFeatures.HasAllExact(Requirement.RequiredFeatures);
		case ELayoutZoneFeatureMatchMode::Any:
		default:
			return ProvidedFeatures.HasAnyExact(Requirement.RequiredFeatures);
		}
	}

	TArray<FLayoutId> BuildDelegatedZoneFeatureRequirementIds(
		const FLayoutRegionSolveRequest& ParentRequest,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
	{
		TArray<FLayoutId> RequirementIds;
		for (const FLayoutZoneFeatureRequirement& Requirement : ParentRequest.ProfileSnapshot.ZoneFeatureRequirements)
		{
			if (Requirement.RequiredFeatures.IsEmpty()
				|| !DoesProvidedZoneFeatureSetMatchRequirement(EntrySnapshot.ProvidedZoneFeatures, Requirement)
				|| !DoesPotentialPlacementZoneOverlapRequirementZone(EntrySnapshot.ChildPlacementZone, Requirement.Zone))
			{
				continue;
			}

			RequirementIds.AddUnique(Requirement.RequirementId);
		}
		RequirementIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		return RequirementIds;
	}

	TArray<FLayoutId> BuildDelegatedClosureRequirementIds(
		const FLayoutRegionSolveRequest& ParentRequest,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope)
	{
		TArray<FLayoutId> RequirementIds;
		for (const FLayoutClosureRequirement& Requirement : ParentRequest.ProfileSnapshot.ClosureRequirements)
		{
			if (Requirement.ClosureId.IsNone()
				|| !DoesPotentialPlacementZoneOverlapRequirementZone(EntrySnapshot.ChildPlacementZone, Requirement.Zone))
			{
				continue;
			}

			const bool bCanSupportClosure = CapabilityEnvelope.SpanCapabilities.ContainsByPredicate(
				[&Requirement](const FLayoutChildCapabilitySpan& SpanCapability)
				{
					return SpanCapability.bSealsBoundary
						&& (SpanCapability.ClosureId.IsNone() || SpanCapability.ClosureId == Requirement.ClosureId);
				});
			if (bCanSupportClosure)
			{
				RequirementIds.AddUnique(Requirement.ClosureId);
			}
		}
		RequirementIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		return RequirementIds;
	}

	FLayoutId TryResolveSelectedClosureSpanCapabilityId(
		const TArray<FLayoutId>& DelegatedClosureRequirementIds,
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope)
	{
		TArray<FLayoutId> MatchingCapabilityIds;
		for (const FLayoutId& ClosureId : DelegatedClosureRequirementIds)
		{
			if (ClosureId.IsNone())
			{
				continue;
			}
			for (const FLayoutChildCapabilitySpan& SpanCapability : CapabilityEnvelope.SpanCapabilities)
			{
				if (SpanCapability.CapabilityId.IsNone()
					|| !SpanCapability.bSealsBoundary
					|| (!SpanCapability.ClosureId.IsNone() && SpanCapability.ClosureId != ClosureId))
				{
					continue;
				}
				MatchingCapabilityIds.AddUnique(SpanCapability.CapabilityId);
			}
		}

		return MatchingCapabilityIds.Num() == 1 ? MatchingCapabilityIds[0] : NAME_None;
	}

	void CarryParentBranchCertificateToChildRequest(
		const FLayoutProducedParentBranchCertificateArtifact& Certificate,
		FLayoutRegionSolveRequest& InOutChildRequest)
	{
		if (!Certificate.bHasProducedArtifact
			|| Certificate.StableChildId != InOutChildRequest.RegionDebugPath
			|| Certificate.SourceContentEntryId != InOutChildRequest.SourceContentEntryId)
		{
			return;
		}

		InOutChildRequest.bHasCertifiedParentBranchCertificateArtifact = true;
		InOutChildRequest.CertifiedParentBranchCertificateArtifactId = Certificate.ArtifactId;
		InOutChildRequest.CertifiedParentBranchParentContractId = Certificate.ParentContractId;
		InOutChildRequest.CertifiedParentBranchParentContractHash = Certificate.ParentContractHash;
		InOutChildRequest.CertifiedParentBranchId = Certificate.BranchId;
		InOutChildRequest.CertifiedParentBranchChildScoutResultId = Certificate.ChildScoutResultId;
		InOutChildRequest.CertifiedParentBranchCertificateInputHash = Certificate.CertificateInputHash;

		for (const FLayoutId& EndpointCapabilityId : Certificate.SelectedEndpointCapabilityIds)
		{
			if (!EndpointCapabilityId.IsNone())
			{
				InOutChildRequest.CertifiedSelectedEndpointCapabilityIds.AddUnique(EndpointCapabilityId);
			}
		}
		for (const FLayoutId& VerticalCapabilityId : Certificate.SelectedVerticalCapabilityIds)
		{
			if (!VerticalCapabilityId.IsNone())
			{
				InOutChildRequest.CertifiedSelectedVerticalCapabilityIds.AddUnique(VerticalCapabilityId);
			}
		}
		if (!Certificate.SelectedClosureSpanCapabilityId.IsNone())
		{
			InOutChildRequest.CertifiedSelectedClosureSpanCapabilityId = Certificate.SelectedClosureSpanCapabilityId;
		}
		if (Certificate.SelectedSeamCapabilityIds.Num() == 1)
		{
			InOutChildRequest.CertifiedSelectedSeamCapabilityId = Certificate.SelectedSeamCapabilityIds[0];
		}
		if (Certificate.SelectedSeamWitnessIds.Num() == 1)
		{
			InOutChildRequest.CertifiedSelectedSeamWitnessId = Certificate.SelectedSeamWitnessIds[0];
		}
		for (const FLayoutProducedRequirementToAssertionMapping& Mapping : Certificate.RequirementToAssertionMappings)
		{
			if (!Mapping.AssertionId.IsNone())
			{
				InOutChildRequest.CertifiedParentAssertionSubsetIds.AddUnique(Mapping.AssertionId);
			}
		}
		for (const FLayoutId& FeatureRequirementId : Certificate.DelegatedFeatureRequirementIds)
		{
			if (!FeatureRequirementId.IsNone())
			{
				InOutChildRequest.DelegatedZoneFeatureRequirementIds.AddUnique(FeatureRequirementId);
			}
		}
		for (const FLayoutId& ClosureRequirementId : Certificate.DelegatedClosureRequirementIds)
		{
			if (!ClosureRequirementId.IsNone())
			{
				InOutChildRequest.DelegatedClosureRequirementIds.AddUnique(ClosureRequirementId);
			}
		}
		for (const FLayoutId& SeamWitnessId : Certificate.DelegatedSeamWitnessIds)
		{
			if (!SeamWitnessId.IsNone())
			{
				InOutChildRequest.CertifiedDelegatedSeamWitnessIds.AddUnique(SeamWitnessId);
			}
		}
		for (const FLayoutId& HostVerticalAccessWitnessId : Certificate.DelegatedHostVerticalAccessWitnessIds)
		{
			if (!HostVerticalAccessWitnessId.IsNone())
			{
				InOutChildRequest.CertifiedDelegatedHostVerticalAccessWitnessIds.AddUnique(HostVerticalAccessWitnessId);
			}
		}
		InOutChildRequest.bHasCertifiedParentAssertionSubsetArtifact = !Certificate.RequirementToAssertionMappings.IsEmpty();
		InOutChildRequest.CertifiedSelectedEndpointCapabilityIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedSelectedVerticalCapabilityIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedDelegatedSeamWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.DelegatedZoneFeatureRequirementIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.DelegatedClosureRequirementIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedDelegatedHostVerticalAccessWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedParentAssertionSubsetIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
	}

	void PopulateCertifiedWitnessScaffoldOnChildRequest(
		const FNegotiatedDemandResult& NegotiatedDemandResult,
		FLayoutRegionSolveRequest& InOutChildRequest)
	{
		const FNegotiatedChildCapabilityWitness& CapabilityWitness = NegotiatedDemandResult.CapabilityWitness;
		const FNegotiatedChildObligationWitness& ObligationWitness = NegotiatedDemandResult.ObligationWitness;
		if (CapabilityWitness.ChildRegionDebugPath == InOutChildRequest.RegionDebugPath
			&& CapabilityWitness.SourceContentEntryId == InOutChildRequest.SourceContentEntryId)
		{
			for (const FLayoutId& EndpointCapabilityId : CapabilityWitness.SelectedEndpointCapabilityIds)
			{
				if (!EndpointCapabilityId.IsNone())
				{
					InOutChildRequest.CertifiedSelectedEndpointCapabilityIds.AddUnique(EndpointCapabilityId);
				}
			}
			for (const FLayoutId& VerticalCapabilityId : CapabilityWitness.SelectedVerticalCapabilityIds)
			{
				if (!VerticalCapabilityId.IsNone())
				{
					InOutChildRequest.CertifiedSelectedVerticalCapabilityIds.AddUnique(VerticalCapabilityId);
				}
			}
			if (!CapabilityWitness.SelectedClosureSpanCapabilityId.IsNone())
			{
				InOutChildRequest.CertifiedSelectedClosureSpanCapabilityId = CapabilityWitness.SelectedClosureSpanCapabilityId;
			}
			if (!CapabilityWitness.SelectedSeamCapabilityId.IsNone())
			{
				InOutChildRequest.CertifiedSelectedSeamCapabilityId = CapabilityWitness.SelectedSeamCapabilityId;
			}
			if (!CapabilityWitness.SelectedSeamWitnessId.IsNone())
			{
				InOutChildRequest.CertifiedSelectedSeamWitnessId = CapabilityWitness.SelectedSeamWitnessId;
			}
		}

		for (const FLayoutId& SeamWitnessId : ObligationWitness.DelegatedSeamWitnessIds)
		{
			if (!SeamWitnessId.IsNone())
			{
				InOutChildRequest.CertifiedDelegatedSeamWitnessIds.AddUnique(SeamWitnessId);
			}
		}
		for (const FLayoutId& HostVerticalAccessWitnessId : ObligationWitness.DelegatedHostVerticalAccessWitnessIds)
		{
			if (!HostVerticalAccessWitnessId.IsNone())
			{
				InOutChildRequest.CertifiedDelegatedHostVerticalAccessWitnessIds.AddUnique(HostVerticalAccessWitnessId);
			}
		}

		InOutChildRequest.CertifiedSelectedEndpointCapabilityIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedSelectedVerticalCapabilityIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedDelegatedSeamWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		InOutChildRequest.CertifiedDelegatedHostVerticalAccessWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
	}

	TArray<FLayoutRegionSolveRequest> BuildNegotiatedChildProofRequests(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		TArray<FLayoutRegionSolveRequest> ChildRequests;
		for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
		{
			if (!Result.bSucceeded)
			{
				continue;
			}

			ChildRequests.Add(BuildNegotiatedChildProofRequest(
				Result,
				SolveContext.RootRequest,
				SolveContext.PublicationMetadata));
		}
		ChildRequests.Sort([](const FLayoutRegionSolveRequest& Left, const FLayoutRegionSolveRequest& Right)
		{
			return Left.RegionDebugPath < Right.RegionDebugPath;
		});
		return ChildRequests;
	}

	FLayoutRegionSolveRequest BuildNegotiatedChildProofRequest(
		const FNegotiatedDemandResult& NegotiatedDemandResult,
		const FLayoutRegionSolveRequest& ParentRequest,
		const FQueueCachePublicationMetadata& PublicationMetadata)
	{
		FLayoutRegionSolveRequest ChildRequest = NegotiatedDemandResult.ChildRequest;
		ChildRequest.RootSolveId = PublicationMetadata.RootSolveId;
		ChildRequest.RootCandidateId = PublicationMetadata.RootCandidateId;
		ChildRequest.RootPlacementPolicyId = PublicationMetadata.RootPlacementPolicyId;
		ChildRequest.RootContinuationSelection = PublicationMetadata.RootContinuationSelection;
		for (const FLayoutCommittedEndpointAnchor& EndpointCommitment :
			BuildChildEndpointCommitments(NegotiatedDemandResult))
		{
			AppendEndpointCommitmentIfMissing(
				ChildRequest.CommittedEndpointAnchors,
				EndpointCommitment);
		}
		AppendNegotiatedContractIfMissing(
			ChildRequest.NegotiatedChildResponsibilityContracts,
			NegotiatedDemandResult.ResponsibilitySet.ResponsibilityContract);
		if (const FLayoutRegionContentEntrySolveSnapshot* EntrySnapshot =
				FindContentEntrySnapshotById(ParentRequest.ContentSetSnapshot, ChildRequest.SourceContentEntryId))
		{
			ChildRequest.ProfilePath = EntrySnapshot->ChildProfilePath.IsValid()
				? EntrySnapshot->ChildProfilePath
				: ChildRequest.ProfileSnapshot.SourceProfilePath;
			ChildRequest.DelegatedZoneFeatureRequirementIds =
				BuildDelegatedZoneFeatureRequirementIds(ParentRequest, *EntrySnapshot);
			ChildRequest.DelegatedClosureRequirementIds =
				BuildDelegatedClosureRequirementIds(
					ParentRequest,
					*EntrySnapshot,
					ChildRequest.SuppliedChildCapabilityEnvelope);
			ChildRequest.CertifiedSelectedClosureSpanCapabilityId = TryResolveSelectedClosureSpanCapabilityId(
				ChildRequest.DelegatedClosureRequirementIds,
				ChildRequest.SuppliedChildCapabilityEnvelope);
		}
		else if (!ChildRequest.ProfilePath.IsValid())
		{
			ChildRequest.ProfilePath = ChildRequest.ProfileSnapshot.SourceProfilePath;
		}
		PopulateCertifiedWitnessScaffoldOnChildRequest(NegotiatedDemandResult, ChildRequest);
		CarryParentBranchCertificateToChildRequest(NegotiatedDemandResult.ParentBranchCertificate, ChildRequest);
		AppendNegotiatedContractProofDiagnosticsToRequestImpl(ChildRequest);
		return ChildRequest;
	}


	TArray<FLayoutDirectChildRegionCommitment> BuildNegotiatedDirectChildCommitments(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		TArray<FLayoutDirectChildRegionCommitment> DirectChildCommitments;
		for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
		{
			if (!Result.bSucceeded)
			{
				continue;
			}

			FLayoutDirectChildRegionCommitment& Commitment = DirectChildCommitments.AddDefaulted_GetRef();
			Commitment.ParentRegionDebugPath =
				Result.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath;
			Commitment.ChildRegionDebugPath =
				Result.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath;
			Commitment.StageMapping = Result.StageMapping;
			Commitment.FrozenBoundaryPoints =
				Result.CertifiedChildIncomingBoundaryPoints;
			Commitment.EndpointCommitments = BuildChildEndpointCommitments(Result);
			Commitment.ZoneFeatureProviderCommitments =
				Result.ZoneFeatureProviderCommitments;
			if (!Commitment.StageMapping.IsValid()
				&& Commitment.EndpointCommitments.IsEmpty()
				&& Commitment.ZoneFeatureProviderCommitments.IsEmpty())
			{
				DirectChildCommitments.SetNum(DirectChildCommitments.Num() - 1, EAllowShrinking::No);
				continue;
			}
			Commitment.ParentTraversalIngressCommitments =
				Result.ResponsibilitySet.ContactSet.ParentTraversalIngressAnchors;
			Commitment.bAllowsChildTraversalBridgeForCommittedContacts =
				Result.ResponsibilitySet.ContactSet.bAllowsChildTraversalBridge;
			Commitment.NegotiatedResponsibilityContract =
				Result.ResponsibilitySet.ResponsibilityContract;
		}
		DirectChildCommitments.Sort([](const FLayoutDirectChildRegionCommitment& Left, const FLayoutDirectChildRegionCommitment& Right)
		{
			return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
		});
		return DirectChildCommitments;
	}

	TArray<FLayoutNegotiatedChildResponsibilityContract> BuildNegotiatedResponsibilityContracts(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		TArray<FLayoutNegotiatedChildResponsibilityContract> Contracts;
		for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
		{
			if (Result.bSucceeded)
			{
				AppendNegotiatedContractIfMissing(Contracts, Result.ResponsibilitySet.ResponsibilityContract);
			}
		}
		Contracts.Sort([](const FLayoutNegotiatedChildResponsibilityContract& Left, const FLayoutNegotiatedChildResponsibilityContract& Right)
		{
			return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
		});
		return Contracts;
	}

	TArray<FBoundedParentProofEvidence> BuildBoundedParentProofEvidenceRecords(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		TArray<FBoundedParentProofEvidence> EvidenceRecords;
		for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
		{
			if (Result.bSucceeded
				&& !Result.ResponsibilitySet.BoundedParentProofEvidence.ChildRegionDebugPath.IsEmpty())
			{
				AppendBoundedParentProofEvidenceIfMissing(
					EvidenceRecords,
					Result.ResponsibilitySet.BoundedParentProofEvidence);
			}
		}

		EvidenceRecords.Sort([](const FBoundedParentProofEvidence& Left, const FBoundedParentProofEvidence& Right)
		{
			return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
		});
		return EvidenceRecords;
	}

	TArray<FLayoutProducedParentBranchCertificateArtifact> BuildParentBranchCertificates(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults)
	{
		TArray<FLayoutProducedParentBranchCertificateArtifact> Certificates;
		for (const FNegotiatedDemandResult& Result : NegotiatedDemandResults)
		{
			if (Result.bSucceeded && Result.ParentBranchCertificate.bHasProducedArtifact)
			{
				Certificates.Add(Result.ParentBranchCertificate);
			}
		}
		Certificates.Sort([](
			const FLayoutProducedParentBranchCertificateArtifact& Left,
			const FLayoutProducedParentBranchCertificateArtifact& Right)
		{
			return Left.StableChildId < Right.StableChildId;
		});
		return Certificates;
	}

	FProofJobPlan BuildProofJobPlan(
		const FNegotiatedProofScheduleContract& ProofContract)
	{
		return FLayoutProofContractBuilder::BuildJobPlan(ProofContract);
	}
}
