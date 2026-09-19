// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ChunkWorldGeneratedBlockProbeLibrary.generated.h"

class AChunkWorld;

/**
 * Defines a bounded generated-block probe for opt-in runtime validation.
 */
USTRUCT(BlueprintType)
struct FGeneratedBlockColumnProbeRequest
{
	GENERATED_BODY()

	/** Center block-world position used for generated grid offsets and explicit relative column offsets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug")
	FIntVector CenterBlockWorldPos = FIntVector::ZeroValue;

	/** Inclusive lowest Z block-world coordinate scanned in each column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug")
	int32 MinZBlock = 0;

	/** Inclusive highest Z block-world coordinate scanned in each column. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug")
	int32 MaxZBlock = 255;

	/** Square generated probe radius in X/Y blocks when RelativeColumnOffsets is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug", meta = (ClampMin = "0"))
	int32 RadiusXYBlocks = 0;

	/** Step in X/Y blocks for generated square-grid probes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug", meta = (ClampMin = "1"))
	int32 StepXYBlocks = 1;

	/** Optional hand-picked X/Y offsets from CenterBlockWorldPos. When set, RadiusXYBlocks and StepXYBlocks are ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug")
	TArray<FIntPoint> RelativeColumnOffsets;

	/** Material index treated as empty/air when deciding whether a generated block is solid. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug")
	int32 EmptyMaterialIndex = 0x000FFFFF;

	/** Maximum columns this request may scan; protects temporary probes from accidentally scanning huge areas. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug", meta = (ClampMin = "1"))
	int32 MaxColumns = 256;

	/** Maximum Z cells each column may scan; protects temporary probes from accidentally scanning huge vertical ranges. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug", meta = (ClampMin = "1"))
	int32 MaxZSpanBlocks = 512;

	/** Uses Porism's non-blocking debug cache read. This can miss unloaded/cache-locked blocks but is safer for temporary per-frame probes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ChunkWorld|Debug")
	bool bUseNonBlockingRead = false;
};

/**
 * Summarizes one generated block column after scanning its material values.
 */
USTRUCT(BlueprintType)
struct FGeneratedBlockColumnProbeResult
{
	GENERATED_BODY()

	/** X/Y block-world coordinate for the scanned column. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	FIntPoint BlockWorldXY = FIntPoint::ZeroValue;

	/** True when the column contained at least one non-empty material in the requested Z range. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	bool bFoundSolid = false;

	/** Lowest non-empty Z found in this column. Only meaningful when bFoundSolid is true. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 LowestSolidZ = 0;

	/** Highest non-empty Z found in this column. Only meaningful when bFoundSolid is true. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 HighestSolidZ = 0;

	/** Material index read at HighestSolidZ. Only meaningful when bFoundSolid is true. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 HighestSolidMaterialIndex = 0;

	/** Number of non-empty material cells found in the requested Z range. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 SolidBlockCount = 0;
};

/**
 * Full result for a generated-block probe request.
 */
USTRUCT(BlueprintType)
struct FGeneratedBlockProbeReport
{
	GENERATED_BODY()

	/** True when at least one scanned column contained a non-empty generated block. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	bool bFoundAnySolid = false;

	/** Number of column requests accepted after safety bounds were applied. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 SampledColumnCount = 0;

	/** Number of accepted columns that contained at least one non-empty generated block. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 SolidColumnCount = 0;

	/** Lowest highest-solid-Z across solid columns. Only meaningful when bFoundAnySolid is true. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 MinHighestSolidZ = 0;

	/** Highest highest-solid-Z across solid columns. Only meaningful when bFoundAnySolid is true. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	int32 MaxHighestSolidZ = 0;

	/** Human-readable compact summary suitable for one-off validation logs. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	FString Summary;

	/** Per-column scan details. */
	UPROPERTY(BlueprintReadOnly, Category = "ChunkWorld|Debug")
	TArray<FGeneratedBlockColumnProbeResult> Columns;
};

/**
 * Explicit opt-in helpers for validating blocks that Porism actually generated into a chunk world.
 *
 * These helpers intentionally do not hook into AChunkWorldExtended startup events. The chunk-world
 * startup-ready signal is driven by registered ChunkWorldWalker updates; editor/manual generation
 * paths can generate chunks without those walker updates, so callers should invoke this utility from
 * an explicit temporary debug command, editor button, or delayed validation step after generation settles.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UChunkWorldGeneratedBlockProbeLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Scans generated material values in bounded block-world columns without adding any generation-time cost.
	 */
	UFUNCTION(BlueprintCallable, Category = "ChunkWorld|Debug", meta = (DevelopmentOnly))
	static bool ProbeGeneratedBlockColumns(
		AChunkWorld* ChunkWorld,
		const FGeneratedBlockColumnProbeRequest& Request,
		FGeneratedBlockProbeReport& OutReport);

	/**
	 * Runs ProbeGeneratedBlockColumns and logs the compact report plus optional per-column details.
	 */
	UFUNCTION(BlueprintCallable, Category = "ChunkWorld|Debug", meta = (DevelopmentOnly))
	static bool LogGeneratedBlockColumns(
		AChunkWorld* ChunkWorld,
		const FGeneratedBlockColumnProbeRequest& Request,
		bool bLogEachColumn,
		FGeneratedBlockProbeReport& OutReport);
};
