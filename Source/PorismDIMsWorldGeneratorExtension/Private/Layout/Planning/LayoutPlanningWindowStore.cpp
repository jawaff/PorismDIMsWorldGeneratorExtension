// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Runtime/LayoutRealizationWritePlan.h"

namespace
{
	bool IsPlanningStorePositiveCellSize(const FIntVector& CellSizeInBlocks)
	{
		return CellSizeInBlocks.X > 0 && CellSizeInBlocks.Y > 0 && CellSizeInBlocks.Z > 0;
	}

	bool IsPlanningStorePositiveFootprint(const FIntPoint& Footprint)
	{
		return Footprint.X > 0 && Footprint.Y > 0;
	}

	bool DoPlanningStoreInclusiveBoundsOverlap(
		const FIntPoint& AMin,
		const FIntPoint& AMax,
		const FIntPoint& BMin,
		const FIntPoint& BMax)
	{
		return AMin.X <= BMax.X
			&& AMax.X >= BMin.X
			&& AMin.Y <= BMax.Y
			&& AMax.Y >= BMin.Y;
	}

	bool TryComputePlanningStoreSolvedRecordBlockBounds(
		const FPlannedLayoutSiteRecord& Record,
		const FIntVector& CellSizeInBlocks,
		FIntPoint& OutMinBlockXY,
		FIntPoint& OutMaxBlockXY)
	{
		const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
			Record.GetPlannedSiteReservationSourceSelection();
		const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
			Record.GetPlannedSiteLifecycleMetadata();
		const FIntPoint SolvedFootprintSize = LifecycleMetadata.State == EPlannedLayoutSiteState::Realized
			? LifecycleMetadata.RetainedSolvedFootprintSize
			: Record.GetPlannedSiteAcceptedSolvePayload().SolveResult.FootprintSize;
		if (!IsPlanningStorePositiveCellSize(CellSizeInBlocks)
			|| !IsPlanningStorePositiveFootprint(SolvedFootprintSize))
		{
			return false;
		}

		const FIntPoint FootprintSizeInBlocks(
			SolvedFootprintSize.X * CellSizeInBlocks.X,
			SolvedFootprintSize.Y * CellSizeInBlocks.Y);
		OutMinBlockXY = FIntPoint(
			ReservationSourceSelection.SiteCenterBlockWorldPos.X - FootprintSizeInBlocks.X / 2,
			ReservationSourceSelection.SiteCenterBlockWorldPos.Y - FootprintSizeInBlocks.Y / 2);
		OutMaxBlockXY = FIntPoint(
			OutMinBlockXY.X + FootprintSizeInBlocks.X - 1,
			OutMinBlockXY.Y + FootprintSizeInBlocks.Y - 1);
		return true;
	}

	bool NormalizePlannedSiteRecordIdentityForStore(
		FPlannedLayoutSiteRecord& InOutRecord)
	{
		FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
			InOutRecord.GetPlannedSiteLifecycleMetadata();
		if (LifecycleMetadata.StableRecordKey.IsEmpty())
		{
			LifecycleMetadata.StableRecordKey =
				ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
					InOutRecord.GetWorldBindingFrontendSelection(),
					InOutRecord.GetPlannedSiteReservationSourceSelection());
			InOutRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
		}

		if (LifecycleMetadata.StableRecordKey.IsEmpty())
		{
			return false;
		}
		return true;
	}

	FLayoutPlannedSiteLifecycleMetadata BuildAcceptedPlanningStoreLifecycleMetadata(
		const FLayoutPlannedSiteLifecycleMetadata& LifecycleMetadata,
		const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
	{
		FLayoutPlannedSiteLifecycleMetadata UpdatedMetadata = LifecycleMetadata;
		UpdatedMetadata.State = EPlannedLayoutSiteState::Accepted;
		UpdatedMetadata.RejectionReason.Reset();
		UpdatedMetadata.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		UpdatedMetadata.FrozenSubmissionState = ELayoutPlannedSiteFrozenSubmissionState::Accepted;
		UpdatedMetadata.FrozenSubmissionDescriptorId = NAME_None;
		UpdatedMetadata.FrozenSubmissionGeneration = 0;
		UpdatedMetadata.FrozenSubmissionAttemptIndex = 0;
		UpdatedMetadata.FrozenSubmissionAuditHash = 0;
		return UpdatedMetadata;
	}

	FLayoutPlannedSiteLifecycleMetadata BuildRejectedPlanningStoreLifecycleMetadata(
		const FLayoutPlannedSiteLifecycleMetadata& LifecycleMetadata,
		const FString& RejectionReason,
		const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
	{
		FLayoutPlannedSiteLifecycleMetadata UpdatedMetadata = LifecycleMetadata;
		UpdatedMetadata.State = EPlannedLayoutSiteState::Rejected;
		UpdatedMetadata.RejectionReason = RejectionReason;
		UpdatedMetadata.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		UpdatedMetadata.FrozenSubmissionState = ELayoutPlannedSiteFrozenSubmissionState::Rejected;
		UpdatedMetadata.FrozenSubmissionDescriptorId = NAME_None;
		UpdatedMetadata.FrozenSubmissionGeneration = 0;
		UpdatedMetadata.FrozenSubmissionAttemptIndex = 0;
		UpdatedMetadata.FrozenSubmissionAuditHash = 0;
		return UpdatedMetadata;
	}

	FLayoutPlannedSiteLifecycleMetadata BuildRealizedPlanningStoreLifecycleMetadata(
		const FLayoutPlannedSiteLifecycleMetadata& LifecycleMetadata)
	{
		FLayoutPlannedSiteLifecycleMetadata UpdatedMetadata = LifecycleMetadata;
		UpdatedMetadata.State = EPlannedLayoutSiteState::Realized;
		return UpdatedMetadata;
	}

	FLayoutPlannedSiteLifecycleMetadata BuildPlanningStoreLifecycleMetadataWithTerrainDiagnostic(
		const FLayoutPlannedSiteLifecycleMetadata& LifecycleMetadata,
		const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
	{
		FLayoutPlannedSiteLifecycleMetadata UpdatedMetadata = LifecycleMetadata;
		UpdatedMetadata.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		return UpdatedMetadata;
	}

	bool DoesPlanningEndpointMatch(
		const FLayoutPlanningWindowEndpointRecord& Record,
		const FResolvedLayoutConnectorEndpoint& Endpoint,
		const FName ContinuationFamilyId)
	{
		return Record.ContinuationFamilyId == ContinuationFamilyId
			&& Record.Endpoint.RootRecordKey == Endpoint.RootRecordKey
			&& Record.Endpoint.SiteReservationKey == Endpoint.SiteReservationKey
			&& Record.Endpoint.LocalCell == Endpoint.LocalCell
			&& Record.Endpoint.EndpointBlockWorldPos == Endpoint.EndpointBlockWorldPos
			&& Record.Endpoint.ConnectorTypeTag == Endpoint.ConnectorTypeTag
			&& Record.Endpoint.ExposedEntryFaceDirection == Endpoint.ExposedEntryFaceDirection;
	}

	const FLayoutPlanningWindowEndpointRecord* FindPlanningEndpointRecord(
		const TMap<FString, FLayoutPlanningWindowEndpointRecord>& RecordsByKey,
		const FResolvedLayoutConnectorEndpoint& Endpoint,
		const FName ContinuationFamilyId)
	{
		const FLayoutPlanningWindowEndpointRecord* Match = nullptr;
		for (const TPair<FString, FLayoutPlanningWindowEndpointRecord>& Pair : RecordsByKey)
		{
			if (!DoesPlanningEndpointMatch(Pair.Value, Endpoint, ContinuationFamilyId))
			{
				continue;
			}
			if (Match != nullptr)
			{
				return nullptr;
			}
			Match = &Pair.Value;
		}
		return Match;
	}

	bool ValidateAcceptedFrozenTerrainContractActiveCells(
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (FrozenTerrainContract.ContractId.IsNone())
		{
			return true;
		}
		if (FrozenTerrainContract.ActiveCells.IsEmpty())
		{
			OutFailureReason = TEXT("Accepted frozen terrain contract requires active-cell provenance before publication.");
			return false;
		}
		if (!FLayoutContractPipeline::ValidateActiveCellRecords(FrozenTerrainContract.ActiveCells, OutFailureReason))
		{
			return false;
		}
		if (!FrozenTerrainContract.TerrainWrites.IsEmpty() && FrozenTerrainContract.CellContracts.IsEmpty())
		{
			OutFailureReason = TEXT("Accepted frozen terrain contract with terrain writes requires terrain cell contract authority before publication.");
			return false;
		}

		TSet<FIntVector> RealActiveCells;
		for (const FLayoutContractActiveCellRecord& ActiveCell : FrozenTerrainContract.ActiveCells)
		{
			RealActiveCells.Add(ActiveCell.Cell);
		}
		for (const FLayoutTerrainCellContractRecord& CellContract : FrozenTerrainContract.CellContracts)
		{
			if (!RealActiveCells.Contains(CellContract.Cell))
			{
				OutFailureReason = TEXT("Accepted frozen terrain contract has a terrain cell contract without real active-cell authority.");
				return false;
			}
		}
		return true;
	}

	bool ValidateAcceptedSolvePayloadBeforeRealizedState(
		const FLayoutPlannedSiteAcceptedSolvePayload& AcceptedSolvePayload,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (AcceptedSolvePayload.FrozenTerrainContract.ContractId.IsNone())
		{
			return true;
		}
		if (AcceptedSolvePayload.SolvedArtifactId.IsNone()
			|| AcceptedSolvePayload.SolveResult.Placements.IsEmpty())
		{
			OutFailureReason = TEXT("Realized planned-site records require solved-artifact metadata.");
			return false;
		}
		if (!ValidateAcceptedFrozenTerrainContractActiveCells(AcceptedSolvePayload.FrozenTerrainContract, OutFailureReason))
		{
			return false;
		}
		if (AcceptedSolvePayload.SolvedArtifactActiveCellCount != AcceptedSolvePayload.FrozenTerrainContract.ActiveCells.Num())
		{
			OutFailureReason = TEXT("Realized planned-site records require matching solved-artifact active-cell count.");
			return false;
		}
		return LayoutRealizationWritePlan::ValidateSolveResultWriteAuthority(
			AcceptedSolvePayload.SolveResult,
			AcceptedSolvePayload.FrozenTerrainContract.ActiveCells,
			TEXT("Realized planned-site records"),
			OutFailureReason);
	}
}

FLayoutValidationResult FLayoutPlanningWindowSettings::Validate() const
{
	FLayoutValidationResult Result;

	if (SampleSpacing <= 0)
	{
		Result.AddError(TEXT("SampleSpacing must be greater than zero."));
	}

	return Result;
}

FLayoutValidationResult ULayoutPlanningWindowStore::ValidatePlanningWindowSettings(
	const FLayoutPlanningWindowSettings& InSettings)
{
	return InSettings.Validate();
}

FString ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKey(
	const FName WorldBindingId,
	const FName WorldBindingCandidateId,
	const FLayoutResolvedWorldBindingContinuationSelection ResolvedContinuationSelection,
	const FName BiomeRowName,
	const FIntPoint ReservationKey,
	const FIntVector SiteCenterBlockWorldPos,
	const int32 WorldSeed)
{
	return FString::Printf(
		TEXT("%s|%s|%s|%d|%d|%s|%d,%d|%d,%d,%d|%d"),
		*WorldBindingId.ToString(),
		*WorldBindingCandidateId.ToString(),
		*ResolvedContinuationSelection.FamilyId.ToString(),
		static_cast<int32>(ResolvedContinuationSelection.PlacementKind),
		ResolvedContinuationSelection.ResolvedEntryLevel,
		*BiomeRowName.ToString(),
		ReservationKey.X,
		ReservationKey.Y,
		SiteCenterBlockWorldPos.X,
		SiteCenterBlockWorldPos.Y,
		SiteCenterBlockWorldPos.Z,
		WorldSeed);
}

FString ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromFrontendSelection(
	const FLayoutWorldBindingSiteFrontendSelection& FrontendSelection,
	const FIntPoint ReservationKey,
	const FIntVector SiteCenterBlockWorldPos,
	const int32 WorldSeed)
{
	return MakePlannedLayoutSiteRecordKey(
		FrontendSelection.WorldBindingId,
		FrontendSelection.WorldBindingCandidateId,
		FrontendSelection.ResolvedContinuationSelection,
		FrontendSelection.BiomeRowName,
		ReservationKey,
		SiteCenterBlockWorldPos,
		WorldSeed);
}

FString ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
	const FLayoutWorldBindingSiteFrontendSelection& FrontendSelection,
	const FLayoutPlannedSiteReservationSourceSelection& ReservationSourceSelection)
{
	return MakePlannedLayoutSiteRecordKeyFromFrontendSelection(
		FrontendSelection,
		ReservationSourceSelection.ReservationKey,
		ReservationSourceSelection.SiteCenterBlockWorldPos,
		ReservationSourceSelection.WorldSeed);
}

FLayoutValidationResult ULayoutPlanningWindowStore::SetPlanningWindowSettings(
	const FLayoutPlanningWindowSettings& InSettings)
{
	const FLayoutValidationResult ValidationResult = InSettings.Validate();
	if (ValidationResult.IsValid())
	{
		Settings = InSettings;
	}

	return ValidationResult;
}

void ULayoutPlanningWindowStore::ResetPlannedLayoutSiteRecords()
{
	++ContinuationEndpointRevision;
	PlannedSiteRecordsByKey.Reset();
	ContinuationEndpointRecordsByKey.Reset();
	ContinuationEdgeRecordsByKey.Reset();
	ConsumedContinuationEndpointKeys.Reset();
}

bool ULayoutPlanningWindowStore::UpsertPendingPlannedLayoutSiteRecord(
	const FPlannedLayoutSiteRecord& InRecord,
	FPlannedLayoutSiteRecord& OutRecord)
{
	FPlannedLayoutSiteRecord NormalizedRecord = InRecord;
	if (!NormalizePlannedSiteRecordIdentityForStore(NormalizedRecord))
	{
		OutRecord = FPlannedLayoutSiteRecord();
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		NormalizedRecord.GetPlannedSiteLifecycleMetadata();

	if (const FPlannedLayoutSiteRecord* ExistingRecord = PlannedSiteRecordsByKey.Find(LifecycleMetadata.StableRecordKey))
	{
		OutRecord = *ExistingRecord;
		return false;
	}

	LifecycleMetadata.State = EPlannedLayoutSiteState::Pending;
	LifecycleMetadata.RejectionReason.Reset();
	LifecycleMetadata.TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;
	LifecycleMetadata.FrozenSubmissionState = ELayoutPlannedSiteFrozenSubmissionState::None;
	LifecycleMetadata.FrozenSubmissionDescriptorId = NAME_None;
	LifecycleMetadata.FrozenSubmissionGeneration = 0;
	LifecycleMetadata.FrozenSubmissionAttemptIndex = 0;
	LifecycleMetadata.FrozenSubmissionAuditHash = 0;
	NormalizedRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	PlannedSiteRecordsByKey.Add(LifecycleMetadata.StableRecordKey, NormalizedRecord);
	OutRecord = NormalizedRecord;
	return true;
}

bool ULayoutPlanningWindowStore::TryGetPlannedLayoutSiteRecord(
	const FString& StableRecordKey,
	FPlannedLayoutSiteRecord& OutRecord) const
{
	if (const FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey))
	{
		OutRecord = *Record;
		return true;
	}

	OutRecord = FPlannedLayoutSiteRecord();
	return false;
}

FString ULayoutPlanningWindowStore::BuildDebugStatusSummary() const
{
	int32 Pending = 0, Accepted = 0, Failed = 0, Realized = 0, Canceled = 0;
	FString Failure;
	for (const auto& Pair : PlannedSiteRecordsByKey)
	{
		const FLayoutPlannedSiteLifecycleMetadata Metadata = Pair.Value.GetPlannedSiteLifecycleMetadata();
		switch (Metadata.State)
		{
		case EPlannedLayoutSiteState::Pending:
			if (Metadata.FrozenSubmissionState == ELayoutPlannedSiteFrozenSubmissionState::Tombstoned)
			{
				++Canceled;
			}
			else
			{
				++Pending;
			}
			break;
		case EPlannedLayoutSiteState::Accepted: ++Accepted; break;
		case EPlannedLayoutSiteState::Realized: ++Realized; break;
		case EPlannedLayoutSiteState::Rejected:
			++Failed;
			if (Failure.IsEmpty())
			{
				Failure = FString::Printf(TEXT("\nLayout failure [%s]: %s"),
					*Pair.Value.GetWorldBindingFrontendSelection().WorldBindingCandidateId.ToString(),
					*Metadata.RejectionReason.Left(450));
			}
			break;
		}
	}
	return FString::Printf(TEXT("pending=%d solved=%d failed=%d placed=%d canceled=%d%s"),
		Pending, Accepted, Failed, Realized, Canceled, *Failure);
}

TArray<FPlannedLayoutSiteRecord> ULayoutPlanningWindowStore::GetPlannedLayoutSiteRecords() const
{
	TArray<FPlannedLayoutSiteRecord> Records;
	for (const TPair<FString, FPlannedLayoutSiteRecord>& Pair : PlannedSiteRecordsByKey)
	{
		const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata = Pair.Value.GetPlannedSiteLifecycleMetadata();
		if (LifecycleMetadata.State == EPlannedLayoutSiteState::Accepted)
		{
			FString AcceptedPayloadFailureReason;
			if (!ValidateAcceptedSolvePayloadBeforeRealizedState(
					Pair.Value.GetPlannedSiteAcceptedSolvePayload(),
					AcceptedPayloadFailureReason))
			{
				continue;
			}
		}
		Records.Add(Pair.Value);
	}
	Records.Sort([](const FPlannedLayoutSiteRecord& A, const FPlannedLayoutSiteRecord& B)
	{
		return A.GetPlannedSiteLifecycleMetadata().StableRecordKey
			< B.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	});
	return Records;
}

int32 ULayoutPlanningWindowStore::ImportPlannedLayoutSiteRecords(
	const TArray<FPlannedLayoutSiteRecord>& InRecords,
	const bool bReplaceExisting)
{
	if (bReplaceExisting)
	{
		++ContinuationEndpointRevision;
		PlannedSiteRecordsByKey.Reset();
		ContinuationEndpointRecordsByKey.Reset();
		ContinuationEdgeRecordsByKey.Reset();
		ConsumedContinuationEndpointKeys.Reset();
	}

	int32 ImportedRecordCount = 0;
	for (FPlannedLayoutSiteRecord Record : InRecords)
	{
		if (!NormalizePlannedSiteRecordIdentityForStore(Record))
		{
			continue;
		}

		const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
			Record.GetPlannedSiteLifecycleMetadata();
		if (LifecycleMetadata.State == EPlannedLayoutSiteState::Accepted)
		{
			FString AcceptedPayloadFailureReason;
			if (!ValidateAcceptedSolvePayloadBeforeRealizedState(
					Record.GetPlannedSiteAcceptedSolvePayload(),
					AcceptedPayloadFailureReason))
			{
				PlannedSiteRecordsByKey.Remove(LifecycleMetadata.StableRecordKey);
				continue;
			}
		}
		PlannedSiteRecordsByKey.Add(LifecycleMetadata.StableRecordKey, Record);
		++ImportedRecordCount;
	}

	return ImportedRecordCount;
}

TArray<FPlannedLayoutSiteRecord> ULayoutPlanningWindowStore::GetPlannedLayoutSiteRecordsByState(
	const EPlannedLayoutSiteState State) const
{
	TArray<FPlannedLayoutSiteRecord> Records;
	for (const TPair<FString, FPlannedLayoutSiteRecord>& Pair : PlannedSiteRecordsByKey)
	{
		if (Pair.Value.GetPlannedSiteLifecycleMetadata().State == State)
		{
			if (State == EPlannedLayoutSiteState::Accepted)
			{
				FString AcceptedPayloadFailureReason;
				if (!ValidateAcceptedSolvePayloadBeforeRealizedState(
						Pair.Value.GetPlannedSiteAcceptedSolvePayload(),
						AcceptedPayloadFailureReason))
				{
					continue;
				}
			}
			Records.Add(Pair.Value);
		}
	}

	Records.Sort([](const FPlannedLayoutSiteRecord& A, const FPlannedLayoutSiteRecord& B)
	{
		return A.GetPlannedSiteLifecycleMetadata().StableRecordKey
			< B.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	});
	return Records;
}

TArray<FPlannedLayoutSiteRecord> ULayoutPlanningWindowStore::GetAcceptedPlannedLayoutSiteRecordsOverlappingBlockBounds(
	const FIntPoint MinBlockXY,
	const FIntPoint MaxBlockXY,
	const FIntVector CellSizeInBlocks) const
{
	TArray<FPlannedLayoutSiteRecord> Records;
	if (MinBlockXY.X > MaxBlockXY.X || MinBlockXY.Y > MaxBlockXY.Y || !IsPlanningStorePositiveCellSize(CellSizeInBlocks))
	{
		return Records;
	}

	for (const TPair<FString, FPlannedLayoutSiteRecord>& Pair : PlannedSiteRecordsByKey)
	{
		const FPlannedLayoutSiteRecord& Record = Pair.Value;
		const EPlannedLayoutSiteState RecordState =
			Record.GetPlannedSiteLifecycleMetadata().State;
		if (RecordState != EPlannedLayoutSiteState::Accepted
			&& RecordState != EPlannedLayoutSiteState::Realized)
		{
			continue;
		}

		FIntPoint RecordMinBlockXY;
		FIntPoint RecordMaxBlockXY;
		if (TryComputePlanningStoreSolvedRecordBlockBounds(Record, CellSizeInBlocks, RecordMinBlockXY, RecordMaxBlockXY)
			&& DoPlanningStoreInclusiveBoundsOverlap(MinBlockXY, MaxBlockXY, RecordMinBlockXY, RecordMaxBlockXY))
		{
			Records.Add(Record);
		}
	}

	Records.Sort([](const FPlannedLayoutSiteRecord& A, const FPlannedLayoutSiteRecord& B)
	{
		return A.GetPlannedSiteLifecycleMetadata().StableRecordKey
			< B.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	});
	return Records;
}

bool ULayoutPlanningWindowStore::AcceptPlannedLayoutSiteRecord(
	const FString& StableRecordKey,
	const FLayoutSolveResult& SolveResult)
{
	return AcceptPlannedLayoutSiteRecordWithFrozenTerrainContract(
		StableRecordKey,
		SolveResult,
		FLayoutFrozenTerrainContract());
}

bool ULayoutPlanningWindowStore::AcceptPlannedLayoutSiteRecordWithFrozenTerrainContract(
	const FString& StableRecordKey,
	const FLayoutSolveResult& SolveResult,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract)
{
	return AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(
		StableRecordKey,
		SolveResult,
		FrozenTerrainContract,
		NAME_None);
}

bool ULayoutPlanningWindowStore::AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(
	const FString& StableRecordKey,
	const FLayoutSolveResult& SolveResult,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	const FLayoutId SolvedArtifactId)
{
	FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
	if (Record == nullptr)
	{
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record->GetPlannedSiteLifecycleMetadata();
	if (LifecycleMetadata.State == EPlannedLayoutSiteState::Rejected
		|| LifecycleMetadata.State == EPlannedLayoutSiteState::Realized)
	{
		return false;
	}

	FString ActiveCellFailureReason;
	if (!ValidateAcceptedFrozenTerrainContractActiveCells(FrozenTerrainContract, ActiveCellFailureReason))
	{
		return false;
	}
	if (!FrozenTerrainContract.ContractId.IsNone()
		&& (SolvedArtifactId.IsNone() || SolveResult.Placements.IsEmpty()))
	{
		return false;
	}

	LifecycleMetadata =
		BuildAcceptedPlanningStoreLifecycleMetadata(
			LifecycleMetadata,
			FrozenTerrainContract.ContractId.IsNone()
				? ELayoutWorldBindingTerrainFitDiagnosticKind::None
				: FrozenTerrainContract.DiagnosticKind);
	LifecycleMetadata.RetainedSolvedFootprintSize = FIntPoint::ZeroValue;
	Record->SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	FLayoutPlannedSiteAcceptedSolvePayload AcceptedSolvePayload =
		Record->GetPlannedSiteAcceptedSolvePayload();
	AcceptedSolvePayload.SolveResult = SolveResult;
	AcceptedSolvePayload.FrozenTerrainContract = FrozenTerrainContract;
	AcceptedSolvePayload.SolvedArtifactId = SolvedArtifactId;
	AcceptedSolvePayload.SolvedArtifactActiveCellCount = FrozenTerrainContract.ActiveCells.Num();
	Record->SetPlannedSiteAcceptedSolvePayload(AcceptedSolvePayload);
	return true;
}

bool ULayoutPlanningWindowStore::RejectPlannedLayoutSiteRecord(
	const FString& StableRecordKey,
	const FString& RejectionReason,
	const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
{
	FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
	if (Record == nullptr)
	{
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record->GetPlannedSiteLifecycleMetadata();
	if (LifecycleMetadata.State == EPlannedLayoutSiteState::Realized)
	{
		return false;
	}

	LifecycleMetadata = BuildRejectedPlanningStoreLifecycleMetadata(
		LifecycleMetadata,
		RejectionReason,
		TerrainFitDiagnosticKind);
	Record->SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	Record->SetPlannedSiteAcceptedSolvePayload(FLayoutPlannedSiteAcceptedSolvePayload());
	RemoveContinuationEndpointRecordsForRoot(StableRecordKey);
	return true;
}

bool ULayoutPlanningWindowStore::UpdatePlannedLayoutSiteRecordTerrainFitDiagnostic(
	const FString& StableRecordKey,
	const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
{
	FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
	if (Record == nullptr)
	{
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record->GetPlannedSiteLifecycleMetadata();
	LifecycleMetadata = BuildPlanningStoreLifecycleMetadataWithTerrainDiagnostic(
		LifecycleMetadata,
		TerrainFitDiagnosticKind);
	Record->SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	return true;
}

bool ULayoutPlanningWindowStore::UpdatePlannedLayoutSiteFrozenSubmissionState(
	const FString& StableRecordKey,
	const ELayoutPlannedSiteFrozenSubmissionState State,
	const FLayoutId DescriptorId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const int32 AuditHash)
{
	FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
	if (Record == nullptr)
	{
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record->GetPlannedSiteLifecycleMetadata();
	if (LifecycleMetadata.State == EPlannedLayoutSiteState::Realized)
	{
		return false;
	}

	const bool bRequiresActiveDescriptorIdentity =
		State == ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady
		|| State == ELayoutPlannedSiteFrozenSubmissionState::SolveQueued
		|| State == ELayoutPlannedSiteFrozenSubmissionState::CompletedAwaitingPublish;
	if (bRequiresActiveDescriptorIdentity
		&& (DescriptorId.IsNone() || AttemptIndex < 0 || AuditHash == 0))
	{
		return false;
	}

	const auto MatchesCurrentDescriptorIdentity = [&LifecycleMetadata, DescriptorId, Generation, AttemptIndex, AuditHash]()
	{
		return LifecycleMetadata.FrozenSubmissionDescriptorId == DescriptorId
			&& LifecycleMetadata.FrozenSubmissionGeneration == Generation
			&& LifecycleMetadata.FrozenSubmissionAttemptIndex == AttemptIndex
			&& LifecycleMetadata.FrozenSubmissionAuditHash == AuditHash;
	};
	if (State == ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady
		&& LifecycleMetadata.FrozenSubmissionState != ELayoutPlannedSiteFrozenSubmissionState::None
		&& LifecycleMetadata.FrozenSubmissionState != ELayoutPlannedSiteFrozenSubmissionState::Tombstoned
		&& LifecycleMetadata.FrozenSubmissionState != ELayoutPlannedSiteFrozenSubmissionState::Rejected)
	{
		return false;
	}
	if (State == ELayoutPlannedSiteFrozenSubmissionState::SolveQueued
		&& (LifecycleMetadata.FrozenSubmissionState != ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady
			|| !MatchesCurrentDescriptorIdentity()))
	{
		return false;
	}
	if (State == ELayoutPlannedSiteFrozenSubmissionState::CompletedAwaitingPublish
		&& (LifecycleMetadata.FrozenSubmissionState != ELayoutPlannedSiteFrozenSubmissionState::SolveQueued
			|| !MatchesCurrentDescriptorIdentity()))
	{
		return false;
	}

	LifecycleMetadata.FrozenSubmissionState = State;
	LifecycleMetadata.FrozenSubmissionDescriptorId = DescriptorId;
	LifecycleMetadata.FrozenSubmissionGeneration = Generation;
	LifecycleMetadata.FrozenSubmissionAttemptIndex = AttemptIndex;
	LifecycleMetadata.FrozenSubmissionAuditHash = AuditHash;
	Record->SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	return true;
}

bool ULayoutPlanningWindowStore::ResetPlannedLayoutSiteRecordForFrozenSubmissionRetry(const FString& StableRecordKey)
{
	FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
	if (Record == nullptr)
	{
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record->GetPlannedSiteLifecycleMetadata();
	const bool bRejectedRetry = LifecycleMetadata.State == EPlannedLayoutSiteState::Rejected;
	const bool bTombstonedRetry = LifecycleMetadata.FrozenSubmissionState == ELayoutPlannedSiteFrozenSubmissionState::Tombstoned
		|| LifecycleMetadata.FrozenSubmissionState == ELayoutPlannedSiteFrozenSubmissionState::Rejected;
	if (!bRejectedRetry && !bTombstonedRetry)
	{
		return false;
	}

	LifecycleMetadata.State = EPlannedLayoutSiteState::Pending;
	LifecycleMetadata.RejectionReason.Reset();
	LifecycleMetadata.TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;
	LifecycleMetadata.FrozenSubmissionState = ELayoutPlannedSiteFrozenSubmissionState::None;
	LifecycleMetadata.FrozenSubmissionDescriptorId = NAME_None;
	LifecycleMetadata.FrozenSubmissionGeneration = 0;
	LifecycleMetadata.FrozenSubmissionAttemptIndex = FMath::Max(0, LifecycleMetadata.FrozenSubmissionAttemptIndex) + 1;
	LifecycleMetadata.FrozenSubmissionAuditHash = 0;
	Record->SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	Record->SetPlannedSiteAcceptedSolvePayload(FLayoutPlannedSiteAcceptedSolvePayload());
	return true;
}

bool ULayoutPlanningWindowStore::MarkPlannedLayoutSiteRecordRealized(const FString& StableRecordKey)
{
	FPlannedLayoutSiteRecord* Record = PlannedSiteRecordsByKey.Find(StableRecordKey);
	if (Record == nullptr)
	{
		return false;
	}

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record->GetPlannedSiteLifecycleMetadata();
	if (LifecycleMetadata.State == EPlannedLayoutSiteState::Rejected)
	{
		return false;
	}

	if (LifecycleMetadata.State == EPlannedLayoutSiteState::Realized)
	{
		return true;
	}

	if (LifecycleMetadata.State != EPlannedLayoutSiteState::Accepted)
	{
		return false;
	}

	FString AcceptedPayloadFailureReason;
	if (!ValidateAcceptedSolvePayloadBeforeRealizedState(
			Record->GetPlannedSiteAcceptedSolvePayload(),
			AcceptedPayloadFailureReason))
	{
		return false;
	}

	LifecycleMetadata =
		BuildRealizedPlanningStoreLifecycleMetadata(LifecycleMetadata);
	LifecycleMetadata.RetainedSolvedFootprintSize =
		Record->GetPlannedSiteAcceptedSolvePayload().SolveResult.FootprintSize;
	Record->SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	// Realized roots discard full solve payload but retain footprint authority for overlap rejection.
	Record->SetPlannedSiteAcceptedSolvePayload(FLayoutPlannedSiteAcceptedSolvePayload());
	return true;
}

int32 ULayoutPlanningWindowStore::RemoveSettledRecordsWithoutInfluence(const TSet<FString>& RetainedRootKeys)
{
	int32 RemovedCount = 0;
	for (auto It = PlannedSiteRecordsByKey.CreateIterator(); It; ++It)
	{
		const EPlannedLayoutSiteState State = It.Value().GetPlannedSiteLifecycleMetadata().State;
		if ((State == EPlannedLayoutSiteState::Realized || State == EPlannedLayoutSiteState::Rejected)
			&& !RetainedRootKeys.Contains(It.Key()))
		{
			It.RemoveCurrent();
			++RemovedCount;
		}
	}
	return RemovedCount;
}

bool ULayoutPlanningWindowStore::UpsertContinuationEndpointRecord(
	const FLayoutPlanningWindowEndpointRecord& InRecord,
	FLayoutPlanningWindowEndpointRecord& OutRecord)
{
	if (InRecord.StableEndpointKey.IsEmpty() || InRecord.RootRecordKey.IsEmpty()
		|| InRecord.ContinuationFamilyId.IsNone() || InRecord.RemainingConnections <= 0
		|| ConsumedContinuationEndpointKeys.Contains(InRecord.StableEndpointKey)
		|| CountContinuationRootConnections(InRecord.RootRecordKey, InRecord.ContinuationFamilyId, true) >= InRecord.MaxConnectionsPerSite)
	{
		OutRecord = FLayoutPlanningWindowEndpointRecord();
		return false;
	}
	if (const FLayoutPlanningWindowEndpointRecord* Existing =
			ContinuationEndpointRecordsByKey.Find(InRecord.StableEndpointKey))
	{
		OutRecord = *Existing;
		return false;
	}
	ContinuationEndpointRecordsByKey.Add(InRecord.StableEndpointKey, InRecord);
	++ContinuationEndpointRevision;
	OutRecord = InRecord;
	return true;
}

TArray<FLayoutPlanningWindowEndpointRecord>
ULayoutPlanningWindowStore::GetContinuationEndpointRecords() const
{
	TArray<FLayoutPlanningWindowEndpointRecord> Records;
	ContinuationEndpointRecordsByKey.GenerateValueArray(Records);
	Records.Sort([](const FLayoutPlanningWindowEndpointRecord& Left,
		const FLayoutPlanningWindowEndpointRecord& Right)
	{
		return Left.StableEndpointKey < Right.StableEndpointKey;
	});
	return Records;
}

bool ULayoutPlanningWindowStore::TryMakeContinuationEdgeKey(
	const FResolvedLayoutConnectorEndpoint& Start,
	const FResolvedLayoutConnectorEndpoint& End,
	const FName ContinuationFamilyId,
	const FName ContinuationFamilyCandidateId,
	FString& OutEdgeKey,
	FString& OutFailureReason) const
{
	OutEdgeKey.Reset();
	OutFailureReason.Reset();
	const FLayoutPlanningWindowEndpointRecord* StartRecord =
		FindPlanningEndpointRecord(ContinuationEndpointRecordsByKey, Start, ContinuationFamilyId);
	const FLayoutPlanningWindowEndpointRecord* EndRecord =
		FindPlanningEndpointRecord(ContinuationEndpointRecordsByKey, End, ContinuationFamilyId);
	if (StartRecord == nullptr || EndRecord == nullptr || StartRecord == EndRecord
		|| ContinuationFamilyId.IsNone() || ContinuationFamilyCandidateId.IsNone())
	{
		OutFailureReason = TEXT("Continuation edge requires two distinct unambiguous retained endpoints and selected family candidate.");
		return false;
	}
	const FString& FirstKey = StartRecord->StableEndpointKey < EndRecord->StableEndpointKey
		? StartRecord->StableEndpointKey : EndRecord->StableEndpointKey;
	const FString& SecondKey = StartRecord->StableEndpointKey < EndRecord->StableEndpointKey
		? EndRecord->StableEndpointKey : StartRecord->StableEndpointKey;
	OutEdgeKey = FString::Printf(TEXT("%s|%s|%s|%s"), *FirstKey, *SecondKey,
		*ContinuationFamilyId.ToString(), *ContinuationFamilyCandidateId.ToString());
	return true;
}

int32 ULayoutPlanningWindowStore::CountContinuationRootConnections(const FString& RootRecordKey, const FName ContinuationFamilyId, const bool bCommittedOnly) const
{
	int32 Count = 0;
	for (const TPair<FString, FLayoutPlanningWindowContinuationEdgeRecord>& Pair : ContinuationEdgeRecordsByKey)
	{
		const FLayoutPlanningWindowContinuationEdgeRecord& Edge = Pair.Value;
		if (Edge.State != ELayoutContinuationEdgeState::Failed
			&& (!bCommittedOnly || Edge.State == ELayoutContinuationEdgeState::Committed)
			&& Edge.ContinuationFamilyId == ContinuationFamilyId
			&& (Edge.StartRootRecordKey == RootRecordKey || Edge.EndRootRecordKey == RootRecordKey)) ++Count;
	}
	return Count;
}

bool ULayoutPlanningWindowStore::ReserveContinuationEndpointPair(
	const FResolvedLayoutConnectorEndpoint& Start,
	const FResolvedLayoutConnectorEndpoint& End,
	const FName ContinuationFamilyId,
	const FName ContinuationFamilyCandidateId,
	FString& OutEdgeKey,
	FString& OutFailureReason,
	const bool bExplicitRetry)
{
	if (!TryMakeContinuationEdgeKey(Start, End, ContinuationFamilyId, ContinuationFamilyCandidateId, OutEdgeKey, OutFailureReason))
	{
		return false;
	}
	if (const FLayoutPlanningWindowContinuationEdgeRecord* Existing = ContinuationEdgeRecordsByKey.Find(OutEdgeKey);
		Existing != nullptr && (!bExplicitRetry || Existing->State != ELayoutContinuationEdgeState::Failed))
	{
		OutFailureReason = TEXT("Continuation endpoint pair already has a reserved, committed or failed attempt.");
		return false;
	}
	const FLayoutPlanningWindowEndpointRecord* StartMatch =
		FindPlanningEndpointRecord(ContinuationEndpointRecordsByKey, Start, ContinuationFamilyId);
	const FLayoutPlanningWindowEndpointRecord* EndMatch =
		FindPlanningEndpointRecord(ContinuationEndpointRecordsByKey, End, ContinuationFamilyId);
	FLayoutPlanningWindowEndpointRecord* StartRecord = StartMatch != nullptr
		? ContinuationEndpointRecordsByKey.Find(StartMatch->StableEndpointKey) : nullptr;
	FLayoutPlanningWindowEndpointRecord* EndRecord = EndMatch != nullptr
		? ContinuationEndpointRecordsByKey.Find(EndMatch->StableEndpointKey) : nullptr;
	if (StartRecord == nullptr || EndRecord == nullptr || StartRecord == EndRecord
		|| StartRecord->State != ELayoutContinuationEndpointState::Ready
		|| EndRecord->State != ELayoutContinuationEndpointState::Ready
		|| StartRecord->RemainingConnections <= 0 || EndRecord->RemainingConnections <= 0)
	{
		OutEdgeKey.Reset();
		OutFailureReason = TEXT("Continuation endpoint is no longer available.");
		return false;
	}
	if (CountContinuationRootConnections(StartRecord->RootRecordKey, ContinuationFamilyId) >= StartRecord->MaxConnectionsPerSite
		|| CountContinuationRootConnections(EndRecord->RootRecordKey, ContinuationFamilyId) >= EndRecord->MaxConnectionsPerSite)
	{
		OutEdgeKey.Reset();
		OutFailureReason = TEXT("Continuation root/family connection limit reached.");
		return false;
	}
	// Distinct retained entry pairs remain eligible; failed edge identities above prevent retries.
	for (const TPair<FString, FLayoutPlanningWindowContinuationEdgeRecord>& Pair : ContinuationEdgeRecordsByKey)
	{
		const FLayoutPlanningWindowContinuationEdgeRecord& Edge = Pair.Value;
		if (Edge.ContinuationFamilyId == ContinuationFamilyId
			&& ((Edge.StartRootRecordKey == StartRecord->RootRecordKey && Edge.EndRootRecordKey == EndRecord->RootRecordKey)
				|| (Edge.StartRootRecordKey == EndRecord->RootRecordKey && Edge.EndRootRecordKey == StartRecord->RootRecordKey)))
		{
			if (Edge.State == ELayoutContinuationEdgeState::Failed)
			{
				continue;
			}
			OutEdgeKey.Reset();
			OutFailureReason = TEXT("Continuation root pair already has a reserved or committed route in this family.");
			return false;
		}
	}
	StartRecord->State = ELayoutContinuationEndpointState::Reserved;
	EndRecord->State = ELayoutContinuationEndpointState::Reserved;
	StartRecord->ReservedEdgeKey = OutEdgeKey;
	EndRecord->ReservedEdgeKey = OutEdgeKey;
	FLayoutPlanningWindowContinuationEdgeRecord& Edge = ContinuationEdgeRecordsByKey.Add(OutEdgeKey);
	Edge.EdgeKey = OutEdgeKey;
	Edge.State = ELayoutContinuationEdgeState::Reserved;
	Edge.StartEndpointKey = StartRecord->StableEndpointKey;
	Edge.EndEndpointKey = EndRecord->StableEndpointKey;
	Edge.StartRootRecordKey = StartRecord->RootRecordKey;
	Edge.EndRootRecordKey = EndRecord->RootRecordKey;
	Edge.ContinuationFamilyId = ContinuationFamilyId;
	Edge.StartEndpointBlockWorldPos = StartRecord->Endpoint.EndpointBlockWorldPos;
	Edge.EndEndpointBlockWorldPos = EndRecord->Endpoint.EndpointBlockWorldPos;
	return true;
}

void ULayoutPlanningWindowStore::ReleaseContinuationEndpointPair(const FString& EdgeKey, const bool bFailed)
{
	FLayoutPlanningWindowContinuationEdgeRecord* Edge = ContinuationEdgeRecordsByKey.Find(EdgeKey);
	if (Edge == nullptr || Edge->State != ELayoutContinuationEdgeState::Reserved)
	{
		return;
	}
	for (const FString& EndpointKey : { Edge->StartEndpointKey, Edge->EndEndpointKey })
	{
		if (FLayoutPlanningWindowEndpointRecord* Record = ContinuationEndpointRecordsByKey.Find(EndpointKey);
			Record != nullptr && Record->ReservedEdgeKey == EdgeKey)
		{
			Record->State = ELayoutContinuationEndpointState::Ready;
			Record->ReservedEdgeKey.Reset();
		}
	}
	if (bFailed)
	{
		Edge->State = ELayoutContinuationEdgeState::Failed;
		++ContinuationEndpointRevision;
	}
	else
	{
		ContinuationEdgeRecordsByKey.Remove(EdgeKey);
	}
}

void ULayoutPlanningWindowStore::InvalidateContinuationFailureHistory()
{
	bool bChanged = false;
	for (auto It = ContinuationEdgeRecordsByKey.CreateIterator(); It; ++It)
	{
		if (It.Value().State == ELayoutContinuationEdgeState::Failed)
		{
			It.RemoveCurrent();
			bChanged = true;
		}
	}
	if (bChanged) ++ContinuationEndpointRevision;
}

bool ULayoutPlanningWindowStore::ConsumeContinuationEndpointPair(const FString& EdgeKey)
{
	FLayoutPlanningWindowContinuationEdgeRecord* Edge = ContinuationEdgeRecordsByKey.Find(EdgeKey);
	if (Edge == nullptr)
	{
		return false;
	}
	if (Edge->State == ELayoutContinuationEdgeState::Committed)
	{
		return true;
	}
	FLayoutPlanningWindowEndpointRecord* StartRecord = ContinuationEndpointRecordsByKey.Find(Edge->StartEndpointKey);
	FLayoutPlanningWindowEndpointRecord* EndRecord = ContinuationEndpointRecordsByKey.Find(Edge->EndEndpointKey);
	if (StartRecord == nullptr || EndRecord == nullptr
		|| StartRecord->ReservedEdgeKey != EdgeKey || EndRecord->ReservedEdgeKey != EdgeKey)
	{
		return false;
	}
	for (FLayoutPlanningWindowEndpointRecord* Record : { StartRecord, EndRecord })
	{
		Record->RemainingConnections = FMath::Max(0, Record->RemainingConnections - 1);
		Record->ReservedEdgeKey.Reset();
		Record->State = Record->RemainingConnections > 0
			? ELayoutContinuationEndpointState::Ready
			: ELayoutContinuationEndpointState::Connected;
	}
	if (StartRecord->RemainingConnections == 0)
	{
		ConsumedContinuationEndpointKeys.Add(StartRecord->StableEndpointKey);
		ContinuationEndpointRecordsByKey.Remove(StartRecord->StableEndpointKey);
	}
	if (EndRecord->RemainingConnections == 0)
	{
		ConsumedContinuationEndpointKeys.Add(EndRecord->StableEndpointKey);
		ContinuationEndpointRecordsByKey.Remove(EndRecord->StableEndpointKey);
	}
	Edge->State = ELayoutContinuationEdgeState::Committed;
	// Full committed capacity proves these unreserved entries cannot gain another route.
	// Unloaded terrain, missing neighbors and failed alternatives do not prove that.
	for (auto It = ContinuationEndpointRecordsByKey.CreateIterator(); It; ++It)
	{
		const auto& Record = It.Value();
		if (Record.State != ELayoutContinuationEndpointState::Reserved
			&& CountContinuationRootConnections(Record.RootRecordKey, Record.ContinuationFamilyId, true) >= Record.MaxConnectionsPerSite)
		{
			It.RemoveCurrent();
		}
	}
	++ContinuationEndpointRevision;
	return true;
}

int32 ULayoutPlanningWindowStore::RemoveContinuationEndpointRecordsOutsideWindows(
	const TArray<FIntPoint>& MinBlockXYs,
	const TArray<FIntPoint>& MaxBlockXYs)
{
	if (MinBlockXYs.Num() != MaxBlockXYs.Num())
	{
		return 0;
	}
	int32 RemovedCount = 0;
	for (auto It = ContinuationEndpointRecordsByKey.CreateIterator(); It; ++It)
	{
		const FIntVector Position = It.Value().Endpoint.EndpointBlockWorldPos;
		bool bRetained = false;
		for (int32 Index = 0; Index < MinBlockXYs.Num(); ++Index)
		{
			bRetained = Position.X >= MinBlockXYs[Index].X && Position.X <= MaxBlockXYs[Index].X
				&& Position.Y >= MinBlockXYs[Index].Y && Position.Y <= MaxBlockXYs[Index].Y;
			if (bRetained) break;
		}
		if (!bRetained)
		{
			It.RemoveCurrent();
			++RemovedCount;
		}
	}
	auto IsInsideAnyWindow = [&MinBlockXYs, &MaxBlockXYs](const FIntVector& Position)
	{
		for (int32 Index = 0; Index < MinBlockXYs.Num(); ++Index)
		{
			if (Position.X >= MinBlockXYs[Index].X && Position.X <= MaxBlockXYs[Index].X
				&& Position.Y >= MinBlockXYs[Index].Y && Position.Y <= MaxBlockXYs[Index].Y)
			{
				return true;
			}
		}
		return false;
	};
	for (auto It = ContinuationEdgeRecordsByKey.CreateIterator(); It; ++It)
	{
		const bool bEndpointsRetained = ContinuationEndpointRecordsByKey.Contains(It.Value().StartEndpointKey)
			&& ContinuationEndpointRecordsByKey.Contains(It.Value().EndEndpointKey);
		const bool bHistoricalEdgeRetained = It.Value().State != ELayoutContinuationEdgeState::Reserved
			&& (IsInsideAnyWindow(It.Value().StartEndpointBlockWorldPos)
				|| IsInsideAnyWindow(It.Value().EndEndpointBlockWorldPos));
		if (!bEndpointsRetained && !bHistoricalEdgeRetained)
		{
			if (It.Value().State == ELayoutContinuationEdgeState::Reserved)
			{
				for (const FString& EndpointKey : { It.Value().StartEndpointKey, It.Value().EndEndpointKey })
				{
					if (FLayoutPlanningWindowEndpointRecord* Record = ContinuationEndpointRecordsByKey.Find(EndpointKey);
						Record != nullptr && Record->ReservedEdgeKey == It.Key())
					{
						Record->State = ELayoutContinuationEndpointState::Ready;
						Record->ReservedEdgeKey.Reset();
					}
				}
			}
			It.RemoveCurrent();
		}
	}
	// Capacity tombstones share committed-edge influence, not the lifetime of this store.
	// An edge remains while either endpoint is relevant, preventing republished roots
	// from recovering a spent slot at the retention boundary.
	TSet<FString> RetainedConsumedKeys;
	for (const auto& Pair : ContinuationEdgeRecordsByKey)
	{
		if (Pair.Value.State != ELayoutContinuationEdgeState::Committed) continue;
		RetainedConsumedKeys.Add(Pair.Value.StartEndpointKey);
		RetainedConsumedKeys.Add(Pair.Value.EndEndpointKey);
	}
	for (auto It = ConsumedContinuationEndpointKeys.CreateIterator(); It; ++It)
	{
		if (!RetainedConsumedKeys.Contains(*It)) It.RemoveCurrent();
	}
	if (RemovedCount > 0) ++ContinuationEndpointRevision;
	return RemovedCount;
}

void ULayoutPlanningWindowStore::RemoveContinuationEndpointRecordsForRoot(
	const FString& RootRecordKey)
{
	++ContinuationEndpointRevision;
	TSet<FString> RemovedEndpointKeys;
	for (auto It = ContinuationEndpointRecordsByKey.CreateIterator(); It; ++It)
	{
		if (It.Value().RootRecordKey == RootRecordKey)
		{
			RemovedEndpointKeys.Add(It.Key());
			It.RemoveCurrent();
		}
	}
	for (auto It = ConsumedContinuationEndpointKeys.CreateIterator(); It; ++It)
	{
		if ((*It).StartsWith(RootRecordKey + TEXT("/")))
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = ContinuationEdgeRecordsByKey.CreateIterator(); It; ++It)
	{
		if (RemovedEndpointKeys.Contains(It.Value().StartEndpointKey)
			|| RemovedEndpointKeys.Contains(It.Value().EndEndpointKey)
			|| It.Value().StartRootRecordKey == RootRecordKey
			|| It.Value().EndRootRecordKey == RootRecordKey)
		{
			if (It.Value().State == ELayoutContinuationEdgeState::Reserved)
			{
				for (const FString& EndpointKey : { It.Value().StartEndpointKey, It.Value().EndEndpointKey })
				{
					if (FLayoutPlanningWindowEndpointRecord* Record = ContinuationEndpointRecordsByKey.Find(EndpointKey);
						Record != nullptr && Record->ReservedEdgeKey == It.Key())
					{
						Record->State = ELayoutContinuationEndpointState::Ready;
						Record->ReservedEdgeKey.Reset();
					}
				}
			}
			It.RemoveCurrent();
		}
	}
}
