// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Types/LayoutTypes.h"

class AChunkWorldCore;
class FLayoutActiveBiomeSampler;
struct FLayoutNoiseCoordinateSettings;

/**
 * Plain-data terrain-fit result used by runtime realization after world-facing
 * terrain sampling has chosen one anchor for the current placement footprint.
 */
struct FLayoutWorldBindingTerrainFitResult
{
	/** True when the current placement may continue at the resolved anchor. */
	bool bIsValid = false;

	/** Human-readable failure reason when the terrain fit is rejected. */
	FString FailureReason;

	/** Structured world-binding terrain-fit outcome preserved alongside FailureReason. */
	ELayoutWorldBindingTerrainFitDiagnosticKind DiagnosticKind =
		ELayoutWorldBindingTerrainFitDiagnosticKind::None;

	/** Resolved anchor used for stamping the current placement or footprint. */
	FIntVector AnchorBlockWorldPos = FIntVector::ZeroValue;

	/** Exact top-surface plane that later world-facing terrain shaping should meet when alignment data is available. */
	int32 ResolvedTerrainAlignmentTopSurfaceZ = INDEX_NONE;

	/** Terrain-shape and transition hints derived from the sampled footprint at the resolved anchor. */
	FLayoutTerrainClassificationResult TerrainClassification;

	/** Outer-perimeter transition hints sampled immediately around the resolved footprint edge. */
	FLayoutTerrainPerimeterTransitionResult PerimeterTransitionAnalysis;

	/** Optional foundation-fill positions that should be written before stamping the solved placement. */
	TArray<FIntVector> FoundationFillPositions;

	/** Material payload that matches FoundationFillPositions one-for-one. */
	TArray<int32> FoundationFillMaterials;

	/** Optional perimeter-ramp or terrace block positions that should be written before stamping the solved placement. */
	TArray<FIntVector> PerimeterTransitionPositions;

	/** Material payload that matches PerimeterTransitionPositions one-for-one. */
	TArray<int32> PerimeterTransitionMaterials;
};

