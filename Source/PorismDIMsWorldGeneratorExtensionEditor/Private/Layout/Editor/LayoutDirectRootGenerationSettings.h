// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Types/LayoutTypes.h"
#include "UObject/Object.h"

#include "LayoutDirectRootGenerationSettings.generated.h"

class AChunkWorldExtended;
class ULayoutProfileAsset;
class ULayoutWorldBindingAsset;

/**
 * Transient editor settings for the Layout Generator workflow.
 * The editor mode and tool window both read from this object so hover, preview,
 * and apply all use the same explicit-root solve inputs.
 */
UCLASS(Transient)
class ULayoutDirectRootGenerationSettings : public UObject
{
	GENERATED_BODY()

public:
	/** Optional chunk world that must match the hovered target before preview or apply can run. */
	UPROPERTY(EditAnywhere, Category = "Target", meta = (ToolTip = "Optional chunk world that must match the hovered target before preview or apply can run. Leave empty to accept any hovered chunk world."))
	TObjectPtr<AChunkWorldExtended> TargetChunkWorld = nullptr;

	/** Root or continuation profile solved when the hovered location is previewed or applied. */
	UPROPERTY(EditAnywhere, Category = "Layout", meta = (ToolTip = "Root or continuation profile solved when the hovered location is previewed or applied."))
	TObjectPtr<ULayoutProfileAsset> LayoutProfile = nullptr;

	/** Optional authored world binding that supplies shared lattice and placement policy for the active preview/apply path. */
	UPROPERTY(EditAnywhere, Category = "Layout", meta = (ToolTip = "Optional authored world binding that supplies shared lattice, placement policy, and continuation-family context for the active preview/apply path. When assigned, Layout Generator rebuilds the explicit-root request from this binding-owned contract instead of relying only on profile-local inputs."))
	TObjectPtr<ULayoutWorldBindingAsset> LayoutWorldBinding = nullptr;

	/** If true, derive the solve seed from the hovered site position and chunk-world seed. */
	UPROPERTY(EditAnywhere, Category = "Solve", meta = (ToolTip = "If true, derive the solve seed from the hovered site position and chunk-world seed. Disable this only when intentionally forcing a manual preview seed."))
	bool bUseSiteDerivedSeed = true;

	/** Manual seed used when site-derived seeding is disabled. */
	UPROPERTY(EditAnywhere, Category = "Solve", meta = (EditCondition = "!bUseSiteDerivedSeed", ToolTip = "Manual seed used when site-derived seeding is disabled."))
	int32 ManualSolveSeed = 1337;

	/** Enables explicit debug application of retained failed placements without certifying their solve. */
	UPROPERTY(EditAnywhere, Category = "Solve", meta = (DisplayName = "Allow Invalid Preview Apply", ToolTip = "Enable Apply Invalid Preview for retained failed placements. Normal Apply remains blocked. Does not bypass world/write-plan guards or certify the geometry."))
	bool bAllowPartialPreviewApply = true;

	/** Maximum trace distance used when projecting the editor cursor into the chunk world. */
	UPROPERTY(EditAnywhere, Category = "Viewport", meta = (ClampMin = "1000.0", UIMin = "1000.0", ToolTip = "Maximum trace distance used when projecting the editor cursor into the chunk world."))
	float HoverTraceDistance = 500000.0f;

	/** If true, draw a block-aligned hover square before any solve is triggered. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw a block-aligned hover square before any solve is triggered."))
	bool bDrawHoverSquare = true;

	/** If true, draw solved root bounds or one colored bound per continuation segment. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw cached root footprint bounds or per-segment continuation bounds. Continuation colors: yellow=full, orange=retained partial, red=failed."))
	bool bDrawSolvedFootprint = true;

	/** If true, draw intent-colored spheres plus compact local and terrain-adjusted placement-zone borders. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw intent-colored center spheres, compact placement-zone wire borders, and terrain retaining seam face squares for root preview or each continuation segment with solve data. Zone borders: green=Perimeter, coral-red=Edge, purple=Corner, blue=Interior, orange=Core."))
	bool bDrawCellZoneMarkers = true;

	/** If true, draw bright yellow pyramids on cells that have solver-classified OuterBoundary faces, pointing in the boundary direction. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw bright yellow pyramids on solver-classified OuterBoundary faces for root preview or each continuation segment with solve data. Each pyramid points outward."))
	bool bDrawBoundaryFacePyramids = true;

	/** If true, draw a red diagonal X on cells that have no solved placement commitment. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw a red diagonal X through cells without a solved placement for root preview or each continuation segment with solve data."))
	bool bDrawUnoccupiedCellMarkers = true;

	/** If true, draw teal doorway-like wire boxes on outward faces of exported entry cells. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw small teal wire boxes on outward faces of planned Entry cells for root preview or each continuation segment with solve data."))
	bool bDrawEntryMarkers = true;

	/** If true, draw lime-green wire boxes on the ceiling face of vertical-ascent cells. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw small lime-green wire boxes on top faces of VerticalAccess cells for root preview or each continuation segment with solve data."))
	bool bDrawVerticalAccessMarkers = true;

	/** Coalesces contiguous, same-color terrain writes without changing the frozen write plan. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Performance", meta = (ToolTip = "Draw adjacent terrain-write blocks as row bounds instead of individual voxel boxes. Preserves holes and clear/fill colors. Disable to inspect individual blocks; large previews can submit millions of lines in that mode."))
	bool bMergeTerrainPreviewBlocks = true;

	/** Pixel radius below which cell diagnostics use intent points instead of detailed wire markers. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Performance", meta = (ClampMin = "0", ToolTip = "Minimum projected intent-sphere radius in pixels for detailed cell markers. Smaller cells show intent-colored points when Cell Zone Markers is enabled; zoom in to inspect all marker layers. Set 0 for full detail at every distance. Footprints, routes and interface diagnostics remain visible."))
	float MinimumDetailedMarkerRadiusPixels = 4.0f;

	/** If true, draw cached child-region bounds from the recursive schedule result. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw child-region footprint bounds from root preview or each continuation segment schedule."))
	bool bDrawChildBounds = true;

	/** If true, draw cached closure segments and partition seams from the merged recursive result. */
	UPROPERTY(EditAnywhere, Category = "Viewport|Layers", meta = (ToolTip = "If true, draw closure segments and partition seams from root preview or each continuation segment with solve data."))
	bool bDrawInterfaceBounds = true;
};
