// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutTypes.h"
#include "Layout/Async/LayoutBackgroundSolveDispatcher.h"

#include "LayoutPreviewActor.generated.h"

class ULayoutProfileAsset;
class USceneComponent;
struct FLayoutBackgroundSolveSettings;

/**
 * Lightweight preview actor that stores deterministic solve output for one
 * layout profile and its preferred authored content source without mutating the chunk world.
 * The editor-only overlay is intended for layout verification before planning-window integration.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ALayoutPreviewActor : public AActor
{
	GENERATED_BODY()

public:
	ALayoutPreviewActor();
	virtual ~ALayoutPreviewActor();

	/** Rebuilds the in-memory preview result for the currently assigned profile and content source. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Layout")
	bool RebuildPreview();

	/** Clears any previously cached preview result. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Layout")
	void ClearPreview();

	/** Returns the cached root-region result retained for editor-only overlay drawing. */
	const FLayoutRegionSolveResult& GetCachedRegionResultForTesting() const { return CachedRegionResult; }

	/** Root component used so editor-only debug children can move with the actor transform. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	/** Profile used to solve this preview. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Profile used to solve this preview."))
	TObjectPtr<ULayoutProfileAsset> LayoutProfile = nullptr;

	/** Deterministic seed used for preview solves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Deterministic seed used for preview solves."))
	int32 Seed = 1337;

	/** Root solve budget used when this preview actor builds one standalone root request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Root solve budget used when this preview actor builds one standalone root request."))
	FLayoutRootSolveBudgetSettings SolveBudget;

	/** Caller-owned world-facing placement policy used when this preview actor builds one standalone root request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Caller-owned world-facing placement policy used when this preview actor builds one standalone root request. This keeps the editor preview on the same placement-policy surface as direct-root runtime callers instead of reopening profile-owned terrain policy."))
	FLayoutWorldBindingPlacementPolicy PreviewPlacementPolicy;

	/** Size of one block in Unreal units when turning layout block dimensions into preview boxes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug", meta = (ClampMin = "0.01", UIMin = "0.01", ToolTip = "Size of one layout block in Unreal units for editor preview drawing. The actor location is treated as the center of the solved footprint on level 0."))
	float BlockSizeInUnrealUnits = 100.0f;

	/** If true, redraw the debug overlay immediately after rebuilding the preview result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug", meta = (ToolTip = "If true, rebuilding the preview result also refreshes the editor-only debug overlay."))
	bool bAutoRedrawDebugAfterSolve = true;

	/** If true, changing debug settings in the editor immediately rebuilds the debug overlay from the cached result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug", meta = (ToolTip = "If true, changing debug settings in the editor immediately redraws the cached overlay without re-solving."))
	bool bAutoRedrawDebugOnSettingsChange = true;

	/** Small padding removed from preview boxes so adjacent cells stay visually distinguishable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Padding removed from preview boxes so adjacent cells stay visually distinguishable."))
	float DebugBoxPaddingInUnrealUnits = 2.0f;

	/** If true, draw the root solved-footprint bounds for the cached preview result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw the root solved-footprint bounds for the cached preview result."))
	bool bDrawRootFootprintBounds = true;

	/** If true, draw planned cells using the configured intent filters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw planned cells using the configured intent filters."))
	bool bDrawPlannedCells = true;

	/** If true, draw solved structural placement cells using the configured filters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw solved structural placement cells using the configured filters."))
	bool bDrawPlacements = true;

	/** If true, draw one bounds box for each scheduled child region included in the cached preview result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw one bounds box for each scheduled child region included in the cached preview result."))
	bool bDrawChildRegionBounds = true;

	/** If true, draw exported entry cells from the cached preview result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw exported entry cells from the cached preview result."))
	bool bDrawExportedEntryCells = true;

	/** If true, draw exported boundary points from the cached region result. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw exported boundary points from the cached region result."))
	bool bDrawExportedBoundaryPoints = true;

	/** If true, draw closure-segment boxes using the configured zone filters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw closure-segment boxes using the configured zone filters."))
	bool bDrawClosureSegments = true;

	/** If true, draw grouped closure runs using the configured zone filters. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw grouped closure runs using the configured zone filters."))
	bool bDrawClosureRuns = true;

	/** If true, draw shared partition seams; the closure-zone filter treats every seam as implicit Perimeter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw shared partition seams. The closure-zone filter treats every seam as implicit Perimeter."))
	bool bDrawPartitionSeams = true;

	/** If true, draw residual cells that survived the structural pass. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw residual cells that survived the structural pass."))
	bool bDrawResidualCells = true;

	/** If true, draw sparse placements accepted after structural commitments settled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Layers", meta = (ToolTip = "If true, draw sparse placements accepted after structural commitments settled."))
	bool bDrawSparsePlacements = true;

	/** Optional planned-cell intent filter. Leave empty to draw every intent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Filters", meta = (ToolTip = "Optional planned-cell intent filter. Leave empty to draw every intent."))
	TArray<ELayoutCellIntent> VisiblePlannedCellIntents;

	/** Optional placement intent filter. Leave empty to draw every solved placement intent. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Filters", meta = (ToolTip = "Optional placement intent filter. Leave empty to draw every solved placement intent."))
	TArray<ELayoutCellIntent> VisiblePlacementIntents;

	/** Optional placement role filter. Leave empty to draw every solved placement role. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Filters", meta = (ToolTip = "Optional placement role filter. Leave empty to draw every solved placement role."))
	TArray<ELayoutModuleRole> VisiblePlacementRoles;

	/** Optional closure-zone filter; shared seams are admitted as implicit Perimeter. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Debug|Filters", meta = (ToolTip = "Optional closure-zone filter. Shared seams are admitted as implicit Perimeter; leave empty to draw all closures and seams."))
	TArray<ELayoutPlacementZone> VisibleClosureZones;

	/** Last in-memory solve result generated by this preview actor. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Last in-memory solve result generated by this preview actor."))
	FLayoutSolveResult PreviewResult;

#if WITH_EDITOR
	/** Keeps the debug overlay synchronized when editor properties change. */
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
#if WITH_EDITOR
	/** Destroys the currently registered editor-only debug components. */
	void DestroyDebugPreviewComponents();

	/** Rebuilds the editor-only debug components from the cached preview result. */
	void RebuildDebugPreviewComponents();
#endif

	/** Resolves the shared layout-cell size used by the current preview content source. */
	FIntVector ResolveSharedCellSizeInBlocks() const;

	/** Returns true when a cached region result is available for debug drawing. */
	bool HasCachedRegionResult() const;

	/** Cached region result retained so editor-only overlays can draw boundary points and future region metadata. */
	FLayoutRegionSolveResult CachedRegionResult;

	/** Cached scheduled region-tree result retained so editor-only overlays can inspect child-region solves. */
	FLayoutRegionSolveScheduleResult CachedScheduleResult;

	/** Shared cell size cached alongside the last region result so overlays stay deterministic. */
	FIntVector CachedSharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Background solve dispatcher for routing preview solves through the lifecycle chain. */
	TUniquePtr<FLayoutBackgroundSolveDispatcher> BackgroundSolveDispatcher;

	/** True once CachedRegionResult and CachedSharedCellSizeInBlocks have been populated by RebuildPreview. */
	bool bHasCachedPreviewRegionResult = false;

#if WITH_EDITORONLY_DATA
	/** Editor-only debug components created from the cached preview result. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<USceneComponent>> DebugPreviewComponents;
#endif
};
