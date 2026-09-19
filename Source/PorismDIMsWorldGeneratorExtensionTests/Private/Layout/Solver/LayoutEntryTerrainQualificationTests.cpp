// Copyright 2026 Spotted Loaf Studio

#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutEntryTerrainQualificationSurvivesParentAuthorityRelocationTest,
	"PorismExtension.Layout.Solver.Entry.TerrainQualificationSurvivesParentAuthorityRelocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Confirms child-aware Entry relocation cannot turn an unqualified terrain cell into a valid Entry. */
bool FLayoutEntryTerrainQualificationSurvivesParentAuthorityRelocationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutEntryTerrainQualification_Relocation"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("Cell"), FIntVector(8, 8, 8));
	TArray<FLayoutFaceRule> Faces;
	for (int32 DirectionIndex = 0; DirectionIndex < 6; ++DirectionIndex)
	{
		const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(DirectionIndex);
		Faces.Add(MakeConnectionFaceRule(
			Direction,
			LayoutGameplayTags::FaceOpen,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			Direction == ELayoutFaceDirection::PosZ || Direction == ELayoutFaceDirection::NegZ
				? FGameplayTagContainer()
				: MakeTags({LayoutGameplayTags::TraversalPrimary})));
	}
	ULayoutModuleAsset* Universal = CreateModule(
		Outer,
		TEXT("Universal"),
		Template,
		{ELayoutCellIntent::Entry, ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
		Faces);

	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("EntryTerrainQualificationRelocation");
	Request.Seed = 57;
	Request.FootprintSize = FIntPoint(3, 3);
	Request.ProfileSnapshot.LevelCount = 1;
	Request.ProfileSnapshot.EntryCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.EntryCount = 1;
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Request.ModuleCatalog.Modules = {FLayoutProfileSolver::BuildModuleSnapshot(Universal, 1)};
	Request.bHasQualifiedEntryCells = true;
	const FIntVector QualifiedEntry(0, 1, 0);
	Request.QualifiedEntryCells = {QualifiedEntry};

	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.ModuleLevelIndex = 0;
			Cell.Intent = Cell.Cell == QualifiedEntry
				? ELayoutCellIntent::Entry
				: (X == 0 || Y == 0 || X == 2 || Y == 2)
					? ELayoutCellIntent::Boundary
					: ELayoutCellIntent::Interior;
			Cell.EntryOrigin = Cell.Intent == ELayoutCellIntent::Entry
				? ELayoutEntryOrigin::AuthoredBoundary
				: ELayoutEntryOrigin::None;
		}
	}

	FLayoutRegionSolveRequest FrozenRequest;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Original terrain-qualified Entry freezes successfully"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request,
			true,
			{},
			{},
			FrozenRequest,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	TestFalse(
		TEXT("Blocking the only terrain-qualified Entry rejects instead of relocating onto unqualified terrain"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request,
			true,
			{QualifiedEntry},
			{},
			FrozenRequest,
			FailureReason));
	return true;
}
