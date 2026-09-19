// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	ULayoutCompositeModuleAsset* CreateCompositeModule(
		const TCHAR* Name,
		const TArray<FLayoutCompositeModuleCell>& Cells)
	{
		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(GetTransientPackage(), FName(Name));
		Composite->Cells = Cells;
		return Composite;
	}

	FLayoutCompositeModuleCell MakeCompositeCell(
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
	FLayoutCompositeModuleValidationRejectsMissingOccupiedCellsTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsMissingOccupiedCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationRejectsMissingOccupiedCellsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(TEXT("LayoutCompositeModule_Empty"), {});
	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();

	TestFalse(TEXT("Composite without occupied cells is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports that the composite must contain at least one occupied cell"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("must contain at least one occupied cell")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationRejectsDuplicateLocalCellsTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsDuplicateLocalCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationRejectsDuplicateLocalCellsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeDuplicateLocalCell"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* LeafA = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeDuplicateLeafA"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutModuleAsset* LeafB = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeDuplicateLeafB"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_DuplicateLocalCell"),
		{
			MakeCompositeCell(LeafA, FIntVector(0, 0, 0)),
			MakeCompositeCell(LeafB, FIntVector(0, 0, 0))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestFalse(TEXT("Composite with duplicate occupied local cells is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports duplicate occupied local cells"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("duplicates another occupied local cell")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationRejectsInvalidRelativeYawTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsInvalidRelativeYaw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationDefersStaleOneCellBoundsMismatchToLeafValidationTest,
	"PorismExtension.Layout.CompositeModule.Validation.DefersStaleOneCellBoundsMismatchToLeafValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationRejectsInvalidRelativeYawTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeInvalidYaw"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Leaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeInvalidYawLeaf"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_InvalidYaw"),
		{
			MakeCompositeCell(Leaf, FIntVector(0, 0, 0), 5)
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestFalse(TEXT("Composite with an invalid relative yaw is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports the invalid yaw range"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("invalid relative yaw step")));
	return true;
}

bool FLayoutCompositeModuleValidationDefersStaleOneCellBoundsMismatchToLeafValidationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeStaleOneCellBounds"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Leaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeStaleOneCellBoundsLeaf"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_StaleOneCellBounds"),
		{
			MakeCompositeCell(Leaf, FIntVector(0, 0, 0))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestTrue(TEXT("Stale one-cell bounds drift is now treated as compatibility-only at the composite surface"), ValidationResult.IsValid());
	TestFalse(TEXT("Validation no longer reports a leaf-owned template-dimension mismatch when the occupied contract is already one cell"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("template dimensions do not match base cell size and bounds")));
	TestFalse(TEXT("Composite validation no longer adds a second fake non-leaf error when occupancy is already one cell"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("references a non-leaf module")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationRejectsMismatchedLeafCellMetricsTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsMismatchedLeafCellMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationRejectsNonSquareLeafCellMetricsTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsNonSquareLeafCellMetrics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationIgnoresHorizontalMatchingYawRequirementTest,
	"PorismExtension.Layout.CompositeModule.Validation.IgnoresHorizontalMatchingYawRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationRejectsMismatchedLeafCellMetricsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* SmallTemplate = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeSmallMetric"), FIntVector(8, 8, 8));
	UChunkStructureTemplate* LargeTemplate = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeLargeMetric"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* SmallLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeSmallMetricLeaf"),
		SmallTemplate,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutModuleAsset* LargeLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeLargeMetricLeaf"),
		LargeTemplate,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_MismatchedLeafMetrics"),
		{
			MakeCompositeCell(SmallLeaf, FIntVector(0, 0, 0)),
			MakeCompositeCell(LargeLeaf, FIntVector(1, 0, 0))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestFalse(TEXT("Composite with mismatched leaf cell metrics is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports the shared cell-size mismatch"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("mismatched leaf cell size")));
	return true;
}

bool FLayoutCompositeModuleValidationRejectsNonSquareLeafCellMetricsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeNonSquareMetric"), FIntVector(16, 8, 8));
	ULayoutModuleAsset* Leaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeNonSquareMetricLeaf"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_NonSquareLeafMetrics"),
		{
			MakeCompositeCell(Leaf, FIntVector(0, 0, 0), 1)
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestFalse(TEXT("Composite with non-square leaf cell metrics is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports the non-square leaf cell size contract failure"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("non-square shared leaf cell size")));
	return true;
}

bool FLayoutCompositeModuleValidationIgnoresHorizontalMatchingYawRequirementTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeHorizontalYaw"), FIntVector(8, 8, 8));
	TArray<FLayoutFaceRule> LeftFaceRules = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	LeftFaceRules[0].bRequireMatchingYawWithFilledNeighbor = true;

	TArray<FLayoutFaceRule> RightFaceRules = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	RightFaceRules[0] = MakeFaceRule(
		ELayoutFaceDirection::PosX,
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	RightFaceRules[2] = MakeFaceRule(
		ELayoutFaceDirection::PosY,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	RightFaceRules[2].bRequireMatchingYawWithFilledNeighbor = true;

	ULayoutModuleAsset* LeftLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeHorizontalYawLeft"),
		Template,
		{ELayoutCellIntent::Interior},
		LeftFaceRules);
	ULayoutModuleAsset* RightLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeHorizontalYawRight"),
		Template,
		{ELayoutCellIntent::Interior},
		RightFaceRules);

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_HorizontalYawIgnored"),
		{
			MakeCompositeCell(LeftLeaf, FIntVector(0, 0, 0), 0),
			MakeCompositeCell(RightLeaf, FIntVector(1, 0, 0), 1)
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestTrue(TEXT("Composite validation ignores horizontal matching-yaw requirements on glued faces"), ValidationResult.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationRejectsIncompatibleInternalGlueTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsIncompatibleInternalGlue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationRejectsIncompatibleInternalGlueTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeIncompatibleGlue"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* LeftLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeIncompatibleLeft"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	TArray<FLayoutFaceRule> RightFaceRules = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceSolid}),
		MakeTags({LayoutGameplayTags::FaceSolid}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	RightFaceRules[1].OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor;
	ULayoutModuleAsset* RightLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeIncompatibleRight"),
		Template,
		{ELayoutCellIntent::Interior},
		RightFaceRules);

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_IncompatibleGlue"),
		{
			MakeCompositeCell(LeftLeaf, FIntVector(0, 0, 0)),
			MakeCompositeCell(RightLeaf, FIntVector(1, 0, 0))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestFalse(TEXT("Composite with incompatible glued faces is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports structurally incompatible glued neighbors"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("not structurally compatible")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationRejectsDisconnectedTraversableCellsTest,
	"PorismExtension.Layout.CompositeModule.Validation.RejectsDisconnectedTraversableCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationRejectsDisconnectedTraversableCellsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeDisconnectedTraversal"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* PrimaryTraversalLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeTraversalPrimary"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* SecondaryTraversalLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeTraversalSecondary"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalSecondary})));

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_DisconnectedTraversal"),
		{
			MakeCompositeCell(PrimaryTraversalLeaf, FIntVector(0, 0, 0)),
			MakeCompositeCell(SecondaryTraversalLeaf, FIntVector(1, 0, 0))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestFalse(TEXT("Composite with traversal-capable cells that do not share glued traversal is rejected"), ValidationResult.IsValid());
	TestTrue(TEXT("Validation reports disconnected traversable leaf cells"), ContainsValidationMessageSubstring(
		ValidationResult.Messages,
		TEXT("do not form one connected traversable subgraph")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationAllowsNonTraversableWallCellsTest,
	"PorismExtension.Layout.CompositeModule.Validation.AllowsNonTraversableWallCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationAllowsNonTraversableWallCellsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_CompositeWallCell"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* TraversableLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeTraversableLeaf"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	TArray<FLayoutFaceRule> WallFaceRules = {
		MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceOpen}), MakeTags({LayoutGameplayTags::FaceOpen}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeFaceRule(ELayoutFaceDirection::PosY, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeFaceRule(ELayoutFaceDirection::NegY, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeFaceRule(ELayoutFaceDirection::PosZ, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
		MakeFaceRule(ELayoutFaceDirection::NegZ, MakeTags({LayoutGameplayTags::FaceSolid}), MakeTags({LayoutGameplayTags::FaceSolid}), ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
	};
	ULayoutModuleAsset* WallLeaf = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeWallLeaf"),
		Template,
		{ELayoutCellIntent::Boundary},
		WallFaceRules);
	// Mark exposed faces as boundary-facing so composite validation recognizes
	// this wall cell contributes a real exterior boundary face.
	WallLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	WallLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	WallLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	WallLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	WallLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_NonTraversableWall"),
		{
			MakeCompositeCell(TraversableLeaf, FIntVector(0, 0, 0)),
			MakeCompositeCell(WallLeaf, FIntVector(1, 0, 0))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	if (!ValidationResult.IsValid())
	{
		for (const FLayoutValidationMessage& Message : ValidationResult.Messages)
		{
			AddInfo(Message.Message);
		}
	}

	TestTrue(TEXT("Composite allows a glued non-traversable wall cell when the traversable subset stays connected"), ValidationResult.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutCompositeModuleValidationAllowsUpperBoundaryStairCellTest,
	"PorismExtension.Layout.CompositeModule.Validation.AllowsUpperBoundaryStairCell",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutCompositeModuleValidationAllowsUpperBoundaryStairCellTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UChunkStructureTemplate* Template = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_CompositeUpperBoundaryStairs"),
		FIntVector(8, 8, 8));
	const TArray<FLayoutFaceRule> StairFaceRules = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		FGameplayTagContainer(),
		MakeTags({LayoutGameplayTags::TraversalPrimary}),
		MakeTags({LayoutGameplayTags::TraversalPrimary}));
	ULayoutModuleAsset* LowerStair = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeLowerInteriorStair"),
		Template,
		{ELayoutCellIntent::Interior, ELayoutCellIntent::VerticalAccess},
		StairFaceRules);
	ULayoutModuleAsset* UpperStair = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_CompositeUpperBoundaryStair"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::VerticalAccess},
		StairFaceRules);
	ULayoutCompositeModuleAsset* Composite = CreateCompositeModule(
		TEXT("LayoutCompositeModule_UpperBoundaryStairs"),
		{
			MakeCompositeCell(LowerStair, FIntVector(0, 0, 0)),
			MakeCompositeCell(UpperStair, FIntVector(0, 0, 1))
		});

	const FLayoutValidationResult ValidationResult = Composite->ValidateCompositeModule();
	TestTrue(
		TEXT("Upper stair may independently support Boundary intent without forcing an exterior-only face contract"),
		ValidationResult.IsValid());
	TestFalse(
		TEXT("Composite validation does not require Boundary intent to imply an explicit exterior-only face"),
		ContainsValidationMessageSubstring(
			ValidationResult.Messages,
			TEXT("boundary-capable leaf becomes fully non-boundary")));
	return true;
}
