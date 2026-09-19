// Copyright 2026 Spotted Loaf Studio

#include "Layout/Editor/LayoutDirectRootGenerationController.h"

#include "Algo/Count.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorld/Blueprint/ChunkWorldBlockHitBlueprintLibrary.h"
#include "EditorViewportClient.h"
#include "HAL/PlatformProcess.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Editor/LayoutDirectRootGenerationSettings.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Runtime/LayoutGeneratorSolveFacade.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "PrimitiveSceneProxy.h"
#include "SceneManagement.h"

namespace
{
	const TCHAR* const LayoutGeneratorName = TEXT("Layout Generator");

	bool RehydratePreviewPlacementCarriersFromContentSet(
		const ULayoutRegionContentSetAsset* const ContentSet,
		FLayoutSolveResult& InOutSolveResult)
	{
		if (ContentSet == nullptr)
		{
			return false;
		}

		bool bUpdatedAnyPlacement = false;
		for (FLayoutPlacedModule& Placement : InOutSolveResult.Placements)
		{
			if (Placement.SourceContentEntryId.IsNone()
				|| Placement.Module != nullptr
				|| Placement.CompositeModule != nullptr)
			{
				continue;
			}

			const FLayoutRegionContentEntry* SourceEntry =
				FLayoutGeneratorSolveFacade::FindFrozenPlacementSourceEntry(ContentSet, Placement);
			if (SourceEntry == nullptr)
			{
				continue;
			}

			Placement.Module = SourceEntry->ModuleSettings.Module;
			Placement.CompositeModule = SourceEntry->ModuleSettings.CompositeModule;
			bUpdatedAnyPlacement |= Placement.Module != nullptr || Placement.CompositeModule != nullptr;
		}
		return bUpdatedAnyPlacement;
	}

	void RehydratePreviewPlacementCarriersFromContentSet(
		const ULayoutRegionContentSetAsset* const ContentSet,
		FResolvedLayoutSiteRecord& InOutSiteRecord,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		FResolvedLayoutSiteSolvedPayload SolvedPayload = InOutSiteRecord.GetResolvedSiteSolvedPayload();
		if (RehydratePreviewPlacementCarriersFromContentSet(ContentSet, SolvedPayload.SolveResult))
		{
			InOutSiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);
		}

		RehydratePreviewPlacementCarriersFromContentSet(ContentSet, InOutScheduleResult.MergedSolveResult);
		for (FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
		{
			RehydratePreviewPlacementCarriersFromContentSet(ContentSet, RegionResult.SolveResult);
		}
	}

	FString SummarizeBiomeRows(const TArray<FName>& BiomeRowNames)
	{
		if (BiomeRowNames.IsEmpty())
		{
			return TEXT("<none>");
		}

		TArray<FString> RowNames;
		RowNames.Reserve(BiomeRowNames.Num());
		for (const FName& BiomeRowName : BiomeRowNames)
		{
			RowNames.Add(BiomeRowName.ToString());
		}
		return FString::Join(RowNames, TEXT(","));
	}

	FString SummarizeTerrainWriteSpan(const FLayoutExplicitRootPreviewTerrainFit& PreviewTerrainFit)
	{
		if (PreviewTerrainFit.TerrainWriteMinZ == INDEX_NONE
			|| PreviewTerrainFit.TerrainWriteMaxZ == INDEX_NONE)
		{
			return TEXT("<none>");
		}

		return FString::Printf(
			TEXT("[%d,%d]"),
			PreviewTerrainFit.TerrainWriteMinZ,
			PreviewTerrainFit.TerrainWriteMaxZ);
	}

	void CachePreviewTerrainStateFromFrozenContract(
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		const ELayoutWorldBindingPlacementKind RootPlacementKind,
		bool& bOutHasPreviewTerrainFit,
		FIntVector& OutTerrainAnchorBlockWorldPos,
		ELayoutWorldBindingTerrainFitDiagnosticKind& OutTerrainFitDiagnosticKind,
		TArray<FIntVector>& OutTerrainWritePositions,
		TArray<int32>& OutTerrainWriteMaterials)
	{
		bOutHasPreviewTerrainFit = RootPlacementKind != ELayoutWorldBindingPlacementKind::None
			&& !FrozenTerrainContract.ContractId.IsNone();
		OutTerrainAnchorBlockWorldPos = bOutHasPreviewTerrainFit
			? FrozenTerrainContract.FootprintMinBlockWorldPos
			: FIntVector::ZeroValue;
		OutTerrainFitDiagnosticKind = bOutHasPreviewTerrainFit
			? FrozenTerrainContract.DiagnosticKind
			: ELayoutWorldBindingTerrainFitDiagnosticKind::None;
		OutTerrainWritePositions.Reset();
		OutTerrainWriteMaterials.Reset();
		if (!bOutHasPreviewTerrainFit)
		{
			return;
		}

		OutTerrainWritePositions.Reserve(FrozenTerrainContract.TerrainWrites.Num());
		OutTerrainWriteMaterials.Reserve(FrozenTerrainContract.TerrainWrites.Num());
		for (const FLayoutFrozenTerrainWriteRecord& WriteRecord : FrozenTerrainContract.TerrainWrites)
		{
			OutTerrainWritePositions.Add(WriteRecord.BlockWorldPos);
			OutTerrainWriteMaterials.Add(WriteRecord.Material);
		}
	}

	// Zone color palette for debug cell markers.
	// These are the colors shown in the Overlay Legend.
	FColor ColorForZone(const ELayoutPlacementZone Zone)
	{
		switch (Zone)
		{
		case ELayoutPlacementZone::Perimeter:
			return FColor(120, 220, 120);  // green
		case ELayoutPlacementZone::Edge:
			return FColor(220, 70, 70);    // coral red — distinct from blue interior
		case ELayoutPlacementZone::Corner:
			return FColor(170, 120, 255);  // purple
		case ELayoutPlacementZone::Core:
			return FColor(255, 170, 70);   // orange
		case ELayoutPlacementZone::Interior:
		default:
			return FColor(80, 140, 255);   // blue
		}
	}

	/** Returns the center-sphere color for a planner-owned cell intent. */
	FColor ColorForIntent(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Boundary:
			return FColor(255, 210, 80);
		case ELayoutCellIntent::Entry:
			return FColor(0, 220, 200);
		case ELayoutCellIntent::Core:
			return FColor(255, 170, 70);
		case ELayoutCellIntent::Connector:
			return FColor(255, 100, 210);
		case ELayoutCellIntent::VerticalAccess:
			return FColor(120, 255, 80);
		case ELayoutCellIntent::Interior:
		default:
			return FColor(80, 140, 255);
		}
	}

	/** Returns true when a cell was culled or represented by an overview point instead of wire detail. */
	bool DrawCellOverview(const FSceneView* View, FPrimitiveDrawInterface* PDI,
		const ULayoutDirectRootGenerationSettings& Settings, const FVector& Center,
		const FVector& CellSize, const ELayoutCellIntent Intent)
	{
		if (View == nullptr) return false;
		// Enclose the furthest Entry/VA/seam marker, not only the small center sphere.
		if (!View->ViewFrustum.IntersectBox(Center, CellSize * 0.75)) return true;
		if (Settings.MinimumDetailedMarkerRadiusPixels <= 0.0f) return false;
		const FMatrix& Projection = View->ViewMatrices.GetProjectionMatrix();
		const double PixelScale = 0.5 * FMath::Max(
			FMath::Abs(Projection.M[0][0]) * View->UnscaledViewRect.Width(),
			FMath::Abs(Projection.M[1][1]) * View->UnscaledViewRect.Height());
		// Homogeneous W accounts for perspective depth; orthographic views keep W=1.
		const double RadiusPixels = CellSize.GetMin() * 0.0875 * PixelScale
			/ FMath::Max(0.001, FMath::Abs(View->WorldToScreen(Center).W));
		if (RadiusPixels >= Settings.MinimumDetailedMarkerRadiusPixels) return false;
		if (Settings.bDrawCellZoneMarkers)
		{
			PDI->DrawPoint(Center, ColorForIntent(Intent), 3.0f, SDPG_Foreground);
		}
		return true;
	}

	bool PreviewTerrainFitOffsetsOverlayLayout(
		const FResolvedLayoutSiteRecord& SiteRecord,
		const bool bHasCachedPreviewTerrainFit)
	{
		if (!bHasCachedPreviewTerrainFit)
		{
			return false;
		}

		const ELayoutWorldBindingPlacementKind RootPlacementKind =
			SiteRecord.GetResolvedSiteSolvedPayload().SolveResult.RootPlacementKind;
		return RootPlacementKind != ELayoutWorldBindingPlacementKind::None;
	}

	int32 ResolveBlockSizeUnrealUnits(const AChunkWorldExtended& ChunkWorld)
	{
		if (ChunkWorld.PrimLayer != nullptr && ChunkWorld.PrimLayer->BlockSize > 0)
		{
			return ChunkWorld.PrimLayer->BlockSize;
		}

		return ChunkWorld.WorldGenDef != nullptr ? FMath::Max(1, ChunkWorld.WorldGenDef->BaseBlockSize) : 100;
	}

	FVector ResolveWorldBoxCenter(const FIntVector& MinBlockWorldPos, const FIntVector& SizeInBlocks)
	{
		return FVector(
			static_cast<double>(MinBlockWorldPos.X) + static_cast<double>(SizeInBlocks.X) * 0.5,
			static_cast<double>(MinBlockWorldPos.Y) + static_cast<double>(SizeInBlocks.Y) * 0.5,
			static_cast<double>(MinBlockWorldPos.Z) + static_cast<double>(SizeInBlocks.Z) * 0.5);
	}

	// Replays the solver's own boundary-cell classification for editor debug drawing.
	bool IsBoundaryCell(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		return Cell.X == 0
			|| Cell.Y == 0
			|| Cell.X == FootprintSize.X - 1
			|| Cell.Y == FootprintSize.Y - 1;
	}

	bool IsCornerBoundaryCell(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		if (FootprintSize.X <= 1 || FootprintSize.Y <= 1)
		{
			return false;
		}

		const bool bOnXEdge = Cell.X == 0 || Cell.X == FootprintSize.X - 1;
		const bool bOnYEdge = Cell.Y == 0 || Cell.Y == FootprintSize.Y - 1;
		return bOnXEdge && bOnYEdge;
	}

	// Classifies a cell into its most-specific placement zone, exactly replaying the solver's
	// DoesCellMatchPlacementZoneInFootprint rules in most-specific-first order.
	ELayoutPlacementZone ClassifyCellZone(const FIntVector& Cell, const FIntPoint& FootprintSize)
	{
		// Corner is most specific
		if (IsCornerBoundaryCell(Cell, FootprintSize))
		{
			return ELayoutPlacementZone::Corner;
		}

		// Edge = boundary but not corner
		if (IsBoundaryCell(Cell, FootprintSize))
		{
			return ELayoutPlacementZone::Edge;
		}

		// Core = roughly the center cell at Z=0
		if (Cell.Z == 0)
		{
			const float CenterX = static_cast<float>(FootprintSize.X - 1) * 0.5f;
			const float CenterY = static_cast<float>(FootprintSize.Y - 1) * 0.5f;
			if (FMath::Abs(static_cast<float>(Cell.X) - CenterX) <= 0.5f
				&& FMath::Abs(static_cast<float>(Cell.Y) - CenterY) <= 0.5f)
			{
				return ELayoutPlacementZone::Core;
			}
		}

		// Everything else is interior fill
		return ELayoutPlacementZone::Interior;
	}

	ELayoutPlacementZone ResolveBaseZoneFromPlannedTopology(
		const FIntVector& Cell,
		const TSet<FIntVector>& LocalCells,
		const TSet<FIntVector>& ExternalCells,
		const FIntVector& CellOffset)
	{
		uint8 MissingLateralFaceMask = 0;
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			const FIntVector LocalNeighbor = Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
			const FIntVector ExternalNeighbor = CellOffset + LocalNeighbor;
			if (!LocalCells.Contains(LocalNeighbor) && !ExternalCells.Contains(ExternalNeighbor))
			{
				MissingLateralFaceMask |= LayoutFaceDirectionMask(Direction);
			}
		}
		return ResolveLayoutPlacementZoneFromLateralFaceMask(MissingLateralFaceMask);
	}

	void RemoveInternalLateralBoundaryFaces(
		TArray<ELayoutFaceDirection>& InOutBoundaryFaces,
		const FIntVector& Cell,
		const TSet<FIntVector>& OccupiedCells)
	{
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
		{
			if (OccupiedCells.Contains(Cell + FLayoutDirectionUtils::ToCellDelta(Direction)))
			{
				InOutBoundaryFaces.Remove(Direction);
			}
		}
	}

	// Returns the set of face directions on a cell that face outside the footprint.
	// MaxPlannedLevelZ is the highest Z index among all planned cells; cells at this Z
	// are treated as the top-facing boundary, including TopBridge cells added by the
	// stepped-terrain adapter.
	TArray<ELayoutFaceDirection> ResolveRegionBoundaryFaces(const FIntVector& Cell, const FIntPoint& FootprintSize, const int32 MaxPlannedLevelZ)
	{
		TArray<ELayoutFaceDirection> BoundaryFaces;
		if (Cell.X == 0)                     { BoundaryFaces.Add(ELayoutFaceDirection::NegX); }
		if (Cell.X == FootprintSize.X - 1)   { BoundaryFaces.Add(ELayoutFaceDirection::PosX); }
		if (Cell.Y == 0)                     { BoundaryFaces.Add(ELayoutFaceDirection::NegY); }
		if (Cell.Y == FootprintSize.Y - 1)   { BoundaryFaces.Add(ELayoutFaceDirection::PosY); }
		if (Cell.Z == 0)                     { BoundaryFaces.Add(ELayoutFaceDirection::NegZ); }
		if (Cell.Z == MaxPlannedLevelZ)      { BoundaryFaces.Add(ELayoutFaceDirection::PosZ); }
		return BoundaryFaces;
	}

	int32 NormalizeDirectRootYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	FIntVector RotateDirectRootPlacementCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		switch (NormalizeDirectRootYawRotationSteps(YawRotationSteps))
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

	FIntPoint ResolveOccupiedLocalCellRotationFootprint(const TArray<FIntVector>& OccupiedLocalCells)
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

	TArray<FIntVector> ResolveDirectRootPlacementOccupiedLocalCells(const FLayoutPlacedModule& Placement)
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

	FIntPoint ResolveDirectRootPlacementRotationFootprint(const FLayoutPlacedModule& Placement)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolveDirectRootPlacementOccupiedLocalCells(Placement);
		if (!OccupiedLocalCells.IsEmpty())
		{
			return ResolveOccupiedLocalCellRotationFootprint(OccupiedLocalCells);
		}

		if (Placement.BundleBoundsCells != FIntVector::ZeroValue)
		{
			return FIntPoint(
				FMath::Max(1, Placement.BundleBoundsCells.X),
				FMath::Max(1, Placement.BundleBoundsCells.Y));
		}

		if (Placement.CompositeModule != nullptr)
		{
			const FIntVector BoundsCells = Placement.CompositeModule->GetBoundsCells();
			return FIntPoint(FMath::Max(1, BoundsCells.X), FMath::Max(1, BoundsCells.Y));
		}

		if (Placement.Module != nullptr)
		{
			// Live leaf modules use a one-cell contract on active caller paths. If
			// the solved occupied-cell carrier is absent, do not reopen raw legacy
			// BoundsCells as a fallback here.
			return FIntPoint(1, 1);
		}

		return FIntPoint(1, 1);
	}

	TArray<FIntVector> BuildDirectRootPlacementCells(const FLayoutPlacedModule& Placement)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolveDirectRootPlacementOccupiedLocalCells(Placement);
		if (OccupiedLocalCells.IsEmpty())
		{
			return {Placement.Cell};
		}

		const FIntPoint RotationFootprint = ResolveOccupiedLocalCellRotationFootprint(OccupiedLocalCells);
		TArray<FIntVector> PlacementCells;
		PlacementCells.Reserve(OccupiedLocalCells.Num());
		for (const FIntVector& LocalCell : OccupiedLocalCells)
		{
			PlacementCells.Add(
				Placement.Cell + RotateDirectRootPlacementCellInFootprintYaw(LocalCell, RotationFootprint, Placement.YawRotationSteps));
		}

		return PlacementCells;
	}

	FIntVector ResolvePlacementSizeInBlocks(const FLayoutPlacedModule& Placement, const FIntVector& SharedCellSizeInBlocks)
	{
		if (Placement.Module == nullptr)
		{
			return SharedCellSizeInBlocks;
		}

		FIntVector SizeInBlocks = Placement.Module->GetEffectiveTemplateDimensionsBlocks();
		if (SizeInBlocks.X <= 0 || SizeInBlocks.Y <= 0 || SizeInBlocks.Z <= 0)
		{
			// Editor overlay/debug paths should not silently convert an unreadable
			// leaf template payload into one shared-cell-sized placement box. The
			// active Group 4 path now prefers strict validation and explicit asset
			// failure over this legacy-looking fallback because a bad `0,0,0`
			// template size is a real authored/runtime problem, not an alternate
			// visualization contract.
			return FIntVector::ZeroValue;
		}

		if ((Placement.YawRotationSteps & 1) != 0)
		{
			Swap(SizeInBlocks.X, SizeInBlocks.Y);
		}

		return SizeInBlocks;
	}

	int32 ResolveSolveHeightInCells(const FLayoutSolveResult& SolveResult)
	{
		int32 MaxZ = 0;
		for (const FLayoutPlannedCell& PlannedCell : SolveResult.PlannedCells)
		{
			MaxZ = FMath::Max(MaxZ, PlannedCell.Cell.Z);
		}
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			MaxZ = FMath::Max(MaxZ, Placement.Cell.Z);
		}
		for (const FIntVector& EntryCell : SolveResult.ExportedEntryCells)
		{
			MaxZ = FMath::Max(MaxZ, EntryCell.Z);
		}
		for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
		{
			MaxZ = FMath::Max(MaxZ, ResidualCell.Cell.Z);
		}
		return MaxZ + 1;
	}

	FVector ResolveFaceDirectionVector(const ELayoutFaceDirection Direction)
	{
		switch (Direction)
		{
		case ELayoutFaceDirection::NegX:
			return FVector(-1.0, 0.0, 0.0);
		case ELayoutFaceDirection::PosY:
			return FVector(0.0, 1.0, 0.0);
		case ELayoutFaceDirection::NegY:
			return FVector(0.0, -1.0, 0.0);
		case ELayoutFaceDirection::PosZ:
			return FVector(0.0, 0.0, 1.0);
		case ELayoutFaceDirection::NegZ:
			return FVector(0.0, 0.0, -1.0);
		case ELayoutFaceDirection::PosX:
		default:
			return FVector(1.0, 0.0, 0.0);
		}
	}

	const TCHAR* ToSeverityLabel(const ELayoutValidationSeverity Severity)
	{
		switch (Severity)
		{
		case ELayoutValidationSeverity::Warning:
			return TEXT("Warning");
		case ELayoutValidationSeverity::Error:
		default:
			return TEXT("Error");
		}
	}

	int32 CountVerticalAccessPlacements(const FLayoutSolveResult& SolveResult)
	{
		return Algo::CountIf(
			SolveResult.Placements,
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.Intent == ELayoutCellIntent::VerticalAccess;
			});
	}
}

// Test-visible wrappers that delegate to the anonymous-namespace debug classification
// functions so editor automation can validate boundary-face and zone classification directly
// without needing a full preview solve + BuildCellDebugInfos cycle.
TArray<ELayoutFaceDirection> FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
	const FIntVector& Cell,
	const FIntPoint& FootprintSize,
	const int32 MaxPlannedLevelZ)
{
	return ResolveRegionBoundaryFaces(Cell, FootprintSize, MaxPlannedLevelZ);
}

ELayoutPlacementZone FLayoutDirectRootGenerationController::ClassifyCellPlacementZoneForTesting(
	const FIntVector& Cell,
	const FIntPoint& FootprintSize)
{
	return ClassifyCellZone(Cell, FootprintSize);
}

int32 FLayoutDirectRootGenerationController::ResolveContinuationPreviewAlignmentLevel(
	const FLayoutSolveResult& SolveResult,
	const FResolvedLayoutConnectorRecord& ConnectorRecord)
{
	if (SolveResult.ResolvedTerrainAlignmentLevel != INDEX_NONE)
	{
		return SolveResult.ResolvedTerrainAlignmentLevel;
	}

	return ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel != INDEX_NONE
		? ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel
		: 0;
}

#if WITH_AUTOMATION_TESTS
int32 FLayoutDirectRootGenerationController::ResolveContinuationPreviewAlignmentLevelForTesting(
	const FLayoutSolveResult& SolveResult,
	const FResolvedLayoutConnectorRecord& ConnectorRecord)
{
	return ResolveContinuationPreviewAlignmentLevel(SolveResult, ConnectorRecord);
}
#endif

FLayoutCellDebugInfo FLayoutDirectRootGenerationController::BuildCellDebugInfo(
	const FLayoutPlannedCell& Cell,
	const TArray<FLayoutPlannedCell>& PlannedCells,
	const TSet<FIntVector>& ExternalCells,
	const FIntVector& CellOffset,
	const TArray<FLayoutCommittedEndpointAnchor>* const CommittedEndpointAnchors)
{
	TSet<FIntVector> LocalCells;
	LocalCells.Reserve(PlannedCells.Num());
	for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
	{
		LocalCells.Add(PlannedCell.Cell);
	}

	FLayoutCellDebugInfo DebugInfo;
	DebugInfo.Cell = CellOffset + Cell.Cell;
	DebugInfo.BaseZone = ResolveBaseZoneFromPlannedTopology(
		Cell.Cell,
		LocalCells,
		ExternalCells,
		CellOffset);
	DebugInfo.EffectiveZone = Cell.PlacementZone;
	DebugInfo.TerrainSeamFaceMask = Cell.TerrainSeamFaceMask;
	DebugInfo.Intent = Cell.Intent;
	DebugInfo.bIsPlannedEntryCell = Cell.Intent == ELayoutCellIntent::Entry;
	if (DebugInfo.bIsPlannedEntryCell && CommittedEndpointAnchors != nullptr)
	{
		for (const FLayoutCommittedEndpointAnchor& Anchor : *CommittedEndpointAnchors)
		{
			if (Anchor.LocalCell == Cell.Cell
				&& Anchor.FaceDirection != ELayoutFaceDirection::PosZ
				&& Anchor.FaceDirection != ELayoutFaceDirection::NegZ)
			{
				DebugInfo.EntryFaces.AddUnique(Anchor.FaceDirection);
			}
		}
	}
	return DebugInfo;
}

FLayoutDirectRootGenerationController::FLayoutDirectRootGenerationController()
	: SettingsObject(TStrongObjectPtr<ULayoutDirectRootGenerationSettings>(NewObject<ULayoutDirectRootGenerationSettings>(GetTransientPackage())))
{
	SetStatusMessage(FString::Printf(TEXT("Activate %s, hover a chunk-world surface, then use Ctrl + Left Click or the preview button."), LayoutGeneratorName));
}

ULayoutDirectRootGenerationSettings* FLayoutDirectRootGenerationController::GetSettingsObject() const
{
	return SettingsObject.Get();
}

void FLayoutDirectRootGenerationController::SetToolActive(const bool bInToolActive)
{
	if (bToolActive == bInToolActive)
	{
		return;
	}

	bToolActive = bInToolActive;
	if (!bToolActive)
	{
		bHasHoveredBlock = false;
		HoveredChunkWorld.Reset();
	}

	StateChanged.Broadcast();
}

bool FLayoutDirectRootGenerationController::UpdateHoverFromViewport(FEditorViewportClient* const ViewportClient, FViewport* const Viewport)
{
	if (!bToolActive || ViewportClient == nullptr || Viewport == nullptr || ViewportClient->GetWorld() == nullptr)
	{
		return false;
	}

	const FViewportCursorLocation Cursor = ViewportClient->GetCursorWorldLocationFromMousePos();
	const FVector TraceStart = Cursor.GetOrigin();
	const FVector TraceDirection = Cursor.GetDirection().GetSafeNormal();
	const float TraceDistance = SettingsObject.IsValid() ? SettingsObject->HoverTraceDistance : 500000.0f;
	const FVector TraceEnd = TraceStart + TraceDirection * TraceDistance;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(LayoutDirectRootHover), true);
	FHitResult Hit;
	const bool bHit = ViewportClient->GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);

	TWeakObjectPtr<AChunkWorldExtended> PreviousChunkWorld = HoveredChunkWorld;
	const FIntVector PreviousBlockWorldPos = HoveredBlockWorldPos;
	const bool bPreviousHasHoveredBlock = bHasHoveredBlock;

	bHasHoveredBlock = false;
	HoveredChunkWorld.Reset();
	HoveredBlockWorldPos = FIntVector::ZeroValue;

	if (bHit)
	{
		FChunkWorldResolvedBlockHit ResolvedHit;
		if (UChunkWorldBlockHitBlueprintLibrary::TryResolveBlockHitContextFromHitResult(Hit, TraceDirection, ResolvedHit)
			&& ResolvedHit.bHasBlock)
		{
			AChunkWorldExtended* const ChunkWorld = Cast<AChunkWorldExtended>(ResolvedHit.ChunkWorld.Get());
			if (ChunkWorld != nullptr
				&& (SettingsObject->TargetChunkWorld == nullptr || SettingsObject->TargetChunkWorld == ChunkWorld))
			{
				HoveredChunkWorld = ChunkWorld;
				HoveredBlockWorldPos = ResolvedHit.BlockWorldPos;
				bHasHoveredBlock = true;
			}
		}
	}

	if (PreviousChunkWorld != HoveredChunkWorld
		|| PreviousBlockWorldPos != HoveredBlockWorldPos
		|| bPreviousHasHoveredBlock != bHasHoveredBlock)
	{
		StateChanged.Broadcast();
	}

	return bHasHoveredBlock;
}

bool FLayoutDirectRootGenerationController::HandlePrimaryActionAtHoveredLocation()
{
	AChunkWorldExtended* const ChunkWorld = GetUsableHoveredChunkWorld();
	UChunkWorldLayoutRuntimeComponent* const RuntimeComponent =
		ChunkWorld != nullptr ? ChunkWorld->GetLayoutRuntimeComponent() : nullptr;
	if (ChunkWorld == nullptr || RuntimeComponent == nullptr || SettingsObject->LayoutProfile == nullptr)
	{
		SetStatusMessage(FString::Printf(TEXT("%s action requires a hovered chunk world, runtime component, and layout profile."), LayoutGeneratorName));
		return false;
	}
	if (SettingsObject->LayoutWorldBinding != nullptr
		&& SettingsObject->LayoutWorldBinding->ContinuationFamilies.ContainsByPredicate(
			[this](const FLayoutWorldBindingContinuationFamily& Family)
			{
				return Family.Candidates.ContainsByPredicate([this](const FLayoutWorldBindingContinuationCandidate& Candidate)
				{
					return Candidate.LayoutProfile == SettingsObject->LayoutProfile;
				});
			}))
	{
		return HandleContinuationPrimaryAction(*ChunkWorld, *RuntimeComponent);
	}
	return PreviewSolveAtHoveredLocation();
}

bool FLayoutDirectRootGenerationController::HandleContinuationPrimaryAction(
	AChunkWorldExtended& ChunkWorld,
	UChunkWorldLayoutRuntimeComponent& RuntimeComponent)
{
	FResolvedLayoutConnectorEndpoint HoveredEndpoint;
	FString FailureReason;
	if (!RuntimeComponent.TryFindContinuationEndpointAtBlock(
			HoveredBlockWorldPos,
			SettingsObject->LayoutWorldBinding,
			SettingsObject->LayoutProfile,
			HoveredEndpoint,
			FailureReason))
	{
		SetStatusMessage(FString::Printf(TEXT("Connect Layouts rejected: %s"), *FailureReason));
		return false;
	}
	if (!bHasSelectedContinuationStartEndpoint)
	{
		SelectedContinuationStartEndpoint = HoveredEndpoint;
		SelectedContinuationChunkWorld = &ChunkWorld;
		bHasSelectedContinuationStartEndpoint = true;
		SetStatusMessage(TEXT("Connect Layouts source selected. Ctrl + Left Click compatible target entry."));
		StateChanged.Broadcast();
		return true;
	}
	const bool bSameRoot = !HoveredEndpoint.RootRecordKey.IsEmpty()
		&& HoveredEndpoint.RootRecordKey == SelectedContinuationStartEndpoint.RootRecordKey;
	if (bSameRoot)
	{
		SelectedContinuationStartEndpoint = HoveredEndpoint;
		SelectedContinuationChunkWorld = &ChunkWorld;
		SetStatusMessage(TEXT("Connect Layouts source reselected. Ctrl + Left Click compatible target entry."));
		StateChanged.Broadcast();
		return true;
	}

	if (!CachedContinuationSegments.IsEmpty())
	{
		if (UChunkWorldLayoutRuntimeComponent* const PreviousRuntime = CachedPreviewChunkWorld.IsValid()
			? CachedPreviewChunkWorld->GetLayoutRuntimeComponent()
			: nullptr)
		{
			PreviousRuntime->CancelExplicitContinuationLayoutSolve(CachedContinuationSegments[0].ConnectorRecord);
		}
	}
	FLayoutGeneratorSolveFacade::CancelSubmittedSolve(&RuntimeComponent, PreviewAsyncState);
	bPreviewSolveInProgress = false;
	CachedPreviewChunkWorld = &ChunkWorld;
	bHasCachedAcceptedContinuationPreview = false;
	CachedContinuationSegments.Reset();
	CachedContinuationCellDebugInfos.Reset();
	bCachedContinuationPreviewIsPartial = false;
	// Starting a continuation supersedes any root preview. A rejected route must not make Apply
	// fall through to stale root authority from the previous tool operation.
	CachedSiteRecord = FResolvedLayoutSiteRecord();
	CachedScheduleResult = FLayoutRegionSolveScheduleResult();
	bHasCachedAcceptedPreview = false;
	CachedPreviewFrozenTerrainContract = FLayoutFrozenTerrainContract();
	bHasCachedPreviewTerrainFit = false;
	CachedPreviewTerrainAnchorBlockWorldPos = FIntVector::ZeroValue;
	CachedPreviewTerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;
	CachedPreviewTerrainWritePositions.Reset();
	CachedPreviewTerrainWriteMaterials.Reset();
	CachedTerrainPreviewRuns.Reset();
	CachedPreviewFailureReason.Reset();
	CachedCellDebugInfos.Reset();
	CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
	bContinuationPreviewInProgress = true;
	const int32 SolveSeed = ResolveSolveSeed(ChunkWorld, HoveredBlockWorldPos);
	FLayoutGeneratorContinuationSolveRequest Request;
	Request.RuntimeComponent = &RuntimeComponent;
	Request.StartEndpoint = SelectedContinuationStartEndpoint;
	Request.EndEndpoint = HoveredEndpoint;
	Request.WorldBinding = SettingsObject->LayoutWorldBinding;
	Request.ContinuationProfile = SettingsObject->LayoutProfile;
	Request.SolveSeed = SolveSeed;
	SetStatusMessage(TEXT("Connect Layouts preview scheduled."));
	FLayoutGeneratorSolveFacade::SubmitExplicitContinuation(
		Request,
		[this](const FLayoutGeneratorContinuationSolveResult& Result)
		{
			bContinuationPreviewInProgress = false;
			const bool bAllowPartial = SettingsObject.IsValid() && SettingsObject->bAllowPartialPreviewApply;
			const bool bCanPreviewPartial = bAllowPartial && Result.Segments.ContainsByPredicate(
				[](const FLayoutGeneratorContinuationSegmentSolveResult& Segment)
				{
					return Segment.bIsPartial
						&& !Segment.ConnectorRecord.SolveResult.Placements.IsEmpty()
						&& !Segment.ConnectorRecord.SolvedArtifactId.IsNone();
				});
			const bool bHasPreviewableSegment = bCanPreviewPartial || Result.Segments.ContainsByPredicate(
				[](const FLayoutGeneratorContinuationSegmentSolveResult& Segment)
				{
					return (Segment.bSucceeded && !Segment.ConnectorRecord.SolvedArtifactId.IsNone())
						|| (Segment.PreviewGeometry.SharedCellSizeInBlocks != FIntVector::ZeroValue
							&& (!Segment.PreviewGeometry.CenterlineCells.IsEmpty()
								|| Segment.Descriptor.FootprintSize != FIntPoint::ZeroValue));
				});
			if (!Result.bSucceeded && !bHasPreviewableSegment)
			{
				const FLayoutGeneratorContinuationSegmentSolveResult* const PublishedSegment =
					Result.Segments.FindByPredicate([](const FLayoutGeneratorContinuationSegmentSolveResult& Segment)
					{
						return !Segment.ConnectorRecord.SolvedArtifactId.IsNone();
					});
				if (PublishedSegment != nullptr)
				{
					if (UChunkWorldLayoutRuntimeComponent* const Runtime = CachedPreviewChunkWorld.IsValid()
						? CachedPreviewChunkWorld->GetLayoutRuntimeComponent()
						: nullptr)
					{
						Runtime->CancelExplicitContinuationLayoutSolve(PublishedSegment->ConnectorRecord);
					}
				}
				bHasCachedAcceptedContinuationPreview = false;
				SetStatusMessage(FString::Printf(TEXT("Connect Layouts preview rejected: %s"), *Result.FailureReason));
				StateChanged.Broadcast();
				return;
			}
			CachedContinuationSegments = Result.Segments;
			// A route with valid siblings is accepted even when other segments were omitted.
			bCachedContinuationPreviewIsPartial = !Result.bSucceeded;
			bHasCachedAcceptedContinuationPreview = true;
			CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
			int32 PlacementCount = 0;
			TArray<FString> PreviewDiagnosticDetails;
			for (const FLayoutGeneratorContinuationSegmentSolveResult& Segment : CachedContinuationSegments)
			{
				PlacementCount += Segment.ConnectorRecord.SolveResult.Placements.Num();
				if (CachedSharedCellSizeInBlocks == FIntVector::ZeroValue)
				{
					CachedSharedCellSizeInBlocks = Segment.PreviewGeometry.SharedCellSizeInBlocks != FIntVector::ZeroValue
						? Segment.PreviewGeometry.SharedCellSizeInBlocks
						: Segment.ConnectorRecord.FrontendSharedCellSizeInBlocks;
				}
				for (const FLayoutContinuationPreviewDiagnostic& Diagnostic : Segment.PreviewGeometry.Diagnostics)
				{
					PreviewDiagnosticDetails.Add(FString::Printf(TEXT("Segment %d %s%s%s%s"),
						Segment.ConnectorRecord.ContinuationSegmentIndex,
						*Diagnostic.Stage,
						Diagnostic.bHasLocalCell ? *FString::Printf(TEXT(" at %s"), *Diagnostic.LocalCell.ToString()) : TEXT(""),
						Diagnostic.Requirement.IsEmpty() ? TEXT("") : *FString::Printf(TEXT(": %s"), *Diagnostic.Requirement),
						Diagnostic.Detail.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("; %s"), *Diagnostic.Detail)));
				}
			}
			const FString PreviewDiagnosticSummary = PreviewDiagnosticDetails.IsEmpty()
				? FString()
				: FString::Printf(TEXT(" Diagnostics: %s"), *FString::Join(PreviewDiagnosticDetails, TEXT(" | ")));
			CachedCellDebugInfos.Reset();
			BuildContinuationCellDebugInfos();
			bHasSelectedContinuationStartEndpoint = false;
			SetStatusMessage(!Result.bSucceeded
				? FString::Printf(TEXT("Connect Layouts preview contains failed segments. Segments=%d placements=%d. Reason: %s%s"),
					CachedContinuationSegments.Num(), PlacementCount, *Result.FailureReason, *PreviewDiagnosticSummary)
				: (bCanPreviewPartial
					? FString::Printf(TEXT("Connect Layouts partial preview accepted. Segments=%d placements=%d. Reason: %s"),
						CachedContinuationSegments.Num(), PlacementCount, *Result.FailureReason)
					: FString::Printf(TEXT("Connect Layouts preview accepted. Segments=%d placements=%d."),
						CachedContinuationSegments.Num(), PlacementCount)));
			StateChanged.Broadcast();
		},
		&ContinuationPreviewAsyncState);
	return true;
}

bool FLayoutDirectRootGenerationController::PreviewSolveAtHoveredLocation()
{
	return PreviewSolveAtHoveredLocationInternal(true);
}

#if WITH_AUTOMATION_TESTS
bool FLayoutDirectRootGenerationController::StartPreviewSolveAtHoveredLocationForTesting()
{
	return PreviewSolveAtHoveredLocationInternal(false);
}
#endif

bool FLayoutDirectRootGenerationController::PreviewSolveAtHoveredLocationInternal(const bool bPumpUntilCompletionForAutomation)
{
	AChunkWorldExtended* const ChunkWorld = GetUsableHoveredChunkWorld();
	UChunkWorldLayoutRuntimeComponent* const LayoutRuntimeComponent = ChunkWorld != nullptr ? ChunkWorld->GetLayoutRuntimeComponent() : nullptr;
	if (ChunkWorld == nullptr || SettingsObject->LayoutProfile == nullptr || LayoutRuntimeComponent == nullptr)
	{
		SetStatusMessage(FString::Printf(TEXT("%s preview requires a hovered chunk world with a layout runtime component and an assigned layout profile."), LayoutGeneratorName));
		return false;
	}

	// A newer request replaces an in-flight preview through the facade generation guard.
	// Binding-aware previews participate in the same world-owned lattice contract as
	// ordinary chunk-generation sites. Use the hovered block directly so nearest-plane
	// snapping is based on the global cell lattice instead of an editor-only +1 bias.
	const FIntVector RawSiteCenterBlockWorldPos =
		SettingsObject->LayoutWorldBinding != nullptr
			? HoveredBlockWorldPos
			: HoveredBlockWorldPos + FIntVector(0, 0, 1);
	CachedPreviewChunkWorld = ChunkWorld;
	FString FailureReason;
	bHasCachedPreviewAttempt = true;
	bPreviewSolveInProgress = true;
	LastApplySeconds = -1.0;
	LastApplyFailureReason.Reset();

	const int32 AsyncSolveSeed = ResolveSolveSeed(*ChunkWorld, RawSiteCenterBlockWorldPos);
	FLayoutGeneratorSolveRequest AsyncRequest;
	AsyncRequest.RuntimeComponent = LayoutRuntimeComponent;
	AsyncRequest.RequestedSiteCenterBlockWorldPos = RawSiteCenterBlockWorldPos;
	AsyncRequest.WorldBinding = SettingsObject->LayoutWorldBinding;
	AsyncRequest.LayoutProfile = SettingsObject->LayoutProfile;
	AsyncRequest.SolveSeed = AsyncSolveSeed;
	SetStatusMessage(FString::Printf(TEXT("%s preview scheduled at %s."), LayoutGeneratorName, *RawSiteCenterBlockWorldPos.ToString()));
	const TSharedRef<FThreadSafeCounter, ESPMode::ThreadSafe> AutomationCompletionCounter =
		MakeShared<FThreadSafeCounter, ESPMode::ThreadSafe>();
	FLayoutGeneratorSolveFacade::SubmitExplicitRoot(
		AsyncRequest,
		[this, ChunkWorld, RawSiteCenterBlockWorldPos, AutomationCompletionCounter](const FLayoutGeneratorSolveResult& Result)
		{
			CachedSiteRecord = Result.SiteRecord;
			CachedScheduleResult = Result.ScheduleResult;
			const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection = CachedSiteRecord.GetSiteSolveSourceSelection();
			ULayoutRegionContentSetAsset* const CachedContentSet = SiteSolveSourceSelection.ContentSet.Get() != nullptr
				? SiteSolveSourceSelection.ContentSet.Get()
				: SiteSolveSourceSelection.ContentSet.LoadSynchronous();
			RehydratePreviewPlacementCarriersFromContentSet(CachedContentSet, CachedSiteRecord, CachedScheduleResult);
			const FIntVector ResolvedCenter = Result.SiteRecord.GetResolvedSiteLocationMetadata().SiteCenterBlockWorldPos;
			// A retained plan crossed the runtime completion gate, including valid snapped world zero.
			CachedPreviewSiteCenterBlockWorldPos = Result.SiteRecord.bLayoutSolved
				|| !Result.ScheduleResult.MergedSolveResult.PlannedCells.IsEmpty()
				|| ResolvedCenter != FIntVector::ZeroValue
				? ResolvedCenter : RawSiteCenterBlockWorldPos;
			LastPreviewSolveSeconds = Result.TotalSolveSeconds;
			CachedSharedCellSizeInBlocks = ResolveCachedSharedCellSizeInBlocks();
			CachedPreviewFrozenTerrainContract = FLayoutFrozenTerrainContract();
			bHasCachedPreviewTerrainFit = false;
			CachedPreviewTerrainAnchorBlockWorldPos = FIntVector::ZeroValue;
			CachedPreviewTerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;
			CachedPreviewTerrainWritePositions.Reset();
			CachedPreviewTerrainWriteMaterials.Reset();
			CachedPreviewFailureReason.Reset();
			if (!Result.bSucceeded && !CachedSiteRecord.CachedFrozenTerrainBaseZByColumn.IsEmpty())
			{
				// Cache the rejected diagnostic origin once, not once per cell/face draw.
				CachedPreviewTerrainAnchorBlockWorldPos.Z = MAX_int32;
				for (const TPair<FIntPoint, int32>& Column : CachedSiteRecord.CachedFrozenTerrainBaseZByColumn)
				{
					CachedPreviewTerrainAnchorBlockWorldPos.Z = FMath::Min(CachedPreviewTerrainAnchorBlockWorldPos.Z, Column.Value);
				}
			}

			if (!Result.bSucceeded)
			{
				const bool bHasRetainedPartial =
					!Result.SiteRecord.SolveResult.Placements.IsEmpty()
					&& !Result.SiteRecord.SolvedArtifactId.IsNone()
					&& Result.SiteRecord.SolvedArtifactActiveCellCount > 0;
				const bool bHasPlannedCells =
					(Result.SiteRecord.SolveResult.FootprintSize.X > 0
						&& Result.SiteRecord.SolveResult.FootprintSize.Y > 0)
					|| !Result.SiteRecord.SolveResult.PlannedCells.IsEmpty();
				// Retention is diagnostic, independent of permission to stamp invalid geometry.
				if (bHasRetainedPartial || bHasPlannedCells)
				{
					UE_LOG(LogTemp, Display, TEXT("[LayoutPreview.Callback] Partial preview retained: placementRecords=%d plannedCellsAllLevels=%d. Failure: %s"),
						Result.SiteRecord.SolveResult.Placements.Num(),
						Result.SiteRecord.SolveResult.PlannedCells.Num(),
						*Result.FailureReason);
					bHasCachedAcceptedPreview = false;
					bPreviewSolveInProgress = false;
					CachedPreviewFrozenTerrainContract = Result.FrozenTerrainContract;
					CachedSharedCellSizeInBlocks = ResolveCachedSharedCellSizeInBlocks();
					BuildCellDebugInfos();
					CachedPreviewFailureReason = FString::Printf(
						TEXT("Invalid partial: %d placement records across %d planned cells (all levels) — %s"),
						Result.SiteRecord.SolveResult.Placements.Num(),
						Result.SiteRecord.SolveResult.PlannedCells.Num(),
						*Result.FailureReason);
					SetStatusMessage(FString::Printf(
						TEXT("%s partial preview at %s (%d placement records across %d planned cells on all levels). Reason: %s"),
						LayoutGeneratorName,
						*CachedPreviewSiteCenterBlockWorldPos.ToString(),
						Result.SiteRecord.SolveResult.Placements.Num(),
						Result.SiteRecord.SolveResult.PlannedCells.Num(),
						*Result.FailureReason));
					AutomationCompletionCounter->Increment();
					return;
				}

				bHasCachedAcceptedPreview = false;
				bPreviewSolveInProgress = false;
				CachedPreviewFailureReason = Result.FailureReason;
				BuildCellDebugInfos();
				SetStatusMessage(FString::Printf(
					TEXT("%s preview rejected at %s. Reason: %s"),
					LayoutGeneratorName,
					*CachedPreviewSiteCenterBlockWorldPos.ToString(),
					*CachedPreviewFailureReason));
				AutomationCompletionCounter->Increment();
				return;
			}

			bHasCachedAcceptedPreview = true;
			bPreviewSolveInProgress = false;
			CachedPreviewFrozenTerrainContract = Result.FrozenTerrainContract;
			CachePreviewTerrainStateFromFrozenContract(
				CachedPreviewFrozenTerrainContract,
				CachedSiteRecord.SolveResult.RootPlacementKind,
				bHasCachedPreviewTerrainFit,
				CachedPreviewTerrainAnchorBlockWorldPos,
				CachedPreviewTerrainFitDiagnosticKind,
				CachedPreviewTerrainWritePositions,
				CachedPreviewTerrainWriteMaterials);
			// Publish derived drawing caches only after the matching frozen terrain payload is installed.
			BuildCellDebugInfos();
			const FString SnappedSummary = CachedPreviewSiteCenterBlockWorldPos != RawSiteCenterBlockWorldPos
				? FString::Printf(TEXT(" snapped from %s"), *RawSiteCenterBlockWorldPos.ToString())
				: FString();
			SetStatusMessage(FString::Printf(
				TEXT("%s preview accepted asynchronously at %s%s. Footprint=%s placements=%d."),
				LayoutGeneratorName,
				*CachedPreviewSiteCenterBlockWorldPos.ToString(),
				*SnappedSummary,
				*Result.ScheduleResult.MergedSolveResult.FootprintSize.ToString(),
				Result.ScheduleResult.MergedSolveResult.Placements.Num()));
			AutomationCompletionCounter->Increment();
		},
		&PreviewAsyncState);
	if (GIsAutomationTesting && bPumpUntilCompletionForAutomation)
	{
		for (int32 PumpIndex = 0; PumpIndex < 300 && AutomationCompletionCounter->GetValue() == 0; ++PumpIndex)
		{
			LayoutRuntimeComponent->PumpBackgroundLayoutSolves();
		}
		LayoutRuntimeComponent->PumpBackgroundLayoutSolves();
		return bHasCachedAcceptedPreview;
	}
	return true;

}

bool FLayoutDirectRootGenerationController::ApplyCachedSolve(const bool bAllowInvalidPreview)
{
	if (bPreviewSolveInProgress || !SettingsObject.IsValid())
	{
		SetStatusMessage(TEXT("Apply requires a settled preview and live editor settings."));
		return false;
	}
	LastApplyFailureReason.Reset();
	const bool bDebugApply = bAllowInvalidPreview && SettingsObject.IsValid()
		&& SettingsObject->bAllowPartialPreviewApply;
	if (!bDebugApply && ((!CachedScheduleResult.bSucceeded && !bHasCachedAcceptedContinuationPreview)
		|| bCachedContinuationPreviewIsPartial))
	{
		SetStatusMessage(TEXT("Normal Apply requires an accepted solve. Use Apply Invalid Preview to debug retained geometry; write-safety guards still apply."));
		return false;
	}
	AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get();
	if (bHasCachedAcceptedContinuationPreview)
	{
		UChunkWorldLayoutRuntimeComponent* const ContinuationRuntime =
			ChunkWorld != nullptr ? ChunkWorld->GetLayoutRuntimeComponent() : nullptr;
		FString FailureReason;
		const bool bAllowPartial = bDebugApply;
		if (ContinuationRuntime == nullptr || CachedContinuationSegments.IsEmpty())
		{
			bHasCachedAcceptedContinuationPreview = false;
			SetStatusMessage(TEXT("Connect Layouts apply failed: cached continuation route has no segments."));
			return false;
		}
		int32 AppliedSegmentCount = 0;
		TArray<FString> UnappliedSegmentLabels;
		TArray<FString> UnappliedSegmentReasons;
		for (const FLayoutGeneratorContinuationSegmentSolveResult& Segment : CachedContinuationSegments)
		{
			const FString SegmentLabel = FString::FromInt(Segment.Descriptor.SegmentIndex);
			if (!Segment.bSucceeded && !(bAllowPartial && Segment.bIsPartial))
			{
				UnappliedSegmentLabels.Add(SegmentLabel);
				UnappliedSegmentReasons.Add(FString::Printf(
					TEXT("Segment %s: %s"),
					*SegmentLabel,
					*Segment.FailureReason));
				continue;
			}
			FailureReason.Reset();
			if (!ContinuationRuntime->TryApplySolvedExplicitConnector(
					Segment.ConnectorRecord,
					Segment.FrozenTerrainContract,
					&FailureReason))
			{
				UnappliedSegmentLabels.Add(SegmentLabel);
				UnappliedSegmentReasons.Add(FString::Printf(TEXT("Segment %s: %s"), *SegmentLabel, *FailureReason));
				continue;
			}
			++AppliedSegmentCount;
		}
		const FString UnappliedReasonSummary = FString::Join(UnappliedSegmentReasons, TEXT(" | "));
		if (AppliedSegmentCount == 0)
		{
			SetStatusMessage(FString::Printf(TEXT("Connect Layouts apply failed. Unapplied segments: %s. %s"),
				*FString::Join(UnappliedSegmentLabels, TEXT(", ")),
				*UnappliedReasonSummary));
			return false;
		}
		SetStatusMessage(UnappliedSegmentLabels.IsEmpty()
			? (bCachedContinuationPreviewIsPartial
				? TEXT("Connect Layouts applied cached partial continuation placements.")
				: TEXT("Connect Layouts applied cached continuation."))
			: FString::Printf(TEXT("Connect Layouts applied %d segment(s). Unapplied segments: %s. %s"),
				AppliedSegmentCount,
				*FString::Join(UnappliedSegmentLabels, TEXT(", ")),
				*UnappliedReasonSummary));
		return true;
	}
	const bool bAllowPartial = bDebugApply;
	const ELayoutCachedApplyability CachedApplyability =
		CachedSiteRecord.GetResolvedSiteRuntimeState().CachedApplyability;
	const bool bHasRetainedPartial =
		CachedApplyability == ELayoutCachedApplyability::Partial
		&& !CachedSiteRecord.SolveResult.Placements.IsEmpty();
	const bool bHasValidSiteRecord =
		CachedSiteRecord.bWritePlanReady
		&& (CachedApplyability == ELayoutCachedApplyability::Solved
			|| (bAllowPartial && bHasRetainedPartial));

	UChunkWorldLayoutRuntimeComponent* const LayoutRuntimeComponent = ChunkWorld != nullptr ? ChunkWorld->GetLayoutRuntimeComponent() : nullptr;
	if (ChunkWorld == nullptr || LayoutRuntimeComponent == nullptr || !bHasValidSiteRecord)
	{
		SetStatusMessage(FString::Printf(TEXT("%s apply requires a cached preview solve (or partial preview) and the chunk world that produced that preview."), LayoutGeneratorName));
		return false;
	}

	const FResolvedLayoutSiteLocationMetadata CachedLocationMetadata =
		CachedSiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutRootPublicationMetadata CachedPublicationMetadata =
		CachedSiteRecord.GetRootPublicationMetadata();
	if (CachedSiteRecord.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
		&& !bHasCachedAcceptedPreview && !bDebugApply)
	{
		const FString FailureText = !CachedPreviewFailureReason.IsEmpty()
			? CachedPreviewFailureReason
			: TEXT("The cached preview solve never passed terrain-fit validation, so apply would only replay a rejected world-facing site.");
		SetStatusMessage(FString::Printf(
			TEXT("Apply blocked at %s. Preview must be accepted before apply. Reason: %s"),
			*CachedSiteRecord.SiteCenterBlockWorldPos.ToString(),
			*FailureText));
		return false;
	}
	FResolvedLayoutSiteRecord ApplySiteRecord = CachedSiteRecord;
	if (bDebugApply && !CachedScheduleResult.bSucceeded)
	{
		// A parent proof can succeed before a descendant rejects the schedule.
		// Debug stamping must not promote that parent payload to an accepted root.
		ApplySiteRecord.SolveResult.bSucceeded = false;
		if (ApplySiteRecord.SolveResult.FailureReason.IsEmpty())
			ApplySiteRecord.SolveResult.FailureReason = CachedPreviewFailureReason;
	}
	FString FailureReason;
	const double ApplyStartSeconds = FPlatformTime::Seconds();
	const FLayoutFrozenTerrainContract* const CachedContractOverride =
		CachedPreviewFrozenTerrainContract.ContractId.IsNone() ? nullptr : &CachedPreviewFrozenTerrainContract;
	if (!LayoutRuntimeComponent->TryApplySolvedExplicitRootLayoutSite(
			ApplySiteRecord,
			&FailureReason,
			true,
			CachedContractOverride,
			SettingsObject->LayoutWorldBinding,
			bDebugApply))
	{
		LastApplySeconds = FPlatformTime::Seconds() - ApplyStartSeconds;
		LastApplyFailureReason = FailureReason;
		SetStatusMessage(FString::Printf(
			TEXT("Apply failed at %s. Reason: %s"),
			*CachedSiteRecord.SiteCenterBlockWorldPos.ToString(),
			*FailureReason));
		return false;
	}
	LastApplySeconds = FPlatformTime::Seconds() - ApplyStartSeconds;

	LayoutRuntimeComponent->ProcessQueuedLayoutWorkNow();

	const FLayoutSiteSolveSourceSelection CachedSolveSourceSelection =
		CachedSiteRecord.GetSiteSolveSourceSelection();
	const TArray<FResolvedLayoutSiteRecord> ResolvedSiteRecords =
		LayoutRuntimeComponent->GetResolvedLayoutSiteRecords();
	const FResolvedLayoutSiteRecord* const AppliedSiteRecord = ResolvedSiteRecords.FindByPredicate(
		[&CachedLocationMetadata, &CachedPublicationMetadata, &CachedSolveSourceSelection](const FResolvedLayoutSiteRecord& Candidate)
		{
			const FResolvedLayoutSiteLocationMetadata CandidateLocationMetadata =
				Candidate.GetResolvedSiteLocationMetadata();
			const FLayoutRootPublicationMetadata CandidatePublicationMetadata =
				Candidate.GetRootPublicationMetadata();
			const FLayoutSiteSolveSourceSelection CandidateSolveSourceSelection =
				Candidate.GetSiteSolveSourceSelection();
			return CandidateLocationMetadata.SiteCenterBlockWorldPos == CachedLocationMetadata.SiteCenterBlockWorldPos
				&& CandidatePublicationMetadata.RootSolveId == CachedPublicationMetadata.RootSolveId
				&& CandidateSolveSourceSelection.SolveSeed == CachedSolveSourceSelection.SolveSeed;
		});
	if (AppliedSiteRecord == nullptr)
	{
		SetStatusMessage(FString::Printf(
			TEXT("Applied cached explicit-root layout at %s, but the runtime cache no longer exposes the resolved site record."),
			*CachedSiteRecord.SiteCenterBlockWorldPos.ToString()));
		return false;
	}

	CachedSiteRecord = *AppliedSiteRecord;
	const FResolvedLayoutSiteRuntimeState RuntimeState =
		CachedSiteRecord.GetResolvedSiteRuntimeState();
	if (!RuntimeState.bLayoutRealized || !RuntimeState.bHasBeenCommittedToChunkWorld)
	{
		const FString ApplyFailureDetail = FailureReason.IsEmpty()
			? TEXT("The runtime site record never reached a realized+committed state during apply.")
			: FailureReason;
		SetStatusMessage(FString::Printf(
			TEXT("Apply failed at %s. %s"),
			*CachedSiteRecord.SiteCenterBlockWorldPos.ToString(),
			*ApplyFailureDetail));
		return false;
	}

	SetStatusMessage(FString::Printf(
		TEXT("Applied cached explicit-root layout at %s. The solved site is now committed to the chunk world."),
		*CachedSiteRecord.SiteCenterBlockWorldPos.ToString()));
	return true;
}

void FLayoutDirectRootGenerationController::ClearCachedSolve()
{
	AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get();
	UChunkWorldLayoutRuntimeComponent* const LayoutRuntimeComponent = ChunkWorld != nullptr ? ChunkWorld->GetLayoutRuntimeComponent() : nullptr;
	FLayoutGeneratorSolveFacade::CancelSubmittedSolve(LayoutRuntimeComponent, PreviewAsyncState);
	FLayoutGeneratorSolveFacade::CancelSubmittedSolve(LayoutRuntimeComponent, ContinuationPreviewAsyncState);
	if (LayoutRuntimeComponent != nullptr && !CachedContinuationSegments.IsEmpty())
	{
		LayoutRuntimeComponent->CancelExplicitContinuationLayoutSolve(CachedContinuationSegments[0].ConnectorRecord);
	}
	bPreviewSolveInProgress = false;
	bHasSelectedContinuationStartEndpoint = false;
	SelectedContinuationStartEndpoint = FResolvedLayoutConnectorEndpoint();
	SelectedContinuationChunkWorld.Reset();
	bContinuationPreviewInProgress = false;
	bHasCachedAcceptedContinuationPreview = false;
	CachedContinuationSegments.Reset();
	CachedContinuationCellDebugInfos.Reset();
	bCachedContinuationPreviewIsPartial = false;
	CachedSiteRecord = FResolvedLayoutSiteRecord();
	CachedScheduleResult = FLayoutRegionSolveScheduleResult();
	CachedPreviewChunkWorld.Reset();
	CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
	CachedPreviewSiteCenterBlockWorldPos = FIntVector::ZeroValue;
	bHasCachedAcceptedPreview = false;
	CachedPreviewFrozenTerrainContract = FLayoutFrozenTerrainContract();
	bHasCachedPreviewTerrainFit = false;
	CachedPreviewTerrainAnchorBlockWorldPos = FIntVector::ZeroValue;
	CachedPreviewTerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;
	CachedPreviewTerrainWritePositions.Reset();
	CachedPreviewTerrainWriteMaterials.Reset();
	CachedPreviewFailureReason.Reset();
	LastApplyFailureReason.Reset();
	bHasCachedPreviewAttempt = false;
	LastPreviewSolveSeconds = 0.0;
	LastApplySeconds = -1.0;
	CachedCellDebugInfos.Reset();
	CachedTerrainPreviewRuns.Empty();
	CachedContinuationTerrainPreviewRuns.Empty();
	SetStatusMessage(TEXT("Cleared the cached explicit-root preview solve."));
}

FText FLayoutDirectRootGenerationController::GetStatusText() const
{
	return FText::FromString(BuildPersistentDiagnosticsText());
}

FText FLayoutDirectRootGenerationController::GetGenerationSummaryText() const
{
	return FText::FromString(BuildGenerationSummaryText());
}

FText FLayoutDirectRootGenerationController::GetInstructionText() const
{
	return FText::FromString(
		TEXT("Workflow\n")
		TEXT("  1. Pick a Layout Profile in Generation Settings.\n")
		TEXT("  2. Optionally assign a Layout World Binding when binding-owned lattice or continuation policy should drive the preview.\n")
		TEXT("  3. Hover a chunk-world surface in the active level viewport.\n")
		TEXT("  4. Root profile: Ctrl + Left Click runs one root preview. Continuation profile: Ctrl + Left Click source exported entry, then compatible target entry.\n")
		TEXT("  5. Layout Generator snaps world-binding-driven previews to the nearest valid binding lattice plane automatically; there is no separate snap mode to manage.\n")
		TEXT("  6. Read Generation Attempt for each continuation segment's full, partial, or failed state, placement count, and failure reason.\n")
		TEXT("  7. Use the layer toggles in Generation Settings to inspect root-equivalent debug markers on every solved continuation segment. Pink route line shows the discovered path; red bounds/line mark failed segments.\n")
		TEXT("  8. Use Apply Cached Solve only after you want the accepted result stamped into the chunk world.\n")
		TEXT("\n")
		TEXT("Important\n")
		TEXT("  Ctrl + Left Click does not draw modules into the chunk world. It only caches a preview solve and draws editor overlays.\n")
		TEXT("  Apply Cached Solve is the step that commits the accepted cached result into the chunk world."));
}

TArray<FLayoutDirectRootGenerationController::FTerrainPreviewRun>
FLayoutDirectRootGenerationController::BuildTerrainPreviewRuns(const TArray<FLayoutFrozenTerrainWriteRecord>& Writes)
{
	TArray<FTerrainPreviewRun> Runs;
	Runs.Reserve(Writes.Num());
	for (const FLayoutFrozenTerrainWriteRecord& Write : Writes)
	{
		Runs.Add({Write.BlockWorldPos, Write.BlockWorldPos, Write.Material == EmptyMaterial});
	}
	Runs.StableSort([](const FTerrainPreviewRun& A, const FTerrainPreviewRun& B)
	{
		if (A.Min.Z != B.Min.Z) return A.Min.Z < B.Min.Z;
		if (A.Min.Y != B.Min.Y) return A.Min.Y < B.Min.Y;
		return A.Min.X < B.Min.X;
	});
	// ponytail: merge X runs only; multi-axis packing belongs here if row counts remain costly.
	int32 Count = 0;
	for (int32 Index = 0; Index < Runs.Num(); ++Index)
	{
		const FTerrainPreviewRun Current = Runs[Index];
		// Stable coordinate order preserves the final write's color at duplicate positions.
		if (Index + 1 < Runs.Num() && Runs[Index + 1].Min == Current.Min) continue;
		if (Count > 0 && Runs[Count - 1].bEmpty == Current.bEmpty
			&& Runs[Count - 1].Min.Y == Current.Min.Y && Runs[Count - 1].Min.Z == Current.Min.Z
			&& static_cast<int64>(Runs[Count - 1].Max.X) + 1 >= Current.Min.X)
		{
			Runs[Count - 1].Max.X = FMath::Max(Runs[Count - 1].Max.X, Current.Max.X);
		}
		else
		{
			Runs[Count++] = Current;
		}
	}
	Runs.SetNum(Count);
	Runs.Shrink();
	return Runs;
}

void FLayoutDirectRootGenerationController::RenderTerrainPreviewRuns(const FSceneView* View,
	FPrimitiveDrawInterface* PDI, const AChunkWorldExtended& ChunkWorld, const TArray<FTerrainPreviewRun>& Runs)
{
	const FVector BlockExtent(FMath::Max(0.0, ResolveBlockSizeUnrealUnits(ChunkWorld) * 0.5 - 1.0));
	for (const FTerrainPreviewRun& Run : Runs)
	{
		const FBox Bounds(ChunkWorld.BlockWorldPosToUEWorldPos(Run.Min) - BlockExtent,
			ChunkWorld.BlockWorldPosToUEWorldPos(Run.Max) + BlockExtent);
		if (View != nullptr && !View->ViewFrustum.IntersectBox(Bounds.GetCenter(), Bounds.GetExtent())) continue;
		DrawWireBox(PDI, Bounds, Run.bEmpty ? FColor(255, 110, 110) : FColor(110, 220, 140), SDPG_Foreground);
	}
}

void FLayoutDirectRootGenerationController::Render(const FSceneView* const View, FPrimitiveDrawInterface* const PDI) const
{
	if (!bToolActive || PDI == nullptr)
	{
		return;
	}

	const AChunkWorldExtended* const HoverChunkWorld = HoveredChunkWorld.Get();
	if (SettingsObject->bDrawHoverSquare && bHasHoveredBlock && HoverChunkWorld != nullptr)
	{
		const int32 BlockSize = ResolveBlockSizeUnrealUnits(*HoverChunkWorld);
		const FVector HoverCenter = HoverChunkWorld->BlockWorldPosToUEWorldPos(HoveredBlockWorldPos);
		const FVector HoverExtent = FVector(static_cast<double>(BlockSize) * 0.5, static_cast<double>(BlockSize) * 0.5, 8.0);
		DrawWireBox(
			PDI,
			FBox(HoverCenter - HoverExtent, HoverCenter + HoverExtent),
			FColor(80, 220, 255),
			SDPG_Foreground);
	}

	if (bHasSelectedContinuationStartEndpoint)
	{
		if (const AChunkWorldExtended* const SourceChunkWorld = SelectedContinuationChunkWorld.Get())
		{
			const int32 BlockSize = ResolveBlockSizeUnrealUnits(*SourceChunkWorld);
			const FVector SourceCenter = SourceChunkWorld->BlockWorldPosToUEWorldPos(
				SelectedContinuationStartEndpoint.EndpointBlockWorldPos);
			const FVector SourceExtent(static_cast<double>(BlockSize) * 0.75);
			DrawWireBox(
				PDI,
				FBox(SourceCenter - SourceExtent, SourceCenter + SourceExtent),
				FColor::Cyan,
				SDPG_Foreground);
		}
	}

	const bool bRenderingContinuationPreview = bHasCachedAcceptedContinuationPreview;
	if (bRenderingContinuationPreview)
	{
		if (const AChunkWorldExtended* const ContinuationChunkWorld = CachedPreviewChunkWorld.Get())
		{
			RenderContinuationPreview(View, PDI, *ContinuationChunkWorld);
		}
		return;
	}

	if (CachedCellDebugInfos.IsEmpty() || CachedSharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return;
	}

	const AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get();
	if (ChunkWorld == nullptr)
	{
		return;
	}

	const int32 BlockSize = ResolveBlockSizeUnrealUnits(*ChunkWorld);
	const FLayoutSolveResult* DiagnosticResult = GetCachedDiagnosticSolveResult();
	if (DiagnosticResult == nullptr) return;
	const FLayoutSolveResult& OverlaySolveResult = *DiagnosticResult;
	const FLayoutRegionSolveScheduleResult& OverlayScheduleResult = CachedScheduleResult;
	const FIntVector RootFootprintMinBlockWorldPos = ResolveCachedRootFootprintMinBlockWorldPos();
	auto ResolveOverlayCellCenter = [this](const FIntVector& Cell)
	{
		return ResolveCachedCellCenterWorld(Cell);
	};

	if (!bRenderingContinuationPreview && SettingsObject->bDrawSolvedFootprint)
	{
		const FIntVector RootFootprintSizeInBlocks(
			OverlaySolveResult.FootprintSize.X * CachedSharedCellSizeInBlocks.X,
			OverlaySolveResult.FootprintSize.Y * CachedSharedCellSizeInBlocks.Y,
			ResolveSolveHeightInCells(OverlaySolveResult) * CachedSharedCellSizeInBlocks.Z);

		// 10% cell-size offset so bounds enclose entire cells, not just their centers.
		const FVector CellSizeUnits(
			static_cast<double>(CachedSharedCellSizeInBlocks.X) * static_cast<double>(BlockSize),
			static_cast<double>(CachedSharedCellSizeInBlocks.Y) * static_cast<double>(BlockSize),
			static_cast<double>(CachedSharedCellSizeInBlocks.Z) * static_cast<double>(BlockSize));
		const FVector OuterOffset = CellSizeUnits * 0.1;

		const FVector RootCenter = ChunkWorld->BlockWorldPosToUEWorldPos(
			FIntVector(
				RootFootprintMinBlockWorldPos.X + RootFootprintSizeInBlocks.X / 2,
				RootFootprintMinBlockWorldPos.Y + RootFootprintSizeInBlocks.Y / 2,
				RootFootprintMinBlockWorldPos.Z + RootFootprintSizeInBlocks.Z / 2));
		const FVector RootExtent(
			static_cast<double>(RootFootprintSizeInBlocks.X) * 0.5 * static_cast<double>(BlockSize) + OuterOffset.X,
			static_cast<double>(RootFootprintSizeInBlocks.Y) * 0.5 * static_cast<double>(BlockSize) + OuterOffset.Y,
			static_cast<double>(RootFootprintSizeInBlocks.Z) * 0.5 * static_cast<double>(BlockSize) + OuterOffset.Z);
		DrawWireBox(PDI, FBox(RootCenter - RootExtent, RootCenter + RootExtent), FColor::Yellow, SDPG_Foreground);
	}

	if (SettingsObject->bMergeTerrainPreviewBlocks)
	{
		RenderTerrainPreviewRuns(View, PDI, *ChunkWorld, CachedTerrainPreviewRuns);
	}
	else if (!CachedPreviewTerrainWritePositions.IsEmpty())
	{
		const FVector BlockExtent(
			static_cast<double>(BlockSize) * 0.5 - 1.0,
			static_cast<double>(BlockSize) * 0.5 - 1.0,
			static_cast<double>(BlockSize) * 0.5 - 1.0);
		for (int32 Index = 0; Index < CachedPreviewTerrainWritePositions.Num(); ++Index)
		{
			const FIntVector& BlockWorldPos = CachedPreviewTerrainWritePositions[Index];
			const int32 MaterialIndex =
				CachedPreviewTerrainWriteMaterials.IsValidIndex(Index)
					? CachedPreviewTerrainWriteMaterials[Index]
					: EmptyMaterial;
			const FColor TerrainWriteColor =
				MaterialIndex == EmptyMaterial ? FColor(255, 110, 110) : FColor(110, 220, 140);
			const FVector Center = ChunkWorld->BlockWorldPosToUEWorldPos(BlockWorldPos);
			DrawWireBox(
				PDI,
				FBox(Center - BlockExtent, Center + BlockExtent),
				TerrainWriteColor,
				SDPG_Foreground);
		}
	}

	if (CachedCellDebugInfos.IsEmpty())
	{
		return;
	}

	// Resolve the set of occupied cell positions for unoccupied-cell detection.
	TSet<FIntVector> OccupiedCells;
	for (const FLayoutPlacedModule& Placement : OverlaySolveResult.Placements)
	{
		for (const FIntVector& PlacementCell : BuildDirectRootPlacementCells(Placement))
		{
			OccupiedCells.Add(PlacementCell);
		}
	}
	// Include child-region placements so cells with child-region modules don't falsely show the unoccupied marker.
	for (const FLayoutRegionSolveResult& RegionResult : OverlayScheduleResult.RegionResults)
	{
		if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
		{
			continue;
		}

		for (const FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
		{
			for (const FIntVector& PlacementCell : BuildDirectRootPlacementCells(Placement))
			{
				OccupiedCells.Add(RegionResult.RegionCellOffset + PlacementCell);
			}
		}
	}

	const FVector CellSizeUnits(
		static_cast<double>(CachedSharedCellSizeInBlocks.X) * static_cast<double>(BlockSize),
		static_cast<double>(CachedSharedCellSizeInBlocks.Y) * static_cast<double>(BlockSize),
		static_cast<double>(CachedSharedCellSizeInBlocks.Z) * static_cast<double>(BlockSize));
	const float SphereRadius = static_cast<float>(CellSizeUnits.GetMin() * 0.0875);
	const float PyramidBaseRadius = SphereRadius * 0.7f;
	// Pyramid centroid at 75% of the cell half-extent toward the boundary face,
	// keeping it clear of the zone sphere at center.
	const float PyramidPlacementOffset = static_cast<float>(CellSizeUnits.GetMin() * 0.375);

	for (const FLayoutCellDebugInfo& DebugInfo : CachedCellDebugInfos)
	{
		const FVector CellCenter = ResolveOverlayCellCenter(DebugInfo.Cell);
		if (DrawCellOverview(View, PDI, *SettingsObject, CellCenter, CellSizeUnits, DebugInfo.Intent)) continue;

		// Center sphere exposes planner intent while borders show placement zones.
		if (SettingsObject->bDrawCellZoneMarkers)
		{
			const FColor IntentColor = ColorForIntent(DebugInfo.Intent);
			constexpr float IntentSphereThickness = 2.0f;
			::DrawCircle(PDI, CellCenter, FVector(1, 0, 0), FVector(0, 1, 0), FLinearColor(IntentColor), SphereRadius, 12, SDPG_Foreground, IntentSphereThickness);
			::DrawCircle(PDI, CellCenter, FVector(1, 0, 0), FVector(0, 0, 1), FLinearColor(IntentColor), SphereRadius, 12, SDPG_Foreground, IntentSphereThickness);
			::DrawCircle(PDI, CellCenter, FVector(0, 1, 0), FVector(0, 0, 1), FLinearColor(IntentColor), SphereRadius, 12, SDPG_Foreground, IntentSphereThickness);
		}

		// Zone borders preserve both local topology and terrain-adjusted placement selection.
		if (SettingsObject->bDrawCellZoneMarkers)
		{
			const FVector BaseZoneExtent(SphereRadius * 1.45f);
			DrawWireBox(
				PDI,
				FBox(CellCenter - BaseZoneExtent, CellCenter + BaseZoneExtent),
				ColorForZone(DebugInfo.BaseZone),
				SDPG_Foreground,
				1.5f);
			if (DebugInfo.BaseZone != DebugInfo.EffectiveZone)
			{
				const FVector EffectiveZoneExtent(SphereRadius * 1.20f);
				DrawWireBox(
					PDI,
					FBox(CellCenter - EffectiveZoneExtent, CellCenter + EffectiveZoneExtent),
					ColorForZone(DebugInfo.EffectiveZone),
					SDPG_Foreground,
					2.0f);
			}
		}

		// Boundary face pyramids
		if (SettingsObject->bDrawBoundaryFacePyramids && !DebugInfo.RegionBoundaryFaces.IsEmpty())
		{
			for (const ELayoutFaceDirection FaceDir : DebugInfo.RegionBoundaryFaces)
			{
				const FVector FaceVector = ResolveFaceDirectionVector(FaceDir);
				// Place the pyramid centroid at PyramidPlacementOffset from cell center toward the face.
				const FVector PyramidCenter = CellCenter + FaceVector * PyramidPlacementOffset;

				// Build a tetrahedron with base facing the cell center and tip pointing outward.
				FVector PerpA, PerpB;
				FaceVector.FindBestAxisVectors(PerpA, PerpB);
				PerpA *= PyramidBaseRadius;
				PerpB *= PyramidBaseRadius;

				const FVector BaseV0 = PyramidCenter - FaceVector * PyramidBaseRadius * 0.6f + PerpA;
				const FVector BaseV1 = PyramidCenter - FaceVector * PyramidBaseRadius * 0.6f - PerpA * 0.5f + PerpB * 0.866f;
				const FVector BaseV2 = PyramidCenter - FaceVector * PyramidBaseRadius * 0.6f - PerpA * 0.5f - PerpB * 0.866f;
				const FVector Tip     = PyramidCenter + FaceVector * PyramidBaseRadius * 0.9f;

				const FColor PyramidColor(255, 255, 80); // bright yellow
				PDI->DrawLine(BaseV0, BaseV1, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(BaseV1, BaseV2, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(BaseV2, BaseV0, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(Tip, BaseV0, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(Tip, BaseV1, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(Tip, BaseV2, PyramidColor, SDPG_Foreground, 1.5f);
			}
		}

		if (SettingsObject->bDrawCellZoneMarkers && DebugInfo.TerrainSeamFaceMask != 0)
		{
			for (const ELayoutFaceDirection Direction : {
				ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
			{
				if (!LayoutFaceMaskContainsDirection(DebugInfo.TerrainSeamFaceMask, Direction))
				{
					continue;
				}
				const FVector FaceVector = ResolveFaceDirectionVector(Direction);
				const FVector FaceAbs = FaceVector.GetAbs();
				const float FaceDepth = static_cast<float>(CellSizeUnits.GetMin() * 0.02f);
				const float FaceHalfWidth = static_cast<float>(CellSizeUnits.GetMin() * 0.18f);
				const FVector FaceExtent = (FVector::OneVector - FaceAbs) * FaceHalfWidth + FaceAbs * FaceDepth;
				const float CellHalfInFaceDirection = CellSizeUnits.Dot(FaceAbs) * 0.5f;
				const FVector FaceCenter = CellCenter + FaceVector * (CellHalfInFaceDirection + FaceDepth);
				DrawWireBox(
					PDI,
					FBox(FaceCenter - FaceExtent, FaceCenter + FaceExtent),
					FColor(0, 220, 220),
					SDPG_Foreground,
					1.5f);
			}
		}

		// Unoccupied cell marker: red diagonal X
		if (SettingsObject->bDrawUnoccupiedCellMarkers && !OccupiedCells.Contains(DebugInfo.Cell))
		{
			const FVector HalfExtent = CellSizeUnits * 0.30;
			const FColor UnoccupiedColor(255, 40, 40);

			// Diagonal corner-to-corner in X/Y plane at cell center Z
			const FVector MinCorner = CellCenter - FVector(HalfExtent.X, HalfExtent.Y, 0.0);
			const FVector MaxCorner = CellCenter + FVector(HalfExtent.X, HalfExtent.Y, 0.0);

			PDI->DrawLine(
				FVector(MinCorner.X, MinCorner.Y, CellCenter.Z),
				FVector(MaxCorner.X, MaxCorner.Y, CellCenter.Z),
				UnoccupiedColor, SDPG_Foreground, 2.0f);
			PDI->DrawLine(
				FVector(MinCorner.X, MaxCorner.Y, CellCenter.Z),
				FVector(MaxCorner.X, MinCorner.Y, CellCenter.Z),
				UnoccupiedColor, SDPG_Foreground, 2.0f);
		}

		// Planned Entry intent remains visible even when structural placement fails.
		if (SettingsObject->bDrawEntryMarkers
			&& DebugInfo.bIsPlannedEntryCell
			&& !DebugInfo.EntryFaces.IsEmpty())
		{
			for (const ELayoutFaceDirection FaceDir : DebugInfo.EntryFaces)
			{
				if (FaceDir == ELayoutFaceDirection::PosZ || FaceDir == ELayoutFaceDirection::NegZ)
				{
					continue;
				}

				const FVector FaceVector = ResolveFaceDirectionVector(FaceDir);
				const FVector FaceAbs = FaceVector.GetAbs();
				const float FaceDepth = static_cast<float>(CellSizeUnits.GetMin() * 0.12f);
				const float FaceWidth = static_cast<float>(CellSizeUnits.GetMin() * 0.25f);
				const FVector BoxExtent = (FVector::OneVector - FaceAbs) * FaceWidth + FaceAbs * FaceDepth;
				const float CellHalfInFaceDir = CellSizeUnits.Dot(FaceAbs) * 0.5f;
				const FVector FaceCenter = CellCenter + FaceVector * (CellHalfInFaceDir + FaceDepth);
				DrawWireBox(PDI, FBox(FaceCenter - BoxExtent, FaceCenter + BoxExtent), FColor(0, 220, 200), SDPG_Foreground);
			}
		}

		// Vertical access marker: small wire box on the ceiling (PosZ face).
		if (SettingsObject->bDrawVerticalAccessMarkers && DebugInfo.Intent == ELayoutCellIntent::VerticalAccess)
		{
			const float BoxSize = static_cast<float>(CellSizeUnits.GetMin() * 0.25);
			const float BoxDepth = static_cast<float>(CellSizeUnits.GetMin() * 0.12);
			const FVector BoxExtent(BoxSize, BoxSize, BoxDepth);

			// Center on the top face (PosZ), centered on the face plane.
			const float CellHalfZ = static_cast<float>(CellSizeUnits.Z) * 0.5f;
			const FVector BoxCenter = CellCenter + FVector(0.0, 0.0, CellHalfZ);

			const FColor VerticalAccessColor(120, 255, 80); // lime green
			DrawWireBox(PDI, FBox(BoxCenter - BoxExtent, BoxCenter + BoxExtent), VerticalAccessColor, SDPG_Foreground);
		}
	}

	// Child region bounds (simplified to one color)
	if (!bRenderingContinuationPreview && SettingsObject->bDrawChildBounds)
	{
		for (const FLayoutRegionSolveResult& RegionResult : CachedScheduleResult.RegionResults)
		{
			if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
			{
				continue;
			}

			FVector Center = FVector::ZeroVector;
			FVector Extent = FVector::ZeroVector;
			if (!TryResolveRegionBoundsWorld(RegionResult, Center, Extent))
			{
				continue;
			}

			static const FColor ChildBoundsColor(180, 120, 255);
			DrawWireBox(PDI, FBox(Center - Extent, Center + Extent), ChildBoundsColor, SDPG_Foreground);
		}
	}

	// Interface bounds: closure segments and partition seams
	if (SettingsObject->bDrawInterfaceBounds)
	{
		for (const FLayoutClosureCoverageSegmentRecord& Segment : OverlaySolveResult.ClosureSegments)
		{
			const FVector SegmentCenter = ResolveOverlayCellCenter(Segment.Cell) + ResolveFaceDirectionVector(Segment.FaceDirection) * 6.0;
			DrawWireSphere(PDI, SegmentCenter, Segment.bCovered ? FColor::Green : FColor::Red, 5.0f, 8, SDPG_Foreground);
		}

		for (const FLayoutPartitionSeamRecord& Seam : OverlaySolveResult.PartitionSeams)
		{
			const FVector OwnerStart = ResolveOverlayCellCenter(Seam.OwnerStartCell);
			const FVector OwnerEnd = ResolveOverlayCellCenter(Seam.OwnerEndCell);
			const FVector PassiveStart = ResolveOverlayCellCenter(Seam.PassiveStartCell);
			const FVector PassiveEnd = ResolveOverlayCellCenter(Seam.PassiveEndCell);
			PDI->DrawLine(OwnerStart, OwnerEnd, FColor(255, 100, 210), SDPG_Foreground, 2.0f);
			PDI->DrawLine(PassiveStart, PassiveEnd, FColor(120, 90, 255), SDPG_Foreground, 1.5f);
			DrawWireSphere(PDI, OwnerStart, FColor(255, 100, 210), 4.0f, 8, SDPG_Foreground);
			DrawWireSphere(PDI, PassiveStart, FColor(120, 90, 255), 4.0f, 8, SDPG_Foreground);
		}
	}
}

void FLayoutDirectRootGenerationController::RenderContinuationPreview(
	const FSceneView* const View,
	FPrimitiveDrawInterface* const PDI,
	const AChunkWorldExtended& ChunkWorld) const
{
	if (CachedContinuationSegments.IsEmpty())
	{
		return;
	}

	const int32 BlockSize = ResolveBlockSizeUnrealUnits(ChunkWorld);
	const float RouteThickness = 4.0f;
	for (int32 SegmentIndex = 0; SegmentIndex < CachedContinuationSegments.Num(); ++SegmentIndex)
	{
		const FLayoutGeneratorContinuationSegmentSolveResult& Segment = CachedContinuationSegments[SegmentIndex];
		const FLayoutContinuationPreviewGeometry& Geometry = Segment.PreviewGeometry;
		TMap<FIntVector, int32> BaseZByCell;
		BaseZByCell.Reserve(Geometry.TerrainCells.Num());
		for (const FLayoutContinuationPreviewTerrainCell& Cell : Geometry.TerrainCells)
		{
			BaseZByCell.FindOrAdd(Cell.LocalCell, Cell.BaseBlockWorldZ);
		}
		const FIntVector& SegmentCellSize = Geometry.SharedCellSizeInBlocks;
		if (SegmentCellSize == FIntVector::ZeroValue)
		{
			continue;
		}
		const FLayoutSolveResult& SegmentSolveResult = Segment.ConnectorRecord.SolveResult;
		const int32 StructuralAlignmentLevel = ResolveContinuationPreviewAlignmentLevel(
			SegmentSolveResult,
			Segment.ConnectorRecord);
		// Match root preview and realization write plans: contract footprint minimum
		// is cell-space origin, while route origin is only endpoint-planning data.
		const FIntVector ContractFootprintMinBlockWorldPos =
			!Segment.FrozenTerrainContract.ContractId.IsNone()
				? Segment.FrozenTerrainContract.FootprintMinBlockWorldPos
				: Geometry.PathOriginBlockWorldPos - SegmentCellSize / 2;
		const FIntPoint FootprintSize = SegmentSolveResult.FootprintSize != FIntPoint::ZeroValue
			? SegmentSolveResult.FootprintSize
			: Segment.Descriptor.FootprintSize;
		if (SettingsObject->bDrawSolvedFootprint && FootprintSize.X > 0 && FootprintSize.Y > 0)
		{
			const FColor SegmentColor = Segment.bSucceeded
				? FColor::Yellow
				: (Segment.bIsPartial ? FColor::Orange : FColor::Red);
			if (!Geometry.TerrainCells.IsEmpty())
			{
				// Match root preview: one readable footprint bound that encloses
				// bridge/landing levels, rather than a wire box around every cell.
				FIntVector MinCenter = FIntVector::ZeroValue;
				FIntVector MaxCenter = FIntVector::ZeroValue;
				for (int32 CellIndex = 0; CellIndex < Geometry.TerrainCells.Num(); ++CellIndex)
				{
					const FLayoutContinuationPreviewTerrainCell& TerrainCell = Geometry.TerrainCells[CellIndex];
					const FIntVector CellCenter(
						ContractFootprintMinBlockWorldPos.X + TerrainCell.LocalCell.X * SegmentCellSize.X
							+ SegmentCellSize.X / 2,
						ContractFootprintMinBlockWorldPos.Y + TerrainCell.LocalCell.Y * SegmentCellSize.Y
							+ SegmentCellSize.Y / 2,
						TerrainCell.BaseBlockWorldZ
							+ (TerrainCell.LocalCell.Z - StructuralAlignmentLevel) * SegmentCellSize.Z
							+ SegmentSolveResult.TemplatePlacementZOffsetBlocks
							+ SegmentCellSize.Z / 2);
					if (CellIndex == 0)
					{
						MinCenter = CellCenter;
						MaxCenter = CellCenter;
						continue;
					}
					MinCenter.X = FMath::Min(MinCenter.X, CellCenter.X);
					MinCenter.Y = FMath::Min(MinCenter.Y, CellCenter.Y);
					MinCenter.Z = FMath::Min(MinCenter.Z, CellCenter.Z);
					MaxCenter.X = FMath::Max(MaxCenter.X, CellCenter.X);
					MaxCenter.Y = FMath::Max(MaxCenter.Y, CellCenter.Y);
					MaxCenter.Z = FMath::Max(MaxCenter.Z, CellCenter.Z);
				}
				const FVector CellSizeUnits(
					static_cast<double>(SegmentCellSize.X * BlockSize),
					static_cast<double>(SegmentCellSize.Y * BlockSize),
					static_cast<double>(SegmentCellSize.Z * BlockSize));
				const FVector Center = ChunkWorld.BlockWorldPosToUEWorldPos((MinCenter + MaxCenter) / 2);
				// Half a cell reaches each outer face exactly. Larger padding makes
				// adjacent, disjoint segment bounds visibly overlap.
				const FVector Extent(
					static_cast<double>(MaxCenter.X - MinCenter.X) * BlockSize * 0.5 + CellSizeUnits.X * 0.5,
					static_cast<double>(MaxCenter.Y - MinCenter.Y) * BlockSize * 0.5 + CellSizeUnits.Y * 0.5,
					static_cast<double>(MaxCenter.Z - MinCenter.Z) * BlockSize * 0.5 + CellSizeUnits.Z * 0.5);
				DrawWireBox(PDI, FBox(Center - Extent, Center + Extent), SegmentColor, SDPG_Foreground);
			}
			else
			{
				const FVector Extent(
					static_cast<double>(FootprintSize.X * SegmentCellSize.X * BlockSize) * 0.5,
					static_cast<double>(FootprintSize.Y * SegmentCellSize.Y * BlockSize) * 0.5,
					static_cast<double>(SegmentCellSize.Z * BlockSize) * 0.5);
				const FVector Center = ChunkWorld.BlockWorldPosToUEWorldPos(FIntVector(
					ContractFootprintMinBlockWorldPos.X + FootprintSize.X * SegmentCellSize.X / 2,
					ContractFootprintMinBlockWorldPos.Y + FootprintSize.Y * SegmentCellSize.Y / 2,
					ContractFootprintMinBlockWorldPos.Z + SegmentCellSize.Z / 2));
				DrawWireBox(PDI, FBox(Center - Extent, Center + Extent), SegmentColor, SDPG_Foreground);
			}
		}

		const FVector BlockExtent(
			static_cast<double>(BlockSize) * 0.5 - 1.0,
			static_cast<double>(BlockSize) * 0.5 - 1.0,
			static_cast<double>(BlockSize) * 0.5 - 1.0);
		if (SettingsObject->bMergeTerrainPreviewBlocks && CachedContinuationTerrainPreviewRuns.IsValidIndex(SegmentIndex))
		{
			RenderTerrainPreviewRuns(View, PDI, ChunkWorld, CachedContinuationTerrainPreviewRuns[SegmentIndex]);
		}
		else for (const FLayoutFrozenTerrainWriteRecord& WriteRecord : Segment.FrozenTerrainContract.TerrainWrites)
		{
			const FColor TerrainWriteColor = WriteRecord.Material == EmptyMaterial
				? FColor(255, 110, 110)
				: FColor(110, 220, 140);
			const FVector Center = ChunkWorld.BlockWorldPosToUEWorldPos(WriteRecord.BlockWorldPos);
			DrawWireBox(PDI, FBox(Center - BlockExtent, Center + BlockExtent), TerrainWriteColor, SDPG_Foreground);
		}

		const auto ResolveSegmentCellCenter = [&ChunkWorld, &Geometry, &SegmentCellSize, &ContractFootprintMinBlockWorldPos](const FIntVector& Cell, const int32 Index)
		{
			const int32 BaseZ = Geometry.CenterlineBaseBlockWorldZs.IsValidIndex(Index)
				? Geometry.CenterlineBaseBlockWorldZs[Index]
				: ContractFootprintMinBlockWorldPos.Z;
			return ChunkWorld.BlockWorldPosToUEWorldPos(FIntVector(
				ContractFootprintMinBlockWorldPos.X + Cell.X * SegmentCellSize.X + SegmentCellSize.X / 2,
				ContractFootprintMinBlockWorldPos.Y + Cell.Y * SegmentCellSize.Y + SegmentCellSize.Y / 2,
				BaseZ + Cell.Z * SegmentCellSize.Z + SegmentCellSize.Z / 2));
		};
		const FColor SegmentRouteColor = Segment.bSucceeded
			? FColor(255, 80, 220)
			: (Segment.bIsPartial ? FColor::Orange : FColor::Red);
		for (int32 Index = 1; Index < Geometry.CenterlineCells.Num(); ++Index)
		{
			PDI->DrawLine(
				ResolveSegmentCellCenter(Geometry.CenterlineCells[Index - 1], Index - 1),
				ResolveSegmentCellCenter(Geometry.CenterlineCells[Index], Index),
				SegmentRouteColor, SDPG_Foreground, RouteThickness);
		}
		if (Geometry.CenterlineCells.Num() == 1)
		{
			DrawWireSphere(PDI, ResolveSegmentCellCenter(Geometry.CenterlineCells[0], 0), SegmentRouteColor, 10.0f, 8, SDPG_Foreground);
		}
		// Rejected route/module requirements are preview-only. Draw them even when
		// no solved artifact exists, so authors can repair modules from viewport data.
		for (const FLayoutContinuationPreviewDiagnostic& Diagnostic : Geometry.Diagnostics)
		{
			if (!Diagnostic.bHasLocalCell)
			{
				continue;
			}
			const int32* const TerrainBaseZ = BaseZByCell.Find(Diagnostic.LocalCell);
			const int32 BaseZ = TerrainBaseZ != nullptr
				? *TerrainBaseZ
				: ContractFootprintMinBlockWorldPos.Z;
			const FVector DiagnosticCenter = ChunkWorld.BlockWorldPosToUEWorldPos(FIntVector(
				ContractFootprintMinBlockWorldPos.X + Diagnostic.LocalCell.X * SegmentCellSize.X + SegmentCellSize.X / 2,
				ContractFootprintMinBlockWorldPos.Y + Diagnostic.LocalCell.Y * SegmentCellSize.Y + SegmentCellSize.Y / 2,
				BaseZ + (Diagnostic.LocalCell.Z - StructuralAlignmentLevel) * SegmentCellSize.Z
					+ SegmentSolveResult.TemplatePlacementZOffsetBlocks + SegmentCellSize.Z / 2));
			const FColor DiagnosticColor = Diagnostic.Stage == TEXT("VerticalAccess host")
				? FColor::Cyan
				: (Diagnostic.Stage == TEXT("Route requirement") ? FColor::Yellow : FColor::Red);
			DrawWireSphere(PDI, DiagnosticCenter, DiagnosticColor, 14.0f, 8, SDPG_Foreground);
			if (Diagnostic.bHasRelatedLocalCell)
			{
				const int32* const RelatedTerrainBaseZ = BaseZByCell.Find(Diagnostic.RelatedLocalCell);
				const int32 RelatedBaseZ = RelatedTerrainBaseZ != nullptr
					? *RelatedTerrainBaseZ
					: ContractFootprintMinBlockWorldPos.Z;
				const FVector RelatedCenter = ChunkWorld.BlockWorldPosToUEWorldPos(FIntVector(
					ContractFootprintMinBlockWorldPos.X + Diagnostic.RelatedLocalCell.X * SegmentCellSize.X + SegmentCellSize.X / 2,
					ContractFootprintMinBlockWorldPos.Y + Diagnostic.RelatedLocalCell.Y * SegmentCellSize.Y + SegmentCellSize.Y / 2,
					RelatedBaseZ + (Diagnostic.RelatedLocalCell.Z - StructuralAlignmentLevel) * SegmentCellSize.Z
						+ SegmentSolveResult.TemplatePlacementZOffsetBlocks + SegmentCellSize.Z / 2));
				PDI->DrawLine(DiagnosticCenter, RelatedCenter, DiagnosticColor, SDPG_Foreground, 3.0f);
			}
		}
		if (CachedContinuationCellDebugInfos.IsValidIndex(SegmentIndex))
		{
			RenderContinuationCellDebugInfos(
				View,
				PDI,
				ChunkWorld,
				Segment,
				CachedContinuationCellDebugInfos[SegmentIndex], BaseZByCell);
		}
	}
}

void FLayoutDirectRootGenerationController::RenderContinuationCellDebugInfos(
	const FSceneView* const View,
	FPrimitiveDrawInterface* const PDI,
	const AChunkWorldExtended& ChunkWorld,
	const FLayoutGeneratorContinuationSegmentSolveResult& Segment,
	const TArray<FLayoutCellDebugInfo>& CellDebugInfos,
	const TMap<FIntVector, int32>& BaseZByCell) const
{
	const FIntVector CellSize = Segment.PreviewGeometry.SharedCellSizeInBlocks;
	if (CellDebugInfos.IsEmpty() || CellSize == FIntVector::ZeroValue)
	{
		return;
	}

	const int32 BlockSize = ResolveBlockSizeUnrealUnits(ChunkWorld);
	const FIntVector ContractFootprintMinBlockWorldPos =
		!Segment.FrozenTerrainContract.ContractId.IsNone()
			? Segment.FrozenTerrainContract.FootprintMinBlockWorldPos
			: Segment.PreviewGeometry.PathOriginBlockWorldPos - CellSize / 2;
	const FLayoutSolveResult& SolveResult = Segment.ConnectorRecord.SolveResult;
	const int32 StructuralAlignmentLevel = ResolveContinuationPreviewAlignmentLevel(
		SolveResult,
		Segment.ConnectorRecord);
	const FLayoutRegionSolveScheduleResult& ScheduleResult = Segment.ScheduleResult;
	// Frozen contract footprint minimum and stage bases mirror root preview and
	// realization write-plan coordinates for every stepped continuation cell.
	auto ResolveCellCenter = [&ChunkWorld, &BaseZByCell, &CellSize, &ContractFootprintMinBlockWorldPos, &SolveResult, StructuralAlignmentLevel](const FIntVector& Cell)
	{
		const int32* const TerrainBaseZ = BaseZByCell.Find(Cell);
		const int32 BaseZ = TerrainBaseZ != nullptr
			? *TerrainBaseZ
			: ContractFootprintMinBlockWorldPos.Z;
		return ChunkWorld.BlockWorldPosToUEWorldPos(FIntVector(
			ContractFootprintMinBlockWorldPos.X + Cell.X * CellSize.X + CellSize.X / 2,
			ContractFootprintMinBlockWorldPos.Y + Cell.Y * CellSize.Y + CellSize.Y / 2,
			BaseZ + (Cell.Z - StructuralAlignmentLevel) * CellSize.Z
				+ SolveResult.TemplatePlacementZOffsetBlocks + CellSize.Z / 2));
	};

	TSet<FIntVector> OccupiedCells;
	for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
	{
		for (const FIntVector& PlacementCell : BuildDirectRootPlacementCells(Placement))
		{
			OccupiedCells.Add(PlacementCell);
		}
	}
	for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
	{
		if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
		{
			continue;
		}
		for (const FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
		{
			for (const FIntVector& PlacementCell : BuildDirectRootPlacementCells(Placement))
			{
				OccupiedCells.Add(RegionResult.RegionCellOffset + PlacementCell);
			}
		}
	}

	const FVector CellSizeUnits(
		static_cast<double>(CellSize.X) * BlockSize,
		static_cast<double>(CellSize.Y) * BlockSize,
		static_cast<double>(CellSize.Z) * BlockSize);
	const float SphereRadius = static_cast<float>(CellSizeUnits.GetMin() * 0.0875);
	const float PyramidBaseRadius = SphereRadius * 0.7f;
	const float PyramidPlacementOffset = static_cast<float>(CellSizeUnits.GetMin() * 0.375);
	for (const FLayoutCellDebugInfo& DebugInfo : CellDebugInfos)
	{
		const FVector CellCenter = ResolveCellCenter(DebugInfo.Cell);
		if (DrawCellOverview(View, PDI, *SettingsObject, CellCenter, CellSizeUnits, DebugInfo.Intent)) continue;
		if (SettingsObject->bDrawCellZoneMarkers)
		{
			const FColor IntentColor = ColorForIntent(DebugInfo.Intent);
			constexpr float IntentSphereThickness = 2.0f;
			::DrawCircle(PDI, CellCenter, FVector(1, 0, 0), FVector(0, 1, 0), FLinearColor(IntentColor), SphereRadius, 12, SDPG_Foreground, IntentSphereThickness);
			::DrawCircle(PDI, CellCenter, FVector(1, 0, 0), FVector(0, 0, 1), FLinearColor(IntentColor), SphereRadius, 12, SDPG_Foreground, IntentSphereThickness);
			::DrawCircle(PDI, CellCenter, FVector(0, 1, 0), FVector(0, 0, 1), FLinearColor(IntentColor), SphereRadius, 12, SDPG_Foreground, IntentSphereThickness);
			const FVector BaseZoneExtent(SphereRadius * 1.45f);
			DrawWireBox(PDI, FBox(CellCenter - BaseZoneExtent, CellCenter + BaseZoneExtent), ColorForZone(DebugInfo.BaseZone), SDPG_Foreground, 1.5f);
			if (DebugInfo.BaseZone != DebugInfo.EffectiveZone)
			{
				const FVector EffectiveZoneExtent(SphereRadius * 1.20f);
				DrawWireBox(PDI, FBox(CellCenter - EffectiveZoneExtent, CellCenter + EffectiveZoneExtent), ColorForZone(DebugInfo.EffectiveZone), SDPG_Foreground, 2.0f);
			}
		}

		if (SettingsObject->bDrawBoundaryFacePyramids)
		{
			for (const ELayoutFaceDirection FaceDirection : DebugInfo.RegionBoundaryFaces)
			{
				const FVector FaceVector = ResolveFaceDirectionVector(FaceDirection);
				const FVector PyramidCenter = CellCenter + FaceVector * PyramidPlacementOffset;
				FVector PerpA, PerpB;
				FaceVector.FindBestAxisVectors(PerpA, PerpB);
				PerpA *= PyramidBaseRadius;
				PerpB *= PyramidBaseRadius;
				const FVector BaseV0 = PyramidCenter - FaceVector * PyramidBaseRadius * 0.6f + PerpA;
				const FVector BaseV1 = PyramidCenter - FaceVector * PyramidBaseRadius * 0.6f - PerpA * 0.5f + PerpB * 0.866f;
				const FVector BaseV2 = PyramidCenter - FaceVector * PyramidBaseRadius * 0.6f - PerpA * 0.5f - PerpB * 0.866f;
				const FVector Tip = PyramidCenter + FaceVector * PyramidBaseRadius * 0.9f;
				const FColor PyramidColor(255, 255, 80);
				PDI->DrawLine(BaseV0, BaseV1, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(BaseV1, BaseV2, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(BaseV2, BaseV0, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(Tip, BaseV0, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(Tip, BaseV1, PyramidColor, SDPG_Foreground, 1.5f);
				PDI->DrawLine(Tip, BaseV2, PyramidColor, SDPG_Foreground, 1.5f);
			}
		}

		if (SettingsObject->bDrawUnoccupiedCellMarkers && !OccupiedCells.Contains(DebugInfo.Cell))
		{
			const FVector HalfExtent = CellSizeUnits * 0.30;
			const FColor UnoccupiedColor(255, 40, 40);
			PDI->DrawLine(CellCenter - FVector(HalfExtent.X, HalfExtent.Y, 0.0), CellCenter + FVector(HalfExtent.X, HalfExtent.Y, 0.0), UnoccupiedColor, SDPG_Foreground, 2.0f);
			PDI->DrawLine(CellCenter - FVector(HalfExtent.X, -HalfExtent.Y, 0.0), CellCenter + FVector(HalfExtent.X, -HalfExtent.Y, 0.0), UnoccupiedColor, SDPG_Foreground, 2.0f);
		}

		if (SettingsObject->bDrawCellZoneMarkers && DebugInfo.TerrainSeamFaceMask != 0)
		{
			for (const ELayoutFaceDirection Direction : {
				ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
				ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
			{
				if (!LayoutFaceMaskContainsDirection(DebugInfo.TerrainSeamFaceMask, Direction))
				{
					continue;
				}
				const FVector FaceVector = ResolveFaceDirectionVector(Direction);
				const FVector FaceAbs = FaceVector.GetAbs();
				const float FaceDepth = static_cast<float>(CellSizeUnits.GetMin() * 0.02f);
				const float FaceHalfWidth = static_cast<float>(CellSizeUnits.GetMin() * 0.18f);
				const FVector FaceExtent = (FVector::OneVector - FaceAbs) * FaceHalfWidth + FaceAbs * FaceDepth;
				const float CellHalfInFaceDirection = CellSizeUnits.Dot(FaceAbs) * 0.5f;
				const FVector FaceCenter = CellCenter + FaceVector * (CellHalfInFaceDirection + FaceDepth);
				DrawWireBox(PDI, FBox(FaceCenter - FaceExtent, FaceCenter + FaceExtent), FColor(0, 220, 220), SDPG_Foreground, 1.5f);
			}
		}

		if (SettingsObject->bDrawEntryMarkers && DebugInfo.bIsPlannedEntryCell && !DebugInfo.RegionBoundaryFaces.IsEmpty())
		{
			for (const ELayoutFaceDirection FaceDirection : DebugInfo.RegionBoundaryFaces)
			{
				if (FaceDirection == ELayoutFaceDirection::PosZ || FaceDirection == ELayoutFaceDirection::NegZ)
				{
					continue;
				}
				const FVector FaceVector = ResolveFaceDirectionVector(FaceDirection);
				const FVector FaceAbs = FaceVector.GetAbs();
				const float FaceDepth = static_cast<float>(CellSizeUnits.GetMin() * 0.12f);
				const float FaceWidth = static_cast<float>(CellSizeUnits.GetMin() * 0.25f);
				const FVector BoxExtent = (FVector::OneVector - FaceAbs) * FaceWidth + FaceAbs * FaceDepth;
				const float CellHalfInFaceDirection = CellSizeUnits.Dot(FaceAbs) * 0.5f;
				const FVector FaceCenter = CellCenter + FaceVector * (CellHalfInFaceDirection + FaceDepth);
				DrawWireBox(PDI, FBox(FaceCenter - BoxExtent, FaceCenter + BoxExtent), FColor(0, 220, 200), SDPG_Foreground);
			}
		}

		if (SettingsObject->bDrawVerticalAccessMarkers && DebugInfo.Intent == ELayoutCellIntent::VerticalAccess)
		{
			const float BoxSize = static_cast<float>(CellSizeUnits.GetMin() * 0.25);
			const float BoxDepth = static_cast<float>(CellSizeUnits.GetMin() * 0.12);
			const FVector BoxExtent(BoxSize, BoxSize, BoxDepth);
			DrawWireBox(PDI, FBox(CellCenter + FVector(0.0, 0.0, CellSizeUnits.Z * 0.5f) - BoxExtent,
				CellCenter + FVector(0.0, 0.0, CellSizeUnits.Z * 0.5f) + BoxExtent), FColor(120, 255, 80), SDPG_Foreground);
		}
	}

	if (SettingsObject->bDrawChildBounds)
	{
		for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
			{
				continue;
			}
			const FIntVector RegionSizeInBlocks(
				RegionResult.SolveResult.FootprintSize.X * CellSize.X,
				RegionResult.SolveResult.FootprintSize.Y * CellSize.Y,
				ResolveSolveHeightInCells(RegionResult.SolveResult) * CellSize.Z);
			if (RegionSizeInBlocks.X <= 0 || RegionSizeInBlocks.Y <= 0 || RegionSizeInBlocks.Z <= 0)
			{
				continue;
			}
			const int32 RegionHeightInCells = ResolveSolveHeightInCells(RegionResult.SolveResult);
			const FIntVector RegionCenterOffset(
				(RegionResult.SolveResult.FootprintSize.X - 1) * CellSize.X / 2,
				(RegionResult.SolveResult.FootprintSize.Y - 1) * CellSize.Y / 2,
				(RegionHeightInCells - 1) * CellSize.Z / 2);
			const FVector Center = ChunkWorld.BlockWorldPosToUEWorldPos(Segment.PreviewGeometry.PathOriginBlockWorldPos
				+ RegionResult.RegionCellOffset * CellSize + RegionCenterOffset);
			const FVector Extent(static_cast<double>(RegionSizeInBlocks.X) * BlockSize * 0.5,
				static_cast<double>(RegionSizeInBlocks.Y) * BlockSize * 0.5,
				static_cast<double>(RegionSizeInBlocks.Z) * BlockSize * 0.5);
			DrawWireBox(PDI, FBox(Center - Extent, Center + Extent), FColor(180, 120, 255), SDPG_Foreground);
		}
	}

	if (SettingsObject->bDrawInterfaceBounds)
	{
		for (const FLayoutClosureCoverageSegmentRecord& ClosureSegment : SolveResult.ClosureSegments)
		{
			DrawWireSphere(PDI, ResolveCellCenter(ClosureSegment.Cell) + ResolveFaceDirectionVector(ClosureSegment.FaceDirection) * 6.0,
				ClosureSegment.bCovered ? FColor::Green : FColor::Red, 5.0f, 8, SDPG_Foreground);
		}
		for (const FLayoutPartitionSeamRecord& Seam : SolveResult.PartitionSeams)
		{
			const FVector OwnerStart = ResolveCellCenter(Seam.OwnerStartCell);
			const FVector OwnerEnd = ResolveCellCenter(Seam.OwnerEndCell);
			const FVector PassiveStart = ResolveCellCenter(Seam.PassiveStartCell);
			const FVector PassiveEnd = ResolveCellCenter(Seam.PassiveEndCell);
			PDI->DrawLine(OwnerStart, OwnerEnd, FColor(255, 100, 210), SDPG_Foreground, 2.0f);
			PDI->DrawLine(PassiveStart, PassiveEnd, FColor(120, 90, 255), SDPG_Foreground, 1.5f);
			DrawWireSphere(PDI, OwnerStart, FColor(255, 100, 210), 4.0f, 8, SDPG_Foreground);
			DrawWireSphere(PDI, PassiveStart, FColor(120, 90, 255), 4.0f, 8, SDPG_Foreground);
		}
	}
}

FLayoutDirectRootGenerationOverlaySummary FLayoutDirectRootGenerationController::BuildOverlaySummary() const
{
	FLayoutDirectRootGenerationOverlaySummary Summary;
	Summary.bHasHoveredBlock = bHasHoveredBlock && HoveredChunkWorld.IsValid();
	Summary.bDrawsHoverSquare = bToolActive && Summary.bHasHoveredBlock && SettingsObject->bDrawHoverSquare;
	const bool bContinuationPreview = bHasCachedAcceptedContinuationPreview;
	if (bContinuationPreview)
	{
		Summary.bHasSolvedPreview = !CachedContinuationSegments.IsEmpty()
			&& CachedSharedCellSizeInBlocks != FIntVector::ZeroValue;
		Summary.SolvedFootprintCount = SettingsObject->bDrawSolvedFootprint
			? CachedContinuationSegments.Num()
			: 0;
		for (const TArray<FLayoutCellDebugInfo>& SegmentDebugInfos : CachedContinuationCellDebugInfos)
		{
			Summary.CellZoneMarkerCount += SettingsObject->bDrawCellZoneMarkers ? SegmentDebugInfos.Num() : 0;
			if (SettingsObject->bDrawBoundaryFacePyramids)
			{
				for (const FLayoutCellDebugInfo& DebugInfo : SegmentDebugInfos)
				{
					Summary.BoundaryFacePyramidCount += DebugInfo.RegionBoundaryFaces.Num();
				}
			}
		}
		return Summary;
	}

	const FLayoutSolveResult* DiagnosticResult = GetCachedDiagnosticSolveResult();
	if (DiagnosticResult == nullptr) return Summary;
	const FLayoutSolveResult& OverlaySolveResult = *DiagnosticResult;
	const FLayoutRegionSolveScheduleResult& OverlayScheduleResult = CachedScheduleResult;
	Summary.bHasSolvedPreview = !CachedCellDebugInfos.IsEmpty()
		&& CachedSharedCellSizeInBlocks != FIntVector::ZeroValue;
	if (!Summary.bHasSolvedPreview)
	{
		return Summary;
	}

	Summary.SolvedFootprintCount = SettingsObject->bDrawSolvedFootprint ? 1 : 0;
	Summary.CellZoneMarkerCount = SettingsObject->bDrawCellZoneMarkers ? CachedCellDebugInfos.Num() : 0;

	if (SettingsObject->bDrawBoundaryFacePyramids)
	{
		for (const FLayoutCellDebugInfo& DebugInfo : CachedCellDebugInfos)
		{
			Summary.BoundaryFacePyramidCount += DebugInfo.RegionBoundaryFaces.Num();
		}
	}

	if (SettingsObject->bDrawUnoccupiedCellMarkers)
	{
		// Count debug-info cells whose position does not appear in the solved Placements array.
		TSet<FIntVector> OccupiedCells;
		for (const FLayoutPlacedModule& Placement : OverlaySolveResult.Placements)
		{
			for (const FIntVector& PlacementCell : BuildDirectRootPlacementCells(Placement))
			{
				OccupiedCells.Add(PlacementCell);
			}
		}
		for (const FLayoutRegionSolveResult& RegionResult : OverlayScheduleResult.RegionResults)
		{
			if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
			{
				continue;
			}

			for (const FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
			{
				for (const FIntVector& PlacementCell : BuildDirectRootPlacementCells(Placement))
				{
					OccupiedCells.Add(RegionResult.RegionCellOffset + PlacementCell);
				}
			}
		}
		for (const FLayoutCellDebugInfo& DebugInfo : CachedCellDebugInfos)
		{
			if (!OccupiedCells.Contains(DebugInfo.Cell))
			{
				++Summary.UnoccupiedCellCount;
			}
		}
	}

	if (SettingsObject->bDrawChildBounds)
	{
		for (const FLayoutRegionSolveResult& RegionResult : OverlayScheduleResult.RegionResults)
		{
			if (RegionResult.RegionCellOffset != FIntVector::ZeroValue)
			{
				++Summary.ChildBoundsCount;
			}
		}
	}

	if (SettingsObject->bDrawInterfaceBounds)
	{
		Summary.ClosureSegmentCount = OverlaySolveResult.ClosureSegments.Num();
		Summary.PartitionSeamCount = OverlaySolveResult.PartitionSeams.Num();
	}

	return Summary;
}

FIntVector FLayoutDirectRootGenerationController::ResolvePlacementSizeInBlocksForTesting(
	const FLayoutPlacedModule& Placement,
	const FIntVector& SharedCellSizeInBlocks) const
{
	return ResolvePlacementSizeInBlocks(Placement, SharedCellSizeInBlocks);
}

TArray<FIntVector> FLayoutDirectRootGenerationController::BuildPlacementCellsForTesting(const FLayoutPlacedModule& Placement) const
{
	return BuildDirectRootPlacementCells(Placement);
}

void FLayoutDirectRootGenerationController::SetHoveredLocationForTesting(
	AChunkWorldExtended* const ChunkWorld,
	const FIntVector& BlockWorldPos)
{
	HoveredChunkWorld = ChunkWorld;
	HoveredBlockWorldPos = BlockWorldPos;
	bHasHoveredBlock = ChunkWorld != nullptr;
	StateChanged.Broadcast();
}

AChunkWorldExtended* FLayoutDirectRootGenerationController::GetUsableHoveredChunkWorld() const
{
	return bHasHoveredBlock ? HoveredChunkWorld.Get() : nullptr;
}

void FLayoutDirectRootGenerationController::SetStatusMessage(FString&& Message)
{
	LastStatusMessage = MoveTemp(Message);
	StateChanged.Broadcast();
}

bool FLayoutDirectRootGenerationController::HasCachedPreviewAttempt() const
{
	return bHasCachedPreviewAttempt;
}

const FLayoutSolveResult* FLayoutDirectRootGenerationController::GetCachedDiagnosticSolveResult() const
{
	if (CachedSiteRecord.bLayoutSolved)
	{
		return &CachedSiteRecord.SolveResult;
	}

	if (bHasCachedPreviewAttempt)
	{
		return &CachedScheduleResult.MergedSolveResult;
	}

	return nullptr;
}

FString FLayoutDirectRootGenerationController::BuildGenerationSummaryText() const
{
	TArray<FString> Lines;

	// Drain pending async solve completions before reading cached results.
	if (AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get())
	{
		if (UChunkWorldLayoutRuntimeComponent* const RuntimeComponent = ChunkWorld->GetLayoutRuntimeComponent())
		{
			RuntimeComponent->PumpBackgroundLayoutSolves();
		}
	}

	Lines.Add(TEXT("Generation Attempt"));
	if (bHasSelectedContinuationStartEndpoint && !bContinuationPreviewInProgress)
	{
		Lines.Add(TEXT("  Mode: Continuation"));
		Lines.Add(TEXT("  Result: Select compatible target entry"));
		Lines.Add(FString::Printf(TEXT("  Source: site=%s cell=%s face=%d"),
			*SelectedContinuationStartEndpoint.SiteReservationKey.ToString(),
			*SelectedContinuationStartEndpoint.LocalCell.ToString(),
			static_cast<int32>(SelectedContinuationStartEndpoint.ExposedEntryFaceDirection)));
		Lines.Add(FString::Printf(TEXT("  Status: %s"), *LastStatusMessage));
		return FString::Join(Lines, TEXT("\n"));
	}
	if (bContinuationPreviewInProgress || bHasCachedAcceptedContinuationPreview)
	{
		Lines.Add(TEXT("  Mode: Continuation"));
		Lines.Add(FString::Printf(TEXT("  Result: %s"),
			bContinuationPreviewInProgress
				? TEXT("Solving...")
				: (bCachedContinuationPreviewIsPartial ? TEXT("Partial") : TEXT("Accepted"))));
		if (bHasCachedAcceptedContinuationPreview)
		{
			int32 PlacementCount = 0;
			for (const FLayoutGeneratorContinuationSegmentSolveResult& Segment : CachedContinuationSegments)
			{
				PlacementCount += Segment.ConnectorRecord.SolveResult.Placements.Num();
			}
			Lines.Add(FString::Printf(TEXT("  Segments: %d  Placements: %d"),
				CachedContinuationSegments.Num(), PlacementCount));
		}
		Lines.Add(FString::Printf(TEXT("  Status: %s"), *LastStatusMessage));
		return FString::Join(Lines, TEXT("\n"));
	}
	if (SettingsObject->LayoutWorldBinding != nullptr && SettingsObject->LayoutProfile != nullptr
		&& SettingsObject->LayoutWorldBinding->ContinuationFamilies.ContainsByPredicate(
			[this](const FLayoutWorldBindingContinuationFamily& Family)
			{
				return Family.Candidates.ContainsByPredicate([this](const FLayoutWorldBindingContinuationCandidate& Candidate)
				{
					return Candidate.LayoutProfile == SettingsObject->LayoutProfile;
				});
			}))
	{
		Lines.Add(TEXT("  Mode: Continuation"));
		Lines.Add(TEXT("  Result: Select compatible source entry"));
		Lines.Add(FString::Printf(TEXT("  Status: %s"), *LastStatusMessage));
		return FString::Join(Lines, TEXT("\n"));
	}
	if (!HasCachedPreviewAttempt())
	{
		Lines.Add(TEXT("  No explicit-root generation attempt has been run yet."));
		Lines.Add(TEXT("  Use Ctrl + Left Click on a hovered chunk-world surface to run one."));
		Lines.Add(FString::Printf(TEXT("  Status: %s"), *LastStatusMessage));
	}
	else
	{
		if (bPreviewSolveInProgress)
		{
			Lines.Add(TEXT("  Result: Solving..."));
			if (AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get())
			{
				if (UChunkWorldLayoutRuntimeComponent* const RT = ChunkWorld->GetLayoutRuntimeComponent())
				{
					const FLayoutBackgroundSolveDiagnosticsSnapshot Diag = RT->GetBackgroundSolveDiagnostics();
					Lines.Add(FString::Printf(TEXT("  Queue: Groups=%d Running=%d Waiting=%d Completed=%d"),
						Diag.InProgressLayoutGroups, Diag.Running, Diag.WaitingForDispatch, Diag.CompletedAwaitingPublish));
				}
			}
		}
		else
		{
		const FLayoutSolveResult* const DiagnosticSolveResult = GetCachedDiagnosticSolveResult();
		const bool bSolveAccepted = CachedScheduleResult.bSucceeded && CachedSiteRecord.bLayoutSolved;
		const bool bSucceeded = CachedSiteRecord.SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::None
			? bSolveAccepted
			: (bSolveAccepted && bHasCachedAcceptedPreview);
		Lines.Add(FString::Printf(
			TEXT("  Result: %s"),
			bSucceeded ? TEXT("Accepted") : TEXT("Rejected")));

		if (DiagnosticSolveResult != nullptr)
		{
			Lines.Add(FString::Printf(
				TEXT("  Footprint XY: %s"),
				*DiagnosticSolveResult->FootprintSize.ToString()));
			Lines.Add(FString::Printf(
				TEXT("  Planned cells (all levels): %d  Placement records (all levels): %d"),
				DiagnosticSolveResult->PlannedCells.Num(),
				DiagnosticSolveResult->Placements.Num()));
			Lines.Add(FString::Printf(
				TEXT("  Regions: %d  Backtracks: %d"),
				CachedScheduleResult.RegionResults.Num(),
				DiagnosticSolveResult->PropagationStats.BacktrackCount));

			const int32 WarningCount = Algo::CountIf(
				DiagnosticSolveResult->Messages,
				[](const FLayoutValidationMessage& Message)
				{
					return Message.Severity == ELayoutValidationSeverity::Warning;
				});
			const int32 ErrorCount = Algo::CountIf(
				DiagnosticSolveResult->Messages,
				[](const FLayoutValidationMessage& Message)
				{
					return Message.Severity == ELayoutValidationSeverity::Error;
				});
			Lines.Add(FString::Printf(
				TEXT("  Warnings: %d  Errors: %d"),
				WarningCount,
				ErrorCount));
			// Accepted flat recovery retains the rejected stepped reason as a warning.
			// Keep its text in both the details panel and copied generation summary.
			for (const FLayoutValidationMessage& Message : DiagnosticSolveResult->Messages)
			{
				if (Message.Severity == ELayoutValidationSeverity::Warning)
				{
					Lines.Add(FString::Printf(TEXT("  Warning: %s"),
						*Message.Message.Replace(TEXT("\n"), TEXT(" | "))));
				}
			}
			if (bSucceeded && ErrorCount > 0)
			{
				const FLayoutValidationMessage* const FirstError =
					DiagnosticSolveResult->Messages.FindByPredicate(
						[](const FLayoutValidationMessage& Message)
						{
							return Message.Severity == ELayoutValidationSeverity::Error;
						});
				if (FirstError != nullptr)
				{
					Lines.Add(FString::Printf(
						TEXT("  Accepted result retained error: %s"),
						*FirstError->Message.Replace(TEXT("\n"), TEXT(" | "))));
				}
			}
			Lines.Add(FString::Printf(
				TEXT("  Timing: total=%.2fms propagation=%.2fms"),
				LastPreviewSolveSeconds * 1000.0,
				DiagnosticSolveResult->PropagationStats.PropagationSeconds * 1000.0));
			Lines.Add(FString::Printf(
				TEXT("  Propagation: runs=%d passes=%d arcs=%d checks=%d removals=%d failed=%d"),
				DiagnosticSolveResult->PropagationStats.PropagationRunCount,
				DiagnosticSolveResult->PropagationStats.PropagationPassCount,
				DiagnosticSolveResult->PropagationStats.ArcQueuePopCount,
				DiagnosticSolveResult->PropagationStats.SupportCheckCount,
				DiagnosticSolveResult->PropagationStats.CandidateRemovalCount,
				DiagnosticSolveResult->PropagationStats.FailedCellCount));

			if (!bSucceeded)
			{
				const FString FailureText = !CachedPreviewFailureReason.IsEmpty()
					? CachedPreviewFailureReason
					: (!CachedScheduleResult.FailureReason.IsEmpty()
						? CachedScheduleResult.FailureReason
						: DiagnosticSolveResult->FailureReason);
				if (!FailureText.IsEmpty())
				{
					Lines.Add(FString::Printf(
						TEXT("  Failure: %s"),
						*FailureText.Replace(TEXT("\n"), TEXT(" | "))));
				}
			}
		}

		if (CachedSiteRecord.bHasBeenCommittedToChunkWorld)
		{
			Lines.Add(FString::Printf(
				TEXT("  Apply Result: committed to the chunk world in %.2fms."),
				FMath::Max(0.0, LastApplySeconds) * 1000.0));
		}
		else if (!LastApplyFailureReason.IsEmpty())
		{
			Lines.Add(FString::Printf(
				TEXT("  Apply Result: failed — %s"),
				*LastApplyFailureReason.Replace(TEXT("\n"), TEXT(" | "))));
		}
		else if (bSucceeded)
		{
			Lines.Add(TEXT("  Apply Result: preview cached and ready to apply."));
		}
		else if (!CachedPreviewFailureReason.IsEmpty())
		{
			const bool bAllowPartial = SettingsObject.IsValid() && SettingsObject->bAllowPartialPreviewApply;
			const bool bHasRetainedPartial =
				!CachedSiteRecord.SolveResult.Placements.IsEmpty()
				&& !CachedSiteRecord.SolvedArtifactId.IsNone()
				&& CachedSiteRecord.SolvedArtifactActiveCellCount > 0;
			const bool bHasPlannedCells =
				(CachedSiteRecord.SolveResult.FootprintSize.X > 0
					&& CachedSiteRecord.SolveResult.FootprintSize.Y > 0)
				|| !CachedSiteRecord.SolveResult.PlannedCells.IsEmpty();
			if (bAllowPartial && bHasRetainedPartial)
			{
				Lines.Add(FString::Printf(
					TEXT("  Apply Result: normal Apply blocked. Apply Invalid Preview can debug retained geometry (%d/%d placed), subject to write-safety guards. Solve remains rejected."),
					CachedSiteRecord.SolveResult.Placements.Num(),
					!CachedSiteRecord.SolveResult.PlannedCells.IsEmpty()
						? CachedSiteRecord.SolveResult.PlannedCells.Num()
						: CachedSiteRecord.SolveResult.FootprintSize.X * CachedSiteRecord.SolveResult.FootprintSize.Y));
			}
			else if (bAllowPartial && bHasPlannedCells)
			{
				Lines.Add(TEXT("  Apply Result: planned-cell preview — no placements, showing planned intent grid."));
			}
			else
			{
				Lines.Add(TEXT("  Apply Result: blocked until the preview rejection is resolved."));
			}
		}
	}
	}

	return FString::Join(Lines, TEXT("\n"));
}

FString FLayoutDirectRootGenerationController::BuildPersistentDiagnosticsText() const
{
	TArray<FString> Lines;

	Lines.Add(TEXT("Hover / Overlay State"));
	if (bHasHoveredBlock && HoveredChunkWorld.IsValid())
	{
		Lines.Add(FString::Printf(
			TEXT("  %s at block %s"),
			*GetNameSafe(HoveredChunkWorld.Get()),
			*HoveredBlockWorldPos.ToString()));
	}
	else
	{
		Lines.Add(TEXT("  No accepted chunk-world surface under the active editor cursor."));
	}

	Lines.Add(TEXT(""));
	Lines.Add(TEXT("Detailed Solve Diagnostics"));
	if (!HasCachedPreviewAttempt())
	{
		Lines.Add(TEXT("  No cached preview attempt."));
	}
	else
	{
		const FLayoutSolveResult* const DiagnosticSolveResult = GetCachedDiagnosticSolveResult();
		const bool bSolveAccepted = CachedScheduleResult.bSucceeded && CachedSiteRecord.bLayoutSolved;
		const bool bSucceeded = CachedSiteRecord.SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::None
			? bSolveAccepted
			: (bSolveAccepted && bHasCachedAcceptedPreview);
		Lines.Add(FString::Printf(
			TEXT("  %s at %s"),
			bSucceeded ? TEXT("Accepted") : TEXT("Rejected"),
			*CachedPreviewSiteCenterBlockWorldPos.ToString()));
		if (DiagnosticSolveResult != nullptr)
		{
			Lines.Add(FString::Printf(
				TEXT("  Footprint=%s Placements=%d Regions=%d Entries=%d Residual=%d Sparse=%d Backtracks=%d"),
				*DiagnosticSolveResult->FootprintSize.ToString(),
				DiagnosticSolveResult->Placements.Num(),
				CachedScheduleResult.RegionResults.Num(),
				DiagnosticSolveResult->ExportedEntryCells.Num(),
				DiagnosticSolveResult->ResidualUnoccupiedCells.Num(),
				DiagnosticSolveResult->SparsePlacementCommitments.Num(),
				DiagnosticSolveResult->PropagationStats.BacktrackCount));

			int32 CoveredClosureSegments = 0;
			int32 UncoveredClosureSegments = 0;
			for (const FLayoutClosureCoverageSegmentRecord& Segment : DiagnosticSolveResult->ClosureSegments)
			{
				Segment.bCovered ? ++CoveredClosureSegments : ++UncoveredClosureSegments;
			}

			int32 DroppedOptionalChildren = 0;
			for (const FLayoutRegionSolveResult& RegionResult : CachedScheduleResult.RegionResults)
			{
				if (RegionResult.bDroppedAsOptionalChild)
				{
					++DroppedOptionalChildren;
				}
			}

			Lines.Add(FString::Printf(
				TEXT("  Closure covered=%d uncovered=%d Seams=%d DroppedOptionalChildren=%d"),
				CoveredClosureSegments,
				UncoveredClosureSegments,
				DiagnosticSolveResult->PartitionSeams.Num(),
				DroppedOptionalChildren));
		}

		const FLayoutRecursiveVerticalAccessSummary& VerticalSummary = CachedScheduleResult.RecursiveVerticalAccessSummary;
		if (VerticalSummary.RequiredHostProviderCount > 0
			|| !VerticalSummary.CountedChildRegionDebugPaths.IsEmpty()
			|| !VerticalSummary.LocalOnlyChildRegionDebugPaths.IsEmpty()
			|| !VerticalSummary.UnusableContributingChildRegionDebugPaths.IsEmpty()
			|| !VerticalSummary.ExtraContributingChildRegionDebugPaths.IsEmpty()
			|| !VerticalSummary.FailureReason.IsEmpty())
		{
			Lines.Add(TEXT(""));
			Lines.Add(TEXT("Vertical Access Composition"));
			Lines.Add(FString::Printf(
				TEXT("  Required=%d ParentCount=%d CountedChildren=%s"),
				VerticalSummary.RequiredHostProviderCount,
				VerticalSummary.CountedParentProviderCount,
				VerticalSummary.CountedChildRegionDebugPaths.IsEmpty() ? TEXT("<none>") : *FString::Join(VerticalSummary.CountedChildRegionDebugPaths, TEXT(", "))));
			if (!VerticalSummary.LocalOnlyChildRegionDebugPaths.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("  LocalOnlyChildren=%s"), *FString::Join(VerticalSummary.LocalOnlyChildRegionDebugPaths, TEXT(", "))));
			}
			if (!VerticalSummary.UnusableContributingChildRegionDebugPaths.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("  UnusableContributingChildren=%s"), *FString::Join(VerticalSummary.UnusableContributingChildRegionDebugPaths, TEXT(", "))));
			}
			if (!VerticalSummary.ExtraContributingChildRegionDebugPaths.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("  ExtraContributingChildren=%s"), *FString::Join(VerticalSummary.ExtraContributingChildRegionDebugPaths, TEXT(", "))));
			}
			if (!VerticalSummary.FailureReason.IsEmpty())
			{
				Lines.Add(FString::Printf(TEXT("  Note: %s"), *VerticalSummary.FailureReason));
			}
		}

		if (DiagnosticSolveResult != nullptr)
		{
			int32 ErrorCount = 0;
			int32 WarningCount = 0;
			TArray<FString> HighlightLines;
			for (const FLayoutValidationMessage& Message : DiagnosticSolveResult->Messages)
			{
				switch (Message.Severity)
				{
				case ELayoutValidationSeverity::Warning:
					++WarningCount;
					break;
				case ELayoutValidationSeverity::Error:
				default:
					++ErrorCount;
					break;
				}

				if (HighlightLines.Num() < 6
					&& (Message.Severity == ELayoutValidationSeverity::Error || Message.Severity == ELayoutValidationSeverity::Warning))
				{
					const FString SingleLineMessage = Message.Message.Replace(TEXT("\n"), TEXT(" | "));
					HighlightLines.Add(FString::Printf(TEXT("  [%s] %s"), ToSeverityLabel(Message.Severity), *SingleLineMessage));
				}
			}

			if (ErrorCount > 0 || WarningCount > 0 || !CachedScheduleResult.FailureReason.IsEmpty())
			{
				Lines.Add(TEXT(""));
				Lines.Add(TEXT("Messages"));
				Lines.Add(FString::Printf(
					TEXT("  Errors=%d Warnings=%d"),
					ErrorCount,
					WarningCount));
				if (!CachedScheduleResult.FailureReason.IsEmpty())
				{
					Lines.Add(FString::Printf(TEXT("  Failure=%s"), *CachedScheduleResult.FailureReason.Replace(TEXT("\n"), TEXT(" | "))));
				}
				if (!CachedPreviewFailureReason.IsEmpty())
				{
					Lines.Add(FString::Printf(TEXT("  PreviewFailure=%s"), *CachedPreviewFailureReason.Replace(TEXT("\n"), TEXT(" | "))));
				}
				Lines.Append(HighlightLines);
			}
		}
	}

	if (!LastStatusMessage.IsEmpty())
	{
		Lines.Add(TEXT(""));
		Lines.Add(TEXT("Latest Action"));
		Lines.Add(FString::Printf(TEXT("  %s"), *LastStatusMessage.Replace(TEXT("\n"), TEXT(" | "))));
	}

	return FString::Join(Lines, TEXT("\n"));
}

void FLayoutDirectRootGenerationController::BuildCellDebugInfos()
{
	CachedCellDebugInfos.Reset();
	CachedTerrainPreviewRuns = CachedPreviewTerrainWritePositions.IsEmpty()
		? TArray<FTerrainPreviewRun>() : BuildTerrainPreviewRuns(CachedPreviewFrozenTerrainContract.TerrainWrites);

	if (CachedSharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return;
	}

	const FLayoutSolveResult& RootSolveResult = CachedSiteRecord.bLayoutSolved
		? CachedSiteRecord.SolveResult
		: CachedScheduleResult.MergedSolveResult;
	const FLayoutRegionSolveScheduleResult& PreviewScheduleResult = CachedScheduleResult;
	const FIntPoint RootFootprintSize = RootSolveResult.FootprintSize;
	if (RootFootprintSize.X <= 0 || RootFootprintSize.Y <= 0)
	{
		return;
	}

	// Compute the maximum planned Z level across all root cells so that ResolveRegionBoundaryFaces
	// can correctly classify PosZ as a boundary face on cells at the highest Z. This accounts for
	// bridge cells and TopBridge cells injected by the stepped-terrain adapter.
	int32 RootMaxPlannedZ = 0;
	for (const FLayoutPlannedCell& PlannedCell : RootSolveResult.PlannedCells)
	{
		RootMaxPlannedZ = FMath::Max(RootMaxPlannedZ, PlannedCell.Cell.Z);
	}

	// Build a root-local cell set so overlay base zones retain adjacent solved regions.
	TSet<FIntVector> AllPreviewCells;
	AllPreviewCells.Reserve(RootSolveResult.PlannedCells.Num() + PreviewScheduleResult.RegionResults.Num() * 4);
	for (const FLayoutPlannedCell& PlannedCell : RootSolveResult.PlannedCells)
	{
		AllPreviewCells.Add(PlannedCell.Cell);
	}
	for (const FLayoutRegionSolveResult& RegionResult : PreviewScheduleResult.RegionResults)
	{
		for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
		{
			AllPreviewCells.Add(RegionResult.RegionCellOffset + PlannedCell.Cell);
		}
	}

	const FIntVector NeighborDeltas[] = {
		{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
	};
	const ELayoutFaceDirection NeighborDirections[] = {
		ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY,
		ELayoutFaceDirection::PosZ, ELayoutFaceDirection::NegZ
	};

	for (const FLayoutPlannedCell& PlannedCell : RootSolveResult.PlannedCells)
	{
		FLayoutCellDebugInfo DebugInfo = BuildCellDebugInfo(
			PlannedCell,
			RootSolveResult.PlannedCells,
			AllPreviewCells);
		DebugInfo.RegionBoundaryFaces = ResolveRegionBoundaryFaces(PlannedCell.Cell, RootFootprintSize, RootMaxPlannedZ);
		RemoveInternalLateralBoundaryFaces(
			DebugInfo.RegionBoundaryFaces,
			PlannedCell.Cell,
			AllPreviewCells);

		for (int32 DirIdx = 0; DirIdx < 6; ++DirIdx)
		{
			if (!AllPreviewCells.Contains(PlannedCell.Cell + NeighborDeltas[DirIdx]))
			{
				DebugInfo.RegionBoundaryFaces.AddUnique(NeighborDirections[DirIdx]);
			}
		}

		DebugInfo.bIsPlannedEntryCell = PlannedCell.Intent == ELayoutCellIntent::Entry;
		if (DebugInfo.bIsPlannedEntryCell)
		{
			DebugInfo.EntryFaces = DebugInfo.RegionBoundaryFaces;
		}
		CachedCellDebugInfos.Add(MoveTemp(DebugInfo));
	}

	// Build debug-info entries for child-region planned cells, offsetting into root-local space.
	for (const FLayoutRegionSolveResult& RegionResult : PreviewScheduleResult.RegionResults)
	{
		if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
		{
			continue; // root region, already handled above
		}

		const FIntPoint ChildFootprintSize = RegionResult.SolveResult.FootprintSize;
		if (ChildFootprintSize.X <= 0 || ChildFootprintSize.Y <= 0)
		{
			continue;
		}

		// Compute the maximum planned Z level across all child cells so that
		// ResolveRegionBoundaryFaces can correctly classify PosZ as a boundary face
		// on cells at the highest Z, accounting for bridge cells and TopBridge cells.
		int32 ChildMaxPlannedZ = 0;
		for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
		{
			ChildMaxPlannedZ = FMath::Max(ChildMaxPlannedZ, PlannedCell.Cell.Z);
		}

		for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
		{
			FLayoutCellDebugInfo DebugInfo = BuildCellDebugInfo(
				PlannedCell,
				RegionResult.SolveResult.PlannedCells,
				AllPreviewCells,
				RegionResult.RegionCellOffset,
				&RegionResult.CommittedEndpointAnchors);
			DebugInfo.RegionBoundaryFaces = ResolveRegionBoundaryFaces(PlannedCell.Cell, ChildFootprintSize, ChildMaxPlannedZ);
			if (DebugInfo.bIsPlannedEntryCell && DebugInfo.EntryFaces.IsEmpty())
			{
				DebugInfo.EntryFaces = DebugInfo.RegionBoundaryFaces;
			}
			RemoveInternalLateralBoundaryFaces(
				DebugInfo.RegionBoundaryFaces,
				DebugInfo.Cell,
				AllPreviewCells);
			CachedCellDebugInfos.Add(MoveTemp(DebugInfo));
		}
	}
}

void FLayoutDirectRootGenerationController::BuildContinuationCellDebugInfos()
{
	CachedContinuationCellDebugInfos.Reset();
	CachedContinuationCellDebugInfos.SetNum(CachedContinuationSegments.Num());
	CachedContinuationTerrainPreviewRuns.Empty(CachedContinuationSegments.Num());
	for (const FLayoutGeneratorContinuationSegmentSolveResult& Segment : CachedContinuationSegments)
	{
		CachedContinuationTerrainPreviewRuns.Add(BuildTerrainPreviewRuns(Segment.FrozenTerrainContract.TerrainWrites));
	}
	const FIntVector NeighborDeltas[] = {
		{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}
	};
	const ELayoutFaceDirection NeighborDirections[] = {
		ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY,
		ELayoutFaceDirection::PosZ, ELayoutFaceDirection::NegZ
	};
	for (int32 SegmentIndex = 0; SegmentIndex < CachedContinuationSegments.Num(); ++SegmentIndex)
	{
		const FLayoutGeneratorContinuationSegmentSolveResult& Segment = CachedContinuationSegments[SegmentIndex];
		const FLayoutSolveResult& SolveResult = Segment.ConnectorRecord.SolveResult;
		const FLayoutRegionSolveScheduleResult& ScheduleResult = Segment.ScheduleResult;
		TArray<FLayoutCellDebugInfo>& DebugInfos = CachedContinuationCellDebugInfos[SegmentIndex];
		const FIntPoint PreviewFootprintSize = SolveResult.FootprintSize != FIntPoint::ZeroValue
			? SolveResult.FootprintSize
			: Segment.Descriptor.FootprintSize;
		if (PreviewFootprintSize.X <= 0 || PreviewFootprintSize.Y <= 0)
		{
			continue;
		}

		TArray<FLayoutPlannedCell> FinalizedPreviewCells;
		FinalizedPreviewCells.Reserve(Segment.PreviewGeometry.TerrainCells.Num());
		for (const FLayoutContinuationPreviewTerrainCell& TerrainCell : Segment.PreviewGeometry.TerrainCells)
		{
			FLayoutPlannedCell& PlannedCell = FinalizedPreviewCells.AddDefaulted_GetRef();
			PlannedCell.Cell = TerrainCell.LocalCell;
			PlannedCell.Intent = TerrainCell.Intent;
			PlannedCell.PlacementZone = TerrainCell.PlacementZone;
			PlannedCell.TerrainSeamFaceMask = TerrainCell.TerrainSeamFaceMask;
			PlannedCell.bIsBridgeCell = TerrainCell.bIsBridgeCell;
		}

		int32 MaxPlannedZ = 0;
		TSet<FIntVector> AllPreviewCells;
		for (const FLayoutPlannedCell& PlannedCell : FinalizedPreviewCells)
		{
			MaxPlannedZ = FMath::Max(MaxPlannedZ, PlannedCell.Cell.Z);
			AllPreviewCells.Add(PlannedCell.Cell);
		}
		for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
			{
				AllPreviewCells.Add(RegionResult.RegionCellOffset + PlannedCell.Cell);
			}
		}

		for (const FLayoutPlannedCell& PlannedCell : FinalizedPreviewCells)
		{
			FLayoutCellDebugInfo DebugInfo = BuildCellDebugInfo(
				PlannedCell, FinalizedPreviewCells, AllPreviewCells);
			DebugInfo.RegionBoundaryFaces = ResolveRegionBoundaryFaces(
				PlannedCell.Cell, PreviewFootprintSize, MaxPlannedZ);
			RemoveInternalLateralBoundaryFaces(
				DebugInfo.RegionBoundaryFaces, PlannedCell.Cell, AllPreviewCells);
			for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
			{
				if (!AllPreviewCells.Contains(PlannedCell.Cell + NeighborDeltas[DirectionIndex]))
				{
					DebugInfo.RegionBoundaryFaces.AddUnique(NeighborDirections[DirectionIndex]);
				}
			}
			DebugInfos.Add(MoveTemp(DebugInfo));
		}

		for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			if (RegionResult.RegionCellOffset == FIntVector::ZeroValue
				|| RegionResult.SolveResult.FootprintSize.X <= 0
				|| RegionResult.SolveResult.FootprintSize.Y <= 0)
			{
				continue;
			}
			int32 ChildMaxPlannedZ = 0;
			for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
			{
				ChildMaxPlannedZ = FMath::Max(ChildMaxPlannedZ, PlannedCell.Cell.Z);
			}
			for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
			{
				FLayoutCellDebugInfo DebugInfo = BuildCellDebugInfo(
					PlannedCell,
					RegionResult.SolveResult.PlannedCells,
					AllPreviewCells,
					RegionResult.RegionCellOffset);
				DebugInfo.RegionBoundaryFaces = ResolveRegionBoundaryFaces(
					PlannedCell.Cell, RegionResult.SolveResult.FootprintSize, ChildMaxPlannedZ);
				RemoveInternalLateralBoundaryFaces(
					DebugInfo.RegionBoundaryFaces, DebugInfo.Cell, AllPreviewCells);
				DebugInfos.Add(MoveTemp(DebugInfo));
			}
		}
	}
}

FIntVector FLayoutDirectRootGenerationController::ResolveCachedSharedCellSizeInBlocks() const
{
	if (CachedSiteRecord.SolveResult.SharedCellSizeInBlocks != FIntVector::ZeroValue)
	{
		return CachedSiteRecord.SolveResult.SharedCellSizeInBlocks;
	}

	if (CachedScheduleResult.MergedSolveResult.SharedCellSizeInBlocks != FIntVector::ZeroValue)
	{
		return CachedScheduleResult.MergedSolveResult.SharedCellSizeInBlocks;
	}

	return FIntVector::ZeroValue;
}

FIntVector FLayoutDirectRootGenerationController::ResolveCachedRootFootprintMinBlockWorldPos() const
{
	if (!CachedSiteRecord.bLayoutSolved)
	{
		// Rejected prewarm retains schedule diagnostics without a solved site payload.
		// Use the same result as debug cells, paired with the attempted (snapped) site.
		if (const FLayoutSolveResult* DiagnosticResult = GetCachedDiagnosticSolveResult())
		{
			FIntVector FootprintMin = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
				CachedPreviewSiteCenterBlockWorldPos, DiagnosticResult->FootprintSize,
				CachedSharedCellSizeInBlocks);
			if (!CachedSiteRecord.CachedFrozenTerrainBaseZByColumn.IsEmpty())
			{
				FootprintMin.Z = CachedPreviewTerrainAnchorBlockWorldPos.Z;
			}
			return FootprintMin;
		}
	}
	const FIntVector BaseFootprintMinBlockWorldPos =
		UChunkWorldLayoutRuntimeComponent::ComputeFootprintMinBlockWorldPos(
			CachedSiteRecord,
			CachedSharedCellSizeInBlocks);
	if (!PreviewTerrainFitOffsetsOverlayLayout(CachedSiteRecord, bHasCachedPreviewTerrainFit))
	{
		return BaseFootprintMinBlockWorldPos;
	}

	return CachedPreviewTerrainAnchorBlockWorldPos;
}

FIntVector FLayoutDirectRootGenerationController::ResolveCachedRootFootprintMinBlockWorldPosForTesting() const
{
	return ResolveCachedRootFootprintMinBlockWorldPos();
}

int32 FLayoutDirectRootGenerationController::ResolveSolveSeed(
	const AChunkWorldExtended& ChunkWorld,
	const FIntVector& SiteCenterBlockWorldPos) const
{
	if (!SettingsObject->bUseSiteDerivedSeed)
	{
		return SettingsObject->ManualSolveSeed;
	}

	return FLayoutSiteReservation::ComputeSiteSolveSeed(SiteCenterBlockWorldPos, ChunkWorld.Seed);
}

	int32 FLayoutDirectRootGenerationController::ResolveTerrainStageShiftBlocksForColumn(const FIntVector& LocalCell) const
	{
		if (const int32* BaseZ = CachedSiteRecord.CachedFrozenTerrainBaseZByColumn.Find(FIntPoint(LocalCell.X, LocalCell.Y)))
		{
			// ResolvedStageBaseBlockWorldZ is the absolute module-base Z for this column.
			// Return the offset from the footprint-min Z so the final position is BaseZ + Cell.Z * CellHeight.
			return *BaseZ - ResolveCachedRootFootprintMinBlockWorldPos().Z;
		}
		return 0;
	}

FVector FLayoutDirectRootGenerationController::ResolveCachedCellCenterWorld(const FIntVector& LocalCell) const
{
	const AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get();
	if (ChunkWorld == nullptr)
	{
		return FVector::ZeroVector;
	}

	const int32 TerrainShiftBlocks = ResolveTerrainStageShiftBlocksForColumn(LocalCell);
	const FLayoutSolveResult* DiagnosticResult = GetCachedDiagnosticSolveResult();
	const int32 TemplateOffset = DiagnosticResult != nullptr ? DiagnosticResult->TemplatePlacementZOffsetBlocks : 0;
	const FIntVector RootFootprintMinBlockWorldPos = ResolveCachedRootFootprintMinBlockWorldPos();
	return ChunkWorld->BlockWorldPosToUEWorldPos(
		FIntVector(
			RootFootprintMinBlockWorldPos.X + LocalCell.X * CachedSharedCellSizeInBlocks.X + CachedSharedCellSizeInBlocks.X / 2,
			RootFootprintMinBlockWorldPos.Y + LocalCell.Y * CachedSharedCellSizeInBlocks.Y + CachedSharedCellSizeInBlocks.Y / 2,
			RootFootprintMinBlockWorldPos.Z + TerrainShiftBlocks + LocalCell.Z * CachedSharedCellSizeInBlocks.Z + TemplateOffset + CachedSharedCellSizeInBlocks.Z / 2));
}

bool FLayoutDirectRootGenerationController::TryResolveRegionBoundsWorld(
	const FLayoutRegionSolveResult& RegionResult,
	FVector& OutCenter,
	FVector& OutExtent) const
{
	const AChunkWorldExtended* const ChunkWorld = CachedPreviewChunkWorld.Get();
	if (ChunkWorld == nullptr || CachedSharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return false;
	}

	const FIntVector RootFootprintMinBlockWorldPos = ResolveCachedRootFootprintMinBlockWorldPos();
	const FIntVector RegionMinBlockWorldPos(
		RootFootprintMinBlockWorldPos.X + RegionResult.RegionCellOffset.X * CachedSharedCellSizeInBlocks.X,
		RootFootprintMinBlockWorldPos.Y + RegionResult.RegionCellOffset.Y * CachedSharedCellSizeInBlocks.Y,
		RootFootprintMinBlockWorldPos.Z + RegionResult.RegionCellOffset.Z * CachedSharedCellSizeInBlocks.Z);
	const FIntVector RegionSizeInBlocks(
		RegionResult.SolveResult.FootprintSize.X * CachedSharedCellSizeInBlocks.X,
		RegionResult.SolveResult.FootprintSize.Y * CachedSharedCellSizeInBlocks.Y,
		ResolveSolveHeightInCells(RegionResult.SolveResult) * CachedSharedCellSizeInBlocks.Z);
	if (RegionSizeInBlocks.X <= 0 || RegionSizeInBlocks.Y <= 0 || RegionSizeInBlocks.Z <= 0)
	{
		return false;
	}

	OutCenter = ChunkWorld->BlockWorldPosToUEWorldPos(
		FIntVector(
			RegionMinBlockWorldPos.X + RegionSizeInBlocks.X / 2,
			RegionMinBlockWorldPos.Y + RegionSizeInBlocks.Y / 2,
			RegionMinBlockWorldPos.Z + RegionSizeInBlocks.Z / 2));
	OutExtent = FVector(
		static_cast<double>(RegionSizeInBlocks.X) * 0.5,
		static_cast<double>(RegionSizeInBlocks.Y) * 0.5,
		static_cast<double>(RegionSizeInBlocks.Z) * 0.5);
	return true;
}
