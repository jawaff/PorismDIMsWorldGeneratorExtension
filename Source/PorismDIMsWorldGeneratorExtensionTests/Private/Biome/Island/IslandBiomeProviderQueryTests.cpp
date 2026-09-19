// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBuildsBoxAnchorDomainTest,
	"PorismExtension.Biome.IslandNoise.BuildsBoxAnchorDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBuildsBoxAnchorDomainTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	UBiomeStrategyData* const Strategy = CreateBoxAnchorIslandStrategy();

	const FNodeLink ReservationDomain = UIslandBiomeFastNoiseLibrary::BuildReservationDomain(Editor, Strategy, TestReservationBiomeTag());
	const FNodeLink SpawnDomain = UIslandBiomeFastNoiseLibrary::BuildSpawnDomain(Editor, Strategy, TestReservationBiomeTag());

	TestTrue(TEXT("Box reservation domain is valid"), ReservationDomain.Node != nullptr);
	TestTrue(TEXT("Box reservation domain uses axis-aligned position slabs"), ReservationDomain.Node != nullptr && ReservationDomain.Node->Flow.Contains(TEXT("PositionOutput")));
	TestTrue(TEXT("Box reservation domain intersects slab masks"), ReservationDomain.Node != nullptr && ReservationDomain.Node->Flow.Contains(TEXT("Min")));
	TestTrue(TEXT("Box spawn domain is valid"), SpawnDomain.Node != nullptr);
	TestTrue(TEXT("Box spawn domain keeps the box-domain shape"), SpawnDomain.Node != nullptr && SpawnDomain.Node->Flow.Contains(TEXT("PositionOutput")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBiomeEditorSelectsOutputsTest,
	"PorismExtension.Biome.IslandNoise.BiomeEditorSelectsOutputs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBiomeEditorSelectsOutputsTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> Nodes;
	UBiomeStrategyData* const Strategy = CreateMainMenuIslandStrategy();
	UBiomeFastNoiseEditor* const Editor = CreateBiomeEditor(Nodes, Strategy);

	Editor->BiomeTag = TestFoundationBiomeTag();
	Editor->NoiseSlot = EBiomeNoiseSlot::DomainNoise;
	TestTrue(TEXT("Slot editor builds foundation domain"), Editor->GetNoiseRef(GetTransientPackage()).Node != nullptr);

	Editor->NoiseSlot = EBiomeNoiseSlot::GenA;
	TestTrue(TEXT("Slot editor builds foundation GenA"), Editor->GetNoiseRef(GetTransientPackage()).Node != nullptr);

	Editor->BiomeTag = TestReservationBiomeTag();
	Editor->NoiseSlot = EBiomeNoiseSlot::DomainNoise;
	TestTrue(TEXT("Slot editor builds reservation domain"), Editor->GetNoiseRef(GetTransientPackage()).Node != nullptr);

	Editor->NoiseSlot = EBiomeNoiseSlot::GenA;
	TestTrue(TEXT("Slot editor builds reservation GenA"), Editor->GetNoiseRef(GetTransientPackage()).Node != nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseBiomeEditorsShareStrategyDataTest,
	"PorismExtension.Biome.IslandNoise.BiomeEditorsShareStrategyData",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseBiomeEditorsShareStrategyDataTest::RunTest(const FString& Parameters)
{
	std::vector<FNodeLink> FoundationNodes;
	std::vector<FNodeLink> ReservationNodes;
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();

	UBiomeFastNoiseEditor* const FoundationEditor = CreateBiomeEditor(FoundationNodes, Strategy);
	FoundationEditor->NoiseSlot = EBiomeNoiseSlot::DomainNoise;
	FoundationEditor->BiomeTag = TestFoundationBiomeTag();

	UBiomeFastNoiseEditor* const ReservationEditor = CreateBiomeEditor(ReservationNodes, Strategy);
	ReservationEditor->NoiseSlot = EBiomeNoiseSlot::DomainNoise;
	ReservationEditor->BiomeTag = TestReservationBiomeTag();

	const FNodeLink FoundationDomain = FoundationEditor->GetNoiseRef(GetTransientPackage());
	const FNodeLink ReservationDomain = ReservationEditor->GetNoiseRef(GetTransientPackage());

	TestTrue(TEXT("Foundation slot wrapper builds from shared strategy"), FoundationDomain.Node != nullptr);
	TestTrue(TEXT("Reservation slot wrapper builds from shared strategy"), ReservationDomain.Node != nullptr);
	TestTrue(TEXT("Foundation domain references the shared strategy carve-out"), FoundationDomain.Node != nullptr && FoundationDomain.Node->Flow.Contains(TEXT("MultiplyFloat-1")));
	TestTrue(TEXT("Reservation domain references the shared surface-anchor shape"), ReservationDomain.Node != nullptr && ReservationDomain.Node->Flow.Contains(TEXT("PowInt")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseQueriesTaggedReservationFieldsTest,
	"PorismExtension.Biome.IslandNoise.QueriesTaggedReservationFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseQueriesTaggedReservationFieldsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	TestTrue(TEXT("Strategy has a default reservation"), Strategy->RootFoundationProvider.Reservations.Num() > 0);
	if (Strategy->RootFoundationProvider.Reservations.Num() == 0)
	{
		return false;
	}

	FReservationDefinition& Reservation = Strategy->RootFoundationProvider.Reservations[0];
	Reservation.FieldTags.AddTag(BiomeGameplayTags::Reservation.GetTag());
	Reservation.FieldTags.AddTag(BiomeGameplayTags::Foundation.GetTag());
	Reservation.bEnableSpawnReservation = true;
	Reservation.SpawnFieldTags.AddTag(BiomeGameplayTags::Reservation.GetTag());
	Reservation.SpawnFieldTags.AddTag(BiomeGameplayTags::Foundation.GetTag());

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);

	TestEqual(TEXT("Reservation and spawn fields are returned"), Fields.Num(), 2);
	if (Fields.Num() != 2)
	{
		return false;
	}

	const FTaggedReservationField* ReservationField = Fields.FindByPredicate(
		[](const FTaggedReservationField& Field)
		{
			return Field.FieldKind == EReservationFieldKind::Reservation;
		});
	const FTaggedReservationField* SpawnField = Fields.FindByPredicate(
		[](const FTaggedReservationField& Field)
		{
			return Field.FieldKind == EReservationFieldKind::Spawn;
		});

	TestNotNull(TEXT("Reservation field is present"), ReservationField);
	TestNotNull(TEXT("Spawn field is present"), SpawnField);
	if (ReservationField == nullptr || SpawnField == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Reservation field carries authored tags"), ReservationField->FieldTags.HasTagExact(BiomeGameplayTags::Reservation.GetTag()));
	TestTrue(TEXT("Reservation field carries authored capability-style tags"), ReservationField->FieldTags.HasTagExact(BiomeGameplayTags::Foundation.GetTag()));
	TestTrue(TEXT("Spawn field carries authored tags"), SpawnField->FieldTags.HasTagExact(BiomeGameplayTags::Reservation.GetTag()));
	TestTrue(TEXT("Spawn field carries authored capability-style tags"), SpawnField->FieldTags.HasTagExact(BiomeGameplayTags::Foundation.GetTag()));
	TestTrue(TEXT("Reservation debug path includes reservation name"), ReservationField->DebugPath.Contains(TEXT("Reservations[Center]")));
	TestTrue(TEXT("Reservation field bounds cover the anchor center"), ReservationField->AuthoredMinBlock.X <= 0.0 && ReservationField->AuthoredMaxBlock.X >= 0.0);

	TArray<FTaggedReservationField> FarFields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(FVector(1000.0, 1000.0, 1000.0), FVector(1100.0, 1100.0, 1100.0)), FarFields);
	TestEqual(TEXT("Far query excludes center fields"), FarFields.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseQueriesFoundationProviderBoundsTest,
	"PorismExtension.Biome.IslandNoise.QueriesFoundationProviderBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseQueriesFoundationProviderBoundsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();

	FFoundationProviderBounds Bounds;
	TestTrue(TEXT("Island strategy reports provider bounds"), Strategy->QueryFoundationProviderBounds(GetTransientPackage(), Bounds));
	TestTrue(TEXT("Provider bounds are marked valid"), Bounds.bValid);
	TestTrue(TEXT("Provider bounds include island center"), Bounds.AuthoredMinBlock.X <= 0.0 && Bounds.AuthoredMaxBlock.X >= 0.0);
	TestEqual(TEXT("Provider bounds include configured domain radius padding"), static_cast<float>(Bounds.AuthoredMaxBlock.X), 132.0f);
	TestEqual(TEXT("Provider bounds include configured domain top padding"), static_cast<float>(Bounds.AuthoredMaxBlock.Z), 12.0f);
	TestEqual(TEXT("Provider bounds include configured bottom depth and top height"), static_cast<float>(Bounds.AuthoredMinBlock.Z), -137.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseResolvesProviderHierarchyInstancesTest,
	"PorismExtension.Biome.IslandNoise.ResolvesProviderHierarchyInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseResolvesProviderHierarchyInstancesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateSharedReservationBiomeHierarchyStrategy();

	FResolvedBiomeStrategy ResolvedStrategy;
	TestTrue(TEXT("Strategy resolves provider hierarchy"), Strategy->ResolveBiomeStrategy(GetTransientPackage(), ResolvedStrategy));
	TestTrue(TEXT("Resolved strategy is marked valid"), ResolvedStrategy.bValid);
	TestEqual(TEXT("Root and child providers resolve as instances"), ResolvedStrategy.ProviderInstances.Num(), 2);
	if (ResolvedStrategy.ProviderInstances.Num() < 2)
	{
		return false;
	}

	const FResolvedFoundationProviderInstance& RootInstance = ResolvedStrategy.ProviderInstances[0];
	const FResolvedFoundationProviderInstance& ChildInstance = ResolvedStrategy.ProviderInstances[1];
	TestEqual(TEXT("Root provider has depth zero"), RootInstance.HierarchyDepth, 0);
	TestEqual(TEXT("Child provider has depth one"), ChildInstance.HierarchyDepth, 1);
	TestTrue(TEXT("Child provider debug path is stable and readable"), ChildInstance.DebugPath.Contains(TEXT("ChildFoundations[0:Child]")));
	TestTrue(TEXT("Child provider bounds are resolved"), ChildInstance.Bounds.bValid);
	TestTrue(TEXT("Child provider bounds include authored child center X"), ChildInstance.Bounds.AuthoredMinBlock.X < 160.0 && ChildInstance.Bounds.AuthoredMaxBlock.X > 160.0);

	TArray<FFoundationProviderBounds> AllBounds;
	Strategy->QueryFoundationProviderBounds(GetTransientPackage(), AllBounds);
	TestEqual(TEXT("Bounds query returns every resolved provider"), AllBounds.Num(), 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseQueriesProviderLocalFoundationSurfaceTest,
	"PorismExtension.Biome.IslandNoise.QueriesProviderLocalFoundationSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseQueriesProviderLocalFoundationSurfaceTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();

	FFoundationSurfaceQuery CenterQuery;
	CenterQuery.CenterXY = FVector2D::ZeroVector;
	CenterQuery.FootprintHalfExtent = FVector2D(35.0f, 35.0f);
	CenterQuery.SampleMode = EFoundationSurfaceSampleMode::CenterSample;

	FResolvedFoundationSurface CenterSurface;
	TestTrue(TEXT("Finite island provider resolves its local top-center surface"), Strategy->QueryFoundationSurface(GetTransientPackage(), CenterQuery, CenterSurface));
	TestTrue(TEXT("Center surface is marked valid"), CenterSurface.bValid);
	TestTrue(TEXT("Provider-local 0,0,0 is the authored finite-foundation top center"), FMath::IsNearlyEqual(CenterSurface.SurfaceZBlock, 0.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Provider surface records samples"), CenterSurface.SampleCount > 0);

	FFoundationSurfaceQuery OffsetQuery = CenterQuery;
	OffsetQuery.CenterXY = FVector2D(20.0f, -10.0f);
	FResolvedFoundationSurface OffsetSurface;
	TestTrue(TEXT("Finite island provider resolves an offset local surface"), Strategy->QueryFoundationSurface(GetTransientPackage(), OffsetQuery, OffsetSurface));
	TestTrue(TEXT("No-detail island offset surface stays on the same authored top plane"), FMath::IsNearlyEqual(OffsetSurface.SurfaceZBlock, 0.0f, KINDA_SMALL_NUMBER));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseQueriesChildProviderReservationFieldsTest,
	"PorismExtension.Biome.IslandNoise.QueriesChildProviderReservationFields",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseQueriesChildProviderReservationFieldsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateSharedReservationBiomeHierarchyStrategy();

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);

	const FTaggedReservationField* ChildField = Fields.FindByPredicate(
		[](const FTaggedReservationField& Field)
		{
			return Field.DebugPath.Contains(TEXT("ChildFoundations[0:Child]"))
				&& Field.DebugPath.Contains(TEXT("ChildCenter"))
				&& Field.FieldKind == EReservationFieldKind::Reservation;
		});
	TestNotNull(TEXT("Child provider reservation field is returned"), ChildField);
	if (ChildField == nullptr)
	{
		return false;
	}

	TestTrue(TEXT("Child reservation field is resolved relative to child provider center X"),
		FMath::IsNearlyEqual(static_cast<float>(ChildField->AuthoredMinBlock.X), 150.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(ChildField->AuthoredMaxBlock.X), 170.0f, KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Child reservation field applies provider-local lift"),
		FMath::IsNearlyEqual(static_cast<float>(ChildField->AuthoredMinBlock.Z), -1.0f, KINDA_SMALL_NUMBER)
		&& FMath::IsNearlyEqual(static_cast<float>(ChildField->AuthoredMaxBlock.Z), 15.0f, KINDA_SMALL_NUMBER));

	TArray<FTaggedReservationField> ChildOnlyFields;
	Strategy->QueryTaggedReservationFields(
		GetTransientPackage(),
		FBox(FVector(145.0, -15.0, -5.0), FVector(175.0, 15.0, 20.0)),
		ChildOnlyFields);
	TestEqual(TEXT("Bounded query returns only the child reservation field"), ChildOnlyFields.Num(), 1);
	TestTrue(TEXT("Bounded child query keeps the child debug path"),
		ChildOnlyFields.Num() == 1 && ChildOnlyFields[0].DebugPath.Contains(TEXT("ChildFoundations[0:Child]")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseResolvesLineMultiInstanceProviderTest,
	"PorismExtension.Biome.IslandNoise.ResolvesLineMultiInstanceProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseResolvesLineMultiInstanceProviderTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceFoundationStrategy();

	FResolvedBiomeStrategy ResolvedStrategy;
	TestTrue(TEXT("Multi-instance strategy resolves"), Strategy->ResolveBiomeStrategy(GetTransientPackage(), ResolvedStrategy));
	TestEqual(TEXT("Multi-instance provider resolves prototype copies"), ResolvedStrategy.ProviderInstances.Num(), 2);
	if (ResolvedStrategy.ProviderInstances.Num() != 2)
	{
		return false;
	}

	TestTrue(TEXT("First instance debug path includes generated instance index"), ResolvedStrategy.ProviderInstances[0].DebugPath.Contains(TEXT("Instances[0:IslandLine]")));
	TestTrue(TEXT("Second instance debug path includes generated instance index"), ResolvedStrategy.ProviderInstances[1].DebugPath.Contains(TEXT("Instances[1:IslandLine]")));
	TestTrue(TEXT("First instance bounds are shifted left"), ResolvedStrategy.ProviderInstances[0].Bounds.AuthoredMaxBlock.X < 0.0f);
	TestTrue(TEXT("Second instance bounds are shifted right"), ResolvedStrategy.ProviderInstances[1].Bounds.AuthoredMinBlock.X > 0.0f);

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);
	TestEqual(TEXT("Each generated instance exposes a transformed reservation field"), Fields.Num(), 2);
	TestTrue(TEXT("Generated reservation fields are shifted with their instances"),
		Fields.ContainsByPredicate(
			[](const FTaggedReservationField& Field)
			{
				return Field.AuthoredMaxBlock.X < 0.0f && Field.DebugPath.Contains(TEXT("Instances[0:IslandLine]"));
			})
		&& Fields.ContainsByPredicate(
			[](const FTaggedReservationField& Field)
			{
				return Field.AuthoredMinBlock.X > 0.0f && Field.DebugPath.Contains(TEXT("Instances[1:IslandLine]"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseMultiInstanceBiomeDomainGeneratesBothInstancesTest,
	"PorismExtension.Biome.IslandNoise.MultiInstanceBiomeDomainGeneratesBothInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseMultiInstanceBiomeDomainGeneratesBothInstancesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceFoundationStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink FoundationDomain = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	float LeftDomain = 0.0f;
	float RightDomain = 0.0f;
	float BetweenDomain = 0.0f;
	TestTrue(TEXT("Resolved multi-instance foundation domain samples left instance"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(-110.0f, 20.0f, -1.0f), LeftDomain));
	TestTrue(TEXT("Resolved multi-instance foundation domain samples right instance"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(110.0f, 20.0f, -1.0f), RightDomain));
	TestTrue(TEXT("Resolved multi-instance foundation domain samples gap between instances"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, -1.0f), BetweenDomain));

	TestTrue(TEXT("Left generated instance contributes positive foundation domain"), LeftDomain > 0.0f);
	TestTrue(TEXT("Right generated instance contributes positive foundation domain"), RightDomain > 0.0f);
	TestTrue(TEXT("Gap between non-overlapping generated instances remains outside foundation domain"), BetweenDomain < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseChildMultiInstanceBiomeDomainGeneratesInstancesTest,
	"PorismExtension.Biome.IslandNoise.ChildMultiInstanceBiomeDomainGeneratesInstances",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseChildMultiInstanceBiomeDomainGeneratesInstancesTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateIslandWithChildMultiInstanceFoundationStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	FResolvedBiomeStrategy ResolvedStrategy;
	TestTrue(TEXT("Island parent with child multi-instance resolves"), Strategy->ResolveBiomeStrategy(GetTransientPackage(), ResolvedStrategy));
	TestEqual(TEXT("Root plus two generated child instances are resolved"), ResolvedStrategy.ProviderInstances.Num(), 3);
	TestTrue(TEXT("Resolved child instances keep the nested multi-instance path"),
		ResolvedStrategy.ProviderInstances.ContainsByPredicate(
			[](const FResolvedFoundationProviderInstance& ProviderInstance)
			{
				return ProviderInstance.DebugPath.Contains(TEXT("ChildFoundations[0:ChildLine].Instances[0:ChildLine]"));
			})
		&& ResolvedStrategy.ProviderInstances.ContainsByPredicate(
			[](const FResolvedFoundationProviderInstance& ProviderInstance)
			{
				return ProviderInstance.DebugPath.Contains(TEXT("ChildFoundations[0:ChildLine].Instances[1:ChildLine]"));
			}));
	TestEqual(TEXT("Root provider snapshot expands the nested multi-instance child for FNE composition"), ResolvedStrategy.ProviderInstances[0].ProviderSnapshot.ChildFoundations.Num(), 2);
	if (ResolvedStrategy.ProviderInstances[0].ProviderSnapshot.ChildFoundations.Num() == 2)
	{
		const FFoundationProviderDefinition* LeftSnapshot = ResolvedStrategy.ProviderInstances[0].ProviderSnapshot.ChildFoundations[0].GetPtr<FFoundationProviderDefinition>();
		const FFoundationProviderDefinition* RightSnapshot = ResolvedStrategy.ProviderInstances[0].ProviderSnapshot.ChildFoundations[1].GetPtr<FFoundationProviderDefinition>();
		const FIslandFoundationShapePayload* LeftPayload = LeftSnapshot != nullptr ? LeftSnapshot->ProviderPayload.GetPtr<FIslandFoundationShapePayload>() : nullptr;
		const FIslandFoundationShapePayload* RightPayload = RightSnapshot != nullptr ? RightSnapshot->ProviderPayload.GetPtr<FIslandFoundationShapePayload>() : nullptr;
		TestTrue(TEXT("Expanded left child snapshot has an island payload"), LeftPayload != nullptr);
		TestTrue(TEXT("Expanded right child snapshot has an island payload"), RightPayload != nullptr);
		if (LeftPayload != nullptr && RightPayload != nullptr)
		{
			TestTrue(*FString::Printf(TEXT("Expanded child centers are generated from child multi-instance arrangement. Left=%s Right=%s"), *LeftPayload->IslandBody.Center.ToString(), *RightPayload->IslandBody.Center.ToString()),
				FMath::IsNearlyEqual(LeftPayload->IslandBody.Center.X, 130.0f, KINDA_SMALL_NUMBER)
				&& FMath::IsNearlyEqual(RightPayload->IslandBody.Center.X, 230.0f, KINDA_SMALL_NUMBER));
		}
	}

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink FoundationDomain = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	float RootDomain = 0.0f;
	float LeftChildDomain = 0.0f;
	float RightChildDomain = 0.0f;
	float GapDomain = 0.0f;
	TestTrue(TEXT("Parent root domain samples at its center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 0.0f, -1.0f), RootDomain));
	TestTrue(TEXT("Nested multi-instance domain samples left child instance"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(130.0f, 20.0f, -1.0f), LeftChildDomain));
	TestTrue(TEXT("Nested multi-instance domain samples right child instance"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(230.0f, 20.0f, -1.0f), RightChildDomain));
	TestTrue(TEXT("Nested multi-instance domain samples gap between child instances"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(180.0f, 0.0f, -1.0f), GapDomain));

	TestTrue(TEXT("Parent root still contributes foundation domain"), RootDomain > 0.0f);
	TestTrue(*FString::Printf(TEXT("Left nested child instance contributes positive foundation domain. Value=%f"), LeftChildDomain), LeftChildDomain > 0.0f);
	TestTrue(*FString::Printf(TEXT("Right nested child instance contributes positive foundation domain. Value=%f"), RightChildDomain), RightChildDomain > 0.0f);
	TestTrue(*FString::Printf(TEXT("Gap between nested generated child instances remains outside foundation domain. Value=%f"), GapDomain), GapDomain < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseMultiInstanceReservationGenAGeneratesBothInstanceReservationsTest,
	"PorismExtension.Biome.IslandNoise.MultiInstanceReservationGenAGeneratesBothInstanceReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseMultiInstanceReservationGenAGeneratesBothInstanceReservationsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceFoundationStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink ReservationGenA = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestReservationBiomeTag());

	float LeftDensity = 0.0f;
	float RightDensity = 0.0f;
	float BetweenDensity = 0.0f;
	TestTrue(TEXT("Resolved multi-instance reservation GenA samples left reservation"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(-110.0f, 0.0f, -1.0f), LeftDensity));
	TestTrue(TEXT("Resolved multi-instance reservation GenA samples right reservation"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(110.0f, 0.0f, -1.0f), RightDensity));
	TestTrue(TEXT("Resolved multi-instance reservation GenA samples gap between reservations"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(0.0f, 0.0f, -1.0f), BetweenDensity));

	TestTrue(TEXT("Left generated reservation contributes solid GenA terrain"), LeftDensity <= 0.0f);
	TestTrue(TEXT("Right generated reservation contributes solid GenA terrain"), RightDensity <= 0.0f);
	TestTrue(TEXT("Gap between generated reservations remains air for reservation GenA"), BetweenDensity > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseMultiInstanceSurfaceAnchorDomainGeneratesRepeatedReservationsTest,
	"PorismExtension.Biome.IslandNoise.MultiInstanceSurfaceAnchorDomainGeneratesRepeatedReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseMultiInstanceSurfaceAnchorDomainGeneratesRepeatedReservationsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceSurfaceAnchorStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestFalse(TEXT("Multi-instance surface-anchor reservation is valid"), Validation.HasErrors());

	FResolvedBiomeStrategy ResolvedStrategy;
	TestTrue(TEXT("Strategy resolves multi-instance surface-anchor reservation"), Strategy->ResolveBiomeStrategy(GetTransientPackage(), ResolvedStrategy));
	TestEqual(TEXT("Single provider remains resolved"), ResolvedStrategy.ProviderInstances.Num(), 1);
	if (ResolvedStrategy.ProviderInstances.Num() == 1)
	{
		TestEqual(TEXT("Provider snapshot expands repeated reservations for FNE composition"), ResolvedStrategy.ProviderInstances[0].ProviderSnapshot.Reservations.Num(), 2);
	}

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);
	TestEqual(TEXT("Each multi-instance surface anchor exposes a tagged field"), Fields.Num(), 2);
	TestTrue(TEXT("Repeated surface-anchor fields are placed from the line arrangement"),
		Fields.ContainsByPredicate(
			[](const FTaggedReservationField& Field)
			{
				return FMath::IsNearlyEqual(static_cast<float>(Field.AuthoredMinBlock.X), -50.0f, KINDA_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(static_cast<float>(Field.AuthoredMaxBlock.X), -30.0f, KINDA_SMALL_NUMBER);
			})
		&& Fields.ContainsByPredicate(
			[](const FTaggedReservationField& Field)
			{
				return FMath::IsNearlyEqual(static_cast<float>(Field.AuthoredMinBlock.X), 30.0f, KINDA_SMALL_NUMBER)
					&& FMath::IsNearlyEqual(static_cast<float>(Field.AuthoredMaxBlock.X), 50.0f, KINDA_SMALL_NUMBER);
			}));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink ReservationDomain = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestReservationBiomeTag());

	float LeftDomain = 0.0f;
	float RightDomain = 0.0f;
	float BetweenDomain = 0.0f;
	TestTrue(TEXT("Multi-instance surface-anchor domain samples left repeated reservation"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(-40.0f, 0.0f, 0.0f), LeftDomain));
	TestTrue(TEXT("Multi-instance surface-anchor domain samples right repeated reservation"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(40.0f, 0.0f, 0.0f), RightDomain));
	TestTrue(TEXT("Multi-instance surface-anchor domain samples gap between repeated reservations"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0f, 0.0f, 0.0f), BetweenDomain));

	TestTrue(TEXT("Left multi-instance surface anchor owns its domain"), LeftDomain > 0.0f);
	TestTrue(TEXT("Right multi-instance surface anchor owns its domain"), RightDomain > 0.0f);
	TestTrue(TEXT("Gap between multi-instance surface anchors remains outside reservation domain"), BetweenDomain < 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseMultiInstanceSurfaceAnchorGenAGeneratesRepeatedReservationsTest,
	"PorismExtension.Biome.IslandNoise.MultiInstanceSurfaceAnchorGenAGeneratesRepeatedReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseMultiInstanceSurfaceAnchorGenAGeneratesRepeatedReservationsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceSurfaceAnchorStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink ReservationGenA = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestReservationBiomeTag());

	float LeftDensity = 0.0f;
	float RightDensity = 0.0f;
	float BetweenDensity = 0.0f;
	TestTrue(TEXT("Multi-instance surface-anchor GenA samples left repeated reservation"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(-40.0f, 0.0f, -1.0f), LeftDensity));
	TestTrue(TEXT("Multi-instance surface-anchor GenA samples right repeated reservation"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(40.0f, 0.0f, -1.0f), RightDensity));
	TestTrue(TEXT("Multi-instance surface-anchor GenA samples gap between repeated reservations"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(0.0f, 0.0f, -1.0f), BetweenDensity));

	TestTrue(TEXT("Left multi-instance surface anchor contributes solid GenA terrain"), LeftDensity <= 0.0f);
	TestTrue(TEXT("Right multi-instance surface anchor contributes solid GenA terrain"), RightDensity <= 0.0f);
	TestTrue(TEXT("Gap between multi-instance surface anchors remains air for reservation GenA"), BetweenDensity > 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseMultiInstanceDirectChildFoundationStaysFixedTest,
	"PorismExtension.Biome.IslandNoise.MultiInstanceDirectChildFoundationStaysFixed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseMultiInstanceDirectChildFoundationStaysFixedTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceWithFixedCenterChildStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestFalse(TEXT("Multi-instance provider allows direct fixed child foundations"), Validation.HasErrors());

	FResolvedBiomeStrategy ResolvedStrategy;
	TestTrue(TEXT("Multi-instance strategy resolves fixed child provider"), Strategy->ResolveBiomeStrategy(GetTransientPackage(), ResolvedStrategy));
	TestEqual(TEXT("Two repeated instances plus one fixed child are resolved"), ResolvedStrategy.ProviderInstances.Num(), 3);
	TestTrue(TEXT("Fixed child keeps a child-foundation debug path"),
		ResolvedStrategy.ProviderInstances.ContainsByPredicate(
			[](const FResolvedFoundationProviderInstance& ProviderInstance)
			{
				return ProviderInstance.DebugPath.Contains(TEXT("ChildFoundations[0:FixedCenter]"));
			}));

	std::vector<FNodeLink> Nodes;
	UFastNoiseEditor* const Editor = CreateTestFastNoiseEditor(Nodes);
	const FNodeLink FoundationDomain = Strategy->BuildBiomeNoise(Editor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestFoundationBiomeTag());

	float FixedChildDomain = 0.0f;
	TestTrue(TEXT("Fixed child provider contributes foundation domain at arrangement center"), EvaluateNoiseAtAuthoredBlock(FoundationDomain, ScaleContext, FVector(0.0f, 15.0f, -1.0f), FixedChildDomain));
	TestTrue(TEXT("Fixed child provider is not repeated with generated instance offsets"), FixedChildDomain > 0.0f);

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);
	TestEqual(TEXT("Repeated prototype reservations plus fixed child reservation are queryable"), Fields.Num(), 3);
	TestTrue(TEXT("Fixed child reservation keeps its provider path"),
		Fields.ContainsByPredicate(
			[](const FTaggedReservationField& Field)
			{
				return Field.DebugPath.Contains(TEXT("ChildFoundations[0:FixedCenter]"))
					&& Field.DebugPath.Contains(TEXT("FixedCenterReservation"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationRejectsOverlappingMultiInstanceProviderTest,
	"PorismExtension.Biome.IslandNoise.ValidationRejectsOverlappingMultiInstanceProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationRejectsOverlappingMultiInstanceProviderTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceFoundationStrategy(10.0f);
	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();

	TestTrue(TEXT("Overlapping multi-instance arrangement is invalid by default"), Validation.HasErrors());
	TestTrue(TEXT("Overlap validation provides actionable multi-instance issue"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("overlap"))
					&& Issue.FixText.Contains(TEXT("Increase spacing"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationRejectsOverlappingMultiInstanceSurfaceAnchorTest,
	"PorismExtension.Biome.IslandNoise.ValidationRejectsOverlappingMultiInstanceSurfaceAnchor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationRejectsOverlappingMultiInstanceSurfaceAnchorTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceSurfaceAnchorStrategy(10.0f);
	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();

	TestTrue(TEXT("Overlapping multi-instance surface-anchor arrangement is invalid by default"), Validation.HasErrors());
	TestTrue(TEXT("Overlap validation provides actionable multi-instance surface-anchor issue"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("overlap"))
					&& Issue.FixText.Contains(TEXT("Increase spacing"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseProviderCapabilitiesAreQueriedGenericallyTest,
	"PorismExtension.Biome.IslandNoise.ProviderCapabilitiesAreQueriedGenerically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseProviderCapabilitiesAreQueriedGenericallyTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();

	const FFoundationProviderQueryCapabilities Capabilities = UBiomeStrategyData::GetProviderQueryCapabilities(Strategy->RootFoundationProvider);
	TestEqual(TEXT("Island providers expose finite surface queries"), Capabilities.SurfaceQueryMode, EFoundationProviderSurfaceQueryMode::Finite);
	TestEqual(TEXT("Island providers expose finite volume queries"), Capabilities.VolumeQueryMode, EFoundationProviderVolumeQueryMode::Finite);

	FFoundationProviderBounds Bounds;
	TestTrue(TEXT("Generic provider bounds query resolves island bounds"), UBiomeStrategyData::QueryProviderBounds(Strategy->RootFoundationProvider, TEXT("RootFoundationProvider"), Bounds));
	TestTrue(TEXT("Generic provider bounds query marks bounds valid"), Bounds.bValid);
	TestTrue(TEXT("Generic provider bounds include provider debug path"), Bounds.DebugPath == TEXT("RootFoundationProvider"));

	FFoundationSurfaceQuery SurfaceQuery;
	SurfaceQuery.CenterXY = FVector2D::ZeroVector;
	SurfaceQuery.SampleMode = EFoundationSurfaceSampleMode::CenterSample;
	FResolvedFoundationSurface Surface;
	TestTrue(TEXT("Generic provider surface query resolves island top center"), UBiomeStrategyData::QueryProviderSurface(
		Strategy->RootFoundationProvider,
		TEXT("RootFoundationProvider"),
		SurfaceQuery,
		ResolveTestScaleContext(Strategy),
		Surface));
	TestTrue(TEXT("Generic provider surface query returns provider debug path"), Surface.DebugPath.Contains(TEXT("RootFoundationProvider")));
	TestTrue(TEXT("Generic provider surface query preserves top-center convention"), FMath::IsNearlyEqual(Surface.SurfaceZBlock, 0.0f, KINDA_SMALL_NUMBER));

	return true;
}

