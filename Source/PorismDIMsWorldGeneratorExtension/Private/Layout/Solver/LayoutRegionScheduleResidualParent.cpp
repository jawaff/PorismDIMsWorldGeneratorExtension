// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Algo/Unique.h"

#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutZoneFeatureDemand.h"
#include "LayoutRegionSchedulePlacementBridgeTypes.h"
#include "LayoutRegionScheduleSearchRuntime.h"
#include "LayoutProfileSolverInternal.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

/**
 * Private rewrite home for `FLayoutResidualParentAnalyzer`.
 *
 * This file will own residual parent support, connectivity, and counted
 * provider analysis once behavior moves out of the legacy scheduler body.
 */

namespace LayoutRegionScheduleSolverPrivate
{
	void BuildValidationChildReservationState(
		const TArray<FAutomaticChildPlacement>& CandidatePlacements,
		const TSet<FIntVector>& ParentProtectedTraversalCells,
		TSet<FIntVector>& OutChildReservedCells,
		TSet<FIntVector>& OutChildSupportingCells,
		TSet<FIntVector>& OutProtectedTraversalCells);
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
		FString* OutFailureReason = nullptr);
	bool RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString* OutFailureReason = nullptr);
	bool DoesParentPlanKeepAnchorCellsConnected(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TArray<FAutomaticChildPlacement>& ExistingPlacements,
		const TArray<FIntVector>& CandidateParentContactCells,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		const TArray<FLayoutCommittedTraversalAnchor>* CandidateParentTraversalAnchors = nullptr,
		const bool bAllowsChildTraversalBridgeForCommittedContacts = false,
		FString* OutFailureReason = nullptr);
	void AppendRecursiveParentIncomingBoundaryPoints(
		const TArray<FLayoutSolveBoundaryPoint>& ParentBoundaryPoints,
		const TArray<FIntVector>& ParentContactCells,
		TArray<FLayoutSolveBoundaryPoint>& InOutBoundaryPoints);
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
	void CollectProtectedParentTraversalCells(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TMap<FIntVector, ELayoutCellIntent>& ParentPlannedCellIntents,
		TSet<FIntVector>& OutProtectedParentCells);
	void AppendProtectedParentRouteReservationCells(
		const FLayoutSolveResult& RootPreparedTopology,
		TSet<FIntVector>& InOutProtectedParentCells);
	TSet<ELayoutFaceDirection> CollectVerticalAccessIngressDirections(const FLayoutModuleCatalog& ModuleCatalog);
	bool IsCommittedVerticalAccessRequirementSatisfied(const FCommittedVerticalAccessOwnership& Ownership);
	FString BuildCommittedVerticalAccessFailureReason(const FCommittedVerticalAccessOwnership& Ownership);
}

namespace LayoutRegionScheduleSolverFacade
{
	namespace
	{
		static const FLayoutId GroundedSameRegionSupportProvenance(TEXT("ParentSupport.SameRegion.Grounded"));
		static const FLayoutId StackedSameRegionSupportProvenance(TEXT("ParentSupport.SameRegion.Stacked"));
		static const FLayoutId ForwardedChildBundleSupportProvenance(TEXT("ParentSupport.ForwardedChild.BundleSupport"));

		static bool IsResidualEarlierCell(
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

		static bool IsStructurallyTraversableIntent(const ELayoutCellIntent Intent)
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
		}

		static bool IsCellOnParentPerimeter(
			const FIntVector& Cell,
			const FIntPoint& ParentFootprintSize)
		{
			return Cell.X == 0
				|| Cell.Y == 0
				|| Cell.X == ParentFootprintSize.X - 1
				|| Cell.Y == ParentFootprintSize.Y - 1;
		}

		static void SortCells(TArray<FIntVector>& Cells)
		{
			Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsResidualEarlierCell(Left, Right);
			});
		}

		static bool IsEarlierFaceDirection(
			const ELayoutFaceDirection Left,
			const ELayoutFaceDirection Right)
		{
			return static_cast<int32>(Left) < static_cast<int32>(Right);
		}

		static int32 ResolveResidualOpenCellCountFromPercent(const int32 EligibleCellCount, const float Percent)
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

		static void ResolveResidualOpenCellRange(
			const int32 EligibleCellCount,
			const float MinPercent,
			const float MaxPercent,
			const int32 MinCells,
			const int32 MaxCells,
			int32& OutMinOpenCells,
			int32& OutMaxOpenCells)
		{
			const int32 MinFromPercent = ResolveResidualOpenCellCountFromPercent(EligibleCellCount, MinPercent);
			const int32 MaxFromPercent = ResolveResidualOpenCellCountFromPercent(EligibleCellCount, MaxPercent);

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

		static TMap<FIntVector, ELayoutCellIntent> BuildPlannedCellIntentMap(
			const TArray<FLayoutPlannedCell>& PlannedCells)
		{
			TMap<FIntVector, ELayoutCellIntent> IntentsByCell;
			for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
			{
				IntentsByCell.Add(PlannedCell.Cell, PlannedCell.Intent);
			}
			return IntentsByCell;
		}


		struct FResidualStructuralSupportSummary
		{
			TSet<FIntVector> SupportedParentCells;
			TSet<FIntVector> SupportedReservedCells;
		};

		static FResidualStructuralSupportSummary BuildResidualStructuralSupportSummary(
			const TArray<FLayoutPlannedCell>& ResidualPlannedCells,
			const TSet<FIntVector>& ReservedCells)
		{
			FResidualStructuralSupportSummary SupportSummary;
			TSet<FIntVector> ResidualParentCells;
			for (const FLayoutPlannedCell& PlannedCell : ResidualPlannedCells)
			{
				ResidualParentCells.Add(PlannedCell.Cell);
				SupportSummary.SupportedParentCells.Add(PlannedCell.Cell);
			}

			for (const FIntVector& ReservedCell : ReservedCells)
			{
				SupportSummary.SupportedReservedCells.Add(ReservedCell);
			}

			bool bRemovedUnsupportedCell = true;
			while (bRemovedUnsupportedCell)
			{
				bRemovedUnsupportedCell = false;
				TArray<FIntVector> UnsupportedParentCells;
				TArray<FIntVector> UnsupportedReservedCells;

				auto IsCellCurrentlySupported = [&SupportSummary](const FIntVector& Cell)
				{
					return SupportSummary.SupportedParentCells.Contains(Cell)
						|| SupportSummary.SupportedReservedCells.Contains(Cell);
				};

				for (const FIntVector& Cell : SupportSummary.SupportedParentCells)
				{
					if (Cell.Z <= 0)
					{
						continue;
					}

					const FIntVector SupportCell = Cell + FIntVector(0, 0, -1);
					if (!IsCellCurrentlySupported(SupportCell))
					{
						UnsupportedParentCells.Add(Cell);
					}
				}

				for (const FIntVector& Cell : SupportSummary.SupportedReservedCells)
				{
					if (Cell.Z <= 0)
					{
						continue;
					}

					const FIntVector SupportCell = Cell + FIntVector(0, 0, -1);
					if (!IsCellCurrentlySupported(SupportCell))
					{
						UnsupportedReservedCells.Add(Cell);
					}
				}

				if (!UnsupportedParentCells.IsEmpty() || !UnsupportedReservedCells.IsEmpty())
				{
					bRemovedUnsupportedCell = true;
					for (const FIntVector& UnsupportedCell : UnsupportedParentCells)
					{
						SupportSummary.SupportedParentCells.Remove(UnsupportedCell);
					}
					for (const FIntVector& UnsupportedCell : UnsupportedReservedCells)
					{
						SupportSummary.SupportedReservedCells.Remove(UnsupportedCell);
					}
				}
			}

			return SupportSummary;
		}

		static FLayoutId ResolveResidualSupportProvenance(
			const FIntVector& Cell,
			const TSet<FIntVector>& SupportedParentCells,
			const TSet<FIntVector>& SupportedReservedCells)
		{
			if (Cell.Z <= 0)
			{
				return GroundedSameRegionSupportProvenance;
			}

			const FIntVector SupportCell = Cell + FIntVector(0, 0, -1);
			if (SupportedParentCells.Contains(SupportCell))
			{
				return StackedSameRegionSupportProvenance;
			}

			if (SupportedReservedCells.Contains(SupportCell))
			{
				return ForwardedChildBundleSupportProvenance;
			}

			return NAME_None;
		}

		static TArray<FIntVector> BuildRequiredChildBundleSupportCells(const TSet<FIntVector>& ReservedCells)
		{
			TSet<FIntVector> RequiredSupportCells;
			for (const FIntVector& ReservedCell : ReservedCells)
			{
				if (ReservedCell.Z <= 0)
				{
					continue;
				}

				const FIntVector SupportingReservedCell = ReservedCell + FIntVector(0, 0, -1);
				if (!ReservedCells.Contains(SupportingReservedCell))
				{
					RequiredSupportCells.Add(SupportingReservedCell);
				}
			}

			TArray<FIntVector> SortedRequiredSupportCells;
			for (const FIntVector& Cell : RequiredSupportCells)
			{
				SortedRequiredSupportCells.Add(Cell);
			}
			SortCells(SortedRequiredSupportCells);
			return SortedRequiredSupportCells;
		}

		static TArray<FIntVector> BuildMissingRequiredChildBundleSupportCells(
			const TArray<FIntVector>& RequiredSupportCells,
			const TArray<FLayoutPlannedCell>& ResidualSupportedPlannedCells)
		{
			TSet<FIntVector> SurvivingSupportedCells;
			for (const FLayoutPlannedCell& PlannedCell : ResidualSupportedPlannedCells)
			{
				SurvivingSupportedCells.Add(PlannedCell.Cell);
			}

			TArray<FIntVector> MissingSupportCells;
			for (const FIntVector& RequiredSupportCell : RequiredSupportCells)
			{
				if (!SurvivingSupportedCells.Contains(RequiredSupportCell))
				{
					MissingSupportCells.Add(RequiredSupportCell);
				}
			}
			SortCells(MissingSupportCells);
			return MissingSupportCells;
		}

		struct FResidualRootConnectivitySummary
		{
			TSet<FIntVector> RootConnectedCells;
			TMap<FIntVector, int32> ComponentIdByCell;
			TMap<FIntVector, TArray<ELayoutFaceDirection>> TraversableFacesByCell;
		};

		static FResidualRootConnectivitySummary BuildRootConnectedTraversableCells(
			const TArray<FIntVector>& ProtectedTraversalCells,
			const TArray<FLayoutCommittedEndpointAnchor>& RootExternalEndpointCommitments,
			const TMap<FIntVector, ELayoutCellIntent>& IntentsByCell,
			const TSet<FIntVector>& SupportedCells)
		{
			FResidualRootConnectivitySummary ConnectivitySummary;
			TSet<FIntVector> TraversableCells;
			for (const TPair<FIntVector, ELayoutCellIntent>& Pair : IntentsByCell)
			{
				if (SupportedCells.Contains(Pair.Key) && IsStructurallyTraversableIntent(Pair.Value))
				{
					TraversableCells.Add(Pair.Key);
				}
			}

			TArray<FIntVector> SeedCells;
			auto AddSeedCell = [&SeedCells](const FIntVector& Cell)
			{
				SeedCells.Add(Cell);
			};

			for (const TPair<FIntVector, ELayoutCellIntent>& Pair : IntentsByCell)
			{
				if (Pair.Value == ELayoutCellIntent::Entry)
				{
					AddSeedCell(Pair.Key);
				}
			}

			for (const FIntVector& Cell : ProtectedTraversalCells)
			{
				AddSeedCell(Cell);
			}

			for (const FLayoutCommittedEndpointAnchor& Anchor : RootExternalEndpointCommitments)
			{
				AddSeedCell(Anchor.LocalCell);
			}

			SeedCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsResidualEarlierCell(Left, Right);
			});
			SeedCells.SetNum(Algo::Unique(SeedCells));

			int32 NextComponentId = 0;
			for (const FIntVector& SeedCell : SeedCells)
			{
				if (!TraversableCells.Contains(SeedCell)
					|| ConnectivitySummary.ComponentIdByCell.Contains(SeedCell))
				{
					continue;
				}

				TArray<FIntVector> PendingCells;
				PendingCells.Add(SeedCell);
				ConnectivitySummary.RootConnectedCells.Add(SeedCell);
				ConnectivitySummary.ComponentIdByCell.Add(SeedCell, NextComponentId);

				while (!PendingCells.IsEmpty())
				{
					const FIntVector CurrentCell = PendingCells.Pop(EAllowShrinking::No);
					for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
					{
						const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
						const FIntVector NeighborCell = CurrentCell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (!TraversableCells.Contains(NeighborCell)
							|| ConnectivitySummary.ComponentIdByCell.Contains(NeighborCell))
						{
							continue;
						}

						ConnectivitySummary.RootConnectedCells.Add(NeighborCell);
						ConnectivitySummary.ComponentIdByCell.Add(NeighborCell, NextComponentId);
						PendingCells.Add(NeighborCell);
					}
				}

				++NextComponentId;
			}

			for (const FIntVector& Cell : ConnectivitySummary.RootConnectedCells)
			{
				TArray<ELayoutFaceDirection>& TraversableFaces =
					ConnectivitySummary.TraversableFacesByCell.FindOrAdd(Cell);
				for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
				{
					const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
					const FIntVector NeighborCell = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (ConnectivitySummary.RootConnectedCells.Contains(NeighborCell))
					{
						TraversableFaces.Add(Direction);
					}
				}

				TraversableFaces.Sort([](const ELayoutFaceDirection Left, const ELayoutFaceDirection Right)
				{
					return IsEarlierFaceDirection(Left, Right);
				});
			}

			return ConnectivitySummary;
		}

		static void AppendBoundaryClassifications(
			const TArray<FLayoutPlannedCell>& ResidualPlannedCells,
			const FIntPoint& ParentFootprintSize,
			FResidualParentCapabilitySummary& Summary)
		{
			struct FBoundaryClassificationAccumulator
			{
				int32 LevelIndex = 0;
				FLayoutId AdjacencyClassId;
				TSet<FIntVector> Cells;
			};

			TMap<FLayoutId, FBoundaryClassificationAccumulator> AccumulatorsById;
			auto AppendCellToClassification =
				[&AccumulatorsById](const FIntVector& Cell, const int32 LevelIndex, const TCHAR* SideName)
			{
				const FLayoutId ClassificationId(*FString::Printf(
					TEXT("ResidualBoundary.Level%d.%s"),
					LevelIndex,
					SideName));
				FBoundaryClassificationAccumulator& Accumulator = AccumulatorsById.FindOrAdd(ClassificationId);
				Accumulator.LevelIndex = LevelIndex;
				Accumulator.AdjacencyClassId = FLayoutId(*FString::Printf(TEXT("Perimeter.%s"), SideName));
				Accumulator.Cells.Add(Cell);
			};

			for (const FLayoutPlannedCell& PlannedCell : ResidualPlannedCells)
			{
				if (!IsCellOnParentPerimeter(PlannedCell.Cell, ParentFootprintSize))
				{
					continue;
				}

				Summary.SeamRelevantBoundaryCells.Add(PlannedCell.Cell);
				if (PlannedCell.Cell.X == 0)
				{
					AppendCellToClassification(PlannedCell.Cell, PlannedCell.Cell.Z, TEXT("NegX"));
				}
				if (PlannedCell.Cell.X == ParentFootprintSize.X - 1)
				{
					AppendCellToClassification(PlannedCell.Cell, PlannedCell.Cell.Z, TEXT("PosX"));
				}
				if (PlannedCell.Cell.Y == 0)
				{
					AppendCellToClassification(PlannedCell.Cell, PlannedCell.Cell.Z, TEXT("NegY"));
				}
				if (PlannedCell.Cell.Y == ParentFootprintSize.Y - 1)
				{
					AppendCellToClassification(PlannedCell.Cell, PlannedCell.Cell.Z, TEXT("PosY"));
				}
			}

			Summary.SeamRelevantBoundaryCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				return IsResidualEarlierCell(Left, Right);
			});
			Summary.SeamRelevantBoundaryCells.SetNum(Algo::Unique(Summary.SeamRelevantBoundaryCells));

			for (const TPair<FLayoutId, FBoundaryClassificationAccumulator>& Pair : AccumulatorsById)
			{
				FResidualBoundarySpanClassification& Classification =
					Summary.BoundarySpanClassifications.AddDefaulted_GetRef();
				Classification.ClassificationId = Pair.Key;
				Classification.LevelIndex = Pair.Value.LevelIndex;
				Classification.AdjacencyClassId = Pair.Value.AdjacencyClassId;
				Classification.bRelevantToSeamOwnership = true;
				Classification.bRelevantToJunctionOwnership = true;
				for (const FIntVector& Cell : Pair.Value.Cells)
				{
					Classification.Cells.Add(Cell);
				}
				SortCells(Classification.Cells);
			}

			Summary.BoundarySpanClassifications.Sort([](
				const FResidualBoundarySpanClassification& Left,
				const FResidualBoundarySpanClassification& Right)
			{
				if (Left.LevelIndex != Right.LevelIndex)
				{
					return Left.LevelIndex < Right.LevelIndex;
				}

				return Left.ClassificationId.LexicalLess(Right.ClassificationId);
			});
		}
	}

	FResidualParentCapabilitySummary BuildResidualParentCapabilitySummary(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& RootConnectedTraversableCells,
		const TArray<FIntVector>& CountedParentVerticalAccessCells)
	{
		FResidualParentCapabilitySummary Summary;
		Summary.PlannedCells = ParentPlannedCells;
		Summary.RootConnectedTraversableCells = RootConnectedTraversableCells;
		Summary.CountedParentVerticalAccessCells = CountedParentVerticalAccessCells;
		SortCells(Summary.CountedParentVerticalAccessCells);
		return Summary;
	}

	FResidualParentCapabilitySummary RebuildResidualParentSummaryForDemand(
		const FCompiledStructuralInputs& StructuralInputs,
		const FNegotiationDemandPlan& DemandPlan)
	{
		const TSet<FIntVector> ReservedCells(DemandPlan.ReservedParentCells);
		TArray<FLayoutPlannedCell> ResidualPlannedCells;
		for (const FLayoutPlannedCell& PlannedCell : StructuralInputs.RootPlannedCells)
		{
			if (ReservedCells.Contains(PlannedCell.Cell))
			{
				continue;
			}

			ResidualPlannedCells.Add(PlannedCell);
		}

		const FResidualStructuralSupportSummary SupportSummary =
			BuildResidualStructuralSupportSummary(ResidualPlannedCells, ReservedCells);
		FResidualParentCapabilitySummary Summary;
		Summary.PlannedCells.Reserve(ResidualPlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : ResidualPlannedCells)
		{
			if (!SupportSummary.SupportedParentCells.Contains(PlannedCell.Cell))
			{
				continue;
			}

			Summary.PlannedCells.Add(PlannedCell);
			Summary.SupportProvenanceByCell.Add(
				PlannedCell.Cell,
				ResolveResidualSupportProvenance(
					PlannedCell.Cell,
					SupportSummary.SupportedParentCells,
					SupportSummary.SupportedReservedCells));
		}
		Summary.RequiredChildBundleSupportCells = BuildRequiredChildBundleSupportCells(ReservedCells);
		Summary.MissingRequiredChildBundleSupportCells = BuildMissingRequiredChildBundleSupportCells(
			Summary.RequiredChildBundleSupportCells,
			Summary.PlannedCells);

		const TMap<FIntVector, ELayoutCellIntent> IntentsByCell =
			BuildPlannedCellIntentMap(Summary.PlannedCells);
		const FResidualRootConnectivitySummary ConnectivitySummary =
			BuildRootConnectedTraversableCells(
				StructuralInputs.RootProtectedTraversalCells,
				StructuralInputs.RootExternalEndpointCommitments,
				IntentsByCell,
				SupportSummary.SupportedParentCells);
		Summary.RootConnectedTraversableCells = ConnectivitySummary.RootConnectedCells;
		Summary.RootConnectedComponentIdByCell = ConnectivitySummary.ComponentIdByCell;
		Summary.RootConnectedTraversableFacesByCell = ConnectivitySummary.TraversableFacesByCell;

		// Route-support vertical-access cells: root-connected VerticalAccess cells that
		// remain available for parent traversal feasibility and derived parent-plan
		// shaping after child replacement volumes displace parent cells.
		//
		// Counted parent providers are no longer populated here. Per the recursive
		// negotiation design, counted providers are classified by negotiation from the
		// route-support surface minus cells that serve only as lower-level handoffs to
		// child-proved ascent. Populating both arrays identically caused the route-
		// support subtraction filter in negotiation to be a no-op, leaving the castle
		// case with a spurious Composed + extra-parent-provider result.
		for (const FLayoutPlannedCell& PlannedCell : Summary.PlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::VerticalAccess
				&& Summary.RootConnectedTraversableCells.Contains(PlannedCell.Cell))
			{
				Summary.RouteSupportVerticalAccessCells.Add(PlannedCell.Cell);
			}
		}
		SortCells(Summary.RouteSupportVerticalAccessCells);

		for (const FLayoutCommittedEndpointAnchor& Anchor : StructuralInputs.RootExternalEndpointCommitments)
		{
			FResidualExternalEndpointReachability& Reachability =
				Summary.ExternalEndpointReachability.AddDefaulted_GetRef();
			Reachability.CommitmentId = Anchor.CommitmentId;
			Reachability.bReachableFromResidualParent =
				Summary.RootConnectedTraversableCells.Contains(Anchor.LocalCell);
			Reachability.ParentComponentId = Reachability.bReachableFromResidualParent
				? Summary.RootConnectedComponentIdByCell.FindRef(Anchor.LocalCell)
				: INDEX_NONE;
		}
		AppendBoundaryClassifications(
			Summary.PlannedCells,
			StructuralInputs.RootFootprintSize,
			Summary);
		return Summary;
	}

		FResidualParentCapabilitySummary RebuildResidualParentSummaryForDemand(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FNegotiationDemandPlan& DemandPlan)
		{
			return RebuildResidualParentSummaryForDemand(
				BuildCompiledStructuralInputs(SolveContext),
				DemandPlan);
		}
	}

namespace LayoutRegionScheduleSolverPrivate
{
	/** Projects certified child restrictions onto retained parent host domains for deferred proof. */
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
		const bool bAuthorityCellsRemainSelectable,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FString& OutValidationFailureReason,
		FLayoutRegionSolveRequest& OutParentRequest,
		FCommittedVerticalAccessOwnership* OutEffectiveVerticalAccessOwnership = nullptr)
	{
		SCOPED_NAMED_EVENT(Layout_ParentValidation_RequestBuild, FColor::Orange);
		FCommittedVerticalAccessOwnership EffectiveVerticalAccessOwnership = VerticalAccessOwnership;

		TSet<FIntVector> ChildReservedCells;
		TSet<FIntVector> ChildSupportingCells;
		TSet<FIntVector> ProtectedTraversalCells;
		BuildValidationChildReservationState(
			CandidatePlacements,
			ParentProtectedTraversalCells,
			ChildReservedCells,
			ChildSupportingCells,
			ProtectedTraversalCells);

		OutParentRequest = RootRequest;
		if (OutParentRequest.OwningTopModuleLevelByXY.IsEmpty())
		{
			for (const FLayoutPlannedCell& Cell : ParentPlannedCells)
			{
				if (Cell.bIsBridgeCell) continue;
				const int32 Level = Cell.ModuleLevelIndex != INDEX_NONE ? Cell.ModuleLevelIndex : Cell.Cell.Z;
				int32& Top = OutParentRequest.OwningTopModuleLevelByXY.FindOrAdd(FIntPoint(Cell.Cell.X, Cell.Cell.Y));
				Top = FMath::Max(Top, Level);
			}
		}
		// Parent proof still constructs local routes, while final reachability waits
		// for merged child traversal carriers that are absent from parent-only CSP.
		OutParentRequest.bDeferTraversalValidationToSchedule = true;
		OutParentRequest.NegotiatedChildResponsibilityContracts.Reset();
		OutParentRequest.ChildContentSetSnapshots.Reset();
		OutParentRequest.ChildModuleCatalogs.Reset();
		OutParentRequest.ChildPlannedCellsByRegion.Reset();
		TMap<FIntVector, FLayoutCellCandidateDomainRestriction> ParentRestrictionsByCell;
		for (const FLayoutCellCandidateDomainRestriction& Restriction :
			RootRequest.CandidateDomainRestrictions)
		{
			// Prepared VerticalAccess witness remains provisional whenever its group
			// retains alternatives for reconciliation after child settlement.
			const FString RestrictionId = Restriction.RestrictionId.ToString();
			const bool bHasRetainedHostAlternatives =
				RootRequest.VerticalAccessHostGroups.ContainsByPredicate(
					[&RestrictionId](const FLayoutVerticalAccessHostGroup& Group)
					{
						if (Group.Options.Num() <= 1)
						{
							return false;
						}
						const FString GroupPrefix = Group.GroupId.ToString();
						return RestrictionId.StartsWith(GroupPrefix + TEXT(".Exact."))
							|| RestrictionId.StartsWith(GroupPrefix + TEXT(".Support."));
					});
			if (bHasRetainedHostAlternatives)
			{
				continue;
			}
			FLayoutCellCandidateDomainRestriction Restored = Restriction;
			if (Restored.BeforeVerticalAccessCandidates.IsSet()
				&& RootRequest.VerticalAccessHostGroups.ContainsByPredicate(
					[](const FLayoutVerticalAccessHostGroup& Group) { return Group.Options.Num() > 1; }))
			{
				Restored.AllowedCandidates = MoveTemp(Restored.BeforeVerticalAccessCandidates.GetValue());
				Restored.RestrictionId = Restored.BeforeVerticalAccessRestrictionId;
				Restored.BeforeVerticalAccessRestrictionId = NAME_None;
				Restored.BeforeVerticalAccessCandidates.Reset();
			}
			ParentRestrictionsByCell.Add(Restored.Cell, MoveTemp(Restored));
		}
		TArray<FString> BoundaryCertificateParts;
		if (!RootRequest.CandidateDomainCertificateId.IsNone())
		{
			BoundaryCertificateParts.Add(
				RootRequest.CandidateDomainCertificateId.ToString());
		}
		const auto SeamSideContainsCell = [](
			const FIntVector& Cell,
			const FIntVector& Start,
			const FIntVector& End)
		{
			return Cell.X >= FMath::Min(Start.X, End.X)
				&& Cell.X <= FMath::Max(Start.X, End.X)
				&& Cell.Y >= FMath::Min(Start.Y, End.Y)
				&& Cell.Y <= FMath::Max(Start.Y, End.Y)
				&& Cell.Z >= FMath::Min(Start.Z, End.Z)
				&& Cell.Z <= FMath::Max(Start.Z, End.Z);
		};
		for (const FAutomaticChildPlacement& ChildPlacement : CandidatePlacements)
		{
			BoundaryCertificateParts.Add(ChildPlacement.BoundaryCertificateId.ToString());
			OutParentRequest.NegotiatedChildResponsibilityContracts.Add(
				ChildPlacement.NegotiatedResponsibilityContract);
			OutParentRequest.ChildContentSetSnapshots.Add(
				ChildPlacement.ChildRegionDebugPath,
				ChildPlacement.ChildRequest.ContentSetSnapshot);
			OutParentRequest.ChildModuleCatalogs.Add(
				ChildPlacement.ChildRegionDebugPath,
				ChildPlacement.ChildRequest.ModuleCatalog);
			OutParentRequest.ChildPlannedCellsByRegion.Add(
				ChildPlacement.ChildRegionDebugPath,
				ChildPlacement.ParentTranslatedPlannedCells);
			for (const FLayoutCellCandidateDomainRestriction& Restriction :
				ChildPlacement.ParentDomainRestrictions)
			{
				if (bAuthorityCellsRemainSelectable)
				{
					const bool bPotentialHostCell =
						RootRequest.VerticalAccessHostGroups.ContainsByPredicate(
							[&Restriction](const FLayoutVerticalAccessHostGroup& Group)
							{
								return Group.Options.ContainsByPredicate(
									[&Restriction](const FLayoutVerticalAccessHostOption& Option)
									{
										return Option.LowerCell == Restriction.Cell;
									});
							});
					const bool bPotentialEntryCell =
						RootRequest.IncomingBoundaryPoints.IsEmpty()
						&& Restriction.Cell.Z == 0
						&& (Restriction.Cell.X == 0
							|| Restriction.Cell.Y == 0
							|| Restriction.Cell.X == ParentFootprintSize.X - 1
							|| Restriction.Cell.Y == ParentFootprintSize.Y - 1);
					if (bPotentialHostCell || bPotentialEntryCell)
					{
						continue;
					}
				}
				const bool bTargetsSharedParentCell =
					ChildPlacement.SharedParentChildFaces.ContainsByPredicate(
						[&Restriction](const FSharedParentChildFace& Face)
						{
							return Face.ParentCell == Restriction.Cell;
						});
				const bool bSharedSeamRestriction = bTargetsSharedParentCell
					|| Restriction.RestrictionId.ToString().StartsWith(
						TEXT("ParentSeam."));
				if (bSharedSeamRestriction)
				{
					bool bParentOwnsCommittedSeam = false;
					bool bParentIsSuppressedAtCell = false;
					for (const FLayoutPartitionSeamRecord& Seam : PlannedPartitionSeams)
					{
						const bool bParentOwnsChildPassive =
							Seam.OwnerRegionDebugPath == RootRequest.RegionDebugPath
							&& Seam.PassiveRegionDebugPath == ChildPlacement.ChildRegionDebugPath
							&& SeamSideContainsCell(
								Restriction.Cell,
								Seam.OwnerStartCell,
								Seam.OwnerEndCell);
						bParentOwnsCommittedSeam |= bParentOwnsChildPassive;
						bParentIsSuppressedAtCell |=
							Seam.OwnerRegionDebugPath != RootRequest.RegionDebugPath
							&& Seam.PassiveRegionDebugPath == RootRequest.RegionDebugPath
							&& SeamSideContainsCell(
								Restriction.Cell,
								Seam.PassiveStartCell,
								Seam.PassiveEndCell);
					}
					// Passive shared-shell placements are suppressed after proof. Their
					// pairwise reciprocal witness remains authoritative, but they must
					// not constrain one nonexistent parent module across several owners.
					if (bParentIsSuppressedAtCell || !bParentOwnsCommittedSeam)
					{
						continue;
					}
				}

				if (FLayoutCellCandidateDomainRestriction* Existing =
						ParentRestrictionsByCell.Find(Restriction.Cell))
				{
					const TArray<FLayoutCandidateVariantIdentity> ExistingCandidates =
						Existing->AllowedCandidates;
					// New child authority must survive removal of the temporary VA overlay.
					if (Existing->BeforeVerticalAccessCandidates.IsSet())
					{
						Existing->BeforeVerticalAccessCandidates->RemoveAll(
							[&Restriction](const FLayoutCandidateVariantIdentity& Candidate)
							{
								return !Restriction.AllowedCandidates.ContainsByPredicate(
									[&Candidate](const FLayoutCandidateVariantIdentity& Other)
									{
										return Candidate.ModuleSnapshotId == Other.ModuleSnapshotId
											&& Candidate.YawRotationSteps == Other.YawRotationSteps;
									});
							});
					}
					Existing->AllowedCandidates.RemoveAll(
						[&Restriction](const FLayoutCandidateVariantIdentity& ExistingCandidate)
						{
							return !Restriction.AllowedCandidates.ContainsByPredicate(
								[&ExistingCandidate](const FLayoutCandidateVariantIdentity& Candidate)
								{
									return Candidate.ModuleSnapshotId == ExistingCandidate.ModuleSnapshotId
										&& Candidate.YawRotationSteps == ExistingCandidate.YawRotationSteps;
								});
						});
					if (Existing->AllowedCandidates.IsEmpty())
					{
						const auto DescribeCandidates = [](
							const TArray<FLayoutCandidateVariantIdentity>& Candidates)
						{
							return Candidates.IsEmpty()
								? FString(TEXT("<none>"))
								: FString::JoinBy(
									Candidates,
									TEXT(","),
									[](const FLayoutCandidateVariantIdentity& Candidate)
									{
										return FString::Printf(
											TEXT("%s@yaw%d"),
											*Candidate.ModuleSnapshotId.ToString(),
											Candidate.YawRotationSteps);
									});
						};
						TArray<FString> CellSeamParts;
						for (const FLayoutPartitionSeamRecord& Seam : PlannedPartitionSeams)
						{
							if (SeamSideContainsCell(
									Restriction.Cell,
									Seam.OwnerStartCell,
									Seam.OwnerEndCell)
								|| SeamSideContainsCell(
									Restriction.Cell,
									Seam.PassiveStartCell,
									Seam.PassiveEndCell))
							{
								CellSeamParts.Add(FString::Printf(
									TEXT("%s>%s:%s"),
									*Seam.OwnerRegionDebugPath,
									*Seam.PassiveRegionDebugPath,
									*Seam.InterfaceFamily.ToString()));
							}
						}
						OutValidationFailureReason = FString::Printf(
							TEXT("Certified child boundary domains have no common parent candidate at %s. Existing=[%s] Incoming=[%s] Child=%s Seams=[%s]."),
							*Restriction.Cell.ToString(),
							*DescribeCandidates(ExistingCandidates),
							*DescribeCandidates(Restriction.AllowedCandidates),
							*ChildPlacement.ChildRegionDebugPath,
							CellSeamParts.IsEmpty()
								? TEXT("<none>")
								: *FString::Join(CellSeamParts, TEXT(",")));
						return false;
					}
				}
				else
				{
					ParentRestrictionsByCell.Add(Restriction.Cell, Restriction);
				}
			}
		}
		BoundaryCertificateParts.Sort();
		OutParentRequest.CandidateDomainCertificateId = FLayoutId(*FString::Printf(
			TEXT("ParentBoundary.%08X"),
			FCrc::StrCrc32(*FString::Join(BoundaryCertificateParts, TEXT("|")))));
		ParentRestrictionsByCell.GenerateValueArray(
			OutParentRequest.CandidateDomainRestrictions);
		OutParentRequest.CandidateDomainRestrictions.Sort(
			[](const FLayoutCellCandidateDomainRestriction& Left,
				const FLayoutCellCandidateDomainRestriction& Right)
			{
				if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
				if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
				return Left.Cell.X < Right.Cell.X;
			});
		// Preserve these rebound incoming certificates when appending descendant boundaries below.
		for (FLayoutSolveBoundaryPoint& BoundaryPoint :
			OutParentRequest.IncomingBoundaryPoints)
		{
			if (!BoundaryPoint.bUsesCertifiedReciprocalDomain)
			{
				continue;
			}
			const FIntVector RestrictedCell = BoundaryPoint.LocalCell
				+ FLayoutDirectionUtils::ToCellDelta(BoundaryPoint.FaceDirection);
			const FLayoutCellCandidateDomainRestriction* Restriction =
				ParentRestrictionsByCell.Find(RestrictedCell);
			if (Restriction != nullptr)
			{
				BoundaryPoint.CertifiedDomainCertificateId =
					OutParentRequest.CandidateDomainCertificateId;
				BoundaryPoint.CertifiedDomainRestrictionId = Restriction->RestrictionId;
			}
		}
		// Feed exact selected direct-child commitments into residual parent
		// module search so one ledger enforces original Min/Max.
		OutParentRequest.PrecommittedZoneFeatureProviderCommitments.Reset();
		for (const FAutomaticChildPlacement& ChildPlacement : CandidatePlacements)
		{
			OutParentRequest.PrecommittedZoneFeatureProviderCommitments.Append(
				ChildPlacement.DirectChildCommitment.ZoneFeatureProviderCommitments);
			const FLayoutRegionContentEntrySolveSnapshot* SourceEntry =
				RootRequest.ContentSetSnapshot.Entries.FindByPredicate(
					[&ChildPlacement](const FLayoutRegionContentEntrySolveSnapshot& Entry)
					{
						return Entry.EntryId == ChildPlacement.EntryId;
					});
			const FLayoutChildStageMappedCell* ProviderRoot =
				ChildPlacement.StageMapping.Cells.FindByPredicate(
					[](const FLayoutChildStageMappedCell& Cell)
					{
						return Cell.SourceChildCell == FIntVector::ZeroValue;
					});
			if (ProviderRoot == nullptr && !ChildPlacement.StageMapping.Cells.IsEmpty())
			{
				ProviderRoot = &ChildPlacement.StageMapping.Cells[0];
			}
			if (SourceEntry == nullptr || ProviderRoot == nullptr)
			{
				continue;
			}
			for (const FLayoutZoneFeatureRequirement& Requirement :
				RootRequest.ProfileSnapshot.ZoneFeatureRequirements)
			{
				if (!LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchRequirement(
						SourceEntry->ProvidedZoneFeatures,
						Requirement)
					|| !LayoutZoneFeatureDemand::DoPotentialPlacementZonesOverlap(
						SourceEntry->ChildPlacementZone,
						Requirement.Zone))
				{
					continue;
				}
				const FLayoutId CommitmentId =
					LayoutZoneFeatureDemand::BuildProviderCommitmentId(
						RootRequest.EffectiveSnapshotId,
						RootRequest.RegionDebugPath,
						Requirement.RequirementId,
						SourceEntry->EntryId,
						ProviderRoot->ParentCell,
						ProviderRoot->ModuleLevelIndex,
						ProviderRoot->TerrainStageIndex);
				if (OutParentRequest.PrecommittedZoneFeatureProviderCommitments.ContainsByPredicate(
					[CommitmentId](const FLayoutZoneFeatureProviderCommitment& Existing)
					{
						return Existing.ProviderCommitmentId == CommitmentId;
					}))
				{
					continue;
				}
				FLayoutZoneFeatureProviderCommitment& Commitment =
					OutParentRequest.PrecommittedZoneFeatureProviderCommitments.AddDefaulted_GetRef();
				Commitment.RequirementId = Requirement.RequirementId;
				Commitment.ProviderCommitmentId = CommitmentId;
				Commitment.SourceRegionDebugPath = RootRequest.RegionDebugPath;
				Commitment.SourceContentEntryId = SourceEntry->EntryId;
				Commitment.Cell = ProviderRoot->ParentCell;
				Commitment.ModuleLevelIndex = ProviderRoot->ModuleLevelIndex;
				Commitment.TerrainStageIndex = ProviderRoot->TerrainStageIndex;
			}
		}
		OutParentRequest.PrecommittedZoneFeatureProviderCommitments.Sort(
			[](const FLayoutZoneFeatureProviderCommitment& Left,
				const FLayoutZoneFeatureProviderCommitment& Right)
			{
				return Left.ProviderCommitmentId.LexicalLess(
					Right.ProviderCommitmentId);
			});
		OutParentRequest.ContentSetSnapshot.Entries.RemoveAll(
			[](const FLayoutRegionContentEntrySolveSnapshot& Entry)
			{
				return Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
			});
		/* !OutParentRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		OutParentRequest.FootprintSize = ParentFootprintSize;
		OutParentRequest.RegionCellOffset = FIntVector::ZeroValue;
		OutParentRequest.PlannedCells.Reset();
		TSet<FIntVector> ParentOwnedJunctionCells;
		for (const LayoutRegionScheduleSolverFacade::FOwnedSeamJunctionRequirement& Junction :
			LayoutRegionScheduleSolverFacade::BuildOwnerSideJunctionRequirements(
				PlannedPartitionSeams, &RootRequest))
		{
			if (Junction.OwnerRegionDebugPath == RootRequest.RegionDebugPath)
			{
				ParentOwnedJunctionCells.Add(Junction.JunctionCell);
			}
		}

		TArray<FLayoutPlannedCell> JunctionAwareParentPlannedCells = ParentPlannedCells;
		for (FLayoutPlannedCell& PlannedCell : JunctionAwareParentPlannedCells)
		{
			if (ParentOwnedJunctionCells.Contains(PlannedCell.Cell))
			{
				PlannedCell.PlacementZone = ELayoutPlacementZone::Corner;
			}
		}
		TArray<FLayoutVerticalAccessHostGroup> BoundaryCompatibleHostGroups =
			RootRequest.VerticalAccessHostGroups;
		if (BoundaryCompatibleHostGroups.IsEmpty()
			&& RootRequest.ProfileSnapshot.LevelCount > 1
			&& RootRequest.ProfileSnapshot.VerticalAccessCountMode
				!= ELayoutCountConstraintMode::None)
		{
			// Root preflight can generate host groups on its mutable request copy.
			// Rebuild those authored alternatives for residual-parent reconciliation
			// instead of freezing selected result cells without reassignment domains.
			FString HostRebuildFailure;
			if (!LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
					RootRequest,
					JunctionAwareParentPlannedCells,
					BoundaryCompatibleHostGroups,
					HostRebuildFailure))
			{
				OutValidationFailureReason = FString::Printf(
					TEXT("Residual parent could not rebuild authored VerticalAccess host alternatives: %s"),
					*HostRebuildFailure);
				return false;
			}
		}
		// Keep module-admitted hosts that touch child boundary restrictions. Those
		// restrictions constrain candidate/yaw, not ownership; exact parent CSP
		// intersects them after child replacement filtering settles host geometry.
		if (!TryBuildParentPlannedCellsWithReservedChildren(
			JunctionAwareParentPlannedCells,
			ParentPlannedCellSet,
			ParentFootprintSize,
			ChildReservedCells,
			ProtectedTraversalCells,
			RootRequest.Seed,
			RootRequest.IncomingBoundaryPoints.IsEmpty(),
			EffectiveVerticalAccessOwnership,
			BoundaryCompatibleHostGroups,
			OutParentRequest.PlannedCells,
			&OutParentRequest.VerticalAccessHostGroups,
			&OutValidationFailureReason))
		{
			return false;
		}
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[RecursiveParentHostGroups] region=%s groups=%s"),
				*RootRequest.RegionDebugPath,
				*FString::JoinBy(OutParentRequest.VerticalAccessHostGroups, TEXT("|"), [](const FLayoutVerticalAccessHostGroup& Group)
				{
					return FString::Printf(
						TEXT("%s:%s"),
						*Group.GroupId.ToString(),
						Group.Options.IsEmpty() ? TEXT("<none>") : *Group.Options[0].LowerCell.ToString());
				}));
		}
		OutParentRequest.ExternalPlannedNeighborFaceMasks.Reset();
		for (const FLayoutPlannedCell& ParentCell : OutParentRequest.PlannedCells)
		{
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				const ELayoutFaceDirection Direction =
					static_cast<ELayoutFaceDirection>(DirectionIndex);
				if (ChildReservedCells.Contains(
					ParentCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction)))
				{
					OutParentRequest.ExternalPlannedNeighborFaceMasks.FindOrAdd(ParentCell.Cell)
						|= LayoutFaceDirectionMask(Direction);
				}
			}
		}

		// Request-backed solving prefers precomputed carriers. Project frozen terrain authority
		// onto the residual parent plan without marking child-delegated cells as terrain-removed.
		OutParentRequest.PrecomputedPlannedCells = OutParentRequest.PlannedCells;
		// Residual topology already carries relocated Entries and rebuilt, child-filtered
		// exact host alternatives. A leaf proof must not seed fresh Entries/hosts over it.
		OutParentRequest.bHasFinalizedSteppedTerrainIntents = true;
		TSet<FIntVector> ResidualParentCells;
		ResidualParentCells.Reserve(OutParentRequest.PlannedCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : OutParentRequest.PlannedCells)
		{
			ResidualParentCells.Add(PlannedCell.Cell);
		}
		OutParentRequest.PrecomputedActiveCells.RemoveAll(
			[&ResidualParentCells](const FLayoutContractActiveCellRecord& ActiveCell)
			{
				return !ResidualParentCells.Contains(ActiveCell.Cell);
			});
		OutParentRequest.PrecomputedFrozenTerrainContract.ActiveCells.RemoveAll(
			[&ResidualParentCells](const FLayoutContractActiveCellRecord& ActiveCell)
			{
				return !ResidualParentCells.Contains(ActiveCell.Cell);
			});
		OutParentRequest.PrecomputedFrozenTerrainContract.CellContracts.RemoveAll(
			[&ResidualParentCells](const FLayoutTerrainCellContractRecord& CellContract)
			{
				return !ResidualParentCells.Contains(CellContract.Cell);
			});
		OutParentRequest.PrecomputedFrozenTerrainContract.TerrainBackedNeighborFaces.RemoveAll(
			[&ResidualParentCells](const FLayoutTerrainBackedNeighborFaceRecord& FaceRecord)
			{
				return !ResidualParentCells.Contains(FaceRecord.Cell);
			});

		if (!RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
			OutParentRequest.PlannedCells,
			EffectiveVerticalAccessOwnership,
			&OutValidationFailureReason))
		{
			return false;
		}


		if (!DoesParentPlanKeepAnchorCellsConnected(
			OutParentRequest.PlannedCells,
			CandidatePlacements,
			TArray<FIntVector>(),
			EffectiveVerticalAccessOwnership,
			nullptr,
			false,
			&OutValidationFailureReason))
		{
			return false;
		}

		for (const FAutomaticChildPlacement& ChildPlacement : CandidatePlacements)
		{
			AppendRecursiveParentIncomingBoundaryPoints(
				ChildPlacement.ParentTranslatedBoundaryPoints,
				ChildPlacement.ParentCommittedEntryCells,
				OutParentRequest.IncomingBoundaryPoints);
			AppendSyntheticParentSupportBoundaryPoints(
				ChildPlacement.ParentTranslatedSupportingCells,
				ChildPlacement.ParentTranslatedPlannedCells,
				FIntVector::ZeroValue,
				ChildPlacement.ChildRegionDebugPath,
				ParentPlannedCellSet,
				ParentPlannedCellIntents,
				OutParentRequest.ProfileSnapshot,
				OutParentRequest.PlannedCells,
				ChildReservedCells,
				ChildPlacement.SharedParentChildFaces,
				ChildPlacement.BoundaryCertificateId,
				ChildPlacement.ParentDomainRestrictions,
				OutParentRequest.IncomingBoundaryPoints);
			for (const FLayoutCommittedTraversalAnchor& TraversalAnchor :
				ChildPlacement.ParentCommittedTraversalAnchors)
			{
				for (FLayoutSolveBoundaryPoint& BoundaryPoint :
					OutParentRequest.IncomingBoundaryPoints)
				{
					if (BoundaryPoint.SourceRegionDebugPath
							== ChildPlacement.ChildRegionDebugPath
						&& BoundaryPoint.LocalCell
								+ FLayoutDirectionUtils::ToCellDelta(
									BoundaryPoint.FaceDirection)
							== TraversalAnchor.Cell)
					{
						// Exact parent-side traversal commitment outranks generic support
						// evidence when both describe the same child handoff edge.
						BoundaryPoint.ConnectedTraversalChannels.AddTag(
							TraversalAnchor.TraversalChannel);
					}
				}
			}
			// Host-ascent ownership does not discharge lateral child Entry handoff.
			// Parent proof always consumes parent-side traversal anchors without
			// seeding child-owned Entry roles.
			OutParentRequest.CommittedTraversalAnchors.Append(
				ChildPlacement.ParentCommittedTraversalAnchors);
		}

		if (OutEffectiveVerticalAccessOwnership != nullptr)
		{
			*OutEffectiveVerticalAccessOwnership = EffectiveVerticalAccessOwnership;
		}
		return true;
	}

	/** Shares exact Entry/host settlement between structural prewarm and complete deferred proof. */
	bool TryFreezeParentAuthorityForPlacements(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FAutomaticChildPlacement>& Placements,
		FLayoutRegionSolveRequest& InOutParentRequest,
		FCommittedVerticalAccessOwnership& InOutOwnership,
		FString& OutFailureReason,
		FLayoutRegionSolveResult* OutFinalProof)
	{
		SCOPED_NAMED_EVENT(Layout_ParentValidation_AuthorityFreeze, FColor::Orange);
		TSet<FIntVector> ForbiddenEntryCells;
		TSet<FIntVector> ForbiddenVerticalAccessCells;
		for (const FAutomaticChildPlacement& Placement : Placements)
		{
			for (const FIntVector& Contact : Placement.ParentCommittedEntryCells)
			{
				ForbiddenEntryCells.Add(Contact);
				ForbiddenVerticalAccessCells.Add(Contact);
			}
			for (const FLayoutPlannedCell& ChildCell : Placement.ParentTranslatedPlannedCells)
			{
				ForbiddenEntryCells.Add(ChildCell.Cell);
				ForbiddenVerticalAccessCells.Add(ChildCell.Cell);
				for (const ELayoutFaceDirection Direction : {
					ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
				{
					const FIntVector Neighbor = ChildCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (!Placement.ParentCommittedEntryCells.Contains(Neighbor)) ForbiddenEntryCells.Add(Neighbor);
				}
			}
		}
		FLayoutRegionSolveRequest Frozen;
		if (!LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
				InOutParentRequest, RootRequest.IncomingBoundaryPoints.IsEmpty(),
				ForbiddenEntryCells, ForbiddenVerticalAccessCells, Frozen, OutFailureReason, OutFinalProof)
			|| !RefreshCommittedParentVerticalAccessCellsFromDerivedPlan(
				Frozen.PlannedCells, InOutOwnership, &OutFailureReason))
		{
			return false;
		}
		InOutParentRequest = MoveTemp(Frozen);
		return true;
	}

/** Rebuilds the reserved parent proof request used by the placement-backed coordinator bridge. */
	bool BuildPlacementBackedParentProofRequest(
		const FLayoutRegionSolveRequest& RootRequest,
		const FLayoutRegionSolveResult& RootPreparedTopology,
		const TArray<FAutomaticChildPlacement>& CandidatePlacements,
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FCommittedVerticalAccessOwnership& VerticalAccessOwnership,
		FLayoutRegionSolveRequest& OutParentRequest,
		FString& OutFailureReason)
	{
		const TArray<FLayoutPlannedCell>& ParentPlannedCells =
			RootPreparedTopology.SolveResult.PlannedCells;
		const FIntPoint ParentFootprintSize =
			RootPreparedTopology.SolveResult.FootprintSize;
		if (ParentFootprintSize.X <= 0 || ParentFootprintSize.Y <= 0 || ParentPlannedCells.IsEmpty())
		{
			OutFailureReason = RootPreparedTopology.SolveResult.FailureReason.IsEmpty()
				? FString::Printf(
					TEXT("Placement-backed proof request for '%s' could not derive valid prepared parent topology."),
					*RootRequest.RegionDebugPath)
				: RootPreparedTopology.SolveResult.FailureReason;
			return false;
		}

		TSet<FIntVector> ParentPlannedCellSet;
		TMap<FIntVector, ELayoutCellIntent> ParentPlannedCellIntents;
		TSet<FIntVector> ParentProtectedTraversalCells;
		for (const FLayoutPlannedCell& PlannedCell : ParentPlannedCells)
		{
			ParentPlannedCellSet.Add(PlannedCell.Cell);
			ParentPlannedCellIntents.Add(PlannedCell.Cell, PlannedCell.Intent);
		}
		CollectProtectedParentTraversalCells(
			ParentPlannedCells,
			ParentPlannedCellIntents,
			ParentProtectedTraversalCells);
		AppendProtectedParentRouteReservationCells(
			RootPreparedTopology.SolveResult,
			ParentProtectedTraversalCells);

		const TSet<ELayoutFaceDirection> ParentVerticalAccessIngressDirections =
			CollectVerticalAccessIngressDirections(RootRequest.ModuleCatalog);
		if (!BuildParentValidationRequest(
			RootRequest,
			ParentPlannedCells,
			ParentPlannedCellSet,
			ParentPlannedCellIntents,
			ParentFootprintSize,
			ParentProtectedTraversalCells,
			ParentVerticalAccessIngressDirections,
			CandidatePlacements,
			PlannedPartitionSeams,
			false,
			VerticalAccessOwnership,
			OutFailureReason,
			OutParentRequest))
		{
			return false;
		}

		// Prepared topology owns settled removed-cell authority; residual proof must
		// replay it instead of selecting the authored reservation again.
		OutParentRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.Reset();
		for (const FLayoutCellReservationRecord& Reservation :
			RootPreparedTopology.SolveResult.CompiledReservations)
		{
			if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
			{
				OutParentRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.Add(
					Reservation);
			}
		}
		return true;
	}
}
