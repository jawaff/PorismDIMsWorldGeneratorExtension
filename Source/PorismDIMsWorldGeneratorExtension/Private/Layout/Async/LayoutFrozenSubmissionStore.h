// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutPreSubmitFrozenSnapshot.h"

/** Runtime region kind used to audit frozen descriptor lookup without owning scheduler state. */
enum class ELayoutFrozenSubmissionRegionKind : uint8
{
	Root,
	Continuation,
	Child
};

/**
 * Pointer-free frozen solve descriptor kept only between scout acceptance and
 * publish/cancel cleanup. Planning/runtime records own lifecycle state.
 */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenSubmissionDescriptor
{
	/** Stable descriptor id selected by the planning/runtime record. */
	FLayoutId DescriptorId;

	/** Region graph node that owns this descriptor attempt. */
	uint64 RegionGroupId = 0;

	/** Planning/connector generation expected by the consuming record. */
	uint64 Generation = 0;

	/** Attempt index for deterministic retry and stale-completion rejection. */
	int32 AttemptIndex = 0;

	/** Region kind used for diagnostics and stale-kind rejection. */
	ELayoutFrozenSubmissionRegionKind RegionKind = ELayoutFrozenSubmissionRegionKind::Root;

	/** Lightweight audit hash copied by the runtime record before submit. */
	uint32 AuditHash = 0;

	/** Final pointer-free worker input captured before solve enqueue. */
	FLayoutPreSubmitFrozenSnapshot Snapshot;

	/** Validates descriptor identity and worker-safe payload before store insertion. */
	bool Validate(FString& OutFailureReason) const;
};

/**
 * Component-owned transient descriptor payload store. It performs lookup and
 * stale rejection only; planning/runtime records remain the scheduler authority.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenSubmissionStore
{
public:
	/** Builds the deterministic audit hash expected by descriptor consumers. */
	static uint32 BuildAuditHash(
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot);

	/** Adds one descriptor after validating it is worker-safe; active descriptor ids must be removed before retry/replacement. */
	bool AddOrReplace(const FLayoutFrozenSubmissionDescriptor& Descriptor, FString& OutFailureReason);

	/** Finds a descriptor and rejects stale id/generation/attempt/hash/kind mismatches. */
	const FLayoutFrozenSubmissionDescriptor* FindValid(
		FLayoutId DescriptorId,
		uint64 ExpectedRegionGroupId,
		uint64 ExpectedGeneration,
		int32 ExpectedAttemptIndex,
		ELayoutFrozenSubmissionRegionKind ExpectedRegionKind,
		uint32 ExpectedAuditHash,
		FString& OutFailureReason) const;

	/** Removes one descriptor during publish/cancel cleanup. */
	bool Remove(FLayoutId DescriptorId);

	/** Clears all transient descriptors for component teardown or generation reset. */
	void Reset();

private:
	TMap<FLayoutId, FLayoutFrozenSubmissionDescriptor> DescriptorsById;
};
