// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseQueriesWorldGenBiomeStrategyBindingsTest,
	"PorismExtension.Biome.IslandNoise.QueriesWorldGenBiomeStrategyBindings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseQueriesWorldGenBiomeStrategyBindingsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateSharedReservationBiomeHierarchyStrategy();
	UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(GetTransientPackage());
	WorldGenDef->WorldBiomes.Add(CreateStrategyBiomeRow(Strategy, TEXT("Foundation"), TestFoundationBiomeTag()));
	WorldGenDef->WorldBiomes.Add(CreateStrategyBiomeRow(Strategy, TEXT("Reservation"), TestReservationBiomeTag()));

	TArray<FBiomeStrategyRowBinding> Bindings;
	UBiomeStrategyBindingLibrary::QueryBiomeStrategyRowBindings(GetTransientPackage(), WorldGenDef, Bindings);

	TestEqual(TEXT("WorldGenDef exposes one binding per strategy-backed biome row"), Bindings.Num(), 2);
	const FBiomeStrategyRowBinding* ReservationBinding = Bindings.FindByPredicate(
		[](const FBiomeStrategyRowBinding& Binding)
		{
			return Binding.BiomeTag == TestReservationBiomeTag();
		});
	TestNotNull(TEXT("Reservation biome row binding is returned"), ReservationBinding);
	if (ReservationBinding == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Reservation row binding preserves row index"), ReservationBinding->RowIndex, 1);
	TestEqual(TEXT("Reservation row binding preserves biome name"), ReservationBinding->BiomeName, FString(TEXT("Reservation")));
	TestTrue(TEXT("Reservation row binding has DomainNoise wrapper"), ReservationBinding->bHasDomainNoiseBinding);
	TestTrue(TEXT("Reservation row binding has GenA wrapper"), ReservationBinding->bHasGenABinding);
	TestEqual(TEXT("Reservation row binding reports instruction count"), ReservationBinding->InstructionCount, 0);
	TestTrue(TEXT("Reservation row binding reports NoiseOnly rows"), ReservationBinding->bNoiseOnly);
	TestEqual(TEXT("Reservation row binding reports DomainOver"), ReservationBinding->DomainOver, 0.0f);
	TestTrue(TEXT("Reservation row binding keeps selected strategy"), ReservationBinding->Strategy == Strategy);
	TestTrue(TEXT("Reservation row binding records debug path"), ReservationBinding->DebugPath.Contains(TEXT("WorldBiomes[1:Reservation]")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseQueriesWorldGenReservationFieldsByBiomeRowTest,
	"PorismExtension.Biome.IslandNoise.QueriesWorldGenReservationFieldsByBiomeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseQueriesWorldGenReservationFieldsByBiomeRowTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateSharedReservationBiomeHierarchyStrategy();
	UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(GetTransientPackage());
	WorldGenDef->WorldBiomes.Add(CreateStrategyBiomeRow(Strategy, TEXT("Foundation"), TestFoundationBiomeTag()));
	WorldGenDef->WorldBiomes.Add(CreateStrategyBiomeRow(Strategy, TEXT("Reservation"), TestReservationBiomeTag()));

	TArray<FBiomeStrategyReservationFieldBinding> FieldBindings;
	UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(GetTransientPackage(), WorldGenDef, FBox(ForceInit), FieldBindings);

	TestEqual(TEXT("Reservation biome row exposes root and child reservation fields"), FieldBindings.Num(), 2);
	TestTrue(TEXT("Only reservation biome row fields are returned"),
		FieldBindings.ContainsByPredicate(
			[](const FBiomeStrategyReservationFieldBinding& Binding)
			{
				return Binding.RowBinding.BiomeName == TEXT("Reservation")
					&& Binding.Field.DebugPath.Contains(TEXT("RootCenter"));
			})
		&& FieldBindings.ContainsByPredicate(
			[](const FBiomeStrategyReservationFieldBinding& Binding)
			{
				return Binding.RowBinding.BiomeName == TEXT("Reservation")
					&& Binding.Field.DebugPath.Contains(TEXT("ChildCenter"));
			}));
	TestFalse(TEXT("Foundation biome row does not expose reservation fields"),
		FieldBindings.ContainsByPredicate(
			[](const FBiomeStrategyReservationFieldBinding& Binding)
			{
				return Binding.RowBinding.BiomeName == TEXT("Foundation");
			}));

	TArray<FBiomeStrategyReservationFieldBinding> ChildOnlyBindings;
	UBiomeStrategyBindingLibrary::QueryWorldGenReservationFields(
		GetTransientPackage(),
		WorldGenDef,
		FBox(FVector(145.0, -15.0, -5.0), FVector(175.0, 15.0, 20.0)),
		ChildOnlyBindings);
	TestEqual(TEXT("Bounded WorldGenDef query returns only the child reservation field"), ChildOnlyBindings.Num(), 1);
	TestTrue(TEXT("Bounded WorldGenDef query keeps child provider debug path"),
		ChildOnlyBindings.Num() == 1 && ChildOnlyBindings[0].Field.DebugPath.Contains(TEXT("ChildFoundations[0:Child]")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseTaggedFieldBoundsUseProviderSurfaceTest,
	"PorismExtension.Biome.IslandNoise.TaggedFieldBoundsUseProviderSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseTaggedFieldBoundsUseProviderSurfaceTest::RunTest(const FString& Parameters)
{
	constexpr float SurfaceOffsetBlocks = 3.0f;
	UBiomeStrategyData* const Strategy = CreateNoisyFoundationSurfaceSolveStrategy(ESurfaceAnchorHeightSolveMode::MaxSamples, SurfaceOffsetBlocks);
	TestTrue(TEXT("Strategy has a default reservation"), Strategy->RootFoundationProvider.Reservations.Num() > 0);
	if (Strategy->RootFoundationProvider.Reservations.Num() == 0)
	{
		return false;
	}

	const FSurfaceAnchorReservationPayload* const ReservationPayload = Strategy->RootFoundationProvider.Reservations[0].ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>();
	TestNotNull(TEXT("Reservation has surface-anchor payload"), ReservationPayload);
	if (ReservationPayload == nullptr)
	{
		return false;
	}

	FFoundationSurfaceQuery SurfaceQuery;
	SurfaceQuery.CenterXY = ReservationPayload->CenterXY;
	SurfaceQuery.FootprintHalfExtent = FVector2D(ReservationPayload->BoxHalfExtent.X, ReservationPayload->BoxHalfExtent.Y);
	SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::MaxSamples;

	FResolvedFoundationSurface ProviderSurface;
	TestTrue(TEXT("Provider resolves the reservation footprint surface"), Strategy->QueryFoundationSurface(GetTransientPackage(), SurfaceQuery, ProviderSurface));

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);
	const FTaggedReservationField* ReservationField = Fields.FindByPredicate(
		[](const FTaggedReservationField& Field)
		{
			return Field.FieldKind == EReservationFieldKind::Reservation;
		});
	TestNotNull(TEXT("Reservation field is returned"), ReservationField);
	if (ReservationField == nullptr)
	{
		return false;
	}

	const float ExpectedAnchorSurfaceZ = ProviderSurface.SurfaceZBlock + ReservationPayload->SurfaceZLift + ReservationPayload->SurfaceZOffset;
	TestTrue(TEXT("Tagged field bottom uses provider surface solve plus lift/offset"), FMath::IsNearlyEqual(static_cast<float>(ReservationField->AuthoredMinBlock.Z), ExpectedAnchorSurfaceZ - ReservationPayload->BoxHalfExtent.Z, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Tagged field top uses provider surface solve plus lift/offset"), FMath::IsNearlyEqual(static_cast<float>(ReservationField->AuthoredMaxBlock.Z), ExpectedAnchorSurfaceZ + ReservationPayload->BoxHalfExtent.X, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseSlotFallbacksAreSafeTest,
	"PorismExtension.Biome.IslandNoise.SlotFallbacksAreSafe",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseSlotFallbacksAreSafeTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);

	UBiomeStrategyData* const EmptyStrategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	const FNodeLink MissingPayloadDomain = UIslandBiomeFastNoiseLibrary::BuildFoundationDomain(Editor, EmptyStrategy);
	TestTrue(TEXT("Missing foundation payload returns a valid fallback node"), MissingPayloadDomain.Node != nullptr);
	TestTrue(TEXT("Missing foundation payload returns constant zero"), MissingPayloadDomain.Node != nullptr && MissingPayloadDomain.Node->Flow.StartsWith(TEXT("Constant0")));

	UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
	const FNodeLink UnknownReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, FGameplayTag());
	const FNodeLink UnknownSpawnDomain = UIslandBiomeFastNoiseLibrary::BuildSpawnDomain(Editor, Strategy, FGameplayTag());
	TestTrue(TEXT("Unknown reservation returns a valid fallback node"), UnknownReservationDomain.Node != nullptr);
	TestTrue(TEXT("Unknown reservation returns constant zero"), UnknownReservationDomain.Node != nullptr && UnknownReservationDomain.Node->Flow.StartsWith(TEXT("Constant0")));
	TestTrue(TEXT("Unknown spawn reservation returns a valid fallback node"), UnknownSpawnDomain.Node != nullptr);
	TestTrue(TEXT("Unknown spawn reservation returns constant zero"), UnknownSpawnDomain.Node != nullptr && UnknownSpawnDomain.Node->Flow.StartsWith(TEXT("Constant0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationReportsStructuredIssuesTest,
	"PorismExtension.Biome.IslandNoise.ValidationReportsStructuredIssues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationReportsStructuredIssuesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const EmptyStrategy = NewObject<UBiomeStrategyData>(GetTransientPackage());
	const FBiomeStrategyValidationResult Validation = EmptyStrategy->ValidateStrategy();

	TestTrue(TEXT("Invalid strategy reports blocking errors"), Validation.HasErrors());
	TestFalse(TEXT("Invalid strategy does not report warnings for required-field errors"), Validation.HasWarnings());
	TestTrue(TEXT("Invalid strategy reports at least one issue"), Validation.Issues.Num() > 0);
	TestTrue(TEXT("Invalid strategy issue includes provider debug path"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.DebugPath.Contains(TEXT("RootFoundationProvider"));
			}));
	TestTrue(TEXT("Invalid strategy issue includes actionable fix text"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return !Issue.FixText.IsEmpty();
			}));

	UBiomeStrategyData* const ValidStrategy = CreateNoDetailMainMenuIslandStrategy();
	const FBiomeStrategyValidationResult ValidValidation = ValidStrategy->ValidateStrategy();
	TestFalse(TEXT("Valid island strategy has no blocking validation errors"), ValidValidation.HasErrors());
	TestFalse(TEXT("Valid island strategy has no validation warnings"), ValidValidation.HasWarnings());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationBlocksRuntimeConstructionTest,
	"PorismExtension.Biome.IslandNoise.ValidationBlocksRuntimeConstruction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationBlocksRuntimeConstructionTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const EmptyStrategy = NewObject<UBiomeStrategyData>(GetTransientPackage());

	const FNodeLink Noise = EmptyStrategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	TestTrue(TEXT("Invalid strategy returns a valid safety node"), Noise.Node != nullptr);
	TestTrue(TEXT("Invalid strategy is blocked with constant zero noise"), Noise.Node != nullptr && Noise.Node->Flow.StartsWith(TEXT("Constant0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseEditorSyncsProviderPayloadToTypeTest,
	"PorismExtension.Biome.IslandNoise.EditorSyncsProviderPayloadToType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseEditorSyncsProviderPayloadToTypeTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

	FIslandFoundationShapePayload IslandPayload;
	IslandPayload.IslandBody.TopRadius = 321.0f;
	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::Island;
	Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>(IslandPayload);
	Strategy->NormalizeProviderPayloadsForEditor();
	const FIslandFoundationShapePayload* PreservedIslandPayload = Strategy->RootFoundationProvider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>();
	TestNotNull(TEXT("Matching island provider payload is preserved"), PreservedIslandPayload);
	TestEqual(TEXT("Matching island provider payload keeps authored values"), PreservedIslandPayload != nullptr ? PreservedIslandPayload->IslandBody.TopRadius : 0.0f, 321.0f);

	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::InfinitePlane;
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Infinite plane provider type replaces stale island payload"), Strategy->RootFoundationProvider.ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>());

	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::Island;
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Island provider type replaces stale infinite plane payload"), Strategy->RootFoundationProvider.ProviderPayload.GetPtr<FIslandFoundationShapePayload>());

	Strategy->RootFoundationProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::VCutVoid;
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("V-cut provider type replaces stale island payload"), Strategy->RootFoundationProvider.ProviderPayload.GetPtr<FVCutFoundationPayload>());
	TestEqual(TEXT("V-cut providers normalize to subtractive void"), Strategy->RootFoundationProvider.ContributionType, EFoundationContributionType::SubtractiveVoid);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseEditorNormalizesTerrainProfilesForProviderTest,
	"PorismExtension.Biome.IslandNoise.EditorNormalizesTerrainProfilesForProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseEditorNormalizesTerrainProfilesForProviderTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::Island;
	Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>();
	Strategy->RootFoundationProvider.TerrainProfile = FInstancedStruct();
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Empty island terrain profile normalizes to the island default"), Strategy->RootFoundationProvider.TerrainProfile.GetPtr<FNoisyFoundationTerrainProfilePayload>());

	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::Island;
	Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>();
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FErodedEdgeFoundationTerrainProfilePayload>();
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Finite-only eroded edge terrain profile is allowed on island providers"), Strategy->RootFoundationProvider.TerrainProfile.GetPtr<FErodedEdgeFoundationTerrainProfilePayload>());
	TestFalse(TEXT("Finite-only terrain profile validates on island providers"), Strategy->ValidateStrategy().HasErrors());

	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::InfinitePlane;
	Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FInfinitePlaneFoundationPayload>();
	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FErodedEdgeFoundationTerrainProfilePayload>();
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Finite-only eroded edge terrain profile normalizes away on infinite plane providers"), Strategy->RootFoundationProvider.TerrainProfile.GetPtr<FFlatFoundationTerrainProfilePayload>());

	Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FNoisyFoundationTerrainProfilePayload>();
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Infinite-compatible noisy terrain profile is preserved on infinite plane providers"), Strategy->RootFoundationProvider.TerrainProfile.GetPtr<FNoisyFoundationTerrainProfilePayload>());

	UBiomeStrategyData* const InvalidStrategy = CreateNoReservationHierarchyStrategy();
	InvalidStrategy->RootFoundationProvider.ProviderType = EFoundationProviderType::InfinitePlane;
	InvalidStrategy->RootFoundationProvider.ProviderPayload.InitializeAs<FInfinitePlaneFoundationPayload>();
	InvalidStrategy->RootFoundationProvider.TerrainProfile.InitializeAs<FErodedEdgeFoundationTerrainProfilePayload>();
	const FBiomeStrategyValidationResult Validation = InvalidStrategy->ValidateStrategy();
	TestTrue(TEXT("Validation rejects finite-only terrain profiles on infinite plane providers"), Validation.HasErrors());
	TestTrue(TEXT("Validation names incompatible terrain profile provider use"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("not compatible"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseEditorSyncsReservationPayloadToTypeTest,
	"PorismExtension.Biome.IslandNoise.EditorSyncsReservationPayloadToType",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseEditorSyncsReservationPayloadToTypeTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

	FReservationDefinition& Reservation = Strategy->RootFoundationProvider.Reservations.AddDefaulted_GetRef();
	Reservation.DebugName = TEXT("AutoReservation");
	Reservation.BiomeTag = TestReservationBiomeTag();
	Reservation.ReservationType = EReservationType::SurfaceAnchor;
	Reservation.ReservationPayload.InitializeAs<FMultiInstanceSurfaceAnchorReservationPayload>();
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Surface anchor reservation type replaces stale multi-instance payload"), Reservation.ReservationPayload.GetPtr<FSurfaceAnchorReservationPayload>());

	Reservation.ReservationType = EReservationType::MultiInstanceSurfaceAnchor;
	Reservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>();
	Strategy->NormalizeProviderPayloadsForEditor();
	const FMultiInstanceSurfaceAnchorReservationPayload* const MultiPayload = Reservation.ReservationPayload.GetPtr<FMultiInstanceSurfaceAnchorReservationPayload>();
	TestNotNull(TEXT("Multi-instance reservation type replaces stale surface payload"), MultiPayload);
	TestNotNull(TEXT("Multi-instance reservation creates surface-anchor prototype"), MultiPayload != nullptr ? MultiPayload->PrototypeSurfaceAnchor.GetPtr<FSurfaceAnchorReservationPayload>() : nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseEditorSyncsMultiInstancePrototypePayloadTest,
	"PorismExtension.Biome.IslandNoise.EditorSyncsMultiInstancePrototypePayload",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseEditorSyncsMultiInstancePrototypePayloadTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::MultiInstance;
	Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FIslandFoundationShapePayload>();
	Strategy->NormalizeProviderPayloadsForEditor();

	FMultiInstanceFoundationPayload* const MultiPayload = Strategy->RootFoundationProvider.ProviderPayload.GetMutablePtr<FMultiInstanceFoundationPayload>();
	TestNotNull(TEXT("Multi instance provider type replaces stale island payload"), MultiPayload);
	if (MultiPayload == nullptr)
	{
		return false;
	}

	FFoundationProviderPrototypeDefinition* const PrototypeProvider = MultiPayload->PrototypeProvider.GetMutablePtr<FFoundationProviderPrototypeDefinition>();
	TestNotNull(TEXT("Multi instance provider creates a prototype provider"), PrototypeProvider);
	if (PrototypeProvider == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Default prototype provider starts as island"), PrototypeProvider->ProviderType, EFoundationProviderType::Island);
	TestNotNull(TEXT("Default prototype provider has island payload"), PrototypeProvider->ProviderPayload.GetPtr<FIslandFoundationShapePayload>());
	TestNotNull(TEXT("Default prototype provider has terrain profile"), PrototypeProvider->TerrainProfile.GetPtr<FNoisyFoundationTerrainProfilePayload>());

	PrototypeProvider->ProviderType = EFoundationProviderType::InfinitePlane;
	PrototypeProvider->ProviderPayload.InitializeAs<FIslandFoundationShapePayload>();
	Strategy->NormalizeProviderPayloadsForEditor();
	TestNotNull(TEXT("Prototype provider type replaces stale payload"), PrototypeProvider->ProviderPayload.GetPtr<FInfinitePlaneFoundationPayload>());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationWarnsForMixedGroupedReservationTerrainTest,
	"PorismExtension.Biome.IslandNoise.ValidationWarnsForMixedGroupedReservationTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationWarnsForMixedGroupedReservationTerrainTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	FReservationDefinition NoisyReservation = CreateProviderLocalBoxReservation(TEXT("NoisySameBiome"), TestReservationBiomeTag());
	NoisyReservation.ReservationPayload.InitializeAs<FSurfaceAnchorReservationPayload>();
	if (FSurfaceAnchorReservationPayload* const SurfacePayload = NoisyReservation.ReservationPayload.GetMutablePtr<FSurfaceAnchorReservationPayload>())
	{
		SurfacePayload->Shape = ESurfaceAnchorReservationShape::Box;
		SurfacePayload->CenterXY = FVector2D(90.0f, 0.0f);
		SurfacePayload->TerrainPayload.InitializeAs<FSurfaceAnchorNoisyTerrainPayload>();
	}
	Strategy->RootFoundationProvider.Reservations.Add(NoisyReservation);

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();

	TestFalse(TEXT("Mixed grouped reservation terrain is a warning, not a blocking error"), Validation.HasErrors());
	TestTrue(TEXT("Mixed grouped reservation terrain reports a warning"), Validation.HasWarnings());
	TestTrue(TEXT("Warning identifies the grouped reservation biome tag"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Warning
					&& Issue.Message.Contains(TEXT("Biome Tag"))
					&& Issue.Message.Contains(TEXT("different terrain payload type"));
			}));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink Noise = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestReservationBiomeTag());
	TestTrue(TEXT("Warnings do not block runtime strategy construction"), Noise.Node != nullptr && !Noise.Node->Flow.StartsWith(TEXT("Constant0")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseReturnsZeroForMissingStrategyTest,
	"PorismExtension.Biome.IslandNoise.ReturnsZeroForMissingStrategy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseReturnsZeroForMissingStrategyTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UBiomeFastNoiseEditor* const Editor = CreateBiomeEditor(Nodes, nullptr);

	const FNodeLink Noise = Editor->GetNoiseRef(GetTransientPackage());

	TestTrue(TEXT("Missing strategy returns a valid fallback node"), Noise.Node != nullptr);
	TestTrue(TEXT("Missing strategy returns constant zero"), Noise.Node != nullptr && Noise.Node->Flow.StartsWith(TEXT("Constant0")));

	return true;
}
