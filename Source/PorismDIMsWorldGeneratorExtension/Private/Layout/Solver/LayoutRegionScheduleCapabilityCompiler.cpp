// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutZoneFeatureDemand.h"
#include "Layout/Types/LayoutGameplayTags.h"

/**
 * Private rewrite home for `FLayoutStructuralCapabilityCompiler`.
 *
 * This file is intentionally created early so module/child placement-bundle
 * compilation has a stable ownership home before behavior migration begins.
 */

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
		static FLayoutProofRecord MakeCapabilityProofRecord(
			const FLayoutId ProofId,
			const ELayoutProofKind ProofKind,
			const FLayoutId TargetId,
			const TArray<FLayoutId>& SourceIds,
			const FString& ProofSummary)
		{
			FLayoutProofRecord Proof;
			Proof.ProofId = ProofId;
			Proof.ProofKind = ProofKind;
			Proof.TargetId = TargetId;
			Proof.SourceIds = SourceIds;
			Proof.ProofSummary = ProofSummary;
			return Proof;
		}

		static FLayoutValidationAssertionRecord MakeCapabilityAssertionRecord(
			const FLayoutId AssertionId,
			const ELayoutValidationAssertionKind AssertionKind,
			const bool bPassed,
			const TArray<FLayoutId>& RelatedIds,
			const FString& FailureReason = FString())
		{
			FLayoutValidationAssertionRecord Assertion;
			Assertion.AssertionId = AssertionId;
			Assertion.AssertionKind = AssertionKind;
			Assertion.bPassed = bPassed;
			Assertion.RelatedIds = RelatedIds;
			Assertion.FailureReason = FailureReason;
			return Assertion;
		}

		// Keep child-demand assertion breadcrumbs on par with root request validation so
		// frozen rewrite failures stay diagnosable without live asset expansion.
		static FString BuildChildRequestBreadcrumb(const FLayoutRegionSolveRequest& ChildRequest)
		{
			return FString::Printf(
				TEXT(" Child request '%s' (%s)."),
				*ChildRequest.RegionDebugPath,
				*ChildRequest.EffectiveSnapshotId.ToString());
		}

		static FString BuildChildCapabilityEnvelopeBreadcrumb(const FLayoutChildCapabilityEnvelope& CapabilityEnvelope)
		{
			return FString::Printf(
				TEXT(" Child capability envelope '%s' (%s)."),
				*CapabilityEnvelope.RegionDebugPath,
				*CapabilityEnvelope.SnapshotId.ToString());
		}

		static FString BuildChildProfileSnapshotBreadcrumb(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
		{
			const FString LiveProfileBreadcrumb =
				ProfileSnapshot.SourceProfile != nullptr
					? FString::Printf(TEXT(" Profile: %s."), *ProfileSnapshot.SourceProfile->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Frozen child profile snapshot %s (%s).%s"),
				*ProfileSnapshot.DebugName.ToString(),
				*ProfileSnapshot.SnapshotId.ToString(),
				*LiveProfileBreadcrumb);
		}

		static FString BuildChildContentSetSnapshotBreadcrumb(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
		{
			const FString LiveContentSetBreadcrumb =
				ContentSetSnapshot.SourceContentSet != nullptr
					? FString::Printf(TEXT(" ContentSet: %s."), *ContentSetSnapshot.SourceContentSet->GetPathName())
					: FString();
			return FString::Printf(
				TEXT(" Frozen child content-set snapshot %s (%s).%s"),
				*ContentSetSnapshot.DebugName.ToString(),
				*ContentSetSnapshot.SnapshotId.ToString(),
				*LiveContentSetBreadcrumb);
		}

		static FString BuildChildModuleCatalogBreadcrumb(const FLayoutModuleCatalog& ModuleCatalog)
		{
			return FString::Printf(
				TEXT(" Frozen child module-set snapshot %s (%s).%s"),
				*ModuleCatalog.DebugName.ToString(),
				*ModuleCatalog.SnapshotId.ToString(),
				*FString());
		}

		static FString BuildFrozenChildSnapshotIdBreadcrumb(
			const TCHAR* SnapshotLabel,
			const FLayoutId SnapshotId)
		{
			return FString::Printf(
				TEXT(" Frozen child %s snapshot (%s)."),
				SnapshotLabel,
				*SnapshotId.ToString());
		}

		static TArray<FLayoutId> BuildChildRequestRelatedIds(
			const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
			const FLayoutRegionSolveRequest& ChildRequest)
		{
			TArray<FLayoutId> RelatedIds = {
				EntrySnapshot.EntryId,
				ChildRequest.EffectiveSnapshotId,
				ChildRequest.ProfileSnapshot.SnapshotId,
				ChildRequest.ContentSetSnapshot.SnapshotId
			};
			if (ChildRequest.ModuleCatalog.SnapshotId != NAME_None)
			{
				RelatedIds.Add(ChildRequest.ModuleCatalog.SnapshotId);
			}
			return RelatedIds;
		}

		static FString TagsToStableKey(const FGameplayTagContainer& Tags);
		static TArray<FIntVector> BuildVerticalAccessCellsFromRequest(const FLayoutRegionSolveRequest& ChildRequest);

		static bool IsLexicographicallyEarlierCell(
			const FIntVector& Left,
			const FIntVector& Right)
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
		}

		static bool IsLexicographicallyEarlierCommittedEndpointAnchor(
			const FLayoutCommittedEndpointAnchor& Left,
			const FLayoutCommittedEndpointAnchor& Right)
		{
			if (Left.LocalCell != Right.LocalCell)
			{
				return IsLexicographicallyEarlierCell(Left.LocalCell, Right.LocalCell);
			}
			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}
			if (Left.CommitmentId != Right.CommitmentId)
			{
				return Left.CommitmentId.LexicalLess(Right.CommitmentId);
			}
			if (Left.RequiredWorldCenterBlockZ != Right.RequiredWorldCenterBlockZ)
			{
				return Left.RequiredWorldCenterBlockZ < Right.RequiredWorldCenterBlockZ;
			}
			return Left.ConnectionTag.ToString() < Right.ConnectionTag.ToString();
		}

		static bool IsLexicographicallyEarlierChildCapabilityEndpoint(
			const FLayoutChildCapabilityEndpoint& Left,
			const FLayoutChildCapabilityEndpoint& Right)
		{
			if (Left.LocalCell != Right.LocalCell)
			{
				return IsLexicographicallyEarlierCell(Left.LocalCell, Right.LocalCell);
			}
			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}
			if (Left.ConnectionTag != Right.ConnectionTag)
			{
				return Left.ConnectionTag.ToString() < Right.ConnectionTag.ToString();
			}
			if (Left.CapabilityId != Right.CapabilityId)
			{
				return Left.CapabilityId.LexicalLess(Right.CapabilityId);
			}
			return TagsToStableKey(Left.TraversalChannels) < TagsToStableKey(Right.TraversalChannels);
		}

		static bool IsLexicographicallyEarlierChildCapabilitySeam(
			const FLayoutChildCapabilitySeam& Left,
			const FLayoutChildCapabilitySeam& Right)
		{
			if (Left.LocalCell != Right.LocalCell)
			{
				return IsLexicographicallyEarlierCell(Left.LocalCell, Right.LocalCell);
			}
			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}
			if (Left.InterfaceFamily != Right.InterfaceFamily)
			{
				return Left.InterfaceFamily.ToString() < Right.InterfaceFamily.ToString();
			}
			if (Left.JunctionUsage != Right.JunctionUsage)
			{
				return static_cast<int32>(Left.JunctionUsage)
					< static_cast<int32>(Right.JunctionUsage);
			}
			if (Left.bCanOwnSeam != Right.bCanOwnSeam)
			{
				return Left.bCanOwnSeam && !Right.bCanOwnSeam;
			}
			if (Left.bCanAcceptSeam != Right.bCanAcceptSeam)
			{
				return Left.bCanAcceptSeam && !Right.bCanAcceptSeam;
			}
			return Left.CapabilityId.LexicalLess(Right.CapabilityId);
		}

		static bool IsLexicographicallyEarlierChildCapabilitySpan(
			const FLayoutChildCapabilitySpan& Left,
			const FLayoutChildCapabilitySpan& Right)
		{
			if (Left.LocalCell != Right.LocalCell)
			{
				return IsLexicographicallyEarlierCell(Left.LocalCell, Right.LocalCell);
			}
			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}
			if (Left.ClosureId != Right.ClosureId)
			{
				return Left.ClosureId.LexicalLess(Right.ClosureId);
			}
			if (Left.ThicknessCells != Right.ThicknessCells)
			{
				return Left.ThicknessCells < Right.ThicknessCells;
			}
			if (Left.bSealsBoundary != Right.bSealsBoundary)
			{
				return Left.bSealsBoundary && !Right.bSealsBoundary;
			}
			return Left.CapabilityId.LexicalLess(Right.CapabilityId);
		}

		static bool IsLexicographicallyEarlierTerrainSteppedTransitionRequirement(
			const FTerrainSteppedTransitionRequirement& Left,
			const FTerrainSteppedTransitionRequirement& Right)
		{
			if (Left.FromCell != Right.FromCell)
			{
				return IsLexicographicallyEarlierCell(Left.FromCell, Right.FromCell);
			}
			if (Left.ToCell != Right.ToCell)
			{
				return IsLexicographicallyEarlierCell(Left.ToCell, Right.ToCell);
			}
			return Left.StepHeightBlocks < Right.StepHeightBlocks;
		}

		static bool DoesTerrainSteppedTransitionHaveRootSupport(
			const FTerrainSteppedTransitionRequirement& Requirement)
		{
			return Requirement.RootSupportStatus
				== ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate;
		}


		/** Capability level scope uses authored coordinates; generated bridge height does not add an authored level. */
		static TArray<FLayoutId> BuildSupportingRootVerticalAccessBundleIds(
			const TArray<FPlacementCapabilityBundle>& RootPlacementBundles,
			const FIntVector& SupportingRootVerticalAccessCandidateCell,
			const FIntPoint& RootFootprintSize,
			const TMap<FIntPoint, int32>& TopPlannedLevelByXY)
		{
			TArray<FLayoutId> BundleIds;
			for (const FPlacementCapabilityBundle& Bundle : RootPlacementBundles)
			{
				const bool bBundleSupportsCandidateCell =
					Bundle.bSupportsRootVerticalAccess
					&& [SupportingRootVerticalAccessCandidateCell, RootFootprintSize, &Bundle]()
					{
						switch (Bundle.PlacementZone)
						{
						case ELayoutPlacementZone::Any:
							return true;
						case ELayoutPlacementZone::Perimeter:
							return SupportingRootVerticalAccessCandidateCell.X == 0
								|| SupportingRootVerticalAccessCandidateCell.Y == 0
								|| SupportingRootVerticalAccessCandidateCell.X == RootFootprintSize.X - 1
								|| SupportingRootVerticalAccessCandidateCell.Y == RootFootprintSize.Y - 1;
						case ELayoutPlacementZone::Edge:
						{
							const bool bOnXEdge =
								SupportingRootVerticalAccessCandidateCell.X == 0
								|| SupportingRootVerticalAccessCandidateCell.X == RootFootprintSize.X - 1;
							const bool bOnYEdge =
								SupportingRootVerticalAccessCandidateCell.Y == 0
								|| SupportingRootVerticalAccessCandidateCell.Y == RootFootprintSize.Y - 1;
							return (bOnXEdge || bOnYEdge) && !(bOnXEdge && bOnYEdge);
						}
						case ELayoutPlacementZone::Corner:
						{
							const bool bOnXEdge =
								SupportingRootVerticalAccessCandidateCell.X == 0
								|| SupportingRootVerticalAccessCandidateCell.X == RootFootprintSize.X - 1;
							const bool bOnYEdge =
								SupportingRootVerticalAccessCandidateCell.Y == 0
								|| SupportingRootVerticalAccessCandidateCell.Y == RootFootprintSize.Y - 1;
							return RootFootprintSize.X > 1 && RootFootprintSize.Y > 1 && bOnXEdge && bOnYEdge;
						}
						case ELayoutPlacementZone::Interior:
							return SupportingRootVerticalAccessCandidateCell.X > 0
								&& SupportingRootVerticalAccessCandidateCell.Y > 0
								&& SupportingRootVerticalAccessCandidateCell.X < RootFootprintSize.X - 1
								&& SupportingRootVerticalAccessCandidateCell.Y < RootFootprintSize.Y - 1;
						case ELayoutPlacementZone::Core:
						{
							if (SupportingRootVerticalAccessCandidateCell.Z != 0)
							{
								return false;
							}

							const float CenterX = static_cast<float>(RootFootprintSize.X - 1) * 0.5f;
							const float CenterY = static_cast<float>(RootFootprintSize.Y - 1) * 0.5f;
							return FMath::Abs(static_cast<float>(SupportingRootVerticalAccessCandidateCell.X) - CenterX) <= 0.5f
								&& FMath::Abs(static_cast<float>(SupportingRootVerticalAccessCandidateCell.Y) - CenterY) <= 0.5f;
						}
						default:
							return true;
						}
					}()
					&& [SupportingRootVerticalAccessCandidateCell, &TopPlannedLevelByXY, &Bundle]()
					{
						switch (Bundle.LevelPlacementPolicy)
						{
						case ELayoutLevelPlacementPolicy::AnyLevel:
							return true;
						case ELayoutLevelPlacementPolicy::GroundOnly:
							return SupportingRootVerticalAccessCandidateCell.Z == 0;
						case ELayoutLevelPlacementPolicy::SpecificLevel:
							return SupportingRootVerticalAccessCandidateCell.Z == Bundle.SpecificLevel;
						case ELayoutLevelPlacementPolicy::TopLevelOnly:
						{
							const int32* TopLevel = TopPlannedLevelByXY.Find(FIntPoint(
								SupportingRootVerticalAccessCandidateCell.X,
								SupportingRootVerticalAccessCandidateCell.Y));
							return TopLevel != nullptr && SupportingRootVerticalAccessCandidateCell.Z == *TopLevel;
						}
						case ELayoutLevelPlacementPolicy::AboveGroundLevel:
							return SupportingRootVerticalAccessCandidateCell.Z > 0;
						case ELayoutLevelPlacementPolicy::BelowTopLevel:
						{
							const int32* TopLevel = TopPlannedLevelByXY.Find(FIntPoint(
								SupportingRootVerticalAccessCandidateCell.X,
								SupportingRootVerticalAccessCandidateCell.Y));
							return TopLevel != nullptr && SupportingRootVerticalAccessCandidateCell.Z < *TopLevel;
						}
						default:
							return true;
						}
					}();
				if (!bBundleSupportsCandidateCell)
				{
					continue;
				}

				BundleIds.Add(Bundle.BundleId);
			}

			BundleIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
			{
				return Left.LexicalLess(Right);
			});
			return BundleIds;
		}

		static const FPlacementCapabilityBundle* FindPlacementCapabilityBundleById(
			const TArray<FPlacementCapabilityBundle>& RootPlacementBundles,
			const FLayoutId BundleId)
		{
			return RootPlacementBundles.FindByPredicate(
				[BundleId](const FPlacementCapabilityBundle& CandidateBundle)
				{
					return CandidateBundle.BundleId == BundleId;
				});
		}

		static bool IsHorizontalTraversalFaceDirection(const ELayoutFaceDirection Direction)
		{
			return Direction == ELayoutFaceDirection::PosX
				|| Direction == ELayoutFaceDirection::NegX
				|| Direction == ELayoutFaceDirection::PosY
				|| Direction == ELayoutFaceDirection::NegY;
		}


		static bool IsBoundaryCell(
			const FIntVector& Cell,
			const FIntPoint& FootprintSize)
		{
			return Cell.X == 0
				|| Cell.Y == 0
				|| Cell.X == FootprintSize.X - 1
				|| Cell.Y == FootprintSize.Y - 1;
		}

		static bool IsCornerBoundaryCell(
			const FIntVector& Cell,
			const FIntPoint& FootprintSize)
		{
			if (FootprintSize.X <= 1 || FootprintSize.Y <= 1)
			{
				return false;
			}

			const bool bOnXEdge = Cell.X == 0 || Cell.X == FootprintSize.X - 1;
			const bool bOnYEdge = Cell.Y == 0 || Cell.Y == FootprintSize.Y - 1;
			return bOnXEdge && bOnYEdge;
		}

		static bool DoesCellMatchPlacementZoneInFootprint(
			const FIntVector& Cell,
			const FIntPoint& FootprintSize,
			const ELayoutPlacementZone PlacementZone)
		{
			switch (PlacementZone)
			{
			case ELayoutPlacementZone::Any:
				return true;
			case ELayoutPlacementZone::Perimeter:
				return IsBoundaryCell(Cell, FootprintSize);
			case ELayoutPlacementZone::Edge:
				return IsBoundaryCell(Cell, FootprintSize) && !IsCornerBoundaryCell(Cell, FootprintSize);
			case ELayoutPlacementZone::Corner:
				return IsCornerBoundaryCell(Cell, FootprintSize);
			case ELayoutPlacementZone::Interior:
				return !IsBoundaryCell(Cell, FootprintSize);
			case ELayoutPlacementZone::Core:
			{
				if (Cell.Z != 0)
				{
					return false;
				}

				const float CenterX = static_cast<float>(FootprintSize.X - 1) * 0.5f;
				const float CenterY = static_cast<float>(FootprintSize.Y - 1) * 0.5f;
				return FMath::Abs(static_cast<float>(Cell.X) - CenterX) <= 0.5f
					&& FMath::Abs(static_cast<float>(Cell.Y) - CenterY) <= 0.5f;
			}
			default:
				return true;
			}
		}

		static bool DoesCellMatchAuthoredLevelScope(
			const FIntVector& Cell,
			const int32 LevelCount,
			const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
			const int32 SpecificLevel)
		{
			switch (LevelPlacementPolicy)
			{
			case ELayoutLevelPlacementPolicy::AnyLevel:
				return true;
			case ELayoutLevelPlacementPolicy::GroundOnly:
				return Cell.Z == 0;
			case ELayoutLevelPlacementPolicy::SpecificLevel:
				return Cell.Z == SpecificLevel;
			case ELayoutLevelPlacementPolicy::TopLevelOnly:
				return Cell.Z == FMath::Max(0, LevelCount - 1);
			case ELayoutLevelPlacementPolicy::AboveGroundLevel:
				return Cell.Z > 0;
			case ELayoutLevelPlacementPolicy::BelowTopLevel:
				return Cell.Z < FMath::Max(0, LevelCount - 1);
			default:
				return true;
			}
		}

		static TArray<FTerrainSteppedTransitionRequirement> BuildRootTerrainSteppedTransitionRequirements(
			const TArray<FLayoutPlannedCell>& RootPlannedCells,
			const FIntPoint& RootFootprintSize,
			const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
			const FLayoutSteppedTerrainSupportMap& RootSteppedTerrainSupportMap,
			const TArray<FPlacementCapabilityBundle>& RootPlacementBundles)
		{
			TArray<FTerrainSteppedTransitionRequirement> TransitionRequirements;
			const bool bHasRelevantTerrainStep =
				RootSteppedTerrainSupportMap.AdjacencySteps.ContainsByPredicate(
					[&WorldBindingPlacementPolicy](const FLayoutSteppedTerrainAdjacencyStep& Step)
					{
						return Step.SnappedLevelDelta > 0
							&& Step.StepHeightBlocks > WorldBindingPlacementPolicy.HeightIgnoreThreshold;
					});
			if (!bHasRelevantTerrainStep)
			{
				return TransitionRequirements;
			}

			TSet<FIntVector> PlannedCellSet;
			TMap<FIntPoint, int32> TopPlannedLevelByXY;
			PlannedCellSet.Reserve(RootPlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : RootPlannedCells)
			{
				PlannedCellSet.Add(PlannedCell.Cell);
				const FIntPoint ColumnKey(PlannedCell.Cell.X, PlannedCell.Cell.Y);
				int32& TopLevel = TopPlannedLevelByXY.FindOrAdd(ColumnKey);
				TopLevel = FMath::Max(TopLevel, PlannedCell.ModuleLevelIndex != INDEX_NONE
					? PlannedCell.ModuleLevelIndex : PlannedCell.Cell.Z);
			}

			for (const FLayoutPlannedCell& PlannedCell : RootPlannedCells)
			{
				if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
				{
					continue;
				}

				FTerrainSteppedTransitionRequirement& Requirement =
					TransitionRequirements.AddDefaulted_GetRef();
				Requirement.FromCell = PlannedCell.Cell;
				Requirement.ToCell = PlannedCell.Cell + FIntVector(0, 0, 1);
				Requirement.StepHeightBlocks =
					RootSteppedTerrainSupportMap.SharedCellHeightInBlocks;
				Requirement.SnappedLevelDelta = 1;
				Requirement.AdjacentVerticalAccessCells = {PlannedCell.Cell};
				Requirement.RequiredTraversalCells = {
					PlannedCell.Cell,
					Requirement.ToCell
				};

				if (!PlannedCellSet.Contains(Requirement.ToCell))
				{
					Requirement.RootSupportStatus =
						ETerrainSteppedTransitionRootSupportStatus::MissingAdjacentVerticalAccessIntent;
					continue;
				}

				Requirement.SupportingRootVerticalAccessBundleIds =
					BuildSupportingRootVerticalAccessBundleIds(
						RootPlacementBundles,
						FIntVector(PlannedCell.Cell.X, PlannedCell.Cell.Y,
							PlannedCell.ModuleLevelIndex != INDEX_NONE ? PlannedCell.ModuleLevelIndex : PlannedCell.Cell.Z),
						RootFootprintSize,
						TopPlannedLevelByXY);
				if (Requirement.SupportingRootVerticalAccessBundleIds.IsEmpty())
				{
					Requirement.RootSupportStatus =
						ETerrainSteppedTransitionRootSupportStatus::MissingRootVerticalAccessCandidate;
					continue;
				}

				Requirement.SupportingRootVerticalAccessCandidateCells.Add(
					PlannedCell.Cell);
				Requirement.RootSupportStatus =
					ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate;
			}

			TransitionRequirements.Sort([](
				const FTerrainSteppedTransitionRequirement& Left,
				const FTerrainSteppedTransitionRequirement& Right)
			{
				return IsLexicographicallyEarlierTerrainSteppedTransitionRequirement(
					Left,
					Right);
			});
			return TransitionRequirements;
		}

		static int32 ResolveSnappedTerrainLevel(
			const FLayoutSteppedTerrainSupportMap& SupportMap,
			const FLayoutSteppedTerrainSupportSample& SupportSample)
		{
			if (SupportMap.SharedCellHeightInBlocks <= 0)
			{
				return 0;
			}

			const int32 SnappedCeilingZ = SupportSample.SnappedSupportCeilingZ != 0
				? SupportSample.SnappedSupportCeilingZ
				: SupportSample.SupportSurfaceZ;
			return FMath::FloorToInt(
				static_cast<float>(SnappedCeilingZ)
				/ static_cast<float>(SupportMap.SharedCellHeightInBlocks));
		}

		static TArray<FTerrainSteppedReachableSurfaceAnnotation> BuildRootTerrainSteppedReachableSurfaceAnnotations(
			const TArray<FLayoutPlannedCell>& RootPlannedCells,
			const FLayoutSteppedTerrainSupportMap& RootSteppedTerrainSupportMap)
		{
			TArray<FTerrainSteppedReachableSurfaceAnnotation> SurfaceAnnotations;
			if (RootSteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
				|| RootSteppedTerrainSupportMap.SupportSamples.IsEmpty())
			{
				return SurfaceAnnotations;
			}

			TMap<FIntVector, ELayoutCellIntent> PlannedIntentByCell;
			PlannedIntentByCell.Reserve(RootPlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : RootPlannedCells)
			{
				PlannedIntentByCell.Add(PlannedCell.Cell, PlannedCell.Intent);
			}

			bool bHasEntryReachableLevel = false;
			bool bHasFallbackReachableLevel = false;
			int32 EntryReachableLevel = 0;
			int32 FallbackReachableLevel = 0;
			for (const FLayoutSteppedTerrainSupportSample& SupportSample : RootSteppedTerrainSupportMap.SupportSamples)
			{
				const ELayoutCellIntent* PlannedIntent = PlannedIntentByCell.Find(SupportSample.LocalCell);
				if (PlannedIntent == nullptr)
				{
					continue;
				}

				const int32 SnappedTerrainLevel = ResolveSnappedTerrainLevel(
					RootSteppedTerrainSupportMap,
					SupportSample);
				FallbackReachableLevel = bHasFallbackReachableLevel
					? FMath::Min(FallbackReachableLevel, SnappedTerrainLevel)
					: SnappedTerrainLevel;
				bHasFallbackReachableLevel = true;

				if (*PlannedIntent == ELayoutCellIntent::Entry)
				{
					EntryReachableLevel = bHasEntryReachableLevel
						? FMath::Min(EntryReachableLevel, SnappedTerrainLevel)
						: SnappedTerrainLevel;
					bHasEntryReachableLevel = true;
				}
			}

			if (!bHasFallbackReachableLevel)
			{
				return SurfaceAnnotations;
			}

			const int32 InitialReachableLevel = bHasEntryReachableLevel
				? EntryReachableLevel
				: FallbackReachableLevel;

			for (const FLayoutSteppedTerrainSupportSample& SupportSample : RootSteppedTerrainSupportMap.SupportSamples)
			{
				if (!PlannedIntentByCell.Contains(SupportSample.LocalCell))
				{
					continue;
				}

				FTerrainSteppedReachableSurfaceAnnotation& Annotation = SurfaceAnnotations.AddDefaulted_GetRef();
				Annotation.Cell = SupportSample.LocalCell;
				Annotation.SnappedTerrainLevel = ResolveSnappedTerrainLevel(
					RootSteppedTerrainSupportMap,
					SupportSample);
				Annotation.SupportSurfaceZ = SupportSample.SupportSurfaceZ;
				Annotation.bReachablePlateau = Annotation.SnappedTerrainLevel == InitialReachableLevel;
				Annotation.bDeferredTerrace = Annotation.SnappedTerrainLevel > InitialReachableLevel;
			}

			SurfaceAnnotations.Sort([](
				const FTerrainSteppedReachableSurfaceAnnotation& Left,
				const FTerrainSteppedReachableSurfaceAnnotation& Right)
			{
				return IsLexicographicallyEarlierCell(Left.Cell, Right.Cell);
			});
			return SurfaceAnnotations;
		}

		static int32 BuildTerrainStageFrontierId(
			const FTerrainSteppedAscentFrontierAnnotation& Frontier,
			const int32 FrontierIndex);

		static TArray<FTerrainSteppedAscentFrontierAnnotation> BuildRootTerrainSteppedAscentFrontierAnnotations(
			const TArray<FTerrainSteppedTransitionRequirement>& RootTerrainSteppedTransitionRequirements,
			const TArray<FTerrainSteppedReachableSurfaceAnnotation>& SurfaceAnnotations)
		{
			TArray<FTerrainSteppedAscentFrontierAnnotation> FrontierAnnotations;
			TMap<FIntVector, int32> SnappedLevelByCell;
			SnappedLevelByCell.Reserve(SurfaceAnnotations.Num());
			for (const FTerrainSteppedReachableSurfaceAnnotation& SurfaceAnnotation : SurfaceAnnotations)
			{
				SnappedLevelByCell.Add(SurfaceAnnotation.Cell, SurfaceAnnotation.SnappedTerrainLevel);
			}

			for (const FTerrainSteppedTransitionRequirement& Requirement : RootTerrainSteppedTransitionRequirements)
			{
				const int32* FromLevel = SnappedLevelByCell.Find(Requirement.FromCell);
				const int32* ToLevel = SnappedLevelByCell.Find(Requirement.ToCell);
				if (FromLevel == nullptr || ToLevel == nullptr || *FromLevel == *ToLevel)
				{
					continue;
				}

				FTerrainSteppedAscentFrontierAnnotation& Frontier = FrontierAnnotations.AddDefaulted_GetRef();
				Frontier.FromCell = Requirement.FromCell;
				Frontier.ToCell = Requirement.ToCell;
				Frontier.LowerSnappedTerrainLevel = FMath::Min(*FromLevel, *ToLevel);
				Frontier.HigherSnappedTerrainLevel = FMath::Max(*FromLevel, *ToLevel);
				Frontier.SnappedLevelDelta = Requirement.SnappedLevelDelta;
				Frontier.ProtectedAscentPocketCells = Requirement.AdjacentVerticalAccessCells;
				Frontier.ProtectedAscentPocketCells.Sort([](const FIntVector& Left, const FIntVector& Right)
				{
					return IsLexicographicallyEarlierCell(Left, Right);
				});
			}

			FrontierAnnotations.Sort([](
				const FTerrainSteppedAscentFrontierAnnotation& Left,
				const FTerrainSteppedAscentFrontierAnnotation& Right)
			{
				if (Left.LowerSnappedTerrainLevel != Right.LowerSnappedTerrainLevel)
				{
					return Left.LowerSnappedTerrainLevel < Right.LowerSnappedTerrainLevel;
				}
				if (Left.HigherSnappedTerrainLevel != Right.HigherSnappedTerrainLevel)
				{
					return Left.HigherSnappedTerrainLevel < Right.HigherSnappedTerrainLevel;
				}
				if (Left.FromCell != Right.FromCell)
				{
					return IsLexicographicallyEarlierCell(Left.FromCell, Right.FromCell);
				}
				return IsLexicographicallyEarlierCell(Left.ToCell, Right.ToCell);
			});
			for (int32 FrontierIndex = 0; FrontierIndex < FrontierAnnotations.Num(); ++FrontierIndex)
			{
				FrontierAnnotations[FrontierIndex].AscentFrontierId =
					BuildTerrainStageFrontierId(FrontierAnnotations[FrontierIndex], FrontierIndex);
			}
			return FrontierAnnotations;
		}

		static int32 ResolveOpenCellCountFromPercent(const int32 EligibleCellCount, const float Percent)
		{
			if (EligibleCellCount <= 0 || Percent <= 0.0f)
			{
				return 0;
			}

			return FMath::Clamp(
				FMath::RoundToInt(static_cast<float>(EligibleCellCount) * Percent / 100.0f),
				0,
				EligibleCellCount);
		}

			static void ResolveOpenCellRange(
				const int32 EligibleCellCount,
				const float MinPercent,
				const float MaxPercent,
				const int32 MinCells,
				const int32 MaxCells,
				int32& OutMinOpenCells,
				int32& OutMaxOpenCells)
			{
				const int32 MinFromPercent = ResolveOpenCellCountFromPercent(EligibleCellCount, MinPercent);
				const int32 MaxFromPercent = ResolveOpenCellCountFromPercent(EligibleCellCount, MaxPercent);

				OutMinOpenCells = FMath::Min(
					EligibleCellCount,
					FMath::Max(MinFromPercent, FMath::Max(0, MinCells)));
				OutMaxOpenCells = MaxPercent > 0.0f || MaxCells > 0
					? FMath::Min(
						EligibleCellCount,
						MaxCells > 0 ? FMath::Min(EligibleCellCount, MaxCells) : EligibleCellCount)
					: EligibleCellCount;

				if (MaxPercent > 0.0f)
				{
					OutMaxOpenCells = FMath::Min(OutMaxOpenCells, MaxFromPercent);
				}

				if (OutMaxOpenCells < OutMinOpenCells)
				{
					OutMaxOpenCells = OutMinOpenCells;
				}
			}

			static void SortCellsDeterministicallyForOpenSelection(
				TArray<FIntVector>& Cells,
				const uint32 Seed)
			{
				Cells.Sort([Seed](const FIntVector& Left, const FIntVector& Right)
				{
					const uint32 LeftHash = HashCombineFast(Seed, GetTypeHash(Left));
					const uint32 RightHash = HashCombineFast(Seed, GetTypeHash(Right));
					if (LeftHash != RightHash)
					{
						return LeftHash < RightHash;
					}

					return IsLexicographicallyEarlierCell(Left, Right);
				});
			}

			static TSet<FIntVector> BuildTerrainSteppedProtectedTraversalCells(
				const TArray<FTerrainSteppedTransitionRequirement>& TransitionRequirements)
			{
				TSet<FIntVector> ProtectedCells;
				for (const FTerrainSteppedTransitionRequirement& Requirement : TransitionRequirements)
				{
					for (const FIntVector& RequiredCell : Requirement.RequiredTraversalCells)
					{
						ProtectedCells.Add(RequiredCell);
					}
				}
				return ProtectedCells;
			}

			static TSet<FIntVector> BuildProtectedRootStructuralCells(
				const FRecursiveScheduleSolveContext& SolveContext,
				const TSet<FIntVector>* AdditionalProtectedCells = nullptr)
			{
				TSet<FIntVector> ProtectedCells = SolveContext.ProtectedParentTraversalCells;
				if (AdditionalProtectedCells != nullptr)
				{
					for (const FIntVector& AdditionalCell : *AdditionalProtectedCells)
					{
						ProtectedCells.Add(AdditionalCell);
					}
				}
				for (const FLayoutCommittedEndpointAnchor& Anchor : SolveContext.RootExternalEndpointCommitments)
				{
					ProtectedCells.Add(Anchor.LocalCell);
				}
				return ProtectedCells;
			}

			static TArray<FCompiledReservedOpenOverlay> BuildRootReservedOpenOverlays(
				const FRecursiveScheduleSolveContext& SolveContext,
				const TSet<FIntVector>* AdditionalProtectedCells = nullptr)
			{
				const TSet<FIntVector> ProtectedCells = BuildProtectedRootStructuralCells(
					SolveContext,
					AdditionalProtectedCells);
				TSet<FIntVector> ReservedCells;
				TArray<FCompiledReservedOpenOverlay> Overlays;
				Overlays.Reserve(SolveContext.RootRequest.ProfileSnapshot.ReservedOpenSpaceRules.Num());
				for (const FLayoutReservedOpenSpaceRule& Rule : SolveContext.RootRequest.ProfileSnapshot.ReservedOpenSpaceRules)
				{
					TArray<FIntVector> EligibleCells;
					for (const FLayoutPlannedCell& PlannedCell : SolveContext.ParentPlannedCells)
					{
						if (ReservedCells.Contains(PlannedCell.Cell)
							|| ProtectedCells.Contains(PlannedCell.Cell)
							|| PlannedCell.Intent == ELayoutCellIntent::Entry
							|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
							|| PlannedCell.Intent == ELayoutCellIntent::Connector
							|| !DoesCellMatchPlacementZoneInFootprint(
								PlannedCell.Cell,
								SolveContext.ParentFootprintSize,
								Rule.PlacementZone)
							|| !DoesCellMatchAuthoredLevelScope(
								PlannedCell.Cell,
								SolveContext.RootRequest.ProfileSnapshot.LevelCount,
								Rule.LevelPlacementPolicy,
								Rule.SpecificLevel))
						{
							continue;
						}

						EligibleCells.Add(PlannedCell.Cell);
					}

					int32 MinimumReservedCount = 0;
					int32 MaximumReservedCount = 0;
					ResolveOpenCellRange(
						EligibleCells.Num(),
						Rule.ReservedPercent,
						Rule.ReservedPercent,
						Rule.MinReservedCells,
						Rule.MaxReservedCells,
						MinimumReservedCount,
						MaximumReservedCount);

					const int32 ReservedCount = MaximumReservedCount > 0
						? FMath::Clamp(MaximumReservedCount, MinimumReservedCount, EligibleCells.Num())
						: FMath::Clamp(MinimumReservedCount, 0, EligibleCells.Num());
					if (ReservedCount <= 0)
					{
						continue;
					}

					SortCellsDeterministicallyForOpenSelection(
						EligibleCells,
						HashCombine(GetTypeHash(Rule.RuleId), static_cast<uint32>(SolveContext.RootRequest.Seed)));

					FCompiledReservedOpenOverlay& Overlay = Overlays.AddDefaulted_GetRef();
					Overlay.RuleId = Rule.RuleId;
					for (int32 CellIndex = 0; CellIndex < ReservedCount && CellIndex < EligibleCells.Num(); ++CellIndex)
					{
						Overlay.ReservedCells.Add(EligibleCells[CellIndex]);
						ReservedCells.Add(EligibleCells[CellIndex]);
					}
					Overlay.ReservedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
					{
						return IsLexicographicallyEarlierCell(Left, Right);
					});
				}

				return Overlays;
			}

			static TArray<FLayoutPlannedCell> BuildFilteredRootPlannedCells(
				const TArray<FLayoutPlannedCell>& RootPlannedCells,
				const TArray<FCompiledReservedOpenOverlay>& ReservedOpenOverlays)
			{
				TSet<FIntVector> ReservedCells;
				for (const FCompiledReservedOpenOverlay& Overlay : ReservedOpenOverlays)
				{
					for (const FIntVector& ReservedCell : Overlay.ReservedCells)
					{
						ReservedCells.Add(ReservedCell);
					}
				}

				TArray<FLayoutPlannedCell> FilteredPlannedCells;
				FilteredPlannedCells.Reserve(RootPlannedCells.Num());
				for (const FLayoutPlannedCell& PlannedCell : RootPlannedCells)
				{
					if (!ReservedCells.Contains(PlannedCell.Cell))
					{
						FilteredPlannedCells.Add(PlannedCell);
					}
				}

				return FilteredPlannedCells;
			}

		static bool IsLexicographicallyEarlierLocalCellFaceRuleSnapshot(
			const FLayoutLocalCellFaceRuleSnapshot& Left,
			const FLayoutLocalCellFaceRuleSnapshot& Right)
		{
			return IsLexicographicallyEarlierCell(Left.LocalCell, Right.LocalCell);
		}

		static bool IsLexicographicallyEarlierInternalAccessLink(
			const FLayoutInternalAccessLink& Left,
			const FLayoutInternalAccessLink& Right)
		{
			const FString LeftFrom = Left.FromTraversalChannel.ToString();
			const FString RightFrom = Right.FromTraversalChannel.ToString();
			if (LeftFrom != RightFrom)
			{
				return LeftFrom < RightFrom;
			}

			const FString LeftTo = Left.ToTraversalChannel.ToString();
			const FString RightTo = Right.ToTraversalChannel.ToString();
			if (LeftTo != RightTo)
			{
				return LeftTo < RightTo;
			}

			return static_cast<int32>(Left.bBidirectional) < static_cast<int32>(Right.bBidirectional);
		}

		static FString TagsToStableKey(const FGameplayTagContainer& Tags)
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

		static FString RolesToStableKey(const TArray<ELayoutModuleRole>& Roles)
		{
			TArray<int32> SortedRoles;
			SortedRoles.Reserve(Roles.Num());
			for (const ELayoutModuleRole Role : Roles)
			{
				SortedRoles.Add(static_cast<int32>(Role));
			}
			SortedRoles.Sort();

			TArray<FString> Parts;
			Parts.Reserve(SortedRoles.Num());
			for (const int32 RoleValue : SortedRoles)
			{
				Parts.Add(LexToString(RoleValue));
			}

			return FString::Join(Parts, TEXT("|"));
		}

		static bool DoesPotentialPlacementZoneOverlapRequirementZone(
			const ELayoutPlacementZone EntryPlacementZone,
			const ELayoutPlacementZone RequirementZone)
		{
			return LayoutZoneFeatureDemand::DoPotentialPlacementZonesOverlap(
				EntryPlacementZone,
				RequirementZone);
		}

		static bool DoesPlannedCellMatchDemandPlacementScope(
			const FLayoutPlannedCell& PlannedCell,
			const FIntPoint& FootprintSize,
			const int32 LevelCount,
			const ELayoutPlacementZone PlacementZone,
			const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
			const int32 SpecificLevel)
		{
			return DoesCellMatchPlacementZoneInFootprint(
				PlannedCell.Cell,
				FootprintSize,
				PlacementZone)
				&& DoesCellMatchAuthoredLevelScope(
					PlannedCell.Cell,
					LevelCount,
					LevelPlacementPolicy,
					SpecificLevel);
		}

		static int32 BuildTerrainStageFrontierId(
			const FTerrainSteppedAscentFrontierAnnotation& Frontier,
			const int32 FrontierIndex)
		{
			return static_cast<int32>(HashCombineFast(
				static_cast<uint32>(FrontierIndex + 1),
				HashCombineFast(
					GetTypeHash(Frontier.FromCell),
					HashCombineFast(
						GetTypeHash(Frontier.ToCell),
						HashCombineFast(
							static_cast<uint32>(Frontier.LowerSnappedTerrainLevel),
							static_cast<uint32>(Frontier.HigherSnappedTerrainLevel))))));
		}

		static int32 BuildTerrainStageFrontierId(
			const FTerrainSteppedAscentFrontierAnnotation& Frontier,
			const int32 FrontierIndex);

		static void ResolveDemandTerrainStageEligibility(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
			const FChildCapabilitySummary& ChildSummary,
			ETerrainStageEligibility& OutTerrainStageEligibility,
			bool& OutRequiresDeferredActivation,
			bool& OutTerrainStageCanActivateEarlyIfUnlocksAscent,
			bool& OutTerrainStageActivatesOnCurrentFrontier,
			TOptional<int32>& OutUnlockingAscentFrontierId,
			int32& OutMinimumReachableLevel,
			int32& OutMinimumDeferredLevel)
		{
			OutTerrainStageEligibility = ETerrainStageEligibility::Active;
			OutRequiresDeferredActivation = false;
			OutTerrainStageCanActivateEarlyIfUnlocksAscent = false;
			OutTerrainStageActivatesOnCurrentFrontier = false;
			OutUnlockingAscentFrontierId.Reset();
			OutMinimumReachableLevel = INDEX_NONE;
			OutMinimumDeferredLevel = INDEX_NONE;

			const TArray<FTerrainSteppedReachableSurfaceAnnotation> SurfaceAnnotations =
				BuildRootTerrainSteppedReachableSurfaceAnnotations(
					SolveContext.ParentPlannedCells,
					SolveContext.SteppedTerrainSupportMap);
			if (SurfaceAnnotations.IsEmpty())
			{
				return;
			}

			const TArray<FTerrainSteppedTransitionRequirement> TransitionRequirements =
				BuildRootTerrainSteppedTransitionRequirements(
					SolveContext.ParentPlannedCells,
					SolveContext.ParentFootprintSize,
					SolveContext.WorldBindingPlacementPolicy,
					SolveContext.SteppedTerrainSupportMap,
					BuildModulePlacementBundles(SolveContext.RootRequest));
			const TArray<FTerrainSteppedAscentFrontierAnnotation> FrontierAnnotations =
				BuildRootTerrainSteppedAscentFrontierAnnotations(
					TransitionRequirements,
					SurfaceAnnotations);

			TMap<FIntVector, FLayoutPlannedCell> PlannedCellByCell;
			PlannedCellByCell.Reserve(SolveContext.ParentPlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : SolveContext.ParentPlannedCells)
			{
				PlannedCellByCell.Add(PlannedCell.Cell, PlannedCell);
			}

			bool bOverlapsReachable = false;
			bool bOverlapsDeferred = false;
			for (const FTerrainSteppedReachableSurfaceAnnotation& Annotation : SurfaceAnnotations)
			{
				const FLayoutPlannedCell* PlannedCell = PlannedCellByCell.Find(Annotation.Cell);
				if (PlannedCell == nullptr)
				{
					continue;
				}
				if (!DoesPlannedCellMatchDemandPlacementScope(
						*PlannedCell,
						SolveContext.ParentFootprintSize,
						SolveContext.RootRequest.ProfileSnapshot.LevelCount,
						EntrySnapshot.ChildPlacementZone,
						EntrySnapshot.ChildLevelPlacementPolicy,
						EntrySnapshot.ChildSpecificLevel))
				{
					continue;
				}

				if (Annotation.bReachablePlateau)
				{
					bOverlapsReachable = true;
					OutMinimumReachableLevel = OutMinimumReachableLevel == INDEX_NONE
						? Annotation.SnappedTerrainLevel
						: FMath::Min(OutMinimumReachableLevel, Annotation.SnappedTerrainLevel);
				}
				if (Annotation.bDeferredTerrace)
				{
					bOverlapsDeferred = true;
					OutMinimumDeferredLevel = OutMinimumDeferredLevel == INDEX_NONE
						? Annotation.SnappedTerrainLevel
						: FMath::Min(OutMinimumDeferredLevel, Annotation.SnappedTerrainLevel);
				}
			}

			OutTerrainStageEligibility = (!bOverlapsReachable && bOverlapsDeferred)
				? ETerrainStageEligibility::Deferred
				: ETerrainStageEligibility::Active;
			OutRequiresDeferredActivation = OutTerrainStageEligibility == ETerrainStageEligibility::Deferred;

			if (!OutRequiresDeferredActivation)
			{
				return;
			}

			const bool bSupportsUnlockingHostAscent =
				EntrySnapshot.bChildContributesHostVerticalAccess
				&& (ChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess
					|| ChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess);
			if (!bSupportsUnlockingHostAscent)
			{
				return;
			}

			for (int32 FrontierIndex = 0; FrontierIndex < FrontierAnnotations.Num(); ++FrontierIndex)
			{
				const FTerrainSteppedAscentFrontierAnnotation& Frontier = FrontierAnnotations[FrontierIndex];
				for (const FIntVector& PocketCell : Frontier.ProtectedAscentPocketCells)
				{
					const FLayoutPlannedCell* PlannedCell = PlannedCellByCell.Find(PocketCell);
					if (PlannedCell == nullptr)
					{
						continue;
					}
					if (!DoesPlannedCellMatchDemandPlacementScope(
							*PlannedCell,
							SolveContext.ParentFootprintSize,
							SolveContext.RootRequest.ProfileSnapshot.LevelCount,
							EntrySnapshot.ChildPlacementZone,
							EntrySnapshot.ChildLevelPlacementPolicy,
							EntrySnapshot.ChildSpecificLevel))
					{
						continue;
					}

					OutTerrainStageCanActivateEarlyIfUnlocksAscent = true;
					OutTerrainStageActivatesOnCurrentFrontier = true;
					OutUnlockingAscentFrontierId = Frontier.AscentFrontierId != INDEX_NONE
						? Frontier.AscentFrontierId
						: BuildTerrainStageFrontierId(Frontier, FrontierIndex);
					return;
				}
			}
		}

		static TMap<FLayoutId, int32> BuildRequiredChildPlacementInstanceCounts(
			const FLayoutRegionSolveRequest& RootRequest)
		{
			TArray<LayoutZoneFeatureDemand::FHardDemand> HardDemands;
			LayoutZoneFeatureDemand::CompileHardDemands(
				RootRequest.ProfileSnapshot.ZoneFeatureRequirements,
				HardDemands);
			TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary> ProviderChoices;
			LayoutZoneFeatureDemand::CompileProviderChoiceSummaries(
				RootRequest.EffectiveSnapshotId,
				HardDemands,
				RootRequest.ContentSetSnapshot.Entries,
				ProviderChoices);

			TMap<FLayoutId, int32> RequiredInstancesByEntryId;
			TSet<FLayoutId> HardGovernedEntryIds;
			for (const LayoutZoneFeatureDemand::FHardDemand& Demand : HardDemands)
			{
				TArray<FLayoutId> MatchingChildEntryIds;
				for (const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice : ProviderChoices)
				{
					if (Choice.RequirementId == Demand.RequirementId
						&& Choice.ContentKind == ELayoutRegionContentKind::ChildRegion)
					{
						MatchingChildEntryIds.AddUnique(Choice.SourceContentEntryId);
						HardGovernedEntryIds.Add(Choice.SourceContentEntryId);
					}
				}
				MatchingChildEntryIds.Sort([](const FLayoutId Left, const FLayoutId Right)
				{
					return Left.LexicalLess(Right);
				});
				if (Demand.MinCount > 0 && !MatchingChildEntryIds.IsEmpty())
				{
					int32& RequiredCount = RequiredInstancesByEntryId.FindOrAdd(
						MatchingChildEntryIds[0]);
					RequiredCount = FMath::Max(RequiredCount, Demand.MinCount);
				}
			}

			for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot :
				RootRequest.ContentSetSnapshot.Entries)
			{
				if (EntrySnapshot.ContentKind == ELayoutRegionContentKind::ChildRegion
					&& !HardGovernedEntryIds.Contains(EntrySnapshot.EntryId))
				{
					RequiredInstancesByEntryId.Add(EntrySnapshot.EntryId, 1);
				}
			}
			return RequiredInstancesByEntryId;
		}

		static FPlacementCapabilityBundle BuildPlacementBundleFromModuleSnapshot(
			const FLayoutRegionSolveRequest& Request,
			const FLayoutModuleSolveSnapshot& ModuleSnapshot,
			const int32 ModuleIndex)
		{
			FPlacementCapabilityBundle Bundle;
			Bundle.BundleId = FLayoutId(*FString::Printf(
				TEXT("%s.Bundle.%d.%s"),
				*Request.EffectiveSnapshotId.ToString(),
				ModuleIndex,
				*ModuleSnapshot.SnapshotId.ToString()));
			Bundle.bSupportsRootVerticalAccess = ModuleSnapshot.SupportsRootIntent(ELayoutCellIntent::VerticalAccess);
			Bundle.PlacementZone = ModuleSnapshot.PlacementZone;
			Bundle.LevelPlacementPolicy = ModuleSnapshot.LevelPlacementPolicy;
			Bundle.SpecificLevel = ModuleSnapshot.SpecificLevel;
			Bundle.OccupiedLocalCells = ModuleSnapshot.OccupiedLocalCells;
			Bundle.OccupiedLocalCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsLexicographicallyEarlierCell(Left, Right);
			});

			Bundle.ExposedFaceRules = ModuleSnapshot.GeneratedLocalCellFaceRules;
			Bundle.ExposedFaceRules.Sort([](
				const FLayoutLocalCellFaceRuleSnapshot& Left,
				const FLayoutLocalCellFaceRuleSnapshot& Right)
			{
				return IsLexicographicallyEarlierLocalCellFaceRuleSnapshot(Left, Right);
			});

			TSet<int32> CoveredLevelSet;
			TSet<FIntVector> OccupiedCellSet;
			for (const FIntVector& OccupiedCell : Bundle.OccupiedLocalCells)
			{
				CoveredLevelSet.Add(OccupiedCell.Z);
				OccupiedCellSet.Add(OccupiedCell);

				const FIntVector SupportSourceCell = OccupiedCell + FIntVector(0, 0, -1);
				if (!OccupiedCellSet.Contains(SupportSourceCell)
					&& !ModuleSnapshot.OccupiedLocalCells.Contains(SupportSourceCell))
				{
					Bundle.RequiredSupportCells.Add(OccupiedCell);
				}
			}

			Bundle.CoveredLevels.Reserve(CoveredLevelSet.Num());
			for (const int32 CoveredLevel : CoveredLevelSet)
			{
				Bundle.CoveredLevels.Add(CoveredLevel);
			}
			Bundle.CoveredLevels.Sort();

			Bundle.RequiredSupportCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsLexicographicallyEarlierCell(Left, Right);
			});

			Bundle.InternalAccessLinks = ModuleSnapshot.InternalAccessLinks;
			Bundle.InternalAccessLinks.Sort([](const FLayoutInternalAccessLink& Left, const FLayoutInternalAccessLink& Right)
			{
				return IsLexicographicallyEarlierInternalAccessLink(Left, Right);
			});

			Bundle.DerivedInternalTraversalLinks = ModuleSnapshot.DerivedInternalTraversalLinks;
			Bundle.DerivedInternalTraversalLinks.Sort([](
				const FLayoutDerivedInternalTraversalLink& Left,
				const FLayoutDerivedInternalTraversalLink& Right)
			{
				if (Left.FromLocalCell != Right.FromLocalCell)
				{
					return IsLexicographicallyEarlierCell(Left.FromLocalCell, Right.FromLocalCell);
				}
				if (Left.ToLocalCell != Right.ToLocalCell)
				{
					return IsLexicographicallyEarlierCell(Left.ToLocalCell, Right.ToLocalCell);
				}
				if (Left.FromTraversalChannel != Right.FromTraversalChannel)
				{
					return Left.FromTraversalChannel.ToString() < Right.FromTraversalChannel.ToString();
				}
				if (Left.ToTraversalChannel != Right.ToTraversalChannel)
				{
					return Left.ToTraversalChannel.ToString() < Right.ToTraversalChannel.ToString();
				}
				if (Left.bBidirectional != Right.bBidirectional)
				{
					return Left.bBidirectional && !Right.bBidirectional;
				}
				return Left.LinkId.LexicalLess(Right.LinkId);
			});
			return Bundle;
		}

		static TArray<FChildVerticalAccessSupportBand> BuildChildVerticalAccessSupportBandsByLevel(
			const TArray<FPlacementCapabilityBundle>& PlacementBundles,
			const TArray<FIntVector>& VerticalAccessCells)
		{
			TMap<int32, TSet<FIntVector>> ProvingCellsByLevel;

			auto AddProvingCellForLevel =
				[&ProvingCellsByLevel](const int32 LevelIndex, const FIntVector& ProvingCell)
			{
				ProvingCellsByLevel.FindOrAdd(LevelIndex).Add(ProvingCell);
			};

			for (const FIntVector& VerticalAccessCell : VerticalAccessCells)
			{
				AddProvingCellForLevel(VerticalAccessCell.Z, VerticalAccessCell);
			}

			for (const FPlacementCapabilityBundle& Bundle : PlacementBundles)
			{
				TArray<FIntVector> BundleProvingCells;
				for (const FIntVector& VerticalAccessCell : VerticalAccessCells)
				{
					if (Bundle.OccupiedLocalCells.Contains(VerticalAccessCell))
					{
						BundleProvingCells.Add(VerticalAccessCell);
					}
				}

				if (BundleProvingCells.IsEmpty())
				{
					continue;
				}

				for (const int32 LevelIndex : Bundle.CoveredLevels)
				{
					for (const FIntVector& ProvingCell : BundleProvingCells)
					{
						AddProvingCellForLevel(LevelIndex, ProvingCell);
					}
				}
			}

			TArray<FChildVerticalAccessSupportBand> SupportBands;
			SupportBands.Reserve(ProvingCellsByLevel.Num());
			for (TPair<int32, TSet<FIntVector>>& Pair : ProvingCellsByLevel)
			{
				FChildVerticalAccessSupportBand& Band = SupportBands.AddDefaulted_GetRef();
				Band.LevelIndex = Pair.Key;
				for (const FIntVector& ProvingCell : Pair.Value)
				{
					Band.ProvingVerticalAccessCells.Add(ProvingCell);
				}
				Band.ProvingVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
				{
					return IsLexicographicallyEarlierCell(Left, Right);
				});
			}

			SupportBands.Sort([](const FChildVerticalAccessSupportBand& Left, const FChildVerticalAccessSupportBand& Right)
			{
				return Left.LevelIndex < Right.LevelIndex;
			});
			return SupportBands;
		}

		static TArray<FChildLevelEndpointCapabilitySummary> BuildChildEndpointOffersByLevel(
			const TArray<FLayoutChildCapabilityEndpoint>& EndpointOffers)
		{
			TMap<int32, TArray<FLayoutChildCapabilityEndpoint>> EndpointOffersByLevel;
			for (const FLayoutChildCapabilityEndpoint& EndpointOffer : EndpointOffers)
			{
				EndpointOffersByLevel.FindOrAdd(EndpointOffer.LocalCell.Z).Add(EndpointOffer);
			}

			TArray<FChildLevelEndpointCapabilitySummary> Summaries;
			Summaries.Reserve(EndpointOffersByLevel.Num());
			for (TPair<int32, TArray<FLayoutChildCapabilityEndpoint>>& Pair : EndpointOffersByLevel)
			{
				FChildLevelEndpointCapabilitySummary& Summary = Summaries.AddDefaulted_GetRef();
				Summary.LevelIndex = Pair.Key;
				Summary.EndpointOffers = Pair.Value;
				Summary.EndpointOffers.Sort([](const FLayoutChildCapabilityEndpoint& Left, const FLayoutChildCapabilityEndpoint& Right)
				{
					return IsLexicographicallyEarlierChildCapabilityEndpoint(Left, Right);
				});
			}

			Summaries.Sort([](const FChildLevelEndpointCapabilitySummary& Left, const FChildLevelEndpointCapabilitySummary& Right)
			{
				return Left.LevelIndex < Right.LevelIndex;
			});
			return Summaries;
		}

		static TArray<FChildLevelSeamCapabilitySummary> BuildChildSeamOffersByLevel(
			const TArray<FLayoutChildCapabilitySeam>& SeamOffers)
		{
			TMap<int32, TArray<FLayoutChildCapabilitySeam>> SeamOffersByLevel;
			for (const FLayoutChildCapabilitySeam& SeamOffer : SeamOffers)
			{
				SeamOffersByLevel.FindOrAdd(SeamOffer.LocalCell.Z).Add(SeamOffer);
			}

			TArray<FChildLevelSeamCapabilitySummary> Summaries;
			Summaries.Reserve(SeamOffersByLevel.Num());
			for (TPair<int32, TArray<FLayoutChildCapabilitySeam>>& Pair : SeamOffersByLevel)
			{
				FChildLevelSeamCapabilitySummary& Summary = Summaries.AddDefaulted_GetRef();
				Summary.LevelIndex = Pair.Key;
				Summary.SeamOffers = Pair.Value;
				Summary.SeamOffers.Sort([](const FLayoutChildCapabilitySeam& Left, const FLayoutChildCapabilitySeam& Right)
				{
					return IsLexicographicallyEarlierChildCapabilitySeam(Left, Right);
				});
			}

			Summaries.Sort([](const FChildLevelSeamCapabilitySummary& Left, const FChildLevelSeamCapabilitySummary& Right)
			{
				return Left.LevelIndex < Right.LevelIndex;
			});
			return Summaries;
		}

		static TArray<FChildLevelSpanCapabilitySummary> BuildChildSpanOffersByLevel(
			const TArray<FLayoutChildCapabilitySpan>& SpanOffers)
		{
			TMap<int32, TArray<FLayoutChildCapabilitySpan>> SpanOffersByLevel;
			for (const FLayoutChildCapabilitySpan& SpanOffer : SpanOffers)
			{
				SpanOffersByLevel.FindOrAdd(SpanOffer.LocalCell.Z).Add(SpanOffer);
			}

			TArray<FChildLevelSpanCapabilitySummary> Summaries;
			Summaries.Reserve(SpanOffersByLevel.Num());
			for (TPair<int32, TArray<FLayoutChildCapabilitySpan>>& Pair : SpanOffersByLevel)
			{
				FChildLevelSpanCapabilitySummary& Summary = Summaries.AddDefaulted_GetRef();
				Summary.LevelIndex = Pair.Key;
				Summary.SpanOffers = Pair.Value;
				Summary.SpanOffers.Sort([](const FLayoutChildCapabilitySpan& Left, const FLayoutChildCapabilitySpan& Right)
				{
					return IsLexicographicallyEarlierChildCapabilitySpan(Left, Right);
				});
			}

			Summaries.Sort([](const FChildLevelSpanCapabilitySummary& Left, const FChildLevelSpanCapabilitySummary& Right)
			{
				return Left.LevelIndex < Right.LevelIndex;
			});
			return Summaries;
		}

			static TArray<FChildLevelTraversalCapabilitySummary> BuildChildTraversalSummariesByLevel(
				const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
				const TArray<FPlacementCapabilityBundle>& PlacementBundles,
				const TArray<FIntVector>& VerticalAccessCells)
			{
				auto DoesBundleCarryVerticalAccess =
					[](const FPlacementCapabilityBundle& Bundle, const TArray<FIntVector>& InVerticalAccessCells)
				{
					return Bundle.OccupiedLocalCells.ContainsByPredicate(
						[&InVerticalAccessCells](const FIntVector& OccupiedCell)
						{
							return InVerticalAccessCells.Contains(OccupiedCell);
						});
				};

			auto DoTraversalChannelsOverlap =
				[](const FGameplayTagContainer& LeftChannels, const FGameplayTagContainer& RightChannels)
			{
				return LeftChannels.HasAnyExact(RightChannels);
			};

			auto BuildAnchorPairId =
				[](const FLayoutChildCapabilityEndpoint& Left, const FLayoutChildCapabilityEndpoint& Right)
			{
				const FLayoutId FirstId = Left.CapabilityId.LexicalLess(Right.CapabilityId)
					? Left.CapabilityId
					: Right.CapabilityId;
				const FLayoutId SecondId = Left.CapabilityId.LexicalLess(Right.CapabilityId)
					? Right.CapabilityId
					: Left.CapabilityId;
				return FLayoutId(*FString::Printf(TEXT("%s__%s"), *FirstId.ToString(), *SecondId.ToString()));
			};

			TMap<int32, FChildLevelTraversalCapabilitySummary> SummariesByLevel;
			// Membership only: retain first-seen spelling and the existing final lexical order.
			TMap<int32, TSet<FLayoutId>> SeenPairIdsByLevel;

			auto FindOrAddSummary = [&SummariesByLevel](const int32 LevelIndex) -> FChildLevelTraversalCapabilitySummary&
			{
				FChildLevelTraversalCapabilitySummary& Summary = SummariesByLevel.FindOrAdd(LevelIndex);
				Summary.LevelIndex = LevelIndex;
				return Summary;
			};

				for (const FPlacementCapabilityBundle& Bundle : PlacementBundles)
				{
					const bool bBundleCarriesVerticalAccess =
						DoesBundleCarryVerticalAccess(Bundle, VerticalAccessCells);
					for (const int32 LevelIndex : Bundle.CoveredLevels)
					{
						FChildLevelTraversalCapabilitySummary& Summary = FindOrAddSummary(LevelIndex);
						Summary.bCanCarryHostVerticalAccess |= bBundleCarriesVerticalAccess;
					}
				}

			for (const FIntVector& VerticalAccessCell : VerticalAccessCells)
			{
				FChildLevelTraversalCapabilitySummary& Summary = FindOrAddSummary(VerticalAccessCell.Z);
				Summary.bCanCarryHostVerticalAccess = true;
			}

			for (const FLayoutChildCapabilityEndpoint& Endpoint : CapabilityEnvelope.EndpointCapabilities)
			{
				FChildLevelTraversalCapabilitySummary& Summary = FindOrAddSummary(Endpoint.LocalCell.Z);
				Summary.TraversalChannels.AppendTags(Endpoint.TraversalChannels);
				Summary.bCanExposeHostIngress = true;
				Summary.bCanExposeHostEgress = true;
			}

			for (int32 LeftIndex = 0; LeftIndex < CapabilityEnvelope.EndpointCapabilities.Num(); ++LeftIndex)
			{
				const FLayoutChildCapabilityEndpoint& LeftEndpoint =
					CapabilityEnvelope.EndpointCapabilities[LeftIndex];
				for (int32 RightIndex = LeftIndex + 1; RightIndex < CapabilityEnvelope.EndpointCapabilities.Num(); ++RightIndex)
				{
					const FLayoutChildCapabilityEndpoint& RightEndpoint =
						CapabilityEnvelope.EndpointCapabilities[RightIndex];
					if (!DoTraversalChannelsOverlap(
							LeftEndpoint.TraversalChannels,
							RightEndpoint.TraversalChannels))
					{
						continue;
					}

					const int32 MinimumLevel = FMath::Min(LeftEndpoint.LocalCell.Z, RightEndpoint.LocalCell.Z);
					const int32 MaximumLevel = FMath::Max(LeftEndpoint.LocalCell.Z, RightEndpoint.LocalCell.Z);
					bool bLevelsGenerallyConnectable = true;
					if (MinimumLevel != MaximumLevel)
					{
						for (int32 LevelIndex = MinimumLevel; LevelIndex <= MaximumLevel; ++LevelIndex)
						{
							const FChildLevelTraversalCapabilitySummary* Summary = SummariesByLevel.Find(LevelIndex);
							if (Summary == nullptr || !Summary->bCanCarryHostVerticalAccess)
							{
								bLevelsGenerallyConnectable = false;
								break;
							}
						}
					}

					if (!bLevelsGenerallyConnectable)
					{
						continue;
					}

					const FLayoutId AnchorPairId = BuildAnchorPairId(LeftEndpoint, RightEndpoint);
					for (int32 LevelIndex = MinimumLevel; LevelIndex <= MaximumLevel; ++LevelIndex)
					{
						FChildLevelTraversalCapabilitySummary& Summary = FindOrAddSummary(LevelIndex);
						if (!AnchorPairId.IsNone())
						{
							bool bAlreadyPresent = false;
							SeenPairIdsByLevel.FindOrAdd(LevelIndex).Add(AnchorPairId, &bAlreadyPresent);
							if (!bAlreadyPresent) Summary.ConnectableAnchorPairIds.Add(AnchorPairId);
						}
					}
				}
			}

			TArray<FChildLevelTraversalCapabilitySummary> Summaries;
			SummariesByLevel.GenerateValueArray(Summaries);
			Summaries.Sort([](const FChildLevelTraversalCapabilitySummary& A, const FChildLevelTraversalCapabilitySummary& B)
			{
				return A.LevelIndex < B.LevelIndex;
			});
			for (FChildLevelTraversalCapabilitySummary& Summary : Summaries)
			{
				Summary.ConnectableAnchorPairIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
				{
					return Left.LexicalLess(Right);
				});
			}
			return Summaries;
		}

		static FLayoutRegionSolveRequest BuildChildRequestFromTemplate(
			const FLayoutChildRequestTemplateSnapshot& Template,
			const int32 Seed,
			const FString& RegionDebugPath,
			const FName SourceContentEntryId,
			const FString& SourceParentRegionDebugPath,
			const bool bSourceContentEntryOptional,
			const FLayoutSolverExecutionSettings& ExecutionSettings)
		{
			FLayoutRegionSolveRequest Request;
			Request.EffectiveSnapshotId = FLayoutId(*FString::Printf(
				TEXT("%s__%s__%s__Seams%d"),
				*RegionDebugPath,
				*Template.ProfileSnapshot.DebugName.ToString(),
				*Template.ContentSetSnapshot.DebugName.ToString(),
				(Template.ProfileSnapshot.bSupportsSteppedTerrainSolve
					&& Template.ProfileSnapshot.bEnableTerrainSeams) ? 1 : 0));
			Request.SnapshotSchemaVersion = Template.SnapshotSchemaVersion;
			Request.RegionDebugPath = RegionDebugPath;
			Request.Seed = Seed;
			Request.SourceContentEntryId = SourceContentEntryId;
			Request.ProfilePath = Template.ChildProfilePath;
			Request.SourceParentRegionDebugPath = SourceParentRegionDebugPath;
			Request.bSourceContentEntryOptional = bSourceContentEntryOptional;
			Request.ExecutionSettings = ExecutionSettings;
			Request.ModuleCatalog = Template.ModuleCatalog;
			Request.ContentSetSnapshot = Template.ContentSetSnapshot;
			Request.ProfileSnapshot = Template.ProfileSnapshot;
			Request.ProofRecords = Template.ProofRecords;
			Request.ValidationAssertions = Template.ValidationAssertions;
			Request.ValidationAssertions.Add(MakeCapabilityAssertionRecord(
				TEXT("RegionRequest.SnapshotContractInitialized"),
				ELayoutValidationAssertionKind::SnapshotContractInitialized,
				Request.EffectiveSnapshotId != NAME_None,
				{Request.ProfileSnapshot.SnapshotId, Request.ContentSetSnapshot.SnapshotId, Request.ModuleCatalog.SnapshotId},
				Request.EffectiveSnapshotId != NAME_None
					? FString()
					: TEXT("Region solve request failed to initialize an effective snapshot id from the compiled child request template.")));
			return Request;
		}

		static TArray<FIntVector> BuildVerticalAccessCellsFromRequest(
			const FLayoutRegionSolveRequest& ChildRequest)
		{
			TSet<FIntVector> UniqueVerticalAccessCells;
			for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ChildRequest.ModuleCatalog.Modules)
			{
				for (const FLayoutDerivedVerticalAccessContract& Contract : ModuleSnapshot.DerivedVerticalAccessContracts)
				{
					UniqueVerticalAccessCells.Add(Contract.LocalCell);
				}
			}

			TArray<FIntVector> VerticalAccessCells;
			VerticalAccessCells.Reserve(UniqueVerticalAccessCells.Num());
			for (const FIntVector& VerticalAccessCell : UniqueVerticalAccessCells)
			{
				VerticalAccessCells.Add(VerticalAccessCell);
			}
			VerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsLexicographicallyEarlierCell(Left, Right);
			});
			return VerticalAccessCells;
		}

			static FLayoutChildCapabilityEnvelope BuildChildCapabilityEnvelopeFromRequestInternal(
				const FLayoutRegionSolveRequest& ChildRequest)
		{
			FLayoutChildCapabilityEnvelope Envelope;
			Envelope.RegionDebugPath = ChildRequest.RegionDebugPath;
			Envelope.SnapshotId = ChildRequest.EffectiveSnapshotId != NAME_None
				? ChildRequest.EffectiveSnapshotId
				: ChildRequest.ModuleCatalog.SnapshotId;

			TSet<FString> SeenEndpointCapabilityKeys;
			TSet<FString> SeenSpanCapabilityKeys;
			TSet<FString> SeenSeamCapabilityKeys;
			for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ChildRequest.ModuleCatalog.Modules)
			{
				TArray<int32> AllowedYawRotationSteps = ModuleSnapshot.AllowedYawRotationSteps;
				if (AllowedYawRotationSteps.IsEmpty())
				{
					AllowedYawRotationSteps.Add(0);
				}

				for (const FLayoutDerivedEndpointOffer& Offer : ModuleSnapshot.DerivedEndpointOffers)
				{
					for (const int32 YawRotationSteps : AllowedYawRotationSteps)
					{
						const ELayoutFaceDirection RotatedFaceDirection =
							FLayoutDirectionUtils::RotateYaw(Offer.FaceDirection, YawRotationSteps);
							const FString CapabilityKey = FString::Printf(
								TEXT("%d|%s|%s|%s|%s"),
								static_cast<int32>(RotatedFaceDirection),
								*Offer.ConnectionTag.ToString(),
								*TagsToStableKey(Offer.AllowedConnectionTags),
								*TagsToStableKey(Offer.TraversalChannels),
								*RolesToStableKey(Offer.Roles));
						if (SeenEndpointCapabilityKeys.Contains(CapabilityKey))
						{
							continue;
						}

						SeenEndpointCapabilityKeys.Add(CapabilityKey);
						FLayoutChildCapabilityEndpoint& Capability =
							Envelope.EndpointCapabilities.AddDefaulted_GetRef();
						Capability.CapabilityId = FLayoutId(*FString::Printf(
							TEXT("%s.Capability.%d"),
							*ChildRequest.RegionDebugPath,
							Envelope.EndpointCapabilities.Num() - 1));
						Capability.LocalCell = Offer.LocalCell;
						Capability.FaceDirection = RotatedFaceDirection;
						Capability.ConnectionTag = Offer.ConnectionTag;
						Capability.AllowedConnectionTags = Offer.AllowedConnectionTags;
						Capability.TraversalChannels = Offer.TraversalChannels;
						Capability.Roles = Offer.Roles;
						Envelope.ProofRecords.Add(MakeCapabilityProofRecord(
							FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Capability.CapabilityId.ToString())),
							ELayoutProofKind::ChildCapability,
							Capability.CapabilityId,
							{ModuleSnapshot.SnapshotId, Offer.OfferId},
							FString::Printf(
								TEXT("Child capability '%s' is derived from module snapshot '%s' endpoint offer '%s' rotated by %d yaw step(s)."),
								*Capability.CapabilityId.ToString(),
								*ModuleSnapshot.SnapshotId.ToString(),
								*Offer.OfferId.ToString(),
								YawRotationSteps)));
					}
				}

				for (const FLayoutClosureProviderIntent& ProviderIntent : ModuleSnapshot.ClosureProviderIntents)
				{
					if (ProviderIntent.Zone != ELayoutPlacementZone::Perimeter)
					{
						continue;
					}

					for (const FLayoutDerivedSpanOffer& SpanOffer : ModuleSnapshot.DerivedSpanOffers)
					{
						if (!SpanOffer.ClosureId.IsNone()
							&& !ProviderIntent.ClosureId.IsNone()
							&& SpanOffer.ClosureId != ProviderIntent.ClosureId)
						{
							continue;
						}

						const FName EffectiveClosureId = !SpanOffer.ClosureId.IsNone()
							? SpanOffer.ClosureId
							: ProviderIntent.ClosureId;
						for (const int32 YawRotationSteps : AllowedYawRotationSteps)
						{
							const ELayoutFaceDirection RotatedFaceDirection =
								FLayoutDirectionUtils::RotateYaw(SpanOffer.FaceDirection, YawRotationSteps);
							const FString CapabilityKey = FString::Printf(
								TEXT("%s|%d|%d|%d|%s|%d|%d|%d"),
								*EffectiveClosureId.ToString(),
								static_cast<int32>(RotatedFaceDirection),
								SpanOffer.ThicknessCells,
								SpanOffer.bSealsBoundary ? 1 : 0,
								*RolesToStableKey(SpanOffer.Roles),
								SpanOffer.LocalCell.X,
								SpanOffer.LocalCell.Y,
								SpanOffer.LocalCell.Z);
							if (SeenSpanCapabilityKeys.Contains(CapabilityKey))
							{
								continue;
							}

							SeenSpanCapabilityKeys.Add(CapabilityKey);
							FLayoutChildCapabilitySpan& Capability =
								Envelope.SpanCapabilities.AddDefaulted_GetRef();
							Capability.CapabilityId = FLayoutId(*FString::Printf(
								TEXT("%s.SpanCapability.%d"),
								*ChildRequest.RegionDebugPath,
								Envelope.SpanCapabilities.Num() - 1));
							Capability.LocalCell = SpanOffer.LocalCell;
							Capability.ClosureId = EffectiveClosureId;
							Capability.FaceDirection = RotatedFaceDirection;
							Capability.Roles = SpanOffer.Roles;
							Capability.ThicknessCells = SpanOffer.ThicknessCells;
							Capability.bSealsBoundary = SpanOffer.bSealsBoundary;
							Envelope.ProofRecords.Add(MakeCapabilityProofRecord(
								FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Capability.CapabilityId.ToString())),
								ELayoutProofKind::ChildCapability,
								Capability.CapabilityId,
								{ModuleSnapshot.SnapshotId, SpanOffer.SpanOfferId, ProviderIntent.ProviderIntentId},
								FString::Printf(
									TEXT("Child span capability '%s' is derived from module snapshot '%s' span offer '%s', provider intent '%s', and %d yaw step(s)."),
									*Capability.CapabilityId.ToString(),
									*ModuleSnapshot.SnapshotId.ToString(),
									*SpanOffer.SpanOfferId.ToString(),
									*ProviderIntent.ProviderIntentId.ToString(),
									YawRotationSteps)));
						}
					}
				}

				for (const FLayoutSeamProviderIntent& SeamIntent : ModuleSnapshot.SeamProviderIntents)
				{
					if (!SeamIntent.InterfaceFamily.IsValid())
					{
						continue;
					}

					for (const FLayoutDerivedSpanOffer& SpanOffer : ModuleSnapshot.DerivedSpanOffers)
					{
						for (const int32 YawRotationSteps : AllowedYawRotationSteps)
						{
							const ELayoutFaceDirection RotatedFaceDirection =
								FLayoutDirectionUtils::RotateYaw(SpanOffer.FaceDirection, YawRotationSteps);
							const FString CapabilityKey = FString::Printf(
								TEXT("%s|%d|%d|%d|%d|%d|%d|%d"),
								*SeamIntent.InterfaceFamily.ToString(),
								static_cast<int32>(SeamIntent.JunctionUsage),
								static_cast<int32>(RotatedFaceDirection),
								SeamIntent.bCanOwnSeam ? 1 : 0,
								SeamIntent.bCanAcceptSeam ? 1 : 0,
								SpanOffer.LocalCell.X,
								SpanOffer.LocalCell.Y,
								SpanOffer.LocalCell.Z);
							if (SeenSeamCapabilityKeys.Contains(CapabilityKey))
							{
								continue;
							}

							SeenSeamCapabilityKeys.Add(CapabilityKey);
							FLayoutChildCapabilitySeam& Capability =
								Envelope.SeamCapabilities.AddDefaulted_GetRef();
							Capability.CapabilityId = FLayoutId(*FString::Printf(
								TEXT("%s.SeamCapability.%d"),
								*ChildRequest.RegionDebugPath,
								Envelope.SeamCapabilities.Num() - 1));
							Capability.LocalCell = SpanOffer.LocalCell;
							Capability.InterfaceFamily = SeamIntent.InterfaceFamily;
							Capability.JunctionUsage = SeamIntent.JunctionUsage;
							Capability.FaceDirection = RotatedFaceDirection;
							Capability.bCanOwnSeam = SeamIntent.bCanOwnSeam;
							Capability.bCanAcceptSeam = SeamIntent.bCanAcceptSeam;
							Envelope.ProofRecords.Add(MakeCapabilityProofRecord(
								FLayoutId(*FString::Printf(TEXT("%s.Proof"), *Capability.CapabilityId.ToString())),
								ELayoutProofKind::ChildCapability,
								Capability.CapabilityId,
								{ModuleSnapshot.SnapshotId, SpanOffer.SpanOfferId, SeamIntent.SeamIntentId},
								FString::Printf(
									TEXT("Child seam capability '%s' is derived from module snapshot '%s' span offer '%s', seam intent '%s', and %d yaw step(s)."),
									*Capability.CapabilityId.ToString(),
									*ModuleSnapshot.SnapshotId.ToString(),
									*SpanOffer.SpanOfferId.ToString(),
									*SeamIntent.SeamIntentId.ToString(),
									YawRotationSteps)));
						}
					}
				}
			}

			Envelope.ValidationAssertions.Add(MakeCapabilityAssertionRecord(
				FLayoutId(*FString::Printf(TEXT("%s.ChildCapabilityEndpointContract"), *ChildRequest.RegionDebugPath)),
				ELayoutValidationAssertionKind::ChildCapabilityContractValid,
				true,
				{ChildRequest.ModuleCatalog.SnapshotId, ChildRequest.ProfileSnapshot.SnapshotId},
				FString()));
			return Envelope;
		}
	}

	FCompiledStructuralInputs BuildCompiledStructuralInputs(
		const FRecursiveScheduleSolveContext& SolveContext)
	{
			FCompiledStructuralInputs StructuralInputs;
			StructuralInputs.RootSteppedTerrainSupportMap = SolveContext.SteppedTerrainSupportMap;
			StructuralInputs.RootVerticalAccessHostGroups = SolveContext.RootRequest.VerticalAccessHostGroups;
			StructuralInputs.RootPlacementBundles = BuildModulePlacementBundles(SolveContext.RootRequest);
			const TArray<FTerrainSteppedTransitionRequirement> PreProtectedTransitionRequirements =
				BuildRootTerrainSteppedTransitionRequirements(
					SolveContext.ParentPlannedCells,
					SolveContext.ParentFootprintSize,
					SolveContext.WorldBindingPlacementPolicy,
					StructuralInputs.RootSteppedTerrainSupportMap,
					StructuralInputs.RootPlacementBundles);
			const TSet<FIntVector> SteppedProtectedTraversalCells =
				BuildTerrainSteppedProtectedTraversalCells(PreProtectedTransitionRequirements);
			StructuralInputs.RootReservedOpenOverlays = BuildRootReservedOpenOverlays(
				SolveContext,
				&SteppedProtectedTraversalCells);
			StructuralInputs.RootPlannedCells = BuildFilteredRootPlannedCells(
				SolveContext.ParentPlannedCells,
				StructuralInputs.RootReservedOpenOverlays);
			StructuralInputs.RootPlannedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
			{
				return IsLexicographicallyEarlierCell(Left.Cell, Right.Cell);
			});
			StructuralInputs.RootFootprintSize = SolveContext.ParentFootprintSize;
			StructuralInputs.RootTerrainSteppedTransitionRequirements =
				BuildRootTerrainSteppedTransitionRequirements(
					StructuralInputs.RootPlannedCells,
					StructuralInputs.RootFootprintSize,
					SolveContext.WorldBindingPlacementPolicy,
					StructuralInputs.RootSteppedTerrainSupportMap,
					StructuralInputs.RootPlacementBundles);
			StructuralInputs.RootTerrainSteppedReachableSurfaceAnnotations =
				BuildRootTerrainSteppedReachableSurfaceAnnotations(
					StructuralInputs.RootPlannedCells,
					StructuralInputs.RootSteppedTerrainSupportMap);
			StructuralInputs.RootTerrainSteppedAscentFrontierAnnotations =
				BuildRootTerrainSteppedAscentFrontierAnnotations(
					StructuralInputs.RootTerrainSteppedTransitionRequirements,
					StructuralInputs.RootTerrainSteppedReachableSurfaceAnnotations);
			const TSet<FIntVector> FinalSteppedProtectedTraversalCells =
				BuildTerrainSteppedProtectedTraversalCells(
					StructuralInputs.RootTerrainSteppedTransitionRequirements);
			TSet<FIntVector> EffectiveProtectedTraversalCells =
				SolveContext.ProtectedParentTraversalCells;
			for (const FIntVector& ProtectedCell : FinalSteppedProtectedTraversalCells)
			{
				EffectiveProtectedTraversalCells.Add(ProtectedCell);
			}
		for (const FIntVector& ProtectedCell : EffectiveProtectedTraversalCells)
		{
			StructuralInputs.RootProtectedTraversalCells.Add(ProtectedCell);
		}
		StructuralInputs.RootProtectedTraversalCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return IsLexicographicallyEarlierCell(Left, Right);
		});
		StructuralInputs.RootExternalEndpointCommitments = SolveContext.RootExternalEndpointCommitments;
			StructuralInputs.RootExternalEndpointCommitments.Sort([](
				const FLayoutCommittedEndpointAnchor& Left,
				const FLayoutCommittedEndpointAnchor& Right)
			{
				return IsLexicographicallyEarlierCommittedEndpointAnchor(Left, Right);
			});
			for (const FTerrainSteppedTransitionRequirement& Requirement : StructuralInputs.RootTerrainSteppedTransitionRequirements)
			{
				if (!DoesTerrainSteppedTransitionHaveRootSupport(Requirement))
				{
					StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.Add(Requirement);
				}
			}
			StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions =
				StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.IsEmpty();
			StructuralInputs.VerticalAccessCountMode =
				SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCountMode;
			StructuralInputs.ProfileVerticalAccessCount =
				SolveContext.RootRequest.ProfileSnapshot.VerticalAccessCount;
			StructuralInputs.ProfileMinVerticalAccessCount =
				SolveContext.RootRequest.ProfileSnapshot.MinVerticalAccessCount;
			StructuralInputs.ProfileMaxVerticalAccessCount =
				SolveContext.RootRequest.ProfileSnapshot.MaxVerticalAccessCount;
			StructuralInputs.ProfileSeed = SolveContext.RootRequest.Seed;
			StructuralInputs.Demands = BuildNegotiationDemandPlans(SolveContext);
			return StructuralInputs;
		}

	FTerrainSteppedRejectionSummary BuildRootTerrainSteppedRejectionSummary(
		const FCompiledStructuralInputs& StructuralInputs)
	{
		FTerrainSteppedRejectionSummary Summary;
		if (StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements.IsEmpty())
		{
			return Summary;
		}

		Summary.bHasUnsupportedTransition = true;
		Summary.UnsupportedTransition = StructuralInputs.RootUnsupportedTerrainSteppedTransitionRequirements[0];

		const FTerrainSteppedTransitionRequirement& Requirement = Summary.UnsupportedTransition;
		const FString TransitionSummary = FString::Printf(
			TEXT("planned ascent (%d,%d,%d) -> (%d,%d,%d) with cell height %d"),
			Requirement.FromCell.X,
			Requirement.FromCell.Y,
			Requirement.FromCell.Z,
			Requirement.ToCell.X,
			Requirement.ToCell.Y,
			Requirement.ToCell.Z,
			Requirement.StepHeightBlocks);

		switch (Requirement.RootSupportStatus)
		{
		case ETerrainSteppedTransitionRootSupportStatus::MissingAdjacentVerticalAccessIntent:
			Summary.FailureReason = FString::Printf(
				TEXT("Root stepped-terrain solve cannot satisfy %s because its VerticalAccess cell has no directly-above planned landing cell."),
				*TransitionSummary);
			break;
		case ETerrainSteppedTransitionRootSupportStatus::MissingRootVerticalAccessCandidate:
			Summary.FailureReason = FString::Printf(
				TEXT("Root stepped-terrain solve cannot satisfy %s because its planned VerticalAccess cell is not backed by any frozen root vertical-access candidate."),
				*TransitionSummary);
			break;
		case ETerrainSteppedTransitionRootSupportStatus::SupportedByRootVerticalAccessCandidate:
			Summary.FailureReason = FString();
			break;
		default:
			Summary.FailureReason = FString::Printf(
				TEXT("Root stepped-terrain solve cannot satisfy %s because the compiled root support status is unknown."),
				*TransitionSummary);
			break;
		}

		return Summary;
	}

		TArray<FTerrainSteppedSupportPlan> BuildRootTerrainSteppedSupportPlans(
			const FCompiledStructuralInputs& StructuralInputs)
		{
			TArray<FTerrainSteppedSupportPlan> SupportPlans;
			for (const FTerrainSteppedTransitionRequirement& Requirement :
				StructuralInputs.RootTerrainSteppedTransitionRequirements)
			{
				if (!DoesTerrainSteppedTransitionHaveRootSupport(Requirement)
					|| Requirement.SupportingRootVerticalAccessCandidateCells.IsEmpty()
					|| Requirement.SupportingRootVerticalAccessBundleIds.IsEmpty())
				{
					continue;
				}

				FTerrainSteppedSupportPlan& SupportPlan =
					SupportPlans.AddDefaulted_GetRef();
				SupportPlan.TransitionRequirement = Requirement;
				SupportPlan.SelectedSupportingRootVerticalAccessCell =
					Requirement.SupportingRootVerticalAccessCandidateCells[0];
				SupportPlan.SelectedRootVerticalAccessBundleId =
					Requirement.SupportingRootVerticalAccessBundleIds[0];
			}

			SupportPlans.Sort([](
				const FTerrainSteppedSupportPlan& Left,
				const FTerrainSteppedSupportPlan& Right)
			{
				if (Left.TransitionRequirement.FromCell !=
					Right.TransitionRequirement.FromCell)
				{
					return IsLexicographicallyEarlierCell(
						Left.TransitionRequirement.FromCell,
						Right.TransitionRequirement.FromCell);
				}
				return IsLexicographicallyEarlierCell(
					Left.TransitionRequirement.ToCell,
					Right.TransitionRequirement.ToCell);
			});
			return SupportPlans;
		}

	/** Returns true when a frozen host group owns this stepped support cell. */
	bool IsSupportPlanBackedByFlexibleVerticalAccessHostGroup(
		const FCompiledStructuralInputs& StructuralInputs,
		const FTerrainSteppedSupportPlan& SupportPlan)
	{
		return StructuralInputs.RootVerticalAccessHostGroups.ContainsByPredicate(
			[&SupportPlan](const FLayoutVerticalAccessHostGroup& HostGroup)
			{
				return HostGroup.Options.ContainsByPredicate(
					[&SupportPlan](const FLayoutVerticalAccessHostOption& Option)
					{
						return Option.LowerCell == SupportPlan.SelectedSupportingRootVerticalAccessCell;
					});
			});
	}

	TArray<FTerrainSteppedInsertionPlan> BuildRootTerrainSteppedInsertionPlans(
		const FCompiledStructuralInputs& StructuralInputs)
	{
		TArray<FTerrainSteppedInsertionPlan> InsertionPlans;
		TArray<FTerrainSteppedSupportPlan> SortedSupportPlans = BuildRootTerrainSteppedSupportPlans(StructuralInputs);
		SortedSupportPlans.Sort([](const FTerrainSteppedSupportPlan& Left, const FTerrainSteppedSupportPlan& Right)
		{
			if (Left.TransitionRequirement.StepHeightBlocks != Right.TransitionRequirement.StepHeightBlocks)
			{
				return Left.TransitionRequirement.StepHeightBlocks > Right.TransitionRequirement.StepHeightBlocks;
			}
			if (Left.TransitionRequirement.FromCell != Right.TransitionRequirement.FromCell)
			{
				return IsLexicographicallyEarlierCell(
					Left.TransitionRequirement.FromCell,
					Right.TransitionRequirement.FromCell);
			}
			return IsLexicographicallyEarlierCell(
				Left.TransitionRequirement.ToCell,
				Right.TransitionRequirement.ToCell);
		});

		for (const FTerrainSteppedSupportPlan& SupportPlan : SortedSupportPlans)
		{
			// Flexible host planning owns the lower/upper cells. Do not emit a guessed
			// bundle insertion that would pin a different host before normal CSP runs.
			if (IsSupportPlanBackedByFlexibleVerticalAccessHostGroup(
					StructuralInputs,
					SupportPlan))
			{
				continue;
			}

			const FPlacementCapabilityBundle* SelectedBundle = StructuralInputs.RootPlacementBundles.FindByPredicate(
				[&SupportPlan](const FPlacementCapabilityBundle& CandidateBundle)
				{
					return CandidateBundle.BundleId == SupportPlan.SelectedRootVerticalAccessBundleId;
				});
			if (SelectedBundle == nullptr)
			{
				continue;
			}

			FTerrainSteppedInsertionPlan& InsertionPlan = InsertionPlans.AddDefaulted_GetRef();
			InsertionPlan.SupportPlan = SupportPlan;
			InsertionPlan.BundleAnchorCell = SupportPlan.SelectedSupportingRootVerticalAccessCell;
			InsertionPlan.SelectedRootPlacementBundle = *SelectedBundle;
		}

		InsertionPlans.Sort([](const FTerrainSteppedInsertionPlan& Left, const FTerrainSteppedInsertionPlan& Right)
		{
			if (Left.BundleAnchorCell != Right.BundleAnchorCell)
			{
				return IsLexicographicallyEarlierCell(Left.BundleAnchorCell, Right.BundleAnchorCell);
			}
			return Left.SelectedRootPlacementBundle.BundleId.LexicalLess(Right.SelectedRootPlacementBundle.BundleId);
		});
		return InsertionPlans;
	}

	TArray<FLayoutRouteConstraintRecord> BuildRootTerrainSteppedRouteConstraints(
		const FCompiledStructuralInputs& StructuralInputs)
	{
		return {};
	}

	FTerrainSteppedPreparedSolveContract BuildRootTerrainSteppedPreparedSolveContract(
		const FCompiledStructuralInputs& StructuralInputs)
	{
		FTerrainSteppedPreparedSolveContract PreparedContract;
		PreparedContract.bRootCanSatisfyTerrainSteppedTransitions =
			StructuralInputs.bRootCanSatisfyTerrainSteppedTransitions;
		PreparedContract.RejectionSummary = BuildRootTerrainSteppedRejectionSummary(StructuralInputs);
		PreparedContract.SupportPlans = BuildRootTerrainSteppedSupportPlans(StructuralInputs);
		PreparedContract.InsertionPlans = BuildRootTerrainSteppedInsertionPlans(StructuralInputs);
		PreparedContract.RouteConstraints = BuildRootTerrainSteppedRouteConstraints(StructuralInputs);
		return PreparedContract;
	}

	FLayoutChildCapabilityEnvelope BuildChildCapabilityEnvelopeFromRequest(
		const FLayoutRegionSolveRequest& Request)
	{
		return BuildChildCapabilityEnvelopeFromRequestInternal(Request);
	}

		FChildCapabilitySummary BuildChildCapabilitySummary(
			const FString& ChildRegionDebugPath,
			const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
			const TArray<FIntVector>& VerticalAccessCells,
			const bool bSupportsHostVerticalAccess,
			const bool bSupportsOptionalDrop,
			const TArray<FPlacementCapabilityBundle>& PlacementBundles,
			const TArray<FSharedParentChildFace>& SharedParentChildFaces)
	{
		FChildCapabilitySummary Summary;
		Summary.ChildRegionDebugPath = ChildRegionDebugPath;
		Summary.CapabilityEnvelope = CapabilityEnvelope;
		Summary.EndpointOffers = CapabilityEnvelope.EndpointCapabilities;
		Summary.SeamOffers = CapabilityEnvelope.SeamCapabilities;
		Summary.SpanOffers = CapabilityEnvelope.SpanCapabilities;
		Summary.EndpointOffersByLevel = BuildChildEndpointOffersByLevel(Summary.EndpointOffers);
		Summary.SeamOffersByLevel = BuildChildSeamOffersByLevel(Summary.SeamOffers);
		Summary.SpanOffersByLevel = BuildChildSpanOffersByLevel(Summary.SpanOffers);
		Summary.PlacementBundles = PlacementBundles;
		Summary.VerticalAccessSupportBandsByLevel =
			BuildChildVerticalAccessSupportBandsByLevel(Summary.PlacementBundles, VerticalAccessCells);
		Summary.TraversalSummariesByLevel =
			BuildChildTraversalSummariesByLevel(CapabilityEnvelope, Summary.PlacementBundles, VerticalAccessCells);
		// Pair lists can contain thousands of owned IDs repeated across levels.
		// Avoid quadratic string scans without changing first-seen identity or ordering.
		TSet<FLayoutId> SeenPairIds;
		for (const FChildLevelTraversalCapabilitySummary& TraversalSummary : Summary.TraversalSummariesByLevel)
		{
			for (const FLayoutId& AnchorPairId : TraversalSummary.ConnectableAnchorPairIds)
			{
				if (!AnchorPairId.IsNone())
				{
					bool bAlreadyPresent = false;
					SeenPairIds.Add(AnchorPairId, &bAlreadyPresent);
					if (!bAlreadyPresent) Summary.GenerallyConnectableAnchorPairIds.Add(AnchorPairId);
				}
			}
		}
		Summary.GenerallyConnectableAnchorPairIds.Sort([](const FLayoutId& Left, const FLayoutId& Right)
		{
			return Left.LexicalLess(Right);
		});
		Summary.VerticalAccessCells = VerticalAccessCells;
		Summary.ResponsibilityParticipation.bSupportsParentOwnedHostAccess =
			!Summary.EndpointOffers.IsEmpty();
		Summary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = bSupportsHostVerticalAccess;
		Summary.ResponsibilityParticipation.bSupportsComposedHostAccess = bSupportsHostVerticalAccess;
		Summary.ResponsibilityParticipation.bSupportsOptionalDrop = bSupportsOptionalDrop;
		Summary.ResponsibilityParticipation.bSupportsSeamOwnership =
			CapabilityEnvelope.SeamCapabilities.ContainsByPredicate([](const FLayoutChildCapabilitySeam& Seam)
			{
				return Seam.bCanOwnSeam;
			});
		Summary.SharedParentChildFaces = SharedParentChildFaces;
		return Summary;
	}

	bool ValidateHostContributingChildCapabilityContract(
		const FChildCapabilitySummary& ChildSummary,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();

		TArray<int32> EndpointLevels;
		EndpointLevels.Reserve(ChildSummary.EndpointOffersByLevel.Num());
		for (const FChildLevelEndpointCapabilitySummary& EndpointSummary : ChildSummary.EndpointOffersByLevel)
		{
			if (!EndpointSummary.EndpointOffers.IsEmpty())
			{
				EndpointLevels.Add(EndpointSummary.LevelIndex);
			}
		}
		EndpointLevels.Sort();
		for (int32 Index = EndpointLevels.Num() - 1; Index > 0; --Index)
		{
			if (EndpointLevels[Index] == EndpointLevels[Index - 1])
			{
				EndpointLevels.RemoveAt(Index);
			}
		}

		if (EndpointLevels.Num() < 2)
		{
			OutFailureReason = FString::Printf(
				TEXT("Child region '%s' is marked child_contributes_host_vertical_access, but its compiled capability summary exposes host-facing endpoint offers on only %d distinct level(s)."),
				*ChildSummary.ChildRegionDebugPath,
				EndpointLevels.Num());
			return false;
		}

		const int32 MinimumEndpointLevel = EndpointLevels[0];
		const int32 MaximumEndpointLevel = EndpointLevels.Last();
		for (int32 LevelIndex = MinimumEndpointLevel; LevelIndex <= MaximumEndpointLevel; ++LevelIndex)
		{
			const FChildLevelTraversalCapabilitySummary* TraversalSummary =
				ChildSummary.TraversalSummariesByLevel.FindByPredicate(
					[LevelIndex](const FChildLevelTraversalCapabilitySummary& Summary)
					{
						return Summary.LevelIndex == LevelIndex;
					});
			if (TraversalSummary == nullptr || !TraversalSummary->bCanCarryHostVerticalAccess)
			{
				OutFailureReason = FString::Printf(
					TEXT("Child region '%s' is marked child_contributes_host_vertical_access, but its compiled capability summary cannot carry host vertical access across level %d between the lowest and highest host-facing endpoint levels."),
					*ChildSummary.ChildRegionDebugPath,
					LevelIndex);
				return false;
			}
		}

		const FChildLevelTraversalCapabilitySummary* LowestLevelSummary =
			ChildSummary.TraversalSummariesByLevel.FindByPredicate(
				[MinimumEndpointLevel](const FChildLevelTraversalCapabilitySummary& Summary)
				{
					return Summary.LevelIndex == MinimumEndpointLevel;
				});
		const FChildLevelTraversalCapabilitySummary* HighestLevelSummary =
			ChildSummary.TraversalSummariesByLevel.FindByPredicate(
				[MaximumEndpointLevel](const FChildLevelTraversalCapabilitySummary& Summary)
				{
					return Summary.LevelIndex == MaximumEndpointLevel;
				});
		if (LowestLevelSummary == nullptr || HighestLevelSummary == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Child region '%s' is marked child_contributes_host_vertical_access, but its compiled traversal summaries do not preserve the lowest and highest host-facing endpoint levels."),
				*ChildSummary.ChildRegionDebugPath);
			return false;
		}

		const bool bHasGenerallyConnectableHostAnchorPair =
			LowestLevelSummary->ConnectableAnchorPairIds.ContainsByPredicate(
				[HighestLevelSummary](const FLayoutId PairId)
				{
					return PairId != NAME_None
						&& HighestLevelSummary->ConnectableAnchorPairIds.Contains(PairId);
				});
		if (!bHasGenerallyConnectableHostAnchorPair)
		{
			OutFailureReason = FString::Printf(
				TEXT("Child region '%s' is marked child_contributes_host_vertical_access, but its compiled capability summary exposes no generally connectable lower/upper host anchor pair across the lowest and highest host-facing endpoint levels."),
				*ChildSummary.ChildRegionDebugPath);
			return false;
		}

		return true;
	}

	void AppendHostContributingChildCapabilityAssertions(
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot,
		FLayoutRegionSolveRequest& InOutChildRequest,
		FChildCapabilitySummary& InOutChildSummary)
	{
		if (!EntrySnapshot.bChildContributesHostVerticalAccess)
		{
			return;
		}

		FString FailureReason;
		const bool bContractValid =
			ValidateHostContributingChildCapabilityContract(InOutChildSummary, FailureReason);
		const TArray<FLayoutId> RelatedIds = BuildChildRequestRelatedIds(EntrySnapshot, InOutChildRequest);

		InOutChildRequest.ValidationAssertions.Add(MakeCapabilityAssertionRecord(
			TEXT("RegionRequest.ChildHostVerticalAccessContract"),
			ELayoutValidationAssertionKind::ChildCapabilityContractValid,
			bContractValid,
			RelatedIds,
			bContractValid
				? FString()
				: FString::Printf(
					TEXT("%s%s%s%s%s\nProblem: Later host-ascent negotiation would have to rediscover that this child can never commit distinct lower and upper host-facing anchors through a usable internal vertical route.\nFix: Author host-facing endpoint offers on distinct covered levels with carried vertical access between them, or stop marking this child entry as host-contributing."),
					*FailureReason,
					*BuildChildRequestBreadcrumb(InOutChildRequest),
					*BuildChildProfileSnapshotBreadcrumb(InOutChildRequest.ProfileSnapshot),
					*BuildChildContentSetSnapshotBreadcrumb(InOutChildRequest.ContentSetSnapshot),
					*BuildChildModuleCatalogBreadcrumb(InOutChildRequest.ModuleCatalog))));
		InOutChildSummary.CapabilityEnvelope.ValidationAssertions.Add(MakeCapabilityAssertionRecord(
			TEXT("ChildCapability.HostVerticalAccessContract"),
			ELayoutValidationAssertionKind::ChildCapabilityContractValid,
			bContractValid,
			RelatedIds,
			bContractValid
				? FString()
				: FString::Printf(
					TEXT("%s%s%s%s%s\nProblem: The compiled child capability envelope cannot satisfy the host-contributing child contract.\nFix: Author host-facing endpoint offers on distinct covered levels with carried vertical access between them, or stop marking this child entry as host-contributing."),
					*FailureReason,
					*BuildChildCapabilityEnvelopeBreadcrumb(InOutChildSummary.CapabilityEnvelope),
					*BuildChildProfileSnapshotBreadcrumb(InOutChildRequest.ProfileSnapshot),
					*BuildChildContentSetSnapshotBreadcrumb(InOutChildRequest.ContentSetSnapshot),
					*BuildChildModuleCatalogBreadcrumb(InOutChildRequest.ModuleCatalog))));

		if (!bContractValid)
		{
			InOutChildSummary.ResponsibilityParticipation.bSupportsChildOwnedHostAccess = false;
			InOutChildSummary.ResponsibilityParticipation.bSupportsComposedHostAccess = false;
		}
	}

	FNegotiationDemandPlan BuildNegotiationDemandPlan(
		const FString& ChildRegionDebugPath,
		const FLayoutRegionSolveRequest& ChildRequest,
		const FChildCapabilitySummary& ChildSummary,
		const TArray<FIntVector>& ReservedParentCells,
		const ELayoutPlacementZone ParentPlacementZone,
		const ELayoutLevelPlacementPolicy ParentLevelPlacementPolicy,
		const int32 ParentSpecificLevel,
		const ETerrainStageEligibility TerrainStageEligibility,
		const bool bRequiresDeferredActivation,
		const bool bTerrainStageCanActivateEarlyIfUnlocksAscent,
		const bool bTerrainStageActivatesOnCurrentFrontier,
		const TOptional<int32> UnlockingAscentFrontierId,
		const int32 TerrainStageMinimumReachableLevel,
		const int32 TerrainStageMinimumDeferredLevel)
	{
		FNegotiationDemandPlan DemandPlan;
		DemandPlan.ChildRegionDebugPath = ChildRegionDebugPath;
		DemandPlan.ChildRequest = ChildRequest;
		DemandPlan.ChildSummary = ChildSummary;
		DemandPlan.ReservedParentCells = ReservedParentCells;
		DemandPlan.FeatureFlags = BuildNegotiationFeatureFlags(ChildRequest, ChildSummary);
		DemandPlan.FeatureFlags.bOptionalDemand = ChildRequest.bSourceContentEntryOptional;
		DemandPlan.ParentPlacementZone = ParentPlacementZone;
		DemandPlan.ParentLevelPlacementPolicy = ParentLevelPlacementPolicy;
		DemandPlan.ParentSpecificLevel = ParentSpecificLevel;
		DemandPlan.TerrainStageEligibility = TerrainStageEligibility;
		DemandPlan.bTerrainStageDeferred = TerrainStageEligibility == ETerrainStageEligibility::Deferred;
		DemandPlan.bRequiresDeferredActivation = bRequiresDeferredActivation;
		DemandPlan.bTerrainStageCanActivateEarlyIfUnlocksAscent = bTerrainStageCanActivateEarlyIfUnlocksAscent;
		DemandPlan.bTerrainStageActivatesOnCurrentFrontier = bTerrainStageActivatesOnCurrentFrontier;
		DemandPlan.UnlockingAscentFrontierId = UnlockingAscentFrontierId;
		DemandPlan.TerrainStageMinimumReachableLevel = TerrainStageMinimumReachableLevel;
		DemandPlan.TerrainStageMinimumDeferredLevel = TerrainStageMinimumDeferredLevel;
		return DemandPlan;
	}

	TArray<FNegotiationDemandPlan> BuildNegotiationDemandPlans(
		const FRecursiveScheduleSolveContext& SolveContext)
	{
		TArray<FNegotiationDemandPlan> DemandPlans;
		const TMap<FLayoutId, int32> RequiredInstanceCounts =
			BuildRequiredChildPlacementInstanceCounts(SolveContext.RootRequest);
		TMap<FLayoutId, FLayoutId> CountedPathIdByEntryId;
		TSet<FLayoutId> HardGovernedEntryIds;
		TArray<LayoutZoneFeatureDemand::FHardDemand> HardDemands;
		LayoutZoneFeatureDemand::CompileHardDemands(
			SolveContext.RootRequest.ProfileSnapshot.ZoneFeatureRequirements,
			HardDemands);
		TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary> ProviderChoices;
		LayoutZoneFeatureDemand::CompileProviderChoiceSummaries(
			SolveContext.RootRequest.EffectiveSnapshotId,
			HardDemands,
			SolveContext.RootRequest.ContentSetSnapshot.Entries,
			ProviderChoices);
		for (const LayoutZoneFeatureDemand::FHardDemand& Demand : HardDemands)
		{
			TArray<FLayoutId> MatchingChildIds;
			for (const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice : ProviderChoices)
			{
				if (Choice.RequirementId == Demand.RequirementId
					&& Choice.ContentKind == ELayoutRegionContentKind::ChildRegion)
				{
					MatchingChildIds.AddUnique(Choice.SourceContentEntryId);
					HardGovernedEntryIds.Add(Choice.SourceContentEntryId);
				}
			}
			MatchingChildIds.Sort([](const FLayoutId Left, const FLayoutId Right)
			{
				return Left.LexicalLess(Right);
			});
			if (!MatchingChildIds.IsEmpty())
			{
				CountedPathIdByEntryId.Add(
					MatchingChildIds[0],
					MatchingChildIds.Num() > 1
						? Demand.RequirementId
						: MatchingChildIds[0]);
			}
		}

		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : SolveContext.RootRequest.ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.ContentKind != ELayoutRegionContentKind::ChildRegion)
			{
				continue;
			}

			const int32 RequiredInstanceCount =
				RequiredInstanceCounts.FindRef(EntrySnapshot.EntryId);
			if (RequiredInstanceCount <= 0)
			{
				continue;
			}
			for (int32 InstanceOrdinal = 0; InstanceOrdinal < RequiredInstanceCount; ++InstanceOrdinal)
			{
				const FLayoutId ChildPathId = CountedPathIdByEntryId.FindRef(
					EntrySnapshot.EntryId).IsNone()
					? EntrySnapshot.EntryId
					: CountedPathIdByEntryId.FindRef(EntrySnapshot.EntryId);
				const FString ChildRegionDebugPath = RequiredInstanceCount > 1
					? FString::Printf(
						TEXT("%s/%s_%d"),
						*SolveContext.RootRequest.RegionDebugPath,
						*ChildPathId.ToString(),
						InstanceOrdinal + 1)
					: FString::Printf(
						TEXT("%s/%s"),
						*SolveContext.RootRequest.RegionDebugPath,
						*ChildPathId.ToString());
				const int32 ChildSeed = static_cast<int32>(HashCombineFast(
					static_cast<uint32>(SolveContext.RootRequest.Seed),
					HashCombineFast(
						GetTypeHash(EntrySnapshot.EntryId),
						static_cast<uint32>(InstanceOrdinal + 1))));
				const bool bCountedProviderChoiceSlot =
					HardGovernedEntryIds.Contains(EntrySnapshot.EntryId);
				const bool bChildDemandIsOptional =
					!bCountedProviderChoiceSlot
					&& EntrySnapshot.bChildOptional
					&& RequiredInstanceCount <= 1;

				FLayoutRegionSolveRequest ChildRequest;
				FLayoutChildCapabilityEnvelope CapabilityEnvelope;
				TArray<FPlacementCapabilityBundle> PlacementBundles;
				TArray<FIntVector> VerticalAccessCells;
				bool bSupportsHostVerticalAccess = false;

				if (EntrySnapshot.CompiledChildRequestTemplate.IsValid())
				{
					ChildRequest = BuildChildRequestFromTemplate(
						*EntrySnapshot.CompiledChildRequestTemplate,
						ChildSeed,
						ChildRegionDebugPath,
						EntrySnapshot.EntryId,
						SolveContext.RootRequest.RegionDebugPath,
						bChildDemandIsOptional,
						SolveContext.RootRequest.ExecutionSettings);
					PlacementBundles = BuildModulePlacementBundles(ChildRequest);
					VerticalAccessCells = BuildVerticalAccessCellsFromRequest(ChildRequest);
					bSupportsHostVerticalAccess =
						EntrySnapshot.bChildContributesHostVerticalAccess && !VerticalAccessCells.IsEmpty();
					CapabilityEnvelope = BuildChildCapabilityEnvelopeFromRequestInternal(ChildRequest);
				}
				else
				{
					ChildRequest.RegionDebugPath = ChildRegionDebugPath;
					ChildRequest.EffectiveSnapshotId =
						FLayoutId(*FString::Printf(TEXT("%s.MissingChildTemplate"), *ChildRegionDebugPath));
					ChildRequest.Seed = ChildSeed;
					ChildRequest.SourceContentEntryId = EntrySnapshot.EntryId;
					ChildRequest.SourceParentRegionDebugPath = SolveContext.RootRequest.RegionDebugPath;
					ChildRequest.bSourceContentEntryOptional = bChildDemandIsOptional;
					ChildRequest.ExecutionSettings = SolveContext.RootRequest.ExecutionSettings;
					ChildRequest.ProfileSnapshot.SnapshotId = EntrySnapshot.ChildProfileSnapshotId;
					ChildRequest.ContentSetSnapshot.SnapshotId = EntrySnapshot.ChildContentSetSnapshotId;
					ChildRequest.ValidationAssertions.Add(MakeCapabilityAssertionRecord(
						TEXT("RegionRequest.ChildTemplatePresent"),
						ELayoutValidationAssertionKind::SnapshotContractInitialized,
						false,
						{EntrySnapshot.EntryId, ChildRequest.EffectiveSnapshotId, EntrySnapshot.ChildProfileSnapshotId, EntrySnapshot.ChildContentSetSnapshotId},
						FString::Printf(
							TEXT("Child content entry '%s' is missing its compiled snapshot-only child request template during demand compilation.%s%s%s"),
							*EntrySnapshot.EntryId.ToString(),
							*BuildChildRequestBreadcrumb(ChildRequest),
							*BuildFrozenChildSnapshotIdBreadcrumb(TEXT("profile"), EntrySnapshot.ChildProfileSnapshotId),
							*BuildFrozenChildSnapshotIdBreadcrumb(TEXT("content-set"), EntrySnapshot.ChildContentSetSnapshotId))));
					CapabilityEnvelope.RegionDebugPath = ChildRegionDebugPath;
					CapabilityEnvelope.SnapshotId = ChildRequest.EffectiveSnapshotId;
					CapabilityEnvelope.ValidationAssertions.Add(MakeCapabilityAssertionRecord(
						TEXT("ChildCapability.TemplatePresent"),
						ELayoutValidationAssertionKind::ChildCapabilityContractValid,
						false,
						{EntrySnapshot.EntryId, CapabilityEnvelope.SnapshotId, EntrySnapshot.ChildProfileSnapshotId, EntrySnapshot.ChildContentSetSnapshotId},
						FString::Printf(
							TEXT("Child content entry '%s' cannot compile a child capability envelope because its snapshot-only child request template is missing.%s%s%s"),
							*EntrySnapshot.EntryId.ToString(),
							*BuildChildCapabilityEnvelopeBreadcrumb(CapabilityEnvelope),
							*BuildFrozenChildSnapshotIdBreadcrumb(TEXT("profile"), EntrySnapshot.ChildProfileSnapshotId),
							*BuildFrozenChildSnapshotIdBreadcrumb(TEXT("content-set"), EntrySnapshot.ChildContentSetSnapshotId))));
				}

				FChildCapabilitySummary ChildSummary = BuildChildCapabilitySummary(
					ChildRegionDebugPath,
					CapabilityEnvelope,
					VerticalAccessCells,
					bSupportsHostVerticalAccess,
					bChildDemandIsOptional,
					PlacementBundles);
				AppendHostContributingChildCapabilityAssertions(
					EntrySnapshot,
					ChildRequest,
					ChildSummary);
				ETerrainStageEligibility TerrainStageEligibility = ETerrainStageEligibility::Active;
				bool bRequiresDeferredActivation = false;
				bool bTerrainStageCanActivateEarlyIfUnlocksAscent = false;
				bool bTerrainStageActivatesOnCurrentFrontier = false;
				TOptional<int32> UnlockingAscentFrontierId;
				int32 TerrainStageMinimumReachableLevel = INDEX_NONE;
				int32 TerrainStageMinimumDeferredLevel = INDEX_NONE;
				ResolveDemandTerrainStageEligibility(
					SolveContext,
					EntrySnapshot,
					ChildSummary,
					TerrainStageEligibility,
					bRequiresDeferredActivation,
					bTerrainStageCanActivateEarlyIfUnlocksAscent,
					bTerrainStageActivatesOnCurrentFrontier,
					UnlockingAscentFrontierId,
					TerrainStageMinimumReachableLevel,
					TerrainStageMinimumDeferredLevel);
				FNegotiationDemandPlan& DemandPlan = DemandPlans.Add_GetRef(
					BuildNegotiationDemandPlan(
						ChildRegionDebugPath,
						ChildRequest,
						ChildSummary,
						{},
						EntrySnapshot.ChildPlacementZone,
						EntrySnapshot.ChildLevelPlacementPolicy,
						EntrySnapshot.ChildSpecificLevel,
						TerrainStageEligibility,
						bRequiresDeferredActivation,
						bTerrainStageCanActivateEarlyIfUnlocksAscent,
						bTerrainStageActivatesOnCurrentFrontier,
						UnlockingAscentFrontierId,
						TerrainStageMinimumReachableLevel,
						TerrainStageMinimumDeferredLevel));
				DemandPlan.FeatureFlags.bCountedProviderChoiceSlot =
					bCountedProviderChoiceSlot;
			}
		}

		return DemandPlans;
	}

	TArray<FPlacementCapabilityBundle> BuildModulePlacementBundles(
		const FLayoutRegionSolveRequest& Request)
	{
		TArray<FPlacementCapabilityBundle> PlacementBundles;
		PlacementBundles.Reserve(Request.ModuleCatalog.Modules.Num());

		for (int32 ModuleIndex = 0; ModuleIndex < Request.ModuleCatalog.Modules.Num(); ++ModuleIndex)
		{
			const FLayoutModuleSolveSnapshot& ModuleSnapshot = Request.ModuleCatalog.Modules[ModuleIndex];
			if (ModuleSnapshot.OccupiedLocalCells.IsEmpty())
			{
				continue;
			}

			PlacementBundles.Add(
				BuildPlacementBundleFromModuleSnapshot(Request, ModuleSnapshot, ModuleIndex));
		}

		return PlacementBundles;
	}
}
