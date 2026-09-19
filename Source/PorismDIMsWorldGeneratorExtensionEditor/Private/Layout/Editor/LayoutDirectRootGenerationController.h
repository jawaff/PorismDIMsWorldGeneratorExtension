// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Runtime/LayoutGeneratorSolveFacade.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutTypes.h"
#include "UObject/StrongObjectPtr.h"

class AChunkWorldExtended;
class UChunkWorldLayoutRuntimeComponent;
class FEditorViewportClient;
class FPrimitiveDrawInterface;
class FSceneView;
class FViewport;
class ULayoutDirectRootGenerationSettings;

/** Lightweight count summary of the cached editor overlay content. */
struct FLayoutDirectRootGenerationOverlaySummary
{
	bool bHasHoveredBlock = false;
	bool bDrawsHoverSquare = false;
	bool bHasSolvedPreview = false;
	int32 SolvedFootprintCount = 0;
	int32 CellZoneMarkerCount = 0;
	int32 BoundaryFacePyramidCount = 0;
	int32 UnoccupiedCellCount = 0;
	int32 ChildBoundsCount = 0;
	int32 ClosureSegmentCount = 0;
	int32 PartitionSeamCount = 0;
};

/**
 * Per-cell debug information assembled by the editor controller after a successful preview solve.
 * Zone and boundary-face classification is a deterministic replay of the solver's own rules —
 * computed from (Cell, FootprintSize) without derivation from placed modules or closure data.
 * The solver is never touched; this struct lives only in the editor module.
 */
struct FLayoutCellDebugInfo
{
	/** Grid position in cell units. */
	FIntVector Cell = FIntVector::ZeroValue;

	/** Local placement zone before terrain retaining faces contribute to topology. */
	ELayoutPlacementZone BaseZone = ELayoutPlacementZone::Any;

	/** Final solver placement zone copied from the corresponding planned cell. */
	ELayoutPlacementZone EffectiveZone = ELayoutPlacementZone::Any;


	/** Adapter-authored retaining terrain faces for this cell. */
	uint8 TerrainSeamFaceMask = 0;

	/**
	 * Broad planner intent copied from the corresponding FLayoutPlannedCell.
	 * Used only for external cross-reference against the Placements array to detect
	 * unoccupied cells — not stored redundantly for its own sake.
	 */
	ELayoutCellIntent Intent = ELayoutCellIntent::Interior;

	/**
	 * World-space face directions classified as OuterBoundary by the solver.
	 * A face is OuterBoundary when its neighbor cell position falls outside the
	 * solved footprint bounds [0, FootprintSize-1] in X/Y or outside [0, MaxPlannedZ] in Z.
	 * Corner cells may have up to 3 faces; top-layer cells at MaxPlannedZ also expose PosZ.
	 */
	TArray<ELayoutFaceDirection> RegionBoundaryFaces;

	/** True when the planner assigned Entry intent before structural solving. */
	bool bIsPlannedEntryCell = false;

	/** Exact region-local Entry faces supplied by solve commitments. */
	TArray<ELayoutFaceDirection> EntryFaces;

};

/**
 * Shared state holder for the direct root-layout editor workflow.
 * One controller instance is owned by the editor module and is reused by the
 * tool window and the editor mode so hover, preview, diagnostics, and apply
 * all stay on one authoritative explicit-root runtime contract.
 */
class PORISMDIMSWORLDGENERATOREXTENSIONEDITOR_API FLayoutDirectRootGenerationController
{
public:
	FLayoutDirectRootGenerationController();

	/** Returns the transient settings object displayed in the tool window details view. */
	ULayoutDirectRootGenerationSettings* GetSettingsObject() const;

	/** Returns true when the direct-root editor mode is active. */
	bool IsToolActive() const { return bToolActive; }

	/** Records the external active/inactive state owned by the editor mode manager. */
	void SetToolActive(bool bInToolActive);

	/** Updates hovered chunk-world context from the active editor viewport. */
	bool UpdateHoverFromViewport(FEditorViewportClient* ViewportClient, FViewport* Viewport);

	/** Dispatches Ctrl-click to root preview or two-endpoint continuation selection for current profile. */
	bool HandlePrimaryActionAtHoveredLocation();

	/** Solves the configured root profile at the current hovered location and caches the preview-side result. */
	bool PreviewSolveAtHoveredLocation();

#if WITH_AUTOMATION_TESTS
	/** Schedules one preview solve without pumping to completion so automation can force superseded/latest-only races. */
	bool StartPreviewSolveAtHoveredLocationForTesting();
#endif

	/** Applies the cached preview through existing realization guards. Invalid geometry requires the
	 * explicit debug action and enabled partial-preview setting; application never certifies its solve. */
	bool ApplyCachedSolve(bool bAllowInvalidPreview = false);

	/** Clears any previously cached explicit-root solve result. */
	void ClearCachedSolve();

	/** Returns the persistent status/diagnostics text shown in the tool window. */
	FText GetStatusText() const;

	/** Returns the generation-attempt summary shown in the primary tool section. */
	FText GetGenerationSummaryText() const;

	/** Returns the static control instructions shown at the bottom of the tool window. */
	FText GetInstructionText() const;

	/** Draws cached solve overlays without changing solve data. Terrain runs are prepared at publication;
	 * the current view selects visible cells and their projected-size detail level. */
	void Render(const FSceneView* View, FPrimitiveDrawInterface* PDI) const;

	/** Returns a lightweight summary of the cached overlay content for diagnostics and automation. */
	FLayoutDirectRootGenerationOverlaySummary BuildOverlaySummary() const;

	/** Builds per-cell debug information from cached planned intents for solved or partial previews. */
	void BuildCellDebugInfos();

	/** Returns the cached per-cell debug information for testing and overlay drawing. */
	const TArray<FLayoutCellDebugInfo>& GetCachedCellDebugInfosForTesting() const { return CachedCellDebugInfos; }

	/** Sets one explicit hovered block directly for headless editor automation coverage. */
	void SetHoveredLocationForTesting(AChunkWorldExtended* ChunkWorld, const FIntVector& BlockWorldPos);

	/** Returns the cached explicit-root site record for editor automation assertions. */
	const FResolvedLayoutSiteRecord& GetCachedSiteRecordForTesting() const { return CachedSiteRecord; }

	/** Returns the cached recursive schedule result for editor automation assertions. */
	const FLayoutRegionSolveScheduleResult& GetCachedScheduleResultForTesting() const { return CachedScheduleResult; }

	/** Resolves one placement footprint size the same way the cached overlay path does for automation coverage. */
	FIntVector ResolvePlacementSizeInBlocksForTesting(const FLayoutPlacedModule& Placement, const FIntVector& SharedCellSizeInBlocks) const;

	/** Rebuilds one placement cell set the same way the cached overlay path does for automation coverage. */
	TArray<FIntVector> BuildPlacementCellsForTesting(const FLayoutPlacedModule& Placement) const;

	/** Returns the cached preview footprint anchor the overlay uses after root-kind terrain-fit rules are applied. */
	FIntVector ResolveCachedRootFootprintMinBlockWorldPosForTesting() const;

	/** Returns how many cached terrain-write blocks the preview produced for overlay/apply parity. */
	int32 GetCachedPreviewTerrainWriteCountForTesting() const { return CachedPreviewTerrainWritePositions.Num(); }

	/** Returns the cached frozen terrain contract replayed by preview/apply automation. */
	const FLayoutFrozenTerrainContract& GetCachedPreviewFrozenTerrainContractForTesting() const { return CachedPreviewFrozenTerrainContract; }

	/** Pure replay of the solver's OuterBoundary classification for one planned cell face. Returns the set of world-space face directions that face outside the footprint. */
	static TArray<ELayoutFaceDirection> ClassifyRegionBoundaryFacesForTesting(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		int32 MaxPlannedLevelZ);

	/** Pure replay of the solver's placement-zone classification for one planned cell. */
	static ELayoutPlacementZone ClassifyCellPlacementZoneForTesting(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize);

#if WITH_AUTOMATION_TESTS
	/** Resolves continuation marker alignment from solved state or preserved preflight selection. */
	static int32 ResolveContinuationPreviewAlignmentLevelForTesting(
		const FLayoutSolveResult& SolveResult,
		const FResolvedLayoutConnectorRecord& ConnectorRecord);
#endif

	/** Builds one cached-overlay cell record from final local topology. */
	static FLayoutCellDebugInfo BuildCellDebugInfo(
		const FLayoutPlannedCell& Cell,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TSet<FIntVector>& ExternalCells,
		const FIntVector& CellOffset = FIntVector::ZeroValue,
		const TArray<FLayoutCommittedEndpointAnchor>* CommittedEndpointAnchors = nullptr);

	/** Fires whenever hover, cached solve, or diagnostics state changes. */
	FSimpleMulticastDelegate& OnStateChanged() { return StateChanged; }

private:
#if WITH_AUTOMATION_TESTS
	friend class FLayoutDirectRootGenerationPreviewRenderingCostTest;
#endif

	/** Inclusive X-run of adjacent blocks sharing one preview color; never bridges holes or Y/Z rows. */
	struct FTerrainPreviewRun
	{
		FIntVector Min = FIntVector::ZeroValue;
		FIntVector Max = FIntVector::ZeroValue;
		bool bEmpty = false;
	};

	/** Builds display-only runs once per published preview, leaving write order/materials untouched.
	 * Conflicting duplicate positions use the final write's preview color. */
	static TArray<FTerrainPreviewRun> BuildTerrainPreviewRuns(const TArray<FLayoutFrozenTerrainWriteRecord>& Writes);

	/** Draws visible cached row bounds in the owning world's current coordinate frame. */
	static void RenderTerrainPreviewRuns(const FSceneView* View, FPrimitiveDrawInterface* PDI,
		const AChunkWorldExtended& ChunkWorld, const TArray<FTerrainPreviewRun>& Runs);

	/** Block-space geometry follows preview publication/Clear; camera movement never rebuilds it. */
	TArray<FTerrainPreviewRun> CachedTerrainPreviewRuns;
	TArray<TArray<FTerrainPreviewRun>> CachedContinuationTerrainPreviewRuns;

	/** Returns the hovered chunk world if it satisfies the current tool settings. */
	AChunkWorldExtended* GetUsableHoveredChunkWorld() const;

	/** Records one persistent status line and notifies the tool window. */
	void SetStatusMessage(FString&& Message);

	/** Returns true once the tool has cached either a successful preview solve or a rejected preview attempt. */
	bool HasCachedPreviewAttempt() const;

	/** Returns the solve result that should currently feed persistent diagnostics. */
	const FLayoutSolveResult* GetCachedDiagnosticSolveResult() const;

	/** Builds the persistent multi-line diagnostics text shown in the tool window. */
	FString BuildPersistentDiagnosticsText() const;

	/** Builds the generation-first summary shown above the detailed diagnostics. */
	FString BuildGenerationSummaryText() const;

	/** Shared preview submit path used by interactive mode and focused automation. */
	bool PreviewSolveAtHoveredLocationInternal(bool bPumpUntilCompletionForAutomation);

	/** Selects first endpoint or submits a two-endpoint continuation preview. */
	bool HandleContinuationPrimaryAction(AChunkWorldExtended& ChunkWorld, UChunkWorldLayoutRuntimeComponent& RuntimeComponent);

	/** Returns the shared cell size used by the currently cached explicit-root solve. */
	FIntVector ResolveCachedSharedCellSizeInBlocks() const;

	/** Returns the root footprint min used by the cached preview, including any cached preview terrain-fit anchor shift. */
	FIntVector ResolveCachedRootFootprintMinBlockWorldPos() const;

	/** Resolves the deterministic solve seed for the current hovered location. */
	int32 ResolveSolveSeed(const AChunkWorldExtended& ChunkWorld, const FIntVector& SiteCenterBlockWorldPos) const;

	/** Returns the terrain stage vertical shift in blocks for the given local cell column, zero for bridge-level Z cells. */
	int32 ResolveTerrainStageShiftBlocksForColumn(const FIntVector& LocalCell) const;

	/** Converts one local layout cell into a world-space center for the cached explicit-root solve. */
	FVector ResolveCachedCellCenterWorld(const FIntVector& LocalCell) const;

	/** Converts one cached region result into its world-space footprint bounds. */
	bool TryResolveRegionBoundsWorld(const FLayoutRegionSolveResult& RegionResult, FVector& OutCenter, FVector& OutExtent) const;

	/** Draws continuation footprint, terrain writes, route centerline, and root-equivalent segment diagnostics. */
	void RenderContinuationPreview(const FSceneView* View, FPrimitiveDrawInterface* PDI, const AChunkWorldExtended& ChunkWorld) const;

	/** Rebuilds root-equivalent cell diagnostics for every independently solved continuation segment. */
	void BuildContinuationCellDebugInfos();

	/** Resolves preview alignment when preflight rejection has no solver-produced result metadata. */
	static int32 ResolveContinuationPreviewAlignmentLevel(
		const FLayoutSolveResult& SolveResult,
		const FResolvedLayoutConnectorRecord& ConnectorRecord);

	/** Draws segment diagnostics in their route-local frame. BaseZByCell preserves the first terrain
	 * occurrence per cell, replacing repeated linear searches without changing fallback heights. */
	void RenderContinuationCellDebugInfos(
		const FSceneView* View,
		FPrimitiveDrawInterface* PDI,
		const AChunkWorldExtended& ChunkWorld,
		const FLayoutGeneratorContinuationSegmentSolveResult& Segment,
		const TArray<FLayoutCellDebugInfo>& CellDebugInfos,
		const TMap<FIntVector, int32>& BaseZByCell) const;

	/** True while the editor mode is active and allowed to read viewport hover state. */
	bool bToolActive = false;

	/** Most recently hovered chunk world accepted by the tool settings. */
	TWeakObjectPtr<AChunkWorldExtended> HoveredChunkWorld;

	/** Hovered block-world position used for preview solve targeting. */
	FIntVector HoveredBlockWorldPos = FIntVector::ZeroValue;

	/** True once a hovered chunk-world block has been resolved from the editor viewport. */
	bool bHasHoveredBlock = false;

	/** Latest async root-preview state used to cancel superseded jobs and ignore late completions. */
	FLayoutGeneratorSolveAsyncState PreviewAsyncState;

	/** Latest async continuation-preview state, isolated from root preview cache ownership. */
	FLayoutGeneratorSolveAsyncState ContinuationPreviewAsyncState;

	/** First endpoint selected by Connect Layouts mode. */
	FResolvedLayoutConnectorEndpoint SelectedContinuationStartEndpoint;
	TWeakObjectPtr<AChunkWorldExtended> SelectedContinuationChunkWorld;
	bool bHasSelectedContinuationStartEndpoint = false;

	/** True while an explicit continuation preview is queued. */
	bool bContinuationPreviewInProgress = false;

	/** Accepted continuation preview keeps independently solved segment artifacts. */
	TArray<FLayoutGeneratorContinuationSegmentSolveResult> CachedContinuationSegments;
	bool bCachedContinuationPreviewIsPartial = false;
	bool bHasCachedAcceptedContinuationPreview = false;

	/** Last successfully preview-solved site record. */
	FResolvedLayoutSiteRecord CachedSiteRecord;

	/** Last recursive schedule result produced by PreviewSolveAtHoveredLocation. */
	FLayoutRegionSolveScheduleResult CachedScheduleResult;

	/** Chunk world that produced the cached preview and owns its apply/render contract. */
	TWeakObjectPtr<AChunkWorldExtended> CachedPreviewChunkWorld;

	/** Site center used by the last preview attempt, including rejected solves. */
	FIntVector CachedPreviewSiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** True once at least one direct preview solve attempt has been executed. */
	bool bHasCachedPreviewAttempt = false;

	/** True when the last cached preview attempt fully passed solve plus terrain-fit validation. */
	bool bHasCachedAcceptedPreview = false;

	/** True while an async preview solve has been submitted and the callback has not yet fired. */
	bool bPreviewSolveInProgress = false;

	/** Shared cell size cached alongside the last explicit-root preview solve. */
	FIntVector CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** True when the cached preview resolved one explicit terrain-fit anchor for later apply parity. */
	bool bHasCachedPreviewTerrainFit = false;

	/** Frozen terrain contract replayed by editor preview/apply without reopening terrain-fit decisions. */
	FLayoutFrozenTerrainContract CachedPreviewFrozenTerrainContract;

	/** Terrain-fit anchor captured from the cached frozen terrain contract. */
	FIntVector CachedPreviewTerrainAnchorBlockWorldPos = FIntVector::ZeroValue;

	/** Structured terrain-fit outcome captured from the cached frozen terrain contract. */
	ELayoutWorldBindingTerrainFitDiagnosticKind CachedPreviewTerrainFitDiagnosticKind =
		ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** Terrain writes captured from the cached frozen terrain contract so overlay/apply parity stays visible. */
	TArray<FIntVector> CachedPreviewTerrainWritePositions;

	/** Material payload paired one-for-one with CachedPreviewTerrainWritePositions. */
	TArray<int32> CachedPreviewTerrainWriteMaterials;

	/** Latest preview-side rejection detail when solve succeeded but preview terrain fit did not. */
	FString CachedPreviewFailureReason;

	/** Latest cached-apply rejection shown separately from preview readiness. */
	FString LastApplyFailureReason;

	/** Persistent diagnostics text shown in the tool window. */
	FString LastStatusMessage;

	/** Wall-clock seconds spent by the most recent explicit-root preview solve attempt. */
	double LastPreviewSolveSeconds = 0.0;

	/** Wall-clock seconds spent by the most recent apply attempt, or a negative value when none has run. */
	double LastApplySeconds = -1.0;

	/** Tool settings shared by the viewport mode and the tab UI. */
	TStrongObjectPtr<ULayoutDirectRootGenerationSettings> SettingsObject;

	/** Per-cell debug information built from the most recent successful preview solve. */
	TArray<FLayoutCellDebugInfo> CachedCellDebugInfos;

	/** Root-equivalent debug records kept independently for every continuation segment. */
	TArray<TArray<FLayoutCellDebugInfo>> CachedContinuationCellDebugInfos;

	/** UI refresh event fired when internal hover or preview state changes. */
	FSimpleMulticastDelegate StateChanged;
};
