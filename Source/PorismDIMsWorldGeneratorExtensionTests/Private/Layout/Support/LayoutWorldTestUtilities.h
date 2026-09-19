// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkStructureTemplate.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Async/LayoutSolveExecution.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Layout/Types/LayoutTypes.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "UObject/UnrealType.h"

namespace PorismLayoutWorldTestUtilities
{
	/** Supplies an authored procedural cavity to the unit-scale harness. Solving must
	 * not infer cavities from saved block edits; encoded graphs keep native DLL
	 * serializer allocations out of the test module's allocator. */
	inline void ConfigureProceduralCavity(UWorldGenDef* Definition, const TCHAR* EncodedTerrain)
	{
		check(Definition && !Definition->WorldBiomes.IsEmpty());
		check(Definition->BaseBlockSize == 1 && Definition->NoiseScale == FVector(1) && Definition->NoiseCoordinateOffset.IsZero());
		FBiomeDualData& Row = Definition->WorldBiomes[0];
		Row.DomainRun = nullptr;
		Row.DomainBP = nullptr;
		Row.Domain = TEXT("AAAAAIA/");
		Row.GenARun = nullptr;
		Row.GenABP = nullptr;
		Row.GenA = EncodedTerrain;
		// This graph owns the fixture density; absent global noise defaults to air.
		Definition->WorldGen.Reset();
		Definition->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(Definition);
		Row.DomainOver = 1.0f;
		if (Row.GenU_Mat1.IsEmpty()) Row.GenU_Mat1.AddDefaulted();
	}

	/** Authors a real unit-scale exterior surface for tests of surface-mode solve/apply.
	 * Reuses the established graph's Z49 exterior; its buried cavity stays below the test site. */
	inline void ConfigureProceduralSurface(UWorldGenDef* Definition, const int32 SurfaceZ)
	{
		ConfigureProceduralCavity(Definition,
			TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAANIC3uQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAADDZCq6AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));
		Definition->NoiseCoordinateOffset.Z = 49 - SurfaceZ;
	}

	/**
	 * Small RAII wrapper that delegates transient chunk-world setup/teardown to the plugin runtime helper.
	 */
	struct FLayoutWorldTestHarness
	{
		AChunkWorldExtended* World = nullptr;
		UChunkWorldLayoutRuntimeComponent* RuntimeComponent = nullptr;

		FLayoutWorldTestHarness() = default;
		FLayoutWorldTestHarness(const FLayoutWorldTestHarness&) = delete;
		FLayoutWorldTestHarness& operator=(const FLayoutWorldTestHarness&) = delete;

		FLayoutWorldTestHarness(FLayoutWorldTestHarness&& Other) noexcept
			: World(Other.World)
			, RuntimeComponent(Other.RuntimeComponent)
			, PlanningController(Other.PlanningController)
		{
			Other.World = nullptr;
			Other.RuntimeComponent = nullptr;
			Other.PlanningController = nullptr;
		}

		FLayoutWorldTestHarness& operator=(FLayoutWorldTestHarness&& Other) noexcept
		{
			if (this != &Other)
			{
				Release();
				World = Other.World;
				RuntimeComponent = Other.RuntimeComponent;
				PlanningController = Other.PlanningController;
				Other.World = nullptr;
				Other.RuntimeComponent = nullptr;
				Other.PlanningController = nullptr;
			}

			return *this;
		}

		~FLayoutWorldTestHarness()
		{
			Release();
		}

		/** Gives automatic-planning fixtures a real tracked character, without invoking discovery or inventing coverage. */
		void TrackPlanningCharacter(const FIntVector& BlockPosition)
		{
			if (!PlanningController)
			{
				UWorld* TestWorld = World->GetWorld();
				PlanningController = TestWorld->SpawnActor<APlayerController>();
				FActorSpawnParameters Spawn;
				Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
				PlanningController->Possess(TestWorld->SpawnActor<ACharacter>(Spawn));
				TestWorld->AddController(PlanningController);
			}
			PlanningController->GetPawn()->SetActorLocation(World->BlockWorldPosToUEWorldPos(BlockPosition));
			// Unit fixtures advance by explicit housekeeping calls, not editor wall-clock sleeps.
			FindFProperty<FFloatProperty>(RuntimeComponent->GetClass(), TEXT("PlanningWindowUpdateIntervalSeconds"))
				->SetPropertyValue_InContainer(RuntimeComponent, 0.0f);
		}

	private:
		APlayerController* PlanningController = nullptr;

		void Release()
		{
			if (PlanningController)
			{
				APawn* Pawn = PlanningController->GetPawn();
				PlanningController->GetWorld()->RemoveController(PlanningController);
				PlanningController->UnPossess();
				if (Pawn) Pawn->Destroy();
				PlanningController->Destroy();
				PlanningController = nullptr;
			}
			if (World != nullptr)
			{
				FLayoutTestWorldSupport::ShutdownTransientChunkWorld(World);
				World->Destroy();
				World = nullptr;
				RuntimeComponent = nullptr;
			}
		}
	};

	/** Creates one transient running chunk world plus the attached layout runtime component. */
	inline FLayoutWorldTestHarness CreateChunkWorldHarness(
		UObject* Outer,
		const FIntVector& ChunkBlockSize = FIntVector(16, 16, 16),
		const FName BiomeRowName = TEXT("LayoutTestBiome"))
	{
		FLayoutWorldTestHarness Harness;
		UWorld* const EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
		check(EditorWorld != nullptr);
		Harness.World = EditorWorld->SpawnActor<AChunkWorldExtended>();
		Harness.RuntimeComponent = Harness.World->GetLayoutRuntimeComponent();
		// Existing fixtures own their centers; an unrelated editor viewport must not add sites.
		// Camera integration tests opt back in explicitly.
		FindFProperty<FBoolProperty>(Harness.RuntimeComponent->GetClass(), TEXT("bFollowEditorCamera"))
			->SetPropertyValue_InContainer(Harness.RuntimeComponent, false);
		Harness.RuntimeComponent->SetLayoutSolveExecutionForTesting(MakeUnique<FSynchronousLayoutSolveExecution>());
		FLayoutTestWorldSupport::InitializeTransientChunkWorld(Harness.World, ChunkBlockSize, BiomeRowName);
		return Harness;
	}

	/** Builds one cached solved-site record with manual placements for realization tests. */
	inline FResolvedLayoutSiteRecord BuildSolvedSiteRecord(
		const FIntVector& SiteCenterBlockWorldPos,
		ULayoutProfileAsset* LayoutProfile,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlacedModule>& Placements)
	{
		FResolvedLayoutSiteRecord SiteRecord;
		SiteRecord.RootSolveId = FLayoutId(*FString::Printf(TEXT("RealizationFixture/%s"), *SiteCenterBlockWorldPos.ToString()));
		SiteRecord.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		SiteRecord.LayoutProfile = LayoutProfile;
		SiteRecord.bLayoutSolved = true;
		FResolvedLayoutSiteRuntimeState RuntimeState;
		RuntimeState.bLayoutSolved = true;
		RuntimeState.CachedApplyability = ELayoutCachedApplyability::Solved;
		SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
		SiteRecord.SolveResult.bSucceeded = true;
		SiteRecord.SolveResult.FootprintSize = FootprintSize;
		SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector::ZeroValue;
		SiteRecord.SolvedArtifactId = SiteRecord.RootSolveId;
		SiteRecord.SolveResult.Placements = Placements;
		// Manual fixture producers must freeze placement authority just like solver output.
		for (FLayoutPlacedModule& Placement : SiteRecord.SolveResult.Placements)
		{
			if (!Placement.ModuleSnapshotId.IsNone() || (!Placement.Module && !Placement.CompositeModule))
				continue;
			const FLayoutModuleSolveSnapshot Snapshot = Placement.CompositeModule
				? FLayoutProfileSolver::BuildCompositeModuleSnapshot(Placement.CompositeModule)
				: FLayoutProfileSolver::BuildModuleSnapshot(Placement.Module);
			Placement.ModuleSnapshotId = Snapshot.SnapshotId;
			Placement.TemplatePath = Snapshot.Template.ToSoftObjectPath();
			Placement.BundleBoundsCells = Snapshot.BoundsCells;
			Placement.OccupiedLocalCells = Snapshot.OccupiedLocalCells;
			for (const auto& Source : Snapshot.GeneratedLocalCellFaceRules)
			{
				auto& Target = Placement.LocalCellFaceRules.AddDefaulted_GetRef();
				Target.LocalCell = Source.LocalCell;
				Target.TemplatePath = Source.TemplatePath;
				Target.RelativeYawRotationSteps = Source.RelativeYawRotationSteps;
				Target.Roles = Source.Roles;
				Target.SupportedCellIntents = Source.SupportedCellIntents;
				Target.ExposedFaceRules = Source.ExposedFaceRules;
			}
		}
		return SiteRecord;
	}

/** Builds a minimal frozen terrain contract from a placement policy for perimeter test
 *  fixtures so realization tests can provide required contract metadata. */
inline FLayoutFrozenTerrainContract BuildPerimeterTestFrozenTerrainContract(
	const FLayoutId ContractId,
	const FIntVector& SiteCenterBlockWorldPos,
	const FIntVector& SharedCellSizeInBlocks,
	const FIntPoint& FootprintSizeInCells,
	const FLayoutWorldBindingPlacementPolicy& PlacementPolicy)
{
	FLayoutFrozenTerrainContract Contract;
	Contract.ContractId = ContractId;
	Contract.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
	Contract.SharedCellSizeInBlocks = SharedCellSizeInBlocks;
	Contract.FootprintSizeInCells = FootprintSizeInCells;

	for (int32 CellY = 0; CellY < FootprintSizeInCells.Y; ++CellY)
	{
		for (int32 CellX = 0; CellX < FootprintSizeInCells.X; ++CellX)
		{
			FLayoutContractActiveCellRecord& ActiveCell = Contract.ActiveCells.AddDefaulted_GetRef();
			ActiveCell.Cell = FIntVector(CellX, CellY, 0);

			const bool bIsBoundary = CellX == 0 || CellY == 0
				|| CellX == FootprintSizeInCells.X - 1 || CellY == FootprintSizeInCells.Y - 1;
			if (bIsBoundary)
			{
				FLayoutTerrainCellContractRecord& CellContract = Contract.CellContracts.AddDefaulted_GetRef();
				CellContract.Cell = FIntVector(CellX, CellY, 0);
				CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
				if (PlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition)
				{
					CellContract.bHasRampTransitionEvidence = true;
				}
				if (PlacementPolicy.TerrainTransition.bAllowFoundationFill)
				{
					CellContract.bHasFoundationFillEvidence = true;
					CellContract.RequiredFoundationDepth = 1;
					CellContract.FoundationMaterial = SinfullMaterial;
				}
			}
		}
	}

	return Contract;
}
}
