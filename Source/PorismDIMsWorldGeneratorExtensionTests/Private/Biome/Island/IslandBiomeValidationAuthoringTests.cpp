// Copyright 2026 Spotted Loaf Studio

#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationRejectsMisconfiguredVCutVoidTest,
	"PorismExtension.Biome.IslandNoise.ValidationRejectsMisconfiguredVCutVoid",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationRejectsMisconfiguredVCutVoidTest::RunTest(const FString& Parameters)
{
	FVCutFoundationPayload VCutPayload;
	VCutPayload.SurfaceHalfWidthBlocks = 4.0f;
	VCutPayload.BottomHalfWidthBlocks = 12.0f;

	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	FFoundationProviderDefinition VCutProvider;
	VCutProvider.DebugName = TEXT("BadVCut");
	VCutProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
	VCutProvider.BiomeTag = TestReservationBiomeTag();
	VCutProvider.ProviderType = EFoundationProviderType::VCutVoid;
	VCutProvider.ProviderPayload.InitializeAs<FVCutFoundationPayload>(VCutPayload);
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(VCutProvider));

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestTrue(TEXT("Misconfigured V-cut provider is rejected"), Validation.HasErrors());
	TestTrue(TEXT("V-cut validation requires subtractive contribution"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("not subtractive"))
					&& Issue.FixText.Contains(TEXT("Subtractive Void"));
			}));
	TestTrue(TEXT("V-cut validation rejects bottom wider than surface"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("wider at the bottom"))
					&& Issue.FixText.Contains(TEXT("Surface Half Width"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationRejectsOverlappingSiblingProvidersTest,
	"PorismExtension.Biome.IslandNoise.ValidationRejectsOverlappingSiblingProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationRejectsOverlappingSiblingProvidersTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("Left"), TestFoundationBiomeTag(), FVector(100.0f, 0.0f, 0.0f), 35.0f)));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("Right"), TestFoundationBiomeTag(), FVector(120.0f, 0.0f, 0.0f), 35.0f)));

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestTrue(TEXT("Overlapping sibling providers are invalid outside explicit hierarchy rules"), Validation.HasErrors());
	TestTrue(TEXT("Sibling provider overlap warning explains hierarchy/multi-instance fix"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("overlap as siblings"))
					&& Issue.FixText.Contains(TEXT("hierarchy carve/union"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseValidationRejectsOverlappingDifferentBiomeReservationsTest,
	"PorismExtension.Biome.IslandNoise.ValidationRejectsOverlappingDifferentBiomeReservations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseValidationRejectsOverlappingDifferentBiomeReservationsTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	Strategy->RootFoundationProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("ReservationA"), TestReservationBiomeTag()));
	Strategy->RootFoundationProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("ReservationB"), TestFoundationBiomeTag()));

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestTrue(TEXT("Overlapping reservations with different biome tags are invalid by default"), Validation.HasErrors());
	TestTrue(TEXT("Reservation overlap error explains same-tag union or hierarchy fix"),
		Validation.Issues.ContainsByPredicate(
			[](const FBiomeStrategyValidationIssue& Issue)
			{
				return Issue.Severity == EBiomeStrategyValidationSeverity::Error
					&& Issue.Message.Contains(TEXT("overlap"))
					&& Issue.FixText.Contains(TEXT("same Biome Tag"));
			}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseProviderQueriesRemainOptionalForNoiseGenerationTest,
	"PorismExtension.Biome.IslandNoise.ProviderQueriesRemainOptionalForNoiseGeneration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseProviderQueriesRemainOptionalForNoiseGenerationTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoDetailMainMenuIslandStrategy();
	const FResolvedWorldGenScaleContext ScaleContext = ResolveTestScaleContext(Strategy);

	std::vector<FNodeLink> DomainNodes;
	UFastNoiseEditor* const DomainEditor = CreateTestFastNoiseEditor(DomainNodes);
	const FNodeLink ReservationDomain = Strategy->BuildBiomeNoise(DomainEditor, GetTransientPackage(), EBiomeNoiseSlot::DomainNoise, TestReservationBiomeTag());

	std::vector<FNodeLink> GenANodes;
	UFastNoiseEditor* const GenAEditor = CreateTestFastNoiseEditor(GenANodes);
	const FNodeLink ReservationGenA = Strategy->BuildBiomeNoise(GenAEditor, GetTransientPackage(), EBiomeNoiseSlot::GenA, TestReservationBiomeTag());

	float DomainValue = 0.0f;
	float GenAValue = 0.0f;
	TestTrue(TEXT("Reservation domain can be sampled without a layout-generator query consumer"), EvaluateNoiseAtAuthoredBlock(ReservationDomain, ScaleContext, FVector(0.0f, 0.0f, 0.0f), DomainValue));
	TestTrue(TEXT("Reservation GenA can be sampled without a layout-generator query consumer"), EvaluateNoiseAtAuthoredBlock(ReservationGenA, ScaleContext, FVector(0.0f, 0.0f, -1.0f), GenAValue));
	TestTrue(TEXT("Reservation domain remains valid fallback data"), DomainValue > 0.0f);
	TestTrue(TEXT("Reservation GenA remains valid fallback terrain"), GenAValue <= 0.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseProviderMetadataStaysLayoutAgnosticTest,
	"PorismExtension.Biome.IslandNoise.ProviderMetadataStaysLayoutAgnostic",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseProviderMetadataStaysLayoutAgnosticTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateLineMultiInstanceSurfaceAnchorStrategy();
	Strategy->RootFoundationProvider.Reservations[0].FieldTags.AddTag(TestReservationBiomeTag());

	const FBiomeStrategyValidationResult Validation = Strategy->ValidateStrategy();
	TestFalse(TEXT("Generic provider/reservation metadata validates without project-specific meanings"), Validation.HasErrors());

	TArray<FTaggedReservationField> Fields;
	Strategy->QueryTaggedReservationFields(GetTransientPackage(), FBox(ForceInit), Fields);
	TestTrue(TEXT("Tagged reservation fields are available through generic biome tags"), Fields.Num() > 0);

	const TArray<FString> BannedTerms = { TEXT("Bridge"), TEXT("Road"), TEXT("Cave"), TEXT("Castle"), TEXT("Spawn") };
	for (const FTaggedReservationField& Field : Fields)
	{
		for (const FString& BannedTerm : BannedTerms)
		{
			TestFalse(
				*FString::Printf(TEXT("Provider metadata remains layout-agnostic and does not hardcode '%s'."), *BannedTerm),
				Field.DebugPath.Contains(BannedTerm) || Field.DebugName.ToString().Contains(BannedTerm));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseAuthoringToolsMoveReservationsWithinProviderTest,
	"PorismExtension.Biome.IslandNoise.AuthoringToolsMoveReservationsWithinProvider",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseAuthoringToolsMoveReservationsWithinProviderTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	Strategy->RootFoundationProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("First"), TestReservationBiomeTag()));
	Strategy->RootFoundationProvider.Reservations.Add(CreateProviderLocalBoxReservation(TEXT("Second"), TestReservationBiomeTag()));

	Strategy->AuthoringTargetProviderPath = TEXT("RootFoundationProvider");
	Strategy->AuthoringTargetReservationIndex = 1;
	TestTrue(TEXT("Reservation authoring tool moves target earlier"), Strategy->MoveTargetReservationEarlier());
	TestEqual(TEXT("Moved reservation becomes first"), Strategy->RootFoundationProvider.Reservations[0].DebugName, FName(TEXT("Second")));
	TestEqual(TEXT("Target reservation index follows the moved reservation"), Strategy->AuthoringTargetReservationIndex, 0);

	TestTrue(TEXT("Reservation authoring tool moves target later"), Strategy->MoveTargetReservationLater());
	TestEqual(TEXT("Moved reservation returns to second"), Strategy->RootFoundationProvider.Reservations[1].DebugName, FName(TEXT("Second")));
	TestEqual(TEXT("Target reservation index follows later move"), Strategy->AuthoringTargetReservationIndex, 1);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseAuthoringToolsMoveProvidersWithinSiblingListTest,
	"PorismExtension.Biome.IslandNoise.AuthoringToolsMoveProvidersWithinSiblingList",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseAuthoringToolsMoveProvidersWithinSiblingListTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("A"), TestFoundationBiomeTag(), FVector(-100.0f, 0.0f, 0.0f))));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("B"), TestFoundationBiomeTag(), FVector::ZeroVector)));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("C"), TestFoundationBiomeTag(), FVector(100.0f, 0.0f, 0.0f))));

	Strategy->AuthoringTargetProviderPath = TEXT("RootFoundationProvider.ChildFoundations[1:B]");
	TestTrue(TEXT("Provider authoring tool moves target earlier"), Strategy->MoveTargetProviderEarlier());
	TestEqual(TEXT("Provider B moves before A"), Strategy->RootFoundationProvider.ChildFoundations[0].GetPtr<FFoundationProviderDefinition>()->DebugName, FName(TEXT("B")));
	TestEqual(TEXT("Provider target path follows earlier move"), Strategy->AuthoringTargetProviderPath, FString(TEXT("RootFoundationProvider.ChildFoundations[0:B]")));

	TestTrue(TEXT("Provider authoring tool moves target later"), Strategy->MoveTargetProviderLater());
	TestEqual(TEXT("Provider B moves back after A"), Strategy->RootFoundationProvider.ChildFoundations[1].GetPtr<FFoundationProviderDefinition>()->DebugName, FName(TEXT("B")));
	TestEqual(TEXT("Provider target path follows later move"), Strategy->AuthoringTargetProviderPath, FString(TEXT("RootFoundationProvider.ChildFoundations[1:B]")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FIslandBiomeFastNoiseAuthoringToolsPromoteAndDemoteProvidersTest,
	"PorismExtension.Biome.IslandNoise.AuthoringToolsPromoteAndDemoteProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FIslandBiomeFastNoiseAuthoringToolsPromoteAndDemoteProvidersTest::RunTest(const FString& Parameters)
{
	UBiomeStrategyData* const Strategy = CreateNoReservationHierarchyStrategy();
	FFoundationProviderDefinition ParentProvider = CreateNoDetailIslandProvider(TEXT("Parent"), TestFoundationBiomeTag(), FVector(-100.0f, 0.0f, 0.0f));
	ParentProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("Child"), TestFoundationBiomeTag(), FVector(-100.0f, 0.0f, 0.0f))));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(ParentProvider));
	Strategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("Sibling"), TestFoundationBiomeTag(), FVector(100.0f, 0.0f, 0.0f))));

	Strategy->AuthoringTargetProviderPath = TEXT("RootFoundationProvider.ChildFoundations[0:Parent].ChildFoundations[0:Child]");
	TestTrue(TEXT("Provider authoring tool promotes target one level"), Strategy->PromoteTargetProviderOneLevel());
	TestEqual(TEXT("Promoted child is inserted after parent"), Strategy->RootFoundationProvider.ChildFoundations[1].GetPtr<FFoundationProviderDefinition>()->DebugName, FName(TEXT("Child")));
	TestEqual(TEXT("Original parent no longer owns promoted child"), Strategy->RootFoundationProvider.ChildFoundations[0].GetPtr<FFoundationProviderDefinition>()->ChildFoundations.Num(), 0);
	TestEqual(TEXT("Provider target path follows promotion"), Strategy->AuthoringTargetProviderPath, FString(TEXT("RootFoundationProvider.ChildFoundations[1:Child]")));

	Strategy->AuthoringTargetProviderPath = TEXT("RootFoundationProvider.ChildFoundations[2:Sibling]");
	TestTrue(TEXT("Provider authoring tool demotes target into previous sibling"), Strategy->DemoteTargetProviderIntoPreviousSibling());
	const FFoundationProviderDefinition* const ChildProvider = Strategy->RootFoundationProvider.ChildFoundations[1].GetPtr<FFoundationProviderDefinition>();
	TestNotNull(TEXT("Promoted child provider remains available"), ChildProvider);
	if (ChildProvider == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Sibling demotes into the promoted child"), ChildProvider != nullptr ? ChildProvider->ChildFoundations.Num() : 0, 1);
	if (ChildProvider->ChildFoundations.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Demoted sibling keeps its definition"), ChildProvider->ChildFoundations[0].GetPtr<FFoundationProviderDefinition>()->DebugName, FName(TEXT("Sibling")));
	TestEqual(TEXT("Provider target path follows previous-sibling demotion"), Strategy->AuthoringTargetProviderPath, FString(TEXT("RootFoundationProvider.ChildFoundations[1:Child].ChildFoundations[0:Sibling]")));

	UBiomeStrategyData* const NextStrategy = CreateNoReservationHierarchyStrategy();
	NextStrategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("Left"), TestFoundationBiomeTag(), FVector(-100.0f, 0.0f, 0.0f))));
	NextStrategy->RootFoundationProvider.ChildFoundations.Add(FInstancedStruct::Make(CreateNoDetailIslandProvider(TEXT("Right"), TestFoundationBiomeTag(), FVector(100.0f, 0.0f, 0.0f))));
	NextStrategy->AuthoringTargetProviderPath = TEXT("RootFoundationProvider.ChildFoundations[0:Left]");
	TestTrue(TEXT("Provider authoring tool demotes target into next sibling"), NextStrategy->DemoteTargetProviderIntoNextSibling());
	const FFoundationProviderDefinition* const RightProvider = NextStrategy->RootFoundationProvider.ChildFoundations[0].GetPtr<FFoundationProviderDefinition>();
	TestNotNull(TEXT("Right provider remains available"), RightProvider);
	if (RightProvider == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Right provider has one demoted child"), RightProvider->ChildFoundations.Num(), 1);
	if (RightProvider->ChildFoundations.Num() != 1)
	{
		return false;
	}
	TestEqual(TEXT("Left demotes into right"), RightProvider->ChildFoundations[0].GetPtr<FFoundationProviderDefinition>()->DebugName, FName(TEXT("Left")));
	TestEqual(TEXT("Provider target path follows next-sibling demotion"), NextStrategy->AuthoringTargetProviderPath, FString(TEXT("RootFoundationProvider.ChildFoundations[0:Right].ChildFoundations[0:Left]")));

	return true;
}

