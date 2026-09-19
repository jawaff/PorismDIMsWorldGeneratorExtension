// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Contracts/LayoutContractPlacementCandidates.h"

#include "Layout/Planning/LayoutSiteReservation.h"

namespace
{
	constexpr uint64 LayoutCandidateOrderFnvOffsetBasis = 14695981039346656037ull;
	constexpr uint64 LayoutCandidateOrderFnvPrime = 1099511628211ull;

	void AppendOrderingStableString(uint64& InOutHash, const FString& Value)
	{
		for (const TCHAR Character : Value)
		{
			InOutHash ^= static_cast<uint64>(Character);
			InOutHash *= LayoutCandidateOrderFnvPrime;
		}
		InOutHash ^= static_cast<uint64>('|');
		InOutHash *= LayoutCandidateOrderFnvPrime;
	}

	void AppendOrderingStableName(uint64& InOutHash, const FLayoutId Value)
	{
		AppendOrderingStableString(InOutHash, Value.ToString());
	}

	void AppendOrderingStableInt(uint64& InOutHash, const int64 Value)
	{
		AppendOrderingStableString(InOutHash, LexToString(Value));
	}

	FLayoutId MakeOrderingStableId(const uint64 Hash)
	{
		return FLayoutId(*FString::Printf(TEXT("CandidateOrder.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	int32 CompareNames(const FLayoutId Left, const FLayoutId Right)
	{
		return FCString::Strcmp(*Left.ToString(), *Right.ToString());
	}

	bool IsBlockXYInsidePlacementCandidateSearchBounds(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntPoint& BlockXY)
	{
		return BlockXY.X >= Artifact.SearchMinBlockXY.X
			&& BlockXY.X <= Artifact.SearchMaxBlockXY.X
			&& BlockXY.Y >= Artifact.SearchMinBlockXY.Y
			&& BlockXY.Y <= Artifact.SearchMaxBlockXY.Y;
	}

	bool HasTerrainBackedOrderingBaseEvidence(const FLayoutFrozenTerrainBiomeAdapterInput& Artifact)
	{
		if (Artifact.EligibleBiomeRowName.IsNone())
		{
			return false;
		}
		if (!Artifact.bHasFiniteSearchBounds
			|| !Artifact.bHasSampledColumnEvidence
			|| !Artifact.bHasSteppedSupportEvidence
			|| Artifact.SearchDepthBlocks <= 0
			|| Artifact.FootprintSizeInBlocks.X <= 0
			|| Artifact.FootprintSizeInBlocks.Y <= 0
			|| Artifact.SearchMinBlockXY.X > Artifact.SearchMaxBlockXY.X
			|| Artifact.SearchMinBlockXY.Y > Artifact.SearchMaxBlockXY.Y
			|| Artifact.SurfaceSamples.IsEmpty())
		{
			return false;
		}
		for (const FLayoutTerrainSurfaceSample& SurfaceSample : Artifact.SurfaceSamples)
		{
			if (!SurfaceSample.bIsValid || !IsBlockXYInsidePlacementCandidateSearchBounds(Artifact, SurfaceSample.BlockXY))
			{
				return false;
			}
		}
		if (Artifact.bRequiresBiomeOwnership)
		{
			if (!Artifact.bHasBiomeOwnershipEvidence || Artifact.EligibleBiomeRowNames.IsEmpty() || Artifact.BiomeOwnershipSamples.IsEmpty())
			{
				return false;
			}
			for (const FLayoutFrozenBiomeOwnershipSample& OwnershipSample : Artifact.BiomeOwnershipSamples)
			{
				if (!OwnershipSample.bOwnedByAllowList
					|| !OwnershipSample.bHasSurfaceEvidence
					|| !IsBlockXYInsidePlacementCandidateSearchBounds(Artifact, OwnershipSample.BlockXY)
					|| (!OwnershipSample.OwningBiomeRowName.IsNone()
						&& !Artifact.EligibleBiomeRowNames.Contains(OwnershipSample.OwningBiomeRowName)))
				{
					return false;
				}
			}
		}
		return true;
	}

	bool IsShiftedFootprintEvidenceUsableForShift(
		const FLayoutFrozenShiftedFootprintEvidence& Evidence,
		const FIntVector& ShiftCells,
		const FLayoutId ShiftId)
	{
		const FLayoutId EvidenceShiftId = !Evidence.PlacementShiftId.IsNone()
			? Evidence.PlacementShiftId
			: FLayoutContractPlacementCandidates::BuildPlacementShiftId(Evidence.ShiftCells);
		return EvidenceShiftId == ShiftId
			&& Evidence.ShiftCells == ShiftCells
			&& Evidence.bHasTerrainFitProof
			&& Evidence.bHasReservationCollisionProof
			&& Evidence.bHasChunkOverlapProvenance
			&& !Evidence.SteppedSupportSamples.IsEmpty();
	}

	bool HasUsableShiftedFootprintEvidenceForShift(
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntVector& ShiftCells,
		const FLayoutId ShiftId)
	{
		if (!Artifact.bHasShiftedFootprintEvidence || Artifact.ShiftedFootprintEvidence.IsEmpty())
		{
			return false;
		}
		return Artifact.ShiftedFootprintEvidence.ContainsByPredicate(
			[&ShiftCells, ShiftId](const FLayoutFrozenShiftedFootprintEvidence& Evidence)
			{
				return IsShiftedFootprintEvidenceUsableForShift(Evidence, ShiftCells, ShiftId);
			});
	}

	FLayoutId BuildChildBranchCandidateId(const FLayoutChildBranchOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("ChildBranch"));
		AppendOrderingStableName(Hash, Input.SourceEntryId);
		AppendOrderingStableName(Hash, Input.StableChildId);
		AppendOrderingStableName(Hash, Input.ChildProfileId);
		AppendOrderingStableName(Hash, Input.SelectedCapabilityId);
		AppendOrderingStableName(Hash, Input.DelegatedFeatureId);
		AppendOrderingStableName(Hash, Input.ClosureSeamOrJunctionId);
		AppendOrderingStableName(Hash, Input.PseudoAscentStrategyId);
		AppendOrderingStableName(Hash, Input.RouteOrHandoffAnchorId);
		AppendOrderingStableName(Hash, Input.OptionalDropOrResidualId);
		AppendOrderingStableInt(Hash, Input.EarlyStructuralPriority);
		return FLayoutId(*FString::Printf(TEXT("ChildBranch.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildChildBranchObligationId(const FLayoutChildBranchOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("ChildObligation"));
		AppendOrderingStableName(Hash, Input.SourceEntryId);
		AppendOrderingStableName(Hash, Input.DelegatedFeatureId);
		AppendOrderingStableName(Hash, Input.ClosureSeamOrJunctionId);
		AppendOrderingStableName(Hash, Input.PseudoAscentStrategyId);
		AppendOrderingStableName(Hash, Input.RouteOrHandoffAnchorId);
		AppendOrderingStableName(Hash, Input.OptionalDropOrResidualId);
		AppendOrderingStableInt(Hash, Input.EarlyStructuralPriority);
		return FLayoutId(*FString::Printf(TEXT("ChildObligation.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildCompositeScoutCandidateId(const FLayoutCompositeScoutOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("CompositeScout"));
		AppendOrderingStableName(Hash, Input.CompositeSnapshotId);
		AppendOrderingStableName(Hash, Input.OccupiedCellFootprintHash);
		AppendOrderingStableName(Hash, Input.ExposedFaceOrCapabilityId);
		AppendOrderingStableName(Hash, Input.PlacementZoneId);
		AppendOrderingStableName(Hash, Input.FeatureProviderId);
		return FLayoutId(*FString::Printf(TEXT("CompositeScout.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildCompositeScoutObligationId(const FLayoutCompositeScoutOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("CompositeObligation"));
		AppendOrderingStableName(Hash, Input.CompositeSnapshotId);
		AppendOrderingStableName(Hash, Input.ExposedFaceOrCapabilityId);
		AppendOrderingStableName(Hash, Input.FeatureProviderId);
		return FLayoutId(*FString::Printf(TEXT("CompositeObligation.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildSingleLevelSteppedAscentCandidateId(const FLayoutSingleLevelSteppedAscentOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("SingleLevelSteppedAscent"));
		AppendOrderingStableName(Hash, Input.LowerAscentId);
		AppendOrderingStableName(Hash, Input.UpperConnectorOrCapId);
		AppendOrderingStableName(Hash, Input.CompositeBundleId);
		AppendOrderingStableInt(Hash, Input.PseudoConnectorCell.X);
		AppendOrderingStableInt(Hash, Input.PseudoConnectorCell.Y);
		AppendOrderingStableInt(Hash, Input.PseudoConnectorCell.Z);
		AppendOrderingStableName(Hash, Input.TraversalChannelId);
		AppendOrderingStableName(Hash, Input.TerrainFrontierId);
		return FLayoutId(*FString::Printf(TEXT("SingleLevelAscent.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildSingleLevelSteppedAscentObligationId(const FLayoutSingleLevelSteppedAscentOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("SingleLevelAscentObligation"));
		AppendOrderingStableName(Hash, Input.LowerAscentId);
		AppendOrderingStableName(Hash, Input.TraversalChannelId);
		AppendOrderingStableName(Hash, Input.TerrainFrontierId);
		return FLayoutId(*FString::Printf(TEXT("SingleLevelAscentObligation.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildContinuationEdgeScoutCandidateId(const FLayoutContinuationEdgeScoutOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("ContinuationEdgeScout"));
		AppendOrderingStableName(Hash, Input.SourceRegionId);
		AppendOrderingStableName(Hash, Input.ContinuationFamilyId);
		AppendOrderingStableName(Hash, Input.SourceEndpointId);
		AppendOrderingStableName(Hash, Input.TargetEndpointOrCandidateId);
		AppendOrderingStableName(Hash, Input.EdgeScoutResultId);
		AppendOrderingStableName(Hash, Input.PathShapeId);
		AppendOrderingStableName(Hash, Input.ContinuationModeId);
		return FLayoutId(*FString::Printf(TEXT("ContinuationEdgeScout.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildContinuationEdgeScoutObligationId(const FLayoutContinuationEdgeScoutOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("ContinuationObligation"));
		AppendOrderingStableName(Hash, Input.SourceRegionId);
		AppendOrderingStableName(Hash, Input.ContinuationFamilyId);
		AppendOrderingStableName(Hash, Input.SourceEndpointId);
		AppendOrderingStableName(Hash, Input.PathShapeId);
		AppendOrderingStableName(Hash, Input.ContinuationModeId);
		return FLayoutId(*FString::Printf(TEXT("ContinuationObligation.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildRootScoutResultCandidateId(const FLayoutRootScoutResultOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("RootScoutResult"));
		AppendOrderingStableName(Hash, Input.WorldBindingId);
		AppendOrderingStableName(Hash, Input.ScoutResultId);
		AppendOrderingStableName(Hash, Input.RootCandidateId);
		AppendOrderingStableName(Hash, Input.PlacementPolicyId);
		AppendOrderingStableName(Hash, Input.BiomeRowName);
		AppendOrderingStableInt(Hash, Input.SnappedSiteCenterBlockWorldPos.X);
		AppendOrderingStableInt(Hash, Input.SnappedSiteCenterBlockWorldPos.Y);
		AppendOrderingStableInt(Hash, Input.SnappedSiteCenterBlockWorldPos.Z);
		return FLayoutId(*FString::Printf(TEXT("RootScoutResult.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildRootScoutResultObligationId(const FLayoutRootScoutResultOrderingInput& Input)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("RootScoutObligation"));
		AppendOrderingStableName(Hash, Input.WorldBindingId);
		AppendOrderingStableName(Hash, Input.PlacementPolicyId);
		AppendOrderingStableName(Hash, Input.BiomeRowName);
		return FLayoutId(*FString::Printf(TEXT("RootScoutObligation.%016llX"), static_cast<unsigned long long>(Hash)));
	}

	FLayoutId BuildTerrainEvidenceOrderingId(const FLayoutFrozenTerrainBiomeAdapterInput& Artifact)
	{
		uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
		AppendOrderingStableString(Hash, TEXT("TerrainEvidence"));
		AppendOrderingStableInt(Hash, Artifact.SearchMinBlockXY.X);
		AppendOrderingStableInt(Hash, Artifact.SearchMinBlockXY.Y);
		AppendOrderingStableInt(Hash, Artifact.SearchMaxBlockXY.X);
		AppendOrderingStableInt(Hash, Artifact.SearchMaxBlockXY.Y);
		AppendOrderingStableInt(Hash, Artifact.SearchStartZBlockWorld);
		AppendOrderingStableInt(Hash, Artifact.SearchDepthBlocks);
		AppendOrderingStableName(Hash, Artifact.EligibleBiomeRowName);
		TArray<FName> BiomeRows = Artifact.EligibleBiomeRowNames;
		BiomeRows.Sort([](const FName Left, const FName Right)
		{
			return Left.LexicalLess(Right);
		});
		for (const FLayoutId BiomeRow : BiomeRows)
		{
			AppendOrderingStableName(Hash, BiomeRow);
		}
		return FLayoutId(*FString::Printf(TEXT("TerrainEvidence.%016llX"), static_cast<unsigned long long>(Hash)));
	}
}

FLayoutId FLayoutContractCandidateOrderingKey::BuildOrderingId() const
{
	uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
	AppendOrderingStableString(Hash, TEXT("ContractCandidateOrder"));
	AppendOrderingStableName(Hash, ManifestId);
	AppendOrderingStableInt(Hash, SolveSeed);
	AppendOrderingStableName(Hash, LocalObligationId);
	AppendOrderingStableName(Hash, CandidateId);
	AppendOrderingStableName(Hash, PlacementShiftId);
	AppendOrderingStableInt(Hash, AttemptIndex);
	AppendOrderingStableName(Hash, ChildCertificateId);
	AppendOrderingStableName(Hash, ChildWitnessOrderingId);
	AppendOrderingStableInt(Hash, static_cast<int64>(ChildCertificateInputHash));
	return MakeOrderingStableId(Hash);
}

bool FLayoutContractCandidateOrderingKey::LexicalLess(const FLayoutContractCandidateOrderingKey& Other) const
{
	if (const int32 ManifestCompare = CompareNames(ManifestId, Other.ManifestId); ManifestCompare != 0)
	{
		return ManifestCompare < 0;
	}
	if (SolveSeed != Other.SolveSeed)
	{
		return SolveSeed < Other.SolveSeed;
	}
	if (const int32 ObligationCompare = CompareNames(LocalObligationId, Other.LocalObligationId); ObligationCompare != 0)
	{
		return ObligationCompare < 0;
	}
	if (const int32 CandidateCompare = CompareNames(CandidateId, Other.CandidateId); CandidateCompare != 0)
	{
		return CandidateCompare < 0;
	}
	if (const int32 ShiftCompare = CompareNames(PlacementShiftId, Other.PlacementShiftId); ShiftCompare != 0)
	{
		return ShiftCompare < 0;
	}
	if (AttemptIndex != Other.AttemptIndex)
	{
		return AttemptIndex < Other.AttemptIndex;
	}
	if (const int32 CertificateCompare = CompareNames(ChildCertificateId, Other.ChildCertificateId); CertificateCompare != 0)
	{
		return CertificateCompare < 0;
	}
	if (const int32 WitnessCompare = CompareNames(ChildWitnessOrderingId, Other.ChildWitnessOrderingId); WitnessCompare != 0)
	{
		return WitnessCompare < 0;
	}
	return ChildCertificateInputHash < Other.ChildCertificateInputHash;
}

void FLayoutContractPlacementCandidates::SortCandidateKeys(TArray<FLayoutContractCandidateOrderingKey>& InOutKeys)
{
	InOutKeys.Sort([](const FLayoutContractCandidateOrderingKey& Left, const FLayoutContractCandidateOrderingKey& Right)
	{
		return Left.LexicalLess(Right);
	});
}

FLayoutId FLayoutContractPlacementCandidates::BuildPlacementShiftId(const FIntVector& ShiftCells)
{
	uint64 Hash = LayoutCandidateOrderFnvOffsetBasis;
	AppendOrderingStableString(Hash, TEXT("PlacementShift"));
	AppendOrderingStableInt(Hash, ShiftCells.X);
	AppendOrderingStableInt(Hash, ShiftCells.Y);
	AppendOrderingStableInt(Hash, ShiftCells.Z);
	return FLayoutId(*FString::Printf(TEXT("PlacementShift.%016llX"), static_cast<unsigned long long>(Hash)));
}

TArray<FLayoutCoarsePlacementShiftCandidate> FLayoutContractPlacementCandidates::BuildCoarsePlacementShiftCandidates(
	const FLayoutId ManifestId,
	const int32 SolveSeed,
	const FLayoutId LocalObligationId,
	const FIntPoint& FootprintSizeInCells,
	const int32 AttemptIndex,
	const int32 MaxRingCount)
{
	const int32 StepX = FMath::Max(1, FootprintSizeInCells.X);
	const int32 StepY = FMath::Max(1, FootprintSizeInCells.Y);
	const int32 RingCount = FMath::Max(0, MaxRingCount);

	TArray<FLayoutCoarsePlacementShiftCandidate> Candidates;
	Candidates.Reserve(1 + RingCount * 8);

	auto AddCandidate = [&Candidates, ManifestId, SolveSeed, LocalObligationId, AttemptIndex](const FIntVector& ShiftCells)
	{
		FLayoutCoarsePlacementShiftCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.ShiftCells = ShiftCells;
		Candidate.ShiftId = BuildPlacementShiftId(ShiftCells);
		Candidate.OrderingKey.ManifestId = ManifestId;
		Candidate.OrderingKey.SolveSeed = SolveSeed;
		Candidate.OrderingKey.LocalObligationId = LocalObligationId;
		Candidate.OrderingKey.CandidateId = Candidate.ShiftId;
		Candidate.OrderingKey.PlacementShiftId = Candidate.ShiftId;
		Candidate.OrderingKey.AttemptIndex = AttemptIndex;
	};

	AddCandidate(FIntVector::ZeroValue);
	for (int32 Ring = 1; Ring <= RingCount; ++Ring)
	{
		const int32 OffsetX = StepX * Ring;
		const int32 OffsetY = StepY * Ring;
		AddCandidate(FIntVector(OffsetX, 0, 0));
		AddCandidate(FIntVector(-OffsetX, 0, 0));
		AddCandidate(FIntVector(0, OffsetY, 0));
		AddCandidate(FIntVector(0, -OffsetY, 0));
		AddCandidate(FIntVector(OffsetX, OffsetY, 0));
		AddCandidate(FIntVector(OffsetX, -OffsetY, 0));
		AddCandidate(FIntVector(-OffsetX, OffsetY, 0));
		AddCandidate(FIntVector(-OffsetX, -OffsetY, 0));
	}

	Candidates.Sort([](const FLayoutCoarsePlacementShiftCandidate& Left, const FLayoutCoarsePlacementShiftCandidate& Right)
	{
		return Left.OrderingKey.LexicalLess(Right.OrderingKey);
	});
	return Candidates;
}

TArray<FLayoutCoarsePlacementShiftCandidate> FLayoutContractPlacementCandidates::BuildTerrainBackedCoarsePlacementShiftCandidates(
	const FLayoutId ManifestId,
	const int32 SolveSeed,
	const FLayoutFrozenTerrainBiomeAdapterInput& TerrainArtifact,
	const FIntVector& SharedCellSizeInBlocks,
	const int32 AttemptIndex,
	const int32 MaxRingCount)
{
	const int32 CellSizeX = FMath::Max(1, SharedCellSizeInBlocks.X);
	const int32 CellSizeY = FMath::Max(1, SharedCellSizeInBlocks.Y);
	const FIntPoint FootprintSizeInCells(
		FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(1, TerrainArtifact.FootprintSizeInBlocks.X), CellSizeX)),
		FMath::Max(1, FMath::DivideAndRoundUp(FMath::Max(1, TerrainArtifact.FootprintSizeInBlocks.Y), CellSizeY)));
	const FLayoutId ObligationId = !TerrainArtifact.ArtifactId.IsNone()
		? TerrainArtifact.ArtifactId
		: BuildTerrainEvidenceOrderingId(TerrainArtifact);
	const bool bHasRequiredBaseEvidence = HasTerrainBackedOrderingBaseEvidence(TerrainArtifact);
	if (!bHasRequiredBaseEvidence)
	{
		return TArray<FLayoutCoarsePlacementShiftCandidate>();
	}
	TArray<FLayoutCoarsePlacementShiftCandidate> Candidates = BuildCoarsePlacementShiftCandidates(
		ManifestId,
		SolveSeed,
		ObligationId,
		FootprintSizeInCells,
		AttemptIndex,
		MaxRingCount);
	Candidates.RemoveAll([&TerrainArtifact](const FLayoutCoarsePlacementShiftCandidate& Candidate)
	{
		return Candidate.ShiftCells != FIntVector::ZeroValue
			&& !HasUsableShiftedFootprintEvidenceForShift(
				TerrainArtifact,
				Candidate.ShiftCells,
				Candidate.ShiftId);
	});
	return Candidates;
}

FLayoutContractCandidateOrderingKey FLayoutContractPlacementCandidates::BuildChildBranchOrderingKey(
	const FLayoutChildBranchOrderingInput& Input)
{
	FLayoutContractCandidateOrderingKey Key;
	Key.ManifestId = Input.ParentContractId;
	Key.SolveSeed = Input.EarlyStructuralPriority;
	Key.LocalObligationId = BuildChildBranchObligationId(Input);
	Key.CandidateId = BuildChildBranchCandidateId(Input);
	Key.PlacementShiftId = Input.PlacementShiftId;
	Key.AttemptIndex = Input.AttemptIndex;
	Key.ChildCertificateId = Input.ChildCertificateId;
	Key.ChildWitnessOrderingId = Input.ChildWitnessOrderingId;
	Key.ChildCertificateInputHash = Input.ChildCertificateInputHash;
	return Key;
}

FLayoutContractCandidateOrderingKey FLayoutContractPlacementCandidates::BuildCompositeScoutOrderingKey(
	const FLayoutCompositeScoutOrderingInput& Input)
{
	FLayoutContractCandidateOrderingKey Key;
	Key.ManifestId = Input.ManifestId;
	Key.LocalObligationId = BuildCompositeScoutObligationId(Input);
	Key.CandidateId = BuildCompositeScoutCandidateId(Input);
	Key.PlacementShiftId = Input.PlacementShiftId;
	Key.AttemptIndex = Input.AttemptIndex;
	return Key;
}

FLayoutContractCandidateOrderingKey FLayoutContractPlacementCandidates::BuildSingleLevelSteppedAscentOrderingKey(
	const FLayoutSingleLevelSteppedAscentOrderingInput& Input)
{
	FLayoutContractCandidateOrderingKey Key;
	Key.ManifestId = Input.ManifestId;
	Key.LocalObligationId = BuildSingleLevelSteppedAscentObligationId(Input);
	Key.CandidateId = BuildSingleLevelSteppedAscentCandidateId(Input);
	Key.PlacementShiftId = Input.PlacementShiftId;
	Key.AttemptIndex = Input.AttemptIndex;
	return Key;
}

FLayoutContractCandidateOrderingKey FLayoutContractPlacementCandidates::BuildContinuationEdgeScoutOrderingKey(
	const FLayoutContinuationEdgeScoutOrderingInput& Input)
{
	FLayoutContractCandidateOrderingKey Key;
	Key.ManifestId = Input.ManifestId;
	Key.LocalObligationId = BuildContinuationEdgeScoutObligationId(Input);
	Key.CandidateId = BuildContinuationEdgeScoutCandidateId(Input);
	Key.PlacementShiftId = Input.PlacementShiftId;
	Key.AttemptIndex = Input.AttemptIndex;
	return Key;
}

FLayoutContractCandidateOrderingKey FLayoutContractPlacementCandidates::BuildRootScoutResultOrderingKey(
	const FLayoutRootScoutResultOrderingInput& Input)
{
	FLayoutContractCandidateOrderingKey Key;
	FString FailureReason;
	TryBuildRootScoutResultOrderingKey(Input, Key, FailureReason);
	return Key;
}

bool FLayoutContractPlacementCandidates::TryBuildRootScoutResultOrderingKey(
	const FLayoutRootScoutResultOrderingInput& Input,
	FLayoutContractCandidateOrderingKey& OutKey,
	FString& OutFailureReason)
{
	OutKey = FLayoutContractCandidateOrderingKey();
	OutFailureReason.Reset();
	if (Input.ManifestId.IsNone())
	{
		OutFailureReason = TEXT("Root scout ordering requires a stable manifest id.");
		return false;
	}
	if (Input.WorldBindingId.IsNone())
	{
		OutFailureReason = TEXT("Root scout ordering requires a stable world-binding id.");
		return false;
	}
	if (Input.ScoutResultId.IsNone())
	{
		OutFailureReason = TEXT("Root scout ordering requires a stable scout result id.");
		return false;
	}
	if (Input.RootCandidateId.IsNone())
	{
		OutFailureReason = TEXT("Root scout ordering requires a stable root candidate id.");
		return false;
	}
	if (Input.PlacementPolicyId.IsNone())
	{
		OutFailureReason = TEXT("Root scout ordering requires a stable placement policy id.");
		return false;
	}
	if (Input.BiomeRowName.IsNone())
	{
		OutFailureReason = TEXT("Root scout ordering requires a matching biome row id.");
		return false;
	}
	if (Input.AttemptIndex < 0)
	{
		OutFailureReason = TEXT("Root scout ordering requires a non-negative attempt index.");
		return false;
	}

	OutKey.ManifestId = Input.ManifestId;
	OutKey.SolveSeed = Input.SolveSeed;
	OutKey.LocalObligationId = BuildRootScoutResultObligationId(Input);
	OutKey.CandidateId = BuildRootScoutResultCandidateId(Input);
	OutKey.PlacementShiftId = Input.PlacementShiftId;
	OutKey.AttemptIndex = Input.AttemptIndex;
	return true;
}

int32 FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
	const FIntVector& FinalizedSiteCenterBlockWorldPos,
	const int32 WorldSeed)
{
	return FLayoutSiteReservation::ComputeSiteSolveSeed(FinalizedSiteCenterBlockWorldPos, WorldSeed);
}
