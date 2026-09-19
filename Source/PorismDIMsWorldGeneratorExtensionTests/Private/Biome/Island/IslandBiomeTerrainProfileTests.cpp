// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBuildsFoundationGenATest,
	"PorismExtension.Biome.IslandNoise.BuildsFoundationGenA",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBuildsFoundationGenATest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Foundation GenA is valid"), GenA.Node != nullptr);
	TestTrue(TEXT("Foundation GenA inverts positive-inside body for terrain polarity"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("MultiplyFloat-1")));
	TestTrue(TEXT("Foundation GenA applies internal density strength for visible terrain writes"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("MultiplyFloat15")));
	TestTrue(TEXT("Foundation GenA applies a small top-face alignment bias"), GenA.Node != nullptr && GenA.Node->Flow.StartsWith(TEXT("Add")));
	TestTrue(TEXT("Foundation GenA keeps island body detail for visible terrain"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseFlatFoundationTerrainProfileOmitsDetailTest,
	"PorismExtension.Biome.IslandNoise.FlatFoundationTerrainProfileOmitsDetail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseFlatFoundationTerrainProfileOmitsDetailTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Flat foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestFalse(TEXT("Flat foundation terrain profile with disabled island body detail omits detail nodes"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseIslandBodyDetailAppliesToFlatTopProfileTest,
	"PorismExtension.Biome.IslandNoise.IslandBodyDetailAppliesToFlatTopProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseIslandBodyDetailAppliesToFlatTopProfileTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	if (FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
	{
		IslandPayload->BodyDetail.RimNoiseAmplitude = 8.0f;
		IslandPayload->BodyDetail.UndersideNoiseAmplitude = 12.0f;
	}

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Flat top profile with island body detail builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Island body detail adds rim/underside detail without requiring a noisy top profile"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Island body detail does not add top-surface profile offset nodes"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("SeedOffset11")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseDisabledIslandBodyDetailOmitsNoiseNodesTest,
	"PorismExtension.Biome.IslandNoise.DisabledIslandBodyDetailOmitsNoiseNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseDisabledIslandBodyDetailOmitsNoiseNodesTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	if (FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
	{
		IslandPayload->BodyDetail.bEnableRimNoise = false;
		IslandPayload->BodyDetail.RimNoiseAmplitude = 50.0f;
		IslandPayload->BodyDetail.bEnableUndersideNoise = false;
		IslandPayload->BodyDetail.UndersideNoiseAmplitude = 50.0f;
		IslandPayload->BodyDetail.bEnableBottomSpikeNoise = false;
		IslandPayload->BodyDetail.BottomSpikeNoiseAmplitude = 50.0f;
	}

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Disabled body detail still builds valid GenA"), GenA.Node != nullptr);
	TestFalse(TEXT("Disabled rim/underside detail omits body-detail noise nodes even with nonzero amplitudes"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseIslandAnalyticSilhouetteControlsAvoidNoiseNodesTest,
	"PorismExtension.Biome.IslandNoise.IslandAnalyticSilhouetteControlsAvoidNoiseNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseIslandAnalyticSilhouetteControlsAvoidNoiseNodesTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	if (FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
	{
		IslandPayload->IslandBody.SideBulgeBlocks = 20.0f;
		IslandPayload->IslandBody.LowerConeSharpness = 0.8f;
		IslandPayload->IslandBody.BottomPointDepthBlocks = 24.0f;
		IslandPayload->BodyDetail.bEnableRimNoise = false;
		IslandPayload->BodyDetail.bEnableUndersideNoise = false;
		IslandPayload->BodyDetail.bEnableBottomSpikeNoise = false;
	}

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Analytic island silhouette controls build valid GenA"), GenA.Node != nullptr);
	TestFalse(TEXT("Analytic side bulge, cone sharpness, and bottom point controls do not add noise nodes"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseIslandSilhouetteControlsExpandDomainTest,
	"PorismExtension.Biome.IslandNoise.IslandSilhouetteControlsExpandDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseIslandSilhouetteControlsExpandDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	if (FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
	{
		IslandPayload->DomainRadiusPadding = 4.0f;
		IslandPayload->DomainVerticalPadding = 4.0f;
		IslandPayload->IslandBody.SideBulgeBlocks = 20.0f;
		IslandPayload->IslandBody.BottomPointDepthBlocks = 30.0f;
		IslandPayload->BodyDetail.bEnableRimNoise = false;
		IslandPayload->BodyDetail.bEnableUndersideNoise = false;
		IslandPayload->BodyDetail.bEnableBottomSpikeNoise = false;
	}
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink Domain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy, &ScaleContext);

	float BulgeExpandedDomain = 0.0f;
	float OutsideBulgeDomain = 0.0f;
	float BottomPointExpandedDomain = 0.0f;
	float BelowBottomPointDomain = 0.0f;
	TestTrue(TEXT("Side-bulge expanded radius sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(140.0f, 0.0f, -10.0f), BulgeExpandedDomain));
	TestTrue(TEXT("Outside side-bulge radius sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(146.0f, 0.0f, -10.0f), OutsideBulgeDomain));
	TestTrue(TEXT("Bottom-point expanded sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(0.0f, 0.0f, -154.0f), BottomPointExpandedDomain));
	TestTrue(TEXT("Below bottom-point allowance sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(0.0f, 0.0f, -161.0f), BelowBottomPointDomain));

	TestTrue(TEXT("Side bulge expands the provider domain radius"), BulgeExpandedDomain > 0.0f);
	TestTrue(TEXT("Provider domain still exits outside the side-bulge allowance"), OutsideBulgeDomain < 0.0f);
	TestTrue(TEXT("Bottom point expands the provider domain bottom"), BottomPointExpandedDomain > 0.0f);
	TestTrue(TEXT("Provider domain still exits below the bottom-point allowance"), BelowBottomPointDomain < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseIslandBodyDetailExpandsFoundationDomainTest,
	"PorismExtension.Biome.IslandNoise.IslandBodyDetailExpandsFoundationDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseIslandBodyDetailExpandsFoundationDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	if (FIslandFoundationShapePayload* const IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
	{
		IslandPayload->DomainRadiusPadding = 4.0f;
		IslandPayload->DomainVerticalPadding = 4.0f;
		IslandPayload->BodyDetail.RimNoiseAmplitude = 24.0f;
		IslandPayload->BodyDetail.UndersideNoiseAmplitude = 50.0f;
	}
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink Domain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy, &ScaleContext);

	float RadiusExpandedDomain = 0.0f;
	float RadiusOutsideDomain = 0.0f;
	float BottomExpandedDomain = 0.0f;
	float BelowExpandedDomain = 0.0f;
	TestTrue(TEXT("Expanded radius sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(145.0f, 0.0f, -5.0f), RadiusExpandedDomain));
	TestTrue(TEXT("Outside expanded radius sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(150.0f, 0.0f, -5.0f), RadiusOutsideDomain));
	TestTrue(TEXT("Expanded bottom sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(0.0f, 0.0f, -174.0f), BottomExpandedDomain));
	TestTrue(TEXT("Below expanded bottom sample evaluates"), EvaluateNoiseAtAuthoredBlock(Domain, ScaleContext, FVector(0.0f, 0.0f, -181.0f), BelowExpandedDomain));

	TestTrue(TEXT("Rim body detail expands the provider domain radius"), RadiusExpandedDomain > 0.0f);
	TestTrue(TEXT("Provider domain still exits outside the expanded rim radius"), RadiusOutsideDomain < 0.0f);
	TestTrue(TEXT("Underside body detail expands the provider domain bottom"), BottomExpandedDomain > 0.0f);
	TestTrue(TEXT("Provider domain still exits below the expanded underside allowance"), BelowExpandedDomain < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseNoisyFoundationTerrainProfileIsExplicitTest,
	"PorismExtension.Biome.IslandNoise.NoisyFoundationTerrainProfileIsExplicit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseNoisyFoundationTerrainProfileIsExplicitTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();

	FNoisyFoundationTerrainProfilePayload NoisyProfile;
	NoisyProfile.SurfaceNoiseAmplitude = 4.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>(NoisyProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Noisy foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Noisy foundation terrain profile adds explicit detail nodes to GenA"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBlendedSupportReservationSurfaceEasesEdgeHeightTest,
	"PorismExtension.Biome.IslandNoise.BlendedSupportReservationSurfaceEasesEdgeHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBlendedSupportReservationSurfaceEasesEdgeHeightTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateBlendedSupportReservationSurfaceIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag(), &ScaleContext);
	const FNodeLink ReservationGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	float CenterSurfaceZ = 0.0f;
	float InteriorSurfaceZ = 0.0f;
	float EdgeSurfaceZ = 0.0f;
	float DomainAtEdge = 0.0f;
	float DomainOutside = 0.0f;
	TestTrue(TEXT("Blended support reservation GenA is valid"), ReservationGenA.Node != nullptr);
	TestTrue(TEXT("Blended support reservation domain is valid"), ReservationDomain.Node != nullptr);
	TestTrue(TEXT("Blended support center surface can be inferred"), InferFlatReservationSurfaceZBlocks(ReservationGenA, ScaleContext, FVector2D::ZeroVector, CenterSurfaceZ));
	TestTrue(TEXT("Blended support interior surface can be inferred"), InferFlatReservationSurfaceZBlocks(ReservationGenA, ScaleContext, FVector2D(20.0f, 0.0f), InteriorSurfaceZ));
	TestTrue(TEXT("Blended support edge surface can be inferred"), InferFlatReservationSurfaceZBlocks(ReservationGenA, ScaleContext, FVector2D(34.0f, 0.0f), EdgeSurfaceZ));
	TestTrue(TEXT("Blended support reservation samples inside its edge domain"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(34.0f, 0.0f, 0.0f), DomainAtEdge));
	TestTrue(TEXT("Blended support reservation samples outside its domain"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(42.0f, 0.0f, 0.0f), DomainOutside));

	TestTrue(
		*FString::Printf(TEXT("Blended support keeps the pad center near the lifted surface. Center=%f"), CenterSurfaceZ),
		CenterSurfaceZ > 10.0f);
	TestTrue(
		*FString::Printf(TEXT("Blended support leaves the inner pad flat before the edge band. Center=%f Interior=%f"), CenterSurfaceZ, InteriorSurfaceZ),
		FMath::IsNearlyEqual(CenterSurfaceZ, InteriorSurfaceZ, 0.75f));
	TestTrue(
		*FString::Printf(TEXT("Blended support eases the terrain edge back toward the foundation. Center=%f Edge=%f"), CenterSurfaceZ, EdgeSurfaceZ),
		EdgeSurfaceZ < CenterSurfaceZ - 5.0f && EdgeSurfaceZ < 5.0f);
	TestTrue(TEXT("Blended support does not change reservation ownership inside the edge band"), DomainAtEdge > 0.0f);
	TestTrue(TEXT("Blended support does not leak reservation ownership outside the authored domain"), DomainOutside < 0.0f);
	TestFalse(TEXT("Blended support terrain adds no noisy reservation detail"), ReservationGenA.Node != nullptr && ReservationGenA.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Blended support terrain adds no terrace nodes"), ReservationGenA.Node != nullptr && ReservationGenA.Node->Flow.Contains(TEXT("Terrace")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBowlFoundationTerrainProfileLowersCenterTest,
	"PorismExtension.Biome.IslandNoise.BowlFoundationTerrainProfileLowersCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBowlFoundationTerrainProfileLowersCenterTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FBowlFoundationTerrainProfilePayload BowlProfile;
	BowlProfile.RadiusBlocks = 80.0f;
	BowlProfile.DepthBlocks = 10.0f;
	BowlProfile.SurfaceNoiseAmplitude = 0.0f;
	BowlProfile.bEnableRaisedRim = false;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FBowlFoundationTerrainProfilePayload>(BowlProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float CenterSurfaceZ = 0.0f;
	float OutsideBowlSurfaceZ = 0.0f;

	TestTrue(TEXT("Bowl foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Bowl foundation terrain profile uses planar distance to shape the basin"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("DistanceToPoint")));
	TestTrue(TEXT("Bowl center top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D::ZeroVector, CenterSurfaceZ));
	TestTrue(TEXT("Outside-bowl top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D(90.0f, 0.0f), OutsideBowlSurfaceZ));
	TestTrue(
		*FString::Printf(TEXT("Bowl terrain profile lowers center relative to outside. Center=%f Outside=%f"), CenterSurfaceZ, OutsideBowlSurfaceZ),
		CenterSurfaceZ < OutsideBowlSurfaceZ - 2.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseRollingHillsFoundationTerrainProfileCreatesBroadReliefTest,
	"PorismExtension.Biome.IslandNoise.RollingHillsFoundationTerrainProfileCreatesBroadRelief",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseRollingHillsFoundationTerrainProfileCreatesBroadReliefTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FRollingHillsFoundationTerrainProfilePayload RollingProfile;
	RollingProfile.HillHeightBlocks = 40.0f;
	RollingProfile.ValleyDepthBlocks = 12.0f;
	RollingProfile.HillScale = 0.45f;
	RollingProfile.DetailAmplitude = 0.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRollingHillsFoundationTerrainProfilePayload>(RollingProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float MinSurfaceZ = 0.0f;
	float MaxSurfaceZ = 0.0f;
	TArray<FVector2D> Samples;
	for (float X = -100.0f; X <= 100.0f; X += 10.0f)
	{
		for (float Y = -100.0f; Y <= 100.0f; Y += 10.0f)
		{
			if (FVector2D(X, Y).SizeSquared() <= FMath::Square(105.0f))
			{
				Samples.Add(FVector2D(X, Y));
			}
		}
	}

	TestTrue(TEXT("Rolling Hills foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Rolling Hills foundation terrain profile uses broad fractal noise"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("FractalFBm")));
	TestTrue(TEXT("Rolling Hills foundation terrain profile top solid samples are found"), FindTopSolidFoundationZRangeBlocks(GenA, ScaleContext, Samples, MinSurfaceZ, MaxSurfaceZ));
	TestTrue(
		*FString::Printf(TEXT("Rolling Hills terrain profile creates broad surface relief. Min=%f Max=%f"), MinSurfaceZ, MaxSurfaceZ),
		MaxSurfaceZ - MinSurfaceZ >= 24.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseErodedEdgeFoundationTerrainProfileLowersRimTest,
	"PorismExtension.Biome.IslandNoise.ErodedEdgeFoundationTerrainProfileLowersRim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseErodedEdgeFoundationTerrainProfileLowersRimTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FErodedEdgeFoundationTerrainProfilePayload ErodedProfile;
	ErodedProfile.EdgeWidthBlocks = 30.0f;
	ErodedProfile.EdgeDepthBlocks = 18.0f;
	ErodedProfile.EdgeNoiseAmplitude = 0.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FErodedEdgeFoundationTerrainProfilePayload>(ErodedProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float CenterSurfaceZ = 0.0f;
	float EdgeSurfaceZ = 0.0f;

	TestTrue(TEXT("Eroded Edge foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Eroded Edge foundation terrain profile uses planar distance"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("DistanceToPoint")));
	TestTrue(TEXT("Eroded Edge center top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D::ZeroVector, CenterSurfaceZ));
	TestTrue(TEXT("Eroded Edge rim top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D(112.0f, 0.0f), EdgeSurfaceZ));
	TestTrue(
		*FString::Printf(TEXT("Eroded Edge terrain profile lowers rim relative to center. Center=%f Edge=%f"), CenterSurfaceZ, EdgeSurfaceZ),
		EdgeSurfaceZ < CenterSurfaceZ - 8.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseCreviceFoundationTerrainProfileCutsSurfaceTest,
	"PorismExtension.Biome.IslandNoise.CreviceFoundationTerrainProfileCutsSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseCreviceFoundationTerrainProfileCutsSurfaceTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FCreviceFoundationTerrainProfilePayload CreviceProfile;
	CreviceProfile.CreviceDepthBlocks = 35.0f;
	CreviceProfile.CreviceScale = 0.7f;
	CreviceProfile.CreviceSharpness = 1.2f;
	CreviceProfile.SurfaceNoiseAmplitude = 0.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FCreviceFoundationTerrainProfilePayload>(CreviceProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float MinSurfaceZ = 0.0f;
	float MaxSurfaceZ = 0.0f;
	TArray<FVector2D> Samples;
	for (float X = -100.0f; X <= 100.0f; X += 10.0f)
	{
		for (float Y = -100.0f; Y <= 100.0f; Y += 10.0f)
		{
			if (FVector2D(X, Y).SizeSquared() <= FMath::Square(105.0f))
			{
				Samples.Add(FVector2D(X, Y));
			}
		}
	}

	TestTrue(TEXT("Crevice foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Crevice foundation terrain profile uses ridged fractal noise for crack masks"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("FractalRidged")));
	TestTrue(TEXT("Crevice foundation terrain profile top solid samples are found"), FindTopSolidFoundationZRangeBlocks(GenA, ScaleContext, Samples, MinSurfaceZ, MaxSurfaceZ));
	TestTrue(
		*FString::Printf(TEXT("Crevice terrain profile cuts some surface samples downward. Min=%f Max=%f"), MinSurfaceZ, MaxSurfaceZ),
		MaxSurfaceZ - MinSurfaceZ >= 10.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseRidgedFoundationTerrainProfileRaisesSurfaceTest,
	"PorismExtension.Biome.IslandNoise.RidgedFoundationTerrainProfileRaisesSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseRidgedFoundationTerrainProfileRaisesSurfaceTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FRidgedFoundationTerrainProfilePayload RidgedProfile;
	RidgedProfile.RidgeHeightBlocks = 200.0f;
	RidgedProfile.ValleyDepthBlocks = 40.0f;
	RidgedProfile.RidgeScale = 1.0f;
	RidgedProfile.RidgeSharpness = 1.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRidgedFoundationTerrainProfilePayload>(RidgedProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float MinSurfaceZ = 0.0f;
	float MaxSurfaceZ = 0.0f;
	TArray<FVector2D> Samples;
	for (float X = -100.0f; X <= 100.0f; X += 10.0f)
	{
		for (float Y = -100.0f; Y <= 100.0f; Y += 10.0f)
		{
			if (FVector2D(X, Y).SizeSquared() <= FMath::Square(105.0f))
			{
				Samples.Add(FVector2D(X, Y));
			}
		}
	}

	TestTrue(TEXT("Ridged foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Ridged foundation terrain profile uses ridged fractal noise"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("FractalRidged")));
	TestTrue(TEXT("Ridged foundation terrain profile top solid samples are found"), FindTopSolidFoundationZRangeBlocks(GenA, ScaleContext, Samples, MinSurfaceZ, MaxSurfaceZ));
	TestTrue(
		*FString::Printf(TEXT("Ridged terrain profile creates a wide surface range. Min=%f Max=%f"), MinSurfaceZ, MaxSurfaceZ),
		MaxSurfaceZ - MinSurfaceZ >= 140.0f);
	TestTrue(
		*FString::Printf(TEXT("Ridged terrain profile reaches substantial positive heights. Max=%f"), MaxSurfaceZ),
		MaxSurfaceZ >= 140.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseTerracedFoundationTerrainProfileBuildsStepsTest,
	"PorismExtension.Biome.IslandNoise.TerracedFoundationTerrainProfileBuildsSteps",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseTerracedFoundationTerrainProfileBuildsStepsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();

	FTerracedFoundationTerrainProfilePayload TerracedProfile;
	TerracedProfile.SurfaceNoiseAmplitude = 12.0f;
	TerracedProfile.TerraceStepHeight = 3.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FTerracedFoundationTerrainProfilePayload>(TerracedProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);

	TestTrue(TEXT("Terraced foundation terrain profile builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Terraced foundation terrain profile adds terrace node"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("Terrace")));
	TestTrue(TEXT("Terraced foundation terrain profile keeps simplex source explicit"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBowlFoundationTerrainProfileBuildsOptionalRaisedRimTest,
	"PorismExtension.Biome.IslandNoise.BowlFoundationTerrainProfileBuildsOptionalRaisedRim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBowlFoundationTerrainProfileBuildsOptionalRaisedRimTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FBowlFoundationTerrainProfilePayload BowlProfile;
	BowlProfile.RadiusBlocks = 60.0f;
	BowlProfile.DepthBlocks = 8.0f;
	BowlProfile.bEnableRaisedRim = true;
	BowlProfile.RimWidthBlocks = 18.0f;
	BowlProfile.RimHeightBlocks = 10.0f;
	BowlProfile.SurfaceNoiseAmplitude = 0.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FBowlFoundationTerrainProfilePayload>(BowlProfile);

	const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(Editor, Strategy);
	float CenterSurfaceZ = 0.0f;
	float RimSurfaceZ = 0.0f;
	float OutsideRimSurfaceZ = 0.0f;

	TestTrue(TEXT("Bowl profile with raised rim builds valid GenA"), GenA.Node != nullptr);
	TestTrue(TEXT("Bowl profile with raised rim uses planar distance"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("DistanceToPoint")));
	TestTrue(TEXT("Bowl raised-rim center top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D::ZeroVector, CenterSurfaceZ));
	TestTrue(TEXT("Bowl raised-rim ring top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D(60.0f, 0.0f), RimSurfaceZ));
	TestTrue(TEXT("Bowl raised-rim outside top solid sample is found"), FindTopSolidFoundationZBlocks(GenA, ScaleContext, FVector2D(95.0f, 0.0f), OutsideRimSurfaceZ));
	TestTrue(
		*FString::Printf(TEXT("Bowl raised-rim profile raises rim above outside terrain. Rim=%f Outside=%f"), RimSurfaceZ, OutsideRimSurfaceZ),
		RimSurfaceZ > OutsideRimSurfaceZ + 2.0f);
	TestTrue(
		*FString::Printf(TEXT("Bowl raised-rim profile depresses center below outside terrain. Center=%f Outside=%f"), CenterSurfaceZ, OutsideRimSurfaceZ),
		CenterSurfaceZ < OutsideRimSurfaceZ - 2.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseFoundationTerrainProfilesStayOutOfDomainNoiseTest,
	"PorismExtension.Biome.IslandNoise.FoundationTerrainProfilesStayOutOfDomainNoise",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseFoundationTerrainProfilesStayOutOfDomainNoiseTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> NoisyDomainNodes;
	UFastNoiseEditor* const NoisyDomainEditor = CreateTestFastNoiseEditor(NoisyDomainNodes);
	UBiomeStrategyData* const NoisyStrategy = CreateNoDetailMainMenuIslandStrategy();
	NoisyStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
	const FNodeLink NoisyDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(NoisyDomainEditor, NoisyStrategy);

	std::vector<FNodeLink> BowlDomainNodes;
	UFastNoiseEditor* const BowlDomainEditor = CreateTestFastNoiseEditor(BowlDomainNodes);
	UBiomeStrategyData* const BowlStrategy = CreateNoDetailMainMenuIslandStrategy();
	BowlStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FBowlFoundationTerrainProfilePayload>();
	const FNodeLink BowlDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(BowlDomainEditor, BowlStrategy);

	std::vector<FNodeLink> RollingDomainNodes;
	UFastNoiseEditor* const RollingDomainEditor = CreateTestFastNoiseEditor(RollingDomainNodes);
	UBiomeStrategyData* const RollingStrategy = CreateNoDetailMainMenuIslandStrategy();
	RollingStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRollingHillsFoundationTerrainProfilePayload>();
	const FNodeLink RollingDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(RollingDomainEditor, RollingStrategy);

	std::vector<FNodeLink> ErodedDomainNodes;
	UFastNoiseEditor* const ErodedDomainEditor = CreateTestFastNoiseEditor(ErodedDomainNodes);
	UBiomeStrategyData* const ErodedStrategy = CreateNoDetailMainMenuIslandStrategy();
	ErodedStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FErodedEdgeFoundationTerrainProfilePayload>();
	const FNodeLink ErodedDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(ErodedDomainEditor, ErodedStrategy);

	std::vector<FNodeLink> CreviceDomainNodes;
	UFastNoiseEditor* const CreviceDomainEditor = CreateTestFastNoiseEditor(CreviceDomainNodes);
	UBiomeStrategyData* const CreviceStrategy = CreateNoDetailMainMenuIslandStrategy();
	CreviceStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FCreviceFoundationTerrainProfilePayload>();
	const FNodeLink CreviceDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(CreviceDomainEditor, CreviceStrategy);

	std::vector<FNodeLink> RidgedDomainNodes;
	UFastNoiseEditor* const RidgedDomainEditor = CreateTestFastNoiseEditor(RidgedDomainNodes);
	UBiomeStrategyData* const RidgedStrategy = CreateNoDetailMainMenuIslandStrategy();
	RidgedStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRidgedFoundationTerrainProfilePayload>();
	const FNodeLink RidgedDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(RidgedDomainEditor, RidgedStrategy);

	std::vector<FNodeLink> TerracedDomainNodes;
	UFastNoiseEditor* const TerracedDomainEditor = CreateTestFastNoiseEditor(TerracedDomainNodes);
	UBiomeStrategyData* const TerracedStrategy = CreateNoDetailMainMenuIslandStrategy();
	TerracedStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FTerracedFoundationTerrainProfilePayload>();
	const FNodeLink TerracedDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(TerracedDomainEditor, TerracedStrategy);

	TestTrue(TEXT("Noisy foundation terrain profile leaves DomainNoise valid"), NoisyDomain.Node != nullptr);
	TestTrue(TEXT("Bowl foundation terrain profile leaves DomainNoise valid"), BowlDomain.Node != nullptr);
	TestTrue(TEXT("Rolling Hills foundation terrain profile leaves DomainNoise valid"), RollingDomain.Node != nullptr);
	TestTrue(TEXT("Eroded Edge foundation terrain profile leaves DomainNoise valid"), ErodedDomain.Node != nullptr);
	TestTrue(TEXT("Crevice foundation terrain profile leaves DomainNoise valid"), CreviceDomain.Node != nullptr);
	TestTrue(TEXT("Ridged foundation terrain profile leaves DomainNoise valid"), RidgedDomain.Node != nullptr);
	TestTrue(TEXT("Terraced foundation terrain profile leaves DomainNoise valid"), TerracedDomain.Node != nullptr);
	TestFalse(TEXT("Noisy terrain profile detail stays out of foundation DomainNoise"), NoisyDomain.Node != nullptr && NoisyDomain.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Bowl terrain profile detail stays out of foundation DomainNoise"), BowlDomain.Node != nullptr && BowlDomain.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Rolling Hills terrain profile detail stays out of foundation DomainNoise"), RollingDomain.Node != nullptr && RollingDomain.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Eroded Edge terrain profile detail stays out of foundation DomainNoise"), ErodedDomain.Node != nullptr && ErodedDomain.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Crevice terrain profile detail stays out of foundation DomainNoise"), CreviceDomain.Node != nullptr && CreviceDomain.Node->Flow.Contains(TEXT("FractalRidged")));
	TestFalse(TEXT("Ridged terrain profile detail stays out of foundation DomainNoise"), RidgedDomain.Node != nullptr && RidgedDomain.Node->Flow.Contains(TEXT("FractalRidged")));
	TestFalse(TEXT("Terraced terrain profile detail stays out of foundation DomainNoise"), TerracedDomain.Node != nullptr && TerracedDomain.Node->Flow.Contains(TEXT("Terrace")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseFoundationTerrainProfilesMaintainCoreTerrainInvariantsTest,
	"PorismExtension.Biome.IslandNoise.FoundationTerrainProfilesMaintainCoreTerrainInvariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseFoundationTerrainProfilesMaintainCoreTerrainInvariantsTest::RunTest(const FString& Parameters)
{
	constexpr float CenterZBlocks = 37.0f;
	constexpr float TopHeightBlocks = 30.0f;
	constexpr float BottomDepthBlocks = 95.0f;
	constexpr float AuthoredBottomZBlocks = CenterZBlocks - TopHeightBlocks - BottomDepthBlocks;
	const TArray<FVector2D> Samples = GetFoundationTerrainProfileInvariantSamples();
	const EFoundationTerrainProfileInvariantCase ProfileCases[] = {
		EFoundationTerrainProfileInvariantCase::Flat,
		EFoundationTerrainProfileInvariantCase::Noisy,
		EFoundationTerrainProfileInvariantCase::RollingHills,
		EFoundationTerrainProfileInvariantCase::ErodedEdge,
		EFoundationTerrainProfileInvariantCase::Crevice,
		EFoundationTerrainProfileInvariantCase::Bowl,
		EFoundationTerrainProfileInvariantCase::Ridged,
		EFoundationTerrainProfileInvariantCase::Terraced
	};

	for (const EFoundationTerrainProfileInvariantCase ProfileCase : ProfileCases)
	{
		const FString ProfileName = LexToString(ProfileCase);
		UBiomeStrategyData* const Strategy = CreateFoundationTerrainProfileInvariantStrategy(ProfileCase, CenterZBlocks);
		const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

		std::vector<FNodeLink> GenANodes;
		UFastNoiseEditor* const GenAEditor = CreateTestFastNoiseEditor(GenANodes);
		const FNodeLink GenA = UIslandBiomeFastNoiseLibrary::BuildFoundationGenA(GenAEditor, Strategy, &ScaleContext);
		TestTrue(*FString::Printf(TEXT("%s profile builds valid GenA"), *ProfileName), GenA.Node != nullptr);

		float MinSurfaceZ = 0.0f;
		float MaxSurfaceZ = 0.0f;
		TestTrue(
			*FString::Printf(TEXT("%s profile finds top solid samples across representative XY points"), *ProfileName),
			FindTopSolidFoundationZRangeBlocks(GenA, ScaleContext, Samples, MinSurfaceZ, MaxSurfaceZ));
		TestTrue(
			*FString::Printf(TEXT("%s profile keeps top solids above the finite bottom. MinSurface=%f Bottom=%f"), *ProfileName, MinSurfaceZ, AuthoredBottomZBlocks),
			MinSurfaceZ > AuthoredBottomZBlocks + 1.0f);
		TestTrue(
			*FString::Printf(TEXT("%s profile keeps surface search in a plausible range for nonzero Center.Z. MaxSurface=%f"), *ProfileName, MaxSurfaceZ),
			MaxSurfaceZ < CenterZBlocks + 80.0f);

		for (const FVector2D& Sample : Samples)
		{
			float SurfaceZ = 0.0f;
			TestTrue(
				*FString::Printf(TEXT("%s profile finds top solid at %s"), *ProfileName, *Sample.ToString()),
				FindTopSolidFoundationZBlocks(GenA, ScaleContext, Sample, SurfaceZ));

			float SolidValue = 0.0f;
			float AirAboveValue = 0.0f;
			TestTrue(
				*FString::Printf(TEXT("%s profile samples top solid density at %s"), *ProfileName, *Sample.ToString()),
				EvaluateNoiseAtAuthoredBlock(GenA, ScaleContext, FVector(Sample.X, Sample.Y, SurfaceZ), SolidValue));
			TestTrue(
				*FString::Printf(TEXT("%s profile samples air above top solid at %s"), *ProfileName, *Sample.ToString()),
				EvaluateNoiseAtAuthoredBlock(GenA, ScaleContext, FVector(Sample.X, Sample.Y, SurfaceZ + 1.0f), AirAboveValue));
			TestTrue(
				*FString::Printf(TEXT("%s profile keeps Porism terrain polarity solid at top. XY=%s Value=%f"), *ProfileName, *Sample.ToString(), SolidValue),
				SolidValue <= 0.0f);
			TestTrue(
				*FString::Printf(TEXT("%s profile keeps Porism terrain polarity air above top. XY=%s Value=%f"), *ProfileName, *Sample.ToString(), AirAboveValue),
				AirAboveValue > 0.0f);
		}

		float JustAboveBottom = 0.0f;
		float JustBelowBottom = 0.0f;
		float OutsideRimAir = 0.0f;
		TestTrue(
			*FString::Printf(TEXT("%s profile samples just above finite bottom"), *ProfileName),
			EvaluateNoiseAtAuthoredBlock(GenA, ScaleContext, FVector(0.0f, 0.0f, AuthoredBottomZBlocks + 1.0f), JustAboveBottom));
		TestTrue(
			*FString::Printf(TEXT("%s profile samples just below finite bottom"), *ProfileName),
			EvaluateNoiseAtAuthoredBlock(GenA, ScaleContext, FVector(0.0f, 0.0f, AuthoredBottomZBlocks - 1.0f), JustBelowBottom));
		TestTrue(
			*FString::Printf(TEXT("%s profile samples outside rim air"), *ProfileName),
			EvaluateNoiseAtAuthoredBlock(GenA, ScaleContext, FVector(150.0f, 0.0f, CenterZBlocks - 1.0f), OutsideRimAir));
		TestTrue(
			*FString::Printf(TEXT("%s profile keeps center solid just above the finite bottom. Value=%f"), *ProfileName, JustAboveBottom),
			JustAboveBottom <= 0.0f);
		TestTrue(
			*FString::Printf(TEXT("%s profile keeps air just below the finite bottom. Value=%f"), *ProfileName, JustBelowBottom),
			JustBelowBottom > 0.0f);
		TestTrue(
			*FString::Printf(TEXT("%s profile keeps outside-rim samples as air. Value=%f"), *ProfileName, OutsideRimAir),
			OutsideRimAir > 0.0f);

		std::vector<FNodeLink> DomainNodes;
		UFastNoiseEditor* const DomainEditor = CreateTestFastNoiseEditor(DomainNodes);
		const FNodeLink Domain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(DomainEditor, Strategy, &ScaleContext);
		TestTrue(*FString::Printf(TEXT("%s profile builds valid DomainNoise"), *ProfileName), Domain.Node != nullptr);
		TestFalse(
			*FString::Printf(TEXT("%s profile keeps terrain-detail source noise out of DomainNoise"), *ProfileName),
			Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("OpenSimplex2")));
		TestFalse(
			*FString::Printf(TEXT("%s profile keeps ridged terrain detail out of DomainNoise"), *ProfileName),
			Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("FractalRidged")));
		TestFalse(
			*FString::Printf(TEXT("%s profile keeps terrace terrain detail out of DomainNoise"), *ProfileName),
			Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("Terrace")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseRaisedTerrainProfileExpandsFoundationDomainTopTest,
	"PorismExtension.Biome.IslandNoise.RaisedTerrainProfileExpandsFoundationDomainTop",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseRaisedTerrainProfileExpandsFoundationDomainTopTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	if (FIslandFoundationShapePayload* IslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FIslandFoundationShapePayload>())
	{
		IslandPayload->DomainVerticalPadding = 0.0f;
	}

	FRidgedFoundationTerrainProfilePayload RidgedProfile;
	RidgedProfile.RidgeHeightBlocks = 50.0f;
	RidgedProfile.ValleyDepthBlocks = 0.0f;
	RidgedProfile.RidgeScale = 1.0f;
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FRidgedFoundationTerrainProfilePayload>(RidgedProfile);

	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy);
	float DomainWithinRaisedHeadroom = 0.0f;
	float DomainAboveRaisedHeadroom = 0.0f;

	TestTrue(TEXT("Raised terrain-profile foundation domain builds valid DomainNoise"), FoundationDomain.Node != nullptr);
	TestFalse(TEXT("Raised terrain profile still keeps ridge detail out of DomainNoise"), FoundationDomain.Node != nullptr && FoundationDomain.Node->Flow.Contains(TEXT("FractalRidged")));
	TestTrue(TEXT("Foundation domain samples inside terrain-profile headroom"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, 45.0f), DomainWithinRaisedHeadroom));
	TestTrue(TEXT("Foundation domain samples above terrain-profile headroom"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, 60.0f), DomainAboveRaisedHeadroom));
	TestTrue(TEXT("Raised terrain profile expands the cheap foundation domain top"), DomainWithinRaisedHeadroom > 0.0f);
	TestTrue(TEXT("Foundation domain still exits above the raised terrain-profile allowance"), DomainAboveRaisedHeadroom < 0.0f);

	return true;
}

