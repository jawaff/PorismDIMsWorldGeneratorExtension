// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutManifestPrewarm.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"

namespace
{
	bool AreTerrainSeamsEnabled(const FLayoutProfileSolveSnapshot& ProfileSnapshot)
	{
		return ProfileSnapshot.bSupportsSteppedTerrainSolve
			&& ProfileSnapshot.bEnableTerrainSeams;
	}

	bool ValidateTerrainSeamOutputForProfile(
		const FLayoutProfileSolveSnapshot& ProfileSnapshot,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		FString& OutFailureReason)
	{
		if (AreTerrainSeamsEnabled(ProfileSnapshot))
		{
			return true;
		}
		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			if (PlannedCell.TerrainSeamFaceMask != 0
				|| PlannedCell.VerticalAccessLandingContactMask != 0
				|| PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam)
			{
				OutFailureReason = FString::Printf(
					TEXT("Terrain-seam-disabled prewarm output contains seam metadata at cell %s."),
					*PlannedCell.Cell.ToString());
				return false;
			}
		}
		return true;
	}

	/** Rejected adapter evidence remains preview-only; it never becomes a worker manifest. */
	FLayoutManifestPrewarmResult RejectManifestPrewarm(const TCHAR* Reason, const FLayoutAdapterOutput* Preview = nullptr)
	{
		FLayoutManifestPrewarmResult Result;
		Result.bTerminalRejection = true;
		Result.FailureReason = Reason;
		if (Preview != nullptr)
		{
			Result.bHasRejectedAdapterPreview = true;
			Result.RejectedAdapterPreview = *Preview;
		}
		return Result;
	}

	/** Publishes one adapter result as the immutable manifest consumed by later lifecycle stages. */
	void CompleteManifestPrewarmWithAdapterOutput(
		const FLayoutManifestPrewarmInput& Input,
		const FLayoutAdapterOutput& AdapterOutput,
		const FIntPoint& FinalizedFootprintSize,
		FLayoutManifestPrewarmResult& OutResult)
	{
		FString SeamOutputFailureReason;
		if (!ValidateTerrainSeamOutputForProfile(
			Input.FrozenRequestManifest.ProfileSnapshot,
			AdapterOutput.PlannedCells,
			SeamOutputFailureReason))
		{
			OutResult = RejectManifestPrewarm(*SeamOutputFailureReason);
			return;
		}

		FLayoutWorkerSolveRequestManifest FinalizedManifest = Input.FrozenRequestManifest;
		FinalizedManifest.PlannedCells = AdapterOutput.PlannedCells;
		FinalizedManifest.bUseSuppliedPlannedCells = !FinalizedManifest.PlannedCells.IsEmpty();
		FinalizedManifest.bHasFinalizedSteppedTerrainIntents = AdapterOutput.bHasFinalizedSteppedTerrainIntents;
		FinalizedManifest.VerticalAccessHostGroups = AdapterOutput.VerticalAccessHostGroups;
		FinalizedManifest.PrecomputedAdapterDiagnostics = AdapterOutput.Diagnostics;
		FinalizedManifest.SteppedTerrainSupportMap = AdapterOutput.SteppedTerrainSupportMap;
		FinalizedManifest.QualifiedEntryCells = AdapterOutput.QualifiedEntryCells;
		FinalizedManifest.bHasQualifiedEntryCells = !FinalizedManifest.QualifiedEntryCells.IsEmpty();
		// Deferred stepped reservations are selected during prewarm, so publish their realization authority with the finalized manifest.
		FinalizedManifest.ReservedOpenTerrainReservations =
			AdapterOutput.FrozenTerrainContract.ReservedOpenTerrainReservations;
		if (Input.bHasFrozenTerrainBiomeAdapterInput)
		{
			FinalizedManifest.bHasFrozenTerrainBiomeAdapterInput = true;
			FinalizedManifest.FrozenTerrainBiomeAdapterInput = Input.FrozenTerrainBiomeAdapterInput;
		}
		if (FinalizedManifest.FootprintSize == FIntPoint::ZeroValue
			&& FinalizedFootprintSize != FIntPoint::ZeroValue)
		{
			FinalizedManifest.FootprintSize = FinalizedFootprintSize;
		}
		if (FinalizedManifest.FootprintSize == FIntPoint::ZeroValue
			&& FinalizedManifest.PlannedCells.Num() > 0)
		{
			int32 MaxCellX = 0;
			int32 MaxCellY = 0;
			for (const FLayoutPlannedCell& Cell : FinalizedManifest.PlannedCells)
			{
				MaxCellX = FMath::Max(MaxCellX, Cell.Cell.X);
				MaxCellY = FMath::Max(MaxCellY, Cell.Cell.Y);
			}
			FinalizedManifest.FootprintSize = FIntPoint(MaxCellX + 1, MaxCellY + 1);
		}

		OutResult.bHasPrecomputedAdapterOutput = true;
		OutResult.PrecomputedAdapterOutput = AdapterOutput;
		OutResult.bHasFrozenRequestManifest = true;
		OutResult.FrozenRequestManifest = MoveTemp(FinalizedManifest);
		OutResult.bHasDerivedStructuralContract = true;
		OutResult.FinalizedManifest = OutResult.FrozenRequestManifest;
		OutResult.DerivedEntryCells = OutResult.FrozenRequestManifest.QualifiedEntryCells;
		FString ExecutionFailure;
		if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::PrewarmStages, ExecutionFailure))
		{
			OutResult = RejectManifestPrewarm(*ExecutionFailure, &AdapterOutput);
		}
	}
}

FLayoutManifestPrewarmResult FLayoutManifestPrewarmSequence::RunPrewarm(const FLayoutManifestPrewarmInput& Input)
{
	PORISM_LAYOUT_PROFILE_SCOPE(Layout_Prewarm, STAT_PorismLayout_Prewarm);
	if (Input.PrewarmId.IsNone())
	{
		return RejectManifestPrewarm(TEXT("Manifest prewarm requires a stable prewarm id."));
	}

	if (!Input.bHasFrozenRequestManifest)
	{
		return RejectManifestPrewarm(TEXT("Manifest prewarm requires an upstream frozen request manifest producer."));
	}

	// One synchronous prewarm invocation owns adapter preparation and both terrain
	// alternatives. Nested authority calls borrow it; no ledger enters frozen data.
	const FLayoutSolverExecutionSettings& Settings = Input.bHasPreSubmitSnapshot
		? Input.PreSubmitSnapshot.WorkerSolvePacket.RequestManifest.ExecutionSettings
		: Input.FrozenRequestManifest.ExecutionSettings;
	LayoutSolveExecution::FScope ExecutionScope(Settings.MaxSolveDurationSeconds, Settings.MaxCandidateAttempts);
	FString WorkerSafeFailureReason;
	if (!Input.FrozenRequestManifest.ValidateNoLiveObjectCarriers(WorkerSafeFailureReason))
	{
		FLayoutManifestPrewarmResult Result;
		Result.bTerminalRejection = true;
		Result.FailureReason = WorkerSafeFailureReason.IsEmpty()
			? TEXT("Manifest prewarm rejected a non-worker-safe request manifest.")
			: WorkerSafeFailureReason;
		return Result;
	}

	if (Input.Kind == ELayoutManifestPrewarmKind::Continuation)
	{
		if (!Input.FrozenRequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable)
		{
			return RejectManifestPrewarm(TEXT("Continuation manifest prewarm requires a profile snapshot with traversal reachability enabled."));
		}
		if (Input.FrozenRequestManifest.RootContinuationSelection.ResolvedEntryLevel == INDEX_NONE)
		{
			return RejectManifestPrewarm(TEXT("Continuation manifest prewarm requires resolved continuation entry metadata."));
		}
	}

	FString ExecutionFailure;
	if (!LayoutSolveExecution::Charge(&LayoutSolveExecution::FWorkLedger::PrewarmStages, ExecutionFailure))
	{
		const auto& Packet = Input.PreSubmitSnapshot.WorkerSolvePacket;
		return RejectManifestPrewarm(*ExecutionFailure,
			Input.bHasPreSubmitSnapshot && Packet.bHasPrecomputedAdapterOutput ? &Packet.PrecomputedAdapterOutput : nullptr);
	}
	FLayoutManifestPrewarmResult Result;

	// Descriptor production may already have finalized this exact packet.
	// Reuse its frozen output instead of rerunning terrain and VA planning.
	if (Input.bHasPreSubmitSnapshot
		&& Input.PreSubmitSnapshot.WorkerSolvePacket.bHasPrecomputedAdapterOutput)
	{
		const FLayoutAdapterOutput& AdapterOutput =
			Input.PreSubmitSnapshot.WorkerSolvePacket.PrecomputedAdapterOutput;
		if (!AdapterOutput.bSucceeded)
		{
			return RejectManifestPrewarm(TEXT("Manifest prewarm received an unsuccessful precomputed adapter output."));
		}
		CompleteManifestPrewarmWithAdapterOutput(
			Input,
			AdapterOutput,
			Input.FrozenRequestManifest.FootprintSize,
			Result);
		return Result;
	}

	// Run the structural-contract planner when the call site packed a
	// pre-submit snapshot and the manifest needs planned cells (either
	// because terrain evidence is available for elevation-aware derivation,
	// or because PlannedCells are empty and need to be derived from FootprintSize).
	const bool bNeedsPlannedCells = Input.bHasFrozenRequestManifest
		&& Input.FrozenRequestManifest.PlannedCells.IsEmpty();
	if (Input.bHasPreSubmitSnapshot && (Input.bHasFrozenTerrainBiomeAdapterInput || bNeedsPlannedCells))
	{
		// Populate a solve request from the manifest without running contract
		// validation (ApplyStandardContractAdapter).  The adapter will produce
		// active cells — calling FinalizeRequestFromPacket before the adapter
		// runs would fail because no active cells exist yet.
		FLayoutRegionSolveRequest PrecomputeRequest;
		Input.PreSubmitSnapshot.WorkerSolvePacket.RequestManifest.PopulateSolveRequest(PrecomputeRequest);
		PrecomputeRequest.Seed = Input.PreSubmitSnapshot.WorkerSolvePacket.SolveSeed;
		PrecomputeRequest.TemplatePlacementZOffsetBlocks =
			Input.PreSubmitSnapshot.WorkerSolvePacket.RuntimeSnapshot.TemplatePlacementZOffsetBlocks;
		PrecomputeRequest.RootPlacementKind =
			Input.PreSubmitSnapshot.WorkerSolvePacket.RuntimeSnapshot.PlacementKind;
		PrecomputeRequest.WorldBindingId =
			Input.PreSubmitSnapshot.WorkerSolvePacket.RuntimeSnapshot.BindingId;
		PrecomputeRequest.WorldBindingPlacementPolicy =
			Input.PreSubmitSnapshot.WorkerSolvePacket.RuntimeSnapshot.PlacementPolicy;
		PrecomputeRequest.RootContinuationSelection =
			Input.PreSubmitSnapshot.WorkerSolvePacket.RuntimeSnapshot.ContinuationSelection;

		// Apply selected mode plan and terrain evidence identity.
		if (Input.PreSubmitSnapshot.WorkerSolvePacket.bHasSelectedModePlan)
		{
			PrecomputeRequest.bHasSelectedModePlan = true;
			PrecomputeRequest.SelectedModePlan =
				Input.PreSubmitSnapshot.WorkerSolvePacket.SelectedModePlan;
		}
		if (PrecomputeRequest.bHasFrozenTerrainBiomeAdapterInput)
		{
			PrecomputeRequest.FrozenTerrainBiomeAdapterInput.ModePlanId =
				PrecomputeRequest.SelectedModePlan.ModePlanId;
			if (PrecomputeRequest.FrozenTerrainBiomeAdapterInput.ArtifactId.IsNone())
			{
				PrecomputeRequest.FrozenTerrainBiomeAdapterInput.ArtifactId = FLayoutId(*FString::Printf(
					TEXT("TerrainAdapter.%s.%s"),
					*PrecomputeRequest.SelectedModePlan.ModePlanId.ToString(),
					*PrecomputeRequest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos.ToString()));
			}
			PrecomputeRequest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos =
				PrecomputeRequest.SelectedModePlan.SiteCenterBlockWorldPos;
		}
		// Overlay terrain evidence from the prewarm input when the snapshot's
		// manifest lacks it (e.g., PlanningWindow descriptor-store path where
		// the snapshot and shared packet carry different manifests).
		else if (Input.bHasFrozenTerrainBiomeAdapterInput)
		{
			PrecomputeRequest.bHasFrozenTerrainBiomeAdapterInput = true;
			PrecomputeRequest.FrozenTerrainBiomeAdapterInput =
				Input.FrozenTerrainBiomeAdapterInput;
			PrecomputeRequest.FrozenTerrainBiomeAdapterInput.ModePlanId =
				PrecomputeRequest.SelectedModePlan.ModePlanId;
			PrecomputeRequest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos =
				PrecomputeRequest.SelectedModePlan.SiteCenterBlockWorldPos;
		}

		// Derive FootprintSize from terrain evidence when the manifest left it zero
		// (e.g., ObservedChunk / ExplicitRoot paths that bypass the profile assignment).
		if (PrecomputeRequest.FootprintSize == FIntPoint::ZeroValue
			&& PrecomputeRequest.bHasFrozenTerrainBiomeAdapterInput)
		{
			const FLayoutFrozenTerrainBiomeAdapterInput& Terrain =
				PrecomputeRequest.FrozenTerrainBiomeAdapterInput;
			if (Terrain.FootprintSizeInBlocks.X > 0 && Terrain.FootprintSizeInBlocks.Y > 0)
			{
				const FIntVector SharedCellSize = PrecomputeRequest.ContentSetSnapshot.SharedCellSizeInBlocks != FIntVector::ZeroValue
					? PrecomputeRequest.ContentSetSnapshot.SharedCellSizeInBlocks
					: PrecomputeRequest.ModuleCatalog.SharedCellSizeInBlocks;
				const int32 CellSizeX = FMath::Max(1, SharedCellSize.X);
				const int32 CellSizeY = FMath::Max(1, SharedCellSize.Y);
				PrecomputeRequest.FootprintSize.X = FMath::Max(1,
					Terrain.FootprintSizeInBlocks.X / CellSizeX);
				PrecomputeRequest.FootprintSize.Y = FMath::Max(1,
					Terrain.FootprintSizeInBlocks.Y / CellSizeY);
			}
			else if (Terrain.SurfaceSamples.Num() > 0)
			{
				PrecomputeRequest.FootprintSize = FIntPoint(1, 1);
			}
		}

		// Continuation planned cells are derived at the call site from the
		// scouted connector path (game-thread path tracing through sampled
		// terrain).  The prewarm consumes them through the adapter, same as
		// root layouts — no span-cell derivation needed here.

		// Run the environment adapter.  For terrain modes with empty PlannedCells
		// the adapter generates cells from stepped support samples or footprint;
		// for StandardRegion it generates a flat Boundary/Interior grid.
		// The adapter also produces ActiveCells, bridge cells, entry cells, and
		// a frozen terrain contract — the full output the solve needs.
		FString PrecomputeFailureReason;
		FLayoutAdapterOutput RejectedAdapterPreview;
		if (FLayoutContractPipeline::TryPrecomputeAdapterOutput(
				PrecomputeRequest,
				PrecomputeFailureReason,
				&RejectedAdapterPreview))
		{
			if (Input.Kind == ELayoutManifestPrewarmKind::Root)
			{
				FLayoutRegionSolveRequest PreparedAuthorityRequest;
				FString AuthorityFailureReason;
				const bool bAllowsFlatRecovery = PrecomputeRequest.bHasSelectedModePlan
					&& PrecomputeRequest.SelectedModePlan.bUsesSteppedTerrainTopology
					&& PrecomputeRequest.WorldBindingPlacementPolicy.TerrainTransition
						.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible;
				bool bPreparedAuthority;
				{
					// Required-child boundary search must leave work/time for flat recovery.
					// This is a bounded attempt, not a declaration of terrain infeasibility.
					TOptional<LayoutSolveExecution::FOptionalImprovementScope> SteppedAllowance;
					if (bAllowsFlatRecovery && !PrecomputeRequest.bUseChildLocalFlatFallback)
						SteppedAllowance.Emplace(FLayoutContractPipeline::CanTryChildLocalFlatFallback(PrecomputeRequest) ? 4 : 2);
					bPreparedAuthority = LayoutRegionScheduleSolverFacade::
						TryPrepareRequiredChildParentAuthorityWitness(
							PrecomputeRequest,
							PreparedAuthorityRequest,
							AuthorityFailureReason,
							nullptr,
							nullptr);
				}
				if (!bPreparedAuthority)
				{
					// The local ceiling has been removed; hard expiry/cancellation still wins.
					if (!LayoutSolveExecution::Checkpoint(AuthorityFailureReason))
					{
						return RejectManifestPrewarm(*AuthorityFailureReason);
					}
					if (!bAllowsFlatRecovery)
					{
						Result.bTerminalRejection = true;
						Result.FailureReason = AuthorityFailureReason.IsEmpty()
							? TEXT("Manifest prewarm found no bounded required-child parent-authority witness.")
							: AuthorityFailureReason;
						return Result;
					}

					FLayoutRegionSolveRequest ChildFlatRequest;
					bool bPreparedChildFlat = false;
					if (FLayoutContractPipeline::TryBuildChildLocalFlatFallbackRequest(
						PrecomputeRequest, ChildFlatRequest))
					{
						FString ChildFlatFailure;
						// Child-local recovery has priority over whole-root flattening and uses
						// the retained allowance. A later root retry is allowed only if budget remains.
						bPreparedChildFlat = LayoutRegionScheduleSolverFacade::
							TryPrepareRequiredChildParentAuthorityWitness(ChildFlatRequest,
								PreparedAuthorityRequest, ChildFlatFailure, nullptr, nullptr);
						if (!LayoutSolveExecution::Checkpoint(ChildFlatFailure))
							return RejectManifestPrewarm(*ChildFlatFailure);
						if (bPreparedChildFlat)
						{
							PreparedAuthorityRequest.PrecomputedAdapterDiagnostics.AddDefaulted_GetRef().Detail =
								FString::Printf(TEXT("Child-local flat recovery retains stepped parent terrain. Original rejection: %s"),
									*AuthorityFailureReason);
						}
						else
						{
							AuthorityFailureReason += FString::Printf(
								TEXT(" Child-local flat preparation failed: %s"), *ChildFlatFailure);
						}
					}
					FLayoutRegionSolveRequest FlatRequest;
					FString FlatFailureReason;
					if (!bPreparedChildFlat && (!FLayoutContractPipeline::
							TryBuildFlatFallbackRequestAfterSteppedChildPreparationFailure(
								PrecomputeRequest,
								AuthorityFailureReason,
								FlatRequest,
								FlatFailureReason)
						|| !LayoutRegionScheduleSolverFacade::
							TryPrepareRequiredChildParentAuthorityWitness(
								FlatRequest,
								PreparedAuthorityRequest,
								FlatFailureReason,
								nullptr,
								nullptr)))
					{
						Result.bTerminalRejection = true;
						Result.FailureReason = FString::Printf(
							TEXT("Stepped required-child preparation failed: %s Flat fallback failed: %s"),
							*AuthorityFailureReason,
							*FlatFailureReason);
						return Result;
					}
				}
				PrecomputeRequest = MoveTemp(PreparedAuthorityRequest);
			}

			// Refresh stepped terrain assertions after the adapter produced
			// stepped support data, so the solved request carries assertions
			// that match the adapter's output (not the pre-prewarm state).
			LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(PrecomputeRequest);

			FLayoutAdapterOutput AdapterOutput;
			AdapterOutput.bSucceeded = true;
			AdapterOutput.ModePlan = PrecomputeRequest.SelectedModePlan;
			AdapterOutput.PlannedCells = PrecomputeRequest.PlannedCells;
			AdapterOutput.bHasFinalizedSteppedTerrainIntents = PrecomputeRequest.bHasFinalizedSteppedTerrainIntents;
			AdapterOutput.VerticalAccessHostGroups = PrecomputeRequest.VerticalAccessHostGroups;
			AdapterOutput.Diagnostics = PrecomputeRequest.PrecomputedAdapterDiagnostics;
			AdapterOutput.ActiveCells = PrecomputeRequest.PrecomputedActiveCells;
			AdapterOutput.FrozenTerrainContract = PrecomputeRequest.PrecomputedFrozenTerrainContract;
			AdapterOutput.SteppedTerrainSupportMap = PrecomputeRequest.PrecomputedSteppedTerrainSupportMap;
			AdapterOutput.QualifiedEntryCells = PrecomputeRequest.QualifiedEntryCells;
			CompleteManifestPrewarmWithAdapterOutput(
				Input,
				AdapterOutput,
				PrecomputeRequest.FootprintSize,
				Result);
			if (Result.bHasFrozenRequestManifest)
			{
				Result.FrozenRequestManifest.PreparedChildPlacementHints =
					PrecomputeRequest.PreparedChildPlacementHints;
				Result.FinalizedManifest.PreparedChildPlacementHints =
					PrecomputeRequest.PreparedChildPlacementHints;
				Result.FrozenRequestManifest.bUseChildLocalFlatFallback = PrecomputeRequest.bUseChildLocalFlatFallback;
				Result.FinalizedManifest.bUseChildLocalFlatFallback = PrecomputeRequest.bUseChildLocalFlatFallback;
			}
		}
		else
		{
			Result.bTerminalRejection = true;
			Result.FailureReason = PrecomputeFailureReason.IsEmpty()
				? TEXT("Manifest prewarm could not finalize the adapter contract.")
				: PrecomputeFailureReason;
			// Rejected topology is editor-only evidence. It never becomes a solve contract.
			if (!RejectedAdapterPreview.PlannedCells.IsEmpty())
			{
				Result.bHasRejectedAdapterPreview = true;
				Result.RejectedAdapterPreview = MoveTemp(RejectedAdapterPreview);
			}
			return Result;
		}
	}
	else
	{
		return RejectManifestPrewarm(TEXT("Manifest prewarm requires a pre-submit snapshot or precomputed adapter output."));
	}

	return Result;
}

bool FLayoutManifestPrewarmSequence::TryBuildPreflightInput(
	const FLayoutManifestPrewarmResult& PrewarmResult,
	const ELayoutManifestPrewarmKind PrewarmKind,
	FLayoutBackgroundAdmissibilityPreflightInput& OutPreflightInput,
	FString& OutFailureReason)
{
	OutPreflightInput = FLayoutBackgroundAdmissibilityPreflightInput();
	OutFailureReason.Reset();

	if (PrewarmResult.bTerminalRejection)
	{
		OutFailureReason = PrewarmResult.FailureReason.IsEmpty()
			? TEXT("Manifest prewarm ended in terminal rejection.")
			: PrewarmResult.FailureReason;
		return false;
	}

	if (!PrewarmResult.bHasFrozenRequestManifest)
	{
		OutFailureReason = TEXT("Manifest prewarm did not produce a frozen request manifest.");
		return false;
	}

	OutPreflightInput.Kind = PrewarmKind == ELayoutManifestPrewarmKind::Continuation
		? ELayoutBackgroundAdmissibilityPreflightKind::Continuation
		: ELayoutBackgroundAdmissibilityPreflightKind::Root;
	OutPreflightInput.RequestManifest = PrewarmResult.FrozenRequestManifest;
	return true;
}
