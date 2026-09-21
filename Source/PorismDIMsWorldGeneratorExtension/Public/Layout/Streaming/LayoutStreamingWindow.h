// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Types/LayoutTypes.h"

struct FLayoutFrozenTerrainContract;

enum class ELayoutChunkScreening : uint8 { Pending, Uncertain, Eligible, Irrelevant, Settled };

/** Compact current-loaded metadata only; no candidate arrays, noise captures or solve payloads. */
struct FLayoutLoadedChunkState
{
	bool bCreated = false;
	ELayoutChunkScreening Screening = ELayoutChunkScreening::Pending;
	uint64 Lifetime = 0;
	uint64 InputRevision = 0;
	FIntPoint ScanStartArea = FIntPoint::ZeroValue;
	int64 ScanOffset = 0;
	bool bScanStarted = false;
};

/** Current loaded observations for one native LOD. Array position is the native detail-level index. */
struct FLayoutLoadedChunkLayer
{
	FIntVector ChunkSizeInBlocks = FIntVector::ZeroValue;
	/** Updated never clears an existing Created lifetime; unloading removes its entire entry. */
	TMap<FIntVector, FLayoutLoadedChunkState> Chunks;
};

/** Ephemeral creation authority. Discovery completion preserves it; native unload/recreation invalidates it. */
struct FLayoutCreatedChunkIdentity
{
	int32 DetailLevel = INDEX_NONE;
	FIntVector Origin = FIntVector::ZeroValue;
	uint64 Lifetime = 0;

	bool IsCurrent(const TConstArrayView<FLayoutLoadedChunkLayer> Layers) const
	{
		if (Lifetime == 0 || !Layers.IsValidIndex(DetailLevel)) return false;
		const FLayoutLoadedChunkState* Chunk = Layers[DetailLevel].Chunks.Find(Origin);
		return Chunk && Chunk->bCreated && Chunk->Lifetime == Lifetime;
	}
};

/**
 * Streaming-window helpers for deterministic chunk-origin coverage checks.
 * These helpers keep the realization gating math testable without requiring a
 * live chunk world instance.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutStreamingWindow
{
public:
	/** Converts one block-world position into the owning chunk origin for the supplied chunk size. */
	static FIntVector BlockWorldPosToChunkOrigin(const FIntVector& BlockWorldPos, const FIntVector& ChunkSizeInBlocks);

	/**
	 * Snaps one world-facing site center onto the global layout cell lattice.
	 * The lattice is anchored at block-world origin `(0,0,0)`, which also
	 * matches the shared chunk-grid origin used by Porism world generation.
	 */
	static FIntVector SnapSiteCenterBlockWorldPosToCellLattice(
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks);

	/** Computes the footprint origin used when converting solved site cells into block-world anchors. */
	static FIntVector ComputeFootprintMinBlockWorldPos(
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntPoint& FootprintSizeInCells,
		const FIntVector& SharedCellSizeInBlocks);

	/** Computes the block-world anchor for one placed site module. */
	static FIntVector ComputeSitePlacementAnchorBlockWorldPos(
		const FResolvedLayoutSiteRecord& SiteRecord,
		const FLayoutPlacedModule& Placement,
		const FIntVector& SharedCellSizeInBlocks);

	/** Computes physical minimum block position of connector cell zero from its center-based path origin. */
	static FIntVector ComputeConnectorFootprintMinBlockWorldPos(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FIntVector& SharedCellSizeInBlocks);

	/** Collects finest-detail chunk origins touched by one solved site. */
	static TSet<FIntVector> CollectRequiredChunkOriginsForSite(
		const FResolvedLayoutSiteRecord& SiteRecord,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntVector& ChunkSizeInBlocks);

	/** Extends existing readiness coverage with frozen write targets and deferred support reads. */
	static void AddFrozenTerrainChunkOrigins(
		const FLayoutFrozenTerrainContract& Contract,
		const FIntVector& ChunkSizeInBlocks,
		TSet<FIntVector>& InOutChunkOrigins);

	/** Expands one solved placement into all occupied-cell anchors for chunk gating. */
	static TArray<FIntVector> BuildPlacementAnchorBlockWorldPositions(
		const FLayoutPlacedModule& Placement,
		const FIntVector& RootAnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks);

	/** Returns the finest observed layer covering a base-block position, or INDEX_NONE.
	 * Created authority is tested on that layer only; restored fine data never falls through to fresh coarse data. */
	static int32 FindLoadedLayerAtPosition(
		const FIntVector& Position,
		TConstArrayView<FLayoutLoadedChunkLayer> Layers,
		bool bRequireCreated = false);

	/** Tests the complete inclusive box against the loaded LOD union, without expanding coarse chunks into fine tiles.
	 * Required Created authority follows finest-available precedence over every covered sub-box. */
	static bool IsBlockBoxCovered(
		const FIntVector& Min,
		const FIntVector& Max,
		TConstArrayView<FLayoutLoadedChunkLayer> Layers,
		bool bRequireCreated = false);

	/** Returns true when every required chunk origin has already been observed. */
	static bool AreRequiredChunkOriginsObserved(
		const TSet<FIntVector>& RequiredChunkOrigins,
		const TSet<FIntVector>& ObservedChunkOrigins);
};
