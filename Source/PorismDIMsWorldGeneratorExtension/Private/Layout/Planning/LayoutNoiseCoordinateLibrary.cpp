// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"

namespace
{
	int32 GetNoiseCoordinateOffsetInBaseBlocks(const int32 OffsetInUnrealUnits, const int32 BaseBlockSize)
	{
		return OffsetInUnrealUnits / FMath::Max(1, BaseBlockSize);
	}

	double ConvertAxisToNoiseCoordinate(
		const int32 BlockWorldCoordinate,
		const int32 OffsetInUnrealUnits,
		const int32 BaseBlockSize,
		const double NoiseScale)
	{
		const int32 SafeBaseBlockSize = FMath::Max(1, BaseBlockSize);
		const int32 OffsetInBaseBlocks = GetNoiseCoordinateOffsetInBaseBlocks(OffsetInUnrealUnits, SafeBaseBlockSize);
		return (static_cast<double>(BlockWorldCoordinate) + OffsetInBaseBlocks)
			* 0.0001
			* static_cast<double>(SafeBaseBlockSize)
			* NoiseScale;
	}
}

FVector ULayoutNoiseCoordinateLibrary::BlockWorldPositionToNoiseCoordinate(
	const FIntVector BlockWorldPosition,
	const FLayoutNoiseCoordinateSettings& Settings)
{
	return FVector(
		ConvertAxisToNoiseCoordinate(
			BlockWorldPosition.X,
			Settings.NoiseCoordinateOffset.X,
			Settings.BaseBlockSize,
			Settings.NoiseScale.X),
		ConvertAxisToNoiseCoordinate(
			BlockWorldPosition.Y,
			Settings.NoiseCoordinateOffset.Y,
			Settings.BaseBlockSize,
			Settings.NoiseScale.Y),
		ConvertAxisToNoiseCoordinate(
			BlockWorldPosition.Z,
			Settings.NoiseCoordinateOffset.Z,
			Settings.BaseBlockSize,
			Settings.NoiseScale.Z));
}

FVector2D ULayoutNoiseCoordinateLibrary::BlockWorldXYToNoiseCoordinate(
	const FIntPoint BlockWorldXY,
	const FLayoutNoiseCoordinateSettings& Settings)
{
	const FVector NoiseCoordinate = BlockWorldPositionToNoiseCoordinate(
		FIntVector(BlockWorldXY.X, BlockWorldXY.Y, 0),
		Settings);
	return FVector2D(NoiseCoordinate.X, NoiseCoordinate.Y);
}
