// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutFrozenSubmissionDescriptorProducer.h"

#include "Layout/Async/LayoutManifestPrewarm.h"
#include "Layout/Async/LayoutPreSubmitFrozenSnapshot.h"
#include "Layout/Contracts/LayoutContractPipeline.h"
#include "Layout/Contracts/LayoutContractPlacementCandidates.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Solver/LayoutWorkerSolveRequestFinalizer.h"

namespace
{
	/** Recomputes the descriptor identity/payload audit hash sealed into a produced descriptor artifact. */
	uint32 BuildProducedDescriptorSeedAuditHash(const FLayoutFrozenSubmissionDescriptorSeed& Seed)
	{
		return FLayoutFrozenSubmissionStore::BuildAuditHash(
			Seed.DescriptorId,
			Seed.RegionGroupId,
			Seed.Generation,
			Seed.AttemptIndex,
			Seed.RegionKind,
			Seed.Snapshot);
	}

	/** Recomputes the manifest-prewarm audit hash sealed into a produced descriptor artifact. */
	uint32 BuildProducedManifestPrewarmAuditHash(const FLayoutManifestPrewarmInput& ManifestPrewarmInput)
	{
		uint32 Hash = GetTypeHash(ManifestPrewarmInput.PrewarmId);
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(ManifestPrewarmInput.Kind)));
		Hash = HashCombine(Hash, GetTypeHash(ManifestPrewarmInput.bHasFrozenRequestManifest));
		if (ManifestPrewarmInput.bHasFrozenRequestManifest)
		{
			const FLayoutWorkerSolveRequestManifest& Manifest = ManifestPrewarmInput.FrozenRequestManifest;
			Hash = HashCombine(Hash, GetTypeHash(Manifest.CapturedSeed));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.CapturedTemplatePlacementZOffsetBlocks));
			Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Manifest.CapturedRootPlacementKind)));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.WorldBindingId));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.SelectedModePlan.ModePlanId));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.bHasSelectedModePlan));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.bHasRootPlacementSubmission));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.RootPlacementShiftId));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.RootPlacementShiftCells));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.RootSiteCenterBlockWorldPos));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.RootReservationKey));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.ProfileSnapshot.bSupportsSteppedTerrainSolve));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.ProfileSnapshot.bEnableTerrainSeams));
			Hash = HashCombine(Hash, GetTypeHash(Manifest.RootContinuationSelection.ResolvedEntryLevel));
		}
		return Hash != 0 ? Hash : 1;
	}

	/** Stores one descriptor only through an already-validated produced artifact path after successful scout/preflight publication. */
	bool TryStoreDescriptorFromSuccessfulPreflightSeed(
		const FLayoutBackgroundSolveCompletion& PreflightCompletion,
		const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
		FLayoutFrozenSubmissionStore& Store,
		const FLayoutFrozenSubmissionDescriptorSeed& Seed,
		FLayoutFrozenSubmissionDescriptor& OutDescriptor,
		FString& OutFailureReason);

	/** Internal generic snapshot bridge retained only so selected-result producers can seal already-frozen snapshots. */
	bool TryBuildProducedDescriptorArtifactFromSnapshotInternal(
		FLayoutId ArtifactId,
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Verifies descriptor payloads carry the metadata consumed by prewarm/preflight before storage. */
	bool ValidateDescriptorMetadataForPreflight(
		const ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FString& OutFailureReason)
	{
		if (!Snapshot.bHasWorkerSolvePacket)
		{
			OutFailureReason = TEXT("Produced frozen descriptor artifact requires a frozen worker packet.");
			return false;
		}

		const FLayoutWorkerSolvePacket& WorkerSolvePacket = Snapshot.WorkerSolvePacket;
		if ((RegionKind == ELayoutFrozenSubmissionRegionKind::Root
				|| RegionKind == ELayoutFrozenSubmissionRegionKind::Continuation)
			&& (!WorkerSolvePacket.bHasSelectedModePlan || !WorkerSolvePacket.RequestManifest.bHasSelectedModePlan))
		{
			OutFailureReason = TEXT("Produced frozen descriptor artifact requires a frozen selected mode plan for preflight.");
			return false;
		}

		if (RegionKind == ELayoutFrozenSubmissionRegionKind::Root
			&& !WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission)
		{
			OutFailureReason = TEXT("Produced frozen descriptor artifact requires a frozen root placement submission for preflight.");
			return false;
		}

		if (RegionKind == ELayoutFrozenSubmissionRegionKind::Continuation)
		{
			if (!WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable)
			{
				OutFailureReason = TEXT("Produced frozen descriptor artifact requires continuation traversal reachability metadata for preflight.");
				return false;
			}
			if (WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel == INDEX_NONE)
			{
				OutFailureReason = TEXT("Produced frozen descriptor artifact requires resolved continuation entry metadata for preflight.");
				return false;
			}
		}

		return true;
	}
}

namespace LayoutFrozenSubmissionDescriptorProducer
{
	/** Internal descriptor constructor retained for artifact-backed storage and automation-only store tests. */
	FLayoutFrozenSubmissionDescriptor BuildDescriptorFromSnapshot(
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot);

	/** Internal descriptor store mutation retained behind artifact-backed storage and automation-only store tests. */
	bool StoreAcceptedDescriptor(
		FLayoutFrozenSubmissionStore& Store,
		const FLayoutFrozenSubmissionDescriptor& Descriptor,
		FString& OutFailureReason);

	/** Internal seed builder retained behind selected-result producers and automation-only seed tests. */
	bool TryBuildPreflightDescriptorSeed(
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);

	/** Internal manifest-prewarm bridge retained behind produced-artifact consumption and automation-only seed tests. */
	bool TryBuildManifestPrewarmInputFromSeed(
		const FLayoutFrozenSubmissionDescriptorSeed& Seed,
		FLayoutManifestPrewarmInput& OutInput,
		FString& OutFailureReason);

	/** Internal seed bridge retained only behind selected-result producers and test compatibility wrappers. */
	bool TryBuildProducedDescriptorArtifactFromSeedInternal(
		FLayoutId ArtifactId,
		const FLayoutFrozenSubmissionDescriptorSeed& Seed,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Internal produced-artifact seed consumer; production uses region-kind wrappers. */
	bool TryGetSeedForSolveSubmissionInternal(
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);

	/** Internal produced-artifact manifest consumer; production uses region-kind wrappers. */
	bool TryGetManifestPrewarmInputForSolveSubmissionInternal(
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutManifestPrewarmInput& OutInput,
		FString& OutFailureReason);
}

FLayoutFrozenSubmissionDescriptor LayoutFrozenSubmissionDescriptorProducer::BuildDescriptorFromSnapshot(
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot)
{
	FLayoutFrozenSubmissionDescriptor Descriptor;
	Descriptor.DescriptorId = DescriptorId;
	Descriptor.RegionGroupId = RegionGroupId;
	Descriptor.Generation = Generation;
	Descriptor.AttemptIndex = AttemptIndex;
	Descriptor.RegionKind = RegionKind;
	Descriptor.Snapshot = Snapshot;
	Descriptor.AuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
		Descriptor.DescriptorId,
		Descriptor.RegionGroupId,
		Descriptor.Generation,
		Descriptor.AttemptIndex,
		Descriptor.RegionKind,
		Descriptor.Snapshot);
	return Descriptor;
}

bool LayoutFrozenSubmissionDescriptorProducer::StoreAcceptedDescriptor(
	FLayoutFrozenSubmissionStore& Store,
	const FLayoutFrozenSubmissionDescriptor& Descriptor,
	FString& OutFailureReason)
{
	if (!ValidateDescriptorMetadataForPreflight(
			Descriptor.RegionKind,
			Descriptor.Snapshot,
			OutFailureReason))
	{
		return false;
	}
	return Store.AddOrReplace(Descriptor, OutFailureReason);
}

const FLayoutFrozenSubmissionDescriptor* LayoutFrozenSubmissionDescriptorProducer::FindForSubmit(
	const FLayoutFrozenSubmissionStore& Store,
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const uint32 AuditHash,
	FString& OutFailureReason)
{
	return Store.FindValid(
		DescriptorId,
		RegionGroupId,
		Generation,
		AttemptIndex,
		RegionKind,
		AuditHash,
		OutFailureReason);
}

const FLayoutFrozenSubmissionDescriptor* LayoutFrozenSubmissionDescriptorProducer::FindForSubmit(
	const FLayoutFrozenSubmissionStore& Store,
	const FLayoutFrozenSubmissionDescriptor& ExpectedDescriptor,
	FString& OutFailureReason)
{
	return FindForSubmit(
		Store,
		ExpectedDescriptor.DescriptorId,
		ExpectedDescriptor.RegionGroupId,
		ExpectedDescriptor.Generation,
		ExpectedDescriptor.AttemptIndex,
		ExpectedDescriptor.RegionKind,
		ExpectedDescriptor.AuditHash,
		OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::ValidateStoredDescriptorForLifecycleUse(
	const FLayoutFrozenSubmissionStore& Store,
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const uint32 AuditHash,
	const TCHAR* const LifecycleContext,
	FString& OutFailureReason)
{
	OutFailureReason.Reset();
	if (DescriptorId.IsNone())
	{
		OutFailureReason = FString::Printf(
			TEXT("%s rejected stale frozen submission descriptor: descriptor id is missing."),
			LifecycleContext != nullptr ? LifecycleContext : TEXT("Layout lifecycle"));
		return false;
	}
	if (AttemptIndex < 0)
	{
		OutFailureReason = FString::Printf(
			TEXT("%s rejected stale frozen submission descriptor: attempt index is invalid."),
			LifecycleContext != nullptr ? LifecycleContext : TEXT("Layout lifecycle"));
		return false;
	}
	if (AuditHash == 0)
	{
		OutFailureReason = FString::Printf(
			TEXT("%s rejected stale frozen submission descriptor: audit hash is missing."),
			LifecycleContext != nullptr ? LifecycleContext : TEXT("Layout lifecycle"));
		return false;
	}

	const FLayoutFrozenSubmissionDescriptor* const Descriptor = FindForSubmit(
		Store,
		DescriptorId,
		RegionGroupId,
		Generation,
		AttemptIndex,
		RegionKind,
		AuditHash,
		OutFailureReason);
	if (Descriptor != nullptr)
	{
		return true;
	}

	const FString BaseFailureReason = OutFailureReason.IsEmpty()
		? TEXT("Frozen submission descriptor is stale.")
		: OutFailureReason;
	OutFailureReason = FString::Printf(
		TEXT("%s rejected stale frozen submission descriptor: %s"),
		LifecycleContext != nullptr ? LifecycleContext : TEXT("Layout lifecycle"),
		*BaseFailureReason);
	return false;
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildPreflightDescriptorSeed(
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot,
	FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
	FString& OutFailureReason)
{
	OutSeed = FLayoutFrozenSubmissionDescriptorSeed();
	OutFailureReason.Reset();
	if (DescriptorId.IsNone())
	{
		OutFailureReason = TEXT("Preflight descriptor seed requires a descriptor id.");
		return false;
	}
	if (AttemptIndex < 0)
	{
		OutFailureReason = TEXT("Preflight descriptor seed requires a non-negative attempt index.");
		return false;
	}
	if (RegionKind == ELayoutFrozenSubmissionRegionKind::Root
		&& Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Root)
	{
		OutFailureReason = TEXT("Root preflight descriptor seed requires a root prewarm snapshot.");
		return false;
	}
	if (RegionKind == ELayoutFrozenSubmissionRegionKind::Continuation
		&& Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Continuation)
	{
		OutFailureReason = TEXT("Continuation preflight descriptor seed requires a continuation prewarm snapshot.");
		return false;
	}
	FString SnapshotFailureReason;
	if (!Snapshot.ValidateNoLiveObjectCarriers(SnapshotFailureReason))
	{
		OutFailureReason = SnapshotFailureReason.IsEmpty()
			? TEXT("Preflight descriptor seed requires a pointer-free frozen snapshot.")
			: SnapshotFailureReason;
		return false;
	}

	OutSeed.DescriptorId = DescriptorId;
	OutSeed.RegionGroupId = RegionGroupId;
	OutSeed.Generation = Generation;
	OutSeed.AttemptIndex = AttemptIndex;
	OutSeed.RegionKind = RegionKind;
	OutSeed.Snapshot = Snapshot;
	return true;
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildManifestPrewarmInputFromSeed(
	const FLayoutFrozenSubmissionDescriptorSeed& Seed,
	FLayoutManifestPrewarmInput& OutInput,
	FString& OutFailureReason)
{
	OutInput = FLayoutManifestPrewarmInput();
	OutFailureReason.Reset();
	if (Seed.DescriptorId.IsNone())
	{
		OutFailureReason = TEXT("Manifest prewarm input requires a descriptor seed id.");
		return false;
	}
	if (Seed.AttemptIndex < 0)
	{
		OutFailureReason = TEXT("Manifest prewarm input requires a non-negative descriptor seed attempt index.");
		return false;
	}
	if (Seed.RegionKind == ELayoutFrozenSubmissionRegionKind::Root
		&& Seed.Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Root)
	{
		OutFailureReason = TEXT("Root manifest prewarm input requires a root descriptor seed snapshot.");
		return false;
	}
	if (Seed.RegionKind == ELayoutFrozenSubmissionRegionKind::Continuation
		&& Seed.Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Continuation)
	{
		OutFailureReason = TEXT("Continuation manifest prewarm input requires a continuation descriptor seed snapshot.");
		return false;
	}
	FString SnapshotFailureReason;
	if (!Seed.Snapshot.ValidateNoLiveObjectCarriers(SnapshotFailureReason))
	{
		OutFailureReason = SnapshotFailureReason.IsEmpty()
			? TEXT("Manifest prewarm input requires a pointer-free descriptor seed snapshot.")
			: SnapshotFailureReason;
		return false;
	}
	if (!ValidateDescriptorMetadataForPreflight(Seed.RegionKind, Seed.Snapshot, OutFailureReason))
	{
		return false;
	}
	return Seed.Snapshot.TryBuildManifestPrewarmInput(OutInput, OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
	const FLayoutSelectedRootScoutResultPrewarmDescriptorInput& Input,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFrozenDescriptorArtifact();
	OutFailureReason.Reset();
	if (Input.ScoutResultId.IsNone())
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor requires a scout result id.");
		return false;
	}
	if (Input.Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Root)
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor requires a root prewarm snapshot.");
		return false;
	}
	if (!Input.Snapshot.bHasWorkerSolvePacket)
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor requires a frozen worker packet.");
		return false;
	}
	if (!Input.Snapshot.WorkerSolvePacket.bHasSelectedModePlan
		|| !Input.Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan)
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor requires a frozen selected mode plan.");
		return false;
	}
	if (!Input.Snapshot.WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission)
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor requires a frozen root placement submission.");
		return false;
	}

	const int32 ExpectedSolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
		Input.FinalizedSiteCenterBlockWorldPos,
		Input.WorldSeed);
	if (Input.Snapshot.WorkerSolvePacket.SolveSeed != ExpectedSolveSeed
		|| Input.Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed != ExpectedSolveSeed)
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor rejected stale solve seed for finalized scout result site.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.RequestManifest.bHasRootPlacementSubmission
		&& Input.Snapshot.WorkerSolvePacket.RequestManifest.RootSiteCenterBlockWorldPos != Input.FinalizedSiteCenterBlockWorldPos)
	{
		OutFailureReason = TEXT("Selected root scout prewarm descriptor rejected mismatched finalized site center.");
		return false;
	}

	return TryBuildProducedDescriptorArtifactFromSnapshotInternal(
		Input.ArtifactId,
		Input.DescriptorId,
		Input.RegionGroupId,
		Input.Generation,
		Input.AttemptIndex,
		ELayoutFrozenSubmissionRegionKind::Root,
		Input.Snapshot,
		OutArtifact,
		OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
	const FLayoutId ArtifactId,
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const FPlannedLayoutSiteRecord& PlanningRecord,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot,
	FLayoutSelectedRootScoutResultPrewarmDescriptorInput& OutInput,
	FString& OutFailureReason)
{
	OutInput = FLayoutSelectedRootScoutResultPrewarmDescriptorInput();
	OutFailureReason.Reset();
	if (PlanningRecord.DiscoveryCandidateId.IsNone())
	{
		OutFailureReason = TEXT("Selected root scout descriptor input requires an explicit discovery candidate id.");
		return false;
	}
	if (PlanningRecord.DiscoveryOrderIndex < 0)
	{
		OutFailureReason = TEXT("Selected root scout descriptor input requires a non-negative persisted discovery order index.");
		return false;
	}
	if (PlanningRecord.DiscoveryOrderingId.IsNone())
	{
		OutFailureReason = TEXT("Selected root scout descriptor input requires a persisted discovery ordering id.");
		return false;
	}
	if (PlanningRecord.DiscoveryPlacementShiftId.IsNone())
	{
		OutFailureReason = TEXT("Selected root scout descriptor input requires a persisted placement shift id.");
		return false;
	}
	const FLayoutId ExpectedPlacementShiftId = FLayoutContractPlacementCandidates::BuildPlacementShiftId(PlanningRecord.DiscoveryPlacementShiftCells);
	if (PlanningRecord.DiscoveryPlacementShiftId != ExpectedPlacementShiftId)
	{
		OutFailureReason = TEXT("Selected root scout descriptor input rejected mismatched persisted placement shift id/cells.");
		return false;
	}

	const FLayoutPlannedSiteReservationSourceSelection ReservationSelection = PlanningRecord.GetPlannedSiteReservationSourceSelection();
	const int32 ExpectedSolveSeed = FLayoutContractPlacementCandidates::BuildFinalizedScoutResultSolveSeed(
		ReservationSelection.SiteCenterBlockWorldPos,
		ReservationSelection.WorldSeed);
	if (PlanningRecord.DiscoverySolveSeed != ExpectedSolveSeed)
	{
		OutFailureReason = TEXT("Selected root scout descriptor input rejected mismatched persisted discovery solve seed.");
		return false;
	}

	OutInput.ArtifactId = ArtifactId;
	OutInput.DescriptorId = DescriptorId;
	OutInput.ScoutResultId = PlanningRecord.DiscoveryCandidateId;
	OutInput.RegionGroupId = RegionGroupId;
	OutInput.Generation = Generation;
	OutInput.AttemptIndex = AttemptIndex;
	OutInput.WorldSeed = ReservationSelection.WorldSeed;
	OutInput.FinalizedSiteCenterBlockWorldPos = ReservationSelection.SiteCenterBlockWorldPos;
	OutInput.Snapshot = Snapshot;
	return true;
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
	const FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput& Input,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFrozenDescriptorArtifact();
	OutFailureReason.Reset();
	if (Input.EdgeScoutResultId.IsNone())
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires an edge scout result id.");
		return false;
	}
	if (Input.Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Continuation)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires a continuation prewarm snapshot.");
		return false;
	}
	if (!Input.Snapshot.bHasWorkerSolvePacket)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires a frozen worker packet.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::Continuation)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires a continuation worker packet.");
		return false;
	}
	if (!Input.Snapshot.WorkerSolvePacket.bHasSelectedModePlan
		|| !Input.Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires a frozen selected mode plan.");
		return false;
	}
	if (!Input.Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bRequireAllTraversalChannelsReachable)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires traversal-reachable profile metadata.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.RequestManifest.RootContinuationSelection.ResolvedEntryLevel == INDEX_NONE)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor requires resolved continuation entry metadata.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.SolveSeed != Input.ExpectedSolveSeed
		|| Input.Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed != Input.ExpectedSolveSeed)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor rejected stale solve seed for finalized edge scout result.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos != Input.FinalizedPrimaryBlockWorldPos)
	{
		OutFailureReason = TEXT("Selected continuation scout prewarm descriptor rejected mismatched finalized primary endpoint position.");
		return false;
	}

	return TryBuildProducedDescriptorArtifactFromSnapshotInternal(
		Input.ArtifactId,
		Input.DescriptorId,
		Input.RegionGroupId,
		Input.Generation,
		Input.AttemptIndex,
		ELayoutFrozenSubmissionRegionKind::Continuation,
		Input.Snapshot,
		OutArtifact,
		OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
	const FLayoutSelectedChildScoutResultPrewarmDescriptorInput& Input,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFrozenDescriptorArtifact();
	OutFailureReason.Reset();
	if (Input.ChildScoutResultId.IsNone())
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a child scout result id.");
		return false;
	}
	if (Input.ExpectedStableChildKey.IsEmpty())
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a stable child key.");
		return false;
	}
	if (Input.ExpectedProofCertificateId.IsNone())
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a proof certificate id.");
		return false;
	}
	if (!Input.Snapshot.bHasWorkerSolvePacket)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a frozen worker packet.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.Kind != ELayoutWorkerSolvePacketKind::Child)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a child worker packet.");
		return false;
	}
	if (!Input.Snapshot.WorkerSolvePacket.bHasFrozenChildHandoff)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a frozen parent-certified child handoff.");
		return false;
	}
	if (!Input.Snapshot.WorkerSolvePacket.bHasSelectedModePlan
		|| !Input.Snapshot.WorkerSolvePacket.RequestManifest.bHasSelectedModePlan)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor requires a frozen selected mode plan.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.RequestManifest.ProfilePath != Input.Snapshot.WorkerSolvePacket.FrozenChildHandoff.ContentMetadata.ChildProfilePath)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor rejected mismatched child profile metadata.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.RequestManifest.SourceContentEntryId != Input.Snapshot.WorkerSolvePacket.FrozenChildHandoff.ContentMetadata.SourceContentEntryId)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor rejected mismatched source content-entry metadata.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.SolveSeed != Input.ExpectedSolveSeed
		|| Input.Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed != Input.ExpectedSolveSeed)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor rejected stale solve seed for certified child branch.");
		return false;
	}
	if (Input.Snapshot.WorkerSolvePacket.PrimaryBlockWorldPos != Input.FinalizedPrimaryBlockWorldPos)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor rejected mismatched finalized child anchor.");
		return false;
	}
	const FLayoutChildSolveHandoff& Handoff = Input.Snapshot.WorkerSolvePacket.FrozenChildHandoff;
	if (Handoff.StableChildKey != Input.ExpectedStableChildKey)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor rejected mismatched stable child key.");
		return false;
	}
	if (Handoff.ProofCertificate.CertificateId != Input.ExpectedProofCertificateId)
	{
		OutFailureReason = TEXT("Selected child scout prewarm descriptor rejected mismatched proof certificate id.");
		return false;
	}

	return TryBuildProducedDescriptorArtifactFromSnapshotInternal(
		Input.ArtifactId,
		Input.DescriptorId,
		Input.RegionGroupId,
		Input.Generation,
		Input.AttemptIndex,
		ELayoutFrozenSubmissionRegionKind::Child,
		Input.Snapshot,
		OutArtifact,
		OutFailureReason);
}

namespace
{
bool TryBuildProducedDescriptorArtifactFromSnapshotInternal(
	const FLayoutId ArtifactId,
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFrozenDescriptorArtifact();
	OutFailureReason.Reset();
	FLayoutFrozenSubmissionDescriptorSeed Seed;
	if (!LayoutFrozenSubmissionDescriptorProducer::TryBuildPreflightDescriptorSeed(
			DescriptorId,
			RegionGroupId,
			Generation,
			AttemptIndex,
			RegionKind,
			Snapshot,
			Seed,
			OutFailureReason))
	{
		return false;
	}

	// Pre-compute the adapter output during descriptor production so the frozen contract
	// and all derived fields are available before solve dispatch.  This unifies the
	// planning-root, explicit-root, and connector paths — all three receive the adapter
	// output from the descriptor seed rather than recomputing it inside RunSharedRootSolveWork.
	//
	// When the adapter rejects the candidate, the descriptor artifact is not produced;
	// the lifecycle sequencer treats this as a terminal prewarm failure.
	if (Seed.Snapshot.bHasWorkerSolvePacket)
	{
		FLayoutRegionSolveRequest PrecomputeRequest;
		FString FinalizeFailureReason;
		if (LayoutWorkerSolveRequestFinalizer::FinalizeRequestFromPacket(
				Seed.Snapshot.WorkerSolvePacket,
				PrecomputeRequest,
				FinalizeFailureReason))
		{
			FString PrecomputeFailureReason;
			if (FLayoutContractPipeline::TryPrecomputeAdapterOutput(
					PrecomputeRequest,
					PrecomputeFailureReason))
			{
				FLayoutAdapterOutput& Output =
					Seed.Snapshot.WorkerSolvePacket.PrecomputedAdapterOutput;
				Output.bSucceeded = true;
				Output.ModePlan = PrecomputeRequest.SelectedModePlan;
				Output.PlannedCells = PrecomputeRequest.PlannedCells;
				Output.bHasFinalizedSteppedTerrainIntents = PrecomputeRequest.bHasFinalizedSteppedTerrainIntents;
				Output.VerticalAccessHostGroups = PrecomputeRequest.VerticalAccessHostGroups;
				Output.ActiveCells = PrecomputeRequest.PrecomputedActiveCells;
				Output.FrozenTerrainContract =
					PrecomputeRequest.PrecomputedFrozenTerrainContract;
				Output.SteppedTerrainSupportMap =
					PrecomputeRequest.PrecomputedSteppedTerrainSupportMap;
				Output.QualifiedEntryCells =
					PrecomputeRequest.QualifiedEntryCells;

				Seed.Snapshot.WorkerSolvePacket.bHasPrecomputedAdapterOutput = true;
			}
			else
			{
				OutFailureReason = PrecomputeFailureReason.IsEmpty()
					? TEXT("Descriptor production could not finalize the adapter contract.")
					: PrecomputeFailureReason;
				return false;
			}
		}
		else
		{
			OutFailureReason = FinalizeFailureReason.IsEmpty()
				? TEXT("Descriptor production could not finalize its worker request.")
				: FinalizeFailureReason;
			return false;
		}
	}

	return LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedInternal(
		ArtifactId,
		Seed,
		OutArtifact,
		OutFailureReason);
}
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedInternal(
	const FLayoutId ArtifactId,
	const FLayoutFrozenSubmissionDescriptorSeed& Seed,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	OutArtifact = FLayoutProducedFrozenDescriptorArtifact();
	OutFailureReason.Reset();
	if (ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Produced frozen descriptor artifact requires an artifact id.");
		return false;
	}
	FString SnapshotFailureReason;
	if (!Seed.Snapshot.ValidateNoLiveObjectCarriers(SnapshotFailureReason))
	{
		OutFailureReason = SnapshotFailureReason.IsEmpty()
			? TEXT("Produced frozen descriptor artifact requires a pointer-free descriptor seed snapshot.")
			: SnapshotFailureReason;
		return false;
	}

	if (!ValidateDescriptorMetadataForPreflight(Seed.RegionKind, Seed.Snapshot, OutFailureReason))
	{
		return false;
	}

	FLayoutManifestPrewarmInput ManifestPrewarmInput;
	if (!TryBuildManifestPrewarmInputFromSeed(Seed, ManifestPrewarmInput, OutFailureReason))
	{
		return false;
	}

	OutArtifact.bHasProducedArtifact = true;
	OutArtifact.ArtifactId = ArtifactId;
	OutArtifact.DescriptorSeed = Seed;
	OutArtifact.DescriptorAuditHash = BuildProducedDescriptorSeedAuditHash(Seed);
	OutArtifact.ManifestPrewarmAuditHash = BuildProducedManifestPrewarmAuditHash(ManifestPrewarmInput);
	OutArtifact.ManifestPrewarmInput = MoveTemp(ManifestPrewarmInput);
	return true;
}

#if WITH_AUTOMATION_TESTS
FLayoutFrozenSubmissionDescriptor LayoutFrozenSubmissionDescriptorProducer::BuildDescriptorFromSnapshotForTests(
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot)
{
	return BuildDescriptorFromSnapshot(
		DescriptorId,
		RegionGroupId,
		Generation,
		AttemptIndex,
		RegionKind,
		Snapshot);
}

bool LayoutFrozenSubmissionDescriptorProducer::StoreAcceptedDescriptorForTests(
	FLayoutFrozenSubmissionStore& Store,
	const FLayoutFrozenSubmissionDescriptor& Descriptor,
	FString& OutFailureReason)
{
	return StoreAcceptedDescriptor(Store, Descriptor, OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildPreflightDescriptorSeedForTests(
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot,
	FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
	FString& OutFailureReason)
{
	return TryBuildPreflightDescriptorSeed(
		DescriptorId,
		RegionGroupId,
		Generation,
		AttemptIndex,
		RegionKind,
		Snapshot,
		OutSeed,
		OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildManifestPrewarmInputFromSeedForTests(
	const FLayoutFrozenSubmissionDescriptorSeed& Seed,
	FLayoutManifestPrewarmInput& OutInput,
	FString& OutFailureReason)
{
	return TryBuildManifestPrewarmInputFromSeed(Seed, OutInput, OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryBuildProducedDescriptorArtifactFromSeedForTests(
	const FLayoutId ArtifactId,
	const FLayoutFrozenSubmissionDescriptorSeed& Seed,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	return TryBuildProducedDescriptorArtifactFromSeedInternal(
		ArtifactId,
		Seed,
		OutArtifact,
		OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionForTests(
	const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
	FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
	FString& OutFailureReason)
{
	return TryGetSeedForSolveSubmissionInternal(ProducedArtifact, OutSeed, OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionForTests(
	const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
	FLayoutManifestPrewarmInput& OutInput,
	FString& OutFailureReason)
{
	return TryGetManifestPrewarmInputForSolveSubmissionInternal(ProducedArtifact, OutInput, OutFailureReason);
}
#endif

bool LayoutFrozenSubmissionDescriptorProducer::TryGetSeedForSolveSubmissionInternal(
	const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
	FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
	FString& OutFailureReason)
{
	OutSeed = FLayoutFrozenSubmissionDescriptorSeed();
	OutFailureReason.Reset();
	if (ProducedArtifact == nullptr || !ProducedArtifact->bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Solve submission requires an explicit produced frozen descriptor artifact.");
		return false;
	}
	if (ProducedArtifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with no artifact id.");
		return false;
	}
	FLayoutFrozenSubmissionDescriptorSeed RevalidatedSeed;
	if (!TryBuildPreflightDescriptorSeed(
			ProducedArtifact->DescriptorSeed.DescriptorId,
			ProducedArtifact->DescriptorSeed.RegionGroupId,
			ProducedArtifact->DescriptorSeed.Generation,
			ProducedArtifact->DescriptorSeed.AttemptIndex,
			ProducedArtifact->DescriptorSeed.RegionKind,
			ProducedArtifact->DescriptorSeed.Snapshot,
			RevalidatedSeed,
			OutFailureReason))
	{
		return false;
	}
	if (!ValidateDescriptorMetadataForPreflight(
			RevalidatedSeed.RegionKind,
			RevalidatedSeed.Snapshot,
			OutFailureReason))
	{
		return false;
	}
	const uint32 RecomputedDescriptorAuditHash = BuildProducedDescriptorSeedAuditHash(RevalidatedSeed);
	if (ProducedArtifact->DescriptorAuditHash == 0
		|| ProducedArtifact->DescriptorAuditHash != RecomputedDescriptorAuditHash)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale descriptor audit hash.");
		return false;
	}
	OutSeed = MoveTemp(RevalidatedSeed);
	return true;
}

bool LayoutFrozenSubmissionDescriptorProducer::TryGetManifestPrewarmInputForSolveSubmissionInternal(
	const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
	FLayoutManifestPrewarmInput& OutInput,
	FString& OutFailureReason)
{
	OutInput = FLayoutManifestPrewarmInput();
	OutFailureReason.Reset();
	if (ProducedArtifact == nullptr || !ProducedArtifact->bHasProducedArtifact)
	{
		OutFailureReason = TEXT("Solve submission requires an explicit produced frozen descriptor artifact before prewarm input can be consumed.");
		return false;
	}
	if (ProducedArtifact->ArtifactId.IsNone())
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with no artifact id.");
		return false;
	}
	FLayoutFrozenSubmissionDescriptorSeed RevalidatedSeed;
	if (!TryBuildPreflightDescriptorSeed(
			ProducedArtifact->DescriptorSeed.DescriptorId,
			ProducedArtifact->DescriptorSeed.RegionGroupId,
			ProducedArtifact->DescriptorSeed.Generation,
			ProducedArtifact->DescriptorSeed.AttemptIndex,
			ProducedArtifact->DescriptorSeed.RegionKind,
			ProducedArtifact->DescriptorSeed.Snapshot,
			RevalidatedSeed,
			OutFailureReason))
	{
		return false;
	}
	if (!ValidateDescriptorMetadataForPreflight(
			RevalidatedSeed.RegionKind,
			RevalidatedSeed.Snapshot,
			OutFailureReason))
	{
		return false;
	}
	const uint32 RecomputedDescriptorAuditHash = BuildProducedDescriptorSeedAuditHash(RevalidatedSeed);
	if (ProducedArtifact->DescriptorAuditHash == 0
		|| ProducedArtifact->DescriptorAuditHash != RecomputedDescriptorAuditHash)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale descriptor audit hash before manifest prewarm.");
		return false;
	}
	if (ProducedArtifact->DescriptorSeed.RegionKind == ELayoutFrozenSubmissionRegionKind::Child)
	{
		OutFailureReason = TEXT("Child solve submission requires the frozen descriptor seed and certified handoff path, not root/continuation manifest prewarm input.");
		return false;
	}
	if (!ProducedArtifact->ManifestPrewarmInput.bHasFrozenRequestManifest)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with no frozen manifest prewarm input.");
		return false;
	}
	if (ProducedArtifact->ManifestPrewarmInput.PrewarmId != ProducedArtifact->DescriptorSeed.Snapshot.SnapshotId)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale manifest prewarm id.");
		return false;
	}
	if (ProducedArtifact->ManifestPrewarmInput.Kind != ProducedArtifact->DescriptorSeed.Snapshot.PrewarmKind)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale manifest prewarm kind.");
		return false;
	}
	if (ProducedArtifact->DescriptorSeed.Snapshot.bHasWorkerSolvePacket
		&& ProducedArtifact->ManifestPrewarmInput.FrozenRequestManifest.CapturedSeed != ProducedArtifact->DescriptorSeed.Snapshot.WorkerSolvePacket.RequestManifest.CapturedSeed)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale manifest solve seed.");
		return false;
	}
	if (ProducedArtifact->DescriptorSeed.Snapshot.bHasWorkerSolvePacket
		&& ProducedArtifact->ManifestPrewarmInput.FrozenRequestManifest.SelectedModePlan.ModePlanId != ProducedArtifact->DescriptorSeed.Snapshot.WorkerSolvePacket.RequestManifest.SelectedModePlan.ModePlanId)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale manifest selected mode plan.");
		return false;
	}
	FLayoutPreSubmitFrozenSnapshot ManifestSnapshot = ProducedArtifact->DescriptorSeed.Snapshot;
	ManifestSnapshot.WorkerSolvePacket.RequestManifest = ProducedArtifact->ManifestPrewarmInput.FrozenRequestManifest;
	if (!ValidateDescriptorMetadataForPreflight(
			ProducedArtifact->DescriptorSeed.RegionKind,
			ManifestSnapshot,
			OutFailureReason))
	{
		return false;
	}
	const uint32 RecomputedManifestAuditHash = BuildProducedManifestPrewarmAuditHash(ProducedArtifact->ManifestPrewarmInput);
	if (ProducedArtifact->ManifestPrewarmAuditHash == 0
		|| ProducedArtifact->ManifestPrewarmAuditHash != RecomputedManifestAuditHash)
	{
		OutFailureReason = TEXT("Solve submission rejected a produced descriptor artifact with stale manifest prewarm audit hash.");
		return false;
	}
	OutInput = ProducedArtifact->ManifestPrewarmInput;

	// Populate the pre-submit snapshot and any pre-existing terrain evidence.
	OutInput.bHasPreSubmitSnapshot = ProducedArtifact->DescriptorSeed.Snapshot.bHasWorkerSolvePacket;
	if (OutInput.bHasPreSubmitSnapshot)
	{
		OutInput.PreSubmitSnapshot = ProducedArtifact->DescriptorSeed.Snapshot;
	}
	if (OutInput.FrozenRequestManifest.bHasFrozenTerrainBiomeAdapterInput)
	{
		OutInput.bHasFrozenTerrainBiomeAdapterInput = true;
		OutInput.FrozenTerrainBiomeAdapterInput = OutInput.FrozenRequestManifest.FrozenTerrainBiomeAdapterInput;
	}

	return true;
}

bool LayoutFrozenSubmissionDescriptorProducer::TryStoreDescriptorFromSuccessfulPreflightArtifact(
	const FLayoutBackgroundSolveCompletion& PreflightCompletion,
	const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
	FLayoutFrozenSubmissionStore& Store,
	const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
	FLayoutFrozenSubmissionDescriptor& OutDescriptor,
	FString& OutFailureReason)
{
	OutDescriptor = FLayoutFrozenSubmissionDescriptor();
	OutFailureReason.Reset();
	FLayoutFrozenSubmissionDescriptorSeed Seed;
	if (!TryGetSeedForSolveSubmissionInternal(ProducedArtifact, Seed, OutFailureReason))
	{
		return false;
	}
	return TryStoreDescriptorFromSuccessfulPreflightSeed(
		PreflightCompletion,
		PreflightResult,
		Store,
		Seed,
		OutDescriptor,
		OutFailureReason);
}

namespace
{
bool TryStoreDescriptorFromSuccessfulPreflightSeed(
	const FLayoutBackgroundSolveCompletion& PreflightCompletion,
	const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
	FLayoutFrozenSubmissionStore& Store,
	const FLayoutFrozenSubmissionDescriptorSeed& Seed,
	FLayoutFrozenSubmissionDescriptor& OutDescriptor,
	FString& OutFailureReason)
{
	OutDescriptor = FLayoutFrozenSubmissionDescriptor();
	OutFailureReason.Reset();
	if (PreflightCompletion.LifecycleStage != ELayoutBackgroundSolveLifecycleStage::AdmissibilityPreflight)
	{
		OutFailureReason = TEXT("Frozen descriptor storage requires an admissibility-preflight completion.");
		return false;
	}
	if (PreflightCompletion.bCanceled)
	{
		OutFailureReason = TEXT("Frozen descriptor storage rejected a canceled preflight completion.");
		return false;
	}
	if (!PreflightCompletion.bWorkSucceeded || !PreflightResult.bAdmissible)
	{
		OutFailureReason = !PreflightCompletion.FailureReason.IsEmpty()
			? PreflightCompletion.FailureReason
			: (PreflightResult.FailureReason.IsEmpty()
				? TEXT("Frozen descriptor storage rejected a non-admissible preflight result.")
				: PreflightResult.FailureReason);
		return false;
	}
	if (Seed.DescriptorId.IsNone())
	{
		OutFailureReason = TEXT("Frozen descriptor storage requires a descriptor id.");
		return false;
	}
	FString SnapshotFailureReason;
	if (!Seed.Snapshot.ValidateNoLiveObjectCarriers(SnapshotFailureReason))
	{
		OutFailureReason = SnapshotFailureReason.IsEmpty()
			? TEXT("Frozen descriptor storage requires a pointer-free snapshot.")
			: SnapshotFailureReason;
		return false;
	}

	OutDescriptor = LayoutFrozenSubmissionDescriptorProducer::BuildDescriptorFromSnapshot(
		Seed.DescriptorId,
		Seed.RegionGroupId,
		Seed.Generation,
		Seed.AttemptIndex,
		Seed.RegionKind,
		Seed.Snapshot);
	return LayoutFrozenSubmissionDescriptorProducer::StoreAcceptedDescriptor(Store, OutDescriptor, OutFailureReason);
}

} // closes anonymous namespace

bool LayoutFrozenSubmissionDescriptorProducer::BuildPreflightDescriptorSeed(
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot,
	FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
	FString& OutFailureReason)
{
	return TryBuildPreflightDescriptorSeed(
		DescriptorId, RegionGroupId, Generation, AttemptIndex, RegionKind, Snapshot,
		OutSeed, OutFailureReason);
}

bool LayoutFrozenSubmissionDescriptorProducer::BuildProducedDescriptorArtifactFromSeed(
	const FLayoutId ArtifactId,
	const FLayoutFrozenSubmissionDescriptorSeed& Seed,
	FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
	FString& OutFailureReason)
{
	return TryBuildProducedDescriptorArtifactFromSeedInternal(
		ArtifactId, Seed, OutArtifact, OutFailureReason);
}
