// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBuildsFoundationDomainTest,
	"PorismExtension.Biome.IslandNoise.BuildsFoundationDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBuildsFoundationDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();

	const FNodeLink Domain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy);

	TestTrue(TEXT("Foundation domain is valid"), Domain.Node != nullptr);
	TestTrue(TEXT("Foundation domain carves reservation domains"), Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("MultiplyFloat-1")));
	TestFalse(TEXT("Foundation domain omits simplex detail by default"), Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Foundation domain omits fractal detail by default"), Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("FractalFBm")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseFoundationDomainKeepsTopFaceAirOwnedWithZeroPaddingTest,
	"PorismExtension.Biome.IslandNoise.FoundationDomainKeepsTopFaceAirOwnedWithZeroPadding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseFoundationDomainKeepsTopFaceAirOwnedWithZeroPaddingTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>();
	TestNotNull(TEXT("Test strategy has an island payload"), IslandPayload);
	if (IslandPayload == nullptr)
	{
		return false;
	}

	IslandPayload->DomainVerticalPadding = 0.0f;
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> DomainNodes;
	UFastNoiseEditor* const DomainEditor = CreateTestFastNoiseEditor(DomainNodes);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(DomainEditor, Strategy, &ScaleContext);

	std::vector<FNodeLink> GenANodes;
	UFastNoiseEditor* const GenAEditor = CreateTestFastNoiseEditor(GenANodes);
	const FNodeLink FoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(GenAEditor, Strategy, &ScaleContext);

	float DomainAtTopFaceAirCell = 0.0f;
	float DomainAboveClearance = 0.0f;
	float GenAAtTopFaceAirCell = 0.0f;
	float GenAAtTopSolidCell = 0.0f;
	TestTrue(TEXT("Foundation domain samples the visible top-face air cell"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, 0.0f), DomainAtTopFaceAirCell));
	TestTrue(TEXT("Foundation domain samples above minimum top clearance"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, 1.0f), DomainAboveClearance));
	TestTrue(TEXT("Foundation GenA samples the visible top-face air cell"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0f, 0.0f, 0.0f), GenAAtTopFaceAirCell));
	TestTrue(TEXT("Foundation GenA samples the top solid cell"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0f, 0.0f, -1.0f), GenAAtTopSolidCell));

	TestTrue(TEXT("Domain owns the air cell at the visible top face even when authored vertical padding is zero"), DomainAtTopFaceAirCell > 0.0f);
	TestTrue(TEXT("Domain does not grow beyond the one-block minimum clearance when authored vertical padding is zero"), DomainAboveClearance <= 0.0f);
	TestTrue(TEXT("Visible top face remains air while domain owns that cell"), GenAAtTopFaceAirCell > 0.0f);
	TestTrue(TEXT("Top solid terrain sample remains one block below the visible top face"), GenAAtTopSolidCell < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSlotWrapperMatchesDirectBuilderTest,
	"PorismExtension.Biome.IslandNoise.BiomeSlotWrapperMatchesDirectBuilder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSlotWrapperMatchesDirectBuilderTest::RunTest(const FString& Parameters)
{
	TestBiomeSlotWrapperMatchesDirectBuilder(*this, EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag(), TEXT("Foundation DomainNoise"));
	TestBiomeSlotWrapperMatchesDirectBuilder(*this, EBiomeNoiseSlot::GenA, TestFoundationBiomeTag(), TEXT("Foundation GenA"));
	TestBiomeSlotWrapperMatchesDirectBuilder(*this, EBiomeNoiseSlot::DomainNoise, TestReservationBiomeTag(), TEXT("Reservation DomainNoise"));
	TestBiomeSlotWrapperMatchesDirectBuilder(*this, EBiomeNoiseSlot::GenA, TestReservationBiomeTag(), TEXT("Reservation GenA"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceTerrainPayloadsAreNotReservationPayloadsTest,
	"PorismExtension.Biome.IslandNoise.SurfaceTerrainPayloadsAreNotReservationPayloads",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceTerrainPayloadsAreNotReservationPayloadsTest::RunTest(const FString& Parameters)
{
	TestTrue(
		TEXT("Surface anchor reservation payload remains selectable as a reservation payload"),
		TIsDerivedFrom<FSurfaceAnchorReservationPayload, FReservationPayloadBase>::Value);
	TestFalse(
		TEXT("Flat terrain payload is not selectable as a reservation payload"),
		TIsDerivedFrom<FSurfaceAnchorFlatTerrainPayload, FReservationPayloadBase>::Value);
	TestFalse(
		TEXT("Blended support terrain payload is not selectable as a reservation payload"),
		TIsDerivedFrom<FSurfaceAnchorBlendedSupportTerrainPayload, FReservationPayloadBase>::Value);
	TestFalse(
		TEXT("Noisy terrain payload is not selectable as a reservation payload"),
		TIsDerivedFrom<FSurfaceAnchorNoisyTerrainPayload, FReservationPayloadBase>::Value);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseFoundationGenACalibratesSurfaceAndBoundsTest,
	"PorismExtension.Biome.IslandNoise.FoundationGenACalibratesSurfaceAndBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseFoundationGenACalibratesSurfaceAndBoundsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink FoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float AtSurface = 0.0f;
	float JustBelowSurface = 0.0f;
	float JustAboveSurface = 0.0f;
	float AtBottom = 0.0f;
	float JustAboveBottom = 0.0f;
	float JustBelowBottom = 0.0f;

	TestTrue(TEXT("Foundation GenA samples at authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, 0.0), AtSurface));
	TestTrue(TEXT("Foundation GenA samples below authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, -1.0), JustBelowSurface));
	TestTrue(TEXT("Foundation GenA samples above authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, 1.0), JustAboveSurface));
	TestTrue(TEXT("Foundation GenA samples at authored bottom"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, -125.0), AtBottom));
	TestTrue(TEXT("Foundation GenA samples above authored bottom"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, -124.0), JustAboveBottom));
	TestTrue(TEXT("Foundation GenA samples below authored bottom"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, -126.0), JustBelowBottom));

	TestTrue(TEXT("Authored surface is air-biased so Center.Z is the visible top face"), AtSurface > 0.0f && FMath::Abs(AtSurface) < 0.1f);
	TestTrue(TEXT("Foundation is solid below authored surface"), JustBelowSurface < 0.0f);
	TestTrue(TEXT("Foundation is air above authored surface"), JustAboveSurface > 0.0f);
	TestTrue(TEXT("Authored bottom is air-biased at the finite boundary"), AtBottom > 0.0f && FMath::Abs(AtBottom) < 0.1f);
	TestTrue(TEXT("Foundation is solid just above authored bottom"), JustAboveBottom < 0.0f);
	TestTrue(TEXT("Foundation is air just below authored bottom"), JustBelowBottom > 0.0f);
	TestTrue(TEXT("Configured island bottom stays inside finite Z range"), -125.0f >= ScaleContext.AuthoredMinBlock.Z);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseFoundationGenAUsesInternalDensityCalibrationTest,
	"PorismExtension.Biome.IslandNoise.FoundationGenAUsesInternalDensityCalibration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseFoundationGenAUsesInternalDensityCalibrationTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink FoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float AtSurface = 0.0f;
	float OneBlockBelow = 0.0f;
	float OneBlockAbove = 0.0f;
	float DeepInterior = 0.0f;
	TestTrue(TEXT("Foundation GenA samples at authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, 0.0), AtSurface));
	TestTrue(TEXT("Foundation GenA samples one block below authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, -1.0), OneBlockBelow));
	TestTrue(TEXT("Foundation GenA samples one block above authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, 1.0), OneBlockAbove));
	TestTrue(TEXT("Foundation GenA samples deep island interior"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(0.0, 0.0, -60.0), DeepInterior));

	TestTrue(TEXT("Surface is biased slightly toward air so authored Z is the visible top face"), FMath::IsNearlyEqual(AtSurface, 0.07f, 0.015f));
	TestTrue(TEXT("One block below the visible top face is solid"), OneBlockBelow < 0.0f);
	TestTrue(TEXT("One block above is air despite surface bias"), OneBlockAbove > 0.05f);
	TestTrue(TEXT("One authored block changes density strongly enough for Porism terrain writes"), OneBlockAbove - OneBlockBelow > 0.1f);
	TestTrue(TEXT("Deep island interior is strongly solid enough to overcome global additive fields"), DeepInterior < -1.0f);

	return true;
}

