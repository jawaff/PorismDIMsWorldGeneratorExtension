// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutFrozenSubmissionStore.h"

namespace
{
uint32 HashName(const FLayoutId Name)
{
	return GetTypeHash(Name);
}
}

bool FLayoutFrozenSubmissionDescriptor::Validate(FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (DescriptorId.IsNone())
	{
		OutFailureReason = TEXT("Frozen submission descriptor requires a descriptor id.");
		return false;
	}
	if (AttemptIndex < 0)
	{
		OutFailureReason = TEXT("Frozen submission descriptor requires a non-negative attempt index.");
		return false;
	}
	if (AuditHash == 0)
	{
		OutFailureReason = TEXT("Frozen submission descriptor requires an audit hash.");
		return false;
	}
	if (RegionKind == ELayoutFrozenSubmissionRegionKind::Root
		&& Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Root)
	{
		OutFailureReason = TEXT("Frozen submission descriptor root kind requires a root prewarm snapshot.");
		return false;
	}
	if (RegionKind == ELayoutFrozenSubmissionRegionKind::Continuation
		&& Snapshot.PrewarmKind != ELayoutManifestPrewarmKind::Continuation)
	{
		OutFailureReason = TEXT("Frozen submission descriptor continuation kind requires a continuation prewarm snapshot.");
		return false;
	}
	if (Snapshot.bHasWorkerSolvePacket)
	{
		const ELayoutWorkerSolvePacketKind PacketKind = Snapshot.WorkerSolvePacket.Kind;
		const bool bRootDescriptor = RegionKind == ELayoutFrozenSubmissionRegionKind::Root
			&& (PacketKind == ELayoutWorkerSolvePacketKind::PlanningRoot
				|| PacketKind == ELayoutWorkerSolvePacketKind::ExplicitPreviewRoot
				|| PacketKind == ELayoutWorkerSolvePacketKind::ObservedFallbackRoot);
		const bool bContinuationDescriptor = RegionKind == ELayoutFrozenSubmissionRegionKind::Continuation
			&& PacketKind == ELayoutWorkerSolvePacketKind::Continuation;
		const bool bChildDescriptor = RegionKind == ELayoutFrozenSubmissionRegionKind::Child
			&& PacketKind == ELayoutWorkerSolvePacketKind::Child;
		if (!bRootDescriptor && !bContinuationDescriptor && !bChildDescriptor)
		{
			OutFailureReason = TEXT("Frozen submission descriptor region kind does not match its worker packet kind.");
			return false;
		}
	}
	const uint32 ExpectedAuditHash = FLayoutFrozenSubmissionStore::BuildAuditHash(
		DescriptorId,
		RegionGroupId,
		Generation,
		AttemptIndex,
		RegionKind,
		Snapshot);
	if (AuditHash != ExpectedAuditHash)
	{
		OutFailureReason = TEXT("Frozen submission descriptor audit hash does not match its payload.");
		return false;
	}
	return Snapshot.ValidateNoLiveObjectCarriers(OutFailureReason);
}

uint32 FLayoutFrozenSubmissionStore::BuildAuditHash(
	const FLayoutId DescriptorId,
	const uint64 RegionGroupId,
	const uint64 Generation,
	const int32 AttemptIndex,
	const ELayoutFrozenSubmissionRegionKind RegionKind,
	const FLayoutPreSubmitFrozenSnapshot& Snapshot)
{
	uint32 Hash = HashName(DescriptorId);
	Hash = HashCombine(Hash, GetTypeHash(RegionGroupId));
	Hash = HashCombine(Hash, GetTypeHash(Generation));
	Hash = HashCombine(Hash, GetTypeHash(AttemptIndex));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(RegionKind)));
	Hash = HashCombine(Hash, HashName(Snapshot.SnapshotId));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Snapshot.PrewarmKind)));
	if (Snapshot.bHasWorkerSolvePacket)
	{
		Hash = HashCombine(Hash, HashName(Snapshot.WorkerSolvePacket.SelectedModePlan.ModePlanId));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.WorkerSolvePacket.SolveSeed));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bSupportsSteppedTerrainSolve));
		Hash = HashCombine(Hash, GetTypeHash(Snapshot.WorkerSolvePacket.RequestManifest.ProfileSnapshot.bEnableTerrainSeams));
	}
	return Hash != 0 ? Hash : 1;
}

bool FLayoutFrozenSubmissionStore::AddOrReplace(
	const FLayoutFrozenSubmissionDescriptor& Descriptor,
	FString& OutFailureReason)
{
	if (!Descriptor.Validate(OutFailureReason))
	{
		return false;
	}
	if (DescriptorsById.Contains(Descriptor.DescriptorId))
	{
		OutFailureReason = TEXT("Frozen submission descriptor replacement conflicts with an active descriptor id.");
		return false;
	}
	for (const TPair<FLayoutId, FLayoutFrozenSubmissionDescriptor>& ExistingDescriptorPair : DescriptorsById)
	{
		const FLayoutFrozenSubmissionDescriptor& ExistingDescriptor = ExistingDescriptorPair.Value;
		if (ExistingDescriptor.RegionGroupId == Descriptor.RegionGroupId
			&& ExistingDescriptor.Generation == Descriptor.Generation
			&& ExistingDescriptor.RegionKind == Descriptor.RegionKind)
		{
			OutFailureReason = TEXT("Frozen submission descriptor conflicts with an active region group and generation.");
			return false;
		}
	}
	DescriptorsById.Add(Descriptor.DescriptorId, Descriptor);
	return true;
}

const FLayoutFrozenSubmissionDescriptor* FLayoutFrozenSubmissionStore::FindValid(
	const FLayoutId DescriptorId,
	const uint64 ExpectedRegionGroupId,
	const uint64 ExpectedGeneration,
	const int32 ExpectedAttemptIndex,
	const ELayoutFrozenSubmissionRegionKind ExpectedRegionKind,
	const uint32 ExpectedAuditHash,
	FString& OutFailureReason) const
{
	OutFailureReason.Reset();
	if (DescriptorId.IsNone())
	{
		OutFailureReason = TEXT("Frozen submission descriptor lookup requires a descriptor id.");
		return nullptr;
	}
	if (ExpectedAttemptIndex < 0)
	{
		OutFailureReason = TEXT("Frozen submission descriptor lookup requires a non-negative attempt index.");
		return nullptr;
	}
	if (ExpectedAuditHash == 0)
	{
		OutFailureReason = TEXT("Frozen submission descriptor lookup requires an audit hash.");
		return nullptr;
	}
	const FLayoutFrozenSubmissionDescriptor* const Descriptor = DescriptorsById.Find(DescriptorId);
	if (Descriptor == nullptr)
	{
		OutFailureReason = TEXT("Frozen submission descriptor is missing.");
		return nullptr;
	}
	if (Descriptor->RegionGroupId != ExpectedRegionGroupId)
	{
		OutFailureReason = TEXT("Frozen submission descriptor region group is stale.");
		return nullptr;
	}
	if (Descriptor->Generation != ExpectedGeneration)
	{
		OutFailureReason = TEXT("Frozen submission descriptor generation is stale.");
		return nullptr;
	}
	if (Descriptor->AttemptIndex != ExpectedAttemptIndex)
	{
		OutFailureReason = TEXT("Frozen submission descriptor attempt is stale.");
		return nullptr;
	}
	if (Descriptor->RegionKind != ExpectedRegionKind)
	{
		OutFailureReason = TEXT("Frozen submission descriptor kind is stale.");
		return nullptr;
	}
	if (Descriptor->AuditHash != ExpectedAuditHash)
	{
		OutFailureReason = TEXT("Frozen submission descriptor audit hash is stale.");
		return nullptr;
	}
	FString ValidationFailureReason;
	if (!Descriptor->Validate(ValidationFailureReason))
	{
		OutFailureReason = ValidationFailureReason;
		return nullptr;
	}
	return Descriptor;
}

bool FLayoutFrozenSubmissionStore::Remove(const FLayoutId DescriptorId)
{
	return DescriptorsById.Remove(DescriptorId) > 0;
}

void FLayoutFrozenSubmissionStore::Reset()
{
	DescriptorsById.Reset();
}
