// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Actors/LayoutPreviewActor.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "EngineUtils.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Async/LayoutBackgroundLifecycleSequencer.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutSolveExecution.h"
#include "Layout/Async/LayoutWorkerSolvePacket.h"
#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SceneComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/SphereComponent.h"

namespace
{
	const FLayoutId PreviewPlacementPolicyId(TEXT("PreviewActor"));

	FLayoutWorldBindingPlacementPolicy BuildDefaultPreviewPlacementPolicy()
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
		return PlacementPolicy;
	}

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
				LayoutWorldBindingRuntimeHelpers::FindFrozenPlacementSourceEntry(ContentSet, Placement);
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
		FLayoutSolveResult& InOutSolveResult,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		RehydratePreviewPlacementCarriersFromContentSet(ContentSet, InOutSolveResult);
		RehydratePreviewPlacementCarriersFromContentSet(ContentSet, InOutScheduleResult.MergedSolveResult);
		for (FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
		{
			RehydratePreviewPlacementCarriersFromContentSet(ContentSet, RegionResult.SolveResult);
		}
	}

	struct FLayoutPreviewTerrainSamplingContext
	{
		AChunkWorldExtended* ChunkWorld = nullptr;
		FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;
		FLayoutNoiseCoordinateSettings CoordinateSettings;
		FLayoutActiveBiomeSampler ActiveBiomeSampler;
	};

	FLayoutNoiseCoordinateSettings BuildPreviewCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}

	AChunkWorldExtended* FindNearestPreviewChunkWorld(const ALayoutPreviewActor& PreviewActor)
	{
		UWorld* const World = PreviewActor.GetWorld();
		if (World == nullptr)
		{
			return nullptr;
		}

		AChunkWorldExtended* BestChunkWorld = nullptr;
		double BestDistanceSquared = TNumericLimits<double>::Max();
		for (TActorIterator<AChunkWorldExtended> It(World); It; ++It)
		{
			AChunkWorldExtended* const Candidate = *It;
			if (Candidate == nullptr || Candidate->WorldGenDef == nullptr)
			{
				continue;
			}

			const double CandidateDistanceSquared =
				FVector::DistSquared(PreviewActor.GetActorLocation(), Candidate->GetActorLocation());
			if (CandidateDistanceSquared < BestDistanceSquared)
			{
				BestDistanceSquared = CandidateDistanceSquared;
				BestChunkWorld = Candidate;
			}
		}

		return BestChunkWorld;
	}

	bool ShouldDerivePreviewSteppedTerrainSupport(const FLayoutWorldBindingRuntimeView& RuntimeView)
	{
		return RuntimeView.LayoutProfile != nullptr
			&& RuntimeView.LayoutProfile->bSupportsSteppedTerrainSolve
			&& RuntimeView.PlacementKind != ELayoutWorldBindingPlacementKind::None;
	}

	bool TryBuildPreviewTerrainSamplingContext(
		ALayoutPreviewActor& PreviewActor,
		FLayoutPreviewTerrainSamplingContext& OutContext)
	{
		OutContext = FLayoutPreviewTerrainSamplingContext();

		AChunkWorldExtended* const ChunkWorld = FindNearestPreviewChunkWorld(PreviewActor);
		if (ChunkWorld == nullptr || ChunkWorld->WorldGenDef == nullptr)
		{
			return false;
		}

		OutContext.ChunkWorld = ChunkWorld;
		OutContext.SiteCenterBlockWorldPos =
			ChunkWorld->UEWorldPosToBlockWorldPos(PreviewActor.GetActorLocation());
		OutContext.CoordinateSettings = BuildPreviewCoordinateSettings(ChunkWorld->WorldGenDef);
		return OutContext.ActiveBiomeSampler.Initialize(
			&PreviewActor,
			ChunkWorld->WorldGenDef,
			ChunkWorld->Seed);
	}

	FColor ColorForIntent(const ELayoutCellIntent Intent)
	{
		switch (Intent)
		{
		case ELayoutCellIntent::Entry:
			return FColor(70, 200, 255);
		case ELayoutCellIntent::VerticalAccess:
			return FColor(255, 170, 70);
		case ELayoutCellIntent::Connector:
			return FColor(255, 90, 200);
		case ELayoutCellIntent::Boundary:
			return FColor(120, 220, 120);
		case ELayoutCellIntent::Interior:
		default:
			return FColor(210, 210, 210);
		}
	}

	bool ContainsIntentFilter(const TArray<ELayoutCellIntent>& Filter, const ELayoutCellIntent Intent)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}

		for (const ELayoutCellIntent FilterIntent : Filter)
		{
			if (FilterIntent == Intent)
			{
				return true;
			}
		}

		return false;
	}

	bool ContainsZoneFilter(const TArray<ELayoutPlacementZone>& Filter, const ELayoutPlacementZone Zone)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}

		for (const ELayoutPlacementZone FilterZone : Filter)
		{
			if (FilterZone == Zone)
			{
				return true;
			}
		}

		return false;
	}

	bool MatchesRoleFilter(const TArray<ELayoutModuleRole>& Filter, const ULayoutModuleAsset* Module)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}

		if (Module == nullptr)
		{
			return false;
		}

		for (const ELayoutModuleRole Role : Module->Roles)
		{
			for (const ELayoutModuleRole FilterRole : Filter)
			{
				if (Role == FilterRole)
				{
					return true;
				}
			}
		}

		return false;
	}

	bool PlacementMatchesRoleFilter(const TArray<ELayoutModuleRole>& Filter, const FLayoutPlacedModule& Placement)
	{
		if (Filter.IsEmpty())
		{
			return true;
		}

		if (MatchesRoleFilter(Filter, Placement.Module.Get()))
		{
			return true;
		}

		if (Placement.CompositeModule != nullptr)
		{
			for (const FLayoutCompositeModuleCell& CompositeCell : Placement.CompositeModule->Cells)
			{
				if (MatchesRoleFilter(Filter, CompositeCell.Module.Get()))
				{
					return true;
				}
			}
		}

		return false;
	}

	bool PlacementContainsRole(const FLayoutPlacedModule& Placement, const ELayoutModuleRole Role)
	{
		if (Placement.Module != nullptr && Placement.Module->Roles.Contains(Role))
		{
			return true;
		}

		if (Placement.CompositeModule != nullptr)
		{
			for (const FLayoutCompositeModuleCell& CompositeCell : Placement.CompositeModule->Cells)
			{
				if (CompositeCell.Module != nullptr && CompositeCell.Module->Roles.Contains(Role))
				{
					return true;
				}
			}
		}

		return false;
	}

	int32 NormalizePreviewYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	FIntVector RotatePreviewPlacementCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		switch (NormalizePreviewYawRotationSteps(YawRotationSteps))
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

	TArray<FIntVector> ResolvePreviewPlacementOccupiedLocalCells(const FLayoutPlacedModule& Placement)
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

	FIntPoint ResolvePreviewPlacementRotationFootprint(const FLayoutPlacedModule& Placement)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolvePreviewPlacementOccupiedLocalCells(Placement);
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

	TArray<FIntVector> BuildPreviewPlacementCells(const FLayoutPlacedModule& Placement)
	{
		const TArray<FIntVector> OccupiedLocalCells = ResolvePreviewPlacementOccupiedLocalCells(Placement);
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
				Placement.Cell + RotatePreviewPlacementCellInFootprintYaw(LocalCell, RotationFootprint, Placement.YawRotationSteps));
		}

		return PlacementCells;
	}

	int32 ResolvePreviewLevelCount(const ALayoutPreviewActor& PreviewActor)
	{
		return PreviewActor.LayoutProfile != nullptr ? FMath::Max(1, PreviewActor.LayoutProfile->LevelCount) : 1;
	}

	FVector ResolveCellSizeUnits(const FIntVector& SharedCellSizeInBlocks, const float BlockSizeInUnrealUnits)
	{
		return FVector(
			static_cast<double>(FMath::Max(1, SharedCellSizeInBlocks.X)) * BlockSizeInUnrealUnits,
			static_cast<double>(FMath::Max(1, SharedCellSizeInBlocks.Y)) * BlockSizeInUnrealUnits,
			static_cast<double>(FMath::Max(1, SharedCellSizeInBlocks.Z)) * BlockSizeInUnrealUnits);
	}

	FVector ResolveFootprintMinRelativeLocation(
		const FLayoutSolveResult& SolveResult,
		const FIntVector& SharedCellSizeInBlocks,
		const float BlockSizeInUnrealUnits)
	{
		const FVector CellSizeUnits = ResolveCellSizeUnits(SharedCellSizeInBlocks, BlockSizeInUnrealUnits);
		return FVector(
			-static_cast<double>(SolveResult.FootprintSize.X) * CellSizeUnits.X * 0.5,
			-static_cast<double>(SolveResult.FootprintSize.Y) * CellSizeUnits.Y * 0.5,
			static_cast<double>(SolveResult.TemplatePlacementZOffsetBlocks) * BlockSizeInUnrealUnits);
	}

	FVector ResolveCellCenterRelativeLocation(
		const FLayoutSolveResult& SolveResult,
		const FIntVector& SharedCellSizeInBlocks,
		const float BlockSizeInUnrealUnits,
		const FIntVector& Cell)
	{
		const FVector CellSizeUnits = ResolveCellSizeUnits(SharedCellSizeInBlocks, BlockSizeInUnrealUnits);
		const FVector FootprintMin = ResolveFootprintMinRelativeLocation(SolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits);
		return FootprintMin + FVector(
			(static_cast<double>(Cell.X) + 0.5) * CellSizeUnits.X,
			(static_cast<double>(Cell.Y) + 0.5) * CellSizeUnits.Y,
			(static_cast<double>(Cell.Z) + 0.5) * CellSizeUnits.Z);
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

	FVector ResolveFaceOffset(
		const FVector& CellHalfExtents,
		const ELayoutFaceDirection Direction,
		const float FaceDepthUnits)
	{
		switch (Direction)
		{
		case ELayoutFaceDirection::NegX:
			return FVector(-(CellHalfExtents.X - FaceDepthUnits * 0.5), 0.0, 0.0);
		case ELayoutFaceDirection::PosY:
			return FVector(0.0, CellHalfExtents.Y - FaceDepthUnits * 0.5, 0.0);
		case ELayoutFaceDirection::NegY:
			return FVector(0.0, -(CellHalfExtents.Y - FaceDepthUnits * 0.5), 0.0);
		case ELayoutFaceDirection::PosZ:
			return FVector(0.0, 0.0, CellHalfExtents.Z - FaceDepthUnits * 0.5);
		case ELayoutFaceDirection::NegZ:
			return FVector(0.0, 0.0, -(CellHalfExtents.Z - FaceDepthUnits * 0.5));
		case ELayoutFaceDirection::PosX:
		default:
			return FVector(CellHalfExtents.X - FaceDepthUnits * 0.5, 0.0, 0.0);
		}
	}

#if WITH_EDITOR
	template <typename TComponentType>
	TComponentType* CreatePreviewSceneComponent(
		ALayoutPreviewActor& PreviewActor,
		TArray<TObjectPtr<USceneComponent>>& OutComponents,
		const FName BaseName)
	{
		// Preview overlays can emit many components of the same kind per redraw, so each one needs a unique object
		// name or later allocations can replace earlier boxes instead of adding another visible overlay primitive.
		const FName UniqueName = MakeUniqueObjectName(&PreviewActor, TComponentType::StaticClass(), BaseName);
		TComponentType* Component = NewObject<TComponentType>(&PreviewActor, UniqueName, RF_Transient | RF_TextExportTransient);
		Component->SetupAttachment(PreviewActor.GetRootComponent());
		Component->SetMobility(EComponentMobility::Movable);
		Component->SetHiddenInGame(true);
		Component->bIsEditorOnly = true;
		PreviewActor.AddInstanceComponent(Component);
		Component->RegisterComponent();
		OutComponents.Add(Component);
		return Component;
	}

	void ConfigureShapeComponent(UShapeComponent& ShapeComponent, const FColor& Color)
	{
		ShapeComponent.SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ShapeComponent.SetGenerateOverlapEvents(false);
		ShapeComponent.SetCanEverAffectNavigation(false);
		ShapeComponent.SetHiddenInGame(true);
		ShapeComponent.ShapeColor = Color;
		ShapeComponent.bDrawOnlyIfSelected = false;
	}
#endif
}

ALayoutPreviewActor::ALayoutPreviewActor()
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);
	PreviewPlacementPolicy = BuildDefaultPreviewPlacementPolicy();
}

ALayoutPreviewActor::~ALayoutPreviewActor()
{
}

bool ALayoutPreviewActor::RebuildPreview()
{
	if (LayoutProfile == nullptr)
	{
		bHasCachedPreviewRegionResult = false;
		CachedScheduleResult = FLayoutRegionSolveScheduleResult();
		CachedRegionResult = FLayoutRegionSolveResult();
		CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
		PreviewResult = FLayoutSolveResult();
		PreviewResult.bSucceeded = false;
		PreviewResult.FailureReason = TEXT("Preview actor requires a layout profile.");
#if WITH_EDITOR
		DestroyDebugPreviewComponents();
#endif
		return false;
	}

	FLayoutRegionSolveRequest SolveRequest;
	if (LayoutProfile->ContentSet != nullptr)
	{
		const FString PreviewRootPath = FString::Printf(TEXT("Preview/%s"), *GetName());
		const FLayoutWorldBindingRuntimeView PreviewRuntimeView =
			LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
				LayoutProfile, LayoutProfile->ContentSet.Get(), SolveBudget,
				PreviewPlacementPolicy);
		FLayoutPreviewTerrainSamplingContext TerrainSamplingContext;
		FString RequestFailureReason;
		const bool bHasTerrainSamplingContext = TryBuildPreviewTerrainSamplingContext(
			*this,
			TerrainSamplingContext);

		// Snap the site center to the shared-cell lattice for the request builder.
		// Terrain sampling already uses the sampler initialized from the actor location;
		// the snapped site center only affects the lattice validation check.
		FIntVector SnappedSiteCenter = TerrainSamplingContext.SiteCenterBlockWorldPos;
		if (bHasTerrainSamplingContext)
		{
			const FIntVector SharedCellSize =
				LayoutProfile->ContentSet != nullptr
					? LayoutProfile->ContentSet->GetSharedCellSizeInBlocks()
					: FIntVector(16, 16, 16);
			if (SharedCellSize.X > 0 && SharedCellSize.Y > 0 && SharedCellSize.Z > 0)
			{
				SnappedSiteCenter =
					FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
						TerrainSamplingContext.SiteCenterBlockWorldPos,
						SharedCellSize);
			}
		}

		const bool bBuiltRequest = LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			PreviewRuntimeView,
			Seed,
			PreviewRootPath,
			PreviewPlacementPolicyId,
			FLayoutId(*PreviewRootPath),
			FLayoutId(*PreviewRootPath),
			SolveRequest,
			RequestFailureReason);
		if (!bBuiltRequest)
		{
			bHasCachedPreviewRegionResult = false;
			CachedScheduleResult = FLayoutRegionSolveScheduleResult();
			CachedRegionResult = FLayoutRegionSolveResult();
			CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
			PreviewResult = FLayoutSolveResult();
			PreviewResult.bSucceeded = false;
			PreviewResult.FailureReason = RequestFailureReason.IsEmpty()
				? TEXT("Preview actor could not build a standalone runtime-view request.")
				: RequestFailureReason;
#if WITH_EDITOR
			DestroyDebugPreviewComponents();
#endif
			return false;
		}

		// Route the preview solve through the background dispatcher lifecycle
		// chain (prewarm -> preflight -> solve), same as production.  A
		// synchronous executor processes the chain inline during Submit()
		// so the result is available when RebuildPreview returns.
		{
			FLayoutWorkerSolvePacket WorkerSolvePacket =
				FLayoutWorkerSolvePacket::CaptureExplicitPreviewRoot(
					GetName(),
					PreviewRuntimeView,
					SnappedSiteCenter,
					Seed);
			WorkerSolvePacket.RequestManifest =
				FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(SolveRequest);
			WorkerSolvePacket.bHasRequestManifest = true;
			{
				FLayoutContractModeSelectionInput ModeInput;
				ModeInput.SolveRequest = &SolveRequest;
				ModeInput.SiteCenterBlockWorldPos = SnappedSiteCenter;
				ModeInput.WorldSeed = Seed;
				WorkerSolvePacket.SelectedModePlan =
					FLayoutContractModeSelection::SelectModePlan(ModeInput);

			}
			WorkerSolvePacket.bHasSelectedModePlan = true;
			WorkerSolvePacket.RequestManifest.bHasSelectedModePlan = true;
			WorkerSolvePacket.RequestManifest.SelectedModePlan =
				WorkerSolvePacket.SelectedModePlan;

			if (!BackgroundSolveDispatcher.IsValid())
			{
				BackgroundSolveDispatcher =
					MakeUnique<FLayoutBackgroundSolveDispatcher>(
						FLayoutBackgroundSolveSettings());
				BackgroundSolveDispatcher->SetExecution(
					MakeUnique<FSynchronousLayoutSolveExecution>());
			}

			struct FPreviewSolveSharedResult
			{
				FLayoutRegionSolveScheduleResult ScheduleResult;
			};
			TSharedRef<FPreviewSolveSharedResult, ESPMode::ThreadSafe> SharedResult =
				MakeShared<FPreviewSolveSharedResult, ESPMode::ThreadSafe>();
			TSharedRef<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe> SharedWorkerPacket =
				MakeShared<FLayoutWorkerSolvePacket, ESPMode::ThreadSafe>(
					MoveTemp(WorkerSolvePacket));
			TWeakObjectPtr<ALayoutPreviewActor> WeakThis(this);
			ULayoutRegionContentSetAsset* const CapturedContentSet =
				LayoutProfile->ContentSet.Get();
			FLayoutBackgroundSolveSubmission Submission;
			Submission.DebugName = FString::Printf(
				TEXT("Preview %s"), *GetName());
			Submission.LayoutGroupId = static_cast<uint64>(GetTypeHash(GetName()))
				^ (static_cast<uint64>(static_cast<uint32>(Seed)) << 32);
			Submission.Tier = ELayoutBackgroundSolveJobTier::NearRoot;
			Submission.Priority = 0;
			Submission.Work = [
				SharedResult,
				SharedWorkerPacket](
				const FLayoutSolveCancellationToken& CancellationToken,
				FString& OutFailureReason) mutable
			{
				if (CancellationToken.IsCancellationRequested())
				{
					OutFailureReason = TEXT("Preview solve canceled.");
					return false;
				}
				FLayoutRegionSolveRequest FinalizedRequest;
				if (!LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(
						*SharedWorkerPacket,
						FinalizedRequest,
						OutFailureReason))
				{
					return false;
				}
				if (FinalizedRequest.PrecomputedFrozenTerrainContract.ContractId.IsNone())
				{
					FString PrecomputeFailure;
					if (!FLayoutContractPipeline::TryPrecomputeAdapterOutput(
							FinalizedRequest,
							PrecomputeFailure))
					{
						OutFailureReason = PrecomputeFailure.IsEmpty()
							? TEXT("Preview adapter precompute rejected the request.")
							: PrecomputeFailure;
						return false;
					}
				}
				SharedResult->ScheduleResult =
					FLayoutProfileSolver::SolveRegionTree(FinalizedRequest);
				const bool bSucceeded =
					SharedResult->ScheduleResult.MergedSolveResult.bSucceeded;
				if (!bSucceeded)
				{
					OutFailureReason =
						SharedResult->ScheduleResult.MergedSolveResult.FailureReason;
				}
				return bSucceeded;
			};
			Submission.PublishOnGameThread = [
				WeakThis,
				SharedResult,
				CapturedContentSet](
				const FLayoutBackgroundSolveCompletion& Completion)
			{
				ALayoutPreviewActor* const Actor = WeakThis.Get();
				if (Actor == nullptr)
				{
					return;
				}
				Actor->CachedScheduleResult = SharedResult->ScheduleResult;
				Actor->CachedRegionResult = FLayoutRegionSolveResult();
				if (!Completion.bWorkSucceeded || Completion.bCanceled)
				{
					Actor->CachedScheduleResult.MergedSolveResult.bSucceeded = false;
					Actor->CachedScheduleResult.MergedSolveResult.FailureReason = Completion.FailureReason;
				}
				Actor->PreviewResult =
					Actor->CachedScheduleResult.MergedSolveResult;
				if (!Actor->PreviewResult.bSucceeded)
				{
					UE_LOG(LogTemp, Verbose,
						TEXT("LayoutPreviewActor::RebuildPreview solve rejected: %s"),
						*Actor->PreviewResult.FailureReason);
#if WITH_EDITOR
					Actor->DestroyDebugPreviewComponents();
#endif
				}
				// Extract root-region result for overlay drawing.
				for (const FLayoutRegionSolveResult& RegionResult :
					Actor->CachedScheduleResult.RegionResults)
				{
					if (RegionResult.RegionCellOffset == FIntVector::ZeroValue)
					{
						Actor->CachedRegionResult = RegionResult;
						break;
					}
				}
				Actor->CachedSharedCellSizeInBlocks =
					Actor->PreviewResult.SharedCellSizeInBlocks != FIntVector::ZeroValue
						? Actor->PreviewResult.SharedCellSizeInBlocks
						: Actor->CachedRegionResult.SolveResult.SharedCellSizeInBlocks;
				Actor->bHasCachedPreviewRegionResult =
					Actor->PreviewResult.bSucceeded;
				// Rehydrate live module/composite pointer carriers from content set
				// after snapshot-based solve.
				if (Actor->bHasCachedPreviewRegionResult
					&& CapturedContentSet != nullptr)
				{
					RehydratePreviewPlacementCarriersFromContentSet(
						CapturedContentSet,
						Actor->PreviewResult,
						Actor->CachedScheduleResult);
#if WITH_EDITOR
					if (Actor->bAutoRedrawDebugAfterSolve)
					{
						Actor->RebuildDebugPreviewComponents();
					}
#endif
				}
			};
			UChunkWorldLayoutRuntimeComponent::SubmitRootSolveViaLifecycleSequencer(
				*BackgroundSolveDispatcher,
				FString::Printf(TEXT("PreviewRoot %s"), *GetName()),
				Submission.LayoutGroupId,
				Submission.Priority,
				*SharedWorkerPacket,
				MoveTemp(Submission),
				SharedWorkerPacket);
		}

		return bHasCachedPreviewRegionResult;

	}
	else
	{
		bHasCachedPreviewRegionResult = false;
		CachedScheduleResult = FLayoutRegionSolveScheduleResult();
		CachedRegionResult = FLayoutRegionSolveResult();
		CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
		PreviewResult = FLayoutSolveResult();
		PreviewResult.bSucceeded = false;
		PreviewResult.FailureReason = TEXT("Preview actor requires a layout profile that references a ContentSet.");
#if WITH_EDITOR
		DestroyDebugPreviewComponents();
#endif
		return false;
	}

	bHasCachedPreviewRegionResult = false;
	CachedScheduleResult = FLayoutRegionSolveScheduleResult();
	CachedRegionResult = FLayoutRegionSolveResult();
	CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
	PreviewResult = FLayoutSolveResult();
	PreviewResult.bSucceeded = false;
	PreviewResult.FailureReason = TEXT("LayoutPreviewActor synchronous proof path is disabled. Use the async layout generator facade/controller preview path.");
#if WITH_EDITOR
	DestroyDebugPreviewComponents();
#endif
	return false;
}

void ALayoutPreviewActor::ClearPreview()
{
#if WITH_EDITOR
	DestroyDebugPreviewComponents();
#endif
	bHasCachedPreviewRegionResult = false;
	CachedScheduleResult = FLayoutRegionSolveScheduleResult();
	CachedRegionResult = FLayoutRegionSolveResult();
	CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;
	PreviewResult = FLayoutSolveResult();
}

FIntVector ALayoutPreviewActor::ResolveSharedCellSizeInBlocks() const
{
	if (PreviewResult.SharedCellSizeInBlocks != FIntVector::ZeroValue)
	{
		return PreviewResult.SharedCellSizeInBlocks;
	}

	if (CachedRegionResult.SolveResult.SharedCellSizeInBlocks != FIntVector::ZeroValue)
	{
		return CachedRegionResult.SolveResult.SharedCellSizeInBlocks;
	}

	return FIntVector::ZeroValue;
}

bool ALayoutPreviewActor::HasCachedRegionResult() const
{
	return bHasCachedPreviewRegionResult;
}

#if WITH_EDITOR
void ALayoutPreviewActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	const FLayoutId PropertyName = PropertyChangedEvent.GetPropertyName();
	if (PropertyName == GET_MEMBER_NAME_CHECKED(ALayoutPreviewActor, LayoutProfile)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(ALayoutPreviewActor, Seed))
	{
		if (bAutoRedrawDebugAfterSolve)
		{
			RebuildPreview();
		}
		return;
	}

	if (bAutoRedrawDebugOnSettingsChange)
	{
		RebuildDebugPreviewComponents();
	}
}

void ALayoutPreviewActor::DestroyDebugPreviewComponents()
{
	for (USceneComponent* Component : DebugPreviewComponents)
	{
		if (Component != nullptr)
		{
			Component->DestroyComponent();
		}
	}

	DebugPreviewComponents.Reset();
}

void ALayoutPreviewActor::RebuildDebugPreviewComponents()
{
	DestroyDebugPreviewComponents();

	if (!HasCachedRegionResult())
	{
		return;
	}

	const FLayoutSolveResult& SolveResult = CachedRegionResult.SolveResult;
	// Use the merged schedule result for structural visualization so recursive child placements remain visible in preview.
	const FLayoutSolveResult& DisplaySolveResult = PreviewResult.bSucceeded ? PreviewResult : SolveResult;
	const FIntVector SharedCellSizeInBlocks = CachedSharedCellSizeInBlocks != FIntVector::ZeroValue
		? CachedSharedCellSizeInBlocks
		: ResolveSharedCellSizeInBlocks();
	if (SharedCellSizeInBlocks == FIntVector::ZeroValue)
	{
		return;
	}

	const FVector CellSizeUnits = ResolveCellSizeUnits(SharedCellSizeInBlocks, BlockSizeInUnrealUnits);
	const FVector DefaultHalfExtents = FVector(
		FMath::Max(1.0, CellSizeUnits.X * 0.5 - DebugBoxPaddingInUnrealUnits),
		FMath::Max(1.0, CellSizeUnits.Y * 0.5 - DebugBoxPaddingInUnrealUnits),
		FMath::Max(1.0, CellSizeUnits.Z * 0.5 - DebugBoxPaddingInUnrealUnits));

	if (bDrawRootFootprintBounds)
	{
		const FVector FootprintMin = ResolveFootprintMinRelativeLocation(SolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits);
		const FVector FootprintSizeUnits(
			static_cast<double>(SolveResult.FootprintSize.X) * CellSizeUnits.X,
			static_cast<double>(SolveResult.FootprintSize.Y) * CellSizeUnits.Y,
			static_cast<double>(ResolvePreviewLevelCount(*this)) * CellSizeUnits.Z);
		UBoxComponent* RootBounds = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewRootBounds"));
		ConfigureShapeComponent(*RootBounds, FColor::White);
		RootBounds->InitBoxExtent(FVector(
			FMath::Max(1.0, FootprintSizeUnits.X * 0.5),
			FMath::Max(1.0, FootprintSizeUnits.Y * 0.5),
			FMath::Max(1.0, FootprintSizeUnits.Z * 0.5)));
		RootBounds->SetRelativeLocation(FootprintMin + FVector(FootprintSizeUnits.X * 0.5, FootprintSizeUnits.Y * 0.5, FootprintSizeUnits.Z * 0.5));
	}

	if (bDrawChildRegionBounds && CachedScheduleResult.RegionResults.Num() > 1)
	{
		const FVector RootFootprintMin = ResolveFootprintMinRelativeLocation(SolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits);
		for (const FLayoutRegionSolveResult& RegionResult : CachedScheduleResult.RegionResults)
		{
			if (RegionResult.RegionDebugPath == CachedRegionResult.RegionDebugPath
				|| RegionResult.SolveResult.FootprintSize.X <= 0
				|| RegionResult.SolveResult.FootprintSize.Y <= 0)
			{
				continue;
			}

			int32 ChildLevelCount = 1;
			for (const FLayoutPlannedCell& PlannedCell : RegionResult.SolveResult.PlannedCells)
			{
				ChildLevelCount = FMath::Max(ChildLevelCount, PlannedCell.Cell.Z + 1);
			}
			const FVector ChildFootprintMin = RootFootprintMin + FVector(
				static_cast<double>(RegionResult.RegionCellOffset.X) * CellSizeUnits.X,
				static_cast<double>(RegionResult.RegionCellOffset.Y) * CellSizeUnits.Y,
				static_cast<double>(RegionResult.RegionCellOffset.Z) * CellSizeUnits.Z);
			const FVector ChildFootprintSizeUnits(
				static_cast<double>(RegionResult.SolveResult.FootprintSize.X) * CellSizeUnits.X,
				static_cast<double>(RegionResult.SolveResult.FootprintSize.Y) * CellSizeUnits.Y,
				static_cast<double>(ChildLevelCount) * CellSizeUnits.Z);
			UBoxComponent* ChildBounds = CreatePreviewSceneComponent<UBoxComponent>(
				*this,
				DebugPreviewComponents,
				FName(*FString::Printf(TEXT("PreviewChildBounds_%s"), *RegionResult.RegionDebugPath.Replace(TEXT("/"), TEXT("_")))));
			ConfigureShapeComponent(*ChildBounds, FColor(255, 210, 90));
			ChildBounds->InitBoxExtent(FVector(
				FMath::Max(1.0, ChildFootprintSizeUnits.X * 0.5),
				FMath::Max(1.0, ChildFootprintSizeUnits.Y * 0.5),
				FMath::Max(1.0, ChildFootprintSizeUnits.Z * 0.5)));
			ChildBounds->SetRelativeLocation(ChildFootprintMin + FVector(
				ChildFootprintSizeUnits.X * 0.5,
				ChildFootprintSizeUnits.Y * 0.5,
				ChildFootprintSizeUnits.Z * 0.5));
		}
	}

	if (bDrawPlannedCells)
	{
		auto DrawRegionPlannedCells = [&](const FLayoutSolveResult& RegionSolveResult, const FIntVector& RegionOffset)
		{
			for (const FLayoutPlannedCell& PlannedCell : RegionSolveResult.PlannedCells)
			{
				if (!ContainsIntentFilter(VisiblePlannedCellIntents, PlannedCell.Intent))
				{
					continue;
				}

				UBoxComponent* PlannedBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewPlannedCell"));
				ConfigureShapeComponent(*PlannedBox, ColorForIntent(PlannedCell.Intent));
				PlannedBox->InitBoxExtent(DefaultHalfExtents * 0.85);
				PlannedBox->SetRelativeLocation(ResolveCellCenterRelativeLocation(
					SolveResult,
					SharedCellSizeInBlocks,
					BlockSizeInUnrealUnits,
					PlannedCell.Cell + RegionOffset));
			}
		};
		DrawRegionPlannedCells(SolveResult, FIntVector::ZeroValue);
		for (const FLayoutRegionSolveResult& RegionResult : CachedScheduleResult.RegionResults)
		{
			if (RegionResult.RegionDebugPath != CachedRegionResult.RegionDebugPath
				&& !RegionResult.bDroppedAsOptionalChild)
			{
				DrawRegionPlannedCells(RegionResult.SolveResult, RegionResult.RegionCellOffset);
			}
		}
	}

	if (bDrawPlacements)
	{
		for (const FLayoutPlacedModule& Placement : DisplaySolveResult.Placements)
		{
			if (!ContainsIntentFilter(VisiblePlacementIntents, Placement.Intent) || !PlacementMatchesRoleFilter(VisiblePlacementRoles, Placement))
			{
				continue;
			}

			const FColor PlacementColor = PlacementContainsRole(Placement, ELayoutModuleRole::Entry)
				? FColor(70, 200, 255)
				: ColorForIntent(Placement.Intent);

			for (const FIntVector& PlacementCell : BuildPreviewPlacementCells(Placement))
			{
				UBoxComponent* PlacementBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewPlacement"));
				ConfigureShapeComponent(*PlacementBox, PlacementColor);
				PlacementBox->InitBoxExtent(DefaultHalfExtents);
				PlacementBox->SetRelativeLocation(
					ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, PlacementCell));
			}
		}
	}

	if (bDrawExportedEntryCells)
	{
		const float EntryRadius = static_cast<float>(FMath::Max(8.0, FMath::Min3(CellSizeUnits.X, CellSizeUnits.Y, CellSizeUnits.Z) * 0.18));
		auto DrawRegionEntryCells = [&](const FLayoutSolveResult& RegionSolveResult, const FIntVector& RegionOffset)
		{
			for (const FIntVector& EntryCell : RegionSolveResult.ExportedEntryCells)
			{
				USphereComponent* EntrySphere = CreatePreviewSceneComponent<USphereComponent>(*this, DebugPreviewComponents, TEXT("PreviewEntryCell"));
				ConfigureShapeComponent(*EntrySphere, FColor(0, 255, 255));
				EntrySphere->InitSphereRadius(EntryRadius);
				EntrySphere->SetRelativeLocation(ResolveCellCenterRelativeLocation(
					SolveResult,
					SharedCellSizeInBlocks,
					BlockSizeInUnrealUnits,
					EntryCell + RegionOffset));
			}
		};
		DrawRegionEntryCells(SolveResult, FIntVector::ZeroValue);
		for (const FLayoutRegionSolveResult& RegionResult : CachedScheduleResult.RegionResults)
		{
			if (RegionResult.RegionDebugPath != CachedRegionResult.RegionDebugPath
				&& !RegionResult.bDroppedAsOptionalChild)
			{
				DrawRegionEntryCells(RegionResult.SolveResult, RegionResult.RegionCellOffset);
			}
		}
	}

	if (bDrawExportedBoundaryPoints)
	{
		const float ArrowLength = static_cast<float>(FMath::Max(24.0, CellSizeUnits.GetMin() * 0.45));
		auto DrawRegionBoundaryPoints = [&](const TArray<FLayoutSolveBoundaryPoint>& BoundaryPoints, const FIntVector& RegionOffset)
		{
			for (const FLayoutSolveBoundaryPoint& BoundaryPoint : BoundaryPoints)
			{
				UArrowComponent* Arrow = CreatePreviewSceneComponent<UArrowComponent>(*this, DebugPreviewComponents, TEXT("PreviewBoundaryPoint"));
				Arrow->ArrowColor = BoundaryPoint.bRepresentsFilledNeighbor ? FColor::Green : FColor::Cyan;
				Arrow->ArrowSize = 0.7f;
				Arrow->ArrowLength = ArrowLength;
				Arrow->bIsScreenSizeScaled = false;

				const FVector CellCenter = ResolveCellCenterRelativeLocation(
					SolveResult,
					SharedCellSizeInBlocks,
					BlockSizeInUnrealUnits,
					BoundaryPoint.LocalCell + RegionOffset);
				const FVector FaceVector = ResolveFaceDirectionVector(BoundaryPoint.FaceDirection);
				Arrow->SetRelativeLocation(CellCenter + FaceVector * (CellSizeUnits.GetMin() * 0.25));
				Arrow->SetRelativeRotation(FaceVector.Rotation());
			}
		};
		DrawRegionBoundaryPoints(CachedRegionResult.ExportedBoundaryPoints, FIntVector::ZeroValue);
		for (const FLayoutRegionSolveResult& RegionResult : CachedScheduleResult.RegionResults)
		{
			if (RegionResult.RegionDebugPath != CachedRegionResult.RegionDebugPath
				&& !RegionResult.bDroppedAsOptionalChild)
			{
				DrawRegionBoundaryPoints(RegionResult.ExportedBoundaryPoints, RegionResult.RegionCellOffset);
			}
		}
	}

	if (bDrawClosureSegments)
	{
		const float FaceDepthUnits = static_cast<float>(FMath::Max(4.0, CellSizeUnits.GetMin() * 0.12));
		for (const FLayoutClosureCoverageSegmentRecord& Segment : DisplaySolveResult.ClosureSegments)
		{
			if (!ContainsZoneFilter(VisibleClosureZones, Segment.BoundaryZone))
			{
				continue;
			}

			UBoxComponent* SegmentBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewClosureSegment"));
			ConfigureShapeComponent(*SegmentBox, Segment.bCovered ? FColor::Green : FColor::Red);

			FVector HalfExtents = DefaultHalfExtents;
			switch (Segment.FaceDirection)
			{
			case ELayoutFaceDirection::PosX:
			case ELayoutFaceDirection::NegX:
				HalfExtents.X = FaceDepthUnits * 0.5f;
				break;
			case ELayoutFaceDirection::PosY:
			case ELayoutFaceDirection::NegY:
				HalfExtents.Y = FaceDepthUnits * 0.5f;
				break;
			case ELayoutFaceDirection::PosZ:
			case ELayoutFaceDirection::NegZ:
				HalfExtents.Z = FaceDepthUnits * 0.5f;
				break;
			default:
				break;
			}

			const FVector CellCenter = ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, Segment.Cell);
			const FVector FaceOffset = ResolveFaceOffset(DefaultHalfExtents, Segment.FaceDirection, FaceDepthUnits);
			SegmentBox->InitBoxExtent(HalfExtents);
			SegmentBox->SetRelativeLocation(CellCenter + FaceOffset);
		}
	}

	if (bDrawClosureRuns)
	{
		for (const FLayoutClosureRunRecord& Run : DisplaySolveResult.ClosureRuns)
		{
			if (!ContainsZoneFilter(VisibleClosureZones, Run.BoundaryZone))
			{
				continue;
			}

			const FVector StartCenter = ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, Run.StartCell);
			const FVector EndCenter = ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, Run.EndCell);
			const FVector RunCenter = (StartCenter + EndCenter) * 0.5;
			const FVector RunSpan = (EndCenter - StartCenter).GetAbs();

			UBoxComponent* RunBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewClosureRun"));
			ConfigureShapeComponent(*RunBox, Run.bCovered ? FColor(60, 220, 120) : FColor(255, 70, 70));
			RunBox->InitBoxExtent(FVector(
				FMath::Max(DefaultHalfExtents.X * 0.35, RunSpan.X * 0.5 + DefaultHalfExtents.X * 0.5),
				FMath::Max(DefaultHalfExtents.Y * 0.35, RunSpan.Y * 0.5 + DefaultHalfExtents.Y * 0.5),
				FMath::Max(DefaultHalfExtents.Z * 0.35, RunSpan.Z * 0.5 + DefaultHalfExtents.Z * 0.5)));
			RunBox->SetRelativeLocation(RunCenter);
		}
	}

	if (bDrawPartitionSeams)
	{
		for (const FLayoutPartitionSeamRecord& Seam : DisplaySolveResult.PartitionSeams)
		{
			if (!ContainsZoneFilter(VisibleClosureZones, ELayoutPlacementZone::Perimeter))
			{
				continue;
			}

			const FVector StartCenter = ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, Seam.OwnerStartCell);
			const FVector EndCenter = ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, Seam.OwnerEndCell);
			const FVector SeamCenter = (StartCenter + EndCenter) * 0.5;
			const FVector SeamSpan = (EndCenter - StartCenter).GetAbs();

			UBoxComponent* SeamBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewPartitionSeam"));
			ConfigureShapeComponent(*SeamBox, FColor(255, 0, 255));
			SeamBox->InitBoxExtent(FVector(
				FMath::Max(DefaultHalfExtents.X * 0.2, SeamSpan.X * 0.5 + DefaultHalfExtents.X * 0.3),
				FMath::Max(DefaultHalfExtents.Y * 0.2, SeamSpan.Y * 0.5 + DefaultHalfExtents.Y * 0.3),
				FMath::Max(DefaultHalfExtents.Z * 0.2, SeamSpan.Z * 0.5 + DefaultHalfExtents.Z * 0.3)));
			SeamBox->SetRelativeLocation(SeamCenter);
		}
	}

	if (bDrawResidualCells)
	{
		for (const FLayoutResidualCellRecord& ResidualCell : DisplaySolveResult.ResidualUnoccupiedCells)
		{
			UBoxComponent* ResidualBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewResidualCell"));
			// Local project change: terrain-backed no-write cells must stay visually distinct from dropped-child and legacy residuals.
			const FColor ResidualColor = ResidualCell.Source == ELayoutResidualCellSource::DroppedOptionalChild
				? FColor(255, 130, 70)
				: ResidualCell.Source == ELayoutResidualCellSource::TerrainBackedRule
					? FColor(80, 170, 255)
					: FColor(180, 180, 180);
			ConfigureShapeComponent(*ResidualBox, ResidualColor);
			ResidualBox->InitBoxExtent(DefaultHalfExtents * 0.7);
			ResidualBox->SetRelativeLocation(ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, ResidualCell.Cell));
		}
	}

	if (bDrawSparsePlacements)
	{
		for (const FLayoutSparsePlacementCommitment& Commitment : DisplaySolveResult.SparsePlacementCommitments)
		{
			UBoxComponent* SparseBox = CreatePreviewSceneComponent<UBoxComponent>(*this, DebugPreviewComponents, TEXT("PreviewSparsePlacement"));
			ConfigureShapeComponent(*SparseBox, FColor(70, 255, 150));
			SparseBox->InitBoxExtent(DefaultHalfExtents * 0.6);
			SparseBox->SetRelativeLocation(ResolveCellCenterRelativeLocation(DisplaySolveResult, SharedCellSizeInBlocks, BlockSizeInUnrealUnits, Commitment.Cell));
		}
	}
}
#endif
