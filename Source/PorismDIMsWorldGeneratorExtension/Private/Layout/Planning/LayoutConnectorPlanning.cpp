// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Layout/Solver/LayoutStandaloneRegionRequestBuilder.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Types/LayoutGameplayTags.h"

namespace
{
	const FLayoutId ConnectorPlacementPolicyId(TEXT("ConnectorExplicit"));

	/** Converts one horizontal endpoint face into its route portal offset. */
	bool TryGetHorizontalEntryPortalOffset(
		const ELayoutFaceDirection EntryFaceDirection,
		FIntPoint& OutPortalOffset)
	{
		switch (EntryFaceDirection)
		{
		case ELayoutFaceDirection::PosX: OutPortalOffset = FIntPoint(1, 0); return true;
		case ELayoutFaceDirection::NegX: OutPortalOffset = FIntPoint(-1, 0); return true;
		case ELayoutFaceDirection::PosY: OutPortalOffset = FIntPoint(0, 1); return true;
		case ELayoutFaceDirection::NegY: OutPortalOffset = FIntPoint(0, -1); return true;
		default:
			OutPortalOffset = FIntPoint::ZeroValue;
			return false;
		}
	}

	FIntPoint ToConnectorCell(
		const FIntVector& StartWorldPos,
		const FIntVector& CurrentWorldPos,
		const FIntVector& ConnectorCellSizeInBlocks);

	/** Returns block-min grid origin for route terrain samples from center-based endpoint data. */
	FIntVector GetConnectorRouteGridOrigin(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FIntVector& SharedCellSizeInBlocks)
	{
		return ConnectorRecord.StartEndpointBlockWorldPos - FIntVector(
			SharedCellSizeInBlocks.X / 2,
			SharedCellSizeInBlocks.Y / 2,
			SharedCellSizeInBlocks.Z / 2);
	}

	struct FConnectorRoutePortals
	{
		FIntPoint StartPortalCell = FIntPoint::ZeroValue;
		FIntPoint StartStubCell = FIntPoint::ZeroValue;
		FIntPoint EndPortalCell = FIntPoint::ZeroValue;
		FIntPoint EndStubCell = FIntPoint::ZeroValue;
	};

	/** Builds fixed outward portal segments so endpoint doors retain a straight corridor approach. */
	bool TryBuildConnectorRoutePortals(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FIntVector& SharedCellSizeInBlocks,
		FConnectorRoutePortals& OutPortals)
	{
		const FIntPoint EndRootCell = ToConnectorCell(
			ConnectorRecord.StartEndpointBlockWorldPos,
			ConnectorRecord.EndEndpointBlockWorldPos,
			SharedCellSizeInBlocks);
		FIntPoint StartPortalOffset;
		FIntPoint EndPortalOffset;
		if (!TryGetHorizontalEntryPortalOffset(ConnectorRecord.StartRootEntryFaceDirection, StartPortalOffset)
			|| !TryGetHorizontalEntryPortalOffset(ConnectorRecord.EndRootEntryFaceDirection, EndPortalOffset))
		{
			return false;
		}
		OutPortals.StartPortalCell = StartPortalOffset;
		OutPortals.StartStubCell = OutPortals.StartPortalCell + StartPortalOffset;
		OutPortals.EndPortalCell = EndRootCell + EndPortalOffset;
		OutPortals.EndStubCell = OutPortals.EndPortalCell + EndPortalOffset;
		return true;
	}

	FString BuildConnectorRegionDebugPath(
		const FIntVector& StartEndpointBlockWorldPos,
		const FIntVector& EndEndpointBlockWorldPos)
	{
		return FString::Printf(
			TEXT("Connector/%s/%s"),
			*StartEndpointBlockWorldPos.ToString(),
			*EndEndpointBlockWorldPos.ToString());
	}

	FLayoutRootPublicationMetadata BuildConnectorRootPublicationMetadata(
		const FIntVector& StartEndpointBlockWorldPos,
		const FIntVector& EndEndpointBlockWorldPos)
	{
		const FString RegionDebugPath = BuildConnectorRegionDebugPath(
			StartEndpointBlockWorldPos,
			EndEndpointBlockWorldPos);
		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootPlacementPolicyId = ConnectorPlacementPolicyId;
		PublicationMetadata.RootCandidateId = FLayoutId(*RegionDebugPath);
		PublicationMetadata.RootSolveId = FLayoutId(*RegionDebugPath);
		return PublicationMetadata;
	}

	ELayoutWorldBindingPlacementKind ResolveConnectorPlanningContinuationFamilyPlacementKind(
		const ELayoutWorldBindingContinuationFamilyType FamilyType)
	{
		switch (FamilyType)
		{
		case ELayoutWorldBindingContinuationFamilyType::SurfacePath:
			return ELayoutWorldBindingPlacementKind::SurfacePath;
		case ELayoutWorldBindingContinuationFamilyType::BridgeContinuation:
			return ELayoutWorldBindingPlacementKind::BridgeContinuation;
		case ELayoutWorldBindingContinuationFamilyType::TunnelContinuation:
			return ELayoutWorldBindingPlacementKind::TunnelContinuation;
		default:
			return ELayoutWorldBindingPlacementKind::None;
		}
	}

	FLayoutWorldBindingPlacementPolicy BuildConnectorPlanningContinuationFrontendPlacementPolicy(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamily& Family)
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy =
			WorldBinding != nullptr
				? WorldBinding->DefaultPlacementPolicy
				: FLayoutWorldBindingPlacementPolicy();
		if (Family.bOverrideTerrainTransitionPolicy)
		{
			PlacementPolicy.TerrainTransition = Family.TerrainTransitionPolicyOverride;
		}
		return PlacementPolicy;
	}

	FResolvedLayoutConnectorRecord BuildContinuationConnectorRecord(
		const FResolvedLayoutConnectorEndpoint& LeftEndpoint,
		const FResolvedLayoutConnectorEndpoint& RightEndpoint,
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamily& Family,
		const FName FamilyCandidateId,
		const ULayoutProfileAsset* const ConnectorProfile,
		ULayoutRegionContentSetAsset* const ConnectorContentSet,
		const FName SharedBiomeRowName,
		const int32 WorldSeed,
		const uint64 PairKey)
	{
		FResolvedLayoutConnectorRecord Record;
		Record.StartSiteReservationKey = LeftEndpoint.SiteReservationKey;
		Record.EndSiteReservationKey = RightEndpoint.SiteReservationKey;
		Record.ConnectorTypeTag = LeftEndpoint.ConnectorTypeTag;

		FLayoutResolvedConnectorFrontendSelection FrontendSelection;
		const FLayoutWorldBindingPlacementPolicy FrontendPlacementPolicy =
			BuildConnectorPlanningContinuationFrontendPlacementPolicy(WorldBinding, Family);
		const ELayoutWorldBindingPlacementKind FrontendPlacementKind =
			ResolveConnectorPlanningContinuationFamilyPlacementKind(Family.FamilyType);
		FrontendSelection.WorldBindingId =
			WorldBinding == nullptr
				? NAME_None
				: (!WorldBinding->BindingId.IsNone()
					? WorldBinding->BindingId
					: WorldBinding->GetFName());
		FrontendSelection.BiomeRowName = SharedBiomeRowName;
		FrontendSelection.ContinuationFamilyId = Family.FamilyId;
		FrontendSelection.ContinuationFamilyCandidateId = FamilyCandidateId;
		FrontendSelection.PlacementKind = FrontendPlacementKind;
		FrontendSelection.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
		FrontendSelection.ResolvedContinuationSelection.PlacementKind =
			FrontendPlacementKind;
		FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel =
			ConnectorProfile != nullptr
				? ConnectorProfile->ContinuationEntryLevel
				: INDEX_NONE;
		FrontendSelection.TerrainPathSelection.bEnableTerrainAwarePathing = true;
		// Root biome ownership does not constrain intervening terrain. Empty path
		// row selection uses any active biome surface, retaining slope/support checks.
		FrontendSelection.WorldBindingPlacementPolicy = FrontendPlacementPolicy;
		FrontendSelection.ContinuationPolicy = Family.ContinuationPolicy;
		FrontendSelection.SolveBudget = Family.SolveBudget;
		FrontendSelection.SharedCellSizeInBlocks =
			WorldBinding != nullptr
				? WorldBinding->BaseCellDimensionsBlocks
				: FIntVector::ZeroValue;
		FrontendSelection.TemplatePlacementZOffsetBlocks =
			WorldBinding != nullptr
				? WorldBinding->TemplatePlacementZOffsetBlocks
				: 0;
		Record.SetResolvedConnectorFrontendSelection(FrontendSelection);
		Record.StartEndpointBlockWorldPos = LeftEndpoint.EndpointBlockWorldPos;
		Record.EndEndpointBlockWorldPos = RightEndpoint.EndpointBlockWorldPos;
		Record.StartRootEntryFaceDirection = LeftEndpoint.ExposedEntryFaceDirection;
		Record.EndRootEntryFaceDirection = RightEndpoint.ExposedEntryFaceDirection;
		Record.SetRootPublicationMetadata(
			BuildConnectorRootPublicationMetadata(
				Record.StartEndpointBlockWorldPos,
				Record.EndEndpointBlockWorldPos));

		FLayoutResolvedConnectorSolveSourceSelection SolveSourceSelection;
		SolveSourceSelection.ContentSet =
			TSoftObjectPtr<ULayoutRegionContentSetAsset>(ConnectorContentSet);

		SolveSourceSelection.LayoutProfile =
			TSoftObjectPtr<ULayoutProfileAsset>(const_cast<ULayoutProfileAsset*>(ConnectorProfile));
		SolveSourceSelection.SolveSeed = static_cast<int32>(
			HashCombine(static_cast<uint32>(PairKey), static_cast<uint32>(WorldSeed)));
		Record.SetResolvedConnectorSolveSourceSelection(SolveSourceSelection);
		return Record;
	}

	// Keep terrain-path settings private to the active connector planner. The
	// active search contract now comes from the shared world-binding placement
	// policy, while family-specific path tuning still rides on the continuation
	// family carrier.
	struct FConnectorPathPlanningSettings
	{
		bool bEnableTerrainAwarePathing = true;
		FName PathBiomeRowName;
		TArray<FName> PathBiomeRowNames;
		int32 TerrainSearchStartZ = 256;
		int32 TerrainSearchDepthBlocks = 256;
		int32 PathPaddingCells = 4;
		int32 MaxSlopeBlocks = 3;
		int32 MaxBridgeGapCells = 0;
		bool bAllowSteepTerrainTunneling = false;
	};

	enum class EInternalConnectorPathCellType : uint8
	{
		Walkable,
		Gap,
		Blocked
	};

	struct FInternalConnectorPathCell
	{
		EInternalConnectorPathCellType Type = EInternalConnectorPathCellType::Blocked;
		int32 SurfaceZBlockWorld = 0;
	};

	struct FInternalConnectorPathQuery
	{
		FIntPoint StartCell = FIntPoint::ZeroValue;
		FIntPoint EndCell = FIntPoint::ZeroValue;
		FIntPoint MinCell = FIntPoint::ZeroValue;
		FIntPoint MaxCell = FIntPoint::ZeroValue;
		int32 MaxSlopeBlocks = 3;
		int32 MaxBridgeGapCells = 0;
		bool bAllowSteepTerrainTunneling = false;
	};

	FLayoutValidationAssertionRecord MakeConnectorPlanningAssertionRecord(
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

	bool HasResolvedConnectorWorldBindingFrontendCarrierForPlanning(
		const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		// Default nested placement-policy state is not authoritative proof that a
		// connector came from the world-binding continuation path.
		return FrontendSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None
			|| !FrontendSelection.ResolvedContinuationSelection.FamilyId.IsNone()
			|| !FrontendSelection.ContinuationFamilyId.IsNone();
	}

	FLayoutValidationAssertionRecord MakeConnectorWorldPlacementLatticeAssertion(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FIntVector& SharedCellSizeInBlocks,
		const FString& RegionDebugPath)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		const bool bWorldFacingPlacement =
			HasResolvedConnectorWorldBindingFrontendCarrierForPlanning(ConnectorRecord);
		const int32 SharedCellHeightInBlocks = SharedCellSizeInBlocks.Z;
		const bool bHasUsableLatticeHeight = SharedCellHeightInBlocks > 0;
		// World-binding template offsets shift connector cell centers from the raw
		// shared-cell midpoint. Validate same physical center used by placement.
		const int32 ExpectedCenterOffsetInBlocks =
			bHasUsableLatticeHeight
				? SharedCellHeightInBlocks / 2 + FrontendSelection.TemplatePlacementZOffsetBlocks
				: 0;
		const bool bPassed =
			!bWorldFacingPlacement
			|| !bHasUsableLatticeHeight
			|| ((ConnectorRecord.PathOriginBlockWorldPos.Z - ExpectedCenterOffsetInBlocks) % SharedCellHeightInBlocks) == 0;

		return MakeConnectorPlanningAssertionRecord(
			TEXT("RegionRequest.WorldPlacementLatticeContractValid"),
			ELayoutValidationAssertionKind::RequestContractValid,
			bPassed,
			{ConnectorPlacementPolicyId, FrontendSelection.ContinuationFamilyId},
			bPassed
				? FString()
				: FString::Printf(
					TEXT("Continuation request '%s' uses path origin %s, which does not preserve the continuation cell-center offset over the resolved Z lattice for shared cell height %d. World-facing continuation requests must preserve the resolved shared-cell center offset before solve execution begins."),
					*RegionDebugPath,
					*ConnectorRecord.PathOriginBlockWorldPos.ToString(),
					SharedCellHeightInBlocks));
	}

	FIntVector ComputeSiteFootprintMinBlockWorldPos(const FResolvedLayoutSiteRecord& SiteRecord, const FIntVector& SharedCellSizeInBlocks)
	{
		const FResolvedLayoutSiteLocationMetadata LocationMetadata =
			SiteRecord.GetResolvedSiteLocationMetadata();
		if (LocationMetadata.RealizedFootprintMinBlockWorldPos != FIntVector::ZeroValue)
		{
			return LocationMetadata.RealizedFootprintMinBlockWorldPos;
		}

		const FResolvedLayoutSiteSolvedPayload SolvedPayload =
			SiteRecord.GetResolvedSiteSolvedPayload();
		const FIntVector FootprintSizeInBlocks(
			SolvedPayload.SolveResult.FootprintSize.X * SharedCellSizeInBlocks.X,
			SolvedPayload.SolveResult.FootprintSize.Y * SharedCellSizeInBlocks.Y,
			0);

		FIntVector MinBlockWorldPos = LocationMetadata.SiteCenterBlockWorldPos;
		MinBlockWorldPos.X -= FootprintSizeInBlocks.X / 2;
		MinBlockWorldPos.Y -= FootprintSizeInBlocks.Y / 2;
		return MinBlockWorldPos;
	}

	uint64 BuildConnectorPairKey(const FIntPoint& A, const FIntPoint& B)
	{
		const uint32 FirstHash = GetTypeHash(A);
		const uint32 SecondHash = GetTypeHash(B);
		return FirstHash < SecondHash
			? (static_cast<uint64>(FirstHash) << 32) | static_cast<uint64>(SecondHash)
			: (static_cast<uint64>(SecondHash) << 32) | static_cast<uint64>(FirstHash);
	}

	bool DoesEndpointMatchContinuationFamily(
		const FResolvedLayoutConnectorEndpoint& Endpoint,
		const FLayoutWorldBindingContinuationFamily& Family)
	{
		return Family.EndpointConnectorTypeTag.IsValid()
			&& Endpoint.ConnectorTypeTag == Family.EndpointConnectorTypeTag;
	}

	FIntPoint ToConnectorCell(const FIntVector& StartWorldPos, const FIntVector& CurrentWorldPos, const FIntVector& ConnectorCellSizeInBlocks)
	{
		return FIntPoint(
			FMath::RoundToInt(static_cast<double>(CurrentWorldPos.X - StartWorldPos.X) / static_cast<double>(FMath::Max(1, ConnectorCellSizeInBlocks.X))),
			FMath::RoundToInt(static_cast<double>(CurrentWorldPos.Y - StartWorldPos.Y) / static_cast<double>(FMath::Max(1, ConnectorCellSizeInBlocks.Y))));
	}

	FString BuildConnectionCountKey(const FIntPoint& SiteKey, const FGameplayTag& ConnectorTypeTag)
	{
		return FString::Printf(TEXT("%s|%s"), *SiteKey.ToString(), *ConnectorTypeTag.ToString());
	}

	bool ShouldDeriveConnectorTerrainSupport(
		const FLayoutContinuationCandidateSnapshot* const LayoutProfile,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy)
	{
		return PlacementKind != ELayoutWorldBindingPlacementKind::None
			&& LayoutProfile != nullptr
			&& (LayoutProfile->bSupportsSteppedTerrainSolve
				|| (TerrainTransitionPolicy.MaxFoundationDepth > 0
					&& (TerrainTransitionPolicy.bAllowFoundationFill
						|| TerrainTransitionPolicy.bAllowPerimeterRampTransition)));
	}

	void PublishRequestOwnedSteppedCarriersOnConnectorSolveResult(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		// Connector solves now depend on the scheduler's prepared stepped contract to
		// materialize request-owned carriers. Preserve those solved carriers here and
		// only backfill from the original request when the merged solve surface stayed empty.
		InOutScheduleResult.MergedSolveResult.SteppedTerrainSupportMap =
			SolveRequest.SteppedTerrainSupportMap;
		if (InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
		{
			InOutScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions =
				SolveRequest.ForcedPlacementBundleInsertions;
		}
		if (InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
		{
			InOutScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints =
				SolveRequest.RequiredRouteConstraints;
		}

		if (InOutScheduleResult.MergedSolveResult.PlannedCells.IsEmpty() && !SolveRequest.PlannedCells.IsEmpty())
		{
			InOutScheduleResult.MergedSolveResult.PlannedCells = SolveRequest.PlannedCells;
		}
	}

	int32 GetManhattanDistance(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Abs(A.X - B.X) + FMath::Abs(A.Y - B.Y);
	}

	ULayoutRegionContentSetAsset* ResolveConnectorPreferredContentSet(const ULayoutProfileAsset* const Profile)
	{
		return Profile != nullptr ? Profile->ContentSet.Get() : nullptr;
	}

	FName ResolveWorldBindingId(const ULayoutWorldBindingAsset* const WorldBinding)
	{
		if (WorldBinding == nullptr)
		{
			return NAME_None;
		}

		return !WorldBinding->BindingId.IsNone()
			? WorldBinding->BindingId
			: WorldBinding->GetFName();
	}

	FIntVector ResolveConnectorSharedCellSizeInBlocks(
		const FLayoutSolveResult& SolveResult,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		if (SolveResult.SharedCellSizeInBlocks != FIntVector::ZeroValue)
		{
			return SolveResult.SharedCellSizeInBlocks;
		}

		if (ContentSet != nullptr)
		{
			const FIntVector DerivedContentSetSharedCellSizeInBlocks =
				ContentSet->GetDerivedSharedCellSizeInBlocks();
			if (DerivedContentSetSharedCellSizeInBlocks != FIntVector::ZeroValue)
			{
				return DerivedContentSetSharedCellSizeInBlocks;
			}
		}

		return FIntVector::ZeroValue;
	}

	FIntVector ResolveConnectorSharedCellSizeInBlocks(
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		return ResolveConnectorSharedCellSizeInBlocks(FLayoutSolveResult(), ContentSet);
	}

	FIntVector ResolveConnectorBindingOwnedSharedCellSizeInBlocks(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		if (WorldBinding != nullptr
			&& WorldBinding->BaseCellDimensionsBlocks != FIntVector::ZeroValue)
		{
			return WorldBinding->BaseCellDimensionsBlocks;
		}

		return ResolveConnectorSharedCellSizeInBlocks(ContentSet);
	}

	FIntVector ResolveConnectorPlanningSharedCellSizeInBlocks(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const ULayoutWorldBindingAsset* const WorldBinding,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		if (FrontendSelection.SharedCellSizeInBlocks != FIntVector::ZeroValue)
		{
			return FrontendSelection.SharedCellSizeInBlocks;
		}

		return ResolveConnectorBindingOwnedSharedCellSizeInBlocks(
			WorldBinding,
			ContentSet);
	}

	TArray<FIntPoint> BuildFallbackManhattanPath(const FIntPoint& EndCell)
	{
		TArray<FIntPoint> PathCells;
		FIntPoint Cursor(0, 0);
		PathCells.Add(Cursor);
		while (Cursor.X != EndCell.X)
		{
			Cursor.X += EndCell.X > Cursor.X ? 1 : -1;
			PathCells.Add(Cursor);
		}
		while (Cursor.Y != EndCell.Y)
		{
			Cursor.Y += EndCell.Y > Cursor.Y ? 1 : -1;
			PathCells.Add(Cursor);
		}

		return PathCells;
	}

	bool CanEnterCell(
		const FIntPoint& From,
		const FIntPoint& To,
		const int32 FromConsecutiveGapCount,
		const FInternalConnectorPathQuery& Query,
		const TMap<FIntPoint, FInternalConnectorPathCell>& CellsByGrid,
		int32& OutConsecutiveGapCount,
		int32& OutMoveCost)
	{
		const FInternalConnectorPathCell* const FromCell = CellsByGrid.Find(From);
		const FInternalConnectorPathCell* const ToCell = CellsByGrid.Find(To);
		if (FromCell == nullptr || ToCell == nullptr || ToCell->Type == EInternalConnectorPathCellType::Blocked)
		{
			return false;
		}

		if (ToCell->Type == EInternalConnectorPathCellType::Gap)
		{
			OutConsecutiveGapCount = FromConsecutiveGapCount + 1;
			if (OutConsecutiveGapCount > Query.MaxBridgeGapCells)
			{
				return false;
			}

			OutMoveCost = 60;
			return true;
		}

		OutConsecutiveGapCount = 0;
		if (FromCell->Type == EInternalConnectorPathCellType::Walkable)
		{
			const int32 SurfaceDelta = FMath::Abs(ToCell->SurfaceZBlockWorld - FromCell->SurfaceZBlockWorld);
			if (SurfaceDelta > Query.MaxSlopeBlocks)
			{
				if (!Query.bAllowSteepTerrainTunneling)
				{
					return false;
				}

				OutMoveCost = 30 + SurfaceDelta * 2;
				return true;
			}

			OutMoveCost = 10 + SurfaceDelta * 3;
			return true;
		}

		OutMoveCost = 10;
		return true;
	}

	void NormalizePathCells(
		const TArray<FIntPoint>& RawPathCells,
		const int32 StructuralSolveLevel,
		TArray<FLayoutPlannedCell>& OutPlannedCells,
		FIntPoint& OutFootprintSize,
		FIntPoint& OutMinRawCell)
	{
		if (RawPathCells.IsEmpty())
		{
			OutFootprintSize = FIntPoint::ZeroValue;
			OutMinRawCell = FIntPoint::ZeroValue;
			return;
		}

		int32 MinX = RawPathCells[0].X;
		int32 MaxX = RawPathCells[0].X;
		int32 MinY = RawPathCells[0].Y;
		int32 MaxY = RawPathCells[0].Y;
		for (const FIntPoint& Cell : RawPathCells)
		{
			MinX = FMath::Min(MinX, Cell.X);
			MaxX = FMath::Max(MaxX, Cell.X);
			MinY = FMath::Min(MinY, Cell.Y);
			MaxY = FMath::Max(MaxY, Cell.Y);
		}

		OutMinRawCell = FIntPoint(MinX, MinY);
		OutPlannedCells.Reset();
		for (int32 Index = 0; Index < RawPathCells.Num(); ++Index)
		{
			const FIntPoint RawCell = RawPathCells[Index];
			FLayoutPlannedCell& PlannedCell = OutPlannedCells.AddDefaulted_GetRef();
			PlannedCell.Cell = FIntVector(RawCell.X - MinX, RawCell.Y - MinY, StructuralSolveLevel);
			PlannedCell.Intent = (Index == 0 || Index == RawPathCells.Num() - 1)
				? ELayoutCellIntent::Entry
				: ELayoutCellIntent::Connector;
		}

		OutFootprintSize = FIntPoint(MaxX - MinX + 1, MaxY - MinY + 1);
	}


		bool FindConnectorPathCellsPrivate(
			const FInternalConnectorPathQuery& Query,
			const TMap<FIntPoint, FInternalConnectorPathCell>& CellsByGrid,
			TArray<FIntPoint>& OutPathCells);

		bool ExpandContinuationCenterline(
			const TArray<FIntPoint>& CenterlineCells,
			int32 CorridorWidthInCells,
			int32 StructuralSolveLevel,
			TArray<FLayoutPlannedCell>& OutPlannedCells,
			FIntPoint& OutFootprintSize,
			FIntPoint& OutMinCell);

	ELayoutWorldBindingPlacementKind ResolveConnectorNormalizedPlacementKind(
		const FResolvedLayoutConnectorRecord& ConnectorRecord);


	int32 ResolveConnectorStructuralSolveLevel(const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const ELayoutWorldBindingPlacementKind NormalizedPlacementKind =
			ResolveConnectorNormalizedPlacementKind(ConnectorRecord);
		const bool bContinuationPlacement =
			NormalizedPlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
			|| NormalizedPlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
			|| NormalizedPlacementKind == ELayoutWorldBindingPlacementKind::TunnelContinuation;
		if (!bContinuationPlacement)
		{
			return 0;
		}

		// Thin cached connector carriers may preserve continuation deck intent only
		// on normalized continuation-facing fields instead of the legacy top-level one.
		const int32 ResolvedEntryLevel =
			ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel != INDEX_NONE
				? ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel
				: ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel;
		return FMath::Max(0, ResolvedEntryLevel);
	}

	ELayoutWorldBindingPlacementKind ResolveConnectorNormalizedPlacementKind(
		const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		if (FrontendSelection.PlacementKind != ELayoutWorldBindingPlacementKind::None)
		{
			return FrontendSelection.PlacementKind;
		}

		if (FrontendSelection.ResolvedContinuationSelection.PlacementKind
			!= ELayoutWorldBindingPlacementKind::None)
		{
			return FrontendSelection.ResolvedContinuationSelection.PlacementKind;
		}

		return ConnectorRecord.SolveResult.RootPlacementKind;
	}


		FIntVector RotateConnectorPlanningPlacementCellInFootprintYaw(
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

	TArray<FIntVector> ResolveConnectorPlanningPlacementOccupiedLocalCells(const FLayoutPlacedModule& Placement)
	{
		if (!Placement.OccupiedLocalCells.IsEmpty())
		{
			return Placement.OccupiedLocalCells;
		}

		if (Placement.CompositeModule != nullptr)
		{
			return Placement.CompositeModule->GetOccupiedLocalCells();
		}

		if (Placement.Module != nullptr)
		{
			return Placement.Module->GetOccupiedLocalCells();
		}

		return {};
	}

	FIntPoint ResolveConnectorPlanningPlacementRotationFootprint(const TArray<FIntVector>& OccupiedLocalCells)
	{
		if (OccupiedLocalCells.IsEmpty())
		{
			return FIntPoint(1, 1);
		}

		FIntVector MinCell = OccupiedLocalCells[0];
		FIntVector MaxCell = OccupiedLocalCells[0];
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			MinCell.X = FMath::Min(MinCell.X, LocalCell.X);
			MinCell.Y = FMath::Min(MinCell.Y, LocalCell.Y);
			MaxCell.X = FMath::Max(MaxCell.X, LocalCell.X);
			MaxCell.Y = FMath::Max(MaxCell.Y, LocalCell.Y);
		}

		return FIntPoint(
			FMath::Max(1, MaxCell.X - MinCell.X + 1),
			FMath::Max(1, MaxCell.Y - MinCell.Y + 1));
	}

	void AppendConnectorEndpointBoundaryStripColumns(
		const FLayoutSolveResult& SolveResult,
		const FIntVector& PathOriginBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntVector& EndpointBlockWorldPos,
		const ELayoutFaceDirection EndpointFacingDirection,
		TSet<FIntPoint>& InOutColumns)
	{
		if (SharedCellSizeInBlocks.X <= 0 || SharedCellSizeInBlocks.Y <= 0)
		{
			InOutColumns.Add(FIntPoint(EndpointBlockWorldPos.X, EndpointBlockWorldPos.Y));
			return;
		}

		const FIntVector EndpointLocalCell(
			(EndpointBlockWorldPos.X - PathOriginBlockWorldPos.X) / SharedCellSizeInBlocks.X,
			(EndpointBlockWorldPos.Y - PathOriginBlockWorldPos.Y) / SharedCellSizeInBlocks.Y,
			0);
		const bool bMatchX =
			EndpointFacingDirection == ELayoutFaceDirection::NegX
			|| EndpointFacingDirection == ELayoutFaceDirection::PosX;

		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			TArray<FIntVector> OccupiedLocalCells = ResolveConnectorPlanningPlacementOccupiedLocalCells(Placement);
			if (OccupiedLocalCells.IsEmpty())
			{
				OccupiedLocalCells = {FIntVector::ZeroValue};
			}

			const FIntPoint RotationFootprint =
				ResolveConnectorPlanningPlacementRotationFootprint(OccupiedLocalCells);
			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				const FIntVector RotatedLocalCell =
					RotateConnectorPlanningPlacementCellInFootprintYaw(
						OccupiedLocalCell,
						RotationFootprint,
						Placement.YawRotationSteps);
				const FIntVector RootLocalCell = Placement.Cell + RotatedLocalCell;
				const bool bMatchesEndpointBoundary =
					bMatchX
						? RootLocalCell.X == EndpointLocalCell.X
						: RootLocalCell.Y == EndpointLocalCell.Y;
				if (!bMatchesEndpointBoundary)
				{
					continue;
				}

				InOutColumns.Add(FIntPoint(
					PathOriginBlockWorldPos.X + RootLocalCell.X * SharedCellSizeInBlocks.X,
					PathOriginBlockWorldPos.Y + RootLocalCell.Y * SharedCellSizeInBlocks.Y));
			}
		}

		if (InOutColumns.IsEmpty())
		{
			InOutColumns.Add(FIntPoint(EndpointBlockWorldPos.X, EndpointBlockWorldPos.Y));
		}
	}

	void AddConnectorEndpointBoundaryPoint(
		const FIntVector& ConnectorLocalCell,
		const ELayoutFaceDirection ConnectorFacingDirection,
		const FString& SourceRegionDebugPath,
		const FLayoutId CommitmentId,
		TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints,
		const FLayoutFaceRule* const RootEntryFaceRule = nullptr)
	{
		FLayoutSolveBoundaryPoint& BoundaryPoint = OutBoundaryPoints.AddDefaulted_GetRef();
		BoundaryPoint.LocalCell = ConnectorLocalCell + FLayoutDirectionUtils::ToCellDelta(ConnectorFacingDirection);
		BoundaryPoint.FaceDirection = FLayoutDirectionUtils::GetOpposite(ConnectorFacingDirection);
		if (RootEntryFaceRule != nullptr)
		{
			BoundaryPoint.ConnectionTag = RootEntryFaceRule->GetEffectiveConnectionTag();
			BoundaryPoint.ConnectedTraversalChannels = RootEntryFaceRule->ConnectedTraversalChannels;
			BoundaryPoint.AllowedConnectionTags = RootEntryFaceRule->GetEffectiveAllowedConnectionTags();
		}
		else
		{
			BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceEntry;
			BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		}
		// Connector endpoints join two solved regions; face tags enforce this contract without exterior-only admission.
		BoundaryPoint.bRequiresBoundaryFacing = false;
		BoundaryPoint.bRepresentsFilledNeighbor = false;
		BoundaryPoint.SourceRegionDebugPath = SourceRegionDebugPath;
		BoundaryPoint.SourceCell = BoundaryPoint.LocalCell;
		BoundaryPoint.CommitmentId = CommitmentId;
	}

	void AddConnectorEndpointCommitment(
		const FIntVector& ConnectorLocalCell,
		const ELayoutFaceDirection ConnectorFacingDirection,
		const int32 RequiredWorldCenterBlockZ,
		const FLayoutId CommitmentId,
		TArray<FLayoutCommittedEndpointAnchor>& OutAnchors,
		const FLayoutFaceRule* const RootEntryFaceRule = nullptr)
	{
		FLayoutCommittedEndpointAnchor& Anchor = OutAnchors.AddDefaulted_GetRef();
		Anchor.CommitmentId = CommitmentId;
		Anchor.LocalCell = ConnectorLocalCell;
		Anchor.FaceDirection = ConnectorFacingDirection;
		Anchor.RequiredWorldCenterBlockZ = RequiredWorldCenterBlockZ;
		if (RootEntryFaceRule != nullptr)
		{
			Anchor.ConnectionTag = RootEntryFaceRule->GetEffectiveConnectionTag();
			Anchor.AllowedConnectionTags = RootEntryFaceRule->GetEffectiveAllowedConnectionTags();
			Anchor.TraversalChannels = RootEntryFaceRule->ConnectedTraversalChannels;
		}
		else
		{
			Anchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
			Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);
			Anchor.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
			Anchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		}
		Anchor.bRequireMatchingYawWithFilledNeighbor = false;
	}

	void AddConnectorVerticalSupportBoundaryPoint(
		const FIntVector& SupportedLocalCell,
		const FString& SourceRegionDebugPath,
		TArray<FLayoutSolveBoundaryPoint>& OutBoundaryPoints)
	{
		FLayoutSolveBoundaryPoint& BoundaryPoint = OutBoundaryPoints.AddDefaulted_GetRef();
		BoundaryPoint.LocalCell = SupportedLocalCell + FLayoutDirectionUtils::ToCellDelta(ELayoutFaceDirection::NegZ);
		BoundaryPoint.FaceDirection = ELayoutFaceDirection::PosZ;
		BoundaryPoint.ConnectionTag = LayoutGameplayTags::FaceOpen;
		BoundaryPoint.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		BoundaryPoint.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		BoundaryPoint.bRepresentsFilledNeighbor = true;
		BoundaryPoint.SourceRegionDebugPath = SourceRegionDebugPath;
		BoundaryPoint.SourceCell = BoundaryPoint.LocalCell;
	}

	struct FConnectorEndpointRequestContracts
	{
		TArray<FLayoutSolveBoundaryPoint> IncomingBoundaryPoints;
		TArray<FLayoutCommittedEndpointAnchor> CommittedEndpointAnchors;
		bool bStartSupportsCommittedEndpoint = false;
		bool bEndSupportsCommittedEndpoint = false;
	};

	bool CanModuleSupportCommittedConnectorEndpoint(
		const FLayoutModuleSolveSnapshot& ModuleSnapshot,
		const ELayoutFaceDirection ExternalFaceDirection,
		const ELayoutFaceDirection ContinuationFaceDirection)
	{
		if (!ModuleSnapshot.SupportsIntent(ELayoutCellIntent::Entry))
		{
			return false;
		}

		// Solver rotates leaf modules. Preflight must validate those legal world-facing
		// orientations too, or it rejects valid road endpoints before solve.
		for (const int32 YawRotationSteps : ModuleSnapshot.AllowedYawRotationSteps)
		{
			const FLayoutFaceRule* const ExternalFaceRule = ModuleSnapshot.EffectiveFaceRules.FindRule(
				FLayoutDirectionUtils::RotateYaw(ExternalFaceDirection, 4 - YawRotationSteps));
			const FLayoutFaceRule* const ContinuationFaceRule = ModuleSnapshot.EffectiveFaceRules.FindRule(
				FLayoutDirectionUtils::RotateYaw(ContinuationFaceDirection, 4 - YawRotationSteps));
			if (ExternalFaceRule == nullptr || ContinuationFaceRule == nullptr)
			{
				continue;
			}

			const bool bExternalFaceSupportsCommittedEntry =
				ExternalFaceRule->GetEffectiveConnectionTags().HasTagExact(LayoutGameplayTags::FaceEntry)
				&& ExternalFaceRule->ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary);
			const bool bContinuationSupportsInteriorTraversal =
				ContinuationFaceRule->GetEffectiveConnectionTag().IsValid()
				&& ContinuationFaceRule->ConnectedTraversalChannels.HasTagExact(LayoutGameplayTags::TraversalPrimary)
				&& ContinuationFaceRule->OccupancyPolicy != ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
			if (bExternalFaceSupportsCommittedEntry && bContinuationSupportsInteriorTraversal)
			{
				return true;
			}
		}
		return false;
	}

	bool CanRequestSupportCommittedConnectorEndpoint(
		const FLayoutRegionSolveRequest& SolveRequest,
		const ELayoutFaceDirection ExternalFaceDirection,
		const ELayoutFaceDirection ContinuationFaceDirection)
	{
		for (const FLayoutModuleSolveSnapshot& ModuleSnapshot : SolveRequest.ModuleCatalog.Modules)
		{
			if (CanModuleSupportCommittedConnectorEndpoint(
				ModuleSnapshot,
				ExternalFaceDirection,
				ContinuationFaceDirection))
			{
				return true;
			}
		}

		return false;
	}

	FConnectorEndpointRequestContracts BuildConnectorEndpointRequestContracts(
		const FLayoutRegionSolveRequest& SolveRequest,
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const TArray<FIntPoint>& RawPathCells,
		const FIntPoint& MinRawPathCell,
		const FString& RegionDebugPath,
		const FLayoutFaceRule* const StartRootEntryFaceRule = nullptr,
		const FLayoutFaceRule* const EndRootEntryFaceRule = nullptr)
	{
		FConnectorEndpointRequestContracts Contracts;
		if (RawPathCells.Num() < 2)
		{
			return Contracts;
		}

		if (!ConnectorRecord.bHasResolvedEndpointFacingDirections)
		{
			return Contracts;
		}
		const ELayoutFaceDirection StartConnectorFacingDirection =
			ConnectorRecord.StartEndpointFacingDirection;
		const ELayoutFaceDirection EndConnectorFacingDirection =
			ConnectorRecord.EndEndpointFacingDirection;

		const int32 StructuralSolveLevel = ResolveConnectorStructuralSolveLevel(ConnectorRecord);
		const FIntVector StartLocalCell(RawPathCells[0].X - MinRawPathCell.X, RawPathCells[0].Y - MinRawPathCell.Y, 0);
		const FIntVector StartLevelLocalCell(StartLocalCell.X, StartLocalCell.Y, StructuralSolveLevel);
		const ELayoutFaceDirection StartPathDirection = FLayoutDirectionUtils::GetOpposite(StartConnectorFacingDirection);
		Contracts.bStartSupportsCommittedEndpoint =
			CanRequestSupportCommittedConnectorEndpoint(
				SolveRequest,
				StartConnectorFacingDirection,
				StartPathDirection);
		const FLayoutId StartCommitmentId(*FString::Printf(TEXT("%s.RootEndpoint.Start"), *RegionDebugPath));
		AddConnectorEndpointBoundaryPoint(
			StartLevelLocalCell,
			StartConnectorFacingDirection,
			RegionDebugPath,
			Contracts.bStartSupportsCommittedEndpoint ? StartCommitmentId : NAME_None,
			Contracts.IncomingBoundaryPoints,
			StartRootEntryFaceRule);
		if (Contracts.bStartSupportsCommittedEndpoint)
		{
			AddConnectorEndpointCommitment(
				StartLevelLocalCell,
				StartConnectorFacingDirection,
				ConnectorRecord.StartEndpointBlockWorldPos.Z,
				StartCommitmentId,
				Contracts.CommittedEndpointAnchors,
				StartRootEntryFaceRule);
		}

		const int32 EndIndex = RawPathCells.Num() - 1;
		const FIntVector EndLocalCell(RawPathCells[EndIndex].X - MinRawPathCell.X, RawPathCells[EndIndex].Y - MinRawPathCell.Y, 0);
		const FIntVector EndLevelLocalCell(EndLocalCell.X, EndLocalCell.Y, StructuralSolveLevel);
		const ELayoutFaceDirection EndPathDirection = EndConnectorFacingDirection;
		Contracts.bEndSupportsCommittedEndpoint =
			CanRequestSupportCommittedConnectorEndpoint(
				SolveRequest,
				EndPathDirection,
				FLayoutDirectionUtils::GetOpposite(EndPathDirection));
		const FLayoutId EndCommitmentId(*FString::Printf(TEXT("%s.RootEndpoint.End"), *RegionDebugPath));
		AddConnectorEndpointBoundaryPoint(
			EndLevelLocalCell,
			EndPathDirection,
			RegionDebugPath,
			Contracts.bEndSupportsCommittedEndpoint ? EndCommitmentId : NAME_None,
			Contracts.IncomingBoundaryPoints,
			EndRootEntryFaceRule);
		if (Contracts.bEndSupportsCommittedEndpoint)
		{
			AddConnectorEndpointCommitment(
				EndLevelLocalCell,
				EndPathDirection,
				ConnectorRecord.EndEndpointBlockWorldPos.Z,
				EndCommitmentId,
				Contracts.CommittedEndpointAnchors,
				EndRootEntryFaceRule);
		}

		if (StructuralSolveLevel > 0)
		{
			for (const FIntPoint& RawPathCell : RawPathCells)
			{
				AddConnectorVerticalSupportBoundaryPoint(
					FIntVector(
						RawPathCell.X - MinRawPathCell.X,
						RawPathCell.Y - MinRawPathCell.Y,
						StructuralSolveLevel),
					RegionDebugPath,
					Contracts.IncomingBoundaryPoints);
			}
		}

		return Contracts;
	}

	bool BuildTerrainPathCells(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FIntPoint& EndCell,
		const FIntVector& ConnectorCellSizeInBlocks,
		const FConnectorPathPlanningSettings& PathSettings,
		const FLayoutConnectorTerrainPathContext* TerrainPathContext,
		TArray<FIntPoint>& OutPathCells,
		TMap<FIntPoint, FInternalConnectorPathCell>* const OutCellsByGrid = nullptr)
	{
		if (TerrainPathContext == nullptr || TerrainPathContext->ActiveBiomeSampler == nullptr)
		{
			return false;
		}

		FInternalConnectorPathQuery Query;
		Query.StartCell = FIntPoint::ZeroValue;
		Query.EndCell = EndCell;
		Query.MinCell = FIntPoint(
			FMath::Min(0, EndCell.X) - FMath::Max(0, PathSettings.PathPaddingCells),
			FMath::Min(0, EndCell.Y) - FMath::Max(0, PathSettings.PathPaddingCells));
		Query.MaxCell = FIntPoint(
			FMath::Max(0, EndCell.X) + FMath::Max(0, PathSettings.PathPaddingCells),
			FMath::Max(0, EndCell.Y) + FMath::Max(0, PathSettings.PathPaddingCells));
		Query.MaxSlopeBlocks = FMath::Max(0, PathSettings.MaxSlopeBlocks);
		Query.MaxBridgeGapCells = FMath::Max(0, PathSettings.MaxBridgeGapCells);
		Query.bAllowSteepTerrainTunneling = PathSettings.bAllowSteepTerrainTunneling;

		TMap<FIntPoint, FInternalConnectorPathCell> CellsByGrid;
		for (int32 Y = Query.MinCell.Y; Y <= Query.MaxCell.Y; ++Y)
		{
			for (int32 X = Query.MinCell.X; X <= Query.MaxCell.X; ++X)
			{
				const FIntPoint Cell(X, Y);
				const FIntPoint BlockXY(
					ConnectorRecord.StartEndpointBlockWorldPos.X + X * ConnectorCellSizeInBlocks.X,
					ConnectorRecord.StartEndpointBlockWorldPos.Y + Y * ConnectorCellSizeInBlocks.Y);

				FLayoutActiveBiomeSurfaceSample Surface;
				const bool bSearchCompleted = !PathSettings.PathBiomeRowNames.IsEmpty()
					? (PathSettings.PathBiomeRowNames.Num() == 1
						? TerrainPathContext->ActiveBiomeSampler->FindEligibleBiomeSurface(
							PathSettings.PathBiomeRowNames[0],
							BlockXY,
							PathSettings.TerrainSearchStartZ,
							PathSettings.TerrainSearchDepthBlocks,
							TerrainPathContext->CoordinateSettings,
							Surface)
						: TerrainPathContext->ActiveBiomeSampler->FindEligibleBiomeSurfaceFromAnyRow(
							PathSettings.PathBiomeRowNames,
							BlockXY,
							PathSettings.TerrainSearchStartZ,
							PathSettings.TerrainSearchDepthBlocks,
							TerrainPathContext->CoordinateSettings,
							Surface))
					: (PathSettings.PathBiomeRowName.IsNone()
						? TerrainPathContext->ActiveBiomeSampler->FindAnyActiveBiomeSurface(
							BlockXY,
							PathSettings.TerrainSearchStartZ,
							PathSettings.TerrainSearchDepthBlocks,
							TerrainPathContext->CoordinateSettings,
							Surface)
						: TerrainPathContext->ActiveBiomeSampler->FindEligibleBiomeSurface(
							PathSettings.PathBiomeRowName,
							BlockXY,
							PathSettings.TerrainSearchStartZ,
							PathSettings.TerrainSearchDepthBlocks,
							TerrainPathContext->CoordinateSettings,
							Surface));

					FInternalConnectorPathCell& PathCell = CellsByGrid.Add(Cell);
					if (bSearchCompleted && Surface.bIsValid)
					{
						PathCell.Type = EInternalConnectorPathCellType::Walkable;
						PathCell.SurfaceZBlockWorld = Surface.SurfaceBlockWorldPos.Z;
					}
					else
					{
						PathCell.Type = PathSettings.MaxBridgeGapCells > 0
							? EInternalConnectorPathCellType::Gap
							: EInternalConnectorPathCellType::Blocked;
					}
				}
			}

			if (FInternalConnectorPathCell* const StartPathCell = CellsByGrid.Find(Query.StartCell))
			{
				StartPathCell->Type = EInternalConnectorPathCellType::Walkable;
				StartPathCell->SurfaceZBlockWorld = ConnectorRecord.StartEndpointBlockWorldPos.Z;
			}
			if (FInternalConnectorPathCell* const EndPathCell = CellsByGrid.Find(Query.EndCell))
			{
				EndPathCell->Type = EInternalConnectorPathCellType::Walkable;
				EndPathCell->SurfaceZBlockWorld = ConnectorRecord.EndEndpointBlockWorldPos.Z;
			}

		if (OutCellsByGrid != nullptr)
		{
			*OutCellsByGrid = CellsByGrid;
		}

			return FindConnectorPathCellsPrivate(Query, CellsByGrid, OutPathCells);
		}

		bool TryBuildConnectorSteppedTerrainSupportMap(
			const FIntVector& AnchorBlockWorldPos,
			const FIntVector& SharedCellSizeInBlocks,
			const FIntPoint& MinPlannedCell,
			const TMap<FIntPoint, FInternalConnectorPathCell>& CellsByGrid,
			const TArray<FLayoutPlannedCell>& PlannedCells,
			FLayoutSteppedTerrainSupportMap& OutSupportMap,
			FString& OutFailureReason)
	{
		TMap<FIntPoint, int32> SurfaceZByBlockXY;
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			const FIntPoint RawCell(
				PlannedCell.Cell.X + MinPlannedCell.X,
				PlannedCell.Cell.Y + MinPlannedCell.Y);
			const FInternalConnectorPathCell* const PathCell = CellsByGrid.Find(RawCell);
			if (PathCell == nullptr || PathCell->Type != EInternalConnectorPathCellType::Walkable)
			{
				continue;
			}

			const FIntPoint BlockXY(
				AnchorBlockWorldPos.X + PlannedCell.Cell.X * SharedCellSizeInBlocks.X,
				AnchorBlockWorldPos.Y + PlannedCell.Cell.Y * SharedCellSizeInBlocks.Y);
			SurfaceZByBlockXY.Add(BlockXY, PathCell->SurfaceZBlockWorld);
		}

		return FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
			AnchorBlockWorldPos,
			SharedCellSizeInBlocks,
			SharedCellSizeInBlocks.X,
			PlannedCells,
			SurfaceZByBlockXY,
			OutSupportMap,
			OutFailureReason);
	}

		FConnectorPathPlanningSettings BuildConnectorPathPlanningSettingsFromFrontendSelection(
			const FLayoutResolvedConnectorFrontendSelection& FrontendSelection)
		{
		FConnectorPathPlanningSettings Settings;
		Settings.bEnableTerrainAwarePathing =
			FrontendSelection.TerrainPathSelection.bEnableTerrainAwarePathing;
		Settings.PathBiomeRowName =
			FrontendSelection.TerrainPathSelection.PathBiomeRowName;
		Settings.PathBiomeRowNames =
			FrontendSelection.TerrainPathSelection.PathBiomeRowNames;
		Settings.TerrainSearchStartZ =
			FrontendSelection.WorldBindingPlacementPolicy.SurfaceSearch
				.TerrainSearchStartZ;
		Settings.TerrainSearchDepthBlocks =
			FrontendSelection.WorldBindingPlacementPolicy.SurfaceSearch
				.TerrainSearchDepthBlocks;
		Settings.PathPaddingCells =
			FrontendSelection.ContinuationPolicy.PathPaddingCells;
		Settings.MaxSlopeBlocks =
			FrontendSelection.ContinuationPolicy.MaxSlopeBlocks;
		Settings.MaxBridgeGapCells =
			FrontendSelection.PlacementKind
				== ELayoutWorldBindingPlacementKind::BridgeContinuation
			? FrontendSelection.ContinuationPolicy.MaxBridgeGapCells
			: 0;
		Settings.bAllowSteepTerrainTunneling =
			FrontendSelection.PlacementKind
				== ELayoutWorldBindingPlacementKind::TunnelContinuation;
		return Settings;
	}

	bool TryBuildContinuationCenterlineFromFrozenTerrain(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainEvidence,
		const int32 CorridorWidth,
		const bool bRequireAdjacentTerrainStages,
		const TConstArrayView<FLayoutRootSpacingReservation> RootFootprints,
		TArray<FIntPoint>& OutCenterlineCells,
		TMap<FIntPoint, FInternalConnectorPathCell>& OutCellsByGrid,
		FString& OutFailureReason)
	{
		OutCenterlineCells.Reset();
		OutCellsByGrid.Reset();
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		const FIntVector CellSize = FrontendSelection.SharedCellSizeInBlocks;
		const FIntVector RouteGridOrigin = GetConnectorRouteGridOrigin(ConnectorRecord, CellSize);
		if (CellSize.X <= 0 || CellSize.Y <= 0)
		{
			OutFailureReason = TEXT("Prepared continuation requires positive shared cell dimensions.");
			return false;
		}
		if (CorridorWidth <= 0 || CorridorWidth % 2 == 0)
		{
			OutFailureReason = TEXT("Continuation profile minimum footprint width must be positive and odd.");
			return false;
		}

		const FConnectorPathPlanningSettings PathSettings =
			BuildConnectorPathPlanningSettingsFromFrontendSelection(FrontendSelection);
		const FIntPoint EndRootCell = ToConnectorCell(
			ConnectorRecord.StartEndpointBlockWorldPos,
			ConnectorRecord.EndEndpointBlockWorldPos,
			CellSize);
		FConnectorRoutePortals Portals;
		if (!TryBuildConnectorRoutePortals(ConnectorRecord, CellSize, Portals))
		{
			OutFailureReason = TEXT("Continuation endpoints require horizontal cardinal entry faces.");
			return false;
		}
		const int32 Padding = FMath::Max(0, PathSettings.PathPaddingCells) + (CorridorWidth - 1) / 2;
		FInternalConnectorPathQuery Query;
		Query.MinCell = FIntPoint(
			FMath::Min(FMath::Min(FMath::Min(0, EndRootCell.X), Portals.StartPortalCell.X), FMath::Min(Portals.StartStubCell.X, FMath::Min(Portals.EndPortalCell.X, Portals.EndStubCell.X))) - Padding,
			FMath::Min(FMath::Min(FMath::Min(0, EndRootCell.Y), Portals.StartPortalCell.Y), FMath::Min(Portals.StartStubCell.Y, FMath::Min(Portals.EndPortalCell.Y, Portals.EndStubCell.Y))) - Padding);
		Query.MaxCell = FIntPoint(
			FMath::Max(FMath::Max(FMath::Max(0, EndRootCell.X), Portals.StartPortalCell.X), FMath::Max(Portals.StartStubCell.X, FMath::Max(Portals.EndPortalCell.X, Portals.EndStubCell.X))) + Padding,
			FMath::Max(FMath::Max(FMath::Max(0, EndRootCell.Y), Portals.StartPortalCell.Y), FMath::Max(Portals.StartStubCell.Y, FMath::Max(Portals.EndPortalCell.Y, Portals.EndStubCell.Y))) + Padding);
		Query.StartCell = Portals.StartStubCell;
		Query.EndCell = Portals.EndStubCell;
		Query.MaxSlopeBlocks = FMath::Max(0, PathSettings.MaxSlopeBlocks);
		Query.MaxBridgeGapCells = FMath::Max(0, PathSettings.MaxBridgeGapCells);
		Query.bAllowSteepTerrainTunneling = PathSettings.bAllowSteepTerrainTunneling;

		TMap<FIntPoint, FInternalConnectorPathCell> CellsByGrid;
		for (int32 Y = Query.MinCell.Y; Y <= Query.MaxCell.Y; ++Y)
		{
			for (int32 X = Query.MinCell.X; X <= Query.MaxCell.X; ++X)
			{
				const FIntPoint GridCell(X, Y);
				const FIntPoint BlockXY(
					RouteGridOrigin.X + X * CellSize.X,
					RouteGridOrigin.Y + Y * CellSize.Y);
				FInternalConnectorPathCell& PathCell = CellsByGrid.Add(GridCell);
				PathCell.Type = PathSettings.MaxBridgeGapCells > 0
					? EInternalConnectorPathCellType::Gap
					: EInternalConnectorPathCellType::Blocked;
				for (const FLayoutTerrainSurfaceSample& Sample : TerrainEvidence.SurfaceSamples)
				{
					if (Sample.BlockXY == BlockXY && Sample.bIsValid)
					{
						PathCell.Type = EInternalConnectorPathCellType::Walkable;
						PathCell.SurfaceZBlockWorld = Sample.SurfaceBlockWorldPos.Z;
						break;
					}
				}
			}
		}
		// Keep physical occupancy separate from centerline clearance: the widened-corridor
		// check below applies width once, including turns and fixed endpoint strips.
		TSet<FIntPoint> RootBlockedCells;
		for (TPair<FIntPoint, FInternalConnectorPathCell>& Pair : CellsByGrid)
		{
			const int64 CellMinX = int64(RouteGridOrigin.X)
				+ int64(Pair.Key.X) * CellSize.X;
			const int64 CellMinY = int64(RouteGridOrigin.Y)
				+ int64(Pair.Key.Y) * CellSize.Y;
			const int64 CellMaxX = int64(RouteGridOrigin.X)
				+ (int64(Pair.Key.X) + 1) * CellSize.X - 1;
			const int64 CellMaxY = int64(RouteGridOrigin.Y)
				+ (int64(Pair.Key.Y) + 1) * CellSize.Y - 1;
			for (const FLayoutRootSpacingReservation& RootFootprint : RootFootprints)
			{
				if (CellMinX <= RootFootprint.Max.X && CellMaxX >= RootFootprint.Min.X
					&& CellMinY <= RootFootprint.Max.Y && CellMaxY >= RootFootprint.Min.Y)
				{
					Pair.Value.Type = EInternalConnectorPathCellType::Blocked;
					RootBlockedCells.Add(Pair.Key);
					break;
				}
			}
		}

		if (FInternalConnectorPathCell* StartPortalCell = CellsByGrid.Find(Portals.StartPortalCell))
		{
			StartPortalCell->Type = EInternalConnectorPathCellType::Walkable;
			StartPortalCell->SurfaceZBlockWorld = ConnectorRecord.StartEndpointBlockWorldPos.Z;
		}
		if (FInternalConnectorPathCell* EndPortalCell = CellsByGrid.Find(Portals.EndPortalCell))
		{
			EndPortalCell->Type = EInternalConnectorPathCellType::Walkable;
			EndPortalCell->SurfaceZBlockWorld = ConnectorRecord.EndEndpointBlockWorldPos.Z;
		}
		auto DescribeCell = [&CellsByGrid, &RootBlockedCells, RootFootprints, RouteGridOrigin, CellSize](const FIntPoint Cell)
		{
			const FInternalConnectorPathCell* Value = CellsByGrid.Find(Cell);
			FString RootBounds;
			if (RootBlockedCells.Contains(Cell))
			{
				const int64 X = int64(RouteGridOrigin.X) + int64(Cell.X) * CellSize.X;
				const int64 Y = int64(RouteGridOrigin.Y) + int64(Cell.Y) * CellSize.Y;
				for (const auto& Root : RootFootprints)
					if (X <= Root.Max.X && X + CellSize.X - 1 >= Root.Min.X
						&& Y <= Root.Max.Y && Y + CellSize.Y - 1 >= Root.Min.Y)
					{
						RootBounds = FString::Printf(TEXT(" binding=%s rootBounds=[%s..%s]"),
							*Root.BindingId.ToString(), *Root.Min.ToString(), *Root.Max.ToString());
						break;
					}
			}
			return FString::Printf(TEXT("grid=(%d,%d) blockXY=(%lld,%lld) type=%d surfaceZ=%s obstacle=%s%s"),
				Cell.X, Cell.Y, int64(RouteGridOrigin.X) + int64(Cell.X) * CellSize.X,
				int64(RouteGridOrigin.Y) + int64(Cell.Y) * CellSize.Y,
				Value ? int32(Value->Type) : -1,
				Value ? *LexToString(Value->SurfaceZBlockWorld) : TEXT("missing"),
				RootBlockedCells.Contains(Cell) ? TEXT("root layout footprint") : TEXT("none"), *RootBounds);
		};
		int32 UnusedGapCount = 0;
		int32 UnusedMoveCost = 0;
		if (!CanEnterCell(
				Portals.StartPortalCell,
				Portals.StartStubCell,
				0,
				Query,
				CellsByGrid,
				UnusedGapCount,
				UnusedMoveCost)
			|| !CanEnterCell(
				Portals.EndStubCell,
				Portals.EndPortalCell,
				0,
				Query,
				CellsByGrid,
				UnusedGapCount,
				UnusedMoveCost))
		{
			OutFailureReason = FString::Printf(
				TEXT("Prepared continuation cannot satisfy forced endpoint stubs startPortal=(%d,%d) endPortal=(%d,%d). startStub={%s} endStub={%s} endpointZ=%d/%d maxSlope=%d roots=%d"),
				Portals.StartPortalCell.X,
				Portals.StartPortalCell.Y,
				Portals.EndPortalCell.X,
				Portals.EndPortalCell.Y,
				*DescribeCell(Portals.StartStubCell), *DescribeCell(Portals.EndStubCell),
				ConnectorRecord.StartEndpointBlockWorldPos.Z, ConnectorRecord.EndEndpointBlockWorldPos.Z,
				Query.MaxSlopeBlocks, RootFootprints.Num());
			return false;
		}
		TMap<FIntPoint, FInternalConnectorPathCell> InteriorCellsByGrid = CellsByGrid;
		if (Portals.StartPortalCell != Query.StartCell && Portals.StartPortalCell != Query.EndCell)
		{
			InteriorCellsByGrid.FindChecked(Portals.StartPortalCell).Type = EInternalConnectorPathCellType::Blocked;
		}
		if (Portals.EndPortalCell != Query.StartCell && Portals.EndPortalCell != Query.EndCell)
		{
			InteriorCellsByGrid.FindChecked(Portals.EndPortalCell).Type = EInternalConnectorPathCellType::Blocked;
		}
		FString CorridorRejection;
		auto IsExpandedCorridorStageCompatible = [&CellsByGrid, &RootBlockedCells, &DescribeCell, CellSize, CorridorWidth, bRequireAdjacentTerrainStages, &CorridorRejection](
			const TArray<FIntPoint>& Centerline,
			FIntPoint& OutRejectedRouteCell)
		{
			TArray<FLayoutPlannedCell> CorridorCells;
			FIntPoint IgnoredFootprint;
			FIntPoint CorridorMinCell;
			if (!ExpandContinuationCenterline(
					Centerline,
					CorridorWidth,
					0,
					CorridorCells,
					IgnoredFootprint,
					CorridorMinCell))
			{
				return false;
			}
			auto FindNearestRouteCell = [&Centerline](const FIntPoint& Cell)
			{
				FIntPoint Nearest = Centerline[0];
				int32 NearestDistance = FMath::Abs(Nearest.X - Cell.X) + FMath::Abs(Nearest.Y - Cell.Y);
				for (const FIntPoint& Candidate : Centerline)
				{
					const int32 Distance = FMath::Abs(Candidate.X - Cell.X) + FMath::Abs(Candidate.Y - Cell.Y);
					if (Distance < NearestDistance)
					{
						Nearest = Candidate;
						NearestDistance = Distance;
					}
				}
				return Nearest;
			};
			TSet<FIntPoint> CorridorGridCells;
			for (const FLayoutPlannedCell& CorridorCell : CorridorCells)
			{
				const FIntPoint GridCell = FIntPoint(CorridorCell.Cell.X, CorridorCell.Cell.Y) + CorridorMinCell;
				const FInternalConnectorPathCell* const PathCell = CellsByGrid.Find(GridCell);
				if (RootBlockedCells.Contains(GridCell))
				{
					CorridorRejection = FString::Printf(TEXT("widened-cell intersects root layout footprint: %s"), *DescribeCell(GridCell));
					OutRejectedRouteCell = FindNearestRouteCell(GridCell);
					return false;
				}
				if (bRequireAdjacentTerrainStages && (PathCell == nullptr || PathCell->Type != EInternalConnectorPathCellType::Walkable))
				{
					CorridorRejection = FString::Printf(TEXT("widened-cell unavailable: %s"), *DescribeCell(GridCell));
					OutRejectedRouteCell = FindNearestRouteCell(GridCell);
					return false;
				}
				CorridorGridCells.Add(GridCell);
			}
			if (!bRequireAdjacentTerrainStages) return true;
			for (const FIntPoint& GridCell : CorridorGridCells)
			{
				const FInternalConnectorPathCell& PathCell = CellsByGrid.FindChecked(GridCell);
				for (const FIntPoint& Delta : {
					FIntPoint(1, 0), FIntPoint(0, 1),
					FIntPoint(1, 1), FIntPoint(1, -1) })
				{
					const FIntPoint NeighborCell = GridCell + Delta;
					if (!CorridorGridCells.Contains(NeighborCell))
					{
						continue;
					}
					const FInternalConnectorPathCell& Neighbor = CellsByGrid.FindChecked(NeighborCell);
					const int32 StageDelta = FMath::Abs(
						FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(
							CellSize.Z,
							PathCell.SurfaceZBlockWorld)
						- FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(
							CellSize.Z,
							Neighbor.SurfaceZBlockWorld)) / CellSize.Z;
					if (StageDelta > 1)
					{
						CorridorRejection = FString::Printf(TEXT("stage-delta cell={%s} neighbor={%s} snappedZ=%d/%d delta=%d cellZ=%d"),
							*DescribeCell(GridCell), *DescribeCell(NeighborCell),
							FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(CellSize.Z, PathCell.SurfaceZBlockWorld),
							FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(CellSize.Z, Neighbor.SurfaceZBlockWorld), StageDelta, CellSize.Z);
						OutRejectedRouteCell = FindNearestRouteCell(GridCell);
						return false;
					}
				}
			}
			return true;
		};

		const int32 MaxPathAttempts = InteriorCellsByGrid.Num();
		for (int32 AttemptIndex = 0; AttemptIndex < MaxPathAttempts; ++AttemptIndex)
		{
			TArray<FIntPoint> InteriorPathCells;
			if (!FindConnectorPathCellsPrivate(Query, InteriorCellsByGrid, InteriorPathCells))
			{
				if (CorridorRejection.IsEmpty()) CorridorRejection = FString::Printf(
					TEXT("A* exhausted before widened-corridor validation startStub={%s} endStub={%s}"),
					*DescribeCell(Portals.StartStubCell), *DescribeCell(Portals.EndStubCell));
				break;
			}
			TArray<FIntPoint> CandidateCenterline;
			CandidateCenterline.Add(Portals.StartPortalCell);
			CandidateCenterline.Append(InteriorPathCells);
			CandidateCenterline.Add(Portals.EndPortalCell);
			FIntPoint RejectedRouteCell = FIntPoint::ZeroValue;
			if (IsExpandedCorridorStageCompatible(CandidateCenterline, RejectedRouteCell))
			{
				OutCenterlineCells = MoveTemp(CandidateCenterline);
				OutCellsByGrid = MoveTemp(CellsByGrid);
				return true;
			}
			if (RejectedRouteCell == Portals.StartPortalCell
				|| RejectedRouteCell == Portals.EndPortalCell
				|| RejectedRouteCell == Portals.StartStubCell
				|| RejectedRouteCell == Portals.EndStubCell)
			{
				break;
			}
			FInternalConnectorPathCell* const RejectedPathCell = InteriorCellsByGrid.Find(RejectedRouteCell);
			if (RejectedPathCell == nullptr || RejectedPathCell->Type == EInternalConnectorPathCellType::Blocked)
			{
				break;
			}
			RejectedPathCell->Type = EInternalConnectorPathCellType::Blocked;
		}

		int32 ValidSamples = 0;
		int32 MinSurfaceZ = MAX_int32, MaxSurfaceZ = MIN_int32;
		for (const FLayoutTerrainSurfaceSample& Sample : TerrainEvidence.SurfaceSamples)
		{
			if (!Sample.bIsValid) continue;
			++ValidSamples;
			MinSurfaceZ = FMath::Min(MinSurfaceZ, Sample.SurfaceBlockWorldPos.Z);
			MaxSurfaceZ = FMath::Max(MaxSurfaceZ, Sample.SurfaceBlockWorldPos.Z);
		}
		CorridorRejection += FString::Printf(TEXT(" samples=%d/%d surfaceZ=%s endpointZ=%d/%d maxSlope=%d roots=%d"),
			ValidSamples, TerrainEvidence.SurfaceSamples.Num(),
			ValidSamples ? *FString::Printf(TEXT("%d..%d"), MinSurfaceZ, MaxSurfaceZ) : TEXT("none"),
			ConnectorRecord.StartEndpointBlockWorldPos.Z, ConnectorRecord.EndEndpointBlockWorldPos.Z,
			Query.MaxSlopeBlocks, RootFootprints.Num());
		OutFailureReason = FString::Printf(
			TEXT("Prepared continuation could not find a full-corridor terrain-valid connector centerline between forced stubs start=(%d,%d) end=(%d,%d). %s"),
			Portals.StartStubCell.X,
			Portals.StartStubCell.Y,
			Portals.EndStubCell.X,
			Portals.EndStubCell.Y,
			*CorridorRejection);
		return false;
	}

	bool ExpandContinuationCenterline(
		const TArray<FIntPoint>& CenterlineCells,
		const int32 CorridorWidthInCells,
		const int32 StructuralSolveLevel,
		TArray<FLayoutPlannedCell>& OutPlannedCells,
		FIntPoint& OutFootprintSize,
		FIntPoint& OutMinCell)
	{
		OutPlannedCells.Reset();
		OutFootprintSize = FIntPoint::ZeroValue;
		OutMinCell = FIntPoint::ZeroValue;
		if (CenterlineCells.IsEmpty() || CorridorWidthInCells <= 0 || CorridorWidthInCells % 2 == 0)
		{
			return false;
		}
		const int32 Radius = (CorridorWidthInCells - 1) / 2;
		TSet<FIntPoint> ExpandedCells;
		auto AddStrip = [&ExpandedCells, Radius](const FIntPoint& Cell, const bool bHorizontal)
		{
			for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
			{
				ExpandedCells.Add(bHorizontal
					? FIntPoint(Cell.X, Cell.Y + Offset)
					: FIntPoint(Cell.X + Offset, Cell.Y));
			}
		};
		auto AddTurnSquare = [&ExpandedCells, Radius](const FIntPoint& Cell)
		{
			for (int32 OffsetY = -Radius; OffsetY <= Radius; ++OffsetY)
			{
				for (int32 OffsetX = -Radius; OffsetX <= Radius; ++OffsetX)
				{
					ExpandedCells.Add(FIntPoint(Cell.X + OffsetX, Cell.Y + OffsetY));
				}
			}
		};
		if (CenterlineCells.Num() == 1)
		{
			AddStrip(CenterlineCells[0], true);
		}
		for (int32 Index = 1; Index < CenterlineCells.Num(); ++Index)
		{
			const FIntPoint Delta = CenterlineCells[Index] - CenterlineCells[Index - 1];
			if (FMath::Abs(Delta.X) + FMath::Abs(Delta.Y) != 1)
			{
				return false;
			}
			const bool bHorizontal = Delta.X != 0;
			AddStrip(CenterlineCells[Index - 1], bHorizontal);
			AddStrip(CenterlineCells[Index], bHorizontal);
			if (Index > 1)
			{
				const FIntPoint PreviousDelta = CenterlineCells[Index - 1] - CenterlineCells[Index - 2];
				if (PreviousDelta.X * Delta.X + PreviousDelta.Y * Delta.Y == 0)
				{
					// Fill convex turn gaps so a widened L-corridor remains square at its elbow.
					AddTurnSquare(CenterlineCells[Index - 1]);
				}
			}
		}
		TArray<FIntPoint> SortedCells = ExpandedCells.Array();
		SortedCells.Sort([](const FIntPoint& Left, const FIntPoint& Right)
		{
			return Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X;
		});
		OutMinCell = SortedCells[0];
		int32 MaxX = SortedCells[0].X;
		int32 MaxY = SortedCells[0].Y;
		for (const FIntPoint& Cell : SortedCells)
		{
			OutMinCell.X = FMath::Min(OutMinCell.X, Cell.X);
			OutMinCell.Y = FMath::Min(OutMinCell.Y, Cell.Y);
			MaxX = FMath::Max(MaxX, Cell.X);
			MaxY = FMath::Max(MaxY, Cell.Y);
		}
		const FIntPoint StartCell = CenterlineCells[0];
		const FIntPoint EndCell = CenterlineCells.Last();
		for (const FIntPoint& Cell : SortedCells)
		{
			FLayoutPlannedCell& PlannedCell = OutPlannedCells.AddDefaulted_GetRef();
			PlannedCell.Cell = FIntVector(Cell.X - OutMinCell.X, Cell.Y - OutMinCell.Y, StructuralSolveLevel);
			PlannedCell.Intent = Cell == StartCell || Cell == EndCell
				? ELayoutCellIntent::Entry
				: ELayoutCellIntent::Connector;
		}
		OutFootprintSize = FIntPoint(MaxX - OutMinCell.X + 1, MaxY - OutMinCell.Y + 1);
		return true;
	}

	bool TryBuildPreparedContinuationRequest(
		FLayoutPreparedContinuation& InOutPrepared,
		const FLayoutContinuationCandidateSnapshot* const Profile,
		const int32 WorldSeed,
		const TMap<FIntPoint, FInternalConnectorPathCell>& TerrainCellsByGrid,
		FString& OutFailureReason)
	{
		const FResolvedLayoutConnectorRecord& ConnectorRecord = InOutPrepared.ConnectorRecord;
		const FLayoutResolvedConnectorSolveSourceSelection SolveSourceSelection =
			ConnectorRecord.GetResolvedConnectorSolveSourceSelection();
		const FString RegionDebugPath = BuildConnectorRegionDebugPath(
			ConnectorRecord.StartEndpointBlockWorldPos,
			ConnectorRecord.EndEndpointBlockWorldPos);
		const FLayoutRootPublicationMetadata PublicationMetadata = ConnectorRecord.GetRootPublicationMetadata();
		if (PublicationMetadata.RootPlacementPolicyId.IsNone()
			|| PublicationMetadata.RootCandidateId.IsNone()
			|| PublicationMetadata.RootSolveId.IsNone())
		{
			OutFailureReason = TEXT("Prepared continuation requires root publication metadata.");
			return false;
		}
		if (!Profile->FailureReason.IsEmpty())
		{
			OutFailureReason = Profile->FailureReason;
			return false;
		}
		const FLayoutRegionSolveRequest& Prototype = Profile->RequestPrototype;
		FLayoutRegionSolveRequest SolveRequest;
		LayoutRegionRequestSnapshotBuilder::PopulateStandaloneRequestBase(
			SolveRequest, Prototype.SnapshotSchemaVersion, SolveSourceSelection.SolveSeed ^ WorldSeed,
			RegionDebugPath, Prototype.ExecutionSettings, PublicationMetadata.RootPlacementPolicyId,
			PublicationMetadata.RootCandidateId, PublicationMetadata.RootSolveId,
			Prototype.TemplatePlacementZOffsetBlocks, Prototype.RootPlacementKind, Prototype.WorldBindingPlacementPolicy);
		SolveRequest.ContentSetSnapshot = Prototype.ContentSetSnapshot;
		SolveRequest.ProfileSnapshot = Prototype.ProfileSnapshot;
		SolveRequest.ModuleCatalog = Prototype.ModuleCatalog;
		SolveRequest.FootprintSize = LayoutStandaloneRegionRequestBuilder::SelectFootprintSize(
			SolveRequest.ProfileSnapshot, SolveRequest.Seed);
		// Rebuild per-segment identity/assertions exactly as the authored builder does; never reuse prototype proofs.
		LayoutRegionRequestSnapshotBuilder::FinalizeStandaloneRequestSnapshots(SolveRequest);
		SolveRequest.WorldBindingId = Prototype.WorldBindingId;
		SolveRequest.FootprintSize = InOutPrepared.FootprintSize;
		SolveRequest.PlannedCells = InOutPrepared.PlannedCells;
		SolveRequest.RootContinuationSelection = ConnectorRecord.ResolvedContinuationSelection;
		SolveRequest.ValidationAssertions.Add(MakeConnectorWorldPlacementLatticeAssertion(
			ConnectorRecord,
			ConnectorRecord.FrontendSharedCellSizeInBlocks,
			RegionDebugPath));
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy =
			SolveRequest.WorldBindingPlacementPolicy.TerrainTransition;
		const bool bBuildTerrainSupport = ShouldDeriveConnectorTerrainSupport(
			Profile,
			SolveRequest.RootPlacementKind,
			TerrainTransitionPolicy);
		if (bBuildTerrainSupport)
		{
			FLayoutFrozenTerrainBiomeAdapterInput NormalizedTerrain = InOutPrepared.FrozenTerrainEvidence;
			FLayoutSteppedTerrainSupportMap SupportMap;
			if (!TryBuildConnectorSteppedTerrainSupportMap(
					InOutPrepared.PathOriginBlockWorldPos,
					ConnectorRecord.FrontendSharedCellSizeInBlocks,
					InOutPrepared.MinPlannedCell,
					TerrainCellsByGrid,
					InOutPrepared.PlannedCells,
					SupportMap,
					OutFailureReason))
			{
				return false;
			}
			FLayoutSteppedTerrainSupportMap NeighborHaloMap;
			if (Profile->bSupportsSteppedTerrainSolve)
			{
				TSet<FIntVector> PlannedCellSet;
				for (const FLayoutPlannedCell& PlannedCell : InOutPrepared.PlannedCells)
				{
					PlannedCellSet.Add(PlannedCell.Cell);
				}
				TArray<FLayoutPlannedCell> HaloCells;
				for (const FLayoutPlannedCell& PlannedCell : InOutPrepared.PlannedCells)
				{
					for (const FIntVector& Delta : {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)})
					{
						const FIntVector NeighborCell = PlannedCell.Cell + Delta;
						const FIntPoint RawNeighbor(
							NeighborCell.X + InOutPrepared.MinPlannedCell.X,
							NeighborCell.Y + InOutPrepared.MinPlannedCell.Y);
						const FInternalConnectorPathCell* const TerrainCell = TerrainCellsByGrid.Find(RawNeighbor);
						if (PlannedCellSet.Contains(NeighborCell)
							|| TerrainCell == nullptr
							|| TerrainCell->Type != EInternalConnectorPathCellType::Walkable
							|| HaloCells.ContainsByPredicate([NeighborCell](const FLayoutPlannedCell& Candidate)
							{
								return Candidate.Cell == NeighborCell;
							}))
						{
							continue;
						}
						FLayoutPlannedCell& HaloCell = HaloCells.AddDefaulted_GetRef();
						HaloCell.Cell = NeighborCell;
						HaloCell.Intent = ELayoutCellIntent::Interior;
					}
				}
				if (!HaloCells.IsEmpty()
					&& !TryBuildConnectorSteppedTerrainSupportMap(
						InOutPrepared.PathOriginBlockWorldPos,
						ConnectorRecord.FrontendSharedCellSizeInBlocks,
						InOutPrepared.MinPlannedCell,
						TerrainCellsByGrid,
						HaloCells,
						NeighborHaloMap,
						OutFailureReason))
				{
					return false;
				}

				// Reuse root terrain evidence normalization and diagonal cluster policy.
				// A shifted cluster must produce same bridge/deck topology for root and
				// continuation prewarm.
				const FIntVector SegmentFootprintMinBlockWorldPos =
					InOutPrepared.PathOriginBlockWorldPos - FIntVector(
						ConnectorRecord.FrontendSharedCellSizeInBlocks.X / 2,
						ConnectorRecord.FrontendSharedCellSizeInBlocks.Y / 2,
						ConnectorRecord.FrontendSharedCellSizeInBlocks.Z / 2);
				NormalizedTerrain = FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
						InOutPrepared.FrozenTerrainEvidence.SiteCenterBlockWorldPos,
						SegmentFootprintMinBlockWorldPos,
						ConnectorRecord.FrontendSharedCellSizeInBlocks,
						FIntPoint(
							SolveRequest.FootprintSize.X * ConnectorRecord.FrontendSharedCellSizeInBlocks.X,
							SolveRequest.FootprintSize.Y * ConnectorRecord.FrontendSharedCellSizeInBlocks.Y),
						SolveRequest.WorldBindingPlacementPolicy.SurfaceSearch,
						SolveRequest.WorldBindingPlacementPolicy.TerrainTransition,
						InOutPrepared.FrozenTerrainEvidence.CoordinateSettings,
						ConnectorRecord.FrontendSharedCellSizeInBlocks.X,
						InOutPrepared.FrozenTerrainEvidence.EligibleBiomeRowName,
						InOutPrepared.FrozenTerrainEvidence.EligibleBiomeRowNames,
						SupportMap,
						false,
						NeighborHaloMap.SupportSamples);
				// Shared stepped normalization rebuilds support/classification evidence.
				// Retain route-owned samples so the bridge adapter still receives the
				// frozen continuation terrain-path contract after that rebuild.
				NormalizedTerrain.TerrainPathSamples = InOutPrepared.FrozenTerrainEvidence.TerrainPathSamples;
				NormalizedTerrain.bHasTerrainPathEvidence = !NormalizedTerrain.TerrainPathSamples.IsEmpty();
				if (NormalizedTerrain.SteppedSupportSamples.Num() == SupportMap.SupportSamples.Num())
				{
					SupportMap.SupportSamples = NormalizedTerrain.SteppedSupportSamples;
					SupportMap.MaximumObservedNeighborHeightDelta = 0;
					SupportMap.MaximumObservedSnappedLevelDelta = 0;
					for (FLayoutSteppedTerrainSupportSample& SupportSample : SupportMap.SupportSamples)
					{
						SupportSample.SnappedSupportCeilingZ = SupportSample.SnappedSupportFloorZ
							+ ConnectorRecord.FrontendSharedCellSizeInBlocks.Z;
					}
					for (FLayoutSteppedTerrainAdjacencyStep& Step : SupportMap.AdjacencySteps)
					{
						const FLayoutSteppedTerrainSupportSample* const FromSupport =
							SupportMap.SupportSamples.FindByPredicate([&Step](const FLayoutSteppedTerrainSupportSample& Sample)
							{
								return Sample.LocalCell == Step.FromCell;
							});
						const FLayoutSteppedTerrainSupportSample* const ToSupport =
							SupportMap.SupportSamples.FindByPredicate([&Step](const FLayoutSteppedTerrainSupportSample& Sample)
							{
								return Sample.LocalCell == Step.ToCell;
							});
						if (FromSupport == nullptr || ToSupport == nullptr)
						{
							continue;
						}
						Step.StepHeightBlocks = FMath::Abs(FromSupport->SupportSurfaceZ - ToSupport->SupportSurfaceZ);
						Step.SnappedLevelDelta = FMath::Abs(
							FromSupport->SnappedSupportCeilingZ - ToSupport->SnappedSupportCeilingZ)
							/ ConnectorRecord.FrontendSharedCellSizeInBlocks.Z;
						SupportMap.MaximumObservedNeighborHeightDelta = FMath::Max(
							SupportMap.MaximumObservedNeighborHeightDelta,
							Step.StepHeightBlocks);
						SupportMap.MaximumObservedSnappedLevelDelta = FMath::Max(
							SupportMap.MaximumObservedSnappedLevelDelta,
							Step.SnappedLevelDelta);
					}
				}
				LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(SolveRequest);
			}
			// Keep the segment-local noise artifact intact for worker prewarm and
			// failed-preview bounds. The stepped adapter requires both support and
			// footprint-classification evidence; root preparation supplies both too.
			InOutPrepared.FrozenTerrainEvidence = NormalizedTerrain;
			InOutPrepared.FrozenTerrainEvidence.bHasSteppedSupportEvidence = !SupportMap.SupportSamples.IsEmpty();
			InOutPrepared.FrozenTerrainEvidence.SteppedSupportSamples = SupportMap.SupportSamples;
			InOutPrepared.FrozenTerrainEvidence.SteppedNeighborHaloSamples = NeighborHaloMap.SupportSamples;
			InOutPrepared.FrozenTerrainEvidence.SteppedAdjacencySteps = SupportMap.AdjacencySteps;
			InOutPrepared.FrozenTerrainEvidence.MaxObservedNeighborHeightDelta =
				SupportMap.MaximumObservedNeighborHeightDelta;
			InOutPrepared.FrozenTerrainEvidence.MaxObservedSnappedLevelDelta =
				SupportMap.MaximumObservedSnappedLevelDelta;
			SolveRequest.bHasFrozenTerrainBiomeAdapterInput = true;
			SolveRequest.FrozenTerrainBiomeAdapterInput = InOutPrepared.FrozenTerrainEvidence;
			SolveRequest.SteppedTerrainSupportMap = MoveTemp(SupportMap);
		}
		const FConnectorEndpointRequestContracts EndpointContracts = BuildConnectorEndpointRequestContracts(
			SolveRequest,
			ConnectorRecord,
			InOutPrepared.CenterlineCells,
			InOutPrepared.MinPlannedCell,
			RegionDebugPath);
		SolveRequest.IncomingBoundaryPoints = EndpointContracts.IncomingBoundaryPoints;
		SolveRequest.CommittedEndpointAnchors = EndpointContracts.CommittedEndpointAnchors;

		TSet<FIntVector> ContinuationEntryCells;
		for (const FLayoutPlannedCell& PlannedCell : InOutPrepared.PlannedCells)
		{
			if (PlannedCell.Intent == ELayoutCellIntent::Entry)
			{
				ContinuationEntryCells.Add(PlannedCell.Cell);
			}
		}
		if (ContinuationEntryCells.Num() != 2
			|| EndpointContracts.CommittedEndpointAnchors.Num() != 2)
		{
			OutFailureReason = FString::Printf(
				TEXT("Continuation requires exactly two contracted entry anchors (plannedEntries=%d, contractedAnchors=%d, startSupported=%d, endSupported=%d, profile=%s, segment=%d)."),
				ContinuationEntryCells.Num(),
				EndpointContracts.CommittedEndpointAnchors.Num(),
				EndpointContracts.bStartSupportsCommittedEndpoint ? 1 : 0,
				EndpointContracts.bEndSupportsCommittedEndpoint ? 1 : 0,
				*Profile->ProfilePath.ToString(),
				ConnectorRecord.ContinuationSegmentIndex);
			return false;
		}
		for (const FLayoutCommittedEndpointAnchor& Anchor : EndpointContracts.CommittedEndpointAnchors)
		{
			FIntPoint UnusedPortalOffset;
			if (!ContinuationEntryCells.Contains(Anchor.LocalCell)
				|| !TryGetHorizontalEntryPortalOffset(Anchor.FaceDirection, UnusedPortalOffset))
			{
				OutFailureReason = TEXT("Continuation entry anchor must map to one horizontal contracted entry cell.");
				return false;
			}
		}
		if (EndpointContracts.CommittedEndpointAnchors[0].LocalCell
			== EndpointContracts.CommittedEndpointAnchors[1].LocalCell)
		{
			OutFailureReason = TEXT("Continuation entry anchors must occupy distinct cells.");
			return false;
		}

		InOutPrepared.SolveRequest = MoveTemp(SolveRequest);
		return true;
	}
}

bool FLayoutConnectorPlanning::TrySampleConnectorCorridorTerrain(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const FLayoutConnectorTerrainPathContext* const TerrainPathContext,
	FLayoutFrozenTerrainBiomeAdapterInput& OutTerrainEvidence,
	const int32 RequiredCorridorWidthInCells)
{
	OutTerrainEvidence = FLayoutFrozenTerrainBiomeAdapterInput();
	if (TerrainPathContext == nullptr || TerrainPathContext->ActiveBiomeSampler == nullptr)
	{
		return false;
	}

	const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
		ConnectorRecord.GetResolvedConnectorFrontendSelection();
	const FIntVector& SharedCellSizeInBlocks =
		FrontendSelection.SharedCellSizeInBlocks;
	if (SharedCellSizeInBlocks.X <= 0 || SharedCellSizeInBlocks.Y <= 0)
	{
		return false;
	}

	const FConnectorPathPlanningSettings PathSettings =
		BuildConnectorPathPlanningSettingsFromFrontendSelection(FrontendSelection);
	const FIntVector RouteGridOrigin = GetConnectorRouteGridOrigin(
		ConnectorRecord,
		SharedCellSizeInBlocks);
	const int32 CorridorWidthInCells = RequiredCorridorWidthInCells;
	if (CorridorWidthInCells <= 0 || CorridorWidthInCells % 2 == 0)
	{
		return false;
	}
	const int32 WidthRadiusInCells = (CorridorWidthInCells - 1) / 2;

	// Compute the corridor bounding box in cell coordinates, relative to
	// the start endpoint as the origin.
	const FIntPoint EndRootCell = ToConnectorCell(
		ConnectorRecord.StartEndpointBlockWorldPos,
		ConnectorRecord.EndEndpointBlockWorldPos,
		SharedCellSizeInBlocks);
	FConnectorRoutePortals Portals;
	if (!TryBuildConnectorRoutePortals(ConnectorRecord, SharedCellSizeInBlocks, Portals))
	{
		return false;
	}
	const FIntPoint Padding(
		FMath::Max(0, PathSettings.PathPaddingCells) + WidthRadiusInCells,
		FMath::Max(0, PathSettings.PathPaddingCells) + WidthRadiusInCells);
	const int32 MinPathCellX = FMath::Min(FMath::Min(FMath::Min(0, EndRootCell.X), Portals.StartPortalCell.X), FMath::Min(Portals.StartStubCell.X, FMath::Min(Portals.EndPortalCell.X, Portals.EndStubCell.X)));
	const int32 MinPathCellY = FMath::Min(FMath::Min(FMath::Min(0, EndRootCell.Y), Portals.StartPortalCell.Y), FMath::Min(Portals.StartStubCell.Y, FMath::Min(Portals.EndPortalCell.Y, Portals.EndStubCell.Y)));
	const int32 MaxPathCellX = FMath::Max(FMath::Max(FMath::Max(0, EndRootCell.X), Portals.StartPortalCell.X), FMath::Max(Portals.StartStubCell.X, FMath::Max(Portals.EndPortalCell.X, Portals.EndStubCell.X)));
	const int32 MaxPathCellY = FMath::Max(FMath::Max(FMath::Max(0, EndRootCell.Y), Portals.StartPortalCell.Y), FMath::Max(Portals.StartStubCell.Y, FMath::Max(Portals.EndPortalCell.Y, Portals.EndStubCell.Y)));
	const FIntPoint MinCell(
		MinPathCellX - FMath::Max(0, Padding.X),
		MinPathCellY - FMath::Max(0, Padding.Y));
	const FIntPoint MaxCell(
		MaxPathCellX + FMath::Max(0, Padding.X),
		MaxPathCellY + FMath::Max(0, Padding.Y));

	// Sample terrain surface Z for every column in the corridor.
	int32 MinBlockX = TNumericLimits<int32>::Max();
	int32 MaxBlockX = TNumericLimits<int32>::Min();
	int32 MinBlockY = TNumericLimits<int32>::Max();
	int32 MaxBlockY = TNumericLimits<int32>::Min();
	for (int32 Y = MinCell.Y; Y <= MaxCell.Y; ++Y)
	{
		if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested()) return false;
		for (int32 X = MinCell.X; X <= MaxCell.X; ++X)
		{
			const FIntPoint BlockXY(
				RouteGridOrigin.X + X * SharedCellSizeInBlocks.X,
				RouteGridOrigin.Y + Y * SharedCellSizeInBlocks.Y);

			FLayoutActiveBiomeSurfaceSample ActiveSample;
			const bool bFound = !PathSettings.PathBiomeRowNames.IsEmpty()
				? (PathSettings.PathBiomeRowNames.Num() == 1
					? TerrainPathContext->ActiveBiomeSampler->FindEligibleBiomeSurface(
						PathSettings.PathBiomeRowNames[0], BlockXY,
						PathSettings.TerrainSearchStartZ, PathSettings.TerrainSearchDepthBlocks,
						TerrainPathContext->CoordinateSettings, ActiveSample)
					: TerrainPathContext->ActiveBiomeSampler->FindEligibleBiomeSurfaceFromAnyRow(
						PathSettings.PathBiomeRowNames, BlockXY,
						PathSettings.TerrainSearchStartZ, PathSettings.TerrainSearchDepthBlocks,
						TerrainPathContext->CoordinateSettings, ActiveSample))
				: (PathSettings.PathBiomeRowName.IsNone()
					? TerrainPathContext->ActiveBiomeSampler->FindAnyActiveBiomeSurface(
						BlockXY, PathSettings.TerrainSearchStartZ,
						PathSettings.TerrainSearchDepthBlocks,
						TerrainPathContext->CoordinateSettings, ActiveSample)
					: TerrainPathContext->ActiveBiomeSampler->FindEligibleBiomeSurface(
						PathSettings.PathBiomeRowName, BlockXY,
						PathSettings.TerrainSearchStartZ, PathSettings.TerrainSearchDepthBlocks,
						TerrainPathContext->CoordinateSettings, ActiveSample));
			FLayoutTerrainSurfaceSample Sample;
			Sample.bIsValid = bFound && ActiveSample.bIsValid;
			Sample.BlockXY = BlockXY;
			Sample.SurfaceBlockWorldPos = Sample.bIsValid
				? ActiveSample.SurfaceBlockWorldPos
				: ConnectorRecord.StartEndpointBlockWorldPos;
			OutTerrainEvidence.SurfaceSamples.Add(Sample);
			MinBlockX = FMath::Min(MinBlockX, BlockXY.X);
			MaxBlockX = FMath::Max(MaxBlockX, BlockXY.X);
			MinBlockY = FMath::Min(MinBlockY, BlockXY.Y);
			MaxBlockY = FMath::Max(MaxBlockY, BlockXY.Y);
		}
	}

	// Preserve the complete game-thread corridor envelope. The worker-side bridge
	// adapter must consume frozen evidence and cannot recover this live sampling state.
	OutTerrainEvidence.EligibleBiomeRowName = ConnectorRecord.BiomeRowName;
	OutTerrainEvidence.bHasFiniteSearchBounds = MinBlockX <= MaxBlockX && MinBlockY <= MaxBlockY;
	OutTerrainEvidence.bHasSampledColumnEvidence = !OutTerrainEvidence.SurfaceSamples.IsEmpty();
	OutTerrainEvidence.SiteCenterBlockWorldPos = ConnectorRecord.StartEndpointBlockWorldPos;
	OutTerrainEvidence.TerrainSampleGridSpacing = FMath::Max(1, SharedCellSizeInBlocks.X);
	OutTerrainEvidence.SearchDepthBlocks = FMath::Max(1, PathSettings.TerrainSearchDepthBlocks);

	if (OutTerrainEvidence.bHasFiniteSearchBounds)
	{
		OutTerrainEvidence.SearchMinBlockXY = FIntPoint(MinBlockX, MinBlockY);
		OutTerrainEvidence.SearchMaxBlockXY = FIntPoint(MaxBlockX, MaxBlockY);
		OutTerrainEvidence.FootprintMinBlockWorldPos = FIntVector(
			MinBlockX,
			MinBlockY,
			RouteGridOrigin.Z);
		OutTerrainEvidence.FootprintSizeInBlocks = FIntPoint(
			MaxBlockX - MinBlockX + SharedCellSizeInBlocks.X,
			MaxBlockY - MinBlockY + SharedCellSizeInBlocks.Y);
	}

	return true;
}


bool FLayoutConnectorPlanning::CaptureContinuationPlanningInputs(
	const FResolvedLayoutConnectorRecord& RouteRecord,
	const ULayoutWorldBindingAsset* WorldBinding,
	const int32 WorldSeed,
	FLayoutContinuationPlanningSnapshot& OutSnapshot,
	FString& OutFailureReason)
{
	check(IsInGameThread());
	OutSnapshot = FLayoutContinuationPlanningSnapshot();
	OutFailureReason.Reset();
	if (WorldBinding == nullptr)
	{
		OutFailureReason = TEXT("Continuation planning requires a world binding.");
		return false;
	}
	const auto* Family = WorldBinding->ContinuationFamilies.FindByPredicate([&RouteRecord](const auto& Value)
	{
		return Value.FamilyId == RouteRecord.ContinuationFamilyId;
	});
	if (Family == nullptr)
	{
		OutFailureReason = TEXT("Continuation planning could not resolve the selected family.");
		return false;
	}
	OutSnapshot.BindingId = WorldBinding->BindingId.IsNone() ? WorldBinding->GetFName() : WorldBinding->BindingId;
	OutSnapshot.FamilyId = Family->FamilyId;
	for (const FLayoutWorldBindingContinuationCandidate& Source : Family->Candidates)
	{
		auto& Captured = OutSnapshot.Candidates.AddDefaulted_GetRef();
		Captured.CandidateId = Source.CandidateId;
		Captured.Weight = Source.Weight;
		const ULayoutProfileAsset* Profile = Source.LayoutProfile;
		Captured.bHasProfile = Profile != nullptr;
		if (Profile == nullptr) continue;
		Captured.ProfilePath = Source.LayoutProfile;
		Captured.ContentSetPath = Profile->ContentSet;
		Captured.MinimumFootprintInCells = Profile->MinimumFootprintInCells;
		Captured.MaximumFootprintInCells = Profile->MaximumFootprintInCells;
		Captured.ContinuationEntryLevel = Profile->ContinuationEntryLevel;
		Captured.bSupportsSteppedTerrainSolve = Profile->bSupportsSteppedTerrainSolve;
		FResolvedLayoutConnectorRecord PrototypeRecord = RouteRecord;
		auto SolveSource = PrototypeRecord.GetResolvedConnectorSolveSourceSelection();
		SolveSource.LayoutProfile = Captured.ProfilePath;
		SolveSource.ContentSet = Captured.ContentSetPath;
		PrototypeRecord.SetResolvedConnectorSolveSourceSelection(SolveSource);
		auto Frontend = PrototypeRecord.GetResolvedConnectorFrontendSelection();
		Frontend.ContinuationFamilyCandidateId = Captured.CandidateId;
		Frontend.ResolvedContinuationSelection.ResolvedEntryLevel = Captured.ContinuationEntryLevel;
		PrototypeRecord.SetResolvedConnectorFrontendSelection(Frontend);
		FLayoutWorldBindingRuntimeView RuntimeView;
		const auto Metadata = PrototypeRecord.GetRootPublicationMetadata();
		if (!LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding, PrototypeRecord, PrototypeRecord.SolveBudget, RuntimeView, Captured.FailureReason)) continue;
		LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView, SolveSource.SolveSeed ^ WorldSeed,
			BuildConnectorRegionDebugPath(PrototypeRecord.StartEndpointBlockWorldPos, PrototypeRecord.EndEndpointBlockWorldPos),
			Metadata.RootPlacementPolicyId, Metadata.RootCandidateId, Metadata.RootSolveId,
			Captured.RequestPrototype, Captured.FailureReason);
	}
	return true;
}

bool FLayoutConnectorPlanning::TryDiscoverContinuationRoute(
	const FResolvedLayoutConnectorRecord& Record, const ULayoutWorldBindingAsset* Binding,
	const FLayoutConnectorTerrainPathContext* Context, FLayoutFrozenTerrainBiomeAdapterInput& Terrain,
	TArray<FIntPoint>& Cells, FString& Failure)
{
	Terrain = FLayoutFrozenTerrainBiomeAdapterInput();
	Cells.Reset();
	FLayoutContinuationPlanningSnapshot Snapshot;
	return CaptureContinuationPlanningInputs(Record, Binding, 0, Snapshot, Failure)
		&& TryDiscoverContinuationRoute(Record, Snapshot, Context, Terrain, Cells, Failure);
}

bool FLayoutConnectorPlanning::TryBuildContinuationRouteSegments(
	const FResolvedLayoutConnectorRecord& Record, const ULayoutWorldBindingAsset* Binding,
	const TArray<FIntPoint>& Cells, const TSet<int32>& Forbidden, FLayoutContinuationRouteRecord& Route, FString& Failure)
{
	Route = FLayoutContinuationRouteRecord();
	FLayoutContinuationPlanningSnapshot Snapshot;
	return CaptureContinuationPlanningInputs(Record, Binding, 0, Snapshot, Failure)
		&& TryBuildContinuationRouteSegments(Record, Snapshot, Cells, Forbidden, Route, Failure);
}

bool FLayoutConnectorPlanning::TryPrepareContinuationRoute(
	const FResolvedLayoutConnectorRecord& Record, const ULayoutWorldBindingAsset* Binding, int32 WorldSeed,
	const FLayoutConnectorTerrainPathContext* Context, FLayoutPreparedContinuationRoute& Route, FString& Failure)
{
	Route = FLayoutPreparedContinuationRoute();
	FLayoutContinuationPlanningSnapshot Snapshot;
	return CaptureContinuationPlanningInputs(Record, Binding, WorldSeed, Snapshot, Failure)
		&& TryPrepareContinuationRoute(Record, Snapshot, WorldSeed, Context, Route, Failure);
}

bool FLayoutConnectorPlanning::TryPrepareContinuationSegment(
	const FResolvedLayoutConnectorRecord& Record, const FLayoutContinuationSegmentDescriptor& Segment,
	const FLayoutContinuationRouteRecord& Route, const ULayoutWorldBindingAsset* Binding, int32 WorldSeed,
	const FLayoutFrozenTerrainBiomeAdapterInput& Terrain, FLayoutPreparedContinuation& Prepared, FString& Failure)
{
	Prepared = FLayoutPreparedContinuation();
	FLayoutContinuationPlanningSnapshot Snapshot;
	return CaptureContinuationPlanningInputs(Record, Binding, WorldSeed, Snapshot, Failure)
		&& TryPrepareContinuationSegment(Record, Segment, Route, Snapshot, WorldSeed, Terrain, Prepared, Failure);
}

bool FLayoutConnectorPlanning::TryDiscoverContinuationRoute(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const FLayoutContinuationPlanningSnapshot& Snapshot,
	const FLayoutConnectorTerrainPathContext* const TerrainPathContext,
	FLayoutFrozenTerrainBiomeAdapterInput& OutTerrainEvidence,
	TArray<FIntPoint>& OutCenterlineCells,
	FString& OutFailureReason)
{
	OutTerrainEvidence = FLayoutFrozenTerrainBiomeAdapterInput();
	OutCenterlineCells.Reset();
	OutFailureReason.Reset();
	int32 MaximumCorridorWidth = 0;
	bool bRequireAdjacentTerrainStages = false;
	for (const FLayoutContinuationCandidateSnapshot& Candidate : Snapshot.Candidates)
	{
		const FLayoutContinuationCandidateSnapshot* const Profile = &Candidate;
		if (!Profile->bHasProfile)
		{
			continue;
		}
		const int32 Width = FMath::Max(
			Profile->MinimumFootprintInCells.X,
			Profile->MinimumFootprintInCells.Y);
		if (Width > 0 && Width % 2 != 0)
		{
			MaximumCorridorWidth = FMath::Max(MaximumCorridorWidth, Width);
			bRequireAdjacentTerrainStages |= Profile->bSupportsSteppedTerrainSolve;
		}
	}
	if (MaximumCorridorWidth == 0)
	{
		OutFailureReason = TEXT("Continuation route discovery found no candidate with a positive odd minimum width.");
		return false;
	}
	if (!TrySampleConnectorCorridorTerrain(
			ConnectorRecord,
			TerrainPathContext,
			OutTerrainEvidence,
			MaximumCorridorWidth))
	{
		OutFailureReason = TEXT("Continuation route discovery could not capture corridor terrain evidence.");
		return false;
	}

	TMap<FIntPoint, FInternalConnectorPathCell> IgnoredTerrainCells;
	if (!TryBuildContinuationCenterlineFromFrozenTerrain(
		ConnectorRecord,
		OutTerrainEvidence,
		MaximumCorridorWidth,
		bRequireAdjacentTerrainStages,
		TerrainPathContext != nullptr
			? TConstArrayView<FLayoutRootSpacingReservation>(TerrainPathContext->RootFootprints)
			: TConstArrayView<FLayoutRootSpacingReservation>(),
		OutCenterlineCells,
		IgnoredTerrainCells,
		OutFailureReason)) return false;

	// Recheck widened clearance against the captured footprints after routing. This catches
	// conversion mistakes while root footprints already act as A* obstacles above.
	TArray<FLayoutPlannedCell> Cells;
	FIntPoint Footprint, MinCell;
	if (!ExpandContinuationCenterline(OutCenterlineCells, MaximumCorridorWidth, 0, Cells, Footprint, MinCell))
	{
		OutFailureReason = TEXT("Continuation clearance requires a valid widened corridor.");
		return false;
	}
	const FIntVector CellSize = ConnectorRecord.GetResolvedConnectorFrontendSelection().SharedCellSizeInBlocks;
	const FIntVector Origin = GetConnectorRouteGridOrigin(ConnectorRecord, CellSize)
		+ FIntVector(MinCell.X * CellSize.X + CellSize.X / 2, MinCell.Y * CellSize.Y + CellSize.Y / 2, 0);
	return TerrainPathContext == nullptr || ValidateContinuationClearance(
		Origin, CellSize, Cells, TerrainPathContext->RootFootprints, OutFailureReason);
}

void FLayoutConnectorPlanning::BuildContinuationPreviewGeometry(
	const FLayoutPreparedContinuation& Prepared,
	FLayoutContinuationPreviewGeometry& OutGeometry)
{
	OutGeometry = FLayoutContinuationPreviewGeometry();
	OutGeometry.PathOriginBlockWorldPos = Prepared.PathOriginBlockWorldPos;
	OutGeometry.SharedCellSizeInBlocks = Prepared.ConnectorRecord.FrontendSharedCellSizeInBlocks;
	if (OutGeometry.SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return;
	}

	const auto ResolveTerrainBaseZ = [&Prepared, &OutGeometry](const FIntVector& LocalCell)
	{
		const FLayoutSteppedTerrainSupportSample* const Support =
			Prepared.SolveRequest.SteppedTerrainSupportMap.SupportSamples.FindByPredicate(
				[&LocalCell](const FLayoutSteppedTerrainSupportSample& Candidate)
				{
					return Candidate.LocalCell.X == LocalCell.X
						&& Candidate.LocalCell.Y == LocalCell.Y;
				});
		if (Support != nullptr)
		{
			return Support->SnappedSupportFloorZ;
		}

		const FIntPoint BlockXY(
			OutGeometry.PathOriginBlockWorldPos.X + LocalCell.X * OutGeometry.SharedCellSizeInBlocks.X,
			OutGeometry.PathOriginBlockWorldPos.Y + LocalCell.Y * OutGeometry.SharedCellSizeInBlocks.Y);
		const FLayoutTerrainSurfaceSample* const FrozenNoiseSurface =
			Prepared.FrozenTerrainEvidence.SurfaceSamples.FindByPredicate(
				[&BlockXY](const FLayoutTerrainSurfaceSample& Candidate)
				{
					return Candidate.bIsValid && Candidate.BlockXY == BlockXY;
				});
		return FrozenNoiseSurface != nullptr && OutGeometry.SharedCellSizeInBlocks.Z > 0
			? FMath::FloorToInt(static_cast<float>(FrozenNoiseSurface->SurfaceBlockWorldPos.Z)
				/ OutGeometry.SharedCellSizeInBlocks.Z) * OutGeometry.SharedCellSizeInBlocks.Z
			: OutGeometry.PathOriginBlockWorldPos.Z;
	};

	const TArray<FLayoutPlannedCell>& PreviewPlannedCells = !Prepared.SolveRequest.PlannedCells.IsEmpty()
		? Prepared.SolveRequest.PlannedCells
		: Prepared.PlannedCells;
	for (const FLayoutPlannedCell& PlannedCell : PreviewPlannedCells)
	{
		FLayoutContinuationPreviewTerrainCell& TerrainCell = OutGeometry.TerrainCells.AddDefaulted_GetRef();
		TerrainCell.LocalCell = PlannedCell.Cell;
		TerrainCell.BaseBlockWorldZ = ResolveTerrainBaseZ(PlannedCell.Cell);
		TerrainCell.Intent = PlannedCell.Intent;
		TerrainCell.PlacementZone = PlannedCell.PlacementZone;
		TerrainCell.TerrainSeamFaceMask = PlannedCell.TerrainSeamFaceMask;
		TerrainCell.bIsBridgeCell = PlannedCell.bIsBridgeCell;
	}
	OutGeometry.TerrainCells.Sort([](
		const FLayoutContinuationPreviewTerrainCell& Left,
		const FLayoutContinuationPreviewTerrainCell& Right)
	{
		return Left.LocalCell.Z != Right.LocalCell.Z
			? Left.LocalCell.Z < Right.LocalCell.Z
			: (Left.LocalCell.Y != Right.LocalCell.Y
				? Left.LocalCell.Y < Right.LocalCell.Y
				: Left.LocalCell.X < Right.LocalCell.X);
	});

	for (const FIntPoint& CenterlineCell : Prepared.CenterlineCells)
	{
		const FIntVector CenterlineXY(
			CenterlineCell.X - Prepared.MinPlannedCell.X,
			CenterlineCell.Y - Prepared.MinPlannedCell.Y,
			0);
		const FLayoutContinuationPreviewTerrainCell* const TerrainCell =
			OutGeometry.TerrainCells.FindByPredicate([&CenterlineXY](const FLayoutContinuationPreviewTerrainCell& Candidate)
			{
				return Candidate.LocalCell.X == CenterlineXY.X
					&& Candidate.LocalCell.Y == CenterlineXY.Y;
			});
		OutGeometry.CenterlineCells.Add(TerrainCell != nullptr ? TerrainCell->LocalCell : CenterlineXY);
		OutGeometry.CenterlineBaseBlockWorldZs.Add(
			TerrainCell != nullptr ? TerrainCell->BaseBlockWorldZ : OutGeometry.PathOriginBlockWorldPos.Z);
	}
}

bool FLayoutConnectorPlanning::TryPrepareContinuationRoute(
	const FResolvedLayoutConnectorRecord& RouteRecord,
	const FLayoutContinuationPlanningSnapshot& Snapshot,
	const int32 WorldSeed,
	const FLayoutConnectorTerrainPathContext* const TerrainPathContext,
	FLayoutPreparedContinuationRoute& OutPreparedRoute,
	FString& OutFailureReason)
{
	OutPreparedRoute = FLayoutPreparedContinuationRoute();
	OutFailureReason.Reset();
	OutPreparedRoute.RouteRecord = RouteRecord;
	TArray<FIntPoint> DiscoveredCenterlineCells;
	if (!TryDiscoverContinuationRoute(
			RouteRecord,
			Snapshot,
			TerrainPathContext,
			OutPreparedRoute.FrozenTerrainEvidence,
			DiscoveredCenterlineCells,
			OutFailureReason))
	{
		return false;
	}
	TSet<int32> RequiredForbiddenSeamIndices;
	TSet<int32> SteppedTransitionSeamIndices;
	const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
		RouteRecord.GetResolvedConnectorFrontendSelection();
	const FIntVector CellSize = FrontendSelection.SharedCellSizeInBlocks;
	const FIntVector RouteGridOrigin = GetConnectorRouteGridOrigin(RouteRecord, CellSize);
	const bool bKeepSteppedTransitionsInsideOneSegment = Snapshot.Candidates.ContainsByPredicate(
		[](const FLayoutContinuationCandidateSnapshot& Candidate)
		{
			return Candidate.bHasProfile && Candidate.bSupportsSteppedTerrainSolve;
		});
	TMap<FIntPoint, FLayoutTerrainSurfaceSample> TerrainSamplesByRouteCell;
	for (const FLayoutTerrainSurfaceSample& Sample : OutPreparedRoute.FrozenTerrainEvidence.SurfaceSamples)
	{
		const int32 DeltaX = Sample.BlockXY.X - RouteGridOrigin.X;
		const int32 DeltaY = Sample.BlockXY.Y - RouteGridOrigin.Y;
		if (CellSize.X > 0 && CellSize.Y > 0 && DeltaX % CellSize.X == 0 && DeltaY % CellSize.Y == 0)
		{
			TerrainSamplesByRouteCell.Add(FIntPoint(DeltaX / CellSize.X, DeltaY / CellSize.Y), Sample);
		}
	}
	for (int32 Index = 1; Index < DiscoveredCenterlineCells.Num() - 1; ++Index)
	{
		const FLayoutTerrainSurfaceSample* const LeftSample =
			TerrainSamplesByRouteCell.Find(DiscoveredCenterlineCells[Index]);
		const FLayoutTerrainSurfaceSample* const RightSample =
			TerrainSamplesByRouteCell.Find(DiscoveredCenterlineCells[Index + 1]);
		const bool bMissingOrTooSteep = LeftSample == nullptr || RightSample == nullptr
			|| !LeftSample->bIsValid || !RightSample->bIsValid
			|| FMath::Abs(LeftSample->SurfaceBlockWorldPos.Z - RightSample->SurfaceBlockWorldPos.Z)
				> FrontendSelection.ContinuationPolicy.MaxSlopeBlocks;
		// Match adapter support-map stage classification. Raw floor division can
		// split a continuation through an edge that later normalizes to one stepped
		// terrain stage, leaving a non-endpoint segment without its bridge topology.
		const bool bCrossesSteppedLattice = bKeepSteppedTransitionsInsideOneSegment
			&& CellSize.Z > 0
			&& LeftSample != nullptr
			&& RightSample != nullptr
			&& FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(
				CellSize.Z, LeftSample->SurfaceBlockWorldPos.Z)
				!= FLayoutTerrainSampling::SnapSurfaceZUpToSharedCellLattice(
					CellSize.Z, RightSample->SurfaceBlockWorldPos.Z);
		if (bMissingOrTooSteep)
		{
			RequiredForbiddenSeamIndices.Add(Index);
		}
		if (bCrossesSteppedLattice)
		{
			// A vertical transition needs one segment-owned bridge/landing topology.
			// Splitting it duplicates that requirement across both descriptors.
			RequiredForbiddenSeamIndices.Add(Index);
			SteppedTransitionSeamIndices.Add(Index);
		}
	}

	// Prefer one flat route cell between each segment boundary and a terrain
	// transition. This leaves room for root-equivalent bridge/landing topology.
	TSet<int32> PreferredForbiddenSeamIndices = RequiredForbiddenSeamIndices;
	for (const int32 StepSeamIndex : SteppedTransitionSeamIndices)
	{
		PreferredForbiddenSeamIndices.Add(StepSeamIndex - 1);
		PreferredForbiddenSeamIndices.Add(StepSeamIndex + 1);
	}
	if (!TryBuildContinuationRouteSegments(
			RouteRecord,
			Snapshot,
			DiscoveredCenterlineCells,
			PreferredForbiddenSeamIndices,
			OutPreparedRoute.Route,
			OutFailureReason))
	{
		// Profile maximums may leave no one-cell buffer. Keep the step itself
		// unsplittable, but permit a boundary next to it so later segments run.
		if (SteppedTransitionSeamIndices.IsEmpty()
			|| !TryBuildContinuationRouteSegments(
				RouteRecord,
				Snapshot,
				DiscoveredCenterlineCells,
				RequiredForbiddenSeamIndices,
				OutPreparedRoute.Route,
				OutFailureReason))
		{
			return false;
		}
	}

	int32 PreparedSegmentCount = 0;
	TArray<FString> SegmentFailureReasons;
	for (const FLayoutContinuationSegmentDescriptor& Descriptor : OutPreparedRoute.Route.Segments)
	{
		if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested()) return false;
		FLayoutPreparedContinuationRouteSegment& SegmentResult =
			OutPreparedRoute.Segments.AddDefaulted_GetRef();
		SegmentResult.Descriptor = Descriptor;
		FLayoutPreparedContinuation PreparedSegment;
		if (TryPrepareContinuationSegment(
				RouteRecord,
				Descriptor,
				OutPreparedRoute.Route,
				Snapshot,
				WorldSeed,
				OutPreparedRoute.FrozenTerrainEvidence,
				PreparedSegment,
				SegmentResult.FailureReason))
		{
			SegmentResult.ConnectorRecord = PreparedSegment.ConnectorRecord;
			BuildContinuationPreviewGeometry(PreparedSegment, SegmentResult.PreviewGeometry);
			SegmentResult.PreparedContinuation.Emplace(MoveTemp(PreparedSegment));
			++PreparedSegmentCount;
		}
		else
		{
			BuildContinuationPreviewGeometry(PreparedSegment, SegmentResult.PreviewGeometry);
			SegmentFailureReasons.Add(FString::Printf(TEXT("Segment %d: %s"), Descriptor.SegmentIndex, *SegmentResult.FailureReason));
			SegmentResult.ConnectorRecord = MoveTemp(PreparedSegment.ConnectorRecord);
			if (SegmentResult.ConnectorRecord.ContinuationRouteId.IsNone())
			{
				SegmentResult.ConnectorRecord = RouteRecord;
				SegmentResult.ConnectorRecord.ContinuationRouteId = FLayoutId(*OutPreparedRoute.Route.RouteKey);
				SegmentResult.ConnectorRecord.ContinuationSegmentIndex = Descriptor.SegmentIndex;
				SegmentResult.ConnectorRecord.ContinuationFamilyCandidateId = Descriptor.CandidateId;
			}
		}
	}
	if (PreparedSegmentCount == 0)
	{
		OutFailureReason = FString::Printf(
			TEXT("No bounded continuation segment prepared from the discovered route. %s"),
			*FString::Join(SegmentFailureReasons, TEXT(" | ")));
	}
	return true;
}

bool FLayoutConnectorPlanning::TryPrepareContinuationSegment(
	const FResolvedLayoutConnectorRecord& RouteRecord,
	const FLayoutContinuationSegmentDescriptor& Segment,
	const FLayoutContinuationRouteRecord& Route,
	const FLayoutContinuationPlanningSnapshot& Snapshot,
	const int32 WorldSeed,
	const FLayoutFrozenTerrainBiomeAdapterInput& TerrainEvidence,
	FLayoutPreparedContinuation& OutPreparedContinuation,
	FString& OutFailureReason)
{
	OutPreparedContinuation = FLayoutPreparedContinuation();
	OutFailureReason.Reset();
	if (Segment.SegmentIndex < 0
		|| Segment.FirstRouteCellIndex < 0
		|| Segment.LastRouteCellIndex >= Route.CenterlineCells.Num()
		|| Segment.FirstRouteCellIndex >= Segment.LastRouteCellIndex)
	{
		OutFailureReason = TEXT("Continuation segment preparation received an invalid route descriptor.");
		return false;
	}

	const FLayoutContinuationCandidateSnapshot* const Profile =
		Snapshot.Candidates.FindByPredicate([&Segment](const FLayoutContinuationCandidateSnapshot& Value)
		{
			return Value.CandidateId == Segment.CandidateId;
		});
	if (Profile == nullptr || !Profile->bHasProfile || Profile->ContentSetPath.IsNull())
	{
		OutFailureReason = TEXT("Continuation segment candidate requires a profile with a content set.");
		return false;
	}

	FResolvedLayoutConnectorRecord SegmentRecord = RouteRecord;
	FLayoutResolvedConnectorSolveSourceSelection SolveSource = SegmentRecord.GetResolvedConnectorSolveSourceSelection();
	SolveSource.LayoutProfile = Profile->ProfilePath;
	SolveSource.ContentSet = Profile->ContentSetPath;
	SolveSource.SolveSeed = RouteRecord.SolveSeed ^ HashCombineFast(
		GetTypeHash(Route.RouteKey), static_cast<uint32>(Segment.SegmentIndex));
	SegmentRecord.SetResolvedConnectorSolveSourceSelection(SolveSource);
	FLayoutResolvedConnectorFrontendSelection FrontendSelection = SegmentRecord.GetResolvedConnectorFrontendSelection();
	FrontendSelection.ContinuationFamilyCandidateId = Segment.CandidateId;
	FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel = Profile->ContinuationEntryLevel;
	SegmentRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);
	SegmentRecord.ContinuationFamilyCandidateId = Segment.CandidateId;
	SegmentRecord.ContinuationRouteId = FLayoutId(*Route.RouteKey);
	SegmentRecord.ContinuationSegmentIndex = Segment.SegmentIndex;
	SegmentRecord.StartRootEntryFaceDirection = FLayoutDirectionUtils::GetOpposite(Segment.IngressFaceDirection);
	SegmentRecord.EndRootEntryFaceDirection = FLayoutDirectionUtils::GetOpposite(Segment.EgressFaceDirection);
	SegmentRecord.bHasResolvedEndpointFacingDirections = true;
	SegmentRecord.StartEndpointFacingDirection = Segment.IngressFaceDirection;
	SegmentRecord.EndEndpointFacingDirection = Segment.EgressFaceDirection;

	OutPreparedContinuation.ConnectorRecord = SegmentRecord;
	OutPreparedContinuation.FrozenTerrainEvidence = TerrainEvidence;
	OutPreparedContinuation.StartFacing = Segment.IngressFaceDirection;
	OutPreparedContinuation.EndFacing = Segment.EgressFaceDirection;
	for (int32 Index = Segment.FirstRouteCellIndex; Index <= Segment.LastRouteCellIndex; ++Index)
	{
		OutPreparedContinuation.CenterlineCells.Add(Route.CenterlineCells[Index]);
	}
	OutPreparedContinuation.MinCenterlineCell = OutPreparedContinuation.CenterlineCells[0];
	for (const FIntPoint& Cell : OutPreparedContinuation.CenterlineCells)
	{
		OutPreparedContinuation.MinCenterlineCell.X = FMath::Min(OutPreparedContinuation.MinCenterlineCell.X, Cell.X);
		OutPreparedContinuation.MinCenterlineCell.Y = FMath::Min(OutPreparedContinuation.MinCenterlineCell.Y, Cell.Y);
	}
	const int32 CorridorWidth = FMath::Max(
		Profile->MinimumFootprintInCells.X,
		Profile->MinimumFootprintInCells.Y);
	if (!ExpandContinuationCenterline(
			OutPreparedContinuation.CenterlineCells,
			CorridorWidth,
			ResolveConnectorStructuralSolveLevel(SegmentRecord),
			OutPreparedContinuation.PlannedCells,
			OutPreparedContinuation.FootprintSize,
			OutPreparedContinuation.MinPlannedCell))
	{
		OutFailureReason = TEXT("Continuation segment could not expand its cardinal centerline.");
		return false;
	}

	const FIntVector CellSize = FrontendSelection.SharedCellSizeInBlocks;
	const FIntVector RouteGridOrigin = GetConnectorRouteGridOrigin(RouteRecord, CellSize);
	if (CellSize.X <= 0 || CellSize.Y <= 0 || CellSize.Z <= 0)
	{
		OutFailureReason = TEXT("Continuation segment requires positive shared cell dimensions.");
		return false;
	}
	// Root terminals own their physical center. Propagate that height through
	// same-stage route prefixes/suffixes; only an actual frozen terrain stage may
	// change a seam height and require stepped topology.
	auto ResolveRouteStageBaseZ = [&Route, &TerrainEvidence, &RouteGridOrigin, &CellSize](
		const int32 RouteIndex,
		int32& OutStageBaseZ) -> bool
	{
		const FIntPoint& RouteCell = Route.CenterlineCells[RouteIndex];
		const FIntPoint BlockXY(
			RouteGridOrigin.X + RouteCell.X * CellSize.X,
			RouteGridOrigin.Y + RouteCell.Y * CellSize.Y);
		const FLayoutTerrainSurfaceSample* const Surface = TerrainEvidence.SurfaceSamples.FindByPredicate(
			[&BlockXY](const FLayoutTerrainSurfaceSample& Candidate)
			{
				return Candidate.bIsValid && Candidate.BlockXY == BlockXY;
			});
		if (Surface == nullptr)
		{
			return false;
		}
		OutStageBaseZ = FMath::DivideAndRoundUp(
			Surface->SurfaceBlockWorldPos.Z + 1,
			CellSize.Z) * CellSize.Z;
		return true;
	};
	auto ResolveSegmentEndpoint = [&Route, &RouteGridOrigin, &CellSize, &FrontendSelection, &ResolveRouteStageBaseZ](
		const int32 RouteIndex,
		const FIntVector& RootEndpoint,
		FIntVector& OutEndpoint) -> bool
	{
		if (RouteIndex == 0 || RouteIndex == Route.CenterlineCells.Num() - 1)
		{
			OutEndpoint = RootEndpoint;
			return true;
		}
		int32 StageBaseZ = 0;
		if (!ResolveRouteStageBaseZ(RouteIndex, StageBaseZ))
		{
			return false;
		}
		int32 StartStageBaseZ = 0;
		bool bSharesStartStage = ResolveRouteStageBaseZ(0, StartStageBaseZ);
		for (int32 Index = 1; bSharesStartStage && Index <= RouteIndex; ++Index)
		{
			int32 CandidateStageBaseZ = 0;
			bSharesStartStage = ResolveRouteStageBaseZ(Index, CandidateStageBaseZ)
				&& CandidateStageBaseZ == StartStageBaseZ;
		}
		int32 EndStageBaseZ = 0;
		bool bSharesEndStage = ResolveRouteStageBaseZ(Route.CenterlineCells.Num() - 1, EndStageBaseZ);
		for (int32 Index = Route.CenterlineCells.Num() - 2; bSharesEndStage && Index >= RouteIndex; --Index)
		{
			int32 CandidateStageBaseZ = 0;
			bSharesEndStage = ResolveRouteStageBaseZ(Index, CandidateStageBaseZ)
				&& CandidateStageBaseZ == EndStageBaseZ;
		}
		const FIntPoint& RouteCell = Route.CenterlineCells[RouteIndex];
		const FIntPoint BlockXY(
			RouteGridOrigin.X + RouteCell.X * CellSize.X,
			RouteGridOrigin.Y + RouteCell.Y * CellSize.Y);
		OutEndpoint = FIntVector(
			BlockXY.X,
			BlockXY.Y,
			bSharesStartStage
				? Route.StartRootEndpoint.EndpointBlockWorldPos.Z
				: (bSharesEndStage
					? Route.EndRootEndpoint.EndpointBlockWorldPos.Z
					: StageBaseZ + FrontendSelection.TemplatePlacementZOffsetBlocks + CellSize.Z / 2));
		return true;
	};
	if (!ResolveSegmentEndpoint(
			Segment.FirstRouteCellIndex,
			Route.StartRootEndpoint.EndpointBlockWorldPos,
			SegmentRecord.StartEndpointBlockWorldPos)
		|| !ResolveSegmentEndpoint(
			Segment.LastRouteCellIndex,
			Route.EndRootEndpoint.EndpointBlockWorldPos,
			SegmentRecord.EndEndpointBlockWorldPos))
	{
		OutFailureReason = TEXT("Continuation segment seam requires frozen terrain evidence.");
		return false;
	}
	// Planned cells are normalized against MinPlannedCell. Derive their XY center
	// from shared route grid, never from an already world-positioned segment entry.
	// Adding MinPlannedCell to StartEndpoint double-translates later descriptors.
	SegmentRecord.PathOriginBlockWorldPos = FIntVector(
		RouteGridOrigin.X + OutPreparedContinuation.MinPlannedCell.X * CellSize.X + CellSize.X / 2,
		RouteGridOrigin.Y + OutPreparedContinuation.MinPlannedCell.Y * CellSize.Y + CellSize.Y / 2,
		SegmentRecord.StartEndpointBlockWorldPos.Z
			- ResolveConnectorStructuralSolveLevel(SegmentRecord) * CellSize.Z);
	OutPreparedContinuation.PathOriginBlockWorldPos = SegmentRecord.PathOriginBlockWorldPos;
	OutPreparedContinuation.ConnectorRecord = SegmentRecord;

	const FConnectorPathPlanningSettings PathSettings =
		BuildConnectorPathPlanningSettingsFromFrontendSelection(FrontendSelection);
	TMap<FIntPoint, FInternalConnectorPathCell> TerrainCellsByGrid;
	for (const FLayoutTerrainSurfaceSample& Sample : TerrainEvidence.SurfaceSamples)
	{
		const int32 DeltaX = Sample.BlockXY.X - RouteGridOrigin.X;
		const int32 DeltaY = Sample.BlockXY.Y - RouteGridOrigin.Y;
		if (DeltaX % CellSize.X != 0 || DeltaY % CellSize.Y != 0)
		{
			continue;
		}
		FInternalConnectorPathCell& TerrainCell = TerrainCellsByGrid.Add(
			FIntPoint(DeltaX / CellSize.X, DeltaY / CellSize.Y));
		TerrainCell.Type = Sample.bIsValid
			? EInternalConnectorPathCellType::Walkable
			: (PathSettings.MaxBridgeGapCells > 0
				? EInternalConnectorPathCellType::Gap
				: EInternalConnectorPathCellType::Blocked);
		TerrainCell.SurfaceZBlockWorld = Sample.SurfaceBlockWorldPos.Z;
	}
	for (const FLayoutPlannedCell& PlannedCell : OutPreparedContinuation.PlannedCells)
	{
		const FIntPoint RouteCell(
			PlannedCell.Cell.X + OutPreparedContinuation.MinPlannedCell.X,
			PlannedCell.Cell.Y + OutPreparedContinuation.MinPlannedCell.Y);
		const FInternalConnectorPathCell* const TerrainCell = TerrainCellsByGrid.Find(RouteCell);
		if (TerrainCell == nullptr || TerrainCell->Type == EInternalConnectorPathCellType::Blocked)
		{
			OutFailureReason = TEXT("Continuation segment widened corridor cannot clear frozen terrain.");
			return false;
		}
	}

	if (!TryBuildPreparedContinuationRequest(
			OutPreparedContinuation,
			Profile,
			WorldSeed,
			TerrainCellsByGrid,
			OutFailureReason))
	{
		return false;
	}
	return true;
}

bool FLayoutConnectorPlanning::TryBuildContinuationRouteSegments(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const FLayoutContinuationPlanningSnapshot& Snapshot,
	const TArray<FIntPoint>& CenterlineCells,
	const TSet<int32>& ForbiddenSeamIndices,
	FLayoutContinuationRouteRecord& OutRoute,
	FString& OutFailureReason)
{
	OutRoute = FLayoutContinuationRouteRecord();
	OutFailureReason.Reset();
	if (CenterlineCells.Num() < 2)
	{
		OutFailureReason = TEXT("Continuation route segmentation requires a world binding and at least two centerline cells.");
		return false;
	}

	const FLayoutContinuationPlanningSnapshot& Family = Snapshot;
	if (Family.Candidates.IsEmpty())
	{
		OutFailureReason = TEXT("Continuation route segmentation requires at least one family candidate.");
		return false;
	}

	OutRoute.RouteKey = FString::Printf(
		TEXT("%llu|%s"),
		BuildConnectorPairKey(
			ConnectorRecord.StartSiteReservationKey,
			ConnectorRecord.EndSiteReservationKey),
		*Family.FamilyId.ToString());
	OutRoute.WorldBindingId = Snapshot.BindingId;
	OutRoute.ContinuationFamilyId = Family.FamilyId;
	OutRoute.StartRootEndpoint.SiteReservationKey = ConnectorRecord.StartSiteReservationKey;
	OutRoute.StartRootEndpoint.EndpointBlockWorldPos = ConnectorRecord.StartEndpointBlockWorldPos;
	OutRoute.StartRootEndpoint.ConnectorTypeTag = ConnectorRecord.ConnectorTypeTag;
	OutRoute.StartRootEndpoint.ExposedEntryFaceDirection = ConnectorRecord.StartRootEntryFaceDirection;
	OutRoute.EndRootEndpoint.SiteReservationKey = ConnectorRecord.EndSiteReservationKey;
	OutRoute.EndRootEndpoint.EndpointBlockWorldPos = ConnectorRecord.EndEndpointBlockWorldPos;
	OutRoute.EndRootEndpoint.ConnectorTypeTag = ConnectorRecord.ConnectorTypeTag;
	OutRoute.EndRootEndpoint.ExposedEntryFaceDirection = ConnectorRecord.EndRootEntryFaceDirection;
	OutRoute.CenterlineCells = CenterlineCells;

	auto ResolveDirection = [](const FIntPoint& From, const FIntPoint& To, ELayoutFaceDirection& OutDirection)
	{
		const FIntPoint Delta = To - From;
		if (Delta == FIntPoint(1, 0)) { OutDirection = ELayoutFaceDirection::PosX; return true; }
		if (Delta == FIntPoint(-1, 0)) { OutDirection = ELayoutFaceDirection::NegX; return true; }
		if (Delta == FIntPoint(0, 1)) { OutDirection = ELayoutFaceDirection::PosY; return true; }
		if (Delta == FIntPoint(0, -1)) { OutDirection = ELayoutFaceDirection::NegY; return true; }
		return false;
	};
	auto IsLegalSeam = [&CenterlineCells, &ForbiddenSeamIndices](const int32 LeftIndex)
	{
		// Descriptors use [First..Last] then [Last+1..]. Both endpoints must
		// face across one straight route edge; a turn on either side belongs to
		// one descriptor so expanded corner footprints cannot overlap.
		if (ForbiddenSeamIndices.Contains(LeftIndex)
			|| LeftIndex <= 0 || LeftIndex + 2 >= CenterlineCells.Num())
		{
			return false;
		}
		const FIntPoint BeforeSeam = CenterlineCells[LeftIndex] - CenterlineCells[LeftIndex - 1];
		const FIntPoint AcrossSeam = CenterlineCells[LeftIndex + 1] - CenterlineCells[LeftIndex];
		const FIntPoint AfterSeam = CenterlineCells[LeftIndex + 2] - CenterlineCells[LeftIndex + 1];
		return BeforeSeam == AcrossSeam && AcrossSeam == AfterSeam;
	};
	auto DoesCandidateFit = [&CenterlineCells](
		const FLayoutContinuationCandidateSnapshot* const Profile,
		const int32 FirstIndex,
		const int32 LastIndex,
		FIntPoint& OutFootprintSize,
		FIntPoint& OutMinRouteCell)
	{
		if (!Profile->bHasProfile || FirstIndex < 0 || LastIndex >= CenterlineCells.Num() || FirstIndex >= LastIndex)
		{
			return false;
		}
		const int32 Width = FMath::Max(Profile->MinimumFootprintInCells.X, Profile->MinimumFootprintInCells.Y);
		if (Width <= 0 || Width % 2 == 0)
		{
			return false;
		}
		TArray<FIntPoint> SegmentCells;
		SegmentCells.Reserve(LastIndex - FirstIndex + 1);
		for (int32 Index = FirstIndex; Index <= LastIndex; ++Index)
		{
			SegmentCells.Add(CenterlineCells[Index]);
		}
		TArray<FLayoutPlannedCell> ExpandedCells;
		if (!ExpandContinuationCenterline(SegmentCells, Width, 0, ExpandedCells, OutFootprintSize, OutMinRouteCell))
		{
			return false;
		}
		return OutFootprintSize.X >= Profile->MinimumFootprintInCells.X
			&& OutFootprintSize.Y >= Profile->MinimumFootprintInCells.Y
			&& OutFootprintSize.X <= Profile->MaximumFootprintInCells.X
			&& OutFootprintSize.Y <= Profile->MaximumFootprintInCells.Y;
	};

	auto BuildCandidateOrder = [&](const int32 SegmentIndex)
	{
		const uint64 SegmentSelectionKey = static_cast<uint64>(HashCombineFast(
			GetTypeHash(OutRoute.RouteKey), static_cast<uint32>(SegmentIndex)));
		const int32 PreferredIndex = LayoutWorldBindingRuntimeView::ChooseWeightedWorldBindingContinuationCandidateIndex(
			Family, SegmentSelectionKey, ConnectorRecord.SolveSeed);
		const FLayoutContinuationCandidateSnapshot* Preferred = Family.Candidates.IsValidIndex(PreferredIndex)
			? &Family.Candidates[PreferredIndex] : nullptr;
		TArray<const FLayoutContinuationCandidateSnapshot*> Candidates;
		for (const FLayoutContinuationCandidateSnapshot& Candidate : Family.Candidates)
		{
			Candidates.Add(&Candidate);
		}
		Candidates.Sort([](
			const FLayoutContinuationCandidateSnapshot& Left,
			const FLayoutContinuationCandidateSnapshot& Right)
		{
			return Left.CandidateId.LexicalLess(Right.CandidateId);
		});
		if (Preferred != nullptr)
		{
			Candidates.Remove(Preferred);
			Candidates.Insert(Preferred, 0);
		}
		return Candidates;
	};

	TFunction<bool(int32, int32)> TryPackFrom = [&](const int32 FirstIndex, const int32 SegmentIndex)
	{
		if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested()) return false;
		if (FirstIndex == CenterlineCells.Num())
		{
			return true;
		}
		for (int32 LastIndex = CenterlineCells.Num() - 1; LastIndex > FirstIndex; --LastIndex)
		{
			if (LastIndex < CenterlineCells.Num() - 1 && !IsLegalSeam(LastIndex))
			{
				continue;
			}

			ELayoutFaceDirection FirstDirection;
			ELayoutFaceDirection LastDirection;
			if (!ResolveDirection(CenterlineCells[FirstIndex], CenterlineCells[FirstIndex + 1], FirstDirection)
				|| !ResolveDirection(CenterlineCells[LastIndex - 1], CenterlineCells[LastIndex], LastDirection))
			{
				OutFailureReason = TEXT("Continuation route centerline contains a non-cardinal segment.");
				return false;
			}

			for (const FLayoutContinuationCandidateSnapshot* const Candidate : BuildCandidateOrder(SegmentIndex))
			{
				const FLayoutContinuationCandidateSnapshot* const Profile = Candidate;
				FIntPoint FootprintSize;
				FIntPoint MinRouteCell;
				if (!DoesCandidateFit(Profile, FirstIndex, LastIndex, FootprintSize, MinRouteCell))
				{
					continue;
				}

				FLayoutContinuationSegmentDescriptor Segment;
				Segment.SegmentIndex = SegmentIndex;
				Segment.FirstRouteCellIndex = FirstIndex;
				Segment.LastRouteCellIndex = LastIndex;
				Segment.CandidateId = Candidate->CandidateId;
				Segment.LayoutProfile = Profile->ProfilePath;
				Segment.FootprintSize = FootprintSize;
				Segment.MinRouteCell = MinRouteCell;
				Segment.IngressCell = FIntVector(
					CenterlineCells[FirstIndex].X - MinRouteCell.X,
					CenterlineCells[FirstIndex].Y - MinRouteCell.Y,
					0);
				Segment.EgressCell = FIntVector(
					CenterlineCells[LastIndex].X - MinRouteCell.X,
					CenterlineCells[LastIndex].Y - MinRouteCell.Y,
					0);
				Segment.IngressFaceDirection = FLayoutDirectionUtils::GetOpposite(FirstDirection);
				Segment.EgressFaceDirection = LastDirection;
				OutRoute.Segments.Add(MoveTemp(Segment));
				// Descriptors own adjacent, non-overlapping cells. Their connection
				// contract belongs to boundary faces, not a duplicate stamped seam cell.
				if (TryPackFrom(LastIndex + 1, SegmentIndex + 1))
				{
					return true;
				}
				OutRoute.Segments.Pop();
			}
		}
		return false;
	};

	if (!TryPackFrom(0, 0))
	{
		OutRoute = FLayoutContinuationRouteRecord();
		OutFailureReason = TEXT("No continuation family candidate packing fits the route within profile bounds and legal seams.");
		return false;
	}

	return true;
}

TArray<FResolvedLayoutConnectorEndpoint> FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(
	const FIntPoint& SiteReservationKey,
	const FResolvedLayoutSiteRecord& SiteRecord)
{
	TArray<FResolvedLayoutConnectorEndpoint> Endpoints;
	const FResolvedLayoutSiteLocationMetadata LocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection =
		SiteRecord.GetSiteSolveSourceSelection();
	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	const ULayoutRegionContentSetAsset* const ContentSet =
		SiteSolveSourceSelection.ContentSet.LoadSynchronous();
	const FIntVector SharedCellSizeInBlocks = ResolveConnectorSharedCellSizeInBlocks(SolvedPayload.SolveResult, ContentSet);
	const FResolvedLayoutSiteRuntimeState RuntimeState = SiteRecord.GetResolvedSiteRuntimeState();
	// Debug stamping is not a regional proof. Failed/partial roots must not become
	// normal continuation authorities merely because their geometry was applied.
	const bool bHasExportableLayout = RuntimeState.bLayoutSolved;
	if (!bHasExportableLayout
		|| SharedCellSizeInBlocks == FIntVector::ZeroValue
		|| SolvedPayload.ExportedEntryCells.IsEmpty()
		|| SolvedPayload.ExportedConnectorTypeTags.IsEmpty())
	{
		return Endpoints;
	}

	const FIntVector FootprintMin = ComputeSiteFootprintMinBlockWorldPos(SiteRecord, SharedCellSizeInBlocks);

	// Pre-scan solved placements to build a cell-to-face-direction map.
	// Each entry cell has a single outward face with FaceEntry tag.
	TMap<FIntVector, ELayoutFaceDirection> EntryFaceByCell;
	for (const FLayoutPlacedModule& Placement : SolvedPayload.SolveResult.Placements)
	{
		if (Placement.Module == nullptr)
		{
			continue;
		}
		for (int32 FaceIdx = 0; FaceIdx < 6; ++FaceIdx)
		{
			const ELayoutFaceDirection FaceDir = static_cast<ELayoutFaceDirection>(FaceIdx);
			const FLayoutFaceRule* const FaceRule =
				Placement.Module->FaceRules.FindRule(FaceDir);
			if (FaceRule != nullptr
				&& FaceRule->GetEffectiveConnectionTags().HasTagExact(LayoutGameplayTags::FaceEntry))
			{
				EntryFaceByCell.Add(
					Placement.Cell,
					FLayoutDirectionUtils::RotateYaw(FaceDir, Placement.YawRotationSteps));
				break;
			}
		}
	}

	TMap<FIntVector, ELayoutEntryOrigin> EntryOriginByCell;
	for (const FLayoutPlannedCell& PlannedCell : SolvedPayload.SolveResult.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry)
		{
			EntryOriginByCell.Add(PlannedCell.Cell, PlannedCell.EntryOrigin);
		}
	}

	for (const FIntVector& LocalCell : SolvedPayload.ExportedEntryCells)
	{
		// Internal child and retaining-terrain gates must never enter root continuation ledger.
		const ELayoutEntryOrigin* const EntryOrigin = EntryOriginByCell.Find(LocalCell);
		if (EntryOrigin != nullptr
			&& (*EntryOrigin == ELayoutEntryOrigin::TerrainSeam
				|| *EntryOrigin == ELayoutEntryOrigin::ChildContract))
		{
			continue;
		}
		const int32* const StageBaseZ =
			SiteRecord.CachedFrozenTerrainBaseZByColumn.Find(FIntPoint(LocalCell.X, LocalCell.Y));
		const int32 EndpointCenterZ = (StageBaseZ != nullptr ? *StageBaseZ : FootprintMin.Z)
			+ LocalCell.Z * SharedCellSizeInBlocks.Z
			+ SolvedPayload.SolveResult.TemplatePlacementZOffsetBlocks
			+ SharedCellSizeInBlocks.Z / 2;
		const FIntVector EndpointBlockWorldPos = FIntVector(
			FootprintMin.X + LocalCell.X * SharedCellSizeInBlocks.X + SharedCellSizeInBlocks.X / 2,
			FootprintMin.Y + LocalCell.Y * SharedCellSizeInBlocks.Y + SharedCellSizeInBlocks.Y / 2,
			EndpointCenterZ);

		for (const FGameplayTag& ConnectorTypeTag : SolvedPayload.ExportedConnectorTypeTags)
		{
			FResolvedLayoutConnectorEndpoint& Endpoint = Endpoints.AddDefaulted_GetRef();
			Endpoint.SiteReservationKey = SiteReservationKey;
			Endpoint.SiteCenterBlockWorldPos = LocationMetadata.SiteCenterBlockWorldPos;
			Endpoint.LocalCell = LocalCell;
			Endpoint.EndpointBlockWorldPos = EndpointBlockWorldPos;
			Endpoint.ConnectorTypeTag = ConnectorTypeTag;
			if (const ELayoutFaceDirection* const FaceDir = EntryFaceByCell.Find(LocalCell))
			{
				Endpoint.ExposedEntryFaceDirection = *FaceDir;
			}
		}
	}

	return Endpoints;
}

bool FLayoutConnectorPlanning::ValidateContinuationClearance(
	const FIntVector& PathOriginBlockWorldPos,
	const FIntVector& CellSize,
	const TConstArrayView<FLayoutPlannedCell> PlannedCells,
	const TConstArrayView<FLayoutRootSpacingReservation> RootFootprints,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (CellSize.X <= 0 || CellSize.Y <= 0 || PlannedCells.IsEmpty())
	{
		OutFailureReason = TEXT("Continuation clearance requires positive cell dimensions and planned cells.");
		return false;
	}
	for (const FLayoutPlannedCell& Cell : PlannedCells)
	{
		const int64 MinX = int64(PathOriginBlockWorldPos.X) - CellSize.X / 2 + int64(Cell.Cell.X) * CellSize.X;
		const int64 MinY = int64(PathOriginBlockWorldPos.Y) - CellSize.Y / 2 + int64(Cell.Cell.Y) * CellSize.Y;
		for (const FLayoutRootSpacingReservation& Root : RootFootprints)
		{
			if (MinX <= Root.Max.X && MinX + CellSize.X - 1 >= Root.Min.X
				&& MinY <= Root.Max.Y && MinY + CellSize.Y - 1 >= Root.Min.Y)
			{
				OutFailureReason = FString::Printf(
					TEXT("Continuation corridor intersects a root layout footprint at cell %s; routing around buildings is unsupported."),
					*Cell.Cell.ToString());
				return false;
			}
		}
	}
	return true;
}

bool FLayoutConnectorPlanning::TryBuildContinuationRecordForEndpointPair(
	const FResolvedLayoutConnectorEndpoint& Start,
	const FResolvedLayoutConnectorEndpoint& End,
	const ULayoutWorldBindingAsset* const WorldBinding,
	const ULayoutProfileAsset* const ContinuationProfile,
	const int32 WorldSeed,
	FResolvedLayoutConnectorRecord& OutRecord,
	FString& OutFailureReason)
{
	OutRecord = FResolvedLayoutConnectorRecord();
	OutFailureReason.Reset();
	if (WorldBinding == nullptr || ContinuationProfile == nullptr)
	{
		OutFailureReason = TEXT("Continuation endpoint-pair planning requires a world binding and continuation profile.");
		return false;
	}
	const bool bHasStableRootIdentity = !Start.RootRecordKey.IsEmpty()
		&& !End.RootRecordKey.IsEmpty();
	const bool bSameResolvedSite = bHasStableRootIdentity
		? Start.RootRecordKey == End.RootRecordKey
		: Start.SiteReservationKey == End.SiteReservationKey;
	if (bSameResolvedSite)
	{
		OutFailureReason = TEXT("Continuation endpoints must come from different resolved sites.");
		return false;
	}
	if (!Start.ConnectorTypeTag.IsValid() || Start.ConnectorTypeTag != End.ConnectorTypeTag)
	{
		OutFailureReason = TEXT("Continuation endpoints must export the same valid connector tag.");
		return false;
	}
	FIntPoint StartPortalOffset;
	FIntPoint EndPortalOffset;
	if (!TryGetHorizontalEntryPortalOffset(Start.ExposedEntryFaceDirection, StartPortalOffset)
		|| !TryGetHorizontalEntryPortalOffset(End.ExposedEntryFaceDirection, EndPortalOffset))
	{
		OutFailureReason = FString::Printf(
			TEXT("Continuation endpoints require horizontal cardinal entry faces. Start=%s End=%s."),
			*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Start.ExposedEntryFaceDirection)),
			*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(End.ExposedEntryFaceDirection)));
		return false;
	}

	FLayoutWorldBindingRuntimeView RuntimeView;
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	if (!LayoutWorldBindingRuntimeView::TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
			WorldBinding,
			const_cast<ULayoutProfileAsset*>(ContinuationProfile),
			RuntimeView,
			FrontendSelection,
			OutFailureReason))
	{
		return false;
	}
	if (RuntimeView.ContinuationSelection.FamilyId.IsNone())
	{
		OutFailureReason = TEXT("Selected profile resolves as an ordinary root, not a continuation candidate.");
		return false;
	}

	const FLayoutWorldBindingContinuationFamily* Family = nullptr;
	int32 FamilyIndex = INDEX_NONE;
	for (int32 Index = 0; Index < WorldBinding->ContinuationFamilies.Num(); ++Index)
	{
		if (WorldBinding->ContinuationFamilies[Index].FamilyId == RuntimeView.ContinuationSelection.FamilyId)
		{
			Family = &WorldBinding->ContinuationFamilies[Index];
			FamilyIndex = Index;
			break;
		}
	}
	if (Family == nullptr || Family->EndpointConnectorTypeTag != Start.ConnectorTypeTag)
	{
		OutFailureReason = TEXT("Selected continuation family does not accept the exported endpoint tag.");
		return false;
	}

	const uint64 PairKey = BuildConnectorPairKey(Start.SiteReservationKey, End.SiteReservationKey);
	const FLayoutWorldBindingContinuationCandidate* const CandidateTarget =
		Family->Candidates.FindByPredicate([ContinuationProfile](const FLayoutWorldBindingContinuationCandidate& Candidate)
		{
			return Candidate.LayoutProfile == ContinuationProfile;
		});
	if (CandidateTarget == nullptr)
	{
		OutFailureReason = TEXT("Selected continuation profile is not a candidate in the selected family.");
		return false;
	}

	const FIntVector SharedCellSize = WorldBinding->BaseCellDimensionsBlocks;
	if (SharedCellSize.X <= 0 || SharedCellSize.Y <= 0 || Family->MaxConnectionsPerSite <= 0)
	{
		OutFailureReason = TEXT("Continuation family requires positive shared cell dimensions and a positive per-site connection limit.");
		return false;
	}
	const FIntVector Delta = Start.EndpointBlockWorldPos - End.EndpointBlockWorldPos;
	const int64 DistanceSquared = static_cast<int64>(Delta.X) * Delta.X
		+ static_cast<int64>(Delta.Y) * Delta.Y
		+ static_cast<int64>(Delta.Z) * Delta.Z;
	const int64 MaxDistanceBlocks = static_cast<int64>(Family->MaxConnectionDistanceInCells)
		* FMath::Max(SharedCellSize.X, SharedCellSize.Y);
	if (DistanceSquared > MaxDistanceBlocks * MaxDistanceBlocks)
	{
		OutFailureReason = TEXT("Continuation endpoint pair exceeds the family's maximum connection distance.");
		return false;
	}

	ULayoutRegionContentSetAsset* const ContentSet =
		ResolveConnectorPreferredContentSet(const_cast<ULayoutProfileAsset*>(ContinuationProfile));
	const FName SharedBiomeRowName = WorldBinding->BiomeRowNames.Num() == 1
		? WorldBinding->BiomeRowNames[0]
		: NAME_None;
	OutRecord = BuildContinuationConnectorRecord(
		Start,
		End,
		WorldBinding,
		*Family,
		CandidateTarget->CandidateId,
		ContinuationProfile,
		ContentSet,
		SharedBiomeRowName,
		WorldSeed,
		PairKey);
	return true;
}

namespace
{
bool FindConnectorPathCellsPrivate(
	const FInternalConnectorPathQuery& Query,
	const TMap<FIntPoint, FInternalConnectorPathCell>& CellsByGrid,
	TArray<FIntPoint>& OutPathCells)
{
	OutPathCells.Reset();
	const FInternalConnectorPathCell* const StartCell = CellsByGrid.Find(Query.StartCell);
	const FInternalConnectorPathCell* const EndCell = CellsByGrid.Find(Query.EndCell);
	if (StartCell == nullptr
		|| EndCell == nullptr
		|| StartCell->Type != EInternalConnectorPathCellType::Walkable
		|| EndCell->Type != EInternalConnectorPathCellType::Walkable)
	{
		return false;
	}

	struct FSearchNode
	{
		FIntPoint Cell = FIntPoint::ZeroValue;
		int32 CostFromStart = 0;
		int32 EstimatedTotalCost = 0;
		int32 ConsecutiveGapCount = 0;
	};

	TArray<FSearchNode> OpenSet;
	TSet<FIntPoint> ClosedSet;
	TMap<FIntPoint, int32> BestCostByCell;
	TMap<FIntPoint, FIntPoint> ParentByCell;

	FSearchNode StartNode;
	StartNode.Cell = Query.StartCell;
	StartNode.EstimatedTotalCost = GetManhattanDistance(Query.StartCell, Query.EndCell) * 10;
	OpenSet.Add(StartNode);
	BestCostByCell.Add(Query.StartCell, 0);

	static const TArray<FIntPoint> NeighborOffsets = {
		FIntPoint(1, 0),
		FIntPoint(0, 1),
		FIntPoint(-1, 0),
		FIntPoint(0, -1)
	};

	while (!OpenSet.IsEmpty())
	{
		int32 BestOpenIndex = 0;
		for (int32 Index = 1; Index < OpenSet.Num(); ++Index)
		{
			const FSearchNode& Candidate = OpenSet[Index];
			const FSearchNode& Best = OpenSet[BestOpenIndex];
			if (Candidate.EstimatedTotalCost < Best.EstimatedTotalCost
				|| (Candidate.EstimatedTotalCost == Best.EstimatedTotalCost && Candidate.CostFromStart < Best.CostFromStart)
				|| (Candidate.EstimatedTotalCost == Best.EstimatedTotalCost && Candidate.CostFromStart == Best.CostFromStart && Candidate.Cell.Y < Best.Cell.Y)
				|| (Candidate.EstimatedTotalCost == Best.EstimatedTotalCost && Candidate.CostFromStart == Best.CostFromStart && Candidate.Cell.Y == Best.Cell.Y && Candidate.Cell.X < Best.Cell.X))
			{
				BestOpenIndex = Index;
			}
		}

		const FSearchNode Current = OpenSet[BestOpenIndex];
		OpenSet.RemoveAtSwap(BestOpenIndex, 1, EAllowShrinking::No);
		if (ClosedSet.Contains(Current.Cell))
		{
			continue;
		}

		if (Current.Cell == Query.EndCell)
		{
			TArray<FIntPoint> ReversedPath;
			FIntPoint Cursor = Query.EndCell;
			ReversedPath.Add(Cursor);
			while (Cursor != Query.StartCell)
			{
				const FIntPoint* const Parent = ParentByCell.Find(Cursor);
				if (Parent == nullptr)
				{
					return false;
				}
				Cursor = *Parent;
				ReversedPath.Add(Cursor);
			}

			for (int32 Index = ReversedPath.Num() - 1; Index >= 0; --Index)
			{
				OutPathCells.Add(ReversedPath[Index]);
			}
			return true;
		}

		ClosedSet.Add(Current.Cell);
		for (const FIntPoint& NeighborOffset : NeighborOffsets)
		{
			const FIntPoint Neighbor = Current.Cell + NeighborOffset;
			if (Neighbor.X < Query.MinCell.X
				|| Neighbor.X > Query.MaxCell.X
				|| Neighbor.Y < Query.MinCell.Y
				|| Neighbor.Y > Query.MaxCell.Y
				|| ClosedSet.Contains(Neighbor))
			{
				continue;
			}

			int32 ConsecutiveGapCount = 0;
			int32 MoveCost = 0;
			if (!CanEnterCell(Current.Cell, Neighbor, Current.ConsecutiveGapCount, Query, CellsByGrid, ConsecutiveGapCount, MoveCost))
			{
				continue;
			}

			const int32 NewCost = Current.CostFromStart + MoveCost;
			const int32 ExistingBestCost = BestCostByCell.FindRef(Neighbor);
			if (BestCostByCell.Contains(Neighbor) && NewCost >= ExistingBestCost)
			{
				continue;
			}

			BestCostByCell.Add(Neighbor, NewCost);
			ParentByCell.Add(Neighbor, Current.Cell);

			FSearchNode& NextNode = OpenSet.AddDefaulted_GetRef();
			NextNode.Cell = Neighbor;
			NextNode.CostFromStart = NewCost;
			NextNode.EstimatedTotalCost = NewCost + GetManhattanDistance(Neighbor, Query.EndCell) * 10;
			NextNode.ConsecutiveGapCount = ConsecutiveGapCount;
		}
	}

	return false;
}
} // namespace

#if WITH_AUTOMATION_TESTS
bool FLayoutConnectorPlanning::FindConnectorPathCells(
	const FLayoutConnectorPathQuery& Query,
	const TMap<FIntPoint, FLayoutConnectorPathCell>& CellsByGrid,
	TArray<FIntPoint>& OutPathCells)
{
	FInternalConnectorPathQuery InternalQuery;
	InternalQuery.StartCell = Query.StartCell;
	InternalQuery.EndCell = Query.EndCell;
	InternalQuery.MinCell = Query.MinCell;
	InternalQuery.MaxCell = Query.MaxCell;
	InternalQuery.MaxSlopeBlocks = Query.MaxSlopeBlocks;
	InternalQuery.MaxBridgeGapCells = Query.MaxBridgeGapCells;
	InternalQuery.bAllowSteepTerrainTunneling = Query.bAllowSteepTerrainTunneling;

	TMap<FIntPoint, FInternalConnectorPathCell> InternalCellsByGrid;
	for (const TPair<FIntPoint, FLayoutConnectorPathCell>& Pair : CellsByGrid)
	{
		FInternalConnectorPathCell& InternalCell = InternalCellsByGrid.Add(Pair.Key);
		switch (Pair.Value.Type)
		{
		case ELayoutConnectorPathCellType::Walkable:
			InternalCell.Type = EInternalConnectorPathCellType::Walkable;
			break;
		case ELayoutConnectorPathCellType::Gap:
			InternalCell.Type = EInternalConnectorPathCellType::Gap;
			break;
		case ELayoutConnectorPathCellType::Blocked:
		default:
			InternalCell.Type = EInternalConnectorPathCellType::Blocked;
			break;
		}
		InternalCell.SurfaceZBlockWorld = Pair.Value.SurfaceZBlockWorld;
	}

	return FindConnectorPathCellsPrivate(InternalQuery, InternalCellsByGrid, OutPathCells);
}
#endif

#if WITH_AUTOMATION_TESTS
bool FLayoutConnectorPlanning::ExpandContinuationCenterlineForTesting(
	const TArray<FIntPoint>& CenterlineCells,
	const int32 CorridorWidthInCells,
	const int32 StructuralSolveLevel,
	TArray<FLayoutPlannedCell>& OutPlannedCells,
	FIntPoint& OutFootprintSize,
	FIntPoint& OutMinCell)
{
	return ExpandContinuationCenterline(
		CenterlineCells,
		CorridorWidthInCells,
		StructuralSolveLevel,
		OutPlannedCells,
		OutFootprintSize,
		OutMinCell);
}
#endif
