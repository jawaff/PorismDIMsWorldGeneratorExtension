// Copyright 2026 Spotted Loaf Studio

#include "Layout/Planning/LayoutReservationPocketPlanning.h"

namespace
{
	const TArray<FIntPoint>& GetPocketNeighborOffsets()
	{
		static const TArray<FIntPoint> CardinalOffsets = {
			FIntPoint(1, 0),
			FIntPoint(-1, 0),
			FIntPoint(0, 1),
			FIntPoint(0, -1)
		};
		return CardinalOffsets;
	}

	bool IsPocketBoundarySample(
		const FIntPoint& GridXY,
		const TSet<FIntPoint>& PocketGrid,
		const TArray<FIntPoint>& NeighborOffsets)
	{
		for (const FIntPoint& Offset : NeighborOffsets)
		{
			if (!PocketGrid.Contains(GridXY + Offset))
			{
				return true;
			}
		}

		return false;
	}

	FIntPoint ComputeApproximateInteriorBlockXY(
		const TArray<int32>& PocketSampleIndices,
		const TArray<FLayoutReservationPocketSample>& Samples,
		const TArray<FIntPoint>& NeighborOffsets)
	{
		TSet<FIntPoint> PocketGrid;
		TMap<FIntPoint, int32> SampleIndexByGrid;
		PocketGrid.Reserve(PocketSampleIndices.Num());
		SampleIndexByGrid.Reserve(PocketSampleIndices.Num());

		for (const int32 SampleIndex : PocketSampleIndices)
		{
			const FIntPoint GridXY = Samples[SampleIndex].SampleGridXY;
			PocketGrid.Add(GridXY);
			SampleIndexByGrid.Add(GridXY, SampleIndex);
		}

		TArray<FIntPoint> Queue;
		TMap<FIntPoint, int32> DistanceFromBoundaryByGrid;
		Queue.Reserve(PocketSampleIndices.Num());
		DistanceFromBoundaryByGrid.Reserve(PocketSampleIndices.Num());

		for (const int32 SampleIndex : PocketSampleIndices)
		{
			const FIntPoint GridXY = Samples[SampleIndex].SampleGridXY;
			if (IsPocketBoundarySample(GridXY, PocketGrid, NeighborOffsets))
			{
				DistanceFromBoundaryByGrid.Add(GridXY, 0);
				Queue.Add(GridXY);
			}
		}

		int32 QueueReadIndex = 0;
		while (QueueReadIndex < Queue.Num())
		{
			const FIntPoint Current = Queue[QueueReadIndex++];
			const int32 CurrentDistance = DistanceFromBoundaryByGrid.FindChecked(Current);
			for (const FIntPoint& Offset : NeighborOffsets)
			{
				const FIntPoint Neighbor = Current + Offset;
				if (!PocketGrid.Contains(Neighbor) || DistanceFromBoundaryByGrid.Contains(Neighbor))
				{
					continue;
				}

				DistanceFromBoundaryByGrid.Add(Neighbor, CurrentDistance + 1);
				Queue.Add(Neighbor);
			}
		}

		int32 BestSampleIndex = PocketSampleIndices[0];
		int32 BestDistance = MIN_int32;
		for (const int32 SampleIndex : PocketSampleIndices)
		{
			const FIntPoint GridXY = Samples[SampleIndex].SampleGridXY;
			const int32 Distance = DistanceFromBoundaryByGrid.FindRef(GridXY);
			const FIntPoint BlockXY = Samples[SampleIndex].BlockXY;
			const FIntPoint BestBlockXY = Samples[BestSampleIndex].BlockXY;
			const bool bTieBreaksEarlier = Distance == BestDistance
				&& (BlockXY.X < BestBlockXY.X || (BlockXY.X == BestBlockXY.X && BlockXY.Y < BestBlockXY.Y));

			if (Distance > BestDistance || bTieBreaksEarlier)
			{
				BestDistance = Distance;
				BestSampleIndex = SampleIndex;
			}
		}

		return Samples[BestSampleIndex].BlockXY;
	}

	FIntPoint ComputeNearestCentroidSampleBlockXY(
		const TArray<int32>& PocketSampleIndices,
		const TArray<FLayoutReservationPocketSample>& Samples,
		const FVector2D& CentroidBlockXY)
	{
		int32 BestSampleIndex = PocketSampleIndices[0];
		double BestDistanceSquared = MAX_dbl;
		for (const int32 SampleIndex : PocketSampleIndices)
		{
			const FVector2D SampleBlockXY(
				Samples[SampleIndex].BlockXY.X,
				Samples[SampleIndex].BlockXY.Y);
			const double DistanceSquared = FVector2D::DistSquared(SampleBlockXY, CentroidBlockXY);
			const FIntPoint BlockXY = Samples[SampleIndex].BlockXY;
			const FIntPoint BestBlockXY = Samples[BestSampleIndex].BlockXY;
			const bool bTieBreaksEarlier = FMath::IsNearlyEqual(DistanceSquared, BestDistanceSquared)
				&& (BlockXY.X < BestBlockXY.X || (BlockXY.X == BestBlockXY.X && BlockXY.Y < BestBlockXY.Y));
			if (DistanceSquared < BestDistanceSquared || bTieBreaksEarlier)
			{
				BestDistanceSquared = DistanceSquared;
				BestSampleIndex = SampleIndex;
			}
		}

		return Samples[BestSampleIndex].BlockXY;
	}

	FLayoutReservationPocket BuildPocket(
		const TArray<int32>& PocketSampleIndices,
		const TArray<FLayoutReservationPocketSample>& Samples,
		const TArray<FIntPoint>& NeighborOffsets)
	{
		FLayoutReservationPocket Pocket;
		Pocket.SampleCount = PocketSampleIndices.Num();

		FVector2D CentroidSum = FVector2D::ZeroVector;
		for (int32 LocalIndex = 0; LocalIndex < PocketSampleIndices.Num(); ++LocalIndex)
		{
			const FLayoutReservationPocketSample& Sample = Samples[PocketSampleIndices[LocalIndex]];
			if (LocalIndex == 0)
			{
				Pocket.MinSampleGridXY = Sample.SampleGridXY;
				Pocket.MaxSampleGridXY = Sample.SampleGridXY;
				Pocket.MinBlockXY = Sample.BlockXY;
				Pocket.MaxBlockXY = Sample.BlockXY;
			}
			else
			{
				Pocket.MinSampleGridXY.X = FMath::Min(Pocket.MinSampleGridXY.X, Sample.SampleGridXY.X);
				Pocket.MinSampleGridXY.Y = FMath::Min(Pocket.MinSampleGridXY.Y, Sample.SampleGridXY.Y);
				Pocket.MaxSampleGridXY.X = FMath::Max(Pocket.MaxSampleGridXY.X, Sample.SampleGridXY.X);
				Pocket.MaxSampleGridXY.Y = FMath::Max(Pocket.MaxSampleGridXY.Y, Sample.SampleGridXY.Y);
				Pocket.MinBlockXY.X = FMath::Min(Pocket.MinBlockXY.X, Sample.BlockXY.X);
				Pocket.MinBlockXY.Y = FMath::Min(Pocket.MinBlockXY.Y, Sample.BlockXY.Y);
				Pocket.MaxBlockXY.X = FMath::Max(Pocket.MaxBlockXY.X, Sample.BlockXY.X);
				Pocket.MaxBlockXY.Y = FMath::Max(Pocket.MaxBlockXY.Y, Sample.BlockXY.Y);
			}

			CentroidSum += FVector2D(Sample.BlockXY.X, Sample.BlockXY.Y);
		}

		Pocket.CentroidBlockXY = CentroidSum / static_cast<double>(FMath::Max(1, Pocket.SampleCount));
		Pocket.SampleBlockXYs.Reserve(PocketSampleIndices.Num());
		for (const int32 SampleIndex : PocketSampleIndices)
		{
			Pocket.SampleBlockXYs.Add(Samples[SampleIndex].BlockXY);
		}
		Pocket.SampleBlockXYs.Sort([](const FIntPoint& Left, const FIntPoint& Right)
		{
			return Left.X != Right.X
				? Left.X < Right.X
				: Left.Y < Right.Y;
		});
		Pocket.CentroidNearestSampleBlockXY =
			ComputeNearestCentroidSampleBlockXY(
				PocketSampleIndices,
				Samples,
				Pocket.CentroidBlockXY);
		Pocket.ApproximateInteriorBlockXY = ComputeApproximateInteriorBlockXY(PocketSampleIndices, Samples, NeighborOffsets);
		return Pocket;
	}
	TArray<FLayoutReservationPocket> FindConnectedReservationPocketsInternal(
		const TArray<FLayoutReservationPocketSample>& Samples,
		const FLayoutReservationPocketPlanningSettings& Settings,
		const TSet<FIntPoint>& ExcludedSampleGridXY)
	{
		TArray<FLayoutReservationPocket> Pockets;
		if (Samples.IsEmpty())
		{
			return Pockets;
		}

		const int32 MinimumSampleCount = FMath::Max(1, Settings.MinimumSampleCount);
		const int32 MaximumSampleCount = FMath::Max(0, Settings.MaximumSampleCount);
		const TArray<FIntPoint>& NeighborOffsets = GetPocketNeighborOffsets();

		TMap<FIntPoint, int32> EligibleSampleIndexByGrid;
		EligibleSampleIndexByGrid.Reserve(Samples.Num());
		for (int32 SampleIndex = 0; SampleIndex < Samples.Num(); ++SampleIndex)
		{
			const FIntPoint SampleGridXY = Samples[SampleIndex].SampleGridXY;
			if (Samples[SampleIndex].NoiseValue >= Settings.EligibilityThreshold
				&& !ExcludedSampleGridXY.Contains(SampleGridXY))
			{
				EligibleSampleIndexByGrid.Add(SampleGridXY, SampleIndex);
			}
		}

		TSet<FIntPoint> Visited;
		Visited.Reserve(EligibleSampleIndexByGrid.Num());

		for (const TPair<FIntPoint, int32>& EligibleSample : EligibleSampleIndexByGrid)
		{
			if (Visited.Contains(EligibleSample.Key))
			{
				continue;
			}

			TArray<FIntPoint> Queue;
			TArray<int32> PocketSampleIndices;
			Queue.Add(EligibleSample.Key);
			Visited.Add(EligibleSample.Key);

			int32 QueueReadIndex = 0;
			while (QueueReadIndex < Queue.Num())
			{
				const FIntPoint Current = Queue[QueueReadIndex++];
				PocketSampleIndices.Add(EligibleSampleIndexByGrid.FindChecked(Current));

				for (const FIntPoint& Offset : NeighborOffsets)
				{
					const FIntPoint Neighbor = Current + Offset;
					if (!EligibleSampleIndexByGrid.Contains(Neighbor) || Visited.Contains(Neighbor))
					{
						continue;
					}

					Visited.Add(Neighbor);
					Queue.Add(Neighbor);
				}
			}

			const bool bPassesMinimumSize = PocketSampleIndices.Num() >= MinimumSampleCount;
			const bool bPassesMaximumSize = MaximumSampleCount <= 0 || PocketSampleIndices.Num() <= MaximumSampleCount;
			if (bPassesMinimumSize && bPassesMaximumSize)
			{
				Pockets.Add(BuildPocket(PocketSampleIndices, Samples, NeighborOffsets));
			}
		}

		Pockets.Sort([](const FLayoutReservationPocket& A, const FLayoutReservationPocket& B)
		{
			if (A.MinBlockXY.X != B.MinBlockXY.X)
			{
				return A.MinBlockXY.X < B.MinBlockXY.X;
			}

			return A.MinBlockXY.Y < B.MinBlockXY.Y;
		});

		return Pockets;
	}
}

TArray<FLayoutReservationPocket> ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(
	const TArray<FLayoutReservationPocketSample>& Samples,
	const FLayoutReservationPocketPlanningSettings& Settings)
{
	return FindConnectedReservationPocketsInternal(Samples, Settings, {});
}

TArray<FLayoutReservationPocket> ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPocketsWithExclusions(
	const TArray<FLayoutReservationPocketSample>& Samples,
	const TArray<FLayoutReservationPocketSample>& ExclusionSamples,
	const FLayoutReservationPocketPlanningSettings& Settings,
	const float ExclusionThreshold)
{
	TSet<FIntPoint> ExcludedSampleGridXY;
	ExcludedSampleGridXY.Reserve(ExclusionSamples.Num());
	for (const FLayoutReservationPocketSample& ExclusionSample : ExclusionSamples)
	{
		if (ExclusionSample.NoiseValue >= ExclusionThreshold)
		{
			ExcludedSampleGridXY.Add(ExclusionSample.SampleGridXY);
		}
	}

	return FindConnectedReservationPocketsInternal(Samples, Settings, ExcludedSampleGridXY);
}
