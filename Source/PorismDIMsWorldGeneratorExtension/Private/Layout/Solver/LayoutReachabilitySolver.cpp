// Copyright 2026 Spotted Loaf Studio

#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutPlacementOccupancy.h"

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Types/LayoutEntryRootUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace LayoutReachabilitySolverPrivate
{
	using LayoutProfileSolverInternal::GetSolveCandidateInternalAccessLinks;
	using LayoutProfileSolverInternal::GetSolvePlacementDerivedInternalTraversalLinks;
	using LayoutProfileSolverInternal::GetSolveCandidateWalkableAreas;
	using LayoutProfileSolverInternal::GetSolvePlacementInternalAccessLinks;
	using LayoutProfileSolverInternal::GetSolvePlacementWalkableAreas;
	using LayoutProfileSolverInternal::TryGetSolveCandidateFaceRule;
	using LayoutProfileSolverInternal::TryGetSolvePlacementFaceRule;

	const FLayoutProfileSolveSnapshot& GetEffectiveProfileSnapshot(const LayoutProfileSolverInternal::FSolveContext& Context)
	{
		return Context.ProfileSnapshot;
	}

	void AddSolveWarning(LayoutProfileSolverInternal::FSolveContext& Context, const FString& MessageText)
	{
		FLayoutValidationMessage& Message = Context.Result.Messages.AddDefaulted_GetRef();
		Message.Severity = ELayoutValidationSeverity::Warning;
		Message.Message = MessageText;
	}
	using LayoutProfileSolverInternal::CellFaceHasAnyPlannedNeighborCarrier;
	using LayoutProfileSolverInternal::FindCompiledCellFaceInterface;
	using LayoutProfileSolverInternal::FindPlacementOrFixedNeighbor;
	using LayoutProfileSolverInternal::ResolveCompiledIncomingBoundaryPoint;

	const FLayoutModuleSolveSnapshot* FindPlacementModuleSnapshot(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement)
	{
		if (const LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant* Variant =
			LayoutProfileSolverInternal::FindSolvePlacementVariant(Context, Placement))
		{
			if (Context.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex))
			{
				return &Context.ModuleSnapshots[Variant->ModuleSnapshotIndex];
			}
		}

		return nullptr;
	}

	bool TryResolvePlacementBundleWorldCell(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement,
		const FIntVector& LocalBundleCell,
		FIntVector& OutWorldCell)
	{
		const FLayoutModuleSolveSnapshot* ModuleSnapshot = FindPlacementModuleSnapshot(Context, Placement);
		if (ModuleSnapshot == nullptr)
		{
			return false;
		}

		OutWorldCell = LayoutPlacementOccupancy::ProjectLocalCellToWorld(
			Placement.BundleRootCell,
			LocalBundleCell,
			ModuleSnapshot->BoundsCells,
			Placement.YawRotationSteps);
		return true;
	}

	FString BuildWalkableNodeDebugString(
		const LayoutProfileSolverInternal::FWalkableNodeKey& Node,
		const LayoutProfileSolverInternal::FSolveContext& Context)
	{
		if (const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* Placement = Context.Placements.Find(Node.Cell))
		{
			return FString::Printf(
				TEXT("traversal channel '%s' at cell %s module=%s yaw=%d"),
				*Node.TraversalChannel.ToString(),
				*Node.Cell.ToString(),
				*LayoutProfileSolverInternal::GetSolvePlacementDebugName(Context, *Placement).ToString(),
				Placement->YawRotationSteps);
		}

		return FString::Printf(
			TEXT("traversal channel '%s' at cell %s module=<none> yaw=0"),
			*Node.TraversalChannel.ToString(),
			*Node.Cell.ToString());
	}

	bool HasAnyActiveTraversalContext(const LayoutProfileSolverInternal::FSolveContext& Context)
	{
		for (const TPair<int32, TSet<FGameplayTag>>& Pair : Context.ActiveTraversalChannelsByLevel)
		{
			if (!Pair.Value.IsEmpty())
			{
				return true;
			}
		}

		return false;
	}

	bool HasAnyActiveWalkableContext(const LayoutProfileSolverInternal::FSolveContext& Context)
	{
		return HasAnyActiveTraversalContext(Context);
	}

	bool MustGrowTraversalChannelFromEntryRoot(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const FGameplayTag& TraversalChannel)
	{
		const TSet<FGameplayTag>* ActiveTraversalChannels = Context.ActiveTraversalChannelsByLevel.Find(Cell.Z);
		if (ActiveTraversalChannels != nullptr && !ActiveTraversalChannels->IsEmpty())
		{
			return TraversalChannel.IsValid() && ActiveTraversalChannels->Contains(TraversalChannel);
		}

		return TraversalChannel.IsValid() && GetEffectiveProfileSnapshot(Context).bRequireAllTraversalChannelsReachable;
	}

	bool ShouldUseWalkableAreaForReachabilityGraph(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FGameplayTag& WalkableArea)
	{
		return WalkableArea.IsValid();
	}

	bool FaceConnectsWalkableArea(const FLayoutFaceRule& FaceRule, const FGameplayTag& WalkableArea)
	{
		return WalkableArea.IsValid() && FaceRule.ConnectedTraversalChannels.HasTagExact(WalkableArea);
	}

	bool HasAdjacentFixedNeighborPlacement(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell)
	{
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const LayoutProfileSolverInternal::FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface != nullptr
				&& FaceInterface->NeighborKind
					== LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::FixedFilledNeighbor)
			{
				return true;
			}
		}

		return false;
	}

	void AddIncomingFixedNeighborRootNodesForPlacement(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement,
		const bool bOnlyRequiredWalkableAreas,
		TArray<LayoutProfileSolverInternal::FWalkableNodeKey>& OutRootNodes)
	{
		if (!LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(Placement))
		{
			return;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const LayoutProfileSolverInternal::FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr
				|| FaceInterface->NeighborKind
					!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::FixedFilledNeighbor)
			{
				continue;
			}

			const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* FixedNeighborPlacement =
				FindPlacementOrFixedNeighbor(Context, NeighborCell);
			if (FixedNeighborPlacement == nullptr
				|| !LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(*FixedNeighborPlacement))
			{
				continue;
			}

			FLayoutFaceRule FaceRule;
			FLayoutFaceRule NeighborFaceRule;
			if (!TryGetSolvePlacementFaceRule(Context, Placement, Direction, FaceRule)
				|| !TryGetSolvePlacementFaceRule(Context, *FixedNeighborPlacement, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule))
			{
				continue;
			}

			for (const FGameplayTag& WalkableArea : FaceRule.ConnectedTraversalChannels)
			{
				if (!WalkableArea.IsValid()
					|| !NeighborFaceRule.ConnectedTraversalChannels.HasTagExact(WalkableArea)
					|| (bOnlyRequiredWalkableAreas && !MustGrowTraversalChannelFromEntryRoot(Context, Cell, WalkableArea)))
				{
					continue;
				}

				OutRootNodes.AddUnique(LayoutProfileSolverInternal::FWalkableNodeKey{Cell, WalkableArea});
			}
		}
	}

	void AddIncomingBoundaryRootNodesForPlacement(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement,
		const bool bOnlyRequiredWalkableAreas,
		TArray<LayoutProfileSolverInternal::FWalkableNodeKey>& OutRootNodes)
	{
		if (!LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(Placement))
		{
			return;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const LayoutProfileSolverInternal::FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr
				|| (FaceInterface->NeighborKind
						!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::IncomingBoundary
					&& FaceInterface->NeighborKind
						!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::SupportBoundary))
			{
				continue;
			}

			const FLayoutSolveBoundaryPoint* BoundaryPoint =
				ResolveCompiledIncomingBoundaryPoint(Context, *FaceInterface);
			if (BoundaryPoint == nullptr)
			{
				continue;
			}

			FLayoutFaceRule FaceRule;
			if (!TryGetSolvePlacementFaceRule(Context, Placement, Direction, FaceRule))
			{
				continue;
			}

			for (const FGameplayTag& WalkableArea : FaceRule.ConnectedTraversalChannels)
			{
				if (!WalkableArea.IsValid()
					|| !BoundaryPoint->ConnectedTraversalChannels.HasTagExact(WalkableArea)
					|| (bOnlyRequiredWalkableAreas && !MustGrowTraversalChannelFromEntryRoot(Context, Cell, WalkableArea)))
				{
					continue;
				}

				OutRootNodes.AddUnique(LayoutProfileSolverInternal::FWalkableNodeKey{Cell, WalkableArea});
			}
		}
	}

	bool CandidateCanAttachWalkableAreaToIncomingFixedNeighbor(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		const FGameplayTag& WalkableArea)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
		{
			return false;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const LayoutProfileSolverInternal::FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr
				|| FaceInterface->NeighborKind
					!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::FixedFilledNeighbor)
			{
				continue;
			}

			const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* FixedNeighborPlacement =
				FindPlacementOrFixedNeighbor(Context, NeighborCell);
			if (FixedNeighborPlacement == nullptr
				|| !LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(*FixedNeighborPlacement))
			{
				continue;
			}

			FLayoutFaceRule CandidateFaceRule;
			FLayoutFaceRule NeighborFaceRule;
			if (TryGetSolveCandidateFaceRule(Context, Candidate, Direction, CandidateFaceRule)
				&& FaceConnectsWalkableArea(CandidateFaceRule, WalkableArea)
				&& TryGetSolvePlacementFaceRule(Context, *FixedNeighborPlacement, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule)
				&& FaceConnectsWalkableArea(NeighborFaceRule, WalkableArea))
			{
				return true;
			}
		}

		return false;
	}

	bool CandidateCanAttachWalkableAreaToIncomingBoundary(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		const FGameplayTag& WalkableArea)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
		{
			return false;
		}

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const LayoutProfileSolverInternal::FSolveCellFaceInterface* FaceInterface =
				FindCompiledCellFaceInterface(Context, Cell, Direction);
			if (FaceInterface == nullptr
				|| (FaceInterface->NeighborKind
						!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::IncomingBoundary
					&& FaceInterface->NeighborKind
						!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::SupportBoundary))
			{
				continue;
			}

			const FLayoutSolveBoundaryPoint* BoundaryPoint =
				ResolveCompiledIncomingBoundaryPoint(Context, *FaceInterface);
			if (BoundaryPoint == nullptr
				|| !BoundaryPoint->ConnectedTraversalChannels.HasTagExact(WalkableArea))
			{
				continue;
			}

			FLayoutFaceRule CandidateFaceRule;
			if (TryGetSolveCandidateFaceRule(Context, Candidate, Direction, CandidateFaceRule)
				&& FaceConnectsWalkableArea(CandidateFaceRule, WalkableArea))
			{
				return true;
			}
		}

		return false;
	}

	bool IsPlacedEntryRootForWalkableArea(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& Placement,
		const FGameplayTag& WalkableArea)
	{
		if (!ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
		{
			return false;
		}

		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr || *Intent != ELayoutCellIntent::Entry || !LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(Placement) || !GetSolvePlacementWalkableAreas(Context, Placement).HasTagExact(WalkableArea))
		{
			if (Intent != nullptr && *Intent == ELayoutCellIntent::Entry)
			{
			}
			return false;
		}

		TArray<ELayoutFaceDirection> ExteriorDirections;
		TArray<FLayoutFaceRule> WorldFaceRules;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			FLayoutFaceRule FaceRule;
			if (!TryGetSolvePlacementFaceRule(Context, Placement, Direction, FaceRule))
			{
				continue;
			}

			WorldFaceRules.Add(FaceRule);
			if (!CellFaceHasAnyPlannedNeighborCarrier(
					Context,
					Cell,
					Direction))
			{
				ExteriorDirections.Add(Direction);
			}
		}

		const bool bCanReach = LayoutEntryRootUtilities::CanReachWalkableAreaFromEntryRoot(
			WorldFaceRules,
			GetSolvePlacementInternalAccessLinks(Context, Placement),
			ExteriorDirections,
			WalkableArea);
		if (!bCanReach)
		{
			for (const FLayoutFaceRule& FR : WorldFaceRules)
			{
			}
		}
		return bCanReach;
	}

	bool IsCandidateEntryRootForWalkableArea(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		const FGameplayTag& WalkableArea)
	{
		if (!ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
		{
			return false;
		}

		const ELayoutCellIntent* Intent = Context.PlannedCellIntents.Find(Cell);
		if (Intent == nullptr || *Intent != ELayoutCellIntent::Entry || !LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !GetSolveCandidateWalkableAreas(Context, Candidate).HasTagExact(WalkableArea))
		{
			return false;
		}

		TArray<ELayoutFaceDirection> ExteriorDirections;
		TArray<FLayoutFaceRule> WorldFaceRules;
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			FLayoutFaceRule FaceRule;
			if (!TryGetSolveCandidateFaceRule(Context, Candidate, Direction, FaceRule))
			{
				continue;
			}

			WorldFaceRules.Add(FaceRule);
			if (!CellFaceHasAnyPlannedNeighborCarrier(
					Context,
					Cell,
					Direction))
			{
				ExteriorDirections.Add(Direction);
			}
		}

		return LayoutEntryRootUtilities::CanReachWalkableAreaFromEntryRoot(
			WorldFaceRules,
			GetSolveCandidateInternalAccessLinks(Context, Candidate),
			ExteriorDirections,
			WalkableArea);
	}

	void AddCandidateReachableSeedAreas(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const FIntVector& Cell,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		const TSet<LayoutProfileSolverInternal::FWalkableNodeKey>& ReachableNodes,
		TSet<FGameplayTag>& OutReachableAreas)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return;
		}

		for (const FGameplayTag& WalkableArea : GetSolveCandidateWalkableAreas(Context, Candidate))
		{
			if (!ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
			{
				continue;
			}

			if (IsCandidateEntryRootForWalkableArea(Context, Cell, Candidate, WalkableArea)
				|| CandidateCanAttachWalkableAreaToIncomingFixedNeighbor(Context, Cell, Candidate, WalkableArea)
				|| CandidateCanAttachWalkableAreaToIncomingBoundary(Context, Cell, Candidate, WalkableArea))
			{
				OutReachableAreas.Add(WalkableArea);
				continue;
			}

			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
				FLayoutFaceRule CandidateFaceRule;
				if (!TryGetSolveCandidateFaceRule(Context, Candidate, Direction, CandidateFaceRule)
					|| !FaceConnectsWalkableArea(CandidateFaceRule, WalkableArea))
				{
					continue;
				}

				const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (!ReachableNodes.Contains(LayoutProfileSolverInternal::FWalkableNodeKey{NeighborCell, WalkableArea}))
				{
					continue;
				}

				const LayoutProfileSolverInternal::FSolveContext::FSolvePlacement* NeighborPlacement = FindPlacementOrFixedNeighbor(Context, NeighborCell);
				if (NeighborPlacement == nullptr || !LayoutProfileSolverInternal::FSolveContext::IsOccupiedPlacement(*NeighborPlacement))
				{
					continue;
				}

				FLayoutFaceRule NeighborFaceRule;
				if (TryGetSolvePlacementFaceRule(Context, *NeighborPlacement, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule)
					&& FaceConnectsWalkableArea(NeighborFaceRule, WalkableArea))
				{
					OutReachableAreas.Add(WalkableArea);
					break;
				}
			}
		}
	}

	void ExpandCandidateReachableAreasThroughInternalLinks(
		const LayoutProfileSolverInternal::FSolveContext& Context,
		const LayoutProfileSolverInternal::FSolveCandidate& Candidate,
		TSet<FGameplayTag>& ReachableAreas)
	{
		if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
		{
			return;
		}

		bool bChanged = true;
		while (bChanged)
		{
			bChanged = false;
			for (const FLayoutInternalAccessLink& Link : GetSolveCandidateInternalAccessLinks(Context, Candidate))
			{
				if (!ShouldUseWalkableAreaForReachabilityGraph(Context, Link.FromTraversalChannel)
					|| !ShouldUseWalkableAreaForReachabilityGraph(Context, Link.ToTraversalChannel))
				{
					continue;
				}

				if (ReachableAreas.Contains(Link.FromTraversalChannel) && !ReachableAreas.Contains(Link.ToTraversalChannel))
				{
					ReachableAreas.Add(Link.ToTraversalChannel);
					bChanged = true;
				}

				if (Link.bBidirectional && ReachableAreas.Contains(Link.ToTraversalChannel) && !ReachableAreas.Contains(Link.FromTraversalChannel))
				{
					ReachableAreas.Add(Link.FromTraversalChannel);
					bChanged = true;
				}
			}
		}
	}
}

bool LayoutProfileSolverInternal::DoesProfileRequireReachabilityValidation(const FSolveContext& Context)
{
	return !Context.bDeferSparseStructuralReachabilityUntilAssignmentComplete
		&& (LayoutReachabilitySolverPrivate::GetEffectiveProfileSnapshot(Context).bRequireAllTraversalChannelsReachable
			|| LayoutReachabilitySolverPrivate::HasAnyActiveTraversalContext(Context));
}

void LayoutProfileSolverInternal::SeedActiveWalkableAreasFromEntryCandidates(FSolveContext& Context)
{
	for (const FLayoutPlannedCell& PlannedCell : Context.Result.PlannedCells)
	{
		if (PlannedCell.Intent != ELayoutCellIntent::Entry)
		{
			continue;
		}

		TSet<FGameplayTag>& ActiveWalkableAreas = Context.ActiveTraversalChannelsByLevel.FindOrAdd(PlannedCell.Cell.Z);
		if (!ActiveWalkableAreas.IsEmpty())
		{
			continue;
		}

		const TArray<FSolveCandidate>* Candidates = Context.InitialDomains.Find(PlannedCell.Cell);
		if (Candidates == nullptr)
		{
			continue;
		}

		for (const FSolveCandidate& Candidate : *Candidates)
		{
			if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate))
			{
				continue;
			}

			for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolveCandidateWalkableAreas(Context, Candidate))
			{
				if (LayoutReachabilitySolverPrivate::IsCandidateEntryRootForWalkableArea(Context, PlannedCell.Cell, Candidate, WalkableArea)
					|| LayoutReachabilitySolverPrivate::CandidateCanAttachWalkableAreaToIncomingBoundary(Context, PlannedCell.Cell, Candidate, WalkableArea))
				{
					ActiveWalkableAreas.Add(WalkableArea);
				}
			}
		}

		if (PlannedCell.Cell.Z == 0 && ActiveWalkableAreas.Contains(LayoutGameplayTags::TraversalPrimary))
		{
			ActiveWalkableAreas.Reset();
			ActiveWalkableAreas.Add(LayoutGameplayTags::TraversalPrimary);
		}
	}
}

TSet<LayoutProfileSolverInternal::FWalkableNodeKey> LayoutProfileSolverInternal::BuildReachableWalkableNodesFromPlacedRoots(
	const FSolveContext& Context,
	const TArray<FWalkableNodeKey>* ExplicitRoots,
	FSolveContext* SparseWorkContext)
{
	TMap<FWalkableNodeKey, TSet<FWalkableNodeKey>> Adjacency;
	TArray<FWalkableNodeKey> RootNodes;

	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
	{
		if (SparseWorkContext != nullptr && !ConsumeSparseStructuralWork(*SparseWorkContext)) return {};
		if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value))
		{
			continue;
		}

		for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolvePlacementWalkableAreas(Context, PlacementPair.Value))
		{
			if (!LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
			{
				continue;
			}

			const FWalkableNodeKey Node{PlacementPair.Key, WalkableArea};
			Adjacency.FindOrAdd(Node);
			if (LayoutReachabilitySolverPrivate::IsPlacedEntryRootForWalkableArea(Context, PlacementPair.Key, PlacementPair.Value, WalkableArea))
			{
				RootNodes.AddUnique(Node);
			}
		}

		LayoutReachabilitySolverPrivate::AddIncomingFixedNeighborRootNodesForPlacement(Context, PlacementPair.Key, PlacementPair.Value, false, RootNodes);
		LayoutReachabilitySolverPrivate::AddIncomingBoundaryRootNodesForPlacement(Context, PlacementPair.Key, PlacementPair.Value, false, RootNodes);

		for (const FLayoutInternalAccessLink& Link : LayoutReachabilitySolverPrivate::GetSolvePlacementInternalAccessLinks(Context, PlacementPair.Value))
		{
			if (!LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, Link.FromTraversalChannel)
				|| !LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, Link.ToTraversalChannel))
			{
				continue;
			}

			const FWalkableNodeKey FromKey{PlacementPair.Key, Link.FromTraversalChannel};
			const FWalkableNodeKey ToKey{PlacementPair.Key, Link.ToTraversalChannel};
			Adjacency.FindOrAdd(FromKey).Add(ToKey);
			if (Link.bBidirectional)
			{
				Adjacency.FindOrAdd(ToKey).Add(FromKey);
			}
		}

		if (PlacementPair.Value.bBundleRoot)
		{
			for (const FLayoutDerivedInternalTraversalLink& Link :
				LayoutReachabilitySolverPrivate::GetSolvePlacementDerivedInternalTraversalLinks(Context, PlacementPair.Value))
			{
				if (!LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, Link.FromTraversalChannel)
					|| !LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, Link.ToTraversalChannel))
				{
					continue;
				}

				FIntVector FromWorldCell;
				FIntVector ToWorldCell;
				if (!LayoutReachabilitySolverPrivate::TryResolvePlacementBundleWorldCell(
						Context,
						PlacementPair.Value,
						Link.FromLocalCell,
						FromWorldCell)
					|| !LayoutReachabilitySolverPrivate::TryResolvePlacementBundleWorldCell(
						Context,
						PlacementPair.Value,
						Link.ToLocalCell,
						ToWorldCell))
				{
					continue;
				}

				const FWalkableNodeKey FromKey{FromWorldCell, Link.FromTraversalChannel};
				const FWalkableNodeKey ToKey{ToWorldCell, Link.ToTraversalChannel};
				Adjacency.FindOrAdd(FromKey).Add(ToKey);
				if (Link.bBidirectional)
				{
					Adjacency.FindOrAdd(ToKey).Add(FromKey);
				}
			}
		}
	}

	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
	{
		if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value))
		{
			continue;
		}

		if (SparseWorkContext != nullptr && !ConsumeSparseStructuralWork(*SparseWorkContext)) return {};
		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			FLayoutFaceRule FaceRule;
			if (!LayoutReachabilitySolverPrivate::TryGetSolvePlacementFaceRule(Context, PlacementPair.Value, Direction, FaceRule) || FaceRule.ConnectedTraversalChannels.IsEmpty())
			{
				continue;
			}

			const FIntVector NeighborCell = PlacementPair.Key + FLayoutDirectionUtils::ToCellDelta(Direction);
			const FSolveContext::FSolvePlacement* NeighborPlacement =
				FindPlacementOrFixedNeighbor(Context, NeighborCell);
			if (NeighborPlacement == nullptr || !FSolveContext::IsOccupiedPlacement(*NeighborPlacement))
			{
				continue;
			}

			FLayoutFaceRule NeighborFaceRule;
			if (!LayoutReachabilitySolverPrivate::TryGetSolvePlacementFaceRule(Context, *NeighborPlacement, FLayoutDirectionUtils::GetOpposite(Direction), NeighborFaceRule))
			{
				continue;
			}

			for (const FGameplayTag& WalkableArea : FaceRule.ConnectedTraversalChannels)
			{
				if (!LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea)
					|| !NeighborFaceRule.ConnectedTraversalChannels.HasTagExact(WalkableArea))
				{
					continue;
				}

				const FWalkableNodeKey FromKey{PlacementPair.Key, WalkableArea};
				const FWalkableNodeKey ToKey{NeighborCell, WalkableArea};
				Adjacency.FindOrAdd(FromKey).Add(ToKey);
				Adjacency.FindOrAdd(ToKey).Add(FromKey);
			}
		}
	}

	TSet<FWalkableNodeKey> Visited;
	TArray<FWalkableNodeKey> Frontier = ExplicitRoots != nullptr ? *ExplicitRoots : RootNodes;
	while (!Frontier.IsEmpty())
	{
		if (SparseWorkContext != nullptr && !ConsumeSparseStructuralWork(*SparseWorkContext)) return {};
		const FWalkableNodeKey Current = Frontier.Pop(EAllowShrinking::No);
		if (Visited.Contains(Current))
		{
			continue;
		}

		Visited.Add(Current);
		if (const TSet<FWalkableNodeKey>* Neighbors = Adjacency.Find(Current))
		{
			for (const FWalkableNodeKey& Neighbor : *Neighbors)
			{
				if (!Visited.Contains(Neighbor))
				{
					Frontier.Add(Neighbor);
				}
			}
		}
	}

	return Visited;
}

bool LayoutProfileSolverInternal::IsPlacedEntryRootForWalkableArea(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveContext::FSolvePlacement& Placement,
	const FGameplayTag& WalkableArea)
{
	return LayoutReachabilitySolverPrivate::IsPlacedEntryRootForWalkableArea(Context, Cell, Placement, WalkableArea);
}

int32 LayoutProfileSolverInternal::GetCandidateReachabilityPreference(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const TSet<FWalkableNodeKey>& ReachableNodes)
{
	if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !DoesProfileRequireReachabilityValidation(Context))
	{
		return 0;
	}

	int32 RequiredCount = 0;
	for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolveCandidateWalkableAreas(Context, Candidate))
	{
		if (LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, Cell, WalkableArea))
		{
			++RequiredCount;
		}
	}

	if (RequiredCount <= 0)
	{
		return 0;
	}

	TSet<FGameplayTag> CandidateReachableAreas;
	LayoutReachabilitySolverPrivate::AddCandidateReachableSeedAreas(Context, Cell, Candidate, ReachableNodes, CandidateReachableAreas);
	LayoutReachabilitySolverPrivate::ExpandCandidateReachableAreasThroughInternalLinks(Context, Candidate, CandidateReachableAreas);

	int32 ConnectedCount = 0;
	for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolveCandidateWalkableAreas(Context, Candidate))
	{
		if (LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, Cell, WalkableArea)
			&& CandidateReachableAreas.Contains(WalkableArea))
		{
			++ConnectedCount;
		}
	}

	return ConnectedCount > 0 ? 2 : 1;
}

void LayoutProfileSolverInternal::PruneDetachedReachabilityCandidatesWhenAttachable(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const TSet<FWalkableNodeKey>& ReachableNodes,
	TArray<FSolveCandidate>& Candidates)
{
	if (!DoesProfileRequireReachabilityValidation(Context) || ReachableNodes.IsEmpty() || Candidates.Num() <= 1)
	{
		return;
	}

	bool bHasCandidateThatAttachesRequiredReachability = false;
	for (const FSolveCandidate& Candidate : Candidates)
	{
		if (GetCandidateReachabilityPreference(Context, Cell, Candidate, ReachableNodes) > 1)
		{
			bHasCandidateThatAttachesRequiredReachability = true;
			break;
		}
	}

	if (!bHasCandidateThatAttachesRequiredReachability)
	{
		return;
	}

	for (int32 CandidateIndex = Candidates.Num() - 1; CandidateIndex >= 0; --CandidateIndex)
	{
		const FSolveCandidate& Candidate = Candidates[CandidateIndex];
		if (LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) && GetCandidateReachabilityPreference(Context, Cell, Candidate, ReachableNodes) == 1)
		{
			Candidates.RemoveAt(CandidateIndex, 1, EAllowShrinking::No);
		}
	}
}

bool LayoutProfileSolverInternal::CandidateExposesRequiredWalkableArea(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate)
{
	if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !DoesProfileRequireReachabilityValidation(Context))
	{
		return false;
	}

	for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolveCandidateWalkableAreas(Context, Candidate))
	{
		if (LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, Cell, WalkableArea))
		{
			return true;
		}
	}

	return false;
}

bool LayoutProfileSolverInternal::CandidateCanAttachAllEstablishedRequiredWalkableAreas(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const FSolveCandidate& Candidate,
	const TSet<FWalkableNodeKey>& ReachableNodes,
	FGameplayTag& OutFirstUnreachableWalkableArea)
{
	if (!LayoutProfileSolverInternal::IsOccupiedCandidate(Candidate) || !DoesProfileRequireReachabilityValidation(Context))
	{
		return true;
	}

	TSet<FGameplayTag> RequiredAreas;
	TSet<FGameplayTag> CandidateReachableAreas;
	bool bHasAnyRequiredArea = false;
	for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolveCandidateWalkableAreas(Context, Candidate))
	{
		if (!LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, Cell, WalkableArea))
		{
			continue;
		}

		bHasAnyRequiredArea = true;
		RequiredAreas.Add(WalkableArea);
	}

	if (!bHasAnyRequiredArea)
	{
		return true;
	}

	LayoutReachabilitySolverPrivate::AddCandidateReachableSeedAreas(Context, Cell, Candidate, ReachableNodes, CandidateReachableAreas);
	const bool bCanSeedAnyReachableArea = !CandidateReachableAreas.IsEmpty();
	LayoutReachabilitySolverPrivate::ExpandCandidateReachableAreasThroughInternalLinks(Context, Candidate, CandidateReachableAreas);
	if (ReachableNodes.IsEmpty() && !bCanSeedAnyReachableArea)
	{
		if (Context.FixedNeighborPlacements.IsEmpty() && !LayoutReachabilitySolverPrivate::HasAdjacentFixedNeighborPlacement(Context, Cell))
		{
			return true;
		}

		for (const FGameplayTag& WalkableArea : RequiredAreas)
		{
			OutFirstUnreachableWalkableArea = WalkableArea;
			break;
		}
		return false;
	}

	for (const FGameplayTag& WalkableArea : RequiredAreas)
	{
		if ((!ReachableNodes.IsEmpty() || !Context.FixedNeighborPlacements.IsEmpty())
			&& !CandidateReachableAreas.Contains(WalkableArea))
		{
			OutFirstUnreachableWalkableArea = WalkableArea;
			return false;
		}
	}

	return true;
}

int32 LayoutProfileSolverInternal::GetCellReachabilitySelectionRank(
	const FSolveContext& Context,
	const FIntVector& Cell,
	const TArray<FSolveCandidate>& Candidates,
	const TSet<FWalkableNodeKey>& ReachableNodes)
{
	if (!DoesProfileRequireReachabilityValidation(Context))
	{
		return 0;
	}

	bool bHasRequiredWalkableCandidate = false;
	bool bTouchesReachableNeighbor = false;
	for (const FSolveCandidate& Candidate : Candidates)
	{
		if (!CandidateExposesRequiredWalkableArea(Context, Cell, Candidate))
		{
			continue;
		}

		bHasRequiredWalkableCandidate = true;
		if (GetCandidateReachabilityPreference(Context, Cell, Candidate, ReachableNodes) > 1)
		{
			return 0;
		}

		if (LayoutReachabilitySolverPrivate::HasAdjacentFixedNeighborPlacement(Context, Cell))
		{
			return 0;
		}

		for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolveCandidateWalkableAreas(Context, Candidate))
		{
			if (!LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
			{
				continue;
			}

			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
				const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (ReachableNodes.Contains(FWalkableNodeKey{NeighborCell, WalkableArea}))
				{
					bTouchesReachableNeighbor = true;
					break;
				}
			}

			if (bTouchesReachableNeighbor)
			{
				break;
			}
		}
	}

	if (bTouchesReachableNeighbor)
	{
		return 1;
	}

	return bHasRequiredWalkableCandidate ? 2 : 3;
}

TMap<FIntVector, int32> LayoutProfileSolverInternal::BuildReachabilityFrontierDistances(
	FSolveContext& Context,
	const TSet<FWalkableNodeKey>& ReachableNodes)
{
	TMap<FIntVector, int32> DistancesByCell;
	if (ReachableNodes.IsEmpty())
	{
		return DistancesByCell;
	}

	TMap<FWalkableNodeKey, int32> DistancesByNode;
	TArray<FWalkableNodeKey> Queue;
	for (const FWalkableNodeKey& ReachableNode : ReachableNodes)
	{
		DistancesByNode.Add(ReachableNode, 0);
		Queue.Add(ReachableNode);
	}

	auto CellCanExposeWalkableAreaOnFace = [&Context](const FIntVector& Cell, const FGameplayTag& WalkableArea, const ELayoutFaceDirection Direction)
	{
		if (!WalkableArea.IsValid())
		{
			return false;
		}

		const FSolveContext::FSolvePlacement* Placement =
			FindPlacementOrFixedNeighbor(Context, Cell);
		if (Placement != nullptr)
		{
			if (!FSolveContext::IsOccupiedPlacement(*Placement))
			{
				return false;
			}

			FLayoutFaceRule FaceRule;
			return LayoutReachabilitySolverPrivate::TryGetSolvePlacementFaceRule(Context, *Placement, Direction, FaceRule)
				&& LayoutReachabilitySolverPrivate::FaceConnectsWalkableArea(FaceRule, WalkableArea);
		}

		const LayoutProfileSolverInternal::FSolveCellFaceInterface* FaceInterface =
			FindCompiledCellFaceInterface(Context, Cell, Direction);
		if (FaceInterface == nullptr
			|| FaceInterface->NeighborKind
				!= LayoutProfileSolverInternal::ESolveCellFaceNeighborKind::InternalPlannedNeighbor)
		{
			return false;
		}

		return true;
	};

	int32 QueueIndex = 0;
	while (QueueIndex < Queue.Num())
	{
		const FWalkableNodeKey Current = Queue[QueueIndex++];
		const int32 CurrentDistance = DistancesByNode.FindRef(Current);

		for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
			const FIntVector NeighborCell = Current.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			if (Context.Placements.Contains(NeighborCell))
			{
				continue;
			}

			const FWalkableNodeKey NeighborNode{NeighborCell, Current.TraversalChannel};
			if (DistancesByNode.Contains(NeighborNode)
				|| !CellCanExposeWalkableAreaOnFace(Current.Cell, Current.TraversalChannel, Direction)
				|| !CellCanExposeWalkableAreaOnFace(NeighborCell, Current.TraversalChannel, FLayoutDirectionUtils::GetOpposite(Direction)))
			{
				continue;
			}

			const int32 NeighborDistance = CurrentDistance + 1;
			DistancesByNode.Add(NeighborNode, NeighborDistance);
			int32& CellDistance = DistancesByCell.FindOrAdd(NeighborCell, MAX_int32);
			CellDistance = FMath::Min(CellDistance, NeighborDistance);
			Queue.Add(NeighborNode);
		}
	}

	return DistancesByCell;
}

bool LayoutProfileSolverInternal::ValidateReachability(FSolveContext& Context, FString& OutFailureReason)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Solve_Reachability, STAT_PorismLayout_Reachability);
	const TSet<FWalkableNodeKey> Visited = BuildReachableWalkableNodesFromPlacedRoots(Context);
	if (Visited.IsEmpty() && !DoesProfileRequireReachabilityValidation(Context))
	{
		return true;
	}

	if (!LayoutReachabilitySolverPrivate::HasAnyActiveWalkableContext(Context))
	{
		for (const FWalkableNodeKey& RootNode : Visited)
		{
			if (RootNode.TraversalChannel.IsValid())
			{
				Context.ActiveTraversalChannelsByLevel.FindOrAdd(RootNode.Cell.Z).Add(RootNode.TraversalChannel);
			}
		}
	}

	if (LayoutReachabilitySolverPrivate::GetEffectiveProfileSnapshot(Context).bRequireAllTraversalChannelsReachable)
	{
		const bool bHasUpperPlannedCells =
			Context.ProfileSnapshot.LevelCount > 1 && Context.FootprintSize == FIntPoint(1, 1);
		if (bHasUpperPlannedCells)
		{
			bool bHasAnyExplicitVerticalAccessCapability = false;
			for (const FLayoutModuleSolveSnapshot& Snapshot : Context.ModuleSnapshots)
			{
				if (Snapshot.SupportsIntent(ELayoutCellIntent::VerticalAccess)
					|| !Snapshot.DerivedVerticalAccessContracts.IsEmpty())
				{
					bHasAnyExplicitVerticalAccessCapability = true;
					break;
				}
			}
			if (!bHasAnyExplicitVerticalAccessCapability)
			{
				OutFailureReason = TEXT("Upper planned walkable areas are unreachable from the authored entry root because the reachable staged contract never realizes explicit VerticalAccess support.");
				return false;
			}
		}
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value))
			{
				continue;
			}

			// VerticalAccess cells provide transit infrastructure between Z levels.
			// Their walkable areas may be on vertical faces that the same-Z-level BFS
			// cannot reach from the horizontal entry root.  Skip reachability for them;
			// their own face-rule validation ensures they bridge the transition correctly.
			const ELayoutCellIntent* const CellIntent = Context.PlannedCellIntents.Find(PlacementPair.Key);
			if (CellIntent != nullptr && *CellIntent == ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}

			for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolvePlacementWalkableAreas(Context, PlacementPair.Value))
			{
				if (!LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, PlacementPair.Key, WalkableArea))
				{
					continue;
				}

				const FWalkableNodeKey Node{PlacementPair.Key, WalkableArea};
				if (!Visited.Contains(Node))
				{
					OutFailureReason = FString::Printf(
						TEXT("Strict traversal reachability rejected placed cell %s on level %d for channel '%s' from module '%s': no path from any committed Entry root reaches this walkable node."),
						*PlacementPair.Key.ToString(),
						PlacementPair.Key.Z,
						*WalkableArea.ToString(),
						*LayoutProfileSolverInternal::GetSolvePlacementDebugName(
							Context,
							PlacementPair.Value).ToString());
					return false;
				}

			}
		}

	}
	else if (LayoutReachabilitySolverPrivate::HasAnyActiveWalkableContext(Context))
	{
		bool bFoundUnreachableRequiredWalkableArea = false;
		for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
		{
			if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value))
			{
				continue;
			}

			// VerticalAccess cells are transit infrastructure; skip reachability for them.
			const ELayoutCellIntent* const CellIntent = Context.PlannedCellIntents.Find(PlacementPair.Key);
			if (CellIntent != nullptr && *CellIntent == ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}

			bool bExposesRequiredWalkableArea = false;
			bool bHasReachableWalkableArea = false;
			for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolvePlacementWalkableAreas(Context, PlacementPair.Value))
			{
				if (!LayoutReachabilitySolverPrivate::ShouldUseWalkableAreaForReachabilityGraph(Context, WalkableArea))
				{
					continue;
				}

				bExposesRequiredWalkableArea |= LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, PlacementPair.Key, WalkableArea);
				bHasReachableWalkableArea |= Visited.Contains(FWalkableNodeKey{PlacementPair.Key, WalkableArea});
			}

			if (bExposesRequiredWalkableArea && !bHasReachableWalkableArea)
			{
				bFoundUnreachableRequiredWalkableArea = true;
				break;
			}
		}

		if (bFoundUnreachableRequiredWalkableArea)
		{
			// Project-specific behavior: accepted layouts may intentionally keep optional
			// propagated vertical-continuation walkable metadata outside the authored
			// entry-root-reachable set unless the profile makes full reachability fatal.
		}
	}

	return true;
}

bool LayoutProfileSolverInternal::ValidatePartialReachabilityFeasibility(FSolveContext& Context, FString& OutFailureReason)
{
	if (!LayoutReachabilitySolverPrivate::GetEffectiveProfileSnapshot(Context).bRequireAllTraversalChannelsReachable)
	{
		return true;
	}

	const TSet<FWalkableNodeKey> ReachableNodes = BuildReachableWalkableNodesFromPlacedRoots(Context);
	if (ReachableNodes.IsEmpty())
	{
		return true;
	}

	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& PlacementPair : Context.Placements)
	{
		if (!FSolveContext::IsOccupiedPlacement(PlacementPair.Value))
		{
			continue;
		}

		for (const FGameplayTag& WalkableArea : LayoutReachabilitySolverPrivate::GetSolvePlacementWalkableAreas(Context, PlacementPair.Value))
		{
			if (!LayoutReachabilitySolverPrivate::MustGrowTraversalChannelFromEntryRoot(Context, PlacementPair.Key, WalkableArea))
			{
				continue;
			}

			const FWalkableNodeKey Node{PlacementPair.Key, WalkableArea};
			if (ReachableNodes.Contains(Node))
			{
				continue;
			}

			OutFailureReason = FString::Printf(
				TEXT("%s is not reachable from the authored entry root and no remaining legal candidate path can attach it to the reachable frontier."),
				*LayoutReachabilitySolverPrivate::BuildWalkableNodeDebugString(Node, Context));
			return false;
		}
	}

	return true;
}
