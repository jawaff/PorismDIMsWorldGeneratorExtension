// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateBoundaryClosureTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}


	void AddOuterPerimeterClosure(ULayoutProfileAsset* Profile, const int32 MinThicknessCells)
	{
		FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
		ClosureRequirement.ClosureId = TEXT("OuterPerimeter");
		ClosureRequirement.Zone = ELayoutPlacementZone::Perimeter;
		ClosureRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
		ClosureRequirement.BoundsPolicy.InsetCells = 0;
		ClosureRequirement.BoundsPolicy.MinLevel = 0;
		ClosureRequirement.BoundsPolicy.MaxLevel = 0;
		ClosureRequirement.MinThicknessCells = MinThicknessCells;
			}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosureSatisfiedByDerivedModuleSpanOffersTest,
	"PorismExtension.Layout.Solver.Closure.SatisfiedByDerivedModuleSpanOffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosureSatisfiedByDerivedModuleSpanOffersTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateBoundaryClosureTestOuter(TEXT("LayoutBoundaryClosureSatisfied"));
	ULayoutProfileAsset* Profile = PorismLayoutTestUtilities::CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ClosureProfile_Satisfied"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	Profile->ContentSet->Entries.SetNum(1);
	AddOuterPerimeterClosure(Profile, 1);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 17);
	TestTrue(TEXT("Solve succeeds when derived module spans cover the closure"), Result.bSucceeded);
	TestEqual(TEXT("One closure coverage summary is emitted"), Result.ClosureCoverage.Num(), 1);
	if (Result.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestTrue(TEXT("Closure summary is satisfied"), Result.ClosureCoverage[0].bSatisfied);
	TestEqual(TEXT("Every required closure segment is covered"), Result.ClosureCoverage[0].CoveredSegmentCount, Result.ClosureCoverage[0].RequiredSegmentCount);
	TestTrue(TEXT("Compiled closure segments were emitted"), Result.ClosureSegments.Num() > 0);
	TestEqual(TEXT("A rectangular 2x2 perimeter groups into four covered closure runs"), Result.ClosureRuns.Num(), 4);
	TestFalse(TEXT("No compiled closure segment is left uncovered"), Result.ClosureSegments.ContainsByPredicate([](const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		return !Segment.bCovered;
	}));
	TestFalse(TEXT("No closure run is left uncovered"), Result.ClosureRuns.ContainsByPredicate([](const FLayoutClosureRunRecord& Run)
	{
		return !Run.bCovered;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosureRejectsUnsatisfiedThicknessRequirementTest,
	"PorismExtension.Layout.Solver.Closure.RejectsUnsatisfiedThicknessRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosureRejectsUnsatisfiedThicknessRequirementTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateBoundaryClosureTestOuter(TEXT("LayoutBoundaryClosureUnsatisfied"));
	ULayoutProfileAsset* Profile = PorismLayoutTestUtilities::CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ClosureProfile_Unsatisfied"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	AddOuterPerimeterClosure(Profile, 2);

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 23);
	TestFalse(TEXT("Solve fails when no provider can prove the required closure thickness"), Result.bSucceeded);
	TestEqual(TEXT("One closure coverage summary is emitted"), Result.ClosureCoverage.Num(), 1);
	if (Result.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestFalse(TEXT("Closure summary is unsatisfied"), Result.ClosureCoverage[0].bSatisfied);
	TestEqual(TEXT("An uncovered 2x2 perimeter still groups into four closure runs"), Result.ClosureRuns.Num(), 4);
	TestFalse(TEXT("No closure run is marked covered when no provider is thick enough"), Result.ClosureRuns.ContainsByPredicate([](const FLayoutClosureRunRecord& Run)
	{
		return Run.bCovered;
	}));
	TestTrue(TEXT("Failure reason mentions closure coverage"), Result.FailureReason.Contains(TEXT("Closure 'OuterPerimeter' is not fully covered")));
	TestTrue(TEXT("Validation messages mention uncovered closure coverage"), Result.Messages.ContainsByPredicate([](const FLayoutValidationMessage& Message)
	{
		return Message.Severity == ELayoutValidationSeverity::Error
			&& Message.Message.Contains(TEXT("not fully covered"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosureUsesActualPlannedCellPositionsTest,
	"PorismExtension.Layout.Solver.Closure.UsesActualPlannedCellPositions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosureUsesActualPlannedCellPositionsTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateBoundaryClosureTestOuter(TEXT("LayoutBoundaryClosureActualPlannedCells"));
	ULayoutProfileAsset* Profile = PorismLayoutTestUtilities::CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ClosureProfile_OffsetBoundary"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	AddOuterPerimeterClosure(Profile, 1);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
	PlannedCell.Cell = FIntVector(3, 4, 0);
	PlannedCell.Intent = ELayoutCellIntent::Boundary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells( Profile, 29, FIntPoint(1, 1), PlannedCells);
	TestTrue(TEXT("Solve succeeds for supplied planned cells whose local origin is offset"), Result.bSucceeded);
	TestEqual(TEXT("One closure coverage summary is emitted"), Result.ClosureCoverage.Num(), 1);
	if (Result.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestTrue(TEXT("Offset one-cell closure is satisfied"), Result.ClosureCoverage[0].bSatisfied);
	TestEqual(TEXT("A one-cell perimeter compiles four exterior closure faces"), Result.ClosureCoverage[0].RequiredSegmentCount, 4);
	TestEqual(TEXT("All one-cell closure faces are covered"), Result.ClosureCoverage[0].CoveredSegmentCount, 4);
	TestEqual(TEXT("A one-cell perimeter derives four one-segment closure runs"), Result.ClosureRuns.Num(), 4);
	TestTrue(TEXT("Compiled closure segments keep the supplied planned cell position"), Result.ClosureSegments.ContainsByPredicate([](const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		return Segment.Cell == FIntVector(3, 4, 0) && Segment.FaceDirection == ELayoutFaceDirection::PosX;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosureRejectsMismatchedClosureSpecificSpanOffersTest,
	"PorismExtension.Layout.Solver.Closure.RejectsMismatchedClosureSpecificSpanOffers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosureRejectsMismatchedClosureSpecificSpanOffersTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateBoundaryClosureTestOuter(TEXT("LayoutBoundaryClosureClosureIdMismatch"));
	ULayoutProfileAsset* Profile = PorismLayoutTestUtilities::CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ClosureProfile_ClosureIdMismatch"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	AddOuterPerimeterClosure(Profile, 1);

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 31, TEXT("ClosureIdMismatch"));
	for (FLayoutModuleSolveSnapshot& ModuleSnapshot : Request.ModuleCatalog.Modules)
	{
		for (FLayoutDerivedSpanOffer& SpanOffer : ModuleSnapshot.DerivedSpanOffers)
		{
			SpanOffer.ClosureId = TEXT("DifferentClosure");
		}
	}

	const FLayoutRegionSolveResult Result = FLayoutProfileSolver::SolveRegion(Request);
	TestFalse(TEXT("Solve fails when every span offer is bound to a different closure id"), Result.SolveResult.bSucceeded);
	TestTrue(TEXT("Failure reason mentions uncovered closure coverage"), Result.SolveResult.FailureReason.Contains(TEXT("not fully covered")));
	TestEqual(TEXT("One closure coverage summary is emitted"), Result.SolveResult.ClosureCoverage.Num(), 1);
	if (Result.SolveResult.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestFalse(TEXT("Closure summary is unsatisfied"), Result.SolveResult.ClosureCoverage[0].bSatisfied);
	TestEqual(TEXT("No mismatched span offer covers the closure"), Result.SolveResult.ClosureCoverage[0].CoveredSegmentCount, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosureUsesAuthoredLevelAndSkipsGeneratedBridgeCellsTest,
	"PorismExtension.Layout.Solver.Closure.UsesAuthoredLevelAndSkipsGeneratedBridgeCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosureUsesAuthoredLevelAndSkipsGeneratedBridgeCellsTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateBoundaryClosureTestOuter(TEXT("LayoutBoundaryClosureAuthoredLevelBridge"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ClosureProfile_AuthoredLevelBridge"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet->Entries.SetNum(1);
	AddOuterPerimeterClosure(Profile, 1);

	TArray<FLayoutPlannedCell> PlannedCells;
	FLayoutPlannedCell& ShiftedRealCell = PlannedCells.AddDefaulted_GetRef();
	ShiftedRealCell.Cell = FIntVector(3, 4, 7);
	ShiftedRealCell.ModuleLevelIndex = 0;
	ShiftedRealCell.Intent = ELayoutCellIntent::Boundary;

	FLayoutPlannedCell& GeneratedBridgeCell = PlannedCells.AddDefaulted_GetRef();
	GeneratedBridgeCell.Cell = FIntVector(4, 4, 7);
	GeneratedBridgeCell.ModuleLevelIndex = 0;
	GeneratedBridgeCell.Intent = ELayoutCellIntent::Boundary;
	GeneratedBridgeCell.bIsBridgeCell = true;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveWithPlannedCells(
		Profile,
		37,
		FIntPoint(2, 1),
		PlannedCells);
	TestTrue(TEXT("Shifted authored-level closure solve succeeds"), Result.bSucceeded);
	TestEqual(TEXT("One closure coverage summary is emitted"), Result.ClosureCoverage.Num(), 1);
	if (!Result.bSucceeded || Result.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestEqual(TEXT("Only real shifted cell contributes its three exterior faces"), Result.ClosureCoverage[0].RequiredSegmentCount, 3);
	TestTrue(TEXT("Closure segments retain physical shifted Z"), Result.ClosureSegments.ContainsByPredicate([](const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		return Segment.Cell == FIntVector(3, 4, 7) && Segment.FaceDirection == ELayoutFaceDirection::NegX;
	}));
	TestFalse(TEXT("Generated bridge cell contributes no exterior closure segment"), Result.ClosureSegments.ContainsByPredicate([](const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		return Segment.Cell == FIntVector(4, 4, 7);
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosurePrunesMismatchedProviderDomainTest,
	"PorismExtension.Layout.Solver.Closure.PrunesMismatchedProviderDomain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosurePrunesMismatchedProviderDomainTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateBoundaryClosureTestOuter(TEXT("LayoutBoundaryClosureProviderPruning"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ClosureProfile_ProviderPruning"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	AddOuterPerimeterClosure(Profile, 1);

	FLayoutClosureProviderIntent& NonProviderIntent =
		Profile->ContentSet->Entries[0].ClosureProviderIntents.AddDefaulted_GetRef();
	NonProviderIntent.ProviderIntentId = TEXT("MismatchedProvider");
	NonProviderIntent.Zone = ELayoutPlacementZone::Perimeter;
	NonProviderIntent.ClosureId = TEXT("DifferentClosure");

	FLayoutClosureProviderIntent& ProviderIntent =
		Profile->ContentSet->Entries[1].ClosureProviderIntents.AddDefaulted_GetRef();
	ProviderIntent.ProviderIntentId = TEXT("OuterProvider");
	ProviderIntent.Zone = ELayoutPlacementZone::Perimeter;
	ProviderIntent.ClosureId = TEXT("OuterPerimeter");

	const FLayoutSolveResult Result = FLayoutProfileSolver::Solve(Profile, 41);
	TestTrue(TEXT("Hard closure chooses an eligible provider domain"), Result.bSucceeded);
	TestEqual(TEXT("One closure coverage summary is emitted"), Result.ClosureCoverage.Num(), 1);
	if (!Result.bSucceeded || Result.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestTrue(TEXT("Hard closure coverage remains satisfied after domain pruning"), Result.ClosureCoverage[0].bSatisfied);
	TestFalse(TEXT("Mismatched closure provider never survives to a boundary placement"), Result.Placements.ContainsByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.SourceContentEntryId.ToString().Contains(TEXT("GenericEntry"));
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutBoundaryClosureProjectsRotatedCompositeSpanFromRootOnceTest,
	"PorismExtension.Layout.Solver.Closure.ProjectsRotatedCompositeSpanFromRootOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutBoundaryClosureProjectsRotatedCompositeSpanFromRootOnceTest::RunTest(const FString& Parameters)
{
	LayoutProfileSolverInternal::FSolveContext Context;
	Context.ProfileSnapshot.LevelCount = 1;
	FLayoutClosureRequirement& Requirement = Context.ProfileSnapshot.ClosureRequirements.AddDefaulted_GetRef();
	Requirement.ClosureId = TEXT("OuterPerimeter");
	Requirement.Zone = ELayoutPlacementZone::Perimeter;
	Requirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
	Requirement.BoundsPolicy.MinLevel = 0;
	Requirement.BoundsPolicy.MaxLevel = 0;
	Requirement.MinThicknessCells = 1;

	for (const FIntVector& Cell : {FIntVector(10, 10, 0), FIntVector(10, 11, 0)})
	{
		FLayoutPlannedCell& PlannedCell = Context.Result.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = ELayoutCellIntent::Boundary;
		Context.PlannedCellIntents.Add(Cell, PlannedCell.Intent);
	}

	FLayoutModuleSolveSnapshot& Snapshot = Context.ModuleSnapshots.AddDefaulted_GetRef();
	Snapshot.BoundsCells = FIntVector(2, 1, 1);
	Snapshot.OccupiedLocalCells = {FIntVector::ZeroValue, FIntVector(1, 0, 0)};
	FLayoutDerivedSpanOffer& SpanOffer = Snapshot.DerivedSpanOffers.AddDefaulted_GetRef();
	SpanOffer.SpanOfferId = TEXT("RotatedCompositeFarSpan");
	SpanOffer.LocalCell = FIntVector(1, 0, 0);
	SpanOffer.FaceDirection = ELayoutFaceDirection::PosX;
	SpanOffer.ThicknessCells = 1;
	SpanOffer.bSealsBoundary = true;

	LayoutProfileSolverInternal::FSolveContext::FOrientedModuleVariant& Variant = Context.Variants.AddDefaulted_GetRef();
	Variant.ModuleSnapshotIndex = 0;
	Variant.YawRotationSteps = 1;
	Variant.WorldSpanOffers = Snapshot.DerivedSpanOffers;
	Variant.WorldSpanOffers[0].FaceDirection = FLayoutDirectionUtils::RotateYaw(
		SpanOffer.FaceDirection,
		Variant.YawRotationSteps);

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& RootPlacement =
		Context.Placements.Add(FIntVector(10, 10, 0));
	RootPlacement.YawRotationSteps = 1;
	RootPlacement.VariantIndex = 0;
	RootPlacement.ModuleSnapshotIndex = 0;
	RootPlacement.BundleRootCell = FIntVector(10, 10, 0);
	RootPlacement.bBundleRoot = true;
	RootPlacement.bEmpty = false;

	LayoutProfileSolverInternal::FSolveContext::FSolvePlacement& ShadowPlacement =
		Context.Placements.Add(FIntVector(10, 11, 0));
	ShadowPlacement = RootPlacement;
	ShadowPlacement.LocalBundleCell = FIntVector(1, 0, 0);
	ShadowPlacement.bBundleRoot = false;

	TestFalse(TEXT("Partial synthetic composite leaves unrelated perimeter faces uncovered"), LayoutProfileSolverInternal::ValidateClosureCoverageForContext(Context));
	const ELayoutFaceDirection RotatedFace = FLayoutDirectionUtils::RotateYaw(ELayoutFaceDirection::PosX, 1);
	int32 CoveredRotatedFarSegmentCount = 0;
	for (const FLayoutClosureCoverageSegmentRecord& Segment : Context.Result.ClosureSegments)
	{
		CoveredRotatedFarSegmentCount += Segment.Cell == FIntVector(10, 11, 0)
			&& Segment.FaceDirection == RotatedFace
			&& Segment.bCovered;
	}
	TestEqual(TEXT("One rotated far-cell segment is covered from bundle root"), CoveredRotatedFarSegmentCount, 1);
	return true;
}
