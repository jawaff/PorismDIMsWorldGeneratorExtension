// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "HAL/ThreadSafeBool.h"
#include "Layout/Contracts/LayoutContractTypes.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Types/LayoutTypes.h"

class UChunkStructureTemplate;
class ULayoutCompositeModuleAsset;
class ULayoutModuleAsset;
class UObject;
class ULayoutProfileAsset;
class ULayoutRegionContentSetAsset;
struct FLayoutChildRequestTemplateSnapshot;

/** Structured stepped-plan rejection categories consumed by prewarm fallback policy. */
enum class ELayoutSteppedTerrainFinalizationFailureKind : uint8
{
	None,
	ReservedOpenSteppedTopologyInfeasible,
	EntryTerrainQualificationInfeasible,
	StageTopologyInfeasible
};

/** One occupied local cell plus the exposed face rules regenerated for that cell. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutLocalCellFaceRuleSnapshot
{
	/** Occupied local cell inside the module bounds. */
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Pointer-free leaf template identity required to realize this occupied local cell. */
	FSoftObjectPath TemplatePath;

	/** Leaf yaw relative to the composite root yaw in clockwise quarter-turn steps. */
	int32 RelativeYawRotationSteps = 0;

	/** Roles this exact occupied local cell contributes to composite admission. */
	TArray<ELayoutModuleRole> Roles;

	/** Planned intents this exact occupied local cell may satisfy. */
	TArray<ELayoutCellIntent> SupportedCellIntents;

	/** Exposed face rules on this occupied local cell after bounds and footprint compilation. */
	TArray<FLayoutFaceRule> ExposedFaceRules;
};

/** One derived internal traversal bridge between occupied local cells in a canonical module shape. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDerivedInternalTraversalLink
{
	/** Owned derived-link identity used by diagnostics and fixture replay. */
	FLayoutId LinkId;

	/** Source occupied local cell. */
	FIntVector FromLocalCell = FIntVector::ZeroValue;

	/** Source traversal channel carried by this link. */
	FGameplayTag FromTraversalChannel;

	/** Destination occupied local cell. */
	FIntVector ToLocalCell = FIntVector::ZeroValue;

	/** Destination traversal channel carried by this link. */
	FGameplayTag ToTraversalChannel;

	/** If true, traversal may flow in both directions across this local-cell bridge. */
	bool bBidirectional = true;
};

/** Immutable solver-facing copy of one module asset's authored contract. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutModuleSolveSnapshot
{
	/** Stable snapshot identifier used by fixtures and diagnostics. */
	FLayoutId SnapshotId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 2;

	/** Source asset retained only for diagnostics and final placement identity during the transition to snapshot-only solving. */
	TObjectPtr<ULayoutModuleAsset> SourceModule = nullptr;

	/** Optional composite source retained while multi-cell composites migrate onto the snapshot and bundle path. */
	TObjectPtr<ULayoutCompositeModuleAsset> SourceCompositeModule = nullptr;

	/** Stable human-readable module name used for deterministic diagnostics. */
	FName DebugName;

	/** Template stamped when this module is realized. */
	TSoftObjectPtr<UChunkStructureTemplate> Template;

	/** Fixed layout cell size in block units. */
	FIntVector CellSizeInBlocks = FIntVector::ZeroValue;

	/** Rectangular local-cell bounds occupied by this module before occupied-mask cutouts. */
	FIntVector BoundsCells = FIntVector(1, 1, 1);

	/** Realized template dimensions in blocks for this module payload. */
	FIntVector TemplateDimensionsBlocks = FIntVector::ZeroValue;

	/** Occupied local cells inside BoundsCells after applying the module footprint mask. */
	TArray<FIntVector> OccupiedLocalCells;

	/** Exposed face rules regenerated per occupied local cell from the canonical module shape. */
	TArray<FLayoutLocalCellFaceRuleSnapshot> GeneratedLocalCellFaceRules;

	/** Fixed solver roles copied from authored module data. */
	TArray<ELayoutModuleRole> Roles;

	/** Broad planned cell intents this module may fill, derived from Roles. */
	TArray<ELayoutCellIntent> SupportedCellIntents;

	/** Planned cell intents legal for the local root cell when this snapshot is used as one anchored solver candidate. */
	TArray<ELayoutCellIntent> RootSupportedCellIntents;

	/** Normalized yaw rotations available to the solver. */
	TArray<int32> AllowedYawRotationSteps;

	/** Effective six-face rules after authored symmetry has been expanded. */
	FLayoutModuleFaceRules EffectiveFaceRules;

	/** Traversal channels exposed by the module. */
	FGameplayTagContainer TraversalChannels;

	/** Internal traversal links between the module's traversal channels. */
	TArray<FLayoutInternalAccessLink> InternalAccessLinks;

	/** Derived traversal bridges that connect occupied local cells inside one canonical module shape. */
	TArray<FLayoutDerivedInternalTraversalLink> DerivedInternalTraversalLinks;

	/** Minimum compatible traversable filled neighbors required by this module. */
	int32 MinTraversableNeighborFaces = 0;

	/** Deterministic selection weight used when ordering candidates. */
	int32 Weight = 1;

	/** Unified content-entry id that supplied this module snapshot when compiled from a content set. */
	FName SourceContentEntryId;

	/** Exact zone features copied from the source entry for every leaf, composite, and vertical-region solve path. */
	FGameplayTagContainer ProvidedZoneFeatures;

	/** Parent-relative placement zone authored on the module content entry that supplied this snapshot. */
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

	/** Parent-relative level placement policy authored on the module content entry that supplied this snapshot. */
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific level index used when LevelPlacementPolicy is SpecificLevel. */
	int32 SpecificLevel = 0;

	/** If true, the source module content entry was marked optional. */
	bool bOptional = false;

	/** Optional closure-provider intents copied from the owning content entry. */
	TArray<FLayoutClosureProviderIntent> ClosureProviderIntents;

	/** Optional seam-provider intents copied from the owning content entry. */
	TArray<FLayoutSeamProviderIntent> SeamProviderIntents;

	/** Validation messages captured when the snapshot was built. */
	FLayoutValidationResult Validation;

	/** Derived endpoint offers proven from the canonical module contract. */
	TArray<FLayoutDerivedEndpointOffer> DerivedEndpointOffers;

	/** Derived boundary span offers proven from the canonical module contract. */
	TArray<FLayoutDerivedSpanOffer> DerivedSpanOffers;

	/** Derived vertical-access contracts proven from the canonical module contract. */
	TArray<FLayoutDerivedVerticalAccessContract> DerivedVerticalAccessContracts;

	/** Proof-bearing records captured while building the snapshot. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results captured while building the snapshot. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;

	/** Returns true when the copied intent list contains the supplied intent. */
	bool SupportsIntent(ELayoutCellIntent Intent) const;

	/** Returns true when the local root cell may anchor on the supplied planned intent. */
	bool SupportsRootIntent(ELayoutCellIntent Intent) const;

	/** Returns true when the copied traversal-channel list exposes the supplied tag. */
	bool ExposesTraversalChannel(const FGameplayTag& TraversalChannel) const;
};

/** Immutable solver-facing copy of one unified module-or-child content entry. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionContentEntrySolveSnapshot
{
	/** Stable content-entry identifier used by diagnostics and fixtures. */
	FName EntryId;

	/** Whether this entry references a module or a child region profile. */
	ELayoutRegionContentKind ContentKind = ELayoutRegionContentKind::Module;

	/** Deterministic selection weight used when ordering candidates. */
	int32 Weight = 1;

	/** Feature tags this content entry provides for counted zone feature requirements. */
	FGameplayTagContainer ProvidedZoneFeatures;

	/** Optional closure-provider intents authored on this content entry. */
	TArray<FLayoutClosureProviderIntent> ClosureProviderIntents;

	/** Optional seam-provider intents authored on this content entry. */
	TArray<FLayoutSeamProviderIntent> SeamProviderIntents;

	/** Index of the compiled module snapshot when this is a module entry. */
	int32 ModuleSnapshotIndex = INDEX_NONE;

	/** Parent-relative placement zone authored for this module entry. */
	ELayoutPlacementZone ModulePlacementZone = ELayoutPlacementZone::Any;

	/** Parent-relative level placement policy authored for this module entry. */
	ELayoutLevelPlacementPolicy ModuleLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific level index used when ModuleLevelPlacementPolicy is SpecificLevel. */
	int32 ModuleSpecificLevel = 0;

	/** If true, this module entry was marked optional in the authored content set. */
	bool bModuleOptional = false;

	/** Snapshot id of the child region profile when this is a child entry. */
	FLayoutId ChildProfileSnapshotId;

	/** Stable soft path of the child region profile when this is a child entry. */
	FSoftObjectPath ChildProfilePath;

	/** Snapshot id of the child region content set when a compiled child request template exists. */
	FLayoutId ChildContentSetSnapshotId;

	/** Parent-relative placement zone authored for this child entry. */
	ELayoutPlacementZone ChildPlacementZone = ELayoutPlacementZone::Any;

	/** Parent-relative level placement policy authored for this child entry. */
	ELayoutLevelPlacementPolicy ChildLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific level index used when ChildLevelPlacementPolicy is SpecificLevel. */
	int32 ChildSpecificLevel = 0;

	/** If true, this child entry may be dropped later when structural solving cannot place it compatibly. */
	bool bChildOptional = false;

	/** If true, this child entry's realized vertical access may satisfy the host region's ascent requirement. */
	bool bChildContributesHostVerticalAccess = false;

	/** Compiled snapshot-only child request template used by recursive scheduling without live asset expansion. */
	TSharedPtr<FLayoutChildRequestTemplateSnapshot> CompiledChildRequestTemplate;
};

/** Immutable solver-facing copy of the long-term unified content-set contract. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionContentSetSolveSnapshot
{
	/** Stable snapshot identifier used by fixtures and diagnostics. */
	FLayoutId SnapshotId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 2;

	/** Source asset retained while the runtime transitions away from module-set-only authoring. */
	TObjectPtr<const ULayoutRegionContentSetAsset> SourceContentSet = nullptr;

	/** Stable human-readable content-set name used for diagnostics. */
	FName DebugName;

	/** Shared fixed cell size copied from module entries when available. */
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Unified weighted content entries after defaults and adapters. */
	TArray<FLayoutRegionContentEntrySolveSnapshot> Entries;

	/** Validation messages captured when the snapshot was built. */
	FLayoutValidationResult Validation;

	/** Proof-bearing records captured while building the snapshot. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results captured while building the snapshot. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** Immutable solver-facing copy of a module set and all referenced module contracts. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutModuleCatalog
{
	/** Stable snapshot identifier used by fixtures and diagnostics. */
	FLayoutId SnapshotId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 2;

	/** Stable human-readable module-set name used for diagnostics. */
	FName DebugName;

	/** Shared fixed cell size copied from the module set. */
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Module contracts copied in authored module-set order. */
	TArray<FLayoutModuleSolveSnapshot> Modules;

	/** Validation messages captured when the snapshot was built. */
	FLayoutValidationResult Validation;

	/** Proof-bearing records captured while building the snapshot. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results captured while building the snapshot. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** Worker-only kind tag flattened from one reflected sparse-placement rule variant. */
enum class ELayoutSparsePlacementRuleKind : uint8
{
	PreserveTerrain,
	Exact,
	Range,
	FillAvailable
};

/** Immutable solver-facing copy of one typed terrain-preservation or sparse-placement rule. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSparsePlacementRuleSolveSnapshot
{
	/** Stable sparse-rule identifier used by diagnostics, fixtures, and deterministic placement ordering. */
	FName RuleId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 4;

	/** Concrete reflected rule variant flattened for worker-safe dispatch. */
	ELayoutSparsePlacementRuleKind RuleKind = ELayoutSparsePlacementRuleKind::PreserveTerrain;

	/** Selects whether a placement variant preserves terrain or consumes residuals produced elsewhere. */
	ELayoutSparseCandidateSource CandidateSource = ELayoutSparseCandidateSource::PreserveSupportedTerrain;

	/** Content set copied for this sparse pass. */
	FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;

	/** Module-backed adapter snapshot consumed by the current sparse placement runtime. */
	FLayoutModuleCatalog ModuleCatalog;

	/** Exact sparse placement count when Count Mode is Exact. */
	int32 Count = 0;

	/** Minimum sparse placement count when Count Mode is Range. */
	int32 MinCount = 0;

	/** Maximum sparse placement count when Count Mode is Range. */
	int32 MaxCount = 0;

	/** Parent-relative finalized zone matched by this rule. */
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Interior;

	/** Authored module-level scope matched after Flat or Stepped terrain finalization. */
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Authored level used when LevelPlacementPolicy is SpecificLevel. */
	int32 SpecificLevel = 0;

	/** Minimum Manhattan spacing in cells between accepted placements from this rule. */
	int32 MinSpacingCells = 0;

	/** Validation messages captured when the sparse rule snapshot was built. */
	FLayoutValidationResult Validation;

	/** Proof-bearing records captured while building the sparse rule snapshot. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results captured while building the sparse rule snapshot. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** Immutable solver-facing copy of one profile's authored placement contract. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProfileSolveSnapshot
{
	/** Stable snapshot identifier used by fixtures and diagnostics. */
	FLayoutId SnapshotId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 4;

	/** Source asset retained for request adapters while snapshot-only solving is completed. */
	TObjectPtr<const ULayoutProfileAsset> SourceProfile = nullptr;

	/** Stable soft path of the source profile for pointer-free replay and child certification. */
	FSoftObjectPath SourceProfilePath;

	/** Stable human-readable profile name used for diagnostics. */
	FName DebugName;

	/** Minimum footprint size in cells. */
	FIntPoint MinimumFootprintInCells = FIntPoint(1, 1);

	/** Maximum footprint size in cells. */
	FIntPoint MaximumFootprintInCells = FIntPoint(1, 1);

	/** Number of vertical cell layers to solve. */
	int32 LevelCount = 1;

	/** Entry reservation count mode. */
	ELayoutCountConstraintMode EntryCountMode = ELayoutCountConstraintMode::None;

	/** Exact entry reservation count. */
	int32 EntryCount = 0;

	/** Minimum entry count for range mode. */
	int32 MinEntryCount = 0;

	/** Maximum entry count for range mode. */
	int32 MaxEntryCount = 0;

	/** Vertical-access reservation count mode. */
	ELayoutCountConstraintMode VerticalAccessCountMode = ELayoutCountConstraintMode::None;

	/** Exact vertical-access reservation count. */
	int32 VerticalAccessCount = 0;

	/** Minimum vertical-access count for range mode. */
	int32 MinVerticalAccessCount = 0;

	/** Maximum vertical-access count for range mode. */
	int32 MaxVerticalAccessCount = 0;

	/** If true, vertical-access role modules are restricted to planned vertical-access cells. */
	bool bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	/** Explicit recursive closure requirements copied from the profile. */
	TArray<FLayoutClosureRequirement> ClosureRequirements;

	/** Counted feature requirements copied from the profile. */
	TArray<FLayoutZoneFeatureRequirement> ZoneFeatureRequirements;

	/** Level-scoped fill behavior copied from the profile. */
	TArray<FLayoutLevelFillRule> LevelFillRules;

	/** Hard-open-space rules copied from the profile. */
	TArray<FLayoutReservedOpenSpaceRule> ReservedOpenSpaceRules;

	/** If true, all authored traversal channels must be reachable from an entry root. */
	bool bRequireAllTraversalChannelsReachable = false;

	/** If true, frozen request adapters may author stepped terrain support for this profile. */
	bool bSupportsSteppedTerrainSolve = false;

	/** If true, stepped adapters author terrain-seam retaining and gate contracts. */
	bool bEnableTerrainSeams = true;

	/** If true, this profile is designed for underground placement in cavities below the biome surface. */
	bool bUndergroundPlacement = false;

	/** Sparse post-structural placement rules copied from the profile. */
	TArray<FLayoutSparsePlacementRuleSolveSnapshot> SparsePlacementRules;

	/** Validation messages captured when the snapshot was built. */
	FLayoutValidationResult Validation;

	/** Proof-bearing records captured while building the snapshot. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results captured while building the snapshot. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** Immutable snapshot-only child request template compiled from one child profile and its content graph. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildRequestTemplateSnapshot
{
	/** Stable effective-snapshot id used for replay compatibility and diagnostics. */
	FLayoutId EffectiveSnapshotId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 2;

	/** Deterministic base seed used when the parent derives concrete child solve seeds. */
	int32 Seed = 0;

	/** Stable soft path of the compiled child profile for pointer-free worker certification. */
	FSoftObjectPath ChildProfilePath;

	/** Immutable module-set contract consumed by the child region job. */
	FLayoutModuleCatalog ModuleCatalog;

	/** Immutable unified content-set contract consumed by the child region job. */
	FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;

	/** Immutable profile contract consumed by the child region job. */
	FLayoutProfileSolveSnapshot ProfileSnapshot;

	/** Proof-bearing records copied onto the template for fixture replay and diagnostics. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results copied onto the template for fixture replay and diagnostics. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** One generic boundary point available to seed or export a region solve. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolveBoundaryPoint
{
	/** Region-local cell touched by this boundary point. */
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Face direction on the local cell that owns the boundary relationship. */
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Single connection tag offered at this boundary. */
	FGameplayTag ConnectionTag;

	/** Opposite-side connection tags accepted by this boundary. */
	FGameplayTagContainer AllowedConnectionTags;

	/** Traversal channels exposed by this boundary point. */
	FGameplayTagContainer ConnectedTraversalChannels;

	/** True when the boundary represents a filled solved neighbor instead of empty exterior space. */
	bool bRepresentsFilledNeighbor = true;

	/**
	 * True when the receiving face must itself be authored as a region-boundary-facing face.
	 * Exported region-to-region boundary points clear this so adjacent regions can consume the
	 * interface on ordinary inward-facing faces instead of only outer perimeter faces.
	 */
	bool bRequiresBoundaryFacing = true;

	/** True when this boundary requires matching yaw from a filled neighbor. */
	bool bRequireMatchingYawWithFilledNeighbor = false;

	/** Source region debug path for diagnostics. */
	FString SourceRegionDebugPath;

	/** Source placement cell when this boundary was exported from a solved module. */
	FIntVector SourceCell = FIntVector::ZeroValue;

	/** Source placement yaw when this boundary was exported from a solved module. */
	int32 SourceYawRotationSteps = 0;

	/** Optional committed-anchor id that caused this boundary point to be forwarded. */
	FLayoutId CommitmentId;

	/** Candidate-domain certificate that already proved reciprocal module/yaw compatibility for this face. */
	FLayoutId CertifiedDomainCertificateId;

	/** Child cell restriction whose exact variants were certified against the reciprocal parent domain. */
	FLayoutId CertifiedDomainRestrictionId;

	/** True when this boundary consumes the frozen reciprocal candidate-domain certificate instead of one completed source placement. */
	bool bUsesCertifiedReciprocalDomain = false;

	/** True when current staged-terrain ownership says this boundary is owned by reachable plateau cells. */
	bool bTerrainStageReachablePlateauOwned = true;

	/** True when this boundary is preserved only for later future-terrace proof translation. */
	bool bTerrainStageFutureTerraceProofOnly = false;

	/** Optional staged ascent-frontier id associated with this boundary point when future-terrace proof owns it. */
	int32 TerrainAscentFrontierId = INDEX_NONE;
};

/** Child capability envelope proven from one child region's top-level compiled snapshot. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildCapabilityEnvelope
{
	/** Region debug path that owns this capability envelope. */
	FString RegionDebugPath;

	/** Stable snapshot id used to prove this capability envelope. */
	FLayoutId SnapshotId;

	/** Parent-facing endpoint capabilities proven without consulting grandchildren. */
	TArray<FLayoutChildCapabilityEndpoint> EndpointCapabilities;

	/** Parent-facing boundary span capabilities proven without consulting grandchildren. */
	TArray<FLayoutChildCapabilitySpan> SpanCapabilities;

	/** Parent-facing seam capabilities proven without consulting grandchildren. */
	TArray<FLayoutChildCapabilitySeam> SeamCapabilities;

	/** Proof-bearing records captured while compiling this capability envelope. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results captured while compiling this capability envelope. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** Parent-side traversal anchor that must remain connected in the solved route graph. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCommittedTraversalAnchor
{
	/** Planned parent-local cell that must stay reachable. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Traversal channel that should be used when routing to this anchor. */
	FGameplayTag TraversalChannel;
};

/** Negotiated owner for a parent/child vertical-ascent obligation after child placement settles. */
enum class ELayoutNegotiatedHostVerticalAccessResponsibility : uint8
{
	ParentOwned,
	ChildOwned,
	Composed
};

/** One deterministic set of parent-local cells grouped under a covered level of a negotiated contract. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutNegotiatedLevelCellSet
{
	/** Covered parent-local level in the negotiated contract. */
	int32 Level = 0;

	/** Parent-local cells owned by this negotiated set on the covered level. */
	TArray<FIntVector> Cells;
};

/** One deterministic parent/child interface bundle grouped by covered level. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutNegotiatedLevelInterfaceContract
{
	/** Covered level where these anchors remain authoritative after negotiation. */
	int32 Level = 0;

	/** Hard child endpoint anchors the negotiated child must honor on this level. */
	TArray<FLayoutCommittedEndpointAnchor> EndpointAnchors;

	/** Parent-local traversal anchors that must stay reachable through the negotiated interface on this level. */
	TArray<FLayoutCommittedTraversalAnchor> TraversalAnchors;
};

/** One deterministic sibling-seam bundle grouped by covered level for negotiated child placements. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutNegotiatedLevelSeamSet
{
	/** Covered level where these sibling-facing seams remain authoritative after negotiation. */
	int32 Level = 0;

	/** Level-specific sibling seams already fixed by the parent-owned negotiation outcome. */
	TArray<FLayoutPartitionSeamRecord> Seams;
};

/** Explicit host vertical-access ownership/support outcome keyed by one staged ascent-frontier id. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutNegotiatedHostVerticalAccessFrontierResponsibility
{
	/** Deterministic staged ascent-frontier id, or INDEX_NONE when the ownership outcome is not tied to one frontier. */
	int32 TerrainAscentFrontierId = INDEX_NONE;

	/** Final negotiated owner of the host vertical-ascent obligation for this frontier. */
	ELayoutNegotiatedHostVerticalAccessResponsibility HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;

	/** Exact parent-owned vertical-access provider groups that negotiation counted for this frontier. */
	int32 CountedParentProviderCount = 0;

	/** Exact parent-local representative cells for the retained counted parent provider groups on this frontier. */
	TArray<FIntVector> CountedParentVerticalAccessCells;

	/** Parent-local vertical-access cells kept only as retained route-support or child handoff state on this frontier. */
	TArray<FIntVector> RetainedParentRouteSupportVerticalAccessCells;

	/** Child region paths that negotiation counted toward the host vertical-access requirement on this frontier. */
	TArray<FString> CountedChildProviderRegionDebugPaths;
};

/** Snapshot-backed negotiated responsibility contract fixed before the residual parent and child proof solves begin. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutNegotiatedChildResponsibilityContract
{
	/** Parent region that negotiated and owns this contract. */
	FString ParentRegionDebugPath;

	/** Child region that must satisfy this contract. */
	FString ChildRegionDebugPath;

	/** Parent-local replacement volume removed from the parent solve on each covered level. */
	TArray<FLayoutNegotiatedLevelCellSet> ReplacementVolumeByLevel;

	/** Parent-local shell cells that remain parent-owned inside or beside the replacement volume on each covered level. */
	TArray<FLayoutNegotiatedLevelCellSet> RetainedParentShellCellsByLevel;

	/** Parent-local shell cells kept only as proof metadata for shared-host ascent reasoning, not parent reservation subtraction. */
	TArray<FLayoutNegotiatedLevelCellSet> ProofOnlyHostAscentParentShellCellsByLevel;

	/** Committed parent/child interface anchors grouped by covered level. */
	TArray<FLayoutNegotiatedLevelInterfaceContract> CommittedParentChildInterfacesByLevel;

	/** Committed sibling-facing seam records grouped by covered level. */
	TArray<FLayoutNegotiatedLevelSeamSet> CommittedSiblingInterfacesByLevel;

	/** Final negotiated owner of the host vertical-ascent obligation for this child placement. */
	ELayoutNegotiatedHostVerticalAccessResponsibility HostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned;

	/** Authored host-provider count that negotiation had to satisfy after replacement subtraction. */
	int32 RequiredHostProviderCount = 0;

	/** True when the authored/root host-provider count must match exactly instead of acting as a lower bound. */
	bool bRequiresExactHostProviderCount = false;

	/** Exact parent-owned vertical-access provider groups that negotiation counted after replacement subtraction. */
	int32 CountedParentProviderCount = 0;

	/** Exact parent-local representative cells for the retained counted parent provider groups. */
	TArray<FIntVector> CountedParentVerticalAccessCells;

	/** Parent-local vertical-access cells kept only as retained route-support or child handoff state, not counted provider groups. */
	TArray<FIntVector> RetainedParentRouteSupportVerticalAccessCells;

	/** Child region paths that negotiation counted toward the host vertical-access requirement. */
	TArray<FString> CountedChildProviderRegionDebugPaths;

	/** Explicit frontier-keyed host vertical-access ownership/support records kept stable for staged terrain replay and diagnostics. */
	TArray<FLayoutNegotiatedHostVerticalAccessFrontierResponsibility> HostVerticalAccessFrontierResponsibilities;

	/** True when negotiation fixed a concrete lower host-facing ingress anchor for child-owned or composed ascent. */
	bool bHasRequiredHostIngressAnchor = false;

	/** Required lower host-facing ingress anchor when negotiation fixed one. */
	FLayoutCommittedEndpointAnchor RequiredHostIngressAnchor;

	/** True when negotiation fixed a concrete upper host-facing egress anchor for child-owned or composed ascent. */
	bool bHasRequiredHostEgressAnchor = false;

	/** Required upper host-facing egress anchor when negotiation fixed one. */
	FLayoutCommittedEndpointAnchor RequiredHostEgressAnchor;

	/** Stable child capability-summary anchor-pair proof selected for child-owned or composed host ascent. */
	FLayoutId RequiredChildGenerallyConnectableAnchorPairId;

	/** Child-local levels covered by the selected generally-connectable internal ascent proof. */
	TArray<int32> RequiredChildInternalVerticalSpanLevels;

	/** Child-local cells that bound the internal negotiated vertical route when the child owns or composes host ascent. */
	TArray<FIntVector> RequiredChildInternalVerticalRouteCells;
};

/** Derived child placement class carried before stage-aware mapping activates in the shared coordinator. */
enum class ELayoutChildPlacementStageClass : uint8
{
	StageNeutral,
	StageRigid,
	InheritedStage,
	CrossStage,
	/** Independently supported flat ground plane; parent terrain remains stepped. */
	LocalFlat
};

/** One child cell mapped from authored child topology into finalized parent physical topology. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildStageMappedCell
{
	/** Authored child-local cell before mapping. Generated cells use a unique negative-Z
	 * correspondence key, not an authored coordinate; never use that key for level admission. */
	FIntVector SourceChildCell = FIntVector::ZeroValue;

	/** Child-local physical cell consumed by the delegated regional proof. */
	FIntVector MappedChildCell = FIntVector::ZeroValue;

	/** Exact parent-local physical cell reserved and merged by coordination. */
	FIntVector ParentCell = FIntVector::ZeroValue;

	/** Authored child level retained independently from physical stage Z. */
	int32 ModuleLevelIndex = 0;

	/** Frozen parent terrain stage selected for this child column, or INDEX_NONE for stage-neutral parents. */
	int32 TerrainStageIndex = INDEX_NONE;

	/** Support/deck added by this child's topology preparation rather than its authored grid. */
	bool bGeneratedByChildTopology = false;
};

/** Pointer-free atomic mapping authority shared by placement, proof handoff, merge, artifact, and publication. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildStageMappingResult
{
	/** Effective mapping class derived from finalized geometry and committed route obligations. */
	ELayoutChildPlacementStageClass StageClass = ELayoutChildPlacementStageClass::StageNeutral;

	/** Stable identity of this exact source/yaw/transform/mapped-cell value. */
	FLayoutId MappingId;

	/** Parent-owned authored-level/XY transform used to derive this mapping. */
	FIntVector ParentRegionCellOffset = FIntVector::ZeroValue;

	/** Exact source-to-child-to-parent cell correspondence. */
	TArray<FLayoutChildStageMappedCell> Cells;

	/** Child-local planned topology consumed by child proof. */
	TArray<FLayoutPlannedCell> ChildLocalPlannedCells;

	/** Parent-local planned topology consumed by reservation, seam, and merge logic. */
	TArray<FLayoutPlannedCell> ParentTranslatedPlannedCells;

	/** Parent terrain authority clipped and rebased into child-local coordinates. */
	FLayoutFrozenTerrainContract ChildLocalTerrainContract;

	/** Parent stage authority clipped and rebased into child-local XY. */
	TArray<FLayoutFrozenTerrainStageCellRecord> ChildLocalStageMap;

	/** Frozen support samples and adjacency clipped into child-local coordinates. */
	FLayoutSteppedTerrainSupportMap ChildLocalSteppedTerrainSupportMap;

	/** Frozen VerticalAccess host groups whose mapped cells remain inside this child value. */
	TArray<FLayoutVerticalAccessHostGroup> ChildLocalVerticalAccessHostGroups;

	/** Stable mapped-frontier ids available for exact route-crossing promotion. */
	TArray<FLayoutId> CrossedFrontierIds;

	/** Returns true when the result owns a complete deterministic mapping. */
	bool IsValid() const
	{
		return !MappingId.IsNone()
			&& !Cells.IsEmpty()
			&& Cells.Num() == ChildLocalPlannedCells.Num()
			&& Cells.Num() == ParentTranslatedPlannedCells.Num();
	}
};

/** Direct parent-to-child commitment recorded before the child solve begins. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutDirectChildRegionCommitment
{
	/** Region debug path that owns the parent side of this commitment. */
	FString ParentRegionDebugPath;

	/** Region debug path that must consume the committed child anchors. */
	FString ChildRegionDebugPath;

	/** Exact stage/transform authority selected by parent coordination. */
	FLayoutChildStageMappingResult StageMapping;

	/** Stable reciprocal boundary-domain witnesses selected before parent and child proof. */
	TArray<FLayoutId> BoundaryWitnessIds;

	/** Hard child anchors selected by the parent before the child solve runs. */
	TArray<FLayoutCommittedEndpointAnchor> EndpointCommitments;

	/** Parent-only ingress anchors that must stay reachable before any child traversal bridge is used. */
	TArray<FLayoutCommittedTraversalAnchor> ParentTraversalIngressCommitments;

	/** True when merged traversal may bridge through the child after the parent ingress subset is satisfied. */
	bool bAllowsChildTraversalBridgeForCommittedContacts = false;

	/** Exact parent-owned hard zone-feature commitments supplied once by this direct child instance. */
	TArray<FLayoutZoneFeatureProviderCommitment> ZoneFeatureProviderCommitments;

	/** Immutable negotiated contract fixed alongside these child commitments. */
	FLayoutNegotiatedChildResponsibilityContract NegotiatedResponsibilityContract;

	/** Frozen parent-contact boundary points resolved from the parent solve result during deferred
	 *  validation. Each entry carries the parent boundary placement's effective face-rule data
	 *  (ConnectionTag, AllowedConnectionTags, occupancy, yaw-match). The child's independent
	 *  proof consumes only this frozen array and never queries the live ParentProofResult.
	 *  Parent->child only; no sibling-chain contacts exist in current target fixtures. */
	TArray<FLayoutSolveBoundaryPoint> FrozenBoundaryPoints;
};

/** Execution-only solve budgets and diagnostics carried by one solve request. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutSolverExecutionSettings
{
	/** Maximum recursive candidate attempts before the solve aborts. */
	int32 MaxCandidateAttempts = 50000;

	/** Maximum wall-clock time in seconds before the solve aborts. Zero disables the time budget. */
	float MaxSolveDurationSeconds = 5.0f;

	/** Maximum independent child proof jobs the recursive scheduler may stage concurrently once async proof dispatch is enabled. */
	int32 MaxConcurrentChildProofJobs = 1;

	/** Maximum candidate rejection details retained in one failure result. */
	int32 MaxFailureDetails = 24;

	/** Optional bounded trace recording mode for solve diagnostics. */
	ELayoutSolverTraceMode TraceMode = ELayoutSolverTraceMode::Disabled;

	/** Maximum trace events retained before older events are overwritten. */
	int32 MaxTraceEvents = 512;

	/** If true, trace output includes candidate detail strings. */
	bool bIncludeTraceCandidateDetails = false;
};

/** Stable module/yaw candidate identity used by a frozen cell-domain restriction. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCandidateVariantIdentity
{
	/** Immutable module snapshot selected into this allowed domain. */
	FLayoutId ModuleSnapshotId;

	/** Normalized module yaw selected into this allowed domain. */
	int32 YawRotationSteps = 0;
};

/** Pointer-free allowed candidate domain frozen for one planned cell before regional proof. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutCellCandidateDomainRestriction
{
	/** Stable restriction id used by certificates, memo identity, and diagnostics. */
	FLayoutId RestrictionId;

	/** Region-local planned cell whose module domain is restricted. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Exact module/yaw candidates allowed at this cell. */
	TArray<FLayoutCandidateVariantIdentity> AllowedCandidates;

	/** Authored/child domain before temporary VA narrowing; restored when selecting another witness. */
	TOptional<TArray<FLayoutCandidateVariantIdentity>> BeforeVerticalAccessCandidates;

	/** Original identity paired with the saved domain; temporary intersections receive content-specific IDs. */
	FLayoutId BeforeVerticalAccessRestrictionId;

	/** Set only by owner-side junction proof: its certified domain may use Corner providers where the continuing and branching seams meet. Ordinary shared cells do not grant this admission. */
	bool bTreatAsJunctionPlacementZone = false;
};

/** Seeded child transform proven structurally feasible during parent prewarm. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPreparedChildPlacementHint
{
	/** Child content entry whose candidate stream owns this hint. */
	FName SourceContentEntryId;

	/** Stable required-instance ordinal within the child content entry. */
	int32 InstanceOrdinal = INDEX_NONE;

	/** Parent-local child transform selected by the prewarm witness. */
	FIntVector RegionCellOffset = FIntVector::ZeroValue;

	/** Stable mapped topology identity selected by the prewarm witness. */
	FLayoutId StageMappingId;
};

/** Self-contained request for solving one layout region. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveRequest
{
	/** Stable effective-snapshot id for this request. */
	FLayoutId EffectiveSnapshotId;

	/** Snapshot schema version used for replay compatibility. */
	int32 SnapshotSchemaVersion = 3;

	/** Stable debug path used for deterministic diagnostics and scheduler ordering. */
	FString RegionDebugPath;

	/** Deterministic seed for this region solve. */
	int32 Seed = 0;

	/** Parent- or root-relative cell offset applied when this request is merged into a scheduled region tree. */
	FIntVector RegionCellOffset = FIntVector::ZeroValue;

	/** Optional authored-block Z offset that later preview and realization code must apply when stamping solved templates. */
	int32 TemplatePlacementZOffsetBlocks = 0;

	/** World-facing placement kind chosen before solve execution. */
	ELayoutWorldBindingPlacementKind RootPlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** World-binding-owned placement policy that later realization must preserve without rereading live authoring state. */
	FLayoutWorldBindingPlacementPolicy WorldBindingPlacementPolicy;

	/** Optional stepped-terrain support map preserved on the request boundary before stepped adjacency is compiled into solver constraints. */
	FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

	/** Optional content-entry id that authored this region request when it came from unified recursive content selection. */
	FName SourceContentEntryId;

	/** Exact direct-child provider commitments already frozen before residual parent module search. */
	TArray<FLayoutZoneFeatureProviderCommitment> PrecommittedZoneFeatureProviderCommitments;

	/** Stable soft path of the profile snapshot source for pointer-free replay and child certification. */
	FSoftObjectPath ProfilePath;

	/** Stable owning world-binding id selected before solve execution; explicit direct-root solves may leave this empty. */
	FName WorldBindingId;

	/** Optional root solve id chosen by higher-level caller code for later queue/cache publication and trace correlation. */
	FLayoutId RootSolveId;

	/** Optional root candidate id chosen by world-binding, preview, or direct-root caller code for later publication and queue/cache tracing. */
	FLayoutId RootCandidateId;

	/** Optional root placement-policy id chosen by world-binding or direct-root request code for later queue/cache publication. */
	FLayoutId RootPlacementPolicyId;

	/** True when the request carries one selected root placement submission artifact. */
	bool bHasRootPlacementSubmission = false;

	/** Stable placement shift id selected before this root solve executes. */
	FLayoutId RootPlacementShiftId;

	/** Coarse placement shift selected before this root solve executes. */
	FIntVector RootPlacementShiftCells = FIntVector::ZeroValue;

	/** Exact root site center selected before this root solve executes. */
	FIntVector RootSiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Exact root reservation key selected before this root solve executes. */
	FIntPoint RootReservationKey = FIntPoint::ZeroValue;

	/** Optional resolved continuation-selection contract chosen before solve execution for world-facing path alignment. */
	FLayoutResolvedWorldBindingContinuationSelection RootContinuationSelection;

	/** True when terrain-mode adapters may consume FrozenTerrainBiomeAdapterInput without live world reads. */
	bool bHasFrozenTerrainBiomeAdapterInput = false;

	/** Frozen terrain/biome input captured before worker-side terrain-mode adapters run. */
	FLayoutFrozenTerrainBiomeAdapterInput FrozenTerrainBiomeAdapterInput;

	/** True when the background submit/finalizer path supplied a frozen selected mode plan. */
	bool bHasSelectedModePlan = false;

	/** Frozen mode plan selected before worker execution and consumed by solver scheduling. */
	FLayoutModePlan SelectedModePlan;

	/** Parent region path that selected this region request from a unified child content entry. */
	FString SourceParentRegionDebugPath;

	/** If true, this request came from an optional child content entry and may be dropped later if structural solving rejects it. */
	bool bSourceContentEntryOptional = false;

	/** Caller-supplied footprint size for override-planned region solves. */
	FIntPoint FootprintSize = FIntPoint::ZeroValue;

	/** Caller-supplied planned cells for override-planned region solves. */
	TArray<FLayoutPlannedCell> PlannedCells;

	/** Pre-computed planned cells from adapter. Non-empty when adapter ran in prewarm/preflight. */
	TArray<FLayoutPlannedCell> PrecomputedPlannedCells;

	/** Owning-region authored column tops before child reservation; residual proof must not redefine level policies. Empty for uncropped requests. */
	TMap<FIntPoint, int32> OwningTopModuleLevelByXY;

	/** Planned neighbors outside this regional request that must remain topology-visible across the solve cut. */
	TMap<FIntVector, uint8> ExternalPlannedNeighborFaceMasks;

	/** Frozen host alternatives for stepped VerticalAccess requirements. */
	TArray<FLayoutVerticalAccessHostGroup> VerticalAccessHostGroups;

	/** Seeded child transforms from one bounded prewarm feasibility witness. */
	TArray<FLayoutPreparedChildPlacementHint> PreparedChildPlacementHints;

	/** Recovery attempt for GroundOnly children on a single observed ground plane.
	 * Parent topology remains stepped; reservations, support and seams must be reproved. */
	bool bUseChildLocalFlatFallback = false;

	/** True when terrain-adapted Entry origins and VerticalAccess alternatives are final, including flat plans. */
	bool bHasFinalizedSteppedTerrainIntents = false;

	/** Pre-computed kind-qualified active cells from adapter. */
	TArray<FLayoutContractActiveCellRecord> PrecomputedActiveCells;

	/** Structured adapter diagnostics retained through prewarm, including flat-fallback provenance. */
	TArray<FLayoutAdapterDiagnostic> PrecomputedAdapterDiagnostics;

	/** True when prewarm pre-qualified entry cells and the solver should not select its own. */
	bool bHasQualifiedEntryCells = false;

	/** Pre-qualified entry cells from prewarm. Only populated when bHasQualifiedEntryCells is true. */
	TArray<FIntVector> QualifiedEntryCells;

	/** Pre-computed frozen terrain contract from adapter. */
	FLayoutFrozenTerrainContract PrecomputedFrozenTerrainContract;

	/** Pre-computed stepped terrain support map from adapter. */
	FLayoutSteppedTerrainSupportMap PrecomputedSteppedTerrainSupportMap;

	/** Immutable module-set contract consumed by the region job. */
	FLayoutModuleCatalog ModuleCatalog;

	/** Immutable unified content-set contract carried forward for adapter and future recursive solve work. */
	FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;

	/** Immutable profile contract consumed by the region job. */
	FLayoutProfileSolveSnapshot ProfileSnapshot;

	/** Execution-only budgets and diagnostics applied while solving this request. */
	FLayoutSolverExecutionSettings ExecutionSettings;

	/** Incoming region boundary points that later scheduler work can use to seed dependencies. */
	TArray<FLayoutSolveBoundaryPoint> IncomingBoundaryPoints;

	/** Explicit parent-side traversal anchors that must remain reachable in the solved route graph. */
	TArray<FLayoutCommittedTraversalAnchor> CommittedTraversalAnchors;

	/** Additional caller-supplied parent structural cells that reserved-open and open-budget compilation must preserve. */
	TArray<FIntVector> ProtectedStructuralCells;

	/** Explicit endpoint anchors committed by a parent before this region solves. */
	TArray<FLayoutCommittedEndpointAnchor> CommittedEndpointAnchors;

	/** Caller-owned anchored bundle insertions that must be preserved before ordinary search continues. */
	TArray<FLayoutForcedPlacementBundleInsertion> ForcedPlacementBundleInsertions;

	/** Caller-owned route constraints that must remain active before ordinary traversal planning continues. */
	TArray<FLayoutRouteConstraintRecord> RequiredRouteConstraints;

	/** Exact candidate domains certified before this regional proof begins. */
	TArray<FLayoutCellCandidateDomainRestriction> CandidateDomainRestrictions;

	/** Stable certificate shared by every candidate-domain restriction in this request. */
	FLayoutId CandidateDomainCertificateId;

	/** Future-terrace boundary points preserved only for later proof translation, not current-stage export ownership. */
	TArray<FLayoutSolveBoundaryPoint> FutureTerraceProofBoundaryPoints;

	/** Immutable negotiated child-responsibility contracts already fixed before this region solve starts. */
	TArray<FLayoutNegotiatedChildResponsibilityContract> NegotiatedChildResponsibilityContracts;

	/** Child content set snapshots keyed by child region debug path.
	 *  Populated before parent solve so the parent CSP can validate child
	 *  boundary feasibility during candidate evaluation. */
	TMap<FString, FLayoutRegionContentSetSolveSnapshot> ChildContentSetSnapshots;

	/** Child module catalogs keyed by child region debug path.
	 *  Populated alongside ChildContentSetSnapshots for parent CSP lookup. */
	TMap<FString, FLayoutModuleCatalog> ChildModuleCatalogs;

	/** Exact parent-translated child planned cells keyed by child region path for parent boundary-domain admission. */
	TMap<FString, TArray<FLayoutPlannedCell>> ChildPlannedCellsByRegion;

	/** If true, use the caller-supplied child capability envelope instead of rebuilding a generic union from the request snapshots. */
	bool bUseSuppliedChildCapabilityEnvelope = false;

	/** Exact rotated child capability envelope selected during recursive scheduling for later seam/traversal contract validation. */
	FLayoutChildCapabilityEnvelope SuppliedChildCapabilityEnvelope;

	/** Parent zone feature requirement ids explicitly delegated to this child request during negotiation. */
	TArray<FLayoutId> DelegatedZoneFeatureRequirementIds;

	/** Parent closure requirement ids explicitly delegated to this child request during negotiation. */
	TArray<FLayoutId> DelegatedClosureRequirementIds;

	/** Exact endpoint capability ids selected by parent negotiation when an existing producer records them. */
	TArray<FLayoutId> CertifiedSelectedEndpointCapabilityIds;

	/** True when CertifiedSelectedTraversalCapabilityIds came from an authoritative selected-traversal artifact. */
	bool bHasCertifiedSelectedTraversalCapabilityArtifact = false;

	/** Exact traversal capability ids selected by parent negotiation when an existing producer records them. */
	TArray<FLayoutId> CertifiedSelectedTraversalCapabilityIds;

	/** Exact vertical capability ids selected by parent negotiation when an existing producer records them. */
	TArray<FLayoutId> CertifiedSelectedVerticalCapabilityIds;

	/** Exact closure-span capability id selected by parent negotiation when one delegated closure requirement has one selected span. */
	FLayoutId CertifiedSelectedClosureSpanCapabilityId;

	/** Exact seam capability id selected by parent negotiation when an existing producer records it. */
	FLayoutId CertifiedSelectedSeamCapabilityId;

	/** Exact seam witness id selected by parent negotiation when an existing producer records it. */
	FLayoutId CertifiedSelectedSeamWitnessId;

	/** Exact seam witness ids delegated by parent negotiation when existing seam planning records them. */
	TArray<FLayoutId> CertifiedDelegatedSeamWitnessIds;

	/** Exact junction witness ids delegated by parent negotiation when existing seam planning records them. */
	TArray<FLayoutId> CertifiedDelegatedJunctionWitnessIds;

	/** Exact host vertical-access witness ids delegated by parent negotiation when existing responsibility records them. */
	TArray<FLayoutId> CertifiedDelegatedHostVerticalAccessWitnessIds;

	/** True when CertifiedParentAssertionSubsetIds came from an authoritative requirement-to-assertion artifact. */
	bool bHasCertifiedParentAssertionSubsetArtifact = false;

	/** Exact parent assertion ids used to certify this child when an existing producer narrows the parent assertion set. */
	TArray<FLayoutId> CertifiedParentAssertionSubsetIds;

	/** True when parent negotiation emitted a worker/result-local parent-branch certificate for this child request. */
	bool bHasCertifiedParentBranchCertificateArtifact = false;

	/** Stable worker/result-local parent-branch certificate artifact id carried for diagnostics and handoff correlation. */
	FLayoutId CertifiedParentBranchCertificateArtifactId;

	/** Parent contract id referenced by the worker/result-local parent-branch certificate. */
	FLayoutId CertifiedParentBranchParentContractId;

	/** Deterministic parent contract fingerprint referenced by the worker/result-local parent-branch certificate. */
	uint64 CertifiedParentBranchParentContractHash = 0;

	/** Stable selected branch id from parent negotiation. */
	FLayoutId CertifiedParentBranchId;

	/** Stable selected child branch/scout id from parent negotiation. */
	FLayoutId CertifiedParentBranchChildScoutResultId;

	/** Deterministic selected-field fingerprint for the carried parent-branch certificate. */
	uint64 CertifiedParentBranchCertificateInputHash = 0;

	/** If true, local closure validation is deferred so a later schedule-level merged audit can include committed child providers. */
	bool bDeferClosureValidationToSchedule = false;

	/** If true, local traversal route planning and final reachability are deferred so a later schedule-level merged audit can validate recursive circulation across children and seams. */
	bool bDeferTraversalValidationToSchedule = false;

	/** Proof-bearing records copied onto the request for fixture replay and diagnostics. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Fail-fast assertion results copied onto the request for fixture replay and diagnostics. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;
};

/** Result wrapper for one region solve, including exported boundary metadata. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveResult
{
	/** Debug path copied from the request that produced this result. */
	FString RegionDebugPath;

	/** Parent- or root-relative cell offset copied from the request for schedule merging and preview diagnostics. */
	FIntVector RegionCellOffset = FIntVector::ZeroValue;

	/** Optional content-entry id copied from the request that produced this result. */
	FName SourceContentEntryId;

	/** True when this region was not solved because it was deterministically dropped as an optional child. */
	bool bDroppedAsOptionalChild = false;

	/** Optional drop-decision id when the region was dropped as an optional child. */
	FLayoutId OptionalDropDecisionId;

	/** Human-readable reason captured when the optional child drop was recorded. */
	FString OptionalDropReason;

	/** Existing public solve result preserved for realization and current callers. */
	FLayoutSolveResult SolveResult;

	/** Boundary points exported from solved placements for later dependent regions. */
	TArray<FLayoutSolveBoundaryPoint> ExportedBoundaryPoints;

	/** Future-terrace boundary points preserved only for later proof translation, not current-stage export ownership. */
	TArray<FLayoutSolveBoundaryPoint> FutureTerraceProofBoundaryPoints;

	/** Explicit endpoint anchors this region was asked to honor before solve. */
	TArray<FLayoutCommittedEndpointAnchor> CommittedEndpointAnchors;

	/** Immutable negotiated child-responsibility contracts visible to diagnostics and fixture replay. */
	TArray<FLayoutNegotiatedChildResponsibilityContract> NegotiatedChildResponsibilityContracts;

	/** Proof records attached to the request before this region proof started. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Validation assertions attached to the request before this region proof started. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;

	/** Request-owned stepped terrain support map preserved for public terrain-stepped diagnostics. */
	FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

	/** Request-owned forced bundle insertions preserved for public terrain-stepped diagnostics. */
	TArray<FLayoutForcedPlacementBundleInsertion> ForcedPlacementBundleInsertions;

	/** Request-owned required route constraints preserved for public terrain-stepped diagnostics. */
	TArray<FLayoutRouteConstraintRecord> RequiredRouteConstraints;

	/** Public diagnostics for every stepped-terrain transition compiled on the root path. */
	TArray<FLayoutSteppedTerrainTransitionDiagnostic> SteppedTerrainTransitionDiagnostics;

	/** Public diagnostics for the stepped-terrain transitions the root path could not currently satisfy. */
	TArray<FLayoutSteppedTerrainTransitionDiagnostic> UnsupportedSteppedTerrainTransitionDiagnostics;

	/** Future-terrace closure segments preserved only for later proof translation, not current-stage closure ownership. */
	TArray<FLayoutClosureCoverageSegmentRecord> FutureTerraceProofClosureSegments;

	/** Deterministic staged-terrain diagnostics grouped by plateau stage and later frontier unlock order. */
	TArray<FLayoutSteppedTerrainStageDiagnostic> TerrainStageDiagnostics;

	/** Deterministic staged-terrain ownership/support diagnostics keyed by ascent-frontier id. */
	TArray<FLayoutSteppedTerrainFrontierOwnershipDiagnostic> TerrainFrontierOwnershipDiagnostics;

	/** True when every public stepped-terrain transition diagnostic was structurally supported on the root path. */
	bool bCanSatisfySteppedTerrainTransitions = true;
};

/** Directed dependency requiring one region to finish before another can solve. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveDependency
{
	/** Region debug path that must complete first. */
	FString PrerequisiteRegionDebugPath;

	/** Region debug path that waits for the prerequisite. */
	FString DependentRegionDebugPath;
};

/** Owner-side junction requirement preserved across explicit seam-planning boundaries. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutOwnedSeamJunctionRequirement
{
	/** Stable junction requirement identifier used by diagnostics and replay coverage. */
	FLayoutId JunctionRequirementId;

	/** Seam owner that must satisfy this first-pass junction contract. */
	FString OwnerRegionDebugPath;

	/** Region passively attaching as the terminating branch on the owner-side junction. */
	FString PassiveRegionDebugPath;

	/** Narrow adjacency-class identifier used by the first-pass junction topology rules. */
	FLayoutId AdjacencyClassId;

	/** Same-level owner-local junction cell where the continuing run and branch meet. */
	FIntVector JunctionCell = FIntVector::ZeroValue;

	/** Seam that stays the owner-defined continuing run at this first-pass junction. */
	FLayoutId ContinuingSeamId;

	/** Seam that terminates into the continuing run, or None when owner-perimeter geometry supplies the branch face. */
	FLayoutId BranchSeamId;

	/** Passive region that stays attached across the continuing run. */
	FString ContinuingPassiveRegionDebugPath;

	/** Interface family shared by the continuing run and terminating branch at this junction. */
	FGameplayTag InterfaceFamily;

	/** Owner-facing continuing-run direction used by the first-pass owner-side junction contract. Realization should map the module's main run against this side after existing face rules identify the outward side. */
	ELayoutFaceDirection ContinuingOwnerFaceDirection = ELayoutFaceDirection::PosX;

	/** Owner-facing terminating-branch direction used by the first-pass owner-side junction contract. Realization should map the module's branch side against this direction after existing face rules identify the outward side. */
	ELayoutFaceDirection BranchOwnerFaceDirection = ELayoutFaceDirection::PosX;
};

/** Batch of region requests submitted to the synchronous scheduler facade. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveScheduleRequest
{
	/** Region requests to execute. Independent requests are sorted by debug path for deterministic output. */
	TArray<FLayoutRegionSolveRequest> RegionRequests;

	/** Dependency edges between region debug paths. */
	TArray<FLayoutRegionSolveDependency> Dependencies;

	/** Parent-owned child commitments that must be validated before dependent child solves begin. */
	TArray<FLayoutDirectChildRegionCommitment> DirectChildCommitments;

	/** Explicit seam contracts already committed by the recursive scheduler before late validation/export runs. */
	TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams;

	/** True when the supplied planned seams already represent the authoritative committed schedule and must not be expanded later. */
	bool bPlannedPartitionSeamsAreAuthoritative = false;

	/** Explicit owner-side junction requirements already committed by seam planning before late validation/export runs. */
	TArray<FLayoutOwnedSeamJunctionRequirement> PlannedJunctionRequirements;
};

/** Public recursive vertical-access accounting summary captured from one successful region-tree solve. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRecursiveVerticalAccessSummary
{
	/** Host/root vertical-access provider count the composed region tree had to satisfy. */
	int32 RequiredHostProviderCount = 0;

	/** True when the composed region tree had to satisfy the host/root provider count exactly. */
	bool bRequiresExactHostProviderCount = false;

	/** Total host/root provider groups satisfied after composed parent/child ascent ownership is resolved. */
	int32 ResolvedHostProviderCount = 0;

	/** Host/root provider count still satisfied directly by the parent/root region after reservations settle. */
	int32 CountedParentProviderCount = 0;

	/** Exact parent/root local representative cells for the counted retained parent provider groups. */
	TArray<FIntVector> CountedParentVerticalAccessCells;

	/** Parent/root local vertical-access cells preserved only for retained route-support or child handoff traversal. */
	TArray<FIntVector> RetainedParentRouteSupportVerticalAccessCells;

	/** Child-region debug paths that were counted upward to satisfy the remaining host/root requirement. */
	TArray<FString> CountedChildRegionDebugPaths;

	/** Child-region debug paths that kept local vertical access only and were intentionally not counted upward. */
	TArray<FString> LocalOnlyChildRegionDebugPaths;

	/** Child-region debug paths that were authored as host-contributing but could not actually provide usable vertical access. */
	TArray<FString> UnusableContributingChildRegionDebugPaths;

	/** Child-region debug paths that contributed usable vertical access beyond the minimum host/root requirement. */
	TArray<FString> ExtraContributingChildRegionDebugPaths;

	/** Optional summary failure/explanation string when the composed host/root requirement could not be satisfied cleanly. */
	FString FailureReason;
};

/** Deterministic result of one synchronous scheduled region batch. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveScheduleResult
{
	/** True when every region request executed and succeeded. */
	bool bSucceeded = false;

	/** Region results in deterministic execution order. */
	TArray<FLayoutRegionSolveResult> RegionResults;

	/** Existing public solve result shape merged from all scheduled region results. */
	FLayoutSolveResult MergedSolveResult;

	/** Exact child stage mappings preserved in certified child order for artifact and publication parity. */
	TArray<FLayoutChildStageMappingResult> ChildStageMappings;

	/** Explicit owner-side junction requirements preserved on the public schedule boundary. */
	TArray<FLayoutOwnedSeamJunctionRequirement> PlannedJunctionRequirements;

	/** Recursive host/root vertical-access composition summary retained for diagnostics and editor tooling. */
	FLayoutRecursiveVerticalAccessSummary RecursiveVerticalAccessSummary;

	/** True when deferred complete-placement validation used the independent residual-parent plus child-proof split path. */
	bool bUsedIndependentDeferredProofSplit = false;

	/** True when final merge consumed already-certified regional proof outputs instead of rerunning their CSP solves. */
	bool bConsumedCertifiedRegionalProofResults = false;

	/** Failure detail for invalid dependencies, cycles, or failed region solves. */
	FString FailureReason;
};

/** Stable indexed candidate record used by the upcoming bitset-backed solver path. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutIndexedDomainCandidate
{
	/** Index used by bitset domains and compatibility matrices. */
	int32 CandidateIndex = INDEX_NONE;

	/** Variant index from the current oriented-variant table, or INDEX_NONE for an intentional empty cell. */
	int32 VariantIndex = INDEX_NONE;

	/** Module snapshot index used as worker-safe candidate identity, or INDEX_NONE for an intentional empty cell. */
	int32 ModuleSnapshotIndex = INDEX_NONE;

	/** Module snapshot id used as worker-safe candidate identity, or None for an intentional empty cell. */
	FLayoutId ModuleSnapshotId;

	/** Stable module debug name, or "Empty" for intentional empty-cell candidates. */
	FName ModuleDebugName;

	/** Yaw rotation in clockwise 90-degree steps. */
	int32 YawRotationSteps = 0;

	/** True when this candidate represents an intentionally empty planned cell. */
	bool bEmpty = false;
};

/** One cell's current candidate domain represented as stable indices and bit words. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutIndexedCellDomain
{
	/** Planned cell represented by this domain. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Planned intent represented by this domain. */
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Ordered candidate indices matching the current deterministic candidate order for this cell. */
	TArray<int32> OrderedCandidateIndices;

	/** Bitset containing every candidate in this cell domain. */
	TArray<uint64> CandidateBits;
};

/** Bitset row listing which target candidates can neighbor one source candidate on one face. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutIndexedCandidateCompatibility
{
	/** Source candidate index for this compatibility row. */
	int32 SourceCandidateIndex = INDEX_NONE;

	/** Source face direction being tested. */
	ELayoutFaceDirection Direction = ELayoutFaceDirection::PosX;

	/** Bitset of target candidate indices compatible against the opposite face. */
	TArray<uint64> CompatibleCandidateBits;
};

/** Indexed candidate-domain snapshot built from the current domains for parity-gated optimization. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutIndexedDomainSnapshot
{
	/** True when the indexed snapshot was built from valid request data. */
	bool bSucceeded = false;

	/** Candidate universe shared by every cell domain. */
	TArray<FLayoutIndexedDomainCandidate> Candidates;

	/** Cell domains in planned-cell order. */
	TArray<FLayoutIndexedCellDomain> CellDomains;

	/** Candidate compatibility matrix rows keyed by source candidate and face direction. */
	TArray<FLayoutIndexedCandidateCompatibility> CompatibilityRows;

	/** Structured preparation failure preserved before candidate CSP search begins. */
	ELayoutSolvePreparationFailureKind PreparationFailureKind = ELayoutSolvePreparationFailureKind::None;

	/** Candidate attempts consumed while building this pre-search snapshot. Preparation rejection must keep this at zero. */
	int32 CandidateAttemptCount = 0;

	/** Validation and conversion messages. */
	TArray<FLayoutValidationMessage> Messages;

	/** Failure detail when the snapshot cannot be built. */
	FString FailureReason;
};

/**
 * Deterministic offline solver for the first supported footprint-fill layout
 * family. This stays in-memory only and is intended to back preview, validation,
 * and later chunk-load realization.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutProfileSolver
{
public:
	/** Derives a stable per-region seed from world/layout identity without reading UObject state. */
	static int32 DeriveRegionSeed(
		int32 WorldSeed, const FIntVector& LayoutLocationCell, FName PlanningContextName, const FString& RegionDebugPath, FName LayoutProfileName);

	/** Copies one module asset and its set-local weight into an immutable solver-facing snapshot. */
	static FLayoutModuleSolveSnapshot BuildModuleSnapshot(
		const ULayoutModuleAsset* Module,
		int32 Weight = 1,
		ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel,
		int32 SpecificLevel = 0,
		bool bOptional = false,
		const FIntVector& EstablishedSharedCellSizeInBlocks = FIntVector::ZeroValue);

	/** Copies one composite asset and its fixed glued leaf arrangement into an immutable solver-facing snapshot. */
	static FLayoutModuleSolveSnapshot BuildCompositeModuleSnapshot(
		const ULayoutCompositeModuleAsset* Composite,
		int32 Weight = 1,
		ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any,
		ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel,
		int32 SpecificLevel = 0,
		bool bOptional = false,
		const FIntVector& EstablishedSharedCellSizeInBlocks = FIntVector::ZeroValue);

	/** Compiles module-backed entries from a unified content set into the current module-solve snapshot path. */
	static FLayoutModuleCatalog BuildModuleCatalog(const ULayoutRegionContentSetAsset* ContentSet);

	/** Copies one unified content set into an immutable solver-facing snapshot. */
	static FLayoutRegionContentSetSolveSnapshot BuildContentSetSnapshot(const ULayoutRegionContentSetAsset* ContentSet);

	/** Copies one profile asset into an immutable solver-facing snapshot. */
	static FLayoutProfileSolveSnapshot BuildProfileSnapshot(const ULayoutProfileAsset* Profile);

	/** Finalizes terrain-adapted Entry/host authority before CSP; selected flat modes share flat recovery preparation. */
	static bool FinalizeSteppedTerrainPlan(
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason,
		ELayoutSteppedTerrainFinalizationFailureKind* OutFailureKind = nullptr,
		TArray<FLayoutCellReservationRecord>* OutSelectedReservations = nullptr);

	/** Applies flat reservations, Entry admission and exact host planning once, without introducing stepped topology. */
	static bool FinalizeFlatTerrainPlan(
		TArray<FLayoutPlannedCell>& InOutPlannedCells,
		FLayoutRegionSolveRequest& InOutRequest,
		FString& OutFailureReason);

	/** Builds the standard single-region request from the new unified content-set authoring surface for non-world-facing solves, with an optional request-owned shared-cell override. */
	static FLayoutRegionSolveRequest BuildStandaloneRegionRequest(
		const ULayoutRegionContentSetAsset* ContentSet,
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FString& RegionDebugPath = TEXT("Standalone"),
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings(),
		FLayoutId RootPlacementPolicyId = NAME_None,
		FLayoutId RootCandidateId = NAME_None,
		int32 TemplatePlacementZOffsetBlocks = 0,
		FLayoutId RootSolveId = NAME_None,
		const FIntVector* SharedCellSizeOverride = nullptr);

	/** Builds the standard single-region request from unified content while preserving an already resolved world-binding placement contract and an optional request-owned shared-cell override. */
	static FLayoutRegionSolveRequest BuildStandaloneRegionRequest(
		const ULayoutRegionContentSetAsset* ContentSet,
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		FLayoutId RootPlacementPolicyId,
		FLayoutId RootCandidateId,
		int32 TemplatePlacementZOffsetBlocks,
		FLayoutId RootSolveId,
		ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
		const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap = nullptr,
		const FIntVector* SharedCellSizeOverride = nullptr);

	/** Builds the standard single-region request using the profile's authored unified content set for non-world-facing solves, with an optional request-owned shared-cell override. */
	static FLayoutRegionSolveRequest BuildStandaloneRegionRequest(
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FString& RegionDebugPath = TEXT("Standalone"),
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings(),
		FLayoutId RootPlacementPolicyId = NAME_None,
		FLayoutId RootCandidateId = NAME_None,
		int32 TemplatePlacementZOffsetBlocks = 0,
		FLayoutId RootSolveId = NAME_None,
		const FIntVector* SharedCellSizeOverride = nullptr);

	/** Builds the standard single-region request from profile-owned content while preserving an already resolved world-binding placement contract and an optional request-owned shared-cell override. */
	static FLayoutRegionSolveRequest BuildStandaloneRegionRequest(
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FString& RegionDebugPath,
		const FLayoutSolverExecutionSettings& ExecutionSettings,
		FLayoutId RootPlacementPolicyId,
		FLayoutId RootCandidateId,
		int32 TemplatePlacementZOffsetBlocks,
		FLayoutId RootSolveId,
		ELayoutWorldBindingPlacementKind RootPlacementKind,
		const FLayoutWorldBindingPlacementPolicy& WorldBindingPlacementPolicy,
		const FLayoutSteppedTerrainSupportMap* SteppedTerrainSupportMap = nullptr,
		const FIntVector* SharedCellSizeOverride = nullptr);

	/** Solves one region request and returns the region wrapper while preserving the existing public solve result. */
	static FLayoutRegionSolveResult SolveRegion(const FLayoutRegionSolveRequest& Request);

	/** Worker/automation compatibility entry point for scheduled region proof; production game-thread callers fail fast. */
	static FLayoutRegionSolveScheduleResult SolveRegionsSynchronously(const FLayoutRegionSolveScheduleRequest& ScheduleRequest);

	/** Worker/testing-only root proof helper; production game-thread callers must submit background snapshot jobs instead. */
	static FLayoutRegionSolveScheduleResult SolveRegionTree(const FLayoutRegionSolveRequest& RootRequest);

	/** Builds indexed bitset candidate domains from the current candidate domains without changing solve behavior. */
	static FLayoutIndexedDomainSnapshot BuildIndexedDomainSnapshot(const FLayoutRegionSolveRequest& Request);

	/** Attempts one deterministic footprint-fill solve from the unified content-set surface; production game-thread callers fail fast. */
	static FLayoutSolveResult Solve(
		const ULayoutRegionContentSetAsset* ContentSet,
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings());

	/** Attempts one deterministic footprint-fill solve using the profile's authored unified content set; production game-thread callers fail fast. */
	static FLayoutSolveResult Solve(
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings());

	/** Attempts one deterministic solve using caller-supplied planned cells; production game-thread callers fail fast. */
	static FLayoutSolveResult SolveWithPlannedCells(
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings());

#if WITH_AUTOMATION_TESTS
	/** Solves with indexed candidate filtering disabled so automation can enforce bitset parity against the unindexed filtering path. */
	static FLayoutSolveResult SolveWithoutIndexedFiltering(
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings());

	/** Solves supplied planned cells with indexed candidate filtering disabled for focused automation parity checks. */
	static FLayoutSolveResult SolveWithPlannedCellsWithoutIndexedFiltering(
		const ULayoutProfileAsset* Profile,
		int32 Seed,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutSolverExecutionSettings& ExecutionSettings = FLayoutSolverExecutionSettings());

	/** Returns the sibling shared-seam face directions used by recursive scheduling for same-world-cell overlap checks. */
	static bool DebugTryGetSiblingSharedOverlapFaceDirections(
		const FIntVector& CandidateLocalCell,
		const FIntPoint& CandidateFootprintSize,
		const FIntVector& ExistingLocalCell,
		const FIntPoint& ExistingFootprintSize,
		ELayoutFaceDirection& OutCandidateFaceDirection,
		ELayoutFaceDirection& OutExistingFaceDirection);

	/** Returns the closure/shared-seam overlap face directions used during merged seam auditing. */
	static bool DebugTryGetClosureSharedOverlapFaceDirections(
		const FIntVector& FirstLocalCell,
		const FIntPoint& FirstFootprintSize,
		const FIntVector& SecondLocalCell,
		const FIntPoint& SecondFootprintSize,
		ELayoutFaceDirection& OutFirstFaceDirection,
		ELayoutFaceDirection& OutSecondFaceDirection);

	/** Holds selected solver cancellation checkpoints until automation releases or cancels them. */
	static void SetCancellationCheckpointGateForTesting(TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Gate);

	/** Records when automation-targeted solver cancellation checkpoints are reached. */
	static void SetCancellationCheckpointCounterForTesting(TSharedPtr<FThreadSafeCounter, ESPMode::ThreadSafe> Counter);

	/** Runs one automation-only checkpoint probe that mirrors solver-loop cancellation polling. */
	static bool RunCancellationCheckpointProbeForTesting(int32 IterationCount);
#endif
};
