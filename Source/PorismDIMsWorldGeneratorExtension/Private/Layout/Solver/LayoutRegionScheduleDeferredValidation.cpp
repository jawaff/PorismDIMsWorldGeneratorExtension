// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "LayoutRegionScheduleDeferredValidation.h"
#include "LayoutSolveExecutionBudget.h"
#include "LayoutProfileSolverInternal.h"
#include "LayoutRegionScheduleSearchRuntime.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/ScopeExit.h"

namespace LayoutRegionScheduleSolverPrivate
{
	bool RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString* OutFailureReason);

	void ApplyCommittedVerticalAccessOwnershipToNegotiatedContract(
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FString& ChildRegionDebugPath,
		FLayoutNegotiatedChildResponsibilityContract& InOutContract);

	bool ApplyOwnedSeamUsageRestrictions(
		const FString& ParentRegionDebugPath,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		FLayoutRegionSolveRequest& InOutParentRequest,
		TArray<FAutomaticChildPlacement>& InOutPlacements,
		FString& OutFailureReason);

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

	void BuildChildRegionMemoIdentityMap(
		const TArray<FAutomaticChildPlacement>& Placements,
		TMap<FString, FString>& OutIdentityByRegionPath);

	void BuildChildRegionSemanticMemoIdentityMap(
		const TArray<FAutomaticChildPlacement>& Placements,
		TMap<FString, FString>& OutIdentityByRegionPath);

	FString CanonicalizeRegionPathForMemo(
		const FString& RegionDebugPath,
		const TMap<FString, FString>& MemoIdentityByRegionPath);

	FString BuildCommittedVerticalAccessOwnershipMemoKey(
		const FCommittedVerticalAccessOwnership& Ownership,
		const TMap<FString, FString>& MemoIdentityByRegionPath);

	FString TagsToStableKey(const FGameplayTagContainer& Tags);

	bool AreConnectionTagsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		const FLayoutFaceRule& TargetFaceRule);

	bool AreSolverYawRotationsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		int32 SourceYawRotationSteps,
		const FLayoutFaceRule& TargetFaceRule,
		int32 TargetYawRotationSteps);

	bool HasSharedConnectedWalkableArea(
		const FLayoutFaceRule& LeftFaceRule,
		const FLayoutFaceRule& RightFaceRule);

	bool AreCommittedConnectionTagsCompatible(
		FGameplayTag CapabilityConnectionTag,
		const FGameplayTagContainer& CapabilityAllowedConnectionTags,
		FGameplayTag CommitmentConnectionTag,
		const FGameplayTagContainer& CommitmentAllowedConnectionTags);

	bool IsCommittedVerticalAccessRequirementSatisfied(
		const FCommittedVerticalAccessOwnership& Ownership);

	FString BuildCommittedVerticalAccessFailureReason(
		const FCommittedVerticalAccessOwnership& Ownership);

	void BuildIndependentChildProofValidationRequest(
		const FAutomaticChildPlacement& Placement,
		FLayoutRegionSolveRequest& OutChildRequest);

	// Freezes every child contact-face boundary point from the parent solve result into a
	// pointer-free handoff array consumed by the child's independent SolveRegion.
	TArray<FLayoutSolveBoundaryPoint> FreezeChildIncomingBoundaryPointsFromParentResult(
		const FLayoutRegionSolveResult& ParentProofResult,
		const FAutomaticChildPlacement& Placement,
		const TArray<FLayoutRegionSolveResult>* SiblingResults = nullptr);

	bool BuildIndependentDeferredProofScheduleResult(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutRegionSolveRequest>& ChildRequests,
		const FCommittedRecursiveScheduleState& CandidateSchedule,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FLayoutRegionSolveResult&& ParentResult,
		TArray<FLayoutRegionSolveResult>&& ChildResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason,
		bool bApplyPostStructuralHandoffs);

	bool BuildIndependentChildProofResult(
		const FLayoutRegionSolveRequest& ChildProofRequest,
		FLayoutRegionSolveResult& OutChildProofResult,
		FString& OutFailureReason);

	bool ValidateCommittedEndpointAnchorCoverageOnPlacements(
		const FString& RegionDebugPath,
		const FIntVector& RegionOffset,
		const TArray<FLayoutPlacedModule>& Placements,
		const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
		FString& OutFailureReason);

	bool ValidateMergedScheduleTraversalConnectivity(
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		const FString& RootRegionDebugPath,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FCommittedRecursiveScheduleState* CandidateSchedule,
		FString& OutFailureReason);

	namespace
	{
		struct FMergedTraversalAuditNode
		{
			FIntVector Cell = FIntVector::ZeroValue;
			FGameplayTag TraversalChannel;

			bool operator==(const FMergedTraversalAuditNode& Other) const
			{
				return Cell == Other.Cell && TraversalChannel == Other.TraversalChannel;
			}
		};

		uint32 GetTypeHash(const FMergedTraversalAuditNode& Node)
		{
			return HashCombine(GetTypeHash(Node.Cell), GetTypeHash(Node.TraversalChannel));
		}

		bool IsFaceCompatibleWithFilledNeighborForMergedTraversalAudit(
			const FLayoutFaceRule& FaceRule)
		{
			switch (FaceRule.OccupancyPolicy)
			{
			case ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor:
			case ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor:
			case ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor:
			case ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor:
			case ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor:
				return true;
			case ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor:
			default:
				return false;
			}
		}

		FIntVector RotateLocalCellForMergedTraversalAudit(
			const FIntVector& LocalCell,
			const FIntVector& BoundsCells,
			const int32 YawRotationSteps)
		{
			const int32 NormalizedYawSteps = ((YawRotationSteps % 4) + 4) % 4;
			switch (NormalizedYawSteps)
			{
			case 1:
				return FIntVector(BoundsCells.Y - 1 - LocalCell.Y, LocalCell.X, LocalCell.Z);
			case 2:
				return FIntVector(BoundsCells.X - 1 - LocalCell.X, BoundsCells.Y - 1 - LocalCell.Y, LocalCell.Z);
			case 3:
				return FIntVector(LocalCell.Y, BoundsCells.X - 1 - LocalCell.X, LocalCell.Z);
			case 0:
			default:
				return LocalCell;
			}
		}

		bool TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			FIntVector& OutAuthoredLocalCell);

		void AddMergedTraversalAuditEdge(
			TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>& Adjacency,
			const FMergedTraversalAuditNode& Left,
			const FMergedTraversalAuditNode& Right)
		{
			Adjacency.FindOrAdd(Left).Add(Right);
			Adjacency.FindOrAdd(Right).Add(Left);
		}

		bool TryGetWorldFaceRuleForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const ELayoutFaceDirection WorldDirection,
			FLayoutFaceRule& OutFaceRule)
		{
			if (Placement.Module == nullptr)
			{
				return false;
			}

			const FLayoutModuleFaceRules EffectiveFaceRules =
				Placement.Module->GetEffectiveFaceRules();
			const ELayoutFaceDirection AuthoredDirection =
				FLayoutDirectionUtils::RotateYaw(WorldDirection, -Placement.YawRotationSteps);
			const FLayoutFaceRule* FaceRule =
				EffectiveFaceRules.FindRule(AuthoredDirection);
			if (FaceRule == nullptr)
			{
				return false;
			}

			OutFaceRule = *FaceRule;
			OutFaceRule.Direction = WorldDirection;
			return true;
		}

		FIntVector GetPlacementBoundsCellsForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement)
		{
			if (!Placement.OccupiedLocalCells.IsEmpty()
				|| !Placement.LocalCellFaceRules.IsEmpty())
			{
				return Placement.BundleBoundsCells;
			}

			if (Placement.Module != nullptr)
			{
				return Placement.Module->GetOccupiedBoundsCells();
			}

			return FIntVector(1, 1, 1);
		}

		void AppendPlacementOccupiedLocalCellsForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			TArray<FIntVector>& OutOccupiedLocalCells)
		{
			OutOccupiedLocalCells = Placement.OccupiedLocalCells;
			if (!OutOccupiedLocalCells.IsEmpty())
			{
				return;
			}

			if (Placement.Module != nullptr)
			{
				OutOccupiedLocalCells = Placement.Module->GetOccupiedLocalCells();
			}

			if (OutOccupiedLocalCells.IsEmpty())
			{
				OutOccupiedLocalCells.Add(FIntVector::ZeroValue);
			}
		}

		bool TryResolvePlacementWorldCellForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& LocalCell,
			FIntVector& OutWorldCell)
		{
			const FIntVector BoundsCells =
				GetPlacementBoundsCellsForMergedTraversalAudit(Placement);
			OutWorldCell =
				Placement.Cell + RotateLocalCellForMergedTraversalAudit(
					LocalCell,
					BoundsCells,
					Placement.YawRotationSteps);
			return true;
		}

		bool TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			const ELayoutFaceDirection WorldDirection,
			FLayoutFaceRule& OutFaceRule)
		{
			FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
			if (!TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
					Placement,
					WorldCell,
					AuthoredLocalCell))
			{
				return false;
			}

			if (!Placement.LocalCellFaceRules.IsEmpty())
			{
				const FLayoutPlacedLocalCellFaceRuleSnapshot* CellSnapshot =
					Placement.LocalCellFaceRules.FindByPredicate(
						[&AuthoredLocalCell](
							const FLayoutPlacedLocalCellFaceRuleSnapshot& CandidateCellSnapshot)
						{
							return CandidateCellSnapshot.LocalCell == AuthoredLocalCell;
						});
				if (CellSnapshot == nullptr)
				{
					return false;
				}

				if (const FLayoutFaceRule* Rule =
						CellSnapshot->ExposedFaceRules.FindByPredicate(
							[&Placement, WorldDirection](const FLayoutFaceRule& CandidateRule)
							{
								return FLayoutDirectionUtils::RotateYaw(
										   CandidateRule.Direction,
										   Placement.YawRotationSteps)
									== WorldDirection;
							}))
				{
					OutFaceRule = *Rule;
					OutFaceRule.Direction = WorldDirection;
					return true;
				}

				return false;
			}

			return TryGetWorldFaceRuleForMergedTraversalAudit(
				Placement,
				WorldDirection,
				OutFaceRule);
		}

		TSet<FGameplayTag> CollectPlacementTraversalChannelsAtWorldCellForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell)
		{
			TSet<FGameplayTag> TraversalChannels;
			FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
			if (!TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
					Placement,
					WorldCell,
					AuthoredLocalCell))
			{
				return TraversalChannels;
			}

			if (!Placement.LocalCellFaceRules.IsEmpty())
			{
				if (const FLayoutPlacedLocalCellFaceRuleSnapshot* CellSnapshot =
						Placement.LocalCellFaceRules.FindByPredicate(
							[&AuthoredLocalCell](
								const FLayoutPlacedLocalCellFaceRuleSnapshot& CandidateCellSnapshot)
							{
								return CandidateCellSnapshot.LocalCell == AuthoredLocalCell;
							}))
				{
					for (const FLayoutFaceRule& FaceRule :
						 CellSnapshot->ExposedFaceRules)
					{
						for (const FGameplayTag& TraversalChannel :
							 FaceRule.ConnectedTraversalChannels)
						{
							if (TraversalChannel.IsValid())
							{
								TraversalChannels.Add(TraversalChannel);
							}
						}
					}
				}
			}
			else
			{
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					FLayoutFaceRule FaceRule;
					if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
							Placement,
							WorldCell,
							static_cast<ELayoutFaceDirection>(DirectionIndex),
							FaceRule))
					{
						continue;
					}

					for (const FGameplayTag& TraversalChannel :
						 FaceRule.ConnectedTraversalChannels)
					{
						if (TraversalChannel.IsValid())
						{
							TraversalChannels.Add(TraversalChannel);
						}
					}
				}
			}

			for (const FLayoutPlacedDerivedInternalTraversalLink& DerivedLink :
				 Placement.DerivedInternalTraversalLinks)
			{
				if (DerivedLink.FromLocalCell == AuthoredLocalCell
					&& DerivedLink.FromTraversalChannel.IsValid())
				{
					TraversalChannels.Add(DerivedLink.FromTraversalChannel);
				}
				if (DerivedLink.ToLocalCell == AuthoredLocalCell
					&& DerivedLink.ToTraversalChannel.IsValid())
				{
					TraversalChannels.Add(DerivedLink.ToTraversalChannel);
				}
			}

			if (Placement.DerivedInternalTraversalLinks.IsEmpty()
				&& Placement.Module != nullptr)
			{
				for (const FLayoutInternalAccessLink& InternalLink :
					 Placement.Module->GetEffectiveInternalAccessLinks())
				{
					if (InternalLink.FromTraversalChannel.IsValid())
					{
						TraversalChannels.Add(InternalLink.FromTraversalChannel);
					}
					if (InternalLink.ToTraversalChannel.IsValid())
					{
						TraversalChannels.Add(InternalLink.ToTraversalChannel);
					}
				}
			}

			return TraversalChannels;
		}

		TSet<FGameplayTag> CollectPlacementTraversalChannelsForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement)
		{
			TSet<FGameplayTag> TraversalChannels;
			TArray<FIntVector> OccupiedLocalCells;
			AppendPlacementOccupiedLocalCellsForMergedTraversalAudit(
				Placement,
				OccupiedLocalCells);
			for (const FIntVector& LocalCell : OccupiedLocalCells)
			{
				FIntVector WorldCell = FIntVector::ZeroValue;
				TryResolvePlacementWorldCellForMergedTraversalAudit(
					Placement,
					LocalCell,
					WorldCell);
				TraversalChannels.Append(
					CollectPlacementTraversalChannelsAtWorldCellForMergedTraversalAudit(
						Placement,
						WorldCell));
			}

			return TraversalChannels;
		}

		void AddPlacementInternalTraversalEdgesForMergedTraversalAudit(
			TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>& Adjacency,
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			const TSet<FGameplayTag>& LocalTraversalChannels)
		{
			FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
			if (!TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
					Placement,
					WorldCell,
					AuthoredLocalCell))
			{
				return;
			}

			for (const FLayoutPlacedDerivedInternalTraversalLink& DerivedLink :
				 Placement.DerivedInternalTraversalLinks)
			{
				if (DerivedLink.FromLocalCell != AuthoredLocalCell
					&& DerivedLink.ToLocalCell != AuthoredLocalCell)
				{
					continue;
				}

				if (!DerivedLink.FromTraversalChannel.IsValid()
					|| !DerivedLink.ToTraversalChannel.IsValid())
				{
					continue;
				}

				FIntVector FromWorldCell = FIntVector::ZeroValue;
				FIntVector ToWorldCell = FIntVector::ZeroValue;
				TryResolvePlacementWorldCellForMergedTraversalAudit(
					Placement,
					DerivedLink.FromLocalCell,
					FromWorldCell);
				TryResolvePlacementWorldCellForMergedTraversalAudit(
					Placement,
					DerivedLink.ToLocalCell,
					ToWorldCell);
				AddMergedTraversalAuditEdge(
					Adjacency,
					FMergedTraversalAuditNode{
						FromWorldCell, DerivedLink.FromTraversalChannel},
					FMergedTraversalAuditNode{
						ToWorldCell, DerivedLink.ToTraversalChannel});
				if (DerivedLink.bBidirectional)
				{
					AddMergedTraversalAuditEdge(
						Adjacency,
						FMergedTraversalAuditNode{
							ToWorldCell, DerivedLink.ToTraversalChannel},
						FMergedTraversalAuditNode{
							FromWorldCell, DerivedLink.FromTraversalChannel});
				}
			}

			if (!Placement.DerivedInternalTraversalLinks.IsEmpty())
			{
				return;
			}

			if (Placement.Module == nullptr)
			{
				return;
			}

			for (const FLayoutInternalAccessLink& InternalLink :
				 Placement.Module->GetEffectiveInternalAccessLinks())
			{
				if (InternalLink.FromTraversalChannel.IsValid())
				{
					Adjacency.FindOrAdd(FMergedTraversalAuditNode{
						WorldCell, InternalLink.FromTraversalChannel});
				}
				if (InternalLink.ToTraversalChannel.IsValid())
				{
					Adjacency.FindOrAdd(FMergedTraversalAuditNode{
						WorldCell, InternalLink.ToTraversalChannel});
				}
				if (!InternalLink.FromTraversalChannel.IsValid()
					|| !InternalLink.ToTraversalChannel.IsValid()
					|| !LocalTraversalChannels.Contains(
						InternalLink.FromTraversalChannel)
					|| !LocalTraversalChannels.Contains(
						InternalLink.ToTraversalChannel))
				{
					continue;
				}

				AddMergedTraversalAuditEdge(
					Adjacency,
					FMergedTraversalAuditNode{
						WorldCell, InternalLink.FromTraversalChannel},
					FMergedTraversalAuditNode{
						WorldCell, InternalLink.ToTraversalChannel});
				if (InternalLink.bBidirectional)
				{
					AddMergedTraversalAuditEdge(
						Adjacency,
						FMergedTraversalAuditNode{
							WorldCell, InternalLink.ToTraversalChannel},
						FMergedTraversalAuditNode{
							WorldCell, InternalLink.FromTraversalChannel});
				}
			}
		}

		TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>
		BuildMergedTraversalAuditGraph(
			const FLayoutSolveResult& SolveResult,
			const TArray<FLayoutPartitionSeamRecord>& PartitionSeams)
		{
			TMap<FIntVector, TArray<const FLayoutPlacedModule*>> PlacementsByCell;
			TMap<FIntVector, TSet<FGameplayTag>> TraversalChannelsByCell;
			TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>
				Adjacency;

			for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
			{
				TArray<FIntVector> OccupiedLocalCells;
				AppendPlacementOccupiedLocalCellsForMergedTraversalAudit(
					Placement,
					OccupiedLocalCells);
				for (const FIntVector& LocalCell : OccupiedLocalCells)
				{
					FIntVector WorldCell = FIntVector::ZeroValue;
					TryResolvePlacementWorldCellForMergedTraversalAudit(
						Placement,
						LocalCell,
						WorldCell);
					TArray<const FLayoutPlacedModule*>& PlacementsAtCell =
						PlacementsByCell.FindOrAdd(WorldCell);
					PlacementsAtCell.Add(&Placement);

					TSet<FGameplayTag> TraversalChannels =
						CollectPlacementTraversalChannelsAtWorldCellForMergedTraversalAudit(
							Placement,
							WorldCell);
					TSet<FGameplayTag>& CellTraversalChannels =
						TraversalChannelsByCell.FindOrAdd(WorldCell);
					for (const FGameplayTag& TraversalChannel :
						 TraversalChannels)
					{
						if (TraversalChannel.IsValid())
						{
							CellTraversalChannels.Add(TraversalChannel);
							Adjacency.FindOrAdd(
								FMergedTraversalAuditNode{WorldCell, TraversalChannel});
						}
					}
				}
			}

			for (const TPair<FIntVector, TArray<const FLayoutPlacedModule*>>& Pair :
				 PlacementsByCell)
			{
				const FIntVector& Cell = Pair.Key;
				const TArray<const FLayoutPlacedModule*>& LocalPlacements =
					Pair.Value;
				const TSet<FGameplayTag>* LocalTraversalChannels =
					TraversalChannelsByCell.Find(Cell);
				if (LocalTraversalChannels == nullptr)
				{
					continue;
				}

				for (const FLayoutPlacedModule* PlacementPtr : LocalPlacements)
				{
					if (PlacementPtr == nullptr)
					{
						continue;
					}

					AddPlacementInternalTraversalEdgesForMergedTraversalAudit(
						Adjacency,
						*PlacementPtr,
						Cell,
						*LocalTraversalChannels);
				}

				static const ELayoutFaceDirection PositiveDirections[] =
				{
					ELayoutFaceDirection::PosX,
					ELayoutFaceDirection::PosY,
					ELayoutFaceDirection::PosZ
				};

				for (const ELayoutFaceDirection Direction : PositiveDirections)
				{
					const FIntVector NeighborCell =
						Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					const TArray<const FLayoutPlacedModule*>* NeighborPlacementsPtr =
						PlacementsByCell.Find(NeighborCell);
					if (NeighborPlacementsPtr == nullptr
						|| NeighborPlacementsPtr->IsEmpty())
					{
						continue;
					}

					for (const FLayoutPlacedModule* PlacementPtr : LocalPlacements)
					{
						if (PlacementPtr == nullptr)
						{
							continue;
						}

						const FLayoutPlacedModule& Placement = *PlacementPtr;
						FLayoutFaceRule PlacementFaceRule;
						if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
								Placement,
								Cell,
								Direction,
								PlacementFaceRule))
						{
							continue;
						}

						for (const FLayoutPlacedModule* NeighborPlacementPtr :
							 *NeighborPlacementsPtr)
						{
							if (NeighborPlacementPtr == nullptr)
							{
								continue;
							}

							const FLayoutPlacedModule& NeighborPlacement =
								*NeighborPlacementPtr;
							FLayoutFaceRule NeighborFaceRule;
							if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
									NeighborPlacement,
									NeighborCell,
									FLayoutDirectionUtils::GetOpposite(Direction),
									NeighborFaceRule))
							{
								continue;
							}

							if (!IsFaceCompatibleWithFilledNeighborForMergedTraversalAudit(
										PlacementFaceRule)
								|| !IsFaceCompatibleWithFilledNeighborForMergedTraversalAudit(
									NeighborFaceRule)
								|| !AreConnectionTagsCompatible(
									PlacementFaceRule,
									NeighborFaceRule)
								|| !AreConnectionTagsCompatible(
									NeighborFaceRule,
									PlacementFaceRule)
								|| !HasSharedConnectedWalkableArea(
									PlacementFaceRule,
									NeighborFaceRule)
								|| !AreSolverYawRotationsCompatible(
									PlacementFaceRule,
									Placement.YawRotationSteps,
									NeighborFaceRule,
									NeighborPlacement.YawRotationSteps))
							{
								continue;
							}

							for (const FGameplayTag& TraversalChannel :
								 PlacementFaceRule.ConnectedTraversalChannels)
							{
								if (!TraversalChannel.IsValid()
									|| !NeighborFaceRule.ConnectedTraversalChannels
											.HasTagExact(TraversalChannel))
								{
									continue;
								}

								AddMergedTraversalAuditEdge(
									Adjacency,
									FMergedTraversalAuditNode{
										Cell, TraversalChannel},
									FMergedTraversalAuditNode{
										NeighborCell, TraversalChannel});
							}
						}
					}
				}
			}

			for (const FLayoutPartitionSeamRecord& SeamRecord : PartitionSeams)
			{
				if (SeamRecord.InterfaceFamily
					!= LayoutGameplayTags::InterfacePartitionDoor)
				{
					continue;
				}

				const FIntVector OwnerStep(
					FMath::Clamp(
						SeamRecord.OwnerEndCell.X - SeamRecord.OwnerStartCell.X,
						-1,
						1),
					FMath::Clamp(
						SeamRecord.OwnerEndCell.Y - SeamRecord.OwnerStartCell.Y,
						-1,
						1),
					FMath::Clamp(
						SeamRecord.OwnerEndCell.Z - SeamRecord.OwnerStartCell.Z,
						-1,
						1));
				const FIntVector PassiveStep(
					FMath::Clamp(
						SeamRecord.PassiveEndCell.X
							- SeamRecord.PassiveStartCell.X,
						-1,
						1),
					FMath::Clamp(
						SeamRecord.PassiveEndCell.Y
							- SeamRecord.PassiveStartCell.Y,
						-1,
						1),
					FMath::Clamp(
						SeamRecord.PassiveEndCell.Z
							- SeamRecord.PassiveStartCell.Z,
						-1,
						1));

				for (int32 SegmentIndex = 0;
					 SegmentIndex < SeamRecord.SegmentCount;
					 ++SegmentIndex)
				{
					const FIntVector OwnerWorldCell =
						SeamRecord.OwnerStartCell + OwnerStep * SegmentIndex;
					const FIntVector PassiveWorldCell =
						SeamRecord.PassiveStartCell + PassiveStep * SegmentIndex;
					if (OwnerWorldCell == PassiveWorldCell)
					{
						continue;
					}

					const TArray<const FLayoutPlacedModule*>* OwnerPlacementsPtr =
						PlacementsByCell.Find(OwnerWorldCell);
					const TArray<const FLayoutPlacedModule*>* PassivePlacementsPtr =
						PlacementsByCell.Find(PassiveWorldCell);
					if (OwnerPlacementsPtr == nullptr
						|| PassivePlacementsPtr == nullptr
						|| OwnerPlacementsPtr->IsEmpty()
						|| PassivePlacementsPtr->IsEmpty())
					{
						continue;
					}

					for (const FLayoutPlacedModule* OwnerPlacementPtr :
						 *OwnerPlacementsPtr)
					{
						if (OwnerPlacementPtr == nullptr)
						{
							continue;
						}

						FLayoutFaceRule OwnerFaceRule;
						if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
								*OwnerPlacementPtr,
								OwnerWorldCell,
								SeamRecord.OwnerFaceDirection,
								OwnerFaceRule))
						{
							continue;
						}

						for (const FLayoutPlacedModule* PassivePlacementPtr :
							 *PassivePlacementsPtr)
						{
							if (PassivePlacementPtr == nullptr)
							{
								continue;
							}

							FLayoutFaceRule PassiveFaceRule;
							if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
									*PassivePlacementPtr,
									PassiveWorldCell,
									SeamRecord.PassiveFaceDirection,
									PassiveFaceRule))
							{
								continue;
							}

							for (const FGameplayTag& TraversalChannel :
								 OwnerFaceRule.ConnectedTraversalChannels)
							{
								if (!TraversalChannel.IsValid()
									|| !PassiveFaceRule.ConnectedTraversalChannels
											.HasTagExact(TraversalChannel))
								{
									continue;
								}

								AddMergedTraversalAuditEdge(
									Adjacency,
									FMergedTraversalAuditNode{
										OwnerWorldCell, TraversalChannel},
									FMergedTraversalAuditNode{
										PassiveWorldCell, TraversalChannel});
							}
						}
					}
				}
			}


			return Adjacency;
		}

		TSet<FMergedTraversalAuditNode> FloodMergedTraversalAuditReachableNodes(
			const TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>&
				Adjacency,
			const TArray<FMergedTraversalAuditNode>& StartNodes)
		{
			TSet<FMergedTraversalAuditNode> Visited;
			TArray<FMergedTraversalAuditNode> Frontier = StartNodes;
			for (const FMergedTraversalAuditNode& StartNode : StartNodes)
			{
				Visited.Add(StartNode);
			}

			while (!Frontier.IsEmpty())
			{
				const FMergedTraversalAuditNode Node =
					Frontier.Pop(EAllowShrinking::No);
				const TSet<FMergedTraversalAuditNode>* Neighbors =
					Adjacency.Find(Node);
				if (Neighbors == nullptr)
				{
					continue;
				}

				for (const FMergedTraversalAuditNode& Neighbor : *Neighbors)
				{
					if (Visited.Contains(Neighbor))
					{
						continue;
					}

					Visited.Add(Neighbor);
					Frontier.Add(Neighbor);
				}
			}

			return Visited;
		}

		bool DoesMergedTraversalAuditReachAnyCell(
			const TSet<FMergedTraversalAuditNode>& Visited,
			const TSet<FIntVector>& TargetCells)
		{
			for (const FMergedTraversalAuditNode& Node : Visited)
			{
				if (TargetCells.Contains(Node.Cell))
				{
					return true;
				}
			}

			return false;
		}

		/** Describes target graph degree and nearest reachable nodes for merged traversal rejection diagnostics. */
		FString DescribeMergedTraversalAuditConnectivity(
			const TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>& Adjacency,
			const TSet<FMergedTraversalAuditNode>& ReachableNodes,
			const TSet<FIntVector>& TargetCells)
		{
			TArray<FString> TargetNodes;
			for (const TPair<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>& Pair : Adjacency)
			{
				if (!TargetCells.Contains(Pair.Key.Cell))
				{
					continue;
				}
				TargetNodes.Add(FString::Printf(
					TEXT("%s[%s] reachable=%s degree=%d neighbors={%s}"),
					*Pair.Key.Cell.ToString(),
					*Pair.Key.TraversalChannel.ToString(),
					ReachableNodes.Contains(Pair.Key) ? TEXT("true") : TEXT("false"),
					Pair.Value.Num(),
					*FString::JoinBy(Pair.Value, TEXT(", "), [&ReachableNodes](const FMergedTraversalAuditNode& Node)
					{
						return FString::Printf(
							TEXT("%s[%s]/reachable=%s"),
							*Node.Cell.ToString(),
							*Node.TraversalChannel.ToString(),
							ReachableNodes.Contains(Node) ? TEXT("true") : TEXT("false"));
					})));
			}
			TargetNodes.Sort();

			TArray<FString> NearestReachable;
			int32 NearestDistance = MAX_int32;
			for (const FMergedTraversalAuditNode& ReachableNode : ReachableNodes)
			{
				for (const FIntVector& TargetCell : TargetCells)
				{
					const int32 Distance = FMath::Abs(ReachableNode.Cell.X - TargetCell.X)
						+ FMath::Abs(ReachableNode.Cell.Y - TargetCell.Y)
						+ FMath::Abs(ReachableNode.Cell.Z - TargetCell.Z);
					if (Distance < NearestDistance)
					{
						NearestDistance = Distance;
						NearestReachable.Reset();
					}
					if (Distance == NearestDistance && NearestReachable.Num() < 8)
					{
						NearestReachable.Add(FString::Printf(
							TEXT("%s[%s]"),
							*ReachableNode.Cell.ToString(),
							*ReachableNode.TraversalChannel.ToString()));
					}
				}
			}
			NearestReachable.Sort();
			return FString::Printf(
				TEXT("targetNodes={%s}; nearestReachableDistance=%s nearestReachable={%s}"),
				TargetNodes.IsEmpty() ? TEXT("<none>") : *FString::Join(TargetNodes, TEXT(" | ")),
				NearestDistance == MAX_int32 ? TEXT("<none>") : *FString::FromInt(NearestDistance),
				NearestReachable.IsEmpty() ? TEXT("<none>") : *FString::Join(NearestReachable, TEXT(", ")));
		}

		FString DescribeMergedTraversalAuditCells(
			const FLayoutSolveResult& SolveResult,
			const TSet<FIntVector>& Cells)
		{
			TArray<FString> CellDescriptions;
			for (const FIntVector& Cell : Cells)
			{
				TArray<FString> PlacementDescriptions;
				for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
				{
					FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
					if (!TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
							Placement,
							Cell,
							AuthoredLocalCell))
					{
						continue;
					}

					TArray<FString> TraversalChannelStrings;
					for (const FGameplayTag& TraversalChannel :
						 CollectPlacementTraversalChannelsAtWorldCellForMergedTraversalAudit(
							 Placement,
							 Cell))
					{
						if (TraversalChannel.IsValid())
						{
							TraversalChannelStrings.Add(
								TraversalChannel.ToString());
						}
					}
					TraversalChannelStrings.Sort();
					const FString PlacementDebugName =
						Placement.Module != nullptr
							? Placement.Module->GetName()
							: (!Placement.ModuleSnapshotId.IsNone()
								   ? Placement.ModuleSnapshotId.ToString()
								   : (Placement.CompositeModule != nullptr
										 ? TEXT("<composite>")
										 : TEXT("<null>")));
					PlacementDescriptions.Add(FString::Printf(
						TEXT("%s[yaw=%d intent=%d local=%s channels=%s]"),
						*PlacementDebugName,
						Placement.YawRotationSteps,
						static_cast<int32>(Placement.Intent),
						*AuthoredLocalCell.ToString(),
						TraversalChannelStrings.IsEmpty()
							? TEXT("<none>")
							: *FString::Join(
								TraversalChannelStrings,
								TEXT("|"))));
				}
				PlacementDescriptions.Sort();
				CellDescriptions.Add(FString::Printf(
					TEXT("%s -> %s"),
					*Cell.ToString(),
					PlacementDescriptions.IsEmpty()
						? TEXT("<no placements>")
						: *FString::Join(PlacementDescriptions, TEXT(", "))));
			}
			CellDescriptions.Sort();
			return CellDescriptions.IsEmpty()
				? TEXT("<none>")
				: FString::Join(CellDescriptions, TEXT("; "));
		}

		FString DescribeCommittedEndpointAnchorsForMergedTraversalAudit(
			const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
			const FIntVector& RegionOffset)
		{
			TArray<FString> AnchorDescriptions;
			for (const FLayoutCommittedEndpointAnchor& Anchor : Anchors)
			{
				TArray<FGameplayTag> TraversalChannels;
				Anchor.TraversalChannels.GetGameplayTagArray(
					TraversalChannels);
				TraversalChannels.Sort(
					[](const FGameplayTag& Left, const FGameplayTag& Right)
					{
						return Left.ToString() < Right.ToString();
					});

				AnchorDescriptions.Add(FString::Printf(
					TEXT("%s world=%s face=%s channels=%s"),
					*Anchor.LocalCell.ToString(),
					*(Anchor.LocalCell + RegionOffset).ToString(),
					*StaticEnum<ELayoutFaceDirection>()
						 ->GetNameStringByValue(
							 static_cast<int64>(Anchor.FaceDirection)),
					TraversalChannels.IsEmpty()
						? TEXT("<none>")
						: *FString::JoinBy(
							TraversalChannels,
							TEXT("|"),
							[](const FGameplayTag& Tag)
							{
								return Tag.ToString();
							})));
			}
			AnchorDescriptions.Sort();
			return AnchorDescriptions.IsEmpty()
				? TEXT("<none>")
				: FString::Join(AnchorDescriptions, TEXT("; "));
		}

		FString DescribeDirectCommitmentParentCellsForMergedTraversalAudit(
			const FLayoutSolveResult& SolveResult,
			const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
			const FIntVector& RegionOffset)
		{
			TSet<FIntVector> ParentCells;
			for (const FLayoutCommittedEndpointAnchor& Anchor : Anchors)
			{
				ParentCells.Add(
					Anchor.LocalCell + RegionOffset
					+ FLayoutDirectionUtils::ToCellDelta(Anchor.FaceDirection));
			}

			return DescribeMergedTraversalAuditCells(SolveResult, ParentCells);
		}

		bool TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			FIntVector& OutAuthoredLocalCell)
		{
			TArray<FIntVector> OccupiedLocalCells;
			AppendPlacementOccupiedLocalCellsForMergedTraversalAudit(
				Placement,
				OccupiedLocalCells);

			for (const FIntVector& AuthoredLocalCell : OccupiedLocalCells)
			{
				const FIntVector RotatedLocalCell =
					RotateLocalCellForMergedTraversalAudit(
						AuthoredLocalCell,
						GetPlacementBoundsCellsForMergedTraversalAudit(Placement),
						Placement.YawRotationSteps);
				if (Placement.Cell + RotatedLocalCell == WorldCell)
				{
					OutAuthoredLocalCell = AuthoredLocalCell;
					return true;
				}
			}

			return false;
		}

		bool DoesPlacementExposeFaceAtWorldCellForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			const ELayoutFaceDirection WorldFaceDirection,
			FLayoutFaceRule* OutFaceRule = nullptr)
		{
			FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
			if (!TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
					Placement,
					WorldCell,
					AuthoredLocalCell))
			{
				return false;
			}

			const ELayoutFaceDirection AuthoredFaceDirection =
				FLayoutDirectionUtils::RotateYaw(
					WorldFaceDirection,
					-Placement.YawRotationSteps);
			TSet<FIntVector> OccupiedAuthoredCells;
			TArray<FIntVector> OccupiedLocalCells;
			AppendPlacementOccupiedLocalCellsForMergedTraversalAudit(
				Placement,
				OccupiedLocalCells);
			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				OccupiedAuthoredCells.Add(OccupiedLocalCell);
			}

			if (OccupiedAuthoredCells.Contains(
					AuthoredLocalCell
					+ FLayoutDirectionUtils::ToCellDelta(AuthoredFaceDirection)))
			{
				return false;
			}

			FLayoutFaceRule FaceRule;
			if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
					Placement,
					WorldCell,
					WorldFaceDirection,
					FaceRule))
			{
				return false;
			}

			if (OutFaceRule != nullptr)
			{
				*OutFaceRule = FaceRule;
			}

			return true;
		}

	}

	const FLayoutPlacedModule* FindPlacementAtCellForMergedTraversalAudit(
			const TArray<FLayoutPlacedModule>& Placements,
			const FIntVector& Cell)
		{
			return Placements.FindByPredicate(
				[&Cell](const FLayoutPlacedModule& Placement)
				{
					FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
					return TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
						Placement,
						Cell,
						AuthoredLocalCell);
				});
		}

	// Freezes every child contact-face boundary point from the parent solve result into a
	// pointer-free handoff array. Runs in the parent worker after the parent solve and
	// before child proof dispatch, so the child's independent SolveRegion consumes only
	// this frozen array and never queries the live parent result. Each emitted boundary
	// point carries the parent placement's effective face-rule data (connection tag,
	// allowed connection tags, traversal channels, occupancy) for true face-rule
	// continuity between parent and child, and between sibling children.
	TArray<FLayoutSolveBoundaryPoint> FreezeChildIncomingBoundaryPointsFromParentResult(
		const FLayoutRegionSolveResult& ParentProofResult,
		const FAutomaticChildPlacement& Placement,
		const TArray<FLayoutRegionSolveResult>* SiblingResults)
	{
		TArray<FLayoutSolveBoundaryPoint> FrozenBoundaryPoints;

		const FIntVector& RegionOffset = Placement.RegionCellOffset;
		const FString& ParentRegionDebugPath =
			Placement.DirectChildCommitment.ParentRegionDebugPath;
		const bool bParentAscentDeferred =
			HasParentOwnedAscentObligation(Placement.NegotiatedResponsibilityContract);

		// Collect the child region's cells (child-local) so internal neighbors are
		// distinguished from contact faces that touch a non-child region cell (the
		// parent shell or the exterior). When the request supplies explicit planned
		// cells, iterate those; otherwise fall back to the full footprint.
		TArray<FIntVector> ChildCells;
		if (!Placement.ChildRequest.PlannedCells.IsEmpty())
		{
			ChildCells.Reserve(Placement.ChildRequest.PlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : Placement.ChildRequest.PlannedCells)
			{
				ChildCells.Add(PlannedCell.Cell);
			}
		}
		else
		{
			const FIntPoint FootprintXY =
				Placement.ChildRequest.ProfileSnapshot.MaximumFootprintInCells;
			const int32 NumLevels =
				Placement.ChildRequest.ProfileSnapshot.LevelCount;
			ChildCells.Reserve(FootprintXY.X * FootprintXY.Y * NumLevels);
			for (int32 Z = 0; Z < NumLevels; ++Z)
			{
				for (int32 Y = 0; Y < FootprintXY.Y; ++Y)
				{
					for (int32 X = 0; X < FootprintXY.X; ++X)
					{
						ChildCells.Add(FIntVector(X, Y, Z));
					}
				}
			}
		}

		TSet<FIntVector> ChildCellSet(ChildCells);

		for (const FIntVector& ChildCell : ChildCells)
		{
			for (int32 FaceIndex = 0; FaceIndex < 6; ++FaceIndex)
			{
				const ELayoutFaceDirection ChildFaceDirection =
					static_cast<ELayoutFaceDirection>(FaceIndex);
				const FIntVector NeighborLocal =
					ChildCell + FLayoutDirectionUtils::ToCellDelta(ChildFaceDirection);
				if (ChildCellSet.Contains(NeighborLocal))
				{
					// Internal neighbor: another child cell, not a contact face.
					continue;
				}

				// This face touches a non-child cell. The boundary point lives at the
				// neighbor cell (child-local) and faces back toward the child cell.
				const ELayoutFaceDirection BoundaryFaceDirection =
					FLayoutDirectionUtils::GetOpposite(ChildFaceDirection);
				const FIntVector NeighborWorld = NeighborLocal + RegionOffset;

				const FLayoutPlacedModule* ParentPlacement =
					FindPlacementAtCellForMergedTraversalAudit(
						ParentProofResult.SolveResult.Placements,
						NeighborWorld);
				if (ParentPlacement == nullptr)
				{
					// No parent or sibling placement at this contact cell: the face
					// sees exterior (empty) space outside the region footprint.
					// Sibling-to-sibling contacts are resolved post-merge by the
					// seam planner.
					FLayoutSolveBoundaryPoint BoundaryPoint;
					BoundaryPoint.LocalCell = NeighborLocal;
					BoundaryPoint.FaceDirection = BoundaryFaceDirection;
					BoundaryPoint.SourceRegionDebugPath = ParentRegionDebugPath;
					BoundaryPoint.SourceCell = NeighborLocal;
					// Region-to-region boundary points clear bRequiresBoundaryFacing so the
					// child can consume the boundary on ordinary inward-facing faces
					// (no BoundaryRequirement requirement). Face-rule continuity is
					// enforced separately via ConnectionTag/AllowedConnectionTags checks.
					BoundaryPoint.bRequiresBoundaryFacing = false;
					BoundaryPoint.bRepresentsFilledNeighbor = false;
					FrozenBoundaryPoints.Add(MoveTemp(BoundaryPoint));
					continue;
				}

				// Carry the parent placement's effective face-rule data for the face
				// that points back toward this child cell. This is true face-rule
				// continuity: the child's face rules evaluate against the parent's
				// actual solved face, not a synthesized boundary flag.
				FLayoutFaceRule ParentFaceRule;
				if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
						*ParentPlacement,
						NeighborWorld,
						BoundaryFaceDirection,
						ParentFaceRule))
				{
					// The contact face is internal to a multi-cell parent placement
					// (the parent occupies the next cell too). There is no exposed
					// parent face to represent; leave this face without a boundary
					// point rather than emitting empty face data.
					continue;
				}

				FLayoutSolveBoundaryPoint BoundaryPoint;
				BoundaryPoint.LocalCell = NeighborLocal;
				BoundaryPoint.FaceDirection = BoundaryFaceDirection;
				BoundaryPoint.SourceRegionDebugPath = ParentRegionDebugPath;
				BoundaryPoint.SourceCell = NeighborLocal;
				BoundaryPoint.SourceYawRotationSteps = ParentPlacement->YawRotationSteps;
				// Region-to-region boundary points clear bRequiresBoundaryFacing so the
				// child can consume the boundary on ordinary inward-facing faces
				// (no BoundaryRequirement requirement). Face-rule continuity is
				// enforced separately via ConnectionTag/AllowedConnectionTags checks.
				BoundaryPoint.bRequiresBoundaryFacing = false;
				BoundaryPoint.bRepresentsFilledNeighbor = true;

				// Use parent face rule directly as boundary constraint.
				// Parent CSP already proved collective child-boundary
				// compatibility; mirroring parent rules is sufficient.
				BoundaryPoint.ConnectionTag =
					ParentFaceRule.GetEffectiveConnectionTag();
				BoundaryPoint.AllowedConnectionTags =
					ParentFaceRule.GetEffectiveAllowedConnectionTags();
				BoundaryPoint.ConnectedTraversalChannels =
					ParentFaceRule.ConnectedTraversalChannels;
				BoundaryPoint.bRequireMatchingYawWithFilledNeighbor =
					ParentFaceRule.bRequireMatchingYawWithFilledNeighbor;
				if (bParentAscentDeferred)
				{
					// Parent-owned retained-shell seams prove face coverage for the
					// child boundary but are not child-owned host traversal routes.
					BoundaryPoint.ConnectedTraversalChannels.Reset();
				}
				FrozenBoundaryPoints.Add(MoveTemp(BoundaryPoint));
			}
		}

		return FrozenBoundaryPoints;
	}

		bool DoesPlacementExposeEntryCoverageOnFaceForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			const ELayoutFaceDirection FaceDirection)
		{
			FLayoutFaceRule FaceRule;
			return DoesPlacementExposeFaceAtWorldCellForMergedTraversalAudit(
					   Placement,
					   WorldCell,
					   FaceDirection,
					   &FaceRule)
				&& FaceRule.GetEffectiveConnectionTags().HasTagExact(
					LayoutGameplayTags::FaceEntry);
		}

		bool DoesFaceRuleSatisfyCommittedAnchorForMergedTraversalAudit(
			const FLayoutFaceRule& FaceRule,
			const FLayoutCommittedEndpointAnchor& Anchor)
		{
			bool bHasCompatibleConnectionTag = false;
			for (const FGameplayTag& CapabilityConnectionTag :
				 FaceRule.GetEffectiveConnectionTags())
			{
				if (!CapabilityConnectionTag.IsValid())
				{
					continue;
				}

				if (AreCommittedConnectionTagsCompatible(
						CapabilityConnectionTag,
						FaceRule.GetEffectiveAllowedConnectionTags(),
						Anchor.ConnectionTag,
						Anchor.AllowedConnectionTags))
				{
					bHasCompatibleConnectionTag = true;
					break;
				}
			}

			if (!bHasCompatibleConnectionTag)
			{
				return false;
			}

			return Anchor.TraversalChannels.IsEmpty()
				|| FaceRule.ConnectedTraversalChannels.HasAnyExact(
					Anchor.TraversalChannels);
		}

	bool DoesPlacementExposeCommittedAnchorOnFaceForMergedTraversalAudit(
			const FLayoutPlacedModule& Placement,
			const FIntVector& WorldCell,
			const FLayoutCommittedEndpointAnchor& Anchor)
		{
			FLayoutFaceRule FaceRule;
			return DoesPlacementExposeFaceAtWorldCellForMergedTraversalAudit(
					   Placement,
					   WorldCell,
					   Anchor.FaceDirection,
					   &FaceRule)
				&& DoesFaceRuleSatisfyCommittedAnchorForMergedTraversalAudit(
					FaceRule,
					Anchor);
		}

		bool ValidateCommittedEndpointAnchorRealization(
			const FLayoutRegionSolveScheduleResult& ScheduleResult,
			const FLayoutRegionSolveResult& RegionResult,
			FString& OutFailureReason)
		{
			for (const FLayoutCommittedEndpointAnchor& Anchor :
				 RegionResult.CommittedEndpointAnchors)
			{
				const FLayoutPlacedModule* LocalPlacement =
					FindPlacementAtCellForMergedTraversalAudit(
						RegionResult.SolveResult.Placements,
						Anchor.LocalCell);
				if (LocalPlacement == nullptr
					|| !DoesPlacementExposeCommittedAnchorOnFaceForMergedTraversalAudit(
						*LocalPlacement,
						Anchor.LocalCell,
						Anchor))
				{
					OutFailureReason = FString::Printf(
						TEXT("Region '%s' accepted committed endpoint anchor '%s' at local cell %s on face %s, but the realized child placement did not expose compatible face coverage on that face."),
						*RegionResult.RegionDebugPath,
						*Anchor.CommitmentId.ToString(),
						*Anchor.LocalCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()
							 ->GetNameStringByValue(
								 static_cast<int64>(Anchor.FaceDirection)));
					return false;
				}

				const FIntVector WorldCell =
					Anchor.LocalCell + RegionResult.RegionCellOffset;
				const FLayoutPlacedModule* MergedPlacement =
					FindPlacementAtCellForMergedTraversalAudit(
						ScheduleResult.MergedSolveResult.Placements,
						WorldCell);
				if (MergedPlacement == nullptr
					|| !DoesPlacementExposeCommittedAnchorOnFaceForMergedTraversalAudit(
						*MergedPlacement,
						WorldCell,
						Anchor))
				{
					OutFailureReason = FString::Printf(
						TEXT("Region '%s' lost merged compatible face coverage for committed endpoint anchor '%s' at world cell %s on face %s."),
						*RegionResult.RegionDebugPath,
						*Anchor.CommitmentId.ToString(),
						*WorldCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()
							 ->GetNameStringByValue(
								 static_cast<int64>(Anchor.FaceDirection)));
					return false;
				}
			}

			return true;
		}

		TSet<FIntVector> BuildPlannedCellSetForMergedTraversalAudit(
			const FLayoutRegionSolveResult& RegionResult)
		{
			TSet<FIntVector> PlannedCells;
			for (const FLayoutPlannedCell& PlannedCell :
				 RegionResult.SolveResult.PlannedCells)
			{
				PlannedCells.Add(PlannedCell.Cell);
			}
			return PlannedCells;
		}

		TMap<FIntVector, TSet<ELayoutFaceDirection>>
		BuildLocalSeamFaceDirectionsForMergedTraversalAudit(
			const FLayoutRegionSolveScheduleResult& ScheduleResult,
			const FLayoutRegionSolveResult& RegionResult)
		{
			TMap<FIntVector, TSet<ELayoutFaceDirection>> Result;
			for (const FLayoutPartitionSeamRecord& SeamRecord :
				 ScheduleResult.MergedSolveResult.PartitionSeams)
			{
				const bool bOwner =
					SeamRecord.OwnerRegionDebugPath == RegionResult.RegionDebugPath;
				const bool bPassive =
					SeamRecord.PassiveRegionDebugPath
					== RegionResult.RegionDebugPath;
				if (!bOwner && !bPassive)
				{
					continue;
				}

				const FIntVector WorldStartCell =
					bOwner ? SeamRecord.OwnerStartCell
						   : SeamRecord.PassiveStartCell;
				const FIntVector WorldEndCell =
					bOwner ? SeamRecord.OwnerEndCell
						   : SeamRecord.PassiveEndCell;
				const ELayoutFaceDirection FaceDirection =
					bOwner ? SeamRecord.OwnerFaceDirection
						   : SeamRecord.PassiveFaceDirection;

				FIntVector CurrentWorldCell = WorldStartCell;
				FIntVector Step = FIntVector::ZeroValue;
				if (WorldEndCell.X != WorldStartCell.X)
				{
					Step.X =
						(WorldEndCell.X > WorldStartCell.X) ? 1 : -1;
				}
				else if (WorldEndCell.Y != WorldStartCell.Y)
				{
					Step.Y =
						(WorldEndCell.Y > WorldStartCell.Y) ? 1 : -1;
				}
				else if (WorldEndCell.Z != WorldStartCell.Z)
				{
					Step.Z =
						(WorldEndCell.Z > WorldStartCell.Z) ? 1 : -1;
				}

				auto AddLocalCell = [&](const FIntVector& WorldCell)
				{
					const FIntVector LocalCell =
						WorldCell - RegionResult.RegionCellOffset;
					Result.FindOrAdd(LocalCell).Add(FaceDirection);
				};

				AddLocalCell(CurrentWorldCell);
				for (int32 SegmentIndex = 1;
					 SegmentIndex < SeamRecord.SegmentCount
					 && Step != FIntVector::ZeroValue;
					 ++SegmentIndex)
				{
					CurrentWorldCell += Step;
					AddLocalCell(CurrentWorldCell);
				}
				AddLocalCell(WorldEndCell);
			}

			return Result;
		}

		bool ValidateBoundaryFacingOrientationForMergedTraversalAudit(
			const FLayoutRegionSolveScheduleResult& ScheduleResult,
			const FLayoutRegionSolveResult& RegionResult,
			const FCommittedRecursiveScheduleState* CandidateSchedule,
			FString& OutFailureReason)
		{
			const TSet<FIntVector> PlannedCells =
				BuildPlannedCellSetForMergedTraversalAudit(RegionResult);
			const TMap<FIntVector, TSet<ELayoutFaceDirection>>
				SeamFaceDirections =
					BuildLocalSeamFaceDirectionsForMergedTraversalAudit(
						ScheduleResult,
						RegionResult);

			const FAutomaticChildPlacement* CertifiedPlacement =
				CandidateSchedule != nullptr
					? CandidateSchedule->Placements.FindByPredicate(
						[&RegionResult](const FAutomaticChildPlacement& Placement)
						{
							return Placement.ChildRegionDebugPath == RegionResult.RegionDebugPath;
						})
					: nullptr;

			static const ELayoutFaceDirection HorizontalDirections[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY
			};

			for (const FLayoutPlacedModule& Placement :
				 RegionResult.SolveResult.Placements)
			{
				if (Placement.Intent != ELayoutCellIntent::Boundary
					&& Placement.Intent != ELayoutCellIntent::Entry)
				{
					continue;
				}

				for (const ELayoutFaceDirection Direction :
					 HorizontalDirections)
				{
					const FIntVector NeighborCell =
						Placement.Cell
						+ FLayoutDirectionUtils::ToCellDelta(Direction);
					if (PlannedCells.Contains(NeighborCell))
					{
						continue;
					}

					if (const TSet<ELayoutFaceDirection>* SeamDirections =
							SeamFaceDirections.Find(Placement.Cell))
					{
						if (SeamDirections->Contains(Direction))
						{
							continue;
						}
					}

					const bool bHasCertifiedFilledNeighbor =
						CertifiedPlacement != nullptr
						&& CertifiedPlacement->CertifiedChildIncomingBoundaryPoints.ContainsByPredicate(
							[&](const FLayoutSolveBoundaryPoint& BoundaryPoint)
							{
								return BoundaryPoint.bUsesCertifiedReciprocalDomain
									&& BoundaryPoint.bRepresentsFilledNeighbor
									&& BoundaryPoint.LocalCell == NeighborCell
									&& BoundaryPoint.FaceDirection == FLayoutDirectionUtils::GetOpposite(Direction);
							});

					FLayoutFaceRule FaceRule;
					if (!TryGetPlacementFaceRuleAtWorldCellForMergedTraversalAudit(
							Placement,
							Placement.Cell,
							Direction,
							FaceRule))
					{
						OutFailureReason = FString::Printf(
							TEXT("Region '%s' realized placement at local cell %s without a face rule on %s."),
							*RegionResult.RegionDebugPath,
							*Placement.Cell.ToString(),
							*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
								static_cast<int64>(Direction)));
						return false;
					}
					if (bHasCertifiedFilledNeighbor)
					{
						if (FaceRule.BoundaryRequirement
							== ELayoutFaceBoundaryRequirement::MustFaceInterior)
						{
							OutFailureReason = FString::Printf(
								TEXT("Region '%s' realized placement '%s' at local cell %s with MustFaceInterior pointing outside the region on %s."),
								*RegionResult.RegionDebugPath,
								Placement.Module != nullptr
									? *Placement.Module->GetName()
									: *Placement.ModuleSnapshotId.ToString(),
								*Placement.Cell.ToString(),
								*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
									static_cast<int64>(Direction)));
							return false;
						}
						continue;
					}
					if (FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceInterior
						|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor)
					{
						const TSet<ELayoutFaceDirection>* AvailableSeamDirections =
							SeamFaceDirections.Find(Placement.Cell);
						OutFailureReason = FString::Printf(
							TEXT("Region '%s' realized placement '%s' at local cell %s with a face on %s that requires interior or filled-neighbor support, but that face is outside the region footprint and not part of a committed seam. Committed seam faces at this cell: %s."),
							*RegionResult.RegionDebugPath,
							Placement.Module != nullptr
								? *Placement.Module->GetName()
								: *Placement.ModuleSnapshotId.ToString(),
							*Placement.Cell.ToString(),
							*StaticEnum<ELayoutFaceDirection>()
								 ->GetNameStringByValue(
									 static_cast<int64>(Direction)),
							AvailableSeamDirections == nullptr
								? TEXT("<none>")
								: *FString::JoinBy(
									AvailableSeamDirections->Array(),
									TEXT(","),
									[](const ELayoutFaceDirection SeamDirection)
									{
										return StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
											static_cast<int64>(SeamDirection));
									}));
						return false;
					}
				}
			}

			return true;
		}

	bool ValidateMergedScheduleTraversalConnectivity(
			const FLayoutRegionSolveScheduleResult& ScheduleResult,
			const FString& RootRegionDebugPath,
			const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
			const FCommittedRecursiveScheduleState* CandidateSchedule,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();

			const FLayoutRegionSolveResult* RootRegionResult =
				ScheduleResult.RegionResults.FindByPredicate(
					[&RootRegionDebugPath](
						const FLayoutRegionSolveResult& RegionResult)
					{
						return RegionResult.SourceContentEntryId.IsNone()
							&& RegionResult.RegionDebugPath
								== RootRegionDebugPath
							&& !RegionResult.bDroppedAsOptionalChild;
					});
			if (RootRegionResult == nullptr)
			{
				return true;
			}

			TSet<FIntVector> RootEntryWorldCells;
			for (const FIntVector& EntryCell :
				 RootRegionResult->SolveResult.ExportedEntryCells)
			{
				RootEntryWorldCells.Add(
					EntryCell + RootRegionResult->RegionCellOffset);
			}
			if (RootEntryWorldCells.IsEmpty())
			{
				return true;
			}

			const TMap<FMergedTraversalAuditNode, TSet<FMergedTraversalAuditNode>>
				TraversalGraph = BuildMergedTraversalAuditGraph(
					ScheduleResult.MergedSolveResult,
					ScheduleResult.MergedSolveResult.PartitionSeams);
			TArray<FMergedTraversalAuditNode> RootStartNodes;
			for (const FIntVector& RootEntryWorldCell : RootEntryWorldCells)
			{
				for (const FLayoutPlacedModule& Placement :
					 ScheduleResult.MergedSolveResult.Placements)
				{
					FIntVector AuthoredLocalCell = FIntVector::ZeroValue;
					if (!TryGetPlacementCoveredAuthoredLocalCellForMergedTraversalAudit(
							Placement,
							RootEntryWorldCell,
							AuthoredLocalCell))
					{
						continue;
					}

					for (const FGameplayTag& TraversalChannel :
						 CollectPlacementTraversalChannelsAtWorldCellForMergedTraversalAudit(
							 Placement,
							 RootEntryWorldCell))
					{
						RootStartNodes.Add(
							FMergedTraversalAuditNode{
								RootEntryWorldCell, TraversalChannel});
					}
				}
			}
			if (RootStartNodes.IsEmpty())
			{
				return true;
			}

			const TSet<FMergedTraversalAuditNode> ReachableNodes =
				FloodMergedTraversalAuditReachableNodes(
					TraversalGraph,
					RootStartNodes);

			for (const FLayoutRegionSolveResult& RegionResult :
				 ScheduleResult.RegionResults)
			{
				if (RegionResult.bDroppedAsOptionalChild
					|| RegionResult.RegionDebugPath == RootRegionDebugPath
					|| RegionResult.SolveResult.ExportedEntryCells.IsEmpty())
				{
					continue;
				}

				if (!ValidateCommittedEndpointAnchorRealization(
						ScheduleResult,
						RegionResult,
						OutFailureReason))
				{
					return false;
				}

				if (!ValidateBoundaryFacingOrientationForMergedTraversalAudit(
						ScheduleResult,
						RegionResult,
						CandidateSchedule,
						OutFailureReason))
				{
					return false;
				}

				TSet<FIntVector> EntryWorldCells;
				for (const FIntVector& EntryCell :
					 RegionResult.SolveResult.ExportedEntryCells)
				{
					EntryWorldCells.Add(
						EntryCell + RegionResult.RegionCellOffset);
				}

				if (!EntryWorldCells.IsEmpty()
					&& !DoesMergedTraversalAuditReachAnyCell(
						ReachableNodes,
						EntryWorldCells))
				{
					OutFailureReason = FString::Printf(
						TEXT("Merged schedule traversal could not reach child region '%s' entry cells from root region '%s'. Root entry cells: %s. Child entry cells: %s. Child entry placements: %s. Child committed anchors: %s. Parent contact placements: %s"),
						*RegionResult.RegionDebugPath,
						*RootRegionDebugPath,
						*FString::JoinBy(
							RootEntryWorldCells,
							TEXT(", "),
							[](const FIntVector& Cell)
							{
								return Cell.ToString();
							}),
						*FString::JoinBy(
							EntryWorldCells,
							TEXT(", "),
							[](const FIntVector& Cell)
							{
								return Cell.ToString();
							}),
						*DescribeMergedTraversalAuditCells(
							ScheduleResult.MergedSolveResult,
							EntryWorldCells),
						*DescribeCommittedEndpointAnchorsForMergedTraversalAudit(
							RegionResult.CommittedEndpointAnchors,
							RegionResult.RegionCellOffset),
						*DescribeDirectCommitmentParentCellsForMergedTraversalAudit(
							ScheduleResult.MergedSolveResult,
							RegionResult.CommittedEndpointAnchors,
							RegionResult.RegionCellOffset));
					return false;
				}
			}

			if (!IsCommittedVerticalAccessRequirementSatisfied(
					VerticalAccessOwnership))
			{
				OutFailureReason = VerticalAccessOwnership.FailureReason.IsEmpty()
					? BuildCommittedVerticalAccessFailureReason(
						VerticalAccessOwnership)
					: VerticalAccessOwnership.FailureReason;
				return false;
			}

			if (!RootRegionResult->NegotiatedChildResponsibilityContracts.IsEmpty())
			{
				TSet<FIntVector> ExactParentProviderWorldCells;
				TSet<FString> ValidatedChildResponsibilityPaths;
				for (const FLayoutNegotiatedChildResponsibilityContract& Contract :
					 RootRegionResult->NegotiatedChildResponsibilityContracts)
				{
					if ((Contract.HostVerticalAccessResponsibility
							 == ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned
						 || Contract.HostVerticalAccessResponsibility
								== ELayoutNegotiatedHostVerticalAccessResponsibility::Composed)
						&& Contract.CountedParentProviderCount > 0)
					{
						for (const FIntVector& ParentLocalCell :
							 Contract.CountedParentVerticalAccessCells)
						{
							ExactParentProviderWorldCells.Add(
								ParentLocalCell
								+ RootRegionResult->RegionCellOffset);
						}
					}

					if (Contract.HostVerticalAccessResponsibility
						== ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned)
					{
						continue;
					}

					if (ValidatedChildResponsibilityPaths.Contains(
							Contract.ChildRegionDebugPath))
					{
						continue;
					}
					ValidatedChildResponsibilityPaths.Add(
						Contract.ChildRegionDebugPath);

					const FLayoutRegionSolveResult* OwnerRegionResult =
						ScheduleResult.RegionResults.FindByPredicate(
							[&Contract](
								const FLayoutRegionSolveResult& RegionResult)
							{
								return !RegionResult.bDroppedAsOptionalChild
									&& RegionResult.RegionDebugPath
										== Contract.ChildRegionDebugPath;
							});
					if (OwnerRegionResult == nullptr)
					{
						OutFailureReason = FString::Printf(
							TEXT("Merged schedule traversal expected negotiated child-responsibility proof for region '%s', but that child result was missing from the accepted schedule."),
							*Contract.ChildRegionDebugPath);
						return false;
					}

					TSet<FIntVector> RequiredRouteWorldCells;
					for (const FIntVector& LocalRouteCell :
						 Contract.RequiredChildInternalVerticalRouteCells)
					{
						RequiredRouteWorldCells.Add(
							LocalRouteCell
							+ OwnerRegionResult->RegionCellOffset);
					}
					if (RequiredRouteWorldCells.IsEmpty())
					{
						OutFailureReason = FString::Printf(
							TEXT("Merged schedule traversal expected negotiated child route proof for region '%s', but the contract exposed no committed route cells."),
							*Contract.ChildRegionDebugPath);
						return false;
					}

					for (const FIntVector& WorldRouteCell :
						 RequiredRouteWorldCells)
					{
						if (!DoesMergedTraversalAuditReachAnyCell(
								ReachableNodes,
								TSet<FIntVector>{WorldRouteCell}))
						{
							OutFailureReason = FString::Printf(
								TEXT("Merged schedule traversal could not reach negotiated child route cell %s in region '%s' from root region '%s'. Route cells: %s. Route placements: %s"),
								*WorldRouteCell.ToString(),
								*Contract.ChildRegionDebugPath,
								*RootRegionDebugPath,
								*FString::JoinBy(
									RequiredRouteWorldCells,
									TEXT(", "),
									[](const FIntVector& Cell)
									{
										return Cell.ToString();
									}),
								*DescribeMergedTraversalAuditCells(
									ScheduleResult.MergedSolveResult,
									RequiredRouteWorldCells));
							return false;
						}
					}

					auto ValidateCommittedAnchorReachability =
						[&ReachableNodes,
						 &RootRegionDebugPath,
						 &Contract,
						 OwnerRegionResult,
						 &ScheduleResult,
						 &OutFailureReason](
							const FLayoutCommittedEndpointAnchor& Anchor,
							const TCHAR* AnchorLabel) -> bool
						{
							const FIntVector WorldAnchorCell =
								Anchor.LocalCell
								+ OwnerRegionResult->RegionCellOffset;
							if (DoesMergedTraversalAuditReachAnyCell(
									ReachableNodes,
									TSet<FIntVector>{WorldAnchorCell}))
							{
								return true;
							}

							OutFailureReason = FString::Printf(
								TEXT("Merged schedule traversal could not reach the negotiated %s anchor at %s in region '%s' from root region '%s'. Child placements: %s"),
								AnchorLabel,
								*WorldAnchorCell.ToString(),
								*Contract.ChildRegionDebugPath,
								*RootRegionDebugPath,
								*DescribeMergedTraversalAuditCells(
									ScheduleResult.MergedSolveResult,
									TSet<FIntVector>{WorldAnchorCell}));
							return false;
						};

					if (Contract.bHasRequiredHostIngressAnchor
						&& !ValidateCommittedAnchorReachability(
							Contract.RequiredHostIngressAnchor,
							TEXT("host ingress")))
					{
						return false;
					}

					if (Contract.bHasRequiredHostEgressAnchor
						&& !ValidateCommittedAnchorReachability(
							Contract.RequiredHostEgressAnchor,
							TEXT("host egress")))
					{
						return false;
					}
				}

				for (const FIntVector& WorldCell :
					 ExactParentProviderWorldCells)
				{
					if (!DoesMergedTraversalAuditReachAnyCell(
							ReachableNodes,
							TSet<FIntVector>{WorldCell}))
					{
						OutFailureReason = FString::Printf(
							TEXT("Merged schedule traversal could not reach the negotiated parent vertical-access provider cell %s from root region '%s'. Exact committed parent cells: %s. Candidate placements: %s. Graph origin: %s"),
							*WorldCell.ToString(),
							*RootRegionDebugPath,
							*FString::JoinBy(
								ExactParentProviderWorldCells,
								TEXT(", "),
								[](const FIntVector& Cell)
								{
									return Cell.ToString();
								}),
							*DescribeMergedTraversalAuditCells(
								ScheduleResult.MergedSolveResult,
								ExactParentProviderWorldCells),
							*DescribeMergedTraversalAuditConnectivity(
								TraversalGraph,
								ReachableNodes,
								ExactParentProviderWorldCells));
						return false;
					}
				}

				return true;
			}

			TSet<FIntVector> CountedParentVerticalAccessWorldCells;
			for (const FIntVector& ParentLocalCell :
				 VerticalAccessOwnership.CountedParentVerticalAccessCells)
			{
				CountedParentVerticalAccessWorldCells.Add(
					ParentLocalCell + RootRegionResult->RegionCellOffset);
			}
			int32 ReachableCountedParentVerticalAccessWorldCellCount = 0;
			for (const FIntVector& WorldCell :
				 CountedParentVerticalAccessWorldCells)
			{
				if (DoesMergedTraversalAuditReachAnyCell(
						ReachableNodes,
						TSet<FIntVector>{WorldCell}))
				{
					++ReachableCountedParentVerticalAccessWorldCellCount;
				}
			}
			if (VerticalAccessOwnership.CountedParentProviderCount > 0
				&& ReachableCountedParentVerticalAccessWorldCellCount
					< VerticalAccessOwnership.CountedParentProviderCount)
			{
				OutFailureReason = FString::Printf(
					TEXT("Merged schedule traversal could not reach enough surviving parent vertical-access providers from root region '%s'. Reachable parent cells: %d / required %d. Candidate parent cells: %s. Candidate placements: %s"),
					*RootRegionDebugPath,
					ReachableCountedParentVerticalAccessWorldCellCount,
					VerticalAccessOwnership.CountedParentProviderCount,
					*FString::JoinBy(
						VerticalAccessOwnership.CountedParentVerticalAccessCells,
						TEXT(", "),
						[](const FIntVector& Cell)
						{
							return Cell.ToString();
						}),
					*DescribeMergedTraversalAuditCells(
						ScheduleResult.MergedSolveResult,
						CountedParentVerticalAccessWorldCells));
				return false;
			}

			return true;
		}
		FString DescribeCommittedPlacementValidationSchedule(
			const FCommittedRecursiveScheduleState& CandidateSchedule)
		{
			if (CandidateSchedule.Placements.IsEmpty())
			{
				return TEXT("<none>");
			}

			TArray<FString> PlacementParts;
			PlacementParts.Reserve(CandidateSchedule.Placements.Num());
			for (const FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
			{
				const FString ParentContactCells = Placement.ParentCommittedEntryCells.IsEmpty()
					? TEXT("<none>")
					: FString::JoinBy(Placement.ParentCommittedEntryCells, TEXT(","), [](const FIntVector& Cell)
					{
						return Cell.ToString();
					});
				const FString TraversalIngressCells = Placement.ParentCommittedTraversalAnchors.IsEmpty()
					? TEXT("<none>")
					: FString::JoinBy(
						Placement.ParentCommittedTraversalAnchors,
						TEXT(","),
						[](const FLayoutCommittedTraversalAnchor& Anchor)
						{
							return FString::Printf(
								TEXT("%s[%s]"),
								*Anchor.Cell.ToString(),
								*Anchor.TraversalChannel.ToString());
						});
				PlacementParts.Add(FString::Printf(
					TEXT("%s offset=%s footprint=%s parentContacts=%s traversalIngress=%s bridge=%d reserved=%d seams=%d"),
					*Placement.ChildRegionDebugPath,
					*Placement.RegionCellOffset.ToString(),
					*Placement.ChildRequest.FootprintSize.ToString(),
					*ParentContactCells,
					*TraversalIngressCells,
					Placement.bAllowsChildTraversalBridgeForCommittedContacts ? 1 : 0,
					Placement.ParentPlanReservedCells.Num(),
					Placement.SharedParentChildFaces.Num()));
			}

			return FString::Join(PlacementParts, TEXT(" | "));
		}

		void LogDeferredValidationCandidateOrderingRow(
			const TCHAR* MessageTag,
			const FString& RegionDebugPath,
			const int32 Rank,
			const FDeferredValidationCandidateOrderingRow& Row)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[%s] region=%s rank=%d score=%d rawScore=%d orderingPenalty=%d tie=%u placements=%d directPlacements=%d siblingOnlyPlacements=%d parentContacts=%d routePoints=%d routeCells=%d routeMultiCells=%d boundaryFacingRoutePoints=%d routeContractCells=%d routeContractLevels=%d routeContractTraversalAnchors=%d requiredRoutes=%d mainRoutes=%d boundaryRoutes=%d interiorRoutes=%d reservedRoutes=%d reservedMainRoutes=%d reservedBoundaryRoutes=%d reservedInteriorRoutes=%d reservedRouteFaces=%d reservedRouteContactAdjacency=%d reservedRouteTraversalAdjacency=%d warnings=%d skippedOptionals=%d scoreParts=%d preAdjust=%d preAdjustEntries=%d preAdjustResolve=%d entrySupportSaturation=%d entryIntent=%d entryCapability=%d entrySupport=%d entryProtected=%d entryAnchored=%d entryDistance=%d entryNoParent=%d entryDirectCount=%d entryNoParentCount=%d adjust=%d probeRoute=%d vertical=%d boundaryPenalty=%d shellPenalty=%d parentSeam=%d noSeamPenalty=%d siblingSeam=%d rootPenalty=%d providerPenalty=%d boundaryTouches=%d shellTouches=%d committedFaces=%s localContacts=%s localTraversalAnchors=%s routeDemand=%s signature=%s"),
				MessageTag,
				*RegionDebugPath,
				Rank,
				Row.Score,
				Row.RawScore,
				Row.OrderingPenalty,
				Row.TieBreakHash,
				Row.PlacementCount,
				Row.DirectPlacementCount,
				Row.SiblingOnlyPlacementCount,
				Row.ParentContactCount,
				Row.TraversalBoundaryPointCount,
				Row.TraversalBoundaryCellCount,
				Row.MultiTraversalBoundaryCellCount,
				Row.BoundaryFacingTraversalPointCount,
				Row.NegotiatedRouteCellCount,
				Row.NegotiatedRouteSpanLevelCount,
				Row.NegotiatedInterfaceTraversalAnchorCount,
				Row.RequiredRouteConstraintCount,
				Row.MainRouteConstraintCount,
				Row.BoundaryRouteConstraintCount,
				Row.InteriorRouteConstraintCount,
				Row.ReservedRouteConstraintCount,
				Row.ReservedMainRouteConstraintCount,
				Row.ReservedBoundaryRouteConstraintCount,
				Row.ReservedInteriorRouteConstraintCount,
				Row.ReservedRouteFaceRequirementCount,
				Row.ReservedRouteContactAdjacencyCount,
				Row.ReservedRouteTraversalAdjacencyCount,
				Row.WarningCount,
				Row.SkippedOptionalCount,
				Row.bHasScoreBreakdown ? 1 : 0,
				Row.PreAdjustmentResolvedScore,
				Row.PreAdjustmentEntryAccumulatedScore,
				Row.PreAdjustmentResolveAdjustment,
				Row.DirectParentSupportSaturationPenalty,
				Row.EntryIntentScore,
				Row.EntryParentCapabilitySupportContribution,
				Row.EntryParentSupportContribution,
				Row.EntryProtectedTraversalPenalty,
				Row.EntryAnchoredIntentPenalty,
				Row.EntryPriorityDistancePenalty,
				Row.EntryNoParentResolutionScore,
				Row.DirectParentEntryCount,
				Row.NoParentEntryCount,
				Row.AdjustmentTotal,
				Row.ProbeRouteConstraintScore,
				Row.VerticalLevelPenalty,
				Row.BoundaryTouchPenalty,
				Row.ShellAdjacencyPenalty,
				Row.ParentChildSeamScore,
				Row.RequiredCopiesNoSeamPenalty,
				Row.SiblingSeamScore,
				Row.RootDistancePenalty,
				Row.ProviderDistancePenalty,
				Row.ParentBoundaryTouchCount,
				Row.ParentShellAdjacencyCount,
				*Row.CommittedContactFaceSignature,
				*Row.NormalizedContactSignature,
				*Row.NormalizedTraversalAnchorSignature,
				*Row.NegotiatedRouteDemandSignature,
				*Row.PlacementSignature);
		}

		FString BuildCommittedContactFaceSignature(
			const FAutomaticChildPlacement& Placement)
		{
			int32 PosXCount = 0;
			int32 NegXCount = 0;
			int32 PosYCount = 0;
			int32 NegYCount = 0;
			int32 PosZCount = 0;
			int32 NegZCount = 0;
			for (const FLayoutCommittedEndpointAnchor& Commitment :
				Placement.ParentDirectContactCommitments)
			{
				switch (Commitment.FaceDirection)
				{
				case ELayoutFaceDirection::PosX:
					++PosXCount;
					break;
				case ELayoutFaceDirection::NegX:
					++NegXCount;
					break;
				case ELayoutFaceDirection::PosY:
					++PosYCount;
					break;
				case ELayoutFaceDirection::NegY:
					++NegYCount;
					break;
				case ELayoutFaceDirection::PosZ:
					++PosZCount;
					break;
				case ELayoutFaceDirection::NegZ:
					++NegZCount;
					break;
				default:
					break;
				}
			}

			TArray<FString> Parts;
			auto AppendFacePart = [&Parts](const TCHAR* Label, const int32 Count)
			{
				if (Count > 0)
				{
					Parts.Add(FString::Printf(TEXT("%s:%d"), Label, Count));
				}
			};
			AppendFacePart(TEXT("+X"), PosXCount);
			AppendFacePart(TEXT("-X"), NegXCount);
			AppendFacePart(TEXT("+Y"), PosYCount);
			AppendFacePart(TEXT("-Y"), NegYCount);
			AppendFacePart(TEXT("+Z"), PosZCount);
			AppendFacePart(TEXT("-Z"), NegZCount);
			return Parts.IsEmpty() ? TEXT("<none>") : FString::Join(Parts, TEXT(","));
		}

		FString BuildNormalizedContactSignature(
			const FAutomaticChildPlacement& Placement)
		{
			if (Placement.ParentCommittedEntryCells.IsEmpty())
			{
				return TEXT("<none>");
			}

			TArray<FString> Parts;
			Parts.Reserve(Placement.ParentCommittedEntryCells.Num());
			for (const FIntVector& ContactCell : Placement.ParentCommittedEntryCells)
			{
				Parts.Add((ContactCell - Placement.RegionCellOffset).ToString());
			}
			Parts.Sort();
			return FString::Join(Parts, TEXT(","));
		}

		FString BuildNormalizedTraversalAnchorSignature(
			const FAutomaticChildPlacement& Placement)
		{
			if (Placement.ParentCommittedTraversalAnchors.IsEmpty())
			{
				return TEXT("<none>");
			}

			TArray<FString> Parts;
			Parts.Reserve(Placement.ParentCommittedTraversalAnchors.Num());
			for (const FLayoutCommittedTraversalAnchor& Anchor :
				Placement.ParentCommittedTraversalAnchors)
			{
				Parts.Add(FString::Printf(
					TEXT("%s[%s]"),
					*(Anchor.Cell - Placement.RegionCellOffset).ToString(),
					Anchor.TraversalChannel.IsValid()
						? *Anchor.TraversalChannel.ToString()
						: TEXT("<none>")));
			}
			Parts.Sort();
			return FString::Join(Parts, TEXT(","));
		}

		const TCHAR* DescribeNegotiatedHostVerticalAccessResponsibility(
			const ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility)
		{
			switch (Responsibility)
			{
			case ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned:
				return TEXT("ChildOwned");
			case ELayoutNegotiatedHostVerticalAccessResponsibility::Composed:
				return TEXT("Composed");
			case ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned:
			default:
				return TEXT("ParentOwned");
			}
		}

		FString BuildNegotiatedRouteDemandSignature(
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
		{
			TArray<int32> SortedSpanLevels =
				Contract.RequiredChildInternalVerticalSpanLevels;
			SortedSpanLevels.Sort();
			TArray<FString> SpanParts;
			SpanParts.Reserve(SortedSpanLevels.Num());
			for (const int32 SpanLevel : SortedSpanLevels)
			{
				SpanParts.Add(LexToString(SpanLevel));
			}

			TArray<FIntVector> SortedRouteCells =
				Contract.RequiredChildInternalVerticalRouteCells;
			SortedRouteCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return Left.Z != Right.Z
					? Left.Z < Right.Z
					: (Left.Y != Right.Y
						? Left.Y < Right.Y
						: Left.X < Right.X);
			});
			TArray<FString> RouteCellParts;
			RouteCellParts.Reserve(SortedRouteCells.Num());
			for (const FIntVector& RouteCell : SortedRouteCells)
			{
				RouteCellParts.Add(RouteCell.ToString());
			}

			int32 InterfaceTraversalAnchorCount = 0;
			for (const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract :
				Contract.CommittedParentChildInterfacesByLevel)
			{
				InterfaceTraversalAnchorCount +=
					InterfaceContract.TraversalAnchors.Num();
			}

			return FString::Printf(
				TEXT("hostVA=%s|pair=%s|levels=%s|route=%s|ifaceTraversal=%d|ingress=%d|egress=%d"),
				DescribeNegotiatedHostVerticalAccessResponsibility(
					Contract.HostVerticalAccessResponsibility),
				Contract.RequiredChildGenerallyConnectableAnchorPairId == NAME_None
					? TEXT("<none>")
					: *Contract.RequiredChildGenerallyConnectableAnchorPairId.ToString(),
				SpanParts.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(SpanParts, TEXT(",")),
				RouteCellParts.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(RouteCellParts, TEXT(",")),
				InterfaceTraversalAnchorCount,
				Contract.bHasRequiredHostIngressAnchor ? 1 : 0,
				Contract.bHasRequiredHostEgressAnchor ? 1 : 0);
		}

		bool IsCellEqualOrFaceAdjacent(
			const FIntVector& Cell,
			const TSet<FIntVector>& CandidateCells)
		{
			if (CandidateCells.Contains(Cell))
			{
				return true;
			}

			static const ELayoutFaceDirection Directions[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY,
				ELayoutFaceDirection::PosZ,
				ELayoutFaceDirection::NegZ
			};
			for (const ELayoutFaceDirection Direction : Directions)
			{
				const FIntVector NeighborCell =
					Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (CandidateCells.Contains(NeighborCell))
				{
					return true;
				}
			}

			return false;
		}

		FDeferredValidationParentRouteDemandFeatures
		BuildDeferredValidationParentRouteDemandFeatures(
			const FLayoutRegionSolveRequest& RootRequest,
			const FCommittedRecursiveScheduleState& Schedule)
		{
			FDeferredValidationParentRouteDemandFeatures Features;

			TSet<FIntVector> ReservedCells;
			TSet<FIntVector> ParentContactCells;
			TSet<FIntVector> ParentTraversalAnchorCells;
			for (const FAutomaticChildPlacement& Placement : Schedule.Placements)
			{
				for (const FIntVector& ReservedCell : Placement.ParentPlanReservedCells)
				{
					ReservedCells.Add(ReservedCell);
				}
				for (const FIntVector& ParentContactCell : Placement.ParentCommittedEntryCells)
				{
					ParentContactCells.Add(ParentContactCell);
				}
				for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
					Placement.ParentCommittedTraversalAnchors)
				{
					ParentTraversalAnchorCells.Add(TraversalAnchor.Cell);
				}
			}

			for (const FLayoutRouteConstraintRecord& RouteConstraint :
				RootRequest.RequiredRouteConstraints)
			{
				++Features.RequiredRouteConstraintCount;
				Features.MainRouteConstraintCount +=
					RouteConstraint.bScoreAsMainRoute ? 1 : 0;
				if (RouteConstraint.Intent == ELayoutCellIntent::Boundary)
				{
					++Features.BoundaryRouteConstraintCount;
				}
				else if (RouteConstraint.Intent == ELayoutCellIntent::Interior
					|| RouteConstraint.Intent == ELayoutCellIntent::Core)
				{
					++Features.InteriorRouteConstraintCount;
				}

				if (!ReservedCells.Contains(RouteConstraint.Cell))
				{
					continue;
				}

				++Features.ReservedRouteConstraintCount;
				Features.ReservedMainRouteConstraintCount +=
					RouteConstraint.bScoreAsMainRoute ? 1 : 0;
				if (RouteConstraint.Intent == ELayoutCellIntent::Boundary)
				{
					++Features.ReservedBoundaryRouteConstraintCount;
				}
				else if (RouteConstraint.Intent == ELayoutCellIntent::Interior
					|| RouteConstraint.Intent == ELayoutCellIntent::Core)
				{
					++Features.ReservedInteriorRouteConstraintCount;
				}
				Features.ReservedRouteFaceRequirementCount +=
					RouteConstraint.FaceRequirements.Num();
				Features.ReservedRouteContactAdjacencyCount +=
					IsCellEqualOrFaceAdjacent(
						RouteConstraint.Cell,
						ParentContactCells)
						? 1
						: 0;
				Features.ReservedRouteTraversalAdjacencyCount +=
					IsCellEqualOrFaceAdjacent(
						RouteConstraint.Cell,
						ParentTraversalAnchorCells)
						? 1
						: 0;
			}

			return Features;
		}

		FDeferredValidationCandidateSelectionFeatures
		BuildDeferredValidationCandidateSelectionFeatures(
			const FLayoutRegionSolveRequest& RootRequest,
			const FCommittedRecursiveScheduleState& Schedule)
		{
			FDeferredValidationCandidateSelectionFeatures Features;
			Features.WarningCount = Schedule.Warnings.Num();
			Features.SkippedOptionalCount = Schedule.SkippedOptionalPlacements.Num();
			Features.ParentRouteDemand =
				BuildDeferredValidationParentRouteDemandFeatures(
					RootRequest,
					Schedule);
			Features.Placements.Reserve(Schedule.Placements.Num());
			for (const FAutomaticChildPlacement& Placement : Schedule.Placements)
			{
				Features.Placements.Add(Placement.SelectionFeatures);
			}
			return Features;
		}

		int32 ComputeDeferredValidationOrderingPenalty(
			const FDeferredValidationCandidateSelectionFeatures& Features)
		{
			int32 Penalty = 0;
			for (const FChildPlacementSelectionFeatures& Placement :
				Features.Placements)
			{
				if (Placement.AuthoredPlacementZone == ELayoutPlacementZone::Interior
					&& Placement.ParentExteriorReservedCellCount > 0)
				{
					// Interior children may use certified shared shells when necessary,
					// but shell seam bonuses must not outrank child-clear interior fits.
					Penalty += FMath::Max(0, Placement.ParentChildSeamScore);
				}
			}
			return Penalty;
		}

		FDeferredValidationCandidateOrderingRow BuildDeferredValidationCandidateOrderingRow(
			const FPlacementBridgeDeferredValidationCandidate& Candidate)
		{
			FDeferredValidationCandidateOrderingRow Row;
			Row.Score = Candidate.Score;
			Row.RawScore = Candidate.RawScore;
			Row.OrderingPenalty = Candidate.OrderingPenalty;
			Row.TieBreakHash = Candidate.TieBreakHash;
			Row.PlacementCount = Candidate.SelectionFeatures.Placements.Num();
			Row.RequiredRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.RequiredRouteConstraintCount;
			Row.MainRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.MainRouteConstraintCount;
			Row.BoundaryRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.BoundaryRouteConstraintCount;
			Row.InteriorRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.InteriorRouteConstraintCount;
			Row.ReservedRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedRouteConstraintCount;
			Row.ReservedMainRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedMainRouteConstraintCount;
			Row.ReservedBoundaryRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedBoundaryRouteConstraintCount;
			Row.ReservedInteriorRouteConstraintCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedInteriorRouteConstraintCount;
			Row.ReservedRouteFaceRequirementCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedRouteFaceRequirementCount;
			Row.ReservedRouteContactAdjacencyCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedRouteContactAdjacencyCount;
			Row.ReservedRouteTraversalAdjacencyCount =
				Candidate.SelectionFeatures.ParentRouteDemand.ReservedRouteTraversalAdjacencyCount;
			Row.WarningCount = Candidate.SelectionFeatures.WarningCount;
			Row.SkippedOptionalCount =
				Candidate.SelectionFeatures.SkippedOptionalCount;
			TArray<FString> PlacementParts;
			PlacementParts.Reserve(Candidate.Schedule.Placements.Num());
			for (int32 PlacementIndex = 0;
				PlacementIndex < Candidate.Schedule.Placements.Num();
				++PlacementIndex)
			{
				const FAutomaticChildPlacement& Placement =
					Candidate.Schedule.Placements[PlacementIndex];
				const FChildPlacementSelectionFeatures& Features =
					Candidate.SelectionFeatures.Placements[PlacementIndex];
				Row.ParentContactCount += Features.ParentContactCount;
				if (Features.bUsesDirectContact)
				{
					++Row.DirectPlacementCount;
				}
				else
				{
					++Row.SiblingOnlyPlacementCount;
				}
				Row.TraversalBoundaryPointCount +=
					Features.TraversalBoundaryPointCount;
				Row.TraversalBoundaryCellCount +=
					Features.TraversalBoundaryCellCount;
				Row.MultiTraversalBoundaryCellCount +=
					Features.MultiTraversalBoundaryCellCount;
				Row.BoundaryFacingTraversalPointCount +=
					Features.BoundaryFacingTraversalPointCount;
				Row.NegotiatedRouteCellCount +=
					Features.NegotiatedRouteCellCount;
				Row.NegotiatedRouteSpanLevelCount +=
					Features.NegotiatedRouteSpanLevelCount;
				Row.NegotiatedInterfaceTraversalAnchorCount +=
					Features.NegotiatedInterfaceTraversalAnchorCount;

				TArray<FString> ContactParts;
				ContactParts.Reserve(Placement.ParentCommittedEntryCells.Num());
				for (const FIntVector& ContactCell : Placement.ParentCommittedEntryCells)
				{
					ContactParts.Add(ContactCell.ToString());
				}
				ContactParts.Sort();

				PlacementParts.Add(FString::Printf(
					TEXT("%s@%s|contacts=%s|traversal=%d|reserved=%d|shared=%d"),
					Placement.EntryId == NAME_None ? TEXT("<none>") : *Placement.EntryId.ToString(),
					*Placement.RegionCellOffset.ToString(),
					ContactParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ContactParts, TEXT(",")),
					Placement.ParentCommittedTraversalAnchors.Num(),
					Placement.ParentPlanReservedCells.Num(),
					Placement.SharedParentChildFaces.Num()));

					if (Placement.ScoreBreakdown.bHasDiagnostics)
					{
					Row.bHasScoreBreakdown = true;
					Row.PreAdjustmentResolvedScore +=
						Placement.ScoreBreakdown.PreAdjustmentResolvedScore;
					Row.PreAdjustmentEntryAccumulatedScore +=
						Placement.ScoreBreakdown.PreAdjustmentEntryAccumulatedScore;
					Row.PreAdjustmentResolveAdjustment +=
						Placement.ScoreBreakdown.PreAdjustmentResolveAdjustment;
					Row.DirectParentSupportSaturationPenalty +=
						Placement.ScoreBreakdown.DirectParentSupportSaturationPenalty;
					Row.EntryIntentScore +=
						Placement.ScoreBreakdown.EntryIntentScore;
					Row.EntryParentCapabilitySupportContribution +=
						Placement.ScoreBreakdown.EntryParentCapabilitySupportContribution;
					Row.EntryParentSupportContribution +=
						Placement.ScoreBreakdown.EntryParentSupportContribution;
					Row.EntryProtectedTraversalPenalty +=
						Placement.ScoreBreakdown.EntryProtectedTraversalPenalty;
					Row.EntryAnchoredIntentPenalty +=
						Placement.ScoreBreakdown.EntryAnchoredIntentPenalty;
					Row.EntryPriorityDistancePenalty +=
						Placement.ScoreBreakdown.EntryPriorityDistancePenalty;
					Row.EntryNoParentResolutionScore +=
						Placement.ScoreBreakdown.EntryNoParentResolutionScore;
					Row.DirectParentEntryCount +=
						Placement.ScoreBreakdown.DirectParentEntryCount;
					Row.NoParentEntryCount +=
						Placement.ScoreBreakdown.NoParentEntryCount;
					Row.AdjustmentTotal +=
						Placement.ScoreBreakdown.AdjustmentTotal;
					Row.ProbeRouteConstraintScore +=
						Placement.ScoreBreakdown.ProbeRouteConstraintScore;
					Row.VerticalLevelPenalty +=
						Placement.ScoreBreakdown.VerticalLevelPenalty;
					Row.BoundaryTouchPenalty +=
						Placement.ScoreBreakdown.BoundaryTouchPenalty;
					Row.ShellAdjacencyPenalty +=
						Placement.ScoreBreakdown.ShellAdjacencyPenalty;
					Row.ParentChildSeamScore +=
						Placement.ScoreBreakdown.ParentChildSeamScore;
					Row.RequiredCopiesNoSeamPenalty +=
						Placement.ScoreBreakdown.RequiredCopiesNoSeamPenalty;
					Row.SiblingSeamScore +=
						Placement.ScoreBreakdown.SiblingSeamScore;
					Row.RootDistancePenalty +=
						Placement.ScoreBreakdown.RootDistancePenalty;
					Row.ProviderDistancePenalty +=
						Placement.ScoreBreakdown.ProviderDistancePenalty;
					Row.ParentBoundaryTouchCount +=
						Placement.ScoreBreakdown.ParentBoundaryTouchCount;
						Row.ParentShellAdjacencyCount +=
							Placement.ScoreBreakdown.ParentShellAdjacencyCount;
					}

					const FString NormalizedContactSignature =
						BuildNormalizedContactSignature(Placement);
					if (Row.NormalizedContactSignature.IsEmpty())
					{
						Row.NormalizedContactSignature = NormalizedContactSignature;
					}
					else
					{
						Row.NormalizedContactSignature += FString::Printf(
							TEXT(" | %s"),
							*NormalizedContactSignature);
					}

					const FString NormalizedTraversalAnchorSignature =
						BuildNormalizedTraversalAnchorSignature(Placement);
					if (Row.NormalizedTraversalAnchorSignature.IsEmpty())
					{
						Row.NormalizedTraversalAnchorSignature =
							NormalizedTraversalAnchorSignature;
					}
					else
					{
						Row.NormalizedTraversalAnchorSignature += FString::Printf(
							TEXT(" | %s"),
							*NormalizedTraversalAnchorSignature);
					}

					const FString NegotiatedRouteDemandSignature =
						BuildNegotiatedRouteDemandSignature(
							Placement.NegotiatedResponsibilityContract);
					if (Row.NegotiatedRouteDemandSignature.IsEmpty())
					{
						Row.NegotiatedRouteDemandSignature =
							NegotiatedRouteDemandSignature;
					}
					else
					{
						Row.NegotiatedRouteDemandSignature += FString::Printf(
							TEXT(" | %s"),
							*NegotiatedRouteDemandSignature);
					}
				}
			PlacementParts.Sort();
			Row.PlacementSignature =
				PlacementParts.IsEmpty() ? TEXT("<none>") : FString::Join(PlacementParts, TEXT(";"));
			Row.CommittedContactFaceSignature = Candidate.Schedule.Placements.Num() == 1
				? BuildCommittedContactFaceSignature(Candidate.Schedule.Placements[0])
				: FString::JoinBy(
					Candidate.Schedule.Placements,
					TEXT(";"),
					[](const FAutomaticChildPlacement& Placement)
					{
						return FString::Printf(
							TEXT("%s@%s[%s]"),
							Placement.EntryId == NAME_None
								? TEXT("<none>")
								: *Placement.EntryId.ToString(),
							*Placement.RegionCellOffset.ToString(),
							*BuildCommittedContactFaceSignature(Placement));
					});

			return Row;
		}

		void PopulateDeferredValidationCandidateOrderingSummary(
			const TArray<FPlacementBridgeDeferredValidationCandidate>& SortedCandidates,
			const int32 MaxCandidateCount,
			FDeferredValidationCandidateOrderingSummary& OutSummary)
		{
			OutSummary = FDeferredValidationCandidateOrderingSummary{};
			OutSummary.QueuedCount = SortedCandidates.Num();
			OutSummary.RetainedCount =
				FMath::Min(SortedCandidates.Num(), MaxCandidateCount);
			if (SortedCandidates.IsEmpty())
			{
				return;
			}

			TSet<int32> RetainedScores;
			RetainedScores.Reserve(OutSummary.RetainedCount);
			const int32 BestScore = SortedCandidates[0].Score;
			int32 TopScoreTieCount = 0;
			for (int32 CandidateIndex = 0;
				CandidateIndex < OutSummary.RetainedCount;
				++CandidateIndex)
			{
				const FPlacementBridgeDeferredValidationCandidate& Candidate =
					SortedCandidates[CandidateIndex];
				RetainedScores.Add(Candidate.Score);
				if (Candidate.Score == BestScore)
				{
					++TopScoreTieCount;
				}
			}

			OutSummary.DistinctRetainedScoreCount = RetainedScores.Num();
			OutSummary.TopScoreTieCount = TopScoreTieCount;
			OutSummary.BestScore = SortedCandidates[0].Score;
			OutSummary.CutoffScore =
				SortedCandidates[OutSummary.RetainedCount - 1].Score;

			static constexpr int32 MaxLoggedCandidates = 8;
			const int32 LoggedCount =
				FMath::Min(OutSummary.RetainedCount, MaxLoggedCandidates);
			OutSummary.TopCandidates.Reserve(LoggedCount);
			for (int32 CandidateIndex = 0; CandidateIndex < LoggedCount; ++CandidateIndex)
			{
				OutSummary.TopCandidates.Add(
					BuildDeferredValidationCandidateOrderingRow(
						SortedCandidates[CandidateIndex]));
			}

			static constexpr int32 MaxQueuedFamiliesToLog = 8;
			TArray<FDeferredValidationQueuedFamilySummary> QueuedFamilySummaries;
			TMap<int32, int32> QueuedFamilyIndexByContactCount;
			QueuedFamilySummaries.Reserve(MaxQueuedFamiliesToLog);
			for (int32 CandidateIndex = 0;
				CandidateIndex < SortedCandidates.Num();
				++CandidateIndex)
			{
				const FDeferredValidationCandidateOrderingRow Row =
					BuildDeferredValidationCandidateOrderingRow(
						SortedCandidates[CandidateIndex]);
				const int32 ParentContactCount = Row.ParentContactCount;
				if (int32* ExistingIndex =
						QueuedFamilyIndexByContactCount.Find(ParentContactCount))
				{
					++QueuedFamilySummaries[*ExistingIndex].CandidateCount;
					continue;
				}

				FDeferredValidationQueuedFamilySummary& Summary =
					QueuedFamilySummaries.AddDefaulted_GetRef();
				Summary.ParentContactCount = ParentContactCount;
				Summary.CandidateCount = 1;
				Summary.BestRank = CandidateIndex + 1;
				Summary.BestCandidate = Row;
				QueuedFamilyIndexByContactCount.Add(
					ParentContactCount,
					QueuedFamilySummaries.Num() - 1);
			}

			QueuedFamilySummaries.Sort(
				[](const FDeferredValidationQueuedFamilySummary& Left,
					const FDeferredValidationQueuedFamilySummary& Right)
				{
					if (Left.BestCandidate.Score != Right.BestCandidate.Score)
					{
						return Left.BestCandidate.Score > Right.BestCandidate.Score;
					}
					return Left.ParentContactCount > Right.ParentContactCount;
				});
			const int32 QueuedFamilyLoggedCount =
				FMath::Min(QueuedFamilySummaries.Num(), MaxQueuedFamiliesToLog);
			OutSummary.QueuedFamiliesByParentContact.Reserve(QueuedFamilyLoggedCount);
			for (int32 FamilyIndex = 0;
				FamilyIndex < QueuedFamilyLoggedCount;
				++FamilyIndex)
			{
				OutSummary.QueuedFamiliesByParentContact.Add(
					QueuedFamilySummaries[FamilyIndex]);
			}
		}

		bool CanUseIndependentDeferredProofValidation(
			const FCommittedRecursiveScheduleState& CandidateSchedule)
		{
			return !CandidateSchedule.Placements.IsEmpty()
				&& !CandidateSchedule.Placements.ContainsByPredicate(
					[](const FAutomaticChildPlacement& Placement)
					{
						return Placement.bAllowsChildTraversalBridgeForCommittedContacts;
					});
		}

		void BuildPlacementValidationScheduleRequest(
			const FLayoutRegionSolveRequest& ParentRequest,
			const FCommittedRecursiveScheduleState& CandidateSchedule,
			FLayoutRegionSolveScheduleRequest& OutScheduleRequest)
		{
			OutScheduleRequest.RegionRequests.Reset();
			OutScheduleRequest.Dependencies.Reset();
			OutScheduleRequest.DirectChildCommitments.Reset();
			OutScheduleRequest.PlannedPartitionSeams =
				CandidateSchedule.PlannedPartitionSeams;
			OutScheduleRequest.bPlannedPartitionSeamsAreAuthoritative = true;

			FLayoutRegionSolveRequest NegotiatedParentRequest = ParentRequest;
			NegotiatedParentRequest.NegotiatedChildResponsibilityContracts.Reset();
			for (const FAutomaticChildPlacement& ChildPlacement :
				CandidateSchedule.Placements)
			{
				NegotiatedParentRequest.NegotiatedChildResponsibilityContracts.Add(
					ChildPlacement.NegotiatedResponsibilityContract);
			}

			OutScheduleRequest.RegionRequests.Add(NegotiatedParentRequest);
			for (const FAutomaticChildPlacement& ChildPlacement :
				CandidateSchedule.Placements)
			{
				OutScheduleRequest.RegionRequests.Add(ChildPlacement.ChildRequest);
				OutScheduleRequest.Dependencies.Add(
					{ParentRequest.RegionDebugPath, ChildPlacement.ChildRegionDebugPath});
				if (!ChildPlacement.DirectChildCommitment.EndpointCommitments.IsEmpty())
				{
					OutScheduleRequest.DirectChildCommitments.Add(
						ChildPlacement.DirectChildCommitment);
				}
			}
		}

		void RecordCompleteValidationParentProofFailure(
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			const FString& FailureReason)
		{
			FString FailureFirstLine;
			if (!FailureReason.Split(TEXT("\n"), &FailureFirstLine, nullptr))
			{
				FailureFirstLine = FailureReason;
			}

			FailureFirstLine = FailureFirstLine.Left(160).TrimStartAndEnd();
			if (FailureFirstLine.IsEmpty())
			{
				FailureFirstLine = TEXT("<empty>");
			}

			FRecursiveSchedulerFullProofFailureStats& Stats =
				InOutPerfMetrics.CompleteValidationParentProofFailureStatsByFirstLine
					.FindOrAdd(FailureFirstLine);
			++Stats.Count;
			if (Stats.SampleFirstLine.IsEmpty())
			{
				Stats.SampleFirstLine = FailureFirstLine;
			}
		}

		FString ExtractDeferredValidationFailureFirstLine(const FString& FailureReason)
		{
			FString FailureFirstLine;
			if (!FailureReason.Split(TEXT("\n"), &FailureFirstLine, nullptr))
			{
				FailureFirstLine = FailureReason;
			}

			FailureFirstLine = FailureFirstLine.Left(160).TrimStartAndEnd();
			if (FailureFirstLine.IsEmpty())
			{
				FailureFirstLine = TEXT("<empty>");
			}
			return FailureFirstLine;
		}

		void ResetDeferredValidationOutcome(
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics)
		{
			InOutPerfMetrics.LastCompleteValidationOutcome =
				FRecursiveSchedulerDeferredValidationOutcome{};
		}

		void RecordDeferredValidationOutcome(
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			const FLayoutId ResultKind,
			const bool bSucceeded,
			const bool bMemoHit,
			const double ParentSolveSeconds,
			const double ChildSolveSeconds,
			const FString& FailureReason)
		{
			FRecursiveSchedulerDeferredValidationOutcome& Outcome =
				InOutPerfMetrics.LastCompleteValidationOutcome;
			Outcome.bSucceeded = bSucceeded;
			Outcome.bMemoHit = bMemoHit;
			Outcome.ResultKind = ResultKind;
			Outcome.ParentSolveSeconds = ParentSolveSeconds;
			Outcome.ChildSolveSeconds = ChildSolveSeconds;
			Outcome.FailureFirstLine =
				FailureReason.IsEmpty()
					? FString()
					: ExtractDeferredValidationFailureFirstLine(FailureReason);
		}

		FString ToDebugString(
			const ELayoutRouteDomainFailureKind FailureKind)
		{
			const UEnum* FailureEnum = StaticEnum<ELayoutRouteDomainFailureKind>();
			return FailureEnum != nullptr
				? FailureEnum->GetNameStringByValue(static_cast<int64>(FailureKind))
				: FString(TEXT("Unknown"));
		}

		void RecordPreparedPlanRouteDemandSummary(
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			const LayoutProfileSolverInternal::FPreparedPlanRouteDemandSummary& Summary)
		{
			FRecursiveSchedulerDeferredValidationOutcome& Outcome =
				InOutPerfMetrics.LastCompleteValidationOutcome;
			Outcome.PreparedPlanRouteConstraintCount =
				Summary.RouteConstraintCount;
			Outcome.PreparedPlanMainRouteConstraintCount =
				Summary.MainRouteConstraintCount;
			Outcome.PreparedPlanBoundaryRouteConstraintCount =
				Summary.BoundaryRouteConstraintCount;
			Outcome.PreparedPlanInteriorRouteConstraintCount =
				Summary.InteriorRouteConstraintCount;
			Outcome.PreparedPlanBoundaryMultiFaceConstraintCount =
				Summary.BoundaryMultiFaceConstraintCount;
			Outcome.PreparedPlanRouteFaceRequirementCount =
				Summary.RouteFaceRequirementCount;
			Outcome.PreparedPlanRequiredRouteReservationCount =
				Summary.RequiredRouteReservationCount;
			Outcome.PreparedPlanReachabilityBranchReservationCount =
				Summary.ReachabilityBranchReservationCount;
			Outcome.PreparedPlanLiveDomainConstrainedCellCount =
				Summary.LiveDomainConstrainedCellCount;
			Outcome.PreparedPlanLiveDomainBoundaryConstrainedCellCount =
				Summary.LiveDomainBoundaryConstrainedCellCount;
			Outcome.PreparedPlanLiveDomainMultiFaceBoundaryConstrainedCellCount =
				Summary.LiveDomainMultiFaceBoundaryConstrainedCellCount;
			Outcome.PreparedPlanLiveDomainEligibleCandidateCount =
				Summary.LiveDomainEligibleCandidateCount;
			Outcome.PreparedPlanLiveDomainEliminatedCandidateCount =
				Summary.LiveDomainEliminatedCandidateCount;
			Outcome.PreparedPlanLiveDomainTightestRemainingSize =
				Summary.LiveDomainTightestRemainingSize;
			Outcome.PreparedPlanLiveDomainSingleRemainingCandidateCellCount =
				Summary.LiveDomainSingleRemainingCandidateCellCount;
			Outcome.PreparedPlanLiveDomainAtMostFourRemainingCandidateCellCount =
				Summary.LiveDomainAtMostFourRemainingCandidateCellCount;
			Outcome.bPreparedPlanLiveDomainFailedConstraint =
				Summary.bLiveDomainFailedConstraint;
			Outcome.PreparedPlanLiveDomainFailedConstraintCell =
				Summary.LiveDomainFailedConstraintCell;
			Outcome.PreparedPlanLiveDomainFailedConstraintEligibleCandidateCount =
				Summary.LiveDomainFailedConstraintEligibleCandidateCount;
			Outcome.PreparedPlanLiveDomainFailedConstraintRequiredFaceCount =
				Summary.LiveDomainFailedConstraintRequiredFaceCount;
			Outcome.PreparedPlanLiveDomainFailedConstraintDominantFailureKind =
				Summary.LiveDomainFailedConstraintDominantFailureKind;
			Outcome.PreparedPlanLiveDomainFailedConstraintDominantFailureCount =
				Summary.LiveDomainFailedConstraintDominantFailureCount;
			Outcome.bPreparedPlanRepairModeRouteDomainAttempted =
				Summary.bRepairModeRouteDomainAttempted;
			Outcome.bPreparedPlanRepairModeRouteDomainSucceeded =
				Summary.bRepairModeRouteDomainSucceeded;
			Outcome.bPreparedPlanRepairModeLiveDomainFailedConstraint =
				Summary.bRepairModeLiveDomainFailedConstraint;
			Outcome.PreparedPlanRepairModeLiveDomainFailedConstraintCell =
				Summary.RepairModeLiveDomainFailedConstraintCell;
			Outcome
				.PreparedPlanRepairModeLiveDomainFailedConstraintEligibleCandidateCount =
				Summary
					.RepairModeLiveDomainFailedConstraintEligibleCandidateCount;
			Outcome
				.PreparedPlanRepairModeLiveDomainFailedConstraintRequiredFaceCount =
				Summary
					.RepairModeLiveDomainFailedConstraintRequiredFaceCount;
			Outcome
				.PreparedPlanRepairModeLiveDomainFailedConstraintDominantFailureKind =
				Summary
					.RepairModeLiveDomainFailedConstraintDominantFailureKind;
			Outcome
				.PreparedPlanRepairModeLiveDomainFailedConstraintDominantFailureCount =
				Summary
					.RepairModeLiveDomainFailedConstraintDominantFailureCount;
			Outcome.PreparedPlanSearchPrefixPlacedCellCount =
				Summary.SearchPrefixPlacedCellCount;
			Outcome.PreparedPlanSearchPrefixForcedPlacementCellCount =
				Summary.SearchPrefixForcedPlacementCellCount;
			Outcome.bPreparedPlanSearchPrefixSolvedAfterForcedPlacements =
				Summary.bSearchPrefixSolvedAfterForcedPlacements;
			Outcome.bPreparedPlanSearchPrefixNoCellSelectable =
				Summary.bSearchPrefixNoCellSelectable;
			Outcome.PreparedPlanSearchPrefixConstrainedCellCount =
				Summary.SearchPrefixConstrainedCellCount;
			Outcome.PreparedPlanSearchPrefixEligibleCandidateCount =
				Summary.SearchPrefixEligibleCandidateCount;
			Outcome.PreparedPlanSearchPrefixTightestRemainingSize =
				Summary.SearchPrefixTightestRemainingSize;
			Outcome
				.PreparedPlanSearchPrefixSingleRemainingCandidateCellCount =
				Summary.SearchPrefixSingleRemainingCandidateCellCount;
			Outcome
				.PreparedPlanSearchPrefixAtMostFourRemainingCandidateCellCount =
				Summary.SearchPrefixAtMostFourRemainingCandidateCellCount;
			Outcome.PreparedPlanSearchPrefixSelectedCell =
				Summary.SearchPrefixSelectedCell;
			Outcome.PreparedPlanSearchPrefixSelectedIntent =
				Summary.SearchPrefixSelectedIntent;
			Outcome.PreparedPlanSearchPrefixSelectedDomainSize =
				Summary.SearchPrefixSelectedDomainSize;
			Outcome.PreparedPlanSearchPrefixDominantFailureKind =
				Summary.SearchPrefixDominantFailureKind;
			Outcome.PreparedPlanSearchPrefixDominantFailureCount =
				Summary.SearchPrefixDominantFailureCount;
			Outcome.bPreparedPlanRepairModeSearchPrefixAttempted =
				Summary.bRepairModeSearchPrefixAttempted;
			Outcome.bPreparedPlanRepairModeSearchPrefixSucceeded =
				Summary.bRepairModeSearchPrefixSucceeded;
			Outcome.PreparedPlanRepairModeSearchPrefixPlacedCellCount =
				Summary.RepairModeSearchPrefixPlacedCellCount;
			Outcome.PreparedPlanRepairModeSearchPrefixForcedPlacementCellCount =
				Summary.RepairModeSearchPrefixForcedPlacementCellCount;
			Outcome.bPreparedPlanRepairModeSearchPrefixSolvedAfterForcedPlacements =
				Summary.bRepairModeSearchPrefixSolvedAfterForcedPlacements;
			Outcome.bPreparedPlanRepairModeSearchPrefixNoCellSelectable =
				Summary.bRepairModeSearchPrefixNoCellSelectable;
			Outcome.PreparedPlanRepairModeSearchPrefixConstrainedCellCount =
				Summary.RepairModeSearchPrefixConstrainedCellCount;
			Outcome.PreparedPlanRepairModeSearchPrefixEligibleCandidateCount =
				Summary.RepairModeSearchPrefixEligibleCandidateCount;
			Outcome.PreparedPlanRepairModeSearchPrefixTightestRemainingSize =
				Summary.RepairModeSearchPrefixTightestRemainingSize;
			Outcome
				.PreparedPlanRepairModeSearchPrefixSingleRemainingCandidateCellCount =
				Summary.RepairModeSearchPrefixSingleRemainingCandidateCellCount;
			Outcome
				.PreparedPlanRepairModeSearchPrefixAtMostFourRemainingCandidateCellCount =
				Summary.RepairModeSearchPrefixAtMostFourRemainingCandidateCellCount;
			Outcome.PreparedPlanRepairModeSearchPrefixSelectedCell =
				Summary.RepairModeSearchPrefixSelectedCell;
			Outcome.PreparedPlanRepairModeSearchPrefixSelectedIntent =
				Summary.RepairModeSearchPrefixSelectedIntent;
			Outcome.PreparedPlanRepairModeSearchPrefixSelectedDomainSize =
				Summary.RepairModeSearchPrefixSelectedDomainSize;
			Outcome.PreparedPlanRepairModeSearchPrefixDominantFailureKind =
				Summary.RepairModeSearchPrefixDominantFailureKind;
			Outcome.PreparedPlanRepairModeSearchPrefixDominantFailureCount =
				Summary.RepairModeSearchPrefixDominantFailureCount;
		}

		/**
		 * Captures the actual live route-domain filter state from the real parent
		 * proof result so prepared-request preflight signals can be compared
		 * against the route-domain burden that survived into full vertical solve.
		 */
		/** Preserves typed parent preparation failure so root policy can choose bounded fallback. */
		void PreserveDeferredParentPreparationFailure(
			const FLayoutSolveResult& ParentSolveResult,
			FRecursiveChildPlacementSchedulerState& InOutSchedulerState)
		{
			if (ParentSolveResult.PreparationFailureKind == ELayoutSolvePreparationFailureKind::None)
			{
				return;
			}
			InOutSchedulerState.PreparationFailureKind = ParentSolveResult.PreparationFailureKind;
			InOutSchedulerState.PreparationCandidateAttemptCount +=
				ParentSolveResult.PropagationStats.CandidateAttemptCount;
		}

		void RecordParentProofRouteDomainDiagnostics(
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			const FLayoutSolveResult& ParentSolveResult)
		{
			const FLayoutRouteDomainFilterDiagnostics& Diagnostics =
				ParentSolveResult.RouteDomainFilterDiagnostics;
			FRecursiveSchedulerDeferredValidationOutcome& Outcome =
				InOutPerfMetrics.LastCompleteValidationOutcome;
			Outcome.ParentProofLiveDomainConstrainedCellCount =
				Diagnostics.ConstrainedCellCount;
			Outcome.ParentProofLiveDomainBoundaryConstrainedCellCount =
				Diagnostics.BoundaryConstrainedCellCount;
			Outcome.ParentProofLiveDomainMultiFaceBoundaryConstrainedCellCount =
				Diagnostics.MultiFaceBoundaryConstrainedCellCount;
			Outcome.ParentProofLiveDomainEligibleCandidateCount =
				Diagnostics.TotalEligibleCandidateCount;
			Outcome.ParentProofLiveDomainEliminatedCandidateCount =
				Diagnostics.EliminatedCandidateCount;
			Outcome.ParentProofLiveDomainTightestRemainingSize =
				Diagnostics.TightestRemainingDomainSize;
			Outcome.ParentProofLiveDomainSingleRemainingCandidateCellCount =
				Diagnostics.SingleRemainingCandidateCellCount;
			Outcome.ParentProofLiveDomainAtMostFourRemainingCandidateCellCount =
				Diagnostics.AtMostFourRemainingCandidateCellCount;
			Outcome.bParentProofLiveDomainFailedConstraint =
				Diagnostics.bFailedConstraint;
			Outcome.ParentProofLiveDomainFailedConstraintCell =
				Diagnostics.FailedConstraintCell;
			Outcome.ParentProofLiveDomainFailedConstraintEligibleCandidateCount =
				Diagnostics.FailedConstraintEligibleCandidateCount;
			Outcome.ParentProofLiveDomainFailedConstraintRequiredFaceCount =
				Diagnostics.FailedConstraintRequiredFaceCount;
			Outcome.ParentProofLiveDomainFailedConstraintDominantFailureKind =
				Diagnostics.FailedConstraintDominantFailureKind;
			Outcome.ParentProofLiveDomainFailedConstraintDominantFailureCount =
				Diagnostics.FailedConstraintDominantFailureCount;
		}

		bool IsInteriorLikeIntentForDeferredSupportSummary(
			const ELayoutCellIntent Intent)
		{
			return Intent == ELayoutCellIntent::Interior
				|| Intent == ELayoutCellIntent::Core
				|| Intent == ELayoutCellIntent::Connector;
		}

		struct FDeferredValidationChildBundleSupportSummary
		{
			int32 ChildBundleSupportedCellCount = 0;
			int32 ChildBundleSupportedUpperCellCount = 0;
			int32 ChildBundleSupportedUpperInteriorLikeCellCount = 0;
			int32 ChildBundleSupportedTopLevelCellCount = 0;
			int32 ChildBundleSupportedTopLevelInteriorLikeCellCount = 0;
		};

		FDeferredValidationChildBundleSupportSummary BuildDeferredValidationChildBundleSupportSummary(
			const FLayoutRegionSolveRequest& ParentRequest,
			const FCommittedRecursiveScheduleState& CandidateSchedule)
		{
			FDeferredValidationChildBundleSupportSummary Summary;

			if (ParentRequest.PlannedCells.IsEmpty())
			{
				return Summary;
			}

			TSet<FIntVector> ReservedChildCells;
			for (const FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
			{
				for (const FIntVector& ReservedCell : Placement.ParentPlanReservedCells)
				{
					ReservedChildCells.Add(ReservedCell);
				}
			}
			if (ReservedChildCells.IsEmpty())
			{
				return Summary;
			}

			int32 MaxParentLevel = 0;
			for (const FLayoutPlannedCell& PlannedCell : ParentRequest.PlannedCells)
			{
				MaxParentLevel = FMath::Max(MaxParentLevel, PlannedCell.Cell.Z);
			}

			for (const FLayoutPlannedCell& PlannedCell : ParentRequest.PlannedCells)
			{
				if (PlannedCell.Cell.Z <= 0)
				{
					continue;
				}

				const FIntVector SupportCell = PlannedCell.Cell + FIntVector(0, 0, -1);
				if (!ReservedChildCells.Contains(SupportCell))
				{
					continue;
				}

				++Summary.ChildBundleSupportedCellCount;
				++Summary.ChildBundleSupportedUpperCellCount;
				if (IsInteriorLikeIntentForDeferredSupportSummary(PlannedCell.Intent))
				{
					++Summary.ChildBundleSupportedUpperInteriorLikeCellCount;
				}

				if (PlannedCell.Cell.Z == MaxParentLevel)
				{
					++Summary.ChildBundleSupportedTopLevelCellCount;
					if (IsInteriorLikeIntentForDeferredSupportSummary(PlannedCell.Intent))
					{
						++Summary.ChildBundleSupportedTopLevelInteriorLikeCellCount;
					}
				}
			}

			return Summary;
		}

		void PopulateDeferredValidationChildBundleSupportSummary(
			const FDeferredValidationChildBundleSupportSummary& Summary,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics)
		{
			FRecursiveSchedulerDeferredValidationOutcome& Outcome =
				InOutPerfMetrics.LastCompleteValidationOutcome;
			Outcome.ChildBundleSupportedCellCount =
				Summary.ChildBundleSupportedCellCount;
			Outcome.ChildBundleSupportedUpperCellCount =
				Summary.ChildBundleSupportedUpperCellCount;
			Outcome.ChildBundleSupportedUpperInteriorLikeCellCount =
				Summary.ChildBundleSupportedUpperInteriorLikeCellCount;
			Outcome.ChildBundleSupportedTopLevelCellCount =
				Summary.ChildBundleSupportedTopLevelCellCount;
			Outcome.ChildBundleSupportedTopLevelInteriorLikeCellCount =
				Summary.ChildBundleSupportedTopLevelInteriorLikeCellCount;
		}

		bool ShouldRejectBundleSupportedUpperInteriorDeferredParentProof(
			const FCommittedRecursiveScheduleState& CandidateSchedule,
			const FDeferredValidationChildBundleSupportSummary& Summary)
		{
			if (CandidateSchedule.Placements.Num() != 1)
			{
				return false;
			}

			const FAutomaticChildPlacement& Placement = CandidateSchedule.Placements[0];
			const int32 ParentContactCount = Placement.ParentCommittedEntryCells.Num();
			if (ParentContactCount <= 0
				|| ParentContactCount > 2
				|| Placement.ParentPlanReservedCells.Num() < 18)
			{
				return false;
			}

			if (Summary.ChildBundleSupportedTopLevelInteriorLikeCellCount < 9)
			{
				return false;
			}

			return Summary.ChildBundleSupportedUpperCellCount
					== Summary.ChildBundleSupportedUpperInteriorLikeCellCount
				&& Summary.ChildBundleSupportedTopLevelCellCount
					== Summary.ChildBundleSupportedTopLevelInteriorLikeCellCount;
		}

		FString BuildBundleSupportedUpperInteriorDeferredParentProofFailureReason(
			const FCommittedRecursiveScheduleState& CandidateSchedule,
			const FDeferredValidationChildBundleSupportSummary& Summary)
		{
			const FAutomaticChildPlacement& Placement = CandidateSchedule.Placements[0];
			return FString::Printf(
				TEXT("Deferred parent proof rejected before full solve.\n")
				TEXT("Problem: The surviving parent plan preserves %d upper interior cells, including %d top-level interior cells, only through child-bundle support while using %d committed parent contact(s).\n")
				TEXT("Fix: Use a placement that leaves same-region support beneath the retained upper parent stack, or reduce the preserved upper interior footprint above the child-supported bundle."),
				Summary.ChildBundleSupportedUpperInteriorLikeCellCount,
				Summary.ChildBundleSupportedTopLevelInteriorLikeCellCount,
				Placement.ParentCommittedEntryCells.Num());
		}

		enum class EDeferredParentValidationPreflightFailureKind : uint8
		{
			None,
			BuildParentRequest,
			ParentBundleSupportReject
		};

		/**
		 * Immutable parent-only preflight carried across deferred validation
		 * stages once request setup settles.
		 *
		 * Keep this request-local seam explicit so later proof stages can
		 * consume stable inputs without rebuilding parent-only state, and so the
		 * same carrier can become a clean parallel validation work unit later.
		 */
		struct FDeferredParentValidationPreflight
		{
			FLayoutRegionSolveRequest ParentRequest;
			/** Dispatches the complete sparse parent proof only in the solve stage; preparation retains host alternatives. */
			bool bUseSparseParentSolve = false;
			FCommittedVerticalAccessOwnership EffectiveVerticalAccessOwnership;
			FDeferredValidationChildBundleSupportSummary BundleSupportSummary;
			LayoutProfileSolverInternal::FPreparedPlanRouteDemandSummary
				PreparedPlanSummary;
			bool bHasPreparedPlanSummary = false;
		};

		FLayoutId GetDeferredParentValidationPreflightFailureResultKind(
			const EDeferredParentValidationPreflightFailureKind FailureKind)
		{
			switch (FailureKind)
			{
			case EDeferredParentValidationPreflightFailureKind::BuildParentRequest:
				return TEXT("BuildParentRequestFail");
			case EDeferredParentValidationPreflightFailureKind::ParentBundleSupportReject:
				return TEXT("ParentBundleSupportReject");
			case EDeferredParentValidationPreflightFailureKind::None:
			default:
				return NAME_None;
			}
		}

		bool TryBuildDeferredParentValidationPreflight(
			const FLayoutRegionSolveRequest& RootRequest,
			const TArray<FLayoutPlannedCell>& ParentPlannedCells,
			const TSet<FIntVector>& ParentPlannedCellSet,
			const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
			const FIntPoint& ParentFootprintSize,
			const TSet<FIntVector>& ParentProtectedTraversalCells,
			const TSet<ELayoutFaceDirection>& ParentVerticalAccessIngressDirections,
			FCommittedRecursiveScheduleState& CandidateSchedule,
			const bool bTrackDetailedRecursivePerf,
			FDeferredParentValidationPreflight& OutPreflight,
			EDeferredParentValidationPreflightFailureKind& OutFailureKind,
			FString& OutFailureReason)
		{
			SCOPED_NAMED_EVENT(Layout_Deferred_Preflight, FColor::Orange);
			OutPreflight = FDeferredParentValidationPreflight{};
			OutFailureKind = EDeferredParentValidationPreflightFailureKind::None;
			OutFailureReason.Reset();

			// Rebuild from the settled schedule so speculative prepared geometry cannot bypass exact cut-face restrictions.
			if (!BuildParentValidationRequest(
					RootRequest,
					ParentPlannedCells,
					ParentPlannedCellSet,
					ParentPlannedCellIntents,
					ParentFootprintSize,
					ParentProtectedTraversalCells,
					ParentVerticalAccessIngressDirections,
					CandidateSchedule.Placements,
					CandidateSchedule.PlannedPartitionSeams,
					false,
					CandidateSchedule.VerticalAccessOwnership,
					OutFailureReason,
					OutPreflight.ParentRequest,
					&OutPreflight.EffectiveVerticalAccessOwnership))
			{
				OutFailureKind =
					EDeferredParentValidationPreflightFailureKind::BuildParentRequest;
				return false;
			}
			if (!ApplyOwnedSeamUsageRestrictions(
					RootRequest.RegionDebugPath,
					CandidateSchedule.PlannedPartitionSeams,
					OutPreflight.ParentRequest,
					CandidateSchedule.Placements,
					OutFailureReason))
			{
				OutFailureKind =
					EDeferredParentValidationPreflightFailureKind::BuildParentRequest;
				return false;
			}

			// Independent sparse parents solve against negotiated child boundaries in the
			// parent solve stage, not inside each Entry/host preparation attempt.
			const bool bIndependentSparseParent = CanUseIndependentDeferredProofValidation(CandidateSchedule)
				&& OutPreflight.ParentRequest.ProfileSnapshot.SparsePlacementRules.ContainsByPredicate(
					[](const FLayoutSparsePlacementRuleSolveSnapshot& Rule)
					{
						return Rule.RuleKind == ELayoutSparsePlacementRuleKind::PreserveTerrain
							|| Rule.CandidateSource == ELayoutSparseCandidateSource::PreserveSupportedTerrain;
					});
			OutPreflight.bUseSparseParentSolve = bIndependentSparseParent;
			// Settle alternatives on the rebuilt cut-face contract, not speculative geometry.
			if (!TryFreezeParentAuthorityForPlacements(
					RootRequest, CandidateSchedule.Placements, OutPreflight.ParentRequest,
					OutPreflight.EffectiveVerticalAccessOwnership, OutFailureReason,
					nullptr))
			{
				OutFailureKind = EDeferredParentValidationPreflightFailureKind::BuildParentRequest;
				return false;
			}

			if (bIndependentSparseParent)
			{
				OutPreflight.ParentRequest.bDeferTraversalValidationToSchedule = false;
			}

			if (bTrackDetailedRecursivePerf)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[RecursiveParentFilteredHostGroups] region=%s groups=%s"),
					*RootRequest.RegionDebugPath,
					*FString::JoinBy(OutPreflight.ParentRequest.VerticalAccessHostGroups, TEXT("|"), [](const FLayoutVerticalAccessHostGroup& Group)
					{
						return FString::Printf(
							TEXT("%s:%s"),
							*Group.GroupId.ToString(),
							Group.Options.IsEmpty() ? TEXT("<none>") : *Group.Options[0].LowerCell.ToString());
					}));
			}

			OutPreflight.ParentRequest.ContentSetSnapshot.Entries.RemoveAll(
				[](const FLayoutRegionContentEntrySolveSnapshot& Entry)
				{
					return Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
				});

			OutPreflight.BundleSupportSummary =
				BuildDeferredValidationChildBundleSupportSummary(
					OutPreflight.ParentRequest,
					CandidateSchedule);
			if (ShouldRejectBundleSupportedUpperInteriorDeferredParentProof(
					CandidateSchedule,
					OutPreflight.BundleSupportSummary))
			{
				OutFailureReason =
					BuildBundleSupportedUpperInteriorDeferredParentProofFailureReason(
						CandidateSchedule,
						OutPreflight.BundleSupportSummary);
				OutFailureKind =
					EDeferredParentValidationPreflightFailureKind::
						ParentBundleSupportReject;
				return false;
			}

			if (bTrackDetailedRecursivePerf)
			{
				OutPreflight.PreparedPlanSummary =
					LayoutProfileSolverInternal::BuildPreparedPlanRouteDemandSummaryForRequest(
						OutPreflight.ParentRequest);
				OutPreflight.bHasPreparedPlanSummary = true;
			}

			return true;
		}

		void PopulateDeferredParentValidationPreflightDiagnostics(
			const FDeferredParentValidationPreflight& Preflight,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics)
		{
			if (Preflight.bHasPreparedPlanSummary)
			{
				RecordPreparedPlanRouteDemandSummary(
					InOutPerfMetrics,
					Preflight.PreparedPlanSummary);
			}

			PopulateDeferredValidationChildBundleSupportSummary(
				Preflight.BundleSupportSummary,
				InOutPerfMetrics);
		}

		/**
		 * Immutable result of the parent-only solve-execution stage used by
		 * deferred independent validation.
		 *
		 * Keep the costly parent-only solve as its own deterministic work unit
		 * so later anchor audit, child proof, and merged traversal audit can
		 * consume one settled parent result.
		 */
		struct FDeferredIndependentParentSolveStageResult
		{
			FLayoutRegionSolveResult ParentProofResult;
			double ParentSolveSeconds = 0.0;
		};

		/**
		 * Immutable result of the parent-only route-domain / deterministic-prefix
		 * / explicit post-prefix branch-frontier stage used by deferred
		 * independent validation.
		 *
		 * Keep this carrier request-local so validation can separate frontier
		 * preparation from full parent solve continuation, which is a better
		 * ownership seam for later threading work.
		 */
		struct FDeferredIndependentParentPrefixStageResult
		{
			LayoutProfileSolverInternal::FPreparedRequestBackedLeafSearchBranchContinuation
				PreparedLeafSearchBranchContinuation;
			bool bHasPreparedLeafSearchBranchContinuation = false;
			double ParentPrefixSeconds = 0.0;
		};

		/**
		 * Immutable result of the exact post-prefix candidate apply /
		 * forward-check stage used by deferred independent validation.
		 *
		 * This seam is only used when the prepared branch frontier is
		 * deterministic, so validation can separate exact post-prefix
		 * propagation feasibility from the later recursive parent solve without
		 * introducing ordering heuristics.
		 */
		struct FDeferredIndependentParentBranchApplyStageResult
		{
			LayoutProfileSolverInternal::FPreparedRequestBackedLeafSearchBranchApplyContinuation
				PreparedLeafSearchBranchApplyContinuation;
			bool bHasPreparedLeafSearchBranchApplyContinuation = false;
			double ParentBranchApplySeconds = 0.0;
		};

		/**
		 * Immutable result of the later deterministic-prefix stage that runs
		 * after one exact branch-apply continuation has already succeeded.
		 *
		 * This keeps the post-branch propagation / forced-placement seam explicit
		 * so deferred validation can stop on exact later feasibility failures
		 * before paying for full parent recursion.
		 */
		struct FDeferredIndependentParentPostBranchPrefixStageResult
		{
			LayoutProfileSolverInternal::FPreparedRequestBackedLeafSearchPrefixContinuation
				PreparedLeafSearchPrefixContinuation;
			bool bHasPreparedLeafSearchPrefixContinuation = false;
			double ParentPostBranchPrefixSeconds = 0.0;
		};

		bool TryExecuteDeferredIndependentParentPrefixStage(
			const FDeferredParentValidationPreflight& Preflight,
			const bool bTrackDetailedRecursivePerf,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentParentPrefixStageResult& OutStageResult,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentParentPrefixStageResult{};
			OutFailureReason.Reset();

			if (Preflight.bUseSparseParentSolve || Preflight.ParentRequest.ProfileSnapshot.LevelCount != 1)
			{
				return true;
			}

			const double ParentPrefixStartSeconds = FPlatformTime::Seconds();
			LayoutProfileSolverInternal::TryPrepareRequestBackedLeafRegionThroughSearchBranchStage(
				Preflight.ParentRequest,
				OutStageResult.PreparedLeafSearchBranchContinuation,
				false);
			OutStageResult.ParentPrefixSeconds =
				FPlatformTime::Seconds() - ParentPrefixStartSeconds;
			InOutPerfMetrics.CompleteValidationIndependentParentSolveSeconds +=
				OutStageResult.ParentPrefixSeconds;
			OutStageResult.bHasPreparedLeafSearchBranchContinuation = true;

			if (OutStageResult.PreparedLeafSearchBranchContinuation
					.bReadyForFullSolveContinuation)
			{
				return true;
			}

			if (bTrackDetailedRecursivePerf)
			{
				RecordParentProofRouteDomainDiagnostics(
					InOutPerfMetrics,
					OutStageResult.PreparedLeafSearchBranchContinuation
						.FailureSolveResult);
			}

			++InOutPerfMetrics.CompleteValidationParentProofFailures;
			OutFailureReason =
				OutStageResult.PreparedLeafSearchBranchContinuation
						 .FailureSolveResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Independent deferred parent proof failed for region '%s' before full solve continuation."),
						*Preflight.ParentRequest.RegionDebugPath)
					: OutStageResult.PreparedLeafSearchBranchContinuation
						  .FailureSolveResult.FailureReason;
			if (bTrackDetailedRecursivePerf)
			{
				RecordCompleteValidationParentProofFailure(
					InOutPerfMetrics,
					OutFailureReason);
			}
			return false;
		}

		bool TryExecuteDeferredIndependentParentBranchApplyStage(
			const FDeferredParentValidationPreflight& Preflight,
			FDeferredIndependentParentPrefixStageResult& ParentPrefixStage,
			const bool bTrackDetailedRecursivePerf,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentParentBranchApplyStageResult& OutStageResult,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentParentBranchApplyStageResult{};
			OutFailureReason.Reset();

			if (!ParentPrefixStage.bHasPreparedLeafSearchBranchContinuation)
			{
				return true;
			}

			const LayoutProfileSolverInternal::FPreparedSearchBranchStageCarrier&
				SearchBranchStage =
					ParentPrefixStage.PreparedLeafSearchBranchContinuation
						.SearchBranchStage;
			if (SearchBranchStage.SearchPrefixStage.bSolvedAfterForcedPlacements
				|| SearchBranchStage.WorkItems.Num() != 1)
			{
				return true;
			}

			const double ParentBranchApplyStartSeconds = FPlatformTime::Seconds();
			const bool bPreparedBranchApply =
				LayoutProfileSolverInternal::
					TryPrepareRequestBackedLeafRegionThroughSearchBranchApplyStage(
						Preflight.ParentRequest,
						MoveTemp(
							ParentPrefixStage
								.PreparedLeafSearchBranchContinuation),
						0,
						OutStageResult.PreparedLeafSearchBranchApplyContinuation);
			OutStageResult.ParentBranchApplySeconds =
				FPlatformTime::Seconds() - ParentBranchApplyStartSeconds;
			InOutPerfMetrics.CompleteValidationIndependentParentSolveSeconds +=
				OutStageResult.ParentBranchApplySeconds;

			if (bPreparedBranchApply)
			{
				OutStageResult.bHasPreparedLeafSearchBranchApplyContinuation =
					true;
				return true;
			}

			if (bTrackDetailedRecursivePerf)
			{
				RecordParentProofRouteDomainDiagnostics(
					InOutPerfMetrics,
					OutStageResult.PreparedLeafSearchBranchApplyContinuation
						.FailureSolveResult);
			}

			++InOutPerfMetrics.CompleteValidationParentProofFailures;
			OutFailureReason =
				OutStageResult.PreparedLeafSearchBranchApplyContinuation
						 .FailureSolveResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Independent deferred parent proof failed for region '%s' after deterministic post-prefix branch apply."),
						*Preflight.ParentRequest.RegionDebugPath)
					: OutStageResult.PreparedLeafSearchBranchApplyContinuation
						  .FailureSolveResult.FailureReason;
			if (bTrackDetailedRecursivePerf)
			{
				RecordCompleteValidationParentProofFailure(
					InOutPerfMetrics,
					OutFailureReason);
			}
			return false;
		}

		bool TryExecuteDeferredIndependentParentSolveStage(
			FDeferredParentValidationPreflight& Preflight,
			FDeferredIndependentParentPrefixStageResult& ParentPrefixStage,
			FDeferredIndependentParentBranchApplyStageResult& ParentBranchApplyStage,
			FDeferredIndependentParentPostBranchPrefixStageResult& ParentPostBranchPrefixStage,
			const bool bTrackDetailedRecursivePerf,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentParentSolveStageResult& OutStageResult,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentParentSolveStageResult{};
			OutFailureReason.Reset();

			const double ParentSolveStartSeconds = FPlatformTime::Seconds();
			if (Preflight.bUseSparseParentSolve || !Preflight.ParentRequest.CommittedEndpointAnchors.IsEmpty())
			{
				// Sparse and endpoint proofs require the canonical finalized placement carrier;
				// replaying a partial staged carrier can omit bundle roots needed for coverage.
				FLayoutRegionSolveRequest FinalAuthority;
				OutStageResult.ParentProofResult =
					LayoutProfileSolverInternal::SolveRequestBackedRegion(
						Preflight.ParentRequest,
						true, &FinalAuthority);
				if (OutStageResult.ParentProofResult.SolveResult.bSucceeded)
				{
					Preflight.ParentRequest = MoveTemp(FinalAuthority);
				}
			}
			else if (ParentPostBranchPrefixStage
					.bHasPreparedLeafSearchPrefixContinuation)
			{
				OutStageResult.ParentProofResult =
					LayoutProfileSolverInternal::
						ContinuePreparedRequestBackedLeafRegionAfterSearchPrefixStage(
							Preflight.ParentRequest,
							MoveTemp(
								ParentPostBranchPrefixStage
									.PreparedLeafSearchPrefixContinuation));
			}
			else if (ParentBranchApplyStage
						 .bHasPreparedLeafSearchBranchApplyContinuation)
			{
				OutStageResult.ParentProofResult =
					LayoutProfileSolverInternal::
						ContinuePreparedRequestBackedLeafRegionAfterSearchBranchApplyStage(
							Preflight.ParentRequest,
							MoveTemp(
								ParentBranchApplyStage
									.PreparedLeafSearchBranchApplyContinuation));
			}
			else if (ParentPrefixStage.bHasPreparedLeafSearchBranchContinuation)
			{
				OutStageResult.ParentProofResult =
					LayoutProfileSolverInternal::
						ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
							Preflight.ParentRequest,
							MoveTemp(
								ParentPrefixStage
									.PreparedLeafSearchBranchContinuation));
			}
			else
			{
				FLayoutRegionSolveRequest FinalAuthority;
				OutStageResult.ParentProofResult =
					LayoutProfileSolverInternal::SolveRequestBackedRegion(
						Preflight.ParentRequest,
						true, &FinalAuthority);
				if (OutStageResult.ParentProofResult.SolveResult.bSucceeded)
				{
					// Permanent inherited domains were audited by the producer. Merge
					// must now consume this exact selection, not obsolete VA overlays.
					Preflight.ParentRequest = MoveTemp(FinalAuthority);
				}
			}
			if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
			{
				UE_LOG(LogTemp, Display,
					TEXT("[RecursiveParentProofCarrier] region=%s anchors=%d succeeded=%d placements=%d planned=%d"),
					*Preflight.ParentRequest.RegionDebugPath,
					Preflight.ParentRequest.CommittedEndpointAnchors.Num(),
					OutStageResult.ParentProofResult.SolveResult.bSucceeded ? 1 : 0,
					OutStageResult.ParentProofResult.SolveResult.Placements.Num(),
					OutStageResult.ParentProofResult.SolveResult.PlannedCells.Num());
			}
			OutStageResult.ParentSolveSeconds =
				ParentPrefixStage.ParentPrefixSeconds
				+ ParentBranchApplyStage.ParentBranchApplySeconds
				+ ParentPostBranchPrefixStage.ParentPostBranchPrefixSeconds
				+ (FPlatformTime::Seconds() - ParentSolveStartSeconds);
			InOutPerfMetrics.CompleteValidationIndependentParentSolveSeconds +=
				(OutStageResult.ParentSolveSeconds
				 - ParentPrefixStage.ParentPrefixSeconds
				 - ParentBranchApplyStage.ParentBranchApplySeconds
				 - ParentPostBranchPrefixStage.ParentPostBranchPrefixSeconds);
			if (bTrackDetailedRecursivePerf)
			{
				RecordParentProofRouteDomainDiagnostics(
					InOutPerfMetrics,
					OutStageResult.ParentProofResult.SolveResult);
			}

			// Full solve may choose a different retained host alternative. Publish its
			// actual ownership, not the provisional cells from boundary preparation.
			if (OutStageResult.ParentProofResult.SolveResult.bSucceeded
				&& !RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
					OutStageResult.ParentProofResult.SolveResult.PlannedCells,
					Preflight.EffectiveVerticalAccessOwnership,
					&OutStageResult.ParentProofResult.SolveResult.FailureReason))
			{
				OutStageResult.ParentProofResult.SolveResult.bSucceeded = false;
			}
			if (!OutStageResult.ParentProofResult.SolveResult.bSucceeded)
			{
				++InOutPerfMetrics.CompleteValidationParentProofFailures;
				OutFailureReason =
					OutStageResult.ParentProofResult.SolveResult.FailureReason.IsEmpty()
						? FString::Printf(
							TEXT("Independent deferred parent proof failed for region '%s'."),
							*Preflight.ParentRequest.RegionDebugPath)
						: OutStageResult.ParentProofResult.SolveResult.FailureReason;
				if (bTrackDetailedRecursivePerf)
				{
					RecordCompleteValidationParentProofFailure(
						InOutPerfMetrics,
						OutFailureReason);
				}
				return false;
			}

			// Keep retained request contracts aligned with the proved host selection.
			// Otherwise publication mistakes stale representative cells for a changed
			// child contract and reconstructs authority from the pre-solve host choices.
			for (auto& Contract : Preflight.ParentRequest.NegotiatedChildResponsibilityContracts)
			{
				ApplyCommittedVerticalAccessOwnershipToNegotiatedContract(
					Preflight.EffectiveVerticalAccessOwnership, Contract.ChildRegionDebugPath, Contract);
			}
			return true;
		}

		bool TryExecuteDeferredIndependentParentPostBranchPrefixStage(
			const FDeferredParentValidationPreflight& Preflight,
			FDeferredIndependentParentBranchApplyStageResult& ParentBranchApplyStage,
			const bool bTrackDetailedRecursivePerf,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentParentPostBranchPrefixStageResult& OutStageResult,
			FString& OutFailureReason)
		{
			OutStageResult =
				FDeferredIndependentParentPostBranchPrefixStageResult{};
			OutFailureReason.Reset();

			if (!ParentBranchApplyStage
					 .bHasPreparedLeafSearchBranchApplyContinuation)
			{
				return true;
			}

			const double ParentPostBranchPrefixStartSeconds =
				FPlatformTime::Seconds();
			const bool bPreparedPostBranchPrefix =
				LayoutProfileSolverInternal::
					TryPrepareRequestBackedLeafRegionThroughSearchPrefixStage(
						Preflight.ParentRequest,
						MoveTemp(
							ParentBranchApplyStage
								.PreparedLeafSearchBranchApplyContinuation),
						OutStageResult
							.PreparedLeafSearchPrefixContinuation);
			ParentBranchApplyStage
				.bHasPreparedLeafSearchBranchApplyContinuation = false;
			OutStageResult.ParentPostBranchPrefixSeconds =
				FPlatformTime::Seconds() - ParentPostBranchPrefixStartSeconds;
			InOutPerfMetrics.CompleteValidationIndependentParentSolveSeconds +=
				OutStageResult.ParentPostBranchPrefixSeconds;

			if (bPreparedPostBranchPrefix)
			{
				OutStageResult.bHasPreparedLeafSearchPrefixContinuation = true;
				return true;
			}

			if (bTrackDetailedRecursivePerf)
			{
				RecordParentProofRouteDomainDiagnostics(
					InOutPerfMetrics,
					OutStageResult.PreparedLeafSearchPrefixContinuation
						.FailureSolveResult);
			}

			++InOutPerfMetrics.CompleteValidationParentProofFailures;
			OutFailureReason =
				OutStageResult.PreparedLeafSearchPrefixContinuation
						 .FailureSolveResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Independent deferred parent proof failed for region '%s' during later deterministic post-branch prefix preparation."),
						*Preflight.ParentRequest.RegionDebugPath)
					: OutStageResult.PreparedLeafSearchPrefixContinuation
						  .FailureSolveResult.FailureReason;
			if (bTrackDetailedRecursivePerf)
			{
				RecordCompleteValidationParentProofFailure(
					InOutPerfMetrics,
					OutFailureReason);
			}
			return false;
		}

		/**
		 * Run the committed-endpoint anchor audit after parent-only proof
		 * succeeds.
		 *
		 * Keep this audit separate from solve execution so threading work can
		 * schedule the parent solve as one unit and run the deterministic
		 * anchor coverage check as a later request-local verification stage.
		 */
		bool TryExecuteDeferredIndependentParentAnchorAuditStage(
			const FDeferredParentValidationPreflight& Preflight,
			const FDeferredIndependentParentSolveStageResult& ParentSolveStage,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FString& OutFailureReason)
		{
			OutFailureReason.Reset();

			if (!ValidateCommittedEndpointAnchorCoverageOnPlacements(
					Preflight.ParentRequest.RegionDebugPath,
					Preflight.ParentRequest.RegionCellOffset,
					ParentSolveStage.ParentProofResult.SolveResult.Placements,
					Preflight.ParentRequest.CommittedEndpointAnchors,
					OutFailureReason))
			{
				++InOutPerfMetrics.CompleteValidationParentAnchorFailures;
				OutFailureReason += FString::Printf(
					TEXT(" PlannedEntries=[%s]."),
					*FString::JoinBy(
						Preflight.ParentRequest.PlannedCells.FilterByPredicate(
							[](const FLayoutPlannedCell& Cell){ return Cell.Intent == ELayoutCellIntent::Entry; }),
						TEXT("|"),
						[](const FLayoutPlannedCell& Cell){ return Cell.Cell.ToString(); }));
				return false;
			}

			return true;
		}

		enum class EDeferredIndependentChildProofRequestFailureKind : uint8
		{
			None,
			ChildBoundary
		};

		/**
		 * Immutable result of the child-proof request-preparation stage used by
		 * deferred independent validation.
		 *
		 * Keep request preparation separate from proof execution so the
		 * settled parent-only result can hand off one immutable fan-out batch
		 * of child solve requests for future threading.
		 */
		struct FDeferredIndependentChildProofRequestStageResult
		{
			TArray<FLayoutRegionSolveRequest> ChildProofRequests;
		};

		bool TryPrepareDeferredIndependentChildProofRequestsStage(
			const FDeferredIndependentParentSolveStageResult& ParentSolveStage,
			const FCommittedRecursiveScheduleState& CandidateSchedule,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentChildProofRequestStageResult& OutStageResult,
			EDeferredIndependentChildProofRequestFailureKind& OutFailureKind,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentChildProofRequestStageResult{};
			(void)ParentSolveStage;
			OutFailureKind =
				EDeferredIndependentChildProofRequestFailureKind::None;
			OutFailureReason.Reset();

			OutStageResult.ChildProofRequests.Reserve(
				CandidateSchedule.Placements.Num());
			for (const FAutomaticChildPlacement& Placement :
				CandidateSchedule.Placements)
			{
				FLayoutRegionSolveRequest& ChildProofRequest =
					OutStageResult.ChildProofRequests.AddDefaulted_GetRef();
				BuildIndependentChildProofValidationRequest(
					Placement,
					ChildProofRequest);
			}

			return true;
		}

		enum class EDeferredIndependentChildProofSolveFailureKind : uint8
		{
			None,
			ChildProof
		};

		/**
		 * Immutable result of the child-proof solve stage used by deferred
		 * independent validation.
		 *
		 * Keep execution separate from request preparation so future threading
		 * can fan out child proof over an already-settled request batch.
		 */
		struct FDeferredIndependentChildProofSolveStageResult
		{
			TArray<FLayoutRegionSolveResult> ChildProofResults;
			double ChildSolveSeconds = 0.0;
		};

		bool TryExecuteDeferredIndependentChildProofSolveStage(
			const FDeferredIndependentChildProofRequestStageResult& RequestStage,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentChildProofSolveStageResult& OutStageResult,
			EDeferredIndependentChildProofSolveFailureKind& OutFailureKind,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentChildProofSolveStageResult{};
			OutFailureKind = EDeferredIndependentChildProofSolveFailureKind::None;
			OutFailureReason.Reset();

			OutStageResult.ChildProofResults.Reserve(
				RequestStage.ChildProofRequests.Num());
			for (const FLayoutRegionSolveRequest& ChildProofRequest :
				RequestStage.ChildProofRequests)
			{
				FLayoutRegionSolveResult& ChildProofResult =
					OutStageResult.ChildProofResults.AddDefaulted_GetRef();
				const double ChildSolveStartSeconds = FPlatformTime::Seconds();
				if (!BuildIndependentChildProofResult(
						ChildProofRequest,
						ChildProofResult,
						OutFailureReason))
				{
					const double ChildSolveSeconds =
						FPlatformTime::Seconds() - ChildSolveStartSeconds;
					InOutPerfMetrics.CompleteValidationIndependentChildSolveSeconds +=
						ChildSolveSeconds;
					OutStageResult.ChildSolveSeconds += ChildSolveSeconds;
					++InOutPerfMetrics.CompleteValidationChildProofFailures;
					OutFailureKind =
						EDeferredIndependentChildProofSolveFailureKind::ChildProof;
					return false;
				}

				const double ChildSolveSeconds =
					FPlatformTime::Seconds() - ChildSolveStartSeconds;
				InOutPerfMetrics.CompleteValidationIndependentChildSolveSeconds +=
					ChildSolveSeconds;
				OutStageResult.ChildSolveSeconds += ChildSolveSeconds;
			}

			return true;
		}

		enum class EDeferredIndependentScheduleBuildFailureKind : uint8
		{
			None,
			ScheduleBuild
		};

		/**
		 * Immutable result of the merged schedule build stage used by deferred
		 * independent validation.
		 *
		 * Keep schedule materialization separate from merged traversal audit so
		 * the final reduce step is explicit and request-local before the last
		 * validation pass runs.
		 */
		struct FDeferredIndependentScheduleBuildStageResult
		{
			FLayoutRegionSolveScheduleResult ScheduleResult;
		};

		bool TryExecuteDeferredIndependentScheduleBuildStage(
			const FDeferredParentValidationPreflight& ValidationPreflight,
			const FCommittedRecursiveScheduleState& CandidateSchedule,
			FLayoutRegionSolveResult&& ParentProofResult,
			const TArray<FLayoutRegionSolveRequest>& ChildProofRequests,
			TArray<FLayoutRegionSolveResult>&& ChildProofResults,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentScheduleBuildStageResult& OutStageResult,
			EDeferredIndependentScheduleBuildFailureKind& OutFailureKind,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentScheduleBuildStageResult{};
			OutFailureKind = EDeferredIndependentScheduleBuildFailureKind::None;
			OutFailureReason.Reset();

			if (!BuildIndependentDeferredProofScheduleResult(
					ValidationPreflight.ParentRequest,
					ChildProofRequests,
					CandidateSchedule,
					ValidationPreflight.EffectiveVerticalAccessOwnership,
					MoveTemp(ParentProofResult),
					MoveTemp(ChildProofResults),
					OutStageResult.ScheduleResult,
					OutFailureReason,
					true))
			{
				++InOutPerfMetrics.CompleteValidationIndependentScheduleBuildFailures;
				OutFailureKind =
					EDeferredIndependentScheduleBuildFailureKind::ScheduleBuild;
				return false;
			}

			return true;
		}

		enum class EDeferredIndependentTraversalAuditFailureKind : uint8
		{
			None,
			MergedTraversal
		};

		/**
		 * Immutable result of the final merged traversal audit stage used by
		 * deferred independent validation.
		 *
		 * Keep the last audit explicit so the merged schedule can be
		 * materialized once and then validated as a final isolated pass.
		 */
		struct FDeferredIndependentTraversalAuditStageResult
		{
			double AuditSeconds = 0.0;
		};

		bool TryExecuteDeferredIndependentTraversalAuditStage(
			const FDeferredParentValidationPreflight& ValidationPreflight,
			const FCommittedRecursiveScheduleState& CandidateSchedule,
			const FDeferredIndependentScheduleBuildStageResult& ScheduleBuildStage,
			FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
			FDeferredIndependentTraversalAuditStageResult& OutStageResult,
			EDeferredIndependentTraversalAuditFailureKind& OutFailureKind,
			FString& OutFailureReason)
		{
			OutStageResult = FDeferredIndependentTraversalAuditStageResult{};
			OutFailureKind =
				EDeferredIndependentTraversalAuditFailureKind::None;
			OutFailureReason.Reset();

			const double ValidationAuditStartSeconds = FPlatformTime::Seconds();
			if (!ValidateMergedScheduleTraversalConnectivity(
					ScheduleBuildStage.ScheduleResult,
					ValidationPreflight.ParentRequest.RegionDebugPath,
					ValidationPreflight.EffectiveVerticalAccessOwnership,
					&CandidateSchedule,
					OutFailureReason))
			{
				++InOutPerfMetrics.CompleteValidationMergedTraversalFailures;
				OutFailureKind =
					EDeferredIndependentTraversalAuditFailureKind::MergedTraversal;
				return false;
			}

			OutStageResult.AuditSeconds =
				FPlatformTime::Seconds() - ValidationAuditStartSeconds;
			InOutPerfMetrics.CompleteValidationAuditSeconds +=
				OutStageResult.AuditSeconds;
			return true;
		}

			FString BuildDeferredValidationFailureDetailForLog(
				const FString& FailureReason)
			{
				if (FailureReason.IsEmpty())
				{
				return FString();
			}

			FString Detail = FailureReason;
			FString ScheduleDetail;
			if (Detail.Split(
					TEXT("\nCommitted placement schedule:"),
					&Detail,
					&ScheduleDetail))
			{
				Detail = Detail.TrimEnd();
			}

			return Detail.Left(1200);
		}
		bool BuildCommittedPartitionSeams(
			const FString& ParentRegionDebugPath,
			const TArray<FAutomaticChildPlacement>& CandidatePlacements,
			TArray<FLayoutPartitionSeamRecord>& OutPlannedPartitionSeams,
		FString* OutFailureReason);

	FString BuildCompletePlacementValidationKey(
		const FIntPoint& ParentFootprintSize,
		const TArray<FAutomaticChildPlacement>& Placements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FLayoutRegionSolveRequest* PreparedParentProofRequest)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Recursive_MemoKeyBuild, STAT_PorismLayout_MemoKeyBuild);
		TMap<FString, FString> ChildMemoIdentityByRegionPath;
		BuildChildRegionMemoIdentityMap(Placements, ChildMemoIdentityByRegionPath);

		TArray<FString> PlacementParts;
		PlacementParts.Reserve(Placements.Num());
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			TArray<FString> PlannedCellParts;
			PlannedCellParts.Reserve(Placement.ChildRequest.PlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : Placement.ChildRequest.PlannedCells)
			{
				PlannedCellParts.Add(FString::Printf(
					TEXT("%s:%d"),
					*PlannedCell.Cell.ToString(),
					static_cast<int32>(PlannedCell.Intent)));
			}
			PlannedCellParts.Sort();

			TArray<FString> ContactParts;
			ContactParts.Reserve(Placement.ParentCommittedEntryCells.Num());
			for (const FIntVector& ContactCell : Placement.ParentCommittedEntryCells)
			{
				ContactParts.Add(ContactCell.ToString());
			}
			ContactParts.Sort();

			TArray<FString> TraversalAnchorParts;
			TraversalAnchorParts.Reserve(Placement.ParentCommittedTraversalAnchors.Num());
			for (const FLayoutCommittedTraversalAnchor& Anchor :
				Placement.ParentCommittedTraversalAnchors)
			{
				TraversalAnchorParts.Add(FString::Printf(
					TEXT("%s:%s"),
					*Anchor.Cell.ToString(),
					Anchor.TraversalChannel.IsValid()
						? *Anchor.TraversalChannel.ToString()
						: TEXT("<none>")));
			}
			TraversalAnchorParts.Sort();

			TArray<FString> ReservedCellParts;
			ReservedCellParts.Reserve(Placement.ParentPlanReservedCells.Num());
			for (const FIntVector& ReservedCell : Placement.ParentPlanReservedCells)
			{
				ReservedCellParts.Add(ReservedCell.ToString());
			}
			ReservedCellParts.Sort();

			TArray<FString> SeamParts;
			SeamParts.Reserve(Placement.SharedParentChildFaces.Num());
			for (const FSharedParentChildFace& SharedFace :
				Placement.SharedParentChildFaces)
			{
				SeamParts.Add(BuildSharedParentChildFaceIdentityPart(SharedFace));
			}
			SeamParts.Sort();

			TArray<FString> CommitmentParts;
			CommitmentParts.Reserve(
				Placement.DirectChildCommitment.EndpointCommitments.Num());
			for (const FLayoutCommittedEndpointAnchor& Commitment :
				Placement.DirectChildCommitment.EndpointCommitments)
			{
				CommitmentParts.Add(FString::Printf(
					TEXT("%s:%s:%d:%s:%s:%s:%d"),
					Commitment.CommitmentId == NAME_None
						? TEXT("<none>")
						: *Commitment.CommitmentId.ToString(),
					*Commitment.LocalCell.ToString(),
					static_cast<int32>(Commitment.FaceDirection),
					Commitment.ConnectionTag.IsValid()
						? *Commitment.ConnectionTag.ToString()
						: TEXT("<none>"),
					*TagsToStableKey(Commitment.AllowedConnectionTags),
					*TagsToStableKey(Commitment.TraversalChannels),
					Commitment.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
			}
			CommitmentParts.Sort();

			PlacementParts.Add(FString::Printf(
				TEXT("%s@%s|Footprint=%s|Planned=%s|Contacts=%s|Traversal=%s|Bridge=%d|Reserved=%s|Seams=%s|Commitments=%s"),
				Placement.EntryId == NAME_None ? TEXT("<none>") : *Placement.EntryId.ToString(),
				*Placement.RegionCellOffset.ToString(),
				*Placement.ChildRequest.FootprintSize.ToString(),
				PlannedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedCellParts, TEXT(",")),
				ContactParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ContactParts, TEXT(",")),
				TraversalAnchorParts.IsEmpty() ? TEXT("<none>") : *FString::Join(TraversalAnchorParts, TEXT(",")),
				Placement.bAllowsChildTraversalBridgeForCommittedContacts ? 1 : 0,
				ReservedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ReservedCellParts, TEXT(",")),
				SeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(SeamParts, TEXT(",")),
				CommitmentParts.IsEmpty() ? TEXT("<none>") : *FString::Join(CommitmentParts, TEXT(","))));
		}
		PlacementParts.Sort();

		TArray<FString> PlannedSeamParts;
		PlannedSeamParts.Reserve(PlannedPartitionSeams.Num());
		for (const FLayoutPartitionSeamRecord& SeamRecord : PlannedPartitionSeams)
		{
			PlannedSeamParts.Add(FString::Printf(
				TEXT("%s|%s|%s|%s|%s|%s"),
				*CanonicalizeRegionPathForMemo(
					SeamRecord.OwnerRegionDebugPath,
					ChildMemoIdentityByRegionPath),
				*CanonicalizeRegionPathForMemo(
					SeamRecord.PassiveRegionDebugPath,
					ChildMemoIdentityByRegionPath),
				SeamRecord.InterfaceFamily.IsValid()
					? *SeamRecord.InterfaceFamily.ToString()
					: TEXT("<none>"),
				*SeamRecord.OwnerStartCell.ToString(),
				*SeamRecord.PassiveStartCell.ToString(),
				*SeamRecord.OwnerEndCell.ToString()));
		}
		PlannedSeamParts.Sort();

		FString PreparedParentProofPart = TEXT("<none>");
		if (PreparedParentProofRequest != nullptr)
		{
			TArray<FString> PlannedCellParts;
			PlannedCellParts.Reserve(
				PreparedParentProofRequest->PlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell :
				PreparedParentProofRequest->PlannedCells)
			{
				PlannedCellParts.Add(FString::Printf(
					TEXT("%s:%d"),
					*PlannedCell.Cell.ToString(),
					static_cast<int32>(PlannedCell.Intent)));
			}
			PlannedCellParts.Sort();

			TArray<FString> BoundaryPointParts;
			BoundaryPointParts.Reserve(
				PreparedParentProofRequest->IncomingBoundaryPoints.Num());
			for (const FLayoutSolveBoundaryPoint& BoundaryPoint :
				PreparedParentProofRequest->IncomingBoundaryPoints)
			{
				BoundaryPointParts.Add(FString::Printf(
					TEXT("%s:%d:%s:%s:%s:%d:%d:%d:%s:%d:%s"),
					*BoundaryPoint.LocalCell.ToString(),
					static_cast<int32>(BoundaryPoint.FaceDirection),
					BoundaryPoint.ConnectionTag.IsValid()
						? *BoundaryPoint.ConnectionTag.ToString()
						: TEXT("<none>"),
					*TagsToStableKey(BoundaryPoint.AllowedConnectionTags),
					*TagsToStableKey(BoundaryPoint.ConnectedTraversalChannels),
					BoundaryPoint.bRepresentsFilledNeighbor ? 1 : 0,
					BoundaryPoint.bRequiresBoundaryFacing ? 1 : 0,
					BoundaryPoint.bRequireMatchingYawWithFilledNeighbor ? 1 : 0,
					*BoundaryPoint.SourceCell.ToString(),
					BoundaryPoint.SourceYawRotationSteps,
					BoundaryPoint.CommitmentId == NAME_None
						? TEXT("<none>")
						: *BoundaryPoint.CommitmentId.ToString()));
			}
			BoundaryPointParts.Sort();

			TArray<FString> AnchorParts;
			AnchorParts.Reserve(
				PreparedParentProofRequest->CommittedEndpointAnchors.Num());
			for (const FLayoutCommittedEndpointAnchor& Anchor :
				PreparedParentProofRequest->CommittedEndpointAnchors)
			{
				AnchorParts.Add(FString::Printf(
					TEXT("%s:%s:%d:%d:%s:%s:%s:%d"),
					Anchor.CommitmentId == NAME_None
						? TEXT("<none>")
						: *Anchor.CommitmentId.ToString(),
					*Anchor.LocalCell.ToString(),
					static_cast<int32>(Anchor.FaceDirection),
					Anchor.RequiredWorldCenterBlockZ,
					Anchor.ConnectionTag.IsValid()
						? *Anchor.ConnectionTag.ToString()
						: TEXT("<none>"),
					*TagsToStableKey(Anchor.AllowedConnectionTags),
					*TagsToStableKey(Anchor.TraversalChannels),
					Anchor.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
			}
			AnchorParts.Sort();

			TArray<FString> TraversalAnchorParts;
			TraversalAnchorParts.Reserve(
				PreparedParentProofRequest->CommittedTraversalAnchors.Num());
			for (const FLayoutCommittedTraversalAnchor& Anchor :
				PreparedParentProofRequest->CommittedTraversalAnchors)
			{
				TraversalAnchorParts.Add(FString::Printf(
					TEXT("%s:%s"),
					*Anchor.Cell.ToString(),
					Anchor.TraversalChannel.IsValid()
						? *Anchor.TraversalChannel.ToString()
						: TEXT("<none>")));
			}
			TraversalAnchorParts.Sort();

			PreparedParentProofPart = FString::Printf(
				TEXT("Seed=%d|Footprint=%s|Planned=%s|Incoming=%s|Anchors=%s|Traversal=%s"),
				PreparedParentProofRequest->Seed,
				*PreparedParentProofRequest->FootprintSize.ToString(),
				PlannedCellParts.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(PlannedCellParts, TEXT(",")),
				BoundaryPointParts.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(BoundaryPointParts, TEXT(",")),
				AnchorParts.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(AnchorParts, TEXT(",")),
				TraversalAnchorParts.IsEmpty()
					? TEXT("<none>")
					: *FString::Join(TraversalAnchorParts, TEXT(",")));
		}

		return FString::Printf(
			TEXT("ParentFootprint=%s|Placements=%s|CommittedSeams=%s|VerticalOwner=%s|PreparedParent=%s"),
			*ParentFootprintSize.ToString(),
			PlacementParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlacementParts, TEXT(";")),
			PlannedSeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedSeamParts, TEXT(";")),
			*BuildCommittedVerticalAccessOwnershipMemoKey(
				VerticalAccessOwnership,
				ChildMemoIdentityByRegionPath),
			*PreparedParentProofPart);
	}

	FString BuildCompletePlacementValidationCoarseKey(
		const FIntPoint& ParentFootprintSize,
		const TArray<FAutomaticChildPlacement>& Placements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FLayoutRegionSolveRequest* PreparedParentProofRequest)
	{
		TMap<FString, FString> ChildMemoIdentityByRegionPath;
		BuildChildRegionSemanticMemoIdentityMap(
			Placements,
			ChildMemoIdentityByRegionPath);

		TArray<FString> PlacementParts;
		PlacementParts.Reserve(Placements.Num());
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			TArray<FString> PlannedCellParts;
			PlannedCellParts.Reserve(Placement.ChildRequest.PlannedCells.Num());
			for (const FLayoutPlannedCell& PlannedCell : Placement.ChildRequest.PlannedCells)
			{
				PlannedCellParts.Add(FString::Printf(
					TEXT("%s:%d"),
					*PlannedCell.Cell.ToString(),
					static_cast<int32>(PlannedCell.Intent)));
			}
			PlannedCellParts.Sort();

			TArray<FString> ContactParts;
			ContactParts.Reserve(Placement.ParentCommittedEntryCells.Num());
			for (const FIntVector& ContactCell : Placement.ParentCommittedEntryCells)
			{
				ContactParts.Add(ContactCell.ToString());
			}
			ContactParts.Sort();

			TArray<FString> TraversalAnchorParts;
			TraversalAnchorParts.Reserve(Placement.ParentCommittedTraversalAnchors.Num());
			for (const FLayoutCommittedTraversalAnchor& Anchor :
				Placement.ParentCommittedTraversalAnchors)
			{
				TraversalAnchorParts.Add(FString::Printf(
					TEXT("%s:%s"),
					*Anchor.Cell.ToString(),
					Anchor.TraversalChannel.IsValid()
						? *Anchor.TraversalChannel.ToString()
						: TEXT("<none>")));
			}
			TraversalAnchorParts.Sort();

			TArray<FString> ReservedCellParts;
			ReservedCellParts.Reserve(Placement.ParentPlanReservedCells.Num());
			for (const FIntVector& ReservedCell : Placement.ParentPlanReservedCells)
			{
				ReservedCellParts.Add(ReservedCell.ToString());
			}
			ReservedCellParts.Sort();

			TArray<FString> SeamParts;
			SeamParts.Reserve(Placement.SharedParentChildFaces.Num());
			for (const FSharedParentChildFace& SharedFace :
				Placement.SharedParentChildFaces)
			{
				SeamParts.Add(BuildSharedParentChildFaceIdentityPart(SharedFace));
			}
			SeamParts.Sort();

			TArray<FString> CommitmentParts;
			CommitmentParts.Reserve(
				Placement.DirectChildCommitment.EndpointCommitments.Num());
			for (const FLayoutCommittedEndpointAnchor& Commitment :
				Placement.DirectChildCommitment.EndpointCommitments)
			{
				CommitmentParts.Add(FString::Printf(
					TEXT("%s:%d:%s:%s:%s:%d"),
					*Commitment.LocalCell.ToString(),
					static_cast<int32>(Commitment.FaceDirection),
					Commitment.ConnectionTag.IsValid()
						? *Commitment.ConnectionTag.ToString()
						: TEXT("<none>"),
					*TagsToStableKey(Commitment.AllowedConnectionTags),
					*TagsToStableKey(Commitment.TraversalChannels),
					Commitment.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
			}
			CommitmentParts.Sort();

			PlacementParts.Add(FString::Printf(
				TEXT("%s|Footprint=%s|Planned=%s|Contacts=%s|Traversal=%s|Bridge=%d|Reserved=%s|Seams=%s|Commitments=%s"),
				*Placement.RegionCellOffset.ToString(),
				*Placement.ChildRequest.FootprintSize.ToString(),
				PlannedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedCellParts, TEXT(",")),
				ContactParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ContactParts, TEXT(",")),
				TraversalAnchorParts.IsEmpty() ? TEXT("<none>") : *FString::Join(TraversalAnchorParts, TEXT(",")),
				Placement.bAllowsChildTraversalBridgeForCommittedContacts ? 1 : 0,
				ReservedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ReservedCellParts, TEXT(",")),
				SeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(SeamParts, TEXT(",")),
				CommitmentParts.IsEmpty() ? TEXT("<none>") : *FString::Join(CommitmentParts, TEXT(","))));
		}
		PlacementParts.Sort();

		TArray<FString> PlannedSeamParts;
		PlannedSeamParts.Reserve(PlannedPartitionSeams.Num());
		for (const FLayoutPartitionSeamRecord& SeamRecord : PlannedPartitionSeams)
		{
			PlannedSeamParts.Add(FString::Printf(
				TEXT("%s|%s|%s|%s|%s|%s"),
				*CanonicalizeRegionPathForMemo(
					SeamRecord.OwnerRegionDebugPath,
					ChildMemoIdentityByRegionPath),
				*CanonicalizeRegionPathForMemo(
					SeamRecord.PassiveRegionDebugPath,
					ChildMemoIdentityByRegionPath),
				SeamRecord.InterfaceFamily.IsValid()
					? *SeamRecord.InterfaceFamily.ToString()
					: TEXT("<none>"),
				*SeamRecord.OwnerStartCell.ToString(),
				*SeamRecord.PassiveStartCell.ToString(),
				*SeamRecord.OwnerEndCell.ToString()));
		}
		PlannedSeamParts.Sort();

		FString PreparedParentProofPart = TEXT("<none>");
		if (PreparedParentProofRequest != nullptr)
		{
			PreparedParentProofPart = FString::Printf(
				TEXT("Seed=%d|Footprint=%s|Planned=%d|Incoming=%d|Anchors=%d|Traversal=%d"),
				PreparedParentProofRequest->Seed,
				*PreparedParentProofRequest->FootprintSize.ToString(),
				PreparedParentProofRequest->PlannedCells.Num(),
				PreparedParentProofRequest->IncomingBoundaryPoints.Num(),
				PreparedParentProofRequest->CommittedEndpointAnchors.Num(),
				PreparedParentProofRequest->CommittedTraversalAnchors.Num());
		}

		return FString::Printf(
			TEXT("ParentFootprint=%s|Placements=%s|CommittedSeams=%s|VerticalOwner=%s|PreparedParent=%s"),
			*ParentFootprintSize.ToString(),
			PlacementParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlacementParts, TEXT(";")),
			PlannedSeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedSeamParts, TEXT(";")),
			*BuildCommittedVerticalAccessOwnershipMemoKey(
				VerticalAccessOwnership,
				ChildMemoIdentityByRegionPath),
			*PreparedParentProofPart);
	}

	FString BuildDeferredCompletePlacementValidationKey(
		const FIntPoint& ParentFootprintSize,
		const TArray<FAutomaticChildPlacement>& Placements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FLayoutRegionSolveRequest* PreparedParentProofRequest)
	{
		return BuildCompletePlacementValidationKey(
			ParentFootprintSize,
			Placements,
			PlannedPartitionSeams,
			VerticalAccessOwnership,
			PreparedParentProofRequest);
	}

	void QueueDeferredCompletePlacementCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const FString& ParentRegionDebugPath,
		const FIntPoint& ParentFootprintSize,
		const int32 Score,
		const TArray<FAutomaticChildPlacement>& Placements,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FSkippedOptionalChildPlacement>& SkippedOptionalPlacements,
		const TArray<FLayoutValidationMessage>& Warnings,
		const bool bHasPreparedParentProofRequest,
		const FLayoutRegionSolveRequest* PreparedParentProofRequest,
		TArray<FPlacementBridgeDeferredValidationCandidate>& InOutCandidates,
		TSet<FString>& InOutCandidateKeys)
	{
		TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams;
		if (!BuildCommittedPartitionSeams(
			ParentRegionDebugPath,
			Placements,
			PlannedPartitionSeams,
			nullptr))
		{
			return;
		}

		const FString DeferredCandidateKey = BuildDeferredCompletePlacementValidationKey(
			ParentFootprintSize,
			Placements,
			PlannedPartitionSeams,
			VerticalAccessOwnership,
			bHasPreparedParentProofRequest ? PreparedParentProofRequest : nullptr);
		if (InOutCandidateKeys.Contains(DeferredCandidateKey))
		{
			return;
		}

		InOutCandidateKeys.Add(DeferredCandidateKey);
		FPlacementBridgeDeferredValidationCandidate& DeferredCandidate = InOutCandidates.AddDefaulted_GetRef();
		DeferredCandidate.RawScore = Score;
		DeferredCandidate.Score = Score;
		DeferredCandidate.TieBreakHash = GetTypeHash(DeferredCandidateKey);
		DeferredCandidate.Schedule.Placements = Placements;
		DeferredCandidate.Schedule.SkippedOptionalPlacements = SkippedOptionalPlacements;
		DeferredCandidate.Schedule.PlannedPartitionSeams = MoveTemp(PlannedPartitionSeams);
		DeferredCandidate.Schedule.VerticalAccessOwnership = VerticalAccessOwnership;
		DeferredCandidate.Schedule.Warnings = Warnings;
		DeferredCandidate.Schedule.bHasPreparedParentProofRequest =
			bHasPreparedParentProofRequest
			&& PreparedParentProofRequest != nullptr;
		if (DeferredCandidate.Schedule.bHasPreparedParentProofRequest)
		{
			DeferredCandidate.Schedule.PreparedParentProofRequest =
				*PreparedParentProofRequest;
		}
		DeferredCandidate.SelectionFeatures =
			BuildDeferredValidationCandidateSelectionFeatures(
				RootRequest,
				DeferredCandidate.Schedule);
		DeferredCandidate.OrderingPenalty =
			ComputeDeferredValidationOrderingPenalty(
				DeferredCandidate.SelectionFeatures);
		DeferredCandidate.Score -= DeferredCandidate.OrderingPenalty;
	}

	void SortDeferredCompletePlacementCandidates(
		TArray<FPlacementBridgeDeferredValidationCandidate>& InOutCandidates)
	{
		InOutCandidates.Sort([](
			const FPlacementBridgeDeferredValidationCandidate& Left,
			const FPlacementBridgeDeferredValidationCandidate& Right)
		{
			if (Left.Score != Right.Score)
			{
				return Left.Score > Right.Score;
			}

			return Left.TieBreakHash < Right.TieBreakHash;
		});
	}

	void TrimDeferredCompletePlacementCandidates(
		TArray<FPlacementBridgeDeferredValidationCandidate>& InOutCandidates,
		const int32 MaxCandidateCount)
	{
		if (InOutCandidates.Num() > MaxCandidateCount)
		{
			InOutCandidates.SetNum(MaxCandidateCount);
		}
	}

	FPlacementBridgeDeferredValidationResult ResolveDeferredCompletePlacementCandidates(
		const FString& RegionDebugPath,
		TArray<FPlacementBridgeDeferredValidationCandidate>& InOutCandidates,
		const int32 MaxCandidateCount,
		FDeferredValidationCandidateOrderingSummary* OutOrderingSummary,
		FRecursiveSchedulerPerfMetrics* InOutPerfMetrics,
		TFunctionRef<bool(FCommittedRecursiveScheduleState&, FString&)> ValidateCandidate)
	{
		SCOPED_NAMED_EVENT(Layout_Recursive_DeferredCandidates, FColor::Orange);
		TRACE_BOOKMARK(TEXT("Layout_DeferredCandidates region=%s retained=%d cap=%d"),
			*RegionDebugPath.Left(128), InOutCandidates.Num(), MaxCandidateCount);
		FPlacementBridgeDeferredValidationResult ResolutionResult;
		static const bool bShouldLogPerf =
			!FPlatformMisc::GetEnvironmentVariable(
				TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF"))
					.IsEmpty();
		SortDeferredCompletePlacementCandidates(InOutCandidates);
		if (OutOrderingSummary != nullptr)
		{
			PopulateDeferredValidationCandidateOrderingSummary(
				InOutCandidates,
				MaxCandidateCount,
				*OutOrderingSummary);
		}
		TrimDeferredCompletePlacementCandidates(
			InOutCandidates,
			MaxCandidateCount);

		for (FPlacementBridgeDeferredValidationCandidate& DeferredCandidate : InOutCandidates)
		{
			if (!LayoutSolveExecution::Checkpoint(ResolutionResult.FailureReason)) return ResolutionResult;
			if (OutOrderingSummary != nullptr)
			{
				++OutOrderingSummary->ValidatedCandidateCount;
			}
			TOptional<FDeferredValidationCandidateOrderingRow>
				CandidateOrderingRow;
			if (bShouldLogPerf || OutOrderingSummary != nullptr)
			{
				CandidateOrderingRow.Emplace(
					BuildDeferredValidationCandidateOrderingRow(
						DeferredCandidate));
			}
			FString ValidationFailureReason;
			const bool bCandidateSucceeded =
				ValidateCandidate(DeferredCandidate.Schedule, ValidationFailureReason);
			if (!LayoutSolveExecution::Checkpoint(ResolutionResult.FailureReason)) return ResolutionResult;
			if (bShouldLogPerf && InOutPerfMetrics != nullptr)
			{
				const FRecursiveSchedulerDeferredValidationOutcome& Outcome =
					InOutPerfMetrics->LastCompleteValidationOutcome;
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[RecursiveSchedulerDeferredValidated] region=%s rank=%d score=%d rawScore=%d orderingPenalty=%d tie=%u result=%s memo=%d parentSolve=%.6fs childSolve=%.6fs placements=%d directPlacements=%d siblingOnlyPlacements=%d parentContacts=%d routePoints=%d routeCells=%d routeMultiCells=%d boundaryFacingRoutePoints=%d prepRoutes=%d prepMainRoutes=%d prepBoundaryRoutes=%d prepInteriorRoutes=%d prepBoundaryMultiFace=%d prepRouteFaces=%d prepRequiredReservations=%d prepReachabilityBranches=%d prepDomainCells=%d prepDomainBoundaryCells=%d prepDomainMultiFaceCells=%d prepDomainEligible=%d prepDomainEliminated=%d prepDomainTightest=%d prepDomainSingle=%d prepDomainLe4=%d prepDomainFailed=%d prepDomainFailedCell=%s prepDomainFailedEligible=%d prepDomainFailedFaces=%d prepDomainFailedKind=%s prepDomainFailedKindCount=%d prepRepairAttempted=%d prepRepairSucceeded=%d prepRepairFailed=%d prepRepairFailedCell=%s prepRepairFailedEligible=%d prepRepairFailedFaces=%d prepRepairFailedKind=%s prepRepairFailedKindCount=%d prepPrefixPlaced=%d prepPrefixForced=%d prepPrefixSolved=%d prepPrefixNoCell=%d prepPrefixCells=%d prepPrefixEligible=%d prepPrefixTightest=%d prepPrefixSingle=%d prepPrefixLe4=%d prepPrefixCell=%s prepPrefixIntent=%s prepPrefixDomain=%d prepPrefixFailureKind=%s prepPrefixFailureKindCount=%d prepRepairPrefixAttempted=%d prepRepairPrefixSucceeded=%d prepRepairPrefixPlaced=%d prepRepairPrefixForced=%d prepRepairPrefixSolved=%d prepRepairPrefixNoCell=%d prepRepairPrefixCells=%d prepRepairPrefixEligible=%d prepRepairPrefixTightest=%d prepRepairPrefixSingle=%d prepRepairPrefixLe4=%d prepRepairPrefixCell=%s prepRepairPrefixIntent=%s prepRepairPrefixDomain=%d prepRepairPrefixFailureKind=%s prepRepairPrefixFailureKindCount=%d proofDomainCells=%d proofDomainBoundaryCells=%d proofDomainMultiFaceCells=%d proofDomainEligible=%d proofDomainEliminated=%d proofDomainTightest=%d proofDomainSingle=%d proofDomainLe4=%d proofDomainFailed=%d proofDomainFailedCell=%s proofDomainFailedEligible=%d proofDomainFailedFaces=%d proofDomainFailedKind=%s proofDomainFailedKindCount=%d warnings=%d skippedOptionals=%d scoreParts=%d preAdjust=%d preAdjustEntries=%d preAdjustResolve=%d entrySupportSaturation=%d entryIntent=%d entryCapability=%d entrySupport=%d entryProtected=%d entryAnchored=%d entryDistance=%d entryNoParent=%d entryDirectCount=%d entryNoParentCount=%d adjust=%d probeRoute=%d vertical=%d boundaryPenalty=%d shellPenalty=%d parentSeam=%d noSeamPenalty=%d siblingSeam=%d rootPenalty=%d providerPenalty=%d boundaryTouches=%d shellTouches=%d bundleCells=%d bundleUpper=%d bundleUpperInterior=%d bundleTop=%d bundleTopInterior=%d committedFaces=%s signature=%s failure=\"%s\""),
					*RegionDebugPath,
					OutOrderingSummary != nullptr
						? OutOrderingSummary->ValidatedCandidateCount
						: 0,
					CandidateOrderingRow->Score,
					CandidateOrderingRow->RawScore,
					CandidateOrderingRow->OrderingPenalty,
					CandidateOrderingRow->TieBreakHash,
					Outcome.ResultKind.IsNone() ? TEXT("<none>") : *Outcome.ResultKind.ToString(),
					Outcome.bMemoHit ? 1 : 0,
					Outcome.ParentSolveSeconds,
					Outcome.ChildSolveSeconds,
					CandidateOrderingRow->PlacementCount,
					CandidateOrderingRow->DirectPlacementCount,
					CandidateOrderingRow->SiblingOnlyPlacementCount,
					CandidateOrderingRow->ParentContactCount,
					CandidateOrderingRow->TraversalBoundaryPointCount,
					CandidateOrderingRow->TraversalBoundaryCellCount,
					CandidateOrderingRow->MultiTraversalBoundaryCellCount,
					CandidateOrderingRow->BoundaryFacingTraversalPointCount,
					Outcome.PreparedPlanRouteConstraintCount,
					Outcome.PreparedPlanMainRouteConstraintCount,
					Outcome.PreparedPlanBoundaryRouteConstraintCount,
					Outcome.PreparedPlanInteriorRouteConstraintCount,
					Outcome.PreparedPlanBoundaryMultiFaceConstraintCount,
					Outcome.PreparedPlanRouteFaceRequirementCount,
					Outcome.PreparedPlanRequiredRouteReservationCount,
					Outcome.PreparedPlanReachabilityBranchReservationCount,
					Outcome.PreparedPlanLiveDomainConstrainedCellCount,
					Outcome.PreparedPlanLiveDomainBoundaryConstrainedCellCount,
					Outcome.PreparedPlanLiveDomainMultiFaceBoundaryConstrainedCellCount,
					Outcome.PreparedPlanLiveDomainEligibleCandidateCount,
					Outcome.PreparedPlanLiveDomainEliminatedCandidateCount,
					Outcome.PreparedPlanLiveDomainTightestRemainingSize,
					Outcome.PreparedPlanLiveDomainSingleRemainingCandidateCellCount,
					Outcome.PreparedPlanLiveDomainAtMostFourRemainingCandidateCellCount,
					Outcome.bPreparedPlanLiveDomainFailedConstraint ? 1 : 0,
					*Outcome.PreparedPlanLiveDomainFailedConstraintCell.ToString(),
					Outcome.PreparedPlanLiveDomainFailedConstraintEligibleCandidateCount,
					Outcome.PreparedPlanLiveDomainFailedConstraintRequiredFaceCount,
					*ToDebugString(
						Outcome.PreparedPlanLiveDomainFailedConstraintDominantFailureKind),
					Outcome.PreparedPlanLiveDomainFailedConstraintDominantFailureCount,
					Outcome.bPreparedPlanRepairModeRouteDomainAttempted ? 1 : 0,
					Outcome.bPreparedPlanRepairModeRouteDomainSucceeded ? 1 : 0,
					Outcome.bPreparedPlanRepairModeLiveDomainFailedConstraint ? 1 : 0,
					*Outcome.PreparedPlanRepairModeLiveDomainFailedConstraintCell.ToString(),
					Outcome.PreparedPlanRepairModeLiveDomainFailedConstraintEligibleCandidateCount,
					Outcome.PreparedPlanRepairModeLiveDomainFailedConstraintRequiredFaceCount,
					*ToDebugString(
						Outcome.PreparedPlanRepairModeLiveDomainFailedConstraintDominantFailureKind),
					Outcome.PreparedPlanRepairModeLiveDomainFailedConstraintDominantFailureCount,
					Outcome.PreparedPlanSearchPrefixPlacedCellCount,
					Outcome.PreparedPlanSearchPrefixForcedPlacementCellCount,
					Outcome.bPreparedPlanSearchPrefixSolvedAfterForcedPlacements ? 1 : 0,
					Outcome.bPreparedPlanSearchPrefixNoCellSelectable ? 1 : 0,
					Outcome.PreparedPlanSearchPrefixConstrainedCellCount,
					Outcome.PreparedPlanSearchPrefixEligibleCandidateCount,
					Outcome.PreparedPlanSearchPrefixTightestRemainingSize,
					Outcome.PreparedPlanSearchPrefixSingleRemainingCandidateCellCount,
					Outcome.PreparedPlanSearchPrefixAtMostFourRemainingCandidateCellCount,
					*Outcome.PreparedPlanSearchPrefixSelectedCell.ToString(),
					*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(
						static_cast<int64>(
							Outcome.PreparedPlanSearchPrefixSelectedIntent)),
					Outcome.PreparedPlanSearchPrefixSelectedDomainSize,
					*Outcome.PreparedPlanSearchPrefixDominantFailureKind.ToString(),
					Outcome.PreparedPlanSearchPrefixDominantFailureCount,
					Outcome.bPreparedPlanRepairModeSearchPrefixAttempted ? 1 : 0,
					Outcome.bPreparedPlanRepairModeSearchPrefixSucceeded ? 1 : 0,
					Outcome.PreparedPlanRepairModeSearchPrefixPlacedCellCount,
					Outcome.PreparedPlanRepairModeSearchPrefixForcedPlacementCellCount,
					Outcome
						.bPreparedPlanRepairModeSearchPrefixSolvedAfterForcedPlacements
						? 1
						: 0,
					Outcome.bPreparedPlanRepairModeSearchPrefixNoCellSelectable
						? 1
						: 0,
					Outcome.PreparedPlanRepairModeSearchPrefixConstrainedCellCount,
					Outcome.PreparedPlanRepairModeSearchPrefixEligibleCandidateCount,
					Outcome.PreparedPlanRepairModeSearchPrefixTightestRemainingSize,
					Outcome
						.PreparedPlanRepairModeSearchPrefixSingleRemainingCandidateCellCount,
					Outcome
						.PreparedPlanRepairModeSearchPrefixAtMostFourRemainingCandidateCellCount,
					*Outcome.PreparedPlanRepairModeSearchPrefixSelectedCell
						 .ToString(),
					*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(
						static_cast<int64>(
							Outcome
								.PreparedPlanRepairModeSearchPrefixSelectedIntent)),
					Outcome.PreparedPlanRepairModeSearchPrefixSelectedDomainSize,
					*Outcome.PreparedPlanRepairModeSearchPrefixDominantFailureKind
						 .ToString(),
					Outcome.PreparedPlanRepairModeSearchPrefixDominantFailureCount,
					Outcome.ParentProofLiveDomainConstrainedCellCount,
					Outcome.ParentProofLiveDomainBoundaryConstrainedCellCount,
					Outcome.ParentProofLiveDomainMultiFaceBoundaryConstrainedCellCount,
					Outcome.ParentProofLiveDomainEligibleCandidateCount,
					Outcome.ParentProofLiveDomainEliminatedCandidateCount,
					Outcome.ParentProofLiveDomainTightestRemainingSize,
					Outcome.ParentProofLiveDomainSingleRemainingCandidateCellCount,
					Outcome.ParentProofLiveDomainAtMostFourRemainingCandidateCellCount,
					Outcome.bParentProofLiveDomainFailedConstraint ? 1 : 0,
					*Outcome.ParentProofLiveDomainFailedConstraintCell.ToString(),
					Outcome.ParentProofLiveDomainFailedConstraintEligibleCandidateCount,
					Outcome.ParentProofLiveDomainFailedConstraintRequiredFaceCount,
					*ToDebugString(
						Outcome.ParentProofLiveDomainFailedConstraintDominantFailureKind),
					Outcome.ParentProofLiveDomainFailedConstraintDominantFailureCount,
					CandidateOrderingRow->WarningCount,
					CandidateOrderingRow->SkippedOptionalCount,
					CandidateOrderingRow->bHasScoreBreakdown ? 1 : 0,
					CandidateOrderingRow->PreAdjustmentResolvedScore,
					CandidateOrderingRow->PreAdjustmentEntryAccumulatedScore,
					CandidateOrderingRow->PreAdjustmentResolveAdjustment,
					CandidateOrderingRow->DirectParentSupportSaturationPenalty,
					CandidateOrderingRow->EntryIntentScore,
					CandidateOrderingRow->EntryParentCapabilitySupportContribution,
					CandidateOrderingRow->EntryParentSupportContribution,
					CandidateOrderingRow->EntryProtectedTraversalPenalty,
					CandidateOrderingRow->EntryAnchoredIntentPenalty,
					CandidateOrderingRow->EntryPriorityDistancePenalty,
					CandidateOrderingRow->EntryNoParentResolutionScore,
					CandidateOrderingRow->DirectParentEntryCount,
					CandidateOrderingRow->NoParentEntryCount,
					CandidateOrderingRow->AdjustmentTotal,
					CandidateOrderingRow->ProbeRouteConstraintScore,
					CandidateOrderingRow->VerticalLevelPenalty,
					CandidateOrderingRow->BoundaryTouchPenalty,
					CandidateOrderingRow->ShellAdjacencyPenalty,
					CandidateOrderingRow->ParentChildSeamScore,
					CandidateOrderingRow->RequiredCopiesNoSeamPenalty,
					CandidateOrderingRow->SiblingSeamScore,
					CandidateOrderingRow->RootDistancePenalty,
					CandidateOrderingRow->ProviderDistancePenalty,
					CandidateOrderingRow->ParentBoundaryTouchCount,
					CandidateOrderingRow->ParentShellAdjacencyCount,
					Outcome.ChildBundleSupportedCellCount,
					Outcome.ChildBundleSupportedUpperCellCount,
					Outcome.ChildBundleSupportedUpperInteriorLikeCellCount,
					Outcome.ChildBundleSupportedTopLevelCellCount,
					Outcome.ChildBundleSupportedTopLevelInteriorLikeCellCount,
					*CandidateOrderingRow->CommittedContactFaceSignature,
					*CandidateOrderingRow->PlacementSignature,
					*Outcome.FailureFirstLine);
				if (!bCandidateSucceeded
					&& !ValidationFailureReason.IsEmpty()
					&& Outcome.ResultKind == TEXT("ParentProofFail"))
				{
					UE_LOG(
						LogTemp,
						Display,
						TEXT("[RecursiveSchedulerDeferredFailureDetail] region=%s rank=%d result=%s detail=\"%s\""),
						*RegionDebugPath,
						OutOrderingSummary != nullptr
							? OutOrderingSummary->ValidatedCandidateCount
							: 0,
						*Outcome.ResultKind.ToString(),
						*BuildDeferredValidationFailureDetailForLog(
							ValidationFailureReason));
				}
			}
			if (!bCandidateSucceeded)
			{
				if (ResolutionResult.FailureReason.IsEmpty() && !ValidationFailureReason.IsEmpty())
				{
					ResolutionResult.FailureReason = MoveTemp(ValidationFailureReason);
				}
				continue;
			}

			ResolutionResult.bSucceeded = true;
			ResolutionResult.BestScore = DeferredCandidate.Score;
			ResolutionResult.BestSchedule = MoveTemp(DeferredCandidate.Schedule);
			if (OutOrderingSummary != nullptr)
			{
				OutOrderingSummary->bSucceeded = true;
				OutOrderingSummary->SucceededRank = OutOrderingSummary->ValidatedCandidateCount;
				OutOrderingSummary->SuccessfulCandidate = *CandidateOrderingRow;
			}
			return ResolutionResult;
		}

		return ResolutionResult;
	}

	void LogDeferredValidationCandidateOrderingProfile(
		const FString& RegionDebugPath,
		const FDeferredValidationCandidateOrderingSummary& Summary)
	{
		static const bool bShouldLogPerf =
			!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty();
		if (!bShouldLogPerf || Summary.RetainedCount <= 0)
		{
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[RecursiveSchedulerDeferredQueue] region=%s queued=%d retained=%d distinctScores=%d topScoreTieCount=%d bestScore=%d cutoffScore=%d validated=%d succeeded=%d succeededRank=%d"),
			*RegionDebugPath,
			Summary.QueuedCount,
			Summary.RetainedCount,
			Summary.DistinctRetainedScoreCount,
			Summary.TopScoreTieCount,
			Summary.BestScore,
			Summary.CutoffScore,
			Summary.ValidatedCandidateCount,
			Summary.bSucceeded ? 1 : 0,
			Summary.SucceededRank);

		if (Summary.bSucceeded)
		{
			LogDeferredValidationCandidateOrderingRow(
				TEXT("RecursiveSchedulerDeferredSuccess"),
				RegionDebugPath,
				Summary.SucceededRank,
				Summary.SuccessfulCandidate);
		}

		for (int32 CandidateIndex = 0; CandidateIndex < Summary.TopCandidates.Num(); ++CandidateIndex)
		{
			LogDeferredValidationCandidateOrderingRow(
				TEXT("RecursiveSchedulerDeferredCandidate"),
				RegionDebugPath,
				CandidateIndex + 1,
				Summary.TopCandidates[CandidateIndex]);
		}

		for (const FDeferredValidationQueuedFamilySummary& FamilySummary :
			Summary.QueuedFamiliesByParentContact)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[RecursiveSchedulerDeferredQueuedFamily] region=%s parentContacts=%d count=%d bestRank=%d bestScore=%d bestRawScore=%d committedFaces=%s signature=%s"),
				*RegionDebugPath,
				FamilySummary.ParentContactCount,
				FamilySummary.CandidateCount,
				FamilySummary.BestRank,
				FamilySummary.BestCandidate.Score,
				FamilySummary.BestCandidate.RawScore,
				*FamilySummary.BestCandidate.CommittedContactFaceSignature,
				*FamilySummary.BestCandidate.PlacementSignature);
		}
	}

	bool FinalizePlacementValidationFailure(
		const FString& ValidationMemoKey,
		const FString& FailureReason,
		const FCommittedRecursiveScheduleState* CandidateSchedule,
		const double ValidationStartSeconds,
		TMap<FString, FPlacementBridgeDeferredValidationMemoEntry>& InOutValidationMemo,
		TMap<FString, FPlacementBridgeDeferredValidationMemoEntry>* SharedValidationMemo,
		FRecursiveSchedulerPerfMetrics& InOutPerfMetrics,
		FString& OutValidationFailureReason)
	{
		OutValidationFailureReason = FailureReason;
		if (CandidateSchedule != nullptr && !CandidateSchedule->Placements.IsEmpty())
		{
			OutValidationFailureReason = FString::Printf(
				TEXT("%s\nCommitted placement schedule: %s"),
				*FailureReason,
				*DescribeCommittedPlacementValidationSchedule(*CandidateSchedule));
		}

		FPlacementBridgeDeferredValidationMemoEntry& FailedMemo =
			InOutValidationMemo.Add(ValidationMemoKey);
		FailedMemo.bSucceeded = false;
		FailedMemo.FailureReason = OutValidationFailureReason;
		if (SharedValidationMemo != nullptr)
		{
			SharedValidationMemo->Add(ValidationMemoKey, FailedMemo);
		}

		InOutPerfMetrics.CompleteValidationSeconds +=
			(FPlatformTime::Seconds() - ValidationStartSeconds);
		return false;
	}

	void FinalizePlacementValidationSuccess(
		const FString& ValidationMemoKey,
		const double ValidationStartSeconds,
		TMap<FString, FPlacementBridgeDeferredValidationMemoEntry>& InOutValidationMemo,
		TMap<FString, FPlacementBridgeDeferredValidationMemoEntry>* SharedValidationMemo,
		FRecursiveSchedulerPerfMetrics& InOutPerfMetrics)
	{
		FPlacementBridgeDeferredValidationMemoEntry& SuccessMemo =
			InOutValidationMemo.Add(ValidationMemoKey);
		SuccessMemo.bSucceeded = true;
		SuccessMemo.FailureReason.Reset();
		if (SharedValidationMemo != nullptr)
		{
			SharedValidationMemo->Add(ValidationMemoKey, SuccessMemo);
		}

		InOutPerfMetrics.CompleteValidationSeconds +=
			(FPlatformTime::Seconds() - ValidationStartSeconds);
	}

	bool FDeferredCompletePlacementValidator::operator()(
		FCommittedRecursiveScheduleState& CandidateSchedule,
		FString& OutValidationFailureReason) const
	{
		PORISM_LAYOUT_PROBE_SCOPE(Layout_Probe_DeferredValidation, STAT_PorismLayout_DeferredValidation);
		INC_DWORD_STAT(STAT_PorismLayout_DeferredValidationCalls);
		static const bool bTrackDetailedRecursivePerf =
			!FPlatformMisc::GetEnvironmentVariable(
				TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF"))
					.IsEmpty();
		const double ValidationStartSeconds = FPlatformTime::Seconds();
		++SchedulerState.PerfMetrics.CompleteValidationCalls;
		TRACE_BOOKMARK(TEXT("Layout_DeferredBegin region=%s seed=%d attempt=%d children=%d seams=%d hostGroups=%d deadlineSet=%d remainingMs=%.3f"),
			*RootRequest.RegionDebugPath.Left(128), RootRequest.Seed, SchedulerState.PerfMetrics.CompleteValidationCalls,
			CandidateSchedule.Placements.Num(), CandidateSchedule.PlannedPartitionSeams.Num(), RootRequest.VerticalAccessHostGroups.Num(), SharedDeadlineSeconds > 0.0,
			SharedDeadlineSeconds > 0.0 ? (SharedDeadlineSeconds - ValidationStartSeconds) * 1000.0 : -1.0);
		ON_SCOPE_EXIT
		{
			TRACE_BOOKMARK(TEXT("Layout_DeferredEnd region=%s attempt=%d elapsedMs=%.3f deadlineSet=%d remainingMs=%.3f failure=%s"),
				*RootRequest.RegionDebugPath.Left(128), SchedulerState.PerfMetrics.CompleteValidationCalls,
				(FPlatformTime::Seconds() - ValidationStartSeconds) * 1000.0, SharedDeadlineSeconds > 0.0,
				SharedDeadlineSeconds > 0.0 ? (SharedDeadlineSeconds - FPlatformTime::Seconds()) * 1000.0 : -1.0,
				*OutValidationFailureReason.Left(256));
		};
		CandidateSchedule.CertifiedRegionalProofResults = FCertifiedRegionalProofResults();
		for (FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
		{
			if (!FinalizeMappedChildVerticalAccess(Placement, OutValidationFailureReason)) return false;
		}
		if (SharedDeadlineSeconds > 0.0
			&& ValidationStartSeconds >= SharedDeadlineSeconds)
		{
			OutValidationFailureReason = TEXT("Recursive child coordination exhausted its shared solve-duration budget before deferred proof validation.");
			return false;
		}
		if (bTrackDetailedRecursivePerf)
		{
			ResetDeferredValidationOutcome(SchedulerState.PerfMetrics);
		}
		// These outcome snapshots only feed detailed recursive perf logging.
		const auto MaybeRecordDeferredValidationOutcome =
			[&](
				const FLayoutId ResultKind,
				const bool bSucceeded,
				const bool bMemoHit,
				const double ParentSolveSeconds,
				const double ChildSolveSeconds,
				const FString& FailureReason)
			{
				TRACE_BOOKMARK(TEXT("Layout_DeferredOutcome region=%s attempt=%d kind=%s ok=%d memo=%d parentMs=%.3f childMs=%.3f failure=%s"),
					*RootRequest.RegionDebugPath.Left(128), SchedulerState.PerfMetrics.CompleteValidationCalls,
					*ResultKind.ToString().Left(64), bSucceeded, bMemoHit, ParentSolveSeconds * 1000.0, ChildSolveSeconds * 1000.0,
					*FailureReason.Left(256));
				if (!bTrackDetailedRecursivePerf)
				{
					return;
				}

				// Keep the first bounded failures visible even when preflight never
				// produces a parent/child solve whose detailed metrics can be emitted.
				if (SchedulerState.PerfMetrics.CompleteValidationCalls <= 4 || bSucceeded)
				{
					UE_LOG(LogTemp, Display, TEXT("[RecursiveDeferredOutcome] region=%s attempt=%d kind=%s ok=%d memo=%d failure=%s"),
						*RootRequest.RegionDebugPath.Left(128), SchedulerState.PerfMetrics.CompleteValidationCalls,
						*ResultKind.ToString().Left(64), bSucceeded, bMemoHit, *FailureReason.Left(2048));
				}
				RecordDeferredValidationOutcome(
					SchedulerState.PerfMetrics,
					ResultKind,
					bSucceeded,
					bMemoHit,
					ParentSolveSeconds,
					ChildSolveSeconds,
					FailureReason);
			};
		const FString ValidationMemoKey = BuildCompletePlacementValidationKey(
			ParentFootprintSize,
			CandidateSchedule.Placements,
			CandidateSchedule.PlannedPartitionSeams,
			CandidateSchedule.VerticalAccessOwnership,
			CandidateSchedule.bHasPreparedParentProofRequest
				? &CandidateSchedule.PreparedParentProofRequest
				: nullptr);

		TRACE_BOOKMARK(TEXT("Layout_DeferredIdentity region=%s attempt=%d key=%08x"),
			*RootRequest.RegionDebugPath.Left(128), SchedulerState.PerfMetrics.CompleteValidationCalls, GetTypeHash(ValidationMemoKey));
		bool bValidationCoarseKeyRepeated = false;
		if (bTrackDetailedRecursivePerf)
		{
			const FString ValidationCoarseKey =
				BuildCompletePlacementValidationCoarseKey(
					ParentFootprintSize,
					CandidateSchedule.Placements,
					CandidateSchedule.PlannedPartitionSeams,
					CandidateSchedule.VerticalAccessOwnership,
					CandidateSchedule.bHasPreparedParentProofRequest
						? &CandidateSchedule.PreparedParentProofRequest
						: nullptr);
			bValidationCoarseKeyRepeated =
				SchedulerState.CompletePlacementValidationCoarseKeysSeen.Contains(
					ValidationCoarseKey);
			if (!bValidationCoarseKeyRepeated)
			{
				SchedulerState.CompletePlacementValidationCoarseKeysSeen.Add(
					ValidationCoarseKey);
				++SchedulerState.PerfMetrics.CompleteValidationCoarseKeyCount;
			}
		}

		if (const FPlacementValidationMemo* ExistingMemo =
				SchedulerState.CompletePlacementValidationMemo.Find(
					ValidationMemoKey))
		{
			++SchedulerState.PerfMetrics.CompleteValidationMemoHits;
			MaybeRecordDeferredValidationOutcome(
				ExistingMemo->bSucceeded ? TEXT("MemoSuccess") : TEXT("MemoFailure"),
				ExistingMemo->bSucceeded,
				true,
				0.0,
				0.0,
				ExistingMemo->FailureReason);
			SchedulerState.PerfMetrics.CompleteValidationSeconds +=
				(FPlatformTime::Seconds() - ValidationStartSeconds);
			OutValidationFailureReason = ExistingMemo->FailureReason;
			return ExistingMemo->bSucceeded;
		}

		if (SchedulerState.SharedCompletePlacementValidationMemo != nullptr)
		{
			if (const FPlacementValidationMemo* SharedMemo =
					SchedulerState.SharedCompletePlacementValidationMemo->Find(
						ValidationMemoKey))
			{
				SchedulerState.CompletePlacementValidationMemo.Add(
					ValidationMemoKey,
					*SharedMemo);
				++SchedulerState.PerfMetrics.CompleteValidationMemoHits;
				MaybeRecordDeferredValidationOutcome(
					SharedMemo->bSucceeded ? TEXT("SharedMemoSuccess") : TEXT("SharedMemoFailure"),
					SharedMemo->bSucceeded,
					true,
					0.0,
					0.0,
					SharedMemo->FailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSeconds +=
					(FPlatformTime::Seconds() - ValidationStartSeconds);
				OutValidationFailureReason = SharedMemo->FailureReason;
				return SharedMemo->bSucceeded;
			}
		}

		if (bValidationCoarseKeyRepeated)
		{
			++SchedulerState.PerfMetrics.CompleteValidationCoarseKeyRepeatCount;
		}

		FDeferredParentValidationPreflight ValidationPreflight;
		EDeferredParentValidationPreflightFailureKind PreflightFailureKind =
			EDeferredParentValidationPreflightFailureKind::None;
		const double ValidationPrepStartSeconds = FPlatformTime::Seconds();
		if (!TryBuildDeferredParentValidationPreflight(
				RootRequest,
				ParentPlannedCells,
				ParentPlannedCellSet,
				ParentPlannedCellIntents,
				ParentFootprintSize,
				ParentProtectedTraversalCells,
				ParentVerticalAccessIngressDirections,
				CandidateSchedule,
				bTrackDetailedRecursivePerf,
				ValidationPreflight,
				PreflightFailureKind,
				OutValidationFailureReason))
		{
			if (PreflightFailureKind
				== EDeferredParentValidationPreflightFailureKind::BuildParentRequest)
			{
				++SchedulerState.PerfMetrics
					 .CompleteValidationBuildParentRequestFailures;
			}
			else if (bTrackDetailedRecursivePerf)
			{
				PopulateDeferredParentValidationPreflightDiagnostics(
					ValidationPreflight,
					SchedulerState.PerfMetrics);
			}

			MaybeRecordDeferredValidationOutcome(
				GetDeferredParentValidationPreflightFailureResultKind(
					PreflightFailureKind),
				false,
				false,
				0.0,
				0.0,
				OutValidationFailureReason);
			SchedulerState.PerfMetrics.CompleteValidationPrepSeconds +=
				(FPlatformTime::Seconds() - ValidationPrepStartSeconds);
			return FinalizePlacementValidationFailure(
				ValidationMemoKey,
				OutValidationFailureReason,
				&CandidateSchedule,
				ValidationStartSeconds,
				SchedulerState.CompletePlacementValidationMemo,
				SchedulerState.SharedCompletePlacementValidationMemo,
				SchedulerState.PerfMetrics,
				OutValidationFailureReason);
		}

		if (bTrackDetailedRecursivePerf)
		{
			PopulateDeferredParentValidationPreflightDiagnostics(
				ValidationPreflight,
				SchedulerState.PerfMetrics);
		}

		if (SharedDeadlineSeconds > 0.0)
		{
			const double RemainingSeconds =
				SharedDeadlineSeconds - FPlatformTime::Seconds();
			if (RemainingSeconds <= 0.0)
			{
				OutValidationFailureReason = TEXT("Recursive child coordination exhausted its shared solve-duration budget before parent proof.");
				return false;
			}
			ValidationPreflight.ParentRequest.ExecutionSettings.MaxSolveDurationSeconds =
				static_cast<float>(RemainingSeconds);
		}

		if (CanUseIndependentDeferredProofValidation(CandidateSchedule))
		{
			++SchedulerState.PerfMetrics.CompleteValidationIndependentSplitCalls;
			if (bTrackDetailedRecursivePerf)
			{
				const FString ParentRequestKey =
					BuildCanonicalParentProbeSolveRequestMemoKey(
						ValidationPreflight.ParentRequest);
				if (SchedulerState.CompletePlacementValidationParentRequestKeysSeen
						.Contains(ParentRequestKey))
				{
					++SchedulerState.PerfMetrics
						 .CompleteValidationParentRequestRepeatCount;
				}
				else
				{
					SchedulerState.CompletePlacementValidationParentRequestKeysSeen.Add(
						ParentRequestKey);
					++SchedulerState.PerfMetrics
						 .CompleteValidationParentRequestKeyCount;
				}
			}

			SchedulerState.PerfMetrics.CompleteValidationPrepSeconds +=
				(FPlatformTime::Seconds() - ValidationPrepStartSeconds);

			const double IndependentSolveStartSeconds = FPlatformTime::Seconds();
			FDeferredIndependentParentPrefixStageResult ParentPrefixStage;
			if (!TryExecuteDeferredIndependentParentPrefixStage(
					ValidationPreflight,
					bTrackDetailedRecursivePerf,
					SchedulerState.PerfMetrics,
					ParentPrefixStage,
					OutValidationFailureReason))
			{
				PreserveDeferredParentPreparationFailure(
					ParentPrefixStage.PreparedLeafSearchBranchContinuation.FailureSolveResult,
					SchedulerState);
				MaybeRecordDeferredValidationOutcome(
					TEXT("ParentProofFail"),
					false,
					false,
					ParentPrefixStage.ParentPrefixSeconds,
					0.0,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			FDeferredIndependentParentBranchApplyStageResult ParentBranchApplyStage;
			if (!TryExecuteDeferredIndependentParentBranchApplyStage(
					ValidationPreflight,
					ParentPrefixStage,
					bTrackDetailedRecursivePerf,
					SchedulerState.PerfMetrics,
					ParentBranchApplyStage,
					OutValidationFailureReason))
			{
				PreserveDeferredParentPreparationFailure(
					ParentBranchApplyStage.PreparedLeafSearchBranchApplyContinuation.FailureSolveResult,
					SchedulerState);
				MaybeRecordDeferredValidationOutcome(
					TEXT("ParentProofFail"),
					false,
					false,
					ParentPrefixStage.ParentPrefixSeconds
						+ ParentBranchApplyStage.ParentBranchApplySeconds,
					0.0,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			FDeferredIndependentParentPostBranchPrefixStageResult
				ParentPostBranchPrefixStage;
			if (!TryExecuteDeferredIndependentParentPostBranchPrefixStage(
					ValidationPreflight,
					ParentBranchApplyStage,
					bTrackDetailedRecursivePerf,
					SchedulerState.PerfMetrics,
					ParentPostBranchPrefixStage,
					OutValidationFailureReason))
			{
				PreserveDeferredParentPreparationFailure(
					ParentPostBranchPrefixStage.PreparedLeafSearchPrefixContinuation.FailureSolveResult,
					SchedulerState);
				MaybeRecordDeferredValidationOutcome(
					TEXT("ParentProofFail"),
					false,
					false,
					ParentPrefixStage.ParentPrefixSeconds
						+ ParentBranchApplyStage.ParentBranchApplySeconds
						+ ParentPostBranchPrefixStage
							  .ParentPostBranchPrefixSeconds,
					0.0,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			FDeferredIndependentParentSolveStageResult ParentSolveStage;
			double ChildSolveSecondsForOutcome = 0.0;
			if (!TryExecuteDeferredIndependentParentSolveStage(
					ValidationPreflight,
					ParentPrefixStage,
					ParentBranchApplyStage,
					ParentPostBranchPrefixStage,
					bTrackDetailedRecursivePerf,
					SchedulerState.PerfMetrics,
					ParentSolveStage,
					OutValidationFailureReason))
			{
				PreserveDeferredParentPreparationFailure(
					ParentSolveStage.ParentProofResult.SolveResult,
					SchedulerState);
				MaybeRecordDeferredValidationOutcome(
					TEXT("ParentProofFail"),
					false,
					false,
					ParentSolveStage.ParentSolveSeconds,
					0.0,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			if (!TryExecuteDeferredIndependentParentAnchorAuditStage(
					ValidationPreflight,
					ParentSolveStage,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason))
			{
				MaybeRecordDeferredValidationOutcome(
					TEXT("ParentAnchorFail"),
					false,
					false,
					ParentSolveStage.ParentSolveSeconds,
					0.0,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			FDeferredIndependentChildProofRequestStageResult ChildProofRequestStage;
			EDeferredIndependentChildProofRequestFailureKind
				ChildProofRequestFailureKind =
					EDeferredIndependentChildProofRequestFailureKind::None;
			if (!TryPrepareDeferredIndependentChildProofRequestsStage(
					ParentSolveStage,
					CandidateSchedule,
					SchedulerState.PerfMetrics,
					ChildProofRequestStage,
					ChildProofRequestFailureKind,
					OutValidationFailureReason))
			{
				MaybeRecordDeferredValidationOutcome(
					TEXT("ChildBoundaryFail"),
					false,
					false,
					ParentSolveStage.ParentSolveSeconds,
					0.0,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			FDeferredIndependentChildProofSolveStageResult ChildProofSolveStage;
			EDeferredIndependentChildProofSolveFailureKind
				ChildProofSolveFailureKind =
					EDeferredIndependentChildProofSolveFailureKind::None;
			if (!TryExecuteDeferredIndependentChildProofSolveStage(
					ChildProofRequestStage,
					SchedulerState.PerfMetrics,
					ChildProofSolveStage,
					ChildProofSolveFailureKind,
					OutValidationFailureReason))
			{
				// Preserve typed zero-attempt child preparation rejection after
				// plan compilation moved ahead of exact child route proof.
				if (!ChildProofSolveStage.ChildProofResults.IsEmpty())
				{
					const FLayoutSolveResult& FailedChildSolve =
						ChildProofSolveStage.ChildProofResults.Last().SolveResult;
					if (FailedChildSolve.PreparationFailureKind !=
						ELayoutSolvePreparationFailureKind::None)
					{
						SchedulerState.PreparationFailureKind =
							FailedChildSolve.PreparationFailureKind;
						SchedulerState.PreparationCandidateAttemptCount +=
							FailedChildSolve.PropagationStats.CandidateAttemptCount;
					}
				}
				MaybeRecordDeferredValidationOutcome(
					TEXT("ChildProofFail"),
					false,
					false,
					ParentSolveStage.ParentSolveSeconds,
					ChildProofSolveStage.ChildSolveSeconds,
					OutValidationFailureReason);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}
			ChildSolveSecondsForOutcome = ChildProofSolveStage.ChildSolveSeconds;

			SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
				(FPlatformTime::Seconds() - IndependentSolveStartSeconds);
			// Validate copies now; retain original regional outputs for final certified reduction.
			FLayoutRegionSolveResult ParentProofResultForValidation =
				ParentSolveStage.ParentProofResult;
			TArray<FLayoutRegionSolveResult> ChildProofResultsForValidation =
				ChildProofSolveStage.ChildProofResults;
			FDeferredIndependentScheduleBuildStageResult ScheduleBuildStage;
			EDeferredIndependentScheduleBuildFailureKind ScheduleBuildFailureKind =
				EDeferredIndependentScheduleBuildFailureKind::None;
			if (!TryExecuteDeferredIndependentScheduleBuildStage(
					ValidationPreflight,
					CandidateSchedule,
					MoveTemp(ParentProofResultForValidation),
					ChildProofRequestStage.ChildProofRequests,
					MoveTemp(ChildProofResultsForValidation),
					SchedulerState.PerfMetrics,
					ScheduleBuildStage,
					ScheduleBuildFailureKind,
					OutValidationFailureReason))
			{
				MaybeRecordDeferredValidationOutcome(
					TEXT("IndependentScheduleBuildFail"),
					false,
					false,
					ParentSolveStage.ParentSolveSeconds,
					ChildSolveSecondsForOutcome,
					OutValidationFailureReason);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			FDeferredIndependentTraversalAuditStageResult TraversalAuditStage;
			EDeferredIndependentTraversalAuditFailureKind
				TraversalAuditFailureKind =
					EDeferredIndependentTraversalAuditFailureKind::None;
			if (!TryExecuteDeferredIndependentTraversalAuditStage(
					ValidationPreflight,
					CandidateSchedule,
					ScheduleBuildStage,
					SchedulerState.PerfMetrics,
					TraversalAuditStage,
					TraversalAuditFailureKind,
					OutValidationFailureReason))
			{
				MaybeRecordDeferredValidationOutcome(
					TEXT("MergedTraversalFail"),
					false,
					false,
					ParentSolveStage.ParentSolveSeconds,
					ChildSolveSecondsForOutcome,
					OutValidationFailureReason);
				return FinalizePlacementValidationFailure(
					ValidationMemoKey,
					OutValidationFailureReason,
					&CandidateSchedule,
					ValidationStartSeconds,
					SchedulerState.CompletePlacementValidationMemo,
					SchedulerState.SharedCompletePlacementValidationMemo,
					SchedulerState.PerfMetrics,
					OutValidationFailureReason);
			}

			CandidateSchedule.VerticalAccessOwnership =
				ValidationPreflight.EffectiveVerticalAccessOwnership;
			CandidateSchedule.PreparedParentProofRequest = ValidationPreflight.ParentRequest;
			CandidateSchedule.bHasPreparedParentProofRequest = true;
			CandidateSchedule.bUsedIndependentDeferredProofSplit = true;
			CandidateSchedule.CertifiedRegionalProofResults.bIsSet = true;
			CandidateSchedule.CertifiedRegionalProofResults.ValidationKey = ValidationMemoKey;
			CandidateSchedule.CertifiedRegionalProofResults.ParentProofResult =
				MoveTemp(ParentSolveStage.ParentProofResult);
			CandidateSchedule.CertifiedRegionalProofResults.ChildProofResults =
				MoveTemp(ChildProofSolveStage.ChildProofResults);
			++SchedulerState.PerfMetrics.CompleteValidationSuccesses;
			MaybeRecordDeferredValidationOutcome(
				TEXT("Success"),
				true,
				false,
				ParentSolveStage.ParentSolveSeconds,
				ChildSolveSecondsForOutcome,
				FString());
			FinalizePlacementValidationSuccess(
				ValidationMemoKey,
				ValidationStartSeconds,
				SchedulerState.CompletePlacementValidationMemo,
				SchedulerState.SharedCompletePlacementValidationMemo,
				SchedulerState.PerfMetrics);
			OutValidationFailureReason.Reset();
			return true;
		}

		FLayoutRegionSolveScheduleRequest ScheduleRequest;
		BuildPlacementValidationScheduleRequest(
			ValidationPreflight.ParentRequest,
			CandidateSchedule,
			ScheduleRequest);
		SchedulerState.PerfMetrics.CompleteValidationPrepSeconds +=
			(FPlatformTime::Seconds() - ValidationPrepStartSeconds);

		const FLayoutRegionSolveScheduleResult ScheduleResult =
			[&]() -> FLayoutRegionSolveScheduleResult
			{
				const double SolveStartSeconds = FPlatformTime::Seconds();
				const FLayoutRegionSolveScheduleResult Result =
					FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
				SchedulerState.PerfMetrics.CompleteValidationSolveSeconds +=
					(FPlatformTime::Seconds() - SolveStartSeconds);
				return Result;
			}();
		if (!ScheduleResult.bSucceeded)
		{
			if (ScheduleResult.MergedSolveResult.PreparationFailureKind !=
				ELayoutSolvePreparationFailureKind::None)
			{
				SchedulerState.PreparationFailureKind =
					ScheduleResult.MergedSolveResult.PreparationFailureKind;
				SchedulerState.PreparationCandidateAttemptCount +=
					ScheduleResult.MergedSolveResult.PropagationStats.CandidateAttemptCount;
			}
			++SchedulerState.PerfMetrics.CompleteValidationScheduleSolveFailures;
			MaybeRecordDeferredValidationOutcome(
				TEXT("ScheduleSolveFail"),
				false,
				false,
				0.0,
				0.0,
				ScheduleResult.FailureReason);
			return FinalizePlacementValidationFailure(
				ValidationMemoKey,
				ScheduleResult.FailureReason,
				&CandidateSchedule,
				ValidationStartSeconds,
				SchedulerState.CompletePlacementValidationMemo,
				SchedulerState.SharedCompletePlacementValidationMemo,
				SchedulerState.PerfMetrics,
				OutValidationFailureReason);
		}

		FString MergedTraversalFailureReason;
		const double ValidationAuditStartSeconds = FPlatformTime::Seconds();
		if (!ValidateMergedScheduleTraversalConnectivity(
				ScheduleResult,
				RootRequest.RegionDebugPath,
				ValidationPreflight.EffectiveVerticalAccessOwnership,
				&CandidateSchedule,
				MergedTraversalFailureReason))
		{
			++SchedulerState.PerfMetrics.CompleteValidationMergedTraversalFailures;
			MaybeRecordDeferredValidationOutcome(
				TEXT("MergedTraversalFail"),
				false,
				false,
				0.0,
				0.0,
				MergedTraversalFailureReason);
			return FinalizePlacementValidationFailure(
				ValidationMemoKey,
				MergedTraversalFailureReason,
				&CandidateSchedule,
				ValidationStartSeconds,
				SchedulerState.CompletePlacementValidationMemo,
				SchedulerState.SharedCompletePlacementValidationMemo,
				SchedulerState.PerfMetrics,
				OutValidationFailureReason);
		}
		SchedulerState.PerfMetrics.CompleteValidationAuditSeconds +=
			(FPlatformTime::Seconds() - ValidationAuditStartSeconds);

		CandidateSchedule.VerticalAccessOwnership =
			ValidationPreflight.EffectiveVerticalAccessOwnership;
		++SchedulerState.PerfMetrics.CompleteValidationSuccesses;
		MaybeRecordDeferredValidationOutcome(
			TEXT("Success"),
			true,
			false,
			0.0,
			0.0,
			FString());
		FinalizePlacementValidationSuccess(
			ValidationMemoKey,
			ValidationStartSeconds,
			SchedulerState.CompletePlacementValidationMemo,
			SchedulerState.SharedCompletePlacementValidationMemo,
			SchedulerState.PerfMetrics);
		return true;
	}
}
