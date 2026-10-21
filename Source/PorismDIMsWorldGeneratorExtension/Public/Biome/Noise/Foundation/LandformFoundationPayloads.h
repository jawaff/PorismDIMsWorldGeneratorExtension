// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "LandformFoundationPayloads.generated.h"

/** A connected chain of finite bridge spans. Points are authored top-face positions, not island references. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FArchBridgeFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Bridge", meta = (ToolTip = "Two to 32 ordered deck points in authored blocks. Each consecutive pair forms an arch; overlap endpoints with island terrain to join them. Heights may differ. Branches use another bridge child."))
	TArray<FVector> DeckPoints = { FVector(-100, 0, 0), FVector(100, 0, 0) };

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Bridge", meta = (ClampMin = "2", ToolTip = "Half-width in blocks. Rounded span ends join adjacent spans without gaps."))
	float HalfWidthBlocks = 12.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Bridge", meta = (ClampMin = "2", ToolTip = "Deck-to-underside distance at each arch crown. Negative surface noise retains two blocks of bridge body, or the scaled crown thickness if smaller."))
	float CrownThicknessBlocks = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Bridge", meta = (ClampMin = "0", ToolTip = "Additional underside depth at span ends. The underside rises parabolically to the crown at midspan; zero gives a slab."))
	float ArchRiseBlocks = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Bridge", meta = (ClampMin = "1", ToolTip = "Analytic ownership clearance in blocks around the body. Surface-profile positive height is included separately."))
	float DomainPaddingBlocks = 2.0f;
};

/** Finite elliptical mountain rooted at BaseCenter. Repeat with Multi-Instance for a range. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FMountainFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Mountain", meta = (ToolTip = "Base-center position in authored blocks. Overlap the base with a plane or island when a grounded mountain is wanted."))
	FVector BaseCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Mountain", meta = (ToolTip = "Positive X/Y base radii in blocks. Unequal radii stretch the mountain into a ridge."))
	FVector2D BaseRadiiBlocks = FVector2D(80, 80);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Mountain", meta = (ClampMin = "2", ToolTip = "Summit height above Base Center, before the terrain profile."))
	float HeightBlocks = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Mountain", meta = (ClampMin = "0", ClampMax = "0.9", ToolTip = "Flat summit radius as a fraction of the base radius. Zero gives a peak; larger values give a truncated mountain."))
	float SummitRadiusFraction = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Mountain", meta = (ClampMin = "0.25", ClampMax = "4", ToolTip = "Exponent of the base-to-summit falloff. One is conical; larger values narrow the upper slopes."))
	float SlopePower = 1.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Mountain", meta = (ClampMin = "1", ToolTip = "Analytic ownership clearance in blocks; also includes possible positive terrain-profile height."))
	float DomainPaddingBlocks = 2.0f;
};

/** Tapered finite column. Broad top radii make a butte or mesa; wall detail is independent of the top profile. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FColumnFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ToolTip = "Base-center position in authored blocks. Place the base inside supporting terrain for a grounded column."))
	FVector BaseCenter = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ToolTip = "Positive elliptical base radii in blocks. Larger than top radii gives tapered sides."))
	FVector2D BaseRadiiBlocks = FVector2D(22, 22);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ToolTip = "Positive elliptical top radii in blocks. Use wide radii for mesas and small radii for narrow rock towers."))
	FVector2D TopRadiiBlocks = FVector2D(15, 15);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ClampMin = "2", ToolTip = "Top-face height above the base, before the terrain profile."))
	float HeightBlocks = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ClampMin = "0", ToolTip = "Wall displacement in blocks along the shorter radius. Must be less than half the smallest radius. Zero gives clean tapered walls."))
	float WallNoiseAmplitudeBlocks = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ClampMin = "0.001", ToolTip = "Noise-coordinate frequency for wall detail; larger values make finer features."))
	float WallNoiseScale = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ClampMin = "0", ToolTip = "Vertical frequency relative to horizontal wall noise. Zero gives vertical flutes; small values produce slowly varying rock columns."))
	float WallVerticalScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ToolTip = "Deterministic offset from the world noise seed for wall variation."))
	int32 WallSeedOffset = 419;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Column", meta = (ClampMin = "1", ToolTip = "Analytic ownership clearance. Bounds also include worst-case wall displacement and positive top-profile height."))
	float DomainPaddingBlocks = 2.0f;
};

/** Finite bowl subtraction. Center is the opening; the terrain profile shapes the floor, never adds a biome. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FPitFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ToolTip = "Opening-center position in authored blocks. Place at or above a mountain summit and use a smaller radius to carve a caldera."))
	FVector Center = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ToolTip = "Positive X/Y opening radii in blocks, before optional rim noise."))
	FVector2D OpeningRadiiBlocks = FVector2D(35, 35);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ClampMin = "2", ToolTip = "Maximum depth below Center.Z. Floor-profile displacement is clamped inside this depth and the opening."))
	float DepthBlocks = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ClampMin = "0", ClampMax = "0.9", ToolTip = "Flat bottom radius as a fraction of the opening radius. Zero gives a rounded bowl; larger values widen the pit floor."))
	float BottomRadiusFraction = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ClampMin = "0", ToolTip = "Bounded opening/wall displacement in blocks along the shorter radius. Must be less than half that radius."))
	float RimNoiseAmplitudeBlocks = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ClampMin = "0.001", ToolTip = "Noise-coordinate frequency for rim variation. Larger values produce finer crater edges."))
	float RimNoiseScale = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ToolTip = "Deterministic offset from the world noise seed for rim variation."))
	int32 RimSeedOffset = 521;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Pit", meta = (ClampMin = "1", ToolTip = "Carved air above Center.Z in blocks. Raise Center.Z or this clearance to reach through a noisy mountain summit."))
	float OpeningTopPaddingBlocks = 2.0f;
};
