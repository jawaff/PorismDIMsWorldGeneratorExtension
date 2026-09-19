// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Async/Async.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	const FLayoutId ConnectorPlacementPolicyId(TEXT("ConnectorExplicit"));
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");
	constexpr float ConnectorSolveBudgetTimeoutSeconds = 1.0e-6f;

	UWorldGenDef* CreateConnectorWorldGenDef(UObject* const Outer)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(Outer);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		// An absent global node defaults to positive (air); fixtures need zero additive density.
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		return WorldGenDef;
	}

	ELayoutWorldBindingPlacementKind ResolveContinuationPlacementKindForTest(
		const ELayoutWorldBindingContinuationFamilyType FamilyType)
	{
		switch (FamilyType)
		{
		case ELayoutWorldBindingContinuationFamilyType::SurfacePath:
			return ELayoutWorldBindingPlacementKind::SurfacePath;
		case ELayoutWorldBindingContinuationFamilyType::BridgeContinuation:
			return ELayoutWorldBindingPlacementKind::BridgeContinuation;
		case ELayoutWorldBindingContinuationFamilyType::TunnelContinuation:
			return ELayoutWorldBindingPlacementKind::TunnelContinuation;
		default:
			return ELayoutWorldBindingPlacementKind::None;
		}
	}

	FLayoutWorldBindingPlacementPolicy BuildNormalizedContinuationPlacementPolicyForTest(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamily& Family)
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy =
			WorldBinding != nullptr
				? WorldBinding->DefaultPlacementPolicy
				: FLayoutWorldBindingPlacementPolicy();
		if (Family.bOverrideTerrainTransitionPolicy)
		{
			PlacementPolicy.TerrainTransition =
				Family.TerrainTransitionPolicyOverride;
		}
		return PlacementPolicy;
	}

	/** Builds profile-owned continuation content with rotatable Entry and route support. */
	ULayoutRegionContentSetAsset* CreateContinuationContentSet(const FName FamilyId)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutTemplate_%s_Continuation"), *FamilyId.ToString()),
			FIntVector(16, 16, 16));
		ULayoutModuleAsset* Module = CreateModule(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutModule_%s_Continuation"), *FamilyId.ToString()),
			Template,
			{
				ELayoutCellIntent::Boundary,
				ELayoutCellIntent::Entry,
				ELayoutCellIntent::Core,
				ELayoutCellIntent::Interior,
				ELayoutCellIntent::Connector,
				ELayoutCellIntent::VerticalAccess
			},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = *FString::Printf(TEXT("%sContinuationEntry"), *FamilyId.ToString());
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		return CreateRegionContentSet(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutContentSet_%s_Continuation"), *FamilyId.ToString()),
			{Entry});
	}

	void PopulateConnectorRootPublicationMetadataForTesting(
		FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutRootPublicationMetadata ExistingMetadata =
			ConnectorRecord.GetRootPublicationMetadata();
		if (!ExistingMetadata.RootPlacementPolicyId.IsNone()
			&& !ExistingMetadata.RootCandidateId.IsNone()
			&& !ExistingMetadata.RootSolveId.IsNone())
		{
			return;
		}

		const FString RegionDebugPath = FString::Printf(
			TEXT("Connector/%s/%s"),
			*ConnectorRecord.StartEndpointBlockWorldPos.ToString(),
			*ConnectorRecord.EndEndpointBlockWorldPos.ToString());
		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootPlacementPolicyId = ConnectorPlacementPolicyId;
		PublicationMetadata.RootCandidateId = FLayoutId(*RegionDebugPath);
		PublicationMetadata.RootSolveId = FLayoutId(*RegionDebugPath);
		ConnectorRecord.SetRootPublicationMetadata(PublicationMetadata);
	}


	ULayoutWorldBindingAsset* CreateContinuationWorldBinding(
		ULayoutProfileAsset* ConnectorProfile,
		const FName BindingId,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag)
	{
		ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutWorldBinding_%s"), *FamilyId.ToString()));
		WorldBinding->BindingId = BindingId;
		WorldBinding->BiomeRowNames = {TEXT("Reservation")};
		WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);

		FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
		RootCandidate.CandidateId = TEXT("RootCandidate");
		RootCandidate.LayoutProfile = CreateProfile(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutProfile_%s_Root"), *FamilyId.ToString()),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			1,
			false);
		ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutContentSet_%s_Root"), *FamilyId.ToString()),
			{});
		RootCandidate.LayoutProfile->ContentSet = RootContentSet;
		RootCandidate.Weight = 1;

		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = FamilyId;
		Family.FamilyType = FamilyType;
		Family.EndpointConnectorTypeTag = EndpointConnectorTypeTag;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 16;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("%sCandidate"), *FamilyId.ToString());
		if (ConnectorProfile != nullptr && ConnectorProfile->ContinuationEntryLevel == INDEX_NONE)
		{
			ConnectorProfile->ContinuationEntryLevel = 0;
		}
		if (ConnectorProfile != nullptr && ConnectorProfile->ContentSet == nullptr)
		{
			ConnectorProfile->ContentSet = CreateContinuationContentSet(FamilyId);
		}
		FamilyCandidate.LayoutProfile = ConnectorProfile;
		FamilyCandidate.Weight = 1;
		return WorldBinding;
	}

	bool RunFiniteCenteredZHigherEntrySteppedConnectorRequestOnActivePath(
		FAutomationTestBase& Test,
		const FString& FamilyLabel,
		const TCHAR* ProfileName,
		const TCHAR* BindingName,
		const TCHAR* CandidateName,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag& ConnectorTypeTag,
		const int32 SolveSeed,
		const int32 EndEndpointX = 32,
		const bool bRequireMultipleSegments = false)
	{
		ULayoutProfileAsset* ConnectorProfile = CreateProfile(
			GetTransientPackage(),
			ProfileName,
			FIntPoint(3, 3),
			FIntPoint(7, 7),
			2,
			2,
			false);
		ConnectorProfile->bSupportsSteppedTerrainSolve = true;
		ConnectorProfile->ContinuationEntryLevel = 1;

		ULayoutWorldBindingAsset* WorldBinding = CreateContinuationWorldBinding(
			ConnectorProfile,
			BindingName,
			CandidateName,
			FamilyType,
			PlacementKind,
			ConnectorTypeTag);
		WorldBinding->BiomeRowNames = {TEXT("ConnectorTerrain")};
		WorldBinding->DefaultPlacementPolicy.HeightIgnoreThreshold = 1;
		WorldBinding->ContinuationFamilies[0].ContinuationPolicy.MaxSlopeBlocks = 64;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;

		AChunkWorldExtended* ChunkWorld = NewObject<AChunkWorldExtended>();
		UWorldGenDef* const WorldGenDef = CreateConnectorWorldGenDef(ChunkWorld);
		WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
		WorldGenDef->NoiseCoordinateOffset = FIntVector(0, 0, -128);
		FBiomeDualData Row;
		Row.BiomeName = TEXT("ConnectorTerrain");
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		// Terrain ownership requires a contributing biome, not a noise-only row.
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
		WorldGenDef->WorldBiomes.Add(Row);
		ChunkWorld->WorldGenDef = WorldGenDef;

		LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z higher-entry stepped %s connector request fixture resolves finite chunk-world bounds"),
					*FamilyLabel),
				LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
					ChunkWorld,
					Bounds)))
		{
			return false;
		}

		FLayoutActiveBiomeSampler ActiveBiomeSampler;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z higher-entry stepped %s connector sampler initializes"),
					*FamilyLabel),
				ActiveBiomeSampler.Initialize(GetTransientPackage(), WorldGenDef, 0)))
		{
			return false;
		}

		FLayoutConnectorTerrainPathContext TerrainContext;
		TerrainContext.ActiveBiomeSampler = &ActiveBiomeSampler;
		TerrainContext.CoordinateSettings.BaseBlockSize = WorldGenDef->BaseBlockSize;
		TerrainContext.CoordinateSettings.NoiseScale = WorldGenDef->NoiseScale;
		TerrainContext.CoordinateSettings.NoiseCoordinateOffset = WorldGenDef->NoiseCoordinateOffset;

		FResolvedLayoutConnectorRecord ConnectorRecord;
		ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
		ConnectorRecord.BiomeRowName = TEXT("ConnectorTerrain");
		ConnectorRecord.ContinuationFamilyId = WorldBinding->ContinuationFamilies[0].FamilyId;
		ConnectorRecord.ContinuationFamilyCandidateId =
			WorldBinding->ContinuationFamilies[0].Candidates[0].CandidateId;
		ConnectorRecord.PlacementKind = PlacementKind;
		ConnectorRecord.ResolvedContinuationSelection.FamilyId =
			WorldBinding->ContinuationFamilies[0].FamilyId;
		ConnectorRecord.ResolvedContinuationSelection.PlacementKind = PlacementKind;
		ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;
		ConnectorRecord.TerrainPathSelection.bEnableTerrainAwarePathing = true;
		ConnectorRecord.TerrainPathSelection.PathBiomeRowName = TEXT("ConnectorTerrain");
		ConnectorRecord.TerrainPathSelection.PathBiomeRowNames = {TEXT("ConnectorTerrain")};
		ConnectorRecord.WorldBindingPlacementPolicy = BuildNormalizedContinuationPlacementPolicyForTest(WorldBinding, WorldBinding->ContinuationFamilies[0]);
		ConnectorRecord.ContinuationPolicy =
			WorldBinding->ContinuationFamilies[0].ContinuationPolicy;
		ConnectorRecord.SolveBudget =
			WorldBinding->ContinuationFamilies[0].SolveBudget;
		ConnectorRecord.FrontendSharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
		ConnectorRecord.FrontendTemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
		ConnectorRecord.LayoutProfile =
			TSoftObjectPtr<ULayoutProfileAsset>(ConnectorProfile);
		ConnectorRecord.StartEndpointBlockWorldPos = FIntVector(0, 0, 40);
		ConnectorRecord.EndEndpointBlockWorldPos = FIntVector(EndEndpointX, 0, 40);
		ConnectorRecord.SolveSeed = SolveSeed;
		PopulateConnectorRootPublicationMetadataForTesting(ConnectorRecord);

		FLayoutPreparedContinuationRoute PreparedRoute;
		FString FailureReason;
		const bool bBuilt = FLayoutConnectorPlanning::TryPrepareContinuationRoute(
			ConnectorRecord,
			WorldBinding,
			SolveSeed,
			&TerrainContext,
			PreparedRoute,
			FailureReason);
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z higher-entry stepped %s connector route assembly accepts terrain-aware stepped support"),
					*FamilyLabel),
				bBuilt))
		{
			Test.AddError(FailureReason);
			return false;
		}
		if (FamilyType == ELayoutWorldBindingContinuationFamilyType::SurfacePath && EndEndpointX == 32)
		{
			FLayoutRootSpacingReservation Blocker;
			Blocker.Min = FIntPoint(8, -8);
			Blocker.Max = FIntPoint(24, 8);
			TerrainContext.RootFootprints.Add(Blocker);
			FLayoutPreparedContinuationRoute BlockedRoute;
			Test.TestFalse(TEXT("Shared route preparation rejects intervening root without a detour"),
				FLayoutConnectorPlanning::TryPrepareContinuationRoute(ConnectorRecord, WorldBinding,
					SolveSeed, &TerrainContext, BlockedRoute, FailureReason));
			Test.TestTrue(TEXT("Rejection identifies root footprint"), FailureReason.Contains(TEXT("root layout footprint")));
			TerrainContext.RootFootprints.Reset();

			// A root beside the fixed entry strip must not acquire a second corridor-width halo.
			Blocker.Min = FIntPoint(-8, -40);
			Blocker.Max = FIntPoint(7, -25);
			TerrainContext.RootFootprints.Add(Blocker);
			FLayoutPreparedContinuationRoute AdjacentRoute;
			const bool bAdjacentBuilt = FLayoutConnectorPlanning::TryPrepareContinuationRoute(
				ConnectorRecord, WorldBinding, SolveSeed, &TerrainContext, AdjacentRoute, FailureReason);
			Test.TestTrue(TEXT("Root outside the widened endpoint strip does not double-expand clearance"), bAdjacentBuilt);
			if (!bAdjacentBuilt) Test.AddError(FailureReason);
			TerrainContext.RootFootprints.Reset();
		}
		FLayoutContinuationPlanningSnapshot Captured;
		if (!Test.TestTrue(TEXT("Continuation inputs capture before worker handoff"),
			FLayoutConnectorPlanning::CaptureContinuationPlanningInputs(ConnectorRecord, WorldBinding, SolveSeed, Captured, FailureReason))) return false;
		{
			TGuardValue<TArray<FLayoutWorldBindingContinuationFamily>> FamiliesGuard(WorldBinding->ContinuationFamilies, {});
			TGuardValue<FIntPoint> BoundsGuard(ConnectorProfile->MaximumFootprintInCells, FIntPoint::ZeroValue);
			FLayoutPreparedContinuationRoute WorkerRoute;
			const bool bWorkerBuilt = Async(EAsyncExecution::ThreadPool, [&]()
			{
				return FLayoutConnectorPlanning::TryPrepareContinuationRoute(
					ConnectorRecord, Captured, SolveSeed, &TerrainContext, WorkerRoute, FailureReason);
			}).Get();
			Test.TestTrue(TEXT("Worker route uses captured inputs after authored family/bounds change"), bWorkerBuilt);
			Test.TestEqual(TEXT("Worker preserves descriptor count"), WorkerRoute.Segments.Num(), PreparedRoute.Segments.Num());
			for (int32 Index = 0; Index < WorkerRoute.Segments.Num() && Index < PreparedRoute.Segments.Num(); ++Index)
			{
				const auto& Before = PreparedRoute.Segments[Index];
				const auto& After = WorkerRoute.Segments[Index];
				Test.TestEqual(TEXT("Worker preserves candidate order"), After.Descriptor.CandidateId, Before.Descriptor.CandidateId);
				Test.TestEqual(TEXT("Worker preserves segment outcome"), After.PreparedContinuation.IsSet(), Before.PreparedContinuation.IsSet());
				if (Before.PreparedContinuation.IsSet() && After.PreparedContinuation.IsSet())
				{
					Test.TestEqual(TEXT("Worker preserves segment seed"), After.PreparedContinuation->SolveRequest.Seed, Before.PreparedContinuation->SolveRequest.Seed);
					Test.TestEqual(TEXT("Worker preserves request identity"), After.PreparedContinuation->SolveRequest.EffectiveSnapshotId, Before.PreparedContinuation->SolveRequest.EffectiveSnapshotId);
				}
			}
		}
		if (bRequireMultipleSegments)
		{
			if (!Test.TestTrue(
					TEXT("Long continuation route prepares multiple bounded descriptors"),
					PreparedRoute.Segments.Num() > 1))
			{
				return false;
			}
			TSet<FIntPoint> OccupiedWorldColumns;
			for (int32 SegmentIndex = 1; SegmentIndex < PreparedRoute.Segments.Num(); ++SegmentIndex)
			{
				Test.TestEqual(
					FString::Printf(TEXT("Adjacent descriptors %d and %d face across their seam"), SegmentIndex - 1, SegmentIndex),
					PreparedRoute.Segments[SegmentIndex - 1].Descriptor.EgressFaceDirection,
					FLayoutDirectionUtils::GetOpposite(PreparedRoute.Segments[SegmentIndex].Descriptor.IngressFaceDirection));
			}
			for (const FLayoutPreparedContinuationRouteSegment& Segment : PreparedRoute.Segments)
			{
				if (!Segment.PreparedContinuation.IsSet())
				{
					continue;
				}
				const FLayoutPreparedContinuation& Prepared = Segment.PreparedContinuation.GetValue();
				FLayoutRegionSolveRequest AdapterRequest = Prepared.SolveRequest;
				FString AdapterFailureReason;
				if (!Test.TestTrue(
						FString::Printf(TEXT("Descriptor %d accepts frozen endpoint height contracts: %s"), Segment.Descriptor.SegmentIndex, *AdapterFailureReason),
						FLayoutContractPipeline::TryPrecomputeAdapterOutput(
							AdapterRequest,
							AdapterFailureReason)))
				{
					return false;
				}
				const FIntVector SharedCellSize =
					Prepared.ConnectorRecord.GetResolvedConnectorFrontendSelection().SharedCellSizeInBlocks;
				const FIntVector RouteGridOrigin = ConnectorRecord.StartEndpointBlockWorldPos - FIntVector(
					SharedCellSize.X / 2,
					SharedCellSize.Y / 2,
					SharedCellSize.Z / 2);
				Test.TestEqual(
					FString::Printf(TEXT("Descriptor %d uses route-grid footprint origin on X"), Segment.Descriptor.SegmentIndex),
					Prepared.PathOriginBlockWorldPos.X,
					RouteGridOrigin.X + Prepared.MinPlannedCell.X * SharedCellSize.X + SharedCellSize.X / 2);
				Test.TestEqual(
					FString::Printf(TEXT("Descriptor %d uses route-grid footprint origin on Y"), Segment.Descriptor.SegmentIndex),
					Prepared.PathOriginBlockWorldPos.Y,
					RouteGridOrigin.Y + Prepared.MinPlannedCell.Y * SharedCellSize.Y + SharedCellSize.Y / 2);
				for (const FLayoutPlannedCell& PlannedCell : Prepared.PlannedCells)
				{
					const FIntPoint WorldColumn(
						Prepared.PathOriginBlockWorldPos.X + PlannedCell.Cell.X * SharedCellSize.X,
						Prepared.PathOriginBlockWorldPos.Y + PlannedCell.Cell.Y * SharedCellSize.Y);
					Test.TestFalse(
						FString::Printf(TEXT("Descriptor %d does not duplicate stamped seam column (%d,%d)"), Segment.Descriptor.SegmentIndex, WorldColumn.X, WorldColumn.Y),
						OccupiedWorldColumns.Contains(WorldColumn));
					OccupiedWorldColumns.Add(WorldColumn);
				}
			}
			return true;
		}
		const FLayoutPreparedContinuationRouteSegment* const PreparedSegment =
			PreparedRoute.Segments.FindByPredicate([](const FLayoutPreparedContinuationRouteSegment& Segment)
			{
				return Segment.PreparedContinuation.IsSet();
			});
		if (!Test.TestTrue(TEXT("Segmented continuation route retains one prepared segment"), PreparedSegment != nullptr))
		{
			for (const FLayoutPreparedContinuationRouteSegment& Segment : PreparedRoute.Segments)
			{
				Test.AddError(FString::Printf(TEXT("Segment %d preparation failed: %s"), Segment.Descriptor.SegmentIndex, *Segment.FailureReason));
			}
			return false;
		}
		const FLayoutRegionSolveRequest& SolveRequest = PreparedSegment->PreparedContinuation.GetValue().SolveRequest;
		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector keeps its source approach straight"),
				*FamilyLabel),
			PreparedRoute.Route.CenterlineCells[1] - PreparedRoute.Route.CenterlineCells[0],
			FIntPoint(1, 0));
		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector keeps its target approach straight"),
				*FamilyLabel),
			PreparedRoute.Route.CenterlineCells.Last() - PreparedRoute.Route.CenterlineCells[PreparedRoute.Route.CenterlineCells.Num() - 2],
			FIntPoint(-1, 0));

		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector request keeps the resolved continuation entry level"),
				*FamilyLabel),
			SolveRequest.RootContinuationSelection.ResolvedEntryLevel,
			1);
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector request preserves stepped support samples"),
				*FamilyLabel),
			!SolveRequest.SteppedTerrainSupportMap.SupportSamples.IsEmpty());
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector request freezes terrain adapter evidence"),
				*FamilyLabel),
			SolveRequest.bHasFrozenTerrainBiomeAdapterInput);
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector request freezes footprint classification"),
				*FamilyLabel),
			SolveRequest.FrozenTerrainBiomeAdapterInput.bHasFootprintClassificationEvidence
				&& !SolveRequest.FrozenTerrainBiomeAdapterInput.FootprintClassification.CellClassifications.IsEmpty());
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector request keeps one planned cell on the raised continuation deck"),
				*FamilyLabel),
			SolveRequest.PlannedCells.ContainsByPredicate(
				[](const FLayoutPlannedCell& Cell)
				{
					return Cell.Cell.Z == 1;
				}));
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry stepped %s connector request keeps one support sample inside the finite Z chunk span"),
				*FamilyLabel),
			SolveRequest.SteppedTerrainSupportMap.SupportSamples.ContainsByPredicate(
				[&Bounds](const FLayoutSteppedTerrainSupportSample& Sample)
				{
					return LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
						Bounds,
						FIntVector(0, 0, Sample.SupportSurfaceZ));
				}));
		return true;
	}

	bool RunFiniteCenteredZHigherEntryConnectorRequestOnActivePath(
		FAutomationTestBase& Test,
		const FString& FamilyLabel,
		const TCHAR* ProfileName,
		const TCHAR* BindingName,
		const TCHAR* CandidateName,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag& ConnectorTypeTag,
		const int32 SolveSeed,
		const int32 ContinuationEndpointTransitionDepth)
	{
		ULayoutProfileAsset* ConnectorProfile = CreateProfile(
			GetTransientPackage(),
			ProfileName,
			FIntPoint(3, 3),
			FIntPoint(7, 7),
			2,
			2,
			false);
		ConnectorProfile->ContinuationEntryLevel = 1;

		ULayoutWorldBindingAsset* WorldBinding = CreateContinuationWorldBinding(
			ConnectorProfile,
			BindingName,
			CandidateName,
			FamilyType,
			PlacementKind,
			ConnectorTypeTag);
		WorldBinding->BiomeRowNames = {TEXT("ConnectorTerrain")};
		WorldBinding->ContinuationFamilies[0].ContinuationPolicy.MaxSlopeBlocks = 64;
		WorldBinding->ContinuationFamilies[0].ContinuationPolicy.MaxBridgeGapCells = 4;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
		if (PlacementKind != ELayoutWorldBindingPlacementKind::SurfacePath)
		{
			WorldBinding->ContinuationFamilies[0].bOverrideTerrainTransitionPolicy = true;
		}

		AChunkWorldExtended* ChunkWorld = NewObject<AChunkWorldExtended>();
		UWorldGenDef* const WorldGenDef = CreateConnectorWorldGenDef(ChunkWorld);
		WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
		WorldGenDef->NoiseCoordinateOffset = FIntVector(0, 0, -128);
		FBiomeDualData Row;
		Row.BiomeName = TEXT("ConnectorTerrain");
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		// Terrain ownership requires a contributing biome, not a noise-only row.
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
		WorldGenDef->WorldBiomes.Add(Row);
		ChunkWorld->WorldGenDef = WorldGenDef;

		LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z higher-entry %s connector request fixture resolves finite chunk-world bounds"),
					*FamilyLabel),
				LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
					ChunkWorld,
					Bounds)))
		{
			return false;
		}

		FLayoutActiveBiomeSampler ActiveBiomeSampler;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z higher-entry %s connector sampler initializes"),
					*FamilyLabel),
				ActiveBiomeSampler.Initialize(GetTransientPackage(), WorldGenDef, 0)))
		{
			return false;
		}

		FLayoutConnectorTerrainPathContext TerrainContext;
		TerrainContext.ActiveBiomeSampler = &ActiveBiomeSampler;
		TerrainContext.CoordinateSettings.BaseBlockSize = WorldGenDef->BaseBlockSize;
		TerrainContext.CoordinateSettings.NoiseScale = WorldGenDef->NoiseScale;
		TerrainContext.CoordinateSettings.NoiseCoordinateOffset = WorldGenDef->NoiseCoordinateOffset;

		FResolvedLayoutConnectorRecord ConnectorRecord;
		ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
		ConnectorRecord.BiomeRowName = TEXT("ConnectorTerrain");
		ConnectorRecord.ContinuationFamilyId = WorldBinding->ContinuationFamilies[0].FamilyId;
		ConnectorRecord.ContinuationFamilyCandidateId =
			WorldBinding->ContinuationFamilies[0].Candidates[0].CandidateId;
		ConnectorRecord.PlacementKind = PlacementKind;
		ConnectorRecord.ResolvedContinuationSelection.FamilyId =
			WorldBinding->ContinuationFamilies[0].FamilyId;
		ConnectorRecord.ResolvedContinuationSelection.PlacementKind = PlacementKind;
		ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;
		ConnectorRecord.TerrainPathSelection.bEnableTerrainAwarePathing = true;
		ConnectorRecord.TerrainPathSelection.PathBiomeRowName = TEXT("ConnectorTerrain");
		ConnectorRecord.TerrainPathSelection.PathBiomeRowNames = {TEXT("ConnectorTerrain")};
		ConnectorRecord.WorldBindingPlacementPolicy = BuildNormalizedContinuationPlacementPolicyForTest(WorldBinding, WorldBinding->ContinuationFamilies[0]);
		ConnectorRecord.ContinuationPolicy =
			WorldBinding->ContinuationFamilies[0].ContinuationPolicy;
		ConnectorRecord.SolveBudget =
			WorldBinding->ContinuationFamilies[0].SolveBudget;
		ConnectorRecord.FrontendSharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
		ConnectorRecord.FrontendTemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
		ConnectorRecord.LayoutProfile =
			TSoftObjectPtr<ULayoutProfileAsset>(ConnectorProfile);
		ConnectorRecord.StartEndpointBlockWorldPos = FIntVector(0, 0, 40);
		ConnectorRecord.EndEndpointBlockWorldPos = FIntVector(32, 0, 40);
		ConnectorRecord.SolveSeed = SolveSeed;
		PopulateConnectorRootPublicationMetadataForTesting(ConnectorRecord);

		FLayoutPreparedContinuationRoute PreparedRoute;
		FString FailureReason;
		const bool bBuilt = FLayoutConnectorPlanning::TryPrepareContinuationRoute(
			ConnectorRecord,
			WorldBinding,
			SolveSeed,
			&TerrainContext,
			PreparedRoute,
			FailureReason);
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z higher-entry %s connector route assembly accepts terrain-aware continuation support"),
					*FamilyLabel),
				bBuilt))
		{
			Test.AddError(FailureReason);
			return false;
		}

		const FLayoutPreparedContinuationRouteSegment* const PreparedSegment =
			PreparedRoute.Segments.FindByPredicate([](const FLayoutPreparedContinuationRouteSegment& Segment)
			{
				return Segment.PreparedContinuation.IsSet();
			});
		if (!Test.TestTrue(TEXT("Segmented continuation route retains one prepared segment"), PreparedSegment != nullptr))
		{
			for (const FLayoutPreparedContinuationRouteSegment& Segment : PreparedRoute.Segments)
			{
				Test.AddError(FString::Printf(TEXT("Segment %d preparation failed: %s"), Segment.Descriptor.SegmentIndex, *Segment.FailureReason));
			}
			return false;
		}
		const FLayoutRegionSolveRequest& SolveRequest = PreparedSegment->PreparedContinuation.GetValue().SolveRequest;
		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry %s connector request keeps the resolved continuation entry level"),
				*FamilyLabel),
			SolveRequest.RootContinuationSelection.ResolvedEntryLevel,
			1);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry %s connector request keeps the continuation placement kind"),
				*FamilyLabel),
			SolveRequest.RootContinuationSelection.PlacementKind,
			PlacementKind);
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry %s connector request keeps one planned cell on the raised continuation deck"),
				*FamilyLabel),
			SolveRequest.PlannedCells.ContainsByPredicate(
				[](const FLayoutPlannedCell& Cell)
				{
					return Cell.Cell.Z == 1;
				}));
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry %s connector request stays non-stepped on the request carrier"),
				*FamilyLabel),
			SolveRequest.SteppedTerrainSupportMap.SupportSamples.IsEmpty());
				Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z higher-entry %s connector request keeps at least one planned deck cell inside the finite Z chunk span"),
				*FamilyLabel),
			SolveRequest.PlannedCells.ContainsByPredicate(
				[&Bounds](const FLayoutPlannedCell& Cell)
				{
					return LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
						Bounds,
						FIntVector(0, 0, Cell.Cell.Z * 16));
				}));
		return true;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySurfacePathConnectorRequestOnActivePathTest,
	"PorismExtension.Layout.Connectors.BuildsFiniteCenteredZHigherEntrySurfacePathConnectorRequestOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntryBridgeConnectorRequestOnActivePathTest,
	"PorismExtension.Layout.Connectors.BuildsFiniteCenteredZHigherEntryBridgeConnectorRequestOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntryTunnelConnectorRequestOnActivePathTest,
	"PorismExtension.Layout.Connectors.BuildsFiniteCenteredZHigherEntryTunnelConnectorRequestOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySteppedSurfacePathConnectorRequestOnActivePathTest,
	"PorismExtension.Layout.Connectors.BuildsFiniteCenteredZHigherEntrySteppedSurfacePathConnectorRequestOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningSharesFrozenWorldHeightAcrossSegmentSeamsTest,
	"PorismExtension.Layout.Connectors.SharesFrozenWorldHeightAcrossSegmentSeams",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySteppedBridgeConnectorRequestOnActivePathTest,
	"PorismExtension.Layout.Connectors.BuildsFiniteCenteredZHigherEntrySteppedBridgeConnectorRequestOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySteppedTunnelConnectorRequestOnActivePathTest,
	"PorismExtension.Layout.Connectors.BuildsFiniteCenteredZHigherEntrySteppedTunnelConnectorRequestOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutConnectorPlanningSharesFrozenWorldHeightAcrossSegmentSeamsTest::RunTest(const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntrySteppedConnectorRequestOnActivePath(
		*this,
		TEXT("SurfacePath seams"),
		TEXT("LayoutProfile_FiniteCenteredZSegmentSeams"),
		TEXT("FiniteCenteredZSegmentSeamsBinding"),
		TEXT("FiniteCenteredZSegmentSeamsRoad"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		285,
		128,
		true);
}

bool FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySteppedSurfacePathConnectorRequestOnActivePathTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntrySteppedConnectorRequestOnActivePath(
		*this,
		TEXT("SurfacePath"),
		TEXT("LayoutProfile_FiniteCenteredZHigherEntrySteppedSurfacePathConnectorRequest"),
		TEXT("FiniteCenteredZHigherEntrySurfaceBinding"),
		TEXT("FiniteCenteredZHigherEntrySurfaceRoadSteppedRequest"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		282);
}

bool FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySurfacePathConnectorRequestOnActivePathTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryConnectorRequestOnActivePath(
		*this,
		TEXT("SurfacePath"),
		TEXT("LayoutProfile_FiniteCenteredZHigherEntrySurfacePathConnectorRequest"),
		TEXT("FiniteCenteredZHigherEntrySurfaceBinding"),
		TEXT("FiniteCenteredZHigherEntrySurfaceRoadRequest"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		272,
		0);
}

bool FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntryBridgeConnectorRequestOnActivePathTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryConnectorRequestOnActivePath(
		*this,
		TEXT("bridge"),
		TEXT("LayoutProfile_FiniteCenteredZHigherEntryBridgeConnectorRequest"),
		TEXT("FiniteCenteredZHigherEntryBridgeBinding"),
		TEXT("FiniteCenteredZHigherEntryBridgeRoadRequest"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		273,
		4);
}

bool FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntryTunnelConnectorRequestOnActivePathTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryConnectorRequestOnActivePath(
		*this,
		TEXT("tunnel"),
		TEXT("LayoutProfile_FiniteCenteredZHigherEntryTunnelConnectorRequest"),
		TEXT("FiniteCenteredZHigherEntryTunnelBinding"),
		TEXT("FiniteCenteredZHigherEntryTunnelRoadRequest"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		274,
		4);
}

bool FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySteppedBridgeConnectorRequestOnActivePathTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntrySteppedConnectorRequestOnActivePath(
		*this,
		TEXT("bridge"),
		TEXT("LayoutProfile_FiniteCenteredZHigherEntrySteppedBridgeConnectorRequest"),
		TEXT("FiniteCenteredZHigherEntryBridgeBinding"),
		TEXT("FiniteCenteredZHigherEntryBridgeRoadSteppedRequest"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		283);
}

bool FLayoutConnectorPlanningBuildsFiniteCenteredZHigherEntrySteppedTunnelConnectorRequestOnActivePathTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntrySteppedConnectorRequestOnActivePath(
		*this,
		TEXT("tunnel"),
		TEXT("LayoutProfile_FiniteCenteredZHigherEntrySteppedTunnelConnectorRequest"),
		TEXT("FiniteCenteredZHigherEntryTunnelBinding"),
		TEXT("FiniteCenteredZHigherEntryTunnelRoadSteppedRequest"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		284);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningExpandsThreeWideCornerTest,
	"PorismExtension.Layout.Connectors.ExpandsThreeWideCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutConnectorPlanningExpandsThreeWideCornerTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	FIntPoint FootprintSize;
	FIntPoint MinCell;
	const bool bExpanded = FLayoutConnectorPlanning::ExpandContinuationCenterlineForTesting(
		{FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(1, 1)},
		3,
		0,
		PlannedCells,
		FootprintSize,
		MinCell);
	TestTrue(TEXT("Three-wide connector corner expands"), bExpanded);
	TestEqual(TEXT("Three-wide connector corner footprint includes both strips"), FootprintSize, FIntPoint(3, 3));
	TestEqual(TEXT("Three-wide connector corner keeps exactly two entry cells"),
		PlannedCells.FilterByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::Entry;
		}).Num(), 2);
	TestEqual(TEXT("Three-wide connector corner fills full square elbow"), PlannedCells.Num(), 9);
	TestTrue(TEXT("Three-wide connector corner fills outer elbow corner"), PlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(2, 0, 0);
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutConnectorPlanningRoutesPortalTurnTest,
	"PorismExtension.Layout.Connectors.RoutesPortalTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutConnectorPlanningRoutesPortalTurnTest::RunTest(const FString& Parameters)
{
	FLayoutConnectorPathQuery Query;
	Query.StartCell = FIntPoint(0, 1);
	Query.EndCell = FIntPoint(3, 2);
	Query.MinCell = FIntPoint(-1, -1);
	Query.MaxCell = FIntPoint(4, 4);
	TMap<FIntPoint, FLayoutConnectorPathCell> CellsByGrid;
	for (int32 Y = Query.MinCell.Y; Y <= Query.MaxCell.Y; ++Y)
	{
		for (int32 X = Query.MinCell.X; X <= Query.MaxCell.X; ++X)
		{
			FLayoutConnectorPathCell& Cell = CellsByGrid.Add(FIntPoint(X, Y));
			Cell.Type = ELayoutConnectorPathCellType::Walkable;
		}
	}
	TArray<FIntPoint> PathCells;
	if (!TestTrue(
			TEXT("Portal cells route through an open connector grid"),
			FLayoutConnectorPlanning::FindConnectorPathCells(Query, CellsByGrid, PathCells)))
	{
		return false;
	}
	TestEqual(TEXT("Portal route keeps source portal"), PathCells[0], Query.StartCell);
	TestEqual(TEXT("Portal route keeps target portal"), PathCells.Last(), Query.EndCell);
	bool bHasTurn = false;
	for (int32 Index = 2; Index < PathCells.Num(); ++Index)
	{
		bHasTurn |= (PathCells[Index - 1] - PathCells[Index - 2])
			!= (PathCells[Index] - PathCells[Index - 1]);
	}
	TestTrue(TEXT("Offset portal route contains a cardinal turn"), bHasTurn);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutContinuationClearanceTest,
	"PorismExtension.Layout.Connectors.FootprintClearance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContinuationClearanceTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("ClearancePath"),
		FIntPoint(1, 1), FIntPoint(7, 7), 1, 1, false);
	ULayoutWorldBindingAsset* Binding = CreateContinuationWorldBinding(Profile, TEXT("ClearanceBinding"),
		TEXT("ClearanceFamily"), ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath, LayoutGameplayTags::FaceEntry);
	Binding->BiomeRowNames = {TEXT("RootOnlyBiome")};
	FResolvedLayoutConnectorEndpoint Start, End;
	Start.RootRecordKey = TEXT("Start");
	End.RootRecordKey = TEXT("End");
	Start.SiteReservationKey = FIntPoint(0, 0);
	End.SiteReservationKey = FIntPoint(5, 5);
	Start.ConnectorTypeTag = End.ConnectorTypeTag = LayoutGameplayTags::FaceEntry;
	Start.EndpointBlockWorldPos = FIntVector(0, 0, 0);
	End.EndpointBlockWorldPos = FIntVector(50, 50, 0);
	Start.ExposedEntryFaceDirection = ELayoutFaceDirection::PosX;
	End.ExposedEntryFaceDirection = ELayoutFaceDirection::NegY;
	FResolvedLayoutConnectorRecord Record;
	FString Failure;
	auto BuildPair = [&]() { return FLayoutConnectorPlanning::TryBuildContinuationRecordForEndpointPair(
		Start, End, Binding, Profile, 0, Record, Failure); };
	TestTrue(TEXT("Diagonal +X/-Y entries need not face opposite directions"), BuildPair());
	TestTrue(TEXT("Root biome cannot restrict corridor rows"), Record.TerrainPathSelection.PathBiomeRowName.IsNone()
		&& Record.TerrainPathSelection.PathBiomeRowNames.IsEmpty());
	Start.ExposedEntryFaceDirection = ELayoutFaceDirection::NegX;
	End.ExposedEntryFaceDirection = ELayoutFaceDirection::NegX;
	TestTrue(TEXT("Route planning, not endpoint direction, decides whether same-facing entries connect"), BuildPair());
	Start.ExposedEntryFaceDirection = ELayoutFaceDirection::PosX;
	End.ExposedEntryFaceDirection = ELayoutFaceDirection::NegY;
	End.EndpointBlockWorldPos.Y = 0;
	TestTrue(TEXT("Route planning accepts inline entries with perpendicular authored faces"), BuildPair());
	End.ExposedEntryFaceDirection = ELayoutFaceDirection::NegX;
	TestTrue(TEXT("Inline facing doors remain eligible"), BuildPair());

	const FIntVector CellSize(10, 10, 10);
	TArray<FLayoutPlannedCell> Cells;
	FIntPoint Size, Min;
	TestTrue(TEXT("Expand full-width elbow"), FLayoutConnectorPlanning::ExpandContinuationCenterlineForTesting(
		{FIntPoint(1, 0), FIntPoint(2, 0), FIntPoint(3, 0), FIntPoint(3, 1), FIntPoint(3, 2)},
		3, 0, Cells, Size, Min));
	const FIntVector Origin(Min.X * 10 + 5, Min.Y * 10 + 5, 0);
	TArray<FLayoutRootSpacingReservation> Roots;
	FLayoutRootSpacingReservation Root;
	Root.Min = FIntPoint(-10, -10);
	Root.Max = FIntPoint(9, 9);
	Roots.Add(Root);
	auto Clear = [&]() { return FLayoutConnectorPlanning::ValidateContinuationClearance(
		Origin, CellSize, Cells, Roots, Failure); };
	TestTrue(TEXT("Small root leaves full-width corridor clear"), Clear());
	Roots[0].Max.X = 19;
	TestFalse(TEXT("Larger actual footprint blocks same doorway coordinates"), Clear());
	Roots[0] = Root;
	Root.Min = FIntPoint(30, -10);
	Root.Max = FIntPoint(39, -1);
	Roots.Add(Root);
	TestFalse(TEXT("Third root clips widened edge despite clear centerline"), Clear());
	Roots.Last().Min.Y = -20;
	Roots.Last().Max.Y = -11;
	TestTrue(TEXT("Adjacent nonoverlapping footprint is allowed"), Clear());
	return true;
}

} // anonymous namespace

