// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"

#include "LayoutLocalBlockCoordinates.generated.h"

/** Compact local block coordinate inside a validated layout cell axis. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutLocalBlockCoord8
{
	GENERATED_BODY()

	/** Local block coordinate in a cell axis; valid runtime ranges are guaranteed by asset validation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Coordinates", meta = (ToolTip = "Local block coordinate in a cell axis. Runtime/prewarm code assumes owning cell dimensions were asset-validated to [1,255]."))
	uint8 Value = 0;

	FLayoutLocalBlockCoord8() = default;

	FLayoutLocalBlockCoord8(const uint8 InValue)
		: Value(InValue)
	{
	}

	FLayoutLocalBlockCoord8& operator=(const uint8 InValue)
	{
		Value = InValue;
		return *this;
	}

	/** Returns the compact coordinate as an int32 for comparisons and diagnostics. */
	int32 ToInt() const
	{
		return static_cast<int32>(Value);
	}
};

/** Compact local block XYZ offset inside a validated layout cell. */
USTRUCT(BlueprintType)
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutLocalBlockOffset8
{
	GENERATED_BODY()

	/** Local X block coordinate in the cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Coordinates", meta = (ToolTip = "Local X block coordinate in a validated layout cell."))
	FLayoutLocalBlockCoord8 X;

	/** Local Y block coordinate in the cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Coordinates", meta = (ToolTip = "Local Y block coordinate in a validated layout cell."))
	FLayoutLocalBlockCoord8 Y;

	/** Local Z block coordinate in the cell. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Layout|Coordinates", meta = (ToolTip = "Local Z block coordinate in a validated layout cell."))
	FLayoutLocalBlockCoord8 Z;

	FLayoutLocalBlockOffset8() = default;

	FLayoutLocalBlockOffset8(const uint8 InX, const uint8 InY, const uint8 InZ)
		: X(InX)
		, Y(InY)
		, Z(InZ)
	{
	}
};

namespace LayoutLocalBlockCoordinates
{
	/** Attempts to compact an int coordinate after asset validation has constrained cell dimensions to [1,255]. */
	inline bool TryMakeCoord8(const int32 InValue, FLayoutLocalBlockCoord8& OutCoord)
	{
		if (InValue < 0 || InValue > TNumericLimits<uint8>::Max())
		{
			OutCoord = FLayoutLocalBlockCoord8();
			return false;
		}

		OutCoord = FLayoutLocalBlockCoord8(static_cast<uint8>(InValue));
		return true;
	}

	/** Attempts to compact an XYZ local block offset without silently truncating producer output. */
	inline bool TryMakeOffset8(const FIntVector& InValue, FLayoutLocalBlockOffset8& OutOffset)
	{
		FLayoutLocalBlockCoord8 X;
		FLayoutLocalBlockCoord8 Y;
		FLayoutLocalBlockCoord8 Z;
		if (!TryMakeCoord8(InValue.X, X)
			|| !TryMakeCoord8(InValue.Y, Y)
			|| !TryMakeCoord8(InValue.Z, Z))
		{
			OutOffset = FLayoutLocalBlockOffset8();
			return false;
		}

		OutOffset.X = X;
		OutOffset.Y = Y;
		OutOffset.Z = Z;
		return true;
	}
}
