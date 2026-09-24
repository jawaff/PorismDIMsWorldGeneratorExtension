// Copyright 2026 Spotted Loaf Studio
#pragma once

#include "CoreMinimal.h"

/** Game-thread finest-N-LOD envelopes shared by readiness consumers and ongoing movement priority.
 * Bounds are spatial hints, never obligations for hypothetical native chunk events. */
struct FLayoutStartupCoverage
{
	struct FTarget
	{
		FIntVector Position, RootCenter, RootLimits;
		TArray<double> RefinementRanges;
		bool operator==(const FTarget& Other) const
		{
			return Position == Other.Position && RootCenter == Other.RootCenter && RootLimits == Other.RootLimits
				&& RefinementRanges == Other.RefinementRanges;
		}
	};
	TMap<TWeakObjectPtr<UObject>, TArray<TWeakObjectPtr<UObject>>> Consumers;
	TArray<TWeakObjectPtr<UObject>> Walkers;
	TArray<FTarget> Targets;
	TArray<FIntVector> LayerShapes;
	FTransform Transform = FTransform::Identity;
	FVector LayoutReach = FVector::ZeroVector;
	double BlockSize = 0;
	int32 FirstRequiredDetailLevel = INDEX_NONE;
	/** Parallel to Walkers/Targets; invalid boxes retain indexing for walkers with no required footprint. */
	TArray<FBox> BoundsInBlocks;
	uint64 Revision = 0;
	double NextDiagnosticTime = 0;
};
