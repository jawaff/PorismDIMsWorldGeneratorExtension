// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Algo/Unique.h"

#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/Crc.h"

/**
 * Private rewrite home for:
 * - `FLayoutResponsibilityNegotiator`
 * - `FLayoutResponsibilityConfirmation`
 *
 * This file is where the rewrite-owned parent/child responsibility negotiation
 * and bounded confirmation stages live, separate from the higher-level
 * coordinator while the broader recursive scheduler migration remains
 * incremental.
 */

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
		static bool DoesChildRequestCarryValidationAssertion(
			const FLayoutRegionSolveRequest& ChildRequest,
			const FLayoutId AssertionId)
		{
			return ChildRequest.ValidationAssertions.ContainsByPredicate(
				[AssertionId](const FLayoutValidationAssertionRecord& Assertion)
				{
					return Assertion.AssertionId == AssertionId;
				});
		}

		static bool LexicalLess(const FIntVector& Left, const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		}

		static bool AreCommittedEndpointAnchorsEquivalent(
			const FLayoutCommittedEndpointAnchor& Left,
			const FLayoutCommittedEndpointAnchor& Right)
		{
			return Left.CommitmentId == Right.CommitmentId
				&& Left.LocalCell == Right.LocalCell
				&& Left.FaceDirection == Right.FaceDirection
				&& Left.RequiredWorldCenterBlockZ == Right.RequiredWorldCenterBlockZ
				&& Left.ConnectionTag == Right.ConnectionTag;
		}

		static bool AreCommittedTraversalAnchorsEquivalent(
			const FLayoutCommittedTraversalAnchor& Left,
			const FLayoutCommittedTraversalAnchor& Right)
		{
			return Left.Cell == Right.Cell && Left.TraversalChannel == Right.TraversalChannel;
		}

		static bool AreParentContactFacesEquivalent(
			const FCommittedParentContactFace& Left,
			const FCommittedParentContactFace& Right)
		{
			return Left.Cell == Right.Cell
				&& Left.FaceDirection == Right.FaceDirection;
		}

		static bool DoesEndpointOfferMatchCommitment(
			const FLayoutChildCapabilityEndpoint& EndpointOffer,
			const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return EndpointOffer.LocalCell == EndpointCommitment.LocalCell
				&& EndpointOffer.FaceDirection == EndpointCommitment.FaceDirection
				&& EndpointOffer.ConnectionTag == EndpointCommitment.ConnectionTag;
		}

		static FLayoutId BuildEndpointCommitmentId(const FLayoutChildCapabilityEndpoint& EndpointOffer)
		{
			if (!EndpointOffer.CapabilityId.IsNone())
			{
				return EndpointOffer.CapabilityId;
			}

			return *FString::Printf(
				TEXT("Endpoint_Z%d_Y%d_X%d_%d"),
				EndpointOffer.LocalCell.Z,
				EndpointOffer.LocalCell.Y,
				EndpointOffer.LocalCell.X,
				static_cast<int32>(EndpointOffer.FaceDirection));
		}

		static uint32 HashPlacementBundle(const FPlacementCapabilityBundle& PlacementBundle)
		{
			uint32 BundleHash = 0;
			for (const FIntVector& Cell : PlacementBundle.OccupiedLocalCells)
			{
				BundleHash = HashCombine(BundleHash, GetTypeHash(Cell));
			}
			for (const int32 LevelIndex : PlacementBundle.CoveredLevels)
			{
				BundleHash = HashCombine(BundleHash, GetTypeHash(LevelIndex));
			}
			for (const FIntVector& Cell : PlacementBundle.RequiredSupportCells)
			{
				BundleHash = HashCombine(BundleHash, GetTypeHash(Cell));
			}
			for (const FLayoutDerivedInternalTraversalLink& Link : PlacementBundle.DerivedInternalTraversalLinks)
			{
				BundleHash = HashCombine(BundleHash, GetTypeHash(Link.FromLocalCell));
				BundleHash = HashCombine(BundleHash, GetTypeHash(Link.FromTraversalChannel));
				BundleHash = HashCombine(BundleHash, GetTypeHash(Link.ToLocalCell));
				BundleHash = HashCombine(BundleHash, GetTypeHash(Link.ToTraversalChannel));
				BundleHash = HashCombine(BundleHash, GetTypeHash(Link.bBidirectional));
			}
			return BundleHash;
		}

		static FLayoutId BuildContactFamilyId(
			const FLayoutChildCapabilityEndpoint& EndpointOffer,
			const FIntVector& ParentContactCell,
			const int32 LevelIndex,
			const int32 ParentComponentId,
			const FLayoutId AdjacencyClassId,
			const FPlacementCapabilityBundle& PlacementBundle)
		{
			return *FString::Printf(
				TEXT("ContactFamily_%s_L%d_C%d_Z%d_Y%d_X%d_A%s_B%u"),
				EndpointOffer.CapabilityId.IsNone() ? TEXT("Anonymous") : *EndpointOffer.CapabilityId.ToString(),
				LevelIndex,
				ParentComponentId,
				ParentContactCell.Z,
				ParentContactCell.Y,
				ParentContactCell.X,
				AdjacencyClassId.IsNone() ? TEXT("None") : *AdjacencyClassId.ToString(),
				HashPlacementBundle(PlacementBundle));
		}

		static FLayoutId ResolveAdjacencyClassId(
			const FResidualParentCapabilitySummary& ParentSummary,
			const FIntVector& ParentContactCell)
		{
			const FResidualBoundarySpanClassification* MatchingBoundaryClass =
				ParentSummary.BoundarySpanClassifications.FindByPredicate(
					[ParentContactCell](const FResidualBoundarySpanClassification& Classification)
					{
						return Classification.LevelIndex == ParentContactCell.Z
							&& Classification.Cells.Contains(ParentContactCell)
							&& !Classification.AdjacencyClassId.IsNone();
					});
			if (MatchingBoundaryClass != nullptr)
			{
				return MatchingBoundaryClass->AdjacencyClassId;
			}

			if (ParentSummary.SeamRelevantBoundaryCells.Contains(ParentContactCell))
			{
				return TEXT("Boundary");
			}

			const FLayoutPlannedCell* PlannedCell = ParentSummary.PlannedCells.FindByPredicate(
				[ParentContactCell](const FLayoutPlannedCell& Cell)
				{
					return Cell.Cell == ParentContactCell;
				});
			if (PlannedCell != nullptr
				&& (PlannedCell->Intent == ELayoutCellIntent::Boundary
					|| PlannedCell->Intent == ELayoutCellIntent::Entry))
			{
				return TEXT("Boundary");
			}

			return TEXT("Interior");
		}

		static const TArray<FIntVector>& GetRetainedParentRouteSupportCells(
			const FResidualParentCapabilitySummary& ParentSummary)
		{
			return ParentSummary.RouteSupportVerticalAccessCells.IsEmpty()
				? ParentSummary.CountedParentVerticalAccessCells
				: ParentSummary.RouteSupportVerticalAccessCells;
		}

		static int32 ResolveParentSupportScore(
			const FResidualParentCapabilitySummary& ParentSummary,
			const FIntVector& ParentContactCell)
		{
			int32 SupportScore = 0;
			if (const TArray<ELayoutFaceDirection>* RootConnectedFaces =
				ParentSummary.RootConnectedTraversableFacesByCell.Find(ParentContactCell))
			{
				SupportScore += RootConnectedFaces->Num();
			}

			if (GetRetainedParentRouteSupportCells(ParentSummary).Contains(ParentContactCell))
			{
				SupportScore += 4;
			}

			if (ParentSummary.RootConnectedTraversableCells.Contains(ParentContactCell))
			{
				++SupportScore;
			}

			return SupportScore;
		}

		static int32 GetMaximumResolvedHostVerticalAccessCount(
			const FLayoutProfileSolveSnapshot& ProfileSnapshot)
		{
			switch (ProfileSnapshot.VerticalAccessCountMode)
			{
			case ELayoutCountConstraintMode::Exact:
				return ProfileSnapshot.VerticalAccessCount;
			case ELayoutCountConstraintMode::Range:
				return ProfileSnapshot.MaxVerticalAccessCount;
			case ELayoutCountConstraintMode::None:
			default:
				return 0;
			}
		}

		static void CapFallbackCountedParentProvidersToAuthoredHostCount(
			const FLayoutRegionSolveRequest* RootRequest,
			const FResidualParentCapabilitySummary& ParentSummary,
			TArray<FIntVector>& InOutCountedParentProviders)
		{
			if (RootRequest == nullptr
				|| !ParentSummary.CountedParentVerticalAccessCells.IsEmpty())
			{
				return;
			}

			const int32 MaxResolvedHostProviderCount =
				GetMaximumResolvedHostVerticalAccessCount(
					RootRequest->ProfileSnapshot);
			if (MaxResolvedHostProviderCount > 0
				&& InOutCountedParentProviders.Num()
					> MaxResolvedHostProviderCount)
			{
				InOutCountedParentProviders.SetNum(
					MaxResolvedHostProviderCount,
					EAllowShrinking::No);
			}
		}

		static TArray<FLayoutCommittedTraversalAnchor> BuildCommittedTraversalAnchors(
			const FLayoutChildCapabilityEndpoint& EndpointOffer,
			const FIntVector& ParentContactCell)
		{
			TArray<FLayoutCommittedTraversalAnchor> TraversalAnchors;
			for (const FGameplayTag& TraversalChannel : EndpointOffer.TraversalChannels)
			{
				if (!TraversalChannel.IsValid())
				{
					continue;
				}

				FLayoutCommittedTraversalAnchor& TraversalAnchor = TraversalAnchors.AddDefaulted_GetRef();
				TraversalAnchor.Cell = ParentContactCell;
				TraversalAnchor.TraversalChannel = TraversalChannel;
			}
			return TraversalAnchors;
		}

		static FCommittedParentContactFace BuildCommittedParentContactFace(
			const FIntVector& ParentContactCell,
			const ELayoutFaceDirection ParentFaceDirection)
		{
			FCommittedParentContactFace ParentContactFace;
			ParentContactFace.Cell = ParentContactCell;
			ParentContactFace.FaceDirection = ParentFaceDirection;
			return ParentContactFace;
		}

		static FLayoutCommittedEndpointAnchor BuildCommittedEndpointAnchor(
			const FLayoutChildCapabilityEndpoint& EndpointOffer)
		{
			FLayoutCommittedEndpointAnchor EndpointCommitment;
			EndpointCommitment.CommitmentId = BuildEndpointCommitmentId(EndpointOffer);
			EndpointCommitment.LocalCell = EndpointOffer.LocalCell;
			EndpointCommitment.FaceDirection = EndpointOffer.FaceDirection;
			EndpointCommitment.ConnectionTag = EndpointOffer.ConnectionTag;
			EndpointCommitment.AllowedConnectionTags = EndpointOffer.AllowedConnectionTags;
			EndpointCommitment.TraversalChannels = EndpointOffer.TraversalChannels;
			return EndpointCommitment;
		}

		static int32 CountPlacementBundleOccupiedCellsAtLevel(
			const FPlacementCapabilityBundle& PlacementBundle,
			const int32 LevelIndex)
		{
			int32 OccupiedCellCount = 0;
			for (const FIntVector& OccupiedLocalCell : PlacementBundle.OccupiedLocalCells)
			{
				if (OccupiedLocalCell.Z == LevelIndex)
				{
					++OccupiedCellCount;
				}
			}
			return OccupiedCellCount;
		}

		static TArray<TPair<int32, FLayoutChildCapabilityEndpoint>> CollectLevelAwareEndpointOffers(
			const FChildCapabilitySummary& ChildSummary)
		{
			TArray<TPair<int32, FLayoutChildCapabilityEndpoint>> EndpointOffersByLevel;
			if (!ChildSummary.EndpointOffersByLevel.IsEmpty())
			{
				for (const FChildLevelEndpointCapabilitySummary& LevelSummary : ChildSummary.EndpointOffersByLevel)
				{
					for (const FLayoutChildCapabilityEndpoint& EndpointOffer : LevelSummary.EndpointOffers)
					{
						EndpointOffersByLevel.Add(TPair<int32, FLayoutChildCapabilityEndpoint>(LevelSummary.LevelIndex, EndpointOffer));
					}
				}
				return EndpointOffersByLevel;
			}

			for (const FLayoutChildCapabilityEndpoint& EndpointOffer : ChildSummary.EndpointOffers)
			{
				EndpointOffersByLevel.Add(TPair<int32, FLayoutChildCapabilityEndpoint>(EndpointOffer.LocalCell.Z, EndpointOffer));
			}
			return EndpointOffersByLevel;
		}

		static TArray<FPlacementCapabilityBundle> FindPlacementBundlesForEndpoint(
			const FChildCapabilitySummary& ChildSummary,
			const FLayoutChildCapabilityEndpoint& EndpointOffer,
			const int32 LevelIndex)
		{
			TArray<FPlacementCapabilityBundle> MatchingBundles;
			for (const FPlacementCapabilityBundle& PlacementBundle : ChildSummary.PlacementBundles)
			{
				if (!PlacementBundle.CoveredLevels.Contains(LevelIndex)
					|| !PlacementBundle.OccupiedLocalCells.Contains(EndpointOffer.LocalCell))
				{
					continue;
				}

				MatchingBundles.Add(PlacementBundle);
			}

			if (MatchingBundles.IsEmpty())
			{
				MatchingBundles.AddDefaulted();
			}
			return MatchingBundles;
		}

		static FLayoutId BuildEndpointGroupId(
			const FLayoutCommittedEndpointAnchor& EndpointCommitment)
		{
			return *FString::Printf(
				TEXT("Endpoint_%s_Z%d_Y%d_X%d_F%d"),
				*EndpointCommitment.CommitmentId.ToString(),
				EndpointCommitment.LocalCell.Z,
				EndpointCommitment.LocalCell.Y,
				EndpointCommitment.LocalCell.X,
				static_cast<int32>(EndpointCommitment.FaceDirection));
		}

		static FLayoutId BuildPlacementFamilyPartitionId(
			const FPlacementCapabilityBundle& PlacementBundle,
			const int32 ParentComponentId,
			const FLayoutId AdjacencyClassId)
		{
			return *FString::Printf(
				TEXT("PlacementPartition_C%d_A%s_B%u"),
				ParentComponentId,
				AdjacencyClassId.IsNone() ? TEXT("None") : *AdjacencyClassId.ToString(),
				HashPlacementBundle(PlacementBundle));
		}

		static FLayoutId BuildSetBackedPlacementFamilyId(
			const FLayoutId PartitionId,
			const TArray<FParentContactCapabilityCandidate>& SelectedCandidates)
		{
			TArray<FString> ContactParts;
			ContactParts.Reserve(SelectedCandidates.Num());
			for (const FParentContactCapabilityCandidate& Candidate : SelectedCandidates)
			{
				ContactParts.Add(FString::Printf(
					TEXT("%s@Z%d_Y%d_X%d"),
					*Candidate.EndpointCommitment.CommitmentId.ToString(),
					Candidate.ParentContactCell.Z,
					Candidate.ParentContactCell.Y,
					Candidate.ParentContactCell.X));
			}
			ContactParts.Sort();
			return *FString::Printf(
				TEXT("%s_%s"),
				*PartitionId.ToString(),
				*FString::Join(ContactParts, TEXT("|")));
		}

		static void CollectCandidateSelectionsRecursive(
			const TArray<TArray<FParentContactCapabilityCandidate>>& CandidateGroups,
			const int32 GroupIndex,
			TArray<FParentContactCapabilityCandidate>& InOutSelection,
			TArray<TArray<FParentContactCapabilityCandidate>>& OutSelections)
		{
			if (GroupIndex >= CandidateGroups.Num())
			{
				OutSelections.Add(InOutSelection);
				return;
			}

			for (const FParentContactCapabilityCandidate& Candidate : CandidateGroups[GroupIndex])
			{
				InOutSelection.Add(Candidate);
				CollectCandidateSelectionsRecursive(
					CandidateGroups,
					GroupIndex + 1,
					InOutSelection,
					OutSelections);
				InOutSelection.Pop(EAllowShrinking::No);
			}
		}

		static void CollectGroupIndexSelectionsRecursive(
			const int32 TotalGroupCount,
			const int32 RequiredSelectionCount,
			const int32 StartIndex,
			TArray<int32>& InOutSelection,
			TArray<TArray<int32>>& OutSelections)
		{
			if (InOutSelection.Num() == RequiredSelectionCount)
			{
				OutSelections.Add(InOutSelection);
				return;
			}

			const int32 RemainingSelectionsNeeded =
				RequiredSelectionCount - InOutSelection.Num();
			for (int32 GroupIndex = StartIndex;
				GroupIndex <= TotalGroupCount - RemainingSelectionsNeeded;
				++GroupIndex)
			{
				InOutSelection.Add(GroupIndex);
				CollectGroupIndexSelectionsRecursive(
					TotalGroupCount,
					RequiredSelectionCount,
					GroupIndex + 1,
					InOutSelection,
					OutSelections);
				InOutSelection.Pop(EAllowShrinking::No);
			}
		}

		static bool DoesCandidateSelectionCoverAllLevels(
			const TArray<FParentContactCapabilityCandidate>& CandidateSelection,
			const TArray<int32>& RequiredLevels)
		{
			for (const int32 RequiredLevel : RequiredLevels)
			{
				if (!CandidateSelection.ContainsByPredicate(
					[RequiredLevel](const FParentContactCapabilityCandidate& Candidate)
					{
						return Candidate.LevelIndex == RequiredLevel;
					}))
				{
					return false;
				}
			}

			return true;
		}

		static FContactBackedPlacementFamily BuildPlacementFamilyFromSelectedCandidates(
			const FLayoutId FamilyId,
			const TArray<FParentContactCapabilityCandidate>& CandidateSelection,
			const bool bAllowsChildTraversalBridge,
			const FResidualParentCapabilitySummary& ParentSummary)
		{
			FContactBackedPlacementFamily ContactFamily;
			ContactFamily.FamilyId = FamilyId;
			ContactFamily.bAllowsChildTraversalBridge = bAllowsChildTraversalBridge;
			if (!CandidateSelection.IsEmpty())
			{
				ContactFamily.PlacementBundle = CandidateSelection[0].PlacementBundle;
				ContactFamily.ParentComponentId = CandidateSelection[0].ParentComponentId;
				ContactFamily.AdjacencyClassId = CandidateSelection[0].AdjacencyClassId;
			}

			for (const FParentContactCapabilityCandidate& Candidate : CandidateSelection)
			{
				ContactFamily.EndpointCommitments.Add(Candidate.EndpointCommitment);
				ContactFamily.ParentContactCells.Add(Candidate.ParentContactCell);
				ContactFamily.AllCommittedContactTraversalAnchors.Append(
					Candidate.AllCommittedContactTraversalAnchors);
			}

			NormalizeNegotiatedContactSet(ContactFamily);
			SelectParentTraversalIngressSubset(ParentSummary, ContactFamily);
			return ContactFamily;
		}

		static TSet<int32> CollectParentRouteSeedComponents(
			const FLayoutRegionSolveRequest& RootRequest,
			const FResidualParentCapabilitySummary& ParentSummary)
		{
			TSet<int32> RouteSeedComponents;
			for (const FLayoutPlannedCell& PlannedCell : RootRequest.PlannedCells)
			{
				if (PlannedCell.Intent != ELayoutCellIntent::Entry)
				{
					continue;
				}

				if (const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(PlannedCell.Cell))
				{
					RouteSeedComponents.Add(*ComponentId);
				}
			}

			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
				RootRequest.CommittedTraversalAnchors)
			{
				if (const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(TraversalAnchor.Cell))
				{
					RouteSeedComponents.Add(*ComponentId);
				}
			}

			for (const FIntVector& ProtectedCell : RootRequest.ProtectedStructuralCells)
			{
				if (const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(ProtectedCell))
				{
					RouteSeedComponents.Add(*ComponentId);
				}
			}

			for (const FLayoutCommittedEndpointAnchor& EndpointAnchor :
				RootRequest.CommittedEndpointAnchors)
			{
				if (const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(EndpointAnchor.LocalCell))
				{
					RouteSeedComponents.Add(*ComponentId);
				}
			}

			for (const FIntVector& RouteSupportCell :
				GetRetainedParentRouteSupportCells(ParentSummary))
			{
				if (const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(RouteSupportCell))
				{
					RouteSeedComponents.Add(*ComponentId);
				}
			}

			return RouteSeedComponents;
		}

		static int32 CountCountedParentProvidersOnComponent(
			const FResidualParentCapabilitySummary& ParentSummary,
			const int32 ParentComponentId)
		{
			if (ParentComponentId == INDEX_NONE)
			{
				return 0;
			}

			int32 Count = 0;
			for (const FIntVector& CountedParentProviderCell :
				ParentSummary.CountedParentVerticalAccessCells)
			{
				const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(
						CountedParentProviderCell);
				if (ComponentId != nullptr && *ComponentId == ParentComponentId)
				{
					++Count;
				}
			}
			return Count;
		}

		static int32 CountReachableRootExternalEndpointsOnComponent(
			const FResidualParentCapabilitySummary& ParentSummary,
			const int32 ParentComponentId)
		{
			if (ParentComponentId == INDEX_NONE)
			{
				return 0;
			}

			int32 Count = 0;
			for (const FResidualExternalEndpointReachability& Reachability :
				ParentSummary.ExternalEndpointReachability)
			{
				if (Reachability.bReachableFromResidualParent
					&& Reachability.ParentComponentId == ParentComponentId)
				{
					++Count;
				}
			}

			return Count;
		}

		static int32 ResolvePlacementFamilySelectionScore(
			const FLayoutRegionSolveRequest& RootRequest,
			const FContactBackedPlacementFamily& ContactFamily,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FLayoutNegotiatedChildResponsibilityContract& Contract,
			const int32 RequiredHostProviderCount)
		{
			int32 Score = 0;
			const TSet<int32> ParentRouteSeedComponents =
				CollectParentRouteSeedComponents(RootRequest, ParentSummary);
			if (ContactFamily.ParentComponentId != INDEX_NONE
				&& ParentRouteSeedComponents.Contains(ContactFamily.ParentComponentId))
			{
				Score += 16;
			}
			Score += CountReachableRootExternalEndpointsOnComponent(
				ParentSummary,
				ContactFamily.ParentComponentId) * 12;
			if (RequiredHostProviderCount > 0)
			{
				const int32 CountedProvidersOnComponent =
					Contract.CountedParentProviderCount;
				Score += CountedProvidersOnComponent * 6;
				if (CountedProvidersOnComponent >= RequiredHostProviderCount)
				{
					Score += 24;
				}
				else
				{
					Score -= 24;
				}
			}
			const TArray<FIntVector>& RouteSupportCells =
				Contract.RetainedParentRouteSupportVerticalAccessCells.IsEmpty()
					? GetRetainedParentRouteSupportCells(ParentSummary)
					: Contract.RetainedParentRouteSupportVerticalAccessCells;
			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : ContactFamily.ParentTraversalIngressAnchors)
			{
				if (RouteSupportCells.Contains(TraversalAnchor.Cell))
				{
					Score += 8;
				}
			}
			for (const FIntVector& ParentContactCell : ContactFamily.ParentContactCells)
			{
				if (RouteSupportCells.Contains(ParentContactCell))
				{
					Score += 4;
				}
				if (ParentSummary.RootConnectedTraversableCells.Contains(ParentContactCell))
				{
					++Score;
				}
			}
			Score += ContactFamily.EndpointCommitments.Num() * 2;
			Score += ContactFamily.AllCommittedContactTraversalAnchors.Num();
			Score -= ContactFamily.ParentTraversalIngressAnchors.Num();
			return Score;
		}

		static void SortNegotiationCells(TArray<FIntVector>& Cells)
		{
			Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return LexicalLess(Left, Right);
			});
			Cells.SetNum(Algo::Unique(Cells));
		}

		static void SortNegotiationNames(TArray<FLayoutId>& Names)
		{
			Names.Sort([](const FLayoutId& Left, const FLayoutId& Right)
			{
				return Left.ToString() < Right.ToString();
			});
			Names.SetNum(Algo::Unique(Names));
		}

		static void SortNegotiationLevels(TArray<int32>& Levels)
		{
			Levels.Sort();
			for (int32 Index = Levels.Num() - 1; Index > 0; --Index)
			{
				if (Levels[Index] == Levels[Index - 1])
				{
					Levels.RemoveAt(Index);
				}
			}
		}

		static void CollectBoundedParentRouteSeedCells(
			const FLayoutRegionSolveRequest& RootRequest,
			const FResidualParentCapabilitySummary& ParentSummary,
			const int32 ParentComponentId,
			TArray<FIntVector>& OutRouteSeedCells,
			const bool bIncludeCountedParentProviders = true)
		{
			if (ParentComponentId == INDEX_NONE)
			{
				return;
			}

			auto TryAppendRouteSeedCell =
				[&ParentSummary, ParentComponentId, &OutRouteSeedCells](const FIntVector& Cell)
				{
					const int32* CellComponentId =
						ParentSummary.RootConnectedComponentIdByCell.Find(Cell);
					if (CellComponentId != nullptr && *CellComponentId == ParentComponentId)
					{
						OutRouteSeedCells.Add(Cell);
					}
				};

			for (const FLayoutPlannedCell& PlannedCell : RootRequest.PlannedCells)
			{
				if (PlannedCell.Intent == ELayoutCellIntent::Entry)
				{
					TryAppendRouteSeedCell(PlannedCell.Cell);
				}
			}

			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
				RootRequest.CommittedTraversalAnchors)
			{
				TryAppendRouteSeedCell(TraversalAnchor.Cell);
			}

			for (const FIntVector& ProtectedCell : RootRequest.ProtectedStructuralCells)
			{
				TryAppendRouteSeedCell(ProtectedCell);
			}

			for (const FLayoutCommittedEndpointAnchor& EndpointAnchor :
				RootRequest.CommittedEndpointAnchors)
			{
				TryAppendRouteSeedCell(EndpointAnchor.LocalCell);
			}

			if (!bIncludeCountedParentProviders)
			{
				return;
			}

			for (const FIntVector& RouteSupportCell :
				GetRetainedParentRouteSupportCells(ParentSummary))
			{
				TryAppendRouteSeedCell(RouteSupportCell);
			}
		}

		static void CollectNegotiatedParentRouteTargetCells(
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			TArray<FIntVector>& OutRouteTargetCells)
		{
			auto TryAppendRouteTargetCell =
				[&ParentSummary, &ContactSet, &OutRouteTargetCells](const FIntVector& Cell)
				{
					if (ContactSet.ParentComponentId == INDEX_NONE)
					{
						OutRouteTargetCells.Add(Cell);
						return;
					}

					const int32* CellComponentId =
						ParentSummary.RootConnectedComponentIdByCell.Find(Cell);
					if (CellComponentId != nullptr
						&& *CellComponentId == ContactSet.ParentComponentId)
					{
						OutRouteTargetCells.Add(Cell);
					}
				};

			for (const FIntVector& ParentContactCell : ContactSet.ParentContactCells)
			{
				TryAppendRouteTargetCell(ParentContactCell);
			}

			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
				ContactSet.ParentTraversalIngressAnchors)
			{
				TryAppendRouteTargetCell(TraversalAnchor.Cell);
			}

			SortNegotiationCells(OutRouteTargetCells);
		}

		static bool TryBuildNegotiatedParentRouteNetwork(
			const FResidualParentCapabilitySummary& ParentSummary,
			const TArray<FIntVector>& RouteSeedCells,
			const TArray<FIntVector>& RouteTargetCells,
			TArray<FIntVector>& OutRouteNetworkCells)
		{
			if (RouteSeedCells.IsEmpty() || RouteTargetCells.IsEmpty())
			{
				return false;
			}

			auto BuildDistances =
				[&ParentSummary](
					const TArray<FIntVector>& StartCells,
					TMap<FIntVector, int32>& OutDistances)
				{
					TArray<FIntVector> PendingCells;
					PendingCells.Reserve(StartCells.Num());
					for (const FIntVector& StartCell : StartCells)
					{
						if (OutDistances.Contains(StartCell))
						{
							continue;
						}

						OutDistances.Add(StartCell, 0);
						PendingCells.Add(StartCell);
					}

					for (int32 PendingIndex = 0; PendingIndex < PendingCells.Num(); ++PendingIndex)
					{
						const FIntVector CurrentCell = PendingCells[PendingIndex];
						const int32 CurrentDistance = OutDistances.FindChecked(CurrentCell);
						const TArray<ELayoutFaceDirection>* TraversableFaces =
							ParentSummary.RootConnectedTraversableFacesByCell.Find(CurrentCell);
						if (TraversableFaces == nullptr)
						{
							continue;
						}

						for (const ELayoutFaceDirection FaceDirection : *TraversableFaces)
						{
							const FIntVector NeighborCell =
								CurrentCell + FLayoutDirectionUtils::ToCellDelta(FaceDirection);
							if (OutDistances.Contains(NeighborCell)
								|| !ParentSummary.RootConnectedTraversableCells.Contains(NeighborCell))
							{
								continue;
							}

							OutDistances.Add(NeighborCell, CurrentDistance + 1);
							PendingCells.Add(NeighborCell);
						}
					}
				};

			TMap<FIntVector, int32> DistanceFromSeeds;
			BuildDistances(RouteSeedCells, DistanceFromSeeds);

			int32 BestRouteDistance = MAX_int32;
			for (const FIntVector& RouteTargetCell : RouteTargetCells)
			{
				if (const int32* Distance = DistanceFromSeeds.Find(RouteTargetCell))
				{
					BestRouteDistance = FMath::Min(BestRouteDistance, *Distance);
				}
			}

			if (BestRouteDistance == MAX_int32)
			{
				return false;
			}

			TMap<FIntVector, int32> DistanceFromTargets;
			BuildDistances(RouteTargetCells, DistanceFromTargets);

			for (const TPair<FIntVector, int32>& Pair : DistanceFromSeeds)
			{
				const int32* DistanceToTarget = DistanceFromTargets.Find(Pair.Key);
				if (DistanceToTarget != nullptr
					&& Pair.Value + *DistanceToTarget == BestRouteDistance)
				{
					OutRouteNetworkCells.Add(Pair.Key);
				}
			}

			SortNegotiationCells(OutRouteNetworkCells);
			return !OutRouteNetworkCells.IsEmpty();
		}

		static TArray<FIntVector> FilterCountedParentProvidersToNegotiatedRouteNetwork(
			const FLayoutRegionSolveRequest* RootRequest,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			const TArray<FIntVector>& CountedParentProviders,
			TArray<FIntVector>* OutRouteNetworkCells = nullptr)
		{
			if (RootRequest == nullptr
				|| ContactSet.ParentComponentId == INDEX_NONE
				|| CountedParentProviders.IsEmpty())
			{
				return CountedParentProviders;
			}

			TArray<FIntVector> RouteSeedCells;
			CollectBoundedParentRouteSeedCells(
				*RootRequest,
				ParentSummary,
				ContactSet.ParentComponentId,
				RouteSeedCells,
				false);

			TArray<FIntVector> RouteTargetCells;
			CollectNegotiatedParentRouteTargetCells(
				ParentSummary,
				ContactSet,
				RouteTargetCells);

			TArray<FIntVector> RouteNetworkCells;
			if (!TryBuildNegotiatedParentRouteNetwork(
					ParentSummary,
					RouteSeedCells,
					RouteTargetCells,
					RouteNetworkCells))
			{
				return CountedParentProviders;
			}

			if (OutRouteNetworkCells != nullptr)
			{
				*OutRouteNetworkCells = RouteNetworkCells;
			}

			const TSet<FIntVector> RouteNetworkCellSet(RouteNetworkCells);
			TArray<FIntVector> FilteredProviders;
			for (const FIntVector& CountedParentProviderCell : CountedParentProviders)
			{
				if (RouteNetworkCellSet.Contains(CountedParentProviderCell))
				{
					FilteredProviders.Add(CountedParentProviderCell);
				}
			}

			SortNegotiationCells(FilteredProviders);
			return FilteredProviders;
		}

		static FBoundedParentProofEvidence BuildBoundedParentProofEvidence(
			const FLayoutRegionSolveRequest& RootRequest,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			FBoundedParentProofEvidence Evidence;
			Evidence.ChildRegionDebugPath = Contract.ChildRegionDebugPath;
			Evidence.ParentComponentId = ContactSet.ParentComponentId;
			CollectBoundedParentRouteSeedCells(
				RootRequest,
				ParentSummary,
				ContactSet.ParentComponentId,
				Evidence.ConfirmedParentRouteSeedCells);
			Evidence.ConfirmedParentContactCells = ContactSet.ParentContactCells;
			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
				ContactSet.ParentTraversalIngressAnchors)
			{
				Evidence.ConfirmedParentTraversalIngressCells.Add(TraversalAnchor.Cell);
			}
			Evidence.ConfirmedCountedParentProviderCells =
				Contract.CountedParentVerticalAccessCells;
			Evidence.ConfirmedRequiredChildBundleSupportCells =
				ParentSummary.RequiredChildBundleSupportCells;
			TSet<FLayoutId> RequiredRootEndpointCommitmentIds;
			for (const FLayoutCommittedEndpointAnchor& RootEndpointCommitment :
				RootRequest.CommittedEndpointAnchors)
			{
				if (!RootEndpointCommitment.CommitmentId.IsNone())
				{
					RequiredRootEndpointCommitmentIds.Add(
						RootEndpointCommitment.CommitmentId);
				}
			}
			for (const FResidualExternalEndpointReachability& Reachability :
				ParentSummary.ExternalEndpointReachability)
			{
				if (Reachability.bReachableFromResidualParent
					&& !Reachability.CommitmentId.IsNone()
					&& RequiredRootEndpointCommitmentIds.Contains(
						Reachability.CommitmentId)
					&& (ContactSet.ParentComponentId == INDEX_NONE
						|| Reachability.ParentComponentId == INDEX_NONE
						|| Reachability.ParentComponentId == ContactSet.ParentComponentId))
				{
					Evidence.PreservedRootExternalEndpointCommitmentIds.Add(
						Reachability.CommitmentId);
				}
			}

			SortNegotiationCells(Evidence.ConfirmedParentRouteSeedCells);
			SortNegotiationCells(Evidence.ConfirmedParentContactCells);
			SortNegotiationCells(Evidence.ConfirmedParentTraversalIngressCells);
			SortNegotiationCells(Evidence.ConfirmedCountedParentProviderCells);
			SortNegotiationCells(Evidence.ConfirmedRequiredChildBundleSupportCells);
			SortNegotiationNames(Evidence.PreservedRootExternalEndpointCommitmentIds);
			return Evidence;
		}

		static void SortNegotiationCommittedEndpointAnchors(
			TArray<FLayoutCommittedEndpointAnchor>& Anchors)
		{
			Anchors.Sort([](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
			{
				if (Left.CommitmentId != Right.CommitmentId)
				{
					return Left.CommitmentId.LexicalLess(Right.CommitmentId);
				}
				if (Left.LocalCell != Right.LocalCell)
				{
					return LexicalLess(Left.LocalCell, Right.LocalCell);
				}
				if (Left.RequiredWorldCenterBlockZ != Right.RequiredWorldCenterBlockZ)
				{
					return Left.RequiredWorldCenterBlockZ < Right.RequiredWorldCenterBlockZ;
				}
				return static_cast<uint8>(Left.FaceDirection) < static_cast<uint8>(Right.FaceDirection);
			});
			Anchors.SetNum(Algo::Unique(
				Anchors,
				[](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
				{
					return AreCommittedEndpointAnchorsEquivalent(Left, Right);
				}));
		}

		static void SortNegotiationCommittedTraversalAnchors(
			TArray<FLayoutCommittedTraversalAnchor>& Anchors)
		{
			Anchors.Sort([](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
			{
				if (Left.Cell != Right.Cell)
				{
					return LexicalLess(Left.Cell, Right.Cell);
				}
				return Left.TraversalChannel.ToString() < Right.TraversalChannel.ToString();
			});
			Anchors.SetNum(Algo::Unique(
				Anchors,
				[](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
				{
					return AreCommittedTraversalAnchorsEquivalent(Left, Right);
			}));
		}

		static void SortNegotiationParentContactFaces(
			TArray<FCommittedParentContactFace>& ParentContactFaces)
		{
			ParentContactFaces.Sort([](
				const FCommittedParentContactFace& Left,
				const FCommittedParentContactFace& Right)
			{
				return Left.Cell.Z != Right.Cell.Z ? Left.Cell.Z < Right.Cell.Z
					: Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y
					: Left.Cell.X != Right.Cell.X ? Left.Cell.X < Right.Cell.X
					: static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			});
			ParentContactFaces.SetNum(Algo::Unique(
				ParentContactFaces,
				[](const FCommittedParentContactFace& Left, const FCommittedParentContactFace& Right)
				{
					return AreParentContactFacesEquivalent(Left, Right);
				}));
		}

		static FLayoutNegotiatedLevelCellSet& FindOrAddNegotiatedLevelCellSetForNegotiation(
			TArray<FLayoutNegotiatedLevelCellSet>& LevelSets,
			const int32 Level)
		{
			if (FLayoutNegotiatedLevelCellSet* Existing = LevelSets.FindByPredicate(
				[Level](const FLayoutNegotiatedLevelCellSet& LevelSet)
				{
					return LevelSet.Level == Level;
				}))
			{
				return *Existing;
			}

			FLayoutNegotiatedLevelCellSet& Added = LevelSets.AddDefaulted_GetRef();
			Added.Level = Level;
			return Added;
		}

		static FLayoutNegotiatedLevelInterfaceContract& FindOrAddNegotiatedLevelInterfaceContractForNegotiation(
			TArray<FLayoutNegotiatedLevelInterfaceContract>& Contracts,
			const int32 Level)
		{
			if (FLayoutNegotiatedLevelInterfaceContract* Existing = Contracts.FindByPredicate(
				[Level](const FLayoutNegotiatedLevelInterfaceContract& Contract)
				{
					return Contract.Level == Level;
				}))
			{
				return *Existing;
			}

			FLayoutNegotiatedLevelInterfaceContract& Added = Contracts.AddDefaulted_GetRef();
			Added.Level = Level;
			return Added;
		}

		static void NormalizeNegotiatedResponsibilityContract(
			FLayoutNegotiatedChildResponsibilityContract& InOutContract)
		{
			for (FLayoutNegotiatedLevelCellSet& LevelSet : InOutContract.ReplacementVolumeByLevel)
			{
				SortNegotiationCells(LevelSet.Cells);
			}
			InOutContract.ReplacementVolumeByLevel.Sort([](
				const FLayoutNegotiatedLevelCellSet& Left,
				const FLayoutNegotiatedLevelCellSet& Right)
			{
				return Left.Level < Right.Level;
			});

			for (FLayoutNegotiatedLevelCellSet& LevelSet : InOutContract.RetainedParentShellCellsByLevel)
			{
				SortNegotiationCells(LevelSet.Cells);
			}
			InOutContract.RetainedParentShellCellsByLevel.Sort([](
				const FLayoutNegotiatedLevelCellSet& Left,
				const FLayoutNegotiatedLevelCellSet& Right)
			{
				return Left.Level < Right.Level;
			});

			for (FLayoutNegotiatedLevelCellSet& LevelSet : InOutContract.ProofOnlyHostAscentParentShellCellsByLevel)
			{
				SortNegotiationCells(LevelSet.Cells);
			}
			InOutContract.ProofOnlyHostAscentParentShellCellsByLevel.Sort([](
				const FLayoutNegotiatedLevelCellSet& Left,
				const FLayoutNegotiatedLevelCellSet& Right)
			{
				return Left.Level < Right.Level;
			});

			for (FLayoutNegotiatedLevelInterfaceContract& InterfaceContract : InOutContract.CommittedParentChildInterfacesByLevel)
			{
				SortNegotiationCommittedEndpointAnchors(InterfaceContract.EndpointAnchors);
				SortNegotiationCommittedTraversalAnchors(InterfaceContract.TraversalAnchors);
			}
			InOutContract.CommittedParentChildInterfacesByLevel.Sort([](
				const FLayoutNegotiatedLevelInterfaceContract& Left,
				const FLayoutNegotiatedLevelInterfaceContract& Right)
			{
				return Left.Level < Right.Level;
			});

			SortNegotiationCells(InOutContract.CountedParentVerticalAccessCells);
			SortNegotiationCells(InOutContract.RetainedParentRouteSupportVerticalAccessCells);
			InOutContract.CountedChildProviderRegionDebugPaths.Sort();
			SortNegotiationLevels(InOutContract.RequiredChildInternalVerticalSpanLevels);
			SortNegotiationCells(InOutContract.RequiredChildInternalVerticalRouteCells);
		}

		static FString DescribeNegotiatedHostVerticalAccessResponsibility(
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

		static int32 CountTotalNegotiatedHostProviders(
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			switch (Contract.HostVerticalAccessResponsibility)
			{
			case ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned:
				return Contract.CountedParentProviderCount;
			case ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned:
				return Contract.CountedChildProviderRegionDebugPaths.Num();
			case ELayoutNegotiatedHostVerticalAccessResponsibility::Composed:
				// Composed host ascent means the child participates in the same
				// provider group that the retained parent component already counts.
				// Keep the child path for diagnostics and proof ownership, but do
				// not double-count it as a second authored host provider.
				return Contract.CountedParentProviderCount;
			default:
				return Contract.CountedParentProviderCount
					+ Contract.CountedChildProviderRegionDebugPaths.Num();
			}
		}

		static bool DoesNegotiatedContractSatisfyHostProviderCount(
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			if (Contract.RequiredHostProviderCount <= 0)
			{
				return true;
			}

			const int32 TotalNegotiatedHostProviders =
				CountTotalNegotiatedHostProviders(Contract);
			if (Contract.bRequiresExactHostProviderCount)
			{
				return TotalNegotiatedHostProviders == Contract.RequiredHostProviderCount;
			}

			return TotalNegotiatedHostProviders >= Contract.RequiredHostProviderCount;
		}

		static bool ValidateReplacementVolumeDoesNotOverlapRetainedShell(
			const FLayoutNegotiatedChildResponsibilityContract& Contract,
			FString& OutFailureReason)
		{
			for (const FLayoutNegotiatedLevelCellSet& RetainedShellLevel :
				Contract.RetainedParentShellCellsByLevel)
			{
				const FLayoutNegotiatedLevelCellSet* ReplacementLevel =
					Contract.ReplacementVolumeByLevel.FindByPredicate(
						[&RetainedShellLevel](const FLayoutNegotiatedLevelCellSet& LevelSet)
						{
							return LevelSet.Level == RetainedShellLevel.Level;
						});
				if (ReplacementLevel == nullptr)
				{
					continue;
				}

				for (const FIntVector& RetainedShellCell : RetainedShellLevel.Cells)
				{
					if (ReplacementLevel->Cells.Contains(RetainedShellCell))
					{
						OutFailureReason = FString::Printf(
							TEXT("Negotiated child responsibility contract for child region '%s' keeps parent-local cell %s in both replacement volume and retained parent shell on level %d."),
							*Contract.ChildRegionDebugPath,
							*RetainedShellCell.ToString(),
							RetainedShellLevel.Level);
						return false;
					}
				}
			}

			return true;
		}

		static bool ValidateNegotiatedChildResponsibilityContract(
			const FLayoutNegotiatedChildResponsibilityContract& Contract,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();
			if (Contract.ParentRegionDebugPath.IsEmpty())
			{
				OutFailureReason = TEXT("Negotiated child responsibility contract is missing the parent region debug path.");
				return false;
			}

			if (Contract.ChildRegionDebugPath.IsEmpty())
			{
				OutFailureReason = TEXT("Negotiated child responsibility contract is missing the child region debug path.");
				return false;
			}

			if (!ValidateReplacementVolumeDoesNotOverlapRetainedShell(
				Contract,
				OutFailureReason))
			{
				return false;
			}

			if (Contract.HostVerticalAccessResponsibility == ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned)
			{
				const bool bCurrentChildCountsTowardHost =
					Contract.CountedChildProviderRegionDebugPaths.Contains(Contract.ChildRegionDebugPath);
				if (bCurrentChildCountsTowardHost)
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated child responsibility contract for child region '%s' is marked ParentOwned, but the counted host-provider set still includes that child."),
						*Contract.ChildRegionDebugPath);
					return false;
				}

				// ParentOwned child contracts do not themselves satisfy the parent's
				// global host-ascent count; they only prove the child is not taking over
				// that responsibility. The residual parent plan validation owns the
				// concrete provider-count check after all child reservations are applied.
				if (Contract.CountedParentVerticalAccessCells.Num() != Contract.CountedParentProviderCount)
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated child responsibility contract for child region '%s' reports %d counted parent providers, but the committed parent provider set contains %d cells instead of the exact counted set."),
						*Contract.ChildRegionDebugPath,
						Contract.CountedParentProviderCount,
						Contract.CountedParentVerticalAccessCells.Num());
					return false;
				}

				return true;
			}

			if (Contract.ReplacementVolumeByLevel.IsEmpty())
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' classified host vertical access as %s, but recorded no replacement volume."),
					*Contract.ChildRegionDebugPath,
					*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility));
				return false;
			}

			const bool bCurrentChildCountsTowardHost =
				Contract.CountedChildProviderRegionDebugPaths.Contains(Contract.ChildRegionDebugPath);
			if (!bCurrentChildCountsTowardHost)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' is marked %s, but the counted host-provider set does not include that child."),
					*Contract.ChildRegionDebugPath,
					*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility));
				return false;
			}

			if (Contract.HostVerticalAccessResponsibility == ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned
				&& Contract.CountedParentProviderCount > 0)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' is marked ChildOwned, but %d parent providers were still counted."),
					*Contract.ChildRegionDebugPath,
					Contract.CountedParentProviderCount);
				return false;
			}

			if (Contract.HostVerticalAccessResponsibility == ELayoutNegotiatedHostVerticalAccessResponsibility::Composed
				&& Contract.CountedParentProviderCount <= 0)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' is marked Composed, but negotiation retained no counted parent providers."),
					*Contract.ChildRegionDebugPath);
				return false;
			}

			if (Contract.CountedParentVerticalAccessCells.Num() != Contract.CountedParentProviderCount)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' reports %d counted parent providers for %s host ascent, but the committed parent provider set contains %d cells instead of the exact counted set."),
					*Contract.ChildRegionDebugPath,
					Contract.CountedParentProviderCount,
					*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
					Contract.CountedParentVerticalAccessCells.Num());
				return false;
			}

			if (Contract.RequiredHostProviderCount > 0)
			{
				const int32 TotalNegotiatedHostProviders =
					CountTotalNegotiatedHostProviders(Contract);
				if (!DoesNegotiatedContractSatisfyHostProviderCount(Contract))
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated child responsibility contract for child region '%s' requires %s %d total counted host providers, but %s negotiation retained %d across child and parent providers."),
						*Contract.ChildRegionDebugPath,
						Contract.bRequiresExactHostProviderCount ? TEXT("exactly") : TEXT("at least"),
						Contract.RequiredHostProviderCount,
						*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
						TotalNegotiatedHostProviders);
					return false;
				}
			}

			if (!Contract.bHasRequiredHostIngressAnchor || !Contract.bHasRequiredHostEgressAnchor)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' classified host vertical access as %s, but did not commit both required host-facing anchors."),
					*Contract.ChildRegionDebugPath,
					*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility));
				return false;
			}

			if (Contract.RequiredHostIngressAnchor.LocalCell.Z == Contract.RequiredHostEgressAnchor.LocalCell.Z)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' committed ingress and egress anchors on the same level %d."),
					*Contract.ChildRegionDebugPath,
					Contract.RequiredHostIngressAnchor.LocalCell.Z);
				return false;
			}

			const int32 LowerLevel = FMath::Min(
				Contract.RequiredHostIngressAnchor.LocalCell.Z,
				Contract.RequiredHostEgressAnchor.LocalCell.Z);
			const int32 UpperLevel = FMath::Max(
				Contract.RequiredHostIngressAnchor.LocalCell.Z,
				Contract.RequiredHostEgressAnchor.LocalCell.Z);
			if (!Contract.RequiredChildGenerallyConnectableAnchorPairId.IsNone())
			{
				TSet<int32> SpannedLevels;
				for (const int32 Level : Contract.RequiredChildInternalVerticalSpanLevels)
				{
					SpannedLevels.Add(Level);
				}
				for (int32 Level = LowerLevel; Level <= UpperLevel; ++Level)
				{
					if (!SpannedLevels.Contains(Level))
					{
						OutFailureReason = FString::Printf(
							TEXT("Negotiated child responsibility contract for child region '%s' classified host vertical access as %s, but the generally connectable child anchor-pair proof does not cover ascent level %d."),
							*Contract.ChildRegionDebugPath,
							*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
							Level);
						return false;
					}
				}
			}
			else if (!Contract.RequiredChildInternalVerticalRouteCells.IsEmpty())
			{
				TSet<int32> RouteLevels;
				for (const FIntVector& RouteCell : Contract.RequiredChildInternalVerticalRouteCells)
				{
					RouteLevels.Add(RouteCell.Z);
				}
				for (int32 Level = LowerLevel + 1; Level < UpperLevel; ++Level)
				{
					if (!RouteLevels.Contains(Level))
					{
						OutFailureReason = FString::Printf(
							TEXT("Negotiated child responsibility contract for child region '%s' classified host vertical access as %s, but the child internal vertical route does not cover intermediate ascent level %d."),
							*Contract.ChildRegionDebugPath,
							*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
							Level);
						return false;
					}
				}
			}
			else
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' classified host vertical access as %s, but recorded neither a generally connectable child anchor-pair proof nor compatibility route cells."),
					*Contract.ChildRegionDebugPath,
					*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility));
				return false;
			}
			for (int32 Level = LowerLevel; Level <= UpperLevel; ++Level)
			{
				const FLayoutNegotiatedLevelCellSet* ReplacementLevel =
					Contract.ReplacementVolumeByLevel.FindByPredicate(
						[Level](const FLayoutNegotiatedLevelCellSet& LevelSet)
						{
							return LevelSet.Level == Level && !LevelSet.Cells.IsEmpty();
						});
				if (ReplacementLevel == nullptr)
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated child responsibility contract for child region '%s' does not reserve any replacement cells on covered host-ascent level %d."),
						*Contract.ChildRegionDebugPath,
						Level);
					return false;
				}
			}

			return true;
		}

		static int32 ResolveRequiredHostProviderCount(
			const FRecursiveScheduleSolveContext& SolveContext)
		{
			switch (SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode)
			{
			case ELayoutCountConstraintMode::Exact:
				if (SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount > 0)
				{
					return SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount;
				}
				break;
			case ELayoutCountConstraintMode::Range:
				if (SolveContext.RootRequest.ProfileSnapshot.MaxVerticalAccessCount > 0)
				{
					return SolveContext.RootRequest.ProfileSnapshot.MaxVerticalAccessCount;
				}
				break;
			case ELayoutCountConstraintMode::None:
			default:
				break;
			}

			return SolveContext.ParentPlannedCells.ContainsByPredicate(
				[](const FLayoutPlannedCell& PlannedCell)
				{
					return PlannedCell.Intent == ELayoutCellIntent::VerticalAccess;
				})
				? 1
				: 0;
		}

		static bool ResolveRequiresExactHostProviderCount(
			const FRecursiveScheduleSolveContext& SolveContext)
		{
			return SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
				&& SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount > 0;
		}

		static void ResolveNegotiatedEntryCountRange(
			const FLayoutProfileSolveSnapshot& ProfileSnapshot,
			int32& OutMinimumEntryCount,
			int32& OutMaximumEntryCount)
		{
			switch (ProfileSnapshot.EntryCountMode)
			{
			case ELayoutCountConstraintMode::Exact:
				OutMinimumEntryCount = FMath::Max(1, ProfileSnapshot.EntryCount);
				OutMaximumEntryCount = OutMinimumEntryCount;
				return;
			case ELayoutCountConstraintMode::Range:
				OutMinimumEntryCount = FMath::Max(1, ProfileSnapshot.MinEntryCount);
				OutMaximumEntryCount = FMath::Max(
					OutMinimumEntryCount,
					ProfileSnapshot.MaxEntryCount);
				return;
			case ELayoutCountConstraintMode::None:
			default:
				OutMinimumEntryCount = 1;
				OutMaximumEntryCount = 1;
				return;
			}
		}

		static bool IsTraversableIntentForNegotiatedVerticalRoute(
			const ELayoutCellIntent Intent)
		{
			switch (Intent)
			{
			case ELayoutCellIntent::Entry:
			case ELayoutCellIntent::Core:
			case ELayoutCellIntent::Interior:
			case ELayoutCellIntent::Connector:
			case ELayoutCellIntent::VerticalAccess:
				return true;
			default:
				return false;
			}
		}

		static TArray<FIntVector> CollectRetainedParentVerticalAccessCellsForContactComponent(
			const TArray<FIntVector>& SourceCells,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			const TArray<FIntVector>& ReservedParentCells)
		{
			const TSet<FIntVector> ReservedParentCellSet(ReservedParentCells);
			if (ContactSet.ParentComponentId == INDEX_NONE)
			{
				TArray<FIntVector> RetainedCells =
					SourceCells.FilterByPredicate(
						[&ReservedParentCellSet](const FIntVector& Cell)
						{
							return !ReservedParentCellSet.Contains(Cell);
						});
				SortNegotiationCells(RetainedCells);
				return RetainedCells;
			}

			TArray<FIntVector> RetainedCells;
			for (const FIntVector& Cell : SourceCells)
			{
				if (ReservedParentCellSet.Contains(Cell))
				{
					continue;
				}

				const int32* ProviderComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(Cell);
				if (ProviderComponentId != nullptr
					&& *ProviderComponentId == ContactSet.ParentComponentId)
				{
					RetainedCells.Add(Cell);
				}
			}
			SortNegotiationCells(RetainedCells);
			return RetainedCells;
		}

		// Route-support fallback can preserve a full retained stair stack even when
		// the residual rewrite no longer precomputes explicit counted-provider
		// representatives. Collapse each connected retained stack/group back to one
		// deterministic representative cell so exact host-provider counts stay
		// aligned with provider groups rather than raw stair-cell totals.
		static TArray<FIntVector> CollapseRetainedRouteSupportCellsToProviderRepresentatives(
			const TArray<FIntVector>& RetainedRouteSupportCells)
		{
			TArray<FIntVector> SortedCells = RetainedRouteSupportCells;
			SortNegotiationCells(SortedCells);
			if (SortedCells.Num() <= 1)
			{
				return SortedCells;
			}

			TSet<FIntVector> RemainingCells(SortedCells);
			TArray<FIntVector> Representatives;
			static const ELayoutFaceDirection ConnectivityDirections[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY,
				ELayoutFaceDirection::PosZ,
				ELayoutFaceDirection::NegZ
			};

			for (const FIntVector& SeedCell : SortedCells)
			{
				if (!RemainingCells.Contains(SeedCell))
				{
					continue;
				}

				FIntVector RepresentativeCell = SeedCell;
				TArray<FIntVector> Frontier = {SeedCell};
				RemainingCells.Remove(SeedCell);
				for (int32 FrontierIndex = 0; FrontierIndex < Frontier.Num(); ++FrontierIndex)
				{
					const FIntVector CurrentCell = Frontier[FrontierIndex];
					if (LexicalLess(CurrentCell, RepresentativeCell))
					{
						RepresentativeCell = CurrentCell;
					}

					for (const ELayoutFaceDirection Direction : ConnectivityDirections)
					{
						const FIntVector NeighborCell =
							CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (!RemainingCells.Contains(NeighborCell))
						{
							continue;
						}

						RemainingCells.Remove(NeighborCell);
						Frontier.Add(NeighborCell);
					}
				}

				Representatives.Add(RepresentativeCell);
			}

			SortNegotiationCells(Representatives);
			return Representatives;
		}

		// Count only parent-owned vertical-access provider groups that remain on the
		// selected residual component after the child's negotiated replacement
		// volume displaces parent cells.
		static TArray<FIntVector> CollectCountedParentProvidersForContactComponent(
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			const TArray<FIntVector>& ReservedParentCells)
		{
			if (!ParentSummary.CountedParentVerticalAccessCells.IsEmpty())
			{
				return CollectRetainedParentVerticalAccessCellsForContactComponent(
					ParentSummary.CountedParentVerticalAccessCells,
					ParentSummary,
					ContactSet,
					ReservedParentCells);
			}

			const TArray<FIntVector> RetainedRouteSupportCells =
				CollectRetainedParentVerticalAccessCellsForContactComponent(
					GetRetainedParentRouteSupportCells(ParentSummary),
					ParentSummary,
					ContactSet,
					ReservedParentCells);
			return CollapseRetainedRouteSupportCellsToProviderRepresentatives(
				RetainedRouteSupportCells);
		}

		static TArray<FIntVector> CollectRouteSupportParentVerticalAccessCellsForContactComponent(
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			const TArray<FIntVector>& ReservedParentCells)
		{
			return CollectRetainedParentVerticalAccessCellsForContactComponent(
				ParentSummary.RouteSupportVerticalAccessCells,
				ParentSummary,
				ContactSet,
				ReservedParentCells);
		}

		// In exact-single host-ascent profiles, a lone counted parent stair cell
		// that survives only as the lower negotiated handoff into a child-owned
		// multi-level ascent should not be treated as a second independent host
		// provider. The child already proves the real lower/upper ascent contract;
		// this surviving parent contact remains a route handoff, not an authored
		// extra provider.
		static bool IsSingleCountedParentProviderOnlyLowerHostHandoff(
			const FLayoutNegotiatedChildResponsibilityContract& Contract,
			const FContactBackedPlacementFamily& ContactSet,
			const TArray<FIntVector>& CountedParentProviders,
			const FLayoutCommittedEndpointAnchor& IngressAnchor,
			const FLayoutCommittedEndpointAnchor& EgressAnchor,
			const FResidualParentCapabilitySummary* ParentSummary = nullptr,
			const TArray<FIntVector>* RouteNetworkCells = nullptr)
		{
			if (!Contract.bRequiresExactHostProviderCount
				|| Contract.RequiredHostProviderCount != 1
				|| CountedParentProviders.Num() != 1)
			{
				return false;
			}

			const FIntVector& ProviderCell = CountedParentProviders[0];
			const int32 LowerHostLevel = FMath::Min(
				IngressAnchor.LocalCell.Z,
				EgressAnchor.LocalCell.Z);
			if (ProviderCell.Z != LowerHostLevel)
			{
				return false;
			}

			const bool bMatchesParentContact =
				ContactSet.ParentContactCells.Contains(ProviderCell);
			const bool bMatchesTraversalIngress =
				ContactSet.ParentTraversalIngressAnchors.ContainsByPredicate(
					[&ProviderCell](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
					{
						return TraversalAnchor.Cell == ProviderCell;
					});
			if (bMatchesParentContact || bMatchesTraversalIngress)
			{
				return true;
			}

			auto IsOrthogonallyAdjacentOnLowerLevel =
				[LowerHostLevel](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.Z != LowerHostLevel || Right.Z != LowerHostLevel)
				{
					return false;
				}

				const int32 DeltaX = FMath::Abs(Left.X - Right.X);
				const int32 DeltaY = FMath::Abs(Left.Y - Right.Y);
				return (DeltaX == 1 && DeltaY == 0)
					|| (DeltaX == 0 && DeltaY == 1);
			};

			const bool bTouchesParentContact =
				ContactSet.ParentContactCells.ContainsByPredicate(
					[&](const FIntVector& ParentContactCell)
					{
						return IsOrthogonallyAdjacentOnLowerLevel(
							ProviderCell,
							ParentContactCell);
					});
			if (bTouchesParentContact)
			{
				return true;
			}

			const bool bTouchesTraversalIngress =
				ContactSet.ParentTraversalIngressAnchors.ContainsByPredicate(
				[&](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
				{
					return IsOrthogonallyAdjacentOnLowerLevel(
						ProviderCell,
						TraversalAnchor.Cell);
				});
			if (bTouchesTraversalIngress)
			{
				return true;
			}

			if (ParentSummary == nullptr
				|| RouteNetworkCells == nullptr
				|| RouteNetworkCells->IsEmpty())
			{
				return false;
			}

			TSet<FIntVector> LowerLevelRouteNetworkCells;
			for (const FIntVector& RouteCell : *RouteNetworkCells)
			{
				if (RouteCell.Z == LowerHostLevel)
				{
					LowerLevelRouteNetworkCells.Add(RouteCell);
				}
			}
			if (!LowerLevelRouteNetworkCells.Contains(ProviderCell))
			{
				return false;
			}

			TArray<FIntVector> LowerLevelRouteTargets;
			for (const FIntVector& ParentContactCell : ContactSet.ParentContactCells)
			{
				if (ParentContactCell.Z == LowerHostLevel
					&& ParentContactCell != ProviderCell)
				{
					LowerLevelRouteTargets.AddUnique(ParentContactCell);
				}
			}
			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
				ContactSet.ParentTraversalIngressAnchors)
			{
				if (TraversalAnchor.Cell.Z == LowerHostLevel
					&& TraversalAnchor.Cell != ProviderCell)
				{
					LowerLevelRouteTargets.AddUnique(TraversalAnchor.Cell);
				}
			}
			if (LowerLevelRouteTargets.IsEmpty())
			{
				return false;
			}

			TSet<FIntVector> RemainingTargets(LowerLevelRouteTargets);
			TSet<FIntVector> VisitedCells = {ProviderCell};
			TArray<FIntVector> PendingCells = {ProviderCell};
			for (int32 PendingIndex = 0; PendingIndex < PendingCells.Num(); ++PendingIndex)
			{
				const FIntVector CurrentCell = PendingCells[PendingIndex];
				if (RemainingTargets.Contains(CurrentCell))
				{
					return true;
				}

				const TArray<ELayoutFaceDirection>* TraversableFaces =
					ParentSummary->RootConnectedTraversableFacesByCell.Find(CurrentCell);
				if (TraversableFaces == nullptr)
				{
					continue;
				}

				for (const ELayoutFaceDirection FaceDirection : *TraversableFaces)
				{
					const FIntVector NeighborCell =
						CurrentCell + FLayoutDirectionUtils::ToCellDelta(FaceDirection);
					if (!LowerLevelRouteNetworkCells.Contains(NeighborCell)
						|| VisitedCells.Contains(NeighborCell))
					{
						continue;
					}

					VisitedCells.Add(NeighborCell);
					PendingCells.Add(NeighborCell);
				}
			}

			return false;
		}

		static bool TrySelectNegotiatedHostVerticalAccessAnchors(
			const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
			const TArray<FLayoutPlannedCell>& ChildPlannedCells,
			const TArray<FIntVector>& ChildVerticalAccessLocalCells,
			FLayoutCommittedEndpointAnchor& OutIngressAnchor,
			FLayoutCommittedEndpointAnchor& OutEgressAnchor,
			TArray<FIntVector>& OutRouteCells)
		{
			OutRouteCells.Reset();
			int32 LowestAnchorLevel = MAX_int32;
			int32 HighestAnchorLevel = MIN_int32;
			for (const FLayoutCommittedEndpointAnchor& Anchor : CandidateCommitments)
			{
				LowestAnchorLevel = FMath::Min(LowestAnchorLevel, Anchor.LocalCell.Z);
				HighestAnchorLevel = FMath::Max(HighestAnchorLevel, Anchor.LocalCell.Z);
			}

			if (LowestAnchorLevel == MAX_int32
				|| HighestAnchorLevel == MIN_int32
				|| LowestAnchorLevel == HighestAnchorLevel
				|| ChildVerticalAccessLocalCells.IsEmpty())
			{
				return false;
			}

			TSet<FIntVector> TraversableCells;
			for (const FLayoutPlannedCell& PlannedCell : ChildPlannedCells)
			{
				if (IsTraversableIntentForNegotiatedVerticalRoute(PlannedCell.Intent))
				{
					TraversableCells.Add(PlannedCell.Cell);
				}
			}

			TArray<const FLayoutCommittedEndpointAnchor*> LowestAnchors;
			TArray<const FLayoutCommittedEndpointAnchor*> HighestAnchors;
			for (const FLayoutCommittedEndpointAnchor& Anchor : CandidateCommitments)
			{
				if (Anchor.LocalCell.Z == LowestAnchorLevel)
				{
					LowestAnchors.Add(&Anchor);
				}
				if (Anchor.LocalCell.Z == HighestAnchorLevel)
				{
					HighestAnchors.Add(&Anchor);
				}
			}

			auto IsAnchorOnChildPlannedCell = [&](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return ChildPlannedCells.ContainsByPredicate(
					[&](const FLayoutPlannedCell& PlannedCell)
					{
						return PlannedCell.Cell == Anchor.LocalCell;
					});
			};

			TSet<FIntVector> VerticalAccessCells(ChildVerticalAccessLocalCells);
			auto TryBuildRouteBetweenAnchors =
				[&](
					const FLayoutCommittedEndpointAnchor& CandidateIngressAnchor,
					const FLayoutCommittedEndpointAnchor& CandidateEgressAnchor,
					TArray<FIntVector>& CandidateRouteCells)
			{
				CandidateRouteCells.Reset();
				if (!IsAnchorOnChildPlannedCell(CandidateIngressAnchor)
					|| !IsAnchorOnChildPlannedCell(CandidateEgressAnchor))
				{
					return false;
				}

				TSet<FIntVector> CandidateRouteEligibleCells = TraversableCells;
				CandidateRouteEligibleCells.Add(CandidateIngressAnchor.LocalCell);
				CandidateRouteEligibleCells.Add(CandidateEgressAnchor.LocalCell);

				TArray<FIntVector> Frontier = {CandidateIngressAnchor.LocalCell};
				TSet<FIntVector> Visited = {CandidateIngressAnchor.LocalCell};
				TMap<FIntVector, FIntVector> PreviousByCell;
				bool bFoundRoute = false;
				while (!Frontier.IsEmpty())
				{
					const FIntVector CurrentCell = Frontier.Pop(EAllowShrinking::No);
					if (CurrentCell == CandidateEgressAnchor.LocalCell)
					{
						bFoundRoute = true;
						break;
					}

					static const ELayoutFaceDirection RouteDirections[] =
					{
						ELayoutFaceDirection::PosX,
						ELayoutFaceDirection::NegX,
						ELayoutFaceDirection::PosY,
						ELayoutFaceDirection::NegY,
						ELayoutFaceDirection::PosZ,
						ELayoutFaceDirection::NegZ
					};

					for (const ELayoutFaceDirection Direction : RouteDirections)
					{
						const FIntVector NeighborCell =
							CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (!CandidateRouteEligibleCells.Contains(NeighborCell)
							|| Visited.Contains(NeighborCell))
						{
							continue;
						}

						if ((Direction == ELayoutFaceDirection::PosZ
								|| Direction == ELayoutFaceDirection::NegZ)
							&& !VerticalAccessCells.Contains(CurrentCell)
							&& !VerticalAccessCells.Contains(NeighborCell))
						{
							continue;
						}

						Visited.Add(NeighborCell);
						PreviousByCell.Add(NeighborCell, CurrentCell);
						Frontier.Add(NeighborCell);
					}
				}

				if (!bFoundRoute)
				{
					return false;
				}

				for (FIntVector RouteCell = CandidateEgressAnchor.LocalCell;;)
				{
					CandidateRouteCells.Add(RouteCell);
					if (RouteCell == CandidateIngressAnchor.LocalCell)
					{
						break;
					}

					const FIntVector* PreviousCell = PreviousByCell.Find(RouteCell);
					if (PreviousCell == nullptr)
					{
						CandidateRouteCells.Reset();
						return false;
					}

					RouteCell = *PreviousCell;
				}
				Algo::Reverse(CandidateRouteCells);

				bool bRouteTouchesVerticalAccess = false;
				for (int32 RouteIndex = 0; RouteIndex < CandidateRouteCells.Num(); ++RouteIndex)
				{
					if (VerticalAccessCells.Contains(CandidateRouteCells[RouteIndex]))
					{
						bRouteTouchesVerticalAccess = true;
					}

					if (RouteIndex == 0)
					{
						continue;
					}

					const FIntVector& PreviousRouteCell =
						CandidateRouteCells[RouteIndex - 1];
					const FIntVector& CurrentRouteCell =
						CandidateRouteCells[RouteIndex];
					if (PreviousRouteCell.Z == CurrentRouteCell.Z)
					{
						continue;
					}

					if (!VerticalAccessCells.Contains(PreviousRouteCell)
						&& !VerticalAccessCells.Contains(CurrentRouteCell))
					{
						CandidateRouteCells.Reset();
						return false;
					}
				}

				if (!bRouteTouchesVerticalAccess)
				{
					CandidateRouteCells.Reset();
					return false;
				}

				return true;
			};

			for (const FLayoutCommittedEndpointAnchor* CandidateIngressAnchor :
				LowestAnchors)
			{
				if (CandidateIngressAnchor == nullptr)
				{
					continue;
				}

				for (const FLayoutCommittedEndpointAnchor* CandidateEgressAnchor :
					HighestAnchors)
				{
					if (CandidateEgressAnchor == nullptr)
					{
						continue;
					}

					if (TryBuildRouteBetweenAnchors(
						*CandidateIngressAnchor,
						*CandidateEgressAnchor,
						OutRouteCells))
					{
						OutIngressAnchor = *CandidateIngressAnchor;
						OutEgressAnchor = *CandidateEgressAnchor;
						return true;
					}
				}
			}

			return false;
		}

		static bool TrySelectNegotiatedHostVerticalAccessCapabilityProof(
			const FChildCapabilitySummary& ChildSummary,
			const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
			FLayoutCommittedEndpointAnchor& OutIngressAnchor,
			FLayoutCommittedEndpointAnchor& OutEgressAnchor,
			FLayoutId& OutAnchorPairId,
			TArray<int32>& OutSpanLevels)
		{
			OutAnchorPairId = NAME_None;
			OutSpanLevels.Reset();

			const FLayoutCommittedEndpointAnchor* LowestAnchor = nullptr;
			const FLayoutCommittedEndpointAnchor* HighestAnchor = nullptr;
			for (const FLayoutCommittedEndpointAnchor& Anchor : CandidateCommitments)
			{
				if (LowestAnchor == nullptr || Anchor.LocalCell.Z < LowestAnchor->LocalCell.Z)
				{
					LowestAnchor = &Anchor;
				}

				if (HighestAnchor == nullptr || Anchor.LocalCell.Z > HighestAnchor->LocalCell.Z)
				{
					HighestAnchor = &Anchor;
				}
			}

			if (LowestAnchor == nullptr
				|| HighestAnchor == nullptr
				|| LowestAnchor->LocalCell.Z == HighestAnchor->LocalCell.Z)
			{
				return false;
			}

			const TArray<TPair<int32, FLayoutChildCapabilityEndpoint>> LevelAwareEndpointOffers =
				CollectLevelAwareEndpointOffers(ChildSummary);
			const TPair<int32, FLayoutChildCapabilityEndpoint>* LowestEndpointOffer =
				LevelAwareEndpointOffers.FindByPredicate(
					[LowestAnchor](const TPair<int32, FLayoutChildCapabilityEndpoint>& Candidate)
					{
						return DoesEndpointOfferMatchCommitment(
							Candidate.Value,
							*LowestAnchor);
					});
			const TPair<int32, FLayoutChildCapabilityEndpoint>* HighestEndpointOffer =
				LevelAwareEndpointOffers.FindByPredicate(
					[HighestAnchor](const TPair<int32, FLayoutChildCapabilityEndpoint>& Candidate)
					{
						return DoesEndpointOfferMatchCommitment(
							Candidate.Value,
							*HighestAnchor);
					});
			if (LowestEndpointOffer == nullptr || HighestEndpointOffer == nullptr)
			{
				return false;
			}

			const FLayoutId LowestAnchorId =
				LowestEndpointOffer->Value.CapabilityId.IsNone()
					? LowestAnchor->CommitmentId
					: LowestEndpointOffer->Value.CapabilityId;
			const FLayoutId HighestAnchorId =
				HighestEndpointOffer->Value.CapabilityId.IsNone()
					? HighestAnchor->CommitmentId
					: HighestEndpointOffer->Value.CapabilityId;
			OutAnchorPairId = LowestAnchorId.LexicalLess(HighestAnchorId)
				? FLayoutId(*FString::Printf(TEXT("%s__%s"), *LowestAnchorId.ToString(), *HighestAnchorId.ToString()))
				: FLayoutId(*FString::Printf(TEXT("%s__%s"), *HighestAnchorId.ToString(), *LowestAnchorId.ToString()));

			const int32 LowerLevel = FMath::Min(
				LowestAnchor->LocalCell.Z,
				HighestAnchor->LocalCell.Z);
			const int32 UpperLevel = FMath::Max(
				LowestAnchor->LocalCell.Z,
				HighestAnchor->LocalCell.Z);
			for (int32 LevelIndex = LowerLevel; LevelIndex <= UpperLevel; ++LevelIndex)
			{
				const FChildLevelTraversalCapabilitySummary* TraversalSummary =
					ChildSummary.TraversalSummariesByLevel.FindByPredicate(
						[LevelIndex](const FChildLevelTraversalCapabilitySummary& Summary)
						{
							return Summary.LevelIndex == LevelIndex;
						});
				if (TraversalSummary == nullptr
					|| !TraversalSummary->bCanCarryHostVerticalAccess
					|| !TraversalSummary->ConnectableAnchorPairIds.Contains(
						OutAnchorPairId))
				{
					OutAnchorPairId = NAME_None;
					OutSpanLevels.Reset();
					return false;
				}

				OutSpanLevels.Add(LevelIndex);
			}

			OutIngressAnchor = *LowestAnchor;
			OutEgressAnchor = *HighestAnchor;
			return true;
		}

		static void PopulateNegotiatedHostVerticalAccessFields(
			const FLayoutRegionSolveRequest* RootRequest,
			const FNegotiationDemandPlan& DemandPlan,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet,
			FLayoutNegotiatedChildResponsibilityContract& InOutContract)
		{
			const bool bSupportsChildOwned =
				DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess;
			const bool bSupportsComposed =
				DemandPlan.ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess;
			if (!DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet
				|| (!bSupportsChildOwned && !bSupportsComposed))
			{
				return;
			}

			FLayoutCommittedEndpointAnchor IngressAnchor;
			FLayoutCommittedEndpointAnchor EgressAnchor;
			FLayoutCommittedEndpointAnchor RouteIngressAnchor;
			FLayoutCommittedEndpointAnchor RouteEgressAnchor;
			FLayoutId AnchorPairId = NAME_None;
			TArray<int32> SpanLevels;
			const bool bHasCapabilityProof = TrySelectNegotiatedHostVerticalAccessCapabilityProof(
				DemandPlan.ChildSummary,
				ContactSet.EndpointCommitments,
				IngressAnchor,
				EgressAnchor,
				AnchorPairId,
				SpanLevels);

			TArray<FIntVector> RouteCells;
			const bool bHasRouteProof = TrySelectNegotiatedHostVerticalAccessAnchors(
				ContactSet.EndpointCommitments,
				DemandPlan.ChildRequest.PlannedCells,
				DemandPlan.ChildSummary.VerticalAccessCells,
				RouteIngressAnchor,
				RouteEgressAnchor,
				RouteCells);

			if (!bHasCapabilityProof && !bHasRouteProof)
			{
				return;
			}

			if (!bHasCapabilityProof)
			{
				IngressAnchor = RouteIngressAnchor;
				EgressAnchor = RouteEgressAnchor;
				SpanLevels.Reset();
				TSet<int32> RouteLevels;
				for (const FIntVector& RouteCell : RouteCells)
				{
					RouteLevels.Add(RouteCell.Z);
				}
				RouteLevels.Add(IngressAnchor.LocalCell.Z);
				RouteLevels.Add(EgressAnchor.LocalCell.Z);
				for (const int32 LevelIndex : RouteLevels)
				{
					SpanLevels.Add(LevelIndex);
				}
				SpanLevels.Sort();
			}

			TArray<FIntVector> CountedParentProvidersOnContactComponent =
				CollectCountedParentProvidersForContactComponent(
					ParentSummary,
					ContactSet,
					DemandPlan.ReservedParentCells);
			CapFallbackCountedParentProvidersToAuthoredHostCount(
				RootRequest,
				ParentSummary,
				CountedParentProvidersOnContactComponent);
			TArray<FIntVector> RetainedRouteSupportVerticalAccessCells =
				CollectRouteSupportParentVerticalAccessCellsForContactComponent(
					ParentSummary,
					ContactSet,
					DemandPlan.ReservedParentCells);
			TArray<FIntVector> NegotiatedRouteNetworkCells;
			RetainedRouteSupportVerticalAccessCells =
				FilterCountedParentProvidersToNegotiatedRouteNetwork(
					RootRequest,
					ParentSummary,
					ContactSet,
					RetainedRouteSupportVerticalAccessCells,
					&NegotiatedRouteNetworkCells);
			SortNegotiationCells(CountedParentProvidersOnContactComponent);
			InOutContract.RetainedParentRouteSupportVerticalAccessCells =
				RetainedRouteSupportVerticalAccessCells;
			if (IsSingleCountedParentProviderOnlyLowerHostHandoff(
					InOutContract,
					ContactSet,
					CountedParentProvidersOnContactComponent,
					IngressAnchor,
					EgressAnchor,
					&ParentSummary,
					&NegotiatedRouteNetworkCells))
			{
				CountedParentProvidersOnContactComponent.Reset();
			}
			const bool bHasCountedParentProviders =
				!CountedParentProvidersOnContactComponent.IsEmpty();
			if (bHasCountedParentProviders)
			{
				if (!bSupportsComposed)
				{
					return;
				}

				InOutContract.HostVerticalAccessResponsibility =
					ELayoutNegotiatedHostVerticalAccessResponsibility::Composed;
				InOutContract.CountedParentProviderCount =
					CountedParentProvidersOnContactComponent.Num();
				InOutContract.CountedParentVerticalAccessCells =
					CountedParentProvidersOnContactComponent;
			}
			else
			{
				if (!bSupportsChildOwned)
				{
					return;
				}

				InOutContract.HostVerticalAccessResponsibility =
					ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
				InOutContract.CountedParentProviderCount = 0;
				InOutContract.CountedParentVerticalAccessCells.Reset();
			}

			InOutContract.CountedChildProviderRegionDebugPaths = {
				DemandPlan.ChildRegionDebugPath
			};
			if (DemandPlan.UnlockingAscentFrontierId.IsSet())
			{
				FLayoutNegotiatedHostVerticalAccessFrontierResponsibility& FrontierResponsibility =
					InOutContract.HostVerticalAccessFrontierResponsibilities.AddDefaulted_GetRef();
				FrontierResponsibility.TerrainAscentFrontierId =
					DemandPlan.UnlockingAscentFrontierId.GetValue();
				FrontierResponsibility.HostVerticalAccessResponsibility =
					InOutContract.HostVerticalAccessResponsibility;
				FrontierResponsibility.CountedParentProviderCount =
					InOutContract.CountedParentProviderCount;
				FrontierResponsibility.CountedParentVerticalAccessCells =
					InOutContract.CountedParentVerticalAccessCells;
				FrontierResponsibility.RetainedParentRouteSupportVerticalAccessCells =
					InOutContract.RetainedParentRouteSupportVerticalAccessCells;
				FrontierResponsibility.CountedChildProviderRegionDebugPaths =
					InOutContract.CountedChildProviderRegionDebugPaths;
			}
			InOutContract.bHasRequiredHostIngressAnchor = true;
			InOutContract.RequiredHostIngressAnchor = IngressAnchor;
			InOutContract.bHasRequiredHostEgressAnchor = true;
			InOutContract.RequiredHostEgressAnchor = EgressAnchor;
			InOutContract.RequiredChildGenerallyConnectableAnchorPairId = AnchorPairId;
			InOutContract.RequiredChildInternalVerticalSpanLevels = MoveTemp(SpanLevels);
			InOutContract.RequiredChildInternalVerticalRouteCells = MoveTemp(RouteCells);
		}
	}

		void NormalizeNegotiatedContactSet(FContactBackedPlacementFamily& InOutContactSet)
		{
		InOutContactSet.EndpointCommitments.Sort([](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
		{
			return Left.CommitmentId.LexicalLess(Right.CommitmentId);
		});
		InOutContactSet.EndpointCommitments.SetNum(Algo::Unique(
			InOutContactSet.EndpointCommitments,
			[](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
			{
				return AreCommittedEndpointAnchorsEquivalent(Left, Right);
			}));

		InOutContactSet.ParentContactCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		});
			InOutContactSet.ParentContactCells.SetNum(Algo::Unique(InOutContactSet.ParentContactCells));

			SortNegotiationParentContactFaces(InOutContactSet.ParentContactFaces);

		InOutContactSet.AllCommittedContactTraversalAnchors.Sort([](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
		{
			return Left.Cell.Z != Right.Cell.Z ? Left.Cell.Z < Right.Cell.Z
				: Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y
				: Left.Cell.X != Right.Cell.X ? Left.Cell.X < Right.Cell.X
				: Left.TraversalChannel.ToString() < Right.TraversalChannel.ToString();
		});
		InOutContactSet.AllCommittedContactTraversalAnchors.SetNum(Algo::Unique(
			InOutContactSet.AllCommittedContactTraversalAnchors,
			[](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
			{
				return AreCommittedTraversalAnchorsEquivalent(Left, Right);
			}));

		InOutContactSet.ParentTraversalIngressAnchors.Sort([](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
		{
			return Left.Cell.Z != Right.Cell.Z ? Left.Cell.Z < Right.Cell.Z
				: Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y
				: Left.Cell.X != Right.Cell.X ? Left.Cell.X < Right.Cell.X
				: Left.TraversalChannel.ToString() < Right.TraversalChannel.ToString();
		});
		InOutContactSet.ParentTraversalIngressAnchors.SetNum(Algo::Unique(
			InOutContactSet.ParentTraversalIngressAnchors,
			[](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
			{
				return AreCommittedTraversalAnchorsEquivalent(Left, Right);
			}));
	}

	TArray<FParentContactCapabilityCandidate> FLayoutResponsibilityNegotiator::CollectParentContactCapabilityCandidates(
		const FChildCapabilitySummary& ChildSummary,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		TArray<FParentContactCapabilityCandidate> Candidates;
		const TArray<TPair<int32, FLayoutChildCapabilityEndpoint>> EndpointOffersByLevel =
			CollectLevelAwareEndpointOffers(ChildSummary);
		for (const TPair<int32, FLayoutChildCapabilityEndpoint>& EndpointOfferByLevel : EndpointOffersByLevel)
		{
			const int32 LevelIndex = EndpointOfferByLevel.Key;
			const FLayoutChildCapabilityEndpoint& EndpointOffer = EndpointOfferByLevel.Value;
			const ELayoutFaceDirection RequiredParentFaceDirection =
				FLayoutDirectionUtils::GetOpposite(EndpointOffer.FaceDirection);
			const TArray<FPlacementCapabilityBundle> MatchingBundles =
				FindPlacementBundlesForEndpoint(ChildSummary, EndpointOffer, LevelIndex);

			for (const TPair<FIntVector, TArray<ELayoutFaceDirection>>& ParentCellFaces :
				ParentSummary.RootConnectedTraversableFacesByCell)
			{
				if (ParentCellFaces.Key.Z != LevelIndex
					|| !ParentCellFaces.Value.Contains(RequiredParentFaceDirection))
				{
					continue;
				}

				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(ParentCellFaces.Key);
				for (const FPlacementCapabilityBundle& PlacementBundle : MatchingBundles)
				{
					FParentContactCapabilityCandidate& Candidate = Candidates.AddDefaulted_GetRef();
					Candidate.EndpointCommitment = BuildCommittedEndpointAnchor(EndpointOffer);
					Candidate.ParentContactCell = ParentCellFaces.Key;
					Candidate.ParentContactFaceDirection = RequiredParentFaceDirection;
					Candidate.LevelIndex = LevelIndex;
					Candidate.ParentComponentId = ParentComponentId != nullptr ? *ParentComponentId : INDEX_NONE;
					Candidate.AdjacencyClassId = ResolveAdjacencyClassId(ParentSummary, ParentCellFaces.Key);
					Candidate.PlacementBundle = PlacementBundle;
					Candidate.AllCommittedContactTraversalAnchors =
						BuildCommittedTraversalAnchors(EndpointOffer, ParentCellFaces.Key);
					Candidate.ParentSupportScore = ResolveParentSupportScore(ParentSummary, ParentCellFaces.Key);
					Candidate.FamilyId = BuildContactFamilyId(
						EndpointOffer,
						ParentCellFaces.Key,
						LevelIndex,
						Candidate.ParentComponentId,
						Candidate.AdjacencyClassId,
						PlacementBundle);
				}
			}
		}

		Candidates.Sort([](const FParentContactCapabilityCandidate& Left, const FParentContactCapabilityCandidate& Right)
		{
			return Left.FamilyId.LexicalLess(Right.FamilyId);
		});
		return Candidates;
	}

	TArray<FContactBackedPlacementFamily> FLayoutResponsibilityNegotiator::CollectCapabilityBackedContactSets(
		const FChildCapabilitySummary& ChildSummary,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		TArray<FContactBackedPlacementFamily> ContactSets;
		for (const FParentContactCapabilityCandidate& Candidate :
			FLayoutResponsibilityNegotiator::CollectParentContactCapabilityCandidates(ChildSummary, ParentSummary))
		{
			FContactBackedPlacementFamily* ExistingFamily = ContactSets.FindByPredicate(
				[&Candidate](const FContactBackedPlacementFamily& ContactSet)
				{
					return ContactSet.FamilyId == Candidate.FamilyId;
				});

			if (ExistingFamily == nullptr)
			{
				FContactBackedPlacementFamily& ContactSet = ContactSets.AddDefaulted_GetRef();
				ContactSet.FamilyId = Candidate.FamilyId;
				ContactSet.PlacementBundle = Candidate.PlacementBundle;
				ContactSet.ParentComponentId = Candidate.ParentComponentId;
				ContactSet.AdjacencyClassId = Candidate.AdjacencyClassId;
				ContactSet.bAllowsChildTraversalBridge =
					!ChildSummary.GenerallyConnectableAnchorPairIds.IsEmpty();
				ExistingFamily = &ContactSet;
			}

			ExistingFamily->EndpointCommitments.Add(Candidate.EndpointCommitment);
			ExistingFamily->ParentContactCells.Add(Candidate.ParentContactCell);
			ExistingFamily->ParentContactFaces.Add(
				BuildCommittedParentContactFace(
					Candidate.ParentContactCell,
					Candidate.ParentContactFaceDirection));
			ExistingFamily->AllCommittedContactTraversalAnchors.Append(
				Candidate.AllCommittedContactTraversalAnchors);
		}

		for (FContactBackedPlacementFamily& ContactSet : ContactSets)
		{
			NormalizeNegotiatedContactSet(ContactSet);
			FLayoutResponsibilityNegotiator::SelectParentTraversalIngressSubset(ParentSummary, ContactSet);
		}

		ContactSets.Sort([](const FContactBackedPlacementFamily& Left, const FContactBackedPlacementFamily& Right)
		{
			return Left.FamilyId.LexicalLess(Right.FamilyId);
		});
		return ContactSets;
	}

	TArray<FContactBackedPlacementFamily> FLayoutResponsibilityNegotiator::BuildPlacementFamilies(
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		if (!IsNegotiationDemandActiveForCurrentTerrainStage(DemandPlan))
		{
			return {};
		}

		const bool bRequiresMultiEntry =
			DemandPlan.FeatureFlags.bRequiresMultiEntryContactSet;
		const bool bRequiresMultiLevel =
			DemandPlan.FeatureFlags.bRequiresMultiLevelAnchorSet;
		if (!bRequiresMultiEntry && !bRequiresMultiLevel)
		{
			return FLayoutResponsibilityNegotiator::CollectCapabilityBackedContactSets(DemandPlan.ChildSummary, ParentSummary);
		}

		int32 MinimumRequiredEntryCount = 1;
		int32 MaximumRequiredEntryCount = 1;
		ResolveNegotiatedEntryCountRange(
			DemandPlan.ChildRequest.ProfileSnapshot,
			MinimumRequiredEntryCount,
			MaximumRequiredEntryCount);
		const TArray<FParentContactCapabilityCandidate> Candidates =
			FLayoutResponsibilityNegotiator::CollectParentContactCapabilityCandidates(DemandPlan.ChildSummary, ParentSummary);
		TArray<FContactBackedPlacementFamily> ContactFamilies;

		TArray<FLayoutId> PartitionIds;
		TMap<FLayoutId, TArray<FParentContactCapabilityCandidate>> CandidatesByPartitionId;
		for (const FParentContactCapabilityCandidate& Candidate : Candidates)
		{
			const FLayoutId PartitionId = BuildPlacementFamilyPartitionId(
				Candidate.PlacementBundle,
				Candidate.ParentComponentId,
				Candidate.AdjacencyClassId);
			if (!CandidatesByPartitionId.Contains(PartitionId))
			{
				PartitionIds.Add(PartitionId);
			}
			CandidatesByPartitionId.FindOrAdd(PartitionId).Add(Candidate);
		}
		PartitionIds.Sort([](const FLayoutId Left, const FLayoutId Right)
		{
			return Left.LexicalLess(Right);
		});

		for (const FLayoutId PartitionId : PartitionIds)
		{
			const TArray<FParentContactCapabilityCandidate>* PartitionCandidates =
				CandidatesByPartitionId.Find(PartitionId);
			if (PartitionCandidates == nullptr || PartitionCandidates->IsEmpty())
			{
				continue;
			}

			TArray<FLayoutId> EndpointGroupIds;
			TMap<FLayoutId, TArray<FParentContactCapabilityCandidate>> CandidatesByEndpointGroupId;
			for (const FParentContactCapabilityCandidate& Candidate : *PartitionCandidates)
			{
				const FLayoutId EndpointGroupId = BuildEndpointGroupId(Candidate.EndpointCommitment);
				if (!CandidatesByEndpointGroupId.Contains(EndpointGroupId))
				{
					EndpointGroupIds.Add(EndpointGroupId);
				}
				CandidatesByEndpointGroupId.FindOrAdd(EndpointGroupId).Add(Candidate);
			}
			EndpointGroupIds.Sort([](const FLayoutId Left, const FLayoutId Right)
			{
				return Left.LexicalLess(Right);
			});

			TArray<int32> LevelGroupIds;
			for (const FParentContactCapabilityCandidate& Candidate : *PartitionCandidates)
			{
				LevelGroupIds.AddUnique(Candidate.LevelIndex);
			}
			LevelGroupIds.Sort();

			const auto AppendEndpointGroupedFamilies =
				[&ContactFamilies,
				 &MinimumRequiredEntryCount,
				 &MaximumRequiredEntryCount,
				 &DemandPlan,
				 &ParentSummary,
				 &EndpointGroupIds,
				 &CandidatesByEndpointGroupId,
				 &LevelGroupIds,
				 &PartitionId](
				 const bool bRequireAllLevelCoverage)
				{
					if (EndpointGroupIds.Num() < MinimumRequiredEntryCount)
					{
						return;
					}

					const int32 MaximumSelectedEntryCount = FMath::Min(
						MaximumRequiredEntryCount,
						EndpointGroupIds.Num());
					for (int32 SelectedEntryCount = MinimumRequiredEntryCount;
						SelectedEntryCount <= MaximumSelectedEntryCount;
						++SelectedEntryCount)
					{
						TArray<TArray<int32>> EndpointGroupSelections;
						if (EndpointGroupIds.Num() == SelectedEntryCount)
						{
							TArray<int32>& GroupSelection =
								EndpointGroupSelections.AddDefaulted_GetRef();
							for (int32 GroupIndex = 0; GroupIndex < EndpointGroupIds.Num(); ++GroupIndex)
							{
								GroupSelection.Add(GroupIndex);
							}
						}
						else
						{
							TArray<int32> CurrentGroupSelection;
							CollectGroupIndexSelectionsRecursive(
								EndpointGroupIds.Num(),
								SelectedEntryCount,
								0,
								CurrentGroupSelection,
								EndpointGroupSelections);
						}

						for (const TArray<int32>& EndpointGroupSelection : EndpointGroupSelections)
						{
							TArray<TArray<FParentContactCapabilityCandidate>> CandidateGroups;
							CandidateGroups.Reserve(EndpointGroupSelection.Num());
							for (const int32 GroupIndex : EndpointGroupSelection)
							{
								TArray<FParentContactCapabilityCandidate>& CandidateGroup =
									CandidateGroups.AddDefaulted_GetRef();
								CandidateGroup =
									CandidatesByEndpointGroupId.FindChecked(EndpointGroupIds[GroupIndex]);
								CandidateGroup.Sort([](
									const FParentContactCapabilityCandidate& Left,
									const FParentContactCapabilityCandidate& Right)
								{
									if (Left.ParentSupportScore != Right.ParentSupportScore)
									{
										return Left.ParentSupportScore > Right.ParentSupportScore;
									}
									if (Left.ParentContactCell != Right.ParentContactCell)
									{
										return LexicalLess(Left.ParentContactCell, Right.ParentContactCell);
									}
									return Left.FamilyId.LexicalLess(Right.FamilyId);
								});
							}

							TArray<TArray<FParentContactCapabilityCandidate>> CandidateSelections;
							TArray<FParentContactCapabilityCandidate> CurrentSelection;
							CollectCandidateSelectionsRecursive(
								CandidateGroups,
								0,
								CurrentSelection,
								CandidateSelections);

							for (const TArray<FParentContactCapabilityCandidate>& CandidateSelection : CandidateSelections)
							{
								if (CandidateSelection.Num() != SelectedEntryCount)
								{
									continue;
								}

								if (bRequireAllLevelCoverage
									&& !DoesCandidateSelectionCoverAllLevels(CandidateSelection, LevelGroupIds))
								{
									continue;
								}

								ContactFamilies.Add(BuildPlacementFamilyFromSelectedCandidates(
									BuildSetBackedPlacementFamilyId(PartitionId, CandidateSelection),
									CandidateSelection,
									DemandPlan.FeatureFlags.bAllowsChildTraversalBridge,
									ParentSummary));
							}
						}
					}
				};

			if (bRequiresMultiEntry && bRequiresMultiLevel)
			{
				AppendEndpointGroupedFamilies(true);
				continue;
			}

			if (bRequiresMultiLevel)
			{
				TMap<int32, TArray<FParentContactCapabilityCandidate>> CandidatesByLevelId;
				for (const FParentContactCapabilityCandidate& Candidate : *PartitionCandidates)
				{
					if (!CandidatesByLevelId.Contains(Candidate.LevelIndex))
					{
						LevelGroupIds.Add(Candidate.LevelIndex);
					}
					CandidatesByLevelId.FindOrAdd(Candidate.LevelIndex).Add(Candidate);
				}
				LevelGroupIds.Sort();

				if (LevelGroupIds.Num() < 2)
				{
					continue;
				}

				TArray<TArray<FParentContactCapabilityCandidate>> CandidateGroups;
				CandidateGroups.Reserve(LevelGroupIds.Num());
				for (const int32 LevelGroupId : LevelGroupIds)
				{
					TArray<FParentContactCapabilityCandidate>& CandidateGroup =
						CandidateGroups.AddDefaulted_GetRef();
					CandidateGroup = CandidatesByLevelId.FindChecked(LevelGroupId);
					CandidateGroup.Sort([](
						const FParentContactCapabilityCandidate& Left,
						const FParentContactCapabilityCandidate& Right)
					{
						if (Left.ParentSupportScore != Right.ParentSupportScore)
						{
							return Left.ParentSupportScore > Right.ParentSupportScore;
						}
						if (Left.ParentContactCell != Right.ParentContactCell)
						{
							return LexicalLess(Left.ParentContactCell, Right.ParentContactCell);
						}
						return Left.FamilyId.LexicalLess(Right.FamilyId);
					});
				}

				TArray<TArray<FParentContactCapabilityCandidate>> CandidateSelections;
				TArray<FParentContactCapabilityCandidate> CurrentSelection;
				CollectCandidateSelectionsRecursive(
					CandidateGroups,
					0,
					CurrentSelection,
					CandidateSelections);

				for (const TArray<FParentContactCapabilityCandidate>& CandidateSelection : CandidateSelections)
				{
					if (CandidateSelection.Num() != LevelGroupIds.Num())
					{
						continue;
					}

					ContactFamilies.Add(BuildPlacementFamilyFromSelectedCandidates(
						BuildSetBackedPlacementFamilyId(PartitionId, CandidateSelection),
						CandidateSelection,
						DemandPlan.FeatureFlags.bAllowsChildTraversalBridge,
						ParentSummary));
				}
				continue;
			}

			AppendEndpointGroupedFamilies(false);
		}

		ContactFamilies.Sort([](const FContactBackedPlacementFamily& Left, const FContactBackedPlacementFamily& Right)
		{
			return Left.FamilyId.LexicalLess(Right.FamilyId);
		});
		return ContactFamilies;
	}

	void FLayoutResponsibilityNegotiator::SelectParentTraversalIngressSubset(
		const FResidualParentCapabilitySummary& ParentSummary,
		FContactBackedPlacementFamily& InOutContactSet)
	{
		TArray<FLayoutCommittedTraversalAnchor> EligibleAnchors =
			InOutContactSet.AllCommittedContactTraversalAnchors.FilterByPredicate(
				[&ParentSummary, &InOutContactSet](const FLayoutCommittedTraversalAnchor& TraversalAnchor)
				{
					if (!ParentSummary.RootConnectedTraversableCells.Contains(TraversalAnchor.Cell))
					{
						return false;
					}

					if (InOutContactSet.ParentComponentId == INDEX_NONE)
					{
						return true;
					}

					const int32* ParentComponentId =
						ParentSummary.RootConnectedComponentIdByCell.Find(TraversalAnchor.Cell);
					return ParentComponentId != nullptr
						&& *ParentComponentId == InOutContactSet.ParentComponentId;
				});
		EligibleAnchors.Sort([&ParentSummary](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
		{
			const bool bLeftCountedProvider =
				GetRetainedParentRouteSupportCells(ParentSummary).Contains(Left.Cell);
			const bool bRightCountedProvider =
				GetRetainedParentRouteSupportCells(ParentSummary).Contains(Right.Cell);
			if (bLeftCountedProvider != bRightCountedProvider)
			{
				return bLeftCountedProvider && !bRightCountedProvider;
			}
			if (Left.TraversalChannel != Right.TraversalChannel)
			{
				return Left.TraversalChannel.ToString() < Right.TraversalChannel.ToString();
			}
			return LexicalLess(Left.Cell, Right.Cell);
		});

		InOutContactSet.ParentTraversalIngressAnchors.Reset();
		TSet<FGameplayTag> ClaimedTraversalChannels;
		for (const FLayoutCommittedTraversalAnchor& EligibleAnchor : EligibleAnchors)
		{
			if (ClaimedTraversalChannels.Contains(EligibleAnchor.TraversalChannel))
			{
				continue;
			}

			InOutContactSet.ParentTraversalIngressAnchors.Add(EligibleAnchor);
			ClaimedTraversalChannels.Add(EligibleAnchor.TraversalChannel);
		}

		if (InOutContactSet.ParentTraversalIngressAnchors.IsEmpty()
			&& InOutContactSet.AllCommittedContactTraversalAnchors.Num() == 1)
		{
			InOutContactSet.ParentTraversalIngressAnchors =
				InOutContactSet.AllCommittedContactTraversalAnchors;
		}
	}

		FNegotiatedResponsibilitySet FLayoutResponsibilityNegotiator::BuildResponsibilitySet(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FNegotiationDemandPlan& DemandPlan,
			const FResidualParentCapabilitySummary& ParentSummary,
		const FContactBackedPlacementFamily& ContactSet)
	{
		FNegotiatedResponsibilitySet ResponsibilitySet;
		ResponsibilitySet.ContactSet = ContactSet;
		FLayoutNegotiatedChildResponsibilityContract& Contract =
			ResponsibilitySet.ResponsibilityContract;
		Contract.ParentRegionDebugPath =
			DemandPlan.ChildRequest.SourceParentRegionDebugPath;
		Contract.ChildRegionDebugPath =
			DemandPlan.ChildRegionDebugPath;
		Contract.CountedParentVerticalAccessCells =
			CollectCountedParentProvidersForContactComponent(
				ParentSummary,
				ContactSet,
				DemandPlan.ReservedParentCells);
		CapFallbackCountedParentProvidersToAuthoredHostCount(
			&SolveContext.RootRequest,
			ParentSummary,
			Contract.CountedParentVerticalAccessCells);
		Contract.CountedParentProviderCount =
			Contract.CountedParentVerticalAccessCells.Num();
		Contract.RequiredHostProviderCount =
			ResolveRequiredHostProviderCount(SolveContext);
		Contract.bRequiresExactHostProviderCount =
			ResolveRequiresExactHostProviderCount(SolveContext);
		Contract.HostVerticalAccessResponsibility =
			ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;

		for (const FIntVector& ReservedParentCell : DemandPlan.ReservedParentCells)
		{
			FindOrAddNegotiatedLevelCellSetForNegotiation(
				Contract.ReplacementVolumeByLevel,
				ReservedParentCell.Z).Cells.Add(ReservedParentCell);
		}

		const TSet<FIntVector> ReservedParentCellSet(
			DemandPlan.ReservedParentCells);

		for (const FIntVector& ParentContactCell : ContactSet.ParentContactCells)
		{
			if (ReservedParentCellSet.Contains(ParentContactCell))
			{
				continue;
			}

			FindOrAddNegotiatedLevelCellSetForNegotiation(
				Contract.RetainedParentShellCellsByLevel,
				ParentContactCell.Z).Cells.Add(ParentContactCell);
		}

		// Level-aware retained parent shell and proof-only host-ascent:
		// SharedParentChildFaces from the child capability summary must surface as
		// both retained shell and proof-only host-ascent metadata so L1+ seam evidence
		// survives contract freeze. Direct parent contact cells cover only the
		// negotiated L0 surface; SharedParentChildFaces carry L1+ parent/child
		// adjacency needed for level-aware seam, retained-shell, and host-ascent
		// evidence through contract freeze per RecursiveNegotiationDesign step 5.
		// Proof-only host-ascent evidence is kept separate from retained shell so it
		// does not become a direct child endpoint obligation.
		for (const FSharedParentChildFace& SharedFace : DemandPlan.ChildSummary.SharedParentChildFaces)
		{
			if (SharedFace.InterfaceFamily != LayoutGameplayTags::InterfacePartitionSolid
				&& SharedFace.InterfaceFamily != LayoutGameplayTags::InterfacePartitionDoor)
			{
				continue;
			}
			if (ReservedParentCellSet.Contains(SharedFace.ParentCell))
			{
				continue;
			}

			FindOrAddNegotiatedLevelCellSetForNegotiation(
				Contract.RetainedParentShellCellsByLevel,
				SharedFace.ParentCell.Z).Cells.AddUnique(SharedFace.ParentCell);
			FindOrAddNegotiatedLevelCellSetForNegotiation(
				Contract.ProofOnlyHostAscentParentShellCellsByLevel,
				SharedFace.ParentCell.Z).Cells.AddUnique(SharedFace.ParentCell);
		}

		for (const FLayoutCommittedEndpointAnchor& EndpointCommitment : ContactSet.EndpointCommitments)
		{
			FindOrAddNegotiatedLevelInterfaceContractForNegotiation(
				Contract.CommittedParentChildInterfacesByLevel,
				EndpointCommitment.LocalCell.Z).EndpointAnchors.Add(EndpointCommitment);
		}

		const TArray<FLayoutCommittedTraversalAnchor>& TraversalAnchors =
			ContactSet.ParentTraversalIngressAnchors.IsEmpty()
				? ContactSet.AllCommittedContactTraversalAnchors
				: ContactSet.ParentTraversalIngressAnchors;
		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : TraversalAnchors)
		{
			FindOrAddNegotiatedLevelInterfaceContractForNegotiation(
				Contract.CommittedParentChildInterfacesByLevel,
				TraversalAnchor.Cell.Z).TraversalAnchors.Add(TraversalAnchor);
		}

		PopulateNegotiatedHostVerticalAccessFields(
			&SolveContext.RootRequest,
			DemandPlan,
			ParentSummary,
			ContactSet,
			Contract);

		NormalizeNegotiatedResponsibilityContract(Contract);
		return ResponsibilitySet;
	}

	FNegotiatedChildCapabilityWitness BuildNegotiatedChildCapabilityWitness(
		const FNegotiationDemandPlan& DemandPlan,
		const FContactBackedPlacementFamily& ContactFamily,
		const FLayoutNegotiatedChildResponsibilityContract& ResponsibilityContract)
	{
		FNegotiatedChildCapabilityWitness Witness;
		Witness.SourceContentEntryId = DemandPlan.ChildRequest.SourceContentEntryId;
		Witness.ChildRegionDebugPath = DemandPlan.ChildRegionDebugPath;
		for (const FLayoutCommittedEndpointAnchor& Anchor : ContactFamily.EndpointCommitments)
		{
			TArray<FLayoutId> MatchingCapabilityIds;
			for (const FLayoutChildCapabilityEndpoint& Capability : DemandPlan.ChildSummary.CapabilityEnvelope.EndpointCapabilities)
			{
				if (Capability.LocalCell == Anchor.LocalCell
					&& Capability.FaceDirection == Anchor.FaceDirection
					&& Capability.ConnectionTag == Anchor.ConnectionTag)
				{
					MatchingCapabilityIds.AddUnique(Capability.CapabilityId);
				}
			}
			if (MatchingCapabilityIds.Num() == 1)
			{
				Witness.SelectedEndpointCapabilityIds.AddUnique(MatchingCapabilityIds[0]);
			}
		}

		if (!ResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId.IsNone())
		{
			// Local project-specific writer seam: this is the first exact negotiation-time vertical selection id currently exposed by the responsibility contract.
			Witness.SelectedVerticalCapabilityIds.AddUnique(ResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId);
		}

		for (const FLayoutNegotiatedLevelSeamSet& SeamSet : ResponsibilityContract.CommittedSiblingInterfacesByLevel)
		{
			for (const FLayoutPartitionSeamRecord& Seam : SeamSet.Seams)
			{
				TArray<FLayoutId> MatchingSeamCapabilityIds;
				for (const FLayoutChildCapabilitySeam& Capability : DemandPlan.ChildSummary.CapabilityEnvelope.SeamCapabilities)
				{
					const bool bChildOwnsSeam = Seam.OwnerRegionDebugPath == DemandPlan.ChildRegionDebugPath
						&& Capability.bCanOwnSeam
						&& Capability.FaceDirection == Seam.OwnerFaceDirection;
					const bool bChildAcceptsSeam = Seam.PassiveRegionDebugPath == DemandPlan.ChildRegionDebugPath
						&& Capability.bCanAcceptSeam
						&& Capability.FaceDirection == Seam.PassiveFaceDirection;
					if (Capability.InterfaceFamily == Seam.InterfaceFamily
						&& (bChildOwnsSeam || bChildAcceptsSeam))
					{
						MatchingSeamCapabilityIds.AddUnique(Capability.CapabilityId);
					}
				}
				if (MatchingSeamCapabilityIds.Num() == 1 && Witness.SelectedSeamCapabilityId.IsNone())
				{
					Witness.SelectedSeamCapabilityId = MatchingSeamCapabilityIds[0];
					Witness.SelectedSeamWitnessId = Seam.SeamId;
				}
			}
		}

		// Local project-specific writer seam: select traversal capability ids from
		// the child capability summary where one connectable anchor pair is available
		// per level. Full traversal-witness selection waits on the authoritative
		// parent-branch certificate writer.
		for (const FChildLevelTraversalCapabilitySummary& TraversalSummary :
			DemandPlan.ChildSummary.TraversalSummariesByLevel)
		{
			if (TraversalSummary.ConnectableAnchorPairIds.Num() == 1)
			{
				Witness.SelectedTraversalCapabilityIds.AddUnique(
					TraversalSummary.ConnectableAnchorPairIds[0]);
			}
		}

		Witness.SelectedEndpointCapabilityIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		Witness.SelectedVerticalCapabilityIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		return Witness;
	}

	FNegotiatedChildObligationWitness BuildNegotiatedChildObligationWitness(
		const FNegotiationDemandPlan& DemandPlan,
		const FLayoutNegotiatedChildResponsibilityContract& ResponsibilityContract)
	{
		FNegotiatedChildObligationWitness Witness;
		for (const FLayoutNegotiatedLevelSeamSet& SeamSet : ResponsibilityContract.CommittedSiblingInterfacesByLevel)
		{
			for (const FLayoutPartitionSeamRecord& Seam : SeamSet.Seams)
			{
				if (!Seam.SeamId.IsNone()
					&& (Seam.OwnerRegionDebugPath == DemandPlan.ChildRegionDebugPath
						|| Seam.PassiveRegionDebugPath == DemandPlan.ChildRegionDebugPath))
				{
					Witness.DelegatedSeamWitnessIds.AddUnique(Seam.SeamId);
				}
			}
		}
		if (!ResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId.IsNone())
		{
			Witness.DelegatedHostVerticalAccessWitnessIds.AddUnique(
				ResponsibilityContract.RequiredChildGenerallyConnectableAnchorPairId);
		}
		for (const FLayoutNegotiatedHostVerticalAccessFrontierResponsibility& Frontier :
			ResponsibilityContract.HostVerticalAccessFrontierResponsibilities)
		{
			if (Frontier.TerrainAscentFrontierId != INDEX_NONE)
			{
				Witness.DelegatedHostVerticalAccessWitnessIds.AddUnique(FLayoutId(*FString::Printf(
					TEXT("HostVerticalAccess.Frontier.%d"),
					Frontier.TerrainAscentFrontierId)));
			}
		}
		Witness.DelegatedSeamWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		Witness.DelegatedHostVerticalAccessWitnessIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		return Witness;
	}

	uint64 HashCertificateNameArray(uint64 InHash, const TArray<FLayoutId>& Names)
	{
		uint64 Hash = InHash;
		for (const FLayoutId& Name : Names)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Name));
		}
		return Hash;
	}

	uint64 BuildParentBranchCertificateContractHash(const FNegotiatedDemandResult& Result)
	{
		uint64 Hash = GetTypeHash(FLayoutId(*Result.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath));
		Hash = HashCombineFast(Hash, GetTypeHash(FLayoutId(*Result.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath)));
		Hash = HashCombineFast(Hash, GetTypeHash(Result.ChildRequest.SourceContentEntryId));
		Hash = HashCertificateNameArray(Hash, Result.CapabilityWitness.SelectedEndpointCapabilityIds);
		Hash = HashCertificateNameArray(Hash, Result.CapabilityWitness.SelectedTraversalCapabilityIds);
		Hash = HashCertificateNameArray(Hash, Result.CapabilityWitness.SelectedVerticalCapabilityIds);
		Hash = HashCombineFast(Hash, GetTypeHash(Result.CapabilityWitness.SelectedClosureSpanCapabilityId));
		Hash = HashCombineFast(Hash, GetTypeHash(Result.CapabilityWitness.SelectedSeamCapabilityId));
		Hash = HashCombineFast(Hash, GetTypeHash(Result.CapabilityWitness.SelectedSeamWitnessId));
		Hash = HashCertificateNameArray(Hash, Result.ObligationWitness.DelegatedFeatureRequirementIds);
		Hash = HashCertificateNameArray(Hash, Result.ObligationWitness.DelegatedClosureRequirementIds);
		Hash = HashCertificateNameArray(Hash, Result.ObligationWitness.DelegatedSeamWitnessIds);
		Hash = HashCertificateNameArray(Hash, Result.ObligationWitness.DelegatedJunctionWitnessIds);
		Hash = HashCertificateNameArray(Hash, Result.ObligationWitness.DelegatedHostVerticalAccessWitnessIds);
		return Hash != 0 ? Hash : 1;
	}

	bool TryBuildParentBranchCertificateFromNegotiatedResult(
		const FNegotiatedDemandResult& Result,
		FLayoutProducedParentBranchCertificateArtifact& OutCertificate,
		FString& OutFailureReason)
	{
		FLayoutParentBranchCertificateInput CertificateInput;
		CertificateInput.ParentContractId = FLayoutId(*FString::Printf(
			TEXT("ParentContract.%s.%s"),
			*Result.ResponsibilitySet.ResponsibilityContract.ParentRegionDebugPath,
			*Result.ResponsibilitySet.ResponsibilityContract.ChildRegionDebugPath));
		CertificateInput.ParentContractHash = BuildParentBranchCertificateContractHash(Result);
		CertificateInput.BranchId = FLayoutId(*FString::Printf(TEXT("ParentBranch.%s"), *Result.ChildRegionDebugPath));
		CertificateInput.ChildScoutResultId = FLayoutId(*FString::Printf(TEXT("ChildBranch.%s"), *Result.ChildRegionDebugPath));
		CertificateInput.ArtifactId = FLayoutId(*FString::Printf(TEXT("ParentBranchCertificate.%s"), *Result.ChildRegionDebugPath));
		CertificateInput.StableChildId = Result.ChildRegionDebugPath;
		CertificateInput.SourceContentEntryId = Result.ChildRequest.SourceContentEntryId;
		CertificateInput.SelectedClosureSpanCapabilityId = Result.CapabilityWitness.SelectedClosureSpanCapabilityId;
		CertificateInput.SelectedEndpointCapabilityIds = Result.CapabilityWitness.SelectedEndpointCapabilityIds;
		CertificateInput.SelectedVerticalCapabilityIds = Result.CapabilityWitness.SelectedVerticalCapabilityIds;
		CertificateInput.DelegatedFeatureRequirementIds = Result.ObligationWitness.DelegatedFeatureRequirementIds;
		CertificateInput.DelegatedClosureRequirementIds = Result.ObligationWitness.DelegatedClosureRequirementIds;
		CertificateInput.DelegatedSeamWitnessIds = Result.ObligationWitness.DelegatedSeamWitnessIds;
		CertificateInput.DelegatedHostVerticalAccessWitnessIds = Result.ObligationWitness.DelegatedHostVerticalAccessWitnessIds;
		if (!Result.CapabilityWitness.SelectedSeamCapabilityId.IsNone())
		{
			CertificateInput.SelectedSeamCapabilityIds.Add(Result.CapabilityWitness.SelectedSeamCapabilityId);
		}
		if (!Result.CapabilityWitness.SelectedSeamWitnessId.IsNone())
		{
			CertificateInput.SelectedSeamWitnessIds.Add(Result.CapabilityWitness.SelectedSeamWitnessId);
		}

		// Build selected-traversal artifact from the witness if traversal ids were selected.
		// When traversal ids are present, the certificate must include them; artifact build
		// failure is fatal because TryBuildParentBranchCertificateArtifact rejects null
		// artifacts when Input.bHasSelectedTraversalIds is true.
		FLayoutProducedSelectedTraversalArtifact TraversalArtifact;
		const FLayoutProducedSelectedTraversalArtifact* TraversalArtifactPtr = nullptr;
		if (!Result.CapabilityWitness.SelectedTraversalCapabilityIds.IsEmpty())
		{
			CertificateInput.bHasSelectedTraversalIds = true;
			if (!LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
					FLayoutId(*FString::Printf(TEXT("SelectedTraversal.%s"), *Result.ChildRegionDebugPath)),
					CertificateInput.ChildScoutResultId,
					Result.CapabilityWitness.SelectedTraversalCapabilityIds,
					TraversalArtifact,
					OutFailureReason))
			{
				// Artifact build failed while traversal ids were selected.
				// TryBuildParentBranchCertificateArtifact will fail on the null pointer
				// because bHasSelectedTraversalIds is true.  Chain the original
				// artifact-build failure reason so it surfaces in the reject log.
				const FString TraversalFailure = OutFailureReason;
				const bool bCertificateOk =
					LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
						CertificateInput,
						nullptr,
						nullptr,
						OutCertificate,
						OutFailureReason);
				if (!bCertificateOk && !TraversalFailure.IsEmpty())
				{
					OutFailureReason = FString::Printf(
						TEXT("%s  (traversal artifact build: %s)"),
						*OutFailureReason,
						*TraversalFailure);
				}
				return false;
			}
			TraversalArtifactPtr = &TraversalArtifact;
		}

		return LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			CertificateInput,
			TraversalArtifactPtr,
			nullptr,
			OutCertificate,
			OutFailureReason);
	}

	FNegotiatedDemandResult FLayoutResponsibilityNegotiator::Negotiate(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		TArray<FContactBackedPlacementFamily> ContactFamilies =
			FLayoutResponsibilityNegotiator::BuildPlacementFamilies(DemandPlan, ParentSummary);
		if (ContactFamilies.IsEmpty())
		{
			return BuildFailedDemandResult(
				DemandPlan.ChildRegionDebugPath,
				ParentSummary,
				TEXT("No capability-backed placement family survived deterministic responsibility negotiation."));
		}

		ContactFamilies.Sort(
			[&SolveContext, &DemandPlan, &ParentSummary](
				const FContactBackedPlacementFamily& Left,
				const FContactBackedPlacementFamily& Right)
			{
				const int32 RequiredHostProviderCount =
					ResolveRequiredHostProviderCount(SolveContext);
				const FLayoutNegotiatedChildResponsibilityContract LeftContract =
					FLayoutResponsibilityNegotiator::BuildResponsibilitySet(
						SolveContext,
						DemandPlan,
						ParentSummary,
						Left).ResponsibilityContract;
				const FLayoutNegotiatedChildResponsibilityContract RightContract =
					FLayoutResponsibilityNegotiator::BuildResponsibilitySet(
						SolveContext,
						DemandPlan,
						ParentSummary,
						Right).ResponsibilityContract;
				const int32 LeftScore =
					ResolvePlacementFamilySelectionScore(
						SolveContext.RootRequest,
						Left,
						ParentSummary,
						LeftContract,
						RequiredHostProviderCount);
				const int32 RightScore =
					ResolvePlacementFamilySelectionScore(
						SolveContext.RootRequest,
						Right,
						ParentSummary,
						RightContract,
						RequiredHostProviderCount);
				if (LeftScore != RightScore)
				{
					return LeftScore > RightScore;
				}

				return Left.FamilyId.LexicalLess(Right.FamilyId);
			});

		FString FirstFailureReason;
		for (const FContactBackedPlacementFamily& ContactFamily : ContactFamilies)
		{
			FNegotiatedDemandResult Result;
			Result.ChildRegionDebugPath = DemandPlan.ChildRegionDebugPath;
			Result.ChildRequest = DemandPlan.ChildRequest;
			Result.ChildRequest.bUseSuppliedChildCapabilityEnvelope = true;
			Result.ChildRequest.SuppliedChildCapabilityEnvelope =
				DemandPlan.ChildSummary.CapabilityEnvelope;
			Result.ChildRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
				DemandPlan.ChildRegionDebugPath;
			Result.ResponsibilitySet =
				FLayoutResponsibilityNegotiator::BuildResponsibilitySet(
					SolveContext,
					DemandPlan,
					ParentSummary,
					ContactFamily);
			const FLayoutNegotiatedChildResponsibilityContract& Contract =
				Result.ResponsibilitySet.ResponsibilityContract;
			if (Contract.HostVerticalAccessResponsibility == ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned
				&& !DoesNegotiatedContractSatisfyHostProviderCount(Contract))
			{
				Result.FailureReason = FString::Printf(
					TEXT("Negotiated child responsibility contract for child region '%s' requires %s %d total counted host providers, but ParentOwned negotiation retained %d."),
					*Contract.ChildRegionDebugPath,
					Contract.bRequiresExactHostProviderCount ? TEXT("exactly") : TEXT("at least"),
					Contract.RequiredHostProviderCount,
					CountTotalNegotiatedHostProviders(Contract));
				if (FirstFailureReason.IsEmpty())
				{
					FirstFailureReason = Result.FailureReason;
				}
				continue;
			}
			Result.CapabilityWitness = BuildNegotiatedChildCapabilityWitness(
				DemandPlan,
				ContactFamily,
				Contract);
			Result.ObligationWitness = BuildNegotiatedChildObligationWitness(
				DemandPlan,
				Contract);
			Result.ParentSummary = ParentSummary;
			if (!FLayoutResponsibilityConfirmation::Confirm(
				SolveContext.RootRequest,
				ParentSummary,
				DemandPlan.ChildSummary,
				Result.ResponsibilitySet,
				Result.FailureReason))
			{
				if (FirstFailureReason.IsEmpty())
				{
					FirstFailureReason = Result.FailureReason;
				}
				continue;
			}

			Result.ChildRequest = BuildNegotiatedChildProofRequest(
				Result,
				SolveContext.RootRequest,
				SolveContext.PublicationMetadata);
			Result.CapabilityWitness.SelectedClosureSpanCapabilityId = Result.ChildRequest.CertifiedSelectedClosureSpanCapabilityId;
			if (!TryBuildParentBranchCertificateFromNegotiatedResult(
					Result,
					Result.ParentBranchCertificate,
					Result.FailureReason))
			{
				if (FirstFailureReason.IsEmpty())
				{
					FirstFailureReason = Result.FailureReason;
				}
				continue;
			}
			Result.bSucceeded = true;
			return Result;
		}

		if (FirstFailureReason.IsEmpty())
		{
			FirstFailureReason =
				TEXT("Capability-backed responsibility negotiation could not confirm any deterministic placement family.");
		}

		return BuildFailedDemandResult(
			DemandPlan.ChildRegionDebugPath,
			ParentSummary,
			FirstFailureReason);
	}

	bool FLayoutResponsibilityConfirmation::Confirm(
		const FLayoutRegionSolveRequest& RootRequest,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FChildCapabilitySummary& ChildSummary,
		FNegotiatedResponsibilitySet& InOutResponsibilitySet,
		FString& OutFailureReason)
	{
		static_cast<void>(ChildSummary);
		OutFailureReason.Reset();

		const FContactBackedPlacementFamily& ContactSet =
			InOutResponsibilitySet.ContactSet;
		const FLayoutNegotiatedChildResponsibilityContract& Contract =
			InOutResponsibilitySet.ResponsibilityContract;
		TSet<FLayoutId> RequiredRootEndpointCommitmentIds;
		for (const FLayoutCommittedEndpointAnchor& RootEndpointCommitment :
			RootRequest.CommittedEndpointAnchors)
		{
			if (!RootEndpointCommitment.CommitmentId.IsNone())
			{
				RequiredRootEndpointCommitmentIds.Add(
					RootEndpointCommitment.CommitmentId);
			}
		}
		const TSet<int32> ParentRouteSeedComponents =
			CollectParentRouteSeedComponents(RootRequest, ParentSummary);
		const TArray<TPair<int32, FLayoutChildCapabilityEndpoint>> LevelAwareEndpointOffers =
			CollectLevelAwareEndpointOffers(ChildSummary);

		if (!ValidateNegotiatedChildResponsibilityContract(Contract, OutFailureReason))
		{
			return false;
		}

		if (ContactSet.EndpointCommitments.IsEmpty())
		{
			OutFailureReason = TEXT("Negotiated responsibility confirmation requires at least one committed endpoint anchor.");
			return false;
		}

		if (Contract.ParentRegionDebugPath != RootRequest.RegionDebugPath)
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiated responsibility contract parent region '%s' does not match the current root request region '%s'."),
				*Contract.ParentRegionDebugPath,
				*RootRequest.RegionDebugPath);
			return false;
		}

		if (!ParentSummary.MissingRequiredChildBundleSupportCells.IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiated bounded parent proof requires child-bundle support cell %s, but that support is missing after residual parent subtraction."),
				*ParentSummary.MissingRequiredChildBundleSupportCells[0].ToString());
			return false;
		}

		for (const FLayoutCommittedEndpointAnchor& RootEndpointCommitment :
			RootRequest.CommittedEndpointAnchors)
		{
			if (RootEndpointCommitment.CommitmentId.IsNone())
			{
				continue;
			}

			const FResidualExternalEndpointReachability* Reachability =
				ParentSummary.ExternalEndpointReachability.FindByPredicate(
					[&RootEndpointCommitment](const FResidualExternalEndpointReachability& Candidate)
					{
						return Candidate.CommitmentId == RootEndpointCommitment.CommitmentId;
					});
			if (Reachability == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated bounded parent proof is missing residual reachability classification for required root external endpoint commitment '%s'."),
					*RootEndpointCommitment.CommitmentId.ToString());
				return false;
			}
		}

		for (const FResidualExternalEndpointReachability& Reachability :
			ParentSummary.ExternalEndpointReachability)
		{
			if (!Reachability.CommitmentId.IsNone()
				&& !RequiredRootEndpointCommitmentIds.Contains(
					Reachability.CommitmentId))
			{
				continue;
			}

			if (!Reachability.bReachableFromResidualParent)
			{
				OutFailureReason = Reachability.CommitmentId.IsNone()
					? TEXT("Negotiated bounded parent proof lost a required root external endpoint commitment after residual parent subtraction.")
					: FString::Printf(
						TEXT("Negotiated bounded parent proof lost required root external endpoint commitment '%s' after residual parent subtraction."),
						*Reachability.CommitmentId.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE
				&& Reachability.ParentComponentId != INDEX_NONE
				&& Reachability.ParentComponentId != ContactSet.ParentComponentId)
			{
				OutFailureReason = Reachability.CommitmentId.IsNone()
					? FString::Printf(
						TEXT("Negotiated bounded parent proof preserved a required root external endpoint only on residual parent component %d, but the chosen family settled on component %d."),
						Reachability.ParentComponentId,
						ContactSet.ParentComponentId)
					: FString::Printf(
						TEXT("Negotiated bounded parent proof preserved required root external endpoint commitment '%s' only on residual parent component %d, but the chosen family settled on component %d."),
						*Reachability.CommitmentId.ToString(),
						Reachability.ParentComponentId,
						ContactSet.ParentComponentId);
				return false;
			}
		}

		for (const FCommittedParentContactFace& ParentContactFace : ContactSet.ParentContactFaces)
		{
			const TArray<ELayoutFaceDirection>* RootConnectedFaces =
				ParentSummary.RootConnectedTraversableFacesByCell.Find(
					ParentContactFace.Cell);
			if (RootConnectedFaces == nullptr
				|| !RootConnectedFaces->Contains(ParentContactFace.FaceDirection))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated parent contact face %s on cell %s is not preserved in the residual parent traversable-face set."),
					*UEnum::GetValueAsString(ParentContactFace.FaceDirection),
					*ParentContactFace.Cell.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(
						ParentContactFace.Cell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated parent contact face %s on cell %s does not belong to the selected residual parent component %d."),
						*UEnum::GetValueAsString(ParentContactFace.FaceDirection),
						*ParentContactFace.Cell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		if (ContactSet.ParentTraversalIngressAnchors.IsEmpty()
			&& !ContactSet.AllCommittedContactTraversalAnchors.IsEmpty())
		{
			OutFailureReason = TEXT("Negotiated responsibility confirmation requires a non-empty parent traversal-ingress subset when committed traversal anchors exist.");
			return false;
		}

		if (!ParentRouteSeedComponents.IsEmpty()
			&& ContactSet.ParentComponentId != INDEX_NONE
			&& !ParentRouteSeedComponents.Contains(ContactSet.ParentComponentId))
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiated parent contact family component %d is root-connected only through a disconnected residual branch and does not intersect the current parent route seed network."),
				ContactSet.ParentComponentId);
			return false;
		}

		for (const FIntVector& ParentContactCell : ContactSet.ParentContactCells)
		{
			if (!ParentSummary.RootConnectedTraversableCells.Contains(ParentContactCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated parent contact cell %s is not root-connected in the residual parent summary."),
					*ParentContactCell.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(ParentContactCell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated parent contact cell %s does not belong to the selected residual parent component %d."),
						*ParentContactCell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : ContactSet.ParentTraversalIngressAnchors)
		{
			if (!ContactSet.AllCommittedContactTraversalAnchors.ContainsByPredicate(
				[&TraversalAnchor](const FLayoutCommittedTraversalAnchor& Candidate)
				{
					return AreCommittedTraversalAnchorsEquivalent(Candidate, TraversalAnchor);
				}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Parent traversal-ingress anchor at cell %s on channel '%s' is not a subset of the committed parent traversal anchors."),
					*TraversalAnchor.Cell.ToString(),
					*TraversalAnchor.TraversalChannel.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(TraversalAnchor.Cell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Parent traversal-ingress anchor at cell %s does not belong to the selected residual parent component %d."),
						*TraversalAnchor.Cell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		for (const FIntVector& CountedParentProviderCell : Contract.CountedParentVerticalAccessCells)
		{
			if (!ParentSummary.CountedParentVerticalAccessCells.Contains(CountedParentProviderCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated counted parent provider cell %s is not present in the residual parent counted provider set."),
					*CountedParentProviderCell.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(CountedParentProviderCell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated counted parent provider cell %s does not belong to the selected residual parent component %d."),
						*CountedParentProviderCell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		for (const FLayoutCommittedEndpointAnchor& EndpointCommitment : ContactSet.EndpointCommitments)
		{
			if (!LevelAwareEndpointOffers.ContainsByPredicate(
				[&EndpointCommitment](const TPair<int32, FLayoutChildCapabilityEndpoint>& EndpointOffer)
				{
					return DoesEndpointOfferMatchCommitment(EndpointOffer.Value, EndpointCommitment);
				}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated endpoint commitment '%s' at child-local cell %s is not proven by the compiled child capability summary."),
					*EndpointCommitment.CommitmentId.ToString(),
					*EndpointCommitment.LocalCell.ToString());
				return false;
			}

			if (!ContactSet.PlacementBundle.OccupiedLocalCells.Contains(EndpointCommitment.LocalCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated endpoint commitment '%s' at child-local cell %s is not occupied by the selected placement bundle."),
					*EndpointCommitment.CommitmentId.ToString(),
					*EndpointCommitment.LocalCell.ToString());
				return false;
			}

			if (!ContactSet.PlacementBundle.CoveredLevels.Contains(EndpointCommitment.LocalCell.Z))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated endpoint commitment '%s' targets child-local level %d, but the selected placement bundle does not cover that level."),
					*EndpointCommitment.CommitmentId.ToString(),
					EndpointCommitment.LocalCell.Z);
				return false;
			}

			const FLayoutNegotiatedLevelInterfaceContract* LevelInterface =
				Contract.CommittedParentChildInterfacesByLevel.FindByPredicate(
					[&EndpointCommitment](const FLayoutNegotiatedLevelInterfaceContract& Candidate)
					{
						return Candidate.Level == EndpointCommitment.LocalCell.Z;
					});
			if (LevelInterface == nullptr
				|| !LevelInterface->EndpointAnchors.ContainsByPredicate(
					[&EndpointCommitment](const FLayoutCommittedEndpointAnchor& Candidate)
					{
						return AreCommittedEndpointAnchorsEquivalent(Candidate, EndpointCommitment);
					}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated responsibility contract did not preserve endpoint commitment '%s' on level %d."),
					*EndpointCommitment.CommitmentId.ToString(),
					EndpointCommitment.LocalCell.Z);
				return false;
			}
		}

		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor : ContactSet.ParentTraversalIngressAnchors)
		{
			const FLayoutNegotiatedLevelInterfaceContract* LevelInterface =
				Contract.CommittedParentChildInterfacesByLevel.FindByPredicate(
					[&TraversalAnchor](const FLayoutNegotiatedLevelInterfaceContract& Candidate)
					{
						return Candidate.Level == TraversalAnchor.Cell.Z;
					});
			if (LevelInterface == nullptr
				|| !LevelInterface->TraversalAnchors.ContainsByPredicate(
					[&TraversalAnchor](const FLayoutCommittedTraversalAnchor& Candidate)
					{
						return AreCommittedTraversalAnchorsEquivalent(Candidate, TraversalAnchor);
					}))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated responsibility contract did not preserve traversal-ingress anchor at cell %s on level %d."),
					*TraversalAnchor.Cell.ToString(),
					TraversalAnchor.Cell.Z);
				return false;
			}
		}

		for (const FLayoutNegotiatedLevelCellSet& ReplacementLevel : Contract.ReplacementVolumeByLevel)
		{
			if (!ContactSet.PlacementBundle.CoveredLevels.Contains(ReplacementLevel.Level))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated replacement volume includes level %d, but the selected placement bundle does not cover that level."),
					ReplacementLevel.Level);
				return false;
			}

			const int32 PlacementBundleLevelCellCount =
				CountPlacementBundleOccupiedCellsAtLevel(
					ContactSet.PlacementBundle,
					ReplacementLevel.Level);
			if (ReplacementLevel.Cells.Num() != PlacementBundleLevelCellCount)
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated replacement volume keeps %d parent-local cells on level %d, but the selected placement bundle occupies %d child-local cells on that level."),
					ReplacementLevel.Cells.Num(),
					ReplacementLevel.Level,
					PlacementBundleLevelCellCount);
				return false;
			}
		}

		if (Contract.HostVerticalAccessResponsibility != ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned)
		{
			const auto IsCommittedEndpointInChosenFamily =
				[&ContactSet](const FLayoutCommittedEndpointAnchor& Anchor)
				{
					return ContactSet.EndpointCommitments.ContainsByPredicate(
						[&Anchor](const FLayoutCommittedEndpointAnchor& Candidate)
						{
							return AreCommittedEndpointAnchorsEquivalent(Candidate, Anchor);
						});
				};

			if (!IsCommittedEndpointInChosenFamily(Contract.RequiredHostIngressAnchor))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated required host ingress anchor '%s' at child-local cell %s is not part of the chosen negotiated endpoint commitment set."),
					*Contract.RequiredHostIngressAnchor.CommitmentId.ToString(),
					*Contract.RequiredHostIngressAnchor.LocalCell.ToString());
				return false;
			}

			if (!IsCommittedEndpointInChosenFamily(Contract.RequiredHostEgressAnchor))
			{
				OutFailureReason = FString::Printf(
					TEXT("Negotiated required host egress anchor '%s' at child-local cell %s is not part of the chosen negotiated endpoint commitment set."),
					*Contract.RequiredHostEgressAnchor.CommitmentId.ToString(),
					*Contract.RequiredHostEgressAnchor.LocalCell.ToString());
				return false;
			}

			const int32 LowerLevel = FMath::Min(
				Contract.RequiredHostIngressAnchor.LocalCell.Z,
				Contract.RequiredHostEgressAnchor.LocalCell.Z);
			const int32 UpperLevel = FMath::Max(
				Contract.RequiredHostIngressAnchor.LocalCell.Z,
				Contract.RequiredHostEgressAnchor.LocalCell.Z);
			if (!Contract.RequiredChildGenerallyConnectableAnchorPairId.IsNone())
			{
				TSet<int32> SpannedLevels;
				for (const int32 Level : Contract.RequiredChildInternalVerticalSpanLevels)
				{
					SpannedLevels.Add(Level);
				}

				for (int32 Level = LowerLevel; Level <= UpperLevel; ++Level)
				{
					if (!SpannedLevels.Contains(Level))
					{
						OutFailureReason = FString::Printf(
							TEXT("Negotiated generally connectable child anchor-pair proof '%s' does not cover ascent level %d."),
							*Contract.RequiredChildGenerallyConnectableAnchorPairId.ToString(),
							Level);
						return false;
					}

					const FChildLevelTraversalCapabilitySummary* TraversalSummary =
						ChildSummary.TraversalSummariesByLevel.FindByPredicate(
							[Level](const FChildLevelTraversalCapabilitySummary& Candidate)
							{
								return Candidate.LevelIndex == Level;
							});
					if (TraversalSummary == nullptr
						|| !TraversalSummary->ConnectableAnchorPairIds.Contains(
							Contract.RequiredChildGenerallyConnectableAnchorPairId))
					{
						OutFailureReason = FString::Printf(
							TEXT("Negotiated generally connectable child anchor-pair proof '%s' is not proven on child-local level %d by the compiled child capability summary."),
							*Contract.RequiredChildGenerallyConnectableAnchorPairId.ToString(),
							Level);
						return false;
					}
				}
			}
			else if (Contract.RequiredChildInternalVerticalRouteCells.IsEmpty())
			{
				OutFailureReason = TEXT("Negotiated child-owned/composed responsibility kept neither a generally connectable child anchor-pair proof nor compatibility route cells.");
				return false;
			}

			for (const FIntVector& RouteCell : Contract.RequiredChildInternalVerticalRouteCells)
			{
				if (!ContactSet.PlacementBundle.OccupiedLocalCells.Contains(RouteCell))
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated child vertical-route cell %s is not occupied by the selected placement bundle."),
						*RouteCell.ToString());
					return false;
				}

				if (!ContactSet.PlacementBundle.CoveredLevels.Contains(RouteCell.Z))
				{
					OutFailureReason = FString::Printf(
						TEXT("Negotiated child vertical-route cell %s is on level %d, but the selected placement bundle does not cover that level."),
						*RouteCell.ToString(),
						RouteCell.Z);
					return false;
				}
			}
		}

		InOutResponsibilitySet.BoundedParentProofEvidence =
			BuildBoundedParentProofEvidence(
				RootRequest,
				ParentSummary,
				ContactSet,
				Contract);
		return true;
	}

	TArray<FParentContactCapabilityCandidate> CollectParentContactCapabilityCandidates(
		const FChildCapabilitySummary& ChildSummary,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		return FLayoutResponsibilityNegotiator::CollectParentContactCapabilityCandidates(
			ChildSummary,
			ParentSummary);
	}

	TArray<FContactBackedPlacementFamily> BuildNegotiatedPlacementFamilies(
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		return FLayoutResponsibilityNegotiator::BuildPlacementFamilies(
			DemandPlan,
			ParentSummary);
	}

	TArray<FContactBackedPlacementFamily> CollectCapabilityBackedContactSets(
		const FChildCapabilitySummary& ChildSummary,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		return FLayoutResponsibilityNegotiator::CollectCapabilityBackedContactSets(
			ChildSummary,
			ParentSummary);
	}

	void SelectParentTraversalIngressSubset(
		const FResidualParentCapabilitySummary& ParentSummary,
		FContactBackedPlacementFamily& InOutContactSet)
	{
		FLayoutResponsibilityNegotiator::SelectParentTraversalIngressSubset(
			ParentSummary,
			InOutContactSet);
	}

	FNegotiatedResponsibilitySet BuildResponsibilitySetFromContactSet(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FContactBackedPlacementFamily& ContactSet)
	{
		return FLayoutResponsibilityNegotiator::BuildResponsibilitySet(
			SolveContext,
			DemandPlan,
			ParentSummary,
			ContactSet);
	}

	FNegotiatedDemandResult NegotiateDemandResponsibilities(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary)
	{
		return FLayoutResponsibilityNegotiator::Negotiate(
			SolveContext,
			DemandPlan,
			ParentSummary);
	}

	bool ConfirmNegotiatedResponsibilitySet(
		const FLayoutRegionSolveRequest& RootRequest,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FChildCapabilitySummary& ChildSummary,
		FNegotiatedResponsibilitySet& InOutResponsibilitySet,
		FString& OutFailureReason)
	{
		return FLayoutResponsibilityConfirmation::Confirm(
			RootRequest,
			ParentSummary,
			ChildSummary,
			InOutResponsibilitySet,
			OutFailureReason);
	}

}
