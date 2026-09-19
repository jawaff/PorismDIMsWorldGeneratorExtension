// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "Layout/Types/LayoutId.h"
#include "CoreMinimal.h"

/** Compact inclusive XY exclusion retained independently of heavy root solve payloads. */
struct FLayoutRootSpacingReservation
{
	FName BindingId;
	FIntPoint Min = FIntPoint::ZeroValue;
	FIntPoint Max = FIntPoint::ZeroValue;

	/** Builds a centered authored footprint without overflowing block coordinates. */
	static bool TryBuild(FName Binding, FIntVector Center, FIntPoint Footprint, FIntVector CellSize,
		FLayoutRootSpacingReservation& Out)
	{
		if (Footprint.X <= 0 || Footprint.Y <= 0 || CellSize.X <= 0 || CellSize.Y <= 0) return false;
		const int64 Width = int64(Footprint.X) * CellSize.X, Height = int64(Footprint.Y) * CellSize.Y;
		const int64 X = int64(Center.X) - Width / 2, Y = int64(Center.Y) - Height / 2;
		if (X < MIN_int32 || Y < MIN_int32 || X + Width - 1 > MAX_int32 || Y + Height - 1 > MAX_int32) return false;
		Out.BindingId = Binding;
		Out.Min = FIntPoint(int32(X), int32(Y));
		Out.Max = FIntPoint(int32(X + Width - 1), int32(Y + Height - 1));
		return true;
	}

	/** Rectangular XY clearance in the binding's normal cells. Zero still rejects overlapping footprints. */
	bool IsSeparatedFrom(const FLayoutRootSpacingReservation& Other, int32 GapCells, FIntVector CellSize) const
	{
		if (BindingId != Other.BindingId) return true;
		const int64 GapX = int64(FMath::Max(0, GapCells)) * FMath::Max(1, CellSize.X);
		const int64 GapY = int64(FMath::Max(0, GapCells)) * FMath::Max(1, CellSize.Y);
		return int64(Min.X) - Other.Max.X - 1 >= GapX || int64(Other.Min.X) - Max.X - 1 >= GapX
			|| int64(Min.Y) - Other.Max.Y - 1 >= GapY || int64(Other.Min.Y) - Max.Y - 1 >= GapY;
	}

	/** Conservative whole-region proof using one retained root, never merely an intersecting exclusion.
	 * For fixed footprint/gap the rejected center set is rectangular, so all four corners suffice.
	 * Invalid bounds or another binding are inconclusive and must continue through normal discovery. */
	bool ExcludesCenterRegion(FName Binding, FIntPoint CenterMin, FIntPoint CenterMax,
		FIntPoint Footprint, FIntVector CellSize, int32 GapCells) const
	{
		if (Binding != BindingId || CenterMin.X > CenterMax.X || CenterMin.Y > CenterMax.Y || GapCells < 0) return false;
		for (const FIntPoint CenterCorner : {CenterMin, FIntPoint(CenterMax.X, CenterMin.Y),
			FIntPoint(CenterMin.X, CenterMax.Y), CenterMax})
		{
			FLayoutRootSpacingReservation Candidate;
			if (!TryBuild(Binding, FIntVector(CenterCorner.X, CenterCorner.Y, 0), Footprint, CellSize, Candidate)
				|| Candidate.IsSeparatedFrom(*this, GapCells, CellSize)) return false;
		}
		return true;
	}

	/** Keeps an exclusion while its configured clearance can touch any candidate-coverage window. */
	bool IntersectsCoverage(const TArray<FIntPoint>& Mins, const TArray<FIntPoint>& Maxs, int64 MarginX, int64 MarginY) const
	{
		if (Mins.Num() != Maxs.Num()) return true;
		for (int32 Index = 0; Index < Mins.Num(); ++Index)
		{
			if (int64(Min.X) - MarginX <= Maxs[Index].X && int64(Max.X) + MarginX >= Mins[Index].X
				&& int64(Min.Y) - MarginY <= Maxs[Index].Y && int64(Max.Y) + MarginY >= Mins[Index].Y) return true;
		}
		return false;
	}
};
