// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Terrain/LayoutCompatibleBiomeAllowList.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutFrozenTerrainBiomeAdapterInput MakeTerrainBiomeArtifactWithOwnershipSample()
	{
		FLayoutFrozenTerrainBiomeAdapterInput Artifact;
		Artifact.ArtifactId = TEXT("TerrainBiome.Test");
		FLayoutFrozenBiomeOwnershipSample& Sample = Artifact.BiomeOwnershipSamples.AddDefaulted_GetRef();
		Sample.BlockXY = FIntPoint(1, 2);
		Sample.OwningBiomeRowName = TEXT("Biome.Forest");
		Sample.bOwnedByAllowList = true;
		Sample.bHasSurfaceEvidence = true;
		return Artifact;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompatibleBiomeAllowListAppliesExplicitArtifactTest,
	"PorismExtension.Layout.Terrain.CompatibleBiomeAllowList.AppliesExplicitArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompatibleBiomeAllowListAppliesExplicitArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutProducedCompatibleBiomeAllowListArtifact AllowListArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit compatible-biome artifact builds"),
		LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
			TEXT("BiomeAllowList.Test"),
			{TEXT("Biome.Forest"), TEXT("Biome.Meadow")},
			TEXT("TestProducer"),
			AllowListArtifact,
			FailureReason));

	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact = MakeTerrainBiomeArtifactWithOwnershipSample();
	TestTrue(
		TEXT("Explicit compatible-biome artifact applies"),
		LayoutCompatibleBiomeAllowList::TryApplyProducedArtifact(
			&AllowListArtifact,
			true,
			TerrainArtifact,
			FailureReason));
	TestEqual(TEXT("Allow-list rows copied exactly"), TerrainArtifact.EligibleBiomeRowNames.Num(), 2);
	TestTrue(TEXT("Biome ownership required after explicit allow-list"), TerrainArtifact.bRequiresBiomeOwnership);
	TestTrue(TEXT("Biome ownership evidence preserved"), TerrainArtifact.bHasBiomeOwnershipEvidence);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompatibleBiomeAllowListBuildsFromFrozenTerrainBiomeArtifactTest,
	"PorismExtension.Layout.Terrain.CompatibleBiomeAllowList.BuildsFromFrozenTerrainBiomeArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompatibleBiomeAllowListBuildsFromFrozenTerrainBiomeArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact = MakeTerrainBiomeArtifactWithOwnershipSample();
	TerrainArtifact.EligibleBiomeRowNames = { TEXT("Biome.Forest"), TEXT("Biome.Meadow") };

	FLayoutProducedCompatibleBiomeAllowListArtifact AllowListArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen terrain/biome explicit rows build compatible-biome artifact"),
		LayoutCompatibleBiomeAllowList::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("BiomeAllowList.FrozenTerrain"),
			TerrainArtifact,
			TEXT("FrozenTerrainProducer"),
			AllowListArtifact,
			FailureReason));
	TestTrue(TEXT("Produced artifact present"), AllowListArtifact.bHasProducedArtifact);
	TestEqual(TEXT("Produced row count copied"), AllowListArtifact.CompatibleBiomeRowNames.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompatibleBiomeAllowListRejectsFrozenTerrainBiomeWithoutRowsTest,
	"PorismExtension.Layout.Terrain.CompatibleBiomeAllowList.RejectsFrozenTerrainBiomeWithoutRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompatibleBiomeAllowListRejectsFrozenTerrainBiomeWithoutRowsTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact = MakeTerrainBiomeArtifactWithOwnershipSample();

	FLayoutProducedCompatibleBiomeAllowListArtifact AllowListArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Frozen terrain/biome artifact without explicit rows fails closed"),
		LayoutCompatibleBiomeAllowList::TryBuildProducedArtifactFromFrozenTerrainBiomeArtifact(
			TEXT("BiomeAllowList.FrozenTerrain"),
			TerrainArtifact,
			TEXT("FrozenTerrainProducer"),
			AllowListArtifact,
			FailureReason));
	TestTrue(TEXT("Missing explicit rows failure reported"), FailureReason.Contains(TEXT("explicit row names")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompatibleBiomeAllowListRejectsRequiredMissingArtifactTest,
	"PorismExtension.Layout.Terrain.CompatibleBiomeAllowList.RejectsRequiredMissingArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompatibleBiomeAllowListRejectsRequiredMissingArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput TerrainArtifact = MakeTerrainBiomeArtifactWithOwnershipSample();
	FString FailureReason;
	TestFalse(
		TEXT("Required compatible-biome artifact fails closed when missing"),
		LayoutCompatibleBiomeAllowList::TryApplyProducedArtifact(
			nullptr,
			true,
			TerrainArtifact,
			FailureReason));
	TestTrue(TEXT("Missing required artifact clears allow-list rows"), TerrainArtifact.EligibleBiomeRowNames.IsEmpty());
	TestFalse(TEXT("Missing required artifact clears ownership requirement"), TerrainArtifact.bRequiresBiomeOwnership);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompatibleBiomeAllowListRejectsInvalidRowsTest,
	"PorismExtension.Layout.Terrain.CompatibleBiomeAllowList.RejectsInvalidRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompatibleBiomeAllowListRejectsInvalidRowsTest::RunTest(const FString& Parameters)
{
	FLayoutProducedCompatibleBiomeAllowListArtifact AllowListArtifact;
	FString FailureReason;
	TestFalse(
		TEXT("Duplicate biome rows fail closed"),
		LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
			TEXT("BiomeAllowList.Test"),
			{TEXT("Biome.Forest"), TEXT("Biome.Forest")},
			TEXT("TestProducer"),
			AllowListArtifact,
			FailureReason));

	TestFalse(
		TEXT("Empty biome rows fail closed"),
		LayoutCompatibleBiomeAllowList::TryBuildProducedArtifact(
			TEXT("BiomeAllowList.Test"),
			{FName()},
			TEXT("TestProducer"),
			AllowListArtifact,
			FailureReason));
	return true;
}
