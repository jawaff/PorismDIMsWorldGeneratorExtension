// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Layout/Types/LayoutTypes.h"
#include "Misc/DataValidation.h"

#include "LayoutProfileAsset.generated.h"

/**
 * High-level planner settings that describe how one family of layouts should
 * claim a footprint and what broad constraints the module solve must respect.
 */
UCLASS(BlueprintType)
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutProfileAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Validates the profile constraints without running a full layout solve. */
	UFUNCTION(BlueprintCallable, Category = "Layout")
	FLayoutValidationResult ValidateProfile() const;

#if WITH_EDITOR
	/** Generates or refreshes a hard perimeter closure draft using the current preferred bounds mode. */
	void GeneratePerimeterClosureDraftInEditor();

	/** Runs profile validation immediately and publishes the result to the editor Message Log. */
	UFUNCTION(CallInEditor, Category = "Layout|Validation", meta = (DisplayName = "Validate Layout Profile", DisplayPriority = "1", ToolTip = "Runs this profile's validation immediately and reports warnings or errors to the Porism Layout Message Log."))
	void ValidateLayoutProfileInEditor() const;
#endif

	/** Minimum footprint size in cells the planner may choose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", ToolTip = "Minimum footprint size in cells the planner may choose."))
	FIntPoint MinimumFootprintInCells = FIntPoint(3, 3);

	/** Maximum footprint size in cells the planner may choose. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", ToolTip = "Maximum footprint size in cells the planner may choose."))
	FIntPoint MaximumFootprintInCells = FIntPoint(5, 5);

	/** Number of vertical cell layers the planner should solve. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of vertical cell layers the planner should solve."))
	int32 LevelCount = 2;

	/** Count mode used when reserving entry cells on the base footprint boundary. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Entries", meta = (ToolTip = "Controls whether the planner reserves no entry cells, an exact number, or a deterministic seeded count in a range."))
	ELayoutCountConstraintMode EntryCountMode = ELayoutCountConstraintMode::Exact;

	/** Exact number of entry cells reserved when Entry Count Mode is Exact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Entries", meta = (EditCondition = "EntryCountMode == ELayoutCountConstraintMode::Exact", EditConditionHides, ClampMin = "1", UIMin = "1", ToolTip = "Exact number of entry cells the planner reserves on the base footprint boundary. With entry-module restriction enabled, this controls the exact number of authored door or gate openings."))
	int32 EntryCount = 1;

	/** Minimum number of entry cells reserved when Entry Count Mode is Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Entries", meta = (EditCondition = "EntryCountMode == ELayoutCountConstraintMode::Range", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Minimum entry cells the planner may reserve when Entry Count Mode is Range."))
	int32 MinEntryCount = 1;

	/** Maximum number of entry cells reserved when Entry Count Mode is Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Entries", meta = (EditCondition = "EntryCountMode == ELayoutCountConstraintMode::Range", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Maximum entry cells the planner may reserve when Entry Count Mode is Range."))
	int32 MaxEntryCount = 1;

	/** Count mode used when reserving stairs, ramps, ladders, or other vertical-access cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Vertical Access", meta = (ToolTip = "Controls whether the planner reserves no vertical-access cells, an exact number, or a deterministic seeded count in a range. Use this for stairs, ramps, ladders, or other modules that connect traversable layers."))
	ELayoutCountConstraintMode VerticalAccessCountMode = ELayoutCountConstraintMode::None;

	/** Exact vertical-access cell count reserved when Vertical Access Count Mode is Exact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Vertical Access", meta = (EditCondition = "VerticalAccessCountMode == ELayoutCountConstraintMode::Exact", EditConditionHides, ClampMin = "1", UIMin = "1", ToolTip = "Exact number of vertical-access cells the planner reserves. Use this to get exactly one stairwell or a fixed number of ramps/ladders."))
	int32 VerticalAccessCount = 1;

	/** Minimum vertical-access cell count reserved when Vertical Access Count Mode is Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Vertical Access", meta = (EditCondition = "VerticalAccessCountMode == ELayoutCountConstraintMode::Range", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Minimum vertical-access cells the planner may reserve when Vertical Access Count Mode is Range."))
	int32 MinVerticalAccessCount = 1;

	/** Maximum vertical-access cell count reserved when Vertical Access Count Mode is Range. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Vertical Access", meta = (EditCondition = "VerticalAccessCountMode == ELayoutCountConstraintMode::Range", EditConditionHides, ClampMin = "0", UIMin = "0", ToolTip = "Maximum vertical-access cells the planner may reserve when Vertical Access Count Mode is Range."))
	int32 MaxVerticalAccessCount = 1;

	/** If true, vertical-access modules can only be placed in planned Vertical Access cells. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Vertical Access", meta = (EditCondition = "VerticalAccessCountMode != ELayoutCountConstraintMode::None", EditConditionHides, ToolTip = "If true, modules with the VerticalAccess role can only fill planned Vertical Access cells. Keep enabled to prevent too many stair/ramp/ladder modules from appearing."))
	bool bRestrictVerticalAccessModulesToVerticalAccessCells = true;

	/** If true, this profile can support terrain-driven stepped active-cell solving. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Terrain Capability", meta = (ToolTip = "If true, this profile can support terrain-driven stepped active-cell solving when a world-binding placement policy requests it. Single-level profiles use pseudo joining cells; multi-level profiles use authored real levels. VerticalAccess-capable content is required only by authored VerticalAccess counts or required traversal reachability."))
	bool bSupportsSteppedTerrainSolve = false;

	/** If true, stepped terrain planning authors retaining seams and internal terrain-seam gate Entries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Terrain Capability", meta = (DisplayName = "Enable Terrain Seams", EditCondition = "bSupportsSteppedTerrainSolve", EditConditionHides, ToolTip = "Enables terrain-seam planning when stepped terrain shifts layout cells between terrain stages. When enabled, the planner marks retaining seam faces, may select an internal traversable seam Entry, and can place modules authored with Must Face Terrain Seam or Must Face Exterior Or Terrain Seam. Disable this to keep stepped shifting, bridge cells, and Vertical Access while solving those cells with ordinary module face rules; prewarm and debug preview will not produce terrain-seam markers."))
	bool bEnableTerrainSeams = true;

	/** If true, this profile is designed for underground placement in empty cavities below the biome surface. Requires pocket/void interval evidence from terrain sampling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Terrain Capability", meta = (ToolTip = "If true, this profile is designed for underground placement in empty cavities below the biome surface. Requires pocket/void interval evidence from terrain sampling."))
	bool bUndergroundPlacement = false;

	/** Optional explicit traversable entry level for world-facing continuation alignment. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Connectors", meta = (ClampMin = "-1", UIMin = "-1", ToolTip = "Optional explicit local continuation-entry level used when a world-binding continuation family aligns this profile to an incoming or outgoing route. Use -1 when this profile is not intended for deterministic continuation-entry alignment."))
	int32 ContinuationEntryLevel = INDEX_NONE;

	/** Unified content set used by this profile for module-or-child authoring. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout", meta = (ToolTip = "Unified content set used by this profile for module-or-child authoring."))
	TObjectPtr<class ULayoutRegionContentSetAsset> ContentSet = nullptr;

	/** Optional explicit closure requirements for recursive perimeter scenarios. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Closure", meta = (ToolTip = "Optional explicit closure requirements for recursive perimeter scenarios. Leave empty for layouts that do not require sealed boundary coverage. Any listed requirement must be satisfied by compatible region or module content that can cover the required area.")) 
	TArray<FLayoutClosureRequirement> ClosureRequirements;

	/** Counted feature requirements evaluated against placed content ownership within one zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Features", meta = (ToolTip = "Counted feature requirements evaluated against placed content ownership within one placement zone. Use these for windows, gates, towers, or child-room feature presence instead of overloading closure rules."))
	TArray<FLayoutZoneFeatureRequirement> ZoneFeatureRequirements;

	/** Typed terrain-preservation and sparse-placement rules evaluated in authored order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Sparse Placement", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.LayoutSparsePlacementRuleBase", ExcludeBaseStruct, ToolTip = "Add Preserve Supported Terrain, Exact, Range, or Fill Available rules. Preserve-producing rules expose matching terrain to structural children, routes, and VerticalAccess before optional sparse placement consumes leftovers."))
	TArray<FInstancedStruct> SparsePlacementRules;

	/** Explicit level-scoped fill behavior applied before route and module solving begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Fill", meta = (ToolTip = "Explicit level-scoped fill behavior applied before route and module solving begins. Use this to replace legacy upper-level boundary-only behavior with an authored level rule."))
	TArray<FLayoutLevelFillRule> LevelFillRules;

	/** Deterministic hard-open-space rules that remove matching cells from the structural plan before fill begins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Fill", meta = (ToolTip = "Deterministic hard-open-space rules that remove matching cells from the structural plan before fill begins. Use these for courtyards, yards, pits, or other authored voids that should not enter the normal fill solve."))
	TArray<FLayoutReservedOpenSpaceRule> ReservedOpenSpaceRules;

	/** If true, every authored traversal channel must be connected to an entry root before the solve can succeed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Layout|Solver", meta = (ToolTip = "If true, every authored traversal channel in the solved layout must be reachable from an entry cell. Leave disabled while authoring exterior shells, decorative traversal metadata, or early module tests that should still stamp into the world."))
	bool bRequireAllTraversalChannelsReachable = false;

#if WITH_EDITOR
	/** Reports editor validation failures early so invalid layout profiles are visible before runtime. */
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
