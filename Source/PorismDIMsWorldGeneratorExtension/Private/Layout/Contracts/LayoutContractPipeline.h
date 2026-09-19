// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Assembles prepared region authority and validates frozen terrain writes for publication and realization. */
class AChunkWorldCore;
struct FLayoutWorldBindingTerrainFitResult;

class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractPipeline
{
public:
	/** Builds the static manifest portion from an existing legacy solve request. */
	static FLayoutContractManifest BuildManifestFromSolveRequest(const FLayoutRegionSolveRequest& SolveRequest);

	/** Builds the initial mode plan from an existing legacy solve request and snapped site center. */
	static FLayoutModePlan BuildModePlanFromSolveRequest(
		const FLayoutRegionSolveRequest& SolveRequest,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 WorldSeed);

	/** Returns private diagnostic category names in stable failure-order for tests and telemetry. */
	static TArray<FName> GetStableDiagnosticCategoryNames();

	/** Builds the stable id for one mode plan from value data only. */
	static FLayoutId BuildModePlanId(const FLayoutModePlan& ModePlan);

	/** Builds the stable id for one placement shift from value data only. */
	static FLayoutId BuildPlacementShiftId(const FIntVector& PlacementShiftCells);

	/** Builds a stable id for one unordered local-cell mask. */
	static FLayoutId BuildCellMaskId(const FString& Prefix, const TArray<FIntVector>& Cells);

	/** Builds a stable id for one unordered kind-qualified active-cell collection. */
	static FLayoutId BuildActiveCellMaskId(const FString& Prefix, const TArray<FLayoutContractActiveCellRecord>& ActiveCells);

	/** Validates kind-qualified active cells before CSP or realization-prep may consume them. */
	static bool ValidateActiveCellRecords(
		const TArray<FLayoutContractActiveCellRecord>& ActiveCells,
		FString& OutFailureReason);

	/** Builds a stable id for planned-cell stage/intent mapping. */
	static FLayoutId BuildStageMapId(const TArray<FLayoutPlannedCell>& PlannedCells);

	/** Builds a stable id for terrain-write artifact content. */
	static FLayoutId BuildTerrainWriteArtifactId(const FLayoutFrozenTerrainContract& FrozenTerrainContract);

	/** Assembles validated, prepared adapter evidence. Never selects a mode or executes terrain preparation. */
	static bool TryBuildPreparedRegionContract(
		const FLayoutContractManifest& Manifest,
		const FIntPoint& FootprintSize,
		const FLayoutAdapterOutput& AdapterOutput,
		FLayoutRegionContract& OutContract,
		FString& OutFailureReason);

	/**
	 * Runs the environment adapter on a finalized request before solve and populates pre-computed fields.
	 * Returns adapter-owned diagnostic topology through OutRejectedAdapterPreview when finalization rejects it.
	 */
	static bool TryPrecomputeAdapterOutput(
		FLayoutRegionSolveRequest& InOutSolveRequest,
		FString& OutFailureReason,
		FLayoutAdapterOutput* OutRejectedAdapterPreview = nullptr);

	/** Whether authored recovery policy permits a first ground-child flat-plane attempt. */
	static bool CanTryChildLocalFlatFallback(const FLayoutRegionSolveRequest& Request);

	/** Starts a ground-child flat-plane recovery attempt without flattening parent terrain.
	 * Clears placement hints; all changed child boundaries and parent support require new proof. */
	static bool TryBuildChildLocalFlatFallbackRequest(
		const FLayoutRegionSolveRequest& SteppedRequest,
		FLayoutRegionSolveRequest& OutRequest);

	/** Rebuilds fresh flat adapter authority after terrain-sensitive stepped child preparation fails. */
	static bool TryBuildFlatFallbackRequestAfterSteppedChildPreparationFailure(
		const FLayoutRegionSolveRequest& SteppedRequest,
		const FString& SteppedFailureReason,
		FLayoutRegionSolveRequest& OutFlatRequest,
		FString& OutFailureReason);

	/** Applies a validated region contract back onto the existing solve request boundary, failing closed on invalid active-cell carriers. */
	static bool TryApplyRegionContractToSolveRequest(
		const FLayoutRegionContract& Contract,
		FLayoutRegionSolveRequest& InOutSolveRequest,
		FString& OutFailureReason);

	/** Applies a region contract back onto the existing solve request boundary after validation. */
	static void ApplyRegionContractToSolveRequest(
		const FLayoutRegionContract& Contract,
		FLayoutRegionSolveRequest& InOutSolveRequest);

	// Legacy BuildFrozenTerrainContractFromTerrainFit removed (C5.1). All frozen contracts now produced by LayoutContractModeAdapter.

	/** Applies authorized geometry on the game thread after the caller's fresh-chunk gate.
	 * Samples all deferred fill support materials before the first write; leaves frozen
	 * contracts unchanged and fails without writing if any required material is unavailable.
	 * A supplied reader selects observed LOD data; without one, direct callers use native finest-layer reads. */
	static bool ApplyFrozenTerrainContract(
		AChunkWorldCore* ChunkWorld,
		const FLayoutFrozenTerrainContract& FrozenTerrainContract,
		FString& OutFailureReason,
		const TFunction<TArray<int32>(const TArray<FIntVector>&)>& MaterialReader = {});
};
