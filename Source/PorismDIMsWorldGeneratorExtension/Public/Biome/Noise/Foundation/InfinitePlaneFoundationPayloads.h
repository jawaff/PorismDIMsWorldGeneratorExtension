// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"

#include "InfinitePlaneFoundationPayloads.generated.h"

/** Distance shape used by repeated infinite-foundation domain holes. */
UENUM(BlueprintType)
enum class EInfiniteFoundationRepeatedHoleShape : uint8
{
	/** Round cellular holes. */
	Round,

	/** Square/blocky cellular holes. */
	Square
};

/** Base struct for infinite-foundation DomainNoise-only masks. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FInfiniteFoundationDomainMaskPayloadBase
{
	GENERATED_BODY()
};

/** No-op domain mask for infinite foundations. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FInfiniteFoundationNoDomainMaskPayload : public FInfiniteFoundationDomainMaskPayloadBase
{
	GENERATED_BODY()
};

/** Low-frequency noise patch mask for infinite foundation biome ownership. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FInfiniteFoundationNoisePatchDomainMaskPayload : public FInfiniteFoundationDomainMaskPayloadBase
{
	GENERATED_BODY()

	/** Domain scale for broad patch noise. Smaller values create larger biome patches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Noise Patches", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Domain scale for broad patch noise. Smaller values create larger biome patches. This affects only DomainNoise ownership, not terrain GenA."))
	float FrequencyScale = 0.35f;

	/** Noise threshold for ownership. Higher values produce smaller patches. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Noise Patches", meta = (ToolTip = "Noise threshold for ownership. Higher values produce smaller patches. The mask is positive where noise is above this value unless Invert is enabled."))
	float Threshold = 0.0f;

	/** Scales the domain transition strength near patch edges. Zero keeps the raw noise contrast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Noise Patches", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Scales the domain transition strength near patch edges. Larger values create gentler ownership values for Porism blending; zero keeps the raw noise contrast."))
	float EdgeSoftness = 0.05f;

	/** Seed offset applied to the patch noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Noise Patches", meta = (ToolTip = "Seed offset applied to the patch noise. The strategy seed remains the root source of randomness."))
	int32 SeedOffset = 0;

	/** If true, owns the opposite side of the threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Noise Patches", meta = (ToolTip = "If true, owns the opposite side of the threshold."))
	bool bInvert = false;
};

/** Repeated subtractive holes for infinite foundation biome ownership. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FInfiniteFoundationRepeatedHolesDomainMaskPayload : public FInfiniteFoundationDomainMaskPayloadBase
{
	GENERATED_BODY()

	/** Horizontal cellular spacing in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Repeated Holes", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Horizontal cellular spacing in authored blocks. This affects only DomainNoise ownership, not terrain GenA."))
	float CellSizeBlocks = 180.0f;

	/** Radius or half-width of each repeated hole in authored blocks. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Repeated Holes", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Radius or half-width of each repeated hole in authored blocks. Keep below half of Cell Size Blocks to avoid merging holes."))
	float HoleRadiusBlocks = 35.0f;

	/** Round or square/blocky cellular hole distance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Repeated Holes", meta = (ToolTip = "Round or square/blocky cellular hole distance."))
	EInfiniteFoundationRepeatedHoleShape HoleShape = EInfiniteFoundationRepeatedHoleShape::Round;

	/** Scales the domain transition strength near hole edges. Zero keeps the raw cellular contrast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Repeated Holes", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Scales the domain transition strength near hole edges. Larger values create gentler ownership values for Porism blending; zero keeps the raw cellular contrast."))
	float EdgeSoftnessBlocks = 2.0f;

	/** Seed offset for deterministic repeated hole placement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Repeated Holes", meta = (ToolTip = "Seed offset for deterministic repeated hole placement. The strategy seed remains the root source of randomness."))
	int32 SeedOffset = 0;

	/** If true, owns only the repeated hole cells instead of subtracting them from this provider. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask|Repeated Holes", meta = (ToolTip = "If true, owns only the repeated hole cells instead of subtracting them from this provider."))
	bool bInvert = false;
};

/** Infinite X/Y foundation with a flat provider-local surface. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FInfinitePlaneFoundationPayload : public FFoundationPayloadBase
{
	GENERATED_BODY()

	FInfinitePlaneFoundationPayload()
	{
		DomainMask.InitializeAs<FInfiniteFoundationNoDomainMaskPayload>();
	}

	/** Authored-block Z height of the infinite plane surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane", meta = (DisplayName = "Surface Z (Blocks)", ToolTip = "Authored-block Z height of the infinite provider surface. Reservations authored under this provider are relative to this surface."))
	float SurfaceZ = 0.0f;

	/** Extra authored blocks above the surface where the domain remains positive for ownership/support. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain", meta = (ClampMin = "0.0", UIMin = "0.0", ToolTip = "Extra authored blocks above the surface where the domain remains positive for ownership and supported lifts."))
	float DomainTopPadding = 32.0f;

	/** Authored blocks below the surface where the domain remains positive for ownership. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain", meta = (ClampMin = "0.001", UIMin = "0.001", ToolTip = "Authored blocks below the surface where the domain remains positive for ownership."))
	float DomainBottomDepth = 128.0f;

	/** Optional infinite-foundation DomainNoise mask. This shapes biome ownership only; terrain GenA still comes from the provider Terrain Profile. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Foundation|Infinite Plane|Domain Mask", meta = (BaseStruct = "/Script/PorismDIMsWorldGeneratorExtension.InfiniteFoundationDomainMaskPayloadBase", ExcludeBaseStruct, NoClear, ToolTip = "Optional infinite-foundation DomainNoise mask. Use this for patchy biome ownership or repeated holes. It does not alter the Terrain Profile/GenA terrain fill."))
	FInstancedStruct DomainMask;
};
