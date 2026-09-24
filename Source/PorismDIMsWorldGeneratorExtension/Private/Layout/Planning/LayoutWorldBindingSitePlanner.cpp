// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Solver/LayoutStandaloneRegionRequestBuilder.h"

#include "Biome/Noise/WorldGenScaleContext.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutRootSpacing.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Misc/Crc.h"

namespace
{
	struct FPendingPocketSiteCandidate
	{
		FPlannedLayoutSiteRecord Record;
		FIntPoint SiteCenterBlockXY = FIntPoint::ZeroValue;
		FName CandidateId = NAME_None;
	};

	FPlannedLayoutSiteRecord BuildPendingRootSiteRecordFromPrototype(
		FPlannedLayoutSiteRecord Record, const FIntVector& SharedCellSizeInBlocks,
		const FIntPoint ReservationKey, const FIntVector& SiteCenterBlockWorldPos, const int32 WorldSeed)
	{
		const FIntVector Center = FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
			SiteCenterBlockWorldPos, SharedCellSizeInBlocks);
		auto Reservation = Record.GetPlannedSiteReservationSourceSelection();
		Reservation.ReservationKey = ReservationKey;
		Reservation.SiteCenterBlockWorldPos = Center;
		Reservation.WorldSeed = WorldSeed;
		Record.SetPlannedSiteReservationSourceSelection(Reservation);
		auto Metadata = Record.GetPlannedSiteLifecycleMetadata();
		Metadata.StableRecordKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
			Record.GetWorldBindingFrontendSelection(), Reservation);
		Record.SetPlannedSiteLifecycleMetadata(Metadata);
		auto Publication = Record.GetRootPublicationMetadata();
		Publication.RootSolveId = FLayoutId(*Metadata.StableRecordKey);
		Record.SetRootPublicationMetadata(Publication);
		auto SolveSource = Record.GetSiteSolveSourceSelection();
		SolveSource.SolveSeed = FLayoutSiteReservation::ComputeSiteSolveSeed(Center, WorldSeed);
		Record.SetSiteSolveSourceSelection(SolveSource);
		return Record;
	}

	FPlannedLayoutSiteRecord BuildPendingOrdinaryRootSiteRecordInternal(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FName MatchingBiomeRowName, const FIntPoint ReservationKey,
		const FIntVector& SiteCenterBlockWorldPos, const int32 WorldSeed)
	{
		FPlannedLayoutSiteRecord Record;
		FLayoutPlannedSiteReservationSourceSelection Reservation;
		Reservation.WorldBinding = TSoftObjectPtr<ULayoutWorldBindingAsset>(const_cast<ULayoutWorldBindingAsset*>(WorldBinding));
		Record.SetPlannedSiteReservationSourceSelection(Reservation);
		FLayoutWorldBindingSiteFrontendSelection Frontend;
		Frontend.WorldBindingId = RuntimeView.BindingId;
		Frontend.WorldBindingCandidateId = RuntimeView.CandidateId;
		Frontend.ResolvedContinuationSelection = RuntimeView.ContinuationSelection;
		Frontend.BiomeRowName = MatchingBiomeRowName;
		Record.SetWorldBindingFrontendSelection(Frontend);
		Record.DiscoveryCandidateId = RuntimeView.CandidateId;
		FLayoutRootPublicationMetadata Publication;
		Publication.RootCandidateId = RuntimeView.CandidateId;
		Publication.RootPlacementPolicyId = RuntimeView.BindingId;
		Record.SetRootPublicationMetadata(Publication);
		FLayoutSiteSolveSourceSelection SolveSource;
		SolveSource.LayoutProfile = RuntimeView.LayoutProfile;
		SolveSource.ContentSet = RuntimeView.ContentSet;
		SolveSource.ExportedConnectorTypeTags = RuntimeView.ExportedConnectorTypeTags;
		Record.SetSiteSolveSourceSelection(SolveSource);
		return BuildPendingRootSiteRecordFromPrototype(MoveTemp(Record), RuntimeView.SharedCellSizeInBlocks,
			ReservationKey, SiteCenterBlockWorldPos, WorldSeed);
	}

	EAxisBehavior GetWorldGenAxisBehavior(const UWorldGenDef* const WorldGenDef, const int32 Axis)
	{
		if (WorldGenDef == nullptr)
		{
			return EAxisBehavior::Infinity;
		}

		return Axis == 0
			? WorldGenDef->AxisBehaviorX
			: (Axis == 1 ? WorldGenDef->AxisBehaviorY : WorldGenDef->AxisBehaviorZ);
	}

	bool IsFiniteAxisBehavior(const EAxisBehavior AxisBehavior)
	{
		return AxisBehavior != EAxisBehavior::Infinity;
	}

	double GetVectorAxis(const FVector& Value, const int32 Axis)
	{
		return Axis == 0 ? Value.X : (Axis == 1 ? Value.Y : Value.Z);
	}

	void SetIntVectorAxis(FIntVector& Value, const int32 Axis, const int32 Component)
	{
		if (Axis == 0)
		{
			Value.X = Component;
		}
		else if (Axis == 1)
		{
			Value.Y = Component;
		}
		else
		{
			Value.Z = Component;
		}
	}
}

namespace LayoutWorldBindingSitePlanner
{
	TArray<FIntPoint> BuildBoundedNormalCellSiteCenters(const FIntPoint Min, const FIntPoint Max,
		const FIntVector CellSize, const FIntPoint SpacingInCells, const float JitterFraction,
		const int32 WorldSeed, const FName BindingId)
	{
		TArray<FIntPoint> Sites;
		if (CellSize.X <= 0 || CellSize.Y <= 0 || SpacingInCells.X <= 0 || SpacingInCells.Y <= 0
			|| Min.X > Max.X || Min.Y > Max.Y || !FMath::IsFinite(JitterFraction)
			|| JitterFraction < 0.0f || JitterFraction > 1.0f) return Sites;
		const int64 FirstCellX = FMath::CeilToInt64(double(Min.X) / CellSize.X);
		const int64 FirstCellY = FMath::CeilToInt64(double(Min.Y) / CellSize.Y);
		const int64 LastCellX = FMath::FloorToInt64(double(Max.X) / CellSize.X);
		const int64 LastCellY = FMath::FloorToInt64(double(Max.Y) / CellSize.Y);
		if (FirstCellX > LastCellX || FirstCellY > LastCellY) return Sites;
		const auto Bucket = [](const int64 Cell, const int32 Pitch) { return Cell / Pitch - (Cell % Pitch < 0); };
		const int64 FirstX = Bucket(FirstCellX, SpacingInCells.X), LastX = Bucket(LastCellX, SpacingInCells.X);
		const int64 FirstY = Bucket(FirstCellY, SpacingInCells.Y), LastY = Bucket(LastCellY, SpacingInCells.Y);
		const int64 Width = LastX - FirstX + 1, Height = LastY - FirstY + 1;
		if (Width > 4 || Height > 4) return Sites;
		const bool bJitter = JitterFraction > 0.0f && (SpacingInCells.X > 1 || SpacingInCells.Y > 1);
		const FString BindingText = bJitter ? BindingId.ToString().ToLower() : FString();
		const auto Offset = [JitterFraction](const int32 Pitch, const uint32 Hash)
		{
			const int64 Draw = (uint64(Hash) * uint64(Pitch)) >> 32;
			return FMath::RoundToInt64(FMath::Lerp(double(Pitch - 1) * 0.5, double(Draw), double(JitterFraction)));
		};
		Sites.Reserve(int32(Width * Height));
		for (int64 Y = FirstY; Y <= LastY; ++Y)
		for (int64 X = FirstX; X <= LastX; ++X)
		{
			uint32 HashX = 0, HashY = 0;
			if (bJitter)
			{
				const FString Identity = FString::Printf(TEXT("LayoutSiteJitter|%d|%s|%lld|%lld"), WorldSeed, *BindingText, X, Y);
				HashX = FCrc::StrCrc32(*Identity, 0x58u);
				HashY = FCrc::StrCrc32(*Identity, 0x59u);
			}
			const int64 CellX = X * SpacingInCells.X + Offset(SpacingInCells.X, HashX);
			const int64 CellY = Y * SpacingInCells.Y + Offset(SpacingInCells.Y, HashY);
			// Check final ownership before multiplication/narrowing; products now fit authored int32 bounds.
			if (CellX < FirstCellX || CellX > LastCellX || CellY < FirstCellY || CellY > LastCellY) continue;
			Sites.Add(FIntPoint(int32(CellX * CellSize.X), int32(CellY * CellSize.Y)));
		}
		return Sites;
	}

	bool PassesOccupancy(const int32 WorldSeed, const FName BindingId, const FIntVector SnappedSite, const float Probability)
	{
		if (!FMath::IsFinite(Probability) || Probability <= 0.0f || Probability > 1.0f) return false;
		if (Probability == 1.0f) return true;
		const FString Identity = FString::Printf(TEXT("LayoutOccupancy|%d|%s|%d|%d|%d"),
			WorldSeed, *BindingId.ToString().ToLower(), SnappedSite.X, SnappedSite.Y, SnappedSite.Z);
		return double(FCrc::StrCrc32(*Identity)) < double(Probability) * 4294967296.0;
	}

	int32 ResolveOrdinaryRootSiteCenterZ(
		const int32 SharedCellHeightInBlocks,
		const int32 TerrainTopSurfaceZ)
	{
		if (SharedCellHeightInBlocks <= 0)
		{
			return TerrainTopSurfaceZ;
		}

		const int64 LatticeIndex = FMath::CeilToInt64(
			static_cast<double>(TerrainTopSurfaceZ) / static_cast<double>(SharedCellHeightInBlocks));
		return static_cast<int32>(LatticeIndex * static_cast<int64>(SharedCellHeightInBlocks));
	}

	int32 ResolveOrdinaryRootSiteCenterZ(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const int32 TerrainTopSurfaceZ)
	{
		return ResolveOrdinaryRootSiteCenterZ(
			WorldBinding != nullptr ? WorldBinding->BaseCellDimensionsBlocks.Z : 0,
			TerrainTopSurfaceZ);
	}

	bool TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
		const int32 SharedCellHeightInBlocks,
		const int32 TerrainTopSurfaceZ,
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		int32& OutSiteCenterZ)
	{
		const int32 SnappedSiteCenterZ =
			ResolveOrdinaryRootSiteCenterZ(SharedCellHeightInBlocks, TerrainTopSurfaceZ);
		OutSiteCenterZ = SnappedSiteCenterZ;
		if (SharedCellHeightInBlocks <= 0 || !Bounds.bHasFiniteZ)
		{
			return true;
		}

		const int64 MinLatticeIndex = FMath::CeilToInt64(
			static_cast<double>(Bounds.MinInclusive.Z) / static_cast<double>(SharedCellHeightInBlocks));
		const int64 MaxLatticeIndex = FMath::FloorToInt64(
			static_cast<double>(Bounds.MaxInclusive.Z) / static_cast<double>(SharedCellHeightInBlocks));
		if (MinLatticeIndex > MaxLatticeIndex)
		{
			return false;
		}

		const int64 SnappedLatticeIndex =
			static_cast<int64>(SnappedSiteCenterZ) / static_cast<int64>(SharedCellHeightInBlocks);
		if (SnappedLatticeIndex > MaxLatticeIndex)
		{
			return false;
		}

		const int64 ConstrainedLatticeIndex =
			FMath::Max(SnappedLatticeIndex, MinLatticeIndex);
		OutSiteCenterZ = static_cast<int32>(
			ConstrainedLatticeIndex * static_cast<int64>(SharedCellHeightInBlocks));
		return true;
	}

	bool TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const int32 TerrainTopSurfaceZ,
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		int32& OutSiteCenterZ)
	{
		return TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
			WorldBinding != nullptr ? WorldBinding->BaseCellDimensionsBlocks.Z : 0,
			TerrainTopSurfaceZ,
			Bounds,
			OutSiteCenterZ);
	}

	/** Resolves a Planning Window site from bounded procedural occupancy before pending-record publication. */
	bool TryResolvePlanningWindowEnvironmentSiteCenterZ(
		const bool bUnderground,
		const FIntPoint BlockXY,
		const int32 SharedCellHeightInBlocks,
		const int32 RequiredLayoutHeightInBlocks,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		const int32 GeneratedSurfaceZ,
		const int32 PreferredSiteZ,
		int32& OutSiteCenterZ,
		bool& bOutUsedFinalOccupancy)
	{
		bOutUsedFinalOccupancy = false;
		if (SharedCellHeightInBlocks <= 0 || RequiredLayoutHeightInBlocks <= 0)
		{
			return false;
		}
		const int32 MinZ = SurfaceSearch.TerrainSearchStartZ - SurfaceSearch.TerrainSearchDepthBlocks + 1;
		// Include the full snapped layout envelope above the discovered surface;
		// a short search window must not truncate otherwise valid open-sky clearance.
		const int32 MaxZ = FMath::Max(
			SurfaceSearch.TerrainSearchStartZ + FMath::Max(
				SurfaceSearch.TerrainSearchDepthBlocks,
				RequiredLayoutHeightInBlocks + SharedCellHeightInBlocks),
			GeneratedSurfaceZ + RequiredLayoutHeightInBlocks + SharedCellHeightInBlocks);
		TArray<FLayoutTerrainColumnProfile> Profiles;
		FString FailureReason;
		if (!FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromActiveBiomeSampler(
				FIntVector(BlockXY.X, BlockXY.Y, MinZ),
				1,
				1,
				MinZ,
				MaxZ,
				CoordinateSettings,
				ActiveBiomeSampler,
				Profiles,
				FailureReason))
		{
			return false;
		}
		if (Profiles.Num() != 1)
		{
			return false;
		}

		const FLayoutTerrainColumnProfile& Profile = Profiles[0];
		if (!bUnderground)
		{
			if (Profile.Runs.Num() < 2
				|| !Profile.Runs.Last().bIsEmpty
				|| Profile.Runs[Profile.Runs.Num() - 2].bIsEmpty)
			{
				// Preserve the existing sampler's bounded all-solid result, but never
				// reinterpret an observed bounded cavity as open-sky surface clearance.
				return Profile.Runs.Num() == 1 && !Profile.Runs[0].bIsEmpty
					&& TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
						SharedCellHeightInBlocks, GeneratedSurfaceZ + 1, Bounds, OutSiteCenterZ);
			}
			const FLayoutTerrainColumnRun& OpenSkyRun = Profile.Runs.Last();
			const int32 SupportSurfaceZ = Profile.Runs[Profile.Runs.Num() - 2].MaxZ;
			int32 CandidateZ = 0;
			if (!TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
					SharedCellHeightInBlocks,
					SupportSurfaceZ + 1,
					Bounds,
					CandidateZ)
				|| CandidateZ < OpenSkyRun.MinZ
				|| static_cast<int64>(CandidateZ) + RequiredLayoutHeightInBlocks - 1 > OpenSkyRun.MaxZ)
			{
				return false;
			}
			OutSiteCenterZ = CandidateZ;
			bOutUsedFinalOccupancy = true;
			return true;
		}

		bool bFound = false;
		int32 BestCandidateZ = 0;
		int64 BestDistance = MAX_int64;
		for (int32 RunIndex = 1; RunIndex + 1 < Profile.Runs.Num(); ++RunIndex)
		{
			const FLayoutTerrainColumnRun& Run = Profile.Runs[RunIndex];
			if (!Run.bIsEmpty
				|| Profile.Runs[RunIndex - 1].bIsEmpty
				|| Profile.Runs[RunIndex + 1].bIsEmpty)
			{
				continue;
			}
			int32 CandidateZ = 0;
			if (!TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
					SharedCellHeightInBlocks,
					Run.MinZ,
					Bounds,
					CandidateZ)
				|| CandidateZ < Run.MinZ
				|| static_cast<int64>(CandidateZ) + RequiredLayoutHeightInBlocks - 1 > Run.MaxZ)
			{
				continue;
			}
			const int64 Distance = PreferredSiteZ != INDEX_NONE
				? FMath::Abs(static_cast<int64>(CandidateZ) - PreferredSiteZ)
				: static_cast<int64>(MaxZ) - CandidateZ;
			if (!bFound || Distance < BestDistance || (Distance == BestDistance && CandidateZ > BestCandidateZ))
			{
				bFound = true;
				BestCandidateZ = CandidateZ;
				BestDistance = Distance;
			}
		}
		if (bFound)
		{
			OutSiteCenterZ = BestCandidateZ;
			bOutUsedFinalOccupancy = true;
		}
		return bFound;
	}

	bool TryResolveChunkWorldFiniteAxisBlockBounds(
		const AChunkWorldExtended* const ChunkWorld,
		FChunkWorldFiniteAxisBlockBounds& OutBounds)
	{
		OutBounds = FChunkWorldFiniteAxisBlockBounds();
		if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
		{
			return false;
		}

		const FResolvedWorldGenScaleContext ScaleContext =
			FWorldGenScaleContextResolver::Resolve(
				const_cast<AChunkWorldExtended*>(ChunkWorld),
				nullptr,
				false);
		const FVector RawMinBlock =
			ScaleContext.AuthoredBlockPositionToRawBlock(ScaleContext.AuthoredMinBlock);
		const FVector RawMaxBlock =
			ScaleContext.AuthoredBlockPositionToRawBlock(ScaleContext.AuthoredMaxBlock);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const bool bFiniteAxis =
				IsFiniteAxisBehavior(GetWorldGenAxisBehavior(ChunkWorld->WorldGenDef, Axis));
			if (!bFiniteAxis)
			{
				continue;
			}

			const int32 MinInclusive = FMath::CeilToInt(GetVectorAxis(RawMinBlock, Axis));
			const int32 MaxInclusive =
				FMath::CeilToInt(GetVectorAxis(RawMaxBlock, Axis)) - 1;
			if (Axis == 0)
			{
				OutBounds.bHasFiniteX = true;
			}
			else if (Axis == 1)
			{
				OutBounds.bHasFiniteY = true;
			}
			else
			{
				OutBounds.bHasFiniteZ = true;
			}

			SetIntVectorAxis(OutBounds.MinInclusive, Axis, MinInclusive);
			SetIntVectorAxis(OutBounds.MaxInclusive, Axis, MaxInclusive);
		}

		return true;
	}

	bool IsBlockWorldPosInsideFiniteAxisBounds(
		const FChunkWorldFiniteAxisBlockBounds& Bounds,
		const FIntVector& BlockWorldPos)
	{
		return (!Bounds.bHasFiniteX
				|| (BlockWorldPos.X >= Bounds.MinInclusive.X
					&& BlockWorldPos.X <= Bounds.MaxInclusive.X))
			&& (!Bounds.bHasFiniteY
				|| (BlockWorldPos.Y >= Bounds.MinInclusive.Y
					&& BlockWorldPos.Y <= Bounds.MaxInclusive.Y))
			&& (!Bounds.bHasFiniteZ
				|| (BlockWorldPos.Z >= Bounds.MinInclusive.Z
					&& BlockWorldPos.Z <= Bounds.MaxInclusive.Z));
	}

	bool ValidateDiscoveredEnvironmentEvidence(
		const FPlannedLayoutSiteRecord& PlannedSiteRecord,
		const bool bPrepared,
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainArtifact,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (!PlannedSiteRecord.bHasDiscoveredEnvironmentMode
			|| !PlannedSiteRecord.bEnvironmentDiscoveryQualifiedProceduralOccupancy)
		{
			return true;
		}
		if (!bPrepared || !TerrainArtifact.bHasRelativeEnvironmentClassification)
		{
			OutFailureReason = TEXT("Planning Window final-occupancy candidate lost environment evidence before prewarm.");
			return false;
		}
		if (TerrainArtifact.bIsClassifiedUnderground != PlannedSiteRecord.bDiscoveredUnderground)
		{
			OutFailureReason = FString::Printf(
				TEXT("Planning Window candidate environment changed before prewarm: expected=%s observed=%s."),
				PlannedSiteRecord.bDiscoveredUnderground ? TEXT("Underground") : TEXT("Surface"),
				TerrainArtifact.bIsClassifiedUnderground ? TEXT("Underground") : TEXT("Surface"));
			return false;
		}
		return true;
	}

	bool TrySelectOrdinaryRootRuntimeViewForSite(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FName MatchingBiomeRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 WorldSeed,
		FLayoutWorldBindingRuntimeView& OutRuntimeView,
		FString& OutFailureReason)
	{
		FLayoutWorldBindingCandidateTarget CandidateTarget;
		if (!LayoutWorldBindingRuntimeView::TryResolveWorldBindingCandidateTarget(
			WorldBinding,
			SiteCenterBlockWorldPos,
			WorldSeed,
			CandidateTarget,
			OutFailureReason))
		{
			OutRuntimeView = FLayoutWorldBindingRuntimeView();
			return false;
		}

		return LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingCandidateTarget(
			WorldBinding,
			CandidateTarget,
			MatchingBiomeRowName,
			OutRuntimeView,
			OutFailureReason);
	}

	FSitePlanningSnapshot CaptureSitePlanningInputs(
		const ULayoutWorldBindingAsset* WorldBinding, const FName MatchingBiomeRowName,
		const int32 WorldSeed,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const int32 RootReferenceZ, AChunkWorldExtended* ChunkWorld)
	{
		check(IsInGameThread());
		FSitePlanningSnapshot Inputs;
		if (WorldBinding == nullptr) return Inputs;
		Inputs.BindingId = WorldBinding->BindingId.IsNone() ? WorldBinding->GetFName() : WorldBinding->BindingId;
		Inputs.MatchingBiomeRowName = MatchingBiomeRowName;
		Inputs.WorldSeed = WorldSeed;
		Inputs.SharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
		Inputs.MinimumRootGapCells = WorldBinding->MinimumRootGapCells;
		Inputs.OccupancyProbability = WorldBinding->OccupancyProbability;
		Inputs.SurfaceSearch = SurfaceSearch;
		Inputs.CoordinateSettings = CoordinateSettings;
		Inputs.RootReferenceZ = RootReferenceZ;
		Inputs.bQualifyEnvironment = ChunkWorld != nullptr;
		if (ChunkWorld != nullptr) TryResolveChunkWorldFiniteAxisBlockBounds(ChunkWorld, Inputs.FiniteAxisBounds);
		for (int32 Index = 0; Index < WorldBinding->Candidates.Num(); ++Index)
		{
			auto& Candidate = Inputs.Candidates.AddDefaulted_GetRef();
			Candidate.Weight = WorldBinding->Candidates[Index].Weight;
			FLayoutWorldBindingCandidateTarget Target;
			Target.CandidateIndex = Index;
			Target.Candidate = &WorldBinding->Candidates[Index];
			FLayoutWorldBindingRuntimeView View;
			FString Failure;
			Candidate.bValid = LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingCandidateTarget(
				WorldBinding, Target, MatchingBiomeRowName, View, Failure);
			if (!Candidate.bValid) continue;
			Candidate.Prototype = BuildPendingOrdinaryRootSiteRecordInternal(WorldBinding, View, MatchingBiomeRowName,
				FIntPoint::ZeroValue, FIntVector::ZeroValue, WorldSeed);
			Candidate.CandidateId = View.CandidateId;
			Candidate.SharedCellSizeInBlocks = View.SharedCellSizeInBlocks;
			Candidate.FootprintProfile.MinimumFootprintInCells = View.LayoutProfile->MinimumFootprintInCells;
			Candidate.FootprintProfile.MaximumFootprintInCells = View.LayoutProfile->MaximumFootprintInCells;
			Candidate.LevelCount = View.LayoutProfile->LevelCount;
			Candidate.bUnderground = View.LayoutProfile->bUndergroundPlacement;
			Candidate.PlacementPolicy = View.PlacementPolicy;
		}
		return Inputs;
	}

	TArray<FPlannedLayoutSiteRecord> BuildPendingSiteRecordsFromPockets(
		const TArray<FLayoutReservationPocket>& Pockets, const FSitePlanningSnapshot& Inputs,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler, TSet<FString>* OutBlockingReservations, int32* OutOccupancyRejected,
		int32* OutOwnerRejected)
	{
		if (OutBlockingReservations) OutBlockingReservations->Reset();
		if (OutOccupancyRejected) *OutOccupancyRejected = 0;
		if (OutOwnerRejected) *OutOwnerRejected = 0;
		TArray<FPlannedLayoutSiteRecord> Records;
		if (Inputs.Candidates.IsEmpty()) return Records;
		const FName MatchingBiomeRowName = Inputs.MatchingBiomeRowName;
		const int32 WorldSeed = Inputs.WorldSeed;
		const int32 RootReferenceZ = Inputs.RootReferenceZ;
		const auto& SurfaceSearch = Inputs.SurfaceSearch;
		const auto& CoordinateSettings = Inputs.CoordinateSettings;
		const auto SelectCandidate = [&](const FIntVector& Center) -> const FPlanningCandidateSnapshot*
		{
			const int32 Index = LayoutWorldBindingRuntimeView::ChooseWeightedWorldBindingCandidateIndex(
				Inputs.BindingId, Inputs.Candidates, Center, WorldSeed);
			return Inputs.Candidates.IsValidIndex(Index) && Inputs.Candidates[Index].bValid ? &Inputs.Candidates[Index] : nullptr;
		};

		TArray<FPendingPocketSiteCandidate> PendingCandidates;
		for (const FLayoutReservationPocket& Pocket : Pockets)
		{
			for (const FIntPoint& SampleBlockXY : Pocket.SampleBlockXYs)
			{
				FLayoutActiveBiomeSurfaceSample Surface;
				if (!ActiveBiomeSampler.FindEligibleBiomeSurface(
					MatchingBiomeRowName,
					SampleBlockXY,
					SurfaceSearch.TerrainSearchStartZ,
					SurfaceSearch.TerrainSearchDepthBlocks,
					CoordinateSettings,
					Surface)
					|| !Surface.bIsValid)
				{
					continue;
				}

				// Candidate selection starts from the eligible surface; profile mode then selects procedural column evidence.
				const int32 SurfaceCandidateRootZ = ResolveOrdinaryRootSiteCenterZ(
					Inputs.SharedCellSizeInBlocks.Z,
					Surface.SurfaceBlockWorldPos.Z + 1);
				const int32 CandidateRootZ = !Inputs.bQualifyEnvironment && RootReferenceZ != INDEX_NONE
					? RootReferenceZ
					: SurfaceCandidateRootZ;
				FIntVector SiteCenterBlockWorldPos(SampleBlockXY.X, SampleBlockXY.Y, CandidateRootZ);

				const FPlanningCandidateSnapshot* Candidate = SelectCandidate(SiteCenterBlockWorldPos);
				if (Candidate == nullptr) continue;
				bool bUsedFinalOccupancy = false;
				if (Inputs.bQualifyEnvironment)
				{
					const int32 RequiredLayoutHeightInBlocks =
						FMath::Max(1, Candidate->LevelCount)
						* Candidate->SharedCellSizeInBlocks.Z;
					if (!TryResolvePlanningWindowEnvironmentSiteCenterZ(
							Candidate->bUnderground,
							SampleBlockXY,
							Candidate->SharedCellSizeInBlocks.Z,
							RequiredLayoutHeightInBlocks,
							Candidate->PlacementPolicy.SurfaceSearch,
							CoordinateSettings,
							ActiveBiomeSampler,
							Inputs.FiniteAxisBounds,
							Surface.SurfaceBlockWorldPos.Z,
							RootReferenceZ,
							SiteCenterBlockWorldPos.Z,
							bUsedFinalOccupancy))
					{
						continue;
					}
				}
				SiteCenterBlockWorldPos =
					FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
						SiteCenterBlockWorldPos,
						Candidate->SharedCellSizeInBlocks);
				if (!Inputs.bQualifyEnvironment)
				{
					Candidate = SelectCandidate(SiteCenterBlockWorldPos);
					if (Candidate == nullptr) continue;
				}
				if (Inputs.CandidateCenterBoundsInBlocks.IsValid
					&& !Inputs.CandidateCenterBoundsInBlocks.IsInsideOrOn(FVector(SiteCenterBlockWorldPos)))
				{
					if (OutOwnerRejected) ++*OutOwnerRejected;
					continue;
				}
				if (!PassesOccupancy(WorldSeed, Inputs.BindingId, SiteCenterBlockWorldPos, Inputs.OccupancyProbability))
				{
					if (OutOccupancyRejected) ++*OutOccupancyRejected;
					continue;
				}
				const FIntPoint ReservationKey = FLayoutSiteReservation::ComputeReservationKey(SiteCenterBlockWorldPos);

				FPlannedLayoutSiteRecord Record = BuildPendingRootSiteRecordFromPrototype(
					Candidate->Prototype, Candidate->SharedCellSizeInBlocks, ReservationKey, SiteCenterBlockWorldPos, WorldSeed);
				// Captured reservations can reject before footprint sampling, but never reserve
				// space against another speculative candidate. Runtime rechecks before submission.
				FLayoutRootSpacingReservation SpacingBounds;
				if (!FLayoutRootSpacingReservation::TryBuild(Inputs.BindingId, SiteCenterBlockWorldPos,
					Candidate->FootprintProfile.MaximumFootprintInCells, Candidate->SharedCellSizeInBlocks, SpacingBounds)) continue;
				bool bBlocked = false;
				if (Inputs.RootReservations)
					for (const auto& Pair : *Inputs.RootReservations)
						if (!SpacingBounds.IsSeparatedFrom(Pair.Value, Inputs.MinimumRootGapCells, Inputs.SharedCellSizeInBlocks))
						{
							if (OutBlockingReservations) OutBlockingReservations->Add(Pair.Key);
							bBlocked = true;
							break;
						}
				if (bBlocked) continue;
				// Match request footprint selection, then check cell columns and outer corners before admission.
				const FIntPoint Footprint = LayoutStandaloneRegionRequestBuilder::SelectFootprintSize(
					Candidate->FootprintProfile, Record.GetSiteSolveSourceSelection().SolveSeed);
				if (Footprint.X <= 0 || Footprint.Y <= 0) continue;
				const FIntVector CellSize = Candidate->SharedCellSizeInBlocks;
				const FIntVector FootprintMin = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(SiteCenterBlockWorldPos, Footprint, CellSize);
				const auto HasColumnOwnership = [&](const FIntPoint XY)
				{
					if (Candidate->bUnderground)
					{
						// Cavity classification/height was settled above; do not substitute an exterior surface.
						FLayoutActiveBiomeSample Sample;
						return ActiveBiomeSampler.SampleAtBlockPosition(FIntVector(XY.X, XY.Y, SiteCenterBlockWorldPos.Z - 1), CoordinateSettings, Sample)
							&& Sample.bIsValid && Sample.bAnyPositiveDomain
							&& (Sample.WinningRow.RowName == MatchingBiomeRowName || FLayoutId(*Sample.WinningRow.BiomeName) == MatchingBiomeRowName);
					}
					FLayoutActiveBiomeSurfaceSample Column;
					return ActiveBiomeSampler.FindEligibleBiomeSurface(MatchingBiomeRowName, XY,
						Candidate->PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
						Candidate->PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, CoordinateSettings, Column)
						&& Column.bIsValid;
				};
				const FIntPoint MinXY(FootprintMin.X, FootprintMin.Y);
				const FIntPoint MaxXY(FootprintMin.X + Footprint.X * CellSize.X - 1, FootprintMin.Y + Footprint.Y * CellSize.Y - 1);
				bool bOwnedFootprint = HasColumnOwnership(MinXY) && HasColumnOwnership(MaxXY)
					&& HasColumnOwnership(FIntPoint(MinXY.X, MaxXY.Y)) && HasColumnOwnership(FIntPoint(MaxXY.X, MinXY.Y));
				for (int32 Y = 0; Y < Footprint.Y && bOwnedFootprint; ++Y)
					for (int32 X = 0; X < Footprint.X && bOwnedFootprint; ++X)
						bOwnedFootprint = HasColumnOwnership(FIntPoint(FootprintMin.X + X * CellSize.X + CellSize.X / 2,
							FootprintMin.Y + Y * CellSize.Y + CellSize.Y / 2));
				if (!bOwnedFootprint) continue;

				FPendingPocketSiteCandidate& PendingCandidate = PendingCandidates.AddDefaulted_GetRef();
				PendingCandidate.SiteCenterBlockXY = SampleBlockXY;
				PendingCandidate.CandidateId = Candidate->CandidateId;
				PendingCandidate.Record = MoveTemp(Record);
				PendingCandidate.Record.bHasDiscoveredEnvironmentMode = Inputs.bQualifyEnvironment;
				PendingCandidate.Record.bDiscoveredUnderground =
					Candidate->bUnderground;
				PendingCandidate.Record.bEnvironmentDiscoveryQualifiedProceduralOccupancy = bUsedFinalOccupancy;
			}
		}

		PendingCandidates.Sort([](const FPendingPocketSiteCandidate& Left, const FPendingPocketSiteCandidate& Right)
		{
			if (Left.SiteCenterBlockXY.Y != Right.SiteCenterBlockXY.Y)
			{
				return Left.SiteCenterBlockXY.Y < Right.SiteCenterBlockXY.Y;
			}

			if (Left.SiteCenterBlockXY.X != Right.SiteCenterBlockXY.X)
			{
				return Left.SiteCenterBlockXY.X < Right.SiteCenterBlockXY.X;
			}

			return Left.CandidateId.LexicalLess(Right.CandidateId);
		});

		// Discovery proposes sites; only runtime reservations may spend spacing. Keep
		// overlapping alternatives in order so rejecting one cannot erase another.
		TSet<FString> SeenKeys;
		Records.Reserve(PendingCandidates.Num());
		for (FPendingPocketSiteCandidate& PendingCandidate : PendingCandidates)
		{
			const FString& Key = PendingCandidate.Record.StableRecordKey;
			if (SeenKeys.Contains(Key)) continue;
			SeenKeys.Add(Key);
			Records.Add(MoveTemp(PendingCandidate.Record));
		}

		return Records;
	}
}
