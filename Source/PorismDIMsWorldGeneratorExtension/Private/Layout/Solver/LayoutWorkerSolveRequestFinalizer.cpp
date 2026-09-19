// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"

#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Contracts/LayoutManifestSnapshots.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Contracts/LayoutContractModeAdapter.h"
#include "Layout/Diagnostics/LayoutGenerationProfiling.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"

namespace
{
	bool ValidatePacketRequestData(
		const FLayoutWorkerSolvePacket& WorkerSolvePacket,
		FString& OutFailureReason)
	{
		if (!WorkerSolvePacket.ValidateNoLiveObjectCarriers(OutFailureReason))
		{
			return false;
		}

		if (WorkerSolvePacket.RuntimeSnapshot.BindingId != WorkerSolvePacket.RequestManifest.RootPlacementPolicyId
			&& WorkerSolvePacket.RequestManifest.RootPlacementPolicyId != NAME_None)
		{
			// Direct-root packets may use tool-owned placement policy ids instead of binding ids.
		}

		if (WorkerSolvePacket.SolveSeed != WorkerSolvePacket.RequestManifest.CapturedSeed)
		{
			OutFailureReason = TEXT("Worker packet solve seed does not match packet request manifest seed.");
			return false;
		}

		if (WorkerSolvePacket.RuntimeSnapshot.TemplatePlacementZOffsetBlocks != WorkerSolvePacket.RequestManifest.CapturedTemplatePlacementZOffsetBlocks)
		{
			OutFailureReason = TEXT("Worker packet template placement offset does not match packet request manifest offset.");
			return false;
		}

		if (WorkerSolvePacket.RuntimeSnapshot.PlacementKind != WorkerSolvePacket.RequestManifest.CapturedRootPlacementKind)
		{
			OutFailureReason = TEXT("Worker packet placement kind does not match packet request manifest placement kind.");
			return false;
		}

		return true;
	}

	void MergeUniqueCells(const TArray<FIntVector>& SourceCells, TArray<FIntVector>& InOutCells)
	{
		for (const FIntVector& Cell : SourceCells)
		{
			InOutCells.AddUnique(Cell);
		}
	}

	void MergeUniqueRouteConstraints(
		const TArray<FLayoutRouteConstraintRecord>& SourceConstraints,
		TArray<FLayoutRouteConstraintRecord>& InOutConstraints)
	{
		for (const FLayoutRouteConstraintRecord& Constraint : SourceConstraints)
		{
			const bool bAlreadyPresent = InOutConstraints.ContainsByPredicate(
				[&Constraint](const FLayoutRouteConstraintRecord& ExistingConstraint)
				{
					return ExistingConstraint.ConstraintId == Constraint.ConstraintId;
				});
			if (!bAlreadyPresent)
			{
				InOutConstraints.Add(Constraint);
			}
		}
	}

	bool ValidateSelectedModePlan(
		const FLayoutWorkerSolvePacket& WorkerSolvePacket,
		const FLayoutRegionSolveRequest& Request,
		FString& OutFailureReason)
	{
		if (!WorkerSolvePacket.bHasSelectedModePlan)
		{
			OutFailureReason = TEXT("Worker request finalizer is missing a frozen selected mode plan.");
			return false;
		}

		FLayoutContractModeSelectionInput Input;
		Input.SolveRequest = &Request;
		Input.SiteCenterBlockWorldPos = WorkerSolvePacket.SelectedModePlan.SiteCenterBlockWorldPos;
		Input.WorldSeed = WorkerSolvePacket.SelectedModePlan.WorldSeed;
		const FLayoutModePlan ExpectedModePlan = FLayoutContractModeSelection::SelectModePlan(Input);


		if (ExpectedModePlan.ModePlanId != WorkerSolvePacket.SelectedModePlan.ModePlanId)
		{
			const FLayoutModePlan& FrozenMode = WorkerSolvePacket.SelectedModePlan;
			OutFailureReason = FString::Printf(
				TEXT("Worker request finalizer selected mode plan does not match finalized frozen request data. "
					"frozenId=%s expectedId=%s frozen=[scope=%d environment=%d stepped=%d site=%s worldSeed=%d solveSeed=%d placement=%d continuationFamily=%s continuationPlacement=%d continuationLevel=%d] "
					"expected=[scope=%d environment=%d stepped=%d site=%s worldSeed=%d solveSeed=%d placement=%d continuationFamily=%s continuationPlacement=%d continuationLevel=%d]."),
				*FrozenMode.ModePlanId.ToString(),
				*ExpectedModePlan.ModePlanId.ToString(),
				static_cast<int32>(FrozenMode.Scope),
				static_cast<int32>(FrozenMode.EnvironmentMode),
				FrozenMode.bUsesSteppedTerrainTopology ? 1 : 0,
				*FrozenMode.SiteCenterBlockWorldPos.ToString(),
				FrozenMode.WorldSeed,
				FrozenMode.SolveSeed,
				static_cast<int32>(FrozenMode.PlacementKind),
				*FrozenMode.ContinuationSelection.FamilyId.ToString(),
				static_cast<int32>(FrozenMode.ContinuationSelection.PlacementKind),
				FrozenMode.ContinuationSelection.ResolvedEntryLevel,
				static_cast<int32>(ExpectedModePlan.Scope),
				static_cast<int32>(ExpectedModePlan.EnvironmentMode),
				ExpectedModePlan.bUsesSteppedTerrainTopology ? 1 : 0,
				*ExpectedModePlan.SiteCenterBlockWorldPos.ToString(),
				ExpectedModePlan.WorldSeed,
				ExpectedModePlan.SolveSeed,
				static_cast<int32>(ExpectedModePlan.PlacementKind),
				*ExpectedModePlan.ContinuationSelection.FamilyId.ToString(),
				static_cast<int32>(ExpectedModePlan.ContinuationSelection.PlacementKind),
				ExpectedModePlan.ContinuationSelection.ResolvedEntryLevel);
			return false;
		}

		return true;
	}

	bool ShouldRunStandardAdapter(const FLayoutRegionSolveRequest& Request)
	{
		return Request.bHasSelectedModePlan
			&& (Request.SelectedModePlan.EnvironmentMode == ELayoutContractEnvironmentMode::StandardRegion
				|| Request.SelectedModePlan.EnvironmentMode == ELayoutContractEnvironmentMode::ChildRegion);
	}

	bool ApplyStandardContractAdapter(
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason)
	{
		PORISM_LAYOUT_PROFILE_SCOPE(Layout_Request_StandardContractApply, STAT_PorismLayout_StandardContractApply);
		if (!ShouldRunStandardAdapter(InOutRequest))
		{
			return true;
		}

		const FLayoutContractManifest Manifest = FLayoutContractPipeline::BuildManifestFromSolveRequest(InOutRequest);
		FLayoutContractModeAdapterInput Input;
		Input.SolveRequest = &InOutRequest;
		Input.Manifest = &Manifest;
		Input.ModePlan = InOutRequest.SelectedModePlan;
		FLayoutAdapterOutput Prepared;
		FLayoutRegionContract Contract;
		if (!FLayoutContractModeAdapter::TryPrepareRegionOutput(Input, Prepared, OutFailureReason)
			|| !FLayoutContractPipeline::TryBuildPreparedRegionContract(
				Manifest, InOutRequest.FootprintSize, Prepared, Contract, OutFailureReason))
		{
			return false;
		}

		if (!FLayoutContractPipeline::TryApplyRegionContractToSolveRequest(Contract, InOutRequest, OutFailureReason))
		{
			return false;
		}
		InOutRequest.bHasSelectedModePlan = true;
		InOutRequest.SelectedModePlan = Contract.ModePlan;
		return true;
	}

	void ApplyChildHandoffOverrides(
		const FLayoutChildSolveHandoff& ChildSolveHandoff,
		FLayoutRegionSolveRequest& InOutRequest)
	{
		InOutRequest.SourceParentRegionDebugPath = ChildSolveHandoff.ParentRegionDebugPath;
		InOutRequest.SourceContentEntryId = ChildSolveHandoff.ContentMetadata.SourceContentEntryId;
		InOutRequest.ProfilePath = ChildSolveHandoff.ContentMetadata.ChildProfilePath;
		InOutRequest.bSourceContentEntryOptional = ChildSolveHandoff.ContentMetadata.bOptional;
		InOutRequest.RegionCellOffset = ChildSolveHandoff.ChildRegionCellOffset;
		MergeUniqueCells(ChildSolveHandoff.ProtectedStructuralCells, InOutRequest.ProtectedStructuralCells);
		MergeUniqueRouteConstraints(ChildSolveHandoff.RequiredRouteConstraints, InOutRequest.RequiredRouteConstraints);

		for (const FLayoutCommittedTraversalAnchor& Anchor : ChildSolveHandoff.DirectCommitment.ParentTraversalIngressCommitments)
		{
			const bool bAlreadyPresent = InOutRequest.CommittedTraversalAnchors.ContainsByPredicate(
				[&Anchor](const FLayoutCommittedTraversalAnchor& ExistingAnchor)
				{
					return ExistingAnchor.Cell == Anchor.Cell
						&& ExistingAnchor.TraversalChannel == Anchor.TraversalChannel;
				});
			if (!bAlreadyPresent)
			{
				InOutRequest.CommittedTraversalAnchors.Add(Anchor);
			}
		}

		for (const FLayoutCommittedEndpointAnchor& Anchor : ChildSolveHandoff.DirectCommitment.EndpointCommitments)
		{
			const bool bAlreadyPresent = InOutRequest.CommittedEndpointAnchors.ContainsByPredicate(
				[&Anchor](const FLayoutCommittedEndpointAnchor& ExistingAnchor)
				{
					return ExistingAnchor.CommitmentId == Anchor.CommitmentId;
				});
			if (!bAlreadyPresent)
			{
				InOutRequest.CommittedEndpointAnchors.Add(Anchor);
			}
		}

		InOutRequest.NegotiatedChildResponsibilityContracts = {
			ChildSolveHandoff.DirectCommitment.NegotiatedResponsibilityContract
		};
		InOutRequest.bUseSuppliedChildCapabilityEnvelope = true;
		InOutRequest.SuppliedChildCapabilityEnvelope = ChildSolveHandoff.ChildCapabilityEnvelope;
		InOutRequest.DelegatedZoneFeatureRequirementIds = ChildSolveHandoff.DelegatedZoneFeatureRequirementIds;
		InOutRequest.DelegatedClosureRequirementIds = ChildSolveHandoff.DelegatedClosureRequirementIds;
		InOutRequest.bHasCertifiedSelectedTraversalCapabilityArtifact = ChildSolveHandoff.bHasCertifiedSelectedTraversalCapabilityArtifact;
		InOutRequest.CertifiedSelectedTraversalCapabilityIds = ChildSolveHandoff.CertifiedSelectedTraversalCapabilityIds;
		InOutRequest.bHasCertifiedParentAssertionSubsetArtifact = ChildSolveHandoff.bHasCertifiedParentAssertionSubsetArtifact;
		InOutRequest.CertifiedParentAssertionSubsetIds = ChildSolveHandoff.CertifiedParentAssertionSubsetIds;
	}
}

bool LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(
	const FLayoutWorkerSolvePacket& WorkerSolvePacket,
	FLayoutRegionSolveRequest& OutRequest,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (!ValidatePacketRequestData(WorkerSolvePacket, OutFailureReason))
	{
		return false;
	}

	WorkerSolvePacket.RequestManifest.PopulateSolveRequest(OutRequest);
	OutRequest.Seed = WorkerSolvePacket.SolveSeed;
	OutRequest.TemplatePlacementZOffsetBlocks = WorkerSolvePacket.RuntimeSnapshot.TemplatePlacementZOffsetBlocks;
	OutRequest.RootPlacementKind = WorkerSolvePacket.RuntimeSnapshot.PlacementKind;
	OutRequest.WorldBindingId = WorkerSolvePacket.RuntimeSnapshot.BindingId;
	OutRequest.WorldBindingPlacementPolicy = WorkerSolvePacket.RuntimeSnapshot.PlacementPolicy;
	OutRequest.RootContinuationSelection = WorkerSolvePacket.RuntimeSnapshot.ContinuationSelection;
	// Preserve authored flat reserved-open terrain authority until prewarm rebuilds its frozen contract.
	OutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations =
		WorkerSolvePacket.RequestManifest.ReservedOpenTerrainReservations;

	if (WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::Child)
	{
		ApplyChildHandoffOverrides(WorkerSolvePacket.FrozenChildHandoff, OutRequest);
	}

	if (!ValidateSelectedModePlan(WorkerSolvePacket, OutRequest, OutFailureReason))
	{
		return false;
	}
	OutRequest.bHasSelectedModePlan = true;
	OutRequest.SelectedModePlan = WorkerSolvePacket.SelectedModePlan;
	if (OutRequest.bHasFrozenTerrainBiomeAdapterInput)
	{
		OutRequest.FrozenTerrainBiomeAdapterInput.ModePlanId = OutRequest.SelectedModePlan.ModePlanId;
		if (OutRequest.FrozenTerrainBiomeAdapterInput.ArtifactId.IsNone())
		{
			OutRequest.FrozenTerrainBiomeAdapterInput.ArtifactId = FLayoutId(*FString::Printf(
				TEXT("TerrainAdapter.%s.%s"),
				*OutRequest.SelectedModePlan.ModePlanId.ToString(),
				*OutRequest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos.ToString()));
		}
		OutRequest.FrozenTerrainBiomeAdapterInput.SiteCenterBlockWorldPos =
			OutRequest.SelectedModePlan.SiteCenterBlockWorldPos;
		if (OutRequest.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks == FIntPoint::ZeroValue)
		{
			const FIntVector SharedCellSize = OutRequest.ContentSetSnapshot.SharedCellSizeInBlocks != FIntVector::ZeroValue
				? OutRequest.ContentSetSnapshot.SharedCellSizeInBlocks
				: OutRequest.ModuleCatalog.SharedCellSizeInBlocks;
			OutRequest.FrozenTerrainBiomeAdapterInput.FootprintSizeInBlocks = FIntPoint(
				OutRequest.FootprintSize.X * SharedCellSize.X,
				OutRequest.FootprintSize.Y * SharedCellSize.Y);
		}
		OutRequest.FrozenTerrainBiomeAdapterInput.AuditMessages.Add(TEXT("FinalizedFromPacket"));
	}
	// Transfer descriptor-produced precomputed adapter output before the compatibility
	// contract adapter so ApplyStandardContractAdapter can consume precomputed fields
	// on its fast path instead of falling through every time.
	if (WorkerSolvePacket.bHasPrecomputedAdapterOutput)
	{
		const FLayoutAdapterOutput& Output = WorkerSolvePacket.PrecomputedAdapterOutput;
		OutRequest.PrecomputedPlannedCells = Output.PlannedCells;
		OutRequest.bHasFinalizedSteppedTerrainIntents = Output.bHasFinalizedSteppedTerrainIntents;
		OutRequest.VerticalAccessHostGroups = Output.VerticalAccessHostGroups;
		OutRequest.PlannedCells = Output.PlannedCells;
		OutRequest.PrecomputedActiveCells = Output.ActiveCells;
		TArray<FLayoutCellReservationRecord> ExistingReservedOpenTerrainReservations =
			MoveTemp(OutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations);
		OutRequest.PrecomputedFrozenTerrainContract = Output.FrozenTerrainContract;
		if (OutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations.IsEmpty())
		{
			OutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations =
				MoveTemp(ExistingReservedOpenTerrainReservations);
		}
		OutRequest.PrecomputedSteppedTerrainSupportMap = Output.SteppedTerrainSupportMap;
		OutRequest.QualifiedEntryCells = Output.QualifiedEntryCells;
		OutRequest.bHasQualifiedEntryCells = !Output.QualifiedEntryCells.IsEmpty();
		OutRequest.SelectedModePlan = Output.ModePlan;
		OutRequest.bHasSelectedModePlan = true;
	}

	// Refresh standalone stepped-terrain assertions now that the prewarm
	// may have populated SteppedTerrainSupportMap on the finalized request.
	LayoutRegionRequestSnapshotBuilder::RefreshStandaloneSteppedTerrainAssertions(OutRequest);

	if (!ApplyStandardContractAdapter(OutRequest, OutFailureReason))
	{
		return false;
	}

	if (!LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveRequest(OutRequest, OutFailureReason))
	{
		return false;
	}

	return true;
}

bool LayoutWorkerSolveRequestFinalizer::FinalizeRequestAndManifestFromPacket(
	const FLayoutWorkerSolvePacket& WorkerSolvePacket,
	FLayoutContractManifestCache& ManifestCache,
	FLayoutRegionSolveRequest& OutRequest,
	FLayoutContractManifestCacheEntry& OutManifestEntry,
	bool& bOutManifestCacheHit,
	FString& OutFailureReason)
{
	OutManifestEntry = FLayoutContractManifestCacheEntry();
	bOutManifestCacheHit = false;
	if (!FinalizeRequestFromPacket(WorkerSolvePacket, OutRequest, OutFailureReason))
	{
		return false;
	}

	const FLayoutFrozenRequestManifestArtifact FrozenArtifact =
		WorkerSolvePacket.Kind == ELayoutWorkerSolvePacketKind::Child
			? FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(OutRequest)
			: WorkerSolvePacket.RequestManifest.BuildFrozenRequestManifestArtifact();

	// Validate immutable snapshot identity before cache lookup.
	// Skip when the source path is empty (planning/test paths may construct requests
	// without durable profile/content/module-set paths).
	{
		FString SnapshotFailureReason;

		if (!OutRequest.ProfilePath.IsNull())
		{
			const FLayoutProfileManifestSnapshot ProfileSnapshot =
				FLayoutProfileManifestSnapshot::Build(OutRequest.ProfilePath);
			if (!ProfileSnapshot.Validate(SnapshotFailureReason))
			{
				OutFailureReason = FString::Printf(
					TEXT("Worker request finalizer rejected stale profile snapshot: %s"), *SnapshotFailureReason);
				return false;
			}
		}

		if (OutRequest.ContentSetSnapshot.SourceContentSet.Get() != nullptr)
		{
			const FSoftObjectPath ContentSetPath(
				OutRequest.ContentSetSnapshot.SourceContentSet->GetPathName());
			FLayoutContentSetManifestSnapshot ContentSetSnapshot =
				FLayoutContentSetManifestSnapshot::Build(ContentSetPath);
			if (!ContentSetSnapshot.Validate(SnapshotFailureReason))
			{
				OutFailureReason = FString::Printf(
					TEXT("Worker request finalizer rejected stale content-set snapshot: %s"), *SnapshotFailureReason);
				return false;
			}
		}
	}

	if (!ManifestCache.FindOrAddManifest(FrozenArtifact, OutManifestEntry, bOutManifestCacheHit))
	{
		OutFailureReason = TEXT("Worker request finalizer could not resolve a pointer-free contract manifest cache entry.");
		return false;
	}

	return true;
}
