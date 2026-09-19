// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBuildsReservationSlotsTest,
	"PorismExtension.Biome.IslandNoise.BuildsReservationSlots",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBuildsReservationSlotsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink SpawnDomain = UIslandBiomeFastNoiseLibrary::BuildSpawnDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(Editor, Strategy, TestReservationBiomeTag());
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	float SpawnAtSurface = 0.0f;
	float SpawnAbove = 0.0f;

	TestTrue(TEXT("Reservation domain is valid"), ReservationDomain.Node != nullptr);
	TestTrue(TEXT("Spawn domain is valid"), SpawnDomain.Node != nullptr);
	TestTrue(TEXT("Reservation GenA is valid"), BiomeGenA.Node != nullptr);
	TestTrue(TEXT("Spawn domain samples at authored-block surface"), EvaluateNoiseAtAuthoredBlock(SpawnDomain, ScaleContext, FVector(0.0, 0.0, 0.0), SpawnAtSurface));
	TestTrue(TEXT("Spawn domain samples above its shallow authored-block sphere subregion"), EvaluateNoiseAtAuthoredBlock(SpawnDomain, ScaleContext, FVector(0.0, 0.0, 15.0), SpawnAbove));
	TestTrue(TEXT("Reservation domain is a shallow sphere surface-anchor mask"), ReservationDomain.Node != nullptr && ReservationDomain.Node->Flow.Contains(TEXT("PowInt")));
	TestTrue(TEXT("Spawn domain is positive at its configured surface"), SpawnAtSurface > 0.0f);
	TestTrue(TEXT("Spawn domain uses its smaller configured sphere subregion"), SpawnAbove < 0.0f);
	TestTrue(TEXT("Reservation GenA gates and caps its flat terrain plane by the reservation domain"),
		BiomeGenA.Node != nullptr
		&& BiomeGenA.Node->Flow.StartsWith(TEXT("MinFloat"))
		&& BiomeGenA.Node->Flow.Contains(TEXT("Max")));
	TestFalse(TEXT("Reservation GenA keeps transition blending disabled for the menu-ready anchor path"), BiomeGenA.Node != nullptr && BiomeGenA.Node->Flow.Contains(TEXT("Fade")));
	TestTrue(TEXT("Reservation domain is surface-local instead of an infinite vertical cylinder"), ReservationDomain.Node != nullptr && ReservationDomain.Node->Flow.Contains(TEXT("PositionOutput")));
	TestFalse(TEXT("Reservation domain omits terrain detail by default"), ReservationDomain.Node != nullptr && ReservationDomain.Node->Flow.Contains(TEXT("OpenSimplex2")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSpawnDomainStaysInsideReservationDomainTest,
	"PorismExtension.Biome.IslandNoise.SpawnDomainStaysInsideReservationDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSpawnDomainStaysInsideReservationDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink SpawnDomain = UIslandBiomeFastNoiseLibrary::BuildSpawnDomain(Editor, Strategy, TestReservationBiomeTag());

	float ReservationAtOuterPoint = 0.0f;
	float SpawnAtOuterPoint = 0.0f;
	TestTrue(TEXT("Reservation domain samples inside reservation but outside spawn"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(24.0, 0.0, 0.0), ReservationAtOuterPoint));
	TestTrue(TEXT("Spawn domain samples inside reservation but outside spawn"), EvaluateNoiseAtAuthoredBlock(SpawnDomain, ScaleContext, FVector(24.0, 0.0, 0.0), SpawnAtOuterPoint));

	TestTrue(TEXT("Reservation domain owns the outer reservation point"), ReservationAtOuterPoint > 0.0f);
	TestTrue(TEXT("Spawn domain is strictly inside the reservation domain"), SpawnAtOuterPoint < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSharedReservationTagUnionsReservationsTest,
	"PorismExtension.Biome.IslandNoise.SharedReservationTagUnionsReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSharedReservationTagUnionsReservationsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateTwoAnchorIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());

	float DomainAtLeft = 0.0f;
	float DomainAtRight = 0.0f;
	float DomainBetweenAnchors = 0.0f;
	TestTrue(TEXT("Reservation domain samples left anchor"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(-45.0, 0.0, 0.0), DomainAtLeft));
	TestTrue(TEXT("Reservation domain samples right anchor"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(45.0, 0.0, 0.0), DomainAtRight));
	TestTrue(TEXT("Reservation domain samples gap between anchors"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector::ZeroVector, DomainBetweenAnchors));

	TestTrue(TEXT("Shared reservation tag owns both authored anchors"), DomainAtLeft > 0.0f && DomainAtRight > 0.0f);
	TestTrue(TEXT("Shared reservation tag still keeps separated anchors separated"), DomainBetweenAnchors < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSameBiomeTagChildFoundationUnionsWithParentTest,
	"PorismExtension.Biome.IslandNoise.SameBiomeTagChildFoundationUnionsWithParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSameBiomeTagChildFoundationUnionsWithParentTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	const FFoundationProviderDefinition ChildProvider = CreateNoDetailIslandProvider(TEXT("Child"), TestFoundationBiomeTag(), FVector(120.0f, 0.0f, 0.0f), 24.0f);
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ChildProvider));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestFoundationBiomeTag(), &ScaleContext);

	float ParentCenter = 0.0f;
	float ChildCenter = 0.0f;
	float GapBetweenProviders = 0.0f;
	TestTrue(TEXT("Same-tag hierarchy samples parent center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector::ZeroVector, ParentCenter));
	TestTrue(TEXT("Same-tag hierarchy samples child center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(120.0f, 0.0f, 0.0f), ChildCenter));
	TestTrue(TEXT("Same-tag hierarchy samples gap between providers"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(90.0f, 0.0f, 0.0f), GapBetweenProviders));

	TestTrue(TEXT("Parent foundation remains owned"), ParentCenter > 0.0f);
	TestTrue(TEXT("Same BiomeTag child contributes to the same biome domain"), ChildCenter > 0.0f);
	TestTrue(TEXT("Union does not fill unrelated space between separated providers"), GapBetweenProviders < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseProviderLocalReservationPositionStaysStableWithChildFoundationTest,
	"PorismExtension.Biome.IslandNoise.ProviderLocalReservationPositionStaysStableWithChildFoundation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseProviderLocalReservationPositionStaysStableWithChildFoundationTest::RunTest(const FString& Parameters)
{
	const FVector RootTopCenter(40.0f, -25.0f, 12.0f);
	UBiomeStrategyData* const NoChildStrategy = CreateOffsetRootReservationStrategy(false);
	UBiomeStrategyData* const ChildStrategy = CreateOffsetRootReservationStrategy(true);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(ChildStrategy);

	std::vector<FNodeLink> NoChildNodes;
	UFastNoiseEditor* const NoChildEditor = CreateTestFastNoiseEditor(NoChildNodes);
	const FNodeLink NoChildReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(NoChildEditor, NoChildStrategy, TestReservationBiomeTag(), &ScaleContext);
	const FNodeLink NoChildReservationGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(NoChildEditor, NoChildStrategy, TestReservationBiomeTag(), &ScaleContext);

	std::vector<FNodeLink> ChildNodes;
	UFastNoiseEditor* const ChildEditor = CreateTestFastNoiseEditor(ChildNodes);
	const FNodeLink ChildReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(ChildEditor, ChildStrategy, TestReservationBiomeTag(), &ScaleContext);
	const FNodeLink ChildReservationGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(ChildEditor, ChildStrategy, TestReservationBiomeTag(), &ScaleContext);
	const FNodeLink ChildBiomeDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(ChildEditor, ChildStrategy, TestReservationBiomeTag(), &ScaleContext);
	const FNodeLink ChildBiomeGenA = UIslandBiomeFastNoiseLibrary::BuildBiomeGenA(ChildEditor, ChildStrategy, TestReservationBiomeTag(), &ScaleContext);

	float NoChildDomainAtRootCenter = 0.0f;
	float ChildDomainAtRootCenter = 0.0f;
	float ChildDomainAtWorldOrigin = 0.0f;
	float ChildBiomeDomainAtRootCenter = 0.0f;
	float ChildBiomeDomainAtWorldOrigin = 0.0f;
	TestTrue(TEXT("No-child reservation domain samples provider-local root top center"), EvaluateNoiseAtAuthoredBlock(NoChildReservationDomain, ScaleContext, RootTopCenter, NoChildDomainAtRootCenter));
	TestTrue(TEXT("Child reservation domain samples provider-local root top center"), EvaluateNoiseAtAuthoredBlock(ChildReservationDomain, ScaleContext, RootTopCenter, ChildDomainAtRootCenter));
	TestTrue(TEXT("Child reservation domain samples world origin"), EvaluateNoiseAtAuthoredBlock(ChildReservationDomain, ScaleContext, FVector::ZeroVector, ChildDomainAtWorldOrigin));
	TestTrue(TEXT("Child biome domain samples provider-local root top center"), EvaluateNoiseAtAuthoredBlock(ChildBiomeDomain, ScaleContext, RootTopCenter, ChildBiomeDomainAtRootCenter));
	TestTrue(TEXT("Child biome domain samples world origin"), EvaluateNoiseAtAuthoredBlock(ChildBiomeDomain, ScaleContext, FVector::ZeroVector, ChildBiomeDomainAtWorldOrigin));

	float NoChildSurfaceZ = 0.0f;
	float ChildSurfaceZ = 0.0f;
	float ChildBiomeSurfaceZ = 0.0f;
	TestTrue(TEXT("No-child reservation GenA surface can be inferred at provider top center XY"), InferFlatReservationSurfaceZBlocks(NoChildReservationGenA, ScaleContext, FVector2D(RootTopCenter.X, RootTopCenter.Y), NoChildSurfaceZ));
	TestTrue(TEXT("Child reservation GenA surface can be inferred at provider top center XY"), InferFlatReservationSurfaceZBlocks(ChildReservationGenA, ScaleContext, FVector2D(RootTopCenter.X, RootTopCenter.Y), ChildSurfaceZ));
	TestTrue(TEXT("Child biome GenA surface can be inferred at provider top center XY"), InferFlatReservationSurfaceZBlocks(ChildBiomeGenA, ScaleContext, FVector2D(RootTopCenter.X, RootTopCenter.Y), ChildBiomeSurfaceZ));

	TestTrue(TEXT("No-child reservation owns the provider-local top center"), NoChildDomainAtRootCenter > 0.0f);
	TestTrue(TEXT("Adding a child foundation does not move the root reservation domain"), ChildDomainAtRootCenter > 0.0f);
	TestTrue(*FString::Printf(TEXT("Provider-local root reservation is not positively owned at world origin. DomainAtOrigin=%f DomainAtRoot=%f"), ChildDomainAtWorldOrigin, ChildDomainAtRootCenter), ChildDomainAtWorldOrigin <= 0.0f);
	TestTrue(TEXT("Biome-domain path keeps provider-local root reservation at the root top center"), ChildBiomeDomainAtRootCenter > 0.0f);
	TestTrue(*FString::Printf(TEXT("Biome-domain path does not positively own provider-local root reservation at world origin. DomainAtOrigin=%f DomainAtRoot=%f"), ChildBiomeDomainAtWorldOrigin, ChildBiomeDomainAtRootCenter), ChildBiomeDomainAtWorldOrigin <= 0.0f);
	TestTrue(*FString::Printf(TEXT("No-child reservation surface follows the owning provider top-center Z. SurfaceZ=%f Expected=%f"), NoChildSurfaceZ, RootTopCenter.Z), FMath::IsNearlyEqual(NoChildSurfaceZ, RootTopCenter.Z, 0.6f));
	TestTrue(TEXT("Adding a child foundation does not shift the root reservation surface"), FMath::IsNearlyEqual(ChildSurfaceZ, NoChildSurfaceZ, 0.1f));
	TestTrue(TEXT("Biome GenA path keeps the same provider-local root reservation surface"), FMath::IsNearlyEqual(ChildBiomeSurfaceZ, NoChildSurfaceZ, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSharedReservationBiomeKeepsProviderLocalGenATest,
	"PorismExtension.Biome.IslandNoise.SharedReservationBiomeKeepsProviderLocalGenA",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSharedReservationBiomeKeepsProviderLocalGenATest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateSharedReservationBiomeHierarchyStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> ReservationNodes;
	UFastNoiseEditor* const ReservationEditor = CreateTestFastNoiseEditor(ReservationNodes);
	const FNodeLink ReservationGenA = UIslandBiomeFastNoiseLibrary::BuildReservationGenA(ReservationEditor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	std::vector<FNodeLink> BiomeNodes;
	UFastNoiseEditor* const BiomeEditor = CreateTestFastNoiseEditor(BiomeNodes);
	const FNodeLink BiomeGenA = UIslandBiomeFastNoiseLibrary::BuildBiomeGenA(BiomeEditor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	float ReservationRootSurfaceZ = 0.0f;
	float ReservationChildSurfaceZ = 0.0f;
	float BiomeRootSurfaceZ = 0.0f;
	float BiomeChildSurfaceZ = 0.0f;
	TestTrue(TEXT("Reservation GenA infers the root reservation surface"), InferFlatReservationSurfaceZBlocks(ReservationGenA, ScaleContext, FVector2D::ZeroVector, ReservationRootSurfaceZ));
	TestTrue(TEXT("Reservation GenA infers the child reservation surface"), InferFlatReservationSurfaceZBlocks(ReservationGenA, ScaleContext, FVector2D(160.0f, 0.0f), ReservationChildSurfaceZ));
	TestTrue(TEXT("Biome GenA infers the root reservation surface"), InferFlatReservationSurfaceZBlocks(BiomeGenA, ScaleContext, FVector2D::ZeroVector, BiomeRootSurfaceZ));
	TestTrue(TEXT("Biome GenA infers the child reservation surface"), InferFlatReservationSurfaceZBlocks(BiomeGenA, ScaleContext, FVector2D(160.0f, 0.0f), BiomeChildSurfaceZ));

	TestTrue(*FString::Printf(TEXT("Child SurfaceZLift does not raise the root reservation in reservation-only GenA. RootSurface=%f"), ReservationRootSurfaceZ), FMath::IsNearlyEqual(ReservationRootSurfaceZ, 0.0f, 0.6f));
	TestTrue(*FString::Printf(TEXT("Child SurfaceZLift raises only the child reservation in reservation-only GenA. ChildSurface=%f"), ReservationChildSurfaceZ), FMath::IsNearlyEqual(ReservationChildSurfaceZ, 5.0f, 0.6f));
	TestTrue(TEXT("Child SurfaceZLift does not raise the root reservation in biome GenA"), FMath::IsNearlyEqual(BiomeRootSurfaceZ, ReservationRootSurfaceZ, 0.1f));
	TestTrue(TEXT("Child SurfaceZLift raises the child reservation in biome GenA"), FMath::IsNearlyEqual(BiomeChildSurfaceZ, ReservationChildSurfaceZ, 0.1f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseDifferentBiomeTagChildFoundationCarvesParentTest,
	"PorismExtension.Biome.IslandNoise.DifferentBiomeTagChildFoundationCarvesParent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseDifferentBiomeTagChildFoundationCarvesParentTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	const FFoundationProviderDefinition ChildProvider = CreateNoDetailIslandProvider(TEXT("Child"), TestReservationBiomeTag(), FVector(20.0f, 0.0f, 0.0f), 18.0f);
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ChildProvider));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestFoundationBiomeTag(), &ScaleContext);
	const FNodeLink ChildBiomeDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	float FoundationAtChild = 0.0f;
	float ChildAtChild = 0.0f;
	float FoundationAwayFromChild = 0.0f;
	TestTrue(TEXT("Parent biome samples child center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(20.0f, 0.0f, 0.0f), FoundationAtChild));
	TestTrue(TEXT("Child biome samples child center"), EvaluateNoiseAtAuthoredBlock(ChildBiomeDomain, ScaleContext, FVector(20.0f, 0.0f, 0.0f), ChildAtChild));
	TestTrue(TEXT("Parent biome samples away from child"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(-40.0f, 0.0f, 0.0f), FoundationAwayFromChild));

	TestTrue(*FString::Printf(TEXT("Different BiomeTag child carves its parent branch. FoundationAtChild=%f ChildAtChild=%f"), FoundationAtChild, ChildAtChild), FoundationAtChild <= 0.0f);
	TestTrue(TEXT("Different BiomeTag child contributes to its own biome"), ChildAtChild > 0.0f);
	TestTrue(TEXT("Parent branch remains owned away from the child carve"), FoundationAwayFromChild > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSubtractiveVoidChildFoundationCarvesWithoutBiomeTest,
	"PorismExtension.Biome.IslandNoise.SubtractiveVoidChildFoundationCarvesWithoutBiome",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSubtractiveVoidChildFoundationCarvesWithoutBiomeTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	FFoundationProviderDefinition VoidProvider = CreateNoDetailIslandProvider(TEXT("Void"), FGameplayTag(), FVector(20.0f, 0.0f, 0.0f), 18.0f);
	VoidProvider.ContributionType = EFoundationContributionType::SubtractiveVoid;
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(VoidProvider));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestFoundationBiomeTag(), &ScaleContext);
	const FNodeLink ReservationBiomeDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	float FoundationAtVoid = 0.0f;
	float FoundationAwayFromVoid = 0.0f;
	float ReservationAtVoid = 0.0f;
	TestTrue(TEXT("Parent biome samples void child center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(20.0f, 0.0f, 0.0f), FoundationAtVoid));
	TestTrue(TEXT("Parent biome samples away from void child"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(-40.0f, 0.0f, 0.0f), FoundationAwayFromVoid));
	TestTrue(TEXT("Unrelated biome samples void child center"), EvaluateNoiseAtAuthoredBlock(ReservationBiomeDomain, ScaleContext, FVector(20.0f, 0.0f, 0.0f), ReservationAtVoid));

	TestTrue(*FString::Printf(TEXT("Subtractive void child carves the parent branch. FoundationAtVoid=%f ReservationAtVoid=%f"), FoundationAtVoid, ReservationAtVoid), FoundationAtVoid <= 0.0f);
	TestTrue(TEXT("Parent branch remains owned away from the void carve"), FoundationAwayFromVoid > 0.0f);
	TestTrue(TEXT("Subtractive void child does not add an unrelated biome contribution"), ReservationAtVoid <= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseVCutVoidChildNarrowsWithDepthTest,
	"PorismExtension.Biome.IslandNoise.VCutVoidChildNarrowsWithDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseVCutVoidChildNarrowsWithDepthTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

	FVCutFoundationPayload VCutPayload;
	VCutPayload.Center = FVector::ZeroVector;
	VCutPayload.Axis = EVCutFoundationAxis::Y;
	VCutPayload.HalfLengthBlocks = 45.0f;
	VCutPayload.SurfaceHalfWidthBlocks = 30.0f;
	VCutPayload.BottomHalfWidthBlocks = 5.0f;
	VCutPayload.DepthBlocks = 60.0f;

	FFoundationProviderDefinition VCutProvider;
	VCutProvider.DebugName = TEXT("VCut");
	VCutProvider.ContributionType = EFoundationContributionType::SubtractiveVoid;
	VCutProvider.ProviderType = EFoundationProviderType::VCutVoid;
	VCutProvider.ProviderPayload.InitializeAs<FVCutFoundationPayload>(VCutPayload);
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(VCutProvider));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestFoundationBiomeTag(), &ScaleContext);
	const FNodeLink UnrelatedDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	float FoundationCenterDeep = 0.0f;
	float FoundationWideNearSurface = 0.0f;
	float FoundationWideNearBottom = 0.0f;
	float FoundationOutsideLength = 0.0f;
	float UnrelatedCenterDeep = 0.0f;
	TestTrue(TEXT("Foundation samples deep V-cut center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, -30.0f), FoundationCenterDeep));
	TestTrue(TEXT("Foundation samples wide V-cut surface opening"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(24.0f, 0.0f, -5.0f), FoundationWideNearSurface));
	TestTrue(TEXT("Foundation samples outside narrowed lower V-cut"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(24.0f, 0.0f, -30.0f), FoundationWideNearBottom));
	TestTrue(TEXT("Foundation samples outside V-cut length"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 55.0f, -5.0f), FoundationOutsideLength));
	TestTrue(TEXT("Unrelated biome samples V-cut center"), EvaluateNoiseAtAuthoredBlock(UnrelatedDomain, ScaleContext, FVector(0.0f, 0.0f, -30.0f), UnrelatedCenterDeep));

	TestTrue(*FString::Printf(TEXT("V-cut carves the parent at the deep center. Foundation=%f"), FoundationCenterDeep), FoundationCenterDeep <= 0.0f);
	TestTrue(*FString::Printf(TEXT("V-cut carves the wide near-surface opening. Foundation=%f"), FoundationWideNearSurface), FoundationWideNearSurface <= 0.0f);
	TestTrue(*FString::Printf(TEXT("V-cut narrows with depth so the same X sample survives near the bottom. Foundation=%f"), FoundationWideNearBottom), FoundationWideNearBottom > 0.0f);
	TestTrue(TEXT("V-cut does not carve outside its authored length"), FoundationOutsideLength > 0.0f);
	TestTrue(TEXT("V-cut void does not contribute to an unrelated biome"), UnrelatedCenterDeep <= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseVCutVoidOrganicVariationBuildsDetailNodesTest,
	"PorismExtension.Biome.IslandNoise.VCutVoidOrganicVariationBuildsDetailNodes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseVCutVoidOrganicVariationBuildsDetailNodesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

	FVCutFoundationPayload VCutPayload;
	VCutPayload.Center = FVector::ZeroVector;
	VCutPayload.Axis = EVCutFoundationAxis::Y;
	VCutPayload.HalfLengthBlocks = 45.0f;
	VCutPayload.SurfaceHalfWidthBlocks = 30.0f;
	VCutPayload.BottomHalfWidthBlocks = 5.0f;
	VCutPayload.DepthBlocks = 60.0f;
	VCutPayload.bEnableOrganicVariation = true;
	VCutPayload.EdgeNoiseAmplitudeBlocks = 5.0f;
	VCutPayload.CenterlineWanderAmplitudeBlocks = 4.0f;
	VCutPayload.bEnableDomainWarp = true;
	VCutPayload.DomainWarpAmplitudeBlocks = 3.0f;
	VCutPayload.bEnableSteps = true;
	VCutPayload.StepHeightBlocks = 4.0f;

	FFoundationProviderDefinition VCutProvider;
	VCutProvider.DebugName = TEXT("OrganicVCut");
	VCutProvider.ContributionType = EFoundationContributionType::SubtractiveVoid;
	VCutProvider.ProviderType = EFoundationProviderType::VCutVoid;
	VCutProvider.ProviderPayload.InitializeAs<FVCutFoundationPayload>(VCutPayload);
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(VCutProvider));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestFoundationBiomeTag(), &ScaleContext);

	float FoundationCenterDeep = 0.0f;
	TestTrue(TEXT("Organic V-cut domain samples deep center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, -30.0f), FoundationCenterDeep));
	TestTrue(TEXT("Organic V-cut still carves the parent branch"), FoundationCenterDeep <= 0.0f);
	TestTrue(TEXT("Organic V-cut uses ridged wall variation"), FoundationDomain.Node != nullptr && FoundationDomain.Node->Flow.Contains(TEXT("FractalRidged")));
	TestTrue(TEXT("Organic V-cut can domain-warp wall variation"), FoundationDomain.Node != nullptr && FoundationDomain.Node->Flow.Contains(TEXT("DomainWarpGradient")));
	TestTrue(TEXT("Organic V-cut can terrace its wall-depth solve"), FoundationDomain.Node != nullptr && FoundationDomain.Node->Flow.Contains(TEXT("Terrace")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseChildReservationCarvesOnlyOwningProviderBranchTest,
	"PorismExtension.Biome.IslandNoise.ChildReservationCarvesOnlyOwningProviderBranch",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseChildReservationCarvesOnlyOwningProviderBranchTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	FFoundationProviderDefinition ChildProvider = CreateNoDetailIslandProvider(TEXT("Child"), TestFoundationBiomeTag(), FVector(120.0f, 0.0f, 0.0f), 24.0f);
	ChildProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("ChildAnchor"), TestReservationBiomeTag()));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ChildProvider));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestFoundationBiomeTag(), &ScaleContext);
	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildBiomeDomain(Editor, Strategy, TestReservationBiomeTag(), &ScaleContext);

	float FoundationAtParentCenter = 0.0f;
	float FoundationAtChildReservation = 0.0f;
	float ReservationAtChildReservation = 0.0f;
	float ReservationAtParentCenter = 0.0f;
	TestTrue(TEXT("Foundation samples parent center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector::ZeroVector, FoundationAtParentCenter));
	TestTrue(TEXT("Foundation samples provider-local child reservation"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(120.0f, 0.0f, 0.0f), FoundationAtChildReservation));
	TestTrue(TEXT("Reservation samples provider-local child reservation"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(120.0f, 0.0f, 0.0f), ReservationAtChildReservation));
	TestTrue(TEXT("Reservation samples parent center"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector::ZeroVector, ReservationAtParentCenter));

	TestTrue(TEXT("Child reservation does not carve the unrelated parent center"), FoundationAtParentCenter > 0.0f);
	TestTrue(TEXT("Child reservation carves its owning child provider branch"), FoundationAtChildReservation < 0.0f);
	TestTrue(TEXT("Child reservation contributes to its reservation biome at provider-local top center"), ReservationAtChildReservation > 0.0f);
	TestTrue(TEXT("Provider-local child reservation is not accidentally built at world origin"), ReservationAtParentCenter < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseReservationDomainWinsOverCarvedFoundationDomainTest,
	"PorismExtension.Biome.IslandNoise.ReservationDomainWinsOverCarvedFoundationDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseReservationDomainWinsOverCarvedFoundationDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink FoundationDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, Strategy);

	float ReservationAtAnchor = 0.0f;
	float FoundationAtAnchor = 0.0f;
	TestTrue(TEXT("Reservation domain samples anchor center"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector::ZeroVector, ReservationAtAnchor));
	TestTrue(TEXT("Foundation domain samples anchor center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector::ZeroVector, FoundationAtAnchor));

	TestTrue(TEXT("Reservation domain is positive inside the anchor"), ReservationAtAnchor > 0.0f);
	TestTrue(TEXT("Foundation domain is carved below the reservation domain inside the anchor"), FoundationAtAnchor <= 0.0f);
	TestTrue(TEXT("Reservation domain has stronger ownership than the carved foundation"), ReservationAtAnchor > FoundationAtAnchor);
	TestTrue(TEXT("Foundation carve clears Porism's default DomainOver blend window inside the anchor"), FoundationAtAnchor <= -0.5f);

	return true;
}

