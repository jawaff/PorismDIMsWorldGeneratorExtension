// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "LayoutRegionSchedulePlacementBridge.h"
#include "LayoutSolveExecutionBudget.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Contracts/LayoutContractPipeline.h"

/**
 * Live implementation home for the recursive placement bridge.
 *
 * The legacy file is no longer a compiled fallback. This file is now the only
 * live runtime path and will keep shedding extracted ownership into dedicated
 * helper units until the bridge is reduced to orchestration only.
 */

#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutPlacementOccupancy.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutZoneFeatureDemand.h"
#include "LayoutRegionScheduleDeferredValidation.h"
#include "LayoutRegionScheduleParentProbe.h"
#include "LayoutRegionSchedulePlacementBridgeTypes.h"
#include "LayoutRegionScheduleSearchRuntime.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Misc/ScopeExit.h"

/**
 * Main implementation body for the recursive placement bridge.
 *
 * The hot path is still being split into smaller runtime units, but this file
 * now owns the live orchestration path directly.
 */

namespace LayoutRegionScheduleSolverPrivate
{
	FLayoutRegionSolveScheduleResult SolveRegionsSynchronouslyInternal(const FLayoutRegionSolveScheduleRequest& ScheduleRequest);
	FLayoutRegionSolveScheduleResult SolveRegionTreeInternal(const FLayoutRegionSolveRequest& RootRequest);
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
	void AppendDroppedChildResidualCells(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutDroppedOptionalChildRecord& DropRecord,
		FLayoutSolveResult& MergedSolveResult);
	void AppendOwnedDroppedChildResidualCellsToRegionResult(
		const FString& RegionDebugPath,
		const FLayoutSolveResult& MergedSolveResult,
		const TArray<FLayoutDroppedOptionalChildRecord>& DroppedOptionalChildren,
		FLayoutSolveResult& RegionSolveResult);
	bool EvaluateScheduledZoneFeatureRequirements(
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason);
	void SuppressPassiveSeamPlacementsOnRegionResults(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult);
	void NormalizeSuccessfulChildPlacementCandidatesForTestsInternal(
		TArray<LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests>& InOutCandidates,
		const bool bPartialPlacementStep,
		const int32 PlacementDemandCount,
		int32& OutCollapsedCount,
		int32& OutBeamTrimmedCount);
	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnershipFromNegotiatedContracts(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TArray<FAutomaticChildPlacement>& Placements);
	FIntVector MapSourceChildCellToParent(
		const FIntVector& SourceChildCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping);
	FIntVector MapParentCellToChildResultCell(
		const FIntVector& ParentCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping);
	FIntVector MapChildResultCellToParent(
		const FIntVector& ChildCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping);

	bool AreConnectionTagsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		const FLayoutFaceRule& TargetFaceRule)
	{
		return SourceFaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| SourceFaceRule.GetEffectiveAllowedConnectionTags().HasAnyExact(TargetFaceRule.GetEffectiveConnectionTags());
	}

	int32 NormalizeSolverYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	bool DoSolverFaceRulesRequireMatchingYaw(const FLayoutFaceRule& SourceFaceRule, const FLayoutFaceRule& TargetFaceRule)
	{
		return SourceFaceRule.bRequireMatchingYawWithFilledNeighbor
			|| TargetFaceRule.bRequireMatchingYawWithFilledNeighbor;
	}

	bool AreSolverYawRotationsCompatible(
		const FLayoutFaceRule& SourceFaceRule,
		const int32 SourceYawRotationSteps,
		const FLayoutFaceRule& TargetFaceRule,
		const int32 TargetYawRotationSteps)
	{
		return !DoSolverFaceRulesRequireMatchingYaw(SourceFaceRule, TargetFaceRule)
			|| NormalizeSolverYawRotationSteps(SourceYawRotationSteps) == NormalizeSolverYawRotationSteps(TargetYawRotationSteps);
	}

	FString RolesToStableKey(const TArray<ELayoutModuleRole>& Roles)
	{
		if (Roles.IsEmpty())
		{
			return TEXT("<none>");
		}

		TArray<FString> Parts;
		Parts.Reserve(Roles.Num());
		for (const ELayoutModuleRole Role : Roles)
		{
			Parts.Add(StaticEnum<ELayoutModuleRole>()->GetNameStringByValue(static_cast<int64>(Role)));
		}
		Parts.Sort();
		return FString::Join(Parts, TEXT(","));
	}

	bool HasSharedConnectedWalkableArea(const FLayoutFaceRule& LeftFaceRule, const FLayoutFaceRule& RightFaceRule)
	{
		for (const FGameplayTag& TraversalChannel : LeftFaceRule.ConnectedTraversalChannels)
		{
			if (TraversalChannel.IsValid() && RightFaceRule.ConnectedTraversalChannels.HasTagExact(TraversalChannel))
			{
				return true;
			}
		}

		return false;
	}

	bool AreCommittedConnectionTagsCompatible(
		const FGameplayTag CapabilityConnectionTag,
		const FGameplayTagContainer& CapabilityAllowedConnectionTags,
		const FGameplayTag CommitmentConnectionTag,
		const FGameplayTagContainer& CommitmentAllowedConnectionTags);

	bool IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(const FLayoutFaceRule& FaceRule)
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

	/** Chooses one stable legacy-compatible primary tag from an exact face domain. */
	FGameplayTag GetStablePrimaryFaceConnectionTag(const FLayoutFaceRule& FaceRule)
	{
		if (FaceRule.ConnectionTag.IsValid())
		{
			return FaceRule.ConnectionTag;
		}
		TArray<FGameplayTag> Tags;
		FaceRule.GetEffectiveConnectionTags().GetGameplayTagArray(Tags);
		Tags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		return Tags.IsEmpty() ? FGameplayTag() : Tags[0];
	}

	bool TryGetSnapshotWorldFaceRule(
		const FLayoutModuleSolveSnapshot& ModuleSnapshot,
		const ELayoutFaceDirection WorldDirection,
		const int32 YawRotationSteps,
		FLayoutFaceRule& OutFaceRule)
	{
		// Composite root candidates expose root-cell generated faces rather than
		// an intentionally empty aggregate face set.
		if (ModuleSnapshot.OccupiedLocalCells.Num() > 1)
		{
			const FLayoutLocalCellFaceRuleSnapshot* const RootCellRules =
				ModuleSnapshot.GeneratedLocalCellFaceRules.FindByPredicate(
					[](const FLayoutLocalCellFaceRuleSnapshot& Candidate)
					{
						return Candidate.LocalCell == FIntVector::ZeroValue;
					});
			if (RootCellRules != nullptr)
			{
				const FLayoutFaceRule* const RootFaceRule =
					RootCellRules->ExposedFaceRules.FindByPredicate(
						[WorldDirection, YawRotationSteps](const FLayoutFaceRule& Candidate)
						{
							return FLayoutDirectionUtils::RotateYaw(
								Candidate.Direction,
								YawRotationSteps) == WorldDirection;
						});
				if (RootFaceRule != nullptr)
				{
					OutFaceRule = *RootFaceRule;
					OutFaceRule.Direction = WorldDirection;
					return true;
				}
			}
		}

		const ELayoutFaceDirection AuthoredDirection =
			FLayoutDirectionUtils::RotateYaw(WorldDirection, -YawRotationSteps);
		const FLayoutFaceRule* FaceRule = ModuleSnapshot.EffectiveFaceRules.FindRule(AuthoredDirection);
		if (FaceRule == nullptr)
		{
			return false;
		}

		OutFaceRule = *FaceRule;
		OutFaceRule.Direction = WorldDirection;
		return true;
	}

	int32 ScoreParentModuleCatalogSupportForDirectContactCapability(
		const FLayoutModuleCatalog& ParentModuleCatalog,
		const ELayoutCellIntent ParentIntent,
		const ELayoutFaceDirection ParentFaceDirection,
		const FGameplayTag& ChildConnectionTag,
		const FGameplayTagContainer& ChildAllowedConnectionTags,
		const FGameplayTagContainer& ChildTraversalChannels,
		const bool bRequiresFilledUpperNeighbor,
		FLayoutId* OutParentModuleSnapshotId = nullptr,
		int32* OutParentYawRotationSteps = nullptr)
	{
		if (OutParentModuleSnapshotId != nullptr)
		{
			*OutParentModuleSnapshotId = NAME_None;
		}
		if (OutParentYawRotationSteps != nullptr)
		{
			*OutParentYawRotationSteps = 0;
		}
		int32 BestScore = 0;
		FLayoutId BestModuleSnapshotId;
		int32 BestYawRotationSteps = 0;
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ParentModuleCatalog.Modules)
		{
			if (!ModuleSnapshot.SupportsIntent(ParentIntent))
			{
				continue;
			}

			TArray<int32> AllowedYawRotationSteps = ModuleSnapshot.AllowedYawRotationSteps;
			if (AllowedYawRotationSteps.IsEmpty())
			{
				AllowedYawRotationSteps.Add(0);
			}

			for (const int32 YawRotationSteps : AllowedYawRotationSteps)
			{
				FLayoutFaceRule ParentFaceRule;
				if (!TryGetSnapshotWorldFaceRule(
					ModuleSnapshot,
					ParentFaceDirection,
					YawRotationSteps,
					ParentFaceRule))
				{
					continue;
				}

				if (!IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(ParentFaceRule))
				{
					continue;
				}

				if (!AreCommittedConnectionTagsCompatible(
					ParentFaceRule.GetEffectiveConnectionTag(),
					ParentFaceRule.GetEffectiveAllowedConnectionTags(),
					ChildConnectionTag,
					ChildAllowedConnectionTags))
				{
					continue;
				}

				if (!ChildTraversalChannels.IsEmpty()
					&& !ParentFaceRule.ConnectedTraversalChannels.HasAnyExact(ChildTraversalChannels))
				{
					continue;
				}

				if (bRequiresFilledUpperNeighbor)
				{
					FLayoutFaceRule UpperFaceRule;
					if (!TryGetSnapshotWorldFaceRule(
						ModuleSnapshot,
						ELayoutFaceDirection::PosZ,
						YawRotationSteps,
						UpperFaceRule)
						|| !IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(UpperFaceRule))
					{
						continue;
					}
				}

				int32 SupportScore = 1;
				static const ELayoutFaceDirection HorizontalDirections[] =
				{
					ELayoutFaceDirection::PosX,
					ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY,
					ELayoutFaceDirection::NegY
				};
				for (const ELayoutFaceDirection HorizontalDirection : HorizontalDirections)
				{
					if (HorizontalDirection == ParentFaceDirection)
					{
						continue;
					}

					FLayoutFaceRule LateralFaceRule;
					if (!TryGetSnapshotWorldFaceRule(
						ModuleSnapshot,
						HorizontalDirection,
						YawRotationSteps,
						LateralFaceRule)
						|| !IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(LateralFaceRule))
					{
						continue;
					}

					const bool bSharesTraversal =
						ChildTraversalChannels.IsEmpty()
							? !LateralFaceRule.ConnectedTraversalChannels.IsEmpty()
							: LateralFaceRule.ConnectedTraversalChannels.HasAnyExact(ChildTraversalChannels);
					if (bSharesTraversal)
					{
						SupportScore += 2;
					}
				}

				const bool bBetterSupport = SupportScore > BestScore;
				const bool bStableTie = SupportScore == BestScore
					&& (BestModuleSnapshotId == NAME_None
						|| ModuleSnapshot.SnapshotId.LexicalLess(BestModuleSnapshotId)
						|| (ModuleSnapshot.SnapshotId == BestModuleSnapshotId
							&& YawRotationSteps < BestYawRotationSteps));
				if (bBetterSupport || bStableTie)
				{
					BestScore = SupportScore;
					BestModuleSnapshotId = ModuleSnapshot.SnapshotId;
					BestYawRotationSteps = YawRotationSteps;
				}
			}
		}

		if (OutParentModuleSnapshotId != nullptr)
		{
			*OutParentModuleSnapshotId = BestModuleSnapshotId;
		}
		if (OutParentYawRotationSteps != nullptr)
		{
			*OutParentYawRotationSteps = BestYawRotationSteps;
		}
		return BestScore;
	}

	void AccumulatePropagationStats(FLayoutSolverPropagationStats& Target, const FLayoutSolverPropagationStats& Source)
	{
		Target.PropagationRunCount += Source.PropagationRunCount;
		Target.PropagationPassCount += Source.PropagationPassCount;
		Target.ArcQueuePopCount += Source.ArcQueuePopCount;
		Target.SupportCheckCount += Source.SupportCheckCount;
		Target.CandidateRemovalCount += Source.CandidateRemovalCount;
		Target.FailedCellCount += Source.FailedCellCount;
		Target.BacktrackCount += Source.BacktrackCount;
		Target.CandidateAttemptCount += Source.CandidateAttemptCount;
		Target.HardZoneFeatureDemandCount += Source.HardZoneFeatureDemandCount;
		Target.HardZoneFeatureProviderRootCount += Source.HardZoneFeatureProviderRootCount;
		Target.HardZoneFeatureCountBoundPruneCount += Source.HardZoneFeatureCountBoundPruneCount;
		Target.HardZoneFeatureLateAuditFailureCount += Source.HardZoneFeatureLateAuditFailureCount;
		Target.PropagationSeconds += Source.PropagationSeconds;
	}

	struct FScheduledChildSourceInfo
	{
		FName ContentEntryId;
		FString ParentRegionDebugPath;
		FLayoutId ChildProfileSnapshotId;
		bool bOptional = false;
		bool bResolved = false;
	};


	void SortCommittedEndpointAnchors(TArray<FLayoutCommittedEndpointAnchor>& Anchors);

	void SortCommittedTraversalAnchors(TArray<FLayoutCommittedTraversalAnchor>& Anchors)
	{
		Anchors.Sort([](const FLayoutCommittedTraversalAnchor& Left, const FLayoutCommittedTraversalAnchor& Right)
		{
			if (Left.Cell != Right.Cell)
			{
				return Left.Cell.X != Right.Cell.X ? Left.Cell.X < Right.Cell.X
					: (Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y : Left.Cell.Z < Right.Cell.Z);
			}

			return Left.TraversalChannel.ToString() < Right.TraversalChannel.ToString();
		});
	}

	void SortPartitionSeamRecords(TArray<FLayoutPartitionSeamRecord>& Seams)
	{
		Seams.Sort([](const FLayoutPartitionSeamRecord& Left, const FLayoutPartitionSeamRecord& Right)
		{
			if (Left.SeamId != Right.SeamId)
			{
				return Left.SeamId.LexicalLess(Right.SeamId);
			}

			if (Left.OwnerStartCell != Right.OwnerStartCell)
			{
				return Left.OwnerStartCell.X != Right.OwnerStartCell.X ? Left.OwnerStartCell.X < Right.OwnerStartCell.X
					: (Left.OwnerStartCell.Y != Right.OwnerStartCell.Y ? Left.OwnerStartCell.Y < Right.OwnerStartCell.Y : Left.OwnerStartCell.Z < Right.OwnerStartCell.Z);
			}

			return Left.PassiveRegionDebugPath < Right.PassiveRegionDebugPath;
		});
	}

	void SortAutomaticChildPlacementForDeterminism(FAutomaticChildPlacement& Placement)
	{
		Placement.ParentTranslatedPlannedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
		{
			if (Left.Cell != Right.Cell)
			{
				return Left.Cell.X != Right.Cell.X ? Left.Cell.X < Right.Cell.X
					: (Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y : Left.Cell.Z < Right.Cell.Z);
			}

			return static_cast<int32>(Left.Intent) < static_cast<int32>(Right.Intent);
		});

		Placement.ParentPlanReservedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
		});

		auto SortBoundaryPoints = [](TArray<FLayoutSolveBoundaryPoint>& BoundaryPoints)
		{
			BoundaryPoints.Sort([](const FLayoutSolveBoundaryPoint& Left, const FLayoutSolveBoundaryPoint& Right)
			{
				if (Left.TerrainAscentFrontierId != Right.TerrainAscentFrontierId)
				{
					return Left.TerrainAscentFrontierId < Right.TerrainAscentFrontierId;
				}
				if (Left.LocalCell != Right.LocalCell)
				{
					return Left.LocalCell.X != Right.LocalCell.X ? Left.LocalCell.X < Right.LocalCell.X
						: (Left.LocalCell.Y != Right.LocalCell.Y ? Left.LocalCell.Y < Right.LocalCell.Y : Left.LocalCell.Z < Right.LocalCell.Z);
				}

				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			});
		};
		SortBoundaryPoints(Placement.ParentTranslatedBoundaryPoints);
		SortBoundaryPoints(Placement.ParentTranslatedFutureTerraceBoundaryPoints);

		Placement.ParentCommittedEntryCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
		});

		SortCommittedTraversalAnchors(Placement.ParentCommittedTraversalAnchors);

		Placement.DirectChildCommitment.EndpointCommitments.Sort([](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
		{
			if (Left.LocalCell != Right.LocalCell)
			{
				return Left.LocalCell.X != Right.LocalCell.X ? Left.LocalCell.X < Right.LocalCell.X
					: (Left.LocalCell.Y != Right.LocalCell.Y ? Left.LocalCell.Y < Right.LocalCell.Y : Left.LocalCell.Z < Right.LocalCell.Z);
			}

			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}

			return Left.ConnectionTag.ToString() < Right.ConnectionTag.ToString();
		});

		for (FLayoutNegotiatedLevelCellSet& LevelCells : Placement.NegotiatedResponsibilityContract.ReplacementVolumeByLevel)
		{
			LevelCells.Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return Left.X != Right.X ? Left.X < Right.X
					: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
			});
		}
		Placement.NegotiatedResponsibilityContract.ReplacementVolumeByLevel.Sort([](const FLayoutNegotiatedLevelCellSet& Left, const FLayoutNegotiatedLevelCellSet& Right)
		{
			return Left.Level < Right.Level;
		});

		for (FLayoutNegotiatedLevelCellSet& LevelCells : Placement.NegotiatedResponsibilityContract.RetainedParentShellCellsByLevel)
		{
			LevelCells.Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return Left.X != Right.X ? Left.X < Right.X
					: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
			});
		}
		Placement.NegotiatedResponsibilityContract.RetainedParentShellCellsByLevel.Sort([](const FLayoutNegotiatedLevelCellSet& Left, const FLayoutNegotiatedLevelCellSet& Right)
		{
			return Left.Level < Right.Level;
		});

		for (FLayoutNegotiatedLevelCellSet& LevelCells : Placement.NegotiatedResponsibilityContract.ProofOnlyHostAscentParentShellCellsByLevel)
		{
			LevelCells.Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return Left.X != Right.X ? Left.X < Right.X
					: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
			});
		}
		Placement.NegotiatedResponsibilityContract.ProofOnlyHostAscentParentShellCellsByLevel.Sort([](const FLayoutNegotiatedLevelCellSet& Left, const FLayoutNegotiatedLevelCellSet& Right)
		{
			return Left.Level < Right.Level;
		});

		for (FLayoutNegotiatedLevelInterfaceContract& InterfaceContract : Placement.NegotiatedResponsibilityContract.CommittedParentChildInterfacesByLevel)
		{
			SortCommittedEndpointAnchors(InterfaceContract.EndpointAnchors);
			SortCommittedTraversalAnchors(InterfaceContract.TraversalAnchors);
		}
		Placement.NegotiatedResponsibilityContract.CommittedParentChildInterfacesByLevel.Sort([](const FLayoutNegotiatedLevelInterfaceContract& Left, const FLayoutNegotiatedLevelInterfaceContract& Right)
		{
			return Left.Level < Right.Level;
		});

		for (FLayoutNegotiatedLevelSeamSet& SeamSet : Placement.NegotiatedResponsibilityContract.CommittedSiblingInterfacesByLevel)
		{
			SortPartitionSeamRecords(SeamSet.Seams);
		}
		Placement.NegotiatedResponsibilityContract.CommittedSiblingInterfacesByLevel.Sort([](const FLayoutNegotiatedLevelSeamSet& Left, const FLayoutNegotiatedLevelSeamSet& Right)
		{
			return Left.Level < Right.Level;
		});

		Placement.ChildVerticalAccessLocalCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
		});

		Placement.NegotiatedResponsibilityContract.RequiredChildInternalVerticalRouteCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
		});
		Placement.NegotiatedResponsibilityContract.CountedParentVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.X != Right.X ? Left.X < Right.X
				: (Left.Y != Right.Y ? Left.Y < Right.Y : Left.Z < Right.Z);
		});
		Placement.NegotiatedResponsibilityContract.CountedChildProviderRegionDebugPaths.Sort();

		Placement.SharedParentChildFaces.Sort([](const FSharedParentChildFace& Left, const FSharedParentChildFace& Right)
		{
			if (Left.ParentCell != Right.ParentCell)
			{
				return Left.ParentCell.X != Right.ParentCell.X ? Left.ParentCell.X < Right.ParentCell.X
					: (Left.ParentCell.Y != Right.ParentCell.Y ? Left.ParentCell.Y < Right.ParentCell.Y : Left.ParentCell.Z < Right.ParentCell.Z);
			}

			if (Left.ChildLocalCell != Right.ChildLocalCell)
			{
				return Left.ChildLocalCell.X != Right.ChildLocalCell.X ? Left.ChildLocalCell.X < Right.ChildLocalCell.X
					: (Left.ChildLocalCell.Y != Right.ChildLocalCell.Y ? Left.ChildLocalCell.Y < Right.ChildLocalCell.Y : Left.ChildLocalCell.Z < Right.ChildLocalCell.Z);
			}

			if (Left.FaceDirection != Right.FaceDirection)
			{
				return static_cast<int32>(Left.FaceDirection) < static_cast<int32>(Right.FaceDirection);
			}
			return Left.InterfaceFamily.ToString() < Right.InterfaceFamily.ToString();
		});

		Placement.DirectChildCommitment.NegotiatedResponsibilityContract =
			Placement.NegotiatedResponsibilityContract;
		Placement.ChildRequest.NegotiatedChildResponsibilityContracts = {Placement.NegotiatedResponsibilityContract};
	}

	void SortAutomaticChildPlacementsForDeterminism(TArray<FAutomaticChildPlacement>& Placements)
	{
		for (FAutomaticChildPlacement& Placement : Placements)
		{
			SortAutomaticChildPlacementForDeterminism(Placement);
		}

		Placements.Sort([](const FAutomaticChildPlacement& Left, const FAutomaticChildPlacement& Right)
		{
			if (Left.ChildRegionDebugPath != Right.ChildRegionDebugPath)
			{
				return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
			}

			if (Left.EntryId != Right.EntryId)
			{
				return Left.EntryId.LexicalLess(Right.EntryId);
			}

			return Left.RegionCellOffset.X != Right.RegionCellOffset.X ? Left.RegionCellOffset.X < Right.RegionCellOffset.X
				: (Left.RegionCellOffset.Y != Right.RegionCellOffset.Y ? Left.RegionCellOffset.Y < Right.RegionCellOffset.Y : Left.RegionCellOffset.Z < Right.RegionCellOffset.Z);
		});
	}


	struct FRecursiveTreeSolvePerfMetrics
	{
		double StartSeconds = FPlatformTime::Seconds();
		double RootSolveSeconds = 0.0;
		double ChildSchedulingSeconds = 0.0;
		int32 AttemptCount = 0;
		int32 RootSolveSuccessCount = 0;
		int32 RootSolveFailureCount = 0;
		int32 DuplicateRootPlanSkipCount = 0;
		int32 ChildScheduleSuccessCount = 0;
		int32 ChildScheduleFailureCount = 0;
	};

	FLayoutProofRecord MakeSnapshotProofRecord(
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

	FLayoutValidationAssertionRecord MakeSnapshotAssertionRecord(
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

	FString TagsToStableKey(const FGameplayTagContainer& Tags)
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

	FString DescribeNegotiatedHostVerticalAccessResponsibility(
		const ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility);

	int32 CountTotalNegotiatedHostProviders(
		const FLayoutNegotiatedChildResponsibilityContract& Contract)
	{
		switch (Contract.HostVerticalAccessResponsibility)
		{
		case ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned:
			return Contract.CountedParentProviderCount;
		case ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned:
			return Contract.CountedChildProviderRegionDebugPaths.Num();
		case ELayoutNegotiatedHostVerticalAccessResponsibility::Composed:
			// Composed ascent keeps the child as an ownership/proof participant,
			// but it still represents the same retained host provider groups that
			// the parent-side counted-provider set already describes.
			return Contract.CountedParentProviderCount;
		default:
			return Contract.CountedParentProviderCount
				+ Contract.CountedChildProviderRegionDebugPaths.Num();
		}
	}

	bool TryResolveNegotiatedEndpointAuthoredLevel(
		const FLayoutNegotiatedChildResponsibilityContract& Contract,
		const FLayoutCommittedEndpointAnchor& Anchor,
		int32& OutAuthoredLevel)
	{
		bool bFound = false;
		for (const FLayoutNegotiatedLevelInterfaceContract& LevelContract : Contract.CommittedParentChildInterfacesByLevel)
		{
			const bool bContainsAnchor = LevelContract.EndpointAnchors.ContainsByPredicate(
				[&Anchor](const FLayoutCommittedEndpointAnchor& Candidate)
				{
					return (!Anchor.CommitmentId.IsNone() && Candidate.CommitmentId == Anchor.CommitmentId)
						|| (Candidate.LocalCell == Anchor.LocalCell
							&& Candidate.FaceDirection == Anchor.FaceDirection);
				});
			if (!bContainsAnchor)
			{
				continue;
			}
			if (bFound && OutAuthoredLevel != LevelContract.Level)
			{
				return false;
			}
			OutAuthoredLevel = LevelContract.Level;
			bFound = true;
		}
		return bFound;
	}

	bool ValidateNegotiatedChildResponsibilityContract(
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

			// ParentOwned child contracts prove that the child is not taking host
			// vertical-access responsibility. Placement-backed search may refresh this
			// per-child contract before the derived parent plan settles the final
			// counted parent provider set; that global count is validated from the
			// committed ownership summary after reservations are applied.
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
			const bool bSatisfiesHostProviderCount =
				Contract.bRequiresExactHostProviderCount
					? TotalNegotiatedHostProviders == Contract.RequiredHostProviderCount
					: TotalNegotiatedHostProviders >= Contract.RequiredHostProviderCount;
			if (!bSatisfiesHostProviderCount)
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

		int32 IngressAuthoredLevel = INDEX_NONE;
		int32 EgressAuthoredLevel = INDEX_NONE;
		if (!TryResolveNegotiatedEndpointAuthoredLevel(
				Contract,
				Contract.RequiredHostIngressAnchor,
				IngressAuthoredLevel)
			|| !TryResolveNegotiatedEndpointAuthoredLevel(
				Contract,
				Contract.RequiredHostEgressAnchor,
				EgressAuthoredLevel))
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiated child responsibility contract for child region '%s' does not carry both required host anchors in its authored-level interface sets."),
				*Contract.ChildRegionDebugPath);
			return false;
		}
		if (IngressAuthoredLevel == EgressAuthoredLevel)
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiated child responsibility contract for child region '%s' committed ingress and egress anchors on the same level (authored level %d)."),
				*Contract.ChildRegionDebugPath,
				IngressAuthoredLevel);
			return false;
		}

		const int32 LowerLevel = FMath::Min(IngressAuthoredLevel, EgressAuthoredLevel);
		const int32 UpperLevel = FMath::Max(IngressAuthoredLevel, EgressAuthoredLevel);
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
			const FLayoutNegotiatedLevelCellSet* ReplacementLevel = Contract.ReplacementVolumeByLevel.FindByPredicate(
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

	FString BuildNegotiatedChildResponsibilityContractMemoKey(
		const FLayoutNegotiatedChildResponsibilityContract& Contract)
	{
		auto BuildLevelCellSetParts = [](const TArray<FLayoutNegotiatedLevelCellSet>& LevelSets) -> FString
		{
			TArray<FString> LevelParts;
			LevelParts.Reserve(LevelSets.Num());
			for (const FLayoutNegotiatedLevelCellSet& LevelSet : LevelSets)
			{
				TArray<FString> CellParts;
				CellParts.Reserve(LevelSet.Cells.Num());
				for (const FIntVector& Cell : LevelSet.Cells)
				{
					CellParts.Add(Cell.ToString());
				}
				CellParts.Sort();
				LevelParts.Add(FString::Printf(
					TEXT("%d:%s"),
					LevelSet.Level,
					CellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(CellParts, TEXT(","))));
			}
			LevelParts.Sort();
			return LevelParts.IsEmpty() ? TEXT("<none>") : FString::Join(LevelParts, TEXT(";"));
		};

		auto BuildInterfaceParts = [](const TArray<FLayoutNegotiatedLevelInterfaceContract>& InterfaceContracts) -> FString
		{
			TArray<FString> LevelParts;
			LevelParts.Reserve(InterfaceContracts.Num());
			for (const FLayoutNegotiatedLevelInterfaceContract& InterfaceContract : InterfaceContracts)
			{
				TArray<FString> EndpointParts;
				EndpointParts.Reserve(InterfaceContract.EndpointAnchors.Num());
				for (const FLayoutCommittedEndpointAnchor& Anchor : InterfaceContract.EndpointAnchors)
				{
					EndpointParts.Add(FString::Printf(
						TEXT("%s:%s:%d:%s:%s:%s:%d"),
						Anchor.CommitmentId == NAME_None ? TEXT("<none>") : *Anchor.CommitmentId.ToString(),
						*Anchor.LocalCell.ToString(),
						static_cast<int32>(Anchor.FaceDirection),
						Anchor.ConnectionTag.IsValid() ? *Anchor.ConnectionTag.ToString() : TEXT("<none>"),
						*TagsToStableKey(Anchor.AllowedConnectionTags),
						*TagsToStableKey(Anchor.TraversalChannels),
						Anchor.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
				}
				EndpointParts.Sort();

				TArray<FString> TraversalParts;
				TraversalParts.Reserve(InterfaceContract.TraversalAnchors.Num());
				for (const FLayoutCommittedTraversalAnchor& Anchor : InterfaceContract.TraversalAnchors)
				{
					TraversalParts.Add(FString::Printf(
						TEXT("%s:%s"),
						*Anchor.Cell.ToString(),
						Anchor.TraversalChannel.IsValid() ? *Anchor.TraversalChannel.ToString() : TEXT("<none>")));
				}
				TraversalParts.Sort();

				LevelParts.Add(FString::Printf(
					TEXT("%d:Endpoints=%s|Traversal=%s"),
					InterfaceContract.Level,
					EndpointParts.IsEmpty() ? TEXT("<none>") : *FString::Join(EndpointParts, TEXT(",")),
					TraversalParts.IsEmpty() ? TEXT("<none>") : *FString::Join(TraversalParts, TEXT(","))));
			}
			LevelParts.Sort();
			return LevelParts.IsEmpty() ? TEXT("<none>") : FString::Join(LevelParts, TEXT(";"));
		};

		auto BuildSeamParts = [](const TArray<FLayoutNegotiatedLevelSeamSet>& SeamSets) -> FString
		{
			TArray<FString> LevelParts;
			LevelParts.Reserve(SeamSets.Num());
			for (const FLayoutNegotiatedLevelSeamSet& SeamSet : SeamSets)
			{
				TArray<FString> SeamParts;
				SeamParts.Reserve(SeamSet.Seams.Num());
				for (const FLayoutPartitionSeamRecord& Seam : SeamSet.Seams)
				{
					SeamParts.Add(FString::Printf(
						TEXT("%s|%s|%s|%s|%s|%s"),
						*Seam.OwnerRegionDebugPath,
						*Seam.PassiveRegionDebugPath,
						Seam.InterfaceFamily.IsValid() ? *Seam.InterfaceFamily.ToString() : TEXT("<none>"),
						*Seam.OwnerStartCell.ToString(),
						*Seam.PassiveStartCell.ToString(),
						*Seam.OwnerEndCell.ToString()));
				}
				SeamParts.Sort();
				LevelParts.Add(FString::Printf(
					TEXT("%d:%s"),
					SeamSet.Level,
					SeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(SeamParts, TEXT(","))));
			}
			LevelParts.Sort();
			return LevelParts.IsEmpty() ? TEXT("<none>") : FString::Join(LevelParts, TEXT(";"));
		};

		TArray<FString> RouteCellParts;
		RouteCellParts.Reserve(Contract.RequiredChildInternalVerticalRouteCells.Num());
		for (const FIntVector& RouteCell : Contract.RequiredChildInternalVerticalRouteCells)
		{
			RouteCellParts.Add(RouteCell.ToString());
		}
		RouteCellParts.Sort();

		TArray<FString> SpanLevelParts;
		SpanLevelParts.Reserve(Contract.RequiredChildInternalVerticalSpanLevels.Num());
		for (const int32 Level : Contract.RequiredChildInternalVerticalSpanLevels)
		{
			SpanLevelParts.Add(FString::FromInt(Level));
		}
		SpanLevelParts.Sort();

		return FString::Printf(
			TEXT("Replacement=%s|RetainedShell=%s|Interfaces=%s|SiblingSeams=%s|HostVA=%s|RequiredProviders=%d|CountedParentProviders=%d|CountedParentCells=%s|CountedChildProviders=%s|Ingress=%s|Egress=%s|AnchorPair=%s|SpanLevels=%s|Route=%s"),
			*BuildLevelCellSetParts(Contract.ReplacementVolumeByLevel),
			*BuildLevelCellSetParts(Contract.RetainedParentShellCellsByLevel),
			*BuildInterfaceParts(Contract.CommittedParentChildInterfacesByLevel),
			*BuildSeamParts(Contract.CommittedSiblingInterfacesByLevel),
			*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
			Contract.RequiredHostProviderCount,
			Contract.CountedParentProviderCount,
			Contract.CountedParentVerticalAccessCells.IsEmpty()
				? TEXT("<none>")
				: *FString::JoinBy(Contract.CountedParentVerticalAccessCells, TEXT(","), [](const FIntVector& Cell)
				{
					return Cell.ToString();
				}),
			Contract.CountedChildProviderRegionDebugPaths.IsEmpty()
				? TEXT("<none>")
				: *FString::Join(Contract.CountedChildProviderRegionDebugPaths, TEXT(",")),
			Contract.bHasRequiredHostIngressAnchor ? *Contract.RequiredHostIngressAnchor.CommitmentId.ToString() : TEXT("<none>"),
			Contract.bHasRequiredHostEgressAnchor ? *Contract.RequiredHostEgressAnchor.CommitmentId.ToString() : TEXT("<none>"),
			Contract.RequiredChildGenerallyConnectableAnchorPairId.IsNone()
				? TEXT("<none>")
				: *Contract.RequiredChildGenerallyConnectableAnchorPairId.ToString(),
			SpanLevelParts.IsEmpty()
				? TEXT("<none>")
				: *FString::Join(SpanLevelParts, TEXT(",")),
			RouteCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(RouteCellParts, TEXT(",")));
	}

	FString IndentMultiline(const FString& Text, const FString& Prefix)
	{
		if (Text.IsEmpty())
		{
			return Prefix + TEXT("<none>");
		}

		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, false);
		if (Lines.IsEmpty())
		{
			return Prefix + Text;
		}

		for (FString& Line : Lines)
		{
			Line = Prefix + Line;
		}

		return FString::Join(Lines, TEXT("\n"));
	}

	FString DescribeChildPlacementContext(
		const FLayoutRegionSolveRequest& RootRequest,
		const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
	{
		const FString ChildProfileName = EntrySnapshot.ChildProfileSnapshotId.IsNone()
			? FString(TEXT("<none>"))
			: EntrySnapshot.ChildProfileSnapshotId.ToString();
		const FString ChildContentSetName = EntrySnapshot.ChildContentSetSnapshotId.IsNone()
			? FString(TEXT("<none>"))
			: EntrySnapshot.ChildContentSetSnapshotId.ToString();
		return FString::Printf(
			TEXT("Parent Region: %s\nParent ContentSet: %s\nChild Entry: %s\nChild Profile: %s\nChild ContentSet: %s\nPlacement Zone: %s\nLevel Placement Policy: %s\nSpecific Level: %d"),
			*RootRequest.RegionDebugPath,
			*RootRequest.ContentSetSnapshot.DebugName.ToString(),
			*EntrySnapshot.EntryId.ToString(),
			*ChildProfileName,
			*ChildContentSetName,
			*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(EntrySnapshot.ChildPlacementZone)),
			*StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(static_cast<int64>(EntrySnapshot.ChildLevelPlacementPolicy)),
			EntrySnapshot.ChildSpecificLevel);
	}

	FLayoutRegionSolveRequest BuildChildRequestFromTemplate(
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
		Request.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			TEXT("RegionRequest.SnapshotContractInitialized"),
			ELayoutValidationAssertionKind::SnapshotContractInitialized,
			Request.EffectiveSnapshotId != NAME_None,
			{Request.ProfileSnapshot.SnapshotId, Request.ContentSetSnapshot.SnapshotId, Request.ModuleCatalog.SnapshotId},
			Request.EffectiveSnapshotId != NAME_None
				? FString()
				: TEXT("Region solve request failed to initialize an effective snapshot id from the compiled child request template.")));
		return Request;
	}

	FString BuildRejectedChildOffsetMessage(
		const FIntVector& CandidateOffset,
		const TArray<FIntVector>& CandidateParentContactCells,
		const FParentProbeEvaluation& ParentProbeEvaluation)
	{
		const FString ContactsString = CandidateParentContactCells.IsEmpty()
			? TEXT("<none>")
			: FString::JoinBy(CandidateParentContactCells, TEXT(", "), [](const FIntVector& Cell)
			{
				return Cell.ToString();
			});

		return FString::Printf(
			TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: %s\nFailed During: %s\nRejection Detail:\n%s"),
			*CandidateOffset.ToString(),
			*ContactsString,
			ParentProbeEvaluation.bFailedDuringChildProbe ? TEXT("Child Probe") : TEXT("Parent Probe"),
			*IndentMultiline(
				ParentProbeEvaluation.FailureReason.IsEmpty()
					? TEXT("No additional failure detail was reported.")
					: ParentProbeEvaluation.FailureReason,
				TEXT("  ")));
	}

	bool IsCanonicalChildPlacementChoiceLess(
		const FCanonicalChildPlacementChoice& Left,
		const FCanonicalChildPlacementChoice& Right)
	{
		if (Left.Offset.Z != Right.Offset.Z)
		{
			return Left.Offset.Z < Right.Offset.Z;
		}
		if (Left.Offset.Y != Right.Offset.Y)
		{
			return Left.Offset.Y < Right.Offset.Y;
		}
		if (Left.Offset.X != Right.Offset.X)
		{
			return Left.Offset.X < Right.Offset.X;
		}
		return Left.VariantIndex < Right.VariantIndex;
	}

	bool DoChildSeamCapabilitiesSupportSharedInterface(
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		const ELayoutFaceDirection FirstFaceDirection,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		const ELayoutFaceDirection SecondFaceDirection)
	{
		for (const FLayoutChildCapabilitySeam& FirstCapability : FirstEnvelope.SeamCapabilities)
		{
			if (!FirstCapability.InterfaceFamily.IsValid() || FirstCapability.FaceDirection != FirstFaceDirection)
			{
				continue;
			}

			for (const FLayoutChildCapabilitySeam& SecondCapability : SecondEnvelope.SeamCapabilities)
			{
				if (SecondCapability.InterfaceFamily != FirstCapability.InterfaceFamily
					|| SecondCapability.FaceDirection != SecondFaceDirection)
				{
					continue;
				}

				const bool bFirstOwnsSecondAccepts = FirstCapability.bCanOwnSeam && SecondCapability.bCanAcceptSeam;
				const bool bSecondOwnsFirstAccepts = SecondCapability.bCanOwnSeam && FirstCapability.bCanAcceptSeam;
				if (bFirstOwnsSecondAccepts || bSecondOwnsFirstAccepts)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool TryResolveSharedSeamCapabilities(
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		const ELayoutFaceDirection FirstFaceDirection,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		const ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& RequiredInterfaceFamily,
		bool& bOutFirstOwnsSecondAccepts,
		bool& bOutSecondOwnsFirstAccepts)
	{
		bOutFirstOwnsSecondAccepts = false;
		bOutSecondOwnsFirstAccepts = false;
		bool bFoundPair = false;
		for (const FLayoutChildCapabilitySeam& FirstCapability : FirstEnvelope.SeamCapabilities)
		{
			if (FirstCapability.InterfaceFamily != RequiredInterfaceFamily
				|| FirstCapability.FaceDirection != FirstFaceDirection)
			{
				continue;
			}
			for (const FLayoutChildCapabilitySeam& SecondCapability : SecondEnvelope.SeamCapabilities)
			{
				if (SecondCapability.InterfaceFamily != RequiredInterfaceFamily
					|| SecondCapability.FaceDirection != SecondFaceDirection)
				{
					continue;
				}
				const bool bFirstOwnsSecondAccepts =
					FirstCapability.bCanOwnSeam && SecondCapability.bCanAcceptSeam;
				const bool bSecondOwnsFirstAccepts =
					SecondCapability.bCanOwnSeam && FirstCapability.bCanAcceptSeam;
				bFoundPair |= bFirstOwnsSecondAccepts || bSecondOwnsFirstAccepts;
				bOutFirstOwnsSecondAccepts |= bFirstOwnsSecondAccepts;
				bOutSecondOwnsFirstAccepts |= bSecondOwnsFirstAccepts;
			}
		}
		return bFoundPair;
	}

	bool DoChildSeamCapabilitiesSupportSpecificSharedInterface(
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		const ELayoutFaceDirection FirstFaceDirection,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		const ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& RequiredInterfaceFamily)
	{
		bool bFirstOwnsSecondAccepts = false;
		bool bSecondOwnsFirstAccepts = false;
		return RequiredInterfaceFamily.IsValid()
			&& TryResolveSharedSeamCapabilities(
				FirstEnvelope,
				FirstFaceDirection,
				SecondEnvelope,
				SecondFaceDirection,
				RequiredInterfaceFamily,
				bFirstOwnsSecondAccepts,
				bSecondOwnsFirstAccepts);
	}

	bool IsDoorLikeEndpointCapability(const FLayoutChildCapabilityEndpoint& Capability)
	{
		return Capability.ConnectionTag == LayoutGameplayTags::FaceEntry
			|| Capability.ConnectionTag == LayoutGameplayTags::FaceOpen
			|| Capability.AllowedConnectionTags.HasTagExact(LayoutGameplayTags::FaceEntry)
			|| Capability.AllowedConnectionTags.HasTagExact(LayoutGameplayTags::FaceOpen);
	}

	bool DoesEnvelopeExposeDoorEndpointCapability(
		const FLayoutChildCapabilityEnvelope& Envelope,
		const ELayoutFaceDirection FaceDirection)
	{
		return Envelope.EndpointCapabilities.ContainsByPredicate(
			[FaceDirection](const FLayoutChildCapabilityEndpoint& Capability)
			{
				return Capability.FaceDirection == FaceDirection
					&& IsDoorLikeEndpointCapability(Capability)
					&& !Capability.TraversalChannels.IsEmpty();
			});
	}

	bool DoChildEndpointCapabilitiesSupportImplicitParentOwnedDoorInterface(
		const FLayoutChildCapabilityEnvelope& ParentEnvelope,
		const ELayoutFaceDirection ParentFaceDirection,
		const FLayoutChildCapabilityEnvelope& ChildEnvelope,
		const ELayoutFaceDirection ChildFaceDirection)
	{
		return DoesEnvelopeExposeDoorEndpointCapability(ParentEnvelope, ParentFaceDirection)
			&& DoesEnvelopeExposeDoorEndpointCapability(ChildEnvelope, ChildFaceDirection);
	}

	bool DoesEnvelopeAcceptSpecificSharedInterface(
		const FLayoutChildCapabilityEnvelope& Envelope,
		const ELayoutFaceDirection FaceDirection,
		const FGameplayTag& RequiredInterfaceFamily)
	{
		return RequiredInterfaceFamily.IsValid()
			&& Envelope.SeamCapabilities.ContainsByPredicate(
				[&](const FLayoutChildCapabilitySeam& Capability)
				{
					return Capability.InterfaceFamily == RequiredInterfaceFamily
						&& Capability.FaceDirection == FaceDirection
						&& Capability.bCanAcceptSeam;
				});
	}

	bool TryChooseSpecificSharedInterfaceOwner(
		const FString& FirstRegionPath,
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		const ELayoutFaceDirection FirstFaceDirection,
		const FString& SecondRegionPath,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		const ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& RequiredInterfaceFamily,
		FString& OutOwnerRegionPath,
		FString& OutPassiveRegionPath)
	{
		OutOwnerRegionPath.Reset();
		OutPassiveRegionPath.Reset();
		bool bFirstOwnsSecondAccepts = false;
		bool bSecondOwnsFirstAccepts = false;
		if (!RequiredInterfaceFamily.IsValid()
			|| !TryResolveSharedSeamCapabilities(
				FirstEnvelope,
				FirstFaceDirection,
				SecondEnvelope,
				SecondFaceDirection,
				RequiredInterfaceFamily,
				bFirstOwnsSecondAccepts,
				bSecondOwnsFirstAccepts))
		{
			return false;
		}
		if (bFirstOwnsSecondAccepts && bSecondOwnsFirstAccepts)
		{
			// When both siblings can own the same shared interface, prefer the child currently
			// being evaluated by the recursive scheduler. Entry negotiation asks whether the new
			// child can realize its doorway through an already-placed sibling seam; choosing the
			// newly placed child here preserves that contract instead of forcing the older sibling
			// to own both sides of the shared door.
			OutOwnerRegionPath = FirstRegionPath;
			OutPassiveRegionPath = SecondRegionPath;
			return true;
		}

		if (bFirstOwnsSecondAccepts)
		{
			OutOwnerRegionPath = FirstRegionPath;
			OutPassiveRegionPath = SecondRegionPath;
			return true;
		}

		if (bSecondOwnsFirstAccepts)
		{
			OutOwnerRegionPath = SecondRegionPath;
			OutPassiveRegionPath = FirstRegionPath;
			return true;
		}

		return false;
	}

	bool TryResolveSharedSeamOwnershipWitness(
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		const ELayoutFaceDirection FirstFaceDirection,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		const ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& InterfaceFamily,
		bool& bOutFirstCanOwn,
		bool& bOutSecondCanOwn,
		FLayoutId& OutFirstOwnerCapabilityId,
		FLayoutId& OutSecondOwnerCapabilityId)
	{
		bOutFirstCanOwn = false;
		bOutSecondCanOwn = false;
		OutFirstOwnerCapabilityId = NAME_None;
		OutSecondOwnerCapabilityId = NAME_None;
		bool bFirstOwnsSecondAccepts = false;
		bool bSecondOwnsFirstAccepts = false;
		if (!TryResolveSharedSeamCapabilities(
			FirstEnvelope,
			FirstFaceDirection,
			SecondEnvelope,
			SecondFaceDirection,
			InterfaceFamily,
			bFirstOwnsSecondAccepts,
			bSecondOwnsFirstAccepts))
		{
			return false;
		}

		const auto FindParticipantCapabilityId = [](
			const FLayoutChildCapabilityEnvelope& Envelope,
			const ELayoutFaceDirection FaceDirection,
			const FGameplayTag& RequiredFamily) -> FLayoutId
		{
			FLayoutId BestId;
			for (const FLayoutChildCapabilitySeam& Capability : Envelope.SeamCapabilities)
			{
				if ((!Capability.bCanOwnSeam && !Capability.bCanAcceptSeam)
					|| Capability.FaceDirection != FaceDirection
					|| Capability.InterfaceFamily != RequiredFamily)
				{
					continue;
				}
				if (BestId.IsNone() || Capability.CapabilityId.LexicalLess(BestId))
				{
					BestId = Capability.CapabilityId;
				}
			}
			return BestId;
		};

		bOutFirstCanOwn = bFirstOwnsSecondAccepts;
		bOutSecondCanOwn = bSecondOwnsFirstAccepts;
		OutFirstOwnerCapabilityId = FindParticipantCapabilityId(
			FirstEnvelope, FirstFaceDirection, InterfaceFamily);
		OutSecondOwnerCapabilityId = FindParticipantCapabilityId(
			SecondEnvelope, SecondFaceDirection, InterfaceFamily);
		return true;
	}

	int32 CountAttachedSeamSupportRun(
		const TSet<FIntVector>& PlannedCells,
		const FIntVector& AnchorCell,
		const ELayoutFaceDirection FaceDirection)
	{
		if (!PlannedCells.Contains(AnchorCell))
		{
			return 0;
		}
		const FIntVector RunDelta =
			FaceDirection == ELayoutFaceDirection::PosX || FaceDirection == ELayoutFaceDirection::NegX
				? FIntVector(0, 1, 0)
				: FIntVector(1, 0, 0);
		int32 RunLength = 1;
		for (FIntVector Cell = AnchorCell + RunDelta; PlannedCells.Contains(Cell); Cell += RunDelta)
		{
			++RunLength;
		}
		for (FIntVector Cell = AnchorCell - RunDelta; PlannedCells.Contains(Cell); Cell -= RunDelta)
		{
			++RunLength;
		}
		return RunLength;
	}

	bool TryChooseCommittedSeamRunOwner(
		const FString& ParentRegionDebugPath,
		const FString& FirstRegionDebugPath,
		const bool bFirstCanOwn,
		const int32 FirstSupportRunLength,
		const bool bFirstOwnsDoorAnchor,
		const FString& SecondRegionDebugPath,
		const bool bSecondCanOwn,
		const int32 SecondSupportRunLength,
		const bool bSecondOwnsDoorAnchor,
		FString& OutOwnerRegionDebugPath,
		FString& OutPassiveRegionDebugPath)
	{
		OutOwnerRegionDebugPath.Reset();
		OutPassiveRegionDebugPath.Reset();
		if (!bFirstCanOwn && !bSecondCanOwn)
		{
			return false;
		}

		bool bChooseFirst = bFirstCanOwn;
		if (bFirstOwnsDoorAnchor != bSecondOwnsDoorAnchor)
		{
			bChooseFirst = bFirstOwnsDoorAnchor ? bFirstCanOwn : !bSecondCanOwn;
		}
		else if (bFirstCanOwn != bSecondCanOwn)
		{
			bChooseFirst = bFirstCanOwn;
		}
		else if (FirstSupportRunLength != SecondSupportRunLength)
		{
			bChooseFirst = FirstSupportRunLength > SecondSupportRunLength;
		}
		else if ((FirstRegionDebugPath == ParentRegionDebugPath)
			!= (SecondRegionDebugPath == ParentRegionDebugPath))
		{
			bChooseFirst = FirstRegionDebugPath == ParentRegionDebugPath;
		}
		else
		{
			bChooseFirst = FirstRegionDebugPath < SecondRegionDebugPath;
		}

		OutOwnerRegionDebugPath = bChooseFirst ? FirstRegionDebugPath : SecondRegionDebugPath;
		OutPassiveRegionDebugPath = bChooseFirst ? SecondRegionDebugPath : FirstRegionDebugPath;
		return true;
	}

	bool TryGetSharedOverlapFaceDirections(
		const FIntVector& FirstCell,
		const FIntPoint& FirstFootprintSize,
		const FIntVector& SecondCell,
		const FIntPoint& SecondFootprintSize,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection);

	bool DoesPlacementContainSiblingCommittedDoorAgainstPeer(
		const FAutomaticChildPlacement& OwnerPlacement,
		const FAutomaticChildPlacement& PeerPlacement)
	{
		for (const FLayoutCommittedEndpointAnchor& Anchor : OwnerPlacement.DirectChildCommitment.EndpointCommitments)
		{
			const FIntVector OwnerWorldCell = MapChildResultCellToParent(
				Anchor.LocalCell,
				OwnerPlacement.RegionCellOffset,
				&OwnerPlacement.StageMapping);
			const FIntVector ExpectedAdjacentPeerWorldCell =
				OwnerWorldCell + FLayoutDirectionUtils::ToCellDelta(Anchor.FaceDirection);
			for (int32 PeerCellIndex = 0;
				PeerCellIndex < PeerPlacement.ChildRequest.PlannedCells.Num();
				++PeerCellIndex)
			{
				const FLayoutPlannedCell& PeerPlannedCell =
					PeerPlacement.ChildRequest.PlannedCells[PeerCellIndex];
				const FIntVector PeerWorldCell =
					PeerPlacement.ParentTranslatedPlannedCells.IsValidIndex(PeerCellIndex)
						? PeerPlacement.ParentTranslatedPlannedCells[PeerCellIndex].Cell
						: PeerPlannedCell.Cell + PeerPlacement.RegionCellOffset;
				if (PeerWorldCell == ExpectedAdjacentPeerWorldCell)
				{
					return true;
				}

				if (PeerWorldCell != OwnerWorldCell)
				{
					continue;
				}

				ELayoutFaceDirection OwnerOverlapDirection = ELayoutFaceDirection::PosX;
				ELayoutFaceDirection PeerOverlapDirection = ELayoutFaceDirection::NegX;
				if (TryGetSharedOverlapFaceDirections(
					Anchor.LocalCell,
					OwnerPlacement.ChildRequest.FootprintSize,
					PeerPlannedCell.Cell,
					PeerPlacement.ChildRequest.FootprintSize,
					OwnerOverlapDirection,
					PeerOverlapDirection)
					&& OwnerOverlapDirection == Anchor.FaceDirection)
				{
					return true;
				}
			}
		}

		return false;
	}

	int32 ScoreSharedSiblingSeamOpportunity(
		const TArray<FLayoutPlannedCell>& CandidateChildPlannedCells,
		const FIntVector& CandidateOffset,
		const FLayoutChildCapabilityEnvelope& CandidateEnvelope,
		const TArray<FAutomaticChildPlacement>& ExistingPlacements)
	{
		static const ELayoutFaceDirection HorizontalDirections[] =
		{
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY
		};

		TSet<FIntVector> CandidateTranslatedCells;
		CandidateTranslatedCells.Reserve(CandidateChildPlannedCells.Num());
		for (const FLayoutPlannedCell& ChildPlannedCell : CandidateChildPlannedCells)
		{
			CandidateTranslatedCells.Add(ChildPlannedCell.Cell + CandidateOffset);
		}

		int32 AdjacentEdgeCount = 0;
		int32 SharedSeamEdgeCount = 0;
		for (const FAutomaticChildPlacement& ExistingPlacement : ExistingPlacements)
		{
			const FLayoutChildCapabilityEnvelope& ExistingEnvelope = ExistingPlacement.ChildCapabilityEnvelope;
			TSet<FIntVector> ExistingTranslatedCells;
			ExistingTranslatedCells.Reserve(ExistingPlacement.ParentTranslatedPlannedCells.Num());
			for (const FLayoutPlannedCell& ExistingCell : ExistingPlacement.ParentTranslatedPlannedCells)
			{
				ExistingTranslatedCells.Add(ExistingCell.Cell);
			}

			for (const FIntVector& CandidateCell : CandidateTranslatedCells)
			{
				for (const ELayoutFaceDirection Direction : HorizontalDirections)
				{
					const FIntVector NeighborCell = CandidateCell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (!ExistingTranslatedCells.Contains(NeighborCell))
					{
						continue;
					}

					++AdjacentEdgeCount;
					if (DoChildSeamCapabilitiesSupportSharedInterface(
						CandidateEnvelope,
						Direction,
						ExistingEnvelope,
						FLayoutDirectionUtils::GetOpposite(Direction)))
					{
						++SharedSeamEdgeCount;
					}
				}
			}
		}

		return (AdjacentEdgeCount * 40) + (SharedSeamEdgeCount * 600);
	}

	int32 ScoreSharedParentChildSeamOpportunity(const TArray<FSharedParentChildFace>& SharedParentChildFaces)
	{
		TSet<uint32> UniqueSharedFaces;
		int32 LongestCertifiedRun = 0;
		int32 CertifiedDoorFaceCount = 0;
		for (const FSharedParentChildFace& SharedFace : SharedParentChildFaces)
		{
			if (SharedFace.ReciprocalDomainWitnessId.IsNone())
			{
				continue;
			}
			UniqueSharedFaces.Add(HashCombineFast(
				GetTypeHash(SharedFace.ParentCell),
				static_cast<uint32>(SharedFace.FaceDirection)));
			LongestCertifiedRun = FMath::Max(
				LongestCertifiedRun,
				FMath::Max(
					SharedFace.ParentOwnerSupportRunLength,
					SharedFace.ChildOwnerSupportRunLength));
			CertifiedDoorFaceCount +=
				SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
					? 1
					: 0;
		}

		// Cheap ordering and final scoring share this exact certificate-derived value.
		return (UniqueSharedFaces.Num() * 600)
			+ (LongestCertifiedRun * 100)
			+ (CertifiedDoorFaceCount * 300);
	}

	bool TryGetSharedOverlapFaceDirections(
		const FIntVector& CandidateLocalCell,
		const FIntPoint& CandidateFootprintSize,
		const FIntVector& ExistingLocalCell,
		const FIntPoint& ExistingFootprintSize,
		ELayoutFaceDirection& OutCandidateFaceDirection,
		ELayoutFaceDirection& OutExistingFaceDirection)
	{
		if (CandidateLocalCell.Y == ExistingLocalCell.Y)
		{
			if (CandidateLocalCell.X == CandidateFootprintSize.X - 1 && ExistingLocalCell.X == 0)
			{
				OutCandidateFaceDirection = ELayoutFaceDirection::PosX;
				OutExistingFaceDirection = ELayoutFaceDirection::NegX;
				return true;
			}

			if (CandidateLocalCell.X == 0 && ExistingLocalCell.X == ExistingFootprintSize.X - 1)
			{
				OutCandidateFaceDirection = ELayoutFaceDirection::NegX;
				OutExistingFaceDirection = ELayoutFaceDirection::PosX;
				return true;
			}
		}

		if (CandidateLocalCell.X == ExistingLocalCell.X)
		{
			if (CandidateLocalCell.Y == CandidateFootprintSize.Y - 1 && ExistingLocalCell.Y == 0)
			{
				OutCandidateFaceDirection = ELayoutFaceDirection::PosY;
				OutExistingFaceDirection = ELayoutFaceDirection::NegY;
				return true;
			}

			if (CandidateLocalCell.Y == 0 && ExistingLocalCell.Y == ExistingFootprintSize.Y - 1)
			{
				OutCandidateFaceDirection = ELayoutFaceDirection::NegY;
				OutExistingFaceDirection = ELayoutFaceDirection::PosY;
				return true;
			}
		}

		return false;
	}

	bool TryGetAdjacentSharedFaceDirections(
		const FIntVector& FirstWorldCell,
		const FIntVector& SecondWorldCell,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection)
	{
		if (FirstWorldCell.Z != SecondWorldCell.Z)
		{
			return false;
		}

		const FIntVector Delta = SecondWorldCell - FirstWorldCell;
		if (Delta == FIntVector(1, 0, 0))
		{
			OutFirstFaceDirection = ELayoutFaceDirection::PosX;
			OutSecondFaceDirection = ELayoutFaceDirection::NegX;
			return true;
		}
		if (Delta == FIntVector(-1, 0, 0))
		{
			OutFirstFaceDirection = ELayoutFaceDirection::NegX;
			OutSecondFaceDirection = ELayoutFaceDirection::PosX;
			return true;
		}
		if (Delta == FIntVector(0, 1, 0))
		{
			OutFirstFaceDirection = ELayoutFaceDirection::PosY;
			OutSecondFaceDirection = ELayoutFaceDirection::NegY;
			return true;
		}
		if (Delta == FIntVector(0, -1, 0))
		{
			OutFirstFaceDirection = ELayoutFaceDirection::NegY;
			OutSecondFaceDirection = ELayoutFaceDirection::PosY;
			return true;
		}

		return false;
	}

	void AppendParentOwnedSharedOverlapFaceDirections(
		const FIntVector& ChildLocalCell,
		const FIntPoint& ChildFootprintSize,
		const FIntVector& ParentCell,
		const FIntPoint& ParentFootprintSize,
		TArray<ELayoutFaceDirection>& OutFaceDirections)
	{
		// Parent/child shared shell ownership must work for interior recursive rooms
		// too, not only when the overlap happens on the root outer footprint. The
		// child perimeter face itself defines the shared wall direction.
		if (ChildLocalCell.X == 0)
		{
			OutFaceDirections.AddUnique(ELayoutFaceDirection::NegX);
		}

		if (ChildLocalCell.X == ChildFootprintSize.X - 1)
		{
			OutFaceDirections.AddUnique(ELayoutFaceDirection::PosX);
		}

		if (ChildLocalCell.Y == 0)
		{
			OutFaceDirections.AddUnique(ELayoutFaceDirection::NegY);
		}

		if (ChildLocalCell.Y == ChildFootprintSize.Y - 1)
		{
			OutFaceDirections.AddUnique(ELayoutFaceDirection::PosY);
		}
	}

	void SortSkippedOptionalChildPlacementsForDeterminism(
		TArray<FSkippedOptionalChildPlacement>& Placements)
	{
		Placements.Sort([](
			const FSkippedOptionalChildPlacement& Left,
			const FSkippedOptionalChildPlacement& Right)
		{
			return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
		});
	}

	bool IsCellOnFootprintPerimeter(const FIntVector& LocalCell, const FIntPoint& FootprintSize)
	{
		return LocalCell.X == 0
			|| LocalCell.Y == 0
			|| LocalCell.X == FootprintSize.X - 1
			|| LocalCell.Y == FootprintSize.Y - 1;
	}

	/** Returns whether one local cell exposes the requested horizontal footprint face. */
	bool DoesCellExposeFootprintPerimeterFace(
		const FIntVector& LocalCell,
		const FIntPoint& FootprintSize,
		const ELayoutFaceDirection FaceDirection)
	{
		switch (FaceDirection)
		{
		case ELayoutFaceDirection::NegX:
			return LocalCell.X == 0;
		case ELayoutFaceDirection::PosX:
			return LocalCell.X == FootprintSize.X - 1;
		case ELayoutFaceDirection::NegY:
			return LocalCell.Y == 0;
		case ELayoutFaceDirection::PosY:
			return LocalCell.Y == FootprintSize.Y - 1;
		default:
			return false;
		}
	}

	bool DoesChildCellSupportSharedBoundarySeam(
		const FLayoutPlannedCell& ChildPlannedCell,
		const FIntVector& CandidateOffset,
		const FIntPoint& ChildFootprintSize,
		const FIntPoint& ParentFootprintSize,
		const FLayoutChildCapabilityEnvelope& ParentEnvelope,
		const FLayoutChildCapabilityEnvelope& ChildEnvelope,
		TArray<FSharedParentChildFace>& OutSharedFaces,
		const TSet<FIntVector>* ParentPlannedCells = nullptr,
		const TSet<FIntVector>* ChildPlannedCells = nullptr,
		const FIntVector* ExplicitParentCell = nullptr)
	{
		OutSharedFaces.Reset();
		if (!IsCellOnFootprintPerimeter(ChildPlannedCell.Cell, ChildFootprintSize))
		{
			return false;
		}

		const FIntVector ParentCell = ExplicitParentCell != nullptr
			? *ExplicitParentCell
			: ChildPlannedCell.Cell + CandidateOffset;
		TArray<ELayoutFaceDirection> SharedFaceDirections;
		AppendParentOwnedSharedOverlapFaceDirections(
			ChildPlannedCell.Cell,
			ChildFootprintSize,
			ParentCell,
			ParentFootprintSize,
			SharedFaceDirections);
		if (SharedFaceDirections.IsEmpty())
		{
			return false;
		}

		for (const ELayoutFaceDirection FaceDirection : SharedFaceDirections)
		{
			TArray<FGameplayTag> RequiredInterfaceFamilies;
			if (ChildPlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				RequiredInterfaceFamilies.Add(LayoutGameplayTags::InterfacePartitionDoor);
			}
			else
			{
				RequiredInterfaceFamilies.Add(LayoutGameplayTags::InterfacePartitionSolid);
				RequiredInterfaceFamilies.Add(LayoutGameplayTags::InterfacePartitionDoor);
			}

			for (const FGameplayTag& InterfaceFamily : RequiredInterfaceFamilies)
			{
				bool bParentCanOwn = false;
				bool bChildCanOwn = false;
				FLayoutId ParentOwnerCapabilityId;
				FLayoutId ChildOwnerCapabilityId;
				const bool bSupportsExplicitInterface = TryResolveSharedSeamOwnershipWitness(
					ParentEnvelope,
					FaceDirection,
					ChildEnvelope,
					FaceDirection,
					InterfaceFamily,
					bParentCanOwn,
					bChildCanOwn,
					ParentOwnerCapabilityId,
					ChildOwnerCapabilityId);
				const bool bSupportsImplicitParentOwnedDoorInterface =
					InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
					&& ChildPlannedCell.Intent == ELayoutCellIntent::Entry
					&& DoChildEndpointCapabilitiesSupportImplicitParentOwnedDoorInterface(
						ParentEnvelope,
						FaceDirection,
						ChildEnvelope,
						FaceDirection);
				const bool bSupportsImplicitParentOwnedSolidInterface =
					InterfaceFamily == LayoutGameplayTags::InterfacePartitionSolid
					&& ChildPlannedCell.Intent == ELayoutCellIntent::Boundary
					&& ChildPlannedCell.Cell.Z > 0
					&& DoesEnvelopeAcceptSpecificSharedInterface(
						ChildEnvelope,
						FaceDirection,
						InterfaceFamily);
				if (!bSupportsExplicitInterface
					&& !bSupportsImplicitParentOwnedDoorInterface
					&& !bSupportsImplicitParentOwnedSolidInterface)
				{
					continue;
				}
				bParentCanOwn |= bSupportsImplicitParentOwnedDoorInterface
					|| bSupportsImplicitParentOwnedSolidInterface;

				TSet<FIntVector> SingleParentSupportCell;
				TSet<FIntVector> SingleChildSupportCell;
				SingleParentSupportCell.Add(ParentCell);
				SingleChildSupportCell.Add(ChildPlannedCell.Cell);
				const TSet<FIntVector>* const EffectiveParentPlannedCells =
					ParentPlannedCells != nullptr ? ParentPlannedCells : &SingleParentSupportCell;
				const TSet<FIntVector>* const EffectiveChildPlannedCells =
					ChildPlannedCells != nullptr ? ChildPlannedCells : &SingleChildSupportCell;

				FSharedParentChildFace& SharedFace = OutSharedFaces.AddDefaulted_GetRef();
				SharedFace.ParentCell = ParentCell;
				SharedFace.ChildLocalCell = ChildPlannedCell.Cell;
				SharedFace.FaceDirection = FaceDirection;
				SharedFace.InterfaceFamily = InterfaceFamily;
				SharedFace.bParentCanOwnSeam = bParentCanOwn;
				SharedFace.bChildCanOwnSeam = bChildCanOwn;
				SharedFace.ParentOwnerSupportRunLength = CountAttachedSeamSupportRun(
					*EffectiveParentPlannedCells, ParentCell, FaceDirection);
				SharedFace.ChildOwnerSupportRunLength = CountAttachedSeamSupportRun(
					*EffectiveChildPlannedCells, ChildPlannedCell.Cell, FaceDirection);
				SharedFace.ParentSeamCapabilityId = ParentOwnerCapabilityId;
				SharedFace.ChildSeamCapabilityId = ChildOwnerCapabilityId;
				break;
			}
		}

		return !OutSharedFaces.IsEmpty();
	}

	bool DoesParentCellKeepSameRegionSupportUnderReservation(
		const FIntVector& ParentCell,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedParentCells,
		TMap<FIntVector, bool>& InOutMemo)
	{
		if (const bool* ExistingResult = InOutMemo.Find(ParentCell))
		{
			return *ExistingResult;
		}

		if (!ParentPlannedCellSet.Contains(ParentCell)
			|| ReservedParentCells.Contains(ParentCell))
		{
			InOutMemo.Add(ParentCell, false);
			return false;
		}

		if (ParentCell.Z <= 0)
		{
			InOutMemo.Add(ParentCell, true);
			return true;
		}

		const FIntVector SupportingCell = ParentCell + FIntVector(0, 0, -1);
		const bool bSupported =
			DoesParentCellKeepSameRegionSupportUnderReservation(
				SupportingCell,
				ParentPlannedCellSet,
				ReservedParentCells,
				InOutMemo);
		InOutMemo.Add(ParentCell, bSupported);
		return bSupported;
	}

	bool CanChildReservationShareSiblingSeamCell(
		const FLayoutPlannedCell& CandidateChildPlannedCell,
		const FIntVector& CandidateOffset,
		const FIntPoint& CandidateFootprintSize,
		const FLayoutChildCapabilityEnvelope& CandidateEnvelope,
		const TArray<FAutomaticChildPlacement>& ExistingPlacements,
		FString* OutFailureReason = nullptr,
		const FIntVector* ExplicitParentCell = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		if (!IsCellOnFootprintPerimeter(CandidateChildPlannedCell.Cell, CandidateFootprintSize))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Candidate child local cell %s is not on the child footprint perimeter, so it cannot participate in a sibling shared seam overlap."),
					*CandidateChildPlannedCell.Cell.ToString(),
					*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(CandidateChildPlannedCell.Intent)));
			}
			return false;
		}

		const FIntVector SharedParentCell = ExplicitParentCell != nullptr
			? *ExplicitParentCell
			: CandidateChildPlannedCell.Cell + CandidateOffset;
		const FAutomaticChildPlacement* MatchingPlacement = nullptr;
		const FLayoutPlannedCell* MatchingExistingChildCell = nullptr;
		for (const FAutomaticChildPlacement& ExistingPlacement : ExistingPlacements)
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
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("Existing child local cell %s at region '%s' is not on that child footprint perimeter, so the overlap cannot be treated as a sibling seam."),
							*ExistingChildPlannedCell.Cell.ToString(),
							*ExistingPlacement.ChildRegionDebugPath,
							*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(ExistingChildPlannedCell.Intent)));
					}
					return false;
				}

				if (MatchingPlacement != nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("Parent cell %s is already claimed by more than one existing child cell, so the overlap is ambiguous and cannot become a single sibling seam."),
							*SharedParentCell.ToString());
					}
					return false;
				}

				MatchingPlacement = &ExistingPlacement;
				MatchingExistingChildCell = &ExistingChildPlannedCell;
			}
		}

		if (MatchingPlacement == nullptr || MatchingExistingChildCell == nullptr)
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Parent cell %s did not line up with any existing child boundary/entry cell for sibling seam sharing."),
					*SharedParentCell.ToString());
			}
			return false;
		}

		if (MatchingPlacement->RegionCellOffset == CandidateOffset)
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Candidate child offset %s exactly matches existing child region '%s'. Full child-region overlap cannot be treated as a sibling shared seam."),
					*CandidateOffset.ToString(),
					*MatchingPlacement->ChildRegionDebugPath);
			}
			return false;
		}

		ELayoutFaceDirection CandidateFaceDirection = ELayoutFaceDirection::PosX;
		ELayoutFaceDirection ExistingFaceDirection = ELayoutFaceDirection::NegX;
		if (!TryGetSharedOverlapFaceDirections(
			CandidateChildPlannedCell.Cell,
			CandidateFootprintSize,
			MatchingExistingChildCell->Cell,
			MatchingPlacement->ChildRequest.FootprintSize,
			CandidateFaceDirection,
			ExistingFaceDirection))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Candidate child local cell %s and existing child local cell %s in region '%s' do not form opposing overlap faces for seam sharing."),
					*CandidateChildPlannedCell.Cell.ToString(),
					*MatchingExistingChildCell->Cell.ToString(),
					*MatchingPlacement->ChildRegionDebugPath);
			}
			return false;
		}

		const bool bSupportsSharedInterface = DoChildSeamCapabilitiesSupportSharedInterface(
			CandidateEnvelope,
			CandidateFaceDirection,
			MatchingPlacement->ChildCapabilityEnvelope,
			ExistingFaceDirection);
		if (!bSupportsSharedInterface && OutFailureReason != nullptr)
		{
			*OutFailureReason = FString::Printf(
				TEXT("Candidate child face %s on local cell %s and existing child face %s on region '%s' local cell %s do not expose compatible sibling seam capabilities."),
				*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(CandidateFaceDirection)),
				*CandidateChildPlannedCell.Cell.ToString(),
				*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(ExistingFaceDirection)),
				*MatchingPlacement->ChildRegionDebugPath,
				*MatchingExistingChildCell->Cell.ToString());
		}
		return bSupportsSharedInterface;
	}

	FString BuildProtectedParentIntentChildOffsetMessage(
		const FIntVector& CandidateOffset,
		const FIntVector& ParentCell,
		const ELayoutCellIntent ParentIntent)
	{
		return FString::Printf(
			TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Parent Plan Compatibility\nRejection Detail:\n  Child footprint overlaps protected parent cell %s.\n  Parent Intent: %s\n  Problem: Child-region reservations are not allowed to overwrite parent Entry or VerticalAccess cells.\n  Fix: Move the child region so it leaves the parent entry/stair location available, or adjust the parent plan so the protected cell lands elsewhere."),
			*CandidateOffset.ToString(),
			*ParentCell.ToString(),
			*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(ParentIntent)));
	}

	/** Selects non-overlapping admitted hosts, permitting only planner-marked Range surplus omissions. */
	bool TrySelectAdmittedParentVerticalAccessHosts(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TSet<FIntVector>& ReservedCells,
		const TArray<FIntVector>& LowerRouteTargets,
		const TArray<FIntVector>& UpperRouteTargets,
		const int32 Seed,
		TArray<FLayoutVerticalAccessHostGroup>& OutSelectedHostGroups,
		TArray<FLayoutVerticalAccessHostGroup>* OutFilteredHostGroups,
		FString* OutFailureReason = nullptr)
	{
		OutSelectedHostGroups.Reset();
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		if (HostGroups.IsEmpty())
		{
			return true;
		}

		TArray<TArray<int32>> LegalOptionIndicesByGroup;
		LegalOptionIndicesByGroup.SetNum(HostGroups.Num());
		for (int32 GroupIndex = 0; GroupIndex < HostGroups.Num(); ++GroupIndex)
		{
			const FLayoutVerticalAccessHostGroup& Group = HostGroups[GroupIndex];
			for (int32 OptionIndex = 0; OptionIndex < Group.Options.Num(); ++OptionIndex)
			{
				const FLayoutVerticalAccessHostOption& Option = Group.Options[OptionIndex];
				if (ParentPlannedCellSet.Contains(Option.LowerCell)
					&& ParentPlannedCellSet.Contains(Option.UpperCell)
					&& !ReservedCells.Contains(Option.LowerCell)
					&& !ReservedCells.Contains(Option.UpperCell)
					// Exact parent witnesses cannot place their bundle, support, or clearance
					// inside independently reserved child authority. Shared seam cells are not reserved.
					&& (!Option.bHasExactCandidateWitness
						|| (!Option.OccupiedCells.ContainsByPredicate([&](const FIntVector& Cell) { return ReservedCells.Contains(Cell); })
							&& !Option.RequiredFilledSupportCells.ContainsByPredicate([&](const FIntVector& Cell) { return ReservedCells.Contains(Cell); })
							&& !Option.RequiredEmptyClearanceCells.ContainsByPredicate([&](const FIntVector& Cell) { return ReservedCells.Contains(Cell); }))))
				{
					LegalOptionIndicesByGroup[GroupIndex].Add(OptionIndex);
				}
			}
			LegalOptionIndicesByGroup[GroupIndex].Sort(
				[&](const int32 LeftIndex, const int32 RightIndex)
				{
					const auto RouteCost = [&](const FLayoutVerticalAccessHostOption& Option)
					{
						int32 Cost = 0;
						for (const FIntVector& Target : LowerRouteTargets)
						{
							if (Target.Z == Option.LowerCell.Z)
							{
								Cost += FMath::Abs(Target.X - Option.LowerCell.X)
									+ FMath::Abs(Target.Y - Option.LowerCell.Y);
							}
						}
						int32 UpperDistance = MAX_int32;
						for (const FIntVector& Target : UpperRouteTargets)
						{
							if (Target.Z == Option.UpperCell.Z)
							{
								UpperDistance = FMath::Min(
									UpperDistance,
									FMath::Abs(Target.X - Option.UpperCell.X)
										+ FMath::Abs(Target.Y - Option.UpperCell.Y));
							}
						}
						return UpperDistance == MAX_int32 ? Cost : Cost + UpperDistance;
					};
					const FLayoutVerticalAccessHostOption& Left = Group.Options[LeftIndex];
					const FLayoutVerticalAccessHostOption& Right = Group.Options[RightIndex];
					if (Left.Tier != Right.Tier) return Left.Tier < Right.Tier;
					const int32 LeftCost = RouteCost(Left);
					const int32 RightCost = RouteCost(Right);
					if (LeftCost != RightCost) return LeftCost < RightCost;
					const uint32 LeftSeedRank = HashCombineFast(static_cast<uint32>(Seed), GetTypeHash(Left.LowerCell));
					const uint32 RightSeedRank = HashCombineFast(static_cast<uint32>(Seed), GetTypeHash(Right.LowerCell));
					if (LeftSeedRank != RightSeedRank) return LeftSeedRank < RightSeedRank;
					if (Left.LowerCell.Z != Right.LowerCell.Z) return Left.LowerCell.Z < Right.LowerCell.Z;
					if (Left.LowerCell.Y != Right.LowerCell.Y) return Left.LowerCell.Y < Right.LowerCell.Y;
					return Left.LowerCell.X < Right.LowerCell.X;
				});
			if (LegalOptionIndicesByGroup[GroupIndex].IsEmpty() && !Group.bAllowOmission)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Residual parent VerticalAccess host group '%s' has no original module-admitted option outside effective child replacement."),
						*Group.GroupId.ToString());
				}
				return false;
			}
		}

		if (OutFilteredHostGroups != nullptr)
		{
			*OutFilteredHostGroups = HostGroups;
			for (int32 GroupIndex = 0; GroupIndex < OutFilteredHostGroups->Num(); ++GroupIndex)
			{
				TArray<FLayoutVerticalAccessHostOption> FilteredOptions;
				for (const int32 OptionIndex : LegalOptionIndicesByGroup[GroupIndex])
				{
					FilteredOptions.Add(HostGroups[GroupIndex].Options[OptionIndex]);
				}
				(*OutFilteredHostGroups)[GroupIndex].Options = MoveTemp(FilteredOptions);
			}
		}

		TArray<int32> GroupSearchOrder;
		for (int32 GroupIndex = 0; GroupIndex < HostGroups.Num(); ++GroupIndex)
		{
			GroupSearchOrder.Add(GroupIndex);
		}
		GroupSearchOrder.Sort(
			[&](const int32 LeftGroupIndex, const int32 RightGroupIndex)
			{
				// Mandatory counts settle before surplus choices consume their available cells.
				if (HostGroups[LeftGroupIndex].bAllowOmission != HostGroups[RightGroupIndex].bAllowOmission)
				{
					return !HostGroups[LeftGroupIndex].bAllowOmission;
				}
				const int32 LeftCount = LegalOptionIndicesByGroup[LeftGroupIndex].Num();
				const int32 RightCount = LegalOptionIndicesByGroup[RightGroupIndex].Num();
				if (LeftCount != RightCount)
				{
					return LeftCount < RightCount;
				}
				return HostGroups[LeftGroupIndex].GroupId.LexicalLess(
					HostGroups[RightGroupIndex].GroupId);
			});

		TArray<int32> SelectedOptionIndexByGroup;
		SelectedOptionIndexByGroup.Init(INDEX_NONE, HostGroups.Num());
		TSet<FIntVector> SelectedOccupiedCells;
		// Settle the precompiled host domain atomically so child replacement cannot
		// freeze individually legal stairs into an overlapping stack.
		TFunction<bool(int32)> SelectGroup = [&](const int32 SearchIndex)
		{
			if (SearchIndex >= GroupSearchOrder.Num())
			{
				return true;
			}

			const int32 GroupIndex = GroupSearchOrder[SearchIndex];
			const FLayoutVerticalAccessHostGroup& Group = HostGroups[GroupIndex];
			if (Group.bAllowOmission && Group.bPreferOmission && SelectGroup(SearchIndex + 1)) return true;
			for (int32 AdjacencyPass = 0; AdjacencyPass < 2; ++AdjacencyPass)
			{
				for (const int32 OptionIndex : LegalOptionIndicesByGroup[GroupIndex])
				{
					const FLayoutVerticalAccessHostOption& Option =
						HostGroups[GroupIndex].Options[OptionIndex];
					if (SelectedOccupiedCells.Contains(Option.LowerCell)
						|| SelectedOccupiedCells.Contains(Option.UpperCell))
					{
						continue;
					}
					const bool bTouchesSelectedBundle = [&]()
					{
						for (const FIntVector& SelectedCell : SelectedOccupiedCells)
						{
							if ((SelectedCell.Z == Option.LowerCell.Z
									&& FMath::Abs(SelectedCell.X - Option.LowerCell.X)
										+ FMath::Abs(SelectedCell.Y - Option.LowerCell.Y) == 1)
								|| (SelectedCell.Z == Option.UpperCell.Z
									&& FMath::Abs(SelectedCell.X - Option.UpperCell.X)
										+ FMath::Abs(SelectedCell.Y - Option.UpperCell.Y) == 1))
							{
								return true;
							}
						}
						return false;
					}();
					if ((AdjacencyPass == 0) == bTouchesSelectedBundle)
					{
						continue;
					}

					SelectedOptionIndexByGroup[GroupIndex] = OptionIndex;
					SelectedOccupiedCells.Add(Option.LowerCell);
					SelectedOccupiedCells.Add(Option.UpperCell);
					if (SelectGroup(SearchIndex + 1))
					{
						return true;
					}
					SelectedOccupiedCells.Remove(Option.LowerCell);
					SelectedOccupiedCells.Remove(Option.UpperCell);
					SelectedOptionIndexByGroup[GroupIndex] = INDEX_NONE;
				}
			}
			return Group.bAllowOmission && !Group.bPreferOmission && SelectGroup(SearchIndex + 1);
		};
		if (!SelectGroup(0))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("Residual parent VerticalAccess host groups have legal individual options but no non-overlapping complete assignment after child replacement.");
			}
			return false;
		}

		for (int32 GroupIndex = 0; GroupIndex < HostGroups.Num(); ++GroupIndex)
		{
			if (SelectedOptionIndexByGroup[GroupIndex] == INDEX_NONE)
			{
				if (OutFilteredHostGroups != nullptr) (*OutFilteredHostGroups)[GroupIndex].bPreferOmission = true;
				continue;
			}
			const FLayoutVerticalAccessHostOption SelectedOption =
				HostGroups[GroupIndex].Options[SelectedOptionIndexByGroup[GroupIndex]];
			FLayoutVerticalAccessHostGroup& SelectedGroup = OutSelectedHostGroups.Add_GetRef(HostGroups[GroupIndex]);
			SelectedGroup.Options = {SelectedOption};
			SelectedGroup.DeckCell = SelectedOption.UpperCell;
			SelectedGroup.bAllowOmission = false;
			SelectedGroup.bPreferOmission = false;
			if (OutFilteredHostGroups != nullptr)
			{
				FLayoutVerticalAccessHostGroup& FilteredGroup =
					(*OutFilteredHostGroups)[GroupIndex];
				const int32 SelectedFilteredIndex = FilteredGroup.Options.IndexOfByPredicate(
					[&SelectedOption](const FLayoutVerticalAccessHostOption& Option)
					{
						return Option.LowerCell == SelectedOption.LowerCell
							&& Option.UpperCell == SelectedOption.UpperCell;
					});
				if (SelectedFilteredIndex > 0)
				{
					FilteredGroup.Options.Swap(0, SelectedFilteredIndex);
				}
				FilteredGroup.DeckCell = SelectedOption.UpperCell;
				FilteredGroup.bPreferOmission = false;
			}
		}
		return true;
	}

	/** Restores selectable VerticalAccess representatives to their structural parent intent. */
	ELayoutCellIntent DetermineProvisionalParentIntent(
		const FIntVector& Cell,
		const FIntPoint& ParentFootprintSize)
	{
		if (Cell.X == 0 || Cell.Y == 0
			|| Cell.X == ParentFootprintSize.X - 1
			|| Cell.Y == ParentFootprintSize.Y - 1)
		{
			return ELayoutCellIntent::Boundary;
		}
		const float CenterX = static_cast<float>(ParentFootprintSize.X - 1) * 0.5f;
		const float CenterY = static_cast<float>(ParentFootprintSize.Y - 1) * 0.5f;
		if (Cell.Z == 0
			&& FMath::Abs(static_cast<float>(Cell.X) - CenterX) <= 0.5f
			&& FMath::Abs(static_cast<float>(Cell.Y) - CenterY) <= 0.5f)
		{
			return ELayoutCellIntent::Core;
		}
		return ELayoutCellIntent::Interior;
	}

	void RebuildTopPlannedLevelByXY(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		TMap<FIntPoint, int32>& OutTopLevelByXY,
		int32& OutMaxLevel)
	{
		OutTopLevelByXY.Reset();
		OutMaxLevel = 0;
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			const FIntPoint ColumnKey(PlannedCell.Cell.X, PlannedCell.Cell.Y);
			int32& TopLevel = OutTopLevelByXY.FindOrAdd(ColumnKey);
			TopLevel = FMath::Max(TopLevel, PlannedCell.Cell.Z);
			OutMaxLevel = FMath::Max(OutMaxLevel, PlannedCell.Cell.Z);
		}
	}

	bool DoesCellMatchLevelPlacementPolicy(
		const FIntVector& Cell,
		const TMap<FIntPoint, int32>& TopLevelByXY,
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
		{
			const int32* TopLevel = TopLevelByXY.Find(FIntPoint(Cell.X, Cell.Y));
			return TopLevel != nullptr && Cell.Z == *TopLevel;
		}
		case ELayoutLevelPlacementPolicy::AboveGroundLevel:
			return Cell.Z > 0;
		case ELayoutLevelPlacementPolicy::BelowTopLevel:
		{
			const int32* TopLevel = TopLevelByXY.Find(FIntPoint(Cell.X, Cell.Y));
			return TopLevel != nullptr && Cell.Z < *TopLevel;
		}
		default:
			return true;
		}
	}

	void CollectProtectedParentTraversalCells(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		TSet<FIntVector>& OutProtectedTraversalCells)
	{
		OutProtectedTraversalCells.Reset();

		static const ELayoutFaceDirection HorizontalDirections[] =
		{
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY
		};

		for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
		{
			if (PlannedCell.Intent != ELayoutCellIntent::Entry)
			{
				continue;
			}

			for (const ELayoutFaceDirection Direction : HorizontalDirections)
			{
				const FIntVector NeighborCell = PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				const ELayoutCellIntent* NeighborIntent = ParentPlannedCellIntents.Find(NeighborCell);
				if (NeighborIntent == nullptr)
				{
					continue;
				}

				const bool bSupportsProtectedTraversal =
					*NeighborIntent == ELayoutCellIntent::Entry
					|| *NeighborIntent == ELayoutCellIntent::Connector
					|| *NeighborIntent == ELayoutCellIntent::Core
					|| *NeighborIntent == ELayoutCellIntent::Interior
					|| *NeighborIntent == ELayoutCellIntent::VerticalAccess;
				if (!bSupportsProtectedTraversal)
				{
					continue;
				}

				OutProtectedTraversalCells.Add(NeighborCell);
			}
		}
	}

	void AppendProtectedParentRouteReservationCells(
		const FLayoutSolveResult& ParentSolveResult,
		TSet<FIntVector>& InOutProtectedTraversalCells)
	{
		for (const FLayoutCellReservationRecord& Reservation : ParentSolveResult.CompiledReservations)
		{
			switch (Reservation.ReservationKind)
			{
			case ELayoutCellReservationKind::RequiredRoute:
			case ELayoutCellReservationKind::RouteJunction:
			case ELayoutCellReservationKind::VerticalContinuation:
				InOutProtectedTraversalCells.Add(Reservation.Cell);
				break;
			default:
				break;
			}
		}
	}

	TSet<ELayoutFaceDirection> CollectVerticalAccessIngressDirections(const FLayoutModuleCatalog& ModuleCatalog)
	{
		TSet<ELayoutFaceDirection> Result;
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : ModuleCatalog.Modules)
		{
			if (!ModuleSnapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess))
			{
				continue;
			}

			for (const FLayoutDerivedVerticalAccessContract& Contract : ModuleSnapshot.DerivedVerticalAccessContracts)
			{
				for (const FLayoutDerivedEndpointOffer& Offer : ModuleSnapshot.DerivedEndpointOffers)
				{
					if (Offer.LocalCell != Contract.LocalCell
						|| Offer.FaceDirection == ELayoutFaceDirection::PosZ
						|| Offer.FaceDirection == ELayoutFaceDirection::NegZ)
					{
						continue;
					}

					for (const FGameplayTag& TraversalChannel : Offer.TraversalChannels)
					{
						if (TraversalChannel.IsValid() && Contract.SourceTraversalChannels.HasTagExact(TraversalChannel))
						{
							Result.Add(Offer.FaceDirection);
							break;
						}
					}
				}
			}
		}

		return Result;
	}

	bool TryBuildParentPlannedCellsWithReservedChildren(
		const TArray<FLayoutPlannedCell>& BaseParentPlannedCells,
		const TSet<FIntVector>& BaseParentPlannedCellSet,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ReservedCells,
		const TSet<FIntVector>& ProtectedTraversalCells,
		const int32 EntrySelectionSeed,
		const bool bAllowExteriorEntryRelocation,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FLayoutVerticalAccessHostGroup>& VerticalAccessHostGroups,
		TArray<FLayoutPlannedCell>& OutParentPlannedCells,
		TArray<FLayoutVerticalAccessHostGroup>* OutFilteredHostGroups,
		FString* OutFailureReason = nullptr)
	{
		const auto ReclassifyDerivedExposedCellsToBoundary =
			[&BaseParentPlannedCellSet](TArray<FLayoutPlannedCell>& PlannedCells)
			{
				TSet<FIntVector> PlannedCellSet;
				for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
				{
					PlannedCellSet.Add(PlannedCell.Cell);
				}

				for (FLayoutPlannedCell& PlannedCell : PlannedCells)
				{
					if (PlannedCell.Intent == ELayoutCellIntent::Entry
						|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
					{
						continue;
					}

					bool bTouchesMissingLateralNeighbor = false;
					static const ELayoutFaceDirection LateralDirections[] =
					{
						ELayoutFaceDirection::PosX,
						ELayoutFaceDirection::NegX,
						ELayoutFaceDirection::PosY,
						ELayoutFaceDirection::NegY
					};
					for (const ELayoutFaceDirection Direction : LateralDirections)
					{
						const FIntVector NeighborCell =
							PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (!PlannedCellSet.Contains(NeighborCell)
							&& !BaseParentPlannedCellSet.Contains(NeighborCell))
						{
							bTouchesMissingLateralNeighbor = true;
							break;
						}
					}

					if (bTouchesMissingLateralNeighbor)
					{
						// After rebuilding a derived parent plan, cells that became exposed to
						// authored-outside space must solve as boundary cells instead of keeping
						// stale Interior/Core/Connector intent. Do not promote cells that are
						// only exposed because a child reserved an adjacent parent cell; those
						// interfaces are negotiated separately as parent/child seams.
						PlannedCell.Intent = ELayoutCellIntent::Boundary;
					}
				}
			};

		OutParentPlannedCells.Reset();
		if (OutFilteredHostGroups != nullptr)
		{
			*OutFilteredHostGroups = VerticalAccessHostGroups;
		}
		static const ELayoutFaceDirection HorizontalDirections[] =
		{
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY
		};
		const auto IsPhysicalExteriorCell = [&ParentFootprintSize](const FIntVector& Cell)
		{
			return Cell.X == 0
				|| Cell.Y == 0
				|| Cell.X == ParentFootprintSize.X - 1
				|| Cell.Y == ParentFootprintSize.Y - 1;
		};
		const auto IsRelocatableExteriorEntry = [&IsPhysicalExteriorCell](
			const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Intent == ELayoutCellIntent::Entry
				&& (PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary
					|| (PlannedCell.EntryOrigin == ELayoutEntryOrigin::None
						&& IsPhysicalExteriorCell(PlannedCell.Cell)));
		};

		TArray<FLayoutPlannedCell> RelocatedEntries;
		TSet<FIntVector> RelocatedEntryCells;
		if (bAllowExteriorEntryRelocation)
		{
			for (const FLayoutPlannedCell& PlannedCell : BaseParentPlannedCells)
			{
				// Child replacement may move authored exterior Entries, but fixed
				// terrain, continuation, and child-contract gates keep their authority.
				if (!IsRelocatableExteriorEntry(PlannedCell))
				{
					continue;
				}

				bool bHasSurvivingContinuation = false;
				for (const ELayoutFaceDirection Direction : HorizontalDirections)
				{
					const FIntVector NeighborCell =
						PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					const FLayoutPlannedCell* Neighbor = BaseParentPlannedCells.FindByPredicate(
						[&NeighborCell](const FLayoutPlannedCell& Candidate)
						{
							return Candidate.Cell == NeighborCell;
						});
					if (Neighbor != nullptr
						&& !ReservedCells.Contains(NeighborCell)
						&& (Neighbor->Intent == ELayoutCellIntent::Entry
							|| Neighbor->Intent == ELayoutCellIntent::Connector
							|| Neighbor->Intent == ELayoutCellIntent::Core
							|| Neighbor->Intent == ELayoutCellIntent::Interior
							|| Neighbor->Intent == ELayoutCellIntent::VerticalAccess))
					{
						bHasSurvivingContinuation = true;
						break;
					}
				}
				if (ReservedCells.Contains(PlannedCell.Cell) || !bHasSurvivingContinuation)
				{
					RelocatedEntries.Add(PlannedCell);
					RelocatedEntryCells.Add(PlannedCell.Cell);
				}
			}
		}
		RelocatedEntries.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
		{
			return Left.ModuleLevelIndex != Right.ModuleLevelIndex
				? Left.ModuleLevelIndex < Right.ModuleLevelIndex
				: Left.Cell.Z != Right.Cell.Z
					? Left.Cell.Z < Right.Cell.Z
					: Left.Cell.Y != Right.Cell.Y
						? Left.Cell.Y < Right.Cell.Y
						: Left.Cell.X < Right.Cell.X;
		});

		const auto IsTraversalProtectedByRetainedEntry =
			[&](const FIntVector& Cell)
			{
				for (const FLayoutPlannedCell& PlannedCell : BaseParentPlannedCells)
				{
					if (PlannedCell.Intent != ELayoutCellIntent::Entry
						|| RelocatedEntryCells.Contains(PlannedCell.Cell))
					{
						continue;
					}
					for (const ELayoutFaceDirection Direction : HorizontalDirections)
					{
						if (PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction) == Cell)
						{
							return true;
						}
					}
				}
				return false;
			};

		TArray<FLayoutPlannedCell> OriginalVerticalAccessCells;
		for (const FLayoutPlannedCell& PlannedCell : BaseParentPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				OriginalVerticalAccessCells.Add(PlannedCell);
				continue;
			}

			if (!ReservedCells.Contains(PlannedCell.Cell))
			{
				FLayoutPlannedCell AdjustedPlannedCell = PlannedCell;
				if (RelocatedEntryCells.Contains(PlannedCell.Cell))
				{
					AdjustedPlannedCell.Intent = ELayoutCellIntent::Boundary;
					AdjustedPlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
				}
				OutParentPlannedCells.Add(AdjustedPlannedCell);
				continue;
			}

			if (ProtectedTraversalCells.Contains(PlannedCell.Cell)
				&& IsTraversalProtectedByRetainedEntry(PlannedCell.Cell))
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Child footprint overlaps protected parent traversal cell %s.\nParent Intent: %s\nProblem: Child-region reservations are not allowed to remove the interior continuation needed to route from the parent entry/stairs into the remaining plan.\nFix: Move or rotate the child region so it leaves the parent corridor cell available, or adjust the parent plan so a different corridor path can be used."),
						*PlannedCell.Cell.ToString(),
						*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(PlannedCell.Intent)));
				}
				return false;
			}

			if (PlannedCell.Intent == ELayoutCellIntent::Entry
				&& !RelocatedEntryCells.Contains(PlannedCell.Cell))
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Child footprint overlaps protected parent cell %s.\nParent Intent: %s\nProblem: Child-region reservations are not allowed to overwrite parent Entry cells.\nFix: Move the child region so it leaves the parent entry location available, or adjust the parent plan so the protected cell lands elsewhere."),
						*PlannedCell.Cell.ToString(),
						*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(PlannedCell.Intent)));
				}
				return false;
			}

		}

		/** Reassigns only child-blocked ordinary-root entries; fixed continuation anchors never enter this path. */
		// Parent plans are bounded; index by cell only if relocation profiling becomes material.
		const auto RelocateEntries = [&]()
		{
			TArray<FIntVector> ExistingEntryCells;
			for (const FLayoutPlannedCell& PlannedCell : OutParentPlannedCells)
			{
				if (PlannedCell.Intent == ELayoutCellIntent::Entry)
				{
					ExistingEntryCells.Add(PlannedCell.Cell);
				}
			}

			for (int32 RelocationIndex = 0; RelocationIndex < RelocatedEntries.Num(); ++RelocationIndex)
			{
				const FLayoutPlannedCell& RelocatedEntry = RelocatedEntries[RelocationIndex];
				FLayoutPlannedCell* BestCell = nullptr;
				bool bBestIsCorner = true;
				int32 BestDistance = MIN_int32;
				uint32 BestTieBreaker = MAX_uint32;
				for (FLayoutPlannedCell& Candidate : OutParentPlannedCells)
				{
					const bool bOnMinOrMaxX = Candidate.Cell.X == 0
						|| Candidate.Cell.X == ParentFootprintSize.X - 1;
					const bool bOnMinOrMaxY = Candidate.Cell.Y == 0
						|| Candidate.Cell.Y == ParentFootprintSize.Y - 1;
					if (Candidate.Cell.Z != RelocatedEntry.Cell.Z
						|| Candidate.ModuleLevelIndex != RelocatedEntry.ModuleLevelIndex
						|| (!bOnMinOrMaxX && !bOnMinOrMaxY)
						|| Candidate.Intent == ELayoutCellIntent::Entry
						|| Candidate.Intent == ELayoutCellIntent::VerticalAccess)
					{
						continue;
					}

					bool bHasContinuation = false;
					for (const ELayoutFaceDirection Direction : HorizontalDirections)
					{
						const FIntVector NeighborCell =
							Candidate.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
						const FLayoutPlannedCell* Neighbor = OutParentPlannedCells.FindByPredicate(
							[&NeighborCell](const FLayoutPlannedCell& Value)
							{
								return Value.Cell == NeighborCell;
							});
						if (Neighbor != nullptr
							&& (Neighbor->Intent == ELayoutCellIntent::Entry
								|| Neighbor->Intent == ELayoutCellIntent::Connector
								|| Neighbor->Intent == ELayoutCellIntent::Core
								|| Neighbor->Intent == ELayoutCellIntent::Interior
								|| Neighbor->Intent == ELayoutCellIntent::VerticalAccess))
						{
							bHasContinuation = true;
							break;
						}
					}
					if (!bHasContinuation)
					{
						continue;
					}

					const bool bIsCorner = bOnMinOrMaxX && bOnMinOrMaxY;
					int32 NearestDistance = ExistingEntryCells.IsEmpty() ? 0 : MAX_int32;
					for (const FIntVector& ExistingEntryCell : ExistingEntryCells)
					{
						NearestDistance = FMath::Min(
							NearestDistance,
							FMath::Abs(Candidate.Cell.X - ExistingEntryCell.X)
								+ FMath::Abs(Candidate.Cell.Y - ExistingEntryCell.Y));
					}
					const uint32 TieBreaker = HashCombineFast(
						static_cast<uint32>(EntrySelectionSeed),
						GetTypeHash(Candidate.Cell));
					if (BestCell == nullptr
						|| (!bIsCorner && bBestIsCorner)
						|| (bIsCorner == bBestIsCorner && NearestDistance > BestDistance)
						|| (bIsCorner == bBestIsCorner
							&& NearestDistance == BestDistance
							&& TieBreaker < BestTieBreaker))
					{
						BestCell = &Candidate;
						bBestIsCorner = bIsCorner;
						BestDistance = NearestDistance;
						BestTieBreaker = TieBreaker;
					}
				}

				if (BestCell == nullptr)
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = TEXT("Child replacement blocked a parent exterior Entry and no remaining boundary cell retained a traversable parent continuation.");
					}
					return false;
				}
				BestCell->Intent = ELayoutCellIntent::Entry;
				BestCell->EntryOrigin = RelocatedEntry.EntryOrigin;
				ExistingEntryCells.Add(BestCell->Cell);
			}
			return true;
		};

		if (OriginalVerticalAccessCells.IsEmpty() && VerticalAccessHostGroups.IsEmpty())
		{
			if (!RelocateEntries())
			{
				return false;
			}
			ReclassifyDerivedExposedCellsToBoundary(OutParentPlannedCells);
			return true;
		}

		for (const FLayoutPlannedCell& OriginalVerticalAccessCell : OriginalVerticalAccessCells)
		{
			if (ReservedCells.Contains(OriginalVerticalAccessCell.Cell))
			{
				continue;
			}
			FLayoutPlannedCell ProvisionalCell = OriginalVerticalAccessCell;
			ProvisionalCell.Intent = DetermineProvisionalParentIntent(
				ProvisionalCell.Cell,
				ParentFootprintSize);
			OutParentPlannedCells.Add(MoveTemp(ProvisionalCell));
		}

		TArray<FIntVector> LowerRouteTargets = ProtectedTraversalCells.Array();
		TArray<FIntVector> UpperRouteTargets;
		for (const FLayoutPlannedCell& PlannedCell : BaseParentPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				LowerRouteTargets.AddUnique(PlannedCell.Cell);
			}
			const int32 ModuleLevel = PlannedCell.ModuleLevelIndex == INDEX_NONE
				? PlannedCell.Cell.Z
				: PlannedCell.ModuleLevelIndex;
			if (ModuleLevel > 0
				&& (PlannedCell.PlacementZone == ELayoutPlacementZone::Edge
					|| PlannedCell.PlacementZone == ELayoutPlacementZone::Corner))
			{
				UpperRouteTargets.Add(PlannedCell.Cell);
			}
		}
		TArray<FLayoutVerticalAccessHostGroup> SelectedHostGroups;
		TArray<FLayoutVerticalAccessHostGroup> FilteredHostGroups;
		if (!VerticalAccessHostGroups.IsEmpty())
		{
			if (!TrySelectAdmittedParentVerticalAccessHosts(
				VerticalAccessHostGroups,
				BaseParentPlannedCellSet,
				ReservedCells,
				LowerRouteTargets,
				UpperRouteTargets,
				EntrySelectionSeed,
				SelectedHostGroups,
				&FilteredHostGroups,
				OutFailureReason))
			{
				return false;
			}
		}
		else
		{
			for (const FLayoutPlannedCell& OriginalVerticalAccessCell : OriginalVerticalAccessCells)
			{
				if (ReservedCells.Contains(OriginalVerticalAccessCell.Cell))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("Child replacement covers parent VerticalAccess cell %s, but the frozen request has no original module-admitted host group for reassignment."),
							*OriginalVerticalAccessCell.Cell.ToString());
					}
					return false;
				}
				FLayoutVerticalAccessHostGroup& Group = SelectedHostGroups.AddDefaulted_GetRef();
				Group.GroupId = FLayoutId(*FString::Printf(
					TEXT("FrozenVerticalAccess.%s"),
					*OriginalVerticalAccessCell.Cell.ToString()));
				FLayoutVerticalAccessHostOption& Option = Group.Options.AddDefaulted_GetRef();
				Option.LowerCell = OriginalVerticalAccessCell.Cell;
				Option.UpperCell = OriginalVerticalAccessCell.Cell + FIntVector(0, 0, 1);
				Group.DeckCell = Option.UpperCell;
			}
		}

		for (const FLayoutVerticalAccessHostGroup& Group : SelectedHostGroups)
		{
			if (Group.Options.Num() != 1)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Residual parent VerticalAccess host group '%s' did not freeze exactly one admitted option."),
						*Group.GroupId.ToString());
				}
				return false;
			}
			const FIntVector& SelectedLowerCell = Group.Options[0].LowerCell;
			FLayoutPlannedCell* SelectedCell = OutParentPlannedCells.FindByPredicate(
				[&SelectedLowerCell](const FLayoutPlannedCell& PlannedCell)
				{
					return PlannedCell.Cell == SelectedLowerCell;
				});
			if (SelectedCell == nullptr)
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Selected residual parent VerticalAccess host %s is absent after child replacement."),
						*SelectedLowerCell.ToString());
				}
				return false;
			}
			SelectedCell->Intent = ELayoutCellIntent::VerticalAccess;
		}
		if (OutFilteredHostGroups != nullptr)
		{
			*OutFilteredHostGroups = VerticalAccessHostGroups.IsEmpty()
				? MoveTemp(SelectedHostGroups)
				: MoveTemp(FilteredHostGroups);
		}

		if (!RelocateEntries())
		{
			return false;
		}
		ReclassifyDerivedExposedCellsToBoundary(OutParentPlannedCells);
		return true;
	}

	int32 CountChildRegionEntries(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
	{
		int32 Count = 0;
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.ContentKind == ELayoutRegionContentKind::ChildRegion
				&& EntrySnapshot.ChildProfileSnapshotId != NAME_None)
			{
				++Count;
			}
		}
		return Count;
	}

	bool ContentSetHasChildRegionEntries(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
	{
		return CountChildRegionEntries(ContentSetSnapshot) > 0;
	}

	bool ContentSetAuthorsDoorSeamProviders(const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot)
	{
		return ContentSetSnapshot.Entries.ContainsByPredicate(
			[](const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
			{
				return EntrySnapshot.SeamProviderIntents.ContainsByPredicate(
					[](const FLayoutSeamProviderIntent& SeamIntent)
					{
						return SeamIntent.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor;
					});
			});
	}

	bool ModuleCatalogExposesDoorInterfaceCapabilities(const FLayoutModuleCatalog& ModuleCatalog)
	{
		return ModuleCatalog.Modules.ContainsByPredicate(
			[](const FLayoutModuleSolveSnapshot& ModuleSnapshot)
			{
				return ModuleSnapshot.SeamProviderIntents.ContainsByPredicate(
					[](const FLayoutSeamProviderIntent& SeamIntent)
					{
						return SeamIntent.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor;
					});
			});
	}

	bool RequestTemplateTreeContainsDoorInterfaceAuthoring(
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog)
	{
		TSet<FLayoutId> VisitedTemplateIds;
		TFunction<bool(const FLayoutRegionContentSetSolveSnapshot&, const FLayoutModuleCatalog&)> VisitTemplateTree =
			[&VisitedTemplateIds, &VisitTemplateTree](
				const FLayoutRegionContentSetSolveSnapshot& CandidateContentSetSnapshot,
				const FLayoutModuleCatalog& CandidateModuleCatalog) -> bool
			{
				if (ContentSetAuthorsDoorSeamProviders(CandidateContentSetSnapshot)
					|| ModuleCatalogExposesDoorInterfaceCapabilities(CandidateModuleCatalog))
				{
					return true;
				}

				for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : CandidateContentSetSnapshot.Entries)
				{
					if (!EntrySnapshot.CompiledChildRequestTemplate.IsValid())
					{
						continue;
					}

					const FLayoutChildRequestTemplateSnapshot& ChildTemplate =
						*EntrySnapshot.CompiledChildRequestTemplate;
					if (ChildTemplate.EffectiveSnapshotId != NAME_None)
					{
						if (VisitedTemplateIds.Contains(ChildTemplate.EffectiveSnapshotId))
						{
							continue;
						}
						VisitedTemplateIds.Add(ChildTemplate.EffectiveSnapshotId);
					}

					if (VisitTemplateTree(
						ChildTemplate.ContentSetSnapshot,
						ChildTemplate.ModuleCatalog))
					{
						return true;
					}
				}

				return false;
			};

		return VisitTemplateTree(ContentSetSnapshot, ModuleCatalog);
	}

	FString BuildCommittedVerticalAccessOwnershipKey(const FCommittedVerticalAccessOwnership& Ownership)
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
		const FString CountedChildren =
			Ownership.CountedChildRegionDebugPaths.IsEmpty()
				? TEXT("<none>")
				: FString::Join(Ownership.CountedChildRegionDebugPaths, TEXT(","));
		return FString::Printf(
			TEXT("Required=%d|Exact=%d|ParentPotential=%d|ParentCount=%d|ParentCells=%s|RouteSupportCells=%s|CountedChildren=%s"),
			Ownership.RequiredHostProviderCount,
			Ownership.bRequiresExactHostProviderCount ? 1 : 0,
			Ownership.PotentialParentProviderCount,
			Ownership.CountedParentProviderCount,
			ParentCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ParentCellParts, TEXT(",")),
			RouteSupportCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(RouteSupportCellParts, TEXT(",")),
			*CountedChildren);
	}

	FLayoutId BuildEntryAnchorTemplateCommitmentId(const int32 EntryOrdinal)
	{
		return FLayoutId(*FString::Printf(TEXT("EntryAnchor.%d"), EntryOrdinal));
	}

	FLayoutId BuildChildEntryAnchorCommitmentId(
		const FString& ChildRegionDebugPath,
		const int32 EntryOrdinal)
	{
		return FLayoutId(*FString::Printf(TEXT("%s.EntryAnchor.%d"), *ChildRegionDebugPath, EntryOrdinal));
	}

	FString BuildPlacementRegionMemoIdentity(const FAutomaticChildPlacement& Placement)
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

		return FString::Printf(
			TEXT("%s@%s|Footprint=%s|Planned=%s"),
			Placement.EntryId == NAME_None ? TEXT("<none>") : *Placement.EntryId.ToString(),
			*Placement.RegionCellOffset.ToString(),
			*Placement.ChildRequest.FootprintSize.ToString(),
			PlannedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedCellParts, TEXT(",")));
	}

	FString BuildPlacementRegionSemanticMemoIdentity(const FAutomaticChildPlacement& Placement)
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

		return FString::Printf(
			TEXT("%s|Footprint=%s|Planned=%s"),
			*Placement.RegionCellOffset.ToString(),
			*Placement.ChildRequest.FootprintSize.ToString(),
			PlannedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedCellParts, TEXT(",")));
	}

	void BuildChildRegionMemoIdentityMap(
		const TArray<FAutomaticChildPlacement>& Placements,
		TMap<FString, FString>& OutMemoIdentityByRegionPath)
	{
		OutMemoIdentityByRegionPath.Reset();
		OutMemoIdentityByRegionPath.Reserve(Placements.Num());
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			OutMemoIdentityByRegionPath.Add(
				Placement.ChildRegionDebugPath,
				BuildPlacementRegionMemoIdentity(Placement));
		}
	}

	void BuildChildRegionSemanticMemoIdentityMap(
		const TArray<FAutomaticChildPlacement>& Placements,
		TMap<FString, FString>& OutMemoIdentityByRegionPath)
	{
		OutMemoIdentityByRegionPath.Reset();
		OutMemoIdentityByRegionPath.Reserve(Placements.Num());
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			OutMemoIdentityByRegionPath.Add(
				Placement.ChildRegionDebugPath,
				BuildPlacementRegionSemanticMemoIdentity(Placement));
		}
	}

	FString CanonicalizeRegionPathForMemo(
		const FString& RegionDebugPath,
		const TMap<FString, FString>& MemoIdentityByRegionPath)
	{
		if (const FString* CanonicalIdentity = MemoIdentityByRegionPath.Find(RegionDebugPath))
		{
			return *CanonicalIdentity;
		}

		return RegionDebugPath;
	}

	FString BuildCommittedVerticalAccessOwnershipMemoKey(
		const FCommittedVerticalAccessOwnership& Ownership,
		const TMap<FString, FString>& MemoIdentityByRegionPath)
	{
		TArray<FString> CanonicalChildPaths;
		CanonicalChildPaths.Reserve(Ownership.CountedChildRegionDebugPaths.Num());
		for (const FString& RegionPath : Ownership.CountedChildRegionDebugPaths)
		{
			CanonicalChildPaths.Add(CanonicalizeRegionPathForMemo(RegionPath, MemoIdentityByRegionPath));
		}

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

		return FString::Printf(
			TEXT("Required=%d|ParentPotential=%d|ParentCount=%d|ParentCells=%s|RouteSupportCells=%s|CountedChildren=%s"),
			Ownership.RequiredHostProviderCount,
			Ownership.PotentialParentProviderCount,
			Ownership.CountedParentProviderCount,
			ParentCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ParentCellParts, TEXT(",")),
			RouteSupportCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(RouteSupportCellParts, TEXT(",")),
			CanonicalChildPaths.IsEmpty() ? TEXT("<none>") : *FString::Join(CanonicalChildPaths, TEXT(",")));
	}

	int32 GetMinimumRequiredVerticalAccessCount(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		switch (ProfileSnapshot.VerticalAccessCountMode)
		{
		case ELayoutCountConstraintMode::Exact:
			return ProfileSnapshot.VerticalAccessCount;
		case ELayoutCountConstraintMode::Range:
			return ProfileSnapshot.MinVerticalAccessCount;
		case ELayoutCountConstraintMode::None:
		default:
			return 0;
		}
	}

	bool RequiresExactHostVerticalAccessProviderCount(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		return ProfileSnapshot.VerticalAccessCountMode == ELayoutCountConstraintMode::Exact
			&& ProfileSnapshot.VerticalAccessCount > 0;
	}

	int32 GetRequiredHostVerticalAccessProviderCount(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells)
	{
		const bool bParentPlanContainsVerticalAccessIntent =
			ParentPlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& PlannedCell)
			{
				return PlannedCell.Intent == ELayoutCellIntent::VerticalAccess;
			});
		if (!bParentPlanContainsVerticalAccessIntent)
		{
			// Some imported fixtures constrain vertical-access through module-role CSP
			// rather than pre-tagged planned cells. In that mode the parent proof solve,
			// not recursive placement ownership, validates the profile's stair count.
			return 0;
		}

		const int32 ExplicitCount = GetMinimumRequiredVerticalAccessCount(RootRequest.ProfileSnapshot);
		if (ExplicitCount > 0)
		{
			return ExplicitCount;
		}

		return 1;
	}

	bool DoesPlacementSupportCommittedVerticalAccess(const FAutomaticChildPlacement& Placement)
	{
		return Placement.bContributesHostVerticalAccess
			&& Placement.bSupportsVerticalAccess
			&& !Placement.ChildVerticalAccessLocalCells.IsEmpty();
	}

	int32 GetCommittedVerticalAccessProviderCount(const FCommittedVerticalAccessOwnership& Ownership)
	{
		if (Ownership.ResolvedHostProviderCount > 0
			|| Ownership.RequiredHostProviderCount <= 0)
		{
			return Ownership.ResolvedHostProviderCount;
		}

		return FMath::Min(
				Ownership.CountedParentProviderCount,
				Ownership.CountedParentVerticalAccessCells.Num())
			+ Ownership.CountedChildRegionDebugPaths.Num();
	}

	bool IsCommittedVerticalAccessRequirementSatisfied(const FCommittedVerticalAccessOwnership& Ownership)
	{
		if (Ownership.RequiredHostProviderCount <= 0)
		{
			return true;
		}

		const int32 TotalProviderCount =
			GetCommittedVerticalAccessProviderCount(Ownership);
		if (Ownership.bRequiresExactHostProviderCount)
		{
			return TotalProviderCount == Ownership.RequiredHostProviderCount;
		}

		return TotalProviderCount >= Ownership.RequiredHostProviderCount;
	}

	void FinalizeCommittedVerticalAccessOwnershipSummary(FCommittedVerticalAccessOwnership& Ownership)
	{
		Ownership.CountedParentVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
		Ownership.RetainedParentRouteSupportVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
		Ownership.CountedChildRegionDebugPaths.Sort();
		Ownership.LocalOnlyChildRegionDebugPaths.Sort();
		Ownership.UnusableContributingChildRegionDebugPaths.Sort();
		Ownership.ExtraContributingChildRegionDebugPaths.Sort();
		Ownership.ResolvedHostProviderCount =
			GetCommittedVerticalAccessProviderCount(Ownership);

		Ownership.ChildRegionDebugPath.Reset();
		if (Ownership.CountedParentProviderCount > 0)
		{
			Ownership.OwnerKind = ECommittedVerticalAccessOwnerKind::Parent;
			return;
		}

		if (!Ownership.CountedChildRegionDebugPaths.IsEmpty())
		{
			Ownership.OwnerKind = ECommittedVerticalAccessOwnerKind::ChildRegion;
			if (Ownership.CountedChildRegionDebugPaths.Num() == 1)
			{
				Ownership.ChildRegionDebugPath = Ownership.CountedChildRegionDebugPaths[0];
			}
			return;
		}

		Ownership.OwnerKind = Ownership.RequiredHostProviderCount > 0
			? ECommittedVerticalAccessOwnerKind::Parent
			: ECommittedVerticalAccessOwnerKind::None;
	}

	FString BuildCommittedVerticalAccessFailureReason(const FCommittedVerticalAccessOwnership& Ownership)
	{
		const FString CountedChildren =
			Ownership.CountedChildRegionDebugPaths.IsEmpty()
				? TEXT("<none>")
				: FString::Join(Ownership.CountedChildRegionDebugPaths, TEXT(", "));
		const FString LocalOnlyChildren =
			Ownership.LocalOnlyChildRegionDebugPaths.IsEmpty()
				? TEXT("<none>")
				: FString::Join(Ownership.LocalOnlyChildRegionDebugPaths, TEXT(", "));
		const FString UnusableContributingChildren =
			Ownership.UnusableContributingChildRegionDebugPaths.IsEmpty()
				? TEXT("<none>")
				: FString::Join(Ownership.UnusableContributingChildRegionDebugPaths, TEXT(", "));
		const FString ExtraContributingChildren =
			Ownership.ExtraContributingChildRegionDebugPaths.IsEmpty()
				? TEXT("<none>")
				: FString::Join(Ownership.ExtraContributingChildRegionDebugPaths, TEXT(", "));

		return FString::Printf(
			TEXT("Recursive host vertical-access composition could not satisfy the required provider count.\nRequired host provider count: %s %d\nResolved host provider groups: %d\nPotential parent providers after relocation: %d\nCounted parent providers: %d\nCounted child providers: %s\nLocal-only child vertical access (ignored for host counting): %s\nHost-contributing child entries without usable vertical access: %s\nExtra contributing child providers not needed for the current host count: %s\nProblem: The host/root schedule depends on composed parent-plus-child vertical access, but the accepted subtree does not expose the authored counted provider total.\nFix: keep more parent stair capacity in the host plan, mark an eligible child entry as host-contributing, or author enough child providers with usable vertical access to satisfy the host count explicitly."),
			Ownership.bRequiresExactHostProviderCount ? TEXT("exactly") : TEXT("at least"),
			Ownership.RequiredHostProviderCount,
			GetCommittedVerticalAccessProviderCount(Ownership),
			Ownership.PotentialParentProviderCount,
			Ownership.CountedParentProviderCount,
			*CountedChildren,
			*LocalOnlyChildren,
			*UnusableContributingChildren,
			*ExtraContributingChildren);
	}

	FLayoutRecursiveVerticalAccessSummary BuildRecursiveVerticalAccessSummaryFromCommittedOwnership(
		const FCommittedVerticalAccessOwnership& Ownership)
	{
		FLayoutRecursiveVerticalAccessSummary Summary;
		Summary.RequiredHostProviderCount = Ownership.RequiredHostProviderCount;
		Summary.bRequiresExactHostProviderCount =
			Ownership.bRequiresExactHostProviderCount;
		Summary.ResolvedHostProviderCount = Ownership.ResolvedHostProviderCount;
		Summary.CountedParentProviderCount = Ownership.CountedParentProviderCount;
		Summary.CountedParentVerticalAccessCells =
			Ownership.CountedParentVerticalAccessCells;
		Summary.RetainedParentRouteSupportVerticalAccessCells =
			Ownership.RetainedParentRouteSupportVerticalAccessCells;
		Summary.CountedChildRegionDebugPaths = Ownership.CountedChildRegionDebugPaths;
		Summary.LocalOnlyChildRegionDebugPaths = Ownership.LocalOnlyChildRegionDebugPaths;
		Summary.UnusableContributingChildRegionDebugPaths =
			Ownership.UnusableContributingChildRegionDebugPaths;
		Summary.ExtraContributingChildRegionDebugPaths =
			Ownership.ExtraContributingChildRegionDebugPaths;
		Summary.FailureReason = Ownership.FailureReason;
		return Summary;
	}

	FLayoutNegotiatedLevelCellSet& FindOrAddNegotiatedLevelCellSet(
		TArray<FLayoutNegotiatedLevelCellSet>& LevelSets,
		const int32 Level)
	{
		if (FLayoutNegotiatedLevelCellSet* Existing = LevelSets.FindByPredicate([Level](const FLayoutNegotiatedLevelCellSet& Set)
		{
			return Set.Level == Level;
		}))
		{
			return *Existing;
		}

		FLayoutNegotiatedLevelCellSet& Added = LevelSets.AddDefaulted_GetRef();
		Added.Level = Level;
		return Added;
	}

	int32 ResolveChildAuthoredModuleLevel(
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const FIntVector& ChildLocalCell)
	{
		const FLayoutPlannedCell* const PlannedCell = ChildPlannedCells.FindByPredicate(
			[&ChildLocalCell](const FLayoutPlannedCell& Candidate)
			{
				return Candidate.Cell == ChildLocalCell;
			});
		return PlannedCell != nullptr && PlannedCell->ModuleLevelIndex != INDEX_NONE
			? PlannedCell->ModuleLevelIndex
			: ChildLocalCell.Z;
	}

	FLayoutNegotiatedLevelInterfaceContract& FindOrAddNegotiatedLevelInterfaceContract(
		TArray<FLayoutNegotiatedLevelInterfaceContract>& Contracts,
		const int32 Level)
	{
		if (FLayoutNegotiatedLevelInterfaceContract* Existing = Contracts.FindByPredicate([Level](const FLayoutNegotiatedLevelInterfaceContract& Contract)
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

	FLayoutNegotiatedLevelSeamSet& FindOrAddNegotiatedLevelSeamSet(
		TArray<FLayoutNegotiatedLevelSeamSet>& SeamSets,
		const int32 Level)
	{
		if (FLayoutNegotiatedLevelSeamSet* Existing = SeamSets.FindByPredicate([Level](const FLayoutNegotiatedLevelSeamSet& SeamSet)
		{
			return SeamSet.Level == Level;
		}))
		{
			return *Existing;
		}

		FLayoutNegotiatedLevelSeamSet& Added = SeamSets.AddDefaulted_GetRef();
		Added.Level = Level;
		return Added;
	}

	ELayoutNegotiatedHostVerticalAccessResponsibility DetermineNegotiatedHostVerticalAccessResponsibility(
		const FString& ChildRegionDebugPath,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership)
	{
		const bool bChildCountedForHost =
			VerticalAccessOwnership.CountedChildRegionDebugPaths.Contains(ChildRegionDebugPath);
		const bool bParentCountedForHost = VerticalAccessOwnership.CountedParentProviderCount > 0;

		if (bChildCountedForHost && bParentCountedForHost)
		{
			return ELayoutNegotiatedHostVerticalAccessResponsibility::Composed;
		}

		if (bChildCountedForHost)
		{
			return ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
		}

		return ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
	}

	TArray<FLayoutCommittedEndpointAnchor> BuildHostFacingAnchorsForNegotiatedVerticalAccess(
		const FAutomaticChildPlacement& Placement)
	{
		TArray<FLayoutCommittedEndpointAnchor> HostFacingAnchors =
			Placement.DirectChildCommitment.EndpointCommitments;
		auto HasMatchingHostFacingAnchor =
			[&HostFacingAnchors](const FIntVector& LocalCell, const ELayoutFaceDirection FaceDirection, const FGameplayTag ConnectionTag)
			{
				return HostFacingAnchors.ContainsByPredicate(
					[&](const FLayoutCommittedEndpointAnchor& ExistingAnchor)
					{
						return ExistingAnchor.LocalCell == LocalCell
							&& ExistingAnchor.FaceDirection == FaceDirection
							&& ExistingAnchor.ConnectionTag == ConnectionTag;
					});
			};

		// Host-ascent ownership may widen beyond the lower direct contact, but only
		// from exact placement-backed shared parent/child faces. Raw child
		// capability offers are too broad here: they can describe hypothetical
		// perimeter contacts that never became a committed or retained parent-facing
		// interface for this accepted placement.
		for (const FSharedParentChildFace& SharedFace : Placement.SharedParentChildFaces)
		{
			if (SharedFace.InterfaceFamily != LayoutGameplayTags::InterfacePartitionSolid
				&& SharedFace.InterfaceFamily != LayoutGameplayTags::InterfacePartitionDoor)
			{
				continue;
			}

			const FGameplayTag HostConnectionTag =
				SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
					? LayoutGameplayTags::FaceEntry
					: LayoutGameplayTags::FaceOpen;
			const bool bAlreadyPresent = HostFacingAnchors.ContainsByPredicate(
				[&](const FLayoutCommittedEndpointAnchor& ExistingAnchor)
				{
					return ExistingAnchor.LocalCell == SharedFace.ChildLocalCell
						&& ExistingAnchor.FaceDirection == SharedFace.FaceDirection
						&& ExistingAnchor.ConnectionTag == HostConnectionTag;
				});
			if (bAlreadyPresent)
			{
				continue;
			}

			FLayoutCommittedEndpointAnchor& Anchor = HostFacingAnchors.AddDefaulted_GetRef();
			Anchor.CommitmentId = FLayoutId(*FString::Printf(
				TEXT("%s.SharedHost.%s.%d.%s"),
				*Placement.ChildRegionDebugPath,
				*SharedFace.ChildLocalCell.ToString(),
				static_cast<int32>(SharedFace.FaceDirection),
				SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
					? TEXT("Door")
					: TEXT("Solid")));
			Anchor.LocalCell = SharedFace.ChildLocalCell;
			Anchor.FaceDirection = SharedFace.FaceDirection;
			Anchor.ConnectionTag = HostConnectionTag;
			Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
			if (SharedFace.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor)
			{
				Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);
			}
			else
			{
				Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
			}
			Anchor.bRequireMatchingYawWithFilledNeighbor = false;
		}

		return HostFacingAnchors;
	}

	FString DescribeHostFacingAnchorLevelsForNegotiatedVerticalAccess(
		const TArray<FLayoutCommittedEndpointAnchor>& HostFacingAnchors)
	{
		TMap<int32, int32> AnchorCountByLevel;
		for (const FLayoutCommittedEndpointAnchor& Anchor : HostFacingAnchors)
		{
			++AnchorCountByLevel.FindOrAdd(Anchor.LocalCell.Z);
		}

		TArray<FString> LevelParts;
		AnchorCountByLevel.KeySort(TLess<int32>());
		for (const TPair<int32, int32>& Pair : AnchorCountByLevel)
		{
			LevelParts.Add(FString::Printf(TEXT("L%d=%d"), Pair.Key, Pair.Value));
		}

		return LevelParts.IsEmpty()
			? TEXT("<none>")
			: FString::Join(LevelParts, TEXT(","));
	}

	FString DescribeSharedParentChildFaceLevelsForNegotiatedVerticalAccess(
		const TArray<FSharedParentChildFace>& SharedFaces)
	{
		TMap<int32, int32> SharedFaceCountByLevel;
		for (const FSharedParentChildFace& SharedFace : SharedFaces)
		{
			++SharedFaceCountByLevel.FindOrAdd(SharedFace.ChildLocalCell.Z);
		}

		TArray<FString> LevelParts;
		SharedFaceCountByLevel.KeySort(TLess<int32>());
		for (const TPair<int32, int32>& Pair : SharedFaceCountByLevel)
		{
			LevelParts.Add(FString::Printf(TEXT("L%d=%d"), Pair.Key, Pair.Value));
		}

		return LevelParts.IsEmpty()
			? TEXT("<none>")
			: FString::Join(LevelParts, TEXT(","));
	}

	bool TrySelectNegotiatedHostVerticalAccessAnchors(
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessLocalCells,
		FLayoutCommittedEndpointAnchor& OutIngressAnchor,
		FLayoutCommittedEndpointAnchor& OutEgressAnchor,
		TArray<FIntVector>& OutRouteCells,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		OutRouteCells.Reset();
		int32 LowestAnchorLevel = MAX_int32;
		int32 HighestAnchorLevel = MIN_int32;
		for (const FLayoutCommittedEndpointAnchor& Anchor : CandidateCommitments)
		{
			LowestAnchorLevel = FMath::Min(LowestAnchorLevel, Anchor.LocalCell.Z);
			HighestAnchorLevel = FMath::Max(HighestAnchorLevel, Anchor.LocalCell.Z);
		}

		if (LowestAnchorLevel == MAX_int32 || HighestAnchorLevel == MIN_int32)
		{
			OutFailureReason = TEXT("No committed host-facing child anchors were available after entry negotiation.");
			return false;
		}

		if (LowestAnchorLevel == HighestAnchorLevel)
		{
			OutFailureReason = FString::Printf(
				TEXT("Negotiation only found host-facing child anchors on level %d, so it could not commit distinct lower and upper host contacts."),
				LowestAnchorLevel);
			return false;
		}

		if (ChildVerticalAccessLocalCells.IsEmpty())
		{
			OutFailureReason = TEXT("The child exposes no VerticalAccess cells, so it cannot prove an internal ascent route between host-facing anchors.");
			return false;
		}

		auto IsTraversableIntentForNegotiatedVerticalRoute = [](const ELayoutCellIntent Intent)
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
		};

		TSet<FIntVector> RouteEligibleCells;
		for (const FLayoutPlannedCell& PlannedCell : ChildPlannedCells)
		{
			if (IsTraversableIntentForNegotiatedVerticalRoute(PlannedCell.Intent))
			{
				RouteEligibleCells.Add(PlannedCell.Cell);
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
				TArray<FIntVector>& CandidateRouteCells,
				FString& CandidateFailureReason)
			{
				CandidateRouteCells.Reset();
				CandidateFailureReason.Reset();
				if (!IsAnchorOnChildPlannedCell(CandidateIngressAnchor)
					|| !IsAnchorOnChildPlannedCell(CandidateEgressAnchor))
				{
					CandidateFailureReason = TEXT("Committed host-facing child anchors do not both land on child planned cells.");
					return false;
				}

				TSet<FIntVector> CandidateRouteEligibleCells = RouteEligibleCells;
				CandidateRouteEligibleCells.Add(CandidateIngressAnchor.LocalCell);
				CandidateRouteEligibleCells.Add(CandidateEgressAnchor.LocalCell);

				TArray<FIntVector> Frontier = { CandidateIngressAnchor.LocalCell };
				TSet<FIntVector> Visited = { CandidateIngressAnchor.LocalCell };
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
						const FIntVector NeighborCell = CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (!CandidateRouteEligibleCells.Contains(NeighborCell) || Visited.Contains(NeighborCell))
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
					CandidateFailureReason = TEXT("The child could not prove a traversable internal route between the negotiated lower and upper host-facing anchors.");
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
						CandidateFailureReason = TEXT("The child route proof could not reconstruct the negotiated internal vertical path.");
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

					const FIntVector& PreviousRouteCell = CandidateRouteCells[RouteIndex - 1];
					const FIntVector& CurrentRouteCell = CandidateRouteCells[RouteIndex];
					if (PreviousRouteCell.Z == CurrentRouteCell.Z)
					{
						continue;
					}

					if (!VerticalAccessCells.Contains(PreviousRouteCell)
						&& !VerticalAccessCells.Contains(CurrentRouteCell))
					{
						CandidateRouteCells.Reset();
						CandidateFailureReason = TEXT("The negotiated child route changed levels without traversing a VerticalAccess child cell.");
						return false;
					}
				}

				if (!bRouteTouchesVerticalAccess)
				{
					CandidateRouteCells.Reset();
					CandidateFailureReason = TEXT("The child route between negotiated host-facing anchors never traversed a VerticalAccess child cell.");
					return false;
				}

				return true;
			};

		FString LastRouteFailureReason;
		for (const FLayoutCommittedEndpointAnchor* CandidateIngressAnchor : LowestAnchors)
		{
			if (CandidateIngressAnchor == nullptr)
			{
				continue;
			}

			for (const FLayoutCommittedEndpointAnchor* CandidateEgressAnchor : HighestAnchors)
			{
				if (CandidateEgressAnchor == nullptr)
				{
					continue;
				}

				if (TryBuildRouteBetweenAnchors(
					*CandidateIngressAnchor,
					*CandidateEgressAnchor,
					OutRouteCells,
					LastRouteFailureReason))
				{
					OutIngressAnchor = *CandidateIngressAnchor;
					OutEgressAnchor = *CandidateEgressAnchor;
					return true;
				}
			}
		}

		OutFailureReason = LastRouteFailureReason.IsEmpty()
			? TEXT("The child could not prove a traversable internal route between the negotiated lower and upper host-facing anchors.")
			: LastRouteFailureReason;
		return false;
	}

	bool TryPromoteChildStageMappingForCommittedRoute(
		FLayoutChildStageMappingResult& InOutMapping,
		const TArray<FLayoutCommittedEndpointAnchor>& Commitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessCells,
		TArray<FIntVector>& OutRequiredVerticalRouteCells,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (InOutMapping.StageClass != ELayoutChildPlacementStageClass::InheritedStage)
		{
			return true;
		}

		TSet<int32> CommittedRouteStages;
		for (const FLayoutCommittedEndpointAnchor& Commitment : Commitments)
		{
			for (const FLayoutChildStageMappedCell& MappedCell : InOutMapping.Cells)
			{
				if (MappedCell.SourceChildCell.X == Commitment.LocalCell.X
					&& MappedCell.SourceChildCell.Y == Commitment.LocalCell.Y
					&& MappedCell.TerrainStageIndex != INDEX_NONE)
				{
					CommittedRouteStages.Add(MappedCell.TerrainStageIndex);
				}
			}
		}
		if (CommittedRouteStages.Num() <= 1)
		{
			return true;
		}

		FLayoutCommittedEndpointAnchor IngressAnchor;
		FLayoutCommittedEndpointAnchor EgressAnchor;
		if (!TrySelectNegotiatedHostVerticalAccessAnchors(
			Commitments,
			ChildPlannedCells,
			ChildVerticalAccessCells,
			IngressAnchor,
			EgressAnchor,
			OutRequiredVerticalRouteCells,
			OutFailureReason))
		{
			OutFailureReason = FString::Printf(
				TEXT("Cross-stage child route requires internal VerticalAccess regardless of host contribution. %s"),
				*OutFailureReason);
			return false;
		}

		InOutMapping.StageClass = ELayoutChildPlacementStageClass::CrossStage;
		InOutMapping.MappingId = FLayoutId(*FString::Printf(
			TEXT("%s.CrossStage"),
			*InOutMapping.MappingId.ToString()));
		InOutMapping.ChildLocalTerrainContract.ContractId = InOutMapping.MappingId;
		return true;
	}

	bool DoesPlacementProvideHostFacingVerticalAccess(
		const FAutomaticChildPlacement& Placement,
		FString* OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		if (!DoesPlacementSupportCommittedVerticalAccess(Placement))
		{
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = TEXT("Placement does not support committed vertical access.");
			}
			return false;
		}

		const TArray<FLayoutCommittedEndpointAnchor> HostFacingAnchors =
			BuildHostFacingAnchorsForNegotiatedVerticalAccess(Placement);
		FLayoutCommittedEndpointAnchor IngressAnchor;
		FLayoutCommittedEndpointAnchor EgressAnchor;
		TArray<FIntVector> RouteCells;
		FString FailureReason;
		const bool bSucceeded = TrySelectNegotiatedHostVerticalAccessAnchors(
			HostFacingAnchors,
			Placement.ChildRequest.PlannedCells,
			Placement.ChildVerticalAccessLocalCells,
			IngressAnchor,
			EgressAnchor,
			RouteCells,
			FailureReason);
		if (!bSucceeded && OutFailureReason != nullptr)
		{
			*OutFailureReason = FString::Printf(
				TEXT("%s Host-facing anchors by level: [%s]"),
				*FailureReason,
				*DescribeHostFacingAnchorLevelsForNegotiatedVerticalAccess(HostFacingAnchors));
		}
		return bSucceeded;
	}

	FLayoutNegotiatedChildResponsibilityContract BuildNegotiatedChildResponsibilityContract(
		const FLayoutRegionSolveRequest& RootRequest,
		const FAutomaticChildPlacement& Placement,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership)
	{
		FLayoutNegotiatedChildResponsibilityContract Contract;
		Contract.ParentRegionDebugPath = RootRequest.RegionDebugPath;
		Contract.ChildRegionDebugPath = Placement.ChildRegionDebugPath;
		Contract.HostVerticalAccessResponsibility =
			DetermineNegotiatedHostVerticalAccessResponsibility(Placement.ChildRegionDebugPath, VerticalAccessOwnership);
		Contract.RequiredHostProviderCount = VerticalAccessOwnership.RequiredHostProviderCount;
		Contract.CountedParentProviderCount = VerticalAccessOwnership.CountedParentProviderCount;
		Contract.CountedParentVerticalAccessCells = VerticalAccessOwnership.CountedParentVerticalAccessCells;
		Contract.RetainedParentRouteSupportVerticalAccessCells = VerticalAccessOwnership.RetainedParentRouteSupportVerticalAccessCells;
		Contract.CountedChildProviderRegionDebugPaths = VerticalAccessOwnership.CountedChildRegionDebugPaths;

		for (const FIntVector& ReservedCell : Placement.ParentPlanReservedCells)
		{
			FindOrAddNegotiatedLevelCellSet(Contract.ReplacementVolumeByLevel, ReservedCell.Z).Cells.AddUnique(ReservedCell);
		}

		for (const FSharedParentChildFace& SharedFace : Placement.SharedParentChildFaces)
		{
			if (Placement.ParentPlanReservedCells.Contains(SharedFace.ParentCell))
			{
				continue;
			}

			FindOrAddNegotiatedLevelCellSet(Contract.RetainedParentShellCellsByLevel, SharedFace.ParentCell.Z).Cells.AddUnique(SharedFace.ParentCell);
		}
		for (const FLayoutCommittedEndpointAnchor& Anchor : Placement.DirectChildCommitment.EndpointCommitments)
		{
			const int32 AuthoredLevel = ResolveChildAuthoredModuleLevel(
				Placement.ChildRequest.PlannedCells,
				Anchor.LocalCell);
			FindOrAddNegotiatedLevelInterfaceContract(
				Contract.CommittedParentChildInterfacesByLevel,
				AuthoredLevel).EndpointAnchors.Add(Anchor);
		}

		for (const FLayoutCommittedTraversalAnchor& Anchor : Placement.ParentCommittedTraversalAnchors)
		{
			FindOrAddNegotiatedLevelInterfaceContract(Contract.CommittedParentChildInterfacesByLevel, Anchor.Cell.Z).TraversalAnchors.Add(Anchor);
		}

		for (const FLayoutPartitionSeamRecord& SeamRecord : PlannedPartitionSeams)
		{
			if (SeamRecord.OwnerRegionDebugPath != Placement.ChildRegionDebugPath
				&& SeamRecord.PassiveRegionDebugPath != Placement.ChildRegionDebugPath)
			{
				continue;
			}

			const int32 SeamLevel =
				SeamRecord.OwnerRegionDebugPath == Placement.ChildRegionDebugPath
					? SeamRecord.OwnerStartCell.Z
					: SeamRecord.PassiveStartCell.Z;
			FindOrAddNegotiatedLevelSeamSet(Contract.CommittedSiblingInterfacesByLevel, SeamLevel).Seams.Add(SeamRecord);

			const bool bOwnerIsChild = SeamRecord.OwnerRegionDebugPath == Placement.ChildRegionDebugPath;
			const bool bPassiveIsChild = SeamRecord.PassiveRegionDebugPath == Placement.ChildRegionDebugPath;
			const bool bOwnerIsParent = SeamRecord.OwnerRegionDebugPath == RootRequest.RegionDebugPath;
			const bool bPassiveIsParent = SeamRecord.PassiveRegionDebugPath == RootRequest.RegionDebugPath;
			if ((!bOwnerIsChild && !bPassiveIsChild)
				|| (!bOwnerIsParent && !bPassiveIsParent))
			{
				continue;
			}

			const FIntVector OwnerStep(
				FMath::Clamp(SeamRecord.OwnerEndCell.X - SeamRecord.OwnerStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Y - SeamRecord.OwnerStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.OwnerEndCell.Z - SeamRecord.OwnerStartCell.Z, -1, 1));
			const FIntVector PassiveStep(
				FMath::Clamp(SeamRecord.PassiveEndCell.X - SeamRecord.PassiveStartCell.X, -1, 1),
				FMath::Clamp(SeamRecord.PassiveEndCell.Y - SeamRecord.PassiveStartCell.Y, -1, 1),
				FMath::Clamp(SeamRecord.PassiveEndCell.Z - SeamRecord.PassiveStartCell.Z, -1, 1));

			for (int32 SegmentIndex = 0; SegmentIndex < FMath::Max(SeamRecord.SegmentCount, 1); ++SegmentIndex)
			{
				const FIntVector OwnerCell = SeamRecord.OwnerStartCell + OwnerStep * SegmentIndex;
				const FIntVector PassiveCell = SeamRecord.PassiveStartCell + PassiveStep * SegmentIndex;
				const FIntVector ParentCell = bOwnerIsParent ? OwnerCell : PassiveCell;
				const FIntVector ChildWorldCell = bOwnerIsChild ? OwnerCell : PassiveCell;
				const ELayoutFaceDirection ChildFaceDirection =
					bOwnerIsChild ? SeamRecord.OwnerFaceDirection : SeamRecord.PassiveFaceDirection;

				if (SeamRecord.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor)
				{
					const FIntVector ChildLocalCell = MapParentCellToChildResultCell(
						ChildWorldCell,
						Placement.RegionCellOffset,
						&Placement.StageMapping);
					const int32 AuthoredLevel = ResolveChildAuthoredModuleLevel(
						Placement.ChildRequest.PlannedCells,
						ChildLocalCell);
					FLayoutNegotiatedLevelInterfaceContract& InterfaceContract =
						FindOrAddNegotiatedLevelInterfaceContract(
							Contract.CommittedParentChildInterfacesByLevel,
							AuthoredLevel);
					const bool bAlreadyPresent = InterfaceContract.EndpointAnchors.ContainsByPredicate(
						[&](const FLayoutCommittedEndpointAnchor& ExistingAnchor)
						{
							return ExistingAnchor.LocalCell == ChildLocalCell
								&& ExistingAnchor.FaceDirection == ChildFaceDirection
								&& ExistingAnchor.ConnectionTag == LayoutGameplayTags::FaceEntry;
						});
					if (!bAlreadyPresent)
					{
						FLayoutCommittedEndpointAnchor& Anchor =
							InterfaceContract.EndpointAnchors.AddDefaulted_GetRef();
						Anchor.CommitmentId = FLayoutId(*FString::Printf(
							TEXT("%s.SeamDoor.%s.%d"),
							*Placement.ChildRegionDebugPath,
							*ChildLocalCell.ToString(),
							static_cast<int32>(ChildFaceDirection)));
						Anchor.LocalCell = ChildLocalCell;
						Anchor.FaceDirection = ChildFaceDirection;
						Anchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
						Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);
						Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
						Anchor.bRequireMatchingYawWithFilledNeighbor = false;
					}
				}
				else
				{
					if (Placement.ParentPlanReservedCells.Contains(ParentCell))
					{
						continue;
					}

					FindOrAddNegotiatedLevelCellSet(
						Contract.RetainedParentShellCellsByLevel,
						ParentCell.Z).Cells.AddUnique(ParentCell);
				}
			}
		}

		if (Contract.HostVerticalAccessResponsibility != ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned)
		{
			FString AnchorFailureReason;
			const TArray<FLayoutCommittedEndpointAnchor> HostFacingAnchors =
				BuildHostFacingAnchorsForNegotiatedVerticalAccess(Placement);
			if (TrySelectNegotiatedHostVerticalAccessAnchors(
				HostFacingAnchors,
				Placement.ChildRequest.PlannedCells,
				Placement.ChildVerticalAccessLocalCells,
				Contract.RequiredHostIngressAnchor,
				Contract.RequiredHostEgressAnchor,
				Contract.RequiredChildInternalVerticalRouteCells,
				AnchorFailureReason))
			{
				Contract.bHasRequiredHostIngressAnchor = true;
				Contract.bHasRequiredHostEgressAnchor = true;
			}
		}

		return Contract;
	}

	// Refresh bridge-owned contracts from the same component-aware negotiation
	// surface the ordinary recursive solver uses, then merge the result back
	// onto the richer bridge contract so retained shell and seam evidence do not
	// drift from host-ascent ownership/count proof.
	LayoutRegionScheduleSolverFacade::FPlacementCapabilityBundle BuildPlacementBackedNegotiationBundle(
		const FAutomaticChildPlacement& Placement)
	{
		LayoutRegionScheduleSolverFacade::FPlacementCapabilityBundle Bundle;
		Bundle.BundleId = FLayoutId(*FString::Printf(
			TEXT("RequiredPlacementBridge.%s"),
			*Placement.ChildRegionDebugPath));

		TSet<int32> CoveredLevels;
		for (const FLayoutPlannedCell& PlannedCell : Placement.ChildRequest.PlannedCells)
		{
			Bundle.OccupiedLocalCells.AddUnique(PlannedCell.Cell);
			CoveredLevels.Add(PlannedCell.Cell.Z);
		}

		for (const int32 Level : CoveredLevels)
		{
			Bundle.CoveredLevels.Add(Level);
		}
		Bundle.CoveredLevels.Sort();
		return Bundle;
	}

	bool TryResolvePlacementBackedParentComponentId(
		const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary& ParentSummary,
		const FAutomaticChildPlacement& Placement,
		int32& OutParentComponentId)
	{
		OutParentComponentId = INDEX_NONE;
		for (const FIntVector& ParentContactCell : Placement.ParentCommittedEntryCells)
		{
			const int32* ParentComponentId =
				ParentSummary.RootConnectedComponentIdByCell.Find(ParentContactCell);
			if (ParentComponentId == nullptr)
			{
				continue;
			}

			if (OutParentComponentId == INDEX_NONE)
			{
				OutParentComponentId = *ParentComponentId;
				continue;
			}

			if (OutParentComponentId != *ParentComponentId)
			{
				return false;
			}
		}

		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
			Placement.ParentCommittedTraversalAnchors)
		{
			const int32* ParentComponentId =
				ParentSummary.RootConnectedComponentIdByCell.Find(TraversalAnchor.Cell);
			if (ParentComponentId == nullptr)
			{
				continue;
			}

			if (OutParentComponentId == INDEX_NONE)
			{
				OutParentComponentId = *ParentComponentId;
				continue;
			}

			if (OutParentComponentId != *ParentComponentId)
			{
				return false;
			}
		}

		return true;
	}

	LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily
		BuildPlacementBackedNegotiationContactSet(
			const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary& ParentSummary,
			const FAutomaticChildPlacement& Placement)
	{
		LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily ContactSet;
		ContactSet.FamilyId = FLayoutId(*FString::Printf(
			TEXT("RequiredPlacementBridge.%s"),
			*Placement.ChildRegionDebugPath));
		ContactSet.EndpointCommitments =
			Placement.DirectChildCommitment.EndpointCommitments;
		ContactSet.ParentContactCells = Placement.ParentCommittedEntryCells;
		ContactSet.AllCommittedContactTraversalAnchors =
			Placement.ParentCommittedTraversalAnchors;
		ContactSet.bAllowsChildTraversalBridge =
			Placement.bAllowsChildTraversalBridgeForCommittedContacts;
		ContactSet.PlacementBundle =
			BuildPlacementBackedNegotiationBundle(Placement);
		// Required-placement bridge commitments preserve the chosen parent contact
		// cells and ingress anchors, but they do not retain one authoritative
		// parent-face commitment set equivalent to the generic capability-backed
		// negotiation families. Do not reconstruct guessed parent faces here:
		// shared confirmation can still validate the contact cells, component,
		// ingress anchors, provider counts, bundle coverage, and child anchor
		// proof without over-constraining the bridge on an invented face surface.

		const bool bHasStableParentComponentId =
			TryResolvePlacementBackedParentComponentId(
				ParentSummary,
				Placement,
				ContactSet.ParentComponentId);
		if (!bHasStableParentComponentId)
		{
			ContactSet.ParentComponentId = INDEX_NONE;
		}

		LayoutRegionScheduleSolverFacade::SelectParentTraversalIngressSubset(
			ParentSummary,
			ContactSet);
		return ContactSet;
	}

	TSet<int32> CollectPlacementBackedParentRouteSeedComponents(
		const FLayoutRegionSolveRequest& RootRequest,
		const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary& ParentSummary)
	{
		TSet<int32> RouteSeedComponents;

		auto TryAppendComponent =
			[&ParentSummary, &RouteSeedComponents](const FIntVector& Cell)
			{
				if (const int32* ComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(Cell))
				{
					RouteSeedComponents.Add(*ComponentId);
				}
			};

		for (const FLayoutPlannedCell& PlannedCell : RootRequest.PlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				TryAppendComponent(PlannedCell.Cell);
			}
		}

		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
			RootRequest.CommittedTraversalAnchors)
		{
			TryAppendComponent(TraversalAnchor.Cell);
		}

		for (const FIntVector& ProtectedCell : RootRequest.ProtectedStructuralCells)
		{
			TryAppendComponent(ProtectedCell);
		}

		for (const FLayoutCommittedEndpointAnchor& EndpointAnchor :
			RootRequest.CommittedEndpointAnchors)
		{
			TryAppendComponent(EndpointAnchor.LocalCell);
		}

		for (const FIntVector& ParentVerticalAccessRouteCell :
			ParentSummary.CountedParentVerticalAccessCells)
		{
			TryAppendComponent(ParentVerticalAccessRouteCell);
		}

		return RouteSeedComponents;
	}

	void CollectPlacementBackedBoundedParentRouteSeedCells(
		const FLayoutRegionSolveRequest& RootRequest,
		const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary& ParentSummary,
		const int32 ParentComponentId,
		TArray<FIntVector>& OutRouteSeedCells)
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

		for (const FIntVector& ParentVerticalAccessRouteCell :
			ParentSummary.CountedParentVerticalAccessCells)
		{
			TryAppendRouteSeedCell(ParentVerticalAccessRouteCell);
		}
	}

	LayoutRegionScheduleSolverFacade::FBoundedParentProofEvidence
		BuildPlacementBackedBoundedParentProofEvidence(
			const FLayoutRegionSolveRequest& RootRequest,
			const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary& ParentSummary,
			const LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily& ContactSet,
			const FLayoutNegotiatedChildResponsibilityContract& Contract)
	{
		LayoutRegionScheduleSolverFacade::FBoundedParentProofEvidence Evidence;
		Evidence.ChildRegionDebugPath = Contract.ChildRegionDebugPath;
		Evidence.ParentComponentId = ContactSet.ParentComponentId;
		CollectPlacementBackedBoundedParentRouteSeedCells(
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

		for (const LayoutRegionScheduleSolverFacade::FResidualExternalEndpointReachability& Reachability :
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

		Evidence.ConfirmedParentRouteSeedCells.Sort([](
			const FIntVector& Left,
			const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		});
		Evidence.ConfirmedParentContactCells.Sort([](
			const FIntVector& Left,
			const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		});
		Evidence.ConfirmedParentTraversalIngressCells.Sort([](
			const FIntVector& Left,
			const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		});
		Evidence.ConfirmedCountedParentProviderCells.Sort([](
			const FIntVector& Left,
			const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		});
		Evidence.ConfirmedRequiredChildBundleSupportCells.Sort([](
			const FIntVector& Left,
			const FIntVector& Right)
		{
			return Left.Z != Right.Z ? Left.Z < Right.Z
				: Left.Y != Right.Y ? Left.Y < Right.Y
				: Left.X < Right.X;
		});
		Evidence.PreservedRootExternalEndpointCommitmentIds.Sort([](
			const FLayoutId& Left,
			const FLayoutId& Right)
		{
			return Left.ToString() < Right.ToString();
		});
		return Evidence;
	}

	bool TryConfirmPlacementBackedNegotiatedContract(
		const FLayoutRegionSolveRequest& RootRequest,
		const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary& ParentSummary,
		LayoutRegionScheduleSolverFacade::FNegotiatedResponsibilitySet& InOutResponsibilitySet,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		const LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily& ContactSet =
			InOutResponsibilitySet.ContactSet;
		const FLayoutNegotiatedChildResponsibilityContract& Contract =
			InOutResponsibilitySet.ResponsibilityContract;

		if (!ValidateNegotiatedChildResponsibilityContract(
			Contract,
			OutFailureReason))
		{
			return false;
		}

		const TSet<int32> ParentRouteSeedComponents =
			CollectPlacementBackedParentRouteSeedComponents(
				RootRequest,
				ParentSummary);
		if (!ParentRouteSeedComponents.IsEmpty()
			&& ContactSet.ParentComponentId != INDEX_NONE
			&& !ParentRouteSeedComponents.Contains(ContactSet.ParentComponentId))
		{
			OutFailureReason = FString::Printf(
				TEXT("Placement-backed parent contact family component %d is root-connected only through a disconnected residual branch and does not intersect the current parent route seed network."),
				ContactSet.ParentComponentId);
			return false;
		}

		const bool bParentOwnedChildContract =
			Contract.HostVerticalAccessResponsibility
			== ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
		for (const FIntVector& ParentContactCell : ContactSet.ParentContactCells)
		{
			// Parent-owned side-room contracts can retain shell/seam contact cells that
			// are not themselves route seeds; traversal anchors below still need to be
			// root-connected. Requiring every contact cell to be root-connected rejects
			// valid authored castle room seams.
			if (bParentOwnedChildContract)
			{
				continue;
			}

			if (!ParentSummary.RootConnectedTraversableCells.Contains(
				ParentContactCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Placement-backed parent contact cell %s is not root-connected in the residual parent summary."),
					*ParentContactCell.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(
						ParentContactCell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Placement-backed parent contact cell %s does not belong to the selected residual parent component %d."),
						*ParentContactCell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
			ContactSet.ParentTraversalIngressAnchors)
		{
			if (bParentOwnedChildContract)
			{
				continue;
			}

			if (!ParentSummary.RootConnectedTraversableCells.Contains(
				TraversalAnchor.Cell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Placement-backed parent traversal-ingress anchor at cell %s is not root-connected in the residual parent summary."),
					*TraversalAnchor.Cell.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(
						TraversalAnchor.Cell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Placement-backed parent traversal-ingress anchor at cell %s does not belong to the selected residual parent component %d."),
						*TraversalAnchor.Cell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		const TArray<FIntVector>& ParentCountedProviderSource =
			ParentSummary.CountedParentVerticalAccessCells.IsEmpty()
				? ParentSummary.RouteSupportVerticalAccessCells
				: ParentSummary.CountedParentVerticalAccessCells;
		for (const FIntVector& CountedParentProviderCell :
			Contract.CountedParentVerticalAccessCells)
		{
			if (!ParentCountedProviderSource.Contains(
				CountedParentProviderCell))
			{
				OutFailureReason = FString::Printf(
					TEXT("Placement-backed counted parent provider cell %s is not present in the residual parent counted provider set."),
					*CountedParentProviderCell.ToString());
				return false;
			}

			if (ContactSet.ParentComponentId != INDEX_NONE)
			{
				const int32* ParentComponentId =
					ParentSummary.RootConnectedComponentIdByCell.Find(
						CountedParentProviderCell);
				if (ParentComponentId == nullptr
					|| *ParentComponentId != ContactSet.ParentComponentId)
				{
					OutFailureReason = FString::Printf(
						TEXT("Placement-backed counted parent provider cell %s does not belong to the selected residual parent component %d."),
						*CountedParentProviderCell.ToString(),
						ContactSet.ParentComponentId);
					return false;
				}
			}
		}

		InOutResponsibilitySet.BoundedParentProofEvidence =
			BuildPlacementBackedBoundedParentProofEvidence(
				RootRequest,
				ParentSummary,
				ContactSet,
				Contract);
		return true;
	}

	bool TryRefreshPlacementBackedNegotiatedContract(
		const LayoutRegionScheduleSolverFacade::FRecursiveScheduleSolveContext& SolveContext,
		const LayoutRegionScheduleSolverFacade::FCompiledStructuralInputs& StructuralInputs,
		FAutomaticChildPlacement& InOutPlacement,
		LayoutRegionScheduleSolverFacade::FNegotiatedDemandResult& OutDemandResult,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		OutDemandResult = LayoutRegionScheduleSolverFacade::FNegotiatedDemandResult();
		OutDemandResult.ChildRegionDebugPath = InOutPlacement.ChildRegionDebugPath;
		OutDemandResult.ChildRequest = InOutPlacement.ChildRequest;
		const bool bHasReciprocalBoundaryObligation =
			!InOutPlacement.ParentDirectContactCommitments.IsEmpty()
			|| !InOutPlacement.SharedParentChildFaces.IsEmpty();
		if (bHasReciprocalBoundaryObligation
			&& InOutPlacement.BoundaryWitnessIds.IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("Child placement '%s' reached contract refresh with reciprocal boundary obligations but no frozen boundary witness."),
				*InOutPlacement.ChildRegionDebugPath);
			return false;
		}

		FLayoutRegionSolveRequest CapabilityBackedChildRequest =
			InOutPlacement.ChildRequest;
		CapabilityBackedChildRequest.bUseSuppliedChildCapabilityEnvelope = true;
		CapabilityBackedChildRequest.SuppliedChildCapabilityEnvelope =
			InOutPlacement.ChildCapabilityEnvelope;
		CapabilityBackedChildRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
			InOutPlacement.ChildRegionDebugPath;

		const TArray<LayoutRegionScheduleSolverFacade::FPlacementCapabilityBundle> PlacementBundles = {
			BuildPlacementBackedNegotiationBundle(InOutPlacement)};
		const bool bSupportsHostVerticalAccess =
			InOutPlacement.bContributesHostVerticalAccess
			&& !InOutPlacement.ChildVerticalAccessLocalCells.IsEmpty();
		const LayoutRegionScheduleSolverFacade::FChildCapabilitySummary ChildSummary =
			LayoutRegionScheduleSolverFacade::BuildChildCapabilitySummary(
				InOutPlacement.ChildRegionDebugPath,
				InOutPlacement.ChildCapabilityEnvelope,
				InOutPlacement.ChildVerticalAccessLocalCells,
				bSupportsHostVerticalAccess,
				CapabilityBackedChildRequest.bSourceContentEntryOptional,
				PlacementBundles,
				InOutPlacement.SharedParentChildFaces);
		const LayoutRegionScheduleSolverFacade::FNegotiationDemandPlan DemandPlan =
			LayoutRegionScheduleSolverFacade::BuildNegotiationDemandPlan(
				InOutPlacement.ChildRegionDebugPath,
				CapabilityBackedChildRequest,
				ChildSummary,
				InOutPlacement.ParentPlanReservedCells,
				ELayoutPlacementZone::Any,
				ELayoutLevelPlacementPolicy::AnyLevel,
				0,
				LayoutRegionScheduleSolverFacade::ETerrainStageEligibility::Active,
				false,
				false,
				false,
				TOptional<int32>(),
				INDEX_NONE,
				INDEX_NONE);
		const LayoutRegionScheduleSolverFacade::FResidualParentCapabilitySummary ParentSummary =
			LayoutRegionScheduleSolverFacade::RebuildResidualParentSummaryForDemand(
				StructuralInputs,
				DemandPlan);
		LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily ContactSet =
			BuildPlacementBackedNegotiationContactSet(
				ParentSummary,
				InOutPlacement);
		LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily
			OwnershipContactSet = ContactSet;
		OwnershipContactSet.EndpointCommitments =
			BuildHostFacingAnchorsForNegotiatedVerticalAccess(InOutPlacement);
		LayoutRegionScheduleSolverFacade::FNegotiatedResponsibilitySet ResponsibilitySet =
			LayoutRegionScheduleSolverFacade::BuildResponsibilitySetFromContactSet(
				SolveContext,
				DemandPlan,
				ParentSummary,
				OwnershipContactSet);
		LayoutRegionScheduleSolverFacade::FContactBackedPlacementFamily
			ConfirmationContactSet = ContactSet;
		// Placement-backed refresh still owns one direct parent-contact surface for
		// bounded parent proof, but negotiated host-ascent confirmation must see the
		// widened placement-backed host-facing endpoint family too. Otherwise upper
		// shared host contacts can prove responsibility during negotiation and then
		// get dropped before contract confirmation.
		ConfirmationContactSet.EndpointCommitments =
			OwnershipContactSet.EndpointCommitments;
		ResponsibilitySet.ContactSet = MoveTemp(ConfirmationContactSet);

		const FLayoutNegotiatedChildResponsibilityContract& ExistingContract =
			InOutPlacement.NegotiatedResponsibilityContract;
		const FLayoutNegotiatedChildResponsibilityContract& NegotiatedContract =
			ResponsibilitySet.ResponsibilityContract;
		FLayoutNegotiatedChildResponsibilityContract RefreshedContract =
			NegotiatedContract;
		// Placement-backed refresh should rebuild the authoritative parent/child
		// contract from the current negotiation surface, but sibling seam
		// commitments are carried separately and are not reconstructed by the
		// contact-family responsibility builder.
		RefreshedContract.CommittedSiblingInterfacesByLevel =
			ExistingContract.CommittedSiblingInterfacesByLevel;

		ResponsibilitySet.ResponsibilityContract = RefreshedContract;
		// Placement-backed refresh is now contract-authoritative for final host
		// ownership/count semantics too. Do not strip authored count requirements
		// here: refreshed contracts that overcount host providers must be rejected
		// and backtracked instead of being reinterpreted later by a parallel
		// ownership summary path.
		if (!TryConfirmPlacementBackedNegotiatedContract(
			SolveContext.RootRequest,
			ParentSummary,
			ResponsibilitySet,
			OutFailureReason))
		{
			return false;
		}

		InOutPlacement.NegotiatedResponsibilityContract =
			ResponsibilitySet.ResponsibilityContract;
		InOutPlacement.DirectChildCommitment.NegotiatedResponsibilityContract =
			InOutPlacement.NegotiatedResponsibilityContract;
		InOutPlacement.ChildRequest = CapabilityBackedChildRequest;
		InOutPlacement.ChildRequest.CandidateDomainRestrictions =
			InOutPlacement.ChildDomainRestrictions;
		InOutPlacement.ChildRequest.CandidateDomainCertificateId =
			InOutPlacement.BoundaryCertificateId;
		InOutPlacement.ChildRequest.IncomingBoundaryPoints =
			InOutPlacement.CertifiedChildIncomingBoundaryPoints;
		InOutPlacement.ChildRequest.NegotiatedChildResponsibilityContracts = {
			InOutPlacement.NegotiatedResponsibilityContract};

		OutDemandResult.ChildRequest = InOutPlacement.ChildRequest;
		OutDemandResult.CapabilityWitness.SourceContentEntryId = InOutPlacement.EntryId;
		OutDemandResult.CapabilityWitness.ChildRegionDebugPath = InOutPlacement.ChildRegionDebugPath;
		OutDemandResult.CapabilityWitness.SelectedBoundaryWitnessIds =
			InOutPlacement.BoundaryWitnessIds;
		OutDemandResult.ResponsibilitySet = ResponsibilitySet;
		OutDemandResult.ParentSummary = ParentSummary;
		OutDemandResult.StageMapping = InOutPlacement.StageMapping;
		OutDemandResult.BoundaryCertificateId = InOutPlacement.BoundaryCertificateId;
		OutDemandResult.CertifiedChildIncomingBoundaryPoints =
			InOutPlacement.CertifiedChildIncomingBoundaryPoints;
		OutDemandResult.ChildDomainRestrictions = InOutPlacement.ChildDomainRestrictions;
		OutDemandResult.ZoneFeatureProviderCommitments =
			InOutPlacement.DirectChildCommitment.ZoneFeatureProviderCommitments;
		OutDemandResult.bSucceeded = true;
		return true;
	}

	void ApplyCommittedVerticalAccessOwnershipToNegotiatedContract(
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const FString& ChildRegionDebugPath,
		FLayoutNegotiatedChildResponsibilityContract& InOutContract)
	{
		InOutContract.HostVerticalAccessResponsibility =
			DetermineNegotiatedHostVerticalAccessResponsibility(
				ChildRegionDebugPath,
				VerticalAccessOwnership);
		InOutContract.RequiredHostProviderCount =
			VerticalAccessOwnership.RequiredHostProviderCount;
		InOutContract.bRequiresExactHostProviderCount =
			VerticalAccessOwnership.bRequiresExactHostProviderCount;
		InOutContract.CountedParentProviderCount =
			VerticalAccessOwnership.CountedParentProviderCount;
		InOutContract.CountedParentVerticalAccessCells =
			VerticalAccessOwnership.CountedParentVerticalAccessCells;
		InOutContract.RetainedParentRouteSupportVerticalAccessCells =
			VerticalAccessOwnership.RetainedParentRouteSupportVerticalAccessCells;
		InOutContract.CountedChildProviderRegionDebugPaths =
			VerticalAccessOwnership.CountedChildRegionDebugPaths;

		if (InOutContract.HostVerticalAccessResponsibility
			== ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned)
		{
			InOutContract.bHasRequiredHostIngressAnchor = false;
			InOutContract.RequiredHostIngressAnchor = FLayoutCommittedEndpointAnchor();
			InOutContract.bHasRequiredHostEgressAnchor = false;
			InOutContract.RequiredHostEgressAnchor = FLayoutCommittedEndpointAnchor();
			InOutContract.RequiredChildGenerallyConnectableAnchorPairId = NAME_None;
			InOutContract.RequiredChildInternalVerticalSpanLevels.Reset();
			InOutContract.RequiredChildInternalVerticalRouteCells.Reset();
		}
	}

	FString DescribeNegotiatedHostVerticalAccessResponsibility(
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

	FString DescribeNegotiatedChildResponsibilityContract(
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
			TEXT("Negotiated child responsibility: parent='%s' child='%s' hostVerticalAccess=%s requiredHostProviders=%d countedParentProviders=%d countedParentCells=%d routeSupportParentCells=%d countedChildProviders=%d replacementLevels=%d replacementCells=%d retainedShellLevels=%d retainedShellCells=%d proofOnlyHostShellLevels=%d proofOnlyHostShellCells=%d interfaceLevels=%d endpointAnchors=%d traversalAnchors=%d siblingSeamLevels=%d siblingSeams=%d ingress=%s egress=%s childVerticalRouteCells=%d"),
			*Contract.ParentRegionDebugPath,
			*Contract.ChildRegionDebugPath,
			*DescribeNegotiatedHostVerticalAccessResponsibility(Contract.HostVerticalAccessResponsibility),
			Contract.RequiredHostProviderCount,
			Contract.CountedParentProviderCount,
			Contract.CountedParentVerticalAccessCells.Num(),
			Contract.RetainedParentRouteSupportVerticalAccessCells.Num(),
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

	void AppendNegotiatedContractDiagnostics(
		const TArray<FLayoutNegotiatedChildResponsibilityContract>& Contracts,
		FLayoutRegionSolveResult& InOutRootRegionResult)
	{
		for (const FLayoutNegotiatedChildResponsibilityContract& Contract : Contracts)
		{
			TArray<FLayoutId> SourceIds;
			if (!Contract.ParentRegionDebugPath.IsEmpty())
			{
				SourceIds.Add(FLayoutId(*Contract.ParentRegionDebugPath));
			}
			if (!Contract.ChildRegionDebugPath.IsEmpty())
			{
				SourceIds.Add(FLayoutId(*Contract.ChildRegionDebugPath));
			}

			InOutRootRegionResult.ProofRecords.Add(MakeSnapshotProofRecord(
				FLayoutId(*FString::Printf(
					TEXT("%s.%s.NegotiatedResponsibility"),
					Contract.ParentRegionDebugPath.IsEmpty() ? TEXT("Root") : *Contract.ParentRegionDebugPath,
					Contract.ChildRegionDebugPath.IsEmpty() ? TEXT("Child") : *Contract.ChildRegionDebugPath)),
				ELayoutProofKind::NormalizedCommitment,
				FLayoutId(*Contract.ChildRegionDebugPath),
				SourceIds,
				DescribeNegotiatedChildResponsibilityContract(Contract)));
		}
	}

	bool AppendNegotiatedContractEvidenceToRequest(
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		for (const FLayoutNegotiatedChildResponsibilityContract& Contract : InOutRequest.NegotiatedChildResponsibilityContracts)
		{
			const FString ContractDescription = DescribeNegotiatedChildResponsibilityContract(Contract);
			const FLayoutId ContractRecordId(*FString::Printf(
				TEXT("%s.NegotiatedChildResponsibility.%s"),
				*InOutRequest.RegionDebugPath,
				*Contract.ChildRegionDebugPath));
			const TArray<FLayoutId> RelatedIds = {
				InOutRequest.EffectiveSnapshotId,
				InOutRequest.ProfileSnapshot.SnapshotId,
				FLayoutId(*Contract.ParentRegionDebugPath),
				FLayoutId(*Contract.ChildRegionDebugPath)
			};

			FString ContractFailureReason;
			const bool bContractValid = ValidateNegotiatedChildResponsibilityContract(Contract, ContractFailureReason);
			InOutRequest.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
				ContractRecordId,
				ELayoutValidationAssertionKind::ChildCommitmentContractValid,
				bContractValid,
				RelatedIds,
				bContractValid
					? FString()
					: FString::Printf(
						TEXT("Negotiated child responsibility contract is incomplete before region proof starts.\n%s\nProblem: Later proof phases would have to rediscover ownership or proceed with an invalid negotiated boundary.\nDetail: %s"),
						*ContractDescription,
						*ContractFailureReason)));
			if (!bContractValid)
			{
				OutFailureReason = InOutRequest.ValidationAssertions.Last().FailureReason;
				return false;
			}

			InOutRequest.ProofRecords.Add(MakeSnapshotProofRecord(
				ContractRecordId,
				ELayoutProofKind::NormalizedCommitment,
				InOutRequest.EffectiveSnapshotId,
				RelatedIds,
				FString::Printf(
					TEXT("Validated negotiated child responsibility contract before region proof began.\n%s"),
					*ContractDescription)));
		}

		return true;
	}

	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnership(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedParentCells,
		const TArray<FAutomaticChildPlacement>& Placements)
	{
		FCommittedVerticalAccessOwnership Ownership;
		Ownership.RequiredHostProviderCount =
			GetRequiredHostVerticalAccessProviderCount(RootRequest, ParentPlannedCells);
		Ownership.bRequiresExactHostProviderCount =
			RequiresExactHostVerticalAccessProviderCount(RootRequest.ProfileSnapshot);
		if (Ownership.RequiredHostProviderCount <= 0)
		{
			return Ownership;
		}

		bool bAnyPlacementHasNegotiatedContract = false;
		bool bAllPlacementsHaveNegotiatedContracts = true;
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			const bool bHasNegotiatedContract =
				!Placement.NegotiatedResponsibilityContract.ChildRegionDebugPath.IsEmpty()
				&& Placement.NegotiatedResponsibilityContract.ChildRegionDebugPath
					== Placement.ChildRegionDebugPath;
			bAnyPlacementHasNegotiatedContract |= bHasNegotiatedContract;
			bAllPlacementsHaveNegotiatedContracts &= bHasNegotiatedContract;
		}
		if (bAnyPlacementHasNegotiatedContract
			&& bAllPlacementsHaveNegotiatedContracts)
		{
			return DetermineCommittedVerticalAccessOwnershipFromNegotiatedContracts(
				RootRequest,
				ParentPlannedCells,
				Placements);
		}

		int32 OriginalParentVerticalAccessCellCount = 0;
		TArray<FIntVector> UnreservedParentVerticalAccessCells;
		for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
		{
			if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}

			++OriginalParentVerticalAccessCellCount;
			if (!ReservedParentCells.Contains(PlannedCell.Cell))
			{
				UnreservedParentVerticalAccessCells.Add(PlannedCell.Cell);
			}
		}
		UnreservedParentVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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

		Ownership.PotentialParentProviderCount = OriginalParentVerticalAccessCellCount;

		TArray<FString> CandidateChildOwners;
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			if (Placement.bSupportsVerticalAccess && !Placement.ChildVerticalAccessLocalCells.IsEmpty())
			{
				if (Placement.bContributesHostVerticalAccess)
				{
					FString HostVerticalAccessFailureReason;
					if (DoesPlacementProvideHostFacingVerticalAccess(
						Placement,
						&HostVerticalAccessFailureReason))
					{
						CandidateChildOwners.Add(Placement.ChildRegionDebugPath);
					}
					else
					{
						if (GIsAutomationTesting)
						{
							UE_LOG(
								LogTemp,
								Warning,
								TEXT("LayoutVerticalAccessOwnershipDiag[%s]: child host-ascent proof rejected. supportsVertical=%d contributesHostVA=%d verticalCells=%d sharedFaces=%d directCommitments=%d detail=%s"),
								*Placement.ChildRegionDebugPath,
								Placement.bSupportsVerticalAccess ? 1 : 0,
								Placement.bContributesHostVerticalAccess ? 1 : 0,
								Placement.ChildVerticalAccessLocalCells.Num(),
								Placement.SharedParentChildFaces.Num(),
								Placement.DirectChildCommitment.EndpointCommitments.Num(),
								*HostVerticalAccessFailureReason);
						}
						Ownership.LocalOnlyChildRegionDebugPaths.AddUnique(Placement.ChildRegionDebugPath);
					}
				}
				else
				{
					Ownership.LocalOnlyChildRegionDebugPaths.AddUnique(Placement.ChildRegionDebugPath);
				}
			}
			else if (Placement.bContributesHostVerticalAccess)
			{
				Ownership.UnusableContributingChildRegionDebugPaths.AddUnique(Placement.ChildRegionDebugPath);
			}
		}
		CandidateChildOwners.Sort();

		const int32 DisplacedRequiredParentProviderCount = FMath::Max(
			0,
			Ownership.RequiredHostProviderCount
				- UnreservedParentVerticalAccessCells.Num());
		const int32 NeededChildProviders = FMath::Min(
			CandidateChildOwners.Num(),
			DisplacedRequiredParentProviderCount);
		for (int32 Index = 0; Index < CandidateChildOwners.Num(); ++Index)
		{
			if (Index < NeededChildProviders)
			{
				Ownership.CountedChildRegionDebugPaths.Add(
					CandidateChildOwners[Index]);
			}
			else
			{
				Ownership.ExtraContributingChildRegionDebugPaths.Add(
					CandidateChildOwners[Index]);
			}
		}
		Ownership.CountedParentProviderCount = FMath::Max(
			0,
			Ownership.RequiredHostProviderCount
				- Ownership.CountedChildRegionDebugPaths.Num());
		Ownership.RetainedParentRouteSupportVerticalAccessCells =
			UnreservedParentVerticalAccessCells;
		Ownership.CountedParentVerticalAccessCells.Reset();
		const int32 CountedParentCellCount = FMath::Min(
			Ownership.CountedParentProviderCount,
			UnreservedParentVerticalAccessCells.Num());
		for (int32 Index = 0; Index < CountedParentCellCount; ++Index)
		{
			Ownership.CountedParentVerticalAccessCells.Add(
				UnreservedParentVerticalAccessCells[Index]);
		}
		Ownership.RetainedParentRouteSupportVerticalAccessCells.RemoveAll(
			[&Ownership](const FIntVector& Cell)
			{
				return Ownership.CountedParentVerticalAccessCells.Contains(Cell);
			});

		FinalizeCommittedVerticalAccessOwnershipSummary(Ownership);
		if (!IsCommittedVerticalAccessRequirementSatisfied(Ownership))
		{
			Ownership.FailureReason = BuildCommittedVerticalAccessFailureReason(Ownership);
		}
		return Ownership;
	}

	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnershipForProspectiveCandidate(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& CurrentReservedParentCells,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const FString& CandidateChildRegionDebugPath,
		const bool bCandidateSupportsVerticalAccess,
		const bool bCandidateContributesHostVerticalAccess,
		const TArray<FIntVector>& CandidateVerticalAccessLocalCells,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const TArray<FLayoutPlannedCell>& CandidateChildPlannedCells,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces)
	{
		TSet<FIntVector> ProspectiveReservedCells = CurrentReservedParentCells;
		for (const FIntVector& ReservedCell : CandidateParentPlanReservedCells)
		{
			ProspectiveReservedCells.Add(ReservedCell);
		}

		TArray<FAutomaticChildPlacement> ProspectivePlacements = CurrentPlacements;
		FAutomaticChildPlacement& ProspectivePlacement = ProspectivePlacements.AddDefaulted_GetRef();
		ProspectivePlacement.ChildRegionDebugPath = CandidateChildRegionDebugPath;
		ProspectivePlacement.bSupportsVerticalAccess = bCandidateSupportsVerticalAccess;
		ProspectivePlacement.bContributesHostVerticalAccess = bCandidateContributesHostVerticalAccess;
		ProspectivePlacement.ChildVerticalAccessLocalCells = CandidateVerticalAccessLocalCells;
		ProspectivePlacement.ChildRequest.PlannedCells = CandidateChildPlannedCells;
		ProspectivePlacement.DirectChildCommitment.EndpointCommitments = CandidateCommitments;
		ProspectivePlacement.SharedParentChildFaces = CandidateSharedParentChildFaces;

		return DetermineCommittedVerticalAccessOwnership(
			RootRequest,
			ParentPlannedCells,
			ProspectiveReservedCells,
			ProspectivePlacements);
	}

	FCommittedVerticalAccessOwnership DetermineCommittedVerticalAccessOwnershipFromNegotiatedContracts(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TArray<FAutomaticChildPlacement>& Placements)
	{
		FCommittedVerticalAccessOwnership Ownership;
		Ownership.RequiredHostProviderCount =
			GetRequiredHostVerticalAccessProviderCount(RootRequest, ParentPlannedCells);
		Ownership.bRequiresExactHostProviderCount =
			RequiresExactHostVerticalAccessProviderCount(RootRequest.ProfileSnapshot);
		if (Ownership.RequiredHostProviderCount <= 0)
		{
			return Ownership;
		}

		for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				++Ownership.PotentialParentProviderCount;
			}
		}

		TSet<FString> CountedChildProviders;
		TSet<FString> LocalOnlyChildren;
		TSet<FString> UnusableContributingChildren;
		TSet<FString> ExtraContributingChildren;
		TSet<FIntVector> CountedParentProviderCells;
		TSet<FIntVector> RouteSupportParentCells;

		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			const FLayoutNegotiatedChildResponsibilityContract& Contract =
				Placement.NegotiatedResponsibilityContract;
			if (!Contract.ChildRegionDebugPath.IsEmpty())
			{
				for (const FString& CountedChildRegionPath :
					Contract.CountedChildProviderRegionDebugPaths)
				{
					CountedChildProviders.Add(CountedChildRegionPath);
				}
				for (const FIntVector& ParentProviderCell :
					Contract.CountedParentVerticalAccessCells)
				{
					CountedParentProviderCells.Add(ParentProviderCell);
				}
				for (const FIntVector& RouteSupportCell :
					Contract.RetainedParentRouteSupportVerticalAccessCells)
				{
					RouteSupportParentCells.Add(RouteSupportCell);
				}
			}

			if (Placement.bSupportsVerticalAccess
				&& !Placement.ChildVerticalAccessLocalCells.IsEmpty())
			{
				if (CountedChildProviders.Contains(Placement.ChildRegionDebugPath))
				{
					continue;
				}

				if (!Placement.bContributesHostVerticalAccess)
				{
					LocalOnlyChildren.Add(Placement.ChildRegionDebugPath);
					continue;
				}

				FString HostVerticalAccessFailureReason;
				if (DoesPlacementProvideHostFacingVerticalAccess(
					Placement,
					&HostVerticalAccessFailureReason))
				{
					ExtraContributingChildren.Add(Placement.ChildRegionDebugPath);
				}
				else
				{
					LocalOnlyChildren.Add(Placement.ChildRegionDebugPath);
				}
			}
			else if (Placement.bContributesHostVerticalAccess)
			{
				UnusableContributingChildren.Add(Placement.ChildRegionDebugPath);
			}
		}

		TArray<FString> OrderedCountedChildProviders = CountedChildProviders.Array();
		OrderedCountedChildProviders.Sort();
		const int32 CountedChildProviderLimit = FMath::Min(
			Ownership.RequiredHostProviderCount,
			OrderedCountedChildProviders.Num());
		for (int32 ChildIndex = 0;
			ChildIndex < OrderedCountedChildProviders.Num();
			++ChildIndex)
		{
			if (ChildIndex < CountedChildProviderLimit)
			{
				Ownership.CountedChildRegionDebugPaths.Add(
					OrderedCountedChildProviders[ChildIndex]);
			}
			else
			{
				ExtraContributingChildren.Add(
					OrderedCountedChildProviders[ChildIndex]);
			}
		}

		TArray<FIntVector> OrderedParentProviderCells =
			CountedParentProviderCells.Array();
		OrderedParentProviderCells.Sort([](
			const FIntVector& Left,
			const FIntVector& Right)
		{
			if (Left.Z != Right.Z) return Left.Z < Right.Z;
			if (Left.Y != Right.Y) return Left.Y < Right.Y;
			return Left.X < Right.X;
		});
		const int32 RequiredParentProviderCount = FMath::Max(
			0,
			Ownership.RequiredHostProviderCount
				- Ownership.CountedChildRegionDebugPaths.Num());
		const int32 CountedParentCellLimit = FMath::Min(
			RequiredParentProviderCount,
			OrderedParentProviderCells.Num());
		for (int32 CellIndex = 0;
			CellIndex < OrderedParentProviderCells.Num();
			++CellIndex)
		{
			if (CellIndex < CountedParentCellLimit)
			{
				Ownership.CountedParentVerticalAccessCells.Add(
					OrderedParentProviderCells[CellIndex]);
			}
			else
			{
				RouteSupportParentCells.Add(OrderedParentProviderCells[CellIndex]);
			}
		}
		Ownership.CountedParentProviderCount =
			Ownership.CountedParentVerticalAccessCells.Num();
		for (const FIntVector& CountedCell : Ownership.CountedParentVerticalAccessCells)
		{
			RouteSupportParentCells.Remove(CountedCell);
		}
		for (const FIntVector& RouteSupportCell : RouteSupportParentCells)
		{
			Ownership.RetainedParentRouteSupportVerticalAccessCells.Add(RouteSupportCell);
		}
		for (const FString& ChildRegionPath : LocalOnlyChildren)
		{
			Ownership.LocalOnlyChildRegionDebugPaths.Add(ChildRegionPath);
		}
		for (const FString& ChildRegionPath : UnusableContributingChildren)
		{
			Ownership.UnusableContributingChildRegionDebugPaths.Add(ChildRegionPath);
		}
		for (const FString& ChildRegionPath : ExtraContributingChildren)
		{
			Ownership.ExtraContributingChildRegionDebugPaths.Add(ChildRegionPath);
		}

		FinalizeCommittedVerticalAccessOwnershipSummary(Ownership);
		if (!IsCommittedVerticalAccessRequirementSatisfied(Ownership))
		{
			Ownership.FailureReason = BuildCommittedVerticalAccessFailureReason(Ownership);
		}
		return Ownership;
	}

	/** Resolves host ownership after child contracts refresh, retaining parent-only authority when no child was selected. */
	FCommittedVerticalAccessOwnership ResolveCommittedVerticalAccessOwnershipAfterContractRefresh(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TArray<FAutomaticChildPlacement>& Placements)
	{
		if (!Placements.IsEmpty())
		{
			return DetermineCommittedVerticalAccessOwnershipFromNegotiatedContracts(
				RootRequest,
				ParentPlannedCells,
				Placements);
		}

		return DetermineCommittedVerticalAccessOwnership(
			RootRequest,
			ParentPlannedCells,
			TSet<FIntVector>(),
			Placements);
	}

	/** Rebinds remaining host demand to derived parent cells without granting solve authority.
	 * Callers must still prove the parent plan and negotiated child contracts; missing providers reject. */
	bool RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
		const TArray<FLayoutPlannedCell>& DerivedParentPlannedCells,
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString* OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		// A refreshed contract can retain fewer counted cells than the root needs.
		// Rebind its remaining demand to derived parent cells, not the stale count.
		const int32 RequiredParentProviderCount = FMath::Max(
			InOutOwnership.CountedParentProviderCount,
			FMath::Max(0, InOutOwnership.RequiredHostProviderCount
				- InOutOwnership.CountedChildRegionDebugPaths.Num()));
		if (RequiredParentProviderCount <= 0)
		{
			InOutOwnership.CountedParentVerticalAccessCells.Reset();
			TArray<FIntVector> DerivedVerticalAccessCells;
			for (const FLayoutPlannedCell& PlannedCell : DerivedParentPlannedCells)
			{
				if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
				{
					DerivedVerticalAccessCells.Add(PlannedCell.Cell);
				}
			}
			DerivedVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
			InOutOwnership.RetainedParentRouteSupportVerticalAccessCells = MoveTemp(DerivedVerticalAccessCells);
			FinalizeCommittedVerticalAccessOwnershipSummary(InOutOwnership);
			return true;
		}

		TSet<FIntVector> DerivedVerticalAccessCellSet;
		TArray<FIntVector> DerivedVerticalAccessCells;
		for (const FLayoutPlannedCell& PlannedCell : DerivedParentPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				DerivedVerticalAccessCellSet.Add(PlannedCell.Cell);
				DerivedVerticalAccessCells.Add(PlannedCell.Cell);
			}
		}

		TArray<FIntVector> RetainedCountedParentCells;
		for (const FIntVector& CountedParentCell : InOutOwnership.CountedParentVerticalAccessCells)
		{
			if (DerivedVerticalAccessCellSet.Contains(CountedParentCell))
			{
				RetainedCountedParentCells.AddUnique(CountedParentCell);
			}
		}
		for (const FIntVector& DerivedVerticalAccessCell : DerivedVerticalAccessCells)
		{
			RetainedCountedParentCells.AddUnique(DerivedVerticalAccessCell);
		}

		RetainedCountedParentCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
		DerivedVerticalAccessCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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

		const bool bHadConcreteCountedParentCells = !InOutOwnership.CountedParentVerticalAccessCells.IsEmpty();
		const bool bRetainedTooFewConcreteCountedCells =
			bHadConcreteCountedParentCells
			&& RetainedCountedParentCells.Num() < RequiredParentProviderCount;
		const bool bDerivedTooFewParentVerticalAccessCells =
			!bHadConcreteCountedParentCells
			&& DerivedVerticalAccessCells.Num() < RequiredParentProviderCount;
		if (bRetainedTooFewConcreteCountedCells || bDerivedTooFewParentVerticalAccessCells)
		{
			if (OutFailureReason != nullptr)
			{
				TArray<FString> CountedParentCellStrings;
				for (const FIntVector& CountedParentCell : InOutOwnership.CountedParentVerticalAccessCells)
				{
					CountedParentCellStrings.Add(CountedParentCell.ToString());
				}
				TArray<FString> DerivedVerticalAccessCellStrings;
				for (const FIntVector& DerivedVerticalAccessCell : DerivedVerticalAccessCells)
				{
					DerivedVerticalAccessCellStrings.Add(DerivedVerticalAccessCell.ToString());
				}
				*OutFailureReason = FString::Printf(
					TEXT("Derived parent plan retained only %d concrete parent-owned vertical-access cell(s), but %d are required by the remaining host/provider contract.\nCounted Parent Cells: %s\nDerived Vertical-Access Cells: %s\nProblem: The surviving residual parent plan cannot supply the remaining parent ascent obligation.\nFix: Move blocking child regions, reduce parent-owned ascent obligations, or ensure the negotiated child contract contributes the displaced host vertical access explicitly."),
					RetainedCountedParentCells.Num(),
					RequiredParentProviderCount,
					CountedParentCellStrings.IsEmpty() ? TEXT("<none>") : *FString::Join(CountedParentCellStrings, TEXT(", ")),
					DerivedVerticalAccessCellStrings.IsEmpty() ? TEXT("<none>") : *FString::Join(DerivedVerticalAccessCellStrings, TEXT(", ")));
			}
			return false;
		}

		if (bHadConcreteCountedParentCells || RetainedCountedParentCells.Num() >= RequiredParentProviderCount)
		{
			InOutOwnership.CountedParentVerticalAccessCells.Reset();
			const int32 CountedCellsToRetain = FMath::Min(
				RequiredParentProviderCount,
				RetainedCountedParentCells.Num());
			for (int32 Index = 0; Index < CountedCellsToRetain; ++Index)
			{
				InOutOwnership.CountedParentVerticalAccessCells.Add(RetainedCountedParentCells[Index]);
			}
		}
		InOutOwnership.RetainedParentRouteSupportVerticalAccessCells.Reset();
		for (const FIntVector& DerivedVerticalAccessCell : DerivedVerticalAccessCells)
		{
			if (!InOutOwnership.CountedParentVerticalAccessCells.Contains(DerivedVerticalAccessCell))
			{
				InOutOwnership.RetainedParentRouteSupportVerticalAccessCells.Add(
					DerivedVerticalAccessCell);
			}
		}
		InOutOwnership.CountedParentProviderCount = RequiredParentProviderCount;
		// Recompute cached totals after refreshing counted cells, including recovery
		// from a contract-local undercount. No failure path publishes this update.
		InOutOwnership.ResolvedHostProviderCount = 0;
		FinalizeCommittedVerticalAccessOwnershipSummary(InOutOwnership);
		InOutOwnership.FailureReason = IsCommittedVerticalAccessRequirementSatisfied(InOutOwnership)
			? FString() : BuildCommittedVerticalAccessFailureReason(InOutOwnership);
		return true;
	}

	FString BuildSchedulerPlacementStateKey(
		const int32 DemandIndex,
		const TArray<FAutomaticChildPlacement>& Placements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership)
	{
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

			TArray<FString> WitnessParts;
			WitnessParts.Reserve(Placement.BoundaryWitnessIds.Num());
			for (const FLayoutId WitnessId : Placement.BoundaryWitnessIds)
			{
				WitnessParts.Add(WitnessId.ToString());
			}
			WitnessParts.Sort();

			TArray<FString> ContactParts;
			ContactParts.Reserve(Placement.ParentCommittedEntryCells.Num());
			for (const FIntVector& ContactCell : Placement.ParentCommittedEntryCells)
			{
				ContactParts.Add(ContactCell.ToString());
			}
			ContactParts.Sort();

			TArray<FString> TraversalAnchorParts;
			TraversalAnchorParts.Reserve(Placement.ParentCommittedTraversalAnchors.Num());
			for (const FLayoutCommittedTraversalAnchor& Anchor : Placement.ParentCommittedTraversalAnchors)
			{
				TraversalAnchorParts.Add(FString::Printf(
					TEXT("%s:%s"),
					*Anchor.Cell.ToString(),
					Anchor.TraversalChannel.IsValid() ? *Anchor.TraversalChannel.ToString() : TEXT("<none>")));
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
			for (const FSharedParentChildFace& SharedFace : Placement.SharedParentChildFaces)
			{
				SeamParts.Add(BuildSharedParentChildFaceIdentityPart(SharedFace));
			}
			SeamParts.Sort();

			const FString NegotiatedContractPart =
				BuildNegotiatedChildResponsibilityContractMemoKey(Placement.NegotiatedResponsibilityContract);

			TArray<FString> CommitmentParts;
			CommitmentParts.Reserve(Placement.DirectChildCommitment.EndpointCommitments.Num());
			for (const FLayoutCommittedEndpointAnchor& Commitment : Placement.DirectChildCommitment.EndpointCommitments)
			{
				CommitmentParts.Add(FString::Printf(
					TEXT("%s:%s:%d:%s:%s:%s:%d"),
					Commitment.CommitmentId == NAME_None ? TEXT("<none>") : *Commitment.CommitmentId.ToString(),
					*Commitment.LocalCell.ToString(),
					static_cast<int32>(Commitment.FaceDirection),
					Commitment.ConnectionTag.IsValid() ? *Commitment.ConnectionTag.ToString() : TEXT("<none>"),
					*TagsToStableKey(Commitment.AllowedConnectionTags),
					*TagsToStableKey(Commitment.TraversalChannels),
					Commitment.bRequireMatchingYawWithFilledNeighbor ? 1 : 0));
			}
			CommitmentParts.Sort();

			PlacementParts.Add(FString::Printf(
				TEXT("%s@%s|StageClass=%d|StageMapping=%s|Footprint=%s|Planned=%s|Witnesses=%s|Contacts=%s|Traversal=%s|Bridge=%d|Reserved=%s|Seams=%s|Commitments=%s|Contract=%s"),
				Placement.EntryId == NAME_None ? TEXT("<none>") : *Placement.EntryId.ToString(),
				*Placement.RegionCellOffset.ToString(),
				static_cast<int32>(Placement.StageMapping.StageClass),
				Placement.StageMapping.MappingId == NAME_None ? TEXT("<none>") : *Placement.StageMapping.MappingId.ToString(),
				*Placement.ChildRequest.FootprintSize.ToString(),
				PlannedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedCellParts, TEXT(",")),
				WitnessParts.IsEmpty() ? TEXT("<none>") : *FString::Join(WitnessParts, TEXT(",")),
				ContactParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ContactParts, TEXT(",")),
				TraversalAnchorParts.IsEmpty() ? TEXT("<none>") : *FString::Join(TraversalAnchorParts, TEXT(",")),
				Placement.bAllowsChildTraversalBridgeForCommittedContacts ? 1 : 0,
				ReservedCellParts.IsEmpty() ? TEXT("<none>") : *FString::Join(ReservedCellParts, TEXT(",")),
				SeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(SeamParts, TEXT(",")),
				CommitmentParts.IsEmpty() ? TEXT("<none>") : *FString::Join(CommitmentParts, TEXT(",")),
				*NegotiatedContractPart));
		}
		PlacementParts.Sort();

		TArray<FString> PlannedSeamParts;
		PlannedSeamParts.Reserve(PlannedPartitionSeams.Num());
		for (const FLayoutPartitionSeamRecord& SeamRecord : PlannedPartitionSeams)
		{
			PlannedSeamParts.Add(FString::Printf(
				TEXT("%s|%s|%s|%s|%s|%s"),
				*CanonicalizeRegionPathForMemo(SeamRecord.OwnerRegionDebugPath, ChildMemoIdentityByRegionPath),
				*CanonicalizeRegionPathForMemo(SeamRecord.PassiveRegionDebugPath, ChildMemoIdentityByRegionPath),
				SeamRecord.InterfaceFamily.IsValid() ? *SeamRecord.InterfaceFamily.ToString() : TEXT("<none>"),
				*SeamRecord.OwnerStartCell.ToString(),
				*SeamRecord.PassiveStartCell.ToString(),
				*SeamRecord.OwnerEndCell.ToString()));
		}
		PlannedSeamParts.Sort();

		return FString::Printf(
			TEXT("Demand=%d|Placements=%s|CommittedSeams=%s|VerticalOwner=%s"),
			DemandIndex,
			PlacementParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlacementParts, TEXT(";")),
			PlannedSeamParts.IsEmpty() ? TEXT("<none>") : *FString::Join(PlannedSeamParts, TEXT(";")),
			*BuildCommittedVerticalAccessOwnershipMemoKey(VerticalAccessOwnership, ChildMemoIdentityByRegionPath));
	}

	static constexpr int32 RecursiveSchedulerDeferredValidationCandidateCap = 64;
	static constexpr int32 RecursiveSchedulerSingleDemandDeferredValidationCandidateCap = 24;
	static constexpr int32 RecursiveSchedulerTwoDemandDeferredValidationCandidateCap = 24;

	int32 GetDeferredValidationCandidateCap(const int32 PlacementDemandCount)
	{
		if (PlacementDemandCount <= 1)
		{
			return RecursiveSchedulerSingleDemandDeferredValidationCandidateCap;
		}

		if (PlacementDemandCount == 2)
		{
			// Two-demand recursive schedules can accumulate a long structurally valid tail
			// even after beam-capping the first child. Keep validation focused on the
			// strongest completed schedules instead of replaying the whole tail.
			return RecursiveSchedulerTwoDemandDeferredValidationCandidateCap;
		}

		return RecursiveSchedulerDeferredValidationCandidateCap;
	}

	using FDeferredPlacementResolutionResult = FPlacementBridgeDeferredValidationResult;

	void AddCommittedPartitionSeamRecord(
		const FString& ParentRegionDebugPath,
		const FString& OwnerRegionDebugPath,
		const FString& PassiveRegionDebugPath,
		const FGameplayTag& InterfaceFamily,
		const ELayoutFaceDirection OwnerFaceDirection,
		const ELayoutFaceDirection PassiveFaceDirection,
		const FIntVector& OwnerStartCell,
		const FIntVector& OwnerEndCell,
		const FIntVector& PassiveStartCell,
		const FIntVector& PassiveEndCell,
		TArray<FLayoutPartitionSeamRecord>& InOutPlannedPartitionSeams)
	{
		const int32 SegmentCount =
			FMath::Max3(
				FMath::Abs(OwnerEndCell.X - OwnerStartCell.X),
				FMath::Abs(OwnerEndCell.Y - OwnerStartCell.Y),
				FMath::Abs(OwnerEndCell.Z - OwnerStartCell.Z))
			+ 1;
		const bool bAlreadyPresent = InOutPlannedPartitionSeams.ContainsByPredicate(
			[&](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == ParentRegionDebugPath
					&& SeamRecord.OwnerRegionDebugPath == OwnerRegionDebugPath
					&& SeamRecord.PassiveRegionDebugPath == PassiveRegionDebugPath
					&& SeamRecord.InterfaceFamily == InterfaceFamily
					&& SeamRecord.OwnerFaceDirection == OwnerFaceDirection
					&& SeamRecord.PassiveFaceDirection == PassiveFaceDirection
					&& SeamRecord.OwnerStartCell == OwnerStartCell
					&& SeamRecord.OwnerEndCell == OwnerEndCell
					&& SeamRecord.PassiveStartCell == PassiveStartCell
					&& SeamRecord.PassiveEndCell == PassiveEndCell;
			});
		if (bAlreadyPresent)
		{
			return;
		}

		FLayoutPartitionSeamRecord& SeamRecord = InOutPlannedPartitionSeams.AddDefaulted_GetRef();
		SeamRecord.ParentRegionDebugPath = ParentRegionDebugPath;
		SeamRecord.OwnerRegionDebugPath = OwnerRegionDebugPath;
		SeamRecord.PassiveRegionDebugPath = PassiveRegionDebugPath;
		SeamRecord.InterfaceFamily = InterfaceFamily;
		SeamRecord.OwnerFaceDirection = OwnerFaceDirection;
		SeamRecord.PassiveFaceDirection = PassiveFaceDirection;
		SeamRecord.OwnerStartCell = OwnerStartCell;
		SeamRecord.OwnerEndCell = OwnerEndCell;
		SeamRecord.PassiveStartCell = PassiveStartCell;
		SeamRecord.PassiveEndCell = PassiveEndCell;
		SeamRecord.SegmentCount = SegmentCount;
		SeamRecord.SeamId = FLayoutId(*FString::Printf(
			TEXT("%s.%s.%s.%s.%s"),
			*ParentRegionDebugPath,
			*OwnerRegionDebugPath,
			*PassiveRegionDebugPath,
			InterfaceFamily.IsValid() ? *InterfaceFamily.ToString() : TEXT("<none>"),
			*OwnerStartCell.ToString()));
	}

	bool HasCommittedPartitionSeamRecord(
		const FString& ParentRegionDebugPath,
		const FString& OwnerRegionDebugPath,
		const FString& PassiveRegionDebugPath,
		const FGameplayTag& InterfaceFamily,
		const ELayoutFaceDirection OwnerFaceDirection,
		const ELayoutFaceDirection PassiveFaceDirection,
		const FIntVector& OwnerStartCell,
		const FIntVector& OwnerEndCell,
		const FIntVector& PassiveStartCell,
		const FIntVector& PassiveEndCell,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams)
	{
		return PlannedPartitionSeams.ContainsByPredicate(
			[&](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == ParentRegionDebugPath
					&& SeamRecord.OwnerRegionDebugPath == OwnerRegionDebugPath
					&& SeamRecord.PassiveRegionDebugPath == PassiveRegionDebugPath
					&& SeamRecord.InterfaceFamily == InterfaceFamily
					&& SeamRecord.OwnerFaceDirection == OwnerFaceDirection
					&& SeamRecord.PassiveFaceDirection == PassiveFaceDirection
					&& SeamRecord.OwnerStartCell == OwnerStartCell
					&& SeamRecord.OwnerEndCell == OwnerEndCell
					&& SeamRecord.PassiveStartCell == PassiveStartCell
					&& SeamRecord.PassiveEndCell == PassiveEndCell;
			});
	}

	bool DoesRequestContainCommittedEndpointAnchor(
		const FLayoutRegionSolveRequest& Request,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection);

	bool DoesExactSharedSeamCandidateOwn(
		const FLayoutModuleSolveSnapshot& Module,
		int32 YawRotationSteps,
		ELayoutFaceDirection FaceDirection,
		const FGameplayTag& InterfaceFamily);

	bool TryCertifyExactSharedSeamCandidatePairForFaces(
		const FLayoutModuleSolveSnapshot& ParentModule,
		int32 ParentYawRotationSteps,
		ELayoutFaceDirection ParentFaceDirection,
		bool bParentOwnershipAllowed,
		const FLayoutModuleSolveSnapshot& ChildModule,
		int32 ChildYawRotationSteps,
		ELayoutFaceDirection ChildFaceDirection,
		bool bChildOwnershipAllowed,
		const FGameplayTag& InterfaceFamily,
		bool& bOutParentCanOwn,
		bool& bOutChildCanOwn,
		FLayoutId& OutParentEvidenceId,
		FLayoutId& OutChildEvidenceId,
		FLayoutId& OutWitnessId);

	FLayoutChildCapabilityEnvelope BuildChildCapabilityEnvelope(
		const FLayoutRegionSolveRequest& Request);

	bool DoesPlacementContainCommittedEndpointAnchor(
		const FAutomaticChildPlacement& Placement,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection)
	{
		return Placement.DirectChildCommitment.EndpointCommitments.ContainsByPredicate(
			[&](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return Anchor.LocalCell == LocalCell
					&& Anchor.FaceDirection == FaceDirection;
			});
	}

	/** Requires exact frozen module/yaw domains before sibling seam ownership can settle. */
	bool TryResolveExactSiblingSeamOwnershipWitness(
		const FAutomaticChildPlacement& FirstPlacement,
		const FLayoutPlannedCell& FirstPlannedCell,
		const ELayoutFaceDirection FirstFaceDirection,
		const FAutomaticChildPlacement& SecondPlacement,
		const FLayoutPlannedCell& SecondPlannedCell,
		const ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& InterfaceFamily,
		const bool bSuppressPassiveCellCandidate,
		bool& bOutFirstCanOwn,
		bool& bOutSecondCanOwn)
	{
		const bool bFirstCapabilityCanOwn = bOutFirstCanOwn;
		const bool bSecondCapabilityCanOwn = bOutSecondCanOwn;
		bOutFirstCanOwn = false;
		bOutSecondCanOwn = false;
		const FLayoutCellCandidateDomainRestriction* FirstRestriction =
			FirstPlacement.ChildDomainRestrictions.FindByPredicate(
				[&FirstPlannedCell](const FLayoutCellCandidateDomainRestriction& Restriction)
				{
					return Restriction.Cell == FirstPlannedCell.Cell;
				});
		const FLayoutCellCandidateDomainRestriction* SecondRestriction =
			SecondPlacement.ChildDomainRestrictions.FindByPredicate(
				[&SecondPlannedCell](const FLayoutCellCandidateDomainRestriction& Restriction)
				{
					return Restriction.Cell == SecondPlannedCell.Cell;
				});
		if (FirstRestriction == nullptr || SecondRestriction == nullptr)
		{
			return false;
		}

		if (bSuppressPassiveCellCandidate)
		{
			// Same-cell shared shell realizes only owner content; passive acceptance remains capability-backed.
			for (const FLayoutCandidateVariantIdentity& Identity : FirstRestriction->AllowedCandidates)
			{
				const FLayoutModuleSolveSnapshot* Module = FirstPlacement.ChildRequest.ModuleCatalog.Modules.FindByPredicate(
					[&Identity](const FLayoutModuleSolveSnapshot& Candidate)
					{
						return Candidate.SnapshotId == Identity.ModuleSnapshotId;
					});
				bOutFirstCanOwn |= bFirstCapabilityCanOwn
					&& Module != nullptr
					&& DoesExactSharedSeamCandidateOwn(
						*Module,
						Identity.YawRotationSteps,
						FirstFaceDirection,
						InterfaceFamily);
			}
			for (const FLayoutCandidateVariantIdentity& Identity : SecondRestriction->AllowedCandidates)
			{
				const FLayoutModuleSolveSnapshot* Module = SecondPlacement.ChildRequest.ModuleCatalog.Modules.FindByPredicate(
					[&Identity](const FLayoutModuleSolveSnapshot& Candidate)
					{
						return Candidate.SnapshotId == Identity.ModuleSnapshotId;
					});
				bOutSecondCanOwn |= bSecondCapabilityCanOwn
					&& Module != nullptr
					&& DoesExactSharedSeamCandidateOwn(
						*Module,
						Identity.YawRotationSteps,
						SecondFaceDirection,
						InterfaceFamily);
			}
			return bOutFirstCanOwn || bOutSecondCanOwn;
		}

		bool bFoundExactPair = false;
		for (const FLayoutCandidateVariantIdentity& FirstIdentity : FirstRestriction->AllowedCandidates)
		{
			const FLayoutModuleSolveSnapshot* FirstModule =
				FirstPlacement.ChildRequest.ModuleCatalog.Modules.FindByPredicate(
					[&FirstIdentity](const FLayoutModuleSolveSnapshot& Module)
					{
						return Module.SnapshotId == FirstIdentity.ModuleSnapshotId;
					});
			if (FirstModule == nullptr)
			{
				continue;
			}
			for (const FLayoutCandidateVariantIdentity& SecondIdentity : SecondRestriction->AllowedCandidates)
			{
				const FLayoutModuleSolveSnapshot* SecondModule =
					SecondPlacement.ChildRequest.ModuleCatalog.Modules.FindByPredicate(
						[&SecondIdentity](const FLayoutModuleSolveSnapshot& Module)
						{
							return Module.SnapshotId == SecondIdentity.ModuleSnapshotId;
						});
				if (SecondModule == nullptr)
				{
					continue;
				}

				bool bFirstCanOwn = false;
				bool bSecondCanOwn = false;
				FLayoutId FirstEvidenceId;
				FLayoutId SecondEvidenceId;
				FLayoutId WitnessId;
				if (TryCertifyExactSharedSeamCandidatePairForFaces(
					*FirstModule,
					FirstIdentity.YawRotationSteps,
					FirstFaceDirection,
					true,
					*SecondModule,
					SecondIdentity.YawRotationSteps,
					SecondFaceDirection,
					true,
					InterfaceFamily,
					bFirstCanOwn,
					bSecondCanOwn,
					FirstEvidenceId,
					SecondEvidenceId,
					WitnessId))
				{
					bFoundExactPair = true;
					bOutFirstCanOwn |= bFirstCanOwn;
					bOutSecondCanOwn |= bSecondCanOwn;
				}
			}
		}
		return bFoundExactPair && (bOutFirstCanOwn || bOutSecondCanOwn);
	}

	bool TryChooseCommittedSiblingSeamContractForFacePair(
		const FAutomaticChildPlacement& FirstPlacement,
		const FLayoutPlannedCell& FirstPlannedCell,
		const FAutomaticChildPlacement& SecondPlacement,
		const FLayoutPlannedCell& SecondPlannedCell,
		const ELayoutFaceDirection FirstFaceDirection,
		const ELayoutFaceDirection SecondFaceDirection,
		const bool bSuppressPassiveCellCandidate,
		FGameplayTag& OutInterfaceFamily,
		FString& OutOwnerRegionPath,
		FString& OutPassiveRegionPath,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection)
	{
		OutInterfaceFamily = FGameplayTag();
		OutOwnerRegionPath.Reset();
		OutPassiveRegionPath.Reset();
		OutFirstFaceDirection = FirstFaceDirection;
		OutSecondFaceDirection = SecondFaceDirection;

		const bool bFirstOwnsCommittedDoor = DoesPlacementContainCommittedEndpointAnchor(
			FirstPlacement,
			FirstPlannedCell.Cell,
			OutFirstFaceDirection);
		const bool bSecondOwnsCommittedDoor = DoesPlacementContainCommittedEndpointAnchor(
			SecondPlacement,
			SecondPlannedCell.Cell,
			OutSecondFaceDirection);
		const bool bFirstOwnsSiblingDoorRun = bFirstOwnsCommittedDoor
			|| DoesPlacementContainSiblingCommittedDoorAgainstPeer(FirstPlacement, SecondPlacement);
		const bool bSecondOwnsSiblingDoorRun = bSecondOwnsCommittedDoor
			|| DoesPlacementContainSiblingCommittedDoorAgainstPeer(SecondPlacement, FirstPlacement);
		TSet<FIntVector> FirstPlannedCells;
		TSet<FIntVector> SecondPlannedCells;
		for (const FLayoutPlannedCell& PlannedCell : FirstPlacement.ChildRequest.PlannedCells)
		{
			FirstPlannedCells.Add(PlannedCell.Cell);
		}
		for (const FLayoutPlannedCell& PlannedCell : SecondPlacement.ChildRequest.PlannedCells)
		{
			SecondPlannedCells.Add(PlannedCell.Cell);
		}

		const auto TryChooseRunOwner = [&](const FGameplayTag& InterfaceFamily) -> bool
		{
			bool bFirstCanOwn = false;
			bool bSecondCanOwn = false;
			FLayoutId FirstCapabilityId;
			FLayoutId SecondCapabilityId;
			const bool bHasCapabilityPair = TryResolveSharedSeamOwnershipWitness(
				FirstPlacement.ChildCapabilityEnvelope,
				OutFirstFaceDirection,
				SecondPlacement.ChildCapabilityEnvelope,
				OutSecondFaceDirection,
				InterfaceFamily,
				bFirstCanOwn,
				bSecondCanOwn,
				FirstCapabilityId,
				SecondCapabilityId);
			if (!bHasCapabilityPair && InterfaceFamily != LayoutGameplayTags::InterfacePartitionDoor)
			{
				return false;
			}
			if (!TryResolveExactSiblingSeamOwnershipWitness(
					FirstPlacement,
					FirstPlannedCell,
					OutFirstFaceDirection,
					SecondPlacement,
					SecondPlannedCell,
					OutSecondFaceDirection,
					InterfaceFamily,
					bSuppressPassiveCellCandidate,
					bFirstCanOwn,
					bSecondCanOwn))
			{
				return false;
			}
			if (!TryChooseCommittedSeamRunOwner(
					FString(),
					FirstPlacement.ChildRegionDebugPath,
					bFirstCanOwn,
					CountAttachedSeamSupportRun(
						FirstPlannedCells, FirstPlannedCell.Cell, OutFirstFaceDirection),
					bFirstOwnsSiblingDoorRun,
					SecondPlacement.ChildRegionDebugPath,
					bSecondCanOwn,
					CountAttachedSeamSupportRun(
						SecondPlannedCells, SecondPlannedCell.Cell, OutSecondFaceDirection),
					bSecondOwnsSiblingDoorRun,
					OutOwnerRegionPath,
					OutPassiveRegionPath))
			{
				return false;
			}
			OutInterfaceFamily = InterfaceFamily;
			return true;
		};

		if ((bFirstOwnsCommittedDoor || bSecondOwnsCommittedDoor)
			&& TryChooseRunOwner(LayoutGameplayTags::InterfacePartitionDoor))
		{
			return true;
		}

		const bool bEitherOverlapCellIsEntry =
			FirstPlannedCell.Intent == ELayoutCellIntent::Entry
			|| SecondPlannedCell.Intent == ELayoutCellIntent::Entry;
		if (!bEitherOverlapCellIsEntry
			&& TryChooseRunOwner(LayoutGameplayTags::InterfacePartitionSolid))
		{
			return true;
		}
		return TryChooseRunOwner(LayoutGameplayTags::InterfacePartitionDoor);
	}

	bool TryChooseCommittedSiblingSeamContract(
		const FAutomaticChildPlacement& FirstPlacement,
		const FLayoutPlannedCell& FirstPlannedCell,
		const FAutomaticChildPlacement& SecondPlacement,
		const FLayoutPlannedCell& SecondPlannedCell,
		FGameplayTag& OutInterfaceFamily,
		FString& OutOwnerRegionPath,
		FString& OutPassiveRegionPath,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection)
	{
		if (!TryGetSharedOverlapFaceDirections(
			FirstPlannedCell.Cell,
			FirstPlacement.ChildRequest.FootprintSize,
			SecondPlannedCell.Cell,
			SecondPlacement.ChildRequest.FootprintSize,
			OutFirstFaceDirection,
			OutSecondFaceDirection))
		{
			return false;
		}

		return TryChooseCommittedSiblingSeamContractForFacePair(
			FirstPlacement,
			FirstPlannedCell,
			SecondPlacement,
			SecondPlannedCell,
			OutFirstFaceDirection,
			OutSecondFaceDirection,
			true,
			OutInterfaceFamily,
			OutOwnerRegionPath,
			OutPassiveRegionPath,
			OutFirstFaceDirection,
			OutSecondFaceDirection);
	}

	void MergeCommittedPartitionSeamSegmentsIntoRuns(
		TArray<FLayoutPartitionSeamRecord>& InOutSeams)
	{
		struct FSeamSegment
		{
			FLayoutPartitionSeamRecord Template;
			FIntVector OwnerCell = FIntVector::ZeroValue;
			FIntVector PassiveCell = FIntVector::ZeroValue;
			int32 Coordinate = 0;
		};

		TMap<FString, TArray<FSeamSegment>> SegmentsByLine;
		for (const FLayoutPartitionSeamRecord& Seam : InOutSeams)
		{
			const FIntVector OwnerStep(
				FMath::Clamp(Seam.OwnerEndCell.X - Seam.OwnerStartCell.X, -1, 1),
				FMath::Clamp(Seam.OwnerEndCell.Y - Seam.OwnerStartCell.Y, -1, 1),
				FMath::Clamp(Seam.OwnerEndCell.Z - Seam.OwnerStartCell.Z, -1, 1));
			const FIntVector PassiveStep(
				FMath::Clamp(Seam.PassiveEndCell.X - Seam.PassiveStartCell.X, -1, 1),
				FMath::Clamp(Seam.PassiveEndCell.Y - Seam.PassiveStartCell.Y, -1, 1),
				FMath::Clamp(Seam.PassiveEndCell.Z - Seam.PassiveStartCell.Z, -1, 1));
			const int32 SegmentCount = FMath::Max(1, Seam.SegmentCount);
			for (int32 SegmentIndex = 0; SegmentIndex < SegmentCount; ++SegmentIndex)
			{
				FSeamSegment Segment;
				Segment.Template = Seam;
				Segment.OwnerCell = Seam.OwnerStartCell + OwnerStep * SegmentIndex;
				Segment.PassiveCell = Seam.PassiveStartCell + PassiveStep * SegmentIndex;
				const bool bRunsAlongY = Seam.OwnerFaceDirection == ELayoutFaceDirection::PosX
					|| Seam.OwnerFaceDirection == ELayoutFaceDirection::NegX;
				Segment.Coordinate = bRunsAlongY ? Segment.OwnerCell.Y : Segment.OwnerCell.X;
				const FString LineKey = FString::Printf(
					TEXT("%s|%s|%s|%s|%d|%d|%d|%d|%d|%d|%d|%s|%s"),
					*Seam.ParentRegionDebugPath,
					*Seam.OwnerRegionDebugPath,
					*Seam.PassiveRegionDebugPath,
					Seam.InterfaceFamily.IsValid() ? *Seam.InterfaceFamily.ToString() : TEXT("None"),
					static_cast<int32>(Seam.OwnerFaceDirection),
					static_cast<int32>(Seam.PassiveFaceDirection),
					bRunsAlongY ? Segment.OwnerCell.X : Segment.OwnerCell.Y,
					Segment.OwnerCell.Z,
					bRunsAlongY ? Segment.PassiveCell.X : Segment.PassiveCell.Y,
					Segment.PassiveCell.Z,
					Seam.bCountsTowardClosure ? 1 : 0,
					Seam.ProviderId.IsNone() ? TEXT("None") : *Seam.ProviderId.ToString(),
					Seam.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
						? *Seam.SeamId.ToString()
						: TEXT("SolidRun"));
				SegmentsByLine.FindOrAdd(LineKey).Add(MoveTemp(Segment));
			}
		}

		TArray<FLayoutPartitionSeamRecord> MergedSeams;
		for (TPair<FString, TArray<FSeamSegment>>& Pair : SegmentsByLine)
		{
			TArray<FSeamSegment>& Segments = Pair.Value;
			Segments.Sort([](const FSeamSegment& Left, const FSeamSegment& Right)
			{
				if (Left.Coordinate != Right.Coordinate)
				{
					return Left.Coordinate < Right.Coordinate;
				}
				if (Left.OwnerCell != Right.OwnerCell)
				{
					return Left.OwnerCell.X != Right.OwnerCell.X
						? Left.OwnerCell.X < Right.OwnerCell.X
						: Left.OwnerCell.Y != Right.OwnerCell.Y
							? Left.OwnerCell.Y < Right.OwnerCell.Y
							: Left.OwnerCell.Z < Right.OwnerCell.Z;
				}
				return Left.PassiveCell.X != Right.PassiveCell.X
					? Left.PassiveCell.X < Right.PassiveCell.X
					: Left.PassiveCell.Y != Right.PassiveCell.Y
						? Left.PassiveCell.Y < Right.PassiveCell.Y
						: Left.PassiveCell.Z < Right.PassiveCell.Z;
			});

			int32 RunStart = 0;
			for (int32 SegmentIndex = 1; SegmentIndex <= Segments.Num(); ++SegmentIndex)
			{
				const FIntVector PassiveDelta = SegmentIndex < Segments.Num()
					? Segments[SegmentIndex].PassiveCell - Segments[SegmentIndex - 1].PassiveCell
					: FIntVector::ZeroValue;
				const bool bContinuesRun = SegmentIndex < Segments.Num()
					&& Segments[SegmentIndex].Coordinate == Segments[SegmentIndex - 1].Coordinate + 1
					&& FMath::Abs(PassiveDelta.X) + FMath::Abs(PassiveDelta.Y) + FMath::Abs(PassiveDelta.Z) == 1;
				if (bContinuesRun)
				{
					continue;
				}

				const FSeamSegment& First = Segments[RunStart];
				const FSeamSegment& Last = Segments[SegmentIndex - 1];
				FLayoutPartitionSeamRecord& Merged = MergedSeams.AddDefaulted_GetRef();
				Merged = First.Template;
				Merged.OwnerStartCell = First.OwnerCell;
				Merged.OwnerEndCell = Last.OwnerCell;
				Merged.PassiveStartCell = First.PassiveCell;
				Merged.PassiveEndCell = Last.PassiveCell;
				Merged.SegmentCount = SegmentIndex - RunStart;
				Merged.SeamId = FLayoutId(*FString::Printf(
					TEXT("Seam.%s.%s.%s.%s.%s.%s"),
					*Merged.ParentRegionDebugPath,
					*Merged.OwnerRegionDebugPath,
					*Merged.PassiveRegionDebugPath,
					Merged.InterfaceFamily.IsValid() ? *Merged.InterfaceFamily.ToString() : TEXT("None"),
					*Merged.OwnerStartCell.ToString(),
					*Merged.OwnerEndCell.ToString()));
				RunStart = SegmentIndex;
			}
		}
		SortPartitionSeamRecords(MergedSeams);
		InOutSeams = MoveTemp(MergedSeams);
	}

	bool BuildCommittedPartitionSeams(
		const FString& ParentRegionDebugPath,
		const TArray<FAutomaticChildPlacement>& CandidatePlacements,
		TArray<FLayoutPartitionSeamRecord>& OutPlannedPartitionSeams,
		FString* OutFailureReason = nullptr)
	{
		OutPlannedPartitionSeams.Reset();
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		for (const FAutomaticChildPlacement& ChildPlacement : CandidatePlacements)
		{
			for (const FSharedParentChildFace& SharedFace : ChildPlacement.SharedParentChildFaces)
			{
				if (SharedFace.ReciprocalDomainWitnessId.IsNone())
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("Committed parent/child seam at %s has no exact reciprocal module/yaw domain witness."),
							*SharedFace.ParentCell.ToString());
					}
					return false;
				}
				const FIntVector ChildWorldCell = MapSourceChildCellToParent(
					SharedFace.ChildLocalCell,
					ChildPlacement.RegionCellOffset,
					&ChildPlacement.StageMapping);
				const FLayoutChildStageMappedCell* SharedMappedCell =
					ChildPlacement.StageMapping.Cells.FindByPredicate(
						[&SharedFace](const FLayoutChildStageMappedCell& Cell)
						{
							return Cell.SourceChildCell == SharedFace.ChildLocalCell;
						});
				const FIntVector ChildMappedLocalCell = SharedMappedCell != nullptr
					? SharedMappedCell->MappedChildCell
					: SharedFace.ChildLocalCell;
				if (DoesPlacementContainCommittedEndpointAnchor(
					ChildPlacement,
					ChildMappedLocalCell,
					SharedFace.FaceDirection))
				{
					// Direct Entry handoff remains an endpoint/traversal contract rather
					// than competing structural seam ownership on the same face.
					continue;
				}
				FString OwnerRegionPath;
				FString PassiveRegionPath;
				if (!TryChooseCommittedSeamRunOwner(
						ParentRegionDebugPath,
						ParentRegionDebugPath,
						SharedFace.bParentCanOwnSeam,
						SharedFace.ParentOwnerSupportRunLength,
						false,
						ChildPlacement.ChildRegionDebugPath,
						SharedFace.bChildCanOwnSeam,
						SharedFace.ChildOwnerSupportRunLength,
						false,
						OwnerRegionPath,
						PassiveRegionPath))
				{
					if (OutFailureReason != nullptr)
					{
						*OutFailureReason = FString::Printf(
							TEXT("Committed parent/child seam at %s has no capability-certified owner across '%s' and '%s'."),
							*ChildWorldCell.ToString(),
							*ParentRegionDebugPath,
							*ChildPlacement.ChildRegionDebugPath);
					}
					return false;
				}
				const bool bParentOwns = OwnerRegionPath == ParentRegionDebugPath;
				AddCommittedPartitionSeamRecord(
					ParentRegionDebugPath,
					OwnerRegionPath,
					PassiveRegionPath,
					SharedFace.InterfaceFamily,
					SharedFace.FaceDirection,
					SharedFace.FaceDirection,
					bParentOwns ? SharedFace.ParentCell : ChildWorldCell,
					bParentOwns ? SharedFace.ParentCell : ChildWorldCell,
					bParentOwns ? ChildWorldCell : SharedFace.ParentCell,
					bParentOwns ? ChildWorldCell : SharedFace.ParentCell,
					OutPlannedPartitionSeams);
			}
		}

		for (int32 FirstIndex = 0; FirstIndex < CandidatePlacements.Num(); ++FirstIndex)
		{
			for (int32 SecondIndex = FirstIndex + 1; SecondIndex < CandidatePlacements.Num(); ++SecondIndex)
			{
				const FAutomaticChildPlacement& FirstPlacement = CandidatePlacements[FirstIndex];
				const FAutomaticChildPlacement& SecondPlacement = CandidatePlacements[SecondIndex];
				TSet<FIntVector> FirstWorldCells;
				TSet<FIntVector> SecondWorldCells;
				FirstWorldCells.Reserve(FirstPlacement.ChildRequest.PlannedCells.Num());
				SecondWorldCells.Reserve(SecondPlacement.ChildRequest.PlannedCells.Num());
				for (int32 CellIndex = 0;
					CellIndex < FirstPlacement.ChildRequest.PlannedCells.Num();
					++CellIndex)
				{
					FirstWorldCells.Add(
						FirstPlacement.ParentTranslatedPlannedCells.IsValidIndex(CellIndex)
							? FirstPlacement.ParentTranslatedPlannedCells[CellIndex].Cell
							: FirstPlacement.ChildRequest.PlannedCells[CellIndex].Cell
								+ FirstPlacement.RegionCellOffset);
				}
				for (int32 CellIndex = 0;
					CellIndex < SecondPlacement.ChildRequest.PlannedCells.Num();
					++CellIndex)
				{
					SecondWorldCells.Add(
						SecondPlacement.ParentTranslatedPlannedCells.IsValidIndex(CellIndex)
							? SecondPlacement.ParentTranslatedPlannedCells[CellIndex].Cell
							: SecondPlacement.ChildRequest.PlannedCells[CellIndex].Cell
								+ SecondPlacement.RegionCellOffset);
				}
				for (int32 FirstCellIndex = 0;
					FirstCellIndex < FirstPlacement.ChildRequest.PlannedCells.Num();
					++FirstCellIndex)
				{
					const FLayoutPlannedCell& FirstPlannedCell =
						FirstPlacement.ChildRequest.PlannedCells[FirstCellIndex];
					const FIntVector FirstWorldCell =
						FirstPlacement.ParentTranslatedPlannedCells.IsValidIndex(FirstCellIndex)
							? FirstPlacement.ParentTranslatedPlannedCells[FirstCellIndex].Cell
							: FirstPlannedCell.Cell + FirstPlacement.RegionCellOffset;
					for (int32 SecondCellIndex = 0;
						SecondCellIndex < SecondPlacement.ChildRequest.PlannedCells.Num();
						++SecondCellIndex)
					{
						const FLayoutPlannedCell& SecondPlannedCell =
							SecondPlacement.ChildRequest.PlannedCells[SecondCellIndex];
						const FIntVector SecondWorldCell =
							SecondPlacement.ParentTranslatedPlannedCells.IsValidIndex(SecondCellIndex)
								? SecondPlacement.ParentTranslatedPlannedCells[SecondCellIndex].Cell
								: SecondPlannedCell.Cell + SecondPlacement.RegionCellOffset;
						ELayoutFaceDirection AdjacentFirstFaceDirection = ELayoutFaceDirection::PosX;
						ELayoutFaceDirection AdjacentSecondFaceDirection = ELayoutFaceDirection::NegX;
						if (!TryGetAdjacentSharedFaceDirections(
							FirstWorldCell,
							SecondWorldCell,
							AdjacentFirstFaceDirection,
							AdjacentSecondFaceDirection))
						{
							continue;
						}

						// Only exclusive volumes separated by reciprocal local perimeter
						// faces form an adjacent sibling seam. If either neighboring cell
						// also belongs to the other child, this edge is internal to an
						// overlapping child volume and same-cell shared-shell logic owns it.
						if (FirstWorldCells.Contains(SecondWorldCell)
							|| SecondWorldCells.Contains(FirstWorldCell)
							|| !DoesCellExposeFootprintPerimeterFace(
								FirstPlannedCell.Cell,
								FirstPlacement.ChildRequest.FootprintSize,
								AdjacentFirstFaceDirection)
							|| !DoesCellExposeFootprintPerimeterFace(
								SecondPlannedCell.Cell,
								SecondPlacement.ChildRequest.FootprintSize,
								AdjacentSecondFaceDirection))
						{
							continue;
						}

						FGameplayTag AdjacentInterfaceFamily;
						FString AdjacentOwnerRegionPath;
						FString AdjacentPassiveRegionPath;
						ELayoutFaceDirection ChosenFirstFaceDirection = AdjacentFirstFaceDirection;
						ELayoutFaceDirection ChosenSecondFaceDirection = AdjacentSecondFaceDirection;
						if (!TryChooseCommittedSiblingSeamContractForFacePair(
							FirstPlacement,
							FirstPlannedCell,
							SecondPlacement,
							SecondPlannedCell,
							AdjacentFirstFaceDirection,
							AdjacentSecondFaceDirection,
							false,
							AdjacentInterfaceFamily,
							AdjacentOwnerRegionPath,
							AdjacentPassiveRegionPath,
							ChosenFirstFaceDirection,
							ChosenSecondFaceDirection))
						{
							if (OutFailureReason != nullptr)
							{
								*OutFailureReason = FString::Printf(
									TEXT("Recursive sibling seam between world cells %s and %s across '%s' and '%s' did not produce an explicit committed seam contract."),
									*FirstWorldCell.ToString(),
									*SecondWorldCell.ToString(),
									*FirstPlacement.ChildRegionDebugPath,
									*SecondPlacement.ChildRegionDebugPath);
							}
							return false;
						}

						const bool bAdjacentOwnerIsFirst = AdjacentOwnerRegionPath == FirstPlacement.ChildRegionDebugPath;
						AddCommittedPartitionSeamRecord(
							ParentRegionDebugPath,
							AdjacentOwnerRegionPath,
							AdjacentPassiveRegionPath,
							AdjacentInterfaceFamily,
							bAdjacentOwnerIsFirst ? ChosenFirstFaceDirection : ChosenSecondFaceDirection,
							bAdjacentOwnerIsFirst ? ChosenSecondFaceDirection : ChosenFirstFaceDirection,
							bAdjacentOwnerIsFirst ? FirstWorldCell : SecondWorldCell,
							bAdjacentOwnerIsFirst ? FirstWorldCell : SecondWorldCell,
							bAdjacentOwnerIsFirst ? SecondWorldCell : FirstWorldCell,
							bAdjacentOwnerIsFirst ? SecondWorldCell : FirstWorldCell,
							OutPlannedPartitionSeams);
					}

					if (FirstPlannedCell.Intent != ELayoutCellIntent::Boundary
						&& FirstPlannedCell.Intent != ELayoutCellIntent::Entry)
					{
						continue;
					}

					const FLayoutPlannedCell* MatchingSecondCell = nullptr;
					for (int32 SecondCellIndex = 0;
						SecondCellIndex < SecondPlacement.ChildRequest.PlannedCells.Num();
						++SecondCellIndex)
					{
						const FLayoutPlannedCell& CandidateSecondCell =
							SecondPlacement.ChildRequest.PlannedCells[SecondCellIndex];
						const FIntVector CandidateSecondWorldCell =
							SecondPlacement.ParentTranslatedPlannedCells.IsValidIndex(SecondCellIndex)
								? SecondPlacement.ParentTranslatedPlannedCells[SecondCellIndex].Cell
								: CandidateSecondCell.Cell + SecondPlacement.RegionCellOffset;
						if ((CandidateSecondCell.Intent == ELayoutCellIntent::Boundary
								|| CandidateSecondCell.Intent == ELayoutCellIntent::Entry)
							&& CandidateSecondWorldCell == FirstWorldCell)
						{
							MatchingSecondCell = &CandidateSecondCell;
							break;
						}
					}
					if (MatchingSecondCell == nullptr)
					{
						continue;
					}

					FGameplayTag InterfaceFamily;
					FString OwnerRegionPath;
					FString PassiveRegionPath;
					ELayoutFaceDirection FirstFaceDirection = ELayoutFaceDirection::PosX;
					ELayoutFaceDirection SecondFaceDirection = ELayoutFaceDirection::NegX;
					if (!TryChooseCommittedSiblingSeamContract(
						FirstPlacement,
						FirstPlannedCell,
						SecondPlacement,
						*MatchingSecondCell,
						InterfaceFamily,
						OwnerRegionPath,
						PassiveRegionPath,
						FirstFaceDirection,
						SecondFaceDirection))
					{
						if (OutFailureReason != nullptr)
						{
							*OutFailureReason = FString::Printf(
								TEXT("Recursive sibling overlap at world cell %s between '%s' and '%s' did not produce an explicit committed seam contract."),
								*FirstWorldCell.ToString(),
								*FirstPlacement.ChildRegionDebugPath,
								*SecondPlacement.ChildRegionDebugPath);
						}
						return false;
					}

					const bool bOwnerIsFirst = OwnerRegionPath == FirstPlacement.ChildRegionDebugPath;
					const ELayoutFaceDirection SiblingOwnerFaceDirection = bOwnerIsFirst
						? FirstFaceDirection
						: SecondFaceDirection;
					// One sibling owns same-cell shared-shell content. Replace both
					// provisional parent/child copies at that exact slot with one child-
					// owned parent suppression seam, then retain sibling owner/passive.
					OutPlannedPartitionSeams.RemoveAll(
						[&](const FLayoutPartitionSeamRecord& Seam)
						{
							const bool bConnectsParentToEitherSibling =
								(Seam.OwnerRegionDebugPath == ParentRegionDebugPath
									&& (Seam.PassiveRegionDebugPath == FirstPlacement.ChildRegionDebugPath
										|| Seam.PassiveRegionDebugPath == SecondPlacement.ChildRegionDebugPath))
								|| (Seam.PassiveRegionDebugPath == ParentRegionDebugPath
									&& (Seam.OwnerRegionDebugPath == FirstPlacement.ChildRegionDebugPath
										|| Seam.OwnerRegionDebugPath == SecondPlacement.ChildRegionDebugPath));
							return bConnectsParentToEitherSibling
								&& Seam.OwnerStartCell == FirstWorldCell
								&& Seam.OwnerEndCell == FirstWorldCell
								&& Seam.PassiveStartCell == FirstWorldCell
								&& Seam.PassiveEndCell == FirstWorldCell;
						});
					AddCommittedPartitionSeamRecord(
						ParentRegionDebugPath,
						OwnerRegionPath,
						ParentRegionDebugPath,
						InterfaceFamily,
						SiblingOwnerFaceDirection,
						SiblingOwnerFaceDirection,
						FirstWorldCell,
						FirstWorldCell,
						FirstWorldCell,
						FirstWorldCell,
						OutPlannedPartitionSeams);
					AddCommittedPartitionSeamRecord(
						ParentRegionDebugPath,
						OwnerRegionPath,
						PassiveRegionPath,
						InterfaceFamily,
						bOwnerIsFirst ? FirstFaceDirection : SecondFaceDirection,
						bOwnerIsFirst ? SecondFaceDirection : FirstFaceDirection,
						FirstWorldCell,
						FirstWorldCell,
						FirstWorldCell,
						FirstWorldCell,
						OutPlannedPartitionSeams);
				}
			}
		}

		MergeCommittedPartitionSeamSegmentsIntoRuns(OutPlannedPartitionSeams);
		return true;
	}

	/** Promotes committed child Entry anchors when an exact sibling Door seam settles. */
	void ApplyCommittedSiblingDoorTraversalToPlacements(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		TArray<FAutomaticChildPlacement>& InOutPlacements)
	{
		for (const FLayoutPartitionSeamRecord& Seam : PlannedPartitionSeams)
		{
			if (Seam.InterfaceFamily != LayoutGameplayTags::InterfacePartitionDoor)
			{
				continue;
			}

			const auto ApplyRegionSide = [&](
				const FString& RegionPath,
				const FIntVector& SeamCell,
				const ELayoutFaceDirection FaceDirection)
			{
				FAutomaticChildPlacement* Placement =
					InOutPlacements.FindByPredicate(
						[&RegionPath](const FAutomaticChildPlacement& Candidate)
						{
							return Candidate.ChildRegionDebugPath == RegionPath;
						});
				if (Placement == nullptr)
				{
					return;
				}

				bool bUpdatedAnchor = false;
				for (FLayoutCommittedEndpointAnchor& Anchor :
					Placement->DirectChildCommitment.EndpointCommitments)
				{
					const FIntVector AnchorWorldCell = MapChildResultCellToParent(
						Anchor.LocalCell,
						Placement->RegionCellOffset,
						&Placement->StageMapping);
					if (AnchorWorldCell == SeamCell
						&& Anchor.FaceDirection == FaceDirection)
					{
						Anchor.TraversalChannels.AddTag(
							LayoutGameplayTags::TraversalPrimary);
						bUpdatedAnchor = true;
					}
				}
				if (bUpdatedAnchor)
				{
					Placement->ChildRequest.CommittedEndpointAnchors =
						Placement->DirectChildCommitment.EndpointCommitments;
				}
			};

			ApplyRegionSide(
				Seam.OwnerRegionDebugPath,
				Seam.OwnerStartCell,
				Seam.OwnerFaceDirection);
			ApplyRegionSide(
				Seam.PassiveRegionDebugPath,
				Seam.PassiveStartCell,
				Seam.PassiveFaceDirection);
		}
	}

	/** Returns known owning-rectangle exterior faces; unknown footprints add no claims, nor do child boundaries. */
	uint8 GetOwningPerimeterFaceMask(const FIntVector& Cell, const FIntPoint& Footprint)
	{
		if (Footprint.X <= 0 || Footprint.Y <= 0) return 0;
		uint8 Mask = 0;
		if (Cell.X == 0) Mask |= LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
		if (Cell.X == Footprint.X - 1) Mask |= LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
		if (Cell.Y == 0) Mask |= LayoutFaceDirectionMask(ELayoutFaceDirection::NegY);
		if (Cell.Y == Footprint.Y - 1) Mask |= LayoutFaceDirectionMask(ELayoutFaceDirection::PosY);
		return Mask;
	}

	/** Restricts every owner seam cell to exact module/yaw candidates with valid usage. */
	bool ApplyOwnedSeamUsageRestrictions(
		const FString& ParentRegionDebugPath,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		FLayoutRegionSolveRequest& InOutParentRequest,
		TArray<FAutomaticChildPlacement>& InOutPlacements,
		FString& OutFailureReason)
	{
		SCOPED_NAMED_EVENT(Layout_ParentValidation_SeamRestrictions, FColor::Orange);
		struct FOwnerFaceRequirement
		{
			FGameplayTag InterfaceFamily;
			ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		};
		struct FOwnerCellRequirement
		{
			FString OwnerRegionDebugPath;
			FIntVector WorldCell = FIntVector::ZeroValue;
			bool bJunction = false;
			uint8 OwningExteriorFaces = 0;
			FLayoutId AdjacencyClassId;
			TArray<FOwnerFaceRequirement> Faces;
		};

		TMap<FString, FOwnerCellRequirement> RequirementsByCell;
		const auto FindOrAddRequirement = [&](
			const FString& OwnerPath,
			const FIntVector& WorldCell) -> FOwnerCellRequirement&
		{
			const FString Key = FString::Printf(
				TEXT("%s|%s"),
				*OwnerPath,
				*WorldCell.ToString());
			FOwnerCellRequirement& Requirement = RequirementsByCell.FindOrAdd(Key);
			Requirement.OwnerRegionDebugPath = OwnerPath;
			Requirement.WorldCell = WorldCell;
			return Requirement;
		};
		const auto AddFace = [](
			FOwnerCellRequirement& Requirement,
			const FGameplayTag& InterfaceFamily,
			const ELayoutFaceDirection FaceDirection)
		{
			if (!Requirement.Faces.ContainsByPredicate(
				[&](const FOwnerFaceRequirement& Existing)
				{
					return Existing.InterfaceFamily == InterfaceFamily
						&& Existing.FaceDirection == FaceDirection;
				}))
			{
				FOwnerFaceRequirement& Face = Requirement.Faces.AddDefaulted_GetRef();
				Face.InterfaceFamily = InterfaceFamily;
				Face.FaceDirection = FaceDirection;
			}
		};

		for (const FLayoutPartitionSeamRecord& Seam : PartitionSeams)
		{
			const FIntVector Step(
				FMath::Clamp(Seam.OwnerEndCell.X - Seam.OwnerStartCell.X, -1, 1),
				FMath::Clamp(Seam.OwnerEndCell.Y - Seam.OwnerStartCell.Y, -1, 1),
				FMath::Clamp(Seam.OwnerEndCell.Z - Seam.OwnerStartCell.Z, -1, 1));
			for (int32 SegmentIndex = 0;
				SegmentIndex < FMath::Max(1, Seam.SegmentCount);
				++SegmentIndex)
			{
				FOwnerCellRequirement& Requirement = FindOrAddRequirement(
					Seam.OwnerRegionDebugPath,
					Seam.OwnerStartCell + Step * SegmentIndex);
				AddFace(Requirement, Seam.InterfaceFamily, Seam.OwnerFaceDirection);
			}
		}

		for (const LayoutRegionScheduleSolverFacade::FOwnedSeamJunctionRequirement& Junction :
			LayoutRegionScheduleSolverFacade::BuildOwnerSideJunctionRequirements(
				PartitionSeams, &InOutParentRequest))
		{
			FOwnerCellRequirement& Requirement = FindOrAddRequirement(
				Junction.OwnerRegionDebugPath,
				Junction.JunctionCell);
			Requirement.bJunction = true;
			Requirement.AdjacencyClassId = Junction.AdjacencyClassId;
			AddFace(
				Requirement,
				Junction.InterfaceFamily,
				Junction.ContinuingOwnerFaceDirection);
			AddFace(
				Requirement,
				Junction.InterfaceFamily,
				Junction.BranchOwnerFaceDirection);
		}

		const auto CandidateSupportsRequirement = [](
			const FLayoutModuleSolveSnapshot& Module,
			const int32 YawRotationSteps,
			const FOwnerCellRequirement& Requirement)
		{
			for (int32 Index = 0; Index < 4; ++Index)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(Index);
				if (!(Requirement.OwningExteriorFaces & LayoutFaceDirectionMask(Direction))) continue;
				FLayoutFaceRule Rule;
				if (TryGetSnapshotWorldFaceRule(Module, Direction, YawRotationSteps, Rule)
					&& Rule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceInterior) return false;
			}
			for (const FOwnerFaceRequirement& RequiredFace : Requirement.Faces)
			{
				const bool bSupported = Module.SeamProviderIntents.ContainsByPredicate(
					[&](const FLayoutSeamProviderIntent& Intent)
					{
						const bool bUsageAllowed = Requirement.bJunction
							? Intent.JunctionUsage != ELayoutSeamJunctionUsage::NonJunctionOnly
							: Intent.JunctionUsage != ELayoutSeamJunctionUsage::JunctionOnly;
						return bUsageAllowed
							&& Intent.bCanOwnSeam
							&& Intent.InterfaceFamily == RequiredFace.InterfaceFamily
							&& (!Requirement.bJunction
								|| Module.DerivedSpanOffers.ContainsByPredicate(
								[&](const FLayoutDerivedSpanOffer& Offer)
								{
									return Offer.bSealsBoundary
										&& FLayoutDirectionUtils::RotateYaw(
											Offer.FaceDirection,
											YawRotationSteps)
											== RequiredFace.FaceDirection
										&& LayoutPlacementOccupancy::ProjectLocalCellToWorld(
											FIntVector::ZeroValue,
											Offer.LocalCell,
											Module.BoundsCells,
											YawRotationSteps)
											== FIntVector::ZeroValue;
								}));
					});
				if (!bSupported)
				{
					return false;
				}
			}
			return true;
		};

		TArray<FString> SortedRequirementKeys;
		RequirementsByCell.GenerateKeyArray(SortedRequirementKeys);
		SortedRequirementKeys.Sort();
		for (const FString& RequirementKey : SortedRequirementKeys)
		{
			FOwnerCellRequirement& Requirement =
				RequirementsByCell.FindChecked(RequirementKey);
			Requirement.Faces.Sort([](
				const FOwnerFaceRequirement& Left,
				const FOwnerFaceRequirement& Right)
			{
				if (Left.InterfaceFamily != Right.InterfaceFamily)
				{
					return Left.InterfaceFamily.ToString()
						< Right.InterfaceFamily.ToString();
				}
				return static_cast<int32>(Left.FaceDirection)
					< static_cast<int32>(Right.FaceDirection);
			});
			FLayoutRegionSolveRequest* OwnerRequest = nullptr;
			TArray<FLayoutCellCandidateDomainRestriction>* Restrictions = nullptr;
			FIntVector LocalCell = Requirement.WorldCell;
			FAutomaticChildPlacement* OwnerPlacement = nullptr;
			if (Requirement.OwnerRegionDebugPath == ParentRegionDebugPath)
			{
				OwnerRequest = &InOutParentRequest;
				Restrictions = &InOutParentRequest.CandidateDomainRestrictions;
			}
			else
			{
				OwnerPlacement = InOutPlacements.FindByPredicate(
					[&](const FAutomaticChildPlacement& Placement)
					{
						return Placement.ChildRegionDebugPath
							== Requirement.OwnerRegionDebugPath;
					});
				if (OwnerPlacement != nullptr)
				{
					OwnerRequest = &OwnerPlacement->ChildRequest;
					Restrictions = &OwnerPlacement->ChildDomainRestrictions;
					LocalCell = MapParentCellToChildResultCell(
						Requirement.WorldCell,
						OwnerPlacement->RegionCellOffset,
						&OwnerPlacement->StageMapping);
				}
			}
			if (OwnerRequest == nullptr || Restrictions == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Owned seam cell %s references missing owner region '%s'."),
					*Requirement.WorldCell.ToString(),
					*Requirement.OwnerRegionDebugPath);
				return false;
			}

			Requirement.OwningExteriorFaces = GetOwningPerimeterFaceMask(LocalCell, OwnerRequest->FootprintSize);
			FLayoutCellCandidateDomainRestriction* Restriction =
				Restrictions->FindByPredicate(
					[&LocalCell](const FLayoutCellCandidateDomainRestriction& Existing)
					{
						return Existing.Cell == LocalCell;
					});
			const bool bCreatedRestriction = Restriction == nullptr;
			if (bCreatedRestriction)
			{
				Restriction = &Restrictions->AddDefaulted_GetRef();
				Restriction->Cell = LocalCell;
				for (const FLayoutModuleSolveSnapshot& Module : OwnerRequest->ModuleCatalog.Modules)
				{
					TArray<int32> AllowedYaws = Module.AllowedYawRotationSteps;
					if (AllowedYaws.IsEmpty())
					{
						AllowedYaws.Add(0);
					}
					for (const int32 YawRotationSteps : AllowedYaws)
					{
						FLayoutCandidateVariantIdentity& Identity =
							Restriction->AllowedCandidates.AddDefaulted_GetRef();
						Identity.ModuleSnapshotId = Module.SnapshotId;
						Identity.YawRotationSteps = YawRotationSteps;
					}
				}
			}
			Restriction->bTreatAsJunctionPlacementZone |= Requirement.bJunction;
			TArray<FString> ExcludedProviderCandidates;
			Restriction->AllowedCandidates.RemoveAll(
				[&](const FLayoutCandidateVariantIdentity& Identity)
				{
					const FLayoutModuleSolveSnapshot* Module =
						OwnerRequest->ModuleCatalog.Modules.FindByPredicate(
							[&](const FLayoutModuleSolveSnapshot& Candidate)
							{
								return Candidate.SnapshotId == Identity.ModuleSnapshotId;
							});
					const bool bExcluded = Module == nullptr
						|| !CandidateSupportsRequirement(
							*Module,
							Identity.YawRotationSteps,
							Requirement);
					if (bExcluded && ExcludedProviderCandidates.Num() < 16)
					{
						ExcludedProviderCandidates.Add(FString::Printf(
							TEXT("%s/Yaw%d"),
							*Identity.ModuleSnapshotId.ToString(),
							Identity.YawRotationSteps));
					}
					return bExcluded;
				});
			if (Restriction->AllowedCandidates.IsEmpty())
			{
				OutFailureReason = FString::Printf(
					TEXT("Owned %s seam domain is empty for region '%s' at world cell %s local cell %s adjacency=%s faces=%s excludedProviders=[%s]."),
					Requirement.bJunction ? TEXT("junction") : TEXT("ordinary"),
					*Requirement.OwnerRegionDebugPath,
					*Requirement.WorldCell.ToString(),
					*LocalCell.ToString(),
					Requirement.AdjacencyClassId.IsNone()
						? TEXT("Ordinary")
						: *Requirement.AdjacencyClassId.ToString(),
					*FString::JoinBy(
						Requirement.Faces,
						TEXT(","),
						[](const FOwnerFaceRequirement& Face)
						{
							return FString::Printf(
								TEXT("%s:%d"),
								*Face.InterfaceFamily.ToString(),
								static_cast<int32>(Face.FaceDirection));
						}),
					*FString::Join(ExcludedProviderCandidates, TEXT(",")));
				return false;
			}
			Restriction->AllowedCandidates.Sort([](
				const FLayoutCandidateVariantIdentity& Left,
				const FLayoutCandidateVariantIdentity& Right)
			{
				if (Left.ModuleSnapshotId != Right.ModuleSnapshotId)
				{
					return Left.ModuleSnapshotId.LexicalLess(Right.ModuleSnapshotId);
				}
				return Left.YawRotationSteps < Right.YawRotationSteps;
			});
			if (bCreatedRestriction)
			{
				Restriction->RestrictionId = FLayoutId(*FString::Printf(
					TEXT("SeamUsage.%08X"),
					FCrc::StrCrc32(*FString::Printf(
						TEXT("%s|%s|%d|%s|%s|%s"),
						*Requirement.OwnerRegionDebugPath,
						*LocalCell.ToString(),
						Requirement.bJunction ? 1 : 0,
						*Requirement.AdjacencyClassId.ToString(),
						*FString::JoinBy(
							Requirement.Faces,
							TEXT(","),
							[](const FOwnerFaceRequirement& Face)
							{
								return FString::Printf(
									TEXT("%s:%d"),
									*Face.InterfaceFamily.ToString(),
									static_cast<int32>(Face.FaceDirection));
							}),
						*FString::JoinBy(
							Restriction->AllowedCandidates,
							TEXT(","),
							[](const FLayoutCandidateVariantIdentity& Candidate)
							{
								return FString::Printf(
									TEXT("%s:%d"),
									*Candidate.ModuleSnapshotId.ToString(),
									Candidate.YawRotationSteps);
							})))));
			}
			if (Requirement.bJunction
				&& Requirement.OwnerRegionDebugPath == ParentRegionDebugPath)
			{
				// Topology owns junction selection; incoming face tags must not override its exact domain.
				for (FLayoutSolveBoundaryPoint& BoundaryPoint : OwnerRequest->IncomingBoundaryPoints)
				{
					if (BoundaryPoint.LocalCell
							+ FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection)
						!= LocalCell)
					{
						continue;
					}
					BoundaryPoint.bUsesCertifiedReciprocalDomain = true;
					BoundaryPoint.CertifiedDomainCertificateId =
						OwnerRequest->CandidateDomainCertificateId;
					BoundaryPoint.CertifiedDomainRestrictionId = Restriction->RestrictionId;
					if (Restriction->AllowedCandidates.Num() == 1)
					{
						const FLayoutCandidateVariantIdentity& Identity = Restriction->AllowedCandidates[0];
						const FLayoutModuleSolveSnapshot* Module = OwnerRequest->ModuleCatalog.Modules.FindByPredicate(
							[&](const FLayoutModuleSolveSnapshot& Candidate)
							{
								return Candidate.SnapshotId == Identity.ModuleSnapshotId;
							});
						FLayoutFaceRule OwnerFaceRule;
						if (Module != nullptr
							&& TryGetSnapshotWorldFaceRule(
								*Module,
								FLayoutDirectionUtils::GetOpposite(BoundaryPoint.FaceDirection),
								Identity.YawRotationSteps,
								OwnerFaceRule))
						{
							BoundaryPoint.ConnectedTraversalChannels =
								OwnerFaceRule.ConnectedTraversalChannels;
						}
					}
				}
			}
			if (OwnerPlacement != nullptr)
			{
				OwnerPlacement->ChildRequest.CandidateDomainRestrictions =
					OwnerPlacement->ChildDomainRestrictions;
			}
		}

		InOutParentRequest.IncomingBoundaryPoints.RemoveAll(
			[&](const FLayoutSolveBoundaryPoint& BoundaryPoint)
			{
				if (!BoundaryPoint.CommitmentId.IsNone())
				{
					return false;
				}
				return PartitionSeams.ContainsByPredicate(
					[&](const FLayoutPartitionSeamRecord& Seam)
					{
						return (Seam.OwnerRegionDebugPath == ParentRegionDebugPath
								&& Seam.PassiveRegionDebugPath == BoundaryPoint.SourceRegionDebugPath)
							|| (Seam.PassiveRegionDebugPath == ParentRegionDebugPath
								&& Seam.OwnerRegionDebugPath == BoundaryPoint.SourceRegionDebugPath);
					});
			});


		const auto OrderVerticalAccessHosts =
			[&OutFailureReason](FLayoutRegionSolveRequest& Request)
			{
				// Restrictions constrain module/yaw, not cell ownership. Remove a host
				// only when its lower-cell certificate excludes every VA root variant.
				for (FLayoutVerticalAccessHostGroup& HostGroup : Request.VerticalAccessHostGroups)
				{
					HostGroup.Options.RemoveAll(
						[&Request](const FLayoutVerticalAccessHostOption& Option)
						{
							const FLayoutCellCandidateDomainRestriction* Restriction =
								Request.CandidateDomainRestrictions.FindByPredicate(
									[&Option](const FLayoutCellCandidateDomainRestriction& Candidate)
									{
										return Candidate.Cell == Option.LowerCell;
									});
							if (Restriction == nullptr) return false;
							return !Restriction->AllowedCandidates.ContainsByPredicate(
								[&Request](const FLayoutCandidateVariantIdentity& Allowed)
								{
									const FLayoutModuleSolveSnapshot* Module = Request.ModuleCatalog.Modules.FindByPredicate(
										[&Allowed](const FLayoutModuleSolveSnapshot& Candidate)
										{
											return Candidate.SnapshotId == Allowed.ModuleSnapshotId;
										});
									return Module != nullptr
										&& Module->Roles.Contains(ELayoutModuleRole::VerticalAccess)
										&& Module->SupportsRootIntent(ELayoutCellIntent::VerticalAccess)
										&& Module->AllowedYawRotationSteps.Contains(Allowed.YawRotationSteps);
								});
						});
					if (HostGroup.Options.IsEmpty() && !HostGroup.bAllowOmission)
					{
						OutFailureReason = FString::Printf(
							TEXT("VerticalAccess host group '%s' has no lower-cell module/yaw admitted by exact child and seam domains in region '%s'."),
							*HostGroup.GroupId.ToString(), *Request.RegionDebugPath);
						return false;
					}
				}
				if (!Request.VerticalAccessHostGroups.IsEmpty())
				{
					TSet<FIntVector> PlannedCells;
					for (const FLayoutPlannedCell& PlannedCell : Request.PlannedCells)
					{
						PlannedCells.Add(PlannedCell.Cell);
					}
					TArray<FLayoutVerticalAccessHostGroup> SelectedGroups;
					TArray<FLayoutVerticalAccessHostGroup> ReorderedGroups;
					if (!TrySelectAdmittedParentVerticalAccessHosts(
						Request.VerticalAccessHostGroups,
						PlannedCells,
						{},
						{},
						{},
						Request.Seed,
						SelectedGroups,
						&ReorderedGroups,
						&OutFailureReason))
					{
						return false;
					}
					Request.VerticalAccessHostGroups = MoveTemp(ReorderedGroups);
				}
				return true;
			};
		if (!OrderVerticalAccessHosts(InOutParentRequest))
		{
			return false;
		}
		for (FAutomaticChildPlacement& Placement : InOutPlacements)
		{
			if (!OrderVerticalAccessHosts(Placement.ChildRequest))
			{
				return false;
			}
		}
		return true;
	}

	bool ValidatePotentialOwnedSeamJunctionRequirements(
		const TArray<FLayoutOwnedSeamJunctionRequirement>& JunctionRequirements,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		for (const FLayoutOwnedSeamJunctionRequirement& Requirement : JunctionRequirements)
		{
			const FLayoutRegionSolveRequest* const* OwnerRequestPtr =
				RequestsByPath.Find(Requirement.OwnerRegionDebugPath);
			const FLayoutPartitionSeamRecord* ContinuingSeam = PartitionSeams.FindByPredicate(
				[&Requirement](const FLayoutPartitionSeamRecord& Seam)
				{
					return Seam.SeamId == Requirement.ContinuingSeamId;
				});
			const FLayoutPartitionSeamRecord* BranchSeam = PartitionSeams.FindByPredicate(
				[&Requirement](const FLayoutPartitionSeamRecord& Seam)
				{
					return Seam.SeamId == Requirement.BranchSeamId;
				});
			if (OwnerRequestPtr == nullptr || *OwnerRequestPtr == nullptr
				|| ContinuingSeam == nullptr
				|| (!Requirement.BranchSeamId.IsNone() && BranchSeam == nullptr))
			{
				OutFailureReason = FString::Printf(
					TEXT("Owned seam junction '%s' is missing owner request or committed seam authority before proof."),
					*Requirement.JunctionRequirementId.ToString());
				return false;
			}

			bool bHasPotentialProvider = false;
			for (const FLayoutModuleSolveSnapshot& Module : (*OwnerRequestPtr)->ModuleCatalog.Modules)
			{
				for (const int32 YawRotationSteps : Module.AllowedYawRotationSteps)
				{
					for (const FLayoutSeamProviderIntent& Intent : Module.SeamProviderIntents)
					{
						if (!Intent.bCanOwnSeam
							|| Intent.JunctionUsage == ELayoutSeamJunctionUsage::NonJunctionOnly
							|| Intent.InterfaceFamily != Requirement.InterfaceFamily)
						{
							continue;
						}
						for (const FLayoutDerivedSpanOffer& ContinuingOffer : Module.DerivedSpanOffers)
						{
							if (!ContinuingOffer.bSealsBoundary
								|| FLayoutDirectionUtils::RotateYaw(
									ContinuingOffer.FaceDirection,
									YawRotationSteps) != Requirement.ContinuingOwnerFaceDirection)
							{
								continue;
							}
							bHasPotentialProvider = Module.DerivedSpanOffers.ContainsByPredicate(
								[&](const FLayoutDerivedSpanOffer& BranchOffer)
								{
									return BranchOffer.bSealsBoundary
										&& BranchOffer.LocalCell == ContinuingOffer.LocalCell
										&& FLayoutDirectionUtils::RotateYaw(
											BranchOffer.FaceDirection,
											YawRotationSteps) == Requirement.BranchOwnerFaceDirection;
								});
							if (bHasPotentialProvider)
							{
								break;
							}
						}
						if (bHasPotentialProvider)
						{
							break;
						}
					}
					if (bHasPotentialProvider)
					{
						break;
					}
				}
				if (bHasPotentialProvider)
				{
					break;
				}
			}
			if (!bHasPotentialProvider)
			{
				OutFailureReason = FString::Printf(
					TEXT("Owned seam junction '%s' at %s has no owner module candidate supporting both %d and %d faces before regional proof."),
					*Requirement.JunctionRequirementId.ToString(),
					*Requirement.JunctionCell.ToString(),
					static_cast<int32>(Requirement.ContinuingOwnerFaceDirection),
					static_cast<int32>(Requirement.BranchOwnerFaceDirection));
				return false;
			}
		}
		return true;
	}

	bool IsSolverExecutionBudgetFailure(const FString& FailureReason)
	{
		return FailureReason.Contains(TEXT("Layout solve exceeded MaxSolverDurationSeconds"))
			|| FailureReason.Contains(TEXT("Layout solve exceeded MaxSolverCandidateAttempts"));
	}

	FString BuildScheduleFailureDiagnostics(const FLayoutRegionSolveScheduleResult& ScheduleResult)
	{
		FString CombinedDiagnostics = !ScheduleResult.FailureReason.IsEmpty()
			? ScheduleResult.FailureReason
			: ScheduleResult.MergedSolveResult.FailureReason;
		for (const FLayoutValidationMessage& Message : ScheduleResult.MergedSolveResult.Messages)
		{
			CombinedDiagnostics += TEXT("\n");
			CombinedDiagnostics += Message.Message;
		}

		return CombinedDiagnostics;
	}

	FIntVector TranslateCell(const FIntVector& Cell, const FIntVector& Offset)
	{
		return Cell + Offset;
	}

	FLayoutPlannedCell TranslatePlannedCell(const FLayoutPlannedCell& PlannedCell, const FIntVector& Offset)
	{
		FLayoutPlannedCell TranslatedCell = PlannedCell;
		TranslatedCell.Cell = TranslateCell(TranslatedCell.Cell, Offset);
		return TranslatedCell;
	}

	FLayoutPlacedModule TranslatePlacedModule(const FLayoutPlacedModule& Placement, const FIntVector& Offset)
	{
		FLayoutPlacedModule TranslatedPlacement = Placement;
		TranslatedPlacement.Cell = TranslateCell(TranslatedPlacement.Cell, Offset);
		return TranslatedPlacement;
	}

	FLayoutCellReservationRecord TranslateReservationRecord(const FLayoutCellReservationRecord& Reservation, const FIntVector& Offset)
	{
		FLayoutCellReservationRecord TranslatedReservation = Reservation;
		TranslatedReservation.Cell = TranslateCell(TranslatedReservation.Cell, Offset);
		return TranslatedReservation;
	}

	FLayoutRouteConstraintRecord TranslateRouteConstraintRecord(const FLayoutRouteConstraintRecord& Constraint, const FIntVector& Offset)
	{
		FLayoutRouteConstraintRecord TranslatedConstraint = Constraint;
		TranslatedConstraint.Cell = TranslateCell(TranslatedConstraint.Cell, Offset);
		return TranslatedConstraint;
	}

	FLayoutClosureCoverageSegmentRecord TranslateClosureCoverageSegmentRecord(const FLayoutClosureCoverageSegmentRecord& Segment, const FIntVector& Offset)
	{
		FLayoutClosureCoverageSegmentRecord TranslatedSegment = Segment;
		TranslatedSegment.Cell = TranslateCell(TranslatedSegment.Cell, Offset);
		return TranslatedSegment;
	}

	FLayoutClosureRunRecord TranslateClosureRunRecord(const FLayoutClosureRunRecord& Run, const FIntVector& Offset)
	{
		FLayoutClosureRunRecord TranslatedRun = Run;
		TranslatedRun.StartCell = TranslateCell(TranslatedRun.StartCell, Offset);
		TranslatedRun.EndCell = TranslateCell(TranslatedRun.EndCell, Offset);
		return TranslatedRun;
	}

	FLayoutResidualCellRecord TranslateResidualCellRecord(const FLayoutResidualCellRecord& ResidualCell, const FIntVector& Offset)
	{
		FLayoutResidualCellRecord TranslatedResidualCell = ResidualCell;
		TranslatedResidualCell.Cell = TranslateCell(TranslatedResidualCell.Cell, Offset);
		return TranslatedResidualCell;
	}

	FLayoutSparsePlacementCommitment TranslateSparsePlacementCommitment(const FLayoutSparsePlacementCommitment& Commitment, const FIntVector& Offset)
	{
		FLayoutSparsePlacementCommitment TranslatedCommitment = Commitment;
		TranslatedCommitment.Cell = TranslateCell(TranslatedCommitment.Cell, Offset);
		for (FIntVector& OccupiedCell : TranslatedCommitment.OccupiedCells)
		{
			OccupiedCell = TranslateCell(OccupiedCell, Offset);
		}
		return TranslatedCommitment;
	}

	FLayoutSolveBoundaryPoint TranslateBoundaryPoint(const FLayoutSolveBoundaryPoint& BoundaryPoint, const FIntVector& Offset)
	{
		FLayoutSolveBoundaryPoint TranslatedBoundaryPoint = BoundaryPoint;
		TranslatedBoundaryPoint.LocalCell = TranslateCell(TranslatedBoundaryPoint.LocalCell, Offset);
		TranslatedBoundaryPoint.SourceCell = TranslateCell(TranslatedBoundaryPoint.SourceCell, Offset);
		return TranslatedBoundaryPoint;
	}

	void AppendRecursiveParentIncomingBoundaryPoints(
		const TArray<FLayoutSolveBoundaryPoint>& SourceBoundaryPoints,
		const TArray<FIntVector>& ParentCommittedEntryCells,
		TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints)
	{
		for (const FLayoutSolveBoundaryPoint& BoundaryPoint : SourceBoundaryPoints)
		{
			FLayoutSolveBoundaryPoint& SanitizedBoundaryPoint = OutBoundaryPoints.AddDefaulted_GetRef();
			SanitizedBoundaryPoint = BoundaryPoint;
			const FIntVector ParentInteriorCell =
				BoundaryPoint.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
			const bool bIsDirectParentContact = ParentCommittedEntryCells.Contains(ParentInteriorCell);
			// Recursive scheduling rule: parent solves consume translated child contacts as
			// face-compatibility context by default. Only direct parent-child contacts should stay
			// committed route anchors for the parent solve. Sibling-only seams and other recursive
			// translated contacts clear CommitmentId so the parent route graph is not overanchored.
			if (!bIsDirectParentContact)
			{
				SanitizedBoundaryPoint.CommitmentId = NAME_None;
			}
		}
	}

	void PopulateSyntheticBoundaryPointFromParentIntent(
		FLayoutSolveBoundaryPoint& BoundaryPoint);
	void PopulateSyntheticBoundaryPointFromChildIntent(
		ELayoutCellIntent ChildIntent,
		FLayoutSolveBoundaryPoint& BoundaryPoint);
	bool IsPreservedTerrainSparseCandidateCell(
		const FLayoutProfileSolveSnapshot& ParentProfileSnapshot,
		const FLayoutPlannedCell& ParentCell,
		int32 ParentTopModuleLevel);

	/**
	 * Adds support-only filled-neighbor boundary points for child cells that
	 * remain adjacent to surviving parent planned cells after reservations.
	 */
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
		const FLayoutId ChildBoundaryCertificateId,
		const TArray<FLayoutCellCandidateDomainRestriction>& CertifiedParentRestrictions,
		TArray<FLayoutSolveBoundaryPoint>& InOutBoundaryPoints)
	{
		TMap<FIntVector, const FLayoutPlannedCell*> ParentPlannedCellByCell;
		TMap<FIntPoint, int32> ParentTopModuleLevelByXY;
		for (const FLayoutPlannedCell& ParentPlannedCell : ParentPlannedCells)
		{
			ParentPlannedCellByCell.Add(ParentPlannedCell.Cell, &ParentPlannedCell);
			int32& TopModuleLevel = ParentTopModuleLevelByXY.FindOrAdd(
				FIntPoint(ParentPlannedCell.Cell.X, ParentPlannedCell.Cell.Y),
				MIN_int32);
			TopModuleLevel = FMath::Max(
				TopModuleLevel,
				ParentPlannedCell.ModuleLevelIndex == INDEX_NONE
					? ParentPlannedCell.Cell.Z
					: ParentPlannedCell.ModuleLevelIndex);
		}

		const auto BuildBoundaryEdgeKey = [](const FIntVector& Cell,
			const ELayoutFaceDirection Direction)
		{
			FIntVector FirstCell = Cell;
			FIntVector SecondCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const bool bSecondComesFirst = SecondCell.Z != FirstCell.Z
				? SecondCell.Z < FirstCell.Z
				: SecondCell.Y != FirstCell.Y
					? SecondCell.Y < FirstCell.Y
					: SecondCell.X < FirstCell.X;
			if (bSecondComesFirst)
			{
				Swap(FirstCell, SecondCell);
			}
			return HashCombineFast(GetTypeHash(FirstCell), GetTypeHash(SecondCell));
		};
		TSet<uint32> ExistingParentBoundaryPointKeys;
		for (FLayoutSolveBoundaryPoint& BoundaryPoint : InOutBoundaryPoints)
		{
			ExistingParentBoundaryPointKeys.Add(BuildBoundaryEdgeKey(
				BoundaryPoint.LocalCell,
				BoundaryPoint.FaceDirection));
		}

		static const ELayoutFaceDirection LateralSupportDirections[] =
		{
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY
		};
		static const ELayoutFaceDirection VerticalSupportDirections[] =
		{
			ELayoutFaceDirection::PosZ,
			ELayoutFaceDirection::NegZ
		};
		TMap<FIntVector, ELayoutCellIntent> ChildIntentByTranslatedCell;
		TSet<FIntVector> RetainedSharedShellCells;
		for (const FSharedParentChildFace& SharedFace : SharedParentChildFaces)
		{
			RetainedSharedShellCells.Add(SharedFace.ParentCell);
		}
		TArray<FIntVector> TranslatedPlannedCells;
		TranslatedPlannedCells.Reserve(ChildPlannedCells.Num());
		for (const FLayoutPlannedCell& ChildPlannedCell : ChildPlannedCells)
		{
			const FIntVector TranslatedChildCell =
				TranslateCell(ChildPlannedCell.Cell, ChildRegionOffset);
			TranslatedPlannedCells.Add(TranslatedChildCell);
			ChildIntentByTranslatedCell.Add(TranslatedChildCell, ChildPlannedCell.Intent);
		}

		const auto TryAppendBoundaryPointForCell =
			[&](
				const FIntVector& TranslatedChildCell,
				const ELayoutFaceDirection Direction)
		{
			// Retained shared-shell cells stay inside parent CSP; their parent module supplies
			// adjacency directly and must not masquerade as an external child boundary.
			if (RetainedSharedShellCells.Contains(TranslatedChildCell))
			{
				return;
			}
			const FIntVector ParentNeighborCell =
				TranslatedChildCell + FLayoutDirectionUtils::ToCellDelta(Direction);
			if (!ParentPlannedCellSet.Contains(ParentNeighborCell)
				|| ReservedParentCells.Contains(ParentNeighborCell))
			{
				return;
			}

			const FLayoutPlannedCell* const* ParentPlannedCell =
				ParentPlannedCellByCell.Find(ParentNeighborCell);
			const bool bCertificateRequiresFilledParent =
				CertifiedParentRestrictions.ContainsByPredicate(
					[&ParentNeighborCell](const FLayoutCellCandidateDomainRestriction& Restriction)
					{
						return Restriction.Cell == ParentNeighborCell;
					});
			if (!ChildBoundaryCertificateId.IsNone()
				&& !bCertificateRequiresFilledParent
				&& ParentPlannedCell != nullptr
				&& *ParentPlannedCell != nullptr
				&& IsPreservedTerrainSparseCandidateCell(
					ParentProfileSnapshot,
					**ParentPlannedCell,
					ParentTopModuleLevelByXY.FindRef(FIntPoint(
						ParentNeighborCell.X,
						ParentNeighborCell.Y))))
			{
				// Exact child boundary certification already proved this face accepts
				// an empty parent neighbor. Do not overwrite that
				// witness with generic filled-child support during parent rebuilding.
				return;
			}

			const uint32 BoundaryPointKey = BuildBoundaryEdgeKey(
				TranslatedChildCell,
				Direction);
			if (ExistingParentBoundaryPointKeys.Contains(BoundaryPointKey))
			{
				return;
			}

			FLayoutSolveBoundaryPoint& SupportBoundaryPoint =
				InOutBoundaryPoints.AddDefaulted_GetRef();
			SupportBoundaryPoint.LocalCell = TranslatedChildCell;
			SupportBoundaryPoint.FaceDirection = Direction;
			SupportBoundaryPoint.bRepresentsFilledNeighbor = true;
			SupportBoundaryPoint.bRequiresBoundaryFacing = false;
			SupportBoundaryPoint.SourceRegionDebugPath = ChildRegionDebugPath;
			SupportBoundaryPoint.SourceCell = TranslatedChildCell;
			const ELayoutCellIntent* ChildIntent =
				ChildIntentByTranslatedCell.Find(TranslatedChildCell);
			PopulateSyntheticBoundaryPointFromParentIntent(SupportBoundaryPoint);
			if (ChildIntent != nullptr
				&& *ChildIntent == ELayoutCellIntent::VerticalAccess
				&& (Direction == ELayoutFaceDirection::PosZ
					|| Direction == ELayoutFaceDirection::NegZ))
			{
				SupportBoundaryPoint.ConnectedTraversalChannels.AddTag(
					LayoutGameplayTags::TraversalPrimary);
			}
			ExistingParentBoundaryPointKeys.Add(BoundaryPointKey);
		};

		for (const FIntVector& TranslatedChildCell : TranslatedPlannedCells)
		{
			for (const ELayoutFaceDirection Direction : LateralSupportDirections)
			{
				TryAppendBoundaryPointForCell(TranslatedChildCell, Direction);
			}
		}
		for (const FIntVector& ChildSupportingCell : ChildSupportingCells)
		{
			const FIntVector TranslatedChildCell =
				TranslateCell(ChildSupportingCell, ChildRegionOffset);
			for (const ELayoutFaceDirection Direction : VerticalSupportDirections)
			{
				TryAppendBoundaryPointForCell(TranslatedChildCell, Direction);
			}
		}
	}

	void AppendTranslatedChildBoundaryPoints(
		const FIntVector& RegionCellOffset,
		const FString& ChildRegionDebugPath,
		const TArray<FLayoutCommittedEndpointAnchor>& EndpointCommitments,
		TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints)
	{
		for (const FLayoutCommittedEndpointAnchor& Anchor : EndpointCommitments)
		{
			FLayoutSolveBoundaryPoint& BoundaryPoint = OutBoundaryPoints.AddDefaulted_GetRef();
			BoundaryPoint.LocalCell = TranslateCell(
				Anchor.LocalCell + FLayoutDirectionUtils::ToCellDelta(Anchor.FaceDirection),
				RegionCellOffset);
			BoundaryPoint.FaceDirection = FLayoutDirectionUtils::GetOpposite(Anchor.FaceDirection);
			BoundaryPoint.ConnectionTag = Anchor.ConnectionTag;
			BoundaryPoint.AllowedConnectionTags = Anchor.AllowedConnectionTags;
			BoundaryPoint.ConnectedTraversalChannels = Anchor.TraversalChannels;
			BoundaryPoint.bRequireMatchingYawWithFilledNeighbor = Anchor.bRequireMatchingYawWithFilledNeighbor;
			BoundaryPoint.bRepresentsFilledNeighbor = true;
			BoundaryPoint.SourceRegionDebugPath = ChildRegionDebugPath;
			BoundaryPoint.SourceCell = BoundaryPoint.LocalCell;
		}
	}

	void PopulateSyntheticBoundaryPointFromParentIntent(
		FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
		// Support-only translated child occupancy should preserve "filled neighbor" semantics
		// without inventing new same-level wall/closure contracts. Explicit child interface
		// carriers are responsible for stronger lateral tags when the child actually proves them.
		BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
	}

	void PopulateSyntheticBoundaryPointFromChildIntent(
		const ELayoutCellIntent ChildIntent,
		FLayoutSolveBoundaryPoint& BoundaryPoint)
	{
		BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);

		if (BoundaryPoint.FaceDirection == ELayoutFaceDirection::PosZ
			|| BoundaryPoint.FaceDirection == ELayoutFaceDirection::NegZ)
		{
			// Child boundary intent describes lateral shell ownership. When the scheduler synthesizes
			// stacked vertical interfaces, keep them open so upper-level replacement and continuation
			// can negotiate against explicit traversal/anchor contracts instead of a fake solid ceiling.
			BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
			return;
		}

		switch (ChildIntent)
		{
		case ELayoutCellIntent::Boundary:
			BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceSolid;
			break;
		case ELayoutCellIntent::Entry:
		case ELayoutCellIntent::Core:
		case ELayoutCellIntent::Interior:
		case ELayoutCellIntent::Connector:
		case ELayoutCellIntent::VerticalAccess:
			BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
			break;
		default:
			BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
			break;
		}
	}

	void AddUniqueRejectedCandidateReason(
		const FString& RejectedMessage,
		TArray<FString>& InOutRejectedCandidateReasons)
	{
		if (RejectedMessage.IsEmpty()
			|| InOutRejectedCandidateReasons.Num() >= 12
			|| InOutRejectedCandidateReasons.Contains(RejectedMessage))
		{
			return;
		}

		InOutRejectedCandidateReasons.Add(RejectedMessage);
	}

	void UpdateBestRejectedCandidateReason(
		const int32 CandidateScore,
		const FString& RejectedMessage,
		int32& InOutBestRejectedCandidateScore,
		FString& InOutBestRejectedCandidateReason)
	{
		if (RejectedMessage.IsEmpty() || CandidateScore <= InOutBestRejectedCandidateScore)
		{
			return;
		}

		InOutBestRejectedCandidateScore = CandidateScore;
		InOutBestRejectedCandidateReason = RejectedMessage;
	}

	uint32 BuildChildCandidateTieBreakHash(
		const int32 RootSeed,
		const FName EntryId,
		const FIntVector& CandidateOffset,
		const int32 InstanceOrdinal,
		const int32 VariantIndex)
	{
		return HashCombineFast(
			HashCombineFast(static_cast<uint32>(RootSeed), GetTypeHash(EntryId)),
			HashCombineFast(
				HashCombineFast(GetTypeHash(CandidateOffset), static_cast<uint32>(InstanceOrdinal)),
				static_cast<uint32>(VariantIndex)));
	}

	bool BuildPlacementBackedParentProofRequest(
		const FLayoutRegionSolveRequest& RootRequest,
		const FLayoutRegionSolveResult& RootPreparedTopology,
		const TArray<FAutomaticChildPlacement>& CandidatePlacements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FLayoutRegionSolveRequest& OutParentRequest,
		FString& OutFailureReason);

	void RebuildMergedPostStructuralArtifacts(
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		const TArray<FLayoutDirectChildRegionCommitment>& DirectChildCommitments);

	const FLayoutChildCapabilityEndpoint* FindSummaryEntryCapabilityForFace(
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const ELayoutFaceDirection FaceDirection)
	{
		return InterfaceSummary.EntryCapabilities.FindByPredicate(
			[FaceDirection](const FLayoutChildCapabilityEndpoint& Capability)
			{
				return Capability.FaceDirection == FaceDirection;
			});
	}

	TArray<FLayoutSolveBoundaryPoint> BuildSyntheticExportedBoundaryPointsFromChildSummary(
		const FString& ChildRegionDebugPath,
		const FIntVector& CandidateOffset,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const FChildPlanningVariantInterfaceSummary& InterfaceSummary,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FSharedParentChildFace>& CandidateSharedParentChildFaces,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const bool bChildContributesHostVerticalAccess,
		const int32 ChildMaxLocalLevel)
	{
		TArray<FLayoutSolveBoundaryPoint> BoundaryPoints;
		static const ELayoutFaceDirection LateralDirections[] =
		{
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY
		};
		for (const FChildPlanningVariantInterfaceSummary::FBoundaryFace& BoundaryFace : InterfaceSummary.BoundaryFaces)
		{
			const bool bSharedShellFace = CandidateSharedParentChildFaces.ContainsByPredicate(
				[&](const FSharedParentChildFace& SharedFace)
				{
					return SharedFace.ChildLocalCell == BoundaryFace.LocalCell
						&& SharedFace.FaceDirection == BoundaryFace.FaceDirection;
				});
			if (bSharedShellFace)
			{
				const bool bHasDirectCommitment = CandidateCommitments.ContainsByPredicate(
					[&](const FLayoutCommittedEndpointAnchor& Commitment)
					{
						return Commitment.LocalCell == BoundaryFace.LocalCell
							&& Commitment.FaceDirection == BoundaryFace.FaceDirection;
					});
				if (!bHasDirectCommitment)
				{
					continue;
				}
			}

			const FLayoutCommittedEndpointAnchor* MatchingCommitment = CandidateCommitments.FindByPredicate(
				[&](const FLayoutCommittedEndpointAnchor& Commitment)
				{
					return Commitment.LocalCell == BoundaryFace.LocalCell
						&& Commitment.FaceDirection == BoundaryFace.FaceDirection;
				});
			const bool bIsVerticalBoundaryFace =
				BoundaryFace.FaceDirection == ELayoutFaceDirection::PosZ
				|| BoundaryFace.FaceDirection == ELayoutFaceDirection::NegZ;
			const bool bHasExplicitBoundaryContract =
				MatchingCommitment != nullptr
				|| !BoundaryFace.TraversalChannels.IsEmpty();
			if (!bHasExplicitBoundaryContract && !bIsVerticalBoundaryFace)
			{
				// Generic same-level child shell ownership is already preserved by the reserved-cell
				// parent-plan rebuild. Only export lateral boundary points when the child summary
				// proves an explicit parent-facing contract that the parent proof must honor.
				continue;
			}
			if (MatchingCommitment == nullptr
				&& bIsVerticalBoundaryFace
				&& BoundaryFace.TraversalChannels.IsEmpty())
			{
				// Keep stacked vertical support on the dedicated support-boundary path.
				// When a child face proves no traversal at all, the support-only carrier is
				// still enough. Traversable upper-level support must stay on the explicit
				// per-face boundary carrier so parent proof keeps the child-side contract.
				continue;
			}

			const FIntVector ParentNeighborCell = TranslateCell(
				BoundaryFace.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryFace.FaceDirection),
				CandidateOffset);
			if (!ParentPlannedCellSet.Contains(ParentNeighborCell))
			{
				continue;
			}

			const FIntVector TranslatedChildCell = TranslateCell(BoundaryFace.LocalCell, CandidateOffset);
			FLayoutSolveBoundaryPoint& BoundaryPoint = BoundaryPoints.AddDefaulted_GetRef();
			BoundaryPoint.LocalCell = TranslatedChildCell;
			BoundaryPoint.FaceDirection = BoundaryFace.FaceDirection;
			BoundaryPoint.SourceRegionDebugPath = ChildRegionDebugPath;
			BoundaryPoint.SourceCell = TranslatedChildCell;
			if (MatchingCommitment != nullptr
				&& (BoundaryFace.ConnectionTag.IsValid()
				|| !BoundaryFace.AllowedConnectionTags.IsEmpty())
			)
			{
				BoundaryPoint.ConnectionTag = BoundaryFace.ConnectionTag;
				BoundaryPoint.AllowedConnectionTags =
					BoundaryFace.AllowedConnectionTags;
			}
			else
			{
				PopulateSyntheticBoundaryPointFromChildIntent(
					BoundaryFace.Intent,
					BoundaryPoint);
			}
			BoundaryPoint.ConnectedTraversalChannels =
				BoundaryFace.TraversalChannels;

			if (MatchingCommitment != nullptr)
			{
				if (!MatchingCommitment->TraversalChannels.IsEmpty())
				{
					BoundaryPoint.ConnectedTraversalChannels =
						MatchingCommitment->TraversalChannels;
				}
				BoundaryPoint.bRequireMatchingYawWithFilledNeighbor =
					MatchingCommitment->bRequireMatchingYawWithFilledNeighbor;
				BoundaryPoint.CommitmentId = MatchingCommitment->CommitmentId;
			}

			if (BoundaryFace.Intent == ELayoutCellIntent::Entry)
			{
				if (const FLayoutChildCapabilityEndpoint* MatchingCapability =
					FindSummaryEntryCapabilityForFace(
						InterfaceSummary,
						BoundaryFace.FaceDirection))
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

						const FIntVector TranslatedChildCell = TranslateCell(ExtraReservedLocalCell, CandidateOffset);
						for (const ELayoutFaceDirection Direction : LateralDirections)
						{
							const FIntVector NeighborLocalCell = ExtraReservedLocalCell + FLayoutDirectionUtils::ToCellDelta(Direction);
							if (ExtraReservedLocalCells.Contains(NeighborLocalCell))
							{
								continue;
							}

							const FIntVector ParentNeighborCell = TranslateCell(NeighborLocalCell, CandidateOffset);
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
							PopulateSyntheticBoundaryPointFromChildIntent(ELayoutCellIntent::Interior, BoundaryPoint);
							BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
						}
					}
				}
			}
		}

		return BoundaryPoints;
	}

	bool DoesChildPlacementRespectProtectedParentEntryNeighbors(
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TSet<FIntVector>& ChildPlannedCellSet,
		const FIntVector& CandidateOffset,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		FString* OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		static const ELayoutFaceDirection HorizontalDirections[] =
		{
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY
		};

		for (const FLayoutPlannedCell& ChildPlannedCell : ChildPlannedCells)
		{
			for (const ELayoutFaceDirection Direction : HorizontalDirections)
			{
				const FIntVector ChildNeighborCell = ChildPlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (ChildPlannedCellSet.Contains(ChildNeighborCell))
				{
					continue;
				}

				const FIntVector ParentNeighborCell = TranslateCell(ChildNeighborCell, CandidateOffset);
				const ELayoutCellIntent* ParentIntent = ParentPlannedCellIntents.Find(ParentNeighborCell);
				if (ParentIntent == nullptr || *ParentIntent != ELayoutCellIntent::Entry)
				{
					continue;
				}

				const bool bHasMatchingCommittedEntryFace = CandidateCommitments.ContainsByPredicate(
					[&ChildPlannedCell, Direction](const FLayoutCommittedEndpointAnchor& Commitment)
					{
						return Commitment.LocalCell == ChildPlannedCell.Cell
							&& Commitment.FaceDirection == Direction;
					});
				if (bHasMatchingCommittedEntryFace)
				{
					continue;
				}

				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Child boundary at local cell %s would face protected parent entry cell %s on face %s without a committed child entry anchor.\nProblem: The parent root entry would terminate into child boundary content instead of a traversable child contact.\nFix: Move or rotate the child so it does not sit directly behind the parent entry, or expose a matching child entry face on that side."),
						*ChildPlannedCell.Cell.ToString(),
						*ParentNeighborCell.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Direction)));
				}
				return false;
			}
		}

		return true;
	}

	FIntVector MapSourceChildCellToParent(
		const FIntVector& SourceChildCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping)
	{
		if (StageMapping != nullptr)
		{
			if (const FLayoutChildStageMappedCell* MappedCell = StageMapping->Cells.FindByPredicate(
				[&SourceChildCell](const FLayoutChildStageMappedCell& Candidate)
				{
					return Candidate.SourceChildCell == SourceChildCell;
				}))
			{
				return MappedCell->ParentCell;
			}
		}
		return TranslateCell(SourceChildCell, Offset);
	}

	FIntVector MapParentCellToChildResultCell(
		const FIntVector& ParentCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping)
	{
		if (StageMapping != nullptr)
		{
			if (const FLayoutChildStageMappedCell* MappedCell = StageMapping->Cells.FindByPredicate(
				[&ParentCell](const FLayoutChildStageMappedCell& Candidate)
				{
					return Candidate.ParentCell == ParentCell;
				}))
			{
				return MappedCell->MappedChildCell;
			}
		}
		return ParentCell - Offset;
	}

	FIntVector MapChildResultCellToParent(
		const FIntVector& ChildCell,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping)
	{
		if (StageMapping != nullptr)
		{
			if (const FLayoutChildStageMappedCell* MappedCell = StageMapping->Cells.FindByPredicate(
				[&ChildCell](const FLayoutChildStageMappedCell& Candidate)
				{
					return Candidate.MappedChildCell == ChildCell;
				}))
			{
				return MappedCell->ParentCell;
			}
		}
		return TranslateCell(ChildCell, Offset);
	}

	void AppendTranslatedSolveResultArtifacts(
		FLayoutSolveResult& TargetResult,
		const FLayoutSolveResult& SourceResult,
		const FIntVector& Offset,
		const FLayoutChildStageMappingResult* StageMapping = nullptr)
	{
		for (const FLayoutPlannedCell& PlannedCell : SourceResult.PlannedCells)
		{
			FLayoutPlannedCell MappedCell = PlannedCell;
			MappedCell.Cell = MapChildResultCellToParent(PlannedCell.Cell, Offset, StageMapping);
			TargetResult.PlannedCells.Add(MoveTemp(MappedCell));
		}

		for (const FLayoutPlacedModule& Placement : SourceResult.Placements)
		{
			FLayoutPlacedModule MappedPlacement = Placement;
			MappedPlacement.Cell = MapChildResultCellToParent(Placement.Cell, Offset, StageMapping);
			TargetResult.Placements.Add(MoveTemp(MappedPlacement));
		}

		for (const FIntVector& EntryCell : SourceResult.ExportedEntryCells)
		{
			TargetResult.ExportedEntryCells.Add(MapChildResultCellToParent(EntryCell, Offset, StageMapping));
		}

		for (const FLayoutCellReservationRecord& Reservation : SourceResult.CompiledReservations)
		{
			FLayoutCellReservationRecord MappedReservation = Reservation;
			MappedReservation.Cell = MapChildResultCellToParent(Reservation.Cell, Offset, StageMapping);
			TargetResult.CompiledReservations.Add(MoveTemp(MappedReservation));
		}

		for (const FLayoutRouteConstraintRecord& Constraint : SourceResult.RouteConstraints)
		{
			FLayoutRouteConstraintRecord MappedConstraint = Constraint;
			MappedConstraint.Cell = MapChildResultCellToParent(Constraint.Cell, Offset, StageMapping);
			TargetResult.RouteConstraints.Add(MoveTemp(MappedConstraint));
		}

		for (const FLayoutClosureCoverageSegmentRecord& Segment : SourceResult.ClosureSegments)
		{
			FLayoutClosureCoverageSegmentRecord MappedSegment = Segment;
			MappedSegment.Cell = MapChildResultCellToParent(Segment.Cell, Offset, StageMapping);
			TargetResult.ClosureSegments.Add(MoveTemp(MappedSegment));
		}
		for (const FLayoutClosureCoverageSegmentRecord& Segment : SourceResult.FutureTerraceProofClosureSegments)
		{
			FLayoutClosureCoverageSegmentRecord MappedSegment = Segment;
			MappedSegment.Cell = MapChildResultCellToParent(Segment.Cell, Offset, StageMapping);
			TargetResult.FutureTerraceProofClosureSegments.Add(MoveTemp(MappedSegment));
		}

		TargetResult.ClosureCoverage.Append(SourceResult.ClosureCoverage);
		TargetResult.TerrainStageDiagnostics.Append(SourceResult.TerrainStageDiagnostics);
		TargetResult.TerrainFrontierOwnershipDiagnostics.Append(SourceResult.TerrainFrontierOwnershipDiagnostics);

		for (const FLayoutClosureRunRecord& Run : SourceResult.ClosureRuns)
		{
			FLayoutClosureRunRecord MappedRun = Run;
			MappedRun.StartCell = MapChildResultCellToParent(Run.StartCell, Offset, StageMapping);
			MappedRun.EndCell = MapChildResultCellToParent(Run.EndCell, Offset, StageMapping);
			TargetResult.ClosureRuns.Add(MoveTemp(MappedRun));
		}

		for (const FLayoutResidualCellRecord& ResidualCell : SourceResult.ResidualUnoccupiedCells)
		{
			FLayoutResidualCellRecord MappedResidualCell = ResidualCell;
			MappedResidualCell.Cell = MapChildResultCellToParent(ResidualCell.Cell, Offset, StageMapping);
			TargetResult.ResidualUnoccupiedCells.Add(MoveTemp(MappedResidualCell));
		}

		for (const FLayoutSparsePlacementCommitment& Commitment : SourceResult.SparsePlacementCommitments)
		{
			FLayoutSparsePlacementCommitment MappedCommitment = Commitment;
			MappedCommitment.Cell = MapChildResultCellToParent(Commitment.Cell, Offset, StageMapping);
			TargetResult.SparsePlacementCommitments.Add(MoveTemp(MappedCommitment));
		}
	}

	bool IsBoundaryCellInFootprint(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		return Cell.X == 0
			|| Cell.Y == 0
			|| Cell.X == FootprintSize.X - 1
			|| Cell.Y == FootprintSize.Y - 1;
	}

	bool IsCornerBoundaryCellInFootprint(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		return (Cell.X == 0 || Cell.X == FootprintSize.X - 1)
			&& (Cell.Y == 0 || Cell.Y == FootprintSize.Y - 1);
	}

	bool DoesCellMatchPlacementZoneInFootprint(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const ELayoutPlacementZone PlacementZone)
	{
		switch (PlacementZone)
		{
		case ELayoutPlacementZone::Any:
			return true;
		case ELayoutPlacementZone::Perimeter:
			return IsBoundaryCellInFootprint(Cell, FootprintSize);
		case ELayoutPlacementZone::Edge:
			return IsBoundaryCellInFootprint(Cell, FootprintSize) && !IsCornerBoundaryCellInFootprint(Cell, FootprintSize);
		case ELayoutPlacementZone::Corner:
			return IsCornerBoundaryCellInFootprint(Cell, FootprintSize);
		case ELayoutPlacementZone::Interior:
			return !IsBoundaryCellInFootprint(Cell, FootprintSize);
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

	bool DoesTranslatedChildPlanMatchPlacementZone(
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const FIntVector& Offset,
		const FIntPoint& ParentFootprintSize,
		const ELayoutPlacementZone PlacementZone,
		const TSet<FIntVector>* ExemptTranslatedCells = nullptr,
		FString* OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}

		if (PlacementZone == ELayoutPlacementZone::Any)
		{
			return true;
		}

		if (PlacementZone == ELayoutPlacementZone::Interior)
		{
			for (const FLayoutPlannedCell& PlannedCell : ChildPlannedCells)
			{
				const FIntVector TranslatedCell = TranslateCell(PlannedCell.Cell, Offset);
				if (ExemptTranslatedCells != nullptr && ExemptTranslatedCells->Contains(TranslatedCell))
				{
					continue;
				}

				if (DoesCellMatchPlacementZoneInFootprint(TranslatedCell, ParentFootprintSize, PlacementZone))
				{
					continue;
				}

				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Placement Zone\nRejection Detail:\n  Child cell %s translated to parent cell %s, which does not satisfy placement zone %s.\n  Problem: Interior child placements may only overlap the parent perimeter where a parent-owned shared seam was proven.\n  Fix: Move or rotate the child so its non-shared cells stay off the parent perimeter, or add compatible parent/child seam support on the overlapping boundary faces."),
						*Offset.ToString(),
						*PlannedCell.Cell.ToString(),
						*TranslatedCell.ToString(),
						*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(PlacementZone)));
				}
				return false;
			}

			return true;
		}

		if (PlacementZone == ELayoutPlacementZone::Core)
		{
			if (ChildPlannedCells.IsEmpty())
			{
				return false;
			}

			FIntVector MinCell = ChildPlannedCells[0].Cell;
			FIntVector MaxCell = ChildPlannedCells[0].Cell;
			for (const FLayoutPlannedCell& PlannedCell : ChildPlannedCells)
			{
				MinCell.X = FMath::Min(MinCell.X, PlannedCell.Cell.X);
				MinCell.Y = FMath::Min(MinCell.Y, PlannedCell.Cell.Y);
				MaxCell.X = FMath::Max(MaxCell.X, PlannedCell.Cell.X);
				MaxCell.Y = FMath::Max(MaxCell.Y, PlannedCell.Cell.Y);
			}

			const FIntVector ChildFootprintCenter(
				(MinCell.X + MaxCell.X) / 2,
				(MinCell.Y + MaxCell.Y) / 2,
				0);
			const bool bMatchesCore = DoesCellMatchPlacementZoneInFootprint(
				TranslateCell(ChildFootprintCenter, Offset),
				ParentFootprintSize,
				PlacementZone);
			if (!bMatchesCore && OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Placement Zone\nRejection Detail:\n  Child footprint center translated to parent cell %s, which does not satisfy placement zone %s.\n  Problem: Core placement requires the child footprint center to land in the parent core zone.\n  Fix: Move or rotate the child so its center lands deeper inside the parent footprint."),
					*Offset.ToString(),
					*TranslateCell(ChildFootprintCenter, Offset).ToString(),
					*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(PlacementZone)));
			}
			return bMatchesCore;
		}

		for (const FLayoutPlannedCell& PlannedCell : ChildPlannedCells)
		{
			if (DoesCellMatchPlacementZoneInFootprint(TranslateCell(PlannedCell.Cell, Offset), ParentFootprintSize, PlacementZone))
			{
				return true;
			}
		}

		if (OutFailureReason != nullptr && !ChildPlannedCells.IsEmpty())
		{
			*OutFailureReason = FString::Printf(
				TEXT("Best Rejected Placement:\nOffset: %s\nParent Contact Cells: <none>\nFailed During: Placement Zone\nRejection Detail:\n  No translated child cell satisfied placement zone %s.\n  Problem: The child footprint never reached a compatible parent zone cell for this placement.\n  Fix: Move or rotate the child, or broaden the content entry placement zone if that is intended."),
				*Offset.ToString(),
				*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(PlacementZone)));
		}
		return false;
	}

	bool DoesMappedChildPlanMatchLevelPlacementPolicy(
		const TArray<FLayoutPlannedCell>& MappedChildPlannedCells,
		const TMap<FIntPoint, int32>& ParentTopModuleLevelByXY,
		const int32 ParentModuleLevelOffset,
		const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		const int32 SpecificLevel,
		FString* OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		if (LevelPlacementPolicy == ELayoutLevelPlacementPolicy::AnyLevel)
		{
			return true;
		}

		int32 MinChildModuleLevel = TNumericLimits<int32>::Max();
		int32 MaxChildModuleLevel = TNumericLimits<int32>::Lowest();
		for (const FLayoutPlannedCell& PlannedCell : MappedChildPlannedCells)
		{
			MinChildModuleLevel = FMath::Min(MinChildModuleLevel, PlannedCell.ModuleLevelIndex);
			MaxChildModuleLevel = FMath::Max(MaxChildModuleLevel, PlannedCell.ModuleLevelIndex);
		}

		for (const FLayoutPlannedCell& PlannedCell : MappedChildPlannedCells)
		{
			const bool bUsesChildBaseSlice =
				LevelPlacementPolicy == ELayoutLevelPlacementPolicy::GroundOnly
				|| LevelPlacementPolicy == ELayoutLevelPlacementPolicy::SpecificLevel
				|| LevelPlacementPolicy == ELayoutLevelPlacementPolicy::AboveGroundLevel;
			const int32 RequiredChildModuleLevel = bUsesChildBaseSlice
				? MinChildModuleLevel
				: MaxChildModuleLevel;
			if (PlannedCell.ModuleLevelIndex != RequiredChildModuleLevel)
			{
				continue;
			}

			const int32 ParentModuleLevel =
				PlannedCell.ModuleLevelIndex + ParentModuleLevelOffset;
			const int32* ParentTopModuleLevel = ParentTopModuleLevelByXY.Find(
				FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
			bool bMatches = false;
			switch (LevelPlacementPolicy)
			{
			case ELayoutLevelPlacementPolicy::GroundOnly:
				bMatches = ParentModuleLevel == 0;
				break;
			case ELayoutLevelPlacementPolicy::SpecificLevel:
				bMatches = ParentModuleLevel == SpecificLevel;
				break;
			case ELayoutLevelPlacementPolicy::TopLevelOnly:
				bMatches = ParentTopModuleLevel != nullptr
					&& ParentModuleLevel == *ParentTopModuleLevel;
				break;
			case ELayoutLevelPlacementPolicy::AboveGroundLevel:
				bMatches = ParentModuleLevel > 0;
				break;
			case ELayoutLevelPlacementPolicy::BelowTopLevel:
				bMatches = ParentTopModuleLevel != nullptr
					&& ParentModuleLevel < *ParentTopModuleLevel;
				break;
			case ELayoutLevelPlacementPolicy::AnyLevel:
			default:
				bMatches = true;
				break;
			}
			if (bMatches)
			{
				continue;
			}

			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = FString::Printf(
					TEXT("Best Rejected Placement:\nParent Module Level Offset: %d\nParent Contact Cells: <none>\nFailed During: Level Placement Policy\nRejection Detail:\n  Child anchor cell %s at authored level %d maps to parent authored level %d, which does not satisfy level placement policy %s.\n  Specific Level: %d\n  Problem: The mapped child anchor slice violates the authored parent-relative level placement policy.\n  Fix: Move the child to a compatible authored level, change the level placement policy, or adjust the parent level shape so this policy can be satisfied."),
					ParentModuleLevelOffset,
					*PlannedCell.Cell.ToString(),
					PlannedCell.ModuleLevelIndex,
					ParentModuleLevel,
					*StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(static_cast<int64>(LevelPlacementPolicy)),
					SpecificLevel);
			}
			return false;
		}

		return true;
	}

	FIntPoint RotateFootprintSizeYaw(const FIntPoint& FootprintSize, const int32 YawRotationSteps)
	{
		const int32 NormalizedYawSteps = ((YawRotationSteps % 4) + 4) % 4;
		return (NormalizedYawSteps % 2) == 0
			? FootprintSize
			: FIntPoint(FootprintSize.Y, FootprintSize.X);
	}

	FIntVector RotateCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		const int32 NormalizedYawSteps = ((YawRotationSteps % 4) + 4) % 4;
		switch (NormalizedYawSteps)
		{
		case 1:
			return FIntVector(FootprintSize.Y - 1 - Cell.Y, Cell.X, Cell.Z);
		case 2:
			return FIntVector(FootprintSize.X - 1 - Cell.X, FootprintSize.Y - 1 - Cell.Y, Cell.Z);
		case 3:
			return FIntVector(Cell.Y, FootprintSize.X - 1 - Cell.X, Cell.Z);
		case 0:
		default:
			return Cell;
		}
	}

	FLayoutChildCapabilityEnvelope RotateChildCapabilityEnvelopeYaw(
		const FLayoutChildCapabilityEnvelope& SourceEnvelope,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		FLayoutChildCapabilityEnvelope RotatedEnvelope = SourceEnvelope;
		for (FLayoutChildCapabilityEndpoint& Endpoint : RotatedEnvelope.EndpointCapabilities)
		{
			Endpoint.LocalCell = RotateCellInFootprintYaw(
				Endpoint.LocalCell,
				FootprintSize,
				YawRotationSteps);
			Endpoint.FaceDirection = FLayoutDirectionUtils::RotateYaw(Endpoint.FaceDirection, YawRotationSteps);
		}
		for (FLayoutChildCapabilitySpan& Span : RotatedEnvelope.SpanCapabilities)
		{
			Span.LocalCell = RotateCellInFootprintYaw(
				Span.LocalCell,
				FootprintSize,
				YawRotationSteps);
			Span.FaceDirection = FLayoutDirectionUtils::RotateYaw(Span.FaceDirection, YawRotationSteps);
		}
		for (FLayoutChildCapabilitySeam& Seam : RotatedEnvelope.SeamCapabilities)
		{
			Seam.LocalCell = RotateCellInFootprintYaw(
				Seam.LocalCell,
				FootprintSize,
				YawRotationSteps);
			Seam.FaceDirection = FLayoutDirectionUtils::RotateYaw(Seam.FaceDirection, YawRotationSteps);
		}
		return RotatedEnvelope;
	}

	bool DoesPotentialPlacementZoneOverlapRequirementZone(
		const ELayoutPlacementZone EntryPlacementZone,
		const ELayoutPlacementZone RequirementZone)
	{
		return LayoutZoneFeatureDemand::DoPotentialPlacementZonesOverlap(
			EntryPlacementZone,
			RequirementZone);
	}

	bool IsScheduledDirectChildPath(
		const FString& ParentRegionPath,
		const FString& CandidateChildPath)
	{
		if (ParentRegionPath.IsEmpty() || CandidateChildPath.IsEmpty())
		{
			return false;
		}

		const FString Prefix = ParentRegionPath + TEXT("/");
		if (!CandidateChildPath.StartsWith(Prefix))
		{
			return false;
		}

		const FString Remainder = CandidateChildPath.Mid(Prefix.Len());
		return !Remainder.IsEmpty() && !Remainder.Contains(TEXT("/"));
	}

	FString FormatZoneFeatureTags(const FGameplayTagContainer& Tags)
	{
		TArray<FGameplayTag> SortedTags;
		Tags.GetGameplayTagArray(SortedTags);
		SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
		{
			return Left.ToString() < Right.ToString();
		});

		if (SortedTags.IsEmpty())
		{
			return TEXT("<none>");
		}

		TArray<FString> TagStrings;
		TagStrings.Reserve(SortedTags.Num());
		for (const FGameplayTag& Tag : SortedTags)
		{
			TagStrings.Add(Tag.ToString());
		}

		return FString::Printf(TEXT("[%s]"), *FString::Join(TagStrings, TEXT(", ")));
	}

	FString DescribeZoneFeatureCountExpectation(const FLayoutZoneFeatureRequirement& Requirement)
	{
		if (Requirement.MaxCount > 0)
		{
			if (Requirement.MinCount == Requirement.MaxCount)
			{
				return FString::Printf(TEXT("exactly %d"), Requirement.MinCount);
			}

			return FString::Printf(TEXT("between %d and %d"), Requirement.MinCount, Requirement.MaxCount);
		}

		if (Requirement.MinCount > 0)
		{
			return FString::Printf(TEXT("at least %d"), Requirement.MinCount);
		}

		return TEXT("any count");
	}

	const FLayoutRegionContentEntrySolveSnapshot* FindContentSetEntrySnapshotById(
		const FLayoutRegionContentSetSolveSnapshot& ContentSetSnapshot,
		const FName EntryId)
	{
		return ContentSetSnapshot.Entries.FindByPredicate([EntryId](const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot)
		{
			return EntrySnapshot.EntryId == EntryId;
		});
	}

	struct FZoneFeatureRequirementCountDetails
	{
		int32 MatchCount = 0;
		TArray<FString> MatchingSources;
	};

	FString BuildZoneFeatureRequirementOutcomeMessage(
		const FString& RegionDebugPath,
		const FLayoutZoneFeatureRequirement& Requirement,
		const FZoneFeatureRequirementCountDetails& CountDetails,
		const TArray<FString>& AvailableProviders,
		const FString& Problem)
	{
		const FString ZoneName = StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Requirement.Zone));
		const FString MatchModeName = StaticEnum<ELayoutZoneFeatureMatchMode>()->GetNameStringByValue(static_cast<int64>(Requirement.MatchMode));
		const FString MatchingSourcesText = CountDetails.MatchingSources.IsEmpty()
			? TEXT("<none>")
			: FString::Join(CountDetails.MatchingSources, TEXT(", "));
		const FString ProviderText = AvailableProviders.IsEmpty()
			? TEXT("<none>")
			: FString::Join(AvailableProviders, TEXT(", "));

		return FString::Printf(
			TEXT("Zone feature requirement '%s' was not satisfied.\n")
			TEXT("Region: %s\n")
			TEXT("Zone: %s\n")
			TEXT("Match mode: %s\n")
			TEXT("Required features: %s\n")
			TEXT("Expected count: %s\n")
			TEXT("Observed count: %d\n")
			TEXT("Matching sources: %s\n")
			TEXT("Available providers: %s\n")
			TEXT("Problem: %s"),
			*Requirement.RequirementId.ToString(),
			*RegionDebugPath,
			*ZoneName,
			*MatchModeName,
			*FormatZoneFeatureTags(Requirement.RequiredFeatures),
			*DescribeZoneFeatureCountExpectation(Requirement),
			CountDetails.MatchCount,
			*MatchingSourcesText,
			*ProviderText,
			*Problem);
	}

	void AppendInclusiveLineCells(
		const FIntVector& StartCell,
		const FIntVector& EndCell,
		TSet<FIntVector>& OutCells)
	{
		const FIntVector Step(
			(StartCell.X == EndCell.X) ? 0 : (StartCell.X < EndCell.X ? 1 : -1),
			(StartCell.Y == EndCell.Y) ? 0 : (StartCell.Y < EndCell.Y ? 1 : -1),
			(StartCell.Z == EndCell.Z) ? 0 : (StartCell.Z < EndCell.Z ? 1 : -1));

		FIntVector CurrentCell = StartCell;
		while (true)
		{
			OutCells.Add(CurrentCell);
			if (CurrentCell == EndCell)
			{
				break;
			}

			CurrentCell += Step;
		}
	}

		void SortCommittedEndpointAnchors(TArray<FLayoutCommittedEndpointAnchor>& Anchors)
	{
		Anchors.Sort([](const FLayoutCommittedEndpointAnchor& Left, const FLayoutCommittedEndpointAnchor& Right)
		{
			if (Left.CommitmentId != Right.CommitmentId)
			{
				return Left.CommitmentId.LexicalLess(Right.CommitmentId);
			}

			if (Left.LocalCell != Right.LocalCell)
			{
				if (Left.LocalCell.X != Right.LocalCell.X)
				{
					return Left.LocalCell.X < Right.LocalCell.X;
				}

				if (Left.LocalCell.Y != Right.LocalCell.Y)
				{
					return Left.LocalCell.Y < Right.LocalCell.Y;
				}

				return Left.LocalCell.Z < Right.LocalCell.Z;
			}

			return static_cast<uint8>(Left.FaceDirection) < static_cast<uint8>(Right.FaceDirection);
		});
	}

	bool DoesRequestContainCommittedEndpointAnchor(
		const FLayoutRegionSolveRequest& Request,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection)
	{
		return Request.CommittedEndpointAnchors.ContainsByPredicate(
			[&LocalCell, FaceDirection](const FLayoutCommittedEndpointAnchor& Anchor)
			{
				return Anchor.LocalCell == LocalCell
					&& Anchor.FaceDirection == FaceDirection;
			});
	}

	bool AreCommittedConnectionTagsCompatible(
		const FGameplayTag CapabilityConnectionTag,
		const FGameplayTagContainer& CapabilityAllowedConnectionTags,
		const FGameplayTag CommitmentConnectionTag,
		const FGameplayTagContainer& CommitmentAllowedConnectionTags)
	{
		return CapabilityConnectionTag.IsValid()
			&& CommitmentConnectionTag.IsValid()
			&& (CapabilityConnectionTag == CommitmentConnectionTag
				|| CapabilityAllowedConnectionTags.HasTagExact(CommitmentConnectionTag)
				|| CommitmentAllowedConnectionTags.HasTagExact(CapabilityConnectionTag));
	}

	bool DoesChildCapabilityMatchCommittedAnchor(
		const FLayoutChildCapabilityEndpoint& Capability,
		const FLayoutCommittedEndpointAnchor& Commitment)
	{
		if (Capability.FaceDirection != Commitment.FaceDirection)
		{
			return false;
		}

		if (!AreCommittedConnectionTagsCompatible(
			Capability.ConnectionTag,
			Capability.AllowedConnectionTags,
			Commitment.ConnectionTag,
			Commitment.AllowedConnectionTags))
		{
			return false;
		}

		return Commitment.TraversalChannels.IsEmpty()
			|| Capability.TraversalChannels.HasAnyExact(Commitment.TraversalChannels);
	}

	bool DoesBoundaryPointSatisfyCommittedAnchor(
		const FLayoutSolveBoundaryPoint& BoundaryPoint,
		const FLayoutCommittedEndpointAnchor& Commitment)
	{
		const FIntVector DependentCell = BoundaryPoint.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
		if (DependentCell != Commitment.LocalCell
			|| BoundaryPoint.FaceDirection != FLayoutDirectionUtils::GetOpposite(Commitment.FaceDirection))
		{
			return false;
		}

		if (!AreCommittedConnectionTagsCompatible(
			BoundaryPoint.ConnectionTag,
			BoundaryPoint.AllowedConnectionTags,
			Commitment.ConnectionTag,
			Commitment.AllowedConnectionTags))
		{
			return false;
		}

		if (!Commitment.TraversalChannels.IsEmpty()
			&& !BoundaryPoint.ConnectedTraversalChannels.HasAnyExact(Commitment.TraversalChannels))
		{
			return false;
		}

		return !Commitment.bRequireMatchingYawWithFilledNeighbor
			|| BoundaryPoint.bRequireMatchingYawWithFilledNeighbor;
	}

	void GatherEffectiveProviderIntents(
		const FLayoutModuleSolveSnapshot& ModuleSnapshot,
		TArray<const FLayoutClosureProviderIntent*>& OutProviderIntents)
	{
		OutProviderIntents.Reset();
		for (const FLayoutClosureProviderIntent& ProviderIntent : ModuleSnapshot.ClosureProviderIntents)
		{
			OutProviderIntents.Add(&ProviderIntent);
		}
	}

	FLayoutChildCapabilityEnvelope BuildChildCapabilityEnvelope(const FLayoutRegionSolveRequest& Request)
	{
		if (Request.bUseSuppliedChildCapabilityEnvelope)
		{
			FLayoutChildCapabilityEnvelope Envelope = Request.SuppliedChildCapabilityEnvelope;
			Envelope.RegionDebugPath = Request.RegionDebugPath;
			if (Envelope.SnapshotId == NAME_None)
			{
				Envelope.SnapshotId = Request.EffectiveSnapshotId != NAME_None
					? Request.EffectiveSnapshotId
					: Request.ModuleCatalog.SnapshotId;
			}
			return Envelope;
		}

		FLayoutChildCapabilityEnvelope Envelope;
		Envelope.RegionDebugPath = Request.RegionDebugPath;
		Envelope.SnapshotId = Request.EffectiveSnapshotId != NAME_None
			? Request.EffectiveSnapshotId
			: Request.ModuleCatalog.SnapshotId;

		TSet<FString> SeenCapabilityKeys;
		TSet<FString> SeenSpanCapabilityKeys;
		TSet<FString> SeenSeamCapabilityKeys;
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : Request.ModuleCatalog.Modules)
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
					const ELayoutFaceDirection RotatedFaceDirection = FLayoutDirectionUtils::RotateYaw(Offer.FaceDirection, YawRotationSteps);
					const FString CapabilityKey = FString::Printf(
						TEXT("%d|%s|%s|%s|%s|%d|%d|%d"),
						static_cast<int32>(RotatedFaceDirection),
						*Offer.ConnectionTag.ToString(),
						*TagsToStableKey(Offer.AllowedConnectionTags),
						*TagsToStableKey(Offer.TraversalChannels),
						*RolesToStableKey(Offer.Roles),
						Offer.LocalCell.X,
						Offer.LocalCell.Y,
						Offer.LocalCell.Z);
					if (SeenCapabilityKeys.Contains(CapabilityKey))
					{
						continue;
					}

					SeenCapabilityKeys.Add(CapabilityKey);
					FLayoutChildCapabilityEndpoint& Capability = Envelope.EndpointCapabilities.AddDefaulted_GetRef();
					Capability.CapabilityId = FLayoutId(*FString::Printf(
						TEXT("%s.Capability.%d"),
						*Request.RegionDebugPath,
						Envelope.EndpointCapabilities.Num() - 1));
					Capability.LocalCell = Offer.LocalCell;
					Capability.FaceDirection = RotatedFaceDirection;
					Capability.ConnectionTag = Offer.ConnectionTag;
					Capability.AllowedConnectionTags = Offer.AllowedConnectionTags;
					Capability.TraversalChannels = Offer.TraversalChannels;
					Capability.Roles = Offer.Roles;
					Envelope.ProofRecords.Add(MakeSnapshotProofRecord(
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

			TArray<const FLayoutClosureProviderIntent*> EffectiveProviderIntents;
			GatherEffectiveProviderIntents(ModuleSnapshot, EffectiveProviderIntents);
			for (const FLayoutClosureProviderIntent* ProviderIntentPtr : EffectiveProviderIntents)
			{
				const FLayoutClosureProviderIntent& ProviderIntent = *ProviderIntentPtr;
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
						const ELayoutFaceDirection RotatedFaceDirection = FLayoutDirectionUtils::RotateYaw(SpanOffer.FaceDirection, YawRotationSteps);
						const FString SpanCapabilityKey = FString::Printf(
							TEXT("%s|%d|%d|%d|%s|%d|%d|%d"),
							*EffectiveClosureId.ToString(),
							static_cast<int32>(RotatedFaceDirection),
							SpanOffer.ThicknessCells,
							SpanOffer.bSealsBoundary ? 1 : 0,
							*RolesToStableKey(SpanOffer.Roles),
							SpanOffer.LocalCell.X,
							SpanOffer.LocalCell.Y,
							SpanOffer.LocalCell.Z);
						if (SeenSpanCapabilityKeys.Contains(SpanCapabilityKey))
						{
							continue;
						}

						SeenSpanCapabilityKeys.Add(SpanCapabilityKey);
						FLayoutChildCapabilitySpan& Capability = Envelope.SpanCapabilities.AddDefaulted_GetRef();
						Capability.CapabilityId = FLayoutId(*FString::Printf(
							TEXT("%s.SpanCapability.%d"),
							*Request.RegionDebugPath,
							Envelope.SpanCapabilities.Num() - 1));
						Capability.LocalCell = SpanOffer.LocalCell;
						Capability.ClosureId = EffectiveClosureId;
						Capability.FaceDirection = RotatedFaceDirection;
						Capability.Roles = SpanOffer.Roles;
						Capability.ThicknessCells = SpanOffer.ThicknessCells;
						Capability.bSealsBoundary = SpanOffer.bSealsBoundary;
						Envelope.ProofRecords.Add(MakeSnapshotProofRecord(
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
						const ELayoutFaceDirection RotatedFaceDirection = FLayoutDirectionUtils::RotateYaw(SpanOffer.FaceDirection, YawRotationSteps);
						const FString SeamCapabilityKey = FString::Printf(
							TEXT("%s|%d|%d|%d|%d|%d|%d|%d"),
							*SeamIntent.InterfaceFamily.ToString(),
							static_cast<int32>(SeamIntent.JunctionUsage),
							static_cast<int32>(RotatedFaceDirection),
							SeamIntent.bCanOwnSeam ? 1 : 0,
							SeamIntent.bCanAcceptSeam ? 1 : 0,
							SpanOffer.LocalCell.X,
							SpanOffer.LocalCell.Y,
							SpanOffer.LocalCell.Z);
						if (SeenSeamCapabilityKeys.Contains(SeamCapabilityKey))
						{
							continue;
						}

						SeenSeamCapabilityKeys.Add(SeamCapabilityKey);
						FLayoutChildCapabilitySeam& Capability = Envelope.SeamCapabilities.AddDefaulted_GetRef();
						Capability.CapabilityId = FLayoutId(*FString::Printf(
							TEXT("%s.SeamCapability.%d"),
							*Request.RegionDebugPath,
							Envelope.SeamCapabilities.Num() - 1));
						Capability.LocalCell = SpanOffer.LocalCell;
						Capability.InterfaceFamily = SeamIntent.InterfaceFamily;
						Capability.JunctionUsage = SeamIntent.JunctionUsage;
						Capability.FaceDirection = RotatedFaceDirection;
						Capability.bCanOwnSeam = SeamIntent.bCanOwnSeam;
						Capability.bCanAcceptSeam = SeamIntent.bCanAcceptSeam;
						Envelope.ProofRecords.Add(MakeSnapshotProofRecord(
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

		Envelope.ValidationAssertions.Add(MakeSnapshotAssertionRecord(
			FLayoutId(*FString::Printf(TEXT("%s.ChildCapabilityEndpointContract"), *Request.RegionDebugPath)),
			ELayoutValidationAssertionKind::ChildCapabilityContractValid,
			true,
			{Request.ModuleCatalog.SnapshotId, Request.ProfileSnapshot.SnapshotId},
			FString()));
		return Envelope;
	}

	FScheduledChildSourceInfo ResolveScheduledChildSourceInfo(
		const FLayoutRegionSolveRequest& Request,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, TSet<FString>>& PrerequisitesByRegion)
	{
		FScheduledChildSourceInfo SourceInfo;
		SourceInfo.ChildProfileSnapshotId = Request.ProfileSnapshot.SnapshotId;

		if (Request.SourceContentEntryId != NAME_None)
		{
			SourceInfo.ContentEntryId = Request.SourceContentEntryId;
			SourceInfo.ParentRegionDebugPath = Request.SourceParentRegionDebugPath;
			SourceInfo.bOptional = Request.bSourceContentEntryOptional;
			SourceInfo.bResolved = true;
			return SourceInfo;
		}

		struct FEntryMatch
		{
			FString ParentRegionDebugPath;
			const FLayoutRegionContentEntrySolveSnapshot* EntrySnapshot = nullptr;
		};

		TArray<FEntryMatch> Matches;
		for (const TPair<FString, const FLayoutRegionSolveRequest*>& RequestPair : RequestsByPath)
		{
			if (RequestPair.Value == nullptr)
			{
				continue;
			}

			for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : RequestPair.Value->ContentSetSnapshot.Entries)
			{
				if (EntrySnapshot.ContentKind != ELayoutRegionContentKind::ChildRegion
					|| EntrySnapshot.ChildProfileSnapshotId != Request.ProfileSnapshot.SnapshotId)
				{
					continue;
				}

				FEntryMatch& Match = Matches.AddDefaulted_GetRef();
				Match.ParentRegionDebugPath = RequestPair.Key;
				Match.EntrySnapshot = &EntrySnapshot;
			}
		}

		if (Matches.Num() > 1)
		{
			if (const TSet<FString>* Prerequisites = PrerequisitesByRegion.Find(Request.RegionDebugPath))
			{
				TArray<FEntryMatch> FilteredMatches;
				for (const FString& PrerequisitePath : *Prerequisites)
				{
					const FEntryMatch* Match = Matches.FindByPredicate([&PrerequisitePath](const FEntryMatch& Candidate)
					{
						return Candidate.ParentRegionDebugPath == PrerequisitePath;
					});
					if (Match != nullptr)
					{
						FilteredMatches.Add(*Match);
					}
				}

				if (FilteredMatches.Num() == 1)
				{
					Matches = MoveTemp(FilteredMatches);
				}
			}
		}

		if (Matches.Num() == 1 && Matches[0].EntrySnapshot != nullptr)
		{
			SourceInfo.ContentEntryId = Matches[0].EntrySnapshot->EntryId;
			SourceInfo.ParentRegionDebugPath = Matches[0].ParentRegionDebugPath;
			SourceInfo.bOptional = Matches[0].EntrySnapshot->bChildOptional;
			SourceInfo.bResolved = true;
		}

		return SourceInfo;
	}

	int32 GetNearestCellDistance(const FIntVector& SourceCell, const TArray<FIntVector>& TargetCells)
	{
		if (TargetCells.IsEmpty())
		{
			return 0;
		}

		int32 BestDistance = MAX_int32;
		for (const FIntVector& TargetCell : TargetCells)
		{
			BestDistance = FMath::Min(
				BestDistance,
				FMath::Abs(SourceCell.X - TargetCell.X)
					+ FMath::Abs(SourceCell.Y - TargetCell.Y)
					+ FMath::Abs(SourceCell.Z - TargetCell.Z));
		}
		return BestDistance == MAX_int32 ? 0 : BestDistance;
	}

	int32 GetParentContactIntentScore(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Entry:
			return 600;
		case ELayoutCellIntent::VerticalAccess:
			return 500;
		case ELayoutCellIntent::Connector:
			return 425;
		case ELayoutCellIntent::Core:
			return 450;
		case ELayoutCellIntent::Interior:
			return 350;
		case ELayoutCellIntent::Boundary:
			// Child-entry commitments must route through an actual traversable parent cell.
			// Shared perimeter ownership is handled separately through exported boundary points,
			// so allowing boundary cells here incorrectly forces traversal onto shell modules.
			return MIN_int32 / 4;
		default:
			return 0;
		}
	}

	FChildPlanningVariantInterfaceSummary BuildChildPlanningVariantInterfaceSummary(
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells)
	{
		FChildPlanningVariantInterfaceSummary Summary;
		auto GatherBoundaryFaceConnectionTag =
			[&CapabilityEnvelope](
				const FIntVector& LocalCell,
				const ELayoutFaceDirection FaceDirection)
		{
			for (const FLayoutChildCapabilityEndpoint& Capability :
				CapabilityEnvelope.EndpointCapabilities)
			{
				if (Capability.LocalCell == LocalCell
					&& Capability.FaceDirection == FaceDirection
					&& Capability.ConnectionTag.IsValid())
				{
					return Capability.ConnectionTag;
				}
			}

			return FGameplayTag();
		};
		auto GatherBoundaryFaceAllowedConnectionTags =
			[&CapabilityEnvelope](
				const FIntVector& LocalCell,
				const ELayoutFaceDirection FaceDirection)
		{
			FGameplayTagContainer AllowedConnectionTags;
			for (const FLayoutChildCapabilityEndpoint& Capability :
				CapabilityEnvelope.EndpointCapabilities)
			{
				if (Capability.LocalCell != LocalCell
					|| Capability.FaceDirection != FaceDirection)
				{
					continue;
				}

				AllowedConnectionTags.AppendTags(
					Capability.AllowedConnectionTags);
			}

			return AllowedConnectionTags;
		};
		auto GatherBoundaryFaceTraversalChannels =
			[&CapabilityEnvelope](
				const FIntVector& LocalCell,
				const ELayoutFaceDirection FaceDirection)
		{
			FGameplayTagContainer TraversalChannels;
			for (const FLayoutChildCapabilityEndpoint& Capability :
				CapabilityEnvelope.EndpointCapabilities)
			{
				if (Capability.LocalCell != LocalCell
					|| Capability.FaceDirection != FaceDirection)
				{
					continue;
				}

				TraversalChannels.AppendTags(Capability.TraversalChannels);
			}

			return TraversalChannels;
		};
		TSet<FIntVector> ChildPlannedCellSet;
		ChildPlannedCellSet.Reserve(ChildPlannedCells.Num());
		for (const FLayoutPlannedCell& ChildPlannedCell : ChildPlannedCells)
		{
			ChildPlannedCellSet.Add(ChildPlannedCell.Cell);
			Summary.FootprintCells.AddUnique(ChildPlannedCell.Cell);
		}

		Summary.BoundarySpanCapabilities = CapabilityEnvelope.SpanCapabilities;
		Summary.ParentShareableSpanOffers = Summary.BoundarySpanCapabilities;
		Summary.SeamCapabilities = CapabilityEnvelope.SeamCapabilities;
		Summary.ParentShareableSeamOffers = Summary.SeamCapabilities;
		Summary.SiblingShareableSeamOffers = Summary.SeamCapabilities;
		for (const FLayoutChildCapabilityEndpoint& Capability : CapabilityEnvelope.EndpointCapabilities)
		{
			if (!Capability.Roles.Contains(ELayoutModuleRole::Entry))
			{
				continue;
			}

			Summary.EntryCapabilities.Add(Capability);
			Summary.ParentShareableEntryOffers.Add(Capability);
			Summary.bHasExplicitEntryCapabilities =
				Summary.bHasExplicitEntryCapabilities
				|| Capability.ConnectionTag == LayoutGameplayTags::FaceEntry;
		}

		if (Summary.bHasExplicitEntryCapabilities)
		{
			Summary.EntryCapabilities.RemoveAll([](const FLayoutChildCapabilityEndpoint& Capability)
			{
				return Capability.ConnectionTag != LayoutGameplayTags::FaceEntry;
			});
		}

		for (const FLayoutPlannedCell& ChildPlannedCell : ChildPlannedCells)
		{
			static const ELayoutFaceDirection BoundaryDirections[] =
			{
				ELayoutFaceDirection::PosX,
				ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY,
				ELayoutFaceDirection::NegY,
				ELayoutFaceDirection::PosZ,
				ELayoutFaceDirection::NegZ
			};
			for (const ELayoutFaceDirection Direction : BoundaryDirections)
			{
				const FIntVector NeighborCell = ChildPlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (ChildPlannedCellSet.Contains(NeighborCell))
				{
					continue;
				}

				FChildPlanningVariantInterfaceSummary::FBoundaryFace& BoundaryFace =
					Summary.BoundaryFaces.AddDefaulted_GetRef();
				BoundaryFace.LocalCell = ChildPlannedCell.Cell;
				BoundaryFace.FaceDirection = Direction;
				BoundaryFace.Intent = ChildPlannedCell.Intent;
				BoundaryFace.ConnectionTag =
					GatherBoundaryFaceConnectionTag(
						ChildPlannedCell.Cell,
						Direction);
				BoundaryFace.AllowedConnectionTags =
					GatherBoundaryFaceAllowedConnectionTags(
						ChildPlannedCell.Cell,
						Direction);
				BoundaryFace.TraversalChannels =
					GatherBoundaryFaceTraversalChannels(
						ChildPlannedCell.Cell,
						Direction);
			}

			if (ChildPlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				Summary.EntryCells.AddUnique(ChildPlannedCell.Cell);
			}
			else if (ChildPlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				Summary.bSupportsVerticalAccess = true;
				Summary.VerticalAccessCells.Add(ChildPlannedCell.Cell);
			}
		}

		return Summary;
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
		FString* OutFailureReason = nullptr);

	int32 GetParentContactIntentScore(const ELayoutCellIntent Intent);

	bool CanAnyRemainingPeerFitStructurally(
		const FPreparedChildPlacementSource& PreparedSource,
		const FPreparedChildPlacementSource::FPlanningVariant& CurrentPlanningVariant,
		const TArray<FAutomaticChildPlacement>& CurrentPlacements,
		const TSet<FIntVector>& CurrentReservedParentCells,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TArray<FIntVector>& CandidateParentPlanReservedCells,
		const FIntVector& CandidateOffset,
		const FLayoutChildStageMappingResult& CandidateStageMapping,
		const FChildPlacementDemand& Demand)
	{
		FAutomaticChildPlacement ProspectivePlacement;
		ProspectivePlacement.EntryId = PreparedSource.EntrySnapshot->EntryId;
		ProspectivePlacement.ChildRegionDebugPath = Demand.ChildRegionDebugPath;
		ProspectivePlacement.RegionCellOffset = CandidateOffset;
		ProspectivePlacement.StageMapping = CandidateStageMapping;
		ProspectivePlacement.ParentTranslatedPlannedCells = CandidateStageMapping.ParentTranslatedPlannedCells;
		ProspectivePlacement.ChildCapabilityEnvelope = CurrentPlanningVariant.CapabilityEnvelope;
		ProspectivePlacement.bSupportsVerticalAccess = CurrentPlanningVariant.InterfaceSummary.bSupportsVerticalAccess;
		ProspectivePlacement.bContributesHostVerticalAccess = PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess;
		ProspectivePlacement.ChildVerticalAccessLocalCells = CurrentPlanningVariant.InterfaceSummary.VerticalAccessCells;
		ProspectivePlacement.ChildRequest.FootprintSize = CurrentPlanningVariant.FootprintSize;
		ProspectivePlacement.ChildRequest.PlannedCells = CandidateStageMapping.ChildLocalPlannedCells;

		TArray<FAutomaticChildPlacement> ProspectivePlacements = CurrentPlacements;
		ProspectivePlacements.Add(ProspectivePlacement);

		TSet<FIntVector> ProspectiveReservedParentCells = CurrentReservedParentCells;
		for (const FIntVector& ReservedCell : CandidateParentPlanReservedCells)
		{
			ProspectiveReservedParentCells.Add(ReservedCell);
		}

		for (const FPreparedChildPlacementSource::FPlanningVariant& PeerVariant : PreparedSource.PlanningVariants)
		{
			for (const FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& PeerCandidate : PeerVariant.NormalizedCandidates)
			{
				const FIntVector& PeerOffset = PeerCandidate.Offset;
				bool bPeerFitsParentPlan = true;
				for (const FLayoutPlannedCell& PeerChildPlannedCell : PeerVariant.PlannedCells)
				{
					const FIntVector ParentCell = MapSourceChildCellToParent(
						PeerChildPlannedCell.Cell,
						PeerOffset,
						&PeerCandidate.StageMapping);
					if (ProspectiveReservedParentCells.Contains(ParentCell)
						&& !CanChildReservationShareSiblingSeamCell(
							PeerChildPlannedCell,
							PeerOffset,
							PeerVariant.FootprintSize,
							PeerVariant.CapabilityEnvelope,
							ProspectivePlacements,
							nullptr,
							&ParentCell))
					{
						bPeerFitsParentPlan = false;
						break;
					}
				}

				if (!bPeerFitsParentPlan)
				{
					continue;
				}

				bool bPeerEntryFeasible = true;
				for (const FIntVector& PeerEntryCell : PeerVariant.InterfaceSummary.EntryCells)
				{
					bool bEntryCellHasOption = false;
					bool bEntryCellHasProtectedDirectOption = false;
					for (const FLayoutChildCapabilityEndpoint& Capability : PeerVariant.InterfaceSummary.EntryCapabilities)
					{
						if (Capability.LocalCell.Z != PeerEntryCell.Z)
						{
							continue;
						}

						if (Capability.FaceDirection == ELayoutFaceDirection::PosZ
							|| Capability.FaceDirection == ELayoutFaceDirection::NegZ)
						{
							continue;
						}

						const FIntVector PeerNeighborCell =
							PeerEntryCell + FLayoutDirectionUtils::ToCellDelta(Capability.FaceDirection);
						if (PeerVariant.PlannedCellSet.Contains(PeerNeighborCell))
						{
							continue;
						}

						const FIntVector ParentEntryCell = MapSourceChildCellToParent(
							PeerEntryCell,
							PeerOffset,
							&PeerCandidate.StageMapping);
						const FIntVector ParentContactCell =
							ParentEntryCell + FLayoutDirectionUtils::ToCellDelta(Capability.FaceDirection);
						if (ParentPlannedCellSet.Contains(ParentContactCell)
							&& !ProspectiveReservedParentCells.Contains(ParentContactCell))
						{
							const ELayoutCellIntent* ParentIntent = ParentPlannedCellIntents.Find(ParentContactCell);
							if (ParentIntent != nullptr
								&& GetParentContactIntentScore(*ParentIntent) > (MIN_int32 / 8))
							{
								if (!ParentProtectedTraversalCells.Contains(ParentContactCell))
								{
									bEntryCellHasOption = true;
									break;
								}

								bEntryCellHasProtectedDirectOption = true;
							}
						}

						const FIntVector SharedParentCell = MapSourceChildCellToParent(
							PeerEntryCell,
							PeerOffset,
							&PeerCandidate.StageMapping);
						for (const FAutomaticChildPlacement& ExistingPlacement : ProspectivePlacements)
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

								ELayoutFaceDirection PeerOverlapDirection = ELayoutFaceDirection::PosX;
								ELayoutFaceDirection ExistingOverlapDirection = ELayoutFaceDirection::PosX;
								if (!TryGetSharedOverlapFaceDirections(
									PeerEntryCell,
									PeerVariant.FootprintSize,
									ExistingChildPlannedCell.Cell,
									ExistingPlacement.ChildRequest.FootprintSize,
									PeerOverlapDirection,
									ExistingOverlapDirection))
								{
									continue;
								}

								if (PeerOverlapDirection != Capability.FaceDirection)
								{
									continue;
								}

								if (!DoChildSeamCapabilitiesSupportSpecificSharedInterface(
									PeerVariant.CapabilityEnvelope,
									PeerOverlapDirection,
									ExistingPlacement.ChildCapabilityEnvelope,
									ExistingOverlapDirection,
									LayoutGameplayTags::InterfacePartitionDoor))
								{
									continue;
								}

								FString DoorSeamOwnerRegionPath;
								FString DoorSeamPassiveRegionPath;
								if (!TryChooseSpecificSharedInterfaceOwner(
									TEXT("__RemainingPeer__"),
									PeerVariant.CapabilityEnvelope,
									PeerOverlapDirection,
									ExistingPlacement.ChildRegionDebugPath,
									ExistingPlacement.ChildCapabilityEnvelope,
									ExistingOverlapDirection,
									LayoutGameplayTags::InterfacePartitionDoor,
									DoorSeamOwnerRegionPath,
									DoorSeamPassiveRegionPath))
								{
									continue;
								}

								if (DoorSeamOwnerRegionPath == TEXT("__RemainingPeer__"))
								{
									bEntryCellHasOption = true;
									break;
								}
							}

							if (bEntryCellHasOption)
							{
								break;
							}
						}

						if (bEntryCellHasOption)
						{
							break;
						}

					}

					if (!bEntryCellHasOption && bEntryCellHasProtectedDirectOption)
					{
						bEntryCellHasOption = true;
					}

					if (!bEntryCellHasOption)
					{
						bPeerEntryFeasible = false;
						break;
					}
				}

				if (!bPeerEntryFeasible)
				{
					continue;
				}

				return true;
			}
		}

		return false;
	}


	/** Initializes one prepared source per authored child; counted slot expansion happens once in BuildChildPlacementDemands. */
	TMap<FLayoutId, int32> BuildDefaultChildPlacementSourceCounts(
		const FLayoutRegionSolveRequest& RootRequest)
	{
		TMap<FLayoutId, int32> SourceCounts;
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot :
			RootRequest.ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.ContentKind == ELayoutRegionContentKind::ChildRegion)
			{
				SourceCounts.Add(EntrySnapshot.EntryId, 1);
			}
		}
		return SourceCounts;
	}

	/** Builds all child planning probes and yaw-normalized planning variants for recursive scheduling. */
	static TArray<FIntVector> BuildRotatedPlanningVariantStructuralSupportLocalCells(
		const TArray<LayoutRegionScheduleSolverFacade::FPlacementCapabilityBundle>& BasePlacementBundles,
		const TArray<FLayoutPlannedCell>& PlanningProbeCells,
		const FIntPoint& PlanningProbeFootprintSize,
		const int32 YawRotationSteps)
	{
		TSet<FIntVector> StructuralSupportLocalCellSet;
		if (!BasePlacementBundles.IsEmpty())
		{
			for (const LayoutRegionScheduleSolverFacade::FPlacementCapabilityBundle& PlacementBundle : BasePlacementBundles)
			{
				for (const FIntVector& OccupiedLocalCell : PlacementBundle.OccupiedLocalCells)
				{
					StructuralSupportLocalCellSet.Add(RotateCellInFootprintYaw(
						OccupiedLocalCell,
						PlanningProbeFootprintSize,
						YawRotationSteps));
				}
			}
		}
		else
		{
			for (const FLayoutPlannedCell& PlannedCell : PlanningProbeCells)
			{
				StructuralSupportLocalCellSet.Add(RotateCellInFootprintYaw(
					PlannedCell.Cell,
					PlanningProbeFootprintSize,
					YawRotationSteps));
			}
		}

		TArray<FIntVector> StructuralSupportLocalCells;
		for (const FIntVector& Cell : StructuralSupportLocalCellSet)
		{
			StructuralSupportLocalCells.Add(Cell);
		}
		StructuralSupportLocalCells.Sort([](const FIntVector& Left, const FIntVector& Right)
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
		return StructuralSupportLocalCells;
	}

	bool BuildPreparedChildPlacementSources(
		const FLayoutRegionSolveRequest& RootRequest,
		const TMap<FLayoutId, int32>& RequiredInstancesByEntryId,
		TArray<FPreparedChildPlacementSource>& OutPreparedSources,
		TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary>& OutProviderChoices,
		FRecursiveSchedulerPerfMetrics* InOutPerfMetrics,
		FString& OutFailureReason)
	{
		SCOPED_NAMED_EVENT(Layout_Recursive_SourcePreparation, FColor::Cyan);
		const double PreparedSourceBuildStartSeconds = FPlatformTime::Seconds();
		OutPreparedSources.Reset();
		OutPreparedSources.Reserve(RequiredInstancesByEntryId.Num());

		// Local project fix: expose direct-child feature choices from immutable snapshots before transform search or child proof.
		TArray<LayoutZoneFeatureDemand::FHardDemand> HardDemands;
		LayoutZoneFeatureDemand::CompileHardDemands(
			RootRequest.ProfileSnapshot.ZoneFeatureRequirements,
			HardDemands);
		LayoutZoneFeatureDemand::CompileProviderChoiceSummaries(
			RootRequest.EffectiveSnapshotId,
			HardDemands,
			RootRequest.ContentSetSnapshot.Entries,
			OutProviderChoices);

		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : RootRequest.ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.ContentKind != ELayoutRegionContentKind::ChildRegion)
			{
				continue;
			}

			FPreparedChildPlacementSource& PreparedSource = OutPreparedSources.AddDefaulted_GetRef();
			PreparedSource.EntrySnapshot = &EntrySnapshot;
			PreparedSource.HardFeatureProviderChoices = OutProviderChoices.FilterByPredicate(
				[&EntrySnapshot](const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice)
				{
					return Choice.ContentKind == ELayoutRegionContentKind::ChildRegion
						&& Choice.SourceContentEntryId == EntrySnapshot.EntryId;
				});
			PreparedSource.RequiredInstanceCount = RequiredInstancesByEntryId.FindRef(EntrySnapshot.EntryId);
			if (!EntrySnapshot.CompiledChildRequestTemplate.IsValid())
			{
				OutFailureReason = FString::Printf(
					TEXT("Required child entry could not be expanded because its snapshot-only child request template is missing.\n%s\nProblem: The parent content set snapshot does not carry a compiled child profile/content template for this entry.\nFix: Rebuild the parent content set snapshot so the child entry compiles into an immutable child request template."),
					*DescribeChildPlacementContext(RootRequest, EntrySnapshot));
				return false;
			}

			const int32 ChildSeed = static_cast<int32>(HashCombineFast(static_cast<uint32>(RootRequest.Seed), GetTypeHash(EntrySnapshot.EntryId)));
			const double ChildTemplateBuildStartSeconds = FPlatformTime::Seconds();
			PreparedSource.ChildRequestTemplate = BuildChildRequestFromTemplate(
				*EntrySnapshot.CompiledChildRequestTemplate,
				ChildSeed,
				FString::Printf(TEXT("%s/%s"), *RootRequest.RegionDebugPath, *EntrySnapshot.EntryId.ToString()),
				EntrySnapshot.EntryId,
				RootRequest.RegionDebugPath,
				EntrySnapshot.bChildOptional,
				RootRequest.ExecutionSettings);
			if (InOutPerfMetrics != nullptr)
			{
				InOutPerfMetrics->ChildTemplateBuildSeconds += (FPlatformTime::Seconds() - ChildTemplateBuildStartSeconds);
			}

			switch (PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCountMode)
			{
			case ELayoutCountConstraintMode::Range:
			{
				PreparedSource.EntryCountDomain.MinCount =
					FMath::Max(0, PreparedSource.ChildRequestTemplate.ProfileSnapshot.MinEntryCount);
				PreparedSource.EntryCountDomain.MaxCount =
					FMath::Max(PreparedSource.EntryCountDomain.MinCount, PreparedSource.ChildRequestTemplate.ProfileSnapshot.MaxEntryCount);
				for (int32 RequestedEntryCount = PreparedSource.EntryCountDomain.MinCount; RequestedEntryCount <= PreparedSource.EntryCountDomain.MaxCount; ++RequestedEntryCount)
				{
					PreparedSource.EntryCountDomain.AdmissibleCounts.Add(RequestedEntryCount);
				}
				break;
			}
			case ELayoutCountConstraintMode::Exact:
				PreparedSource.EntryCountDomain.MinCount = PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount;
				PreparedSource.EntryCountDomain.MaxCount = PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount;
				PreparedSource.EntryCountDomain.AdmissibleCounts.Add(PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount);
				break;
			default:
				PreparedSource.EntryCountDomain.MinCount = PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount;
				PreparedSource.EntryCountDomain.MaxCount = PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount;
				PreparedSource.EntryCountDomain.AdmissibleCounts.Add(PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount);
				break;
			}

			if (PreparedSource.EntryCountDomain.AdmissibleCounts.IsEmpty())
			{
				PreparedSource.EntryCountDomain.MinCount = PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount;
				PreparedSource.EntryCountDomain.MaxCount = PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount;
				PreparedSource.EntryCountDomain.AdmissibleCounts.Add(PreparedSource.ChildRequestTemplate.ProfileSnapshot.EntryCount);
			}

			TSet<FString> SeenPlanningVariantKeys;
			FString LastPreparationFailureReason;
			for (const int32 RequestedEntryCount : PreparedSource.EntryCountDomain.AdmissibleCounts)
			{
				FLayoutRegionSolveRequest PreparedVariantRequest = PreparedSource.ChildRequestTemplate;
				PreparedVariantRequest.Seed = static_cast<int32>(HashCombineFast(
					static_cast<uint32>(PreparedSource.ChildRequestTemplate.Seed),
					HashCombineFast(static_cast<uint32>(RequestedEntryCount), 1u)));
				if (PreparedVariantRequest.ProfileSnapshot.EntryCountMode == ELayoutCountConstraintMode::Range)
				{
					PreparedVariantRequest.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::Exact;
					PreparedVariantRequest.ProfileSnapshot.EntryCount = RequestedEntryCount;
					PreparedVariantRequest.ProfileSnapshot.MinEntryCount = RequestedEntryCount;
					PreparedVariantRequest.ProfileSnapshot.MaxEntryCount = RequestedEntryCount;
				}

				const double ChildPreparationStartSeconds = FPlatformTime::Seconds();
				const FLayoutRegionSolveResult PreparedTopology =
					LayoutProfileSolverInternal::BuildRequestBackedChildIntrinsicPreparedTopology(
						PreparedVariantRequest);
				if (InOutPerfMetrics != nullptr)
				{
					InOutPerfMetrics->ChildPlanningVariantBuildSeconds +=
						FPlatformTime::Seconds() - ChildPreparationStartSeconds;
				}
				PreparedSource.PreparationCandidateAttemptCount +=
					PreparedTopology.SolveResult.PropagationStats.CandidateAttemptCount;
				if (!PreparedTopology.SolveResult.bSucceeded
					|| PreparedTopology.SolveResult.PlannedCells.IsEmpty()
					|| PreparedTopology.SolveResult.FootprintSize.X <= 0
					|| PreparedTopology.SolveResult.FootprintSize.Y <= 0)
				{
					if (PreparedSource.PreparationFailureKind == ELayoutSolvePreparationFailureKind::None)
					{
						PreparedSource.PreparationFailureKind =
							PreparedTopology.SolveResult.PreparationFailureKind;
					}
					LastPreparationFailureReason = PreparedTopology.SolveResult.FailureReason;
					continue;
				}

				PreparedSource.bPreparationSucceeded = true;
				PreparedSource.ChildFootprintSize = PreparedTopology.SolveResult.FootprintSize;
				PreparedSource.ChildPlannedCells = PreparedTopology.SolveResult.PlannedCells;
				PreparedVariantRequest.FootprintSize = PreparedTopology.SolveResult.FootprintSize;
				TArray<FLayoutVerticalAccessHostGroup> PreparedHostGroups;
				FString VerticalAccessPlanningFailure;
				if (!LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
					PreparedVariantRequest,
					PreparedSource.ChildPlannedCells,
					PreparedHostGroups,
					VerticalAccessPlanningFailure))
				{
					LastPreparationFailureReason = MoveTemp(VerticalAccessPlanningFailure);
					continue;
				}
				PreparedVariantRequest.PlannedCells = PreparedSource.ChildPlannedCells;
				PreparedVariantRequest.PrecomputedPlannedCells = PreparedSource.ChildPlannedCells;
				PreparedVariantRequest.VerticalAccessHostGroups = MoveTemp(PreparedHostGroups);

				const FLayoutChildCapabilityEnvelope BaseCapabilityEnvelope =
					BuildChildCapabilityEnvelope(PreparedVariantRequest);
				const TArray<LayoutRegionScheduleSolverFacade::FPlacementCapabilityBundle> BasePlacementBundles =
					LayoutRegionScheduleSolverFacade::BuildModulePlacementBundles(
						PreparedVariantRequest);
				for (int32 YawRotationSteps = 0; YawRotationSteps < 4; ++YawRotationSteps)
				{
					const FIntPoint RotatedFootprintSize = RotateFootprintSizeYaw(
						PreparedSource.ChildFootprintSize,
						YawRotationSteps);
					TArray<FLayoutPlannedCell> RotatedPlannedCells;
					RotatedPlannedCells.Reserve(PreparedSource.ChildPlannedCells.Num());
					TSet<FIntVector> RotatedPlannedCellSet;
					int32 RotatedMaxLocalLevel = 0;
					for (const FLayoutPlannedCell& ChildPlannedCell : PreparedSource.ChildPlannedCells)
					{
						FLayoutPlannedCell& RotatedPlannedCell = RotatedPlannedCells.AddDefaulted_GetRef();
						RotatedPlannedCell = ChildPlannedCell;
						RotatedPlannedCell.Cell = RotateCellInFootprintYaw(
							ChildPlannedCell.Cell,
							PreparedSource.ChildFootprintSize,
							YawRotationSteps);
						RotatedPlannedCellSet.Add(RotatedPlannedCell.Cell);
						RotatedMaxLocalLevel = FMath::Max(
							RotatedMaxLocalLevel,
							RotatedPlannedCell.Cell.Z);
					}
					RotatedPlannedCells.Sort([](const FLayoutPlannedCell& Left, const FLayoutPlannedCell& Right)
					{
						return Left.Cell.Z != Right.Cell.Z ? Left.Cell.Z < Right.Cell.Z
							: Left.Cell.Y != Right.Cell.Y ? Left.Cell.Y < Right.Cell.Y
							: Left.Cell.X < Right.Cell.X;
					});

					const FString VariantKey = FString::Printf(
						TEXT("EntryCount=%d|Yaw=%d|Footprint=%s|Cells=%s"),
						RequestedEntryCount,
						YawRotationSteps,
						*RotatedFootprintSize.ToString(),
						*FString::JoinBy(RotatedPlannedCells, TEXT(";"), [](const FLayoutPlannedCell& PlannedCell)
						{
							return FString::Printf(
								TEXT("%s:%d:%d:%d%s"),
								*PlannedCell.Cell.ToString(),
								static_cast<int32>(PlannedCell.Intent),
								PlannedCell.ModuleLevelIndex,
								PlannedCell.bIsBridgeCell ? 1 : 0,
								PlannedCell.bIsTopBridgeOffer ? TEXT(":TopBridgeOffer") : TEXT(""));
						}));
					if (SeenPlanningVariantKeys.Contains(VariantKey))
					{
						continue;
					}
					SeenPlanningVariantKeys.Add(VariantKey);
					if (InOutPerfMetrics != nullptr)
					{
						++InOutPerfMetrics->ChildPlanningVariantCount;
					}

					FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant =
						PreparedSource.PlanningVariants.AddDefaulted_GetRef();
					PlanningVariant.ChildRequestTemplate = PreparedVariantRequest;
					PlanningVariant.YawRotationSteps = YawRotationSteps;
					PlanningVariant.FootprintSize = RotatedFootprintSize;
					PlanningVariant.MaxLocalLevel = RotatedMaxLocalLevel;
					PlanningVariant.PlannedCells = MoveTemp(RotatedPlannedCells);
					PlanningVariant.PlannedCellSet = MoveTemp(RotatedPlannedCellSet);
					PlanningVariant.StructuralSupportLocalCells =
						BuildRotatedPlanningVariantStructuralSupportLocalCells(
							BasePlacementBundles,
							PreparedSource.ChildPlannedCells,
							PreparedSource.ChildFootprintSize,
							YawRotationSteps);
					PlanningVariant.CapabilityEnvelope = RotateChildCapabilityEnvelopeYaw(
						BaseCapabilityEnvelope,
						PreparedSource.ChildFootprintSize,
						YawRotationSteps);
					PlanningVariant.InterfaceSummary = BuildChildPlanningVariantInterfaceSummary(
						PlanningVariant.CapabilityEnvelope,
						PlanningVariant.PlannedCells);
				}
			}

			PreparedSource.PreparationFailureReason = LastPreparationFailureReason;
			if (PreparedSource.bPreparationSucceeded && PreparedSource.PlanningVariants.IsEmpty())
			{
				PreparedSource.bPreparationSucceeded = false;
				PreparedSource.PreparationFailureReason = TEXT("Prepared child topology produced no usable variants after entry-count and yaw normalization.");
			}
			if (PreparedSource.bPreparationSucceeded
				&& !PreparedSource.PlanningVariants.ContainsByPredicate(
					[](const FPreparedChildPlacementSource::FPlanningVariant& Variant)
					{
						return Variant.InterfaceSummary.EntryCells.IsEmpty()
							|| !Variant.InterfaceSummary.EntryCapabilities.IsEmpty();
					}))
			{
				// Boundary-domain compilation owns this host-independent failure;
				// no offset can repair an Entry cell with no endpoint capability.
				PreparedSource.bPreparationSucceeded = false;
				PreparedSource.PreparationFailureKind =
					ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible;
				PreparedSource.PreparationFailureReason =
					TEXT("Prepared child boundary domain has authored Entry cells but no entry-capable endpoint contract on any deterministic plan/yaw variant.");
			}
		}

		if (InOutPerfMetrics != nullptr)
		{
			InOutPerfMetrics->PreparedChildSourceCount = OutPreparedSources.Num();
			InOutPerfMetrics->PreparedSourceBuildSeconds += (FPlatformTime::Seconds() - PreparedSourceBuildStartSeconds);
		}

		return true;
	}

	/** Builds atomic child-local/parent-local geometry. Stepped-capable ground children follow
	 * nonbridge authored ground; support volume alone cannot certify a rigid ground plane. */
	bool TryBuildChildStageMapping(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ChildFootprintSize,
		const FIntVector& CandidateOffset,
		const FName SourceEntryId,
		const int32 YawRotationSteps,
		const bool bChildSupportsSteppedTerrain,
		const TArray<FLayoutPlannedCell>& SourceChildPlannedCells,
		FLayoutChildStageMappingResult& OutMapping,
		FString& OutFailureReason,
		const FLayoutRegionSolveRequest* ChildRequest = nullptr)
	{
		OutMapping = FLayoutChildStageMappingResult{};
		OutFailureReason.Reset();
		OutMapping.ParentRegionCellOffset = CandidateOffset;

		TMap<FIntPoint, const FLayoutFrozenTerrainStageCellRecord*> StageByParentColumn;
		for (const FLayoutFrozenTerrainStageCellRecord& StageRecord :
			ParentRequest.PrecomputedFrozenTerrainContract.StageMap)
		{
			if (StageByParentColumn.Contains(StageRecord.FootprintCellXY))
			{
				OutFailureReason = FString::Printf(
					TEXT("Parent frozen stage map contains duplicate column X=%d Y=%d."),
					StageRecord.FootprintCellXY.X,
					StageRecord.FootprintCellXY.Y);
				return false;
			}
			StageByParentColumn.Add(StageRecord.FootprintCellXY, &StageRecord);
		}

		TSet<FIntVector> ParentPhysicalCells;
		TMap<FIntPoint, TArray<const FLayoutPlannedCell*>> ParentCellsByColumn;
		for (const FLayoutPlannedCell& ParentCell : ParentPlannedCells)
		{
			ParentPhysicalCells.Add(ParentCell.Cell);
			if (!ParentCell.bIsBridgeCell)
			{
				ParentCellsByColumn.FindOrAdd(FIntPoint(ParentCell.Cell.X, ParentCell.Cell.Y)).Add(&ParentCell);
			}
		}

		auto FindMappedParentCell = [&ParentCellsByColumn](
			const FIntPoint& ParentXY,
			const int32 TargetModuleLevel,
			FIntVector& OutParentCell)
		{
			const TArray<const FLayoutPlannedCell*>* ColumnCells = ParentCellsByColumn.Find(ParentXY);
			if (ColumnCells == nullptr || ColumnCells->IsEmpty())
			{
				return false;
			}
			const FLayoutPlannedCell* HighestCell = nullptr;
			for (const FLayoutPlannedCell* Cell : *ColumnCells)
			{
				if (Cell->ModuleLevelIndex == TargetModuleLevel)
				{
					OutParentCell = Cell->Cell;
					return true;
				}
				if (HighestCell == nullptr || Cell->ModuleLevelIndex > HighestCell->ModuleLevelIndex)
				{
					HighestCell = Cell;
				}
			}
			if (HighestCell != nullptr && TargetModuleLevel > HighestCell->ModuleLevelIndex)
			{
				OutParentCell = FIntVector(
					ParentXY.X,
					ParentXY.Y,
					HighestCell->Cell.Z + TargetModuleLevel - HighestCell->ModuleLevelIndex);
				return true;
			}
			return false;
		};

		const bool bUseLocalFlat = ParentRequest.bUseChildLocalFlatFallback
			&& CandidateOffset.Z == 0 && !StageByParentColumn.IsEmpty()
			&& ParentRequest.ContentSetSnapshot.Entries.ContainsByPredicate(
				[SourceEntryId](const FLayoutRegionContentEntrySolveSnapshot& Entry)
				{
					return Entry.EntryId == SourceEntryId
						&& Entry.ChildLevelPlacementPolicy == ELayoutLevelPlacementPolicy::GroundOnly;
				});
		FIntVector RigidOffset = CandidateOffset;
		if (bUseLocalFlat)
		{
			// Only the highest finalized ground plane is considered; lower planes need frozen excavation authority.
			// Lower columns still require real terrain or parent-module support in final proof.
			int32 GroundPlane = MIN_int32;
			for (const FLayoutPlannedCell& Cell : SourceChildPlannedCells)
			{
				if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
				if (Cell.bIsBridgeCell || Cell.Cell.Z != Cell.ModuleLevelIndex)
				{
					OutFailureReason = TEXT("Child-local flat placement requires an unmapped authored child grid.");
					return false;
				}
				if (Cell.ModuleLevelIndex != 0) continue;
				FIntVector Ground;
				if (!FindMappedParentCell(FIntPoint(Cell.Cell.X + CandidateOffset.X,
					Cell.Cell.Y + CandidateOffset.Y), 0, Ground))
				{
					OutFailureReason = TEXT("Child-local flat placement has no observed parent ground column.");
					return false;
				}
				GroundPlane = FMath::Max(GroundPlane, Ground.Z);
			}
			if (GroundPlane == MIN_int32)
			{
				OutFailureReason = TEXT("Child-local flat placement requires an authored ground slice.");
				return false;
			}
			RigidOffset.Z = GroundPlane;
		}

		TArray<FIntVector> RigidParentCells;
		RigidParentCells.Reserve(SourceChildPlannedCells.Num());
		bool bRigidGeometryFits = true;
		for (const FLayoutPlannedCell& ChildCell : SourceChildPlannedCells)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			const FIntVector RigidParentCell = ChildCell.Cell + RigidOffset;
			if (!bUseLocalFlat && bChildSupportsSteppedTerrain && !StageByParentColumn.IsEmpty()
				&& CandidateOffset.Z == 0 && ChildCell.ModuleLevelIndex == 0 && !ChildCell.bIsBridgeCell)
			{
				FIntVector GroundParentCell;
				if (!FindMappedParentCell(FIntPoint(RigidParentCell.X, RigidParentCell.Y), 0, GroundParentCell)
					|| GroundParentCell != RigidParentCell)
				{
					bRigidGeometryFits = false;
					break;
				}
			}
			const TArray<const FLayoutPlannedCell*>* ColumnCells =
				ParentCellsByColumn.Find(FIntPoint(RigidParentCell.X, RigidParentCell.Y));
			int32 HighestColumnZ = MIN_int32;
			if (ColumnCells != nullptr)
			{
				for (const FLayoutPlannedCell* ColumnCell : *ColumnCells)
				{
					HighestColumnZ = FMath::Max(HighestColumnZ, ColumnCell->Cell.Z);
				}
			}
			if (!ParentPhysicalCells.Contains(RigidParentCell)
				&& (ColumnCells == nullptr || RigidParentCell.Z <= HighestColumnZ))
			{
				bRigidGeometryFits = false;
				break;
			}
			RigidParentCells.Add(RigidParentCell);
		}

		if (bUseLocalFlat && !bRigidGeometryFits)
		{
			OutFailureReason = TEXT("Child-local flat plane does not fit the prepared parent volume.");
			return false;
		}
		TArray<FIntVector> ParentMappedCells;
		ParentMappedCells.Reserve(SourceChildPlannedCells.Num());
		if (bRigidGeometryFits)
		{
			ParentMappedCells = MoveTemp(RigidParentCells);
		}
		else
		{
			for (const FLayoutPlannedCell& ChildCell : SourceChildPlannedCells)
			{
				if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
				const FIntPoint ParentXY(
					ChildCell.Cell.X + CandidateOffset.X,
					ChildCell.Cell.Y + CandidateOffset.Y);
				FIntVector ParentCell;
				if (!FindMappedParentCell(
					ParentXY,
					ChildCell.ModuleLevelIndex + CandidateOffset.Z,
					ParentCell))
				{
					OutFailureReason = FString::Printf(
						TEXT("Child cell %s at authored level %d has no finalized parent mapping at X=%d Y=%d level=%d."),
						*ChildCell.Cell.ToString(),
						ChildCell.ModuleLevelIndex,
						ParentXY.X,
						ParentXY.Y,
						ChildCell.ModuleLevelIndex + CandidateOffset.Z);
					return false;
				}
				ParentMappedCells.Add(ParentCell);
			}
		}

		TSet<int32> SelectedStages;
		int32 MinSelectedStage = MAX_int32;
		int32 ChildPhysicalOriginZ = MAX_int32;
		for (int32 CellIndex = 0; CellIndex < SourceChildPlannedCells.Num(); ++CellIndex)
		{
			const FLayoutPlannedCell& SourceCell = SourceChildPlannedCells[CellIndex];
			const FIntVector& ParentCell = ParentMappedCells[CellIndex];
			const FLayoutFrozenTerrainStageCellRecord* const* StageRecord =
				StageByParentColumn.Find(FIntPoint(ParentCell.X, ParentCell.Y));
			if (!StageByParentColumn.IsEmpty() && StageRecord == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Child stage mapping has no frozen parent stage record at X=%d Y=%d."),
					ParentCell.X,
					ParentCell.Y);
				return false;
			}
			const int32 StageIndex = StageRecord != nullptr
				? (*StageRecord)->TerrainStageIndex
				: INDEX_NONE;
			if (StageIndex != INDEX_NONE)
			{
				SelectedStages.Add(StageIndex);
				MinSelectedStage = FMath::Min(MinSelectedStage, StageIndex);
			}
			ChildPhysicalOriginZ = FMath::Min(
				ChildPhysicalOriginZ,
				ParentCell.Z - SourceCell.ModuleLevelIndex);
		}
		if (ChildPhysicalOriginZ == MAX_int32)
		{
			OutFailureReason = TEXT("Child stage mapping produced no mapped cells.");
			return false;
		}

		if (!bRigidGeometryFits && SelectedStages.Num() > 1 && !bChildSupportsSteppedTerrain)
		{
			OutFailureReason = TEXT("Child mapping inherits multiple finalized parent stages, but the child profile does not support Stepped solving.");
			return false;
		}
		OutMapping.StageClass = bUseLocalFlat ? ELayoutChildPlacementStageClass::LocalFlat : StageByParentColumn.IsEmpty()
			? ELayoutChildPlacementStageClass::StageNeutral
			: (bRigidGeometryFits
				? (SelectedStages.Num() <= 1
					? ELayoutChildPlacementStageClass::StageRigid
					: ELayoutChildPlacementStageClass::StageNeutral)
				: (SelectedStages.Num() <= 1
					? ELayoutChildPlacementStageClass::StageRigid
					: ELayoutChildPlacementStageClass::InheritedStage));

		TSet<FIntPoint> AddedChildStageColumns;
		TArray<FString> MappingCellParts;
		for (int32 CellIndex = 0; CellIndex < SourceChildPlannedCells.Num(); ++CellIndex)
		{
			const FLayoutPlannedCell& SourceCell = SourceChildPlannedCells[CellIndex];
			const FIntVector& ParentCell = ParentMappedCells[CellIndex];
			const FIntPoint ParentXY(ParentCell.X, ParentCell.Y);
			const FLayoutFrozenTerrainStageCellRecord* const* StageRecord = StageByParentColumn.Find(ParentXY);
			const int32 StageIndex = !bUseLocalFlat && StageRecord != nullptr ? (*StageRecord)->TerrainStageIndex : INDEX_NONE;
			const FIntVector MappedChildCell(
				SourceCell.Cell.X,
				SourceCell.Cell.Y,
				ParentCell.Z - ChildPhysicalOriginZ);

			FLayoutChildStageMappedCell& CellMapping = OutMapping.Cells.AddDefaulted_GetRef();
			CellMapping.SourceChildCell = SourceCell.Cell;
			CellMapping.MappedChildCell = MappedChildCell;
			CellMapping.ParentCell = ParentCell;
			CellMapping.ModuleLevelIndex = SourceCell.ModuleLevelIndex;
			CellMapping.TerrainStageIndex = StageIndex;

			FLayoutPlannedCell ChildMappedPlan = SourceCell;
			ChildMappedPlan.Cell = MappedChildCell;
			OutMapping.ChildLocalPlannedCells.Add(MoveTemp(ChildMappedPlan));
			FLayoutPlannedCell ParentMappedPlan = SourceCell;
			ParentMappedPlan.Cell = ParentCell;
			OutMapping.ParentTranslatedPlannedCells.Add(MoveTemp(ParentMappedPlan));

			const FIntPoint ChildXY(SourceCell.Cell.X, SourceCell.Cell.Y);
			if (!bUseLocalFlat && StageRecord != nullptr && !AddedChildStageColumns.Contains(ChildXY))
			{
				AddedChildStageColumns.Add(ChildXY);
				FLayoutFrozenTerrainStageCellRecord ChildStage = **StageRecord;
				ChildStage.FootprintCellXY = ChildXY;
				if (MinSelectedStage != MAX_int32)
				{
					ChildStage.TerrainStageIndex -= MinSelectedStage;
				}
				OutMapping.ChildLocalStageMap.Add(MoveTemp(ChildStage));
			}
			MappingCellParts.Add(FString::Printf(
				TEXT("%s>%s>%s:L%d:S%d"),
				*SourceCell.Cell.ToString(),
				*MappedChildCell.ToString(),
				*ParentCell.ToString(),
				SourceCell.ModuleLevelIndex,
				StageIndex));
		}

		if (ChildRequest != nullptr && OutMapping.StageClass == ELayoutChildPlacementStageClass::InheritedStage)
		{
			FLayoutRegionSolveRequest TopologyRequest;
			TopologyRequest.ProfileSnapshot = ChildRequest->ProfileSnapshot;
			TopologyRequest.FootprintSize = ChildFootprintSize;
			const int32 AuthoredCellCount = OutMapping.Cells.Num();
			if (!FLayoutContractModeAdapter::TryCompleteMappedSteppedTopology(
				TopologyRequest, OutMapping.ChildLocalPlannedCells, OutFailureReason)) return false;
			for (int32 Index = 0; Index < OutMapping.ChildLocalPlannedCells.Num(); ++Index)
			{
				const FLayoutPlannedCell& Planned = OutMapping.ChildLocalPlannedCells[Index];
				const FIntVector ParentCell = Planned.Cell + FIntVector(CandidateOffset.X, CandidateOffset.Y, ChildPhysicalOriginZ);
				FLayoutPlannedCell ParentPlanned = Planned;
				ParentPlanned.Cell = ParentCell;
				if (Index < AuthoredCellCount)
				{
					OutMapping.ParentTranslatedPlannedCells[Index] = ParentPlanned;
					continue;
				}
				FLayoutChildStageMappedCell& Mapping = OutMapping.Cells.AddDefaulted_GetRef();
				Mapping.bGeneratedByChildTopology = true;
				Mapping.SourceChildCell = FIntVector(Planned.Cell.X, Planned.Cell.Y, -1 - Planned.Cell.Z);
				Mapping.MappedChildCell = Planned.Cell;
				Mapping.ParentCell = ParentCell;
				Mapping.ModuleLevelIndex = Planned.ModuleLevelIndex;
				const auto* Stage = StageByParentColumn.FindRef(FIntPoint(ParentCell.X, ParentCell.Y));
				Mapping.TerrainStageIndex = Stage != nullptr ? Stage->TerrainStageIndex : INDEX_NONE;
				OutMapping.ParentTranslatedPlannedCells.Add(MoveTemp(ParentPlanned));
			}
			// Includes generated-cell occupancy, authored levels, seam/landing masks and offers.
			MappingCellParts.Add(FLayoutContractPipeline::BuildStageMapId(OutMapping.ChildLocalPlannedCells).ToString());
		}

		// Cells are finalized above and stay immutable for these call-local lookups.
		// Preserve FindByPredicate's first-match semantics, including stacked/generated columns.
		TMap<FIntPoint, const FLayoutChildStageMappedCell*> FirstMappedCellBySourceColumn;
		TMap<FIntVector, const FLayoutChildStageMappedCell*> FirstMappedCellByParentCell;
		FirstMappedCellBySourceColumn.Reserve(OutMapping.Cells.Num());
		FirstMappedCellByParentCell.Reserve(OutMapping.Cells.Num());
		for (const FLayoutChildStageMappedCell& Cell : OutMapping.Cells)
		{
			const FIntPoint SourceXY(Cell.SourceChildCell.X, Cell.SourceChildCell.Y);
			if (!FirstMappedCellBySourceColumn.Contains(SourceXY))
				FirstMappedCellBySourceColumn.Add(SourceXY, &Cell);
			if (!FirstMappedCellByParentCell.Contains(Cell.ParentCell))
				FirstMappedCellByParentCell.Add(Cell.ParentCell, &Cell);
		}

		for (const FLayoutChildStageMappedCell& Cell : OutMapping.Cells)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			if (Cell.bGeneratedByChildTopology) continue;
			const FIntPoint ChildXY(Cell.SourceChildCell.X, Cell.SourceChildCell.Y);
			for (const FIntPoint Delta : {FIntPoint(1, 0), FIntPoint(0, 1)})
			{
				const FLayoutChildStageMappedCell* Neighbor = FirstMappedCellBySourceColumn.FindRef(ChildXY + Delta);
				if (Neighbor == nullptr
					|| Cell.TerrainStageIndex == INDEX_NONE
					|| Neighbor->TerrainStageIndex == INDEX_NONE
					|| Cell.TerrainStageIndex == Neighbor->TerrainStageIndex)
				{
					continue;
				}
				OutMapping.CrossedFrontierIds.AddUnique(FLayoutId(*FString::Printf(
					TEXT("StageFrontier.%d.%d.%d.%d.%d.%d"),
					ChildXY.X,
					ChildXY.Y,
					(ChildXY + Delta).X,
					(ChildXY + Delta).Y,
					Cell.TerrainStageIndex,
					Neighbor->TerrainStageIndex)));
			}
		}
		OutMapping.CrossedFrontierIds.Sort();
		OutMapping.ChildLocalStageMap.Sort([](const FLayoutFrozenTerrainStageCellRecord& Left, const FLayoutFrozenTerrainStageCellRecord& Right)
		{
			return Left.FootprintCellXY.X != Right.FootprintCellXY.X
				? Left.FootprintCellXY.X < Right.FootprintCellXY.X
				: Left.FootprintCellXY.Y < Right.FootprintCellXY.Y;
		});
		MappingCellParts.Sort();
		OutMapping.MappingId = FLayoutId(*FString::Printf(
			TEXT("ChildStage.%s.Yaw%d.Offset%s.Class%d.%08x"),
			*SourceEntryId.ToString(),
			YawRotationSteps,
			*CandidateOffset.ToString(),
			static_cast<int32>(OutMapping.StageClass),
			FCrc::StrCrc32(*FString::Join(MappingCellParts, TEXT("|")))));

		auto FindChildCellByParentCell = [&FirstMappedCellByParentCell](const FIntVector& ParentCell, FIntVector& OutChildCell)
		{
			if (const FLayoutChildStageMappedCell* MappedCell = FirstMappedCellByParentCell.FindRef(ParentCell))
			{
				OutChildCell = MappedCell->MappedChildCell;
				return true;
			}
			return false;
		};

		OutMapping.ChildLocalTerrainContract = ParentRequest.PrecomputedFrozenTerrainContract;
		FLayoutFrozenTerrainContract& ChildTerrain = OutMapping.ChildLocalTerrainContract;
		ChildTerrain.ContractId = OutMapping.MappingId;
		ChildTerrain.FootprintMinBlockWorldPos =
			ParentRequest.PrecomputedFrozenTerrainContract.FootprintMinBlockWorldPos
			+ FIntVector(
				CandidateOffset.X * ChildTerrain.SharedCellSizeInBlocks.X,
				CandidateOffset.Y * ChildTerrain.SharedCellSizeInBlocks.Y,
				ChildPhysicalOriginZ * ChildTerrain.SharedCellSizeInBlocks.Z);
		ChildTerrain.FootprintSizeInCells = ChildFootprintSize;
		ChildTerrain.ActiveCells.Reset();
		ChildTerrain.CellContracts.Reset();
		ChildTerrain.StageMap = OutMapping.ChildLocalStageMap;
		ChildTerrain.TerrainBackedNeighborFaces.Reset();
		ChildTerrain.ReservedOpenTerrainReservations.Reset();
		ChildTerrain.TerrainWrites.Reset();
		ChildTerrain.RemovedCells.Reset();
		ChildTerrain.ProtectedOccupiedCells.Reset();
		TMap<FIntVector, const FLayoutTerrainCellContractRecord*> FirstParentContractByCell;
		FirstParentContractByCell.Reserve(ParentRequest.PrecomputedFrozenTerrainContract.CellContracts.Num());
		for (const FLayoutTerrainCellContractRecord& Record : ParentRequest.PrecomputedFrozenTerrainContract.CellContracts)
		{
			if (!FirstParentContractByCell.Contains(Record.Cell))
				FirstParentContractByCell.Add(Record.Cell, &Record);
		}
		for (const FLayoutChildStageMappedCell& MappedCell : OutMapping.Cells)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			ChildTerrain.ActiveCells.Add({MappedCell.MappedChildCell});
			const FLayoutTerrainCellContractRecord* ParentCellContract =
				FirstParentContractByCell.FindRef(MappedCell.ParentCell);
			FLayoutTerrainCellContractRecord ChildCellContract = ParentCellContract != nullptr
				? *ParentCellContract
				: FLayoutTerrainCellContractRecord{};
			ChildCellContract.Cell = MappedCell.MappedChildCell;
			ChildCellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
			ChildTerrain.CellContracts.Add(MoveTemp(ChildCellContract));
		}
		for (const FLayoutTerrainBackedNeighborFaceRecord& ParentFace :
			ParentRequest.PrecomputedFrozenTerrainContract.TerrainBackedNeighborFaces)
		{
			FIntVector ChildCell;
			if (!FindChildCellByParentCell(ParentFace.Cell, ChildCell))
			{
				continue;
			}
			FLayoutTerrainBackedNeighborFaceRecord ChildFace = ParentFace;
			ChildFace.Cell = ChildCell;
			ChildFace.NeighborCell =
				ChildCell + FLayoutDirectionUtils::ToCellDelta(ParentFace.FaceDirection);
			ChildTerrain.TerrainBackedNeighborFaces.Add(MoveTemp(ChildFace));
		}
		for (const FLayoutCellReservationRecord& ParentReservation :
			ParentRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations)
		{
			FIntVector ChildCell;
			if (FindChildCellByParentCell(ParentReservation.Cell, ChildCell))
			{
				FLayoutCellReservationRecord ChildReservation = ParentReservation;
				ChildReservation.Cell = ChildCell;
				ChildTerrain.ReservedOpenTerrainReservations.Add(MoveTemp(ChildReservation));
			}
		}
		for (const FIntVector& ParentProtectedCell :
			ParentRequest.PrecomputedFrozenTerrainContract.ProtectedOccupiedCells)
		{
			FIntVector ChildCell;
			if (FindChildCellByParentCell(ParentProtectedCell, ChildCell))
			{
				ChildTerrain.ProtectedOccupiedCells.AddUnique(ChildCell);
			}
		}
		for (const FIntVector& ParentRemovedCell :
			ParentRequest.PrecomputedFrozenTerrainContract.RemovedCells)
		{
			const int32 ChildX = ParentRemovedCell.X - CandidateOffset.X;
			const int32 ChildY = ParentRemovedCell.Y - CandidateOffset.Y;
			const int32 ChildZ = ParentRemovedCell.Z - ChildPhysicalOriginZ;
			if (ChildX >= 0
				&& ChildY >= 0
				&& ChildX < ChildTerrain.FootprintSizeInCells.X
				&& ChildY < ChildTerrain.FootprintSizeInCells.Y
				&& ChildZ >= 0)
			{
				ChildTerrain.RemovedCells.AddUnique(FIntVector(ChildX, ChildY, ChildZ));
			}
		}

		const FLayoutSteppedTerrainSupportMap& ParentSupportMap =
			!ParentRequest.PrecomputedSteppedTerrainSupportMap.SupportSamples.IsEmpty()
				? ParentRequest.PrecomputedSteppedTerrainSupportMap
				: ParentRequest.SteppedTerrainSupportMap;
		FLayoutSteppedTerrainSupportMap& ChildSupportMap =
			OutMapping.ChildLocalSteppedTerrainSupportMap;
		ChildSupportMap.SharedCellHeightInBlocks = ParentSupportMap.SharedCellHeightInBlocks;
		for (const FLayoutSteppedTerrainSupportSample& ParentSample : ParentSupportMap.SupportSamples)
		{
			const int32 ChildX = ParentSample.LocalCell.X - CandidateOffset.X;
			const int32 ChildY = ParentSample.LocalCell.Y - CandidateOffset.Y;
			if (ChildX < 0
				|| ChildY < 0
				|| ChildX >= ChildTerrain.FootprintSizeInCells.X
				|| ChildY >= ChildTerrain.FootprintSizeInCells.Y)
			{
				continue;
			}
			FLayoutSteppedTerrainSupportSample ChildSample = ParentSample;
			ChildSample.LocalCell = FIntVector(ChildX, ChildY, 0);
			ChildSupportMap.SupportSamples.Add(MoveTemp(ChildSample));
		}
		for (const FLayoutSteppedTerrainAdjacencyStep& ParentStep : ParentSupportMap.AdjacencySteps)
		{
			const FIntVector ChildFrom(
				ParentStep.FromCell.X - CandidateOffset.X,
				ParentStep.FromCell.Y - CandidateOffset.Y,
				0);
			const FIntVector ChildTo(
				ParentStep.ToCell.X - CandidateOffset.X,
				ParentStep.ToCell.Y - CandidateOffset.Y,
				0);
			if (ChildFrom.X < 0
				|| ChildFrom.Y < 0
				|| ChildTo.X < 0
				|| ChildTo.Y < 0
				|| ChildFrom.X >= ChildTerrain.FootprintSizeInCells.X
				|| ChildFrom.Y >= ChildTerrain.FootprintSizeInCells.Y
				|| ChildTo.X >= ChildTerrain.FootprintSizeInCells.X
				|| ChildTo.Y >= ChildTerrain.FootprintSizeInCells.Y)
			{
				continue;
			}
			FLayoutSteppedTerrainAdjacencyStep ChildStep = ParentStep;
			ChildStep.FromCell = ChildFrom;
			ChildStep.ToCell = ChildTo;
			ChildSupportMap.AdjacencySteps.Add(MoveTemp(ChildStep));
			ChildSupportMap.MaximumObservedNeighborHeightDelta = FMath::Max(
				ChildSupportMap.MaximumObservedNeighborHeightDelta,
				ParentStep.StepHeightBlocks);
			ChildSupportMap.MaximumObservedSnappedLevelDelta = FMath::Max(
				ChildSupportMap.MaximumObservedSnappedLevelDelta,
				ParentStep.SnappedLevelDelta);
		}

		if (bUseLocalFlat)
		{
			ChildSupportMap = FLayoutSteppedTerrainSupportMap();
		}
		// Parent stair alternatives are not child placements. Child-owned alternatives
		// are rebuilt from its catalog after this mapped candidate is selected.
		return OutMapping.IsValid();
	}

	struct FCertifiedBoundaryCellOption
	{
		FString StableKey;
		bool bRequiresFutureJunction = false;
		FLayoutCellCandidateDomainRestriction ChildRestriction;
		TMap<FIntVector, FLayoutCellCandidateDomainRestriction> ParentRestrictionsByCell;
		TArray<FLayoutSolveBoundaryPoint> ChildIncomingBoundaryPoints;
		TArray<FSharedParentChildFace> CertifiedSharedFaces;
		FLayoutModuleFaceRules ChildWorldFaceRules;
	};

	/** A branch requires two incident shared axes at an actual parent perimeter Edge, never an arbitrary child attachment. */
	uint8 GetParentPerimeterJunctionFaces(const FLayoutRegionSolveRequest& ParentRequest,
		const FLayoutPlannedCell& ParentCell, const FSharedParentChildFace& Face,
		const TArray<FSharedParentChildFace>& SharedFaces)
	{
		if (ParentCell.PlacementZone != ELayoutPlacementZone::Edge
			|| !LayoutRegionScheduleSolverFacade::HasParentPerimeterContinuation(ParentRequest, ParentCell.Cell)) return 0;
		uint8 Mask = 0;
		for (const auto& Other : SharedFaces)
			if (Other.ParentCell == Face.ParentCell && Other.ChildLocalCell == Face.ChildLocalCell
				&& Other.InterfaceFamily == Face.InterfaceFamily)
				Mask |= LayoutFaceDirectionMask(Other.FaceDirection);
		return (Mask & (LayoutFaceDirectionMask(ELayoutFaceDirection::PosX) | LayoutFaceDirectionMask(ELayoutFaceDirection::NegX))) != 0
			&& (Mask & (LayoutFaceDirectionMask(ELayoutFaceDirection::PosY) | LayoutFaceDirectionMask(ELayoutFaceDirection::NegY))) != 0 ? Mask : 0;
	}

	/** Junction ownership requires one provider sealing both branches; a straight passive counterpart may still accept it. */
	bool CanOwnPerimeterJunction(const FLayoutModuleSolveSnapshot& Module, const int32 Yaw,
		const FGameplayTag& Family, const uint8 Faces)
	{
		if (Faces == 0) return true;
		if (!Module.SeamProviderIntents.ContainsByPredicate([&](const auto& Intent)
		{
			return Intent.bCanOwnSeam && Intent.InterfaceFamily == Family
				&& Intent.JunctionUsage != ELayoutSeamJunctionUsage::NonJunctionOnly;
		})) return false;
		for (int32 Index = 0; Index < 4; ++Index)
		{
			const auto Direction = static_cast<ELayoutFaceDirection>(Index);
			if (!(Faces & LayoutFaceDirectionMask(Direction))) continue;
			if (!Module.DerivedSpanOffers.ContainsByPredicate([&](const auto& Offer)
			{
				return Offer.bSealsBoundary && FLayoutDirectionUtils::RotateYaw(Offer.FaceDirection, Yaw) == Direction
					&& LayoutPlacementOccupancy::ProjectLocalCellToWorld(FIntVector::ZeroValue,
						Offer.LocalCell, Module.BoundsCells, Yaw) == FIntVector::ZeroValue;
			})) return false;
		}
		return true;
	}

	/** Corner offers outside their ordinary zone need actual branch ownership or deferred, contract-equivalent retention; levels remain authored. */
	bool DoesSnapshotMatchBoundaryPlannedCell(
		const FLayoutModuleSolveSnapshot& Module,
		const FLayoutPlannedCell& PlannedCell,
		const int32 TopModuleLevel,
		const bool bAllowJunctionCorner = false)
	{
		if (!Module.SupportsRootIntent(PlannedCell.Intent))
		{
			return false;
		}

		const bool bZoneMatches = Module.PlacementZone == ELayoutPlacementZone::Any
			|| (Module.PlacementZone == ELayoutPlacementZone::Perimeter
				&& (PlannedCell.PlacementZone == ELayoutPlacementZone::Edge
					|| PlannedCell.PlacementZone == ELayoutPlacementZone::Corner))
			|| Module.PlacementZone == PlannedCell.PlacementZone
			|| (Module.PlacementZone == ELayoutPlacementZone::Core
				&& PlannedCell.Intent == ELayoutCellIntent::Core)
			|| (bAllowJunctionCorner && PlannedCell.PlacementZone == ELayoutPlacementZone::Edge
				&& Module.PlacementZone == ELayoutPlacementZone::Corner);
		if (!bZoneMatches)
		{
			return false;
		}

		// Legacy unshifted planned carriers use INDEX_NONE to mean Cell.Z.
		const int32 ModuleLevel = PlannedCell.ModuleLevelIndex == INDEX_NONE
			? PlannedCell.Cell.Z
			: PlannedCell.ModuleLevelIndex;
		switch (Module.LevelPlacementPolicy)
		{
		case ELayoutLevelPlacementPolicy::GroundOnly:
			return ModuleLevel == 0;
		case ELayoutLevelPlacementPolicy::SpecificLevel:
			return ModuleLevel == Module.SpecificLevel;
		case ELayoutLevelPlacementPolicy::TopLevelOnly:
			return ModuleLevel == TopModuleLevel;
		case ELayoutLevelPlacementPolicy::AboveGroundLevel:
			return ModuleLevel > 0;
		case ELayoutLevelPlacementPolicy::BelowTopLevel:
			return ModuleLevel < TopModuleLevel;
		case ELayoutLevelPlacementPolicy::AnyLevel:
		default:
			return true;
		}
	}

	bool DoesFaceAllowCertifiedEmptyNeighbor(const FLayoutFaceRule& FaceRule)
	{
		if (FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceInterior
			|| FaceRule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam)
		{
			return false;
		}
		return FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrWalkableFilledNeighbor
			|| FaceRule.OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
	}

	/** Returns whether one parent cell is allowed to settle as unclaimed preserved terrain. */
	bool IsPreservedTerrainSparseCandidateCell(
		const FLayoutProfileSolveSnapshot& ParentProfileSnapshot,
		const FLayoutPlannedCell& ParentCell,
		const int32 ParentTopModuleLevel)
	{
		const int32 ModuleLevel = ParentCell.ModuleLevelIndex == INDEX_NONE
			? ParentCell.Cell.Z
			: ParentCell.ModuleLevelIndex;
		for (const FLayoutSparsePlacementRuleSolveSnapshot& Rule :
			ParentProfileSnapshot.SparsePlacementRules)
		{
			if (Rule.RuleKind != ELayoutSparsePlacementRuleKind::PreserveTerrain
				&& Rule.CandidateSource != ELayoutSparseCandidateSource::PreserveSupportedTerrain)
			{
				continue;
			}
			const bool bZoneMatches = Rule.PlacementZone == ELayoutPlacementZone::Any
				|| Rule.PlacementZone == ParentCell.PlacementZone
				|| (Rule.PlacementZone == ELayoutPlacementZone::Perimeter
					&& (ParentCell.PlacementZone == ELayoutPlacementZone::Edge
						|| ParentCell.PlacementZone == ELayoutPlacementZone::Corner));
			if (!bZoneMatches)
			{
				continue;
			}
			switch (Rule.LevelPlacementPolicy)
			{
			case ELayoutLevelPlacementPolicy::GroundOnly:
				if (ModuleLevel == 0) return true;
				break;
			case ELayoutLevelPlacementPolicy::SpecificLevel:
				if (ModuleLevel == Rule.SpecificLevel) return true;
				break;
			case ELayoutLevelPlacementPolicy::TopLevelOnly:
				if (ModuleLevel == ParentTopModuleLevel) return true;
				break;
			case ELayoutLevelPlacementPolicy::AboveGroundLevel:
				if (ModuleLevel > 0) return true;
				break;
			case ELayoutLevelPlacementPolicy::BelowTopLevel:
				if (ModuleLevel < ParentTopModuleLevel) return true;
				break;
			case ELayoutLevelPlacementPolicy::AnyLevel:
			default:
				return true;
			}
		}
		return false;
	}

	bool DoesBoundaryCandidateSupportEntryCapability(
		const FLayoutModuleSolveSnapshot& Module,
		const int32 YawRotationSteps,
		const FLayoutChildCapabilityEndpoint& Capability)
	{
		FLayoutFaceRule FaceRule;
		return TryGetSnapshotWorldFaceRule(
				Module,
				Capability.FaceDirection,
				YawRotationSteps,
				FaceRule)
			&& FaceRule.GetEffectiveConnectionTags().HasTagExact(
				Capability.ConnectionTag)
			&& FaceRule.GetEffectiveAllowedConnectionTags().HasAnyExact(
				Capability.AllowedConnectionTags)
			&& (Capability.TraversalChannels.IsEmpty()
				|| FaceRule.ConnectedTraversalChannels.HasAnyExact(
					Capability.TraversalChannels));
	}

	bool AreBoundaryFaceRulesReciprocallyCompatible(
		const FLayoutFaceRule& ChildFaceRule,
		const int32 ChildYawRotationSteps,
		const FLayoutFaceRule& ParentFaceRule,
		const int32 ParentYawRotationSteps)
	{
		if (!IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(ChildFaceRule)
			|| !IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(ParentFaceRule)
			|| !AreConnectionTagsCompatible(ChildFaceRule, ParentFaceRule)
			|| !AreConnectionTagsCompatible(ParentFaceRule, ChildFaceRule))
		{
			return false;
		}
		if (!ChildFaceRule.ConnectedTraversalChannels.IsEmpty()
			&& !ParentFaceRule.ConnectedTraversalChannels.HasAnyExact(
				ChildFaceRule.ConnectedTraversalChannels))
		{
			return false;
		}
		return (!ChildFaceRule.bRequireMatchingYawWithFilledNeighbor
				&& !ParentFaceRule.bRequireMatchingYawWithFilledNeighbor)
			|| (((ChildYawRotationSteps % 4) + 4) % 4
				== ((ParentYawRotationSteps % 4) + 4) % 4);
	}

	/** Describes one boundary face compactly for bounded child-placement rejection diagnostics. */
	FString DescribeBoundaryFaceRule(const FLayoutFaceRule& Rule)
	{
		return FString::Printf(
			TEXT("boundary=%s occupancy=%s connections={%s} allowed={%s} traversal={%s}"),
			*StaticEnum<ELayoutFaceBoundaryRequirement>()->GetNameStringByValue(
				static_cast<int64>(Rule.BoundaryRequirement)),
			*StaticEnum<ELayoutFaceOccupancyPolicy>()->GetNameStringByValue(
				static_cast<int64>(Rule.OccupancyPolicy)),
			*Rule.GetEffectiveConnectionTags().ToStringSimple(),
			*Rule.GetEffectiveAllowedConnectionTags().ToStringSimple(),
			*Rule.ConnectedTraversalChannels.ToStringSimple());
	}

	struct FExactSharedSeamCandidateEvidence
	{
		bool bCanOwn = false;
		bool bCanAccept = false;
		FLayoutId EvidenceId;
		FLayoutFaceRule FaceRule;
	};

	/** Resolves seam evidence owned by one exact module/yaw candidate at its root cell. */
	bool TryGetExactSharedSeamCandidateEvidence(
		const FLayoutModuleSolveSnapshot& Module,
		const int32 YawRotationSteps,
		const ELayoutFaceDirection FaceDirection,
		const FGameplayTag& InterfaceFamily,
		FExactSharedSeamCandidateEvidence& OutEvidence)
	{
		OutEvidence = FExactSharedSeamCandidateEvidence{};
		if (!InterfaceFamily.IsValid()
			|| !TryGetSnapshotWorldFaceRule(
				Module,
				FaceDirection,
				YawRotationSteps,
				OutEvidence.FaceRule))
		{
			return false;
		}

		TArray<FString> EvidenceParts;
		for (const FLayoutSeamProviderIntent& SeamIntent : Module.SeamProviderIntents)
		{
			if (SeamIntent.InterfaceFamily != InterfaceFamily)
			{
				continue;
			}
			for (const FLayoutDerivedSpanOffer& SpanOffer : Module.DerivedSpanOffers)
			{
				if (FLayoutDirectionUtils::RotateYaw(
						SpanOffer.FaceDirection,
						YawRotationSteps) != FaceDirection
					|| LayoutPlacementOccupancy::ProjectLocalCellToWorld(
						FIntVector::ZeroValue,
						SpanOffer.LocalCell,
						Module.BoundsCells,
						YawRotationSteps) != FIntVector::ZeroValue)
				{
					continue;
				}

				const FGameplayTagContainer FaceConnectionTags =
					OutEvidence.FaceRule.GetEffectiveConnectionTags();
				const FGameplayTagContainer FaceAllowedTags =
					OutEvidence.FaceRule.GetEffectiveAllowedConnectionTags();
				if ((SpanOffer.ConnectionTag.IsValid()
						&& !FaceConnectionTags.HasTagExact(SpanOffer.ConnectionTag))
					|| (!SpanOffer.AllowedConnectionTags.IsEmpty()
						&& !FaceAllowedTags.HasAnyExact(
							SpanOffer.AllowedConnectionTags)))
				{
					continue;
				}

				OutEvidence.bCanOwn |= SeamIntent.bCanOwnSeam;
				OutEvidence.bCanAccept |= SeamIntent.bCanAcceptSeam;
				EvidenceParts.Add(FString::Printf(
					TEXT("%s:%s:Usage%d"),
					*SeamIntent.SeamIntentId.ToString(),
					*SpanOffer.SpanOfferId.ToString(),
					static_cast<int32>(SeamIntent.JunctionUsage)));
			}
		}
		if (EvidenceParts.IsEmpty())
		{
			return false;
		}

		EvidenceParts.Sort();
		OutEvidence.EvidenceId = FLayoutId(*FString::Printf(
			TEXT("SeamCandidate.%08X"),
			FCrc::StrCrc32(*FString::Printf(
				TEXT("%s|Yaw%d|Face%d|%s|Own%d|Accept%d|%s"),
				*Module.SnapshotId.ToString(),
				NormalizeSolverYawRotationSteps(YawRotationSteps),
				static_cast<int32>(FaceDirection),
				*InterfaceFamily.ToString(),
				OutEvidence.bCanOwn ? 1 : 0,
				OutEvidence.bCanAccept ? 1 : 0,
				*FString::Join(EvidenceParts, TEXT("|"))))));
		return !OutEvidence.EvidenceId.IsNone();
	}

	bool DoesExactSharedSeamCandidateOwn(
		const FLayoutModuleSolveSnapshot& Module,
		const int32 YawRotationSteps,
		const ELayoutFaceDirection FaceDirection,
		const FGameplayTag& InterfaceFamily)
	{
		FExactSharedSeamCandidateEvidence Evidence;
		return TryGetExactSharedSeamCandidateEvidence(
			Module,
			YawRotationSteps,
			FaceDirection,
			InterfaceFamily,
			Evidence)
			&& Evidence.bCanOwn;
	}

	/** Requires exact face, occupancy, tag, yaw, and ownership compatibility for one pair. */
	bool TryCertifyExactSharedSeamCandidatePairForFaces(
		const FLayoutModuleSolveSnapshot& ParentModule,
		const int32 ParentYawRotationSteps,
		const ELayoutFaceDirection ParentFaceDirection,
		const bool bParentOwnershipAllowed,
		const FLayoutModuleSolveSnapshot& ChildModule,
		const int32 ChildYawRotationSteps,
		const ELayoutFaceDirection ChildFaceDirection,
		const bool bChildOwnershipAllowed,
		const FGameplayTag& InterfaceFamily,
		bool& bOutParentCanOwn,
		bool& bOutChildCanOwn,
		FLayoutId& OutParentEvidenceId,
		FLayoutId& OutChildEvidenceId,
		FLayoutId& OutWitnessId)
	{
		bOutParentCanOwn = false;
		bOutChildCanOwn = false;
		OutParentEvidenceId = NAME_None;
		OutChildEvidenceId = NAME_None;
		OutWitnessId = NAME_None;

		FExactSharedSeamCandidateEvidence ParentEvidence;
		FExactSharedSeamCandidateEvidence ChildEvidence;
		if (!TryGetExactSharedSeamCandidateEvidence(
				ParentModule,
				ParentYawRotationSteps,
				ParentFaceDirection,
				InterfaceFamily,
				ParentEvidence)
			|| !TryGetExactSharedSeamCandidateEvidence(
				ChildModule,
				ChildYawRotationSteps,
				ChildFaceDirection,
				InterfaceFamily,
				ChildEvidence))
		{
			return false;
		}

		const FGameplayTagContainer ParentConnectionTags =
			ParentEvidence.FaceRule.GetEffectiveConnectionTags();
		const FGameplayTagContainer ChildConnectionTags =
			ChildEvidence.FaceRule.GetEffectiveConnectionTags();
		const bool bTraversalCompatible =
			(ParentEvidence.FaceRule.ConnectedTraversalChannels.IsEmpty()
				&& ChildEvidence.FaceRule.ConnectedTraversalChannels.IsEmpty())
			|| ParentEvidence.FaceRule.ConnectedTraversalChannels.HasAnyExact(
				ChildEvidence.FaceRule.ConnectedTraversalChannels);
		const bool bSharesFilledOccupancy =
			IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(
				ParentEvidence.FaceRule)
			&& IsFaceCompatibleWithFilledNeighborForRecursiveNegotiation(
				ChildEvidence.FaceRule);
		const bool bSharesEmptyOccupancy =
			DoesFaceAllowCertifiedEmptyNeighbor(ParentEvidence.FaceRule)
			&& DoesFaceAllowCertifiedEmptyNeighbor(ChildEvidence.FaceRule);
		// Shared seam records describe two owners of one physical face, not filled neighbors.
		// Each face's allowed tags already validate its own authored span evidence above.
		if (ParentConnectionTags.IsEmpty()
			|| ChildConnectionTags.IsEmpty()
			|| !ParentConnectionTags.HasAnyExact(ChildConnectionTags)
			|| !bTraversalCompatible
			|| (!bSharesFilledOccupancy && !bSharesEmptyOccupancy)
			|| (DoSolverFaceRulesRequireMatchingYaw(
					ParentEvidence.FaceRule,
					ChildEvidence.FaceRule)
				&& NormalizeSolverYawRotationSteps(ParentYawRotationSteps)
					!= NormalizeSolverYawRotationSteps(ChildYawRotationSteps)))
		{
			return false;
		}

		bOutParentCanOwn = bParentOwnershipAllowed
			&& ParentEvidence.bCanOwn
			&& ChildEvidence.bCanAccept;
		bOutChildCanOwn = bChildOwnershipAllowed
			&& ChildEvidence.bCanOwn
			&& ParentEvidence.bCanAccept;
		if (!bOutParentCanOwn && !bOutChildCanOwn)
		{
			return false;
		}

		OutParentEvidenceId = ParentEvidence.EvidenceId;
		OutChildEvidenceId = ChildEvidence.EvidenceId;
		OutWitnessId = FLayoutId(*FString::Printf(
			TEXT("SeamPair.%08X"),
			FCrc::StrCrc32(*FString::Printf(
				TEXT("%s|Yaw%d|Face%d|Own%d|%s|Yaw%d|Face%d|Own%d|%s|%s|%s"),
				*ParentModule.SnapshotId.ToString(),
				NormalizeSolverYawRotationSteps(ParentYawRotationSteps),
				static_cast<int32>(ParentFaceDirection),
				bParentOwnershipAllowed ? 1 : 0,
				*ChildModule.SnapshotId.ToString(),
				NormalizeSolverYawRotationSteps(ChildYawRotationSteps),
				static_cast<int32>(ChildFaceDirection),
				bChildOwnershipAllowed ? 1 : 0,
				*InterfaceFamily.ToString(),
				*ParentEvidence.EvidenceId.ToString(),
				*ChildEvidence.EvidenceId.ToString()))));
		return true;
	}

	/** Certifies shared-face ownership; an exterior-incompatible parent yaw may remain a passive acceptor. */
	bool TryCertifyExactSharedSeamCandidatePair(
		const FLayoutModuleSolveSnapshot& ParentModule,
		const int32 ParentYawRotationSteps,
		const FLayoutModuleSolveSnapshot& ChildModule,
		const int32 ChildYawRotationSteps,
		const ELayoutFaceDirection FaceDirection,
		const FGameplayTag& InterfaceFamily,
		const bool bChildEntry,
		bool& bOutParentCanOwn,
		bool& bOutChildCanOwn,
		FLayoutId& OutParentEvidenceId,
		FLayoutId& OutChildEvidenceId,
		FLayoutId& OutWitnessId,
		const FLayoutRegionSolveRequest* ParentRequest = nullptr,
		const FIntVector& ParentCell = FIntVector::ZeroValue)
	{
		bool bParentOwnershipAllowed = !bChildEntry;
		if (ParentRequest != nullptr)
		{
			// Matching the shared face alone cannot certify the rest of this yaw.
			// An Entry/host retry cannot create owning-region interior outside the footprint.
			const uint8 Exterior = GetOwningPerimeterFaceMask(ParentCell, ParentRequest->FootprintSize);
			for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
			{
				if (!(Exterior & LayoutFaceDirectionMask(Direction))) continue;
				FLayoutFaceRule Rule;
				if (TryGetSnapshotWorldFaceRule(ParentModule, Direction, ParentYawRotationSteps, Rule)
					&& Rule.BoundaryRequirement == ELayoutFaceBoundaryRequirement::MustFaceInterior)
				{
					// The same candidate may remain passive when the child supplies the physical wall.
					bParentOwnershipAllowed = false;
				}
			}
		}
		return TryCertifyExactSharedSeamCandidatePairForFaces(
			ParentModule,
			ParentYawRotationSteps,
			FaceDirection,
			bParentOwnershipAllowed,
			ChildModule,
			ChildYawRotationSteps,
			FaceDirection,
			true,
			InterfaceFamily,
			bOutParentCanOwn,
			bOutChildCanOwn,
			OutParentEvidenceId,
			OutChildEvidenceId,
			OutWitnessId);
	}

	void IntersectCandidateRestriction(
		FLayoutCellCandidateDomainRestriction& InOutRestriction,
		const FLayoutCellCandidateDomainRestriction& IncomingRestriction)
	{
		InOutRestriction.AllowedCandidates.RemoveAll(
			[&IncomingRestriction](const FLayoutCandidateVariantIdentity& ExistingCandidate)
			{
				return !IncomingRestriction.AllowedCandidates.ContainsByPredicate(
					[&ExistingCandidate](const FLayoutCandidateVariantIdentity& IncomingCandidate)
					{
						return IncomingCandidate.ModuleSnapshotId == ExistingCandidate.ModuleSnapshotId
							&& IncomingCandidate.YawRotationSteps == ExistingCandidate.YawRotationSteps;
					});
			});
	}

	bool TryBuildCertifiedBoundaryOption(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TMap<FIntVector, const FLayoutPlannedCell*>& ParentPlannedCellByCell,
		const TMap<FIntPoint, int32>& ParentTopLevelByXY,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const TMap<FIntVector, const FLayoutChildStageMappedCell*>& StageCellBySourceCell,
		const TSet<FIntVector>& ParentChildCells,
		const TSet<FIntVector>& MappedChildCells,
		const TArray<FSharedParentChildFace>& PotentialSharedFaces,
		const FLayoutPlannedCell& ChildPlannedCell,
		const int32 ChildTopModuleLevel,
		const FLayoutModuleSolveSnapshot& ChildModule,
		const int32 ChildYawRotationSteps,
		FCertifiedBoundaryCellOption& OutOption,
		FString* const OutFailureReason = nullptr)
	{
		OutOption = FCertifiedBoundaryCellOption{};
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		const auto Reject = [&](const FString& Reason)
		{
			if (OutFailureReason != nullptr && OutFailureReason->IsEmpty())
			{
				const auto* DiagnosticMapping = StageCellBySourceCell.FindRef(ChildPlannedCell.Cell);
				const FIntVector DiagnosticCell = DiagnosticMapping != nullptr && DiagnosticMapping->bGeneratedByChildTopology
					? DiagnosticMapping->MappedChildCell : ChildPlannedCell.Cell;
				*OutFailureReason = FString::Printf(
					TEXT("childCell=%s intent=%s module=%s yaw=%d: %s"),
					*DiagnosticCell.ToString(),
					*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(
						static_cast<int64>(ChildPlannedCell.Intent)),
					*ChildModule.DebugName.ToString(),
					ChildYawRotationSteps,
					*Reason);
			}
			return false;
		};
		FLayoutCandidateVariantIdentity ChildIdentity;
		ChildIdentity.ModuleSnapshotId = ChildModule.SnapshotId;
		ChildIdentity.YawRotationSteps = ChildYawRotationSteps;
		OutOption.ChildRestriction.AllowedCandidates.Add(ChildIdentity);
		OutOption.ChildWorldFaceRules = FLayoutModuleFaceRules();
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection WorldDirection =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			FLayoutFaceRule WorldRule;
			if (TryGetSnapshotWorldFaceRule(
				ChildModule,
				WorldDirection,
				ChildYawRotationSteps,
				WorldRule))
			{
				OutOption.ChildWorldFaceRules.SetRule(WorldRule);
			}
		}
		if (ChildPlannedCell.Intent == ELayoutCellIntent::Entry
			&& !PlanningVariant.InterfaceSummary.EntryCapabilities.ContainsByPredicate(
				[&](const FLayoutChildCapabilityEndpoint& Capability)
				{
					return Capability.LocalCell.Z == ChildPlannedCell.Cell.Z
						&& !PlanningVariant.PlannedCellSet.Contains(
							ChildPlannedCell.Cell
								+ FLayoutDirectionUtils::ToCellDelta(
									Capability.FaceDirection))
						&& DoesBoundaryCandidateSupportEntryCapability(
							ChildModule,
							ChildYawRotationSteps,
							Capability);
				}))
		{
			return Reject(TEXT("Entry candidate does not satisfy any exposed compiled child Entry capability."));
		}

		const FLayoutChildStageMappedCell* const* MappedCellPtr =
			StageCellBySourceCell.Find(ChildPlannedCell.Cell);
		const FLayoutChildStageMappedCell* MappedCell =
			MappedCellPtr != nullptr ? *MappedCellPtr : nullptr;
		if (MappedCell == nullptr)
		{
			return Reject(TEXT("Stage mapping omitted this child boundary cell."));
		}
		OutOption.ChildRestriction.Cell = MappedCell->MappedChildCell;

		int32 EligibleTraversableNeighborFaceCount = 0;
		int32 PotentialWalkableNeighborFaceCount = 0;

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection ChildDirection =
				static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FIntVector ChildNeighbor =
				MappedCell->MappedChildCell + FLayoutDirectionUtils::ToCellDelta(ChildDirection);
			if (MappedChildCells.Contains(ChildNeighbor)
				&& ChildModule.OccupiedLocalCells.Num() > 1
				&& LayoutPlacementOccupancy::BuildSnapshotWorldOccupiedCells(
					&ChildModule,
					MappedCell->MappedChildCell,
					ChildYawRotationSteps).Contains(ChildNeighbor))
			{
				// Composite-internal faces are discharged by the same atomic bundle.
				continue;
			}
			const FLayoutFaceRule* ChildFaceRule =
				OutOption.ChildWorldFaceRules.FindRule(ChildDirection);
			if (ChildFaceRule == nullptr)
			{
				return Reject(FString::Printf(
					TEXT("No face rule exists for child direction %s."),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
						static_cast<int64>(ChildDirection))));
			}
			if (MappedChildCells.Contains(ChildNeighbor))
			{
				// Boundary negotiation constrains only parent-facing faces. Internal
				// adjacency remains a child-owned CSP decision after descendants and
				// atomic bundles settle.
				continue;
			}
			// Filled parent contact does not turn outside-child space into child interior.
			if (ChildDirection != ELayoutFaceDirection::PosZ
				&& ChildDirection != ELayoutFaceDirection::NegZ
				&& ChildFaceRule->BoundaryRequirement
					== ELayoutFaceBoundaryRequirement::MustFaceInterior)
			{
				return Reject(FString::Printf(
					TEXT("Exposed child face %s requires child-interior topology (%s)."),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
						static_cast<int64>(ChildDirection)),
					*DescribeBoundaryFaceRule(*ChildFaceRule)));
			}

			const FIntVector ParentNeighbor =
				MappedCell->ParentCell + FLayoutDirectionUtils::ToCellDelta(ChildDirection);
			if (ParentChildCells.Contains(ParentNeighbor))
			{
				continue;
			}

			const FLayoutPlannedCell* const* ParentPlannedCell =
				ParentPlannedCellByCell.Find(ParentNeighbor);
			FLayoutSolveBoundaryPoint BoundaryPoint;
			BoundaryPoint.LocalCell = ChildNeighbor;
			BoundaryPoint.FaceDirection = FLayoutDirectionUtils::GetOpposite(ChildDirection);
			BoundaryPoint.SourceRegionDebugPath = ParentRequest.RegionDebugPath;
			BoundaryPoint.SourceCell = ParentNeighbor;
			BoundaryPoint.bRequiresBoundaryFacing = false;
			BoundaryPoint.bUsesCertifiedReciprocalDomain = true;
			if (ParentPlannedCell == nullptr || *ParentPlannedCell == nullptr)
			{
				if (!DoesFaceAllowCertifiedEmptyNeighbor(*ChildFaceRule))
				{
					return Reject(FString::Printf(
						TEXT("Parent neighbor %s is absent, but exposed child face %s rejects empty occupancy (%s)."),
						*ParentNeighbor.ToString(),
						*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
							static_cast<int64>(ChildDirection)),
						*DescribeBoundaryFaceRule(*ChildFaceRule)));
				}
				BoundaryPoint.bRepresentsFilledNeighbor = false;
				OutOption.ChildIncomingBoundaryPoints.Add(MoveTemp(BoundaryPoint));
				continue;
			}

			FLayoutCellCandidateDomainRestriction ParentRestriction;
			ParentRestriction.Cell = ParentNeighbor;
			TArray<FString> ParentCandidateFailures;
			bool bHasWalkableParentNeighbor = false;
			const int32 ParentTopLevel = ParentTopLevelByXY.FindRef(
				FIntPoint(ParentNeighbor.X, ParentNeighbor.Y));
			const bool bCanOmitParentDeck = (*ParentPlannedCell)->bIsTopBridgeOffer
				&& LayoutProfileSolverInternal::CanTopBridgeOfferRemainEmpty(**ParentPlannedCell,
					(*ParentPlannedCell)->Intent,
					OutOption.ParentRestrictionsByCell.Contains(ParentNeighbor)
						|| ParentRequest.CandidateDomainRestrictions.ContainsByPredicate([&](const auto& R) { return R.Cell == ParentNeighbor; })
						|| ParentRequest.RequiredRouteConstraints.ContainsByPredicate([&](const auto& R) { return R.Cell == ParentNeighbor; })
						|| ParentRequest.CommittedTraversalAnchors.ContainsByPredicate([&](const auto& A) { return A.Cell == ParentNeighbor; }));
			if (ChildPlannedCell.Intent != ELayoutCellIntent::Entry
				&& DoesFaceAllowCertifiedEmptyNeighbor(*ChildFaceRule)
				&& (bCanOmitParentDeck || IsPreservedTerrainSparseCandidateCell(
					ParentRequest.ProfileSnapshot,
					**ParentPlannedCell,
					ParentTopLevel)))
			{
				// Do not turn an optional deck into mandatory filled support merely
				// because a child wall can face it. Final parent occupancy refreshes this contact.
				BoundaryPoint.bRepresentsFilledNeighbor = false;
				OutOption.ChildIncomingBoundaryPoints.Add(MoveTemp(BoundaryPoint));
				continue;
			}
			// Child boundaries certify both provisional content and still-selectable stair roots.
			const bool bPotentialVerticalAccessRoot =
				ParentRequest.VerticalAccessHostGroups.ContainsByPredicate(
					[&ParentNeighbor, &ParentPlannedCellByCell](const FLayoutVerticalAccessHostGroup& Group)
					{
						return Group.Options.ContainsByPredicate(
							[&ParentNeighbor, &ParentPlannedCellByCell](const FLayoutVerticalAccessHostOption& Option)
							{
								// Parent host selection requires both cells in its prepared topology.
								return Option.LowerCell == ParentNeighbor
									&& ParentPlannedCellByCell.Contains(Option.UpperCell);
							});
					});
			FLayoutPlannedCell VerticalAccessPlannedCell = **ParentPlannedCell;
			VerticalAccessPlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
			for (const FLayoutModuleSolveSnapshot& ParentModule :
				ParentRequest.ModuleCatalog.Modules)
			{
				const bool bMatchesSettledIntent = DoesSnapshotMatchBoundaryPlannedCell(
					ParentModule,
					**ParentPlannedCell,
					ParentTopLevel);
				const bool bMatchesSelectableVerticalAccessIntent = bPotentialVerticalAccessRoot
					&& DoesSnapshotMatchBoundaryPlannedCell(
						ParentModule,
						VerticalAccessPlannedCell,
						ParentTopLevel);
				if (!bMatchesSettledIntent && !bMatchesSelectableVerticalAccessIntent)
				{
					if (OutFailureReason != nullptr && ParentCandidateFailures.Num() < 8)
					{
						ParentCandidateFailures.Add(FString::Printf(
							TEXT("%s: static mismatch intentSupport=%s moduleZone=%s levelPolicy=%s plannedModuleLevel=%d topModuleLevel=%d"),
							*ParentModule.DebugName.ToString(),
							ParentModule.SupportsRootIntent((*ParentPlannedCell)->Intent) ? TEXT("true") : TEXT("false"),
							*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
								static_cast<int64>(ParentModule.PlacementZone)),
							*StaticEnum<ELayoutLevelPlacementPolicy>()->GetNameStringByValue(
								static_cast<int64>(ParentModule.LevelPlacementPolicy)),
							(*ParentPlannedCell)->ModuleLevelIndex,
							ParentTopLevel));
					}
					continue;
				}
				TArray<int32> ParentYaws = ParentModule.AllowedYawRotationSteps;
				if (ParentYaws.IsEmpty()) ParentYaws.Add(0);
				for (const int32 ParentYaw : ParentYaws)
				{
					FLayoutFaceRule ParentFaceRule;
					if (!TryGetSnapshotWorldFaceRule(
						ParentModule,
						FLayoutDirectionUtils::GetOpposite(ChildDirection),
						ParentYaw,
						ParentFaceRule))
					{
						if (OutFailureReason != nullptr && ParentCandidateFailures.Num() < 8)
						{
							ParentCandidateFailures.Add(FString::Printf(
								TEXT("%s[yaw=%d]: missing opposite face rule"),
								*ParentModule.DebugName.ToString(),
								ParentYaw));
						}
						continue;
					}
					if (!AreBoundaryFaceRulesReciprocallyCompatible(
						*ChildFaceRule,
						ChildYawRotationSteps,
						ParentFaceRule,
						ParentYaw))
					{
						if (OutFailureReason != nullptr && ParentCandidateFailures.Num() < 8)
						{
							ParentCandidateFailures.Add(FString::Printf(
								TEXT("%s[yaw=%d]: %s"),
								*ParentModule.DebugName.ToString(),
								ParentYaw,
								*DescribeBoundaryFaceRule(ParentFaceRule)));
						}
						continue;
					}
					FLayoutCandidateVariantIdentity& ParentIdentity =
						ParentRestriction.AllowedCandidates.AddDefaulted_GetRef();
					ParentIdentity.ModuleSnapshotId = ParentModule.SnapshotId;
					ParentIdentity.YawRotationSteps = ParentYaw;
					bHasWalkableParentNeighbor = bHasWalkableParentNeighbor
						|| HasSharedConnectedWalkableArea(
							*ChildFaceRule,
							ParentFaceRule);
				}
			}
			if (ParentRestriction.AllowedCandidates.IsEmpty())
			{
				return Reject(FString::Printf(
					TEXT("Parent neighbor %s intent=%s zone=%s has no reciprocal module/yaw across child face %s (%s). ParentCandidates=[%s]"),
					*ParentNeighbor.ToString(),
					*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(
						static_cast<int64>((*ParentPlannedCell)->Intent)),
					*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
						static_cast<int64>((*ParentPlannedCell)->PlacementZone)),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(
						static_cast<int64>(ChildDirection)),
					*DescribeBoundaryFaceRule(*ChildFaceRule),
					ParentCandidateFailures.IsEmpty()
						? TEXT("No parent module matched intent, placement zone, and level policy.")
						: *FString::Join(ParentCandidateFailures, TEXT(" | "))));
			}
			ParentRestriction.RestrictionId = FLayoutId(*FString::Printf(
				TEXT("ParentBoundary.%s.%s.%d"),
				*ParentNeighbor.ToString(),
				*ChildPlannedCell.Cell.ToString(),
				DirectionIndex));
			if (FLayoutCellCandidateDomainRestriction* ExistingRestriction =
					OutOption.ParentRestrictionsByCell.Find(ParentNeighbor))
			{
				IntersectCandidateRestriction(
					*ExistingRestriction,
					ParentRestriction);
				if (ExistingRestriction->AllowedCandidates.IsEmpty())
				{
					return Reject(FString::Printf(
						TEXT("Parent neighbor %s cannot satisfy all incident child boundary faces after candidate-domain intersection."),
						*ParentNeighbor.ToString()));
				}
			}
			else
			{
				OutOption.ParentRestrictionsByCell.Add(
					ParentNeighbor,
					MoveTemp(ParentRestriction));
			}
			BoundaryPoint.bRepresentsFilledNeighbor = true;
			OutOption.ChildIncomingBoundaryPoints.Add(MoveTemp(BoundaryPoint));
			if (!ChildFaceRule->ConnectedTraversalChannels.IsEmpty())
			{
				++EligibleTraversableNeighborFaceCount;
				PotentialWalkableNeighborFaceCount +=
					bHasWalkableParentNeighbor ? 1 : 0;
			}
		}
		if (PotentialWalkableNeighborFaceCount < FMath::Min(
			ChildModule.MinTraversableNeighborFaces,
			EligibleTraversableNeighborFaceCount))
		{
			return Reject(FString::Printf(
				TEXT("Only %d/%d eligible traversable neighbor faces can connect; candidate minimum is %d."),
				PotentialWalkableNeighborFaceCount,
				EligibleTraversableNeighborFaceCount,
				ChildModule.MinTraversableNeighborFaces));
		}

		struct FOptionExactPair
		{
			FLayoutCandidateVariantIdentity ParentIdentity;
			bool bParentCanOwn = false;
			bool bChildCanOwn = false;
			FLayoutId ParentEvidenceId;
			FLayoutId ChildEvidenceId;
			FLayoutId WitnessId;
		};
		for (const FSharedParentChildFace& PotentialFace : PotentialSharedFaces)
		{
			if (PotentialFace.ChildLocalCell != ChildPlannedCell.Cell)
			{
				continue;
			}
			const FLayoutPlannedCell* const* ParentPlannedCell =
				ParentPlannedCellByCell.Find(PotentialFace.ParentCell);
			if (ParentPlannedCell == nullptr || *ParentPlannedCell == nullptr)
			{
				continue;
			}

			TArray<FOptionExactPair> ExactPairs;
			FLayoutCellCandidateDomainRestriction ParentSeamRestriction;
			ParentSeamRestriction.Cell = PotentialFace.ParentCell;
			const uint8 JunctionFaces = GetParentPerimeterJunctionFaces(ParentRequest,
				**ParentPlannedCell, PotentialFace, PotentialSharedFaces);
			ParentSeamRestriction.bTreatAsJunctionPlacementZone = JunctionFaces != 0;
			const int32 ParentTopLevel = ParentTopLevelByXY.FindRef(
				FIntPoint(PotentialFace.ParentCell.X, PotentialFace.ParentCell.Y));
			for (const FLayoutModuleSolveSnapshot& ParentModule :
				ParentRequest.ModuleCatalog.Modules)
			{
				if (!DoesSnapshotMatchBoundaryPlannedCell(
					ParentModule,
					**ParentPlannedCell,
					ParentTopLevel, JunctionFaces != 0))
				{
					continue;
				}
				TArray<int32> ParentYaws = ParentModule.AllowedYawRotationSteps;
				if (ParentYaws.IsEmpty()) ParentYaws.Add(0);
				for (const int32 ParentYaw : ParentYaws)
				{
					FOptionExactPair Pair;
					Pair.ParentIdentity.ModuleSnapshotId = ParentModule.SnapshotId;
					Pair.ParentIdentity.YawRotationSteps = ParentYaw;
					if (!TryCertifyExactSharedSeamCandidatePair(
						ParentModule,
						ParentYaw,
						ChildModule,
						ChildYawRotationSteps,
						PotentialFace.FaceDirection,
						PotentialFace.InterfaceFamily,
						ChildPlannedCell.Intent == ELayoutCellIntent::Entry,
						Pair.bParentCanOwn,
						Pair.bChildCanOwn,
						Pair.ParentEvidenceId,
						Pair.ChildEvidenceId,
						Pair.WitnessId,
						&ParentRequest,
						PotentialFace.ParentCell))
					{
						continue;
					}
					Pair.bParentCanOwn &= CanOwnPerimeterJunction(ParentModule, ParentYaw, PotentialFace.InterfaceFamily, JunctionFaces);
					Pair.bChildCanOwn &= CanOwnPerimeterJunction(ChildModule, ChildYawRotationSteps, PotentialFace.InterfaceFamily, JunctionFaces);
					if (!Pair.bParentCanOwn && !Pair.bChildCanOwn) continue;
					if (!ParentSeamRestriction.AllowedCandidates.ContainsByPredicate(
						[&Pair](const FLayoutCandidateVariantIdentity& Candidate)
						{
							return Candidate.ModuleSnapshotId == Pair.ParentIdentity.ModuleSnapshotId
								&& Candidate.YawRotationSteps == Pair.ParentIdentity.YawRotationSteps;
						}))
					{
						ParentSeamRestriction.AllowedCandidates.Add(Pair.ParentIdentity);
					}
					ExactPairs.Add(MoveTemp(Pair));
				}
			}
			if (ExactPairs.IsEmpty())
			{
				if (JunctionFaces != 0) return Reject(TEXT("Required perimeter junction face has no compatible owner/passive pair."));
				continue;
			}

			const TMap<FIntVector, FLayoutCellCandidateDomainRestriction> SavedParentRestrictions =
				OutOption.ParentRestrictionsByCell;
			if (FLayoutCellCandidateDomainRestriction* ExistingRestriction =
					OutOption.ParentRestrictionsByCell.Find(PotentialFace.ParentCell))
			{
				IntersectCandidateRestriction(*ExistingRestriction, ParentSeamRestriction);
				ExistingRestriction->bTreatAsJunctionPlacementZone |= JunctionFaces != 0;
				if (ExistingRestriction->AllowedCandidates.IsEmpty())
				{
					OutOption.ParentRestrictionsByCell = SavedParentRestrictions;
					if (JunctionFaces != 0) return Reject(TEXT("Required perimeter junction faces have no common parent candidate."));
					continue;
				}
			}
			else
			{
				ParentSeamRestriction.RestrictionId = FLayoutId(*FString::Printf(
					TEXT("ParentSeam.%s.%d.%08X"),
					*PotentialFace.ParentCell.ToString(),
					static_cast<int32>(PotentialFace.FaceDirection),
					FCrc::StrCrc32(*PotentialFace.InterfaceFamily.ToString())));
				OutOption.ParentRestrictionsByCell.Add(
					PotentialFace.ParentCell,
					MoveTemp(ParentSeamRestriction));
			}

			const FLayoutCellCandidateDomainRestriction& FinalParentRestriction =
				OutOption.ParentRestrictionsByCell.FindChecked(PotentialFace.ParentCell);
			ExactPairs.RemoveAll(
				[&FinalParentRestriction](const FOptionExactPair& Pair)
				{
					return !FinalParentRestriction.AllowedCandidates.ContainsByPredicate(
						[&Pair](const FLayoutCandidateVariantIdentity& Candidate)
						{
							return Candidate.ModuleSnapshotId == Pair.ParentIdentity.ModuleSnapshotId
								&& Candidate.YawRotationSteps == Pair.ParentIdentity.YawRotationSteps;
						});
				});
			if (ExactPairs.IsEmpty())
			{
				OutOption.ParentRestrictionsByCell = SavedParentRestrictions;
				if (JunctionFaces != 0) return Reject(TEXT("Required perimeter junction lost its correlated candidate pair."));
				continue;
			}

			FSharedParentChildFace CertifiedFace = PotentialFace;
			CertifiedFace.bParentCanOwnSeam = false;
			CertifiedFace.bChildCanOwnSeam = false;
			TArray<FString> WitnessParts;
			for (const FOptionExactPair& Pair : ExactPairs)
			{
				CertifiedFace.bParentCanOwnSeam |= Pair.bParentCanOwn;
				CertifiedFace.bChildCanOwnSeam |= Pair.bChildCanOwn;
				if (CertifiedFace.ParentSeamCapabilityId.IsNone()
					|| Pair.ParentEvidenceId.LexicalLess(CertifiedFace.ParentSeamCapabilityId))
				{
					CertifiedFace.ParentSeamCapabilityId = Pair.ParentEvidenceId;
				}
				if (CertifiedFace.ChildSeamCapabilityId.IsNone()
					|| Pair.ChildEvidenceId.LexicalLess(CertifiedFace.ChildSeamCapabilityId))
				{
					CertifiedFace.ChildSeamCapabilityId = Pair.ChildEvidenceId;
				}
				WitnessParts.Add(Pair.WitnessId.ToString());
			}
			WitnessParts.Sort();
			CertifiedFace.ReciprocalDomainWitnessId = FLayoutId(*FString::Printf(
				TEXT("SeamDomain.%08X"),
				FCrc::StrCrc32(*FString::Join(WitnessParts, TEXT("|")))));
			if (const FLayoutFaceRule* ChildFaceRule =
					OutOption.ChildWorldFaceRules.FindRule(PotentialFace.FaceDirection))
			{
				CertifiedFace.ConnectionTag = GetStablePrimaryFaceConnectionTag(*ChildFaceRule);
				CertifiedFace.AllowedConnectionTags =
					ChildFaceRule->GetEffectiveAllowedConnectionTags();
				CertifiedFace.TraversalChannels = ChildFaceRule->ConnectedTraversalChannels;
				CertifiedFace.bRequireMatchingYawWithFilledNeighbor =
					ChildFaceRule->bRequireMatchingYawWithFilledNeighbor;
			}
			OutOption.CertifiedSharedFaces.Add(MoveTemp(CertifiedFace));
		}

		OutOption.StableKey = FString::Printf(
			TEXT("%s:%s:Yaw%d"),
			*MappedCell->MappedChildCell.ToString(),
			*ChildModule.SnapshotId.ToString(),
			ChildYawRotationSteps);
		OutOption.ChildRestriction.RestrictionId = FLayoutId(*FString::Printf(
			TEXT("ChildBoundary.%s.%s.Yaw%d"),
			*MappedCell->MappedChildCell.ToString(),
			*ChildModule.SnapshotId.ToString(),
			ChildYawRotationSteps));
		for (const FSharedParentChildFace& CertifiedFace : OutOption.CertifiedSharedFaces)
		{
			OutOption.StableKey += TEXT(":");
			OutOption.StableKey += BuildSharedParentChildFaceIdentityPart(CertifiedFace);
		}
		return true;
	}

	/** Settles boundary modules, exact shared seams, and placement-zone eligibility in one backtracking domain. */
	bool TryBuildCertifiedBoundaryDomains(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ParentFootprintSize,
		const TMap<FIntPoint, int32>& ParentTopLevelByXY,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const FLayoutChildStageMappingResult& StageMapping,
		const FName SourceEntryId,
		const ELayoutPlacementZone ChildPlacementZone,
		const TArray<FSharedParentChildFace>& PotentialSharedFaces,
		TArray<FLayoutCellCandidateDomainRestriction>& OutParentRestrictions,
		TArray<FLayoutCellCandidateDomainRestriction>& OutChildRestrictions,
		TArray<FLayoutSolveBoundaryPoint>& OutChildIncomingBoundaryPoints,
		TArray<FSharedParentChildFace>& OutCertifiedSharedFaces,
		FLayoutId& OutCertificateId,
		int32& InOutBoundarySearchAttempts,
		FString* const OutFailureReason = nullptr)
	{
		if (OutFailureReason != nullptr)
		{
			OutFailureReason->Reset();
		}
		OutParentRestrictions.Reset();
		OutChildRestrictions.Reset();
		OutChildIncomingBoundaryPoints.Reset();
		OutCertifiedSharedFaces.Reset();
		OutCertificateId = NAME_None;

		TMap<FIntVector, const FLayoutPlannedCell*> ParentPlannedCellByCell;
		for (const FLayoutPlannedCell& ParentPlannedCell : ParentPlannedCells)
		{
			ParentPlannedCellByCell.Add(ParentPlannedCell.Cell, &ParentPlannedCell);
		}
		int32 ChildTopModuleLevel = INDEX_NONE;
		for (const FLayoutPlannedCell& ChildPlannedCell : PlanningVariant.PlannedCells)
		{
			ChildTopModuleLevel = FMath::Max(
				ChildTopModuleLevel,
				ChildPlannedCell.ModuleLevelIndex);
		}
		TMap<FIntVector, const FLayoutChildStageMappedCell*> StageCellBySourceCell;
		TSet<FIntVector> ParentChildCells;
		TSet<FIntVector> MappedChildCells;
		for (const FLayoutChildStageMappedCell& StageCell : StageMapping.Cells)
		{
			StageCellBySourceCell.Add(StageCell.SourceChildCell, &StageCell);
			ParentChildCells.Add(StageCell.ParentCell);
			MappedChildCells.Add(StageCell.MappedChildCell);
		}

		// Certified shared cells are a subset of these geometric possibilities.
		// Reject impossible zones before constructing module/yaw options for every face.
		if (ChildPlacementZone != ELayoutPlacementZone::Any)
		{
			TSet<FIntVector> PossibleSharedCells;
			for (const auto& Face : PotentialSharedFaces) PossibleSharedCells.Add(Face.ParentCell);
			if (!DoesTranslatedChildPlanMatchPlacementZone(StageMapping.ParentTranslatedPlannedCells,
				FIntVector::ZeroValue, ParentFootprintSize, ChildPlacementZone, &PossibleSharedCells, OutFailureReason)) return false;
		}

		struct FBoundaryCellOptionSet
		{
			FIntVector Cell = FIntVector::ZeroValue;
			TArray<FCertifiedBoundaryCellOption> Options;
		};
		TArray<FBoundaryCellOptionSet> OptionSets;
		for (const FLayoutPlannedCell& SourceChildCell : PlanningVariant.PlannedCells)
		{
			// Keep source coordinates for stage lookup, but classify the physical
			// child boundary after mapping. A terrain step can expose an additional
			// face at a source-grid Edge; that is a real mapped Corner, not a seam override.
			FLayoutPlannedCell ChildPlannedCell = SourceChildCell;
			const auto* StageCell = StageCellBySourceCell.FindRef(SourceChildCell.Cell);
			if (StageCell == nullptr) return false;
			if (SourceChildCell.bIsTopBridgeOffer && SourceChildCell.VerticalAccessLandingContactMask == 0)
			{
				bool bTouchesExternalParent = false;
				for (int32 Direction = 0; Direction < 6; ++Direction)
				{
					const FIntVector Neighbor = StageCell->ParentCell
						+ FLayoutDirectionUtils::ToCellDelta(static_cast<ELayoutFaceDirection>(Direction));
					bTouchesExternalParent |= ParentPlannedCellByCell.Contains(Neighbor) && !ParentChildCells.Contains(Neighbor);
				}
				// Above-parent deck offers carry no parent contact to certify. An occupied
				// restriction here would incorrectly remove their ordinary Empty alternative.
				if (!bTouchesExternalParent) continue;
			}
			uint8 MissingFaces = SourceChildCell.TerrainSeamFaceMask;
			for (const auto Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
				if (!MappedChildCells.Contains(StageCell->MappedChildCell + FLayoutDirectionUtils::ToCellDelta(Direction)))
					MissingFaces |= LayoutFaceDirectionMask(Direction);
			ChildPlannedCell.PlacementZone = ResolveLayoutPlacementZoneFromLateralFaceMask(MissingFaces);
			if (MissingFaces == 0)
			{
				continue;
			}

			FBoundaryCellOptionSet& OptionSet = OptionSets.AddDefaulted_GetRef();
			TArray<FString> CandidateFailures;
			for (const FLayoutModuleSolveSnapshot& ChildModule :
				PlanningVariant.ChildRequestTemplate.ModuleCatalog.Modules)
			{
				if (!DoesSnapshotMatchBoundaryPlannedCell(
					ChildModule,
					ChildPlannedCell,
					ChildTopModuleLevel,
					// Keep contract-equivalent offers for this child's own later branches.
					// Do not set the placement-zone marker: only actual ownership may do that.
					ContentSetHasChildRegionEntries(PlanningVariant.ChildRequestTemplate.ContentSetSnapshot)))
				{
					continue;
				}
				TArray<int32> ChildYaws = ChildModule.AllowedYawRotationSteps;
				if (ChildYaws.IsEmpty()) ChildYaws.Add(0);
				for (const int32 ChildYaw : ChildYaws)
				{
					FCertifiedBoundaryCellOption Option;
					FString CandidateFailure;
					if (TryBuildCertifiedBoundaryOption(
						ParentRequest,
						ParentPlannedCellByCell,
						ParentTopLevelByXY,
						PlanningVariant,
						StageCellBySourceCell,
						ParentChildCells,
						MappedChildCells,
						PotentialSharedFaces,
						ChildPlannedCell,
						ChildTopModuleLevel,
						ChildModule,
						ChildYaw,
						Option,
						OutFailureReason != nullptr ? &CandidateFailure : nullptr))
					{
						Option.bRequiresFutureJunction = ChildPlannedCell.PlacementZone == ELayoutPlacementZone::Edge
							&& ChildModule.PlacementZone == ELayoutPlacementZone::Corner;
						OptionSet.Cell = Option.ChildRestriction.Cell;
						OptionSet.Options.Add(MoveTemp(Option));
					}
					else if (!CandidateFailure.IsEmpty() && CandidateFailures.Num() < 12)
					{
						CandidateFailures.Add(MoveTemp(CandidateFailure));
					}
				}
			}
			OptionSet.Options.Sort([](const FCertifiedBoundaryCellOption& Left,
				const FCertifiedBoundaryCellOption& Right)
			{
				if (Left.CertifiedSharedFaces.Num() != Right.CertifiedSharedFaces.Num())
				{
					return Left.CertifiedSharedFaces.Num() > Right.CertifiedSharedFaces.Num();
				}
				return Left.StableKey < Right.StableKey;
			});
			if (OptionSet.Options.IsEmpty())
			{
				if (OutFailureReason != nullptr)
				{
					*OutFailureReason = FString::Printf(
						TEXT("Child boundary cell %s intent=%s has no certified module/yaw option. CandidateFailures=[%s]"),
						*(StageCell->bGeneratedByChildTopology ? StageCell->MappedChildCell : ChildPlannedCell.Cell).ToString(),
						*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(
							static_cast<int64>(ChildPlannedCell.Intent)),
						CandidateFailures.IsEmpty()
							? TEXT("No child module matched this cell's role, placement zone, and level policy.")
							: *FString::Join(CandidateFailures, TEXT(" | ")));
				}
				return false;
			}
		}
		OptionSets.Sort([](const FBoundaryCellOptionSet& Left,
			const FBoundaryCellOptionSet& Right)
		{
			if (Left.Options.Num() != Right.Options.Num())
			{
				return Left.Options.Num() < Right.Options.Num();
			}
			if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
			if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
			return Left.Cell.X < Right.Cell.X;
		});

		if (ChildPlacementZone != ELayoutPlacementZone::Any)
		{
			// Even the union of available shared seams must admit this placement zone.
			// Reject impossible offsets before their yaw permutations spend the shared search budget.
			// The search below still chooses and validates the actual shared seams jointly.
			TSet<FIntVector> PossibleSharedParentCells;
			for (const FBoundaryCellOptionSet& OptionSet : OptionSets)
			{
				for (const FCertifiedBoundaryCellOption& Option : OptionSet.Options)
				{
					for (const FSharedParentChildFace& Face : Option.CertifiedSharedFaces)
					{
						PossibleSharedParentCells.Add(Face.ParentCell);
					}
				}
			}
			if (!DoesTranslatedChildPlanMatchPlacementZone(StageMapping.ParentTranslatedPlannedCells,
				FIntVector::ZeroValue, ParentFootprintSize, ChildPlacementZone,
				&PossibleSharedParentCells, OutFailureReason)) return false;
		}

		TMap<FIntVector, FLayoutCellCandidateDomainRestriction> ParentRestrictionsByCell;
		TMap<FIntVector, const FCertifiedBoundaryCellOption*> SelectedOptionByCell;
		TArray<const FCertifiedBoundaryCellOption*> SelectedOptions;
		const int32 BoundaryAttemptsAtEntry = InOutBoundarySearchAttempts;
		int32 PlacementZoneRejections = 0;
		TFunction<bool(int32)> SearchBoundaryOptions = [&](const int32 OptionSetIndex)
		{
			if (OptionSetIndex >= OptionSets.Num())
			{
				TSet<FIntVector> CertifiedSharedParentCells;
				for (const FCertifiedBoundaryCellOption* SelectedOption : SelectedOptions)
				{
					for (const FSharedParentChildFace& SharedFace : SelectedOption->CertifiedSharedFaces)
					{
						CertifiedSharedParentCells.Add(SharedFace.ParentCell);
					}
				}
				const bool bMatchesZone = DoesTranslatedChildPlanMatchPlacementZone(
					StageMapping.ParentTranslatedPlannedCells,
					FIntVector::ZeroValue,
					ParentFootprintSize,
					ChildPlacementZone,
					&CertifiedSharedParentCells);
				if (!bMatchesZone) ++PlacementZoneRejections;
				return bMatchesZone;
			}
			if (++InOutBoundarySearchAttempts > FMath::Max(1, ParentRequest.ExecutionSettings.MaxCandidateAttempts))
			{
				return false;
			}

			const FBoundaryCellOptionSet& OptionSet = OptionSets[OptionSetIndex];
			for (const FCertifiedBoundaryCellOption& Option : OptionSet.Options)
			{
				// Conditional offers may enrich an equivalent external contract below,
				// but cannot establish an ordinary Edge's initial boundary witness.
				if (Option.bRequiresFutureJunction) continue;
				bool bAdjacentSignaturesCompatible = true;
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					const ELayoutFaceDirection Direction =
						static_cast<ELayoutFaceDirection>(DirectionIndex);
					const FCertifiedBoundaryCellOption* const* NeighborOption =
						SelectedOptionByCell.Find(
							OptionSet.Cell + FLayoutDirectionUtils::ToCellDelta(Direction));
					if (NeighborOption == nullptr || *NeighborOption == nullptr)
					{
						continue;
					}
					const FLayoutFaceRule* LeftRule = Option.ChildWorldFaceRules.FindRule(Direction);
					const FLayoutFaceRule* RightRule = (*NeighborOption)->ChildWorldFaceRules.FindRule(
						FLayoutDirectionUtils::GetOpposite(Direction));
					if (LeftRule == nullptr || RightRule == nullptr
						|| !AreBoundaryFaceRulesReciprocallyCompatible(
							*LeftRule,
							Option.ChildRestriction.AllowedCandidates[0].YawRotationSteps,
							*RightRule,
							(*NeighborOption)->ChildRestriction.AllowedCandidates[0].YawRotationSteps))
					{
						bAdjacentSignaturesCompatible = false;
						break;
					}
				}
				if (!bAdjacentSignaturesCompatible)
				{
					continue;
				}

				// Boundary-only maps stay small; use an undo log if profiling shows copy cost.
				const TMap<FIntVector, FLayoutCellCandidateDomainRestriction> SavedParentRestrictions =
					ParentRestrictionsByCell;
				bool bParentDomainsRemainNonEmpty = true;
				for (const TPair<FIntVector, FLayoutCellCandidateDomainRestriction>& Pair :
					Option.ParentRestrictionsByCell)
				{
					if (FLayoutCellCandidateDomainRestriction* Existing =
							ParentRestrictionsByCell.Find(Pair.Key))
					{
						IntersectCandidateRestriction(*Existing, Pair.Value);
						if (Existing->AllowedCandidates.IsEmpty())
						{
							bParentDomainsRemainNonEmpty = false;
							break;
						}
					}
					else
					{
						ParentRestrictionsByCell.Add(Pair.Key, Pair.Value);
					}
				}
				if (!bParentDomainsRemainNonEmpty)
				{
					ParentRestrictionsByCell = SavedParentRestrictions;
					continue;
				}

				SelectedOptionByCell.Add(OptionSet.Cell, &Option);
				SelectedOptions.Add(&Option);
				if (SearchBoundaryOptions(OptionSetIndex + 1))
				{
					return true;
				}
				SelectedOptions.Pop(EAllowShrinking::No);
				SelectedOptionByCell.Remove(OptionSet.Cell);
				ParentRestrictionsByCell = SavedParentRestrictions;
			}
			return false;
		};
		if (!SearchBoundaryOptions(0))
		{
			if (OutFailureReason != nullptr && OutFailureReason->IsEmpty())
			{
				*OutFailureReason = FString::Printf(
					TEXT("All %d child boundary cells had local module/yaw options, but no joint selection retained compatible adjacent child faces, non-empty parent restrictions, and placement zone %s within the %d-attempt boundary-search budget. AttemptsAtEntry=%d PlacementZoneRejections=%d."),
					OptionSets.Num(),
					*StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
						static_cast<int64>(ChildPlacementZone)),
					FMath::Max(1, ParentRequest.ExecutionSettings.MaxCandidateAttempts),
					BoundaryAttemptsAtEntry, PlacementZoneRejections);
			}
			return false;
		}

		const auto BuildBoundaryContractKey = [](const FCertifiedBoundaryCellOption& Option)
		{
			return FString::JoinBy(
				Option.ChildIncomingBoundaryPoints,
				TEXT("|"),
				[](const FLayoutSolveBoundaryPoint& Point)
				{
					return FString::Printf(
						TEXT("%s:%d:%d:%s:%s:%s:%d"),
						*Point.LocalCell.ToString(),
						static_cast<int32>(Point.FaceDirection),
						Point.bRepresentsFilledNeighbor ? 1 : 0,
						*Point.ConnectionTag.ToString(),
						*Point.AllowedConnectionTags.ToStringSimple(),
						*Point.ConnectedTraversalChannels.ToStringSimple(),
						Point.bRequireMatchingYawWithFilledNeighbor ? 1 : 0);
				});
		};
		TArray<FString> CertificateParts;
		for (const FCertifiedBoundaryCellOption* SelectedOption : SelectedOptions)
		{
			FLayoutCellCandidateDomainRestriction ChildRestriction =
				SelectedOption->ChildRestriction;
			CertificateParts.Add(SelectedOption->StableKey);
			if (SelectedOption->CertifiedSharedFaces.IsEmpty()
				&& SelectedOption->ParentRestrictionsByCell.IsEmpty())
			{
				const FString SelectedContractKey =
					BuildBoundaryContractKey(*SelectedOption);
				const FBoundaryCellOptionSet* OptionSet = OptionSets.FindByPredicate(
					[SelectedOption](const FBoundaryCellOptionSet& CandidateSet)
					{
						return CandidateSet.Cell == SelectedOption->ChildRestriction.Cell;
					});
				if (OptionSet != nullptr)
				{
					for (const FCertifiedBoundaryCellOption& EquivalentOption :
						OptionSet->Options)
					{
						if (!EquivalentOption.CertifiedSharedFaces.IsEmpty()
							|| !EquivalentOption.ParentRestrictionsByCell.IsEmpty()
							|| BuildBoundaryContractKey(EquivalentOption)
								!= SelectedContractKey)
						{
							continue;
						}
						for (const FLayoutCandidateVariantIdentity& Identity :
							EquivalentOption.ChildRestriction.AllowedCandidates)
						{
							if (!ChildRestriction.AllowedCandidates.ContainsByPredicate(
								[&Identity](const FLayoutCandidateVariantIdentity& Existing)
								{
									return Existing.ModuleSnapshotId == Identity.ModuleSnapshotId
										&& Existing.YawRotationSteps == Identity.YawRotationSteps;
								}))
							{
								ChildRestriction.AllowedCandidates.Add(Identity);
							}
						}
						ChildRestriction.bTreatAsJunctionPlacementZone |=
							EquivalentOption.ChildRestriction.bTreatAsJunctionPlacementZone;
						CertificateParts.AddUnique(EquivalentOption.StableKey);
					}
				}
			}
			OutChildRestrictions.Add(MoveTemp(ChildRestriction));
			OutChildIncomingBoundaryPoints.Append(
				SelectedOption->ChildIncomingBoundaryPoints);
			OutCertifiedSharedFaces.Append(SelectedOption->CertifiedSharedFaces);
		}
		RefreshCertifiedSharedParentChildFaceRunLengths(OutCertifiedSharedFaces);

		CertificateParts.Sort();
		CertificateParts.Add(StageMapping.MappingId.ToString());
		CertificateParts.Add(SourceEntryId.ToString());
		CertificateParts.Add(ParentRequest.EffectiveSnapshotId.ToString());
		CertificateParts.Add(ParentRequest.ModuleCatalog.SnapshotId.ToString());
		OutCertificateId = FLayoutId(*FString::Printf(
			TEXT("ChildBoundary.%08X"),
			FCrc::StrCrc32(*FString::Join(CertificateParts, TEXT("|")))));
		ParentRestrictionsByCell.GenerateValueArray(OutParentRestrictions);
		OutParentRestrictions.Sort([](
			const FLayoutCellCandidateDomainRestriction& Left,
			const FLayoutCellCandidateDomainRestriction& Right)
		{
			if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
			if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
			return Left.Cell.X < Right.Cell.X;
		});
		for (FLayoutSolveBoundaryPoint& BoundaryPoint : OutChildIncomingBoundaryPoints)
		{
			const FIntVector ChildCell = BoundaryPoint.LocalCell
				+ FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
			const FLayoutCellCandidateDomainRestriction* Restriction =
				OutChildRestrictions.FindByPredicate(
					[ChildCell](const FLayoutCellCandidateDomainRestriction& Candidate)
					{
						return Candidate.Cell == ChildCell;
					});
			BoundaryPoint.CertifiedDomainCertificateId = OutCertificateId;
			BoundaryPoint.CertifiedDomainRestrictionId =
				Restriction != nullptr ? Restriction->RestrictionId : NAME_None;
		}
		if (ContentSetHasChildRegionEntries(
			PlanningVariant.ChildRequestTemplate.ContentSetSnapshot))
		{
			// Recursive children may release shell choices backed by concrete face data,
			// but certificate-backed contacts need their domain until child-local proof.
			TSet<FIntVector> SourceEntryCells;
			for (const FLayoutPlannedCell& PlannedCell : PlanningVariant.PlannedCells)
			{
				if (PlannedCell.Intent == ELayoutCellIntent::Entry)
				{
					SourceEntryCells.Add(PlannedCell.Cell);
				}
			}
			TSet<FIntVector> MappedEntryCells;
			for (const FLayoutChildStageMappedCell& StageCell : StageMapping.Cells)
			{
				if (SourceEntryCells.Contains(StageCell.SourceChildCell))
				{
					MappedEntryCells.Add(StageCell.MappedChildCell);
				}
			}
			OutChildRestrictions.RemoveAll(
				[&MappedEntryCells, &OutChildIncomingBoundaryPoints](const FLayoutCellCandidateDomainRestriction& Restriction)
				{
					return !MappedEntryCells.Contains(Restriction.Cell)
						&& !OutChildIncomingBoundaryPoints.ContainsByPredicate(
							[&Restriction](const FLayoutSolveBoundaryPoint& Point)
							{
								return Point.bUsesCertifiedReciprocalDomain
									&& Point.CertifiedDomainRestrictionId == Restriction.RestrictionId;
							});
				});
			for (FLayoutSolveBoundaryPoint& BoundaryPoint : OutChildIncomingBoundaryPoints)
			{
				const bool bRetainsExactEntryDomain =
					OutChildRestrictions.ContainsByPredicate(
						[&BoundaryPoint](const FLayoutCellCandidateDomainRestriction& Restriction)
						{
							return Restriction.RestrictionId
								== BoundaryPoint.CertifiedDomainRestrictionId;
						});
				if (!bRetainsExactEntryDomain)
				{
					BoundaryPoint.bUsesCertifiedReciprocalDomain = false;
					BoundaryPoint.CertifiedDomainCertificateId = NAME_None;
					BoundaryPoint.CertifiedDomainRestrictionId = NAME_None;
				}
			}
		}
		return !OutCertificateId.IsNone();
	}

	/** Removes capability-only overlaps and freezes exact reciprocal module/yaw domains using authored column tops, never physical terrain heights. */
	void CertifyExactSharedParentChildFaces(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TMap<FIntPoint, int32>& ParentTopLevelByXY,
		const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant,
		const FLayoutChildStageMappingResult& StageMapping,
		TArray<FSharedParentChildFace>& InOutSharedFaces,
		TArray<FLayoutCellCandidateDomainRestriction>& InOutParentRestrictions,
		TArray<FLayoutCellCandidateDomainRestriction>& InOutChildRestrictions,
		FLayoutId& InOutCertificateId,
		TArray<FLayoutSolveBoundaryPoint>& InOutChildIncomingBoundaryPoints)
	{
		struct FExactCandidatePair
		{
			FLayoutCandidateVariantIdentity Parent;
			FLayoutCandidateVariantIdentity Child;
			bool bParentCanOwn = false;
			bool bChildCanOwn = false;
			FLayoutId ParentEvidenceId;
			FLayoutId ChildEvidenceId;
			FLayoutId WitnessId;
			FLayoutFaceRule ChildFaceRule;
		};
		const auto IdentitiesMatch = [](
			const FLayoutCandidateVariantIdentity& Left,
			const FLayoutCandidateVariantIdentity& Right)
		{
			return Left.ModuleSnapshotId == Right.ModuleSnapshotId
				&& Left.YawRotationSteps == Right.YawRotationSteps;
		};
		const auto RestrictionContains = [&IdentitiesMatch](
			const FLayoutCellCandidateDomainRestriction& Restriction,
			const FLayoutCandidateVariantIdentity& Identity)
		{
			return Restriction.AllowedCandidates.ContainsByPredicate(
				[&](const FLayoutCandidateVariantIdentity& Candidate)
				{
					return IdentitiesMatch(Candidate, Identity);
				});
		};
		const TArray<FLayoutCellCandidateDomainRestriction> BaseParentRestrictions =
			InOutParentRestrictions;
		const TArray<FLayoutCellCandidateDomainRestriction> BaseChildRestrictions =
			InOutChildRestrictions;
		const auto RefreshExactFaceEvidence = [&](FSharedParentChildFace& Face)
		{
			const FLayoutChildStageMappedCell* MappedChildCell =
				StageMapping.Cells.FindByPredicate(
					[&](const FLayoutChildStageMappedCell& Candidate)
					{
						return Candidate.SourceChildCell == Face.ChildLocalCell;
					});
			const FLayoutPlannedCell* ChildPlannedCell =
				PlanningVariant.PlannedCells.FindByPredicate(
					[&](const FLayoutPlannedCell& Candidate)
					{
						return Candidate.Cell == Face.ChildLocalCell;
					});
			const FLayoutCellCandidateDomainRestriction* ChildRestriction =
				MappedChildCell != nullptr
					? InOutChildRestrictions.FindByPredicate(
						[MappedChildCell](const FLayoutCellCandidateDomainRestriction& Restriction)
						{
							return Restriction.Cell == MappedChildCell->MappedChildCell;
						})
					: nullptr;
			const FLayoutCellCandidateDomainRestriction* ParentRestriction =
				InOutParentRestrictions.FindByPredicate(
					[&](const FLayoutCellCandidateDomainRestriction& Restriction)
					{
						return Restriction.Cell == Face.ParentCell;
					});
			if (ChildPlannedCell == nullptr
				|| ChildRestriction == nullptr
				|| ParentRestriction == nullptr)
			{
				return false;
			}

			TArray<FExactCandidatePair> FinalPairs;
			for (const FLayoutCandidateVariantIdentity& ChildIdentity :
				ChildRestriction->AllowedCandidates)
			{
				const FLayoutModuleSolveSnapshot* ChildModule =
					PlanningVariant.ChildRequestTemplate.ModuleCatalog.Modules.FindByPredicate(
						[&ChildIdentity](const FLayoutModuleSolveSnapshot& Module)
						{
							return Module.SnapshotId == ChildIdentity.ModuleSnapshotId;
						});
				if (ChildModule == nullptr)
				{
					continue;
				}
				for (const FLayoutCandidateVariantIdentity& ParentIdentity :
					ParentRestriction->AllowedCandidates)
				{
					const FLayoutModuleSolveSnapshot* ParentModule =
						ParentRequest.ModuleCatalog.Modules.FindByPredicate(
							[&ParentIdentity](const FLayoutModuleSolveSnapshot& Module)
							{
								return Module.SnapshotId == ParentIdentity.ModuleSnapshotId;
							});
					if (ParentModule == nullptr)
					{
						continue;
					}
					FExactCandidatePair Pair;
					Pair.Parent = ParentIdentity;
					Pair.Child = ChildIdentity;
					if (TryCertifyExactSharedSeamCandidatePair(
						*ParentModule,
						ParentIdentity.YawRotationSteps,
						*ChildModule,
						ChildIdentity.YawRotationSteps,
						Face.FaceDirection,
						Face.InterfaceFamily,
						ChildPlannedCell->Intent == ELayoutCellIntent::Entry,
						Pair.bParentCanOwn,
						Pair.bChildCanOwn,
						Pair.ParentEvidenceId,
						Pair.ChildEvidenceId,
						Pair.WitnessId,
						&ParentRequest,
						Face.ParentCell)
						&& TryGetSnapshotWorldFaceRule(
							*ChildModule,
							Face.FaceDirection,
							ChildIdentity.YawRotationSteps,
							Pair.ChildFaceRule))
					{
						FinalPairs.Add(MoveTemp(Pair));
					}
				}
			}
			if (FinalPairs.IsEmpty())
			{
				return false;
			}

			Face.bParentCanOwnSeam = false;
			Face.bChildCanOwnSeam = false;
			Face.ParentSeamCapabilityId = NAME_None;
			Face.ChildSeamCapabilityId = NAME_None;
			TArray<FString> PairWitnessParts;
			for (const FExactCandidatePair& Pair : FinalPairs)
			{
				Face.bParentCanOwnSeam |= Pair.bParentCanOwn;
				Face.bChildCanOwnSeam |= Pair.bChildCanOwn;
				if (Face.ParentSeamCapabilityId.IsNone()
					|| Pair.ParentEvidenceId.LexicalLess(Face.ParentSeamCapabilityId))
				{
					Face.ParentSeamCapabilityId = Pair.ParentEvidenceId;
				}
				if (Face.ChildSeamCapabilityId.IsNone()
					|| Pair.ChildEvidenceId.LexicalLess(Face.ChildSeamCapabilityId))
				{
					Face.ChildSeamCapabilityId = Pair.ChildEvidenceId;
				}
				PairWitnessParts.Add(Pair.WitnessId.ToString());
			}
			PairWitnessParts.Sort();
			Face.ConnectionTag = GetStablePrimaryFaceConnectionTag(
				FinalPairs[0].ChildFaceRule);
			Face.AllowedConnectionTags =
				FinalPairs[0].ChildFaceRule.GetEffectiveAllowedConnectionTags();
			Face.TraversalChannels = FinalPairs[0].ChildFaceRule.ConnectedTraversalChannels;
			Face.bRequireMatchingYawWithFilledNeighbor =
				FinalPairs[0].ChildFaceRule.bRequireMatchingYawWithFilledNeighbor;
			Face.ReciprocalDomainWitnessId = FLayoutId(*FString::Printf(
				TEXT("SeamDomain.%08X"),
				FCrc::StrCrc32(*FString::Printf(
					TEXT("%s|%s|%d|%s|%s"),
					*Face.ParentCell.ToString(),
					*Face.ChildLocalCell.ToString(),
					static_cast<int32>(Face.FaceDirection),
					*Face.InterfaceFamily.ToString(),
					*FString::Join(PairWitnessParts, TEXT("|"))))));
			return true;
		};

		TArray<FSharedParentChildFace> CertifiedFaces;
		for (const FSharedParentChildFace& ProvisionalFace : InOutSharedFaces)
		{
			const FLayoutChildStageMappedCell* MappedChildCell =
				StageMapping.Cells.FindByPredicate(
					[&](const FLayoutChildStageMappedCell& Candidate)
					{
						return Candidate.SourceChildCell == ProvisionalFace.ChildLocalCell;
					});
			const FLayoutPlannedCell* ChildPlannedCell =
				PlanningVariant.PlannedCells.FindByPredicate(
					[&](const FLayoutPlannedCell& Candidate)
					{
						return Candidate.Cell == ProvisionalFace.ChildLocalCell;
					});
			const FLayoutPlannedCell* ParentPlannedCell =
				ParentPlannedCells.FindByPredicate(
					[&](const FLayoutPlannedCell& Candidate)
					{
						return Candidate.Cell == ProvisionalFace.ParentCell;
					});
			if (MappedChildCell == nullptr
				|| ChildPlannedCell == nullptr
				|| ParentPlannedCell == nullptr)
			{
				continue;
			}

			const int32 ChildRestrictionIndex = InOutChildRestrictions.IndexOfByPredicate(
				[MappedChildCell](const FLayoutCellCandidateDomainRestriction& Restriction)
				{
					return Restriction.Cell == MappedChildCell->MappedChildCell;
				});
			if (ChildRestrictionIndex == INDEX_NONE)
			{
				continue;
			}

			const uint8 JunctionFaces = GetParentPerimeterJunctionFaces(ParentRequest,
				*ParentPlannedCell, ProvisionalFace, InOutSharedFaces);
			TArray<FExactCandidatePair> ExactPairs;
			const FLayoutCellCandidateDomainRestriction& CurrentChildRestriction =
				InOutChildRestrictions[ChildRestrictionIndex];
			const int32 ParentTopLevel = ParentTopLevelByXY.FindRef(
				FIntPoint(ProvisionalFace.ParentCell.X, ProvisionalFace.ParentCell.Y));
			for (const FLayoutCandidateVariantIdentity& ChildIdentity :
				CurrentChildRestriction.AllowedCandidates)
			{
				const FLayoutModuleSolveSnapshot* ChildModule =
					PlanningVariant.ChildRequestTemplate.ModuleCatalog.Modules.FindByPredicate(
						[&ChildIdentity](const FLayoutModuleSolveSnapshot& Module)
						{
							return Module.SnapshotId == ChildIdentity.ModuleSnapshotId;
						});
				if (ChildModule == nullptr)
				{
					continue;
				}

				for (const FLayoutModuleSolveSnapshot& ParentModule :
					ParentRequest.ModuleCatalog.Modules)
				{
					if (!DoesSnapshotMatchBoundaryPlannedCell(
						ParentModule,
						*ParentPlannedCell,
						ParentTopLevel, JunctionFaces != 0))
					{
						continue;
					}
					TArray<int32> ParentYaws = ParentModule.AllowedYawRotationSteps;
					if (ParentYaws.IsEmpty())
					{
						ParentYaws.Add(0);
					}
					for (const int32 ParentYaw : ParentYaws)
					{
						FExactCandidatePair Pair;
						Pair.Parent.ModuleSnapshotId = ParentModule.SnapshotId;
						Pair.Parent.YawRotationSteps = ParentYaw;
						Pair.Child = ChildIdentity;
						if (TryCertifyExactSharedSeamCandidatePair(
							ParentModule,
							ParentYaw,
							*ChildModule,
							ChildIdentity.YawRotationSteps,
							ProvisionalFace.FaceDirection,
							ProvisionalFace.InterfaceFamily,
							ChildPlannedCell->Intent == ELayoutCellIntent::Entry,
							Pair.bParentCanOwn,
							Pair.bChildCanOwn,
							Pair.ParentEvidenceId,
							Pair.ChildEvidenceId,
							Pair.WitnessId,
							&ParentRequest,
							ProvisionalFace.ParentCell))
						{
							Pair.bParentCanOwn &= CanOwnPerimeterJunction(ParentModule, ParentYaw, ProvisionalFace.InterfaceFamily, JunctionFaces);
							Pair.bChildCanOwn &= CanOwnPerimeterJunction(*ChildModule, ChildIdentity.YawRotationSteps, ProvisionalFace.InterfaceFamily, JunctionFaces);
							if (Pair.bParentCanOwn || Pair.bChildCanOwn) ExactPairs.Add(MoveTemp(Pair));
						}
					}
				}
			}
			if (ExactPairs.IsEmpty())
			{
				continue;
			}

			FLayoutCellCandidateDomainRestriction ChildSeamRestriction;
			ChildSeamRestriction.Cell = MappedChildCell->MappedChildCell;
			FLayoutCellCandidateDomainRestriction ParentSeamRestriction;
			ParentSeamRestriction.Cell = ProvisionalFace.ParentCell;
			ParentSeamRestriction.bTreatAsJunctionPlacementZone = JunctionFaces != 0;
			for (const FExactCandidatePair& Pair : ExactPairs)
			{
				if (!RestrictionContains(ChildSeamRestriction, Pair.Child))
				{
					ChildSeamRestriction.AllowedCandidates.Add(Pair.Child);
				}
				if (!RestrictionContains(ParentSeamRestriction, Pair.Parent))
				{
					ParentSeamRestriction.AllowedCandidates.Add(Pair.Parent);
				}
			}

			const FLayoutCellCandidateDomainRestriction SavedChildRestriction =
				InOutChildRestrictions[ChildRestrictionIndex];
			IntersectCandidateRestriction(
				InOutChildRestrictions[ChildRestrictionIndex],
				ChildSeamRestriction);
			if (InOutChildRestrictions[ChildRestrictionIndex].AllowedCandidates.IsEmpty())
			{
				InOutChildRestrictions[ChildRestrictionIndex] = SavedChildRestriction;
				continue;
			}

			int32 ParentRestrictionIndex = InOutParentRestrictions.IndexOfByPredicate(
				[&](const FLayoutCellCandidateDomainRestriction& Restriction)
				{
					return Restriction.Cell == ProvisionalFace.ParentCell;
				});
			const bool bHadParentRestriction = ParentRestrictionIndex != INDEX_NONE;
			FLayoutCellCandidateDomainRestriction SavedParentRestriction;
			if (bHadParentRestriction)
			{
				SavedParentRestriction = InOutParentRestrictions[ParentRestrictionIndex];
				IntersectCandidateRestriction(
					InOutParentRestrictions[ParentRestrictionIndex],
					ParentSeamRestriction);
				InOutParentRestrictions[ParentRestrictionIndex].bTreatAsJunctionPlacementZone |=
					ParentSeamRestriction.bTreatAsJunctionPlacementZone;
			}
			else
			{
				ParentSeamRestriction.RestrictionId = FLayoutId(*FString::Printf(
					TEXT("ParentSeam.%s.%d.%08X"),
					*ProvisionalFace.ParentCell.ToString(),
					static_cast<int32>(ProvisionalFace.FaceDirection),
					FCrc::StrCrc32(*ProvisionalFace.InterfaceFamily.ToString())));
				ParentRestrictionIndex = InOutParentRestrictions.Add(
					MoveTemp(ParentSeamRestriction));
			}
			if (InOutParentRestrictions[ParentRestrictionIndex].AllowedCandidates.IsEmpty())
			{
				InOutChildRestrictions[ChildRestrictionIndex] = SavedChildRestriction;
				if (bHadParentRestriction)
				{
					InOutParentRestrictions[ParentRestrictionIndex] = SavedParentRestriction;
				}
				else
				{
					InOutParentRestrictions.RemoveAt(ParentRestrictionIndex);
				}
				continue;
			}

			ExactPairs.RemoveAll(
				[&](const FExactCandidatePair& Pair)
				{
					return !RestrictionContains(
						InOutChildRestrictions[ChildRestrictionIndex],
						Pair.Child)
						|| !RestrictionContains(
							InOutParentRestrictions[ParentRestrictionIndex],
							Pair.Parent);
				});
			if (ExactPairs.IsEmpty())
			{
				InOutChildRestrictions[ChildRestrictionIndex] = SavedChildRestriction;
				if (bHadParentRestriction)
				{
					InOutParentRestrictions[ParentRestrictionIndex] = SavedParentRestriction;
				}
				else
				{
					InOutParentRestrictions.RemoveAt(ParentRestrictionIndex);
				}
				continue;
			}

			CertifiedFaces.Add(ProvisionalFace);
		}

		bool bFinalDomainsRemainReciprocal = true;
		for (FSharedParentChildFace& Face : CertifiedFaces)
		{
			if (!RefreshExactFaceEvidence(Face))
			{
				bFinalDomainsRemainReciprocal = false;
				break;
			}
		}
		if (!bFinalDomainsRemainReciprocal)
		{
			InOutParentRestrictions = BaseParentRestrictions;
			InOutChildRestrictions = BaseChildRestrictions;
			CertifiedFaces.Reset();
		}

		InOutSharedFaces = MoveTemp(CertifiedFaces);
		RefreshCertifiedSharedParentChildFaceRunLengths(InOutSharedFaces);
		if (InOutSharedFaces.IsEmpty())
		{
			return;
		}

		TArray<FString> CertificateParts;
		CertificateParts.Add(InOutCertificateId.ToString());
		for (const FSharedParentChildFace& Face : InOutSharedFaces)
		{
			CertificateParts.Add(BuildSharedParentChildFaceIdentityPart(Face));
		}
		CertificateParts.Sort();
		InOutCertificateId = FLayoutId(*FString::Printf(
			TEXT("ChildBoundary.%08X"),
			FCrc::StrCrc32(*FString::Join(CertificateParts, TEXT("|")))));
		for (FLayoutSolveBoundaryPoint& BoundaryPoint : InOutChildIncomingBoundaryPoints)
		{
			BoundaryPoint.CertifiedDomainCertificateId = InOutCertificateId;
		}
	}

	/** Returns whether one mapped child value preserves every parent-owned exclusive cell. */
	bool DoesMappedChildPlanAvoidParentExclusiveCells(
		const TArray<FLayoutPlannedCell>& ParentTranslatedChildCells,
		const TSet<FIntVector>& ParentChildExclusiveCells,
		const TSet<FIntVector>& RelocatableParentEntryCells,
		const bool bMayDisplaceRelocatableParentEntry)
	{
		return !ParentTranslatedChildCells.ContainsByPredicate(
			[&](const FLayoutPlannedCell& ChildCell)
			{
				return ParentChildExclusiveCells.Contains(ChildCell.Cell)
					&& (!bMayDisplaceRelocatableParentEntry
						|| !RelocatableParentEntryCells.Contains(ChildCell.Cell));
			});
	}

	/** Precomputes static child placement facts so recursive search only handles dynamic decisions. */
	void PrecomputeNormalizedChildPlacementCandidates(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ParentPlannedCellSet,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		const TSet<FIntVector>& ParentChildExclusiveCells,
		const TSet<FIntVector>& RelocatableParentEntryCells,
		const TMap<FIntPoint, int32>& ParentTopLevelByXY,
		const TMap<FIntPoint, int32>& ParentTopModuleLevelByXY,
		const int32 ParentMaxLevel,
		const FLayoutModuleCatalog& ParentModuleCatalog,
		const FLayoutChildCapabilityEnvelope& ParentCapabilityEnvelope,
		TArray<FPreparedChildPlacementSource>& InOutPreparedSources)
	{
		SCOPED_NAMED_EVENT(Layout_Recursive_StaticCandidates, FColor::Cyan);
		int32 BoundarySearchAttempts = 0;
		for (FPreparedChildPlacementSource& PreparedSource : InOutPreparedSources)
		{
			const bool bProvidesExteriorZoneFeature =
				PreparedSource.HardFeatureProviderChoices.ContainsByPredicate(
					[&ParentRequest](const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice)
					{
						const FLayoutZoneFeatureRequirement* Requirement =
							ParentRequest.ProfileSnapshot.ZoneFeatureRequirements.FindByPredicate(
								[&Choice](const FLayoutZoneFeatureRequirement& Candidate)
								{
									return Candidate.RequirementId == Choice.RequirementId;
								});
						return Requirement != nullptr
							&& (Requirement->Zone == ELayoutPlacementZone::Perimeter
								|| Requirement->Zone == ELayoutPlacementZone::Edge
								|| Requirement->Zone == ELayoutPlacementZone::Corner);
					});
			for (FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant : PreparedSource.PlanningVariants)
			{
				// Reuse immutable catalog/capability data; only candidate geometry changes per offset.
				FPreparedChildPlacementSource::FPlanningVariant MappedPlanningVariant;
				MappedPlanningVariant.ChildRequestTemplate = PlanningVariant.ChildRequestTemplate;
				MappedPlanningVariant.FootprintSize = PlanningVariant.FootprintSize;
				MappedPlanningVariant.CapabilityEnvelope = PlanningVariant.CapabilityEnvelope;
				MappedPlanningVariant.InterfaceSummary = PlanningVariant.InterfaceSummary;
				MappedPlanningVariant.YawRotationSteps = PlanningVariant.YawRotationSteps;
				MappedPlanningVariant.MaxLocalLevel = PlanningVariant.MaxLocalLevel;
				const int32 MaxCandidateOffsetZ = FMath::Max(0, ParentMaxLevel - PlanningVariant.MaxLocalLevel);
				for (int32 OffsetZ = 0; OffsetZ <= MaxCandidateOffsetZ; ++OffsetZ)
				{
					if (LayoutSolveExecution::ShouldStop()) return;
					// Base-slice policies depend on authored levels, not mapped XY/physical height.
					// Reject the whole impossible level before mapping and support certification.
					const ELayoutLevelPlacementPolicy LevelPolicy =
						PreparedSource.EntrySnapshot->ChildLevelPlacementPolicy;
					const bool bUsesBaseSlice = LevelPolicy == ELayoutLevelPlacementPolicy::GroundOnly
						|| LevelPolicy == ELayoutLevelPlacementPolicy::SpecificLevel
						|| LevelPolicy == ELayoutLevelPlacementPolicy::AboveGroundLevel;
					if (bUsesBaseSlice && !DoesMappedChildPlanMatchLevelPlacementPolicy(
						PlanningVariant.PlannedCells, ParentTopModuleLevelByXY, OffsetZ,
						LevelPolicy, PreparedSource.EntrySnapshot->ChildSpecificLevel))
					{
						const int32 RejectedOffsets = FMath::Max(0,
							ParentFootprintSize.X - PlanningVariant.FootprintSize.X + 1)
							* FMath::Max(0, ParentFootprintSize.Y - PlanningVariant.FootprintSize.Y + 1);
						PlanningVariant.StaticCandidateOffsetCount += RejectedOffsets;
						PlanningVariant.StaticLevelPlacementRejectCount += RejectedOffsets;
						continue;
					}
					for (int32 OffsetY = 0; OffsetY <= ParentFootprintSize.Y - PlanningVariant.FootprintSize.Y; ++OffsetY)
					{
						for (int32 OffsetX = 0; OffsetX <= ParentFootprintSize.X - PlanningVariant.FootprintSize.X; ++OffsetX)
						{
							if (LayoutSolveExecution::ShouldStop()) return;
							++PlanningVariant.StaticCandidateOffsetCount;
							const FIntVector CandidateOffset(OffsetX, OffsetY, OffsetZ);
							FLayoutChildStageMappingResult StageMapping;
							FString StageMappingFailureReason;
							if (!TryBuildChildStageMapping(
								ParentRequest,
								ParentPlannedCells,
								PlanningVariant.FootprintSize,
								CandidateOffset,
								PreparedSource.EntrySnapshot->EntryId,
								PlanningVariant.YawRotationSteps,
								PlanningVariant.ChildRequestTemplate.ProfileSnapshot.bSupportsSteppedTerrainSolve,
								PlanningVariant.PlannedCells,
								StageMapping,
								StageMappingFailureReason,
								&PlanningVariant.ChildRequestTemplate))
							{
								++PlanningVariant.StaticParentPlanRejectCount;
								++PlanningVariant.StaticStageMappingRejectCount;
								if (PlanningVariant.FirstStageMappingFailureReason.IsEmpty())
								{
									PlanningVariant.FirstStageMappingFailureReason =
										MoveTemp(StageMappingFailureReason);
								}
								continue;
							}
							if (!DoesMappedChildPlanAvoidParentExclusiveCells(
								StageMapping.ParentTranslatedPlannedCells,
								ParentChildExclusiveCells,
								RelocatableParentEntryCells,
								bProvidesExteriorZoneFeature))
							{
								++PlanningVariant.StaticParentPlanRejectCount;
								continue;
							}

							MappedPlanningVariant.PlannedCells = StageMapping.ChildLocalPlannedCells;
							MappedPlanningVariant.PlannedCellSet.Reset();
							for (int32 Index = 0; Index < StageMapping.Cells.Num(); ++Index)
							{
								MappedPlanningVariant.PlannedCells[Index].Cell = StageMapping.Cells[Index].SourceChildCell;
								MappedPlanningVariant.PlannedCellSet.Add(StageMapping.Cells[Index].SourceChildCell);
							}
							bool bFitsParentPlan = true;
							TArray<FSharedParentChildFace> CandidatePotentialSharedParentChildFaces;
							TSet<FIntVector> CandidatePotentialSharedParentChildCells;
							TSet<FIntVector> CandidateOverlappedParentCells;
							TSet<FIntPoint> ReservedFootprintColumns;
							for (int32 ChildCellIndex = 0; ChildCellIndex < MappedPlanningVariant.PlannedCells.Num(); ++ChildCellIndex)
							{
								const FLayoutPlannedCell& ChildPlannedCell = MappedPlanningVariant.PlannedCells[ChildCellIndex];
								const FIntVector ParentCell = StageMapping.ParentTranslatedPlannedCells[ChildCellIndex].Cell;
								if (!ParentPlannedCellSet.Contains(ParentCell))
								{
									// A taller child can own volume above the current parent plan while still
									// attaching through its lower parent-facing seam. Only cells inside the
									// authored parent plan become reserved/overlapped parent cells.
									// Mapping extends above each owning column, not above the highest
									// point anywhere in the parent. Internal holes remain forbidden.
									const int32* ColumnTop = ParentTopLevelByXY.Find(FIntPoint(ParentCell.X, ParentCell.Y));
									if (ColumnTop != nullptr && ParentCell.Z > *ColumnTop
										&& ParentCell.X >= 0
										&& ParentCell.Y >= 0
										&& ParentCell.X < ParentFootprintSize.X
										&& ParentCell.Y < ParentFootprintSize.Y)
									{
										continue;
									}

									if (PlanningVariant.FirstParentPlanFailureReason.IsEmpty())
										PlanningVariant.FirstParentPlanFailureReason = FString::Printf(
											TEXT("Mapped child volume rejected: offset=%s yaw=%d cell=%s columnTop=%d globalTop=%d authoredLevel=%d."),
											*CandidateOffset.ToString(), PlanningVariant.YawRotationSteps, *ParentCell.ToString(),
											ColumnTop != nullptr ? *ColumnTop : INDEX_NONE, ParentMaxLevel, ChildPlannedCell.ModuleLevelIndex);
									bFitsParentPlan = false;
									break;
								}

								CandidateOverlappedParentCells.Add(ParentCell);
								const ELayoutCellIntent* ParentIntent = ParentPlannedCellIntents.Find(ParentCell);
								if (ParentIntent == nullptr)
								{
									bFitsParentPlan = false;
									break;
								}

								const bool bParentIntentCanHostSharedShell =
									*ParentIntent == ELayoutCellIntent::Boundary
									// Upper-level recursive rooms can validly share retained parent shell
									// against generic covered parent walkable cells even when the parent
									// cell intent is not itself tagged Boundary yet. Without this, valid
									// multi-level child wall seams collapse before host-ascent ownership
									// proof ever sees the upper shared-shell surface.
									|| (ChildPlannedCell.Intent == ELayoutCellIntent::Boundary
										&& ChildPlannedCell.Cell.Z > 0
										&& (*ParentIntent == ELayoutCellIntent::Core
											|| *ParentIntent == ELayoutCellIntent::Interior
											|| *ParentIntent == ELayoutCellIntent::Connector));
												TArray<FSharedParentChildFace> SharedFacesForCell;
								if (bParentIntentCanHostSharedShell
									&& DoesChildCellSupportSharedBoundarySeam(
										ChildPlannedCell,
										CandidateOffset,
										PlanningVariant.FootprintSize,
										ParentFootprintSize,
										ParentCapabilityEnvelope,
										PlanningVariant.CapabilityEnvelope,
										SharedFacesForCell,
										&ParentPlannedCellSet,
										&MappedPlanningVariant.PlannedCellSet,
										&ParentCell))
								{
									// Keep the broader upper-level seam opportunity surface, but only
									// commit retained parent shell later if the overlapped parent cell can
									// still keep same-region structural support after the child reservation
									// settles. That prevents floating upper parent shell from being treated
									// as a valid generic shared seam.
													CandidatePotentialSharedParentChildCells.Add(ParentCell);
									CandidatePotentialSharedParentChildFaces.Append(SharedFacesForCell);
								}


								if (PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess
									&& PlanningVariant.InterfaceSummary.VerticalAccessCells.ContainsByPredicate(
										[&ChildPlannedCell](const FIntVector& VerticalAccessCell)
										{
											return VerticalAccessCell.X == ChildPlannedCell.Cell.X
												&& VerticalAccessCell.Y == ChildPlannedCell.Cell.Y;
										}))
								{
									ReservedFootprintColumns.Add(
										FIntPoint(
											ChildPlannedCell.Cell.X,
											ChildPlannedCell.Cell.Y));
								}
							}

							auto BuildCandidateReservedParentCells =
								[&](
									const TSet<FIntVector>& SharedParentChildCells,
									TSet<FIntVector>& OutReservedParentCells)
								{
									OutReservedParentCells.Reset();
									for (const FIntVector& OverlappedParentCell : CandidateOverlappedParentCells)
									{
										if (!SharedParentChildCells.Contains(OverlappedParentCell))
										{
											OutReservedParentCells.Add(OverlappedParentCell);
										}
									}

									if (!PreparedSource.EntrySnapshot->bChildContributesHostVerticalAccess)
									{
										return true;
									}

									const int32 ReservedTopLocalLevel =
										FMath::Max(PlanningVariant.MaxLocalLevel, 1);
									for (const FIntPoint& Column : ReservedFootprintColumns)
									{
										for (int32 LocalLevel = 0; LocalLevel <= ReservedTopLocalLevel; ++LocalLevel)
										{
											const FIntVector ParentCell(
												Column.X + CandidateOffset.X,
												Column.Y + CandidateOffset.Y,
												LocalLevel + CandidateOffset.Z);
											if (!ParentPlannedCellSet.Contains(ParentCell)
												|| SharedParentChildCells.Contains(ParentCell))
											{
												continue;
											}

											const ELayoutCellIntent* ParentIntent =
												ParentPlannedCellIntents.Find(ParentCell);
											if (ParentIntent == nullptr
												|| *ParentIntent == ELayoutCellIntent::Entry
												|| ParentChildExclusiveCells.Contains(ParentCell))
											{
												return false;
											}

											OutReservedParentCells.Add(ParentCell);
										}
									}

									return true;
								};

							if (bFitsParentPlan)
							{
								TSet<FIntVector> CandidateSharedParentChildCells =
									CandidatePotentialSharedParentChildCells;
								while (true)
								{
									TSet<FIntVector> CandidateReservedParentCells;
									if (!BuildCandidateReservedParentCells(
										CandidateSharedParentChildCells,
										CandidateReservedParentCells))
									{
										bFitsParentPlan = false;
										break;
									}

									TSet<FIntVector> SupportedSharedParentChildCells;
									TMap<FIntVector, bool> SameRegionSupportMemo;
									for (const FIntVector& SharedParentCell :
										CandidateSharedParentChildCells)
									{
										if (DoesParentCellKeepSameRegionSupportUnderReservation(
											SharedParentCell,
											ParentPlannedCellSet,
											CandidateReservedParentCells,
											SameRegionSupportMemo))
										{
											SupportedSharedParentChildCells.Add(SharedParentCell);
										}
									}

									bool bSharedCellSetStable =
										SupportedSharedParentChildCells.Num()
											== CandidateSharedParentChildCells.Num();
									if (bSharedCellSetStable)
									{
										for (const FIntVector& SharedParentCell :
											CandidateSharedParentChildCells)
										{
											if (!SupportedSharedParentChildCells.Contains(
												SharedParentCell))
											{
												bSharedCellSetStable = false;
												break;
											}
										}
									}
									if (bSharedCellSetStable)
									{
										CandidatePotentialSharedParentChildCells =
											MoveTemp(SupportedSharedParentChildCells);
										break;
									}

									CandidateSharedParentChildCells =
										MoveTemp(SupportedSharedParentChildCells);
								}
							}

							if (!bFitsParentPlan)
							{
								++PlanningVariant.StaticParentPlanRejectCount;
								continue;
							}

							CandidatePotentialSharedParentChildFaces.RemoveAll(
								[&](const FSharedParentChildFace& Face)
								{
									return !CandidatePotentialSharedParentChildCells.Contains(
										Face.ParentCell);
								});
							TArray<FSharedParentChildFace> CandidateSharedParentChildFaces =
								CandidatePotentialSharedParentChildFaces;

							TArray<FIntVector> CandidateParentPlanReservedCells;
							TSet<FIntVector> CandidateParentPlanReservedCellSet;

							if (!DoesMappedChildPlanMatchLevelPlacementPolicy(
								StageMapping.ParentTranslatedPlannedCells,
								ParentTopModuleLevelByXY,
								StageMapping.ParentRegionCellOffset.Z,
								PreparedSource.EntrySnapshot->ChildLevelPlacementPolicy,
								PreparedSource.EntrySnapshot->ChildSpecificLevel))
							{
								++PlanningVariant.StaticLevelPlacementRejectCount;
								continue;
							}

							TArray<FLayoutCellCandidateDomainRestriction> ParentDomainRestrictions;
							TArray<FLayoutCellCandidateDomainRestriction> ChildDomainRestrictions;
							TArray<FLayoutSolveBoundaryPoint> ChildIncomingBoundaryPoints;
							const TArray<FSharedParentChildFace> PotentialSharedParentChildFaces =
								CandidateSharedParentChildFaces;
							FLayoutId BoundaryCertificateId;
							const bool bMatchesZoneWithoutSharedSeam = DoesTranslatedChildPlanMatchPlacementZone(
								StageMapping.ParentTranslatedPlannedCells, FIntVector::ZeroValue,
								ParentFootprintSize, PreparedSource.EntrySnapshot->ChildPlacementZone);
							int32 ParentPerimeterClearance = MAX_int32;
							for (const FLayoutPlannedCell& Cell : StageMapping.ParentTranslatedPlannedCells)
							{
								ParentPerimeterClearance = FMath::Min(ParentPerimeterClearance,
									FMath::Min(FMath::Min(Cell.Cell.X, Cell.Cell.Y),
										FMath::Min(ParentFootprintSize.X - 1 - Cell.Cell.X, ParentFootprintSize.Y - 1 - Cell.Cell.Y)));
							}
							// Only a better diagnostic candidate needs expensive per-face rejection strings.
							const bool bRecordBoundaryFailure = PlanningVariant.FirstBoundaryDomainFailureReason.IsEmpty()
								|| (bMatchesZoneWithoutSharedSeam && !PlanningVariant.bBestBoundaryDomainFailureMatchesZoneWithoutSharedSeam)
								|| (bMatchesZoneWithoutSharedSeam == PlanningVariant.bBestBoundaryDomainFailureMatchesZoneWithoutSharedSeam
									&& (ParentPerimeterClearance > PlanningVariant.BestBoundaryDomainFailurePerimeterClearance
										|| (ParentPerimeterClearance == PlanningVariant.BestBoundaryDomainFailurePerimeterClearance
											&& PotentialSharedParentChildFaces.Num() < PlanningVariant.BestBoundaryDomainFailureSharedFaceCount)));
							FString BoundaryDomainFailureReason;
							if (!TryBuildCertifiedBoundaryDomains(
								ParentRequest,
								ParentPlannedCells,
								ParentFootprintSize,
								ParentTopModuleLevelByXY,
								MappedPlanningVariant,
								StageMapping,
								PreparedSource.EntrySnapshot->EntryId,
								PreparedSource.EntrySnapshot->ChildPlacementZone,
								PotentialSharedParentChildFaces,
								ParentDomainRestrictions,
								ChildDomainRestrictions,
								ChildIncomingBoundaryPoints,
								CandidateSharedParentChildFaces,
								BoundaryCertificateId,
								BoundarySearchAttempts,
								bRecordBoundaryFailure ? &BoundaryDomainFailureReason : nullptr))
							{
								++PlanningVariant.StaticParentPlanRejectCount;
								++PlanningVariant.StaticBoundaryDomainRejectCount;
								if (bRecordBoundaryFailure)
								{
									PlanningVariant.BestBoundaryDomainFailureSharedFaceCount =
										PotentialSharedParentChildFaces.Num();
									PlanningVariant.bBestBoundaryDomainFailureMatchesZoneWithoutSharedSeam =
										bMatchesZoneWithoutSharedSeam;
									PlanningVariant.BestBoundaryDomainFailurePerimeterClearance =
										ParentPerimeterClearance;
									PlanningVariant.FirstBoundaryDomainFailureReason = FString::Printf(
										TEXT("Joint boundary/seam domain produced no placement-zone-compatible value at offset %s from %d provisional shared faces.%s%s"),
										*CandidateOffset.ToString(),
										PotentialSharedParentChildFaces.Num(),
										BoundaryDomainFailureReason.IsEmpty() ? TEXT("") : TEXT("\n  First boundary-domain cause: "),
										*BoundaryDomainFailureReason);
								}
								continue;
							}
							const TArray<FLayoutCellCandidateDomainRestriction> BaseParentDomainRestrictions =
								ParentDomainRestrictions;
							const TArray<FLayoutCellCandidateDomainRestriction> BaseChildDomainRestrictions =
								ChildDomainRestrictions;
							const TArray<FLayoutSolveBoundaryPoint> BaseChildIncomingBoundaryPoints =
								ChildIncomingBoundaryPoints;
							const FLayoutId BaseBoundaryCertificateId = BoundaryCertificateId;
							TArray<FSharedParentChildFace> SupportEligibleSharedFaces =
								CandidateSharedParentChildFaces;
							TSet<FIntVector> CertifiedSharedParentChildCells;
							bool bExactReservationValid = true;
							while (true)
							{
								ParentDomainRestrictions = BaseParentDomainRestrictions;
								ChildDomainRestrictions = BaseChildDomainRestrictions;
								ChildIncomingBoundaryPoints = BaseChildIncomingBoundaryPoints;
								BoundaryCertificateId = BaseBoundaryCertificateId;
								CandidateSharedParentChildFaces = SupportEligibleSharedFaces;
								CertifyExactSharedParentChildFaces(
									ParentRequest,
									ParentPlannedCells,
									ParentTopModuleLevelByXY,
									MappedPlanningVariant,
									StageMapping,
									CandidateSharedParentChildFaces,
									ParentDomainRestrictions,
									ChildDomainRestrictions,
									BoundaryCertificateId,
									ChildIncomingBoundaryPoints);

								CertifiedSharedParentChildCells.Reset();
								for (const FSharedParentChildFace& SharedFace :
									CandidateSharedParentChildFaces)
								{
									CertifiedSharedParentChildCells.Add(SharedFace.ParentCell);
								}
								if (!BuildCandidateReservedParentCells(
									CertifiedSharedParentChildCells,
									CandidateParentPlanReservedCellSet))
								{
									bExactReservationValid = false;
									break;
								}

								TSet<FIntVector> SupportedCertifiedSharedCells;
								TMap<FIntVector, bool> SameRegionSupportMemo;
								for (const FIntVector& SharedParentCell :
									CertifiedSharedParentChildCells)
								{
									if (DoesParentCellKeepSameRegionSupportUnderReservation(
										SharedParentCell,
										ParentPlannedCellSet,
										CandidateParentPlanReservedCellSet,
										SameRegionSupportMemo))
									{
										SupportedCertifiedSharedCells.Add(SharedParentCell);
									}
								}
								if (SupportedCertifiedSharedCells.Num()
									== CertifiedSharedParentChildCells.Num())
								{
									break;
								}

								SupportEligibleSharedFaces.RemoveAll(
									[&](const FSharedParentChildFace& Face)
									{
										return !SupportedCertifiedSharedCells.Contains(
											Face.ParentCell);
									});
							}
							if (!bExactReservationValid)
							{
								++PlanningVariant.StaticParentPlanRejectCount;
								continue;
							}
							// Support filtering and exact recertification may remove optional seams,
							// but cannot silently discard either face of a required perimeter branch.
							for (const FSharedParentChildFace& RequiredFace : PotentialSharedParentChildFaces)
							{
								const FLayoutPlannedCell* ParentCell = ParentPlannedCells.FindByPredicate(
									[&](const auto& Cell) { return Cell.Cell == RequiredFace.ParentCell; });
								if (ParentCell == nullptr || GetParentPerimeterJunctionFaces(ParentRequest,
									*ParentCell, RequiredFace, PotentialSharedParentChildFaces) == 0) continue;
								if (!CandidateSharedParentChildFaces.ContainsByPredicate([&](const FSharedParentChildFace& Face)
								{
									return Face.ParentCell == RequiredFace.ParentCell && Face.ChildLocalCell == RequiredFace.ChildLocalCell
										&& Face.FaceDirection == RequiredFace.FaceDirection && Face.InterfaceFamily == RequiredFace.InterfaceFamily;
								}))
								{
									bExactReservationValid = false;
									PlanningVariant.FirstBoundaryDomainFailureReason = FString::Printf(
										TEXT("Required perimeter junction at %s lost a face during exact reservation certification."),
										*RequiredFace.ParentCell.ToString());
									break;
								}
							}
							if (!bExactReservationValid)
							{
								++PlanningVariant.StaticParentPlanRejectCount;
								++PlanningVariant.StaticBoundaryDomainRejectCount;
								continue;
							}
							if (!DoesTranslatedChildPlanMatchPlacementZone(
								StageMapping.ParentTranslatedPlannedCells,
								FIntVector::ZeroValue,
								ParentFootprintSize,
								PreparedSource.EntrySnapshot->ChildPlacementZone,
								&CertifiedSharedParentChildCells))
							{
								++PlanningVariant.StaticPlacementZoneRejectCount;
								continue;
							}
							CandidateParentPlanReservedCells.Reserve(
								CandidateParentPlanReservedCellSet.Num());
							for (const FIntVector& ReservedCell : CandidateParentPlanReservedCellSet)
							{
								CandidateParentPlanReservedCells.Add(ReservedCell);
							}

							if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
							{
								UE_LOG(LogTemp, Display,
									TEXT("[RecursiveStructuralValue] region=%s child=%s offset=%s zone=%d potentialSeams=%d certifiedSeams=%d reserved=%d certificate=%s"),
									*ParentRequest.RegionDebugPath,
									*PreparedSource.EntrySnapshot->EntryId.ToString(),
									*CandidateOffset.ToString(),
									static_cast<int32>(PreparedSource.EntrySnapshot->ChildPlacementZone),
									PotentialSharedParentChildFaces.Num(),
									CandidateSharedParentChildFaces.Num(),
									CandidateParentPlanReservedCellSet.Num(),
									*BoundaryCertificateId.ToString());
							}
							FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate& NormalizedCandidate =
								PlanningVariant.NormalizedCandidates.AddDefaulted_GetRef();
							NormalizedCandidate.Offset = CandidateOffset;
							NormalizedCandidate.bDisplacesRelocatableParentEntry =
								StageMapping.ParentTranslatedPlannedCells.ContainsByPredicate(
									[&RelocatableParentEntryCells](const FLayoutPlannedCell& PlannedCell)
									{
										return RelocatableParentEntryCells.Contains(PlannedCell.Cell);
									});
							NormalizedCandidate.StageMapping = MoveTemp(StageMapping);
							NormalizedCandidate.ParentPlanReservedCells = MoveTemp(CandidateParentPlanReservedCells);
							NormalizedCandidate.SharedParentChildFaces = MoveTemp(CandidateSharedParentChildFaces);
							NormalizedCandidate.ParentDomainRestrictions = MoveTemp(ParentDomainRestrictions);
							NormalizedCandidate.ChildDomainRestrictions = MoveTemp(ChildDomainRestrictions);
							NormalizedCandidate.ChildIncomingBoundaryPoints = MoveTemp(ChildIncomingBoundaryPoints);
							NormalizedCandidate.BoundaryCertificateId = BoundaryCertificateId;
							NormalizedCandidate.ParentOwnedSeamOpportunityScore =
								ScoreSharedParentChildSeamOpportunity(
									NormalizedCandidate.SharedParentChildFaces);

							for (const FIntVector& ChildEntryCell : PlanningVariant.InterfaceSummary.EntryCells)
							{
								for (const FLayoutChildCapabilityEndpoint& Capability : PlanningVariant.InterfaceSummary.EntryCapabilities)
								{
									if (Capability.LocalCell.Z != ChildEntryCell.Z)
									{
										continue;
									}

									if (Capability.FaceDirection == ELayoutFaceDirection::PosZ
										|| Capability.FaceDirection == ELayoutFaceDirection::NegZ)
									{
										continue;
									}
									const FLayoutChildStageMappedCell* CertifiedEntryCell =
										NormalizedCandidate.StageMapping.Cells.FindByPredicate(
											[&ChildEntryCell](const FLayoutChildStageMappedCell& Cell)
											{
												return Cell.SourceChildCell == ChildEntryCell;
											});
									const FLayoutCellCandidateDomainRestriction* ChildRestriction =
										CertifiedEntryCell != nullptr
											? NormalizedCandidate.ChildDomainRestrictions.FindByPredicate(
												[CertifiedEntryCell](const FLayoutCellCandidateDomainRestriction& Restriction)
												{
													return Restriction.Cell == CertifiedEntryCell->MappedChildCell;
												})
											: nullptr;
									const FLayoutCandidateVariantIdentity* CertifiedIdentity =
										ChildRestriction != nullptr && !ChildRestriction->AllowedCandidates.IsEmpty()
											? &ChildRestriction->AllowedCandidates[0]
											: nullptr;
									const FLayoutModuleSolveSnapshot* CertifiedModule =
										CertifiedIdentity != nullptr
											? PlanningVariant.ChildRequestTemplate.ModuleCatalog.Modules.FindByPredicate(
												[CertifiedIdentity](const FLayoutModuleSolveSnapshot& Module)
												{
													return Module.SnapshotId == CertifiedIdentity->ModuleSnapshotId;
												})
											: nullptr;
									FLayoutFaceRule CertifiedChildFaceRule;
									if (CertifiedModule == nullptr
										|| !DoesBoundaryCandidateSupportEntryCapability(
											*CertifiedModule,
											CertifiedIdentity->YawRotationSteps,
											Capability)
										|| !TryGetSnapshotWorldFaceRule(
											*CertifiedModule,
											Capability.FaceDirection,
											CertifiedIdentity->YawRotationSteps,
											CertifiedChildFaceRule))
									{
										continue;
									}

									const FIntVector ChildNeighborCell =
										ChildEntryCell + FLayoutDirectionUtils::ToCellDelta(Capability.FaceDirection);
									if (PlanningVariant.PlannedCellSet.Contains(ChildNeighborCell))
									{
										continue;
									}

									const FIntVector ParentEntryCell = MapSourceChildCellToParent(
										ChildEntryCell,
										CandidateOffset,
										&NormalizedCandidate.StageMapping);
									const FIntVector ParentContactCell =
										ParentEntryCell + FLayoutDirectionUtils::ToCellDelta(Capability.FaceDirection);
									const ELayoutCellIntent* ParentIntent = ParentPlannedCellIntents.Find(ParentContactCell);
									if (ParentIntent == nullptr
										|| GetParentContactIntentScore(*ParentIntent) <= (MIN_int32 / 8))
									{
										continue;
									}

									const ELayoutFaceDirection ParentFaceDirection =
										FLayoutDirectionUtils::GetOpposite(Capability.FaceDirection);
									const bool bRequiresFilledUpperNeighbor =
										ParentPlannedCellSet.Contains(ParentContactCell + FIntVector(0, 0, 1));
									FLayoutId ParentModuleSnapshotId;
									int32 ParentModuleYawRotationSteps = 0;
									const int32 ParentCapabilitySupportScore =
										ScoreParentModuleCatalogSupportForDirectContactCapability(
										ParentModuleCatalog,
										*ParentIntent,
										ParentFaceDirection,
										CertifiedChildFaceRule.ConnectionTag,
										CertifiedChildFaceRule.AllowedConnectionTags,
										CertifiedChildFaceRule.ConnectedTraversalChannels,
										bRequiresFilledUpperNeighbor,
										&ParentModuleSnapshotId,
										&ParentModuleYawRotationSteps);
									if (ParentCapabilitySupportScore <= 0)
									{
										continue;
									}

									FPreparedChildPlacementSource::FPlanningVariant::FNormalizedCandidate::FParentContactOpportunity& Opportunity =
										NormalizedCandidate.ParentContactOpportunities.AddDefaulted_GetRef();
									Opportunity.ChildEntryCell = ChildEntryCell;
									Opportunity.FaceDirection = Capability.FaceDirection;
									// Freeze contact data from the certified module/yaw rather than its coarse capability summary.
									Opportunity.ConnectionTag = CertifiedChildFaceRule.ConnectionTag;
									Opportunity.AllowedConnectionTags = CertifiedChildFaceRule.AllowedConnectionTags;
									Opportunity.TraversalChannels = CertifiedChildFaceRule.ConnectedTraversalChannels;
									Opportunity.ParentContactCell = ParentContactCell;
									Opportunity.ParentIntent = *ParentIntent;
									Opportunity.ParentCapabilitySupportScore = ParentCapabilitySupportScore;
									Opportunity.bProtectedTraversalCell = ParentProtectedTraversalCells.Contains(ParentContactCell);
									Opportunity.BoundaryWitnessId = FLayoutId(*FString::Printf(
										TEXT("%s.Boundary.%s.%d.%s.%s.%s.Yaw%d"),
										*PreparedSource.EntrySnapshot->EntryId.ToString(),
										*ChildEntryCell.ToString(),
										static_cast<int32>(Capability.FaceDirection),
										*ParentContactCell.ToString(),
										*Capability.CapabilityId.ToString(),
										*ParentModuleSnapshotId.ToString(),
										ParentModuleYawRotationSteps));
								}
							}

						}
					}
				}
			}
		}
	}

	/** Expands prepared child sources into concrete scheduler demands. */
	void BuildChildPlacementDemands(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FPreparedChildPlacementSource>& PreparedSources,
		const TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary>& ProviderChoices,
		TArray<FChildPlacementDemand>& OutPlacementDemands)
	{
		OutPlacementDemands.Reset();
		TSet<int32> HardGovernedSourceIndices;
		TArray<LayoutZoneFeatureDemand::FHardDemand> HardDemands;
		LayoutZoneFeatureDemand::CompileHardDemands(
			RootRequest.ProfileSnapshot.ZoneFeatureRequirements,
			HardDemands);
		for (const LayoutZoneFeatureDemand::FHardDemand& HardDemand : HardDemands)
		{
			if (HardDemand.MinCount <= 0)
			{
				continue;
			}

			TArray<int32> AlternativeSourceIndices;
			for (int32 SourceIndex = 0; SourceIndex < PreparedSources.Num(); ++SourceIndex)
			{
				if (PreparedSources[SourceIndex].HardFeatureProviderChoices.ContainsByPredicate(
					[&HardDemand](const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice)
					{
						return Choice.RequirementId == HardDemand.RequirementId;
					}))
				{
					AlternativeSourceIndices.Add(SourceIndex);
					HardGovernedSourceIndices.Add(SourceIndex);
				}
			}
			AlternativeSourceIndices.Sort([&PreparedSources](const int32 Left, const int32 Right)
			{
				return PreparedSources[Left].EntrySnapshot->EntryId.LexicalLess(
					PreparedSources[Right].EntrySnapshot->EntryId);
			});
			if (AlternativeSourceIndices.IsEmpty())
			{
				continue;
			}

			for (int32 InstanceOrdinal = 0;
				 InstanceOrdinal < HardDemand.MinCount;
				 ++InstanceOrdinal)
			{
				FChildPlacementDemand& Demand = OutPlacementDemands.AddDefaulted_GetRef();
				Demand.SourceIndex = AlternativeSourceIndices[0];
				Demand.AlternativeSourceIndices = AlternativeSourceIndices;
				Demand.InstanceOrdinal = InstanceOrdinal;
				Demand.bOptional = ProviderChoices.ContainsByPredicate(
					[&HardDemand](const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice)
					{
						return Choice.RequirementId == HardDemand.RequirementId
							&& Choice.ContentKind == ELayoutRegionContentKind::Module;
					});
				Demand.bSharedFeatureChoiceSlot = true;
				Demand.RequiredPlacementZone = HardDemand.Zone;
				const int32 CanonicalZoneComponentCount =
					HardDemand.Zone == ELayoutPlacementZone::Corner
						|| HardDemand.Zone == ELayoutPlacementZone::Edge
					? 4
					: 1;
				Demand.RequiredPlacementZoneComponentIndex =
					CanonicalZoneComponentCount > 1
						&& HardDemand.MinCount == CanonicalZoneComponentCount
					? InstanceOrdinal
					: INDEX_NONE;
				Demand.RequirementId = HardDemand.RequirementId;
				const bool bUsesUniqueProviderPath = AlternativeSourceIndices.Num() == 1;
				const FLayoutId PathSourceId = bUsesUniqueProviderPath
					? PreparedSources[AlternativeSourceIndices[0]].EntrySnapshot->EntryId
					: HardDemand.RequirementId;
				Demand.ChildRegionDebugPath = HardDemand.MinCount > 1
					? FString::Printf(
						TEXT("%s/%s_%d"),
						*RootRequest.RegionDebugPath,
						*PathSourceId.ToString(),
						InstanceOrdinal + 1)
					: FString::Printf(
						TEXT("%s/%s"),
						*RootRequest.RegionDebugPath,
						*PathSourceId.ToString());
			}
		}

		for (int32 SourceIndex = 0; SourceIndex < PreparedSources.Num(); ++SourceIndex)
		{
			const FPreparedChildPlacementSource& PreparedSource = PreparedSources[SourceIndex];
			check(PreparedSource.EntrySnapshot != nullptr);
			if (HardGovernedSourceIndices.Contains(SourceIndex))
			{
				continue;
			}

			const int32 InstanceCount = FMath::Max(1, PreparedSource.RequiredInstanceCount);
			for (int32 InstanceOrdinal = 0; InstanceOrdinal < InstanceCount; ++InstanceOrdinal)
			{
				FChildPlacementDemand& Demand = OutPlacementDemands.AddDefaulted_GetRef();
				Demand.SourceIndex = SourceIndex;
				Demand.InstanceOrdinal = InstanceOrdinal;
				Demand.bOptional = PreparedSource.EntrySnapshot->bChildOptional && PreparedSource.RequiredInstanceCount <= 1;
				Demand.ChildRegionDebugPath = InstanceCount > 1
					? FString::Printf(
						TEXT("%s/%s_%d"),
						*RootRequest.RegionDebugPath,
						*PreparedSource.EntrySnapshot->EntryId.ToString(),
						InstanceOrdinal + 1)
					: FString::Printf(
						TEXT("%s/%s"),
						*RootRequest.RegionDebugPath,
						*PreparedSource.EntrySnapshot->EntryId.ToString());
			}
		}
	}

	int32 EstimatePlacementDemandSearchDifficulty(
		const FPreparedChildPlacementSource& PreparedSource)
	{
		if (!PreparedSource.bPreparationSucceeded || PreparedSource.PlanningVariants.IsEmpty())
		{
			return MAX_int32;
		}

		int32 CandidateEstimate = 0;
		for (const FPreparedChildPlacementSource::FPlanningVariant& PlanningVariant : PreparedSource.PlanningVariants)
		{
			CandidateEstimate += PlanningVariant.NormalizedCandidates.Num();
		}

		return CandidateEstimate > 0 ? CandidateEstimate : MAX_int32 - 1;
	}

	/** Orders demands fail-first so tighter required children are scheduled before easier or optional ones. */
	void SortChildPlacementDemandsFailFirst(
		const TArray<FPreparedChildPlacementSource>& PreparedSources,
		TArray<FChildPlacementDemand>& InOutPlacementDemands)
	{
		InOutPlacementDemands.Sort(
			[&PreparedSources](const FChildPlacementDemand& Left, const FChildPlacementDemand& Right)
			{
				if (Left.bOptional != Right.bOptional)
				{
					return !Left.bOptional;
				}

				const FPreparedChildPlacementSource* LeftSource =
					PreparedSources.IsValidIndex(Left.SourceIndex) ? &PreparedSources[Left.SourceIndex] : nullptr;
				const FPreparedChildPlacementSource* RightSource =
					PreparedSources.IsValidIndex(Right.SourceIndex) ? &PreparedSources[Right.SourceIndex] : nullptr;
				const int32 LeftDifficulty =
					LeftSource != nullptr ? EstimatePlacementDemandSearchDifficulty(*LeftSource) : MAX_int32;
				const int32 RightDifficulty =
					RightSource != nullptr ? EstimatePlacementDemandSearchDifficulty(*RightSource) : MAX_int32;
				if (LeftDifficulty != RightDifficulty)
				{
					return LeftDifficulty < RightDifficulty;
				}

				if (Left.SourceIndex != Right.SourceIndex)
				{
					return Left.SourceIndex < Right.SourceIndex;
				}

				return Left.InstanceOrdinal < Right.InstanceOrdinal;
			});
	}

	/** Creates the mutable scheduler state after sources and demands are known. */
	FRecursiveChildPlacementSchedulerState InitializeRecursiveChildPlacementSchedulerState(
		const int32 PreparedSourceCount,
		FPlacementBridgeSharedMemoStore* SharedMemoStore = nullptr)
	{
		FRecursiveChildPlacementSchedulerState State;
		State.LastChosenPlacementBySource.SetNum(PreparedSourceCount);
		if (SharedMemoStore != nullptr)
		{
			State.SharedParentProbeSolveMemo = &SharedMemoStore->ParentProbeSolveMemo;
			State.SharedCompletePlacementValidationMemo = &SharedMemoStore->CompletePlacementValidationMemo;
		}
		return State;
	}

	/** Builds the reserved and protected parent cell sets used by complete-placement validation. */
	void BuildValidationChildReservationState(
		const TArray<FAutomaticChildPlacement>& CandidatePlacements,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		TSet<FIntVector>& OutChildReservedCells,
		TSet<FIntVector>& OutChildSupportingCells,
		TSet<FIntVector>& OutProtectedTraversalCells)
	{
		OutChildReservedCells.Reset();
		OutChildSupportingCells.Reset();
		OutProtectedTraversalCells = ParentProtectedTraversalCells;
		for (const FAutomaticChildPlacement& ChildPlacement : CandidatePlacements)
		{
			TSet<FIntVector> RetainedShellCells;
			for (const FLayoutNegotiatedLevelCellSet& LevelCells : ChildPlacement.NegotiatedResponsibilityContract.RetainedParentShellCellsByLevel)
			{
				for (const FIntVector& RetainedCell : LevelCells.Cells)
				{
					RetainedShellCells.Add(RetainedCell);
				}
			}

			bool bAddedReservedCellsFromNegotiatedContract = false;
			for (const FLayoutNegotiatedLevelCellSet& LevelCells : ChildPlacement.NegotiatedResponsibilityContract.ReplacementVolumeByLevel)
			{
				for (const FIntVector& ReservedCell : LevelCells.Cells)
				{
					if (RetainedShellCells.Contains(ReservedCell))
					{
						continue;
					}

					OutChildReservedCells.Add(ReservedCell);
					bAddedReservedCellsFromNegotiatedContract = true;
				}
			}

			if (!bAddedReservedCellsFromNegotiatedContract)
			{
				for (const FIntVector& ReservedCell : ChildPlacement.ParentPlanReservedCells)
				{
					OutChildReservedCells.Add(ReservedCell);
				}
			}
			if (!ChildPlacement.ParentTranslatedSupportingCells.IsEmpty())
			{
				for (const FIntVector& SupportingCell : ChildPlacement.ParentTranslatedSupportingCells)
				{
					OutChildSupportingCells.Add(SupportingCell);
				}
			}
			else
			{
				for (const FLayoutPlannedCell& PlannedCell : ChildPlacement.ParentTranslatedPlannedCells)
				{
					OutChildSupportingCells.Add(PlannedCell.Cell);
				}
			}
			for (const FIntVector& ParentContactCell : ChildPlacement.ParentCommittedEntryCells)
			{
				OutProtectedTraversalCells.Add(ParentContactCell);
			}
		}
	}

	void LogRecursiveSchedulerPerf(
		const FString& RegionDebugPath,
		const bool bSucceeded,
		const FRecursiveSchedulerPerfMetrics& PerfMetrics)
	{
		INC_DWORD_STAT_BY(STAT_PorismLayout_SearchStates, PerfMetrics.SearchStatesVisited);
		INC_DWORD_STAT_BY(STAT_PorismLayout_ParentFullProofCalls, PerfMetrics.ParentProbeFullSolveCalls);

		if (auto* Ledger = LayoutSolveExecution::CurrentThreadLedger(); Ledger && !Ledger->DiagnosticContext.IsEmpty())
		{
			const double Elapsed = FPlatformTime::Seconds() - PerfMetrics.StartSeconds;
			++Ledger->DiagnosticRegionCount;
			// Bound per-request output; keep the slowest summary even after detailed rows are suppressed.
			if (Ledger->DiagnosticRegionCount <= 16 || Elapsed > Ledger->DiagnosticSlowestRegionSeconds)
			{
				const FString Report = FString::Printf(
					TEXT("region=%s succeeded=%d inclusiveMs=%.3f setupMs=%.3f sourcesMs=%.3f normalizeMs=%.3f entryMs=%.3f validationPrepMs=%.3f validationSolveMs=%.3f states=%d probes=%d probeMemoHits=%d validations=%d validationMemoHits=%d repeatedParentRequests=%d repeatedCoarseKeys=%d used=%llu max=%llu"),
					*RegionDebugPath.Left(256), bSucceeded, Elapsed * 1000.0, PerfMetrics.SetupSeconds * 1000.0,
					PerfMetrics.PreparedSourceBuildSeconds * 1000.0, PerfMetrics.CandidateNormalizationSeconds * 1000.0,
					PerfMetrics.EntryNegotiationSeconds * 1000.0, PerfMetrics.CompleteValidationPrepSeconds * 1000.0,
					PerfMetrics.CompleteValidationSolveSeconds * 1000.0, PerfMetrics.SearchStatesVisited,
					PerfMetrics.ParentProbeCalls, PerfMetrics.ParentProbeMemoHits, PerfMetrics.CompleteValidationCalls,
					PerfMetrics.CompleteValidationMemoHits, PerfMetrics.CompleteValidationParentRequestRepeatCount,
					PerfMetrics.CompleteValidationCoarseKeyRepeatCount, Ledger->UsedWorkUnits, Ledger->MaxWorkUnits);
				if (Elapsed > Ledger->DiagnosticSlowestRegionSeconds)
				{
					Ledger->DiagnosticSlowestRegionSeconds = Elapsed;
					Ledger->DiagnosticSlowestRegionReport = Report;
				}
				if (Ledger->DiagnosticRegionCount <= 16)
					UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s phase=negotiation %s"), *Ledger->DiagnosticContext, *Report);
			}
			if (Ledger->DiagnosticRegionCount == 17)
				UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s phase=negotiation rowsSuppressed=1 slowestRetained=1"), *Ledger->DiagnosticContext);
		}
		static const bool bShouldLogPerf =
			!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty();
		if (!bShouldLogPerf)
		{
			return;
		}

		const double TotalSeconds = FPlatformTime::Seconds() - PerfMetrics.StartSeconds;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("[RecursiveSchedulerPerf] region=%s success=%d total=%.3fs setup=%.3fs prepared=%.3fs normalize=%.3fs demand=%.3fs entry=%.3fs entryOptions=%.3fs comboEnum=%.3fs entryResolve=%.3fs parentProbe=%.3fs structuralPrecheck=%.3fs parentSolve=%.3fs completeValidation=%.3fs states=%d probeCalls=%d probeMemoHits=%d probeFullSolves=%d structuralRejects=%d residualPlanReuses=%d combosEnumerated=%d combosStructuralRejected=%d combosFullProof=%d combosFullProofSurvived=%d successfulCombos=%d preparedStores=%d preparedReuses=%d"),
			*RegionDebugPath,
			bSucceeded ? 1 : 0,
			TotalSeconds,
			PerfMetrics.SetupSeconds,
			PerfMetrics.PreparedSourceBuildSeconds,
			PerfMetrics.CandidateNormalizationSeconds,
			PerfMetrics.DemandPreparationSeconds,
			PerfMetrics.EntryNegotiationSeconds,
			PerfMetrics.EntryOptionCollectionSeconds,
			PerfMetrics.CombinationEnumerationSeconds,
			PerfMetrics.EntryCommitmentResolutionSeconds,
			PerfMetrics.ParentProbeSeconds,
			PerfMetrics.ParentStructuralPrecheckSeconds,
			PerfMetrics.ParentProbeSolveSeconds,
			PerfMetrics.CompleteValidationSeconds,
			PerfMetrics.SearchStatesVisited,
			PerfMetrics.ParentProbeCalls,
			PerfMetrics.ParentProbeMemoHits,
			PerfMetrics.ParentProbeFullSolveCalls,
			PerfMetrics.ParentProbeStructuralRejects,
			PerfMetrics.ParentResidualPlanReuseCount,
			PerfMetrics.EnumeratedCommitmentCombinationCount,
			PerfMetrics.StructuralRejectedCommitmentCombinationCount,
			PerfMetrics.FullProofReachedCommitmentCombinationCount,
			PerfMetrics.FullProofSurvivedCommitmentCombinationCount,
			PerfMetrics.SuccessfulCommitmentCombinationCount,
			PerfMetrics.PreparedParentProofCandidateStoreCount,
			PerfMetrics.PreparedParentProofCandidateReuseCount);

		if (PerfMetrics.EnumeratedCommitmentCombinationCount <= 0
			&& PerfMetrics.CombinationFamilyStatsByKey.IsEmpty()
			&& PerfMetrics.ProofAttemptOffsetStats.IsEmpty()
			&& PerfMetrics.FullProofFailureStatsByKind.IsEmpty()
			&& PerfMetrics.CompleteValidationCalls <= 0)
		{
			return;
		}

		UE_LOG(
			LogTemp,
			Display,
			TEXT("[RecursiveSchedulerPerfDetail] region=%s enumerated=%d structuralRejected=%d fullProof=%d fullProofSurvived=%d optionCollection=%.6fs combinationEnumeration=%.6fs structuralPrecheck=%.6fs fullProofSolve=%.6fs completeValidationCalls=%d completeValidationMemoHits=%d completeValidationSplitCalls=%d completeValidationCoarseKeys=%d completeValidationCoarseRepeats=%d completeValidationPrep=%.6fs completeValidationSolve=%.6fs completeValidationAudit=%.6fs"),
			*RegionDebugPath,
			PerfMetrics.EnumeratedCommitmentCombinationCount,
			PerfMetrics.StructuralRejectedCommitmentCombinationCount,
			PerfMetrics.FullProofReachedCommitmentCombinationCount,
			PerfMetrics.FullProofSurvivedCommitmentCombinationCount,
			PerfMetrics.EntryOptionCollectionSeconds,
			PerfMetrics.CombinationEnumerationSeconds,
			PerfMetrics.ParentStructuralPrecheckSeconds,
			PerfMetrics.ParentProbeSolveSeconds,
			PerfMetrics.CompleteValidationCalls,
			PerfMetrics.CompleteValidationMemoHits,
			PerfMetrics.CompleteValidationIndependentSplitCalls,
			PerfMetrics.CompleteValidationCoarseKeyCount,
			PerfMetrics.CompleteValidationCoarseKeyRepeatCount,
			PerfMetrics.CompleteValidationPrepSeconds,
			PerfMetrics.CompleteValidationSolveSeconds,
			PerfMetrics.CompleteValidationAuditSeconds);

		if (PerfMetrics.CompleteValidationCalls > 0)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[RecursiveSchedulerPerfValidation] region=%s successes=%d parentRequestKeys=%d parentRequestRepeats=%d buildParentRequestFails=%d parentProofFails=%d parentAnchorFails=%d childBoundaryFails=%d childProofFails=%d independentBuildFails=%d scheduleSolveFails=%d mergedTraversalFails=%d splitParentSolve=%.6fs splitChildSolve=%.6fs"),
				*RegionDebugPath,
				PerfMetrics.CompleteValidationSuccesses,
				PerfMetrics.CompleteValidationParentRequestKeyCount,
				PerfMetrics.CompleteValidationParentRequestRepeatCount,
				PerfMetrics.CompleteValidationBuildParentRequestFailures,
				PerfMetrics.CompleteValidationParentProofFailures,
				PerfMetrics.CompleteValidationParentAnchorFailures,
				PerfMetrics.CompleteValidationChildBoundaryFailures,
				PerfMetrics.CompleteValidationChildProofFailures,
				PerfMetrics.CompleteValidationIndependentScheduleBuildFailures,
				PerfMetrics.CompleteValidationScheduleSolveFailures,
				PerfMetrics.CompleteValidationMergedTraversalFailures,
				PerfMetrics.CompleteValidationIndependentParentSolveSeconds,
				PerfMetrics.CompleteValidationIndependentChildSolveSeconds);
		}

		if (!PerfMetrics.CompleteValidationParentProofFailureStatsByFirstLine.IsEmpty())
		{
			struct FDeferredParentFailureLogRow
			{
				const FString* FailureFirstLine = nullptr;
				const FRecursiveSchedulerFullProofFailureStats* Stats = nullptr;
			};

			TArray<FDeferredParentFailureLogRow> DeferredParentFailureRows;
			DeferredParentFailureRows.Reserve(
				PerfMetrics.CompleteValidationParentProofFailureStatsByFirstLine.Num());
			for (const TPair<FString, FRecursiveSchedulerFullProofFailureStats>& Pair :
				PerfMetrics.CompleteValidationParentProofFailureStatsByFirstLine)
			{
				DeferredParentFailureRows.Add({&Pair.Key, &Pair.Value});
			}

			DeferredParentFailureRows.Sort(
				[](const FDeferredParentFailureLogRow& Left, const FDeferredParentFailureLogRow& Right)
				{
					if (Left.Stats->Count != Right.Stats->Count)
					{
						return Left.Stats->Count > Right.Stats->Count;
					}
					return *Left.FailureFirstLine < *Right.FailureFirstLine;
				});

			static constexpr int32 MaxLoggedDeferredParentFailureKinds = 5;
			for (int32 FailureIndex = 0;
				FailureIndex < DeferredParentFailureRows.Num() && FailureIndex < MaxLoggedDeferredParentFailureKinds;
				++FailureIndex)
			{
				const FDeferredParentFailureLogRow& Row = DeferredParentFailureRows[FailureIndex];
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[RecursiveSchedulerDeferredParentFailure] region=%s rank=%d count=%d sample=\"%s\""),
					*RegionDebugPath,
					FailureIndex + 1,
					Row.Stats->Count,
					*Row.Stats->SampleFirstLine);
			}
		}

		if (!PerfMetrics.StructuralRejectFailureStatsByFirstLine.IsEmpty())
		{
			struct FStructuralRejectLogRow
			{
				const FString* FailureFirstLine = nullptr;
				const FRecursiveSchedulerFullProofFailureStats* Stats = nullptr;
			};

			TArray<FStructuralRejectLogRow> StructuralRejectRows;
			StructuralRejectRows.Reserve(
				PerfMetrics.StructuralRejectFailureStatsByFirstLine.Num());
			for (const TPair<FString, FRecursiveSchedulerFullProofFailureStats>& Pair :
				PerfMetrics.StructuralRejectFailureStatsByFirstLine)
			{
				StructuralRejectRows.Add({&Pair.Key, &Pair.Value});
			}

			StructuralRejectRows.Sort(
				[](const FStructuralRejectLogRow& Left, const FStructuralRejectLogRow& Right)
				{
					if (Left.Stats->Count != Right.Stats->Count)
					{
						return Left.Stats->Count > Right.Stats->Count;
					}
					return *Left.FailureFirstLine < *Right.FailureFirstLine;
				});

			static constexpr int32 MaxLoggedStructuralRejectKinds = 5;
			for (int32 FailureIndex = 0;
				FailureIndex < StructuralRejectRows.Num()
				&& FailureIndex < MaxLoggedStructuralRejectKinds;
				++FailureIndex)
			{
				const FStructuralRejectLogRow& Row =
					StructuralRejectRows[FailureIndex];
				UE_LOG(
					LogTemp,
					Display,
					TEXT("[RecursiveSchedulerStructuralReject] region=%s rank=%d count=%d sample=\"%s\""),
					*RegionDebugPath,
					FailureIndex + 1,
					Row.Stats->Count,
					*Row.Stats->SampleFirstLine);
			}
		}

		struct FCombinationFamilyLogRow
		{
			const FRecursiveSchedulerCombinationFamilyStats* Stats = nullptr;
		};
		TArray<FCombinationFamilyLogRow> FamilyRows;
		FamilyRows.Reserve(PerfMetrics.CombinationFamilyStatsByKey.Num());
		for (const TPair<FRecursiveSchedulerCombinationFamilyKey, FRecursiveSchedulerCombinationFamilyStats>& Pair :
			PerfMetrics.CombinationFamilyStatsByKey)
		{
			FamilyRows.Add({&Pair.Value});
		}
		FamilyRows.Sort(
			[](const FCombinationFamilyLogRow& Left, const FCombinationFamilyLogRow& Right)
			{
				if (Left.Stats->FullProofAttemptCount != Right.Stats->FullProofAttemptCount)
				{
					return Left.Stats->FullProofAttemptCount > Right.Stats->FullProofAttemptCount;
				}
				if (Left.Stats->EnumeratedCount != Right.Stats->EnumeratedCount)
				{
					return Left.Stats->EnumeratedCount > Right.Stats->EnumeratedCount;
				}
				if (Left.Stats->EntryCount != Right.Stats->EntryCount)
				{
					return Left.Stats->EntryCount < Right.Stats->EntryCount;
				}
				if (Left.Stats->ParentContactCount != Right.Stats->ParentContactCount)
				{
					return Left.Stats->ParentContactCount < Right.Stats->ParentContactCount;
				}
				if (Left.Stats->DirectContactCount != Right.Stats->DirectContactCount)
				{
					return Left.Stats->DirectContactCount < Right.Stats->DirectContactCount;
				}
				if (Left.Stats->SiblingSharedDoorCount != Right.Stats->SiblingSharedDoorCount)
				{
					return Left.Stats->SiblingSharedDoorCount < Right.Stats->SiblingSharedDoorCount;
				}
				return Left.Stats->ParentSharedDoorCount < Right.Stats->ParentSharedDoorCount;
			});
		static constexpr int32 MaxLoggedRecursiveCombinationFamilies = 5;
		for (int32 FamilyIndex = 0;
			FamilyIndex < FamilyRows.Num() && FamilyIndex < MaxLoggedRecursiveCombinationFamilies;
			++FamilyIndex)
		{
			const FCombinationFamilyLogRow& Row = FamilyRows[FamilyIndex];
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[RecursiveSchedulerPerfFamily] region=%s rank=%d enumerated=%d structuralRejected=%d fullProof=%d fullProofSurvived=%d entries=%d parentContacts=%d direct=%d siblingDoor=%d parentDoor=%d"),
				*RegionDebugPath,
				FamilyIndex + 1,
				Row.Stats->EnumeratedCount,
				Row.Stats->StructuralRejectCount,
				Row.Stats->FullProofAttemptCount,
				Row.Stats->FullProofSuccessCount,
				Row.Stats->EntryCount,
				Row.Stats->ParentContactCount,
				Row.Stats->DirectContactCount,
				Row.Stats->SiblingSharedDoorCount,
				Row.Stats->ParentSharedDoorCount);
		}

		struct FProofOffsetLogRow
		{
			FIntVector Offset = FIntVector::ZeroValue;
			const FRecursiveSchedulerProofOffsetStats* Stats = nullptr;
		};
		TArray<FProofOffsetLogRow> OffsetRows;
		OffsetRows.Reserve(PerfMetrics.ProofAttemptOffsetStats.Num());
		for (const TPair<FIntVector, FRecursiveSchedulerProofOffsetStats>& Pair :
			PerfMetrics.ProofAttemptOffsetStats)
		{
			OffsetRows.Add({Pair.Key, &Pair.Value});
		}
		OffsetRows.Sort(
			[](const FProofOffsetLogRow& Left, const FProofOffsetLogRow& Right)
			{
				if (Left.Stats->FullProofAttemptCount != Right.Stats->FullProofAttemptCount)
				{
					return Left.Stats->FullProofAttemptCount > Right.Stats->FullProofAttemptCount;
				}
				if (Left.Stats->FullProofSuccessCount != Right.Stats->FullProofSuccessCount)
				{
					return Left.Stats->FullProofSuccessCount > Right.Stats->FullProofSuccessCount;
				}
				if (Left.Offset != Right.Offset)
				{
					if (Left.Offset.X != Right.Offset.X)
					{
						return Left.Offset.X < Right.Offset.X;
					}
					if (Left.Offset.Y != Right.Offset.Y)
					{
						return Left.Offset.Y < Right.Offset.Y;
					}
					return Left.Offset.Z < Right.Offset.Z;
				}
				return false;
			});
		static constexpr int32 MaxLoggedRecursiveProofOffsets = 5;
		for (int32 OffsetIndex = 0;
			OffsetIndex < OffsetRows.Num() && OffsetIndex < MaxLoggedRecursiveProofOffsets;
			++OffsetIndex)
		{
			const FProofOffsetLogRow& Row = OffsetRows[OffsetIndex];
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[RecursiveSchedulerPerfOffset] region=%s rank=%d offset=%s fullProof=%d fullProofSurvived=%d"),
				*RegionDebugPath,
				OffsetIndex + 1,
				*Row.Offset.ToString(),
				Row.Stats->FullProofAttemptCount,
				Row.Stats->FullProofSuccessCount);
		}

		struct FFullProofFailureLogRow
		{
			FLayoutId FailureKind = NAME_None;
			const FRecursiveSchedulerFullProofFailureStats* Stats = nullptr;
		};
		TArray<FFullProofFailureLogRow> FailureRows;
		FailureRows.Reserve(PerfMetrics.FullProofFailureStatsByKind.Num());
		for (const TPair<FLayoutId, FRecursiveSchedulerFullProofFailureStats>& Pair :
			PerfMetrics.FullProofFailureStatsByKind)
		{
			FailureRows.Add({Pair.Key, &Pair.Value});
		}
		FailureRows.Sort(
			[](const FFullProofFailureLogRow& Left, const FFullProofFailureLogRow& Right)
			{
				if (Left.Stats->Count != Right.Stats->Count)
				{
					return Left.Stats->Count > Right.Stats->Count;
				}
				return Left.FailureKind.LexicalLess(Right.FailureKind);
			});
		static constexpr int32 MaxLoggedRecursiveFullProofFailureKinds = 5;
		for (int32 FailureIndex = 0;
			FailureIndex < FailureRows.Num() && FailureIndex < MaxLoggedRecursiveFullProofFailureKinds;
			++FailureIndex)
		{
			const FFullProofFailureLogRow& Row = FailureRows[FailureIndex];
			UE_LOG(
				LogTemp,
				Display,
				TEXT("[RecursiveSchedulerPerfFullProofFailure] region=%s rank=%d kind=%s count=%d sample=\"%s\""),
				*RegionDebugPath,
				FailureIndex + 1,
				*Row.FailureKind.ToString(),
				Row.Stats->Count,
				*Row.Stats->SampleFirstLine);
		}
	}

	void LogRecursiveTreeSolvePerf(
		const FString& RegionDebugPath,
		const bool bSucceeded,
		const FRecursiveTreeSolvePerfMetrics& PerfMetrics)
	{
		(void)RegionDebugPath;
		(void)bSucceeded;
		(void)PerfMetrics;
	}

	bool TryBuildAutomaticChildPlacements(
		const FLayoutRegionSolveRequest& RootRequest,
		const FLayoutRegionSolveResult& RootPreparedTopology,
		const bool bStructuralWitnessOnly,
		TArray<FAutomaticChildPlacement>& OutChildPlacements,
		TArray<FLayoutValidationMessage>& OutWarnings,
		TArray<FSkippedOptionalChildPlacement>* OutSkippedOptionalPlacements,
		FCommittedVerticalAccessOwnership* OutVerticalAccessOwnership,
		TArray<FLayoutPartitionSeamRecord>* OutCommittedPartitionSeams,
		FLayoutRegionSolveRequest* OutPreparedParentProofRequest,
		bool* OutUsedIndependentDeferredProofSplit,
		FCertifiedRegionalProofResults* OutCertifiedRegionalProofResults,
		ELayoutSolvePreparationFailureKind* OutPreparationFailureKind,
		int32* OutPreparationCandidateAttemptCount,
		FLayoutRegionalFailureRecord* OutRegionalFailure,
		FPlacementBridgeSharedMemoStore* SharedMemoStore,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_RecursiveNegotiation, STAT_PorismLayout_RecursiveNegotiation);
		OutChildPlacements.Reset();
		OutWarnings.Reset();
		if (OutPreparationFailureKind != nullptr)
		{
			*OutPreparationFailureKind = ELayoutSolvePreparationFailureKind::None;
		}
		if (OutPreparationCandidateAttemptCount != nullptr)
		{
			*OutPreparationCandidateAttemptCount = 0;
		}
		if (OutRegionalFailure != nullptr)
		{
			*OutRegionalFailure = FLayoutRegionalFailureRecord();
		}
		if (OutSkippedOptionalPlacements != nullptr)
		{
			OutSkippedOptionalPlacements->Reset();
		}
		if (OutVerticalAccessOwnership != nullptr)
		{
			*OutVerticalAccessOwnership = FCommittedVerticalAccessOwnership();
		}
		if (OutCommittedPartitionSeams != nullptr)
		{
			OutCommittedPartitionSeams->Reset();
		}
		if (OutPreparedParentProofRequest != nullptr)
		{
			*OutPreparedParentProofRequest = FLayoutRegionSolveRequest();
		}
		if (OutUsedIndependentDeferredProofSplit != nullptr)
		{
			*OutUsedIndependentDeferredProofSplit = false;
		}
		if (OutCertifiedRegionalProofResults != nullptr)
		{
			*OutCertifiedRegionalProofResults = FCertifiedRegionalProofResults();
		}
		OutFailureReason.Reset();

		if (!ContentSetHasChildRegionEntries(RootRequest.ContentSetSnapshot))
		{
			return true;
		}

		LayoutSolveExecution::FScope ExecutionScope(RootRequest.ExecutionSettings.MaxSolveDurationSeconds, RootRequest.ExecutionSettings.MaxCandidateAttempts);
		if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
		FRecursiveSchedulerPerfMetrics PerfMetrics;
		const double SetupStartSeconds = FPlatformTime::Seconds();
		// Bookmarks carry identity; static timer names keep candidate counts from growing the timer registry.
		TRACE_BOOKMARK(TEXT("Layout_NegotiationBegin region=%s seed=%d structuralOnly=%d sparseRules=%d vaMode=%d vaExact=%d vaMin=%d vaMax=%d requiredTraversal=%d budgetSeconds=%.3f candidateBudget=%d"),
			*RootRequest.RegionDebugPath.Left(128), RootRequest.Seed, bStructuralWitnessOnly,
			RootRequest.ProfileSnapshot.SparsePlacementRules.Num(), static_cast<int32>(RootRequest.ProfileSnapshot.VerticalAccessCountMode),
			RootRequest.ProfileSnapshot.VerticalAccessCount, RootRequest.ProfileSnapshot.MinVerticalAccessCount,
			RootRequest.ProfileSnapshot.MaxVerticalAccessCount, RootRequest.ProfileSnapshot.bRequireAllTraversalChannelsReachable,
			RootRequest.ExecutionSettings.MaxSolveDurationSeconds, RootRequest.ExecutionSettings.MaxCandidateAttempts);
		ON_SCOPE_EXIT
		{
			TRACE_BOOKMARK(TEXT("Layout_NegotiationEnd region=%s seed=%d elapsedMs=%.3f children=%d failure=%s"),
				*RootRequest.RegionDebugPath.Left(128), RootRequest.Seed, (FPlatformTime::Seconds() - SetupStartSeconds) * 1000.0,
				OutChildPlacements.Num(), *OutFailureReason.Left(256));
			const auto& Work = *LayoutSolveExecution::CurrentThreadLedger();
			TRACE_BOOKMARK(TEXT("Layout_ExecutionWork region=%s used=%llu max=%llu hosts=%llu ranked=%llu entries=%llu entryNodes=%llu witnesses=%llu variants=%llu domains=%llu candidates=%llu variantReuses=%llu sparseWork=%llu"),
				*RootRequest.RegionDebugPath.Left(128), Work.UsedWorkUnits, Work.MaxWorkUnits, Work.HostAssignments,
				Work.RankedNodes, Work.EntryAssignments, Work.EntryNodes, Work.WitnessRefreshes,
				Work.VariantBuilds, Work.DomainBuilds, Work.CandidateAttempts, Work.VariantReuses, Work.SparseWork);
			static const bool bLogWork = !FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty();
			if (bLogWork)
			{
				UE_LOG(LogTemp, Display, TEXT("[LayoutExecutionWork] region=%s used=%llu max=%llu hosts=%llu ranked=%llu entries=%llu entryNodes=%llu witnesses=%llu variants=%llu domains=%llu candidates=%llu variantReuses=%llu sparseWork=%llu"),
					*RootRequest.RegionDebugPath.Left(128), Work.UsedWorkUnits, Work.MaxWorkUnits, Work.HostAssignments,
					Work.RankedNodes, Work.EntryAssignments, Work.EntryNodes, Work.WitnessRefreshes,
					Work.VariantBuilds, Work.DomainBuilds, Work.CandidateAttempts, Work.VariantReuses, Work.SparseWork);
			}
		};

		const TArray<FLayoutPlannedCell>& ParentPlannedCells =
			RootPreparedTopology.SolveResult.PlannedCells;
		const FIntPoint ParentFootprintSize =
			RootPreparedTopology.SolveResult.FootprintSize;
		if (ParentFootprintSize.X <= 0 || ParentFootprintSize.Y <= 0 || ParentPlannedCells.IsEmpty())
		{
			OutFailureReason = RootPreparedTopology.SolveResult.FailureReason.IsEmpty()
				? FString::Printf(TEXT("Region tree solve for '%s' could not derive valid prepared parent topology for child placement."), *RootRequest.RegionDebugPath)
				: RootPreparedTopology.SolveResult.FailureReason;
			return false;
		}

		TSet<FIntVector> ParentPlannedCellSet;
		TMap<FIntVector, ELayoutCellIntent> ParentPlannedCellIntents;
		TArray<FLayoutPlannedCell> StructuralParentPlannedCells = ParentPlannedCells;
		TMap<FIntVector, ELayoutCellIntent> StructuralParentPlannedCellIntents;
		TMap<FIntPoint, int32> ParentTopLevelByXY;
		TMap<FIntPoint, int32> ParentTopModuleLevelByXY;
		TArray<FIntVector> ParentPriorityTargets;
		TSet<FIntVector> ParentProtectedTraversalCells;
		int32 ParentMaxLevel = 0;
		for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
		{
			ParentPlannedCellSet.Add(PlannedCell.Cell);
			ParentPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
			int32& TopModuleLevel = ParentTopModuleLevelByXY.FindOrAdd(
				FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y),
				TNumericLimits<int32>::Lowest());
			TopModuleLevel = FMath::Max(TopModuleLevel, PlannedCell.ModuleLevelIndex);
			if (PlannedCell.Intent == ELayoutCellIntent::Entry || PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				ParentPriorityTargets.Add(PlannedCell.Cell);
			}
		}
		for (FLayoutPlannedCell& PlannedCell : StructuralParentPlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
			{
				PlannedCell.Intent = DetermineProvisionalParentIntent(
					PlannedCell.Cell,
					ParentFootprintSize);
			}
			StructuralParentPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
		}
		LayoutRegionScheduleSolverPrivate::CollectProtectedParentTraversalCells(
			ParentPlannedCells,
			ParentPlannedCellIntents,
			ParentProtectedTraversalCells);
		LayoutRegionScheduleSolverPrivate::AppendProtectedParentRouteReservationCells(
			RootPreparedTopology.SolveResult,
			ParentProtectedTraversalCells);
		RebuildTopPlannedLevelByXY(ParentPlannedCells, ParentTopLevelByXY, ParentMaxLevel);

		// Build a parent capability envelope so child placement can detect when a parent boundary cell
		// may be shared by seam ownership instead of always forcing the child off the perimeter.
		const FLayoutChildCapabilityEnvelope ParentCapabilityEnvelope = BuildChildCapabilityEnvelope(RootRequest);
		const TSet<ELayoutFaceDirection> ParentVerticalAccessIngressDirections =
			CollectVerticalAccessIngressDirections(RootRequest.ModuleCatalog);

		const TMap<FLayoutId, int32> RequiredInstancesByEntryId =
			BuildDefaultChildPlacementSourceCounts(RootRequest);
		PerfMetrics.SetupSeconds += (FPlatformTime::Seconds() - SetupStartSeconds);

		TArray<FPreparedChildPlacementSource> PreparedSources;
		TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary> ProviderChoices;
		if (!BuildPreparedChildPlacementSources(
			RootRequest,
			RequiredInstancesByEntryId,
			PreparedSources,
			ProviderChoices,
			&PerfMetrics,
			OutFailureReason))
		{
			return false;
		}

		FLayoutRegionSolveRequest PlacementRootRequest = RootRequest;
		if (PlacementRootRequest.VerticalAccessHostGroups.IsEmpty()
			&& PlacementRootRequest.ProfileSnapshot.LevelCount > 1
			&& PlacementRootRequest.ProfileSnapshot.VerticalAccessCountMode
				!= ELayoutCountConstraintMode::None)
		{
			SCOPED_NAMED_EVENT(Layout_Recursive_VerticalAccessPreparation, FColor::Cyan);
			TArray<FLayoutPlannedCell> HostPlanningCells = StructuralParentPlannedCells;
			if (!LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
					PlacementRootRequest,
					HostPlanningCells,
					PlacementRootRequest.VerticalAccessHostGroups,
					OutFailureReason))
			{
				return false;
			}
		}
		const LayoutRegionScheduleSolverFacade::FRecursiveScheduleSolveContext
			PreparedSolveContext = LayoutRegionScheduleSolverFacade::BuildSolveContext(
				PlacementRootRequest);
		const LayoutRegionScheduleSolverFacade::FCompiledStructuralInputs
			PreparedStructuralInputs = LayoutRegionScheduleSolverFacade::BuildCompiledStructuralInputs(
				PreparedSolveContext);
		TSet<FIntVector> ParentChildExclusiveCells;
		TSet<FIntVector> RelocatableParentEntryCells;
		for (const FLayoutPlannedCell& ParentPlannedCell : ParentPlannedCells)
		{
			if (ParentPlannedCell.Intent != ELayoutCellIntent::Entry)
			{
				continue;
			}
			ParentChildExclusiveCells.Add(ParentPlannedCell.Cell);
			const bool bPhysicalExterior = ParentPlannedCell.Cell.X == 0
				|| ParentPlannedCell.Cell.Y == 0
				|| ParentPlannedCell.Cell.X == ParentFootprintSize.X - 1
				|| ParentPlannedCell.Cell.Y == ParentFootprintSize.Y - 1;
			if (ParentPlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary
				|| (ParentPlannedCell.EntryOrigin == ELayoutEntryOrigin::None
					&& bPhysicalExterior))
			{
				RelocatableParentEntryCells.Add(ParentPlannedCell.Cell);
			}
		}
		for (const LayoutRegionScheduleSolverFacade::FCompiledReservedOpenOverlay& Overlay :
			PreparedStructuralInputs.RootReservedOpenOverlays)
		{
			for (const FIntVector& ReservedOpenCell : Overlay.ReservedCells)
			{
				ParentChildExclusiveCells.Add(ReservedOpenCell);
			}
		}
		for (const FLayoutCellReservationRecord& ReservedOpenReservation :
			RootRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations)
		{
			ParentChildExclusiveCells.Add(ReservedOpenReservation.Cell);
		}
		const double CandidateNormalizationStartSeconds = FPlatformTime::Seconds();
		PrecomputeNormalizedChildPlacementCandidates(
			RootRequest,
			StructuralParentPlannedCells,
			ParentFootprintSize,
			ParentPlannedCellSet,
			StructuralParentPlannedCellIntents,
			ParentProtectedTraversalCells,
			ParentChildExclusiveCells,
			RelocatableParentEntryCells,
			ParentTopLevelByXY,
			ParentTopModuleLevelByXY,
			ParentMaxLevel,
			RootRequest.ModuleCatalog,
			ParentCapabilityEnvelope,
			PreparedSources);
		PerfMetrics.CandidateNormalizationSeconds += (FPlatformTime::Seconds() - CandidateNormalizationStartSeconds);

		const double DemandPreparationStartSeconds = FPlatformTime::Seconds();
		TArray<FChildPlacementDemand> PlacementDemands;
		BuildChildPlacementDemands(
			RootRequest,
			PreparedSources,
			ProviderChoices,
			PlacementDemands);
		SortChildPlacementDemandsFailFirst(
			PreparedSources,
			PlacementDemands);
		PerfMetrics.DemandPreparationSeconds += (FPlatformTime::Seconds() - DemandPreparationStartSeconds);

		FRecursiveChildPlacementSchedulerState SchedulerState =
			InitializeRecursiveChildPlacementSchedulerState(
				PreparedSources.Num(),
				SharedMemoStore);
		SchedulerState.PerfMetrics = PerfMetrics;
		bool bFoundPlacementSolution = false;
		FDeferredCompletePlacementValidator CompletePlacementValidator
		{
			PlacementRootRequest,
			ParentPlannedCells,
			ParentPlannedCellSet,
			ParentPlannedCellIntents,
			ParentFootprintSize,
			ParentProtectedTraversalCells,
			ParentVerticalAccessIngressDirections,
			SchedulerState,
			LayoutSolveExecution::CurrentThreadLedger()->DeadlineSeconds
		};
		FRecursivePlacementDemandSearcher PlacementSearcher
		{
			PlacementRootRequest,
			ParentPlannedCells,
			ParentFootprintSize,
			ParentPlannedCellSet,
			ParentPlannedCellIntents,
			ParentPriorityTargets,
			ParentProtectedTraversalCells,
			ParentVerticalAccessIngressDirections,
			PreparedSolveContext,
			PreparedStructuralInputs,
			PlacementDemands,
			PreparedSources,
			SchedulerState,
			GetDeferredValidationCandidateCap(PlacementDemands.Num())
		};

		const bool bFoundAnyStructuralPlacementCandidate =
			PlacementSearcher.Search(0, 0, SchedulerState.SearchFailureReason);
		if (bFoundAnyStructuralPlacementCandidate)
		{
			static const bool bTrackDetailedRecursivePerf =
				!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty();
			FDeferredValidationCandidateOrderingSummary DeferredOrderingSummary;
			FDeferredValidationCandidateOrderingSummary* DeferredOrderingSummaryPtr =
				bTrackDetailedRecursivePerf ? &DeferredOrderingSummary : nullptr;
			FDeferredPlacementResolutionResult ResolutionResult;
			if (bStructuralWitnessOnly)
			{
				const auto ValidateStructuralWitness = [
					&RootRequest,
					&ParentPlannedCells,
					&ParentPlannedCellSet,
					&ParentPlannedCellIntents,
					ParentFootprintSize,
					&ParentProtectedTraversalCells,
					&ParentVerticalAccessIngressDirections](
						FCommittedRecursiveScheduleState& CandidateSchedule,
						FString& OutValidationFailureReason)
				{
					SCOPED_NAMED_EVENT(Layout_Recursive_StructuralWitnessValidation, FColor::Orange);
					TRACE_BOOKMARK(TEXT("Layout_StructuralBegin region=%s children=%d seams=%d"),
						*RootRequest.RegionDebugPath.Left(128), CandidateSchedule.Placements.Num(), CandidateSchedule.PlannedPartitionSeams.Num());
					ON_SCOPE_EXIT
					{
						TRACE_BOOKMARK(TEXT("Layout_StructuralEnd region=%s stopped=%d failure=%s"),
							*RootRequest.RegionDebugPath.Left(128), LayoutSolveExecution::ShouldStop(), *OutValidationFailureReason.Left(256));
					};
					if (!LayoutSolveExecution::Checkpoint(OutValidationFailureReason)) return false;
					for (FAutomaticChildPlacement& Placement : CandidateSchedule.Placements)
					{
						if (!FinalizeMappedChildVerticalAccess(Placement, OutValidationFailureReason)) return false;
					}
					FLayoutRegionSolveRequest ParentRequest;
					FCommittedVerticalAccessOwnership EffectiveOwnership;
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
							true,
							CandidateSchedule.VerticalAccessOwnership,
							OutValidationFailureReason,
							ParentRequest,
							&EffectiveOwnership))
					{
						return false;
					}
					if (!ApplyOwnedSeamUsageRestrictions(
							RootRequest.RegionDebugPath,
							CandidateSchedule.PlannedPartitionSeams,
							ParentRequest,
							CandidateSchedule.Placements,
							OutValidationFailureReason))
					{
						return false;
					}

					if (!TryFreezeParentAuthorityForPlacements(
							RootRequest, CandidateSchedule.Placements, ParentRequest,
							EffectiveOwnership, OutValidationFailureReason))
					{
						OutValidationFailureReason += FString::Printf(
							TEXT(" Ownership=%d countedParent=%d countedChildren=%d hostGroups=%d placements=[%s] seams=[%s]."),
							static_cast<int32>(EffectiveOwnership.OwnerKind),
							EffectiveOwnership.CountedParentVerticalAccessCells.Num(),
							EffectiveOwnership.CountedChildRegionDebugPaths.Num(),
							ParentRequest.VerticalAccessHostGroups.Num(),
							*FString::JoinBy(
								CandidateSchedule.Placements,
								TEXT("|"),
								[](const FAutomaticChildPlacement& Placement)
								{
									return FString::Printf(
										TEXT("%s@%s reserved=%d mapped=%d"),
										*Placement.ChildRegionDebugPath,
										*Placement.RegionCellOffset.ToString(),
										Placement.ParentPlanReservedCells.Num(),
										Placement.ParentTranslatedPlannedCells.Num());
								}),
							*FString::JoinBy(
								CandidateSchedule.PlannedPartitionSeams,
								TEXT("|"),
								[](const FLayoutPartitionSeamRecord& Seam)
								{
									return FString::Printf(
										TEXT("%s:%s:%s-%s"),
										*Seam.OwnerRegionDebugPath,
										*Seam.InterfaceFamily.ToString(),
										*Seam.OwnerStartCell.ToString(),
										*Seam.OwnerEndCell.ToString());
								}));
						return false;
					}
					if (!LayoutSolveExecution::Checkpoint(OutValidationFailureReason)) return false;
					CandidateSchedule.VerticalAccessOwnership = EffectiveOwnership;
					CandidateSchedule.PreparedParentProofRequest = MoveTemp(ParentRequest);
					CandidateSchedule.bHasPreparedParentProofRequest = true;
					OutValidationFailureReason.Reset();
					return true;
				};
				ResolutionResult = ResolveDeferredCompletePlacementCandidates(
					RootRequest.RegionDebugPath,
					SchedulerState.DeferredCompletePlacementCandidates,
					GetDeferredValidationCandidateCap(PlacementDemands.Num()),
					DeferredOrderingSummaryPtr,
					DeferredOrderingSummaryPtr != nullptr
						? &SchedulerState.PerfMetrics
						: nullptr,
					ValidateStructuralWitness);
			}
			else
			{
				ResolutionResult = ResolveDeferredCompletePlacementCandidates(
					RootRequest.RegionDebugPath,
					SchedulerState.DeferredCompletePlacementCandidates,
					GetDeferredValidationCandidateCap(PlacementDemands.Num()),
					DeferredOrderingSummaryPtr,
					DeferredOrderingSummaryPtr != nullptr
						? &SchedulerState.PerfMetrics
						: nullptr,
					CompletePlacementValidator);
			}
			if (DeferredOrderingSummaryPtr != nullptr)
			{
				LogDeferredValidationCandidateOrderingProfile(
					RootRequest.RegionDebugPath,
					DeferredOrderingSummary);
			}
			if (ResolutionResult.bSucceeded)
			{
				bFoundPlacementSolution = true;
				SchedulerState.BestSchedule = MoveTemp(ResolutionResult.BestSchedule);
			}
			else if (SchedulerState.SearchFailureReason.IsEmpty() && !ResolutionResult.FailureReason.IsEmpty())
			{
				SchedulerState.SearchFailureReason = ResolutionResult.FailureReason;
			}
		}

		if (!bFoundPlacementSolution)
		{
			LogRecursiveSchedulerPerf(RootRequest.RegionDebugPath, false, SchedulerState.PerfMetrics);
			if (OutPreparationFailureKind != nullptr)
			{
				*OutPreparationFailureKind = SchedulerState.PreparationFailureKind;
			}
			if (OutPreparationCandidateAttemptCount != nullptr)
			{
				*OutPreparationCandidateAttemptCount =
					SchedulerState.PreparationCandidateAttemptCount;
			}
			OutFailureReason = SchedulerState.SearchFailureReason;
			if (OutRegionalFailure != nullptr)
			{
				*OutRegionalFailure = SchedulerState.FirstCausalFailure;
				if (SchedulerState.BestDownstreamFailure.IsSet())
				{
					OutRegionalFailure->DownstreamCause =
						SchedulerState.BestDownstreamFailure.FirstCause;
				}
			}
			return false;
		}

		LogRecursiveSchedulerPerf(RootRequest.RegionDebugPath, true, SchedulerState.PerfMetrics);

		OutChildPlacements = MoveTemp(SchedulerState.BestSchedule.Placements);
		SortAutomaticChildPlacementsForDeterminism(OutChildPlacements);
		OutWarnings = MoveTemp(SchedulerState.BestSchedule.Warnings);
		if (OutSkippedOptionalPlacements != nullptr)
		{
			*OutSkippedOptionalPlacements =
				MoveTemp(SchedulerState.BestSchedule.SkippedOptionalPlacements);
			SortSkippedOptionalChildPlacementsForDeterminism(
				*OutSkippedOptionalPlacements);
		}
		if (OutVerticalAccessOwnership != nullptr)
		{
			*OutVerticalAccessOwnership =
				SchedulerState.BestSchedule.VerticalAccessOwnership;
		}
		if (OutCommittedPartitionSeams != nullptr)
		{
			*OutCommittedPartitionSeams =
				MoveTemp(SchedulerState.BestSchedule.PlannedPartitionSeams);
		}
		if (OutPreparedParentProofRequest != nullptr
			&& SchedulerState.BestSchedule.bHasPreparedParentProofRequest)
		{
			*OutPreparedParentProofRequest =
				SchedulerState.BestSchedule.PreparedParentProofRequest;
		}
		if (OutUsedIndependentDeferredProofSplit != nullptr)
		{
			*OutUsedIndependentDeferredProofSplit = SchedulerState.BestSchedule.bUsedIndependentDeferredProofSplit;
		}
		if (OutCertifiedRegionalProofResults != nullptr
			&& SchedulerState.BestSchedule.CertifiedRegionalProofResults.IsSet())
		{
			*OutCertifiedRegionalProofResults =
				MoveTemp(SchedulerState.BestSchedule.CertifiedRegionalProofResults);
		}
		return true;
	}

	bool CanDropOptionalChildRegion(
		const FString& RegionPath,
		const FScheduledChildSourceInfo& SourceInfo,
		const TMap<FString, FLayoutDirectChildRegionCommitment>& DirectChildCommitmentsByChild,
		const TMap<FString, TSet<FString>>& DependentsByRegion,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		FString& OutFailureReason)
	{
		if (!SourceInfo.bResolved || !SourceInfo.bOptional)
		{
			return false;
		}

		if (DirectChildCommitmentsByChild.Contains(RegionPath))
		{
			OutFailureReason = FString::Printf(
				TEXT("Optional child region '%s' cannot be dropped because it already has hard committed anchors."),
				*RegionPath);
			return false;
		}

		if (const TSet<FString>* Dependents = DependentsByRegion.Find(RegionPath))
		{
			if (!Dependents->IsEmpty())
			{
				OutFailureReason = FString::Printf(
					TEXT("Optional child region '%s' cannot be dropped because other scheduled regions depend on it."),
					*RegionPath);
				return false;
			}
		}

		const bool bParticipatesInSeam = PlannedPartitionSeams.ContainsByPredicate([&RegionPath](const FLayoutPartitionSeamRecord& SeamRecord)
		{
			return SeamRecord.OwnerRegionDebugPath == RegionPath
				|| SeamRecord.PassiveRegionDebugPath == RegionPath;
		});
		if (bParticipatesInSeam)
		{
			OutFailureReason = FString::Printf(
				TEXT("Optional child region '%s' cannot be dropped after seam planning because it already participates in a shared seam contract."),
				*RegionPath);
			return false;
		}

		return true;
	}

	FLayoutDroppedOptionalChildRecord BuildDroppedOptionalChildRecord(
		const FLayoutRegionSolveRequest& Request,
		const FScheduledChildSourceInfo& SourceInfo,
		const FLayoutSolveResult& FailedSolveResult)
	{
		FLayoutDroppedOptionalChildRecord Record;
		Record.DropDecisionId = FLayoutId(*FString::Printf(TEXT("%s.OptionalDrop"), *Request.RegionDebugPath));
		Record.ParentRegionDebugPath = SourceInfo.ParentRegionDebugPath;
		Record.ChildRegionDebugPath = Request.RegionDebugPath;
		Record.SourceContentEntryId = SourceInfo.ContentEntryId;
		Record.ChildProfileSnapshotId = Request.ProfileSnapshot.SnapshotId;
		Record.DroppedPlannedCells = Request.PlannedCells;
		Record.RelatedIds = {
			Request.EffectiveSnapshotId,
			Request.ProfileSnapshot.SnapshotId,
			Request.ContentSetSnapshot.SnapshotId
		};
		if (SourceInfo.ContentEntryId != NAME_None)
		{
			Record.RelatedIds.Add(SourceInfo.ContentEntryId);
		}
		Record.FailureReason = FailedSolveResult.FailureReason.IsEmpty()
			? FString::Printf(TEXT("Optional child region '%s' was dropped after a failed child solve."), *Request.RegionDebugPath)
			: FailedSolveResult.FailureReason;
		return Record;
	}

FLayoutRegionSolveScheduleResult LayoutRegionScheduleSolverPrivate::SolveRegionsSynchronouslyInternal(const FLayoutRegionSolveScheduleRequest& ScheduleRequest)
{
	FLayoutRegionSolveScheduleResult ScheduleResult;
	ScheduleResult.PlannedJunctionRequirements = ScheduleRequest.PlannedJunctionRequirements;
	for (const FLayoutDirectChildRegionCommitment& Commitment : ScheduleRequest.DirectChildCommitments)
	{
		if (Commitment.StageMapping.IsValid())
		{
			ScheduleResult.ChildStageMappings.Add(Commitment.StageMapping);
			ScheduleResult.MergedSolveResult.ChildStageMappingIds.AddUnique(
				Commitment.StageMapping.MappingId);
		}
	}
	ScheduleResult.ChildStageMappings.Sort([](
		const FLayoutChildStageMappingResult& Left,
		const FLayoutChildStageMappingResult& Right)
	{
		return Left.MappingId.LexicalLess(Right.MappingId);
	});
	ScheduleResult.MergedSolveResult.ChildStageMappingIds.Sort();
	if (ScheduleRequest.RegionRequests.IsEmpty())
	{
		ScheduleResult.FailureReason = TEXT("Region solve schedule requires at least one region request.");
		ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
		return ScheduleResult;
	}

	TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath;
	TMap<FString, TSet<FString>> DependentsByRegion;
	for (const FLayoutRegionSolveRequest& Request : ScheduleRequest.RegionRequests)
	{
		if (Request.RegionDebugPath.IsEmpty())
		{
			ScheduleResult.FailureReason = TEXT("Region solve schedule requires every region to have a non-empty debug path.");
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		if (RequestsByPath.Contains(Request.RegionDebugPath))
		{
			ScheduleResult.FailureReason = FString::Printf(TEXT("Region solve schedule contains duplicate region debug path '%s'."), *Request.RegionDebugPath);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		RequestsByPath.Add(Request.RegionDebugPath, &Request);
	}

	TMap<FString, TSet<FString>> PrerequisitesByRegion;
	for (const FLayoutRegionSolveDependency& Dependency : ScheduleRequest.Dependencies)
	{
		if (!RequestsByPath.Contains(Dependency.PrerequisiteRegionDebugPath) || !RequestsByPath.Contains(Dependency.DependentRegionDebugPath))
		{
			ScheduleResult.FailureReason = FString::Printf(
				TEXT("Region solve dependency references an unknown region: '%s' -> '%s'."),
				*Dependency.PrerequisiteRegionDebugPath,
				*Dependency.DependentRegionDebugPath);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		PrerequisitesByRegion.FindOrAdd(Dependency.DependentRegionDebugPath).Add(Dependency.PrerequisiteRegionDebugPath);
		DependentsByRegion.FindOrAdd(Dependency.PrerequisiteRegionDebugPath).Add(Dependency.DependentRegionDebugPath);
	}

	TMap<FString, FLayoutDirectChildRegionCommitment> DirectChildCommitmentsByChild;
	TSet<FString> ParentRegionsWithCommittedChildren;
	TMap<FString, TArray<FString>> ChildrenByParent;
	for (const FLayoutDirectChildRegionCommitment& RawCommitment : ScheduleRequest.DirectChildCommitments)
	{
		if (!RequestsByPath.Contains(RawCommitment.ParentRegionDebugPath) || !RequestsByPath.Contains(RawCommitment.ChildRegionDebugPath))
		{
			ScheduleResult.FailureReason = FString::Printf(
				TEXT("Direct child commitment references an unknown region: '%s' -> '%s'."),
				*RawCommitment.ParentRegionDebugPath,
				*RawCommitment.ChildRegionDebugPath);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		if (RawCommitment.EndpointCommitments.IsEmpty())
		{
			ScheduleResult.FailureReason = FString::Printf(
				TEXT("Direct child commitment '%s' -> '%s' must include at least one committed endpoint anchor."),
				*RawCommitment.ParentRegionDebugPath,
				*RawCommitment.ChildRegionDebugPath);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		if (DirectChildCommitmentsByChild.Contains(RawCommitment.ChildRegionDebugPath))
		{
			ScheduleResult.FailureReason = FString::Printf(
				TEXT("Direct child commitment already exists for region '%s'. Batch F supports one committed parent per child region."),
				*RawCommitment.ChildRegionDebugPath);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		FLayoutDirectChildRegionCommitment Commitment = RawCommitment;
		LayoutRegionScheduleSolverPrivate::SortCommittedEndpointAnchors(Commitment.EndpointCommitments);
		DirectChildCommitmentsByChild.Add(Commitment.ChildRegionDebugPath, Commitment);
		ParentRegionsWithCommittedChildren.Add(Commitment.ParentRegionDebugPath);
		ChildrenByParent.FindOrAdd(Commitment.ParentRegionDebugPath).Add(Commitment.ChildRegionDebugPath);
		PrerequisitesByRegion.FindOrAdd(Commitment.ChildRegionDebugPath).Add(Commitment.ParentRegionDebugPath);
		DependentsByRegion.FindOrAdd(Commitment.ParentRegionDebugPath).Add(Commitment.ChildRegionDebugPath);
	}

	for (const TPair<FString, const FLayoutRegionSolveRequest*>& RequestPair : RequestsByPath)
	{
		const FLayoutRegionSolveRequest* Request = RequestPair.Value;
		if (Request == nullptr
			|| Request->SourceParentRegionDebugPath.IsEmpty()
			|| !RequestsByPath.Contains(Request->SourceParentRegionDebugPath))
		{
			continue;
		}

		TArray<FString>& Children = ChildrenByParent.FindOrAdd(Request->SourceParentRegionDebugPath);
		if (!Children.Contains(Request->RegionDebugPath))
		{
			Children.Add(Request->RegionDebugPath);
		}
	}

	for (TPair<FString, TArray<FString>>& ParentChildrenPair : ChildrenByParent)
	{
		ParentChildrenPair.Value.Sort();
	}

	TMap<FString, FLayoutChildCapabilityEnvelope> CapabilityEnvelopeByRegion;
	for (const TPair<FString, const FLayoutRegionSolveRequest*>& RequestPair : RequestsByPath)
	{
		CapabilityEnvelopeByRegion.Add(RequestPair.Key, LayoutRegionScheduleSolverPrivate::BuildChildCapabilityEnvelope(*RequestPair.Value));
	}

	TMap<FString, LayoutRegionScheduleSolverPrivate::FScheduledChildSourceInfo> ChildSourceInfoByRegion;
	for (const TPair<FString, const FLayoutRegionSolveRequest*>& RequestPair : RequestsByPath)
	{
		ChildSourceInfoByRegion.Add(
			RequestPair.Key,
			LayoutRegionScheduleSolverPrivate::ResolveScheduledChildSourceInfo(*RequestPair.Value, RequestsByPath, PrerequisitesByRegion));
	}

	TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = ScheduleRequest.PlannedPartitionSeams;
	TSet<FString> PassiveSeamRegions;
	for (const FLayoutPartitionSeamRecord& PlannedSeam : PlannedPartitionSeams)
	{
		if (!PlannedSeam.PassiveRegionDebugPath.IsEmpty())
		{
			PassiveSeamRegions.Add(PlannedSeam.PassiveRegionDebugPath);
		}
	}
	if (!LayoutProfileSolverInternal::BuildSchedulePartitionSeams(
		RequestsByPath,
		CapabilityEnvelopeByRegion,
		ChildrenByParent,
		PlannedPartitionSeams,
		PassiveSeamRegions,
		ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative,
		ScheduleResult.FailureReason))
	{
		ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
		return ScheduleResult;
	}

	if (!ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative)
	{
		for (const TPair<FString, TArray<FString>>& ParentChildrenPair : ChildrenByParent)
		{
			const FString& ParentPath = ParentChildrenPair.Key;
			const FLayoutRegionSolveRequest* ParentRequest = RequestsByPath.FindRef(ParentPath);
			const FLayoutChildCapabilityEnvelope* ParentEnvelope = CapabilityEnvelopeByRegion.Find(ParentPath);
			if (ParentRequest == nullptr || ParentEnvelope == nullptr)
			{
				continue;
			}

			for (const FString& ChildPath : ParentChildrenPair.Value)
			{
				const FLayoutRegionSolveRequest* ChildRequest = RequestsByPath.FindRef(ChildPath);
				const FLayoutChildCapabilityEnvelope* ChildEnvelope = CapabilityEnvelopeByRegion.Find(ChildPath);
				if (ChildRequest == nullptr || ChildEnvelope == nullptr)
				{
					continue;
				}

				for (const FLayoutPlannedCell& ChildPlannedCell : ChildRequest->PlannedCells)
				{
					TArray<FSharedParentChildFace> SharedFacesForCell;
					if (!LayoutRegionScheduleSolverPrivate::DoesChildCellSupportSharedBoundarySeam(
						ChildPlannedCell,
						ChildRequest->RegionCellOffset,
						ChildRequest->FootprintSize,
						ParentRequest->FootprintSize,
						*ParentEnvelope,
						*ChildEnvelope,
						SharedFacesForCell))
					{
						continue;
					}

					const FIntVector ChildWorldCell = ChildPlannedCell.Cell + ChildRequest->RegionCellOffset;
					for (const FSharedParentChildFace& SharedFace : SharedFacesForCell)
					{
						const bool bAlreadyPresent = PlannedPartitionSeams.ContainsByPredicate(
							[&](const FLayoutPartitionSeamRecord& SeamRecord)
							{
								return SeamRecord.ParentRegionDebugPath == ParentPath
									&& SeamRecord.OwnerRegionDebugPath == ParentPath
									&& SeamRecord.PassiveRegionDebugPath == ChildPath
									&& SeamRecord.InterfaceFamily == SharedFace.InterfaceFamily
									&& SeamRecord.OwnerFaceDirection == SharedFace.FaceDirection
									&& SeamRecord.PassiveFaceDirection == SharedFace.FaceDirection
									&& SeamRecord.OwnerStartCell == ChildWorldCell
									&& SeamRecord.PassiveStartCell == ChildWorldCell;
							});
						if (bAlreadyPresent)
						{
							continue;
						}

						FLayoutPartitionSeamRecord& SeamRecord =
							PlannedPartitionSeams.AddDefaulted_GetRef();
						SeamRecord.ParentRegionDebugPath = ParentPath;
						SeamRecord.OwnerRegionDebugPath = ParentPath;
						SeamRecord.PassiveRegionDebugPath = ChildPath;
						SeamRecord.InterfaceFamily = SharedFace.InterfaceFamily;
						SeamRecord.OwnerFaceDirection = SharedFace.FaceDirection;
						SeamRecord.PassiveFaceDirection = SharedFace.FaceDirection;
						SeamRecord.OwnerStartCell = ChildWorldCell;
						SeamRecord.OwnerEndCell = ChildWorldCell;
						SeamRecord.PassiveStartCell = ChildWorldCell;
						SeamRecord.PassiveEndCell = ChildWorldCell;
						SeamRecord.SegmentCount = 1;
						SeamRecord.SeamId = FLayoutId(*FString::Printf(
							TEXT("%s.%s.%s.%s.%s"),
							*ParentPath,
							*ParentPath,
							*ChildPath,
							*SharedFace.InterfaceFamily.ToString(),
							*ChildWorldCell.ToString()));
						PassiveSeamRegions.Add(ChildPath);
					}
				}
			}
		}
	}

	if (!ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative)
	{
		for (const TPair<FString, TArray<FString>>& ParentChildrenPair : ChildrenByParent)
		{
			const FString& ParentPath = ParentChildrenPair.Key;
			const TArray<FString>& ChildPaths = ParentChildrenPair.Value;
			for (int32 LeftChildIndex = 0; LeftChildIndex < ChildPaths.Num(); ++LeftChildIndex)
			{
				for (int32 RightChildIndex = LeftChildIndex + 1; RightChildIndex < ChildPaths.Num(); ++RightChildIndex)
				{
					const FString& FirstChildPath = ChildPaths[LeftChildIndex];
					const FString& SecondChildPath = ChildPaths[RightChildIndex];
					const FLayoutRegionSolveRequest* FirstRequest = RequestsByPath.FindRef(FirstChildPath);
					const FLayoutRegionSolveRequest* SecondRequest = RequestsByPath.FindRef(SecondChildPath);
					const FLayoutChildCapabilityEnvelope* FirstEnvelope = CapabilityEnvelopeByRegion.Find(FirstChildPath);
					const FLayoutChildCapabilityEnvelope* SecondEnvelope = CapabilityEnvelopeByRegion.Find(SecondChildPath);
					if (FirstRequest == nullptr
						|| SecondRequest == nullptr
						|| FirstEnvelope == nullptr
						|| SecondEnvelope == nullptr)
					{
						continue;
					}

					const bool bAlreadyHasSiblingDoorSeam = PlannedPartitionSeams.ContainsByPredicate(
						[&](const FLayoutPartitionSeamRecord& SeamRecord)
						{
							const bool bForwardMatch =
								SeamRecord.OwnerRegionDebugPath == FirstChildPath
								&& SeamRecord.PassiveRegionDebugPath == SecondChildPath;
							const bool bReverseMatch =
								SeamRecord.OwnerRegionDebugPath == SecondChildPath
								&& SeamRecord.PassiveRegionDebugPath == FirstChildPath;
							return SeamRecord.ParentRegionDebugPath == ParentPath
								&& SeamRecord.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
								&& (bForwardMatch || bReverseMatch);
						});
					if (bAlreadyHasSiblingDoorSeam)
					{
						continue;
					}

					const FIntPoint FirstFootprintSize = FirstRequest->FootprintSize;
					const FIntPoint SecondFootprintSize = SecondRequest->FootprintSize;
					for (const FLayoutPlannedCell& FirstPlannedCell : FirstRequest->PlannedCells)
					{
						if (FirstPlannedCell.Intent != ELayoutCellIntent::Boundary
							&& FirstPlannedCell.Intent != ELayoutCellIntent::Entry)
						{
							continue;
						}

						const FIntVector FirstWorldCell = FirstPlannedCell.Cell + FirstRequest->RegionCellOffset;
						const FLayoutPlannedCell* MatchingSecondCell = SecondRequest->PlannedCells.FindByPredicate(
							[&](const FLayoutPlannedCell& SecondPlannedCell)
							{
								return (SecondPlannedCell.Intent == ELayoutCellIntent::Boundary
										|| SecondPlannedCell.Intent == ELayoutCellIntent::Entry)
									&& (SecondPlannedCell.Cell + SecondRequest->RegionCellOffset) == FirstWorldCell;
							});
						if (MatchingSecondCell == nullptr)
						{
							continue;
						}
						if (FirstPlannedCell.Intent != ELayoutCellIntent::Entry
							&& MatchingSecondCell->Intent != ELayoutCellIntent::Entry)
						{
							continue;
						}

						ELayoutFaceDirection FirstFaceDirection = ELayoutFaceDirection::PosX;
						ELayoutFaceDirection SecondFaceDirection = ELayoutFaceDirection::NegX;
						if (!LayoutRegionScheduleSolverPrivate::TryGetSharedOverlapFaceDirections(
							FirstPlannedCell.Cell,
							FirstFootprintSize,
							MatchingSecondCell->Cell,
							SecondFootprintSize,
							FirstFaceDirection,
							SecondFaceDirection))
						{
							continue;
						}

						FString OwnerRegionPath;
						FString PassiveRegionPath;
						const bool bFirstOwnsCommittedDoor = LayoutRegionScheduleSolverPrivate::DoesRequestContainCommittedEndpointAnchor(
							*FirstRequest,
							FirstPlannedCell.Cell,
							FirstFaceDirection);
						const bool bSecondOwnsCommittedDoor = LayoutRegionScheduleSolverPrivate::DoesRequestContainCommittedEndpointAnchor(
							*SecondRequest,
							MatchingSecondCell->Cell,
							SecondFaceDirection);
						if (!bFirstOwnsCommittedDoor && !bSecondOwnsCommittedDoor)
						{
							// Only synthesize an explicit sibling shared-door seam when recursive entry
							// negotiation actually committed one side of the overlap as a doorway. If the
							// two children merely happen to place planned entry cells on the same shared
							// span, keep the generic sibling seam contract instead of upgrading it to a
							// door seam that neither request explicitly owns.
							continue;
						}
						if (bFirstOwnsCommittedDoor != bSecondOwnsCommittedDoor)
						{
							OwnerRegionPath = bFirstOwnsCommittedDoor ? FirstChildPath : SecondChildPath;
							PassiveRegionPath = bFirstOwnsCommittedDoor ? SecondChildPath : FirstChildPath;
						}
						else
						{
							const bool bHasExplicitOwner = LayoutRegionScheduleSolverPrivate::TryChooseSpecificSharedInterfaceOwner(
								FirstChildPath,
								*FirstEnvelope,
								FirstFaceDirection,
								SecondChildPath,
								*SecondEnvelope,
								SecondFaceDirection,
								LayoutGameplayTags::InterfacePartitionDoor,
								OwnerRegionPath,
								PassiveRegionPath);
							if (!bHasExplicitOwner)
							{
								if (FirstPlannedCell.Intent == ELayoutCellIntent::Entry
									&& MatchingSecondCell->Intent != ELayoutCellIntent::Entry)
								{
									OwnerRegionPath = FirstChildPath;
									PassiveRegionPath = SecondChildPath;
								}
								else if (MatchingSecondCell->Intent == ELayoutCellIntent::Entry
									&& FirstPlannedCell.Intent != ELayoutCellIntent::Entry)
								{
									OwnerRegionPath = SecondChildPath;
									PassiveRegionPath = FirstChildPath;
								}
								else
								{
									OwnerRegionPath = FirstChildPath;
									PassiveRegionPath = SecondChildPath;
								}
							}
						}

						const bool bOwnerIsFirst = OwnerRegionPath == FirstChildPath;
						FLayoutPartitionSeamRecord& DoorSeamRecord = PlannedPartitionSeams.AddDefaulted_GetRef();
						DoorSeamRecord.ParentRegionDebugPath = ParentPath;
						DoorSeamRecord.OwnerRegionDebugPath = OwnerRegionPath;
						DoorSeamRecord.PassiveRegionDebugPath = PassiveRegionPath;
						DoorSeamRecord.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
						DoorSeamRecord.OwnerFaceDirection = bOwnerIsFirst ? FirstFaceDirection : SecondFaceDirection;
						DoorSeamRecord.PassiveFaceDirection = bOwnerIsFirst ? SecondFaceDirection : FirstFaceDirection;
						DoorSeamRecord.OwnerStartCell = bOwnerIsFirst ? FirstWorldCell : FirstWorldCell;
						DoorSeamRecord.OwnerEndCell = DoorSeamRecord.OwnerStartCell;
						DoorSeamRecord.PassiveStartCell = bOwnerIsFirst ? FirstWorldCell : FirstWorldCell;
						DoorSeamRecord.PassiveEndCell = DoorSeamRecord.PassiveStartCell;
						DoorSeamRecord.SegmentCount = 1;
						DoorSeamRecord.SeamId = FLayoutId(*FString::Printf(
							TEXT("%s.%s.%s.%s.%s"),
							*ParentPath,
							*OwnerRegionPath,
							*PassiveRegionPath,
							*LayoutGameplayTags::InterfacePartitionDoor.GetTag().ToString(),
							*FirstWorldCell.ToString()));
						PassiveSeamRegions.Add(PassiveRegionPath);
					}
				}
			}
		}
	}

	{
		for (const TPair<FString, FLayoutDirectChildRegionCommitment>& CommitmentPair : DirectChildCommitmentsByChild)
		{
			const FString& ChildRegionPath = CommitmentPair.Key;
			const FLayoutDirectChildRegionCommitment& Commitment = CommitmentPair.Value;
			const FLayoutRegionSolveRequest* ChildRequest = RequestsByPath.FindRef(ChildRegionPath);
			if (ChildRequest == nullptr)
			{
				continue;
			}

			for (const FLayoutCommittedEndpointAnchor& Anchor : Commitment.EndpointCommitments)
			{
				const FIntVector ChildWorldCell = Anchor.LocalCell + ChildRequest->RegionCellOffset;
				const FIntVector ParentWorldCell = ChildWorldCell + FLayoutDirectionUtils::ToCellDelta(Anchor.FaceDirection);
				const ELayoutFaceDirection ParentFaceDirection = FLayoutDirectionUtils::GetOpposite(Anchor.FaceDirection);
				const FLayoutPartitionSeamRecord* ExistingDoorSeam = PlannedPartitionSeams.FindByPredicate(
					[&](const FLayoutPartitionSeamRecord& Seam)
					{
						const bool bParentOwns = Seam.OwnerRegionDebugPath == Commitment.ParentRegionDebugPath
							&& Seam.PassiveRegionDebugPath == ChildRegionPath
							&& Seam.OwnerStartCell == ParentWorldCell
							&& Seam.PassiveStartCell == ChildWorldCell
							&& Seam.OwnerFaceDirection == ParentFaceDirection
							&& Seam.PassiveFaceDirection == Anchor.FaceDirection;
						const bool bChildOwns = Seam.OwnerRegionDebugPath == ChildRegionPath
							&& Seam.PassiveRegionDebugPath == Commitment.ParentRegionDebugPath
							&& Seam.OwnerStartCell == ChildWorldCell
							&& Seam.PassiveStartCell == ParentWorldCell
							&& Seam.OwnerFaceDirection == Anchor.FaceDirection
							&& Seam.PassiveFaceDirection == ParentFaceDirection;
						return Seam.ParentRegionDebugPath == Commitment.ParentRegionDebugPath
							&& Seam.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
							&& (bParentOwns || bChildOwns);
					});
				if (ExistingDoorSeam != nullptr)
				{
					PassiveSeamRegions.Add(ExistingDoorSeam->PassiveRegionDebugPath);
					continue;
				}

				if (ScheduleRequest.bPlannedPartitionSeamsAreAuthoritative)
				{
					ScheduleResult.FailureReason = FString::Printf(
						TEXT("Authoritative recursive schedule for parent '%s' expected committed child door seam at parent cell %s and child cell %s for region '%s'."),
						*Commitment.ParentRegionDebugPath,
						*ParentWorldCell.ToString(),
						*ChildWorldCell.ToString(),
						*ChildRegionPath);
					ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
					return ScheduleResult;
				}

				FLayoutPartitionSeamRecord& DoorSeamRecord = PlannedPartitionSeams.AddDefaulted_GetRef();
				DoorSeamRecord.ParentRegionDebugPath = Commitment.ParentRegionDebugPath;
				DoorSeamRecord.OwnerRegionDebugPath = ChildRegionPath;
				DoorSeamRecord.PassiveRegionDebugPath = Commitment.ParentRegionDebugPath;
				DoorSeamRecord.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
				DoorSeamRecord.OwnerFaceDirection = Anchor.FaceDirection;
				DoorSeamRecord.PassiveFaceDirection = ParentFaceDirection;
				DoorSeamRecord.OwnerStartCell = ChildWorldCell;
				DoorSeamRecord.OwnerEndCell = ChildWorldCell;
				DoorSeamRecord.PassiveStartCell = ParentWorldCell;
				DoorSeamRecord.PassiveEndCell = ParentWorldCell;
				DoorSeamRecord.SegmentCount = 1;
				DoorSeamRecord.SeamId = FLayoutId(*FString::Printf(
					TEXT("%s.%s.%s.%s.%s"),
					*Commitment.ParentRegionDebugPath,
					*ChildRegionPath,
					*Commitment.ParentRegionDebugPath,
					*LayoutGameplayTags::InterfacePartitionDoor.GetTag().ToString(),
					*ChildWorldCell.ToString()));
				PassiveSeamRegions.Add(Commitment.ParentRegionDebugPath);
			}
		}
	}

	for (const TPair<FString, FLayoutDirectChildRegionCommitment>& CommitmentPair : DirectChildCommitmentsByChild)
	{
		const FString& ChildRegionPath = CommitmentPair.Key;
		const FLayoutDirectChildRegionCommitment& Commitment = CommitmentPair.Value;
		FLayoutChildCapabilityEnvelope* Envelope = CapabilityEnvelopeByRegion.Find(ChildRegionPath);
		check(Envelope != nullptr);

		const TSet<FString>* ChildPrerequisites = PrerequisitesByRegion.Find(ChildRegionPath);
		bool bHasUnsupportedPrerequisite = false;
		if (ChildPrerequisites != nullptr)
		{
			for (const FString& Prerequisite : *ChildPrerequisites)
			{
				if (Prerequisite != Commitment.ParentRegionDebugPath)
				{
					bHasUnsupportedPrerequisite = true;
					break;
				}
			}
		}

		const bool bHasCommittedGrandchildren = ParentRegionsWithCommittedChildren.Contains(ChildRegionPath);
		Envelope->ValidationAssertions.Add(LayoutRegionScheduleSolverPrivate::MakeSnapshotAssertionRecord(
			FLayoutId(*FString::Printf(TEXT("%s.ChildCapabilityIndependentOfGrandchildren"), *ChildRegionPath)),
			ELayoutValidationAssertionKind::ChildCapabilityContractValid,
			!bHasUnsupportedPrerequisite && !bHasCommittedGrandchildren,
			{Envelope->SnapshotId},
			!bHasUnsupportedPrerequisite && !bHasCommittedGrandchildren
				? FString()
				: FString::Printf(
					TEXT("Child region '%s' cannot participate in direct-child negotiation because its capability would depend on another region solve."),
					*ChildRegionPath)));

		for (const FLayoutValidationAssertionRecord& Assertion : Envelope->ValidationAssertions)
		{
			if (!Assertion.bPassed)
			{
				ScheduleResult.FailureReason = Assertion.FailureReason.IsEmpty()
					? FString::Printf(TEXT("Child capability assertion '%s' failed for region '%s'."), *Assertion.AssertionId.ToString(), *ChildRegionPath)
					: Assertion.FailureReason;
				ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
				return ScheduleResult;
			}
		}

		for (const FLayoutCommittedEndpointAnchor& Anchor : Commitment.EndpointCommitments)
		{
			const bool bSupported = Envelope->EndpointCapabilities.ContainsByPredicate([&Anchor](const FLayoutChildCapabilityEndpoint& Capability)
			{
				return LayoutRegionScheduleSolverPrivate::DoesChildCapabilityMatchCommittedAnchor(Capability, Anchor);
			});
			if (!bSupported)
			{
				ScheduleResult.FailureReason = FString::Printf(
					TEXT("Child region '%s' cannot prove committed anchor '%s' from its top-level capability envelope."),
					*ChildRegionPath,
					*Anchor.CommitmentId.ToString());
				ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
				return ScheduleResult;
			}
		}
	}

	FString JunctionPreflightFailureReason;
	if (!LayoutRegionScheduleSolverPrivate::ValidatePotentialOwnedSeamJunctionRequirements(
			ScheduleResult.PlannedJunctionRequirements,
			PlannedPartitionSeams,
			RequestsByPath,
			JunctionPreflightFailureReason))
	{
		ScheduleResult.FailureReason = JunctionPreflightFailureReason;
		ScheduleResult.MergedSolveResult.FailureReason = JunctionPreflightFailureReason;
		return ScheduleResult;
	}

	TArray<FString> PendingPaths;
	RequestsByPath.GetKeys(PendingPaths);
	PendingPaths.Sort();

	TSet<FString> CompletedPaths;
	TMap<FString, TArray<FLayoutSolveBoundaryPoint>> ExportedBoundaryPointsByRegion;
	while (!PendingPaths.IsEmpty())
	{
		int32 ReadyIndex = INDEX_NONE;
		for (int32 Index = 0; Index < PendingPaths.Num(); ++Index)
		{
			const TSet<FString>* Prerequisites = PrerequisitesByRegion.Find(PendingPaths[Index]);
			bool bPrerequisitesSatisfied = true;
			if (Prerequisites != nullptr)
			{
				for (const FString& Prerequisite : *Prerequisites)
				{
					if (!CompletedPaths.Contains(Prerequisite))
					{
						bPrerequisitesSatisfied = false;
						break;
					}
				}
			}

			if (bPrerequisitesSatisfied)
			{
				ReadyIndex = Index;
				break;
			}
		}

		if (ReadyIndex == INDEX_NONE)
		{
			ScheduleResult.FailureReason = TEXT("Region solve schedule could not make progress because dependencies contain a cycle.");
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		const FString RegionPath = PendingPaths[ReadyIndex];
		PendingPaths.RemoveAt(ReadyIndex);

		const FLayoutRegionSolveRequest* const* RequestPtr = RequestsByPath.Find(RegionPath);
		check(RequestPtr != nullptr && *RequestPtr != nullptr);
		FLayoutRegionSolveRequest EffectiveRequest = **RequestPtr;
		if (const FLayoutChildCapabilityEnvelope* CapabilityEnvelope = CapabilityEnvelopeByRegion.Find(RegionPath))
		{
			EffectiveRequest.ProofRecords.Append(CapabilityEnvelope->ProofRecords);
			EffectiveRequest.ValidationAssertions.Append(CapabilityEnvelope->ValidationAssertions);
		}

		const FLayoutDirectChildRegionCommitment* DirectChildCommitment = DirectChildCommitmentsByChild.Find(RegionPath);
		const LayoutRegionScheduleSolverPrivate::FScheduledChildSourceInfo* ChildSourceInfo = ChildSourceInfoByRegion.Find(RegionPath);
		if (ChildSourceInfo != nullptr)
		{
			if (EffectiveRequest.SourceContentEntryId == NAME_None)
			{
				EffectiveRequest.SourceContentEntryId = ChildSourceInfo->ContentEntryId;
			}
			if (EffectiveRequest.SourceParentRegionDebugPath.IsEmpty())
			{
				EffectiveRequest.SourceParentRegionDebugPath = ChildSourceInfo->ParentRegionDebugPath;
			}
			EffectiveRequest.bSourceContentEntryOptional = EffectiveRequest.bSourceContentEntryOptional || ChildSourceInfo->bOptional;
		}
		if (DirectChildCommitment != nullptr)
		{
			EffectiveRequest.CommittedEndpointAnchors = DirectChildCommitment->EndpointCommitments;
			if (EffectiveRequest.NegotiatedChildResponsibilityContracts.IsEmpty()
				&& !DirectChildCommitment->NegotiatedResponsibilityContract.ChildRegionDebugPath.IsEmpty())
			{
				EffectiveRequest.NegotiatedChildResponsibilityContracts.Add(
					DirectChildCommitment->NegotiatedResponsibilityContract);
			}
		}
		if (ParentRegionsWithCommittedChildren.Contains(RegionPath) || PassiveSeamRegions.Contains(RegionPath))
		{
			EffectiveRequest.bDeferClosureValidationToSchedule = true;
			EffectiveRequest.ProofRecords.Add(LayoutRegionScheduleSolverPrivate::MakeSnapshotProofRecord(
				FLayoutId(*FString::Printf(TEXT("%s.ScheduleDeferredClosureAudit"), *RegionPath)),
				ELayoutProofKind::NormalizedCommitment,
				FLayoutId(*RegionPath),
				{EffectiveRequest.ProfileSnapshot.SnapshotId},
				FString::Printf(
					TEXT("Deferred closure validation for region '%s' so the schedule-level audit can include committed child providers or shared seams."),
					*RegionPath)));
		}
		const bool bRegionHasScheduledChildren =
			ChildrenByParent.Contains(RegionPath)
			&& !ChildrenByParent.FindChecked(RegionPath).IsEmpty();
		const bool bShouldDeferTraversalValidation =
			(ParentRegionsWithCommittedChildren.Contains(RegionPath) || bRegionHasScheduledChildren)
			&& EffectiveRequest.CommittedTraversalAnchors.IsEmpty();
		if (bShouldDeferTraversalValidation)
		{
			EffectiveRequest.bDeferTraversalValidationToSchedule = true;
			EffectiveRequest.ProofRecords.Add(LayoutRegionScheduleSolverPrivate::MakeSnapshotProofRecord(
				FLayoutId(*FString::Printf(TEXT("%s.ScheduleDeferredTraversalAudit"), *RegionPath)),
				ELayoutProofKind::NormalizedCommitment,
				FLayoutId(*RegionPath),
				{EffectiveRequest.ProfileSnapshot.SnapshotId},
				FString::Printf(
					TEXT("Deferred hard traversal validation for region '%s' so recursive parent/child circulation can be validated on the merged schedule result instead of forcing a standalone route proof before child regions are assembled."),
					*RegionPath)));
		}

		if (!LayoutRegionScheduleSolverPrivate::AppendNegotiatedContractEvidenceToRequest(
			EffectiveRequest,
			ScheduleResult.FailureReason))
		{
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			return ScheduleResult;
		}

		if (const TSet<FString>* Prerequisites = PrerequisitesByRegion.Find(RegionPath))
		{
			TArray<FString> SortedPrerequisites = Prerequisites->Array();
			SortedPrerequisites.Sort();

			TSet<FLayoutId> SatisfiedCommitmentIds;
			for (const FString& Prerequisite : SortedPrerequisites)
			{
				if (const TArray<FLayoutSolveBoundaryPoint>* ExportedBoundaryPoints = ExportedBoundaryPointsByRegion.Find(Prerequisite))
				{
					for (const FLayoutSolveBoundaryPoint& BoundaryPoint : *ExportedBoundaryPoints)
					{
						bool bTouchesDependentPlan = EffectiveRequest.PlannedCells.IsEmpty();
						if (!EffectiveRequest.PlannedCells.IsEmpty())
						{
							const FIntVector DependentCell = BoundaryPoint.LocalCell + FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
							for (const FLayoutPlannedCell& PlannedCell : EffectiveRequest.PlannedCells)
							{
								if (LayoutRegionScheduleSolverPrivate::TranslateCell(PlannedCell.Cell, EffectiveRequest.RegionCellOffset) == DependentCell)
								{
									bTouchesDependentPlan = true;
									break;
								}
							}
						}

						if (!bTouchesDependentPlan)
						{
							continue;
						}

						const FIntVector InverseRegionOffset(
							-EffectiveRequest.RegionCellOffset.X,
							-EffectiveRequest.RegionCellOffset.Y,
							-EffectiveRequest.RegionCellOffset.Z);
						FLayoutSolveBoundaryPoint TranslatedBoundaryPoint =
							LayoutRegionScheduleSolverPrivate::TranslateBoundaryPoint(BoundaryPoint, InverseRegionOffset);
						TranslatedBoundaryPoint.bRequiresBoundaryFacing = (DirectChildCommitment != nullptr);

						if (DirectChildCommitment == nullptr)
						{
							EffectiveRequest.IncomingBoundaryPoints.Add(TranslatedBoundaryPoint);
							continue;
						}

						FLayoutSolveBoundaryPoint BoundaryPointForChild = TranslatedBoundaryPoint;
						for (const FLayoutCommittedEndpointAnchor& Anchor : DirectChildCommitment->EndpointCommitments)
						{
							if (SatisfiedCommitmentIds.Contains(Anchor.CommitmentId))
							{
								continue;
							}

							if (!LayoutRegionScheduleSolverPrivate::DoesBoundaryPointSatisfyCommittedAnchor(TranslatedBoundaryPoint, Anchor))
							{
								continue;
							}

							BoundaryPointForChild.CommitmentId = Anchor.CommitmentId;
							SatisfiedCommitmentIds.Add(Anchor.CommitmentId);
							break;
						}

						EffectiveRequest.IncomingBoundaryPoints.Add(BoundaryPointForChild);
					}
				}
			}

			if (DirectChildCommitment != nullptr)
			{
				for (const FLayoutCommittedEndpointAnchor& Anchor : DirectChildCommitment->EndpointCommitments)
				{
					const bool bSatisfied = SatisfiedCommitmentIds.Contains(Anchor.CommitmentId);
					EffectiveRequest.ValidationAssertions.Add(LayoutRegionScheduleSolverPrivate::MakeSnapshotAssertionRecord(
						FLayoutId(*FString::Printf(TEXT("%s.%s.ChildCommitmentSatisfied"), *RegionPath, *Anchor.CommitmentId.ToString())),
						ELayoutValidationAssertionKind::ChildCommitmentContractValid,
						bSatisfied,
						{Anchor.CommitmentId},
						bSatisfied
							? FString()
							: FString::Printf(
								TEXT("Parent region '%s' did not export a boundary point that satisfied committed child anchor '%s' for region '%s'."),
								*DirectChildCommitment->ParentRegionDebugPath,
								*Anchor.CommitmentId.ToString(),
								*RegionPath)));
					EffectiveRequest.ProofRecords.Add(LayoutRegionScheduleSolverPrivate::MakeSnapshotProofRecord(
						FLayoutId(*FString::Printf(TEXT("%s.%s.NormalizedCommitment"), *RegionPath, *Anchor.CommitmentId.ToString())),
						ELayoutProofKind::NormalizedCommitment,
						Anchor.CommitmentId,
						{EffectiveRequest.ModuleCatalog.SnapshotId, EffectiveRequest.ProfileSnapshot.SnapshotId},
						FString::Printf(
							TEXT("Committed child anchor '%s' was normalized onto child region '%s' before solving."),
							*Anchor.CommitmentId.ToString(),
							*RegionPath)));
				}
			}
		}

		FLayoutRegionSolveResult RegionResult = FLayoutProfileSolver::SolveRegion(EffectiveRequest);
		RegionResult.RegionCellOffset = EffectiveRequest.RegionCellOffset;
		RegionResult.SourceContentEntryId = EffectiveRequest.SourceContentEntryId;
		RegionResult.SolveResult.PartitionSeams = PlannedPartitionSeams.FilterByPredicate([&RegionPath](const FLayoutPartitionSeamRecord& SeamRecord)
		{
			return SeamRecord.ParentRegionDebugPath == RegionPath
				|| SeamRecord.OwnerRegionDebugPath == RegionPath
				|| SeamRecord.PassiveRegionDebugPath == RegionPath;
		});
		if (!RegionResult.SolveResult.bSucceeded)
		{
			FString OptionalDropBlockerReason;
			if (ChildSourceInfo != nullptr
				&& LayoutRegionScheduleSolverPrivate::CanDropOptionalChildRegion(
					RegionPath,
					*ChildSourceInfo,
					DirectChildCommitmentsByChild,
					DependentsByRegion,
					PlannedPartitionSeams,
					OptionalDropBlockerReason))
			{
				const FLayoutDroppedOptionalChildRecord DropRecord = LayoutRegionScheduleSolverPrivate::BuildDroppedOptionalChildRecord(
					EffectiveRequest,
					*ChildSourceInfo,
					RegionResult.SolveResult);
				ScheduleResult.MergedSolveResult.DroppedOptionalChildren.Add(DropRecord);
				LayoutRegionScheduleSolverPrivate::AppendDroppedChildResidualCells(
					EffectiveRequest,
					DropRecord,
					ScheduleResult.MergedSolveResult);

				FLayoutRegionSolveResult DroppedResult;
				DroppedResult.RegionDebugPath = RegionPath;
				DroppedResult.RegionCellOffset = EffectiveRequest.RegionCellOffset;
				DroppedResult.SourceContentEntryId = EffectiveRequest.SourceContentEntryId;
				DroppedResult.bDroppedAsOptionalChild = true;
				DroppedResult.OptionalDropDecisionId = DropRecord.DropDecisionId;
				DroppedResult.OptionalDropReason = DropRecord.FailureReason;
				DroppedResult.SolveResult.bSucceeded = true;
				FLayoutValidationMessage& Warning = DroppedResult.SolveResult.Messages.AddDefaulted_GetRef();
				Warning.Severity = ELayoutValidationSeverity::Warning;
				Warning.Message = FString::Printf(
					TEXT("Dropped optional child region '%s' from content entry '%s'. %s"),
					*RegionPath,
					EffectiveRequest.SourceContentEntryId.IsNone() ? TEXT("<none>") : *EffectiveRequest.SourceContentEntryId.ToString(),
					*DropRecord.FailureReason);
				ScheduleResult.MergedSolveResult.Messages.Append(DroppedResult.SolveResult.Messages);

				CompletedPaths.Add(RegionPath);
				ExportedBoundaryPointsByRegion.Add(RegionPath, {});
				ScheduleResult.RegionResults.Add(MoveTemp(DroppedResult));
				continue;
			}

			if (!OptionalDropBlockerReason.IsEmpty())
			{
				FLayoutValidationMessage& Warning = ScheduleResult.MergedSolveResult.Messages.AddDefaulted_GetRef();
				Warning.Severity = ELayoutValidationSeverity::Warning;
				Warning.Message = OptionalDropBlockerReason;
			}
			ScheduleResult.FailureReason = FString::Printf(
				TEXT("Region '%s' failed to solve. %s"),
				*RegionPath,
				RegionResult.SolveResult.FailureReason.IsEmpty() ? TEXT("No additional failure detail was reported.") : *RegionResult.SolveResult.FailureReason);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			ScheduleResult.RegionResults.Add(MoveTemp(RegionResult));
			return ScheduleResult;
		}

		ScheduleResult.MergedSolveResult.Messages.Append(RegionResult.SolveResult.Messages);
		LayoutRegionScheduleSolverPrivate::AccumulatePropagationStats(
			ScheduleResult.MergedSolveResult.PropagationStats,
			RegionResult.SolveResult.PropagationStats);
		const FLayoutDirectChildRegionCommitment* RegionStageCommitment =
			ScheduleRequest.DirectChildCommitments.FindByPredicate(
				[&RegionPath](const FLayoutDirectChildRegionCommitment& Commitment)
				{
					return Commitment.ChildRegionDebugPath == RegionPath;
				});
		LayoutRegionScheduleSolverPrivate::AppendTranslatedSolveResultArtifacts(
			ScheduleResult.MergedSolveResult,
			RegionResult.SolveResult,
			RegionResult.RegionCellOffset,
			RegionStageCommitment != nullptr ? &RegionStageCommitment->StageMapping : nullptr);
		if (ScheduleResult.RegionResults.IsEmpty())
		{
			ScheduleResult.MergedSolveResult.Seed = RegionResult.SolveResult.Seed;
			ScheduleResult.MergedSolveResult.FootprintSize = RegionResult.SolveResult.FootprintSize;
			ScheduleResult.MergedSolveResult.SharedCellSizeInBlocks = RegionResult.SolveResult.SharedCellSizeInBlocks;
			ScheduleResult.MergedSolveResult.TemplatePlacementZOffsetBlocks = RegionResult.SolveResult.TemplatePlacementZOffsetBlocks;
			ScheduleResult.MergedSolveResult.ResolvedTerrainAlignmentLevel =
				RegionResult.SolveResult.ResolvedTerrainAlignmentLevel;
			ScheduleResult.MergedSolveResult.RootPlacementKind = EffectiveRequest.RootPlacementKind;
			ScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy = EffectiveRequest.WorldBindingPlacementPolicy;
		}

		CompletedPaths.Add(RegionPath);
		TArray<FLayoutSolveBoundaryPoint> TranslatedBoundaryPoints;
		TranslatedBoundaryPoints.Reserve(RegionResult.ExportedBoundaryPoints.Num());
		for (const FLayoutSolveBoundaryPoint& BoundaryPoint : RegionResult.ExportedBoundaryPoints)
		{
			FLayoutSolveBoundaryPoint MappedBoundaryPoint = BoundaryPoint;
			MappedBoundaryPoint.LocalCell = LayoutRegionScheduleSolverPrivate::MapChildResultCellToParent(
				BoundaryPoint.LocalCell,
				RegionResult.RegionCellOffset,
				RegionStageCommitment != nullptr ? &RegionStageCommitment->StageMapping : nullptr);
			MappedBoundaryPoint.SourceCell = LayoutRegionScheduleSolverPrivate::MapChildResultCellToParent(
				BoundaryPoint.SourceCell,
				RegionResult.RegionCellOffset,
				RegionStageCommitment != nullptr ? &RegionStageCommitment->StageMapping : nullptr);
			TranslatedBoundaryPoints.Add(MoveTemp(MappedBoundaryPoint));
		}
		ExportedBoundaryPointsByRegion.Add(RegionPath, MoveTemp(TranslatedBoundaryPoints));
		ScheduleResult.RegionResults.Add(MoveTemp(RegionResult));
	}

	TMap<FString, int32> RegionResultIndexByPath;
	for (int32 ResultIndex = 0; ResultIndex < ScheduleResult.RegionResults.Num(); ++ResultIndex)
	{
		RegionResultIndexByPath.Add(ScheduleResult.RegionResults[ResultIndex].RegionDebugPath, ResultIndex);
	}

	FString JunctionFailureReason;
	if (!LayoutProfileSolverInternal::ValidateOwnedSeamJunctionRequirements(
			ScheduleResult.PlannedJunctionRequirements,
			PlannedPartitionSeams,
			RequestsByPath,
			RegionResultIndexByPath,
			ScheduleResult,
			JunctionFailureReason))
	{
		ScheduleResult.FailureReason = FString::Printf(
			TEXT("Final seam-junction audit detected proof/carrier corruption. %s"),
			*JunctionFailureReason);
		ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
		ScheduleResult.bSucceeded = false;
		ScheduleResult.MergedSolveResult.bSucceeded = false;
		return ScheduleResult;
	}

	for (const FString& ParentRegionPath : ParentRegionsWithCommittedChildren)
	{
		FString DeferredClosureFailureReason;
		if (!LayoutProfileSolverInternal::ReevaluateDeferredClosureCoverageForRegion(
			ParentRegionPath,
			RequestsByPath,
			RegionResultIndexByPath,
			ChildrenByParent,
			ScheduleResult,
			DeferredClosureFailureReason))
		{
			ScheduleResult.FailureReason = DeferredClosureFailureReason.IsEmpty()
				? FString::Printf(TEXT("Deferred closure coverage audit failed for parent region '%s'."), *ParentRegionPath)
				: FString::Printf(TEXT("Region '%s' failed deferred closure coverage audit. %s"), *ParentRegionPath, *DeferredClosureFailureReason);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			ScheduleResult.bSucceeded = false;
			ScheduleResult.MergedSolveResult.bSucceeded = false;
			return ScheduleResult;
		}
	}

	TArray<FString> SortedPassiveSeamRegions = PassiveSeamRegions.Array();
	SortedPassiveSeamRegions.Sort();
	for (const FString& PassiveRegionPath : SortedPassiveSeamRegions)
	{
		FString SeamAuditFailureReason;
		if (!LayoutProfileSolverInternal::ReevaluatePassiveSeamClosureCoverageForRegion(
			PassiveRegionPath,
			PlannedPartitionSeams,
			RequestsByPath,
			RegionResultIndexByPath,
			ScheduleResult,
			SeamAuditFailureReason))
		{
			ScheduleResult.FailureReason = SeamAuditFailureReason.IsEmpty()
				? FString::Printf(TEXT("Deferred seam coverage audit failed for region '%s'."), *PassiveRegionPath)
				: FString::Printf(TEXT("Region '%s' failed deferred seam coverage audit. %s"), *PassiveRegionPath, *SeamAuditFailureReason);
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			ScheduleResult.bSucceeded = false;
			ScheduleResult.MergedSolveResult.bSucceeded = false;
			return ScheduleResult;
		}
	}

	LayoutRegionScheduleSolverPrivate::SuppressPassiveSeamPlacementsOnRegionResults(
		PlannedPartitionSeams,
		RequestsByPath,
		RegionResultIndexByPath,
		ScheduleResult);

	ScheduleResult.bSucceeded = true;
	ScheduleResult.MergedSolveResult.bSucceeded = true;
	ScheduleResult.MergedSolveResult.PartitionSeams = PlannedPartitionSeams;
	LayoutProfileSolverInternal::PopulateResidualUnoccupiedCells(ScheduleResult.MergedSolveResult);
	for (FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
	{
		if (RegionResult.bDroppedAsOptionalChild)
		{
			continue;
		}

		const FLayoutRegionSolveRequest* RegionRequest = RequestsByPath.FindRef(RegionResult.RegionDebugPath);
		if (RegionRequest == nullptr)
		{
			continue;
		}

		LayoutRegionScheduleSolverPrivate::AppendOwnedDroppedChildResidualCellsToRegionResult(
			RegionResult.RegionDebugPath,
			ScheduleResult.MergedSolveResult,
			ScheduleResult.MergedSolveResult.DroppedOptionalChildren,
			RegionResult.SolveResult);

		FString SparseFailureReason;
		if (!LayoutProfileSolverInternal::ApplySparsePlacementRulesToSolveResult(
			RegionRequest->ProfileSnapshot,
			RegionResult.RegionDebugPath,
			RegionRequest->Seed,
			RegionResult.SolveResult,
			SparseFailureReason))
		{
			ScheduleResult.FailureReason = SparseFailureReason.IsEmpty()
				? FString::Printf(TEXT("Sparse placement failed for region '%s'."), *RegionResult.RegionDebugPath)
				: SparseFailureReason;
			ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
			ScheduleResult.bSucceeded = false;
			ScheduleResult.MergedSolveResult.bSucceeded = false;
			return ScheduleResult;
		}
	}

	FString ZoneFeatureFailureReason;
	if (!LayoutRegionScheduleSolverPrivate::EvaluateScheduledZoneFeatureRequirements(
		RequestsByPath,
		ScheduleResult,
		ZoneFeatureFailureReason))
	{
		ScheduleResult.FailureReason = ZoneFeatureFailureReason.IsEmpty()
			? TEXT("Zone feature requirement audit failed.")
			: ZoneFeatureFailureReason;
		ScheduleResult.MergedSolveResult.FailureReason = ScheduleResult.FailureReason;
		ScheduleResult.bSucceeded = false;
		ScheduleResult.MergedSolveResult.bSucceeded = false;
		return ScheduleResult;
	}
	LayoutRegionScheduleSolverPrivate::RebuildMergedPostStructuralArtifacts(
		ScheduleResult, ScheduleRequest.DirectChildCommitments);
	ScheduleResult.MergedSolveResult.FailureReason.Reset();
	return ScheduleResult;
}

FLayoutRegionSolveScheduleResult LayoutRegionScheduleSolverPrivate::SolveRegionTreeInternal(const FLayoutRegionSolveRequest& RootRequest)
{
	if (!LayoutRegionScheduleSolverPrivate::ContentSetHasChildRegionEntries(RootRequest.ContentSetSnapshot))
	{
		return LayoutRegionScheduleSolverFacade::SolveSingleRegionTreeWithPreparedSteppedContract(
			RootRequest);
	}

	// The shared caller surface now routes recursive region trees through the
	// coordinator-owned scheduler directly, while legacy recursive helpers
	// remain internal only.
	const LayoutRegionScheduleSolverFacade::FCapabilityBackedScheduleResult
		CapabilityBackedResult =
			LayoutRegionScheduleSolverFacade::SolveRegionTreeWithCapabilityBackedNegotiation(
				RootRequest);
	return CapabilityBackedResult.ScheduleResult;
}

}

bool LayoutRegionScheduleSolverFacade::TryPrepareRequiredChildParentAuthorityWitness(
	const FLayoutRegionSolveRequest& RootRequest,
	FLayoutRegionSolveRequest& OutPreparedParentRequest,
	FString& OutFailureReason,
	ELayoutSolvePreparationFailureKind* OutPreparationFailureKind,
	int32* OutPreparationCandidateAttemptCount)
{
	LayoutSolveExecution::FScope ExecutionScope(RootRequest.ExecutionSettings.MaxSolveDurationSeconds, RootRequest.ExecutionSettings.MaxCandidateAttempts);
	OutPreparedParentRequest = RootRequest;
	OutFailureReason.Reset();
	if (OutPreparationFailureKind != nullptr)
	{
		*OutPreparationFailureKind = ELayoutSolvePreparationFailureKind::None;
	}
	if (OutPreparationCandidateAttemptCount != nullptr)
	{
		*OutPreparationCandidateAttemptCount = 0;
	}
	if (!LayoutRegionScheduleSolverPrivate::ContentSetHasChildRegionEntries(
			RootRequest.ContentSetSnapshot))
	{
		return true;
	}

	if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
	const FLayoutRegionSolveResult RootPreparedTopology =
		LayoutProfileSolverInternal::BuildRequestBackedRegionPreparedTopology(
			RootRequest);
	if (!RootPreparedTopology.SolveResult.bSucceeded)
	{
		OutFailureReason = RootPreparedTopology.SolveResult.FailureReason;
		if (OutPreparationFailureKind != nullptr)
		{
			*OutPreparationFailureKind =
				RootPreparedTopology.SolveResult.PreparationFailureKind;
		}
		if (OutPreparationCandidateAttemptCount != nullptr)
		{
			*OutPreparationCandidateAttemptCount =
				RootPreparedTopology.SolveResult.PropagationStats.CandidateAttemptCount;
		}
		return false;
	}

	TArray<LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement> Placements;
	TArray<FLayoutValidationMessage> Warnings;
	TArray<LayoutRegionScheduleSolverPrivate::FSkippedOptionalChildPlacement>
		SkippedOptionalPlacements;
	LayoutRegionScheduleSolverPrivate::FCommittedVerticalAccessOwnership
		VerticalAccessOwnership;
	TArray<FLayoutPartitionSeamRecord> PartitionSeams;
	FLayoutRegionSolveRequest PreparedResidualParentRequest;
	ELayoutSolvePreparationFailureKind PreparationFailureKind =
		ELayoutSolvePreparationFailureKind::None;
	int32 PreparationCandidateAttemptCount = 0;
	FLayoutRegionalFailureRecord RegionalFailure;
	LayoutRegionScheduleSolverPrivate::FPlacementBridgeSharedMemoStore MemoStore;
	if (!LayoutRegionScheduleSolverPrivate::TryBuildAutomaticChildPlacements(
			RootRequest,
			RootPreparedTopology,
			true,
			Placements,
			Warnings,
			&SkippedOptionalPlacements,
			&VerticalAccessOwnership,
			&PartitionSeams,
			&PreparedResidualParentRequest,
			nullptr,
			nullptr,
			&PreparationFailureKind,
			&PreparationCandidateAttemptCount,
			&RegionalFailure,
			&MemoStore,
			OutFailureReason))
	{
		if (OutPreparationFailureKind != nullptr)
		{
			*OutPreparationFailureKind = PreparationFailureKind;
		}
		if (OutPreparationCandidateAttemptCount != nullptr)
		{
			*OutPreparationCandidateAttemptCount =
				PreparationCandidateAttemptCount;
		}
		return false;
	}
	if (PreparedResidualParentRequest.PlannedCells.IsEmpty())
	{
		OutFailureReason = TEXT("Required-child structural preparation produced no parent authority cells.");
		return false;
	}

	TMap<FIntVector, FLayoutPlannedCell> FrozenAuthorityCells;
	for (const FLayoutPlannedCell& PlannedCell :
		PreparedResidualParentRequest.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry
			|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			FrozenAuthorityCells.Add(PlannedCell.Cell, PlannedCell);
		}
	}
	for (FLayoutPlannedCell& PlannedCell : OutPreparedParentRequest.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry
			|| PlannedCell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			PlannedCell.Intent =
				LayoutRegionScheduleSolverPrivate::DetermineProvisionalParentIntent(
					PlannedCell.Cell,
					OutPreparedParentRequest.FootprintSize);
			PlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
		}
		if (const FLayoutPlannedCell* FrozenAuthority =
				FrozenAuthorityCells.Find(PlannedCell.Cell))
		{
			PlannedCell.Intent = FrozenAuthority->Intent;
			PlannedCell.EntryOrigin = FrozenAuthority->EntryOrigin;
		}
	}
	OutPreparedParentRequest.PrecomputedPlannedCells =
		OutPreparedParentRequest.PlannedCells;
	OutPreparedParentRequest.VerticalAccessHostGroups =
		PreparedResidualParentRequest.VerticalAccessHostGroups;
	OutPreparedParentRequest.PreparedChildPlacementHints.Reset();
	for (const LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement& Placement :
		Placements)
	{
		FLayoutPreparedChildPlacementHint& Hint =
			OutPreparedParentRequest.PreparedChildPlacementHints.AddDefaulted_GetRef();
		Hint.SourceContentEntryId = Placement.EntryId;
		Hint.InstanceOrdinal = Placement.InstanceOrdinal;
		Hint.RegionCellOffset = Placement.RegionCellOffset;
		Hint.StageMappingId = Placement.StageMapping.MappingId;
	}
	OutPreparedParentRequest.PreparedChildPlacementHints.Sort([](
		const FLayoutPreparedChildPlacementHint& Left,
		const FLayoutPreparedChildPlacementHint& Right)
	{
		if (Left.SourceContentEntryId != Right.SourceContentEntryId)
		{
			return Left.SourceContentEntryId.LexicalLess(Right.SourceContentEntryId);
		}
		return Left.InstanceOrdinal < Right.InstanceOrdinal;
	});
	OutPreparedParentRequest.QualifiedEntryCells.Reset();
	for (const FLayoutPlannedCell& PlannedCell :
		OutPreparedParentRequest.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry)
		{
			OutPreparedParentRequest.QualifiedEntryCells.Add(PlannedCell.Cell);
		}
	}
	OutPreparedParentRequest.bHasQualifiedEntryCells =
		!OutPreparedParentRequest.QualifiedEntryCells.IsEmpty();
	return true;
}

bool LayoutRegionScheduleSolverFacade::TryBuildRequiredPlacementBackedDemandResults(
	const FLayoutRegionSolveRequest& RootRequest,
	TArray<FNegotiatedDemandResult>& OutDemandResults,
	FLayoutRegionSolveRequest* OutParentProofRequest,
	FString& OutFailureReason,
	FLayoutRecursiveVerticalAccessSummary* OutPlannedRecursiveVerticalAccessSummary,
	FNegotiatedSeamPlan* OutCommittedSeamPlan,
	ELayoutSolvePreparationFailureKind* OutPreparationFailureKind,
	int32* OutPreparationCandidateAttemptCount,
	FLayoutRegionalFailureRecord* OutRegionalFailure,
	LayoutRegionScheduleSolverPrivate::FCertifiedRegionalProofResults* OutCertifiedRegionalProofResults)
{
	OutDemandResults.Reset();
	if (OutPreparationFailureKind != nullptr)
	{
		*OutPreparationFailureKind = ELayoutSolvePreparationFailureKind::None;
	}
	if (OutPreparationCandidateAttemptCount != nullptr)
	{
		*OutPreparationCandidateAttemptCount = 0;
	}
	if (OutRegionalFailure != nullptr)
	{
		*OutRegionalFailure = FLayoutRegionalFailureRecord();
	}
	if (OutCertifiedRegionalProofResults != nullptr)
	{
		*OutCertifiedRegionalProofResults =
			LayoutRegionScheduleSolverPrivate::FCertifiedRegionalProofResults();
	}
	if (OutParentProofRequest != nullptr)
	{
		*OutParentProofRequest = FLayoutRegionSolveRequest();
	}
	if (OutPlannedRecursiveVerticalAccessSummary != nullptr)
	{
		*OutPlannedRecursiveVerticalAccessSummary = FLayoutRecursiveVerticalAccessSummary();
	}
	if (OutCommittedSeamPlan != nullptr)
	{
		*OutCommittedSeamPlan = FNegotiatedSeamPlan();
		OutCommittedSeamPlan->bPlannedPartitionSeamsAreAuthoritative = true;
	}
	OutFailureReason.Reset();

	const bool bRootHasChildRegionEntries =
		RootRequest.PlannedCells.IsEmpty()
		&& LayoutRegionScheduleSolverPrivate::ContentSetHasChildRegionEntries(
			RootRequest.ContentSetSnapshot);
	LayoutRegionScheduleSolverPrivate::FPlacementBridgeSharedMemoStore SharedMemoStore;
	FString FirstFailureReason;
	FLayoutRegionalFailureRecord FirstCausalRejection;
	FLayoutRegionalFailureRecord BestDownstreamRejection;
	auto RecordRejection = [
		&FirstCausalRejection,
		&BestDownstreamRejection,
		&FirstFailureReason,
		&RootRequest,
		OutRegionalFailure](
		const FName Phase,
		const FString& Reason,
		const ELayoutSolvePreparationFailureKind PreparationFailureKind,
		const int32 CandidateAttemptCount,
		const FName SourceEntryId = NAME_None,
		const FString& ChildRegionDebugPath = FString())
	{
		FLayoutRegionalFailureRecord Record;
		Record.Scope = SourceEntryId.IsNone() && ChildRegionDebugPath.IsEmpty()
			? ELayoutRegionalFailureScope::Parent
			: ELayoutRegionalFailureScope::Child;
		Record.SourceContentEntryId = SourceEntryId;
		Record.ParentRegionDebugPath = RootRequest.RegionDebugPath;
		Record.RegionDebugPath = ChildRegionDebugPath.IsEmpty()
			? RootRequest.RegionDebugPath
			: ChildRegionDebugPath;
		Record.Phase = Phase;
		Record.PreparationFailureKind = PreparationFailureKind;
		Record.CandidateAttemptCount = CandidateAttemptCount;
		Record.FirstCause = Reason;
		if (!FirstCausalRejection.IsSet())
		{
			FirstCausalRejection = Record;
			FirstFailureReason = Reason;
		}
		else
		{
			BestDownstreamRejection = MoveTemp(Record);
		}
		if (OutRegionalFailure != nullptr)
		{
			*OutRegionalFailure = FirstCausalRejection;
			if (BestDownstreamRejection.IsSet())
			{
				OutRegionalFailure->DownstreamCause =
					BestDownstreamRejection.FirstCause;
			}
		}
	};
	do
	{
		FLayoutRegionSolveRequest AttemptRootRequest = RootRequest;
		if (bRootHasChildRegionEntries)
		{
			AttemptRootRequest.ProfileSnapshot.MinimumFootprintInCells =
				AttemptRootRequest.ProfileSnapshot.MaximumFootprintInCells;
		}

		FLayoutRegionSolveResult RootPreparedTopology =
			LayoutProfileSolverInternal::BuildRequestBackedRegionPreparedTopology(
				AttemptRootRequest);
		if (!RootPreparedTopology.SolveResult.bSucceeded)
		{
			const FString AttemptFailureReason =
				RootPreparedTopology.SolveResult.FailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Coordinator required-demand placement bridge could not prepare finalized parent topology for '%s'."),
						*RootRequest.RegionDebugPath)
					: RootPreparedTopology.SolveResult.FailureReason;
			RecordRejection(
				TEXT("ParentTopologyPreparation"),
				AttemptFailureReason,
				RootPreparedTopology.SolveResult.PreparationFailureKind,
				RootPreparedTopology.SolveResult.PropagationStats.CandidateAttemptCount);
			if (LayoutRegionScheduleSolverPrivate::IsSolverExecutionBudgetFailure(
				AttemptFailureReason))
			{
				OutFailureReason = AttemptFailureReason;
				return false;
			}
			continue;
		}

		// Local project fix: every recursive consumer must see the same finalized
		// parent topology; imported or pre-adaptation supplied cells are not proof authority.
		AttemptRootRequest.FootprintSize = RootPreparedTopology.SolveResult.FootprintSize;
		AttemptRootRequest.PlannedCells = RootPreparedTopology.SolveResult.PlannedCells;
		AttemptRootRequest.PrecomputedPlannedCells = AttemptRootRequest.PlannedCells;
		AttemptRootRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.Reset();
		for (const FLayoutCellReservationRecord& Reservation :
			RootPreparedTopology.SolveResult.CompiledReservations)
		{
			if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
			{
				AttemptRootRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.Add(
					Reservation);
			}
		}

		TArray<LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement> ChildPlacements;
		TArray<FLayoutValidationMessage> PlacementWarnings;
		TArray<LayoutRegionScheduleSolverPrivate::FSkippedOptionalChildPlacement> SkippedOptionalPlacements;
		LayoutRegionScheduleSolverPrivate::FCommittedVerticalAccessOwnership PlacementBackedVerticalAccessOwnership;
		TArray<FLayoutPartitionSeamRecord> PlacementBackedPartitionSeams;
		FString AttemptFailureReason;
		ELayoutSolvePreparationFailureKind AttemptPreparationFailureKind =
			ELayoutSolvePreparationFailureKind::None;
		int32 AttemptPreparationCandidateAttemptCount = 0;
		FLayoutRegionalFailureRecord AttemptRegionalFailure;
		LayoutRegionScheduleSolverPrivate::FCertifiedRegionalProofResults
			AttemptCertifiedRegionalProofResults;
		FLayoutRegionSolveRequest AttemptParentProofRequest;
		bool bUsedIndependentDeferredProofSplit = false;
		if (!LayoutRegionScheduleSolverPrivate::TryBuildAutomaticChildPlacements(
			AttemptRootRequest,
			RootPreparedTopology,
			false,
			ChildPlacements,
			PlacementWarnings,
			&SkippedOptionalPlacements,
			&PlacementBackedVerticalAccessOwnership,
			&PlacementBackedPartitionSeams,
			&AttemptParentProofRequest,
			&bUsedIndependentDeferredProofSplit,
			&AttemptCertifiedRegionalProofResults,
			&AttemptPreparationFailureKind,
			&AttemptPreparationCandidateAttemptCount,
			&AttemptRegionalFailure,
			&SharedMemoStore,
			AttemptFailureReason))
		{
			if (OutPreparationFailureKind != nullptr
				&& *OutPreparationFailureKind == ELayoutSolvePreparationFailureKind::None)
			{
				*OutPreparationFailureKind = AttemptPreparationFailureKind;
			}
			if (OutPreparationCandidateAttemptCount != nullptr)
			{
				*OutPreparationCandidateAttemptCount +=
					AttemptPreparationCandidateAttemptCount;
			}
			RecordRejection(
				AttemptRegionalFailure.IsSet()
					? AttemptRegionalFailure.Phase
					: (AttemptPreparationFailureKind == ELayoutSolvePreparationFailureKind::None
						? FName(TEXT("ChildPlacementDomain"))
						: FName(TEXT("ChildPreparation"))),
				AttemptRegionalFailure.IsSet()
					? AttemptRegionalFailure.FirstCause
					: AttemptFailureReason,
				AttemptPreparationFailureKind,
				AttemptPreparationCandidateAttemptCount,
				AttemptRegionalFailure.SourceContentEntryId,
				AttemptRegionalFailure.RegionDebugPath);
			if (AttemptPreparationFailureKind != ELayoutSolvePreparationFailureKind::None)
			{
				// Local project fix: typed preparation rejection is deterministic for
				// frozen child templates and must not reset work through root-seed retries.
				OutFailureReason = AttemptFailureReason;
				return false;
			}
			if (LayoutRegionScheduleSolverPrivate::IsSolverExecutionBudgetFailure(
				AttemptFailureReason))
			{
				OutFailureReason = AttemptFailureReason;
				return false;
			}
			continue;
		}

		const FRecursiveScheduleSolveContext AttemptSolveContext =
			LayoutRegionScheduleSolverFacade::BuildSolveContext(AttemptRootRequest);
		const FCompiledStructuralInputs AttemptStructuralInputs =
			LayoutRegionScheduleSolverFacade::BuildCompiledStructuralInputs(
				AttemptSolveContext);
		OutDemandResults.Reset();
		OutDemandResults.Reserve(
			ChildPlacements.Num()
			+ SkippedOptionalPlacements.Num());
		for (LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement& Placement :
			ChildPlacements)
		{
			FNegotiatedDemandResult RefreshedDemandResult;
			FString RefreshFailureReason;
			if (!LayoutRegionScheduleSolverPrivate::TryRefreshPlacementBackedNegotiatedContract(
				AttemptSolveContext,
				AttemptStructuralInputs,
				Placement,
				RefreshedDemandResult,
				RefreshFailureReason))
			{
				AttemptFailureReason = RefreshFailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Coordinator required-demand placement bridge could not refresh the negotiated responsibility contract for child region '%s' from the component-aware negotiation surface."),
						*Placement.ChildRegionDebugPath)
					: RefreshFailureReason;
				RecordRejection(
					TEXT("BoundaryContractRefresh"),
					AttemptFailureReason,
					ELayoutSolvePreparationFailureKind::None,
					0,
					Placement.EntryId,
					Placement.ChildRegionDebugPath);
				if (LayoutRegionScheduleSolverPrivate::IsSolverExecutionBudgetFailure(
					AttemptFailureReason))
				{
					OutFailureReason = AttemptFailureReason;
					return false;
				}
				OutDemandResults.Reset();
				break;
			}

			OutDemandResults.Add(MoveTemp(RefreshedDemandResult));
		}
		if (OutDemandResults.Num() != ChildPlacements.Num())
		{
			continue;
		}

		// Deferred validation already froze and proved exact parent host cells.
		// Contract refresh may update boundary evidence, but must not replace that
		// certified ownership with a later reconstruction from incomplete carriers.
		for (int32 PlacementIndex = 0; PlacementIndex < ChildPlacements.Num(); ++PlacementIndex)
		{
			LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement& Placement =
				ChildPlacements[PlacementIndex];
			FNegotiatedDemandResult& DemandResult = OutDemandResults[PlacementIndex];
			LayoutRegionScheduleSolverPrivate::ApplyCommittedVerticalAccessOwnershipToNegotiatedContract(
				PlacementBackedVerticalAccessOwnership,
				Placement.ChildRegionDebugPath,
				Placement.NegotiatedResponsibilityContract);
			Placement.DirectChildCommitment.NegotiatedResponsibilityContract =
				Placement.NegotiatedResponsibilityContract;
			Placement.ChildRequest.NegotiatedChildResponsibilityContracts = {
				Placement.NegotiatedResponsibilityContract};
			DemandResult.ChildRequest = Placement.ChildRequest;
			DemandResult.ResponsibilitySet.ResponsibilityContract =
				Placement.NegotiatedResponsibilityContract;

			FString ContractFailureReason;
			if (!LayoutRegionScheduleSolverPrivate::ValidateNegotiatedChildResponsibilityContract(
				Placement.NegotiatedResponsibilityContract,
				ContractFailureReason))
			{
				AttemptFailureReason = ContractFailureReason.IsEmpty()
					? FString::Printf(
						TEXT("Coordinator required-demand placement bridge could not finalize the negotiated responsibility contract for child region '%s' from the committed host ownership summary."),
						*Placement.ChildRegionDebugPath)
					: ContractFailureReason;
				RecordRejection(
					TEXT("ResponsibilityContract"),
					AttemptFailureReason,
					ELayoutSolvePreparationFailureKind::None,
					0,
					Placement.EntryId,
					Placement.ChildRegionDebugPath);
				OutDemandResults.Reset();
				break;
			}
		}
		if (OutDemandResults.Num() != ChildPlacements.Num())
		{
			continue;
		}

		// The accepted independent branch already owns this exact parent request.
		// Reuse only while every refreshed child contract retains its certified value;
		// use the full handoff identity rather than the abbreviated diagnostic memo key.
		bool bCanReusePreparedParent = bUsedIndependentDeferredProofSplit
			&& AttemptCertifiedRegionalProofResults.IsSet()
			&& !AttemptParentProofRequest.PlannedCells.IsEmpty();
		for (const auto& Placement : ChildPlacements)
		{
			if (!bCanReusePreparedParent)
			{
				break;
			}
			const auto* PreparedContract =
				AttemptParentProofRequest.NegotiatedChildResponsibilityContracts.FindByPredicate(
					[&](const FLayoutNegotiatedChildResponsibilityContract& Contract)
					{
						return Contract.ChildRegionDebugPath == Placement.ChildRegionDebugPath;
					});
			if (PreparedContract == nullptr)
			{
				bCanReusePreparedParent = false;
				break;
			}
			FLayoutChildSolveHandoff ContractIdentity;
			ContractIdentity.DirectCommitment.NegotiatedResponsibilityContract = *PreparedContract;
			const uint64 PreparedHash = ContractIdentity.ComputeDeterministicInputHash();
			ContractIdentity.DirectCommitment.NegotiatedResponsibilityContract =
				Placement.NegotiatedResponsibilityContract;
			bCanReusePreparedParent = PreparedHash == ContractIdentity.ComputeDeterministicInputHash();
		}
		if (!bCanReusePreparedParent
			&& !LayoutRegionScheduleSolverPrivate::BuildPlacementBackedParentProofRequest(
				AttemptRootRequest,
				RootPreparedTopology,
				ChildPlacements,
				PlacementBackedPartitionSeams,
				PlacementBackedVerticalAccessOwnership,
				AttemptParentProofRequest,
				AttemptFailureReason))
		{
			RecordRejection(
				TEXT("ParentResidualPreparation"),
				AttemptFailureReason,
				ELayoutSolvePreparationFailureKind::None,
				0);
			if (LayoutRegionScheduleSolverPrivate::IsSolverExecutionBudgetFailure(
				AttemptFailureReason))
			{
				OutFailureReason = AttemptFailureReason;
				return false;
			}
			continue;
		}

		for (const LayoutRegionScheduleSolverPrivate::FSkippedOptionalChildPlacement& SkippedOptionalPlacement :
			SkippedOptionalPlacements)
		{
			FNegotiatedDemandResult& DemandResult = OutDemandResults.AddDefaulted_GetRef();
			DemandResult.ChildRegionDebugPath = SkippedOptionalPlacement.ChildRegionDebugPath;
			DemandResult.ChildRequest = SkippedOptionalPlacement.ChildRequest;
			DemandResult.ChildRequest.RegionDebugPath =
				SkippedOptionalPlacement.ChildRegionDebugPath;
			DemandResult.bSucceeded = false;
			DemandResult.FailureReason = SkippedOptionalPlacement.FailureReason;
		}

		OutDemandResults.Sort([](
			const FNegotiatedDemandResult& Left,
			const FNegotiatedDemandResult& Right)
		{
			return Left.ChildRegionDebugPath < Right.ChildRegionDebugPath;
		});
		if (OutParentProofRequest != nullptr)
		{
			*OutParentProofRequest = MoveTemp(AttemptParentProofRequest);
		}
		if (OutPlannedRecursiveVerticalAccessSummary != nullptr)
		{
			*OutPlannedRecursiveVerticalAccessSummary =
				LayoutRegionScheduleSolverPrivate::BuildRecursiveVerticalAccessSummaryFromCommittedOwnership(
					PlacementBackedVerticalAccessOwnership);
		}
		if (OutCommittedSeamPlan != nullptr)
		{
			OutCommittedSeamPlan->PlannedPartitionSeams =
				MoveTemp(PlacementBackedPartitionSeams);
			OutCommittedSeamPlan->JunctionRequirements =
				LayoutRegionScheduleSolverFacade::BuildOwnerSideJunctionRequirements(
					OutCommittedSeamPlan->PlannedPartitionSeams, &AttemptRootRequest);
		}
		if (OutCertifiedRegionalProofResults != nullptr
			&& AttemptCertifiedRegionalProofResults.IsSet())
		{
			*OutCertifiedRegionalProofResults =
				MoveTemp(AttemptCertifiedRegionalProofResults);
		}
		return true;
	}
	while (false);

	OutFailureReason = FirstFailureReason.IsEmpty()
		? FString::Printf(
			TEXT("Coordinator required-demand placement bridge found no certified child schedule for prepared parent topology '%s'."),
			*RootRequest.RegionDebugPath)
		: FirstFailureReason;
	if (FirstCausalRejection.IsSet())
	{
		OutFailureReason += FString::Printf(
			TEXT("\nRegional rejection firstCause scope=%s phase=%s source=%s region=%s preparation=%s candidateAttempts=%d."),
			*StaticEnum<ELayoutRegionalFailureScope>()->GetNameStringByValue(
				static_cast<int64>(FirstCausalRejection.Scope)),
			*FirstCausalRejection.Phase.ToString(),
			FirstCausalRejection.SourceContentEntryId.IsNone()
				? TEXT("<unknown>")
				: *FirstCausalRejection.SourceContentEntryId.ToString(),
			FirstCausalRejection.RegionDebugPath.IsEmpty()
				? TEXT("<unknown>")
				: *FirstCausalRejection.RegionDebugPath,
			*StaticEnum<ELayoutSolvePreparationFailureKind>()->GetNameStringByValue(
				static_cast<int64>(FirstCausalRejection.PreparationFailureKind)),
			FirstCausalRejection.CandidateAttemptCount);
	}
	if (BestDownstreamRejection.IsSet()
		&& BestDownstreamRejection.FirstCause != FirstCausalRejection.FirstCause)
	{
		OutFailureReason += FString::Printf(
			TEXT("\nRegional rejection downstream scope=%s phase=%s source=%s region=%s candidateAttempts=%d: %s"),
			*StaticEnum<ELayoutRegionalFailureScope>()->GetNameStringByValue(
				static_cast<int64>(BestDownstreamRejection.Scope)),
			*BestDownstreamRejection.Phase.ToString(),
			BestDownstreamRejection.SourceContentEntryId.IsNone()
				? TEXT("<unknown>")
				: *BestDownstreamRejection.SourceContentEntryId.ToString(),
			BestDownstreamRejection.RegionDebugPath.IsEmpty()
				? TEXT("<unknown>")
				: *BestDownstreamRejection.RegionDebugPath,
			BestDownstreamRejection.CandidateAttemptCount,
			*BestDownstreamRejection.FirstCause);
	}
	return false;
}

#if WITH_AUTOMATION_TESTS
FLayoutRecursiveVerticalAccessSummary LayoutProfileSolverInternal::ResolveParentOnlyVerticalAccessOwnershipForTests(
	const FLayoutRegionSolveRequest& ParentRequest,
	const TArray<FLayoutPlannedCell>& ParentPlannedCells)
{
	const LayoutRegionScheduleSolverPrivate::FCommittedVerticalAccessOwnership Ownership =
		LayoutRegionScheduleSolverPrivate::ResolveCommittedVerticalAccessOwnershipAfterContractRefresh(
			ParentRequest,
			ParentPlannedCells,
			{});
	return LayoutRegionScheduleSolverPrivate::BuildRecursiveVerticalAccessSummaryFromCommittedOwnership(
		Ownership);
}

bool LayoutProfileSolverInternal::TrySettleParentEntriesAroundChildReplacementForTests(
	const TArray<FLayoutPlannedCell>& ParentPlannedCells,
	const FIntPoint& ParentFootprintSize,
	const TSet<FIntVector>& ReservedCells,
	const int32 Seed,
	const bool bAllowExteriorEntryRelocation,
	TArray<FLayoutPlannedCell>& OutParentPlannedCells,
	FString& OutFailureReason)
{
	TSet<FIntVector> ParentPlannedCellSet;
	TMap<FIntVector, ELayoutCellIntent> ParentPlannedCellIntents;
	for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
	{
		ParentPlannedCellSet.Add(PlannedCell.Cell);
		ParentPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
	}
	TSet<FIntVector> ProtectedTraversalCells;
	LayoutRegionScheduleSolverPrivate::CollectProtectedParentTraversalCells(
		ParentPlannedCells,
		ParentPlannedCellIntents,
		ProtectedTraversalCells);
	return LayoutRegionScheduleSolverPrivate::TryBuildParentPlannedCellsWithReservedChildren(
		ParentPlannedCells,
		ParentPlannedCellSet,
		ParentFootprintSize,
		ReservedCells,
		ProtectedTraversalCells,
		Seed,
		bAllowExteriorEntryRelocation,
		LayoutRegionScheduleSolverPrivate::FCommittedVerticalAccessOwnership(),
		{},
		OutParentPlannedCells,
		nullptr,
		&OutFailureReason);
}

bool LayoutProfileSolverInternal::DoesMappedChildPlanAvoidParentExclusiveCellsForTests(
	const TArray<FLayoutPlannedCell>& ParentTranslatedChildCells,
	const TSet<FIntVector>& ParentChildExclusiveCells)
{
	const TSet<FIntVector> NoRelocatableEntryCells;
	return LayoutRegionScheduleSolverPrivate::DoesMappedChildPlanAvoidParentExclusiveCells(
		ParentTranslatedChildCells,
		ParentChildExclusiveCells,
		NoRelocatableEntryCells,
		false);
}

void LayoutProfileSolverInternal::AppendSyntheticParentSupportBoundaryPointsForTests(
	const TArray<FLayoutPlannedCell>& ChildPlannedCells,
	const TSet<FIntVector>& ParentPlannedCells,
	const bool bFirstChildCellIsShared,
	TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints)
{
	TMap<FIntVector, ELayoutCellIntent> ParentPlannedCellIntents;
	for (const FIntVector& ParentCell : ParentPlannedCells)
	{
		ParentPlannedCellIntents.Add(ParentCell, ELayoutCellIntent::Interior);
	}
	TArray<LayoutRegionScheduleSolverPrivate::FSharedParentChildFace> SharedFaces;
	if (bFirstChildCellIsShared && !ChildPlannedCells.IsEmpty())
	{
		LayoutRegionScheduleSolverPrivate::FSharedParentChildFace& SharedFace =
			SharedFaces.AddDefaulted_GetRef();
		SharedFace.ParentCell = ChildPlannedCells[0].Cell;
		SharedFace.ChildLocalCell = ChildPlannedCells[0].Cell;
		SharedFace.FaceDirection = ELayoutFaceDirection::PosX;
		SharedFace.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	}
	TArray<FLayoutPlannedCell> ParentPlannedCellRecords;
	for (const FIntVector& ParentCell : ParentPlannedCells)
	{
		FLayoutPlannedCell& PlannedCell = ParentPlannedCellRecords.AddDefaulted_GetRef();
		PlannedCell.Cell = ParentCell;
		PlannedCell.Intent = ELayoutCellIntent::Interior;
	}
	LayoutRegionScheduleSolverPrivate::AppendSyntheticParentSupportBoundaryPoints(
		{},
		ChildPlannedCells,
		FIntVector::ZeroValue,
		TEXT("ChildSupportTest"),
		ParentPlannedCells,
		ParentPlannedCellIntents,
		FLayoutProfileSolveSnapshot(),
		ParentPlannedCellRecords,
		{},
		SharedFaces,
		NAME_None,
		{},
		OutBoundaryPoints);
}

void LayoutProfileSolverInternal::MergeCommittedPartitionSeamSegmentsIntoRunsForTests(
	TArray<FLayoutPartitionSeamRecord>& InOutSeams)
{
	LayoutRegionScheduleSolverPrivate::MergeCommittedPartitionSeamSegmentsIntoRuns(InOutSeams);
}

bool LayoutProfileSolverInternal::TryChooseCommittedSeamRunOwnerForTests(
	const FString& ParentRegionDebugPath,
	const FString& FirstRegionDebugPath,
	const bool bFirstCanOwn,
	const int32 FirstSupportRunLength,
	const bool bFirstOwnsDoorAnchor,
	const FString& SecondRegionDebugPath,
	const bool bSecondCanOwn,
	const int32 SecondSupportRunLength,
	const bool bSecondOwnsDoorAnchor,
	FString& OutOwnerRegionDebugPath,
	FString& OutPassiveRegionDebugPath)
{
	return LayoutRegionScheduleSolverPrivate::TryChooseCommittedSeamRunOwner(
		ParentRegionDebugPath,
		FirstRegionDebugPath,
		bFirstCanOwn,
		FirstSupportRunLength,
		bFirstOwnsDoorAnchor,
		SecondRegionDebugPath,
		bSecondCanOwn,
		SecondSupportRunLength,
		bSecondOwnsDoorAnchor,
		OutOwnerRegionDebugPath,
		OutPassiveRegionDebugPath);
}

bool LayoutProfileSolverInternal::TryResolveSharedSeamCapabilitiesForTests(
	const FLayoutChildCapabilityEnvelope& FirstEnvelope,
	const ELayoutFaceDirection FirstFaceDirection,
	const FLayoutChildCapabilityEnvelope& SecondEnvelope,
	const ELayoutFaceDirection SecondFaceDirection,
	const FGameplayTag& InterfaceFamily)
{
	bool bFirstOwnsSecondAccepts = false;
	bool bSecondOwnsFirstAccepts = false;
	return LayoutRegionScheduleSolverPrivate::TryResolveSharedSeamCapabilities(
		FirstEnvelope,
		FirstFaceDirection,
		SecondEnvelope,
		SecondFaceDirection,
		InterfaceFamily,
		bFirstOwnsSecondAccepts,
		bSecondOwnsFirstAccepts);
}

bool LayoutProfileSolverInternal::ApplyOwnedSeamUsageRestrictionsForTests(
	const FString& ParentRegionDebugPath,
	const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
	FLayoutRegionSolveRequest& InOutParentRequest,
	FString& OutFailureReason)
{
	TArray<LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement> Placements;
	return LayoutRegionScheduleSolverPrivate::ApplyOwnedSeamUsageRestrictions(
		ParentRegionDebugPath,
		PartitionSeams,
		InOutParentRequest,
		Placements,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::ApplyChildOwnedSeamUsageRestrictionsForTests(
	const FString& ParentRegionDebugPath,
	const FString& ChildRegionDebugPath,
	const FIntVector& ChildRegionCellOffset,
	const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
	FLayoutRegionSolveRequest& InOutParentRequest,
	FLayoutRegionSolveRequest& InOutChildRequest,
	FString& OutFailureReason)
{
	LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement Placement;
	Placement.ChildRegionDebugPath = ChildRegionDebugPath;
	Placement.RegionCellOffset = ChildRegionCellOffset;
	Placement.ChildRequest = InOutChildRequest;
	Placement.ChildDomainRestrictions = InOutChildRequest.CandidateDomainRestrictions;
	TArray<LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement> Placements = {
		MoveTemp(Placement)};
	const bool bSucceeded =
		LayoutRegionScheduleSolverPrivate::ApplyOwnedSeamUsageRestrictions(
			ParentRegionDebugPath,
			PartitionSeams,
			InOutParentRequest,
			Placements,
			OutFailureReason);
	InOutChildRequest = MoveTemp(Placements[0].ChildRequest);
	return bSucceeded;
}

bool LayoutProfileSolverInternal::TryCertifyExactSharedSeamCandidatePairForTests(
	const FLayoutModuleSolveSnapshot& ParentModule,
	const int32 ParentYawRotationSteps,
	const FLayoutModuleSolveSnapshot& ChildModule,
	const int32 ChildYawRotationSteps,
	const ELayoutFaceDirection FaceDirection,
	const FGameplayTag& InterfaceFamily,
	const bool bChildEntry,
	bool& bOutParentCanOwn,
	bool& bOutChildCanOwn,
	FLayoutId& OutWitnessId)
{
	FLayoutId ParentEvidenceId;
	FLayoutId ChildEvidenceId;
	return LayoutRegionScheduleSolverPrivate::TryCertifyExactSharedSeamCandidatePair(
		ParentModule,
		ParentYawRotationSteps,
		ChildModule,
		ChildYawRotationSteps,
		FaceDirection,
		InterfaceFamily,
		bChildEntry,
		bOutParentCanOwn,
		bOutChildCanOwn,
		ParentEvidenceId,
		ChildEvidenceId,
		OutWitnessId);
}

bool LayoutProfileSolverInternal::TrySelectAdmittedParentVerticalAccessHostsForTests(
	const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
	const TSet<FIntVector>& ParentPlannedCells,
	const TSet<FIntVector>& ReservedCells,
	TArray<FLayoutVerticalAccessHostGroup>& OutSelectedHostGroups,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::TrySelectAdmittedParentVerticalAccessHosts(
		HostGroups,
		ParentPlannedCells,
		ReservedCells,
		{},
		{},
		0,
		OutSelectedHostGroups,
		nullptr,
		&OutFailureReason);
}

bool LayoutProfileSolverInternal::TrySelectRouteAwareParentVerticalAccessHostsForTests(
	const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
	const TSet<FIntVector>& ParentPlannedCells,
	const TSet<FIntVector>& ReservedCells,
	const TArray<FIntVector>& LowerRouteTargets,
	const TArray<FIntVector>& UpperRouteTargets,
	const int32 Seed,
	TArray<FLayoutVerticalAccessHostGroup>& OutSelectedHostGroups,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::TrySelectAdmittedParentVerticalAccessHosts(
		HostGroups,
		ParentPlannedCells,
		ReservedCells,
		LowerRouteTargets,
		UpperRouteTargets,
		Seed,
		OutSelectedHostGroups,
		nullptr,
		&OutFailureReason);
}

bool LayoutProfileSolverInternal::DoesMappedChildPlanMatchLevelPlacementPolicyForTests(
	const TArray<FLayoutPlannedCell>& MappedChildPlannedCells,
	const TArray<FLayoutPlannedCell>& ParentPlannedCells,
	const int32 ParentModuleLevelOffset,
	const ELayoutLevelPlacementPolicy LevelPlacementPolicy,
	const int32 SpecificLevel)
{
	TMap<FIntPoint, int32> ParentTopModuleLevelByXY;
	for (const FLayoutPlannedCell& ParentCell : ParentPlannedCells)
	{
		int32& TopModuleLevel = ParentTopModuleLevelByXY.FindOrAdd(
			FIntPoint(ParentCell.Cell.X, ParentCell.Cell.Y),
			TNumericLimits<int32>::Lowest());
		TopModuleLevel = FMath::Max(TopModuleLevel, ParentCell.ModuleLevelIndex);
	}
	return LayoutRegionScheduleSolverPrivate::DoesMappedChildPlanMatchLevelPlacementPolicy(
		MappedChildPlannedCells,
		ParentTopModuleLevelByXY,
		ParentModuleLevelOffset,
		LevelPlacementPolicy,
		SpecificLevel);
}

bool LayoutProfileSolverInternal::TryBuildCommittedParentChildSeamForTests(
	const FLayoutChildCapabilityEnvelope& ParentEnvelope,
	const FLayoutChildCapabilityEnvelope& ChildEnvelope,
	TArray<FLayoutPartitionSeamRecord>& OutSeams,
	FString& OutFailureReason)
{
	FLayoutPlannedCell ChildCell;
	ChildCell.Cell = FIntVector::ZeroValue;
	ChildCell.Intent = ELayoutCellIntent::Boundary;
	TArray<LayoutRegionScheduleSolverPrivate::FSharedParentChildFace> SharedFaces;
	const FIntVector ChildOffset(1, 1, 0);
	if (!LayoutRegionScheduleSolverPrivate::DoesChildCellSupportSharedBoundarySeam(
			ChildCell,
			ChildOffset,
			FIntPoint(1, 1),
			FIntPoint(3, 3),
			ParentEnvelope,
			ChildEnvelope,
			SharedFaces))
	{
		OutSeams.Reset();
		OutFailureReason = TEXT("Exact parent/child overlap did not resolve a parent-owned seam capability pair.");
		return false;
	}

	LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement Placement;
	Placement.ChildRegionDebugPath = TEXT("CommittedParentChild");
	Placement.RegionCellOffset = ChildOffset;
	Placement.SharedParentChildFaces = MoveTemp(SharedFaces);
	for (LayoutRegionScheduleSolverPrivate::FSharedParentChildFace& SharedFace :
		Placement.SharedParentChildFaces)
	{
		SharedFace.ReciprocalDomainWitnessId = TEXT("SyntheticExactPair");
	}
	return LayoutRegionScheduleSolverPrivate::BuildCommittedPartitionSeams(
		TEXT("CommittedParent"),
		{Placement},
		OutSeams,
		&OutFailureReason);
}

bool LayoutProfileSolverInternal::TryBuildPartitionSeamsForDirectChildEntryForTests(
	TArray<FLayoutPartitionSeamRecord>& OutSeams,
	FString& OutFailureReason)
{
	LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement Placement;
	Placement.ChildRegionDebugPath = TEXT("DoorChild");
	Placement.RegionCellOffset = FIntVector(1, 0, 0);
	FLayoutCommittedEndpointAnchor& Anchor =
		Placement.DirectChildCommitment.EndpointCommitments.AddDefaulted_GetRef();
	Anchor.CommitmentId = TEXT("DoorAnchor");
	Anchor.LocalCell = FIntVector::ZeroValue;
	Anchor.FaceDirection = ELayoutFaceDirection::NegX;
	Anchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	return LayoutRegionScheduleSolverPrivate::BuildCommittedPartitionSeams(
		TEXT("DoorParent"),
		{Placement},
		OutSeams,
		&OutFailureReason);
}

bool LayoutProfileSolverInternal::TryBuildCommittedSiblingSeamsForTests(
	const TArray<FLayoutChildCapabilityEnvelope>& ChildEnvelopes,
	const TArray<FIntVector>& RegionOffsets,
	TArray<FLayoutPartitionSeamRecord>& OutSeams,
	FString& OutFailureReason)
{
	if (ChildEnvelopes.Num() != RegionOffsets.Num())
	{
		OutSeams.Reset();
		OutFailureReason = TEXT("Committed sibling seam test inputs require one offset per child envelope.");
		return false;
	}

	TArray<LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement> Placements;
	Placements.Reserve(ChildEnvelopes.Num());
	for (int32 Index = 0; Index < ChildEnvelopes.Num(); ++Index)
	{
		LayoutRegionScheduleSolverPrivate::FAutomaticChildPlacement& Placement =
			Placements.AddDefaulted_GetRef();
		Placement.ChildRegionDebugPath = FString::Printf(TEXT("CommittedSibling%d"), Index);
		Placement.RegionCellOffset = RegionOffsets[Index];
		Placement.ChildCapabilityEnvelope = ChildEnvelopes[Index];
		Placement.ChildRequest.FootprintSize = FIntPoint(1, 1);
		FLayoutPlannedCell& PlannedCell = Placement.ChildRequest.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = FIntVector::ZeroValue;
		PlannedCell.Intent = ELayoutCellIntent::Boundary;

		FLayoutModuleSolveSnapshot& Module =
			Placement.ChildRequest.ModuleCatalog.Modules.AddDefaulted_GetRef();
		Module.SnapshotId = FLayoutId(*FString::Printf(TEXT("CommittedSibling%d.Module"), Index));
		Module.BoundsCells = FIntVector(1, 1, 1);
		Module.AllowedYawRotationSteps = {0};
		for (const FLayoutChildCapabilitySeam& Capability : ChildEnvelopes[Index].SeamCapabilities)
		{
			FLayoutFaceRule FaceRule;
			FaceRule.Direction = Capability.FaceDirection;
			FaceRule.ConnectionTag = Capability.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor
				? LayoutGameplayTags::FaceEntry
				: LayoutGameplayTags::FaceSolid;
			FaceRule.AllowedConnectionTags.AddTag(FaceRule.ConnectionTag);
			FaceRule.OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
			Module.EffectiveFaceRules.SetRule(FaceRule);

			FLayoutSeamProviderIntent& Intent = Module.SeamProviderIntents.AddDefaulted_GetRef();
			// Test fixture materializes an authored label; production capability IDs remain owned.
			Intent.SeamIntentId = FName(*Capability.CapabilityId.ToString());
			Intent.InterfaceFamily = Capability.InterfaceFamily;
			Intent.JunctionUsage = Capability.JunctionUsage;
			Intent.bCanOwnSeam = Capability.bCanOwnSeam;
			Intent.bCanAcceptSeam = Capability.bCanAcceptSeam;

			FLayoutDerivedSpanOffer& Span = Module.DerivedSpanOffers.AddDefaulted_GetRef();
			Span.SpanOfferId = FLayoutId(*FString::Printf(
				TEXT("%s.Span"),
				*Capability.CapabilityId.ToString()));
			Span.LocalCell = FIntVector::ZeroValue;
			Span.FaceDirection = Capability.FaceDirection;
			Span.ConnectionTag = FaceRule.ConnectionTag;
			Span.AllowedConnectionTags = FaceRule.AllowedConnectionTags;
		}
		FLayoutCellCandidateDomainRestriction& Restriction =
			Placement.ChildDomainRestrictions.AddDefaulted_GetRef();
		Restriction.Cell = FIntVector::ZeroValue;
		FLayoutCandidateVariantIdentity& Identity =
			Restriction.AllowedCandidates.AddDefaulted_GetRef();
		Identity.ModuleSnapshotId = Module.SnapshotId;
		Identity.YawRotationSteps = 0;
	}
	return LayoutRegionScheduleSolverPrivate::BuildCommittedPartitionSeams(
		TEXT("CommittedSiblingTestParent"),
		Placements,
		OutSeams,
		&OutFailureReason);
}
#endif

void LayoutRegionScheduleSolverPrivate::NormalizeSuccessfulChildPlacementCandidatesForTestsInternal(
	TArray<LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests>& InOutCandidates,
	const bool bPartialPlacementStep,
	const int32 PlacementDemandCount,
	int32& OutCollapsedCount,
	int32& OutBeamTrimmedCount)
{
	TArray<LayoutRegionScheduleSolverPrivate::FScoredChildOffset> SuccessfulCandidates;
	SuccessfulCandidates.Reserve(InOutCandidates.Num());
	for (const LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests& Candidate : InOutCandidates)
	{
		LayoutRegionScheduleSolverPrivate::FScoredChildOffset& InternalCandidate =
			SuccessfulCandidates.AddDefaulted_GetRef();
		InternalCandidate.BoundaryCertificateId = Candidate.BoundaryCertificateId;
		InternalCandidate.Commitments = Candidate.Commitments;
		InternalCandidate.ParentCommittedTraversalAnchors = Candidate.ParentCommittedTraversalAnchors;
		InternalCandidate.bAllowsChildTraversalBridgeForCommittedContacts =
			Candidate.bAllowsChildTraversalBridgeForCommittedContacts;
		InternalCandidate.ParentPlanReservedCells = Candidate.ParentPlanReservedCells;
		InternalCandidate.NegotiatedHostVerticalAccessResponsibility =
			Candidate.NegotiatedHostVerticalAccessResponsibility;
		InternalCandidate.bHasRequiredHostIngressAnchor = Candidate.bHasRequiredHostIngressAnchor;
		InternalCandidate.RequiredHostIngressAnchor = Candidate.RequiredHostIngressAnchor;
		InternalCandidate.bHasRequiredHostEgressAnchor = Candidate.bHasRequiredHostEgressAnchor;
		InternalCandidate.RequiredHostEgressAnchor = Candidate.RequiredHostEgressAnchor;
		InternalCandidate.RequiredChildInternalVerticalRouteCells = Candidate.RequiredChildInternalVerticalRouteCells;
		InternalCandidate.Score = Candidate.Score;
		InternalCandidate.TieBreakHash = Candidate.TieBreakHash;
		InternalCandidate.StageMapping.MappingId = FLayoutId(*FString::Printf(
			TEXT("TestMapping.%s.%s"),
			Candidate.bHasRequiredHostEgressAnchor
				? *Candidate.RequiredHostEgressAnchor.CommitmentId.ToString()
				: TEXT("None"),
			*FString::JoinBy(
				Candidate.RequiredChildInternalVerticalRouteCells,
				TEXT(","),
				[](const FIntVector& Cell) { return Cell.ToString(); })));
		InternalCandidate.SharedParentChildFaces.Reserve(Candidate.SharedParentChildFaces.Num());
		for (const LayoutProfileSolverInternal::FNegotiatedSharedParentChildFaceForTests& SharedFace : Candidate.SharedParentChildFaces)
		{
			LayoutRegionScheduleSolverPrivate::FSharedParentChildFace& InternalFace =
				InternalCandidate.SharedParentChildFaces.AddDefaulted_GetRef();
			InternalFace.ParentCell = SharedFace.ParentCell;
			InternalFace.ChildLocalCell = SharedFace.ChildLocalCell;
			InternalFace.FaceDirection = SharedFace.FaceDirection;
			InternalFace.InterfaceFamily = SharedFace.InterfaceFamily;
		}
	}

	LayoutRegionScheduleSolverPrivate::FRecursiveSchedulerPerfMetrics PerfMetrics;
	LayoutRegionScheduleSolverPrivate::SortAndCapSuccessfulChildPlacementCandidates(
		SuccessfulCandidates,
		true,
		bPartialPlacementStep,
		PlacementDemandCount,
		&PerfMetrics);
	OutCollapsedCount = PerfMetrics.CandidateNormalizationCollapsedCount;
	OutBeamTrimmedCount = PerfMetrics.CandidateNormalizationBeamTrimmedCount;

	InOutCandidates.Reset();
	InOutCandidates.Reserve(SuccessfulCandidates.Num());
	for (const LayoutRegionScheduleSolverPrivate::FScoredChildOffset& InternalCandidate : SuccessfulCandidates)
	{
		LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests& Candidate =
			InOutCandidates.AddDefaulted_GetRef();
		Candidate.BoundaryCertificateId = InternalCandidate.BoundaryCertificateId;
		Candidate.Commitments = InternalCandidate.Commitments;
		Candidate.ParentCommittedTraversalAnchors = InternalCandidate.ParentCommittedTraversalAnchors;
		Candidate.bAllowsChildTraversalBridgeForCommittedContacts =
			InternalCandidate.bAllowsChildTraversalBridgeForCommittedContacts;
		Candidate.ParentPlanReservedCells = InternalCandidate.ParentPlanReservedCells;
		Candidate.NegotiatedHostVerticalAccessResponsibility =
			InternalCandidate.NegotiatedHostVerticalAccessResponsibility;
		Candidate.bHasRequiredHostIngressAnchor = InternalCandidate.bHasRequiredHostIngressAnchor;
		Candidate.RequiredHostIngressAnchor = InternalCandidate.RequiredHostIngressAnchor;
		Candidate.bHasRequiredHostEgressAnchor = InternalCandidate.bHasRequiredHostEgressAnchor;
		Candidate.RequiredHostEgressAnchor = InternalCandidate.RequiredHostEgressAnchor;
		Candidate.RequiredChildInternalVerticalRouteCells = InternalCandidate.RequiredChildInternalVerticalRouteCells;
		Candidate.Score = InternalCandidate.Score;
		Candidate.TieBreakHash = InternalCandidate.TieBreakHash;
		Candidate.SharedParentChildFaces.Reserve(InternalCandidate.SharedParentChildFaces.Num());
		for (const LayoutRegionScheduleSolverPrivate::FSharedParentChildFace& SharedFace : InternalCandidate.SharedParentChildFaces)
		{
			LayoutProfileSolverInternal::FNegotiatedSharedParentChildFaceForTests& ExternalFace =
				Candidate.SharedParentChildFaces.AddDefaulted_GetRef();
			ExternalFace.ParentCell = SharedFace.ParentCell;
			ExternalFace.ChildLocalCell = SharedFace.ChildLocalCell;
			ExternalFace.FaceDirection = SharedFace.FaceDirection;
			ExternalFace.InterfaceFamily = SharedFace.InterfaceFamily;
		}
	}
}

bool LayoutProfileSolverInternal::TryPromoteChildStageMappingForCommittedRouteForTests(
	FLayoutChildStageMappingResult& InOutMapping,
	const TArray<FLayoutCommittedEndpointAnchor>& Commitments,
	const TArray<FLayoutPlannedCell>& ChildPlannedCells,
	const TArray<FIntVector>& ChildVerticalAccessCells,
	TArray<FIntVector>& OutRequiredVerticalRouteCells,
	FString& OutFailureReason)
{
	return LayoutRegionScheduleSolverPrivate::TryPromoteChildStageMappingForCommittedRoute(
		InOutMapping,
		Commitments,
		ChildPlannedCells,
		ChildVerticalAccessCells,
		OutRequiredVerticalRouteCells,
		OutFailureReason);
}

bool LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(
	const FLayoutRegionSolveRequest& ParentRequest,
	const TArray<FLayoutPlannedCell>& ParentPlannedCells,
	const FIntPoint& ChildFootprintSize,
	const FIntVector& CandidateOffset,
	const FName SourceEntryId,
	const int32 YawRotationSteps,
	const bool bChildSupportsSteppedTerrain,
	const TArray<FLayoutPlannedCell>& SourceChildPlannedCells,
	FLayoutChildStageMappingResult& OutMapping,
	FString& OutFailureReason,
	const FLayoutRegionSolveRequest* ChildRequest)
{
	return LayoutRegionScheduleSolverPrivate::TryBuildChildStageMapping(
		ParentRequest,
		ParentPlannedCells,
		ChildFootprintSize,
		CandidateOffset,
		SourceEntryId,
		YawRotationSteps,
		bChildSupportsSteppedTerrain,
		SourceChildPlannedCells,
		OutMapping,
		OutFailureReason,
		ChildRequest);
}
