// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutCertifiedChildWitnessArtifactProducer.h"

#include "Misc/AutomationTest.h"

namespace
{
	TArray<FLayoutValidationAssertionRecord> BuildParentAssertions()
	{
		TArray<FLayoutValidationAssertionRecord> Assertions;
		FLayoutValidationAssertionRecord First;
		First.AssertionId = TEXT("Assertion.Parent.0");
		First.bPassed = true;
		Assertions.Add(First);

		FLayoutValidationAssertionRecord Failed;
		Failed.AssertionId = TEXT("Assertion.Parent.Failed");
		Failed.bPassed = false;
		Assertions.Add(Failed);
		return Assertions;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerBuildsSelectedTraversalTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.BuildsSelectedTraversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerBuildsSelectedTraversalTest::RunTest(const FString& Parameters)
{
	FLayoutProducedSelectedTraversalArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit selected traversal ids build produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
			TEXT("Artifact.Traversal.0"),
			TEXT("ChildScout.0"),
			{ TEXT("Traversal.Capability.0"), TEXT("Traversal.Capability.1") },
			Artifact,
			FailureReason));
	TestTrue(TEXT("Traversal produced artifact marked present"), Artifact.bHasProducedArtifact);

	FLayoutChildTraversalWitnessMappingArtifact MappingArtifact;
	TestTrue(
		TEXT("Produced traversal artifact converts to mapping artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildTraversalWitnessMappingArtifact(
			&Artifact,
			MappingArtifact,
			FailureReason));
	TestTrue(TEXT("Traversal mapping marked explicit"), MappingArtifact.bHasExplicitTraversalWitness);
	TestEqual(TEXT("Traversal mapping id count"), MappingArtifact.SelectedTraversalCapabilityIds.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerBuildsSelectedTraversalFromChildHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.BuildsSelectedTraversalFromChildHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerBuildsSelectedTraversalFromChildHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.bHasCertifiedSelectedTraversalCapabilityArtifact = true;
	ChildHandoff.CertifiedSelectedTraversalCapabilityIds = { TEXT("Traversal.Capability.Handoff.0") };

	FLayoutProducedSelectedTraversalArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Handoff-carried authoritative traversal payload builds produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifactFromChildHandoff(
			TEXT("Artifact.Traversal.Handoff.0"),
			TEXT("ChildScout.0"),
			ChildHandoff,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Handoff traversal produced artifact marked present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Handoff traversal id preserved"), Artifact.SelectedTraversalCapabilityIds[0], FLayoutId(TEXT("Traversal.Capability.Handoff.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsUnflaggedTraversalHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsUnflaggedTraversalHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsUnflaggedTraversalHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.CertifiedSelectedTraversalCapabilityIds = { TEXT("Traversal.Capability.Handoff.0") };

	FLayoutProducedSelectedTraversalArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Unflagged handoff traversal payload fails closed"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifactFromChildHandoff(
			TEXT("Artifact.Traversal.Handoff.0"),
			TEXT("ChildScout.0"),
			ChildHandoff,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Unflagged handoff traversal failure reported"), FailureReason.Contains(TEXT("authoritative traversal")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerBuildsSelectedTraversalFromChildRequestTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.BuildsSelectedTraversalFromChildRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerBuildsSelectedTraversalFromChildRequestTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest ChildRequest;
	ChildRequest.bHasCertifiedSelectedTraversalCapabilityArtifact = true;
	ChildRequest.CertifiedSelectedTraversalCapabilityIds = { TEXT("Traversal.Capability.0") };

	FLayoutProducedSelectedTraversalArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Request-carried authoritative traversal payload builds produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifactFromChildRequest(
			TEXT("Artifact.Traversal.Request.0"),
			TEXT("ChildScout.0"),
			ChildRequest,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Request traversal produced artifact marked present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Request traversal id preserved"), Artifact.SelectedTraversalCapabilityIds[0], FLayoutId(TEXT("Traversal.Capability.0")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsUnflaggedTraversalRequestTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsUnflaggedTraversalRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsUnflaggedTraversalRequestTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest ChildRequest;
	ChildRequest.CertifiedSelectedTraversalCapabilityIds = { TEXT("Traversal.Capability.0") };

	FLayoutProducedSelectedTraversalArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Unflagged traversal payload fails closed"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifactFromChildRequest(
			TEXT("Artifact.Traversal.Request.0"),
			TEXT("ChildScout.0"),
			ChildRequest,
			Artifact,
			FailureReason));
	TestTrue(TEXT("Unflagged traversal failure reported"), FailureReason.Contains(TEXT("authoritative traversal")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsDuplicateTraversalTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsDuplicateTraversal",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsDuplicateTraversalTest::RunTest(const FString& Parameters)
{
	FLayoutProducedSelectedTraversalArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Duplicate selected traversal ids fail closed"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
			TEXT("Artifact.Traversal.0"),
			TEXT("ChildScout.0"),
			{ TEXT("Traversal.Capability.0"), TEXT("Traversal.Capability.0") },
			Artifact,
			FailureReason));
	TestTrue(TEXT("Duplicate traversal failure reported"), FailureReason.Contains(TEXT("duplicates")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerBuildsRequirementToAssertionTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.BuildsRequirementToAssertion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerBuildsRequirementToAssertionTest::RunTest(const FString& Parameters)
{
	FLayoutProducedRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.0");

	FLayoutProducedRequirementToAssertionArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit requirement-to-assertion ids build produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifact(
			TEXT("Artifact.RequirementAssertion.0"),
			TEXT("ChildScout.0"),
			{ Mapping },
			BuildParentAssertions(),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Requirement-to-assertion produced artifact marked present"), Artifact.bHasProducedArtifact);

	FLayoutChildParentAssertionSubsetMappingArtifact MappingArtifact;
	TestTrue(
		TEXT("Produced requirement-to-assertion artifact converts to assertion subset mapping"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentAssertionSubsetMappingArtifact(
			&Artifact,
			MappingArtifact,
			FailureReason));
	TestTrue(TEXT("Assertion subset mapping marked explicit"), MappingArtifact.bHasExplicitMapping);
	TestEqual(TEXT("Assertion subset id count"), MappingArtifact.ParentAssertionSubsetIds.Num(), 1);
	TestEqual(TEXT("Assertion subset id preserved"), MappingArtifact.ParentAssertionSubsetIds[0], Mapping.AssertionId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerBuildsRequirementToAssertionFromChildHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.BuildsRequirementToAssertionFromChildHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerBuildsRequirementToAssertionFromChildHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutCertifiedChildRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.Handoff.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.0");

	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.bHasCertifiedRequirementToAssertionArtifact = true;
	ChildHandoff.CertifiedRequirementToAssertionMappings = { Mapping };

	FLayoutProducedRequirementToAssertionArtifact Artifact;
	FString FailureReason;
	TestTrue(
		TEXT("Handoff-carried authoritative requirement-to-assertion payload builds produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifactFromChildHandoff(
			TEXT("Artifact.RequirementAssertion.Handoff.0"),
			TEXT("ChildScout.0"),
			ChildHandoff,
			BuildParentAssertions(),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Handoff requirement-to-assertion produced artifact marked present"), Artifact.bHasProducedArtifact);
	TestEqual(TEXT("Handoff requirement id preserved"), Artifact.Mappings[0].RequirementId, Mapping.RequirementId);
	TestEqual(TEXT("Handoff assertion id preserved"), Artifact.Mappings[0].AssertionId, Mapping.AssertionId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsUnflaggedRequirementToAssertionHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsUnflaggedRequirementToAssertionHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsUnflaggedRequirementToAssertionHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutCertifiedChildRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.Handoff.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.0");

	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.CertifiedRequirementToAssertionMappings = { Mapping };

	FLayoutProducedRequirementToAssertionArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Unflagged handoff requirement-to-assertion payload fails closed"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifactFromChildHandoff(
			TEXT("Artifact.RequirementAssertion.Handoff.0"),
			TEXT("ChildScout.0"),
			ChildHandoff,
			BuildParentAssertions(),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Unflagged handoff requirement mapping failure reported"), FailureReason.Contains(TEXT("authoritative requirement-to-assertion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsFailedAssertionTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsFailedAssertion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsFailedAssertionTest::RunTest(const FString& Parameters)
{
	FLayoutProducedRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.Failed");

	FLayoutProducedRequirementToAssertionArtifact Artifact;
	FString FailureReason;
	TestFalse(
		TEXT("Failed parent assertion ids fail closed"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifact(
			TEXT("Artifact.RequirementAssertion.0"),
			TEXT("ChildScout.0"),
			{ Mapping },
			BuildParentAssertions(),
			Artifact,
			FailureReason));
	TestTrue(TEXT("Failed assertion failure reported"), FailureReason.Contains(TEXT("failed assertion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRequiresProducedArtifactsTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RequiresProducedArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRequiresProducedArtifactsTest::RunTest(const FString& Parameters)
{
	FString FailureReason;
	FLayoutChildTraversalWitnessMappingArtifact TraversalMapping;
	TestFalse(
		TEXT("Traversal mapping requires produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildTraversalWitnessMappingArtifact(
			nullptr,
			TraversalMapping,
			FailureReason));
	TestTrue(TEXT("Traversal produced artifact requirement reported"), FailureReason.Contains(TEXT("produced")));

	FLayoutChildParentAssertionSubsetMappingArtifact AssertionMapping;
	TestFalse(
		TEXT("Assertion subset mapping requires produced artifact"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentAssertionSubsetMappingArtifact(
			nullptr,
			AssertionMapping,
			FailureReason));
	TestTrue(TEXT("Requirement-to-assertion produced artifact requirement reported"), FailureReason.Contains(TEXT("produced")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerBuildsParentBranchCertificateTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.BuildsParentBranchCertificate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerBuildsParentBranchCertificateTest::RunTest(const FString& Parameters)
{
	FLayoutProducedSelectedTraversalArtifact TraversalArtifact;
	FLayoutProducedRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.0");
	FLayoutProducedRequirementToAssertionArtifact RequirementArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Traversal artifact builds for branch certificate"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
			TEXT("Artifact.Traversal.0"),
			TEXT("ChildScout.0"),
			{ TEXT("Traversal.Capability.0") },
			TraversalArtifact,
			FailureReason));
	TestTrue(
		TEXT("Requirement artifact builds for branch certificate"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifact(
			TEXT("Artifact.RequirementAssertion.0"),
			TEXT("ChildScout.0"),
			{ Mapping },
			BuildParentAssertions(),
			RequirementArtifact,
			FailureReason));

	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.0");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	Input.SelectedClosureSpanCapabilityId = TEXT("ClosureSpan.Capability.0");
	Input.SelectedEndpointCapabilityIds = { TEXT("Endpoint.Capability.0") };
	Input.SelectedSeamCapabilityIds = { TEXT("Seam.Capability.0") };
	Input.SelectedSeamWitnessIds = { TEXT("Seam.Witness.0") };
	Input.SelectedVerticalCapabilityIds = { TEXT("Vertical.Capability.0") };
	Input.DelegatedFeatureRequirementIds = { TEXT("Feature.Requirement.0") };
	Input.DelegatedClosureRequirementIds = { TEXT("Closure.Requirement.0") };
	Input.DelegatedSeamWitnessIds = { TEXT("Delegated.Seam.0") };
	Input.DelegatedHostVerticalAccessWitnessIds = { TEXT("Delegated.HostVA.0") };
	Input.ReferencedProofAssertionIds = { TEXT("Assertion.Parent.0") };

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	TestTrue(
		TEXT("Explicit parent branch certificate builds from produced witness artifacts"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			&TraversalArtifact,
			&RequirementArtifact,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Parent branch certificate marked produced"), Certificate.bHasProducedArtifact);
	TestEqual(TEXT("Traversal ids carried"), Certificate.SelectedTraversalCapabilityIds.Num(), 1);
	TestEqual(TEXT("Requirement mappings carried"), Certificate.RequirementToAssertionMappings.Num(), 1);
	TestEqual(TEXT("Delegated feature requirement carried"), Certificate.DelegatedFeatureRequirementIds[0], FLayoutId(TEXT("Feature.Requirement.0")));
	TestEqual(TEXT("Delegated closure requirement carried"), Certificate.DelegatedClosureRequirementIds[0], FLayoutId(TEXT("Closure.Requirement.0")));
	TestEqual(TEXT("Delegated seam witness carried"), Certificate.DelegatedSeamWitnessIds[0], FLayoutId(TEXT("Delegated.Seam.0")));
	TestEqual(TEXT("Delegated host VA witness carried"), Certificate.DelegatedHostVerticalAccessWitnessIds[0], FLayoutId(TEXT("Delegated.HostVA.0")));
	TestTrue(TEXT("Certificate hash populated"), Certificate.CertificateInputHash != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerAppliesParentBranchCertificateToHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.AppliesParentBranchCertificateToHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerAppliesParentBranchCertificateToHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutProducedSelectedTraversalArtifact TraversalArtifact;
	FLayoutProducedRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.0");
	FLayoutProducedRequirementToAssertionArtifact RequirementArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Traversal artifact builds for handoff certificate application"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
			TEXT("Artifact.Traversal.0"),
			TEXT("ChildScout.0"),
			{ TEXT("Traversal.Capability.0") },
			TraversalArtifact,
			FailureReason));
	TestTrue(
		TEXT("Requirement artifact builds for handoff certificate application"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifact(
			TEXT("Artifact.RequirementAssertion.0"),
			TEXT("ChildScout.0"),
			{ Mapping },
			BuildParentAssertions(),
			RequirementArtifact,
			FailureReason));

	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.0");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	Input.SelectedClosureSpanCapabilityId = TEXT("ClosureSpan.Capability.0");
	Input.SelectedEndpointCapabilityIds = { TEXT("Endpoint.Capability.0") };
	Input.SelectedSeamCapabilityIds = { TEXT("Seam.Capability.0") };
	Input.SelectedSeamWitnessIds = { TEXT("Seam.Witness.0") };
	Input.SelectedVerticalCapabilityIds = { TEXT("Vertical.Capability.0") };
	Input.DelegatedFeatureRequirementIds = { TEXT("Feature.Requirement.0") };
	Input.DelegatedClosureRequirementIds = { TEXT("Closure.Requirement.0") };
	Input.DelegatedSeamWitnessIds = { TEXT("Delegated.Seam.0") };
	Input.DelegatedHostVerticalAccessWitnessIds = { TEXT("Delegated.HostVA.0") };
	Input.ReferencedProofAssertionIds = { TEXT("Assertion.Parent.0") };

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	TestTrue(
		TEXT("Parent branch certificate builds for handoff application"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			&TraversalArtifact,
			&RequirementArtifact,
			Certificate,
			FailureReason));

	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.StableChildKey = TEXT("StableChild.0");
	ChildHandoff.ContentMetadata.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	TestTrue(
		TEXT("Produced parent branch certificate applies to matching child handoff"),
		LayoutCertifiedChildWitnessArtifactProducer::TryApplyParentBranchCertificateToChildHandoff(
			Certificate,
			ChildHandoff,
			FailureReason));
	TestTrue(TEXT("Traversal artifact flag set on handoff"), ChildHandoff.bHasCertifiedSelectedTraversalCapabilityArtifact);
	TestTrue(TEXT("Requirement artifact flag set on handoff"), ChildHandoff.bHasCertifiedRequirementToAssertionArtifact);
	TestTrue(TEXT("Parent assertion subset flag set on handoff"), ChildHandoff.bHasCertifiedParentAssertionSubsetArtifact);
	TestEqual(TEXT("Traversal id copied"), ChildHandoff.CertifiedSelectedTraversalCapabilityIds[0], FLayoutId(TEXT("Traversal.Capability.0")));
	TestEqual(TEXT("Requirement mapping copied"), ChildHandoff.CertifiedRequirementToAssertionMappings[0].RequirementId, FName(TEXT("Requirement.Parent.0")));
	TestEqual(TEXT("Delegated feature requirement copied"), ChildHandoff.DelegatedZoneFeatureRequirementIds[0], FLayoutId(TEXT("Feature.Requirement.0")));
	TestEqual(TEXT("Delegated closure requirement copied"), ChildHandoff.DelegatedClosureRequirementIds[0], FLayoutId(TEXT("Closure.Requirement.0")));
	TestEqual(TEXT("Witness branch copied"), ChildHandoff.CertifiedWitnessBundle.BranchId, FLayoutId(TEXT("Branch.0")));
	TestEqual(TEXT("Witness closure-span capability copied"), ChildHandoff.CertifiedWitnessBundle.SelectedClosureSpanCapabilityId, FLayoutId(TEXT("ClosureSpan.Capability.0")));
	TestEqual(TEXT("Witness seam capability copied"), ChildHandoff.CertifiedWitnessBundle.SelectedSeamCapabilityId, FLayoutId(TEXT("Seam.Capability.0")));
	TestEqual(TEXT("Witness seam witness copied"), ChildHandoff.CertifiedWitnessBundle.SelectedSeamWitnessId, FLayoutId(TEXT("Seam.Witness.0")));
	TestEqual(TEXT("Delegated seam witness copied"), ChildHandoff.CertifiedWitnessBundle.DelegatedSeamWitnessIds[0], FLayoutId(TEXT("Delegated.Seam.0")));
	TestEqual(TEXT("Delegated host VA witness copied"), ChildHandoff.CertifiedWitnessBundle.DelegatedHostVerticalAccessWitnessIds[0], FLayoutId(TEXT("Delegated.HostVA.0")));
	TestEqual(TEXT("Witness parent assertion subset copied"), ChildHandoff.CertifiedWitnessBundle.ParentAssertionIds[0], FLayoutId(TEXT("Assertion.Parent.0")));
	TestEqual(TEXT("Certificate id refreshed from parent branch artifact"), ChildHandoff.ProofCertificate.CertificateId, FLayoutId(TEXT("Artifact.ParentBranch.0")));
	TestTrue(TEXT("Certificate input hash refreshed"), ChildHandoff.ProofCertificate.InputHash != 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsUnreferencedParentBranchAssertionMappingTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsUnreferencedParentBranchAssertionMapping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsUnreferencedParentBranchAssertionMappingTest::RunTest(const FString& Parameters)
{
	FLayoutProducedRequirementToAssertionMapping Mapping;
	Mapping.RequirementId = TEXT("Requirement.Parent.0");
	Mapping.AssertionId = TEXT("Assertion.Parent.0");
	FLayoutProducedRequirementToAssertionArtifact RequirementArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Requirement artifact builds for unreferenced assertion guard"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildRequirementToAssertionArtifact(
			TEXT("Artifact.RequirementAssertion.0"),
			TEXT("ChildScout.0"),
			{ Mapping },
			BuildParentAssertions(),
			RequirementArtifact,
			FailureReason));

	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.UnreferencedAssertion");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	TestFalse(
		TEXT("Parent branch certificate rejects requirement mapping before handoff when proof assertion is not referenced"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			&RequirementArtifact,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Unreferenced assertion mapping failure is reported"), FailureReason.Contains(TEXT("referenced proof assertion")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsMismatchedParentBranchCertificateHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsMismatchedParentBranchCertificateHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsMismatchedParentBranchCertificateHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.0");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestTrue(
		TEXT("Parent branch certificate builds without optional witness artifacts"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));

	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.StableChildKey = TEXT("DifferentStableChild.0");
	ChildHandoff.ContentMetadata.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	TestFalse(
		TEXT("Parent branch certificate rejects mismatched child handoff"),
		LayoutCertifiedChildWitnessArtifactProducer::TryApplyParentBranchCertificateToChildHandoff(
			Certificate,
			ChildHandoff,
			FailureReason));
	TestTrue(TEXT("Mismatched child handoff failure reported"), FailureReason.Contains(TEXT("different stable child")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsMismatchedParentBranchCertificateChildScoutTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsMismatchedParentBranchCertificateChildScout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsMismatchedParentBranchCertificateChildScoutTest::RunTest(const FString& Parameters)
{
	FLayoutProducedSelectedTraversalArtifact TraversalArtifact;
	FString FailureReason;
	TestTrue(
		TEXT("Traversal artifact builds for mismatched branch certificate test"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildSelectedTraversalArtifact(
			TEXT("Artifact.Traversal.0"),
			TEXT("OtherChildScout.0"),
			{ TEXT("Traversal.Capability.0") },
			TraversalArtifact,
			FailureReason));

	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.0");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	TestFalse(
		TEXT("Parent branch certificate rejects traversal artifacts for another child scout result"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			&TraversalArtifact,
			nullptr,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Mismatched child scout failure reported"), FailureReason.Contains(TEXT("different child scout")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsMultiSeamParentBranchCertificateHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsMultiSeamParentBranchCertificateHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsMultiSeamParentBranchCertificateHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.MultiSeam");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	Input.SelectedSeamCapabilityIds = { TEXT("Seam.Capability.0"), TEXT("Seam.Capability.1") };

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestFalse(
		TEXT("Parent branch certificate rejects multi-seam capability ids without matching witness ids"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Unmatched multi-seam failure is reported"), FailureReason.Contains(TEXT("unmatched selected seam capability and witness ids")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsUnmatchedSeamWitnessParentBranchCertificateTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsUnmatchedSeamWitnessParentBranchCertificate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsUnmatchedSeamWitnessParentBranchCertificateTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.UnmatchedSeamWitness");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	Input.SelectedSeamCapabilityIds = { TEXT("Seam.Capability.0") };

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestFalse(
		TEXT("Parent branch certificate rejects selected seam capability without matching witness id"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Unmatched seam witness failure is reported"), FailureReason.Contains(TEXT("unmatched selected seam capability and witness ids")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsMultiSeamWitnessParentBranchCertificateHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsMultiSeamWitnessParentBranchCertificateHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsMultiSeamWitnessParentBranchCertificateHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.MultiSeamWitness");
	Input.ParentContractId = TEXT("ParentContract.0");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.0");
	Input.ChildScoutResultId = TEXT("ChildScout.0");
	Input.StableChildId = TEXT("StableChild.0");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.0");
	Input.SelectedSeamWitnessIds = { TEXT("Seam.Witness.0"), TEXT("Seam.Witness.1") };

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestFalse(
		TEXT("Parent branch certificate rejects multi-seam witness ids without matching capability ids"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Unmatched multi-seam witness failure is reported"), FailureReason.Contains(TEXT("unmatched selected seam capability and witness ids")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsStaleParentBranchCertificateHashAtHandoffTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsStaleParentBranchCertificateHashAtHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsStaleParentBranchCertificateHashAtHandoffTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.Hash");
	Input.ParentContractId = TEXT("ParentContract.Hash");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.Hash");
	Input.ChildScoutResultId = TEXT("ChildScout.Hash");
	Input.StableChildId = TEXT("StableChild.Hash");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.Hash");
	Input.SelectedEndpointCapabilityIds = { TEXT("Endpoint.Capability.0") };

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestTrue(
		TEXT("Parent branch certificate builds before stale mutation"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));

	Certificate.SelectedClosureSpanCapabilityId = TEXT("ClosureSpan.Capability.Stale");
	FLayoutChildSolveHandoff ChildHandoff;
	ChildHandoff.StableChildKey = TEXT("StableChild.Hash");
	ChildHandoff.ContentMetadata.SourceContentEntryId = TEXT("ContentEntry.Child.Hash");
	TestFalse(
		TEXT("Child handoff rejects certificates mutated after selected-field hash capture"),
		LayoutCertifiedChildWitnessArtifactProducer::TryApplyParentBranchCertificateToChildHandoff(
			Certificate,
			ChildHandoff,
			FailureReason));
	TestTrue(TEXT("Stale hash failure is reported"), FailureReason.Contains(TEXT("stale selected-field hash")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsParentBranchCertificateWhenHandoffIdentityMissingTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsParentBranchCertificateWhenHandoffIdentityMissing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsParentBranchCertificateWhenHandoffIdentityMissingTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.Identity");
	Input.ParentContractId = TEXT("ParentContract.Identity");
	Input.ParentContractHash = 12345;
	Input.BranchId = TEXT("Branch.Identity");
	Input.ChildScoutResultId = TEXT("ChildScout.Identity");
	Input.StableChildId = TEXT("StableChild.Identity");
	Input.SourceContentEntryId = TEXT("ContentEntry.Child.Identity");

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestTrue(
		TEXT("Parent branch certificate builds for identity guard test"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));

	FLayoutChildSolveHandoff MissingStableChildHandoff;
	MissingStableChildHandoff.ContentMetadata.SourceContentEntryId = TEXT("ContentEntry.Child.Identity");
	TestFalse(
		TEXT("Child handoff rejects certificate when stable child identity is absent"),
		LayoutCertifiedChildWitnessArtifactProducer::TryApplyParentBranchCertificateToChildHandoff(
			Certificate,
			MissingStableChildHandoff,
			FailureReason));
	TestTrue(TEXT("Missing stable child identity failure is reported"), FailureReason.Contains(TEXT("missing stable child identity")));

	FLayoutChildSolveHandoff MissingSourceEntryHandoff;
	MissingSourceEntryHandoff.StableChildKey = TEXT("StableChild.Identity");
	TestFalse(
		TEXT("Child handoff rejects certificate when source content-entry identity is absent"),
		LayoutCertifiedChildWitnessArtifactProducer::TryApplyParentBranchCertificateToChildHandoff(
			Certificate,
			MissingSourceEntryHandoff,
			FailureReason));
	TestTrue(TEXT("Missing source content-entry identity failure is reported"), FailureReason.Contains(TEXT("missing source content-entry identity")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerRejectsNullTraversalWhenIdsSelectedTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.RejectsNullTraversalWhenIdsSelected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerRejectsNullTraversalWhenIdsSelectedTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.NullTraversal");
	Input.ParentContractId = TEXT("ParentContract.NullTraversal");
	Input.ParentContractHash = 99999;
	Input.BranchId = TEXT("Branch.NullTraversal");
	Input.ChildScoutResultId = TEXT("ChildScout.NullTraversal");
	Input.StableChildId = TEXT("StableChild.NullTraversal");
	Input.SourceContentEntryId = TEXT("ContentEntry.NullTraversal");
	Input.bHasSelectedTraversalIds = true;

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestFalse(
		TEXT("Parent branch certificate rejects nullptr traversal artifact when ids were selected"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));
	TestTrue(
		TEXT("Rejection reason reports selected traversal ids require an explicit artifact"),
		FailureReason.Contains(TEXT("require")) && FailureReason.Contains(TEXT("traversal")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCertifiedChildWitnessArtifactProducerAllowsNullTraversalWhenNoIdsSelectedTest,
	"PorismExtension.Layout.Async.ChildWitnessArtifactProducer.AllowsNullTraversalWhenNoIdsSelected",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCertifiedChildWitnessArtifactProducerAllowsNullTraversalWhenNoIdsSelectedTest::RunTest(const FString& Parameters)
{
	FLayoutParentBranchCertificateInput Input;
	Input.ArtifactId = TEXT("Artifact.ParentBranch.NoTraversal");
	Input.ParentContractId = TEXT("ParentContract.NoTraversal");
	Input.ParentContractHash = 88888;
	Input.BranchId = TEXT("Branch.NoTraversal");
	Input.ChildScoutResultId = TEXT("ChildScout.NoTraversal");
	Input.StableChildId = TEXT("StableChild.NoTraversal");
	Input.SourceContentEntryId = TEXT("ContentEntry.NoTraversal");
	// bHasSelectedTraversalIds stays false (default)

	FLayoutProducedParentBranchCertificateArtifact Certificate;
	FString FailureReason;
	TestTrue(
		TEXT("Parent branch certificate still builds when traversal ids were not selected"),
		LayoutCertifiedChildWitnessArtifactProducer::TryBuildParentBranchCertificateArtifact(
			Input,
			nullptr,
			nullptr,
			Certificate,
			FailureReason));
	TestTrue(TEXT("Certificate marked produced without traversal"), Certificate.bHasProducedArtifact);
	TestTrue(TEXT("No traversal ids carried when none were selected"), Certificate.SelectedTraversalCapabilityIds.IsEmpty());
	return true;
}
