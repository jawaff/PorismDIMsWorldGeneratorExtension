// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "Layout/Async/LayoutCertifiedChildWitnessArtifactProducer.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutRegionSchedulePlacementBridgeTypes.h"
#include "Misc/Optional.h"
/**
 * Private scaffold for the capability-backed recursive schedule-solver rewrite.
 *
 * Target feature set gathered from the active in-progress layout work:
 * - capability-backed child responsibility negotiation instead of local
 *   face/intention heuristics
 * - exact per-cell multi-cell module offer derivation from occupied local-cell
 *   contracts instead of one coarse module-wide six-face fallback
 * - residual parent planning that preserves only real route/support obligations
 * - explicit host vertical-access composition for `ParentOwned`, `ChildOwned`,
 *   and `Composed` contracts
 * - level-aware multi-entry and multi-level anchor negotiation
 * - parent/child and sibling seam planning, including mixed-height seam cases
 * - owner-side seam junction planning for the planned narrow same-level
 *   junction contract, including adjacency-class-aware commitments
 * - deterministic optional-child drop handling after structural commitments
 * - fixed proof-contract handoff into parent proof, child proof, deferred
 *   proof, and merged traversal audit
 * - closure/seam re-audit handoff and residual sparse placement handoff after
 *   structural proof
 * - request-owned shared cell metrics, root external endpoint commitments, and
 *   terrain/world-binding/root-placement context consumed as immutable inputs
 *   instead of profile-owned solver state
 * - deterministic diagnostics, proof records, and fixture/runtime parity
 * - thread-ready staging where whole-root solve dispatch and independent child
 *   proof jobs can be executed from immutable negotiated contracts later
 *
 * The replacement design is grouped into explicit feature stages:
 * - bootstrap the immutable root solve context
 * - compile child-demand capability summaries
 * - derive residual parent route/support summaries
 * - negotiate one parent/child responsibility set per demand
 * - plan seams, optional-child outcomes, and fixed proof contracts
 * - run parent proof, child proof, and final schedule audit against that
 *   contract
 *
 * These declarations intentionally model the rewrite around the features we
 * actually need instead of the current heuristic-heavy legacy flow.
 *
 * Durable design reference:
 * - `Docs/PorismExtension/LayoutDesign/RecursiveScheduleSolverDesign.md`
 *
 * Design source references:
 * - `Docs/InProgress/Layout/RecursiveRegionDataShapeProposal.md`
 * - `Docs/InProgress/Layout/RecursiveRegionAuthoringAssetModel.md`
 */
namespace LayoutRegionScheduleSolverFacade
{
	using LayoutRegionScheduleSolverPrivate::FSharedParentChildFace;

	struct FNegotiationDemandPlan;

	/** Feature toggles derived from one demand that affect negotiation behavior. */
	struct FNegotiationFeatureFlags
	{
		/** True when the demand needs more than one parent-facing contact. */
		bool bRequiresMultiEntryContactSet = false;

		/** True when the negotiated responsibility may bridge parent traversal through the child. */
		bool bAllowsChildTraversalBridge = false;

		/** True when this demand must negotiate anchors across more than one local level. */
		bool bRequiresMultiLevelAnchorSet = false;

		/** True when the demand participates in parent/child or sibling seam planning. */
		bool bRequiresSeamPlanning = false;

		/** True when the demand may be deterministically dropped as optional after commitments settle. */
		bool bOptionalDemand = false;

		/** True when this compiled slot may be satisfied by another direct-child source or residual parent module count. */
		bool bCountedProviderChoiceSlot = false;
	};

	/** Deterministic seed-stream bundle reserved for the major recursive scheduler branch families. */
	struct FDeterministicSeedStreams
	{
		/** Seed used for child-demand ordering and tie-breaks. */
		int32 ChildDemandSeed = 0;

		/** Seed used while ranking or deduplicating contact-backed placement families. */
		int32 ContactFamilySeed = 0;

		/** Seed used while bounded confirmation chooses between otherwise equivalent surviving families. */
		int32 ConfirmationSeed = 0;

		/** Seed used for seam, junction, and optional-child deterministic ordering. */
		int32 SeamAndOptionalSeed = 0;

		/** Seed used while ordering staged-terrain frontier unlocks and related ownership branches. */
		int32 TerrainStageSeed = 0;
	};

	/** Plain-data metadata that later queue/cache systems can use to publish or trace one root solve. */
	struct FQueueCachePublicationMetadata
	{
		/** Stable root solve identifier used by later queue/cache integrations. */
		FLayoutId RootSolveId;

		/** Stable root candidate identifier chosen by higher-level world-binding or direct-root request code. */
		FLayoutId RootCandidateId;

		/** Optional world-binding or request-policy identifier that selected this root solve. */
		FLayoutId RootPlacementPolicyId;

		/** Optional resolved continuation-selection contract chosen before solve execution for world-facing path alignment. */
		FLayoutResolvedWorldBindingContinuationSelection RootContinuationSelection;
	};

	/** Full occupied-cell consequence bundle compiled from one module or child capability offer. */
	struct FPlacementCapabilityBundle
	{
		/** Stable bundle identifier used by diagnostics, negotiation normalization, and later proof. */
		FLayoutId BundleId;

		/** True when the bundle's root anchor can satisfy a planned `VerticalAccess` cell directly. */
		bool bSupportsRootVerticalAccess = false;

		/** Parent-relative placement zone restriction preserved from the frozen module snapshot. */
		ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

		/** Parent-relative level placement restriction preserved from the frozen module snapshot. */
		ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

		/** Specific level used when `LevelPlacementPolicy` is `SpecificLevel`. */
		int32 SpecificLevel = 0;

		/** Occupied local cells claimed when this placement bundle is selected. */
		TArray<FIntVector> OccupiedLocalCells;

		/** Per-cell exposed-face rules carried by this placement bundle. */
		TArray<FLayoutLocalCellFaceRuleSnapshot> ExposedFaceRules;

		/** Covered local levels touched by this bundle. */
		TArray<int32> CoveredLevels;

		/** Internal traversal links carried by the module or child capability behind this bundle. */
		TArray<FLayoutInternalAccessLink> InternalAccessLinks;

		/** Cell-specific traversal bridges carried by canonical multi-cell shapes or glued composites. */
		TArray<FLayoutDerivedInternalTraversalLink> DerivedInternalTraversalLinks;

		/** Child or module-local cells that require supporting filled neighbors when this bundle is realized. */
		TArray<FIntVector> RequiredSupportCells;
	};


	/** One deterministic hard-open overlay compiled from the immutable root structural state. */
	struct FCompiledReservedOpenOverlay
	{
		/** Stable authored rule identifier carried forward for diagnostics and later structural consumers. */
		FName RuleId;

		/** Root-local cells this hard-open rule removes from the structural plan before later fill/quota work. */
		TArray<FIntVector> ReservedCells;
	};

	/** One terrain-driven local transition that later stepped solve must satisfy without flattening after search. */
	enum class ETerrainSteppedTransitionRootSupportStatus : uint8
	{
		MissingAdjacentVerticalAccessIntent,
		MissingRootVerticalAccessCandidate,
		SupportedByRootVerticalAccessCandidate
	};

	/** One terrain-driven local transition that later stepped solve must satisfy without flattening after search. */
	struct FTerrainSteppedTransitionRequirement
	{
		/** Root-local support cell on one side of the terrain step. */
		FIntVector FromCell = FIntVector::ZeroValue;

		/** Root-local support cell on the opposite side of the terrain step. */
		FIntVector ToCell = FIntVector::ZeroValue;

		/** Exact terrain height delta in blocks between the two support cells. */
		int32 StepHeightBlocks = 0;

		/** Exact designated-cell level delta between the two support cells after lattice snapping is applied. */
		int32 SnappedLevelDelta = 0;

		/** Planned root cells already marked `VerticalAccess` on either side of the required transition. */
		TArray<FIntVector> AdjacentVerticalAccessCells;

		/** Root vertical-access candidate cells proven by the frozen module snapshot set at the required transition edge. */
		TArray<FIntVector> SupportingRootVerticalAccessCandidateCells;

		/** Frozen root placement bundles that can realize the supporting vertical-access structure at this transition edge. */
		TArray<FLayoutId> SupportingRootVerticalAccessBundleIds;

		/** Deterministic first-pass root-side support classification for this required terrain transition. */
		ETerrainSteppedTransitionRootSupportStatus RootSupportStatus =
			ETerrainSteppedTransitionRootSupportStatus::MissingAdjacentVerticalAccessIntent;

		/** Exact root-local traversal cells that later stepped solve must keep connected for this transition. */
		TArray<FIntVector> RequiredTraversalCells;
	};

	/** Stage-local classification for one root cell after stepped support is projected onto the lattice. */
	struct FTerrainSteppedReachableSurfaceAnnotation
	{
		/** Root-local planned cell annotated for staged terrain negotiation. */
		FIntVector Cell = FIntVector::ZeroValue;

		/** Deterministic snapped terrain level derived from the support sample's upper lattice snap. */
		int32 SnappedTerrainLevel = 0;

		/** Raw sampled support surface preserved so diagnostics can explain the staged classification. */
		int32 SupportSurfaceZ = 0;

		/** True when this cell belongs to the initial lowest reachable plateau for the current staged contract. */
		bool bReachablePlateau = false;

		/** True when this cell is a higher snapped-level terrace deferred until an ascent frontier is satisfied. */
		bool bDeferredTerrace = false;
	};

	/** One deterministic terrain ascent edge and its protected support pocket annotations. */
	struct FTerrainSteppedAscentFrontierAnnotation
	{
		/** Root-local cell on one side of the terrain ascent frontier. */
		FIntVector FromCell = FIntVector::ZeroValue;

		/** Root-local cell on the other side of the terrain ascent frontier. */
		FIntVector ToCell = FIntVector::ZeroValue;

		/** Lower snapped terrain level participating in this ascent frontier. */
		int32 LowerSnappedTerrainLevel = 0;

		/** Higher snapped terrain level that remains blocked until this frontier is satisfied. */
		int32 HigherSnappedTerrainLevel = 0;

		/** Absolute snapped level delta copied from the existing stepped adjacency carrier. */
		int32 SnappedLevelDelta = 0;

		/** Existing planned vertical-access cells protected as the first ascent pocket for this frontier. */
		TArray<FIntVector> ProtectedAscentPocketCells;

		/** Deterministic ascent-frontier id derived from stable frontier geometry and ordering. */
		int32 AscentFrontierId = INDEX_NONE;
	};

	/** Compiled root-side structural inputs shared across all child-demand negotiations. */
	struct FCompiledStructuralInputs
	{
		/** Root planned cells normalized before downstream capability and residual stages inspect them. */
		TArray<FLayoutPlannedCell> RootPlannedCells;

		/** Root footprint paired with the normalized planned cells. */
		FIntPoint RootFootprintSize = FIntPoint::ZeroValue;

		/** Parent traversal cells that later residual subtraction must preserve. */
		TArray<FIntVector> RootProtectedTraversalCells;

		/** Root external endpoint commitments copied onto the immutable structural carrier. */
		TArray<FLayoutCommittedEndpointAnchor> RootExternalEndpointCommitments;

		/** Root stepped terrain support map copied onto the immutable structural carrier for later stepped-solve stages. */
		FLayoutSteppedTerrainSupportMap RootSteppedTerrainSupportMap;

		/** Frozen host alternatives retained so stepped support does not freeze a competing bundle insertion. */
		TArray<FLayoutVerticalAccessHostGroup> RootVerticalAccessHostGroups;

		/** Terrain-driven root transitions that later stepped solve must satisfy with explicit vertical-access-capable structure. */
		TArray<FTerrainSteppedTransitionRequirement> RootTerrainSteppedTransitionRequirements;

		/** Terrain-driven root transitions that the frozen root structural carrier cannot currently satisfy. */
		TArray<FTerrainSteppedTransitionRequirement> RootUnsupportedTerrainSteppedTransitionRequirements;

		/** Stage-local reachable/deferred cell annotations derived from the existing stepped support carrier. */
		TArray<FTerrainSteppedReachableSurfaceAnnotation> RootTerrainSteppedReachableSurfaceAnnotations;

		/** Stage-local ascent-frontier annotations derived from existing stepped transitions and route-support pockets. */
		TArray<FTerrainSteppedAscentFrontierAnnotation> RootTerrainSteppedAscentFrontierAnnotations;

		/** True when every compiled root terrain-step transition has first-pass root-side structural support. */
		bool bRootCanSatisfyTerrainSteppedTransitions = true;

		/** Profile-configured vertical-access count mode for capping root terrain stepped insertion plans. */
		ELayoutCountConstraintMode VerticalAccessCountMode = ELayoutCountConstraintMode::None;

		/** Profile-configured exact vertical-access count. */
		int32 ProfileVerticalAccessCount = 0;

		/** Profile-configured minimum vertical-access count for range mode. */
		int32 ProfileMinVerticalAccessCount = 0;

		/** Profile-configured maximum vertical-access count for range mode. */
		int32 ProfileMaxVerticalAccessCount = 0;

		/** Profile root seed used for deterministic count resolution in range mode. */
		int32 ProfileSeed = 0;

		/** Deterministic hard-open overlays compiled from the normalized root plan. */
		TArray<FCompiledReservedOpenOverlay> RootReservedOpenOverlays;


		/** Child demands compiled from the root request in deterministic order. */
		TArray<FNegotiationDemandPlan> Demands;

		/** Placement bundles compiled from the root/module side that can be reused across negotiations. */
		TArray<FPlacementCapabilityBundle> RootPlacementBundles;
	};

	/** Owner-side seam junction requirement emitted after seam ownership settles. */
	struct FOwnedSeamJunctionRequirement
	{
		/** Stable junction requirement identifier used by diagnostics and replay coverage. */
		FLayoutId JunctionRequirementId;

		/** Seam owner that must satisfy this junction contract. */
		FString OwnerRegionDebugPath;

		/** Region passively attaching as the terminating branch on the owner-side junction. */
		FString PassiveRegionDebugPath;

		/** Narrow adjacency-class identifier used by first-pass junction topology rules. */
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

		/** Owner-facing continuing-run direction used by the first-pass owner-side junction contract. */
		ELayoutFaceDirection ContinuingOwnerFaceDirection = ELayoutFaceDirection::PosX;

		/** Owner-facing terminating-branch direction used by the first-pass owner-side junction contract. */
		ELayoutFaceDirection BranchOwnerFaceDirection = ELayoutFaceDirection::PosX;
	};

	/** Ownership participation flags compiled for one child before responsibility negotiation begins. */
	struct FChildResponsibilityParticipation
	{
		/** True when the child can participate in a strict parent-owned host-ascent contract. */
		bool bSupportsParentOwnedHostAccess = false;

		/** True when the child can own the host ascent across the negotiated interface. */
		bool bSupportsChildOwnedHostAccess = false;

		/** True when the child can compose host ascent with retained parent providers. */
		bool bSupportsComposedHostAccess = false;

		/** True when the child can own a seam when seam planning selects it as the owner. */
		bool bSupportsSeamOwnership = false;

		/** True when the child can pass through deterministic optional-drop handling. */
		bool bSupportsOptionalDrop = false;
	};

	/** Lightweight per-level traversal summary compiled from child bundles and endpoint offers. */
	struct FChildLevelTraversalCapabilitySummary
	{
		/** Child-local level the summary describes. */
		int32 LevelIndex = 0;

		/** Traversal channels the child can expose on this level. */
		FGameplayTagContainer TraversalChannels;

		/** Stable anchor-pair ids that the child can generally connect through this level. */
		TArray<FLayoutId> ConnectableAnchorPairIds;

		/** True when the child can expose a host-facing ingress on this level. */
		bool bCanExposeHostIngress = false;

		/** True when the child can expose a host-facing egress on this level. */
		bool bCanExposeHostEgress = false;

		/** True when the child can carry vertical access through this level. */
		bool bCanCarryHostVerticalAccess = false;
	};

	/** Exact vertical-access proving cells grouped by the child-local levels they can support. */
	struct FChildVerticalAccessSupportBand
	{
		/** Child-local level that can be supported by the proving vertical-access cells. */
		int32 LevelIndex = 0;

		/** Exact proving vertical-access cells whose occupied-cell bundles cover this level. */
		TArray<FIntVector> ProvingVerticalAccessCells;
	};

	/** Exact seam offers grouped by the child-local levels that prove them. */
	struct FChildLevelSeamCapabilitySummary
	{
		/** Child-local level whose seam offers are summarized here. */
		int32 LevelIndex = 0;

		/** Exact seam offers proven on this child-local level. */
		TArray<FLayoutChildCapabilitySeam> SeamOffers;
	};

	/** Exact endpoint offers grouped by the child-local levels that prove them. */
	struct FChildLevelEndpointCapabilitySummary
	{
		/** Child-local level whose endpoint offers are summarized here. */
		int32 LevelIndex = 0;

		/** Exact endpoint offers proven on this child-local level. */
		TArray<FLayoutChildCapabilityEndpoint> EndpointOffers;
	};

	/** Exact span offers grouped by the child-local levels that prove them. */
	struct FChildLevelSpanCapabilitySummary
	{
		/** Child-local level whose span offers are summarized here. */
		int32 LevelIndex = 0;

		/** Exact span offers proven on this child-local level. */
		TArray<FLayoutChildCapabilitySpan> SpanOffers;
	};

	/** Root external endpoint reachability summary compiled from one residual parent view. */
	struct FResidualExternalEndpointReachability
	{
		/** Stable committed endpoint id when one is available. */
		FLayoutId CommitmentId;

		/** Parent component that currently owns the reachable endpoint, when known. */
		int32 ParentComponentId = INDEX_NONE;

		/** True when the endpoint is still reachable from the residual root-connected parent graph. */
		bool bReachableFromResidualParent = false;
	};

	/** Seam- or junction-relevant surviving boundary span classification in a residual parent summary. */
	struct FResidualBoundarySpanClassification
	{
		/** Stable classification id used by diagnostics and later seam planning. */
		FLayoutId ClassificationId;

		/** Covered residual-parent level for this boundary span classification. */
		int32 LevelIndex = 0;

		/** Parent-local cells that participate in this classified boundary span. */
		TArray<FIntVector> Cells;

		/** Adjacency class used when junction or seam topology rules apply. */
		FLayoutId AdjacencyClassId;

		/** True when the span remains relevant to seam ownership. */
		bool bRelevantToSeamOwnership = false;

		/** True when the span remains relevant to junction ownership. */
		bool bRelevantToJunctionOwnership = false;
	};

	/** Immutable root-level context shared by the replacement recursive scheduler stages. */
	struct FRecursiveScheduleSolveContext
	{
		/** Root request currently being scheduled. */
		FLayoutRegionSolveRequest RootRequest;

		/** Root external endpoint commitments that the recursive schedule must preserve. */
		TArray<FLayoutCommittedEndpointAnchor> RootExternalEndpointCommitments;

		/** Deterministic seed streams reserved for the major branch families. */
		FDeterministicSeedStreams SeedStreams;

		/** Request-owned bounded child-proof concurrency limit reserved for later async work. */
		int32 MaxConcurrentChildProofJobs = 1;

		/** Queue/cache metadata copied from the higher-level request or planning context. */
		FQueueCachePublicationMetadata PublicationMetadata;

		/** Shared cell size normalized from the request-carried snapshots for later world-binding and realization context. */
		FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

		/** Request-owned authored-block Z offset that later preview and realization must preserve. */
		int32 TemplatePlacementZOffsetBlocks = 0;

		/** World-facing placement kind chosen before solve execution. */
		ELayoutWorldBindingPlacementKind RootPlacementKind = ELayoutWorldBindingPlacementKind::None;

		/** World-binding-owned placement policy that later realization must preserve. */
		FLayoutWorldBindingPlacementPolicy WorldBindingPlacementPolicy;

		/** Request-owned stepped terrain support map preserved for later stepped-solve leaf stages. */
		FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

		/** Root-level parent planned cells used to derive the residual parent plan. */
		TArray<FLayoutPlannedCell> ParentPlannedCells;

		/** Root-level parent footprint used by residual planning and later proof. */
		FIntPoint ParentFootprintSize = FIntPoint::ZeroValue;

		/** Parent traversal cells that child reservations must not destroy. */
		TSet<FIntVector> ProtectedParentTraversalCells;

		/** Parent ingress directions that matter when preserving host-owned ascent. */
		TSet<ELayoutFaceDirection> ParentVerticalAccessIngressDirections;
	};

	/** Immutable child-side summary used during responsibility negotiation. */
	struct FChildCapabilitySummary
	{
		/** Child region path the summary belongs to. */
		FString ChildRegionDebugPath;

		/** Rotated capability envelope already proven from the selected planning variant. */
		FLayoutChildCapabilityEnvelope CapabilityEnvelope;

		/** Parent-facing endpoint offers grouped by level-aware child entry/face opportunities. */
		TArray<FLayoutChildCapabilityEndpoint> EndpointOffers;

		/** Parent-facing seam offers grouped by the selected child capability envelope. */
		TArray<FLayoutChildCapabilitySeam> SeamOffers;

		/** Parent-facing boundary span offers grouped by the selected child capability envelope. */
		TArray<FLayoutChildCapabilitySpan> SpanOffers;

		/** Placement bundles compiled from the child capability envelope. */
		TArray<FPlacementCapabilityBundle> PlacementBundles;

		/** Stable ids of child anchor-band pairs that are generally connectable without proving the exact final route. */
		TArray<FLayoutId> GenerallyConnectableAnchorPairIds;

		/** Lightweight per-level traversal summaries derived from endpoint offers and placement bundles. */
		TArray<FChildLevelTraversalCapabilitySummary> TraversalSummariesByLevel;

		/** Exact endpoint offers grouped by the child-local levels that prove them. */
		TArray<FChildLevelEndpointCapabilitySummary> EndpointOffersByLevel;

		/** Exact seam offers grouped by the child-local levels that prove them. */
		TArray<FChildLevelSeamCapabilitySummary> SeamOffersByLevel;

		/** Exact span offers grouped by the child-local levels that prove them. */
		TArray<FChildLevelSpanCapabilitySummary> SpanOffersByLevel;

		/** Exact vertical-access support bands grouped by supported child-local level. */
		TArray<FChildVerticalAccessSupportBand> VerticalAccessSupportBandsByLevel;

		/** Vertical-access cells the child can realize if it owns or composes host ascent. */
		TArray<FIntVector> VerticalAccessCells;

		/** Ownership and participation flags compiled before negotiation begins. */
		FChildResponsibilityParticipation ResponsibilityParticipation;

		/** Parent-owned faces shared with the child, compiled before negotiation.
		 Used for level-aware retained parent shell and proof-only host-ascent evidence
		 during contract freeze, so upper-level seam data survives contract formation. */
		TArray<FSharedParentChildFace> SharedParentChildFaces;
	};

	/** Terrain-stage activation state carried on one authored child demand without rewriting the authored set itself. */
	enum class ETerrainStageEligibility : uint8
	{
		Active,
		Deferred
	};

	/** One staged child demand the replacement scheduler will negotiate independently. */
	struct FNegotiationDemandPlan
	{
		/** Child region path for this demand. */
		FString ChildRegionDebugPath;

		/** Child request that later proof will solve if negotiation succeeds. */
		FLayoutRegionSolveRequest ChildRequest;

		/** Capability-backed child summary compiled from the selected planning variant. */
		FChildCapabilitySummary ChildSummary;

		/** Parent cells reserved by the negotiated child footprint. */
		TArray<FIntVector> ReservedParentCells;

		/** Derived feature flags that shape the negotiation stages for this demand. */
		FNegotiationFeatureFlags FeatureFlags;

		/** Authored parent-relative placement zone preserved so terrain staging can gate demand activation without rewriting the authored set. */
		ELayoutPlacementZone ParentPlacementZone = ELayoutPlacementZone::Any;

		/** Authored parent-relative level policy preserved so terrain staging can gate demand activation without rewriting the authored set. */
		ELayoutLevelPlacementPolicy ParentLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

		/** Specific parent-relative level preserved when ParentLevelPlacementPolicy is SpecificLevel. */
		int32 ParentSpecificLevel = 0;

		/** Terrain-stage activation state for this authored child demand. */
		ETerrainStageEligibility TerrainStageEligibility = ETerrainStageEligibility::Active;

		/** True when this authored demand currently overlaps deferred terrain only and should stay inactive until a later stage unlocks it. */
		bool bTerrainStageDeferred = false;

		/** True when this authored demand must remain inactive until an ascent frontier explicitly unlocks it. */
		bool bRequiresDeferredActivation = false;

		/** True when this deferred demand may activate early only if it proves the current ascent unlock itself. */
		bool bTerrainStageCanActivateEarlyIfUnlocksAscent = false;

		/** True when the current ascent frontier exposes protected ascent-pocket cells that overlap this demand's authored placement scope. */
		bool bTerrainStageActivatesOnCurrentFrontier = false;

		/** Optional current frontier key that explicitly unlocks this deferred demand for the active terrain stage. */
		TOptional<int32> UnlockingAscentFrontierId;

		/** Lowest reachable snapped terrain level overlapping this demand's authored placement scope, or INDEX_NONE when none overlap. */
		int32 TerrainStageMinimumReachableLevel = INDEX_NONE;

		/** Lowest deferred snapped terrain level overlapping this demand's authored placement scope, or INDEX_NONE when none overlap. */
		int32 TerrainStageMinimumDeferredLevel = INDEX_NONE;
	};

	/** Residual parent-side summary used to judge routeability before full proof. */
	struct FResidualParentCapabilitySummary
	{
		/** Residual parent planned cells after child reservations are applied. */
		TArray<FLayoutPlannedCell> PlannedCells;

		/** Parent entry/root-connected corridor cells available before child proof bridges are used. */
		TSet<FIntVector> RootConnectedTraversableCells;

		/** Deterministic residual parent component ids for root-connected traversable cells. */
		TMap<FIntVector, int32> RootConnectedComponentIdByCell;

		/** Exact root-connected traversable faces preserved on each surviving parent cell. */
		TMap<FIntVector, TArray<ELayoutFaceDirection>> RootConnectedTraversableFacesByCell;

		/** Exact representative cells for the retained counted parent provider groups. */
		TArray<FIntVector> CountedParentVerticalAccessCells;

		/** Retained parent vertical-access cells preserved only for route-support or child handoff traversal. */
		TArray<FIntVector> RouteSupportVerticalAccessCells;

		/** Support provenance identifiers for parent cells that survive reservation subtraction, including same-region and bundle-forwarded child support. */
		TMap<FIntVector, FLayoutId> SupportProvenanceByCell;

		/** Parent-local support cells the reserved child occupied-cell footprint still requires beneath its bottom supported cells. */
		TArray<FIntVector> RequiredChildBundleSupportCells;

		/** Required child-bundle support cells that are missing after reservation subtraction and parent support culling. */
		TArray<FIntVector> MissingRequiredChildBundleSupportCells;

		/** Root external endpoints and whether they remain reachable after reservation subtraction. */
		TArray<FResidualExternalEndpointReachability> ExternalEndpointReachability;


		/** Parent cells that remain structurally relevant to seam or junction ownership after residual filtering. */
		TArray<FIntVector> SeamRelevantBoundaryCells;

		/** Seam/junction-relevant surviving boundary spans derived from the residual parent state. */
		TArray<FResidualBoundarySpanClassification> BoundarySpanClassifications;
	};

	/** One parent-facing capability offer that can seed contact-set negotiation. */
	struct FCommittedParentContactFace
	{
		/** Parent-local cell that must keep this contact face traversable. */
		FIntVector Cell = FIntVector::ZeroValue;

		/** Exact face direction that must remain available on that cell. */
		ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;
	};

	/** One parent-facing capability offer that can seed contact-set negotiation. */
	struct FParentContactCapabilityCandidate
	{
		/** Normalized family identifier used to group related parent-contact opportunities. */
		FLayoutId FamilyId;

		/** Child endpoint offer this parent contact would satisfy. */
		FLayoutCommittedEndpointAnchor EndpointCommitment;

		/** Parent cell that would realize the commitment. */
		FIntVector ParentContactCell = FIntVector::ZeroValue;

		/** Exact residual parent face that must stay traversable on that cell. */
		ELayoutFaceDirection ParentContactFaceDirection = ELayoutFaceDirection::PosX;

		/** Covered level index for the committed contact. */
		int32 LevelIndex = 0;

		/** Root-connected parent component identity for the contact cell when known. */
		int32 ParentComponentId = INDEX_NONE;

		/** Narrow adjacency-class identifier used when seam or junction topology matters. */
		FLayoutId AdjacencyClassId;

		/** Full placement consequence bundle that this parent-contact opportunity would commit. */
		FPlacementCapabilityBundle PlacementBundle;

		/** All committed parent-side traversal faces or anchors implied by this contact opportunity. */
		TArray<FLayoutCommittedTraversalAnchor> AllCommittedContactTraversalAnchors;

		/** Parent-facing support score for the committed face. */
		int32 ParentSupportScore = 0;
	};

	/** One contact-backed placement family chosen as a set, not local independent contacts. */
	struct FContactBackedPlacementFamily
	{
		/** Stable family identifier used for normalization and bounded confirmation. */
		FLayoutId FamilyId;

		/** Child endpoint commitments selected for this negotiated set. */
		TArray<FLayoutCommittedEndpointAnchor> EndpointCommitments;

		/** Parent cells that realize those commitments. */
		TArray<FIntVector> ParentContactCells;

		/** Exact parent-local faces that realize those commitments. */
		TArray<FCommittedParentContactFace> ParentContactFaces;

		/** Full placement consequence bundle carried by this family. */
		FPlacementCapabilityBundle PlacementBundle;

		/** All committed parent-side traversal faces or anchors implied by this family. */
		TArray<FLayoutCommittedTraversalAnchor> AllCommittedContactTraversalAnchors;

		/** Root-connected parent component identity for the chosen family when known. */
		int32 ParentComponentId = INDEX_NONE;

		/** Narrow adjacency-class identifier when seam or junction topology matters. */
		FLayoutId AdjacencyClassId;

		/** Smaller parent-only ingress subset that must be reachable before child proof can bridge. */
		TArray<FLayoutCommittedTraversalAnchor> ParentTraversalIngressAnchors;

		/** True when merged traversal may bridge through the child after parent ingress is satisfied. */
		bool bAllowsChildTraversalBridge = false;
	};

	using FNegotiatedContactSet = FContactBackedPlacementFamily;

	/** Publishable bounded parent-proof evidence fixed during negotiation confirmation for one chosen family. */
	struct FBoundedParentProofEvidence
	{
		/** Child region path the confirmed evidence belongs to. */
		FString ChildRegionDebugPath;

		/** Residual parent component that survived bounded confirmation for this family. */
		int32 ParentComponentId = INDEX_NONE;

		/** Exact parent-local route-seed cells on the surviving component that justified routeability. */
		TArray<FIntVector> ConfirmedParentRouteSeedCells;

		/** Exact parent-local contact cells confirmed on the surviving component. */
		TArray<FIntVector> ConfirmedParentContactCells;

		/** Exact parent-local ingress cells confirmed before any child bridge is used. */
		TArray<FIntVector> ConfirmedParentTraversalIngressCells;

		/** Exact parent-local counted provider cells that the confirmed contract still depends on. */
		TArray<FIntVector> ConfirmedCountedParentProviderCells;

		/** Exact parent-local support cells that remained available beneath the reserved child bundle. */
		TArray<FIntVector> ConfirmedRequiredChildBundleSupportCells;

		/** Root external endpoint commitments that remained reachable under the confirmed residual parent state. */
		TArray<FLayoutId> PreservedRootExternalEndpointCommitmentIds;
	};

	/** Final negotiated responsibility set carried forward into later proof stages. */
	struct FNegotiatedResponsibilitySet
	{
		/** Contact family committed for the child placement. */
		FNegotiatedContactSet ContactSet;

		/** Vertical-access ownership chosen for the placement. */
		FLayoutNegotiatedChildResponsibilityContract ResponsibilityContract;

		/** Publishable bounded parent-proof evidence gathered when confirmation succeeds. */
		FBoundedParentProofEvidence BoundedParentProofEvidence;
	};

	/** Exact child capability ids selected while accepting one child demand. */
	struct FNegotiatedChildCapabilityWitness
	{
		/** Source content entry that produced the child demand. */
		FName SourceContentEntryId;

		/** Stable child id/path that parent negotiation accepted. */
		FString ChildRegionDebugPath;

		/** Exact reciprocal boundary-domain witness ids selected before regional proof. */
		TArray<FLayoutId> SelectedBoundaryWitnessIds;

		/** Exact endpoint capability ids selected for committed parent-child anchors. */
		TArray<FLayoutId> SelectedEndpointCapabilityIds;

		/** Exact traversal capability ids selected for parent/child route handoff. */
		TArray<FLayoutId> SelectedTraversalCapabilityIds;

		/** Exact vertical capability ids selected for child-owned or composed ascent. */
		TArray<FLayoutId> SelectedVerticalCapabilityIds;

		/** Exact closure span capability id selected for delegated closure, when unique. */
		FLayoutId SelectedClosureSpanCapabilityId;

		/** Exact seam capability id selected for sibling seam ownership or passive acceptance. */
		FLayoutId SelectedSeamCapabilityId;

		/** Exact seam witness id selected by parent negotiation. */
		FLayoutId SelectedSeamWitnessId;
	};

	/** Exact delegated obligations and witness ids certified for one child demand. */
	struct FNegotiatedChildObligationWitness
	{
		/** Parent feature requirement ids delegated to this child. */
		TArray<FLayoutId> DelegatedFeatureRequirementIds;

		/** Parent closure requirement ids delegated to this child. */
		TArray<FLayoutId> DelegatedClosureRequirementIds;

		/** Sibling seam witness ids delegated to this child. */
		TArray<FLayoutId> DelegatedSeamWitnessIds;

		/** Junction witness ids delegated to this child. */
		TArray<FLayoutId> DelegatedJunctionWitnessIds;

		/** Host vertical-access witness ids delegated to this child. */
		TArray<FLayoutId> DelegatedHostVerticalAccessWitnessIds;

		/** Route constraint ids delegated to or preserved through this child. */
		TArray<FLayoutId> DelegatedRouteConstraintIds;

		/** Exact parent validation assertions used to certify this child demand. */
		TArray<FLayoutId> ParentAssertionSubsetIds;

		/** Exact child validation assertions used to certify this child demand. */
		TArray<FLayoutId> ChildAssertionSubsetIds;
	};

	/** Negotiation output for one child demand before parent/child proof execution. */
	struct FNegotiatedDemandResult
	{
		/**
		 * Legacy child-provider facts still kept for test scaffolding and ad hoc
		 * diagnostics. Proof-side ownership packaging should derive semantics from
		 * the negotiated responsibility contract instead of this coarse summary.
		 */
		struct FProofChildVerticalAccessProvider
		{
			/** True when the negotiated child can realize local vertical-access cells at all. */
			bool bSupportsVerticalAccess = false;

			/** True when the authored child entry was marked as host-contributing before negotiation. */
			bool bContributesHostVerticalAccess = false;

			/** Child-local vertical-access cells proven by the selected planning variant. */
			TArray<FIntVector> VerticalAccessLocalCells;

			/** Child region path this provider summary belongs to. */
			FString ChildRegionDebugPath;
		};

		/** Child region path the result belongs to. */
		FString ChildRegionDebugPath;

		/** Snapshot-backed child proof request carried forward after responsibility negotiation succeeds. */
		FLayoutRegionSolveRequest ChildRequest;

		/** Final negotiated responsibility set for the child demand. */
		FNegotiatedResponsibilitySet ResponsibilitySet;

		/** Exact selected child capability witnesses captured at negotiation time. */
		FNegotiatedChildCapabilityWitness CapabilityWitness;

		/** Exact delegated obligation witnesses captured at negotiation time. */
		FNegotiatedChildObligationWitness ObligationWitness;

		/** Residual parent summary that justified the chosen responsibility set. */
		FResidualParentCapabilitySummary ParentSummary;

		/** Exact stage/transform authority frozen with this negotiated child value. */
		FLayoutChildStageMappingResult StageMapping;

		/** Stable transform-owned reciprocal boundary-domain certificate. */
		FLayoutId BoundaryCertificateId;

		/** Child-local incoming boundaries frozen before parent proof. */
		TArray<FLayoutSolveBoundaryPoint> CertifiedChildIncomingBoundaryPoints;

		/** Exact child boundary domains frozen before child proof. */
		TArray<FLayoutCellCandidateDomainRestriction> ChildDomainRestrictions;

		/** Exact direct-child zone-feature commitments frozen with selected provider and transform. */
		TArray<FLayoutZoneFeatureProviderCommitment> ZoneFeatureProviderCommitments;

		/** Worker/result-local parent-branch certificate emitted from exact negotiation-time witnesses. */
		FLayoutProducedParentBranchCertificateArtifact ParentBranchCertificate;

		/** Legacy coarse provider facts retained only for compatibility/debug scaffolding. */
		FProofChildVerticalAccessProvider ChildVerticalAccessProvider;

		/** True when the demand negotiated successfully. */
		bool bSucceeded = false;

		/** Failure detail if the demand could not negotiate a valid responsibility set. */
		FString FailureReason;
	};

	/** Seam-planning output that later proof and closure audits consume. */
	struct FNegotiatedSeamPlan
	{
		/** Parent/child and sibling seam records committed during schedule negotiation. */
		TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams;

		/** True when the seam plan intentionally represents the full authoritative seam set, including an explicit empty set. */
		bool bPlannedPartitionSeamsAreAuthoritative = false;

		/** Owner-side narrow junction requirements emitted after seam ownership settles. */
		TArray<FOwnedSeamJunctionRequirement> JunctionRequirements;
	};

	/** Optional-child outcome records decided after structural commitments settle. */
	struct FOptionalChildDecisionPlan
	{
		/** Optional children kept in the negotiated schedule. */
		TArray<FString> KeptChildRegionDebugPaths;

		/** Optional children deterministically dropped from the negotiated schedule. */
		TArray<FLayoutDroppedOptionalChildRecord> DroppedOptionalChildren;
	};

	/** Deterministic first-pass rejection summary built from compiled root stepped-terrain structural inputs. */
	struct FTerrainSteppedRejectionSummary
	{
		/** True when at least one compiled terrain-step transition is structurally unsupported on the root side. */
		bool bHasUnsupportedTransition = false;

		/** First deterministic unsupported transition when root structural support is incomplete. */
		FTerrainSteppedTransitionRequirement UnsupportedTransition;

		/** Stable formatted rejection reason describing why the first unsupported transition cannot currently be satisfied. */
		FString FailureReason;
	};

	/** Deterministic first-pass accepted support choice for one root stepped-terrain transition. */
	struct FTerrainSteppedSupportPlan
	{
		/** Supported terrain transition this plan satisfies. */
		FTerrainSteppedTransitionRequirement TransitionRequirement;

		/** Deterministically selected planned `VerticalAccess` cell that the chosen support bundle proves for this transition. */
		FIntVector SelectedSupportingRootVerticalAccessCell = FIntVector::ZeroValue;

		/** Deterministically selected frozen root placement bundle that can satisfy the transition. */
		FLayoutId SelectedRootVerticalAccessBundleId;
	};

	/** Deterministic pre-search insertion plan for one supported root stepped-terrain transition. */
	struct FTerrainSteppedInsertionPlan
	{
		/** Accepted support choice this insertion plan materializes. */
		FTerrainSteppedSupportPlan SupportPlan;

		/** Planned root cell where the selected support bundle is anchored for later stepped solve consumption. */
		FIntVector BundleAnchorCell = FIntVector::ZeroValue;

		/** Frozen root placement bundle that later stepped solve should use for the anchored support insertion. */
		FPlacementCapabilityBundle SelectedRootPlacementBundle;
	};

	/** Frozen stepped-terrain solve contract built from immutable root structural inputs before coordinator consumption. */
	struct FTerrainSteppedPreparedSolveContract
	{
		/** True when every compiled terrain-step transition has root-side structural support. */
		bool bRootCanSatisfyTerrainSteppedTransitions = true;

		/** Stable first unsupported transition summary when the root cannot satisfy every terrain step yet. */
		FTerrainSteppedRejectionSummary RejectionSummary;

		/** Deterministic accepted support choices for the terrain-step transitions that already have root-side support. */
		TArray<FTerrainSteppedSupportPlan> SupportPlans;

		/** Deterministic anchored support insertions for the terrain-step transitions that already have root-side support. */
		TArray<FTerrainSteppedInsertionPlan> InsertionPlans;

		/** Deterministic route-facing constraints normalized from every compiled terrain-step transition. */
		TArray<FLayoutRouteConstraintRecord> RouteConstraints;
	};

	/** Thread-ready proof job plan built from immutable negotiated contracts. */
	struct FProofJobPlan
	{
		/** Parent proof request that owns the residual parent route/modules. */
		FLayoutRegionSolveRequest ParentRequest;

		/** Child proof requests that can eventually run independently once threading is enabled. */
		TArray<FLayoutRegionSolveRequest> ChildRequests;

		/** Queue/cache metadata preserved so future worker dispatch keeps the same publishable root identity. */
		FQueueCachePublicationMetadata PublicationMetadata;

		/** Frozen root stepped-terrain contract preserved so future proof execution can consume the same prepared carrier without rediscovery. */
		FTerrainSteppedPreparedSolveContract RootTerrainSteppedPreparedSolveContract;

		/** Stable child merge ordering by region debug path or committed child id. */
		TArray<FString> StableMergeOrder;

		/** Bounded concurrency policy for later child-proof parallelism. */
		int32 MaxConcurrentChildProofJobs = 1;

		/** Worker/result-local parent-branch certificates carried from negotiation into child dispatch planning. */
		TArray<FLayoutProducedParentBranchCertificateArtifact> ParentBranchCertificates;
	};

	/** Fixed proof contract handed from negotiation into parent/child proof stages. */
	struct FNegotiatedProofScheduleContract
	{
		/** Parent request to solve after residual derivation and committed-interface export. */
		FLayoutRegionSolveRequest ParentRequest;

		/** Child requests to solve once negotiation commits their responsibilities. */
		TArray<FLayoutRegionSolveRequest> ChildRequests;

		/** Committed direct child interfaces that later proof must realize exactly. */
		TArray<FLayoutDirectChildRegionCommitment> DirectChildCommitments;

		/** Root external endpoint obligations that later proof and audit must preserve. */
		TArray<FLayoutCommittedEndpointAnchor> RootExternalEndpointCommitments;

		/** Negotiated child responsibility contracts visible to later proof and diagnostics. */
		TArray<FLayoutNegotiatedChildResponsibilityContract> ResponsibilityContracts;

		/** Publishable bounded parent-proof evidence records for each confirmed negotiated child demand. */
		TArray<FBoundedParentProofEvidence> BoundedParentProofEvidenceRecords;

		/** Worker/result-local parent-branch certificates emitted from exact negotiation-time witnesses. */
		TArray<FLayoutProducedParentBranchCertificateArtifact> ParentBranchCertificates;

		/** Exact recursive vertical-access ownership summary frozen before proof starts. */
		FLayoutRecursiveVerticalAccessSummary PlannedRecursiveVerticalAccessSummary;

		/** Committed seam plan that later proof and closure audits consume. */
		FNegotiatedSeamPlan SeamPlan;

		/** Optional-child outcomes that later merged results must preserve. */
		FOptionalChildDecisionPlan OptionalChildPlan;

		/** Request-owned concurrency limit reserved for later child-proof queueing. */
		int32 MaxConcurrentChildProofJobs = 1;

		/** Queue/cache metadata copied from the higher-level request context. */
		FQueueCachePublicationMetadata PublicationMetadata;

		/** Frozen root stepped-terrain contract handed forward intact from the coordinator boundary for later proof/execution consumption. */
		FTerrainSteppedPreparedSolveContract RootTerrainSteppedPreparedSolveContract;
	};

	/** Final replacement-scheduler result before it is wired into the public facade. */
	struct FCapabilityBackedScheduleResult
	{
		/** True when the replacement schedule path negotiated and proved the root successfully. */
		bool bSucceeded = false;

		/** Failure detail when negotiation or proof fails. */
		FString FailureReason;

		/** Final public schedule result emitted by the coordinator-owned recursive path. */
		FLayoutRegionSolveScheduleResult ScheduleResult;

		/** Negotiated proof contract used for the final parent/child proof. */
		FNegotiatedProofScheduleContract ProofContract;
	};

	// Bootstrap and context
	PORISMDIMSWORLDGENERATOREXTENSION_API FRecursiveScheduleSolveContext BuildSolveContext(const FLayoutRegionSolveRequest& RootRequest);
	FNegotiationFeatureFlags BuildNegotiationFeatureFlags(
		const FLayoutRegionSolveRequest& ChildRequest,
		const FChildCapabilitySummary& ChildSummary);

	// Child capability compilation
	PORISMDIMSWORLDGENERATOREXTENSION_API FCompiledStructuralInputs BuildCompiledStructuralInputs(
		const FRecursiveScheduleSolveContext& SolveContext);
	/** Builds one deterministic first-pass stepped-terrain rejection summary from immutable compiled structural inputs. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FTerrainSteppedRejectionSummary BuildRootTerrainSteppedRejectionSummary(
		const FCompiledStructuralInputs& StructuralInputs);
	/** Builds deterministic accepted root support plans for terrain-step transitions that already have root-side structural support. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FTerrainSteppedSupportPlan> BuildRootTerrainSteppedSupportPlans(
		const FCompiledStructuralInputs& StructuralInputs);
	/** Builds deterministic anchored root support insertions from the accepted terrain-step support plans. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FTerrainSteppedInsertionPlan> BuildRootTerrainSteppedInsertionPlans(
		const FCompiledStructuralInputs& StructuralInputs);
	/** Builds deterministic route-constraint records from compiled root stepped-terrain transitions. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FLayoutRouteConstraintRecord> BuildRootTerrainSteppedRouteConstraints(
		const FCompiledStructuralInputs& StructuralInputs);
	/** Builds one frozen stepped-terrain solve contract from immutable root structural inputs for later recursive legality consumption. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FTerrainSteppedPreparedSolveContract BuildRootTerrainSteppedPreparedSolveContract(
		const FCompiledStructuralInputs& StructuralInputs);
	/** Builds a normalized child-capability envelope directly from one frozen solve request. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutChildCapabilityEnvelope BuildChildCapabilityEnvelopeFromRequest(
		const FLayoutRegionSolveRequest& Request);
	FChildCapabilitySummary BuildChildCapabilitySummary(
		const FString& ChildRegionDebugPath,
		const FLayoutChildCapabilityEnvelope& CapabilityEnvelope,
		const TArray<FIntVector>& VerticalAccessCells,
		bool bSupportsHostVerticalAccess,
		bool bSupportsOptionalDrop,
		const TArray<FPlacementCapabilityBundle>& PlacementBundles = TArray<FPlacementCapabilityBundle>(),
		const TArray<FSharedParentChildFace>& SharedParentChildFaces = TArray<FSharedParentChildFace>());
	FNegotiationDemandPlan BuildNegotiationDemandPlan(
		const FString& ChildRegionDebugPath,
		const FLayoutRegionSolveRequest& ChildRequest,
		const FChildCapabilitySummary& ChildSummary,
		const TArray<FIntVector>& ReservedParentCells,
		ELayoutPlacementZone ParentPlacementZone = ELayoutPlacementZone::Any,
		ELayoutLevelPlacementPolicy ParentLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel,
		int32 ParentSpecificLevel = 0,
		ETerrainStageEligibility TerrainStageEligibility = ETerrainStageEligibility::Active,
		bool bRequiresDeferredActivation = false,
		bool bTerrainStageCanActivateEarlyIfUnlocksAscent = false,
		bool bTerrainStageActivatesOnCurrentFrontier = false,
		TOptional<int32> UnlockingAscentFrontierId = TOptional<int32>(),
		int32 TerrainStageMinimumReachableLevel = INDEX_NONE,
		int32 TerrainStageMinimumDeferredLevel = INDEX_NONE);
	TArray<FNegotiationDemandPlan> BuildNegotiationDemandPlans(
		const FRecursiveScheduleSolveContext& SolveContext);
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FPlacementCapabilityBundle> BuildModulePlacementBundles(
		const FLayoutRegionSolveRequest& Request);

	// Residual parent analysis
	inline bool IsNegotiationDemandActiveForCurrentTerrainStage(const FNegotiationDemandPlan& DemandPlan)
	{
		if (!DemandPlan.bRequiresDeferredActivation)
		{
			return true;
		}

		return DemandPlan.TerrainStageEligibility == ETerrainStageEligibility::Deferred
			&& DemandPlan.bTerrainStageCanActivateEarlyIfUnlocksAscent
			&& DemandPlan.bTerrainStageActivatesOnCurrentFrontier
			&& DemandPlan.UnlockingAscentFrontierId.IsSet();
	}

	PORISMDIMSWORLDGENERATOREXTENSION_API FResidualParentCapabilitySummary BuildResidualParentCapabilitySummary(
		const TArray<FLayoutPlannedCell>& ParentPlannedCells,
		const TSet<FIntVector>& RootConnectedTraversableCells,
		const TArray<FIntVector>& CountedParentVerticalAccessCells);
	PORISMDIMSWORLDGENERATOREXTENSION_API FResidualParentCapabilitySummary RebuildResidualParentSummaryForDemand(
		const FCompiledStructuralInputs& StructuralInputs,
		const FNegotiationDemandPlan& DemandPlan);
	PORISMDIMSWORLDGENERATOREXTENSION_API FResidualParentCapabilitySummary RebuildResidualParentSummaryForDemand(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiationDemandPlan& DemandPlan);

	// Responsibility negotiation
	struct FLayoutResponsibilityNegotiator
	{
		static TArray<FParentContactCapabilityCandidate> CollectParentContactCapabilityCandidates(
			const FChildCapabilitySummary& ChildSummary,
			const FResidualParentCapabilitySummary& ParentSummary);
		static TArray<FContactBackedPlacementFamily> BuildPlacementFamilies(
			const FNegotiationDemandPlan& DemandPlan,
			const FResidualParentCapabilitySummary& ParentSummary);
		static TArray<FContactBackedPlacementFamily> CollectCapabilityBackedContactSets(
			const FChildCapabilitySummary& ChildSummary,
			const FResidualParentCapabilitySummary& ParentSummary);
		static void SelectParentTraversalIngressSubset(
			const FResidualParentCapabilitySummary& ParentSummary,
			FContactBackedPlacementFamily& InOutContactSet);
		static FNegotiatedResponsibilitySet BuildResponsibilitySet(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FNegotiationDemandPlan& DemandPlan,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FContactBackedPlacementFamily& ContactSet);
		static FNegotiatedDemandResult Negotiate(
			const FRecursiveScheduleSolveContext& SolveContext,
			const FNegotiationDemandPlan& DemandPlan,
			const FResidualParentCapabilitySummary& ParentSummary);
	};

	struct FLayoutResponsibilityConfirmation
	{
		static bool Confirm(
			const FLayoutRegionSolveRequest& RootRequest,
			const FResidualParentCapabilitySummary& ParentSummary,
			const FChildCapabilitySummary& ChildSummary,
			FNegotiatedResponsibilitySet& InOutResponsibilitySet,
			FString& OutFailureReason);
	};

	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FParentContactCapabilityCandidate> CollectParentContactCapabilityCandidates(
		const FChildCapabilitySummary& ChildSummary,
		const FResidualParentCapabilitySummary& ParentSummary);
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FContactBackedPlacementFamily> BuildNegotiatedPlacementFamilies(
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary);
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FContactBackedPlacementFamily> CollectCapabilityBackedContactSets(
		const FChildCapabilitySummary& ChildSummary,
		const FResidualParentCapabilitySummary& ParentSummary);
	PORISMDIMSWORLDGENERATOREXTENSION_API void SelectParentTraversalIngressSubset(
		const FResidualParentCapabilitySummary& ParentSummary,
		FContactBackedPlacementFamily& InOutContactSet);
	PORISMDIMSWORLDGENERATOREXTENSION_API FNegotiatedResponsibilitySet BuildResponsibilitySetFromContactSet(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FContactBackedPlacementFamily& ContactSet);
	PORISMDIMSWORLDGENERATOREXTENSION_API FNegotiatedDemandResult NegotiateDemandResponsibilities(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiationDemandPlan& DemandPlan,
		const FResidualParentCapabilitySummary& ParentSummary);
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ConfirmNegotiatedResponsibilitySet(
		const FLayoutRegionSolveRequest& RootRequest,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FChildCapabilitySummary& ChildSummary,
		FNegotiatedResponsibilitySet& InOutResponsibilitySet,
		FString& OutFailureReason);

	// Seam planning and optional-child decisions
#if WITH_AUTOMATION_TESTS
	/** Test-only geometry planner retained for focused parity fixtures. Production consumes committed candidate seams. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FNegotiatedSeamPlan BuildNegotiatedSeamPlan(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);
#endif
	/** True only when mandatory boundary topology continues on both sides at the same physical height. */
	bool HasParentPerimeterContinuation(const FLayoutRegionSolveRequest& Request, const FIntVector& Cell);

	/** Includes a child wall branching from a continuing parent perimeter when owning topology is supplied. */
	PORISMDIMSWORLDGENERATOREXTENSION_API TArray<FOwnedSeamJunctionRequirement> BuildOwnerSideJunctionRequirements(
		const TArray<FLayoutPartitionSeamRecord>& PlannedPartitionSeams,
		const FLayoutRegionSolveRequest* ParentRequest = nullptr);
	PORISMDIMSWORLDGENERATOREXTENSION_API FOptionalChildDecisionPlan BuildOptionalChildDecisionPlan(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);

	// Proof-contract building
	PORISMDIMSWORLDGENERATOREXTENSION_API FNegotiatedProofScheduleContract BuildNegotiatedProofScheduleContract(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults,
		const FNegotiatedSeamPlan& CommittedSeamPlan,
		const FTerrainSteppedPreparedSolveContract* RootTerrainSteppedPreparedSolveContract = nullptr);
#if WITH_AUTOMATION_TESTS
	/** Test-only convenience that derives synthetic seams before building a proof contract. Production callers must supply committed seams. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FNegotiatedProofScheduleContract BuildNegotiatedProofScheduleContract(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);
	/** Test-only convenience retaining synthetic stepped-contract fixture construction. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FNegotiatedProofScheduleContract BuildNegotiatedProofScheduleContract(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults,
		const FTerrainSteppedPreparedSolveContract* RootTerrainSteppedPreparedSolveContract);
#endif
	/** Reattaches request-owned negotiated child contract proof/assertion diagnostics after carrier merges. */
	PORISMDIMSWORLDGENERATOREXTENSION_API void AppendNegotiatedContractProofDiagnosticsToRequest(
		FLayoutRegionSolveRequest& InOutRequest);
	FLayoutRegionSolveRequest BuildNegotiatedChildProofRequest(
		const FNegotiatedDemandResult& NegotiatedDemandResult,
		const FLayoutRegionSolveRequest& ParentRequest,
		const FQueueCachePublicationMetadata& PublicationMetadata);
	TArray<FLayoutRegionSolveRequest> BuildNegotiatedChildProofRequests(
		const FRecursiveScheduleSolveContext& SolveContext,
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);
	TArray<FLayoutDirectChildRegionCommitment> BuildNegotiatedDirectChildCommitments(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);
	TArray<FLayoutNegotiatedChildResponsibilityContract> BuildNegotiatedResponsibilityContracts(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);
	TArray<FBoundedParentProofEvidence> BuildBoundedParentProofEvidenceRecords(
		const TArray<FNegotiatedDemandResult>& NegotiatedDemandResults);
	PORISMDIMSWORLDGENERATOREXTENSION_API FProofJobPlan BuildProofJobPlan(
		const FNegotiatedProofScheduleContract& ProofContract);
	/** Builds frozen negotiated proof contract without executing parent/child proof so runtime async dispatch can hand off certified child jobs. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FCapabilityBackedScheduleResult BuildCapabilityBackedNegotiatedProofContract(
		const FLayoutRegionSolveRequest& RootRequest);

	// Final proof and audit
	/** Builds the temporary public schedule request consumed by the legacy-sync proof adapter. */
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveScheduleRequest BuildProofExecutionCompatibilityScheduleRequest(
		const FNegotiatedProofScheduleContract& ProofContract);
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveScheduleResult ExecuteNegotiatedProofSchedule(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiatedProofScheduleContract& ProofContract);
	/** Reassembles one final negotiated schedule from already-proved parent and child results plus prepared proof requests. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildIndependentProofScheduleResultFromPreparedJobPlan(
		const FProofJobPlan& PreparedProofJobPlan,
		const FNegotiatedProofScheduleContract& ProofContract,
		FLayoutRegionSolveResult&& ParentProofResult,
		TArray<FLayoutRegionSolveResult>&& ChildProofResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason);
	/** Reassembles one final negotiated schedule from resume-artifact request shells without depending on a prepared proof plan. */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool BuildIndependentProofScheduleResultFromPreparedRequests(
		const FLayoutRegionSolveRequest& ParentRequest,
		const TArray<FLayoutRegionSolveRequest>& ChildRequests,
		const FNegotiatedProofScheduleContract& ProofContract,
		FLayoutRegionSolveResult&& ParentProofResult,
		TArray<FLayoutRegionSolveResult>&& ChildProofResults,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason);
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ValidateNegotiatedScheduleResult(
		const FRecursiveScheduleSolveContext& SolveContext,
		const FNegotiatedProofScheduleContract& ProofContract,
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		FString& OutFailureReason);
	/**
	 * Bridges required placement-backed recursive child families into plain-data
	 * negotiated demand results without public caller cutover and can also freeze
	 * the bridge-owned recursive vertical-access summary for later proof.
	 */
	/**
	 * Finds one bounded child-placement witness and freezes exact parent Entry /
	 * VerticalAccess cells without running parent or child interior CSP.
	 */
	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryPrepareRequiredChildParentAuthorityWitness(
		const FLayoutRegionSolveRequest& RootRequest,
		FLayoutRegionSolveRequest& OutPreparedParentRequest,
		FString& OutFailureReason,
		ELayoutSolvePreparationFailureKind* OutPreparationFailureKind = nullptr,
		int32* OutPreparationCandidateAttemptCount = nullptr);

	PORISMDIMSWORLDGENERATOREXTENSION_API bool TryBuildRequiredPlacementBackedDemandResults(
		const FLayoutRegionSolveRequest& RootRequest,
		TArray<FNegotiatedDemandResult>& OutDemandResults,
		FLayoutRegionSolveRequest* OutParentProofRequest,
		FString& OutFailureReason,
		FLayoutRecursiveVerticalAccessSummary* OutPlannedRecursiveVerticalAccessSummary = nullptr,
		FNegotiatedSeamPlan* OutCommittedSeamPlan = nullptr,
		ELayoutSolvePreparationFailureKind* OutPreparationFailureKind = nullptr,
		int32* OutPreparationCandidateAttemptCount = nullptr,
		FLayoutRegionalFailureRecord* OutRegionalFailure = nullptr,
		LayoutRegionScheduleSolverPrivate::FCertifiedRegionalProofResults* OutCertifiedRegionalProofResults = nullptr);
	PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionSolveScheduleResult SolveSingleRegionTreeWithPreparedSteppedContract(
		const FLayoutRegionSolveRequest& RootRequest);
	PORISMDIMSWORLDGENERATOREXTENSION_API FCapabilityBackedScheduleResult SolveRegionTreeWithCapabilityBackedNegotiation(
		const FLayoutRegionSolveRequest& RootRequest);

	// Post-structural handoff
	PORISMDIMSWORLDGENERATOREXTENSION_API bool ApplyPostStructuralScheduleHandoffs(
		FLayoutRegionSolveScheduleResult& InOutScheduleResult,
		const FNegotiatedProofScheduleContract& ProofContract,
		FString& OutFailureReason);

	// General helpers
	void NormalizeNegotiatedContactSet(FContactBackedPlacementFamily& InOutContactSet);
	FNegotiatedDemandResult BuildFailedDemandResult(
		const FString& ChildRegionDebugPath,
		const FResidualParentCapabilitySummary& ParentSummary,
		const FString& FailureReason);
	FCapabilityBackedScheduleResult BuildFailedCapabilityBackedScheduleResult(
		const FString& FailureReason);
}
