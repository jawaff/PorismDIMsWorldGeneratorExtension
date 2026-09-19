// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "LayoutZoneFeatureDemand.h"

namespace LayoutRegionScheduleSolverFacade
{
	struct FNegotiatedProofScheduleContract;
}

/**
 * Shared private solver internals used while `LayoutProfileSolver.cpp` is being
 * decomposed into focused implementation units.
 */
namespace LayoutProfileSolverInternal
{
	/** Traversal graph node key used by route and reachability planning. */
	struct FTraversalNodeKey
	{
		FIntVector Cell = FIntVector::ZeroValue;
		FGameplayTag TraversalChannel;

		friend bool operator==(const FTraversalNodeKey& Left, const FTraversalNodeKey& Right)
		{
			return Left.Cell == Right.Cell && Left.TraversalChannel == Right.TraversalChannel;
		}

		friend uint32 GetTypeHash(const FTraversalNodeKey& Key)
		{
			return HashCombine(GetTypeHash(Key.Cell), GetTypeHash(Key.TraversalChannel));
		}
	};

	using FWalkableNodeKey = FTraversalNodeKey;

	/** Candidate module selection stored in one unsolved domain. */
	struct FSolveCandidate
	{
		int32 YawRotationSteps = 0;
		int32 VariantIndex = INDEX_NONE;
		int32 ModuleSnapshotIndex = INDEX_NONE;
		FLayoutId ModuleSnapshotId;
		bool bEmpty = true;
	};

	/** Canonical per-face neighbor classification compiled once from the live solve context. */
	enum class ESolveCellFaceNeighborKind : uint8
	{
		OuterBoundary,
		IncomingBoundary,
		SupportBoundary,
		FixedFilledNeighbor,
		FixedEmptyNeighbor,
		/** Frozen terrain fills this otherwise-unplanned horizontal neighboring volume. */
		TerrainBackedFilledNeighbor,
		/** Intentional no-write terrain inside owning-region topology, currently unoccupied. */
		TerrainResidualNeighbor,
		ReservedExternalCell,
		/** Planned cell stacked across Z without a selected traversal interface. It supplies structural occupancy only. */
		StructuralVerticalOverlap,
		InternalPlannedNeighbor,
		/** Project-specific rebuilt continuation face that still points at an unresolved planned cell outside the current level context. */
		ExternalPlannedNeighbor,
		/** Child-intrinsic preparation face whose exact parent, sibling, seam, or open-space contract is selected after transform mapping. */
		DeferredExternalContract,

		/** Parent-cell face touching a child region reservation cell during parent solve.
		 *  The parent CSP must verify that at least one child boundary module is
		 *  face-rule compatible before accepting this parent placement candidate. */
		ChildRegionContact
	};

	/** Immutable face-level continuation/interface facts used by both domain admission and live legality. */
	struct FSolveCellFaceInterface
	{
		ELayoutFaceDirection Direction = ELayoutFaceDirection::PosX;
		ESolveCellFaceNeighborKind NeighborKind =
			ESolveCellFaceNeighborKind::OuterBoundary;
		int32 CommittedEndpointAnchorIndex = INDEX_NONE;
		int32 IncomingBoundaryPointIndex = INDEX_NONE;
	};

	/** One compiled face-interface row for every planned cell in the live solve context. */
	struct FSolveCellFaceInterfaceSet
	{
		FSolveCellFaceInterface Faces[6];
	};

	/** Returns whether the candidate represents a real module variant instead of the explicit empty candidate. */
	inline bool IsOccupiedCandidate(const FSolveCandidate& Candidate)
	{
		return !Candidate.bEmpty && Candidate.VariantIndex != INDEX_NONE;
	}

	/** Ring-buffer trace event stored while solver tracing is enabled. */
	struct FSolverTraceEvent
	{
		FString Message;
	};

	/** Immutable request-local metadata for one finalized physical cell. */
	struct FLayoutFinalizedCellViewRecord
	{
		/** Index into FSolveContext::Result.PlannedCells; no duplicate planned-cell payload is stored. */
		int32 PlannedCellIndex = INDEX_NONE;
		/** Authored source coordinate for a real cell. Generated bridge/joining cells have no authored source. */
		FIntVector SourceAuthoredCell = FIntVector::ZeroValue;
		/** Frozen terrain-stage index for this physical cell's XY column. */
		int32 TerrainStageIndex = INDEX_NONE;
		bool bHasAuthoredSource = false;
		bool bHasTerrainStage = false;
	};

	/** Mutable single-region solve context shared across the leaf and structural solver passes. */
	struct FSolveContext
	{
		const ULayoutProfileAsset* Profile = nullptr;
		FLayoutId EffectiveSnapshotId;
		/** Stable direct region identity used by deterministic provider commitments. */
		FString RegionDebugPath;
		FLayoutProfileSolveSnapshot ProfileSnapshot;
		/** Frozen request scope controls root-only authored exterior Entry auditing. */
		ELayoutContractRegionScope RegionScope = ELayoutContractRegionScope::Root;
		int32 Seed = 0;
		FIntPoint FootprintSize = FIntPoint::ZeroValue;
		TMap<FIntVector, ELayoutCellIntent> PlannedCellIntents;
		/** Frozen terrain authority consulted while building the request-local finalized-cell view. */
		const FLayoutFrozenTerrainContract* FrozenTerrainContract = nullptr;
		/** True when frozen mode state selected shared stepped terrain topology. */
		bool bUsesSteppedTerrainContract = false;
		/** Canonical read-only index over finalized planned cells and frozen terrain-stage records. */
		TMap<FIntVector, FLayoutFinalizedCellViewRecord> FinalizedCellsByPhysicalCell;
		/** Full owning-region topology retained when a sparse local solve filters its active structural work set. */
		TMap<FIntVector, FLayoutPlannedCell> OwningTopologyCellsByPhysicalCell;
		/** Frozen removed-cell reservation authority, indexed without copying the reservation payload. */
		TMap<FIntVector, int32> ReservedOpenReservationIndexByCell;
		/** Frozen removed physical-cell index consumed by Batch 9 checks. */
		TSet<FIntVector> RemovedFinalizedCells;
		/** Final same-Z surface zones used for module admission after terrain transformation. */
		TMap<FIntVector, ELayoutPlacementZone> PlacementZonesByCell;
		/** Intentional terrain-supported cells that prefer empty structural candidates until explicitly claimed. */
		TMap<FIntVector, FLayoutId> TerrainResidualRuleIdByCell;
		/** Original preservation ownership survives local claims and successive corridor crops. */
		TMap<FIntVector, FLayoutId> OwningTerrainResidualRuleIdByCell;
		/** Sparse post-pass mode treats every still-unoccupied terrain residual as settled empty face authority. */
		bool bTreatTerrainResidualsAsSettledEmpty = false;
		/** Adapter-authored horizontal retaining seam faces keyed by final spatial cell. */
		TMap<FIntVector, uint8> TerrainSeamFaceMasksByCell;
		/** Frozen horizontal faces whose unplanned neighbor volume remains filled terrain. */
		TMap<FIntVector, uint8> TerrainBackedFilledFaceMasksByCell;
		/** Provenance-backed records retained for candidate diagnostics. */
		TArray<FLayoutTerrainBackedNeighborFaceRecord> TerrainBackedNeighborFaces;
		/** Frozen VerticalAccess endpoints excluded from terrain-seam Entry promotion. */
		TSet<FIntVector> VerticalAccessReservedCells;
		/** Canonical per-face continuation/interface carrier shared by static admission and live legality checks. */
		TMap<FIntVector, FSolveCellFaceInterfaceSet> CompiledFaceInterfaces;
		/** Per-cell admission mask compiled from the face carrier so seed widening does not drift across admission helpers. */
		TMap<FIntVector, uint8> CompiledSeedIntentMasks;
		/** Module-policy level per spatial cell; terrain shifts preserve authored values. */
		TMap<FIntVector, int32> ModuleLevelByCell;
		/** Boundary cells produced by stepped bridge or spatial-shift topology and eligible for shared route policy. */
		TSet<FIntVector> SteppedTraversalDeckCells;
		/** Frozen owning-region tops retained across child reservation and local plan rebuilds. */
		TMap<FIntPoint, int32> OwningTopModuleLevelByXY;
		/** Highest module-policy level in each column, honoring owning-region authority when supplied. */
		TMap<FIntPoint, int32> TopPlannedLevelByXY;
		TArray<FIntVector> SolveOrder;
		struct FSolvePlacement
		{
			int32 YawRotationSteps = 0;
			int32 VariantIndex = INDEX_NONE;
			int32 ModuleSnapshotIndex = INDEX_NONE;
			FLayoutId ModuleSnapshotId;
			FIntVector BundleRootCell = FIntVector::ZeroValue;
			FIntVector LocalBundleCell = FIntVector::ZeroValue;
			bool bBundleRoot = true;
			bool bEmpty = true;
		};

		/** Returns whether the placement represents a real module variant instead of an explicit empty cell. */
		static bool IsOccupiedPlacement(const FSolvePlacement& Placement)
		{
			return !Placement.bEmpty && Placement.VariantIndex != INDEX_NONE;
		}

		TMap<FIntVector, FSolvePlacement> Placements;
		/** Largest legal occupied placement frontier seen during this solve for partial artifact recovery. */
		TMap<FIntVector, FSolvePlacement> BestPartialPlacements;
		int32 BestPartialOccupiedPlacementCount = 0;
		TMap<FIntVector, FSolvePlacement> FixedNeighborPlacements;
		TArray<FLayoutSolveBoundaryPoint> IncomingBoundaryPoints;
		TArray<FLayoutCommittedEndpointAnchor> CommittedEndpointAnchors;
		/** Immutable accepted child payloads retained across local corridor trials; never re-solved by cropping. */
		TArray<TSharedPtr<const FLayoutRegionSolveResult>> SparseStructuralChildProofs;
		/** Imported fixed-neighbor snapshots can validate faces but cannot supply this region's candidates. */
		TSet<FLayoutId> FixedOnlyModuleSnapshotIds;
		TArray<FLayoutCommittedTraversalAnchor> CommittedTraversalAnchors;
		TArray<FLayoutForcedPlacementBundleInsertion> ForcedPlacementBundleInsertions;
		TArray<FLayoutRouteConstraintRecord> RequiredRouteConstraints;
		/** True when a sparse structural coordinator supplied the complete exact-port route assignment. */
		bool bHasCompleteSparseStructuralRouteAssignment = false;
		/** Exact certified module/yaw domains keyed by planned cell. */
		TMap<FIntVector, FLayoutCellCandidateDomainRestriction> CandidateDomainRestrictionsByCell;
		/** Stable certificate shared by request-carried candidate-domain restrictions. */
		FLayoutId CandidateDomainCertificateId;
		/** Face-local external planned-neighbor carriers used by rebuilt continuation levels. */
		TMap<FIntVector, uint8> ExternalPlannedNeighborFaceMasks;
		/** Exact rebuilt faces whose opposite side is still occupied by another unresolved level. */
		TMap<FIntVector, uint8> ExternalReservedNeighborFaceMasks;
		/** True only while compiling child-intrinsic topology before transform-specific external contracts are certified. */
		bool bDeferUnresolvedExternalFaceValidation = false;
		/** All child-region reservation cells (parent-local coords) used during parent
		 *  CSP face compilation to identify ChildRegionContact faces. */
		TSet<FIntVector> ChildReservationCells;

		/** Per-cell lookup from a child reservation cell to its content set snapshot.
		 *  Populated before parent solve from NegotiatedChildResponsibilityContracts. */
		TMap<FIntVector, const FLayoutRegionContentSetSolveSnapshot*> ChildContentSetsByCell;

		/** Exact child module catalog selected for each reserved child cell. */
		TMap<FIntVector, const FLayoutModuleCatalog*> ChildModuleCatalogsByCell;

		/** Parent-translated child planned-cell metadata used to enforce child intent, zone, and authored level. */
		TMap<FIntVector, FLayoutPlannedCell> ChildPlannedCellsByCell;

		/** Highest authored module level for the selected child region at each reserved child cell. */
		TMap<FIntVector, int32> ChildTopModuleLevelByCell;

		TMap<int32, TSet<FGameplayTag>> ActiveTraversalChannelsByLevel;
		TMap<FIntVector, int32> RouteConstraintIndexByCell;
		TMap<FIntVector, uint8> ReservationFlagsByCell;
		TSet<FIntVector> ReservedOpenCells;
		/** True when reserved-open prewarm cannot retain a viable finalized topology. */
		bool bReservedOpenPrewarmInfeasible = false;
		int32 CandidateAttemptCount = 0;
		int32 MaxCandidateAttempts = 50000;
		double SolveStartTimeSeconds = 0.0;
		double MaxSolveDurationSeconds = 0.0;
		bool bTimeBudgetExceeded = false;
		bool bUsesChildSolveContext = false;
		int32 MaxFailureDetails = 24;
		ELayoutSolverTraceMode TraceMode = ELayoutSolverTraceMode::Disabled;
		int32 MaxTraceEvents = 0;
		bool bIncludeTraceCandidateDetails = false;
		bool bDeferFinalReachabilityAudit = false;
		/** Sparse route assignment postpones reachability pruning until every constrained corridor cell is placed. */
		bool bDeferSparseStructuralReachabilityUntilAssignmentComplete = false;
		struct FOrientedModuleVariant
		{
			int32 ModuleSnapshotIndex = INDEX_NONE;
			FLayoutId ModuleSnapshotId;
			FName ModuleDebugName;
			FName SourceContentEntryId;
			/** Exact feature tags copied from the immutable owning content-entry snapshot. */
			FGameplayTagContainer ProvidedZoneFeatures;
			ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;
			ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
			int32 SpecificLevel = 0;
			bool bOptional = false;
			int32 YawRotationSteps = 0;
			FLayoutModuleFaceRules WorldFaceRules;
			TArray<FLayoutDerivedSpanOffer> WorldSpanOffers;
			TArray<FLayoutClosureProviderIntent> ClosureProviderIntents;
			FGameplayTagContainer TraversalChannels;
			TArray<FLayoutInternalAccessLink> InternalAccessLinks;
			TArray<FLayoutDerivedInternalTraversalLink> DerivedInternalTraversalLinks;
			TArray<FLayoutDerivedVerticalAccessContract> VerticalAccessContracts;
			TArray<ELayoutModuleRole> Roles;
			TArray<ELayoutCellIntent> SupportedCellIntents;
			int32 MinTraversableNeighborFaces = 0;
			int32 Weight = 1;
			FString Signature;
		};
		TArray<FOrientedModuleVariant> Variants;
		TMap<ELayoutCellIntent, TArray<int32>> VariantIndicesByIntent;
		TArray<FLayoutModuleSolveSnapshot> ModuleSnapshots;
		/** Exact child provider commitments frozen before residual parent module search. */
		TArray<FLayoutZoneFeatureProviderCommitment> PrecommittedZoneFeatureProviderCommitments;
		/** External direct-child capacity keyed by hard requirement id during pre-placement parent planning. */
		TMap<FLayoutId, int32> HardZoneFeatureExternalProviderCapacityByRequirementId;
		/** Hard requirements compiled once from immutable profile state. */
		TArray<LayoutZoneFeatureDemand::FHardDemand> HardZoneFeatureDemands;
		/** Branch-local committed bundle-root counts aligned with HardZoneFeatureDemands. */
		TArray<int32> HardZoneFeatureCommittedCounts;
		/** Stable potential provider roots aligned with HardZoneFeatureDemands. */
		TArray<TArray<FIntVector>> HardZoneFeatureProviderRootsByDemand;
		bool bHardZoneFeatureDemandsCompiled = false;
		TMap<FIntVector, TArray<FSolveCandidate>> InitialDomains;
		/** Bounded reasons explaining why initial-domain variants were rejected for a planned cell. */
		TMap<FIntVector, TArray<FString>> InitialDomainAdmissionFailuresByCell;
		/** Identifies a prepared empty domain without parsing user-facing diagnostics during host retries. */
		bool bHasPreparedInitialDomainFailure = false;
		FIntVector PreparedInitialDomainFailureCell = FIntVector::ZeroValue;
		TArray<FLayoutIndexedDomainCandidate> IndexedCandidates;
		TArray<FLayoutIndexedCandidateCompatibility> IndexedCompatibilityRows;
		TMap<uint64, int32> IndexedCompatibilityRowByKey;
		TMap<FIntVector, TArray<int32>> IndexedOrderedDomainCandidatesByCell;
		TMap<FIntVector, TArray<uint64>> IndexedDomainBitsByCell;
		/** Hard closure segments compiled once from finalized request topology before indexed search. */
		TArray<FLayoutClosureCoverageSegmentRecord> CompiledHardClosureSegments;
		/** Affected-segment lookup by physical occupied cell. */
		TMap<FIntVector, TArray<int32>> HardClosureSegmentIndicesByPhysicalCell;
		/** Candidate-root providers indexed per hard closure segment for branch-local feasibility. */
		TArray<TArray<FIntVector>> HardClosureProviderRootsBySegment;
		/** Affected-segment lookup by candidate root whose live domain can provide coverage. */
		TMap<FIntVector, TArray<int32>> HardClosureSegmentIndicesByProviderRoot;
		int32 IndexedCandidateWordCount = 0;
		int32 EmptyIndexedCandidateIndex = INDEX_NONE;
		bool bUseIndexedCandidateFiltering = true;
		TSet<uint64> FailedStateHashes;
		int32 FailedStateMemoHits = 0;
		int32 TraceEventsDropped = 0;
		int32 TraceNextWriteIndex = 0;
		bool bTraceWrapped = false;
		bool bForceReachabilityBranchFaceScore = false;
		/** Request-local exact propagation mode used by staged validation solves to spend work earlier instead of deferring domain collapse. */
		bool bForceExpensiveForwardChecks = false;
		TArray<FSolverTraceEvent> TraceEvents;
		FLayoutSolveResult Result;
		/** Terrain cell contracts from the frozen terrain contract, used for entry walkability filtering. */
		TArray<FLayoutTerrainCellContractRecord> TerrainCellContracts;
		/** Pre-qualified entry cells from prewarm. When non-empty, solver uses these instead of computing its own. */
		TArray<FIntVector> QualifiedEntryCells;
	};

	/** Resolves preserved authored level from canonical finalized metadata when available. */
	inline int32 GetFinalizedCellModuleLevel(const FSolveContext& Context, const FIntVector& Cell)
	{
		if (const FLayoutPlannedCell* OwningCell = Context.OwningTopologyCellsByPhysicalCell.Find(Cell))
		{
			return OwningCell->ModuleLevelIndex != INDEX_NONE
				? OwningCell->ModuleLevelIndex
				: OwningCell->Cell.Z;
		}
		if (const FLayoutFinalizedCellViewRecord* ViewRecord = Context.FinalizedCellsByPhysicalCell.Find(Cell))
		{
			if (Context.Result.PlannedCells.IsValidIndex(ViewRecord->PlannedCellIndex))
			{
				const FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells[ViewRecord->PlannedCellIndex];
				return ViewRecord->bHasAuthoredSource
					? ViewRecord->SourceAuthoredCell.Z
					: (PlannedCell.ModuleLevelIndex != INDEX_NONE
						? PlannedCell.ModuleLevelIndex
						: PlannedCell.Cell.Z);
			}
		}
		return Context.ModuleLevelByCell.FindRef(Cell);
	}

	/** Resolves final terrain-seam metadata from canonical finalized cells when available. */
	inline uint8 GetFinalizedTerrainSeamFaceMask(const FSolveContext& Context, const FIntVector& Cell)
	{
		if (const FLayoutPlannedCell* OwningCell = Context.OwningTopologyCellsByPhysicalCell.Find(Cell))
		{
			return OwningCell->TerrainSeamFaceMask;
		}
		if (const FLayoutFinalizedCellViewRecord* ViewRecord = Context.FinalizedCellsByPhysicalCell.Find(Cell))
		{
			if (Context.Result.PlannedCells.IsValidIndex(ViewRecord->PlannedCellIndex))
			{
				return Context.Result.PlannedCells[ViewRecord->PlannedCellIndex].TerrainSeamFaceMask;
			}
		}
		return Context.TerrainSeamFaceMasksByCell.FindRef(Cell);
	}

	/** Matches a module placement zone against final local topology or ordinary footprint topology. */
	inline bool DoesCellMatchResolvedPlacementZone(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutPlacementZone CandidatePlacementZone)
	{
		const FLayoutFinalizedCellViewRecord* ViewRecord = Context.FinalizedCellsByPhysicalCell.Find(Cell);
		const FLayoutPlannedCell* FinalizedCell = Context.OwningTopologyCellsByPhysicalCell.Find(Cell);
		if (FinalizedCell == nullptr)
		{
			FinalizedCell = ViewRecord != nullptr
				&& Context.Result.PlannedCells.IsValidIndex(ViewRecord->PlannedCellIndex)
				? &Context.Result.PlannedCells[ViewRecord->PlannedCellIndex]
				: nullptr;
		}
		const int32 ModuleLevel = GetFinalizedCellModuleLevel(Context, Cell);
		const bool bCore = ModuleLevel == 0
			&& FMath::Abs(static_cast<float>(Cell.X) - static_cast<float>(Context.FootprintSize.X - 1) * 0.5f) <= 0.5f
			&& FMath::Abs(static_cast<float>(Cell.Y) - static_cast<float>(Context.FootprintSize.Y - 1) * 0.5f) <= 0.5f;
		const ELayoutPlacementZone* LocalPlacementZone = FinalizedCell != nullptr
			? &FinalizedCell->PlacementZone
			: Context.PlacementZonesByCell.Find(Cell);
		if (LocalPlacementZone == nullptr)
		{
			const bool bBoundary = Cell.X == 0
				|| Cell.Y == 0
				|| Cell.X == Context.FootprintSize.X - 1
				|| Cell.Y == Context.FootprintSize.Y - 1;
			const bool bCorner = Context.FootprintSize.X > 1
				&& Context.FootprintSize.Y > 1
				&& (Cell.X == 0 || Cell.X == Context.FootprintSize.X - 1)
				&& (Cell.Y == 0 || Cell.Y == Context.FootprintSize.Y - 1);
			switch (CandidatePlacementZone)
			{
			case ELayoutPlacementZone::Any: return true;
			case ELayoutPlacementZone::Perimeter: return bBoundary;
			case ELayoutPlacementZone::Edge: return bBoundary && !bCorner;
			case ELayoutPlacementZone::Corner: return bCorner;
			case ELayoutPlacementZone::Interior: return !bBoundary;
			case ELayoutPlacementZone::Core: return bCore;
			default: return false;
			}
		}

		// Final topology classifies center cells as Interior; preserve authored Core admission after that map exists.
		if (CandidatePlacementZone == ELayoutPlacementZone::Core)
		{
			return bCore && *LocalPlacementZone == ELayoutPlacementZone::Interior;
		}

		switch (CandidatePlacementZone)
		{
		case ELayoutPlacementZone::Any:
			return true;
		case ELayoutPlacementZone::Perimeter:
			return *LocalPlacementZone == ELayoutPlacementZone::Edge
				|| *LocalPlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Edge:
			return *LocalPlacementZone == ELayoutPlacementZone::Edge;
		case ELayoutPlacementZone::Corner:
			return *LocalPlacementZone == ELayoutPlacementZone::Corner;
		case ELayoutPlacementZone::Interior:
			return *LocalPlacementZone == ELayoutPlacementZone::Interior;
		case ELayoutPlacementZone::Core:
			return *LocalPlacementZone == ELayoutPlacementZone::Core;
		default:
			return false;
		}
	}

	/** Ordinary seams retain owning-region zones. A certified owner-side junction may use its exact Corner provider domain. */
	inline bool DoesModuleMatchResolvedPlacementZone(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const ELayoutPlacementZone CandidatePlacementZone)
	{
		if (DoesCellMatchResolvedPlacementZone(Context, Cell, CandidatePlacementZone)) return true;
		const auto* Restriction = Context.CandidateDomainRestrictionsByCell.Find(Cell);
		return CandidatePlacementZone == ELayoutPlacementZone::Corner
			&& Restriction != nullptr && Restriction->bTreatAsJunctionPlacementZone
			&& !Context.CandidateDomainCertificateId.IsNone() && !Restriction->RestrictionId.IsNone()
			&& !Restriction->AllowedCandidates.IsEmpty();
	}

	/** Only optional deck offers may be omitted; callers supply active intent and exact domain/route occupancy authority. */
	inline bool CanTopBridgeOfferRemainEmpty(const FLayoutPlannedCell& Cell,
		const ELayoutCellIntent ActiveIntent, const bool bHasOccupancyAuthority)
	{
		return Cell.bIsBridgeCell && Cell.bIsTopBridgeOffer && Cell.VerticalAccessLandingContactMask == 0
			&& ActiveIntent != ELayoutCellIntent::Entry && ActiveIntent != ELayoutCellIntent::Connector
			&& ActiveIntent != ELayoutCellIntent::VerticalAccess && !bHasOccupancyAuthority;
	}

	/** Builds flat-profile intent plan and, when requested, durable selected reserved-open records for terrain realization. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildAuthoredPlan(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FLayoutModuleCatalog& ModuleCatalog,
		int32 Seed,
		const FIntPoint& FootprintSize,
		TArray<FLayoutPlannedCell>& OutPlannedCells,
		FString& OutFailureReason,
		TArray<FLayoutCellReservationRecord>* OutCompiledReservations = nullptr);

	/** Tests one face-local continuation placeholder mask without reopening old whole-cell continuation categories. */
	bool DoesFaceMaskContainDirection(
		uint8 FaceMask,
		ELayoutFaceDirection Direction);

	/** Returns whether the rebuilt continuation surface still carries an unresolved external planned-neighbor placeholder on this exact face. */
	bool CellHasExternalPlannedNeighborFace(
		const FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutFaceDirection Direction);

	/** Returns whether the exact face still points at any unresolved planned neighbor, either inside the current context or across an external continuation boundary. */
	bool CellFaceHasAnyPlannedNeighborCarrier(
		const FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutFaceDirection Direction);

	/** Returns whether the rebuilt continuation surface still carries an unresolved reserved external occupant on this exact face. */
	bool CellHasExternalReservedNeighborFace(
		const FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutFaceDirection Direction);

	/** Resolves the authoritative filled/empty neighbor carrier for one face by checking live placements first, then the rebuilt fixed-neighbor carry-through map. */
	const FSolveContext::FSolvePlacement* FindPlacementOrFixedNeighbor(
		const FSolveContext& Context,
		const FIntVector& Cell);

	/** Returns the compiled per-face carrier row for one planned cell when later helper stages need the same settled face classification as admission and legality. */
	inline const FSolveCellFaceInterface* FindCompiledCellFaceInterface(
		const FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutFaceDirection Direction)
	{
		const FSolveCellFaceInterfaceSet* InterfaceSet =
			Context.CompiledFaceInterfaces.Find(Cell);
		if (InterfaceSet == nullptr)
		{
			return nullptr;
		}

		const int32 DirectionIndex = static_cast<int32>(Direction);
		return DirectionIndex >= 0
			&& DirectionIndex < UE_ARRAY_COUNT(InterfaceSet->Faces)
			? &InterfaceSet->Faces[DirectionIndex]
			: nullptr;
	}

	/** Returns the incoming/support boundary carrier that occupies the opposite side of the requested face, if any. */
	const FLayoutSolveBoundaryPoint* FindIncomingBoundaryPointForCellFace(
		const FSolveContext& Context,
		const FIntVector& Cell,
		ELayoutFaceDirection Direction);

	/** Resolves the exact incoming/support boundary fact chosen by one compiled face row. */
	inline const FLayoutSolveBoundaryPoint* ResolveCompiledIncomingBoundaryPoint(
		const FSolveContext& Context,
		const FSolveCellFaceInterface& FaceInterface)
	{
		return Context.IncomingBoundaryPoints.IsValidIndex(
				FaceInterface.IncomingBoundaryPointIndex)
			? &Context.IncomingBoundaryPoints[
				FaceInterface.IncomingBoundaryPointIndex]
			: nullptr;
	}

	/** Immutable request-backed summary of the deterministic pre-branch frontier after forced placements settle. */
	struct FPreparedSearchPrefixStageSummary
	{
		int32 PlacedCellCount = 0;
		int32 ForcedPlacementCellCount = 0;
		bool bSolvedAfterForcedPlacements = false;
		bool bNoCellSelectable = false;
		int32 ConstrainedCellCount = 0;
		int32 EligibleCandidateCount = 0;
		int32 TightestRemainingSize = 0;
		int32 SingleRemainingCandidateCellCount = 0;
		int32 AtMostFourRemainingCandidateCellCount = 0;
		FIntVector SelectedCell = FIntVector::ZeroValue;
		ELayoutCellIntent SelectedIntent = ELayoutCellIntent::Interior;
		int32 SelectedDomainSize = 0;
		FLayoutId DominantFailureKind = NAME_None;
		int32 DominantFailureCount = 0;
	};

	/** Immutable request-backed carrier for the exact deterministic pre-branch frontier. */
	struct FPreparedSearchPrefixStageCarrier
	{
		TArray<FIntVector> ForcedCells;
		FIntVector SelectedCell = FIntVector::ZeroValue;
		TArray<FSolveCandidate> SelectedCandidates;
		bool bSolvedAfterForcedPlacements = false;
		FPreparedSearchPrefixStageSummary Summary;
	};

	/** Immutable one-candidate work unit on the selected post-prefix branch frontier. */
	struct FPreparedSearchBranchWorkItem
	{
		FIntVector Cell = FIntVector::ZeroValue;
		ELayoutCellIntent Intent = ELayoutCellIntent::Interior;
		FSolveCandidate Candidate;
	};

	/** Immutable request-backed carrier for the exact post-prefix per-candidate branch frontier. */
	struct FPreparedSearchBranchStageCarrier
	{
		FPreparedSearchPrefixStageCarrier SearchPrefixStage;
		TArray<FPreparedSearchBranchWorkItem> WorkItems;
	};

	/**
	 * Immutable request-backed carrier for one exact post-prefix candidate after
	 * immediate placement and forward-check feasibility have already succeeded.
	 *
	 * This later branch seam stays explicit so future threading work can hand
	 * off one already-propagated candidate continuation instead of reopening
	 * the full branch-frontier loop.
	 */
	struct FPreparedSearchBranchApplyStageCarrier
	{
		FPreparedSearchBranchStageCarrier SearchBranchStage;
		FPreparedSearchBranchWorkItem WorkItem;
		bool bOccupiedCandidate = false;
		TArray<FIntVector> PlacedCells;
		int32 NextSolveDepth = 1;
	};

	/** Off-by-default prepared-plan route-demand summary used for recursive deferred-validation diagnosis. */
	struct FPreparedPlanRouteDemandSummary
	{
		int32 RouteConstraintCount = 0;
		int32 MainRouteConstraintCount = 0;
		int32 BoundaryRouteConstraintCount = 0;
		int32 InteriorRouteConstraintCount = 0;
		int32 BoundaryMultiFaceConstraintCount = 0;
		int32 RouteFaceRequirementCount = 0;
		int32 RequiredRouteReservationCount = 0;
		int32 ReachabilityBranchReservationCount = 0;
		int32 LiveDomainConstrainedCellCount = 0;
		int32 LiveDomainBoundaryConstrainedCellCount = 0;
		int32 LiveDomainMultiFaceBoundaryConstrainedCellCount = 0;
		int32 LiveDomainEligibleCandidateCount = 0;
		int32 LiveDomainEliminatedCandidateCount = 0;
		int32 LiveDomainTightestRemainingSize = 0;
		int32 LiveDomainSingleRemainingCandidateCellCount = 0;
		int32 LiveDomainAtMostFourRemainingCandidateCellCount = 0;
		bool bLiveDomainFailedConstraint = false;
		FIntVector LiveDomainFailedConstraintCell = FIntVector::ZeroValue;
		int32 LiveDomainFailedConstraintEligibleCandidateCount = 0;
		int32 LiveDomainFailedConstraintRequiredFaceCount = 0;
		ELayoutRouteDomainFailureKind LiveDomainFailedConstraintDominantFailureKind =
			ELayoutRouteDomainFailureKind::None;
		int32 LiveDomainFailedConstraintDominantFailureCount = 0;
		bool bRepairModeRouteDomainAttempted = false;
		bool bRepairModeRouteDomainSucceeded = false;
		bool bRepairModeLiveDomainFailedConstraint = false;
		FIntVector RepairModeLiveDomainFailedConstraintCell =
			FIntVector::ZeroValue;
		int32 RepairModeLiveDomainFailedConstraintEligibleCandidateCount = 0;
		int32 RepairModeLiveDomainFailedConstraintRequiredFaceCount = 0;
		ELayoutRouteDomainFailureKind
			RepairModeLiveDomainFailedConstraintDominantFailureKind =
				ELayoutRouteDomainFailureKind::None;
		int32 RepairModeLiveDomainFailedConstraintDominantFailureCount = 0;
		int32 SearchPrefixPlacedCellCount = 0;
		int32 SearchPrefixForcedPlacementCellCount = 0;
		bool bSearchPrefixSolvedAfterForcedPlacements = false;
		bool bSearchPrefixNoCellSelectable = false;
		int32 SearchPrefixConstrainedCellCount = 0;
		int32 SearchPrefixEligibleCandidateCount = 0;
		int32 SearchPrefixTightestRemainingSize = 0;
		int32 SearchPrefixSingleRemainingCandidateCellCount = 0;
		int32 SearchPrefixAtMostFourRemainingCandidateCellCount = 0;
		FIntVector SearchPrefixSelectedCell = FIntVector::ZeroValue;
		ELayoutCellIntent SearchPrefixSelectedIntent =
			ELayoutCellIntent::Interior;
		int32 SearchPrefixSelectedDomainSize = 0;
		FLayoutId SearchPrefixDominantFailureKind = NAME_None;
		int32 SearchPrefixDominantFailureCount = 0;
		bool bRepairModeSearchPrefixAttempted = false;
		bool bRepairModeSearchPrefixSucceeded = false;
		int32 RepairModeSearchPrefixPlacedCellCount = 0;
		int32 RepairModeSearchPrefixForcedPlacementCellCount = 0;
		bool bRepairModeSearchPrefixSolvedAfterForcedPlacements = false;
		bool bRepairModeSearchPrefixNoCellSelectable = false;
		int32 RepairModeSearchPrefixConstrainedCellCount = 0;
		int32 RepairModeSearchPrefixEligibleCandidateCount = 0;
		int32 RepairModeSearchPrefixTightestRemainingSize = 0;
		int32 RepairModeSearchPrefixSingleRemainingCandidateCellCount = 0;
		int32 RepairModeSearchPrefixAtMostFourRemainingCandidateCellCount = 0;
		FIntVector RepairModeSearchPrefixSelectedCell = FIntVector::ZeroValue;
		ELayoutCellIntent RepairModeSearchPrefixSelectedIntent =
			ELayoutCellIntent::Interior;
		int32 RepairModeSearchPrefixSelectedDomainSize = 0;
		FLayoutId RepairModeSearchPrefixDominantFailureKind = NAME_None;
		int32 RepairModeSearchPrefixDominantFailureCount = 0;
		FString FailureFirstLine;
	};

	/** Returns the frozen oriented-variant row backing one candidate when the candidate was lowered from a snapshot. */
	inline const FSolveContext::FOrientedModuleVariant* FindSolveCandidateVariant(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		return Context.Variants.IsValidIndex(Candidate.VariantIndex)
			? &Context.Variants[Candidate.VariantIndex]
			: nullptr;
	}

	/** Returns the frozen oriented-variant row backing one solved placement when the placement was lowered from a snapshot. */
	inline const FSolveContext::FOrientedModuleVariant* FindSolvePlacementVariant(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		return Context.Variants.IsValidIndex(Placement.VariantIndex)
			? &Context.Variants[Placement.VariantIndex]
			: nullptr;
	}

	/** Returns one oriented world-face rule directly from a frozen variant row. */
	inline bool TryGetSolveVariantFaceRule(
		const FSolveContext& Context,
		const int32 VariantIndex,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		if (!Context.Variants.IsValidIndex(VariantIndex))
		{
			return false;
		}

		const FLayoutFaceRule* Rule = Context.Variants[VariantIndex].WorldFaceRules.FindRule(Direction);
		if (Rule == nullptr)
		{
			return false;
		}

		OutFaceRule = *Rule;
		return true;
	}

	/** Returns one authored face rule rotated into world space for legacy leaf-module callers. */
	inline bool TryGetSolveWorldFaceRule(
		const ULayoutModuleAsset* Module,
		const ELayoutFaceDirection WorldDirection,
		const int32 YawRotationSteps,
		FLayoutFaceRule& OutFaceRule)
	{
		if (Module == nullptr)
		{
			return false;
		}

		const ELayoutFaceDirection AuthoredDirection = FLayoutDirectionUtils::RotateYaw(WorldDirection, -YawRotationSteps);
		FLayoutFaceRule EffectiveRule;
		if (!Module->GetEffectiveFaceRule(AuthoredDirection, EffectiveRule))
		{
			return false;
		}

		OutFaceRule = EffectiveRule;
		OutFaceRule.Direction = WorldDirection;
		return true;
	}

	/** Returns one candidate face rule, preferring frozen variant data before legacy live-module fallback. */
	inline bool TryGetSolveCandidateFaceRule(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		if (Candidate.VariantIndex != INDEX_NONE)
		{
			return TryGetSolveVariantFaceRule(Context, Candidate.VariantIndex, Direction, OutFaceRule);
		}

		return false;
	}

	/** Returns one candidate face rule for a specific covered local bundle cell on snapshot-backed multi-cell variants. */
	inline bool TryGetSolveCandidateLocalFaceRule(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate,
		const FIntVector& LocalBundleCell,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		if (Candidate.VariantIndex != INDEX_NONE)
		{
			if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolveCandidateVariant(Context, Candidate))
			{
				if (Context.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex))
				{
					const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[Variant->ModuleSnapshotIndex];
					if (ModuleSnapshot.OccupiedLocalCells.Num() > 1)
					{
						const FLayoutLocalCellFaceRuleSnapshot* CellSnapshot = ModuleSnapshot.GeneratedLocalCellFaceRules.FindByPredicate(
							[&LocalBundleCell](const FLayoutLocalCellFaceRuleSnapshot& CandidateCellSnapshot)
							{
								return CandidateCellSnapshot.LocalCell == LocalBundleCell;
							});
						if (CellSnapshot != nullptr)
						{
							if (const FLayoutFaceRule* Rule = CellSnapshot->ExposedFaceRules.FindByPredicate(
								[&Candidate, Direction](const FLayoutFaceRule& CandidateRule)
								{
									return FLayoutDirectionUtils::RotateYaw(CandidateRule.Direction, Candidate.YawRotationSteps) == Direction;
								}))
							{
								OutFaceRule = *Rule;
								OutFaceRule.Direction = Direction;
								return true;
							}
						}
					}
				}
			}

			return TryGetSolveVariantFaceRule(Context, Candidate.VariantIndex, Direction, OutFaceRule);
		}

		return false;
	}

	/** Returns one placement face rule, preferring frozen variant data before legacy live-module fallback. */
	inline bool TryGetSolvePlacementFaceRule(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule)
	{
		if (Placement.VariantIndex != INDEX_NONE)
		{
			if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolvePlacementVariant(Context, Placement))
			{
				if (Context.ModuleSnapshots.IsValidIndex(Variant->ModuleSnapshotIndex))
				{
					const FLayoutModuleSolveSnapshot& ModuleSnapshot = Context.ModuleSnapshots[Variant->ModuleSnapshotIndex];
					if (ModuleSnapshot.OccupiedLocalCells.Num() > 1)
					{
						const FLayoutLocalCellFaceRuleSnapshot* CellSnapshot = ModuleSnapshot.GeneratedLocalCellFaceRules.FindByPredicate(
							[&Placement](const FLayoutLocalCellFaceRuleSnapshot& CandidateCellSnapshot)
							{
								return CandidateCellSnapshot.LocalCell == Placement.LocalBundleCell;
							});
						if (CellSnapshot != nullptr)
						{
							if (const FLayoutFaceRule* Rule = CellSnapshot->ExposedFaceRules.FindByPredicate(
								[&Placement, Direction](const FLayoutFaceRule& CandidateRule)
								{
									return FLayoutDirectionUtils::RotateYaw(CandidateRule.Direction, Placement.YawRotationSteps) == Direction;
								}))
							{
								OutFaceRule = *Rule;
								OutFaceRule.Direction = Direction;
								return true;
							}
						}
					}
				}
			}

			return TryGetSolveVariantFaceRule(Context, Placement.VariantIndex, Direction, OutFaceRule);
		}

		return false;
	}

	/** Returns the canonical debug name for a candidate, preferring the frozen variant row. */
	inline FLayoutId GetSolveCandidateDebugName(const FSolveContext& Context, const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolveCandidateVariant(Context, Candidate))
		{
			return Variant->ModuleDebugName;
		}

		return Candidate.ModuleSnapshotId;
	}

	/** Returns the canonical debug name for a placement, preferring the frozen variant row. */
	inline FLayoutId GetSolvePlacementDebugName(const FSolveContext& Context, const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolvePlacementVariant(Context, Placement))
		{
			return Variant->ModuleDebugName;
		}

		return Placement.ModuleSnapshotId;
	}

	/** Returns the traversal channels owned by a candidate's frozen variant contract. */
	inline const FGameplayTagContainer& GetSolveCandidateTraversalChannels(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolveCandidateVariant(Context, Candidate))
		{
			return Variant->TraversalChannels;
		}

		static const FGameplayTagContainer Empty;
		return Empty;
	}

	/** Returns the traversal channels owned by a placement's frozen variant contract. */
	inline const FGameplayTagContainer& GetSolvePlacementTraversalChannels(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolvePlacementVariant(Context, Placement))
		{
			return Variant->TraversalChannels;
		}

		static const FGameplayTagContainer Empty;
		return Empty;
	}

	/** Returns the walkable-area view of a candidate's traversal channels. */
	inline const FGameplayTagContainer& GetSolveCandidateWalkableAreas(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		return GetSolveCandidateTraversalChannels(Context, Candidate);
	}

	/** Returns the walkable-area view of a placement's traversal channels. */
	inline const FGameplayTagContainer& GetSolvePlacementWalkableAreas(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		return GetSolvePlacementTraversalChannels(Context, Placement);
	}

	/** Returns the internal traversal links owned by a candidate's frozen variant contract. */
	inline const TArray<FLayoutInternalAccessLink>& GetSolveCandidateInternalAccessLinks(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolveCandidateVariant(Context, Candidate))
		{
			return Variant->InternalAccessLinks;
		}

		static const TArray<FLayoutInternalAccessLink> Empty;
		return Empty;
	}

	/** Returns the internal traversal links owned by a placement's frozen variant contract. */
	inline const TArray<FLayoutInternalAccessLink>& GetSolvePlacementInternalAccessLinks(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolvePlacementVariant(Context, Placement))
		{
			return Variant->InternalAccessLinks;
		}

		static const TArray<FLayoutInternalAccessLink> Empty;
		return Empty;
	}

	/** Returns the derived local-cell traversal bridges owned by a candidate's frozen variant contract. */
	inline const TArray<FLayoutDerivedInternalTraversalLink>& GetSolveCandidateDerivedInternalTraversalLinks(
		const FSolveContext& Context,
		const FSolveCandidate& Candidate)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolveCandidateVariant(Context, Candidate))
		{
			return Variant->DerivedInternalTraversalLinks;
		}

		static const TArray<FLayoutDerivedInternalTraversalLink> Empty;
		return Empty;
	}

	/** Returns the derived local-cell traversal bridges owned by a placement's frozen variant contract. */
	inline const TArray<FLayoutDerivedInternalTraversalLink>& GetSolvePlacementDerivedInternalTraversalLinks(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement)
	{
		if (const FSolveContext::FOrientedModuleVariant* Variant = FindSolvePlacementVariant(Context, Placement))
		{
			return Variant->DerivedInternalTraversalLinks;
		}

		static const TArray<FLayoutDerivedInternalTraversalLink> Empty;
		return Empty;
	}

	/** Simplified child-provider summary used by tests to verify recursive host vertical-access ownership. */
	struct FRecursiveVerticalAccessProvider
	{
		bool bSupportsVerticalAccess = false;
		bool bContributesHostVerticalAccess = false;
		TArray<FIntVector> VerticalAccessLocalCells;
		TArray<FLayoutPlannedCell> PlannedCells;
		TArray<FLayoutCommittedEndpointAnchor> EndpointCommitments;
		FString ChildRegionDebugPath;
	};

	/** Test-only mirror of one shared parent-owned face used by negotiated candidate normalization. */
	struct FNegotiatedSharedParentChildFaceForTests
	{
		FIntVector ParentCell = FIntVector::ZeroValue;
		FIntVector ChildLocalCell = FIntVector::ZeroValue;
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
		FGameplayTag InterfaceFamily;
	};

	/** Test-only mirror of one successful negotiated child-placement candidate before normalization. */
	struct FNegotiatedCandidateNormalizationInputForTests
	{
		FLayoutId BoundaryCertificateId;
		TArray<FLayoutCommittedEndpointAnchor> Commitments;
		TArray<FLayoutCommittedTraversalAnchor> ParentCommittedTraversalAnchors;
		bool bAllowsChildTraversalBridgeForCommittedContacts = false;
		TArray<FIntVector> ParentPlanReservedCells;
		TArray<FNegotiatedSharedParentChildFaceForTests> SharedParentChildFaces;
		ELayoutNegotiatedHostVerticalAccessResponsibility NegotiatedHostVerticalAccessResponsibility =
			ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;
		bool bHasRequiredHostIngressAnchor = false;
		FLayoutCommittedEndpointAnchor RequiredHostIngressAnchor;
		bool bHasRequiredHostEgressAnchor = false;
		FLayoutCommittedEndpointAnchor RequiredHostEgressAnchor;
		TArray<FIntVector> RequiredChildInternalVerticalRouteCells;
		int32 Score = MIN_int32 / 4;
		uint32 TieBreakHash = 0;
	};

	/** Deterministic composed host vertical-access accounting summary used by recursive schedule tests and diagnostics. */
	struct FRecursiveHostVerticalAccessComposition
	{
		int32 RequiredHostProviderCount = 0;
		bool bRequiresExactHostProviderCount = false;
		int32 CountedParentProviderCount = 0;
		TArray<FString> CountedChildRegionDebugPaths;
		TArray<FString> LocalOnlyChildRegionDebugPaths;
		TArray<FString> UnusableContributingChildRegionDebugPaths;
		TArray<FString> ExtraContributingChildRegionDebugPaths;
		FString FailureReason;
	};

	/** Normalized owner kind for the current first-pass recursive host vertical-access policy. */
	enum class ERecursiveVerticalAccessOwnerKind : uint8
	{
		None,
		Parent,
		ChildRegion
	};

	/** Runs closure coverage validation against the current solve context. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateClosureCoverageForContext(FSolveContext& Context);

	/** Compiles supported hard closure segments once from finalized physical topology. */
	void CompileHardClosureSegmentsForContext(
		const FSolveContext& Context,
		TArray<FLayoutClosureCoverageSegmentRecord>& OutSegments);

	/** Applies snapshot bounds and yaw before matching one span offer against one closure segment. */
	bool DoesProjectedSpanOfferCoverClosureSegment(
		const FLayoutDerivedSpanOffer& WorldSpanOffer,
		const FIntVector& BundleRootCell,
		const FIntVector& BundleBoundsCells,
		int32 YawRotationSteps,
		const FLayoutClosureCoverageSegmentRecord& Segment);

	/** Exact selected structural endpoint; internal links may connect it across levels or channels. */
	struct PORISMDIMSWORLDGENERATOREXTENSION_API FSparseStructuralTraversalPort
	{
		/** Stable selected Entry, child, or VerticalAccess proof identity retained across route retries. */
		FLayoutId ProofId;
		/** Planned occupied endpoint; endpoint contacts constrain the provider without independently owning it. */
		FIntVector Cell = FIntVector::ZeroValue;
		/** Horizontal approaches; child ingress or committed internal anchors may be supplied without an external route face. */
		uint8 RouteFaceMask = 0;
		/** Traversal channel resolved from the selected candidate snapshot. */
		FGameplayTag TraversalChannel;
		/** Exact occupied/support/clearance claim with no optional route offer; never an endpoint commitment. */
		bool bStructuralOnly = false;
		/** Directed reachability proved inside this exact bundle, never inferred from a shared proof id. */
		TSet<FWalkableNodeKey> InternallyReachableNodes;
		/** Qualified gate roots seed connectivity independently, matching placed-graph entry semantics. */
		bool bIsEntryRoot = false;
		/** Exclusive occupied claims owned by this proof even when no route crosses their cells. */
		TArray<FIntVector> ClaimedCells;
		/** Filled obligations may share cells with another provider; exact candidate restrictions remain authoritative. */
		TArray<FIntVector> RequiredSupportCells;
		/** Cells that must remain empty throughout corridor search and assembled validation. */
		TArray<FIntVector> RequiredClearanceCells;
		/** Request-owned cell/channel that must remain Entry-reachable even when global traversal is optional. */
		TOptional<FLayoutCommittedTraversalAnchor> TraversalCommitment;
		/** Full child/fixed endpoint payload when this port came from an accepted endpoint commitment. */
		TOptional<FLayoutCommittedEndpointAnchor> EndpointCommitment;
		/** Accepted child owning the external face. EndpointCommitment remains child-local, Cell is parent ingress. */
		TSharedPtr<const FLayoutRegionSolveResult> ChildProof;
	};

	/** Tentative route diagnostics on rejection; accepted evidence only when the coordinator returns true. */
	struct PORISMDIMSWORLDGENERATOREXTENSION_API FSparseStructuralRouteAssignment
	{
		TArray<TArray<FIntVector>> Paths;
		TArray<FLayoutId> RetainedProofIds;
		TArray<FLayoutCommittedEndpointAnchor> RetainedEndpointCommitments;
		TArray<TSharedPtr<const FLayoutRegionSolveResult>> RetainedChildProofs;
		TArray<FIntVector> RejectedRouteCells;
		int32 AttemptCount = 0;
	};

	/**
	 * Imports one accepted child into a prepared parent trial. Keeps full immutable result, translates actual
	 * boundary faces, retains all external support/clearance obligations, and reserves replacement topology.
	 * Ports refer to parent ingress without claiming ownership; a selected gate/VA may supply that contact.
	 * Failure leaves parent/ports unchanged except inherited work/deadline accounting. Request must be the
	 * immutable request that produced ChildProof. Child solve work must already be charged by its caller.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryImportSparseStructuralChildProof(
		FSolveContext& ParentContext,
		const FLayoutRegionSolveRequest& ChildRequest,
		const FLayoutRegionSolveResult& ChildProof,
		TArray<FSparseStructuralTraversalPort>& InOutPorts,
		FString& OutFailureReason);

	/** Charges one bounded sparse preparation/route work unit, checking inherited deadline and cancellation. */
	bool ConsumeSparseStructuralWork(FSolveContext& Context);

	/** Builds exact claims and directed ports, including singleton-selected normal interfaces.
	 * Normal interfaces never become Entry roots. Optional portless claims retain occupancy, not reachability.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildSparseStructuralTraversalPorts(
		FSolveContext& PreparedContext,
		const TArray<FLayoutVerticalAccessHostGroup>& SelectedHostGroups,
		const TArray<int32>& SelectedOptionIndices,
		TArray<FSparseStructuralTraversalPort>& OutPorts,
		FString& OutFailureReason,
		const TArray<FIntVector>& NormalInterfaceCells = {});

	/**
	 * Connects selected exact ports under one solve budget. Advances only the base context's work/time
	 * accounting, including failed trials; placements and domains remain unchanged. Output must not alias
	 * the base context. The same CSP proves retained claims, mandatory support closure, corridors and all
	 * remaining normal active cells. Optional disconnected subsets are legal; required placed reachability stays hard.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TrySolveSparseStructuralPortAssignment(
		FSolveContext& PreparedBaseContext,
		const TArray<FSparseStructuralTraversalPort>& SelectedPorts,
		FSolveContext& OutSolvedContext,
		FSparseStructuralRouteAssignment& OutAssignment,
		FString& OutFailureReason);

	/** Compiles the prepared region footprint into route constraints and reservation records. */
	void BuildRequiredTraversalRoutesForPreparedPlan(FSolveContext& Context);

	/** Returns whether a compiled reservation of the requested kind exists on the cell. */
	bool CellHasReservation(const FSolveContext& Context, const FIntVector& Cell, ELayoutCellReservationKind ReservationKind);

	/** Returns whether the cell has a required compiled traversal-route contract. */
	bool IsRequiredTraversalRouteCell(const FSolveContext& Context, const FIntVector& Cell);

	/** Scores how strongly the candidate satisfies a compiled traversal-route contract. */
	int32 GetCandidateRequiredTraversalRouteScore(const FSolveContext& Context, const FIntVector& Cell, const FSolveCandidate& Candidate);

	/** Returns whether the cell should use the uncapped reachable-face score for route-sensitive ordering. */
	bool ShouldUseFullReachableHorizontalFaceScore(const FSolveContext& Context, const FIntVector& Cell);

	/** Returns whether structured-fill cells should bias toward low traversal exposure. */
	bool ShouldPreferStructuredFillTraversalExposure(const FSolveContext& Context, const FIntVector& Cell);

	/** Compares traversal exposure preference between two candidates for structured-fill cells. */
	bool ShouldPreferLeftStructuredFillTraversalExposure(const FSolveContext& Context, const FSolveCandidate& Left, const FSolveCandidate& Right, bool& bOutHasPreference);

	/** Computes the reservation-aware MRV priority for an unsolved planned cell. */
	int32 GetReservationSelectionPriority(const FSolveContext& Context, const FIntVector& Cell);

	/** Filters initial domains against compiled traversal-route contracts. */
	bool ApplyRequiredTraversalRouteDomainConstraints(FSolveContext& Context);

	/**
	 * Compiles one prepared solve context through live route-domain pruning before
	 * indexed search structures exist so callers can inspect real route-domain
	 * feasibility before committing to the remaining solve stages. Variant reuse requires
	 * unchanged prepared module snapshots and fixed-only IDs; domains are always rebuilt.
	 */
	bool PrepareSolveContextThroughRouteDomainStage(FSolveContext& Context, bool bReusePreparedVariants = false);

	/** Materializes indexed candidate/search structures after route-domain pruning has stabilized the live domains. */
	void FinalizePreparedSolveContextForIndexedSearch(FSolveContext& Context);

	/**
	 * Continues one already-built solve context through indexed finalization,
	 * active-walkable seeding, and deterministic forced placements so callers
	 * can capture the exact pre-branch frontier without rebuilding the request.
	 */
	bool TryPrepareSolveContextThroughSearchPrefixStage(
		FSolveContext& Context,
		FPreparedSearchPrefixStageCarrier& OutStageCarrier);

	/**
	 * Derives immutable per-candidate branch work items from one already-built
	 * deterministic prefix frontier so later callers can fan out branch work
	 * without rediscovering the selected cell and candidate order.
	 */
	void
	BuildPreparedSearchBranchStageFromSearchPrefixStage(
		const FPreparedSearchPrefixStageCarrier& SearchPrefixStage,
		FPreparedSearchBranchStageCarrier& OutStageCarrier);

	/**
	 * Continues one solve context after the exact post-prefix branch frontier
	 * has already been prepared, avoiding duplicated frontier setup when a
	 * caller needs to inspect or hand off one immutable branch batch before
	 * recursive solve continues.
	 */
	bool
	ContinuePreparedSolveContextAfterSearchBranchStage(
		FSolveContext& Context,
		const FPreparedSearchBranchStageCarrier& SearchBranchStage,
		bool bValidateClosureContracts = true);

	/**
	 * Applies one exact post-prefix candidate and runs the immediate
	 * forward-check feasibility stage without entering deeper recursion yet.
	 */
	bool
	TryPrepareSolveContextThroughSearchBranchApplyStage(
		FSolveContext& Context,
		const FPreparedSearchBranchStageCarrier& SearchBranchStage,
		int32 WorkItemIndex,
		FPreparedSearchBranchApplyStageCarrier& OutStageCarrier);

	/**
	 * Continues one solve context after the exact post-prefix candidate apply /
	 * forward-check stage has already succeeded.
	 */
	bool
	ContinuePreparedSolveContextAfterSearchBranchApplyStage(
		FSolveContext& Context,
		const FPreparedSearchBranchApplyStageCarrier& SearchBranchApplyStage,
		bool bValidateClosureContracts = true);

	/**
	 * Continues one solve context after the deterministic prefix frontier has
	 * already been prepared, avoiding duplicated frontier setup when a caller
	 * needs to inspect or hand off that stage before recursive solve continues.
	 */
	bool
	ContinuePreparedSolveContextAfterSearchPrefixStage(
		FSolveContext& Context,
		const FPreparedSearchPrefixStageCarrier& SearchPrefixStage,
		bool bValidateClosureContracts = true);

	/**
	 * Continues a prepared solve context after route-domain pruning has already
	 * completed, avoiding duplicated preparation when a caller needs to inspect
	 * live route-domain state before finishing the solve.
	 */
	bool ContinuePreparedSolveContextAfterRouteDomainStage(
		FSolveContext& Context,
		bool bValidateClosureContracts = true);

	/** Returns whether the profile currently needs reachability-specific validation or heuristics. */
	bool DoesProfileRequireReachabilityValidation(const FSolveContext& Context);

	/** Seeds active traversal-channel state from entry candidates before recursive solving begins. */
	void SeedActiveWalkableAreasFromEntryCandidates(FSolveContext& Context);

	/**
	 * Traverses the directed placed graph from authored roots, or only ExplicitRoots for a local child proof.
	 * SparseWorkContext optionally charges graph construction/traversal and receives exhaustion state;
	 * callers must reject an interrupted (empty) result. Ordinary solver callers keep existing accounting.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API TSet<FWalkableNodeKey> BuildReachableWalkableNodesFromPlacedRoots(
		const FSolveContext& Context,
		const TArray<FWalkableNodeKey>* ExplicitRoots = nullptr,
		FSolveContext* SparseWorkContext = nullptr);

	/** Evaluates recursive host vertical-access ownership from the current reserved parent cells and child providers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FRecursiveHostVerticalAccessComposition DetermineRecursiveHostVerticalAccessCompositionForSchedule(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedParentCells,
		const TArray<FRecursiveVerticalAccessProvider>& ChildProviders);

	/** Evaluates recursive host vertical-access ownership from the current reserved parent cells and child providers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API ERecursiveVerticalAccessOwnerKind DetermineRecursiveVerticalAccessOwnerForSchedule(
		const FLayoutRegionSolveRequest& RootRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedParentCells,
		const TArray<FRecursiveVerticalAccessProvider>& ChildProviders,
		FString* OutChildRegionDebugPath = nullptr);

	/** Selects deterministic lower and upper host-facing child anchors once child-owned/composed ascent is negotiated. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TrySelectNegotiatedHostVerticalAccessAnchorsForTests(
		const TArray<FLayoutCommittedEndpointAnchor>& CandidateCommitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessLocalCells,
		FLayoutCommittedEndpointAnchor& OutIngressAnchor,
		FLayoutCommittedEndpointAnchor& OutEgressAnchor,
		TArray<FIntVector>& OutRouteCells,
		FString& OutFailureReason);

#if WITH_AUTOMATION_TESTS
	/** Verifies final audit normalization for one validated proof-selected parent host assignment. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool DoesProofSelectedParentHostRefinementPreserveResponsibilityContractForTests(
		const FLayoutNegotiatedChildResponsibilityContract& NegotiatedContract,
		const FLayoutRecursiveVerticalAccessSummary& ActualSummary,
		const FLayoutNegotiatedChildResponsibilityContract& ActualContract);

	/** Rebuilds one ordinary-root parent plan after child replacement settles exterior Entry alternatives. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TrySettleParentEntriesAroundChildReplacementForTests(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ParentFootprintSize,
		const TSet<FIntVector>& ReservedCells,
		int32 Seed,
		bool bAllowExteriorEntryRelocation,
		TArray<FLayoutPlannedCell>& OutParentPlannedCells,
		FString& OutFailureReason);

	/** Resolves exact shared capability support for focused committed-seam arbitration coverage. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryResolveSharedSeamCapabilitiesForTests(
		const FLayoutChildCapabilityEnvelope& FirstEnvelope,
		ELayoutFaceDirection FirstFaceDirection,
		const FLayoutChildCapabilityEnvelope& SecondEnvelope,
		ELayoutFaceDirection SecondFaceDirection,
		const FGameplayTag& InterfaceFamily);

	/** Applies exact ordinary/junction usage filtering to seam-owner candidate domains. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplyOwnedSeamUsageRestrictionsForTests(
		const FString& ParentRegionDebugPath,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		FLayoutRegionSolveRequest& InOutParentRequest,
		FString& OutFailureReason);

	/** Applies owner-domain filtering to one child request without parent-catalog substitution. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplyChildOwnedSeamUsageRestrictionsForTests(
		const FString& ParentRegionDebugPath,
		const FString& ChildRegionDebugPath,
		const FIntVector& ChildRegionCellOffset,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		FLayoutRegionSolveRequest& InOutParentRequest,
		FLayoutRegionSolveRequest& InOutChildRequest,
		FString& OutFailureReason);

	/** Certifies one exact parent/child module pair for focused seam-domain coverage. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryCertifyExactSharedSeamCandidatePairForTests(
		const FLayoutModuleSolveSnapshot& ParentModule,
		int32 ParentYawRotationSteps,
		const FLayoutModuleSolveSnapshot& ChildModule,
		int32 ChildYawRotationSteps,
		ELayoutFaceDirection FaceDirection,
		const FGameplayTag& InterfaceFamily,
		bool bChildEntry,
		bool& bOutParentCanOwn,
		bool& bOutChildCanOwn,
		FLayoutId& OutWitnessId);

	/** Returns whether mapped child cells preserve parent Entry and reserved-open exclusions. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool DoesMappedChildPlanAvoidParentExclusiveCellsForTests(
		const TArray<FLayoutPlannedCell>& ParentTranslatedChildCells,
		const TSet<FIntVector>& ParentChildExclusiveCells);

	/** Builds parent-facing support evidence from child planned intents for focused carrier-isolation coverage. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void AppendSyntheticParentSupportBoundaryPointsForTests(
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TSet<FIntVector>& ParentPlannedCells,
		bool bFirstChildCellIsShared,
		TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints);

	/** Merges committed one-cell seam segments into deterministic continuous runs. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void MergeCommittedPartitionSeamSegmentsIntoRunsForTests(
		TArray<FLayoutPartitionSeamRecord>& InOutSeams);

	/** Chooses one deterministic owner for a certified continuous seam run. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryChooseCommittedSeamRunOwnerForTests(
		const FString& ParentRegionDebugPath,
		const FString& FirstRegionDebugPath,
		bool bFirstCanOwn,
		int32 FirstSupportRunLength,
		bool bFirstOwnsDoorAnchor,
		const FString& SecondRegionDebugPath,
		bool bSecondCanOwn,
		int32 SecondSupportRunLength,
		bool bSecondOwnsDoorAnchor,
		FString& OutOwnerRegionDebugPath,
		FString& OutPassiveRegionDebugPath);

	/** Runs exact parent/child overlap arbitration and committed-seam construction. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildCommittedParentChildSeamForTests(
		const FLayoutChildCapabilityEnvelope& ParentEnvelope,
		const FLayoutChildCapabilityEnvelope& ChildEnvelope,
		TArray<FLayoutPartitionSeamRecord>& OutSeams,
		FString& OutFailureReason);

	/** Proves a direct child Entry handoff does not create structural seam ownership. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildPartitionSeamsForDirectChildEntryForTests(
		TArray<FLayoutPartitionSeamRecord>& OutSeams,
		FString& OutFailureReason);

	/** Runs exact committed sibling-seam construction for focused multi-pair rejection coverage. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildCommittedSiblingSeamsForTests(
		const TArray<FLayoutChildCapabilityEnvelope>& ChildEnvelopes,
		const TArray<FIntVector>& RegionOffsets,
		TArray<FLayoutPartitionSeamRecord>& OutSeams,
		FString& OutFailureReason);
#endif

	/** Validates that a negotiated child responsibility contract fully describes the chosen runtime obligations. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateNegotiatedChildResponsibilityContractForTests(
		const FLayoutNegotiatedChildResponsibilityContract& Contract,
		FString& OutFailureReason);

	/** Builds the independent child proof result used by split proof execution, including recursive child subtrees. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildIndependentChildProofResult(
		const FLayoutRegionSolveRequest& ChildProofRequest,
		FLayoutRegionSolveResult& OutChildProofResult,
		FString& OutFailureReason);

	/** Appends proof-execution contract assertions to one prepared parent or child request before split proof runs. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool PrepareIndependentProofRequestForExecution(
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason);

	/** Validates that a solved parent region really realized every committed root/parent endpoint anchor on its placements. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateCommittedEndpointAnchorCoverageOnPlacementsForSchedule(
		const FString& RegionDebugPath,
		const FIntVector& RegionOffset,
		const TArray<FLayoutPlacedModule>& Placements,
		const TArray<FLayoutCommittedEndpointAnchor>& Anchors,
		FString& OutFailureReason);

	/**
	 * Freezes every child contact-face boundary point from a proved parent result into a
	 * pointer-free handoff array. Runs in the parent worker after the parent solve and before
	 * child proof dispatch; the child's independent SolveRegion consumes only this frozen
	 * array and never queries the live parent result. Each boundary point carries the parent
	 * placement's effective face-rule data for true face-rule continuity.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FLayoutSolveBoundaryPoint> FreezeChildIncomingBoundaryPointsFromParentResult(
		const FLayoutRegionSolveResult& ParentProofResult,
		const FLayoutDirectChildRegionCommitment& DirectChildCommitment,
		const FLayoutRegionSolveRequest& ChildProofRequest,
		const TArray<FLayoutRegionSolveResult>* SiblingResults = nullptr);

	/**
	 * Builds one committed-contact child proof request from the frozen boundary-point
	 * handoff stored in DirectChildCommitment. Consumes only the frozen handoff
	 * (no live parent result) so the child proof stays independent and deterministic.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildIndependentCommittedChildProofRequestFromHandoff(
		const FLayoutDirectChildRegionCommitment& DirectChildCommitment,
		const FLayoutRegionSolveRequest& ChildProofRequest,
		FLayoutRegionSolveRequest& OutChildProofRequest,
		FString& OutFailureReason);

	/** Builds one full schedule result from already-proved parent/child results when a no-drop proof contract is safe for adapter-free reconstruction. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildIndependentProofScheduleResultFromContract(
		const LayoutRegionScheduleSolverFacade::FNegotiatedProofScheduleContract& ProofContract,
		FLayoutRegionSolveResult&& ParentProofResult,
		TArray<FLayoutRegionSolveResult>&& ChildProofResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason);

	/** Compatibility wrapper kept for existing test helpers while the shared internal proof helper remains the real owner. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildIndependentChildProofResultForTests(
		const FLayoutRegionSolveRequest& ChildProofRequest,
		FLayoutRegionSolveResult& OutChildProofResult,
		FString& OutFailureReason);

	/** Resolves parent-only host VerticalAccess after an optional child-provider slot remains unselected. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRecursiveVerticalAccessSummary ResolveParentOnlyVerticalAccessOwnershipForTests(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells);

	/** Builds child coordinate mapping for pure contract tests. Supplying ChildRequest also
	 * runs production child-owned bridge preparation with that profile's level/seam policy. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildChildStageMappingForTests(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const FIntPoint& ChildFootprintSize,
		const FIntVector& CandidateOffset,
		FName SourceEntryId,
		int32 YawRotationSteps,
		bool bChildSupportsSteppedTerrain,
		const TArray<FLayoutPlannedCell>& SourceChildPlannedCells,
		FLayoutChildStageMappingResult& OutMapping,
		FString& OutFailureReason,
		const FLayoutRegionSolveRequest* ChildRequest = nullptr);

	/** Promotes inherited mapping to cross-stage only when committed route endpoints cross stages and internal VerticalAccess proves the span. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPromoteChildStageMappingForCommittedRouteForTests(
		FLayoutChildStageMappingResult& InOutMapping,
		const TArray<FLayoutCommittedEndpointAnchor>& Commitments,
		const TArray<FLayoutPlannedCell>& ChildPlannedCells,
		const TArray<FIntVector>& ChildVerticalAccessCells,
		TArray<FIntVector>& OutRequiredVerticalRouteCells,
		FString& OutFailureReason);

	/** Runs the scheduler's negotiated-candidate normalization pass and reports collapse/trim counts. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void NormalizeSuccessfulChildPlacementCandidatesForTests(
		TArray<FNegotiatedCandidateNormalizationInputForTests>& InOutCandidates,
		const bool bPartialPlacementStep,
		const int32 PlacementDemandCount,
		int32& OutCollapsedCount,
		int32& OutBeamTrimmedCount);

	/** Returns whether an accepted placement acts as an authored entry root for the walkable area. */
	bool IsPlacedEntryRootForWalkableArea(const FSolveContext& Context, const FIntVector& Cell, const FSolveContext::FSolvePlacement& Placement, const FGameplayTag& WalkableArea);

	/** Scores how well a candidate can attach required walkable areas to the reachable frontier. */
	int32 GetCandidateReachabilityPreference(const FSolveContext& Context, const FIntVector& Cell, const FSolveCandidate& Candidate, const TSet<FWalkableNodeKey>& ReachableNodes);

	/** Removes detached candidates when at least one candidate can already attach to the reachable frontier. */
	void PruneDetachedReachabilityCandidatesWhenAttachable(const FSolveContext& Context, const FIntVector& Cell, const TSet<FWalkableNodeKey>& ReachableNodes, TArray<FSolveCandidate>& Candidates);

	/** Returns whether the candidate exposes any required walkable area on the cell. */
	bool CandidateExposesRequiredWalkableArea(const FSolveContext& Context, const FIntVector& Cell, const FSolveCandidate& Candidate);

	/** Returns whether the candidate can attach all required walkable areas to the established reachable frontier. */
	bool CandidateCanAttachAllEstablishedRequiredWalkableAreas(const FSolveContext& Context, const FIntVector& Cell, const FSolveCandidate& Candidate, const TSet<FWalkableNodeKey>& ReachableNodes, FGameplayTag& OutFirstUnreachableWalkableArea);

	/** Computes reachability-aware MRV ranking for an unsolved cell. */
	int32 GetCellReachabilitySelectionRank(const FSolveContext& Context, const FIntVector& Cell, const TArray<FSolveCandidate>& Candidates, const TSet<FWalkableNodeKey>& ReachableNodes);

	/** Builds frontier distances from the current reachable walkable graph. */
	TMap<FIntVector, int32> BuildReachabilityFrontierDistances(FSolveContext& Context, const TSet<FWalkableNodeKey>& ReachableNodes);

	/** Runs the final solved-layout reachability audit. */
	bool ValidateReachability(FSolveContext& Context, FString& OutFailureReason);

	/** Test-only wrapper that rebuilds exported boundary points from the current request/result contract. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FLayoutSolveBoundaryPoint> BuildExportedBoundaryPointsFromSolveResultForTests(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutSolveResult& SolveResult);

	/** Returns whether a frozen public placed-result carrier represents a real occupied bundle. */
	bool IsOccupiedPlacedModule(const FLayoutPlacedModule& Placement);

	/** Resolves the compiled module snapshot that matches the frozen placed-result bundle carrier. */
	const FLayoutModuleSolveSnapshot* FindModuleSnapshotForPlacedModule(
		const FLayoutRegionSolveRequest& Request,
		const FLayoutPlacedModule& Placement);

	/** Test-only wrapper for the current vertical-continuation fixed-neighbor selection logic. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TMap<FIntVector, FSolveContext::FSolvePlacement> BuildVerticalContinuationFixedNeighborsForTests(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TMap<FIntVector, FSolveContext::FSolvePlacement>& SolvedPlacements,
		int32 CurrentLevel,
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots = {});

	/** Test-only wrapper for the direct upward continuation-face classification used by rebuilt continuation levels. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TMap<FIntVector, uint8> BuildVerticalContinuationUpwardContinuationFaceMasksForTests(
		const TArray<FLayoutPlannedCell>& PlannedCells,
		int32 CurrentLevel);

	/** Test-only wrapper for next-level active traversal-area propagation from one solved continuation level. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TSet<FGameplayTag> BuildNextLevelActiveWalkableAreasForTests(
		const FSolveContext& LevelContext,
		const TSet<FGameplayTag>& CurrentLevelActiveAreas,
		int32 CurrentLevel);

	/** Test-only wrapper for the current vertical-access provisional orientation counting logic. */
	PORISMDIMSWORLDGENERATOREXTENSION_API int32 CountViableVerticalAccessOrientationsAtCellForTests(
		const TArray<FLayoutModuleSolveSnapshot>& ModuleSnapshots,
		const FLayoutProfileSolveSnapshot& Profile,
		const FIntPoint& FootprintSize,
		const TSet<FIntVector>& EntryCells,
		const FIntVector& Cell);

	/** Test-only wrapper for module-compatible authored VerticalAccess host admission. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool DoesVerticalAccessHostAdmitCandidatePairForTests(
		const FLayoutRegionSolveRequest& Request,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& SourceCells,
		const FIntVector& LowerCell,
		FString& OutFailureReason);

	/** Test-only wrapper for mapped physical-component VerticalAccess host planning. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplyVerticalAccessPlanningForTests(
		const FLayoutRegionSolveRequest& Request,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		TArray<FLayoutVerticalAccessHostGroup>& OutHostGroups,
		FString& OutFailureReason);

	/** Selects one complete non-overlapping assignment from frozen parent host groups. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TrySelectAdmittedParentVerticalAccessHostsForTests(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TSet<FIntVector>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedCells,
		TArray<FLayoutVerticalAccessHostGroup>& OutSelectedHostGroups,
		FString& OutFailureReason);

	/** Selects an atomic host assignment ordered by lower and upper route cost. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TrySelectRouteAwareParentVerticalAccessHostsForTests(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TSet<FIntVector>& ParentPlannedCells,
		const TSet<FIntVector>& ReservedCells,
		const TArray<FIntVector>& LowerRouteTargets,
		const TArray<FIntVector>& UpperRouteTargets,
		int32 Seed,
		TArray<FLayoutVerticalAccessHostGroup>& OutSelectedHostGroups,
		FString& OutFailureReason);

	/** Tests parent-relative level policy against stage-mapped child cells. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool DoesMappedChildPlanMatchLevelPlacementPolicyForTests(
		const TArray<FLayoutPlannedCell>& MappedChildPlannedCells,
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		int32 ParentModuleLevelOffset,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy,
		int32 SpecificLevel);

	/** Test-only wrapper that rebuilds oriented variants from frozen module snapshots. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void BuildOrientedVariantsForTests(
		FSolveContext& Context);

	/** Test-only wrapper that prepares a live solve context through the initial route-domain stage. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool PrepareSolveContextThroughRouteDomainStageForTests(
		FSolveContext& Context);

	/** Test-only wrapper for the traversal planner's best horizontal exposure query. */
	PORISMDIMSWORLDGENERATOREXTENSION_API int32 GetBestHorizontalTraversalExposureForCellForTests(
		const FSolveContext& Context,
		const FIntVector& Cell);

	/** Selects stable representatives for exact traversal-capable structural domains. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FIntVector> BuildTraversalComponentRepresentativesForTests(
		const FSolveContext& Context,
		const TArray<FIntVector>& ExistingAnchorCells);

	/** Test-only wrapper for shared stepped-deck Boundary intermediate eligibility. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ShouldAllowBoundaryCellsAsIntermediateRouteStepForTests(
		const FSolveContext& Context,
		const FIntVector& Cell);

	/** Resolves a level fill rule using the cell's preserved module-policy level. */
	PORISMDIMSWORLDGENERATOREXTENSION_API ELayoutLevelFillMode GetLevelFillModeForCellForTests(
		const FSolveContext& Context,
		const FIntVector& Cell);

	/** Test-only wrapper for the final solved-placement adjacency audit. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RefreshCompiledFaceInterfacesForTests(
		FSolveContext& Context);

	/** Runs production host enumeration directly, retaining its exact last authority for boundary regressions. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolveResult SolveVerticalAccessHostAlternativesForTests(
		const FLayoutRegionSolveRequest& Request,
		FLayoutRegionSolveRequest& OutLastAttemptRequest);

	/** Production witness-application seam for retry ownership and support-domain regressions. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplySelectedVerticalAccessCandidateWitnessesForTests(
		const TArray<FLayoutVerticalAccessHostGroup>& HostGroups,
		const TArray<int32>& OptionIndices,
		FLayoutRegionSolveRequest& Request,
		FString& FailureReason);

	/** Prepares local domains from frozen request topology without entering parent route or host-prefix proof. Not a solved/publication result. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPrepareSparseStructuralLocalSolveView(
		const FLayoutRegionSolveRequest& Request,
		const TSet<FIntVector>& LocalWorkCells,
		FSolveContext& OutLocalContext,
		FString& OutFailureReason);

	/**
	 * Builds a sparse local work view retaining committed occupancy and full owning topology.
	 * Variant reuse requires unchanged prepared module snapshots and fixed-only IDs; it never reuses domains.
	 * Discovery-only callers may skip indexed compatibility; normal solve preparation builds it before search.
	 * Callers restoring offer topology before admission may also defer domains (requires indexing disabled).
	 * Deferred callers must build fresh domains after restoring topology, before inspecting candidates.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildSparseStructuralLocalSolveView(
		const FSolveContext& OwningContext,
		const TSet<FIntVector>& LocalWorkCells,
		FSolveContext& OutLocalContext,
		FString& OutFailureReason,
		bool bReusePreparedVariants = false,
		bool bPrepareIndexedSearch = true,
		bool bPrepareDomains = true);

	/** Test-only wrapper for the final solved-placement adjacency audit. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateSolvedPlacementAdjacencyForTests(
		FSolveContext& Context,
		FString& OutFailureReason);

	/** Test-only wrapper for projecting one solve context's occupied bundle roots onto the public result carrier. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolveResult BuildSolveResultFromContextPlacementsForTests(
		const FSolveContext& Context);

	/** Test-only wrapper for sparse fixed-neighbor seeding from an existing solved result. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TMap<FIntVector, FSolveContext::FSolvePlacement> BuildSparseFixedNeighborPlacementsForTests(
		const FLayoutSolveResult& SolveResult);

	/** Test-only wrapper for solve-result occupied world-cell reconstruction. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TSet<FIntVector> BuildSolveResultOccupiedWorldCellsForTests(
		const FLayoutSolveResult& SolveResult);

	/** Test-only wrapper for the current placement face-rule lookup. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryGetPlacementFaceRuleForTests(
		const FSolveContext& Context,
		const FSolveContext::FSolvePlacement& Placement,
		const ELayoutFaceDirection Direction,
		FLayoutFaceRule& OutFaceRule);

	/** Test-only wrapper for the current reachability-attachment preference calculation. */
	PORISMDIMSWORLDGENERATOREXTENSION_API int32 GetCandidateReachabilityPreferenceForTests(
		const FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		const TSet<FWalkableNodeKey>& ReachableNodes);

	/** Test-only wrapper for the current candidate compatibility audit. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool IsCandidateCompatibleForTests(
		FSolveContext& Context,
		const FIntVector& Cell,
		const FSolveCandidate& Candidate,
		FString& OutFailureReason);

	/** Test-only wrapper for atomically publishing one occupied bundle into the live placement map. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FIntVector> CommitOccupiedCandidateBundleForTests(
		FSolveContext& Context,
		const FIntVector& RootCell,
		const FSolveCandidate& Candidate);

	/** Test-only wrapper for removing one previously published occupied bundle from the live placement map. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void RollbackOccupiedCandidateBundleForTests(
		FSolveContext& Context,
		const TArray<FIntVector>& PlacedCells);

	/** Test-only wrapper for the immediate unsolved-neighbor forward-check pass over a just-placed cell set. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ForwardCheckPlacedCellsForTests(
		FSolveContext& Context,
		const TArray<FIntVector>& PlacedCells,
		FString& OutFailureReason);

	/**
	 * Internal request-backed region solve seam shared by runtime parent-proof
	 * callers and the public facade so request-owned preparation does not drift
	 * across entry points. OutFinalAuthority receives the actual selected request;
	 * callers composing certificates must use it after success, not the preflight VA overlay.
	 * It must not alias Request. Permanent inherited domains are audited before replacement.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult SolveRequestBackedRegion(
		const FLayoutRegionSolveRequest& Request,
		bool bValidateLiveCompositePlacementGuard = true,
		FLayoutRegionSolveRequest* OutFinalAuthority = nullptr);

	/** Audits permanent inherited domains and all selected restrictions before proof authority is replaced. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateSelectedHostProofAuthority(
		const FLayoutRegionSolveRequest& InheritedRequest,
		const FLayoutRegionSolveRequest& SelectedRequest,
		const FLayoutSolveResult& Result,
		FString& OutFailureReason);

	/** Builds finalized request-owned topology without route preparation or module proof. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult BuildRequestBackedRegionPreparedTopology(
		const FLayoutRegionSolveRequest& Request);

	/** Builds child-intrinsic topology while deferring every transform-owned external face contract. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult BuildRequestBackedChildIntrinsicPreparedTopology(
		const FLayoutRegionSolveRequest& Request);

	/** Revalidates one VerticalAccess host against current prepared topology. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool DoesPreparedVerticalAccessHostAdmitCandidatePair(
		const FLayoutRegionSolveRequest& Request,
		const TArray<FLayoutPlannedCell>& SourceCells,
		const FIntVector& LowerCell,
		FString& OutFailureReason);

	/** Rebuilds module-admitted VerticalAccess host alternatives for prepared topology. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool RebuildVerticalAccessPlanningForPreparedTopology(
		const FLayoutRegionSolveRequest& Request,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		TArray<FLayoutVerticalAccessHostGroup>& OutHostGroups,
		FString& OutFailureReason);

	/** Test-only wrapper that runs the live root solver without the transitional blanket composite guard. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult SolveRegionIgnoringLiveCompositePlacementGuardForTests(
		const FLayoutRegionSolveRequest& Request);

	/** Test-only wrapper that builds the live per-region solve context from one frozen request. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FSolveContext BuildSolveContextFromRequestForTests(
		const FLayoutRegionSolveRequest& Request);

	/**
	 * Builds the request-backed solve context at the exact live route-domain seam
	 * so callers can inspect real route-face feasibility without finishing the
	 * rest of the solve pipeline.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPrepareRequestSolveContextThroughRouteDomainStage(
		const FLayoutRegionSolveRequest& Request,
		FSolveContext& OutContext,
		bool bDeferFinalReachabilityAudit = false);


	/**
	 * Request-local continuation/failure carrier for one request-backed leaf
	 * solve prepared through the deterministic prefix seam.
	 *
	 * If `bReadyForFullSolveContinuation` is true, the caller can continue full
	 * solve from `Context` and `SearchPrefixStage` without rebuilding
	 * request-owned state. Otherwise `FailureSolveResult` already contains the
	 * normalized prepared failure solve state for the prepared route-domain or
	 * deterministic-prefix stages.
	 */
	struct FPreparedRequestBackedLeafSearchPrefixContinuation
	{
		FSolveContext Context;
		FPreparedSearchPrefixStageCarrier SearchPrefixStage;
		FLayoutSolveResult FailureSolveResult;
		bool bReadyForFullSolveContinuation = false;
	};

	/** Checks whether a child content set carries at least one boundary-intent
	 *  module whose face rule for ChildFaceDirection is compatible with
	 *  ParentFaceRule.  Used by the parent CSP to validate child boundary
	 *  feasibility before accepting a parent placement.
	 *  If OutChildConnectionTag is non-null and a compatible module is found,
	 *  it receives the child module's connection tag for freeze use. */
	bool HasCompatibleChildBoundaryModule(
		const FLayoutRegionContentSetSolveSnapshot& ChildContentSet,
		const FLayoutModuleCatalog& ModuleCatalog,
		ELayoutCellIntent Intent,
		ELayoutFaceDirection ChildFaceDirection,
		const FLayoutFaceRule& ParentFaceRule,
		FGameplayTag* OutChildConnectionTag = nullptr);

	/** Multi-face collective variant: checks whether a child content set
	 *  carries at least one boundary-intent module whose face rules for EVERY
	 *  direction in ParentFaceRulesByDirection are simultaneously compatible
	 *  with the corresponding parent face rules.  Used by the parent CSP to
	 *  prevent layouts where individual face-pair checks pass but no single
	 *  child module satisfies all parent faces at a child cell at once.
	 *  If OutChildConnectionTag is non-null and a compatible module is found,
	 *  it receives the first compatible module's connection tag for freeze use.
	 *  Empty parent admission checks occupancy only, preserving sparse child contacts. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool HasCompatibleChildBoundaryModule(
		const FLayoutRegionContentSetSolveSnapshot& ChildContentSet,
		const FLayoutModuleCatalog& ModuleCatalog,
		const FLayoutPlannedCell& ChildPlannedCell,
		int32 ChildTopModuleLevel,
		const TMap<ELayoutFaceDirection, FLayoutFaceRule>& ParentFaceRulesByDirection,
		FGameplayTag* OutChildConnectionTag = nullptr,
		bool bParentFilled = true);

	/** Returns the collectively-compatible child module's face rule for a
	 *  single child-facing direction.  Call this after the multi-face
	 *  HasCompatibleChildBoundaryModule passes to extract per-face connection
	 *  tags from the found module for freeze boundary points.
	 *  Returns nullptr if no collectively-compatible module exists. */
	const FLayoutFaceRule* GetChildBoundaryModuleFaceRuleForDirection(
		const FLayoutRegionContentSetSolveSnapshot& ChildContentSet,
		const FLayoutModuleCatalog& ModuleCatalog,
		const TMap<ELayoutFaceDirection, FLayoutFaceRule>& ParentFaceRulesByDirection,
		ELayoutFaceDirection ChildFaceDirection);

	/**
	 * Request-local continuation/failure carrier for one request-backed leaf
	 * solve prepared through the explicit post-prefix branch frontier.
	 *
	 * If `bReadyForFullSolveContinuation` is true, the caller can continue full
	 * solve from `Context` and `SearchBranchStage` without rebuilding
	 * request-owned state. Otherwise `FailureSolveResult` already contains the
	 * normalized prepared failure solve state for the prepared route-domain or
	 * deterministic-prefix stages.
	 */
	struct FPreparedRequestBackedLeafSearchBranchContinuation
	{
		FSolveContext Context;
		FPreparedSearchBranchStageCarrier SearchBranchStage;
		FLayoutSolveResult FailureSolveResult;
		bool bReadyForFullSolveContinuation = false;
	};

	/**
	 * Request-local continuation/failure carrier for one exact post-prefix
	 * candidate after immediate placement and forward-check feasibility.
	 *
	 * If `bReadyForFullSolveContinuation` is true, the caller can continue
	 * recursive solve from `Context` and `SearchBranchApplyStage` without
	 * rebuilding request-owned state. Otherwise `FailureSolveResult` already
	 * contains the normalized prepared failure solve state at the branch-apply
	 * seam.
	 */
	struct FPreparedRequestBackedLeafSearchBranchApplyContinuation
	{
		FSolveContext Context;
		FPreparedSearchBranchApplyStageCarrier SearchBranchApplyStage;
		FLayoutSolveResult FailureSolveResult;
		bool bReadyForFullSolveContinuation = false;
	};

	/**
	 * Prepares the request-backed solve context through indexed finalization,
	 * active-walkable seeding, and deterministic forced placements so callers
	 * can inspect the real search frontier before recursive branching begins.
	 *
	 * The returned stage summary is immutable and request-local so later
	 * validation or threading-oriented callers can consume the exact frontier
	 * without rediscovering it from ad hoc counters.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPrepareRequestSolveContextThroughSearchPrefixStage(
		const FLayoutRegionSolveRequest& Request,
		FSolveContext& OutContext,
		FPreparedSearchPrefixStageCarrier& OutStageCarrier,
		bool bDeferFinalReachabilityAudit = false);

	/**
	 * Lazily settles parent authority with distinct Entry/VA child-clearance domains.
	 * When OutFinalProof is supplied, complete the selected request proof and retain singleton host
	 * authority instead of returning a prefix with alternatives. The caller owns final schedule audits.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryFreezeParentAuthorityCellsFromPreparedPrefix(
		const FLayoutRegionSolveRequest& Request,
		bool bAllowExteriorEntryRelocation,
		const TSet<FIntVector>& ForbiddenEntryCells,
		const TSet<FIntVector>& ForbiddenVerticalAccessCells,
		FLayoutRegionSolveRequest& OutRequest,
		FString& OutFailureReason,
		FLayoutRegionSolveResult* OutFinalProof = nullptr);

	/**
	 * Prepares one request-backed leaf-region solve through the deterministic
	 * prefix seam and returns either a continuation-ready carrier or the
	 * normalized failure result at that seam.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool
		TryPrepareRequestBackedLeafRegionThroughSearchPrefixStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchPrefixContinuation& OutContinuation,
			bool bDeferFinalReachabilityAudit = false);

	/**
	 * Prepares one request-backed leaf-region solve through the explicit
	 * post-prefix branch frontier and returns either a continuation-ready
	 * carrier or the normalized failure result at the earlier route-domain /
	 * deterministic-prefix seams.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool
		TryPrepareRequestBackedLeafRegionThroughSearchBranchStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchBranchContinuation& OutContinuation,
			bool bDeferFinalReachabilityAudit = false);

	/**
	 * Prepares one request-backed leaf-region solve through the exact
	 * post-prefix candidate apply / forward-check seam and returns either a
	 * continuation-ready carrier or the normalized failure result at that seam.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool
		TryPrepareRequestBackedLeafRegionThroughSearchBranchApplyStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchBranchContinuation&& BranchContinuation,
			int32 WorkItemIndex,
			FPreparedRequestBackedLeafSearchBranchApplyContinuation& OutContinuation);

	/**
	 * Consumes one already-applied branch continuation and advances it through
	 * the next deterministic prefix / forced-placement seam before any deeper
	 * recursive branching resumes.
	 *
	 * This exposes the later parent-only feasibility boundary needed by staged
	 * validation without forcing callers to jump straight from branch apply into
	 * full recursive solve.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool
		TryPrepareRequestBackedLeafRegionThroughSearchPrefixStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchBranchApplyContinuation&& BranchApplyContinuation,
			FPreparedRequestBackedLeafSearchPrefixContinuation& OutContinuation);

	/**
	 * Finalizes one already-built leaf-region solve context into the normal
	 * request-backed result shape without rebuilding the request-owned runtime
	 * state first.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		BuildRequestBackedLeafRegionResultFromPreparedContext(
			const FLayoutRegionSolveRequest& Request,
			FSolveContext&& Context,
			bool bPublishTrace = false);

	/**
	 * Continues one already-prepared leaf-region solve from the immutable
	 * deterministic prefix carrier and returns the normal request-backed solve
	 * result shape.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		ContinuePreparedRequestBackedLeafRegionAfterSearchPrefixStage(
			const FLayoutRegionSolveRequest& Request,
			FSolveContext&& Context,
			const FPreparedSearchPrefixStageCarrier& SearchPrefixStage);

	/**
	 * Continues one already-prepared leaf-region solve from the immutable
	 * post-prefix branch frontier and returns the normal request-backed solve
	 * result shape.
	 *
	 * Request-backed leaf solves consume the exact post-branch deterministic
	 * prefix seam before deeper recursion so later feasibility failures can
	 * stop earlier without adding ranking heuristics.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
			const FLayoutRegionSolveRequest& Request,
			FSolveContext&& Context,
			const FPreparedSearchBranchStageCarrier& SearchBranchStage);

	/**
	 * Continues one already-prepared leaf-region solve from the exact
	 * post-prefix candidate apply / forward-check seam and returns the normal
	 * request-backed solve result shape.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		ContinuePreparedRequestBackedLeafRegionAfterSearchBranchApplyStage(
			const FLayoutRegionSolveRequest& Request,
			FSolveContext&& Context,
			const FPreparedSearchBranchApplyStageCarrier& SearchBranchApplyStage);

	/**
	 * Consumes one request-local deterministic-prefix continuation/failure
	 * carrier and returns the normal request-backed solve result shape.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		ContinuePreparedRequestBackedLeafRegionAfterSearchPrefixStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchPrefixContinuation&& Continuation);

	/**
	 * Consumes one request-local post-prefix branch continuation/failure
	 * carrier and returns the normal request-backed solve result shape.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		ContinuePreparedRequestBackedLeafRegionAfterSearchBranchStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchBranchContinuation&& Continuation);

	/**
	 * Consumes one request-local post-prefix branch-apply continuation/failure
	 * carrier and returns the normal request-backed solve result shape.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
		ContinuePreparedRequestBackedLeafRegionAfterSearchBranchApplyStage(
			const FLayoutRegionSolveRequest& Request,
			FPreparedRequestBackedLeafSearchBranchApplyContinuation&& Continuation);

	/** Builds the prepared-plan route-demand summary for one frozen request without running full solve recursion. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FPreparedPlanRouteDemandSummary BuildPreparedPlanRouteDemandSummaryForRequest(
		const FLayoutRegionSolveRequest& Request);

	/** Runs the partial forward-check reachability feasibility audit. */
	bool ValidatePartialReachabilityFeasibility(FSolveContext& Context, FString& OutFailureReason);

	/** Rebuilds residual planned-cell records after placements and structural commitments settle. */
	void PopulateResidualUnoccupiedCells(FLayoutSolveResult& SolveResult);

	/** Finds an unoccupied mandatory bridge after structural solve; uncommitted top-deck offers may remain empty. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryFindFirstUnoccupiedGeneratedBridgeCell(
		const FLayoutSolveResult& SolveResult,
		FLayoutPlannedCell& OutBridgeCell);

	/** Applies profile-authored sparse placement rules after structural commitments settle. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplySparsePlacementRulesToSolveResult(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const FString& RegionDebugPath,
		const int32 Seed,
		FLayoutSolveResult& SolveResult,
		FString& OutFailureReason);

	/** One scheduled planned cell projected into world space for seam planning. */
	struct FScheduleSeamPlanningCell
	{
		FIntVector WorldCell = FIntVector::ZeroValue;
		FIntVector LocalCell = FIntVector::ZeroValue;
		ELayoutCellIntent Intent = ELayoutCellIntent::Interior;
	};

	/** One candidate shared seam segment between two scheduled regions. */
	struct FCompiledSeamSegment
	{
		FString RegionAPath;
		FString RegionBPath;
		FIntVector RegionACell = FIntVector::ZeroValue;
		FIntVector RegionBCell = FIntVector::ZeroValue;
		ELayoutCellIntent RegionAIntent = ELayoutCellIntent::Interior;
		ELayoutCellIntent RegionBIntent = ELayoutCellIntent::Interior;
		ELayoutFaceDirection RegionAFaceDirection = ELayoutFaceDirection::PosX;
		ELayoutFaceDirection RegionBFaceDirection = ELayoutFaceDirection::NegX;
	};

	/** The chosen owner/passive seam contract for one contiguous run. */
	struct FChosenSeamContract
	{
		bool bValid = false;
		FString OwnerRegionPath;
		FString PassiveRegionPath;
		FGameplayTag InterfaceFamily;
		ELayoutFaceDirection OwnerFaceDirection = ELayoutFaceDirection::PosX;
		ELayoutFaceDirection PassiveFaceDirection = ELayoutFaceDirection::NegX;
	};

	/** Returns whether a child exposes any authored seam capability on the requested face. */
	bool HasAnyChildSeamCapabilityForFace(
		const FLayoutChildCapabilityEnvelope& Envelope,
		const ELayoutFaceDirection FaceDirection);

	/** Rebuilds schedule planned cells in world space for seam-planning leaf stages. */
	bool TryGetSchedulePlannedCellsForSeamPlanning(
		const FLayoutRegionSolveRequest& Request,
		TArray<FScheduleSeamPlanningCell>& OutPlannedCells);

	/** Collects shared seam segments between one explicit pair of scheduled regions. */
	void CollectSharedSeamSegmentsForPair(
		const FString& RegionAPath,
		const TArray<FScheduleSeamPlanningCell>& RegionAPlannedCells,
		const FString& RegionBPath,
		const TArray<FScheduleSeamPlanningCell>& RegionBPlannedCells,
		const bool bIncludeAdjacentSegments,
		const bool bIncludeOverlapSegments,
		const bool bAllowSameFaceOverlap,
		TArray<FCompiledSeamSegment>& OutSegments);

	/** Returns whether one compiled seam segment requires the door interface family. */
	bool DoesSeamSegmentRequireDoorInterface(const FCompiledSeamSegment& Segment);

	/** Groups shared seam segments into deterministic contiguous runs. */
	void BuildContiguousSeamRuns(
		const TArray<FCompiledSeamSegment>& Segments,
		TArray<TArray<FCompiledSeamSegment>>& OutRuns);

	/** Chooses one owner/passive seam contract for a contiguous shared-seam run. */
	bool TryChooseSeamContractForRun(
		const TArray<FCompiledSeamSegment>& RunSegments,
		const FString& PreferredOwnerPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		FChosenSeamContract& OutChosenContract);

#if WITH_AUTOMATION_TESTS
	/** Runs leaf-stage seam arbitration for focused exact-domain coverage. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryChooseLeafStageSeamContractForTests(
		const TArray<FCompiledSeamSegment>& RunSegments,
		const FString& PreferredOwnerPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		FChosenSeamContract& OutChosenContract);
#endif

	/** Rebuilds one plain-data partition seam record from a chosen run contract. */
	FLayoutPartitionSeamRecord BuildPartitionSeamRecordFromRun(
		const FString& ParentRegionPath,
		const TArray<FCompiledSeamSegment>& RunSegments,
		const FChosenSeamContract& ChosenContract);

	/** Applies the shared pair-level seam policy using the deterministic seam leaf stages. */
	bool AppendSchedulePartitionSeamsForPairFromLeafStages(
		const FString& ParentPath,
		const FString& LeftPath,
		const FString& RightPath,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		TArray<FLayoutPartitionSeamRecord>& InOutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason);

	/** Applies the shared parent-family seam iteration using the shared pair-level policy. */
	bool AppendSchedulePartitionSeamsForParentFromSharedPairPolicy(
		const FString& ParentPath,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		const TArray<FString>& Children,
		TArray<FLayoutPartitionSeamRecord>& InOutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason);

	/** Applies the shared whole-schedule seam iteration using the shared parent-family policy. */
	bool BuildSchedulePartitionSeamsFromSharedParentPolicy(
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		const TMap<FString, TArray<FString>>& ChildrenByParent,
		TArray<FLayoutPartitionSeamRecord>& OutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason);

	/** Plans straight parent-owned partition seams for compatible scheduled child adjacencies. */
	bool BuildSchedulePartitionSeams(
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		const TMap<FString, TArray<FString>>& ChildrenByParent,
		TArray<FLayoutPartitionSeamRecord>& OutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason);

	/** Appends planned partition seams for one parent-owned region family without hiding the caller's iteration order. */
	bool AppendSchedulePartitionSeamsForParent(
		const FString& ParentPath,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		const TArray<FString>& Children,
		TArray<FLayoutPartitionSeamRecord>& InOutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason);

	/** Appends planned partition seams for one explicit region pair under one parent-owned region family. */
	bool AppendSchedulePartitionSeamsForPair(
		const FString& ParentPath,
		const FString& LeftPath,
		const FString& RightPath,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, FLayoutChildCapabilityEnvelope>& CapabilityEnvelopeByRegion,
		TArray<FLayoutPartitionSeamRecord>& InOutSeams,
		TSet<FString>& OutPassiveRegionPaths,
		const bool bPlannedSeamsAreAuthoritative,
		FString& OutFailureReason);

	/** Sorts planned partition seam records deterministically once a planner-owned pass is complete. */
	void SortSchedulePartitionSeams(TArray<FLayoutPartitionSeamRecord>& InOutSeams);

	/** Re-runs deferred closure coverage for parent regions after committed children are solved. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ReevaluateDeferredClosureCoverageForRegion(
		const FString& RegionPath,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		const TMap<FString, TArray<FString>>& ChildrenByParent,
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason);

	/** Verifies each committed owner-side junction is realized by one placed module supporting both owned faces. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateOwnedSeamJunctionRequirements(
		const TArray<FLayoutOwnedSeamJunctionRequirement>& JunctionRequirements,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason);

	/** Re-runs passive seam closure coverage after owner-side seam providers are known. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ReevaluatePassiveSeamClosureCoverageForRegion(
		const FString& PassiveRegionPath,
		const TArray<FLayoutPartitionSeamRecord>& PartitionSeams,
		const TMap<FString, const FLayoutRegionSolveRequest*>& RequestsByPath,
		const TMap<FString, int32>& RegionResultIndexByPath,
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason);

	/** Resolves stepped Entry intents and ordered VerticalAccess host choices before CSP. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool EnrichPlannedCellsWithSteppedTerrainIntents(
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason,
		ELayoutSteppedTerrainFinalizationFailureKind* OutFailureKind = nullptr);

}
