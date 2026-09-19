// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Layout/Types/LayoutLocalBlockCoordinates.h"
#include "Layout/Types/LayoutTypes.h"

#include "LayoutContractTypes.generated.h"

/** High-level scope that determines which inherited obligations one region contract may receive. */
UENUM(BlueprintType)
enum class ELayoutContractRegionScope : uint8
{
	Root,
	Child,
	Continuation
};

/** Environment adapter selected before region-contract proof starts. */
UENUM(BlueprintType)
enum class ELayoutContractEnvironmentMode : uint8
{
	StandardRegion,
	NonSteppedWorldPlacement,
	SteppedSurfacePlacement,
	BridgeContinuation,
	TunnelContinuation,
	UndergroundPocketPlacement,
	ChildRegion
};

/** Terrain-side ownership of one local layout cell in the frozen contract. */
UENUM(BlueprintType)
enum class ELayoutFrozenTerrainCellContract : uint8
{
	Active,
	Removed,
	FlatClearance,
	BridgeSpan,
	TunnelExcavation,
	Forbidden
};

/** One active cell admitted by the terrain adapter before CSP module selection. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractActiveCellRecord
{
	GENERATED_BODY()

	/** Region-local cell id admitted into the solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Region-local cell id admitted into the solve."))
	FIntVector Cell = FIntVector::ZeroValue;
};

/** Stable local terrain-cell obligation emitted before module-domain solving begins. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainCellContractRecord
{
	GENERATED_BODY()

	/** Region-local cell covered by this terrain contract record. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Region-local cell covered by this terrain contract record."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Frozen terrain behavior assigned to the local cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Frozen terrain behavior assigned to the local cell."))
	ELayoutFrozenTerrainCellContract Contract = ELayoutFrozenTerrainCellContract::Active;

	/** True when placement evidence proved foundation fill is authorized for this terrain cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when placement evidence proved foundation fill is authorized for this terrain cell."))
	bool bHasFoundationFillEvidence = false;

	/** Frozen number of missing support blocks below this cell when foundation fill is authorized. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of missing support blocks below this cell that foundation realization must fill."))
	int32 RequiredFoundationDepth = 0;

	/** Frozen material used by bounded foundation writes for this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Material frozen from nearest final-occupancy support and used for bounded foundation writes."))
	int32 FoundationMaterial = 0;

	/** True when placement evidence proved clearance is authorized for this terrain cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when placement evidence proved clearance is authorized for this terrain cell."))
	bool bHasClearanceEvidence = false;

	/** True when placement evidence proved ramp or perimeter transition support is authorized for this terrain cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when placement evidence proved ramp or perimeter transition support is authorized for this terrain cell."))
	bool bHasRampTransitionEvidence = false;

	/** True when placement evidence proved bridge/support authority for this terrain cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when placement evidence proved bridge/support authority for this terrain cell."))
	bool bHasBridgeSupportEvidence = false;

	/** True when placement evidence proved local terrain/template overlap requiring excavation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when placement evidence proved local terrain/template overlap requiring excavation."))
	bool bHasExcavationOverlapZ = false;

	/** Minimum local cell Z block touched by terrain/template overlap; valid only when bHasExcavationOverlapZ is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Minimum local cell Z block touched by terrain/template overlap; valid only when bHasExcavationOverlapZ is true."))
	FLayoutLocalBlockCoord8 ExcavationOverlapMinLocalZ;

	/** Maximum local cell Z block touched by terrain/template overlap; valid only when bHasExcavationOverlapZ is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Maximum local cell Z block touched by terrain/template overlap; valid only when bHasExcavationOverlapZ is true."))
	FLayoutLocalBlockCoord8 ExcavationOverlapMaxLocalZ;

	/** True when this boundary cell is walkable for entry placement (outside-perimeter verdict). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when this boundary cell is walkable for entry placement (outside-perimeter verdict)."))
	bool bEntryWalkable = true;
};

/** Stable terrain-write obligation emitted by contract proof and consumed later by realization. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenTerrainWriteRecord
{
	GENERATED_BODY()

	/** Block-world position that realization may write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Block-world position that realization may write."))
	FIntVector BlockWorldPos = FIntVector::ZeroValue;

	/** Material payload, or preview hint when realization samples support material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Material payload; preview hint only when support material is resolved at realization."))
	int32 Material = 0;

	/** Fill geometry is frozen, but material comes from fresh terrain immediately before writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Sample support material at realization. Prewarm must not read loaded terrain for this fill."))
	bool bResolveMaterialFromTerrain = false;

	/** Procedural support coordinate beneath this fill column; not a write target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (EditCondition = "bResolveMaterialFromTerrain", ToolTip = "Frozen support block beneath the fill column, sampled for material once its chunk is ready."))
	FIntVector MaterialSourceBlockWorldPos = FIntVector::ZeroValue;

	/** Maximum downward material-only search from the predicted support; geometry remains immutable. Zero keeps exact lookup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ClampMin = "0", EditCondition = "bResolveMaterialFromTerrain", ToolTip = "Frozen downward search allowance copied from the terrain transition depth. Uses the first solid material in this column without moving or extending any terrain writes."))
	int32 MaterialSourceSearchDepthBlocks = 0;

	/** Terrain-cell contract that authorized this write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Terrain-cell contract that authorized this write."))
	ELayoutFrozenTerrainCellContract SourceContract = ELayoutFrozenTerrainCellContract::Active;
};

/** One stable validation assertion carried into a cached contract manifest. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifestValidationAssertion
{
	GENERATED_BODY()

	/** Stable assertion id copied from validation/snapshot setup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable assertion id copied from validation and snapshot setup."))
	FLayoutId AssertionId;

	/** Invariant category preserved for later contract and certificate checks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Invariant category preserved for later contract and certificate checks."))
	ELayoutValidationAssertionKind AssertionKind = ELayoutValidationAssertionKind::AssetValidationPassed;

	/** Whether the captured assertion passed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Whether the captured assertion passed."))
	bool bPassed = true;

	/** Stable ids cited by this assertion. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable ids cited by this assertion."))
	TArray<FLayoutId> RelatedIds;
};

/** Stable summary of one unified content entry copied into the contract manifest. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifestContentEntrySummary
{
	GENERATED_BODY()

	/** Stable content-entry id used for deterministic matching, ownership, and diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable content-entry id used for deterministic matching, ownership, and diagnostics."))
	FName EntryId;

	/** Whether this entry resolves to a module snapshot or a child-region request template. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Whether this entry resolves to a module snapshot or a child-region request template."))
	ELayoutRegionContentKind ContentKind = ELayoutRegionContentKind::Module;

	/** Deterministic selection weight copied from authored content data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Deterministic selection weight copied from authored content data."))
	int32 Weight = 1;

	/** Feature tags this content entry provides toward parent feature ownership counts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (Categories = "Layout.Feature", ToolTip = "Feature tags this content entry provides toward parent feature ownership counts."))
	FGameplayTagContainer ProvidedZoneFeatures;

	/** Closure-provider intents copied from the authored content entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Closure-provider intents copied from the authored content entry."))
	TArray<FLayoutClosureProviderIntent> ClosureProviderIntents;

	/** Seam-provider intents copied from the authored content entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Seam-provider intents copied from the authored content entry."))
	TArray<FLayoutSeamProviderIntent> SeamProviderIntents;

	/** Compiled module snapshot index when this is a module entry. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Compiled module snapshot index when this is a module entry."))
	int32 ModuleSnapshotIndex = INDEX_NONE;

	/** Placement zone copied from module-side authored entry settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Placement zone copied from module-side authored entry settings."))
	ELayoutPlacementZone ModulePlacementZone = ELayoutPlacementZone::Any;

	/** Level policy copied from module-side authored entry settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Level policy copied from module-side authored entry settings."))
	ELayoutLevelPlacementPolicy ModuleLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific module level copied when the authored policy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Specific module level copied when the authored policy is SpecificLevel."))
	int32 ModuleSpecificLevel = 0;

	/** True when the authored module entry was optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the authored module entry was optional."))
	bool bModuleOptional = false;

	/** Child profile snapshot id when this entry resolves to a child region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Child profile snapshot id when this entry resolves to a child region."))
	FLayoutId ChildProfileSnapshotId;

	/** Stable child profile soft path when this entry resolves to a child region. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable child profile soft path when this entry resolves to a child region."))
	FSoftObjectPath ChildProfilePath;

	/** Child content-set snapshot id when a compiled child request template exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Child content-set snapshot id when a compiled child request template exists."))
	FLayoutId ChildContentSetSnapshotId;

	/** Placement zone copied from child-side authored entry settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Placement zone copied from child-side authored entry settings."))
	ELayoutPlacementZone ChildPlacementZone = ELayoutPlacementZone::Any;

	/** Level policy copied from child-side authored entry settings. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Level policy copied from child-side authored entry settings."))
	ELayoutLevelPlacementPolicy ChildLevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific child level copied when the authored policy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Specific child level copied when the authored policy is SpecificLevel."))
	int32 ChildSpecificLevel = 0;

	/** True when the authored child entry was optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the authored child entry was optional."))
	bool bChildOptional = false;

	/** True when the authored child entry may satisfy host VerticalAccess. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the authored child entry may satisfy host VerticalAccess."))
	bool bChildContributesHostVerticalAccess = false;
};

/** Stable summary of one compiled module snapshot copied into the contract manifest. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifestModuleSummary
{
	GENERATED_BODY()

	/** Stable module snapshot id used for later contract and proof matching. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable module snapshot id used for later contract and proof matching."))
	FLayoutId SnapshotId;

	/** Stable human-readable snapshot name preserved for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable human-readable snapshot name preserved for diagnostics."))
	FName DebugName;

	/** Template soft path preserved for realization-facing manifest diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Template soft path preserved for realization-facing manifest diagnostics."))
	FSoftObjectPath TemplatePath;

	/** Bounds of the canonical module or composite footprint in local cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Bounds of the canonical module or composite footprint in local cells."))
	FIntVector BoundsCells = FIntVector(1, 1, 1);

	/** Every occupied local cell reserved by this module or composite snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Every occupied local cell reserved by this module or composite snapshot."))
	TArray<FIntVector> OccupiedLocalCells;

	/** Solver roles copied from the compiled snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Solver roles copied from the compiled snapshot."))
	TArray<ELayoutModuleRole> Roles;

	/** Traversal channels exposed by the compiled snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Traversal channels exposed by the compiled snapshot."))
	FGameplayTagContainer TraversalChannels;

	/** Content-entry id that supplied this compiled module snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Content-entry id that supplied this compiled module snapshot."))
	FName SourceContentEntryId;

	/** Placement zone copied from the content-entry settings that supplied this snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Placement zone copied from the content-entry settings that supplied this snapshot."))
	ELayoutPlacementZone PlacementZone = ELayoutPlacementZone::Any;

	/** Level policy copied from the content-entry settings that supplied this snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Level policy copied from the content-entry settings that supplied this snapshot."))
	ELayoutLevelPlacementPolicy LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;

	/** Specific level copied when the authored policy is SpecificLevel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Specific level copied when the authored policy is SpecificLevel."))
	int32 SpecificLevel = 0;

	/** True when the source content entry was optional. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the source content entry was optional."))
	bool bOptional = false;

	/** True when this compiled snapshot represents a multi-cell composite module. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when this compiled snapshot represents a multi-cell composite module."))
	bool bCompositeSnapshot = false;
};

/** Static, terrain-independent profile/content summary reused across candidate sites. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutContractManifest
{
	GENERATED_BODY()

	/** Stable id for the authored profile/content/module snapshot summarized by this manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for the authored profile/content/module snapshot summarized by this manifest."))
	FLayoutId ManifestId;

	/** Stable profile snapshot id copied into the manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable profile snapshot id copied into the manifest."))
	FLayoutId ProfileSnapshotId;

	/** Stable soft path of the source profile when a pointer-free path is already available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable soft path of the source profile when a pointer-free path is already available."))
	FSoftObjectPath ProfileSourcePath;

	/** Stable content-set snapshot id copied into the manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable content-set snapshot id copied into the manifest."))
	FLayoutId ContentSetSnapshotId;

	/** Stable module-set snapshot id copied into the manifest. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable module-set snapshot id copied into the manifest."))
	FLayoutId ModuleCatalogId;

	/** Stable owning world-binding id copied from the frozen request boundary when one exists. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable owning world-binding id copied from the frozen request boundary when one exists."))
	FName WorldBindingId;

	/** Shared cell dimensions captured from the solved source snapshots. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Shared cell dimensions captured from the solved source snapshots."))
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Minimum authored footprint that a location adapter must be able to host. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Minimum authored footprint that a location adapter must be able to host."))
	FIntPoint MinimumFootprintInCells = FIntPoint::ZeroValue;

	/** Maximum authored footprint considered while generating candidate bounds. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Maximum authored footprint considered while generating candidate bounds."))
	FIntPoint MaximumFootprintInCells = FIntPoint::ZeroValue;

	/** Number of authored solved levels copied from the profile snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Number of authored solved levels copied from the profile snapshot."))
	int32 LevelCount = 1;

	/** True when the authored profile supports stepped-terrain solve paths. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the authored profile supports stepped-terrain solve paths."))
	bool bSupportsSteppedTerrainSolve = false;

	/** True when the authored profile enables terrain-seam planning for stepped terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the authored profile enables terrain-seam planning for stepped terrain."))
	bool bEnableTerrainSeams = true;

	/** Exact authored VerticalAccess count requirement when one is known, or INDEX_NONE while legacy paths still own the proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Exact authored VerticalAccess count requirement when one is known, or INDEX_NONE while legacy paths still own the proof."))
	int32 RequiredVerticalAccessCount = INDEX_NONE;

	/** Stable closure requirement ids copied from the authored profile snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable closure requirement ids copied from the authored profile snapshot."))
	TArray<FLayoutId> ClosureRequirementIds;

	/** Stable feature requirement ids copied from the authored profile snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable feature requirement ids copied from the authored profile snapshot."))
	TArray<FLayoutId> ZoneFeatureRequirementIds;

	/** Stable validation assertions copied from request/profile/content/module snapshot setup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable validation assertions copied from request, profile, content, and module snapshot setup."))
	TArray<FLayoutContractManifestValidationAssertion> ValidationAssertions;

	/** Stable content-entry summaries copied from the unified content-set snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable content-entry summaries copied from the unified content-set snapshot."))
	TArray<FLayoutContractManifestContentEntrySummary> ContentEntries;

	/** Stable module/composite summaries copied from the compiled module-set snapshot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable module and composite summaries copied from the compiled module-set snapshot."))
	TArray<FLayoutContractManifestModuleSummary> Modules;

};

/** Location-sensitive solve mode chosen after snapping and any accepted coarse placement shift. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutModePlan
{
	GENERATED_BODY()

	/** Region scope for inherited contract semantics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Region scope for inherited contract semantics."))
	ELayoutContractRegionScope Scope = ELayoutContractRegionScope::Root;

	/** Environment adapter selected for admission and environment-specific Entry evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Environment adapter selected for admission and environment-specific Entry evidence."))
	ELayoutContractEnvironmentMode EnvironmentMode = ELayoutContractEnvironmentMode::StandardRegion;

	/** True when this request must use the shared stepped topology pipeline before any classified flat fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Whether this request uses shared stepped stage, bridge, deck, and terrain-seam topology independent of Surface or Underground admission."))
	bool bUsesSteppedTerrainTopology = false;

	/** Stable deterministic id for this exact mode-plan value set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable deterministic id for this exact mode-plan value set."))
	FLayoutId ModePlanId;

	/** Final snapped site center used for deterministic seeding and world anchoring. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Final snapped site center used for deterministic seeding and world anchoring."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Coarse placement shift accepted before region proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Coarse placement shift accepted before region proof."))
	FIntVector PlacementShiftCells = FIntVector::ZeroValue;

	/** Stable id for the accepted placement-shift candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for the accepted placement-shift candidate."))
	FLayoutId PlacementShiftId;

	/** World seed used to derive the final solve seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "World seed used to derive the final solve seed."))
	int32 WorldSeed = 0;

	/** Deterministic seed derived from world seed and final snapped site center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Deterministic seed derived from world seed and final snapped site center."))
	int32 SolveSeed = 0;

	/** World-facing placement kind preserved from the selected adapter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "World-facing placement kind preserved from the selected adapter."))
	ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::None;

	/** Existing world-binding terrain policy copied into the contract; no new config is introduced. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Existing world-binding terrain policy copied into the contract; no new config is introduced."))
	FLayoutWorldBindingPlacementPolicy PlacementPolicy;

	/** Continuation selection when this mode plan represents a bridge, tunnel, or surface path. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Continuation selection when this mode plan represents a bridge, tunnel, or surface path."))
	FLayoutResolvedWorldBindingContinuationSelection ContinuationSelection;
};

/** One frozen terrain-stage column record keyed by footprint XY.
 * Every Real active XY column in SteppedSurfacePlacement must have exactly one stage-map record.
 * Preview, cached apply, and runtime apply share these resolved stage anchors. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenTerrainStageCellRecord
{
	GENERATED_BODY()

	/** Footprint-local XY in cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Footprint-local XY in cells."))
	FIntPoint FootprintCellXY = FIntPoint::ZeroValue;

	/** Lattice terrain stage selected for this XY column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Lattice terrain stage selected for this XY column."))
	int32 TerrainStageIndex = 0;

	/** Exact vertical shift in blocks from the base lattice stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Exact vertical shift in blocks from the base lattice stage."))
	int32 VerticalShiftBlocks = 0;

	/** Global lattice-aligned support-floor Z for this XY column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Global lattice-aligned support-floor Z for this XY column."))
	int32 SnappedSupportFloorZ = 0;

	/** Block-world Z used as base for authored placement levels on this XY column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Block-world Z used as base for authored placement levels on this XY column."))
	int32 ResolvedStageBaseBlockWorldZ = 0;

	/** Source evidence/support provenance id for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Source evidence/support provenance id for diagnostics."))
	FLayoutId SourceEvidenceId;
};

/** Frozen proof that retained terrain fills one otherwise-unplanned horizontal neighbor volume. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainBackedNeighborFaceRecord
{
	GENERATED_BODY()

	/** Final planned cell whose face touches retained terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Final planned cell whose face touches retained terrain."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Horizontal face on Cell backed by retained terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Horizontal face on Cell backed by retained terrain."))
	ELayoutFaceDirection FaceDirection = ELayoutFaceDirection::PosX;

	/** Unplanned neighboring cell volume proven filled by frozen terrain evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Unplanned neighboring cell volume proven filled by frozen terrain evidence."))
	FIntVector NeighborCell = FIntVector::ZeroValue;

	/** Stable frozen support evidence provenance for diagnostics and contract identity. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable frozen support evidence provenance for diagnostics and contract identity."))
	FLayoutId SourceEvidenceId;
};

/** Frozen terrain obligations produced by contract proof and replayed by preview/apply/runtime realization. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenTerrainContract
{
	GENERATED_BODY()

	/** Stable id for this terrain contract artifact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for this terrain contract artifact."))
	FLayoutId ContractId;

	/** Final snapped site center this terrain contract was proven against. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Final snapped site center this terrain contract was proven against."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Block-world minimum footprint anchor chosen before realization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Block-world minimum footprint anchor chosen before realization."))
	FIntVector FootprintMinBlockWorldPos = FIntVector::ZeroValue;

	/** Shared cell dimensions used to interpret local terrain cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Shared cell dimensions used to interpret local terrain cells."))
	FIntVector SharedCellSizeInBlocks = FIntVector::ZeroValue;

	/** Solved or contracted footprint size in cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Solved or contracted footprint size in cells."))
	FIntPoint FootprintSizeInCells = FIntPoint::ZeroValue;

	/** Per-cell terrain behavior frozen before terrain writes are emitted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Per-cell terrain behavior frozen before terrain writes are emitted."))
	TArray<FLayoutTerrainCellContractRecord> CellContracts;

	/** Kind-qualified active cells that authorize realization-prep and terrain-write interpretation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Kind-qualified active cells that authorize realization-prep and terrain-write interpretation."))
	TArray<FLayoutContractActiveCellRecord> ActiveCells;

	/** Frozen per-XY-column terrain stage map populated by the adapter for SteppedSurfacePlacement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Frozen per-XY-column terrain stage map populated by the adapter for SteppedSurfacePlacement."))
	TArray<FLayoutFrozenTerrainStageCellRecord> StageMap;

	/** Face-level retained-terrain occupancy proof consumed by module candidate admission. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Face-level retained-terrain occupancy proof consumed by module candidate admission."))
	TArray<FLayoutTerrainBackedNeighborFaceRecord> TerrainBackedNeighborFaces;

	/** Reserved-open cells and terrain behavior selected before final plan publication. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Reserved-open cells and their frozen terrain behavior. ClearReservedCell entries authorize only their own shared-cell volume."))
	TArray<FLayoutCellReservationRecord> ReservedOpenTerrainReservations;

	/** Terrain writes authorized by the frozen contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Terrain writes authorized by the frozen contract."))
	TArray<FLayoutFrozenTerrainWriteRecord> TerrainWrites;

	/** Removed local cells that must not receive features, children, or terrain writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Removed local cells that must not receive features, children, or terrain writes."))
	TArray<FIntVector> RemovedCells;

	/** Occupied active cells that terrain realization must protect unless clearance/excavation owns the write. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Occupied active cells that terrain realization must protect unless clearance/excavation owns the write."))
	TArray<FIntVector> ProtectedOccupiedCells;

	/** Diagnostic that mirrors current terrain-fit reporting until the old terrain-fit branch is removed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Diagnostic that mirrors current terrain-fit reporting until the old terrain-fit branch is removed."))
	ELayoutWorldBindingTerrainFitDiagnosticKind DiagnosticKind = ELayoutWorldBindingTerrainFitDiagnosticKind::None;

};

/** Adapter-stage authoring diagnostic retained when precompute rejects before solver submission. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutAdapterDiagnostic
{
	GENERATED_BODY()

	/** Primary local cell involved in the adapter decision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Primary local cell involved in the adapter decision."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** Related local cell for an edge conflict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Related local cell for an edge conflict."))
	FIntVector RelatedCell = FIntVector::ZeroValue;

	/** True when RelatedCell defines a diagnostic edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when RelatedCell defines a diagnostic edge."))
	bool bHasRelatedCell = false;

	/** Exact adapter requirement or failed terrain/module contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Exact adapter requirement or failed terrain/module contract."))
	FString Detail;
};

/** Adapter result passed from environment-specific classification into shared region proof. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutAdapterOutput
{
	GENERATED_BODY()

	/** True when the adapter produced a usable contract seed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when the adapter produced a usable contract seed."))
	bool bSucceeded = false;

	/** Stable failure reason when the adapter rejects a candidate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable failure reason when the adapter rejects a candidate."))
	FString FailureReason;

	/** Mode plan produced by the selected adapter. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Mode plan produced by the selected adapter."))
	FLayoutModePlan ModePlan;

	/** Planned cells emitted by the adapter before inner proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Planned cells emitted by the adapter before inner proof."))
	TArray<FLayoutPlannedCell> PlannedCells;

	/** Preview-only adapter diagnostics. Never authorize solve apply or realization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Preview-only adapter diagnostics. Never authorize solve apply or realization."))
	TArray<FLayoutAdapterDiagnostic> Diagnostics;

	/** True when stepped Entry and VerticalAccess intents have been finalized for these cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when stepped Entry and VerticalAccess intents are final and must not be recomputed by later request paths."))
	bool bHasFinalizedSteppedTerrainIntents = false;

	/** Frozen alternatives for each stepped VerticalAccess requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Frozen alternatives for each stepped VerticalAccess requirement."))
	TArray<FLayoutVerticalAccessHostGroup> VerticalAccessHostGroups;

	/** Kind-qualified active cells emitted by the adapter before inner proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Kind-qualified active cells emitted by the adapter before inner proof."))
	TArray<FLayoutContractActiveCellRecord> ActiveCells;

	/** Stepped terrain support frozen by the adapter when terrain stepping is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stepped terrain support frozen by the adapter when terrain stepping is active."))
	FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

	/** Terrain obligations produced by the adapter before module-domain proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Terrain obligations produced by the adapter before module-domain proof."))
	FLayoutFrozenTerrainContract FrozenTerrainContract;

	/** Pre-qualified entry cells for the solver, subset of walkable footprint boundary cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Pre-qualified entry cells for the solver, subset of walkable footprint boundary cells."))
	TArray<FIntVector> QualifiedEntryCells;
};

/** Shared root/child/continuation contract consumed by the existing low-level solver. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutRegionContract
{
	GENERATED_BODY()

	/** Static authored-data summary used by this region contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Static authored-data summary used by this region contract."))
	FLayoutContractManifest Manifest;

	/** Location-sensitive mode chosen for this solve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Location-sensitive mode chosen for this solve."))
	FLayoutModePlan ModePlan;

	/** Contracted footprint passed to inner proof. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Contracted footprint passed to inner proof."))
	FIntPoint FootprintSize = FIntPoint::ZeroValue;

	/** Active planned cells after terrain contraction or flat clearance classification. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Active planned cells after terrain contraction or flat clearance classification."))
	TArray<FLayoutPlannedCell> PlannedCells;

	/** True when stepped Entry and VerticalAccess intents are final for this contract. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "True when stepped Entry and VerticalAccess intents are final and must not be recomputed by later request paths."))
	bool bHasFinalizedSteppedTerrainIntents = false;

	/** Frozen alternatives for each stepped VerticalAccess requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Frozen alternatives for each stepped VerticalAccess requirement."))
	TArray<FLayoutVerticalAccessHostGroup> VerticalAccessHostGroups;

	/** Kind-qualified active cells admitted by the adapter before CSP content selection. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Kind-qualified active cells admitted by the adapter before CSP content selection."))
	TArray<FLayoutContractActiveCellRecord> ActiveCells;

	/** Stable id for the planned active-cell mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for the planned active-cell mask."))
	FLayoutId ActiveMaskId;

	/** Stable id for the removed-cell mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for the removed-cell mask."))
	FLayoutId RemovedMaskId;

	/** Stable id for the VerticalAccess frontier mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for the VerticalAccess frontier mask."))
	FLayoutId VerticalAccessMaskId;

	/** Stable id for the protected traversal-cell mask. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for the protected traversal-cell mask."))
	FLayoutId ProtectedTraversalMaskId;

	/** Stable id for planned-cell stage/intent mapping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Stable id for planned-cell stage/intent mapping."))
	FLayoutId StageMapId;

	/** Local cells removed before inner proof and forbidden to later placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Local cells removed before inner proof and forbidden to later placement."))
	TArray<FIntVector> RemovedCells;

	/** Local cells that satisfy or reserve VerticalAccess obligations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Local cells that satisfy or reserve VerticalAccess obligations."))
	TArray<FIntVector> VerticalAccessCells;

	/** Local entry/traversal cells that must stay reachable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Local entry/traversal cells that must stay reachable."))
	TArray<FIntVector> ProtectedTraversalCells;

	/** Request-owned stepped terrain support map that inner proof may consume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Request-owned stepped terrain support map that inner proof may consume."))
	FLayoutSteppedTerrainSupportMap SteppedTerrainSupportMap;

	/** Frozen terrain obligations replayed by editor preview, cached apply, and runtime realization. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Contract", meta = (ToolTip = "Frozen terrain obligations replayed by editor preview, cached apply, and runtime realization."))
	FLayoutFrozenTerrainContract FrozenTerrainContract;

};
