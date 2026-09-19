// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "IslandBiomeStrategyPayloads.generated.h"

/** Island body shape settings authored in blocks. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FIslandFoundationBodySettings
{
	GENERATED_BODY()

	/** Authored-block top-center surface reference for this island provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ToolTip = "Authored-block top-center surface reference for this island provider. Center.Z is the intended visible top face, not the bottom of the generated world."))
	FVector Center = FVector::ZeroVector;

	/** Primary playable radius of the island top in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Primary playable radius of the island top in authored blocks. Terrain profiles can add visual variation inside this provider shape."))
	float TopRadius = 120.0f;

	/** Height from the internal body center to the visible top face in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Height from the internal body center to the visible top face in authored blocks. This affects the island body shape, not terrain noise."))
	float TopHeight = 30.0f;

	/** Depth from the internal body center down to the island bottom in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Depth from the internal body center down to the island bottom in authored blocks."))
	float BottomDepth = 95.0f;

	/** Vertical thickness retained near the rim before the underside curves toward the bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Vertical thickness retained near the rim before the underside curves toward the bottom."))
	float RimThickness = 20.0f;

	/** Extra horizontal radius added through the middle/lower body, fading out at the top and bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra horizontal radius added through the middle/lower body, fading out at the top and bottom. This is an analytic shape control and does not add noise cost."))
	float SideBulgeBlocks = 0.0f;

	/** Blends the underside from the original broad curve toward a sharper lower cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", ToolTip = "Blends the underside from the original broad curve toward a sharper lower cone. This is an analytic shape control and does not add noise cost."))
	float LowerConeSharpness = 0.0f;

	/** Extra downward depth at the bottom-center point for a more stalactite-like island silhouette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Shape", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra downward depth at the bottom-center point for a more stalactite-like island silhouette. This is an analytic shape control and expands the provider domain downward automatically."))
	float BottomPointDepthBlocks = 0.0f;
};

/** Island-specific rim and underside detail that applies regardless of the selected top terrain profile. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FIslandFoundationBodyDetailSettings
{
	GENERATED_BODY()

	/** If true, the island body adds low-frequency rim radius noise. Disable to avoid the rim noise nodes entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Rim", meta = (ToolTip = "If true, the island body adds low-frequency rim radius noise. Disable to avoid the rim noise nodes entirely."))
	bool bEnableRimNoise = true;

	/** Low-frequency authored-block rim radius variation used to break up the island's circular silhouette. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Rim", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableRimNoise", EditConditionHides, ToolTip = "Low-frequency authored-block rim radius variation used to break up the island's circular silhouette. The provider domain expands by this amount automatically."))
	float RimNoiseAmplitude = 8.0f;

	/** Domain scale for rim noise. Smaller values produce broader rim waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Rim", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableRimNoise", EditConditionHides, ToolTip = "Domain scale for rim noise. Smaller values produce broader rim waves."))
	float RimNoiseScale = 0.45f;

	/** If true, the island body adds downward-only underside protrusion noise. Disable to avoid the underside noise nodes entirely. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Underside", meta = (ToolTip = "If true, the island body adds downward-only underside protrusion noise. Disable to avoid the underside noise nodes entirely."))
	bool bEnableUndersideNoise = true;

	/** Maximum authored-block downward underside protrusion. This only pulls the underside lower and fades out near the rim. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Underside", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableUndersideNoise", EditConditionHides, ToolTip = "Maximum authored-block downward underside protrusion. This only pulls the underside lower, fades near the rim, and expands the provider domain downward automatically."))
	float UndersideNoiseAmplitude = 6.0f;

	/** Domain scale for underside protrusion noise. Smaller values produce broader underside features. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Underside", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableUndersideNoise", EditConditionHides, ToolTip = "Domain scale for underside protrusion noise. Smaller values produce broader underside features."))
	float UndersideNoiseScale = 0.55f;

	/** If true, adds an extra downward-only noisy spike near the bottom center. Disabled by default because it adds another noise field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Bottom Spikes", meta = (ToolTip = "If true, adds an extra downward-only noisy spike near the bottom center. Disabled by default because it adds another noise field."))
	bool bEnableBottomSpikeNoise = false;

	/** Maximum authored-block depth for optional bottom-center spike noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Bottom Spikes", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableBottomSpikeNoise", EditConditionHides, ToolTip = "Maximum authored-block depth for optional bottom-center spike noise. This pulls the lower point downward and expands the provider domain downward automatically."))
	float BottomSpikeNoiseAmplitude = 10.0f;

	/** Domain scale for optional bottom-center spike noise. Smaller values produce broader hanging points. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Bottom Spikes", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableBottomSpikeNoise", EditConditionHides, ToolTip = "Domain scale for optional bottom-center spike noise. Smaller values produce broader hanging points."))
	float BottomSpikeNoiseScale = 0.75f;

	/** Seed offset applied to island rim and underside detail noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Body Detail|Noise", meta = (EditCondition = "bEnableRimNoise || bEnableUndersideNoise || bEnableBottomSpikeNoise", EditConditionHides, ToolTip = "Seed offset applied to island rim, underside, and bottom-spike detail noise. Top-surface terrain profiles use their own seed offsets."))
	int32 SeedOffset = 0;
};

/** Island foundation payload used by the first generic biome strategy implementation. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FIslandFoundationShapePayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	/** Island body shape settings authored in blocks. Top-surface terrain detail is configured through the provider Terrain Profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island", meta = (ToolTip = "Island body shape settings authored in blocks and converted to Porism noise coordinates by the biome strategy builders. Top-surface terrain detail is configured through the provider Terrain Profile."))
	FIslandFoundationBodySettings IslandBody;

	/** Island-specific rim and underside detail that applies to flat, noisy, bowl, and future top-surface profiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island", meta = (ToolTip = "Island-specific rim and underside detail that applies to flat, noisy, bowl, and future top-surface profiles."))
	FIslandFoundationBodyDetailSettings BodyDetail;

	/** Extra authored block horizontal padding added to the cheap domain envelope around IslandBody. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Domain", meta = (DisplayName = "Domain Radius Padding (Blocks)", ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra authored-block horizontal padding added to the cheap foundation-domain envelope. This affects biome ownership reach, not visible terrain detail."))
	float DomainRadiusPadding = 15.0f;

	/** Extra authored block vertical padding added to the cheap domain envelope around IslandBody. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Island|Domain", meta = (DisplayName = "Domain Vertical Padding (Blocks)", ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra authored-block vertical padding added to the cheap foundation-domain envelope. Positive Surface Z Lift values extend the domain top automatically for support slopes."))
	float DomainVerticalPadding = 15.0f;
};
