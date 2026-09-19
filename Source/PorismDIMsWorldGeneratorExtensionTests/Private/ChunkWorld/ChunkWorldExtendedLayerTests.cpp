// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"

#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Materials/Material.h"
#include "Misc/AutomationTest.h"

namespace
{
	void AddWorldGridMaterial(UWorldGenDef& WorldGenDef)
	{
		WorldGenDef.WorldMaterialDefs.Add(FMaterialDef(UMaterial::GetDefaultMaterial(MD_Surface), 0, false, NAME_None, ECollisionEnabled::NoCollision));
	}

	int32 CalculateFlatAxisChunkSize(const UWorldGenDef* WorldGenDef, const FChunkDataParams& LayerParams)
	{
		const double BlockSize = LayerParams.BlockSizeMulti * static_cast<double>(WorldGenDef->BaseBlockSize);
		return FMath::RoundToInt(LayerParams.ChunkSizeMulti.Z * static_cast<double>(WorldGenDef->ChunkBlockSize.Z) * BlockSize);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldExtendedNormalizes2DFlatAxisLayerSpansTest,
	"PorismExtension.ChunkWorld.LayerNormalization.Normalizes2DFlatAxisSpans",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldExtendedNormalizes2DFlatAxisLayerSpansTest::RunTest(const FString& Parameters)
{
	AChunkWorldExtended* const ChunkWorld = NewObject<AChunkWorldExtended>(GetTransientPackage());
	UWorldGenDef* const AuthoredWorldGenDef = NewObject<UWorldGenDef>(GetTransientPackage());
	AuthoredWorldGenDef->BaseBlockSize = 100;
	AuthoredWorldGenDef->ChunkBlockSize = FIntVector(16, 16, 256);
	AuthoredWorldGenDef->AxisBehaviorX = EAxisBehavior::Infinity;
	AuthoredWorldGenDef->AxisBehaviorY = EAxisBehavior::Infinity;
	AuthoredWorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
	AddWorldGridMaterial(*AuthoredWorldGenDef);

	FChunkDataParams ParentLayer;
	ParentLayer.BlockSizeMulti = 2.0;
	ParentLayer.ChunkSizeMulti = FVector(1.0, 1.0, 1.0);

	FChunkDataParams LastLayer;
	LastLayer.BlockSizeMulti = 1.0;
	LastLayer.ChunkSizeMulti = FVector(1.0, 1.0, 1.0);

	AuthoredWorldGenDef->WorldChunks = { ParentLayer, LastLayer };
	ChunkWorld->WorldGenDef = AuthoredWorldGenDef;

	TestEqual(TEXT("Parent layer starts with the mismatched flat-axis span"), CalculateFlatAxisChunkSize(AuthoredWorldGenDef, AuthoredWorldGenDef->WorldChunks[0]), 51200);
	TestEqual(TEXT("Last layer provides the expected 256-block flat-axis span"), CalculateFlatAxisChunkSize(AuthoredWorldGenDef, AuthoredWorldGenDef->WorldChunks[1]), 25600);

	TestTrue(TEXT("Layer spans are normalized for generation"), ChunkWorld->PrepareRuntimeWorldGenDefForGeneration());
	TestTrue(TEXT("Runtime generation uses a copied WorldGenDef"), ChunkWorld->WorldGenDef != AuthoredWorldGenDef);
	TestNull(TEXT("Runtime copy uses inline normalized chunks instead of mutating a chunk data table"), ChunkWorld->WorldGenDef->WorldChunksDT);
	TestEqual(TEXT("Runtime copy preserves layer count"), ChunkWorld->WorldGenDef->WorldChunks.Num(), 2);
	TestEqual(TEXT("Parent flat-axis ChunkSizeMulti is adjusted to preserve the last-layer span"), ChunkWorld->WorldGenDef->WorldChunks[0].ChunkSizeMulti.Z, 0.5);
	TestEqual(TEXT("Parent layer now matches the last-layer flat-axis span"), CalculateFlatAxisChunkSize(ChunkWorld->WorldGenDef, ChunkWorld->WorldGenDef->WorldChunks[0]), 25600);
	TestEqual(TEXT("Authored WorldGenDef remains unchanged"), AuthoredWorldGenDef->WorldChunks[0].ChunkSizeMulti.Z, 1.0);
	TestNull(TEXT("Non-biome-strategy WorldGenDefs do not receive biome-strategy zero WorldGen overrides"), ChunkWorld->WorldGenDef->WorldGenRun);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FChunkWorldExtendedInstallsZeroWorldGenForBiomeStrategiesTest,
	"PorismExtension.ChunkWorld.WorldGen.InstallsZeroFallbackForBiomeStrategies",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FChunkWorldExtendedInstallsZeroWorldGenForBiomeStrategiesTest::RunTest(const FString& Parameters)
{
	AChunkWorldExtended* const ChunkWorld = NewObject<AChunkWorldExtended>(GetTransientPackage());
	UWorldGenDef* const AuthoredWorldGenDef = NewObject<UWorldGenDef>(GetTransientPackage());
	AuthoredWorldGenDef->BaseBlockSize = 100;
	AuthoredWorldGenDef->ChunkBlockSize = FIntVector(16, 16, 256);
	AuthoredWorldGenDef->AxisBehaviorX = EAxisBehavior::Infinity;
	AuthoredWorldGenDef->AxisBehaviorY = EAxisBehavior::Infinity;
	AuthoredWorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
	AddWorldGridMaterial(*AuthoredWorldGenDef);

	FBiomeDualData ReservationBiome;
	ReservationBiome.BiomeName = TEXT("Reservation");
	UBiomeFastNoiseEditor* const BiomeGenA = NewObject<UBiomeFastNoiseEditor>(AuthoredWorldGenDef);
	BiomeGenA->NoiseSlot = EBiomeNoiseSlot::GenA;
	ReservationBiome.GenARun = BiomeGenA;
	AuthoredWorldGenDef->WorldBiomes.Add(ReservationBiome);
	ChunkWorld->WorldGenDef = AuthoredWorldGenDef;

	TestTrue(TEXT("Biome strategy worlds receive runtime preparation"), ChunkWorld->PrepareRuntimeWorldGenDefForGeneration());
	TestTrue(TEXT("Runtime generation uses a copied WorldGenDef"), ChunkWorld->WorldGenDef != AuthoredWorldGenDef);
	TestNotNull(TEXT("Runtime copy receives an explicit zero WorldGen node"), ChunkWorld->WorldGenDef->WorldGenRun.Get());
	TestNull(TEXT("Authored WorldGenDef remains unmodified"), AuthoredWorldGenDef->WorldGenRun);

	return true;
}
