// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "FoundationFastNoiseLibrary.generated.h"

/**
 * Parameters for a signed floating-island body field.
 *
 * The generated field is positive inside the island body and negative outside.
 * Values are authored in the same noise coordinate space as the owning
 * FastNoiseEditor graph.
 */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FFloatingIslandBodyNoiseSettings
{
	GENERATED_BODY()

	/** Center of the island in the noise graph coordinate space. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise", meta = (ToolTip = "Center of the island in the noise graph coordinate space."))
	FVector Center = FVector::ZeroVector;

	/** Primary playable radius of the island top in noise units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Primary playable radius of the island top in noise units."))
	float TopRadius = 2.0f;

	/** Height above Center.Z where the noisy upper surface is placed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Height above Center.Z where the noisy upper surface is placed."))
	float TopHeight = 0.35f;

	/** Depth below Center.Z where the curved underside reaches its lowest point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Depth below Center.Z where the curved underside reaches its lowest point."))
	float BottomDepth = 1.45f;

	/** Vertical thickness retained at the island rim before the underside curves down toward the bottom point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Vertical thickness retained at the island rim before the underside curves down toward the bottom point."))
	float RimThickness = 0.2f;

	/** Extra horizontal radius added through the middle/lower body, fading out at the top and bottom. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Shape", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra horizontal radius added through the middle/lower body, fading out at the top and bottom."))
	float SideBulge = 0.0f;

	/** Blends the underside from the original broad curve toward a sharper lower cone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Shape", meta = (ClampMin = "0.0", ClampMax = "1.0", UIMin = "0.0", UIMax = "1.0", ToolTip = "Blends the underside from the original broad curve toward a sharper lower cone."))
	float LowerConeSharpness = 0.0f;

	/** Extra downward depth at the bottom-center point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Shape", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra downward depth at the bottom-center point."))
	float BottomPointDepth = 0.0f;

	/** Low-frequency rim radius variation; useful for less circular island silhouettes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Low-frequency rim radius variation; useful for less circular island silhouettes."))
	float RimNoiseAmplitude = 0.12f;

	/** Domain scale for rim noise; smaller values produce broader rim waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for rim noise; smaller values produce broader rim waves."))
	float RimNoiseScale = 0.45f;

	/** Upper-surface height variation; keep this modest near reservations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Upper-surface height variation; keep this modest near reservations."))
	float SurfaceNoiseAmplitude = 0.08f;

	/** Domain scale for upper-surface noise; smaller values produce broader hills and bowls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for upper-surface noise; smaller values produce broader hills and bowls."))
	float SurfaceNoiseScale = 0.65f;

	/** Maximum downward underside protrusion used to keep the curved island bottom from looking perfectly lathed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum downward underside protrusion used to keep the curved island bottom from looking perfectly lathed."))
	float UndersideNoiseAmplitude = 0.06f;

	/** Domain scale for underside noise; smaller values produce broader side and underside waves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for underside noise; smaller values produce broader side and underside waves."))
	float UndersideNoiseScale = 0.55f;

	/** Maximum downward bottom-center spike noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Maximum downward bottom-center spike noise."))
	float BottomSpikeNoiseAmplitude = 0.0f;

	/** Domain scale for bottom-center spike noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Domain scale for bottom-center spike noise."))
	float BottomSpikeNoiseScale = 0.75f;

	/** Seed offset applied to generated detail noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation Noise|Detail", meta = (ToolTip = "Seed offset applied to generated detail noise."))
	int32 SeedOffset = 0;
};

/**
 * Project-owned extension helpers for assembling signed foundation body masks
 * inside FastNoiseEditor Blueprint graphs.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UFoundationFastNoiseLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Builds a positive-inside floating-island field with noisy top, rim, and curved underside. */
	UFUNCTION(BlueprintCallable, Category = "Foundation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildFloatingIslandBody(UFastNoiseEditor* Editor, const FFloatingIslandBodyNoiseSettings& Settings);

	/** Builds a positive-inside floating-island field and adds an optional surface-height offset node to the top surface. */
	static FNodeLink BuildFloatingIslandBodyWithSurfaceOffset(UFastNoiseEditor* Editor, const FFloatingIslandBodyNoiseSettings& Settings, FNodeLink SurfaceHeightOffset);
};
