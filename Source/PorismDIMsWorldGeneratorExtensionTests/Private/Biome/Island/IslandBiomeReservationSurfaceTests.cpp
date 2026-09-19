// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceAnchorSamplesShallowDomainTest,
	"PorismExtension.Biome.IslandNoise.SurfaceAnchorSamplesShallowDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceAnchorSamplesShallowDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateOffsetSurfaceAnchorIslandStrategy();

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink BaseEnvelopeDomain = UIslandBiomeFastNoiseLibrary::BuildIslandEnvelopeDomain(Editor, Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	float ReservationAtSurface = 0.0f;
	float ReservationAbove = 0.0f;
	float ReservationDeepBelow = 0.0f;
	float BaseEnvelopeDeepBelow = 0.0f;
	float FoundationAtSurface = 0.0f;
	float FoundationDeepBelow = 0.0f;
	TestTrue(TEXT("Reservation domain samples at intended authored-block surface"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, -12.0), ReservationAtSurface));
	TestTrue(TEXT("Reservation domain samples above shallow authored-block vertical bounds"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 5.0), ReservationAbove));
	TestTrue(TEXT("Reservation domain samples below shallow authored-block vertical bounds"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, -70.0), ReservationDeepBelow));
	TestTrue(TEXT("Base envelope samples below anchor volume in authored blocks"), EvaluateNoiseAtAuthoredBlock(BaseEnvelopeDomain, ScaleContext, FVector(0.0, 0.0, -70.0), BaseEnvelopeDeepBelow));
	TestTrue(TEXT("Foundation domain samples at anchor surface in authored blocks"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0, 0.0, -12.0), FoundationAtSurface));
	TestTrue(TEXT("Foundation domain samples below anchor volume in authored blocks"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0, 0.0, -70.0), FoundationDeepBelow));

	TestTrue(TEXT("Surface anchor domain is positive at configured terrain surface"), ReservationAtSurface > 0.0f);
	TestTrue(TEXT("Surface anchor domain is negative above its shallow vertical bounds"), ReservationAbove < 0.0f);
	TestTrue(TEXT("Surface anchor domain is negative below its shallow vertical bounds"), ReservationDeepBelow < 0.0f);
	TestTrue(TEXT("Foundation domain is carved at the anchor surface"), FoundationAtSurface <= 0.0f);
	TestTrue(TEXT("Foundation domain remains positive below the shallow anchor"), FoundationDeepBelow > 0.0f);
	TestTrue(TEXT("Foundation carve does not remove the underside below the shallow anchor"), BaseEnvelopeDeepBelow > 0.0f && ReservationDeepBelow < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceAnchorAlignsCenterMenuSurfaceTest,
	"PorismExtension.Biome.IslandNoise.SurfaceAnchorAlignsCenterMenuSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceAnchorAlignsCenterMenuSurfaceTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateZeroSurfaceMenuIslandStrategy();

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	float DomainAtSurface = 0.0f;
	float DomainAtDefaultDepth = 0.0f;
	float DomainAtDefaultClearance = 0.0f;
	float DomainAbove = 0.0f;
	float GenAAtSurface = 0.0f;
	float GenAOneBlockBelowSurface = 0.0f;
	float GenABelow = 0.0f;
	float GenAAbove = 0.0f;
	float GenASameHeightAwayFromCenter = 0.0f;
	TestTrue(TEXT("Reservation domain samples at centered authored-block menu surface"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 0.0), DomainAtSurface));
	TestTrue(TEXT("Reservation domain samples at configured default depth below surface"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, -18.0), DomainAtDefaultDepth));
	TestTrue(TEXT("Reservation domain samples at configured default clearance above surface"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 35.0), DomainAtDefaultClearance));
	TestTrue(TEXT("Reservation domain samples above centered authored-block menu surface"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 40.0), DomainAbove));
	TestTrue(TEXT("Reservation GenA samples at centered authored-block menu surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 0.0), GenAAtSurface));
	TestTrue(TEXT("Reservation GenA samples one block below centered authored-block menu surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, -1.0), GenAOneBlockBelowSurface));
	TestTrue(TEXT("Reservation GenA samples below centered authored-block menu surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, -5.0), GenABelow));
	TestTrue(TEXT("Reservation GenA samples above centered authored-block menu surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 5.0), GenAAbove));
	TestTrue(TEXT("Reservation GenA samples same height away from center"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(20.0, 0.0, 0.0), GenASameHeightAwayFromCenter));

	TestTrue(TEXT("Surface anchor domain is positive at authored-block Z zero when the island top is authored at zero"), DomainAtSurface > 0.0f);
	TestTrue(TEXT("Surface anchor domain includes its configured default depth"), DomainAtDefaultDepth > 0.0f);
	TestTrue(TEXT("Surface anchor domain includes its configured default structure clearance"), DomainAtDefaultClearance > 0.0f);
	TestTrue(TEXT("Surface anchor domain is negative above its structure clearance height"), DomainAbove < 0.0f);
	TestTrue(TEXT("Reservation GenA treats the solved surface as the visible top boundary"), GenAAtSurface > 0.0f);
	TestTrue(TEXT("Reservation GenA is solid one block below the centered surface"), GenAOneBlockBelowSurface < 0.0f);
	TestTrue(TEXT("Reservation GenA is solid below the centered surface"), GenABelow < 0.0f);
	TestTrue(TEXT("Reservation GenA is air above the centered surface"), GenAAbove > 0.0f);
	TestTrue(TEXT("Reservation GenA is exactly flat across XY by default"), FMath::IsNearlyEqual(GenAAtSurface, GenASameHeightAwayFromCenter, 0.001f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBiomeGenAOptionalNoiseIsExplicitTest,
	"PorismExtension.Biome.IslandNoise.BiomeGenAOptionalNoiseIsExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBiomeGenAOptionalNoiseIsExplicitTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> FlatNodes;
	UFastNoiseEditor* const FlatEditor = CreateTestFastNoiseEditor(FlatNodes);
	UBiomeStrategyData* const FlatStrategy = CreateNoDetailMainMenuIslandStrategy();
	const FNodeLink FlatBiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(FlatEditor, FlatStrategy, TestReservationBiomeTag());

	std::vector<FNodeLink> NoisyNodes;
	UFastNoiseEditor* const NoisyEditor = CreateTestFastNoiseEditor(NoisyNodes);
	UBiomeStrategyData* const NoisyStrategy = CreateNoisyReservationSurfaceIslandStrategy();
	const FNodeLink NoisyBiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(NoisyEditor, NoisyStrategy, TestReservationBiomeTag());

	TestTrue(TEXT("Default reservation GenA is valid"), FlatBiomeGenA.Node != nullptr);
	TestFalse(TEXT("Default reservation GenA has no surface noise"), FlatBiomeGenA.Node != nullptr && FlatBiomeGenA.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestTrue(TEXT("Default flat reservation GenA uses foundation-strength density for Porism's additive biome merge"), FlatBiomeGenA.Node != nullptr && FlatBiomeGenA.Node->Flow.Contains(TEXT("MultiplyFloat15")));
	TestTrue(TEXT("Noisy reservation GenA is valid"), NoisyBiomeGenA.Node != nullptr);
	TestTrue(TEXT("Noisy reservation GenA includes explicit surface noise"), NoisyBiomeGenA.Node != nullptr && NoisyBiomeGenA.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestTrue(TEXT("Noisy reservation GenA uses fractal surface detail for visible hills"), NoisyBiomeGenA.Node != nullptr && NoisyBiomeGenA.Node->Flow.Contains(TEXT("FractalFBm")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseNoisyReservationSurfaceChangesHeightAcrossFootprintTest,
	"PorismExtension.Biome.IslandNoise.NoisyReservationSurfaceChangesHeightAcrossFootprint",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseNoisyReservationSurfaceChangesHeightAcrossFootprintTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoisyReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	TArray<FVector2D> Samples;
	Samples.Add(FVector2D(0.0f, 0.0f));
	Samples.Add(FVector2D(12.0f, 0.0f));
	Samples.Add(FVector2D(24.0f, 10.0f));
	Samples.Add(FVector2D(-18.0f, 16.0f));
	Samples.Add(FVector2D(26.0f, -18.0f));

	float MinSurfaceZ = TNumericLimits<float>::Max();
	float MaxSurfaceZ = -TNumericLimits<float>::Max();
	for (const FVector2D& Sample : Samples)
	{
		float SurfaceZ = 0.0f;
		TestTrue(
			*FString::Printf(TEXT("Noisy reservation surface can be inferred at XY=%s"), *Sample.ToString()),
			InferFlatReservationSurfaceZBlocks(BiomeGenA, ScaleContext, Sample, SurfaceZ));
		MinSurfaceZ = FMath::Min(MinSurfaceZ, SurfaceZ);
		MaxSurfaceZ = FMath::Max(MaxSurfaceZ, SurfaceZ);
	}

	TestTrue(TEXT("Noisy reservation terrain payload creates visible block-height variation"), MaxSurfaceZ - MinSurfaceZ > 0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseTerracedReservationSurfaceAddsTerraceNodeTest,
	"PorismExtension.Biome.IslandNoise.TerracedReservationSurfaceAddsTerraceNode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseTerracedReservationSurfaceAddsTerraceNodeTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateTerracedReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());

	TestTrue(TEXT("Terraced reservation GenA is valid"), BiomeGenA.Node != nullptr);
	TestTrue(TEXT("Terraced reservation GenA quantizes the noisy surface"), BiomeGenA.Node != nullptr && BiomeGenA.Node->Flow.Contains(TEXT("Terrace")));

	TArray<FVector2D> Samples;
	Samples.Add(FVector2D(0.0f, 0.0f));
	Samples.Add(FVector2D(12.0f, 0.0f));
	Samples.Add(FVector2D(24.0f, 10.0f));
	Samples.Add(FVector2D(-18.0f, 16.0f));
	Samples.Add(FVector2D(26.0f, -18.0f));

	float MinSurfaceZ = TNumericLimits<float>::Max();
	float MaxSurfaceZ = -TNumericLimits<float>::Max();
	float FirstSurfaceZ = 0.0f;
	bool bHasFirstSurface = false;
	for (const FVector2D& Sample : Samples)
	{
		float SurfaceZ = 0.0f;
		TestTrue(
			*FString::Printf(TEXT("Terraced reservation surface can be inferred at XY=%s"), *Sample.ToString()),
			InferFlatReservationSurfaceZBlocks(BiomeGenA, ScaleContext, Sample, SurfaceZ));
		MinSurfaceZ = FMath::Min(MinSurfaceZ, SurfaceZ);
		MaxSurfaceZ = FMath::Max(MaxSurfaceZ, SurfaceZ);
		if (!bHasFirstSurface)
		{
			FirstSurfaceZ = SurfaceZ;
			bHasFirstSurface = true;
		}

		const float RelativeSurfaceZ = SurfaceZ - FirstSurfaceZ;
		const float SnappedSurfaceZ = FMath::GridSnap(RelativeSurfaceZ, 2.0f);
		TestTrue(
			*FString::Printf(TEXT("Terraced reservation surface changes in two-block steps at XY=%s. RelativeSurface=%f Snapped=%f"), *Sample.ToString(), RelativeSurfaceZ, SnappedSurfaceZ),
			FMath::IsNearlyEqual(RelativeSurfaceZ, SnappedSurfaceZ, 0.25f));
	}

	TestTrue(TEXT("Terraced reservation terrain payload creates visible stepped height variation"), MaxSurfaceZ - MinSurfaceZ >= 2.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseTerrainSurfaceOffsetThreeBlocksMovesExactFlatSurfaceTest,
	"PorismExtension.Biome.IslandNoise.TerrainSurfaceOffsetThreeBlocksMovesExactFlatSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseTerrainSurfaceOffsetThreeBlocksMovesExactFlatSurfaceTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateThreeBlockRaisedReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	float OneBlockBelow = 0.0f;
	float AtRaisedSurface = 0.0f;
	float OneBlockAbove = 0.0f;
	TestTrue(TEXT("Reservation GenA samples one block below raised surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 2.0), OneBlockBelow));
	TestTrue(TEXT("Reservation GenA samples at raised surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 3.0), AtRaisedSurface));
	TestTrue(TEXT("Reservation GenA samples one block above raised surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 4.0), OneBlockAbove));

	TestTrue(TEXT("SurfaceZOffset raises the visible flat surface boundary by three authored blocks"), OneBlockBelow < 0.0f && AtRaisedSurface > 0.0f);
	TestTrue(TEXT("Raised flat surface is solid below"), OneBlockBelow < 0.0f);
	TestTrue(TEXT("Raised flat surface is air at the visible top boundary"), AtRaisedSurface > 0.0f);
	TestTrue(TEXT("Raised flat surface is air above"), OneBlockAbove > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseOneBlockSurfaceZLiftKeepsFlatReservationSurfaceTest,
	"PorismExtension.Biome.IslandNoise.OneBlockSurfaceZLiftKeepsFlatReservationSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseOneBlockSurfaceZLiftKeepsFlatReservationSurfaceTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateOneBlockLiftedReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	const FVector2D InteriorSamples[] = {
		FVector2D(0.0f, 0.0f),
		FVector2D(12.0f, 0.0f),
		FVector2D(-18.0f, 7.0f),
		FVector2D(24.0f, -12.0f)
	};

	float ReferenceSurfaceZ = 0.0f;
	bool bHasReferenceSurface = false;
	for (const FVector2D& Sample : InteriorSamples)
	{
		float SurfaceZ = 0.0f;
		TestTrue(
			*FString::Printf(TEXT("Lifted flat reservation surface can be inferred at XY=%s"), *Sample.ToString()),
			InferFlatReservationSurfaceZBlocks(ReservationGenA, ScaleContext, Sample, SurfaceZ));
		if (!bHasReferenceSurface)
		{
			ReferenceSurfaceZ = SurfaceZ;
			bHasReferenceSurface = true;
		}

		TestTrue(
			*FString::Printf(TEXT("One-block SurfaceZLift keeps flat reservation GenA level at XY=%s. Surface=%f Reference=%f"), *Sample.ToString(), SurfaceZ, ReferenceSurfaceZ),
			FMath::IsNearlyEqual(SurfaceZ, ReferenceSurfaceZ, 0.1f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseTerrainSurfaceOffsetAppliesAfterSolveTest,
	"PorismExtension.Biome.IslandNoise.TerrainSurfaceOffsetAppliesAfterSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseTerrainSurfaceOffsetAppliesAfterSolveTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateRaisedReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	float GenAAtFoundationSurface = 0.0f;
	float GenAAtRaisedSurface = 0.0f;
	float DomainAtFoundationSurface = 0.0f;
	float DomainAtRaisedClearance = 0.0f;
	float DomainAboveRaisedClearance = 0.0f;
	TestTrue(TEXT("Reservation GenA samples at original foundation surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 0.0), GenAAtFoundationSurface));
	TestTrue(TEXT("Reservation GenA samples at raised anchor surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 20.0), GenAAtRaisedSurface));
	TestTrue(TEXT("Reservation domain samples at original foundation surface"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 0.0), DomainAtFoundationSurface));
	TestTrue(TEXT("Reservation domain samples at raised structure clearance"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 55.0), DomainAtRaisedClearance));
	TestTrue(TEXT("Reservation domain samples above raised structure clearance"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 55.6), DomainAboveRaisedClearance));

	TestTrue(TEXT("Positive surface offset gates reservation GenA to air below the raised anchor domain"), GenAAtFoundationSurface > 0.0f);
	TestTrue(TEXT("Raised GenA surface is air at the visible top boundary"), GenAAtRaisedSurface > 0.0f);
	TestTrue(TEXT("Large positive offsets can move the whole anchor volume above the foundation surface"), DomainAtFoundationSurface < 0.0f);
	TestTrue(TEXT("Domain extends above the raised anchor surface for structures"), DomainAtRaisedClearance > 0.0f);
	TestTrue(TEXT("Domain stops outside the raised structure clearance"), DomainAboveRaisedClearance < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBoxAnchorKeepsNarrowBoundaryBlendTest,
	"PorismExtension.Biome.IslandNoise.BoxAnchorKeepsNarrowBoundaryBlend",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBoxAnchorKeepsNarrowBoundaryBlendTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateDefaultBoxAnchorIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	float Center = 0.0f;
	float TwoBlocksInsideEdge = 0.0f;
	float EdgeCell = 0.0f;
	float FirstOutsideEdge = 0.0f;
	TestTrue(TEXT("Box reservation domain samples at center"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0, 0.0, 0.0), Center));
	TestTrue(TEXT("Box reservation domain samples two blocks inside the X edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(33.0, 0.0, 0.0), TwoBlocksInsideEdge));
	TestTrue(TEXT("Box reservation domain samples the configured positive X edge cell"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.0, 0.0, 0.0), EdgeCell));
	TestTrue(TEXT("Box reservation domain samples the first cell outside the X edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(36.0, 0.0, 0.0), FirstOutsideEdge));

	TestTrue(TEXT("Box reservation domain has full-strength ownership at the center"), Center >= 0.99f);
	TestTrue(TEXT("Box reservation domain keeps most of the footprint at full strength"), TwoBlocksInsideEdge >= 0.99f);
	TestTrue(TEXT("Box reservation domain keeps the configured edge cell owned"), EdgeCell > 0.0f);
	TestTrue(TEXT("Box reservation domain exits the default overlap band at the first outside edge cell"), FirstOutsideEdge <= -0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseReservationTerrainFillsConfiguredSurfaceEdgesTest,
	"PorismExtension.Biome.IslandNoise.ReservationTerrainFillsConfiguredSurfaceEdges",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseReservationTerrainFillsConfiguredSurfaceEdgesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const SphereStrategy = CreateNoDetailMainMenuIslandStrategy();
	UBiomeStrategyData* const BoxStrategy = CreateDefaultBoxAnchorIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(SphereStrategy);

	std::vector<FNodeLink> SphereNodes;
	UFastNoiseEditor* const SphereEditor = CreateTestFastNoiseEditor(SphereNodes);
	const FNodeLink SphereGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(SphereEditor, SphereStrategy, TestReservationBiomeTag(), &ScaleContext);

	std::vector<FNodeLink> BoxNodes;
	UFastNoiseEditor* const BoxEditor = CreateTestFastNoiseEditor(BoxNodes);
	const FNodeLink BoxGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(BoxEditor, BoxStrategy, TestReservationBiomeTag(), &ScaleContext);

	float SphereEdgeSolid = 0.0f;
	float SphereOutsideEdge = 0.0f;
	float BoxEdgeSolid = 0.0f;
	float BoxOutsideEdge = 0.0f;
	TestTrue(TEXT("Sphere reservation GenA samples the configured positive X edge"), EvaluateNoiseAtAuthoredBlock(SphereGenA, ScaleContext, FVector(35.0f, 0.0f, -1.0f), SphereEdgeSolid));
	TestTrue(TEXT("Sphere reservation GenA samples outside the configured positive X edge"), EvaluateNoiseAtAuthoredBlock(SphereGenA, ScaleContext, FVector(36.0f, 0.0f, -1.0f), SphereOutsideEdge));
	TestTrue(TEXT("Box reservation GenA samples the configured positive X edge"), EvaluateNoiseAtAuthoredBlock(BoxGenA, ScaleContext, FVector(35.0f, 0.0f, -1.0f), BoxEdgeSolid));
	TestTrue(TEXT("Box reservation GenA samples outside the configured positive X edge"), EvaluateNoiseAtAuthoredBlock(BoxGenA, ScaleContext, FVector(36.0f, 0.0f, -1.0f), BoxOutsideEdge));

	TestTrue(TEXT("Sphere reservation terrain fills its configured edge cell"), SphereEdgeSolid < 0.0f);
	TestTrue(TEXT("Sphere reservation terrain remains gated outside its configured edge"), SphereOutsideEdge > 0.0f);
	TestTrue(TEXT("Box reservation terrain fills its configured edge cell"), BoxEdgeSolid < 0.0f);
	TestTrue(TEXT("Box reservation terrain remains gated outside its configured edge"), BoxOutsideEdge > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSphereOutsideDomainKeepsFoundationTopFaceFlatTest,
	"PorismExtension.Biome.IslandNoise.SphereOutsideDomainKeepsFoundationTopFaceFlat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSphereOutsideDomainKeepsFoundationTopFaceFlatTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> ReservationNodes;
	UFastNoiseEditor* const ReservationEditor = CreateTestFastNoiseEditor(ReservationNodes);
	const FNodeLink ReservationGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(ReservationEditor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	std::vector<FNodeLink> FoundationNodes;
	UFastNoiseEditor* const FoundationEditor = CreateTestFastNoiseEditor(FoundationNodes);
	const FNodeLink FoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(FoundationEditor, Strategy, &ScaleContext);

	const FVector ProbePoints[] = {
		FVector(36.0f, 0.0f, 0.0f),
		FVector(35.0f, 9.0f, 0.0f),
		FVector(34.0f, 12.0f, 0.0f),
		FVector(31.0f, 18.0f, 0.0f),
		FVector(27.0f, 24.0f, 0.0f)
	};

	for (const FVector& ProbePoint : ProbePoints)
	{
		float FoundationAtTopFace = 0.0f;
		float FoundationOneBlockBelow = 0.0f;
		float ReservationDensity = 0.0f;
		TestTrue(*FString::Printf(TEXT("Foundation samples near sphere outside-domain top face point %s"), *ProbePoint.ToString()), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, ProbePoint, FoundationAtTopFace));
		TestTrue(*FString::Printf(TEXT("Foundation samples one block below sphere outside-domain point %s"), *ProbePoint.ToString()), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, ProbePoint - FVector(0.0f, 0.0f, 1.0f), FoundationOneBlockBelow));
		TestTrue(*FString::Printf(TEXT("Reservation GenA samples near sphere outside-domain point %s"), *ProbePoint.ToString()), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, ProbePoint, ReservationDensity));

		TestTrue(
			*FString::Printf(TEXT("Foundation authored top face remains air near sphere edge point %s"), *ProbePoint.ToString()),
			FoundationAtTopFace > 0.0f && FoundationAtTopFace < 0.1f);
		TestTrue(
			*FString::Printf(TEXT("Foundation remains solid one block below the top face near sphere edge point %s"), *ProbePoint.ToString()),
			FoundationOneBlockBelow < 0.0f);
		TestTrue(
			*FString::Printf(TEXT("Outside-domain reservation terrain stays air at %s. ReservationDensity=%f FoundationAtTopFace=%f"), *ProbePoint.ToString(), ReservationDensity, FoundationAtTopFace),
			ReservationDensity > 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSphereAnchorExitsOverlapBandOutsideConfiguredEdgeTest,
	"PorismExtension.Biome.IslandNoise.SphereAnchorExitsOverlapBandOutsideConfiguredEdge",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSphereAnchorExitsOverlapBandOutsideConfiguredEdgeTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	float EdgeCell = 0.0f;
	float FirstOutsideEdge = 0.0f;
	TestTrue(TEXT("Sphere reservation domain samples the configured positive X edge cell"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.0f, 0.0f, 0.0f), EdgeCell));
	TestTrue(TEXT("Sphere reservation domain samples the first cell outside the X edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(36.0f, 0.0f, 0.0f), FirstOutsideEdge));

	TestTrue(TEXT("Sphere reservation domain keeps the configured edge cell owned"), EdgeCell > 0.0f);
	TestTrue(TEXT("Sphere reservation domain exits the default overlap band at the first outside edge cell"), FirstOutsideEdge <= -0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceZLiftAndOffsetComposeTest,
	"PorismExtension.Biome.IslandNoise.SurfaceZLiftAndOffsetCompose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceZLiftAndOffsetComposeTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateLiftedAndOffsetReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	float OneBlockBelow = 0.0f;
	float AtComposedSurface = 0.0f;
	float OneBlockAbove = 0.0f;
	TestTrue(TEXT("Reservation GenA samples one block below lifted and offset surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 4.0), OneBlockBelow));
	TestTrue(TEXT("Reservation GenA samples at lifted and offset surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 5.0), AtComposedSurface));
	TestTrue(TEXT("Reservation GenA samples one block above lifted and offset surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, 6.0), OneBlockAbove));

	TestTrue(TEXT("SurfaceZLift and SurfaceZOffset compose additively at the visible top boundary"), OneBlockBelow < 0.0f && AtComposedSurface > 0.0f);
	TestTrue(TEXT("Lifted and offset surface remains solid below"), OneBlockBelow < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseNegativeSurfaceZLiftAndOffsetComposeTest,
	"PorismExtension.Biome.IslandNoise.NegativeSurfaceZLiftAndOffsetCompose",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseNegativeSurfaceZLiftAndOffsetComposeTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateLoweredAndOffsetReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	float OneBlockBelow = 0.0f;
	float AtComposedSurface = 0.0f;
	float OneBlockAbove = 0.0f;
	TestTrue(TEXT("Reservation GenA samples one block below lowered and offset surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, -8.0), OneBlockBelow));
	TestTrue(TEXT("Reservation GenA samples at lowered and offset surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, -7.0), AtComposedSurface));
	TestTrue(TEXT("Reservation GenA samples one block above lowered and offset surface"), EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0, 0.0, -6.0), OneBlockAbove));

	TestTrue(TEXT("Negative SurfaceZLift and positive SurfaceZOffset compose additively at the visible top boundary"), OneBlockBelow < 0.0f && AtComposedSurface > 0.0f);
	TestTrue(TEXT("Lowered and offset surface remains solid below"), OneBlockBelow < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceZOffsetDoesNotRaiseFoundationSupportTest,
	"PorismExtension.Biome.IslandNoise.SurfaceZOffsetDoesNotRaiseFoundationSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceZOffsetDoesNotRaiseFoundationSupportTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> BaselineNodes;
	UFastNoiseEditor* const BaselineEditor = CreateTestFastNoiseEditor(BaselineNodes);
	UBiomeStrategyData* const BaselineStrategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(BaselineStrategy);
	const FNodeLink BaselineFoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(BaselineEditor, BaselineStrategy);

	std::vector<FNodeLink> OffsetNodes;
	UFastNoiseEditor* const OffsetEditor = CreateTestFastNoiseEditor(OffsetNodes);
	UBiomeStrategyData* const OffsetStrategy = CreateRaisedReservationSurfaceIslandStrategy();
	const FNodeLink OffsetFoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(OffsetEditor, OffsetStrategy);

	const FVector ProbePoints[] = {
		FVector(0.0, 0.0, 10.0),
		FVector(35.0, 0.0, 10.0),
		FVector(42.0, 0.0, 6.0)
	};
	for (const FVector& ProbePoint : ProbePoints)
	{
		float BaselineValue = 0.0f;
		float OffsetValue = 0.0f;
		TestTrue(*FString::Printf(TEXT("Baseline foundation samples at %s"), *ProbePoint.ToString()), EvaluateNoiseAtAuthoredBlock(BaselineFoundationGenA, ScaleContext, ProbePoint, BaselineValue));
		TestTrue(*FString::Printf(TEXT("Offset foundation samples at %s"), *ProbePoint.ToString()), EvaluateNoiseAtAuthoredBlock(OffsetFoundationGenA, ScaleContext, ProbePoint, OffsetValue));
		TestTrue(
			*FString::Printf(TEXT("SurfaceZOffset does not change foundation support density at %s"), *ProbePoint.ToString()),
			FMath::IsNearlyEqual(BaselineValue, OffsetValue, 0.0001f));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseNegativeSurfaceZLiftLowersFoundationSupportTest,
	"PorismExtension.Biome.IslandNoise.NegativeSurfaceZLiftLowersFoundationSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseNegativeSurfaceZLiftLowersFoundationSupportTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> BaselineNodes;
	UFastNoiseEditor* const BaselineEditor = CreateTestFastNoiseEditor(BaselineNodes);
	UBiomeStrategyData* const BaselineStrategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(BaselineStrategy);
	const FNodeLink BaselineFoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(BaselineEditor, BaselineStrategy);

	std::vector<FNodeLink> LoweredNodes;
	UFastNoiseEditor* const LoweredEditor = CreateTestFastNoiseEditor(LoweredNodes);
	UBiomeStrategyData* const LoweredStrategy = CreateLoweredReservationSurfaceIslandStrategy();
	const FNodeLink LoweredFoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(LoweredEditor, LoweredStrategy);
	const FNodeLink LoweredFoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(LoweredEditor, LoweredStrategy);

	float BaselineNearEdge = 0.0f;
	float LoweredNearEdge = 0.0f;
	float LoweredBelowSupport = 0.0f;
	float LoweredFarOutside = 0.0f;
	float LoweredDomainEdgeBand = 0.0f;
	float LoweredDomainOutsideSupport = 0.0f;
	float LoweredDomainCore = 0.0f;
	TestTrue(TEXT("Baseline foundation samples near lowered anchor edge"), EvaluateNoiseAtAuthoredBlock(BaselineFoundationGenA, ScaleContext, FVector(40.0, 0.0, -1.0), BaselineNearEdge));
	TestTrue(TEXT("Lowered foundation samples near lowered anchor edge"), EvaluateNoiseAtAuthoredBlock(LoweredFoundationGenA, ScaleContext, FVector(40.0, 0.0, -1.0), LoweredNearEdge));
	TestTrue(TEXT("Lowered foundation samples below lowered support"), EvaluateNoiseAtAuthoredBlock(LoweredFoundationGenA, ScaleContext, FVector(40.0, 0.0, -8.0), LoweredBelowSupport));
	TestTrue(TEXT("Lowered foundation samples beyond support slope"), EvaluateNoiseAtAuthoredBlock(LoweredFoundationGenA, ScaleContext, FVector(55.0, 0.0, -1.0), LoweredFarOutside));
	TestTrue(TEXT("Lowered foundation domain samples in the reservation edge band"), EvaluateNoiseAtAuthoredBlock(LoweredFoundationDomain, ScaleContext, FVector(35.0, 0.0, -5.0), LoweredDomainEdgeBand));
	TestTrue(TEXT("Lowered foundation domain samples just outside the reservation support"), EvaluateNoiseAtAuthoredBlock(LoweredFoundationDomain, ScaleContext, FVector(40.0, 0.0, -5.0), LoweredDomainOutsideSupport));
	TestTrue(TEXT("Lowered foundation domain samples in the reservation core"), EvaluateNoiseAtAuthoredBlock(LoweredFoundationDomain, ScaleContext, FVector(0.0, 0.0, -5.0), LoweredDomainCore));

	TestTrue(TEXT("Baseline foundation is solid near the unlowered surface"), BaselineNearEdge < 0.0f);
	TestTrue(TEXT("Negative SurfaceZLift cuts support terrain down near the anchor edge"), LoweredNearEdge > 0.0f);
	TestTrue(TEXT("Negative SurfaceZLift keeps terrain solid below the lowered support surface"), LoweredBelowSupport < 0.0f);
	TestTrue(TEXT("Negative SurfaceZLift support slope ends outside its lift-width band"), LoweredFarOutside < 0.0f);
	TestTrue(TEXT("Negative SurfaceZLift carves foundation ownership from the reservation edge band"), LoweredDomainEdgeBand < 0.0f);
	TestTrue(TEXT("Negative SurfaceZLift keeps foundation support available outside the reservation edge"), LoweredDomainOutsideSupport > 0.0f);
	TestTrue(TEXT("Negative SurfaceZLift still carves the foundation from the reservation core"), LoweredDomainCore < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceZLiftRaisesFoundationSupportTest,
	"PorismExtension.Biome.IslandNoise.SurfaceZLiftRaisesFoundationSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceZLiftRaisesFoundationSupportTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> BaselineNodes;
	UFastNoiseEditor* const BaselineEditor = CreateTestFastNoiseEditor(BaselineNodes);
	UBiomeStrategyData* const BaselineStrategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(BaselineStrategy);
	const FNodeLink BaselineFoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(BaselineEditor, BaselineStrategy);

	std::vector<FNodeLink> LiftNodes;
	UFastNoiseEditor* const LiftEditor = CreateTestFastNoiseEditor(LiftNodes);
	UBiomeStrategyData* const LiftStrategy = CreateLiftedReservationSurfaceIslandStrategy();
	const FNodeLink LiftFoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(LiftEditor, LiftStrategy);
	const FNodeLink LiftFoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(LiftEditor, LiftStrategy);

	float BaselineNearEdge = 0.0f;
	float LiftNearEdge = 0.0f;
	float LiftFarOutside = 0.0f;
	float LiftDomainEdgeBand = 0.0f;
	float LiftDomainOutsideSupport = 0.0f;
	float LiftDomainCore = 0.0f;
	TestTrue(TEXT("Baseline foundation samples near lifted anchor edge"), EvaluateNoiseAtAuthoredBlock(BaselineFoundationGenA, ScaleContext, FVector(40.0, 0.0, 5.0), BaselineNearEdge));
	TestTrue(TEXT("Lifted foundation samples near lifted anchor edge"), EvaluateNoiseAtAuthoredBlock(LiftFoundationGenA, ScaleContext, FVector(40.0, 0.0, 5.0), LiftNearEdge));
	TestTrue(TEXT("Lifted foundation samples beyond support slope"), EvaluateNoiseAtAuthoredBlock(LiftFoundationGenA, ScaleContext, FVector(55.0, 0.0, 5.0), LiftFarOutside));
	TestTrue(TEXT("Lifted foundation domain samples in the reservation edge band"), EvaluateNoiseAtAuthoredBlock(LiftFoundationDomain, ScaleContext, FVector(35.0, 0.0, 5.0), LiftDomainEdgeBand));
	TestTrue(TEXT("Lifted foundation domain samples just outside the reservation support"), EvaluateNoiseAtAuthoredBlock(LiftFoundationDomain, ScaleContext, FVector(40.0, 0.0, 5.0), LiftDomainOutsideSupport));
	TestTrue(TEXT("Lifted foundation domain samples in the reservation core"), EvaluateNoiseAtAuthoredBlock(LiftFoundationDomain, ScaleContext, FVector(0.0, 0.0, 5.0), LiftDomainCore));

	TestTrue(TEXT("Baseline foundation remains air above the unlifted surface near the anchor"), BaselineNearEdge > 0.0f);
	TestTrue(TEXT("SurfaceZLift raises support terrain near the anchor edge"), LiftNearEdge < 0.0f);
	TestTrue(TEXT("SurfaceZLift support slope ends outside its lift-width band"), LiftFarOutside > 0.0f);
	TestTrue(TEXT("SurfaceZLift carves foundation ownership from the reservation edge band"), LiftDomainEdgeBand < 0.0f);
	TestTrue(TEXT("SurfaceZLift keeps foundation support available outside the reservation edge"), LiftDomainOutsideSupport > 0.0f);
	TestTrue(TEXT("SurfaceZLift still carves the foundation from the reservation core"), LiftDomainCore < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceZLiftExtendsFoundationDomainTopTest,
	"PorismExtension.Biome.IslandNoise.SurfaceZLiftExtendsFoundationDomainTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceZLiftExtendsFoundationDomainTopTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateTwentyBlockLiftedReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy);
	const FNodeLink FoundationGenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	float DomainAtUpperSupportSlope = 0.0f;
	float GenAAtUpperSupportSlope = 0.0f;
	float DomainAboveSupportSlope = 0.0f;
	TestTrue(TEXT("Lifted foundation domain samples the upper support slope"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(40.0, 0.0, 15.0), DomainAtUpperSupportSlope));
	TestTrue(TEXT("Lifted foundation GenA samples the upper support slope"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(40.0, 0.0, 15.0), GenAAtUpperSupportSlope));
	TestTrue(TEXT("Lifted foundation domain samples above the support slope"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(40.0, 0.0, 35.0), DomainAboveSupportSlope));

	TestTrue(TEXT("SurfaceZLift extends foundation domain high enough for its support slope"), DomainAtUpperSupportSlope > 0.0f);
	TestTrue(TEXT("SurfaceZLift support slope remains solid inside the extended domain"), GenAAtUpperSupportSlope < 0.0f);
	TestTrue(TEXT("Extended foundation domain still ends above the lifted support area"), DomainAboveSupportSlope < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSurfaceHeightSolveModesUseFoundationSurfaceAndOffsetTest,
	"PorismExtension.Biome.IslandNoise.SurfaceHeightSolveModesUseFoundationSurfaceAndOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSurfaceHeightSolveModesUseFoundationSurfaceAndOffsetTest::RunTest(const FString& Parameters)
{
	constexpr float SurfaceOffsetBlocks = 3.0f;
	const ESurfaceAnchorHeightSolveMode SolveModes[] = {
		ESurfaceAnchorHeightSolveMode::CenterSample,
		ESurfaceAnchorHeightSolveMode::AverageSamples,
		ESurfaceAnchorHeightSolveMode::CornerMaxSamples,
		ESurfaceAnchorHeightSolveMode::MaxSamples
	};

	for (const ESurfaceAnchorHeightSolveMode SolveMode : SolveModes)
	{
		std::vector<FNodeLink> Nodes;
		UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
		UBiomeStrategyData* const Strategy = CreateNoisyFoundationSurfaceSolveStrategy(SolveMode, SurfaceOffsetBlocks);
		const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
		const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
		const FSurfaceAnchorReservationPayload* const ReservationPayload = Strategy->RootFoundationProvider.Reservations[0].ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>();
		TestNotNull(TEXT("Surface solve strategy has a surface-anchor reservation payload"), ReservationPayload);
		if (ReservationPayload == nullptr)
		{
			return false;
		}

		FFoundationSurfaceQuery SurfaceQuery;
		SurfaceQuery.CenterXY = ReservationPayload->CenterXY;
		SurfaceQuery.FootprintHalfExtent = FVector2D(ReservationPayload->BoxHalfExtent.X, ReservationPayload->BoxHalfExtent.Y);
		SurfaceQuery.SampleMode = SolveMode == ESurfaceAnchorHeightSolveMode::CenterSample
			? EFoundationSurfaceSampleMode::CenterSample
			: (SolveMode == ESurfaceAnchorHeightSolveMode::AverageSamples
				? EFoundationSurfaceSampleMode::AverageSamples
				: (SolveMode == ESurfaceAnchorHeightSolveMode::CornerMaxSamples
					? EFoundationSurfaceSampleMode::CornerMaxSamples
					: EFoundationSurfaceSampleMode::MaxSamples));

		FResolvedFoundationSurface ProviderSurface;
		TestTrue(TEXT("Provider surface query resolves the expected foundation surface"), Strategy->QueryFoundationSurface(GetTransientPackage(), SurfaceQuery, ProviderSurface));
		const float ExpectedSurfaceZ = ProviderSurface.SurfaceZBlock + SurfaceOffsetBlocks;

		float AtExpectedSurface = 0.0f;
		float BelowExpectedSurface = 0.0f;
		float AboveExpectedSurface = 0.0f;
		TestTrue(
			*FString::Printf(TEXT("Reservation surface samples at solve mode %d expected height"), static_cast<int32>(SolveMode)),
			EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0f, 0.0f, ExpectedSurfaceZ), AtExpectedSurface));
		TestTrue(
			*FString::Printf(TEXT("Reservation surface samples one block below solve mode %d expected height"), static_cast<int32>(SolveMode)),
			EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0f, 0.0f, ExpectedSurfaceZ - 1.0f), BelowExpectedSurface));
		TestTrue(
			*FString::Printf(TEXT("Reservation surface samples one block above solve mode %d expected height"), static_cast<int32>(SolveMode)),
			EvaluateNoiseAtAuthoredBlock(BiomeGenA, ScaleContext, FVector(0.0f, 0.0f, ExpectedSurfaceZ + 1.0f), AboveExpectedSurface));
		TestTrue(
			*FString::Printf(TEXT("Solve mode %d applies its expected foundation surface and post-solve offset"), static_cast<int32>(SolveMode)),
			BelowExpectedSurface < 0.0f && AtExpectedSurface > 0.0f && AboveExpectedSurface > 0.0f);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseReservationDomainIsClippedToFoundationEnvelopeTest,
	"PorismExtension.Biome.IslandNoise.ReservationDomainIsClippedToFoundationEnvelope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseReservationDomainIsClippedToFoundationEnvelopeTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateOutOfEnvelopeAnchorIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	float DomainAtOutOfEnvelopeAnchor = 0.0f;
	TestTrue(
		TEXT("Reservation domain samples at out-of-envelope authored anchor"),
		EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(300.0, 0.0, 0.0), DomainAtOutOfEnvelopeAnchor));

	TestTrue(TEXT("Reservation domain is clipped by the foundation envelope"), DomainAtOutOfEnvelopeAnchor < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInvalidAnchorExtentsFallbackToBlockUnitsTest,
	"PorismExtension.Biome.IslandNoise.InvalidAnchorExtentsFallbackToBlockUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInvalidAnchorExtentsFallbackToBlockUnitsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateInvalidAnchorExtentIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	float DomainAtCenter = 0.0f;
	float DomainTwentyBlocksFromCenter = 0.0f;
	TestTrue(TEXT("Reservation domain samples invalid-extent anchor center"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector::ZeroVector, DomainAtCenter));
	TestTrue(TEXT("Reservation domain samples block-unit fallback radius"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(20.0, 0.0, 0.0), DomainTwentyBlocksFromCenter));

	TestTrue(TEXT("Invalid anchor extents still own the anchor center"), DomainAtCenter > 0.0f);
	TestTrue(TEXT("Invalid anchor extents fall back to block-scale dimensions instead of raw-noise-scale dimensions"), DomainTwentyBlocksFromCenter > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBoxAnchorCoversConfiguredBoundaryTest,
	"PorismExtension.Biome.IslandNoise.BoxAnchorCoversConfiguredBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBoxAnchorCoversConfiguredBoundaryTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateDefaultBoxAnchorIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	float AtPositiveEdge = 0.0f;
	float AtNegativeEdge = 0.0f;
	float OutsidePositiveEdge = 0.0f;
	float OutsideNegativeEdge = 0.0f;
	TestTrue(TEXT("Box domain samples positive X configured edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.0, 0.0, 0.0), AtPositiveEdge));
	TestTrue(TEXT("Box domain samples negative X configured edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(-35.0, 0.0, 0.0), AtNegativeEdge));
	TestTrue(TEXT("Box domain samples outside positive X edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.6, 0.0, 0.0), OutsidePositiveEdge));
	TestTrue(TEXT("Box domain samples outside negative X edge"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(-35.6, 0.0, 0.0), OutsideNegativeEdge));

	TestTrue(TEXT("Configured positive X edge is owned"), AtPositiveEdge > 0.0f);
	TestTrue(TEXT("Configured negative X edge is owned"), AtNegativeEdge > 0.0f);
	TestTrue(TEXT("Outside positive X edge is not owned"), OutsidePositiveEdge < 0.0f);
	TestTrue(TEXT("Outside negative X edge is not owned"), OutsideNegativeEdge < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSphereAnchorCoversConfiguredBoundaryTest,
	"PorismExtension.Biome.IslandNoise.SphereAnchorCoversConfiguredBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSphereAnchorCoversConfiguredBoundaryTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	float AtPositiveEdge = 0.0f;
	float AtNegativeEdge = 0.0f;
	float AtPythagoreanEdge = 0.0f;
	float OutsidePositiveEdge = 0.0f;
	float OutsideNegativeEdge = 0.0f;
	float SparseDiagonalAnnulus = 0.0f;
	TestTrue(TEXT("Sphere domain samples positive X configured radius"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.0, 0.0, 0.0), AtPositiveEdge));
	TestTrue(TEXT("Sphere domain samples negative X configured radius"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(-35.0, 0.0, 0.0), AtNegativeEdge));
	TestTrue(TEXT("Sphere domain samples non-cardinal configured radius"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(28.0, 21.0, 0.0), AtPythagoreanEdge));
	TestTrue(TEXT("Sphere domain samples outside positive X radius"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.6, 0.0, 0.0), OutsidePositiveEdge));
	TestTrue(TEXT("Sphere domain samples outside negative X radius"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(-35.6, 0.0, 0.0), OutsideNegativeEdge));
	TestTrue(TEXT("Sphere domain samples diagonal point outside authored radius but inside old half-block annulus"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(35.0, 5.0, 0.0), SparseDiagonalAnnulus));

	TestTrue(TEXT("Configured positive X radius has strong ownership"), AtPositiveEdge >= 0.45f);
	TestTrue(TEXT("Configured negative X radius has strong ownership"), AtNegativeEdge >= 0.45f);
	TestTrue(TEXT("Configured non-cardinal radius has strong ownership"), AtPythagoreanEdge >= 0.45f);
	TestTrue(TEXT("Outside positive X radius exits the overlap band"), OutsidePositiveEdge <= -0.5f);
	TestTrue(TEXT("Outside negative X radius exits the overlap band"), OutsideNegativeEdge <= -0.5f);
	TestTrue(TEXT("Sphere domain excludes sparse diagonal annulus cells outside the authored radius"), SparseDiagonalAnnulus <= -0.5f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSphereAnchorCarvesFoundationAtConfiguredBoundaryTest,
	"PorismExtension.Biome.IslandNoise.SphereAnchorCarvesFoundationAtConfiguredBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSphereAnchorCarvesFoundationAtConfiguredBoundaryTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy, &ScaleContext);
	float CardinalEdge = 0.0f;
	float PythagoreanEdge = 0.0f;
	float OutsideAnnulus = 0.0f;
	TestTrue(TEXT("Foundation domain samples sphere cardinal edge"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(35.0f, 0.0f, 0.0f), CardinalEdge));
	TestTrue(TEXT("Foundation domain samples sphere non-cardinal edge"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(28.0f, 21.0f, 0.0f), PythagoreanEdge));
	TestTrue(TEXT("Foundation domain samples outside diagonal annulus"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(35.0f, 5.0f, 0.0f), OutsideAnnulus));

	TestTrue(TEXT("Foundation is strongly carved at sphere cardinal edge"), CardinalEdge <= -0.5f);
	TestTrue(TEXT("Foundation is strongly carved at sphere non-cardinal edge"), PythagoreanEdge <= -0.5f);
	TestTrue(TEXT("Foundation is not carved by diagonal cells outside the authored sphere radius"), OutsideAnnulus > 0.0f);

	return true;
}

