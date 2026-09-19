// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Frozen request input consumed by contract adapters before inner solve proof. */
struct FLayoutContractModeAdapterInput
{
	/** Final mode selected from packet/finalized request artifacts. */
	FLayoutModePlan ModePlan;

	/** Pointer-free finalized request that owns planned cells and inherited contract data. */
	const FLayoutRegionSolveRequest* SolveRequest = nullptr;

	/** Static contract manifest already built from the finalized request. */
	const FLayoutContractManifest* Manifest = nullptr;
};

/** Dispatches mode-specific adapters without live world, asset, or terrain reads. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractModeAdapter
{
public:
	/** Completes child-owned support/deck topology on already mapped nonbridge cells. Inherits
	 * physical column heights without resampling, changing stages, or applying a second Z shift.
	 * Uses the same bridge/level/seam rules as root preparation; does not select modules or stairs. */
	static bool TryCompleteMappedSteppedTopology(
		const FLayoutRegionSolveRequest& ChildRequest,
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FString& OutFailureReason);

	/** Prepares region assembly inputs, reusing frozen prewarm output when present.
	 * Standard/child finalization must not rerun world-terrain intent search or spend its budget twice. */
	static bool TryPrepareRegionOutput(
		const FLayoutContractModeAdapterInput& Input,
		FLayoutAdapterOutput& OutOutput,
		FString& OutFailureReason);

	/** Runs environment adapter first, then continuation overlay when available. */
	static bool TryRunAdapter(
		const FLayoutContractModeAdapterInput& Input,
		FLayoutAdapterOutput& OutOutput,
		FString& OutFailureReason,
		ELayoutSteppedTerrainFinalizationFailureKind* OutFailureKind = nullptr);
};
