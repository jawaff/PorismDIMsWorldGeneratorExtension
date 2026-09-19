// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"
#include "Layout/Planning/LayoutRootSpacing.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Types/LayoutTypes.h"

class FLayoutActiveBiomeSampler;
class ULayoutProfileAsset;
class ULayoutWorldBindingAsset;

/** One continuation preview cell with its frozen terrain-aligned base height. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationPreviewTerrainCell
{
	/** Normalized continuation cell coordinate. */
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Frozen block-world base height for this cell before its local Z offset. */
	int32 BaseBlockWorldZ = 0;

	/** Final adapter-owned intent used by continuation debug markers. */
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/** Final adapter-owned placement zone used by continuation debug markers. */
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

	/** Final adapter-owned terrain seam faces used by continuation debug markers. */
	uint8 TerrainSeamFaceMask = 0;

	/** True when this final cell is a generated bridge or landing cell. */
	bool bIsBridgeCell = false;
};

/** One preview-only solver or lifecycle diagnostic anchored to a continuation cell when known. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationPreviewDiagnostic
{
	/** Local cell this requirement or failure belongs to. */
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** True when LocalCell identifies a concrete diagnostic marker. */
	bool bHasLocalCell = false;

	/** Related local cell when this diagnostic identifies an edge. */
	FIntVector RelatedLocalCell = FIntVector::ZeroValue;

	/** True when RelatedLocalCell defines a diagnostic edge. */
	bool bHasRelatedLocalCell = false;

	/** Lifecycle stage that emitted this diagnostic. */
	FString Stage;

	/** Required role, face, traversal channel, or other authoring contract. */
	FString Requirement;

	/** Rejection or explanatory result. */
	FString Detail;
};

/** Compact editor-facing geometry captured from one prepared continuation before worker submission. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationPreviewGeometry
{
	/** Block-world origin of normalized continuation cells. */
	FIntVector PathOriginBlockWorldPos = FIntVector::ZeroValue;

	/** Shared cell dimensions used to convert normalized cells into world space. */
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Actual solved centerline cells normalized into the widened planned-cell coordinate space. */
	TArray<FIntVector> CenterlineCells;

	/** Frozen terrain-aligned base heights corresponding by index to CenterlineCells. */
	TArray<int32> CenterlineBaseBlockWorldZs;

	/** Terrain-aligned planned cells used to draw stepped continuation borders and diagnostics. */
	TArray<FLayoutContinuationPreviewTerrainCell> TerrainCells;

	/** Preview-only authoring diagnostics. Never consumed by solve apply or realization. */
	TArray<FLayoutContinuationPreviewDiagnostic> Diagnostics;
};

/** Captured authored candidate. Soft paths are output identity only; workers never resolve them. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationCandidateSnapshot
{
	FName CandidateId;
	int32 Weight = 1;
	bool bHasProfile = false;
	bool bSupportsSteppedTerrainSolve = false;
	int32 ContinuationEntryLevel = 0;
	FIntPoint MinimumFootprintInCells = FIntPoint::ZeroValue;
	FIntPoint MaximumFootprintInCells = FIntPoint::ZeroValue;
	TSoftObjectPtr<ULayoutProfileAsset> ProfilePath;
	TSoftObjectPtr<ULayoutRegionContentSetAsset> ContentSetPath;
	FLayoutRegionSolveRequest RequestPrototype;
	FString FailureReason;
};

/** One bounded route's immutable authored inputs, captured before worker admission. Invalid weighted slots remain present. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContinuationPlanningSnapshot
{
	FName BindingId;
	FName FamilyId;
	TArray<FLayoutContinuationCandidateSnapshot> Candidates;
};

/** Frozen continuation preparation consumed unchanged by async lifecycle stages. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPreparedContinuation
{
	/** Connector identity and resolved endpoint-facing state. */
	FResolvedLayoutConnectorRecord ConnectorRecord;

	/** Pointer-free terrain samples captured on the game thread for prewarm and solve. */
	FLayoutFrozenTerrainBiomeAdapterInput FrozenTerrainEvidence;

	/** Cardinal A* centerline in start-endpoint-relative grid coordinates. */
	TArray<FIntPoint> CenterlineCells;

	/** Normalized widened cells supplied to the solver. */
	TArray<FLayoutPlannedCell> PlannedCells;

	/** Minimum raw centerline and widened grid cells used for normalization. */
	FIntPoint MinCenterlineCell = FIntPoint::ZeroValue;
	FIntPoint MinPlannedCell = FIntPoint::ZeroValue;

	/** Widened footprint and endpoint-facing directions preserved for diagnostics. */
	FIntPoint FootprintSize = FIntPoint::ZeroValue;
	ELayoutFaceDirection StartFacing = ELayoutFaceDirection::PosX;
	ELayoutFaceDirection EndFacing = ELayoutFaceDirection::PosX;

	/** World anchor and final immutable request consumed by worker lifecycle. */
	FIntVector PathOriginBlockWorldPos = FIntVector::ZeroValue;
	FLayoutRegionSolveRequest SolveRequest;
};

/** One descriptor preparation result retained while a route submits independent segment solves. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPreparedContinuationRouteSegment
{
	FLayoutContinuationSegmentDescriptor Descriptor;
	FResolvedLayoutConnectorRecord ConnectorRecord;
	TOptional<FLayoutPreparedContinuation> PreparedContinuation;

	/** Geometry retained for editor diagnosis when preparation fails after route expansion. */
	FLayoutContinuationPreviewGeometry PreviewGeometry;

	/** Stable preparation failure used by route diagnostics and failed-segment overlays. */
	FString FailureReason;
};

/** Frozen route preparation shared by automatic and explicit continuation submission. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutPreparedContinuationRoute
{
	FResolvedLayoutConnectorRecord RouteRecord;
	FLayoutContinuationRouteRecord Route;
	FLayoutFrozenTerrainBiomeAdapterInput FrozenTerrainEvidence;
	TArray<FLayoutPreparedContinuationRouteSegment> Segments;
};

#if WITH_AUTOMATION_TESTS
/** Testing-only connector path cell classification used by focused pure pathfinder coverage. */
enum class ELayoutConnectorPathCellType : uint8
{
	Walkable,
	Gap,
	Blocked
};

/** Testing-only sampled connector-grid cell used by focused pathfinder coverage. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutConnectorPathCell
{
	ELayoutConnectorPathCellType Type = ELayoutConnectorPathCellType::Blocked;
	int32 SurfaceZBlockWorld = 0;
};

/** Testing-only pure pathfinder input for focused connector-grid routing coverage. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutConnectorPathQuery
{
	FIntPoint StartCell = FIntPoint::ZeroValue;
	FIntPoint EndCell = FIntPoint::ZeroValue;
	FIntPoint MinCell = FIntPoint::ZeroValue;
	FIntPoint MaxCell = FIntPoint::ZeroValue;
	int32 MaxSlopeBlocks = 3;
	int32 MaxBridgeGapCells = 0;
	bool bAllowSteepTerrainTunneling = false;
};
#endif

/** Optional active-biome context used by runtime connector planning to sample terrain before chunks load. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutConnectorTerrainPathContext
{
	const FLayoutActiveBiomeSampler* ActiveBiomeSampler = nullptr;
	FLayoutNoiseCoordinateSettings CoordinateSettings;

	/** Actual solved root footprints, including endpoint roots; owned capture is safe on workers. */
	TArray<FLayoutRootSpacingReservation> RootFootprints;
};

/**
 * Connector planning helpers that treat roads, trails, and other inter-layout
 * paths as their own layout family built from exported site endpoints.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutConnectorPlanning
{
public:
	/** Collects connector endpoints exported by one solved site. */
	static TArray<FResolvedLayoutConnectorEndpoint> CollectConnectorEndpointsForSite(
		const FIntPoint& SiteReservationKey,
		const FResolvedLayoutSiteRecord& SiteRecord);

	/** Builds one deterministic continuation record for an explicit exported endpoint pair without sampling or solving. */
	static bool TryBuildContinuationRecordForEndpointPair(
		const FResolvedLayoutConnectorEndpoint& Start,
		const FResolvedLayoutConnectorEndpoint& End,
		const ULayoutWorldBindingAsset* WorldBinding,
		const ULayoutProfileAsset* ContinuationProfile,
		int32 WorldSeed,
		FResolvedLayoutConnectorRecord& OutRecord,
		FString& OutFailureReason);

	/** Rejects full-width planned cells intersecting any root footprint. Path origin is the center of local cell zero,
	 * matching prepared/realized connector records. No obstacle rerouting or biome test. */
	static bool ValidateContinuationClearance(
		const FIntVector& PathOriginBlockWorldPos,
		const FIntVector& CellSize,
		TConstArrayView<FLayoutPlannedCell> PlannedCells,
		TConstArrayView<FLayoutRootSpacingReservation> RootFootprints,
		FString& OutFailureReason);

	/** Captures resident authored inputs on the game thread; never samples terrain or loads assets. */
	static bool CaptureContinuationPlanningInputs(
		const FResolvedLayoutConnectorRecord& RouteRecord,
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 WorldSeed,
		FLayoutContinuationPlanningSnapshot& OutSnapshot,
		FString& OutFailureReason);

	/** Worker-safe overloads use owned snapshots and a request-owned biome sampler. No asset resolution. */
	static bool TryDiscoverContinuationRoute(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FLayoutContinuationPlanningSnapshot& Snapshot,
		const FLayoutConnectorTerrainPathContext* TerrainPathContext,
		FLayoutFrozenTerrainBiomeAdapterInput& OutTerrainEvidence,
		TArray<FIntPoint>& OutCenterlineCells,
		FString& OutFailureReason);
	static bool TryBuildContinuationRouteSegments(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FLayoutContinuationPlanningSnapshot& Snapshot,
		const TArray<FIntPoint>& CenterlineCells,
		const TSet<int32>& ForbiddenSeamIndices,
		FLayoutContinuationRouteRecord& OutRoute,
		FString& OutFailureReason);
	static bool TryPrepareContinuationRoute(
		const FResolvedLayoutConnectorRecord& RouteRecord,
		const FLayoutContinuationPlanningSnapshot& Snapshot,
		int32 WorldSeed,
		const FLayoutConnectorTerrainPathContext* TerrainPathContext,
		FLayoutPreparedContinuationRoute& OutPreparedRoute,
		FString& OutFailureReason);
	static bool TryPrepareContinuationSegment(
		const FResolvedLayoutConnectorRecord& RouteRecord,
		const FLayoutContinuationSegmentDescriptor& Segment,
		const FLayoutContinuationRouteRecord& Route,
		const FLayoutContinuationPlanningSnapshot& Snapshot,
		int32 WorldSeed,
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainEvidence,
		FLayoutPreparedContinuation& OutPreparedContinuation,
		FString& OutFailureReason);

	/** Captures terrain and builds one cardinal route without constructing a whole-route solve request. */
	static bool TryDiscoverContinuationRoute(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const ULayoutWorldBindingAsset* WorldBinding,
		const FLayoutConnectorTerrainPathContext* TerrainPathContext,
		FLayoutFrozenTerrainBiomeAdapterInput& OutTerrainEvidence,
		TArray<FIntPoint>& OutCenterlineCells,
		FString& OutFailureReason);

	/** Splits one routed centerline into deterministic min/max-bounded family-candidate segments. */
	static bool TryBuildContinuationRouteSegments(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const ULayoutWorldBindingAsset* WorldBinding,
		const TArray<FIntPoint>& CenterlineCells,
		const TSet<int32>& ForbiddenSeamIndices,
		FLayoutContinuationRouteRecord& OutRoute,
		FString& OutFailureReason);

	/** Builds terrain-aligned editor geometry from one immutable prepared continuation. */
	static void BuildContinuationPreviewGeometry(
		const FLayoutPreparedContinuation& Prepared,
		FLayoutContinuationPreviewGeometry& OutGeometry);

	/** Prepares all independently eligible bounded segment requests from one frozen terrain-aware route. */
	static bool TryPrepareContinuationRoute(
		const FResolvedLayoutConnectorRecord& RouteRecord,
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 WorldSeed,
		const FLayoutConnectorTerrainPathContext* TerrainPathContext,
		FLayoutPreparedContinuationRoute& OutPreparedRoute,
		FString& OutFailureReason);

	/** Builds one candidate-owned bounded segment request from a frozen route and terrain capture. */
	static bool TryPrepareContinuationSegment(
		const FResolvedLayoutConnectorRecord& RouteRecord,
		const FLayoutContinuationSegmentDescriptor& Segment,
		const FLayoutContinuationRouteRecord& Route,
		const ULayoutWorldBindingAsset* WorldBinding,
		int32 WorldSeed,
		const FLayoutFrozenTerrainBiomeAdapterInput& TerrainEvidence,
		FLayoutPreparedContinuation& OutPreparedContinuation,
		FString& OutFailureReason);

	/** Samples pointer-free corridor evidence using caller-owned noise and captured positive odd width; no asset reads. */
	static bool TrySampleConnectorCorridorTerrain(
		const FResolvedLayoutConnectorRecord& ConnectorRecord,
		const FLayoutConnectorTerrainPathContext* TerrainPathContext,
		FLayoutFrozenTerrainBiomeAdapterInput& OutTerrainEvidence,
		int32 RequiredCorridorWidthInCells);

	#if WITH_AUTOMATION_TESTS
	/** Testing-only pure pathfinder hook for focused connector-grid routing coverage. */
		static bool FindConnectorPathCells(
			const FLayoutConnectorPathQuery& Query,
			const TMap<FIntPoint, FLayoutConnectorPathCell>& CellsByGrid,
			TArray<FIntPoint>& OutPathCells);

	/** Testing-only pure width-expansion hook for focused continuation corridor coverage. */
	static bool ExpandContinuationCenterlineForTesting(
		const TArray<FIntPoint>& CenterlineCells,
		int32 CorridorWidthInCells,
		int32 StructuralSolveLevel,
		TArray<FLayoutPlannedCell>& OutPlannedCells,
		FIntPoint& OutFootprintSize,
		FIntPoint& OutMinCell);

#endif

private:
};
