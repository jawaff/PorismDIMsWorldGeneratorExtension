// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "LayoutRegionScheduleParentProbe.h"

#include "LayoutProfileSolverInternal.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/Crc.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

/**
 * Parent-probe request assembly and solve reuse.
 *
 * This file is intentionally being grown into the efficient replacement for the
 * old inline bridge hot path. The legacy bridge body is reference material;
 * parent-probe ownership should keep consolidating here until the legacy file
 * can be deleted.
 */
namespace LayoutRegionScheduleSolverPrivate
{
	FString TagsToStableKey(const FGameplayTagContainer& Tags);
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
	bool TryBuildParentPlannedCellsWithReservedChildren(
		const TArray<FLayoutPlannedCell>& BaseParentPlannedCells,
		const TSet<FIntVector>& BaseParentPlannedCellSet,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ReservedCells,
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
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString* OutFailureReason);
	bool DoesParentPlanKeepAnchorCellsConnected(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TArray<FAutomaticChildPlacement>& ExistingPlacements,
		const TArray<FIntVector>& CandidateParentContactCells,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FLayoutCommittedTraversalAnchor>* CandidateParentTraversalAnchors,
		bool bAllowsChildTraversalBridgeForCommittedContacts,
		FString* OutFailureReason);
	bool ValidateCommittedEndpointAnchorCoverageOnPlacements(
		const FString& RegionDebugPath,
		const FIntVector& RegionOffset,
		const TArray<FLayoutPlacedModule>& Placements,
		const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
		FString& OutFailureReason);
	void AppendSyntheticParentSupportBoundaryPoints(
		const TArray<FIntVector>& ChildSupportingCells,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const FIntVector& ChildRegionOffset,
		const FString& ChildRegionDebugPath,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const FLayoutProfileSolveSnapshot& ParentProfileSnapshot,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedParentCells,
		const TArray<FSharedParentChildFace>& SharedParentChildFaces,
		FLayoutId ChildBoundaryCertificateId,
		const TArray<FLayoutCellCandidateDomainRestriction>& CertifiedParentRestrictions,
		TArray<FLayoutSolveBoundaryPoint>& InOutBoundaryPoints);

	namespace
	{
		FIntVector ParentProbeTranslateCell(const FIntVector& Cell, const FIntVector& Offset)
		{
			return Cell + Offset;
		}

		FString BuildParentProbeStableDigest(TArray<FString>& Parts)
		{
			if (Parts.IsEmpty())
			{
				return TEXT("<none>");
			}

			Parts.Sort();
			uint32 Digest = 0;
			for (const FString& Part : Parts)
			{
				Digest = HashCombineFast(Digest, GetTypeHash(Part));
			}
			return FString::Printf(TEXT("%08x:%d"), Digest, Parts.Num());
		}

		FString ParentProbeBuildCommittedEndpointAnchorsKey(const TArray<FLayoutCommittedEndpointAnchor>& Commitments)
		{
			TArray<FString> CommitmentParts;
			CommitmentParts.Reserve(Commitments.Num());
			for (const FLayoutCommittedEndpointAnchor& Commitment : Commitments)
			{
				CommitmentParts.Add(FString::Printf(
					TEXT("%s:%s:%d:%d:%s:%s:%s:%d"),
					Commitment.CommitmentId == NAME_None ? TEXT("<none>") : *Commitment.CommitmentId.ToString(),
					*Commitment.LocalCell.ToString(),
					static_cast<int32>(Commitment.FaceDirection),
					Commitment.RequiredWorldCenterBlockZ,
					Commitment.ConnectionTag.IsValid() ? *Commitment.ConnectionTag.ToString() : TEXT("<none>"),
					*TagsToStableKey(Commitment.AllowedConnectionTags),
					*TagsToStableKey(Commitment.TraversalChannels),
					Commitment.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
			}
			return BuildParentProbeStableDigest(CommitmentParts);
		}

		FString ParentProbeBuildParentContactCellsKey(const TArray<FIntVector>& ParentContactCells)
		{
			TArray<FString> Parts;
			Parts.Reserve(ParentContactCells.Num());
			for (const FIntVector& Cell : ParentContactCells)
			{
				Parts.Add(Cell.ToString());
			}
			return BuildParentProbeStableDigest(Parts);
		}

		FString ParentProbeBuildCommittedTraversalAnchorsKey(const TArray<FLayoutCommittedTraversalAnchor>& Anchors)
		{
			TArray<FString> Parts;
			Parts.Reserve(Anchors.Num());
			for (const FLayoutCommittedTraversalAnchor& Anchor : Anchors)
			{
				Parts.Add(FString::Printf(
					TEXT("%s:%s"),
					*Anchor.Cell.ToString(),
					Anchor.TraversalChannel.IsValid() ? *Anchor.TraversalChannel.ToString() : TEXT("<none>")));
			}
			return BuildParentProbeStableDigest(Parts);
		}

		FString ParentProbeBuildSharedParentChildFacesKey(const TArray<FSharedParentChildFace>& SharedParentChildFaces)
		{
			TArray<FString> SeamParts;
			SeamParts.Reserve(SharedParentChildFaces.Num());
			for (const FSharedParentChildFace& SharedFace : SharedParentChildFaces)
			{
				SeamParts.Add(BuildSharedParentChildFaceIdentityPart(SharedFace));
			}
			return BuildParentProbeStableDigest(SeamParts);
		}

		FString ParentProbeBuildCommittedVerticalAccessOwnershipKey(const FCommittedVerticalAccessOwnership& Ownership)
		{
			TArray<FString> ParentCellParts;
			ParentCellParts.Reserve(Ownership.CountedParentVerticalAccessCells.Num());
			for (const FIntVector& Cell : Ownership.CountedParentVerticalAccessCells)
			{
				ParentCellParts.Add(Cell.ToString());
			}
			TArray<FString> RouteSupportCellParts;
			RouteSupportCellParts.Reserve(Ownership.RetainedParentRouteSupportVerticalAccessCells.Num());
			for (const FIntVector& Cell : Ownership.RetainedParentRouteSupportVerticalAccessCells)
			{
				RouteSupportCellParts.Add(Cell.ToString());
			}

			TArray<FString> CountedChildrenParts = Ownership.CountedChildRegionDebugPaths;
			return FString::Printf(
				TEXT("Required=%d|ParentPotential=%d|ParentCount=%d|ParentCells=%s|RouteSupportCells=%s|CountedChildren=%s"),
				Ownership.RequiredHostProviderCount,
				Ownership.PotentialParentProviderCount,
				Ownership.CountedParentProviderCount,
				*BuildParentProbeStableDigest(ParentCellParts),
				*BuildParentProbeStableDigest(RouteSupportCellParts),
				*BuildParentProbeStableDigest(CountedChildrenParts));
		}

		FString ParentProbeBuildPlannedCellsKey(
			const TArray<FLayoutPlannedCell>& PlannedCells);
		FString ParentProbeBuildBoundaryPointsKey(
			const TArray<FLayoutSolveBoundaryPoint>& BoundaryPoints);

		FString ParentProbeBuildParentStructuralFeasibilityMemoKey(
			const TArray<FLayoutPlannedCell>& PreparedParentPlannedCells,
			const TArray<FLayoutSolveBoundaryPoint>& ParentTranslatedBoundaryPoints,
			const TArray<FIntVector>& CandidateParentContactCells,
			const TArray<FLayoutCommittedTraversalAnchor>& CandidateParentTraversalAnchors,
			bool bAllowsChildTraversalBridgeForCommittedContacts,
			const FCommittedVerticalAccessOwnership& VerticalAccessOwnership)
		{
			return FString::Printf(
				TEXT("Planned=%s|Boundary=%s|Contacts=%s|TraversalAnchors=%s|BridgeContacts=%d|VerticalOwner=%s"),
				*ParentProbeBuildPlannedCellsKey(
					PreparedParentPlannedCells),
				*ParentProbeBuildBoundaryPointsKey(
					ParentTranslatedBoundaryPoints),
				*ParentProbeBuildParentContactCellsKey(CandidateParentContactCells),
				*ParentProbeBuildCommittedTraversalAnchorsKey(CandidateParentTraversalAnchors),
				bAllowsChildTraversalBridgeForCommittedContacts ? 1 : 0,
				*ParentProbeBuildCommittedVerticalAccessOwnershipKey(VerticalAccessOwnership));
		}


		const FLayoutChildCapabilityEndpoint* ParentProbeFindSummaryEntryCapabilityForFace(
			const FPlacementBridgeChildInterfaceSummary& InterfaceSummary,
			ELayoutFaceDirection FaceDirection)
		{
			return InterfaceSummary.EntryCapabilities.FindByPredicate(
				[FaceDirection](const FLayoutChildCapabilityEndpoint& Capability)
				{
					return Capability.FaceDirection == FaceDirection;
				});
		}

		TArray<FLayoutSolveBoundaryPoint> ParentProbeBuildSyntheticExportedBoundaryPointsFromChildSummary(
			const FString& ChildRegionDebugPath,
			const FIntVector& CandidateOffset,
			const TSet<FIntVector>& ParentPlannedCellSet,
			const FPlacementBridgeChildInterfaceSummary& InterfaceSummary,
			const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
			const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces,
			const TArray<FIntVector>& CandidateParentPlanReservedCells,
			bool bChildContributesHostVerticalAccess,
			int32 ChildMaxLocalLevel)
		{
			TArray<FLayoutSolveBoundaryPoint> BoundaryPoints;
			static const ELayoutFaceDirection LateralDirections[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY
			};
			for (const FPlacementBridgeChildInterfaceSummary::FBoundaryFace& BoundaryFace : InterfaceSummary.BoundaryFaces)
			{
				const FLayoutCommittedEndpointAnchor* MatchingCommitment = CandidateCommitments.FindByPredicate(
					[&](const FLayoutCommittedEndpointAnchor& Commitment)
					{
						return Commitment.LocalCell == BoundaryFace.LocalCell
							&& Commitment.FaceDirection == BoundaryFace.FaceDirection;
					});
				const FSharedParentChildFace* MatchingSharedFace =
					CandidateSharedParentChildFaces.FindByPredicate(
						[&](const FSharedParentChildFace& SharedFace)
						{
							return SharedFace.ChildLocalCell == BoundaryFace.LocalCell
								&& SharedFace.FaceDirection == BoundaryFace.FaceDirection;
						});
				// Exact shared-shell ownership is enforced by the seam domain. Only
				// a direct endpoint commitment may add a separate route boundary.
				if (MatchingSharedFace != nullptr
					&& MatchingCommitment == nullptr)
				{
					continue;
				}
				const bool bIsVerticalBoundaryFace =
					BoundaryFace.FaceDirection == ELayoutFaceDirection::PosZ
					|| BoundaryFace.FaceDirection ==
						ELayoutFaceDirection::NegZ;
				const bool bHasExplicitBoundaryContract =
					MatchingCommitment != nullptr
					|| !BoundaryFace.TraversalChannels.IsEmpty()
					|| (MatchingSharedFace != nullptr
						&& !MatchingSharedFace->TraversalChannels.IsEmpty());
				if (!bHasExplicitBoundaryContract
					&& !bIsVerticalBoundaryFace)
				{
					// Generic same-level child shell ownership is already preserved by the
					// reserved-cell derived parent plan. Only export lateral boundary points
					// when the child summary proves an explicit parent-facing contract.
					continue;
				}
				if (MatchingCommitment == nullptr
					&& bIsVerticalBoundaryFace
					&& BoundaryFace.TraversalChannels.IsEmpty())
				{
					continue;
				}

				const FIntVector ParentNeighborCell = ParentProbeTranslateCell(
					BoundaryFace.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryFace.FaceDirection),
					CandidateOffset);
				if (!ParentPlannedCellSet.Contains(ParentNeighborCell))
				{
					continue;
				}

				const FIntVector TranslatedChildCell = ParentProbeTranslateCell(BoundaryFace.LocalCell, CandidateOffset);
				const FGameplayTag EffectiveConnectionTag =
					MatchingSharedFace != nullptr && MatchingSharedFace->ConnectionTag.IsValid()
						? MatchingSharedFace->ConnectionTag
						: BoundaryFace.ConnectionTag;
				const FGameplayTagContainer& EffectiveAllowedConnectionTags =
					MatchingSharedFace != nullptr && !MatchingSharedFace->AllowedConnectionTags.IsEmpty()
						? MatchingSharedFace->AllowedConnectionTags
						: BoundaryFace.AllowedConnectionTags;
				if ((MatchingCommitment != nullptr || MatchingSharedFace != nullptr)
					&& EffectiveConnectionTag.IsValid()
					&& !EffectiveAllowedConnectionTags.IsEmpty())
				{
					FLayoutSolveBoundaryPoint& BoundaryPoint = BoundaryPoints.AddDefaulted_GetRef();
					BoundaryPoint.LocalCell = TranslatedChildCell;
					BoundaryPoint.FaceDirection = BoundaryFace.FaceDirection;
					BoundaryPoint.SourceRegionDebugPath = ChildRegionDebugPath;
					BoundaryPoint.SourceCell = TranslatedChildCell;
					// Parent/child interfaces are internal contract boundaries. Exact
					// reciprocal evidence owns compatibility; parent modules do not need
					// an authored exterior-facing boundary requirement on this face.
					BoundaryPoint.bRequiresBoundaryFacing = false;
					BoundaryPoint.ConnectionTag = EffectiveConnectionTag;
					BoundaryPoint.AllowedConnectionTags = EffectiveAllowedConnectionTags;
					BoundaryPoint.ConnectedTraversalChannels =
						MatchingSharedFace != nullptr
							? MatchingSharedFace->TraversalChannels
							: BoundaryFace.TraversalChannels;
					if (MatchingCommitment != nullptr)
					{
						if (!MatchingCommitment->TraversalChannels.IsEmpty())
						{
							BoundaryPoint.ConnectedTraversalChannels =
								MatchingCommitment->TraversalChannels;
						}
						BoundaryPoint.bRequireMatchingYawWithFilledNeighbor =
							MatchingCommitment
								->bRequireMatchingYawWithFilledNeighbor;
						BoundaryPoint.CommitmentId =
							MatchingCommitment->CommitmentId;
					}

					if (BoundaryFace.Intent == ELayoutCellIntent::Entry)
					{
						if (const FLayoutChildCapabilityEndpoint* MatchingCapability =
							ParentProbeFindSummaryEntryCapabilityForFace(InterfaceSummary, BoundaryFace.FaceDirection))
						{
							if (BoundaryPoint.ConnectedTraversalChannels.IsEmpty())
							{
								BoundaryPoint.ConnectedTraversalChannels = MatchingCapability->TraversalChannels;
							}
						}
					}
					else if (BoundaryFace.Intent == ELayoutCellIntent::VerticalAccess
						&& BoundaryPoint.ConnectedTraversalChannels.IsEmpty()
						&& InterfaceSummary.VerticalAccessCells.Contains(BoundaryFace.LocalCell)
						&& (BoundaryFace.FaceDirection == ELayoutFaceDirection::PosZ
							|| BoundaryFace.FaceDirection == ELayoutFaceDirection::NegZ))
					{
						BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
					}
				}
			}

			// Direct child Entry commitments remain authoritative parent route
			// anchors when broad interface-summary matching omitted
			// the commitment id from an otherwise equivalent boundary carrier.
			for (const FLayoutCommittedEndpointAnchor& Commitment : CandidateCommitments)
			{
				const FIntVector TranslatedChildCell = ParentProbeTranslateCell(
					Commitment.LocalCell,
					CandidateOffset);
				const FIntVector ParentContactCell = TranslatedChildCell
					+ FLayoutDirectionUtils::ToCellDelta(Commitment.FaceDirection);
				if (!ParentPlannedCellSet.Contains(ParentContactCell))
				{
					continue;
				}

				FLayoutSolveBoundaryPoint* BoundaryPoint = BoundaryPoints.FindByPredicate(
					[&](const FLayoutSolveBoundaryPoint& Existing)
					{
						return Existing.LocalCell == TranslatedChildCell
							&& Existing.FaceDirection == Commitment.FaceDirection;
					});
				if (BoundaryPoint == nullptr)
				{
					BoundaryPoint = &BoundaryPoints.AddDefaulted_GetRef();
					BoundaryPoint->LocalCell = TranslatedChildCell;
					BoundaryPoint->FaceDirection = Commitment.FaceDirection;
					BoundaryPoint->SourceRegionDebugPath = ChildRegionDebugPath;
					BoundaryPoint->SourceCell = TranslatedChildCell;
					BoundaryPoint->bRequiresBoundaryFacing = false;
				}
				BoundaryPoint->ConnectionTag = Commitment.ConnectionTag;
				BoundaryPoint->AllowedConnectionTags = Commitment.AllowedConnectionTags;
				BoundaryPoint->ConnectedTraversalChannels = Commitment.TraversalChannels;
				BoundaryPoint->bRequireMatchingYawWithFilledNeighbor =
					Commitment.bRequireMatchingYawWithFilledNeighbor;
				BoundaryPoint->CommitmentId = Commitment.CommitmentId;
			}

			if (bChildContributesHostVerticalAccess)
			{
				const int32 ReservedTopLocalLevel = FMath::Max(ChildMaxLocalLevel, 1);
				if (ReservedTopLocalLevel > ChildMaxLocalLevel)
				{
					TSet<FIntVector> ExtraReservedLocalCells;
					for (const FIntVector& ReservedParentCell : CandidateParentPlanReservedCells)
					{
						const FIntVector LocalCell = ReservedParentCell - CandidateOffset;
						if (LocalCell.Z > ChildMaxLocalLevel)
						{
							ExtraReservedLocalCells.Add(LocalCell);
						}
					}

					for (int32 ExtraLocalLevel = ChildMaxLocalLevel + 1; ExtraLocalLevel <= ReservedTopLocalLevel; ++ExtraLocalLevel)
					{
						for (const FIntVector& ExtraReservedLocalCell : ExtraReservedLocalCells)
						{
							if (ExtraReservedLocalCell.Z != ExtraLocalLevel)
							{
								continue;
							}

							const FIntVector TranslatedChildCell = ParentProbeTranslateCell(ExtraReservedLocalCell, CandidateOffset);
							for (const ELayoutFaceDirection Direction : LateralDirections)
							{
								const FIntVector NeighborLocalCell = ExtraReservedLocalCell + FLayoutDirectionUtils::ToCellDelta(Direction);
								if (ExtraReservedLocalCells.Contains(NeighborLocalCell))
								{
									continue;
								}

								const FIntVector ParentNeighborCell = ParentProbeTranslateCell(NeighborLocalCell, CandidateOffset);
								if (!ParentPlannedCellSet.Contains(ParentNeighborCell))
								{
									continue;
								}

								const bool bAlreadyPresent = BoundaryPoints.ContainsByPredicate(
									[&](const FLayoutSolveBoundaryPoint& ExistingBoundaryPoint)
									{
										return ExistingBoundaryPoint.LocalCell == TranslatedChildCell
											&& ExistingBoundaryPoint.FaceDirection == Direction;
									});
								if (bAlreadyPresent)
								{
									continue;
								}

								FLayoutSolveBoundaryPoint& BoundaryPoint = BoundaryPoints.AddDefaulted_GetRef();
								BoundaryPoint.LocalCell = TranslatedChildCell;
								BoundaryPoint.FaceDirection = Direction;
								BoundaryPoint.SourceRegionDebugPath = ChildRegionDebugPath;
								BoundaryPoint.SourceCell = TranslatedChildCell;
								BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
								BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
								BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
								BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
							}
						}
					}
				}
			}

			return BoundaryPoints;
		}

		FString ParentProbeBuildPlannedCellsKey(const TArray<FLayoutPlannedCell>& PlannedCells)
		{
			TArray<FString> Parts;
			Parts.Reserve(PlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
			{
				Parts.Add(FString::Printf(TEXT("%s:%d"), *PlannedCell.Cell.ToString(), static_cast<int32>(PlannedCell.Intent)));
			}
			return BuildParentProbeStableDigest(Parts);
		}

		FString ParentProbeBuildBoundaryPointsKey(const TArray<FLayoutSolveBoundaryPoint>& BoundaryPoints)
		{
			TArray<FString> Parts;
			Parts.Reserve(BoundaryPoints.Num());
			for (const FLayoutSolveBoundaryPoint& BoundaryPoint : BoundaryPoints)
			{
				Parts.Add(FString::Printf(
					TEXT("%s:%d:%s:%s:%s:%d:%d:%d:%s:%s:%d:%s:%d:%s:%s"),
					*BoundaryPoint.LocalCell.ToString(),
					static_cast<int32>(BoundaryPoint.FaceDirection),
					BoundaryPoint.ConnectionTag.IsValid() ? *BoundaryPoint.ConnectionTag.ToString() : TEXT("<none>"),
					*TagsToStableKey(BoundaryPoint.AllowedConnectionTags),
					*TagsToStableKey(BoundaryPoint.ConnectedTraversalChannels),
					BoundaryPoint.bRepresentsFilledNeighbor ? 1 : 0,
					BoundaryPoint.bRequiresBoundaryFacing ? 1 : 0,
					BoundaryPoint.bRequireMatchingYawWithFilledNeighbor ? 1 : 0,
					*BoundaryPoint.SourceRegionDebugPath,
					*BoundaryPoint.SourceCell.ToString(),
					BoundaryPoint.SourceYawRotationSteps,
					BoundaryPoint.CommitmentId == NAME_None ? TEXT("<none>") : *BoundaryPoint.CommitmentId.ToString(),
					BoundaryPoint.bUsesCertifiedReciprocalDomain ? 1 : 0,
					BoundaryPoint.CertifiedDomainCertificateId.IsNone() ? TEXT("<none>") : *BoundaryPoint.CertifiedDomainCertificateId.ToString(),
					BoundaryPoint.CertifiedDomainRestrictionId.IsNone() ? TEXT("<none>") : *BoundaryPoint.CertifiedDomainRestrictionId.ToString()));
			}
			return BuildParentProbeStableDigest(Parts);
		}

		struct FResolvedParentProbeMutableInputs
		{
			const TArray<FAutomaticChildPlacement>& CurrentPlacements;
			const TSet<FIntVector>& CurrentReservedParentCells;
			const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections;
			const TSet<FIntVector>& ParentProtectedTraversalCells;
		};

		struct FPreparedParentProbeSolve
		{
			FCommittedVerticalAccessOwnership VerticalAccessOwnership;
			FLayoutRegionSolveRequest Request;
			FString StructuralMemoKey;
		};

		const FResolvedParentProbeMutableInputs ResolveParentProbeMutableInputs(
			const FPlacementBridgeSolveContext& SolveContext,
			const FPlacementBridgeMutableStateView& MutableState)
		{
			static const TArray<FAutomaticChildPlacement> EmptyPlacements;
			static const TSet<FIntVector> EmptyReservedCells;
			static const TSet<ELayoutFaceDirection> EmptyIngressDirections;
			static const TSet<FIntVector> EmptyProtectedTraversalCells;

			return {
				MutableState.CurrentPlacements != nullptr ? *MutableState.CurrentPlacements : EmptyPlacements,
				MutableState.CurrentReservedParentCells != nullptr ? *MutableState.CurrentReservedParentCells : EmptyReservedCells,
				SolveContext.ParentVerticalAccessIngressDirections != nullptr ? *SolveContext.ParentVerticalAccessIngressDirections : EmptyIngressDirections,
				SolveContext.ParentProtectedTraversalCells != nullptr ? *SolveContext.ParentProtectedTraversalCells : EmptyProtectedTraversalCells};
		}

		void StoreParentProbeMemoResult(
			const FString& MemoKey,
			const bool bSucceeded,
			const FPlacementBridgeParentProbeResult& Result,
			TMap<FString, FPlacementBridgeParentProbeMemoEntry>& InOutMemo)
		{
			FPlacementBridgeParentProbeMemoEntry& MemoEntry = InOutMemo.FindOrAdd(MemoKey);
			MemoEntry.bSucceeded = bSucceeded;
			MemoEntry.Result = Result;
		}

		void InitializePreparedParentProbeResult(
			const FPreparedParentProbeCandidate& PreparedCandidate,
			FPlacementBridgeParentProbeResult& OutResult)
		{
			OutResult = FPlacementBridgeParentProbeResult();
			OutResult.ParentCommittedTraversalAnchors = PreparedCandidate.ParentCommittedTraversalAnchors;
			OutResult.ParentTranslatedBoundaryPoints = PreparedCandidate.ParentTranslatedBoundaryPoints;
			OutResult.ParentTranslatedFutureTerraceBoundaryPoints =
				PreparedCandidate.ParentTranslatedFutureTerraceBoundaryPoints;
		}

		bool ShouldRunImmediateParentProof(const FPlacementBridgeParentProbeCandidate& Candidate)
		{
			static bool bEnableRecursiveParentOnlyProbe = true;
			const bool bRequireEarlySingleDemandParentOnlyProbe = false;
			const bool bShouldRunImmediateFullParentProof =
				Candidate.bRunImmediateFullParentProof || bRequireEarlySingleDemandParentOnlyProbe;
			return bShouldRunImmediateFullParentProof
				&& bEnableRecursiveParentOnlyProbe
				&& !Candidate.bDeferFullParentProofToCompleteValidation;
		}

		bool CandidateAddsExpensiveParentProofBurden(
			const FPlacementBridgeParentProbeCandidate& Candidate,
			const FPlacementBridgeParentProbeResult& ProbeResult)
		{
			if (Candidate.bRunImmediateFullParentProof
				|| !Candidate.DirectParentContactCommitments.IsEmpty()
				|| !Candidate.CandidateParentContactCells.IsEmpty()
				|| !ProbeResult.ParentCommittedTraversalAnchors.IsEmpty()
				|| !Candidate.CandidateSharedParentChildFaces.IsEmpty())
			{
				return true;
			}

			return ProbeResult.ParentTranslatedBoundaryPoints.ContainsByPredicate(
				[](const FLayoutSolveBoundaryPoint& BoundaryPoint)
				{
					return BoundaryPoint.CommitmentId != NAME_None
						|| !BoundaryPoint.ConnectedTraversalChannels.IsEmpty();
				});
		}

		bool TryPrepareParentProbeSolve(
			const FPlacementBridgeSolveContext& SolveContext,
			const FResolvedParentProbeMutableInputs& MutableInputs,
			const FPlacementBridgeParentProbeCandidate& Candidate,
			const FPreparedParentProbeResidualPlan* PreparedResidualPlan,
			FPreparedParentProbeSolve& OutPreparedSolve,
			FString& OutFailureReason)
		{
			SCOPED_NAMED_EVENT(Layout_ParentProbe_ResidualPlan, FColor::Orange);
			if (PreparedResidualPlan != nullptr)
			{
				OutPreparedSolve.VerticalAccessOwnership =
					PreparedResidualPlan->VerticalAccessOwnership;
			}
			else
			{
				OutPreparedSolve.VerticalAccessOwnership = DetermineCommittedVerticalAccessOwnershipForProspectiveCandidate(
					*SolveContext.RootRequest,
					*SolveContext.ParentPlannedCells,
					MutableInputs.CurrentReservedParentCells,
					MutableInputs.CurrentPlacements,
					Candidate.PlanningVariant.ChildRegionDebugPath,
					Candidate.PlanningVariant.InterfaceSummary.bSupportsVerticalAccess,
					Candidate.PlanningVariant.bChildContributesHostVerticalAccess,
					Candidate.PlanningVariant.InterfaceSummary.VerticalAccessCells,
					Candidate.CandidateParentPlanReservedCells,
					Candidate.PlanningVariant.PlannedCells,
					Candidate.CandidateCommitments,
					Candidate.CandidateSharedParentChildFaces);
			}
			OutPreparedSolve.Request = *SolveContext.RootRequest;
			/* !OutPreparedSolve.Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
			OutPreparedSolve.Request.FootprintSize = SolveContext.ParentFootprintSize;
			OutPreparedSolve.Request.RegionCellOffset = FIntVector::ZeroValue;
			OutPreparedSolve.Request.PlannedCells.Reset();
			OutPreparedSolve.Request.IncomingBoundaryPoints = SolveContext.RootRequest->IncomingBoundaryPoints;

			TSet<FIntVector> ProbeReservedCells;
			TSet<FIntVector> ProbeProtectedTraversalCells = MutableInputs.ParentProtectedTraversalCells;
			for (const FAutomaticChildPlacement& ExistingPlacement : MutableInputs.CurrentPlacements)
			{
				for (const FIntVector& ReservedCell : ExistingPlacement.ParentPlanReservedCells)
				{
					ProbeReservedCells.Add(ReservedCell);
				}
				for (const FIntVector& ParentContactCell : ExistingPlacement.ParentCommittedEntryCells)
				{
					ProbeProtectedTraversalCells.Add(ParentContactCell);
				}
			}
			for (const FIntVector& ReservedCell : Candidate.CandidateParentPlanReservedCells)
			{
				ProbeReservedCells.Add(ReservedCell);
			}
			for (const FIntVector& ParentContactCell : Candidate.CandidateParentContactCells)
			{
				ProbeProtectedTraversalCells.Add(ParentContactCell);
			}

			if (PreparedResidualPlan != nullptr)
			{
				// Local project optimization: commitment checks already settled this exact
				// residual plan, so the parent probe must not settle it a second time.
				OutPreparedSolve.Request.PlannedCells = PreparedResidualPlan->PlannedCells;
				OutPreparedSolve.Request.VerticalAccessHostGroups =
					PreparedResidualPlan->FilteredVerticalAccessHostGroups;
			}
			else if (!TryBuildParentPlannedCellsWithReservedChildren(
				*SolveContext.ParentPlannedCells,
				*SolveContext.ParentPlannedCellSet,
				SolveContext.ParentFootprintSize,
				ProbeReservedCells,
				ProbeProtectedTraversalCells,
				SolveContext.RootRequest->Seed,
				SolveContext.RootRequest->IncomingBoundaryPoints.IsEmpty(),
				OutPreparedSolve.VerticalAccessOwnership,
				SolveContext.RootRequest->VerticalAccessHostGroups,
				OutPreparedSolve.Request.PlannedCells,
				&OutPreparedSolve.Request.VerticalAccessHostGroups,
				&OutFailureReason))
			{
				return false;
			}

			OutPreparedSolve.Request.ExternalPlannedNeighborFaceMasks.Reset();
			for (const FLayoutPlannedCell& ParentCell : OutPreparedSolve.Request.PlannedCells)
			{
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					const ELayoutFaceDirection Direction =
						static_cast<ELayoutFaceDirection>(DirectionIndex);
					if (ProbeReservedCells.Contains(
						ParentCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction)))
					{
						OutPreparedSolve.Request.ExternalPlannedNeighborFaceMasks.FindOrAdd(
							ParentCell.Cell) |= LayoutFaceDirectionMask(Direction);
					}
				}
			}

			if (PreparedResidualPlan == nullptr
				&& !RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
					OutPreparedSolve.Request.PlannedCells,
					OutPreparedSolve.VerticalAccessOwnership,
					&OutFailureReason))
			{
				return false;
			}

			return true;
		}

		bool EvaluatePreparedParentProbeStructuralFeasibilityImpl(
			const FResolvedParentProbeMutableInputs& MutableInputs,
			const FPreparedParentProbeCandidate& PreparedCandidate,
			const FPreparedParentProbeSolve& PreparedSolve,
			TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
			FPlacementBridgeParentProbeResult& InOutResult)
		{
			const FPlacementBridgeParentProbeCandidate& Candidate = PreparedCandidate.Candidate;
			FString AnchorConnectivityFailureReason;
			if (const FPlacementBridgeStructuralFeasibilityMemoEntry* ExistingStructuralMemo =
				InOutStructuralFeasibilityMemo.Find(PreparedSolve.StructuralMemoKey))
			{
				if (ExistingStructuralMemo->bSucceeded)
				{
					return true;
				}

				InOutResult.bFailedStructuralFeasibility = true;
				InOutResult.FailureReason = ExistingStructuralMemo->FailureReason;
				return false;
			}

			auto FindIncomingBoundaryPointForCellFace =
				[&PreparedCandidate](const FIntVector& Cell, const ELayoutFaceDirection Direction)
				-> const FLayoutSolveBoundaryPoint*
			{
				const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const ELayoutFaceDirection BoundaryDirection =
					FLayoutDirectionUtils::GetOpposite(Direction);
				return PreparedCandidate.ParentTranslatedBoundaryPoints.FindByPredicate(
					[&](const FLayoutSolveBoundaryPoint& BoundaryPoint)
					{
						return BoundaryPoint.LocalCell == NeighborCell
							&& BoundaryPoint.FaceDirection == BoundaryDirection;
					});
			};

			if (!Candidate.PlanningVariant.bChildContributesHostVerticalAccess
				&& Candidate.CandidateParentContactCells.Num() > 1
				&& PreparedSolve.VerticalAccessOwnership.CountedParentProviderCount > 0)
			{
				TSet<FIntVector> PlannedCellSet;
				PlannedCellSet.Reserve(PreparedSolve.Request.PlannedCells.Num());
				for (const FLayoutPlannedCell& PlannedCell : PreparedSolve.Request.PlannedCells)
				{
					PlannedCellSet.Add(PlannedCell.Cell);
				}

				for (const FLayoutPlannedCell& PlannedCell : PreparedSolve.Request.PlannedCells)
				{
					if (PlannedCell.Cell.Z <= 0)
					{
						continue;
					}

					const FIntVector SupportingCell = PlannedCell.Cell + FIntVector(0, 0, -1);
					if (PlannedCellSet.Contains(SupportingCell))
					{
						continue;
					}

					const FLayoutSolveBoundaryPoint* SupportBoundaryPoint =
						FindIncomingBoundaryPointForCellFace(
							PlannedCell.Cell,
							ELayoutFaceDirection::NegZ);
					if (SupportBoundaryPoint == nullptr
						|| !SupportBoundaryPoint->ConnectedTraversalChannels.IsEmpty())
					{
						continue;
					}

					const FString FailureReason = FString::Printf(
						TEXT("Scheduler traversal feasibility rejected the parent plan before final validation.\nProblem: Upper-level parent cell %s is being preserved only through child support at %s, but that child support exports no traversal channels while parent-owned vertical continuation still has to route nearby.\nFix: Move or rotate the child so the preserved upper-level continuation stays on parent-owned support, or use a child placement whose surviving support face exports a matching traversal channel."),
						*PlannedCell.Cell.ToString(),
						*SupportingCell.ToString());
					FPlacementBridgeStructuralFeasibilityMemoEntry& StructuralMemo =
						InOutStructuralFeasibilityMemo.Add(PreparedSolve.StructuralMemoKey);
					StructuralMemo.bSucceeded = false;
					StructuralMemo.FailureReason = FailureReason;
					InOutResult.bFailedStructuralFeasibility = true;
					InOutResult.FailureReason = FailureReason;
					return false;
				}
			}

			const bool bStructuralFeasibilitySucceeded = DoesParentPlanKeepAnchorCellsConnected(
				PreparedSolve.Request.PlannedCells,
				MutableInputs.CurrentPlacements,
				Candidate.CandidateParentContactCells,
				PreparedSolve.VerticalAccessOwnership,
				&Candidate.CandidateParentTraversalAnchors,
				Candidate.bAllowsChildTraversalBridgeForCommittedContacts,
				&AnchorConnectivityFailureReason);
			FPlacementBridgeStructuralFeasibilityMemoEntry& StructuralMemo =
				InOutStructuralFeasibilityMemo.Add(PreparedSolve.StructuralMemoKey);
			StructuralMemo.bSucceeded = bStructuralFeasibilitySucceeded;
			StructuralMemo.FailureReason = AnchorConnectivityFailureReason;
			if (!bStructuralFeasibilitySucceeded)
			{
				InOutResult.bFailedStructuralFeasibility = true;
				InOutResult.FailureReason = AnchorConnectivityFailureReason;
				return false;
			}

			return true;
		}

		void AppendPreparedParentProbeSolveInputs(
			const FResolvedParentProbeMutableInputs& MutableInputs,
			const FPlacementBridgeParentProbeCandidate& Candidate,
			const FPlacementBridgeParentProbeResult& ProbeResult,
			FLayoutRegionSolveRequest& InOutRequest)
		{
			for (const FAutomaticChildPlacement& ExistingPlacement : MutableInputs.CurrentPlacements)
			{
				InOutRequest.IncomingBoundaryPoints.Append(ExistingPlacement.ParentTranslatedBoundaryPoints);
				InOutRequest.FutureTerraceProofBoundaryPoints.Append(
					ExistingPlacement.ParentTranslatedFutureTerraceBoundaryPoints);
				// Child Entry stays child-owned; boundary and traversal carriers
				// constrain parent proof without widening its endpoint roles.
				InOutRequest.CommittedTraversalAnchors.Append(ExistingPlacement.ParentCommittedTraversalAnchors);
			}

			InOutRequest.IncomingBoundaryPoints.Append(ProbeResult.ParentTranslatedBoundaryPoints);
			InOutRequest.FutureTerraceProofBoundaryPoints.Append(
				ProbeResult.ParentTranslatedFutureTerraceBoundaryPoints);
			InOutRequest.CommittedTraversalAnchors.Append(ProbeResult.ParentCommittedTraversalAnchors);
		}

		int32 ScoreParentProbeRouteConstraints(const TArray<FLayoutRouteConstraintRecord>& RouteConstraints)
		{
			int32 BoundaryRouteConstraintCount = 0;
			int32 BoundaryMultiFaceConstraintCount = 0;
			int32 InteriorRouteConstraintCount = 0;
			for (const FLayoutRouteConstraintRecord& Constraint : RouteConstraints)
			{
				if (Constraint.Intent == ELayoutCellIntent::Boundary)
				{
					++BoundaryRouteConstraintCount;
					if (Constraint.FaceRequirements.Num() > 1)
					{
						++BoundaryMultiFaceConstraintCount;
					}
				}
				else if (Constraint.Intent == ELayoutCellIntent::Interior || Constraint.Intent == ELayoutCellIntent::Core)
				{
					++InteriorRouteConstraintCount;
				}
			}

			return (InteriorRouteConstraintCount * 3)
				- (BoundaryRouteConstraintCount * 200)
				- (BoundaryMultiFaceConstraintCount * 500)
				- RouteConstraints.Num();
		}
	}

	bool DoesParentPlanKeepAnchorCellsConnected(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TArray<FAutomaticChildPlacement>& ExistingPlacements,
		const TArray<FIntVector>& CandidateParentContactCells,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FLayoutCommittedTraversalAnchor>* CandidateParentTraversalAnchors,
		const bool bAllowsChildTraversalBridgeForCommittedContacts,
		FString* OutFailureReason)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		auto IsIntentStructurallyTraversable = [](const ELayoutCellIntent Intent) -> bool
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

		TSet<FIntVector> AvailablePlanCells;
		TSet<FIntVector> TraversablePlanCells;
		AvailablePlanCells.Reserve(ParentPlannedCells.Num());
		TraversablePlanCells.Reserve(ParentPlannedCells.Num());
		TArray<FIntVector> RootEntryCells;
		TArray<FIntVector> RequiredContactCells;
		TArray<FIntVector> RequiredTraversalAnchorCells;
		TArray<FIntVector> CountedVerticalAccessCells;
		for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
		{
			AvailablePlanCells.Add(PlannedCell.Cell);
			if (IsIntentStructurallyTraversable(PlannedCell.Intent))
			{
				TraversablePlanCells.Add(PlannedCell.Cell);
			}

			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				RootEntryCells.AddUnique(PlannedCell.Cell);
			}
			else if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
				&& VerticalAccessOwnership.CountedParentVerticalAccessCells.Contains(PlannedCell.Cell))
			{
				CountedVerticalAccessCells.AddUnique(PlannedCell.Cell);
			}
		}

		for (const FAutomaticChildPlacement& ExistingPlacement : ExistingPlacements)
		{
			if (ExistingPlacement.ParentCommittedTraversalAnchors.IsEmpty()
				&& !ExistingPlacement.bAllowsChildTraversalBridgeForCommittedContacts)
			{
				for (const FIntVector& ParentContactCell : ExistingPlacement.ParentCommittedEntryCells)
				{
					if (AvailablePlanCells.Contains(ParentContactCell))
					{
						RequiredContactCells.AddUnique(ParentContactCell);
						TraversablePlanCells.Add(ParentContactCell);
					}
				}
			}

			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : ExistingPlacement.ParentCommittedTraversalAnchors)
			{
				if (AvailablePlanCells.Contains(TraversalAnchor.Cell))
				{
					RequiredTraversalAnchorCells.AddUnique(TraversalAnchor.Cell);
					TraversablePlanCells.Add(TraversalAnchor.Cell);
				}
			}
		}

		if ((CandidateParentTraversalAnchors == nullptr || CandidateParentTraversalAnchors->IsEmpty())
			&& !bAllowsChildTraversalBridgeForCommittedContacts)
		{
			for (const FIntVector& ParentContactCell : CandidateParentContactCells)
			{
				if (AvailablePlanCells.Contains(ParentContactCell))
				{
					RequiredContactCells.AddUnique(ParentContactCell);
					TraversablePlanCells.Add(ParentContactCell);
				}
			}
		}

		if (CandidateParentTraversalAnchors != nullptr)
		{
			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : *CandidateParentTraversalAnchors)
			{
				if (AvailablePlanCells.Contains(TraversalAnchor.Cell))
				{
					RequiredTraversalAnchorCells.AddUnique(TraversalAnchor.Cell);
					TraversablePlanCells.Add(TraversalAnchor.Cell);
				}
			}
		}

		if (RootEntryCells.IsEmpty())
		{
			return true;
		}

		TSet<FIntVector> VisitedCells;
		TArray<FIntVector> Frontier;
		for (const FIntVector& RootEntryCell : RootEntryCells)
		{
			if (!TraversablePlanCells.Contains(RootEntryCell) || VisitedCells.Contains(RootEntryCell))
			{
				continue;
			}

			Frontier.Add(RootEntryCell);
			VisitedCells.Add(RootEntryCell);
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
			for (const ELayoutFaceDirection Direction : ConnectivityDirections)
			{
				const FIntVector NeighborCell = CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (!TraversablePlanCells.Contains(NeighborCell) || VisitedCells.Contains(NeighborCell))
				{
					continue;
				}

				VisitedCells.Add(NeighborCell);
				Frontier.Add(NeighborCell);
			}
		}

		TArray<FString> UnreachableContactCells;
		for (const FIntVector& ContactCell : RequiredContactCells)
		{
			if (!VisitedCells.Contains(ContactCell))
			{
				UnreachableContactCells.Add(ContactCell.ToString());
			}
		}

		TArray<FString> UnreachableVerticalAccessCells;
		for (const FIntVector& StairCell : CountedVerticalAccessCells)
		{
			if (!VisitedCells.Contains(StairCell))
			{
				UnreachableVerticalAccessCells.Add(StairCell.ToString());
			}
		}

		TArray<FString> UnreachableTraversalAnchorCells;
		for (const FIntVector& TraversalAnchorCell : RequiredTraversalAnchorCells)
		{
			if (!VisitedCells.Contains(TraversalAnchorCell))
			{
				UnreachableTraversalAnchorCells.Add(TraversalAnchorCell.ToString());
			}
		}

		const int32 ReachableVerticalAccessCellCount =
			CountedVerticalAccessCells.Num() - UnreachableVerticalAccessCells.Num();
		if (UnreachableContactCells.IsEmpty()
			&& UnreachableTraversalAnchorCells.IsEmpty()
			&& (VerticalAccessOwnership.CountedParentProviderCount <= 0
				|| ReachableVerticalAccessCellCount >= VerticalAccessOwnership.CountedParentProviderCount))
		{
			return true;
		}

		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = FString::Printf(
				TEXT("Scheduler traversal feasibility rejected the parent plan before final validation.\nProblem: After applying child reservations, the scheduler-local parent route graph could not preserve a traversable path from the root entry to all required child contacts, committed traversal anchors, and enough surviving parent vertical-access cells.\nRoot entries: %s\nUnreachable child contacts: %s\nUnreachable traversal anchors: %s\nReachable parent vertical-access cells: %d / required %d\nUnreachable vertical access cells: %s\nFix: move blocking child regions or walls, reserve a wider parent corridor, or choose child entry/traversal contacts that land on the surviving traversable parent plan."),
				RootEntryCells.IsEmpty() ? TEXT("<none>") : *FString::JoinBy(RootEntryCells, TEXT(", "), [](const FIntVector& Cell){ return Cell.ToString(); }),
				UnreachableContactCells.IsEmpty() ? TEXT("<none>") : *FString::Join(UnreachableContactCells, TEXT(", ")),
				UnreachableTraversalAnchorCells.IsEmpty() ? TEXT("<none>") : *FString::Join(UnreachableTraversalAnchorCells, TEXT(", ")),
				ReachableVerticalAccessCellCount,
				VerticalAccessOwnership.CountedParentProviderCount,
				UnreachableVerticalAccessCells.IsEmpty() ? TEXT("<none>") : *FString::Join(UnreachableVerticalAccessCells, TEXT(", ")));
		}
		return false;
	}

	FString BuildParentProbeEvaluationMemoKey(const FPlacementBridgeParentProbeCandidate& Candidate)
	{
		SCOPED_NAMED_EVENT(Layout_ParentProbe_EvaluationKey, FColor::Orange);
		TArray<FString> ReservedCellParts;
		ReservedCellParts.Reserve(Candidate.CandidateParentPlanReservedCells.Num());
		for (const FIntVector& ReservedCell : Candidate.CandidateParentPlanReservedCells)
		{
			ReservedCellParts.Add(ReservedCell.ToString());
		}

		return FString::Printf(
			TEXT("%s|Variant=%d|Offset=%s|Commitments=%s|Contacts=%s|TraversalAnchors=%s|BridgeContacts=%d|Reserved=%s|SharedFaces=%s|BoundaryCert=%s|Defer=%d|StructuralPrecheck=%d|ImmediateFullParentProof=%d|DeferFullParentProof=%d"),
			*Candidate.SchedulerStateKey,
			Candidate.VariantIndex,
			*Candidate.CandidateOffset.ToString(),
			*ParentProbeBuildCommittedEndpointAnchorsKey(Candidate.CandidateCommitments),
			*ParentProbeBuildParentContactCellsKey(Candidate.CandidateParentContactCells),
			*ParentProbeBuildCommittedTraversalAnchorsKey(Candidate.CandidateParentTraversalAnchors),
			Candidate.bAllowsChildTraversalBridgeForCommittedContacts ? 1 : 0,
			*BuildParentProbeStableDigest(ReservedCellParts),
			*ParentProbeBuildSharedParentChildFacesKey(Candidate.CandidateSharedParentChildFaces),
			*Candidate.CandidateBoundaryCertificateId.ToString(),
			Candidate.bDeferChildTraversalValidationToSchedule ? 1 : 0,
			Candidate.bRequireStructuralFeasibilityPrecheck ? 1 : 0,
			Candidate.bRunImmediateFullParentProof ? 1 : 0,
			Candidate.bDeferFullParentProofToCompleteValidation ? 1 : 0);
	}

	FString BuildCanonicalParentProbeSolveRequestMemoKey(const FLayoutRegionSolveRequest& ParentProbeRequest)
	{
		SCOPED_NAMED_EVENT(Layout_ParentProbe_CanonicalSolveKey, FColor::Orange);
		TArray<FString> ProtectedCellParts;
		ProtectedCellParts.Reserve(ParentProbeRequest.ProtectedStructuralCells.Num());
		for (const FIntVector& ProtectedCell : ParentProbeRequest.ProtectedStructuralCells)
		{
			ProtectedCellParts.Add(ProtectedCell.ToString());
		}
		TArray<FString> ExternalFaceParts;
		for (const TPair<FIntVector, uint8>& Pair : ParentProbeRequest.ExternalPlannedNeighborFaceMasks)
		{
			ExternalFaceParts.Add(FString::Printf(TEXT("%s:%u"), *Pair.Key.ToString(), Pair.Value));
		}

		return FString::Printf(
			TEXT("Region=%s|Footprint=%s|Planned=%s|Incoming=%s|CommittedEndpoints=%s|CommittedTraversal=%s|Protected=%s|External=%s|DomainCert=%s|DeferClosure=%d|DeferTraversal=%d"),
			*ParentProbeRequest.RegionDebugPath,
			*ParentProbeRequest.FootprintSize.ToString(),
			*ParentProbeBuildPlannedCellsKey(ParentProbeRequest.PlannedCells),
			*ParentProbeBuildBoundaryPointsKey(ParentProbeRequest.IncomingBoundaryPoints),
			*ParentProbeBuildCommittedEndpointAnchorsKey(ParentProbeRequest.CommittedEndpointAnchors),
			*ParentProbeBuildCommittedTraversalAnchorsKey(ParentProbeRequest.CommittedTraversalAnchors),
			*BuildParentProbeStableDigest(ProtectedCellParts),
			*BuildParentProbeStableDigest(ExternalFaceParts),
			*ParentProbeRequest.CandidateDomainCertificateId.ToString(),
			ParentProbeRequest.bDeferClosureValidationToSchedule ? 1 : 0,
			ParentProbeRequest.bDeferTraversalValidationToSchedule ? 1 : 0);
	}

	int32 BuildCanonicalParentProbeSolveSeed(const FString& ParentProbeSolveMemoKey)
	{
		return static_cast<int32>(FCrc::StrCrc32(*ParentProbeSolveMemoKey));
	}

	bool TryPrepareParentProbeCandidate(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPlacementBridgeParentProbeCandidate& Candidate,
		FPreparedParentProbeCandidate& OutPreparedCandidate,
		FPlacementBridgeParentProbeResult& OutResult,
		const FPreparedParentProbeResidualPlan* PreparedResidualPlan)
	{
		SCOPED_NAMED_EVENT(Layout_ParentProbe_PrepareCandidate, FColor::Orange);
		check(SolveContext.RootRequest != nullptr);
		check(SolveContext.ParentPlannedCells != nullptr);
		check(SolveContext.ParentPlannedCellSet != nullptr);
		check(SolveContext.ParentPlannedCellIntents != nullptr);

		const FResolvedParentProbeMutableInputs MutableInputs =
			ResolveParentProbeMutableInputs(SolveContext, MutableState);

		OutPreparedCandidate = FPreparedParentProbeCandidate();
		OutPreparedCandidate.EvaluationMemoKey = BuildParentProbeEvaluationMemoKey(Candidate);
		OutPreparedCandidate.Candidate = Candidate;
		OutPreparedCandidate.ParentCommittedTraversalAnchors = Candidate.CandidateParentTraversalAnchors;
		OutPreparedCandidate.ParentTranslatedBoundaryPoints = ParentProbeBuildSyntheticExportedBoundaryPointsFromChildSummary(
			Candidate.PlanningVariant.ChildRegionDebugPath,
			Candidate.CandidateOffset,
			*SolveContext.ParentPlannedCellSet,
			Candidate.PlanningVariant.InterfaceSummary,
			Candidate.CandidateCommitments,
			Candidate.CandidateSharedParentChildFaces,
			Candidate.CandidateParentPlanReservedCells,
			Candidate.PlanningVariant.bChildContributesHostVerticalAccess,
			Candidate.PlanningVariant.MaxLocalLevel);
		TSet<FIntVector> SupportReservedParentCells =
			MutableInputs.CurrentReservedParentCells;
		for (const FIntVector& ReservedCell : Candidate.CandidateParentPlanReservedCells)
		{
			SupportReservedParentCells.Add(ReservedCell);
		}
		for (FLayoutSolveBoundaryPoint& BoundaryPoint :
			OutPreparedCandidate.ParentTranslatedBoundaryPoints)
		{
			const bool bSharedParentChildFace =
				Candidate.CandidateSharedParentChildFaces.ContainsByPredicate(
					[&](const FSharedParentChildFace& SharedFace)
					{
						return ParentProbeTranslateCell(
								   SharedFace.ChildLocalCell,
								   Candidate.CandidateOffset)
								== BoundaryPoint.LocalCell
							&& SharedFace.FaceDirection == BoundaryPoint.FaceDirection;
					});
			if (bSharedParentChildFace)
			{
				continue;
			}

			BoundaryPoint.bRequiresBoundaryFacing = false;
			if (Candidate.bDeferChildTraversalValidationToSchedule)
			{
				BoundaryPoint.CommitmentId = NAME_None;
			}
		}
		AppendSyntheticParentSupportBoundaryPoints(
			Candidate.PlanningVariant.StructuralSupportLocalCells,
			Candidate.PlanningVariant.PlannedCells,
			Candidate.CandidateOffset,
			Candidate.PlanningVariant.ChildRegionDebugPath,
			*SolveContext.ParentPlannedCellSet,
			*SolveContext.ParentPlannedCellIntents,
			SolveContext.RootRequest->ProfileSnapshot,
			*SolveContext.ParentPlannedCells,
			SupportReservedParentCells,
			Candidate.CandidateSharedParentChildFaces,
			Candidate.CandidateBoundaryCertificateId,
			Candidate.CandidateParentDomainRestrictions,
			OutPreparedCandidate.ParentTranslatedBoundaryPoints);
		OutPreparedCandidate.ParentTranslatedFutureTerraceBoundaryPoints.Reset();
		InitializePreparedParentProbeResult(OutPreparedCandidate, OutResult);

		FPreparedParentProbeSolve PreparedSolve;
		FString ParentPlanCompatibilityFailureReason;
		if (!TryPrepareParentProbeSolve(
			SolveContext,
			MutableInputs,
			Candidate,
			PreparedResidualPlan,
			PreparedSolve,
			ParentPlanCompatibilityFailureReason))
		{
			OutResult.FailureReason = ParentPlanCompatibilityFailureReason;
			return false;
		}

		OutPreparedCandidate.VerticalAccessOwnership = PreparedSolve.VerticalAccessOwnership;
		OutPreparedCandidate.PreparedProofRequest = MoveTemp(PreparedSolve.Request);
		OutPreparedCandidate.StructuralMemoKey = ParentProbeBuildParentStructuralFeasibilityMemoKey(
			OutPreparedCandidate.PreparedProofRequest.PlannedCells,
			OutPreparedCandidate.ParentTranslatedBoundaryPoints,
			Candidate.CandidateParentContactCells,
			Candidate.CandidateParentTraversalAnchors,
			Candidate.bAllowsChildTraversalBridgeForCommittedContacts,
			OutPreparedCandidate.VerticalAccessOwnership);
		return true;
	}

		bool EvaluatePreparedParentProbeStructuralFeasibility(
			const FPlacementBridgeSolveContext& SolveContext,
			const FPlacementBridgeMutableStateView& MutableState,
			const FPreparedParentProbeCandidate& PreparedCandidate,
			TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
		FPlacementBridgeParentProbeResult& InOutResult)
	{
		SCOPED_NAMED_EVENT(Layout_ParentProbe_StructuralFeasibility, FColor::Orange);
		const FResolvedParentProbeMutableInputs MutableInputs =
			ResolveParentProbeMutableInputs(SolveContext, MutableState);
		InitializePreparedParentProbeResult(PreparedCandidate, InOutResult);

		FPreparedParentProbeSolve PreparedSolve;
		PreparedSolve.VerticalAccessOwnership = PreparedCandidate.VerticalAccessOwnership;
			PreparedSolve.Request = PreparedCandidate.PreparedProofRequest;
			PreparedSolve.StructuralMemoKey = PreparedCandidate.StructuralMemoKey;

			const double StructuralFeasibilityStartSeconds = FPlatformTime::Seconds();
			const bool bSucceeded = EvaluatePreparedParentProbeStructuralFeasibilityImpl(
				MutableInputs,
				PreparedCandidate,
				PreparedSolve,
				InOutStructuralFeasibilityMemo,
				InOutResult);
		InOutResult.StructuralFeasibilitySeconds =
			FPlatformTime::Seconds() - StructuralFeasibilityStartSeconds;
		return bSucceeded;
	}

	void BuildPreparedParentProofRequest(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPreparedParentProbeCandidate& PreparedCandidate,
		const FPlacementBridgeParentProbeResult& ProbeResult,
		FLayoutRegionSolveRequest& OutRequest)
	{
		const FResolvedParentProbeMutableInputs MutableInputs =
			ResolveParentProbeMutableInputs(SolveContext, MutableState);
		OutRequest = PreparedCandidate.PreparedProofRequest;
		AppendPreparedParentProbeSolveInputs(
			MutableInputs,
			PreparedCandidate.Candidate,
			ProbeResult,
			OutRequest);
	}

	bool RunPreparedParentProof(
		const FPlacementBridgeSolveContext& SolveContext,
		const FLayoutRegionSolveRequest& PreparedRequest,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>* SharedSolveMemo,
		FPlacementBridgeParentProbeResult& InOutResult)
	{
		SCOPED_NAMED_EVENT(Layout_ParentProbe_FullProof, FColor::Orange);
		FLayoutRegionSolveRequest ParentProofRequest = PreparedRequest;
		InOutResult.bRanImmediateFullParentProof = true;
		const FString SharedSolveMemoKey = BuildCanonicalParentProbeSolveRequestMemoKey(ParentProofRequest);
		if (SharedSolveMemo != nullptr)
		{
			ParentProofRequest.Seed = BuildCanonicalParentProbeSolveSeed(SharedSolveMemoKey);
			if (const FPlacementBridgeParentProbeMemoEntry* ExistingSharedSolve = SharedSolveMemo->Find(SharedSolveMemoKey))
			{
				InOutResult = ExistingSharedSolve->Result;
				InOutResult.bUsedSharedSolveMemo = true;
				InOutResult.bExecutedImmediateFullParentProofSolve = false;
				InOutResult.ParentProofSolveSeconds = 0.0;
				return ExistingSharedSolve->bSucceeded;
			}
		}

		const double SolveStartSeconds = FPlatformTime::Seconds();
		InOutResult.bExecutedImmediateFullParentProofSolve = true;
		const FLayoutRegionSolveResult ParentProbeResult =
			LayoutProfileSolverInternal::SolveRequestBackedRegion(
				ParentProofRequest);
		InOutResult.ParentProofSolveSeconds = FPlatformTime::Seconds() - SolveStartSeconds;
		if (!ParentProbeResult.SolveResult.bSucceeded)
		{
			InOutResult.FailureReason = ParentProbeResult.SolveResult.FailureReason;
			if (SharedSolveMemo != nullptr)
			{
				StoreParentProbeMemoResult(SharedSolveMemoKey, false, InOutResult, *SharedSolveMemo);
			}
			return false;
		}

		FString ParentProbeCommittedAnchorFailureReason;
		if (!ValidateCommittedEndpointAnchorCoverageOnPlacements(
			SolveContext.RootRequest->RegionDebugPath,
			FIntVector::ZeroValue,
			ParentProbeResult.SolveResult.Placements,
			ParentProofRequest.CommittedEndpointAnchors,
			ParentProbeCommittedAnchorFailureReason))
		{
			InOutResult.FailureReason = ParentProbeCommittedAnchorFailureReason;
			if (SharedSolveMemo != nullptr)
			{
				StoreParentProbeMemoResult(SharedSolveMemoKey, false, InOutResult, *SharedSolveMemo);
			}
			return false;
		}

		InOutResult.bSucceeded = true;
		InOutResult.ScoreAdjustment =
			ScoreParentProbeRouteConstraints(ParentProbeResult.SolveResult.RouteConstraints);
		if (SharedSolveMemo != nullptr)
		{
			StoreParentProbeMemoResult(SharedSolveMemoKey, true, InOutResult, *SharedSolveMemo);
		}
		return true;
	}

	bool EvaluatePreparedParentProbeCandidate(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPreparedParentProbeCandidate& PreparedCandidate,
		TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>& InOutLocalEvaluationMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>* SharedSolveMemo,
		FPlacementBridgeParentProbeResult& OutResult)
	{
		SCOPED_NAMED_EVENT(Layout_ParentProbe_Evaluate, FColor::Orange);
		const FString ParentProbeMemoKey = PreparedCandidate.EvaluationMemoKey;
		if (const FPlacementBridgeParentProbeMemoEntry* ExistingMemo = InOutLocalEvaluationMemo.Find(ParentProbeMemoKey))
		{
			OutResult = ExistingMemo->Result;
			OutResult.bUsedLocalMemo = true;
			OutResult.bExecutedImmediateFullParentProofSolve = false;
			OutResult.StructuralFeasibilitySeconds = 0.0;
			OutResult.ParentProofSolveSeconds = 0.0;
			return ExistingMemo->bSucceeded;
		}

		InitializePreparedParentProbeResult(PreparedCandidate, OutResult);
		const bool bShouldRunImmediateFullProof =
			ShouldRunImmediateParentProof(PreparedCandidate.Candidate);
		const bool bShouldRunStructuralFeasibility =
			PreparedCandidate.Candidate.bRequireStructuralFeasibilityPrecheck
			|| bShouldRunImmediateFullProof;
		if (!bShouldRunStructuralFeasibility)
		{
			OutResult.bSucceeded = true;
			StoreParentProbeMemoResult(ParentProbeMemoKey, true, OutResult, InOutLocalEvaluationMemo);
			return true;
		}

		if (!EvaluatePreparedParentProbeStructuralFeasibility(
			SolveContext,
			MutableState,
			PreparedCandidate,
			InOutStructuralFeasibilityMemo,
			OutResult))
		{
			StoreParentProbeMemoResult(ParentProbeMemoKey, false, OutResult, InOutLocalEvaluationMemo);
			return false;
		}

		if (!bShouldRunImmediateFullProof)
		{
			OutResult.bSucceeded = true;
			StoreParentProbeMemoResult(ParentProbeMemoKey, true, OutResult, InOutLocalEvaluationMemo);
			return true;
		}

		if (!CandidateAddsExpensiveParentProofBurden(PreparedCandidate.Candidate, OutResult))
		{
			OutResult.bSucceeded = true;
			StoreParentProbeMemoResult(ParentProbeMemoKey, true, OutResult, InOutLocalEvaluationMemo);
			return true;
		}

		FLayoutRegionSolveRequest ParentProofRequest;
		BuildPreparedParentProofRequest(
			SolveContext,
			MutableState,
			PreparedCandidate,
			OutResult,
			ParentProofRequest);
		const bool bProofSucceeded = RunPreparedParentProof(
			SolveContext,
			ParentProofRequest,
			SharedSolveMemo,
			OutResult);
		StoreParentProbeMemoResult(ParentProbeMemoKey, bProofSucceeded, OutResult, InOutLocalEvaluationMemo);
		return bProofSucceeded;
	}

	bool EvaluateParentProbe(
		const FPlacementBridgeSolveContext& SolveContext,
		const FPlacementBridgeMutableStateView& MutableState,
		const FPlacementBridgeParentProbeCandidate& Candidate,
		TMap<FString, FPlacementBridgeStructuralFeasibilityMemoEntry>& InOutStructuralFeasibilityMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>& InOutLocalEvaluationMemo,
		TMap<FString, FPlacementBridgeParentProbeMemoEntry>* SharedSolveMemo,
		FPlacementBridgeParentProbeResult& OutResult)
	{
		const FString ParentProbeMemoKey = BuildParentProbeEvaluationMemoKey(Candidate);
		if (const FPlacementBridgeParentProbeMemoEntry* ExistingMemo = InOutLocalEvaluationMemo.Find(ParentProbeMemoKey))
		{
			OutResult = ExistingMemo->Result;
			OutResult.bUsedLocalMemo = true;
			OutResult.bExecutedImmediateFullParentProofSolve = false;
			OutResult.StructuralFeasibilitySeconds = 0.0;
			OutResult.ParentProofSolveSeconds = 0.0;
			return ExistingMemo->bSucceeded;
		}

		FPreparedParentProbeCandidate PreparedCandidate;
		if (!TryPrepareParentProbeCandidate(
			SolveContext,
			MutableState,
			Candidate,
			PreparedCandidate,
			OutResult))
		{
			StoreParentProbeMemoResult(ParentProbeMemoKey, false, OutResult, InOutLocalEvaluationMemo);
			return false;
		}

		PreparedCandidate.EvaluationMemoKey = ParentProbeMemoKey;
		return EvaluatePreparedParentProbeCandidate(
			SolveContext,
			MutableState,
			PreparedCandidate,
			InOutStructuralFeasibilityMemo,
			InOutLocalEvaluationMemo,
			SharedSolveMemo,
			OutResult);
	}
}
