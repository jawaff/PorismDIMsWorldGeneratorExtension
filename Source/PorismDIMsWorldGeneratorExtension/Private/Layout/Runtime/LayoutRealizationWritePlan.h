// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Solver/LayoutProfileSolver.h"

class AChunkWorldCore;

/** Realization source boundary that distinguishes site and connector write plans. */
enum class ELayoutRealizationWritePlanSource : uint8
{
	Site,
	Connector
};

/** Ordered realization write passes enforced before chunk writes execute. */
enum class ELayoutRealizationChunkWritePass : uint8
{
	ExcavationClearance,
	FoundationRampBridgeSupport,
	TemplatePlacement
};

/** One frozen terrain write entry accepted by the realization write-plan boundary. */
struct FLayoutRealizationTerrainWriteEntry
{
	/** Block-world write position copied from the frozen terrain contract. */
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Material payload, or preview hint for deferred fills. */
	int32 Material = 0;

	/** Frozen support sampling obligation, resolved only immediately before terrain writes. */
	bool bResolveMaterialFromTerrain = false;
	FIntVector MaterialSourceBlockWorldPos = FIntVector::ZeroValue;
	int32 MaterialSourceSearchDepthBlocks = 0;

	/** Frozen cell contract that authorizes this terrain write. */
	ELayoutFrozenTerrainCellContract SourceContract = ELayoutFrozenTerrainCellContract::Active;
};

/** Compact local excavation overlap bounds accepted from terrain placement evidence. */
struct FLayoutRealizationExcavationCellBounds
{
	/** Region-local real cell whose template-empty space may be excavated. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Minimum local Z block touched by terrain/template overlap. */
	FLayoutLocalBlockCoord8 OverlapMinLocalZ;

	/** Maximum local Z block touched by terrain/template overlap. */
	FLayoutLocalBlockCoord8 OverlapMaxLocalZ;
};

/** Exact block volume cleared for one removed reserved-open cell. */
struct FLayoutRealizationReservedOpenClearCell
{
	/** Stable selected reservation identity authorizing this clear volume. */
	FLayoutId ReservationId;

	/** Removed reserved-open local cell that owns this volume. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Inclusive block-world minimum cleared by this reservation. */
	FIntVector VolumeMinBlockWorldPos = FIntVector::ZeroValue;

	/** Inclusive block-world maximum cleared by this reservation. */
	FIntVector VolumeMaxBlockWorldPos = FIntVector::ZeroValue;
};

/** Excavation and clearance terrain-write subplan for the first write pass. */
struct FLayoutRealizationExcavationSubplan
{
	/** Flat-clearance or tunnel-excavation writes to replay before support/template writes. */
	TArray<FLayoutRealizationTerrainWriteEntry> Writes;

	/** Per-cell local overlap bounds that scope future template-empty excavation. */
	TArray<FLayoutRealizationExcavationCellBounds> OverlapBounds;

	/** Region-local cells with explicit clearance evidence and no terrain/template overlap. */
	TArray<FIntVector> ClearanceCells;

	/** Exact selected reserved-open cell volumes cleared after frozen terrain writes. */
	TArray<FLayoutRealizationReservedOpenClearCell> ReservedOpenClearCells;

	/** Empty-material writes generated only from ReservedOpenClearCells. */
	TArray<FLayoutRealizationTerrainWriteEntry> ReservedOpenClearWrites;
};

/** Foundation, ramp, and bridge/support terrain-write subplan for the second write pass. */
struct FLayoutRealizationSupportSubplan
{
	/** Active-cell foundation/ramp/perimeter writes authorized by Real active cells. */
	TArray<FLayoutRealizationTerrainWriteEntry> FoundationRampWrites;

	/** Region-local cells with explicit foundation-fill evidence. */
	TArray<FIntVector> FoundationFillCells;

	/** Region-local cells with explicit ramp or perimeter transition evidence. */
	TArray<FIntVector> RampTransitionCells;

	/** Region-local cells with explicit bridge/support evidence. */
	TArray<FIntVector> BridgeSupportCells;

	/** Bridge/support writes authorized by bridge-span cell contracts. */
	TArray<FLayoutRealizationTerrainWriteEntry> BridgeSupportWrites;
};

/** Terrain-write subplan summary owned by the realization write-plan boundary. */
struct FLayoutRealizationTerrainWriteSubplan
{
	/** Frozen terrain contract id authorizing terrain writes. */
	FLayoutId FrozenTerrainContractId;

	/** Number of explicit cell contracts that authorize terrain writes. */
	int32 CellContractCount = 0;

	/** Frozen structural level used to resolve template and reserved-open clear anchors. */
	int32 StructuralAlignmentLevel = 0;

	/** Frozen authored offset shared by template and reserved-open clear anchors. */
	int32 TemplatePlacementZOffsetBlocks = 0;

	/** Number of frozen and reserved-open terrain writes to replay before template placement. */
	int32 TerrainWriteCount = 0;

	/** First-pass excavation/clearance writes. */
	FLayoutRealizationExcavationSubplan Excavation;

	/** Second-pass foundation/ramp/bridge support writes. */
	FLayoutRealizationSupportSubplan Support;
};

/** One template placement entry validated by the realization write-plan boundary. */
struct FLayoutRealizationTemplatePlacementEntry
{
	/** Region-local anchor cell for this template placement. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Source content entry that owns this placement, when available. */
	FName SourceContentEntryId;

	/** Snapshot id selected by worker/proof without reopening live module assets. */
	FLayoutId ModuleSnapshotId;

	/** Soft template path used by game-thread realization after carrier rehydration. */
	FSoftObjectPath TemplatePath;

	/** Yaw rotation in clockwise 90-degree steps around Z. */
	int32 YawRotationSteps = 0;

	/** Pointer-free leaf descriptors for every occupied local cell in a composite bundle. */
	TArray<FLayoutPlacedLocalCellFaceRuleSnapshot> LocalCellDescriptors;

	/** Resolved block-world placement anchor computed from frozen contract authority. */
	FIntVector AcceptedAnchorBlockWorldPos = FIntVector::ZeroValue;
};

/** Template-placement subplan summary owned by the realization write-plan boundary. */
struct FLayoutRealizationTemplatePlacementSubplan
{
	/** Solved artifact id whose placements will be rehydrated and drawn. */
	FLayoutId SolvedArtifactId;

	/** Number of solved template placements expected by the write boundary. */
	int32 PlacementCount = 0;

	/** Pointer-free template entries validated before runtime template writes. */
	TArray<FLayoutRealizationTemplatePlacementEntry> Entries;
};

/** Chunk write-batch summary that pins realization pass order before runtime writes. */
struct FLayoutRealizationChunkWriteBatch
{
	/** Deterministic pass order used by site and connector realization. */
	TArray<ELayoutRealizationChunkWritePass> PassOrder;

	/** Terrain writes covered by the first two passes. */
	int32 TerrainWriteCount = 0;

	/** Template placements covered by the final pass. */
	int32 TemplatePlacementCount = 0;
};

/** Runtime execution state for enforcing write-batch pass order across terrain and template writes. */
struct FLayoutRealizationWritePlanExecutionState
{
	/** True after terrain replay validates and applies through the write-plan boundary. */
	bool bTerrainReplayApplied = false;
};

/**
 * Top-level realization write-plan boundary.
 *
 * This first production seam records the typed subplan envelope and validates
 * solved-artifact plus frozen-terrain authority before runtime terrain/template writes run.
 */
struct FLayoutRealizationWritePlan
{
	/** Stable write-plan id derived from source, solved artifact, and terrain contract identity. */
	FLayoutId WritePlanId;

	/** Deterministic metadata hash used to reject stale cached realization-prep records. */
	int32 WritePlanHash = 0;

	/** Site or connector realization source. */
	ELayoutRealizationWritePlanSource Source = ELayoutRealizationWritePlanSource::Site;

	/** Terrain replay subplan; executes before template placement. */
	FLayoutRealizationTerrainWriteSubplan TerrainWrites;

	/** Template placement subplan; executes after terrain writes. */
	FLayoutRealizationTemplatePlacementSubplan TemplatePlacements;

	/** Chunk write-batch pass order enforced by terrain and template write boundaries. */
	FLayoutRealizationChunkWriteBatch ChunkWriteBatch;

	/** Kind-qualified active cells preserved as write authority. */
	TArray<FLayoutContractActiveCellRecord> ActiveCells;
};

/** Builds and validates top-level realization write plans before any runtime writes execute. */
namespace LayoutRealizationWritePlan
{
	/** Builds from the consumed merged placements and frozen authority, never from regional artifact cardinality. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource Source,
		FLayoutId SolvedArtifactId,
		int32 SolvedArtifactActiveCellCount,
		const FLayoutSolveResult& SolveResult,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FLayoutRealizationWritePlan& OutWritePlan,
		FString& OutFailureReason);

	/** Validates accepted planned-site payload metadata before realization imports or writes use it. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateAcceptedSolvePayloadForRealizationInputs(
		const FLayoutPlannedSiteAcceptedSolvePayload& AcceptedSolvePayload,
		FString& OutFailureReason);

	/** Validates solved-artifact and frozen-contract metadata before realization imports or writes use it. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateAcceptedSolvedArtifactMetadataForRealizationInputs(
		FLayoutId SolvedArtifactId,
		int32 SolvedArtifactActiveCellCount,
		const FLayoutSolveResult& SolveResult,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		const TCHAR* ContextName,
		FString& OutFailureReason);

	/** Validates solve-result write-authority records against Real active cells before realization writes. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateSolveResultWriteAuthority(
		const FLayoutSolveResult& SolveResult,
		const TArray<FLayoutContractActiveCellRecord>& ActiveCells,
		const TCHAR* ContextName,
		FString& OutFailureReason);

#if WITH_AUTOMATION_TESTS
	/** Test-only terrain replay validator; production must apply through ApplyTerrainWriteReplay so execution state is updated atomically. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateTerrainWriteReplayForTests(
		const FLayoutRealizationWritePlan& WritePlan,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FString& OutFailureReason);
#endif

	/** Revalidates frozen authority before replay. An optional synchronous material reader supplies observed-LOD data without changing support search or writes. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplyTerrainWriteReplay(
		AChunkWorldCore* ChunkWorld,
		const FLayoutRealizationWritePlan& WritePlan,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FLayoutRealizationWritePlanExecutionState& ExecutionState,
		FString& OutFailureReason,
		const TFunction<TArray<int32>(const TArray<FIntVector>&)>& MaterialReader = {});

	/** Validates chunk write-batch pass order and counts before terrain or template writes execute. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateChunkWriteBatch(
		const FLayoutRealizationWritePlan& WritePlan,
		FString& OutFailureReason);

	/** Validates one template placement write against the accepted write-plan template subplan and execution state. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateTemplatePlacementWrite(
		const FLayoutRealizationWritePlan& WritePlan,
		const FLayoutPlacedModule& Placement,
		const FLayoutRealizationWritePlanExecutionState& ExecutionState,
		FString& OutFailureReason);
}
