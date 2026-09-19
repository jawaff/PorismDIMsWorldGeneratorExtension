// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutWorkerSolvePacket.h"

#include "Layout/Contracts/LayoutContractModeSelection.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"

namespace
{
	FLayoutWorkerSolvePacket MakeBasePacket(
		const FString& DebugName,
		const ELayoutWorkerSolvePacketKind Kind,
		const FLayoutWorldBindingRuntimeSnapshot& RuntimeSnapshot,
		const int32 SolveSeed,
		const int32 AttemptIndex)
	{
		FLayoutWorkerSolvePacket Packet;
		Packet.DebugName = DebugName;
		Packet.Kind = Kind;
		Packet.RuntimeSnapshot = RuntimeSnapshot;
		Packet.SolveSeed = SolveSeed;
		Packet.AttemptIndex = AttemptIndex;
		return Packet;
	}
}

FLayoutWorkerSolveRequestManifest FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(const FLayoutRegionSolveRequest& Request)
{
	const FLayoutRegionSolveRequest WorkerSafeRequest = LayoutBackgroundSolveSnapshot::MakeWorkerSafeSolveRequest(Request);
	FLayoutWorkerSolveRequestManifest Manifest;
	Manifest.EffectiveSnapshotId = WorkerSafeRequest.EffectiveSnapshotId;
	Manifest.SnapshotSchemaVersion = WorkerSafeRequest.SnapshotSchemaVersion;
	Manifest.RegionDebugPath = WorkerSafeRequest.RegionDebugPath;
	Manifest.RegionCellOffset = WorkerSafeRequest.RegionCellOffset;
	Manifest.CapturedSeed = WorkerSafeRequest.Seed;
	Manifest.CapturedTemplatePlacementZOffsetBlocks = WorkerSafeRequest.TemplatePlacementZOffsetBlocks;
	Manifest.CapturedRootPlacementKind = WorkerSafeRequest.RootPlacementKind;
	Manifest.WorldBindingId = WorkerSafeRequest.WorldBindingId;
	Manifest.RootSolveId = WorkerSafeRequest.RootSolveId;
	Manifest.RootCandidateId = WorkerSafeRequest.RootCandidateId;
	Manifest.RootPlacementPolicyId = WorkerSafeRequest.RootPlacementPolicyId;
	Manifest.bHasRootPlacementSubmission = WorkerSafeRequest.bHasRootPlacementSubmission;
	Manifest.RootPlacementShiftId = WorkerSafeRequest.RootPlacementShiftId;
	Manifest.RootPlacementShiftCells = WorkerSafeRequest.RootPlacementShiftCells;
	Manifest.RootSiteCenterBlockWorldPos = WorkerSafeRequest.RootSiteCenterBlockWorldPos;
	Manifest.RootReservationKey = WorkerSafeRequest.RootReservationKey;
	Manifest.RootContinuationSelection = WorkerSafeRequest.RootContinuationSelection;
	Manifest.bHasFrozenTerrainBiomeAdapterInput = WorkerSafeRequest.bHasFrozenTerrainBiomeAdapterInput;
	Manifest.FrozenTerrainBiomeAdapterInput = WorkerSafeRequest.FrozenTerrainBiomeAdapterInput;
	Manifest.bHasSelectedModePlan = WorkerSafeRequest.bHasSelectedModePlan;
	Manifest.SelectedModePlan = WorkerSafeRequest.SelectedModePlan;
	Manifest.SourceContentEntryId = WorkerSafeRequest.SourceContentEntryId;
	Manifest.PrecommittedZoneFeatureProviderCommitments =
		WorkerSafeRequest.PrecommittedZoneFeatureProviderCommitments;
	Manifest.ProfilePath = WorkerSafeRequest.ProfilePath.IsValid()
		? WorkerSafeRequest.ProfilePath
		: WorkerSafeRequest.ProfileSnapshot.SourceProfilePath;
	Manifest.SourceParentRegionDebugPath = WorkerSafeRequest.SourceParentRegionDebugPath;
	Manifest.bSourceContentEntryOptional = WorkerSafeRequest.bSourceContentEntryOptional;
	Manifest.bUseSuppliedPlannedCells = !WorkerSafeRequest.PlannedCells.IsEmpty();
	Manifest.bHasFinalizedSteppedTerrainIntents = WorkerSafeRequest.bHasFinalizedSteppedTerrainIntents;
	Manifest.VerticalAccessHostGroups = WorkerSafeRequest.VerticalAccessHostGroups;
	Manifest.PreparedChildPlacementHints = WorkerSafeRequest.PreparedChildPlacementHints;
	Manifest.bUseChildLocalFlatFallback = WorkerSafeRequest.bUseChildLocalFlatFallback;
	Manifest.FootprintSize = WorkerSafeRequest.FootprintSize;
	Manifest.PlannedCells = WorkerSafeRequest.PlannedCells;
	Manifest.ExternalPlannedNeighborFaceMasks =
		WorkerSafeRequest.ExternalPlannedNeighborFaceMasks;
	// Local project fix: preserve removed reserved-cell realization authority across worker snapshots.
	Manifest.ReservedOpenTerrainReservations =
		WorkerSafeRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations;
	Manifest.PrecomputedAdapterDiagnostics = WorkerSafeRequest.PrecomputedAdapterDiagnostics;
	Manifest.bHasQualifiedEntryCells = WorkerSafeRequest.bHasQualifiedEntryCells;
	Manifest.QualifiedEntryCells = WorkerSafeRequest.QualifiedEntryCells;
	Manifest.ModuleCatalog = WorkerSafeRequest.ModuleCatalog;
	Manifest.ContentSetSnapshot = WorkerSafeRequest.ContentSetSnapshot;
	Manifest.ProfileSnapshot = WorkerSafeRequest.ProfileSnapshot;
	Manifest.ExecutionSettings = WorkerSafeRequest.ExecutionSettings;
	Manifest.IncomingBoundaryPoints = WorkerSafeRequest.IncomingBoundaryPoints;
	Manifest.CommittedTraversalAnchors = WorkerSafeRequest.CommittedTraversalAnchors;
	Manifest.ProtectedStructuralCells = WorkerSafeRequest.ProtectedStructuralCells;
	Manifest.CommittedEndpointAnchors = WorkerSafeRequest.CommittedEndpointAnchors;
	Manifest.ForcedPlacementBundleInsertions = WorkerSafeRequest.ForcedPlacementBundleInsertions;
	Manifest.RequiredRouteConstraints = WorkerSafeRequest.RequiredRouteConstraints;
	Manifest.FutureTerraceProofBoundaryPoints = WorkerSafeRequest.FutureTerraceProofBoundaryPoints;
	Manifest.NegotiatedChildResponsibilityContracts = WorkerSafeRequest.NegotiatedChildResponsibilityContracts;
	Manifest.bUseSuppliedChildCapabilityEnvelope = WorkerSafeRequest.bUseSuppliedChildCapabilityEnvelope;
	Manifest.SuppliedChildCapabilityEnvelope = WorkerSafeRequest.SuppliedChildCapabilityEnvelope;
	Manifest.DelegatedZoneFeatureRequirementIds = WorkerSafeRequest.DelegatedZoneFeatureRequirementIds;
	Manifest.DelegatedClosureRequirementIds = WorkerSafeRequest.DelegatedClosureRequirementIds;
	Manifest.CertifiedSelectedEndpointCapabilityIds = WorkerSafeRequest.CertifiedSelectedEndpointCapabilityIds;
	Manifest.bHasCertifiedSelectedTraversalCapabilityArtifact = WorkerSafeRequest.bHasCertifiedSelectedTraversalCapabilityArtifact;
	Manifest.CertifiedSelectedTraversalCapabilityIds = WorkerSafeRequest.CertifiedSelectedTraversalCapabilityIds;
	Manifest.CertifiedSelectedVerticalCapabilityIds = WorkerSafeRequest.CertifiedSelectedVerticalCapabilityIds;
	Manifest.CertifiedSelectedClosureSpanCapabilityId = WorkerSafeRequest.CertifiedSelectedClosureSpanCapabilityId;
	Manifest.CertifiedSelectedSeamCapabilityId = WorkerSafeRequest.CertifiedSelectedSeamCapabilityId;
	Manifest.CertifiedSelectedSeamWitnessId = WorkerSafeRequest.CertifiedSelectedSeamWitnessId;
	Manifest.CertifiedDelegatedSeamWitnessIds = WorkerSafeRequest.CertifiedDelegatedSeamWitnessIds;
	Manifest.CertifiedDelegatedJunctionWitnessIds = WorkerSafeRequest.CertifiedDelegatedJunctionWitnessIds;
	Manifest.CertifiedDelegatedHostVerticalAccessWitnessIds = WorkerSafeRequest.CertifiedDelegatedHostVerticalAccessWitnessIds;
	Manifest.bHasCertifiedParentAssertionSubsetArtifact = WorkerSafeRequest.bHasCertifiedParentAssertionSubsetArtifact;
	Manifest.CertifiedParentAssertionSubsetIds = WorkerSafeRequest.CertifiedParentAssertionSubsetIds;
	Manifest.bHasCertifiedParentBranchCertificateArtifact = WorkerSafeRequest.bHasCertifiedParentBranchCertificateArtifact;
	Manifest.CertifiedParentBranchCertificateArtifactId = WorkerSafeRequest.CertifiedParentBranchCertificateArtifactId;
	Manifest.CertifiedParentBranchParentContractId = WorkerSafeRequest.CertifiedParentBranchParentContractId;
	Manifest.CertifiedParentBranchParentContractHash = WorkerSafeRequest.CertifiedParentBranchParentContractHash;
	Manifest.CertifiedParentBranchId = WorkerSafeRequest.CertifiedParentBranchId;
	Manifest.CertifiedParentBranchChildScoutResultId = WorkerSafeRequest.CertifiedParentBranchChildScoutResultId;
	Manifest.CertifiedParentBranchCertificateInputHash = WorkerSafeRequest.CertifiedParentBranchCertificateInputHash;
	Manifest.bDeferClosureValidationToSchedule = WorkerSafeRequest.bDeferClosureValidationToSchedule;
	Manifest.bDeferTraversalValidationToSchedule = WorkerSafeRequest.bDeferTraversalValidationToSchedule;
	Manifest.ProofRecords = WorkerSafeRequest.ProofRecords;
	Manifest.ValidationAssertions = WorkerSafeRequest.ValidationAssertions;
	Manifest.SteppedTerrainSupportMap = WorkerSafeRequest.SteppedTerrainSupportMap;
	return Manifest;
}

void FLayoutWorkerSolveRequestManifest::PopulateSolveRequest(FLayoutRegionSolveRequest& OutRequest) const
{
	OutRequest = FLayoutRegionSolveRequest();
	OutRequest.EffectiveSnapshotId = EffectiveSnapshotId;
	OutRequest.SnapshotSchemaVersion = SnapshotSchemaVersion;
	OutRequest.RegionDebugPath = RegionDebugPath;
	OutRequest.RegionCellOffset = RegionCellOffset;
	OutRequest.Seed = CapturedSeed;
	OutRequest.TemplatePlacementZOffsetBlocks = CapturedTemplatePlacementZOffsetBlocks;
	OutRequest.RootPlacementKind = CapturedRootPlacementKind;
	OutRequest.WorldBindingId = WorldBindingId;
	OutRequest.RootSolveId = RootSolveId;
	OutRequest.RootCandidateId = RootCandidateId;
	OutRequest.RootPlacementPolicyId = RootPlacementPolicyId;
	OutRequest.bHasRootPlacementSubmission = bHasRootPlacementSubmission;
	OutRequest.RootPlacementShiftId = RootPlacementShiftId;
	OutRequest.RootPlacementShiftCells = RootPlacementShiftCells;
	OutRequest.RootSiteCenterBlockWorldPos = RootSiteCenterBlockWorldPos;
	OutRequest.RootReservationKey = RootReservationKey;
	OutRequest.RootContinuationSelection = RootContinuationSelection;
	OutRequest.bHasFrozenTerrainBiomeAdapterInput = bHasFrozenTerrainBiomeAdapterInput;
	OutRequest.FrozenTerrainBiomeAdapterInput = FrozenTerrainBiomeAdapterInput;
	OutRequest.bHasSelectedModePlan = bHasSelectedModePlan;
	OutRequest.SelectedModePlan = SelectedModePlan;
	OutRequest.SourceContentEntryId = SourceContentEntryId;
	OutRequest.PrecommittedZoneFeatureProviderCommitments =
		PrecommittedZoneFeatureProviderCommitments;
	OutRequest.ProfilePath = ProfilePath;
	OutRequest.SourceParentRegionDebugPath = SourceParentRegionDebugPath;
	OutRequest.bSourceContentEntryOptional = bSourceContentEntryOptional;
	OutRequest.FootprintSize = FootprintSize;
	OutRequest.PlannedCells = PlannedCells;
	OutRequest.ExternalPlannedNeighborFaceMasks =
		ExternalPlannedNeighborFaceMasks;
	// Local project fix: prewarm reconstructs requests through this path before adapter finalization.
	OutRequest.PrecomputedFrozenTerrainContract.ReservedOpenTerrainReservations =
		ReservedOpenTerrainReservations;
	OutRequest.PrecomputedAdapterDiagnostics = PrecomputedAdapterDiagnostics;
	OutRequest.bHasFinalizedSteppedTerrainIntents = bHasFinalizedSteppedTerrainIntents;
	OutRequest.VerticalAccessHostGroups = VerticalAccessHostGroups;
	OutRequest.PreparedChildPlacementHints = PreparedChildPlacementHints;
	OutRequest.bUseChildLocalFlatFallback = bUseChildLocalFlatFallback;
	if (bHasFinalizedSteppedTerrainIntents)
	{
		OutRequest.PrecomputedPlannedCells = PlannedCells;
		OutRequest.PrecomputedSteppedTerrainSupportMap = SteppedTerrainSupportMap;
	}
	OutRequest.bHasQualifiedEntryCells = bHasQualifiedEntryCells;
	OutRequest.QualifiedEntryCells = QualifiedEntryCells;
	OutRequest.ModuleCatalog = ModuleCatalog;
	OutRequest.ContentSetSnapshot = ContentSetSnapshot;
	OutRequest.ProfileSnapshot = ProfileSnapshot;
	OutRequest.ExecutionSettings = ExecutionSettings;
	OutRequest.IncomingBoundaryPoints = IncomingBoundaryPoints;
	OutRequest.CommittedTraversalAnchors = CommittedTraversalAnchors;
	OutRequest.ProtectedStructuralCells = ProtectedStructuralCells;
	OutRequest.CommittedEndpointAnchors = CommittedEndpointAnchors;
	OutRequest.ForcedPlacementBundleInsertions = ForcedPlacementBundleInsertions;
	OutRequest.RequiredRouteConstraints = RequiredRouteConstraints;
	OutRequest.FutureTerraceProofBoundaryPoints = FutureTerraceProofBoundaryPoints;
	OutRequest.NegotiatedChildResponsibilityContracts = NegotiatedChildResponsibilityContracts;
	OutRequest.bUseSuppliedChildCapabilityEnvelope = bUseSuppliedChildCapabilityEnvelope;
	OutRequest.SuppliedChildCapabilityEnvelope = SuppliedChildCapabilityEnvelope;
	OutRequest.DelegatedZoneFeatureRequirementIds = DelegatedZoneFeatureRequirementIds;
	OutRequest.DelegatedClosureRequirementIds = DelegatedClosureRequirementIds;
	OutRequest.CertifiedSelectedEndpointCapabilityIds = CertifiedSelectedEndpointCapabilityIds;
	OutRequest.bHasCertifiedSelectedTraversalCapabilityArtifact = bHasCertifiedSelectedTraversalCapabilityArtifact;
	OutRequest.CertifiedSelectedTraversalCapabilityIds = CertifiedSelectedTraversalCapabilityIds;
	OutRequest.CertifiedSelectedVerticalCapabilityIds = CertifiedSelectedVerticalCapabilityIds;
	OutRequest.CertifiedSelectedClosureSpanCapabilityId = CertifiedSelectedClosureSpanCapabilityId;
	OutRequest.CertifiedSelectedSeamCapabilityId = CertifiedSelectedSeamCapabilityId;
	OutRequest.CertifiedSelectedSeamWitnessId = CertifiedSelectedSeamWitnessId;
	OutRequest.CertifiedDelegatedSeamWitnessIds = CertifiedDelegatedSeamWitnessIds;
	OutRequest.CertifiedDelegatedJunctionWitnessIds = CertifiedDelegatedJunctionWitnessIds;
	OutRequest.CertifiedDelegatedHostVerticalAccessWitnessIds = CertifiedDelegatedHostVerticalAccessWitnessIds;
	OutRequest.bHasCertifiedParentAssertionSubsetArtifact = bHasCertifiedParentAssertionSubsetArtifact;
	OutRequest.CertifiedParentAssertionSubsetIds = CertifiedParentAssertionSubsetIds;
	OutRequest.bHasCertifiedParentBranchCertificateArtifact = bHasCertifiedParentBranchCertificateArtifact;
	OutRequest.CertifiedParentBranchCertificateArtifactId = CertifiedParentBranchCertificateArtifactId;
	OutRequest.CertifiedParentBranchParentContractId = CertifiedParentBranchParentContractId;
	OutRequest.CertifiedParentBranchParentContractHash = CertifiedParentBranchParentContractHash;
	OutRequest.CertifiedParentBranchId = CertifiedParentBranchId;
	OutRequest.CertifiedParentBranchChildScoutResultId = CertifiedParentBranchChildScoutResultId;
	OutRequest.CertifiedParentBranchCertificateInputHash = CertifiedParentBranchCertificateInputHash;
	OutRequest.bDeferClosureValidationToSchedule = bDeferClosureValidationToSchedule;
	OutRequest.bDeferTraversalValidationToSchedule = bDeferTraversalValidationToSchedule;
	OutRequest.ProofRecords = ProofRecords;
	OutRequest.ValidationAssertions = ValidationAssertions;
	OutRequest.SteppedTerrainSupportMap = SteppedTerrainSupportMap;
}

FLayoutRegionSolveRequest FLayoutWorkerSolveRequestManifest::ToSolveRequestForResume() const
{
	FLayoutRegionSolveRequest Request;
	PopulateSolveRequest(Request);
	return Request;
}

FLayoutFrozenRequestManifestArtifact FLayoutWorkerSolveRequestManifest::BuildFrozenRequestManifestArtifact() const
{
	const FLayoutRegionSolveRequest Request = ToSolveRequestForResume();
	return FLayoutContractManifestCache::BuildFrozenRequestManifestArtifact(Request);
}

bool FLayoutWorkerSolveRequestManifest::ValidateNoLiveObjectCarriers(FString& OutFailureReason) const
{
	FLayoutRegionSolveRequest Request;
	PopulateSolveRequest(Request);
	return LayoutBackgroundSolveSnapshot::ValidateWorkerSafeSolveRequest(Request, OutFailureReason);
}

FLayoutWorkerSolvePacket FLayoutWorkerSolvePacket::CapturePlanningRoot(
	const FString& DebugName,
	const FLayoutWorldBindingRuntimeView& RuntimeView,
	const FPlannedLayoutSiteRecord& PendingRecord,
	const int32 AttemptIndex)
{
	const FLayoutSiteSolveSourceSelection SolveSourceSelection = PendingRecord.GetSiteSolveSourceSelection();
	const FLayoutPlannedSiteReservationSourceSelection ReservationSelection = PendingRecord.GetPlannedSiteReservationSourceSelection();
	const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata = PendingRecord.GetPlannedSiteLifecycleMetadata();
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection = PendingRecord.GetWorldBindingFrontendSelection();

	FLayoutWorkerSolvePacket Packet = MakeBasePacket(
		DebugName,
		ELayoutWorkerSolvePacketKind::PlanningRoot,
		FLayoutWorldBindingRuntimeSnapshot::CaptureFromRuntimeView(RuntimeView),
		SolveSourceSelection.SolveSeed,
		AttemptIndex);
	Packet.PrimaryBlockWorldPos = ReservationSelection.SiteCenterBlockWorldPos;
	Packet.StableTextKey = LifecycleMetadata.StableRecordKey;
	Packet.BiomeRowName = FrontendSelection.BiomeRowName;
	Packet.StableNumericKey = (static_cast<uint64>(static_cast<uint32>(ReservationSelection.ReservationKey.X)) << 32)
		| static_cast<uint32>(ReservationSelection.ReservationKey.Y);
	return Packet;
}

FLayoutWorkerSolvePacket FLayoutWorkerSolvePacket::CaptureExplicitPreviewRoot(
	const FString& DebugName,
	const FLayoutWorldBindingRuntimeView& RuntimeView,
	const FIntVector& SiteCenterBlockWorldPos,
	const int32 SolveSeed,
	const int32 AttemptIndex,
	const FName InBiomeRowName)
{
	FLayoutWorkerSolvePacket Packet = MakeBasePacket(
		DebugName,
		ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot,
		FLayoutWorldBindingRuntimeSnapshot::CaptureFromRuntimeView(RuntimeView),
		SolveSeed,
		AttemptIndex);
	Packet.PrimaryBlockWorldPos = SiteCenterBlockWorldPos;
	Packet.BiomeRowName = InBiomeRowName;
	Packet.StableNumericKey = static_cast<uint64>(GetTypeHash(SiteCenterBlockWorldPos))
		^ (static_cast<uint64>(static_cast<uint32>(SolveSeed)) << 32);
	return Packet;
}

FLayoutWorkerSolvePacket FLayoutWorkerSolvePacket::CaptureObservedFallbackRoot(
	const FString& DebugName,
	const FLayoutWorldBindingRuntimeView& RuntimeView,
	const FIntVector& SiteCenterBlockWorldPos,
	const int32 SolveSeed,
	const int32 AttemptIndex,
	const FName InBiomeRowName)
{
	FLayoutWorkerSolvePacket Packet = MakeBasePacket(
		DebugName,
		ELayoutWorkerSolvePacketKind::ObservedFallbackRoot,
		FLayoutWorldBindingRuntimeSnapshot::CaptureFromRuntimeView(RuntimeView),
		SolveSeed,
		AttemptIndex);
	Packet.PrimaryBlockWorldPos = SiteCenterBlockWorldPos;
	Packet.BiomeRowName = InBiomeRowName;
	Packet.StableNumericKey = static_cast<uint64>(GetTypeHash(SiteCenterBlockWorldPos))
		^ (static_cast<uint64>(static_cast<uint32>(SolveSeed)) << 32);
	return Packet;
}

FLayoutWorkerSolvePacket FLayoutWorkerSolvePacket::CaptureContinuation(
	const FString& DebugName,
	const FResolvedLayoutConnectorRecord& ConnectorRecord,
	const uint64 StableConnectorKey,
	const int32 AttemptIndex)
{
	FLayoutWorkerSolvePacket Packet = MakeBasePacket(
		DebugName,
		ELayoutWorkerSolvePacketKind::Continuation,
		FLayoutWorldBindingRuntimeSnapshot::CaptureFromConnectorRecord(ConnectorRecord),
		ConnectorRecord.SolveSeed,
		AttemptIndex);
	Packet.PrimaryBlockWorldPos = ConnectorRecord.StartEndpointBlockWorldPos;
	Packet.SecondaryBlockWorldPos = ConnectorRecord.EndEndpointBlockWorldPos;
	Packet.StableTextKey = ConnectorRecord.RootSolveId.ToString();
	Packet.StableNumericKey = StableConnectorKey;
	Packet.BiomeRowName = ConnectorRecord.BiomeRowName;
	return Packet;
}

FLayoutWorkerSolvePacket FLayoutWorkerSolvePacket::CaptureChild(
	const FString& DebugName,
	const FLayoutWorldBindingRuntimeSnapshot& InRuntimeSnapshot,
	const FLayoutChildSolveHandoff& ChildSolveHandoff,
	const int32 SolveSeed,
	const int32 AttemptIndex)
{
	FLayoutWorkerSolvePacket Packet = MakeBasePacket(
		DebugName,
		ELayoutWorkerSolvePacketKind::Child,
		InRuntimeSnapshot,
		SolveSeed,
		AttemptIndex);
	Packet.bHasFrozenChildHandoff = true;
	Packet.FrozenChildHandoff = ChildSolveHandoff;
	Packet.PrimaryBlockWorldPos = ChildSolveHandoff.bHasChildBlockWorldAnchor
		? ChildSolveHandoff.ChildBlockWorldAnchor
		: ChildSolveHandoff.ChildRegionCellOffset;
	Packet.StableTextKey = ChildSolveHandoff.StableChildKey;
	Packet.StableNumericKey = static_cast<uint64>(GetTypeHash(ChildSolveHandoff.StableChildKey))
		^ (static_cast<uint64>(static_cast<uint32>(SolveSeed)) << 32);
	return Packet;
}

bool FLayoutWorkerSolvePacket::ValidateNoLiveObjectCarriers(FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (!RuntimeSnapshot.ValidateNoLiveObjectCarriers(OutFailureReason))
	{
		return false;
	}

	if (!bHasRequestManifest)
	{
		OutFailureReason = TEXT("Worker solve packet is missing a request manifest.");
		return false;
	}

	if (!RequestManifest.ValidateNoLiveObjectCarriers(OutFailureReason))
	{
		return false;
	}

	if (bHasSelectedModePlan != RequestManifest.bHasSelectedModePlan)
	{
		OutFailureReason = TEXT("Worker solve packet selected mode plan presence does not match packet request manifest mode plan presence.");
		return false;
	}

	if (bHasSelectedModePlan && SelectedModePlan.ModePlanId != RequestManifest.SelectedModePlan.ModePlanId)
	{
		OutFailureReason = TEXT("Worker solve packet selected mode plan does not match packet request manifest mode plan.");
		return false;
	}

	if (Kind == ELayoutWorkerSolvePacketKind::Child)
	{
		if (!bHasFrozenChildHandoff)
		{
			OutFailureReason = TEXT("Child worker solve packet is missing a frozen parent-negotiated handoff payload.");
			return false;
		}

		if (!FrozenChildHandoff.ValidateNoLiveObjectCarriers(OutFailureReason))
		{
			return false;
		}
	}

	return true;
}
