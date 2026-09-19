// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/WorldGenScaleContext.h"

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Biome/Noise/Strategy/BiomeStrategyData.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"

namespace
{
	AChunkWorldExtended* SpawnScaleTestWorld(
		const int32 BaseBlockSize,
		const FVector& NoiseScale,
		const FIntVector& ChunkBlockSize,
		const EAxisBehavior AxisBehaviorX = EAxisBehavior::Infinity,
		const EAxisBehavior AxisBehaviorY = EAxisBehavior::Infinity,
		const EAxisBehavior AxisBehaviorZ = EAxisBehavior::SingleChunk)
	{
		UWorld* const EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
		check(EditorWorld != nullptr);

		AChunkWorldExtended* const ChunkWorld = EditorWorld->SpawnActor<AChunkWorldExtended>();
		ChunkWorld->WorldGenDef = NewObject<UWorldGenDef>(ChunkWorld);
		ChunkWorld->WorldGenDefPredefined = true;
		ChunkWorld->WorldGenDef->BaseBlockSize = BaseBlockSize;
		ChunkWorld->WorldGenDef->NoiseScale = NoiseScale;
		ChunkWorld->WorldGenDef->ChunkBlockSize = ChunkBlockSize;
		ChunkWorld->WorldGenDef->AxisBehaviorX = AxisBehaviorX;
		ChunkWorld->WorldGenDef->AxisBehaviorY = AxisBehaviorY;
		ChunkWorld->WorldGenDef->AxisBehaviorZ = AxisBehaviorZ;

		FChunkDataParams ChunkParams;
		ChunkParams.BlockSizeMulti = 1.0;
		ChunkParams.ChunkSizeMulti = FVector(1.0, 1.0, 1.0);
		ChunkWorld->WorldGenDef->WorldChunks.Add(ChunkParams);
		return ChunkWorld;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldGenScaleContextConvertsAuthoredBlocksTest,
	"PorismExtension.Biome.Noise.ScaleContext.ConvertsAuthoredBlocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenScaleContextConvertsAuthoredBlocksTest::RunTest(const FString& Parameters)
{
	FWorldGenScaleContextResolver::ClearCache();

	FWorldGenScaleSettings Settings;
	Settings.BaseBlockSize = 100;
	Settings.NoiseScale = FVector(1.0, 1.0, 1.0);
	Settings.NoiseCoordinateOffset = FIntVector::ZeroValue;
	Settings.AxisBehaviorX = EAxisBehavior::Infinity;
	Settings.AxisBehaviorY = EAxisBehavior::Infinity;
	Settings.AxisBehaviorZ = EAxisBehavior::SingleChunk;
	Settings.FallbackFiniteAxisBlockSpan = FIntVector(256, 256, 256);

	const FResolvedWorldGenScaleContext Context = FWorldGenScaleContextResolver::Resolve(nullptr, &Settings, true);

	TestTrue(TEXT("Explicit settings are recorded as an override"), Context.bResolvedFromOverride);
	TestFalse(TEXT("Explicit settings do not use fallback defaults"), Context.bUsingFallbackDefaults);
	TestEqual(TEXT("One block converts to Porism base noise distance"), static_cast<float>(Context.BlocksToNoiseScale.X), 0.01f);
	TestEqual(TEXT("Finite Z authored origin is centered in the single chunk"), static_cast<float>(Context.AuthoredOriginToRawBlockOffset.Z), 128.0f);
	TestEqual(TEXT("Infinite X authored origin stays on world origin"), static_cast<float>(Context.AuthoredOriginToRawBlockOffset.X), 0.0f);

	const FVector NoiseOrigin = Context.AuthoredBlockPositionToNoise(FVector::ZeroVector);
	TestTrue(TEXT("Authored block origin maps to the centered Z noise coordinate"), NoiseOrigin.Equals(FVector(0.0, 0.0, 1.28), KINDA_SMALL_NUMBER));

	const FVector NoiseOffset = Context.AuthoredBlockPositionToNoise(FVector(100.0, -50.0, 25.0));
	TestTrue(TEXT("Authored block position converts through per-axis block scale"), NoiseOffset.Equals(FVector(1.0, -0.5, 1.53), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Noise position converts back to authored blocks"), Context.NoisePositionToAuthoredBlock(NoiseOffset).Equals(FVector(100.0, -50.0, 25.0), KINDA_SMALL_NUMBER));
	TestEqual(TEXT("Noise Z converts back to authored block Z"), Context.NoiseZToAuthoredBlockZ(1.28f), 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldGenScaleContextAppliesPorismNoiseOffsetTest,
	"PorismExtension.Biome.Noise.ScaleContext.AppliesPorismNoiseOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenScaleContextAppliesPorismNoiseOffsetTest::RunTest(const FString& Parameters)
{
	FWorldGenScaleContextResolver::ClearCache();

	FWorldGenScaleSettings Settings;
	Settings.BaseBlockSize = 100;
	Settings.NoiseScale = FVector(2.0, 1.0, 0.5);
	Settings.NoiseCoordinateOffset = FIntVector(200, -100, 50);
	Settings.AxisBehaviorX = EAxisBehavior::Infinity;
	Settings.AxisBehaviorY = EAxisBehavior::Infinity;
	Settings.AxisBehaviorZ = EAxisBehavior::Infinity;

	const FResolvedWorldGenScaleContext Context = FWorldGenScaleContextResolver::Resolve(nullptr, &Settings, true);

	TestTrue(TEXT("Noise coordinate offset is converted to raw block units"), Context.NoiseCoordinateOffsetBlocks.Equals(FVector(2.0, -1.0, 0.5), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Porism noise offset participates in authored block to noise conversion"), Context.AuthoredBlockPositionToNoise(FVector::ZeroVector).Equals(FVector(0.04, -0.01, 0.0025), KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Distance conversion ignores global coordinate offsets"), Context.BlockDistanceToNoise(FVector(50.0, 50.0, 50.0)).Equals(FVector(1.0, 0.5, 0.25), KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldGenScaleContextFiniteAxisPreservesInSpanNoiseOriginTest,
	"PorismExtension.Biome.Noise.ScaleContext.FiniteAxisPreservesInSpanNoiseOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenScaleContextFiniteAxisPreservesInSpanNoiseOriginTest::RunTest(const FString& Parameters)
{
	FWorldGenScaleContextResolver::ClearCache();

	FWorldGenScaleSettings Settings;
	Settings.BaseBlockSize = 100;
	Settings.NoiseScale = FVector(1.0, 1.0, 1.0);
	Settings.NoiseCoordinateOffset = FIntVector(0, 0, -20000);
	Settings.AxisBehaviorX = EAxisBehavior::Infinity;
	Settings.AxisBehaviorY = EAxisBehavior::Infinity;
	Settings.AxisBehaviorZ = EAxisBehavior::SingleChunk;
	Settings.FallbackFiniteAxisBlockSpan = FIntVector(16, 16, 256);

	const FResolvedWorldGenScaleContext Context = FWorldGenScaleContextResolver::Resolve(nullptr, &Settings, true);

	TestEqual(TEXT("Noise offset is converted to raw block coordinates"), static_cast<float>(Context.NoiseCoordinateOffsetBlocks.Z), -200.0f);
	TestEqual(TEXT("Finite authored Z origin preserves Porism noise origin when it is inside the chunk span"), static_cast<float>(Context.AuthoredOriginToRawBlockOffset.Z), 200.0f);
	TestEqual(TEXT("Finite authored min reflects the bottom raw block"), static_cast<float>(Context.AuthoredMinBlock.Z), -200.0f);
	TestEqual(TEXT("Finite authored max reflects the top raw block"), static_cast<float>(Context.AuthoredMaxBlock.Z), 56.0f);

	const FVector NoiseOrigin = Context.AuthoredBlockPositionToNoise(FVector::ZeroVector);
	TestTrue(TEXT("Authored zero maps to Porism noise coordinate zero on the offset finite axis"), NoiseOrigin.Equals(FVector::ZeroVector, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Noise position converts back to authored zero"), Context.NoisePositionToAuthoredBlock(NoiseOrigin).Equals(FVector::ZeroVector, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldGenScaleContextUsesExactChunkWorldCreatorTest,
	"PorismExtension.Biome.Noise.ScaleContext.UsesExactChunkWorldCreator",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenScaleContextUsesExactChunkWorldCreatorTest::RunTest(const FString& Parameters)
{
	FWorldGenScaleContextResolver::ClearCache();

	AChunkWorldExtended* const FirstWorld = SpawnScaleTestWorld(100, FVector(1.0, 1.0, 1.0), FIntVector(256, 256, 256));
	AChunkWorldExtended* const SecondWorld = SpawnScaleTestWorld(50, FVector(2.0, 1.0, 1.0), FIntVector(64, 64, 128));

	const FResolvedWorldGenScaleContext FirstContext = FWorldGenScaleContextResolver::Resolve(FirstWorld, nullptr, false);
	const FResolvedWorldGenScaleContext SecondContext = FWorldGenScaleContextResolver::Resolve(SecondWorld, nullptr, false);

	TestTrue(TEXT("First context resolves from its chunk world"), FirstContext.bResolvedFromChunkWorld);
	TestTrue(TEXT("Second context resolves from its chunk world"), SecondContext.bResolvedFromChunkWorld);
	TestEqual(TEXT("First world keeps its base block size"), FirstContext.BaseBlockSize, 100);
	TestEqual(TEXT("Second world keeps its base block size"), SecondContext.BaseBlockSize, 50);
	TestEqual(TEXT("First finite Z span comes from first world chunk settings"), FirstContext.ReferenceChunkBlockSpan.Z, 256);
	TestEqual(TEXT("Second finite Z span comes from second world chunk settings"), SecondContext.ReferenceChunkBlockSpan.Z, 128);
	TestEqual(TEXT("First authored Z origin maps to first finite center"), static_cast<float>(FirstContext.AuthoredOriginToRawBlockOffset.Z), 128.0f);
	TestEqual(TEXT("Second authored Z origin maps to second finite center"), static_cast<float>(SecondContext.AuthoredOriginToRawBlockOffset.Z), 64.0f);
	TestEqual(TEXT("Second world conversion uses its own base size and noise scale"), static_cast<float>(SecondContext.BlocksToNoiseScale.X), 0.01f);

	FirstWorld->Destroy();
	SecondWorld->Destroy();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldGenScaleContextStrategyContextUsesChunkWorldSeedTest,
	"PorismExtension.Biome.Noise.ScaleContext.StrategyContextUsesChunkWorldSeed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenScaleContextStrategyContextUsesChunkWorldSeedTest::RunTest(const FString& Parameters)
{
	FWorldGenScaleContextResolver::ClearCache();

	AChunkWorldExtended* const ChunkWorld = SpawnScaleTestWorld(100, FVector(1.0, 1.0, 1.0), FIntVector(256, 256, 256));
	ChunkWorld->Seed = 2468;

	UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	Strategy->FallbackStrategySeed = 1357;

	const FResolvedStrategyContext ChunkWorldContext = Strategy->ResolveStrategyContext(ChunkWorld);
	TestTrue(TEXT("Strategy context resolves seed from chunk world"), ChunkWorldContext.bSeedResolvedFromChunkWorld);
	TestFalse(TEXT("Chunk-world seed does not use fallback"), ChunkWorldContext.bSeedResolvedFromFallback);
	TestEqual(TEXT("Strategy context keeps exact chunk-world seed"), ChunkWorldContext.StrategySeed, 2468);

	const FResolvedStrategyContext FallbackContext = Strategy->ResolveStrategyContext(GetTransientPackage());
	TestFalse(TEXT("Fallback context is not chunk-world seeded"), FallbackContext.bSeedResolvedFromChunkWorld);
	TestTrue(TEXT("Fallback context records fallback seed source"), FallbackContext.bSeedResolvedFromFallback);
	TestEqual(TEXT("Fallback context uses strategy fallback seed"), FallbackContext.StrategySeed, 1357);

	ChunkWorld->Destroy();

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FWorldGenScaleContextCacheSeparatesScaleSettingsTest,
	"PorismExtension.Biome.Noise.ScaleContext.CacheSeparatesScaleSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldGenScaleContextCacheSeparatesScaleSettingsTest::RunTest(const FString& Parameters)
{
	FWorldGenScaleContextResolver::ClearCache();

	FWorldGenScaleSettings FirstSettings;
	FirstSettings.BaseBlockSize = 100;
	FirstSettings.FallbackFiniteAxisBlockSpan = FIntVector(256, 256, 256);

	FWorldGenScaleSettings SecondSettings = FirstSettings;
	SecondSettings.BaseBlockSize = 50;
	SecondSettings.FallbackFiniteAxisBlockSpan = FIntVector(128, 128, 128);

	const FResolvedWorldGenScaleContext FirstContext = FWorldGenScaleContextResolver::Resolve(nullptr, &FirstSettings, true);
	const FResolvedWorldGenScaleContext SecondContext = FWorldGenScaleContextResolver::Resolve(nullptr, &SecondSettings, true);
	const FResolvedWorldGenScaleContext FirstContextAgain = FWorldGenScaleContextResolver::Resolve(nullptr, &FirstSettings, true);

	TestEqual(TEXT("First cached context keeps first block scale"), static_cast<float>(FirstContext.BlocksToNoiseScale.X), 0.01f);
	TestEqual(TEXT("Second cached context keeps second block scale"), static_cast<float>(SecondContext.BlocksToNoiseScale.X), 0.005f);
	TestEqual(TEXT("First context remains stable after resolving another settings key"), static_cast<float>(FirstContextAgain.BlocksToNoiseScale.X), 0.01f);
	TestEqual(TEXT("Second finite Z center uses its own settings key"), static_cast<float>(SecondContext.AuthoredOriginToRawBlockOffset.Z), 64.0f);

	return true;
}
