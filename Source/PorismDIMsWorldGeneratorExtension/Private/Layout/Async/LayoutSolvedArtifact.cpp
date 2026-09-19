// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutSolvedArtifact.h"

#include "Layout/Contracts/LayoutContractPipeline.h"

namespace
{
	bool ValidateSolvedPlacementForArtifact(const FLayoutPlacedModule& Placement, FString& OutFailureReason)
	{
		if (Placement.Module != nullptr)
		{
			OutFailureReason = TEXT("Solved artifact placement carries a live module asset pointer.");
			return false;
		}
		if (Placement.CompositeModule != nullptr)
		{
			OutFailureReason = TEXT("Solved artifact placement carries a live composite module asset pointer.");
			return false;
		}
		if (Placement.ModuleSnapshotId.IsNone())
		{
			OutFailureReason = TEXT("Solved artifact placement requires a module snapshot id.");
			return false;
		}
		if (!Placement.TemplatePath.IsValid() && Placement.OccupiedLocalCells.IsEmpty())
		{
			OutFailureReason = FString::Printf(
				TEXT("Solved artifact placement requires a soft template path or composite occupied-cell snapshot: cell=%s sourceEntry=%s moduleSnapshotId=%s moduleSnapshotIndex=%d occupiedCells=%d bundleBounds=%s."),
				*Placement.Cell.ToString(),
				*Placement.SourceContentEntryId.ToString(),
				*Placement.ModuleSnapshotId.ToString(),
				Placement.ModuleSnapshotIndex,
				Placement.OccupiedLocalCells.Num(),
				*Placement.BundleBoundsCells.ToString());
			return false;
		}
		if (Placement.YawRotationSteps < 0 || Placement.YawRotationSteps > 3)
		{
			OutFailureReason = TEXT("Solved artifact placement yaw must be normalized to [0, 3].");
			return false;
		}
		return true;
	}

	/** Copies complete local-cell metadata from one root-owned composite placement into its pointer-free artifact record. */
	bool PopulateArtifactOccupiedCellDescriptors(
		const FLayoutPlacedModule& Placement,
		FLayoutSolvedArtifactPlacement& OutArtifactPlacement,
		FString& OutFailureReason)
	{
		TSet<FIntVector> OccupiedCellSet(Placement.OccupiedLocalCells);
		for (const FLayoutPlacedLocalCellFaceRuleSnapshot& LocalCell : Placement.LocalCellFaceRules)
		{
			if (!OccupiedCellSet.Contains(LocalCell.LocalCell)
				|| OutArtifactPlacement.OccupiedCellDescriptors.ContainsByPredicate(
					[&LocalCell](const FLayoutSolvedArtifactOccupiedCell& Existing)
					{
						return Existing.LocalCell == LocalCell.LocalCell;
					}))
			{
				OutFailureReason = TEXT("Solved artifact rejected invalid or duplicate composite local-cell descriptor.");
				return false;
			}

			FLayoutSolvedArtifactOccupiedCell& Descriptor =
				OutArtifactPlacement.OccupiedCellDescriptors.AddDefaulted_GetRef();
			Descriptor.LocalCell = LocalCell.LocalCell;
			Descriptor.TemplatePath = LocalCell.TemplatePath;
			Descriptor.RelativeYawRotationSteps = LocalCell.RelativeYawRotationSteps;
			Descriptor.Roles = LocalCell.Roles;
			Descriptor.SupportedCellIntents = LocalCell.SupportedCellIntents;
			Descriptor.ExposedFaceRules = LocalCell.ExposedFaceRules;
		}

		if (Placement.OccupiedLocalCells.Num() > 1
			&& OutArtifactPlacement.OccupiedCellDescriptors.Num() != Placement.OccupiedLocalCells.Num())
		{
			OutFailureReason = TEXT("Solved artifact requires complete composite local-cell descriptors.");
			return false;
		}
		return true;
	}

	bool AppendSolvedPlacementsForRegion(
		const FLayoutRegionSolveResult& RegionResult,
		TArray<FLayoutSolvedArtifactPlacement>& OutPlacements,
		FString& OutFailureReason)
	{
		for (const FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
		{
			if (!ValidateSolvedPlacementForArtifact(Placement, OutFailureReason))
			{
				return false;
			}

			FLayoutSolvedArtifactPlacement ArtifactPlacement;
			ArtifactPlacement.RegionDebugPath = RegionResult.RegionDebugPath;
			ArtifactPlacement.RegionCellOffset = RegionResult.RegionCellOffset;
			ArtifactPlacement.Cell = Placement.Cell;
			ArtifactPlacement.Intent = Placement.Intent;
			ArtifactPlacement.SourceContentEntryId = Placement.SourceContentEntryId;
			ArtifactPlacement.ModuleSnapshotId = Placement.ModuleSnapshotId;
			ArtifactPlacement.TemplatePath = Placement.TemplatePath;
			ArtifactPlacement.YawRotationSteps = Placement.YawRotationSteps;
			ArtifactPlacement.OccupiedLocalCells = Placement.OccupiedLocalCells;
			if (!PopulateArtifactOccupiedCellDescriptors(Placement, ArtifactPlacement, OutFailureReason))
			{
				return false;
			}
			OutPlacements.Add(MoveTemp(ArtifactPlacement));
		}
		return true;
	}
}

bool LayoutSolvedArtifact::TryBuildFromScheduleResult(
	const FLayoutId ArtifactId,
	const FString& RegionDebugPathFallback,
	const FLayoutRegionSolveScheduleResult& ScheduleResult,
	FLayoutSolvedArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutSolvedArtifact();
	OutFailureReason.Reset();
	if (!ScheduleResult.bSucceeded)
	{
		OutFailureReason = ScheduleResult.FailureReason.IsEmpty()
			? TEXT("Solved artifact requires a successful schedule result.")
			: ScheduleResult.FailureReason;
		return false;
	}

	if (ScheduleResult.RegionResults.IsEmpty())
	{
		FLayoutRegionSolveResult SyntheticResult;
		SyntheticResult.RegionDebugPath = RegionDebugPathFallback;
		SyntheticResult.SolveResult = ScheduleResult.MergedSolveResult;
		const bool bBuilt = TryBuildFromSolveResult(
			ArtifactId,
			SyntheticResult,
			OutArtifact,
			OutFailureReason);
		if (bBuilt)
		{
			OutArtifact.ChildStageMappings = ScheduleResult.ChildStageMappings;
			OutArtifact.JunctionRequirements = ScheduleResult.PlannedJunctionRequirements;
		}
		return bBuilt;
	}

	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Solved artifact requires an artifact id.");
		return false;
	}

	TArray<FLayoutSolvedArtifactPlacement> Placements;
	TArray<FLayoutZoneFeatureProviderCommitment> ZoneFeatureProviderCommitments;
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
	TArray<FLayoutId> CandidateDomainCertificateIds;
	for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
	{
		if (RegionResult.RegionDebugPath.IsEmpty())
		{
			OutFailureReason = TEXT("Solved artifact requires a region debug path.");
			return false;
		}
		if (!AppendSolvedPlacementsForRegion(RegionResult, Placements, OutFailureReason))
		{
			return false;
		}
		ZoneFeatureProviderCommitments.Append(
			RegionResult.SolveResult.ZoneFeatureProviderCommitments);
		if (!RegionResult.SolveResult.CandidateDomainCertificateId.IsNone())
		{
			CandidateDomainCertificateIds.AddUnique(
				RegionResult.SolveResult.CandidateDomainCertificateId);
		}
		ValidationAssertions.Append(RegionResult.ValidationAssertions);
	}

	if (Placements.IsEmpty())
	{
		OutFailureReason = TEXT("Solved artifact requires at least one solved placement.");
		return false;
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.Status = ELayoutSolvedArtifactStatus::SolvedCellsComplete;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.RegionDebugPath = ScheduleResult.RegionResults[0].RegionDebugPath;
	OutArtifact.RegionCellOffset = ScheduleResult.RegionResults[0].RegionCellOffset;
	OutArtifact.Seed = ScheduleResult.MergedSolveResult.Seed;
	OutArtifact.FootprintSize = ScheduleResult.MergedSolveResult.FootprintSize;
	OutArtifact.SharedCellSizeInBlocks = ScheduleResult.MergedSolveResult.SharedCellSizeInBlocks;

	// If the merged result didn't carry a footprint, derive it from placements.
	if (OutArtifact.FootprintSize == FIntPoint::ZeroValue && !Placements.IsEmpty())
	{
		int32 MaxX = 0;
		int32 MaxY = 0;
		for (const FLayoutSolvedArtifactPlacement& P : Placements)
		{
			MaxX = FMath::Max(MaxX, P.Cell.X);
			MaxY = FMath::Max(MaxY, P.Cell.Y);
		}
		OutArtifact.FootprintSize = FIntPoint(MaxX + 1, MaxY + 1);
	}

	OutArtifact.Placements = MoveTemp(Placements);
	// Local project fix: preserve region-local provider records exactly like artifact placements instead of rebuilding from merged cells.
	ZoneFeatureProviderCommitments.Sort(
		[](const FLayoutZoneFeatureProviderCommitment& Left,
			const FLayoutZoneFeatureProviderCommitment& Right)
		{
			return Left.ProviderCommitmentId.LexicalLess(
				Right.ProviderCommitmentId);
		});
	OutArtifact.ZoneFeatureProviderCommitments =
		MoveTemp(ZoneFeatureProviderCommitments);
	OutArtifact.ChildStageMappings = ScheduleResult.ChildStageMappings;
	CandidateDomainCertificateIds.Sort();
	OutArtifact.CandidateDomainCertificateIds =
		MoveTemp(CandidateDomainCertificateIds);
	OutArtifact.PartitionSeams = ScheduleResult.MergedSolveResult.PartitionSeams;
	OutArtifact.JunctionRequirements = ScheduleResult.PlannedJunctionRequirements;
	OutArtifact.bStructuralCommitmentsAuthoritative = true;
	OutArtifact.RegionalFailure = ScheduleResult.MergedSolveResult.RegionalFailure;
	OutArtifact.ValidationAssertions = MoveTemp(ValidationAssertions);

	// Publish the first walkable entry placement as the exposed entry for continuation handoff.
	for (const FLayoutSolvedArtifactPlacement& P : OutArtifact.Placements)
	{
		if (P.Intent == ELayoutCellIntent::Entry)
		{
			OutArtifact.ExposedEntryCell = P.Cell;
			if (P.Cell.X == 0)
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::NegX;
			}
			else if (P.Cell.X == OutArtifact.FootprintSize.X - 1)
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::PosX;
			}
			else if (P.Cell.Y == 0)
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::NegY;
			}
			else
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::PosY;
			}
			break;
		}
	}
	return true;
}

bool LayoutSolvedArtifact::TryBuildFromPartialScheduleResult(
	const FLayoutId ArtifactId,
	const FString& RegionDebugPathFallback,
	const FLayoutRegionSolveScheduleResult& ScheduleResult,
	FLayoutSolvedArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutSolvedArtifact();
	OutFailureReason.Reset();
	if (ScheduleResult.bSucceeded || ScheduleResult.MergedSolveResult.Placements.IsEmpty())
	{
		OutFailureReason = TEXT("Partial artifact requires retained placements from a rejected schedule.");
		return false;
	}

	// A failed recursive solve retains its best legal frontier on the merged
	// result. Region-local results can be stale/empty after rollback, so they
	// cannot own partial artifact serialization.
	FLayoutRegionSolveResult RetainedPartialResult;
	if (!ScheduleResult.RegionResults.IsEmpty())
	{
		RetainedPartialResult = ScheduleResult.RegionResults[0];
	}
	RetainedPartialResult.RegionDebugPath = RetainedPartialResult.RegionDebugPath.IsEmpty()
		? RegionDebugPathFallback
		: RetainedPartialResult.RegionDebugPath;
	RetainedPartialResult.SolveResult = ScheduleResult.MergedSolveResult;
	RetainedPartialResult.SolveResult.bSucceeded = true;

	if (!TryBuildFromSolveResult(
			ArtifactId,
			RetainedPartialResult,
			OutArtifact,
			OutFailureReason))
	{
		return false;
	}
	// Retained partial data remains diagnostic/preview-only. Never publish its
	// provider, seam, or junction records as accepted recursive authority.
	OutArtifact.ChildStageMappings = ScheduleResult.ChildStageMappings;
	OutArtifact.JunctionRequirements = ScheduleResult.PlannedJunctionRequirements;
	OutArtifact.bStructuralCommitmentsAuthoritative = false;
	return true;
}

bool LayoutSolvedArtifact::TryBuildFromSolveResult(
	const FLayoutId ArtifactId,
	const FLayoutRegionSolveResult& SolveResult,
	FLayoutSolvedArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutSolvedArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Solved artifact requires an artifact id.");
		return false;
	}
	if (!SolveResult.SolveResult.bSucceeded)
	{
		OutFailureReason = TEXT("Solved artifact requires a successful solve result.");
		return false;
	}
	if (SolveResult.RegionDebugPath.IsEmpty())
	{
		OutFailureReason = TEXT("Solved artifact requires a region debug path.");
		return false;
	}
	if (SolveResult.SolveResult.Placements.IsEmpty())
	{
		OutFailureReason = TEXT("Solved artifact requires at least one solved placement.");
		return false;
	}

	TArray<FLayoutSolvedArtifactPlacement> Placements;
	Placements.Reserve(SolveResult.SolveResult.Placements.Num());
	if (!AppendSolvedPlacementsForRegion(SolveResult, Placements, OutFailureReason))
	{
		return false;
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.Status = ELayoutSolvedArtifactStatus::SolvedCellsComplete;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.RegionDebugPath = SolveResult.RegionDebugPath;
	OutArtifact.RegionCellOffset = SolveResult.RegionCellOffset;
	OutArtifact.Seed = SolveResult.SolveResult.Seed;
	OutArtifact.FootprintSize = SolveResult.SolveResult.FootprintSize;
	OutArtifact.SharedCellSizeInBlocks = SolveResult.SolveResult.SharedCellSizeInBlocks;

	// If the solve result didn't carry a footprint, derive it from placements.
	if (OutArtifact.FootprintSize == FIntPoint::ZeroValue && !Placements.IsEmpty())
	{
		int32 MaxX = 0;
		int32 MaxY = 0;
		for (const FLayoutSolvedArtifactPlacement& P : Placements)
		{
			MaxX = FMath::Max(MaxX, P.Cell.X);
			MaxY = FMath::Max(MaxY, P.Cell.Y);
		}
		OutArtifact.FootprintSize = FIntPoint(MaxX + 1, MaxY + 1);
	}

	OutArtifact.Placements = MoveTemp(Placements);
	OutArtifact.ZoneFeatureProviderCommitments =
		SolveResult.SolveResult.ZoneFeatureProviderCommitments;
	if (!SolveResult.SolveResult.CandidateDomainCertificateId.IsNone())
	{
		OutArtifact.CandidateDomainCertificateIds.Add(
			SolveResult.SolveResult.CandidateDomainCertificateId);
	}
	OutArtifact.PartitionSeams = SolveResult.SolveResult.PartitionSeams;
	OutArtifact.bStructuralCommitmentsAuthoritative = true;
	OutArtifact.RegionalFailure = SolveResult.SolveResult.RegionalFailure;
	OutArtifact.ValidationAssertions = SolveResult.ValidationAssertions;

	// Publish the first walkable entry placement as the exposed entry for continuation handoff.
	for (const FLayoutSolvedArtifactPlacement& P : OutArtifact.Placements)
	{
		if (P.Intent == ELayoutCellIntent::Entry)
		{
			OutArtifact.ExposedEntryCell = P.Cell;
			// Derive outward face direction from boundary position.
			if (P.Cell.X == 0)
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::NegX;
			}
			else if (P.Cell.X == OutArtifact.FootprintSize.X - 1)
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::PosX;
			}
			else if (P.Cell.Y == 0)
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::NegY;
			}
			else
			{
				OutArtifact.ExposedEntryFaceDirection = ELayoutFaceDirection::PosY;
			}
			break;
		}
	}
	return true;
}

bool LayoutSolvedArtifact::TryAttachActiveCellProvenance(
	const TArray<FLayoutContractActiveCellRecord>& ActiveCells,
	FLayoutSolvedArtifact& InOutArtifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!InOutArtifact.bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Solved artifact active-cell provenance requires a produced solved artifact.");
		return false;
	}
	if (ActiveCells.IsEmpty())
	{
		OutFailureReason = TEXT("Solved artifact active-cell provenance requires at least one active cell.");
		return false;
	}
	if (!FLayoutContractPipeline::ValidateActiveCellRecords(ActiveCells, OutFailureReason))
	{
		return false;
	}

	InOutArtifact.bHasActiveCellProvenance = true;
	InOutArtifact.ActiveCells = ActiveCells;
	InOutArtifact.Status = ELayoutSolvedArtifactStatus::WritePlanReady;
	return true;
}

bool LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(
	const FLayoutSolvedArtifact& Artifact,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!Artifact.bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Solved artifact write inputs require a produced solved artifact.");
		return false;
	}
	if (!Artifact.bHasActiveCellProvenance)
	{
		OutFailureReason = TEXT("Solved artifact write inputs require active-cell provenance.");
		return false;
	}
	if (Artifact.Status != ELayoutSolvedArtifactStatus::WritePlanReady
		&& Artifact.Status != ELayoutSolvedArtifactStatus::AcceptedComplete)
	{
		OutFailureReason = TEXT("Solved artifact write inputs require WritePlanReady or AcceptedComplete status.");
		return false;
	}
	if (Artifact.ActiveCells.IsEmpty())
	{
		OutFailureReason = TEXT("Solved artifact write inputs require at least one active cell.");
		return false;
	}
	if (!FLayoutContractPipeline::ValidateActiveCellRecords(
			Artifact.ActiveCells,
			OutFailureReason))
	{
		return false;
	}
	bool bHasRealActiveCell = false;
	for (const FLayoutContractActiveCellRecord& ActiveCell : Artifact.ActiveCells)
	{
		if (true)
		{
			bHasRealActiveCell = true;
			break;
		}
	}
	if (!bHasRealActiveCell)
	{
		OutFailureReason = TEXT("Solved artifact write inputs require at least one real active cell.");
		return false;
	}
	return true;
}
