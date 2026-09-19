// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;

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

	FLayoutLocalCellFaceRuleSnapshot MakeLocalFaceSnapshot(const FIntVector& Cell)
	{
		FLayoutLocalCellFaceRuleSnapshot Snapshot;
		Snapshot.LocalCell = Cell;
		return Snapshot;
	}

	FLayoutModuleSolveSnapshot MakeRootVerticalAccessIngressModuleSnapshot(
		const FLayoutId SnapshotId,
		const ELayoutFaceDirection IngressDirection)
	{
		FLayoutModuleSolveSnapshot Snapshot;
		Snapshot.SnapshotId = SnapshotId;
		Snapshot.DebugName = FName(*SnapshotId.ToString());
		Snapshot.BoundsCells = FIntVector(1, 1, 1);
		Snapshot.OccupiedLocalCells = {FIntVector::ZeroValue};
		Snapshot.GeneratedLocalCellFaceRules.Add(MakeLocalFaceSnapshot(FIntVector::ZeroValue));
		Snapshot.Roles = {
			ELayoutModuleRole::Boundary,
			ELayoutModuleRole::Entry,
			ELayoutModuleRole::VerticalAccess
		};
		Snapshot.SupportedCellIntents = {
			ELayoutCellIntent::VerticalAccess
		};

		FLayoutDerivedEndpointOffer& EndpointOffer = Snapshot.DerivedEndpointOffers.AddDefaulted_GetRef();
		EndpointOffer.OfferId = FLayoutId(*FString::Printf(TEXT("%s.Ingress"), *SnapshotId.ToString()));
		EndpointOffer.LocalCell = FIntVector::ZeroValue;
		EndpointOffer.FaceDirection = IngressDirection;
		EndpointOffer.ConnectionTag = LayoutGameplayTags::FaceEntry;
		EndpointOffer.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceOpen);
		EndpointOffer.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		EndpointOffer.Roles = Snapshot.Roles;

		FLayoutDerivedVerticalAccessContract& VerticalAccess =
			Snapshot.DerivedVerticalAccessContracts.AddDefaulted_GetRef();
		VerticalAccess.ContractId = FLayoutId(*FString::Printf(TEXT("%s.VerticalAccess"), *SnapshotId.ToString()));
		VerticalAccess.LocalCell = FIntVector::ZeroValue;
		VerticalAccess.SourceTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		VerticalAccess.ExitTraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
		return Snapshot;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteContextBuildTest,
	"PorismExtension.Layout.Solver.Rewrite.ContextBuildUsesRequestOwnedExecutionSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)



IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewritePrefersContentSetSharedCellSizeOverStaleModuleSnapshotTest,
	"PorismExtension.Layout.Solver.Rewrite.PrefersContentSetSharedCellSizeOverStaleModuleSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)







IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteRejectsIncompleteRootPublicationIdentityTest,
	"PorismExtension.Layout.Solver.Rewrite.RejectsIncompleteRootPublicationIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRecursiveScheduleRewriteRejectsTemplatePlacementOffsetWithoutSharedCellSizeTest,
	"PorismExtension.Layout.Solver.Rewrite.RejectsTemplatePlacementOffsetWithoutSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRecursiveScheduleRewriteContextBuildTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.RootSolveId = TEXT("RootSolve");
	RootRequest.SourceContentEntryId = TEXT("ChildSourceEntry");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootPlacementPolicyId = TEXT("DirectRootPolicy");
	RootRequest.TemplatePlacementZOffsetBlocks = -3;
	RootRequest.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	RootRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 40;
	RootRequest.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 18;
	RootRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 16;
	{
		FLayoutSteppedTerrainSupportSample& SupportSample = RootRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		SupportSample.LocalCell = FIntVector(0, 0, 0);
		SupportSample.SupportSurfaceZ = 12;
	}
	{
		FLayoutSteppedTerrainAdjacencyStep& AdjacencyStep = RootRequest.SteppedTerrainSupportMap.AdjacencySteps.AddDefaulted_GetRef();
		AdjacencyStep.FromCell = FIntVector(0, 0, 0);
		AdjacencyStep.ToCell = FIntVector(1, 0, 0);
		AdjacencyStep.StepHeightBlocks = 2;
	}
	RootRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta = 2;
	RootRequest.ContentSetSnapshot.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	RootRequest.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	RootRequest.Seed = 4242;
	RootRequest.ExecutionSettings.MaxConcurrentChildProofJobs = 3;
	RootRequest.ExecutionSettings.MaxSolveDurationSeconds = 0.25f;
	RootRequest.CommittedEndpointAnchors = {
		MakeEndpointAnchor(TEXT("RootDoor"), FIntVector(0, 0, 0), ELayoutFaceDirection::PosX)
	};
	RootRequest.CommittedTraversalAnchors = {
		MakeTraversalAnchor(FIntVector(2, 0, 0))
	};
	/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	RootRequest.FootprintSize = FIntPoint(4, 1);
	RootRequest.PlannedCells = {
		MakePlannedCell(FIntVector(0, 0, 0), ELayoutCellIntent::Entry),
		MakePlannedCell(FIntVector(1, 0, 0), ELayoutCellIntent::Interior),
		MakePlannedCell(FIntVector(2, 0, 0), ELayoutCellIntent::VerticalAccess)
	};

	const FRecursiveScheduleSolveContext FirstContext = BuildSolveContext(RootRequest);
	const FRecursiveScheduleSolveContext SecondContext = BuildSolveContext(RootRequest);

	TestEqual(TEXT("Context keeps the root region path"), FirstContext.RootRequest.RegionDebugPath, RootRequest.RegionDebugPath);
	TestEqual(TEXT("Context keeps request-owned child-proof concurrency"), FirstContext.MaxConcurrentChildProofJobs, 3);
	TestEqual(TEXT("Context keeps the root external endpoint commitments"), FirstContext.RootExternalEndpointCommitments.Num(), 1);
	TestEqual(TEXT("Context keeps the request-owned root solve id instead of inferring it from snapshot/debug ids"), FirstContext.PublicationMetadata.RootSolveId, RootRequest.RootSolveId);
	TestEqual(TEXT("Context keeps the request-owned root candidate id instead of borrowing the child source entry id"), FirstContext.PublicationMetadata.RootCandidateId, RootRequest.RootCandidateId);
	TestEqual(TEXT("Context keeps the root placement policy id"), FirstContext.PublicationMetadata.RootPlacementPolicyId, RootRequest.RootPlacementPolicyId);
	TestEqual(TEXT("Context keeps the request-owned timeout budget on the frozen root request"), FirstContext.RootRequest.ExecutionSettings.MaxSolveDurationSeconds, 0.25f);
	TestEqual(TEXT("Context keeps the template placement offset"), FirstContext.TemplatePlacementZOffsetBlocks, RootRequest.TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Context keeps the world-binding root placement kind"), FirstContext.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Context keeps world-binding terrain search start Z"), FirstContext.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, 40);
	TestEqual(TEXT("Context keeps world-binding terrain search depth"), FirstContext.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, 18);
	TestEqual(TEXT("Context keeps the request-owned stepped support shared cell height"), FirstContext.SteppedTerrainSupportMap.SharedCellHeightInBlocks, 16);
	TestEqual(TEXT("Context keeps the request-owned stepped support sample count"), FirstContext.SteppedTerrainSupportMap.SupportSamples.Num(), 1);
	TestEqual(TEXT("Context keeps the request-owned stepped support adjacency count"), FirstContext.SteppedTerrainSupportMap.AdjacencySteps.Num(), 1);
	TestEqual(TEXT("Context keeps request-owned shared cell size"), FirstContext.SharedCellSizeInBlocks, FIntVector(16, 16, 16));
	TestEqual(TEXT("Context keeps the supplied footprint size"), FirstContext.ParentFootprintSize, RootRequest.FootprintSize);
	TestEqual(TEXT("Context keeps the supplied planned cells"), FirstContext.ParentPlannedCells.Num(), 3);
	TestEqual(TEXT("Context keeps one entry-adjacent protected traversal cell plus the explicit committed anchor"), FirstContext.ProtectedParentTraversalCells.Num(), 2);
	TestTrue(TEXT("Context protects entry-adjacent traversable parent cells from the root planned-cell carrier"), FirstContext.ProtectedParentTraversalCells.Contains(FIntVector(1, 0, 0)));
	TestTrue(TEXT("Context protects committed parent traversal anchors"), FirstContext.ProtectedParentTraversalCells.Contains(FIntVector(2, 0, 0)));
	TestEqual(TEXT("Child-demand seed stream is deterministic"), FirstContext.SeedStreams.ChildDemandSeed, SecondContext.SeedStreams.ChildDemandSeed);
	TestEqual(TEXT("Confirmation seed stream is deterministic"), FirstContext.SeedStreams.ConfirmationSeed, SecondContext.SeedStreams.ConfirmationSeed);
	return true;
}

bool FLayoutRecursiveScheduleRewritePrefersContentSetSharedCellSizeOverStaleModuleSnapshotTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.ContentSetSnapshot.SnapshotId = TEXT("ContentSnapshot");
	RootRequest.ContentSetSnapshot.DebugName = TEXT("ContentSnapshot");
	RootRequest.ContentSetSnapshot.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
	RootRequest.ModuleCatalog.SnapshotId = TEXT("ModuleSnapshot");
	RootRequest.ModuleCatalog.DebugName = TEXT("ModuleSnapshot");
	RootRequest.ModuleCatalog.SharedCellSizeInBlocks = FIntVector(24, 24, 24);

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FLayoutValidationAssertionRecord* SharedCellSizeAssertion = SolveContext.RootRequest.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});

	if (!TestNotNull(TEXT("Mismatched shared cell size request records a request-contract assertion"), SharedCellSizeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Mismatched shared cell size assertion uses the request-contract kind"), SharedCellSizeAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestTrue(TEXT("Content-set-backed shared cell size assertion stays passing when the module snapshot is stale"), SharedCellSizeAssertion->bPassed);
	TestEqual(TEXT("Execution context prefers the content-set shared cell size over the stale module snapshot"), SolveContext.SharedCellSizeInBlocks, FIntVector(16, 16, 16));
	return true;
}

bool FLayoutRecursiveScheduleRewriteRejectsIncompleteRootPublicationIdentityTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.RootPlacementPolicyId = TEXT("DirectRootPolicy");
	RootRequest.RootCandidateId = TEXT("RootCandidate");
	RootRequest.RootSolveId = NAME_None;

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FLayoutValidationAssertionRecord* RootPublicationAssertion = SolveContext.RootRequest.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.RootPublicationIdentityContractValid");
	});

	if (!TestNotNull(TEXT("Incomplete root publication identity records a request-contract assertion"), RootPublicationAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Incomplete root publication identity assertion uses the request-contract kind"), RootPublicationAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestFalse(TEXT("Incomplete root publication identity assertion fails"), RootPublicationAssertion->bPassed);
	TestTrue(TEXT("Incomplete root publication identity assertion reports the request region path"), RootPublicationAssertion->FailureReason.Contains(TEXT("Request 'Root'")));
	TestTrue(TEXT("Incomplete root publication identity assertion references the request placement policy id"), RootPublicationAssertion->RelatedIds.Contains(RootRequest.RootPlacementPolicyId));
	TestTrue(TEXT("Incomplete root publication identity assertion references the request candidate id"), RootPublicationAssertion->RelatedIds.Contains(RootRequest.RootCandidateId));
	TestEqual(TEXT("Execution context still preserves the request-owned root placement policy id for diagnostics"), SolveContext.PublicationMetadata.RootPlacementPolicyId, RootRequest.RootPlacementPolicyId);
	TestEqual(TEXT("Execution context preserves the request-owned root candidate id for diagnostics"), SolveContext.PublicationMetadata.RootCandidateId, RootRequest.RootCandidateId);
	TestEqual(TEXT("Execution context still falls back to the region path for the missing root solve id"), SolveContext.PublicationMetadata.RootSolveId, FLayoutId(TEXT("Root")));
	return true;
}

bool FLayoutRecursiveScheduleRewriteRejectsTemplatePlacementOffsetWithoutSharedCellSizeTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("Root");
	RootRequest.EffectiveSnapshotId = TEXT("RootSnapshot");
	RootRequest.TemplatePlacementZOffsetBlocks = 7;

	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
	const FLayoutValidationAssertionRecord* TemplatePlacementOffsetAssertion = SolveContext.RootRequest.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.TemplatePlacementOffsetContractValid");
	});

	if (!TestNotNull(TEXT("Template placement offset without shared metrics records a request-contract assertion"), TemplatePlacementOffsetAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Template placement offset assertion uses the request-contract kind"), TemplatePlacementOffsetAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestFalse(TEXT("Template placement offset assertion fails"), TemplatePlacementOffsetAssertion->bPassed);
	TestTrue(TEXT("Template placement offset assertion reports the request region path"), TemplatePlacementOffsetAssertion->FailureReason.Contains(TEXT("Request 'Root'")));
	TestTrue(TEXT("Template placement offset assertion reports the frozen content snapshot id in the failure breadcrumb"), TemplatePlacementOffsetAssertion->FailureReason.Contains(RootRequest.ContentSetSnapshot.SnapshotId.ToString()));
	TestTrue(TEXT("Template placement offset assertion reports the frozen module snapshot id in the failure breadcrumb"), TemplatePlacementOffsetAssertion->FailureReason.Contains(RootRequest.ModuleCatalog.SnapshotId.ToString()));
	TestEqual(TEXT("Execution context drops template placement offset when shared metrics are missing"), SolveContext.TemplatePlacementZOffsetBlocks, 0);
	TestEqual(TEXT("Frozen root request also drops template placement offset when shared metrics are missing"), SolveContext.RootRequest.TemplatePlacementZOffsetBlocks, 0);
	return true;
}








