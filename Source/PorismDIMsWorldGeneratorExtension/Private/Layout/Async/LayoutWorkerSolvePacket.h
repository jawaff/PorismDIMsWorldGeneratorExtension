// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "Layout/Async/LayoutBackgroundSolveSnapshot.h"
#include "Layout/Async/LayoutChildSolveHandoff.h"
#include "Layout/Contracts/LayoutContractManifestCache.h"

struct FPlannedLayoutSiteRecord;
struct FResolvedLayoutConnectorRecord;
struct FLayoutWorldBindingRuntimeView;

/** Pointer-free request manifest carried by async packets before worker-side request finalization. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorkerSolveRequestManifest
{
	/** Stable effective snapshot id copied from the frozen contract source. */
	FLayoutId EffectiveSnapshotId;

	/** Snapshot schema version copied from the frozen contract source. */
	int32 SnapshotSchemaVersion = 1;

	/** Stable region debug path for deterministic proof ordering and diagnostics. */
	FString RegionDebugPath;

	/** Parent- or root-relative region offset carried into merged schedules. */
	FIntVector RegionCellOffset = FIntVector::ZeroValue;

	/** Seed captured from the source contract for packet/request consistency checks. */
	int32 CapturedSeed = 0;

	/** Template Z offset captured from the source contract for packet/request consistency checks. */
	int32 CapturedTemplatePlacementZOffsetBlocks = 0;

	/** Placement kind captured from the source contract for packet/request consistency checks. */
	ELayoutWorldBindingPlacementKind CapturedRootPlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Stable owning world-binding id captured from the source contract boundary. */
	FName WorldBindingId;

	/** Optional root solve id chosen by world-binding or preview request code. */
	FLayoutId RootSolveId;

	/** Optional root candidate id chosen by world-binding or preview request code. */
	FLayoutId RootCandidateId;

	/** Optional root placement policy id chosen by world-binding or preview request code. */
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

	/** Optional resolved continuation-selection contract chosen before solve execution. */
	FLayoutResolvedWorldBindingContinuationSelection RootContinuationSelection;

	/** True when the manifest preserves frozen terrain/biome adapter input. */
	bool bHasFrozenTerrainBiomeAdapterInput = false;

	/** Frozen terrain/biome input captured for worker-side terrain-mode adapters. */
	FLayoutFrozenTerrainBiomeAdapterInput FrozenTerrainBiomeAdapterInput;

	/** True when the manifest preserves the frozen selected mode plan. */
	bool bHasSelectedModePlan = false;

	/** Frozen mode plan selected before worker execution. */
	FLayoutModePlan SelectedModePlan;

	/** Optional content-entry id that authored this region request. */
	FName SourceContentEntryId;

	/** Exact direct-child provider commitments already frozen for residual parent module search. */
	TArray<FLayoutZoneFeatureProviderCommitment> PrecommittedZoneFeatureProviderCommitments;

	/** Stable soft path of the request profile snapshot source. */
	FSoftObjectPath ProfilePath;

	/** Parent region path that selected this region request from a child content entry. */
	FString SourceParentRegionDebugPath;

	/** True when this request came from an optional child content entry. */
	bool bSourceContentEntryOptional = false;

	/** True when supplied planned cells should override profile-driven planning. */
	bool bUseSuppliedPlannedCells = false;

	/** True when supplied stepped Entry and VerticalAccess intents are final. */
	bool bHasFinalizedSteppedTerrainIntents = false;

	/** Frozen alternatives for each stepped VerticalAccess requirement. */
	TArray<FLayoutVerticalAccessHostGroup> VerticalAccessHostGroups;

	/** Seeded child transforms from one bounded prewarm feasibility witness. */
	TArray<FLayoutPreparedChildPlacementHint> PreparedChildPlacementHints;

	/** Preserves the selected child-local recovery mode across prewarm and worker execution. */
	bool bUseChildLocalFlatFallback = false;

	/** Caller-supplied footprint size for override-planned region solves. */
	FIntPoint FootprintSize = FIntPoint::ZeroValue;

	/** Caller-supplied planned cells for override-planned region solves. */
	TArray<FLayoutPlannedCell> PlannedCells;

	/** Planned neighbors outside this regional request that remain topology-visible. */
	TMap<FIntVector, uint8> ExternalPlannedNeighborFaceMasks;

	/** Selected reserved-open terrain authority created with an authored flat plan before adapter prewarm. */
	TArray<FLayoutCellReservationRecord> ReservedOpenTerrainReservations;

	/** Structured adapter diagnostics retained through prewarm, including flat-fallback provenance. */
	TArray<FLayoutAdapterDiagnostic> PrecomputedAdapterDiagnostics;

	/** True when prewarm pre-qualified entry cells for the solver. */
	bool bHasQualifiedEntryCells = false;

	/** Pre-qualified entry cells from prewarm, subset of footprint boundary cells. */
	TArray<FIntVector> QualifiedEntryCells;

	/** Immutable module-set contract consumed by the worker job. */
	FLayoutModuleCatalog ModuleCatalog;

	/** Immutable content-set contract consumed by the worker job. */
	FLayoutRegionContentSetSolveSnapshot ContentSetSnapshot;

	/** Immutable profile contract consumed by the worker job. */
	FLayoutProfileSolveSnapshot ProfileSnapshot;

	/** Execution-only budgets and diagnostics applied while solving this request. */
	FLayoutSolverExecutionSettings ExecutionSettings;

	/** Incoming region boundary points for dependent proof. */
	TArray<FLayoutSolveBoundaryPoint> IncomingBoundaryPoints;

	/** Parent-side traversal anchors that must remain reachable. */
	TArray<FLayoutCommittedTraversalAnchor> CommittedTraversalAnchors;

	/** Caller-supplied structural cells that must remain reserved/open. */
	TArray<FIntVector> ProtectedStructuralCells;

	/** Endpoint anchors committed before this region solves. */
	TArray<FLayoutCommittedEndpointAnchor> CommittedEndpointAnchors;

	/** Anchored bundle insertions that must be preserved before ordinary search continues. */
	TArray<FLayoutForcedPlacementBundleInsertion> ForcedPlacementBundleInsertions;

	/** Route constraints that must remain active before ordinary traversal planning continues. */
	TArray<FLayoutRouteConstraintRecord> RequiredRouteConstraints;

	/** Future-terrace boundary points preserved for proof translation. */
	TArray<FLayoutSolveBoundaryPoint> FutureTerraceProofBoundaryPoints;

	/** Negotiated child-responsibility contracts fixed before this region solve starts. */
	TArray<FLayoutNegotiatedChildResponsibilityContract> NegotiatedChildResponsibilityContracts;

	/** True when SuppliedChildCapabilityEnvelope must override rebuilt generic child capability. */
	bool bUseSuppliedChildCapabilityEnvelope = false;

	/** Exact child capability envelope selected during recursive scheduling. */
	FLayoutChildCapabilityEnvelope SuppliedChildCapabilityEnvelope;

	/** Parent zone feature requirement ids explicitly delegated to this child request. */
	TArray<FLayoutId> DelegatedZoneFeatureRequirementIds;

	/** Parent closure requirement ids explicitly delegated to this child request. */
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

	/** True when closure validation is deferred to merged schedule audit. */
	bool bDeferClosureValidationToSchedule = false;

	/** True when traversal validation is deferred to merged schedule audit. */
	bool bDeferTraversalValidationToSchedule = false;

	/** Proof records copied onto the request for replay and diagnostics. */
	TArray<FLayoutProofRecord> ProofRecords;

	/** Validation assertions copied onto the request for replay and diagnostics. */
	TArray<FLayoutValidationAssertionRecord> ValidationAssertions;

	/** Request-owned stepped terrain support map. */
	FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

	/** Captures one worker-safe manifest from a frozen solve request. */
	static FLayoutWorkerSolveRequestManifest CaptureFromSolveRequest(const FLayoutRegionSolveRequest& Request);

	/** Rehydrates this manifest into one solve request shell before packet overrides are applied. */
	void PopulateSolveRequest(FLayoutRegionSolveRequest& OutRequest) const;

	/** Returns one solve request shell for parent-resume normalization without reopening the original proof plan. */
	FLayoutRegionSolveRequest ToSolveRequestForResume() const;

	/** Builds a frozen request-manifest artifact directly from this packet manifest without rebuilding a live request. */
	FLayoutFrozenRequestManifestArtifact BuildFrozenRequestManifestArtifact() const;

	/** Returns true when this manifest carries no live UObject-backed carriers. */
	bool ValidateNoLiveObjectCarriers(FString& OutFailureReason) const;
};

/** High-level worker packet kind used by Phase 1B async solve routing. */
enum class ELayoutWorkerSolvePacketKind : uint8
{
	PlanningRoot,
	ExplicitPreviewRoot,
	ObservedFallbackRoot,
	Continuation,
	Child
};

/** Narrow pointer-free solve packet captured on the game thread before worker-side request finalization. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutWorkerSolvePacket
{
	/** Stable debug name used for diagnostics only. */
	FString DebugName;

	/** High-level solve packet kind for deterministic routing and diagnostics. */
	ELayoutWorkerSolvePacketKind Kind = ELayoutWorkerSolvePacketKind::PlanningRoot;

	/** Stable world-facing runtime snapshot copied from the selected binding/candidate context. */
	FLayoutWorldBindingRuntimeSnapshot RuntimeSnapshot;

	/** Deterministic solve seed carried into later worker-side request finalization. */
	int32 SolveSeed = 0;

	/** Deterministic retry attempt index for future async retry scheduling. */
	int32 AttemptIndex = 0;

	/** Primary site/endpoint block-world position for this packet. */
	FIntVector PrimaryBlockWorldPos = FIntVector::ZeroValue;

	/** Secondary endpoint block-world position for continuation packets when one exists. */
	FIntVector SecondaryBlockWorldPos = FIntVector::ZeroValue;

	/** Optional stable text key carried from planning or child ownership. */
	FString StableTextKey;

	/** Optional stable numeric key carried from connector or child ownership. */
	uint64 StableNumericKey = 0;

	/** Optional biome row context carried when one world-facing path selected it before submit. */
	FName BiomeRowName;

	/** True only after a frozen parent-negotiated child handoff payload has been captured for child solve work. */
	bool bHasFrozenChildHandoff = false;

	/** Frozen parent-negotiated child handoff carried by child packets only. */
	FLayoutChildSolveHandoff FrozenChildHandoff;

	/** True only after a pointer-free request manifest has been copied onto the packet. */
	bool bHasRequestManifest = false;

	/** Pointer-free request manifest carried by this packet for worker-side finalization. */
	FLayoutWorkerSolveRequestManifest RequestManifest;

	/** True only after submit captured the selected mode plan from frozen request values. */
	bool bHasSelectedModePlan = false;

	/** Location-sensitive mode selected on submit without terrain/proof work. */
	FLayoutModePlan SelectedModePlan;

	/** Precomputed adapter output produced by the descriptor producer during prewarm. */
	FLayoutAdapterOutput PrecomputedAdapterOutput;

	/** True when the descriptor producer ran the adapter and the output above is authoritative. */
	bool bHasPrecomputedAdapterOutput = false;

	/** Captures one planning-window root packet from the pending planning record plus resolved runtime view. */
	static FLayoutWorkerSolvePacket CapturePlanningRoot(
		const FString& DebugName,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FPlannedLayoutSiteRecord& PendingRecord,
		int32 AttemptIndex = 0);

	/** Captures one explicit preview packet from a non-binding or binding-aware runtime view. */
	static FLayoutWorkerSolvePacket CaptureExplicitPreviewRoot(
		const FString& DebugName,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 SolveSeed,
		int32 AttemptIndex = 0,
		FName BiomeRowName = NAME_None);

	/** Captures one immediate fallback packet for observed-chunk root publication through the shared store path. */
	static FLayoutWorkerSolvePacket CaptureObservedFallbackRoot(
		const FString& DebugName,
		const FLayoutWorldBindingRuntimeView& RuntimeView,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 SolveSeed,
		int32 AttemptIndex = 0,
		FName BiomeRowName = NAME_None);

	/** Captures one continuation packet from a resolved connector candidate. */
	static FLayoutWorkerSolvePacket CaptureContinuation(
		const FString& DebugName,
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		uint64 StableConnectorKey,
		int32 AttemptIndex = 0);

	/** Captures one child packet from a frozen parent-negotiated child handoff. */
	static FLayoutWorkerSolvePacket CaptureChild(
		const FString& DebugName,
		const FLayoutWorldBindingRuntimeSnapshot& RuntimeSnapshot,
		const FLayoutChildSolveHandoff& ChildSolveHandoff,
		int32 SolveSeed,
		int32 AttemptIndex = 0);

	/** Returns true when this packet remains pointer-free and worker-safe. */
	bool ValidateNoLiveObjectCarriers(FString& OutFailureReason) const;
};
