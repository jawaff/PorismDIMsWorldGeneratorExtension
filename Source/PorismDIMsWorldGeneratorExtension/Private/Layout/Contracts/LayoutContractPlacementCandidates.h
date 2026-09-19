// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

/** Stable ordering key for contract candidates that must ignore pointers, map iteration, and asset load order. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractCandidateOrderingKey
{
	/** Manifest or contract id that scopes this candidate family. */
	FLayoutId ManifestId;

	/** Solve seed after final snap/shift selection. */
	int32 SolveSeed = 0;

	/** Local obligation being satisfied, such as feature, seam, ascent, or closure. */
	FLayoutId LocalObligationId;

	/** Stable candidate id selected by the producing artifact. */
	FLayoutId CandidateId;

	/** Stable placement shift id, or none for unshifted candidates. */
	FLayoutId PlacementShiftId;

	/** Deterministic attempt index inside the owning branch. */
	int32 AttemptIndex = 0;

	/** Stable child proof certificate id when this key orders certified child branches. */
	FLayoutId ChildCertificateId;

	/** Stable witness-content id when this key orders certified child branches. */
	FLayoutId ChildWitnessOrderingId;

	/** Deterministic child certificate input hash when this key orders certified child branches. */
	uint64 ChildCertificateInputHash = 0;

	/** Builds a deterministic id from value fields only. */
	FLayoutId BuildOrderingId() const;

	/** Returns true when this key sorts before another key in deterministic branch order. */
	bool LexicalLess(const FLayoutContractCandidateOrderingKey& Other) const;
};

/** Pointer-free selected child-branch ids used to construct deterministic parent-owned child ordering keys. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildBranchOrderingInput
{
	/** Stable parent contract or resume id that scopes this child branch. */
	FLayoutId ParentContractId;

	/** Source content-entry id selected by parent negotiation. */
	FName SourceEntryId;

	/** Stable child id selected by parent negotiation. */
	FLayoutId StableChildId;

	/** Child profile/snapshot id selected by parent negotiation. */
	FLayoutId ChildProfileId;

	/** Selected capability id relevant to this branch. */
	FLayoutId SelectedCapabilityId;

	/** Delegated feature requirement id relevant to this branch. */
	FLayoutId DelegatedFeatureId;

	/** Closure/seam/junction obligation id relevant to this branch. */
	FLayoutId ClosureSeamOrJunctionId;

	/** Pseudo-ascent strategy id relevant to this branch. */
	FLayoutId PseudoAscentStrategyId;

	/** Route or handoff anchor id relevant to this branch. */
	FLayoutId RouteOrHandoffAnchorId;

	/** Optional-drop or residual decision id relevant to this branch. */
	FLayoutId OptionalDropOrResidualId;

	/** Early structural priority selected by parent negotiation. */
	int32 EarlyStructuralPriority = 0;

	/** Stable shift/provenance id, or none for unshifted child branches. */
	FLayoutId PlacementShiftId;

	/** Deterministic attempt index inside the parent-owned child branch family. */
	int32 AttemptIndex = 0;

	/** Stable child proof certificate id when available. */
	FLayoutId ChildCertificateId;

	/** Stable witness-content id when available. */
	FLayoutId ChildWitnessOrderingId;

	/** Deterministic child certificate input hash when available. */
	uint64 ChildCertificateInputHash = 0;
};

/** Pointer-free composite scout-result ids used to construct deterministic composite placement ordering keys. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCompositeScoutOrderingInput
{
	/** Stable manifest or parent contract id that scopes this composite scout family. */
	FLayoutId ManifestId;

	/** Composite snapshot id selected by authored content. */
	FLayoutId CompositeSnapshotId;

	/** Stable hash/id for occupied-cell footprint shape. */
	FLayoutId OccupiedCellFootprintHash;

	/** Exposed face or capability id selected by the scout result. */
	FLayoutId ExposedFaceOrCapabilityId;

	/** Placement zone id selected by the scout result. */
	FLayoutId PlacementZoneId;

	/** Feature-provider id selected by the scout result. */
	FLayoutId FeatureProviderId;

	/** Stable shift/provenance id, or none for unshifted composite branches. */
	FLayoutId PlacementShiftId;

	/** Deterministic attempt index inside this composite scout family. */
	int32 AttemptIndex = 0;
};

/** Pointer-free single-level stepped ascent ids used to construct deterministic pseudo-ascent ordering keys. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSingleLevelSteppedAscentOrderingInput
{
	/** Stable manifest or parent contract id that scopes this ascent scout family. */
	FLayoutId ManifestId;

	/** Lower ascent capability id selected by parent/adapter proof. */
	FLayoutId LowerAscentId;

	/** Optional upper connector or cap capability id selected for pseudo upper proof. */
	FLayoutId UpperConnectorOrCapId;

	/** Authored composite bundle id when ascent is backed by a composite. */
	FLayoutId CompositeBundleId;

	/** Pseudo connector cell selected for route/handoff proof. */
	FIntVector PseudoConnectorCell = FIntVector::ZeroValue;

	/** Traversal channel id associated with this pseudo ascent. */
	FLayoutId TraversalChannelId;

	/** Terrain frontier id associated with this ascent requirement. */
	FLayoutId TerrainFrontierId;

	/** Stable shift/provenance id, or none for unshifted ascent branches. */
	FLayoutId PlacementShiftId;

	/** Deterministic attempt index inside this ascent scout family. */
	int32 AttemptIndex = 0;
};

/** Pointer-free continuation edge scout ids used to construct deterministic continuation ordering keys. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationEdgeScoutOrderingInput
{
	/** Stable manifest or accepted-region contract id that scopes this continuation edge family. */
	FLayoutId ManifestId;

	/** Stable accepted root/region id that owns the source edge. */
	FLayoutId SourceRegionId;

	/** Continuation family id selected by world-binding policy. */
	FName ContinuationFamilyId;

	/** Stable source endpoint id selected by the accepted region. */
	FLayoutId SourceEndpointId;

	/** Stable target endpoint or candidate id selected by continuation scout. */
	FLayoutId TargetEndpointOrCandidateId;

	/** Stable edge/scout-result id emitted by continuation scout. */
	FLayoutId EdgeScoutResultId;

	/** Path shape id such as surface, bridge, or tunnel shape emitted by continuation scout. */
	FLayoutId PathShapeId;

	/** Continuation mode id emitted by selected mode planning. */
	FLayoutId ContinuationModeId;

	/** Stable shift/provenance id, or none for unshifted continuation branches. */
	FLayoutId PlacementShiftId;

	/** Deterministic attempt index inside this continuation edge scout family. */
	int32 AttemptIndex = 0;
};

/** Pointer-free root scout result ids used to construct deterministic root ordering keys. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRootScoutResultOrderingInput
{
	/** Stable manifest or world-binding scope id for this root scout family. */
	FLayoutId ManifestId;

	/** Stable world-binding id selected by the scout result. */
	FName WorldBindingId;

	/** Stable root scout-result id emitted before request build. */
	FLayoutId ScoutResultId;

	/** Stable root candidate/site id emitted by world-binding planning data. */
	FLayoutId RootCandidateId;

	/** Stable placement policy id selected by the scout result. */
	FLayoutId PlacementPolicyId;

	/** Matching biome row selected by scout/runtime view. */
	FName BiomeRowName;

	/** Final snapped or shifted site center in block-world coordinates. */
	FIntVector SnappedSiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Solve seed after final snap/shift selection. */
	int32 SolveSeed = 0;

	/** Stable shift/provenance id, or none for unshifted root branches. */
	FLayoutId PlacementShiftId;

	/** Deterministic attempt index inside this root scout family. */
	int32 AttemptIndex = 0;
};

/** One coarse placement shift candidate used before expensive proof work. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCoarsePlacementShiftCandidate
{
	/** Coarse shift in layout cells, never a hidden one-cell scan by default. */
	FIntVector ShiftCells = FIntVector::ZeroValue;

	/** Stable shift id derived from ShiftCells. */
	FLayoutId ShiftId;

	/** Stable ordering key for this shift candidate. */
	FLayoutContractCandidateOrderingKey OrderingKey;
};

/** Candidate-ordering utilities for contract path slices. */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractPlacementCandidates
{
public:
	/** Sorts candidate keys by deterministic value fields only. */
	static void SortCandidateKeys(TArray<FLayoutContractCandidateOrderingKey>& InOutKeys);

	/** Builds a stable placement-shift id from value data only. */
	static FLayoutId BuildPlacementShiftId(const FIntVector& ShiftCells);

	/** Builds deterministic coarse placement shifts from footprint scale rather than one-cell scans. */
	static TArray<FLayoutCoarsePlacementShiftCandidate> BuildCoarsePlacementShiftCandidates(
		FLayoutId ManifestId,
		int32 SolveSeed,
		FLayoutId LocalObligationId,
		const FIntPoint& FootprintSizeInCells,
		int32 AttemptIndex,
		int32 MaxRingCount = 1);

	/** Builds coarse shifts scoped to one frozen terrain/biome artifact without reading live terrain. */
	static TArray<FLayoutCoarsePlacementShiftCandidate> BuildTerrainBackedCoarsePlacementShiftCandidates(
		FLayoutId ManifestId,
		int32 SolveSeed,
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainArtifact,
		const FIntVector& SharedCellSizeInBlocks,
		int32 AttemptIndex,
		int32 MaxRingCount = 1);

	/** Builds one deterministic ordering key for a certified child branch from parent-owned selected ids only. */
	static FLayoutContractCandidateOrderingKey BuildChildBranchOrderingKey(
		const FLayoutChildBranchOrderingInput& Input);

	/** Builds one deterministic ordering key for a composite scout result from pointer-free authored ids only. */
	static FLayoutContractCandidateOrderingKey BuildCompositeScoutOrderingKey(
		const FLayoutCompositeScoutOrderingInput& Input);

	/** Builds one deterministic ordering key for a single-level stepped pseudo-ascent branch. */
	static FLayoutContractCandidateOrderingKey BuildSingleLevelSteppedAscentOrderingKey(
		const FLayoutSingleLevelSteppedAscentOrderingInput& Input);

	/** Builds one deterministic ordering key for a continuation edge scout result from pointer-free ids only. */
	static FLayoutContractCandidateOrderingKey BuildContinuationEdgeScoutOrderingKey(
		const FLayoutContinuationEdgeScoutOrderingInput& Input);

	/** Builds one deterministic ordering key for a root scout result from pointer-free ids only. */
	static FLayoutContractCandidateOrderingKey BuildRootScoutResultOrderingKey(
		const FLayoutRootScoutResultOrderingInput& Input);

	/** Fail-closed variant for root scout ordering consumers that must reject missing stable scout identity before ordering. */
	static bool TryBuildRootScoutResultOrderingKey(
		const FLayoutRootScoutResultOrderingInput& Input,
		FLayoutContractCandidateOrderingKey& OutKey,
		FString& OutFailureReason);

	/** Recomputes the deterministic solve seed from the finalized snapped/shifted scout-result site. */
	static int32 BuildFinalizedScoutResultSolveSeed(
		const FIntVector& FinalizedSiteCenterBlockWorldPos,
		int32 WorldSeed);
};
