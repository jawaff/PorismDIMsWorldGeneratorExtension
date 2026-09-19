// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "Layout/Support/LayoutTestUtilities.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;
	using namespace PorismLayoutTestUtilities;

	FLayoutCommittedEndpointAnchor MakeEndpointAnchor(
		const FLayoutId CommitmentId,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection)
	{
		FLayoutCommittedEndpointAnchor Anchor;
		Anchor.CommitmentId = CommitmentId;
		Anchor.LocalCell = LocalCell;
		Anchor.FaceDirection = FaceDirection;
		Anchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
		Anchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		return Anchor;
	}

	FLayoutCommittedTraversalAnchor MakeTraversalAnchor(const FIntVector& Cell)
	{
		FLayoutCommittedTraversalAnchor Anchor;
		Anchor.Cell = Cell;
		Anchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
		return Anchor;
	}

	FLayoutPlannedCell MakePlannedCell(
		const FIntVector& Cell,
		const ELayoutCellIntent Intent)
	{
		FLayoutPlannedCell PlannedCell;
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = Intent;
		return PlannedCell;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteResidualParentDropsUnsupportedUpperCellsTest,
	"PorismExtension.Layout.Solver.Rewrite.ResidualParentDropsUnsupportedUpperCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteResidualParentTracksChildBundleSupportLegalityTest,
	"PorismExtension.Layout.Solver.Rewrite.ResidualParentTracksChildBundleSupportLegality",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteResidualParentClassifiesForwardedChildSupportProvenanceTest,
	"PorismExtension.Layout.Solver.Rewrite.ResidualParentClassifiesForwardedChildSupportProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteResidualParentClassifiesSameRegionSupportProvenanceTest,
	"PorismExtension.Layout.Solver.Rewrite.ResidualParentClassifiesSameRegionSupportProvenance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteResidualParentSummarizesReachabilityAndBoundaryStateTest,
	"PorismExtension.Layout.Solver.Rewrite.ResidualParentSummarizesReachabilityAndBoundaryState",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteResidualParentSeparatesDisconnectedRootComponentsTest,
	"PorismExtension.Layout.Solver.Rewrite.ResidualParentSeparatesDisconnectedRootComponents",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRecursiveScheduleRewriteResidualParentDropsUnsupportedUpperCellsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::VerticalAccess)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/Child");

	const FResidualParentCapabilitySummary Summary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(RootRequest), DemandPlan);

	TestEqual(TEXT("Residual parent removes unsupported upper cells when no same-region or forwarded child support exists"), Summary.PlannedCells.Num(), 1);
	TestTrue(TEXT("Residual parent keeps the surviving entry cell"), Summary.PlannedCells.ContainsByPredicate(
		[](const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Cell == FIntVector(0, 0, 0);
		}));
	TestFalse(TEXT("Residual parent drops the unsupported upper cell"), Summary.PlannedCells.ContainsByPredicate(
		[](const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Cell == FIntVector(1, 0, 1);
		}));
	TestFalse(TEXT("Unsupported upper cells are not preserved in support provenance"), Summary.SupportProvenanceByCell.Contains(FIntVector(1, 0, 1)));
	TestTrue(TEXT("Residual root-connected traversal keeps the surviving entry cell"), Summary.RootConnectedTraversableCells.Contains(FIntVector(0, 0, 0)));
	TestEqual(TEXT("Unsupported vertical-access cells are not counted as surviving parent providers"), Summary.CountedParentVerticalAccessCells.Num(), 0);
	return true;
}



bool FLayoutRecursiveScheduleRewriteResidualParentTracksChildBundleSupportLegalityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest SupportedRootRequest;
	SupportedRootRequest.RegionDebugPath = TEXT("Root");
	SupportedRootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !SupportedRootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	SupportedRootRequest.FootprintSize = FIntPoint(2, 1);
	SupportedRootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::VerticalAccess)
	};

	FNegotiationDemandPlan UpperOnlyDemandPlan;
	UpperOnlyDemandPlan.ChildRegionDebugPath = TEXT("Root/UpperOnlyChild");
	UpperOnlyDemandPlan.ReservedParentCells = {
		FIntVector(1, 0, 1)
	};

	const FResidualParentCapabilitySummary UpperOnlySummary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(SupportedRootRequest), UpperOnlyDemandPlan);
	TestEqual(TEXT("Residual parent records one required parent support cell beneath an upper-only reserved child footprint"), UpperOnlySummary.RequiredChildBundleSupportCells.Num(), 1);
	TestEqual(TEXT("Residual parent records the exact parent support cell required beneath the upper-only reserved child footprint"), UpperOnlySummary.RequiredChildBundleSupportCells[0], FIntVector(1, 0, 0));
	TestEqual(TEXT("Residual parent records no missing support cells when the parent still supports the reserved upper child footprint"), UpperOnlySummary.MissingRequiredChildBundleSupportCells.Num(), 0);

	FNegotiationDemandPlan StackedDemandPlan;
	StackedDemandPlan.ChildRegionDebugPath = TEXT("Root/StackedChild");
	StackedDemandPlan.ReservedParentCells = {
		FIntVector(1, 0, 0),
		FIntVector(1, 0, 1)
	};

	const FResidualParentCapabilitySummary StackedSummary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(SupportedRootRequest), StackedDemandPlan);
	TestEqual(TEXT("Residual parent records no external parent support requirement when the reserved child footprint self-supports through a stacked lower occupied cell"), StackedSummary.RequiredChildBundleSupportCells.Num(), 0);
	TestEqual(TEXT("Residual parent records no missing support cells when the stacked reserved child footprint self-supports"), StackedSummary.MissingRequiredChildBundleSupportCells.Num(), 0);

	FLayoutRegionSolveRequest UnsupportedRootRequest;
	UnsupportedRootRequest.RegionDebugPath = TEXT("Root");
	UnsupportedRootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !UnsupportedRootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	UnsupportedRootRequest.FootprintSize = FIntPoint(2, 1);
	UnsupportedRootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::VerticalAccess)
	};

	const FResidualParentCapabilitySummary UnsupportedSummary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(UnsupportedRootRequest), UpperOnlyDemandPlan);
	TestEqual(TEXT("Residual parent still records the required parent support cell beneath the upper-only reserved child footprint when the parent cannot satisfy it"), UnsupportedSummary.RequiredChildBundleSupportCells.Num(), 1);
	TestEqual(TEXT("Residual parent records the exact missing support cell beneath the unsupported reserved child footprint"), UnsupportedSummary.MissingRequiredChildBundleSupportCells.Num(), 1);
	TestEqual(TEXT("Residual parent records the exact missing parent support cell beneath the unsupported reserved child footprint"), UnsupportedSummary.MissingRequiredChildBundleSupportCells[0], FIntVector(1, 0, 0));
	return true;
}

bool FLayoutRecursiveScheduleRewriteResidualParentClassifiesForwardedChildSupportProvenanceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::VerticalAccess)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/ForwardedSupportChild");
	DemandPlan.ReservedParentCells = {
		FIntVector(1, 0, 0)
	};

	const FResidualParentCapabilitySummary Summary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(RootRequest), DemandPlan);

	TestFalse(TEXT("Residual parent removes the lower parent support cell once the child reserves it"), Summary.PlannedCells.ContainsByPredicate(
		[](const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Cell == FIntVector(1, 0, 0);
		}));
	TestTrue(TEXT("Residual parent keeps the upper parent cell when the reserved child footprint provides filled support below it"), Summary.PlannedCells.ContainsByPredicate(
		[](const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Cell == FIntVector(1, 0, 1);
		}));
	TestEqual(TEXT("Residual parent marks the upper parent cell with forwarded child support provenance"), Summary.SupportProvenanceByCell.FindRef(FIntVector(1, 0, 1)), FLayoutId(TEXT("ParentSupport.ForwardedChild.BundleSupport")));
	TestFalse(TEXT("Residual parent does not automatically treat a structurally supported upper cell as route-connected when no parent traversable bridge remains"), Summary.RootConnectedTraversableCells.Contains(FIntVector(1, 0, 1)));
	TestEqual(TEXT("Residual parent does not count the forwarded-supported upper vertical-access cell as a surviving parent provider without a residual route connection"), Summary.CountedParentVerticalAccessCells.Num(), 0);
	return true;
}



bool FLayoutRecursiveScheduleRewriteResidualParentClassifiesSameRegionSupportProvenanceTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(2, 1);
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(1, 0, 1), ELayoutCellIntent::VerticalAccess)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/Child");

	const FResidualParentCapabilitySummary Summary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(RootRequest), DemandPlan);

	TestEqual(TEXT("Residual-parent provenance test records support provenance for every surviving cell"), Summary.SupportProvenanceByCell.Num(), 3);
	TestEqual(TEXT("Residual-parent provenance test marks grounded entry cells with grounded same-region provenance"), Summary.SupportProvenanceByCell.FindRef(FIntVector(0, 0, 0)), FLayoutId(TEXT("ParentSupport.SameRegion.Grounded")));
	TestEqual(TEXT("Residual-parent provenance test marks grounded interior cells with grounded same-region provenance"), Summary.SupportProvenanceByCell.FindRef(FIntVector(1, 0, 0)), FLayoutId(TEXT("ParentSupport.SameRegion.Grounded")));
	TestEqual(TEXT("Residual-parent provenance test marks supported upper-level cells with stacked same-region provenance"), Summary.SupportProvenanceByCell.FindRef(FIntVector(1, 0, 1)), FLayoutId(TEXT("ParentSupport.SameRegion.Stacked")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteResidualParentSummarizesReachabilityAndBoundaryStateTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(3, 1);
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(2, 0, 0), ELayoutCellIntent::VerticalAccess)
	};
	RootRequest.CommittedTraversalAnchors = {
		MakeTraversalAnchor(FIntVector(1, 0, 0))
	};
	RootRequest.CommittedEndpointAnchors = {
		MakeEndpointAnchor(TEXT("RootExit"), FIntVector(2, 0, 0), ELayoutFaceDirection::PosX)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/Child");

	const FResidualParentCapabilitySummary Summary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(RootRequest), DemandPlan);

	TestEqual(TEXT("Residual parent keeps all supported planned cells"), Summary.PlannedCells.Num(), 3);
	TestTrue(TEXT("Residual parent keeps protected traversal anchors in the root-connected component"), Summary.RootConnectedTraversableCells.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Residual parent keeps reachable vertical-access providers in the root-connected component"), Summary.RootConnectedTraversableCells.Contains(FIntVector(2, 0, 0)));
	TestEqual(TEXT("Residual parent no longer publishes the reachable vertical-access provider through counted-parent provider cells in this residual summary"), Summary.CountedParentVerticalAccessCells.Num(), 0);
	TestEqual(TEXT("Residual parent records support provenance for every surviving cell"), Summary.SupportProvenanceByCell.Num(), 3);
	TestEqual(TEXT("Residual parent records one external endpoint reachability record"), Summary.ExternalEndpointReachability.Num(), 1);
	TestTrue(TEXT("Residual parent preserves root external endpoint reachability when the anchor cell stays connected"), Summary.ExternalEndpointReachability[0].bReachableFromResidualParent);
	TestEqual(TEXT("Reachable external endpoints use the root residual component id"), Summary.ExternalEndpointReachability[0].ParentComponentId, 0);
	TestEqual(TEXT("Residual parent records component ids for all root-connected cells"), Summary.RootConnectedComponentIdByCell.Num(), Summary.RootConnectedTraversableCells.Num());
	TestEqual(TEXT("Residual parent records traversable-face summaries for all root-connected cells"), Summary.RootConnectedTraversableFacesByCell.Num(), Summary.RootConnectedTraversableCells.Num());
	TestEqual(TEXT("Residual parent assigns the protected traversal anchor to root component 0"), Summary.RootConnectedComponentIdByCell.FindRef(FIntVector(1, 0, 0)), 0);
	TestEqual(TEXT("Residual parent assigns the reachable vertical-access provider to root component 0"), Summary.RootConnectedComponentIdByCell.FindRef(FIntVector(2, 0, 0)), 0);
	const TArray<ELayoutFaceDirection>* InteriorFaces =
		Summary.RootConnectedTraversableFacesByCell.Find(FIntVector(1, 0, 0));
	const TArray<ELayoutFaceDirection>* ProviderFaces =
		Summary.RootConnectedTraversableFacesByCell.Find(FIntVector(2, 0, 0));
	if (!TestNotNull(TEXT("Residual parent summarizes traversable faces for the protected interior cell"), InteriorFaces)
		|| !TestNotNull(TEXT("Residual parent summarizes traversable faces for the reachable provider cell"), ProviderFaces))
	{
		return false;
	}
	TestEqual(TEXT("Residual parent keeps two traversable faces on the protected interior cell"), InteriorFaces->Num(), 2);
	TestEqual(TEXT("Residual parent keeps one traversable face on the reachable provider cell"), ProviderFaces->Num(), 1);
	TestTrue(TEXT("Residual parent keeps the interior cell's negative-X traversable face"), InteriorFaces->Contains(ELayoutFaceDirection::NegX));
	TestTrue(TEXT("Residual parent keeps the interior cell's positive-X traversable face"), InteriorFaces->Contains(ELayoutFaceDirection::PosX));
	TestEqual(TEXT("Residual parent keeps only the provider cell's inward negative-X traversable face"), (*ProviderFaces)[0], ELayoutFaceDirection::NegX);
	TestTrue(TEXT("Residual parent classifies seam-relevant boundary cells"), Summary.SeamRelevantBoundaryCells.Num() >= 2);
	TestTrue(TEXT("Residual parent emits at least one boundary span classification"), Summary.BoundarySpanClassifications.Num() > 0);
	TestTrue(TEXT("Residual parent preserves PosX perimeter classification for the reachable outer cell"), Summary.BoundarySpanClassifications.ContainsByPredicate(
		[](const FResidualBoundarySpanClassification& Classification)
		{
			return Classification.AdjacencyClassId == FLayoutId(TEXT("Perimeter.PosX"))
				&& Classification.Cells.Contains(FIntVector(2, 0, 0));
		}));
	return true;
}

bool FLayoutRecursiveScheduleRewriteResidualParentSeparatesDisconnectedRootComponentsTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(7, 1);
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(5, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(6, 0, 0), ELayoutCellIntent::VerticalAccess)
	};
	RootRequest.CommittedTraversalAnchors = {
		MakeTraversalAnchor(FIntVector(5, 0, 0))
	};
	RootRequest.CommittedEndpointAnchors = {
		MakeEndpointAnchor(TEXT("RemoteExit"), FIntVector(6, 0, 0), ELayoutFaceDirection::PosX)
	};

	FNegotiationDemandPlan DemandPlan;
	DemandPlan.ChildRegionDebugPath = TEXT("Root/Child");

	const FResidualParentCapabilitySummary Summary =
		RebuildResidualParentSummaryForDemand(BuildSolveContext(RootRequest), DemandPlan);

	TestEqual(TEXT("Disconnected residual-component test keeps four root-connected traversable cells"), Summary.RootConnectedTraversableCells.Num(), 4);
	TestEqual(TEXT("Disconnected residual-component test records component ids for every root-connected cell"), Summary.RootConnectedComponentIdByCell.Num(), 4);
	const int32 EntryComponentId = Summary.RootConnectedComponentIdByCell.FindRef(FIntVector(0, 0, 0));
	const int32 EntryNeighborComponentId = Summary.RootConnectedComponentIdByCell.FindRef(FIntVector(1, 0, 0));
	const int32 RemoteProtectedComponentId = Summary.RootConnectedComponentIdByCell.FindRef(FIntVector(5, 0, 0));
	const int32 RemoteExitComponentId = Summary.RootConnectedComponentIdByCell.FindRef(FIntVector(6, 0, 0));
	TestEqual(TEXT("Disconnected residual-component test keeps the entry island on component 0"), EntryComponentId, 0);
	TestEqual(TEXT("Disconnected residual-component test keeps the entry-neighbor island on the same component"), EntryNeighborComponentId, EntryComponentId);
	TestEqual(TEXT("Disconnected residual-component test seeds the protected remote island as a second component"), RemoteProtectedComponentId, 1);
	TestEqual(TEXT("Disconnected residual-component test keeps the remote exit on the same protected component"), RemoteExitComponentId, RemoteProtectedComponentId);
	TestTrue(TEXT("Disconnected residual-component test keeps the two islands on different component ids"), EntryComponentId != RemoteProtectedComponentId);
	TestEqual(TEXT("Disconnected residual-component test records one reachable external endpoint"), Summary.ExternalEndpointReachability.Num(), 1);
	if (Summary.ExternalEndpointReachability.Num() == 1)
	{
		TestTrue(TEXT("Disconnected residual-component test keeps the remote endpoint reachable"), Summary.ExternalEndpointReachability[0].bReachableFromResidualParent);
		TestEqual(TEXT("Disconnected residual-component test assigns the remote endpoint to the second root-connected component"), Summary.ExternalEndpointReachability[0].ParentComponentId, RemoteExitComponentId);
	}
	TestEqual(TEXT("Disconnected residual-component test no longer counts the disconnected remote provider as a counted parent provider"), Summary.CountedParentVerticalAccessCells.Num(), 0);
	return true;
}
