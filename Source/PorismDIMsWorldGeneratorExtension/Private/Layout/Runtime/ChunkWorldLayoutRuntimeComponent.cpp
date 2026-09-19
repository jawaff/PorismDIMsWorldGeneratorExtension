// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Engine/DataTable.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#if WITH_EDITOR
#include "Debug/DebugDrawService.h"
#include "Editor.h"
#include "Engine/Canvas.h"
#include "Engine/Blueprint.h"
#include "SceneView.h"
#include "SceneInterface.h"
#include "UObject/UObjectGlobals.h"
#endif
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Crc.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Internationalization/Regex.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Async/LayoutActiveBiomeNoiseSnapshot.h"
#include "Layout/Async/LayoutBackgroundLifecycleSequencer.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutSolveExecution.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Layout/Solver/LayoutSolveDiagnostics.h"
#include "Layout/Async/LayoutBackgroundSolveSnapshot.h"
#include "Layout/Async/LayoutFrozenSubmissionDescriptorProducer.h"
#include "Layout/Async/LayoutFrozenSubmissionStore.h"
#include "Layout/Async/LayoutPreSubmitFrozenSnapshot.h"
#include "Layout/Async/LayoutRegionPrewarmDescriptorProducer.h"
#include "Layout/Async/LayoutSolvedArtifact.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Contracts/LayoutContractPlacementCandidates.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutReservationPocketPlanning.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Runtime/LayoutPlanningAreaQueue.h"
#include "Layout/Solver/LayoutStandaloneRegionRequestBuilder.h"
#include "Misc/ScopeExit.h"
#include "Layout/Runtime/LayoutRealizationWritePlan.h"
#include "Layout/Solver/LayoutPlacementOccupancy.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Solver/LayoutZoneFeatureDemand.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Terrain/LayoutWorldBindingTerrainFit.h"
#include "Layout/Types/LayoutGameplayTags.h"

#if WITH_EDITOR
#endif

namespace
{
	const FLayoutId DirectRootExplicitPlacementPolicyId(TEXT("DirectRootExplicit"));
#if WITH_AUTOMATION_TESTS
	double GLastSelectedSiteTerrainMilliseconds = 0.0;
#endif


	int32 CountVerticalAccessPlacements(const FLayoutSolveResult& SolveResult)
	{
		int32 VerticalAccessPlacementCount = 0;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			if (Placement.Intent == ELayoutCellIntent::VerticalAccess)
			{
				++VerticalAccessPlacementCount;
			}
		}

		return VerticalAccessPlacementCount;
	}

	int32 CountVerticalAccessPlacements(const FLayoutRegionSolveResult& RegionResult)
	{
		return CountVerticalAccessPlacements(RegionResult.SolveResult);
	}

	bool IsPlacementlessWorldFacingSolveResult(const FLayoutSolveResult& SolveResult)
	{
		return SolveResult.bSucceeded
			&& SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
			&& SolveResult.Placements.IsEmpty();
	}

	FString BuildPlacementlessWorldFacingSolveFailureReason(const FLayoutSolveResult& SolveResult)
	{
		return SolveResult.FailureReason.IsEmpty()
			? TEXT("Explicit-root solve produced no placements and cannot be published or applied.")
			: FString::Printf(
				TEXT("Explicit-root solve produced no placements and cannot be published or applied. Prior solver detail: %s"),
				*SolveResult.FailureReason);
	}

	void InvalidatePlacementlessWorldFacingScheduleResult(
		FLayoutRegionSolveScheduleResult& InOutScheduleResult,
		const FString& RootRegionDebugPath)
	{
		if (!IsPlacementlessWorldFacingSolveResult(InOutScheduleResult.MergedSolveResult))
		{
			return;
		}

		const FString FailureReason =
			BuildPlacementlessWorldFacingSolveFailureReason(
				InOutScheduleResult.MergedSolveResult);
		InOutScheduleResult.bSucceeded = false;
		InOutScheduleResult.FailureReason = FailureReason;
		InOutScheduleResult.MergedSolveResult.bSucceeded = false;
		InOutScheduleResult.MergedSolveResult.FailureReason = FailureReason;
		if (FLayoutRegionSolveResult* RootRegionResult =
				InOutScheduleResult.RegionResults.FindByPredicate(
					[&RootRegionDebugPath](const FLayoutRegionSolveResult& RegionResult)
					{
						return RegionResult.RegionDebugPath == RootRegionDebugPath;
					}))
		{
			RootRegionResult->SolveResult.bSucceeded = false;
			RootRegionResult->SolveResult.FailureReason = FailureReason;
		}
	}

	const TCHAR* DescribeHostVerticalAccessResponsibility(
		const ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility)
	{
		switch (Responsibility)
		{
		case ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned:
			return TEXT("ParentOwned");
		case ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned:
			return TEXT("ChildOwned");
		case ELayoutNegotiatedHostVerticalAccessResponsibility::Composed:
			return TEXT("Composed");
		default:
			return TEXT("Unknown");
		}
	}

	FString BuildVerticalAccessRegionDiagnostic(
		const FLayoutRegionSolveScheduleResult& ScheduleResult)
	{
		const FLayoutRegionSolveResult* RootRegionResult =
			ScheduleResult.RegionResults.FindByPredicate(
				[](const FLayoutRegionSolveResult& RegionResult)
				{
					return RegionResult.SourceContentEntryId.IsNone();
				});
		if (RootRegionResult == nullptr)
		{
			return TEXT("root=<missing>");
		}

		TArray<FString> RegionParts;
		RegionParts.Reserve(ScheduleResult.RegionResults.Num());
		for (const FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			RegionParts.Add(FString::Printf(
				TEXT("%s:%d"),
				*RegionResult.RegionDebugPath,
				CountVerticalAccessPlacements(RegionResult)));
		}

		TArray<FString> ContractParts;
		for (const FLayoutNegotiatedChildResponsibilityContract& Contract :
			RootRegionResult->NegotiatedChildResponsibilityContracts)
		{
			const FLayoutRegionSolveResult* ChildRegionResult =
				ScheduleResult.RegionResults.FindByPredicate(
					[&Contract](const FLayoutRegionSolveResult& Candidate)
					{
						return Candidate.RegionDebugPath == Contract.ChildRegionDebugPath;
					});
			TArray<FString> InterfaceParts;
			for (const FLayoutNegotiatedLevelInterfaceContract& InterfaceLevel :
				Contract.CommittedParentChildInterfacesByLevel)
			{
				InterfaceParts.Add(FString::Printf(
					TEXT("L%d(e=%d,t=%d)"),
					InterfaceLevel.Level,
					InterfaceLevel.EndpointAnchors.Num(),
					InterfaceLevel.TraversalAnchors.Num()));
			}

			TArray<FString> ShellParts;
			for (const FLayoutNegotiatedLevelCellSet& LevelCells :
				Contract.RetainedParentShellCellsByLevel)
			{
				ShellParts.Add(FString::Printf(
					TEXT("L%d=%d"),
					LevelCells.Level,
					LevelCells.Cells.Num()));
			}

			TArray<FString> SeamParts;
			for (const FLayoutNegotiatedLevelSeamSet& SeamLevel :
				Contract.CommittedSiblingInterfacesByLevel)
			{
				SeamParts.Add(FString::Printf(
					TEXT("L%d=%d"),
					SeamLevel.Level,
					SeamLevel.Seams.Num()));
			}

			TMap<int32, int32> MergedParentChildSeamCountByLevel;
			for (const FLayoutPartitionSeamRecord& SeamRecord :
				ScheduleResult.MergedSolveResult.PartitionSeams)
			{
				const bool bForwardMatch =
					SeamRecord.OwnerRegionDebugPath == RootRegionResult->RegionDebugPath
					&& SeamRecord.PassiveRegionDebugPath == Contract.ChildRegionDebugPath;
				const bool bReverseMatch =
					SeamRecord.OwnerRegionDebugPath == Contract.ChildRegionDebugPath
					&& SeamRecord.PassiveRegionDebugPath == RootRegionResult->RegionDebugPath;
				if (!bForwardMatch && !bReverseMatch)
				{
					continue;
				}

				const int32 SeamLevel = bForwardMatch
					? SeamRecord.PassiveStartCell.Z - RootRegionResult->RegionCellOffset.Z
					: SeamRecord.OwnerStartCell.Z - RootRegionResult->RegionCellOffset.Z;
				int32& Count = MergedParentChildSeamCountByLevel.FindOrAdd(SeamLevel);
				++Count;
			}

			TArray<FString> MergedSeamParts;
			MergedSeamParts.Reserve(MergedParentChildSeamCountByLevel.Num());
			MergedParentChildSeamCountByLevel.KeySort(TLess<int32>());
			for (const TPair<int32, int32>& Pair : MergedParentChildSeamCountByLevel)
			{
				MergedSeamParts.Add(FString::Printf(
					TEXT("L%d=%d"),
					Pair.Key,
					Pair.Value));
			}

			TMap<int32, int32> ChildCommittedEndpointCountByLevel;
			if (ChildRegionResult != nullptr)
			{
				for (const FLayoutCommittedEndpointAnchor& Anchor :
					ChildRegionResult->CommittedEndpointAnchors)
				{
					++ChildCommittedEndpointCountByLevel.FindOrAdd(Anchor.LocalCell.Z);
				}
			}

			TArray<FString> ChildCommittedEndpointParts;
			ChildCommittedEndpointCountByLevel.KeySort(TLess<int32>());
			for (const TPair<int32, int32>& Pair : ChildCommittedEndpointCountByLevel)
			{
				ChildCommittedEndpointParts.Add(FString::Printf(
					TEXT("L%d=%d"),
					Pair.Key,
					Pair.Value));
			}

			ContractParts.Add(FString::Printf(
				TEXT("%s=%s parentProviders=%d childProviders=%d routeCells=%d childCommittedEndpoints=[%s] interfaces=[%s] retainedShell=[%s] seamSets=[%s] mergedParentChildSeams=[%s]"),
				*Contract.ChildRegionDebugPath,
				DescribeHostVerticalAccessResponsibility(
					Contract.HostVerticalAccessResponsibility),
				Contract.CountedParentProviderCount,
				Contract.CountedChildProviderRegionDebugPaths.Num(),
				Contract.RequiredChildInternalVerticalRouteCells.Num(),
				*FString::Join(ChildCommittedEndpointParts, TEXT(",")),
				*FString::Join(InterfaceParts, TEXT(",")),
				*FString::Join(ShellParts, TEXT(",")),
				*FString::Join(SeamParts, TEXT(",")),
				*FString::Join(MergedSeamParts, TEXT(","))));
		}

		return FString::Printf(
			TEXT("regions=[%s] authoritativeCounts=[required=%d exact=%d resolved=%d parentProviders=%d childProviders=%d] contracts=[%s]"),
			*FString::Join(RegionParts, TEXT(", ")),
			ScheduleResult.RecursiveVerticalAccessSummary.RequiredHostProviderCount,
			ScheduleResult.RecursiveVerticalAccessSummary.bRequiresExactHostProviderCount ? 1 : 0,
			ScheduleResult.RecursiveVerticalAccessSummary.ResolvedHostProviderCount,
			ScheduleResult.RecursiveVerticalAccessSummary.CountedParentProviderCount,
			ScheduleResult.RecursiveVerticalAccessSummary.CountedChildRegionDebugPaths.Num(),
			*FString::Join(ContractParts, TEXT("; ")));
	}

	bool HasCompleteRuntimeRootPublicationMetadata(
		const FLayoutRootPublicationMetadata& PublicationMetadata)
	{
		return !PublicationMetadata.RootPlacementPolicyId.IsNone()
			&& !PublicationMetadata.RootCandidateId.IsNone()
			&& !PublicationMetadata.RootSolveId.IsNone();
	}

	/** Callers validate producer identity before accessing root-owned cache state. */
	FString MakeResolvedSiteRecordKey(const FResolvedLayoutSiteRecord& SiteRecord)
	{
		check(!SiteRecord.RootSolveId.IsNone());
		return SiteRecord.RootSolveId.ToString();
	}

	bool HasWorldBindingFrontendSelection(
		const FLayoutWorldBindingSiteFrontendSelection& FrontendSelection)
	{
		return !FrontendSelection.WorldBindingId.IsNone();
	}

	void ComputePlanningSolveBlockBounds(
		const FIntVector& SiteCenterBlockWorldPos,
		const FLayoutSolveResult& SolveResult,
		const FIntVector& CellSizeInBlocks,
		FIntPoint& OutMinBlockXY,
		FIntPoint& OutMaxBlockXY)
	{
		const FIntPoint FootprintSizeInBlocks(
			SolveResult.FootprintSize.X * CellSizeInBlocks.X,
			SolveResult.FootprintSize.Y * CellSizeInBlocks.Y);
		OutMinBlockXY = FIntPoint(
			SiteCenterBlockWorldPos.X - FootprintSizeInBlocks.X / 2,
			SiteCenterBlockWorldPos.Y - FootprintSizeInBlocks.Y / 2);
		OutMaxBlockXY = FIntPoint(
			OutMinBlockXY.X + FootprintSizeInBlocks.X - 1,
			OutMinBlockXY.Y + FootprintSizeInBlocks.Y - 1);
	}

	FIntVector ComputePlanningSolveFootprintMinBlockWorldPos(
		const FIntVector& SiteCenterBlockWorldPos,
		const FLayoutSolveResult& SolveResult,
		const FIntVector& CellSizeInBlocks)
	{
		return FIntVector(
			SiteCenterBlockWorldPos.X - ((SolveResult.FootprintSize.X * CellSizeInBlocks.X) / 2),
			SiteCenterBlockWorldPos.Y - ((SolveResult.FootprintSize.Y * CellSizeInBlocks.Y) / 2),
			SiteCenterBlockWorldPos.Z);
	}

	FString BuildRuntimeTerrainFitFailureMessage(
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy,
		const FString& TerrainFitFailureReason)
	{
		return FString::Printf(
			TEXT("Terrain anchor could not be resolved for footprintMin=%s footprintSize=(%d,%d) startZ=%d depth=%d. %s"),
			*FootprintMinBlockWorldPos.ToString(),
			FootprintSizeInBlocks.X,
			FootprintSizeInBlocks.Y,
			PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
			PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
			*TerrainFitFailureReason);
	}

	FLayoutNoiseCoordinateSettings MakeCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings CoordinateSettings;
		if (WorldGenDef != nullptr)
		{
			CoordinateSettings.BaseBlockSize = WorldGenDef->BaseBlockSize;
			CoordinateSettings.NoiseScale = WorldGenDef->NoiseScale;
			CoordinateSettings.NoiseCoordinateOffset = WorldGenDef->NoiseCoordinateOffset;
		}
		return CoordinateSettings;
	}

	/** Samples procedural ownership at selected component floors; cache residency and edits cannot replace the owning row. */
	void AttachSelectedFloorBiomeOwnership(
		const bool bDetailedDiagnostics,
		const FIntVector& SharedCellSizeInBlocks,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
		const FLayoutActiveBiomeSampler* ActiveBiomeSampler,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings)
	{
		if (InOutArtifact.PocketVoidIntervals.IsEmpty())
		{
			return;
		}

		PORISM_LAYOUT_PROFILE_SCOPE(Layout_TerrainBiomeOwnerRead, STAT_PorismLayout_TerrainBiomeOwnerRead);
		TMap<FIntPoint, int32> IntervalIndexByXY;
		for (int32 IntervalIndex = 0; IntervalIndex < InOutArtifact.PocketVoidIntervals.Num(); ++IntervalIndex)
		{
			IntervalIndexByXY.Add(InOutArtifact.PocketVoidIntervals[IntervalIndex].BlockXY, IntervalIndex);
		}
		TArray<FIntVector> FloorPositions;
		TArray<int32> FloorIntervalIndices;
		TSet<int32> AddedIntervalIndices;
		FloorPositions.Reserve(InOutArtifact.TerrainPlacementCells.Num());
		FloorIntervalIndices.Reserve(InOutArtifact.TerrainPlacementCells.Num());
		for (const FLayoutTerrainPlacementCellEvidence& Cell : InOutArtifact.TerrainPlacementCells)
		{
			const FIntPoint BlockXY(
				InOutArtifact.FootprintMinBlockWorldPos.X + Cell.Cell.X * SharedCellSizeInBlocks.X + SharedCellSizeInBlocks.X / 2,
				InOutArtifact.FootprintMinBlockWorldPos.Y + Cell.Cell.Y * SharedCellSizeInBlocks.Y + SharedCellSizeInBlocks.Y / 2);
			const int32* const IntervalIndex = IntervalIndexByXY.Find(BlockXY);
			if (IntervalIndex == nullptr || AddedIntervalIndices.Contains(*IntervalIndex))
			{
				continue;
			}
			AddedIntervalIndices.Add(*IntervalIndex);
			const FLayoutFrozenTerrainVoidIntervalSample& Interval = InOutArtifact.PocketVoidIntervals[*IntervalIndex];
			FloorPositions.Add(FIntVector(BlockXY.X, BlockXY.Y, Interval.MinZ - 1));
			FloorIntervalIndices.Add(*IntervalIndex);
		}
		TArray<FLayoutActiveBiomeSample> FloorSamples;
		FloorSamples.SetNum(FloorPositions.Num());
		for (int32 Index = 0; Index < FloorPositions.Num(); ++Index)
		{
			if (ActiveBiomeSampler == nullptr
				|| !ActiveBiomeSampler->SampleAtBlockPosition(FloorPositions[Index], CoordinateSettings, FloorSamples[Index]))
			{
				FloorSamples[Index] = FLayoutActiveBiomeSample();
			}
		}
		TSet<FLayoutId> EligibleRows;
		for (const FLayoutId EligibleRow : InOutArtifact.EligibleBiomeRowNames)
		{
			EligibleRows.Add(EligibleRow);
		}
		if (!InOutArtifact.EligibleBiomeRowName.IsNone())
		{
			EligibleRows.Add(InOutArtifact.EligibleBiomeRowName);
			InOutArtifact.EligibleBiomeRowNames.AddUnique(InOutArtifact.EligibleBiomeRowName);
		}
		InOutArtifact.bRequiresBiomeOwnership = !EligibleRows.IsEmpty();
		InOutArtifact.BiomeOwnershipSamples.Reset();
		for (int32 Index = 0; Index < FloorIntervalIndices.Num(); ++Index)
		{
			const FLayoutActiveBiomeSample& Sample = FloorSamples[Index];
			FLayoutFrozenTerrainVoidIntervalSample& Interval = InOutArtifact.PocketVoidIntervals[FloorIntervalIndices[Index]];
			Interval.bHasFloorBiomeIndex = Sample.bIsValid && Sample.bAnyPositiveDomain && Sample.bTerrainSolid;
			Interval.FloorBiomeIndex = Interval.bHasFloorBiomeIndex ? Sample.WinningRow.SourceRowIndex : INDEX_NONE;
			FLayoutFrozenBiomeOwnershipSample& Ownership = InOutArtifact.BiomeOwnershipSamples.AddDefaulted_GetRef();
			Ownership.BlockXY = Interval.BlockXY;
			Ownership.SurfaceZ = Interval.MinZ - 1;
			Ownership.OwnershipSourceBlockWorldPos = FIntVector(
				Interval.BlockXY.X,
				Interval.BlockXY.Y,
				Ownership.SurfaceZ);
			Ownership.bHasSurfaceEvidence = Interval.bHasFloorBiomeIndex;
			if (Ownership.bHasSurfaceEvidence)
			{
				Ownership.OwningBiomeRowName = Sample.WinningRow.RowName;
				Ownership.bOwnedByAllowList = EligibleRows.IsEmpty()
					|| EligibleRows.Contains(Ownership.OwningBiomeRowName)
					|| EligibleRows.Contains(FLayoutId(*Sample.WinningRow.BiomeName));
			}
		}
		InOutArtifact.bHasBiomeOwnershipEvidence = !InOutArtifact.BiomeOwnershipSamples.IsEmpty();
		if (bDetailedDiagnostics)
		{
			const int32 InvalidIndex = InOutArtifact.BiomeOwnershipSamples.IndexOfByPredicate(
				[](const FLayoutFrozenBiomeOwnershipSample& Sample)
				{
					return !Sample.bHasSurfaceEvidence || !Sample.bOwnedByAllowList;
				});
			if (InvalidIndex != INDEX_NONE)
			{
				const FLayoutActiveBiomeSample& Sample = FloorSamples[InvalidIndex];
				UE_LOG(LogTemp, Log,
					TEXT("Layout floor ownership: footprintMin=%s floor=%s row=%s domain=%g density=%g valid=%d solid=%d source=biome-noise."),
					*InOutArtifact.FootprintMinBlockWorldPos.ToString(), *FloorPositions[InvalidIndex].ToString(),
					*Sample.WinningRow.RowName.ToString(), Sample.WinningDomainValue, Sample.TerrainValue,
					Sample.bIsValid, Sample.bTerrainSolid);
			}
		}
		INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainBiomePositions, static_cast<uint32>(FloorPositions.Num()));
		INC_DWORD_STAT_BY(
			STAT_PorismLayout_TerrainPacketBytes,
			static_cast<uint32>(FloorPositions.Num() * sizeof(FIntVector) + FloorSamples.Num() * sizeof(FLayoutActiveBiomeSample)));
	}

	/** Procedural environment classification for one bounded center column. */
	struct FLayoutEnvironmentCenterColumnProbe
	{
		int32 SampleMinZ = 0;
		int32 SampleMaxZ = 0;
		bool bHasDomainClassification = false;
		bool bDomainUnderground = false;
	};

	/** Evaluates the existing biome-noise utility without consulting loaded terrain. */
	bool TryBuildEnvironmentCenterColumnProbe(
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 ProbeFloorZ,
		const int32 ProbeCeilingZ,
		const int32 LayoutEnvelopeHeightInBlocks,
		FLayoutEnvironmentCenterColumnProbe& OutProbe)
	{
		OutProbe = FLayoutEnvironmentCenterColumnProbe();
		OutProbe.SampleMinZ = FMath::Min(ProbeFloorZ, SiteCenterBlockWorldPos.Z);
		OutProbe.SampleMaxZ = FMath::Max(SiteCenterBlockWorldPos.Z + 1, ProbeCeilingZ);
		TArray<FIntVector> ProbePositions;
		TArray<uint8> DomainEmptyStates;
		ProbePositions.Reserve(OutProbe.SampleMaxZ - OutProbe.SampleMinZ + 1);
		DomainEmptyStates.Reserve(OutProbe.SampleMaxZ - OutProbe.SampleMinZ + 1);
		for (int32 Z = OutProbe.SampleMinZ; Z <= OutProbe.SampleMaxZ; ++Z)
		{
			ProbePositions.Add(FIntVector(SiteCenterBlockWorldPos.X, SiteCenterBlockWorldPos.Y, Z));
			FLayoutActiveBiomeSample Sample;
			if (!ActiveBiomeSampler.SampleAtBlockPosition(ProbePositions.Last(), CoordinateSettings, Sample))
			{
				return false;
			}
			DomainEmptyStates.Add(Sample.bTerrainSolid ? 0 : 1);
		}
		auto Classify = [&](const TArray<uint8>& EmptyStates, bool& bOutHasClassification, bool& bOutUnderground)
		{
			TArray<FLayoutTerrainColumnProfile> Profiles;
			FString FailureReason;
			FLayoutTerrainColumnRun SelectedAirRun;
			bOutHasClassification = FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
				FIntVector(SiteCenterBlockWorldPos.X, SiteCenterBlockWorldPos.Y, OutProbe.SampleMinZ),
				1, 1, OutProbe.SampleMinZ, OutProbe.SampleMaxZ, EmptyStates, Profiles, FailureReason)
				&& Profiles.Num() == 1
				&& FLayoutTerrainSampling::TryClassifyCenterColumnEnvironment(
					Profiles[0],
					SiteCenterBlockWorldPos.Z,
					LayoutEnvelopeHeightInBlocks,
					SelectedAirRun,
					bOutUnderground,
					FailureReason);
		};
		Classify(DomainEmptyStates, OutProbe.bHasDomainClassification, OutProbe.bDomainUnderground);
		return true;
	}

	/** Resolves surface/cavity mode from procedural evidence before chunks load. */
	bool TryResolveEnvironmentCenterColumnMode(
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FIntVector& SharedCellSizeInBlocks,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy,
		const int32 ProfileLevelCount,
		const FIntVector& SiteCenterBlockWorldPos,
		bool& bOutUnderground)
	{
		bOutUnderground = false;
		const int32 ProbeDepth = FMath::Max(
			FMath::Max(1, SharedCellSizeInBlocks.Z),
			PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
		const int32 ProbeFloorZ = FMath::Min(
			PlacementPolicy.SurfaceSearch.TerrainSearchStartZ - ProbeDepth + 1,
			SiteCenterBlockWorldPos.Z - ProbeDepth);
		const int32 ProbeCeilingZ = FMath::Max(
			PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
			SiteCenterBlockWorldPos.Z + ProbeDepth);
		const int32 LayoutEnvelopeHeightInBlocks =
			FMath::Max(1, ProfileLevelCount) * FMath::Max(1, SharedCellSizeInBlocks.Z);
		FLayoutEnvironmentCenterColumnProbe Probe;
		if (!TryBuildEnvironmentCenterColumnProbe(
				ActiveBiomeSampler,
				CoordinateSettings,
				SiteCenterBlockWorldPos,
				ProbeFloorZ,
				ProbeCeilingZ,
				LayoutEnvelopeHeightInBlocks,
				Probe))
		{
			return false;
		}
		if (!Probe.bHasDomainClassification)
		{
			return false;
		}
		bOutUnderground = Probe.bDomainUnderground;
		return true;
	}

	/** Prepares one canonical selected-site occupancy artifact for every ordinary-root caller. */
	bool TryPrepareOrdinaryRootSelectedSiteTerrain(
		const bool bDetailedDiagnostics,
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntPoint& FootprintSizeInCells,
		const FIntVector& SharedCellSizeInBlocks,
		const int32 ProfileLevelCount,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy,
		const FLayoutActiveBiomeSampler* const ActiveBiomeSampler,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_SelectedSiteTerrainTotal, STAT_PorismLayout_SelectedSiteTerrainTotal);
#if WITH_AUTOMATION_TESTS
		const double SelectedSiteTerrainStartSeconds = FPlatformTime::Seconds();
#endif
		OutFailureReason.Reset();
		const int32 FootprintWidthBlocks = FootprintSizeInCells.X * SharedCellSizeInBlocks.X;
		const int32 FootprintHeightBlocks = FootprintSizeInCells.Y * SharedCellSizeInBlocks.Y;
		const int32 BoundaryHaloBlocks = FMath::Max(SharedCellSizeInBlocks.X, SharedCellSizeInBlocks.Y);
		const FIntVector SampleFootprintMinBlockWorldPos = FootprintMinBlockWorldPos
			- FIntVector(BoundaryHaloBlocks, BoundaryHaloBlocks, 0);
		const int32 SampleFootprintWidthBlocks = FootprintWidthBlocks + 2 * BoundaryHaloBlocks;
		const int32 SampleFootprintHeightBlocks = FootprintHeightBlocks + 2 * BoundaryHaloBlocks;
		const int32 FoundationSampleDepth = PlacementPolicy.TerrainTransition.bAllowFoundationFill
			? PlacementPolicy.TerrainTransition.MaxFoundationDepth
			: 0;
		const int32 SampleMinZ = SiteCenterBlockWorldPos.Z
			- FMath::Max(SharedCellSizeInBlocks.Z, FoundationSampleDepth);
		const int32 LayoutEnvelopeMaxZ = SiteCenterBlockWorldPos.Z
			+ FMath::Max(1, ProfileLevelCount) * SharedCellSizeInBlocks.Z - 1;
		const int32 SampleMaxZ = FMath::Max(
			LayoutEnvelopeMaxZ + SharedCellSizeInBlocks.Z,
			PlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
		const int32 EnvelopeHeightBlocks = FMath::Max(0, SampleMaxZ - SampleMinZ + 1);
		TArray<FIntVector> BlockPositions;
		{
			PORISM_LAYOUT_PROFILE_SCOPE(Layout_TerrainPositionBuild, STAT_PorismLayout_TerrainPositionBuild);
			BlockPositions.Reserve(SampleFootprintWidthBlocks * SampleFootprintHeightBlocks * EnvelopeHeightBlocks);
			for (int32 Z = SampleMinZ; Z <= SampleMaxZ; ++Z)
			{
				if (LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
				{
					OutFailureReason = TEXT("Layout preparation was canceled.");
					return false;
				}
				for (int32 Y = SampleFootprintMinBlockWorldPos.Y;
					Y < SampleFootprintMinBlockWorldPos.Y + SampleFootprintHeightBlocks;
					++Y)
				{
					for (int32 X = SampleFootprintMinBlockWorldPos.X;
						X < SampleFootprintMinBlockWorldPos.X + SampleFootprintWidthBlocks;
						++X)
					{
						BlockPositions.Add(FIntVector(X, Y, Z));
					}
				}
			}
		}

		TArray<int> Materials;
		{
			PORISM_LAYOUT_PROFILE_SCOPE(Layout_TerrainMaterialRead, STAT_PorismLayout_TerrainMaterialRead);
			Materials.Reserve(BlockPositions.Num());
			for (int32 Index = 0; Index < BlockPositions.Num(); ++Index)
			{
				if ((Index & 255) == 0 && LayoutSolveCancellation::IsCurrentThreadCancellationRequested())
				{
					OutFailureReason = TEXT("Layout preparation was canceled.");
					return false;
				}
				const FIntVector& Position = BlockPositions[Index];
				FLayoutActiveBiomeSample Sample;
				if (ActiveBiomeSampler == nullptr
					|| !ActiveBiomeSampler->SampleAtBlockPosition(Position, CoordinateSettings, Sample)
					|| !Sample.bIsValid || !FMath::IsFinite(Sample.TerrainValue))
				{
					OutFailureReason = FString::Printf(TEXT("Selected-site biome-noise sampling failed at %s."), *Position.ToString());
					return false;
				}
				// DefaultMaterial is the existing carrier's unknown-solid marker, not
				// a concrete fill material. Density must not invent material provenance.
				Materials.Add(Sample.bTerrainSolid ? DefaultMaterial : EmptyMaterial);
			}
		}
		INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainMaterialPositions, static_cast<uint32>(BlockPositions.Num()));
		INC_DWORD_STAT_BY(
			STAT_PorismLayout_TerrainPacketBytes,
			static_cast<uint32>(BlockPositions.Num() * sizeof(FIntVector) + Materials.Num() * sizeof(int)));
		if (!FLayoutTerrainSampling::TryPrepareSelectedSiteTerrainFromMaterialSamples(
				SampleFootprintMinBlockWorldPos,
				SampleFootprintWidthBlocks,
				SampleFootprintHeightBlocks,
				SampleMinZ,
				SampleMaxZ,
				SiteCenterBlockWorldPos,
				SharedCellSizeInBlocks,
				FMath::Max(1, ProfileLevelCount) * SharedCellSizeInBlocks.Z,
				PlacementPolicy.TerrainTransition,
				EmptyMaterial,
				Materials,
				TConstArrayView<int>(),
				InOutArtifact,
				OutFailureReason))
		{
			InOutArtifact.AuditMessages.Add(OutFailureReason);
			return false;
		}
		// A selected air run touching the scan bottom has no observed floor. Extend
		// only those columns, sharing the configured terrain search limit. Do not
		// freeze the dense packet's lower boundary as an invented terrace.
		TArray<int32> PendingFloorIntervals;
		for (int32 Index = 0; Index < InOutArtifact.PocketVoidIntervals.Num(); ++Index)
		{
			const auto& Interval = InOutArtifact.PocketVoidIntervals[Index];
			if (Interval.bHasVoidEvidence && Interval.MinZ == SampleMinZ)
			{
				PendingFloorIntervals.Add(Index);
			}
		}
		const int32 ClippedFloorCount = PendingFloorIntervals.Num();
		const int32 SearchFloorZ = PlacementPolicy.SurfaceSearch.TerrainSearchStartZ
			- FMath::Max(1, PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks) + 1;
		int32 LowestSampledZ = SampleMinZ;
		int32 FloorProbeCount = 0;
		for (int32 ProbeZ = SampleMinZ - 1; ProbeZ >= SearchFloorZ && !PendingFloorIntervals.IsEmpty(); --ProbeZ)
		{
			if (!LayoutSolveExecution::Checkpoint(OutFailureReason)) return false;
			TArray<FIntVector> FloorPositions;
			FloorPositions.Reserve(PendingFloorIntervals.Num());
			for (const int32 Index : PendingFloorIntervals)
			{
				const auto& Interval = InOutArtifact.PocketVoidIntervals[Index];
				FloorPositions.Add(FIntVector(Interval.BlockXY.X, Interval.BlockXY.Y, ProbeZ));
			}
			LowestSampledZ = ProbeZ;
			FloorProbeCount += FloorPositions.Num();
			for (int32 Index = PendingFloorIntervals.Num() - 1; Index >= 0; --Index)
			{
				FLayoutActiveBiomeSample Sample;
				if (ActiveBiomeSampler == nullptr
					|| !ActiveBiomeSampler->SampleAtBlockPosition(FloorPositions[Index], CoordinateSettings, Sample)
					|| !Sample.bIsValid || !FMath::IsFinite(Sample.TerrainValue))
				{
					OutFailureReason = FString::Printf(TEXT("Selected-site floor biome-noise sampling failed at %s."), *FloorPositions[Index].ToString());
					return false;
				}
				const int Material = Sample.bTerrainSolid ? DefaultMaterial : EmptyMaterial;
				auto& Interval = InOutArtifact.PocketVoidIntervals[PendingFloorIntervals[Index]];
				if (Material == EmptyMaterial)
				{
					Interval.MinZ = ProbeZ;
					continue;
				}
				Interval.MinZ = ProbeZ + 1;
				Interval.bHasFloorMaterialIndex = Material != DefaultMaterial;
				Interval.FloorMaterialIndex = Material;
				PendingFloorIntervals.RemoveAtSwap(Index, EAllowShrinking::No);
			}
		}
		if (ClippedFloorCount > 0)
		{
			InOutArtifact.AuditMessages.Add(FString::Printf(
				TEXT("SelectedSiteFloorExtension clipped=%d probes=%d sampledMinZ=%d searchFloorZ=%d unresolved=%d"),
				ClippedFloorCount, FloorProbeCount, LowestSampledZ, SearchFloorZ, PendingFloorIntervals.Num()));
		}
		InOutArtifact.bHasFiniteSearchBounds = true;
		InOutArtifact.SearchMinBlockXY = FIntPoint(
			SampleFootprintMinBlockWorldPos.X,
			SampleFootprintMinBlockWorldPos.Y);
		InOutArtifact.SearchMaxBlockXY = FIntPoint(
			SampleFootprintMinBlockWorldPos.X + SampleFootprintWidthBlocks - 1,
			SampleFootprintMinBlockWorldPos.Y + SampleFootprintHeightBlocks - 1);
		InOutArtifact.SearchStartZBlockWorld = SampleMaxZ;
		InOutArtifact.SearchDepthBlocks = SampleMaxZ - LowestSampledZ + 1;
		AttachSelectedFloorBiomeOwnership(bDetailedDiagnostics, SharedCellSizeInBlocks, InOutArtifact, ActiveBiomeSampler, CoordinateSettings);
		InOutArtifact.AuditMessages.Add(FString::Printf(
			TEXT("RelativeEnvironmentClassification site=%s footprintMin=%s footprintBlocks=(%d,%d) sampleZ=[%d,%d] layoutEnvelopeMaxZ=%d classifiedUnderground=%d materialSamples=%d"),
			*SiteCenterBlockWorldPos.ToString(),
			*FootprintMinBlockWorldPos.ToString(),
			FootprintWidthBlocks,
			FootprintHeightBlocks,
			SampleMinZ,
			SampleMaxZ,
			LayoutEnvelopeMaxZ,
			InOutArtifact.bIsClassifiedUnderground ? 1 : 0,
			Materials.Num()));
#if WITH_AUTOMATION_TESTS
		// Game-thread test timing is not shared worker output.
		if (IsInGameThread())
		{
			GLastSelectedSiteTerrainMilliseconds =
				(FPlatformTime::Seconds() - SelectedSiteTerrainStartSeconds) * 1000.0;
		}
#endif
		return true;
	}

	template <typename TObjectType>
	TObjectType* ResolveResidentOrLoadSoftObject(const TSoftObjectPtr<TObjectType>& ObjectPtr)
	{
		if (TObjectType* const ResidentObject = ObjectPtr.Get())
		{
			return ResidentObject;
		}

		return ObjectPtr.LoadSynchronous();
	}

	void BackfillWorkerDerivedSteppedCarriersOnMergedSolveResult(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutSolveResult& InOutMergedSolveResult)
	{
		if (InOutMergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
		{
			for (const FLayoutPlacedModule& Placement : InOutMergedSolveResult.Placements)
			{
				if (Placement.Intent != ELayoutCellIntent::VerticalAccess
					|| Placement.ModuleSnapshotId.IsNone()
					|| Placement.ModuleSnapshotIndex == INDEX_NONE
					|| SolveRequest.EffectiveSnapshotId.IsNone())
				{
					continue;
				}

				FLayoutForcedPlacementBundleInsertion& Insertion = InOutMergedSolveResult.ForcedPlacementBundleInsertions.AddDefaulted_GetRef();
				Insertion.BundleId = FLayoutId(*FString::Printf(
					TEXT("%s.Bundle.%d.%s"),
					*SolveRequest.EffectiveSnapshotId.ToString(),
					Placement.ModuleSnapshotIndex,
					*Placement.ModuleSnapshotId.ToString()));
				Insertion.AnchorCell = Placement.Cell;
				Insertion.ProvingCell = Placement.Cell;
			}
		}

	}

	void PublishRequestOwnedSteppedCarriersOnMergedSolveResult(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		// Runtime callers attach derived stepped-support state to the frozen request
		// surface first. Backfill that state onto the merged solve result only when
		// the active schedule path did not already preserve a richer prepared form.
		FLayoutSolveResult& MergedSolveResult = InOutScheduleResult.MergedSolveResult;
		if (MergedSolveResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
			&& MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty()
			&& MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.IsEmpty())
		{
			MergedSolveResult.SteppedTerrainSupportMap = SolveRequest.SteppedTerrainSupportMap;
		}

		if (MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
		{
			MergedSolveResult.ForcedPlacementBundleInsertions =
				SolveRequest.ForcedPlacementBundleInsertions;
		}
		else if (SolveRequest.ForcedPlacementBundleInsertions.Num() > MergedSolveResult.ForcedPlacementBundleInsertions.Num())
		{
			MergedSolveResult.ForcedPlacementBundleInsertions =
				SolveRequest.ForcedPlacementBundleInsertions;
		}
		if (MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
		{
			for (const FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
			{
				if (!RegionResult.ForcedPlacementBundleInsertions.IsEmpty())
				{
					MergedSolveResult.ForcedPlacementBundleInsertions =
						RegionResult.ForcedPlacementBundleInsertions;
					break;
				}
			}
		}

		if (MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
		{
			MergedSolveResult.RequestOwnedRequiredRouteConstraints =
				SolveRequest.RequiredRouteConstraints;
		}
		if (MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
		{
			for (const FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
			{
				if (!RegionResult.RequiredRouteConstraints.IsEmpty())
				{
					MergedSolveResult.RequestOwnedRequiredRouteConstraints =
						RegionResult.RequiredRouteConstraints;
					break;
				}
			}
		}

		if (MergedSolveResult.PlannedCells.IsEmpty() && !SolveRequest.PlannedCells.IsEmpty())
		{
			MergedSolveResult.PlannedCells = SolveRequest.PlannedCells;
		}

		BackfillWorkerDerivedSteppedCarriersOnMergedSolveResult(SolveRequest, MergedSolveResult);
	}

	const FLayoutModuleSolveSnapshot* FindPlacementModuleSnapshot(
		const FLayoutRegionSolveRequest& SolveRequest,
		const FLayoutPlacedModule& Placement)
	{
		// Merged child indices remain local to their catalogs. Resolve identity
		// across frozen catalogs; never reinterpret an index against the root.
		if (Placement.ModuleSnapshotId.IsNone() && Placement.SourceContentEntryId.IsNone()) return nullptr;
		const FLayoutModuleSolveSnapshot* Match = nullptr;
		bool bAmbiguous = false;
		TSet<const FLayoutModuleCatalog*> Visited;
		TFunction<void(const FLayoutModuleCatalog&, const FLayoutRegionContentSetSolveSnapshot&)> Visit =
			[&](const FLayoutModuleCatalog& Catalog, const FLayoutRegionContentSetSolveSnapshot& Content)
		{
			if (Visited.Contains(&Catalog)) return;
			Visited.Add(&Catalog);
			for (const auto& Module : Catalog.Modules)
			{
				if (!Placement.ModuleSnapshotId.IsNone() && Module.SnapshotId != Placement.ModuleSnapshotId) continue;
				if (!Placement.SourceContentEntryId.IsNone() && Module.SourceContentEntryId != Placement.SourceContentEntryId) continue;
				if (Match && (Match->Template != Module.Template || Match->BoundsCells != Module.BoundsCells
					|| Match->OccupiedLocalCells != Module.OccupiedLocalCells)) bAmbiguous = true;
				Match = &Module;
			}
			for (const auto& Entry : Content.Entries)
				if (Entry.CompiledChildRequestTemplate.IsValid())
					Visit(Entry.CompiledChildRequestTemplate->ModuleCatalog, Entry.CompiledChildRequestTemplate->ContentSetSnapshot);
		};
		Visit(SolveRequest.ModuleCatalog, SolveRequest.ContentSetSnapshot);
		return bAmbiguous ? nullptr : Match;
	}

	void BackfillSolvedPlacementTemplatePathsFromSnapshots(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutSolveResult& InOutSolveResult)
	{
		for (FLayoutPlacedModule& Placement : InOutSolveResult.Placements)
		{
			if (Placement.TemplatePath.IsValid())
			{
				continue;
			}
			const FLayoutModuleSolveSnapshot* const ModuleSnapshot = FindPlacementModuleSnapshot(SolveRequest, Placement);
			if (ModuleSnapshot == nullptr || ModuleSnapshot->Template.IsNull())
			{
				continue;
			}

			// Project-specific realization-prep bridge: worker results are scrubbed of
			// live carriers before publication, so the durable solved artifact must keep
			// the snapshot-owned template path that authorizes later template writes.
			Placement.TemplatePath = ModuleSnapshot->Template.ToSoftObjectPath();
			if (Placement.OccupiedLocalCells.IsEmpty())
			{
				Placement.OccupiedLocalCells = LayoutPlacementOccupancy::ResolveOccupiedLocalCells(Placement, ModuleSnapshot);
			}
			if (Placement.BundleBoundsCells == FIntVector::ZeroValue)
			{
				Placement.BundleBoundsCells = !Placement.OccupiedLocalCells.IsEmpty()
					? LayoutPlacementOccupancy::BuildOccupiedLocalCellBounds(Placement.OccupiedLocalCells)
					: ModuleSnapshot->BoundsCells;
			}
		}
	}

	void BackfillSolvedArtifactTemplatePathsFromSnapshots(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		BackfillSolvedPlacementTemplatePathsFromSnapshots(SolveRequest, InOutScheduleResult.MergedSolveResult);
		for (FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
		{
			BackfillSolvedPlacementTemplatePathsFromSnapshots(SolveRequest, RegionResult.SolveResult);
		}
	}

	void BackfillRequestOwnedVerticalAccessPlacementsOnMergedSolveResult(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		const FLayoutModuleSolveSnapshot* MatchingSnapshot = nullptr;
		int32 MatchingSnapshotIndex = INDEX_NONE;
		for (int32 ModuleIndex = 0; ModuleIndex < SolveRequest.ModuleCatalog.Modules.Num(); ++ModuleIndex)
		{
			const FLayoutModuleSolveSnapshot& ModuleSnapshot = SolveRequest.ModuleCatalog.Modules[ModuleIndex];
			if (ModuleSnapshot.SupportedCellIntents.Contains(ELayoutCellIntent::VerticalAccess)
				|| ModuleSnapshot.RootSupportedCellIntents.Contains(ELayoutCellIntent::VerticalAccess))
			{
				MatchingSnapshot = &ModuleSnapshot;
				MatchingSnapshotIndex = ModuleIndex;
				break;
			}
		}
		if (MatchingSnapshot == nullptr)
		{
			return;
		}

		for (const FLayoutPlannedCell& PlannedCell : SolveRequest.PlannedCells)
		{
			if (PlannedCell.Intent != ELayoutCellIntent::VerticalAccess)
			{
				continue;
			}
			if (InOutScheduleResult.MergedSolveResult.Placements.ContainsByPredicate(
					[&PlannedCell](const FLayoutPlacedModule& Placement)
					{
						return Placement.Cell == PlannedCell.Cell;
					}))
			{
				continue;
			}

			FLayoutPlacedModule& BackfilledPlacement = InOutScheduleResult.MergedSolveResult.Placements.AddDefaulted_GetRef();
			BackfilledPlacement.Cell = PlannedCell.Cell;
			BackfilledPlacement.Intent = ELayoutCellIntent::VerticalAccess;
			BackfilledPlacement.SourceContentEntryId = MatchingSnapshot->SourceContentEntryId;
			BackfilledPlacement.ModuleSnapshotId = MatchingSnapshot->SnapshotId;
			BackfilledPlacement.ModuleSnapshotIndex = MatchingSnapshotIndex;
			BackfilledPlacement.TemplatePath = MatchingSnapshot->Template.ToSoftObjectPath();
			BackfilledPlacement.BundleBoundsCells = MatchingSnapshot->BoundsCells;
			BackfilledPlacement.OccupiedLocalCells = MatchingSnapshot->OccupiedLocalCells;
		}
	}


	FName ResolveRuntimeWorldBindingId(const ULayoutWorldBindingAsset* const WorldBinding)
	{
		if (WorldBinding == nullptr)
		{
			return NAME_None;
		}

		return !WorldBinding->BindingId.IsNone()
			? WorldBinding->BindingId
			: WorldBinding->GetFName();
	}

	const ULayoutWorldBindingAsset* FindRuntimeWorldBindingById(
		const TArray<TObjectPtr<ULayoutWorldBindingAsset>>& WorldBindings,
		const FName BindingId)
	{
		if (BindingId.IsNone())
		{
			return nullptr;
		}

		for (const TObjectPtr<ULayoutWorldBindingAsset>& WorldBindingPtr : WorldBindings)
		{
			const ULayoutWorldBindingAsset* const WorldBinding = WorldBindingPtr.Get();
			if (WorldBinding != nullptr && ResolveRuntimeWorldBindingId(WorldBinding) == BindingId)
			{
				return WorldBinding;
			}
		}

		return nullptr;
	}

	bool HasValidBindingOwnedSharedCellSize(const ULayoutWorldBindingAsset* const WorldBinding)
	{
		return WorldBinding != nullptr
			&& WorldBinding->BaseCellDimensionsBlocks.X > 0
			&& WorldBinding->BaseCellDimensionsBlocks.Y > 0
			&& WorldBinding->BaseCellDimensionsBlocks.Z > 0;
	}


	const ULayoutWorldBindingAsset* ResolveRuntimeWorldBindingForSiteRecord(
		const TArray<TObjectPtr<ULayoutWorldBindingAsset>>& WorldBindings,
		const FResolvedLayoutSiteRecord& SiteRecord)
	{
		const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
			SiteRecord.GetWorldBindingFrontendSelection();
		return FindRuntimeWorldBindingById(WorldBindings, FrontendSelection.WorldBindingId);
	}

	const ULayoutWorldBindingAsset* ResolveRuntimeWorldBindingForConnectorRecord(
		const TArray<TObjectPtr<ULayoutWorldBindingAsset>>& WorldBindings,
		const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		return FindRuntimeWorldBindingById(WorldBindings, FrontendSelection.WorldBindingId);
	}

	FIntVector ResolveRuntimeBindingOwnedSharedCellSizeInBlocks(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutSolveResult& SolveResult,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		return HasValidBindingOwnedSharedCellSize(WorldBinding)
			? WorldBinding->BaseCellDimensionsBlocks
			: LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(
				SolveResult,
				ContentSet);
	}

	int32 ResolveRuntimeBindingOwnedTemplatePlacementZOffsetBlocks(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutSolveResult& SolveResult)
	{
		return WorldBinding != nullptr
			? WorldBinding->TemplatePlacementZOffsetBlocks
			: SolveResult.TemplatePlacementZOffsetBlocks;
	}

	void NormalizeRuntimeWorldBindingMetrics(
		FResolvedLayoutSiteRecord& SiteRecord,
		const ULayoutWorldBindingAsset* const WorldBinding,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		FResolvedLayoutSiteSolvedPayload SolvedPayload =
			SiteRecord.GetResolvedSiteSolvedPayload();
		SolvedPayload.SolveResult.SharedCellSizeInBlocks =
			ResolveRuntimeBindingOwnedSharedCellSizeInBlocks(
				WorldBinding,
				SolvedPayload.SolveResult,
				ContentSet);
		SolvedPayload.SolveResult.TemplatePlacementZOffsetBlocks =
			ResolveRuntimeBindingOwnedTemplatePlacementZOffsetBlocks(
				WorldBinding,
				SolvedPayload.SolveResult);
		SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);
	}

	bool RehydratePlacementAssetCarrierFromContentSet(
		const ULayoutRegionContentSetAsset* const ContentSet,
		FLayoutPlacedModule& Placement)
	{
		if (ContentSet == nullptr
			|| Placement.SourceContentEntryId.IsNone()
			|| (Placement.Module != nullptr || Placement.CompositeModule != nullptr))
		{
			return false;
		}

		const FLayoutRegionContentEntry* SourceEntry =
			LayoutWorldBindingRuntimeHelpers::FindFrozenPlacementSourceEntry(ContentSet, Placement);
		if (SourceEntry == nullptr)
		{
			return false;
		}

		// Restore only the selected asset; a parent entry with the same name is not
		// authority to replace a child template after successful proof.
		Placement.Module = SourceEntry->ModuleSettings.Module;
		Placement.CompositeModule = SourceEntry->ModuleSettings.CompositeModule;
		return Placement.Module != nullptr || Placement.CompositeModule != nullptr;
	}

	bool PlacementRequiresTemplateCarrier(
		const ULayoutRegionContentSetAsset* const ContentSet,
		const FLayoutPlacedModule& Placement)
	{
		if (Placement.Module != nullptr || Placement.CompositeModule != nullptr || !Placement.TemplatePath.IsNull())
		{
			return true;
		}

		if (Placement.OccupiedLocalCells.Num() > 1
			&& Placement.LocalCellFaceRules.Num() == Placement.OccupiedLocalCells.Num()
			&& Placement.LocalCellFaceRules.FindByPredicate([](const FLayoutPlacedLocalCellFaceRuleSnapshot& LocalCell)
			{
				return !LocalCell.TemplatePath.IsValid();
			}) == nullptr)
		{
			return true;
		}

		if (ContentSet == nullptr || Placement.SourceContentEntryId.IsNone())
		{
			return false;
		}

		const FLayoutRegionContentEntry* const SourceEntry =
			ContentSet->Entries.FindByPredicate(
				[&Placement](const FLayoutRegionContentEntry& Entry)
				{
					return Entry.EntryId == Placement.SourceContentEntryId;
				});
		return SourceEntry != nullptr
			&& SourceEntry->ContentKind == ELayoutRegionContentKind::Module;
	}

	const FLayoutRegionContentEntry* FindPlacementSourceEntry(
		const ULayoutRegionContentSetAsset* const ContentSet,
		const FLayoutPlacedModule& Placement)
	{
		if (ContentSet == nullptr || Placement.SourceContentEntryId.IsNone())
		{
			return nullptr;
		}

		return ContentSet->Entries.FindByPredicate(
			[&Placement](const FLayoutRegionContentEntry& Entry)
			{
				return Entry.EntryId == Placement.SourceContentEntryId;
			});
	}

	void RehydrateSolvedPlacementAssetCarriersFromContentSet(
		FResolvedLayoutSiteRecord& SiteRecord,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		FResolvedLayoutSiteSolvedPayload SolvedPayload =
			SiteRecord.GetResolvedSiteSolvedPayload();
		bool bUpdatedAnyPlacement = false;
		for (FLayoutPlacedModule& Placement : SolvedPayload.SolveResult.Placements)
		{
			bUpdatedAnyPlacement |= RehydratePlacementAssetCarrierFromContentSet(
				ContentSet,
				Placement);
		}

		if (bUpdatedAnyPlacement)
		{
			SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);
		}
	}

	void RehydrateScheduleResultPlacementAssetCarriersFromContentSet(
		FLayoutRegionSolveScheduleResult& ScheduleResult,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		for (FLayoutPlacedModule& Placement : ScheduleResult.MergedSolveResult.Placements)
		{
			RehydratePlacementAssetCarrierFromContentSet(ContentSet, Placement);
		}
		for (FLayoutRegionSolveResult& RegionResult : ScheduleResult.RegionResults)
		{
			for (FLayoutPlacedModule& Placement : RegionResult.SolveResult.Placements)
			{
				RehydratePlacementAssetCarrierFromContentSet(ContentSet, Placement);
			}
		}
	}

	void NormalizeRuntimeWorldBindingMetrics(
		FResolvedLayoutConnectorRecord& ConnectorRecord,
		const ULayoutWorldBindingAsset* const WorldBinding,
		const ULayoutRegionContentSetAsset* const ContentSet)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		ConnectorRecord.SolveResult.SharedCellSizeInBlocks =
			WorldBinding != nullptr
				? ResolveRuntimeBindingOwnedSharedCellSizeInBlocks(
					WorldBinding,
					ConnectorRecord.SolveResult,
					ContentSet)
				: (FrontendSelection.SharedCellSizeInBlocks != FIntVector::ZeroValue
					? FrontendSelection.SharedCellSizeInBlocks
					: ResolveRuntimeBindingOwnedSharedCellSizeInBlocks(
						nullptr,
						ConnectorRecord.SolveResult,
						ContentSet));
		ConnectorRecord.SolveResult.TemplatePlacementZOffsetBlocks =
			WorldBinding != nullptr
				? ResolveRuntimeBindingOwnedTemplatePlacementZOffsetBlocks(
					WorldBinding,
					ConnectorRecord.SolveResult)
				: (!FrontendSelection.WorldBindingId.IsNone()
					? FrontendSelection.TemplatePlacementZOffsetBlocks
					: ResolveRuntimeBindingOwnedTemplatePlacementZOffsetBlocks(
						nullptr,
						ConnectorRecord.SolveResult));
	}

	void SetResolvedSiteTerrainFitDiagnostic(
		FResolvedLayoutSiteRecord& SiteRecord,
		const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
	{
		FResolvedLayoutSiteRuntimeState RuntimeState =
			SiteRecord.GetResolvedSiteRuntimeState();
		RuntimeState.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
	}

	/** Returns true when immutable publication outcome authorizes site realization. */
	bool IsCachedSiteApplyable(const FResolvedLayoutSiteRuntimeState& RuntimeState)
	{
		return RuntimeState.CachedApplyability != ELayoutCachedApplyability::Rejected;
	}

	void MarkResolvedSiteRealizedAndCommitted(FResolvedLayoutSiteRecord& SiteRecord)
	{
		FResolvedLayoutSiteRuntimeState RuntimeState =
			SiteRecord.GetResolvedSiteRuntimeState();
		RuntimeState.bLayoutRealized = true;
		RuntimeState.bHasBeenCommittedToChunkWorld = true;
		SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
	}

	void SetResolvedConnectorTerrainFitDiagnostic(
		FResolvedLayoutConnectorRecord& ConnectorRecord,
		const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
	{
		FResolvedLayoutConnectorRuntimeState RuntimeState =
			ConnectorRecord.GetResolvedConnectorRuntimeState();
		RuntimeState.TerrainFitDiagnosticKind = TerrainFitDiagnosticKind;
		ConnectorRecord.SetResolvedConnectorRuntimeState(RuntimeState);
	}

	void MarkResolvedConnectorRealizedAndCommitted(FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		FResolvedLayoutConnectorRuntimeState RuntimeState =
			ConnectorRecord.GetResolvedConnectorRuntimeState();
		RuntimeState.bLayoutRealized = true;
		RuntimeState.bHasBeenCommittedToChunkWorld = true;
		ConnectorRecord.SetResolvedConnectorRuntimeState(RuntimeState);
	}

	void CopyTerrainWrites(
		const TArray<FIntVector>& Positions,
		const TArray<int32>& Materials,
		TMap<FIntVector, int32>& InOutWrites)
	{
		const int32 Count = FMath::Min(Positions.Num(), Materials.Num());
		for (int32 Index = 0; Index < Count; ++Index)
		{
			InOutWrites.Add(Positions[Index], Materials[Index]);
		}
	}

	void ReportLayoutPlanningWarning(
		const UObject* const ContextObject,
		const bool bEnabled,
		const FString& MessageText)
	{
		if (!bEnabled)
		{
			return;
		}

		(void)ContextObject;
		UE_LOG(LogTemp, Display, TEXT("[LayoutPlanning] %s"), *MessageText);
	}

	void ReportLayoutSolveMessages(
		const bool bEnabled,
		const FLayoutId BindingRowName,
		const FIntVector& SiteCenterBlockWorldPos,
		const FLayoutSolveResult& SolveResult)
	{
		if (!bEnabled) return;
		TSet<FString> ReportedMessages;
		for (const FLayoutValidationMessage& Message : SolveResult.Messages)
		{
			if (ReportedMessages.Contains(Message.Message)) continue;
			ReportedMessages.Add(Message.Message);
			UE_LOG(LogTemp, Display, TEXT("[LayoutPlanning] binding='%s' site=%s: %s"),
				*BindingRowName.ToString(), *SiteCenterBlockWorldPos.ToString(), *Message.Message);
		}
	}

	FString BuildLayoutSolvePropagationStatsSummary(const FLayoutSolveResult& SolveResult)
	{
		const FLayoutSolverPropagationStats& Stats = SolveResult.PropagationStats;
		return FString::Printf(
			TEXT("prop=%.2fms runs=%d passes=%d arcs=%d checks=%d removals=%d failed=%d attempts=%d backtracks=%d terrainStages=%d blockedFrontiers=%d terrainBacktracks=%d"),
			Stats.PropagationSeconds * 1000.0,
			Stats.PropagationRunCount,
			Stats.PropagationPassCount,
			Stats.ArcQueuePopCount,
			Stats.SupportCheckCount,
			Stats.CandidateRemovalCount,
			Stats.FailedCellCount,
			Stats.CandidateAttemptCount,
			Stats.BacktrackCount,
			Stats.TerrainStageCount,
			Stats.TerrainBlockedFrontierCount,
			Stats.TerrainStageBacktrackCount);
	}

	void ShowLayoutSolvePropagationStatsOnScreen(
		const bool bEnabled,
		const FString& ContextText,
		const FLayoutSolveResult& SolveResult,
		const FColor& Color)
	{
		if (!bEnabled || GEngine == nullptr)
		{
			return;
		}

		GEngine->AddOnScreenDebugMessage(
			INDEX_NONE,
			8.0f,
			Color,
			FString::Printf(
				TEXT("Layout solve %s: %s"),
				*ContextText,
				*BuildLayoutSolvePropagationStatsSummary(SolveResult)));
	}

	int32 NormalizeYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}
		return NormalizedSteps;
	}

	FIntVector RotateTemplateRelativeBlockPosYaw(const FIntVector& RelativeBlockPos, const FIntVector& SizeInBlocks, const int32 YawRotationSteps)
	{
		FIntVector Result = RelativeBlockPos;
		FIntVector EffectiveSize = SizeInBlocks;
		for (int32 Step = 0; Step < NormalizeYawRotationSteps(YawRotationSteps); ++Step)
		{
			const int32 PreviousX = Result.X;
			Result.X = EffectiveSize.Y - 1 - Result.Y;
			Result.Y = PreviousX;

			const int32 PreviousSizeX = EffectiveSize.X;
			EffectiveSize.X = EffectiveSize.Y;
			EffectiveSize.Y = PreviousSizeX;
		}

		return Result;
	}

	FIntVector RotateMeshOffsetPositionYaw(const FIntVector& OffsetPosition, const int32 YawRotationSteps)
	{
		FIntVector Result = OffsetPosition;
		for (int32 Step = 0; Step < NormalizeYawRotationSteps(YawRotationSteps); ++Step)
		{
			const int32 PreviousX = Result.X;
			Result.X = -Result.Y;
			Result.Y = PreviousX;
		}

		return Result;
	}

	FIntVector RotateMeshOffsetRotationYaw(const FIntVector& OffsetRotation, const int32 YawRotationSteps)
	{
		const int32 NormalizedSteps = NormalizeYawRotationSteps(YawRotationSteps);
		if (NormalizedSteps == 0)
		{
			return OffsetRotation;
		}

		const FQuat TemplateYaw = FQuat(FVector::UpVector, FMath::DegreesToRadians(static_cast<float>(NormalizedSteps) * 90.0f));
		const FQuat MeshRotation = FRotator(
			static_cast<float>(OffsetRotation.X),
			static_cast<float>(OffsetRotation.Y),
			static_cast<float>(OffsetRotation.Z)).Quaternion();
		const FRotator FinalRotation = (TemplateYaw * MeshRotation).Rotator();
		return FIntVector(
			FMath::RoundToInt(FinalRotation.Pitch),
			FMath::RoundToInt(FinalRotation.Yaw),
			FMath::RoundToInt(FinalRotation.Roll));
	}

	FIntVector RotateRuntimePlacementCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		switch (NormalizeYawRotationSteps(YawRotationSteps))
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

	uint64 BuildResolvedConnectorRecordKey(const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const uint64 SitePairKey = HashCombine(
			GetTypeHash(ConnectorRecord.StartSiteReservationKey),
			GetTypeHash(ConnectorRecord.EndSiteReservationKey));
		const uint64 BaseKey = HashCombineFast(
			HashCombineFast(
				static_cast<uint32>(SitePairKey),
				GetTypeHash(ConnectorRecord.ContinuationFamilyId)),
			GetTypeHash(ConnectorRecord.ConnectorTypeTag));
		if (ConnectorRecord.ContinuationRouteId.IsNone()
			|| ConnectorRecord.ContinuationSegmentIndex == INDEX_NONE)
		{
			return BaseKey;
		}
		return HashCombineFast(
			static_cast<uint32>(BaseKey),
			HashCombineFast(
				GetTypeHash(ConnectorRecord.ContinuationRouteId),
				GetTypeHash(ConnectorRecord.ContinuationSegmentIndex)));
	}

	void AppendRuntimeConnectorEndpointBoundaryStripColumns(
		const FLayoutSolveResult& SolveResult,
		const FIntVector& PathOriginBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntVector& EndpointBlockWorldPos,
		const ELayoutFaceDirection EndpointFacingDirection,
		TSet<FIntPoint>& InOutColumns);

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

	TArray<FIntVector> ResolveRuntimePlacementOccupiedLocalCells(const FLayoutPlacedModule& Placement)
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

	void AppendRuntimeConnectorEndpointBoundaryStripColumns(
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
		const bool bMatchX = EndpointFacingDirection == ELayoutFaceDirection::NegX
			|| EndpointFacingDirection == ELayoutFaceDirection::PosX;

		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			TArray<FIntVector> OccupiedLocalCells = ResolveRuntimePlacementOccupiedLocalCells(Placement);
			if (OccupiedLocalCells.IsEmpty())
			{
				OccupiedLocalCells = {FIntVector::ZeroValue};
			}

			const FIntPoint RotationFootprint = ResolveOccupiedLocalCellRotationFootprint(OccupiedLocalCells);
			for (const FIntVector& OccupiedLocalCell : OccupiedLocalCells)
			{
				const FIntVector RotatedLocalCell = RotateRuntimePlacementCellInFootprintYaw(
					OccupiedLocalCell,
					RotationFootprint,
					Placement.YawRotationSteps);
				const FIntVector RootLocalCell = Placement.Cell + RotatedLocalCell;
				const bool bMatchesEndpointBoundary = bMatchX
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
	}

	FIntPoint ResolvePlacementRotationFootprintCells(const FLayoutPlacedModule& Placement)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolveRuntimePlacementOccupiedLocalCells(Placement);
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

	FIntPoint ResolvePlacementTerrainFootprintSizeInBlocks(
		const FLayoutPlacedModule& Placement,
		const FIntVector& SharedCellSizeInBlocks)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolveRuntimePlacementOccupiedLocalCells(Placement);
		if (Placement.BundleBoundsCells != FIntVector::ZeroValue || !OccupiedLocalCells.IsEmpty())
		{
			const FIntPoint RotationFootprintCells = ResolvePlacementRotationFootprintCells(Placement);
			return FIntPoint(
				FMath::Max(1, RotationFootprintCells.X * SharedCellSizeInBlocks.X),
				FMath::Max(1, RotationFootprintCells.Y * SharedCellSizeInBlocks.Y));
		}

		if (Placement.Module != nullptr)
		{
			return FIntPoint(
				FMath::Max(1, SharedCellSizeInBlocks.X),
				FMath::Max(1, SharedCellSizeInBlocks.Y));
		}

		return FIntPoint(
			FMath::Max(1, SharedCellSizeInBlocks.X),
			FMath::Max(1, SharedCellSizeInBlocks.Y));
	}

	bool PlaceTemplateAtYawRotation(
		UChunkStructureTemplate* Template,
		AChunkWorldExtended* ChunkWorld,
		const FIntVector& AnchorBlockWorldPos,
		const int32 YawRotationSteps,
		FString& OutReport)
	{
		if (Template == nullptr || ChunkWorld == nullptr)
		{
			OutReport = TEXT("PlaceTemplateAtYawRotation: Template or chunk world is null.");
			return false;
		}

		const int32 NormalizedSteps = NormalizeYawRotationSteps(YawRotationSteps);
		UChunkStructureTemplate* RuntimeTemplate = UChunkStructureTemplate::CreateRuntimeInstance(Template, ChunkWorld);
		if (RuntimeTemplate == nullptr)
		{
			OutReport = TEXT("PlaceTemplateAtYawRotation: Failed to create runtime template instance.");
			return false;
		}

		FString ValidationReport;
		RuntimeTemplate->ValidateAndRemapForWorld(ChunkWorld, ValidationReport);
		OutReport = ValidationReport;

		TArray<FIntVector> ClearWorldPositions;
		TArray<int32> ClearBlockMaterials;
		TArray<FMeshData> ClearMeshData;
		if (RuntimeTemplate->SizeInBlocks.X > 0
			&& RuntimeTemplate->SizeInBlocks.Y > 0
			&& RuntimeTemplate->SizeInBlocks.Z > 0)
		{
			const int32 TemplateVolume =
				RuntimeTemplate->SizeInBlocks.X * RuntimeTemplate->SizeInBlocks.Y * RuntimeTemplate->SizeInBlocks.Z;
			ClearWorldPositions.Reserve(TemplateVolume);
			ClearBlockMaterials.Reserve(TemplateVolume);
			ClearMeshData.Reserve(TemplateVolume);

			// Project-specific layout stamping contract: empty cells inside the authored template
			// volume are carve space and must clear overlapped terrain to real air before solids replay.
			for (int32 Z = 0; Z < RuntimeTemplate->SizeInBlocks.Z; ++Z)
			{
				for (int32 Y = 0; Y < RuntimeTemplate->SizeInBlocks.Y; ++Y)
				{
					for (int32 X = 0; X < RuntimeTemplate->SizeInBlocks.X; ++X)
					{
						ClearWorldPositions.Add(
							AnchorBlockWorldPos
							+ RotateTemplateRelativeBlockPosYaw(
								FIntVector(X, Y, Z),
								RuntimeTemplate->SizeInBlocks,
								NormalizedSteps));
						ClearBlockMaterials.Add(EmptyMaterial);

						FMeshData EmptyMeshData;
						EmptyMeshData.MeshId = EmptyMesh;
						ClearMeshData.Add(EmptyMeshData);
					}
				}
			}
		}

		TArray<FIntVector> BlockWorldPositions;
		TArray<int32> BlockMaterials;
		if (!RuntimeTemplate->Blocks.IsEmpty())
		{
			BlockWorldPositions.Reserve(RuntimeTemplate->Blocks.Num());
			BlockMaterials.Reserve(RuntimeTemplate->Blocks.Num());
			for (const FBlockTemplateEntry& Block : RuntimeTemplate->Blocks)
			{
				BlockWorldPositions.Add(
					AnchorBlockWorldPos + RotateTemplateRelativeBlockPosYaw(Block.RelativeBlockPos, RuntimeTemplate->SizeInBlocks, NormalizedSteps));
				BlockMaterials.Add(Block.MaterialIndex);
			}
		}

		TArray<FIntVector> MeshWorldPositions;
		TArray<FMeshData> MeshData;
		if (!RuntimeTemplate->Meshes.IsEmpty())
		{
			MeshWorldPositions.Reserve(RuntimeTemplate->Meshes.Num());
			MeshData.Reserve(RuntimeTemplate->Meshes.Num());
			for (const FMeshTemplateEntry& Mesh : RuntimeTemplate->Meshes)
			{
				FMeshData RotatedMeshData = Mesh.MeshData;
				RotatedMeshData.OffPos = RotateMeshOffsetPositionYaw(Mesh.MeshData.OffPos, NormalizedSteps);
				RotatedMeshData.OffRot = RotateMeshOffsetRotationYaw(Mesh.MeshData.OffRot, NormalizedSteps);
				MeshWorldPositions.Add(
					AnchorBlockWorldPos + RotateTemplateRelativeBlockPosYaw(Mesh.RelativeBlockPos, RuntimeTemplate->SizeInBlocks, NormalizedSteps));
				MeshData.Add(RotatedMeshData);
			}
		}

		if (!ClearWorldPositions.IsEmpty())
		{
			ChunkWorld->SetBlockValuesByBlockWorldPos(ClearWorldPositions, ClearBlockMaterials, true);
			ChunkWorld->SetMeshDatasByBlockWorldPos(ClearWorldPositions, ClearMeshData, true);
		}
		if (!BlockWorldPositions.IsEmpty())
		{
			ChunkWorld->SetBlockValuesByBlockWorldPos(BlockWorldPositions, BlockMaterials, true);
		}
		if (!MeshWorldPositions.IsEmpty())
		{
			ChunkWorld->SetMeshDatasByBlockWorldPos(MeshWorldPositions, MeshData, true);
		}

		return true;
	}

	bool TryPlaceSolvedTemplatePath(
		AChunkWorldExtended* ChunkWorld,
		const FSoftObjectPath& TemplatePath,
		const FIntVector& AnchorBlockWorldPos,
		const int32 YawRotationSteps,
		FString& OutFailureReason)
	{
		UChunkStructureTemplate* const Template = Cast<UChunkStructureTemplate>(TemplatePath.TryLoad());
		if (Template == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Solved placement template path '%s' did not load a chunk structure template."),
				*TemplatePath.ToString());
			return false;
		}

		FString PlacementReport;
		if (!PlaceTemplateAtYawRotation(Template, ChunkWorld, AnchorBlockWorldPos, YawRotationSteps, PlacementReport))
		{
			OutFailureReason = FString::Printf(
				TEXT("Failed to place template path '%s' at anchor=%s. %s"),
				*TemplatePath.ToString(),
				*AnchorBlockWorldPos.ToString(),
				*PlacementReport);
			return false;
		}

		return true;
	}

	bool TryPlaceSolvedLeafModule(
		AChunkWorldExtended* ChunkWorld,
		ULayoutModuleAsset* Module,
		const FIntVector& AnchorBlockWorldPos,
		const int32 YawRotationSteps,
		FString& OutFailureReason)
	{
		if (Module == nullptr)
		{
			OutFailureReason = TEXT("Solved placement did not preserve a leaf module asset.");
			return false;
		}

		UChunkStructureTemplate* const Template =
			ResolveResidentOrLoadSoftObject(Module->Template);
		if (Template == nullptr)
		{
			OutFailureReason = FString::Printf(
				TEXT("Placement module %s did not load its template."),
				*GetNameSafe(Module));
			return false;
		}

		FString PlacementReport;
		if (!PlaceTemplateAtYawRotation(Template, ChunkWorld, AnchorBlockWorldPos, YawRotationSteps, PlacementReport))
		{
			OutFailureReason = FString::Printf(
				TEXT("Failed to place module %s at anchor=%s. %s"),
				*GetNameSafe(Module),
				*AnchorBlockWorldPos.ToString(),
				*PlacementReport);
			return false;
		}

		return true;
	}

	/** Stamps a multi-cell composite from pointer-free local descriptors frozen by the solver snapshot. */
	bool TryPlaceSolvedFrozenCompositeLeaves(
		AChunkWorldExtended* ChunkWorld,
		const FLayoutPlacedModule& Placement,
		const FIntVector& BundleAnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		FString& OutFailureReason)
	{
		if (Placement.OccupiedLocalCells.Num() <= 1
			|| Placement.LocalCellFaceRules.Num() != Placement.OccupiedLocalCells.Num())
		{
			OutFailureReason = TEXT("Composite realization requires one frozen local-cell descriptor for every occupied cell.");
			return false;
		}

		// Load every leaf before first world write. A bad shadow descriptor must not
		// leave an already-stamped root leaf behind.
		TArray<UChunkStructureTemplate*> LeafTemplates;
		TArray<const FLayoutPlacedLocalCellFaceRuleSnapshot*> LeafDescriptors;
		LeafTemplates.Reserve(Placement.OccupiedLocalCells.Num());
		LeafDescriptors.Reserve(Placement.OccupiedLocalCells.Num());
		for (const FIntVector& LocalCell : Placement.OccupiedLocalCells)
		{
			const FLayoutPlacedLocalCellFaceRuleSnapshot* const Descriptor =
				Placement.LocalCellFaceRules.FindByPredicate(
					[&LocalCell](const FLayoutPlacedLocalCellFaceRuleSnapshot& Candidate)
					{
						return Candidate.LocalCell == LocalCell;
					});
			if (Descriptor == nullptr || !Descriptor->TemplatePath.IsValid())
			{
				OutFailureReason = FString::Printf(
					TEXT("Composite realization requires a valid frozen template path for local cell %s."),
					*LocalCell.ToString());
				return false;
			}

			UChunkStructureTemplate* const Template = Cast<UChunkStructureTemplate>(Descriptor->TemplatePath.TryLoad());
			if (Template == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Composite realization template path '%s' did not load for local cell %s."),
					*Descriptor->TemplatePath.ToString(),
					*LocalCell.ToString());
				return false;
			}
			LeafTemplates.Add(Template);
			LeafDescriptors.Add(Descriptor);
		}

		const FIntPoint RotationFootprintCells = ResolvePlacementRotationFootprintCells(Placement);
		for (int32 LocalCellIndex = 0; LocalCellIndex < Placement.OccupiedLocalCells.Num(); ++LocalCellIndex)
		{
			const FIntVector& LocalCell = Placement.OccupiedLocalCells[LocalCellIndex];
			const FLayoutPlacedLocalCellFaceRuleSnapshot* const Descriptor = LeafDescriptors[LocalCellIndex];
			const FIntVector RotatedLocalCell = RotateRuntimePlacementCellInFootprintYaw(
				LocalCell,
				RotationFootprintCells,
				Placement.YawRotationSteps);
			const FIntVector LeafAnchorBlockWorldPos = BundleAnchorBlockWorldPos + FIntVector(
				RotatedLocalCell.X * SharedCellSizeInBlocks.X,
				RotatedLocalCell.Y * SharedCellSizeInBlocks.Y,
				RotatedLocalCell.Z * SharedCellSizeInBlocks.Z);
			const int32 LeafYawRotationSteps = NormalizeYawRotationSteps(
				Placement.YawRotationSteps + Descriptor->RelativeYawRotationSteps);
			FString PlacementReport;
			if (!PlaceTemplateAtYawRotation(
					LeafTemplates[LocalCellIndex],
					ChunkWorld,
					LeafAnchorBlockWorldPos,
					LeafYawRotationSteps,
					PlacementReport))
			{
				OutFailureReason = FString::Printf(
					TEXT("Failed to realize frozen composite local cell %s at anchor=%s. %s"),
					*LocalCell.ToString(),
					*LeafAnchorBlockWorldPos.ToString(),
					*PlacementReport);
				return false;
			}
		}

		return true;
	}

	bool TryPlaceSolvedPlacementBundle(
		AChunkWorldExtended* ChunkWorld,
		const FLayoutPlacedModule& Placement,
		const FIntVector& BundleAnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		FString& OutFailureReason)
	{
		if (Placement.OccupiedLocalCells.Num() > 1 && !Placement.LocalCellFaceRules.IsEmpty())
		{
			return TryPlaceSolvedFrozenCompositeLeaves(
				ChunkWorld,
				Placement,
				BundleAnchorBlockWorldPos,
				SharedCellSizeInBlocks,
				OutFailureReason);
		}

		// The payload selected by the proof wins over live/rehydrated carriers,
		// including cached results whose authoring assets have since changed.
		if (!Placement.TemplatePath.IsNull())
		{
			return TryPlaceSolvedTemplatePath(ChunkWorld, Placement.TemplatePath,
				BundleAnchorBlockWorldPos, Placement.YawRotationSteps, OutFailureReason);
		}
		if (Placement.Module != nullptr)
		{
			return TryPlaceSolvedLeafModule(
				ChunkWorld,
				Placement.Module.Get(),
				BundleAnchorBlockWorldPos,
				Placement.YawRotationSteps,
				OutFailureReason);
		}

		if (Placement.CompositeModule == nullptr)
		{
			// No module, composite, or template path -- perimeter-only realization
			// (terrain cuts/fills/ramps without a structural template).  Nothing to place.
			return true;
		}

		const FIntPoint RotationFootprintCells = ResolvePlacementRotationFootprintCells(Placement);
		for (const FLayoutCompositeModuleCell& CompositeCell : Placement.CompositeModule->Cells)
		{
			if (CompositeCell.Module == nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("Composite placement %s contains an empty leaf cell at %s."),
					*GetNameSafe(Placement.CompositeModule.Get()),
					*CompositeCell.LocalCell.ToString());
				return false;
			}

			const FIntVector RotatedLocalCell =
				RotateRuntimePlacementCellInFootprintYaw(CompositeCell.LocalCell, RotationFootprintCells, Placement.YawRotationSteps);
			const FIntVector LeafAnchorBlockWorldPos = BundleAnchorBlockWorldPos + FIntVector(
				RotatedLocalCell.X * SharedCellSizeInBlocks.X,
				RotatedLocalCell.Y * SharedCellSizeInBlocks.Y,
				RotatedLocalCell.Z * SharedCellSizeInBlocks.Z);
			const int32 LeafYawRotationSteps =
				NormalizeYawRotationSteps(Placement.YawRotationSteps + CompositeCell.RelativeYawRotationSteps);
			if (!TryPlaceSolvedLeafModule(
				ChunkWorld,
				CompositeCell.Module.Get(),
				LeafAnchorBlockWorldPos,
				LeafYawRotationSteps,
				OutFailureReason))
			{
				OutFailureReason = FString::Printf(
					TEXT("Failed to realize composite placement %s leaf %s at localCell=%s anchor=%s. %s"),
					*GetNameSafe(Placement.CompositeModule.Get()),
					*GetNameSafe(CompositeCell.Module.Get()),
					*CompositeCell.LocalCell.ToString(),
					*LeafAnchorBlockWorldPos.ToString(),
					*OutFailureReason);
				return false;
			}
		}

		return true;
	}

	/** Game-thread coverage for the same leaf carriers and yaw transforms used by TryPlaceSolvedPlacementBundle.
	 * Include the entire carved volume, not just occupied blocks or the anchor. Missing carriers fail closed. */
	bool TryAddPlacementTemplateChunkOrigins(
		const FLayoutPlacedModule& Placement,
		const FIntVector& BundleAnchor,
		const FIntVector& CellSize,
		const FIntVector& ChunkSize,
		TSet<FIntVector>& Origins)
	{
		check(IsInGameThread());
		if (ChunkSize.GetMin() <= 0) return false;
		const auto AddTemplate = [&](const UChunkStructureTemplate* Template, const FIntVector Anchor, const int32 Yaw)
		{
			if (!Template) return false;
			const auto AddPosition = [&](const FIntVector Relative)
			{
				Origins.Add(FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(
					Anchor + RotateTemplateRelativeBlockPosYaw(Relative, Template->SizeInBlocks, Yaw), ChunkSize));
			};
			if (Template->SizeInBlocks.GetMin() > 0)
			{
				const FIntVector A = Anchor + RotateTemplateRelativeBlockPosYaw(FIntVector::ZeroValue, Template->SizeInBlocks, Yaw);
				const FIntVector B = Anchor + RotateTemplateRelativeBlockPosYaw(Template->SizeInBlocks - FIntVector(1), Template->SizeInBlocks, Yaw);
				const FIntVector Min = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(A.ComponentMin(B), ChunkSize);
				const FIntVector Max = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(A.ComponentMax(B), ChunkSize);
				for (int64 Z = Min.Z; Z <= Max.Z; Z += ChunkSize.Z)
				for (int64 Y = Min.Y; Y <= Max.Y; Y += ChunkSize.Y)
				for (int64 X = Min.X; X <= Max.X; X += ChunkSize.X)
					Origins.Add(FIntVector(int32(X), int32(Y), int32(Z)));
			}
			// Authored entries can extend beyond SizeInBlocks; native stamping still replays them.
			for (const FBlockTemplateEntry& Block : Template->Blocks) AddPosition(Block.RelativeBlockPos);
			for (const FMeshTemplateEntry& Mesh : Template->Meshes) AddPosition(Mesh.RelativeBlockPos);
			return true;
		};
		const FIntPoint Footprint = ResolvePlacementRotationFootprintCells(Placement);
		const auto LeafAnchor = [&](const FIntVector LocalCell)
		{
			const FIntVector Cell = RotateRuntimePlacementCellInFootprintYaw(LocalCell, Footprint, Placement.YawRotationSteps);
			return BundleAnchor + FIntVector(Cell.X * CellSize.X, Cell.Y * CellSize.Y, Cell.Z * CellSize.Z);
		};
		if (Placement.OccupiedLocalCells.Num() > 1 && !Placement.LocalCellFaceRules.IsEmpty())
		{
			if (Placement.LocalCellFaceRules.Num() != Placement.OccupiedLocalCells.Num()) return false;
			for (const FIntVector LocalCell : Placement.OccupiedLocalCells)
			{
				const auto* Descriptor = Placement.LocalCellFaceRules.FindByPredicate(
					[LocalCell](const FLayoutPlacedLocalCellFaceRuleSnapshot& Item) { return Item.LocalCell == LocalCell; });
				if (!Descriptor || !Descriptor->TemplatePath.IsValid()
					|| !AddTemplate(Cast<UChunkStructureTemplate>(Descriptor->TemplatePath.TryLoad()), LeafAnchor(LocalCell),
						NormalizeYawRotationSteps(Placement.YawRotationSteps + Descriptor->RelativeYawRotationSteps))) return false;
			}
			return true;
		}
		if (!Placement.TemplatePath.IsNull())
			return AddTemplate(Cast<UChunkStructureTemplate>(Placement.TemplatePath.TryLoad()), BundleAnchor, Placement.YawRotationSteps);
		if (Placement.Module)
			return AddTemplate(ResolveResidentOrLoadSoftObject(Placement.Module->Template), BundleAnchor, Placement.YawRotationSteps);
		if (Placement.CompositeModule)
		{
			for (const FLayoutCompositeModuleCell& Cell : Placement.CompositeModule->Cells)
				if (!Cell.Module || !AddTemplate(ResolveResidentOrLoadSoftObject(Cell.Module->Template), LeafAnchor(Cell.LocalCell),
					NormalizeYawRotationSteps(Placement.YawRotationSteps + Cell.RelativeYawRotationSteps))) return false;
		}
		// Perimeter-only placements have no structural template; their frozen terrain writes supply coverage.
		return true;
	}

	ELayoutWorldBindingPlacementKind ResolveRuntimeConnectorPlacementKind(
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

	FLayoutWorldBindingPlacementPolicy ResolveRuntimeConnectorPlacementPolicy(
		const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		const auto NormalizeContinuationPlacementPolicy =
			[&FrontendSelection, &ConnectorRecord](FLayoutWorldBindingPlacementPolicy PlacementPolicy)
		{
			const ELayoutWorldBindingPlacementKind ResolvedPlacementKind =
				ResolveRuntimeConnectorPlacementKind(ConnectorRecord);
			const bool bContinuationPlacement =
				ResolvedPlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
				|| ResolvedPlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation
				|| ResolvedPlacementKind == ELayoutWorldBindingPlacementKind::TunnelContinuation;
			if (bContinuationPlacement)
			{
				// Runtime connector records can republish placement policy from the
				// stored continuation family. Keep that replay path on the same
				// slope ceiling as connector path discovery and runtime-view request
				// rebuild so stepped carrier evaluation does not drift.
			}
			return PlacementPolicy;
		};
		if (ResolveRuntimeConnectorPlacementKind(ConnectorRecord)
			!= ELayoutWorldBindingPlacementKind::OrdinaryRoot)
		{
			return NormalizeContinuationPlacementPolicy(
				FrontendSelection.WorldBindingPlacementPolicy);
		}

		return NormalizeContinuationPlacementPolicy(
			ConnectorRecord.SolveResult.WorldBindingPlacementPolicy);
	}

	bool RequiresNormalizedContinuationPlacementCarrier(
		const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		return !FrontendSelection.ContinuationFamilyId.IsNone()
			|| !FrontendSelection.ContinuationFamilyCandidateId.IsNone()
			|| FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel
				!= INDEX_NONE;
	}

	bool HasNormalizedContinuationPlacementCarrier(
		const FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutResolvedConnectorFrontendSelection FrontendSelection =
			ConnectorRecord.GetResolvedConnectorFrontendSelection();
		const auto IsContinuationPlacementKind =
			[](const ELayoutWorldBindingPlacementKind PlacementKind)
			{
				return PlacementKind
					== ELayoutWorldBindingPlacementKind::SurfacePath
					|| PlacementKind
						== ELayoutWorldBindingPlacementKind::BridgeContinuation
					|| PlacementKind
						== ELayoutWorldBindingPlacementKind::TunnelContinuation;
			};
		return IsContinuationPlacementKind(FrontendSelection.PlacementKind)
			|| IsContinuationPlacementKind(
				FrontendSelection.ResolvedContinuationSelection.PlacementKind)
			|| IsContinuationPlacementKind(
				ConnectorRecord.SolveResult.RootPlacementKind)
			|| IsContinuationPlacementKind(
				ResolveRuntimeConnectorPlacementKind(ConnectorRecord));
	}

	bool ShouldApplySteppedOrdinaryRootTerrainAnchor(const FLayoutSolveResult& SolveResult)
	{
		return SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
			&& SolveResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
			&& !SolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty();
	}

	FLayoutId BuildRuntimeSolvedArtifactId(const FLayoutId SourceId)
	{
		return SourceId.IsNone()
			? NAME_None
			: FLayoutId(*FString::Printf(TEXT("%s.Solved"), *SourceId.ToString()));
	}

	/** Returns whether a rejected solve retains placements that may be previewed and applied. */
	bool HasRetainedPartialPlacements(const FLayoutSolveResult& SolveResult)
	{
		return !SolveResult.bSucceeded && !SolveResult.Placements.IsEmpty();
	}

	/**
	 * Gives an explicit continuation failure enough request-owned topology for
	 * root-equivalent preview diagnostics when preflight rejects before CSP.
	 */
	void PopulateFailedContinuationPreviewSolveResult(
		FResolvedLayoutConnectorRecord& InOutRecord,
		const FLayoutPreparedContinuation& PreparedContinuation)
	{
		FLayoutSolveResult& SolveResult = InOutRecord.SolveResult;
		if (!SolveResult.PlannedCells.IsEmpty())
		{
			return;
		}

		SolveResult.bSucceeded = false;
		SolveResult.PlannedCells = !PreparedContinuation.SolveRequest.PlannedCells.IsEmpty()
			? PreparedContinuation.SolveRequest.PlannedCells
			: PreparedContinuation.PlannedCells;
		SolveResult.FootprintSize = PreparedContinuation.SolveRequest.FootprintSize != FIntPoint::ZeroValue
			? PreparedContinuation.SolveRequest.FootprintSize
			: PreparedContinuation.FootprintSize;
		SolveResult.SharedCellSizeInBlocks = InOutRecord.FrontendSharedCellSizeInBlocks;
		// Preflight failures do not reach the solver, so preserve request-owned
		// render alignment and binding template offset for preview parity.
		SolveResult.TemplatePlacementZOffsetBlocks = PreparedContinuation.SolveRequest.TemplatePlacementZOffsetBlocks;
		SolveResult.RootPlacementKind = PreparedContinuation.SolveRequest.RootPlacementKind;
		SolveResult.WorldBindingPlacementPolicy = PreparedContinuation.SolveRequest.WorldBindingPlacementPolicy;
		SolveResult.ResolvedTerrainAlignmentLevel =
			PreparedContinuation.SolveRequest.RootContinuationSelection.ResolvedEntryLevel;
	}

	/** Copies adapter-owned terrain metadata into preview-only cells for accepted and rejected continuations. */
	void PopulateContinuationPreviewTerrainCells(
		FLayoutContinuationPreviewGeometry& InOutPreviewGeometry,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		const TArray<FLayoutContractActiveCellRecord>* const ActiveCells = nullptr)
	{
		InOutPreviewGeometry.TerrainCells.Reset();
		InOutPreviewGeometry.TerrainCells.Reserve(ActiveCells != nullptr ? ActiveCells->Num() : PlannedCells.Num());
		const auto AppendPlannedCell = [&InOutPreviewGeometry, &FrozenTerrainContract](const FLayoutPlannedCell& PlannedCell)
		{
			FLayoutContinuationPreviewTerrainCell& TerrainCell =
				InOutPreviewGeometry.TerrainCells.AddDefaulted_GetRef();
			TerrainCell.LocalCell = PlannedCell.Cell;
			TerrainCell.Intent = PlannedCell.Intent;
			TerrainCell.PlacementZone = PlannedCell.PlacementZone;
			TerrainCell.TerrainSeamFaceMask = PlannedCell.TerrainSeamFaceMask;
			TerrainCell.bIsBridgeCell = PlannedCell.bIsBridgeCell;
			const FLayoutFrozenTerrainStageCellRecord* const Stage =
				FrozenTerrainContract.StageMap.FindByPredicate([&TerrainCell](const FLayoutFrozenTerrainStageCellRecord& Candidate)
				{
					return Candidate.FootprintCellXY == FIntPoint(TerrainCell.LocalCell.X, TerrainCell.LocalCell.Y);
				});
			TerrainCell.BaseBlockWorldZ = Stage != nullptr
				? Stage->ResolvedStageBaseBlockWorldZ
				: InOutPreviewGeometry.PathOriginBlockWorldPos.Z;
		};
		if (ActiveCells == nullptr)
		{
			for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
			{
				AppendPlannedCell(PlannedCell);
			}
		}
		else
		{
			for (const FLayoutContractActiveCellRecord& ActiveCell : *ActiveCells)
			{
				const FLayoutPlannedCell* const PlannedCell = PlannedCells.FindByPredicate(
					[&ActiveCell](const FLayoutPlannedCell& Candidate)
					{
						return Candidate.Cell == ActiveCell.Cell;
					});
				checkf(PlannedCell != nullptr,
					TEXT("Frozen continuation contract active cell %s lacks finalized planned-cell metadata."),
					*ActiveCell.Cell.ToString());
				AppendPlannedCell(*PlannedCell);
			}
		}
		InOutPreviewGeometry.TerrainCells.Sort([](
			const FLayoutContinuationPreviewTerrainCell& Left,
			const FLayoutContinuationPreviewTerrainCell& Right)
		{
			return Left.LocalCell.Z != Right.LocalCell.Z
				? Left.LocalCell.Z < Right.LocalCell.Z
				: (Left.LocalCell.Y != Right.LocalCell.Y
					? Left.LocalCell.Y < Right.LocalCell.Y
					: Left.LocalCell.X < Right.LocalCell.X);
		});
	}

	/** Copies route and solver failures into preview-only authoring diagnostics. */
	void PopulateContinuationPreviewSolveDiagnostics(
		FLayoutContinuationPreviewGeometry& InOutPreviewGeometry,
		const FLayoutSolveResult& SolveResult,
		const FString& LifecycleFailureReason = FString())
	{
		for (const FLayoutRouteConstraintRecord& Constraint : SolveResult.RouteConstraints)
		{
			FString RequiredFaces;
			for (const FLayoutRouteFaceRequirement& Face : Constraint.FaceRequirements)
			{
				RequiredFaces += FString::Printf(TEXT("%s:%s "),
					*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(Face.FaceDirection)),
					*Face.TraversalChannel.ToString());
			}
			FLayoutContinuationPreviewDiagnostic& Diagnostic = InOutPreviewGeometry.Diagnostics.AddDefaulted_GetRef();
			Diagnostic.LocalCell = Constraint.Cell;
			Diagnostic.bHasLocalCell = true;
			Diagnostic.Stage = TEXT("Route requirement");
			Diagnostic.Requirement = FString::Printf(TEXT("%s (%s)"),
				*StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(Constraint.Intent)),
				*RequiredFaces);
		}
		if (SolveResult.RouteDomainFilterDiagnostics.bFailedConstraint)
		{
			FLayoutContinuationPreviewDiagnostic& Diagnostic = InOutPreviewGeometry.Diagnostics.AddDefaulted_GetRef();
			Diagnostic.LocalCell = SolveResult.RouteDomainFilterDiagnostics.FailedConstraintCell;
			Diagnostic.bHasLocalCell = true;
			Diagnostic.Stage = TEXT("Route-domain rejection");
			Diagnostic.Requirement = StaticEnum<ELayoutRouteDomainFailureKind>()->GetNameStringByValue(
				static_cast<int64>(SolveResult.RouteDomainFilterDiagnostics.FailedConstraintDominantFailureKind));
			Diagnostic.Detail = SolveResult.FailureReason;
		}
		if (!LifecycleFailureReason.IsEmpty() || !SolveResult.FailureReason.IsEmpty())
		{
			FLayoutContinuationPreviewDiagnostic& Diagnostic = InOutPreviewGeometry.Diagnostics.AddDefaulted_GetRef();
			Diagnostic.Stage = TEXT("Solve/artifact rejection");
			Diagnostic.Detail = !LifecycleFailureReason.IsEmpty() ? LifecycleFailureReason : SolveResult.FailureReason;
		}
	}

	/** Copies frozen VerticalAccess admission evidence into preview-only diagnostics. */
	void AppendContinuationPreviewVerticalAccessDiagnostics(
		FLayoutContinuationPreviewGeometry& InOutPreviewGeometry,
		const FLayoutAdapterOutput& AdapterOutput)
	{
		for (const FLayoutVerticalAccessHostGroup& HostGroup : AdapterOutput.VerticalAccessHostGroups)
		{
			for (const FLayoutVerticalAccessHostOption& Option : HostGroup.Options)
			{
				FLayoutContinuationPreviewDiagnostic& Diagnostic = InOutPreviewGeometry.Diagnostics.AddDefaulted_GetRef();
				Diagnostic.LocalCell = Option.LowerCell;
				Diagnostic.bHasLocalCell = true;
				Diagnostic.Stage = TEXT("VerticalAccess host");
				Diagnostic.Requirement = FString::Printf(TEXT("Group %s, upper landing %s, tier %s"),
					*HostGroup.GroupId.ToString(), *Option.UpperCell.ToString(),
					*StaticEnum<ELayoutVerticalAccessHostTier>()->GetNameStringByValue(static_cast<int64>(Option.Tier)));
			}
			if (!HostGroup.FirstRejectedAdmission.IsEmpty())
			{
				FLayoutContinuationPreviewDiagnostic& Diagnostic = InOutPreviewGeometry.Diagnostics.AddDefaulted_GetRef();
				Diagnostic.LocalCell = HostGroup.DeckCell;
				Diagnostic.bHasLocalCell = true;
				Diagnostic.Stage = TEXT("VerticalAccess admission rejection");
				Diagnostic.Detail = HostGroup.FirstRejectedAdmission;
			}
		}
	}

	/** Resolves immutable publication authority without rewriting the solve verdict. */
	ELayoutCachedApplyability ResolveCachedApplyability(const FLayoutSolveResult& SolveResult)
	{
		if (SolveResult.bSucceeded)
		{
			return ELayoutCachedApplyability::Solved;
		}
		return HasRetainedPartialPlacements(SolveResult)
			? ELayoutCachedApplyability::Partial
			: ELayoutCachedApplyability::Rejected;
	}

	bool ValidateRuntimeSolvedArtifactForPublish(
		const FLayoutSolvedArtifact& Artifact,
		const FLayoutId ExpectedArtifactId,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (!Artifact.bHasProducedArtifact)
		{
			OutFailureReason = TEXT("Runtime publish requires a produced solved artifact.");
			return false;
		}
		if (ExpectedArtifactId.IsNone() || Artifact.ArtifactId != ExpectedArtifactId)
		{
			OutFailureReason = TEXT("Runtime publish rejected a stale solved artifact id.");
			return false;
		}
		if (Artifact.Status != ELayoutSolvedArtifactStatus::SolvedCellsComplete
			&& Artifact.Status != ELayoutSolvedArtifactStatus::WritePlanReady
			&& Artifact.Status != ELayoutSolvedArtifactStatus::AcceptedComplete)
		{
			OutFailureReason = TEXT("Runtime publish rejected solved artifact with non-publishable status.");
			return false;
		}
		if (Artifact.Placements.IsEmpty())
		{
			OutFailureReason = TEXT("Runtime publish rejected an empty solved artifact.");
			return false;
		}
		return true;
	}

} // anonymous namespace

	/** Transitional submit-side terrain evidence builder. C1/C2 deletion gate: replace with selected-scout/prewarm terrain producers. */
	FLayoutFrozenTerrainBiomeAdapterInput BuildTerrainBiomeAdapterInput(
		const FLayoutId ArtifactId,
		const FLayoutId ModePlanId,
		const FName EligibleBiomeRowName,
		const FIntVector SiteCenterBlockWorldPos,
		const FIntVector FootprintMinBlockWorldPos,
		const FIntVector SharedCellSizeInBlocks,
		const FIntPoint FootprintInCells,
		TFunctionRef<int32(int32 CellBlockX, int32 CellBlockY)> ResolveSurfaceZ,
		const int32 MaxFoundationDepth = 0)
	{
		FLayoutFrozenTerrainBiomeAdapterInput Input;
		Input.ArtifactId = ArtifactId;
		Input.ModePlanId = ModePlanId;
		Input.EligibleBiomeRowName = EligibleBiomeRowName;
		Input.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		Input.FootprintMinBlockWorldPos = FootprintMinBlockWorldPos;
		Input.FootprintSizeInBlocks = FIntPoint(
			FootprintInCells.X * SharedCellSizeInBlocks.X,
			FootprintInCells.Y * SharedCellSizeInBlocks.Y);
		Input.bHasTerrainPlacementEvidence = true;
		Input.bHasFiniteSearchBounds = true;
		Input.bHasSampledColumnEvidence = true;
		Input.SearchDepthBlocks = MaxFoundationDepth > 0 ? MaxFoundationDepth : SharedCellSizeInBlocks.Z;
		// Search bounds cover the exact footprint-cell sample envelope produced below.
		Input.SearchMinBlockXY = FIntPoint(
			FootprintMinBlockWorldPos.X,
			FootprintMinBlockWorldPos.Y);
		Input.SearchMaxBlockXY = FIntPoint(
			FootprintMinBlockWorldPos.X + FMath::Max(0, FootprintInCells.X - 1) * SharedCellSizeInBlocks.X,
			FootprintMinBlockWorldPos.Y + FMath::Max(0, FootprintInCells.Y - 1) * SharedCellSizeInBlocks.Y);

		const int32 CellCount = FootprintInCells.X * FootprintInCells.Y;
		Input.SurfaceSamples.Reserve(CellCount);
		Input.TerrainPlacementCells.Reserve(CellCount);

		const int32 SiteBaseBlockZ = SiteCenterBlockWorldPos.Z;
		const int32 CellHeightBlocks = SharedCellSizeInBlocks.Z;
		for (int32 CellY = 0; CellY < FootprintInCells.Y; ++CellY)
		{
			for (int32 CellX = 0; CellX < FootprintInCells.X; ++CellX)
			{
				const int32 CellBlockX = FootprintMinBlockWorldPos.X + CellX * SharedCellSizeInBlocks.X;
				const int32 CellBlockY = FootprintMinBlockWorldPos.Y + CellY * SharedCellSizeInBlocks.Y;
				const int32 CellBottomZ = SiteBaseBlockZ;
				const int32 CellTopZ = SiteBaseBlockZ + CellHeightBlocks;
				const int32 SurfaceZ = ResolveSurfaceZ(CellBlockX, CellBlockY);

				FLayoutTerrainSurfaceSample Sample;
				Sample.bIsValid = true;
				Sample.BlockXY = FIntPoint(CellBlockX, CellBlockY);
				Sample.SurfaceBlockWorldPos = FIntVector(CellBlockX, CellBlockY, SurfaceZ);
				Input.SurfaceSamples.Add(MoveTemp(Sample));

				FLayoutTerrainPlacementCellEvidence CellEvidence;
				CellEvidence.Cell = FIntVector(CellX, CellY, 0);
				CellEvidence.bPlaceableForSelectedMode = true;
				CellEvidence.TerrainStageIndex = 0;
				CellEvidence.VerticalShiftBlocks = 0;
				CellEvidence.ProvenanceId = TEXT("BuildTerrainBiomeAdapterInput");

				if (SurfaceZ > CellTopZ)
				{
					CellEvidence.bHasExcavationEvidence = true;
					CellEvidence.bHasLocalOverlapZ = true;
					CellEvidence.OverlapMinLocalZ = FLayoutLocalBlockCoord8(0);
					CellEvidence.OverlapMaxLocalZ = FLayoutLocalBlockCoord8(
						FMath::Min(SurfaceZ - CellBottomZ, 255));
				}
				else if (SurfaceZ < CellBottomZ)
				{
					CellEvidence.bHasFoundationFillEvidence = true;
				}
				else
				{
					CellEvidence.bHasClearanceEvidence = true;
				}

				// Entry traversability: sample outside-perimeter cells and classify boundary approach.
				const bool bOnBoundary = (CellX == 0 || CellX == FootprintInCells.X - 1
					|| CellY == 0 || CellY == FootprintInCells.Y - 1);
				if (bOnBoundary && MaxFoundationDepth > 0)
				{
					// Find the outside neighbor cell location (one cell beyond each boundary edge).
					int32 OutsideBlockX = CellBlockX;
					int32 OutsideBlockY = CellBlockY;
					if (CellX == 0)           { OutsideBlockX -= SharedCellSizeInBlocks.X; }
					else if (CellX == FootprintInCells.X - 1) { OutsideBlockX += SharedCellSizeInBlocks.X; }
					if (CellY == 0)           { OutsideBlockY -= SharedCellSizeInBlocks.Y; }
					else if (CellY == FootprintInCells.Y - 1) { OutsideBlockY += SharedCellSizeInBlocks.Y; }

					const int32 OutsideSurfaceZ = ResolveSurfaceZ(OutsideBlockX, OutsideBlockY);
					const int32 InsideSurfaceZ = SurfaceZ;
					const int32 Delta = FMath::Abs(InsideSurfaceZ - OutsideSurfaceZ);
					const int32 HalfFoundation = MaxFoundationDepth / 2;

					if (Delta <= HalfFoundation)
					{
						CellEvidence.EntryTraversability = ELayoutEntryTraversabilityVerdict::Walkable;
					}
					else if (Delta <= MaxFoundationDepth)
					{
						CellEvidence.EntryTraversability = ELayoutEntryTraversabilityVerdict::RampNeeded;
					}
					else if (InsideSurfaceZ < OutsideSurfaceZ)
					{
						CellEvidence.EntryTraversability = ELayoutEntryTraversabilityVerdict::ExcavationNeeded;
					}
					else
					{
						CellEvidence.EntryTraversability = ELayoutEntryTraversabilityVerdict::CliffEdge;
					}
				}
				Input.TerrainPlacementCells.Add(MoveTemp(CellEvidence));
			}
		}
		return Input;
	}


	// Requires valid root-scoped active cells even for legacy contracts without an ID.
	// Child-composed occupancy has a different scope and is not a cardinality checksum.
	static bool ValidateFrozenTerrainContractForAcceptance(
		const FLayoutFrozenTerrainContract& Contract,
		FString& OutFailureReason)
	{
		OutFailureReason.Reset();
		if (Contract.ActiveCells.IsEmpty())
		{
			OutFailureReason = TEXT("Frozen terrain contract requires active-cell provenance before acceptance.");
			return false;
		}
		if (!FLayoutContractPipeline::ValidateActiveCellRecords(Contract.ActiveCells, OutFailureReason))
		{
			return false;
		}
		return true;
	}

/** Wraps an already-frozen solve submission in the prewarm->preflight->solve lifecycle chain
 *  and submits the prewarm entry point to the dispatcher.  All three stages run on the
 *  background executor (async) or inline (sync tests).
 *
 *  @param SharedWorkerPacket When non-null, the prewarm completion updates this shared
 *    packet's RequestManifest with the finalized manifest (planned cells, entries, terrain).
 *    Callers that use a shared packet for solve work should pass it here so the prewarm's
 *    structural-contract derivation reaches the solve job. */
FLayoutBackgroundSolveHandle UChunkWorldLayoutRuntimeComponent::SubmitRootSolveViaLifecycleSequencer(
	FLayoutBackgroundSolveDispatcher& Dispatcher,
	const FString& DebugNamePrefix,
	const uint64 LayoutGroupId,
	const int32 Priority,
	FLayoutWorkerSolvePacket& WorkerSolvePacket,
	FLayoutBackgroundSolveSubmission&& SolveSubmission,
	TSharedPtr<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> SharedWorkerPacket)
{
	WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission = true;

	// Build the pre-submit frozen snapshot from the worker packet for descriptor-store consumption.
	FLayoutPreSubmitFrozenSnapshot PreSubmitSnapshot;
	PreSubmitSnapshot.SnapshotId = FLayoutId(*FString::Printf(TEXT("RootSnapshot.%s"), *DebugNamePrefix));
	PreSubmitSnapshot.PrewarmKind = ELayoutManifestPrewarmKind::Root;
	PreSubmitSnapshot.bHasWorkerSolvePacket = WorkerSolvePacket.bHasRequestManifest && WorkerSolvePacket.bHasSelectedModePlan;
	PreSubmitSnapshot.WorkerSolvePacket = WorkerSolvePacket;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = FLayoutId(*FString::Printf(TEXT("RootPrewarm.%s"), *DebugNamePrefix));
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Root;
	PrewarmInput.bHasFrozenRequestManifest = WorkerSolvePacket.bHasRequestManifest;
	PrewarmInput.FrozenRequestManifest = WorkerSolvePacket.RequestManifest;

	// Pack the pre-submit snapshot and any pre-existing terrain evidence from the manifest.
	PrewarmInput.bHasPreSubmitSnapshot = PreSubmitSnapshot.bHasWorkerSolvePacket;
	if (PrewarmInput.bHasPreSubmitSnapshot)
	{
		PrewarmInput.PreSubmitSnapshot = MoveTemp(PreSubmitSnapshot);
	}
	if (WorkerSolvePacket.RequestManifest.bHasFrozenTerrainBiomeAdapterInput)
	{
		PrewarmInput.bHasFrozenTerrainBiomeAdapterInput = true;
		PrewarmInput.FrozenTerrainBiomeAdapterInput = WorkerSolvePacket.RequestManifest.FrozenTerrainBiomeAdapterInput;
	}

	// When a shared packet is provided, wire up the prewarm completion to update the
	// shared packet's manifest with the finalized structural contract before the solve runs.
	FLayoutManifestPrewarmStageComplete OnPrewarmComplete;
	if (SharedWorkerPacket.IsValid())
	{
		TSharedPtr<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> CapturedSharedPacket = SharedWorkerPacket;
		OnPrewarmComplete = [CapturedSharedPacket](
			const FLayoutBackgroundSolveCompletion& /*Completion*/,
			const FLayoutManifestPrewarmResult& PrewarmResult)
		{
			if (PrewarmResult.bHasDerivedStructuralContract)
			{
				CapturedSharedPacket->RequestManifest = PrewarmResult.FinalizedManifest;
			}
			if (PrewarmResult.bHasPrecomputedAdapterOutput)
			{
				CapturedSharedPacket->bHasPrecomputedAdapterOutput = true;
				CapturedSharedPacket->PrecomputedAdapterOutput = PrewarmResult.PrecomputedAdapterOutput;
			}
		};
	}

	FLayoutLifecycleTerminalFailure OnTerminalFailure =
		[Publish = SolveSubmission.PublishOnGameThread](
			const FLayoutBackgroundSolveCompletion& Completion,
			const FString& FailureReason)
		{
			if (Publish)
			{
				FLayoutBackgroundSolveCompletion FailedCompletion = Completion;
				FailedCompletion.bWorkSucceeded = false;
				FailedCompletion.FailureReason = FailureReason;
				Publish(FailedCompletion);
			}
		};
	FLayoutBackgroundSolveSubmission LifecycleSubmission =
		FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
			&Dispatcher,
			FString::Printf(TEXT("RootPrewarm %s"), *DebugNamePrefix),
			FString::Printf(TEXT("RootPreflight %s"), *DebugNamePrefix),
			LayoutGroupId,
			Priority,
			Priority,
			MoveTemp(PrewarmInput),
			MoveTemp(SolveSubmission),
			MoveTemp(OnPrewarmComplete),
			FLayoutAdmissibilityPreflightStageComplete(),
			MoveTemp(OnTerminalFailure));
	return Dispatcher.Submit(MoveTemp(LifecycleSubmission));
}

	/** Writes bounded worker-safe evidence for failed editor-preview roots and continuations without changing solve behavior. */
	static void WriteFailedPreviewCapture(
		const FLayoutWorkerSolvePacket& WorkerSolvePacket,
		const FLayoutRegionSolveRequest& Request,
		const FLayoutSolveResult& SolveResult,
		const FString& FailureMessage)
	{
		const bool bSupportsFailedCapture =
			WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot
			|| WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::Continuation;
		if (!bSupportsFailedCapture || SolveResult.bSucceeded)
		{
			return;
		}

		auto ToJsonVector = [](const FIntVector& Value)
		{
			TArray<TSharedPtr<FJsonValue>> Result;
			Result.Add(MakeShared<FJsonValueNumber>(Value.X));
			Result.Add(MakeShared<FJsonValueNumber>(Value.Y));
			Result.Add(MakeShared<FJsonValueNumber>(Value.Z));
			return Result;
		};
		auto SanitizeFileComponent = [](FString Value)
		{
			for (TCHAR& Character : Value)
			{
				if (!FChar::IsAlnum(Character) && Character != TEXT('_') && Character != TEXT('-') && Character != TEXT('.'))
				{
					Character = TEXT('_');
				}
			}
			return Value.IsEmpty() ? TEXT("UnknownProfile") : Value;
		};

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("message"), FailureMessage);
		Root->SetNumberField(TEXT("v"), 12);
		Root->SetStringField(
			TEXT("packet_kind"),
			WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::Continuation
				? TEXT("Continuation")
				: TEXT("ExplicitPreviewRoot"));
		Root->SetStringField(TEXT("profile"), Request.ProfileSnapshot.DebugName.ToString());
		Root->SetStringField(TEXT("root"), Request.RootSolveId.ToString());
		Root->SetArrayField(TEXT("site"), ToJsonVector(WorkerSolvePacket.PrimaryBlockWorldPos));
		Root->SetNumberField(TEXT("seed"), Request.Seed);
		Root->SetStringField(TEXT("mode"), StaticEnum<ELayoutContractEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(Request.SelectedModePlan.EnvironmentMode)));

		TSharedRef<FJsonObject> EffectiveProfile = MakeShared<FJsonObject>();
		EffectiveProfile->SetStringField(TEXT("snapshot_id"), Request.ProfileSnapshot.SnapshotId.ToString());
		EffectiveProfile->SetStringField(
			TEXT("source_path"),
			(!Request.ProfilePath.IsNull()
				? Request.ProfilePath
				: Request.ProfileSnapshot.SourceProfilePath).ToString());
		EffectiveProfile->SetStringField(TEXT("entry_count_mode"), StaticEnum<ELayoutCountConstraintMode>()->GetNameStringByValue(static_cast<int64>(Request.ProfileSnapshot.EntryCountMode)));
		EffectiveProfile->SetNumberField(TEXT("entry_count"), Request.ProfileSnapshot.EntryCount);
		EffectiveProfile->SetNumberField(TEXT("min_entry_count"), Request.ProfileSnapshot.MinEntryCount);
		EffectiveProfile->SetNumberField(TEXT("max_entry_count"), Request.ProfileSnapshot.MaxEntryCount);
		EffectiveProfile->SetStringField(TEXT("vertical_access_count_mode"), StaticEnum<ELayoutCountConstraintMode>()->GetNameStringByValue(static_cast<int64>(Request.ProfileSnapshot.VerticalAccessCountMode)));
		EffectiveProfile->SetNumberField(TEXT("vertical_access_count"), Request.ProfileSnapshot.VerticalAccessCount);
		EffectiveProfile->SetNumberField(TEXT("min_vertical_access_count"), Request.ProfileSnapshot.MinVerticalAccessCount);
		EffectiveProfile->SetNumberField(TEXT("max_vertical_access_count"), Request.ProfileSnapshot.MaxVerticalAccessCount);
		EffectiveProfile->SetBoolField(TEXT("require_all_traversal_channels_reachable"), Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable);
		EffectiveProfile->SetBoolField(TEXT("supports_stepped_terrain_solve"), Request.ProfileSnapshot.bSupportsSteppedTerrainSolve);
		Root->SetObjectField(TEXT("effective_profile"), EffectiveProfile);

		TSharedRef<FJsonObject> SelectedMode = MakeShared<FJsonObject>();
		SelectedMode->SetBoolField(TEXT("has_selected_mode_plan"), Request.bHasSelectedModePlan);
		SelectedMode->SetStringField(TEXT("mode_plan_id"), Request.SelectedModePlan.ModePlanId.ToString());
		SelectedMode->SetStringField(TEXT("environment"), StaticEnum<ELayoutContractEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(Request.SelectedModePlan.EnvironmentMode)));
		SelectedMode->SetStringField(TEXT("scope"), StaticEnum<ELayoutContractRegionScope>()->GetNameStringByValue(static_cast<int64>(Request.SelectedModePlan.Scope)));
		SelectedMode->SetStringField(TEXT("placement_kind"), StaticEnum<ELayoutWorldBindingPlacementKind>()->GetNameStringByValue(static_cast<int64>(Request.SelectedModePlan.PlacementKind)));
		SelectedMode->SetBoolField(TEXT("uses_stepped_topology"), Request.SelectedModePlan.bUsesSteppedTerrainTopology);
		SelectedMode->SetBoolField(TEXT("has_finalized_stepped_intents"), Request.bHasFinalizedSteppedTerrainIntents);
		SelectedMode->SetBoolField(TEXT("uses_child_local_flat_fallback"), Request.bUseChildLocalFlatFallback);
		Root->SetObjectField(TEXT("selected_mode"), SelectedMode);

		TSharedRef<FJsonObject> Identity = MakeShared<FJsonObject>();
		Identity->SetStringField(TEXT("effective_snapshot_id"), Request.EffectiveSnapshotId.ToString());
		Identity->SetStringField(TEXT("content_set_snapshot_id"), Request.ContentSetSnapshot.SnapshotId.ToString());
		Identity->SetStringField(TEXT("module_catalog_id"), Request.ModuleCatalog.SnapshotId.ToString());
		Identity->SetStringField(TEXT("world_binding_id"), Request.WorldBindingId.ToString());
		Identity->SetStringField(TEXT("root_candidate_id"), Request.RootCandidateId.ToString());
		Identity->SetStringField(TEXT("root_placement_policy_id"), Request.RootPlacementPolicyId.ToString());
		Identity->SetNumberField(TEXT("snapshot_schema_version"), Request.SnapshotSchemaVersion);
		Identity->SetStringField(TEXT("packet_debug_name"), WorkerSolvePacket.DebugName);
		Identity->SetNumberField(TEXT("packet_attempt_index"), WorkerSolvePacket.AttemptIndex);
		Identity->SetBoolField(TEXT("packet_has_request_manifest"), WorkerSolvePacket.bHasRequestManifest);
		Root->SetObjectField(TEXT("identity"), Identity);

		TArray<TSharedPtr<FJsonValue>> QualifiedEntryCells;
		for (const FIntVector& Cell : Request.QualifiedEntryCells)
		{
			QualifiedEntryCells.Add(MakeShared<FJsonValueArray>(ToJsonVector(Cell)));
		}
		Root->SetBoolField(TEXT("has_qualified_entry_cells"), Request.bHasQualifiedEntryCells);
		Root->SetArrayField(TEXT("qualified_entry_cells"), QualifiedEntryCells);
		// Compare input authority with result plan/origins without reconstructing another solve.
		TArray<TSharedPtr<FJsonValue>> RequestedEntries;
		int32 RequestedEntryCount = 0;
		const auto& InputPlan = Request.PrecomputedPlannedCells.IsEmpty() ? Request.PlannedCells : Request.PrecomputedPlannedCells;
		for (const FLayoutPlannedCell& Cell : InputPlan)
		{
			if (Cell.Intent != ELayoutCellIntent::Entry) continue;
			++RequestedEntryCount;
			if (RequestedEntries.Num() >= 32) continue;
			TSharedRef<FJsonObject> Entry = MakeShared<FJsonObject>();
			Entry->SetArrayField(TEXT("cell"), ToJsonVector(Cell.Cell));
			Entry->SetStringField(TEXT("origin"), StaticEnum<ELayoutEntryOrigin>()->GetNameStringByValue(static_cast<int64>(Cell.EntryOrigin)));
			RequestedEntries.Add(MakeShared<FJsonValueObject>(Entry));
		}
		Root->SetNumberField(TEXT("request_selected_entry_count"), RequestedEntryCount);
		Root->SetArrayField(TEXT("request_selected_entries"), RequestedEntries);

		TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
		Stats->SetNumberField(TEXT("runs"), SolveResult.PropagationStats.PropagationRunCount);
		Stats->SetNumberField(TEXT("arcs"), SolveResult.PropagationStats.ArcQueuePopCount);
		Stats->SetNumberField(TEXT("checks"), SolveResult.PropagationStats.SupportCheckCount);
		Stats->SetNumberField(TEXT("backtracks"), SolveResult.PropagationStats.BacktrackCount);
		Stats->SetNumberField(TEXT("candidate_attempts"), SolveResult.PropagationStats.CandidateAttemptCount);
		Stats->SetNumberField(TEXT("host_group_count"), Request.VerticalAccessHostGroups.Num());
		int32 HostOptionCount = 0;
		for (const FLayoutVerticalAccessHostGroup& HostGroup : Request.VerticalAccessHostGroups)
		{
			HostOptionCount += HostGroup.Options.Num();
		}
		Stats->SetNumberField(TEXT("host_option_count"), HostOptionCount);
		Root->SetObjectField(TEXT("stats"), Stats);

		TArray<TSharedPtr<FJsonValue>> HostGroups;
		for (const FLayoutVerticalAccessHostGroup& HostGroup : Request.VerticalAccessHostGroups)
		{
			TSharedRef<FJsonObject> Group = MakeShared<FJsonObject>();
			Group->SetStringField(TEXT("group_id"), HostGroup.GroupId.ToString());
			Group->SetArrayField(TEXT("deck_cell"), ToJsonVector(HostGroup.DeckCell));
			Group->SetNumberField(TEXT("option_count"), HostGroup.Options.Num());
			Group->SetBoolField(TEXT("supplemental"), HostGroup.bIsSupplemental);
			Group->SetBoolField(TEXT("allow_omission"), HostGroup.bAllowOmission);
			Group->SetBoolField(TEXT("prefer_omission"), HostGroup.bPreferOmission);
			Group->SetStringField(TEXT("first_rejected_admission"), HostGroup.FirstRejectedAdmission);
			TArray<TSharedPtr<FJsonValue>> Options;
			const int32 CapturedOptionCount = FMath::Min(HostGroup.Options.Num(), 32);
			for (int32 OptionIndex = 0; OptionIndex < CapturedOptionCount; ++OptionIndex)
			{
				const FLayoutVerticalAccessHostOption& Option = HostGroup.Options[OptionIndex];
				TSharedRef<FJsonObject> OptionRecord = MakeShared<FJsonObject>();
				OptionRecord->SetArrayField(TEXT("lower_cell"), ToJsonVector(Option.LowerCell));
				OptionRecord->SetArrayField(TEXT("upper_cell"), ToJsonVector(Option.UpperCell));
				OptionRecord->SetStringField(TEXT("tier"), StaticEnum<ELayoutVerticalAccessHostTier>()->GetNameStringByValue(static_cast<int64>(Option.Tier)));
				OptionRecord->SetNumberField(TEXT("seed_rank"), Option.SeedRank);
				OptionRecord->SetBoolField(TEXT("exact_witness"), Option.bHasExactCandidateWitness);
				OptionRecord->SetStringField(TEXT("lower_module"), Option.LowerModuleSnapshotId.ToString());
				OptionRecord->SetNumberField(TEXT("lower_yaw"), Option.LowerYawRotationSteps);
				OptionRecord->SetStringField(TEXT("upper_module"), Option.UpperModuleSnapshotId.ToString());
				OptionRecord->SetNumberField(TEXT("upper_yaw"), Option.UpperYawRotationSteps);
				OptionRecord->SetArrayField(TEXT("upper_local_cell"), ToJsonVector(Option.UpperCandidateLocalCell));
				OptionRecord->SetNumberField(TEXT("lower_face_mask"), Option.LowerTraversalPortFaceMask);
				OptionRecord->SetNumberField(TEXT("upper_face_mask"), Option.UpperTraversalPortFaceMask);
				TArray<TSharedPtr<FJsonValue>> Supports;
				for (int32 SupportIndex = 0; SupportIndex < FMath::Min(32, Option.FilledSupportCandidates.Num()); ++SupportIndex)
				{
					const auto& Support = Option.FilledSupportCandidates[SupportIndex];
					TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
					Record->SetArrayField(TEXT("cell"), ToJsonVector(Support.Cell));
					Record->SetArrayField(TEXT("root_cell"), ToJsonVector(Support.RootCell));
					Record->SetStringField(TEXT("module"), Support.ModuleSnapshotId.ToString());
					Record->SetNumberField(TEXT("yaw"), Support.YawRotationSteps);
					Supports.Add(MakeShared<FJsonValueObject>(Record));
				}
				OptionRecord->SetNumberField(TEXT("support_candidate_count"), Option.FilledSupportCandidates.Num());
				OptionRecord->SetArrayField(TEXT("support_candidates"), Supports);
				Options.Add(MakeShared<FJsonValueObject>(OptionRecord));
			}
			// Host domains can be large; preserve total count while bounding debug-file size.
			Group->SetNumberField(TEXT("captured_option_count"), CapturedOptionCount);
			Group->SetArrayField(TEXT("options"), Options);
			HostGroups.Add(MakeShared<FJsonValueObject>(Group));
		}
		Root->SetArrayField(TEXT("host_groups"), HostGroups);
		Root->SetStringField(
			TEXT("preparation_failure_kind"),
			StaticEnum<ELayoutSolvePreparationFailureKind>()->GetNameStringByValue(
				static_cast<int64>(SolveResult.PreparationFailureKind)));
		Root->SetStringField(
			TEXT("solve_authority"),
			SolveResult.Placements.IsEmpty()
				? TEXT("RejectedEmpty")
				: TEXT("RejectedPartial"));
		Root->SetBoolField(TEXT("partial_preview_authoritative"), false);
		Root->SetStringField(
			TEXT("rejected_preview_stage"),
			StaticEnum<ELayoutRejectedPreviewStage>()->GetNameStringByValue(
				static_cast<int64>(SolveResult.RejectedPreviewStage)));
		Root->SetStringField(
			TEXT("candidate_domain_certificate_id"),
			SolveResult.CandidateDomainCertificateId.ToString());
		TArray<TSharedPtr<FJsonValue>> CandidateDomainRestrictionIds;
		for (const FLayoutId& RestrictionId : SolveResult.CandidateDomainRestrictionIds)
		{
			CandidateDomainRestrictionIds.Add(
				MakeShared<FJsonValueString>(RestrictionId.ToString()));
		}
		Root->SetArrayField(
			TEXT("candidate_domain_restriction_ids"),
			CandidateDomainRestrictionIds);
		if (SolveResult.RegionalFailure.IsSet())
		{
			const FLayoutRegionalFailureRecord& RegionalFailure =
				SolveResult.RegionalFailure;
			TSharedRef<FJsonObject> FailureRecord = MakeShared<FJsonObject>();
			FailureRecord->SetStringField(
				TEXT("scope"),
				StaticEnum<ELayoutRegionalFailureScope>()->GetNameStringByValue(
					static_cast<int64>(RegionalFailure.Scope)));
			FailureRecord->SetStringField(TEXT("phase"), RegionalFailure.Phase.ToString());
			FailureRecord->SetStringField(TEXT("parent_region"), RegionalFailure.ParentRegionDebugPath);
			FailureRecord->SetStringField(TEXT("region"), RegionalFailure.RegionDebugPath);
			FailureRecord->SetStringField(TEXT("source_entry_id"), RegionalFailure.SourceContentEntryId.ToString());
			FailureRecord->SetStringField(TEXT("branch_id"), RegionalFailure.BranchId.ToString());
			FailureRecord->SetStringField(TEXT("stage_mapping_id"), RegionalFailure.StageMappingId.ToString());
			FailureRecord->SetStringField(TEXT("certificate_id"), RegionalFailure.CertificateId.ToString());
			FailureRecord->SetStringField(
				TEXT("preparation_failure_kind"),
				StaticEnum<ELayoutSolvePreparationFailureKind>()->GetNameStringByValue(
					static_cast<int64>(RegionalFailure.PreparationFailureKind)));
			FailureRecord->SetNumberField(TEXT("candidate_attempts"), RegionalFailure.CandidateAttemptCount);
			FailureRecord->SetNumberField(TEXT("variant_index"), RegionalFailure.VariantIndex);
			FailureRecord->SetStringField(TEXT("first_cause"), RegionalFailure.FirstCause);
			FailureRecord->SetStringField(TEXT("downstream_cause"), RegionalFailure.DownstreamCause);
			Root->SetObjectField(TEXT("regional_failure"), FailureRecord);
		}

		auto TagsToJson = [](const FGameplayTagContainer& Tags)
		{
			TArray<FGameplayTag> SortedTags;
			Tags.GetGameplayTagArray(SortedTags);
			SortedTags.Sort([](const FGameplayTag& Left, const FGameplayTag& Right)
			{
				return Left.ToString() < Right.ToString();
			});
			TArray<TSharedPtr<FJsonValue>> JsonTags;
			for (const FGameplayTag& Tag : SortedTags)
			{
				JsonTags.Add(MakeShared<FJsonValueString>(Tag.ToString()));
			}
			return JsonTags;
		};
		auto ProviderCommitmentToJson = [&ToJsonVector](
			const FLayoutZoneFeatureProviderCommitment& Commitment)
		{
			TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
			Record->SetStringField(TEXT("provider_id"), Commitment.ProviderCommitmentId.ToString());
			Record->SetStringField(TEXT("requirement_id"), Commitment.RequirementId.ToString());
			Record->SetStringField(TEXT("entry_id"), Commitment.SourceContentEntryId.ToString());
			Record->SetStringField(TEXT("region"), Commitment.SourceRegionDebugPath);
			Record->SetArrayField(TEXT("cell"), ToJsonVector(Commitment.Cell));
			Record->SetNumberField(TEXT("module_level_index"), Commitment.ModuleLevelIndex);
			Record->SetNumberField(TEXT("terrain_stage_index"), Commitment.TerrainStageIndex);
			return MakeShared<FJsonValueObject>(Record);
		};
		TArray<LayoutZoneFeatureDemand::FHardDemand> FeatureDemands;
		LayoutZoneFeatureDemand::CompileHardDemands(
			Request.ProfileSnapshot.ZoneFeatureRequirements,
			FeatureDemands);
		TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary> ProviderChoices;
		LayoutZoneFeatureDemand::CompileProviderChoiceSummaries(
			Request.EffectiveSnapshotId,
			FeatureDemands,
			Request.ContentSetSnapshot.Entries,
			ProviderChoices);
		TArray<TSharedPtr<FJsonValue>> FeatureDemandRecords;
		for (const LayoutZoneFeatureDemand::FHardDemand& Demand : FeatureDemands)
		{
			TSharedRef<FJsonObject> DemandRecord = MakeShared<FJsonObject>();
			DemandRecord->SetStringField(TEXT("requirement_id"), Demand.RequirementId.ToString());
			DemandRecord->SetStringField(
				TEXT("zone"),
				StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
					static_cast<int64>(Demand.Zone)));
			DemandRecord->SetArrayField(TEXT("required_features"), TagsToJson(Demand.RequiredFeatures));
			DemandRecord->SetStringField(
				TEXT("match_mode"),
				StaticEnum<ELayoutZoneFeatureMatchMode>()->GetNameStringByValue(
					static_cast<int64>(Demand.MatchMode)));
			DemandRecord->SetNumberField(TEXT("min_count"), Demand.MinCount);
			DemandRecord->SetNumberField(TEXT("max_count"), Demand.MaxCount);

			TArray<TSharedPtr<FJsonValue>> PotentialSources;
			for (const LayoutZoneFeatureDemand::FProviderChoiceSummary& Choice : ProviderChoices)
			{
				if (Choice.RequirementId != Demand.RequirementId)
				{
					continue;
				}
				TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
				Source->SetStringField(TEXT("entry_id"), Choice.SourceContentEntryId.ToString());
				Source->SetStringField(
					TEXT("kind"),
					StaticEnum<ELayoutRegionContentKind>()->GetNameStringByValue(
						static_cast<int64>(Choice.ContentKind)));
				Source->SetStringField(
					TEXT("placement_zone"),
					StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
						static_cast<int64>(Choice.PlacementZone)));
				PotentialSources.Add(MakeShared<FJsonValueObject>(Source));
			}
			DemandRecord->SetArrayField(TEXT("potential_sources"), PotentialSources);

			TArray<TSharedPtr<FJsonValue>> ExcludedSources;
			for (const FLayoutRegionContentEntrySolveSnapshot& Entry : Request.ContentSetSnapshot.Entries)
			{
				FString ExclusionReason;
				if (!LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchDemand(
						Entry.ProvidedZoneFeatures,
						Demand))
				{
					ExclusionReason = TEXT("MissingRequiredFeatures");
				}
				else
				{
					const ELayoutPlacementZone ProviderZone =
						Entry.ContentKind == ELayoutRegionContentKind::ChildRegion
							? Entry.ChildPlacementZone
							: Entry.ModulePlacementZone;
					if (!LayoutZoneFeatureDemand::DoPotentialPlacementZonesOverlap(
							ProviderZone,
							Demand.Zone))
					{
						ExclusionReason = TEXT("PlacementZoneMismatch");
					}
				}
				if (ExclusionReason.IsEmpty())
				{
					continue;
				}
				TSharedRef<FJsonObject> Excluded = MakeShared<FJsonObject>();
				Excluded->SetStringField(TEXT("entry_id"), Entry.EntryId.ToString());
				Excluded->SetStringField(TEXT("reason"), ExclusionReason);
				const ELayoutPlacementZone ExcludedProviderZone =
					Entry.ContentKind == ELayoutRegionContentKind::ChildRegion
						? Entry.ChildPlacementZone
						: Entry.ModulePlacementZone;
				Excluded->SetStringField(
					TEXT("provided_zone"),
					StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(
						static_cast<int64>(ExcludedProviderZone)));
				Excluded->SetArrayField(TEXT("provided_features"), TagsToJson(Entry.ProvidedZoneFeatures));
				ExcludedSources.Add(MakeShared<FJsonValueObject>(Excluded));
			}
			DemandRecord->SetArrayField(TEXT("excluded_sources"), ExcludedSources);

			TArray<TSharedPtr<FJsonValue>> RequestCommitments;
			for (const FLayoutZoneFeatureProviderCommitment& Commitment : Request.PrecommittedZoneFeatureProviderCommitments)
			{
				if (Commitment.RequirementId != Demand.RequirementId)
				{
					continue;
				}
				RequestCommitments.Add(ProviderCommitmentToJson(Commitment));
			}
			DemandRecord->SetArrayField(TEXT("request_precommitted_providers"), RequestCommitments);

			TArray<TSharedPtr<FJsonValue>> ResultCommitments;
			for (const FLayoutZoneFeatureProviderCommitment& Commitment : SolveResult.ZoneFeatureProviderCommitments)
			{
				if (Commitment.RequirementId == Demand.RequirementId)
				{
					ResultCommitments.Add(ProviderCommitmentToJson(Commitment));
				}
			}
			DemandRecord->SetArrayField(TEXT("result_committed_providers"), ResultCommitments);
			FeatureDemandRecords.Add(MakeShared<FJsonValueObject>(DemandRecord));
		}
		Root->SetArrayField(TEXT("zone_feature_demands"), FeatureDemandRecords);

		TArray<TSharedPtr<FJsonValue>> SelectedChildren;
		TSet<FLayoutId> SelectedChildProviderIds;
		auto AppendSelectedChildren = [
			&SelectedChildren,
			&SelectedChildProviderIds,
			&ProviderCommitmentToJson,
			&ToJsonVector](
			const TArray<FLayoutZoneFeatureProviderCommitment>& Commitments)
		{
			for (const FLayoutZoneFeatureProviderCommitment& Commitment : Commitments)
			{
				if (!Commitment.ModuleSnapshotId.IsNone()
					|| Commitment.ProviderCommitmentId.IsNone()
					|| SelectedChildProviderIds.Contains(Commitment.ProviderCommitmentId))
				{
					continue;
				}
				SelectedChildProviderIds.Add(Commitment.ProviderCommitmentId);
				TSharedRef<FJsonObject> Child = MakeShared<FJsonObject>();
				Child->SetStringField(TEXT("provider_id"), Commitment.ProviderCommitmentId.ToString());
				Child->SetStringField(TEXT("requirement_id"), Commitment.RequirementId.ToString());
				Child->SetStringField(TEXT("source_entry_id"), Commitment.SourceContentEntryId.ToString());
				Child->SetStringField(TEXT("region"), Commitment.SourceRegionDebugPath);
				Child->SetArrayField(TEXT("region_offset"), ToJsonVector(Commitment.Cell));
				Child->SetNumberField(TEXT("module_level_index"), Commitment.ModuleLevelIndex);
				Child->SetNumberField(TEXT("terrain_stage_index"), Commitment.TerrainStageIndex);
				SelectedChildren.Add(MakeShared<FJsonValueObject>(Child));
			}
		};
		AppendSelectedChildren(Request.PrecommittedZoneFeatureProviderCommitments);
		AppendSelectedChildren(SolveResult.ZoneFeatureProviderCommitments);
		Root->SetArrayField(TEXT("selected_children"), SelectedChildren);

		TArray<TSharedPtr<FJsonValue>> SeamRecords;
		for (const FLayoutPartitionSeamRecord& Seam : SolveResult.PartitionSeams)
		{
			TSharedRef<FJsonObject> SeamRecord = MakeShared<FJsonObject>();
			SeamRecord->SetStringField(TEXT("seam_id"), Seam.SeamId.ToString());
			SeamRecord->SetStringField(TEXT("owner"), Seam.OwnerRegionDebugPath);
			SeamRecord->SetStringField(TEXT("passive"), Seam.PassiveRegionDebugPath);
			SeamRecord->SetStringField(TEXT("interface_family"), Seam.InterfaceFamily.ToString());
			SeamRecord->SetNumberField(TEXT("segment_count"), Seam.SegmentCount);
			SeamRecords.Add(MakeShared<FJsonValueObject>(SeamRecord));
		}
		Root->SetArrayField(TEXT("partition_seams"), SeamRecords);

		TArray<TSharedPtr<FJsonValue>> Trace;
		for (const FLayoutValidationMessage& ValidationMessage : SolveResult.Messages)
		{
			if (ValidationMessage.Message.StartsWith(TEXT("Solver trace")))
			{
				Trace.Add(MakeShared<FJsonValueString>(ValidationMessage.Message));
			}
		}
		if (!Trace.IsEmpty())
		{
			Root->SetArrayField(TEXT("trace"), MoveTemp(Trace));
		}

		TArray<TSharedPtr<FJsonValue>> ValidationMessages;
		for (const FLayoutValidationMessage& ValidationMessage : SolveResult.Messages)
		{
			if (ValidationMessage.Message.StartsWith(TEXT("Solver trace")))
			{
				continue;
			}
			TSharedRef<FJsonObject> Record = MakeShared<FJsonObject>();
			Record->SetStringField(TEXT("severity"), StaticEnum<ELayoutValidationSeverity>()->GetNameStringByValue(static_cast<int64>(ValidationMessage.Severity)));
			Record->SetStringField(TEXT("message"), ValidationMessage.Message);
			ValidationMessages.Add(MakeShared<FJsonValueObject>(Record));
		}
		Root->SetArrayField(TEXT("validation_messages"), ValidationMessages);

		TArray<TSharedPtr<FJsonValue>> Plan;
		for (const FLayoutPlannedCell& Cell : SolveResult.PlannedCells)
		{
			TArray<TSharedPtr<FJsonValue>> Record = ToJsonVector(Cell.Cell);
			Record.Add(MakeShared<FJsonValueString>(StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(Cell.Intent))));
			Record.Add(MakeShared<FJsonValueNumber>(Cell.ModuleLevelIndex));
			Record.Add(MakeShared<FJsonValueBoolean>(Cell.bIsBridgeCell));
			Record.Add(MakeShared<FJsonValueString>(StaticEnum<ELayoutPlacementZone>()->GetNameStringByValue(static_cast<int64>(Cell.PlacementZone))));
			Record.Add(MakeShared<FJsonValueNumber>(Cell.TerrainSeamFaceMask));
			Record.Add(MakeShared<FJsonValueString>(StaticEnum<ELayoutEntryOrigin>()->GetNameStringByValue(static_cast<int64>(Cell.EntryOrigin))));
			Record.Add(MakeShared<FJsonValueNumber>(Cell.VerticalAccessLandingContactMask));
			Record.Add(MakeShared<FJsonValueBoolean>(Cell.bIsTopBridgeOffer));
			Plan.Add(MakeShared<FJsonValueArray>(MoveTemp(Record)));
		}
		Root->SetArrayField(TEXT("plan"), Plan);

		TArray<TSharedPtr<FJsonValue>> Placements;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			TSharedRef<FJsonObject> PlacementRecord = MakeShared<FJsonObject>();
			PlacementRecord->SetArrayField(TEXT("cell"), ToJsonVector(Placement.Cell));
			PlacementRecord->SetStringField(TEXT("intent"), StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(Placement.Intent)));
			PlacementRecord->SetStringField(TEXT("module"), Placement.ModuleSnapshotId.ToString());
			PlacementRecord->SetNumberField(TEXT("yaw"), Placement.YawRotationSteps);
			TArray<TSharedPtr<FJsonValue>> OccupiedCells;
			for (const FIntVector& OccupiedLocalCell : Placement.OccupiedLocalCells)
			{
				OccupiedCells.Add(MakeShared<FJsonValueArray>(ToJsonVector(OccupiedLocalCell)));
			}
			PlacementRecord->SetArrayField(TEXT("occupied"), OccupiedCells);
			Placements.Add(MakeShared<FJsonValueObject>(PlacementRecord));
		}
		Root->SetArrayField(TEXT("placements"), Placements);

		FIntVector FirstDomainCell = FIntVector::ZeroValue;
		bool bHasFirstDomainCell = false;
		FRegexMatcher CellMatcher(FRegexPattern(TEXT("cell X=(-?[0-9]+) Y=(-?[0-9]+) Z=(-?[0-9]+)")), FailureMessage);
		while (CellMatcher.FindNext())
		{
			FirstDomainCell = FIntVector(
				FCString::Atoi(*CellMatcher.GetCaptureGroup(1)),
				FCString::Atoi(*CellMatcher.GetCaptureGroup(2)),
				FCString::Atoi(*CellMatcher.GetCaptureGroup(3)));
			bHasFirstDomainCell = true;
		}
		if (bHasFirstDomainCell)
		{
			TSharedRef<FJsonObject> First = MakeShared<FJsonObject>();
			First->SetArrayField(TEXT("cell"), ToJsonVector(FirstDomainCell));
			Root->SetObjectField(TEXT("first"), First);

			const FLayoutIndexedDomainSnapshot Domains = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
			if (Domains.bSucceeded)
			{
				TArray<TSharedPtr<FJsonValue>> Focus;
				for (const FLayoutIndexedCellDomain& Domain : Domains.CellDomains)
				{
					if (FMath::Abs(Domain.Cell.X - FirstDomainCell.X) + FMath::Abs(Domain.Cell.Y - FirstDomainCell.Y) + FMath::Abs(Domain.Cell.Z - FirstDomainCell.Z) > 1)
					{
						continue;
					}
					TSharedRef<FJsonObject> Cell = MakeShared<FJsonObject>();
					Cell->SetArrayField(TEXT("cell"), ToJsonVector(Domain.Cell));
					Cell->SetStringField(TEXT("intent"), StaticEnum<ELayoutCellIntent>()->GetNameStringByValue(static_cast<int64>(Domain.Intent)));
					TArray<TSharedPtr<FJsonValue>> Candidates;
					for (const int32 CandidateIndex : Domain.OrderedCandidateIndices)
					{
						if (!Domains.Candidates.IsValidIndex(CandidateIndex)) continue;
						const FLayoutIndexedDomainCandidate& Candidate = Domains.Candidates[CandidateIndex];
						TArray<TSharedPtr<FJsonValue>> CandidateRecord;
						CandidateRecord.Add(MakeShared<FJsonValueString>(Candidate.ModuleSnapshotId.ToString()));
						CandidateRecord.Add(MakeShared<FJsonValueNumber>(Candidate.YawRotationSteps));
						Candidates.Add(MakeShared<FJsonValueArray>(MoveTemp(CandidateRecord)));
					}
					Cell->SetArrayField(TEXT("domain"), Candidates);
					Focus.Add(MakeShared<FJsonValueObject>(Cell));
				}
				Root->SetArrayField(TEXT("focus"), Focus);
			}
		}

		const FString ProfileSlug = SanitizeFileComponent(Request.ProfileSnapshot.DebugName.ToString());
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("FailedLayoutSolve") / ProfileSlug;
		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		PlatformFile.CreateDirectoryTree(*Directory);
		const FString Stem = FString::Printf(TEXT("X%d_Y%d_Z%d-S%d"), WorkerSolvePacket.PrimaryBlockWorldPos.X, WorkerSolvePacket.PrimaryBlockWorldPos.Y, WorkerSolvePacket.PrimaryBlockWorldPos.Z, Request.Seed);
		int32 Attempt = 1;
		FString Destination;
		do
		{
			Destination = Directory / FString::Printf(TEXT("%s-A%03d.json"), *Stem, Attempt++);
		}
		while (PlatformFile.FileExists(*Destination));

		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		if (FJsonSerializer::Serialize(Root, Writer) && FFileHelper::SaveStringToFile(Json, *(Destination + TEXT(".tmp"))))
		{
			PlatformFile.MoveFile(*Destination, *(Destination + TEXT(".tmp")));
		}
	}

	static bool RunSharedRootSolveWork(
		const FLayoutWorkerSolvePacket& WorkerSolvePacket,
		const FLayoutId SolvedArtifactId,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FLayoutSolvedArtifact& OutSolvedArtifact,
		FString& OutFailureReason,
		FLayoutRegionSolveRequest* OutFinalizedRequest = nullptr)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_RootWorker, STAT_PorismLayout_RootWorker);
		FLayoutRegionSolveRequest FinalizedWorkerRequest;
		{
			PORISM_LAYOUT_PROFILE_SCOPE(Layout_RootWorker_RequestFinalize, STAT_PorismLayout_RequestFinalize);
			if (!LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(
					WorkerSolvePacket,
					FinalizedWorkerRequest,
					OutFailureReason))
			{
				UE_LOG(LogTemp, Warning, TEXT("[RunSharedRootSolveWork] FinalizeRequestFromPacket FAILED: %s"), *OutFailureReason);
				return false;
			}
		}

		if (WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot
			|| WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::Continuation)
		{
			// Continuation failure captures need the same bounded candidate trace as root previews.
			FinalizedWorkerRequest.ExecutionSettings.TraceMode = ELayoutSolverTraceMode::OnFailure;
			FinalizedWorkerRequest.ExecutionSettings.MaxTraceEvents = 8192;
			FinalizedWorkerRequest.ExecutionSettings.bIncludeTraceCandidateDetails = true;
		}

		// Every lifecycle path must carry its one frozen adapter contract into CSP.
		if (FinalizedWorkerRequest.PrecomputedFrozenTerrainContract.ContractId.IsNone())
		{
			OutFailureReason = TEXT("Solve submission is missing its precomputed adapter contract.");
			UE_LOG(LogTemp, Warning, TEXT("[RunSharedRootSolveWork] %s"), *OutFailureReason);
			return false;
		}

		// One worker attempt owns both stepped solve and any flat recovery. Nested
		// regional solves may borrow the ledger but cannot reset its work/deadline.
		LayoutSolveExecution::FScope ExecutionScope(FinalizedWorkerRequest.ExecutionSettings.MaxSolveDurationSeconds,
			FinalizedWorkerRequest.ExecutionSettings.MaxCandidateAttempts);
		const bool bAllowsFlatRecovery =
			WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot
			&& FinalizedWorkerRequest.bHasSelectedModePlan
			&& FinalizedWorkerRequest.SelectedModePlan.bUsesSteppedTerrainTopology
			&& FinalizedWorkerRequest.WorldBindingPlacementPolicy.TerrainTransition
				.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible;
		{
			// Prefer stepped placement without consuming the allowance needed to try
			// flat recovery. Scope exit restores ceilings, never spent work or time.
			TOptional<LayoutSolveExecution::FOptionalImprovementScope> SteppedAllowance;
			if (bAllowsFlatRecovery && !FinalizedWorkerRequest.bUseChildLocalFlatFallback)
				SteppedAllowance.Emplace(FLayoutContractPipeline::CanTryChildLocalFlatFallback(FinalizedWorkerRequest) ? 4 : 2);
			OutScheduleResult = FLayoutProfileSolver::SolveRegionTree(FinalizedWorkerRequest);
		}
		if (!OutScheduleResult.MergedSolveResult.bSucceeded && bAllowsFlatRecovery
			&& !LayoutSolveExecution::ShouldStop())
		{
			FLayoutRegionSolveRequest ChildFlatRequest;
			if (FLayoutContractPipeline::TryBuildChildLocalFlatFallbackRequest(
				FinalizedWorkerRequest, ChildFlatRequest))
			{
				const FString OriginalFailure = !OutScheduleResult.FailureReason.IsEmpty()
					? OutScheduleResult.FailureReason : OutScheduleResult.MergedSolveResult.FailureReason;
				// Spend the retained allowance on keeping the parent stepped. Whole-root
				// recovery remains possible after an early rejection, never after hard expiry.
				OutScheduleResult = FLayoutProfileSolver::SolveRegionTree(ChildFlatRequest);
				if (OutScheduleResult.MergedSolveResult.bSucceeded)
				{
					ChildFlatRequest.PrecomputedAdapterDiagnostics.AddDefaulted_GetRef().Detail =
						FString::Printf(TEXT("Child-local flat recovery retains stepped parent terrain. Original rejection: %s"),
							*OriginalFailure);
				}
				else
				{
					const FString ChildFailure = !OutScheduleResult.FailureReason.IsEmpty()
						? OutScheduleResult.FailureReason : OutScheduleResult.MergedSolveResult.FailureReason;
					OutScheduleResult.FailureReason = FString::Printf(
						TEXT("%s Child-local flat recovery failed: %s"), *OriginalFailure, *ChildFailure);
					OutScheduleResult.MergedSolveResult.FailureReason = OutScheduleResult.FailureReason;
				}
				FinalizedWorkerRequest = MoveTemp(ChildFlatRequest);
			}
		}
		// Child boundary/CSP rejection need not carry a terrain preparation enum.
		// An exhausted stepped allowance permits a retry; cancellation or expiration
		// of the owning invocation still blocks it after the local ceiling is removed.
		if (!OutScheduleResult.MergedSolveResult.bSucceeded && bAllowsFlatRecovery
			&& !LayoutSolveExecution::ShouldStop())
		{
			const FString SteppedFailureReason = !OutScheduleResult.FailureReason.IsEmpty()
				? OutScheduleResult.FailureReason
				: OutScheduleResult.MergedSolveResult.FailureReason;
			FLayoutRegionSolveRequest FlatFallbackRequest;
			FString FlatFallbackFailureReason;
			if (FLayoutContractPipeline::TryBuildFlatFallbackRequestAfterSteppedChildPreparationFailure(
					FinalizedWorkerRequest,
					SteppedFailureReason,
					FlatFallbackRequest,
					FlatFallbackFailureReason))
			{
				FLayoutRegionSolveScheduleResult FlatFallbackResult =
					FLayoutProfileSolver::SolveRegionTree(FlatFallbackRequest);
				if (!FlatFallbackResult.MergedSolveResult.bSucceeded)
				{
					const FString FlatSolveFailureReason = !FlatFallbackResult.FailureReason.IsEmpty()
						? FlatFallbackResult.FailureReason
						: FlatFallbackResult.MergedSolveResult.FailureReason;
					FlatFallbackResult.FailureReason = FString::Printf(
						TEXT("%s Flat terrain fallback also failed: %s"),
						*SteppedFailureReason,
						*FlatSolveFailureReason);
					FlatFallbackResult.MergedSolveResult.FailureReason = FlatFallbackResult.FailureReason;
				}
				FinalizedWorkerRequest = MoveTemp(FlatFallbackRequest);
				OutScheduleResult = MoveTemp(FlatFallbackResult);
			}
			else
			{
				OutScheduleResult.FailureReason = FString::Printf(
					TEXT("%s Flat terrain fallback could not rebuild a fresh contract: %s"),
					*SteppedFailureReason,
					*FlatFallbackFailureReason);
				OutScheduleResult.MergedSolveResult.FailureReason = OutScheduleResult.FailureReason;
			}
		}
		// Merged schedule results can rebuild supplied plans; restore frozen removed-cell authority for realization.
		for (const FLayoutCellReservationRecord& FrozenReservation : FinalizedWorkerRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations)
		{
			if (FrozenReservation.ReservationKind != ELayoutCellReservationKind::ReservedEmpty)
			{
				continue;
			}
			FLayoutCellReservationRecord* const ExistingReservation =
				OutScheduleResult.MergedSolveResult.CompiledReservations.FindByPredicate(
					[&FrozenReservation](const FLayoutCellReservationRecord& Existing)
					{
						return Existing.ReservationId == FrozenReservation.ReservationId;
					});
			if (ExistingReservation != nullptr)
			{
				*ExistingReservation = FrozenReservation;
			}
			else
			{
				OutScheduleResult.MergedSolveResult.CompiledReservations.Add(FrozenReservation);
			}
		}
		PublishRequestOwnedSteppedCarriersOnMergedSolveResult(FinalizedWorkerRequest, OutScheduleResult);
		BackfillSolvedArtifactTemplatePathsFromSnapshots(FinalizedWorkerRequest, OutScheduleResult);
		if (!OutScheduleResult.MergedSolveResult.bSucceeded)
		{
			WriteFailedPreviewCapture(
				WorkerSolvePacket,
				FinalizedWorkerRequest,
				OutScheduleResult.MergedSolveResult,
				!OutScheduleResult.FailureReason.IsEmpty()
					? OutScheduleResult.FailureReason
					: OutScheduleResult.MergedSolveResult.FailureReason);
		}
		LayoutBackgroundSolveSnapshot::ScrubWorkerSolveResult(OutScheduleResult.MergedSolveResult);
		if (!LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveResult(OutScheduleResult.MergedSolveResult, OutFailureReason))
		{
			return false;
		}
		const FLayoutSolveResult& SolveResult = OutScheduleResult.MergedSolveResult;
		const bool bHasRetainedPartial = HasRetainedPartialPlacements(SolveResult);
		if (SolveResult.bSucceeded || bHasRetainedPartial)
		{
			PORISM_LAYOUT_PROFILE_SCOPE(Layout_RootWorker_SolvedArtifact, STAT_PorismLayout_SolvedArtifact);
			const bool bBuiltArtifact = bHasRetainedPartial
				? LayoutSolvedArtifact::TryBuildFromPartialScheduleResult(
					SolvedArtifactId,
					FinalizedWorkerRequest.RegionDebugPath,
					OutScheduleResult,
					OutSolvedArtifact,
					OutFailureReason)
				: LayoutSolvedArtifact::TryBuildFromScheduleResult(
					SolvedArtifactId,
					FinalizedWorkerRequest.RegionDebugPath,
					OutScheduleResult,
					OutSolvedArtifact,
					OutFailureReason);
			if (!bBuiltArtifact)
			{
				return false;
			}
		}
		if (SolveResult.bSucceeded || bHasRetainedPartial)
		{
			if (FinalizedWorkerRequest.PrecomputedFrozenTerrainContract.ActiveCells.IsEmpty())
			{
				OutFailureReason = TEXT("Solved artifact requires contract-owned active-cell provenance.");
				return false;
			}
			if (!LayoutSolvedArtifact::TryAttachActiveCellProvenance(
					FinalizedWorkerRequest.PrecomputedFrozenTerrainContract.ActiveCells,
					OutSolvedArtifact,
					OutFailureReason))
			{
				return false;
			}
		}
		OutFailureReason = !OutScheduleResult.FailureReason.IsEmpty()
			? OutScheduleResult.FailureReason
			: SolveResult.FailureReason;
		if (!SolveResult.bSucceeded)
		{
			const FLayoutValidationMessage* FirstError = nullptr;
			for (const FLayoutValidationMessage& Message : SolveResult.Messages)
			{
				if (Message.Severity != ELayoutValidationSeverity::Error)
				{
					continue;
				}

				FirstError = &Message;
				if (!Message.Message.Contains(TEXT("failed asset validation before"))
					&& !Message.Message.Contains(TEXT("failed validation before")))
				{
					break;
				}
			}
			if (FirstError != nullptr)
			{
				OutFailureReason = FString::Printf(
					TEXT("%s First validation error: %s"),
					*OutFailureReason,
					*FirstError->Message);
			}
		}
		if (OutFinalizedRequest != nullptr)
		{
			*OutFinalizedRequest = MoveTemp(FinalizedWorkerRequest);
		}
		return SolveResult.bSucceeded;
	}

UChunkWorldLayoutRuntimeComponent::UChunkWorldLayoutRuntimeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bTickInEditor = true;
	FrozenSubmissionStore = MakeShared<FLayoutFrozenSubmissionStore>();
	PlanningAreaQueue = MakeShared<FLayoutPlanningAreaQueue>(ObservedChunkLayers);
}

UChunkWorldLayoutRuntimeComponent::~UChunkWorldLayoutRuntimeComponent() = default;

void UChunkWorldLayoutRuntimeComponent::CancelBackgroundLayoutSolve(const FLayoutBackgroundSolveHandle& Handle)
{
	check(IsInGameThread());
	if (Handle.IsValid() && BackgroundSolveDispatcher.IsValid())
	{
		BackgroundSolveDispatcher->CancelGroup(Handle.LayoutGroupId);
	}

	for (auto It = PendingPlanningWindowSolveHandlesByRecordKey.CreateIterator(); It; ++It)
	{
		if (It.Value() != Handle)
		{
			continue;
		}

		const FString RecordKey = It.Key();
		RetireAutomaticRoot(RecordKey);
		break;
	}

	const int32 ConnectorHandleIndex = PendingConnectorRefreshSolveHandles.IndexOfByKey(Handle);
	if (ConnectorHandleIndex != INDEX_NONE)
	{
		const uint64 ConnectorKey = Handle.LayoutGroupId;
		const FLayoutId DescriptorId = PendingConnectorFrozenSubmissionDescriptorIdsByKey.FindRef(ConnectorKey);
		TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, DescriptorId);
		ReleaseContinuationReservationForConnector(ConnectorKey);
		PendingExplicitPreparedSegmentsByConnectorKey.Remove(ConnectorKey);
		ExplicitPreviewConnectorKeys.Remove(ConnectorKey);
		ExplicitContinuationCompletionCallbacks.Remove(ConnectorKey);
		PendingConnectorRefreshSolveHandles.RemoveAtSwap(ConnectorHandleIndex, 1, EAllowShrinking::No);
	}
}

void UChunkWorldLayoutRuntimeComponent::TickComponent(
	const float DeltaTime,
	const ELevelTick TickType,
	FActorComponentTickFunction* const ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshDebugGenerationStatsToggle();
	if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor)
	{
		const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
		if (ChunkWorld == nullptr || !ChunkWorld->IsRunning())
		{
			CancelAutomaticRootWork();
			PumpBackgroundLayoutSolves();
			if (GetDebugGenerationStats())
			{
				PublishCachedDebugGenerationStatsOnScreen();
			}
			return;
		}
	}
	ProcessQueuedLayoutWork();

	if (GetDebugGenerationStats())
	{
		PublishCachedDebugGenerationStatsOnScreen();
	}
}

#if WITH_EDITOR
void UChunkWorldLayoutRuntimeComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Super may reconstruct the component. Apply local state changes before forwarding.
	LastPlanningWindowUpdateTimeSeconds = TNumericLimits<double>::Lowest();
	if (!bEnablePlanningWindowRuntimeUpdates)
	{
		CancelAutomaticRootWork();
	}
	const FLayoutId MemberName = PropertyChangedEvent.MemberProperty != nullptr
		? PropertyChangedEvent.MemberProperty->GetFName() : PropertyChangedEvent.GetPropertyName();
	if (MemberName == GET_MEMBER_NAME_CHECKED(UChunkWorldLayoutRuntimeComponent, LayoutWorldBindings))
	{
		InvalidateAutomaticPlanningInputs();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif

#if WITH_AUTOMATION_TESTS
void UChunkWorldLayoutRuntimeComponent::AddResolvedLayoutSiteRecordForTesting(
	const FIntPoint& ReservationKey,
	const FResolvedLayoutSiteRecord& SiteRecord)
{
	if (SiteRecord.RootSolveId.IsNone()) return;
	const FString SiteRecordKey = MakeResolvedSiteRecordKey(SiteRecord);
	ResolvedSiteRecords.Add(SiteRecordKey, SiteRecord);
	PublishResolvedRootContinuationEndpoints(
		SiteRecordKey,
		ReservationKey,
		SiteRecord);
}

void UChunkWorldLayoutRuntimeComponent::AddResolvedConnectorRecordForTesting(
	const uint64 ConnectorKey,
	const FResolvedLayoutConnectorRecord& ConnectorRecord)
{
	ResolvedConnectorRecords.Add(ConnectorKey, ConnectorRecord);
	ResolvedConnectorFrozenTerrainContracts.Remove(ConnectorKey);
}

bool UChunkWorldLayoutRuntimeComponent::TryGetResolvedConnectorFrozenTerrainContractForTesting(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	FLayoutFrozenTerrainContract& OutFrozenTerrainContract) const
{
	const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
	if (const FLayoutFrozenTerrainContract* const FrozenTerrainContract =
			ResolvedConnectorFrozenTerrainContracts.Find(ConnectorKey))
	{
		OutFrozenTerrainContract = *FrozenTerrainContract;
		return true;
	}

	OutFrozenTerrainContract = FLayoutFrozenTerrainContract();
	return false;
}

void UChunkWorldLayoutRuntimeComponent::InjectResolvedConnectorFrozenTerrainContractForTesting(
	const uint64 ConnectorKey,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract)
{
	ResolvedConnectorFrozenTerrainContracts.Add(ConnectorKey, FrozenTerrainContract);
}

void UChunkWorldLayoutRuntimeComponent::AddPlanningRecordKeyForTesting(
	const FIntPoint& ReservationKey,
	const FString& PlanningRecordKey)
{
	for (const TPair<FString, FResolvedLayoutSiteRecord>& Pair : ResolvedSiteRecords)
	{
		if (FLayoutSiteReservation::ComputeReservationKey(Pair.Value.SiteCenterBlockWorldPos) == ReservationKey)
		{
			PlanningRecordKeysBySiteRecordKey.Add(Pair.Key, PlanningRecordKey);
			return;
		}
	}
}

FString UChunkWorldLayoutRuntimeComponent::GetPlanningRecordKeyForTesting(
	const FIntPoint& ReservationKey) const
{
	for (const TPair<FString, FResolvedLayoutSiteRecord>& Pair : ResolvedSiteRecords)
	{
		if (FLayoutSiteReservation::ComputeReservationKey(Pair.Value.SiteCenterBlockWorldPos) == ReservationKey)
		{
			return PlanningRecordKeysBySiteRecordKey.FindRef(Pair.Key);
		}
	}
	return FString();
}

void UChunkWorldLayoutRuntimeComponent::RefreshConnectorRecordsForTesting()
{
	RefreshConnectorRecords(true);
	if (GIsAutomationTesting)
	{
		for (int32 PumpIndex = 0; PumpIndex < 1000; ++PumpIndex)
		{
			PumpBackgroundLayoutSolves();
			const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = GetBackgroundSolveDiagnostics();
			if (Snapshot.Running == 0
				&& Snapshot.WaitingForDispatch == 0
				&& Snapshot.WaitingForPrerequisites == 0
				&& Snapshot.CompletedAwaitingPublish == 0)
			{
				break;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		PumpBackgroundLayoutSolves();
	}
}

void UChunkWorldLayoutRuntimeComponent::AddObservedLoadedChunkOriginForTesting(const FIntVector& ChunkBlockWorldPos)
{
	AddFreshCreatedChunkOriginForTesting(ChunkBlockWorldPos);
}

void UChunkWorldLayoutRuntimeComponent::AddFreshCreatedChunkOriginForTesting(const FIntVector& ChunkBlockWorldPos)
{
	RecordFinestLoadedChunk(ChunkBlockWorldPos, true);
}

void UChunkWorldLayoutRuntimeComponent::AddChunkStampMarkForTesting(const FIntVector& ChunkOrigin, const FLayoutId ArtifactId, const FLayoutId RootSolveId)
{
	static const FLayoutId LoadedFromSaveSentinel(TEXT("LoadedFromSave"));
	FLayoutChunkStampMark& StampMark = StampedChunkOrigins.FindOrAdd(ChunkOrigin);
	if (ArtifactId == LoadedFromSaveSentinel)
	{
		StampMark.bLoadedFromSave = true;
		StampMark.StampedArtifactIdsByRootSolveId.Reset();
		return;
	}
	StampMark.StampedArtifactIdsByRootSolveId.Add(RootSolveId, ArtifactId);
}

int32 UChunkWorldLayoutRuntimeComponent::GetChunkStampedRootCountForTesting(const FIntVector& ChunkOrigin) const
{
	const FLayoutChunkStampMark* const StampMark = StampedChunkOrigins.Find(ChunkOrigin);
	return StampMark != nullptr ? StampMark->StampedArtifactIdsByRootSolveId.Num() : 0;
}

void UChunkWorldLayoutRuntimeComponent::RunQueuedLayoutWorkForTesting()
{
	ProcessQueuedLayoutWork();
	if (GIsAutomationTesting)
	{
		ImportAcceptedPlannedLayoutSiteRecordsForRealization();
		for (int32 PumpIndex = 0; PumpIndex < 1000; ++PumpIndex)
		{
			PumpBackgroundLayoutSolves();
			const FLayoutBackgroundSolveDiagnosticsSnapshot Snapshot = GetBackgroundSolveDiagnostics();
			if (Snapshot.Running == 0
				&& Snapshot.WaitingForDispatch == 0
				&& Snapshot.WaitingForPrerequisites == 0
				&& Snapshot.CompletedAwaitingPublish == 0)
			{
				break;
			}
			FPlatformProcess::Sleep(0.01f);
		}
		PumpBackgroundLayoutSolves();
		ImportAcceptedPlannedLayoutSiteRecordsForRealization();
	}
}

void UChunkWorldLayoutRuntimeComponent::RunSiteAndConnectorRealizationForTesting()
{
	RunEligibleRealizationPasses(false);
}

void UChunkWorldLayoutRuntimeComponent::RunConnectorRealizationForTesting()
{
	TryRealizeEligibleConnectors();
}

#endif

void UChunkWorldLayoutRuntimeComponent::ProcessQueuedLayoutWorkNow()
{
	ProcessQueuedLayoutWork();
}

void UChunkWorldLayoutRuntimeComponent::RunEligibleRealizationPasses(const bool bRefreshConnectors)
{
	if (bRefreshConnectors)
	{
		RefreshConnectorRecords();
	}

	TryRealizeEligibleSites();
	TryRealizeEligibleConnectors();
}

#if WITH_AUTOMATION_TESTS
bool UChunkWorldLayoutRuntimeComponent::TryRealizeConnectorForTesting(
	FResolvedLayoutConnectorRecord& ConnectorRecord,
	FString* OutFailureReason)
{
	return TryRealizeConnector(ConnectorRecord, OutFailureReason);
}

bool UChunkWorldLayoutRuntimeComponent::TryRealizeSiteForTesting(
	FResolvedLayoutSiteRecord& SiteRecord,
	FString* OutFailureReason)
{
	return TryRealizeSite(SiteRecord, OutFailureReason);
}

bool UChunkWorldLayoutRuntimeComponent::TryRealizeSiteWithContractForTesting(
	FResolvedLayoutSiteRecord& SiteRecord,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FString* OutFailureReason)
{
	return TryRealizeSite(SiteRecord, OutFailureReason, &FrozenTerrainContract);
}

bool UChunkWorldLayoutRuntimeComponent::TryRealizeSiteRecordByKeyForTesting(
	const FIntPoint& ReservationKey,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FString* OutFailureReason)
{
	FResolvedLayoutSiteRecord* SiteRecord = nullptr;
	for (TPair<FString, FResolvedLayoutSiteRecord>& Pair : ResolvedSiteRecords)
	{
		if (FLayoutSiteReservation::ComputeReservationKey(Pair.Value.SiteCenterBlockWorldPos) == ReservationKey)
		{
			SiteRecord = &Pair.Value;
			break;
		}
	}
	if (SiteRecord == nullptr)
	{
		if (OutFailureReason) { *OutFailureReason = TEXT("No site record for reservation key."); }
		return false;
	}
	if (!TryRealizeSite(*SiteRecord, OutFailureReason, &FrozenTerrainContract))
	{
		return false;
	}
	// Mark the site realized — normally done by TryRealizeEligibleSites after TryRealizeSite succeeds.
	FResolvedLayoutSiteRuntimeState RuntimeState = SiteRecord->GetResolvedSiteRuntimeState();
	RuntimeState.bLayoutRealized = true;
	if (FrozenTerrainContract.CellContracts.ContainsByPredicate([](const FLayoutTerrainCellContractRecord& C) { return C.bHasRampTransitionEvidence; }))
	{
		RuntimeState.TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp;
	}
	else if (FrozenTerrainContract.CellContracts.ContainsByPredicate([](const FLayoutTerrainCellContractRecord& C) { return C.bHasFoundationFillEvidence; }))
	{
		RuntimeState.TerrainFitDiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFoundationFill;
	}
	SiteRecord->SetResolvedSiteRuntimeState(RuntimeState);
	return true;
}

void UChunkWorldLayoutRuntimeComponent::InjectExplicitApplyConnectorForTesting(
	const FResolvedLayoutConnectorRecord& ConnectorRecord)
{
	ExplicitApplyInjectedConnectorForTesting = ConnectorRecord;
}

void UChunkWorldLayoutRuntimeComponent::InjectExplicitApplyConnectorFrozenTerrainContractForTesting(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract)
{
	ResolvedConnectorFrozenTerrainContracts.Add(
		BuildResolvedConnectorRecordKey(ConnectorRecord),
		FrozenTerrainContract);
}

void UChunkWorldLayoutRuntimeComponent::InjectRootRealizationWritePlanForTesting(
	const FLayoutId RootSolveId,
	const FLayoutRealizationWritePlan& WritePlan)
{
	ResolvedRootRealizationWritePlans.Add(
		RootSolveId,
		MakeShared<FLayoutRealizationWritePlan>(WritePlan));
}

void UChunkWorldLayoutRuntimeComponent::SetNextExplicitApplyConnectorFailureReasonForTesting(
	const FString& FailureReason)
{
	NextExplicitApplyConnectorFailureReasonForTesting = FailureReason;
}

void UChunkWorldLayoutRuntimeComponent::SetExplicitPreviewWorkGateForTesting(
	TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Gate)
{
	ExplicitPreviewWorkGateForTesting = MoveTemp(Gate);
}

#endif

void UChunkWorldLayoutRuntimeComponent::ProcessQueuedLayoutWork()
{
	TGuardValue<TOptional<TArray<FIntVector>>> CentersForPass(
		PlanningCenterSnapshot, TOptional<TArray<FIntVector>>(CollectPlanningWindowCenters()));
	decltype(PendingChunkLoads) ChunkLoadsToProcess;
	uint64 ReceivedEvents = 0;
	{
		FScopeLock PendingChunkLoadsLock(&PendingChunkLoadsMutex);
		ReceivedEvents = ReceivedChunkEvents;
		ChunkLoadsToProcess = MoveTemp(PendingChunkLoads);
		PendingChunkLoads.Reset();
	}

	ProcessedChunkObservations += ChunkLoadsToProcess.Num();
	for (const auto& Pair : ChunkLoadsToProcess)
	{
		HandleObservedLoadedChunk(Pair.Value);
	}

	if (bContinuationReadinessDirty) RefreshPlacedContinuationRootReadiness();
	UpdateLoadedChunkPlanning();
	PumpBackgroundLayoutSolves();
	if (bRealizationDirty)
	{
		bRealizationDirty = false;
		ImportAcceptedPlannedLayoutSiteRecordsForRealization();
		RunEligibleRealizationPasses(true);
	}
	else
	{
		RefreshConnectorRecords();
	}
	if (GetDetailedDiagnostics())
	{
		const double Now = FPlatformTime::Seconds();
		if (Now >= NextBookkeepingDiagnosticTime)
		{
			const FString Current = PlanningAreaQueue->DescribeBookkeeping();
			UE_LOG(LogTemp, Log, TEXT("[LayoutBookkeeping] component=%s world=%s received=%llu coalesced=%llu before={%s} after={%s}"),
				*GetPathName(), *GetPathNameSafe(GetWorld()), ReceivedEvents - LastReportedChunkEvents,
				ProcessedChunkObservations - LastReportedChunkObservations, *LastBookkeepingDiagnostic, *Current);
			UE_LOG(LogTemp, Log, TEXT("[LayoutDiscoveryTiming] component=%s completed=%llu active=%d gameThreadSelection={%s} gameThreadCapture={%s} gameThreadPublication={%s}"),
				*GetPathName(), CompletedDiscoveryAreas, PendingPlanningAreaDiscovery.IsSet(),
				*DiscoverySelectionTiming.Describe(), *DiscoveryCaptureTiming.Describe(), *DiscoveryPublicationTiming.Describe());
			DiscoverySelectionTiming = {};
			DiscoveryCaptureTiming = {};
			DiscoveryPublicationTiming = {};
			LastBookkeepingDiagnostic = Current;
			LastReportedChunkEvents = ReceivedEvents;
			LastReportedChunkObservations = ProcessedChunkObservations;
			NextBookkeepingDiagnosticTime = Now + 5.0;
		}
	}
}

ULayoutPlanningWindowStore* UChunkWorldLayoutRuntimeComponent::GetLayoutPlanningWindowStore()
{
	return GetOrCreateLayoutPlanningWindowStore();
}

ULayoutPlanningWindowStore* UChunkWorldLayoutRuntimeComponent::GetOrCreateLayoutPlanningWindowStore()
{
	if (PlanningWindowStore == nullptr)
	{
		PlanningWindowStore = NewObject<ULayoutPlanningWindowStore>(this, TEXT("LayoutPlanningWindowStore"));
	}

	return PlanningWindowStore;
}

int32 UChunkWorldLayoutRuntimeComponent::ResolvePlanningWindowDimensionInBlocks(
	const int32 Value,
	const ELayoutPlanningWindowUnit Unit) const
{
	const int32 SafeValue = FMath::Max(1, Value);
	if (Unit == ELayoutPlanningWindowUnit::Blocks)
	{
		return SafeValue;
	}

	const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
	{
		return SafeValue;
	}

	const FIntVector ChunkBlockSize = ChunkWorld->WorldGenDef->ChunkBlockSize;
	const int32 HorizontalChunkSize = FMath::Max(1, FMath::Max(ChunkBlockSize.X, ChunkBlockSize.Y));
	return SafeValue * HorizontalChunkSize;
}

TArray<FIntVector> UChunkWorldLayoutRuntimeComponent::CollectPlanningWindowCenters() const
{
	if (PlanningCenterSnapshot.IsSet())
	{
		return PlanningCenterSnapshot.GetValue();
	}
	TArray<FIntVector> Centers;
	const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	const UWorld* const World = GetWorld();
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr || World == nullptr)
	{
		return Centers;
	}
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* const Pawn = It->Get() != nullptr ? It->Get()->GetPawn() : nullptr;
		if (Pawn != nullptr)
		{
			Centers.AddUnique(ChunkWorld->UEWorldPosToBlockWorldPos(Pawn->GetActorLocation()));
		}
	}
#if WITH_EDITOR
	FVector CameraLocation;
	if (bFollowEditorCamera && (World->WorldType == EWorldType::Editor || World->WorldType == EWorldType::PIE)
		&& ChunkWorld->TryGetEditorViewportCameraLocation(CameraLocation))
	{
		Centers.AddUnique(ChunkWorld->UEWorldPosToBlockWorldPos(CameraLocation));
	}
#endif
	return Centers;
}

void UChunkWorldLayoutRuntimeComponent::FinishPlanningAreaAttempt(const FString& RecordKey, const bool bFailed, const bool bCanceled)
{
	bLoadedInfluenceDirty = true;
	bRealizationDirty = true;
	if ((bFailed || bCanceled) && RootSpacingReservations.Remove(RecordKey) > 0)
		PlanningAreaQueue->NotifyReservationReleased(RecordKey);
	FIntPoint Area;
	if (PlanningAreasByRecordKey.RemoveAndCopyValue(RecordKey, Area)) PlanningAreaQueue->FinishAttempt(Area, RecordKey, bFailed, bCanceled);
}

void UChunkWorldLayoutRuntimeComponent::CancelPlanningAreaDiscovery()
{
	if (!PendingPlanningAreaDiscovery.IsSet()) return;
	const auto Job = PendingPlanningAreaDiscovery.GetValue();
	PendingPlanningAreaDiscovery.Reset();
	if (BackgroundSolveDispatcher.IsValid()) BackgroundSolveDispatcher->CancelGroup(Job.Handle.LayoutGroupId);
	PlanningAreaQueue->FinishScan(Job.Area, true, Job.ScanId, true);
}

void UChunkWorldLayoutRuntimeComponent::SubmitPlanningAreaDiscovery(
	const FIntPoint Area, const FIntVector Center, const FIntPoint Min, const FIntPoint Max)
{
	check(IsInGameThread());
	check(!PendingPlanningAreaDiscovery.IsSet());
	const bool bTimeCapture = GetDetailedDiagnostics();
	const double CaptureStart = bTimeCapture ? FPlatformTime::Seconds() : 0.0;
	ON_SCOPE_EXIT { if (bTimeCapture) DiscoveryCaptureTiming.Record((FPlatformTime::Seconds() - CaptureStart) * 1000.0); };
	const uint64 ScanId = PlanningAreaQueue->GetScanId(Area);
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
	{
		PlanningAreaQueue->FinishScan(Area, false, ScanId);
		return;
	}
	// Read small resident metrics only. One retained root must exclude every center for
	// each candidate; partial overlap or an inconclusive envelope still gets worker discovery.
	TSet<ULayoutWorldBindingAsset*> ExcludedBindings, DisabledBindings;
	for (ULayoutWorldBindingAsset* Binding : LayoutWorldBindings)
	{
		if (!Binding || Binding->Candidates.IsEmpty()) continue;
		if (!FMath::IsFinite(Binding->OccupancyProbability) || Binding->OccupancyProbability <= 0.0f || Binding->OccupancyProbability > 1.0f)
		{
			DisabledBindings.Add(Binding);
			continue;
		}
		const FName BindingId = Binding->BindingId.IsNone() ? Binding->GetFName() : Binding->BindingId;
		bool bAllExcluded = true;
		for (const auto& Candidate : Binding->Candidates)
		{
			bool bExcluded = false;
			if (Candidate.LayoutProfile)
				for (const auto& Pair : RootSpacingReservations)
					if (Pair.Value.ExcludesCenterRegion(BindingId, Min, Max, Candidate.LayoutProfile->MaximumFootprintInCells,
						Binding->BaseCellDimensionsBlocks, Binding->MinimumRootGapCells))
					{
						PlanningAreaQueue->MarkBlockedByReservation(Area, Pair.Key);
						bExcluded = true;
						break;
					}
			bAllExcluded &= bExcluded;
		}
		if (bAllExcluded) ExcludedBindings.Add(Binding);
	}
	if (!LayoutWorldBindings.IsEmpty() && ExcludedBindings.Num() + DisabledBindings.Num() == LayoutWorldBindings.Num())
	{
		if (GetDetailedDiagnostics())
			UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] origin=automatic-area area=(%d,%d) spacingBindings=%d occupancyBindings=%d captureSkipped=1"),
				Area.X, Area.Y, ExcludedBindings.Num(), DisabledBindings.Num());
		PlanningAreaQueue->FinishScan(Area, false, ScanId);
		return;
	}
	const auto Reservations = MakeShared<const TMap<FString, FLayoutRootSpacingReservation>, ESPMode::ThreadSafe>(RootSpacingReservations);
	const int32 WorldSeed = ResolveLayoutWorldSeed();
	FString Failure;
	const auto Noise = FLayoutActiveBiomeNoiseSnapshot::CaptureFromWorldDefinition(this, ChunkWorld->WorldGenDef, WorldSeed, Failure);
	if (!Noise->IsInitialized())
	{
		ReportLayoutPlanningWarning(this, GetDetailedDiagnostics(), Failure);
		PlanningAreaQueue->FinishScan(Area, false, ScanId);
		return;
	}
	struct FPreparedBinding
	{
		TWeakObjectPtr<ULayoutWorldBindingAsset> Binding;
		LayoutWorldBindingSitePlanner::FSitePlanningSnapshot Inputs;
		TArray<FPlannedLayoutSiteRecord> Records;
		TSet<FString> BlockingReservations;
		int32 Samples = 0, OccupancyRejected = 0;
	};
	const auto Prepared = MakeShared<TArray<FPreparedBinding>, ESPMode::ThreadSafe>();
	const auto Coordinates = MakeCoordinateSettings(ChunkWorld->WorldGenDef);
	for (ULayoutWorldBindingAsset* Binding : LayoutWorldBindings)
	{
		if (Binding == nullptr || Binding->Candidates.IsEmpty() || ExcludedBindings.Contains(Binding) || DisabledBindings.Contains(Binding)) continue;
		for (const FName Row : Binding->BiomeRowNames)
		{
			if (!Noise->GetSampler().HasMatchingRow(Row)) continue;
			auto& Item = Prepared->AddDefaulted_GetRef();
			Item.Binding = Binding;
			Item.Inputs = LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(Binding, Row,
				WorldSeed, Binding->DefaultPlacementPolicy.SurfaceSearch,
				Coordinates, Center.Z, ChunkWorld);
			Item.Inputs.RootReservations = Reservations;
		}
	}
	if (Prepared->IsEmpty())
	{
		if (ExcludedBindings.IsEmpty() && DisabledBindings.IsEmpty()) PlanningAreaQueue->MarkIrrelevant(Area, ScanId);
		PlanningAreaQueue->FinishScan(Area, false, ScanId);
		return;
	}
	const FString DiagnosticContext = GetDetailedDiagnostics()
		? FString::Printf(TEXT("origin=automatic-area area=(%d,%d) scan=%llu"), Area.X, Area.Y, ScanId) : FString();
	const FGuid TransportId = FGuid::NewGuid();
	const uint64 GroupId = ((uint64(TransportId.A) << 32) | TransportId.B) | 1;
	FLayoutBackgroundSolveSubmission Submission;
	Submission.DebugName = FString::Printf(TEXT("PlanningArea(%d,%d)"), Area.X, Area.Y);
	Submission.LayoutGroupId = GroupId;
	Submission.Tier = ELayoutBackgroundSolveJobTier::NearRoot;
	Submission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Preparation;
	Submission.Priority = FLayoutPlanningAreaQueue::ComputePriority(Center, PlanningPriorityCenters);
	Submission.Work = [Prepared, Noise, Min, Max, DiagnosticContext](const FLayoutSolveCancellationToken& Token, FString& OutFailure)
	{
		LayoutSolveExecution::FDiagnosticScope Diagnostics(DiagnosticContext, TEXT("location-preparation"), &OutFailure);
		for (auto& Item : *Prepared)
		{
			if (Token.IsCancellationRequested()) return false;
			const auto& Inputs = Item.Inputs;
			// The shared builder consumes only pocket sample positions. Enumerate normal-cell
			// centers directly instead of building/discarding a large halo grid. Its existing
			// surface/cavity and full-footprint checks still sample across native chunk edges.
			TArray<FLayoutReservationPocket> Pockets;
			Pockets.AddDefaulted_GetRef().SampleBlockXYs = LayoutWorldBindingSitePlanner::BuildBoundedNormalCellSiteCenters(
				Min, Max, Inputs.SharedCellSizeInBlocks);
			Item.Samples = Pockets[0].SampleBlockXYs.Num();
			Item.Records = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
				Pockets, Inputs, Noise->GetSampler(), &Item.BlockingReservations, &Item.OccupancyRejected);
			Item.Records.RemoveAll([Min, Max](const FPlannedLayoutSiteRecord& Record)
			{
				const FIntVector Position = Record.GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos;
				return Position.X < Min.X || Position.X > Max.X || Position.Y < Min.Y || Position.Y > Max.Y;
			});
		}
		const bool bSucceeded = !Token.IsCancellationRequested();
		Diagnostics.Finish(bSucceeded);
		return bSucceeded;
	};
	Submission.PublishOnGameThread = [WeakThis = TWeakObjectPtr<UChunkWorldLayoutRuntimeComponent>(this),
		Prepared, Noise, Area, ScanId, GroupId, DiagnosticContext](const FLayoutBackgroundSolveCompletion& Completion)
	{
		auto* Runtime = WeakThis.Get();
		if (Runtime == nullptr || !Runtime->PendingPlanningAreaDiscovery.IsSet()
			|| Runtime->PendingPlanningAreaDiscovery->ScanId != ScanId) return;
		const bool bTimePublication = Runtime->GetDetailedDiagnostics();
		const double PublicationStart = bTimePublication ? FPlatformTime::Seconds() : 0.0;
		ON_SCOPE_EXIT { if (bTimePublication) Runtime->DiscoveryPublicationTiming.Record((FPlatformTime::Seconds() - PublicationStart) * 1000.0); };
		bool bDeferred = false;
		bool bCanceled = true;
		ON_SCOPE_EXIT
		{
			Runtime->PlanningAreaQueue->FinishScan(Area, bDeferred, ScanId, bCanceled);
			Runtime->bLoadedInfluenceDirty = true;
			if (Runtime->PendingPlanningAreaDiscovery.IsSet() && Runtime->PendingPlanningAreaDiscovery->ScanId == ScanId)
				Runtime->PendingPlanningAreaDiscovery.Reset();
		};
		if (!Runtime->PlanningAreaQueue->IsCurrentScan(Area, ScanId)) return;
		if (!Completion.bWorkSucceeded)
		{
			bCanceled = Completion.bCanceled;
			ReportLayoutPlanningWarning(Runtime, Runtime->GetDetailedDiagnostics(), Completion.FailureReason);
			return;
		}
		++Runtime->CompletedDiscoveryAreas;
		const auto* Owner = Runtime->GetOwningChunkWorld();
		if (!Runtime->bEnablePlanningWindowRuntimeUpdates || Owner == nullptr || !Owner->HasAuthority() || !Owner->IsRunning()) return;
		const auto Centers = Runtime->CollectPlanningWindowCenters();
		if (Centers.IsEmpty()) return;
		if (Prepared->ContainsByPredicate([](const FPreparedBinding& Item) { return !Item.Records.IsEmpty(); })
			&& !Runtime->PlanningAreaQueue->MarkEligible(Area, ScanId))
		{
			bDeferred = true;
			bCanceled = false;
			return;
		}
		TGuardValue<TArray<FIntVector>> PriorityScope(Runtime->PlanningPriorityCenters, Centers);
		TGuardValue<TOptional<FIntPoint>> AreaScope(Runtime->ActivePlanningArea, TOptional<FIntPoint>(Area));
		TGuardValue<bool> DeferredScope(Runtime->bPlanningAreaDeferred, false);
		TGuardValue<int32> AdmissionScope(Runtime->PlanningAreaAdmissions, 0);
		TGuardValue<uint64> GroupScope(Runtime->CompletingPlanningAreaGroupId, GroupId);
		for (const auto& Item : *Prepared)
		{
			ULayoutWorldBindingAsset* Binding = Item.Binding.Get();
			if (Binding == nullptr) continue;
			if (!DiagnosticContext.IsEmpty())
				UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s binding=%s row=%s centers=%d candidates=%d occupancyRejected=%d spacingBlockers=%d"),
					*DiagnosticContext, *Item.Inputs.BindingId.ToString(), *Item.Inputs.MatchingBiomeRowName.ToString(),
					Item.Samples, Item.Records.Num(), Item.OccupancyRejected, Item.BlockingReservations.Num());
			for (const FString& Key : Item.BlockingReservations)
			{
				if (Runtime->RootSpacingReservations.Contains(Key)) Runtime->PlanningAreaQueue->MarkBlockedByReservation(Area, Key);
				else Runtime->bPlanningAreaDeferred = true; // Worker rejected against an exclusion already released.
			}
			Runtime->AdmitPlanningSitesFromWorldBinding(Binding, Item.Inputs, Item.Records, Noise);
		}
		bDeferred = Runtime->bPlanningAreaDeferred;
		bCanceled = false;
	};
	FPlanningAreaDiscoveryJob Job;
	Job.Area = Area;
	Job.Center = Center;
	Job.ScanId = ScanId;
	Job.Handle.LayoutGroupId = GroupId;
	PendingPlanningAreaDiscovery = Job;
	const auto Handle = GetOrCreateBackgroundSolveDispatcher().Submit(MoveTemp(Submission));
	if (PendingPlanningAreaDiscovery.IsSet() && PendingPlanningAreaDiscovery->ScanId == ScanId)
	{
		if (!Handle.IsValid()) CancelPlanningAreaDiscovery();
		else PendingPlanningAreaDiscovery->Handle = Handle;
	}
}

void UChunkWorldLayoutRuntimeComponent::CollectLoadedPlanningBounds(TArray<FIntPoint>& Mins, TArray<FIntPoint>& Maxs) const
{
	Mins.Reset();
	Maxs.Reset();
	for (const FLayoutLoadedChunkLayer& Layer : ObservedChunkLayers)
	{
		if (Layer.ChunkSizeInBlocks.GetMin() <= 0) continue;
		for (const auto& Pair : Layer.Chunks)
		{
			Mins.Add(FIntPoint(Pair.Key.X, Pair.Key.Y));
			Maxs.Add(FIntPoint(
				int32(FMath::Min<int64>(MAX_int32, int64(Pair.Key.X) + Layer.ChunkSizeInBlocks.X - 1)),
				int32(FMath::Min<int64>(MAX_int32, int64(Pair.Key.Y) + Layer.ChunkSizeInBlocks.Y - 1))));
		}
	}
}

bool UChunkWorldLayoutRuntimeComponent::IsAutomaticPlanningPositionEligible(const FIntVector Position) const
{
	if (ObservedCoverageTileSize.GetMin() <= 0
		|| FLayoutStreamingWindow::FindLoadedLayerAtPosition(Position, ObservedChunkLayers, true) == INDEX_NONE) return false;
	const FIntVector Origin = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(Position, ObservedCoverageTileSize);
	const auto* Mark = StampedChunkOrigins.Find(Origin);
	return !Mark || !Mark->bLoadedFromSave;
}

int32 UChunkWorldLayoutRuntimeComponent::CountAutomaticPlanningWork() const
{
	int32 Count = PendingAutomaticContinuationPreparations.Num();
	for (const auto& Pair : PendingPlanningWindowSolveHandlesByRecordKey)
	{
		const auto* Record = PlanningWindowStore ? PlanningWindowStore->FindPlannedLayoutSiteRecord(Pair.Key) : nullptr;
		if (Record && Record->State == EPlannedLayoutSiteState::Realized
			&& (!BackgroundSolveDispatcher || !BackgroundSolveDispatcher->HasRetainedGroup(Pair.Value.LayoutGroupId))) continue;
		++Count;
	}
	for (const auto& Pair : ContinuationRouteReservations) Count += Pair.Value.bAutomatic ? 1 : 0;
	return Count;
}

bool UChunkWorldLayoutRuntimeComponent::MakeAutomaticPlanningRoom(const FIntVector IncomingCenter)
{
	const int32 Capacity = FMath::Max(1, MaxCachedPlanningChunks);
	if (CountAutomaticPlanningWork() < Capacity) return true;
	if (bRealizationDirty)
	{
		bRealizationDirty = false;
		ImportAcceptedPlannedLayoutSiteRecordsForRealization();
		RunEligibleRealizationPasses(false);
	}
	if (CountAutomaticPlanningWork() < Capacity) return true;
	const auto Centers = CollectPlanningWindowCenters();
	const int32 IncomingPriority = FLayoutPlanningAreaQueue::ComputePriority(IncomingCenter, Centers);
	int32 BestTier = 2, BestPriority = MAX_int32;
	FString BestKey, RootKey;
	FLayoutId RouteId;
	uint64 PreparationKey = 0;
	const auto Select = [&](const int32 Tier, const FIntVector Position, const FString& Key)
	{
		const int32 Priority = FLayoutPlanningAreaQueue::ComputePriority(Position, Centers);
		if (Priority >= IncomingPriority || Tier > BestTier
			|| (Tier == BestTier && (Priority > BestPriority || (Priority == BestPriority && Key >= BestKey)))) return false;
		BestTier = Tier;
		BestPriority = Priority;
		BestKey = Key;
		RootKey.Reset();
		RouteId = FLayoutId();
		PreparationKey = 0;
		return true;
	};
	for (const auto& Pair : PendingPlanningWindowSolveHandlesByRecordKey)
	{
		if (BackgroundSolveDispatcher && BackgroundSolveDispatcher->HasRunningGroup(Pair.Value.LayoutGroupId)) continue;
		const auto* Record = PlanningWindowStore ? PlanningWindowStore->FindPlannedLayoutSiteRecord(Pair.Key) : nullptr;
		if (!Record || (Record->State != EPlannedLayoutSiteState::Pending && Record->State != EPlannedLayoutSiteState::Accepted)) continue;
		if (Select(Record->State == EPlannedLayoutSiteState::Accepted ? 1 : 0, Record->SiteCenterBlockWorldPos, Pair.Key)) RootKey = Pair.Key;
	}
	for (const auto& Pair : PendingAutomaticContinuationPreparations)
	{
		if (BackgroundSolveDispatcher && BackgroundSolveDispatcher->HasRunningGroup(Pair.Value.LayoutGroupId)) continue;
		const auto* Edge = PlanningWindowStore ? PlanningWindowStore->FindContinuationEdgeRecord(ContinuationEdgeKeysByConnectorKey.FindRef(Pair.Key)) : nullptr;
		if (Edge && Select(0, Edge->StartEndpointBlockWorldPos, Edge->EdgeKey)) PreparationKey = Pair.Key;
	}
	for (const auto& Pair : ContinuationRouteReservations)
	{
		if (!Pair.Value.bAutomatic || Pair.Value.bReleased) continue;
		const auto* Prepared = RetainedPreparedContinuationRoutesById.Find(Pair.Key);
		if (!Prepared) continue;
		bool bRunning = false, bSolved = false;
		for (const uint64 Key : Pair.Value.SegmentKeys)
		{
			bRunning |= BackgroundSolveDispatcher && BackgroundSolveDispatcher->HasRunningGroup(Key);
			bSolved |= ResolvedConnectorRecords.Contains(Key);
		}
		if (!bRunning && Select(bSolved ? 1 : 0, Prepared->Route.StartRootEndpoint.EndpointBlockWorldPos, Pair.Key.ToString())) RouteId = Pair.Key;
	}
	if (!RootKey.IsEmpty()) RetireAutomaticRoot(RootKey);
	else if (!RouteId.IsNone()) RetireAutomaticContinuationRoute(RouteId);
	else if (PreparationKey != 0)
	{
		const auto Handle = PendingAutomaticContinuationPreparations.FindChecked(PreparationKey);
		if (BackgroundSolveDispatcher) BackgroundSolveDispatcher->CancelGroup(Handle.LayoutGroupId);
		ReleaseContinuationReservationForConnector(PreparationKey);
		if (!BackgroundSolveDispatcher || !BackgroundSolveDispatcher->HasRetainedGroup(Handle.LayoutGroupId))
			PendingAutomaticContinuationPreparations.Remove(PreparationKey);
	}
	else return false;
	++EvictedAutomaticWork;
	// A canceled publication/worker may still own its captures. Do not spend that capacity twice.
	return CountAutomaticPlanningWork() < Capacity;
}

void UChunkWorldLayoutRuntimeComponent::RetireAutomaticRoot(const FString& RecordKey)
{
	const FPlannedLayoutSiteRecord* Record = PlanningWindowStore ? PlanningWindowStore->FindPlannedLayoutSiteRecord(RecordKey) : nullptr;
	if (Record && Record->State == EPlannedLayoutSiteState::Realized) return;
	TombstonePlanningRootFrozenSubmissionDescriptor(RecordKey);
	if (const auto* Handle = PendingPlanningWindowSolveHandlesByRecordKey.Find(RecordKey); Handle && BackgroundSolveDispatcher)
		BackgroundSolveDispatcher->CancelGroup(Handle->LayoutGroupId);
	FinishPlanningAreaAttempt(RecordKey, false, true);
	if (PlanningWindowStore)
	{
		PlanningWindowStore->RemoveUnrealizedPlannedLayoutSiteRecord(RecordKey);
		PlanningWindowStore->RemoveContinuationEndpointRecordsForRoot(RecordKey);
	}
	for (auto It = PlanningRecordKeysBySiteRecordKey.CreateIterator(); It; ++It)
	{
		if (It.Value() != RecordKey) continue;
		if (const auto* Site = ResolvedSiteRecords.Find(It.Key()); Site && !Site->bHasBeenCommittedToChunkWorld)
		{
			ResolvedRootFrozenTerrainContracts.Remove(Site->RootSolveId);
			ResolvedRootRealizationWritePlans.Remove(Site->RootSolveId);
			ResolvedSiteRecords.Remove(It.Key());
		}
		It.RemoveCurrent();
	}
	const auto* Handle = PendingPlanningWindowSolveHandlesByRecordKey.Find(RecordKey);
	if (Handle && (!BackgroundSolveDispatcher || !BackgroundSolveDispatcher->HasRetainedGroup(Handle->LayoutGroupId)))
		PendingPlanningWindowSolveHandlesByRecordKey.Remove(RecordKey);
}

void UChunkWorldLayoutRuntimeComponent::CancelAutomaticRootWork()
{
	TArray<FString> Keys;
	PendingPlanningWindowSolveHandlesByRecordKey.GetKeys(Keys);
	for (const FString& Key : Keys) RetireAutomaticRoot(Key);
	TSet<FString> PreservedReservations;
	for (const auto& Pair : ResolvedSiteRecords)
	{
		const FString* AutomaticKey = PlanningRecordKeysBySiteRecordKey.Find(Pair.Key);
		if (!AutomaticKey || Pair.Value.bHasBeenCommittedToChunkWorld)
			PreservedReservations.Add(AutomaticKey ? *AutomaticKey : Pair.Key);
	}
	for (auto It = RootSpacingReservations.CreateIterator(); It; ++It)
		if (!PreservedReservations.Contains(It.Key()))
		{
			PlanningAreaQueue->NotifyReservationReleased(It.Key());
			It.RemoveCurrent();
		}
}

void UChunkWorldLayoutRuntimeComponent::UpdateLoadedChunkPlanning()
{
	if (GetOwner() && !GetOwner()->HasAuthority()) return;
	RefreshPlanningBiomeTableSubscription();
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	UWorld* const World = GetWorld();
	const TArray<FIntVector> Centers = CollectPlanningWindowCenters();
	const bool bActive = bEnablePlanningWindowRuntimeUpdates && !LayoutWorldBindings.IsEmpty()
		&& ChunkWorld && ChunkWorld->WorldGenDef && ChunkWorld->IsRunning() && World && !Centers.IsEmpty();
	if (!bActive)
	{
		if (bAutomaticPlanningActive || bLoadedInfluenceDirty)
		{
			CancelPlanningAreaDiscovery();
			CancelAutomaticRootWork();
			RetireInvalidAutomaticContinuations(true);
			PlanningAreaQueue->RetireWorkingSet();
			for (const auto& Pair : ResolvedSiteRecords)
			{
				if (Pair.Value.bHasBeenCommittedToChunkWorld) continue;
				if (const FString* Key = PlanningRecordKeysBySiteRecordKey.Find(Pair.Key))
				{
					if (RootSpacingReservations.Remove(*Key) > 0) PlanningAreaQueue->NotifyReservationReleased(*Key);
					if (PlanningWindowStore) PlanningWindowStore->RemoveContinuationEndpointRecordsForRoot(*Key);
				}
			}
			TArray<FIntPoint> Mins, Maxs;
			CollectLoadedPlanningBounds(Mins, Maxs);
			PruneContinuationEndpointsOutsideExpandedWindows(Mins, Maxs);
			PruneRootSpacingReservations(Mins, Maxs);
			bLoadedInfluenceDirty = false;
		}
		bAutomaticPlanningActive = false;
		return;
	}
	bLoadedInfluenceDirty |= !bAutomaticPlanningActive;
	bAutomaticPlanningActive = true;
	if (PendingPlanningAreaDiscovery.IsSet()
		&& !PlanningAreaQueue->IsCurrentScan(PendingPlanningAreaDiscovery->Area, PendingPlanningAreaDiscovery->ScanId))
		CancelPlanningAreaDiscovery();
	if (bLoadedInfluenceDirty)
	{
		// Evaluate the coalesced final LOD union, not each Delete/Create callback in isolation.
		TArray<FString> InvalidRoots;
		for (const auto& Pair : PendingPlanningWindowSolveHandlesByRecordKey)
		{
			const auto* Record = PlanningWindowStore ? PlanningWindowStore->FindPlannedLayoutSiteRecord(Pair.Key) : nullptr;
			if (Record && Record->State != EPlannedLayoutSiteState::Realized
				&& !IsAutomaticPlanningPositionEligible(Record->SiteCenterBlockWorldPos)) InvalidRoots.Add(Pair.Key);
		}
		for (const FString& Key : InvalidRoots) RetireAutomaticRoot(Key);
		TArray<FIntPoint> Mins, Maxs;
		CollectLoadedPlanningBounds(Mins, Maxs);
		PruneContinuationEndpointsOutsideExpandedWindows(Mins, Maxs);
		PruneRootSpacingReservations(Mins, Maxs);
		bLoadedInfluenceDirty = false;
	}
	const double Now = World->WorldType == EWorldType::Editor ? FPlatformTime::Seconds() : World->GetTimeSeconds();
	// Priority housekeeping is infrequent; idle discovery wakes on this later tick,
	// not from its publication callback and not after another full polling interval.
	const bool bRefreshPriorities = Now - LastPlanningWindowUpdateTimeSeconds >= PlanningWindowUpdateIntervalSeconds;
	if (bRefreshPriorities) LastPlanningWindowUpdateTimeSeconds = Now;
	if (bRefreshPriorities && BackgroundSolveDispatcher)
	{
		TMap<uint64, int32> Priorities;
		for (const auto& Pair : PendingPlanningWindowSolveHandlesByRecordKey)
			if (const FIntVector* Center = PendingPlanningWindowSolveCentersByRecordKey.Find(Pair.Key))
				Priorities.Add(Pair.Value.LayoutGroupId, FLayoutPlanningAreaQueue::ComputePriority(*Center, Centers));
		if (PendingPlanningAreaDiscovery.IsSet())
			Priorities.Add(PendingPlanningAreaDiscovery->Handle.LayoutGroupId,
				FLayoutPlanningAreaQueue::ComputePriority(PendingPlanningAreaDiscovery->Center, Centers));
		for (const auto& Pair : PendingAutomaticContinuationPreparations)
		{
			const auto* Edge = PlanningWindowStore ? PlanningWindowStore->FindContinuationEdgeRecord(
				ContinuationEdgeKeysByConnectorKey.FindRef(Pair.Key)) : nullptr;
			if (Edge) Priorities.Add(Pair.Value.LayoutGroupId,
				FLayoutPlanningAreaQueue::ComputePriority(Edge->StartEndpointBlockWorldPos, Centers));
		}
		for (const auto& Pair : ContinuationRouteReservations)
		{
			if (!Pair.Value.bAutomatic || Pair.Value.bCanceled || Pair.Value.bReleased) continue;
			const auto* Prepared = RetainedPreparedContinuationRoutesById.Find(Pair.Key);
			if (!Prepared) continue;
			const int32 Priority = FLayoutPlanningAreaQueue::ComputePriority(
				Prepared->Route.StartRootEndpoint.EndpointBlockWorldPos, Centers);
			for (const uint64 SegmentKey : Pair.Value.SegmentKeys) Priorities.Add(SegmentKey, Priority);
		}
		BackgroundSolveDispatcher->UpdateWaitingGroupPriorities(Priorities);
	}
	const FLayoutPlanningWindowSettings Settings = GetOrCreateLayoutPlanningWindowStore()->GetPlanningWindowSettings();
	if (!Settings.Validate().IsValid()) return;
	int32 Spacing = FMath::Clamp(ResolvePlanningWindowDimensionInBlocks(Settings.SampleSpacing, Settings.SampleSpacingUnit), 1, MAX_int32 / 4);
	// SampleSpacing caps scheduling-region size, never the final candidate stride.
	// Four normal-cell columns/rows per region bound every binding's worker enumeration.
	for (const ULayoutWorldBindingAsset* Binding : LayoutWorldBindings)
		if (Binding && Binding->BaseCellDimensionsBlocks.X > 0 && Binding->BaseCellDimensionsBlocks.Y > 0)
			Spacing = FMath::Min(Spacing, FMath::Min(Binding->BaseCellDimensionsBlocks.X, Binding->BaseCellDimensionsBlocks.Y));
	PlanningAreaQueue->Configure(Spacing, MaxCachedPlanningChunks);
	if (PendingPlanningAreaDiscovery.IsSet()) return;
	if (BackgroundSolveDispatcher && BackgroundSolveDispatcher->GetDiagnosticsSnapshot().InProgressLayoutGroups
		>= BuildBackgroundSolveSettings().ResolveMaxConcurrentBackgroundLayoutSolves()) return;
	FIntPoint Area;
	const bool bTimeSelection = GetDetailedDiagnostics();
	const double SelectionStart = bTimeSelection ? FPlatformTime::Seconds() : 0.0;
	const bool bSelected = PlanningAreaQueue->TakeNext(Centers, Area);
	if (bTimeSelection) DiscoverySelectionTiming.Record((FPlatformTime::Seconds() - SelectionStart) * 1000.0);
	if (!bSelected) return;
	FIntPoint Min, Max;
	PlanningAreaQueue->GetBounds(Area, Min, Max);
	const FIntVector Center(int32((int64(Min.X) + Max.X) / 2), int32((int64(Min.Y) + Max.Y) / 2),
		PlanningAreaQueue->GetSelectedCenter().Z);
	if (!MakeAutomaticPlanningRoom(Center))
	{
		PlanningAreaQueue->FinishScan(Area, true, PlanningAreaQueue->GetScanId(Area));
		return;
	}
	SubmitPlanningAreaDiscovery(Area, Center, Min, Max);
}

void UChunkWorldLayoutRuntimeComponent::PruneContinuationEndpointsOutsideExpandedWindows(
	const TArray<FIntPoint>& MinBlockXYs,
	const TArray<FIntPoint>& MaxBlockXYs)
{
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	if (Store == nullptr || MinBlockXYs.Num() != MaxBlockXYs.Num())
	{
		return;
	}
	int32 MaxFamilyRangeBlocks = 0;
	for (const ULayoutWorldBindingAsset* WorldBinding : GetLayoutWorldBindings())
	{
		if (WorldBinding == nullptr) continue;
		const int32 CellWidth = FMath::Max(WorldBinding->BaseCellDimensionsBlocks.X, WorldBinding->BaseCellDimensionsBlocks.Y);
		for (const FLayoutWorldBindingContinuationFamily& Family : WorldBinding->ContinuationFamilies)
		{
			MaxFamilyRangeBlocks = FMath::Max(
				MaxFamilyRangeBlocks,
				FMath::Max(0, Family.MaxConnectionDistanceInCells) * FMath::Max(1, CellWidth));
		}
	}
	const int32 RetentionRadiusBlocks = MaxFamilyRangeBlocks;
	TArray<FIntPoint> ExpandedMinBlockXYs;
	TArray<FIntPoint> ExpandedMaxBlockXYs;
	ExpandedMinBlockXYs.Reserve(MinBlockXYs.Num());
	ExpandedMaxBlockXYs.Reserve(MaxBlockXYs.Num());
	for (int32 Index = 0; Index < MinBlockXYs.Num(); ++Index)
	{
		ExpandedMinBlockXYs.Add(MinBlockXYs[Index] - FIntPoint(RetentionRadiusBlocks, RetentionRadiusBlocks));
		ExpandedMaxBlockXYs.Add(MaxBlockXYs[Index] + FIntPoint(RetentionRadiusBlocks, RetentionRadiusBlocks));
	}
	for (const TPair<FLayoutId, FLayoutPreparedContinuationRoute>& Pair : RetainedPreparedContinuationRoutesById)
	{
		const FLayoutContinuationRouteReservationState* const Reservation =
			ContinuationRouteReservations.Find(Pair.Key);
		bool bHasExplicitPreview = false;
		if (Reservation != nullptr)
		{
			for (const uint64 SegmentKey : Reservation->SegmentKeys)
			{
				if (ExplicitPreviewConnectorKeys.Contains(SegmentKey))
				{
					bHasExplicitPreview = true;
					break;
				}
			}
		}
		if (!bHasExplicitPreview)
		{
			continue;
		}

		// An explicit preview owns its reservation until Apply or Clear, even when
		// its endpoints leave loaded terrain and family-reach influence.
		for (const FResolvedLayoutConnectorEndpoint& Endpoint : {
			Pair.Value.Route.StartRootEndpoint,
			Pair.Value.Route.EndRootEndpoint })
		{
			const FIntPoint EndpointXY(Endpoint.EndpointBlockWorldPos.X, Endpoint.EndpointBlockWorldPos.Y);
			ExpandedMinBlockXYs.Add(EndpointXY);
			ExpandedMaxBlockXYs.Add(EndpointXY);
		}
	}
	Store->RemoveContinuationEndpointRecordsOutsideWindows(ExpandedMinBlockXYs, ExpandedMaxBlockXYs);

	auto IsInsideAnyExpandedWindow = [&ExpandedMinBlockXYs, &ExpandedMaxBlockXYs](const FIntVector& Position)
	{
		for (int32 Index = 0; Index < ExpandedMinBlockXYs.Num(); ++Index)
		{
			if (Position.X >= ExpandedMinBlockXYs[Index].X && Position.X <= ExpandedMaxBlockXYs[Index].X
				&& Position.Y >= ExpandedMinBlockXYs[Index].Y && Position.Y <= ExpandedMaxBlockXYs[Index].Y)
			{
				return true;
			}
		}
		return false;
	};

	TArray<FLayoutId> ExpiredRouteIds;
	for (const TPair<FLayoutId, FLayoutPreparedContinuationRoute>& Pair : RetainedPreparedContinuationRoutesById)
	{
		const FLayoutContinuationRouteRecord& Route = Pair.Value.Route;
		if (!IsInsideAnyExpandedWindow(Route.StartRootEndpoint.EndpointBlockWorldPos)
			|| !IsInsideAnyExpandedWindow(Route.EndRootEndpoint.EndpointBlockWorldPos))
		{
			ExpiredRouteIds.Add(Pair.Key);
		}
	}
	for (const FLayoutId RouteId : ExpiredRouteIds)
	{
		const FLayoutContinuationRouteReservationState* const Reservation =
			ContinuationRouteReservations.Find(RouteId);
		if (Reservation == nullptr)
		{
			RetainedPreparedContinuationRoutesById.Remove(RouteId);
			continue;
		}

		const TArray<uint64> SegmentKeys = Reservation->SegmentKeys.Array();
		if (SegmentKeys.ContainsByPredicate([this](const uint64 SegmentKey)
			{
				return ExplicitPreviewConnectorKeys.Contains(SegmentKey);
			}))
		{
			continue;
		}

		for (const uint64 SegmentKey : SegmentKeys)
		{
			// Expiry owns the whole segment lifecycle, even after its prewarm handle retires.
			if (BackgroundSolveDispatcher.IsValid()) BackgroundSolveDispatcher->CancelGroup(SegmentKey);
			TombstoneConnectorFrozenSubmissionDescriptor(SegmentKey,
				PendingConnectorFrozenSubmissionDescriptorIdsByKey.FindRef(SegmentKey));
			if (!ExplicitConnectorKeys.Contains(SegmentKey))
			{
				ResolvedConnectorRecords.Remove(SegmentKey);
				ResolvedConnectorFrozenTerrainContracts.Remove(SegmentKey);
			}
		}
		for (auto It = ExplicitRoutesByConnectorKey.CreateIterator(); It; ++It)
		{
			if (FLayoutId(*It.Value().RouteKey) == RouteId)
			{
				PendingExplicitPreparedRoutes.Remove(It.Key());
				ExplicitRouteSegmentKeysByConnectorKey.Remove(It.Key());
				ExplicitContinuationCompletionCallbacks.Remove(It.Key());
				It.RemoveCurrent();
			}
		}
		ReleaseContinuationRouteReservation(RouteId);
	}
	// Route settlement releases its reservation during application. Reclaim committed
	// automatic carriers here, after application no longer holds map references.
	for (auto It = ResolvedConnectorRecords.CreateIterator(); It; ++It)
	{
		if (It.Value().GetResolvedConnectorRuntimeState().bHasBeenCommittedToChunkWorld
			&& !ContinuationRouteIdsByConnectorKey.Contains(It.Key())
			&& !ExplicitConnectorKeys.Contains(It.Key()) && !ExplicitPreviewConnectorKeys.Contains(It.Key()))
		{
			ResolvedConnectorFrozenTerrainContracts.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
	TSet<FString> RetainedRoots;
	for (const auto& Endpoint : Store->GetContinuationEndpointRecords()) RetainedRoots.Add(Endpoint.RootRecordKey);
	for (const auto& Pair : RetainedPreparedContinuationRoutesById)
	{
		RetainedRoots.Add(Pair.Value.Route.StartRootEndpoint.RootRecordKey);
		RetainedRoots.Add(Pair.Value.Route.EndRootEndpoint.RootRecordKey);
	}
	for (auto It = PlacedContinuationRootChunks.CreateIterator(); It; ++It)
	{
		if (!RetainedRoots.Contains(It.Key()))
		{
			LoadedContinuationRootKeys.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UChunkWorldLayoutRuntimeComponent::RemoveFrozenSubmissionDescriptorPayload(const FLayoutId DescriptorId)
{
	if (!DescriptorId.IsNone() && FrozenSubmissionStore.IsValid())
	{
		FrozenSubmissionStore->Remove(DescriptorId);
	}
}

void UChunkWorldLayoutRuntimeComponent::TombstoneFrozenSubmissionDescriptorPayload(const FLayoutId DescriptorId)
{
	if (!DescriptorId.IsNone())
	{
		FrozenSubmissionTombstoneFences.FindOrAdd(DescriptorId,
			BackgroundSolveDispatcher.IsValid() ? BackgroundSolveDispatcher->GetLatestSubmittedJobId() : 0);
		RemoveFrozenSubmissionDescriptorPayload(DescriptorId);
	}
}

void UChunkWorldLayoutRuntimeComponent::RemovePlanningRootFrozenSubmissionDescriptorPayload(const FLayoutId DescriptorId)
{
	RemoveFrozenSubmissionDescriptorPayload(DescriptorId);
}

bool UChunkWorldLayoutRuntimeComponent::IsFrozenSubmissionDescriptorTombstoned(const FLayoutId DescriptorId) const
{
	return !DescriptorId.IsNone() && FrozenSubmissionTombstoneFences.Contains(DescriptorId);
}

void UChunkWorldLayoutRuntimeComponent::TombstonePlanningRootFrozenSubmissionDescriptor(
	const FString& StableRecordKey,
	const FLayoutId FallbackDescriptorId)
{
	FLayoutId DescriptorIdToRemove = FallbackDescriptorId;
	if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
	{
		FPlannedLayoutSiteRecord PlannedRecord;
		if (Store->TryGetPlannedLayoutSiteRecord(StableRecordKey, PlannedRecord))
		{
			const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
				PlannedRecord.GetPlannedSiteLifecycleMetadata();
			if (!FallbackDescriptorId.IsNone() && !LifecycleMetadata.FrozenSubmissionDescriptorId.IsNone()
				&& FallbackDescriptorId != LifecycleMetadata.FrozenSubmissionDescriptorId)
			{
				TombstoneFrozenSubmissionDescriptorPayload(FallbackDescriptorId);
				return;
			}
			if (!LifecycleMetadata.FrozenSubmissionDescriptorId.IsNone())
			{
				DescriptorIdToRemove = LifecycleMetadata.FrozenSubmissionDescriptorId;
			}
			Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
				StableRecordKey,
				ELayoutPlannedSiteFrozenSubmissionState::Tombstoned,
				LifecycleMetadata.FrozenSubmissionDescriptorId,
				LifecycleMetadata.FrozenSubmissionGeneration,
				LifecycleMetadata.FrozenSubmissionAttemptIndex,
				LifecycleMetadata.FrozenSubmissionAuditHash);
		}
	}
	TombstoneFrozenSubmissionDescriptorPayload(DescriptorIdToRemove);
	PendingPlanningWindowSolveCentersByRecordKey.Remove(StableRecordKey);
}

bool UChunkWorldLayoutRuntimeComponent::RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
	const FString& StableRecordKey,
	const FString& RejectionReason,
	const FLayoutId FallbackDescriptorId,
	const ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind)
{
	bool bRejectedRecord = false;
	bool bHadRecord = false;
	if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
	{
		FPlannedLayoutSiteRecord Record;
		bHadRecord = Store->TryGetPlannedLayoutSiteRecord(StableRecordKey, Record);
		const FLayoutPlannedSiteLifecycleMetadata Metadata = Record.GetPlannedSiteLifecycleMetadata();
		if (!FallbackDescriptorId.IsNone())
		{
			const FLayoutId CurrentDescriptorId = Metadata.FrozenSubmissionDescriptorId;
			// A late terminal callback owns its attempt, never a replacement submitted for this site.
			if (IsFrozenSubmissionDescriptorTombstoned(FallbackDescriptorId)
				|| (!CurrentDescriptorId.IsNone() && CurrentDescriptorId != FallbackDescriptorId)
				|| Metadata.State == EPlannedLayoutSiteState::Accepted
				|| Metadata.State == EPlannedLayoutSiteState::Realized)
			{
				TombstoneFrozenSubmissionDescriptorPayload(FallbackDescriptorId);
				return false;
			}
		}
		bRejectedRecord = Store->RejectPlannedLayoutSiteRecord(StableRecordKey, RejectionReason, TerrainFitDiagnosticKind);
		if (bRejectedRecord)
		{
			// Generic rejection clears descriptor metadata. Retain this runtime attempt so repeated
			// retries advance rather than recycling an already-tombstoned descriptor ID.
			Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
				StableRecordKey, ELayoutPlannedSiteFrozenSubmissionState::Rejected,
				Metadata.FrozenSubmissionDescriptorId.IsNone() ? FallbackDescriptorId : Metadata.FrozenSubmissionDescriptorId,
				Metadata.FrozenSubmissionGeneration, Metadata.FrozenSubmissionAttemptIndex,
				Metadata.FrozenSubmissionAuditHash);
		}
	}
	TombstonePlanningRootFrozenSubmissionDescriptor(StableRecordKey, FallbackDescriptorId);
	if (const auto* Handle = PendingPlanningWindowSolveHandlesByRecordKey.Find(StableRecordKey);
		Handle && (!BackgroundSolveDispatcher || !BackgroundSolveDispatcher->HasRetainedGroup(Handle->LayoutGroupId)))
	{
		PendingPlanningWindowSolveHandlesByRecordKey.Remove(StableRecordKey);
	}
	// Dense preparation can reject before a store record exists. Its admitted area attempt
	// still failed; treating that outcome as cancellation permits endless unchanged retries.
	if (bRejectedRecord || !bHadRecord) FinishPlanningAreaAttempt(StableRecordKey, true);
	if (GetDetailedDiagnostics())
	{
		UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] origin=automatic descriptor=%s record=%s event=rejection recordUpdated=%d reason=%s"),
			*FallbackDescriptorId.ToString(), *StableRecordKey, bRejectedRecord,
			*RejectionReason.Left(1024).Replace(TEXT("\n"), TEXT(" ")).Replace(TEXT("\r"), TEXT(" ")));
	}
	return bRejectedRecord;
}

void UChunkWorldLayoutRuntimeComponent::TombstoneConnectorFrozenSubmissionDescriptor(
	const uint64 ConnectorKey,
	const FLayoutId DescriptorId)
{
	TombstoneFrozenSubmissionDescriptorPayload(DescriptorId);
	PendingConnectorFrozenSubmissionDescriptorIdsByKey.Remove(ConnectorKey);
	PendingConnectorFrozenSubmissionGenerationsByKey.Remove(ConnectorKey);
	PendingConnectorFrozenSubmissionAttemptIndicesByKey.Remove(ConnectorKey);
	PendingConnectorFrozenSubmissionAuditHashesByKey.Remove(ConnectorKey);
}

int32 UChunkWorldLayoutRuntimeComponent::ResolveLayoutWorldSeed() const
{
	const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	return ChunkWorld != nullptr ? ChunkWorld->Seed : LayoutWorldSeed;
}


void UChunkWorldLayoutRuntimeComponent::PruneRootSpacingReservations(const TArray<FIntPoint>& Mins, const TArray<FIntPoint>& Maxs)
{
	if (Mins.Num() != Maxs.Num()) return;
	for (auto It = RootSpacingReservations.CreateIterator(); It; ++It)
	{
		if (PendingPlanningWindowSolveHandlesByRecordKey.Contains(It.Key())) continue;
		// Explicit cached roots retain their user-owned Apply/Clear lifetime.
		if (ResolvedSiteRecords.Contains(It.Key()) && !PlanningRecordKeysBySiteRecordKey.Contains(It.Key())) continue;
		const ULayoutWorldBindingAsset* Binding = FindRuntimeWorldBindingById(LayoutWorldBindings, It.Value().BindingId);
		int64 MarginX = 0, MarginY = 0;
		if (Binding != nullptr)
		{
			int32 Width = 0, Height = 0;
			for (const auto& Candidate : Binding->Candidates)
			{
				if (Candidate.LayoutProfile == nullptr) continue;
				Width = FMath::Max(Width, Candidate.LayoutProfile->MaximumFootprintInCells.X);
				Height = FMath::Max(Height, Candidate.LayoutProfile->MaximumFootprintInCells.Y);
			}
			MarginX = int64(FMath::Max(0, Binding->MinimumRootGapCells)) * Binding->BaseCellDimensionsBlocks.X
				+ (int64(Width) * Binding->BaseCellDimensionsBlocks.X + 1) / 2;
			MarginY = int64(FMath::Max(0, Binding->MinimumRootGapCells)) * Binding->BaseCellDimensionsBlocks.Y
				+ (int64(Height) * Binding->BaseCellDimensionsBlocks.Y + 1) / 2;
		}
		if (!It.Value().IntersectsCoverage(Mins, Maxs, MarginX, MarginY))
		{
			PlanningAreaQueue->NotifyReservationReleased(It.Key());
			It.RemoveCurrent();
		}
	}
	TSet<FString> RetainedRoots;
	for (const auto& Pair : RootSpacingReservations) RetainedRoots.Add(Pair.Key);
	for (const auto& Pair : PlacedContinuationRootChunks) RetainedRoots.Add(Pair.Key);
	for (auto It = ResolvedSiteRecords.CreateIterator(); It; ++It)
	{
		const FString* RootKey = PlanningRecordKeysBySiteRecordKey.Find(It.Key());
		if (RootKey != nullptr && !RetainedRoots.Contains(*RootKey))
		{
			PlanningRecordKeysBySiteRecordKey.Remove(It.Key());
			It.RemoveCurrent();
			continue;
		}
		FResolvedLayoutSiteRecord& Record = It.Value();
		if (RootKey != nullptr && Record.bHasBeenCommittedToChunkWorld && Record.bWritePlanReady)
		{
			// Maintenance runs outside realization's borrowed map references. Endpoints and
			// loaded-root coverage are already published; retain only compact root status/metrics.
			FLayoutSolveResult CompactResult;
			CompactResult.bSucceeded = Record.SolveResult.bSucceeded;
			CompactResult.FootprintSize = Record.SolveResult.FootprintSize;
			CompactResult.SharedCellSizeInBlocks = Record.SolveResult.SharedCellSizeInBlocks;
			Record.SolveResult = MoveTemp(CompactResult);
			Record.ExportedEntryCells.Empty();
			Record.ExportedConnectorTypeTags.Reset();
			Record.bWritePlanReady = false;
		}
	}
	// Committed automatic roots keep identity and exclusion, not replay payloads.
	TSet<FLayoutId> RetainedSolveIds;
	for (const auto& Pair : ResolvedSiteRecords)
	{
		if (!Pair.Value.bHasBeenCommittedToChunkWorld || !PlanningRecordKeysBySiteRecordKey.Contains(Pair.Key))
			RetainedSolveIds.Add(Pair.Value.RootSolveId);
	}
	for (auto It = ResolvedRootFrozenTerrainContracts.CreateIterator(); It; ++It)
	{
		if (!RetainedSolveIds.Contains(It.Key())) It.RemoveCurrent();
	}
	for (auto It = ResolvedRootRealizationWritePlans.CreateIterator(); It; ++It)
	{
		if (!RetainedSolveIds.Contains(It.Key())) It.RemoveCurrent();
	}
	PlanningAreaQueue->AppendRetainedFailureKeys(RetainedRoots);
	if (PlanningWindowStore != nullptr) PlanningWindowStore->RemoveSettledRecordsWithoutInfluence(RetainedRoots);

	// Keep protection for loaded chunks and every retained root, including explicit
	// previews. Only unloaded, unowned history outside coverage may be forgotten.
	TSet<FLayoutId> RetainedStampRoots;
	for (const auto& Pair : ResolvedSiteRecords) RetainedStampRoots.Add(Pair.Value.RootSolveId);
	for (const auto& Pair : ResolvedConnectorRecords) RetainedStampRoots.Add(Pair.Value.RootSolveId);
	const AChunkWorldExtended* World = GetOwningChunkWorld();
	if (!Mins.IsEmpty() && (World == nullptr || World->WorldGenDef == nullptr)) return;
	const FIntVector ChunkSize = World && World->WorldGenDef ? World->WorldGenDef->ChunkBlockSize : FIntVector(1);
	for (auto It = StampedChunkOrigins.CreateIterator(); It; ++It)
	{
		bool bTouchesLoadedChunk = false;
		FIntVector TileMax;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			TileMax[Axis] = static_cast<int32>(FMath::Min<int64>(MAX_int32,
				int64(It.Key()[Axis]) + FMath::Max(1, ObservedCoverageTileSize[Axis]) - 1));
		}
		for (const FLayoutLoadedChunkLayer& Layer : ObservedChunkLayers)
		{
			if (Layer.ChunkSizeInBlocks.GetMin() <= 0 || Layer.Chunks.IsEmpty()) continue;
			const FIntVector Min = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(It.Key(), Layer.ChunkSizeInBlocks);
			const FIntVector Max = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(TileMax, Layer.ChunkSizeInBlocks);
			for (int64 Z = Min.Z; Z <= Max.Z && !bTouchesLoadedChunk; Z += Layer.ChunkSizeInBlocks.Z)
			for (int64 Y = Min.Y; Y <= Max.Y && !bTouchesLoadedChunk; Y += Layer.ChunkSizeInBlocks.Y)
			for (int64 X = Min.X; X <= Max.X && !bTouchesLoadedChunk; X += Layer.ChunkSizeInBlocks.X)
			{
				bTouchesLoadedChunk = Layer.Chunks.Contains(FIntVector(int32(X), int32(Y), int32(Z)));
			}
			if (bTouchesLoadedChunk) break;
		}
		if (bTouchesLoadedChunk) continue;
		bool bRetained = false;
		for (const auto& Stamp : It.Value().StampedArtifactIdsByRootSolveId)
		{
			if (RetainedStampRoots.Contains(Stamp.Key)) { bRetained = true; break; }
		}
		for (int32 Index = 0; !bRetained && Index < Mins.Num(); ++Index)
		{
			bRetained = int64(It.Key().X) <= Maxs[Index].X && int64(It.Key().X) + ChunkSize.X - 1 >= Mins[Index].X
				&& int64(It.Key().Y) <= Maxs[Index].Y && int64(It.Key().Y) + ChunkSize.Y - 1 >= Mins[Index].Y;
		}
		if (!bRetained) It.RemoveCurrent();
	}
}

bool UChunkWorldLayoutRuntimeComponent::CanPlanOrdinaryRootSiteForBinding(
	const ULayoutWorldBindingAsset* const WorldBinding,
	const FLayoutRootSpacingReservation& Bounds,
	const FString& IgnoredStableRecordKey) const
{
	if (WorldBinding == nullptr || WorldBinding->MinimumRootGapCells < 0) return false;
	for (const auto& Pair : RootSpacingReservations)
	{
		if (Pair.Key != IgnoredStableRecordKey
			&& !Bounds.IsSeparatedFrom(Pair.Value, WorldBinding->MinimumRootGapCells, WorldBinding->BaseCellDimensionsBlocks)) return false;
	}
	return true;
}

TArray<FLayoutRootSpacingReservation> UChunkWorldLayoutRuntimeComponent::CaptureContinuationRootFootprints() const
{
	check(IsInGameThread());
	TArray<FLayoutRootSpacingReservation> Footprints;
	for (const auto& Pair : ResolvedSiteRecords)
	{
		const FResolvedLayoutSiteRecord& Record = Pair.Value;
		if (!Record.GetResolvedSiteRuntimeState().bLayoutSolved) continue;
		FLayoutRootSpacingReservation Bounds;
		if (FLayoutRootSpacingReservation::TryBuild(Record.WorldBindingId, Record.SiteCenterBlockWorldPos,
			Record.SolveResult.FootprintSize, Record.SolveResult.SharedCellSizeInBlocks, Bounds))
		{
			if (Record.RealizedFootprintMinBlockWorldPos != FIntVector::ZeroValue)
			{
				const int64 Width = int64(Bounds.Max.X) - Bounds.Min.X;
				const int64 Height = int64(Bounds.Max.Y) - Bounds.Min.Y;
				Bounds.Min = FIntPoint(Record.RealizedFootprintMinBlockWorldPos.X, Record.RealizedFootprintMinBlockWorldPos.Y);
				Bounds.Max = FIntPoint(
					int32(FMath::Min<int64>(int64(Bounds.Min.X) + Width, MAX_int32)),
					int32(FMath::Min<int64>(int64(Bounds.Min.Y) + Height, MAX_int32)));
			}
			Footprints.Add(Bounds);
		}
	}
	return Footprints;
}

void UChunkWorldLayoutRuntimeComponent::PublishResolvedRootContinuationEndpoints(
	const FString& RootRecordKey,
	const FIntPoint& ReservationKey,
	const FResolvedLayoutSiteRecord& SiteRecord,
	const ULayoutWorldBindingAsset* const ExplicitWorldBindingOverride)
{
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	if (Store == nullptr || RootRecordKey.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG-layout-endpoint] Publish skipped root=%s store=%s."),
			*RootRecordKey, Store != nullptr ? TEXT("valid") : TEXT("null"));
		return;
	}

	if (!SiteRecord.GetResolvedSiteRuntimeState().bLayoutSolved)
	{
		// Replacing a proved root with debug geometry also invalidates its old ports.
		Store->RemoveContinuationEndpointRecordsForRoot(RootRecordKey);
		PlacedContinuationRootChunks.Remove(RootRecordKey);
		LoadedContinuationRootKeys.Remove(RootRecordKey);
		if (RootSpacingReservations.Remove(RootRecordKey) > 0) PlanningAreaQueue->NotifyReservationReleased(RootRecordKey);
		RetireInvalidAutomaticContinuations();
		return;
	}
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
		SiteRecord.GetWorldBindingFrontendSelection();
	if (!RootSpacingReservations.Contains(RootRecordKey))
	{
		FLayoutRootSpacingReservation Bounds;
		if (FLayoutRootSpacingReservation::TryBuild(FrontendSelection.WorldBindingId, SiteRecord.SiteCenterBlockWorldPos,
			SiteRecord.SolveResult.FootprintSize, SiteRecord.SolveResult.SharedCellSizeInBlocks, Bounds))
		{
			RootSpacingReservations.Add(RootRecordKey, Bounds);
		}
	}
	// Local project change: cached realization-only roots have no exported endpoint work.
	// Avoid treating their absent optional world binding as a publication failure.
	const TArray<FResolvedLayoutConnectorEndpoint> ExportedEndpoints =
		FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(ReservationKey, SiteRecord);
	if (ExportedEndpoints.IsEmpty())
	{
		return;
	}
	ULayoutWorldBindingAsset* WorldBinding = const_cast<ULayoutWorldBindingAsset*>(ExplicitWorldBindingOverride);
	const FName ExplicitBindingId = WorldBinding != nullptr && !WorldBinding->BindingId.IsNone()
		? WorldBinding->BindingId
		: WorldBinding != nullptr ? WorldBinding->GetFName() : NAME_None;
	if (WorldBinding != nullptr && ExplicitBindingId != FrontendSelection.WorldBindingId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG-layout-endpoint] Publish skipped root=%s: explicit binding id=%s mismatches solved binding id=%s."),
			*RootRecordKey, *ExplicitBindingId.ToString(), *FrontendSelection.WorldBindingId.ToString());
		return;
	}
	if (WorldBinding == nullptr)
	{
		for (ULayoutWorldBindingAsset* const Candidate : GetLayoutWorldBindings())
		{
			// Runtime views use the asset name when no explicit binding ID is authored.
			if (Candidate != nullptr
				&& (Candidate->BindingId.IsNone() ? Candidate->GetFName() : Candidate->BindingId) == FrontendSelection.WorldBindingId)
			{
				WorldBinding = Candidate;
				break;
			}
		}
	}
	if (WorldBinding == nullptr)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG-layout-endpoint] Publish skipped root=%s bindingId=%s: runtime binding missing."),
			*RootRecordKey, *FrontendSelection.WorldBindingId.ToString());
		return;
	}

	if (SiteRecord.GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld)
	{
		PlacedContinuationRootChunks.Add(RootRecordKey, CollectRequiredChunkOrigins(SiteRecord));
		RefreshPlacedContinuationRootReadiness();
	}
	const FResolvedLayoutSiteSolvedPayload SolvedPayload = SiteRecord.GetResolvedSiteSolvedPayload();
	int32 PublishedRecordCount = 0;
	for (FResolvedLayoutConnectorEndpoint Endpoint : ExportedEndpoints)
	{
		Endpoint.RootRecordKey = RootRecordKey;
		for (const FLayoutWorldBindingContinuationFamily& Family : WorldBinding->ContinuationFamilies)
		{
			if (Family.MaxConnectionsPerSite <= 0
				|| Family.EndpointConnectorTypeTag != Endpoint.ConnectorTypeTag)
			{
				continue;
			}
			FLayoutPlanningWindowEndpointRecord EndpointRecord;
			EndpointRecord.RootRecordKey = RootRecordKey;
			EndpointRecord.WorldBindingId = FrontendSelection.WorldBindingId;
			EndpointRecord.Endpoint = Endpoint;
			EndpointRecord.ContinuationFamilyId = Family.FamilyId;
			EndpointRecord.MaxConnectionsPerSite = Family.MaxConnectionsPerSite;
			EndpointRecord.RemainingConnections = 1;
			EndpointRecord.State = ELayoutContinuationEndpointState::Ready;
			EndpointRecord.StableEndpointKey = FString::Printf(
				TEXT("%s/%s/%s/%s"), *RootRecordKey,
				*Endpoint.LocalCell.ToString(), *Endpoint.ConnectorTypeTag.ToString(),
				*Family.FamilyId.ToString());
			FLayoutPlanningWindowEndpointRecord ExistingRecord;
			if (Store->UpsertContinuationEndpointRecord(EndpointRecord, ExistingRecord))
			{
				++PublishedRecordCount;
			}
		}
	}
	if (GetDetailedDiagnostics())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[DEBUG-layout-endpoint] Publish root=%s reservation=%s binding=%s solved=%d exportedCells=%d exportedTags=%d collectedEndpoints=%d published=%d ledger=%d."),
			*RootRecordKey,
			*ReservationKey.ToString(),
			*FrontendSelection.WorldBindingId.ToString(),
			SiteRecord.GetResolvedSiteRuntimeState().bLayoutSolved ? 1 : 0,
			SolvedPayload.ExportedEntryCells.Num(),
			SolvedPayload.ExportedConnectorTypeTags.Num(),
			ExportedEndpoints.Num(),
			PublishedRecordCount,
			Store->GetContinuationEndpointRecords().Num());
	}
}

int32 UChunkWorldLayoutRuntimeComponent::ImportAcceptedPlannedLayoutSiteRecordsForRealization()
{
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	if (Store == nullptr)
	{
		return 0;
	}

	int32 ImportedCount = 0;
	TArray<FPlannedLayoutSiteRecord> AcceptedRecords = Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted);
	AcceptedRecords.Sort([](const FPlannedLayoutSiteRecord& Left, const FPlannedLayoutSiteRecord& Right)
	{
		const FLayoutPlannedSiteLifecycleMetadata LeftLifecycle = Left.GetPlannedSiteLifecycleMetadata();
		const FLayoutPlannedSiteLifecycleMetadata RightLifecycle = Right.GetPlannedSiteLifecycleMetadata();
		return LeftLifecycle.StableRecordKey < RightLifecycle.StableRecordKey;
	});
	for (const FPlannedLayoutSiteRecord& PlannedRecord : AcceptedRecords)
	{
		const FLayoutPlannedSiteAcceptedSolvePayload AcceptedSolvePayload =
			PlannedRecord.GetPlannedSiteAcceptedSolvePayload();
		if (!AcceptedSolvePayload.SolveResult.bSucceeded)
		{
			continue;
		}

		const FLayoutPlannedSiteLifecycleMetadata PlannedLifecycleMetadata =
			PlannedRecord.GetPlannedSiteLifecycleMetadata();
		FString RealizationInputFailureReason;
		if (!LayoutRealizationWritePlan::ValidateAcceptedSolvePayloadForRealizationInputs(
				AcceptedSolvePayload,
				RealizationInputFailureReason))
		{
			RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				PlannedLifecycleMetadata.StableRecordKey,
				RealizationInputFailureReason);
			ReportLayoutPlanningWarning(
				this,
				true,
				FString::Printf(
					TEXT("Accepted planned site '%s' failed solved-artifact realization input validation: %s"),
					*PlannedLifecycleMetadata.StableRecordKey,
					*RealizationInputFailureReason));
			continue;
		}
		const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
			PlannedRecord.GetPlannedSiteReservationSourceSelection();
		const FLayoutWorldBindingSiteFrontendSelection PlannedFrontendSelection =
			PlannedRecord.GetWorldBindingFrontendSelection();
		const FLayoutRootPublicationMetadata ResolvedPublicationMetadata =
			PlannedRecord.GetRootPublicationMetadata();
		if (!HasCompleteRuntimeRootPublicationMetadata(ResolvedPublicationMetadata))
		{
			RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				PlannedLifecycleMetadata.StableRecordKey,
				TEXT("Accepted planned site is missing authored root publication metadata."));
			ReportLayoutPlanningWarning(
				this,
				true,
				FString::Printf(
					TEXT("Accepted planned site '%s' is missing authored root publication metadata. RootPlacementPolicyId=%s RootCandidateId=%s RootSolveId=%s."),
					*PlannedLifecycleMetadata.StableRecordKey,
					*ResolvedPublicationMetadata.RootPlacementPolicyId.ToString(),
					*ResolvedPublicationMetadata.RootCandidateId.ToString(),
					*ResolvedPublicationMetadata.RootSolveId.ToString()));
			continue;
		}
		const FIntPoint ReservationKey = ReservationSourceSelection.ReservationKey;

		const FLayoutSiteSolveSourceSelection PlannedSiteSolveSourceSelection =
			PlannedRecord.GetSiteSolveSourceSelection();
		ULayoutProfileAsset* const LoadedProfile =
			PlannedSiteSolveSourceSelection.LayoutProfile.LoadSynchronous();
		ULayoutRegionContentSetAsset* const LoadedContentSet =
			PlannedSiteSolveSourceSelection.ContentSet.LoadSynchronous();
		const bool bWorldBindingPlannedRecord =
			HasWorldBindingFrontendSelection(PlannedFrontendSelection);
		const bool bHasRequiredAuthoredContentSource =
			bWorldBindingPlannedRecord
				? LoadedContentSet != nullptr
				: LoadedContentSet != nullptr;
		if (LoadedProfile == nullptr || !bHasRequiredAuthoredContentSource)
		{
			const TCHAR* const RejectionReason = bWorldBindingPlannedRecord
				? TEXT("Accepted planned world-binding site could not load its layout profile or one profile-owned content set.")
				: TEXT("Accepted planned site could not load its layout profile or any authored content source.");
			RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				PlannedLifecycleMetadata.StableRecordKey,
				RejectionReason);
			const FString WarningMessage = bWorldBindingPlannedRecord
				? FString::Printf(
					TEXT("Accepted planned world-binding site '%s' could not load its layout profile or one profile-owned content set. Profile=%s ContentSet=%s."),
					*PlannedLifecycleMetadata.StableRecordKey,
					*PlannedSiteSolveSourceSelection.LayoutProfile.ToString(),
					*PlannedSiteSolveSourceSelection.ContentSet.ToString())
				: FString::Printf(
					TEXT("Accepted planned site '%s' could not load its layout profile or any authored content source. Profile=%s ContentSet=%s."),
					*PlannedLifecycleMetadata.StableRecordKey,
					*PlannedSiteSolveSourceSelection.LayoutProfile.ToString(),
					*PlannedSiteSolveSourceSelection.ContentSet.ToString());
			ReportLayoutPlanningWarning(
				this,
				true,
				WarningMessage);
			continue;
		}

		FResolvedLayoutSiteLocationMetadata LocationMetadata;
		LocationMetadata.SiteCenterBlockWorldPos =
			ReservationSourceSelection.SiteCenterBlockWorldPos;
		if (!AcceptedSolvePayload.FrozenTerrainContract.ContractId.IsNone())
		{
			// Accepted terrain contract already owns final physical anchor. Publish it
			// before continuation endpoints so deferred realization cannot shift a root
			// away from an already prepared connector.
			LocationMetadata.RealizedFootprintMinBlockWorldPos =
				AcceptedSolvePayload.FrozenTerrainContract.FootprintMinBlockWorldPos;
		}
		FLayoutSiteSolveSourceSelection ResolvedSolveSourceSelection =
			PlannedSiteSolveSourceSelection;
		if (bWorldBindingPlannedRecord)
		{
		}
		FResolvedLayoutSiteRecord SiteRecord =
			FLayoutSiteReservation::BuildResolvedSiteRecord(
				LocationMetadata,
				ResolvedSolveSourceSelection,
				&PlannedFrontendSelection,
				ResolvedPublicationMetadata,
				&AcceptedSolvePayload.SolveResult);
		SiteRecord.SolvedArtifactId = AcceptedSolvePayload.SolvedArtifactId;
		SiteRecord.SolvedArtifactActiveCellCount = AcceptedSolvePayload.SolvedArtifactActiveCellCount;
		SetResolvedSiteTerrainFitDiagnostic(
			SiteRecord,
			AcceptedSolvePayload.FrozenTerrainContract.ContractId.IsNone()
				? PlannedLifecycleMetadata.TerrainFitDiagnosticKind
				: AcceptedSolvePayload.FrozenTerrainContract.DiagnosticKind);
		const FString SiteRecordKey = MakeResolvedSiteRecordKey(SiteRecord);
		if (const FResolvedLayoutSiteRecord* const ExistingSiteRecord = ResolvedSiteRecords.Find(SiteRecordKey);
			ExistingSiteRecord != nullptr
			&& ExistingSiteRecord->GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld)
		{
			continue;
		}
		ResolvedSiteRecords.Add(SiteRecordKey, SiteRecord);
		PlanningRecordKeysBySiteRecordKey.Add(
			SiteRecordKey,
			PlannedLifecycleMetadata.StableRecordKey);

		PublishResolvedRootContinuationEndpoints(
			PlannedLifecycleMetadata.StableRecordKey,
			ReservationKey,
			SiteRecord);
		++ImportedCount;
	}

	return ImportedCount;
}

int32 UChunkWorldLayoutRuntimeComponent::AdmitPlanningSitesFromWorldBinding(
	const ULayoutWorldBindingAsset* WorldBinding,
	const LayoutWorldBindingSitePlanner::FSitePlanningSnapshot& Inputs,
	const TArray<FPlannedLayoutSiteRecord>& PendingRecords,
	TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> NoiseSnapshot)
{
	check(IsInGameThread());
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	const FName MatchingBiomeRowName = Inputs.MatchingBiomeRowName;
	const auto& CoordinateSettings = Inputs.CoordinateSettings;
	const auto& FiniteAxisBounds = Inputs.FiniteAxisBounds;

	int32 AcceptedSiteCount = 0, CoverageWaitingCount = 0;
	for (const FPlannedLayoutSiteRecord& PendingRecord : PendingRecords)
	{
		const FLayoutWorldBindingSiteFrontendSelection PendingFrontendSelection =
			PendingRecord.GetWorldBindingFrontendSelection();
		const FLayoutPlannedSiteReservationSourceSelection PendingReservationSourceSelection =
			PendingRecord.GetPlannedSiteReservationSourceSelection();
		if (!WorldBinding || !LayoutWorldBindingSitePlanner::PassesOccupancy(Inputs.WorldSeed, Inputs.BindingId,
			PendingReservationSourceSelection.SiteCenterBlockWorldPos, WorldBinding->OccupancyProbability)) continue;
		// Snapping and cross-boundary pockets may move a center outside the sampled core.
		// Native Created coverage, not character distance, owns automatic eligibility.
		if (!IsAutomaticPlanningPositionEligible(PendingReservationSourceSelection.SiteCenterBlockWorldPos))
		{
			++CoverageWaitingCount;
			continue;
		}
		const FLayoutSiteSolveSourceSelection PendingSolveSourceSelection =
			PendingRecord.GetSiteSolveSourceSelection();
		const FString PendingRecordKey = ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
			PendingFrontendSelection,
			PendingReservationSourceSelection);
		if (!PendingRecordKey.IsEmpty()
			&& PendingPlanningWindowSolveHandlesByRecordKey.Contains(PendingRecordKey))
		{
			if (GetDetailedDiagnostics())
			{
				UE_LOG(
					LogTemp,
					Log,
					TEXT("Layout planning world binding '%s' skipped active planned record before pre-submit snapshot at %s key=(%d,%d)."),
					*PendingFrontendSelection.WorldBindingId.ToString(),
					*PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString(),
					PendingReservationSourceSelection.ReservationKey.X,
					PendingReservationSourceSelection.ReservationKey.Y);
			}
			continue;
		}
		if (ActivePlanningArea.IsSet() && PlanningAreaQueue->HasFailed(ActivePlanningArea.GetValue(), PendingRecordKey)) continue;
		FPlannedLayoutSiteRecord ExistingRecord;
		bool bRetryExistingRecord = false;
		int32 FrozenSubmissionAttemptIndex = 0;
		if (!PendingRecordKey.IsEmpty()
			&& Store->TryGetPlannedLayoutSiteRecord(PendingRecordKey, ExistingRecord))
		{
			const FLayoutPlannedSiteLifecycleMetadata ExistingLifecycleMetadata =
				ExistingRecord.GetPlannedSiteLifecycleMetadata();
			const bool bCanRetryExistingRecord = ExistingLifecycleMetadata.State == EPlannedLayoutSiteState::Rejected
				|| ExistingLifecycleMetadata.FrozenSubmissionState == ELayoutPlannedSiteFrozenSubmissionState::Tombstoned
				|| ExistingLifecycleMetadata.FrozenSubmissionState == ELayoutPlannedSiteFrozenSubmissionState::Rejected;
			if (!bCanRetryExistingRecord)
			{
				if (GetDetailedDiagnostics())
				{
					UE_LOG(
						LogTemp,
						Log,
						TEXT("Layout planning world binding '%s' skipped existing planned record before pre-submit snapshot at %s key=(%d,%d) state=%d frozenState=%d."),
						*PendingFrontendSelection.WorldBindingId.ToString(),
						*PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString(),
						PendingReservationSourceSelection.ReservationKey.X,
						PendingReservationSourceSelection.ReservationKey.Y,
						static_cast<int32>(ExistingLifecycleMetadata.State),
						static_cast<int32>(ExistingLifecycleMetadata.FrozenSubmissionState));
				}
				continue;
			}
			FrozenSubmissionAttemptIndex = ExistingLifecycleMetadata.FrozenSubmissionAttemptIndex + 1;
			if (!Store->ResetPlannedLayoutSiteRecordForFrozenSubmissionRetry(PendingRecordKey)
				|| !Store->TryGetPlannedLayoutSiteRecord(PendingRecordKey, ExistingRecord))
			{
				continue;
			}
			bRetryExistingRecord = true;
		}

		FLayoutWorldBindingRuntimeView ResolvedBindingView;
		FString BindingViewFailureReason;
		if (!LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromPlannedSiteRecord(
			WorldBinding,
			PendingRecord,
			ResolvedBindingView,
			BindingViewFailureReason))
		{
			FPlannedLayoutSiteRecord StoredRecord;
			if (Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, StoredRecord))
			{
				const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
					StoredRecord.GetPlannedSiteLifecycleMetadata();
				RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
					StoredLifecycleMetadata.StableRecordKey,
					BindingViewFailureReason.IsEmpty()
						? TEXT("Planning-window runtime view could not be rebuilt from the world-binding record.")
						: BindingViewFailureReason);
			}
			continue;
		}

		// Reserve the authored maximum envelope before dispatch; final footprints may be smaller.
		FLayoutRootSpacingReservation SpacingBounds;
		if (ResolvedBindingView.LayoutProfile == nullptr
			|| !FLayoutRootSpacingReservation::TryBuild(PendingFrontendSelection.WorldBindingId,
				PendingReservationSourceSelection.SiteCenterBlockWorldPos, ResolvedBindingView.LayoutProfile->MaximumFootprintInCells,
				ResolvedBindingView.SharedCellSizeInBlocks, SpacingBounds)
			|| !CanPlanOrdinaryRootSiteForBinding(WorldBinding, SpacingBounds, PendingRecordKey))
		{
			if (ActivePlanningArea.IsSet())
				for (const auto& Pair : RootSpacingReservations)
					if (Pair.Key != PendingRecordKey && !SpacingBounds.IsSeparatedFrom(Pair.Value,
						WorldBinding->MinimumRootGapCells, WorldBinding->BaseCellDimensionsBlocks))
						PlanningAreaQueue->MarkBlockedByReservation(ActivePlanningArea.GetValue(), Pair.Key);
			continue;
		}

		// Reject sites that overflow the finite Z ceiling when the chunk world
		// has a bounded Z axis (e.g., centered-Z terrain with an explicit max block).
		if (FiniteAxisBounds.bHasFiniteZ)
		{
			const FIntVector SharedCellSize = ResolvedBindingView.SharedCellSizeInBlocks;
			const int32 SiteHeightBlocks = FMath::Max(1, ResolvedBindingView.LayoutProfile
				? ResolvedBindingView.LayoutProfile->LevelCount : 1) * FMath::Max(1, SharedCellSize.Z);
			const int32 SiteMaxZ = PendingReservationSourceSelection.SiteCenterBlockWorldPos.Z
				+ FMath::Max(0, SiteHeightBlocks / 2);
			if (SiteMaxZ > FiniteAxisBounds.MaxInclusive.Z)
			{
				UE_LOG(LogTemp, Verbose, TEXT("Layout planning binding '%s' rejected site at %s because siteMaxZ=%d exceeds finite Z ceiling=%d."),
					*PendingFrontendSelection.WorldBindingId.ToString(),
					*PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString(),
					SiteMaxZ, FiniteAxisBounds.MaxInclusive.Z);
				continue;
			}
		}

		if (ActivePlanningArea.IsSet())
		{
			if (CountAutomaticPlanningWork() >= FMath::Max(1, MaxCachedPlanningChunks)
				|| PlanningAreaAdmissions >= 1 || (CompletingPlanningAreaGroupId == 0 && BackgroundSolveDispatcher.IsValid()
				&& BackgroundSolveDispatcher->GetDiagnosticsSnapshot().InProgressLayoutGroups >= BuildBackgroundSolveSettings().ResolveMaxConcurrentBackgroundLayoutSolves())
				|| !PlanningAreaQueue->BeginAttempt(ActivePlanningArea.GetValue(), PendingRecordKey))
			{
				bPlanningAreaDeferred = true;
				break;
			}
			PlanningAreasByRecordKey.Add(PendingRecordKey, ActivePlanningArea.GetValue());
			++PlanningAreaAdmissions;
		}
		RootSpacingReservations.Add(PendingRecordKey, SpacingBounds);
		bool bSubmittedToLifecycle = false;
		ON_SCOPE_EXIT
		{
			if (!bSubmittedToLifecycle)
			{
				FinishPlanningAreaAttempt(PendingRecordKey, false, true);
				const auto* Handle = PendingPlanningWindowSolveHandlesByRecordKey.Find(PendingRecordKey);
				if (Handle && (!BackgroundSolveDispatcher || !BackgroundSolveDispatcher->HasRetainedGroup(Handle->LayoutGroupId)))
					PendingPlanningWindowSolveHandlesByRecordKey.Remove(PendingRecordKey);
			}
		};

		const uint64 RootLayoutGroupId = static_cast<uint64>(GetTypeHash(PendingReservationSourceSelection.SiteCenterBlockWorldPos))
			^ (static_cast<uint64>(static_cast<uint32>(PendingSolveSourceSelection.SolveSeed)) << 32);
		const bool bHasExplicitRootScoutCarrier = !PendingRecord.DiscoveryCandidateId.IsNone();
		const FLayoutId MissingRootDescriptorId = FLayoutId(*FString::Printf(
			TEXT("PlanningRootPrewarm.MissingScout.%s.%d"),
			*PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString(),
			FrozenSubmissionAttemptIndex));
		// Local project cutover: root descriptor publication must come from explicit scout-result identity.
		// The generic snapshot artifact path is intentionally quarantined from runtime submit to avoid duplicate authority.
		if (!bHasExplicitRootScoutCarrier)
		{
			FPlannedLayoutSiteRecord StoredRecord;
			if (Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, StoredRecord))
			{
				const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
					StoredRecord.GetPlannedSiteLifecycleMetadata();
				RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
					StoredLifecycleMetadata.StableRecordKey,
					TEXT("Planning-window root descriptor rejected missing explicit scout-result identity before prewarm snapshot production."),
					MissingRootDescriptorId);
			}
			else
			{
				TombstonePlanningRootFrozenSubmissionDescriptor(PendingRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey, MissingRootDescriptorId);
			}
			continue;
		}
		// Transport identity is per submission, not the candidate label: another site or a generation
		// restart can reuse both the label and attempt zero while old completions still exist.
		const FLayoutId RootDescriptorId = FLayoutId(*FString::Printf(
			TEXT("PlanningRootPrewarm.%s.%d.%s"),
			*PendingRecord.DiscoveryCandidateId.ToString(),
			FrozenSubmissionAttemptIndex, *FGuid::NewGuid().ToString(EGuidFormats::Digits)));

		// Transitional prewarm descriptor producer: builds the pre-submit snapshot from the resolved
		// runtime binding view and wraps it through the region prewarm producer facade. This keeps the
		// consumer path real (descriptor-id lookup, fail-closed on stale) while authoritative scout/prewarm
		// producers are still being built in upstream batches.
		const int32 PlanningWorldSeed = ResolveLayoutWorldSeed();
		const int32 FinalizedSolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
			PendingReservationSourceSelection.SiteCenterBlockWorldPos,
			PlanningWorldSeed);

		const FString DiagnosticContext = GetDetailedDiagnostics()
			? FString::Printf(TEXT("origin=automatic descriptor=%s profile=%s site=(%s) seed=%d attempt=%d"),
				*RootDescriptorId.ToString(), *GetPathNameSafe(ResolvedBindingView.LayoutProfile),
				*PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString(), FinalizedSolveSeed, FrozenSubmissionAttemptIndex)
			: FString();
		FLayoutBackgroundSolveHandle OwnedRoot;
		OwnedRoot.LayoutGroupId = RootLayoutGroupId;
		PendingPlanningWindowSolveHandlesByRecordKey.Add(PendingRecordKey, OwnedRoot);
		PendingPlanningWindowSolveCentersByRecordKey.Add(PendingRecordKey, PendingReservationSourceSelection.SiteCenterBlockWorldPos);
		LayoutSolveExecution::FDiagnosticScope CaptureDiagnostics(DiagnosticContext, TEXT("site-capture"));
		const auto PreparedInputs = MakeShared<FLayoutFrozenSubmissionDescriptorSeed, ESPMode::ThreadSafe>();
		PreparedInputs->DescriptorId = RootDescriptorId;
		PreparedInputs->RegionGroupId = RootLayoutGroupId;
		PreparedInputs->Generation = RootLayoutGroupId;
		PreparedInputs->AttemptIndex = FrozenSubmissionAttemptIndex;
		FLayoutPreSubmitFrozenSnapshot& RootPreSubmitSnapshot = PreparedInputs->Snapshot;
		FLayoutBackgroundSolveSubmission Preparation;
		Preparation.DebugName = FString::Printf(TEXT("PlanningWindowPrepare.%s"), *RootDescriptorId.ToString());
		Preparation.LayoutGroupId = RootLayoutGroupId;
		Preparation.Tier = ELayoutBackgroundSolveJobTier::NearRoot;
		Preparation.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Preparation;
		Preparation.Priority = FLayoutPlanningAreaQueue::ComputePriority(
			PendingReservationSourceSelection.SiteCenterBlockWorldPos, PlanningPriorityCenters);
		{
			FLayoutWorkerSolvePacket WorkerPacket = FLayoutWorkerSolvePacket::CapturePlanningRoot(
				FString::Printf(TEXT("PlanningRoot %s"), *PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString()),
				ResolvedBindingView,
				PendingRecord,
				FrozenSubmissionAttemptIndex);

			WorkerPacket.SolveSeed = FinalizedSolveSeed;
			WorkerPacket.bHasSelectedModePlan = true;
			WorkerPacket.SelectedModePlan.Scope = ELayoutContractRegionScope::Root;
			WorkerPacket.SelectedModePlan.SiteCenterBlockWorldPos = PendingReservationSourceSelection.SiteCenterBlockWorldPos;
			WorkerPacket.SelectedModePlan.WorldSeed = ResolveLayoutWorldSeed();
			WorkerPacket.SelectedModePlan.SolveSeed = FinalizedSolveSeed;
			WorkerPacket.SelectedModePlan.PlacementKind = ResolvedBindingView.PlacementKind;
			WorkerPacket.SelectedModePlan.EnvironmentMode =
				(ResolvedBindingView.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
					&& ResolvedBindingView.LayoutProfile
					&& ResolvedBindingView.LayoutProfile->bUndergroundPlacement)
				? ELayoutContractEnvironmentMode::UndergroundPocketPlacement
				: (ResolvedBindingView.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
					&& ResolvedBindingView.LayoutProfile
					&& ResolvedBindingView.LayoutProfile->bSupportsSteppedTerrainSolve)
				? ELayoutContractEnvironmentMode::SteppedSurfacePlacement
				: ResolvedBindingView.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
					? ELayoutContractEnvironmentMode::NonSteppedWorldPlacement
					: ELayoutContractEnvironmentMode::StandardRegion;
			WorkerPacket.SelectedModePlan.bUsesSteppedTerrainTopology =
				ResolvedBindingView.LayoutProfile != nullptr
				&& ResolvedBindingView.LayoutProfile->bSupportsSteppedTerrainSolve
				&& (ResolvedBindingView.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
					|| ResolvedBindingView.PlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath
					|| ResolvedBindingView.PlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation);
			// Match the finalizer's SelectModePlan field assignments exactly for hash determinism.
			WorkerPacket.SelectedModePlan.PlacementPolicy = ResolvedBindingView.PlacementPolicy;
			WorkerPacket.SelectedModePlan.PlacementShiftCells = FIntVector::ZeroValue;
			WorkerPacket.SelectedModePlan.PlacementShiftId = FLayoutContractPipeline::BuildPlacementShiftId(FIntVector::ZeroValue);
			WorkerPacket.SelectedModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(WorkerPacket.SelectedModePlan);

			// Build a thin solve request from the runtime view + profile + content
			// set only. Planned cells, stepped support, and terrain adapter evidence
			// are produced by the prewarm background job.
			FLayoutRegionSolveRequest StandaloneRequest;
			FString BuilderFailureReason;
			const bool bRequestBuilt =
				LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequest(
					ResolvedBindingView,
					PendingRecord,
					StandaloneRequest,
					BuilderFailureReason);
			if (!bRequestBuilt)
			{
				FPlannedLayoutSiteRecord StoredRecord;
				if (Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, StoredRecord))
				{
					RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
						StoredRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey,
						BuilderFailureReason.IsEmpty()
							? TEXT("Planning-window root request could not be built.")
							: BuilderFailureReason,
						RootDescriptorId);
				}
				else
				{
					TombstonePlanningRootFrozenSubmissionDescriptor(
						PendingRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey,
						RootDescriptorId);
				}
				continue;
			}
			WorkerPacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(StandaloneRequest);
			if (!DiagnosticContext.IsEmpty())
			{
				UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s event=request footprint=%s cellSize=%s mode=%d maxSeconds=%.3f maxWork=%d"),
					*DiagnosticContext, *StandaloneRequest.FootprintSize.ToString(), *ResolvedBindingView.SharedCellSizeInBlocks.ToString(),
					static_cast<int32>(WorkerPacket.SelectedModePlan.EnvironmentMode),
					StandaloneRequest.ExecutionSettings.MaxSolveDurationSeconds, StandaloneRequest.ExecutionSettings.MaxCandidateAttempts);
			}

			// Overlay transitional prewarm fields onto the captured manifest.
			WorkerPacket.RequestManifest.CapturedSeed = FinalizedSolveSeed;
			WorkerPacket.RequestManifest.bHasSelectedModePlan = true;
			WorkerPacket.RequestManifest.SelectedModePlan = WorkerPacket.SelectedModePlan;
			WorkerPacket.RequestManifest.bHasRootPlacementSubmission = true;
			WorkerPacket.RequestManifest.RootSiteCenterBlockWorldPos = PendingReservationSourceSelection.SiteCenterBlockWorldPos;
			WorkerPacket.RequestManifest.RootReservationKey = PendingReservationSourceSelection.ReservationKey;
			WorkerPacket.RequestManifest.CapturedRootPlacementKind = WorkerPacket.SelectedModePlan.PlacementKind;
			WorkerPacket.bHasRequestManifest = true;

			// Planned cells, entries, and adapter output are produced by the prewarm
			// background job.  The terrain-evidence block below provides optional
			// elevation-aware input for terrain-mode placement kinds.

			// Only copied policy/profile scalars and owned noise cross the worker boundary.
			Preparation.Work = [PreparedInputs, NoiseSnapshot, CoordinateSettings, PendingRecord,
				MatchingBiomeRowName, RootDescriptorId, DiagnosticContext,
				SharedCellSizeInBlocks = ResolvedBindingView.SharedCellSizeInBlocks,
				PlacementPolicy = ResolvedBindingView.PlacementPolicy,
				ProfileLevelCount = ResolvedBindingView.LayoutProfile->LevelCount,
				bSupportsSteppedTerrainSolve = ResolvedBindingView.LayoutProfile->bSupportsSteppedTerrainSolve,
				bDetailedDiagnostics = GetDetailedDiagnostics()](const FLayoutSolveCancellationToken& CancellationToken, FString& OutFailureReason)
			{
				LayoutSolveExecution::FDiagnosticScope PreparationDiagnostics(DiagnosticContext, TEXT("site-preparation"), &OutFailureReason);
				bool bPreparationSucceeded = false;
				ON_SCOPE_EXIT { PreparationDiagnostics.Finish(bPreparationSucceeded); };
				if (CancellationToken.IsCancellationRequested())
				{
					OutFailureReason = TEXT("Layout preparation was canceled.");
					return false;
				}
				FLayoutWorkerSolvePacket& WorkerPacket = PreparedInputs->Snapshot.WorkerSolvePacket;
				const FLayoutActiveBiomeSampler& ActiveBiomeSampler = NoiseSnapshot->GetSampler();
				const auto PendingReservationSourceSelection = PendingRecord.GetPlannedSiteReservationSourceSelection();
				if (!WorkerPacket.RequestManifest.bHasFrozenTerrainBiomeAdapterInput)
				{
					const FLayoutId TerrainArtifactId = FLayoutId(*FString::Printf(TEXT("TerrainBiome.%s"), *RootDescriptorId.ToString()));
					const FIntVector FootprintMinBlockWorldPos = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
						PendingReservationSourceSelection.SiteCenterBlockWorldPos,
						WorkerPacket.RequestManifest.FootprintSize,
						SharedCellSizeInBlocks);
					FLayoutFrozenTerrainBiomeAdapterInput TerrainAdapterInput = BuildTerrainBiomeAdapterInput(
						TerrainArtifactId,
						WorkerPacket.SelectedModePlan.ModePlanId,
						MatchingBiomeRowName,
						PendingReservationSourceSelection.SiteCenterBlockWorldPos,
						FootprintMinBlockWorldPos,
						SharedCellSizeInBlocks,
						WorkerPacket.RequestManifest.FootprintSize,
						[&ActiveBiomeSampler,
							&CoordinateSettings,
							SiteBaseZ = PendingReservationSourceSelection.SiteCenterBlockWorldPos.Z,
							CellH = SharedCellSizeInBlocks.Z,
							bUseSelectedMaterialPacket = WorkerPacket.RequestManifest.SelectedModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot](int32 BlockX, int32 BlockY) -> int32
						{
							if (bUseSelectedMaterialPacket)
							{
								return SiteBaseZ;
							}
							FLayoutActiveBiomeSurfaceSample BioSample;
							const bool bFound = ActiveBiomeSampler.FindAnyActiveBiomeSurface(
								FIntPoint(BlockX, BlockY),
								SiteBaseZ + CellH,
								CellH * 4,
								CoordinateSettings,
								BioSample);
							return bFound ? BioSample.SurfaceBlockWorldPos.Z : SiteBaseZ;
						},
						PlacementPolicy.TerrainTransition.MaxFoundationDepth);
					WorkerPacket.RequestManifest.bHasFrozenTerrainBiomeAdapterInput = true;
					WorkerPacket.RequestManifest.FrozenTerrainBiomeAdapterInput = MoveTemp(TerrainAdapterInput);

					// Every ordinary root uses the same selected-site packet and classification producer.
					if (WorkerPacket.RequestManifest.SelectedModePlan.PlacementKind
						== ELayoutWorldBindingPlacementKind::OrdinaryRoot)
					{
						FString SelectedSiteFailureReason;
						const bool bPrepared = TryPrepareOrdinaryRootSelectedSiteTerrain(
							bDetailedDiagnostics,
							PendingReservationSourceSelection.SiteCenterBlockWorldPos,
							FootprintMinBlockWorldPos,
							WorkerPacket.RequestManifest.FootprintSize,
							SharedCellSizeInBlocks,
							ProfileLevelCount,
							PlacementPolicy,
							&ActiveBiomeSampler,
							CoordinateSettings,
							WorkerPacket.RequestManifest.FrozenTerrainBiomeAdapterInput,
							SelectedSiteFailureReason);
						bool bCenterUnderground = false;
						const bool bHasCenterClassification = TryResolveEnvironmentCenterColumnMode(
							ActiveBiomeSampler,
							CoordinateSettings,
							SharedCellSizeInBlocks,
							PlacementPolicy,
							ProfileLevelCount,
							PendingReservationSourceSelection.SiteCenterBlockWorldPos,
							bCenterUnderground);
						FLayoutFrozenTerrainBiomeAdapterInput& SelectedArtifact =
							WorkerPacket.RequestManifest.FrozenTerrainBiomeAdapterInput;
						if (bHasCenterClassification)
						{
							SelectedArtifact.bHasRelativeEnvironmentClassification = true;
							SelectedArtifact.bIsClassifiedUnderground = bCenterUnderground;
						}
						else if (PendingRecord.bHasDiscoveredEnvironmentMode
							&& !PendingRecord.bEnvironmentDiscoveryQualifiedProceduralOccupancy)
						{
							SelectedArtifact.bHasRelativeEnvironmentClassification = true;
							SelectedArtifact.bIsClassifiedUnderground = PendingRecord.bDiscoveredUnderground;
						}
						FString EnvironmentFailure;
						if (!LayoutWorldBindingSitePlanner::ValidateDiscoveredEnvironmentEvidence(
								PendingRecord,
								bPrepared,
								SelectedArtifact,
								EnvironmentFailure))
						{
							OutFailureReason = !SelectedSiteFailureReason.IsEmpty() ? SelectedSiteFailureReason : EnvironmentFailure;
							return false;
						}
					}

					if (bSupportsSteppedTerrainSolve)
					{
						FLayoutFrozenTerrainBiomeAdapterInput& TerrainArtifact =
							WorkerPacket.RequestManifest.FrozenTerrainBiomeAdapterInput;
						FString SteppedSupportFailureReason;
						if (!FLayoutTerrainSampling::TryAugmentSelectedComponentWithSteppedTerrainEvidence(
								TerrainArtifact.bIsClassifiedUnderground,
								FootprintMinBlockWorldPos,
								SharedCellSizeInBlocks,
								WorkerPacket.RequestManifest.FootprintSize,
								PlacementPolicy.TerrainTransition,
								WorkerPacket.RequestManifest.SteppedTerrainSupportMap,
								TerrainArtifact,
								SteppedSupportFailureReason))
						{
							TerrainArtifact.AuditMessages.Add(FString::Printf(
								TEXT("SelectedSiteFlatFallback: natural stepped support was incomplete: %s"),
								*SteppedSupportFailureReason));
						}
					}
					FLayoutTerrainSampling::ApplySelectedComponentPerimeterRampEvidence(
						FootprintMinBlockWorldPos,
						SharedCellSizeInBlocks,
						WorkerPacket.RequestManifest.FootprintSize,
						PlacementPolicy.TerrainTransition,
						WorkerPacket.RequestManifest.FrozenTerrainBiomeAdapterInput);
				}

				if (CancellationToken.IsCancellationRequested())
				{
					OutFailureReason = TEXT("Layout preparation was canceled.");
					return false;
				}
				bPreparationSucceeded = true;
				return true;
			};

			RootPreSubmitSnapshot.SnapshotId = RootDescriptorId;
			RootPreSubmitSnapshot.PrewarmKind = ELayoutManifestPrewarmKind::Root;
			RootPreSubmitSnapshot.bHasWorkerSolvePacket = true;
			RootPreSubmitSnapshot.WorkerSolvePacket = MoveTemp(WorkerPacket);
		}

		// Reserve identity before Submit: synchronous test executors can publish the entire chain inline.
		FPlannedLayoutSiteRecord PreparingRecord = ExistingRecord;
		if (!bRetryExistingRecord && !Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, PreparingRecord))
		{
			TombstonePlanningRootFrozenSubmissionDescriptor(PendingRecordKey, RootDescriptorId);
			continue;
		}
		if (!Store->UpdatePlannedLayoutSiteFrozenSubmissionState(PendingRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::None, RootDescriptorId,
			RootLayoutGroupId, FrozenSubmissionAttemptIndex, 0))
		{
			TombstonePlanningRootFrozenSubmissionDescriptor(PendingRecordKey, RootDescriptorId);
			continue;
		}
		Preparation.PublishOnGameThread = [WeakThis = TWeakObjectPtr<UChunkWorldLayoutRuntimeComponent>(this),
			PreparedInputs, PendingRecord, PendingRecordKey, RootDescriptorId, DiagnosticContext,
			Priority = Preparation.Priority](const FLayoutBackgroundSolveCompletion& Completion)
		{
			UChunkWorldLayoutRuntimeComponent* const Runtime = WeakThis.Get();
			if (Runtime == nullptr) return;
			FPlannedLayoutSiteRecord CurrentRecord;
			if (!Runtime->GetOrCreateLayoutPlanningWindowStore()->TryGetPlannedLayoutSiteRecord(PendingRecordKey, CurrentRecord)) return;
			const auto Metadata = CurrentRecord.GetPlannedSiteLifecycleMetadata();
			if (Metadata.State != EPlannedLayoutSiteState::Pending
				|| Metadata.FrozenSubmissionDescriptorId != RootDescriptorId
				|| Metadata.FrozenSubmissionState != ELayoutPlannedSiteFrozenSubmissionState::None) return;
			if (!Completion.bWorkSucceeded)
			{
				Runtime->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(PendingRecordKey,
					Completion.FailureReason.IsEmpty() ? TEXT("Planning-window root preparation failed.") : Completion.FailureReason,
					RootDescriptorId);
				return;
			}
			Runtime->SubmitPreparedPlanningRoot(PendingRecord, CurrentRecord, true, MoveTemp(*PreparedInputs), DiagnosticContext, Priority);
		};
		CaptureDiagnostics.Finish(true);
		const FLayoutBackgroundSolveHandle PreparationHandle = GetOrCreateBackgroundSolveDispatcher().Submit(MoveTemp(Preparation));
		if (!PreparationHandle.IsValid())
		{
			RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(PendingRecordKey,
				TEXT("Planning-window root preparation could not be admitted."), RootDescriptorId);
			continue;
		}
		bSubmittedToLifecycle = true;
		FPlannedLayoutSiteRecord CurrentRecord;
		if (Store->TryGetPlannedLayoutSiteRecord(PendingRecordKey, CurrentRecord)
			&& CurrentRecord.GetPlannedSiteLifecycleMetadata().State == EPlannedLayoutSiteState::Pending
			&& CurrentRecord.GetPlannedSiteLifecycleMetadata().FrozenSubmissionDescriptorId == RootDescriptorId
			&& CurrentRecord.GetPlannedSiteLifecycleMetadata().FrozenSubmissionState == ELayoutPlannedSiteFrozenSubmissionState::None)
		{
			PendingPlanningWindowSolveHandlesByRecordKey.Add(PendingRecordKey, PreparationHandle);
			PendingPlanningWindowSolveCentersByRecordKey.Add(
				PendingRecordKey, PendingReservationSourceSelection.SiteCenterBlockWorldPos);
		}
		++AcceptedSiteCount;
	}

	if (GetDetailedDiagnostics())
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Layout planning world binding '%s' complete: submittedSites=%d."),
			*(!WorldBinding->BindingId.IsNone() ? WorldBinding->BindingId.ToString() : WorldBinding->GetName()),
			AcceptedSiteCount);
	}

	if (GetDetailedDiagnostics())
		UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] origin=automatic-admission binding=%s submitted=%d coverageWaiting=%d deferred=%d"),
			*Inputs.BindingId.ToString(), AcceptedSiteCount, CoverageWaitingCount, bPlanningAreaDeferred ? 1 : 0);
	return AcceptedSiteCount;
}

bool UChunkWorldLayoutRuntimeComponent::SubmitPreparedPlanningRoot(
	const FPlannedLayoutSiteRecord& PendingRecord,
	const FPlannedLayoutSiteRecord& ExistingRecord,
	const bool bRetryExistingRecord,
	FLayoutFrozenSubmissionDescriptorSeed PreparedInputs,
	FString DiagnosticContext,
	const int32 Priority)
{
	check(IsInGameThread());
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	const auto PendingFrontendSelection = PendingRecord.GetWorldBindingFrontendSelection();
	const auto PendingReservationSourceSelection = PendingRecord.GetPlannedSiteReservationSourceSelection();
	const auto PendingSolveSourceSelection = PendingRecord.GetSiteSolveSourceSelection();
	const FLayoutId RootDescriptorId = PreparedInputs.DescriptorId;
	const uint64 RootLayoutGroupId = PreparedInputs.RegionGroupId;
	const int32 FrozenSubmissionAttemptIndex = PreparedInputs.AttemptIndex;
	const FLayoutPreSubmitFrozenSnapshot& RootPreSubmitSnapshot = PreparedInputs.Snapshot;
	// Build descriptor seed and artifact through the shared producer so prewarm
	// reuses its finalized adapter contract and retains store/tombstone identity.
	FLayoutFrozenSubmissionDescriptorSeed DescriptorSeed;
	FString ProducerFailureReason;
	if (!LayoutFrozenSubmissionDescriptorProducer::BuildPreflightDescriptorSeed(
			RootDescriptorId, RootLayoutGroupId, RootLayoutGroupId, FrozenSubmissionAttemptIndex,
			ELayoutFrozenSubmissionRegionKind::Root, RootPreSubmitSnapshot,
			DescriptorSeed, ProducerFailureReason))
	{
		FPlannedLayoutSiteRecord StoredRecord;
		if (Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, StoredRecord))
		{
			const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
				StoredRecord.GetPlannedSiteLifecycleMetadata();
			RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				FString::Printf(TEXT("Planning-window root prewarm descriptor seed failed: %s"), *ProducerFailureReason),
				RootDescriptorId);
		}
		else
		{
			TombstonePlanningRootFrozenSubmissionDescriptor(
				PendingRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey, RootDescriptorId);
		}
		return false;
	}

	const FLayoutId ArtifactId = FLayoutId(*FString::Printf(TEXT("ProducedDescriptor.%s"), *RootDescriptorId.ToString()));
	FLayoutProducedFrozenDescriptorArtifact RootProducedDescriptorArtifact;
	if (!LayoutFrozenSubmissionDescriptorProducer::BuildProducedDescriptorArtifactFromSeed(
			ArtifactId, DescriptorSeed, RootProducedDescriptorArtifact, ProducerFailureReason))
	{
		FPlannedLayoutSiteRecord StoredRecord;
		if (Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, StoredRecord))
		{
			const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
				StoredRecord.GetPlannedSiteLifecycleMetadata();
			RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				FString::Printf(TEXT("Planning-window root prewarm descriptor artifact failed: %s"), *ProducerFailureReason),
				RootDescriptorId);
		}
		else
		{
			TombstonePlanningRootFrozenSubmissionDescriptor(
				PendingRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey, RootDescriptorId);
		}
		return false;
	}

	// Seal the produced descriptor audit hash on the stored record.
	FPlannedLayoutSiteRecord StoredPendingRecord = ExistingRecord;
	if (RootProducedDescriptorArtifact.DescriptorAuditHash != 0)
	{
		FLayoutPlannedSiteLifecycleMetadata StoredMetadata = StoredPendingRecord.GetPlannedSiteLifecycleMetadata();
		StoredMetadata.FrozenSubmissionAuditHash = static_cast<int32>(RootProducedDescriptorArtifact.DescriptorAuditHash);
		StoredPendingRecord.SetPlannedSiteLifecycleMetadata(StoredMetadata);
	}
	if (!bRetryExistingRecord && !Store->UpsertPendingPlannedLayoutSiteRecord(PendingRecord, StoredPendingRecord))
	{
		TombstonePlanningRootFrozenSubmissionDescriptor(PendingRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey, RootDescriptorId);
		if (GetDetailedDiagnostics())
		{
			const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
				StoredPendingRecord.GetPlannedSiteLifecycleMetadata();
			UE_LOG(
				LogTemp,
				Log,
				TEXT("Layout planning world binding '%s' skipped duplicate/existing planned record at %s key=(%d,%d) state=%d."),
				*PendingFrontendSelection.WorldBindingId.ToString(),
				*PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString(),
				PendingReservationSourceSelection.ReservationKey.X,
				PendingReservationSourceSelection.ReservationKey.Y,
				static_cast<int32>(StoredLifecycleMetadata.State));
		}
		return false;
	}

	const FString StoredPendingRecordKey = StoredPendingRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	// Record attempt identity before prewarm: Submit can publish synchronously, and prewarm can fail
	// before a descriptor payload exists. None still means preflight has not certified the descriptor.
	if (!Store->UpdatePlannedLayoutSiteFrozenSubmissionState(
		StoredPendingRecordKey, ELayoutPlannedSiteFrozenSubmissionState::None,
		RootDescriptorId, DescriptorSeed.Generation, FrozenSubmissionAttemptIndex,
		static_cast<int32>(RootProducedDescriptorArtifact.DescriptorAuditHash)))
	{
		TombstoneFrozenSubmissionDescriptorPayload(RootDescriptorId);
		return false;
	}
	FLayoutWorkerSolvePacket WorkerSolvePacket = RootProducedDescriptorArtifact.DescriptorSeed.Snapshot.WorkerSolvePacket;

	struct FPlanningWindowBackgroundSolveSharedResult
	{
		FLayoutRegionSolveScheduleResult ScheduleResult;
		FLayoutFrozenTerrainContract FrozenTerrainContract;
		FLayoutSolvedArtifact SolvedArtifact;
		// Publish-time metadata captured from the finalized request so the publish
		// callback can build publication metadata without re-finalizing the packet.
		FLayoutId RootSolveId;
		FLayoutId RootCandidateId;
		FLayoutId RootPlacementPolicyId;
		FLayoutResolvedWorldBindingContinuationSelection RootContinuationSelection;
	};
	TSharedRef<FPlanningWindowBackgroundSolveSharedResult, ESPMode::ThreadSafe> SharedResult =
		MakeShared<FPlanningWindowBackgroundSolveSharedResult, ESPMode::ThreadSafe>();
	TSharedRef<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> SharedDescriptorWorkerSolvePacket =
		MakeShared<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe>(WorkerSolvePacket);
	const FPlannedLayoutSiteRecord CapturedPendingRecord = StoredPendingRecord;
	const FLayoutWorldBindingSiteFrontendSelection CapturedFrontendSelection = PendingFrontendSelection;
	const FLayoutPlannedSiteReservationSourceSelection CapturedReservationSelection = PendingReservationSourceSelection;
	const FLayoutSiteSolveSourceSelection CapturedSolveSourceSelection = PendingSolveSourceSelection;
	const FLayoutId CapturedRootDescriptorId = RootDescriptorId;
	const uint64 CapturedRootRegionGroupId = RootLayoutGroupId;
	const FLayoutId CapturedSolvedArtifactId = BuildRuntimeSolvedArtifactId(RootDescriptorId);
	TWeakObjectPtr<UChunkWorldLayoutRuntimeComponent> WeakThis(this);
	FLayoutBackgroundSolveSubmission Submission;
	Submission.DebugName = FString::Printf(TEXT("PlanningRoot %s"), *PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString());
	Submission.LayoutGroupId = RootLayoutGroupId;
	Submission.Tier = ELayoutBackgroundSolveJobTier::NearRoot;
	Submission.Priority = Priority;
	Submission.PublishOnGameThread = [
		WeakThis,
		SharedResult,
		CapturedPendingRecord,
		SharedDescriptorWorkerSolvePacket,
		CapturedFrontendSelection,
		CapturedReservationSelection,
		CapturedSolveSourceSelection,
		CapturedRootDescriptorId,
		CapturedRootRegionGroupId,
		CapturedSolvedArtifactId](const FLayoutBackgroundSolveCompletion& Completion)
	{
		UChunkWorldLayoutRuntimeComponent* const Component = WeakThis.Get();
		if (Component == nullptr)
		{
			return;
		}
		ULayoutPlanningWindowStore* const PublishStore = Component->GetOrCreateLayoutPlanningWindowStore();
		if (PublishStore == nullptr)
		{
			return;
		}

		const FLayoutSolveResult& SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
		ReportLayoutSolveMessages(
			Component->GetDetailedDiagnostics(),
			CapturedFrontendSelection.WorldBindingId,
			CapturedReservationSelection.SiteCenterBlockWorldPos,
			SolveResult);
		const FLayoutPlannedSiteLifecycleMetadata CapturedLifecycleMetadata =
			CapturedPendingRecord.GetPlannedSiteLifecycleMetadata();
		FPlannedLayoutSiteRecord StoredRecord;
		if (!PublishStore->TryGetPlannedLayoutSiteRecord(CapturedLifecycleMetadata.StableRecordKey, StoredRecord))
		{
			Component->TombstonePlanningRootFrozenSubmissionDescriptor(
				CapturedLifecycleMetadata.StableRecordKey,
				CapturedRootDescriptorId);
			return;
		}

		const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
			StoredRecord.GetPlannedSiteLifecycleMetadata();
		if (StoredLifecycleMetadata.FrozenSubmissionDescriptorId != CapturedRootDescriptorId
			|| Component->IsFrozenSubmissionDescriptorTombstoned(CapturedRootDescriptorId))
		{
			Component->TombstoneFrozenSubmissionDescriptorPayload(CapturedRootDescriptorId);
			return;
		}
		FString DescriptorPublishFailureReason;
		if (!Component->FrozenSubmissionStore.IsValid()
			|| !LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
				*Component->FrozenSubmissionStore,
				StoredLifecycleMetadata.FrozenSubmissionDescriptorId,
				CapturedRootRegionGroupId,
				StoredLifecycleMetadata.FrozenSubmissionGeneration,
				StoredLifecycleMetadata.FrozenSubmissionAttemptIndex,
				ELayoutFrozenSubmissionRegionKind::Root,
				static_cast<uint32>(StoredLifecycleMetadata.FrozenSubmissionAuditHash),
				TEXT("Planning-window root publish"),
				DescriptorPublishFailureReason))
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				DescriptorPublishFailureReason.IsEmpty()
					? TEXT("Planning-window root publish rejected a stale frozen submission descriptor.")
					: DescriptorPublishFailureReason,
				CapturedRootDescriptorId);
			return;
		}
		if (!Completion.bWorkSucceeded || !SolveResult.bSucceeded)
		{
			// Diagnostic: planning-root solve rejection (LogTemp Verbose for PIE scanning)
			UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] Planning-root solve REJECTED: workSucceeded=%d solveSucceeded=%d failure=%s"),
				Completion.bWorkSucceeded ? 1 : 0, SolveResult.bSucceeded ? 1 : 0,
				*Completion.FailureReason);
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				Completion.FailureReason.IsEmpty()
					? TEXT("Planning-window background solve failed.")
					: Completion.FailureReason,
				CapturedRootDescriptorId);
			return;
		}
		if (!PublishStore->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StoredLifecycleMetadata.StableRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::CompletedAwaitingPublish,
			StoredLifecycleMetadata.FrozenSubmissionDescriptorId,
			StoredLifecycleMetadata.FrozenSubmissionGeneration,
			StoredLifecycleMetadata.FrozenSubmissionAttemptIndex,
			StoredLifecycleMetadata.FrozenSubmissionAuditHash))
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				TEXT("Planning-window root publish rejected a stale frozen submission lifecycle state."),
				CapturedRootDescriptorId);
			return;
		}
		Component->RemovePlanningRootFrozenSubmissionDescriptorPayload(CapturedRootDescriptorId);
		Component->PendingPlanningWindowSolveCentersByRecordKey.Remove(StoredLifecycleMetadata.StableRecordKey);
		FString SolvedArtifactFailureReason;
		if (!ValidateRuntimeSolvedArtifactForPublish(
				SharedResult->SolvedArtifact,
				CapturedSolvedArtifactId,
				SolvedArtifactFailureReason))
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				SolvedArtifactFailureReason,
				CapturedRootDescriptorId);
			return;
		}

		FIntPoint SolvedMinBlockXY;
		FIntPoint SolvedMaxBlockXY;
		ComputePlanningSolveBlockBounds(
			CapturedReservationSelection.SiteCenterBlockWorldPos,
			SolveResult,
			SharedDescriptorWorkerSolvePacket->RuntimeSnapshot.SharedCellSizeInBlocks,
			SolvedMinBlockXY,
			SolvedMaxBlockXY);
		const TArray<FPlannedLayoutSiteRecord> OverlappingAcceptedRecords =
			PublishStore->GetAcceptedPlannedLayoutSiteRecordsOverlappingBlockBounds(
				SolvedMinBlockXY,
				SolvedMaxBlockXY,
				SharedDescriptorWorkerSolvePacket->RuntimeSnapshot.SharedCellSizeInBlocks);
		const bool bOverlapsOtherAcceptedRecord = OverlappingAcceptedRecords.ContainsByPredicate(
			[&StoredLifecycleMetadata](const FPlannedLayoutSiteRecord& AcceptedRecord)
			{
				return AcceptedRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey
					!= StoredLifecycleMetadata.StableRecordKey;
			});
		if (bOverlapsOtherAcceptedRecord)
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				TEXT("Solved footprint overlaps an accepted planned site."),
				CapturedRootDescriptorId);
			return;
		}

	// Build publication metadata directly from the solve-lambda output
		// instead of re-finalizing the packet (avoids duplicate snapshot work).
		FLayoutWorldBindingSiteFrontendSelection PendingFrontendSelection =
			CapturedPendingRecord.GetWorldBindingFrontendSelection();
		PendingFrontendSelection.ResolvedContinuationSelection =
			SharedResult->RootContinuationSelection;

		FLayoutRootPublicationMetadata PendingPublicationMetadata;
		PendingPublicationMetadata.RootSolveId = SharedResult->RootSolveId;
		PendingPublicationMetadata.RootCandidateId = SharedResult->RootCandidateId;
		PendingPublicationMetadata.RootPlacementPolicyId = SharedResult->RootPlacementPolicyId;

		FResolvedLayoutSiteLocationMetadata LocationMetadata;
		LocationMetadata.SiteCenterBlockWorldPos =
			CapturedReservationSelection.SiteCenterBlockWorldPos;
		FResolvedLayoutSiteRecord SiteRecord =
			FLayoutSiteReservation::BuildResolvedSiteRecord(
				LocationMetadata,
				CapturedPendingRecord.GetSiteSolveSourceSelection(),
				&PendingFrontendSelection,
				PendingPublicationMetadata,
				&SolveResult);

		// Validate the frozen terrain contract (already populated by the solve lambda
		// from the precompute adapter).  This replaces the expensive
		// TryGetAcceptedFrozenTerrainContract which destroyed the input
		// contract and tried to rebuild it from chunk-world terrain sampling.
		FString ContractFailureReason;
		if (!ValidateFrozenTerrainContractForAcceptance(
				SharedResult->FrozenTerrainContract,
				ContractFailureReason))
		{
			const ELayoutWorldBindingTerrainFitDiagnosticKind RejectionDiag =
				SharedResult->FrozenTerrainContract.DiagnosticKind != ELayoutWorldBindingTerrainFitDiagnosticKind::None
					? SharedResult->FrozenTerrainContract.DiagnosticKind
					: ELayoutWorldBindingTerrainFitDiagnosticKind::RejectedFoundationDepthExceeded;
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				ContractFailureReason,
				CapturedRootDescriptorId,
				RejectionDiag);
			return;
		}

				// Label the contract as a flat-fit acceptance so downstream realization
		// and terrain-fit outcome assertions match the expected diagnostic kind.
		SharedResult->FrozenTerrainContract.DiagnosticKind =
			ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFlatFit;

		if (!PublishStore->AcceptPlannedLayoutSiteRecordWithAcceptedArtifacts(
				StoredLifecycleMetadata.StableRecordKey,
				SolveResult,
				SharedResult->FrozenTerrainContract,
				SharedResult->SolvedArtifact.ArtifactId))
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredLifecycleMetadata.StableRecordKey,
				TEXT("Planning-window root acceptance rejected accepted-artifact publication."),
				CapturedRootDescriptorId);
			return;
		}

		Component->FinishPlanningAreaAttempt(StoredLifecycleMetadata.StableRecordKey, false);
		Component->RecordLayoutSolvePropagationStats(
			FString::Printf(
				TEXT("accepted async %s %s"),
				*CapturedFrontendSelection.WorldBindingId.ToString(),
				*CapturedReservationSelection.SiteCenterBlockWorldPos.ToString()),
			SolveResult,
			FColor::Cyan);
		if (Component->GetDetailedDiagnostics())
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("Layout planning world binding '%s' scheduled/accepted async site at %s solveSeed=%d footprint=%s placements=%d terrainContract=%s."),
				*CapturedFrontendSelection.WorldBindingId.ToString(),
				*CapturedReservationSelection.SiteCenterBlockWorldPos.ToString(),
				CapturedSolveSourceSelection.SolveSeed,
				*SolveResult.FootprintSize.ToString(),
				SolveResult.Placements.Num(),
				*SharedResult->FrozenTerrainContract.ContractId.ToString());
		}
	};
	const FLayoutPlannedSiteLifecycleMetadata StoredLifecycleMetadata =
		StoredPendingRecord.GetPlannedSiteLifecycleMetadata();
	FLayoutManifestPrewarmInput ManifestPrewarmInput;
	FString ManifestPrewarmFailureReason;
	if (!LayoutRegionPrewarmDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmission(
			ELayoutRegionPrewarmDescriptorSourceKind::RootScoutResult,
			&RootProducedDescriptorArtifact,
			ManifestPrewarmInput,
			ManifestPrewarmFailureReason))
	{
		RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
			StoredLifecycleMetadata.StableRecordKey,
			ManifestPrewarmFailureReason.IsEmpty()
				? TEXT("Planning-window root prewarm requires an already-frozen request manifest carrier.")
				: ManifestPrewarmFailureReason,
			RootDescriptorId);
		return false;
	}

	// Pack the pre-submit snapshot and any pre-existing terrain evidence into the prewarm input.
	ManifestPrewarmInput.bHasPreSubmitSnapshot = RootProducedDescriptorArtifact.DescriptorSeed.Snapshot.bHasWorkerSolvePacket;
	if (ManifestPrewarmInput.bHasPreSubmitSnapshot)
	{
		ManifestPrewarmInput.PreSubmitSnapshot = RootProducedDescriptorArtifact.DescriptorSeed.Snapshot;
	}
	if (SharedDescriptorWorkerSolvePacket->RequestManifest.bHasFrozenTerrainBiomeAdapterInput)
	{
		ManifestPrewarmInput.bHasFrozenTerrainBiomeAdapterInput = true;
		ManifestPrewarmInput.FrozenTerrainBiomeAdapterInput = SharedDescriptorWorkerSolvePacket->RequestManifest.FrozenTerrainBiomeAdapterInput;
	}

	const uint64 RootSubmissionLayoutGroupId = Submission.LayoutGroupId;
	const int32 RootSubmissionPriority = Submission.Priority;
	FLayoutFrozenSolveSubmissionFactory RootSolveFactory = [
		WeakThis,
		SharedResult,
		RootProducedDescriptorArtifact,
		SharedDescriptorWorkerSolvePacket,
		StoredPendingRecordKey,
		CapturedSolvedArtifactId,
		Submission = MoveTemp(Submission)](
		const FLayoutBackgroundSolveCompletion& PreflightCompletion,
		const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
		FLayoutBackgroundSolveSubmission& OutSubmission,
		FString& OutFailureReason) mutable
	{
		UChunkWorldLayoutRuntimeComponent* const Component = WeakThis.Get();
		if (Component == nullptr)
		{
			OutFailureReason = TEXT("Planning-window root descriptor storage lost runtime component.");
			return false;
		}
		if (!Component->FrozenSubmissionStore.IsValid())
		{
			Component->FrozenSubmissionStore = MakeShared<FLayoutFrozenSubmissionStore>();
		}
		if (Component->IsFrozenSubmissionDescriptorTombstoned(RootProducedDescriptorArtifact.DescriptorSeed.DescriptorId))
		{
			OutFailureReason = TEXT("Planning-window root descriptor storage rejected a tombstoned frozen submission descriptor.");
			return false;
		}

		FLayoutFrozenSubmissionDescriptor StoredDescriptor;
		if (!LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
				PreflightCompletion,
				PreflightResult,
				*Component->FrozenSubmissionStore,
				&RootProducedDescriptorArtifact,
				StoredDescriptor,
				OutFailureReason))
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				RootProducedDescriptorArtifact.DescriptorSeed.DescriptorId);
			return false;
		}

		ULayoutPlanningWindowStore* const PublishStore = Component->GetOrCreateLayoutPlanningWindowStore();
		if (PublishStore == nullptr)
		{
			OutFailureReason = TEXT("Planning-window root descriptor storage requires a planning store.");
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				StoredDescriptor.DescriptorId);
			return false;
		}
		if (!PublishStore->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StoredPendingRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::DescriptorReady,
			StoredDescriptor.DescriptorId,
			StoredDescriptor.Generation,
			StoredDescriptor.AttemptIndex,
			static_cast<int32>(StoredDescriptor.AuditHash)))
		{
			OutFailureReason = TEXT("Planning-window root descriptor storage rejected a stale lifecycle record before descriptor-ready state.");
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				StoredDescriptor.DescriptorId);
			return false;
		}

		FPlannedLayoutSiteRecord DescriptorRecord;
		if (!PublishStore->TryGetPlannedLayoutSiteRecord(StoredPendingRecordKey, DescriptorRecord))
		{
			OutFailureReason = TEXT("Planning-window root descriptor storage could not reload descriptor lifecycle metadata.");
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				StoredDescriptor.DescriptorId);
			return false;
		}
		const FLayoutPlannedSiteLifecycleMetadata DescriptorLifecycleMetadata =
			DescriptorRecord.GetPlannedSiteLifecycleMetadata();
		if (!LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
				*Component->FrozenSubmissionStore,
				DescriptorLifecycleMetadata.FrozenSubmissionDescriptorId,
				StoredDescriptor.RegionGroupId,
				DescriptorLifecycleMetadata.FrozenSubmissionGeneration,
				DescriptorLifecycleMetadata.FrozenSubmissionAttemptIndex,
				StoredDescriptor.RegionKind,
				static_cast<uint32>(DescriptorLifecycleMetadata.FrozenSubmissionAuditHash),
				TEXT("Planning-window root solve enqueue"),
				OutFailureReason))
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				DescriptorLifecycleMetadata.FrozenSubmissionDescriptorId);
			return false;
		}
		const FLayoutFrozenSubmissionDescriptor* const LookupDescriptor = LayoutFrozenSubmissionDescriptorProducer::FindForSubmit(
			*Component->FrozenSubmissionStore,
			DescriptorLifecycleMetadata.FrozenSubmissionDescriptorId,
			StoredDescriptor.RegionGroupId,
			DescriptorLifecycleMetadata.FrozenSubmissionGeneration,
			DescriptorLifecycleMetadata.FrozenSubmissionAttemptIndex,
			StoredDescriptor.RegionKind,
			static_cast<uint32>(DescriptorLifecycleMetadata.FrozenSubmissionAuditHash),
			OutFailureReason);
		if (LookupDescriptor == nullptr)
		{
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				DescriptorLifecycleMetadata.FrozenSubmissionDescriptorId);
			return false;
		}

		const FLayoutWorkerSolvePacket DescriptorWorkerSolvePacket = LookupDescriptor->Snapshot.WorkerSolvePacket;
		// Preserve the manifest and precomputed output that OnPrewarmComplete
		// already set, then merge all other fields from the descriptor store.
		const FLayoutWorkerSolveRequestManifest SavedManifest = SharedDescriptorWorkerSolvePacket->RequestManifest;
		const bool bSavedHasPrecomputed = SharedDescriptorWorkerSolvePacket->bHasPrecomputedAdapterOutput;
		const FLayoutAdapterOutput SavedPrecomputedOutput = SharedDescriptorWorkerSolvePacket->PrecomputedAdapterOutput;
		const bool bSavedHasManifest = SharedDescriptorWorkerSolvePacket->bHasRequestManifest;
		*SharedDescriptorWorkerSolvePacket = DescriptorWorkerSolvePacket;
		SharedDescriptorWorkerSolvePacket->RequestManifest = SavedManifest;
		SharedDescriptorWorkerSolvePacket->bHasRequestManifest = bSavedHasManifest;
		SharedDescriptorWorkerSolvePacket->bHasPrecomputedAdapterOutput = bSavedHasPrecomputed;
		SharedDescriptorWorkerSolvePacket->PrecomputedAdapterOutput = SavedPrecomputedOutput;
		// Capture the shared ref so the solve work lambda reads post-prewarm state.
		TSharedRef<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> CapturedShared = SharedDescriptorWorkerSolvePacket;
		Submission.Work = [SharedResult, CapturedShared, CapturedSolvedArtifactId](const FLayoutSolveCancellationToken& CancellationToken, FString& WorkerFailureReason) mutable
		{
			if (CancellationToken.IsCancellationRequested())
			{
				WorkerFailureReason = TEXT("Planning-window root solve canceled before proof.");
				return false;
			}

			FLayoutRegionSolveRequest FinalizedWorkerRequest;
			if (!RunSharedRootSolveWork(
					*CapturedShared,
					CapturedSolvedArtifactId,
					SharedResult->ScheduleResult,
					SharedResult->SolvedArtifact,
					WorkerFailureReason,
					&FinalizedWorkerRequest))
			{
				return false;
			}

		// Copy the precomputed frozen terrain contract and active cells into the
			// shared result so the publish callback can use them for acceptance gates.
			SharedResult->FrozenTerrainContract = FinalizedWorkerRequest.PrecomputedFrozenTerrainContract;
			SharedResult->FrozenTerrainContract.ActiveCells = FinalizedWorkerRequest.PrecomputedActiveCells;

			// Attach active cell provenance to the solved artifact so write-input
			// validation and acceptance gates read a complete artifact.
			FString AttachFailureReason;
			if (!LayoutSolvedArtifact::TryAttachActiveCellProvenance(
					SharedResult->FrozenTerrainContract.ActiveCells,
					SharedResult->SolvedArtifact,
					AttachFailureReason))
			{
				WorkerFailureReason = AttachFailureReason;
				return false;
			}

			// Validate write-input provenance so the publish callback doesn't need to.
			FString WriteInputFailureReason;
			if (!LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(
					SharedResult->SolvedArtifact,
					WriteInputFailureReason))
			{
				WorkerFailureReason = WriteInputFailureReason;
				return false;
			}

			// Capture publish-time metadata so the publish callback avoids
			// re-finalizing the solve packet (eliminates duplicate snapshot work).
			SharedResult->RootSolveId = FinalizedWorkerRequest.RootSolveId;
			SharedResult->RootCandidateId = FinalizedWorkerRequest.RootCandidateId;
			SharedResult->RootPlacementPolicyId = FinalizedWorkerRequest.RootPlacementPolicyId;
			SharedResult->RootContinuationSelection = FinalizedWorkerRequest.bHasSelectedModePlan
				? FinalizedWorkerRequest.SelectedModePlan.ContinuationSelection
				: FLayoutResolvedWorldBindingContinuationSelection();

			BackfillRequestOwnedVerticalAccessPlacementsOnMergedSolveResult(FinalizedWorkerRequest, SharedResult->ScheduleResult);

			if (CancellationToken.IsCancellationRequested())
			{
				WorkerFailureReason = TEXT("Planning-window root solve canceled after proof.");
				return false;
			}

			WorkerFailureReason = SharedResult->ScheduleResult.FailureReason.IsEmpty()
				? SharedResult->ScheduleResult.MergedSolveResult.FailureReason
				: SharedResult->ScheduleResult.FailureReason;
			return SharedResult->ScheduleResult.MergedSolveResult.bSucceeded && !CancellationToken.IsCancellationRequested();
		};

		if (!PublishStore->UpdatePlannedLayoutSiteFrozenSubmissionState(
			StoredPendingRecordKey,
			ELayoutPlannedSiteFrozenSubmissionState::SolveQueued,
			StoredDescriptor.DescriptorId,
			StoredDescriptor.Generation,
			StoredDescriptor.AttemptIndex,
			static_cast<int32>(StoredDescriptor.AuditHash)))
		{
			OutFailureReason = TEXT("Planning-window root descriptor storage rejected a stale lifecycle record before solve queueing.");
			Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
				StoredPendingRecordKey,
				OutFailureReason,
				StoredDescriptor.DescriptorId);
			return false;
		}
		OutSubmission = MoveTemp(Submission);
		return true;
	};
	FLayoutBackgroundSolveDispatcher& BackgroundDispatcher = GetOrCreateBackgroundSolveDispatcher();
	FLayoutBackgroundSolveSubmission LifecycleSubmission =
		FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
			&BackgroundDispatcher,
			FString::Printf(TEXT("PlanningRootPrewarm %s"), *PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString()),
			FString::Printf(TEXT("PlanningRootPreflight %s"), *PendingReservationSourceSelection.SiteCenterBlockWorldPos.ToString()),
			RootSubmissionLayoutGroupId,
			RootSubmissionPriority,
			RootSubmissionPriority,
			MoveTemp(ManifestPrewarmInput),
			MoveTemp(RootSolveFactory),
			[CapturedShared = SharedDescriptorWorkerSolvePacket](const FLayoutBackgroundSolveCompletion&, const FLayoutManifestPrewarmResult& PrewarmResult)
			{
				if (PrewarmResult.bHasDerivedStructuralContract)
				{
					CapturedShared->RequestManifest = PrewarmResult.FinalizedManifest;
				}
				if (PrewarmResult.bHasPrecomputedAdapterOutput)
				{
					CapturedShared->bHasPrecomputedAdapterOutput = true;
					CapturedShared->PrecomputedAdapterOutput = PrewarmResult.PrecomputedAdapterOutput;
				}
			},
			FLayoutAdmissibilityPreflightStageComplete(),
			[WeakThis,
				StableRecordKey = StoredLifecycleMetadata.StableRecordKey,
				RootDescriptorId](const FLayoutBackgroundSolveCompletion&, const FString& FailureReason)
			{
				if (UChunkWorldLayoutRuntimeComponent* const Component = WeakThis.Get())
				{
					Component->RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
						StableRecordKey,
						FailureReason.IsEmpty()
							? TEXT("Planning-window root lifecycle rejected before solve submission.")
							: FailureReason,
						RootDescriptorId);
				}
			}, nullptr, DiagnosticContext);
	const FLayoutBackgroundSolveHandle SubmittedHandle = BackgroundDispatcher.Submit(MoveTemp(LifecycleSubmission));
	if (!SubmittedHandle.IsValid())
	{
		FinishPlanningAreaAttempt(StoredLifecycleMetadata.StableRecordKey, false, true);
		RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
			StoredLifecycleMetadata.StableRecordKey,
			TEXT("Planning-window root lifecycle submission failed."),
			RootDescriptorId);
		return false;
	}
	FPlannedLayoutSiteRecord SubmittedRecord;
	if (Store->TryGetPlannedLayoutSiteRecord(StoredLifecycleMetadata.StableRecordKey, SubmittedRecord)
		&& SubmittedRecord.GetPlannedSiteLifecycleMetadata().State == EPlannedLayoutSiteState::Pending
		&& SubmittedRecord.GetPlannedSiteLifecycleMetadata().FrozenSubmissionDescriptorId == RootDescriptorId
		&& !IsFrozenSubmissionDescriptorTombstoned(RootDescriptorId))
	{
		PendingPlanningWindowSolveHandlesByRecordKey.Add(StoredLifecycleMetadata.StableRecordKey, SubmittedHandle);
		PendingPlanningWindowSolveCentersByRecordKey.Add(
			StoredLifecycleMetadata.StableRecordKey,
			PendingReservationSourceSelection.SiteCenterBlockWorldPos);
	}
	return true;
}

TArray<ULayoutWorldBindingAsset*> UChunkWorldLayoutRuntimeComponent::GetLayoutWorldBindings() const
{
	TArray<ULayoutWorldBindingAsset*> Result;
	Result.Reserve(LayoutWorldBindings.Num());
	for (const TObjectPtr<ULayoutWorldBindingAsset>& WorldBinding : LayoutWorldBindings)
	{
		Result.Add(WorldBinding.Get());
	}
	return Result;
}

void UChunkWorldLayoutRuntimeComponent::InvalidateAutomaticPlanningInputs()
{
	check(IsInGameThread());
	bLoadedInfluenceDirty = true;
	bRealizationDirty = true;
	CancelPlanningAreaDiscovery();
	CancelAutomaticRootWork();
	RetireInvalidAutomaticContinuations(true);
	if (PlanningWindowStore != nullptr) PlanningWindowStore->InvalidateContinuationFailureHistory();
	PlanningAreasByRecordKey.Reset();
	PlanningAreaQueue->Reset();
	LastConnectorEndpointRevision = 0;
	LastPlanningWindowUpdateTimeSeconds = TNumericLimits<double>::Lowest();
}

void UChunkWorldLayoutRuntimeComponent::SetLayoutWorldBindings(const TArray<ULayoutWorldBindingAsset*>& InLayoutWorldBindings)
{
	bool bSameBindings = LayoutWorldBindings.Num() == InLayoutWorldBindings.Num();
	for (int32 Index = 0; bSameBindings && Index < InLayoutWorldBindings.Num(); ++Index)
	{
		bSameBindings = LayoutWorldBindings[Index].Get() == InLayoutWorldBindings[Index];
	}
	if (bSameBindings) return;
	// Initial configuration has no previous binding inputs to invalidate.
	if (!LayoutWorldBindings.IsEmpty()) InvalidateAutomaticPlanningInputs();
	LastPlanningWindowUpdateTimeSeconds = TNumericLimits<double>::Lowest();
	LayoutWorldBindings.Reset();
	LayoutWorldBindings.Reserve(InLayoutWorldBindings.Num());
	for (ULayoutWorldBindingAsset* const WorldBinding : InLayoutWorldBindings)
	{
		LayoutWorldBindings.Add(WorldBinding);
	}
}

bool UChunkWorldLayoutRuntimeComponent::GetDebugGenerationStats() const
{
	const AChunkWorldExtended* ChunkWorld = GetOwningChunkWorld();
	return ChunkWorld != nullptr && ChunkWorld->ShowDebugData;
}

bool UChunkWorldLayoutRuntimeComponent::GetDetailedDiagnostics() const
{
	const AChunkWorldExtended* ChunkWorld = GetOwningChunkWorld();
	return ChunkWorld != nullptr && ChunkWorld->GetDetailedDiagnostics();
}

void UChunkWorldLayoutRuntimeComponent::SetDebugGenerationStats(const bool bInDebugGenerationStats)
{
	if (AChunkWorldExtended* ChunkWorld = GetOwningChunkWorld())
	{
		ChunkWorld->ShowDebugData = bInDebugGenerationStats;
	}
	RefreshDebugGenerationStatsToggle();
}

void UChunkWorldLayoutRuntimeComponent::ReportCachedDebugGenerationStats() const
{
	PublishCachedDebugGenerationStatsOnScreen();
	LogCachedDebugGenerationStats();
}

int32 UChunkWorldLayoutRuntimeComponent::ResolveMaxConcurrentBackgroundLayoutSolves() const
{
	return BuildBackgroundSolveSettings().ResolveMaxConcurrentBackgroundLayoutSolves();
}

FLayoutBackgroundSolveDiagnosticsSnapshot UChunkWorldLayoutRuntimeComponent::GetBackgroundSolveDiagnostics() const
{
	return BackgroundSolveDispatcher.IsValid()
		? BackgroundSolveDispatcher->GetDiagnosticsSnapshot()
		: FLayoutBackgroundSolveDiagnosticsSnapshot();
}

FLayoutBackgroundSolveDispatcher& UChunkWorldLayoutRuntimeComponent::GetOrCreateBackgroundSolveDispatcher()
{
	check(IsInGameThread());
	const FLayoutBackgroundSolveSettings Settings = BuildBackgroundSolveSettings();
	const bool bWasAlreadyValid = BackgroundSolveDispatcher.IsValid();
	if (!BackgroundSolveDispatcher.IsValid())
	{
		BackgroundSolveDispatcher = MakeShared<FLayoutBackgroundSolveDispatcher>(Settings);
		if (TestingExecutor.IsValid())
		{
			BackgroundSolveDispatcher->SetExecution(MoveTemp(TestingExecutor));
		}
	}
	else
	{
		BackgroundSolveDispatcher->SetSettings(Settings);
	}
	return *BackgroundSolveDispatcher;
}

void UChunkWorldLayoutRuntimeComponent::SetLayoutSolveExecutionForTesting(TUniquePtr<ILayoutSolveExecution> Executor)
{
	TestingExecutor = MoveTemp(Executor);
}

FLayoutBackgroundSolveSettings UChunkWorldLayoutRuntimeComponent::BuildBackgroundSolveSettings() const
{
	FLayoutBackgroundSolveSettings Settings;
	Settings.MaxConcurrentBackgroundLayoutSolves = MaxConcurrentBackgroundLayoutSolves;
	Settings.MaxLayoutSolveCandidateAttempts = MaxLayoutSolveCandidateAttempts;
	return Settings;
}

void UChunkWorldLayoutRuntimeComponent::PumpBackgroundLayoutSolves()
{
	if (BackgroundSolveDispatcher.IsValid())
	{
		BackgroundSolveDispatcher->SetSettings(BuildBackgroundSolveSettings());
		BackgroundSolveDispatcher->Tick();
	}
	for (auto It = PendingPlanningWindowSolveHandlesByRecordKey.CreateIterator(); It; ++It)
	{
		if (BackgroundSolveDispatcher && BackgroundSolveDispatcher->HasRetainedGroup(It.Value().LayoutGroupId)) continue;
		const auto* Record = PlanningWindowStore ? PlanningWindowStore->FindPlannedLayoutSiteRecord(It.Key()) : nullptr;
		if (Record && Record->State == EPlannedLayoutSiteState::Accepted) continue;
		PendingPlanningWindowSolveCentersByRecordKey.Remove(It.Key());
		It.RemoveCurrent();
	}
	for (auto It = PendingAutomaticContinuationPreparations.CreateIterator(); It; ++It)
	{
		if (BackgroundSolveDispatcher && BackgroundSolveDispatcher->HasRetainedGroup(It.Value().LayoutGroupId)) continue;
		It.RemoveCurrent();
		LastConnectorEndpointRevision = 0;
	}
	for (auto It = ContinuationRouteReservations.CreateIterator(); It; ++It)
	{
		if (!It.Value().bReleased) continue;
		bool bRetained = false;
		for (const uint64 Key : It.Value().SegmentKeys)
			bRetained |= BackgroundSolveDispatcher && BackgroundSolveDispatcher->HasRetainedGroup(Key);
		if (!bRetained)
		{
			It.RemoveCurrent();
			LastConnectorEndpointRevision = 0;
		}
	}
	// Only callbacks admitted before invalidation can still reference a tombstone.
	// New unrelated jobs must not keep historical descriptor ids alive forever.
	if (!FrozenSubmissionTombstoneFences.IsEmpty())
	{
		const uint64 RetiredThrough = BackgroundSolveDispatcher.IsValid()
			? BackgroundSolveDispatcher->GetRetiredJobWatermark() : MAX_uint64;
		for (auto It = FrozenSubmissionTombstoneFences.CreateIterator(); It; ++It)
		{
			if (It.Value() <= RetiredThrough) It.RemoveCurrent();
		}
	}
}

void UChunkWorldLayoutRuntimeComponent::SetDisableAutoPumpForTesting(const bool bDisable)
{
	// Force-create the dispatcher so the flag is set before the first Submit.
	GetOrCreateBackgroundSolveDispatcher();
	if (BackgroundSolveDispatcher.IsValid())
	{
		BackgroundSolveDispatcher->SetDisableAutoPumpForTesting(bDisable);
	}
}

void UChunkWorldLayoutRuntimeComponent::RecordLayoutSolvePropagationStats(
	const FString& ContextText,
	const FLayoutSolveResult& SolveResult,
	const FColor& Color)
{
	const FLayoutSolverPropagationStats& PropagationStats = SolveResult.PropagationStats;
	INC_DWORD_STAT_BY(STAT_PorismLayout_PropagationRuns, PropagationStats.PropagationRunCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_PropagationPasses, PropagationStats.PropagationPassCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_CandidateAttempts, PropagationStats.CandidateAttemptCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_Backtracks, PropagationStats.BacktrackCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_ArcQueuePops, PropagationStats.ArcQueuePopCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_SupportChecks, PropagationStats.SupportCheckCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_CandidateRemovals, PropagationStats.CandidateRemovalCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_FailedCells, PropagationStats.FailedCellCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainStages, PropagationStats.TerrainStageCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainBlockedFrontiers, PropagationStats.TerrainBlockedFrontierCount);
	INC_DWORD_STAT_BY(STAT_PorismLayout_TerrainBacktracks, PropagationStats.TerrainStageBacktrackCount);

	// Trim before taking a reference: removing the oldest line relocates the array.
	constexpr int32 MaxCachedDebugGenerationStatsLines = 12;
	if (LastDebugGenerationStatsLines.Num() >= MaxCachedDebugGenerationStatsLines)
	{
		LastDebugGenerationStatsLines.RemoveAt(0, LastDebugGenerationStatsLines.Num() - MaxCachedDebugGenerationStatsLines + 1, EAllowShrinking::No);
	}
	FDebugGenerationStatsLine& StatsLine = LastDebugGenerationStatsLines.AddDefaulted_GetRef();
	StatsLine.Message = FString::Printf(
		TEXT("Layout solve %s: %s"),
		*ContextText,
		*BuildLayoutSolvePropagationStatsSummary(SolveResult));
	StatsLine.Color = Color;

	ShowLayoutSolvePropagationStatsOnScreen(GetDebugGenerationStats(), ContextText, SolveResult, Color);
	if (GetDetailedDiagnostics())
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *StatsLine.Message);
	}
}

#if WITH_EDITOR
void UChunkWorldLayoutRuntimeComponent::HandlePlanningAssetChanged(UObject* Object, FPropertyChangedEvent& Event)
{
	if (Object == nullptr || LayoutWorldBindings.IsEmpty()) return;
	const auto* Owner = GetOwningChunkWorld();
	const UWorldGenDef* Definition = Owner ? Owner->WorldGenDef.Get() : nullptr;
	RefreshPlanningBiomeTableSubscription();
	// UDataTable broadcasts row/property changes itself; avoid a duplicate revision here.
	if (Definition && Object == Definition->WorldBiomesDT) return;
	// Definition/biome edits change discovery evidence even when layout assets stay unchanged.
	// Reuse revision invalidation; do not restart terrain or poll asset hashes.
	if (Definition && (Object == Definition || Object->IsIn(Definition)
		|| Object == Definition->GetClass()->ClassGeneratedBy))
	{
		InvalidateAutomaticPlanningInputs();
		return;
	}
	if (Definition)
	{
		const auto ChangedNoise = [Object](const FString& Encoded, const UClass* Class, const UObject* Node)
		{
			if (!Encoded.IsEmpty()) return false;
			if (Class)
			{
				const UObject* Defaults = Class->GetDefaultObject(false);
				if (Object == Class || Object == Class->ClassGeneratedBy
					|| (Defaults && (Object == Defaults || Object->IsIn(Defaults)))) return true;
			}
			return Node && (Object == Node || Object->IsIn(Node));
		};
		const auto ChangedRow = [&ChangedNoise](const FBiomeDualData& Row)
		{
			return ChangedNoise(Row.Domain, Row.DomainBP, Row.DomainRun)
				|| ChangedNoise(Row.DualSwitch, Row.DualSwitchBP, Row.DualSwitchRun)
				|| ChangedNoise(Row.GenA, Row.GenABP, Row.GenARun)
				|| ChangedNoise(Row.GenB, Row.GenBBP, Row.GenBRun);
		};
		bool bChanged = false;
		if (Definition->WorldBiomesDT && Definition->WorldBiomesDT->GetRowStruct() == FBiomeDualData::StaticStruct())
		{
			for (const auto& Pair : Definition->WorldBiomesDT->GetRowMap())
				bChanged |= ChangedRow(*reinterpret_cast<const FBiomeDualData*>(Pair.Value));
		}
		else
		{
			for (const FBiomeDualData& Row : Definition->WorldBiomes) bChanged |= ChangedRow(Row);
		}
		if (bChanged)
		{
			InvalidateAutomaticPlanningInputs();
			return;
		}
	}
	if (!(Object->IsA<ULayoutWorldBindingAsset>() || Object->IsA<ULayoutProfileAsset>()
			|| Object->IsA<ULayoutRegionContentSetAsset>() || Object->IsA<ULayoutCompositeModuleAsset>()
			|| Object->IsA<ULayoutModuleAsset>())) return;

	// Walk only resident layout inputs on an editor change event, never on a tick.
	// The visited set also bounds malformed recursive child-profile references.
	TArray<UObject*> Pending;
	for (const auto& Binding : LayoutWorldBindings) Pending.Add(Binding.Get());
	TSet<UObject*> Visited;
	while (!Pending.IsEmpty())
	{
		UObject* Input = Pending.Pop(EAllowShrinking::No);
		if (Input == nullptr || Visited.Contains(Input)) continue;
		if (Input == Object)
		{
			InvalidateAutomaticPlanningInputs();
			return;
		}
		Visited.Add(Input);
		if (const auto* Binding = Cast<ULayoutWorldBindingAsset>(Input))
		{
			for (const auto& Candidate : Binding->Candidates) Pending.Add(Candidate.LayoutProfile.Get());
			for (const auto& Family : Binding->ContinuationFamilies)
				for (const auto& Candidate : Family.Candidates) Pending.Add(Candidate.LayoutProfile.Get());
		}
		else if (const auto* Profile = Cast<ULayoutProfileAsset>(Input))
		{
			Pending.Add(Profile->ContentSet.Get());
		}
		else if (const auto* Content = Cast<ULayoutRegionContentSetAsset>(Input))
		{
			for (const auto& Entry : Content->Entries)
			{
				if (Entry.ContentKind == ELayoutRegionContentKind::ChildRegion)
					Pending.Add(Entry.ChildRegionSettings.RegionProfile.Get());
				else
				{
					Pending.Add(Entry.ModuleSettings.Module.Get());
					Pending.Add(Entry.ModuleSettings.CompositeModule.Get());
				}
			}
		}
		else if (const auto* Composite = Cast<ULayoutCompositeModuleAsset>(Input))
		{
			for (const auto& Cell : Composite->Cells) Pending.Add(Cell.Module.Get());
		}
	}
}
#endif

void UChunkWorldLayoutRuntimeComponent::RefreshPlanningBiomeTableSubscription()
{
	const auto* Owner = GetOwningChunkWorld();
	UDataTable* Table = IsRegistered() && Owner && Owner->WorldGenDef ? Owner->WorldGenDef->WorldBiomesDT : nullptr;
	if (PlanningBiomeTable.Get() == Table) return;
	if (auto* Previous = PlanningBiomeTable.Get()) Previous->OnDataTableChanged().Remove(PlanningBiomeTableChangedHandle);
	PlanningBiomeTable = Table;
	PlanningBiomeTableChangedHandle.Reset();
	if (Table) PlanningBiomeTableChangedHandle = Table->OnDataTableChanged().AddUObject(
		this, &UChunkWorldLayoutRuntimeComponent::InvalidateAutomaticPlanningInputs);
}

void UChunkWorldLayoutRuntimeComponent::OnRegister()
{
	Super::OnRegister();
	RefreshPlanningBiomeTableSubscription();
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.AddUObject(this, &UChunkWorldLayoutRuntimeComponent::HandlePlanningAssetChanged);
	if (GEditor) GEditor->OnBlueprintPreCompile().AddWeakLambda(this, [this](UBlueprint* Blueprint)
	{
		// Fence old captures before class/CDO replacement; fresh capture resumes on the game thread.
		FPropertyChangedEvent Changed(nullptr);
		HandlePlanningAssetChanged(Blueprint, Changed);
	});
	if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor && !EditorDebugDrawHandle.IsValid())
	{
		EditorDebugDrawHandle = UDebugDrawService::Register(TEXT("OnScreenDebug"),
			FDebugDrawDelegate::CreateUObject(this, &UChunkWorldLayoutRuntimeComponent::DrawEditorDebugGenerationStats));
	}
#endif
}

void UChunkWorldLayoutRuntimeComponent::OnUnregister()
{
	if (auto* Table = PlanningBiomeTable.Get()) Table->OnDataTableChanged().Remove(PlanningBiomeTableChangedHandle);
	PlanningBiomeTable.Reset();
	PlanningBiomeTableChangedHandle.Reset();
#if WITH_EDITOR
	FCoreUObjectDelegates::OnObjectPropertyChanged.RemoveAll(this);
	if (GEditor) GEditor->OnBlueprintPreCompile().RemoveAll(this);
	if (EditorDebugDrawHandle.IsValid())
	{
		UDebugDrawService::Unregister(EditorDebugDrawHandle);
		EditorDebugDrawHandle.Reset();
	}
	bReportedEditorStatsDraw = false;
#endif
	Super::OnUnregister();
}

FString UChunkWorldLayoutRuntimeComponent::BuildPlanningWindowDebugStatusMessage() const
{
	const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	int32 QueuedAreas = 0, ExhaustedAreas = 0;
	PlanningAreaQueue->GetCounts(QueuedAreas, ExhaustedAreas);
	// Count retained routes, not segments: one accepted/applied sibling establishes
	// route success while unfinished siblings can still contribute pending work.
	TSet<FLayoutId> SolvingRoutes, SolvedRoutes, PlacedRoutes;
	for (const auto& Pair : ContinuationRouteReservations)
	{
		if (Pair.Value.bCanceled || Pair.Value.bReleased) continue;
		for (const uint64 Key : Pair.Value.SegmentKeys)
		{
			if (!Pair.Value.FailedSegmentKeys.Contains(Key)
				&& !Pair.Value.CommittedSegmentKeys.Contains(Key)
				&& !ResolvedConnectorRecords.Contains(Key))
			{
				SolvingRoutes.Add(Pair.Key);
				break;
			}
		}
	}
	for (const auto& Pair : ResolvedConnectorRecords)
	{
		const FResolvedLayoutConnectorRecord& Record = Pair.Value;
		if (Record.ContinuationRouteId.IsNone() || !Record.bLayoutSolved || !Record.SolveResult.bSucceeded) continue;
		SolvedRoutes.Add(Record.ContinuationRouteId);
		if (Record.bHasBeenCommittedToChunkWorld) PlacedRoutes.Add(Record.ContinuationRouteId);
	}
	int32 Screening = 0, Eligible = 0, WaitingRoots = 0;
	for (const auto& Layer : ObservedChunkLayers)
	for (const auto& Pair : Layer.Chunks)
	{
		if (!Pair.Value.bCreated) continue;
		Screening += Pair.Value.Screening == ELayoutChunkScreening::Pending || Pair.Value.Screening == ELayoutChunkScreening::Uncertain;
		Eligible += Pair.Value.Screening == ELayoutChunkScreening::Eligible;
	}
	for (const auto& Pair : PendingPlanningWindowSolveHandlesByRecordKey)
	{
		const auto* Record = PlanningWindowStore ? PlanningWindowStore->FindPlannedLayoutSiteRecord(Pair.Key) : nullptr;
		WaitingRoots += Record && Record->State == EPlannedLayoutSiteState::Accepted;
	}
	const int32 Capacity = FMath::Max(1, MaxCachedPlanningChunks);
	const int32 Owners = CountAutomaticPlanningWork();
	const int32 CenterCount = CollectPlanningWindowCenters().Num();
	const bool bRunning = ChunkWorld && ChunkWorld->IsRunning();
	const int32 WorkingChunks = PlanningAreaQueue->GetWorkingCount();
	const bool bSaturated = Owners >= Capacity || WorkingChunks >= Capacity;
	const TCHAR* WaitingReason = !bEnablePlanningWindowRuntimeUpdates || !bRunning ? TEXT("inactive")
		: CenterCount == 0 ? TEXT("no centers") : bSaturated ? TEXT("capacity")
		: WaitingRoots > 0 || SolvedRoutes.Num() > PlacedRoutes.Num() ? TEXT("placement coverage/data")
		: Owners > 0 ? TEXT("workers") : QueuedAreas > 0 ? TEXT("screening") : TEXT("settled");
	return FString::Printf(TEXT("Layout cache [%s]: %s terrain=%s centers=%d loaded=%d LOD-records working=%d/%d owners=%d/%d frontier=%d areas backlog=%d chunks saturated=%s wait=%s discovery=%s processed=%llu\nRoot cache: %s | screening=%d eligible=%d exhausted=%d evicted chunks/work=%llu/%llu | Retained routes: solving=%d solved=%d placed=%d unplaced=%d"),
		*GetNameSafe(GetOwner()), bEnablePlanningWindowRuntimeUpdates ? TEXT("enabled") : TEXT("disabled"),
		bRunning ? TEXT("running") : TEXT("stopped"), CenterCount, GetObservedLoadedChunkCount(),
		WorkingChunks, Capacity, Owners, Capacity, PlanningAreaQueue->GetFrontierCount(), QueuedAreas,
		bSaturated ? TEXT("yes") : TEXT("no"), WaitingReason,
		PendingPlanningAreaDiscovery.IsSet() ? TEXT("active") : TEXT("idle"), CompletedDiscoveryAreas,
		PlanningWindowStore != nullptr ? *PlanningWindowStore->BuildDebugStatusSummary() : TEXT("no planning store"),
		Screening, Eligible, ExhaustedAreas, PlanningAreaQueue->GetEvictedCount(), EvictedAutomaticWork,
		SolvingRoutes.Num(), SolvedRoutes.Num(), PlacedRoutes.Num(), SolvedRoutes.Num() - PlacedRoutes.Num());
}

#if WITH_EDITOR
void UChunkWorldLayoutRuntimeComponent::DrawEditorDebugGenerationStats(UCanvas* Canvas, APlayerController*)
{
	if ((!GetDebugGenerationStats() && FPlatformTime::Seconds() > EditorDebugStatsVisibleUntil)
		|| GEngine == nullptr || Canvas == nullptr || Canvas->Canvas == nullptr
		|| Canvas->SceneView == nullptr || Canvas->SceneView->Family == nullptr
		|| Canvas->SceneView->Family->Scene == nullptr
		|| Canvas->SceneView->Family->Scene->GetWorld() != GetWorld())
	{
		return;
	}
	const float Width = FMath::Min(600.0f, Canvas->ClipX - 24.0f);
	if (Width <= 0.0f || Canvas->ClipY <= 80.0f)
	{
		return;
	}
	// One registered component draws this world's column. A shared Y cursor prevents
	// two active actors from painting different counters at the same screen position.
	TArray<UChunkWorldLayoutRuntimeComponent*> Sources;
	const double Now = FPlatformTime::Seconds();
	for (TActorIterator<AChunkWorldExtended> It(GetWorld()); It; ++It)
	{
		auto* Source = It->GetLayoutRuntimeComponent();
		if (!Source || !Source->IsRegistered()
			|| (!Source->GetDebugGenerationStats() && Now > Source->EditorDebugStatsVisibleUntil)) continue;
		if (Source->GetUniqueID() < GetUniqueID()) return;
		Sources.Add(Source);
	}
	Sources.Sort([](const UChunkWorldLayoutRuntimeComponent& A, const UChunkWorldLayoutRuntimeComponent& B)
	{
		return A.GetUniqueID() < B.GetUniqueID();
	});
	const float X = FMath::Max(12.0f, Canvas->ClipX - Width - 12.0f);
	float Y = 60.0f;
	TGuardValue<FColor> RestoreColor(Canvas->DrawColor, FColor::Cyan);
	FFontRenderInfo FontInfo;
	FontInfo.bEnableShadow = true;
	for (auto* Source : Sources)
	{
		FTextSizingParameters Sizing(X, Y, Width, Canvas->ClipY - Y, GEngine->GetSmallFont());
		FString Text = Source->BuildPlanningWindowDebugStatusMessage() + TEXT("\n") + Source->GetBackgroundSolveDiagnostics().ToDebugString();
		for (const FDebugGenerationStatsLine& Line : Source->LastDebugGenerationStatsLines) Text += TEXT("\n") + Line.Message;
		TArray<FString> Paragraphs;
		Text.ParseIntoArrayLines(Paragraphs);
		int32 DrawnLines = 0;
		for (const FString& Paragraph : Paragraphs)
		{
			TArray<FWrappedStringElement> Lines;
			Canvas->WrapString(Sizing, 0.0f, Paragraph, Lines);
			for (const FWrappedStringElement& Line : Lines)
			{
				if (Y + Line.LineExtent.Y > Canvas->ClipY - 12.0f) break;
				Canvas->DrawText(GEngine->GetSmallFont(), Line.Value, X, Y, 1.0f, 1.0f, FontInfo);
				++DrawnLines;
				Y += FMath::Max(12.0f, static_cast<float>(Line.LineExtent.Y)) + 2.0f;
			}
		}
		if (DrawnLines > 0 && !Source->bReportedEditorStatsDraw)
		{
			Source->bReportedEditorStatsDraw = true;
			UE_LOG(LogTemp, Log, TEXT("Layout editor HUD drew %d lines for %s in %dx%d viewport; independent of Show Stats."),
				DrawnLines, *GetNameSafe(Source->GetOwner()), Canvas->SizeX, Canvas->SizeY);
		}
		Y += 12.0f;
		if (Y >= Canvas->ClipY - 12.0f) break;
	}
}
#endif

void UChunkWorldLayoutRuntimeComponent::PublishCachedDebugGenerationStatsOnScreen() const
{
	if (GEngine == nullptr)
	{
		return;
	}

#if WITH_EDITOR
	if (GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor)
	{
		// Editor stats bypass the global message queue and its Show Stats/realtime gate.
		EditorDebugStatsVisibleUntil = FPlatformTime::Seconds() + 2.0;
		return;
	}
#endif
	// Keyed Porism-style text survives slow frames without adding a line every tick.
	const uint64 MessageKey = static_cast<uint64>(GetUniqueID()) << 4;
	const FString Summary = BuildPlanningWindowDebugStatusMessage();
	GEngine->AddOnScreenDebugMessage(MessageKey, 2.0f, FColor::Cyan, Summary);
	GEngine->AddOnScreenDebugMessage(MessageKey + 1, 2.0f, FColor::Cyan, GetBackgroundSolveDiagnostics().ToDebugString());
	for (int32 Index = 0; Index < 12; ++Index)
	{
		if (LastDebugGenerationStatsLines.IsValidIndex(Index))
		{
			const FDebugGenerationStatsLine& StatsLine = LastDebugGenerationStatsLines[Index];
			GEngine->AddOnScreenDebugMessage(MessageKey + 2 + Index, 2.0f, StatsLine.Color, StatsLine.Message);
		}
		else
		{
			GEngine->RemoveOnScreenDebugMessage(MessageKey + 2 + Index);
		}
	}
}

void UChunkWorldLayoutRuntimeComponent::LogCachedDebugGenerationStats() const
{
	UE_LOG(LogTemp, Log, TEXT("%s"), *BuildPlanningWindowDebugStatusMessage());
	UE_LOG(LogTemp, Log, TEXT("%s"), *GetBackgroundSolveDiagnostics().ToDebugString());

	if (LastDebugGenerationStatsLines.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("Layout generation stats: no cached solve stats yet."));
		return;
	}

	for (const FDebugGenerationStatsLine& StatsLine : LastDebugGenerationStatsLines)
	{
		UE_LOG(LogTemp, Log, TEXT("%s"), *StatsLine.Message);
	}
}

void UChunkWorldLayoutRuntimeComponent::RefreshDebugGenerationStatsToggle()
{
	if (GetDebugGenerationStats() && !bLastObservedDebugGenerationStats)
	{
#if WITH_EDITOR
		bReportedEditorStatsDraw = false;
		if (GEditor != nullptr && GetWorld() != nullptr && GetWorld()->WorldType == EWorldType::Editor)
		{
			GEditor->RedrawLevelEditingViewports(false);
			UE_LOG(LogTemp, Log, TEXT("Layout editor HUD enabled: delegateRegistered=%d; viewport On Screen Debug controls visibility, Show Stats is not required."),
				EditorDebugDrawHandle.IsValid());
		}
#endif
		PublishCachedDebugGenerationStatsOnScreen();
	}
	bLastObservedDebugGenerationStats = GetDebugGenerationStats();
}

TArray<FResolvedLayoutSiteRecord> UChunkWorldLayoutRuntimeComponent::GetResolvedLayoutSiteRecords() const
{
	TArray<FResolvedLayoutSiteRecord> Records;
	ResolvedSiteRecords.GenerateValueArray(Records);
	return Records;
}

TArray<FResolvedLayoutConnectorRecord> UChunkWorldLayoutRuntimeComponent::GetResolvedLayoutConnectorRecords() const
{
	TArray<FResolvedLayoutConnectorRecord> Records;
	ResolvedConnectorRecords.GenerateValueArray(Records);
	Records.Sort([](const FResolvedLayoutConnectorRecord& Left, const FResolvedLayoutConnectorRecord& Right)
	{
		return BuildResolvedConnectorRecordKey(Left) < BuildResolvedConnectorRecordKey(Right);
	});
	return Records;
}

void UChunkWorldLayoutRuntimeComponent::CompleteExplicitContinuationRequest(const uint64 ConnectorKey, const FLayoutExplicitContinuationSolveResult& Result)
{
	TFunction<void(const FLayoutExplicitContinuationSolveResult&)> Callback;
	ExplicitContinuationCompletionCallbacks.RemoveAndCopyValue(ConnectorKey, Callback);
	PendingExplicitPreparedRoutes.Remove(ConnectorKey);
	if (const TArray<uint64>* SegmentKeys = ExplicitRouteSegmentKeysByConnectorKey.Find(ConnectorKey))
	{
		for (const uint64 SegmentKey : *SegmentKeys) PendingExplicitPreparedSegmentsByConnectorKey.Remove(SegmentKey);
	}
	// Preview ownership survives publication: Clear still needs the original route-to-segment mapping.
	if (Callback) Callback(Result);
}

void UChunkWorldLayoutRuntimeComponent::RegisterContinuationRouteReservation(
	const FLayoutId RouteId,
	const FString& EdgeKey,
	const TArray<uint64>& SegmentKeys)
{
	if (RouteId.IsNone() || EdgeKey.IsEmpty() || SegmentKeys.IsEmpty())
	{
		return;
	}
	FLayoutContinuationRouteReservationState& State =
		ContinuationRouteReservations.FindOrAdd(RouteId);
	State = FLayoutContinuationRouteReservationState();
	State.EdgeKey = EdgeKey;
	for (const uint64 SegmentKey : SegmentKeys)
	{
		State.SegmentKeys.Add(SegmentKey);
		ContinuationEdgeKeysByConnectorKey.Add(SegmentKey, EdgeKey);
		ContinuationRouteIdsByConnectorKey.Add(SegmentKey, RouteId);
	}
}

void UChunkWorldLayoutRuntimeComponent::ReleaseContinuationRouteReservation(const FLayoutId RouteId)
{
	FLayoutContinuationRouteReservationState* const State = ContinuationRouteReservations.Find(RouteId);
	if (State == nullptr)
	{
		return;
	}
	if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
	{
		Store->ReleaseContinuationEndpointPair(State->EdgeKey);
	}
	for (const uint64 SegmentKey : State->SegmentKeys)
	{
		ContinuationEdgeKeysByConnectorKey.Remove(SegmentKey);
		ContinuationRouteIdsByConnectorKey.Remove(SegmentKey);
	}
	State->bReleased = true;
	State->bAwaitingSubmission = false;
	bool bRetained = false;
	if (State->bAutomatic && BackgroundSolveDispatcher)
		for (const uint64 Key : State->SegmentKeys) bRetained |= BackgroundSolveDispatcher->HasRetainedGroup(Key);
	if (!bRetained) ContinuationRouteReservations.Remove(RouteId);
	RetainedPreparedContinuationRoutesById.Remove(RouteId);
}

void UChunkWorldLayoutRuntimeComponent::ReleaseContinuationReservationForConnector(const uint64 ConnectorKey, const bool bFailed)
{
	if (const FLayoutId* const RouteId = ContinuationRouteIdsByConnectorKey.Find(ConnectorKey))
	{
		const FLayoutId OwnedRouteId = *RouteId;
		FLayoutContinuationRouteReservationState* const State = ContinuationRouteReservations.Find(OwnedRouteId);
		if (State != nullptr)
		{
			if (State->CommittedSegmentKeys.Contains(ConnectorKey)) return;
			State->FailedSegmentKeys.Add(ConnectorKey);
			State->bCanceled |= !bFailed;
		}
		ContinuationEdgeKeysByConnectorKey.Remove(ConnectorKey);
		ContinuationRouteIdsByConnectorKey.Remove(ConnectorKey);
		if (State != nullptr && State->IsSettled())
		{
			if (!State->bCanceled && !State->bCapacityConsumed)
			{
				if (ULayoutPlanningWindowStore* Store = GetOrCreateLayoutPlanningWindowStore()) Store->ReleaseContinuationEndpointPair(State->EdgeKey, true);
			}
			ReleaseContinuationRouteReservation(OwnedRouteId);
		}
		return;
	}
	if (const FString* const EdgeKey = ContinuationEdgeKeysByConnectorKey.Find(ConnectorKey))
	{
		if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
		{
			Store->ReleaseContinuationEndpointPair(*EdgeKey, bFailed);
		}
		if (GetDetailedDiagnostics())
		{
			UE_LOG(LogTemp, Display, TEXT("[DEBUG-layout-endpoint] Edge released connector=%llu edge=%s."), ConnectorKey, **EdgeKey);
		}
		ContinuationEdgeKeysByConnectorKey.Remove(ConnectorKey);
	}
}

bool UChunkWorldLayoutRuntimeComponent::ConsumeContinuationReservationForConnector(const uint64 ConnectorKey)
{
	if (const FLayoutId* const RouteId = ContinuationRouteIdsByConnectorKey.Find(ConnectorKey))
	{
		FLayoutContinuationRouteReservationState* const State =
			ContinuationRouteReservations.Find(*RouteId);
		if (State == nullptr)
		{
			return false;
		}
		if (!State->SegmentKeys.Contains(ConnectorKey) || State->FailedSegmentKeys.Contains(ConnectorKey)) return false;
		const FLayoutId OwnedRouteId = *RouteId;
		if (!State->bCapacityConsumed)
		{
			ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
			if (Store == nullptr || !Store->ConsumeContinuationEndpointPair(State->EdgeKey)) return false;
			State->bCapacityConsumed = true;
		}
		State->CommittedSegmentKeys.Add(ConnectorKey);
		if (State->IsSettled())
		{
			// Committed edges survive release; only transient route payloads disappear.
			ReleaseContinuationRouteReservation(OwnedRouteId);
		}
		return true;
	}
	const FString* const EdgeKey = ContinuationEdgeKeysByConnectorKey.Find(ConnectorKey);
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	if (EdgeKey == nullptr || Store == nullptr || !Store->ConsumeContinuationEndpointPair(*EdgeKey))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG-layout-endpoint] Edge consume failed connector=%llu edge=%s."),
			ConnectorKey, EdgeKey != nullptr ? **EdgeKey : TEXT("<missing>"));
		return false;
	}
	if (GetDetailedDiagnostics())
	{
		UE_LOG(LogTemp, Display, TEXT("[DEBUG-layout-endpoint] Edge consumed connector=%llu edge=%s."), ConnectorKey, **EdgeKey);
	}
	ContinuationEdgeKeysByConnectorKey.Remove(ConnectorKey);
	return true;
}

bool UChunkWorldLayoutRuntimeComponent::TryFindContinuationEndpointAtBlock(
	const FIntVector& HoveredBlockWorldPos,
	const ULayoutWorldBindingAsset* const WorldBinding,
	const ULayoutProfileAsset* const ContinuationProfile,
	FResolvedLayoutConnectorEndpoint& OutEndpoint,
	FString& OutFailureReason) const
{
	OutEndpoint = FResolvedLayoutConnectorEndpoint();
	OutFailureReason.Reset();
	FLayoutWorldBindingRuntimeView RuntimeView;
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	if (!LayoutWorldBindingRuntimeView::TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
			WorldBinding,
			const_cast<ULayoutProfileAsset*>(ContinuationProfile),
			RuntimeView,
			FrontendSelection,
			OutFailureReason)
		|| RuntimeView.ContinuationSelection.FamilyId.IsNone())
	{
		if (OutFailureReason.IsEmpty())
		{
			OutFailureReason = TEXT("Layout Generator requires one unambiguous continuation profile on the selected world binding.");
		}
		return false;
	}
	const FLayoutWorldBindingContinuationFamily* Family = WorldBinding->ContinuationFamilies.FindByPredicate(
		[&RuntimeView](const FLayoutWorldBindingContinuationFamily& Candidate)
		{
			return Candidate.FamilyId == RuntimeView.ContinuationSelection.FamilyId;
		});
	if (Family == nullptr || !Family->EndpointConnectorTypeTag.IsValid())
	{
		OutFailureReason = TEXT("Selected continuation family has no valid endpoint connector tag.");
		return false;
	}
	const FName BindingId = !WorldBinding->BindingId.IsNone() ? WorldBinding->BindingId : WorldBinding->GetFName();
	const FIntVector CellSize = WorldBinding->BaseCellDimensionsBlocks;
	if (CellSize.X <= 0 || CellSize.Y <= 0)
	{
		OutFailureReason = TEXT("Selected world binding has invalid shared cell dimensions.");
		return false;
	}
	const int64 RadiusSquared = static_cast<int64>(CellSize.X / 2) * (CellSize.X / 2)
		+ static_cast<int64>(CellSize.Y / 2) * (CellSize.Y / 2);
	TArray<FResolvedLayoutConnectorEndpoint> Matches;
	int32 LedgerRecordCount = 0;
	int32 CompatibleReadyRecordCount = 0;
	bool bHoveredReservedEndpoint = false;
	if (const ULayoutPlanningWindowStore* const Store = PlanningWindowStore)
	{
		const TArray<FLayoutPlanningWindowEndpointRecord> LedgerRecords =
			Store->GetContinuationEndpointRecords();
		LedgerRecordCount = LedgerRecords.Num();
		for (const FLayoutPlanningWindowEndpointRecord& Record : LedgerRecords)
		{
			if (Record.WorldBindingId != BindingId
				|| Record.ContinuationFamilyId != Family->FamilyId
				|| Record.Endpoint.ConnectorTypeTag != Family->EndpointConnectorTypeTag)
			{
				continue;
			}
			const int64 DeltaX = static_cast<int64>(Record.Endpoint.EndpointBlockWorldPos.X) - HoveredBlockWorldPos.X;
			const int64 DeltaY = static_cast<int64>(Record.Endpoint.EndpointBlockWorldPos.Y) - HoveredBlockWorldPos.Y;
			const bool bUnderHover = DeltaX * DeltaX + DeltaY * DeltaY <= RadiusSquared;
			if (Record.RemainingConnections <= 0 || Record.State != ELayoutContinuationEndpointState::Ready)
			{
				bHoveredReservedEndpoint |= bUnderHover && Record.State == ELayoutContinuationEndpointState::Reserved;
				continue;
			}
			++CompatibleReadyRecordCount;
			if (bUnderHover)
			{
				FResolvedLayoutConnectorEndpoint Endpoint = Record.Endpoint;
				Endpoint.RootRecordKey = Record.RootRecordKey;
				Matches.Add(MoveTemp(Endpoint));
			}
		}
	}
	if (Matches.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DEBUG-layout-endpoint] Hover miss block=%s binding=%s family=%s tag=%s ledger=%d compatibleReady=%d radiusSq=%lld."),
			*HoveredBlockWorldPos.ToString(),
			*BindingId.ToString(),
			*Family->FamilyId.ToString(),
			*Family->EndpointConnectorTypeTag.ToString(),
			LedgerRecordCount,
			CompatibleReadyRecordCount,
			RadiusSquared);
		OutFailureReason = bHoveredReservedEndpoint
			? TEXT("The hovered continuation endpoint is reserved by an in-flight or waiting route. Wait for completion or cancel that preview before selecting it.")
			: TEXT("No compatible exported continuation endpoint is under the hovered block. Select an unused exported entry; committed entries are no longer available.");
		return false;
	}
	Matches.Sort([&HoveredBlockWorldPos](const FResolvedLayoutConnectorEndpoint& Left, const FResolvedLayoutConnectorEndpoint& Right)
	{
		auto DistanceSquared = [&HoveredBlockWorldPos](const FResolvedLayoutConnectorEndpoint& Endpoint)
		{
			const int64 X = Endpoint.EndpointBlockWorldPos.X - HoveredBlockWorldPos.X;
			const int64 Y = Endpoint.EndpointBlockWorldPos.Y - HoveredBlockWorldPos.Y;
			return X * X + Y * Y;
		};
		const int64 LeftDistance = DistanceSquared(Left);
		const int64 RightDistance = DistanceSquared(Right);
		if (LeftDistance != RightDistance) return LeftDistance < RightDistance;
		if (Left.RootRecordKey != Right.RootRecordKey)
		{
			return Left.RootRecordKey < Right.RootRecordKey;
		}
		if (Left.SiteReservationKey != Right.SiteReservationKey)
		{
			return Left.SiteReservationKey.Y != Right.SiteReservationKey.Y
				? Left.SiteReservationKey.Y < Right.SiteReservationKey.Y
				: Left.SiteReservationKey.X < Right.SiteReservationKey.X;
		}
		if (Left.LocalCell != Right.LocalCell)
		{
			if (Left.LocalCell.Z != Right.LocalCell.Z) return Left.LocalCell.Z < Right.LocalCell.Z;
			if (Left.LocalCell.Y != Right.LocalCell.Y) return Left.LocalCell.Y < Right.LocalCell.Y;
			return Left.LocalCell.X < Right.LocalCell.X;
		}
		return Left.ConnectorTypeTag.ToString() < Right.ConnectorTypeTag.ToString();
	});
	OutEndpoint = Matches[0];
	UE_LOG(LogTemp, Display,
		TEXT("[DEBUG-layout-endpoint] Hover hit block=%s endpoint=%s face=%s root=%s reservation=%s family=%s ledger=%d compatibleReady=%d matches=%d."),
		*HoveredBlockWorldPos.ToString(),
		*OutEndpoint.EndpointBlockWorldPos.ToString(),
		*StaticEnum<ELayoutFaceDirection>()->GetNameStringByValue(static_cast<int64>(OutEndpoint.ExposedEntryFaceDirection)),
		*OutEndpoint.RootRecordKey,
		*OutEndpoint.SiteReservationKey.ToString(),
		*Family->FamilyId.ToString(),
		LedgerRecordCount,
		CompatibleReadyRecordCount,
		Matches.Num());
	return true;
}

bool UChunkWorldLayoutRuntimeComponent::TryReserveExplicitContinuationEndpointPair(
	const FResolvedLayoutConnectorEndpoint& StartEndpoint,
	const FResolvedLayoutConnectorEndpoint& EndEndpoint,
	const ULayoutWorldBindingAsset* const WorldBinding,
	const ULayoutProfileAsset* const ContinuationProfile,
	const int32 SolveSeed,
	FResolvedLayoutConnectorRecord& OutConnectorRecord,
	FString& OutEdgeKey,
	FString& OutFailureReason)
{
	check(IsInGameThread());
	OutConnectorRecord = FResolvedLayoutConnectorRecord();
	OutEdgeKey.Reset();
	OutFailureReason.Reset();
	if (!FLayoutConnectorPlanning::TryBuildContinuationRecordForEndpointPair(
			StartEndpoint, EndEndpoint, WorldBinding, ContinuationProfile, SolveSeed,
			OutConnectorRecord, OutFailureReason))
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEBUG-layout-endpoint] Explicit pair build rejected start=%s end=%s reason=%s."),
			*StartEndpoint.EndpointBlockWorldPos.ToString(), *EndEndpoint.EndpointBlockWorldPos.ToString(), *OutFailureReason);
		return false;
	}
	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	const bool bReserved = Store != nullptr && Store->ReserveContinuationEndpointPair(
		StartEndpoint,
		EndEndpoint,
		OutConnectorRecord.ContinuationFamilyId,
		OutConnectorRecord.ContinuationFamilyCandidateId,
		OutEdgeKey,
		OutFailureReason,
		true);
	if (bReserved)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[DEBUG-layout-endpoint] Explicit pair reserved family=%s candidate=%s edge=%s."),
			*OutConnectorRecord.ContinuationFamilyId.ToString(),
			*OutConnectorRecord.ContinuationFamilyCandidateId.ToString(),
			*OutEdgeKey);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DEBUG-layout-endpoint] Explicit pair reserve rejected family=%s candidate=%s reason=%s."),
			*OutConnectorRecord.ContinuationFamilyId.ToString(),
			*OutConnectorRecord.ContinuationFamilyCandidateId.ToString(),
			*OutFailureReason);
	}
	return bReserved;
}

FLayoutBackgroundSolveHandle UChunkWorldLayoutRuntimeComponent::SubmitExplicitContinuationLayoutSolve(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const ULayoutWorldBindingAsset* const WorldBinding,
	const FString& EdgeKey,
	TFunction<void(const FLayoutExplicitContinuationSolveResult&)> OnCompleted)
{
	check(IsInGameThread());
	const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
	if (EdgeKey.IsEmpty() || ResolvedConnectorRecords.Contains(ConnectorKey) || PendingExplicitPreparedRoutes.Contains(ConnectorKey))
	{
		if (!EdgeKey.IsEmpty())
		{
			if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore()) Store->ReleaseContinuationEndpointPair(EdgeKey);
		}
		if (OnCompleted)
		{
			FLayoutExplicitContinuationSolveResult Result;
			Result.FailureReason = TEXT("Continuation endpoint pair already has a resolved or pending connector.");
			OnCompleted(Result);
		}
		return FLayoutBackgroundSolveHandle();
	}
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	FLayoutConnectorTerrainPathContext TerrainPathContext;
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr
		|| !ActiveBiomeSampler.Initialize(this, ChunkWorld->WorldGenDef, ResolveLayoutWorldSeed()))
	{
		if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore()) Store->ReleaseContinuationEndpointPair(EdgeKey);
		if (OnCompleted)
		{
			FLayoutExplicitContinuationSolveResult Result;
			Result.FailureReason = TEXT("Explicit continuation requires an active chunk-world biome sampler.");
			OnCompleted(Result);
		}
		return FLayoutBackgroundSolveHandle();
	}
	TerrainPathContext.ActiveBiomeSampler = &ActiveBiomeSampler;
	TerrainPathContext.CoordinateSettings = MakeCoordinateSettings(ChunkWorld->WorldGenDef);
	TerrainPathContext.RootFootprints = CaptureContinuationRootFootprints();
	FLayoutPreparedContinuationRoute PreparedRoute;
	FString FailureReason;
	if (!FLayoutConnectorPlanning::TryPrepareContinuationRoute(
			ConnectorRecord,
			WorldBinding,
			ResolveLayoutWorldSeed(),
			&TerrainPathContext,
			PreparedRoute,
			FailureReason))
	{
		if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore()) Store->ReleaseContinuationEndpointPair(EdgeKey);
		if (OnCompleted)
		{
			FLayoutExplicitContinuationSolveResult Result;
			Result.FailureReason = FailureReason;
			OnCompleted(Result);
		}
		return FLayoutBackgroundSolveHandle();
	}
	TArray<uint64> SegmentKeys;
	for (const FLayoutPreparedContinuationRouteSegment& Segment : PreparedRoute.Segments)
	{
		const uint64 SegmentKey = BuildResolvedConnectorRecordKey(Segment.ConnectorRecord);
		SegmentKeys.Add(SegmentKey);
		if (Segment.PreparedContinuation.IsSet())
		{
			PendingExplicitPreparedSegmentsByConnectorKey.Add(
				SegmentKey,
				Segment.PreparedContinuation.GetValue());
		}
	}
	if (SegmentKeys.IsEmpty())
	{
		if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore()) Store->ReleaseContinuationEndpointPair(EdgeKey);
		if (OnCompleted)
		{
			FLayoutExplicitContinuationSolveResult Result;
			Result.FailureReason = TEXT("Explicit continuation route contains no segment descriptors.");
			OnCompleted(Result);
		}
		return FLayoutBackgroundSolveHandle();
	}
	const FLayoutId RouteId(*PreparedRoute.Route.RouteKey);
	RegisterContinuationRouteReservation(RouteId, EdgeKey, SegmentKeys);
	for (const uint64 SegmentKey : SegmentKeys) ExplicitPreviewConnectorKeys.Add(SegmentKey);
	RetainedPreparedContinuationRoutesById.Add(RouteId, PreparedRoute);
	PendingExplicitPreparedRoutes.Add(ConnectorKey, MakeShared<FLayoutPreparedContinuationRoute>(MoveTemp(PreparedRoute)));
	ExplicitRouteSegmentKeysByConnectorKey.Add(ConnectorKey, SegmentKeys);
	ExplicitRoutesByConnectorKey.Add(ConnectorKey, PendingExplicitPreparedRoutes.FindChecked(ConnectorKey)->Route);
	UE_LOG(LogTemp, Display, TEXT("[DEBUG-layout-endpoint] Explicit continuation route queued connector=%llu edge=%s segments=%d."), ConnectorKey, *EdgeKey, SegmentKeys.Num());
	if (OnCompleted)
	{
		ExplicitContinuationCompletionCallbacks.Add(ConnectorKey, MoveTemp(OnCompleted));
	}
	RefreshConnectorRecords();
	return PendingConnectorRefreshSolveHandles.IsEmpty()
		? FLayoutBackgroundSolveHandle()
		: PendingConnectorRefreshSolveHandles.Last();
}

void UChunkWorldLayoutRuntimeComponent::CancelExplicitContinuationLayoutSolve(
	const FResolvedLayoutConnectorRecord& ConnectorRecord)
{
	check(IsInGameThread());
	const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
	FLayoutId RouteId = ConnectorRecord.ContinuationRouteId;
	if (RouteId.IsNone())
	{
		if (const FLayoutContinuationRouteRecord* Route = ExplicitRoutesByConnectorKey.Find(ConnectorKey)) RouteId = FLayoutId(*Route->RouteKey);
	}
	TArray<uint64> ConnectorKeys{ConnectorKey};
	for (auto It = ExplicitRoutesByConnectorKey.CreateIterator(); It; ++It)
	{
		if (It.Key() != ConnectorKey && (RouteId.IsNone() || FLayoutId(*It.Value().RouteKey) != RouteId)) continue;
		const uint64 RequestKey = It.Key();
		if (const TArray<uint64>* Segments = ExplicitRouteSegmentKeysByConnectorKey.Find(RequestKey))
		{
			for (const uint64 Key : *Segments) ConnectorKeys.AddUnique(Key);
		}
		PendingExplicitPreparedRoutes.Remove(RequestKey);
		ExplicitRouteSegmentKeysByConnectorKey.Remove(RequestKey);
		ExplicitContinuationCompletionCallbacks.Remove(RequestKey);
		It.RemoveCurrent();
	}
	if (!RouteId.IsNone())
	{
		if (const FLayoutContinuationRouteReservationState* State = ContinuationRouteReservations.Find(RouteId))
		{
			for (const uint64 Key : State->SegmentKeys) ConnectorKeys.AddUnique(Key);
		}
		ReleaseContinuationRouteReservation(RouteId);
	}
	else ReleaseContinuationReservationForConnector(ConnectorKey);
	for (const uint64 Key : ConnectorKeys)
	{
		if (const FLayoutBackgroundSolveHandle* Handle = PendingConnectorRefreshSolveHandles.FindByPredicate(
			[Key](const FLayoutBackgroundSolveHandle& Candidate)
			{
				return Candidate.LayoutGroupId == Key;
			}))
		{
			const FLayoutBackgroundSolveHandle OwnedHandle = *Handle;
			CancelBackgroundLayoutSolve(OwnedHandle);
		}
		PendingExplicitPreparedSegmentsByConnectorKey.Remove(Key);
		ExplicitPreviewConnectorKeys.Remove(Key);
		if (!ExplicitConnectorKeys.Contains(Key))
		{
			ResolvedConnectorRecords.Remove(Key);
			ResolvedConnectorFrozenTerrainContracts.Remove(Key);
		}
	}
}

bool UChunkWorldLayoutRuntimeComponent::TryApplySolvedExplicitConnector(
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const FLayoutFrozenTerrainContract& FrozenTerrainContract,
	FString* const OutFailureReason)
{
	check(IsInGameThread());
	const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
	if (ExplicitConnectorKeys.Contains(ConnectorKey))
	{
		if (OutFailureReason != nullptr) OutFailureReason->Reset();
		return true;
	}
	if (FrozenTerrainContract.ContractId.IsNone())
	{
		if (OutFailureReason != nullptr) *OutFailureReason = TEXT("Explicit continuation apply requires an accepted frozen terrain contract.");
		return false;
	}
	if (!ContinuationEdgeKeysByConnectorKey.Contains(ConnectorKey))
	{
		if (OutFailureReason != nullptr) *OutFailureReason = TEXT("Explicit continuation apply requires its active planning-window edge reservation.");
		return false;
	}

	FResolvedLayoutConnectorRecord* CachedRecord = ResolvedConnectorRecords.Find(ConnectorKey);
	const bool bAddedCachedRecord = CachedRecord == nullptr;
	if (CachedRecord != nullptr)
	{
		const FResolvedLayoutConnectorRuntimeState RuntimeState =
			CachedRecord->GetResolvedConnectorRuntimeState();
		if (RuntimeState.bLayoutRealized || RuntimeState.bHasBeenCommittedToChunkWorld)
		{
			if (OutFailureReason != nullptr) *OutFailureReason = TEXT("Continuation endpoint pair is already realized.");
			return false;
		}
		if (const FLayoutFrozenTerrainContract* const CachedContract =
				ResolvedConnectorFrozenTerrainContracts.Find(ConnectorKey);
			CachedContract != nullptr && CachedContract->ContractId != FrozenTerrainContract.ContractId)
		{
			if (OutFailureReason != nullptr) *OutFailureReason = TEXT("Explicit continuation apply rejected a stale frozen terrain contract.");
			return false;
		}
	}
	else
	{
		CachedRecord = &ResolvedConnectorRecords.Add(ConnectorKey, ConnectorRecord);
	}
	ResolvedConnectorFrozenTerrainContracts.Add(ConnectorKey, FrozenTerrainContract);
	for (const FIntVector& ChunkOrigin : CollectRequiredChunkOrigins(*CachedRecord))
	{
		RecordFinestLoadedChunk(ChunkOrigin);
	}
	FString FailureReason;
	if (!TryRealizeConnector(*CachedRecord, &FailureReason, &FrozenTerrainContract))
	{
		ReleaseContinuationReservationForConnector(ConnectorKey);
		ExplicitPreviewConnectorKeys.Remove(ConnectorKey);
		if (bAddedCachedRecord)
		{
			ResolvedConnectorRecords.Remove(ConnectorKey);
			ResolvedConnectorFrozenTerrainContracts.Remove(ConnectorKey);
		}
		if (OutFailureReason != nullptr) *OutFailureReason = FailureReason;
		return false;
	}
	MarkResolvedConnectorRealizedAndCommitted(*CachedRecord);
	if (!ConsumeContinuationReservationForConnector(ConnectorKey))
	{
		if (OutFailureReason != nullptr) *OutFailureReason = TEXT("Continuation writes committed but endpoint reservation could not be consumed.");
		return false;
	}
	ExplicitPreviewConnectorKeys.Remove(ConnectorKey);
	ExplicitConnectorKeys.Add(ConnectorKey);
	return true;
}

#if WITH_AUTOMATION_TESTS
bool UChunkWorldLayoutRuntimeComponent::TrySolveExplicitRootLayoutSite(
	const FIntVector& SiteCenterBlockWorldPos,
	ULayoutProfileAsset* const LayoutProfile,
	const int32 SolveSeed,
	FResolvedLayoutSiteRecord& OutSiteRecord,
	FLayoutRegionSolveScheduleResult& OutScheduleResult,
	FString* const OutFailureReason)
{
	ULayoutRegionContentSetAsset* const EffectiveContentSet =
		LayoutWorldBindingRuntimeHelpers::ResolveRuntimePreferredContentSet(LayoutProfile);
	// Tests that exercise ExplicitRoot without a real WorldBinding synthesize a
	// runtime view with placeholder ContentSet/Profile so the solve pipeline
	// has valid snapshots to consume.
	// submits when no binding owns solve-budget or placement authority.
	const FLayoutWorldBindingRuntimeView ExplicitRuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			LayoutProfile, EffectiveContentSet, FLayoutRootSolveBudgetSettings(),
			FLayoutWorldBindingPlacementPolicy());
	return TrySolveExplicitRootLayoutSite(
		SiteCenterBlockWorldPos,
		ExplicitRuntimeView,
		SolveSeed,
		OutSiteRecord,
		OutScheduleResult,
		OutFailureReason,
		nullptr);
}

bool UChunkWorldLayoutRuntimeComponent::TrySolveExplicitRootLayoutSite(
	const FIntVector& RequestedSiteCenterBlockWorldPos,
	const ULayoutWorldBindingAsset* const WorldBinding,
	ULayoutProfileAsset* const LayoutProfile,
	const int32 SolveSeed,
	FResolvedLayoutSiteRecord& OutSiteRecord,
	FLayoutRegionSolveScheduleResult& OutScheduleResult,
	FString* const OutFailureReason)
{
	FLayoutWorldBindingRuntimeView RuntimeView;
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FString LocalFailureReason;
	if (!LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRootRuntimeViewFromWorldBindingProfile(
		WorldBinding,
		LayoutProfile,
		RuntimeView,
		FrontendSelection,
		LocalFailureReason))
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = LocalFailureReason;
		}
		OutSiteRecord = FResolvedLayoutSiteRecord();
		OutScheduleResult = FLayoutRegionSolveScheduleResult();
		return false;
	}

	FrontendSelection.BiomeRowName = NAME_None;
	FrontendSelection.CompatibleBiomeRowNames.Reset();
	// Match the editor facade: binding policy does not restrict manually selected terrain.
	FrontendSelection.bUseAnyActiveBiomeSurface = WorldBinding != nullptr;
	const FIntVector ResolvedSiteCenterBlockWorldPos =
		RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None
			? FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
				RequestedSiteCenterBlockWorldPos,
				RuntimeView.SharedCellSizeInBlocks)
			: RequestedSiteCenterBlockWorldPos;

	return TrySolveExplicitRootLayoutSite(
		ResolvedSiteCenterBlockWorldPos,
		RuntimeView,
		SolveSeed,
		OutSiteRecord,
		OutScheduleResult,
		OutFailureReason,
		&FrontendSelection);
}
#endif

FLayoutBackgroundSolveHandle UChunkWorldLayoutRuntimeComponent::SubmitExplicitRootLayoutSiteSolve(
	const FIntVector& SiteCenterBlockWorldPos,
	const FLayoutWorldBindingRuntimeView& RuntimeView,
	const int32 SolveSeed,
	TFunction<void(bool bSucceeded, const FResolvedLayoutSiteRecord& SiteRecord, const FLayoutRegionSolveScheduleResult& ScheduleResult, const FString& FailureReason)> OnCompleted,
	const FLayoutWorldBindingSiteFrontendSelection* const FrontendSelection)
{
	check(IsInGameThread());
	const double OperationStartSeconds = FPlatformTime::Seconds();

	const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	const int32 WorldSeed = ResolveLayoutWorldSeed();
	const FIntVector ResolvedSiteCenterBlockWorldPos =
		RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None
			? FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
				SiteCenterBlockWorldPos,
				RuntimeView.SharedCellSizeInBlocks)
			: SiteCenterBlockWorldPos;

	FLayoutRegionSolveRequest SolveRequest;
	FString LocalFailureReason;
	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	const bool bInitializedActiveBiomeSampler =
		ChunkWorld != nullptr
		&& ChunkWorld->WorldGenDef != nullptr
		&& ActiveBiomeSampler.Initialize(this, ChunkWorld->WorldGenDef, WorldSeed);
	// Manual any-active placement must not inherit the binding's automatic-placement filter.
	const bool bUseAnyActiveBiomeSurface = FrontendSelection != nullptr && FrontendSelection->bUseAnyActiveBiomeSurface;
	TArray<FName> AnyActiveBiomeRows;
	if (bUseAnyActiveBiomeSurface && bInitializedActiveBiomeSampler)
	{
		FLayoutActiveBiomeRowRef Row;
		for (int32 SourceIndex = 0; ActiveBiomeSampler.TryGetRowRefForSourceIndex(SourceIndex, Row); ++SourceIndex)
		{
			if (!Row.RowName.IsNone())
			{
				AnyActiveBiomeRows.AddUnique(Row.RowName);
			}
		}
	}
	FName EffectiveEligibleBiomeRowName = bUseAnyActiveBiomeSurface
		? (AnyActiveBiomeRows.IsEmpty() ? NAME_None : AnyActiveBiomeRows[0])
		: RuntimeView.MatchingBiomeRowName;
	if (!bUseAnyActiveBiomeSurface && EffectiveEligibleBiomeRowName.IsNone() && bInitializedActiveBiomeSampler)
	{
		FLayoutActiveBiomeSample ActiveBiomeSample;
		if (ActiveBiomeSampler.SampleAtBlockPosition(
				ResolvedSiteCenterBlockWorldPos,
				MakeCoordinateSettings(ChunkWorld->WorldGenDef),
				ActiveBiomeSample)
			&& ActiveBiomeSample.bIsValid)
		{
			EffectiveEligibleBiomeRowName = ActiveBiomeSample.WinningRow.RowName;
		}
	}
	if (!LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
		RuntimeView,
		ResolvedSiteCenterBlockWorldPos,
		SolveSeed,
		SolveRequest,
		LocalFailureReason))
	{
		if (OnCompleted)
		{
			OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(), LocalFailureReason);
		}
		return FLayoutBackgroundSolveHandle();
	}

	// Include terrain preparation and queue time in the same allowance as worker
	// prewarm/proof/recovery. This state stays outside frozen manifests and proofs.
	const auto OperationLedger = MakeShared<LayoutSolveExecution::FWorkLedger, ESPMode::ThreadSafe>();
	OperationLedger->MaxWorkUnits = FMath::Max(1, SolveRequest.ExecutionSettings.MaxCandidateAttempts);
	OperationLedger->DeadlineSeconds = SolveRequest.ExecutionSettings.MaxSolveDurationSeconds > 0.0
		? OperationStartSeconds + SolveRequest.ExecutionSettings.MaxSolveDurationSeconds : 0.0;
	TOptional<LayoutSolveExecution::FScope> PreSubmitExecutionScope;
	PreSubmitExecutionScope.Emplace(*OperationLedger);
	if (GetDetailedDiagnostics())
	{
		OperationLedger->DiagnosticContext = FString::Printf(
			TEXT("origin=explicit request=%s profile=%s site=(%s) seed=%d footprint=%s cellSize=%s"),
			*FGuid::NewGuid().ToString(EGuidFormats::Digits), *GetPathNameSafe(RuntimeView.LayoutProfile),
			*ResolvedSiteCenterBlockWorldPos.ToString(), SolveSeed, *SolveRequest.FootprintSize.ToString(),
			*RuntimeView.SharedCellSizeInBlocks.ToString());
	}
	LayoutSolveExecution::FDiagnosticScope PreparationDiagnostics(
		OperationLedger->DiagnosticContext, TEXT("site-preparation"), &LocalFailureReason, 0.0, &OperationLedger.Get());

	const FString WorkerDebugName = FString::Printf(TEXT("ExplicitRoot %s"), *ResolvedSiteCenterBlockWorldPos.ToString());
	FLayoutWorkerSolvePacket WorkerSolvePacket = FLayoutWorkerSolvePacket::CaptureExplicitPreviewRoot(
		WorkerDebugName,
		RuntimeView,
		ResolvedSiteCenterBlockWorldPos,
		SolveSeed,
		0,
		EffectiveEligibleBiomeRowName);
	WorkerSolvePacket.RequestManifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(SolveRequest);
	WorkerSolvePacket.bHasRequestManifest = true;
	// Compute mode plan inline from the solve request.
	{
		FLayoutContractModeSelectionInput ModeInput;
		ModeInput.SolveRequest = &SolveRequest;
		ModeInput.SiteCenterBlockWorldPos = ResolvedSiteCenterBlockWorldPos;
		ModeInput.WorldSeed = ResolveLayoutWorldSeed();
		WorkerSolvePacket.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
	}
	WorkerSolvePacket.bHasSelectedModePlan = true;
	WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = true;
	WorkerSolvePacket.RequestManifest.SelectedModePlan = WorkerSolvePacket.SelectedModePlan;
	if (!OperationLedger->DiagnosticContext.IsEmpty())
	{
		UE_LOG(LogTemp, Display, TEXT("[LayoutSolveDiag] %s event=request mode=%d maxSeconds=%.3f maxWork=%d"),
			*OperationLedger->DiagnosticContext, static_cast<int32>(WorkerSolvePacket.SelectedModePlan.EnvironmentMode),
			SolveRequest.ExecutionSettings.MaxSolveDurationSeconds, SolveRequest.ExecutionSettings.MaxCandidateAttempts);
	}

	// Build terrain evidence on the game thread before the lifecycle chain starts.
	// The prewarm consumes this pointer-free evidence from the WorkerSolvePacket manifest.
	if (RuntimeView.LayoutProfile != nullptr && ChunkWorld != nullptr)
	{
		const FIntVector CellSize = RuntimeView.SharedCellSizeInBlocks;
		const FIntPoint FootprintCells = SolveRequest.FootprintSize;
		const FLayoutId TerrainArtifactId = FLayoutId(*FString::Printf(TEXT("TerrainBiome.%s"), *WorkerDebugName));

		bool bCompleteInitialSurfaceEvidence = true;
		auto BuildEvidence = [&](const FIntVector& Site) -> FLayoutFrozenTerrainBiomeAdapterInput
		{
			const FIntVector SiteFootprintMin = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
				Site, FootprintCells, CellSize);
			FLayoutFrozenTerrainBiomeAdapterInput Evidence = BuildTerrainBiomeAdapterInput(
				TerrainArtifactId,
				WorkerSolvePacket.SelectedModePlan.ModePlanId,
				EffectiveEligibleBiomeRowName,
				Site,
				SiteFootprintMin,
				CellSize,
				FootprintCells,
				[&, Site,
					bUseSelectedMaterialPacket = WorkerSolvePacket.SelectedModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot](int32 BlockX, int32 BlockY) -> int32
				{
					if (bUseSelectedMaterialPacket)
					{
						return Site.Z;
					}
					FLayoutActiveBiomeSurfaceSample Sample;
					if (!bInitializedActiveBiomeSampler
						|| !ActiveBiomeSampler.FindAnyActiveBiomeSurface(
							FIntPoint(BlockX, BlockY),
							RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
							RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
							MakeCoordinateSettings(ChunkWorld->WorldGenDef), Sample)
						|| !Sample.bIsValid)
					{
						bCompleteInitialSurfaceEvidence = false;
						return Site.Z; // Discard this provisional packet below.
					}
					return Sample.SurfaceBlockWorldPos.Z;
				},
				RuntimeView.PlacementPolicy.TerrainTransition.MaxFoundationDepth);
			if (bUseAnyActiveBiomeSurface)
			{
				Evidence.EligibleBiomeRowNames = AnyActiveBiomeRows;
			}
			return Evidence;
		};

		// Explicit-root and Planning Window callers share one selected-site producer.
		auto AttachSelectedSiteTerrainEvidence = [&](
			const FIntVector& Site,
			FLayoutFrozenTerrainBiomeAdapterInput& InOutEvidence) -> bool
		{
			const FIntVector FootprintMin = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
				Site, FootprintCells, CellSize);
			FString SelectedSiteFailureReason;
			const bool bPrepared = TryPrepareOrdinaryRootSelectedSiteTerrain(
				GetDetailedDiagnostics(),
				Site,
				FootprintMin,
				FootprintCells,
				CellSize,
				RuntimeView.LayoutProfile->LevelCount,
				RuntimeView.PlacementPolicy,
				bInitializedActiveBiomeSampler ? &ActiveBiomeSampler : nullptr,
				ChunkWorld->WorldGenDef != nullptr
					? MakeCoordinateSettings(ChunkWorld->WorldGenDef)
					: FLayoutNoiseCoordinateSettings(),
				InOutEvidence,
				SelectedSiteFailureReason);
			if (!bPrepared && !SelectedSiteFailureReason.IsEmpty())
			{
				InOutEvidence.AuditMessages.AddUnique(SelectedSiteFailureReason);
			}
			return bPrepared;
		};

		auto CountWalkableBoundaryCells = [&](const FLayoutFrozenTerrainBiomeAdapterInput& Input) -> int32
		{
			int32 Count = 0;
			for (const FLayoutTerrainPlacementCellEvidence& Cell : Input.TerrainPlacementCells)
			{
				if ((Cell.Cell.X == 0 || Cell.Cell.X == FootprintCells.X - 1
					|| Cell.Cell.Y == 0 || Cell.Cell.Y == FootprintCells.Y - 1)
					&& Cell.bPlaceableForSelectedMode
					&& (Cell.EntryTraversability == ELayoutEntryTraversabilityVerdict::Walkable
						|| Cell.EntryTraversability == ELayoutEntryTraversabilityVerdict::RampNeeded
						|| Cell.EntryTraversability == ELayoutEntryTraversabilityVerdict::None
						|| (Cell.EntryTraversability == ELayoutEntryTraversabilityVerdict::ExcavationNeeded
							&& Cell.bHasExcavationEvidence
							&& Cell.bHasLocalOverlapZ)))
				{
					++Count;
				}
			}
			return Count;
		};

		FLayoutFrozenTerrainBiomeAdapterInput TerrainEvidence = BuildEvidence(ResolvedSiteCenterBlockWorldPos);
		if (!bCompleteInitialSurfaceEvidence)
		{
			if (OnCompleted)
			{
				OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(),
					TEXT("Explicit layout lacks biome-noise surface evidence within the configured search bounds."));
			}
			return FLayoutBackgroundSolveHandle();
		}
		bool bSelectedSiteIsCavity = false;
		if (WorkerSolvePacket.SelectedModePlan.PlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot)
		{
			const bool bPreparedSelectedSite = AttachSelectedSiteTerrainEvidence(
				ResolvedSiteCenterBlockWorldPos,
				TerrainEvidence);
			if (bInitializedActiveBiomeSampler)
			{
				bool bCenterUnderground = false;
				if (TryResolveEnvironmentCenterColumnMode(
						ActiveBiomeSampler,
						MakeCoordinateSettings(ChunkWorld->WorldGenDef),
						RuntimeView.SharedCellSizeInBlocks,
						RuntimeView.PlacementPolicy,
						RuntimeView.LayoutProfile != nullptr ? RuntimeView.LayoutProfile->LevelCount : 1,
						ResolvedSiteCenterBlockWorldPos,
						bCenterUnderground))
				{
					TerrainEvidence.bHasRelativeEnvironmentClassification = true;
					TerrainEvidence.bIsClassifiedUnderground = bCenterUnderground;
				}
			}
			if (!bPreparedSelectedSite)
			{
				if (OnCompleted)
				{
					OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(),
						TerrainEvidence.AuditMessages.IsEmpty()
							? TEXT("Selected-site terrain preparation failed before profile admission.")
							: TerrainEvidence.AuditMessages.Last());
				}
				return FLayoutBackgroundSolveHandle();
			}
			bSelectedSiteIsCavity = TerrainEvidence.bHasRelativeEnvironmentClassification
				&& TerrainEvidence.bIsClassifiedUnderground;
			if (RuntimeView.LayoutProfile->bUndergroundPlacement != bSelectedSiteIsCavity)
			{
				if (OnCompleted)
				{
					OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(),
						RuntimeView.LayoutProfile->bUndergroundPlacement
							? TEXT("Layout profile only supports underground placement.")
							: TEXT("Layout profile only supports surface placement."));
				}
				return FLayoutBackgroundSolveHandle();
			}
		}

		if (RuntimeView.LayoutProfile->bSupportsSteppedTerrainSolve)
		{
			FString SteppedSupportFailureReason;
			if (!FLayoutTerrainSampling::TryAugmentSelectedComponentWithSteppedTerrainEvidence(
					bSelectedSiteIsCavity,
					FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
						ResolvedSiteCenterBlockWorldPos, FootprintCells, CellSize),
					CellSize,
					FootprintCells,
					RuntimeView.PlacementPolicy.TerrainTransition,
					SolveRequest.SteppedTerrainSupportMap,
					TerrainEvidence,
					SteppedSupportFailureReason))
			{
				TerrainEvidence.AuditMessages.Add(FString::Printf(
					TEXT("SelectedSiteFlatFallback: natural stepped support was incomplete: %s"),
					*SteppedSupportFailureReason));
			}
		}
		FLayoutTerrainSampling::ApplySelectedComponentPerimeterRampEvidence(
			FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
				ResolvedSiteCenterBlockWorldPos, FootprintCells, CellSize),
			CellSize,
			FootprintCells,
			RuntimeView.PlacementPolicy.TerrainTransition,
			TerrainEvidence);

		if (TerrainEvidence.TerrainPlacementCells.IsEmpty() || CountWalkableBoundaryCells(TerrainEvidence) == 0)
		{
			const FString RejectionReason = FString::Printf(
				TEXT("Explicit-root solve rejected at selected site %s: no boundary cell has clear or policy-authorized occluded entry traversability."),
				*ResolvedSiteCenterBlockWorldPos.ToString());
			if (OnCompleted)
			{
				OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(), RejectionReason);
			}
			return FLayoutBackgroundSolveHandle();
		}

		// Cavity support is produced after initial packet capture; preserve it for
		// worker prewarm so Underground shares the surface StageMap/CSP path.
		WorkerSolvePacket.RequestManifest.SteppedTerrainSupportMap = SolveRequest.SteppedTerrainSupportMap;
		WorkerSolvePacket.RequestManifest.bHasFrozenTerrainBiomeAdapterInput = true;
		WorkerSolvePacket.RequestManifest.FrozenTerrainBiomeAdapterInput = MoveTemp(TerrainEvidence);
		if (WorkerSolvePacket.RequestManifest.PlannedCells.IsEmpty())
		{
			FString AuthoredPlanFailureReason;
			FLayoutProfileSolveSnapshot AuthoredPlanProfile = WorkerSolvePacket.RequestManifest.ProfileSnapshot;
			const bool bHasSteppedTerrainStages = WorkerSolvePacket.RequestManifest.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells.ContainsByPredicate(
				[](const FLayoutTerrainPlacementCellEvidence& CellEvidence)
				{
					return CellEvidence.TerrainStageIndex != 0;
				});
			if (AuthoredPlanProfile.bSupportsSteppedTerrainSolve && bHasSteppedTerrainStages)
			{
				// Local project fix: reserve voids against projected stepped topology during finalization, not this flat authored plan.
				AuthoredPlanProfile.ReservedOpenSpaceRules.Reset();
			}
			TArray<FLayoutCellReservationRecord> AuthoredPlanReservations;
			if (!LayoutProfileSolverInternal::BuildAuthoredPlan(
				AuthoredPlanProfile,
				WorkerSolvePacket.RequestManifest.ModuleCatalog,
				SolveSeed,
				FootprintCells,
				WorkerSolvePacket.RequestManifest.PlannedCells,
				AuthoredPlanFailureReason,
				&AuthoredPlanReservations))
			{
				if (OnCompleted)
				{
					OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(), AuthoredPlanFailureReason);
				}
				return FLayoutBackgroundSolveHandle();
			}
			// Preserve authored flat reserved-open realization authority through adapter prewarm.
			WorkerSolvePacket.RequestManifest.ReservedOpenTerrainReservations.Reset();
			for (const FLayoutCellReservationRecord& Reservation : AuthoredPlanReservations)
			{
				if (Reservation.ReservationKind == ELayoutCellReservationKind::ReservedEmpty)
				{
					WorkerSolvePacket.RequestManifest.ReservedOpenTerrainReservations.Add(Reservation);
				}
			}
			WorkerSolvePacket.RequestManifest.FootprintSize = FootprintCells;
		}
	}

	FString WorkerSnapshotFailureReason;
	if (!WorkerSolvePacket.ValidateNoLiveObjectCarriers(WorkerSnapshotFailureReason))
	{
		if (OnCompleted)
		{
			OnCompleted(false, FResolvedLayoutSiteRecord(), FLayoutRegionSolveScheduleResult(), WorkerSnapshotFailureReason);
		}
		return FLayoutBackgroundSolveHandle();
	}


	struct FExplicitRootBackgroundSolveSharedResult
	{
		FLayoutRegionSolveScheduleResult ScheduleResult;
		FLayoutSolvedArtifact SolvedArtifact;
		FLayoutRegionSolveRequest FinalizedRequest;
		FLayoutFrozenTerrainContract FrozenTerrainContract;
		FString FailureReason;
	};
	TSharedRef<FExplicitRootBackgroundSolveSharedResult, ESPMode::ThreadSafe> SharedResult =
		MakeShared<FExplicitRootBackgroundSolveSharedResult, ESPMode::ThreadSafe>();
	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> bCompletionDelivered =
		MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	using FExplicitRootCompletion = TFunction<void(
		bool,
		const FResolvedLayoutSiteRecord&,
		const FLayoutRegionSolveScheduleResult&,
		const FString&)>;
	const TSharedRef<FExplicitRootCompletion, ESPMode::ThreadSafe> CompletionGate =
		MakeShared<FExplicitRootCompletion, ESPMode::ThreadSafe>();
	*CompletionGate = [bCompletionDelivered, ResolvedSiteCenterBlockWorldPos, OnCompleted = MoveTemp(OnCompleted)](
		const bool bSucceeded,
		const FResolvedLayoutSiteRecord& SiteRecord,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		const FString& FailureReason) mutable
	{
		if (static_cast<bool>(*bCompletionDelivered))
		{
			return;
		}
		*bCompletionDelivered = true;
		if (OnCompleted)
		{
			if (!bSucceeded && !SiteRecord.bLayoutSolved)
			{
				// Retain attempted spatial metadata even when prewarm has no solved site.
				// Diagnostic schedule data stays separate from apply/publication authority.
				FResolvedLayoutSiteRecord RejectedSite = SiteRecord;
				FResolvedLayoutSiteLocationMetadata Location = RejectedSite.GetResolvedSiteLocationMetadata();
				Location.SiteCenterBlockWorldPos = ResolvedSiteCenterBlockWorldPos;
				RejectedSite.SetResolvedSiteLocationMetadata(Location);
				OnCompleted(false, RejectedSite, ScheduleResult, FailureReason);
			}
			else
			{
				OnCompleted(bSucceeded, SiteRecord, ScheduleResult, FailureReason);
			}
		}
	};
	TSharedRef<FLayoutWorkerSolvePacket> SharedWorkerPacket = MakeShared<FLayoutWorkerSolvePacket>(MoveTemp(WorkerSolvePacket));
	const FLayoutId CapturedSolvedArtifactId = BuildRuntimeSolvedArtifactId(FLayoutId(*WorkerDebugName));
	const TSoftObjectPtr<ULayoutRegionContentSetAsset> CapturedContentSet = RuntimeView.ContentSet;
	const TOptional<FLayoutWorldBindingSiteFrontendSelection> CapturedFrontendSelection =
		FrontendSelection != nullptr
			? TOptional<FLayoutWorldBindingSiteFrontendSelection>(*FrontendSelection)
			: TOptional<FLayoutWorldBindingSiteFrontendSelection>();
	const uint64 LayoutGroupId = static_cast<uint64>(GetTypeHash(ResolvedSiteCenterBlockWorldPos))
		^ (static_cast<uint64>(static_cast<uint32>(SolveSeed)) << 32)
		^ static_cast<uint64>(GetTypeHash(SharedWorkerPacket->SelectedModePlan.ModePlanId));
#if WITH_AUTOMATION_TESTS
	const TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> CapturedExplicitPreviewWorkGate = ExplicitPreviewWorkGateForTesting;
#endif

	// Wrap the solve submission in a factory so the lifecycle sequencer can build
	// it after preflight completes. This mirrors the planning-window pattern.
	FLayoutFrozenSolveSubmissionFactory SolveFactory = [
		this,
		SharedResult,
		SharedWorkerPacket,
		CapturedContentSet,
		CapturedFrontendSelection,
		CapturedSolvedArtifactId,
		ResolvedSiteCenterBlockWorldPos,
		SolveSeed,
		LayoutGroupId,
		CompletionGate
#if WITH_AUTOMATION_TESTS
		, CapturedExplicitPreviewWorkGate
#endif
	](const FLayoutBackgroundSolveCompletion& /*PreflightCompletion*/,
	   const FLayoutBackgroundAdmissibilityPreflightResult& /*PreflightResult*/,
	   FLayoutBackgroundSolveSubmission& OutSubmission,
	   FString& OutFailureReason) mutable
	{
		OutSubmission.DebugName = TEXT("ExplicitRoot");
		OutSubmission.LayoutGroupId = LayoutGroupId;
		OutSubmission.Tier = ELayoutBackgroundSolveJobTier::ExplicitPreview;
		OutSubmission.Priority = 0;
		OutSubmission.Work = [SharedResult, SharedWorkerPacket, CapturedSolvedArtifactId
#if WITH_AUTOMATION_TESTS
			, CapturedExplicitPreviewWorkGate
#endif
		](const FLayoutSolveCancellationToken& CancellationToken, FString& OutWorkFailureReason) mutable
		{
#if WITH_AUTOMATION_TESTS
			while (CapturedExplicitPreviewWorkGate.IsValid()
				&& !static_cast<bool>(*CapturedExplicitPreviewWorkGate)
				&& !CancellationToken.IsCancellationRequested())
			{
				FPlatformProcess::Sleep(0.005f);
			}
#endif
			if (CancellationToken.IsCancellationRequested())
			{
				OutWorkFailureReason = TEXT("Explicit-root background solve canceled before proof.");
				return false;
			}

			const bool bSolveWorkSucceeded = RunSharedRootSolveWork(
					(*SharedWorkerPacket),
					CapturedSolvedArtifactId,
					SharedResult->ScheduleResult,
					SharedResult->SolvedArtifact,
					OutWorkFailureReason,
					&SharedResult->FinalizedRequest);

			SharedResult->FrozenTerrainContract = SharedResult->FinalizedRequest.PrecomputedFrozenTerrainContract;
			SharedResult->FrozenTerrainContract.ActiveCells =
				SharedResult->FinalizedRequest.PrecomputedActiveCells;

			const bool bHasRetainedPartial =
				!bSolveWorkSucceeded
				&& HasRetainedPartialPlacements(SharedResult->ScheduleResult.MergedSolveResult);
			if (!bSolveWorkSucceeded && !bHasRetainedPartial)
			{
				return false;
			}

			FString AttachFailureReason;
			if (!LayoutSolvedArtifact::TryAttachActiveCellProvenance(
					SharedResult->FrozenTerrainContract.ActiveCells,
					SharedResult->SolvedArtifact,
					AttachFailureReason))
			{
				OutWorkFailureReason = AttachFailureReason;
				return false;
			}

			if (!bHasRetainedPartial)
			{
				InvalidatePlacementlessWorldFacingScheduleResult(
					SharedResult->ScheduleResult,
					(*SharedWorkerPacket).DebugName);
			}

			if (CancellationToken.IsCancellationRequested())
			{
				OutWorkFailureReason = TEXT("Explicit-root background solve canceled after proof.");
				return false;
			}

			return bSolveWorkSucceeded || bHasRetainedPartial;
		};

		OutSubmission.PublishOnGameThread = [
			this,
			SharedResult,
			SharedWorkerPacket,
			CapturedContentSet,
			CapturedFrontendSelection,
			CapturedSolvedArtifactId,
			ResolvedSiteCenterBlockWorldPos,
			SolveSeed,
			CompletionGate](const FLayoutBackgroundSolveCompletion& Completion) mutable
		{
			const FLayoutSolveResult& SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
			const FString EffectiveFailureReason = !SharedResult->ScheduleResult.FailureReason.IsEmpty()
				? SharedResult->ScheduleResult.FailureReason
				: (Completion.FailureReason.IsEmpty() ? SolveResult.FailureReason : Completion.FailureReason);
			const ELayoutCachedApplyability CachedApplyability =
				ResolveCachedApplyability(SolveResult);
			const bool bApplyable = CachedApplyability != ELayoutCachedApplyability::Rejected;
			FString SolvedArtifactFailureReason;
			if (Completion.bWorkSucceeded && bApplyable
				&& !ValidateRuntimeSolvedArtifactForPublish(
					SharedResult->SolvedArtifact,
					CapturedSolvedArtifactId,
					SolvedArtifactFailureReason))
			{
				(*CompletionGate)(false, FResolvedLayoutSiteRecord(), SharedResult->ScheduleResult, SolvedArtifactFailureReason);
				return;
			}
			RecordLayoutSolvePropagationStats(
				FString::Printf(
					TEXT("%s async direct root %s"),
					SolveResult.bSucceeded ? TEXT("accepted") : TEXT("rejected"),
					*ResolvedSiteCenterBlockWorldPos.ToString()),
				SolveResult,
				SolveResult.bSucceeded ? FColor::Cyan : FColor::Orange);

			FResolvedLayoutSiteLocationMetadata LocationMetadata;
			LocationMetadata.SiteCenterBlockWorldPos = ResolvedSiteCenterBlockWorldPos;
			// Worker may replace stepped authority with one bounded flat fallback;
			// publication must use the exact request that produced the result.
			FLayoutRegionSolveRequest PublishRequest = SharedResult->FinalizedRequest;
			TOptional<FLayoutWorldBindingSiteFrontendSelection> PublishFrontendSelection;
			if (CapturedFrontendSelection.IsSet())
			{
				PublishFrontendSelection = CapturedFrontendSelection.GetValue();
				if (PublishRequest.bHasSelectedModePlan)
				{
					PublishFrontendSelection->ResolvedContinuationSelection =
						PublishRequest.SelectedModePlan.ContinuationSelection;
				}
			}
			const FLayoutWorldBindingSiteFrontendSelection* const FrontendSelectionPtr =
				PublishFrontendSelection.IsSet() ? &PublishFrontendSelection.GetValue() : nullptr;
			FLayoutSiteSolveSourceSelection SolveSourceSelection;
			SolveSourceSelection.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>((*SharedWorkerPacket).RuntimeSnapshot.LayoutProfilePath);
			SolveSourceSelection.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>((*SharedWorkerPacket).RuntimeSnapshot.ContentSetPath);
			SolveSourceSelection.ExportedConnectorTypeTags = (*SharedWorkerPacket).RuntimeSnapshot.ExportedConnectorTypeTags;
			SolveSourceSelection.SolveSeed = PublishRequest.Seed;
			FLayoutRootPublicationMetadata PublicationMetadata;
			PublicationMetadata.RootSolveId = PublishRequest.RootSolveId;
			PublicationMetadata.RootCandidateId = PublishRequest.RootCandidateId;
			PublicationMetadata.RootPlacementPolicyId = PublishRequest.RootPlacementPolicyId;
			FResolvedLayoutSiteRecord SiteRecord = FLayoutSiteReservation::BuildResolvedSiteRecord(
				LocationMetadata,
				SolveSourceSelection,
				FrontendSelectionPtr,
				PublicationMetadata,
				&SolveResult);
			RehydrateSolvedPlacementAssetCarriersFromContentSet(SiteRecord, CapturedContentSet.Get());
			{
				FResolvedLayoutSiteRuntimeState RuntimeState = SiteRecord.GetResolvedSiteRuntimeState();
				RuntimeState.CachedApplyability = CachedApplyability;
				SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
			}
			if (!PublicationMetadata.RootSolveId.IsNone())
			{
				if (Completion.bWorkSucceeded
					&& bApplyable
					&& !SharedResult->FrozenTerrainContract.ContractId.IsNone())
				{
					UE_LOG(LogTemp, Display,
						TEXT("PublishOnGameThread: storing contract for RootSolveId=%s, ContractId=%s, StageMap=%d, ActiveCells=%d, ReservedOpen=%d"),
						*PublicationMetadata.RootSolveId.ToString(),
						*SharedResult->FrozenTerrainContract.ContractId.ToString(),
						SharedResult->FrozenTerrainContract.StageMap.Num(),
						SharedResult->FrozenTerrainContract.ActiveCells.Num(),
						SharedResult->FrozenTerrainContract.ReservedOpenTerrainReservations.Num());

					ResolvedRootFrozenTerrainContracts.Add(
						PublicationMetadata.RootSolveId,
						SharedResult->FrozenTerrainContract);
				}
				else
				{
					// Rejected previews must not retain runtime realization authority.
					ResolvedRootFrozenTerrainContracts.Remove(PublicationMetadata.RootSolveId);
					ResolvedRootRealizationWritePlans.Remove(PublicationMetadata.RootSolveId);
				}
			}

			SiteRecord.CachedFrozenTerrainBaseZByColumn.Reset();
			SiteRecord.CachedFrozenTerrainBaseZByColumn.Reserve(SharedResult->FrozenTerrainContract.StageMap.Num());
			for (const FLayoutFrozenTerrainStageCellRecord& StageRecord : SharedResult->FrozenTerrainContract.StageMap)
			{
				SiteRecord.CachedFrozenTerrainBaseZByColumn.Add(StageRecord.FootprintCellXY, StageRecord.ResolvedStageBaseBlockWorldZ);
			}
			SiteRecord.SolvedArtifactId = SharedResult->SolvedArtifact.ArtifactId;
			SiteRecord.SolvedArtifactActiveCellCount = SharedResult->SolvedArtifact.ActiveCells.Num();

			SiteRecord.bWritePlanReady = false;
			SiteRecord.CachedTemplatePlacementCount = SolveResult.Placements.Num();
			if (Completion.bWorkSucceeded && bApplyable)
			{
				FLayoutRealizationWritePlan AcceptedWritePlan;
				FString RealizationPrepFailureReason;
				if (!LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
						ELayoutRealizationWritePlanSource::Site,
						SiteRecord.SolvedArtifactId,
						SiteRecord.SolvedArtifactActiveCellCount,
						SolveResult,
						SharedResult->FrozenTerrainContract,
						AcceptedWritePlan,
						RealizationPrepFailureReason))
				{
					(*CompletionGate)(false, FResolvedLayoutSiteRecord(), SharedResult->ScheduleResult, RealizationPrepFailureReason);
					return;
				}
				if (PublicationMetadata.RootSolveId.IsNone())
				{
					(*CompletionGate)(false, FResolvedLayoutSiteRecord(), SharedResult->ScheduleResult, TEXT("Realization-prep requires a non-empty root solve id before WritePlanReady can be set."));
					return;
				}
				const FLayoutId RealizationIdempotencyMarker = FLayoutId(*FString::Printf(
					TEXT("RealizationApplied.%s.%s"),
					*PublicationMetadata.RootSolveId.ToString(),
					*AcceptedWritePlan.WritePlanId.ToString()));
				ResolvedRootRealizationWritePlans.Add(
					PublicationMetadata.RootSolveId,
					MakeShared<FLayoutRealizationWritePlan>(AcceptedWritePlan));
				SiteRecord.CachedRealizationWritePlanId = AcceptedWritePlan.WritePlanId;
				SiteRecord.CachedRealizationWritePlanHash = AcceptedWritePlan.WritePlanHash;
				SiteRecord.CachedRealizationProvenanceId = PublicationMetadata.RootSolveId;
				SiteRecord.CachedRealizationIdempotencyMarker = RealizationIdempotencyMarker;
				SiteRecord.CachedFrozenTerrainContractId = SharedResult->FrozenTerrainContract.ContractId;
				SiteRecord.CachedChunkWritePassCount = AcceptedWritePlan.ChunkWriteBatch.PassOrder.Num();
				SiteRecord.CachedTerrainWriteCount = AcceptedWritePlan.ChunkWriteBatch.TerrainWriteCount;
				SiteRecord.CachedTemplatePlacementCount = AcceptedWritePlan.ChunkWriteBatch.TemplatePlacementCount;
				SiteRecord.bWritePlanReady = true;
			}
			RehydrateScheduleResultPlacementAssetCarriersFromContentSet(SharedResult->ScheduleResult, CapturedContentSet.Get());
			const bool bPublishAccepted = Completion.bWorkSucceeded && SolveResult.bSucceeded;
			UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] Explicit-root publish: accepted=%d workSucceeded=%d solveSucceeded=%d placements=%d site=%s seed=%d mode=%s reason=%s"),
				bPublishAccepted ? 1 : 0, Completion.bWorkSucceeded ? 1 : 0, SolveResult.bSucceeded ? 1 : 0, SolveResult.Placements.Num(),
				*ResolvedSiteCenterBlockWorldPos.ToString(), SolveSeed,
				PublishRequest.bHasSelectedModePlan
					? *StaticEnum<ELayoutContractEnvironmentMode>()->GetNameStringByValue(static_cast<int64>(PublishRequest.SelectedModePlan.EnvironmentMode))
					: TEXT("None"),
				*EffectiveFailureReason);
			// Bounded publication evidence distinguishes child mapping from the root's mode.
			// Parent coordinates below belong to each mapping's owning regional frame.
			for (int32 MappingIndex = 0; MappingIndex < FMath::Min(8, SharedResult->ScheduleResult.ChildStageMappings.Num()); ++MappingIndex)
			{
				const FLayoutChildStageMappingResult& Mapping = SharedResult->ScheduleResult.ChildStageMappings[MappingIndex];
				const FLayoutChildStageMappedCell* Ground = nullptr;
				int32 MinGroundZ = MAX_int32, MaxGroundZ = MIN_int32;
				for (const FLayoutChildStageMappedCell& Cell : Mapping.Cells)
				{
					if (Cell.ModuleLevelIndex != 0) continue;
					if (Ground == nullptr) Ground = &Cell;
					MinGroundZ = FMath::Min(MinGroundZ, Cell.ParentCell.Z);
					MaxGroundZ = FMath::Max(MaxGroundZ, Cell.ParentCell.Z);
				}
				if (Ground == nullptr) continue;
				UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ChildHeightMapping: site=%s seed=%d mapping=%s class=%d offset=%s groundParentZ=[%d,%d] sampleSource=%s sampleChild=%s sampleParent=%s terrainOrigin=%s terrainContract=%s stageColumns=%d"),
					*ResolvedSiteCenterBlockWorldPos.ToString(), SolveSeed, *Mapping.MappingId.ToString(), static_cast<int32>(Mapping.StageClass),
					*Mapping.ParentRegionCellOffset.ToString(), MinGroundZ, MaxGroundZ, *Ground->SourceChildCell.ToString(),
					*Ground->MappedChildCell.ToString(), *Ground->ParentCell.ToString(),
					*Mapping.ChildLocalTerrainContract.FootprintMinBlockWorldPos.ToString(), *Mapping.ChildLocalTerrainContract.ContractId.ToString(),
					Mapping.ChildLocalStageMap.Num());
			}
			// Publish once, not from the polled editor summary. A successful flat result
			// must not hide the original stepped rejection retained in its warnings.
			for (const FLayoutValidationMessage& Message : SolveResult.Messages)
			{
				if (Message.Severity == ELayoutValidationSeverity::Warning)
				{
					UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] Explicit-root warning: site=%s seed=%d message=%s"),
						*ResolvedSiteCenterBlockWorldPos.ToString(), SolveSeed, *Message.Message);
				}
			}
			(*CompletionGate)(
				bPublishAccepted,
				SiteRecord,
				SharedResult->ScheduleResult,
				EffectiveFailureReason);
		};

		return true;
	};

	// Build the pre-submit snapshot and prewarm input so the prewarm job can derive
	// the structural contract from the terrain evidence built above.
	SharedWorkerPacket->RequestManifest.bHasRootPlacementSubmission = true;

	const FLayoutWorkerSolveRequestManifest& RequestManifest = SharedWorkerPacket->RequestManifest;
	FLayoutPreSubmitFrozenSnapshot PreSubmitSnapshot;
	PreSubmitSnapshot.SnapshotId = FLayoutId(*FString::Printf(TEXT("ExplicitRootSnapshot.%s"), *WorkerDebugName));
	PreSubmitSnapshot.PrewarmKind = ELayoutManifestPrewarmKind::Root;
	PreSubmitSnapshot.bHasWorkerSolvePacket = RequestManifest.bHasRootPlacementSubmission
		&& SharedWorkerPacket->bHasSelectedModePlan;
	PreSubmitSnapshot.WorkerSolvePacket = *SharedWorkerPacket;

	FLayoutManifestPrewarmInput PrewarmInput;
	PrewarmInput.PrewarmId = FLayoutId(*FString::Printf(TEXT("ExplicitRootPrewarm.%s"), *WorkerDebugName));
	PrewarmInput.Kind = ELayoutManifestPrewarmKind::Root;
	PrewarmInput.bHasFrozenRequestManifest = SharedWorkerPacket->bHasRequestManifest;
	PrewarmInput.FrozenRequestManifest = RequestManifest;
	PrewarmInput.bHasPreSubmitSnapshot = PreSubmitSnapshot.bHasWorkerSolvePacket;
	if (PrewarmInput.bHasPreSubmitSnapshot)
	{
		PrewarmInput.PreSubmitSnapshot = MoveTemp(PreSubmitSnapshot);
	}
	if (RequestManifest.bHasFrozenTerrainBiomeAdapterInput)
	{
		PrewarmInput.bHasFrozenTerrainBiomeAdapterInput = true;
		PrewarmInput.FrozenTerrainBiomeAdapterInput = RequestManifest.FrozenTerrainBiomeAdapterInput;
	}

	// Wire prewarm completion to update the shared packet with the finalized manifest.
	FLayoutManifestPrewarmStageComplete OnPrewarmComplete;
	{
		TSharedPtr<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> CapturedSharedPacket = SharedWorkerPacket;
		OnPrewarmComplete = [CapturedSharedPacket, SharedResult](
			const FLayoutBackgroundSolveCompletion& /*Completion*/,
			const FLayoutManifestPrewarmResult& PrewarmResult)
		{
			if (PrewarmResult.bHasDerivedStructuralContract)
			{
				CapturedSharedPacket->RequestManifest = PrewarmResult.FinalizedManifest;
			}
			if (PrewarmResult.bHasPrecomputedAdapterOutput)
			{
				CapturedSharedPacket->bHasPrecomputedAdapterOutput = true;
				CapturedSharedPacket->PrecomputedAdapterOutput = PrewarmResult.PrecomputedAdapterOutput;
			}
			if (PrewarmResult.bHasRejectedAdapterPreview)
			{
				const FLayoutAdapterOutput& RejectedPreview = PrewarmResult.RejectedAdapterPreview;
				SharedResult->FrozenTerrainContract = RejectedPreview.FrozenTerrainContract;
				FLayoutSolveResult& RejectedSolveResult = SharedResult->ScheduleResult.MergedSolveResult;
				RejectedSolveResult.bSucceeded = false;
				RejectedSolveResult.PlannedCells = RejectedPreview.PlannedCells;
				RejectedSolveResult.FootprintSize = CapturedSharedPacket->RequestManifest.FootprintSize;
				RejectedSolveResult.SharedCellSizeInBlocks =
					CapturedSharedPacket->RequestManifest.ContentSetSnapshot.SharedCellSizeInBlocks;
				RejectedSolveResult.TemplatePlacementZOffsetBlocks =
					CapturedSharedPacket->RuntimeSnapshot.TemplatePlacementZOffsetBlocks;
				RejectedSolveResult.RootPlacementKind = CapturedSharedPacket->RuntimeSnapshot.PlacementKind;
				RejectedSolveResult.WorldBindingPlacementPolicy = CapturedSharedPacket->RuntimeSnapshot.PlacementPolicy;
			}
		};
	}

	if (!LayoutSolveExecution::Checkpoint(LocalFailureReason))
	{
		(*CompletionGate)(false, FResolvedLayoutSiteRecord(), SharedResult->ScheduleResult, LocalFailureReason);
		return FLayoutBackgroundSolveHandle();
	}
	// Release game-thread access before submitting the first serial worker stage.
	PreparationDiagnostics.Finish(true);
	PreSubmitExecutionScope.Reset();
	FLayoutBackgroundSolveDispatcher& Dispatcher = GetOrCreateBackgroundSolveDispatcher();
	FLayoutBackgroundSolveSubmission LifecycleSubmission =
		FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
			&Dispatcher,
			FString::Printf(TEXT("ExplicitRootPrewarm %s"), *WorkerDebugName),
			FString::Printf(TEXT("ExplicitRootPreflight %s"), *WorkerDebugName),
			LayoutGroupId,
			0,
			0,
			MoveTemp(PrewarmInput),
			MoveTemp(SolveFactory),
			MoveTemp(OnPrewarmComplete),
			FLayoutAdmissibilityPreflightStageComplete(),
			[SharedResult, CompletionGate](
				const FLayoutBackgroundSolveCompletion& /*Completion*/,
				const FString& FailureReason)
			{
				const FString EffectiveFailureReason = FailureReason.IsEmpty()
					? TEXT("Explicit-root lifecycle rejected before solve submission.")
					: FailureReason;
				FLayoutSolveResult& RejectedSolveResult = SharedResult->ScheduleResult.MergedSolveResult;
				RejectedSolveResult.bSucceeded = false;
				RejectedSolveResult.FailureReason = EffectiveFailureReason;
				SharedResult->ScheduleResult.FailureReason = EffectiveFailureReason;
				FResolvedLayoutSiteRecord RejectedSite;
				// Physical cell Z already includes terrain-stage shifts. Preserve the adapter's
				// rebased per-column origins for diagnostics, not a live/applyable contract.
				for (const FLayoutFrozenTerrainStageCellRecord& Stage : SharedResult->FrozenTerrainContract.StageMap)
				{
					RejectedSite.CachedFrozenTerrainBaseZByColumn.Add(Stage.FootprintCellXY, Stage.ResolvedStageBaseBlockWorldZ);
				}
				(*CompletionGate)(
					false,
					RejectedSite,
					SharedResult->ScheduleResult,
					EffectiveFailureReason);
			}, OperationLedger);
	return Dispatcher.Submit(MoveTemp(LifecycleSubmission));
}

#if WITH_AUTOMATION_TESTS
bool UChunkWorldLayoutRuntimeComponent::TrySolveExplicitRootLayoutSite(
	const FIntVector& SiteCenterBlockWorldPos,
	const FLayoutWorldBindingRuntimeView& RuntimeView,
	const int32 SolveSeed,
	FResolvedLayoutSiteRecord& OutSiteRecord,
	FLayoutRegionSolveScheduleResult& OutScheduleResult,
	FString* const OutFailureReason,
	const FLayoutWorldBindingSiteFrontendSelection* const FrontendSelection)
{
	OutSiteRecord = FResolvedLayoutSiteRecord();
	OutScheduleResult = FLayoutRegionSolveScheduleResult();
	if (OutFailureReason != nullptr)
	{
		OutFailureReason->Reset();
	}
	if (IsInGameThread())
	{
		if (GIsAutomationTesting)
		{
			bool bCompleted = false;
			bool bSucceeded = false;
			FString CompletionFailureReason;
			SubmitExplicitRootLayoutSiteSolve(
				SiteCenterBlockWorldPos,
				RuntimeView,
				SolveSeed,
				[&bCompleted, &bSucceeded, &OutSiteRecord, &OutScheduleResult, &CompletionFailureReason](
					const bool bAsyncSucceeded,
					const FResolvedLayoutSiteRecord& SiteRecord,
					const FLayoutRegionSolveScheduleResult& ScheduleResult,
					const FString& FailureReason)
				{
					bSucceeded = bAsyncSucceeded;
					OutSiteRecord = SiteRecord;
					OutScheduleResult = ScheduleResult;
					CompletionFailureReason = FailureReason;
					bCompleted = true;
				},
				FrontendSelection);
			for (int32 PumpIndex = 0; PumpIndex < 5000 && !bCompleted; ++PumpIndex)
			{
				PumpBackgroundLayoutSolves();
				FPlatformProcess::Sleep(0.01f);
			}
			PumpBackgroundLayoutSolves();
			if (OutFailureReason != nullptr)
			{
				*OutFailureReason = CompletionFailureReason;
			}
			return bCompleted && bSucceeded;
		}
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = TEXT("Synchronous explicit-root layout proof is disabled on the game thread. Use SubmitExplicitRootLayoutSiteSolve and consume the async completion.");
		}
		return false;
	}

	const AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	const int32 WorldSeed = ResolveLayoutWorldSeed();
	const double ExplicitRootSolveStartSeconds = FPlatformTime::Seconds();
	double ExplicitRootRequestBuildSeconds = 0.0;
	double ExplicitRootTreeSolveSeconds = 0.0;

	const FIntVector ResolvedSiteCenterBlockWorldPos =
		RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None
			? FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
				SiteCenterBlockWorldPos,
				RuntimeView.SharedCellSizeInBlocks)
			: SiteCenterBlockWorldPos;

	FLayoutRegionSolveRequest SolveRequest;
	FString LocalFailureReason;
		const FName EligibleBiomeRowName = FrontendSelection != nullptr
		? FrontendSelection->BiomeRowName
		: (!RuntimeView.MatchingBiomeRowName.IsNone() ? RuntimeView.MatchingBiomeRowName : NAME_None);
	const double RequestBuildStartSeconds = FPlatformTime::Seconds();
	if (!LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
		RuntimeView,
		ResolvedSiteCenterBlockWorldPos,
		SolveSeed,
		SolveRequest,
		LocalFailureReason))
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = LocalFailureReason;
		}
		return false;
	}
	ExplicitRootRequestBuildSeconds = FPlatformTime::Seconds() - RequestBuildStartSeconds;

	const double TreeSolveStartSeconds = FPlatformTime::Seconds();
	OutScheduleResult = FLayoutProfileSolver::SolveRegionTree(SolveRequest);
	ExplicitRootTreeSolveSeconds = FPlatformTime::Seconds() - TreeSolveStartSeconds;
	PublishRequestOwnedSteppedCarriersOnMergedSolveResult(
		SolveRequest,
		OutScheduleResult);
	InvalidatePlacementlessWorldFacingScheduleResult(
		OutScheduleResult,
		SolveRequest.RegionDebugPath);
	const FLayoutSolveResult& SolveResult = OutScheduleResult.MergedSolveResult;
	RecordLayoutSolvePropagationStats(
		FString::Printf(
			TEXT("%s direct root %s"),
			SolveResult.bSucceeded ? TEXT("accepted") : TEXT("rejected"),
			*ResolvedSiteCenterBlockWorldPos.ToString()),
		SolveResult,
		SolveResult.bSucceeded ? FColor::Cyan : FColor::Orange);

	FResolvedLayoutSiteLocationMetadata LocationMetadata;
	LocationMetadata.SiteCenterBlockWorldPos = ResolvedSiteCenterBlockWorldPos;
	OutSiteRecord = LayoutWorldBindingRuntimeHelpers::BuildResolvedSiteRecordFromRuntimeSolve(
		LocationMetadata,
		RuntimeView,
		SolveSeed,
		SolveRequest,
		SolveResult,
		FrontendSelection);

	if (!SolveResult.bSucceeded && OutFailureReason != nullptr)
	{
		*OutFailureReason = SolveResult.FailureReason;
	}

	const double ExplicitRootTotalSolveSeconds = FPlatformTime::Seconds() - ExplicitRootSolveStartSeconds;
	if (ExplicitRootTotalSolveSeconds >= 1.0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("Runtime explicit-root perf: region=%s success=%d total=%.3fs requestBuild=%.3fs treeSolve=%.3fs placementKind=%d plannedCells=%d routeConstraints=%d backtracks=%d"),
			*SolveRequest.RegionDebugPath,
			SolveResult.bSucceeded ? 1 : 0,
			ExplicitRootTotalSolveSeconds,
			ExplicitRootRequestBuildSeconds,
			ExplicitRootTreeSolveSeconds,
			static_cast<int32>(RuntimeView.PlacementKind),
			SolveRequest.PlannedCells.Num(),
			SolveResult.RouteConstraints.Num(),
			SolveResult.PropagationStats.BacktrackCount);
	}

	return SolveResult.bSucceeded;
}
#endif

bool UChunkWorldLayoutRuntimeComponent::TryCacheExplicitRootLayoutSite(
	const FResolvedLayoutSiteRecord& SiteRecord,
	FIntPoint& OutReservationKey,
	const bool bReplaceCommittedRecord)
{
	const FResolvedLayoutSiteLocationMetadata LocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FResolvedLayoutSiteRuntimeState SiteRuntimeState =
		SiteRecord.GetResolvedSiteRuntimeState();
	if (SiteRecord.RootSolveId.IsNone() || !IsCachedSiteApplyable(SiteRuntimeState))
	{
		OutReservationKey = FIntPoint::ZeroValue;
		return false;
	}

	OutReservationKey = FLayoutSiteReservation::ComputeReservationKey(LocationMetadata.SiteCenterBlockWorldPos);
	const FString SiteRecordKey = MakeResolvedSiteRecordKey(SiteRecord);
	if (const FResolvedLayoutSiteRecord* ExistingRecord = ResolvedSiteRecords.Find(SiteRecordKey))
	{
		const FResolvedLayoutSiteRuntimeState ExistingRuntimeState =
			ExistingRecord->GetResolvedSiteRuntimeState();
		if (ExistingRuntimeState.bHasBeenCommittedToChunkWorld && !bReplaceCommittedRecord)
		{
			return false;
		}
	}

	ResolvedSiteRecords.Add(SiteRecordKey, SiteRecord);
	PlanningRecordKeysBySiteRecordKey.Remove(SiteRecordKey);
	RefreshConnectorRecords();
	return true;
}

bool UChunkWorldLayoutRuntimeComponent::TryGetAcceptedFrozenTerrainContract(
	const FResolvedLayoutSiteRecord& SiteRecord,
	FLayoutFrozenTerrainContract& OutFrozenTerrainContract,
	FString* const OutFailureReason) const
{
	auto SetFailureReason = [OutFailureReason](const FString& Message)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = Message;
		}
	};

	OutFrozenTerrainContract = FLayoutFrozenTerrainContract();
	if (OutFailureReason != nullptr)
	{
		OutFailureReason->Reset();
	}

	const FLayoutRootPublicationMetadata RootPublicationMetadata =
		SiteRecord.GetRootPublicationMetadata();

	if (!RootPublicationMetadata.RootSolveId.IsNone())
	{
		if (const FLayoutFrozenTerrainContract* const AcceptedFrozenTerrainContract =
			ResolvedRootFrozenTerrainContracts.Find(RootPublicationMetadata.RootSolveId))
		{
			OutFrozenTerrainContract = *AcceptedFrozenTerrainContract;
			return true;
		}
	}
	else
	{
		SetFailureReason(TEXT("Cannot resolve accepted frozen terrain contract: RootSolveId is NONE."));
		return false;
	}

	// Write-plan-ready site must have a stored contract. Pre-acceptance preview
	// records without one fail closed — terrain-fit synthesis is not supported.
	if (SiteRecord.bWritePlanReady)
	{
		SetFailureReason(TEXT("Realization gate: missing_frozen_terrain_contract — site has bWritePlanReady set but no accepted frozen terrain contract was found."));
		return false;
	}

	SetFailureReason(FString::Printf(
		TEXT("No accepted frozen terrain contract found for RootSolveId=%s."),
		*RootPublicationMetadata.RootSolveId.ToString()));
	return false;
}

bool UChunkWorldLayoutRuntimeComponent::TryApplySolvedExplicitRootLayoutSite(
	const FResolvedLayoutSiteRecord& SiteRecord,
	FString* const OutFailureReason,
	const bool bReplaceCommittedRecord,
	const FLayoutFrozenTerrainContract* const FrozenTerrainContractOverride,
	const ULayoutWorldBindingAsset* const ExplicitWorldBindingOverride,
	const bool bAllowInvalidPreview)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Realization_ApplyCachedSolve, STAT_PorismLayout_RealizationApply);
	// -----------------------------------------------------------------------
	// Cached-apply call chain (every required boundary):
	//
	// TryApplySolvedExplicitRootLayoutSite
	//   1. Resolve frozen terrain contract from cached map when no override
	//   2. Gate: missing_frozen_terrain_contract (write-plan-ready sites)
	//   3. Gate: null_chunk_world
	//   4. Gate: placementless_world_facing (empty placements)
	//   5. Gate: cache_admission_rejected (record not in cache)
	//   6. Gate: generation_restart_failed (chunk world restart)
	//   7. Gate: site_record_disappeared (record lost during restart)
	//   8. Seed required chunk origins into observed set
	//   9. ShouldAttemptRealization (lifecycle state chain validation)
	//  10. [if eligible] TryRealizeSite
	//       a. Build/validate realization write plan
	//       b. CanStampRequiredChunkOrigins (per-chunk idempotency gate)
	//       c. ApplyTerrainWriteReplay (terrain stamping)
	//       d. Template placement loop (ValidateTemplatePlacementWrite + stamp)
	//       e. MarkRequiredChunkOriginsStamped
	//  11. MarkResolvedSiteRealizedAndCommitted
	//  12. RefreshConnectorRecords
	//
	// Fail-closed gates return false with diagnostic. No legacy terrain-fit
	// fallback; write-plan-ready sites require accepted frozen contract authority.
	// -----------------------------------------------------------------------

	auto SetFailureReason = [OutFailureReason](const FString& Message)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = Message;
		}
	};

	if (OutFailureReason != nullptr)
	{
		OutFailureReason->Reset();
	}

	if (SiteRecord.RootSolveId.IsNone())
	{
		SetFailureReason(TEXT("Explicit root Apply requires producer-owned root solve identity."));
		return false;
	}

	const FLayoutFrozenTerrainContract* EffectiveFrozenTerrainContractOverride = FrozenTerrainContractOverride;
	if (EffectiveFrozenTerrainContractOverride == nullptr || EffectiveFrozenTerrainContractOverride->ContractId.IsNone())
	{
		EffectiveFrozenTerrainContractOverride = ResolvedRootFrozenTerrainContracts.Find(
			SiteRecord.GetRootPublicationMetadata().RootSolveId);
	}

	UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve start: siteCenter=%s rootSolveId=%s artifactId=%s placeCount=%d activeCells=%d frozenContract=%s override=%d bWritePlanReady=%d"),
		*SiteRecord.SiteCenterBlockWorldPos.ToString(),
		*SiteRecord.GetRootPublicationMetadata().RootSolveId.ToString(),
		*SiteRecord.SolvedArtifactId.ToString(),
		SiteRecord.SolveResult.Placements.Num(),
		SiteRecord.SolvedArtifactActiveCellCount,
		(EffectiveFrozenTerrainContractOverride != nullptr && !EffectiveFrozenTerrainContractOverride->ContractId.IsNone())
			? *EffectiveFrozenTerrainContractOverride->ContractId.ToString() : TEXT("none"),
		(FrozenTerrainContractOverride != nullptr) ? 1 : 0,
		SiteRecord.bWritePlanReady ? 1 : 0);

	// Realization gate: write-plan-ready sites must carry accepted frozen terrain contract authority.
	if (!SiteRecord.bWritePlanReady)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: missing_cached_write_plan siteCenter=%s"),
			*SiteRecord.SiteCenterBlockWorldPos.ToString());
		SetFailureReason(TEXT("Realization gate: missing_cached_write_plan — ApplyCachedSolve only accepts payloads prepared during publication."));
		return false;
	}
	if (EffectiveFrozenTerrainContractOverride == nullptr || EffectiveFrozenTerrainContractOverride->ContractId.IsNone())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: missing_frozen_terrain_contract siteCenter=%s bWritePlanReady=%d"),
			*SiteRecord.SiteCenterBlockWorldPos.ToString(), SiteRecord.bWritePlanReady ? 1 : 0);
		SetFailureReason(TEXT("Realization gate: missing_frozen_terrain_contract — ApplyCachedSolve requires accepted frozen terrain contract authority for write-plan-ready sites."));
		return false;
	}

	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	bool bRestartedGenerationThisApply = false;
	if (ChunkWorld == nullptr)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: null_chunk_world siteCenter=%s"),
			*SiteRecord.SiteCenterBlockWorldPos.ToString());
		SetFailureReason(TEXT("Could not apply explicit root site because the owning chunk world is null."));
		return false;
	}

	const FResolvedLayoutSiteLocationMetadata LocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	if (SolvedPayload.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
		&& SolvedPayload.SolveResult.Placements.IsEmpty())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: placementless_world_facing siteCenter=%s placementKind=%d"),
			*SiteRecord.SiteCenterBlockWorldPos.ToString(),
			static_cast<int32>(SolvedPayload.SolveResult.RootPlacementKind));
		SetFailureReason(
			BuildPlacementlessWorldFacingSolveFailureReason(
				SolvedPayload.SolveResult));
		return false;
	}
	if (!SolvedPayload.SolveResult.bSucceeded && !bAllowInvalidPreview)
	{
		SetFailureReason(TEXT("Normal Apply rejects failed solves. Invalid retained geometry requires explicit manual debug application."));
		return false;
	}
	const FIntPoint ExistingReservationKey = FLayoutSiteReservation::ComputeReservationKey(LocationMetadata.SiteCenterBlockWorldPos);
	const FString SiteRecordKey = MakeResolvedSiteRecordKey(SiteRecord);
	const FResolvedLayoutSiteRecord* const ExistingRecord = ResolvedSiteRecords.Find(SiteRecordKey);
	const bool bHadExistingRecord = ExistingRecord != nullptr;
	const FString* const ExistingPlanningRecordKey = PlanningRecordKeysBySiteRecordKey.Find(SiteRecordKey);
	const bool bHadExistingPlanningRecordKey = ExistingPlanningRecordKey != nullptr;
	const FResolvedLayoutSiteRecord PreviousRecord =
		bHadExistingRecord ? *ExistingRecord : FResolvedLayoutSiteRecord();
	const FString PreviousPlanningRecordKey =
		bHadExistingPlanningRecordKey ? *ExistingPlanningRecordKey : FString();
	const TMap<uint64, FResolvedLayoutConnectorRecord> PreviousConnectorRecords =
		ResolvedConnectorRecords;
	auto RestorePreApplyCacheState =
		[this,
		SiteRecordKey,
		ExistingReservationKey,
		bHadExistingRecord,
		PreviousRecord,
		bHadExistingPlanningRecordKey,
		PreviousPlanningRecordKey,
		PreviousConnectorRecords,
		&LocationMetadata]()
	{
		UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve restore_state: siteCenter=%s root=%s reservationKey=(%d,%d) hadExisting=%d hadPlanning=%d"),
			*LocationMetadata.SiteCenterBlockWorldPos.ToString(),
			*SiteRecordKey,
			ExistingReservationKey.X, ExistingReservationKey.Y,
			bHadExistingRecord ? 1 : 0,
			bHadExistingPlanningRecordKey ? 1 : 0);
		if (bHadExistingRecord)
		{
			ResolvedSiteRecords.Add(SiteRecordKey, PreviousRecord);
		}
		else
		{
			ResolvedSiteRecords.Remove(SiteRecordKey);
		}

		if (bHadExistingPlanningRecordKey)
		{
			PlanningRecordKeysBySiteRecordKey.Add(SiteRecordKey, PreviousPlanningRecordKey);
		}
		else
		{
			PlanningRecordKeysBySiteRecordKey.Remove(SiteRecordKey);
		}

		ResolvedConnectorRecords = PreviousConnectorRecords;
	};
	FResolvedLayoutSiteRecord ApplySiteRecord = SiteRecord;
	if (!SolvedPayload.SolveResult.bSucceeded)
	{
		// Explicit debug application preserves a failed publication outcome even
		// when the caller retained a successful parent-only lifecycle carrier.
		auto RuntimeState = ApplySiteRecord.GetResolvedSiteRuntimeState();
		RuntimeState.bLayoutSolved = false;
		RuntimeState.CachedApplyability = ELayoutCachedApplyability::Partial;
		ApplySiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
	}
	FIntPoint ReservationKey = FIntPoint::ZeroValue;
	if (!TryCacheExplicitRootLayoutSite(ApplySiteRecord, ReservationKey, bReplaceCommittedRecord))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: cache_admission_rejected siteCenter=%s replaceCommitted=%d"),
			*LocationMetadata.SiteCenterBlockWorldPos.ToString(), bReplaceCommittedRecord ? 1 : 0);
		SetFailureReason(FString::Printf(
			TEXT("Could not cache explicit root site at %s. An existing committed record may already own the reservation."),
			*LocationMetadata.SiteCenterBlockWorldPos.ToString()));
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve cache_admitted: siteCenter=%s reservationKey=(%d,%d) replaceCommitted=%d"),
		*LocationMetadata.SiteCenterBlockWorldPos.ToString(), ReservationKey.X, ReservationKey.Y, bReplaceCommittedRecord ? 1 : 0);

	if (!ChunkWorld->IsRunning())
	{
		UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve generation_restart: siteCenter=%s reservationKey=(%d,%d)"),
			*LocationMetadata.SiteCenterBlockWorldPos.ToString(), ReservationKey.X, ReservationKey.Y);
		bRestartedGenerationThisApply = true;
		// Persist the solved record on the component before restarting generation so
		// explicit-root apply retains its cached module/template carriers across any
		// StartGen-triggered GC work.
		ChunkWorld->RestartGenerationPreservingLayoutRecords();
		if (!ChunkWorld->IsRunning())
		{
			UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: generation_restart_failed siteCenter=%s"),
				*LocationMetadata.SiteCenterBlockWorldPos.ToString());
			RestorePreApplyCacheState();
			SetFailureReason(FString::Printf(
				TEXT("Could not apply explicit root site at %s because chunk world '%s' is not running. StartGen failed or no runtime WorldGenDef is available."),
				*LocationMetadata.SiteCenterBlockWorldPos.ToString(),
				*GetNameSafe(ChunkWorld)));
			return false;
		}
	}

	FResolvedLayoutSiteRecord* CachedSiteRecord = ResolvedSiteRecords.Find(SiteRecordKey);
	if (CachedSiteRecord == nullptr)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: site_record_disappeared siteCenter=%s root=%s"),
			*LocationMetadata.SiteCenterBlockWorldPos.ToString(), *SiteRecordKey);
		RestorePreApplyCacheState();
		SetFailureReason(FString::Printf(
			TEXT("Cached explicit root site record disappeared for root '%s'."),
			*SiteRecordKey));
		return false;
	}

	if (bRestartedGenerationThisApply)
	{
		// A stopped-world explicit-root apply can enqueue fresh chunk lifecycle
		// events during StartGen before the next component tick runs. Drain that
		// work immediately so apply uses the new runtime observation picture
		// instead of reporting success while realization is still waiting on the
		// first post-restart tick.
		UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve drain_after_restart: siteCenter=%s"),
			*LocationMetadata.SiteCenterBlockWorldPos.ToString());
		ProcessQueuedLayoutWork();
		CachedSiteRecord = ResolvedSiteRecords.Find(SiteRecordKey);
		if (CachedSiteRecord == nullptr)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: site_record_disappeared_after_restart siteCenter=%s root=%s"),
				*LocationMetadata.SiteCenterBlockWorldPos.ToString(), *SiteRecordKey);
			RestorePreApplyCacheState();
			SetFailureReason(FString::Printf(
				TEXT("Cached explicit root site record disappeared after restarting chunk world '%s' for root '%s'."),
				*GetNameSafe(ChunkWorld),
				*SiteRecordKey));
			return false;
		}
	}

	// Explicit direct-root apply owns this exact site footprint, so it can seed
	// the required chunk set even after a restart. The broader observed-chunk
	// picture is still rebuilt from fresh lifecycle events across StartGen, but
	// the manual Apply path should remain one-click and self-contained.
	const TSet<FIntVector> RequiredSiteChunkOrigins = CollectRequiredChunkOrigins(*CachedSiteRecord);
	UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve required_chunks: siteCenter=%s required=%d observed_before=%d"),
		*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
		RequiredSiteChunkOrigins.Num(),
		GetObservedLoadedChunkCount());
	for (const FIntVector& ChunkOrigin : RequiredSiteChunkOrigins)
	{
		RecordFinestLoadedChunk(ChunkOrigin);
	}
	UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve observed_after_seed: siteCenter=%s observed=%d"),
		*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
		GetObservedLoadedChunkCount());

	// When a write-plan-ready site carries cached realization-prep metadata,
	// validate the write plan before accepting the site for (potentially
	// deferred) realization. This gate catches missing write plans and stale
	// metadata so sites with synthetic IDs report failure instead of success.
	if (CachedSiteRecord->bWritePlanReady
		&& EffectiveFrozenTerrainContractOverride != nullptr
		&& !EffectiveFrozenTerrainContractOverride->ContractId.IsNone())
	{
		const FLayoutId RootSolveId = CachedSiteRecord->GetRootPublicationMetadata().RootSolveId;
		if (!RootSolveId.IsNone())
		{
			const TSharedPtr<FLayoutRealizationWritePlan>* const CachedWritePlan =
				ResolvedRootRealizationWritePlans.Find(RootSolveId);
			if (CachedWritePlan == nullptr || !CachedWritePlan->IsValid())
			{
				UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: missing_cached_write_plan siteCenter=%s rootSolveId=%s writePlanId=%s"),
					*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
					*RootSolveId.ToString(),
					*CachedSiteRecord->CachedRealizationWritePlanId.ToString());
				SetFailureReason(FString::Printf(
					TEXT("Realization gate: missing_cached_write_plan — rootSolveId=%s writePlanId=%s."),
					*RootSolveId.ToString(),
					*CachedSiteRecord->CachedRealizationWritePlanId.ToString()));
				return false;
			}

			const FLayoutRealizationWritePlan& Plan = **CachedWritePlan;
			const FLayoutId ContractId = EffectiveFrozenTerrainContractOverride->ContractId;
			if (CachedSiteRecord->CachedRealizationWritePlanId != Plan.WritePlanId
				|| CachedSiteRecord->CachedRealizationWritePlanHash != Plan.WritePlanHash
				|| CachedSiteRecord->CachedRealizationProvenanceId.IsNone()
				|| CachedSiteRecord->CachedRealizationIdempotencyMarker.IsNone()
				|| CachedSiteRecord->CachedFrozenTerrainContractId != ContractId
				|| CachedSiteRecord->CachedChunkWritePassCount != Plan.ChunkWriteBatch.PassOrder.Num()
				|| CachedSiteRecord->CachedTerrainWriteCount != Plan.ChunkWriteBatch.TerrainWriteCount
				|| CachedSiteRecord->CachedTemplatePlacementCount != Plan.ChunkWriteBatch.TemplatePlacementCount)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] ApplyCachedSolve fail: stale_cached_write_plan siteCenter=%s rootSolveId=%s"),
					*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
					*RootSolveId.ToString());
				SetFailureReason(FString::Printf(
					TEXT("Cached realization-prep metadata is stale: cachedWritePlan=%s acceptedWritePlan=%s cachedHash=%d acceptedHash=%d cachedProvenance=%s cachedIdempotency=%s cachedContract=%s acceptedContract=%s cachedPasses=%d acceptedPasses=%d cachedTerrainWrites=%d acceptedTerrainWrites=%d cachedTemplates=%d acceptedTemplates=%d."),
					*CachedSiteRecord->CachedRealizationWritePlanId.ToString(),
					*Plan.WritePlanId.ToString(),
					CachedSiteRecord->CachedRealizationWritePlanHash,
					Plan.WritePlanHash,
					*CachedSiteRecord->CachedRealizationProvenanceId.ToString(),
					*CachedSiteRecord->CachedRealizationIdempotencyMarker.ToString(),
					*CachedSiteRecord->CachedFrozenTerrainContractId.ToString(),
					*ContractId.ToString(),
					CachedSiteRecord->CachedChunkWritePassCount,
					Plan.ChunkWriteBatch.PassOrder.Num(),
					CachedSiteRecord->CachedTerrainWriteCount,
					Plan.ChunkWriteBatch.TerrainWriteCount,
					CachedSiteRecord->CachedTemplatePlacementCount,
					Plan.ChunkWriteBatch.TemplatePlacementCount));
				return false;
			}
		}
	}

		const bool bShouldAttemptSiteRealization = ShouldAttemptRealization(*CachedSiteRecord);
		const bool bAreRequiredSiteChunksObserved = RequiredSiteChunkOrigins.IsEmpty() || AreRequiredChunksObserved(*CachedSiteRecord);
		UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve realization_gate: siteCenter=%s shouldAttempt=%d requiredChunksObserved=%d"),
			*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
			bShouldAttemptSiteRealization ? 1 : 0,
			bAreRequiredSiteChunksObserved ? 1 : 0);
		if (bShouldAttemptSiteRealization
			&& bAreRequiredSiteChunksObserved)
		{
			if (!TryRealizeSite(
				*CachedSiteRecord,
				OutFailureReason,
				EffectiveFrozenTerrainContractOverride))
		{
			UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve fail: realization_rejected siteCenter=%s reason=%s"),
				*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
				OutFailureReason != nullptr ? **OutFailureReason : TEXT("<caller did not request failure reason>"));
			return false;
		}

		MarkResolvedSiteRealizedAndCommitted(*CachedSiteRecord);
		PublishResolvedRootContinuationEndpoints(
			CachedSiteRecord->GetRootPublicationMetadata().RootSolveId.ToString(),
			ExistingReservationKey,
			*CachedSiteRecord,
			ExplicitWorldBindingOverride);
		}
		else if (OutFailureReason != nullptr)
		{
			UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve skip: realization_deferred siteCenter=%s shouldAttempt=%d requiredChunksObserved=%d"),
				*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
				bShouldAttemptSiteRealization ? 1 : 0,
				bAreRequiredSiteChunksObserved ? 1 : 0);
			*OutFailureReason = FString::Printf(
				TEXT("Explicit-root apply skipped site realization at %s because shouldAttempt=%d and requiredChunksObserved=%d."),
				*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
				bShouldAttemptSiteRealization ? 1 : 0,
				bAreRequiredSiteChunksObserved ? 1 : 0);
		}

	RefreshConnectorRecords();
#if WITH_AUTOMATION_TESTS
	if (ExplicitApplyInjectedConnectorForTesting.IsSet())
	{
		const FResolvedLayoutConnectorRecord InjectedConnectorRecord =
			ExplicitApplyInjectedConnectorForTesting.GetValue();
		ResolvedConnectorRecords.Add(
			BuildResolvedConnectorRecordKey(InjectedConnectorRecord),
			InjectedConnectorRecord);
		ExplicitApplyInjectedConnectorForTesting.Reset();
	}
#endif
	for (TPair<uint64, FResolvedLayoutConnectorRecord>& Pair : ResolvedConnectorRecords)
	{
		FResolvedLayoutConnectorRecord& ConnectorRecord = Pair.Value;
		if (ConnectorRecord.StartSiteReservationKey != ReservationKey && ConnectorRecord.EndSiteReservationKey != ReservationKey)
		{
			continue;
		}

		const TSet<FIntVector> RequiredConnectorChunkOrigins = CollectRequiredChunkOrigins(ConnectorRecord);
		for (const FIntVector& ChunkOrigin : RequiredConnectorChunkOrigins)
		{
			RecordFinestLoadedChunk(ChunkOrigin);
		}

		if (!ShouldAttemptConnectorRealization(ConnectorRecord)
			|| !AreRequiredChunksObserved(ConnectorRecord))
		{
			continue;
		}

		FString ConnectorFailureReason;
		bool bForceConnectorFailure = false;
#if WITH_AUTOMATION_TESTS
		if (!NextExplicitApplyConnectorFailureReasonForTesting.IsEmpty())
		{
			ConnectorFailureReason = NextExplicitApplyConnectorFailureReasonForTesting;
			NextExplicitApplyConnectorFailureReasonForTesting.Reset();
			bForceConnectorFailure = true;
		}
#endif
		const FLayoutFrozenTerrainContract* ConnectorFrozenTerrainContractOverride = nullptr;
		if (const FLayoutFrozenTerrainContract* const StoredConnectorFrozenTerrainContract =
				ResolvedConnectorFrozenTerrainContracts.Find(Pair.Key))
		{
			ConnectorFrozenTerrainContractOverride = StoredConnectorFrozenTerrainContract;
		}
		if (bForceConnectorFailure
			|| !TryRealizeConnector(ConnectorRecord, &ConnectorFailureReason, ConnectorFrozenTerrainContractOverride))
		{
			ReleaseContinuationReservationForConnector(Pair.Key);
			// Project-specific direct-root apply is site-owned: once the site has
			// stamped successfully, derived connector failures stay retryable
			// warnings instead of reporting the whole apply as failed after mutation.
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Direct-root apply realized site at %s but could not realize derived connector between %s and %s: %s"),
				*LocationMetadata.SiteCenterBlockWorldPos.ToString(),
				*ConnectorRecord.StartEndpointBlockWorldPos.ToString(),
				*ConnectorRecord.EndEndpointBlockWorldPos.ToString(),
				*ConnectorFailureReason);
			continue;
		}

		MarkResolvedConnectorRealizedAndCommitted(ConnectorRecord);
		ConsumeContinuationReservationForConnector(Pair.Key);
	}

	UE_LOG(LogTemp, Display, TEXT("[LayoutPipeline] ApplyCachedSolve success: siteCenter=%s placeCount=%d terrainWrites=%d"),
		*CachedSiteRecord->SiteCenterBlockWorldPos.ToString(),
		CachedSiteRecord->SolveResult.Placements.Num(),
		CachedSiteRecord->CachedTerrainWriteCount);
	return true;
}

void UChunkWorldLayoutRuntimeComponent::ResetResolvedLayoutSiteRecords(const bool bResetCommittedSites)
{
	if (bResetCommittedSites)
	{
		for (const auto& Pair : RootSpacingReservations) PlanningAreaQueue->NotifyReservationReleased(Pair.Key);
		RootSpacingReservations.Reset();
		ResolvedSiteRecords.Reset();
		PlanningRecordKeysBySiteRecordKey.Reset();
		return;
	}

	for (auto It = ResolvedSiteRecords.CreateIterator(); It; ++It)
	{
		if (!It.Value().GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld)
		{
			if (const FString* RootKey = PlanningRecordKeysBySiteRecordKey.Find(It.Key()))
			{
				if (RootSpacingReservations.Remove(*RootKey) > 0) PlanningAreaQueue->NotifyReservationReleased(*RootKey);
			}
			if (RootSpacingReservations.Remove(It.Key()) > 0) PlanningAreaQueue->NotifyReservationReleased(It.Key());
			PlanningRecordKeysBySiteRecordKey.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UChunkWorldLayoutRuntimeComponent::ResetResolvedLayoutRecords(const bool bResetCommittedRecords)
{
	if (bResetCommittedRecords)
	{
		// Keep the dispatcher and its monotonic ids alive until canceled workers retire,
		// but no old-world completion may publish or submit successor work.
		if (BackgroundSolveDispatcher.IsValid())
		{
			BackgroundSolveDispatcher->CancelAll();
		}
		CancelAutomaticRootWork();
	}
	ResetResolvedLayoutSiteRecords(bResetCommittedRecords);
	if (bResetCommittedRecords)
	{
		ResolvedRootFrozenTerrainContracts.Reset();
		ResolvedRootRealizationWritePlans.Reset();
		ResolvedConnectorRecords.Reset();
		ResolvedConnectorFrozenTerrainContracts.Reset();
		ExplicitConnectorKeys.Reset();
		ExplicitPreviewConnectorKeys.Reset();
		for (const TPair<uint64, FString>& Pair : ContinuationEdgeKeysByConnectorKey)
		{
			if (PlanningWindowStore != nullptr) PlanningWindowStore->ReleaseContinuationEndpointPair(Pair.Value);
		}
		ContinuationEdgeKeysByConnectorKey.Reset();
		ContinuationRouteIdsByConnectorKey.Reset();
		for (auto It = ContinuationRouteReservations.CreateIterator(); It; ++It)
		{
			if (!It.Value().bAutomatic) { It.RemoveCurrent(); continue; }
			It.Value().bReleased = true;
			It.Value().bAwaitingSubmission = false;
		}
		PendingExplicitPreparedRoutes.Reset();
		RetainedPreparedContinuationRoutesById.Reset();
		PendingExplicitPreparedSegmentsByConnectorKey.Reset();
		ExplicitRouteSegmentKeysByConnectorKey.Reset();
		ExplicitRoutesByConnectorKey.Reset();
		ExplicitContinuationCompletionCallbacks.Reset();
		PlanningRecordKeysBySiteRecordKey.Reset();
		ResetObservedChunkLoadStateForGenerationRestart();
		StampedChunkOrigins.Reset();
		PendingConnectorRefreshSolveHandles.Reset();
		PendingConnectorFrozenSubmissionDescriptorIdsByKey.Reset();
		PendingConnectorFrozenSubmissionGenerationsByKey.Reset();
		PendingConnectorFrozenSubmissionAttemptIndicesByKey.Reset();
		PendingConnectorFrozenSubmissionAuditHashesByKey.Reset();
		FrozenSubmissionStore = MakeShared<FLayoutFrozenSubmissionStore>();
		LastDebugGenerationStatsLines.Reset();
		EvictedAutomaticWork = 0;
		bLastAutomaticContinuationEnabled = false;
		++ConnectorRefreshGeneration;
		if (PlanningWindowStore != nullptr)
		{
			PlanningWindowStore->ResetPlannedLayoutSiteRecords();
		}
		return;
	}

	for (auto It = ResolvedConnectorRecords.CreateIterator(); It; ++It)
	{
		if (!It.Value().bHasBeenCommittedToChunkWorld)
		{
			ExplicitPreviewConnectorKeys.Remove(It.Key());
			ResolvedConnectorFrozenTerrainContracts.Remove(It.Key());
			It.RemoveCurrent();
		}
	}
}

void UChunkWorldLayoutRuntimeComponent::ResetObservedChunkLoadStateForGenerationRestart()
{
	CancelPlanningAreaDiscovery();
	CancelAutomaticRootWork();
	PlanningAreaQueue->ResetLoadedDirectory();
	CompletedDiscoveryAreas = 0;
	PlanningAreasByRecordKey.Reset();
	LastPlanningWindowUpdateTimeSeconds = TNumericLimits<double>::Lowest();
	TArray<FLayoutBackgroundSolveHandle> OldHandles;
	for (const FLayoutBackgroundSolveHandle& Handle : PendingConnectorRefreshSolveHandles)
	{
		if (!ExplicitConnectorKeys.Contains(Handle.LayoutGroupId)
			&& !PendingExplicitPreparedRoutes.Contains(Handle.LayoutGroupId)
			&& !ExplicitPreviewConnectorKeys.Contains(Handle.LayoutGroupId))
		{
			OldHandles.Add(Handle);
		}
	}
	for (const FLayoutBackgroundSolveHandle& Handle : OldHandles)
	{
		if (BackgroundSolveDispatcher.IsValid())
		{
			BackgroundSolveDispatcher->CancelGroup(Handle.LayoutGroupId);
		}
		CancelBackgroundLayoutSolve(Handle);
	}
	ObservedCoverageTileSize = FIntVector::ZeroValue;
	bAutomaticPlanningActive = false;
	bLoadedInfluenceDirty = true;
	bRealizationDirty = true;
	LoadedContinuationRootKeys.Reset();
	PlacedContinuationRootChunks.Reset();
	bContinuationReadinessDirty = false;
	LastConnectorEndpointRevision = 0;
	RetireInvalidAutomaticContinuations();

	FScopeLock PendingChunkLoadsLock(&PendingChunkLoadsMutex);
	PendingChunkLoads.Reset();
}

void UChunkWorldLayoutRuntimeComponent::QueueObservedLoadedChunk(const FIntVector& ChunkBlockWorldPos, const int32 DetailLevel)
{
	FChunkWorldObservedChunkLifecycleEvent Event;
	Event.ChunkBlockWorldPos = ChunkBlockWorldPos;
	Event.DetailLevel = DetailLevel;
	Event.EventType = EChunkWorldChunkLifecycleEventType::Updated;
	Event.bGeneratedForFirstTime = false;
	QueueObservedChunkLifecycle(Event);
}

void UChunkWorldLayoutRuntimeComponent::QueueObservedUnloadedChunk(const FIntVector& ChunkBlockWorldPos, const int32 DetailLevel)
{
	FScopeLock PendingChunkLoadsLock(&PendingChunkLoadsMutex);
	++ReceivedChunkEvents;
	FObservedChunkLoad& Observation = PendingChunkLoads.FindOrAdd(MakeTuple(DetailLevel, ChunkBlockWorldPos));
	Observation.ChunkBlockWorldPos = ChunkBlockWorldPos;
	Observation.DetailLevel = DetailLevel;
	Observation.EventType = EChunkWorldChunkLifecycleEventType::Updated;
	Observation.bUnloaded = true;
	Observation.bResetBeforeObservation = true;
}

void UChunkWorldLayoutRuntimeComponent::QueueObservedChunkLifecycle(const FChunkWorldObservedChunkLifecycleEvent& Event)
{
	FScopeLock PendingChunkLoadsLock(&PendingChunkLoadsMutex);
	++ReceivedChunkEvents;
	FObservedChunkLoad& ObservedChunk = PendingChunkLoads.FindOrAdd(MakeTuple(Event.DetailLevel, Event.ChunkBlockWorldPos));
	ObservedChunk.ChunkBlockWorldPos = Event.ChunkBlockWorldPos;
	ObservedChunk.DetailLevel = Event.DetailLevel;
	ObservedChunk.bUnloaded = false;
	if (ObservedChunk.EventType != EChunkWorldChunkLifecycleEventType::Created)
	{
		ObservedChunk.EventType = Event.EventType;
	}
}

bool UChunkWorldLayoutRuntimeComponent::ShouldAttemptRealization(const FResolvedLayoutSiteRecord& SiteRecord) const
{
	const FResolvedLayoutSiteRuntimeState RuntimeState =
		SiteRecord.GetResolvedSiteRuntimeState();

	// Reject impossible lifecycle state combinations with a diagnostic before
	// downstream logic can silently interpret corrupted data.
	if (RuntimeState.bLayoutRealized && !IsCachedSiteApplyable(RuntimeState))
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] LifecycleStateDiag: site claims realized without applyable publication outcome. siteCenter=%s rootSolveId=%s"),
			*SiteRecord.SiteCenterBlockWorldPos.ToString(),
			*SiteRecord.RootSolveId.ToString());
		return false;
	}
	if (RuntimeState.bHasBeenCommittedToChunkWorld && !RuntimeState.bLayoutRealized)
	{
		UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] LifecycleStateDiag: site claims committed without realized state. siteCenter=%s rootSolveId=%s"),
			*SiteRecord.SiteCenterBlockWorldPos.ToString(),
			*SiteRecord.RootSolveId.ToString());
		return false;
	}

	return IsCachedSiteApplyable(RuntimeState)
		&& !RuntimeState.bLayoutRealized
		&& !RuntimeState.bHasBeenCommittedToChunkWorld;
}

bool UChunkWorldLayoutRuntimeComponent::CanStampRequiredChunkOrigins(
	const TSet<FIntVector>& RequiredChunkOrigins,
	const FLayoutId ArtifactId,
	const FLayoutId RootSolveId,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	for (const FIntVector& ChunkOrigin : RequiredChunkOrigins)
	{
		const FLayoutChunkStampMark* const ExistingMark = StampedChunkOrigins.Find(ChunkOrigin);
		if (ExistingMark == nullptr)
		{
			continue;
		}
		if (ExistingMark->bLoadedFromSave)
		{
			OutFailureReason = FString::Printf(
				TEXT("Chunk origin %s was loaded from save and cannot receive layout stamps."),
				*ChunkOrigin.ToString());
			return false;
		}
		if (const FLayoutId* const ExistingArtifactId =
			ExistingMark->StampedArtifactIdsByRootSolveId.Find(RootSolveId))
		{
			if (*ExistingArtifactId != ArtifactId)
			{
				OutFailureReason = FString::Printf(
					TEXT("Chunk origin %s already stamped stable root %s with artifact %s; cannot replace it with artifact %s."),
					*ChunkOrigin.ToString(),
					*RootSolveId.ToString(),
					*ExistingArtifactId->ToString(),
					*ArtifactId.ToString());
				return false;
			}
		}
	}

	return true;
}

void UChunkWorldLayoutRuntimeComponent::MarkRequiredChunkOriginsStamped(
	const TSet<FIntVector>& RequiredChunkOrigins,
	const FLayoutId ArtifactId,
	const FLayoutId RootSolveId)
{
	bLoadedInfluenceDirty = true;
	for (const FIntVector& ChunkOrigin : RequiredChunkOrigins)
	{
		FLayoutChunkStampMark& StampMark = StampedChunkOrigins.FindOrAdd(ChunkOrigin);
		StampMark.StampedArtifactIdsByRootSolveId.Add(RootSolveId, ArtifactId);
	}
}

FIntVector UChunkWorldLayoutRuntimeComponent::ComputeFootprintMinBlockWorldPos(
	const FResolvedLayoutSiteRecord& SiteRecord,
	const FIntVector& SharedCellSizeInBlocks)
{
	const FResolvedLayoutSiteLocationMetadata LocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	if (LocationMetadata.RealizedFootprintMinBlockWorldPos != FIntVector::ZeroValue)
	{
		return LocationMetadata.RealizedFootprintMinBlockWorldPos;
	}

	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	return FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
		LocationMetadata.SiteCenterBlockWorldPos,
		SolvedPayload.SolveResult.FootprintSize,
		SharedCellSizeInBlocks);
}

FIntVector UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
	const FResolvedLayoutSiteRecord& SiteRecord,
	const FLayoutPlacedModule& Placement,
	const FIntVector& SharedCellSizeInBlocks)
{
	return FLayoutStreamingWindow::ComputeSitePlacementAnchorBlockWorldPos(
		SiteRecord,
		Placement,
		SharedCellSizeInBlocks);
}

#if WITH_AUTOMATION_TESTS
double UChunkWorldLayoutRuntimeComponent::GetLastSelectedSiteTerrainMillisecondsForTesting()
{
	return GLastSelectedSiteTerrainMilliseconds;
}

FIntPoint UChunkWorldLayoutRuntimeComponent::ResolvePlacementTerrainFootprintSizeInBlocksForTesting(
	const FLayoutPlacedModule& Placement,
	const FIntVector& SharedCellSizeInBlocks)
{
	return ResolvePlacementTerrainFootprintSizeInBlocks(Placement, SharedCellSizeInBlocks);
}
#endif

void UChunkWorldLayoutRuntimeComponent::RecordLoadedChunk(
	const FIntVector& Position, const int32 DetailLevel, const bool bCreated)
{
	AChunkWorldExtended* const World = GetOwningChunkWorld();
	if (!World || !World->WorldGenDef || DetailLevel < 0
		|| DetailLevel >= World->GetChunkLayerCount()) return;
	const CChunkData* Data = World->WorldChunks[DetailLevel];
	if (!Data || Data->ChunkBlockFactor.GetMin() <= 0) return;
	ObservedCoverageTileSize = World->WorldGenDef->ChunkBlockSize;
	PlanningAreaQueue->ResizeLoadedLayers(World->GetChunkLayerCount());
	const FIntVector Origin = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(Position, Data->ChunkBlockFactor);
	const bool bChanged = PlanningAreaQueue->Observe(MakeTuple(DetailLevel, Origin), Data->ChunkBlockFactor, bCreated);
	bContinuationReadinessDirty |= bChanged;
	bLoadedInfluenceDirty |= bChanged;
	bRealizationDirty |= bChanged;
}

TArray<int32> UChunkWorldLayoutRuntimeComponent::ReadLoadedTerrainMaterials(const TArray<FIntVector>& Positions) const
{
	check(IsInGameThread());
	AChunkWorldCore* const World = GetOwningChunkWorld();
	if (!World || !World->IsRunning()) return {};
	TArray<int32> Materials;
	Materials.Reserve(Positions.Num());
	for (int32 Begin = 0; Begin < Positions.Num();)
	{
		const int32 Level = FLayoutStreamingWindow::FindLoadedLayerAtPosition(Positions[Begin], ObservedChunkLayers);
		if (Level == INDEX_NONE || Level >= World->GetChunkLayerCount() || !World->WorldChunks[Level]) return {};
		int32 End = Begin + 1;
		while (End < Positions.Num()
			&& FLayoutStreamingWindow::FindLoadedLayerAtPosition(Positions[End], ObservedChunkLayers) == Level) ++End;
		TArray<FIntVector> Batch;
		Batch.Append(Positions.GetData() + Begin, End - Begin);
		// The native index overload dispatches to the extension's existing per-chunk batch override.
		const TArray<int32> Values = World->GetBlockValuesByBlockWorldPosLevel(Batch, Level, ERessourceType::MaterialIndex, 0);
		if (Values.Num() != Batch.Num()) return {};
		Materials.Append(Values);
		Begin = End;
	}
	return Materials;
}

void UChunkWorldLayoutRuntimeComponent::RecordFinestLoadedChunk(const FIntVector& Position, const bool bCreated)
{
	if (AChunkWorldExtended* World = GetOwningChunkWorld())
	{
		RecordLoadedChunk(Position, World->GetChunkLayerCount() - 1, bCreated);
	}
}

int32 UChunkWorldLayoutRuntimeComponent::GetObservedLoadedChunkCount() const
{
	int32 Count = 0;
	for (const FLayoutLoadedChunkLayer& Layer : ObservedChunkLayers) Count += Layer.Chunks.Num();
	return Count;
}

bool UChunkWorldLayoutRuntimeComponent::AreRequiredChunkOriginsLoaded(
	const TSet<FIntVector>& Origins, const bool bRequireCreated) const
{
	if (Origins.IsEmpty() || ObservedCoverageTileSize.GetMin() <= 0) return false;
	for (const FIntVector& Origin : Origins)
	{
		FIntVector Max;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			Max[Axis] = static_cast<int32>(FMath::Min<int64>(MAX_int32,
				static_cast<int64>(Origin[Axis]) + ObservedCoverageTileSize[Axis] - 1));
		}
		if (!FLayoutStreamingWindow::IsBlockBoxCovered(Origin, Max, ObservedChunkLayers, bRequireCreated)) return false;
	}
	return true;
}

void UChunkWorldLayoutRuntimeComponent::HandleObservedLoadedChunk(const FObservedChunkLoad& ObservedChunk)
{
	AChunkWorldExtended* const World = GetOwningChunkWorld();
	if (!World || !World->HasAuthority()) return;
	if (ObservedChunk.bUnloaded || ObservedChunk.bResetBeforeObservation)
	{
		if (ObservedChunkLayers.IsValidIndex(ObservedChunk.DetailLevel))
		{
			const bool bChanged = PlanningAreaQueue->Forget(MakeTuple(ObservedChunk.DetailLevel, ObservedChunk.ChunkBlockWorldPos));
			bContinuationReadinessDirty |= bChanged;
			bLoadedInfluenceDirty |= bChanged;
			bRealizationDirty |= bChanged;
		}
		if (ObservedChunk.bUnloaded) return;
	}
	// The shared actor flag intentionally describes finest-detail generation for other listeners.
	// Layouts trust Created intent at every LOD; Updated preserves existing authority and stamp history.
	RecordLoadedChunk(ObservedChunk.ChunkBlockWorldPos, ObservedChunk.DetailLevel,
		ObservedChunk.EventType == EChunkWorldChunkLifecycleEventType::Created);
}


bool UChunkWorldLayoutRuntimeComponent::RequiresFreshCreatedChunkRealizationGate(
	const FResolvedLayoutSiteRecord& SiteRecord) const
{
	return HasWorldBindingFrontendSelection(SiteRecord.GetWorldBindingFrontendSelection());
}

bool UChunkWorldLayoutRuntimeComponent::RequiresFreshCreatedChunkRealizationGate(
	const FResolvedLayoutConnectorRecord& ConnectorRecord) const
{
	return true;
}

bool UChunkWorldLayoutRuntimeComponent::HasFreshCreatedChunkEligibility(
	const TSet<FIntVector>& RequiredChunkOrigins) const
{
	for (const FIntVector& ChunkOrigin : RequiredChunkOrigins)
	{
		const FLayoutChunkStampMark* Mark = StampedChunkOrigins.Find(ChunkOrigin);
		if (Mark && Mark->bLoadedFromSave) return false;
	}
	return AreRequiredChunkOriginsLoaded(RequiredChunkOrigins, true);
}

void UChunkWorldLayoutRuntimeComponent::TryRealizeEligibleSites()
{
	bool bRemovedRejectedAutoDiscoveredSite = false;
	for (auto It = ResolvedSiteRecords.CreateIterator(); It; ++It)
	{
		FResolvedLayoutSiteRecord& SiteRecord = It.Value();
		if (!ShouldAttemptRealization(SiteRecord))
		{
			continue;
		}

		const TSet<FIntVector> RequiredChunkOrigins = CollectRequiredChunkOrigins(SiteRecord);
		if (!AreRequiredChunkOriginsLoaded(RequiredChunkOrigins))
		{
			continue;
		}

		if (RequiresFreshCreatedChunkRealizationGate(SiteRecord)
			&& !HasFreshCreatedChunkEligibility(RequiredChunkOrigins))
		{
			continue;
		}

		const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride = nullptr;
		FLayoutFrozenTerrainContract StoredFrozenTerrainContract;
		if (const FString* const PlanningRecordKey = PlanningRecordKeysBySiteRecordKey.Find(It.Key()))
		{
			if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
			{
				FPlannedLayoutSiteRecord PlannedRecord;
				if (Store->TryGetPlannedLayoutSiteRecord(*PlanningRecordKey, PlannedRecord))
				{
					const FLayoutPlannedSiteAcceptedSolvePayload AcceptedSolvePayload =
						PlannedRecord.GetPlannedSiteAcceptedSolvePayload();
					if (!AcceptedSolvePayload.FrozenTerrainContract.ContractId.IsNone())
					{
						FString RealizationInputFailureReason;
						if (!LayoutRealizationWritePlan::ValidateAcceptedSolvePayloadForRealizationInputs(
								AcceptedSolvePayload,
								RealizationInputFailureReason))
						{
							RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
								*PlanningRecordKey,
								RealizationInputFailureReason);
							UE_LOG(
								LogTemp,
								Warning,
								TEXT("Layout site realization skipped accepted planned-site '%s': %s"),
								**PlanningRecordKey,
								*RealizationInputFailureReason);
							continue;
						}
						StoredFrozenTerrainContract = AcceptedSolvePayload.FrozenTerrainContract;
						FrozenTerrainContractOverride = &StoredFrozenTerrainContract;
					}
				}
			}
		}

		if ((FrozenTerrainContractOverride == nullptr || FrozenTerrainContractOverride->ContractId.IsNone())
			&& !SiteRecord.GetRootPublicationMetadata().RootSolveId.IsNone())
		{
			if (const FLayoutFrozenTerrainContract* const RootFrozenTerrainContract =
				ResolvedRootFrozenTerrainContracts.Find(SiteRecord.GetRootPublicationMetadata().RootSolveId))
			{
				FrozenTerrainContractOverride = RootFrozenTerrainContract;
			}
		}

		FString FailureReason;
		if (TryRealizeSite(SiteRecord, &FailureReason, FrozenTerrainContractOverride))
		{
			MarkResolvedSiteRealizedAndCommitted(SiteRecord);
			const FString* RootPlanningKey = PlanningRecordKeysBySiteRecordKey.Find(It.Key());
			PlacedContinuationRootChunks.Add(RootPlanningKey != nullptr ? *RootPlanningKey : It.Key(),
				CollectRequiredChunkOrigins(SiteRecord));
			RefreshPlacedContinuationRootReadiness();
			if (const FString* const PlanningRecordKey = PlanningRecordKeysBySiteRecordKey.Find(It.Key()))
			{
				if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
				{
					const FResolvedLayoutSiteRuntimeState RuntimeState =
						SiteRecord.GetResolvedSiteRuntimeState();
					Store->UpdatePlannedLayoutSiteRecordTerrainFitDiagnostic(
						*PlanningRecordKey,
						RuntimeState.TerrainFitDiagnosticKind);
					Store->MarkPlannedLayoutSiteRecordRealized(*PlanningRecordKey);
				}
			}
		}
		else if (!FailureReason.IsEmpty())
		{
			if (RejectAutoDiscoveredResolvedSiteAfterRealizationFailure(
				It.Key(),
				SiteRecord,
				FailureReason))
			{
				bRemovedRejectedAutoDiscoveredSite = true;
				It.RemoveCurrent();
				continue;
			}

			if (const FString* const PlanningRecordKey = PlanningRecordKeysBySiteRecordKey.Find(It.Key()))
			{
				if (ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore())
				{
					const FResolvedLayoutSiteRuntimeState RuntimeState =
						SiteRecord.GetResolvedSiteRuntimeState();
					Store->UpdatePlannedLayoutSiteRecordTerrainFitDiagnostic(
						*PlanningRecordKey,
						RuntimeState.TerrainFitDiagnosticKind);
				}
			}

			const FResolvedLayoutSiteLocationMetadata LocationMetadata =
				SiteRecord.GetResolvedSiteLocationMetadata();
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Layout site realization failed at %s: %s"),
				*LocationMetadata.SiteCenterBlockWorldPos.ToString(),
				*FailureReason);
		}
	}

	if (bRemovedRejectedAutoDiscoveredSite)
	{
		RefreshConnectorRecords();
	}
}

void UChunkWorldLayoutRuntimeComponent::RefreshPlacedContinuationRootReadiness()
{
	bContinuationReadinessDirty = false;
	bool bChanged = false;
	for (const auto& Pair : PlacedContinuationRootChunks)
	{
		const bool bLoaded = !Pair.Value.IsEmpty()
			&& AreRequiredChunkOriginsLoaded(Pair.Value);
		if (bLoaded == LoadedContinuationRootKeys.Contains(Pair.Key)) continue;
		if (bLoaded) LoadedContinuationRootKeys.Add(Pair.Key);
		else LoadedContinuationRootKeys.Remove(Pair.Key);
		bChanged = true;
	}
	if (bChanged)
	{
		LastConnectorEndpointRevision = 0;
		RetireInvalidAutomaticContinuations();
	}
}

bool UChunkWorldLayoutRuntimeComponent::IsContinuationRouteEligible(const FLayoutId RouteId) const
{
	const FLayoutPreparedContinuationRoute* Prepared = RetainedPreparedContinuationRoutesById.Find(RouteId);
	return Prepared != nullptr
		&& LoadedContinuationRootKeys.Contains(Prepared->Route.StartRootEndpoint.RootRecordKey)
		&& LoadedContinuationRootKeys.Contains(Prepared->Route.EndRootEndpoint.RootRecordKey);
}

void UChunkWorldLayoutRuntimeComponent::RetireAutomaticContinuationRoute(const FLayoutId RouteId)
{
	const FLayoutContinuationRouteReservationState* State = ContinuationRouteReservations.Find(RouteId);
	if (State == nullptr || !State->bAutomatic || State->bReleased) return;
	const TArray<uint64> Keys = State->SegmentKeys.Array();
	for (const uint64 Key : Keys)
	{
		if (BackgroundSolveDispatcher) BackgroundSolveDispatcher->CancelGroup(Key);
		TombstoneConnectorFrozenSubmissionDescriptor(Key, PendingConnectorFrozenSubmissionDescriptorIdsByKey.FindRef(Key));
		const auto* Record = ResolvedConnectorRecords.Find(Key);
		if (!Record || !Record->GetResolvedConnectorRuntimeState().bHasBeenCommittedToChunkWorld)
		{
			ResolvedConnectorRecords.Remove(Key);
			ResolvedConnectorFrozenTerrainContracts.Remove(Key);
		}
	}
	ReleaseContinuationRouteReservation(RouteId);
}

void UChunkWorldLayoutRuntimeComponent::RetireInvalidAutomaticContinuations(const bool bRetireAll)
{
	TArray<uint64> CanceledPreparations;
	for (const auto& Pair : PendingAutomaticContinuationPreparations)
	{
		const auto* Edge = PlanningWindowStore != nullptr
			? PlanningWindowStore->FindContinuationEdgeRecord(ContinuationEdgeKeysByConnectorKey.FindRef(Pair.Key)) : nullptr;
		if (bRetireAll || Edge == nullptr || !LoadedContinuationRootKeys.Contains(Edge->StartRootRecordKey)
			|| !LoadedContinuationRootKeys.Contains(Edge->EndRootRecordKey)) CanceledPreparations.Add(Pair.Key);
	}
	for (const uint64 Key : CanceledPreparations)
	{
		const auto Handle = PendingAutomaticContinuationPreparations.FindChecked(Key);
		if (BackgroundSolveDispatcher.IsValid()) BackgroundSolveDispatcher->CancelGroup(Handle.LayoutGroupId);
		if (!BackgroundSolveDispatcher || !BackgroundSolveDispatcher->HasRetainedGroup(Handle.LayoutGroupId))
			PendingAutomaticContinuationPreparations.Remove(Key);
		ReleaseContinuationReservationForConnector(Key);
		LastConnectorEndpointRevision = 0;
	}
	TArray<FLayoutId> Expired;
	for (const auto& Pair : RetainedPreparedContinuationRoutesById)
	{
		if (bRetireAll || !IsContinuationRouteEligible(Pair.Key)) Expired.Add(Pair.Key);
	}
	for (const FLayoutId RouteId : Expired) RetireAutomaticContinuationRoute(RouteId);
}

void UChunkWorldLayoutRuntimeComponent::SubmitAutomaticContinuationPreparation(const FResolvedLayoutConnectorRecord& RouteRecord)
{
	const uint64 Key = BuildResolvedConnectorRecordKey(RouteRecord);
	const FString EdgeKey = ContinuationEdgeKeysByConnectorKey.FindRef(Key);
	if (PendingAutomaticContinuationPreparations.Contains(Key)) return;
	for (const auto& Pair : ContinuationRouteReservations)
		if (Pair.Value.EdgeKey == EdgeKey) return;
	if (!MakeAutomaticPlanningRoom(RouteRecord.StartEndpointBlockWorldPos))
	{
		ReleaseContinuationReservationForConnector(Key);
		return;
	}
	auto* Owner = GetOwningChunkWorld();
	if (Owner == nullptr || Owner->WorldGenDef == nullptr)
	{
		ReleaseContinuationReservationForConnector(Key);
		return;
	}
	const FGuid Transport = FGuid::NewGuid();
	const uint64 GroupId = ((uint64(Transport.A) << 32) | Transport.B) | 1;
	FLayoutBackgroundSolveHandle Pending;
	Pending.LayoutGroupId = GroupId;
	PendingAutomaticContinuationPreparations.Add(Key, Pending);
	bool bSubmitted = false;
	ON_SCOPE_EXIT { if (!bSubmitted) PendingAutomaticContinuationPreparations.Remove(Key); };
	const int32 WorldSeed = ResolveLayoutWorldSeed();
	FString Failure;
	const auto Inputs = MakeShared<FLayoutContinuationPlanningSnapshot, ESPMode::ThreadSafe>();
	const auto Noise = FLayoutActiveBiomeNoiseSnapshot::CaptureFromWorldDefinition(this, Owner->WorldGenDef, WorldSeed, Failure);
	if (!Noise->IsInitialized() || !FLayoutConnectorPlanning::CaptureContinuationPlanningInputs(
		RouteRecord, ResolveRuntimeWorldBindingForConnectorRecord(LayoutWorldBindings, RouteRecord), WorldSeed, *Inputs, Failure))
	{
		ReportLayoutPlanningWarning(this, GetDetailedDiagnostics(), Failure);
		ReleaseContinuationReservationForConnector(Key, true);
		return;
	}
	const auto Prepared = MakeShared<FLayoutPreparedContinuationRoute, ESPMode::ThreadSafe>();
	const auto Coordinates = MakeCoordinateSettings(Owner->WorldGenDef);
	const auto RootFootprints = CaptureContinuationRootFootprints();
	FLayoutBackgroundSolveSubmission Submission;
	Submission.LayoutGroupId = GroupId;
	Submission.DebugName = FString::Printf(TEXT("ContinuationPreparation.%llu"), Key);
	Submission.Tier = ELayoutBackgroundSolveJobTier::Continuation;
	Submission.LifecycleStage = ELayoutBackgroundSolveLifecycleStage::Preparation;
	Submission.Priority = FLayoutPlanningAreaQueue::ComputePriority(RouteRecord.StartEndpointBlockWorldPos, CollectPlanningWindowCenters());
	Submission.Work = [Inputs, Noise, Prepared, Coordinates, RootFootprints, RouteRecord, WorldSeed](const FLayoutSolveCancellationToken& Token, FString& Reason)
	{
		LayoutSolveCancellation::FThreadTokenScope Scope(Token);
		FLayoutConnectorTerrainPathContext Context;
		Context.ActiveBiomeSampler = &Noise->GetSampler();
		Context.CoordinateSettings = Coordinates;
		Context.RootFootprints = RootFootprints;
		return !Token.IsCancellationRequested() && FLayoutConnectorPlanning::TryPrepareContinuationRoute(
			RouteRecord, *Inputs, WorldSeed, &Context, *Prepared, Reason) && !Token.IsCancellationRequested();
	};
	Submission.PublishOnGameThread = [WeakThis = TWeakObjectPtr<UChunkWorldLayoutRuntimeComponent>(this), Prepared, Key, EdgeKey, GroupId](const FLayoutBackgroundSolveCompletion& Completion)
	{
		auto* Runtime = WeakThis.Get();
		if (Runtime == nullptr) return;
		const auto* Pending = Runtime->PendingAutomaticContinuationPreparations.Find(Key);
		if (Pending == nullptr || Pending->LayoutGroupId != GroupId) return;
		Runtime->PendingAutomaticContinuationPreparations.Remove(Key);
		Runtime->LastConnectorEndpointRevision = 0;
		const auto* Store = Runtime->PlanningWindowStore.Get();
		const auto* Edge = Store != nullptr ? Store->FindContinuationEdgeRecord(EdgeKey) : nullptr;
		const auto* World = Runtime->GetOwningChunkWorld();
		if (Edge == nullptr || !Runtime->bEnablePlanningWindowRuntimeUpdates || World == nullptr
			|| !World->HasAuthority() || !World->IsRunning() || Runtime->CollectPlanningWindowCenters().IsEmpty()
			|| !Runtime->LoadedContinuationRootKeys.Contains(Edge->StartRootRecordKey)
			|| !Runtime->LoadedContinuationRootKeys.Contains(Edge->EndRootRecordKey))
		{
			Runtime->ReleaseContinuationReservationForConnector(Key);
			return;
		}
		if (!Completion.bWorkSucceeded)
		{
			if (Runtime->GetDetailedDiagnostics())
				UE_LOG(LogTemp, Display, TEXT("[LayoutContinuation] phase=preparation attemptGroup=%llu edge=%s startRoot=%s startEntry=%s start=%s endRoot=%s endEntry=%s end=%s canceled=%d failure=%s"),
					GroupId, *EdgeKey, *Edge->StartRootRecordKey, *Edge->StartEndpointKey, *Edge->StartEndpointBlockWorldPos.ToString(),
					*Edge->EndRootRecordKey, *Edge->EndEndpointKey, *Edge->EndEndpointBlockWorldPos.ToString(), Completion.bCanceled, *Completion.FailureReason);
			Runtime->ReleaseContinuationReservationForConnector(Key, !Completion.bCanceled);
			return;
		}
		// Roots may have settled while preparation ran. Recheck before retaining any segment.
		const auto CurrentFootprints = Runtime->CaptureContinuationRootFootprints();
		FString ClearanceFailure;
		for (const auto& Segment : Prepared->Segments)
		{
			if (Segment.PreparedContinuation.IsSet()
				&& !FLayoutConnectorPlanning::ValidateContinuationClearance(
					Segment.PreparedContinuation->PathOriginBlockWorldPos,
					Segment.PreparedContinuation->ConnectorRecord.GetResolvedConnectorFrontendSelection().SharedCellSizeInBlocks,
					Segment.PreparedContinuation->PlannedCells, CurrentFootprints, ClearanceFailure))
			{
				ReportLayoutPlanningWarning(Runtime, Runtime->GetDetailedDiagnostics(), ClearanceFailure);
				Runtime->ReleaseContinuationReservationForConnector(Key, true);
				return;
			}
		}
		TArray<uint64> SegmentKeys;
		for (const auto& Segment : Prepared->Segments) SegmentKeys.Add(BuildResolvedConnectorRecordKey(Segment.ConnectorRecord));
		if (SegmentKeys.IsEmpty())
		{
			Runtime->ReleaseContinuationReservationForConnector(Key, true);
			return;
		}
		const bool bForward = Prepared->Route.StartRootEndpoint.EndpointBlockWorldPos == Edge->StartEndpointBlockWorldPos;
		Prepared->Route.StartRootEndpoint.RootRecordKey = bForward ? Edge->StartRootRecordKey : Edge->EndRootRecordKey;
		Prepared->Route.EndRootEndpoint.RootRecordKey = bForward ? Edge->EndRootRecordKey : Edge->StartRootRecordKey;
		const FLayoutId RouteId(*Prepared->Route.RouteKey);
		Runtime->RegisterContinuationRouteReservation(RouteId, EdgeKey, SegmentKeys);
		Runtime->ContinuationRouteReservations.FindChecked(RouteId).bAutomatic = true;
		Runtime->ContinuationRouteReservations.FindChecked(RouteId).bAwaitingSubmission = true;
		Runtime->RetainedPreparedContinuationRoutesById.Add(RouteId, MoveTemp(*Prepared));
		if (!SegmentKeys.Contains(Key)) Runtime->ContinuationEdgeKeysByConnectorKey.Remove(Key);
		Runtime->RefreshConnectorRecords(true);
	};
	const auto Handle = GetOrCreateBackgroundSolveDispatcher().Submit(MoveTemp(Submission));
	bSubmitted = Handle.IsValid();
	if (auto* Current = PendingAutomaticContinuationPreparations.Find(Key); Current != nullptr && Current->LayoutGroupId == GroupId)
	{
		if (Handle.IsValid()) *Current = Handle;
		else
		{
			PendingAutomaticContinuationPreparations.Remove(Key);
			ReleaseContinuationReservationForConnector(Key);
			LastConnectorEndpointRevision = 0;
		}
	}
}

void UChunkWorldLayoutRuntimeComponent::RefreshConnectorRecords(const bool bForceRefresh)
{
	if (bRefreshingConnectors) return;
	TGuardValue<bool> RefreshGuard(bRefreshingConnectors, true);
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	const bool bAutomaticEnabled = bEnablePlanningWindowRuntimeUpdates && ChunkWorld != nullptr
		&& ChunkWorld->HasAuthority() && ChunkWorld->IsRunning() && !CollectPlanningWindowCenters().IsEmpty();
	if (bAutomaticEnabled != bLastAutomaticContinuationEnabled)
	{
		bLastAutomaticContinuationEnabled = bAutomaticEnabled;
		LastConnectorEndpointRevision = 0;
		if (!bAutomaticEnabled) RetireInvalidAutomaticContinuations(true);
	}
	RetireInvalidAutomaticContinuations(!bAutomaticEnabled);
	if (!PendingAutomaticContinuationPreparations.IsEmpty()) return;
	// The first-stage handle may already have retired while its successor still runs.
	if (BackgroundSolveDispatcher.IsValid()
		&& PendingConnectorRefreshSolveHandles.ContainsByPredicate([this](const FLayoutBackgroundSolveHandle& Handle)
		{
			return BackgroundSolveDispatcher->HasPendingGroup(Handle.LayoutGroupId);
		}))
	{
		return;
	}
	ULayoutPlanningWindowStore* const PlanningStore = GetOrCreateLayoutPlanningWindowStore();
	if (PlanningStore == nullptr) return;
	const uint64 EndpointRevision = PlanningStore->GetContinuationEndpointRevision();
	if (!bForceRefresh && PendingExplicitPreparedRoutes.IsEmpty() && LastConnectorEndpointRevision == EndpointRevision) return;
	LastConnectorEndpointRevision = EndpointRevision;
	bool bHasPreparedAutomaticRoute = false;
	for (const auto& Pair : ContinuationRouteReservations) bHasPreparedAutomaticRoute |= Pair.Value.bAwaitingSubmission;
	if (!bHasPreparedAutomaticRoute && PendingExplicitPreparedRoutes.IsEmpty()
		&& (CountAutomaticPlanningWork() >= FMath::Max(1, MaxCachedPlanningChunks)
			|| GetOrCreateBackgroundSolveDispatcher().GetDiagnosticsSnapshot().InProgressLayoutGroups >= BuildBackgroundSolveSettings().ResolveMaxConcurrentBackgroundLayoutSolves()))
	{
		LastConnectorEndpointRevision = 0;
		return;
	}
	if (!PendingExplicitPreparedRoutes.IsEmpty()) LastConnectorEndpointRevision = 0;

	TArray<uint64> PendingConnectorKeys;
	PendingConnectorFrozenSubmissionDescriptorIdsByKey.GenerateKeyArray(PendingConnectorKeys);
	for (const uint64 ConnectorKey : PendingConnectorKeys)
	{
		const FLayoutId DescriptorId = PendingConnectorFrozenSubmissionDescriptorIdsByKey.FindRef(ConnectorKey);
		TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, DescriptorId);
	}
	for (const FLayoutBackgroundSolveHandle& PendingHandle : PendingConnectorRefreshSolveHandles)
	{
		GetOrCreateBackgroundSolveDispatcher().CancelGroup(PendingHandle.LayoutGroupId);
	}
	PendingConnectorRefreshSolveHandles.Reset();

	// Retire abandoned reservations only after every submitted lifecycle group has settled.
	TArray<uint64> ReleasedAutomaticConnectorKeys;
	for (const TPair<uint64, FString>& Pair : ContinuationEdgeKeysByConnectorKey)
	{
		if (!ExplicitConnectorKeys.Contains(Pair.Key)
			&& !PendingExplicitPreparedRoutes.Contains(Pair.Key)
			&& !ResolvedConnectorRecords.Contains(Pair.Key)
			&& !ContinuationRouteIdsByConnectorKey.Contains(Pair.Key))
		{
			ReleasedAutomaticConnectorKeys.Add(Pair.Key);
		}
	}
	for (const uint64 ConnectorKey : ReleasedAutomaticConnectorKeys)
	{
		ReleaseContinuationReservationForConnector(ConnectorKey);
	}

	++ConnectorRefreshGeneration;
	const uint64 RefreshGeneration = ConnectorRefreshGeneration;
	TArray<FLayoutPlanningWindowEndpointRecord> RetainedEndpointRecords = bAutomaticEnabled
		? PlanningStore->GetContinuationEndpointRecords() : TArray<FLayoutPlanningWindowEndpointRecord>();
	RetainedEndpointRecords.RemoveAll([this](const FLayoutPlanningWindowEndpointRecord& Endpoint)
	{
		return !LoadedContinuationRootKeys.Contains(Endpoint.RootRecordKey);
	});
	if ((LayoutWorldBindings.IsEmpty() || RetainedEndpointRecords.Num() < 2)
		&& PendingExplicitPreparedRoutes.IsEmpty())
	{
		return;
	}

	TArray<ULayoutWorldBindingAsset*> WorldBindings = GetLayoutWorldBindings();
	TArray<FLayoutPlanningWindowEndpointRecord> SortedEndpointRecords = RetainedEndpointRecords;
	SortedEndpointRecords.Sort([](const FLayoutPlanningWindowEndpointRecord& Left, const FLayoutPlanningWindowEndpointRecord& Right)
	{
		return Left.StableEndpointKey < Right.StableEndpointKey;
	});
	struct FAutomaticEndpointPair
	{
		int32 StartIndex = INDEX_NONE;
		int32 EndIndex = INDEX_NONE;
		int32 UnconnectedRootCount = 0;
		double DistanceSquared = TNumericLimits<double>::Max();
	};
	TArray<int32> RootConnectionCounts;
	for (const FLayoutPlanningWindowEndpointRecord& Endpoint : SortedEndpointRecords)
	{
		RootConnectionCounts.Add(PlanningStore->CountContinuationRootConnections(Endpoint.RootRecordKey, Endpoint.ContinuationFamilyId));
	}
	TArray<FAutomaticEndpointPair> AutomaticPairs;
	for (int32 StartIndex = 0; StartIndex < SortedEndpointRecords.Num(); ++StartIndex)
	{
		const FLayoutPlanningWindowEndpointRecord& Start = SortedEndpointRecords[StartIndex];
		if (!LoadedContinuationRootKeys.Contains(Start.RootRecordKey)
			|| Start.State != ELayoutContinuationEndpointState::Ready || Start.RemainingConnections <= 0
			|| RootConnectionCounts[StartIndex] >= Start.MaxConnectionsPerSite)
		{
			continue;
		}
		for (int32 EndIndex = StartIndex + 1; EndIndex < SortedEndpointRecords.Num(); ++EndIndex)
		{
			const FLayoutPlanningWindowEndpointRecord& End = SortedEndpointRecords[EndIndex];
			if (!LoadedContinuationRootKeys.Contains(End.RootRecordKey)
				|| End.State != ELayoutContinuationEndpointState::Ready || End.RemainingConnections <= 0
				|| RootConnectionCounts[EndIndex] >= End.MaxConnectionsPerSite
				|| Start.RootRecordKey == End.RootRecordKey || Start.WorldBindingId != End.WorldBindingId
				|| Start.ContinuationFamilyId != End.ContinuationFamilyId
				|| Start.Endpoint.ConnectorTypeTag != End.Endpoint.ConnectorTypeTag)
			{
				continue;
			}
			FAutomaticEndpointPair& Pair = AutomaticPairs.AddDefaulted_GetRef();
			Pair.StartIndex = StartIndex;
			Pair.EndIndex = EndIndex;
			Pair.UnconnectedRootCount = int32(RootConnectionCounts[StartIndex] == 0) + int32(RootConnectionCounts[EndIndex] == 0);
			Pair.DistanceSquared = FVector::DistSquared(FVector(Start.Endpoint.EndpointBlockWorldPos), FVector(End.Endpoint.EndpointBlockWorldPos));
		}
	}
	AutomaticPairs.Sort([&SortedEndpointRecords](const FAutomaticEndpointPair& Left, const FAutomaticEndpointPair& Right)
	{
		if (Left.UnconnectedRootCount != Right.UnconnectedRootCount) return Left.UnconnectedRootCount > Right.UnconnectedRootCount;
		if (Left.DistanceSquared != Right.DistanceSquared) return Left.DistanceSquared < Right.DistanceSquared;
		const FString& LeftStart = SortedEndpointRecords[Left.StartIndex].StableEndpointKey;
		const FString& RightStart = SortedEndpointRecords[Right.StartIndex].StableEndpointKey;
		if (LeftStart != RightStart) return LeftStart < RightStart;
		return SortedEndpointRecords[Left.EndIndex].StableEndpointKey < SortedEndpointRecords[Right.EndIndex].StableEndpointKey;
	});
	TArray<FResolvedLayoutConnectorRecord> ConnectorRecords;
	TSet<FString> ReservedEndpointKeys;
	TSet<uint64> SelectedConnectorKeys;
	for (const FAutomaticEndpointPair& Pair : AutomaticPairs)
	{
		// One capture per refresh preserves bounded game-thread admission work.
		if (bHasPreparedAutomaticRoute || !PendingExplicitPreparedRoutes.IsEmpty() || !ConnectorRecords.IsEmpty()) break;
		const FLayoutPlanningWindowEndpointRecord& Start = SortedEndpointRecords[Pair.StartIndex];
		const FLayoutPlanningWindowEndpointRecord& End = SortedEndpointRecords[Pair.EndIndex];
		if (ReservedEndpointKeys.Contains(Start.StableEndpointKey) || ReservedEndpointKeys.Contains(End.StableEndpointKey))
		{
			continue;
		}
		ULayoutWorldBindingAsset* WorldBinding = nullptr;
		for (ULayoutWorldBindingAsset* CandidateBinding : WorldBindings)
		{
			if (CandidateBinding != nullptr
				&& (CandidateBinding->BindingId.IsNone() ? CandidateBinding->GetFName() : CandidateBinding->BindingId) == Start.WorldBindingId)
			{
				WorldBinding = CandidateBinding;
				break;
			}
		}
		const FLayoutWorldBindingContinuationFamily* Family = WorldBinding != nullptr
			? WorldBinding->ContinuationFamilies.FindByPredicate([&Start](const FLayoutWorldBindingContinuationFamily& Candidate)
			{
				return Candidate.FamilyId == Start.ContinuationFamilyId;
			}) : nullptr;
		if (Family == nullptr || Family->Candidates.IsEmpty())
		{
			continue;
		}
		const FLayoutWorldBindingContinuationCandidate* RouteCarrierCandidate = &Family->Candidates[0];
		for (const FLayoutWorldBindingContinuationCandidate& Candidate : Family->Candidates)
		{
			if (Candidate.CandidateId.LexicalLess(RouteCarrierCandidate->CandidateId))
			{
				RouteCarrierCandidate = &Candidate;
			}
		}
		FResolvedLayoutConnectorRecord Record;
		FString FailureReason;
		if (!FLayoutConnectorPlanning::TryBuildContinuationRecordForEndpointPair(
				Start.Endpoint, End.Endpoint, WorldBinding, RouteCarrierCandidate->LayoutProfile,
				ResolveLayoutWorldSeed(), Record, FailureReason))
		{
			continue;
		}
		FString EdgeKey;
		if (PlanningStore == nullptr || !PlanningStore->ReserveContinuationEndpointPair(
				Start.Endpoint, End.Endpoint, Record.ContinuationFamilyId,
				Record.ContinuationFamilyCandidateId, EdgeKey, FailureReason))
		{
			continue;
		}
		const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(Record);
		if (SelectedConnectorKeys.Contains(ConnectorKey))
		{
			PlanningStore->ReleaseContinuationEndpointPair(EdgeKey);
			continue;
		}
		ConnectorRecords.Add(MoveTemp(Record));
		ContinuationEdgeKeysByConnectorKey.Add(ConnectorKey, EdgeKey);
		ReservedEndpointKeys.Add(Start.StableEndpointKey);
		ReservedEndpointKeys.Add(End.StableEndpointKey);
		SelectedConnectorKeys.Add(ConnectorKey);
	}
	TMap<uint64, FLayoutPreparedContinuation> PreparedContinuationsByConnectorKey;
	TArray<FResolvedLayoutConnectorRecord> SegmentedConnectorRecords;
	for (const FResolvedLayoutConnectorRecord& RouteRecord : ConnectorRecords)
	{
		SubmitAutomaticContinuationPreparation(RouteRecord);
	}
	if (!PendingAutomaticContinuationPreparations.IsEmpty()) return;
	TArray<uint64> FailedPreparedSegments;
	for (auto& Pair : RetainedPreparedContinuationRoutesById)
	{
		auto* State = ContinuationRouteReservations.Find(Pair.Key);
		if (State == nullptr || !State->bAwaitingSubmission) continue;
		State->bAwaitingSubmission = false;
		for (auto& Segment : Pair.Value.Segments)
		{
			const uint64 SegmentKey = BuildResolvedConnectorRecordKey(Segment.ConnectorRecord);
			if (Segment.PreparedContinuation.IsSet())
			{
				SegmentedConnectorRecords.Add(Segment.ConnectorRecord);
				PreparedContinuationsByConnectorKey.Add(SegmentKey, MoveTemp(Segment.PreparedContinuation.GetValue()));
				Segment.PreparedContinuation.Reset();
			}
			else FailedPreparedSegments.Add(SegmentKey);
			Segment.PreviewGeometry = FLayoutContinuationPreviewGeometry();
		}
		Pair.Value.FrozenTerrainEvidence = FLayoutFrozenTerrainBiomeAdapterInput();
	}
	for (const uint64 Key : FailedPreparedSegments) ReleaseContinuationReservationForConnector(Key, true);
	ConnectorRecords = MoveTemp(SegmentedConnectorRecords);

	for (const TPair<uint64, TSharedPtr<FLayoutPreparedContinuationRoute>>& Pair : PendingExplicitPreparedRoutes)
	{
		if (!Pair.Value.IsValid())
		{
			continue;
		}
		for (const FLayoutPreparedContinuationRouteSegment& Segment : Pair.Value->Segments)
		{
			const uint64 SegmentKey = BuildResolvedConnectorRecordKey(Segment.ConnectorRecord);
			ExplicitPreviewConnectorKeys.Add(SegmentKey);
			if (Segment.PreparedContinuation.IsSet())
			{
				ConnectorRecords.Add(Segment.ConnectorRecord);
				PreparedContinuationsByConnectorKey.Add(SegmentKey, Segment.PreparedContinuation.GetValue());
			}
			else
			{
				ReleaseContinuationReservationForConnector(SegmentKey, true);
			}
		}
	}

	// Failed segments are terminal omissions. Successful siblings retain their own readiness lifetime.
	ConnectorRecords.Sort([](const FResolvedLayoutConnectorRecord& Left, const FResolvedLayoutConnectorRecord& Right)
	{
		return BuildResolvedConnectorRecordKey(Left) < BuildResolvedConnectorRecordKey(Right);
	});
	if (!ConnectorRecords.IsEmpty() && GetDetailedDiagnostics())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[DEBUG-layout-endpoint] Refresh ledger=%d candidatePairs=%d reservedSubmissions=%d explicitPending=%d."),
			RetainedEndpointRecords.Num(),
			AutomaticPairs.Num(),
			ConnectorRecords.Num(),
			PendingExplicitPreparedRoutes.Num());
	}

	const TArray<FIntVector> PlayerBlockWorldPositions = CollectPlanningWindowCenters();

	struct FConnectorContinuationProofJob
	{
		uint64 ConnectorKey = 0;
		FResolvedLayoutConnectorRecord CandidateRecord;
		FLayoutWorkerSolvePacket WorkerSolvePacket;
		FLayoutManifestPrewarmInput ManifestPrewarmInput;
		FLayoutId DescriptorId;
		FLayoutProducedFrozenDescriptorArtifact ProducedDescriptorArtifact;
		FLayoutContinuationPreviewGeometry PreviewGeometry;
		FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;
	};

	TArray<FConnectorContinuationProofJob> ProofJobs;
	TSet<uint64> AcceptedConnectorKeys;
	for (FResolvedLayoutConnectorRecord& ConnectorRecord : ConnectorRecords)
	{
		const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
		if (AcceptedConnectorKeys.Contains(ConnectorKey))
		{
			continue;
		}

		const FLayoutId MissingConnectorDescriptorId = FLayoutId(*FString::Printf(
			TEXT("ConnectorContinuationPrewarm.MissingScout.%llu.%llu"),
			RefreshGeneration,
			ConnectorKey));
		// Local project cutover: connector descriptor publication must come from explicit selected continuation identity.
		// Legacy connector records without selected family/candidate/publication ids stay quarantined until a scout producer owns them.
		if (ConnectorRecord.RootCandidateId.IsNone()
			|| ConnectorRecord.RootSolveId.IsNone()
			|| ConnectorRecord.ContinuationFamilyId.IsNone()
			|| ConnectorRecord.ContinuationFamilyCandidateId.IsNone())
		{
			TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, MissingConnectorDescriptorId);
			ReleaseContinuationReservationForConnector(ConnectorKey, true);
			continue;
		}
		const FLayoutId SelectedContinuationScoutResultId = FLayoutId(*FString::Printf(
			TEXT("ConnectorEdgeScout.%s.%s.%llu"),
			*ConnectorRecord.RootCandidateId.ToString(),
			*ConnectorRecord.ContinuationFamilyCandidateId.ToString(),
			ConnectorKey));
		const FLayoutId ConnectorDescriptorId = FLayoutId(*FString::Printf(
			TEXT("ConnectorContinuationPrewarm.%llu.%s"),
			RefreshGeneration,
			*SelectedContinuationScoutResultId.ToString()));

		// Build descriptor artifact inline from the connector record.
		// When a scout producer exists for connectors, this becomes a descriptor-store
		// lookup instead of an inline build. SurfacePath connectors use StandardRegion
		// mode and require no terrain evidence.
		{
			FConnectorContinuationProofJob ProofJob;
			ProofJob.ConnectorKey = ConnectorKey;

			const FLayoutPreparedContinuation* const RoutePrepared =
				PreparedContinuationsByConnectorKey.Find(ConnectorKey);
			if (RoutePrepared == nullptr)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("Connector continuation segment is missing its route-owned prepared request for key=%llu."),
					ConnectorKey);
				ReleaseContinuationReservationForConnector(ConnectorKey, true);
				continue;
			}
			FLayoutPreparedContinuation PreparedContinuation = *RoutePrepared;
			FLayoutConnectorPlanning::BuildContinuationPreviewGeometry(
				PreparedContinuation,
				ProofJob.PreviewGeometry);
			ConnectorRecord = PreparedContinuation.ConnectorRecord;
			FLayoutRegionSolveRequest ContinuationRequest = PreparedContinuation.SolveRequest;

			FLayoutWorkerSolvePacket WorkerSolvePacket = FLayoutWorkerSolvePacket::CaptureContinuation(
				FString::Printf(TEXT("ConnectorContinuation %llu"), ConnectorKey),
				ConnectorRecord,
				ConnectorKey,
				0);
			WorkerSolvePacket.RequestManifest =
				FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(ContinuationRequest);
			WorkerSolvePacket.bHasRequestManifest = true;
			WorkerSolvePacket.bHasSelectedModePlan = ContinuationRequest.bHasSelectedModePlan;
			WorkerSolvePacket.SelectedModePlan = ContinuationRequest.SelectedModePlan;

			// Select from the assembled request. Finalizing this packet first is invalid:
			// finalization itself requires the frozen mode selection.
			if (!WorkerSolvePacket.bHasSelectedModePlan)
			{
				FLayoutContractModeSelectionInput ModeInput;
				ModeInput.SolveRequest = &ContinuationRequest;
				const FIntVector NominalFootprintMinBlockWorldPos =
					FLayoutStreamingWindow::ComputeConnectorFootprintMinBlockWorldPos(
						ConnectorRecord,
						ConnectorRecord.FrontendSharedCellSizeInBlocks);
				ModeInput.SiteCenterBlockWorldPos = NominalFootprintMinBlockWorldPos + FIntVector(
						ContinuationRequest.FootprintSize.X * ConnectorRecord.FrontendSharedCellSizeInBlocks.X / 2,
						ContinuationRequest.FootprintSize.Y * ConnectorRecord.FrontendSharedCellSizeInBlocks.Y / 2,
						0);
				ModeInput.WorldSeed = ResolveLayoutWorldSeed();
				WorkerSolvePacket.SelectedModePlan = FLayoutContractModeSelection::SelectModePlan(ModeInput);
				WorkerSolvePacket.bHasSelectedModePlan = true;
				WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = true;
				WorkerSolvePacket.RequestManifest.SelectedModePlan = WorkerSolvePacket.SelectedModePlan;
			}

			// Sample terrain along the connector corridor on the game thread
			// so the prewarm has pointer-free surface data for path tracing.
			{
				const FLayoutFrozenTerrainBiomeAdapterInput CorridorTerrain =
					PreparedContinuation.FrozenTerrainEvidence;
				{
					WorkerSolvePacket.RequestManifest.bHasFrozenTerrainBiomeAdapterInput = true;
					WorkerSolvePacket.RequestManifest.FrozenTerrainBiomeAdapterInput = CorridorTerrain;
					// The corridor sampler is the game-thread authority for bridge-path terrain.
					// Preserve every classified corridor column so the worker preflight and
					// terrain adapter receive the same frozen path evidence.
					if (ConnectorRecord.PlacementKind == ELayoutWorldBindingPlacementKind::BridgeContinuation)
					{
						TArray<FLayoutFrozenTerrainPathSample>& TerrainPathSamples =
							WorkerSolvePacket.RequestManifest.FrozenTerrainBiomeAdapterInput.TerrainPathSamples;
						TerrainPathSamples.Reserve(CorridorTerrain.SurfaceSamples.Num());
						for (const FLayoutTerrainSurfaceSample& SurfaceSample : CorridorTerrain.SurfaceSamples)
						{
							FLayoutFrozenTerrainPathSample& TerrainPathSample =
								TerrainPathSamples.AddDefaulted_GetRef();
							TerrainPathSample.BlockXY = SurfaceSample.BlockXY;
							TerrainPathSample.SurfaceZ = SurfaceSample.SurfaceBlockWorldPos.Z;
							TerrainPathSample.bHasClassificationEvidence = SurfaceSample.bIsValid;
						}
						WorkerSolvePacket.RequestManifest.FrozenTerrainBiomeAdapterInput.bHasTerrainPathEvidence =
							!TerrainPathSamples.IsEmpty();
					}

				}
			}

			FLayoutPreSubmitFrozenSnapshot PreSubmitSnapshot;
			PreSubmitSnapshot.SnapshotId = ConnectorDescriptorId;
			PreSubmitSnapshot.PrewarmKind = ELayoutManifestPrewarmKind::Continuation;
			PreSubmitSnapshot.bHasWorkerSolvePacket = true;
			PreSubmitSnapshot.WorkerSolvePacket = MoveTemp(WorkerSolvePacket);

			FLayoutRegionPrewarmDescriptorInput PrewarmInput;
			PrewarmInput.SourceKind = ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult;
			PrewarmInput.ArtifactId = FLayoutId(*FString::Printf(
				TEXT("ProducedDescriptor.%s"), *ConnectorDescriptorId.ToString()));
			PrewarmInput.DescriptorId = ConnectorDescriptorId;
			PrewarmInput.SelectedResultId = SelectedContinuationScoutResultId;
			PrewarmInput.RegionGroupId = ConnectorKey;
			PrewarmInput.Generation = RefreshGeneration;
			PrewarmInput.AttemptIndex = 0;
			PrewarmInput.ExpectedSolveSeed = ConnectorRecord.SolveSeed;
			PrewarmInput.FinalizedPrimaryBlockWorldPos = WorkerSolvePacket.PrimaryBlockWorldPos;
			PrewarmInput.Snapshot = MoveTemp(PreSubmitSnapshot);

			FLayoutProducedFrozenDescriptorArtifact ProducedDescriptorArtifact;
			FString ProducerFailureReason;

			// Validate continuation-specific identity.  These checks mirror the
			// continuation path in TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult.
			const FLayoutPreSubmitFrozenSnapshot& ContSnapshot = PrewarmInput.Snapshot;
			if (ContSnapshot.PrewarmKind != ELayoutManifestPrewarmKind::Continuation)
			{
				ProducerFailureReason = TEXT("Connector continuation requires a continuation prewarm snapshot.");
			}
			else if (!ContSnapshot.bHasWorkerSolvePacket)
			{
				ProducerFailureReason = TEXT("Connector continuation requires a frozen worker packet.");
			}
			else if (ContSnapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::Continuation)
			{
				ProducerFailureReason = TEXT("Connector continuation requires a continuation worker packet.");
			}
			else if (!ContSnapshot.WorkerSolvePacket.bHasSelectedModePlan)
			{
				ProducerFailureReason = TEXT("Connector continuation requires a frozen selected mode plan.");
			}
			else if (!ContSnapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable)
			{
				ProducerFailureReason = TEXT("Connector continuation requires traversal-reachable profile metadata.");
			}
			else if (ContSnapshot.WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel == INDEX_NONE)
			{
				ProducerFailureReason = TEXT("Connector continuation requires resolved continuation entry metadata.");
			}
			else if (ContSnapshot.WorkerSolvePacket.SolveSeed != PrewarmInput.ExpectedSolveSeed)
			{
				ProducerFailureReason = TEXT("Connector continuation rejected stale solve seed for finalized edge scout result.");
			}
			else if (ContSnapshot.WorkerSolvePacket.PrimaryBlockWorldPos != PrewarmInput.FinalizedPrimaryBlockWorldPos)
			{
				ProducerFailureReason = TEXT("Connector continuation rejected mismatched finalized primary endpoint position.");
			}

			if (!ProducerFailureReason.IsEmpty())
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Connector continuation descriptor validation failed for key=%llu: %s"),
					ConnectorKey, *ProducerFailureReason);
				TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, ConnectorDescriptorId);
				ReleaseContinuationReservationForConnector(ConnectorKey, true);
				continue;
			}

			// Build the descriptor artifact now so continuation prewarm receives
			// its one finalized adapter contract through the shared producer.
			FLayoutFrozenSubmissionDescriptorSeed Seed;
			if (!LayoutFrozenSubmissionDescriptorProducer::BuildPreflightDescriptorSeed(
					ConnectorDescriptorId,
					ConnectorKey,
					RefreshGeneration,
					0,
					ELayoutFrozenSubmissionRegionKind::Continuation,
					ContSnapshot,
					Seed,
					ProducerFailureReason))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Connector continuation descriptor seed failed for key=%llu: %s"),
					ConnectorKey, *ProducerFailureReason);
				TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, ConnectorDescriptorId);
				ReleaseContinuationReservationForConnector(ConnectorKey, true);
				continue;
			}

			const FLayoutId ArtifactId = PrewarmInput.ArtifactId;
			if (!LayoutFrozenSubmissionDescriptorProducer::BuildProducedDescriptorArtifactFromSeed(
					ArtifactId,
					Seed,
					ProducedDescriptorArtifact,
					ProducerFailureReason))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Connector continuation descriptor artifact failed for key=%llu: %s"),
					ConnectorKey, *ProducerFailureReason);
				TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, ConnectorDescriptorId);
				ReleaseContinuationReservationForConnector(ConnectorKey, true);
				continue;
			}

			FLayoutManifestPrewarmInput ManifestPrewarmInput;
			if (!LayoutRegionPrewarmDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmission(
					ELayoutRegionPrewarmDescriptorSourceKind::ContinuationEdgeScoutResult,
					&ProducedDescriptorArtifact,
					ManifestPrewarmInput,
					ProducerFailureReason))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("Connector continuation manifest prewarm input failed for key=%llu: %s"),
					ConnectorKey, *ProducerFailureReason);
				TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, ConnectorDescriptorId);
				ReleaseContinuationReservationForConnector(ConnectorKey, true);
				continue;
			}

			ProofJob.CandidateRecord = ConnectorRecord;
			ProofJob.WorkerSolvePacket = ProducedDescriptorArtifact.DescriptorSeed.Snapshot.WorkerSolvePacket;
			ProofJob.ManifestPrewarmInput = MoveTemp(ManifestPrewarmInput);
			ProofJob.DescriptorId = ConnectorDescriptorId;
			ProofJob.ProducedDescriptorArtifact = MoveTemp(ProducedDescriptorArtifact);
			ProofJob.SharedCellSizeInBlocks = ConnectorRecord.FrontendSharedCellSizeInBlocks;
			ProofJobs.Add(MoveTemp(ProofJob));
			AcceptedConnectorKeys.Add(ConnectorKey);
		}
	}

	ProofJobs.Sort([&PlayerBlockWorldPositions](
		const FConnectorContinuationProofJob& Left,
		const FConnectorContinuationProofJob& Right)
	{
		auto ClosestDistanceSquared = [&PlayerBlockWorldPositions](const FResolvedLayoutConnectorRecord& Record)
		{
			int64 ClosestDistance = TNumericLimits<int64>::Max();
			for (const FIntVector& PlayerPosition : PlayerBlockWorldPositions)
			{
				const FIntVector Delta = Record.PathOriginBlockWorldPos - PlayerPosition;
				ClosestDistance = FMath::Min(ClosestDistance,
					static_cast<int64>(Delta.X) * Delta.X
					+ static_cast<int64>(Delta.Y) * Delta.Y
					+ static_cast<int64>(Delta.Z) * Delta.Z);
			}
			return ClosestDistance;
		};
		const int64 LeftDistance = ClosestDistanceSquared(Left.CandidateRecord);
		const int64 RightDistance = ClosestDistanceSquared(Right.CandidateRecord);
		return LeftDistance != RightDistance
			? LeftDistance < RightDistance
			: Left.ConnectorKey < Right.ConnectorKey;
	});

	if (ProofJobs.IsEmpty())
	{
		// A refresh may find no new work while an accepted explicit preview waits for
		// editor Apply. Keep its published record and frozen contract alive so the next
		// refresh cannot reinterpret its reserved edge as abandoned.
		for (const FResolvedLayoutConnectorRecord& ConnectorRecord : ConnectorRecords)
		{
			const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
			if (!ResolvedConnectorRecords.Contains(ConnectorKey))
			{
				ReleaseContinuationReservationForConnector(ConnectorKey, true);
			}
		}
		TArray<uint64> CompletedRequestKeys;
		PendingExplicitPreparedRoutes.GenerateKeyArray(CompletedRequestKeys);
		for (const uint64 ConnectorKey : CompletedRequestKeys)
		{
			FLayoutExplicitContinuationSolveResult Result;
			if (const TSharedPtr<FLayoutPreparedContinuationRoute>* const PreparedRoute =
					PendingExplicitPreparedRoutes.Find(ConnectorKey);
				PreparedRoute != nullptr && PreparedRoute->IsValid())
			{
				Result.Route = (*PreparedRoute)->Route;
				TArray<FString> SegmentFailureReasons;
				for (const FLayoutPreparedContinuationRouteSegment& PreparedSegment : (*PreparedRoute)->Segments)
				{
					FLayoutExplicitContinuationSegmentSolveResult& SegmentResult = Result.Segments.AddDefaulted_GetRef();
					SegmentResult.Descriptor = PreparedSegment.Descriptor;
					SegmentResult.ConnectorRecord = PreparedSegment.ConnectorRecord;
					if (PreparedSegment.PreparedContinuation.IsSet())
					{
						PopulateFailedContinuationPreviewSolveResult(
							SegmentResult.ConnectorRecord,
							PreparedSegment.PreparedContinuation.GetValue());
					}
					SegmentResult.PreviewGeometry = PreparedSegment.PreviewGeometry;
					SegmentResult.FailureReason = PreparedSegment.FailureReason;
					if (SegmentResult.PreviewGeometry.SharedCellSizeInBlocks != FIntVector::ZeroValue)
					{
						ExplicitPreviewConnectorKeys.Add(BuildResolvedConnectorRecordKey(PreparedSegment.ConnectorRecord));
					}
					SegmentFailureReasons.Add(FString::Printf(TEXT("Segment %d: %s"),
						PreparedSegment.Descriptor.SegmentIndex,
						*PreparedSegment.FailureReason));
				}
				Result.FailureReason = FString::Join(SegmentFailureReasons, TEXT(" | "));
			}
			if (Result.FailureReason.IsEmpty())
			{
				Result.FailureReason = TEXT("Explicit continuation did not prepare a publishable solve.");
			}
			ReleaseContinuationReservationForConnector(ConnectorKey);
			CompleteExplicitContinuationRequest(ConnectorKey, Result);
		}
		if (!ConnectorRecords.IsEmpty())
		{
			const FResolvedLayoutConnectorRecord& FirstRecord = ConnectorRecords[0];
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Connector refresh produced no submission: candidates=%d rootCandidate=%s rootSolve=%s family=%s candidate=%s."),
				ConnectorRecords.Num(),
				*FirstRecord.RootCandidateId.ToString(),
				*FirstRecord.RootSolveId.ToString(),
				*FirstRecord.ContinuationFamilyId.ToString(),
				*FirstRecord.ContinuationFamilyCandidateId.ToString());
		}
		return;
	}

	struct FConnectorContinuationPublishedRecord
	{
		uint64 ConnectorKey = 0;
		bool bIsPartial = false;
		FResolvedLayoutConnectorRecord Record;
		FLayoutRegionSolveScheduleResult ScheduleResult;
		FLayoutFrozenTerrainContract FrozenTerrainContract;
		FLayoutContinuationPreviewGeometry PreviewGeometry;
	};

	struct FConnectorContinuationFailedRecord
	{
		/** Preserves failed worker output so editor preview reuses root partial diagnostics. */
		uint64 ConnectorKey = 0;
		FResolvedLayoutConnectorRecord Record;
		FLayoutRegionSolveScheduleResult ScheduleResult;
		FLayoutFrozenTerrainContract FrozenTerrainContract;
		FLayoutContinuationPreviewGeometry PreviewGeometry;
	};

	struct FConnectorContinuationProofBatch
	{
		uint64 Generation = 0;
		int32 ExpectedCompletions = 0;
		int32 ObservedCompletions = 0;
		TArray<uint64> ExplicitRequestKeys;
		TMap<uint64, FString> FailureReasons;
		TArray<uint64> CandidateConnectorKeys;
		TSet<uint64> CompletedConnectorKeys;
		TArray<FConnectorContinuationPublishedRecord> ProvenRecords;
		TArray<FConnectorContinuationFailedRecord> FailedRecords;
	};

	TSharedRef<FConnectorContinuationProofBatch, ESPMode::ThreadSafe> SharedBatch =
		MakeShared<FConnectorContinuationProofBatch, ESPMode::ThreadSafe>();
	SharedBatch->Generation = RefreshGeneration;
	SharedBatch->ExpectedCompletions = ProofJobs.Num();
	PendingExplicitPreparedRoutes.GenerateKeyArray(SharedBatch->ExplicitRequestKeys);
	for (const FConnectorContinuationProofJob& ProofJob : ProofJobs)
	{
		SharedBatch->CandidateConnectorKeys.Add(ProofJob.ConnectorKey);
	}

	TWeakObjectPtr<UChunkWorldLayoutRuntimeComponent> WeakThis(this);
	for (FConnectorContinuationProofJob& ProofJob : ProofJobs)
	{
		struct FConnectorBackgroundSolveSharedResult
		{
			FLayoutRegionSolveScheduleResult ScheduleResult;
			FLayoutFrozenTerrainContract FrozenTerrainContract;
			FLayoutSolvedArtifact SolvedArtifact;
		};
		TSharedRef<FConnectorBackgroundSolveSharedResult, ESPMode::ThreadSafe> SharedResult =
			MakeShared<FConnectorBackgroundSolveSharedResult, ESPMode::ThreadSafe>();

		const uint64 ConnectorKey = ProofJob.ConnectorKey;
		const FLayoutId CapturedDescriptorId = ProofJob.DescriptorId;
		const FLayoutId CapturedSolvedArtifactId = BuildRuntimeSolvedArtifactId(CapturedDescriptorId);
		const FLayoutProducedFrozenDescriptorArtifact CapturedProducedDescriptorArtifact = ProofJob.ProducedDescriptorArtifact;
		TSharedRef<FLayoutContinuationPreviewGeometry, ESPMode::ThreadSafe> SharedPreviewGeometry =
			MakeShared<FLayoutContinuationPreviewGeometry, ESPMode::ThreadSafe>(MoveTemp(ProofJob.PreviewGeometry));
		const FResolvedLayoutConnectorRecord CapturedRecord = MoveTemp(ProofJob.CandidateRecord);
		TSharedRef<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> SharedDescriptorWorkerSolvePacket =
			MakeShared<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe>(MoveTemp(ProofJob.WorkerSolvePacket));
		FLayoutManifestPrewarmInput ManifestPrewarmInput = MoveTemp(ProofJob.ManifestPrewarmInput);
		const FIntVector SharedCellSizeInBlocks = ProofJob.SharedCellSizeInBlocks;

		FLayoutBackgroundSolveSubmission Submission;
		Submission.DebugName = FString::Printf(TEXT("ConnectorContinuation %llu"), ConnectorKey);
		Submission.LayoutGroupId = ConnectorKey;
		Submission.Tier = ELayoutBackgroundSolveJobTier::Continuation;
		Submission.Priority = TNumericLimits<int32>::Max() - PendingConnectorRefreshSolveHandles.Num();
		Submission.PublishOnGameThread = [WeakThis, SharedBatch, SharedResult, CapturedRecord, SharedPreviewGeometry, SharedDescriptorWorkerSolvePacket, ConnectorKey, CapturedDescriptorId, CapturedSolvedArtifactId, RefreshGeneration, SharedCellSizeInBlocks](const FLayoutBackgroundSolveCompletion& Completion)
		{
			UChunkWorldLayoutRuntimeComponent* const Component = WeakThis.Get();
			if (Component == nullptr)
			{
				return;
			}
			if (Component->ConnectorRefreshGeneration != RefreshGeneration
				|| (!Component->ExplicitPreviewConnectorKeys.Contains(ConnectorKey)
					&& !Component->IsContinuationRouteEligible(CapturedRecord.ContinuationRouteId)))
			{
				Component->TombstoneFrozenSubmissionDescriptorPayload(CapturedDescriptorId);
				Component->ReleaseContinuationReservationForConnector(ConnectorKey);
				return;
			}

			if (SharedBatch->CompletedConnectorKeys.Contains(ConnectorKey)) return;
			SharedBatch->CompletedConnectorKeys.Add(ConnectorKey);
			FString DescriptorPublishFailureReason;
			const FLayoutId* const ExpectedDescriptorId = Component->PendingConnectorFrozenSubmissionDescriptorIdsByKey.Find(ConnectorKey);
			const uint64* const ExpectedGeneration = Component->PendingConnectorFrozenSubmissionGenerationsByKey.Find(ConnectorKey);
			const int32* const ExpectedAttemptIndex = Component->PendingConnectorFrozenSubmissionAttemptIndicesByKey.Find(ConnectorKey);
			const int32* const ExpectedAuditHash = Component->PendingConnectorFrozenSubmissionAuditHashesByKey.Find(ConnectorKey);
			const bool bDescriptorValidForPublish = ExpectedDescriptorId != nullptr
				&& ExpectedGeneration != nullptr
				&& ExpectedAttemptIndex != nullptr
				&& ExpectedAuditHash != nullptr
				&& *ExpectedDescriptorId == CapturedDescriptorId
				&& Component->FrozenSubmissionStore.IsValid()
				&& LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
					*Component->FrozenSubmissionStore,
					CapturedDescriptorId,
					ConnectorKey,
					*ExpectedGeneration,
					*ExpectedAttemptIndex,
					ELayoutFrozenSubmissionRegionKind::Continuation,
					static_cast<uint32>(*ExpectedAuditHash),
					TEXT("Connector-continuation publish"),
					DescriptorPublishFailureReason);
			if (!bDescriptorValidForPublish)
			{
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("Skipping connector completion cleanup/publication for key=%llu because descriptor publish validation failed: %s"),
					ConnectorKey,
					*DescriptorPublishFailureReason);
				Component->TombstoneFrozenSubmissionDescriptorPayload(CapturedDescriptorId);
			}
			else
			{
				Component->TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, CapturedDescriptorId);
			}

			++SharedBatch->ObservedCompletions;
			const auto RecordPublicationFailure = [&](const FString& FailureReason)
			{
				SharedBatch->FailureReasons.Add(ConnectorKey, FailureReason);
				FConnectorContinuationFailedRecord& FailedRecord = SharedBatch->FailedRecords.AddDefaulted_GetRef();
				FailedRecord.ConnectorKey = ConnectorKey;
				FailedRecord.Record = CapturedRecord;
				FResolvedLayoutConnectorRuntimeState RuntimeState = FailedRecord.Record.GetResolvedConnectorRuntimeState();
				RuntimeState.bLayoutSolved = false;
				FailedRecord.Record.SetResolvedConnectorRuntimeState(RuntimeState);
				FailedRecord.Record.SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
				FailedRecord.Record.SolveResult.bSucceeded = false;
				FailedRecord.Record.SolveResult.FailureReason = FailureReason;
				FailedRecord.ScheduleResult = SharedResult->ScheduleResult;
				FailedRecord.FrozenTerrainContract = SharedResult->FrozenTerrainContract;
				FailedRecord.PreviewGeometry = *SharedPreviewGeometry;
				PopulateContinuationPreviewSolveDiagnostics(
					FailedRecord.PreviewGeometry,
					FailedRecord.Record.SolveResult,
					FailureReason);
			};
			const bool bHasRetainedPartial =
				!SharedResult->ScheduleResult.MergedSolveResult.bSucceeded
				&& HasRetainedPartialPlacements(SharedResult->ScheduleResult.MergedSolveResult);
			if (bHasRetainedPartial)
			{
				SharedBatch->FailureReasons.Add(
					ConnectorKey,
					!Completion.FailureReason.IsEmpty()
						? Completion.FailureReason
						: SharedResult->ScheduleResult.MergedSolveResult.FailureReason);
			}
			if ((Completion.bWorkSucceeded || bHasRetainedPartial) && bDescriptorValidForPublish)
			{
				FLayoutRegionSolveRequest PublishRequest;
				FString PublishRequestFailureReason;
				if (!LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(
						*SharedDescriptorWorkerSolvePacket,
						PublishRequest,
						PublishRequestFailureReason))
				{
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("Skipping accepted connector publication for key=%llu because packet manifest could not be finalized: %s"),
						ConnectorKey,
						*PublishRequestFailureReason);
					SharedBatch->FailureReasons.Add(ConnectorKey, PublishRequestFailureReason);
					FConnectorContinuationFailedRecord& FailedRecord = SharedBatch->FailedRecords.AddDefaulted_GetRef();
					FailedRecord.ConnectorKey = ConnectorKey;
					FailedRecord.Record = CapturedRecord;
					FailedRecord.Record.SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
					FailedRecord.Record.SolveResult.bSucceeded = false;
					FailedRecord.Record.SolveResult.FailureReason = PublishRequestFailureReason;
					FailedRecord.ScheduleResult = SharedResult->ScheduleResult;
					FailedRecord.FrozenTerrainContract = SharedResult->FrozenTerrainContract;
					FailedRecord.PreviewGeometry = *SharedPreviewGeometry;
					PopulateContinuationPreviewSolveDiagnostics(
						FailedRecord.PreviewGeometry,
						FailedRecord.Record.SolveResult,
						PublishRequestFailureReason);
				}
				else
				{

				const FLayoutSolveResult& PublishSolveResult = SharedResult->ScheduleResult.MergedSolveResult;
				FString SolvedArtifactFailureReason;
				if (!ValidateRuntimeSolvedArtifactForPublish(
						SharedResult->SolvedArtifact,
						CapturedSolvedArtifactId,
						SolvedArtifactFailureReason))
				{
					const FString SolverFailureReason =
						!SharedResult->ScheduleResult.MergedSolveResult.FailureReason.IsEmpty()
							? SharedResult->ScheduleResult.MergedSolveResult.FailureReason
							: (!SharedResult->ScheduleResult.FailureReason.IsEmpty()
								? SharedResult->ScheduleResult.FailureReason
								: Completion.FailureReason);
					const FString FailureReason = SolverFailureReason.IsEmpty()
						? SolvedArtifactFailureReason
						: FString::Printf(
							TEXT("%s Artifact publication: %s"),
							*SolverFailureReason,
							*SolvedArtifactFailureReason);
					UE_LOG(
						LogTemp,
						Warning,
						TEXT("Skipping accepted connector publication for key=%llu because solved artifact validation failed: %s"),
						ConnectorKey,
						*FailureReason);
					// Artifact rejection cannot enter apply caches, but it must retain the
					// prewarm topology and solver evidence for editor diagnosis.
					SharedBatch->FailureReasons.Add(ConnectorKey, FailureReason);
					FConnectorContinuationFailedRecord& FailedRecord = SharedBatch->FailedRecords.AddDefaulted_GetRef();
					FailedRecord.ConnectorKey = ConnectorKey;
					FailedRecord.Record = CapturedRecord;
					FResolvedLayoutConnectorRuntimeState RuntimeState = FailedRecord.Record.GetResolvedConnectorRuntimeState();
					RuntimeState.bLayoutSolved = false;
					FailedRecord.Record.SetResolvedConnectorRuntimeState(RuntimeState);
					FailedRecord.Record.SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
					FailedRecord.Record.SolveResult.bSucceeded = false;
					FailedRecord.Record.SolveResult.FailureReason = FailureReason;
					FailedRecord.ScheduleResult = SharedResult->ScheduleResult;
					FailedRecord.FrozenTerrainContract = SharedResult->FrozenTerrainContract;
					FailedRecord.PreviewGeometry = *SharedPreviewGeometry;
					PopulateContinuationPreviewSolveDiagnostics(
						FailedRecord.PreviewGeometry,
						FailedRecord.Record.SolveResult,
						FailureReason);
				}
				else
				{
				FResolvedLayoutConnectorRecord SolvedRecord = CapturedRecord;
				if (PublishRequest.bHasSelectedModePlan)
				{
					FLayoutResolvedConnectorFrontendSelection FrontendSelection =
						SolvedRecord.GetResolvedConnectorFrontendSelection();
					FrontendSelection.PlacementKind = PublishRequest.SelectedModePlan.PlacementKind;
					FrontendSelection.ResolvedContinuationSelection =
						PublishRequest.SelectedModePlan.ContinuationSelection;
					FrontendSelection.WorldBindingPlacementPolicy =
						PublishRequest.SelectedModePlan.PlacementPolicy;
					SolvedRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);
					SolvedRecord.PlacementKind = PublishRequest.SelectedModePlan.PlacementKind;
					SolvedRecord.ResolvedContinuationSelection =
						PublishRequest.SelectedModePlan.ContinuationSelection;
					SolvedRecord.WorldBindingPlacementPolicy =
						PublishRequest.SelectedModePlan.PlacementPolicy;
				}
				FLayoutSolveResult SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
				FResolvedLayoutConnectorRuntimeState RuntimeState = SolvedRecord.GetResolvedConnectorRuntimeState();
				RuntimeState.bLayoutSolved = SolveResult.bSucceeded;
				SolvedRecord.SetResolvedConnectorRuntimeState(RuntimeState);
				SolvedRecord.SolveResult = MoveTemp(SolveResult);
				const int32 ContinuationPublicationLevel = SolvedRecord.ResolvedContinuationSelection.ResolvedEntryLevel != INDEX_NONE
					? SolvedRecord.ResolvedContinuationSelection.ResolvedEntryLevel
					: SolvedRecord.SolveResult.ResolvedTerrainAlignmentLevel;
				if (ContinuationPublicationLevel != INDEX_NONE)
				{
					// Preserve solver-owned cell elevations. Stepped VerticalAccess cells
					// and generated landings intentionally occupy distinct local levels.
					SolvedRecord.SolveResult.ResolvedTerrainAlignmentLevel = ContinuationPublicationLevel;
				}
				SolvedRecord.StartEndpointBoundaryStripBlockXY.Reset();
				SolvedRecord.EndEndpointBoundaryStripBlockXY.Reset();

				TSet<FIntPoint> StartBoundaryColumns;
				AppendRuntimeConnectorEndpointBoundaryStripColumns(
					SolvedRecord.SolveResult,
					SolvedRecord.PathOriginBlockWorldPos,
					SharedCellSizeInBlocks,
					SolvedRecord.StartEndpointBlockWorldPos,
					SolvedRecord.StartEndpointFacingDirection,
					StartBoundaryColumns);
				SolvedRecord.StartEndpointBoundaryStripBlockXY = StartBoundaryColumns.Array();
				SolvedRecord.StartEndpointBoundaryStripBlockXY.Sort([](const FIntPoint& Left, const FIntPoint& Right)
				{
					return Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X;
				});

				TSet<FIntPoint> EndBoundaryColumns;
				AppendRuntimeConnectorEndpointBoundaryStripColumns(
					SolvedRecord.SolveResult,
					SolvedRecord.PathOriginBlockWorldPos,
					SharedCellSizeInBlocks,
					SolvedRecord.EndEndpointBlockWorldPos,
					SolvedRecord.EndEndpointFacingDirection,
					EndBoundaryColumns);
				SolvedRecord.EndEndpointBoundaryStripBlockXY = EndBoundaryColumns.Array();
				SolvedRecord.EndEndpointBoundaryStripBlockXY.Sort([](const FIntPoint& Left, const FIntPoint& Right)
				{
					return Left.Y != Right.Y ? Left.Y < Right.Y : Left.X < Right.X;
				});

				FString FrozenTerrainFailureReason;
				if (SharedResult->FrozenTerrainContract.ContractId.IsNone())
				{
					// Contract was not produced by prewarm adapter; skip this connector.
					const FString FailureReason = TEXT("Accepted connector publication has no precomputed frozen terrain contract.");
					UE_LOG(LogTemp, Warning, TEXT("Skipping accepted connector publication for key=%llu: %s"),
						ConnectorKey, *FailureReason);
					RecordPublicationFailure(FailureReason);
				}
				else
				{
					FString ActiveCellArtifactFailureReason;
					if (!LayoutSolvedArtifact::TryAttachActiveCellProvenance(
							SharedResult->FrozenTerrainContract.ActiveCells,
							SharedResult->SolvedArtifact,
							ActiveCellArtifactFailureReason))
					{
						UE_LOG(
							LogTemp,
							Warning,
							TEXT("Skipping accepted connector publication for key=%llu because solved artifact active-cell provenance failed: %s"),
							ConnectorKey,
							*ActiveCellArtifactFailureReason);
						RecordPublicationFailure(ActiveCellArtifactFailureReason);
					}
					else
					{
						FString WriteInputFailureReason;
						if (!LayoutSolvedArtifact::ValidateActiveCellProvenanceForWriteInputs(
								SharedResult->SolvedArtifact,
								WriteInputFailureReason))
						{
							UE_LOG(
								LogTemp,
								Warning,
								TEXT("Skipping accepted connector publication for key=%llu because solved artifact write-input validation failed: %s"),
								ConnectorKey,
								*WriteInputFailureReason);
							RecordPublicationFailure(WriteInputFailureReason);
						}
						else
						{
							FConnectorContinuationPublishedRecord& PublishedRecord = SharedBatch->ProvenRecords.AddDefaulted_GetRef();
							PublishedRecord.ConnectorKey = ConnectorKey;
							PublishedRecord.bIsPartial = bHasRetainedPartial;
							PublishedRecord.PreviewGeometry = *SharedPreviewGeometry;
							if (PublishedRecord.PreviewGeometry.SharedCellSizeInBlocks == FIntVector::ZeroValue)
							{
								PublishedRecord.PreviewGeometry.SharedCellSizeInBlocks =
									SharedCellSizeInBlocks != FIntVector::ZeroValue
										? SharedCellSizeInBlocks
										: SolvedRecord.SolveResult.SharedCellSizeInBlocks;
							}
							// Adapter-owned planned cells preserve bridge and terrain-seam diagnostics.
							PopulateContinuationPreviewTerrainCells(
								PublishedRecord.PreviewGeometry,
								PublishRequest.PlannedCells,
								SharedResult->FrozenTerrainContract,
								&SharedResult->FrozenTerrainContract.ActiveCells);
							PopulateContinuationPreviewSolveDiagnostics(
								PublishedRecord.PreviewGeometry,
								SharedResult->ScheduleResult.MergedSolveResult);
							for (int32 CenterlineIndex = 0;
								CenterlineIndex < PublishedRecord.PreviewGeometry.CenterlineCells.Num();
								++CenterlineIndex)
							{
								const FIntVector& CenterlineCell = PublishedRecord.PreviewGeometry.CenterlineCells[CenterlineIndex];
								if (const FLayoutFrozenTerrainStageCellRecord* const Stage =
									SharedResult->FrozenTerrainContract.StageMap.FindByPredicate(
										[&CenterlineCell](const FLayoutFrozenTerrainStageCellRecord& Candidate)
										{
											return Candidate.FootprintCellXY == FIntPoint(CenterlineCell.X, CenterlineCell.Y);
										}))
								{
									if (PublishedRecord.PreviewGeometry.CenterlineBaseBlockWorldZs.IsValidIndex(CenterlineIndex))
									{
										PublishedRecord.PreviewGeometry.CenterlineBaseBlockWorldZs[CenterlineIndex] =
											Stage->ResolvedStageBaseBlockWorldZ;
									}
								}
							}
							SolvedRecord.SolvedArtifactId = SharedResult->SolvedArtifact.ArtifactId;
							SolvedRecord.SolvedArtifactActiveCellCount = SharedResult->SolvedArtifact.ActiveCells.Num();
							PublishedRecord.Record = MoveTemp(SolvedRecord);
							PublishedRecord.ScheduleResult = SharedResult->ScheduleResult;
							PublishedRecord.FrozenTerrainContract = SharedResult->FrozenTerrainContract;
						}
					}
				}
				}
				}
			}
			else if (!Completion.bWorkSucceeded)
			{
				SharedBatch->FailureReasons.Add(ConnectorKey, Completion.FailureReason);
				// Local editor-preview parity: retain failed solve state exactly as the
				// root path does, without publishing it to runtime realization caches.
				if (!SharedResult->ScheduleResult.MergedSolveResult.PlannedCells.IsEmpty())
				{
					FConnectorContinuationFailedRecord& FailedRecord = SharedBatch->FailedRecords.AddDefaulted_GetRef();
					FailedRecord.ConnectorKey = ConnectorKey;
					FailedRecord.Record = CapturedRecord;
					FResolvedLayoutConnectorRuntimeState RuntimeState = FailedRecord.Record.GetResolvedConnectorRuntimeState();
					RuntimeState.bLayoutSolved = false;
					FailedRecord.Record.SetResolvedConnectorRuntimeState(RuntimeState);
					FailedRecord.Record.SolveResult = SharedResult->ScheduleResult.MergedSolveResult;
					FailedRecord.ScheduleResult = SharedResult->ScheduleResult;
					FailedRecord.FrozenTerrainContract = SharedResult->FrozenTerrainContract;
					FailedRecord.PreviewGeometry = *SharedPreviewGeometry;
					PopulateContinuationPreviewSolveDiagnostics(
						FailedRecord.PreviewGeometry,
						FailedRecord.Record.SolveResult,
						Completion.FailureReason);
				}
				UE_LOG(
					LogTemp,
					Warning,
					TEXT("Connector continuation solve failed for key=%llu: %s"),
					ConnectorKey,
					*Completion.FailureReason);
			}

			Component->bRealizationDirty = true;
			Component->bLoadedInfluenceDirty = true;
			// Publish each valid segment now; siblings and unrelated proofs do not gate readiness.
			const FConnectorContinuationPublishedRecord* CompletedRecord = SharedBatch->ProvenRecords.FindByPredicate(
				[ConnectorKey](const FConnectorContinuationPublishedRecord& Record) { return Record.ConnectorKey == ConnectorKey; });
			if (CompletedRecord != nullptr && !CompletedRecord->bIsPartial)
			{
				Component->ResolvedConnectorRecords.Add(ConnectorKey, CompletedRecord->Record);
				Component->ResolvedConnectorFrozenTerrainContracts.Add(ConnectorKey, CompletedRecord->FrozenTerrainContract);
			}
			else
			{
				Component->ReleaseContinuationReservationForConnector(ConnectorKey, !Completion.bCanceled);
			}

			if (CompletedRecord != nullptr && CompletedRecord->bIsPartial && Component->ExplicitPreviewConnectorKeys.Contains(ConnectorKey))
			{
				Component->ResolvedConnectorRecords.Add(ConnectorKey, CompletedRecord->Record);
				Component->ResolvedConnectorFrozenTerrainContracts.Add(ConnectorKey, CompletedRecord->FrozenTerrainContract);
			}
			if (SharedBatch->ObservedCompletions == SharedBatch->ExpectedCompletions)
			{
				Component->PendingConnectorRefreshSolveHandles.Reset();
			}
			for (const uint64 ExplicitKey : SharedBatch->ExplicitRequestKeys)
			{
				if (!Component->PendingExplicitPreparedRoutes.Contains(ExplicitKey)) continue;
				FLayoutExplicitContinuationSolveResult ExplicitResult;
				if (const FLayoutContinuationRouteRecord* const Route =
						Component->ExplicitRoutesByConnectorKey.Find(ExplicitKey))
				{
					ExplicitResult.Route = *Route;
				}
				const TArray<uint64>* const SegmentKeys =
					Component->ExplicitRouteSegmentKeysByConnectorKey.Find(ExplicitKey);
				// Other routes may still be running or canceled; only this request's segments gate its result.
				if (SegmentKeys != nullptr && SegmentKeys->ContainsByPredicate([&SharedBatch](const uint64 Key)
					{ return SharedBatch->CandidateConnectorKeys.Contains(Key) && !SharedBatch->CompletedConnectorKeys.Contains(Key); })) continue;
				const TSharedPtr<FLayoutPreparedContinuationRoute>* const PreparedRoute =
					Component->PendingExplicitPreparedRoutes.Find(ExplicitKey);
				bool bAllSegmentsSucceeded = SegmentKeys != nullptr && !SegmentKeys->IsEmpty();
				bool bAnySegmentSucceeded = false;
				bool bAnyPartial = false;
				for (int32 SegmentOrdinal = 0;
					SegmentKeys != nullptr && SegmentOrdinal < SegmentKeys->Num();
					++SegmentOrdinal)
				{
					const uint64 SegmentKey = (*SegmentKeys)[SegmentOrdinal];
					FLayoutExplicitContinuationSegmentSolveResult& SegmentResult =
						ExplicitResult.Segments.AddDefaulted_GetRef();
					if (ExplicitResult.Route.Segments.IsValidIndex(SegmentOrdinal))
					{
						SegmentResult.Descriptor = ExplicitResult.Route.Segments[SegmentOrdinal];
					}
					if (PreparedRoute != nullptr && PreparedRoute->IsValid()
						&& (*PreparedRoute)->Segments.IsValidIndex(SegmentOrdinal))
					{
						const FLayoutPreparedContinuationRouteSegment& PreparedSegment =
							(*PreparedRoute)->Segments[SegmentOrdinal];
						SegmentResult.ConnectorRecord = PreparedSegment.ConnectorRecord;
						if (PreparedSegment.PreparedContinuation.IsSet())
						{
							PopulateFailedContinuationPreviewSolveResult(
								SegmentResult.ConnectorRecord,
								PreparedSegment.PreparedContinuation.GetValue());
						}
						SegmentResult.PreviewGeometry = PreparedSegment.PreviewGeometry;
						SegmentResult.FailureReason = PreparedSegment.FailureReason;
					}
					const FConnectorContinuationPublishedRecord* const PublishedRecord =
						SharedBatch->ProvenRecords.FindByPredicate([SegmentKey](const FConnectorContinuationPublishedRecord& Candidate)
						{
							return Candidate.ConnectorKey == SegmentKey;
						});
					if (PublishedRecord == nullptr)
					{
						bAllSegmentsSucceeded = false;
						const FString WorkerFailureReason = SharedBatch->FailureReasons.FindRef(SegmentKey);
						if (!WorkerFailureReason.IsEmpty())
						{
							SegmentResult.FailureReason = WorkerFailureReason;
						}
						if (const FConnectorContinuationFailedRecord* const FailedRecord =
							SharedBatch->FailedRecords.FindByPredicate([SegmentKey](const FConnectorContinuationFailedRecord& Candidate)
							{
								return Candidate.ConnectorKey == SegmentKey;
							}))
						{
							SegmentResult.ConnectorRecord = FailedRecord->Record;
							SegmentResult.ScheduleResult = FailedRecord->ScheduleResult;
							SegmentResult.FrozenTerrainContract = FailedRecord->FrozenTerrainContract;
							SegmentResult.PreviewGeometry = FailedRecord->PreviewGeometry;
						}
						if (SegmentResult.PreviewGeometry.SharedCellSizeInBlocks != FIntVector::ZeroValue)
						{
							Component->ExplicitPreviewConnectorKeys.Add(SegmentKey);
						}
						continue;
					}
					SegmentResult.bSucceeded = !PublishedRecord->bIsPartial;
					SegmentResult.bIsPartial = PublishedRecord->bIsPartial;
					SegmentResult.ConnectorRecord = PublishedRecord->Record;
					SegmentResult.ScheduleResult = PublishedRecord->ScheduleResult;
					SegmentResult.FrozenTerrainContract = PublishedRecord->FrozenTerrainContract;
					SegmentResult.PreviewGeometry = PublishedRecord->PreviewGeometry;
					SegmentResult.FailureReason = SharedBatch->FailureReasons.FindRef(SegmentKey);
					Component->ExplicitPreviewConnectorKeys.Add(SegmentKey);
					bAllSegmentsSucceeded &= SegmentResult.bSucceeded;
					bAnySegmentSucceeded |= SegmentResult.bSucceeded;
					bAnyPartial |= SegmentResult.bIsPartial;
				}
				ExplicitResult.bSucceeded = bAnySegmentSucceeded;
				ExplicitResult.bIsPartial = bAnyPartial || (bAnySegmentSucceeded && !bAllSegmentsSucceeded);
				if (!ExplicitResult.bSucceeded)
				{
					TArray<FString> SegmentFailureReasons;
					for (const FLayoutExplicitContinuationSegmentSolveResult& Segment : ExplicitResult.Segments)
					{
						if (!Segment.FailureReason.IsEmpty())
						{
							SegmentFailureReasons.Add(FString::Printf(TEXT("Segment %d: %s"),
								Segment.Descriptor.SegmentIndex,
								*Segment.FailureReason));
						}
					}
					ExplicitResult.FailureReason = !SegmentFailureReasons.IsEmpty()
						? FString::Join(SegmentFailureReasons, TEXT(" | "))
						: TEXT("Explicit continuation route did not publish every segment artifact.");
				}
				Component->CompleteExplicitContinuationRequest(ExplicitKey, ExplicitResult);
			}
		};

		// Prewarm/preflight rejection does not create a solve submission. Retain the
		// terminal publisher so rejected segments still settle this explicit route.
		const FLayoutBackgroundSolvePublish TerminalFailurePublish = Submission.PublishOnGameThread;
		const uint64 ConnectorSubmissionLayoutGroupId = Submission.LayoutGroupId;
		const int32 ConnectorSubmissionPriority = Submission.Priority;
		FLayoutFrozenSolveSubmissionFactory ConnectorSolveFactory = [
			WeakThis,
			SharedResult,
			ConnectorKey,
			CapturedProducedDescriptorArtifact,
			SharedDescriptorWorkerSolvePacket,
			CapturedDescriptorId,
			CapturedSolvedArtifactId,
			RefreshGeneration,
			Submission = MoveTemp(Submission)](
			const FLayoutBackgroundSolveCompletion& PreflightCompletion,
			const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
			FLayoutBackgroundSolveSubmission& OutSubmission,
			FString& OutFailureReason) mutable
		{
			UChunkWorldLayoutRuntimeComponent* const Component = WeakThis.Get();
			if (Component == nullptr)
			{
				OutFailureReason = TEXT("Connector descriptor storage lost runtime component.");
				return false;
			}
			if (Component->ConnectorRefreshGeneration != RefreshGeneration)
			{
				OutFailureReason = TEXT("Connector descriptor storage rejected a stale refresh generation.");
				return false;
			}
			if (!Component->FrozenSubmissionStore.IsValid())
			{
				Component->FrozenSubmissionStore = MakeShared<FLayoutFrozenSubmissionStore>();
			}
			if (Component->IsFrozenSubmissionDescriptorTombstoned(CapturedProducedDescriptorArtifact.DescriptorSeed.DescriptorId))
			{
				OutFailureReason = TEXT("Connector descriptor storage rejected a tombstoned frozen submission descriptor.");
				return false;
			}

			FLayoutFrozenSubmissionDescriptor StoredDescriptor;
			if (!LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
					PreflightCompletion,
					PreflightResult,
					*Component->FrozenSubmissionStore,
					&CapturedProducedDescriptorArtifact,
					StoredDescriptor,
					OutFailureReason))
			{
				return false;
			}
			Component->PendingConnectorFrozenSubmissionDescriptorIdsByKey.Add(ConnectorKey, StoredDescriptor.DescriptorId);
			Component->PendingConnectorFrozenSubmissionGenerationsByKey.Add(ConnectorKey, StoredDescriptor.Generation);
			Component->PendingConnectorFrozenSubmissionAttemptIndicesByKey.Add(ConnectorKey, StoredDescriptor.AttemptIndex);
			Component->PendingConnectorFrozenSubmissionAuditHashesByKey.Add(ConnectorKey, static_cast<int32>(StoredDescriptor.AuditHash));

			if (!LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
					*Component->FrozenSubmissionStore,
					StoredDescriptor.DescriptorId,
					ConnectorKey,
					StoredDescriptor.Generation,
					StoredDescriptor.AttemptIndex,
					ELayoutFrozenSubmissionRegionKind::Continuation,
					StoredDescriptor.AuditHash,
					TEXT("Connector-continuation solve enqueue"),
					OutFailureReason))
			{
				Component->TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, CapturedDescriptorId);
				return false;
			}
			const FLayoutFrozenSubmissionDescriptor* const LookupDescriptor = LayoutFrozenSubmissionDescriptorProducer::FindForSubmit(
				*Component->FrozenSubmissionStore,
				StoredDescriptor.DescriptorId,
				ConnectorKey,
				StoredDescriptor.Generation,
				StoredDescriptor.AttemptIndex,
				ELayoutFrozenSubmissionRegionKind::Continuation,
				StoredDescriptor.AuditHash,
				OutFailureReason);
			if (LookupDescriptor == nullptr)
			{
				Component->TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, CapturedDescriptorId);
				return false;
			}

			FLayoutWorkerSolvePacket DescriptorWorkerSolvePacket = LookupDescriptor->Snapshot.WorkerSolvePacket;
			if (SharedDescriptorWorkerSolvePacket->bHasPrecomputedAdapterOutput)
			{
				// Prewarm finalized this exact immutable descriptor. Preserve its contract
				// when the descriptor-store lookup restores the raw packet for submission.
				DescriptorWorkerSolvePacket.RequestManifest = SharedDescriptorWorkerSolvePacket->RequestManifest;
				DescriptorWorkerSolvePacket.bHasPrecomputedAdapterOutput = true;
				DescriptorWorkerSolvePacket.PrecomputedAdapterOutput =
					SharedDescriptorWorkerSolvePacket->PrecomputedAdapterOutput;
			}
			*SharedDescriptorWorkerSolvePacket = DescriptorWorkerSolvePacket;
			Submission.Work = [SharedResult, DescriptorWorkerSolvePacket, CapturedSolvedArtifactId](const FLayoutSolveCancellationToken& CancellationToken, FString& WorkerFailureReason) mutable
			{
				if (CancellationToken.IsCancellationRequested())
				{
					WorkerFailureReason = TEXT("Connector continuation solve canceled before proof.");
					return false;
				}

				FLayoutRegionSolveRequest FinalizedWorkerRequest;
				const bool bSolveWorkSucceeded = RunSharedRootSolveWork(
					DescriptorWorkerSolvePacket,
					CapturedSolvedArtifactId,
					SharedResult->ScheduleResult,
					SharedResult->SolvedArtifact,
					WorkerFailureReason,
					&FinalizedWorkerRequest);

				// Preserve the adapter-produced frozen terrain contract on both full and
				// retained-partial paths. Preview diagnostics must use realization coordinates.
				SharedResult->FrozenTerrainContract = FinalizedWorkerRequest.PrecomputedFrozenTerrainContract;
				SharedResult->FrozenTerrainContract.ActiveCells = FinalizedWorkerRequest.PrecomputedActiveCells;
				BackfillRequestOwnedVerticalAccessPlacementsOnMergedSolveResult(FinalizedWorkerRequest, SharedResult->ScheduleResult);

				const bool bHasRetainedPartial =
					!bSolveWorkSucceeded
					&& HasRetainedPartialPlacements(SharedResult->ScheduleResult.MergedSolveResult);
				if (!bSolveWorkSucceeded && !bHasRetainedPartial)
				{
					return false;
				}

				if (CancellationToken.IsCancellationRequested())
				{
					WorkerFailureReason = TEXT("Connector continuation solve canceled after proof.");
					return false;
				}

				WorkerFailureReason = SharedResult->ScheduleResult.FailureReason.IsEmpty()
					? SharedResult->ScheduleResult.MergedSolveResult.FailureReason
					: SharedResult->ScheduleResult.FailureReason;
				return bSolveWorkSucceeded || bHasRetainedPartial;
			};

			OutSubmission = MoveTemp(Submission);
			return true;
		};
		FLayoutBackgroundSolveDispatcher& BackgroundDispatcher = GetOrCreateBackgroundSolveDispatcher();
		FLayoutBackgroundSolveSubmission LifecycleSubmission =
			FLayoutBackgroundLifecycleSequencer::BuildManifestPrewarmThenPreflightThenSolveSubmission(
				&BackgroundDispatcher,
				FString::Printf(TEXT("ConnectorContinuationPrewarm %llu"), ConnectorKey),
				FString::Printf(TEXT("ConnectorContinuationPreflight %llu"), ConnectorKey),
				ConnectorSubmissionLayoutGroupId,
				ConnectorSubmissionPriority,
				ConnectorSubmissionPriority,
				MoveTemp(ManifestPrewarmInput),
				MoveTemp(ConnectorSolveFactory),
				[ConnectorKey, CapturedRecord, SharedDescriptorWorkerSolvePacket, SharedResult, SharedPreviewGeometry, SharedCellSizeInBlocks, TerminalFailurePublish](
					const FLayoutBackgroundSolveCompletion& Completion,
					const FLayoutManifestPrewarmResult& PrewarmResult)
				{
					const FLayoutAdapterOutput* const DiagnosticAdapterOutput =
						PrewarmResult.bHasPrecomputedAdapterOutput
							? &PrewarmResult.PrecomputedAdapterOutput
							: (PrewarmResult.bHasRejectedAdapterPreview
								? &PrewarmResult.RejectedAdapterPreview
								: nullptr);
					if (PrewarmResult.bHasDerivedStructuralContract)
					{
						SharedDescriptorWorkerSolvePacket->RequestManifest = PrewarmResult.FinalizedManifest;
					}
					if (PrewarmResult.bHasPrecomputedAdapterOutput)
					{
						SharedDescriptorWorkerSolvePacket->bHasPrecomputedAdapterOutput = true;
						SharedDescriptorWorkerSolvePacket->PrecomputedAdapterOutput = PrewarmResult.PrecomputedAdapterOutput;
						// Preserve finalized adapter topology before CSP/artifact publication.
						// A later rejection must still show bridges, seams, and host intent.
						PopulateContinuationPreviewTerrainCells(
							*SharedPreviewGeometry,
							PrewarmResult.PrecomputedAdapterOutput.PlannedCells,
							PrewarmResult.PrecomputedAdapterOutput.FrozenTerrainContract,
							&PrewarmResult.PrecomputedAdapterOutput.ActiveCells);
						AppendContinuationPreviewVerticalAccessDiagnostics(
							*SharedPreviewGeometry,
							PrewarmResult.PrecomputedAdapterOutput);
					}
					if (PrewarmResult.bHasRejectedAdapterPreview)
					{
						// Rejected topology remains editor-only. It supplies bridge and seam
						// evidence without becoming a final contract or solve submission.
						const FLayoutAdapterOutput& RejectedPreview = PrewarmResult.RejectedAdapterPreview;
						FLayoutSolveResult& RejectedSolveResult = SharedResult->ScheduleResult.MergedSolveResult;
						RejectedSolveResult.bSucceeded = false;
						RejectedSolveResult.PlannedCells = RejectedPreview.PlannedCells;
						RejectedSolveResult.FootprintSize = SharedDescriptorWorkerSolvePacket->RequestManifest.FootprintSize;
						RejectedSolveResult.SharedCellSizeInBlocks = SharedCellSizeInBlocks;
						RejectedSolveResult.TemplatePlacementZOffsetBlocks =
							SharedDescriptorWorkerSolvePacket->RuntimeSnapshot.TemplatePlacementZOffsetBlocks;
						RejectedSolveResult.RootPlacementKind = SharedDescriptorWorkerSolvePacket->RuntimeSnapshot.PlacementKind;
						RejectedSolveResult.WorldBindingPlacementPolicy =
							SharedDescriptorWorkerSolvePacket->RuntimeSnapshot.PlacementPolicy;
						RejectedSolveResult.ResolvedTerrainAlignmentLevel =
							SharedDescriptorWorkerSolvePacket->RequestManifest.RootContinuationSelection.ResolvedEntryLevel;
						SharedResult->FrozenTerrainContract = RejectedPreview.FrozenTerrainContract;
						PopulateContinuationPreviewTerrainCells(
							*SharedPreviewGeometry,
							RejectedPreview.PlannedCells,
							RejectedPreview.FrozenTerrainContract);
						AppendContinuationPreviewVerticalAccessDiagnostics(*SharedPreviewGeometry, RejectedPreview);
						for (const FLayoutAdapterDiagnostic& AdapterDiagnostic : RejectedPreview.Diagnostics)
						{
							FLayoutContinuationPreviewDiagnostic& PreviewDiagnostic =
								SharedPreviewGeometry->Diagnostics.AddDefaulted_GetRef();
							PreviewDiagnostic.LocalCell = AdapterDiagnostic.Cell;
							PreviewDiagnostic.bHasLocalCell = true;
							PreviewDiagnostic.RelatedLocalCell = AdapterDiagnostic.RelatedCell;
							PreviewDiagnostic.bHasRelatedLocalCell = AdapterDiagnostic.bHasRelatedCell;
							PreviewDiagnostic.Stage = TEXT("Terrain adapter rejection");
							PreviewDiagnostic.Requirement = AdapterDiagnostic.bHasRelatedCell
								? FString::Printf(TEXT("Terrain edge to %s"), *AdapterDiagnostic.RelatedCell.ToString())
								: FString();
							PreviewDiagnostic.Detail = AdapterDiagnostic.Detail;
						}
					}
					if (!Completion.bWorkSucceeded)
					{
						UE_LOG(
							LogTemp,
							Warning,
							TEXT("Connector continuation prewarm failed for key=%llu: %s"),
							ConnectorKey,
							*(!Completion.FailureReason.IsEmpty()
								? Completion.FailureReason
								: PrewarmResult.FailureReason));
						if (TerminalFailurePublish)
						{
							TerminalFailurePublish(Completion);
						}
					}
				},
				[ConnectorKey, TerminalFailurePublish](
					const FLayoutBackgroundSolveCompletion& Completion,
					const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult)
				{
					if (!Completion.bWorkSucceeded || !PreflightResult.bAdmissible)
					{
						UE_LOG(
							LogTemp,
							Warning,
							TEXT("Connector continuation preflight rejected for key=%llu: %s"),
							ConnectorKey,
							*(!Completion.FailureReason.IsEmpty()
								? Completion.FailureReason
								: PreflightResult.FailureReason));
						if (TerminalFailurePublish)
						{
							TerminalFailurePublish(Completion);
						}
					}
				});
		const FLayoutBackgroundSolveHandle SubmittedHandle = BackgroundDispatcher.Submit(MoveTemp(LifecycleSubmission));
		if (!SubmittedHandle.IsValid())
		{
			SharedBatch->CompletedConnectorKeys.Add(ConnectorKey);
			TombstoneConnectorFrozenSubmissionDescriptor(ConnectorKey, CapturedDescriptorId);
			ReleaseContinuationReservationForConnector(ConnectorKey);
			if (TFunction<void(const FLayoutExplicitContinuationSolveResult&)>* Callback =
				ExplicitContinuationCompletionCallbacks.Find(ConnectorKey))
			{
				FLayoutExplicitContinuationSolveResult Result;
				Result.FailureReason = TEXT("Explicit continuation lifecycle submission was rejected.");
				(*Callback)(Result);
				ExplicitContinuationCompletionCallbacks.Remove(ConnectorKey);
				PendingExplicitPreparedRoutes.Remove(ConnectorKey);
				ExplicitRouteSegmentKeysByConnectorKey.Remove(ConnectorKey);
				ExplicitRoutesByConnectorKey.Remove(ConnectorKey);
			}
			--SharedBatch->ExpectedCompletions;
			continue;
		}
		PendingConnectorRefreshSolveHandles.Add(SubmittedHandle);
	}
}

bool UChunkWorldLayoutRuntimeComponent::RejectAutoDiscoveredResolvedSiteAfterRealizationFailure(
	const FString& SiteRecordKey,
	const FResolvedLayoutSiteRecord& SiteRecord,
	const FString& RejectionReason)
{
	if (RejectionReason.IsEmpty())
	{
		return false;
	}

	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
		SiteRecord.GetWorldBindingFrontendSelection();
	if (FrontendSelection.WorldBindingId.IsNone())
	{
		return false;
	}

	ULayoutPlanningWindowStore* const Store = GetOrCreateLayoutPlanningWindowStore();
	if (Store == nullptr)
	{
		return false;
	}

	const FResolvedLayoutSiteRuntimeState RuntimeState =
		SiteRecord.GetResolvedSiteRuntimeState();
	if (const FString* const PlanningRecordKey = PlanningRecordKeysBySiteRecordKey.Find(SiteRecordKey))
	{
		const bool bRejectedRecord = RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
			*PlanningRecordKey,
			RejectionReason,
			NAME_None,
			RuntimeState.TerrainFitDiagnosticKind);
		if (!bRejectedRecord)
		{
			return false;
		}

		PlanningRecordKeysBySiteRecordKey.Remove(SiteRecordKey);
		return true;
	}

	const ULayoutWorldBindingAsset* const WorldBinding =
		ResolveRuntimeWorldBindingForSiteRecord(LayoutWorldBindings, SiteRecord);
	if (WorldBinding == nullptr)
	{
		return false;
	}

	FPlannedLayoutSiteRecord RejectedRecord;
	FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection;
	ReservationSourceSelection.WorldBinding =
		TSoftObjectPtr<ULayoutWorldBindingAsset>(const_cast<ULayoutWorldBindingAsset*>(WorldBinding));
	ReservationSourceSelection.ReservationKey = FLayoutSiteReservation::ComputeReservationKey(
		SiteRecord.GetResolvedSiteLocationMetadata().SiteCenterBlockWorldPos);
	ReservationSourceSelection.SiteCenterBlockWorldPos =
		SiteRecord.GetResolvedSiteLocationMetadata().SiteCenterBlockWorldPos;
	ReservationSourceSelection.WorldSeed = ResolveLayoutWorldSeed();
	RejectedRecord.SetPlannedSiteReservationSourceSelection(ReservationSourceSelection);
	RejectedRecord.SetWorldBindingFrontendSelection(FrontendSelection);

	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
	LifecycleMetadata.StableRecordKey =
		ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
			FrontendSelection,
			ReservationSourceSelection);
	RejectedRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	RejectedRecord.SetRootPublicationMetadata(SiteRecord.GetRootPublicationMetadata());
	RejectedRecord.SetSiteSolveSourceSelection(SiteRecord.GetSiteSolveSourceSelection());

	FPlannedLayoutSiteRecord StoredRecord;
	if (!Store->UpsertPendingPlannedLayoutSiteRecord(RejectedRecord, StoredRecord)
		&& StoredRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey.IsEmpty())
	{
		return false;
	}

	const FString StableRecordKey =
		StoredRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey.IsEmpty()
			? RejectedRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey
			: StoredRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey;
	return RejectPlanningRootAndTombstoneFrozenSubmissionDescriptor(
		StableRecordKey,
		RejectionReason,
		NAME_None,
		RuntimeState.TerrainFitDiagnosticKind);
}

void UChunkWorldLayoutRuntimeComponent::TryRealizeEligibleConnectors()
{
	const TArray<FIntVector> PlayerBlockWorldPositions = CollectPlanningWindowCenters();

	TArray<uint64> EligibleConnectorKeys;
	for (const TPair<uint64, FResolvedLayoutConnectorRecord>& Pair : ResolvedConnectorRecords)
	{
		if (ExplicitPreviewConnectorKeys.Contains(Pair.Key)
			|| !ShouldAttemptConnectorRealization(Pair.Value))
		{
			continue;
		}
		const TSet<FIntVector> RequiredChunkOrigins = CollectRequiredChunkOrigins(Pair.Value);
		if (AreRequiredChunkOriginsLoaded(RequiredChunkOrigins))
		{
			EligibleConnectorKeys.Add(Pair.Key);
		}
	}

	EligibleConnectorKeys.Sort([this, &PlayerBlockWorldPositions](const uint64 LeftKey, const uint64 RightKey)
	{
		auto ClosestDistanceSquared = [&PlayerBlockWorldPositions](const FResolvedLayoutConnectorRecord& Record)
		{
			int64 ClosestDistance = TNumericLimits<int64>::Max();
			for (const FIntVector& PlayerPosition : PlayerBlockWorldPositions)
			{
				const FIntVector Delta = Record.PathOriginBlockWorldPos - PlayerPosition;
				ClosestDistance = FMath::Min(ClosestDistance,
					static_cast<int64>(Delta.X) * Delta.X
					+ static_cast<int64>(Delta.Y) * Delta.Y
					+ static_cast<int64>(Delta.Z) * Delta.Z);
			}
			return ClosestDistance;
		};
		const FResolvedLayoutConnectorRecord* const Left = ResolvedConnectorRecords.Find(LeftKey);
		const FResolvedLayoutConnectorRecord* const Right = ResolvedConnectorRecords.Find(RightKey);
		const int64 LeftDistance = Left != nullptr ? ClosestDistanceSquared(*Left) : TNumericLimits<int64>::Max();
		const int64 RightDistance = Right != nullptr ? ClosestDistanceSquared(*Right) : TNumericLimits<int64>::Max();
		return LeftDistance != RightDistance ? LeftDistance < RightDistance : LeftKey < RightKey;
	});

	TArray<uint64> FailedConnectorKeys;
	for (const uint64 ConnectorKey : EligibleConnectorKeys)
	{
		FResolvedLayoutConnectorRecord* const ConnectorRecord = ResolvedConnectorRecords.Find(ConnectorKey);
		if (ConnectorRecord == nullptr || !ShouldAttemptConnectorRealization(*ConnectorRecord))
		{
			continue;
		}

		const TSet<FIntVector> RequiredChunkOrigins = CollectRequiredChunkOrigins(*ConnectorRecord);
		if (RequiresFreshCreatedChunkRealizationGate(*ConnectorRecord)
			&& !HasFreshCreatedChunkEligibility(RequiredChunkOrigins))
		{
			continue;
		}

		const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride =
			ResolvedConnectorFrozenTerrainContracts.Find(ConnectorKey);
		FString FailureReason;
		if (TryRealizeConnector(*ConnectorRecord, &FailureReason, FrozenTerrainContractOverride))
		{
			MarkResolvedConnectorRealizedAndCommitted(*ConnectorRecord);
			ConsumeContinuationReservationForConnector(ConnectorKey);
		}
		else if (!FailureReason.IsEmpty())
		{
			ReleaseContinuationReservationForConnector(ConnectorKey, true);
			FailedConnectorKeys.Add(ConnectorKey);
			UE_LOG(
				LogTemp,
				Warning,
				TEXT("Layout connector realization failed between %s and %s: %s"),
				*ConnectorRecord->StartEndpointBlockWorldPos.ToString(),
				*ConnectorRecord->EndEndpointBlockWorldPos.ToString(),
				*FailureReason);
		}
	}
	for (const uint64 ConnectorKey : FailedConnectorKeys)
	{
		ResolvedConnectorFrozenTerrainContracts.Remove(ConnectorKey);
		ResolvedConnectorRecords.Remove(ConnectorKey);
	}
}

bool UChunkWorldLayoutRuntimeComponent::AreRequiredChunksObserved(const FResolvedLayoutSiteRecord& SiteRecord) const
{
	return AreRequiredChunkOriginsLoaded(CollectRequiredChunkOrigins(SiteRecord));
}

TSet<FIntVector> UChunkWorldLayoutRuntimeComponent::CollectRequiredChunkOrigins(const FResolvedLayoutSiteRecord& SiteRecord) const
{
	TSet<FIntVector> ChunkOrigins;
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
	{
		return ChunkOrigins;
	}

	const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection =
		SiteRecord.GetSiteSolveSourceSelection();
	const ULayoutRegionContentSetAsset* const ContentSet =
		ResolveResidentOrLoadSoftObject(SiteSolveSourceSelection.ContentSet);
	const ULayoutWorldBindingAsset* const WorldBinding =
		ResolveRuntimeWorldBindingForSiteRecord(LayoutWorldBindings, SiteRecord);
	FResolvedLayoutSiteRecord NormalizedSiteRecord = SiteRecord;
	NormalizeRuntimeWorldBindingMetrics(
		NormalizedSiteRecord,
		WorldBinding,
		ContentSet);
	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		NormalizedSiteRecord.GetResolvedSiteSolvedPayload();
	const FIntVector SharedCellSizeInBlocks =
		SolvedPayload.SolveResult.SharedCellSizeInBlocks;
	if (SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return ChunkOrigins;
	}

	const FLayoutFrozenTerrainContract* Contract = ResolvedRootFrozenTerrainContracts.Find(
		SiteRecord.GetRootPublicationMetadata().RootSolveId);
	FLayoutRealizationWritePlan WritePlan;
	FString FailureReason;
	if (Contract && !LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource::Site, SiteRecord.SolvedArtifactId, SiteRecord.SolvedArtifactActiveCellCount,
		SolvedPayload.SolveResult, *Contract, WritePlan, FailureReason)) return {};
	for (const FLayoutPlacedModule& Placement : SolvedPayload.SolveResult.Placements)
	{
		FIntVector Anchor = ComputePlacementAnchorBlockWorldPos(NormalizedSiteRecord, Placement, SharedCellSizeInBlocks);
		if (Contract)
		{
			const auto* Entry = WritePlan.TemplatePlacements.Entries.FindByPredicate(
				[&Placement](const FLayoutRealizationTemplatePlacementEntry& Item)
				{
					return Item.Cell == Placement.Cell && Item.ModuleSnapshotId == Placement.ModuleSnapshotId
						&& Item.YawRotationSteps == Placement.YawRotationSteps;
				});
			if (!Entry) return {};
			Anchor = Entry->AcceptedAnchorBlockWorldPos;
		}
		for (const FIntVector& LeafAnchor : FLayoutStreamingWindow::BuildPlacementAnchorBlockWorldPositions(
			Placement, Anchor, SharedCellSizeInBlocks))
			ChunkOrigins.Add(FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(LeafAnchor, ChunkWorld->WorldGenDef->ChunkBlockSize));
		if (!TryAddPlacementTemplateChunkOrigins(Placement, Anchor,
			SharedCellSizeInBlocks, ChunkWorld->WorldGenDef->ChunkBlockSize, ChunkOrigins)) return {};
	}
	if (Contract)
	{
		FLayoutStreamingWindow::AddFrozenTerrainChunkOrigins(*Contract, ChunkWorld->WorldGenDef->ChunkBlockSize, ChunkOrigins);
	}
	return ChunkOrigins;
}

bool UChunkWorldLayoutRuntimeComponent::ShouldAttemptConnectorRealization(const FResolvedLayoutConnectorRecord& ConnectorRecord) const
{
	const FResolvedLayoutConnectorRuntimeState RuntimeState =
		ConnectorRecord.GetResolvedConnectorRuntimeState();
	return RuntimeState.bLayoutSolved
		&& !RuntimeState.bLayoutRealized
		&& !RuntimeState.bHasBeenCommittedToChunkWorld;
}

bool UChunkWorldLayoutRuntimeComponent::AreRequiredChunksObserved(const FResolvedLayoutConnectorRecord& ConnectorRecord) const
{
	const TSet<FIntVector> RequiredChunkOrigins = CollectRequiredChunkOrigins(ConnectorRecord);
	return AreRequiredChunkOriginsLoaded(RequiredChunkOrigins);
}

TSet<FIntVector> UChunkWorldLayoutRuntimeComponent::CollectRequiredChunkOrigins(const FResolvedLayoutConnectorRecord& ConnectorRecord) const
{
	TSet<FIntVector> ChunkOrigins;
	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
	{
		return ChunkOrigins;
	}

	const FLayoutResolvedConnectorSolveSourceSelection SolveSourceSelection =
		ConnectorRecord.GetResolvedConnectorSolveSourceSelection();
	const ULayoutRegionContentSetAsset* const ContentSet =
		ResolveResidentOrLoadSoftObject(SolveSourceSelection.ContentSet);
	const ULayoutWorldBindingAsset* const WorldBinding =
		ResolveRuntimeWorldBindingForConnectorRecord(LayoutWorldBindings, ConnectorRecord);
	FResolvedLayoutConnectorRecord NormalizedConnectorRecord = ConnectorRecord;
	NormalizeRuntimeWorldBindingMetrics(
		NormalizedConnectorRecord,
		WorldBinding,
		ContentSet);
	const FIntVector SharedCellSizeInBlocks =
		NormalizedConnectorRecord.SolveResult.SharedCellSizeInBlocks;
	if (SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return ChunkOrigins;
	}

	const uint64 ConnectorKey = BuildResolvedConnectorRecordKey(ConnectorRecord);
	const FLayoutFrozenTerrainContract* const FrozenTerrainContract =
		ResolvedConnectorFrozenTerrainContracts.Find(ConnectorKey);
	if (FrozenTerrainContract == nullptr || FrozenTerrainContract->ContractId.IsNone())
	{
		return ChunkOrigins;
	}
	FLayoutRealizationWritePlan WritePlan;
	FString WritePlanFailureReason;
	if (!LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Connector,
			NormalizedConnectorRecord.SolvedArtifactId,
			NormalizedConnectorRecord.SolvedArtifactActiveCellCount,
			NormalizedConnectorRecord.SolveResult,
			*FrozenTerrainContract,
			WritePlan,
			WritePlanFailureReason))
	{
		return ChunkOrigins;
	}
	for (const FLayoutPlacedModule& Placement : NormalizedConnectorRecord.SolveResult.Placements)
	{
		const FLayoutRealizationTemplatePlacementEntry* const WritePlanEntry =
			WritePlan.TemplatePlacements.Entries.FindByPredicate(
				[&Placement](const FLayoutRealizationTemplatePlacementEntry& Candidate)
				{
					return Candidate.Cell == Placement.Cell
						&& Candidate.ModuleSnapshotId == Placement.ModuleSnapshotId
						&& Candidate.YawRotationSteps == Placement.YawRotationSteps;
				});
		if (WritePlanEntry == nullptr)
		{
			return TSet<FIntVector>();
		}
		if (!TryAddPlacementTemplateChunkOrigins(Placement, WritePlanEntry->AcceptedAnchorBlockWorldPos,
			SharedCellSizeInBlocks, ChunkWorld->WorldGenDef->ChunkBlockSize, ChunkOrigins)) return {};
		for (const FIntVector& AnchorBlockWorldPos : FLayoutStreamingWindow::BuildPlacementAnchorBlockWorldPositions(
			Placement,
			WritePlanEntry->AcceptedAnchorBlockWorldPos,
			SharedCellSizeInBlocks))
		{
			ChunkOrigins.Add(FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(
				AnchorBlockWorldPos,
				ChunkWorld->WorldGenDef->ChunkBlockSize));
		}
	}
	FLayoutStreamingWindow::AddFrozenTerrainChunkOrigins(*FrozenTerrainContract, ChunkWorld->WorldGenDef->ChunkBlockSize, ChunkOrigins);
	return ChunkOrigins;
}

bool UChunkWorldLayoutRuntimeComponent::TryRealizeSite(
	FResolvedLayoutSiteRecord& SiteRecord,
	FString* OutFailureReason,
	const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Realization_Site, STAT_PorismLayout_Realization);
	auto SetFailureReason = [OutFailureReason](const FString& Message)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = Message;
		}
	};

	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	const FResolvedLayoutSiteLocationMetadata LocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection =
		SiteRecord.GetSiteSolveSourceSelection();
	ULayoutRegionContentSetAsset* ContentSet = nullptr;
	ULayoutProfileAsset* LayoutProfile = nullptr;
	const ULayoutWorldBindingAsset* WorldBinding = nullptr;
	FResolvedLayoutSiteSolvedPayload OriginalSolvedPayload;
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Realization_AssetRehydrate, STAT_PorismLayout_RealizationAssetRehydrate);
		ContentSet = ResolveResidentOrLoadSoftObject(SiteSolveSourceSelection.ContentSet);
		LayoutProfile = ResolveResidentOrLoadSoftObject(SiteSolveSourceSelection.LayoutProfile);
		WorldBinding = ResolveRuntimeWorldBindingForSiteRecord(LayoutWorldBindings, SiteRecord);
		OriginalSolvedPayload = SiteRecord.GetResolvedSiteSolvedPayload();
		RehydrateSolvedPlacementAssetCarriersFromContentSet(
			SiteRecord,
			ContentSet);
		NormalizeRuntimeWorldBindingMetrics(
			SiteRecord,
			WorldBinding,
			ContentSet);
	}
	const FResolvedLayoutSiteSolvedPayload NormalizedSolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	const FIntVector SharedCellSizeInBlocks =
		NormalizedSolvedPayload.SolveResult.SharedCellSizeInBlocks;
	if (ChunkWorld == nullptr || SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		SetFailureReason(FString::Printf(
			TEXT("ChunkWorld=%s LayoutProfile=%s SharedCellSize=%s ContentSet=%s."),
			*GetNameSafe(ChunkWorld),
			*GetNameSafe(LayoutProfile),
			*SharedCellSizeInBlocks.ToString(),
			*GetNameSafe(ContentSet)));
		return false;
	}

	FLayoutFrozenTerrainContract TemplateOnlyContract;
	if (FrozenTerrainContractOverride == nullptr
		&& NormalizedSolvedPayload.SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::None)
	{
		// Standalone placement prepares the same no-terrain-write standard adapter authority
		// as region prewarm. Every template then uses the validated write-plan anchor.
		if (SiteRecord.RootSolveId.IsNone() || SiteRecord.SolvedArtifactId.IsNone())
		{
			SetFailureReason(TEXT("Standalone realization requires producer root and solved-artifact identity."));
			return false;
		}
		FLayoutRegionSolveRequest Request;
		Request.FootprintSize = NormalizedSolvedPayload.SolveResult.FootprintSize;
		Request.PlannedCells = NormalizedSolvedPayload.SolveResult.PlannedCells;
		FLayoutContractManifest Manifest;
		Manifest.SharedCellSizeInBlocks = SharedCellSizeInBlocks;
		FLayoutContractModeAdapterInput Input;
		Input.SolveRequest = &Request;
		Input.Manifest = &Manifest;
		Input.ModePlan.EnvironmentMode = ELayoutContractEnvironmentMode::StandardRegion;
		Input.ModePlan.SiteCenterBlockWorldPos = LocationMetadata.SiteCenterBlockWorldPos;
		Input.ModePlan.SolveSeed = NormalizedSolvedPayload.SolveResult.Seed;
		Input.ModePlan.ModePlanId = FLayoutContractPipeline::BuildModePlanId(Input.ModePlan);
		FLayoutAdapterOutput Prepared;
		FLayoutRegionContract Contract;
		FString Failure;
		if (!FLayoutContractModeAdapter::TryPrepareRegionOutput(Input, Prepared, Failure)
			|| !FLayoutContractPipeline::TryBuildPreparedRegionContract(Manifest, Request.FootprintSize, Prepared, Contract, Failure))
		{
			SetFailureReason(Failure);
			return false;
		}
		TemplateOnlyContract = MoveTemp(Contract.FrozenTerrainContract);
		TemplateOnlyContract.FootprintMinBlockWorldPos = ComputeFootprintMinBlockWorldPos(SiteRecord, SharedCellSizeInBlocks);
		for (const auto& Pair : SiteRecord.CachedFrozenTerrainBaseZByColumn)
		{
			auto& Stage = TemplateOnlyContract.StageMap.AddDefaulted_GetRef();
			Stage.FootprintCellXY = Pair.Key;
			Stage.ResolvedStageBaseBlockWorldZ = Pair.Value;
		}
		TemplateOnlyContract.ContractId = FLayoutContractPipeline::BuildTerrainWriteArtifactId(TemplateOnlyContract);
		SiteRecord.SolvedArtifactActiveCellCount = TemplateOnlyContract.ActiveCells.Num();
		FrozenTerrainContractOverride = &TemplateOnlyContract;
	}
	const FLayoutWorldBindingPlacementPolicy& PlacementPolicy =
		NormalizedSolvedPayload.SolveResult.WorldBindingPlacementPolicy;
	SetResolvedSiteTerrainFitDiagnostic(
		SiteRecord,
		ELayoutWorldBindingTerrainFitDiagnosticKind::None);

	// Validate emitted fill thickness per block column. World-Z distance from
	// SiteCenter includes template/stage offsets and is not foundation depth.
	if (FrozenTerrainContractOverride != nullptr
		&& FrozenTerrainContractOverride->CellContracts.ContainsByPredicate(
			[](const FLayoutTerrainCellContractRecord& C) { return C.bHasFoundationFillEvidence; }))
	{
		TMap<FIntPoint, int32> FillCountByBlockXY;
		int32 EmittedFoundationDepth = 0;
		for (const FLayoutFrozenTerrainWriteRecord& Write : FrozenTerrainContractOverride->TerrainWrites)
		{
			if (Write.SourceContract != ELayoutFrozenTerrainCellContract::Active
				|| Write.Material == EmptyMaterial)
			{
				continue;
			}
			const int32 FillCount = ++FillCountByBlockXY.FindOrAdd(FIntPoint(
				Write.BlockWorldPos.X,
				Write.BlockWorldPos.Y));
			EmittedFoundationDepth = FMath::Max(EmittedFoundationDepth, FillCount);
		}
		if (EmittedFoundationDepth > 0
			&& (!PlacementPolicy.TerrainTransition.bAllowFoundationFill
				|| PlacementPolicy.TerrainTransition.MaxFoundationDepth <= 0
				|| EmittedFoundationDepth > PlacementPolicy.TerrainTransition.MaxFoundationDepth))
		{
			SetFailureReason(FString::Printf(
				TEXT("Foundation fill depth %d exceeds policy max %d or is disabled (allowed=%s)."),
				EmittedFoundationDepth,
				PlacementPolicy.TerrainTransition.MaxFoundationDepth,
				PlacementPolicy.TerrainTransition.bAllowFoundationFill ? TEXT("true") : TEXT("false")));
			SetResolvedSiteTerrainFitDiagnostic(
				SiteRecord,
				ELayoutWorldBindingTerrainFitDiagnosticKind::RejectedFoundationDepthExceeded);
			return false;
		}
	}

	// Flat ordinary roots stay on the fixed global cell lattice. When stepped
	// ordinary-root support is active, runtime realization reuses the resolved
	// terrain-fit anchor so placements, preview, and terrain shaping share one
	// footprint origin.
	FIntVector TerrainConformingSiteOffset = FIntVector::ZeroValue;
	FLayoutRealizationWritePlan SiteRealizationWritePlan;
	FLayoutRealizationWritePlanExecutionState SiteRealizationExecutionState;
	const FIntVector FootprintMinBlockWorldPos =
		ComputeFootprintMinBlockWorldPos(SiteRecord, SharedCellSizeInBlocks);
	if (FrozenTerrainContractOverride != nullptr
		&& !FrozenTerrainContractOverride->ContractId.IsNone())
	{
		const FLayoutId RootSolveId = SiteRecord.GetRootPublicationMetadata().RootSolveId;
		if (SiteRecord.bWritePlanReady && !RootSolveId.IsNone())
		{
			const TSharedPtr<FLayoutRealizationWritePlan>* const CachedWritePlan =
				ResolvedRootRealizationWritePlans.Find(RootSolveId);
			if (CachedWritePlan == nullptr || !CachedWritePlan->IsValid())
			{
				SetFailureReason(FString::Printf(
					TEXT("Realization gate: missing_cached_write_plan — rootSolveId=%s writePlanId=%s."),
					*RootSolveId.ToString(),
					*SiteRecord.CachedRealizationWritePlanId.ToString()));
				return false;
			}
			SiteRealizationWritePlan = **CachedWritePlan;

		}
		else
		{
			const FLayoutFrozenTerrainContract DefaultContract;
			const FLayoutFrozenTerrainContract& EffectiveContract =
				FrozenTerrainContractOverride != nullptr
					? *FrozenTerrainContractOverride
					: DefaultContract;

			FString RealizationInputFailureReason;
			if (!LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
					ELayoutRealizationWritePlanSource::Site,
					SiteRecord.SolvedArtifactId,
					SiteRecord.SolvedArtifactActiveCellCount,
					NormalizedSolvedPayload.SolveResult,
					EffectiveContract,
					SiteRealizationWritePlan,
					RealizationInputFailureReason))
			{
				UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] Realization write-plan rejected: %s"), *RealizationInputFailureReason);
				SetFailureReason(RealizationInputFailureReason);
				return false;
			}
		}
		const FLayoutId ContractId = FrozenTerrainContractOverride != nullptr
			? FrozenTerrainContractOverride->ContractId
			: FLayoutId();

		if (SiteRecord.bWritePlanReady)
		{
			if (SiteRecord.CachedRealizationWritePlanId != SiteRealizationWritePlan.WritePlanId
				|| SiteRecord.CachedRealizationWritePlanHash != SiteRealizationWritePlan.WritePlanHash
				|| SiteRecord.CachedRealizationProvenanceId.IsNone()
				|| SiteRecord.CachedRealizationIdempotencyMarker.IsNone()
				|| SiteRecord.CachedFrozenTerrainContractId != ContractId
				|| SiteRecord.CachedChunkWritePassCount != SiteRealizationWritePlan.ChunkWriteBatch.PassOrder.Num()
				|| SiteRecord.CachedTerrainWriteCount != SiteRealizationWritePlan.ChunkWriteBatch.TerrainWriteCount
				|| SiteRecord.CachedTemplatePlacementCount != SiteRealizationWritePlan.ChunkWriteBatch.TemplatePlacementCount)
			{
				SetFailureReason(FString::Printf(
					TEXT("Cached realization-prep metadata is stale: cachedWritePlan=%s acceptedWritePlan=%s cachedHash=%d acceptedHash=%d cachedProvenance=%s cachedIdempotency=%s cachedContract=%s acceptedContract=%s cachedPasses=%d acceptedPasses=%d cachedTerrainWrites=%d acceptedTerrainWrites=%d cachedTemplates=%d acceptedTemplates=%d."),
					*SiteRecord.CachedRealizationWritePlanId.ToString(),
					*SiteRealizationWritePlan.WritePlanId.ToString(),
					SiteRecord.CachedRealizationWritePlanHash,
					SiteRealizationWritePlan.WritePlanHash,
					*SiteRecord.CachedRealizationProvenanceId.ToString(),
					*SiteRecord.CachedRealizationIdempotencyMarker.ToString(),
					*SiteRecord.CachedFrozenTerrainContractId.ToString(),
					*ContractId.ToString(),
					SiteRecord.CachedChunkWritePassCount,
					SiteRealizationWritePlan.ChunkWriteBatch.PassOrder.Num(),
					SiteRecord.CachedTerrainWriteCount,
					SiteRealizationWritePlan.ChunkWriteBatch.TerrainWriteCount,
					SiteRecord.CachedTemplatePlacementCount,
					SiteRealizationWritePlan.ChunkWriteBatch.TemplatePlacementCount));
				return false;
			}
		}

		// Distinct stable roots may share one fresh chunk; same-root retries remain artifact-idempotent.
		{
			const TSet<FIntVector> RequiredChunkOrigins = CollectRequiredChunkOrigins(SiteRecord);
			const FLayoutId ArtifactId = SiteRecord.SolvedArtifactId;
			FString StampFailureReason;
			if (!CanStampRequiredChunkOrigins(RequiredChunkOrigins, ArtifactId, RootSolveId, StampFailureReason))
			{
				UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] Realization gate: chunk_stamp_conflict — %s"), *StampFailureReason);
				SetFailureReason(StampFailureReason);
				return false;
			}
		}

		FString TerrainContractFailureReason;
		{
			PORISM_LAYOUT_PROFILE_SCOPE(Layout_Realization_TerrainReplay, STAT_PorismLayout_TerrainReplay);
			if (!LayoutRealizationWritePlan::ApplyTerrainWriteReplay(
					ChunkWorld,
					SiteRealizationWritePlan,
					*FrozenTerrainContractOverride,
					SiteRealizationExecutionState,
					TerrainContractFailureReason,
					[this](const TArray<FIntVector>& Positions) { return ReadLoadedTerrainMaterials(Positions); }))
			{
				SetFailureReason(TerrainContractFailureReason);
				return false;
			}
		}

		SetResolvedSiteTerrainFitDiagnostic(
			SiteRecord,
			FrozenTerrainContractOverride->DiagnosticKind);
		if (SiteRealizationWritePlan.ChunkWriteBatch.TerrainWriteCount == 0
			&& ContractId.IsNone() == false)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] Realization terrain shaping: no terrain writes authorized by frozen contract %s (terrainWrites=0)."),
				*ContractId.ToString());
		}
		if (NormalizedSolvedPayload.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
			&& (NormalizedSolvedPayload.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::OrdinaryRoot
				|| ShouldApplySteppedOrdinaryRootTerrainAnchor(NormalizedSolvedPayload.SolveResult)))
		{
			TerrainConformingSiteOffset =
				FrozenTerrainContractOverride->FootprintMinBlockWorldPos - FootprintMinBlockWorldPos;
		}
	}
	else
	{
		SetFailureReason(TEXT("Accepted site realization requires solved-artifact frozen contract provenance."));
		return false;
	}

	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Realization_TemplatePlacements, STAT_PorismLayout_TemplatePlacement);
		for (const FLayoutPlacedModule& Placement : NormalizedSolvedPayload.SolveResult.Placements)
		{
			if (!PlacementRequiresTemplateCarrier(ContentSet, Placement))
			{
				const FLayoutRegionContentEntry* const SourceEntry =
					FindPlacementSourceEntry(ContentSet, Placement);
				const FString SkipReason = SourceEntry == nullptr
					? TEXT("missing_source_entry_or_content_set")
					: TEXT("non_template_content_kind");
				// Terrain-only writes have no structural bundles to place.
				if (ContentSet == nullptr)
				{
					continue;
				}
				SetFailureReason(FString::Printf(
					TEXT("Realization write plan rejected skipped template placement at cell=%s sourceEntry=%s reason=%s."),
					*Placement.Cell.ToString(),
					*Placement.SourceContentEntryId.ToString(),
					*SkipReason));
				return false;
			}

		const FLayoutRealizationTemplatePlacementEntry* WritePlanTemplateEntry = nullptr;
		{
			FString WritePlanPlacementFailureReason;
			if (!LayoutRealizationWritePlan::ValidateTemplatePlacementWrite(
					SiteRealizationWritePlan,
					Placement,
					SiteRealizationExecutionState,
					WritePlanPlacementFailureReason))
			{
				SetFailureReason(WritePlanPlacementFailureReason);
				return false;
			}
			for (int32 EntryIndex = 0; EntryIndex < SiteRealizationWritePlan.TemplatePlacements.Entries.Num(); ++EntryIndex)
			{
				const FLayoutRealizationTemplatePlacementEntry& Entry =
					SiteRealizationWritePlan.TemplatePlacements.Entries[EntryIndex];
				if (Entry.Cell == Placement.Cell
					&& Entry.ModuleSnapshotId == Placement.ModuleSnapshotId
					&& Entry.YawRotationSteps == Placement.YawRotationSteps)
				{
					WritePlanTemplateEntry = &Entry;
					break;
				}
			}
		}

		if (WritePlanTemplateEntry == nullptr)
		{
			SetFailureReason(TEXT("Accepted template placement has no write-plan entry."));
			return false;
		}
		const FIntVector AnchorBlockWorldPos = WritePlanTemplateEntry->AcceptedAnchorBlockWorldPos;
		FString PlacementFailureReason;
		if (!TryPlaceSolvedPlacementBundle(
			ChunkWorld,
			Placement,
			AnchorBlockWorldPos,
			SharedCellSizeInBlocks,
			PlacementFailureReason))
		{
			UE_LOG(LogTemp, Verbose, TEXT("[LayoutPipeline] Realization placement failed at cell=%s anchor=%s yaw=%d template=%s source=%s: %s"),
				*Placement.Cell.ToString(), *AnchorBlockWorldPos.ToString(),
				Placement.YawRotationSteps * 90,
				Placement.Module != nullptr ? *GetNameSafe(Placement.Module.Get()) : *GetNameSafe(Placement.CompositeModule.Get()),
				*Placement.SourceContentEntryId.ToString(),
				*PlacementFailureReason);
			SetFailureReason(FString::Printf(
				TEXT("Failed to realize placement at cell=%s anchor=%s. %s"),
				*Placement.Cell.ToString(),
				*AnchorBlockWorldPos.ToString(),
				*PlacementFailureReason));
			return false;
		}

		if (GetDetailedDiagnostics())
		{
			UE_LOG(
				LogTemp,
				Log,
				TEXT("Layout realization site %s placement cell=%s asset=%s yawSteps=%d yawDegrees=%d anchor=%s sharedCellSize=%s selectedSnapshot=%s sourceEntry=%s frozenTemplate=%s."),
				*LocationMetadata.SiteCenterBlockWorldPos.ToString(),
				*Placement.Cell.ToString(),
				Placement.Module != nullptr
					? *GetNameSafe(Placement.Module.Get())
					: *GetNameSafe(Placement.CompositeModule.Get()),
				Placement.YawRotationSteps,
				Placement.YawRotationSteps * 90,
				*AnchorBlockWorldPos.ToString(),
				*SharedCellSizeInBlocks.ToString(),
				*Placement.ModuleSnapshotId.ToString(),
				*Placement.SourceContentEntryId.ToString(),
				*Placement.TemplatePath.ToString());
		}
	}
	}

	// Preserve the realized footprint anchor after stamping succeeds so downstream anchor
	// reconstruction follows the terrain-conformed world location without double-applying it.
	FResolvedLayoutSiteLocationMetadata RealizedLocationMetadata = SiteRecord.GetResolvedSiteLocationMetadata();
	RealizedLocationMetadata.RealizedFootprintMinBlockWorldPos =
		FootprintMinBlockWorldPos + TerrainConformingSiteOffset;
	SiteRecord.SetResolvedSiteLocationMetadata(RealizedLocationMetadata);

	UE_LOG(LogTemp, Log, TEXT("[LayoutPipeline] Realization complete: site=%s placements=%d activeCells=%d"),
		*LocationMetadata.SiteCenterBlockWorldPos.ToString(),
		NormalizedSolvedPayload.SolveResult.Placements.Num(),
		SiteRealizationWritePlan.ActiveCells.Num());

	// Mark all stamped chunk origins after all writes succeed so future sites
	// can detect cross-site conflicts before mutating the same chunk.
	{
		const TSet<FIntVector> RequiredChunkOrigins = CollectRequiredChunkOrigins(SiteRecord);
		const FLayoutId ArtifactId = SiteRecord.SolvedArtifactId;
		const FLayoutId RootSolveId = SiteRecord.GetRootPublicationMetadata().RootSolveId;
		MarkRequiredChunkOriginsStamped(RequiredChunkOrigins, ArtifactId, RootSolveId);
	}

	return true;
}

bool UChunkWorldLayoutRuntimeComponent::TryRealizeConnector(
	FResolvedLayoutConnectorRecord& ConnectorRecord,
	FString* OutFailureReason,
	const FLayoutFrozenTerrainContract* FrozenTerrainContractOverride)
{
	auto SetFailureReason = [OutFailureReason](const FString& Message)
	{
		if (OutFailureReason != nullptr)
		{
			*OutFailureReason = Message;
		}
	};

	AChunkWorldExtended* const ChunkWorld = GetOwningChunkWorld();
	const FLayoutResolvedConnectorSolveSourceSelection SolveSourceSelection =
		ConnectorRecord.GetResolvedConnectorSolveSourceSelection();
	ULayoutRegionContentSetAsset* const ContentSet =
		ResolveResidentOrLoadSoftObject(SolveSourceSelection.ContentSet);
	ULayoutProfileAsset* const LayoutProfile =
		ResolveResidentOrLoadSoftObject(SolveSourceSelection.LayoutProfile);
	// Explicit continuation results cross a worker-safe boundary, so restore template carriers before write-plan validation and stamping.
	for (FLayoutPlacedModule& Placement : ConnectorRecord.SolveResult.Placements)
	{
		RehydratePlacementAssetCarrierFromContentSet(ContentSet, Placement);
	}
	const ULayoutWorldBindingAsset* const WorldBinding =
		ResolveRuntimeWorldBindingForConnectorRecord(LayoutWorldBindings, ConnectorRecord);
	NormalizeRuntimeWorldBindingMetrics(
		ConnectorRecord,
		WorldBinding,
		ContentSet);
	const FIntVector SharedCellSizeInBlocks =
		ConnectorRecord.SolveResult.SharedCellSizeInBlocks;
	if (ChunkWorld == nullptr || SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		SetFailureReason(FString::Printf(
			TEXT("ChunkWorld=%s LayoutProfile=%s SharedCellSize=%s ContentSet=%s."),
			*GetNameSafe(ChunkWorld),
			*GetNameSafe(LayoutProfile),
			*SharedCellSizeInBlocks.ToString(),
			*GetNameSafe(ContentSet)));
		return false;
	}

	FLayoutRealizationWritePlan ConnectorRealizationWritePlan;
	FLayoutRealizationWritePlanExecutionState ConnectorRealizationExecutionState;
	bool bHasConnectorRealizationWritePlan = false;
	SetResolvedConnectorTerrainFitDiagnostic(
		ConnectorRecord,
		ELayoutWorldBindingTerrainFitDiagnosticKind::None);
	if (RequiresNormalizedContinuationPlacementCarrier(ConnectorRecord)
		&& !HasNormalizedContinuationPlacementCarrier(ConnectorRecord))
	{
		SetFailureReason(
			TEXT("Continuation connector realization requires a normalized continuation placement carrier; stale continuation-policy placement fallback is not accepted."));
		return false;
	}

	if (ConnectorRecord.SolvedArtifactId.IsNone()
		|| ConnectorRecord.SolveResult.Placements.IsEmpty())
	{
		SetFailureReason(TEXT("Connector realization requires accepted solved-artifact placement metadata before template writes."));
		return false;
	}

	if (ConnectorRecord.SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::None)
	{
		ConnectorRecord.SolveResult.RootPlacementKind =
			ResolveRuntimeConnectorPlacementKind(ConnectorRecord);
	}

	if ((ConnectorRecord.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::OrdinaryRoot
			&& ConnectorRecord.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None)
		|| ConnectorRecord.SolveResult.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks == 0
		|| ConnectorRecord.SolveResult.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ == 0
		|| ConnectorRecord.SolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing <= 0)
	{
		ConnectorRecord.SolveResult.WorldBindingPlacementPolicy =
			ResolveRuntimeConnectorPlacementPolicy(ConnectorRecord);
	}

	if (ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel == INDEX_NONE
		&& ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel != INDEX_NONE)
	{
		ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel =
			ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel;
	}

	if (FrozenTerrainContractOverride == nullptr || FrozenTerrainContractOverride->ContractId.IsNone())
	{
		SetFailureReason(TEXT("Accepted connector realization requires frozen terrain-contract provenance."));
		return false;
	}
	FString RealizationInputFailureReason;
	if (!LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Connector,
			ConnectorRecord.SolvedArtifactId,
			ConnectorRecord.SolvedArtifactActiveCellCount,
			ConnectorRecord.SolveResult,
			*FrozenTerrainContractOverride,
			ConnectorRealizationWritePlan,
			RealizationInputFailureReason))
	{
		SetFailureReason(RealizationInputFailureReason);
		return false;
	}
	// Revalidate against roots accepted since route preparation, before any native writes.
	FString ClearanceFailure;
	if (!FLayoutConnectorPlanning::ValidateContinuationClearance(
		ConnectorRecord.PathOriginBlockWorldPos, SharedCellSizeInBlocks,
		ConnectorRecord.SolveResult.PlannedCells, CaptureContinuationRootFootprints(), ClearanceFailure))
	{
		SetFailureReason(ClearanceFailure);
		return false;
	}
	bHasConnectorRealizationWritePlan = true;
	FString TerrainContractFailureReason;
	if (!LayoutRealizationWritePlan::ApplyTerrainWriteReplay(
				ChunkWorld,
				ConnectorRealizationWritePlan,
				*FrozenTerrainContractOverride,
				ConnectorRealizationExecutionState,
				TerrainContractFailureReason,
				[this](const TArray<FIntVector>& Positions) { return ReadLoadedTerrainMaterials(Positions); }))
	{
		SetFailureReason(TerrainContractFailureReason);
		return false;
	}
	SetResolvedConnectorTerrainFitDiagnostic(
		ConnectorRecord,
		FrozenTerrainContractOverride->DiagnosticKind);

	for (const FLayoutPlacedModule& Placement : ConnectorRecord.SolveResult.Placements)
	{
		const FLayoutRealizationTemplatePlacementEntry* WritePlanTemplateEntry = nullptr;
		if (bHasConnectorRealizationWritePlan)
		{
			FString WritePlanPlacementFailureReason;
			if (!LayoutRealizationWritePlan::ValidateTemplatePlacementWrite(
					ConnectorRealizationWritePlan,
					Placement,
					ConnectorRealizationExecutionState,
					WritePlanPlacementFailureReason))
			{
				SetFailureReason(WritePlanPlacementFailureReason);
				return false;
			}
			WritePlanTemplateEntry = ConnectorRealizationWritePlan.TemplatePlacements.Entries.FindByPredicate(
				[&Placement](const FLayoutRealizationTemplatePlacementEntry& Entry)
				{
					return Entry.Cell == Placement.Cell
						&& Entry.ModuleSnapshotId == Placement.ModuleSnapshotId
						&& Entry.YawRotationSteps == Placement.YawRotationSteps;
				});
		}

		if (WritePlanTemplateEntry == nullptr)
		{
			SetFailureReason(TEXT("Connector realization write plan lost an accepted template placement anchor."));
			return false;
		}
		const FIntVector AnchorBlockWorldPos = WritePlanTemplateEntry->AcceptedAnchorBlockWorldPos;

		FString PlacementFailureReason;
		if (!TryPlaceSolvedPlacementBundle(
			ChunkWorld,
			Placement,
			AnchorBlockWorldPos,
			SharedCellSizeInBlocks,
			PlacementFailureReason))
		{
			SetFailureReason(FString::Printf(
				TEXT("Failed to realize connector placement at cell=%s anchor=%s. %s"),
				*Placement.Cell.ToString(),
				*AnchorBlockWorldPos.ToString(),
				*PlacementFailureReason));
			return false;
		}
	}

	return true;
}

AChunkWorldExtended* UChunkWorldLayoutRuntimeComponent::GetOwningChunkWorld() const
{
	return Cast<AChunkWorldExtended>(GetOwner());
}
