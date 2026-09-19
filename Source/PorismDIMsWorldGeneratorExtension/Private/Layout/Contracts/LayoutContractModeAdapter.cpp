// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Terrain/LayoutTerrainPlacementEvidence.h"

namespace LayoutContractStandardAdapter
{
	bool TryBuildStandardOutput(
		const FLayoutContractModeAdapterInput& Input,
		FLayoutAdapterOutput& OutOutput,
		FString& OutFailureReason);
}

namespace
{
	bool AreTerrainSeamsEnabled(const FLayoutRegionSolveRequest& SolveRequest)
	{
		return SolveRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve
			&& SolveRequest.ProfileSnapshot.bEnableTerrainSeams;
	}

	void ClearTerrainSeamContracts(TArray<FLayoutPlannedCell>& InOutPlannedCells)
	{
		for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			PlannedCell.TerrainSeamFaceMask = 0;
			PlannedCell.VerticalAccessLandingContactMask = 0;
			if (PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam)
			{
				PlannedCell.EntryOrigin = ELayoutEntryOrigin::None;
				if (PlannedCell.Intent == ELayoutCellIntent::Entry)
				{
					PlannedCell.Intent = ELayoutCellIntent::Interior;
				}
			}
		}
	}

	bool IsBlockXYInsideSearchBounds(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntPoint& BlockXY)
	{
		return BlockXY.X >= Artifact.SearchMinBlockXY.X
			&& BlockXY.X <= Artifact.SearchMaxBlockXY.X
			&& BlockXY.Y >= Artifact.SearchMinBlockXY.Y
			&& BlockXY.Y <= Artifact.SearchMaxBlockXY.Y;
	}

	FIntPoint ResolveEvidenceCellBlockXY(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntVector& Cell)
	{
		const int32 GridSpacing = FMath::Max(1, Artifact.TerrainSampleGridSpacing);
		return FIntPoint(
			Artifact.FootprintMinBlockWorldPos.X + Cell.X * GridSpacing,
			Artifact.FootprintMinBlockWorldPos.Y + Cell.Y * GridSpacing);
	}

	FString BuildOutOfBoundsEvidenceMessage(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const TCHAR* const EvidenceSource,
		const int32 SampleIndex,
		const int32 SampleCount,
		const FIntPoint& BlockXY)
	{
		return FString::Printf(
			TEXT("Terrain adapter rejected sampled terrain evidence outside frozen search bounds: source=%s sampleIndex=%d/%d sampleXY=%s artifact=%s modePlan=%s siteCenter=%s footprintMin=%s footprintSize=%s searchMin=%s searchMax=%s."),
			EvidenceSource,
			SampleIndex,
			SampleCount,
			*BlockXY.ToString(),
			*Artifact.ArtifactId.ToString(),
			*Artifact.ModePlanId.ToString(),
			*Artifact.SiteCenterBlockWorldPos.ToString(),
			*Artifact.FootprintMinBlockWorldPos.ToString(),
			*Artifact.FootprintSizeInBlocks.ToString(),
			*Artifact.SearchMinBlockXY.ToString(),
			*Artifact.SearchMaxBlockXY.ToString());
	}

	bool HasBaseTerrainEvidence(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		FString& OutFailureReason)
	{
		if (Artifact.EligibleBiomeRowName.IsNone())
		{
			OutFailureReason = TEXT("Terrain adapter requires a frozen matching biome row.");
			return false;
		}
		if (!Artifact.bHasFiniteSearchBounds
			|| Artifact.SearchDepthBlocks <= 0
			|| Artifact.FootprintSizeInBlocks.X <= 0
			|| Artifact.FootprintSizeInBlocks.Y <= 0
			|| Artifact.SearchMinBlockXY.X > Artifact.SearchMaxBlockXY.X
			|| Artifact.SearchMinBlockXY.Y > Artifact.SearchMaxBlockXY.Y)
		{
			OutFailureReason = FString::Printf(
				TEXT("Terrain adapter requires finite frozen search bounds: artifact=%s modePlan=%s siteCenter=%s footprintMin=%s footprintSize=%s searchMin=%s searchMax=%s searchDepth=%d."),
				*Artifact.ArtifactId.ToString(),
				*Artifact.ModePlanId.ToString(),
				*Artifact.SiteCenterBlockWorldPos.ToString(),
				*Artifact.FootprintMinBlockWorldPos.ToString(),
				*Artifact.FootprintSizeInBlocks.ToString(),
				*Artifact.SearchMinBlockXY.ToString(),
				*Artifact.SearchMaxBlockXY.ToString(),
				Artifact.SearchDepthBlocks);
			return false;
		}
		if (!Artifact.bHasSampledColumnEvidence || Artifact.SurfaceSamples.IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("Terrain adapter requires frozen sampled-column evidence: artifact=%s modePlan=%s siteCenter=%s footprintMin=%s footprintSize=%s searchMin=%s searchMax=%s surfaceSamples=%d."),
				*Artifact.ArtifactId.ToString(),
				*Artifact.ModePlanId.ToString(),
				*Artifact.SiteCenterBlockWorldPos.ToString(),
				*Artifact.FootprintMinBlockWorldPos.ToString(),
				*Artifact.FootprintSizeInBlocks.ToString(),
				*Artifact.SearchMinBlockXY.ToString(),
				*Artifact.SearchMaxBlockXY.ToString(),
				Artifact.SurfaceSamples.Num());
			return false;
		}
		for (int32 SurfaceSampleIndex = 0; SurfaceSampleIndex < Artifact.SurfaceSamples.Num(); ++SurfaceSampleIndex)
		{
			const FLayoutTerrainSurfaceSample& SurfaceSample = Artifact.SurfaceSamples[SurfaceSampleIndex];
			if (!SurfaceSample.bIsValid || !IsBlockXYInsideSearchBounds(Artifact, SurfaceSample.BlockXY))
			{
				OutFailureReason = BuildOutOfBoundsEvidenceMessage(
					Artifact,
					TEXT("surface"),
					SurfaceSampleIndex,
					Artifact.SurfaceSamples.Num(),
					SurfaceSample.BlockXY);
				return false;
			}
		}
		if (Artifact.bRequiresBiomeOwnership)
		{
			if (!Artifact.bHasBiomeOwnershipEvidence || Artifact.EligibleBiomeRowNames.IsEmpty() || Artifact.BiomeOwnershipSamples.IsEmpty())
			{
				OutFailureReason = TEXT("Terrain adapter requires frozen biome-ownership evidence for the allow-list.");
				return false;
			}
			for (int32 OwnershipSampleIndex = 0; OwnershipSampleIndex < Artifact.BiomeOwnershipSamples.Num(); ++OwnershipSampleIndex)
			{
				const FLayoutFrozenBiomeOwnershipSample& OwnershipSample = Artifact.BiomeOwnershipSamples[OwnershipSampleIndex];
				if (!OwnershipSample.bOwnedByAllowList
					|| !OwnershipSample.bHasSurfaceEvidence
					|| !IsBlockXYInsideSearchBounds(Artifact, OwnershipSample.BlockXY)
					|| (!OwnershipSample.OwningBiomeRowName.IsNone()
						&& !Artifact.EligibleBiomeRowNames.Contains(OwnershipSample.OwningBiomeRowName)))
				{
					OutFailureReason = !IsBlockXYInsideSearchBounds(Artifact, OwnershipSample.BlockXY)
						? BuildOutOfBoundsEvidenceMessage(
							Artifact,
							TEXT("biome_ownership"),
							OwnershipSampleIndex,
							Artifact.BiomeOwnershipSamples.Num(),
							OwnershipSample.BlockXY)
						: FString::Printf(
							TEXT("Terrain adapter rejected a sampled column without allow-list biome ownership evidence: source=biome_ownership sampleIndex=%d/%d sampleXY=%s artifact=%s modePlan=%s eligibleRows=%d owned=%d hasSurface=%d owningRow=%s."),
							OwnershipSampleIndex,
							Artifact.BiomeOwnershipSamples.Num(),
							*OwnershipSample.BlockXY.ToString(),
							*Artifact.ArtifactId.ToString(),
							*Artifact.ModePlanId.ToString(),
							Artifact.EligibleBiomeRowNames.Num(),
							OwnershipSample.bOwnedByAllowList ? 1 : 0,
							OwnershipSample.bHasSurfaceEvidence ? 1 : 0,
							*OwnershipSample.OwningBiomeRowName.ToString());
					return false;
				}
			}
		}
		return true;
	}

	bool ValidateTerrainEvidenceForMode(
		const ELayoutContractEnvironmentMode EnvironmentMode,
		const bool bUsesSteppedTopology,
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		FString& OutFailureReason)
	{
		const bool bSteppedWithSupportEvidence = bUsesSteppedTopology
			&& Artifact.bHasSteppedSupportEvidence
			&& !Artifact.SteppedSupportSamples.IsEmpty();
		const bool bUndergroundWithCavityEvidence =
			EnvironmentMode == ELayoutContractEnvironmentMode::UndergroundPocketPlacement
			&& Artifact.bHasFiniteSearchBounds
			&& Artifact.bHasPocketVoidIntervalEvidence
			&& !Artifact.PocketVoidIntervals.IsEmpty();
		const bool bHasBaseEvidenceToValidate =
			Artifact.bHasSampledColumnEvidence || !Artifact.SurfaceSamples.IsEmpty();
		if ((!bSteppedWithSupportEvidence || bHasBaseEvidenceToValidate)
			&& !bUndergroundWithCavityEvidence
			&& !HasBaseTerrainEvidence(Artifact, OutFailureReason))
		{
			return false;
		}
		if (bUsesSteppedTopology)
		{
			if (!Artifact.bHasSteppedSupportEvidence || Artifact.SteppedSupportSamples.IsEmpty())
			{
				OutFailureReason = TEXT("Shared stepped terrain topology requires frozen stepped-support evidence.");
				return false;
			}
			if (!Artifact.bHasFootprintClassificationEvidence || Artifact.FootprintClassification.CellClassifications.IsEmpty())
			{
				OutFailureReason = TEXT("Shared stepped terrain topology requires frozen footprint classification evidence.");
				return false;
			}
			for (int32 SupportSampleIndex = 0; SupportSampleIndex < Artifact.SteppedSupportSamples.Num(); ++SupportSampleIndex)
			{
				const FLayoutSteppedTerrainSupportSample& SupportSample = Artifact.SteppedSupportSamples[SupportSampleIndex];
				const FIntPoint BlockXY = ResolveEvidenceCellBlockXY(Artifact, SupportSample.LocalCell);
				if (!IsBlockXYInsideSearchBounds(Artifact, BlockXY))
				{
					OutFailureReason = BuildOutOfBoundsEvidenceMessage(
						Artifact,
						TEXT("stepped_support"),
						SupportSampleIndex,
						Artifact.SteppedSupportSamples.Num(),
						BlockXY);
					return false;
				}
			}
			for (int32 ClassificationIndex = 0; ClassificationIndex < Artifact.FootprintClassification.CellClassifications.Num(); ++ClassificationIndex)
			{
				const FLayoutTerrainCellClassification& Classification = Artifact.FootprintClassification.CellClassifications[ClassificationIndex];
				if (!IsBlockXYInsideSearchBounds(Artifact, Classification.BlockXY))
				{
					OutFailureReason = BuildOutOfBoundsEvidenceMessage(
						Artifact,
						TEXT("footprint_classification"),
						ClassificationIndex,
						Artifact.FootprintClassification.CellClassifications.Num(),
						Classification.BlockXY);
					return false;
				}
			}
		}

		switch (EnvironmentMode)
		{
		case ELayoutContractEnvironmentMode::SteppedSurfacePlacement:
			return true;
		case ELayoutContractEnvironmentMode::BridgeContinuation:
			if (!Artifact.bHasTerrainPathEvidence || Artifact.TerrainPathSamples.IsEmpty())
			{
				OutFailureReason = TEXT("Bridge terrain adapter requires frozen terrain-path evidence.");
				return false;
			}
			return true;
		case ELayoutContractEnvironmentMode::UndergroundPocketPlacement:
			// Flat Underground roots may clear frozen solid terrain, matching surface
			// placement. Actual void intervals remain optional diagnostic evidence.
			return true;
		case ELayoutContractEnvironmentMode::NonSteppedWorldPlacement:
			return true;
		default:
			return true;
		}
	}

	ELayoutFrozenTerrainCellContract ResolveTerrainCellContractFromPlacementEvidence(
		const FLayoutTerrainPlacementCellEvidence& PlacementEvidence)
	{
		if (PlacementEvidence.bHasBridgeSupportEvidence)
		{
			return ELayoutFrozenTerrainCellContract::BridgeSpan;
		}
		// Mixed ramp/foundation with clearance/excavation: use Active contract so that support
		// writes (foundation/ramp) can pass. Individual clearance/excavation evidence flags
		// carry local operation authority separately.
		if (PlacementEvidence.bHasExcavationEvidence || PlacementEvidence.bHasClearanceEvidence)
		{
			if (PlacementEvidence.bHasRampTransitionEvidence || PlacementEvidence.bHasFoundationFillEvidence)
			{
				return ELayoutFrozenTerrainCellContract::Active;
			}
			return ELayoutFrozenTerrainCellContract::FlatClearance;
		}
		return ELayoutFrozenTerrainCellContract::Active;
	}

	bool ValidateFrozenPlacementEvidenceForAdapter(
		const ELayoutContractEnvironmentMode EnvironmentMode,
		const bool bUsesSteppedTopology,
		const FLayoutRegionSolveRequest& SolveRequest,
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		FString& OutFailureReason)
	{
		if (!Artifact.bHasTerrainPlacementEvidence)
		{
			return true;
		}

		FLayoutProducedTerrainPlacementEvidenceArtifact ValidatedArtifact;
		if (!LayoutTerrainPlacementEvidence::TryBuildProducedArtifact(
				TEXT("Adapter.TerrainPlacementEvidence"),
				Artifact.TerrainPlacementCells,
				TEXT("ContractModeAdapter"),
				ValidatedArtifact,
				OutFailureReason))
		{
			return false;
		}

		TSet<FIntVector> PlannedCellIds;
		TSet<FIntPoint> PlannedColumns;
		TSet<FIntVector> ReservedOpenCellIds;
		for (const FLayoutPlannedCell& PlannedCell : SolveRequest.PlannedCells)
		{
			PlannedCellIds.Add(PlannedCell.Cell);
			PlannedColumns.Add(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
		}
		for (const FLayoutCellReservationRecord& Reservation : SolveRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations)
		{
			if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
			{
				ReservedOpenCellIds.Add(Reservation.Cell);
			}
		}

		// When PlannedCells are empty the downstream adapter will generate them
		// from placement evidence or footprint.  Skip the membership check so
		// placement evidence is not rejected before cell generation runs.
		const bool bHasPlannedCells = !SolveRequest.PlannedCells.IsEmpty();

		for (int32 PlacementEvidenceIndex = 0; PlacementEvidenceIndex < ValidatedArtifact.Cells.Num(); ++PlacementEvidenceIndex)
		{
			const FLayoutTerrainPlacementCellEvidence& PlacementEvidence = ValidatedArtifact.Cells[PlacementEvidenceIndex];
			const bool bHasMatchingPlannedAuthority = bUsesSteppedTopology
				? PlannedColumns.Contains(FIntPoint(PlacementEvidence.Cell.X, PlacementEvidence.Cell.Y))
				: PlannedCellIds.Contains(PlacementEvidence.Cell);
			const bool bHasReservedOpenAuthority = ReservedOpenCellIds.Contains(PlacementEvidence.Cell);
			if (bHasPlannedCells && !bHasMatchingPlannedAuthority && !bHasReservedOpenAuthority)
			{
				OutFailureReason = FString::Printf(
					TEXT("Terrain adapter rejected placement evidence for a cell outside the frozen planned-cell set: source=placement_evidence sampleIndex=%d/%d cell=%s artifact=%s modePlan=%s plannedCells=%d siteCenter=%s footprintMin=%s footprintSize=%s searchMin=%s searchMax=%s."),
					PlacementEvidenceIndex,
					ValidatedArtifact.Cells.Num(),
					*PlacementEvidence.Cell.ToString(),
					*Artifact.ArtifactId.ToString(),
					*Artifact.ModePlanId.ToString(),
					SolveRequest.PlannedCells.Num(),
					*Artifact.SiteCenterBlockWorldPos.ToString(),
					*Artifact.FootprintMinBlockWorldPos.ToString(),
					*Artifact.FootprintSizeInBlocks.ToString(),
					*Artifact.SearchMinBlockXY.ToString(),
					*Artifact.SearchMaxBlockXY.ToString());
				return false;
			}
			const FIntPoint BlockXY = ResolveEvidenceCellBlockXY(Artifact, PlacementEvidence.Cell);
			if (!IsBlockXYInsideSearchBounds(Artifact, BlockXY))
			{
				OutFailureReason = BuildOutOfBoundsEvidenceMessage(
					Artifact,
					TEXT("placement_evidence"),
					PlacementEvidenceIndex,
					ValidatedArtifact.Cells.Num(),
					BlockXY);
				return false;
			}
		}
		return true;
	}

	bool TryBuildActiveTerrainCellContractsFromEvidence(
		const ELayoutContractEnvironmentMode EnvironmentMode,
		const bool bUsesSteppedTopology,
		const bool bRequireExplicitEntryEvidence,
		const FLayoutRegionSolveRequest& SolveRequest,
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		TArray<FLayoutTerrainCellContractRecord>& OutCellContracts,
		FString& OutFailureReason)
	{
		OutCellContracts.Reset();
		if (!ValidateFrozenPlacementEvidenceForAdapter(EnvironmentMode, bUsesSteppedTopology, SolveRequest, Artifact, OutFailureReason))
		{
			return false;
		}
		TSet<FIntVector> SteppedSupportedCells;
		if (bUsesSteppedTopology)
		{
			for (const FLayoutSteppedTerrainSupportSample& SupportSample : Artifact.SteppedSupportSamples)
			{
				SteppedSupportedCells.Add(SupportSample.LocalCell);
			}
		}

		TMap<FIntVector, const FLayoutTerrainPlacementCellEvidence*> PlacementEvidenceByCell;
		TMap<FIntPoint, const FLayoutTerrainPlacementCellEvidence*> PlacementEvidenceByColumn;
		if (Artifact.bHasTerrainPlacementEvidence)
		{
			for (const FLayoutTerrainPlacementCellEvidence& PlacementEvidence : Artifact.TerrainPlacementCells)
			{
				PlacementEvidenceByCell.Add(PlacementEvidence.Cell, &PlacementEvidence);
				PlacementEvidenceByColumn.Add(FIntPoint(PlacementEvidence.Cell.X, PlacementEvidence.Cell.Y), &PlacementEvidence);
			}
		}

		TArray<FLayoutPlannedCell> GeneratedForContracts;
		const TArray<FLayoutPlannedCell>* EffectivePlannedCells = &SolveRequest.PlannedCells;
		if (SolveRequest.PlannedCells.IsEmpty())
		{
			if (!Artifact.TerrainPlacementCells.IsEmpty())
			{
				for (const FLayoutTerrainPlacementCellEvidence& Ev : Artifact.TerrainPlacementCells)
				{
					FLayoutPlannedCell C;
					C.Cell = Ev.Cell;
					C.Intent = ELayoutCellIntent::Interior;
					GeneratedForContracts.Add(C);
				}
				EffectivePlannedCells = &GeneratedForContracts;
			}
			else
			{
				const int32 LevelCount = SolveRequest.ProfileSnapshot.LevelCount;
				for (int32 Level = 0; Level < LevelCount; ++Level)
					for (int32 Y = 0; Y < SolveRequest.FootprintSize.Y; ++Y)
						for (int32 X = 0; X < SolveRequest.FootprintSize.X; ++X)
						{
							FLayoutPlannedCell C;
							C.Cell = FIntVector(X, Y, Level);
							C.Intent = ELayoutCellIntent::Interior;
							GeneratedForContracts.Add(C);
						}
				EffectivePlannedCells = &GeneratedForContracts;
			}
		}
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy = SolveRequest.SelectedModePlan.PlacementPolicy.TerrainTransition;
		TMap<FIntPoint, int32> LowestPlannedZByColumn;
		for (const FLayoutPlannedCell& PlannedCell : *EffectivePlannedCells)
		{
			int32& LowestZ = LowestPlannedZByColumn.FindOrAdd(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y), PlannedCell.Cell.Z);
			LowestZ = FMath::Min(LowestZ, PlannedCell.Cell.Z);
		}
		for (const FLayoutPlannedCell& PlannedCell : *EffectivePlannedCells)
		{
			if (bUsesSteppedTopology)
			{
				// Support samples are per XY column at Z=0. Accept planned cells at any Z
				// as long as the same column has a support sample at Z=0.
				const FIntVector ColumnSupportCell(PlannedCell.Cell.X, PlannedCell.Cell.Y, 0);
				if (!SteppedSupportedCells.Contains(PlannedCell.Cell)
					&& !SteppedSupportedCells.Contains(ColumnSupportCell))
				{
					OutFailureReason = TEXT("Stepped terrain adapter requires frozen support evidence for every planned cell before proof can continue.");
					return false;
				}
			}

			FLayoutTerrainCellContractRecord& CellContract = OutCellContracts.AddDefaulted_GetRef();
			CellContract.Cell = PlannedCell.Cell;
			CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
			const FLayoutTerrainPlacementCellEvidence* PlacementEvidence = PlacementEvidenceByCell.FindRef(PlannedCell.Cell);
			const FIntPoint PlannedColumn(PlannedCell.Cell.X, PlannedCell.Cell.Y);
			if (PlacementEvidence == nullptr
				&& bUsesSteppedTopology
				&& LowestPlannedZByColumn.FindRef(PlannedColumn) == PlannedCell.Cell.Z)
			{
				PlacementEvidence = PlacementEvidenceByColumn.FindRef(PlannedColumn);
			}
			if (PlacementEvidence != nullptr)
			{
				if (PlacementEvidence->bHasFoundationFillEvidence
					&& (!TerrainTransitionPolicy.bAllowFoundationFill || TerrainTransitionPolicy.MaxFoundationDepth <= 0))
				{
					OutFailureReason = TEXT("Terrain adapter rejected foundation-fill evidence without explicit foundation-fill policy.");
					return false;
				}
				if (PlacementEvidence->bHasFoundationFillEvidence
					&& PlacementEvidence->RequiredFoundationDepth <= 0)
				{
					OutFailureReason = FString::Printf(
						TEXT("Terrain adapter rejected non-positive required foundation depth %d at planned cell %s."),
						PlacementEvidence->RequiredFoundationDepth,
						*PlannedCell.Cell.ToString());
					return false;
				}
				if (PlacementEvidence->bHasRampTransitionEvidence
					&& (!TerrainTransitionPolicy.bAllowPerimeterRampTransition || TerrainTransitionPolicy.MaxFoundationDepth <= 0))
				{
					OutFailureReason = TEXT("Terrain adapter rejected ramp-transition evidence without enabled perimeter ramp policy and positive foundation-depth budget.");
					return false;
				}
				if (PlacementEvidence->bHasBridgeSupportEvidence
					&& EnvironmentMode != ELayoutContractEnvironmentMode::BridgeContinuation)
				{
					OutFailureReason = TEXT("Terrain adapter requires bridge-support evidence to be consumed only by bridge-continuation mode.");
					return false;
				}
				CellContract.Contract = ResolveTerrainCellContractFromPlacementEvidence(*PlacementEvidence);
				CellContract.bHasFoundationFillEvidence = PlacementEvidence->bHasFoundationFillEvidence;
				CellContract.RequiredFoundationDepth = PlacementEvidence->bHasFoundationFillEvidence
					? FMath::Min(PlacementEvidence->RequiredFoundationDepth, TerrainTransitionPolicy.MaxFoundationDepth)
					: 0;
				CellContract.FoundationMaterial = PlacementEvidence->FoundationMaterial;
				CellContract.bHasClearanceEvidence = PlacementEvidence->bHasClearanceEvidence;
				CellContract.bHasRampTransitionEvidence = PlacementEvidence->bHasRampTransitionEvidence;
				CellContract.bHasBridgeSupportEvidence = PlacementEvidence->bHasBridgeSupportEvidence;
				const bool bBoundaryCell = PlannedCell.Cell.X == 0
					|| PlannedCell.Cell.X == SolveRequest.FootprintSize.X - 1
					|| PlannedCell.Cell.Y == 0
					|| PlannedCell.Cell.Y == SolveRequest.FootprintSize.Y - 1;
				CellContract.bEntryWalkable =
					PlacementEvidence->EntryTraversability == ELayoutEntryTraversabilityVerdict::Walkable
					|| PlacementEvidence->EntryTraversability == ELayoutEntryTraversabilityVerdict::RampNeeded
					|| ((!bRequireExplicitEntryEvidence || !bBoundaryCell)
						&& PlacementEvidence->EntryTraversability == ELayoutEntryTraversabilityVerdict::None)
					|| (PlacementEvidence->EntryTraversability == ELayoutEntryTraversabilityVerdict::ExcavationNeeded
						&& PlacementEvidence->bHasExcavationEvidence
						&& PlacementEvidence->bHasLocalOverlapZ);
				if (PlacementEvidence->bHasExcavationEvidence && PlacementEvidence->bHasLocalOverlapZ)
				{
					CellContract.bHasExcavationOverlapZ = true;
					CellContract.ExcavationOverlapMinLocalZ = PlacementEvidence->OverlapMinLocalZ;
					CellContract.ExcavationOverlapMaxLocalZ = PlacementEvidence->OverlapMaxLocalZ;
				}
			}
		}
		return true;
	}

	bool TryBuildActiveCellsFromPlacementEvidence(
		const bool bUsesSteppedTopology,
		const FLayoutRegionSolveRequest& SolveRequest,
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		TArray<FLayoutContractActiveCellRecord>& OutActiveCells,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FString& OutFailureReason,
		const int32 CellHeight = 0,
		const int32 MinimumShiftClusterCells = 1,
		TArray<FLayoutFrozenTerrainStageCellRecord>* StageMap = nullptr,
		TArray<FLayoutAdapterDiagnostic>* Diagnostics = nullptr,
		ELayoutSteppedTerrainFinalizationFailureKind* OutFailureKind = nullptr,
		const bool bAlreadyMapped = false)
	{
		OutActiveCells.Reset();
		if (!AreTerrainSeamsEnabled(SolveRequest))
		{
			ClearTerrainSeamContracts(InOutPlannedCells);
		}
		for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
		{
			if (PlannedCell.ModuleLevelIndex == INDEX_NONE)
			{
				PlannedCell.ModuleLevelIndex = PlannedCell.Cell.Z;
			}
		}
		const bool bRequiresPlacementEvidence = bUsesSteppedTopology;
		if (!Artifact.bHasTerrainPlacementEvidence)
		{
			if (bRequiresPlacementEvidence)
			{
				OutFailureReason = TEXT("Stepped-surface terrain adapter requires placement evidence before producing active cells.");
				return false;
			}
			const TArray<FLayoutPlannedCell>& Cells = InOutPlannedCells.IsEmpty() ? SolveRequest.PlannedCells : InOutPlannedCells;
			OutActiveCells.Reserve(Cells.Num());
			for (const FLayoutPlannedCell& PlannedCell : Cells)
			{
				FLayoutContractActiveCellRecord& ActiveCell = OutActiveCells.AddDefaulted_GetRef();
				ActiveCell.Cell = PlannedCell.Cell;
			}
			return true;
		}

		TMap<FIntVector, const FLayoutTerrainPlacementCellEvidence*> PlacementEvidenceByCell;
		for (const FLayoutTerrainPlacementCellEvidence& PlacementEvidence : Artifact.TerrainPlacementCells)
		{
			PlacementEvidenceByCell.Add(PlacementEvidence.Cell, &PlacementEvidence);
		}

		const TArray<FLayoutPlannedCell>& EffectiveCells = InOutPlannedCells.IsEmpty() ? SolveRequest.PlannedCells : InOutPlannedCells;
		OutActiveCells.Reserve(EffectiveCells.Num());
		for (const FLayoutPlannedCell& PlannedCell : EffectiveCells)
		{
			const FLayoutTerrainPlacementCellEvidence* const PlacementEvidence = PlacementEvidenceByCell.FindRef(PlannedCell.Cell);
			// Placement evidence is per XY column at Z=0. Accept planned cells at any Z
			// if the same column has placement evidence at Z=0.
			const FIntVector ColumnEvidenceCell(PlannedCell.Cell.X, PlannedCell.Cell.Y, 0);
			const FLayoutTerrainPlacementCellEvidence* const ColumnEvidence =
				PlacementEvidence == nullptr ? PlacementEvidenceByCell.FindRef(ColumnEvidenceCell) : PlacementEvidence;
			if (ColumnEvidence == nullptr)
			{
				OutFailureReason = TEXT("Terrain adapter requires placement evidence for every planned cell before producing active cells.");
				return false;
			}
			if (!ColumnEvidence->bPlaceableForSelectedMode)
			{
				OutFailureReason = TEXT("Terrain adapter rejected a planned cell without selected-mode placement evidence.");
				return false;
			}

			FLayoutContractActiveCellRecord& ActiveCell = OutActiveCells.AddDefaulted_GetRef();
			ActiveCell.Cell = PlannedCell.Cell;
		}

		// Inject bridge planned cells where adjacent cells sit at different terrain stages.
		// Higher-stage columns shift Z+1 so Z=0 bridges align with unshifted neighbours.
		// Lower-stage columns get a TopBridge deck at the shared max Z level.
		// No Z=-1 cells — every level has at least two adjacent cells for CSP connectivity.
		if (bRequiresPlacementEvidence)
		{
			// Root and continuation stepped layouts share one bridge topology rule.
			// Diagonal support keeps shifted clusters connected through widened corridor
			// turns, so VerticalAccess planning sees same paired deck topology in both.
			const FIntVector NeighborDeltas[] = {
				FIntVector(-1, -1, 0), FIntVector(0, -1, 0), FIntVector(1, -1, 0),
				FIntVector(-1,  0, 0),                     FIntVector(1,  0, 0),
				FIntVector(-1,  1, 0), FIntVector(0,  1, 0), FIntVector(1,  1, 0)
			};
			const int32 NeighborDeltaCount = UE_ARRAY_COUNT(NeighborDeltas);
			const bool bContinuationTerrain =
				!SolveRequest.RootContinuationSelection.FamilyId.IsNone()
				|| SolveRequest.RootPlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
				|| (SolveRequest.bHasSelectedModePlan
					&& SolveRequest.SelectedModePlan.Scope == ELayoutContractRegionScope::Continuation);
			TSet<FIntPoint> ProtectedContinuationEndpointColumns;
			if (bContinuationTerrain)
			{
				for (const FLayoutCommittedEndpointAnchor& EndpointAnchor : SolveRequest.CommittedEndpointAnchors)
				{
					if (EndpointAnchor.RequiredWorldCenterBlockZ != INDEX_NONE)
					{
						ProtectedContinuationEndpointColumns.Add(
							FIntPoint(EndpointAnchor.LocalCell.X, EndpointAnchor.LocalCell.Y));
					}
				}
			}
			int32 ContinuationBridgeBaseLevel = 0;
			if (bContinuationTerrain)
			{
				ContinuationBridgeBaseLevel = MAX_int32;
				for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
				{
					if (!PlannedCell.bIsBridgeCell)
					{
						ContinuationBridgeBaseLevel = FMath::Min(
							ContinuationBridgeBaseLevel,
							PlannedCell.Cell.Z);
					}
				}
				ContinuationBridgeBaseLevel = ContinuationBridgeBaseLevel == MAX_int32
					? 0
					: ContinuationBridgeBaseLevel;
			}

			// Pass 1: detect stage transitions and lower columns that need TopBridge decks.
			// Seam columns are the only ones that get bridge cells at Z=0.
			TSet<FIntPoint> SeamHigherColumns;
			TMap<FIntPoint, TSet<FIntPoint>> LowerColumnsByHigherColumn;
			/** Cardinal contacts require a direct upper deck; diagonal contacts may only complete that deck. */
			TSet<FIntPoint> CardinalLowerColumnsNeedingTopBridge;
			TSet<FIntPoint> DiagonalOnlyLowerColumnsNeedingTopBridge;
			TSet<FIntPoint> ExcludedShiftColumns;

			for (const FLayoutPlannedCell& PlannedCell : EffectiveCells)
			{
				if (ProtectedContinuationEndpointColumns.Contains(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y)))
				{
					continue;
				}
				const FIntVector EvidenceCell = bAlreadyMapped
					? FIntVector(PlannedCell.Cell.X, PlannedCell.Cell.Y, 0) : PlannedCell.Cell;
				const FLayoutTerrainPlacementCellEvidence* const SourceEvidence = PlacementEvidenceByCell.FindRef(EvidenceCell);
				if (SourceEvidence == nullptr)
				{
					continue;
				}
				for (int32 NeighborIndex = 0; NeighborIndex < NeighborDeltaCount; ++NeighborIndex)
				{
					const FIntVector& NeighborDelta = NeighborDeltas[NeighborIndex];
					const FIntVector NeighborCell = EvidenceCell + NeighborDelta;
					const bool bCardinalContact = NeighborDelta.X == 0 || NeighborDelta.Y == 0;
					const FLayoutTerrainPlacementCellEvidence* const NeighborEvidence = PlacementEvidenceByCell.FindRef(NeighborCell);
					if (NeighborEvidence == nullptr)
					{
						continue;
					}
					const int32 StageDelta = NeighborEvidence->TerrainStageIndex - SourceEvidence->TerrainStageIndex;
					if (StageDelta == 0)
					{
						continue;
					}
					if (FMath::Abs(StageDelta) != 1)
					{
						// Diagonal contacts do not share an edge; cardinal contacts prove whether the terrace skips a stage.
						if (!bCardinalContact)
						{
							continue;
						}
						const FLayoutSteppedTerrainSupportSample* const SourceSupport =
							Artifact.SteppedSupportSamples.FindByPredicate([&PlannedCell](const FLayoutSteppedTerrainSupportSample& Sample)
							{
								return Sample.LocalCell.X == PlannedCell.Cell.X && Sample.LocalCell.Y == PlannedCell.Cell.Y;
							});
						const FLayoutSteppedTerrainSupportSample* const NeighborSupport =
							Artifact.SteppedSupportSamples.FindByPredicate([&NeighborCell](const FLayoutSteppedTerrainSupportSample& Sample)
							{
								return Sample.LocalCell.X == NeighborCell.X && Sample.LocalCell.Y == NeighborCell.Y;
							});
						if (OutFailureKind != nullptr)
						{
							*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
						}
						OutFailureReason = FString::Printf(
							TEXT("Stepped terrain adapter rejects non-adjacent terrain stages %d->%d between %s and %s before bridge-cell injection. Source(surface=%d, floor=%d, ceiling=%d, shift=%d); neighbor(surface=%d, floor=%d, ceiling=%d, shift=%d)."),
							SourceEvidence->TerrainStageIndex, NeighborEvidence->TerrainStageIndex,
							*PlannedCell.Cell.ToString(), *NeighborCell.ToString(),
							SourceSupport != nullptr ? SourceSupport->SupportSurfaceZ : INDEX_NONE,
							SourceSupport != nullptr ? SourceSupport->SnappedSupportFloorZ : INDEX_NONE,
							SourceSupport != nullptr ? SourceSupport->SnappedSupportCeilingZ : INDEX_NONE,
							SourceEvidence->VerticalShiftBlocks,
							NeighborSupport != nullptr ? NeighborSupport->SupportSurfaceZ : INDEX_NONE,
							NeighborSupport != nullptr ? NeighborSupport->SnappedSupportFloorZ : INDEX_NONE,
							NeighborSupport != nullptr ? NeighborSupport->SnappedSupportCeilingZ : INDEX_NONE,
							NeighborEvidence->VerticalShiftBlocks);
						if (Diagnostics != nullptr)
						{
							FLayoutAdapterDiagnostic& Diagnostic = Diagnostics->AddDefaulted_GetRef();
							Diagnostic.Cell = PlannedCell.Cell;
							Diagnostic.RelatedCell = NeighborCell;
							Diagnostic.bHasRelatedCell = true;
							Diagnostic.Detail = OutFailureReason;
						}
						return false;
					}

					const bool bSourceIsLower = SourceEvidence->TerrainStageIndex < NeighborEvidence->TerrainStageIndex;
					const FIntVector HigherCell = bSourceIsLower ? NeighborCell : PlannedCell.Cell;
					const FIntVector LowerCell = bSourceIsLower ? PlannedCell.Cell : NeighborCell;
					const FIntPoint HigherXY(HigherCell.X, HigherCell.Y);
					const FIntPoint LowerXY(LowerCell.X, LowerCell.Y);
					if (ProtectedContinuationEndpointColumns.Contains(HigherXY)
						|| ProtectedContinuationEndpointColumns.Contains(LowerXY))
					{
						continue;
					}
					SeamHigherColumns.Add(HigherXY);
					LowerColumnsByHigherColumn.FindOrAdd(HigherXY).Add(LowerXY);
				}
			}

			// Pass 2: normalize shifted stages to the site's lowest resolved lattice base.
			// Spatial shift and realization-base compensation must use the same absolute
			// offset so stage chains retain every intermediate local Z.

			// Validate minimum cluster size for shifted stages before bridge injection.
			// Each connected component (cluster) within a shifted stage must have at
			// least 3 planned cells. Clusters below this threshold are too small for
			// stable Z-shift + bridge topology — fall back to terrain excavation instead.
			if (!bAlreadyMapped && !SeamHigherColumns.IsEmpty())
			{
				// Build per-stage XY membership (from evidence) and total planned cell
				// count per XY column (across all Z levels).
				TMap<int32, TSet<FIntPoint>> CellsByStage;
				for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
				{
					const FIntPoint ColumnXY(PlannedCell.Cell.X, PlannedCell.Cell.Y);
					const FLayoutTerrainPlacementCellEvidence* const Ev = PlacementEvidenceByCell.FindRef(PlannedCell.Cell);
					if (Ev != nullptr)
					{
						CellsByStage.FindOrAdd(Ev->TerrainStageIndex).Add(ColumnXY);
					}
				}

				// Flood-fill helper: extract a connected component from a set of XY points
				// using 4-direction adjacency.
				auto ExtractCluster = [](TSet<FIntPoint>& Pool, const FIntPoint Seed) -> TSet<FIntPoint>
				{
					TSet<FIntPoint> Cluster;
					TArray<FIntPoint> Queue;
					Queue.Push(Seed);
					Cluster.Add(Seed);
					Pool.Remove(Seed);
					while (!Queue.IsEmpty())
					{
						const FIntPoint Cur = Queue.Pop();
						const FIntPoint Dirs[4] = { {1,0}, {-1,0}, {0,1}, {0,-1} };
						for (const FIntPoint& D : Dirs)
						{
							const FIntPoint N = Cur + D;
							if (Pool.Contains(N))
							{
								Queue.Push(N);
								Cluster.Add(N);
								Pool.Remove(N);
							}
						}
					}
					return Cluster;
				};

				// For each non-zero stage, reject undersized clusters and continuation
				// fringe that raises only ground-level Boundary cells.
				int32 BoundaryOnlyContinuationColumnCount = 0;
				auto ClusterHasRouteBearingAuthority =
					[&](const TSet<FIntPoint>& Cluster)
					{
						for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
						{
							const FIntPoint Column(PlannedCell.Cell.X, PlannedCell.Cell.Y);
							if (Cluster.Contains(Column)
								&& (PlannedCell.Intent != ELayoutCellIntent::Boundary
									|| PlannedCell.ModuleLevelIndex > 0))
							{
								return true;
							}
						}
						for (const FLayoutCommittedTraversalAnchor& Anchor : SolveRequest.CommittedTraversalAnchors)
						{
							if (Cluster.Contains(FIntPoint(Anchor.Cell.X, Anchor.Cell.Y)))
							{
								return true;
							}
						}
						for (const FLayoutCommittedEndpointAnchor& Anchor : SolveRequest.CommittedEndpointAnchors)
						{
							if (!Anchor.TraversalChannels.IsEmpty()
								&& Cluster.Contains(FIntPoint(Anchor.LocalCell.X, Anchor.LocalCell.Y)))
							{
								return true;
							}
						}
						for (const FLayoutRouteConstraintRecord& Constraint : SolveRequest.RequiredRouteConstraints)
						{
							if (Cluster.Contains(FIntPoint(Constraint.Cell.X, Constraint.Cell.Y)))
							{
								return true;
							}
						}
						return false;
					};
				auto ClusterHasExcavationAuthority =
					[&](const TSet<FIntPoint>& Cluster)
					{
						for (const FIntPoint& Column : Cluster)
						{
							const FLayoutTerrainPlacementCellEvidence* const Evidence =
								PlacementEvidenceByCell.FindRef(FIntVector(Column.X, Column.Y, 0));
							if (Evidence == nullptr
								|| !Evidence->bHasExcavationEvidence
								|| !Evidence->bHasLocalOverlapZ)
							{
								return false;
							}
						}
						return true;
					};
				for (auto& StageEntry : CellsByStage)
				{
					if (StageEntry.Key == 0) continue; // stage 0 is never shifted
					TSet<FIntPoint>& Pool = StageEntry.Value;
					while (!Pool.IsEmpty())
					{
						const FIntPoint Seed = *Pool.CreateConstIterator();
						const TSet<FIntPoint> Cluster = ExtractCluster(Pool, Seed);
						const bool bBoundaryOnlyContinuationFringe =
							bContinuationTerrain
							&& !ClusterHasRouteBearingAuthority(Cluster)
							&& ClusterHasExcavationAuthority(Cluster);
						if (Cluster.Num() < MinimumShiftClusterCells || bBoundaryOnlyContinuationFringe)
						{
							ExcludedShiftColumns.Append(Cluster);
							if (bBoundaryOnlyContinuationFringe)
							{
								BoundaryOnlyContinuationColumnCount += Cluster.Num();
							}
						}
					}
				}

				// Remove excluded clusters from SeamHigherColumns.
				if (!ExcludedShiftColumns.IsEmpty())
				{
					// Local project fix: continuation width sampling may include a raised
					// side shell that must not force a stair through the route centerline.
					UE_LOG(LogTemp, Display,
						TEXT("Stepped adapter: excluding %d columns from bridge shift (minimumCluster=%d, boundaryOnlyContinuation=%d); using terrain excavation."),
						ExcludedShiftColumns.Num(),
						MinimumShiftClusterCells,
						BoundaryOnlyContinuationColumnCount);
					if (Diagnostics != nullptr && BoundaryOnlyContinuationColumnCount > 0)
					{
						FLayoutAdapterDiagnostic& Diagnostic = Diagnostics->AddDefaulted_GetRef();
						Diagnostic.Detail = FString::Printf(
							TEXT("Excluded %d boundary-only continuation fringe columns from stepped bridge topology; terrain excavation preserves the authored route."),
							BoundaryOnlyContinuationColumnCount);
					}
					for (const FIntPoint& XY : ExcludedShiftColumns)
					{
						SeamHigherColumns.Remove(XY);
						LowerColumnsByHigherColumn.Remove(XY);
					}
				}
			}

			// Cluster exclusion changes authoritative seam columns. Bottom bridges retain
			// diagonal support, but only cardinal stage contacts require upper deck cells.
			CardinalLowerColumnsNeedingTopBridge.Reset();
			DiagonalOnlyLowerColumnsNeedingTopBridge.Reset();
			for (const FIntPoint& SeamXY : SeamHigherColumns)
			{
				const FLayoutTerrainPlacementCellEvidence* const SeamEvidence =
					PlacementEvidenceByCell.FindRef(FIntVector(SeamXY.X, SeamXY.Y, 0));
				if (SeamEvidence == nullptr)
				{
					continue;
				}
				for (int32 NeighborIndex = 0; NeighborIndex < NeighborDeltaCount; ++NeighborIndex)
				{
					const FIntVector& NeighborDelta = NeighborDeltas[NeighborIndex];
					const FIntPoint NeighborXY(SeamXY.X + NeighborDelta.X, SeamXY.Y + NeighborDelta.Y);
					const FLayoutTerrainPlacementCellEvidence* const NeighborEvidence =
						PlacementEvidenceByCell.FindRef(FIntVector(NeighborXY.X, NeighborXY.Y, 0));
					if (NeighborEvidence == nullptr
						|| NeighborEvidence->TerrainStageIndex >= SeamEvidence->TerrainStageIndex)
					{
						continue;
					}

					const bool bCardinalContact = NeighborDelta.X == 0 || NeighborDelta.Y == 0;
					if (bCardinalContact)
					{
						CardinalLowerColumnsNeedingTopBridge.Add(NeighborXY);
						DiagonalOnlyLowerColumnsNeedingTopBridge.Remove(NeighborXY);
					}
					else if (!CardinalLowerColumnsNeedingTopBridge.Contains(NeighborXY))
					{
						DiagonalOnlyLowerColumnsNeedingTopBridge.Add(NeighborXY);
					}
				}
			}

			TArray<FLayoutPlannedCell> NewBridgeCells;
			if (CellHeight > 0 && StageMap != nullptr)
			{
				// Build lookup: XY -> its current StageMap entry
				TMap<FIntPoint, FLayoutFrozenTerrainStageCellRecord*> StageByXY;
				for (FLayoutFrozenTerrainStageCellRecord& R : *StageMap)
				{
					StageByXY.Add(R.FootprintCellXY, &R);
				}

				// Normalize every shifted stage onto one spatial lattice. Using only
				// the immediate lower neighbor collapses stage 2+ onto spatial Z=1.
				int32 LowestResolvedStageBaseZ = MAX_int32;
				for (const FLayoutFrozenTerrainStageCellRecord& Record : *StageMap)
				{
					LowestResolvedStageBaseZ = FMath::Min(LowestResolvedStageBaseZ, Record.ResolvedStageBaseBlockWorldZ);
				}

				TMap<int32, int32> ShiftCountByStage;
				for (const FIntPoint& SeamXY : SeamHigherColumns)
				{
					const FLayoutFrozenTerrainStageCellRecord* const SeamRecord = StageByXY.FindRef(SeamXY);
					const FLayoutTerrainPlacementCellEvidence* const SeamEvidence =
						PlacementEvidenceByCell.FindRef(FIntVector(SeamXY.X, SeamXY.Y, 0));
					if (SeamRecord == nullptr || SeamEvidence == nullptr || LowestResolvedStageBaseZ == MAX_int32)
					{
						continue;
					}

					const int32 StageOffsetBlocks = SeamRecord->ResolvedStageBaseBlockWorldZ - LowestResolvedStageBaseZ;
					if (StageOffsetBlocks <= 0)
					{
						continue;
					}
					if (StageOffsetBlocks % CellHeight != 0)
					{
						OutFailureReason = FString::Printf(
							TEXT("Stepped terrain stage %d has non-lattice base offset %d for cell (%d,%d)."),
							SeamEvidence->TerrainStageIndex,
							StageOffsetBlocks,
							SeamXY.X,
							SeamXY.Y);
						return false;
					}

					const int32 ShiftCount = StageOffsetBlocks / CellHeight;
					if (const int32* ExistingShiftCount = ShiftCountByStage.Find(SeamEvidence->TerrainStageIndex);
						ExistingShiftCount != nullptr && *ExistingShiftCount != ShiftCount)
					{
						OutFailureReason = FString::Printf(
							TEXT("Stepped terrain stage %d resolves to inconsistent spatial shifts %d and %d."),
							SeamEvidence->TerrainStageIndex,
							*ExistingShiftCount,
							ShiftCount);
						return false;
					}
					ShiftCountByStage.Add(SeamEvidence->TerrainStageIndex, ShiftCount);
				}

				// Shift every planned and active cell in a raised stage. Bridge topology
				// is generated below from this shared finalized stage map for root and
				// continuation layouts alike.
				for (const auto& StageShiftPair : ShiftCountByStage)
				{
					const int32 StageIndex = StageShiftPair.Key;
					const int32 ShiftCount = StageShiftPair.Value;

					for (FLayoutFrozenTerrainStageCellRecord& R : *StageMap)
					{
						const FLayoutTerrainPlacementCellEvidence* const Ev =
							PlacementEvidenceByCell.FindRef(FIntVector(R.FootprintCellXY.X, R.FootprintCellXY.Y, 0));
						if (Ev == nullptr
							|| Ev->TerrainStageIndex != StageIndex
							|| ExcludedShiftColumns.Contains(R.FootprintCellXY))
						{
							continue;
						}

						// Child geometry already carries the inherited physical shift.
						if (bAlreadyMapped) continue;
						R.ResolvedStageBaseBlockWorldZ -= ShiftCount * CellHeight;

						for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
						{
							if (PlannedCell.Cell.X == R.FootprintCellXY.X
								&& PlannedCell.Cell.Y == R.FootprintCellXY.Y)
							{
								PlannedCell.Cell.Z += ShiftCount;
							}
						}
						for (FLayoutContractActiveCellRecord& ActiveCell : OutActiveCells)
						{
							if (ActiveCell.Cell.X == R.FootprintCellXY.X
								&& ActiveCell.Cell.Y == R.FootprintCellXY.Y)
							{
								ActiveCell.Cell.Z += ShiftCount;
							}
						}
					}
				}

				// Add one bridge on each lower seam plane for every higher seam column.
				TSet<FIntVector> PlannedCellSet;
				TMap<FIntVector, int32> AuthoredModuleLevelByCell;
				PlannedCellSet.Reserve(InOutPlannedCells.Num());
				for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
				{
					PlannedCellSet.Add(PlannedCell.Cell);
					AuthoredModuleLevelByCell.Add(PlannedCell.Cell, PlannedCell.ModuleLevelIndex);
				}

				TSet<FIntVector> BottomBridgeCells;
				TMap<FIntVector, int32> BottomBridgeModuleLevels;
				for (const TPair<FIntPoint, TSet<FIntPoint>>& SeamPair : LowerColumnsByHigherColumn)
				{
					const FIntPoint& HigherXY = SeamPair.Key;
					if (!SeamHigherColumns.Contains(HigherXY))
					{
						continue;
					}

					const FLayoutTerrainPlacementCellEvidence* const HigherEvidence =
						PlacementEvidenceByCell.FindRef(FIntVector(HigherXY.X, HigherXY.Y, 0));
					if (HigherEvidence == nullptr)
					{
						continue;
					}

					const int32* HigherShiftCount = ShiftCountByStage.Find(HigherEvidence->TerrainStageIndex);
					if (HigherShiftCount == nullptr || *HigherShiftCount <= 0)
					{
						continue;
					}

					for (const FIntPoint& LowerXY : SeamPair.Value)
					{
						const FLayoutTerrainPlacementCellEvidence* const LowerEvidence =
							PlacementEvidenceByCell.FindRef(FIntVector(LowerXY.X, LowerXY.Y, 0));
						if (LowerEvidence == nullptr)
						{
							continue;
						}

						const int32 LowerShiftCount = ShiftCountByStage.FindRef(LowerEvidence->TerrainStageIndex);
						if (LowerShiftCount >= *HigherShiftCount)
						{
							continue;
						}

						// A bridge belongs on its lower seam plane, not every plane below
						// the higher column. Mixed stage clusters can therefore expose two
						// distinct bridge cells without filling a foreign bottom surface.
						const FIntVector BridgeCell(
							HigherXY.X,
							HigherXY.Y,
							ContinuationBridgeBaseLevel + LowerShiftCount);
						// Extend the lower source plane's authored level. Physical Z includes
						// terrain shifts and must not turn a ground support into top-level content.
						const FIntVector SourceCell(LowerXY.X, LowerXY.Y, BridgeCell.Z);
						const int32* SourceModuleLevel = AuthoredModuleLevelByCell.Find(SourceCell);
						const int32* ExistingModuleLevel = BottomBridgeModuleLevels.Find(BridgeCell);
						if (SourceModuleLevel == nullptr || *SourceModuleLevel == INDEX_NONE
							|| (ExistingModuleLevel != nullptr && *ExistingModuleLevel != *SourceModuleLevel))
						{
							if (OutFailureKind != nullptr)
							{
								*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
							}
							OutFailureReason = FString::Printf(
								TEXT("Bottom bridge %s has missing or conflicting authored-level authority from source %s."),
								*BridgeCell.ToString(), *SourceCell.ToString());
							return false;
						}
						BottomBridgeCells.Add(BridgeCell);
						BottomBridgeModuleLevels.Add(BridgeCell, *SourceModuleLevel);

					}
				}

				TSet<FIntVector> CompleteBridgeTopology = PlannedCellSet;
				CompleteBridgeTopology.Append(BottomBridgeCells);
				TArray<FIntVector> SortedBottomBridgeCells = BottomBridgeCells.Array();
				SortedBottomBridgeCells.Sort([](const FIntVector& Left, const FIntVector& Right)
				{
					if (Left.Z != Right.Z) return Left.Z < Right.Z;
					if (Left.Y != Right.Y) return Left.Y < Right.Y;
					return Left.X < Right.X;
				});
				for (const FIntVector& BridgeCell : SortedBottomBridgeCells)
				{
					uint8 LateralExteriorFaceMask = 0;
					for (const ELayoutFaceDirection Direction : {
						ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
						ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
					{
						const FIntVector NeighborCell = BridgeCell + FLayoutDirectionUtils::ToCellDelta(Direction);
						if (!CompleteBridgeTopology.Contains(NeighborCell))
						{
							LateralExteriorFaceMask |= LayoutFaceDirectionMask(Direction);
						}
					}

					FLayoutPlannedCell& Bridge = NewBridgeCells.AddDefaulted_GetRef();
					Bridge.Cell = BridgeCell;
					// Only the authored footprint shell owns Boundary intent. A lateral
					// gap toward a shifted stage affects retaining geometry, not intent.
					const bool bOnFootprintBoundary =
						BridgeCell.X == 0
						|| BridgeCell.X == SolveRequest.FootprintSize.X - 1
						|| BridgeCell.Y == 0
						|| BridgeCell.Y == SolveRequest.FootprintSize.Y - 1;
					Bridge.Intent = bOnFootprintBoundary
						? ELayoutCellIntent::Boundary
						: ELayoutCellIntent::Interior;
					Bridge.ModuleLevelIndex = BottomBridgeModuleLevels.FindChecked(BridgeCell);
					Bridge.bIsBridgeCell = true;
					Bridge.TerrainSeamFaceMask = 0;
					Bridge.PlacementZone = ResolveLayoutPlacementZoneFromLateralFaceMask(
						LateralExteriorFaceMask);

					FLayoutContractActiveCellRecord& ActiveBridgeCell = OutActiveCells.AddDefaulted_GetRef();
					ActiveBridgeCell.Cell = BridgeCell;
				}
			}

			// Pass 3: capture each column's current authored top after spatial shifting.
			TSet<FIntVector> ExistingPlannedCells;
			TMap<FIntVector, int32> PlannedCellIndexByCell;
			TMap<FIntPoint, int32> TopSpatialLevelByXY;
			TMap<FIntPoint, int32> TopModuleLevelByXY;
			for (int32 PlannedCellIndex = 0; PlannedCellIndex < InOutPlannedCells.Num(); ++PlannedCellIndex)
			{
				const FLayoutPlannedCell& PlannedCell = InOutPlannedCells[PlannedCellIndex];
				ExistingPlannedCells.Add(PlannedCell.Cell);
				PlannedCellIndexByCell.Add(PlannedCell.Cell, PlannedCellIndex);
				const FIntPoint CellXY(PlannedCell.Cell.X, PlannedCell.Cell.Y);
				int32& TopSpatialLevel = TopSpatialLevelByXY.FindOrAdd(CellXY, MIN_int32);
				TopSpatialLevel = FMath::Max(TopSpatialLevel, PlannedCell.Cell.Z);
				int32& TopModuleLevel = TopModuleLevelByXY.FindOrAdd(CellXY, MIN_int32);
				TopModuleLevel = FMath::Max(TopModuleLevel, PlannedCell.ModuleLevelIndex);
			}

			// Pass 4: cardinal stage contacts require upper deck cells. Diagonal
			// contacts may add support only when they complete a stable deck corner.
			TSet<FIntVector> TopBridgeCells;
			TMap<FIntVector, int32> TopBridgeModuleLevels;
			auto TryBuildTopBridgeCandidate = [&ProtectedContinuationEndpointColumns, &TopSpatialLevelByXY, &TopModuleLevelByXY, &ExistingPlannedCells, &SolveRequest](
				const FIntPoint& LowerXY,
				FIntVector& OutTopBridgeCell,
				int32& OutLandingModuleLevel)
			{
				if (ProtectedContinuationEndpointColumns.Contains(LowerXY))
				{
					return false;
				}
				const int32* TopSpatialLevel = TopSpatialLevelByXY.Find(LowerXY);
				const int32* TopModuleLevel = TopModuleLevelByXY.Find(LowerXY);
				if (TopSpatialLevel == nullptr || TopModuleLevel == nullptr)
				{
					return false;
				}

				OutTopBridgeCell = FIntVector(LowerXY.X, LowerXY.Y, *TopSpatialLevel + 1);
				if (ExistingPlannedCells.Contains(OutTopBridgeCell))
				{
					return false;
				}
				// A single authored level needs a virtual above-ground landing level.
				// Otherwise GroundOnly floor content cannot pair with upward stairs.
				OutLandingModuleLevel = SolveRequest.ProfileSnapshot.LevelCount == 1
					? *TopModuleLevel + 1
					: *TopModuleLevel;
				return true;
			};
			auto CountLateralDeckNeighbors = [](const TSet<FIntVector>& DeckCells, const FIntVector& Cell)
			{
				int32 Count = 0;
				for (const FIntVector& LateralDelta : {
					FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
					FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
				{
					Count += DeckCells.Contains(Cell + LateralDelta) ? 1 : 0;
				}
				return Count;
			};

			TArray<FIntPoint> SortedCardinalLowerColumns = CardinalLowerColumnsNeedingTopBridge.Array();
			SortedCardinalLowerColumns.Sort([](const FIntPoint& Left, const FIntPoint& Right)
			{
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			for (const FIntPoint& LowerXY : SortedCardinalLowerColumns)
			{
				FIntVector TopBridgeCell;
				int32 LandingModuleLevel = INDEX_NONE;
				if (TryBuildTopBridgeCandidate(LowerXY, TopBridgeCell, LandingModuleLevel))
				{
					TopBridgeCells.Add(TopBridgeCell);
					TopBridgeModuleLevels.Add(TopBridgeCell, LandingModuleLevel);
				}
			}

			TSet<FIntVector> TopDeckCells = ExistingPlannedCells;
			TopDeckCells.Append(TopBridgeCells);
			TArray<FIntPoint> SortedDiagonalLowerColumns = DiagonalOnlyLowerColumnsNeedingTopBridge.Array();
			SortedDiagonalLowerColumns.Sort([](const FIntPoint& Left, const FIntPoint& Right)
			{
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			bool bAddedDiagonalDeckSupport = false;
			do
			{
				bAddedDiagonalDeckSupport = false;
				TArray<FIntVector> PendingTopBridgeCells;
				TArray<int32> PendingModuleLevels;
				for (const FIntPoint& LowerXY : SortedDiagonalLowerColumns)
				{
					FIntVector TopBridgeCell;
					int32 LandingModuleLevel = INDEX_NONE;
					if (TryBuildTopBridgeCandidate(LowerXY, TopBridgeCell, LandingModuleLevel)
						&& !TopBridgeCells.Contains(TopBridgeCell)
						&& CountLateralDeckNeighbors(TopDeckCells, TopBridgeCell) >= 2)
					{
						PendingTopBridgeCells.Add(TopBridgeCell);
						PendingModuleLevels.Add(LandingModuleLevel);
					}
				}
				for (int32 CandidateIndex = 0; CandidateIndex < PendingTopBridgeCells.Num(); ++CandidateIndex)
				{
					TopBridgeCells.Add(PendingTopBridgeCells[CandidateIndex]);
					TopBridgeModuleLevels.Add(PendingTopBridgeCells[CandidateIndex], PendingModuleLevels[CandidateIndex]);
					TopDeckCells.Add(PendingTopBridgeCells[CandidateIndex]);
					bAddedDiagonalDeckSupport = true;
				}
			}
			while (bAddedDiagonalDeckSupport);

			// Generated terminal deck cells have no stable bridge topology. Peel
			// leaves to a fixed point instead of rejecting the whole plan; stable
			// two-neighbor decks remain shared by root and continuation solves.
			bool bRemovedTerminalTopBridge = false;
			do
			{
				bRemovedTerminalTopBridge = false;
				TArray<FIntVector> TerminalTopBridgeCells;
				for (const FIntVector& TopBridgeCell : TopBridgeCells)
				{
					if (CountLateralDeckNeighbors(TopDeckCells, TopBridgeCell) < 2)
					{
						TerminalTopBridgeCells.Add(TopBridgeCell);
					}
				}
				TerminalTopBridgeCells.Sort([](const FIntVector& Left, const FIntVector& Right)
				{
					if (Left.Z != Right.Z) return Left.Z < Right.Z;
					if (Left.Y != Right.Y) return Left.Y < Right.Y;
					return Left.X < Right.X;
				});
				for (const FIntVector& TopBridgeCell : TerminalTopBridgeCells)
				{
					TopBridgeCells.Remove(TopBridgeCell);
					TopBridgeModuleLevels.Remove(TopBridgeCell);
					TopDeckCells.Remove(TopBridgeCell);
					bRemovedTerminalTopBridge = true;
				}
			}
			while (bRemovedTerminalTopBridge);

			// Both root and continuation planning may already contain direct upper
			// bridge landings. Project seams from every final bridge landing instead
			// of only this adapter's newly generated TopBridge cells.
			TSet<FIntVector> SeamProjectionBridgeCells = TopBridgeCells;
			for (const FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
			{
				if (PlannedCell.bIsBridgeCell
					&& ExistingPlannedCells.Contains(PlannedCell.Cell - FIntVector(0, 0, 1)))
				{
					SeamProjectionBridgeCells.Add(PlannedCell.Cell);
				}
			}

			TMap<FIntVector, uint8> LandingContactMasks;
			// Project every transition edge of a direct upper deck landing onto its
			// authored lower top cell only when this profile enables terrain seams.
			// A seam only exists where that edge meets a same-level lower surface,
			// never at ordinary outer perimeter exposure.
			if (AreTerrainSeamsEnabled(SolveRequest))
			{
			for (const FIntVector& TopBridgeCell : SeamProjectionBridgeCells)
			{
				const FIntVector LowerTopCell(
					TopBridgeCell.X,
					TopBridgeCell.Y,
					TopBridgeCell.Z - 1);
				const int32* LowerTopCellIndex = PlannedCellIndexByCell.Find(LowerTopCell);
				if (LowerTopCellIndex == nullptr)
				{
					continue;
				}

				for (const ELayoutFaceDirection Direction : {
					ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY })
				{
					const FIntVector TopDeckNeighbor =
						TopBridgeCell + FLayoutDirectionUtils::ToCellDelta(Direction);
					const FIntVector LowerSurfaceNeighbor =
						LowerTopCell + FLayoutDirectionUtils::ToCellDelta(Direction);
					if (!TopDeckCells.Contains(TopDeckNeighbor)
						&& ExistingPlannedCells.Contains(LowerSurfaceNeighbor))
					{
						const uint8 ContactMask = LayoutFaceDirectionMask(Direction);
						InOutPlannedCells[*LowerTopCellIndex].TerrainSeamFaceMask |= ContactMask;
						LandingContactMasks.FindOrAdd(TopBridgeCell) |= ContactMask;
					}
				}
			}

			for (FLayoutPlannedCell& PlannedCell : InOutPlannedCells)
			{
				if (PlannedCell.bIsBridgeCell)
				{
					PlannedCell.VerticalAccessLandingContactMask |= LandingContactMasks.FindRef(PlannedCell.Cell);
				}
			}
			}

			TArray<FIntVector> SortedTopBridgeCells = TopBridgeCells.Array();
			SortedTopBridgeCells.Sort([](const FIntVector& Left, const FIntVector& Right)
			{
				if (Left.Z != Right.Z) return Left.Z < Right.Z;
				if (Left.Y != Right.Y) return Left.Y < Right.Y;
				return Left.X < Right.X;
			});
			for (const FIntVector& TopBridgeCell : SortedTopBridgeCells)
			{
				int32 LateralExteriorFaces = 0;
				for (const FIntVector& LateralDelta : { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0) })
				{
					if (!TopDeckCells.Contains(TopBridgeCell + LateralDelta))
					{
						++LateralExteriorFaces;
					}
				}

				ExistingPlannedCells.Add(TopBridgeCell);
				FLayoutPlannedCell& Bridge = NewBridgeCells.AddDefaulted_GetRef();
				Bridge.Cell = TopBridgeCell;
				Bridge.ModuleLevelIndex = TopBridgeModuleLevels.FindRef(TopBridgeCell);
				Bridge.Intent = LateralExteriorFaces > 0
					? ELayoutCellIntent::Boundary
					: ELayoutCellIntent::Interior;
				Bridge.bIsBridgeCell = true;
				Bridge.VerticalAccessLandingContactMask = LandingContactMasks.FindRef(TopBridgeCell);
				// A potential upper deck must not force a solid cell above an authored
				// open-sky cap. Explicit retaining-seam landings remain mandatory.
				Bridge.bIsTopBridgeOffer = Bridge.VerticalAccessLandingContactMask == 0;
				Bridge.PlacementZone = LateralExteriorFaces >= 2
					? ELayoutPlacementZone::Corner
					: LateralExteriorFaces >= 1
						? ELayoutPlacementZone::Edge
						: ELayoutPlacementZone::Interior;

				FLayoutContractActiveCellRecord& ActiveCell = OutActiveCells.AddDefaulted_GetRef();
				ActiveCell.Cell = TopBridgeCell;
			}

			InOutPlannedCells.Append(NewBridgeCells);
		}
		return true;
	}

	/** Compiles occupancy-only retained-terrain proof for generated bottom bridge support cells. */
	bool TryBuildTerrainBackedNeighborFaceRecords(
		const TArray<FLayoutPlannedCell>& FinalPlannedCells,
		const TArray<FLayoutFrozenTerrainStageCellRecord>& StageMap,
		const TArray<FLayoutSteppedTerrainSupportSample>& NeighborHaloSamples,
		const int32 CellHeight,
		TArray<FLayoutTerrainBackedNeighborFaceRecord>& OutRecords,
		FString& OutFailureReason)
	{
		OutRecords.Reset();
		if (FinalPlannedCells.IsEmpty() || StageMap.IsEmpty() || CellHeight <= 0)
		{
			return true;
		}

		TSet<FIntVector> PlannedCells;
		int32 LowestSpatialZ = MAX_int32;
		for (const FLayoutPlannedCell& PlannedCell : FinalPlannedCells)
		{
			PlannedCells.Add(PlannedCell.Cell);
			LowestSpatialZ = FMath::Min(LowestSpatialZ, PlannedCell.Cell.Z);
		}

		TMap<FIntPoint, int32> StageIndexByColumn;
		TMap<FIntPoint, FLayoutId> SourceEvidenceByColumn;
		int32 BaseSupportFloorZ = MAX_int32;
		for (const FLayoutFrozenTerrainStageCellRecord& Stage : StageMap)
		{
			if (StageIndexByColumn.Contains(Stage.FootprintCellXY))
			{
				OutFailureReason = TEXT("Stepped terrain adapter rejected duplicate stage-map columns while compiling terrain-backed occupancy.");
				return false;
			}
			StageIndexByColumn.Add(Stage.FootprintCellXY, Stage.TerrainStageIndex);
			SourceEvidenceByColumn.Add(Stage.FootprintCellXY, Stage.SourceEvidenceId);
			BaseSupportFloorZ = FMath::Min(BaseSupportFloorZ, Stage.SnappedSupportFloorZ);
		}
		for (const FLayoutSteppedTerrainSupportSample& HaloSample : NeighborHaloSamples)
		{
			const FIntPoint HaloXY(HaloSample.LocalCell.X, HaloSample.LocalCell.Y);
			if (StageIndexByColumn.Contains(HaloXY) || BaseSupportFloorZ == MAX_int32)
			{
				continue;
			}
			const int32 HeightAboveBase = HaloSample.SnappedSupportFloorZ - BaseSupportFloorZ;
			if (HeightAboveBase < 0 || HeightAboveBase % CellHeight != 0)
			{
				continue;
			}
			StageIndexByColumn.Add(HaloXY, HeightAboveBase / CellHeight);
			SourceEvidenceByColumn.Add(HaloXY, FLayoutId(*FString::Printf(
				TEXT("SteppedTerrainHalo.%d.%d"), HaloSample.LocalCell.X, HaloSample.LocalCell.Y)));
		}

		const ELayoutFaceDirection HorizontalDirections[] = {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY};
		TMap<FIntVector, uint8> SeenFaceMasksByCell;
		for (const FLayoutPlannedCell& PlannedCell : FinalPlannedCells)
		{
			// Bottom bridge supports have a shifted layout cell directly above. Top
			// bridge decks and ordinary authored cells retain normal module/open-space semantics.
			if (!PlannedCell.bIsBridgeCell
				|| !PlannedCells.Contains(PlannedCell.Cell + FIntVector(0, 0, 1)))
			{
				continue;
			}

			const int32 RelativeSpatialZ = PlannedCell.Cell.Z - LowestSpatialZ;
			for (const ELayoutFaceDirection Direction : HorizontalDirections)
			{
				const FIntVector NeighborCell = PlannedCell.Cell + FLayoutDirectionUtils::ToCellDelta(Direction);
				if (PlannedCells.Contains(NeighborCell))
				{
					continue;
				}

				const FIntPoint NeighborXY(NeighborCell.X, NeighborCell.Y);
				const int32* const NeighborStageIndex = StageIndexByColumn.Find(NeighborXY);
				if (NeighborStageIndex == nullptr || *NeighborStageIndex <= RelativeSpatialZ)
				{
					continue;
				}

				const uint8 FaceMask = LayoutFaceDirectionMask(Direction);
				uint8& SeenFaceMask = SeenFaceMasksByCell.FindOrAdd(PlannedCell.Cell);
				if ((SeenFaceMask & FaceMask) != 0)
				{
					OutFailureReason = TEXT("Stepped terrain adapter rejected duplicate terrain-backed neighbor face proof.");
					return false;
				}
				SeenFaceMask |= FaceMask;

				const FLayoutId SourceEvidenceId = SourceEvidenceByColumn.FindRef(NeighborXY);
				if (SourceEvidenceId.IsNone())
				{
					OutFailureReason = TEXT("Stepped terrain adapter rejected terrain-backed occupancy without source evidence provenance.");
					return false;
				}
				FLayoutTerrainBackedNeighborFaceRecord& Record = OutRecords.AddDefaulted_GetRef();
				Record.Cell = PlannedCell.Cell;
				Record.FaceDirection = Direction;
				Record.NeighborCell = NeighborCell;
				Record.SourceEvidenceId = SourceEvidenceId;
			}
		}

		OutRecords.Sort([](const FLayoutTerrainBackedNeighborFaceRecord& Left, const FLayoutTerrainBackedNeighborFaceRecord& Right)
		{
			if (Left.Cell.Z != Right.Cell.Z) return Left.Cell.Z < Right.Cell.Z;
			if (Left.Cell.Y != Right.Cell.Y) return Left.Cell.Y < Right.Cell.Y;
			if (Left.Cell.X != Right.Cell.X) return Left.Cell.X < Right.Cell.X;
			return static_cast<uint8>(Left.FaceDirection) < static_cast<uint8>(Right.FaceDirection);
		});
		return true;
	}
}

bool FLayoutContractModeAdapter::TryCompleteMappedSteppedTopology(
	const FLayoutRegionSolveRequest& ChildRequest,
	TArray<FLayoutPlannedCell>& InOutPlannedCells,
	FString& OutFailureReason)
{
	// Geometry-only column evidence for the shared topology routine. These records never
	// replace frozen world terrain evidence or grant terrain-write authority.
	TMap<FIntPoint, int32> GroundByColumn;
	for (const FLayoutPlannedCell& Cell : InOutPlannedCells)
	{
		if (Cell.bIsBridgeCell) continue;
		const int32 Level = Cell.ModuleLevelIndex == INDEX_NONE ? Cell.Cell.Z : Cell.ModuleLevelIndex;
		const FIntPoint XY(Cell.Cell.X, Cell.Cell.Y);
		const int32 Ground = Cell.Cell.Z - Level;
		if (const int32* Existing = GroundByColumn.Find(XY); Existing != nullptr && *Existing != Ground)
		{
			OutFailureReason = TEXT("Mapped child column has inconsistent authored-to-physical level offsets.");
			return false;
		}
		GroundByColumn.Add(XY, Ground);
	}
	if (GroundByColumn.IsEmpty())
	{
		OutFailureReason = TEXT("Mapped child topology has no authored columns.");
		return false;
	}
	int32 MinGround = MAX_int32;
	for (const auto& Column : GroundByColumn) MinGround = FMath::Min(MinGround, Column.Value);
	FLayoutFrozenTerrainBiomeAdapterInput Geometry;
	Geometry.bHasTerrainPlacementEvidence = true;
	TArray<FLayoutFrozenTerrainStageCellRecord> GeometryStages;
	for (const auto& Column : GroundByColumn)
	{
		auto& Evidence = Geometry.TerrainPlacementCells.AddDefaulted_GetRef();
		Evidence.Cell = FIntVector(Column.Key.X, Column.Key.Y, 0);
		Evidence.bPlaceableForSelectedMode = true;
		Evidence.TerrainStageIndex = Column.Value - MinGround;
		Evidence.VerticalShiftBlocks = Column.Value - MinGround;
		auto& Stage = GeometryStages.AddDefaulted_GetRef();
		Stage.FootprintCellXY = Column.Key;
		Stage.TerrainStageIndex = Evidence.TerrainStageIndex;
		Stage.ResolvedStageBaseBlockWorldZ = Column.Value;
	}
	TArray<FLayoutContractActiveCellRecord> ActiveCells;
	return TryBuildActiveCellsFromPlacementEvidence(true, ChildRequest, Geometry,
		ActiveCells, InOutPlannedCells, OutFailureReason, 1, 1, &GeometryStages, nullptr, nullptr, true);
}

bool FLayoutContractModeAdapter::TryPrepareRegionOutput(
	const FLayoutContractModeAdapterInput& Input,
	FLayoutAdapterOutput& OutOutput,
	FString& OutFailureReason)
{
	if (Input.SolveRequest == nullptr || Input.SolveRequest->PrecomputedPlannedCells.IsEmpty())
		return TryRunAdapter(Input, OutOutput, OutFailureReason);
	const auto& Request = *Input.SolveRequest;
	OutFailureReason.Reset();
	OutOutput = FLayoutAdapterOutput();
	OutOutput.bSucceeded = true;
	OutOutput.ModePlan = Input.ModePlan;
	OutOutput.PlannedCells = Request.PrecomputedPlannedCells;
	OutOutput.bHasFinalizedSteppedTerrainIntents = Request.bHasFinalizedSteppedTerrainIntents;
	OutOutput.VerticalAccessHostGroups = Request.VerticalAccessHostGroups;
	OutOutput.ActiveCells = Request.PrecomputedActiveCells;
	OutOutput.SteppedTerrainSupportMap = Request.PrecomputedSteppedTerrainSupportMap;
	OutOutput.FrozenTerrainContract = Request.PrecomputedFrozenTerrainContract;
	return true;
}

bool FLayoutContractModeAdapter::TryRunAdapter(
	const FLayoutContractModeAdapterInput& Input,
	FLayoutAdapterOutput& OutOutput,
	FString& OutFailureReason,
	ELayoutSteppedTerrainFinalizationFailureKind* const OutFailureKind)
{
	OutOutput = FLayoutAdapterOutput();
	OutFailureReason.Reset();
	if (OutFailureKind != nullptr)
	{
		*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::None;
	}
	if (Input.SolveRequest == nullptr)
	{
		OutFailureReason = TEXT("Layout contract adapter requires a finalized solve request.");
		return false;
	}

	switch (Input.ModePlan.EnvironmentMode)
	{
	case ELayoutContractEnvironmentMode::StandardRegion:
	case ELayoutContractEnvironmentMode::ChildRegion:
		return LayoutContractStandardAdapter::TryBuildStandardOutput(Input, OutOutput, OutFailureReason);
	case ELayoutContractEnvironmentMode::NonSteppedWorldPlacement:
	case ELayoutContractEnvironmentMode::SteppedSurfacePlacement:
	case ELayoutContractEnvironmentMode::BridgeContinuation:
	case ELayoutContractEnvironmentMode::TunnelContinuation:
	case ELayoutContractEnvironmentMode::UndergroundPocketPlacement:
	{
		if (!Input.SolveRequest->bHasFrozenTerrainBiomeAdapterInput)
		{
			OutFailureReason = FString::Printf(
				TEXT("Layout contract adapter mode %d requires a frozen terrain/biome artifact before worker execution can honor it."),
				static_cast<int32>(Input.ModePlan.EnvironmentMode));
			return false;
		}
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainArtifact = Input.SolveRequest->FrozenTerrainBiomeAdapterInput;
		// Surface and Underground admission feed one explicit stepped-topology decision.
		const bool bUsesSteppedTopology = Input.ModePlan.bUsesSteppedTerrainTopology;
		if (!ValidateTerrainEvidenceForMode(
				Input.ModePlan.EnvironmentMode,
				bUsesSteppedTopology,
				TerrainArtifact,
				OutFailureReason))
		{
			if (bUsesSteppedTopology && OutFailureKind != nullptr)
			{
				*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::StageTopologyInfeasible;
			}
			return false;
		}
		// Populate basic frozen-contract fields before StageMap and bridge injection.
		OutOutput.bSucceeded = true;
		OutOutput.ModePlan = Input.ModePlan;
		OutOutput.SteppedTerrainSupportMap = Input.SolveRequest->SteppedTerrainSupportMap;
		OutOutput.FrozenTerrainContract.ContractId = TerrainArtifact.ArtifactId;
		OutOutput.FrozenTerrainContract.SiteCenterBlockWorldPos = Input.ModePlan.SiteCenterBlockWorldPos;
		OutOutput.FrozenTerrainContract.FootprintMinBlockWorldPos = TerrainArtifact.FootprintMinBlockWorldPos;
		OutOutput.FrozenTerrainContract.SharedCellSizeInBlocks = Input.Manifest != nullptr
			? Input.Manifest->SharedCellSizeInBlocks
			: FIntVector::ZeroValue;
		OutOutput.FrozenTerrainContract.FootprintSizeInCells = Input.SolveRequest->FootprintSize;

		const int32 CellHeight = Input.Manifest != nullptr ? Input.Manifest->SharedCellSizeInBlocks.Z : 0;

		// Build frozen stage map from stepped support samples BEFORE bridge injection
		// so the bridge pass can adjust ResolvedStageBaseBlockWorldZ for shifted columns.
		if (bUsesSteppedTopology
			&& TerrainArtifact.bHasSteppedSupportEvidence
			&& !TerrainArtifact.SteppedSupportSamples.IsEmpty()
			&& CellHeight > 0)
		{
			int32 BaseSnappedSupportFloorZ = TerrainArtifact.SteppedSupportSamples[0].SnappedSupportFloorZ;
			TMap<FIntPoint, const FLayoutSteppedTerrainSupportSample*> FirstSampleByXY;
			for (const FLayoutSteppedTerrainSupportSample& Sample : TerrainArtifact.SteppedSupportSamples)
			{
				const FIntPoint FootprintXY(Sample.LocalCell.X, Sample.LocalCell.Y);
				if (!FirstSampleByXY.Contains(FootprintXY))
				{
					FirstSampleByXY.Add(FootprintXY, &Sample);
				}
				BaseSnappedSupportFloorZ = FMath::Min(BaseSnappedSupportFloorZ, Sample.SnappedSupportFloorZ);
			}

			OutOutput.FrozenTerrainContract.StageMap.Reserve(FirstSampleByXY.Num());
			for (const auto& Pair : FirstSampleByXY)
			{
				const FLayoutSteppedTerrainSupportSample* Sample = Pair.Value;
				FLayoutFrozenTerrainStageCellRecord Record;
				Record.FootprintCellXY = Pair.Key;
				Record.SnappedSupportFloorZ = Sample->SnappedSupportFloorZ;
				Record.VerticalShiftBlocks = Sample->SnappedSupportFloorZ - BaseSnappedSupportFloorZ;
				Record.TerrainStageIndex = Record.VerticalShiftBlocks / CellHeight;
				// Anchor at the first lattice plane above solid terrain:
				//   cell base = ceil((surface + 1) / cellHeight) * cellHeight
				// SnappedSupportFloorZ snaps DOWN to the lattice floor, which is below
				// the terrain surface.  The occupied-stage-base shifts up by one
				// lattice unit so cells rest on top of terrain instead of inside it.
				const int32 OccupiedStageBaseZ =
					FMath::DivideAndRoundUp(Sample->SupportSurfaceZ + 1, CellHeight) * CellHeight;
				Record.ResolvedStageBaseBlockWorldZ = OccupiedStageBaseZ;
				Record.SourceEvidenceId = FLayoutId(*FString::Printf(
					TEXT("SteppedSupport.%d.%d"),
					Sample->LocalCell.X,
					Sample->LocalCell.Y));
				OutOutput.FrozenTerrainContract.StageMap.Add(MoveTemp(Record));
			}

			// Sparse continuation corridors own only their planned columns. Root
			// footprints are rectangular, so this remains the same root contract.
			TSet<FIntPoint> ExpectedPlannedColumns;
			for (const FLayoutPlannedCell& PlannedCell : Input.SolveRequest->PlannedCells)
			{
				ExpectedPlannedColumns.Add(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
			}
			// Automatic roots defer cell generation until below. Their empty input plan
			// still requires complete rectangular support; supplied sparse plans remain exact.
			if (Input.SolveRequest->PlannedCells.IsEmpty()
				&& Input.ModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot)
			{
				for (int32 Y = 0; Y < Input.SolveRequest->FootprintSize.Y; ++Y)
				{
					for (int32 X = 0; X < Input.SolveRequest->FootprintSize.X; ++X)
					{
						ExpectedPlannedColumns.Add(FIntPoint(X, Y));
					}
				}
			}
			bool bStageMapCoversPlannedColumns =
				OutOutput.FrozenTerrainContract.StageMap.Num() == ExpectedPlannedColumns.Num();
			for (const FIntPoint& ExpectedColumn : ExpectedPlannedColumns)
			{
				bStageMapCoversPlannedColumns &= OutOutput.FrozenTerrainContract.StageMap.ContainsByPredicate(
					[&ExpectedColumn](const FLayoutFrozenTerrainStageCellRecord& Record)
					{
						return Record.FootprintCellXY == ExpectedColumn;
					});
			}
			if (!bStageMapCoversPlannedColumns)
			{
				OutFailureReason = FString::Printf(
					TEXT("Shared stepped terrain stage map covers %d planned columns, expected %d (footprint bounds %dx%d)."),
					OutOutput.FrozenTerrainContract.StageMap.Num(),
					ExpectedPlannedColumns.Num(),
					Input.SolveRequest->FootprintSize.X,
					Input.SolveRequest->FootprintSize.Y);
				return false;
			}
		}

		// Endpoint world centers are terrain authority, not a post-topology assertion.
		// Apply them before bridge/deck generation so a root or route seam one-cell
		// change creates real stepped topology instead of a raised empty footprint.
		FLayoutFrozenTerrainBiomeAdapterInput EffectiveTerrainArtifact = TerrainArtifact;
		if (bUsesSteppedTopology
			&& CellHeight > 0)
		{
			const int32 StructuralAlignmentLevel =
				Input.SolveRequest->RootContinuationSelection.ResolvedEntryLevel != INDEX_NONE
					? Input.SolveRequest->RootContinuationSelection.ResolvedEntryLevel
					: 0;
			TMap<FIntPoint, int32> OriginalStageByColumn;
			// Frozen placement evidence owns terrain-stage connectivity. StageMap snap
			// floors can be equal in sparse fixtures even when terrain evidence already
			// identifies a real raised component for bridge topology.
			for (const FLayoutTerrainPlacementCellEvidence& Evidence : TerrainArtifact.TerrainPlacementCells)
			{
				const FIntPoint Column(Evidence.Cell.X, Evidence.Cell.Y);
				const int32* const ExistingStage = OriginalStageByColumn.Find(Column);
				if (ExistingStage != nullptr && *ExistingStage != Evidence.TerrainStageIndex)
				{
					OutFailureReason = TEXT("Continuation frozen terrain topology assigns conflicting stages to one column.");
					return false;
				}
				OriginalStageByColumn.Add(Column, Evidence.TerrainStageIndex);
			}

			// Endpoint commitments translate whole terrain contract in world space. They
			// must not independently rebase flat components: that double-counts a terrain
			// stage when bridge injection also shifts its planned cells upward.
			TOptional<int32> ContractWorldTranslation;
			for (const FLayoutCommittedEndpointAnchor& EndpointAnchor : Input.SolveRequest->CommittedEndpointAnchors)
			{
				if (EndpointAnchor.RequiredWorldCenterBlockZ == INDEX_NONE)
				{
					continue;
				}
				const FLayoutPlannedCell* const EntryCell = Input.SolveRequest->PlannedCells.FindByPredicate(
					[&EndpointAnchor](const FLayoutPlannedCell& PlannedCell)
					{
						return PlannedCell.Intent == ELayoutCellIntent::Entry
							&& PlannedCell.Cell.X == EndpointAnchor.LocalCell.X
							&& PlannedCell.Cell.Y == EndpointAnchor.LocalCell.Y;
					});
				if (EntryCell == nullptr)
				{
					OutFailureReason = FString::Printf(
						TEXT("Continuation endpoint height contract '%s' lost its entry terrain authority."),
						*EndpointAnchor.CommitmentId.ToString());
					return false;
				}
				const FIntPoint EntryXY(EntryCell->Cell.X, EntryCell->Cell.Y);
				const int32* const OriginalEntryStage = OriginalStageByColumn.Find(EntryXY);
				const FLayoutFrozenTerrainStageCellRecord* const EntryStage =
					OutOutput.FrozenTerrainContract.StageMap.FindByPredicate([&EntryXY](const FLayoutFrozenTerrainStageCellRecord& Candidate)
					{
						return Candidate.FootprintCellXY == EntryXY;
					});
				if (OriginalEntryStage == nullptr || EntryStage == nullptr)
				{
					OutFailureReason = FString::Printf(
						TEXT("Continuation endpoint height contract '%s' lost its entry terrain stage."),
						*EndpointAnchor.CommitmentId.ToString());
					return false;
				}
				const int32 ContractedSharedBaseZ = EndpointAnchor.RequiredWorldCenterBlockZ
					- (EntryCell->Cell.Z + *OriginalEntryStage - StructuralAlignmentLevel) * CellHeight
					- Input.SolveRequest->TemplatePlacementZOffsetBlocks
					- CellHeight / 2;
				const int32 CanonicalSharedBaseZ = EntryStage->ResolvedStageBaseBlockWorldZ - *OriginalEntryStage * CellHeight;
				const int32 Translation = ContractedSharedBaseZ - CanonicalSharedBaseZ;
				if (ContractWorldTranslation.IsSet() && ContractWorldTranslation.GetValue() != Translation)
				{
					OutFailureReason = TEXT("Continuation endpoint height contracts disagree with frozen terrain topology.");
					return false;
				}
				ContractWorldTranslation = Translation;
			}
			if (ContractWorldTranslation.IsSet())
			{
				for (FLayoutFrozenTerrainStageCellRecord& Stage : OutOutput.FrozenTerrainContract.StageMap)
				{
					Stage.ResolvedStageBaseBlockWorldZ += ContractWorldTranslation.GetValue();
				}
			}

			// Endpoint contracts anchor world-space placement, but cannot redefine
			// normalized terrain topology. Re-deriving stages from contracted world Z
			// turned a physical one-step terrain edge into a false two-step cliff.
			for (FLayoutFrozenTerrainStageCellRecord& Stage : OutOutput.FrozenTerrainContract.StageMap)
			{
				const int32* const OriginalStage = OriginalStageByColumn.Find(Stage.FootprintCellXY);
				if (OriginalStage == nullptr)
				{
					OutFailureReason = TEXT("Continuation stage map lost normalized terrain-stage evidence.");
					return false;
				}
				Stage.TerrainStageIndex = *OriginalStage;
				Stage.VerticalShiftBlocks = *OriginalStage * CellHeight;
			}
			for (FLayoutTerrainPlacementCellEvidence& Evidence : EffectiveTerrainArtifact.TerrainPlacementCells)
			{
				if (const int32* const OriginalStage = OriginalStageByColumn.Find(FIntPoint(Evidence.Cell.X, Evidence.Cell.Y)))
				{
					Evidence.TerrainStageIndex = *OriginalStage;
					Evidence.VerticalShiftBlocks = *OriginalStage * CellHeight;
				}
			}
		}

		// Build active cells and inject bridge cells for stage transitions.
		// StageMap is passed so the bridge pass can adjust ResolvedStageBaseBlockWorldZ
		// for higher-stage columns (Z-shift to align with unshifted neighbours).
		TArray<FLayoutContractActiveCellRecord> ActiveCells;
		TArray<FLayoutPlannedCell> ExpandedPlannedCells = Input.SolveRequest->PlannedCells;
		if (ExpandedPlannedCells.IsEmpty())
		{
			// For stepped profiles, derive cells from stepped support samples
			// to match the planner's TryDeriveSteppedSurfaceContract output.
			if (bUsesSteppedTopology
				&& TerrainArtifact.bHasSteppedSupportEvidence
				&& !TerrainArtifact.SteppedSupportSamples.IsEmpty())
			{
				TSet<FIntPoint> UniqueColumns;
				int32 MaxCellX = 0, MaxCellY = 0;
				for (const FLayoutSteppedTerrainSupportSample& Sample : TerrainArtifact.SteppedSupportSamples)
				{
					UniqueColumns.Add(FIntPoint(Sample.LocalCell.X, Sample.LocalCell.Y));
					MaxCellX = FMath::Max(MaxCellX, Sample.LocalCell.X);
					MaxCellY = FMath::Max(MaxCellY, Sample.LocalCell.Y);
				}
				const int32 LevelCount = Input.SolveRequest->ProfileSnapshot.LevelCount;
				for (const FIntPoint& Col : UniqueColumns)
				{
					for (int32 Level = 0; Level < LevelCount; ++Level)
					{
						FLayoutPlannedCell Cell;
						Cell.Cell = FIntVector(Col.X, Col.Y, Level);
						Cell.Intent = (Col.X == 0 || Col.Y == 0
							|| Col.X == MaxCellX || Col.Y == MaxCellY)
							? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
						ExpandedPlannedCells.Add(Cell);
					}
				}
			}
			else
			{
				const int32 LevelCount = Input.SolveRequest->ProfileSnapshot.LevelCount;
				for (int32 Level = 0; Level < LevelCount; ++Level)
				{
					for (int32 Y = 0; Y < Input.SolveRequest->FootprintSize.Y; ++Y)
					{
						for (int32 X = 0; X < Input.SolveRequest->FootprintSize.X; ++X)
						{
							FLayoutPlannedCell Cell;
							Cell.Cell = FIntVector(X, Y, Level);
							Cell.Intent = (X == 0 || Y == 0
								|| X == Input.SolveRequest->FootprintSize.X - 1
								|| Y == Input.SolveRequest->FootprintSize.Y - 1)
								? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
							ExpandedPlannedCells.Add(Cell);
						}
					}
				}
			}
		}
		// Retain pre-finalization cells for rejected-preview diagnostics. They never
		// become active contract cells or solve/apply input on this failure path.
		OutOutput.PlannedCells = ExpandedPlannedCells;
		if (!TryBuildActiveCellsFromPlacementEvidence(
				bUsesSteppedTopology,
				*Input.SolveRequest,
				EffectiveTerrainArtifact,
				ActiveCells,
				ExpandedPlannedCells,
				OutFailureReason,
				CellHeight,
				Input.SolveRequest->WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells > 0
					? Input.SolveRequest->WorldBindingPlacementPolicy.TerrainTransition.MinimumSteppedTerrainShiftClusterCells
					: 3,
				&OutOutput.FrozenTerrainContract.StageMap,
				&OutOutput.Diagnostics,
				OutFailureKind))
		{
			if (!AreTerrainSeamsEnabled(*Input.SolveRequest))
			{
				ClearTerrainSeamContracts(OutOutput.PlannedCells);
			}
			return false;
		}
		OutOutput.PlannedCells = MoveTemp(ExpandedPlannedCells);
		if (!AreTerrainSeamsEnabled(*Input.SolveRequest))
		{
			ClearTerrainSeamContracts(OutOutput.PlannedCells);
		}
		OutOutput.ActiveCells = MoveTemp(ActiveCells);
		OutOutput.FrozenTerrainContract.ActiveCells = OutOutput.ActiveCells;


		if (bUsesSteppedTopology)
		{
			TMap<FIntPoint, FLayoutSteppedTerrainSupportSample> SourceSupportByColumn;
			TSet<FIntVector> SupportedCells;
			for (const FLayoutSteppedTerrainSupportSample& SupportSample : OutOutput.SteppedTerrainSupportMap.SupportSamples)
			{
				SourceSupportByColumn.FindOrAdd(FIntPoint(SupportSample.LocalCell.X, SupportSample.LocalCell.Y)) = SupportSample;
				SupportedCells.Add(SupportSample.LocalCell);
			}

			for (const FLayoutPlannedCell& PlannedCell : OutOutput.PlannedCells)
			{
				if (SupportedCells.Contains(PlannedCell.Cell))
				{
					continue;
				}

				const FLayoutSteppedTerrainSupportSample* const SourceSupport =
					SourceSupportByColumn.Find(FIntPoint(PlannedCell.Cell.X, PlannedCell.Cell.Y));
				if (SourceSupport == nullptr)
				{
					continue;
				}

				FLayoutSteppedTerrainSupportSample& InheritedSupport =
					OutOutput.SteppedTerrainSupportMap.SupportSamples.Add_GetRef(*SourceSupport);
				InheritedSupport.LocalCell = PlannedCell.Cell;
				SupportedCells.Add(PlannedCell.Cell);
			}
		}

		// Build terrain contracts after stage shifts and bridge injection so every
		// contract cell has the exact active-cell authority used by realization.
		FLayoutRegionSolveRequest TransformedContractRequest = *Input.SolveRequest;
		TransformedContractRequest.PlannedCells = OutOutput.PlannedCells;
		if (!TryBuildActiveTerrainCellContractsFromEvidence(
				Input.ModePlan.EnvironmentMode,
				bUsesSteppedTopology,
				Input.ModePlan.EnvironmentMode == ELayoutContractEnvironmentMode::UndergroundPocketPlacement
					&& bUsesSteppedTopology,
				TransformedContractRequest,
				TerrainArtifact,
				OutOutput.FrozenTerrainContract.CellContracts,
				OutFailureReason))
		{
			return false;
		}
		if (bUsesSteppedTopology
			&& !TryBuildTerrainBackedNeighborFaceRecords(
				OutOutput.PlannedCells,
				OutOutput.FrozenTerrainContract.StageMap,
				TerrainArtifact.SteppedNeighborHaloSamples,
				CellHeight,
				OutOutput.FrozenTerrainContract.TerrainBackedNeighborFaces,
				OutFailureReason))
		{
			return false;
		}

		// Fail fast when shared stepped topology has no walkable boundary Entry candidate.
		if (bUsesSteppedTopology
			&& !OutOutput.FrozenTerrainContract.CellContracts.IsEmpty())
		{
			const int32 FootprintW = Input.SolveRequest->FootprintSize.X;
			const int32 FootprintH = Input.SolveRequest->FootprintSize.Y;
			int32 WalkableBoundaryCount = 0;
			for (const FLayoutTerrainCellContractRecord& Contract : OutOutput.FrozenTerrainContract.CellContracts)
			{
				if ((Contract.Cell.X == 0 || Contract.Cell.X == FootprintW - 1
					|| Contract.Cell.Y == 0 || Contract.Cell.Y == FootprintH - 1)
					&& Contract.bEntryWalkable)
				{
					++WalkableBoundaryCount;
				}
			}
			if (WalkableBoundaryCount == 0)
			{
				OutFailureReason = TEXT("Shared stepped terrain rejected: no boundary cell has walkable entry traversability. Every entry candidate would face a cliff, excavation, or ramp-too-steep.");
				return false;
			}
		}

		// Collect pre-qualified entry cells for the solver directly from the
		// final planned cell set.  Iterating CellContracts uses pre-shift Z=0
		// positions that no longer match after column shifting.
		{
			const int32 FootprintW = Input.SolveRequest->FootprintSize.X;
			const int32 FootprintH = Input.SolveRequest->FootprintSize.Y;

			TMap<FIntVector, const FLayoutPlannedCell*> PlannedCellByPosition;
			for (const FLayoutPlannedCell& PC : OutOutput.PlannedCells)
			{
				PlannedCellByPosition.Add(PC.Cell, &PC);
			}
			TMap<FIntVector, const FLayoutTerrainCellContractRecord*> TerrainContractByCell;
			for (const FLayoutTerrainCellContractRecord& Contract : OutOutput.FrozenTerrainContract.CellContracts)
			{
				TerrainContractByCell.Add(Contract.Cell, &Contract);
			}
			TMap<FIntPoint, int32> SurfaceZByBlockXY;
			for (const FLayoutTerrainSurfaceSample& SurfaceSample : TerrainArtifact.SurfaceSamples)
			{
				if (SurfaceSample.bIsValid)
				{
					SurfaceZByBlockXY.Add(SurfaceSample.BlockXY, SurfaceSample.SurfaceBlockWorldPos.Z);
				}
			}
			const auto IsTerrainAlignedWithFlatFloor = [
				&SurfaceZByBlockXY,
				&TerrainArtifact,
				&Input,
				FootprintW,
				FootprintH](const FIntVector& Cell)
			{
				if (Input.ModePlan.EnvironmentMode != ELayoutContractEnvironmentMode::NonSteppedWorldPlacement)
				{
					return true;
				}
				const FIntPoint CellSize(
					TerrainArtifact.FootprintSizeInBlocks.X / FMath::Max(1, FootprintW),
					TerrainArtifact.FootprintSizeInBlocks.Y / FMath::Max(1, FootprintH));
				const int32* const SurfaceZ = SurfaceZByBlockXY.Find(FIntPoint(
					TerrainArtifact.FootprintMinBlockWorldPos.X + Cell.X * CellSize.X,
					TerrainArtifact.FootprintMinBlockWorldPos.Y + Cell.Y * CellSize.Y));
				const int32 FlatFloorTolerance = FMath::Max(
					0,
					Input.SolveRequest->WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth / 2);
				return SurfaceZ != nullptr
					&& FMath::Abs((*SurfaceZ + 1) - Input.ModePlan.SiteCenterBlockWorldPos.Z) <= FlatFloorTolerance;
			};

			for (const FLayoutPlannedCell& PC : OutOutput.PlannedCells)
			{
				// Authored Entries require both a clear exterior terrain cell and a clear
				// first interior step; planned topology alone cannot prove terrain access.
				if (PC.Cell.Z != 0 || PC.bIsBridgeCell)
				{
					continue;
				}
				if (PC.Intent != ELayoutCellIntent::Interior
					&& PC.Intent != ELayoutCellIntent::Boundary
					&& PC.Intent != ELayoutCellIntent::Entry)
				{
					continue;
				}

				const bool bCorner = (PC.Cell.X == 0 || PC.Cell.X == FootprintW - 1)
					&& (PC.Cell.Y == 0 || PC.Cell.Y == FootprintH - 1);
				if (bCorner)
				{
					continue;
				}

				const bool bOnPerimeter = PC.Cell.X == 0 || PC.Cell.X == FootprintW - 1
					|| PC.Cell.Y == 0 || PC.Cell.Y == FootprintH - 1;
				if (!bOnPerimeter)
				{
					continue;
				}

				FIntVector InteriorNeighbor = PC.Cell;
				if (PC.Cell.X == 0) { InteriorNeighbor.X = 1; }
				else if (PC.Cell.X == FootprintW - 1) { InteriorNeighbor.X = FootprintW - 2; }
				else if (PC.Cell.Y == 0) { InteriorNeighbor.Y = 1; }
				else if (PC.Cell.Y == FootprintH - 1) { InteriorNeighbor.Y = FootprintH - 2; }
				const FLayoutPlannedCell* const InteriorPC = PlannedCellByPosition.FindRef(InteriorNeighbor);
				const FLayoutTerrainCellContractRecord* const ExteriorTerrain = TerrainContractByCell.FindRef(PC.Cell);
				const FLayoutTerrainCellContractRecord* const InteriorTerrain = TerrainContractByCell.FindRef(InteriorNeighbor);
				if (InteriorPC == nullptr
					|| (InteriorPC->Intent != ELayoutCellIntent::Interior
						&& InteriorPC->Intent != ELayoutCellIntent::Core)
					|| InteriorPC->bIsBridgeCell
					|| ExteriorTerrain == nullptr || !ExteriorTerrain->bEntryWalkable
					|| InteriorTerrain == nullptr || !InteriorTerrain->bEntryWalkable
					|| !IsTerrainAlignedWithFlatFloor(PC.Cell)
					|| !IsTerrainAlignedWithFlatFloor(InteriorNeighbor))
				{
					continue;
				}

				OutOutput.QualifiedEntryCells.Add(PC.Cell);
			}

			const bool bRequiresAuthoredExteriorEntries =
				Input.ModePlan.Scope != ELayoutContractRegionScope::Continuation;
			const int32 MinimumRequiredEntryCount = !bRequiresAuthoredExteriorEntries
				? 0
				: Input.SolveRequest->ProfileSnapshot.EntryCountMode == ELayoutCountConstraintMode::Exact
					? Input.SolveRequest->ProfileSnapshot.EntryCount
					: Input.SolveRequest->ProfileSnapshot.EntryCountMode == ELayoutCountConstraintMode::Range
						? Input.SolveRequest->ProfileSnapshot.MinEntryCount
						: 0;
			if (OutOutput.QualifiedEntryCells.Num() < MinimumRequiredEntryCount)
			{
				if (OutFailureKind != nullptr
					&& bRequiresAuthoredExteriorEntries
					&& bUsesSteppedTopology)
				{
					*OutFailureKind = ELayoutSteppedTerrainFinalizationFailureKind::EntryTerrainQualificationInfeasible;
				}
				OutFailureReason = FString::Printf(
					TEXT("Terrain adapter found %d clear exterior Entry path(s), but profile requires at least %d. Each Entry requires walkable terrain at the boundary cell and its first interior neighbor."),
					OutOutput.QualifiedEntryCells.Num(),
					MinimumRequiredEntryCount);
				return false;
			}
		}

		return true;
	}
	default:
		OutFailureReason = TEXT("Layout contract adapter received an unknown environment mode.");
		return false;
	}
}
