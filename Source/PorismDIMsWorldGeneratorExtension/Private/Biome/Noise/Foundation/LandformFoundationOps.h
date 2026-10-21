// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "Biome/Noise/Strategy/BiomeStrategyData.h"

namespace LandformFoundation
{
	/** Returns the exact reflected payload type, or null for an existing non-landform provider family. */
	UScriptStruct* PayloadType(EFoundationProviderType Type);

	/** Validates dimensions, finite inputs, matching payload and supported authoring contracts. */
	FString Validate(const FFoundationProviderDefinition& Provider);

	/** Conservative analytic bounds include profile height and worst-case wall/rim variation. */
	FBox Bounds(const FFoundationProviderDefinition& Provider);

	/** Applies the existing Multi-Instance authored-block transform; the caller scales the shared terrain profile. */
	void Transform(FFoundationProviderDefinition& Provider, const FVector& OffsetBlocks, float UniformScale);

	/** Positive-inside ownership or negative-inside terrain in noise-distance units. Caller applies terrain gain/bias. */
	FNodeLink Build(UFastNoiseEditor* Editor, const FFoundationProviderDefinition& Provider,
		const FResolvedWorldGenScaleContext& ScaleContext, bool bDomain);

	/** Radius used by radial terrain profiles when no explicit radius override is authored. */
	float ProfileRadius(const FFoundationProviderDefinition& Provider);
}
