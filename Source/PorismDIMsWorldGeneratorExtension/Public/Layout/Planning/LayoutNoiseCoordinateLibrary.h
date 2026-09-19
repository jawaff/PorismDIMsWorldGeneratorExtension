// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LayoutNoiseCoordinateLibrary.generated.h"

/** Minimal Porism world-noise coordinate settings needed by planning-window samplers. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutNoiseCoordinateSettings
{
	GENERATED_BODY()

	/** Base block size in Unreal units; copied from the owning world-gen definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Base block size in Unreal units; copied from the owning world-gen definition."))
	int32 BaseBlockSize = 1;

	/** Porism noise scale vector from the owning world-gen definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Porism noise scale vector from the owning world-gen definition."))
	FVector NoiseScale = FVector::OneVector;

	/** Porism noise coordinate offset in Unreal units from the owning world-gen definition. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Porism noise coordinate offset in Unreal units from the owning world-gen definition."))
	FIntVector NoiseCoordinateOffset = FIntVector::ZeroValue;
};

/** Pure helpers for matching planning-window samples to Porism FastNoise coordinates. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutNoiseCoordinateLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Converts a base-block world position into the FastNoise coordinate Porism uses for world generation. */
	UFUNCTION(BlueprintPure, Category = "Layout|Planning")
	static FVector BlockWorldPositionToNoiseCoordinate(
		FIntVector BlockWorldPosition,
		const FLayoutNoiseCoordinateSettings& Settings);

	/** Converts a base-block XY position into the FastNoise coordinate Porism uses for XY planning samples. */
	UFUNCTION(BlueprintPure, Category = "Layout|Planning")
	static FVector2D BlockWorldXYToNoiseCoordinate(
		FIntPoint BlockWorldXY,
		const FLayoutNoiseCoordinateSettings& Settings);
};
