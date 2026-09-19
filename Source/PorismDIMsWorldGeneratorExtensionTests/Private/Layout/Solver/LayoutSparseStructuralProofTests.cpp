// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "LayoutSparseStructuralTestFixture.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Layout/Async/LayoutBackgroundSolveCancellation.h"

namespace SparseStructuralProofTests
{
	using namespace LayoutProfileSolverInternal;
	using namespace PorismLayoutTestUtilities;

	/** Small authored parent; full canonical square survives a nonrectangular routing work set. */
	bool PrepareParent(FLayoutRegionSolveRequest& Request, FSolveContext& Context, TArray<FSparseStructuralTraversalPort>& Ports, FString& Failure,
		const bool bTwoChannels = false, const bool bTwoOwners = false, const bool bOneWayEntry = false)
	{
		ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(GetTransientPackage(),
			TEXT("SparseProofParent"), FIntPoint(7, 7), FIntPoint(7, 7), 1, 1, false);
		Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
		Profile->bRequireAllTraversalChannelsReachable = true;
		FLayoutSparsePreserveTerrainRule Rule;
		Rule.RuleId = TEXT("WestTerrain");
		Rule.PlacementZone = ELayoutPlacementZone::Interior;
		Profile->SparsePlacementRules.Add(FInstancedStruct::Make(Rule));
		if (bTwoOwners)
		{
			Rule.RuleId = TEXT("PerimeterTerrain");
			Rule.PlacementZone = ELayoutPlacementZone::Perimeter;
			Profile->SparsePlacementRules.Add(FInstancedStruct::Make(Rule));
		}
		if (bTwoChannels)
		{
			for (FLayoutRegionContentEntry& Entry : Profile->ContentSet->Entries)
			{
				ULayoutModuleAsset* Module = Entry.ModuleSettings.Module;
				for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
					ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
					Module->FaceRules.FindRule(Direction)->ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalSecondary);
				if (bOneWayEntry && Module->GetName().Contains(TEXT("EntryModule")))
				{
					Module->FaceRules.PosX.ConnectedTraversalChannels.RemoveTag(LayoutGameplayTags::TraversalSecondary);
					FLayoutInternalAccessLink Link;
					Link.FromTraversalChannel = LayoutGameplayTags::TraversalPrimary;
					Link.ToTraversalChannel = LayoutGameplayTags::TraversalSecondary;
					Link.bBidirectional = false;
					Module->InternalAccessLinks.Add(Link);
				}
			}
		}
		Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 173, TEXT("SparseProofParent"));
		TSet<FIntVector> Work;
		for (int32 Y = 0; Y < 7; ++Y)
		{
			for (int32 X = 0; X < 7; ++X)
			{
				FLayoutPlannedCell Cell;
				Cell.Cell = FIntVector(X, Y, 0);
				const bool bBoundary = X == 0 || Y == 0 || X == 6 || Y == 6;
				Cell.Intent = bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
				Cell.PlacementZone = bBoundary ? ELayoutPlacementZone::Perimeter : ELayoutPlacementZone::Interior;
				Cell.ModuleLevelIndex = 0;
				if (Cell.Cell == FIntVector(0, 3, 0))
				{
					Cell.Intent = ELayoutCellIntent::Entry;
					Cell.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
				}
				Request.PlannedCells.Add(Cell);
				if (!bBoundary || Cell.Intent == ELayoutCellIntent::Entry || (bTwoOwners && X == 6)) Work.Add(Cell.Cell);
			}
		}
		if (bTwoOwners)
		{
			Work.Remove(FIntVector(2, 2, 0));
			Work.Remove(FIntVector(3, 2, 0));
			Work.Remove(FIntVector(4, 2, 0));
		}
		Request.PrecomputedPlannedCells = Request.PlannedCells;
		Request.bHasQualifiedEntryCells = true;
		Request.QualifiedEntryCells = {FIntVector(0, 3, 0)};
		FLayoutCellCandidateDomainRestriction Gate;
		Gate.Cell = FIntVector(0, 3, 0);
		Gate.RestrictionId = TEXT("Gate");
		for (const FLayoutModuleSolveSnapshot& Snapshot : Request.ModuleCatalog.Modules)
		{
			if (Snapshot.DebugName.ToString().Contains(TEXT("EntryModule"))) Gate.AllowedCandidates.Add({Snapshot.SnapshotId, 2});
		}
		Request.CandidateDomainRestrictions.Add(Gate);
		return TryPrepareSparseStructuralLocalSolveView(Request, Work, Context, Failure)
			&& BuildSparseStructuralTraversalPorts(Context, {}, {}, Ports, Failure);
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralAcceptedChildTest,
	"PorismExtension.Layout.Solver.SparseStructural.AcceptedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralAcceptedChildTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	FLayoutRegionSolveRequest ParentRequest;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(ParentRequest, Parent, Ports, Failure))) { AddError(Failure); return false; }
	const FLayoutRegionSolveRequest Request = SparseStructuralTestFixture::BuildChildRequest(FIntVector(5, 3, 0));
	const FLayoutRegionSolveResult Child = FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Child has an accepted placement proof"), Child.SolveResult.bSucceeded))
	{
		AddError(Child.SolveResult.FailureReason);
		return false;
	}
	FLayoutRegionSolveResult Rejected = Child;
	Rejected.SolveResult.bSucceeded = false;
	TestFalse(TEXT("Rejected child cannot acquire ownership"), TryImportSparseStructuralChildProof(Parent, Request, Rejected, Ports, Failure));
	TestEqual(TEXT("Rejected import leaves ports unchanged"), Ports.Num(), 1);
	TestTrue(TEXT("Rejected import leaves child ownership empty"), Parent.SparseStructuralChildProofs.IsEmpty());
	if (!TestTrue(TEXT("Accepted child imports actual footprint and endpoint"), TryImportSparseStructuralChildProof(Parent, Request, Child, Ports, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestEqual(TEXT("One full accepted child payload retained"), Parent.SparseStructuralChildProofs.Num(), 1);
	TestFalse(TEXT("Child-owned cell is not parent route work"), Parent.PlannedCellIntents.Contains(Request.RegionCellOffset));
	TestTrue(TEXT("Actual child occupancy becomes fixed neighbor"), Parent.FixedNeighborPlacements.Contains(Request.RegionCellOffset));
	TestEqual(TEXT("Port translated to parent ingress"), Ports.Last().Cell, FIntVector(4, 3, 0));
	for (const TPair<FIntVector, TArray<FSolveCandidate>>& Domain : Parent.InitialDomains)
	{
		TestFalse(TEXT("Child-only content cannot enter parent corridor domains"), Domain.Value.ContainsByPredicate(
			[&](const FSolveCandidate& Candidate)
			{ return Parent.ModuleSnapshots.IsValidIndex(Candidate.ModuleSnapshotIndex)
				&& Parent.ModuleSnapshots[Candidate.ModuleSnapshotIndex].DebugName.ToString().Contains(TEXT("ChildOnlyInterior")); }));
	}
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Parent corridor connects accepted child proof"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestEqual(TEXT("Immutable child proof reused, not reconstructed"), Assignment.RetainedChildProofs[0].Get(), Parent.SparseStructuralChildProofs[0].Get());
	TestFalse(TEXT("Parent output does not re-place child"), Solved.Placements.Contains(Request.RegionCellOffset));
	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& Placement : Solved.Placements)
	{
		TestFalse(TEXT("Corridor crop cannot select imported child-only content"), Solved.FixedOnlyModuleSnapshotIds.Contains(Placement.Value.ModuleSnapshotId));
	}
	TestEqual(TEXT("Child result placements remain intact"), Assignment.RetainedChildProofs[0]->SolveResult.Placements.Num(), Child.SolveResult.Placements.Num());
	TestFalse(TEXT("Importing same child twice rejects overlap"), TryImportSparseStructuralChildProof(Parent, Request, Child, Ports, Failure));
	TestEqual(TEXT("Overlap rejection retains original proof only"), Parent.SparseStructuralChildProofs.Num(), 1);
	TArray<FSparseStructuralTraversalPort> Clearance = Ports;
	Clearance[0].RequiredClearanceCells.Add(Request.RegionCellOffset);
	TestFalse(TEXT("Clearance cannot overlap fixed accepted child occupancy"), TrySolveSparseStructuralPortAssignment(Parent, Clearance, Solved, Assignment, Failure));
	TArray<FSparseStructuralTraversalPort> Blocked = Ports;
	Blocked.Last().RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
	TestFalse(TEXT("Child ingress cannot route through child-owned occupancy"), TrySolveSparseStructuralPortAssignment(Parent, Blocked, Solved, Assignment, Failure));
	TestTrue(TEXT("Impossible child route clears parent apply state"), Solved.Placements.IsEmpty());
	TestEqual(TEXT("Rejected route retains accepted child payload in base only"), Parent.SparseStructuralChildProofs.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralChildSupportTest,
	"PorismExtension.Layout.Solver.SparseStructural.ChildSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralChildSupportTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	for (const ELayoutFaceOccupancyPolicy Policy : {ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
		ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor})
	{
		FSolveContext Parent;
		TArray<FSparseStructuralTraversalPort> Ports;
		FString Failure;
		FLayoutRegionSolveRequest ParentRequest;
		if (!TestTrue(TEXT("Parent prepares"), PrepareParent(ParentRequest, Parent, Ports, Failure))) { AddError(Failure); return false; }
		FLayoutRegionSolveRequest Request = SparseStructuralTestFixture::BuildChildRequest(FIntVector(4, 3, 0));
		const FLayoutId ChildModule = Request.CandidateDomainRestrictions[0].AllowedCandidates[0].ModuleSnapshotId;
		for (FLayoutModuleSolveSnapshot& Snapshot : Request.ModuleCatalog.Modules)
		{
			if (Snapshot.SnapshotId == ChildModule) Snapshot.EffectiveFaceRules.PosY.OccupancyPolicy = Policy;
		}
		FLayoutSolveBoundaryPoint SupportOffer;
		SupportOffer.LocalCell = FIntVector(0, 1, 0);
		SupportOffer.FaceDirection = ELayoutFaceDirection::NegY;
		SupportOffer.ConnectionTag = LayoutGameplayTags::FaceOpen;
		SupportOffer.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		SupportOffer.ConnectedTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		SupportOffer.bRequiresBoundaryFacing = false;
		Request.IncomingBoundaryPoints.Add(SupportOffer);
		const FLayoutRegionSolveResult Child = FLayoutProfileSolver::SolveRegion(Request);
		if (!TestTrue(TEXT("Child proves non-egress mandatory support against parent offer"), Child.SolveResult.bSucceeded))
		{ AddError(Child.SolveResult.FailureReason); return false; }
		if (!TestTrue(TEXT("Supported child imports"), TryImportSparseStructuralChildProof(Parent, Request, Child, Ports, Failure)))
		{ AddError(Failure); return false; }
		const FIntVector SupportCell(4, 4, 0);
		TestTrue(TEXT("Child carries non-egress support into selected claims"), Ports.Last().RequiredSupportCells.Contains(SupportCell));
		FSolveContext Solved;
		FSparseStructuralRouteAssignment Assignment;
		if (!TestTrue(TEXT("Corridor crop retains mandatory child support"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
		{ AddError(Failure); return false; }
		TestTrue(TEXT("Non-route support is occupied after cropping"), Solved.Placements.Contains(SupportCell)
			&& FSolveContext::IsOccupiedPlacement(Solved.Placements.FindChecked(SupportCell)));
		TSet<FIntVector> CroppedWork;
		for (const auto& Pair : Solved.Placements) if (Pair.Key != SupportCell) CroppedWork.Add(Pair.Key);
		FSolveContext Cropped;
		if (!TestTrue(TEXT("Independent completed-map audit crop prepares"),
			TryBuildSparseStructuralLocalSolveView(Solved, CroppedWork, Cropped, Failure))) { AddError(Failure); return false; }
		Cropped.Placements.Remove(SupportCell);
		TestFalse(TEXT("Final child-face audit catches missing support outside parent work"), ValidateSolvedPlacementAdjacencyForTests(Cropped, Failure));
		TestTrue(TEXT("Rejection identifies child interface, not planned-cell count"), Failure.Contains(TEXT("Sparse child")));
		FLayoutCellCandidateDomainRestriction Impossible;
		Impossible.Cell = SupportCell;
		Impossible.RestrictionId = TEXT("MissingChildSupport");
		Impossible.AllowedCandidates.Add({TEXT("AbsentSupportModule"), 0});
		Parent.CandidateDomainRestrictionsByCell.Add(SupportCell, Impossible);
		Parent.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
		TestFalse(TEXT("Optional routing cannot drop impossible mandatory child support"),
			TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure));
		TestTrue(TEXT("Missing support rejects without apply state"), Solved.Placements.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralChildDirectGateTest,
	"PorismExtension.Layout.Solver.SparseStructural.ChildDirectGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralChildDirectGateTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	FLayoutRegionSolveRequest ParentRequest;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(ParentRequest, Parent, Ports, Failure))) { AddError(Failure); return false; }
	const FLayoutRegionSolveRequest Request = SparseStructuralTestFixture::BuildChildRequest(FIntVector(1, 3, 0));
	const FLayoutRegionSolveResult Child = FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Direct child proof solves"), Child.SolveResult.bSucceeded)) { AddError(Child.SolveResult.FailureReason); return false; }
	if (!TestTrue(TEXT("Child ingress may touch selected gate without another corridor exit"),
		TryImportSparseStructuralChildProof(Parent, Request, Child, Ports, Failure))) { AddError(Failure); return false; }
	TestEqual(TEXT("Child ingress shares gate cell, not gate ownership"), Ports.Last().Cell, Ports[0].Cell);
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Direct gate-child contact needs no duplicate occupied claim"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure))) { AddError(Failure); return false; }
	TestEqual(TEXT("Only gate is parent-owned"), Solved.Placements.Num(), 1);
	TestTrue(TEXT("Direct contact needs no corridor path"), Assignment.Paths.IsEmpty());
	TestEqual(TEXT("Child proof retained"), Assignment.RetainedChildProofs.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralOptionalComponentsTest,
	"PorismExtension.Layout.Solver.SparseStructural.OptionalComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralOptionalComponentsTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	FLayoutRegionSolveRequest ParentRequest;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(ParentRequest, Parent, Ports, Failure))) { AddError(Failure); return false; }
	FSparseStructuralTraversalPort Endpoint;
	Endpoint.ProofId = TEXT("IsolatedStructure");
	Endpoint.Cell = FIntVector(5, 3, 0);
	Endpoint.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	Endpoint.RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
	Ports.Add(Endpoint);
	for (auto It = Parent.PlannedCellIntents.CreateIterator(); It; ++It)
	{
		if (It.Key().X == 3) It.RemoveCurrent();
	}
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	TestFalse(TEXT("Required disconnected components reject"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure));
	Parent.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	Parent.Result.FailureReason.Reset();
	if (!TestTrue(TEXT("Optional disconnected structures survive without partial corridor"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("Both hard structures are placed"), Solved.Placements.Contains(Ports[0].Cell) && Solved.Placements.Contains(Endpoint.Cell));
	TestTrue(TEXT("Abandoned route has no path claims"), Assignment.Paths.IsEmpty());
	TestEqual(TEXT("No background corridor placement"), Solved.Placements.Num(), 2);

	// The optional failure must also be nonfatal when A* succeeds but reciprocal module tags fail CSP.
	const FLayoutModuleSolveSnapshot* Generic = ParentRequest.ModuleCatalog.Modules.FindByPredicate(
		[](const FLayoutModuleSolveSnapshot& Snapshot) { return Snapshot.DebugName.ToString().Contains(TEXT("GenericModule")); });
	if (!TestNotNull(TEXT("Optional corridor source exists"), Generic)) return false;
	FLayoutModuleSolveSnapshot Closed = *Generic;
	Closed.SnapshotId = TEXT("OptionalClosedCorridor");
	for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX,
		ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
	{
		FLayoutFaceRule* Face = Closed.EffectiveFaceRules.FindRule(Direction);
		Face->ConnectionTag = LayoutGameplayTags::FaceSolid;
		Face->AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceSolid});
	}
	ParentRequest.ModuleCatalog.Modules.Add(Closed);
	FLayoutCellCandidateDomainRestriction Restriction;
	Restriction.Cell = FIntVector(1, 3, 0);
	Restriction.RestrictionId = TEXT("OptionalClosed.Exact");
	Restriction.AllowedCandidates.Add({Closed.SnapshotId, 0});
	ParentRequest.CandidateDomainRestrictions.Add(Restriction);
	ParentRequest.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	const TSet<FIntVector> ShortWork{FIntVector(0, 3, 0), FIntVector(1, 3, 0), FIntVector(2, 3, 0)};
	if (!TestTrue(TEXT("Optional unsatisfiable corridor prepares"), TryPrepareSparseStructuralLocalSolveView(ParentRequest, ShortWork, Parent, Failure)
		&& BuildSparseStructuralTraversalPorts(Parent, {}, {}, Ports, Failure))) { AddError(Failure); return false; }
	Endpoint.Cell = FIntVector(2, 3, 0);
	Ports.Add(Endpoint);
	if (!TestTrue(TEXT("Optional corridor CSP failure retains hard structures"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("Failed optional corridor attempted before abandonment"), Assignment.AttemptCount > 1);
	TestTrue(TEXT("Rejected optional corridor leaves no accepted paths"), Assignment.Paths.IsEmpty());
	TestFalse(TEXT("Rejected optional corridor leaves no placement"), Solved.Placements.Contains(Restriction.Cell));
	TestEqual(TEXT("Hard endpoint count survives optional CSP rejection"), Solved.Placements.Num(), 2);

	TSet<FIntVector> MixedWork = ShortWork;
	for (int32 Y = 2; Y <= 4; ++Y) MixedWork.Add(FIntVector(4, Y, 0));
	if (!TestTrue(TEXT("Mixed legal and rejected corridors prepare"),
		TryPrepareSparseStructuralLocalSolveView(ParentRequest, MixedWork, Parent, Failure)
		&& BuildSparseStructuralTraversalPorts(Parent, {}, {}, Ports, Failure))) return false;
	Ports.Add(Endpoint);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FSparseStructuralTraversalPort Island;
		Island.ProofId = Index == 0 ? TEXT("Mixed.North") : TEXT("Mixed.South");
		Island.Cell = FIntVector(4, 2 + Index * 2, 0);
		Island.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
		Island.RouteFaceMask = LayoutFaceDirectionMask(Index == 0 ? ELayoutFaceDirection::PosY : ELayoutFaceDirection::NegY);
		Ports.Add(Island);
	}
	LayoutSolveExecution::FScope ReuseScope(0.0, 50000);
	const auto* ReuseLedger = LayoutSolveExecution::CurrentThreadLedger();
	const uint64 VariantBuildsBefore = ReuseLedger->VariantBuilds;
	const uint64 DomainBuildsBefore = ReuseLedger->DomainBuilds;
	if (!TestTrue(TEXT("Failed extension preserves legal subset"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("Legal subset corridor survives another corridor CSP failure"), Solved.Placements.Contains(FIntVector(4, 3, 0)));
	TestFalse(TEXT("Rejected extension remains empty"), Solved.Placements.Contains(Restriction.Cell));
	TestEqual(TEXT("Corridor alternatives reuse unchanged prepared catalog variants"), ReuseLedger->VariantBuilds, VariantBuildsBefore);
	TestTrue(TEXT("Changed corridor work still rebuilds topology-dependent domains"), ReuseLedger->DomainBuilds > DomainBuildsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralProductionProjectionTest,
	"PorismExtension.Layout.Solver.SparseStructural.ProductionProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Direct request solving must enter sparse assembly, while non-preserving requests keep normal fill. */
bool FSparseStructuralProductionProjectionTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Prepared;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Prepared, Ports, Failure))) return false;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	for (const bool bPreserve : {true, false})
	{
		FLayoutRegionSolveRequest Trial = Request;
		if (!bPreserve) Trial.ProfileSnapshot.SparsePlacementRules.Reset();
		LayoutSolveExecution::FScope Scope(0.0, 50000);
		const auto* Ledger = LayoutSolveExecution::CurrentThreadLedger();
		const uint64 SparseBefore = Ledger->SparseWork;
		FLayoutRegionSolveRequest FinalAuthority;
		const auto Result = SolveRequestBackedRegion(Trial, true, &FinalAuthority);
		if (!TestTrue(TEXT("Direct production solve succeeds"), Result.SolveResult.bSucceeded))
		{
			AddError(Result.SolveResult.FailureReason);
			return false;
		}
		TestEqual(TEXT("Only preserving request enters sparse assembly"), Ledger->SparseWork > SparseBefore, bPreserve);
		TestEqual(TEXT("Published plan retains canonical topology"), Result.SolveResult.PlannedCells.Num(), 49);
		TestEqual(TEXT("Published occupancy follows typed preservation"), Result.SolveResult.Placements.Num(), bPreserve ? 24 : 49);
		TestTrue(TEXT("Original exact gate restriction survives"), FinalAuthority.CandidateDomainRestrictions.ContainsByPredicate(
			[](const auto& Restriction) { return Restriction.RestrictionId == TEXT("Gate") && Restriction.AllowedCandidates.Num() == 1; }));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralJointNormalProjectionTest,
	"PorismExtension.Layout.Solver.SparseStructural.JointNormalProjection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A sparse assignment must also prove every remaining normal cell supplied by its owner. */
bool FSparseStructuralJointNormalProjectionTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Parent, Ports, Failure))) return false;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	TSet<FIntVector> Work;
	for (const auto& Cell : Request.PlannedCells) Work.Add(Cell.Cell);
	if (!TestTrue(TEXT("Mixed normal/preserved view prepares"),
		TryPrepareSparseStructuralLocalSolveView(Request, Work, Parent, Failure)
		&& BuildSparseStructuralTraversalPorts(Parent, {}, {}, Ports, Failure)))
	{
		AddError(Failure);
		return false;
	}
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Joint normal projection solves"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	for (const auto& Cell : Request.PlannedCells)
	{
		const bool bNormal = Cell.PlacementZone == ELayoutPlacementZone::Perimeter;
		TestEqual(TEXT("Normal perimeter retained; unused sparse interior absent"), Solved.Placements.Contains(Cell.Cell), bNormal);
	}
	TestEqual(TEXT("Full normal shell proves with the selected Entry"), Solved.Placements.Num(), 24);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralOptionalSupportChainTest,
	"PorismExtension.Layout.Solver.SparseStructural.OptionalSupportChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Mandatory walkable support can form a legal partial path without reaching another structure. */
bool FSparseStructuralOptionalSupportChainTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Parent, Ports, Failure))) return false;
	const FLayoutId GateId = Request.CandidateDomainRestrictions[0].AllowedCandidates[0].ModuleSnapshotId;
	FLayoutModuleSolveSnapshot* Gate = Request.ModuleCatalog.Modules.FindByPredicate(
		[GateId](const auto& Module) { return Module.SnapshotId == GateId; });
	const FLayoutModuleSolveSnapshot* Generic = Request.ModuleCatalog.Modules.FindByPredicate(
		[](const auto& Module) { return Module.DebugName.ToString().Contains(TEXT("GenericModule")); });
	if (!TestNotNull(TEXT("Exact gate exists"), Gate) || !TestNotNull(TEXT("Support source exists"), Generic)) return false;
	// Selected gate yaw=2 turns authored -X toward the interior (+X).
	Gate->EffectiveFaceRules.NegX.OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
	const FLayoutId CapId = Generic->SnapshotId;
	FLayoutModuleSolveSnapshot Support = *Generic;
	Support.SnapshotId = TEXT("PartialChain.Support");
	Support.EffectiveFaceRules.PosX.OccupancyPolicy = ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor;
	Request.ModuleCatalog.Modules.Add(Support);
	for (int32 X = 1; X <= 2; ++X)
	{
		FLayoutCellCandidateDomainRestriction Exact;
		Exact.Cell = FIntVector(X, 3, 0);
		Exact.RestrictionId = FLayoutId(*FString::Printf(TEXT("PartialChain.%d"), X));
		Exact.AllowedCandidates.Add({X == 1 ? Support.SnapshotId : CapId, 0});
		Request.CandidateDomainRestrictions.Add(Exact);
	}
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	TSet<FIntVector> Work;
	for (int32 X = 0; X <= 5; ++X) Work.Add(FIntVector(X, 3, 0));
	if (!TestTrue(TEXT("Selected support chain prepares"),
		TryPrepareSparseStructuralLocalSolveView(Request, Work, Parent, Failure)
		&& BuildSparseStructuralTraversalPorts(Parent, {}, {}, Ports, Failure)))
	{
		AddError(Failure);
		return false;
	}
	Parent.PlannedCellIntents.Remove(FIntVector(3, 3, 0));
	FSparseStructuralTraversalPort Target;
	Target.ProofId = TEXT("PartialChain.IsolatedTarget");
	Target.Cell = FIntVector(5, 3, 0);
	Target.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	Target.RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
	Ports.Add(Target);
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Optional structural proof keeps mandatory partial support chain"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	for (int32 X : {0, 1, 2, 5}) TestTrue(TEXT("Required structural/support cell survives"), Solved.Placements.Contains(FIntVector(X, 3, 0)));
	TestEqual(TEXT("Only structural cells and mandatory partial path remain"), Solved.Placements.Num(), 4);
	TestTrue(TEXT("No completed target route claimed"), Assignment.Paths.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralOptionalPortlessClaimTest,
	"PorismExtension.Layout.Solver.SparseStructural.OptionalPortlessClaim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Cropped routing space may offer no external corridor without erasing an exact structural claim. */
bool FSparseStructuralOptionalPortlessClaimTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Parent, Ports, Failure))) return false;
	const FIntVector Gate = Ports[0].Cell;
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	if (!TestTrue(TEXT("Structural-only work set prepares"),
		TryPrepareSparseStructuralLocalSolveView(Request, {Gate}, Parent, Failure)))
	{
		AddError(Failure);
		return false;
	}
	if (!TestTrue(TEXT("Optional exact claim survives missing route offers"),
		BuildSparseStructuralTraversalPorts(Parent, {}, {}, Ports, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestEqual(TEXT("One selected structural claim retained"), Ports.Num(), 1);
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Portless structure passes module proof"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("Exact gate survives"), Solved.Placements.Contains(Gate));
	TestTrue(TEXT("No corridor invented"), Assignment.Paths.IsEmpty());
	TestEqual(TEXT("No background fill"), Solved.Placements.Num(), 1);
	if (Ports.Num() == 1)
	{
		Ports[0].EndpointCommitment.Emplace();
		TestFalse(TEXT("Portless marker cannot relax an explicit endpoint obligation"),
			TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure));
		TestTrue(TEXT("Rejected endpoint grants no placements"), Solved.Placements.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralOptionalDisconnectedSubsetTest,
	"PorismExtension.Layout.Solver.SparseStructural.OptionalDisconnectedSubset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** A blocked Entry component must not prevent useful connections between other legal structures. */
bool FSparseStructuralOptionalDisconnectedSubsetTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Parent, Ports, Failure)))
	{
		AddError(Failure);
		return false;
	}
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FSparseStructuralTraversalPort Port;
		Port.ProofId = Index == 0 ? TEXT("Island.North") : TEXT("Island.South");
		Port.Cell = FIntVector(4, 2 + Index * 2, 0);
		Port.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
		Port.RouteFaceMask = LayoutFaceDirectionMask(Index == 0 ? ELayoutFaceDirection::PosY : ELayoutFaceDirection::NegY);
		Ports.Add(Port);
	}
	for (auto It = Parent.PlannedCellIntents.CreateIterator(); It; ++It)
	{
		if (It.Key().X == 3) It.RemoveCurrent();
	}
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	TestFalse(TEXT("Required network rejects disconnected Entry"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure));
	Parent.ProfileSnapshot.bRequireAllTraversalChannelsReachable = false;
	Parent.Result.FailureReason.Reset();
	if (!TestTrue(TEXT("Optional network retains disconnected structures"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("Optional search connects the non-Entry subset"),
		Assignment.Paths.ContainsByPredicate([](const TArray<FIntVector>& Path) { return Path.Contains(FIntVector(4, 3, 0)); }));
	TestTrue(TEXT("Subset corridor survives placed-module proof"), Solved.Placements.Contains(FIntVector(4, 3, 0)));
	for (const auto& Port : Ports) TestTrue(TEXT("Each hard structure survives"), Solved.Placements.Contains(Port.Cell));
	FLayoutCommittedTraversalAnchor& Anchor = Parent.CommittedTraversalAnchors.AddDefaulted_GetRef();
	Anchor.Cell = Ports.Last().Cell;
	Anchor.TraversalChannel = Ports.Last().TraversalChannel;
	TestFalse(TEXT("Optional traversal cannot disconnect an explicit committed anchor from Entry"),
		TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure));
	TestTrue(TEXT("Rejected committed route publishes no placements"), Solved.Placements.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralChannelsAndOwnersTest,
	"PorismExtension.Layout.Solver.SparseStructural.ChannelsAndOwners",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralChannelsAndOwnersTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Two channel and sparse-owner fixture prepares"), PrepareParent(Request, Parent, Ports, Failure, true, true)))
	{
		AddError(Failure);
		return false;
	}
	TestEqual(TEXT("Gate exports separate channels"), Ports.Num(), 2);
	const FGameplayTag Channels[] = {LayoutGameplayTags::TraversalPrimary, LayoutGameplayTags::TraversalSecondary};
	for (const FGameplayTag& Channel : Channels)
	{
		FSparseStructuralTraversalPort Endpoint;
		Endpoint.ProofId = TEXT("EastStructure");
		Endpoint.Cell = FIntVector(6, 3, 0);
		Endpoint.TraversalChannel = Channel;
		Endpoint.RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
		Ports.Add(Endpoint);
	}
	TestEqual(TEXT("Interior keeps original rule owner"), Parent.OwningTerrainResidualRuleIdByCell.FindRef(FIntVector(3, 3, 0)), FLayoutId(TEXT("WestTerrain")));
	TestEqual(TEXT("Perimeter keeps distinct original rule owner"), Parent.OwningTerrainResidualRuleIdByCell.FindRef(FIntVector(6, 3, 0)), FLayoutId(TEXT("PerimeterTerrain")));
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Independent channel routes share a legal corridor across sparse owners"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestEqual(TEXT("Each disconnected channel gets its own proven route"), Assignment.Paths.Num(), 2);
	for (const FGameplayTag& Channel : Channels)
	{
		TestTrue(TEXT("Both placed channels reach far structure"), BuildReachableWalkableNodesFromPlacedRoots(Solved).Contains({FIntVector(6, 3, 0), Channel}));
	}
	TestFalse(TEXT("Nonrectangular omitted cells remain unclaimed"), Solved.Placements.Contains(FIntVector(3, 2, 0)));
	TestTrue(TEXT("Omitted routing cell stays canonical interior"), Solved.OwningTopologyCellsByPhysicalCell.Contains(FIntVector(3, 2, 0)));
	TestEqual(TEXT("Crop retains rule ownership for later residual publication"), Solved.OwningTerrainResidualRuleIdByCell.FindRef(FIntVector(6, 3, 0)), FLayoutId(TEXT("PerimeterTerrain")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralCompetingApproachesTest,
	"PorismExtension.Layout.Solver.SparseStructural.CompetingApproaches",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralCompetingApproachesTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Parent, Ports, Failure))) { AddError(Failure); return false; }
	const FLayoutModuleSolveSnapshot* Generic = Request.ModuleCatalog.Modules.FindByPredicate(
		[](const FLayoutModuleSolveSnapshot& Snapshot) { return Snapshot.DebugName.ToString().Contains(TEXT("GenericModule")); });
	if (!TestNotNull(TEXT("Generic structural module exists"), Generic)) return false;
	FLayoutModuleSolveSnapshot Obstacle = *Generic;
	Obstacle.SnapshotId = TEXT("NorthApproachOnly");
	Obstacle.DebugName = TEXT("NorthApproachOnly");
	for (const ELayoutFaceDirection Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY})
		Obstacle.EffectiveFaceRules.FindRule(Direction)->ConnectedTraversalChannels.Reset();
	for (FLayoutLocalCellFaceRuleSnapshot& Local : Obstacle.GeneratedLocalCellFaceRules)
	{
		for (FLayoutFaceRule& Face : Local.ExposedFaceRules)
			if (Face.Direction != ELayoutFaceDirection::NegY) Face.ConnectedTraversalChannels.Reset();
	}
	Request.ModuleCatalog.Modules.Add(Obstacle);
	FLayoutCellCandidateDomainRestriction Restriction;
	Restriction.Cell = FIntVector(3, 3, 0);
	Restriction.RestrictionId = TEXT("NorthOnly.Exact");
	Restriction.AllowedCandidates.Add({Obstacle.SnapshotId, 0});
	Request.CandidateDomainRestrictions.Add(Restriction);
	TSet<FIntVector> Work;
	for (const TPair<FIntVector, ELayoutCellIntent>& Pair : Parent.PlannedCellIntents) Work.Add(Pair.Key);
	if (!TestTrue(TEXT("Selected approach blocker prepares"), TryPrepareSparseStructuralLocalSolveView(Request, Work, Parent, Failure)
		&& BuildSparseStructuralTraversalPorts(Parent, {}, {}, Ports, Failure))) { AddError(Failure); return false; }
	FSparseStructuralTraversalPort Blocker;
	Blocker.ProofId = Restriction.RestrictionId;
	Blocker.Cell = Restriction.Cell;
	Blocker.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	Blocker.RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegY);
	Ports.Add(Blocker);
	FSparseStructuralTraversalPort Target = Blocker;
	Target.ProofId = TEXT("EastTarget");
	Target.Cell = FIntVector(5, 3, 0);
	Target.RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
	Ports.Add(Target);
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Both selected approaches connect through a legal detour"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestEqual(TEXT("Blocking structure remains its exact selected module"), Solved.Placements.FindChecked(Blocker.Cell).ModuleSnapshotId, Obstacle.SnapshotId);
	TestTrue(TEXT("Detour retains required north approach"), Solved.Placements.Contains(Blocker.Cell + FIntVector(0, -1, 0)));
	TestTrue(TEXT("Target reaches placed graph without consuming blocker exit"), BuildReachableWalkableNodesFromPlacedRoots(Solved).Contains({Target.Cell, Target.TraversalChannel}));
	Ports[1].RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosY);
	TestFalse(TEXT("No matching selected approach rejects atomically"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure));
	TestTrue(TEXT("Rejected competing approach publishes no placements"), Solved.Placements.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralDirectedChannelsTest,
	"PorismExtension.Layout.Solver.SparseStructural.DirectedChannels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralDirectedChannelsTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("One-way gate channel conversion prepares"), PrepareParent(Request, Parent, Ports, Failure, true, false, true)))
	{
		AddError(Failure);
		return false;
	}
	for (const FSparseStructuralTraversalPort& Port : Ports)
	{
		if (Port.TraversalChannel == LayoutGameplayTags::TraversalPrimary)
		{
			TestTrue(TEXT("Primary port has directed access to Secondary"), Port.InternallyReachableNodes.Contains({Port.Cell, LayoutGameplayTags::TraversalSecondary}));
		}
		else
		{
			TestFalse(TEXT("Secondary cannot invent reverse access"), Port.InternallyReachableNodes.Contains({Port.Cell, LayoutGameplayTags::TraversalPrimary}));
			TestFalse(TEXT("Inward Secondary face is not an exterior Entry root"), Port.bIsEntryRoot);
		}
	}
	FSparseStructuralTraversalPort Endpoint;
	Endpoint.ProofId = TEXT("SecondaryStructure");
	Endpoint.Cell = FIntVector(5, 3, 0);
	Endpoint.TraversalChannel = LayoutGameplayTags::TraversalSecondary;
	Endpoint.RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
	Ports.Add(Endpoint);
	FSolveContext Solved;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Primary Entry reaches Secondary corridor via authored one-way link"), TrySolveSparseStructuralPortAssignment(Parent, Ports, Solved, Assignment, Failure)))
	{
		AddError(Failure);
		return false;
	}
	TestTrue(TEXT("Final placed graph proves channel conversion"), BuildReachableWalkableNodesFromPlacedRoots(Solved).Contains({Endpoint.Cell, Endpoint.TraversalChannel}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralInheritedLedgerTest,
	"PorismExtension.Layout.Solver.SparseStructural.InheritedLedger",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Copied local contexts cannot reset the enclosing invocation's work allowance or deadline. */
bool FSparseStructuralInheritedLedgerTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Prepared;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares before inherited-budget control"), PrepareParent(Request, Prepared, Ports, Failure)))
	{
		AddError(Failure);
		return false;
	}
	for (const bool bExpired : {false, true})
	{
		LayoutSolveExecution::FScope Scope(0.0, 2);
		auto* Work = LayoutSolveExecution::CurrentThreadLedger();
		if (bExpired) Work->DeadlineSeconds = FPlatformTime::Seconds() - 1.0;
		FSolveContext Trial = Prepared;
		TArray<FSparseStructuralTraversalPort> TrialPorts = Ports;
		TestFalse(TEXT("Port preparation honors enclosing ledger, not copied local allowance"),
			BuildSparseStructuralTraversalPorts(Trial, {}, {}, TrialPorts, Failure));
		TestTrue(TEXT("Interrupted port preparation publishes no partial ports"), TrialPorts.IsEmpty());
		TestTrue(TEXT("Inherited stop reason survives local preparation"),
			Failure.Contains(bExpired ? TEXT("deadline") : TEXT("work budget")));
		TestEqual(TEXT("Rejected sparse work remains charged to caller"), Work->UsedWorkUnits, bExpired ? uint64(0) : uint64(2));

		Trial = Prepared;
		FSolveContext Solved;
		FSparseStructuralRouteAssignment Assignment;
		TestFalse(TEXT("A fresh local copy cannot resume after inherited stop"),
			TrySolveSparseStructuralPortAssignment(Trial, Ports, Solved, Assignment, Failure));
		TestEqual(TEXT("Stopped invocation starts no corridor attempts"), Assignment.AttemptCount, 0);
		TestTrue(TEXT("Stopped invocation returns no placement authority"), Solved.Placements.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSparseStructuralPreparationCancellationTest,
	"PorismExtension.Layout.Solver.SparseStructural.PreparationCancellation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FSparseStructuralPreparationCancellationTest::RunTest(const FString& Parameters)
{
	using namespace SparseStructuralProofTests;
	FLayoutRegionSolveRequest Request;
	FSolveContext Parent;
	TArray<FSparseStructuralTraversalPort> Ports;
	FString Failure;
	if (!TestTrue(TEXT("Parent prepares"), PrepareParent(Request, Parent, Ports, Failure))) { AddError(Failure); return false; }
	const FLayoutRegionSolveRequest ChildRequest = SparseStructuralTestFixture::BuildChildRequest(FIntVector(5, 3, 0));
	const FLayoutRegionSolveResult Child = FLayoutProfileSolver::SolveRegion(ChildRequest);
	if (!TestTrue(TEXT("Child solves before import"), Child.SolveResult.bSucceeded)) return false;
	FSolveContext Exhausted = Parent;
	Exhausted.MaxCandidateAttempts = Exhausted.CandidateAttemptCount + 2;
	TestFalse(TEXT("Child preparation exhaustion rolls back partially imported metadata"), TryImportSparseStructuralChildProof(Exhausted, ChildRequest, Child, Ports, Failure));
	TestEqual(TEXT("Failed preparation remains charged"), Exhausted.CandidateAttemptCount, Exhausted.MaxCandidateAttempts);
	TestTrue(TEXT("Failed import leaks no child reservations"), Exhausted.ChildReservationCells.IsEmpty());
	TestTrue(TEXT("Failed import leaks no fixed occupancy"), Exhausted.FixedNeighborPlacements.IsEmpty());
	FLayoutSolveCancellationSource Source;
	const FLayoutSolveCancellationToken Token = Source.CreateToken();
	Source.Cancel();
	LayoutSolveCancellation::FThreadTokenScope Scope(Token);
	FSolveContext Canceled = Parent;
	TArray<FSparseStructuralTraversalPort> OutputPorts = Ports;
	TestFalse(TEXT("Cancellation stops port preparation"), BuildSparseStructuralTraversalPorts(Canceled, {}, {}, OutputPorts, Failure));
	TestTrue(TEXT("Canceled builder clears stale output"), OutputPorts.IsEmpty());
	Canceled = Parent;
	TestFalse(TEXT("Cancellation stops child import"), TryImportSparseStructuralChildProof(Canceled, ChildRequest, Child, Ports, Failure));
	TestTrue(TEXT("Canceled import leaves accepted child list empty"), Canceled.SparseStructuralChildProofs.IsEmpty());
	Canceled = Parent;
	FSolveContext Solved = Parent;
	FSparseStructuralRouteAssignment Assignment;
	TestFalse(TEXT("Cancellation stops corridor assignment"), TrySolveSparseStructuralPortAssignment(Canceled, Ports, Solved, Assignment, Failure));
	TestEqual(TEXT("Cancellation starts no attempt"), Assignment.AttemptCount, 0);
	TestTrue(TEXT("Cancellation clears stale placement output"), Solved.Placements.IsEmpty());
	return true;
}
