// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Layout/Solver/LayoutStandaloneRegionRequestBuilder.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

namespace LayoutWorldBindingSolveRequestBuilder
{
	namespace
	{
		bool HasCompletePlanningWindowRootPublicationMetadata(
			const FLayoutRootPublicationMetadata& PublicationMetadata)
		{
			return !PublicationMetadata.RootPlacementPolicyId.IsNone()
				&& !PublicationMetadata.RootCandidateId.IsNone()
				&& !PublicationMetadata.RootSolveId.IsNone();
		}

		bool ShouldDeriveSteppedTerrainSupport(const FLayoutWorldBindingRuntimeView& RuntimeView)
		{
			return RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None
				&& RuntimeView.LayoutProfile != nullptr
				&& RuntimeView.LayoutProfile->bSupportsSteppedTerrainSolve;
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

		FLayoutValidationAssertionRecord MakeWorldPlacementLatticeAssertion(
			const FLayoutWorldBindingRuntimeView& RuntimeView,
			const FPlannedLayoutSiteRecord& PlannedSiteRecord)
		{
			const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
				PlannedSiteRecord.GetWorldBindingFrontendSelection();
			const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
				PlannedSiteRecord.GetPlannedSiteReservationSourceSelection();
			const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
				PlannedSiteRecord.GetPlannedSiteLifecycleMetadata();
			const bool bWorldFacingPlacement =
				RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None;
			const FIntVector SharedCellSizeInBlocks = RuntimeView.SharedCellSizeInBlocks;
			const bool bHasUsableLattice =
				SharedCellSizeInBlocks.X > 0
				|| SharedCellSizeInBlocks.Y > 0
				|| SharedCellSizeInBlocks.Z > 0;
			const bool bPassed =
				!bWorldFacingPlacement
				|| !bHasUsableLattice
				|| ReservationSourceSelection.SiteCenterBlockWorldPos
					== FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
						ReservationSourceSelection.SiteCenterBlockWorldPos,
						SharedCellSizeInBlocks);

			return MakeSnapshotAssertionRecord(
				TEXT("RegionRequest.WorldPlacementLatticeContractValid"),
				ELayoutValidationAssertionKind::RequestContractValid,
				bPassed,
				{FrontendSelection.WorldBindingId, FrontendSelection.WorldBindingCandidateId},
				bPassed
					? FString()
					: FString::Printf(
						TEXT("World-binding planned site '%s' uses site center %s, which does not land on the binding-owned shared-cell lattice for cell size %s. World-facing root requests must start on integer multiples of the binding-owned cell size before solve execution begins."),
						*LifecycleMetadata.StableRecordKey,
						*ReservationSourceSelection.SiteCenterBlockWorldPos.ToString(),
						*SharedCellSizeInBlocks.ToString()));
		}

		void PopulateWorldBindingFrontendStubs(
			const FLayoutWorldBindingRuntimeView& RuntimeView,
			const FLayoutResolvedWorldBindingContinuationSelection* ContinuationSelectionOverride,
			FLayoutRegionSolveRequest& OutSolveRequest)
		{
			OutSolveRequest.WorldBindingId = RuntimeView.BindingId;
			OutSolveRequest.RootPlacementKind = RuntimeView.PlacementKind;
			OutSolveRequest.WorldBindingPlacementPolicy = RuntimeView.PlacementPolicy;
			OutSolveRequest.RootContinuationSelection =
				ContinuationSelectionOverride != nullptr
					? *ContinuationSelectionOverride
					: RuntimeView.ContinuationSelection;
		}

		FIntVector ComputePlanningWindowFootprintAnchorBlockWorldPos(
			const FIntVector& SiteCenterBlockWorldPos,
			const FIntPoint& FootprintSize,
			const FIntVector& SharedCellSizeInBlocks)
		{
			return FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
				SiteCenterBlockWorldPos,
				FootprintSize,
				SharedCellSizeInBlocks);
		}

	}

	FLayoutSolverExecutionSettings BuildExecutionSettingsFromSolveBudget(const FLayoutRootSolveBudgetSettings& SolveBudget)
	{
		FLayoutSolverExecutionSettings Settings;
		Settings.MaxSolveDurationSeconds = SolveBudget.MaxSolveDurationSeconds;
		return Settings;
	}

	bool TryBuildStandaloneSolveRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const int32 SolveSeed,
		const FString& RegionDebugPath,
		const FLayoutId RootPlacementPolicyId,
		const FLayoutId RootCandidateId,
		const FLayoutId RootSolveId,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason)
	{
		OutSolveRequest = FLayoutRegionSolveRequest();
		OutFailureReason.Reset();

		ULayoutProfileAsset* const LayoutProfile = RuntimeView.LayoutProfile;
		ULayoutRegionContentSetAsset* const LayoutContentSet = RuntimeView.ContentSet;
		if (LayoutProfile == nullptr)
		{
			OutFailureReason = TEXT("World-binding solve request requires a layout profile.");
			return false;
		}

		if (LayoutContentSet != LayoutProfile->ContentSet.Get())
		{
			OutFailureReason = FString::Printf(
				TEXT("World-binding solve request content set '%s' must match profile '%s' owned content set '%s'."),
				*GetNameSafe(LayoutContentSet),
				*LayoutProfile->GetName(),
				*GetNameSafe(LayoutProfile->ContentSet.Get()));
			return false;
		}

		if (LayoutContentSet != nullptr)
		{
			const int32 SnapshotSchemaVersion = FLayoutRegionSolveRequest().SnapshotSchemaVersion;
			OutSolveRequest = LayoutStandaloneRegionRequestBuilder::BuildRequestFromContentSet(
				SnapshotSchemaVersion,
				LayoutContentSet,
				LayoutProfile,
				SolveSeed,
				RegionDebugPath,
				BuildExecutionSettingsFromSolveBudget(RuntimeView.SolveBudget),
				RootPlacementPolicyId,
				RootCandidateId,
				RuntimeView.TemplatePlacementZOffsetBlocks,
				RootSolveId,
				RuntimeView.PlacementKind,
				RuntimeView.PlacementPolicy,
				nullptr,
				&RuntimeView.SharedCellSizeInBlocks);
			PopulateWorldBindingFrontendStubs(RuntimeView, nullptr, OutSolveRequest);
			return true;
		}

		OutFailureReason = TEXT("World-binding solve request requires a content set.");
		return false;
	}

	bool TryBuildPlanningWindowSolveRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason)
	{
		const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
			PlannedSiteRecord.GetWorldBindingFrontendSelection();
		const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
			PlannedSiteRecord.GetPlannedSiteReservationSourceSelection();
		const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection =
			PlannedSiteRecord.GetSiteSolveSourceSelection();
		if (PlannedSiteRecord.DiscoverySolveSeed != 0
			&& PlannedSiteRecord.DiscoverySolveSeed != SiteSolveSourceSelection.SolveSeed)
		{
			OutFailureReason = TEXT("Planning-window solve request shifted branch seed does not match planned-site solve source seed.");
			return false;
		}
		const FLayoutRootPublicationMetadata PublicationMetadata =
			PlannedSiteRecord.GetRootPublicationMetadata();
		if (!HasCompletePlanningWindowRootPublicationMetadata(PublicationMetadata))
		{
			OutFailureReason =
				TEXT("Planning-window solve request requires authored root publication metadata.");
			return false;
		}
		const FString RegionDebugPath = FString::Printf(
			TEXT("PlanningWindow/%s/%s/%s"),
			*FrontendSelection.WorldBindingId.ToString(),
			*FrontendSelection.WorldBindingCandidateId.ToString(),
			*ReservationSourceSelection.SiteCenterBlockWorldPos.ToString());
		return TryBuildStandaloneSolveRequest(
			RuntimeView,
			SiteSolveSourceSelection.SolveSeed,
			RegionDebugPath,
			PublicationMetadata.RootPlacementPolicyId,
			PublicationMetadata.RootCandidateId,
			PublicationMetadata.RootSolveId,
			OutSolveRequest,
			OutFailureReason)
			? (PopulateWorldBindingFrontendStubs(
					RuntimeView,
					&FrontendSelection.ResolvedContinuationSelection,
					OutSolveRequest),
				OutSolveRequest.bHasRootPlacementSubmission = true,
				OutSolveRequest.RootPlacementShiftId = PlannedSiteRecord.DiscoveryPlacementShiftId,
				OutSolveRequest.RootPlacementShiftCells = PlannedSiteRecord.DiscoveryPlacementShiftCells,
				OutSolveRequest.RootSiteCenterBlockWorldPos = ReservationSourceSelection.SiteCenterBlockWorldPos,
				OutSolveRequest.RootReservationKey = PlannedSiteRecord.ReservationKey,
				OutSolveRequest.ValidationAssertions.Add(
					MakeWorldPlacementLatticeAssertion(RuntimeView, PlannedSiteRecord)),
				true)
			: false;
	}

	bool TryBuildPlanningWindowSteppedTerrainSupportMap(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		OutSupportMap = FLayoutSteppedTerrainSupportMap();
		OutFailureReason.Reset();
		const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
			PlannedSiteRecord.GetWorldBindingFrontendSelection();
		const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
			PlannedSiteRecord.GetPlannedSiteReservationSourceSelection();

		if (FrontendSelection.BiomeRowName.IsNone())
		{
			OutFailureReason = TEXT("Planning-window stepped terrain support map requires one planned-site biome row name.");
			return false;
		}

		const FIntVector AnchorBlockWorldPos = ComputePlanningWindowFootprintAnchorBlockWorldPos(
			ReservationSourceSelection.SiteCenterBlockWorldPos,
			FootprintSize,
			RuntimeView.SharedCellSizeInBlocks);
		return FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
			AnchorBlockWorldPos,
			RuntimeView.SharedCellSizeInBlocks,
			RuntimeView.SharedCellSizeInBlocks.X,
			PlannedCells,
			RuntimeView.PlacementPolicy.SurfaceSearch,
			CoordinateSettings,
			ActiveBiomeSampler,
			OutSupportMap,
			OutFailureReason);
	}

	bool TryPopulatePlanningWindowSteppedTerrainSupportOnRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& InOutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		FLayoutSteppedTerrainSupportMap SupportMap;
		if (!TryBuildPlanningWindowSteppedTerrainSupportMap(
			RuntimeView,
			PlannedSiteRecord,
			FootprintSize,
			PlannedCells,
			CoordinateSettings,
			ActiveBiomeSampler,
			SupportMap,
			OutFailureReason,
			World))
		{
			return false;
		}

		// The prewarm adapter handles cell derivation; preserve supplied cells here.
		InOutSolveRequest.FootprintSize = FootprintSize;
		InOutSolveRequest.PlannedCells = PlannedCells;
		InOutSolveRequest.SteppedTerrainSupportMap = SupportMap;
		const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
			PlannedSiteRecord.GetWorldBindingFrontendSelection();
		const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
			PlannedSiteRecord.GetPlannedSiteReservationSourceSelection();
		const FIntVector AnchorBlockWorldPos = ComputePlanningWindowFootprintAnchorBlockWorldPos(
			ReservationSourceSelection.SiteCenterBlockWorldPos,
			FootprintSize,
			RuntimeView.SharedCellSizeInBlocks);
		const FName FrozenMatchingBiomeRowName = !RuntimeView.MatchingBiomeRowName.IsNone()
			? RuntimeView.MatchingBiomeRowName
			: FrontendSelection.BiomeRowName;
		InOutSolveRequest.bHasFrozenTerrainBiomeAdapterInput = true;
		InOutSolveRequest.FrozenTerrainBiomeAdapterInput =
			FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
				ReservationSourceSelection.SiteCenterBlockWorldPos,
				AnchorBlockWorldPos,
				RuntimeView.SharedCellSizeInBlocks,
				FIntPoint(
					FootprintSize.X * RuntimeView.SharedCellSizeInBlocks.X,
					FootprintSize.Y * RuntimeView.SharedCellSizeInBlocks.Y),
				RuntimeView.PlacementPolicy.SurfaceSearch,
				RuntimeView.PlacementPolicy.TerrainTransition,
				CoordinateSettings,
				RuntimeView.SharedCellSizeInBlocks.X,
				FrozenMatchingBiomeRowName,
				RuntimeView.CompatibleBiomeRowNames,
				InOutSolveRequest.SteppedTerrainSupportMap);
		LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(InOutSolveRequest);
		return true;
	}

	bool TryBuildPlanningWindowSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		if (!TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedSiteRecord,
			OutSolveRequest,
			OutFailureReason))
		{
			return false;
		}

		return TryPopulatePlanningWindowSteppedTerrainSupportOnRequest(
			RuntimeView,
			PlannedSiteRecord,
			FootprintSize,
			PlannedCells,
			CoordinateSettings,
			ActiveBiomeSampler,
			OutSolveRequest,
			OutFailureReason,
			World);
	}

	bool TryBuildPlanningWindowSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		// Build once so terrain prewarm consumes request-owned selected footprint.
		if (!TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedSiteRecord,
			OutSolveRequest,
			OutFailureReason))
		{
			return false;
		}

		if (!RuntimeView.LayoutProfile || !RuntimeView.LayoutProfile->bSupportsSteppedTerrainSolve)
		{
			return true;
		}

		const FIntPoint FootprintSize = OutSolveRequest.FootprintSize;
		TArray<FLayoutPlannedCell> PlannedCells;
		PlannedCells.Reserve(FootprintSize.X * FootprintSize.Y);
		for (int32 Y = 0; Y < FootprintSize.Y; ++Y)
		{
			for (int32 X = 0; X < FootprintSize.X; ++X)
			{
				const bool bBoundary = X == 0 || Y == 0 || X == FootprintSize.X - 1 || Y == FootprintSize.Y - 1;
				PlannedCells.Add({FIntVector(X, Y, 0), bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior});
			}
		}
		return TryPopulatePlanningWindowSteppedTerrainSupportOnRequest(
			RuntimeView,
			PlannedSiteRecord,
			FootprintSize,
			PlannedCells,
			CoordinateSettings,
			ActiveBiomeSampler,
			OutSolveRequest,
			OutFailureReason,
			World);
	}

}
