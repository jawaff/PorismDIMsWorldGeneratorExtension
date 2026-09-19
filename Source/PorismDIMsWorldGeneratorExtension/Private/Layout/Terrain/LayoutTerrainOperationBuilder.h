// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Contracts/LayoutContractTypes.h"

/** Shared ordinary-root foundation and perimeter-transition write builder. */
namespace LayoutTerrainOperationBuilder
{
	/** Sorts frozen terrain writes into stable block-world order for hashing and replay. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void SortWrites(TArray<FLayoutFrozenTerrainWriteRecord>& InOutWrites);

	/**
	 * Recomputes ordinary-root support obligations at finalized cell bases, then freezes
	 * deterministic writes. Only the lowest admitted cell in each column owns support;
	 * policy depth limits and selected air intervals bound fills. Material hints remain
	 * non-authoritative until realization. Updates cell support metadata with emitted writes.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API void BuildOrdinaryRootWrites(
		FLayoutFrozenTerrainContract& FrozenTerrainContract,
		TConstArrayView<FLayoutFrozenTerrainVoidIntervalSample> SelectedIntervals,
		TConstArrayView<FLayoutTerrainSurfaceSample> PerimeterSurfaceSamples,
		int32 StructuralAlignmentLevel,
		int32 TemplatePlacementZOffsetBlocks,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		TArray<FLayoutFrozenTerrainWriteRecord>& OutWrites);
}
