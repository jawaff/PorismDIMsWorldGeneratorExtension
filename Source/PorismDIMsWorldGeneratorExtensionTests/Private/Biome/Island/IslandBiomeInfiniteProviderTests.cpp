// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInfinitePlaneProviderSupportsSurfaceQueriesTest,
	"PorismExtension.Biome.IslandNoise.InfinitePlaneProviderSupportsSurfaceQueries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInfinitePlaneProviderSupportsSurfaceQueriesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateInfinitePlaneSurfaceAnchorStrategy();

	const FFoundationProviderQueryCapabilities Capabilities = UBiomeStrategyData::GetProviderQueryCapabilities(Strategy->RootFoundationProvider);
	TestEqual(TEXT("Infinite plane exposes infinite surface queries"), Capabilities.SurfaceQueryMode, EFoundationProviderSurfaceQueryMode::Infinite);
	TestEqual(TEXT("Infinite plane exposes infinite volume queries"), Capabilities.VolumeQueryMode, EFoundationProviderVolumeQueryMode::Infinite);

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestFalse(TEXT("Fixed surface anchors validate on infinite providers"), Validation.HasErrors());

	FResolvedFoundationSurface Surface;
	FFoundationSurfaceQuery SurfaceQuery;
	SurfaceQuery.CenterXY = FVector2D(1000.0f, -250.0f);
	TestTrue(TEXT("Infinite plane surface query succeeds at arbitrary XY"), UBiomeStrategyData::QueryProviderSurface(
		Strategy->RootFoundationProvider,
		TEXT("RootFoundationProvider"),
		SurfaceQuery,
		ResolveTestScaleContext(Strategy),
		Surface));
	TestTrue(TEXT("Infinite plane surface stays flat at configured Z"), FMath::IsNearlyEqual(Surface.SurfaceZBlock, 0.0f, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInfinitePlaneBiomeDomainAndGenAGenerateTerrainTest,
	"PorismExtension.Biome.IslandNoise.InfinitePlaneBiomeDomainAndGenAGenerateTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInfinitePlaneBiomeDomainAndGenAGenerateTerrainTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateInfinitePlaneSurfaceAnchorStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> DomainNodes;
	UFastNoiseEditor* const DomainEditor = CreateTestFastNoiseEditor(DomainNodes);
	const FNodeLink FoundationDomain = Strategy->BuildBiomeNoise(DomainEditor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	std::vector<FNodeLink> LegacyDomainNodes;
	UFastNoiseEditor* const LegacyDomainEditor = CreateTestFastNoiseEditor(LegacyDomainNodes);
	const FNodeLink LegacyFoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(LegacyDomainEditor, Strategy, &ScaleContext);

	std::vector<FNodeLink> GenANodes;
	UFastNoiseEditor* const GenAEditor = CreateTestFastNoiseEditor(GenANodes);
	const FNodeLink FoundationGenA = Strategy->BuildBiomeNoise(GenAEditor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestFoundationBiomeTag());

	float DomainBelowSurface = 0.0f;
	float LegacyDomainBelowSurface = 0.0f;
	float DomainAboveBand = 0.0f;
	float DensityAtSurface = 0.0f;
	float DensityBelowSurface = 0.0f;
	float DensityAboveSurface = 0.0f;
	TestTrue(TEXT("Infinite plane domain samples below reservation carve depth"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(500.0f, -500.0f, -20.0f), DomainBelowSurface));
	TestTrue(TEXT("Legacy foundation domain helper samples infinite plane below surface"), EvaluateNoiseAtAuthoredBlock(LegacyFoundationDomain, ScaleContext, FVector(500.0f, -500.0f, -20.0f), LegacyDomainBelowSurface));
	TestTrue(TEXT("Infinite plane domain samples above configured band"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(500.0f, -500.0f, 40.0f), DomainAboveBand));
	TestTrue(TEXT("Infinite plane GenA samples at authored surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(500.0f, -500.0f, 0.0f), DensityAtSurface));
	TestTrue(TEXT("Infinite plane GenA samples below surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(500.0f, -500.0f, -1.0f), DensityBelowSurface));
	TestTrue(TEXT("Infinite plane GenA samples above surface"), EvaluateNoiseAtAuthoredBlock(FoundationGenA, ScaleContext, FVector(500.0f, -500.0f, 2.0f), DensityAboveSurface));

	TestTrue(TEXT("Infinite plane domain owns the configured vertical band"), DomainBelowSurface > 0.0f);
	TestTrue(TEXT("Legacy foundation domain helper uses generic provider path"), LegacyDomainBelowSurface > 0.0f);
	TestTrue(TEXT("Infinite plane domain exits above the configured top band"), DomainAboveBand < 0.0f);
	TestTrue(TEXT("Infinite plane authored surface is the visible top face"), DensityAtSurface > 0.0f);
	TestTrue(TEXT("Infinite plane GenA is solid below the surface"), DensityBelowSurface <= 0.0f);
	TestTrue(TEXT("Infinite plane GenA is air above the surface"), DensityAboveSurface > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInfinitePlaneNoisePatchMaskStaysOutOfGenATest,
	"PorismExtension.Biome.IslandNoise.InfinitePlaneNoisePatchMaskStaysOutOfGenA",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInfinitePlaneNoisePatchMaskStaysOutOfGenATest::RunTest(const FString& Parameters)
{
	FInfiniteFoundationNoisePatchDomainMaskPayload NoisePatch;
	NoisePatch.FrequencyScale = 0.4f;
	NoisePatch.Threshold = 0.0f;
	NoisePatch.EdgeSoftness = 0.0f;
	NoisePatch.SeedOffset = 17;

	UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	Strategy->bUseScaleOverride = true;
	Strategy->RootFoundationProvider = CreateInfinitePlaneProvider(TEXT("PatchPlane"), TestFoundationBiomeTag(), FInstancedStruct::Make(NoisePatch));

	std::vector<FNodeLink> DomainNodes;
	UFastNoiseEditor* const DomainEditor = CreateTestFastNoiseEditor(DomainNodes);
	const FNodeLink Domain = Strategy->BuildBiomeNoise(DomainEditor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	std::vector<FNodeLink> GenANodes;
	UFastNoiseEditor* const GenAEditor = CreateTestFastNoiseEditor(GenANodes);
	const FNodeLink GenA = Strategy->BuildBiomeNoise(GenAEditor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestFoundationBiomeTag());

	TestTrue(TEXT("Noise patch mask builds a valid domain"), Domain.Node != nullptr);
	TestTrue(TEXT("Noise patch mask contributes simplex noise to DomainNoise"), Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestTrue(TEXT("Noise patch mask contributes fractal patching to DomainNoise"), Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("FractalFBm")));
	TestTrue(TEXT("Flat infinite-plane GenA remains valid"), GenA.Node != nullptr);
	TestFalse(TEXT("Domain mask noise stays out of flat infinite-plane GenA"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("OpenSimplex2")));
	TestFalse(TEXT("Domain mask fractal stays out of flat infinite-plane GenA"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("FractalFBm")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInfinitePlaneRepeatedHolesMaskCutsDomainOnlyTest,
	"PorismExtension.Biome.IslandNoise.InfinitePlaneRepeatedHolesMaskCutsDomainOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInfinitePlaneRepeatedHolesMaskCutsDomainOnlyTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	Strategy->bUseScaleOverride = true;
	Strategy->RootFoundationProvider = CreateInfinitePlaneProvider(TEXT("HoleyPlane"), TestFoundationBiomeTag(), CreateRepeatedHolesDomainMask(false));
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> DomainNodes;
	UFastNoiseEditor* const DomainEditor = CreateTestFastNoiseEditor(DomainNodes);
	const FNodeLink Domain = Strategy->BuildBiomeNoise(DomainEditor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	std::vector<FNodeLink> GenANodes;
	UFastNoiseEditor* const GenAEditor = CreateTestFastNoiseEditor(GenANodes);
	const FNodeLink GenA = Strategy->BuildBiomeNoise(GenAEditor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestFoundationBiomeTag());

	FVector OwnedSample = FVector::ZeroVector;
	FVector HoleSample = FVector::ZeroVector;
	TestTrue(TEXT("Repeated holes domain has both owned and cutout samples"), FindPositiveAndNegativeDomainSamples(Domain, ScaleContext, OwnedSample, HoleSample));
	TestTrue(TEXT("Repeated holes mask uses cellular distance in DomainNoise"), Domain.Node != nullptr && Domain.Node->Flow.Contains(TEXT("CellularDistance")));
	TestFalse(TEXT("Repeated holes mask stays out of flat infinite-plane GenA"), GenA.Node != nullptr && GenA.Node->Flow.Contains(TEXT("CellularDistance")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInfinitePlaneChildMaskCarvesParentBiomeTest,
	"PorismExtension.Biome.IslandNoise.InfinitePlaneChildMaskCarvesParentBiome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInfinitePlaneChildMaskCarvesParentBiomeTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	Strategy->bUseScaleOverride = true;
	Strategy->RootFoundationProvider = CreateInfinitePlaneProvider(TEXT("RootPlane"), TestFoundationBiomeTag(), FInstancedStruct::Make(FInfiniteFoundationNoDomainMaskPayload()));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(
		CreateInfinitePlaneProvider(TEXT("ChildPatches"), TestReservationBiomeTag(), CreateRepeatedHolesDomainMask(true))));
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink ParentDomain = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());
	const FNodeLink ChildDomain = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestReservationBiomeTag());

	std::vector<FNodeLink> ChildGenANodes;
	UFastNoiseEditor* const ChildGenAEditor = CreateTestFastNoiseEditor(ChildGenANodes);
	const FNodeLink ChildGenA = Strategy->BuildBiomeNoise(ChildGenAEditor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestReservationBiomeTag());

	FVector ChildPatchSample = FVector::ZeroVector;
	FVector ChildOutsideSample = FVector::ZeroVector;
	TestTrue(TEXT("Child patch domain has both owned and empty samples"), FindPositiveAndNegativeDomainSamples(ChildDomain, ScaleContext, ChildPatchSample, ChildOutsideSample));

	float ParentAtChildPatch = 0.0f;
	float ChildAtChildPatch = 0.0f;
	float ParentOutsideChildPatch = 0.0f;
	float ChildDensityBelowSurface = 0.0f;
	float ChildDensityAboveSurface = 0.0f;
	TestTrue(TEXT("Parent samples the child-owned infinite patch"), EvaluateNoiseAtAuthoredBlock(ParentDomain, ScaleContext, ChildPatchSample, ParentAtChildPatch));
	TestTrue(TEXT("Child samples its owned infinite patch"), EvaluateNoiseAtAuthoredBlock(ChildDomain, ScaleContext, ChildPatchSample, ChildAtChildPatch));
	TestTrue(TEXT("Parent samples outside child patch"), EvaluateNoiseAtAuthoredBlock(ParentDomain, ScaleContext, ChildOutsideSample, ParentOutsideChildPatch));
	TestTrue(TEXT("Child infinite-plane GenA samples below the patch surface"), EvaluateNoiseAtAuthoredBlock(ChildGenA, ScaleContext, FVector(ChildPatchSample.X, ChildPatchSample.Y, -1.0f), ChildDensityBelowSurface));
	TestTrue(TEXT("Child infinite-plane GenA samples above the patch surface"), EvaluateNoiseAtAuthoredBlock(ChildGenA, ScaleContext, FVector(ChildPatchSample.X, ChildPatchSample.Y, 2.0f), ChildDensityAboveSurface));

	TestTrue(*FString::Printf(TEXT("Different BiomeTag infinite child carves parent. Parent=%f Child=%f"), ParentAtChildPatch, ChildAtChildPatch), ParentAtChildPatch <= 0.0f);
	TestTrue(TEXT("Different BiomeTag infinite child owns its patch"), ChildAtChildPatch > 0.0f);
	TestTrue(TEXT("Parent infinite plane remains owned outside child patch"), ParentOutsideChildPatch > 0.0f);
	TestTrue(TEXT("Child infinite-plane GenA is solid below its authored surface"), ChildDensityBelowSurface <= 0.0f);
	TestTrue(TEXT("Child infinite-plane GenA is air above its authored surface"), ChildDensityAboveSurface > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseInfinitePlaneSameBiomeChildMaskUnionsWithParentTest,
	"PorismExtension.Biome.IslandNoise.InfinitePlaneSameBiomeChildMaskUnionsWithParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseInfinitePlaneSameBiomeChildMaskUnionsWithParentTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const BaselineStrategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	BaselineStrategy->bUseScaleOverride = true;
	BaselineStrategy->RootFoundationProvider = CreateInfinitePlaneProvider(TEXT("HoleyPlane"), TestFoundationBiomeTag(), CreateRepeatedHolesDomainMask(false));
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(BaselineStrategy);

	std::vector<FNodeLink> BaselineNodes;
	UFastNoiseEditor* const BaselineEditor = CreateTestFastNoiseEditor(BaselineNodes);
	const FNodeLink BaselineDomain = BaselineStrategy->BuildBiomeNoise(BaselineEditor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	FVector OwnedSample = FVector::ZeroVector;
	FVector HoleSample = FVector::ZeroVector;
	TestTrue(TEXT("Baseline repeated-holes domain exposes a hole sample"), FindPositiveAndNegativeDomainSamples(BaselineDomain, ScaleContext, OwnedSample, HoleSample));

	UBiomeStrategyData* const UnionStrategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	UnionStrategy->bUseScaleOverride = true;
	UnionStrategy->RootFoundationProvider = CreateInfinitePlaneProvider(TEXT("HoleyPlane"), TestFoundationBiomeTag(), CreateRepeatedHolesDomainMask(false));
	UnionStrategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(
		CreateInfinitePlaneProvider(TEXT("SameBiomeFill"), TestFoundationBiomeTag(), CreateRepeatedHolesDomainMask(true))));

	std::vector<FNodeLink> UnionNodes;
	UFastNoiseEditor* const UnionEditor = CreateTestFastNoiseEditor(UnionNodes);
	const FNodeLink UnionDomain = UnionStrategy->BuildBiomeNoise(UnionEditor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	float BaselineAtHole = 0.0f;
	float UnionAtHole = 0.0f;
	TestTrue(TEXT("Baseline samples repeated hole"), EvaluateNoiseAtAuthoredBlock(BaselineDomain, ScaleContext, HoleSample, BaselineAtHole));
	TestTrue(TEXT("Union samples repeated hole"), EvaluateNoiseAtAuthoredBlock(UnionDomain, ScaleContext, HoleSample, UnionAtHole));

	TestTrue(TEXT("Baseline same-tag hole starts carved out"), BaselineAtHole < 0.0f);
	TestTrue(TEXT("Same BiomeTag child provider unions its patch back into the parent biome"), UnionAtHole > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationRejectsInvalidInfiniteRepeatedHolesMaskTest,
	"PorismExtension.Biome.IslandNoise.ValidationRejectsInvalidInfiniteRepeatedHolesMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationRejectsInvalidInfiniteRepeatedHolesMaskTest::RunTest(const FString& Parameters)
{
	FInfiniteFoundationRepeatedHolesDomainMaskPayload RepeatedHoles;
	RepeatedHoles.CellSizeBlocks = 100.0f;
	RepeatedHoles.HoleRadiusBlocks = 55.0f;

	UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	Strategy->bUseScaleOverride = true;
	Strategy->RootFoundationProvider = CreateInfinitePlaneProvider(TEXT("InvalidHoles"), TestFoundationBiomeTag(), FInstancedStruct::Make(RepeatedHoles));

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestTrue(TEXT("Merged repeated holes are rejected"), Validation.HasErrors());
	TestTrue(TEXT("Repeated holes validation explains the half-cell radius rule"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("holes large enough to merge"))
					&& Issue.FixText.Contains(TEXT("below half"));
			}));

	return true;
}

