// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "SurfaceAnchorReservationPayloads.generated.h"

/** Generic surface-local reservation shape used by reservation domain and spawn-domain outputs. */
UENUM(BlueprintType)
enum class ESurfaceAnchorReservationShape : uint8
{
	/** Ellipsoid/sphere-like volume centered on the resolved surface. */
	Sphere,

	/** Axis-aligned box volume centered on the resolved surface. */
	Box
};

/** Surface height policy used when a surface anchor resolves against foundation terrain under its footprint. */
UENUM(BlueprintType)
enum class ESurfaceAnchorHeightSolveMode : uint8
{
	/** Align the anchor from the foundation surface at the anchor center. */
	CenterSample,

	/** Align the anchor from the average foundation surface across the sampled footprint. */
	AverageSamples,

	/** Align the anchor from the highest sampled foundation surface, best for flat structure pads. */
	MaxSamples,

	/** Align the anchor from the highest sampled corner or equivalent footprint-edge surface. */
	CornerMaxSamples
};

/** Base family for surface-anchor terrain generation options. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FSurfaceAnchorTerrainPayloadBase
{
	GENERATED_BODY()
};

/** Exact flat pad surface for structures and authored reservation features. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FSurfaceAnchorFlatTerrainPayload : public FSurfaceAnchorTerrainPayloadBase
{
	GENERATED_BODY()
};

/** Flat-center pad that blends its terrain height down/up to the solved foundation surface near the reservation edge. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FSurfaceAnchorBlendedSupportTerrainPayload : public FSurfaceAnchorTerrainPayloadBase
{
	GENERATED_BODY()

	/** Width of the inner edge band that blends from the anchor surface to the solved foundation surface. Zero behaves like Flat. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Blended Support", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Width of the inner edge band that blends from the anchor surface to the solved foundation surface. The reservation domain is unchanged; only GenA terrain height eases near the edge. Zero behaves like Flat."))
	float EdgeBlendWidthBlocks = 8.0f;
};

/** Terrain surface with authored height variation for organic pads, hills, or terraces. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FSurfaceAnchorNoisyTerrainPayload : public FSurfaceAnchorTerrainPayloadBase
{
	GENERATED_BODY()

	/** Authored-block vertical variation applied around the resolved anchor surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain Noise", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Authored-block vertical variation applied around the resolved anchor surface. Set to 0 for an effectively flat noisy payload."))
	float SurfaceNoiseAmplitude = 4.0f;

	/** Frequency scale for surface variation across the anchor footprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain Noise", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Frequency scale for surface variation across the anchor footprint. Larger values create more height changes across the same area."))
	float SurfaceNoiseScale = 3.0f;

	/** Seed offset applied to surface noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain Noise", meta = (ToolTip = "Seed offset applied to this anchor's surface variation."))
	int32 SurfaceNoiseSeedOffset = 83;

	/** If true, quantize the noisy surface into plateau-like height steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain Noise", meta = (ToolTip = "If true, quantizes the varied surface into plateau-like height steps."))
	bool bUseTerraces = false;

	/** Authored-block terrace step size when terraces are enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain Noise", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bUseTerraces", ToolTip = "Authored-block height between terrace levels when terrace mode is enabled."))
	float TerraceStepHeight = 1.0f;

	/** Smooths terrace edges; zero keeps hard steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain Noise", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bUseTerraces", ToolTip = "Smooths terrace edges; 0 keeps hard steps."))
	float TerraceSmoothness = 0.0f;
};

/** Generic shallow reservation payload for surfaces that sit on a resolved foundation surface. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FSurfaceAnchorReservationPayload : public FReservationPayloadBase
{
	GENERATED_BODY()

	FSurfaceAnchorReservationPayload()
	{
		TerrainPayload.InitializeAs<FSurfaceAnchorFlatTerrainPayload>();
	}

	/** Center of the reservation in authored block XY coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor", meta = (DisplayName = "Center XY (Blocks)", ToolTip = "Center of this surface anchor in authored block coordinates relative to the strategy origin."))
	FVector2D CenterXY = FVector2D::ZeroVector;

	/** Shape used for the surface-local reservation domain and foundation carve-out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor", meta = (ToolTip = "Shape used for the reservation domain, reservation terrain, and optional foundation carve-out. Sphere uses ellipsoid radii; Box uses axis-aligned half extents."))
	ESurfaceAnchorReservationShape Shape = ESurfaceAnchorReservationShape::Sphere;

	/** X/Y/Z authored-block radii for sphere-shaped anchors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Shape", meta = (DisplayName = "Sphere Radii (Blocks)", ClampMin = "0.001", UIMin = "0.001", EditCondition = "Shape == ESurfaceAnchorReservationShape::Sphere", ToolTip = "X/Y/Z authored-block radii for sphere-shaped anchors. Z controls below-surface depth; the domain also keeps above-surface clearance for the X/Y footprint."))
	FVector SphereRadii = FVector(35.0, 35.0, 18.0);

	/** X/Y/Z authored-block half extents for box-shaped anchors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Shape", meta = (DisplayName = "Box Half Extent (Blocks)", ClampMin = "0.001", UIMin = "0.001", EditCondition = "Shape == ESurfaceAnchorReservationShape::Box", ToolTip = "X/Y/Z authored-block half extents for box-shaped anchors. Z controls below-surface depth; the domain also keeps above-surface clearance for the X/Y footprint."))
	FVector BoxHalfExtent = FVector(35.0, 35.0, 18.0);

	/** Optional X/Y/Z authored-block radii for spawn-readable sphere subregions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Spawn", meta = (DisplayName = "Spawn Sphere Radii (Blocks)", ClampMin = "0.0", UIMin = "0.0", EditCondition = "Shape == ESurfaceAnchorReservationShape::Sphere", ToolTip = "Optional X/Y/Z authored-block radii for spawn-readable sphere subregions. Values <= 0 use half of Sphere Radii."))
	FVector SpawnSphereRadii = FVector(18.0, 18.0, 10.0);

	/** Optional X/Y/Z authored-block half extents for spawn-readable box subregions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Spawn", meta = (DisplayName = "Spawn Box Half Extent (Blocks)", ClampMin = "0.0", UIMin = "0.0", EditCondition = "Shape == ESurfaceAnchorReservationShape::Box", ToolTip = "Optional X/Y/Z authored-block half extents for spawn-readable box subregions. Values <= 0 use half of Box Half Extent."))
	FVector SpawnBoxHalfExtent = FVector(18.0, 18.0, 10.0);

	/** Supported authored-block height added after solving the foundation surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Surface Height", meta = (DisplayName = "Surface Z Lift", ToolTip = "Supported authored-block height added after the foundation surface is solved. Positive values raise the anchor and slope foundation terrain up; negative values lower the anchor and cut foundation terrain down toward it."))
	float SurfaceZLift = 0.0f;

	/** Raw authored-block placement offset applied after Surface Z Lift. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Surface Height", meta = (DisplayName = "Surface Z Offset", ToolTip = "Raw authored-block placement offset applied after Surface Z Lift. Positive values can intentionally float the anchor above its support; negative values can sink it into the foundation."))
	float SurfaceZOffset = 0.0f;

	/** Chooses which sampled foundation surface height should place this anchor's flat reservation surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Surface Height", meta = (ToolTip = "Chooses which sampled foundation surface height places this anchor's target surface. MaxSamples is the default for structure pads so the pad does not sink below nearby foundation terrain."))
	ESurfaceAnchorHeightSolveMode SurfaceHeightSolveMode = ESurfaceAnchorHeightSolveMode::MaxSamples;

	/** Terrain generation settings for the reservation surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Surface Anchor|Terrain", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.SurfaceAnchorTerrainPayloadBase", ExcludeBaseStruct, NoClear, ToolTip = "Terrain generation mode for the reservation surface. Flat creates an exact support pad; Noisy adds authored height variation and optional terraces. Editor normalization replaces empty payloads with Flat."))
	FInstancedStruct TerrainPayload;
};

/** Deterministic repeated arrangement of one provider-local surface-anchor reservation. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FMultiInstanceSurfaceAnchorReservationPayload : public FReservationPayloadBase
{
	GENERATED_BODY()

	FMultiInstanceSurfaceAnchorReservationPayload()
	{
		PrototypeSurfaceAnchor.InitializeAs<FSurfaceAnchorReservationPayload>();
	}

	/** Surface-anchor reservation copied for each resolved instance. Biome and field tags come from the owning reservation definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.ReservationPayloadBase", ExcludeBaseStruct, ToolTip = "Surface-anchor reservation copied for each resolved instance. Use Surface Anchor Reservation Payload here; Biome Tag and field tags come from the owning reservation definition."))
	FInstancedStruct PrototypeSurfaceAnchor;

	/** Deterministic horizontal arrangement used to place prototype reservations on the owning provider surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (ToolTip = "Deterministic horizontal arrangement used to place prototype reservations on the owning provider surface."))
	EMultiInstanceFoundationArrangement Arrangement = EMultiInstanceFoundationArrangement::Line;

	/** Number of surface-anchor reservations to create from the prototype. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Number of surface-anchor reservations to create from the prototype."))
	int32 InstanceCount = 2;

	/** Center of the whole arrangement in provider-local authored block XY coordinates. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (DisplayName = "Arrangement Center XY (Blocks)", ToolTip = "Center of the whole arrangement in provider-local authored block coordinates."))
	FVector2D ArrangementCenterXY = FVector2D::ZeroVector;

	/** Horizontal spacing between line/grid reservation instances in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "Arrangement != EMultiInstanceFoundationArrangement::Ring", ToolTip = "Horizontal spacing between line/grid reservation instances in authored blocks. Validation should keep this large enough to avoid overlap. Ring arrangements use Ring Radius instead."))
	float SpacingBlocks = 90.0f;

	/** Number of columns used by Grid arrangement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (ClampMin = "1", UIMin = "1", EditCondition = "Arrangement == EMultiInstanceFoundationArrangement::Grid", ToolTip = "Number of columns used by Grid arrangement. Rows are derived from Instance Count."))
	int32 GridColumns = 2;

	/** Ring radius used by Ring arrangement in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "Arrangement == EMultiInstanceFoundationArrangement::Ring", ToolTip = "Ring radius used by Ring arrangement in authored blocks."))
	float RingRadiusBlocks = 90.0f;

	/** Minimum deterministic extra Surface Z Offset per reservation instance in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance|Variation", meta = (ToolTip = "Minimum deterministic extra Surface Z Offset per reservation instance in authored blocks. This is added after the prototype's own Surface Z Offset."))
	float MinSurfaceZOffsetBlocks = 0.0f;

	/** Maximum deterministic extra Surface Z Offset per reservation instance in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance|Variation", meta = (ToolTip = "Maximum deterministic extra Surface Z Offset per reservation instance in authored blocks. This is added after the prototype's own Surface Z Offset."))
	float MaxSurfaceZOffsetBlocks = 0.0f;

	/** Minimum uniform geometric scale applied to each prototype reservation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance|Variation", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Minimum deterministic uniform geometric scale applied to each prototype reservation. This does not override the strategy-wide worldgen scale context."))
	float MinUniformScale = 1.0f;

	/** Maximum uniform geometric scale applied to each prototype reservation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance|Variation", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Maximum deterministic uniform geometric scale applied to each prototype reservation. This does not override the strategy-wide worldgen scale context."))
	float MaxUniformScale = 1.0f;

	/** Extra authored-block padding used by validation when checking reservation instance overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance|Validation", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra authored-block padding used by validation when checking reservation instance overlap."))
	float FootprintPaddingBlocks = 0.0f;

	/** If true, validation rejects finite arrangements whose worst-case reservation footprints overlap. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Reservation|Multi Instance|Validation", meta = (ToolTip = "If true, validation rejects finite arrangements whose worst-case reservation footprints overlap."))
	bool bRequireNoOverlap = true;
};
