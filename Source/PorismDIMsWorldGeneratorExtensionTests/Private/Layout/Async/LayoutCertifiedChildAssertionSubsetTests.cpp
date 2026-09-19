// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutCertifiedChildAssertionSubset.h"

#include "Misc/AutomationTest.h"

namespace
{
	FLayoutValidationAssertionRecord MakeParentAssertion(const FLayoutId AssertionId, const bool bPassed = true)
	{
		FLayoutValidationAssertionRecord Assertion;
		Assertion.AssertionId = AssertionId;
		Assertion.bPassed = bPassed;
		return Assertion;
	}

	TArray<FLayoutValidationAssertionRecord> MakeParentAssertionPool()
	{
		TArray<FLayoutValidationAssertionRecord> Assertions;
		Assertions.Add(MakeParentAssertion(TEXT("Parent.Assertion.A")));
		Assertions.Add(MakeParentAssertion(TEXT("Parent.Assertion.B")));
		Assertions.Add(MakeParentAssertion(TEXT("Parent.Assertion.Failed"), false));
		return Assertions;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildAssertionSubsetAbsentMappingEmitsEmptyTest,
	"PorismExtension.Layout.Async.ChildAssertionSubset.AbsentMappingEmitsEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildAssertionSubsetAbsentMappingEmitsEmptyTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutValidationAssertionRecord> ParentAssertions = MakeParentAssertionPool();
	TArray<FLayoutId> SubsetIds;
	FString FailureReason;
	TestTrue(
		TEXT("Missing mapping succeeds with empty subset"),
		LayoutCertifiedChildAssertionSubset::TryExtractParentAssertionSubsetIds(
			nullptr,
			ParentAssertions,
			SubsetIds,
			FailureReason));
	TestTrue(TEXT("Missing mapping does not copy full assertion pool"), SubsetIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildAssertionSubsetExplicitMappingAcceptedTest,
	"PorismExtension.Layout.Async.ChildAssertionSubset.ExplicitMappingAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildAssertionSubsetExplicitMappingAcceptedTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutValidationAssertionRecord> ParentAssertions = MakeParentAssertionPool();
	const FLayoutChildParentAssertionSubsetMappingArtifact Mapping =
		LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact({TEXT("Parent.Assertion.B")});

	TArray<FLayoutId> SubsetIds;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit mapping extracts exact subset"),
		LayoutCertifiedChildAssertionSubset::TryExtractParentAssertionSubsetIds(
			&Mapping,
			ParentAssertions,
			SubsetIds,
			FailureReason));
	TestEqual(TEXT("Only explicitly mapped assertion is included"), SubsetIds.Num(), 1);
	TestEqual(TEXT("Mapped assertion id preserved"), SubsetIds[0], FLayoutId(TEXT("Parent.Assertion.B")));

	TArray<FLayoutId> WitnessIds;
	TestTrue(
		TEXT("Exact subset resolves witness ids"),
		LayoutCertifiedChildAssertionSubset::TryResolveParentWitnessAssertionIds(
			ParentAssertions,
			SubsetIds,
			WitnessIds,
			FailureReason));
	TestEqual(TEXT("Witness contains exact subset only"), WitnessIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildAssertionSubsetRejectsMissingOrFailedMappingTest,
	"PorismExtension.Layout.Async.ChildAssertionSubset.RejectsMissingOrFailedMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildAssertionSubsetRejectsMissingOrFailedMappingTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutValidationAssertionRecord> ParentAssertions = MakeParentAssertionPool();
	TArray<FLayoutId> SubsetIds;
	FString FailureReason;

	const FLayoutChildParentAssertionSubsetMappingArtifact MissingMapping =
		LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact({TEXT("Parent.Assertion.Missing")});
	TestFalse(
		TEXT("Missing assertion id fails closed"),
		LayoutCertifiedChildAssertionSubset::TryExtractParentAssertionSubsetIds(
			&MissingMapping,
			ParentAssertions,
			SubsetIds,
			FailureReason));
	TestTrue(TEXT("Missing assertion leaves no subset"), SubsetIds.IsEmpty());

	const FLayoutChildParentAssertionSubsetMappingArtifact FailedMapping =
		LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact({TEXT("Parent.Assertion.Failed")});
	TestFalse(
		TEXT("Failed assertion id fails closed"),
		LayoutCertifiedChildAssertionSubset::TryExtractParentAssertionSubsetIds(
			&FailedMapping,
			ParentAssertions,
			SubsetIds,
			FailureReason));
	TestTrue(TEXT("Failed assertion leaves no subset"), SubsetIds.IsEmpty());
	return true;
}
