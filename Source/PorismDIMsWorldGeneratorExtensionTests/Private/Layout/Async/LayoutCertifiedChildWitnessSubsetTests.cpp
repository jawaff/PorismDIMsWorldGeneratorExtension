// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutCertifiedChildWitnessSubset.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutWorkerSolvePacket.h"

#include "Layout/Solver/LayoutProfileSolver.h"
#include "Misc/AutomationTest.h"

namespace
{
	FLayoutValidationAssertionRecord MakeWitnessSubsetParentAssertion(const FLayoutId AssertionId, const bool bPassed = true)
	{
		FLayoutValidationAssertionRecord Assertion;
		Assertion.AssertionId = AssertionId;
		Assertion.bPassed = bPassed;
		return Assertion;
	}

	TArray<FLayoutValidationAssertionRecord> MakeWitnessSubsetParentAssertions()
	{
		TArray<FLayoutValidationAssertionRecord> Assertions;
		Assertions.Add(MakeWitnessSubsetParentAssertion(TEXT("Parent.Assertion.A")));
		Assertions.Add(MakeWitnessSubsetParentAssertion(TEXT("Parent.Assertion.B")));
		Assertions.Add(MakeWitnessSubsetParentAssertion(TEXT("Parent.Assertion.Failed"), false));
		return Assertions;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetAbsentArtifactsEmitEmptyTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.AbsentArtifactsEmitEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetAbsentArtifactsEmitEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Missing witness subset artifacts emit empty subsets"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsets(
			nullptr,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Traversal subset remains empty without explicit artifact"), Subsets.SelectedTraversalCapabilityIds.IsEmpty());
	TestTrue(TEXT("Parent assertion subset remains empty without explicit artifact"), Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetProducedArtifactAcceptedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.ProducedArtifactAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetProducedArtifactAcceptedTest::RunTest(const FString& Parameters)
{
	FLayoutProducedCertifiedChildWitnessSubsetArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Produced witness subset artifact builds from explicit ids"),
		LayoutCertifiedChildWitnessSubset::TryBuildProducedArtifact(
			TEXT("ChildWitnessSubset.Test"),
			{TEXT("Traversal.Capability.Primary")},
			{TEXT("Parent.Assertion.B")},
			TEXT("TestProducer"),
			Artifact,
			FailureReason));

	FLayoutCertifiedChildWitnessSubsets Subsets;
	TestTrue(
		TEXT("Produced witness subset artifact extracts exact ids"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromProducedArtifact(
			&Artifact,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("Produced traversal id emitted"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Produced parent assertion id emitted"), Subsets.ParentAssertionIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetAbsentProducedArtifactEmitsEmptyTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.AbsentProducedArtifactEmitsEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetAbsentProducedArtifactEmitsEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Missing produced witness artifact emits empty subsets"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromProducedArtifact(
			nullptr,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Missing produced witness artifact emits no traversal ids"), Subsets.SelectedTraversalCapabilityIds.IsEmpty());
	TestTrue(TEXT("Missing produced witness artifact emits no parent assertion ids"), Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetAuthoritativeArtifactsAcceptedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.AuthoritativeArtifactsAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetAuthoritativeArtifactsAcceptedTest::RunTest(const FString& Parameters)
{
	const FLayoutChildTraversalWitnessMappingArtifact TraversalArtifact =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
			{TEXT("Traversal.Capability.Primary")});
	const FLayoutChildParentAssertionSubsetMappingArtifact AssertionArtifact =
		LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact(
			{TEXT("Parent.Assertion.B")});

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Separate authoritative artifacts extract exact witness ids"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
			&TraversalArtifact,
			&AssertionArtifact,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("One traversal artifact id emitted"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("One parent assertion artifact id emitted"), Subsets.ParentAssertionIds.Num(), 1);
	if (Subsets.SelectedTraversalCapabilityIds.Num() == 1)
	{
		TestEqual(TEXT("Traversal artifact id preserved"), Subsets.SelectedTraversalCapabilityIds[0], FLayoutId(TEXT("Traversal.Capability.Primary")));
	}
	if (Subsets.ParentAssertionIds.Num() == 1)
	{
		TestEqual(TEXT("Parent assertion artifact id preserved"), Subsets.ParentAssertionIds[0], FLayoutId(TEXT("Parent.Assertion.B")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetMissingAuthoritativeArtifactsStayEmptyTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.MissingAuthoritativeArtifactsStayEmpty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetMissingAuthoritativeArtifactsStayEmptyTest::RunTest(const FString& Parameters)
{
	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Missing authoritative artifacts emit empty subsets"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
			nullptr,
			nullptr,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Missing traversal artifact emits no traversal ids"), Subsets.SelectedTraversalCapabilityIds.IsEmpty());
	TestTrue(TEXT("Missing assertion artifact emits no parent assertion ids"), Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetPartialAuthoritativeArtifactsStayPartialTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.PartialAuthoritativeArtifactsStayPartial",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetPartialAuthoritativeArtifactsStayPartialTest::RunTest(const FString& Parameters)
{
	const FLayoutChildTraversalWitnessMappingArtifact TraversalArtifact =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
			{TEXT("Traversal.Capability.Primary")});

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Single authoritative artifact extracts only that subset"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
			&TraversalArtifact,
			nullptr,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("Traversal id preserved"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestTrue(TEXT("Missing parent assertion artifact emits no parent assertion ids"), Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetInvalidAuthoritativeArtifactsFailClosedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.InvalidAuthoritativeArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetInvalidAuthoritativeArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	const FLayoutChildTraversalWitnessMappingArtifact TraversalArtifact =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(
			{TEXT("Traversal.Capability.Primary"), TEXT("Traversal.Capability.Primary")});

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestFalse(
		TEXT("Invalid authoritative artifact fails closed"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
			&TraversalArtifact,
			nullptr,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Invalid authoritative artifact emits no subsets"), Subsets.SelectedTraversalCapabilityIds.IsEmpty() && Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetRequestCarriedArtifactsAcceptedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.RequestCarriedArtifactsAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetRequestCarriedArtifactsAcceptedTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest ChildRequest;
	ChildRequest.bHasCertifiedSelectedTraversalCapabilityArtifact = true;
	ChildRequest.CertifiedSelectedTraversalCapabilityIds = {TEXT("Traversal.Capability.Primary")};
	ChildRequest.bHasCertifiedParentAssertionSubsetArtifact = true;
	ChildRequest.CertifiedParentAssertionSubsetIds = {TEXT("Parent.Assertion.B")};

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Request-carried authoritative artifacts extract exact witness ids"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromRequestCarriedArtifacts(
			ChildRequest,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("Request-carried traversal id emitted"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Request-carried assertion id emitted"), Subsets.ParentAssertionIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetRequestArraysIgnoredWithoutArtifactFlagsTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.RequestArraysIgnoredWithoutArtifactFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetRequestArraysIgnoredWithoutArtifactFlagsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest ChildRequest;
	ChildRequest.CertifiedSelectedTraversalCapabilityIds = {TEXT("Traversal.Capability.Primary")};
	ChildRequest.CertifiedParentAssertionSubsetIds = {TEXT("Parent.Assertion.B")};

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Unflagged request arrays do not synthesize witness subsets"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromRequestCarriedArtifacts(
			ChildRequest,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Unflagged traversal array ignored"), Subsets.SelectedTraversalCapabilityIds.IsEmpty());
	TestTrue(TEXT("Unflagged assertion array ignored"), Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetRequestFlaggedEmptyArtifactsFailClosedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.RequestFlaggedEmptyArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetRequestFlaggedEmptyArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest ChildRequest;
	ChildRequest.bHasCertifiedSelectedTraversalCapabilityArtifact = true;

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestFalse(
		TEXT("Flagged empty request-carried traversal artifact fails closed"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromRequestCarriedArtifacts(
			ChildRequest,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Flagged empty artifact emits no subsets"), Subsets.SelectedTraversalCapabilityIds.IsEmpty() && Subsets.ParentAssertionIds.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetRequestManifestRoundTripsArtifactFlagsTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.RequestManifestRoundTripsArtifactFlags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetRequestManifestRoundTripsArtifactFlagsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.bHasCertifiedSelectedTraversalCapabilityArtifact = true;
	Request.CertifiedSelectedTraversalCapabilityIds = {TEXT("Traversal.Capability.Primary")};
	Request.CertifiedSelectedClosureSpanCapabilityId = TEXT("ClosureSpan.Capability.Primary");
	Request.bHasCertifiedParentAssertionSubsetArtifact = true;
	Request.CertifiedParentAssertionSubsetIds = {TEXT("Parent.Assertion.B")};

	const FLayoutWorkerSolveRequestManifest Manifest = FLayoutWorkerSolveRequestManifest::CaptureFromSolveRequest(Request);
	TestTrue(TEXT("Manifest carries selected traversal artifact flag"), Manifest.bHasCertifiedSelectedTraversalCapabilityArtifact);
	TestTrue(TEXT("Manifest carries parent assertion artifact flag"), Manifest.bHasCertifiedParentAssertionSubsetArtifact);
	TestEqual(TEXT("Manifest carries traversal artifact payload"), Manifest.CertifiedSelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Manifest carries closure-span artifact payload"), Manifest.CertifiedSelectedClosureSpanCapabilityId, FLayoutId(TEXT("ClosureSpan.Capability.Primary")));
	TestEqual(TEXT("Manifest carries parent assertion artifact payload"), Manifest.CertifiedParentAssertionSubsetIds.Num(), 1);

	FLayoutRegionSolveRequest RehydratedRequest;
	Manifest.PopulateSolveRequest(RehydratedRequest);
	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Rehydrated request-carried artifacts extract exact witness ids"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromRequestCarriedArtifacts(
			RehydratedRequest,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("Rehydrated traversal id emitted"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Rehydrated closure-span id preserved"), RehydratedRequest.CertifiedSelectedClosureSpanCapabilityId, FLayoutId(TEXT("ClosureSpan.Capability.Primary")));
	TestEqual(TEXT("Rehydrated assertion id emitted"), Subsets.ParentAssertionIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetHandoffArtifactsAcceptedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.HandoffArtifactsAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetHandoffArtifactsAcceptedTest::RunTest(const FString& Parameters)
{
	FLayoutChildSolveHandoff Handoff;
	Handoff.bHasCertifiedSelectedTraversalCapabilityArtifact = true;
	Handoff.CertifiedSelectedTraversalCapabilityIds = {TEXT("Traversal.Capability.Primary")};
	Handoff.bHasCertifiedParentAssertionSubsetArtifact = true;
	Handoff.CertifiedParentAssertionSubsetIds = {TEXT("Parent.Assertion.B")};

	const FLayoutChildTraversalWitnessMappingArtifact TraversalArtifact =
		LayoutCertifiedChildTraversalWitness::BuildExplicitTraversalWitnessArtifact(Handoff.CertifiedSelectedTraversalCapabilityIds);
	const FLayoutChildParentAssertionSubsetMappingArtifact AssertionArtifact =
		LayoutCertifiedChildAssertionSubset::BuildExplicitMappingArtifact(Handoff.CertifiedParentAssertionSubsetIds);

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Handoff-carried authoritative artifacts extract exact witness ids"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsetsFromAuthoritativeArtifacts(
			&TraversalArtifact,
			&AssertionArtifact,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("Handoff traversal artifact emitted"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Handoff assertion artifact emitted"), Subsets.ParentAssertionIds.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetExplicitArtifactsAcceptedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.ExplicitArtifactsAccepted",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetExplicitArtifactsAcceptedTest::RunTest(const FString& Parameters)
{
	const FLayoutCertifiedChildWitnessSubsetArtifacts Artifacts =
		LayoutCertifiedChildWitnessSubset::BuildArtifactsFromExplicitIds(
			{TEXT("Traversal.Capability.Primary")},
			{TEXT("Parent.Assertion.B")});

	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit subset artifacts extract exact witness ids"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsets(
			&Artifacts,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestEqual(TEXT("One traversal id emitted"), Subsets.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Traversal id preserved"), Subsets.SelectedTraversalCapabilityIds[0], FLayoutId(TEXT("Traversal.Capability.Primary")));
	TestEqual(TEXT("One parent assertion id emitted"), Subsets.ParentAssertionIds.Num(), 1);
	TestEqual(TEXT("Parent assertion id preserved"), Subsets.ParentAssertionIds[0], FLayoutId(TEXT("Parent.Assertion.B")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessSubsetInvalidArtifactsFailClosedTest,
	"PorismExtension.Layout.Async.ChildWitnessSubset.InvalidArtifactsFailClosed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessSubsetInvalidArtifactsFailClosedTest::RunTest(const FString& Parameters)
{
	FLayoutCertifiedChildWitnessSubsets Subsets;
	FString FailureReason;

	const FLayoutCertifiedChildWitnessSubsetArtifacts BadTraversalArtifacts =
		LayoutCertifiedChildWitnessSubset::BuildArtifactsFromExplicitIds(
			{TEXT("Traversal.Capability.Primary"), TEXT("Traversal.Capability.Primary")},
			{TEXT("Parent.Assertion.B")});
	TestFalse(
		TEXT("Duplicate explicit traversal id fails closed"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsets(
			&BadTraversalArtifacts,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Invalid traversal artifact emits no subsets"), Subsets.SelectedTraversalCapabilityIds.IsEmpty() && Subsets.ParentAssertionIds.IsEmpty());

	const FLayoutCertifiedChildWitnessSubsetArtifacts BadAssertionArtifacts =
		LayoutCertifiedChildWitnessSubset::BuildArtifactsFromExplicitIds(
			{TEXT("Traversal.Capability.Primary")},
			{TEXT("Parent.Assertion.Failed")});
	TestFalse(
		TEXT("Failed explicit parent assertion id fails closed"),
		LayoutCertifiedChildWitnessSubset::TryExtractWitnessSubsets(
			&BadAssertionArtifacts,
			MakeWitnessSubsetParentAssertions(),
			Subsets,
			FailureReason));
	TestTrue(TEXT("Invalid assertion artifact emits no subsets"), Subsets.SelectedTraversalCapabilityIds.IsEmpty() && Subsets.ParentAssertionIds.IsEmpty());
	return true;
}
