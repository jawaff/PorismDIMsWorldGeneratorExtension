// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundAdmissibilityPreflight.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"
#include "Layout/Async/LayoutFrozenSubmissionStore.h"
#include "Layout/Async/LayoutManifestPrewarm.h"

class AChunkWorldCore;
class FLayoutActiveBiomeSampler;
class ULayoutWorldBindingAsset;
struct FLayoutConnectorTerrainPathContext;
struct FLayoutNoiseCoordinateSettings;
struct FLayoutWorldBindingRuntimeView;
struct FPlannedLayoutSiteRecord;
struct FResolvedLayoutConnectorRecord;

/** Pointer-free descriptor seed carried from prewarm into successful scout/preflight publication. */
struct FLayoutFrozenSubmissionDescriptorSeed
{
	/** Descriptor id chosen before prewarm so lifecycle metadata and diagnostics remain stable. */
	FLayoutId DescriptorId;

	/** Region group or connector key this descriptor belongs to. */
	uint64 RegionGroupId = 0;

	/** Generation expected by later submit lookup. */
	uint64 Generation = 0;

	/** Attempt index expected by later submit lookup. */
	int32 AttemptIndex = 0;

	/** Root/continuation/child kind expected by later submit lookup. */
	ELayoutFrozenSubmissionRegionKind RegionKind = ELayoutFrozenSubmissionRegionKind::Root;

	/** Already-frozen pointer-free snapshot to seal into the descriptor after successful preflight. */
	FLayoutPreSubmitFrozenSnapshot Snapshot;
};

/** Selected root scout result plus already-frozen prewarm snapshot used to build one descriptor artifact. */
struct FLayoutSelectedRootScoutResultPrewarmDescriptorInput
{
	/** Stable produced descriptor artifact id for diagnostics. */
	FLayoutId ArtifactId;

	/** Descriptor id chosen before prewarm so lifecycle metadata and diagnostics remain stable. */
	FLayoutId DescriptorId;

	/** Stable root scout-result id selected by scout. */
	FLayoutId ScoutResultId;

	/** Region group this descriptor belongs to. */
	uint64 RegionGroupId = 0;

	/** Generation expected by later submit lookup. */
	uint64 Generation = 0;

	/** Attempt index expected by later submit lookup. */
	int32 AttemptIndex = 0;

	/** Final snapped or shifted site center selected by scout/prewarm. */
	FIntVector FinalizedSiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** World seed used to recompute the finalized solve seed. */
	int32 WorldSeed = 0;

	/** Already-frozen root snapshot to seal into the descriptor after successful preflight. */
	FLayoutPreSubmitFrozenSnapshot Snapshot;
};

/** Selected continuation edge scout result plus already-frozen prewarm snapshot used to build one descriptor artifact. */
struct FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput
{
	/** Stable produced descriptor artifact id for diagnostics. */
	FLayoutId ArtifactId;

	/** Descriptor id chosen before prewarm so lifecycle metadata and diagnostics remain stable. */
	FLayoutId DescriptorId;

	/** Stable continuation edge scout-result id selected by scout. */
	FLayoutId EdgeScoutResultId;

	/** Region group or connector key this descriptor belongs to. */
	uint64 RegionGroupId = 0;

	/** Generation expected by later submit lookup. */
	uint64 Generation = 0;

	/** Attempt index expected by later submit lookup. */
	int32 AttemptIndex = 0;

	/** Expected continuation solve seed selected by scout/prewarm. */
	int32 ExpectedSolveSeed = 0;

	/** Finalized source endpoint/site position selected by continuation scout. */
	FIntVector FinalizedPrimaryBlockWorldPos = FIntVector::ZeroValue;

	/** Already-frozen continuation snapshot to seal into the descriptor after successful preflight. */
	FLayoutPreSubmitFrozenSnapshot Snapshot;
};

/** Selected child branch scout result plus already-frozen parent-certified handoff snapshot used to build one descriptor artifact. */
struct FLayoutSelectedChildScoutResultPrewarmDescriptorInput
{
	/** Stable produced descriptor artifact id for diagnostics. */
	FLayoutId ArtifactId;

	/** Descriptor id chosen before prewarm so lifecycle metadata and diagnostics remain stable. */
	FLayoutId DescriptorId;

	/** Stable child scout/branch result id selected by parent negotiation. */
	FLayoutId ChildScoutResultId;

	/** Region group or parent branch key this descriptor belongs to. */
	uint64 RegionGroupId = 0;

	/** Generation expected by later submit lookup. */
	uint64 Generation = 0;

	/** Attempt index expected by later submit lookup. */
	int32 AttemptIndex = 0;

	/** Expected child solve seed selected before the frozen handoff was emitted. */
	int32 ExpectedSolveSeed = 0;

	/** Stable child key selected by parent negotiation. */
	FString ExpectedStableChildKey;

	/** Exact proof certificate id selected by parent negotiation. */
	FLayoutId ExpectedProofCertificateId;

	/** Finalized child anchor or offset carried by the frozen worker packet. */
	FIntVector FinalizedPrimaryBlockWorldPos = FIntVector::ZeroValue;

	/** Already-frozen child snapshot to seal into the descriptor after successful parent-certified prewarm. */
	FLayoutPreSubmitFrozenSnapshot Snapshot;
};

/** Explicit prewarm/scout-produced artifact required before solve submission may enqueue. */
struct FLayoutProducedFrozenDescriptorArtifact
{
	/** True only when an upstream prewarm/scout producer emitted this artifact. */
	bool bHasProducedArtifact = false;

	/** Stable artifact id for diagnostics. */
	FLayoutId ArtifactId;

	/** Descriptor seed that successful preflight will seal into the transient store. */
	FLayoutFrozenSubmissionDescriptorSeed DescriptorSeed;

	/** Audit hash of descriptor identity plus frozen snapshot payload at artifact production time. */
	uint32 DescriptorAuditHash = 0;

	/** Audit hash of the frozen manifest prewarm payload at artifact production time. */
	uint32 ManifestPrewarmAuditHash = 0;

	/** Frozen manifest prewarm input derived from the descriptor seed. */
	FLayoutManifestPrewarmInput ManifestPrewarmInput;
};

/**
 * Scout/admissibility-owned helper seam that creates frozen submission descriptors
 * before submit code consumes them through id/hash lookup only.
 */
namespace LayoutFrozenSubmissionDescriptorProducer
{
	/** Looks up one descriptor expected by submit and rejects stale identity/hash mismatches. */
	const FLayoutFrozenSubmissionDescriptor* FindForSubmit(
		const FLayoutFrozenSubmissionStore& Store,
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		uint32 AuditHash,
		FString& OutFailureReason);

	/** Looks up one descriptor expected by submit and rejects stale identity/hash mismatches. */
	const FLayoutFrozenSubmissionDescriptor* FindForSubmit(
		const FLayoutFrozenSubmissionStore& Store,
		const FLayoutFrozenSubmissionDescriptor& ExpectedDescriptor,
		FString& OutFailureReason);

	/** Revalidates stored descriptor identity before enqueue or publish can consume a descriptor-backed solve. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateStoredDescriptorForLifecycleUse(
		const FLayoutFrozenSubmissionStore& Store,
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		uint32 AuditHash,
		const TCHAR* LifecycleContext,
		FString& OutFailureReason);

#if WITH_AUTOMATION_TESTS
	/** Test-only selected-root descriptor producer kept so automation can validate typed failure cases; production uses LayoutRegionPrewarmDescriptorProducer. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedDescriptorArtifactFromSelectedRootScoutResult(
		const FLayoutSelectedRootScoutResultPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Test-only selected-root input builder kept so automation can validate planning-record scout metadata; production uses LayoutRegionPrewarmDescriptorProducer. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildSelectedRootScoutResultPrewarmInputFromPlanningRecord(
		FLayoutId ArtifactId,
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		const FPlannedLayoutSiteRecord& PlanningRecord,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FLayoutSelectedRootScoutResultPrewarmDescriptorInput& OutInput,
		FString& OutFailureReason);

	/** Test-only selected-continuation descriptor producer kept so automation can validate typed failure cases; production uses LayoutRegionPrewarmDescriptorProducer. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedDescriptorArtifactFromSelectedContinuationScoutResult(
		const FLayoutSelectedContinuationScoutResultPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Test-only selected-child descriptor producer kept so automation can validate typed failure cases; production uses LayoutRegionPrewarmDescriptorProducer. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedDescriptorArtifactFromSelectedChildScoutResult(
		const FLayoutSelectedChildScoutResultPrewarmDescriptorInput& Input,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Test-only descriptor constructor kept so automation can assert store behavior without exposing production seed/snapshot mutation APIs. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenSubmissionDescriptor BuildDescriptorFromSnapshotForTests(
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot);

	/** Test-only descriptor store mutation kept so production uses artifact-backed storage only. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool StoreAcceptedDescriptorForTests(
		FLayoutFrozenSubmissionStore& Store,
		const FLayoutFrozenSubmissionDescriptor& Descriptor,
		FString& OutFailureReason);

	/** Test-only seed builder kept so automation can validate prewarm seed invariants while production uses selected prewarm/scout artifacts. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildPreflightDescriptorSeedForTests(
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);

	/** Test-only manifest-prewarm builder kept so production solve submission consumes explicit produced artifacts. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildManifestPrewarmInputFromSeedForTests(
		const FLayoutFrozenSubmissionDescriptorSeed& Seed,
		FLayoutManifestPrewarmInput& OutInput,
		FString& OutFailureReason);

	/** Test-only compatibility seam for producing descriptor artifacts from an already-frozen seed; production must use selected prewarm/scout producers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildProducedDescriptorArtifactFromSeedForTests(
		FLayoutId ArtifactId,
		const FLayoutFrozenSubmissionDescriptorSeed& Seed,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Test-only produced-artifact seed consumer kept so production goes through LayoutRegionPrewarmDescriptorProducer kind wrappers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryGetSeedForSolveSubmissionForTests(
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);

	/** Test-only produced-artifact manifest consumer kept so production goes through LayoutRegionPrewarmDescriptorProducer kind wrappers. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryGetManifestPrewarmInputForSolveSubmissionForTests(
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutManifestPrewarmInput& OutInput,
		FString& OutFailureReason);
#endif

	/** Stores one descriptor from an explicit produced artifact after successful preflight; absent/stale artifacts fail closed. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryStoreDescriptorFromSuccessfulPreflightArtifact(
		const FLayoutBackgroundSolveCompletion& PreflightCompletion,
		const FLayoutBackgroundAdmissibilityPreflightResult& PreflightResult,
		FLayoutFrozenSubmissionStore& Store,
		const FLayoutProducedFrozenDescriptorArtifact* ProducedArtifact,
		FLayoutFrozenSubmissionDescriptor& OutDescriptor,
		FString& OutFailureReason);

	/** Builds a produced descriptor artifact from an already-frozen seed without running the precompute adapter.
	 *  Call sites use this when the prewarm (Phase 4+) owns descriptor precomputation so the artifact
	 *  infrastructure remains viable for store/tombstone flows without redundant adapter work. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildProducedDescriptorArtifactFromSeed(
		FLayoutId ArtifactId,
		const FLayoutFrozenSubmissionDescriptorSeed& Seed,
		FLayoutProducedFrozenDescriptorArtifact& OutArtifact,
		FString& OutFailureReason);

	/** Builds a preflight descriptor seed from a snapshot without running the precompute adapter.
	 *  Call sites use this when the prewarm owns precomputation so the seed carries raw snapshot
	 *  identity for store/tombstone flows. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildPreflightDescriptorSeed(
		FLayoutId DescriptorId,
		uint64 RegionGroupId,
		uint64 Generation,
		int32 AttemptIndex,
		ELayoutFrozenSubmissionRegionKind RegionKind,
		const FLayoutPreSubmitFrozenSnapshot& Snapshot,
		FLayoutFrozenSubmissionDescriptorSeed& OutSeed,
		FString& OutFailureReason);
}
