// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"
#include "Layout/Planning/LayoutReservationPocketPlanning.h"
#include "Layout/Types/LayoutLocalBlockCoordinates.h"
#include "Layout/Types/LayoutTypes.h"

#include "LayoutTerrainSampling.generated.h"

class AChunkWorldCore;
class FLayoutActiveBiomeSampler;
struct FLayoutActiveBiomeSample;
struct FLayoutNoiseCoordinateSettings;

/** Terrain-shape classification for one sampled footprint column. */
UENUM(BlueprintType)
enum class ELayoutTerrainCellShape : uint8
{
	Flat,
	MildSlope,
	SteepEdge
};

/** One sampled footprint column plus its derived terrain-shape classification. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainCellClassification
{
	GENERATED_BODY()

	/** Block-world X/Y column represented by this classification. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Block-world X/Y column represented by this classification."))
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** Supporting terrain Z discovered for this column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Supporting terrain Z discovered for this column."))
	int32 SurfaceZ = 0;

	/** Derived terrain shape for this sampled column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Derived terrain shape for this sampled column."))
	ELayoutTerrainCellShape Shape = ELayoutTerrainCellShape::Flat;
};

/** One inferred vertical transition between two neighboring sampled columns. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainTransitionRequirement
{
	GENERATED_BODY()

	/** Starting sampled column of the inferred transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Starting sampled column of the inferred transition."))
	FIntPoint FromBlockXY = FIntPoint::ZeroValue;

	/** Neighbor sampled column reached by the inferred transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Neighbor sampled column reached by the inferred transition."))
	FIntPoint ToBlockXY = FIntPoint::ZeroValue;

	/** Vertical step height between the sampled columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Vertical step height between the sampled columns."))
	int32 StepHeight = 0;
};

/** Classification and hill-transition hints derived from one sampled footprint. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainClassificationResult
{
	GENERATED_BODY()

	/** Per-column terrain classifications derived from the sampled footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Per-column terrain classifications derived from the sampled footprint."))
	TArray<FLayoutTerrainCellClassification> CellClassifications;

	/** Mild-slope transitions that could later be satisfied by stairs, ramps, or stacked cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Mild-slope transitions that could later be satisfied by stairs, ramps, or stacked cells."))
	TArray<FLayoutTerrainTransitionRequirement> TransitionRequirements;

	/** True when the sampled footprint includes at least one supportable mild slope transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the sampled footprint includes at least one supportable mild slope transition."))
	bool bHasMildSlopeTransitions = false;

	/** True when the sampled footprint includes at least one steep edge that should be terraced or rejected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the sampled footprint includes at least one steep edge that should be terraced or rejected."))
	bool bHasSteepEdges = false;
};

/** One outer-perimeter terrain column paired with the sampled footprint edge it touches. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainPerimeterTransition
{
	GENERATED_BODY()

	/** Sampled footprint edge column that this perimeter column touches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Sampled footprint edge column that this perimeter column touches."))
	FIntPoint AdjacentFootprintBlockXY = FIntPoint::ZeroValue;

	/** Immediately adjacent outer-perimeter terrain column next to the sampled footprint edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Immediately adjacent outer-perimeter terrain column next to the sampled footprint edge."))
	FIntPoint PerimeterBlockXY = FIntPoint::ZeroValue;

	/** Supporting terrain Z discovered under the sampled footprint edge column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Supporting terrain Z discovered under the sampled footprint edge column."))
	int32 FootprintSurfaceZ = 0;

	/** Supporting terrain Z discovered under the adjacent outer-perimeter column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Supporting terrain Z discovered under the adjacent outer-perimeter column."))
	int32 PerimeterSurfaceZ = 0;

	/** Absolute vertical delta between the footprint edge and outer-perimeter columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Absolute vertical delta between the footprint edge and outer-perimeter columns."))
	int32 StepHeight = 0;

	/** True when this boundary step stays within the currently supportable ordinary terrain delta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when this boundary step stays within the currently supportable ordinary terrain delta."))
	bool bIsSupportableTransition = false;
};

/** Outer-perimeter transition analysis for one sampled footprint. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainPerimeterTransitionResult
{
	GENERATED_BODY()

	/** Immediate outer-perimeter transitions around the sampled footprint edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Immediate outer-perimeter transitions around the sampled footprint edge."))
	TArray<FLayoutTerrainPerimeterTransition> BoundaryTransitions;

	/** Outer-perimeter columns that could not be sampled and therefore cannot yet support ramp decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outer-perimeter columns that could not be sampled and therefore cannot yet support ramp decisions."))
	TArray<FIntPoint> MissingPerimeterBlockXYs;

	/** True when at least one adjacent outer-perimeter column is within the supportable ordinary terrain delta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when at least one adjacent outer-perimeter column is within the supportable ordinary terrain delta."))
	bool bHasSupportableTransitions = false;

	/** True when at least one adjacent outer-perimeter column exceeds the supportable ordinary terrain delta. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when at least one adjacent outer-perimeter column exceeds the supportable ordinary terrain delta."))
	bool bHasExcessiveTransitions = false;
};

/** One top-down terrain sample used by terrain-aware layout placement. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainSurfaceSample
{
	GENERATED_BODY()

	/** True when a valid supporting surface was discovered. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when a valid supporting surface was discovered."))
	bool bIsValid = false;

	/** Block-world X/Y column that was sampled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Block-world X/Y column that was sampled."))
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** First solid surface block discovered while scanning downward. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "First solid surface block discovered while scanning downward."))
	FIntVector SurfaceBlockWorldPos = FIntVector::ZeroValue;

	/** Material index read at the discovered supporting surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Material index read at the discovered supporting surface."))
	int32 SurfaceMaterialIndex = EmptyMaterial;

	/** True when this sample carries exact inclusive bounds for its solid terrain run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when SolidRunMinZ and SolidRunMaxZ bound the exact solid terrain run represented by this sample."))
	bool bHasSolidRunBounds = false;

	/** Inclusive minimum Z of the represented solid terrain run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Inclusive minimum Z of the represented solid terrain run."))
	int32 SolidRunMinZ = 0;

	/** Inclusive maximum Z of the represented solid terrain run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Inclusive maximum Z of the represented solid terrain run."))
	int32 SolidRunMaxZ = 0;

	/** Biome-switch value read at the discovered supporting surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Biome-switch value read at the discovered supporting surface."))
	int32 SurfaceBiomeSwitchIndex = EmptyBiome;
};

/** Structured reason why one sampled footprint could not resolve a terrain anchor. */
UENUM(BlueprintType)
enum class ELayoutTerrainAnchorFailureKind : uint8
{
	None,
	MissingSupportSurface,
	MissingEligibleBiomeSurface,
	SparseSupportSurface,
	DisallowedSurfaceMaterial,
	ExcessiveNeighborHeightDelta,
	ExcessiveFootprintVariation
};

/** Result of resolving a terrain-aware anchor for one layout footprint. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainAnchorResult
{
	GENERATED_BODY()

	/** True when the sampled terrain supports the requested footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the sampled terrain supports the requested footprint."))
	bool bIsValid = false;

	/** Anchor block-world position that should be used for the solved layout or module. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Anchor block-world position that should be used for the solved layout or module."))
	FIntVector AnchorBlockWorldPos = FIntVector::ZeroValue;

	/** Lowest discovered terrain surface under the sampled footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Lowest discovered terrain surface under the sampled footprint."))
	int32 MinimumTerrainZ = 0;

	/** Highest discovered terrain surface under the sampled footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Highest discovered terrain surface under the sampled footprint."))
	int32 MaximumTerrainZ = 0;

	/** Largest observed terrain step between neighboring sampled columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Largest observed terrain step between neighboring sampled columns."))
	int32 MaximumObservedNeighborHeightDelta = 0;

	/** True when the resolved anchor expects foundation fill under at least one sampled column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "True when the resolved anchor expects foundation fill under at least one sampled column."))
	bool bRequiresFoundationFill = false;

	/** Largest foundation depth required under any sampled column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Largest foundation depth required under any sampled column."))
	int32 RequiredFoundationDepth = 0;

	/** All column samples collected during anchor resolution. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "All column samples collected during anchor resolution."))
	TArray<FLayoutTerrainSurfaceSample> Samples;

	/** Failure reason when the footprint does not fit the sampled terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Failure reason when the footprint does not fit the sampled terrain."))
	FString FailureReason;

	/** Structured terrain-anchor rejection kind preserved alongside FailureReason. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Structured terrain-anchor rejection kind preserved alongside FailureReason so later runtime policy can distinguish fully unsupported gaps, sparse support footholds, and ordinary depth or slope rejection."))
	ELayoutTerrainAnchorFailureKind FailureKind = ELayoutTerrainAnchorFailureKind::None;

	/** Terrain classification and hill-transition hints derived from the sampled footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Terrain classification and hill-transition hints derived from the sampled footprint."))
	FLayoutTerrainClassificationResult TerrainClassification;

	/** Outer-perimeter terrain transitions sampled immediately around the footprint edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout", meta = (ToolTip = "Outer-perimeter terrain transitions sampled immediately around the footprint edge."))
	FLayoutTerrainPerimeterTransitionResult PerimeterTransitionAnalysis;
};

/** One pointer-free biome ownership sample frozen for worker-side terrain eligibility checks. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenBiomeOwnershipSample
{
	GENERATED_BODY()

	/** Block-world X/Y column whose biome ownership was frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Block-world X/Y column whose biome ownership was frozen."))
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** Owning biome row accepted for this sampled column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Owning biome row accepted for this sampled column."))
	FName OwningBiomeRowName;

	/** True when this column is owned by the frozen allow-list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this column is owned by the frozen allow-list."))
	bool bOwnedByAllowList = false;

	/** True when this ownership sample has matching surface evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this ownership sample has matching surface evidence."))
	bool bHasSurfaceEvidence = false;

	/** Surface Z associated with this ownership sample when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Surface Z associated with this ownership sample when available."))
	int32 SurfaceZ = 0;

	/** True when a domain-cut cavity used bounded compatible solid terrain at or below its proven floor as biome source. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when a domain-cut cavity used bounded compatible solid terrain at or below its proven floor as biome source instead of its empty layout base."))
	bool bUsedCavityFloorFallback = false;

	/** Exact biome-noise source position used to establish frozen ownership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Exact biome-noise source position used to establish frozen ownership for this cavity component."))
	FIntVector OwnershipSourceBlockWorldPos = FIntVector::ZeroValue;
};

/** One pointer-free terrain-path sample reserved for worker-side continuation terrain classification. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenTerrainPathSample
{
	GENERATED_BODY()

	/** Block-world X/Y column represented by this path sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Block-world X/Y column represented by this path sample."))
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** Surface Z when this path sample has known terrain evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Surface Z when this path sample has known terrain evidence."))
	int32 SurfaceZ = 0;

	/** True when this path sample has enough frozen evidence to classify continuation terrain. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this path sample has enough frozen evidence to classify continuation terrain."))
	bool bHasClassificationEvidence = false;
};

/** One contiguous sampled solid or empty run in one bounded terrain column. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainColumnRun
{
	/** Inclusive world Z bounds represented by this contiguous run. */
	int32 MinZ = 0;
	int32 MaxZ = 0;

	/** True when every sampled block in this run is empty. */
	bool bIsEmpty = false;
};

/** One bounded terrain column retaining every sampled solid and empty run. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainColumnProfile
{
	/** Block-world X/Y column represented by this profile. */
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** Ordered, non-overlapping runs covering the sampled vertical range. */
	TArray<FLayoutTerrainColumnRun> Runs;
};

/** One pointer-free vertical interval sample reserved for worker-side pocket/void classification. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenTerrainVoidIntervalSample
{
	GENERATED_BODY()

	/** Block-world X/Y column represented by this void/pocket sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Block-world X/Y column represented by this void/pocket sample."))
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** Inclusive minimum Z of the sampled interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Inclusive minimum Z of the sampled interval."))
	int32 MinZ = 0;

	/** Inclusive maximum Z of the sampled interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Inclusive maximum Z of the sampled interval."))
	int32 MaxZ = 0;

	/** True when this interval has complete frozen pocket/void evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this interval has complete frozen pocket/void evidence."))
	bool bHasVoidEvidence = false;

	/** True when material ownership was frozen from solid support immediately below this interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when FloorMaterialIndex was captured from solid support beneath this interval."))
	bool bHasFloorMaterialIndex = false;

	/** Material index of solid support immediately beneath this interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Material index of solid support immediately beneath this interval; foundation replay uses it per block column."))
	int32 FloorMaterialIndex = 0;

	/** Generated biome index frozen from solid block immediately below this void interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when FloorBiomeIndex was captured from solid support beneath this interval."))
	bool bHasFloorBiomeIndex = false;

	/** Generated biome owner of solid support immediately beneath this void interval. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Generated biome index of solid support immediately beneath this interval."))
	int32 FloorBiomeIndex = INDEX_NONE;
};

/** One pointer-free shifted-footprint evidence bundle reserved for non-zero terrain-backed coarse shifts. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenShiftedFootprintEvidence
{
	GENERATED_BODY()

	/** Stable placement shift id this evidence belongs to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable placement shift id this evidence belongs to."))
	FLayoutId PlacementShiftId;

	/** Coarse shift in layout cells proven by this evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Coarse shift in layout cells proven by this evidence."))
	FIntVector ShiftCells = FIntVector::ZeroValue;

	/** Shifted footprint minimum block-world position covered by frozen samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Shifted footprint minimum block-world position covered by frozen samples."))
	FIntVector FootprintMinBlockWorldPos = FIntVector::ZeroValue;

	/** Inclusive minimum block-world XY covered by shifted samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Inclusive minimum block-world XY covered by shifted samples."))
	FIntPoint SearchMinBlockXY = FIntPoint::ZeroValue;

	/** Inclusive maximum block-world XY covered by shifted samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Inclusive maximum block-world XY covered by shifted samples."))
	FIntPoint SearchMaxBlockXY = FIntPoint::ZeroValue;

	/** Shifted support samples frozen for this placement branch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Shifted support samples frozen for this placement branch."))
	TArray<FLayoutSteppedTerrainSupportSample> SteppedSupportSamples;

	/** Shifted biome ownership samples frozen for this placement branch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Shifted biome ownership samples frozen for this placement branch."))
	TArray<FLayoutFrozenBiomeOwnershipSample> BiomeOwnershipSamples;

	/** True when shifted terrain-fit proof exists for this branch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when shifted terrain-fit proof exists for this branch."))
	bool bHasTerrainFitProof = false;

	/** True when shifted reservation/collision proof exists for this branch. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when shifted reservation/collision proof exists for this branch."))
	bool bHasReservationCollisionProof = false;

	/** True when shifted chunk-overlap provenance exists for safe chunk apply. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when shifted chunk-overlap provenance exists for safe chunk apply."))
	bool bHasChunkOverlapProvenance = false;
};

/** Entry traversability classification produced by prewarm outside-perimeter sampling. */
UENUM(BlueprintType)
enum class ELayoutEntryTraversabilityVerdict : uint8
{
	/** No verdict — cell not evaluated for entry traversability. */
	None,

	/** Walkable from outside terrain. |Z_in - Z_out| <= MaxFoundationDepth / 2. */
	Walkable,

	/** Ramp/perimeter transition needed. MaxFoundationDepth/2 < |Z_in - Z_out| <= MaxFoundationDepth. */
	RampNeeded,

	/** Cliff edge — outside terrain is too far above or below. |delta| > MaxFoundationDepth. */
	CliffEdge,

	/** Entry cell is below outside terrain — excavation needed to expose entry. Z_in < Z_out. */
	ExcavationNeeded,
};

/** One explicit per-cell terrain placement verdict produced by prewarm for the selected mode. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainPlacementCellEvidence
{
	GENERATED_BODY()

	/** Region-local cell receiving this selected-mode terrain placement verdict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Region-local cell receiving this selected-mode terrain placement verdict."))
	FIntVector Cell = FIntVector::ZeroValue;

	/** True when this cell can be active for the selected mode with the frozen operation evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this cell can be active for the selected mode with the frozen operation evidence."))
	bool bPlaceableForSelectedMode = false;

	/** True when foundation-fill evidence is available for this cell. Mutually exclusive with
	 * excavation/clearance for the same local volume (both raise/fill terrain). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when foundation-fill evidence is available for this cell. Mutually exclusive with excavation/clearance for the same local volume."))
	bool bHasFoundationFillEvidence = false;

	/** Number of missing support blocks below selected cell base; valid when foundation fill evidence is present. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Number of missing support blocks below the selected cell base that bounded foundation fill must write."))
	int32 RequiredFoundationDepth = 0;

	/** Frozen material copied from nearest final-occupancy support block for foundation writes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Material copied from nearest final-occupancy support block and frozen for bounded foundation writes."))
	int32 FoundationMaterial = 0;

	/** True when ramp/perimeter transition evidence is available for this cell.
	 * An adjacency/transition operation that can coexist with local clearance or excavation
	 * (the ramp slopes between stages while the local operation handles the cell interior). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when ramp/perimeter transition evidence is available for this cell."))
	bool bHasRampTransitionEvidence = false;

	/** True when bridge/support evidence is available for this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when bridge/support evidence is available for this cell."))
	bool bHasBridgeSupportEvidence = false;

	/** True when excavation evidence is available for terrain overlap in this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when excavation evidence is available for terrain overlap in this cell."))
	bool bHasExcavationEvidence = false;

	/** True when clearance evidence is available for this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when clearance evidence is available for this cell."))
	bool bHasClearanceEvidence = false;

	/** Lattice terrain stage selected for this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Lattice terrain stage selected for this cell."))
	int32 TerrainStageIndex = 0;

	/** Exact vertical shift in blocks from the base lattice stage. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Exact vertical shift in blocks from the base lattice stage."))
	int32 VerticalShiftBlocks = 0;

	/** True when the local overlap Z bounds are valid and should be used by realization-prep. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when the local overlap Z bounds are valid and should be used by realization-prep."))
	bool bHasLocalOverlapZ = false;

	/** Compact minimum local Z overlap within the cell, valid only when bHasLocalOverlapZ is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Compact minimum local Z overlap within the cell, valid only when bHasLocalOverlapZ is true."))
	FLayoutLocalBlockCoord8 OverlapMinLocalZ;

	/** Compact maximum local Z overlap within the cell, valid only when bHasLocalOverlapZ is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Compact maximum local Z overlap within the cell, valid only when bHasLocalOverlapZ is true."))
	FLayoutLocalBlockCoord8 OverlapMaxLocalZ;

	/** Entry traversability verdict for this cell when it is on the footprint boundary. Produced by outside-perimeter sampling in prewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Entry traversability verdict for this cell when it is on the footprint boundary. Produced by outside-perimeter sampling in prewarm."))
	ELayoutEntryTraversabilityVerdict EntryTraversability = ELayoutEntryTraversabilityVerdict::None;

	/** Stable provenance id naming the prewarm sampler or source artifact that produced this verdict. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable provenance id naming the prewarm sampler or source artifact that produced this verdict."))
	FLayoutId ProvenanceId;
};

/** Source class for explicit exact-fallback evidence. */
UENUM(BlueprintType)
enum class ELayoutExactFallbackEvidenceKind : uint8
{
	ThresholdBand,
	PinnedCell,
	NonHeightfieldCell
};

/** Pointer-free evidence for one cell that requires exact terrain fallback instead of analytic-only terrain proof. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutExactFallbackCellEvidence
{
	GENERATED_BODY()

	/** Stable local/region cell covered by exact fallback evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable local/region cell covered by exact fallback evidence."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Why this cell needs exact fallback evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Why this cell needs exact fallback evidence."))
	ELayoutExactFallbackEvidenceKind EvidenceKind = ELayoutExactFallbackEvidenceKind::ThresholdBand;

	/** Stable producer provenance for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable producer provenance for diagnostics."))
	FLayoutId ProvenanceId;
};

/** Pointer-free evidence for one required flat-clearance overlap cell. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFlatClearanceCellEvidence
{
	GENERATED_BODY()

	/** Stable local/region cell covered by flat-clearance evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable local/region cell covered by flat-clearance evidence."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Required flat clearance in local cell blocks for this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Required flat clearance in local cell blocks for this cell. Runtime/prewarm code assumes owning cell dimensions were asset-validated to [1,255]."))
	FLayoutLocalBlockCoord8 RequiredClearanceBlocks;

	/** Observed flat clearance in local cell blocks emitted by the authoritative producer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Observed flat clearance in local cell blocks emitted by the authoritative producer. Runtime/prewarm code assumes owning cell dimensions were asset-validated to [1,255]."))
	FLayoutLocalBlockCoord8 ObservedClearanceBlocks;

	/** Stable producer provenance for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable producer provenance for diagnostics."))
	FLayoutId ProvenanceId;
};

/** Pointer-free evidence for one terrain cell whose analytic footprint overlap was measured by an upstream producer. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutAnalyticOverlapCellEvidence
{
	GENERATED_BODY()

	/** Stable local/region cell covered by analytic overlap evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable local/region cell covered by analytic overlap evidence."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Deterministic overlap area in square blocks or producer-defined integer units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Deterministic overlap area in square blocks or producer-defined integer units."))
	int32 OverlapArea = 0;

	/** Stable producer provenance for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable producer provenance for diagnostics."))
	FLayoutId ProvenanceId;
};

/** Explicit terrain primitive classification emitted by an upstream frozen terrain producer. */
UENUM(BlueprintType)
enum class ELayoutTerrainPrimitiveClassification : uint8
{
	Usable,
	Supportable,
	Contractible,
	Excavatable,
	InvalidRequired
};

/** Pointer-free evidence for one terrain adapter cell classification. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainPrimitiveClassificationCellEvidence
{
	GENERATED_BODY()

	/** Stable local/region cell covered by primitive classification evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable local/region cell covered by primitive classification evidence."))
	FIntVector LocalCell = FIntVector::ZeroValue;

	/** Explicit primitive classification for this cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Explicit primitive classification for this cell."))
	ELayoutTerrainPrimitiveClassification Classification = ELayoutTerrainPrimitiveClassification::Usable;

	/** Stable producer provenance for diagnostics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable producer provenance for diagnostics."))
	FLayoutId ProvenanceId;
};

/** Frozen, pointer-free terrain/biome input for worker-side terrain-mode adapters. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutFrozenTerrainBiomeAdapterInput
{
	GENERATED_BODY()

	/** Stable id for this frozen terrain/biome adapter input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Stable id for this frozen terrain/biome adapter input."))
	FLayoutId ArtifactId;

	/** Mode plan id this adapter input was sampled for. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Mode plan id this adapter input was sampled for."))
	FLayoutId ModePlanId;

	/** Site center used when sampling the terrain/biome data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Site center used when sampling the terrain/biome data."))
	FIntVector SiteCenterBlockWorldPos = FIntVector::ZeroValue;

	/** Footprint anchor/minimum block-world position covered by the frozen samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Footprint anchor/minimum block-world position covered by the frozen samples."))
	FIntVector FootprintMinBlockWorldPos = FIntVector::ZeroValue;

	/** Footprint size in block units covered by the frozen samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Footprint size in block units covered by the frozen samples."))
	FIntPoint FootprintSizeInBlocks = FIntPoint::ZeroValue;

	/** Inclusive minimum block-world XY covered by the frozen terrain search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Inclusive minimum block-world XY covered by the frozen terrain search."))
	FIntPoint SearchMinBlockXY = FIntPoint::ZeroValue;

	/** Inclusive maximum block-world XY covered by the frozen terrain search. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Inclusive maximum block-world XY covered by the frozen terrain search."))
	FIntPoint SearchMaxBlockXY = FIntPoint::ZeroValue;

	/** Coordinate settings used when active-biome noise samples were frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Coordinate settings used when active-biome noise samples were frozen."))
	FLayoutNoiseCoordinateSettings CoordinateSettings;

	/** Surface-search start Z copied from policy when this artifact was sampled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Surface-search start Z copied from policy when this artifact was sampled."))
	int32 SearchStartZBlockWorld = 0;

	/** Surface-search depth copied from policy when this artifact was sampled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Surface-search depth copied from policy when this artifact was sampled."))
	int32 SearchDepthBlocks = 0;

	/** Terrain grid spacing used by the frozen samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Terrain grid spacing used by the frozen samples."))
	int32 TerrainSampleGridSpacing = 0;

	/** Biome row selected for single-row terrain ownership, when applicable. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Biome row selected for single-row terrain ownership, when applicable."))
	FName EligibleBiomeRowName;

	/** Biome row allow-list selected for this adapter input. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Biome row allow-list selected for this adapter input."))
	TArray<FName> EligibleBiomeRowNames;

	/** True when worker-side consumers must reject samples outside the frozen biome allow-list. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when worker-side consumers must reject samples outside the frozen biome allow-list."))
	bool bRequiresBiomeOwnership = false;

	/** True when the artifact carries enough biome ownership data to enforce the allow-list without live sampler reads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when the artifact carries enough biome ownership data to enforce the allow-list without live sampler reads."))
	bool bHasBiomeOwnershipEvidence = false;

	/** True when search bounds and depth form a finite frozen sample envelope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when search bounds and depth form a finite frozen sample envelope."))
	bool bHasFiniteSearchBounds = false;

	/** True when sampled column evidence was frozen from request-owned planning/runtime data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when sampled column evidence was frozen from request-owned planning/runtime data."))
	bool bHasSampledColumnEvidence = false;

	/** True when stepped support evidence was frozen from request-owned planning/runtime data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when stepped support evidence was frozen from request-owned planning/runtime data."))
	bool bHasSteppedSupportEvidence = false;

	/** True when shifted-footprint samples/proofs are present for non-zero coarse shifts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when shifted-footprint samples/proofs are present for non-zero coarse shifts."))
	bool bHasShiftedFootprintEvidence = false;

	/** True when terrain-path samples are present for continuation or open-space classification. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when terrain-path samples are present for continuation or open-space classification."))
	bool bHasTerrainPathEvidence = false;

	/** True when underground pocket/void intervals are present for tunnel or excavation classification. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when underground pocket/void intervals are present for tunnel or excavation classification."))
	bool bHasPocketVoidIntervalEvidence = false;

	/** True when a bounded profile relative to selected layout Z classified the full footprint environment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when a bounded profile relative to selected layout Z classified the full footprint environment."))
	bool bHasRelativeEnvironmentClassification = false;

	/** True when every sampled footprint column has solid overburden above the selected layout envelope. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when every sampled footprint column has solid overburden above the selected layout envelope."))
	bool bIsClassifiedUnderground = false;

	/** Lowest surface Z represented by frozen support samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Lowest surface Z represented by frozen support samples."))
	int32 MinSurfaceZ = 0;

	/** Highest surface Z represented by frozen support samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Highest surface Z represented by frozen support samples."))
	int32 MaxSurfaceZ = 0;

	/** Largest raw neighboring stepped-support delta represented by this artifact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Largest raw neighboring stepped-support delta represented by this artifact."))
	int32 MaxObservedNeighborHeightDelta = 0;

	/** Largest snapped neighboring stepped-support level delta represented by this artifact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Largest snapped neighboring stepped-support level delta represented by this artifact."))
	int32 MaxObservedSnappedLevelDelta = 0;

	/** Frozen surface samples used by terrain-mode adapters without live world reads. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen surface samples used by terrain-mode adapters without live world reads."))
	TArray<FLayoutTerrainSurfaceSample> SurfaceSamples;

	/** Full-resolution terrain surfaces used only for perimeter ramp fill/excavation decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Full-resolution terrain surfaces around the selected footprint. Invalid samples represent columns with no relevant terrain and never authorize excavation."))
	TArray<FLayoutTerrainSurfaceSample> PerimeterSurfaceSamples;

	/** Frozen biome ownership samples paired with worker-safe surface evidence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen biome ownership samples paired with worker-safe surface evidence."))
	TArray<FLayoutFrozenBiomeOwnershipSample> BiomeOwnershipSamples;

	/** True when footprint cell classification was frozen from sampled columns and adjacency. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when footprint cell classification was frozen from sampled columns and adjacency."))
	bool bHasFootprintClassificationEvidence = false;

	/** Frozen terrain anchor analysis for exact/analytic parity and audit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen terrain anchor analysis for exact/analytic parity and audit."))
	FLayoutTerrainAnchorResult AnchorResult;

	/** Frozen sampled-footprint classification derived from support samples and adjacency only. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen sampled-footprint classification derived from support samples and adjacency only."))
	FLayoutTerrainClassificationResult FootprintClassification;

	/** Frozen stepped support samples copied from request-owned planning/runtime data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen stepped support samples copied from request-owned planning/runtime data."))
	TArray<FLayoutSteppedTerrainSupportSample> SteppedSupportSamples;

	/** Frozen cardinal one-cell terrain halo used only to prove retained terrain beside generated support cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen cardinal one-cell terrain halo used only to prove retained terrain beside generated support cells."))
	TArray<FLayoutSteppedTerrainSupportSample> SteppedNeighborHaloSamples;

	/** Frozen stepped adjacency evidence copied from request-owned planning/runtime data. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Frozen stepped adjacency evidence copied from request-owned planning/runtime data."))
	TArray<FLayoutSteppedTerrainAdjacencyStep> SteppedAdjacencySteps;

	/** Pointer-free terrain-path sample scaffold; empty until continuation/open-space path sampling is frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Pointer-free terrain-path sample scaffold; empty until continuation/open-space path sampling is frozen."))
	TArray<FLayoutFrozenTerrainPathSample> TerrainPathSamples;

	/** Pointer-free underground pocket/void interval scaffold; empty until interval sampling is frozen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Pointer-free underground pocket/void interval scaffold; empty until interval sampling is frozen."))
	TArray<FLayoutFrozenTerrainVoidIntervalSample> PocketVoidIntervals;

	/** Pointer-free shifted-footprint evidence scaffold for non-zero coarse placement branches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Pointer-free shifted-footprint evidence scaffold for non-zero coarse placement branches."))
	TArray<FLayoutFrozenShiftedFootprintEvidence> ShiftedFootprintEvidence;

	/** True when this artifact includes exact fallback evidence for non-analytic columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this artifact includes exact fallback evidence for non-analytic columns."))
	bool bHasExactFallbackEvidence = false;

	/** Explicit exact fallback cells emitted by prewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Explicit exact fallback cells emitted by prewarm."))
	TArray<FLayoutExactFallbackCellEvidence> ExactFallbackCells;

	/** True when this artifact includes explicit flat-clearance evidence for required flat overlap cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this artifact includes explicit flat-clearance evidence for required flat overlap cells."))
	bool bHasFlatClearanceEvidence = false;

	/** Explicit flat-clearance cells emitted by prewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Explicit flat-clearance cells emitted by prewarm."))
	TArray<FLayoutFlatClearanceCellEvidence> FlatClearanceCells;

	/** True when this artifact includes explicit analytic overlap evidence for sampled terrain cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this artifact includes explicit analytic overlap evidence for sampled terrain cells."))
	bool bHasAnalyticOverlapEvidence = false;

	/** Explicit analytic-overlap cells emitted by prewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Explicit analytic-overlap cells emitted by prewarm."))
	TArray<FLayoutAnalyticOverlapCellEvidence> AnalyticOverlapCells;

	/** True when this artifact includes explicit per-cell selected-mode placement verdicts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this artifact includes explicit per-cell selected-mode placement verdicts."))
	bool bHasTerrainPlacementEvidence = false;

	/** Explicit selected-mode terrain placement evidence emitted by prewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Explicit selected-mode terrain placement evidence emitted by prewarm."))
	TArray<FLayoutTerrainPlacementCellEvidence> TerrainPlacementCells;

	/** True when this artifact includes explicit primitive classification evidence for terrain adapter cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "True when this artifact includes explicit primitive classification evidence for terrain adapter cells."))
	bool bHasTerrainPrimitiveClassificationEvidence = false;

	/** Explicit primitive-classification cells emitted by prewarm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Explicit primitive-classification cells emitted by prewarm."))
	TArray<FLayoutTerrainPrimitiveClassificationCellEvidence> TerrainPrimitiveClassificationCells;

	/** Compact deterministic audit strings describing sampler decisions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Terrain", meta = (ToolTip = "Compact deterministic audit strings describing sampler decisions."))
	TArray<FString> AuditMessages;
};

/**
 * Shared terrain-sampling helpers used by both preview tooling and runtime
 * chunk-load realization.
 */
class PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutTerrainSampling
{
public:
	/** Generic top-down surface query backed by a caller-provided block-value reader. */
	static FLayoutTerrainSurfaceSample SampleTopDownSurface(
		const FIntPoint& BlockXY,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		TFunctionRef<int32(const FIntVector&)> ReadMaterialIndex,
		TFunctionRef<int32(const FIntVector&)> ReadBiomeSwitchIndex);

	/** Generic footprint-based anchor resolution backed by caller-provided terrain readers. */
	static FLayoutTerrainAnchorResult ResolveFootprintAnchor(
		const FIntVector& CandidateAnchorBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		int32 GridSpacing,
		int32 HeightIgnoreThreshold,
		TFunctionRef<int32(const FIntVector&)> ReadMaterialIndex,
		TFunctionRef<int32(const FIntVector&)> ReadBiomeSwitchIndex,
		TFunctionRef<bool(int32)> IsMaterialAllowed);

	/** World-backed top-down surface query using Porism's live block reads. */
	static FLayoutTerrainSurfaceSample SampleTopDownSurfaceFromWorld(
		AChunkWorldCore* World,
		const FIntPoint& BlockXY,
		int32 SearchStartZ,
		int32 SearchDepthBlocks);

	/** World-backed top-down surface query constrained to one active owning biome row. */
	static FLayoutTerrainSurfaceSample SampleTopDownEligibleBiomeSurfaceFromWorld(
		AChunkWorldCore* World,
		FName EligibleBiomeRowName,
		const FIntPoint& BlockXY,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler);

	/** World-backed top-down surface query constrained to any row in one active owning-biome allow-list. */
	static FLayoutTerrainSurfaceSample SampleTopDownEligibleBiomeSurfaceFromAnyRowFromWorld(
		AChunkWorldCore* World,
		TConstArrayView<FName> EligibleBiomeRowNames,
		const FIntPoint& BlockXY,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler);

	/** World-backed footprint anchor resolution using Porism's live block reads. */
	static FLayoutTerrainAnchorResult ResolveFootprintAnchorFromWorld(
		AChunkWorldCore* World,
		const FIntVector& CandidateAnchorBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		int32 GridSpacing,
		int32 HeightIgnoreThreshold,
		TFunctionRef<bool(int32)> IsMaterialAllowed);

	/** World-backed footprint anchor resolution constrained to one active owning biome row. */
	static FLayoutTerrainAnchorResult ResolveFootprintAnchorFromActiveBiomeSurface(
		AChunkWorldCore* World,
		FName EligibleBiomeRowName,
		const FIntVector& CandidateAnchorBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		int32 GridSpacing,
		int32 HeightIgnoreThreshold,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		TFunctionRef<bool(int32)> IsMaterialAllowed);

	/** World-backed footprint anchor resolution constrained to any row in one active owning-biome allow-list. */
	static FLayoutTerrainAnchorResult ResolveFootprintAnchorFromAnyActiveBiomeSurface(
		AChunkWorldCore* World,
		TConstArrayView<FName> EligibleBiomeRowNames,
		const FIntVector& CandidateAnchorBlockWorldPos,
		const FIntPoint& FootprintSizeInBlocks,
		int32 SearchStartZ,
		int32 SearchDepthBlocks,
		int32 GridSpacing,
		int32 HeightIgnoreThreshold,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		TFunctionRef<bool(int32)> IsMaterialAllowed);

	/** Classifies sampled footprint columns as flat, mild-slope, or steep-edge terrain. */
	static FLayoutTerrainClassificationResult ClassifyFootprintTerrainSamples(
		const TArray<FLayoutTerrainSurfaceSample>& Samples,
		int32 MaxSupportableNeighborHeightDelta);

	/** Snaps a sampled surface to its shared-cell ceiling; route validation must use this same stage rule as support-map generation. */
	static int32 SnapSurfaceZUpToSharedCellLattice(int32 SharedCellHeightInBlocks, int32 SurfaceZ);

	/** Compiles aligned per-cell terrain samples into the frozen stepped-support-map request carrier. */
	static bool TryBuildSteppedTerrainSupportMap(
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutTerrainAnchorResult& AnchorResult,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason);

	/** Compiles aligned frozen surface heights into the stepped-support-map request carrier without requiring live world reads. */
	static bool TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TMap<FIntPoint, int32>& SurfaceZByBlockXY,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason);

	/** Builds all contiguous solid and empty runs from ordered column-major empty-state samples. */
	static bool TryBuildTerrainColumnProfilesFromEmptySamples(
		const FIntVector& FootprintMinBlockWorldPos,
		int32 FootprintWidthBlocks,
		int32 FootprintHeightBlocks,
		int32 MinZ,
		int32 MaxZ,
		TConstArrayView<uint8> EmptyStates,
		TArray<FLayoutTerrainColumnProfile>& OutProfiles,
		FString& OutFailureReason);

	/** Builds generated terrain column profiles by evaluating active-biome noise at every bounded Z. */
	static bool TryBuildTerrainColumnProfilesFromActiveBiomeSampler(
		const FIntVector& FootprintMinBlockWorldPos,
		int32 FootprintWidthBlocks,
		int32 FootprintHeightBlocks,
		int32 MinZ,
		int32 MaxZ,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		TArray<FLayoutTerrainColumnProfile>& OutProfiles,
		FString& OutFailureReason);

	/** Classifies Surface versus Underground from the center air run overlapping the unshifted layout envelope. */
	static bool TryClassifyCenterColumnEnvironment(
		const FLayoutTerrainColumnProfile& CenterProfile,
		int32 LayoutEnvelopeMinZ,
		int32 LayoutEnvelopeHeightInBlocks,
		FLayoutTerrainColumnRun& OutSelectedAirRun,
		bool& bOutUnderground,
		FString& OutFailureReason);

	/** Selects one cardinally connected empty component while center-column evidence alone owns environment classification. */
	static bool TrySelectConnectedEmptyComponent(
		const TArray<FLayoutTerrainColumnProfile>& Profiles,
		const FIntVector& SiteCenterBlockWorldPos,
		int32 LayoutEnvelopeHeightInBlocks,
		TMap<FIntPoint, FLayoutTerrainColumnRun>& OutSelectedRuns,
		bool& bOutUnderground,
		FString& OutFailureReason);

	/** Freezes selected-site environment and floor ownership from one bounded final-occupancy material packet. */
	static bool TryPrepareSelectedSiteTerrainFromMaterialSamples(
		const FIntVector& SampleFootprintMinBlockWorldPos,
		int32 SampleFootprintWidthBlocks,
		int32 SampleFootprintHeightBlocks,
		int32 SampleMinZ,
		int32 SampleMaxZ,
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 LayoutEnvelopeHeightInBlocks,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		int32 EmptyMaterialValue,
		TConstArrayView<int> Materials,
		TConstArrayView<int> BiomeIndices,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
		FString& OutFailureReason);

	/** Compiles selected-component stepped support. Bounded samples must contain an observed floor below each interval; MIN_int32 leaves explicit unbounded interval inputs unchanged. */
	static bool TryBuildSteppedTerrainSupportMapFromVoidIntervals(
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TArray<FLayoutFrozenTerrainVoidIntervalSample>& VoidIntervals,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason,
		int32 MinimumSampledZ = MIN_int32);

	/** Marks terrain-driven perimeter ramps against the unadjusted layout base for flat or stepped roots. */
	static void ApplySelectedComponentPerimeterRampEvidence(
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntPoint& FootprintSizeInCells,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact);

	/** Augments one selected-component artifact with shared stepped topology and environment-specific exterior Entry evidence. */
	static bool TryAugmentSelectedComponentWithSteppedTerrainEvidence(
		bool bUnderground,
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntPoint& FootprintSizeInCells,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
		FString& OutFailureReason);

	/** Compiles a stepped-support map from frozen planning pocket samples that already preserve surface Z. */
	static bool TryBuildSteppedTerrainSupportMapFromPocketSamples(
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TArray<FLayoutReservationPocketSample>& PocketSamples,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason);

	/** Builds frozen stepped terrain evidence; optional cardinal halo samples prove retained terrain beside generated support cells.
	 * Rejects full-cell source overlap before normalization can erase it; rejection returns no placement evidence and an audit reason. */
	static FLayoutFrozenTerrainBiomeAdapterInput BuildFrozenTerrainBiomeAdapterInputFromSteppedSupportMap(
		const FIntVector& SiteCenterBlockWorldPos,
		const FIntVector& FootprintMinBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		const FIntPoint& FootprintSizeInBlocks,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutWorldBindingTerrainTransitionPolicy& TerrainTransitionPolicy,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		int32 TerrainSampleGridSpacing,
		FName EligibleBiomeRowName,
		TConstArrayView<FName> EligibleBiomeRowNames,
		const FLayoutSteppedTerrainSupportMap& SupportMap,
		bool bUseCardinalShiftClusters = false,
		TConstArrayView<FLayoutSteppedTerrainSupportSample> NeighborHaloSamples = {});

	/** Post-processes placement evidence with entry traversability verdicts by sampling outside-perimeter surfaces. */
	static void ComputeEntryTraversabilityVerdicts(
		FLayoutFrozenTerrainBiomeAdapterInput& InOutArtifact,
		const FIntVector& SharedCellSizeInBlocks,
		int32 MaxFoundationDepth,
		TFunctionRef<int32(int32 BlockX, int32 BlockY)> ResolveSurfaceZ);

	/** Samples one active-biome surface per planned cell and compiles the frozen stepped-support-map carrier without live chunk reads. */
	static bool TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason);

	/** Samples one world-backed active-biome surface per planned cell and compiles the frozen stepped-support-map carrier from live chunk terrain. */
	static bool TryBuildSteppedTerrainSupportMapFromActiveBiomeSurfaceWorld(
		AChunkWorldCore* World,
		const FIntVector& AnchorBlockWorldPos,
		const FIntVector& SharedCellSizeInBlocks,
		int32 TerrainSampleGridSpacing,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FLayoutSteppedTerrainSupportMap& OutSupportMap,
		FString& OutFailureReason);

	/** Compares full stepped-support carriers exactly, including raw terrain fields, for focused parity tests. */
	static bool AreSteppedTerrainSupportMapsEquivalent(
		const FLayoutSteppedTerrainSupportMap& Left,
		const FLayoutSteppedTerrainSupportMap& Right,
		FString* OutMismatchReason = nullptr);

	/** Compares stepped-support solve contract semantics while treating raw sampled heights as diagnostic unless they change neighbor-delta policy outcomes. */
	static bool AreSteppedTerrainSupportMapsContractEquivalent(
		const FLayoutSteppedTerrainSupportMap& Left,
		const FLayoutSteppedTerrainSupportMap& Right,
		FString* OutMismatchReason = nullptr);

	/** Builds support-fill block edits under one resolved footprint using the sampled support material. */
	static void BuildFoundationFillBlocks(
		const FLayoutTerrainAnchorResult& AnchorResult,
		TArray<FIntVector>& OutBlockPositions,
		TArray<int32>& OutMaterialIndices);
};
