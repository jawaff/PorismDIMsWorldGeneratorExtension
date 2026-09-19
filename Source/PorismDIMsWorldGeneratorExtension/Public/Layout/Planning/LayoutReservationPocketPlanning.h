// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "LayoutReservationPocketPlanning.generated.h"

/** One sampled active-biome value inside a planning window. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutReservationPocketSample
{
	GENERATED_BODY()

	/** Integer sample coordinate used for connected-region grouping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Integer sample coordinate used for connected-region grouping."))
	FIntPoint SampleGridXY = FIntPoint::ZeroValue;

	/** Block-space XY position represented by this sample. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Block-space XY position represented by this sample."))
	FIntPoint BlockXY = FIntPoint::ZeroValue;

	/** Sample value used for pocket eligibility; values at or above the threshold are eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Sample value used for pocket eligibility; values at or above the threshold are eligible."))
	float NoiseValue = 0.0f;

	/** If true, this frozen planning sample also preserves the discovered top solid surface Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "If true, this frozen planning sample also preserves the discovered top solid surface Z."))
	bool bHasSurfaceZ = false;

	/** Preserved top solid surface Z when the planning sample was backed by an eligible biome surface. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Preserved top solid surface Z when the planning sample was backed by an eligible biome surface."))
	int32 SurfaceZBlockWorld = 0;
};

/** Connected positive reservation-capable region discovered from sampled active-biome values. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutReservationPocket
{
	GENERATED_BODY()

	/** Number of eligible samples in this connected pocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Number of eligible samples in this connected pocket."))
	int32 SampleCount = 0;

	/** Inclusive minimum sample-grid coordinate in this pocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Inclusive minimum sample-grid coordinate in this pocket."))
	FIntPoint MinSampleGridXY = FIntPoint::ZeroValue;

	/** Inclusive maximum sample-grid coordinate in this pocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Inclusive maximum sample-grid coordinate in this pocket."))
	FIntPoint MaxSampleGridXY = FIntPoint::ZeroValue;

	/** Inclusive minimum block-space coordinate in this pocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Inclusive minimum block-space coordinate in this pocket."))
	FIntPoint MinBlockXY = FIntPoint::ZeroValue;

	/** Inclusive maximum block-space coordinate in this pocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Inclusive maximum block-space coordinate in this pocket."))
	FIntPoint MaxBlockXY = FIntPoint::ZeroValue;

	/** Fast geometric center based on the mean block-space position of eligible samples. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Fast geometric center based on the mean block-space position of eligible samples."))
	FVector2D CentroidBlockXY = FVector2D::ZeroVector;

	/** Deterministic eligible sample block positions in this pocket, sorted by block-space XY for later candidate fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic eligible sample block positions in this pocket, sorted by block-space XY for later candidate fallback."))
	TArray<FIntPoint> SampleBlockXYs;

	/** Deterministic eligible sample nearest the geometric centroid, used to keep centroid selection on the discovered sample lattice. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic eligible sample nearest the geometric centroid, used to keep centroid selection on the discovered sample lattice."))
	FIntPoint CentroidNearestSampleBlockXY = FIntPoint::ZeroValue;

	/** Deterministic sample closest to the pocket interior, used when centroid placement fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Deterministic sample closest to the pocket interior, used when centroid placement fails."))
	FIntPoint ApproximateInteriorBlockXY = FIntPoint::ZeroValue;
};

/** Settings for connected-pocket extraction from sampled active-biome values. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutReservationPocketPlanningSettings
{
	GENERATED_BODY()

	/** Minimum sampled value required for a sample to belong to a reservation pocket. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ToolTip = "Minimum sampled value required for a sample to belong to a reservation pocket."))
	float EligibilityThreshold = 0.0f;

	/** Minimum eligible samples required before a pocket is returned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "1", UIMin = "1", ToolTip = "Minimum eligible samples required before a pocket is returned."))
	int32 MinimumSampleCount = 1;

	/** Maximum eligible samples allowed before a pocket is rejected; zero disables the upper bound. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Planning", meta = (ClampMin = "0", UIMin = "0", ToolTip = "Maximum eligible samples allowed before a pocket is rejected; zero disables the upper bound."))
	int32 MaximumSampleCount = 0;

};

/** Pure connected-region helpers used by planning-window runtime code. */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API ULayoutReservationPocketPlanningLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Finds connected eligible reservation pockets from a finite set of sampled values. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	static TArray<FLayoutReservationPocket> FindConnectedReservationPockets(
		const TArray<FLayoutReservationPocketSample>& Samples,
		const FLayoutReservationPocketPlanningSettings& Settings);

	/** Finds connected reservation pockets while removing samples covered by an exclusion field. */
	UFUNCTION(BlueprintCallable, Category = "Layout|Planning")
	static TArray<FLayoutReservationPocket> FindConnectedReservationPocketsWithExclusions(
		const TArray<FLayoutReservationPocketSample>& Samples,
		const TArray<FLayoutReservationPocketSample>& ExclusionSamples,
		const FLayoutReservationPocketPlanningSettings& Settings,
		float ExclusionThreshold = 0.0f);
};
