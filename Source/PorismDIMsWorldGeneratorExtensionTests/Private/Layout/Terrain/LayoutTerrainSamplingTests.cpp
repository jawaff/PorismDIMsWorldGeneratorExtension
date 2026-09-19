// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"

#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Engine/DataTable.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Misc/AutomationTest.h"

namespace
{
	using namespace PorismLayoutWorldTestUtilities;

	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");

	UWorldGenDef* CreateWorldGenDef(UObject* const Outer)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(Outer);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		return WorldGenDef;
	}

	FLayoutNoiseCoordinateSettings MakeTestCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}

	int32 CountDistinctSupportSurfaceZs(const FLayoutSteppedTerrainSupportMap& SupportMap)
	{
		TSet<int32> DistinctSupportSurfaceZs;
		for (const FLayoutSteppedTerrainSupportSample& Sample : SupportMap.SupportSamples)
		{
			DistinctSupportSurfaceZs.Add(Sample.SupportSurfaceZ);
		}

		return DistinctSupportSurfaceZs.Num();
	}

	bool AreEquivalentSteppedTerrainSupportMaps(
		const FLayoutSteppedTerrainSupportMap& Left,
		const FLayoutSteppedTerrainSupportMap& Right)
	{
		if (Left.SharedCellHeightInBlocks != Right.SharedCellHeightInBlocks
			|| Left.MaximumObservedNeighborHeightDelta != Right.MaximumObservedNeighborHeightDelta
			|| Left.MaximumObservedSnappedLevelDelta != Right.MaximumObservedSnappedLevelDelta
			|| Left.SupportSamples.Num() != Right.SupportSamples.Num()
			|| Left.AdjacencySteps.Num() != Right.AdjacencySteps.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Left.SupportSamples.Num(); ++Index)
		{
			const FLayoutSteppedTerrainSupportSample& LeftSample = Left.SupportSamples[Index];
			const FLayoutSteppedTerrainSupportSample& RightSample = Right.SupportSamples[Index];
			if (LeftSample.LocalCell != RightSample.LocalCell
				|| LeftSample.SupportSurfaceZ != RightSample.SupportSurfaceZ
				|| LeftSample.SnappedSupportFloorZ != RightSample.SnappedSupportFloorZ
				|| LeftSample.SnappedSupportCeilingZ != RightSample.SnappedSupportCeilingZ)
			{
				return false;
			}
		}

		for (int32 Index = 0; Index < Left.AdjacencySteps.Num(); ++Index)
		{
			const FLayoutSteppedTerrainAdjacencyStep& LeftStep = Left.AdjacencySteps[Index];
			const FLayoutSteppedTerrainAdjacencyStep& RightStep = Right.AdjacencySteps[Index];
			if (LeftStep.FromCell != RightStep.FromCell
				|| LeftStep.ToCell != RightStep.ToCell
				|| LeftStep.StepHeightBlocks != RightStep.StepHeightBlocks
				|| LeftStep.SnappedLevelDelta != RightStep.SnappedLevelDelta)
			{
				return false;
			}
		}

		return true;
	}

	void ConfigureActiveBiomeRow(AChunkWorldExtended* const World, const FName RowName)
	{
		check(World != nullptr);
		check(World->WorldGenDef != nullptr);
		World->WorldGenDef->WorldBiomes.Reset();
		World->WorldGenDef->WorldGen.Reset();
		World->WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(World->WorldGenDef);
		FBiomeDualData& Row = World->WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
		Row.BiomeName = RowName.ToString();
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(World->WorldGenDef);
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
	}

	void WriteSteppedSurfaceColumns(
		AChunkWorldExtended* const World,
		const TArray<FIntVector>& ColumnBases,
		const int32 SurfaceZ)
	{
		check(World != nullptr);
		for (const FIntVector& ColumnBase : ColumnBases)
		{
			for (int32 Z = 0; Z <= 16; ++Z)
			{
				World->SetBlockValueByBlockWorldPos(
					FIntVector(ColumnBase.X, ColumnBase.Y, Z),
					EmptyMaterial,
					false);
			}

			FLayoutTestWorldSupport::WriteSurfaceBlock(
				World,
				FIntVector(ColumnBase.X, ColumnBase.Y, SurfaceZ));
		}
	}

	void WriteSolidTerrainColumns(
		AChunkWorldExtended* const World,
		const TArray<FIntVector>& ColumnBases,
		const int32 MinSolidZ,
		const int32 MaxSolidZ)
	{
		check(World != nullptr);
		check(MinSolidZ <= MaxSolidZ);
		for (const FIntVector& ColumnBase : ColumnBases)
		{
			for (int32 Z = 0; Z <= MaxSolidZ + 1; ++Z)
			{
				World->SetBlockValueByBlockWorldPos(
					FIntVector(ColumnBase.X, ColumnBase.Y, Z),
					EmptyMaterial,
					false);
			}

			for (int32 Z = MinSolidZ; Z <= MaxSolidZ; ++Z)
			{
				FLayoutTestWorldSupport::WriteSurfaceBlock(
					World,
					FIntVector(ColumnBase.X, ColumnBase.Y, Z));
			}
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingFindsTopDownSurfaceTest,
	"PorismExtension.Layout.Terrain.FindsTopDownSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingFindsTopDownSurfaceTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainSurfaceSample Sample = FLayoutTerrainSampling::SampleTopDownSurface(
		FIntPoint(3, 7),
		32,
		32,
		[](const FIntVector& BlockPos)
		{
			return BlockPos.Z == 12 ? 17 : EmptyMaterial;
		},
		[](const FIntVector& BlockPos)
		{
			return BlockPos.Z == 12 ? 4 : EmptyBiome;
		});

	TestTrue(TEXT("Top-down terrain sampling finds the first solid surface in the column"), Sample.bIsValid);
	TestEqual(TEXT("Surface Z matches the first solid block"), Sample.SurfaceBlockWorldPos.Z, 12);
	TestEqual(TEXT("Surface material is reported from the sampled block"), Sample.SurfaceMaterialIndex, 17);
	TestEqual(TEXT("Surface biome switch is reported from the sampled block"), Sample.SurfaceBiomeSwitchIndex, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingResolvesFlatAnchorTest,
	"PorismExtension.Layout.Terrain.ResolvesFlatAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingResolvesFlatAnchorTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainAnchorResult Result = FLayoutTerrainSampling::ResolveFootprintAnchor(
		FIntVector(100, 200, 0),
		FIntPoint(4, 4),
		24,
		24,
		2,
		2,
		[](const FIntVector& BlockPos)
		{
			return BlockPos.Z == 10 ? 9 : EmptyMaterial;
		},
		[](const FIntVector&)
		{
			return 2;
		},
		[](const int32 MaterialIndex)
		{
			return MaterialIndex == 9;
		});

	TestTrue(TEXT("Flat terrain produces a valid anchor result"), Result.bIsValid);
	TestEqual(TEXT("Anchor Z sits one block above the supporting surface"), Result.AnchorBlockWorldPos.Z, 11);
	TestEqual(TEXT("Flat terrain has no required foundation fill"), Result.RequiredFoundationDepth, 0);
	TestFalse(TEXT("Flat terrain does not request foundation fill"), Result.bRequiresFoundationFill);
	TestEqual(TEXT("Flat terrain does not preserve a rejection kind"), Result.FailureKind, ELayoutTerrainAnchorFailureKind::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingReportsNeighborHeightDeltaWithoutGlobalCapTest,
	"PorismExtension.Layout.Terrain.ReportsNeighborHeightDeltaWithoutGlobalCap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingReportsSparseSupportFailureKindTest,
	"PorismExtension.Layout.Terrain.ReportsSparseSupportFailureKind",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingReportsNeighborHeightDeltaWithoutGlobalCapTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainAnchorResult Result = FLayoutTerrainSampling::ResolveFootprintAnchor(
		FIntVector(0, 0, 0),
		FIntPoint(4, 1),
		24,
		24,
		1,
		8,
		[](const FIntVector& BlockPos)
		{
			const int32 SurfaceZ = BlockPos.X < 2 ? 10 : 14;
			return BlockPos.Z == SurfaceZ ? 9 : EmptyMaterial;
		},
		[](const FIntVector&)
		{
			return 0;
		},
		[](const int32 MaterialIndex)
		{
			return MaterialIndex == 9;
		});

	TestTrue(TEXT("Neighbor delta alone does not reject an anchor within the footprint variation threshold"), Result.bIsValid);
	TestEqual(TEXT("Neighbor delta remains available for downstream terrain policy"), Result.MaximumObservedNeighborHeightDelta, 4);
	TestTrue(TEXT("Accepted terrain anchor has no failure reason"), Result.FailureReason.IsEmpty());
	TestEqual(TEXT("Accepted terrain anchor has no rejection kind"), Result.FailureKind, ELayoutTerrainAnchorFailureKind::None);
	return true;
}

bool FLayoutTerrainSamplingReportsSparseSupportFailureKindTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainAnchorResult Result = FLayoutTerrainSampling::ResolveFootprintAnchor(
		FIntVector(0, 0, 0),
		FIntPoint(2, 1),
		24,
		24,
		1,
		3,
		[](const FIntVector& BlockPos)
		{
			return BlockPos.X == 0 && BlockPos.Z == 10 ? 9 : EmptyMaterial;
		},
		[](const FIntVector&)
		{
			return 0;
		},
		[](const int32 MaterialIndex)
		{
			return MaterialIndex == 9;
		});

	TestFalse(TEXT("Sparse support footholds reject the anchor result"), Result.bIsValid);
	TestTrue(TEXT("Sparse support foothold rejection preserves the missing-surface failure reason"), Result.FailureReason.Contains(TEXT("supporting terrain surface")));
	TestEqual(TEXT("Structured rejection kind distinguishes sparse support from a pure all-gap miss"), Result.FailureKind, ELayoutTerrainAnchorFailureKind::SparseSupportSurface);
	TestEqual(TEXT("Sparse support foothold rejection still preserves the valid sampled support columns collected before the miss"), Result.Samples.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingReportsFoundationFillDepthTest,
	"PorismExtension.Layout.Terrain.ReportsFoundationFillDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingReportsFoundationFillDepthTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainAnchorResult Result = FLayoutTerrainSampling::ResolveFootprintAnchor(
		FIntVector(0, 0, 0),
		FIntPoint(3, 1),
		24,
		24,
		1,
		3,
		[](const FIntVector& BlockPos)
		{
			const int32 SurfaceZ = BlockPos.X == 0 ? 8 : 10;
			return BlockPos.Z == SurfaceZ ? 9 : EmptyMaterial;
		},
		[](const FIntVector&)
		{
			return 0;
		},
		[](const int32 MaterialIndex)
		{
			return MaterialIndex == 9;
		});

	TestTrue(TEXT("Mild terrain variation still resolves a valid anchor"), Result.bIsValid);
	TestTrue(TEXT("Resolved terrain fit reports foundation support when lower samples exist"), Result.bRequiresFoundationFill);
	TestEqual(TEXT("Foundation fill depth matches the gap below the resolved anchor"), Result.RequiredFoundationDepth, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingClassifiesMildSlopeTerrainTest,
	"PorismExtension.Layout.Terrain.ClassifiesMildSlopeTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingClassifiesMildSlopeTerrainTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainAnchorResult Result = FLayoutTerrainSampling::ResolveFootprintAnchor(
		FIntVector(0, 0, 0),
		FIntPoint(3, 1),
		24,
		24,
		1,
		4,
		[](const FIntVector& BlockPos)
		{
			const int32 SurfaceZ = BlockPos.X + 1;
			return BlockPos.Z == SurfaceZ ? 7 : EmptyMaterial;
		},
		[](const FIntVector&)
		{
			return 0;
		},
		[](const int32 MaterialIndex)
		{
			return MaterialIndex == 7;
		});

	TestTrue(TEXT("A shallow hill remains valid within the footprint variation threshold"), Result.bIsValid);
	// Anchor resolution no longer supplies a global neighbor cap; the classifier accepts an explicit threshold.
	const auto Classification = FLayoutTerrainSampling::ClassifyFootprintTerrainSamples(Result.Samples, 1);
	TestTrue(TEXT("Terrain classification detects mild slope transitions"), Classification.bHasMildSlopeTransitions);
	TestFalse(TEXT("Terrain classification does not flag steep edges for a shallow hill"), Classification.bHasSteepEdges);
	TestEqual(TEXT("Each sampled column receives a terrain classification"), Classification.CellClassifications.Num(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingClassifiesSteepTerrainTest,
	"PorismExtension.Layout.Terrain.ClassifiesSteepTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingClassifiesSteepTerrainTest::RunTest(const FString& Parameters)
{
	const FLayoutTerrainAnchorResult Result = FLayoutTerrainSampling::ResolveFootprintAnchor(
		FIntVector(0, 0, 0),
		FIntPoint(2, 1),
		24,
		24,
		1,
		8,
		[](const FIntVector& BlockPos)
		{
			const int32 SurfaceZ = BlockPos.X == 0 ? 3 : 7;
			return BlockPos.Z == SurfaceZ ? 11 : EmptyMaterial;
		},
		[](const FIntVector&)
		{
			return 0;
		},
		[](const int32 MaterialIndex)
		{
			return MaterialIndex == 11;
		});

	TestTrue(TEXT("A steep step within the footprint variation threshold remains a valid anchor"), Result.bIsValid);
	TestTrue(TEXT("Terrain classification still flags steep edges without global rejection"), Result.TerrainClassification.bHasSteepEdges);
	TestFalse(TEXT("Steep terrain does not masquerade as a mild transition"), Result.TerrainClassification.bHasMildSlopeTransitions);
	TestEqual(TEXT("Steep classification alone does not set an anchor rejection kind"), Result.FailureKind, ELayoutTerrainAnchorFailureKind::None);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsFoundationFillBlocksTest,
	"PorismExtension.Layout.Terrain.BuildsFoundationFillBlocks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingDetectsSteppedSupportMapDriftTest,
	"PorismExtension.Layout.Terrain.DetectsSteppedSupportMapDrift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingTreatsRawSurfaceDriftAsDiagnosticWhenSnappedContractMatchesTest,
	"PorismExtension.Layout.Terrain.TreatsRawSurfaceDriftAsDiagnosticWhenSnappedContractMatches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingRejectsSteppedTerrainSupportMapWithoutAlignedSamplesTest,
	"PorismExtension.Layout.Terrain.RejectsSteppedTerrainSupportMapWithoutAlignedSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsFoundationFillBlocksTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainAnchorResult Result;
	Result.bIsValid = true;
	Result.bRequiresFoundationFill = true;
	Result.AnchorBlockWorldPos = FIntVector(0, 0, 6);
	Result.Samples = {
		[]()
		{
			FLayoutTerrainSurfaceSample Sample;
			Sample.bIsValid = true;
			Sample.BlockXY = FIntPoint(0, 0);
			Sample.SurfaceBlockWorldPos = FIntVector(0, 0, 3);
			Sample.SurfaceMaterialIndex = 14;
			return Sample;
		}(),
		[]()
		{
			FLayoutTerrainSurfaceSample Sample;
			Sample.bIsValid = true;
			Sample.BlockXY = FIntPoint(1, 0);
			Sample.SurfaceBlockWorldPos = FIntVector(1, 0, 4);
			Sample.SurfaceMaterialIndex = 9;
			return Sample;
		}()
	};

	TArray<FIntVector> FillPositions;
	TArray<int32> FillMaterials;
	FLayoutTerrainSampling::BuildFoundationFillBlocks(Result, FillPositions, FillMaterials);

	TestEqual(TEXT("Foundation fill emits one material per filled block"), FillPositions.Num(), FillMaterials.Num());
	TestTrue(TEXT("Lower supporting terrain produces multiple filled blocks"), FillPositions.Contains(FIntVector(0, 0, 4)) && FillPositions.Contains(FIntVector(0, 0, 5)));
	TestTrue(TEXT("Near-surface supporting terrain still fills the gap below the anchor"), FillPositions.Contains(FIntVector(1, 0, 5)));
	return true;
}

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainAnchorResult AnchorResult;
	AnchorResult.bIsValid = true;
	AnchorResult.MaximumObservedNeighborHeightDelta = 2;

	FLayoutTerrainSurfaceSample& LeftSample = AnchorResult.Samples.AddDefaulted_GetRef();
	LeftSample.bIsValid = true;
	LeftSample.BlockXY = FIntPoint(100, 200);
	LeftSample.SurfaceBlockWorldPos = FIntVector(100, 200, 10);

	FLayoutTerrainSurfaceSample& RightSample = AnchorResult.Samples.AddDefaulted_GetRef();
	RightSample.bIsValid = true;
	RightSample.BlockXY = FIntPoint(116, 200);
	RightSample.SurfaceBlockWorldPos = FIntVector(116, 200, 12);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Aligned terrain samples compile into a stepped terrain support map"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMap(
			FIntVector(100, 200, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			AnchorResult,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Support map preserves the shared cell height"), SupportMap.SharedCellHeightInBlocks, 16);
	TestEqual(TEXT("Support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Support map preserves the observed raw maximum neighbor delta"), SupportMap.MaximumObservedNeighborHeightDelta, 2);
	TestEqual(TEXT("Support map preserves zero snapped level delta when both raw heights remain inside the same designated cell"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	TestEqual(TEXT("Support map records the first local cell"), SupportMap.SupportSamples[0].LocalCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Support map records the first sampled surface Z"), SupportMap.SupportSamples[0].SupportSurfaceZ, 10);
	TestEqual(TEXT("Support map records the first sampled support floor"), SupportMap.SupportSamples[0].SnappedSupportFloorZ, 0);
	TestEqual(TEXT("Support map records the first sampled support ceiling"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 16);
	TestEqual(TEXT("Support map records the raw adjacency step height"), SupportMap.AdjacencySteps[0].StepHeightBlocks, 2);
	TestEqual(TEXT("Support map records zero snapped level delta for same-cell terrain variation"), SupportMap.AdjacencySteps[0].SnappedLevelDelta, 0);
	return true;
}

bool FLayoutTerrainSamplingDetectsSteppedSupportMapDriftTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap Expected;
	Expected.SharedCellHeightInBlocks = 16;
	FLayoutSteppedTerrainSupportSample& ExpectedSample = Expected.SupportSamples.AddDefaulted_GetRef();
	ExpectedSample.LocalCell = FIntVector(0, 0, 0);
	ExpectedSample.SupportSurfaceZ = 3;
	ExpectedSample.SnappedSupportFloorZ = 0;
	ExpectedSample.SnappedSupportCeilingZ = 16;

	FLayoutSteppedTerrainSupportMap Drifted = Expected;
	Drifted.SupportSamples[0].SnappedSupportCeilingZ = 32;

	FString MismatchReason;
	TestFalse(
		TEXT("Support-map comparison rejects snapped lattice drift"),
		FLayoutTerrainSampling::AreSteppedTerrainSupportMapsEquivalent(Expected, Drifted, &MismatchReason));
	TestTrue(
		TEXT("Support-map comparison reports a mismatch reason for request-builder diagnostics"),
		!MismatchReason.IsEmpty());
	return true;
}

bool FLayoutTerrainSamplingTreatsRawSurfaceDriftAsDiagnosticWhenSnappedContractMatchesTest::RunTest(const FString& Parameters)
{
	FLayoutSteppedTerrainSupportMap Expected;
	Expected.SharedCellHeightInBlocks = 16;
	Expected.MaximumObservedNeighborHeightDelta = 2;
	Expected.MaximumObservedSnappedLevelDelta = 0;
	FLayoutSteppedTerrainSupportSample& ExpectedLeftSample = Expected.SupportSamples.AddDefaulted_GetRef();
	ExpectedLeftSample.LocalCell = FIntVector(0, 0, 0);
	ExpectedLeftSample.SupportSurfaceZ = 3;
	ExpectedLeftSample.SnappedSupportFloorZ = 0;
	ExpectedLeftSample.SnappedSupportCeilingZ = 16;
	FLayoutSteppedTerrainSupportSample& ExpectedRightSample = Expected.SupportSamples.AddDefaulted_GetRef();
	ExpectedRightSample.LocalCell = FIntVector(1, 0, 0);
	ExpectedRightSample.SupportSurfaceZ = 5;
	ExpectedRightSample.SnappedSupportFloorZ = 0;
	ExpectedRightSample.SnappedSupportCeilingZ = 16;
	FLayoutSteppedTerrainAdjacencyStep& ExpectedStep = Expected.AdjacencySteps.AddDefaulted_GetRef();
	ExpectedStep.FromCell = FIntVector(0, 0, 0);
	ExpectedStep.ToCell = FIntVector(1, 0, 0);
	ExpectedStep.StepHeightBlocks = 2;
	ExpectedStep.SnappedLevelDelta = 0;

	FLayoutSteppedTerrainSupportMap Drifted = Expected;
	Drifted.SupportSamples[0].SupportSurfaceZ = 8;
	Drifted.SupportSamples[1].SupportSurfaceZ = 10;
	Drifted.AdjacencySteps[0].StepHeightBlocks = 2;

	FString MismatchReason;
	TestTrue(
		TEXT("Contract comparison ignores raw sampled surface drift when snapped ownership and measured neighbor delta stay the same"),
		FLayoutTerrainSampling::AreSteppedTerrainSupportMapsContractEquivalent(Expected, Drifted, &MismatchReason));
	TestTrue(
		TEXT("Contract comparison leaves mismatch reason empty when only diagnostic raw terrain data drifts"),
		MismatchReason.IsEmpty());

	Drifted.MaximumObservedNeighborHeightDelta = 5;
	Drifted.SupportSamples[1].SupportSurfaceZ = 13;
	Drifted.AdjacencySteps[0].StepHeightBlocks = 5;
	TestFalse(TEXT("Changed measured neighbor delta invalidates the contract despite unchanged snapped ownership"),
		FLayoutTerrainSampling::AreSteppedTerrainSupportMapsContractEquivalent(Expected, Drifted, &MismatchReason));
	TestTrue(TEXT("Measured-delta mismatch reports its own contract field"),
		MismatchReason.Contains(TEXT("raw neighbor delta differs")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromSurfaceHeightsTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapFromSurfaceHeights",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapAcrossLatticeBoundaryTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapAcrossLatticeBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromSurfaceHeightsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	TMap<FIntPoint, int32> SurfaceZByBlockXY;
	SurfaceZByBlockXY.Add(FIntPoint(100, 200), 10);
	SurfaceZByBlockXY.Add(FIntPoint(116, 200), 12);

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen surface heights compile into a stepped terrain support map without live world sampling"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
			FIntVector(100, 200, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceZByBlockXY,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Frozen-surface support map preserves the shared cell height"), SupportMap.SharedCellHeightInBlocks, 16);
	TestEqual(TEXT("Frozen-surface support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Frozen-surface support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Frozen-surface support map preserves the observed raw neighbor delta"), SupportMap.MaximumObservedNeighborHeightDelta, 2);
	TestEqual(TEXT("Frozen-surface support map preserves zero snapped level delta when both raw heights remain inside the same designated cell"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	TestEqual(TEXT("Frozen-surface support map records the first local cell"), SupportMap.SupportSamples[0].LocalCell, FIntVector(0, 0, 0));
	TestEqual(TEXT("Frozen-surface support map records the first sampled surface Z"), SupportMap.SupportSamples[0].SupportSurfaceZ, 10);
	TestEqual(TEXT("Frozen-surface support map rounds the first sampled surface down to the lower lattice floor"), SupportMap.SupportSamples[0].SnappedSupportFloorZ, 0);
	TestEqual(TEXT("Frozen-surface support map rounds the first sampled surface up to the owning lattice ceiling"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 16);
	TestEqual(TEXT("Frozen-surface support map records the raw adjacency step height"), SupportMap.AdjacencySteps[0].StepHeightBlocks, 2);
	TestEqual(TEXT("Frozen-surface support map records zero snapped level delta for same-cell terrain variation"), SupportMap.AdjacencySteps[0].SnappedLevelDelta, 0);
	return true;
}

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapAcrossLatticeBoundaryTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	TMap<FIntPoint, int32> SurfaceZByBlockXY;
	SurfaceZByBlockXY.Add(FIntPoint(100, 200), 16);
	SurfaceZByBlockXY.Add(FIntPoint(116, 200), 17);

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen surface heights compile across one designated-cell lattice boundary"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromSurfaceHeights(
			FIntVector(100, 200, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceZByBlockXY,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Boundary-crossing support map preserves the raw maximum neighbor delta"), SupportMap.MaximumObservedNeighborHeightDelta, 1);
	TestEqual(TEXT("Boundary-crossing support map detects one snapped designated-cell level delta"), SupportMap.MaximumObservedSnappedLevelDelta, 1);
	TestEqual(TEXT("A surface already on the lattice keeps the lower snapped floor"), SupportMap.SupportSamples[0].SnappedSupportFloorZ, 16);
	TestEqual(TEXT("A surface already on the lattice keeps the upper snapped ceiling"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 16);
	TestEqual(TEXT("A surface one block above the lattice keeps the lower snapped floor of the previous designated cell"), SupportMap.SupportSamples[1].SnappedSupportFloorZ, 16);
	TestEqual(TEXT("A surface one block above the lattice snaps upward into the next designated cell"), SupportMap.SupportSamples[1].SnappedSupportCeilingZ, 32);
	TestEqual(TEXT("Boundary-crossing support map preserves the raw adjacency step height"), SupportMap.AdjacencySteps[0].StepHeightBlocks, 1);
	TestEqual(TEXT("Boundary-crossing support map records one snapped level delta on the adjacency"), SupportMap.AdjacencySteps[0].SnappedLevelDelta, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromPocketSamplesTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapFromPocketSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromPocketSamplesTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	TArray<FLayoutReservationPocketSample> PocketSamples;
	{
		FLayoutReservationPocketSample& LeftSample = PocketSamples.AddDefaulted_GetRef();
		LeftSample.SampleGridXY = FIntPoint(0, 0);
		LeftSample.BlockXY = FIntPoint(100, 200);
		LeftSample.NoiseValue = 1.0f;
		LeftSample.bHasSurfaceZ = true;
		LeftSample.SurfaceZBlockWorld = 10;

		FLayoutReservationPocketSample& RightSample = PocketSamples.AddDefaulted_GetRef();
		RightSample.SampleGridXY = FIntPoint(1, 0);
		RightSample.BlockXY = FIntPoint(116, 200);
		RightSample.NoiseValue = 1.0f;
		RightSample.bHasSurfaceZ = true;
		RightSample.SurfaceZBlockWorld = 12;
	}

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Frozen pocket samples compile into a stepped terrain support map without live world sampling"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromPocketSamples(
			FIntVector(100, 200, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			PocketSamples,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Frozen-pocket support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Frozen-pocket support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Frozen-pocket support map derives the observed raw maximum neighbor delta"), SupportMap.MaximumObservedNeighborHeightDelta, 2);
	TestEqual(TEXT("Frozen-pocket support map derives zero snapped level delta when both raw heights remain inside the same designated cell"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	TestEqual(TEXT("Frozen-pocket support map records the first sampled surface Z"), SupportMap.SupportSamples[0].SupportSurfaceZ, 10);
	TestEqual(TEXT("Frozen-pocket support map records the first sampled support floor"), SupportMap.SupportSamples[0].SnappedSupportFloorZ, 0);
	TestEqual(TEXT("Frozen-pocket support map records the first sampled support ceiling"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 16);
	TestEqual(TEXT("Frozen-pocket support map records the raw adjacency step height"), SupportMap.AdjacencySteps[0].StepHeightBlocks, 2);
	TestEqual(TEXT("Frozen-pocket support map records zero snapped level delta for same-cell terrain variation"), SupportMap.AdjacencySteps[0].SnappedLevelDelta, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromActiveBiomeSamplerTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapFromActiveBiomeSampler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromActiveBiomeSamplerWhenSearchStartsInsideSolidTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapFromActiveBiomeSamplerWhenSearchStartsInsideSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromActiveBiomeSurfaceWorldWhenSearchStartsInsideSolidTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapFromActiveBiomeSurfaceWorldWhenSearchStartsInsideSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsEquivalentSteppedTerrainSupportMapsFromActiveBiomeSamplerAndWorldWhenSearchStartsInsideSolidTest,
	"PorismExtension.Layout.Terrain.BuildsEquivalentSteppedTerrainSupportMapsFromActiveBiomeSamplerAndWorldWhenSearchStartsInsideSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingSteppedTerrainSupportMapUsesSharedCellCenterSamplePhaseTest,
	"PorismExtension.Layout.Terrain.SteppedTerrainSupportMapUsesSharedCellCenterSamplePhase",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromActiveBiomeSamplerTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	Row.DomainOver = 1.0f;
	Row.GenU_Mat1.AddDefaulted();
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles runtime-node rows for direct stepped support sampling"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 10;
	SurfaceSearch.TerrainSearchDepthBlocks = 40;
	const int32 ExpectedRecoveredSurfaceZ = SurfaceSearch.TerrainSearchStartZ + SurfaceSearch.TerrainSearchDepthBlocks - 1;

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Active biome sampler compiles directly into a stepped terrain support map without live world sampling"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
			FIntVector(0, 0, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceSearch,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Active-biome support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Active-biome support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Active-biome support map derives zero observed raw neighbor delta for flat runtime-node terrain"), SupportMap.MaximumObservedNeighborHeightDelta, 0);
	TestEqual(TEXT("Active-biome support map derives zero snapped level delta for flat runtime-node terrain"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	if (SupportMap.SupportSamples.Num() != 2 || SupportMap.AdjacencySteps.Num() != 1) return false;
	TestEqual(TEXT("Active-biome support map records the highest reachable sampled surface Z"), SupportMap.SupportSamples[0].SupportSurfaceZ, ExpectedRecoveredSurfaceZ);
	TestEqual(TEXT("Active-biome support map records the snapped support ceiling"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 64);
	TestEqual(TEXT("Active-biome support map records the raw adjacency step height"), SupportMap.AdjacencySteps[0].StepHeightBlocks, 0);
	TestEqual(TEXT("Active-biome support map records zero snapped level delta on the adjacency"), SupportMap.AdjacencySteps[0].SnappedLevelDelta, 0);
	return true;
}

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromActiveBiomeSamplerWhenSearchStartsInsideSolidTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	Row.DomainOver = 1.0f;
	Row.GenU_Mat1.AddDefaulted();
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles runtime-node rows for inside-solid stepped support sampling"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 5;
	SurfaceSearch.TerrainSearchDepthBlocks = 40;
	const int32 ExpectedRecoveredSurfaceZ = SurfaceSearch.TerrainSearchStartZ + SurfaceSearch.TerrainSearchDepthBlocks - 1;

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Active biome sampler still builds a stepped terrain support map when the search starts inside solid terrain"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
			FIntVector(0, 0, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceSearch,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Inside-solid active-biome support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Inside-solid active-biome support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Inside-solid active-biome support map still derives zero observed raw neighbor delta for flat runtime-node terrain"), SupportMap.MaximumObservedNeighborHeightDelta, 0);
	TestEqual(TEXT("Inside-solid active-biome support map still derives zero snapped level delta for flat runtime-node terrain"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	if (SupportMap.SupportSamples.Num() != 2 || SupportMap.AdjacencySteps.Num() != 1) return false;
	TestEqual(TEXT("Inside-solid active-biome support map recovers the first sampled surface Z from the highest reachable owned solid block"), SupportMap.SupportSamples[0].SupportSurfaceZ, ExpectedRecoveredSurfaceZ);
	TestEqual(TEXT("Inside-solid active-biome support map also recovers the second sampled surface Z from the highest reachable owned solid block"), SupportMap.SupportSamples[1].SupportSurfaceZ, ExpectedRecoveredSurfaceZ);
	TestEqual(TEXT("Inside-solid active-biome support map snaps the first sampled surface up to the owning designated cell"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 48);
	return true;
}

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromActiveBiomeSurfaceWorldWhenSearchStartsInsideSolidTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("Reservation"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a world for inside-solid world-backed stepped support coverage"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigureActiveBiomeRow(Harness.World, BiomeRowName);
	WriteSolidTerrainColumns(
		Harness.World,
		{
			FIntVector(8, 8, 0),
			FIntVector(24, 8, 0)
		},
		0,
		44);

	FLayoutActiveBiomeSampler Sampler;
	if (!TestTrue(
			TEXT("Active biome sampler initializes for inside-solid world-backed stepped support coverage"),
			Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
	{
		return false;
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 5;
	SurfaceSearch.TerrainSearchDepthBlocks = 40;
	const int32 ExpectedRecoveredSurfaceZ = SurfaceSearch.TerrainSearchStartZ + SurfaceSearch.TerrainSearchDepthBlocks - 1;

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("World-backed active-biome support map now builds when the search starts inside solid terrain"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSurfaceWorld(
			Harness.World,
			FIntVector(0, 0, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceSearch,
			MakeTestCoordinateSettings(Harness.World->WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Inside-solid world-backed support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Inside-solid world-backed support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Inside-solid world-backed support map derives zero observed raw neighbor delta for flat authored terrain"), SupportMap.MaximumObservedNeighborHeightDelta, 0);
	TestEqual(TEXT("Inside-solid world-backed support map derives zero snapped level delta for flat authored terrain"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	TestEqual(TEXT("Inside-solid world-backed support map recovers the first sampled surface Z from the highest reachable owned solid block"), SupportMap.SupportSamples[0].SupportSurfaceZ, ExpectedRecoveredSurfaceZ);
	TestEqual(TEXT("Inside-solid world-backed support map also recovers the second sampled surface Z from the highest reachable owned solid block"), SupportMap.SupportSamples[1].SupportSurfaceZ, ExpectedRecoveredSurfaceZ);
	TestEqual(TEXT("Inside-solid world-backed support map snaps the first sampled surface up to the owning designated cell"), SupportMap.SupportSamples[0].SnappedSupportCeilingZ, 48);
	return true;
}

bool FLayoutTerrainSamplingBuildsEquivalentSteppedTerrainSupportMapsFromActiveBiomeSamplerAndWorldWhenSearchStartsInsideSolidTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("Reservation"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a world for inside-solid stepped support parity coverage"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigureActiveBiomeRow(Harness.World, BiomeRowName);
	WriteSolidTerrainColumns(
		Harness.World,
		{
			FIntVector(8, 8, 0),
			FIntVector(24, 8, 0)
		},
		0,
		44);

	FLayoutActiveBiomeSampler Sampler;
	if (!TestTrue(
			TEXT("Active biome sampler initializes for inside-solid stepped support parity coverage"),
			Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
	{
		return false;
	}

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 5;
	SurfaceSearch.TerrainSearchDepthBlocks = 40;

	FLayoutSteppedTerrainSupportMap SamplerSupportMap;
	FLayoutSteppedTerrainSupportMap WorldSupportMap;
	FString SamplerFailureReason;
	FString WorldFailureReason;
	if (!TestTrue(
			TEXT("Noise-backed active-biome support map builds for inside-solid parity coverage"),
			FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
				
				FIntVector(0, 0, 0),
				FIntVector(16, 16, 16),
				16,
				PlannedCells,
				SurfaceSearch,
				MakeTestCoordinateSettings(Harness.World->WorldGenDef),
				Sampler,
				SamplerSupportMap,
				SamplerFailureReason)))
	{
		AddError(SamplerFailureReason);
		return false;
	}

	if (!TestTrue(
			TEXT("World-backed active-biome support map builds for inside-solid parity coverage"),
			FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSurfaceWorld(
				Harness.World,
				FIntVector(0, 0, 0),
				FIntVector(16, 16, 16),
				16,
				PlannedCells,
				SurfaceSearch,
				MakeTestCoordinateSettings(Harness.World->WorldGenDef),
				Sampler,
				WorldSupportMap,
				WorldFailureReason)))
	{
		AddError(WorldFailureReason);
		return false;
	}

	TestTrue(
		TEXT("Noise-backed and world-backed active-biome support compilation now preserve the same stepped support carrier when the search starts inside solid terrain"),
		AreEquivalentSteppedTerrainSupportMaps(SamplerSupportMap, WorldSupportMap));
	return true;
}

bool FLayoutTerrainSamplingSteppedTerrainSupportMapUsesSharedCellCenterSamplePhaseTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("Reservation"));
	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			PlannedCells.Add({FIntVector(X, Y, 0), X == 0 && Y == 0 ? ELayoutCellIntent::Entry : ELayoutCellIntent::Interior});
		}
	}

	const TArray<FIntVector> NonShiftedFirstStepColumns = {
		FIntVector(0, 0, 0),
		FIntVector(0, 16, 0),
		FIntVector(0, 32, 0)
	};
	const TArray<FIntVector> NonShiftedSecondStepColumns = {
		FIntVector(16, 0, 0),
		FIntVector(16, 16, 0),
		FIntVector(16, 32, 0)
	};
	const TArray<FIntVector> NonShiftedThirdStepColumns = {
		FIntVector(32, 0, 0),
		FIntVector(32, 16, 0),
		FIntVector(32, 32, 0)
	};
	const TArray<FIntVector> NonShiftedFourthStepColumns = {
		FIntVector(48, 0, 0),
		FIntVector(48, 16, 0),
		FIntVector(48, 32, 0)
	};
	const TArray<FIntVector> NonShiftedFifthStepColumns = {
		FIntVector(64, 0, 0),
		FIntVector(64, 16, 0),
		FIntVector(64, 32, 0)
	};
	const TArray<FIntVector> ShiftedFirstStepColumns = {
		FIntVector(8, 8, 0),
		FIntVector(8, 24, 0),
		FIntVector(8, 40, 0)
	};
	const TArray<FIntVector> ShiftedSecondStepColumns = {
		FIntVector(24, 8, 0),
		FIntVector(24, 24, 0),
		FIntVector(24, 40, 0)
	};
	const TArray<FIntVector> ShiftedThirdStepColumns = {
		FIntVector(40, 8, 0),
		FIntVector(40, 24, 0),
		FIntVector(40, 40, 0)
	};
	const TArray<FIntVector> ShiftedFourthStepColumns = {
		FIntVector(56, 8, 0),
		FIntVector(56, 24, 0),
		FIntVector(56, 40, 0)
	};
	const TArray<FIntVector> ShiftedFifthStepColumns = {
		FIntVector(72, 8, 0),
		FIntVector(72, 24, 0),
		FIntVector(72, 40, 0)
	};

	auto BuildSupportMapForColumns =
		[&](
			const TArray<FIntVector>& FirstStepColumns,
			const TArray<FIntVector>& SecondStepColumns,
			const TArray<FIntVector>& ThirdStepColumns,
			const TArray<FIntVector>& FourthStepColumns,
			const TArray<FIntVector>& FifthStepColumns,
			FLayoutSteppedTerrainSupportMap& OutSupportMap)
	{
		FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
		TestNotNull(TEXT("Chunk-world harness creates a world for stepped support phase coverage"), Harness.World);
		if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
		{
			return false;
		}

		ConfigureActiveBiomeRow(Harness.World, BiomeRowName);
		WriteSteppedSurfaceColumns(Harness.World, FirstStepColumns, 3);
		WriteSteppedSurfaceColumns(Harness.World, SecondStepColumns, 5);
		WriteSteppedSurfaceColumns(Harness.World, ThirdStepColumns, 7);
		WriteSteppedSurfaceColumns(Harness.World, FourthStepColumns, 9);
		WriteSteppedSurfaceColumns(Harness.World, FifthStepColumns, 11);

		FLayoutActiveBiomeSampler Sampler;
		if (!TestTrue(
				TEXT("Active biome sampler initializes for world-backed stepped support phase coverage"),
				Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
		{
			return false;
		}

		FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
		SurfaceSearch.TerrainSearchStartZ = 16;
		SurfaceSearch.TerrainSearchDepthBlocks = 16;

		FString FailureReason;
		if (!TestTrue(
				TEXT("World-backed stepped support map builds for the authored five-column stepped fixture"),
				FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSurfaceWorld(
					Harness.World,
					FIntVector(0, 0, 16),
					FIntVector(16, 16, 16),
					16,
					PlannedCells,
					SurfaceSearch,
					MakeTestCoordinateSettings(Harness.World->WorldGenDef),
					Sampler,
					OutSupportMap,
					FailureReason)))
		{
			AddError(FailureReason);
			return false;
		}

		return true;
	};

	FLayoutSteppedTerrainSupportMap NonShiftedSupportMap;
	if (!BuildSupportMapForColumns(
			NonShiftedFirstStepColumns,
			NonShiftedSecondStepColumns,
			NonShiftedThirdStepColumns,
			NonShiftedFourthStepColumns,
			NonShiftedFifthStepColumns,
			NonShiftedSupportMap))
	{
		return false;
	}

	TestEqual(TEXT("Non-shifted world-backed stepped support map preserves one sample per planned cell"), NonShiftedSupportMap.SupportSamples.Num(), 15);
	TestEqual(TEXT("Non-shifted world-backed stepped support map now collapses to one sampled support height on the shared-cell center sample phase"), CountDistinctSupportSurfaceZs(NonShiftedSupportMap), 1);
	TestEqual(TEXT("Non-shifted world-backed stepped support map now carries zero observed neighbor height delta on the shared-cell center sample phase"), NonShiftedSupportMap.MaximumObservedNeighborHeightDelta, 0);
	TestEqual(TEXT("Non-shifted world-backed stepped support map also carries zero snapped level delta on the shared-cell center sample phase"), NonShiftedSupportMap.MaximumObservedSnappedLevelDelta, 0);

	FLayoutSteppedTerrainSupportMap ShiftedSupportMap;
	if (!BuildSupportMapForColumns(
			ShiftedFirstStepColumns,
			ShiftedSecondStepColumns,
			ShiftedThirdStepColumns,
			ShiftedFourthStepColumns,
			ShiftedFifthStepColumns,
			ShiftedSupportMap))
	{
		return false;
	}

	TestEqual(TEXT("Shifted world-backed stepped support map preserves one sample per planned cell"), ShiftedSupportMap.SupportSamples.Num(), 15);
	TestTrue(TEXT("Shifted world-backed stepped support map now carries more than one sampled support height on the shared-cell center sample phase"), CountDistinctSupportSurfaceZs(ShiftedSupportMap) > 1);
	TestTrue(TEXT("Shifted world-backed stepped support map now carries a non-zero observed neighbor height delta on the shared-cell center sample phase"), ShiftedSupportMap.MaximumObservedNeighborHeightDelta > 0);
	TestEqual(TEXT("Shifted world-backed stepped support map still carries zero snapped level delta when all sampled heights remain inside the same designated cell"), ShiftedSupportMap.MaximumObservedSnappedLevelDelta, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromAnyActiveBiomeSurfaceTest,
	"PorismExtension.Layout.Terrain.BuildsSteppedTerrainSupportMapFromAnyActiveBiomeSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsSteppedTerrainSupportMapFromAnyActiveBiomeSurfaceTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	Row.DomainOver = 1.0f;
	Row.GenU_Mat1.AddDefaulted();
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles runtime-node rows for any-active-biome stepped support sampling"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 10;
	SurfaceSearch.TerrainSearchDepthBlocks = 40;

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Active biome sampler compiles a stepped terrain support map from the winning active biome when no eligible row is supplied"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
			FIntVector(0, 0, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceSearch,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Any-active-biome support map preserves one sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Any-active-biome support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Any-active-biome support map derives zero observed raw neighbor delta for flat runtime-node terrain"), SupportMap.MaximumObservedNeighborHeightDelta, 0);
	TestEqual(TEXT("Any-active-biome support map derives zero snapped level delta for flat runtime-node terrain"), SupportMap.MaximumObservedSnappedLevelDelta, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingRejectsSteppedTerrainSupportMapFromActiveBiomeSamplerWithoutEligibleSurfaceTest,
	"PorismExtension.Layout.Terrain.RejectsSteppedTerrainSupportMapFromActiveBiomeSamplerWithoutEligibleSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingRejectsSteppedTerrainSupportMapFromActiveBiomeSamplerWithoutEligibleSurfaceTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles runtime-node rows for direct stepped support rejection coverage"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 10;
	SurfaceSearch.TerrainSearchDepthBlocks = 40;

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestFalse(
		TEXT("Active biome support-map compilation rejects when no active biome surface exists"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromActiveBiomeSampler(
			FIntVector(0, 0, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			SurfaceSearch,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestTrue(TEXT("Active biome support-map rejection reports the missing active surface"), FailureReason.Contains(TEXT("found no active biome surface")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsColumnProfilesFromEmptyStatesTest,
	"PorismExtension.Layout.Terrain.ColumnProfiles.BuildsFromEmptyStates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsColumnProfilesFromEmptyStatesTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutTerrainColumnProfile> Profiles;
	FString FailureReason;
	TestTrue(
		TEXT("Shared column builder retains generated-style solid and empty runs"),
		FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
			FIntVector(40, 50, 0), 1, 1, 0, 5,
			TArray<uint8>{0, 1, 1, 0, 1, 1},
			Profiles,
			FailureReason));
	TestEqual(TEXT("One bounded column profile emitted"), Profiles.Num(), 1);
	if (Profiles.Num() == 1)
	{
		TestEqual(TEXT("Profile retains four runs"), Profiles[0].Runs.Num(), 4);
		TestTrue(TEXT("First run remains solid"), !Profiles[0].Runs[0].bIsEmpty);
		TestEqual(TEXT("First empty run starts at Z=1"), Profiles[0].Runs[1].MinZ, 1);
		TestEqual(TEXT("Second empty run ends at Z=5"), Profiles[0].Runs[3].MaxZ, 5);
	}
	TestFalse(
		TEXT("Shared column builder rejects truncated final-occupancy packet"),
		FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
			FIntVector(40, 50, 0), 1, 1, 0, 5,
			TArray<uint8>{0, 1, 1},
			Profiles,
			FailureReason));
	TestTrue(TEXT("Truncated packet reports expected sample count"), FailureReason.Contains(TEXT("expected")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingSelectsConnectedEmptyComponentTest,
	"PorismExtension.Layout.Terrain.ColumnProfiles.SelectsConnectedEmptyComponent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingSelectsConnectedEmptyComponentTest::RunTest(const FString& Parameters)
{
	TArray<uint8> EmptyStates;
	for (int32 Z = 0; Z <= 6; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				const bool bBlockedColumn = X == 1 && Y == 1;
				const bool bCavity = Z >= 1 && Z <= 2;
				const bool bOpenSurface = Z >= 4;
				EmptyStates.Add(!bBlockedColumn && (bCavity || bOpenSurface) ? 1 : 0);
			}
		}
	}

	TArray<FLayoutTerrainColumnProfile> Profiles;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Source-neutral profiles build stacked cavity and surface runs"),
		FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
			FIntVector::ZeroValue,
			2,
			2,
			0,
			6,
			EmptyStates,
			Profiles,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	TMap<FIntPoint, FLayoutTerrainColumnRun> SelectedRuns;
	bool bUnderground = false;
	TestTrue(
		TEXT("Enclosed anchor selects connected cavity component"),
		FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
			Profiles,
			FIntVector(0, 0, 1),
			1,
			SelectedRuns,
			bUnderground,
			FailureReason));
	TestTrue(TEXT("Enclosed connected component classifies Underground"), bUnderground);
	TestEqual(TEXT("Cardinal component excludes blocked column"), SelectedRuns.Num(), 3);
	for (const TPair<FIntPoint, FLayoutTerrainColumnRun>& Pair : SelectedRuns)
	{
		TestEqual(TEXT("Selected cavity component keeps lower empty run"), Pair.Value.MinZ, 1);
		TestEqual(TEXT("Selected cavity component does not jump to open surface"), Pair.Value.MaxZ, 2);
	}

	SelectedRuns.Reset();
	bUnderground = true;
	TestTrue(
		TEXT("Open anchor selects connected surface component"),
		FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
			Profiles,
			FIntVector(0, 0, 4),
			1,
			SelectedRuns,
			bUnderground,
			FailureReason));
	TestFalse(TEXT("Component reaching complete upper sample boundary classifies Surface"), bUnderground);
	TestEqual(TEXT("Surface component also excludes blocked column"), SelectedRuns.Num(), 3);

	TArray<FLayoutTerrainColumnProfile> ReversedProfiles;
	for (int32 Index = Profiles.Num() - 1; Index >= 0; --Index)
	{
		ReversedProfiles.Add(Profiles[Index]);
	}
	TMap<FIntPoint, FLayoutTerrainColumnRun> ReorderedRuns;
	bool bReorderedUnderground = false;
	TestTrue(
		TEXT("Selected component ignores profile input order"),
		FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
			ReversedProfiles,
			FIntVector(0, 0, 1),
			1,
			ReorderedRuns,
			bReorderedUnderground,
			FailureReason));
	TestTrue(TEXT("Reordered cavity remains Underground"), bReorderedUnderground);
	TestEqual(TEXT("Reordered cavity selects same column count"), ReorderedRuns.Num(), 3);
	TMap<FIntPoint, FLayoutTerrainColumnRun> OriginalCavityRuns;
	TestTrue(
		TEXT("Original cavity component rebuilds for order comparison"),
		FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
			Profiles,
			FIntVector(0, 0, 1),
			1,
			OriginalCavityRuns,
			bUnderground,
			FailureReason));
	for (const TPair<FIntPoint, FLayoutTerrainColumnRun>& Pair : ReorderedRuns)
	{
		const FLayoutTerrainColumnRun* const OriginalRun = OriginalCavityRuns.Find(Pair.Key);
		TestTrue(TEXT("Reordered cavity preserves selected run"),
			OriginalRun != nullptr && OriginalRun->MinZ == Pair.Value.MinZ && OriginalRun->MaxZ == Pair.Value.MaxZ);
	}

	TArray<uint8> SlopedStates;
	for (int32 Z = 0; Z <= 6; ++Z)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			const int32 RunMinZ = X + 1;
			SlopedStates.Add(Z >= RunMinZ && Z <= RunMinZ + 2 ? 1 : 0);
		}
	}
	TArray<FLayoutTerrainColumnProfile> SlopedProfiles;
	TestTrue(
		TEXT("Gradual cavity floor profiles build"),
		FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
			FIntVector::ZeroValue, 3, 1, 0, 6, SlopedStates, SlopedProfiles, FailureReason));
	TMap<FIntPoint, FLayoutTerrainColumnRun> SlopedRuns;
	TestTrue(
		TEXT("Cardinal overlap propagates gradual cavity floor"),
		FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
			SlopedProfiles, FIntVector(0, 0, 1), 1, SlopedRuns, bUnderground, FailureReason));
	TestTrue(TEXT("Gradual enclosed cavity remains Underground"), bUnderground);
	TestEqual(TEXT("Gradual cavity reaches every cardinal column"), SlopedRuns.Num(), 3);
	for (int32 X = 0; X < 3; ++X)
	{
		const FLayoutTerrainColumnRun* const Run = SlopedRuns.Find(FIntPoint(X, 0));
		TestTrue(FString::Printf(TEXT("Gradual cavity column %d keeps local floor"), X),
			Run != nullptr && Run->MinZ == X + 1);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingClassifiesCenterAirGapAcrossLayoutOverlapTest,
	"PorismExtension.Layout.Terrain.ColumnProfiles.CenterAirGapOwnsEnvironment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingClassifiesCenterAirGapAcrossLayoutOverlapTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutTerrainColumnRun SelectedAirRun;
	bool bUnderground = false;

	FLayoutTerrainColumnProfile SurfaceProfile;
	SurfaceProfile.BlockXY = FIntPoint(4, 4);
	SurfaceProfile.Runs = {
		FLayoutTerrainColumnRun{0, 5, false},
		FLayoutTerrainColumnRun{6, 30, true}
	};
	TestTrue(TEXT("Surface classification tolerates terrain intersecting layout bottom"),
		FLayoutTerrainSampling::TryClassifyCenterColumnEnvironment(
			SurfaceProfile, 4, 15, SelectedAirRun, bUnderground, FailureReason));
	TestFalse(TEXT("Bottom intersection with air open above remains Surface"), bUnderground);
	TestEqual(TEXT("Surface classification selects air above intersecting bottom terrain"), SelectedAirRun.MinZ, 6);

	FLayoutTerrainColumnProfile CavityProfile;
	CavityProfile.BlockXY = FIntPoint(4, 4);
	CavityProfile.Runs = {
		FLayoutTerrainColumnRun{0, 4, false},
		FLayoutTerrainColumnRun{5, 11, true},
		FLayoutTerrainColumnRun{12, 30, false}
	};
	TestTrue(TEXT("Cavity classification tolerates roof intersecting layout top"),
		FLayoutTerrainSampling::TryClassifyCenterColumnEnvironment(
			CavityProfile, 5, 15, SelectedAirRun, bUnderground, FailureReason));
	TestTrue(TEXT("Air gap between lower and upper terrain remains Underground"), bUnderground);
	TestEqual(TEXT("Cavity classification selects intervening air gap"), SelectedAirRun.MaxZ, 11);

	TestTrue(TEXT("Later stepped upward placement still resolves the same cavity air gap"),
		FLayoutTerrainSampling::TryClassifyCenterColumnEnvironment(
			CavityProfile, 8, 15, SelectedAirRun, bUnderground, FailureReason));
	TestTrue(TEXT("Stepped-envelope overlap does not erase upper terrain evidence"), bUnderground);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingUsesFinalOccupancyForCavityDefinitionTest,
	"PorismExtension.Layout.Terrain.ColumnProfiles.FinalOccupancyOwnsCavityDefinition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingUsesFinalOccupancyForCavityDefinitionTest::RunTest(const FString& Parameters)
{
	auto SelectComponent = [this](
		const TCHAR* const Label,
		const TArray<uint8>& EmptyStates,
		TMap<FIntPoint, FLayoutTerrainColumnRun>& OutRuns,
		bool& bOutUnderground) -> bool
	{
		TArray<FLayoutTerrainColumnProfile> Profiles;
		FString FailureReason;
		if (!FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromEmptySamples(
				FIntVector::ZeroValue, 1, 1, 0, 4, EmptyStates, Profiles, FailureReason))
		{
			AddError(FString::Printf(TEXT("%s profile build failed: %s"), Label, *FailureReason));
			return false;
		}
		return FLayoutTerrainSampling::TrySelectConnectedEmptyComponent(
			Profiles, FIntVector(0, 0, 1), 1, OutRuns, bOutUnderground, FailureReason);
	};

	const TArray<uint8> EnclosedFinalOccupancy = {0, 1, 1, 0, 0};
	TMap<FIntPoint, FLayoutTerrainColumnRun> DomainCutRuns;
	TMap<FIntPoint, FLayoutTerrainColumnRun> BiomeHoleRuns;
	bool bDomainCutUnderground = false;
	bool bBiomeHoleUnderground = false;
	TestTrue(
		TEXT("Domain subtraction final occupancy selects enclosed cavity"),
		SelectComponent(TEXT("DomainCut"), EnclosedFinalOccupancy, DomainCutRuns, bDomainCutUnderground));
	TestTrue(
		TEXT("Biome terrain-hole final occupancy selects enclosed cavity"),
		SelectComponent(TEXT("BiomeHole"), EnclosedFinalOccupancy, BiomeHoleRuns, bBiomeHoleUnderground));
	TestTrue(TEXT("Domain-cut component classifies Underground"), bDomainCutUnderground);
	TestTrue(TEXT("Biome-hole component classifies Underground"), bBiomeHoleUnderground);
	TestEqual(TEXT("Both authoring strategies select same final column count"), DomainCutRuns.Num(), BiomeHoleRuns.Num());
	const FLayoutTerrainColumnRun* const DomainCutRun = DomainCutRuns.Find(FIntPoint::ZeroValue);
	const FLayoutTerrainColumnRun* const BiomeHoleRun = BiomeHoleRuns.Find(FIntPoint::ZeroValue);
	TestTrue(TEXT("Both authoring strategies select same final cavity interval"),
		DomainCutRun != nullptr && BiomeHoleRun != nullptr
			&& DomainCutRun->MinZ == BiomeHoleRun->MinZ
			&& DomainCutRun->MaxZ == BiomeHoleRun->MaxZ);

	TMap<FIntPoint, FLayoutTerrainColumnRun> FilledRuns;
	bool bFilledUnderground = false;
	TestFalse(
		TEXT("Another biome filling domain-cut space prevents cavity classification"),
		SelectComponent(TEXT("OverlapFilled"), {0, 0, 0, 0, 0}, FilledRuns, bFilledUnderground));
	TestFalse(
		TEXT("Terrain hole outside active domain cannot override final solid occupancy"),
		SelectComponent(TEXT("InactiveBiomeHole"), {0, 0, 0, 0, 0}, FilledRuns, bFilledUnderground));

	TMap<FIntPoint, FLayoutTerrainColumnRun> LoadedOverrideRuns;
	bool bLoadedOverrideUnderground = false;
	TestFalse(
		TEXT("Loaded solid override closes generated cavity"),
		SelectComponent(TEXT("LoadedSolidOverride"), {0, 0, 0, 0, 0}, LoadedOverrideRuns, bLoadedOverrideUnderground));
	TestTrue(
		TEXT("Loaded empty override opens generated solid column"),
		SelectComponent(TEXT("LoadedEmptyOverride"), EnclosedFinalOccupancy, LoadedOverrideRuns, bLoadedOverrideUnderground));
	TestTrue(TEXT("Loaded empty override classifies enclosed component Underground"), bLoadedOverrideUnderground);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingPreparesCallerIndependentSelectedSiteEvidenceTest,
	"PorismExtension.Layout.Terrain.ColumnProfiles.PreparesCallerIndependentSelectedSiteEvidence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingPreparesCallerIndependentSelectedSiteEvidenceTest::RunTest(const FString& Parameters)
{
	const TArray<int> Materials = {7, -1, -1, 7, 7};
	const TArray<int> BiomeIndices = {3, -1, -1, 3, 3};
	FLayoutFrozenTerrainBiomeAdapterInput ExplicitArtifact;
	ExplicitArtifact.ArtifactId = TEXT("ExplicitFixture");
	FLayoutFrozenTerrainBiomeAdapterInput PlanningArtifact;
	PlanningArtifact.ArtifactId = TEXT("PlanningFixture");
	FString FailureReason;
	TestTrue(
		TEXT("Explicit-style selected-site preparation succeeds"),
		FLayoutTerrainSampling::TryPrepareSelectedSiteTerrainFromMaterialSamples(
			FIntVector::ZeroValue, 1, 1, 0, 4, FIntVector(0, 0, 1), FIntVector(1, 1, 1), 1,
			FLayoutWorldBindingTerrainTransitionPolicy(), -1,
			Materials, BiomeIndices, ExplicitArtifact, FailureReason));
	TestTrue(
		TEXT("Planning-style selected-site preparation succeeds"),
		FLayoutTerrainSampling::TryPrepareSelectedSiteTerrainFromMaterialSamples(
			FIntVector::ZeroValue, 1, 1, 0, 4, FIntVector(0, 0, 1), FIntVector(1, 1, 1), 1,
			FLayoutWorldBindingTerrainTransitionPolicy(), -1,
			Materials, BiomeIndices, PlanningArtifact, FailureReason));
	TestTrue(TEXT("Both callers classify same Underground component"),
		ExplicitArtifact.bHasRelativeEnvironmentClassification
			&& PlanningArtifact.bHasRelativeEnvironmentClassification
			&& ExplicitArtifact.bIsClassifiedUnderground
			&& PlanningArtifact.bIsClassifiedUnderground);
	TestEqual(TEXT("Both callers freeze same interval count"),
		ExplicitArtifact.PocketVoidIntervals.Num(), PlanningArtifact.PocketVoidIntervals.Num());
	if (ExplicitArtifact.PocketVoidIntervals.Num() == 1 && PlanningArtifact.PocketVoidIntervals.Num() == 1)
	{
		const FLayoutFrozenTerrainVoidIntervalSample& ExplicitInterval = ExplicitArtifact.PocketVoidIntervals[0];
		const FLayoutFrozenTerrainVoidIntervalSample& PlanningInterval = PlanningArtifact.PocketVoidIntervals[0];
		TestEqual(TEXT("Both callers freeze same cavity start"), ExplicitInterval.MinZ, PlanningInterval.MinZ);
		TestEqual(TEXT("Both callers freeze same cavity end"), ExplicitInterval.MaxZ, PlanningInterval.MaxZ);
		TestEqual(TEXT("Both callers freeze same floor biome owner"), ExplicitInterval.FloorBiomeIndex, PlanningInterval.FloorBiomeIndex);
		TestEqual(TEXT("Final occupancy floor owner is preserved"), ExplicitInterval.FloorBiomeIndex, 3);
	}
	TestEqual(TEXT("Shared preparation preserves explicit artifact identity"), ExplicitArtifact.ArtifactId, FLayoutId(TEXT("ExplicitFixture")));
	TestEqual(TEXT("Shared preparation preserves planning artifact identity"), PlanningArtifact.ArtifactId, FLayoutId(TEXT("PlanningFixture")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsSteppedSupportFromVoidIntervalsTest,
	"PorismExtension.Layout.Terrain.CavitySupport.BuildsFromVoidIntervals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsSteppedSupportFromVoidIntervalsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector::ZeroValue, ELayoutCellIntent::Interior});
	FLayoutFrozenTerrainVoidIntervalSample Interval;
	Interval.BlockXY = FIntPoint(8, 8);
	Interval.MinZ = 20;
	Interval.MaxZ = 30;
	Interval.bHasVoidEvidence = true;
	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Cavity floor becomes frozen stepped support"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMapFromVoidIntervals(
			FIntVector::ZeroValue, FIntVector(16, 16, 16), 16, PlannedCells, {Interval}, SupportMap, FailureReason));
	TestEqual(TEXT("One cavity support sample emitted"), SupportMap.SupportSamples.Num(), 1);
	if (SupportMap.SupportSamples.Num() == 1)
	{
		TestEqual(TEXT("Support surface is solid block below cavity"), SupportMap.SupportSamples[0].SupportSurfaceZ, 19);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingUndergroundAugmentationPreservesCanonicalArtifactTest,
	"PorismExtension.Layout.Terrain.CavitySupport.AugmentationPreservesCanonicalArtifact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingUndergroundAugmentationPreservesCanonicalArtifactTest::RunTest(const FString& Parameters)
{
	FLayoutFrozenTerrainBiomeAdapterInput Artifact;
	Artifact.ArtifactId = TEXT("Canonical.SelectedComponent");
	Artifact.bHasRelativeEnvironmentClassification = true;
	Artifact.bIsClassifiedUnderground = true;
	Artifact.bHasTerrainPlacementEvidence = true;
	Artifact.bHasBiomeOwnershipEvidence = true;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutTerrainPlacementCellEvidence& Cell = Artifact.TerrainPlacementCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.bPlaceableForSelectedMode = true;
			Cell.ProvenanceId = TEXT("Canonical.SelectedComponent.Cell");
			FLayoutFrozenTerrainVoidIntervalSample& Interval = Artifact.PocketVoidIntervals.AddDefaulted_GetRef();
			Interval.BlockXY = FIntPoint(8 + X * 16, 8 + Y * 16);
			Interval.MinZ = 20;
			Interval.MaxZ = 50;
			Interval.bHasVoidEvidence = true;
			Interval.bHasFloorBiomeIndex = true;
			Interval.FloorBiomeIndex = 0;
		}
	}
	FLayoutFrozenBiomeOwnershipSample& Ownership = Artifact.BiomeOwnershipSamples.AddDefaulted_GetRef();
	Ownership.BlockXY = FIntPoint(8, 8);
	Ownership.OwningBiomeRowName = TEXT("CanonicalFloorOwner");
	Ownership.bOwnedByAllowList = true;
	Ownership.bHasSurfaceEvidence = true;
	Artifact.bHasPocketVoidIntervalEvidence = true;

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	const bool bAugmented = FLayoutTerrainSampling::TryAugmentSelectedComponentWithSteppedTerrainEvidence(
		true,
		FIntVector::ZeroValue,
		FIntVector(16, 16, 16),
		FIntPoint(3, 3),
		FLayoutWorldBindingTerrainTransitionPolicy(),
		SupportMap,
		Artifact,
		FailureReason);
	TestTrue(FString::Printf(TEXT("Underground canonical augmentation succeeds: %s"), *FailureReason), bAugmented);
	if (!bAugmented)
	{
		return false;
	}
	TestTrue(TEXT("Canonical placement evidence survives shared augmentation"),
		Artifact.TerrainPlacementCells.ContainsByPredicate([](const FLayoutTerrainPlacementCellEvidence& Cell)
		{
			return Cell.ProvenanceId == FLayoutId(TEXT("Canonical.SelectedComponent.Cell"));
		}));
	TestTrue(TEXT("Canonical final-occupancy biome owner survives shared augmentation"),
		Artifact.BiomeOwnershipSamples.ContainsByPredicate([](const FLayoutFrozenBiomeOwnershipSample& Sample)
		{
			return Sample.OwningBiomeRowName == FLayoutId(TEXT("CanonicalFloorOwner"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSamplingBuildsColumnProfilesFromActiveBiomeSamplerTest,
	"PorismExtension.Layout.Terrain.ColumnProfiles.BuildsFromActiveBiomeSampler",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSamplingBuildsColumnProfilesFromActiveBiomeSamplerTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("ColumnProfileBiome");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler initializes for bounded generated profile"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));
	TArray<FLayoutTerrainColumnProfile> Profiles;
	FString FailureReason;
	TestTrue(
		TEXT("Generated sampler emits one bounded profile without a surface search"),
		FLayoutTerrainSampling::TryBuildTerrainColumnProfilesFromActiveBiomeSampler(
			FIntVector(60, 70, 0), 1, 1, 0, 3,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			Profiles,
			FailureReason));
	TestEqual(TEXT("Generated profile covers requested column"), Profiles.Num(), 1);
	if (Profiles.Num() == 1)
	{
		TestEqual(TEXT("Generated profile starts at requested min Z"), Profiles[0].Runs[0].MinZ, 0);
		TestEqual(TEXT("Generated profile ends at requested max Z"), Profiles[0].Runs.Last().MaxZ, 3);
	}
	return true;
}

bool FLayoutTerrainSamplingRejectsSteppedTerrainSupportMapWithoutAlignedSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutTerrainAnchorResult AnchorResult;
	AnchorResult.bIsValid = true;

	FLayoutTerrainSurfaceSample& OnlySample = AnchorResult.Samples.AddDefaulted_GetRef();
	OnlySample.bIsValid = true;
	OnlySample.BlockXY = FIntPoint(100, 200);
	OnlySample.SurfaceBlockWorldPos = FIntVector(100, 200, 10);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestFalse(
		TEXT("Support-map compilation rejects when one planned cell has no aligned terrain sample"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMap(
			FIntVector(100, 200, 0),
			FIntVector(16, 16, 16),
			16,
			PlannedCells,
			AnchorResult,
			SupportMap,
			FailureReason));
	TestTrue(TEXT("Support-map compilation reports the missing aligned sample"), FailureReason.Contains(TEXT("missing an aligned terrain sample")));

	FailureReason.Reset();
	TestFalse(
		TEXT("Support-map compilation rejects when terrain sample spacing does not align with the shared cell size"),
		FLayoutTerrainSampling::TryBuildSteppedTerrainSupportMap(
			FIntVector(100, 200, 0),
			FIntVector(16, 16, 16),
			8,
			PlannedCells,
			AnchorResult,
			SupportMap,
			FailureReason));
	TestTrue(TEXT("Support-map compilation reports the spacing mismatch"), FailureReason.Contains(TEXT("requires terrain sample spacing")));
	return true;
}
