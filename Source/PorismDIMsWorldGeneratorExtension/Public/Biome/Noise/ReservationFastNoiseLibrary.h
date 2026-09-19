// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "ReservationFastNoiseLibrary.generated.h"

/** Side ownership metadata for mirrored or team-oriented biome reservations. */
UENUM(BlueprintType)
enum class EBiomeReservationOwnershipRole : uint8
{
	Neutral,
	SideA,
	SideB
};

/** Axis used when a biome reservation exposes mirrored-pair authoring metadata. */
UENUM(BlueprintType)
enum class EBiomeReservationMirrorAxis : uint8
{
	X,
	Y
};

/**
 * Project-owned extension helpers for assembling common signed reservation
 * masks inside FastNoiseEditor Blueprint graphs.
 */
UCLASS()
class PORISMDIMSWORLDGENERATOREXTENSION_API UReservationFastNoiseLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns true when the supplied FastNoiseEditor node link references an authored node. */
	UFUNCTION(BlueprintPure, Category = "Reservation Noise")
	static bool IsReservationNoiseLinkValid(FNodeLink Link);

	/** Builds a positive-inside sphere mask: Radius - DistanceToPoint(Center). */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildSphereReservation(UFastNoiseEditor* Editor, FVector Center, float Radius);

	/** Builds a positive-inside round XY footprint mask with no vertical constraint. */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildRoundReservationXY(UFastNoiseEditor* Editor, FVector2D CenterXY, float Radius);

	/** Builds a side-aware mirrored round XY footprint; Neutral returns the union of both sides. */
	UFUNCTION(BlueprintCallable, Category = "Biome|Reservation Noise|Symmetry", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildMirroredRoundReservationXY(
		UFastNoiseEditor* Editor,
		FVector2D MirrorOriginXY,
		EBiomeReservationMirrorAxis MirrorAxis,
		float MirrorDistance,
		float Radius,
		EBiomeReservationOwnershipRole OwnershipRole);

	/** Builds a positive-inside uniform cube mask using MaxAxis distance from Center. */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildUniformCubeReservation(UFastNoiseEditor* Editor, FVector Center, float HalfExtent);

	/** Builds repeated positive-inside cellular pockets in XY using nearest-cell distance. */
	UFUNCTION(BlueprintCallable, Category = "Biome|Reservation Noise|Repeated", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildRepeatedCellReservationXY(
		UFastNoiseEditor* Editor,
		float CellScale,
		float PocketRadius,
		int32 SeedOffset);

	/** Builds repeated positive-inside cellular pockets in XY using the requested nearest-cell distance function. */
	UFUNCTION(BlueprintCallable, Category = "Biome|Reservation Noise|Repeated", meta = (DefaultToSelf = "Editor"))
	static FNodeLink BuildRepeatedCellReservationXYWithDistance(
		UFastNoiseEditor* Editor,
		float CellScale,
		float PocketRadius,
		int32 SeedOffset,
		FastNoiseDistanceFunction DistanceFunction);

	/** Builds the union of positive-inside reservation masks using chained Max nodes. */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink UnionReservations(UFastNoiseEditor* Editor, const TArray<FNodeLink>& Reservations);

	/** Builds the intersection of two positive-inside masks using Min. */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink IntersectReservations(UFastNoiseEditor* Editor, FNodeLink A, FNodeLink B);

	/** Builds a soft intersection of two positive-inside masks using MinSmooth. */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor", ClampMin = "0.0", UIMin = "0.0"))
	static FNodeLink IntersectReservationsSmooth(UFastNoiseEditor* Editor, FNodeLink A, FNodeLink B, float Smoothness);

	/** Carves Reservation out of Base using Min(Base, -Reservation). */
	UFUNCTION(BlueprintCallable, Category = "Reservation Noise", meta = (DefaultToSelf = "Editor"))
	static FNodeLink CarveReservationFromBase(UFastNoiseEditor* Editor, FNodeLink Base, FNodeLink Reservation);
};
