// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Runtime/LayoutRealizationWritePlan.h"

#include "ChunkWorld/ChunkWorldCore.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Runtime/LayoutCellWorldTransform.h"

namespace
{
	FLayoutId BuildWritePlanId(
		const ELayoutRealizationWritePlanSource Source,
		const FLayoutId SolvedArtifactId,
		const FLayoutId FrozenTerrainContractId)
	{
		const TCHAR* const SourceName = Source == ELayoutRealizationWritePlanSource::Site
			? TEXT("Site")
			: TEXT("Connector");
		return FLayoutId(*FString::Printf(
			TEXT("RealizationWritePlan.%s.%s.%s"),
			SourceName,
			*SolvedArtifactId.ToString(),
			*FrozenTerrainContractId.ToString()));
	}

	int32 BuildWritePlanHash(
		const FLayoutRealizationWritePlan& WritePlan)
	{
		uint32 Hash = GetTypeHash(WritePlan.WritePlanId);
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.TerrainWrites.FrozenTerrainContractId));
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.TemplatePlacements.SolvedArtifactId));
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.TerrainWrites.TerrainWriteCount));
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.TemplatePlacements.PlacementCount));
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.ChunkWriteBatch.PassOrder.Num()));
		for (const ELayoutRealizationChunkWritePass Pass : WritePlan.ChunkWriteBatch.PassOrder)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(Pass)));
		}
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.ChunkWriteBatch.TerrainWriteCount));
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.ChunkWriteBatch.TemplatePlacementCount));
		Hash = HashCombineFast(Hash, GetTypeHash(WritePlan.ActiveCells.Num()));
		// Every accepted anchor is frozen contract authority, including origin cells.
		for (const FLayoutRealizationTemplatePlacementEntry& Entry : WritePlan.TemplatePlacements.Entries)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(Entry.AcceptedAnchorBlockWorldPos));
		}
		for (const FLayoutRealizationReservedOpenClearCell& ClearCell : WritePlan.TerrainWrites.Excavation.ReservedOpenClearCells)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(ClearCell.ReservationId));
			Hash = HashCombineFast(Hash, GetTypeHash(ClearCell.Cell));
			Hash = HashCombineFast(Hash, GetTypeHash(ClearCell.VolumeMinBlockWorldPos));
			Hash = HashCombineFast(Hash, GetTypeHash(ClearCell.VolumeMaxBlockWorldPos));
		}
		for (const FLayoutRealizationTerrainWriteEntry& ClearWrite : WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites)
		{
			Hash = HashCombineFast(Hash, GetTypeHash(ClearWrite.BlockWorldPos));
			Hash = HashCombineFast(Hash, GetTypeHash(ClearWrite.Material));
			Hash = HashCombineFast(Hash, GetTypeHash(static_cast<uint8>(ClearWrite.SourceContract)));
		}
		return static_cast<int32>(Hash & 0x7fffffff);
	}

	bool IsExcavationOrClearanceContract(const ELayoutFrozenTerrainCellContract Contract)
	{
		return Contract == ELayoutFrozenTerrainCellContract::FlatClearance
			|| Contract == ELayoutFrozenTerrainCellContract::TunnelExcavation;
	}

	bool IsSupportContract(const ELayoutFrozenTerrainCellContract Contract)
	{
		return Contract == ELayoutFrozenTerrainCellContract::Active
			|| Contract == ELayoutFrozenTerrainCellContract::BridgeSpan;
	}

	bool HasCellContractAuthority(
		const TArray<FLayoutTerrainCellContractRecord>& CellContracts,
		const ELayoutFrozenTerrainCellContract SourceContract)
	{
		for (const FLayoutTerrainCellContractRecord& CellContract : CellContracts)
		{
			if (CellContract.Contract == SourceContract)
			{
				return true;
			}
		}
		return false;
	}

	bool BuildTypedTerrainSubplans(
		const int32 StructuralAlignmentLevel,
		const int32 TemplatePlacementZOffsetBlocks,
		const FLayoutSolveResult& SolveResult,
		const bool bRequireSolveReservationMatch,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FLayoutRealizationTerrainWriteSubplan& OutTerrainSubplan,
		FString& OutFailureReason)
	{
		OutTerrainSubplan = FLayoutRealizationTerrainWriteSubplan();
		OutTerrainSubplan.FrozenTerrainContractId = FrozenTerrainContract.ContractId;
		OutTerrainSubplan.CellContractCount = FrozenTerrainContract.CellContracts.Num();
		OutTerrainSubplan.StructuralAlignmentLevel = StructuralAlignmentLevel;
		OutTerrainSubplan.TemplatePlacementZOffsetBlocks = TemplatePlacementZOffsetBlocks;
		OutTerrainSubplan.TerrainWriteCount = 0;

		// Local project change: untouched terrain-backed residuals retain active
		// authority for topology but suppress every frozen terrain write in their cell volume.
		// Bounds scan is O(writes * residuals); add source-cell identity to frozen writes if replay profiling shows this dominates.
		TArray<TPair<FIntVector, FIntVector>> TerrainResidualNoWriteBounds;
		for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
		{
			if (ResidualCell.Source != ELayoutResidualCellSource::TerrainBackedRule)
			{
				continue;
			}
			const FIntVector CellMin = LayoutCellWorldTransform::ResolveAcceptedCellBase(
				FrozenTerrainContract,
				ResidualCell.Cell,
				StructuralAlignmentLevel,
				TemplatePlacementZOffsetBlocks);
			TerrainResidualNoWriteBounds.Emplace(
				CellMin,
				CellMin + FrozenTerrainContract.SharedCellSizeInBlocks - FIntVector(1, 1, 1));
		}

		TSet<FIntVector> RealActiveCells;
		for (const FLayoutContractActiveCellRecord& ActiveCell : FrozenTerrainContract.ActiveCells)
		{
			if (true)
			{
				RealActiveCells.Add(ActiveCell.Cell);
			}
		}
		if (!FrozenTerrainContract.TerrainWrites.IsEmpty() && RealActiveCells.IsEmpty())
		{
			OutFailureReason = TEXT("Realization write plan terrain writes require at least one real active-cell authority.");
			return false;
		}
		TSet<ELayoutFrozenTerrainCellContract> ExcavationContractsWithBounds;
		for (const FLayoutTerrainCellContractRecord& CellContract : FrozenTerrainContract.CellContracts)
		{
			if (!RealActiveCells.Contains(CellContract.Cell))
			{
				OutFailureReason = TEXT("Realization write plan terrain cell contracts require real active-cell authority.");
				return false;
			}
			if (CellContract.bHasFoundationFillEvidence)
			{
				if (CellContract.Contract != ELayoutFrozenTerrainCellContract::Active)
				{
					OutFailureReason = TEXT("Realization write plan foundation-fill evidence requires an active cell contract.");
					return false;
				}
				if (CellContract.RequiredFoundationDepth <= 0)
				{
					OutFailureReason = TEXT("Realization write plan foundation-fill evidence requires positive bounded depth.");
					return false;
				}
				OutTerrainSubplan.Support.FoundationFillCells.Add(CellContract.Cell);
			}
			if (CellContract.bHasRampTransitionEvidence)
			{
				if (CellContract.Contract != ELayoutFrozenTerrainCellContract::Active)
				{
					OutFailureReason = TEXT("Realization write plan ramp-transition evidence requires an active cell contract.");
					return false;
				}
				OutTerrainSubplan.Support.RampTransitionCells.Add(CellContract.Cell);
			}
			if (CellContract.bHasClearanceEvidence)
			{
				// Adapter uses Active when support and clearance evidence share one cell.
				if (!IsExcavationOrClearanceContract(CellContract.Contract)
					&& !(CellContract.Contract == ELayoutFrozenTerrainCellContract::Active
						&& (CellContract.bHasFoundationFillEvidence
							|| CellContract.bHasRampTransitionEvidence
							|| CellContract.bHasBridgeSupportEvidence)))
				{
					OutFailureReason = TEXT("Realization write plan clearance evidence requires an excavation or clearance cell contract.");
					return false;
				}
				if (CellContract.bHasExcavationOverlapZ)
				{
					OutFailureReason = TEXT("Realization write plan rejected mixed clearance and excavation overlap evidence.");
					return false;
				}
				OutTerrainSubplan.Excavation.ClearanceCells.Add(CellContract.Cell);
			}
			if (CellContract.bHasBridgeSupportEvidence)
			{
				if (CellContract.Contract != ELayoutFrozenTerrainCellContract::BridgeSpan)
				{
					OutFailureReason = TEXT("Realization write plan bridge-support evidence requires a bridge-span cell contract.");
					return false;
				}
				OutTerrainSubplan.Support.BridgeSupportCells.Add(CellContract.Cell);
			}
			if (CellContract.bHasExcavationOverlapZ)
			{
				// Allows Active contract for mixed ramp+excavation cells — evidence flags are authoritative.
				if (!IsExcavationOrClearanceContract(CellContract.Contract)
					&& !(CellContract.Contract == ELayoutFrozenTerrainCellContract::Active
						&& (CellContract.bHasRampTransitionEvidence || CellContract.bHasBridgeSupportEvidence)))
				{
					OutFailureReason = TEXT("Realization write plan excavation overlap bounds require an excavation or clearance cell contract.");
					return false;
				}
				if (CellContract.ExcavationOverlapMinLocalZ.Value > CellContract.ExcavationOverlapMaxLocalZ.Value)
				{
					OutFailureReason = TEXT("Realization write plan rejected invalid excavation overlap bounds.");
					return false;
				}

				ExcavationContractsWithBounds.Add(CellContract.Contract);
				FLayoutRealizationExcavationCellBounds& OverlapBounds = OutTerrainSubplan.Excavation.OverlapBounds.AddDefaulted_GetRef();
				OverlapBounds.Cell = CellContract.Cell;
				OverlapBounds.OverlapMinLocalZ = CellContract.ExcavationOverlapMinLocalZ;
				OverlapBounds.OverlapMaxLocalZ = CellContract.ExcavationOverlapMaxLocalZ;
			}
		}

		const bool bHasExplicitActiveSupportEvidence = !OutTerrainSubplan.Support.FoundationFillCells.IsEmpty()
			|| !OutTerrainSubplan.Support.RampTransitionCells.IsEmpty();
		const bool bHasExplicitBridgeSupportEvidence = !OutTerrainSubplan.Support.BridgeSupportCells.IsEmpty();

		for (const FLayoutFrozenTerrainWriteRecord& TerrainWrite : FrozenTerrainContract.TerrainWrites)
		{
			const bool bTouchesUntouchedTerrainResidual = TerrainResidualNoWriteBounds.ContainsByPredicate(
				[&TerrainWrite](const TPair<FIntVector, FIntVector>& Bounds)
				{
					return TerrainWrite.BlockWorldPos.X >= Bounds.Key.X && TerrainWrite.BlockWorldPos.X <= Bounds.Value.X
						&& TerrainWrite.BlockWorldPos.Y >= Bounds.Key.Y && TerrainWrite.BlockWorldPos.Y <= Bounds.Value.Y
						&& TerrainWrite.BlockWorldPos.Z >= Bounds.Key.Z && TerrainWrite.BlockWorldPos.Z <= Bounds.Value.Z;
				});
			if (bTouchesUntouchedTerrainResidual)
			{
				continue;
			}
			if (!HasCellContractAuthority(FrozenTerrainContract.CellContracts, TerrainWrite.SourceContract))
			{
				OutFailureReason = TEXT("Realization write plan rejected a terrain write without matching cell-contract authority.");
				return false;
			}

			FLayoutRealizationTerrainWriteEntry WriteEntry;
			WriteEntry.BlockWorldPos = TerrainWrite.BlockWorldPos;
			WriteEntry.Material = TerrainWrite.Material;
			WriteEntry.bResolveMaterialFromTerrain = TerrainWrite.bResolveMaterialFromTerrain;
			WriteEntry.MaterialSourceBlockWorldPos = TerrainWrite.MaterialSourceBlockWorldPos;
			WriteEntry.MaterialSourceSearchDepthBlocks = TerrainWrite.MaterialSourceSearchDepthBlocks;
			WriteEntry.SourceContract = TerrainWrite.SourceContract;

			if (IsExcavationOrClearanceContract(TerrainWrite.SourceContract))
			{
				if (!ExcavationContractsWithBounds.Contains(TerrainWrite.SourceContract)
					&& OutTerrainSubplan.Excavation.ClearanceCells.IsEmpty())
				{
					OutFailureReason = TEXT("Realization write plan excavation or clearance writes require local overlap bounds or clearance evidence.");
					return false;
				}
				OutTerrainSubplan.Excavation.Writes.Add(WriteEntry);
			}
			else if (TerrainWrite.SourceContract == ELayoutFrozenTerrainCellContract::Active)
			{
				if (!bHasExplicitActiveSupportEvidence)
				{
					OutFailureReason = TEXT("Realization write plan active support writes require foundation-fill or ramp-transition evidence.");
					return false;
				}
				OutTerrainSubplan.Support.FoundationRampWrites.Add(WriteEntry);
			}
			else if (TerrainWrite.SourceContract == ELayoutFrozenTerrainCellContract::BridgeSpan)
			{
				if (!bHasExplicitBridgeSupportEvidence)
				{
					OutFailureReason = TEXT("Realization write plan bridge support writes require bridge-support evidence.");
					return false;
				}
				OutTerrainSubplan.Support.BridgeSupportWrites.Add(WriteEntry);
			}
			else if (!IsSupportContract(TerrainWrite.SourceContract))
			{
				OutFailureReason = TEXT("Realization write plan rejected unsupported terrain write source contract.");
				return false;
			}
			++OutTerrainSubplan.TerrainWriteCount;
		}

		TSet<FIntVector> PlannedCells;
		for (const FLayoutPlannedCell& PlannedCell : SolveResult.PlannedCells)
		{
			PlannedCells.Add(PlannedCell.Cell);
		}
		TSet<FIntVector> PlacementCells;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			PlacementCells.Add(Placement.Cell);
		}
		TSet<FLayoutId> SeenReservationIds;
		TSet<FIntVector> SeenReservationCells;
		TSet<FIntVector> SeenClearWritePositions;
		const int64 ExpectedVolumeSize = static_cast<int64>(FrozenTerrainContract.SharedCellSizeInBlocks.X)
			* FrozenTerrainContract.SharedCellSizeInBlocks.Y
			* FrozenTerrainContract.SharedCellSizeInBlocks.Z;
		const bool bHasClearReservation = FrozenTerrainContract.ReservedOpenTerrainReservations.ContainsByPredicate([](const FLayoutCellReservationRecord& Reservation)
		{
			return Reservation.TerrainBehavior == ELayoutReservedOpenTerrainBehavior::ClearReservedCell;
		});
		if (bHasClearReservation
			&& (FrozenTerrainContract.SharedCellSizeInBlocks.X <= 0
				|| FrozenTerrainContract.SharedCellSizeInBlocks.Y <= 0
				|| FrozenTerrainContract.SharedCellSizeInBlocks.Z <= 0
				|| ExpectedVolumeSize > MAX_int32))
		{
			OutFailureReason = TEXT("Realization write plan reserved-open clear volumes require positive bounded shared-cell dimensions.");
			return false;
		}
		for (const FLayoutCellReservationRecord& Reservation : FrozenTerrainContract.ReservedOpenTerrainReservations)
		{
			if (Reservation.ReservationKind != ELayoutCellReservationKind::ReservedEmpty)
			{
				OutFailureReason = TEXT("Realization write plan reserved-open terrain authority contains a non-empty reservation.");
				return false;
			}
			if (Reservation.ReservationId.IsNone()
				|| SeenReservationIds.Contains(Reservation.ReservationId)
				|| SeenReservationCells.Contains(Reservation.Cell))
			{
				OutFailureReason = TEXT("Realization write plan rejected duplicate or unnamed reserved-open terrain authority.");
				return false;
			}
			SeenReservationIds.Add(Reservation.ReservationId);
			SeenReservationCells.Add(Reservation.Cell);
			if (PlannedCells.Contains(Reservation.Cell)
				|| PlacementCells.Contains(Reservation.Cell)
				|| RealActiveCells.Contains(Reservation.Cell))
			{
				OutFailureReason = TEXT("Realization write plan reserved-open terrain authority must reference only removed, inactive cells.");
				return false;
			}
			if (bRequireSolveReservationMatch
				&& !SolveResult.CompiledReservations.ContainsByPredicate([&Reservation](const FLayoutCellReservationRecord& SolveReservation)
				{
					return SolveReservation.ReservationId == Reservation.ReservationId
						&& SolveReservation.Cell == Reservation.Cell
						&& SolveReservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty
						&& SolveReservation.TerrainBehavior == Reservation.TerrainBehavior;
				}))
			{
				OutFailureReason = TEXT("Realization write plan reserved-open terrain authority is missing from the solved reservation record.");
				return false;
			}
			if (Reservation.TerrainBehavior == ELayoutReservedOpenTerrainBehavior::LeaveExistingTerrain)
			{
				continue;
			}
			if (Reservation.TerrainBehavior != ELayoutReservedOpenTerrainBehavior::ClearReservedCell)
			{
				OutFailureReason = TEXT("Realization write plan rejected unknown reserved-open terrain behavior.");
				return false;
			}

			FLayoutRealizationReservedOpenClearCell& ClearCell = OutTerrainSubplan.Excavation.ReservedOpenClearCells.AddDefaulted_GetRef();
			ClearCell.ReservationId = Reservation.ReservationId;
			ClearCell.Cell = Reservation.Cell;
			ClearCell.VolumeMinBlockWorldPos = LayoutCellWorldTransform::ResolveAcceptedCellBase(
				FrozenTerrainContract,
				Reservation.Cell,
				StructuralAlignmentLevel,
				TemplatePlacementZOffsetBlocks);
			ClearCell.VolumeMaxBlockWorldPos = ClearCell.VolumeMinBlockWorldPos + FrozenTerrainContract.SharedCellSizeInBlocks - FIntVector(1, 1, 1);
			int32 GeneratedVolumeSize = 0;
			for (int32 Z = ClearCell.VolumeMinBlockWorldPos.Z; Z <= ClearCell.VolumeMaxBlockWorldPos.Z; ++Z)
			{
				for (int32 Y = ClearCell.VolumeMinBlockWorldPos.Y; Y <= ClearCell.VolumeMaxBlockWorldPos.Y; ++Y)
				{
					for (int32 X = ClearCell.VolumeMinBlockWorldPos.X; X <= ClearCell.VolumeMaxBlockWorldPos.X; ++X)
					{
						const FIntVector BlockWorldPos(X, Y, Z);
						if (SeenClearWritePositions.Contains(BlockWorldPos))
						{
							OutFailureReason = TEXT("Realization write plan rejected overlapping reserved-open clear volumes.");
							return false;
						}
						SeenClearWritePositions.Add(BlockWorldPos);
						FLayoutRealizationTerrainWriteEntry& ClearWrite = OutTerrainSubplan.Excavation.ReservedOpenClearWrites.AddDefaulted_GetRef();
						ClearWrite.BlockWorldPos = BlockWorldPos;
						ClearWrite.Material = EmptyMaterial;
						ClearWrite.SourceContract = ELayoutFrozenTerrainCellContract::Removed;
						++GeneratedVolumeSize;
					}
				}
			}
			if (GeneratedVolumeSize != ExpectedVolumeSize)
			{
				OutFailureReason = TEXT("Realization write plan reserved-open clear volume count does not match shared-cell dimensions.");
				return false;
			}
		}
		OutTerrainSubplan.TerrainWriteCount += OutTerrainSubplan.Excavation.ReservedOpenClearWrites.Num();
		return true;
	}

	bool BuildTemplatePlacementSubplan(
		const FLayoutId SolvedArtifactId,
		const FLayoutSolveResult& SolveResult,
		FLayoutRealizationTemplatePlacementSubplan& OutTemplateSubplan,
		FString& OutFailureReason)
	{
		OutTemplateSubplan = FLayoutRealizationTemplatePlacementSubplan();
		OutTemplateSubplan.SolvedArtifactId = SolvedArtifactId;
		OutTemplateSubplan.PlacementCount = SolveResult.Placements.Num();

		TSet<FString> SeenPlacementKeys;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			if (Placement.ModuleSnapshotId.IsNone())
			{
				OutFailureReason = TEXT("Realization write plan requires template placement module snapshot ids.");
				return false;
			}
			if (Placement.TemplatePath.IsNull() && Placement.OccupiedLocalCells.IsEmpty())
			{
				OutFailureReason = TEXT("Realization write plan requires template placement soft paths or composite occupied-cell snapshots.");
				return false;
			}
			if (Placement.OccupiedLocalCells.Num() > 1
				&& Placement.LocalCellFaceRules.Num() != Placement.OccupiedLocalCells.Num())
			{
				OutFailureReason = TEXT("Realization write plan requires complete frozen composite local-cell descriptors.");
				return false;
			}
			if (Placement.YawRotationSteps < 0 || Placement.YawRotationSteps > 3)
			{
				OutFailureReason = TEXT("Realization write plan rejected invalid template placement yaw.");
				return false;
			}

			const FString PlacementKey = FString::Printf(
				TEXT("%s|%s|%d|%s"),
				*Placement.Cell.ToString(),
				*Placement.ModuleSnapshotId.ToString(),
				Placement.YawRotationSteps,
				*Placement.TemplatePath.ToString());
			if (SeenPlacementKeys.Contains(PlacementKey))
			{
				OutFailureReason = TEXT("Realization write plan rejected duplicate template placement entries.");
				return false;
			}
			SeenPlacementKeys.Add(PlacementKey);

			FLayoutRealizationTemplatePlacementEntry Entry;
			Entry.Cell = Placement.Cell;
			Entry.SourceContentEntryId = Placement.SourceContentEntryId;
			Entry.ModuleSnapshotId = Placement.ModuleSnapshotId;
			Entry.TemplatePath = Placement.TemplatePath;
			Entry.YawRotationSteps = Placement.YawRotationSteps;
			Entry.LocalCellDescriptors = Placement.LocalCellFaceRules;
			OutTemplateSubplan.Entries.Add(Entry);
		}

		return true;
	}

	FLayoutRealizationChunkWriteBatch BuildChunkWriteBatch(
		const FLayoutRealizationTerrainWriteSubplan& TerrainSubplan,
		const FLayoutRealizationTemplatePlacementSubplan& TemplateSubplan)
	{
		FLayoutRealizationChunkWriteBatch ChunkWriteBatch;
		ChunkWriteBatch.PassOrder.Add(ELayoutRealizationChunkWritePass::ExcavationClearance);
		ChunkWriteBatch.PassOrder.Add(ELayoutRealizationChunkWritePass::FoundationRampBridgeSupport);
		ChunkWriteBatch.PassOrder.Add(ELayoutRealizationChunkWritePass::TemplatePlacement);
		ChunkWriteBatch.TerrainWriteCount = TerrainSubplan.TerrainWriteCount;
		ChunkWriteBatch.TemplatePlacementCount = TemplateSubplan.PlacementCount;
		return ChunkWriteBatch;
	}

	bool ValidateAcceptedSolveMetadata(
		const ELayoutRealizationWritePlanSource Source,
		const FLayoutId SolvedArtifactId,
		const int32 SolvedArtifactActiveCellCount,
		const FLayoutSolveResult& SolveResult,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FLayoutRealizationTerrainWriteSubplan& OutTerrainSubplan,
		FLayoutRealizationTemplatePlacementSubplan& OutTemplateSubplan,
		FString& OutFailureReason)
	{
		if (SolvedArtifactId.IsNone())
		{
			OutFailureReason = TEXT("Realization write plan requires a solved artifact id.");
			return false;
		}
		if (SolveResult.Placements.IsEmpty())
		{
			OutFailureReason = TEXT("Realization write plan requires solved placements.");
			return false;
		}
		if (FrozenTerrainContract.ContractId.IsNone())
		{
			OutFailureReason = TEXT("Realization write plan requires a frozen terrain contract id.");
			return false;
		}
		if (SolvedArtifactActiveCellCount <= 0 || SolvedArtifactActiveCellCount != FrozenTerrainContract.ActiveCells.Num())
		{
			OutFailureReason = TEXT("Realization write plan rejected stale solved artifact active-cell metadata.");
			return false;
		}

		if (!FLayoutContractPipeline::ValidateActiveCellRecords(FrozenTerrainContract.ActiveCells, OutFailureReason))
		{
			return false;
		}
		if (!LayoutRealizationWritePlan::ValidateSolveResultWriteAuthority(
				SolveResult,
				FrozenTerrainContract.ActiveCells,
				TEXT("Realization write plan"),
				OutFailureReason))
		{
			return false;
		}
		if (!FrozenTerrainContract.TerrainWrites.IsEmpty() && FrozenTerrainContract.CellContracts.IsEmpty())
		{
			OutFailureReason = TEXT("Realization write plan requires explicit terrain cell contracts before terrain writes.");
			return false;
		}
		const int32 StructuralAlignmentLevel =
			Source == ELayoutRealizationWritePlanSource::Connector
				&& SolveResult.ResolvedTerrainAlignmentLevel != INDEX_NONE
				? SolveResult.ResolvedTerrainAlignmentLevel
				: 0;
		if (!BuildTypedTerrainSubplans(
				StructuralAlignmentLevel,
				SolveResult.TemplatePlacementZOffsetBlocks,
				SolveResult,
				true,
				FrozenTerrainContract,
				OutTerrainSubplan,
				OutFailureReason))
		{
			return false;
		}
		return BuildTemplatePlacementSubplan(SolvedArtifactId, SolveResult, OutTemplateSubplan, OutFailureReason);
	}
}

bool LayoutRealizationWritePlan::ValidateAcceptedSolvePayloadForRealizationInputs(
	const FLayoutPlannedSiteAcceptedSolvePayload& AcceptedSolvePayload,
	FString& OutFailureReason)
{
	return LayoutRealizationWritePlan::ValidateAcceptedSolvedArtifactMetadataForRealizationInputs(
		AcceptedSolvePayload.SolvedArtifactId,
		AcceptedSolvePayload.SolvedArtifactActiveCellCount,
		AcceptedSolvePayload.SolveResult,
		AcceptedSolvePayload.FrozenTerrainContract,
		TEXT("Accepted planned-site"),
		OutFailureReason);
}

bool LayoutRealizationWritePlan::ValidateAcceptedSolvedArtifactMetadataForRealizationInputs(
	const FLayoutId SolvedArtifactId,
	const int32 SolvedArtifactActiveCellCount,
	const FLayoutSolveResult& SolveResult,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	const TCHAR* const ContextName,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!SolveResult.bSucceeded)
	{
		OutFailureReason = FString::Printf(TEXT("%s realization requires a successful solve result."), ContextName);
		return false;
	}
	if (SolvedArtifactId.IsNone())
	{
		OutFailureReason = FString::Printf(TEXT("%s realization requires a solved artifact id."), ContextName);
		return false;
	}
	if (SolveResult.Placements.IsEmpty())
	{
		OutFailureReason = FString::Printf(TEXT("%s realization requires solved artifact placements."), ContextName);
		return false;
	}
	if (SolvedArtifactActiveCellCount <= 0)
	{
		OutFailureReason = FString::Printf(TEXT("%s realization requires solved artifact active-cell provenance."), ContextName);
		return false;
	}
	if (SolvedArtifactActiveCellCount != FrozenTerrainContract.ActiveCells.Num())
	{
		OutFailureReason = FString::Printf(TEXT("%s realization rejected stale solved artifact active-cell count."), ContextName);
		return false;
	}

	if (!FLayoutContractPipeline::ValidateActiveCellRecords(FrozenTerrainContract.ActiveCells, OutFailureReason))
	{
		return false;
	}
	return LayoutRealizationWritePlan::ValidateSolveResultWriteAuthority(
		SolveResult,
		FrozenTerrainContract.ActiveCells,
		ContextName,
		OutFailureReason);
}

bool LayoutRealizationWritePlan::ValidateSolveResultWriteAuthority(
	const FLayoutSolveResult& SolveResult,
	const TArray<FLayoutContractActiveCellRecord>& ActiveCells,
	const TCHAR* const ContextName,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();

	TSet<FIntVector> RealActiveCells;
	for (const FLayoutContractActiveCellRecord& ActiveCell : ActiveCells)
	{
		RealActiveCells.Add(ActiveCell.Cell);
	}
	if (RealActiveCells.IsEmpty())
	{
		OutFailureReason = FString::Printf(TEXT("%s requires at least one real active cell."), ContextName);
		return false;
	}

	auto RequireRealAuthority = [&RealActiveCells, ContextName, &OutFailureReason](const FIntVector& Cell, const TCHAR* AuthorityName)
	{
		if (!RealActiveCells.Contains(Cell))
		{
			OutFailureReason = FString::Printf(TEXT("%s requires %s cells to be backed by real active-cell authority."), ContextName, AuthorityName);
			return false;
		}
		return true;
	};

	for (const FLayoutClosureCoverageSegmentRecord& ClosureSegment : SolveResult.ClosureSegments)
	{
		if (!RequireRealAuthority(ClosureSegment.Cell, TEXT("exterior closure segment")))
		{
			return false;
		}
	}
	for (const FLayoutDroppedOptionalChildRecord& DroppedChild : SolveResult.DroppedOptionalChildren)
	{
		for (const FLayoutPlannedCell& DroppedCell : DroppedChild.DroppedPlannedCells)
		{
			if (!RequireRealAuthority(DroppedCell.Cell, TEXT("dropped child host")))
			{
				return false;
			}
		}
	}
	for (const FLayoutSparsePlacementCommitment& SparsePlacement : SolveResult.SparsePlacementCommitments)
	{
		if (!RequireRealAuthority(SparsePlacement.Cell, TEXT("sparse placement")))
		{
			return false;
		}
	}
	for (const FLayoutCellReservationRecord& Reservation : SolveResult.CompiledReservations)
	{
		if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
		{
			continue;
		}
		if (!RequireRealAuthority(Reservation.Cell, TEXT("compiled reservation")))
		{
			return false;
		}
	}
	for (const FLayoutRouteConstraintRecord& RouteConstraint : SolveResult.RouteConstraints)
	{
		if (!RequireRealAuthority(RouteConstraint.Cell, TEXT("route constraint")))
		{
			return false;
		}
	}
	for (const FLayoutRouteConstraintRecord& RouteConstraint : SolveResult.RequestOwnedRequiredRouteConstraints)
	{
		if (!RealActiveCells.Contains(RouteConstraint.Cell))
		{
			OutFailureReason = FString::Printf(TEXT("%s requires request-owned route constraint cells to be backed by real active-cell authority."), ContextName);
			return false;
		}
	}
	for (const FLayoutResidualCellRecord& ResidualCell : SolveResult.ResidualUnoccupiedCells)
	{
		if (!RequireRealAuthority(ResidualCell.Cell, TEXT("residual")))
		{
			return false;
		}
	}

	return true;
}

bool LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
	const ELayoutRealizationWritePlanSource Source,
	const FLayoutId SolvedArtifactId,
	const int32 SolvedArtifactActiveCellCount,
	const FLayoutSolveResult& SolveResult,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FLayoutRealizationWritePlan& OutWritePlan,
	FString& OutFailureReason)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Realization_Prep, STAT_PorismLayout_RealizationPrep);
	OutWritePlan = FLayoutRealizationWritePlan();
	OutFailureReason.Reset();

	FLayoutRealizationTerrainWriteSubplan TerrainSubplan;
	FLayoutRealizationTemplatePlacementSubplan TemplateSubplan;
	if (!ValidateAcceptedSolveMetadata(
			Source,
			SolvedArtifactId,
			SolvedArtifactActiveCellCount,
			SolveResult,
			FrozenTerrainContract,
			TerrainSubplan,
			TemplateSubplan,
			OutFailureReason))
	{
		return false;
	}

	OutWritePlan.Source = Source;
	OutWritePlan.WritePlanId = BuildWritePlanId(Source, SolvedArtifactId, FrozenTerrainContract.ContractId);
	OutWritePlan.TerrainWrites = TerrainSubplan;
	OutWritePlan.TemplatePlacements = TemplateSubplan;

	// Every frozen contract owns realization anchors. StageMap replaces only the
	// base Z for stepped columns; non-stepped, bridge, and tunnel connectors use
	// the frozen footprint base through this same write-plan path.
	if (FrozenTerrainContract.SharedCellSizeInBlocks != FIntVector::ZeroValue)
	{
		const int32 StructuralAlignmentLevel =
			Source == ELayoutRealizationWritePlanSource::Connector
				&& SolveResult.ResolvedTerrainAlignmentLevel != INDEX_NONE
				? SolveResult.ResolvedTerrainAlignmentLevel
				: 0;
		for (FLayoutRealizationTemplatePlacementEntry& Entry : OutWritePlan.TemplatePlacements.Entries)
		{
			Entry.AcceptedAnchorBlockWorldPos = LayoutCellWorldTransform::ResolveAcceptedCellBase(
				FrozenTerrainContract,
				Entry.Cell,
				StructuralAlignmentLevel,
				SolveResult.TemplatePlacementZOffsetBlocks);
		}
	}

	OutWritePlan.ChunkWriteBatch = BuildChunkWriteBatch(TerrainSubplan, TemplateSubplan);
	OutWritePlan.ActiveCells = FrozenTerrainContract.ActiveCells;
	OutWritePlan.WritePlanHash = BuildWritePlanHash(OutWritePlan);
	if (!LayoutRealizationWritePlan::ValidateChunkWriteBatch(OutWritePlan, OutFailureReason))
	{
		OutWritePlan = FLayoutRealizationWritePlan();
		return false;
	}
	return true;
}

bool LayoutRealizationWritePlan::ValidateChunkWriteBatch(
	const FLayoutRealizationWritePlan& WritePlan,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();

	if (WritePlan.ChunkWriteBatch.PassOrder.Num() != 3
		|| WritePlan.ChunkWriteBatch.PassOrder[0] != ELayoutRealizationChunkWritePass::ExcavationClearance
		|| WritePlan.ChunkWriteBatch.PassOrder[1] != ELayoutRealizationChunkWritePass::FoundationRampBridgeSupport
		|| WritePlan.ChunkWriteBatch.PassOrder[2] != ELayoutRealizationChunkWritePass::TemplatePlacement)
	{
		OutFailureReason = TEXT("Realization write plan rejected invalid chunk write-batch pass order.");
		return false;
	}
	if (WritePlan.ChunkWriteBatch.TerrainWriteCount != WritePlan.TerrainWrites.TerrainWriteCount)
	{
		OutFailureReason = TEXT("Realization write plan rejected stale chunk terrain write count.");
		return false;
	}
	if (WritePlan.ChunkWriteBatch.TemplatePlacementCount != WritePlan.TemplatePlacements.PlacementCount)
	{
		OutFailureReason = TEXT("Realization write plan rejected stale chunk template placement count.");
		return false;
	}

	return true;
}

namespace
{
	/** Validates terrain replay before low-level writes; production reaches this only through ApplyTerrainWriteReplay. */
	bool ValidateTerrainWriteReplayInternal(
		const FLayoutRealizationWritePlan& WritePlan,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();

	if (!LayoutRealizationWritePlan::ValidateChunkWriteBatch(WritePlan, OutFailureReason))
	{
		return false;
	}
	if (WritePlan.TerrainWrites.FrozenTerrainContractId != FrozenTerrainContract.ContractId)
	{
		OutFailureReason = TEXT("Terrain write replay is not authorized by the realization write plan contract id.");
		return false;
	}
	if (WritePlan.TerrainWrites.CellContractCount != FrozenTerrainContract.CellContracts.Num())
	{
		OutFailureReason = TEXT("Terrain write replay rejected stale terrain write-plan cell-contract count.");
		return false;
	}

	FLayoutRealizationTerrainWriteSubplan RecomputedSubplan;
	if (!BuildTypedTerrainSubplans(
			WritePlan.TerrainWrites.StructuralAlignmentLevel,
			WritePlan.TerrainWrites.TemplatePlacementZOffsetBlocks,
			FLayoutSolveResult(),
			false,
			FrozenTerrainContract,
			RecomputedSubplan,
			OutFailureReason))
	{
		return false;
	}
	auto AreWriteEntriesEqual = [](const FLayoutRealizationTerrainWriteEntry& Left, const FLayoutRealizationTerrainWriteEntry& Right)
	{
		return Left.BlockWorldPos == Right.BlockWorldPos
			&& Left.Material == Right.Material
			&& Left.bResolveMaterialFromTerrain == Right.bResolveMaterialFromTerrain
			&& Left.MaterialSourceBlockWorldPos == Right.MaterialSourceBlockWorldPos
			&& Left.MaterialSourceSearchDepthBlocks == Right.MaterialSourceSearchDepthBlocks
			&& Left.SourceContract == Right.SourceContract;
	};
	auto AreWriteEntryArraysEqual = [&AreWriteEntriesEqual](const TArray<FLayoutRealizationTerrainWriteEntry>& Left, const TArray<FLayoutRealizationTerrainWriteEntry>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!AreWriteEntriesEqual(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	};
	auto AreReservedOpenClearCellsEqual = [](const FLayoutRealizationReservedOpenClearCell& Left, const FLayoutRealizationReservedOpenClearCell& Right)
	{
		return Left.ReservationId == Right.ReservationId
			&& Left.Cell == Right.Cell
			&& Left.VolumeMinBlockWorldPos == Right.VolumeMinBlockWorldPos
			&& Left.VolumeMaxBlockWorldPos == Right.VolumeMaxBlockWorldPos;
	};
	auto AreReservedOpenClearCellArraysEqual = [&AreReservedOpenClearCellsEqual](const TArray<FLayoutRealizationReservedOpenClearCell>& Left, const TArray<FLayoutRealizationReservedOpenClearCell>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!AreReservedOpenClearCellsEqual(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	};
	auto AreOverlapBoundsEqual = [](const FLayoutRealizationExcavationCellBounds& Left, const FLayoutRealizationExcavationCellBounds& Right)
	{
		return Left.Cell == Right.Cell
			&& Left.OverlapMinLocalZ.Value == Right.OverlapMinLocalZ.Value
			&& Left.OverlapMaxLocalZ.Value == Right.OverlapMaxLocalZ.Value;
	};
	auto AreOverlapBoundsArraysEqual = [&AreOverlapBoundsEqual](const TArray<FLayoutRealizationExcavationCellBounds>& Left, const TArray<FLayoutRealizationExcavationCellBounds>& Right)
	{
		if (Left.Num() != Right.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Num(); ++Index)
		{
			if (!AreOverlapBoundsEqual(Left[Index], Right[Index]))
			{
				return false;
			}
		}
		return true;
	};
	auto AreCellArraysEqual = [](const TArray<FIntVector>& Left, const TArray<FIntVector>& Right)
	{
		return Left == Right;
	};

	if (WritePlan.TerrainWrites.TerrainWriteCount != RecomputedSubplan.TerrainWriteCount
		|| WritePlan.TerrainWrites.StructuralAlignmentLevel != RecomputedSubplan.StructuralAlignmentLevel
		|| WritePlan.TerrainWrites.TemplatePlacementZOffsetBlocks != RecomputedSubplan.TemplatePlacementZOffsetBlocks
		|| !AreWriteEntryArraysEqual(WritePlan.TerrainWrites.Excavation.Writes, RecomputedSubplan.Excavation.Writes)
		|| !AreWriteEntryArraysEqual(WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites, RecomputedSubplan.Excavation.ReservedOpenClearWrites)
		|| !AreReservedOpenClearCellArraysEqual(WritePlan.TerrainWrites.Excavation.ReservedOpenClearCells, RecomputedSubplan.Excavation.ReservedOpenClearCells)
		|| !AreWriteEntryArraysEqual(WritePlan.TerrainWrites.Support.FoundationRampWrites, RecomputedSubplan.Support.FoundationRampWrites)
		|| !AreWriteEntryArraysEqual(WritePlan.TerrainWrites.Support.BridgeSupportWrites, RecomputedSubplan.Support.BridgeSupportWrites)
		|| !AreOverlapBoundsArraysEqual(WritePlan.TerrainWrites.Excavation.OverlapBounds, RecomputedSubplan.Excavation.OverlapBounds)
		|| !AreCellArraysEqual(WritePlan.TerrainWrites.Excavation.ClearanceCells, RecomputedSubplan.Excavation.ClearanceCells)
		|| !AreCellArraysEqual(WritePlan.TerrainWrites.Support.FoundationFillCells, RecomputedSubplan.Support.FoundationFillCells)
		|| !AreCellArraysEqual(WritePlan.TerrainWrites.Support.RampTransitionCells, RecomputedSubplan.Support.RampTransitionCells)
		|| !AreCellArraysEqual(WritePlan.TerrainWrites.Support.BridgeSupportCells, RecomputedSubplan.Support.BridgeSupportCells))
	{
		OutFailureReason = TEXT("Terrain write replay rejected stale typed terrain subplan entries.");
		return false;
	}

		return true;
	}
}

#if WITH_AUTOMATION_TESTS
bool LayoutRealizationWritePlan::ValidateTerrainWriteReplayForTests(
	const FLayoutRealizationWritePlan& WritePlan,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FString& OutFailureReason)
{
	return ValidateTerrainWriteReplayInternal(WritePlan, FrozenTerrainContract, OutFailureReason);
}
#endif

bool LayoutRealizationWritePlan::ApplyTerrainWriteReplay(
	AChunkWorldCore* const ChunkWorld,
	const FLayoutRealizationWritePlan& WritePlan,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FLayoutRealizationWritePlanExecutionState& ExecutionState,
	FString& OutFailureReason,
	const TFunction<TArray<int32>(const TArray<FIntVector>&)>& MaterialReader)
{
	ExecutionState.bTerrainReplayApplied = false;
	if (!ValidateTerrainWriteReplayInternal(WritePlan, FrozenTerrainContract, OutFailureReason))
	{
		return false;
	}
	if (!FLayoutContractPipeline::ApplyFrozenTerrainContract(ChunkWorld, FrozenTerrainContract, OutFailureReason, MaterialReader))
	{
		return false;
	}
	const TArray<FLayoutRealizationTerrainWriteEntry>& ReservedOpenClearWrites =
		WritePlan.TerrainWrites.Excavation.ReservedOpenClearWrites;
	if (!ReservedOpenClearWrites.IsEmpty())
	{
		TArray<FIntVector> Positions;
		TArray<int32> Materials;
		Positions.Reserve(ReservedOpenClearWrites.Num());
		Materials.Reserve(ReservedOpenClearWrites.Num());
		for (const FLayoutRealizationTerrainWriteEntry& ClearWrite : ReservedOpenClearWrites)
		{
			Positions.Add(ClearWrite.BlockWorldPos);
			Materials.Add(ClearWrite.Material);
		}
		ChunkWorld->SetBlockValuesByBlockWorldPos(Positions, Materials, false);
	}
	ExecutionState.bTerrainReplayApplied = true;
	return true;
}

bool LayoutRealizationWritePlan::ValidateTemplatePlacementWrite(
	const FLayoutRealizationWritePlan& WritePlan,
	const FLayoutPlacedModule& Placement,
	const FLayoutRealizationWritePlanExecutionState& ExecutionState,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();

	if (!ValidateChunkWriteBatch(WritePlan, OutFailureReason))
	{
		return false;
	}
	if (!ExecutionState.bTerrainReplayApplied)
	{
		OutFailureReason = TEXT("Template placement write rejected because terrain write-batch passes have not completed.");
		return false;
	}
	for (const FLayoutRealizationTemplatePlacementEntry& Entry : WritePlan.TemplatePlacements.Entries)
	{
		if (Entry.Cell == Placement.Cell
			&& Entry.ModuleSnapshotId == Placement.ModuleSnapshotId
			&& Entry.TemplatePath == Placement.TemplatePath
			&& Entry.YawRotationSteps == Placement.YawRotationSteps)
		{
			return true;
		}
	}

	OutFailureReason = TEXT("Template placement write is not authorized by the realization write plan.");
	return false;
}
