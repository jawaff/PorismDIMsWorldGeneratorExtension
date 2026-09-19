// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutWorldBindingAsset.h"

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;

namespace
{
	ULayoutCompositeModuleAsset* CreateValidationCompositeModule(
		UObject* Outer,
		const TCHAR* Name,
		const TArray<FLayoutCompositeModuleCell>& Cells)
	{
		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, FName(Name));
		Composite->Cells = Cells;
		return Composite;
	}

	FLayoutCompositeModuleCell MakeValidationCompositeCell(
		ULayoutModuleAsset* Module,
		const FIntVector LocalCell,
		const int32 RelativeYawRotationSteps = 0)
	{
		FLayoutCompositeModuleCell Cell;
		Cell.Module = Module;
		Cell.LocalCell = LocalCell;
		Cell.RelativeYawRotationSteps = RelativeYawRotationSteps;
		return Cell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationAllowsChildOnlyCandidateWithoutCompatibilitySharedCellSizeTest,
	"PorismExtension.Layout.WorldBinding.Validation.AllowsChildOnlyCandidateWithoutCompatibilitySharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsOutOfRangeBaseCellDimensionsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsOutOfRangeBaseCellDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsInvalidOccupancyTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsInvalidOccupancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsNonPositiveOrdinaryRootSolveBudgetTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsNonPositiveOrdinaryRootSolveBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsRampTransitionWithoutFoundationDepthBudgetTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsRampTransitionWithoutFoundationDepthBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsNonPositiveContinuationFamilySolveBudgetTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsNonPositiveContinuationFamilySolveBudget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsCandidateLeafModuleTemplateThatViolatesBindingCellSizeTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsCandidateLeafModuleTemplateThatViolatesBindingCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsCandidateCompositeModuleThatViolatesBindingCellSizeTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsCandidateCompositeModuleThatViolatesBindingCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsCandidateLeafModuleTemplateThatResolvesZeroDimensionsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsCandidateLeafModuleTemplateThatResolvesZeroDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationAllowsChildOnlyCandidateWithoutCompatibilitySharedCellSizeTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* ChildProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationChildOnlyChild"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ChildProfile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationChildOnlyChild"),
		{});

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationChildOnly"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("ChildRegion");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationChildOnly"),
		{ChildEntry});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationChildOnly"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
		TestTrue(TEXT("World binding accepts child-only candidates without relying on one compatibility shared cell size field when the binding already owns the lattice"), Validation.IsValid());
	TestTrue(
		TEXT("World binding no longer reports a candidate shared cell size mismatch for child-only content"),
		!Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("violates the binding-owned shared cell size contract"))
				|| Message.Message.Contains(TEXT("could not resolve a usable shared cell size"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsOutOfRangeBaseCellDimensionsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* ChildProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationOutOfRangeChild"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ChildProfile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationOutOfRangeChild"),
		{});

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationOutOfRange"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	FLayoutRegionContentEntry ChildEntry;
	ChildEntry.EntryId = TEXT("ChildRegion");
	ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	ChildEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationOutOfRange"),
		{ChildEntry});

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationOutOfRange"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(256, 16, 16);

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects shared cell dimensions outside compact local-block range"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports compact local-block range"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("[1, 255]"))
				&& Message.Message.Contains(TEXT("compact uint8 local-block evidence"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsInvalidOccupancyTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationOccupancy"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationOccupancy"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationOccupancy"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;
	WorldBinding->OccupancyProbability = -1.0f;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects an invalid occupancy probability"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the invalid occupancy probability"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("OccupancyProbability"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsNonPositiveOrdinaryRootSolveBudgetTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationOrdinarySolveBudget"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationOrdinarySolveBudget"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationOrdinarySolveBudget"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = -0.25f;

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects a non-positive authored ordinary-root solve budget"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the non-positive authored ordinary-root solve budget"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("ordinary-root SolveBudget.MaxSolveDurationSeconds=-0.250"))
				&& Message.Message.Contains(TEXT("requires a positive root-attempt timeout"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsRampTransitionWithoutFoundationDepthBudgetTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationRampTransitionLimit"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationRampTransitionLimit"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationRampTransitionLimit"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.MaxFoundationDepth = 0;

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects ramp transitions without a foundation-depth budget"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the missing foundation-depth budget"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("MaxFoundationDepth=0"))
				&& Message.Message.Contains(TEXT("perimeter ramp transitions reuse the same depth budget"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsNonPositiveContinuationFamilySolveBudgetTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationSolveBudget"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationSolveBudget"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationContinuationSolveBudget"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorRoad});

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.SolveBudget.MaxSolveDurationSeconds = 0.0f;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("BridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects a non-positive authored continuation-family solve budget"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the non-positive authored continuation-family solve budget"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("continuation family BridgeFamily authors SolveBudget.MaxSolveDurationSeconds=0.000"))
				&& Message.Message.Contains(TEXT("requires a positive family-owned root-attempt timeout"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsCandidateLeafModuleTemplateThatViolatesBindingCellSizeTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutWorldBindingLeafCellSizeTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("LayoutWorldBindingLeafCellSizeModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("LeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutWorldBindingLeafCellSizeContentSet"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutWorldBindingLeafCellSizeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBindingLeafCellSize"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(8, 8, 8);

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects leaf-module candidates whose template dimensions violate the binding cell lattice"), Validation.IsValid());
	TestTrue(TEXT("World binding reports the leaf-module template-dimension mismatch"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("template dimensions violate the binding-owned cell lattice"));
	}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsCandidateCompositeModuleThatViolatesBindingCellSizeTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutWorldBindingCompositeCellSizeTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("LayoutWorldBindingCompositeCellSizeLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));
	ULayoutCompositeModuleAsset* CompositeModule = CreateValidationCompositeModule(
		Outer,
		TEXT("LayoutWorldBindingCompositeCellSizeComposite"),
		{
			MakeValidationCompositeCell(LeafModule, FIntVector(0, 0, 0))
		});

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = CompositeModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutWorldBindingCompositeCellSizeContentSet"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutWorldBindingCompositeCellSizeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBindingCompositeCellSize"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(8, 8, 8);

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects composite-backed candidates whose leaf templates violate the binding cell lattice"), Validation.IsValid());
	TestTrue(TEXT("World binding surfaces the composite shared-cell-size contract failure"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("violates the established shared cell size contract"));
	}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsCandidateLeafModuleTemplateThatResolvesZeroDimensionsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutWorldBindingZeroTemplateSizeTemplate"), FIntVector::ZeroValue);
	ULayoutModuleAsset* LeafModule = CreateModule(
		Outer,
		TEXT("LayoutWorldBindingZeroTemplateSizeLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("LeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = LeafModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutWorldBindingZeroTemplateSizeContentSet"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutWorldBindingZeroTemplateSizeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBindingZeroTemplateSize"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(8, 8, 8);

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects candidates whose leaf module resolves non-positive template dimensions"), Validation.IsValid());
	TestTrue(TEXT("World binding reports that the validator could not resolve a usable template size from the real template/module assets"), Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Message.Contains(TEXT("could not resolve a positive template SizeInBlocks value"))
			|| Message.Message.Contains(TEXT("could not resolve a positive shared cell size"))
			|| Message.Message.Contains(TEXT("could not read a usable size from the real template asset"))
			|| Message.Message.Contains(TEXT("could not read a usable size from the real referenced template/module assets"))
			|| Message.Message.Contains(TEXT("0,0,0"));
	}) || !Validation.Messages.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRequiresCandidateContentSourceTest,
	"PorismExtension.Layout.WorldBinding.Validation.RequiresCandidateContentSource",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsContinuationCandidateWithoutExplicitEntryLevelTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsContinuationCandidateWithoutExplicitEntryLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationAcceptsHigherContinuationEntryLevelOnActivePathTest,
	"PorismExtension.Layout.WorldBinding.Validation.AcceptsHigherContinuationEntryLevelOnActivePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsContinuationCandidateWithoutReachabilityTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsContinuationCandidateWithoutReachability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRequiresCandidateContentSourceTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContentSource"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationContentSource"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};

	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects candidates whose selected profile does not own a content set"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the missing profile-owned content set"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("does not own a content set"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRejectsContinuationCandidateWithoutExplicitEntryLevelTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationEntryLevel"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationEntryLevel"),
		{});
	Profile->ContentSet = ContentSet;
	Profile->bRequireAllTraversalChannelsReachable = true;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationContinuationEntryLevel"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationEntryLevelRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationEntryLevelRoot"),
		{});
	RootCandidate.LayoutProfile->ContentSet = RootContentSet;
	RootCandidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfaceRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects continuation candidates that do not declare an explicit continuation entry level"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the missing explicit continuation entry level"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("does not author an explicit continuation-entry level"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationAcceptsHigherContinuationEntryLevelOnActivePathTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationHigherContinuationEntryLevel"),
		FIntVector(1, 1, 1));
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationHigherContinuationEntryLevel"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);
	Profile->ContinuationEntryLevel = 1;
	Profile->bRequireAllTraversalChannelsReachable = true;

	ULayoutModuleAsset* EntryModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationHigherContinuationEntryLevel"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		{},
		FGameplayTagContainer(),
		{},
		FGameplayTagContainer());

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("RoadEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = EntryModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationHigherContinuationEntryLevel"),
		{ModuleEntry});
	Profile->ContentSet = ContentSet;

	ULayoutProfileAsset* RootProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationHigherContinuationEntryLevelRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationHigherContinuationEntryLevelRoot"),
		{});
	RootProfile->ContentSet = RootContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationHigherContinuationEntryLevel"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = RootProfile;
	RootCandidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeRoute");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("BridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestTrue(TEXT("World binding now accepts higher continuation entry levels on the active continuation structural path"), Validation.IsValid());
	TestTrue(
		TEXT("World binding no longer reports the old active-path higher-continuation-entry fail-fast"),
		Validation.Messages.FilterByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("still only supports local ground-band entry level 0"));
		}).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRequiresPositiveContinuationPlanningLimitsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RequiresPositiveContinuationPlanningLimits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRejectsContinuationCandidateWithoutReachabilityTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationContinuationReachability"),
		FIntVector(1, 1, 1));
	ULayoutModuleAsset* EntryModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationContinuationReachability"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		{},
		FGameplayTagContainer(),
		{},
		FGameplayTagContainer());

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("RoadEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = EntryModule;

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationReachability"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		2,
		0,
		false);
	Profile->ContinuationEntryLevel = 1;
	Profile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationReachability"),
		{ModuleEntry});

	ULayoutProfileAsset* RootProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationReachabilityRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	RootProfile->ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationReachabilityRoot"),
		{});

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationContinuationReachability"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = RootProfile;
	RootCandidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfaceRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects continuation profiles without required traversal reachability"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports missing continuation traversal reachability"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("does not require all traversal channels to be reachable"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRequiresPositiveContinuationPlanningLimitsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationLimits"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationLimits"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationContinuationLimits"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorBridge});

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfaceRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.MaxConnectionsPerSite = 0;
	Family.MaxConnectionDistanceInCells = 0;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects continuation families with non-positive planning limits"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the non-positive connection-count limit"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("MaxConnectionsPerSite"));
		}));
	TestTrue(
		TEXT("World binding reports the non-positive connection-distance limit"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("MaxConnectionDistanceInCells"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsMismatchedContinuationCandidateCellSizeTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsMismatchedContinuationCandidateCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRejectsMismatchedContinuationCandidateCellSizeTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* RootTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationContinuationRoot"),
		FIntVector(1, 1, 1));
	ULayoutModuleAsset* RootModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationContinuationRoot"),
		RootTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	UChunkStructureTemplate* ContinuationTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationContinuationMismatch"),
		FIntVector(2, 2, 1));
	ULayoutModuleAsset* ContinuationModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationContinuationMismatch"),
		ContinuationTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationContinuationMismatch"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationContinuationMismatch"),
		{});
	FLayoutRegionContentEntry ContinuationEntry;
	ContinuationEntry.EntryId = TEXT("ContinuationModule");
	ContinuationEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContinuationEntry.ModuleSettings.Module = ContinuationModule;
	ContentSet->Entries = {ContinuationEntry};
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationContinuationMismatch"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_WorldBindingValidationContinuationRoot"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			TEXT("LayoutContentSet_WorldBindingValidationContinuationRoot"),
			{});
	FLayoutRegionContentEntry RootEntry;
	RootEntry.EntryId = TEXT("RootModule");
	RootEntry.ContentKind = ELayoutRegionContentKind::Module;
	RootEntry.ModuleSettings.Module = RootModule;
	RootContentSet->Entries = {RootEntry};
	RootCandidate.LayoutProfile->ContentSet = RootContentSet;
	RootCandidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfaceRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects continuation-family candidates whose shared cell size disagrees with the binding contract"), Validation.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationAcceptsMultipleSurfacePathFamiliesByEndpointTagTest,
	"PorismExtension.Layout.WorldBinding.Validation.AcceptsMultipleSurfacePathFamiliesByEndpointTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsDuplicateSurfacePathEndpointTagsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsDuplicateSurfacePathEndpointTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationAcceptsMultipleSurfacePathFamiliesByEndpointTagTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationMultipleSurfaceFamilies"),
		FIntVector(1, 1, 1));
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationMultipleSurfaceFamilies"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContinuationEntryLevel = 0;
	Profile->bRequireAllTraversalChannelsReachable = true;

	ULayoutModuleAsset* EntryModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationMultipleSurfaceFamilies"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		{},
		FGameplayTagContainer(),
		{},
		FGameplayTagContainer());

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("RoadEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = EntryModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationMultipleSurfaceFamilies"),
		{ModuleEntry});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationMultipleSurfaceFamilies"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorTrail});

	for (int32 FamilyIndex = 0; FamilyIndex < 2; ++FamilyIndex)
	{
		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = *FString::Printf(TEXT("SurfaceRoad_%d"), FamilyIndex);
		Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
		Family.EndpointConnectorTypeTag = FamilyIndex == 0
			? LayoutGameplayTags::ConnectorRoad
			: LayoutGameplayTags::ConnectorTrail;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 16;
		Family.SolveBudget.MaxSolveDurationSeconds = 1.0f;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("RoadCandidate_%d"), FamilyIndex);
		FamilyCandidate.LayoutProfile = Profile;
		FamilyCandidate.Weight = 1;
	}

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestTrue(TEXT("World binding accepts multiple SurfacePath continuation families when each owns a distinct endpoint connector tag"), Validation.IsValid());
	return true;
}

bool FLayoutWorldBindingValidationRejectsDuplicateSurfacePathEndpointTagsTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationDuplicateSurfacePathEndpointTags"),
		FIntVector(1, 1, 1));
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationDuplicateSurfacePathEndpointTags"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContinuationEntryLevel = 0;
	Profile->bRequireAllTraversalChannelsReachable = true;

	ULayoutModuleAsset* EntryModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationDuplicateSurfacePathEndpointTags"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		{},
		FGameplayTagContainer(),
		{},
		FGameplayTagContainer());

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("RoadEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = EntryModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationDuplicateSurfacePathEndpointTags"),
		{ModuleEntry});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationDuplicateSurfacePathEndpointTags"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;

	for (int32 FamilyIndex = 0; FamilyIndex < 2; ++FamilyIndex)
	{
		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = *FString::Printf(TEXT("SurfaceRoad_%d"), FamilyIndex);
		Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
		Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 16;
		Family.SolveBudget.MaxSolveDurationSeconds = 1.0f;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("RoadCandidate_%d"), FamilyIndex);
		FamilyCandidate.LayoutProfile = Profile;
		FamilyCandidate.Weight = 1;
	}

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects duplicate SurfacePath endpoint connector tags on the active planner path"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the duplicate SurfacePath endpoint tag ambiguity"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("same EndpointConnectorTypeTag"))
				&& Message.Message.Contains(TEXT("SurfacePath"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRequiresSurfacePathFamilyForExportedRootConnectorsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RequiresSurfacePathFamilyForExportedRootConnectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRequiresMatchingContinuationEndpointTagForExportedRootConnectorsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RequiresMatchingContinuationEndpointTagForExportedRootConnectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRequiresMatchingSurfacePathEndpointTagForExportedRoadOrTrailConnectorsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RequiresMatchingSurfacePathEndpointTagForExportedRoadOrTrailConnectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRequiresSurfacePathFamilyForExportedRootConnectorsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationExportedRoadRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationExportedRoadRoot"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationExportedRoadRoot"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorRoad});

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects exported connector roots that have no SurfacePath family on the active runtime path"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the missing SurfacePath family for exported root connectors"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("does not author any SurfacePath continuation family"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRequiresMatchingContinuationEndpointTagForExportedRootConnectorsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationExportedBridgeRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationExportedBridgeRoot"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationExportedBridgeRoot"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorBridge});

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfaceRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects exported connector roots whose connector tag has no matching authored continuation-family endpoint tag"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the missing matching continuation-family endpoint tag for exported root connectors"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("exports connector tag"))
				&& Message.Message.Contains(TEXT("no continuation family on the binding authors that EndpointConnectorTypeTag"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationRequiresMatchingSurfacePathEndpointTagForExportedRoadOrTrailConnectorsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationExportedTrailRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationExportedTrailRoot"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationExportedTrailRoot"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorTrail});

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeTrail");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTrail;
	Family.MaxConnectionsPerSite = 1;
	Family.MaxConnectionDistanceInCells = 16;
	Family.SolveBudget.MaxSolveDurationSeconds = 1.0f;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("TrailBridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects exported road or trail root connectors whose tag is only authored on non-SurfacePath continuation families"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the missing matching SurfacePath family endpoint tag for exported road or trail root connectors"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("exports surface-path connector tag"))
				&& Message.Message.Contains(TEXT("no SurfacePath continuation family"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsDuplicateContinuationEndpointTagsTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsDuplicateContinuationEndpointTags",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationAcceptsDuplicateContinuationEndpointTagsAcrossFamilyTypesTest,
	"PorismExtension.Layout.WorldBinding.Validation.AcceptsDuplicateContinuationEndpointTagsAcrossFamilyTypes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRejectsDuplicateContinuationEndpointTagsTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationDuplicateEndpointTags"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationDuplicateEndpointTags"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationDuplicateEndpointTags"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorRoad});

	for (int32 FamilyIndex = 0; FamilyIndex < 2; ++FamilyIndex)
	{
		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = *FString::Printf(TEXT("RoadFamily_%d"), FamilyIndex);
		Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
		Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 16;
		Family.SolveBudget.MaxSolveDurationSeconds = 1.0f;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("RoadCandidate_%d"), FamilyIndex);
		FamilyCandidate.LayoutProfile = Profile;
		FamilyCandidate.Weight = 1;
	}

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects multiple continuation families of the same family type that author the same endpoint connector tag"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the duplicate endpoint connector tag ambiguity within one family type"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("same EndpointConnectorTypeTag"))
				&& Message.Message.Contains(TEXT("BridgeContinuation"));
		}));
	return true;
}

bool FLayoutWorldBindingValidationAcceptsDuplicateContinuationEndpointTagsAcrossFamilyTypesTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* EntryTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingValidationDuplicateEndpointTagsAcrossTypes"),
		FIntVector(1, 1, 1));
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationDuplicateEndpointTagsAcrossTypes"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContinuationEntryLevel = 0;
	Profile->bRequireAllTraversalChannelsReachable = true;

	ULayoutModuleAsset* EntryModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingValidationDuplicateEndpointTagsAcrossTypes"),
		EntryTemplate,
		{ELayoutCellIntent::Entry},
		{},
		FGameplayTagContainer(),
		{},
		FGameplayTagContainer());

	FLayoutRegionContentEntry ModuleEntry;
	ModuleEntry.EntryId = TEXT("RoadEntry");
	ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	ModuleEntry.ModuleSettings.Module = EntryModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationDuplicateEndpointTagsAcrossTypes"),
		{ModuleEntry});
	Profile->ContentSet = ContentSet;

	ULayoutProfileAsset* RootProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationDuplicateEndpointTagsAcrossTypesRoot"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationDuplicateEndpointTagsAcrossTypesRoot"),
		{});
	RootProfile->ContentSet = RootContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationDuplicateEndpointTagsAcrossTypes"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = RootProfile;
	RootCandidate.Weight = 1;
	RootCandidate.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorRoad});

	for (int32 FamilyIndex = 0; FamilyIndex < 2; ++FamilyIndex)
	{
		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = *FString::Printf(TEXT("RoadFamily_%d"), FamilyIndex);
		Family.FamilyType = FamilyIndex == 0
			? ELayoutWorldBindingContinuationFamilyType::SurfacePath
			: ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
		Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 16;
		Family.SolveBudget.MaxSolveDurationSeconds = 1.0f;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("RoadCandidate_%d"), FamilyIndex);
		FamilyCandidate.LayoutProfile = Profile;
		FamilyCandidate.Weight = 1;
	}

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestTrue(TEXT("World binding accepts duplicate endpoint connector tags across different continuation family types"), Validation.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsBridgeGapSettingsOnSurfacePathTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsBridgeGapSettingsOnSurfacePath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRejectsBridgeGapSettingsOnSurfacePathTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationSurfaceGapSettings"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationSurfaceGapSettings"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationSurfaceGapSettings"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfaceRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxBridgeGapCells = 2;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects bridge-gap settings on SurfacePath families"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the bridge-gap setting on a non-bridge family"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("only BridgeContinuation families may consume bridge-gap spans"));
		}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingValidationRejectsBridgeGapSettingsOnTunnelTest,
	"PorismExtension.Layout.WorldBinding.Validation.RejectsBridgeGapSettingsOnTunnel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingValidationRejectsBridgeGapSettingsOnTunnelTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingValidationTunnelGapSettings"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingValidationTunnelGapSettings"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBindingValidationTunnelGapSettings"));
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	RootCandidate.CandidateId = TEXT("PrimaryCandidate");
	RootCandidate.LayoutProfile = Profile;
	RootCandidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelRoad");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxBridgeGapCells = 2;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("TunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	const FLayoutValidationResult Validation = WorldBinding->ValidateWorldBinding();
	TestFalse(TEXT("World binding rejects bridge-gap settings on TunnelContinuation families"), Validation.IsValid());
	TestTrue(
		TEXT("World binding reports the bridge-gap setting on a tunnel family"),
		Validation.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("only BridgeContinuation families may consume bridge-gap spans"));
	}));
	return true;
}
