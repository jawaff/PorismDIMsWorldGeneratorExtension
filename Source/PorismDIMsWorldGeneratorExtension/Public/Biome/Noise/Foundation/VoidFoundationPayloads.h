// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "VoidFoundationPayloads.generated.h"

/** Horizontal axis that a V-cut void follows. */
UENUM(BlueprintType)
enum class EVCutFoundationAxis : uint8
{
	/** The cut runs along the X axis and narrows across Y. */
	X,

	/** The cut runs along the Y axis and narrows across X. */
	Y
};

/** Finite subtractive V-cut field whose opening is wider at the surface than at depth. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FVCutFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	/** Authored-block center of the top opening. Center.Z is the top of the void field. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ToolTip = "Authored-block center of the top opening. Center.Z is the top of the void field; the cut extends downward by Depth Blocks."))
	FVector Center = FVector::ZeroVector;

	/** Horizontal axis the cut follows. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ToolTip = "Horizontal axis the cut follows. The cut's width narrows across the other horizontal axis."))
	EVCutFoundationAxis Axis = EVCutFoundationAxis::Y;

	/** Half-length of the cut along its axis in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Half-length of the cut along its axis in authored blocks."))
	float HalfLengthBlocks = 90.0f;

	/** Half-width of the V opening at Center.Z in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Half-width of the V opening at Center.Z in authored blocks. This should be larger than Bottom Half Width for a V-shaped cut."))
	float SurfaceHalfWidthBlocks = 35.0f;

	/** Half-width of the cut at the bottom in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Half-width of the cut at the bottom in authored blocks. Keep this below Surface Half Width for a V-shaped narrowing cut."))
	float BottomHalfWidthBlocks = 6.0f;

	/** Downward depth of the V-cut from Center.Z in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Downward depth of the V-cut from Center.Z in authored blocks."))
	float DepthBlocks = 60.0f;

	/** Optional domain strength normalization near the boundary. Zero keeps raw distance strength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Optional domain strength normalization near the boundary. Larger values create softer/weaker ownership around edges; zero keeps raw distance strength."))
	float EdgeSoftnessBlocks = 0.0f;

	/** Enables ridged edge noise, centerline wander, domain warp, and optional stepped walls for a less artificial trench. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ToolTip = "Enables optional domain detail for a less artificial trench. These controls affect the subtractive DomainNoise, so leave them disabled when a precise authored cut is needed."))
	bool bEnableOrganicVariation = false;

	/** Maximum authored-block amount that ridged noise can push the trench walls in or out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableOrganicVariation", ToolTip = "Maximum authored-block amount that ridged noise can push the trench walls in or out. Higher values make the V edges more irregular."))
	float EdgeNoiseAmplitudeBlocks = 9.0f;

	/** Domain scale for ridged wall variation. Smaller values create broader organic bends; larger values create busier edge detail. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bEnableOrganicVariation", ToolTip = "Domain scale for ridged wall variation. Smaller values create broader organic bends; larger values create busier edge detail."))
	float EdgeNoiseScale = 0.45f;

	/** Maximum authored-block sideways offset applied to the trench centerline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableOrganicVariation", ToolTip = "Maximum authored-block sideways offset applied to the trench centerline so the cut meanders instead of staying perfectly straight."))
	float CenterlineWanderAmplitudeBlocks = 24.0f;

	/** Domain scale for centerline wander along the trench axis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bEnableOrganicVariation", ToolTip = "Domain scale for centerline wander along the trench axis. Smaller values create slower bends."))
	float CenterlineWanderScale = 0.12f;

	/** Applies a gradient domain warp to the ridged edge noise before it modifies the trench walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (EditCondition = "bEnableOrganicVariation", ToolTip = "Applies a gradient domain warp to the ridged edge noise before it modifies the trench walls. This is costlier but makes repeated ridge patterns less obvious."))
	bool bEnableDomainWarp = true;

	/** Domain-warp amplitude in authored blocks for the organic wall noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableOrganicVariation && bEnableDomainWarp", ToolTip = "Domain-warp amplitude in authored blocks for the organic wall noise. Larger values bend the ridged detail more strongly."))
	float DomainWarpAmplitudeBlocks = 10.0f;

	/** Domain-warp frequency for the organic wall noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bEnableOrganicVariation && bEnableDomainWarp", ToolTip = "Domain-warp frequency for the organic wall noise. Smaller values create broader warp motion."))
	float DomainWarpFrequency = 0.25f;

	/** Quantizes the V-wall width by depth to create terrace-like steps in the subtractive cut. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Steps", meta = (EditCondition = "bEnableOrganicVariation", ToolTip = "Quantizes the V-wall width by depth to create terrace-like steps in the subtractive cut."))
	bool bEnableSteps = true;

	/** Authored-block vertical height between V-cut wall steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Steps", meta = (ClampMin = "0.001", UIMin = "0.001", EditCondition = "bEnableOrganicVariation && bEnableSteps", ToolTip = "Authored-block vertical height between V-cut wall steps. Larger values create fewer, taller shelves."))
	float StepHeightBlocks = 5.0f;

	/** Smoothness applied to stepped wall transitions. Zero keeps hard steps. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Steps", meta = (ClampMin = "0.0", UIMin = "0.0", EditCondition = "bEnableOrganicVariation && bEnableSteps", ToolTip = "Smoothness applied to stepped wall transitions. Zero keeps hard steps."))
	float StepSmoothness = 0.0f;

	/** Seed offset applied to the V-cut organic noise nodes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Void|V Cut|Organic", meta = (EditCondition = "bEnableOrganicVariation", ToolTip = "Seed offset applied to the V-cut organic noise nodes so multiple cuts can use distinct but deterministic variation."))
	int32 OrganicSeedOffset = 311;
};
