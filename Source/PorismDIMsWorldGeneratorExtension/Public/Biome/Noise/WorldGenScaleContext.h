// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "ChunkWorldStructs/ChunkWorldEnums.h"

#include "WorldGenScaleContext.generated.h"

class AChunkWorldCore;
class UWorldGenDef;

/** Explicit world-generation scale settings used when no live chunk-world context is available. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FWorldGenScaleSettings
{
	GENERATED_BODY()

	/** Size of one finest/base block in Unreal units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Size of one finest/base block in Unreal units. Used to convert block-authored biome settings into Porism FastNoise coordinates."))
	int32 BaseBlockSize = 100;

	/** Per-axis noise scale from the matching Porism WorldGenDef. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ToolTip = "Per-axis noise scale from the matching Porism WorldGenDef."))
	FVector NoiseScale = FVector(1.0, 1.0, 1.0);

	/** Global Porism noise coordinate offset. The current Porism generation path divides this by BaseBlockSize before sampling noise. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ToolTip = "Global Porism noise coordinate offset. The current Porism generation path divides this by BaseBlockSize before sampling noise."))
	FIntVector NoiseCoordinateOffset = FIntVector::ZeroValue;

	/** Axis behavior used to decide whether authored zero maps to raw world origin, the finite-span noise origin, or the finite-span center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ToolTip = "Axis behavior used to decide whether authored zero maps to raw world origin, the finite-span noise origin from NoiseCoordinateOffset, or the finite-span center fallback."))
	EAxisBehavior AxisBehaviorX = EAxisBehavior::Infinity;

	/** Axis behavior used to decide whether authored zero maps to raw world origin, the finite-span noise origin, or the finite-span center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ToolTip = "Axis behavior used to decide whether authored zero maps to raw world origin, the finite-span noise origin from NoiseCoordinateOffset, or the finite-span center fallback."))
	EAxisBehavior AxisBehaviorY = EAxisBehavior::Infinity;

	/** Axis behavior used to decide whether authored zero maps to raw world origin, the finite-span noise origin, or the finite-span center. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ToolTip = "Axis behavior used to decide whether authored zero maps to raw world origin, the finite-span noise origin from NoiseCoordinateOffset, or the finite-span center fallback."))
	EAxisBehavior AxisBehaviorZ = EAxisBehavior::SingleChunk;

	/** Finite-axis block span used by editor/test fallback contexts. Ignored for infinite axes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Finite-axis block span used by editor/test fallback contexts. Ignored for infinite axes."))
	FIntVector FallbackFiniteAxisBlockSpan = FIntVector(256, 256, 256);

	/** Reference chunk detail layer. Negative uses the finest/last configured chunk layer. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Worldgen Scale", meta = (ToolTip = "Reference chunk detail layer. Negative uses the finest/last configured chunk layer."))
	int32 ReferenceDetailLevel = -1;
};

/** Immutable resolved conversion context shared by foundation, reservation, and spawn FNE biome builders. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FResolvedWorldGenScaleContext
{
	GENERATED_BODY()

	/** True when this context was resolved from a live chunk-world creator instead of only explicit/default settings. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	bool bResolvedFromChunkWorld = false;

	/** True when an explicit strategy/shared override supplied this context. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	bool bResolvedFromOverride = false;

	/** True when no live context or explicit override was available. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	bool bUsingFallbackDefaults = false;

	/** Size of one finest/base block in Unreal units. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	int32 BaseBlockSize = 100;

	/** Noise scale copied from the resolved WorldGenDef/settings. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector NoiseScale = FVector(1.0, 1.0, 1.0);

	/** Porism noise coordinate offset copied from the resolved WorldGenDef/settings. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FIntVector NoiseCoordinateOffset = FIntVector::ZeroValue;

	/** Effective block span of the selected reference chunk layer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FIntVector ReferenceChunkBlockSpan = FIntVector(256, 256, 256);

	/** Per-axis finite block span used for non-infinite axes. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector FiniteAxisBlockSpan = FVector(256.0, 256.0, 256.0);

	/** Per-axis raw-block offset for authored zero. Infinite axes are zero; finite axes prefer the in-span noise origin and otherwise use the finite center. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector AuthoredOriginToRawBlockOffset = FVector::ZeroVector;

	/** Multiplier from authored block distances to FastNoise coordinate distances. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector BlocksToNoiseScale = FVector(0.01, 0.01, 0.01);

	/** Multiplier from FastNoise coordinate distances back to authored block distances. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector NoiseToBlocksScale = FVector(100.0, 100.0, 100.0);

	/** Offset in raw block coordinates applied by Porism before converting to FastNoise coordinates. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector NoiseCoordinateOffsetBlocks = FVector::ZeroVector;

	/** Offset in FastNoise coordinates applied by Porism's noise coordinate offset. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector RawBlockToNoiseOffset = FVector::ZeroVector;

	/** Lower authored block bound for finite axes; infinite axes use large validation bounds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector AuthoredMinBlock = FVector(-1000000.0, -1000000.0, -1000000.0);

	/** Upper authored block bound for finite axes; infinite axes use large validation bounds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Worldgen Scale")
	FVector AuthoredMaxBlock = FVector(1000000.0, 1000000.0, 1000000.0);

	/** Converts an authored block-space position into raw Porism block-world coordinates. */
	FVector AuthoredBlockPositionToRawBlock(const FVector& AuthoredBlockPosition) const;

	/** Converts an authored block-space position into Porism FastNoise coordinates. */
	FVector AuthoredBlockPositionToNoise(const FVector& AuthoredBlockPosition) const;

	/** Converts an authored block-space distance vector into Porism FastNoise coordinate distances. */
	FVector BlockDistanceToNoise(const FVector& BlockDistance) const;

	/** Converts a Porism FastNoise position back into authored block-space coordinates. */
	FVector NoisePositionToAuthoredBlock(const FVector& NoisePosition) const;

	/** Converts a Porism FastNoise Z coordinate back into authored block-space Z. */
	float NoiseZToAuthoredBlockZ(float NoiseZ) const;
};

/** Resolves and caches block-unit conversion data for Porism FNE graph construction. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FWorldGenScaleContextResolver
{
	/** Resolves scale context from the creator chunk world, explicit override, or fallback settings. */
	static FResolvedWorldGenScaleContext Resolve(
		UObject* Creator,
		const FWorldGenScaleSettings* ExplicitOverride,
		bool bUseExplicitOverride);

	/** Clears cached resolved contexts. Intended for tests or editor tooling after settings changes. */
	static void ClearCache();
};
