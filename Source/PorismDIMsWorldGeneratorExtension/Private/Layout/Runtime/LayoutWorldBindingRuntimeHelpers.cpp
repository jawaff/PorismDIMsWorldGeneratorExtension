// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

const FLayoutRegionContentEntry* LayoutWorldBindingRuntimeHelpers::FindFrozenPlacementSourceEntry(
	const ULayoutRegionContentSetAsset* ContentSet, const FLayoutPlacedModule& Placement)
{
	if (ContentSet == nullptr || Placement.SourceContentEntryId.IsNone()) return nullptr;
	const FLayoutRegionContentEntry* Match = nullptr;
	const UObject* MatchedAsset = nullptr;
	bool bAmbiguous = false;
	TSet<const ULayoutRegionContentSetAsset*> Visited;
	TFunction<void(const ULayoutRegionContentSetAsset*)> Visit = [&](const ULayoutRegionContentSetAsset* Set)
	{
		if (Set == nullptr || Visited.Contains(Set)) return;
		Visited.Add(Set);
		for (const FLayoutRegionContentEntry& Entry : Set->Entries)
		{
			if (Entry.ContentKind == ELayoutRegionContentKind::ChildRegion)
			{
				if (Entry.ChildRegionSettings.RegionProfile)
					Visit(Entry.ChildRegionSettings.RegionProfile->ContentSet);
				continue;
			}
			if (Entry.ContentKind != ELayoutRegionContentKind::Module || Entry.EntryId != Placement.SourceContentEntryId) continue;
			const auto* Module = Entry.ModuleSettings.Module.Get();
			const auto* Composite = Entry.ModuleSettings.CompositeModule.Get();
			const UObject* Asset = Module ? static_cast<const UObject*>(Module) : static_cast<const UObject*>(Composite);
			if (Asset == nullptr) continue;
			if (!Placement.ModuleSnapshotId.IsNone()
				&& Placement.ModuleSnapshotId != Asset->GetFName()
				&& Placement.ModuleSnapshotId != FLayoutId(*(Asset->GetName() + TEXT(".Entry.") + Entry.EntryId.ToString()))) continue;
			if (Placement.TemplatePath.IsValid()
				&& (Module == nullptr || Module->Template.ToSoftObjectPath() != Placement.TemplatePath)) continue;
			if (Placement.OccupiedLocalCells.Num() > 1 && !Placement.LocalCellFaceRules.IsEmpty())
			{
				if (Composite == nullptr || Composite->Cells.Num() != Placement.LocalCellFaceRules.Num()) continue;
				bool bLeavesMatch = true;
				for (const auto& Frozen : Placement.LocalCellFaceRules)
				{
					bLeavesMatch &= Composite->Cells.ContainsByPredicate([&](const auto& Leaf)
					{
						return Leaf.LocalCell == Frozen.LocalCell
							&& ((Leaf.RelativeYawRotationSteps % 4) + 4) % 4 == Frozen.RelativeYawRotationSteps
							&& Leaf.Module && Frozen.TemplatePath.IsValid()
							&& Leaf.Module->Template.ToSoftObjectPath() == Frozen.TemplatePath;
					});
				}
				if (!bLeavesMatch) continue;
			}
			// Legacy entry-only results may resolve only when the entire graph agrees
			// on the asset. Never choose the first parent or sibling name collision.
			bAmbiguous |= MatchedAsset != nullptr && MatchedAsset != Asset;
			MatchedAsset = Asset;
			Match = &Entry;
		}
	};
	Visit(ContentSet);
	return bAmbiguous ? nullptr : Match;
}

namespace
{
	const FLayoutId DirectRootPlacementPolicyId(TEXT("DirectRootExplicit"));

	bool ShouldDeriveSteppedTerrainSupport(
		const ULayoutProfileAsset* const LayoutProfile,
		const ELayoutWorldBindingPlacementKind PlacementKind)
	{
		return PlacementKind != ELayoutWorldBindingPlacementKind::None
			&& LayoutProfile != nullptr
			&& LayoutProfile->bSupportsSteppedTerrainSolve;
	}

	FLayoutValidationAssertionRecord MakeRuntimeRequestAssertionRecord(
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

	FIntVector ComputeDirectRootFootprintAnchorBlockWorldPos(
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntPoint& FootprintSize,
		const FIntVector& SharedCellSizeInBlocks)
	{
		return FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
			SiteCenterBlockWorldPos,
			FootprintSize,
			SharedCellSizeInBlocks);
	}

	FLayoutId BuildRuntimeTerrainBiomeArtifactId(const FLayoutRegionSolveRequest& Request)
	{
		if (!Request.RootSolveId.IsNone())
		{
			return FLayoutId(*FString::Printf(TEXT("TerrainBiome.%s"), *Request.RootSolveId.ToString()));
		}
		if (!Request.RegionDebugPath.IsEmpty())
		{
			return FLayoutId(*FString::Printf(TEXT("TerrainBiome.%s"), *Request.RegionDebugPath));
		}
		return TEXT("TerrainBiome.ExplicitRoot");
	}

	bool ArePlannedCellArraysEquivalent(
		const TArray<FLayoutPlannedCell>& Left,
		const TArray<FLayoutPlannedCell>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (Left[Index].Cell != Right[Index].Cell || Left[Index].Intent != Right[Index].Intent)
			{
				return false;
			}
		}
		return true;
	}

	bool IsBlockXYInsideFrozenSearchBounds(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntPoint& BlockXY)
	{
		return BlockXY.X >= Artifact.SearchMinBlockXY.X
			&& BlockXY.X <= Artifact.SearchMaxBlockXY.X
			&& BlockXY.Y >= Artifact.SearchMinBlockXY.Y
			&& BlockXY.Y <= Artifact.SearchMaxBlockXY.Y;
	}

	FIntPoint ResolveFrozenEvidenceCellBlockXY(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntVector& Cell,
		const FIntVector& SharedCellSizeInBlocks)
	{
		return FIntPoint(
			Artifact.FootprintMinBlockWorldPos.X + Cell.X * SharedCellSizeInBlocks.X,
			Artifact.FootprintMinBlockWorldPos.Y + Cell.Y * SharedCellSizeInBlocks.Y);
	}

	bool ValidateFrozenTerrainEvidenceEnvelope(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntVector& SharedCellSizeInBlocks,
		FString& OutFailureReason)
	{
		auto RejectOutOfBounds = [&Artifact, &OutFailureReason](
			const TCHAR* Source,
			const int32 Index,
			const int32 Count,
			const FIntPoint& BlockXY)
		{
			OutFailureReason = FString::Printf(
				TEXT("Frozen terrain evidence producer emitted out-of-bounds sample before solve enqueue: source=%s sampleIndex=%d/%d sampleXY=%s artifact=%s modePlan=%s siteCenter=%s footprintMin=%s footprintSize=%s searchMin=%s searchMax=%s."),
				Source,
				Index,
				Count,
				*BlockXY.ToString(),
				*Artifact.ArtifactId.ToString(),
				*Artifact.ModePlanId.ToString(),
				*Artifact.SiteCenterBlockWorldPos.ToString(),
				*Artifact.FootprintMinBlockWorldPos.ToString(),
				*Artifact.FootprintSizeInBlocks.ToString(),
				*Artifact.SearchMinBlockXY.ToString(),
				*Artifact.SearchMaxBlockXY.ToString());
			return false;
		};

		for (int32 Index = 0; Index < Artifact.SurfaceSamples.Num(); ++Index)
		{
			const FIntPoint BlockXY = Artifact.SurfaceSamples[Index].BlockXY;
			if (!IsBlockXYInsideFrozenSearchBounds(Artifact, BlockXY))
			{
				return RejectOutOfBounds(TEXT("surface"), Index, Artifact.SurfaceSamples.Num(), BlockXY);
			}
		}
		for (int32 Index = 0; Index < Artifact.BiomeOwnershipSamples.Num(); ++Index)
		{
			const FIntPoint BlockXY = Artifact.BiomeOwnershipSamples[Index].BlockXY;
			if (!IsBlockXYInsideFrozenSearchBounds(Artifact, BlockXY))
			{
				return RejectOutOfBounds(TEXT("biome_ownership"), Index, Artifact.BiomeOwnershipSamples.Num(), BlockXY);
			}
		}
		for (int32 Index = 0; Index < Artifact.SteppedSupportSamples.Num(); ++Index)
		{
			const FIntPoint BlockXY = ResolveFrozenEvidenceCellBlockXY(Artifact, Artifact.SteppedSupportSamples[Index].LocalCell, SharedCellSizeInBlocks);
			if (!IsBlockXYInsideFrozenSearchBounds(Artifact, BlockXY))
			{
				return RejectOutOfBounds(TEXT("stepped_support"), Index, Artifact.SteppedSupportSamples.Num(), BlockXY);
			}
		}
		for (int32 Index = 0; Index < Artifact.SteppedNeighborHaloSamples.Num(); ++Index)
		{
			const FIntPoint BlockXY = ResolveFrozenEvidenceCellBlockXY(Artifact, Artifact.SteppedNeighborHaloSamples[Index].LocalCell, SharedCellSizeInBlocks);
			if (!IsBlockXYInsideFrozenSearchBounds(Artifact, BlockXY))
			{
				return RejectOutOfBounds(TEXT("stepped_neighbor_halo"), Index, Artifact.SteppedNeighborHaloSamples.Num(), BlockXY);
			}
		}
		for (int32 Index = 0; Index < Artifact.FootprintClassification.CellClassifications.Num(); ++Index)
		{
			const FIntPoint BlockXY = Artifact.FootprintClassification.CellClassifications[Index].BlockXY;
			if (!IsBlockXYInsideFrozenSearchBounds(Artifact, BlockXY))
			{
				return RejectOutOfBounds(TEXT("footprint_classification"), Index, Artifact.FootprintClassification.CellClassifications.Num(), BlockXY);
			}
		}
		for (int32 Index = 0; Index < Artifact.TerrainPlacementCells.Num(); ++Index)
		{
			const FIntPoint BlockXY = ResolveFrozenEvidenceCellBlockXY(Artifact, Artifact.TerrainPlacementCells[Index].Cell, SharedCellSizeInBlocks);
			if (!IsBlockXYInsideFrozenSearchBounds(Artifact, BlockXY))
			{
				return RejectOutOfBounds(TEXT("placement_evidence"), Index, Artifact.TerrainPlacementCells.Num(), BlockXY);
			}
		}
		return true;
	}

}

namespace LayoutWorldBindingRuntimeHelpers
{
	ULayoutRegionContentSetAsset* ResolveRuntimePreferredContentSet(const ULayoutProfileAsset* const Profile)
	{
		return Profile != nullptr ? Profile->ContentSet.Get() : nullptr;
	}

	FLayoutWorldBindingRuntimeView BuildExplicitRootRuntimeView(
		ULayoutProfileAsset* const LayoutProfile,
		ULayoutRegionContentSetAsset* const LayoutContentSet,
		const FLayoutRootSolveBudgetSettings& SolveBudget,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy)
	{
		FLayoutWorldBindingRuntimeView RuntimeView;
		RuntimeView.LayoutProfile = LayoutProfile;
		RuntimeView.ContentSet = LayoutContentSet != nullptr
			? LayoutContentSet
			: ResolveRuntimePreferredContentSet(LayoutProfile);
		RuntimeView.SharedCellSizeInBlocks = ResolveRuntimeSharedCellSizeInBlocks(
			RuntimeView.ContentSet);
		RuntimeView.PlacementPolicy = PlacementPolicy;
		RuntimeView.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		RuntimeView.SolveBudget = SolveBudget;
		return RuntimeView;
	}

	bool TryBuildExplicitRootRuntimeViewFromWorldBindingProfile(
		const ULayoutWorldBindingAsset* const WorldBinding,
		ULayoutProfileAsset* const LayoutProfile,
		FLayoutWorldBindingRuntimeView& OutRuntimeView,
		FLayoutWorldBindingSiteFrontendSelection& OutFrontendSelection,
		FString& OutFailureReason)
	{
		return LayoutWorldBindingRuntimeView::TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
			WorldBinding,
			LayoutProfile,
			OutRuntimeView,
			OutFrontendSelection,
			OutFailureReason);
	}

	bool TryBuildExplicitRuntimeSolveRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 SolveSeed,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason)
	{
		OutSolveRequest = FLayoutRegionSolveRequest();
		OutFailureReason.Reset();

		if (RuntimeView.LayoutProfile == nullptr)
		{
			OutFailureReason = TEXT("Direct root layout solve requires a layout profile.");
			return false;
		}

		const FString RegionDebugPath = FString::Printf(TEXT("DirectRoot/%s"), *SiteCenterBlockWorldPos.ToString());
		const FLayoutId RegionPathName(*RegionDebugPath);
		const bool bBuilt = LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			SolveSeed,
			RegionDebugPath,
			DirectRootPlacementPolicyId,
			RegionPathName,
			RegionPathName,
			OutSolveRequest,
			OutFailureReason);
		if (!bBuilt)
		{
			return false;
		}

		const bool bWorldFacingPlacement = RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None;
		const FIntVector SharedCellSizeInBlocks = RuntimeView.SharedCellSizeInBlocks;
		const bool bHasUsableLattice =
			SharedCellSizeInBlocks.X > 0
			|| SharedCellSizeInBlocks.Y > 0
			|| SharedCellSizeInBlocks.Z > 0;
		const bool bPassed =
			!bWorldFacingPlacement
			|| !bHasUsableLattice
			|| SiteCenterBlockWorldPos
				== FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
					SiteCenterBlockWorldPos,
					SharedCellSizeInBlocks);
		OutSolveRequest.ValidationAssertions.Add(MakeRuntimeRequestAssertionRecord(
			TEXT("RegionRequest.WorldPlacementLatticeContractValid"),
			ELayoutValidationAssertionKind::RequestContractValid,
			bPassed,
			{DirectRootPlacementPolicyId, RegionPathName},
			bPassed
				? FString()
				: FString::Printf(
					TEXT("Direct-root request '%s' uses site center %s, which does not land on the resolved shared-cell lattice for cell size %s. World-facing direct-root requests must start on integer multiples of the shared cell size before solve execution begins."),
					*RegionDebugPath,
					*SiteCenterBlockWorldPos.ToString(),
					*SharedCellSizeInBlocks.ToString())));
		return true;
	}

	bool TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FName EligibleBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& InOutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		OutFailureReason.Reset();

		auto BuildSupportMap = [
			&RuntimeView,
			&CoordinateSettings,
			&ActiveBiomeSampler](
				const FIntVector& AnchorBlockWorldPos,
				const TArray<FLayoutPlannedCell>& SupportPlannedCells,
				FLayoutSteppedTerrainSupportMap& OutSupportMap,
				FString& OutSupportFailureReason) -> bool
		{
			return FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
				AnchorBlockWorldPos,
				RuntimeView.SharedCellSizeInBlocks,
				RuntimeView.SharedCellSizeInBlocks.X,
				SupportPlannedCells,
				RuntimeView.PlacementPolicy.SurfaceSearch,
				CoordinateSettings,
				ActiveBiomeSampler,
				OutSupportMap,
				OutSupportFailureReason);
		};

		const FIntVector InitialAnchorBlockWorldPos = ComputeDirectRootFootprintAnchorBlockWorldPos(
			SiteCenterBlockWorldPos,
			FootprintSize,
			RuntimeView.SharedCellSizeInBlocks);
		FLayoutSteppedTerrainSupportMap PreliminarySupportMap;
		if (!BuildSupportMap(InitialAnchorBlockWorldPos, PlannedCells, PreliminarySupportMap, OutFailureReason))
		{
			return false;
		}

		// The prewarm adapter handles cell derivation; preserve supplied cells here.
		InOutSolveRequest.FootprintSize = FootprintSize;
		InOutSolveRequest.PlannedCells = PlannedCells;
		InOutSolveRequest.SteppedTerrainSupportMap = PreliminarySupportMap;

		const FIntVector FinalAnchorBlockWorldPos = ComputeDirectRootFootprintAnchorBlockWorldPos(
			SiteCenterBlockWorldPos,
			FootprintSize,
			RuntimeView.SharedCellSizeInBlocks);
		FLayoutSteppedTerrainSupportMap FinalSupportMap;
		if (!BuildSupportMap(FinalAnchorBlockWorldPos, PlannedCells, FinalSupportMap, OutFailureReason))
		{
			return false;
		}

		InOutSolveRequest.SteppedTerrainSupportMap = FinalSupportMap;
		TSet<FIntVector> PlannedCellSet;
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			PlannedCellSet.Add(PlannedCell.Cell);
		}
		TArray<FLayoutPlannedCell> NeighborHaloCells;
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			for (const FIntVector& Delta : {FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)})
			{
				const FIntVector NeighborCell = PlannedCell.Cell + Delta;
				if (PlannedCellSet.Contains(NeighborCell)
					|| NeighborHaloCells.ContainsByPredicate([NeighborCell](const FLayoutPlannedCell& Candidate)
					{
						return Candidate.Cell == NeighborCell;
					}))
				{
					continue;
				}
				FLayoutPlannedCell& HaloCell = NeighborHaloCells.AddDefaulted_GetRef();
				HaloCell.Cell = NeighborCell;
				HaloCell.Intent = ELayoutCellIntent::Interior;
			}
		}
		FLayoutSteppedTerrainSupportMap NeighborHaloMap;
		if (!NeighborHaloCells.IsEmpty())
		{
			FString HaloFailureReason;
			if (!BuildSupportMap(FinalAnchorBlockWorldPos, NeighborHaloCells, NeighborHaloMap, HaloFailureReason))
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("Explicit-runtime stepped terrain halo omitted at siteCenter=%s: %s"),
					*SiteCenterBlockWorldPos.ToString(),
					*HaloFailureReason);
				NeighborHaloMap = FLayoutSteppedTerrainSupportMap();
			}
		}
		InOutSolveRequest.bHasFrozenTerrainBiomeAdapterInput = true;
		const FName FrozenMatchingBiomeRowName = !RuntimeView.MatchingBiomeRowName.IsNone()
			? RuntimeView.MatchingBiomeRowName
			: EligibleBiomeRowName;
		InOutSolveRequest.FrozenTerrainBiomeAdapterInput =
			FLayoutTerrainSampling::BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
				SiteCenterBlockWorldPos,
				FinalAnchorBlockWorldPos,
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
				InOutSolveRequest.SteppedTerrainSupportMap,
				false,
				NeighborHaloMap.SupportSamples);
		InOutSolveRequest.FrozenTerrainBiomeAdapterInput.ArtifactId = BuildRuntimeTerrainBiomeArtifactId(InOutSolveRequest);
		if (RuntimeView.PlacementPolicy.TerrainTransition.MaxFoundationDepth > 0)
		{
			bool bCompleteEntrySurfaceEvidence = true;
			FLayoutTerrainSampling::ComputeEntryTraversabilityVerdicts(
				InOutSolveRequest.FrozenTerrainBiomeAdapterInput,
				RuntimeView.SharedCellSizeInBlocks,
				RuntimeView.PlacementPolicy.TerrainTransition.MaxFoundationDepth,
				[&](const int32 BlockX, const int32 BlockY) -> int32
				{
					FLayoutActiveBiomeSurfaceSample Sample;
					if (!ActiveBiomeSampler.FindAnyActiveBiomeSurface(
						FIntPoint(BlockX, BlockY),
						RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
						RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
						CoordinateSettings, Sample) || !Sample.bIsValid)
					{
						bCompleteEntrySurfaceEvidence = false;
						// The callback requires a height; these provisional verdicts are
						// discarded below and can never enter a published solve request.
						return SiteCenterBlockWorldPos.Z;
					}
					return Sample.SurfaceBlockWorldPos.Z;
				});
			if (!bCompleteEntrySurfaceEvidence)
			{
				InOutSolveRequest.bHasFrozenTerrainBiomeAdapterInput = false;
				InOutSolveRequest.FrozenTerrainBiomeAdapterInput = FLayoutFrozenTerrainBiomeAdapterInput();
				OutFailureReason = TEXT("Entry approach lacks biome-noise surface evidence within the configured search bounds.");
				return false;
			}
		}
		if (!ValidateFrozenTerrainEvidenceEnvelope(
				InOutSolveRequest.FrozenTerrainBiomeAdapterInput,
				RuntimeView.SharedCellSizeInBlocks,
				OutFailureReason))
		{
			return false;
		}
		LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(InOutSolveRequest);
		return true;
	}

	bool TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FName EligibleBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 SolveSeed,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		if (!TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			SiteCenterBlockWorldPos,
			SolveSeed,
			OutSolveRequest,
			OutFailureReason))
		{
			return false;
		}

		return TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
			RuntimeView,
			EligibleBiomeRowName,
			SiteCenterBlockWorldPos,
			FootprintSize,
			PlannedCells,
			CoordinateSettings,
			ActiveBiomeSampler,
			OutSolveRequest,
			OutFailureReason,
			World);
	}

	bool TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FName EligibleBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 SolveSeed,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutRegionSolveRequest& OutSolveRequest,
		FString& OutFailureReason,
		const AChunkWorldCore* const World)
	{
		// Build once so terrain prewarm consumes request-owned selected footprint.
		if (!TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			SiteCenterBlockWorldPos,
			SolveSeed,
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
		return TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
			RuntimeView,
			EligibleBiomeRowName,
			SiteCenterBlockWorldPos,
			FootprintSize,
			PlannedCells,
			CoordinateSettings,
			ActiveBiomeSampler,
			OutSolveRequest,
			OutFailureReason,
			World);
	}

	FResolvedLayoutSiteRecord BuildResolvedSiteRecordFromRuntimeSolve(
		const FResolvedLayoutSiteLocationMetadata& LocationMetadata,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const int32 SolveSeed,
		const FLayoutRegionSolveRequest& SolveRequest,
		const FLayoutSolveResult& SolveResult,
		const FLayoutWorldBindingSiteFrontendSelection* FrontendSelection)
	{
		FLayoutSiteSolveSourceSelection SolveSourceSelection;
		SolveSourceSelection.LayoutProfile = RuntimeView.LayoutProfile;
		SolveSourceSelection.ContentSet = RuntimeView.ContentSet;
		SolveSourceSelection.ExportedConnectorTypeTags =
			RuntimeView.ExportedConnectorTypeTags;
		SolveSourceSelection.SolveSeed = SolveSeed;

		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootSolveId = SolveRequest.RootSolveId;
		PublicationMetadata.RootCandidateId = SolveRequest.RootCandidateId;
		PublicationMetadata.RootPlacementPolicyId = SolveRequest.RootPlacementPolicyId;

		return FLayoutSiteReservation::BuildResolvedSiteRecord(
			LocationMetadata,
			SolveSourceSelection,
			FrontendSelection,
			PublicationMetadata,
			&SolveResult);
	}

	FIntVector ResolveRuntimeSharedCellSizeInBlocks(
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

	FIntVector ResolveRuntimeSharedCellSizeInBlocks(
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		return ResolveRuntimeSharedCellSizeInBlocks(FLayoutSolveResult(), ContentSet);
	}
}
