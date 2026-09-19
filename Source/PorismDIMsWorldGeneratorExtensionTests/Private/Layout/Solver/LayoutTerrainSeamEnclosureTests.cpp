// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"

#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateTerrainSeamTestOuter()
	{
		return CreatePackage(TEXT("/Temp/LayoutTerrainSeamEnclosureTests"));
	}

	TArray<FLayoutPlannedCell> BuildTerrainSeamCells(const uint8 TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX))
	{
		TArray<FLayoutPlannedCell> Cells;
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 3; ++X)
			{
				FLayoutPlannedCell& Cell = Cells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, 0);
				Cell.Intent = X == 0 || X == 2 || Y == 0 || Y == 2
					? ELayoutCellIntent::Boundary
					: ELayoutCellIntent::Interior;
				if (Cell.Cell == FIntVector(1, 1, 0))
				{
					Cell.TerrainSeamFaceMask = TerrainSeamFaceMask;
				}
			}
		}
		return Cells;
	}

	FLayoutRegionContentEntry BuildModuleEntry(
		const FName EntryId,
		ULayoutModuleAsset* Module,
		const ELayoutPlacementZone Zone)
	{
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = EntryId;
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		Entry.ModuleSettings.PlacementZone = Zone;
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSeamEnclosureSelectsRetainingEdgeWallTest,
	"PorismExtension.Layout.Solver.TerrainSeam.SelectsRetainingEdgeWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSeamEnclosureSelectsRetainingEdgeWallTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateTerrainSeamTestOuter();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TerrainSeamTemplate"), FIntVector(8, 8, 8));
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer OpenAndSolidTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});

	ULayoutModuleAsset* FillModule = CreateModule(
		Outer,
		TEXT("TerrainSeamFill"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			OpenTags,
			OpenAndSolidTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			OpenTags,
			OpenAndSolidTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	TArray<FLayoutFaceRule> RetainingFaces = BuildFilledCubeFaces(
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	FLayoutFaceRule* RetainingFace = RetainingFaces.FindByPredicate([](const FLayoutFaceRule& Rule)
	{
		return Rule.Direction == ELayoutFaceDirection::PosX;
	});
	if (RetainingFace == nullptr)
	{
		AddError(TEXT("Terrain seam test could not author positive-X retaining face."));
		return false;
	}
	RetainingFace->ConnectionTag = LayoutGameplayTags::FaceSolid;
	RetainingFace->AllowedConnectionTags = OpenTags;
	RetainingFace->OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
	RetainingFace->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExteriorOrTerrainSeam;

	ULayoutModuleAsset* RetainingModule = CreateModule(
		Outer,
		TEXT("TerrainSeamRetainingEdge"),
		Template,
		{ELayoutCellIntent::Interior},
		RetainingFaces);
	TestTrue(TEXT("Shared exterior/terrain-seam retaining wall validates"), RetainingModule->ValidateModule().IsValid());

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TerrainSeamContentSet"),
		{
			BuildModuleEntry(TEXT("TerrainSeamFillEntry"), FillModule, ELayoutPlacementZone::Perimeter),
			BuildModuleEntry(TEXT("TerrainSeamRetainingEntry"), RetainingModule, ELayoutPlacementZone::Edge)
		});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("TerrainSeamProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		818,
		TEXT("TerrainSeam/Edge"));
	Request.FootprintSize = FIntPoint(3, 3);
	Request.PlannedCells = BuildTerrainSeamCells();

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutPlannedCell* SeamCell = Result.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Terrain seam cell survives final topology compilation"), SeamCell);
	if (SeamCell != nullptr)
	{
		TestEqual(TEXT("Terrain seam cell remains Interior intent"), SeamCell->Intent, ELayoutCellIntent::Interior);
		TestEqual(TEXT("Terrain seam cell becomes Edge zone"), SeamCell->PlacementZone, ELayoutPlacementZone::Edge);
	}

	const FLayoutPlacedModule* SeamPlacement = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Terrain seam cell receives a retaining placement"), SeamPlacement);
	if (SeamPlacement != nullptr)
	{
		TestEqual(TEXT("Terrain seam selects retaining content entry"),
			SeamPlacement->SourceContentEntryId,
			FName(TEXT("TerrainSeamRetainingEntry")));
		TestEqual(TEXT("Terrain seam wall yaw faces the positive-X seam"), SeamPlacement->YawRotationSteps, 0);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSeamEnclosureSkipsUnadmittedCornerEntryTest,
	"PorismExtension.Layout.Solver.TerrainSeam.SkipsUnadmittedCornerEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSeamEnclosureSkipsUnadmittedCornerEntryTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateTerrainSeamTestOuter();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TerrainSeamEntrySelectorTemplate"), FIntVector(8, 8, 8));
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer OpenAndSolidTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});
	TArray<FLayoutFaceRule> FillFaces = BuildFilledCubeFaces(
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	for (FLayoutFaceRule& FaceRule : FillFaces)
	{
		if (FaceRule.Direction == ELayoutFaceDirection::PosZ
			|| FaceRule.Direction == ELayoutFaceDirection::NegZ)
		{
			FaceRule.ConnectedTraversalChannels.Reset();
		}
		else
		{
			FaceRule.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);
			FaceRule.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		}
	}
	ULayoutModuleAsset* FillModule = CreateModule(
		Outer,
		TEXT("TerrainSeamEntrySelectorFill"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		FillFaces);

	TArray<FLayoutFaceRule> InadmissibleEntryFaces = FillFaces;
	for (FLayoutFaceRule& FaceRule : InadmissibleEntryFaces)
	{
		if (FaceRule.Direction != ELayoutFaceDirection::PosZ
			&& FaceRule.Direction != ELayoutFaceDirection::NegZ)
		{
			FaceRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceInterior;
		}
		if (FaceRule.Direction == ELayoutFaceDirection::PosY)
		{
			FaceRule.ConnectionTag = LayoutGameplayTags::FaceEntry;
			FaceRule.ConnectedTraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
		}
	}
	ULayoutModuleAsset* InadmissibleCornerEntry = CreateModule(
		Outer,
		TEXT("TerrainSeamEntrySelectorInadmissibleCorner"),
		Template,
		{ELayoutCellIntent::Entry},
		InadmissibleEntryFaces);

	TArray<FLayoutFaceRule> LegalEntryFaces = FillFaces;
	for (FLayoutFaceRule& FaceRule : LegalEntryFaces)
	{
		FaceRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::Any;
		if (FaceRule.Direction == ELayoutFaceDirection::PosX)
		{
			FaceRule.OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
			FaceRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam;
		}
		if (FaceRule.Direction == ELayoutFaceDirection::PosY)
		{
			FaceRule.ConnectionTag = LayoutGameplayTags::FaceEntry;
			FaceRule.ConnectedTraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
		}
	}
	ULayoutModuleAsset* LegalEdgeEntry = CreateModule(
		Outer,
		TEXT("TerrainSeamEntrySelectorLegalEdge"),
		Template,
		{ELayoutCellIntent::Entry},
		LegalEntryFaces);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TerrainSeamEntrySelectorContent"),
		{
			BuildModuleEntry(TEXT("Fill"), FillModule, ELayoutPlacementZone::Any),
			BuildModuleEntry(TEXT("InadmissibleCornerEntry"), InadmissibleCornerEntry, ELayoutPlacementZone::Corner),
			BuildModuleEntry(TEXT("LegalEdgeEntry"), LegalEdgeEntry, ELayoutPlacementZone::Edge)
		});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("TerrainSeamEntrySelectorProfile"),
		FIntPoint(4, 3),
		FIntPoint(4, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		820,
		TEXT("TerrainSeam/EntrySelector"));
	Request.FootprintSize = FIntPoint(4, 3);
	Request.bHasFinalizedSteppedTerrainIntents = true;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 4; ++X)
		{
			FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.Intent = X == 0 || X == 3 || Y == 0 || Y == 2
				? ELayoutCellIntent::Boundary
				: ELayoutCellIntent::Interior;
		}
	}
	for (const FIntVector& CellLocation : {FIntVector(1, 1, 0), FIntVector(2, 1, 0)})
	{
		FLayoutPlannedCell* const Cell = Request.PlannedCells.FindByPredicate([CellLocation](const FLayoutPlannedCell& Candidate)
		{
			return Candidate.Cell == CellLocation;
		});
		if (Cell == nullptr)
		{
			AddError(TEXT("Terrain seam Entry selector test could not create lower seam cell."));
			return false;
		}
		Cell->TerrainSeamFaceMask = CellLocation == FIntVector(1, 1, 0)
			? LayoutFaceDirectionMask(ELayoutFaceDirection::PosX) | LayoutFaceDirectionMask(ELayoutFaceDirection::PosY)
			: LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
		FLayoutPlannedCell& UpperBridge = Request.PlannedCells.AddDefaulted_GetRef();
		UpperBridge.Cell = CellLocation + FIntVector(0, 0, 1);
		UpperBridge.Intent = ELayoutCellIntent::Interior;
		UpperBridge.bIsBridgeCell = true;
	}

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		for (const FLayoutValidationMessage& Message : Result.Messages)
		{
			AddError(Message.Message);
		}
		return false;
	}
	const FLayoutPlannedCell* const InadmissibleCorner = Result.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 1, 0);
	});
	const FLayoutPlannedCell* const LegalEdge = Result.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(2, 1, 0);
	});
	TestNotNull(TEXT("Inadmissible corner seam cell survives"), InadmissibleCorner);
	TestNotNull(TEXT("Legal edge seam cell survives"), LegalEdge);
	if (InadmissibleCorner != nullptr && LegalEdge != nullptr)
	{
		TestFalse(TEXT("Inadmissible corner seam is not promoted to Entry"),
			InadmissibleCorner->Intent == ELayoutCellIntent::Entry);
		TestEqual(TEXT("Legal edge seam becomes terrain-seam Entry"), LegalEdge->Intent, ELayoutCellIntent::Entry);
		TestEqual(TEXT("Legal edge seam retains terrain-seam Entry origin"), LegalEdge->EntryOrigin, ELayoutEntryOrigin::TerrainSeam);
	}

	FLayoutRegionSolveRequest NoGateRequest = Request;
	NoGateRequest.ModuleCatalog.Modules.RemoveAll([](const FLayoutModuleSolveSnapshot& Snapshot)
	{
		return Snapshot.SourceContentEntryId == TEXT("LegalEdgeEntry");
	});
	FLayoutPlannedCell* const FormerEdgeCell = NoGateRequest.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(2, 1, 0);
	});
	if (!TestNotNull(TEXT("No-gate preflight retains the second seam cell"), FormerEdgeCell))
	{
		return false;
	}
	FormerEdgeCell->TerrainSeamFaceMask =
		LayoutFaceDirectionMask(ELayoutFaceDirection::PosX)
		| LayoutFaceDirectionMask(ELayoutFaceDirection::PosY);
	TArray<FLayoutPlannedCell> NoGateCells = NoGateRequest.PlannedCells;
	FString NoGateFailureReason;
	ELayoutSteppedTerrainFinalizationFailureKind NoGateFailureKind =
		ELayoutSteppedTerrainFinalizationFailureKind::None;
	TestFalse(
		TEXT("Root stepped preflight rejects a seam component with no admitted Entry gate"),
		FLayoutProfileSolver::FinalizeSteppedTerrainPlan(
			NoGateCells,
			NoGateRequest,
			NoGateFailureReason,
			&NoGateFailureKind));
	TestTrue(
		TEXT("No-gate rejection preserves terrain-seam Entry evidence"),
		NoGateFailureReason.Contains(TEXT("no route-safe eligible Entry cell")));
	TestEqual(
		TEXT("No-gate rejection is classified for binding-authored flat fallback"),
		NoGateFailureKind,
		ELayoutSteppedTerrainFinalizationFailureKind::EntryTerrainQualificationInfeasible);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSeamEnclosureSelectsRetainingCornerWallTest,
	"PorismExtension.Layout.Solver.TerrainSeam.SelectsRetainingCornerWall",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSeamEnclosureSelectsRetainingCornerWallTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateTerrainSeamTestOuter();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TerrainSeamCornerTemplate"), FIntVector(8, 8, 8));
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer OpenAndSolidTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});

	ULayoutModuleAsset* FillModule = CreateModule(
		Outer,
		TEXT("TerrainSeamCornerFill"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			OpenTags,
			OpenAndSolidTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			OpenTags,
			OpenAndSolidTags,
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	TArray<FLayoutFaceRule> RetainingFaces = BuildFilledCubeFaces(
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	for (FLayoutFaceRule& RetainingFace : RetainingFaces)
	{
		const bool bIsTerrainSeamDirection = RetainingFace.Direction == ELayoutFaceDirection::PosX
			|| RetainingFace.Direction == ELayoutFaceDirection::PosY;
		if (bIsTerrainSeamDirection)
		{
			RetainingFace.ConnectionTag = LayoutGameplayTags::FaceSolid;
			RetainingFace.AllowedConnectionTags = OpenTags;
			RetainingFace.OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
			RetainingFace.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam;
		}
		else if (RetainingFace.Direction == ELayoutFaceDirection::NegX
			|| RetainingFace.Direction == ELayoutFaceDirection::NegY)
		{
			RetainingFace.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceInterior;
		}
	}
	ULayoutModuleAsset* RetainingModule = CreateModule(
		Outer,
		TEXT("TerrainSeamRetainingCorner"),
		Template,
		{ELayoutCellIntent::Interior},
		RetainingFaces);

	TArray<FLayoutFaceRule> IncompleteRetainingFaces = BuildFilledCubeFaces(
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		OpenTags,
		OpenAndSolidTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	FLayoutFaceRule* IncompleteRetainingFace = IncompleteRetainingFaces.FindByPredicate([](const FLayoutFaceRule& Rule)
	{
		return Rule.Direction == ELayoutFaceDirection::PosX;
	});
	if (IncompleteRetainingFace == nullptr)
	{
		AddError(TEXT("Terrain seam corner test could not author incomplete retaining face."));
		return false;
	}
	IncompleteRetainingFace->ConnectionTag = LayoutGameplayTags::FaceSolid;
	IncompleteRetainingFace->AllowedConnectionTags = OpenTags;
	IncompleteRetainingFace->OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	IncompleteRetainingFace->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam;
	for (FLayoutFaceRule& FaceRule : IncompleteRetainingFaces)
	{
		if (FaceRule.Direction == ELayoutFaceDirection::NegX
			|| FaceRule.Direction == ELayoutFaceDirection::PosY
			|| FaceRule.Direction == ELayoutFaceDirection::NegY)
		{
			FaceRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceInterior;
		}
	}
	ULayoutModuleAsset* IncompleteRetainingModule = CreateModule(
		Outer,
		TEXT("TerrainSeamIncompleteRetainingCorner"),
		Template,
		{ELayoutCellIntent::Interior},
		IncompleteRetainingFaces);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TerrainSeamCornerContentSet"),
		{
			BuildModuleEntry(TEXT("TerrainSeamCornerFillEntry"), FillModule, ELayoutPlacementZone::Perimeter),
			BuildModuleEntry(TEXT("TerrainSeamIncompleteRetainingCornerEntry"), IncompleteRetainingModule, ELayoutPlacementZone::Corner),
			BuildModuleEntry(TEXT("TerrainSeamRetainingCornerEntry"), RetainingModule, ELayoutPlacementZone::Corner)
		});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("TerrainSeamCornerProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		819,
		TEXT("TerrainSeam/Corner"));
	Request.FootprintSize = FIntPoint(3, 3);
	Request.PlannedCells = BuildTerrainSeamCells(
		LayoutFaceDirectionMask(ELayoutFaceDirection::PosX)
		| LayoutFaceDirectionMask(ELayoutFaceDirection::PosY));

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}

	const FLayoutPlannedCell* SeamCell = Result.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Terrain seam corner cell survives final topology compilation"), SeamCell);
	if (SeamCell != nullptr)
	{
		TestEqual(TEXT("Terrain seam corner cell remains Interior intent"), SeamCell->Intent, ELayoutCellIntent::Interior);
		TestEqual(TEXT("Two terrain seam faces produce Corner zone"), SeamCell->PlacementZone, ELayoutPlacementZone::Corner);
	}

	const FLayoutPlacedModule* SeamPlacement = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Terrain seam corner cell receives a retaining placement"), SeamPlacement);
	if (SeamPlacement != nullptr)
	{
		TestEqual(TEXT("Terrain seam corner selects retaining content entry"),
			SeamPlacement->SourceContentEntryId,
			FName(TEXT("TerrainSeamRetainingCornerEntry")));
		TestEqual(TEXT("Terrain seam corner yaw covers positive-X and positive-Y faces"), SeamPlacement->YawRotationSteps, 0);
	}

	ULayoutRegionContentSetAsset* IncompleteContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TerrainSeamIncompleteCornerContentSet"),
		{
			BuildModuleEntry(TEXT("TerrainSeamIncompleteCornerFillEntry"), FillModule, ELayoutPlacementZone::Perimeter),
			BuildModuleEntry(TEXT("TerrainSeamIncompleteCornerEntry"), IncompleteRetainingModule, ELayoutPlacementZone::Corner)
		});
	FLayoutRegionSolveRequest IncompleteRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		IncompleteContentSet,
		Profile,
		820,
		TEXT("TerrainSeam/IncompleteCorner"));
	IncompleteRequest.FootprintSize = FIntPoint(3, 3);
	IncompleteRequest.PlannedCells = BuildTerrainSeamCells(
		LayoutFaceDirectionMask(ELayoutFaceDirection::PosX)
		| LayoutFaceDirectionMask(ELayoutFaceDirection::PosY));
	const FLayoutSolveResult IncompleteResult = FLayoutProfileSolver::SolveRegion(IncompleteRequest).SolveResult;
	TestFalse(TEXT("Corner terrain seam rejects retaining content that covers only one required face"), IncompleteResult.bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSeamEnclosureSelectsTraversableDoorTest,
	"PorismExtension.Layout.Solver.TerrainSeam.SelectsTraversableDoor",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSeamEnclosureSelectsTraversableDoorTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateTerrainSeamTestOuter();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TerrainSeamDoorTemplate"), FIntVector(8, 8, 8));
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const FGameplayTagContainer TraversalTags = MakeTags({LayoutGameplayTags::TraversalPrimary});
	const TArray<FLayoutFaceRule> FillFaces = BuildFilledCubeFaces(
		OpenTags,
		OpenTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		OpenTags,
		OpenTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		TraversalTags);
	ULayoutModuleAsset* FillModule = CreateModule(
		Outer,
		TEXT("TerrainSeamDoorFill"),
		Template,
		{ELayoutCellIntent::Boundary},
		FillFaces,
		TraversalTags);

	TArray<FLayoutFaceRule> DoorFaces = FillFaces;
	FLayoutFaceRule* DoorFace = DoorFaces.FindByPredicate([](const FLayoutFaceRule& Rule)
	{
		return Rule.Direction == ELayoutFaceDirection::PosX;
	});
	if (DoorFace == nullptr)
	{
		AddError(TEXT("Terrain seam door test could not author positive-X door face."));
		return false;
	}
	DoorFace->OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor;
	DoorFace->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceTerrainSeam;
	ULayoutModuleAsset* DoorModule = CreateModule(
		Outer,
		TEXT("TerrainSeamDoor"),
		Template,
		{ELayoutCellIntent::Interior, ELayoutCellIntent::Entry},
		DoorFaces,
		TraversalTags);
	TestTrue(TEXT("Traversable terrain seam door validates"), DoorModule->ValidateModule().IsValid());

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TerrainSeamDoorContentSet"),
		{
			BuildModuleEntry(TEXT("TerrainSeamDoorFillEntry"), FillModule, ELayoutPlacementZone::Perimeter),
			BuildModuleEntry(TEXT("TerrainSeamDoorEntry"), DoorModule, ELayoutPlacementZone::Edge)
		});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("TerrainSeamDoorProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->ContentSet = ContentSet;
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		821,
		TEXT("TerrainSeam/Door"));
	Request.FootprintSize = FIntPoint(3, 3);
	Request.PlannedCells = BuildTerrainSeamCells();
	FLayoutRouteConstraintRecord& RouteConstraint = Request.RequiredRouteConstraints.AddDefaulted_GetRef();
	RouteConstraint.ConstraintId = TEXT("TerrainSeamDoor.Route");
	RouteConstraint.Cell = FIntVector(1, 1, 0);
	RouteConstraint.Intent = ELayoutCellIntent::Interior;
	FLayoutRouteFaceRequirement& RouteFace = RouteConstraint.FaceRequirements.AddDefaulted_GetRef();
	RouteFace.FaceDirection = ELayoutFaceDirection::PosX;
	RouteFace.TraversalChannel = LayoutGameplayTags::TraversalPrimary;

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}
	const FLayoutPlannedCell* SeamCell = Result.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Route seam cell remains in the solved plan"), SeamCell);
	if (SeamCell != nullptr)
	{
		TestEqual(TEXT("Route seam cell remains Interior without an upper landing"),
			SeamCell->Intent,
			ELayoutCellIntent::Interior);
	}
	const FLayoutPlacedModule* SeamPlacement = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Terrain seam door fills its seam cell"), SeamPlacement);
	if (SeamPlacement != nullptr)
	{
		TestEqual(TEXT("Terrain seam prefers its dedicated traversable door"),
			SeamPlacement->SourceContentEntryId,
			FName(TEXT("TerrainSeamDoorEntry")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutTerrainSeamEnclosureUsesOrdinaryModulesWhenDisabledTest,
	"PorismExtension.Layout.Solver.TerrainSeam.TerrainSeamsDisabledUsesOrdinaryModules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutTerrainSeamEnclosureUsesOrdinaryModulesWhenDisabledTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateTerrainSeamTestOuter();
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("TerrainSeamDisabledTemplate"), FIntVector(8, 8, 8));
	const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
	const TArray<FLayoutFaceRule> FillFaces = BuildFilledCubeFaces(
		OpenTags,
		OpenTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		OpenTags,
		OpenTags,
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
	ULayoutModuleAsset* FillModule = CreateModule(
		Outer,
		TEXT("TerrainSeamDisabledFill"),
		Template,
		{ELayoutCellIntent::Boundary},
		FillFaces);

	TArray<FLayoutFaceRule> LegacyFaces = FillFaces;
	FLayoutFaceRule* LegacyFace = LegacyFaces.FindByPredicate([](const FLayoutFaceRule& Rule)
	{
		return Rule.Direction == ELayoutFaceDirection::PosX;
	});
	if (LegacyFace == nullptr)
	{
		AddError(TEXT("Terrain seam disabled test could not author positive-X ordinary face."));
		return false;
	}
	LegacyFace->BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceInterior;
	ULayoutModuleAsset* LegacyModule = CreateModule(
		Outer,
		TEXT("TerrainSeamDisabledInterior"),
		Template,
		{ELayoutCellIntent::Interior},
		LegacyFaces);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TerrainSeamDisabledContentSet"),
		{
			BuildModuleEntry(TEXT("TerrainSeamDisabledFillEntry"), FillModule, ELayoutPlacementZone::Perimeter),
			BuildModuleEntry(TEXT("TerrainSeamDisabledInteriorEntry"), LegacyModule, ELayoutPlacementZone::Interior)
		});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("TerrainSeamDisabledProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bEnableTerrainSeams = false;
	Profile->ContentSet = ContentSet;
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ContentSet,
		Profile,
		822,
		TEXT("TerrainSeam/Disabled"));
	Request.FootprintSize = FIntPoint(3, 3);
	Request.PlannedCells = BuildTerrainSeamCells();

	const FLayoutSolveResult Result = FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	if (!Result.bSucceeded)
	{
		AddError(Result.FailureReason);
		return false;
	}
	const FLayoutPlacedModule* SeamPlacement = Result.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Disabled terrain seam cell receives ordinary placement"), SeamPlacement);
	if (SeamPlacement != nullptr)
	{
		TestEqual(TEXT("Disabled terrain seams select ordinary MustFaceInterior content"),
			SeamPlacement->SourceContentEntryId,
			FName(TEXT("TerrainSeamDisabledInteriorEntry")));
	}
	const FLayoutPlannedCell* SeamCell = Result.PlannedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Cell == FIntVector(1, 1, 0);
	});
	TestNotNull(TEXT("Disabled terrain seam cell remains in plan"), SeamCell);
	if (SeamCell != nullptr)
	{
		TestEqual(TEXT("Disabled terrain seam mask is cleared"), SeamCell->TerrainSeamFaceMask, uint8(0));
		TestEqual(TEXT("Disabled terrain seam cell remains ordinary Interior intent"), SeamCell->Intent, ELayoutCellIntent::Interior);
	}
	return true;
}
