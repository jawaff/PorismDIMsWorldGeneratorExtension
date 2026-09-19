// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutCertifiedChildTraversalWitness.h"

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildTraversalWitnessAbsentMappingEmitsEmptyTest,
	"PorismExtension.Layout.Async.ChildTraversalWitness.AbsentMappingEmitsEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildTraversalWitnessAbsentMappingEmitsEmptyTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutId> SelectedTraversalIds;
	FString FailureReason;
	TestTrue(
		TEXT("Missing traversal witness succeeds with empty subset"),
		LayoutCertifiedChildTraversalWitness::TryExtractSelectedTraversalCapabilityIds(
			nullptr,
			SelectedTraversalIds,
			FailureReason));
	TestTrue(TEXT("Missing traversal witness does not synthesize ids"), SelectedTraversalIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildTraversalWitnessExplicitIdsAcceptedTest,
	"PorismExtension.Layout.Async.ChildTraversalWitness.ExplicitIdsAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildTraversalWitnessExplicitIdsAcceptedTest::RunTest(const FString& Parameters)
{
	const FLayoutChildTraversalWitnessMappingArtifact Mapping =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact({TEXT("Traversal.Capability.Primary")});

	TArray<FLayoutId> SelectedTraversalIds;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit traversal ids pass through unchanged"),
		LayoutCertifiedChildTraversalWitness::TryExtractSelectedTraversalCapabilityIds(
			&Mapping,
			SelectedTraversalIds,
			FailureReason));
	TestEqual(TEXT("One explicit traversal id emitted"), SelectedTraversalIds.Num(), 1);
	TestEqual(TEXT("Explicit traversal id preserved"), SelectedTraversalIds[0], FLayoutId(TEXT("Traversal.Capability.Primary")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildTraversalWitnessRejectsEmptyOrDuplicateIdsTest,
	"PorismExtension.Layout.Async.ChildTraversalWitness.RejectsEmptyOrDuplicateIds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildTraversalWitnessRejectsEmptyOrDuplicateIdsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutId> SelectedTraversalIds;
	FString FailureReason;

	const FLayoutChildTraversalWitnessMappingArtifact EmptyIdMapping =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact({FLayoutId()});
	TestFalse(
		TEXT("Empty traversal id fails closed"),
		LayoutCertifiedChildTraversalWitness::TryExtractSelectedTraversalCapabilityIds(
			&EmptyIdMapping,
			SelectedTraversalIds,
			FailureReason));
	TestTrue(TEXT("Empty traversal id leaves no selected ids"), SelectedTraversalIds.IsEmpty());

	const FLayoutChildTraversalWitnessMappingArtifact DuplicateMapping =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact({TEXT("Traversal.Capability.Primary"), TEXT("Traversal.Capability.Primary")});
	TestFalse(
		TEXT("Duplicate traversal id fails closed"),
		LayoutCertifiedChildTraversalWitness::TryExtractSelectedTraversalCapabilityIds(
			&DuplicateMapping,
			SelectedTraversalIds,
			FailureReason));
	TestTrue(TEXT("Duplicate traversal id leaves no selected ids"), SelectedTraversalIds.IsEmpty());
	return true;
}
