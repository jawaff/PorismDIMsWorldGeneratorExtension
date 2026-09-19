// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOfferValidationRejectsEntryModuleWithoutDerivedEndpointOfferTest,
	"PorismExtension.Layout.Module.Validation.RejectsEntryModuleWithoutDerivedEndpointOffer",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOfferValidationRejectsEntryModuleWithoutDerivedEndpointOfferTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_InvalidDerivedOffer"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_InvalidDerivedOffer"),
		Template,
		{ELayoutCellIntent::Entry},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceEntry, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestFalse(TEXT("Snapshot validation fails when no endpoint offer can be derived"), Snapshot.Validation.IsValid());
	TestTrue(TEXT("Validation assertions include failed derived endpoint contract"), Snapshot.ValidationAssertions.ContainsByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionKind == ELayoutValidationAssertionKind::DerivedOfferContractValid
			&& !Assertion.bPassed;
	}));
	TestTrue(TEXT("Validation reports the missing derived endpoint offer"), Snapshot.Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("does not derive any endpoint offers"));
	}));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutOfferValidationRejectsVerticalAccessModuleWithoutDerivedVerticalAccessContractTest,
	"PorismExtension.Layout.Module.Validation.RejectsVerticalAccessModuleWithoutDerivedVerticalAccessContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutOfferValidationRejectsVerticalAccessModuleWithoutDerivedVerticalAccessContractTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_InvalidDerivedVerticalAccess"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_InvalidDerivedVerticalAccess"),
		Template,
		{ELayoutCellIntent::VerticalAccess},
		{
			MakeConnectionFaceRule(ELayoutFaceDirection::PosX, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegY, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor),
			MakeConnectionFaceRule(ELayoutFaceDirection::PosZ, LayoutGameplayTags::FaceOpen, MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor, MakeTags({LayoutGameplayTags::TraversalSecondary})),
			MakeConnectionFaceRule(ELayoutFaceDirection::NegZ, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
		});

	const FLayoutModuleSolveSnapshot Snapshot = FLayoutProfileSolver::BuildModuleSnapshot(Module, 1);
	TestFalse(TEXT("Snapshot validation fails when no vertical-access contract can be derived"), Snapshot.Validation.IsValid());
	TestTrue(TEXT("Validation assertions include failed derived vertical-access contract"), Snapshot.ValidationAssertions.ContainsByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionKind == ELayoutValidationAssertionKind::DerivedVerticalAccessContractValid
			&& !Assertion.bPassed;
	}));
	TestTrue(TEXT("Validation reports the missing derived vertical-access contract"), Snapshot.Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("does not derive any vertical-access contracts"));
	}));

	return true;
}
