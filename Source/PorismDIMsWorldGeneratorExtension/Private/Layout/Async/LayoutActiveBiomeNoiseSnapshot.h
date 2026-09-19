// Copyright 2026 Spotted Loaf Studio

#pragma once

#include "CoreMinimal.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"

class UWorldGenDef;

/** Owns the existing biome utility compiled on the game thread, then shared read-only by workers. */
struct PORISMDIMSWORLDGENERATOREXTENSION_API FLayoutActiveBiomeNoiseSnapshot
{
	int32 WorldSeed = 0;
	TArray<FName> CapturedBiomeRows;

	/** Captures all configured noise and row data before dispatch; workers never read source UObjects. */
	static TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> CaptureFromWorldDefinition(
		UObject* Creator, const UWorldGenDef* WorldGenDef, int32 InWorldSeed, FString& OutFailureReason);

	bool IsInitialized() const { return Sampler.IsInitialized(); }

	/** Exposes the same immutable surface, ownership and column utility used by synchronous callers. */
	const FLayoutActiveBiomeSampler& GetSampler() const { return Sampler; }

	/** Samples captured nodes without consulting the world, assets, saves or caches. */
	bool SampleAtBlockPosition(FIntVector BlockWorldPosition,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings, FLayoutActiveBiomeSample& OutSample) const;

private:
	FLayoutActiveBiomeSampler Sampler;
};
