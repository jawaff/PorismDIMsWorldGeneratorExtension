// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Solver/LayoutProfileSolver.h"

/** Pointer-free static manifest cache key built only from authored snapshot, policy, and continuation identities. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifestCacheKey
{
	/** Stable deterministic id for this manifest-key value set. */
	FLayoutId KeyId;

	/** Effective request snapshot id, when one was already assigned by snapshot/finalizer code. */
	FLayoutId EffectiveSnapshotId;

	/** Profile snapshot id copied from the immutable solve request. */
	FLayoutId ProfileSnapshotId;

	/** Deterministic authored Entry/VerticalAccess count contract; asset names alone do not invalidate edited counts. */
	FLayoutId ProfileCountContractId;

	/** Pointer-free profile path copied from the immutable solve request. */
	FSoftObjectPath ProfilePath;

	/** Content-set snapshot id copied from the immutable solve request. */
	FLayoutId ContentSetSnapshotId;

	/** Module-set snapshot id copied from the immutable solve request. */
	FLayoutId ModuleCatalogId;

	/** Stable owning world-binding id copied from the immutable solve request. */
	FName WorldBindingId;

	/** Root solve/publication id already assigned by caller code. */
	FLayoutId RootSolveId;

	/** Root candidate id already assigned by world-binding, preview, or direct-root caller code. */
	FLayoutId RootCandidateId;

	/** Root placement-policy id already assigned by world-binding or direct-root request code. */
	FLayoutId RootPlacementPolicyId;

	/** Deterministic signature of placement-policy values when a named policy id is absent or insufficient for diagnostics. */
	FLayoutId PlacementPolicySignatureId;

	/** Placement kind selected before this manifest is consumed. */
	ELayoutWorldBindingPlacementKind RootPlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Continuation family selected before this manifest is consumed. */
	FName ContinuationFamilyId;

	/** Continuation placement kind selected before this manifest is consumed. */
	ELayoutWorldBindingPlacementKind ContinuationPlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Continuation entry level selected before this manifest is consumed. */
	int32 ContinuationResolvedEntryLevel = INDEX_NONE;

	/** Parent content-entry id that selected this child request, when this manifest represents child work. */
	FName SourceContentEntryId;

	/** Parent-delegated feature ids carried by the immutable request. */
	TArray<FLayoutId> DelegatedZoneFeatureRequirementIds;

	/** Exact precommitted direct-child provider ids carried by residual parent search. */
	TArray<FLayoutId> PrecommittedZoneFeatureProviderCommitmentIds;

	/** Parent-delegated closure ids carried by the immutable request. */
	TArray<FLayoutId> DelegatedClosureRequirementIds;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 1;

	/** Terrain-seam mode copied into the cache identity so enabled and disabled plans cannot share artifacts. */
	bool bEnableTerrainSeams = true;
};

/** Pointer-free frozen source artifact that can build a static manifest on worker-side cache miss only. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenRequestManifestArtifact
{
	/** Stable cache key resolved from frozen request identity fields. */
	FLayoutContractManifestCacheKey Key;

	/** Static manifest id resolved from effective or profile snapshot identity. */
	FLayoutId ManifestId;

	/** Owning world-binding id preserved for cache distinction and diagnostics. */
	FName WorldBindingId;

	/** Profile path copied from the frozen request boundary when present. */
	FSoftObjectPath ProfilePath;

	/** Pointer-free profile snapshot used to build static manifest summaries on cache miss. */
	FLayoutProfileSolveSnapshot ProfileSnapshot;

	/** Pointer-free content-set snapshot used to build static manifest summaries on cache miss. */
	FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;

	/** Pointer-free module-set snapshot used to build static manifest summaries on cache miss. */
	FLayoutModuleCatalog ModuleCatalog;

	/** Request-level validation assertions copied before cache resolution. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** One immutable manifest cached for workers after the key is resolved. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifestCacheEntry
{
	/** Stable cache key that produced this entry. */
	FLayoutContractManifestCacheKey Key;

	/** Static manifest payload reused across candidate locations. */
	FLayoutContractManifest Manifest;
};

/** Small private in-memory manifest cache used by async/contract slices until planning-store persistence lands. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifestCache
{
public:
	/** Builds a deterministic cache key from immutable request identity and policy fields only. */
	static FLayoutContractManifestCacheKey BuildKeyFromSolveRequest(const FLayoutRegionSolveRequest& SolveRequest);

	/** Builds the deterministic id for a cache key from value data only. */
	static FLayoutId BuildKeyId(const FLayoutContractManifestCacheKey& Key);

	/** Builds a pointer-free frozen source artifact from one already-finalized worker-safe request. */
	static FLayoutFrozenRequestManifestArtifact BuildFrozenRequestManifestArtifact(const FLayoutRegionSolveRequest& SolveRequest);

	/** Validates that a frozen source artifact can feed manifest cache resolution without live assets or missing stable ids. */
	static bool ValidateFrozenRequestManifestArtifact(const FLayoutFrozenRequestManifestArtifact& Artifact, FString& OutFailureReason);

	/** Finds or builds one cache entry from a frozen artifact without consulting world location, debug path, seed, or live assets. */
	bool FindOrAddManifest(
		const FLayoutFrozenRequestManifestArtifact& Artifact,
		FLayoutContractManifestCacheEntry& OutEntry,
		bool& bOutCacheHit);

	/** Finds or builds one cache entry without consulting world location, debug path, seed, or live assets. */
	bool FindOrAddManifest(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutContractManifestCacheEntry& OutEntry,
		bool& bOutCacheHit);

	/** Removes all cached entries. */
	void Reset();

	/** Returns current cached-entry count for diagnostics and tests. */
	int32 Num() const;

private:
	TMap<FLayoutId, FLayoutContractManifestCacheEntry> EntriesByKeyId;
};
