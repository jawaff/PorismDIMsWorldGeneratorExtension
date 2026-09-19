// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "FoundationTerrainProfilePayloads.generated.h"

/** Exact flat top-surface profile for additive foundations that need deterministic terrain. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFlatFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()
};

/** Reusable noisy top-surface terrain profile for additive foundations. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FNoisyFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Upper-surface height variation in authored blocks. Provider-specific rim and underside detail belongs on the provider payload. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Surface", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Upper-surface height variation in authored blocks. Providers without an underside use this as their primary terrain variation."))
	float SurfaceNoiseAmplitude = 12.0f;

	/** Domain scale for upper-surface noise. Smaller values produce broader hills and waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Surface", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for upper-surface noise. Smaller values produce broader hills and waves."))
	float SurfaceNoiseScale = 0.8f;

	/** Seed offset applied to the terrain profile's detail noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Noise", meta = (ToolTip = "Seed offset applied to the terrain profile's detail noise."))
	int32 SeedOffset = 0;
};

/** Broad rolling-hill terrain profile with asymmetric hill and valley heights. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FRollingHillsFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Maximum upward hill height in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Rolling Hills", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum upward hill height in authored blocks."))
	float HillHeightBlocks = 24.0f;

	/** Maximum downward valley depth in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Rolling Hills", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum downward valley depth in authored blocks."))
	float ValleyDepthBlocks = 8.0f;

	/** Domain scale for broad hill noise. Smaller values create larger hills. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Rolling Hills", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for broad hill noise. Smaller values create larger hills."))
	float HillScale = 0.35f;

	/** Small additional surface variation in authored blocks. Use zero for only broad hills. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Rolling Hills|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Small additional surface variation in authored blocks. Use zero for only broad hills."))
	float DetailAmplitude = 3.0f;

	/** Domain scale for optional detail variation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Rolling Hills|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for optional detail variation."))
	float DetailScale = 1.2f;

	/** Seed offset applied to the rolling hills noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Rolling Hills", meta = (ToolTip = "Seed offset applied to the rolling hills noise."))
	int32 SeedOffset = 401;
};

/** Radial edge erosion profile for finite foundations, useful for worn island rims. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FErodedEdgeFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Provider-local XY center of the eroded radius in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge", meta = (ToolTip = "Provider-local XY center of the eroded radius in authored blocks. The owning provider's top-center frame is used as the origin."))
	FVector2D CenterXY = FVector2D::ZeroVector;

	/** Optional authored radius override. Zero uses the owning island provider's top radius when available. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Optional authored radius override. Zero uses the owning island provider's top radius when available."))
	float RadiusOverrideBlocks = 0.0f;

	/** Width of the eroded rim band in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Width of the eroded rim band in authored blocks."))
	float EdgeWidthBlocks = 24.0f;

	/** Maximum downward erosion depth at the outer edge in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum downward erosion depth at the outer edge in authored blocks."))
	float EdgeDepthBlocks = 16.0f;

	/** Noisy edge breakup amplitude in authored blocks. Use zero for a smooth eroded rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Noisy edge breakup amplitude in authored blocks. Use zero for a smooth eroded rim."))
	float EdgeNoiseAmplitude = 8.0f;

	/** Domain scale for edge breakup noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for edge breakup noise."))
	float EdgeNoiseScale = 0.8f;

	/** Seed offset applied to edge erosion detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Eroded Edge", meta = (ToolTip = "Seed offset applied to edge erosion detail."))
	int32 SeedOffset = 503;
};

/** Crevice terrain profile that cuts narrow downward cracks into a provider surface. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FCreviceFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Maximum downward crevice depth in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Crevice", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum downward crevice depth in authored blocks."))
	float CreviceDepthBlocks = 20.0f;

	/** Domain scale for crevice lines. Smaller values produce longer broader cracks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Crevice", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for crevice lines. Smaller values produce longer broader cracks."))
	float CreviceScale = 0.55f;

	/** Power applied to the crevice mask. Higher values make narrower sharper cracks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Crevice", meta = (ClampMin = "0.1", UIMin = "0.1", ToolTip = "Power applied to the crevice mask. Higher values make narrower sharper cracks."))
	float CreviceSharpness = 2.0f;

	/** Small surrounding surface variation in authored blocks. Use zero for only downward crevice cuts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Crevice|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Small surrounding surface variation in authored blocks. Use zero for only downward crevice cuts."))
	float SurfaceNoiseAmplitude = 4.0f;

	/** Domain scale for surrounding surface variation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Crevice|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for surrounding surface variation."))
	float SurfaceNoiseScale = 1.0f;

	/** Seed offset applied to crevice noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Crevice", meta = (ToolTip = "Seed offset applied to crevice noise."))
	int32 SeedOffset = 607;
};

/** Bowl terrain profile that lowers terrain toward a provider-local center without changing provider ownership. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FBowlFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Provider-local XY center of the bowl in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl", meta = (ToolTip = "Provider-local XY center of the bowl in authored blocks. The owning provider's top-center frame is used as the origin."))
	FVector2D CenterXY = FVector2D::ZeroVector;

	/** Horizontal bowl radius in authored blocks. Larger values create broader basins. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Horizontal bowl radius in authored blocks. Larger values create broader basins."))
	float RadiusBlocks = 90.0f;

	/** Maximum center depression in authored blocks. Positive values lower the center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum center depression in authored blocks. Positive values lower the center while leaving the provider domain unchanged."))
	float DepthBlocks = 24.0f;

	/** Optional surface noise mixed into the bowl in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Optional surface noise mixed into the bowl in authored blocks. Use zero for an analytic basin."))
	float SurfaceNoiseAmplitude = 4.0f;

	/** Domain scale for optional bowl surface noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for optional bowl surface noise. Smaller values produce broader unevenness."))
	float SurfaceNoiseScale = 0.7f;

	/** Seed offset applied to optional bowl detail noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl|Detail", meta = (ToolTip = "Seed offset applied to optional bowl detail noise."))
	int32 SeedOffset = 103;

	/** If true, adds a raised ring at the bowl radius for crater and basin profiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl|Raised Rim", meta = (ToolTip = "If true, adds a raised ring at the bowl radius for crater and basin profiles. Disable for a pure smooth bowl."))
	bool bEnableRaisedRim = true;

	/** Width of the raised rim band in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl|Raised Rim", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bEnableRaisedRim", EditConditionHides, ToolTip = "Width of the raised rim band in authored blocks. Wider rims produce broader ring plateaus."))
	float RimWidthBlocks = 18.0f;

	/** Maximum upward rim height in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Bowl|Raised Rim", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableRaisedRim", EditConditionHides, ToolTip = "Maximum upward rim height in authored blocks."))
	float RimHeightBlocks = 10.0f;
};

/** Ridged terrain profile that raises mountain-like folds from a provider surface without changing ownership. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FRidgedFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Maximum upward ridge height in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Ridged", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum upward ridge height in authored blocks. This raises visible terrain while the provider domain remains unchanged."))
	float RidgeHeightBlocks = 36.0f;

	/** Maximum downward valley depth between ridges in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Ridged", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum downward valley depth between ridges in authored blocks. Use this with Ridge Height to increase min/max terrain contrast."))
	float ValleyDepthBlocks = 12.0f;

	/** Domain scale for ridge noise. Smaller values produce broader ridges and mountain chains. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Ridged", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for ridge noise. Smaller values produce broader ridges and mountain chains."))
	float RidgeScale = 0.85f;

	/** Power applied to the normalized ridge mask. Higher values make narrower, sharper ridges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Ridged", meta = (ClampMin = "0.1", UIMin = "0.1", ToolTip = "Power applied to the normalized ridge mask. Higher values make narrower, sharper ridges."))
	float RidgeSharpness = 1.1f;

	/** Seed offset applied to the ridge noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Ridged", meta = (ToolTip = "Seed offset applied to the ridge noise."))
	int32 SeedOffset = 211;
};

/** Terraced terrain profile that converts surface noise into stepped plateaus. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FTerracedFoundationTerrainProfilePayload : public FFoundationTerrainProfilePayloadBase
{
	GENERATED_BODY()

	/** Surface variation range before terracing in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Terraced", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Surface variation range before terracing in authored blocks. Increase this with Step Height to create visible plateaus."))
	float SurfaceNoiseAmplitude = 24.0f;

	/** Domain scale for the pre-terrace surface noise. Smaller values create larger plateau regions. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Terraced", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for the pre-terrace surface noise. Smaller values create larger plateau regions."))
	float SurfaceNoiseScale = 0.65f;

	/** Terrace height in authored blocks. Larger values create fewer, taller steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Terraced", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Terrace height in authored blocks. Larger values create fewer, taller steps."))
	float TerraceStepHeight = 4.0f;

	/** Terrace edge smoothing. Zero creates hard steps; higher values round the transition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Terraced", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Terrace edge smoothing. Zero creates hard steps; higher values round the transition."))
	float TerraceSmoothness = 0.15f;

	/** Seed offset applied to the terraced surface noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Terrain|Terraced", meta = (ToolTip = "Seed offset applied to the terraced surface noise."))
	int32 SeedOffset = 307;
};
