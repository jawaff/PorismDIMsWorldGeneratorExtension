// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/ReservationFastNoiseLibrary.h"

namespace
{
	FNodeLink MakeZero(UFastNoiseEditor* Editor)
	{
		return Editor != nullptr ? Editor->Constant(0.0f) : FNodeLink();
	}
}

bool UReservationFastNoiseLibrary::IsReservationNoiseLinkValid(const FNodeLink Link)
{
	return Link.Node != nullptr;
}

FNodeLink UReservationFastNoiseLibrary::BuildSphereReservation(
	UFastNoiseEditor* Editor,
	const FVector Center,
	const float Radius)
{
	if (Editor == nullptr || Radius <= 0.0f)
	{
		return MakeZero(Editor);
	}

	FNodeLink RadiusNode = Editor->Constant(Radius);
	FNodeLink DistanceNode = Editor->DistanceToPoint(FastNoiseDistanceFunction::Euclidean, Center);
	return Editor->Subtract(RadiusNode, DistanceNode);
}

FNodeLink UReservationFastNoiseLibrary::BuildRoundReservationXY(
	UFastNoiseEditor* Editor,
	const FVector2D CenterXY,
	const float Radius)
{
	if (Editor == nullptr || Radius <= 0.0f)
	{
		return MakeZero(Editor);
	}

	FNodeLink RadiusNode = Editor->Constant(Radius);
	FNodeLink DistanceNode = Editor->DistanceToPoint(
		FastNoiseDistanceFunction::Euclidean,
		FVector(CenterXY.X, CenterXY.Y, 0.0));
	FNodeLink PlanarDistanceNode = Editor->DomainAxisScale(DistanceNode, FVector(1.0, 1.0, 0.0));
	return Editor->Subtract(RadiusNode, PlanarDistanceNode);
}

FNodeLink UReservationFastNoiseLibrary::BuildMirroredRoundReservationXY(
	UFastNoiseEditor* Editor,
	const FVector2D MirrorOriginXY,
	const EBiomeReservationMirrorAxis MirrorAxis,
	const float MirrorDistance,
	const float Radius,
	const EBiomeReservationOwnershipRole OwnershipRole)
{
	if (Editor == nullptr || MirrorDistance <= 0.0f || Radius <= 0.0f)
	{
		return MakeZero(Editor);
	}

	const FVector2D MirrorOffset = MirrorAxis == EBiomeReservationMirrorAxis::X
		? FVector2D(MirrorDistance, 0.0)
		: FVector2D(0.0, MirrorDistance);
	const FVector2D SideACenter = MirrorOriginXY - MirrorOffset;
	const FVector2D SideBCenter = MirrorOriginXY + MirrorOffset;

	if (OwnershipRole == EBiomeReservationOwnershipRole::SideA)
	{
		return BuildRoundReservationXY(Editor, SideACenter, Radius);
	}

	if (OwnershipRole == EBiomeReservationOwnershipRole::SideB)
	{
		return BuildRoundReservationXY(Editor, SideBCenter, Radius);
	}

	return UnionReservations(
		Editor,
		{
			BuildRoundReservationXY(Editor, SideACenter, Radius),
			BuildRoundReservationXY(Editor, SideBCenter, Radius)
		});
}

FNodeLink UReservationFastNoiseLibrary::BuildUniformCubeReservation(
	UFastNoiseEditor* Editor,
	const FVector Center,
	const float HalfExtent)
{
	if (Editor == nullptr || HalfExtent <= 0.0f)
	{
		return MakeZero(Editor);
	}

	FNodeLink ExtentNode = Editor->Constant(HalfExtent);
	FNodeLink DistanceNode = Editor->DistanceToPoint(FastNoiseDistanceFunction::MaxAxis, Center);
	return Editor->Subtract(ExtentNode, DistanceNode);
}

FNodeLink UReservationFastNoiseLibrary::BuildRepeatedCellReservationXY(
	UFastNoiseEditor* Editor,
	const float CellScale,
	const float PocketRadius,
	const int32 SeedOffset)
{
	return BuildRepeatedCellReservationXYWithDistance(
		Editor,
		CellScale,
		PocketRadius,
		SeedOffset,
		FastNoiseDistanceFunction::Euclidean);
}

FNodeLink UReservationFastNoiseLibrary::BuildRepeatedCellReservationXYWithDistance(
	UFastNoiseEditor* Editor,
	const float CellScale,
	const float PocketRadius,
	const int32 SeedOffset,
	const FastNoiseDistanceFunction DistanceFunction)
{
	if (Editor == nullptr || CellScale <= 0.0f || PocketRadius <= 0.0f)
	{
		return MakeZero(Editor);
	}

	FNodeLink RadiusNode = Editor->Constant(PocketRadius);
	FNodeLink CellDistanceNode = Editor->CellularDistance(DistanceFunction);
	FNodeLink SeededCellDistanceNode = Editor->SeedOffset(CellDistanceNode, SeedOffset);
	FNodeLink PlanarCellDistanceNode = Editor->DomainAxisScale(
		SeededCellDistanceNode,
		FVector(CellScale, CellScale, 0.0));
	return Editor->Subtract(RadiusNode, PlanarCellDistanceNode);
}

FNodeLink UReservationFastNoiseLibrary::UnionReservations(
	UFastNoiseEditor* Editor,
	const TArray<FNodeLink>& Reservations)
{
	if (Editor == nullptr)
	{
		return FNodeLink();
	}

	FNodeLink Result;
	for (const FNodeLink& Reservation : Reservations)
	{
		if (!IsReservationNoiseLinkValid(Reservation))
		{
			continue;
		}

		if (!IsReservationNoiseLinkValid(Result))
		{
			Result = Reservation;
			continue;
		}

		FNodeLink ResultCopy = Result;
		FNodeLink ReservationCopy = Reservation;
		Result = Editor->Max(ResultCopy, ReservationCopy);
	}

	return IsReservationNoiseLinkValid(Result) ? Result : MakeZero(Editor);
}

FNodeLink UReservationFastNoiseLibrary::IntersectReservations(
	UFastNoiseEditor* Editor,
	FNodeLink A,
	FNodeLink B)
{
	if (Editor == nullptr)
	{
		return FNodeLink();
	}

	if (!IsReservationNoiseLinkValid(A))
	{
		return B;
	}

	if (!IsReservationNoiseLinkValid(B))
	{
		return A;
	}

	return Editor->Min(A, B);
}

FNodeLink UReservationFastNoiseLibrary::IntersectReservationsSmooth(
	UFastNoiseEditor* Editor,
	FNodeLink A,
	FNodeLink B,
	const float Smoothness)
{
	if (Editor == nullptr)
	{
		return FNodeLink();
	}

	if (!IsReservationNoiseLinkValid(A))
	{
		return B;
	}

	if (!IsReservationNoiseLinkValid(B))
	{
		return A;
	}

	return Editor->MinSmoothFloat(A, B, FMath::Max(0.0f, Smoothness));
}

FNodeLink UReservationFastNoiseLibrary::CarveReservationFromBase(
	UFastNoiseEditor* Editor,
	FNodeLink Base,
	FNodeLink Reservation)
{
	if (Editor == nullptr)
	{
		return FNodeLink();
	}

	if (!IsReservationNoiseLinkValid(Base))
	{
		return MakeZero(Editor);
	}

	if (!IsReservationNoiseLinkValid(Reservation))
	{
		return Base;
	}

	FNodeLink NegativeReservation = Editor->MultiplyFloat(Reservation, -1.0f);
	return Editor->Min(Base, NegativeReservation);
}
