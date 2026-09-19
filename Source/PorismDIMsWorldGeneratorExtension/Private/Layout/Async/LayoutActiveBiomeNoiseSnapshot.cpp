// Copyright 2026 Spotted Loaf Studio

#include "Layout/Async/LayoutActiveBiomeNoiseSnapshot.h"

#include "Engine/DataTable.h"

TSharedRef<const FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe> FLayoutActiveBiomeNoiseSnapshot::CaptureFromWorldDefinition(
	UObject* Creator, const UWorldGenDef* WorldGenDef, const int32 InWorldSeed, FString& OutFailureReason)
{
	check(IsInGameThread());
	OutFailureReason.Reset();
	auto Snapshot = MakeShared<FLayoutActiveBiomeNoiseSnapshot, ESPMode::ThreadSafe>();
	Snapshot->WorldSeed = InWorldSeed;
	if (WorldGenDef == nullptr)
	{
		OutFailureReason = TEXT("Active-biome noise snapshot requires a Porism world definition.");
		return Snapshot;
	}
	if (WorldGenDef->WorldBiomesDT != nullptr
		&& WorldGenDef->WorldBiomesDT->GetRowStruct() != FBiomeDualData::StaticStruct())
	{
		OutFailureReason = TEXT("Active-biome noise snapshot requires WorldBiomesDT rows of type FBiomeDualData.");
		return Snapshot;
	}
	if (!Snapshot->Sampler.Initialize(Creator, WorldGenDef, InWorldSeed))
	{
		OutFailureReason = TEXT("Active-biome noise snapshot captured no visible biome rows.");
	}
	for (const auto& Row : Snapshot->Sampler.CompiledRows)
	{
		Snapshot->CapturedBiomeRows.Add(Row.RowRef.RowName);
	}
	return Snapshot;
}

bool FLayoutActiveBiomeNoiseSnapshot::SampleAtBlockPosition(
	const FIntVector BlockWorldPosition, const FLayoutNoiseCoordinateSettings& CoordinateSettings,
	FLayoutActiveBiomeSample& OutSample) const
{
	return Sampler.SampleAtBlockPosition(BlockWorldPosition, CoordinateSettings, OutSample);
}
