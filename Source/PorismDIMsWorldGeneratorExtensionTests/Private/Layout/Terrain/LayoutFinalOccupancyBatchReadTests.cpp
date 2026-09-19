// Copyright 2026 Spotted Loaf Studio

#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutWorldTestUtilities.h"

#if WITH_DEV_AUTOMATION_TESTS

using namespace PorismLayoutWorldTestUtilities;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutFinalOccupancyBatchReadRefreshesGeneratedCacheTest,
	"PorismExtension.Layout.Terrain.FinalOccupancyBatchRead.RefreshesGeneratedCacheAfterSavedOverrideChunkTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutFinalOccupancyBatchReadRefreshesGeneratedCacheTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Final-occupancy batch fixture creates chunk world"), Harness.World))
	{
		return false;
	}

	const FIntVector FirstChunkCachePosition(2, 2, 3);
	const FIntVector SecondChunkSavedOverridePosition(17, 2, 3);
	const FIntVector SecondChunkCachePosition(18, 2, 3);
	constexpr int32 FirstChunkCachedMaterial = 33;
	constexpr int32 SavedOverrideMaterial = 22;
	constexpr int32 SecondChunkCachedMaterial = 44;

	// Seed generated storage through the public generator, not native-only test setters.
	// Constant terrain makes the random cache-isolation seed irrelevant to expectations.
	Harness.World->StopGen();
	Harness.World->SaveTarget /= FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Harness.World->Seed = static_cast<int32>(GetTypeHash(FGuid::NewGuid()));
	const FMaterialDef SolidMaterial = Harness.World->WorldGenDef->WorldMaterialDefs[1];
	while (Harness.World->WorldGenDef->WorldMaterialDefs.Num() <= SecondChunkCachedMaterial)
	{
		Harness.World->WorldGenDef->WorldMaterialDefs.Add(SolidMaterial);
	}
	FBiomeDualData& Row = Harness.World->WorldGenDef->WorldBiomes[0];
	Row.GenA = TEXT("AAAAAIA/");
	Row.DomainOver = 1.0f;
	FBiomeInstruction& MaterialRule = Row.GenU_Mat1.AddDefaulted_GetRef();
	MaterialRule.OutputMaterialIndex = FirstChunkCachedMaterial;
	MaterialRule.BlockMaterialIndices = {DefaultMaterial};
	MaterialRule.InvertBlockMaterials = false;
	Harness.World->StartGen();
	if (!TestTrue(TEXT("Generated-cache fixture starts with its own material mapping"),
		Harness.World->IsRunning() && Harness.World->RuntimeConfig != nullptr
		&& !Harness.World->WorldChunks.empty())) return false;
	if (!TestEqual(TEXT("Generated-cache fixture compiles one universal material rule"),
		Harness.World->RuntimeConfig->Biomes[0].CompiledRules.Get(0, 0).NormalRules.Num(), 1)) return false;
	CChunkData* Layer = Harness.World->WorldChunks.back();
	const auto GenerateMaterialChunk = [&](const FIntVector& Position, const int32 Material)
	{
		// Ocean fill writes through the public material stage without depending on
		// native normal-rule slice metadata. This test checks storage lookup only.
		Harness.World->RuntimeConfig->Ocean.GenerateOceanFlat = true;
		FRuntimeBiome& Biome = Harness.World->RuntimeConfig->Biomes[0];
		Biome.GenerateOceanFlat = true;
		Biome.RespectAirBubblesOcean = false;
		Biome.StartingOceanHeight = 1000;
		Biome.OceanMaterialIndex = Material;
		TArray<uint8> Scratch;
		Scratch.SetNumUninitialized(4 * Layer->BlockBufferSize + 7 * Layer->GenBufferSize + 64);
		std::vector<STempChunkData> Temp;
		const FIntVector Start = Harness.World->BlockWorldPosToChunkGridPos(Position, Layer) * Layer->ChunkBlockFactor;
		Harness.World->GenerateChunk(&Temp, Layer, Start,
			reinterpret_cast<char*>(Align(Scratch.GetData(), 64)), EGenStage::Material1);
	};
	GenerateMaterialChunk(FirstChunkCachePosition, FirstChunkCachedMaterial);
	GenerateMaterialChunk(SecondChunkCachePosition, SecondChunkCachedMaterial);
	Harness.World->SetBlockValueByBlockWorldPos(
		SecondChunkSavedOverridePosition,
		SavedOverrideMaterial,
		false);

	const TArray<int> Materials = Harness.World->GetBlockValuesByBlockWorldPos(
		{FirstChunkCachePosition, SecondChunkSavedOverridePosition, SecondChunkCachePosition},
		ERessourceType::MaterialIndex);

	TestEqual(TEXT("Batch read returns one material per requested position"), Materials.Num(), 3);
	if (Materials.Num() != 3)
	{
		return false;
	}
	TestEqual(TEXT("First chunk reads generated cache"), Materials[0], FirstChunkCachedMaterial);
	TestEqual(TEXT("Second chunk saved override wins"), Materials[1], SavedOverrideMaterial);
	TestEqual(
		TEXT("Second chunk default position refreshes generated cache after saved override"),
		Materials[2],
		SecondChunkCachedMaterial);
	return true;
}

#endif
