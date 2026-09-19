// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"

namespace
{
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

	void ConfigureOwningRow(FBiomeDualData& Row)
	{
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
	}

	FLayoutNoiseCoordinateSettings MakeTestCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerSamplesInlineWorldBiomeRowsTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.SamplesInlineWorldBiomeRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerSamplesInlineWorldBiomeRowsTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("InlineBiome");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenA = ConstantPositiveFastNoise;
	ConfigureOwningRow(Row);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles inline WorldBiomes rows"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 17));

	FLayoutActiveBiomeSample Sample;
	TestTrue(TEXT("Sampler evaluates one active biome sample"), Sampler.SampleAtBlockPosition(
		FIntVector::ZeroValue,
		MakeTestCoordinateSettings(WorldGenDef),
		Sample));
	TestTrue(TEXT("Inline positive domain owns the sample"), Sample.bAnyPositiveDomain);
	TestEqual(TEXT("Inline row can be identified by biome name"), Sample.WinningRow.RowName, FName(TEXT("InlineBiome")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerAppliesGlobalWorldGenerationTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.AppliesGlobalWorldGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerAppliesGlobalWorldGenerationTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	WorldGenDef->WorldGenRun = nullptr;
	WorldGenDef->WorldGen = ConstantPositiveFastNoise;

	FBiomeDualData Row;
	Row.BiomeName = TEXT("LayoutBiome");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	ConfigureOwningRow(Row);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles global and biome shape nodes"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	FLayoutActiveBiomeSample Sample;
	TestTrue(TEXT("Complete Shape-stage sample succeeds"), Sampler.SampleAtBlockPosition(
		FIntVector::ZeroValue,
		MakeTestCoordinateSettings(WorldGenDef),
		Sample));
	TestEqual(TEXT("Global WorldGen contribution is included in terrain density"), Sample.TerrainValue, 1.0f);
	TestFalse(TEXT("Positive final Shape density is air"), Sample.bTerrainSolid);
	TestEqual(TEXT("Packed biome ownership remains independent from final air/solid density"), Sample.WinningRow.RowName, FName(TEXT("LayoutBiome")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerUsesNativeDomainFallbackTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.UsesNativeDomainFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerUsesNativeDomainFallbackTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData UnboundedRow;
	UnboundedRow.BiomeName = TEXT("Menu");
	UnboundedRow.DualSwitch = ConstantPositiveFastNoise;
	UnboundedRow.GenA = ConstantPositiveFastNoise;
	ConfigureOwningRow(UnboundedRow);
	WorldGenDef->WorldBiomes.Add(UnboundedRow);

	FBiomeDualData DomainBoundRow;
	DomainBoundRow.BiomeName = TEXT("CavityBiome");
	DomainBoundRow.Domain = ConstantPositiveFastNoise;
	DomainBoundRow.DualSwitch = ConstantPositiveFastNoise;
	DomainBoundRow.GenA = ConstantPositiveFastNoise;
	ConfigureOwningRow(DomainBoundRow);
	WorldGenDef->WorldBiomes.Add(DomainBoundRow);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles rows using Porism node fallbacks"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));
	FLayoutActiveBiomeSample Sample;
	TestTrue(TEXT("Sampler evaluates domain-bounded biome"), Sampler.SampleAtBlockPosition(
		FIntVector::ZeroValue,
		MakeTestCoordinateSettings(WorldGenDef),
		Sample));
	TestEqual(TEXT("Missing Domain uses the same constant-positive fallback as native generation"), Sample.WinningRow.RowName, FName(TEXT("Menu")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerSamplesCompatibleDomainWithoutMenuMaskTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.SamplesCompatibleDomainWithoutMenuMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerSamplesCompatibleDomainWithoutMenuMaskTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Negative Domain inside DomainOver contributes beyond current Menu power"),
		FLayoutActiveBiomeSampler::DoesDomainContribute(-29.5565f, 32.0f, 1.0f));
	TestFalse(TEXT("Negative Domain outside DomainOver does not contribute"),
		FLayoutActiveBiomeSampler::DoesDomainContribute(-29.5565f, 30.0f, 1.0f));

	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	for (const TCHAR* const Name : {TEXT("Menu"), TEXT("TemplateTester")})
	{
		FBiomeDualData Row;
		Row.BiomeName = Name;
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenA = ConstantPositiveFastNoise;
		ConfigureOwningRow(Row);
		if (Row.BiomeName == TEXT("Menu"))
		{
			Row.Domain.Reset();
			Row.DomainRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		}
		WorldGenDef->WorldBiomes.Add(Row);
	}

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles overlapping global and compatible rows"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));
	FLayoutActiveBiomeSample Sample;
	TestTrue(TEXT("Compatible source sample completes"), Sampler.SampleAtBlockPositionFromAnyRow(
		FIntVector::ZeroValue,
		MakeTestCoordinateSettings(WorldGenDef),
		{TEXT("TemplateTester")},
		Sample));
	TestEqual(TEXT("Global Menu cannot mask compatible source query"), Sample.WinningRow.RowName, FName(TEXT("TemplateTester")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerPreservesDataTableRowNameTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.PreservesDataTableRowName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerPreservesDataTableRowNameTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	UDataTable* const BiomeTable = NewObject<UDataTable>(WorldGenDef);
	BiomeTable->RowStruct = FBiomeDualData::StaticStruct();

	FBiomeDualData Row;
	Row.BiomeName = TEXT("DisplayNameCanDiffer");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenA = ConstantPositiveFastNoise;
	ConfigureOwningRow(Row);
	BiomeTable->AddRow(TEXT("Biome.Reservation.Center"), Row);
	WorldGenDef->WorldBiomesDT = BiomeTable;

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles DataTable WorldBiomes rows"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 23));
	TestTrue(TEXT("Screening accepts configured row name"), Sampler.HasMatchingRow(TEXT("Biome.Reservation.Center")));
	TestTrue(TEXT("Screening preserves display-name aliases"), Sampler.HasMatchingRow(TEXT("DisplayNameCanDiffer")));
	TestFalse(TEXT("Missing row proves binding irrelevant"), Sampler.HasMatchingRow(TEXT("MissingRow")));

	FLayoutActiveBiomeSample Sample;
	TestTrue(TEXT("Sampler evaluates one DataTable-backed active biome sample"), Sampler.SampleAtBlockPosition(
		FIntVector::ZeroValue,
		MakeTestCoordinateSettings(WorldGenDef),
		Sample));
	TestEqual(TEXT("DataTable row name is preserved for planning bindings"), Sample.WinningRow.RowName, FName(TEXT("Biome.Reservation.Center")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerFindsRuntimeRowReservationSurfaceTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.FindsRuntimeRowReservationSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerFindsTopSurfaceWhenSearchStartsInsideRuntimeRowSolidTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.FindsTopSurfaceWhenSearchStartsInsideRuntimeRowSolid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerFindsSurfaceFromAnyEligibleRuntimeRowNameTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.FindsSurfaceFromAnyEligibleRuntimeRowName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerFindsRuntimeRowReservationSurfaceTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	ConfigureOwningRow(Row);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles active biome rows with runtime GenA nodes"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	const int32 SearchStartZBlockWorld = 10;
	const int32 SearchDepthBlocks = 40;
	const int32 ExpectedRecoveredSurfaceZ = SearchStartZBlockWorld + SearchDepthBlocks - 1;
	FLayoutActiveBiomeSurfaceSample Surface;
	TestTrue(TEXT("Surface query completes against runtime-node reservation biome"), Sampler.FindEligibleBiomeSurface(
		TEXT("Reservation"),
		FIntPoint::ZeroValue,
		SearchStartZBlockWorld,
		SearchDepthBlocks,
		MakeTestCoordinateSettings(WorldGenDef),
		Surface));
	TestTrue(TEXT("Reservation biome has a discovered top solid surface"), Surface.bIsValid);
	TestEqual(TEXT("Top-down search recovers the highest owned solid block reachable within the search budget"), Surface.SurfaceBlockWorldPos.Z, ExpectedRecoveredSurfaceZ);
	TestTrue(TEXT("Surface sample belongs to the requested reservation biome"), Surface.BiomeSample.WinningRow.RowName == FLayoutId(TEXT("Reservation")));

	FLayoutSolveCancellationSource Cancellation;
	const FLayoutSolveCancellationToken Token = Cancellation.CreateToken();
	LayoutSolveCancellation::FThreadTokenScope Scope(Token);
	Cancellation.Cancel();
	TestFalse(TEXT("Canceled location work stops inside shared column search"), Sampler.FindAnyActiveBiomeSurface(
		FIntPoint::ZeroValue, SearchStartZBlockWorld, SearchDepthBlocks, MakeTestCoordinateSettings(WorldGenDef), Surface));
	TestFalse(TEXT("Canceled search cannot reuse preceding valid surface"), Surface.bIsValid);

	return true;
}

bool FLayoutActiveBiomeSamplerFindsTopSurfaceWhenSearchStartsInsideRuntimeRowSolidTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	ConfigureOwningRow(Row);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles runtime-node rows for inside-solid surface recovery"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	const int32 SearchStartZBlockWorld = 5;
	const int32 SearchDepthBlocks = 40;
	const int32 ExpectedRecoveredSurfaceZ = SearchStartZBlockWorld + SearchDepthBlocks - 1;
	FLayoutActiveBiomeSurfaceSample Surface;
	TestTrue(TEXT("Surface query completes when the search starts inside the runtime-node solid body"), Sampler.FindEligibleBiomeSurface(
		TEXT("Reservation"),
		FIntPoint::ZeroValue,
		SearchStartZBlockWorld,
		SearchDepthBlocks,
		MakeTestCoordinateSettings(WorldGenDef),
		Surface));
	TestTrue(TEXT("Inside-solid surface recovery still finds a valid owned surface"), Surface.bIsValid);
	TestEqual(TEXT("Inside-solid surface recovery returns the highest owned solid block reachable within the search budget instead of the starting Z"), Surface.SurfaceBlockWorldPos.Z, ExpectedRecoveredSurfaceZ);
	TestTrue(TEXT("Inside-solid surface recovery preserves the requested runtime row"), Surface.BiomeSample.WinningRow.RowName == FLayoutId(TEXT("Reservation")));

	return true;
}

bool FLayoutActiveBiomeSamplerFindsSurfaceFromAnyEligibleRuntimeRowNameTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("ReservationA");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	ConfigureOwningRow(Row);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles active biome rows for multi-row eligibility"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	const TArray<FName> EligibleRows = {TEXT("ReservationB"), TEXT("ReservationA")};
	FLayoutActiveBiomeSurfaceSample EligibleSurface;
	TestTrue(TEXT("Multi-row surface query completes when one eligible runtime row owns the surface"), Sampler.FindEligibleBiomeSurfaceFromAnyRow(
		EligibleRows,
		FIntPoint::ZeroValue,
		10,
		40,
		MakeTestCoordinateSettings(WorldGenDef),
		EligibleSurface));
	TestTrue(TEXT("Multi-row surface query returns the solid surface owned by one eligible row"), EligibleSurface.bIsValid);
	TestEqual(TEXT("Multi-row surface query preserves the winning eligible row"), EligibleSurface.BiomeSample.WinningRow.RowName, FName(TEXT("ReservationA")));

	const TArray<FName> IneligibleRows = {TEXT("ReservationB"), TEXT("ReservationC")};
	FLayoutActiveBiomeSurfaceSample IneligibleSurface;
	TestTrue(TEXT("Multi-row surface query still completes when no eligible row owns the surface"), Sampler.FindEligibleBiomeSurfaceFromAnyRow(
		IneligibleRows,
		FIntPoint::ZeroValue,
		10,
		40,
		MakeTestCoordinateSettings(WorldGenDef),
		IneligibleSurface));
	TestFalse(TEXT("Multi-row surface query rejects solid surfaces owned only by unlisted rows"), IneligibleSurface.bIsValid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutActiveBiomeSamplerBuildsEligiblePocketGridFromRuntimeRowSurfaceTest,
	"PorismExtension.Layout.Planning.ActiveBiomeSampling.BuildsEligiblePocketGridFromRuntimeRowSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutActiveBiomeSamplerBuildsEligiblePocketGridFromRuntimeRowSurfaceTest::RunTest(const FString& Parameters)
{
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(GetTransientPackage());
	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	ConfigureOwningRow(Row);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Sampler compiles runtime-node rows for grid sampling"), Sampler.Initialize(GetTransientPackage(), WorldGenDef, 0));

	const int32 SearchStartZBlockWorld = 10;
	const int32 SearchDepthBlocks = 40;
	const int32 ExpectedRecoveredSurfaceZ = SearchStartZBlockWorld + SearchDepthBlocks - 1;
	TArray<FLayoutReservationPocketSample> Samples;
	TMap<FIntPoint, int32> SurfaceZBySampleGrid;
	TMap<FIntPoint, int32> SurfaceZByBlockXY;
	TestTrue(TEXT("Sampler builds a planning pocket grid from active reservation ownership"), Sampler.SampleEligibleBiomeSurfaceGrid(
		TEXT("Reservation"),
		FIntPoint(-20, -20),
		FIntPoint(20, 20),
		20,
		SearchStartZBlockWorld,
		SearchDepthBlocks,
		MakeTestCoordinateSettings(WorldGenDef),
		Samples,
		&SurfaceZBySampleGrid,
		&SurfaceZByBlockXY));
	TestEqual(TEXT("Grid has nine samples"), Samples.Num(), 9);
	TestTrue(TEXT("Center sample is eligible from the active reservation biome"), Samples.ContainsByPredicate([](const FLayoutReservationPocketSample& Sample)
	{
		return Sample.SampleGridXY == FIntPoint(1, 1) && Sample.NoiseValue >= 0.0f;
	}));
	TestTrue(TEXT("Eligible center sample preserves the highest reachable discovered surface Z on the frozen pocket sample"), Samples.ContainsByPredicate([ExpectedRecoveredSurfaceZ](const FLayoutReservationPocketSample& Sample)
	{
		return Sample.SampleGridXY == FIntPoint(1, 1) && Sample.bHasSurfaceZ && Sample.SurfaceZBlockWorld == ExpectedRecoveredSurfaceZ;
	}));
	TestTrue(TEXT("Eligible center sample records its surface Z"), SurfaceZBySampleGrid.Contains(FIntPoint(1, 1)));
	TestTrue(TEXT("Eligible center sample also records its surface Z by real block XY"), SurfaceZByBlockXY.Contains(FIntPoint(0, 0)));
	TestEqual(TEXT("Surface Z by block XY preserves the same highest reachable owned solid block"), SurfaceZByBlockXY[FIntPoint(0, 0)], ExpectedRecoveredSurfaceZ);

	return true;
}
