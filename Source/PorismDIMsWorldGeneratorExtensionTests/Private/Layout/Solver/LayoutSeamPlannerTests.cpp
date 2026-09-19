// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

namespace
{
	using namespace LayoutRegionScheduleSolverFacade;

	FLayoutPlannedCell MakeBoundaryCell(const FIntVector& Cell)
	{
		FLayoutPlannedCell PlannedCell;
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = ELayoutCellIntent::Boundary;
		return PlannedCell;
	}

	FLayoutChildCapabilitySeam MakeSeamCapability(
		const FLayoutId CapabilityId,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection,
		const bool bCanOwnSeam = true,
		const bool bCanAcceptSeam = true)
	{
		FLayoutChildCapabilitySeam Seam;
		Seam.CapabilityId = CapabilityId;
		Seam.LocalCell = LocalCell;
		Seam.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		Seam.FaceDirection = FaceDirection;
		Seam.bCanOwnSeam = bCanOwnSeam;
		Seam.bCanAcceptSeam = bCanAcceptSeam;
		return Seam;
	}

	FLayoutDerivedSpanOffer MakeSpanOffer(
		const FLayoutId SpanOfferId,
		const FIntVector& LocalCell,
		const ELayoutFaceDirection FaceDirection)
	{
		FLayoutDerivedSpanOffer SpanOffer;
		SpanOffer.SpanOfferId = SpanOfferId;
		SpanOffer.LocalCell = LocalCell;
		SpanOffer.FaceDirection = FaceDirection;
		SpanOffer.ThicknessCells = 1;
		SpanOffer.bSealsBoundary = true;
		return SpanOffer;
	}

	FLayoutSeamProviderIntent MakeSeamProviderIntent(
		const FName SeamIntentId,
		const bool bCanOwnSeam,
		const bool bCanAcceptSeam)
	{
		FLayoutSeamProviderIntent SeamIntent;
		SeamIntent.SeamIntentId = SeamIntentId;
		SeamIntent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		SeamIntent.bCanOwnSeam = bCanOwnSeam;
		SeamIntent.bCanAcceptSeam = bCanAcceptSeam;
		return SeamIntent;
	}

	FLayoutPartitionSeamRecord MakePartitionSeamRecord(
		const FLayoutId SeamId,
		const FString& ParentRegionPath,
		const FString& OwnerRegionPath,
		const FString& PassiveRegionPath,
		const ELayoutFaceDirection OwnerFaceDirection,
		const ELayoutFaceDirection PassiveFaceDirection,
		const FIntVector& OwnerStartCell,
		const FIntVector& OwnerEndCell,
		const FIntVector& PassiveStartCell,
		const FIntVector& PassiveEndCell,
		const int32 SegmentCount)
	{
		FLayoutPartitionSeamRecord SeamRecord;
		SeamRecord.SeamId = SeamId;
		SeamRecord.ParentRegionDebugPath = ParentRegionPath;
		SeamRecord.OwnerRegionDebugPath = OwnerRegionPath;
		SeamRecord.PassiveRegionDebugPath = PassiveRegionPath;
		SeamRecord.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		SeamRecord.OwnerFaceDirection = OwnerFaceDirection;
		SeamRecord.PassiveFaceDirection = PassiveFaceDirection;
		SeamRecord.OwnerStartCell = OwnerStartCell;
		SeamRecord.OwnerEndCell = OwnerEndCell;
		SeamRecord.PassiveStartCell = PassiveStartCell;
		SeamRecord.PassiveEndCell = PassiveEndCell;
		SeamRecord.SegmentCount = SegmentCount;
		return SeamRecord;
	}

	FLayoutRegionSolveRequest MakeRootParentChildSeamRequest()
	{
		FLayoutRegionSolveRequest RootRequest;
		RootRequest.RegionDebugPath = TEXT("ParentChildSeamRoot");
		/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		RootRequest.FootprintSize = FIntPoint(4, 2);
		RootRequest.ModuleCatalog.SnapshotId = TEXT("ParentChildSeamRoot.ModuleCatalog");
		RootRequest.ProfileSnapshot.SnapshotId = TEXT("ParentChildSeamRoot.Profile");
		RootRequest.PlannedCells = {
			MakeBoundaryCell(FIntVector(0, 0, 0)),
			MakeBoundaryCell(FIntVector(1, 0, 0)),
			MakeBoundaryCell(FIntVector(2, 0, 0)),
			MakeBoundaryCell(FIntVector(3, 0, 0)),
			MakeBoundaryCell(FIntVector(0, 1, 0)),
			MakeBoundaryCell(FIntVector(1, 1, 0)),
			MakeBoundaryCell(FIntVector(2, 1, 0)),
			MakeBoundaryCell(FIntVector(3, 1, 0))
		};

		FLayoutModuleSolveSnapshot& ModuleSnapshot =
			RootRequest.ModuleCatalog.Modules.AddDefaulted_GetRef();
		ModuleSnapshot.SnapshotId = TEXT("ParentChildSeamRoot.Module");
		ModuleSnapshot.AllowedYawRotationSteps = {0};
		ModuleSnapshot.DerivedSpanOffers = {
			MakeSpanOffer(
				TEXT("RootSharedWall"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::PosY)
		};
		ModuleSnapshot.SeamProviderIntents = {
			MakeSeamProviderIntent(
				TEXT("RootOwnsSharedWall"),
				true,
				false)
		};
		return RootRequest;
	}

	FLayoutRegionSolveRequest MakeRootMixedHeightParentChildJunctionRequest()
	{
		FLayoutRegionSolveRequest RootRequest;
		RootRequest.RegionDebugPath = TEXT("MixedHeightParentChildJunctionRoot");
		/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		RootRequest.FootprintSize = FIntPoint(2, 2);
		RootRequest.ModuleCatalog.SnapshotId = TEXT("MixedHeightParentChildJunctionRoot.ModuleCatalog");
		RootRequest.ProfileSnapshot.SnapshotId = TEXT("MixedHeightParentChildJunctionRoot.Profile");
		RootRequest.PlannedCells = {
			MakeBoundaryCell(FIntVector(0, 0, 0)),
			MakeBoundaryCell(FIntVector(1, 0, 0)),
			MakeBoundaryCell(FIntVector(0, 1, 0)),
			MakeBoundaryCell(FIntVector(1, 1, 0)),
			MakeBoundaryCell(FIntVector(0, 0, 1)),
			MakeBoundaryCell(FIntVector(1, 0, 1)),
			MakeBoundaryCell(FIntVector(0, 1, 1)),
			MakeBoundaryCell(FIntVector(1, 1, 1))
		};

		FLayoutModuleSolveSnapshot& ModuleSnapshot =
			RootRequest.ModuleCatalog.Modules.AddDefaulted_GetRef();
		ModuleSnapshot.SnapshotId = TEXT("MixedHeightParentChildJunctionRoot.Module");
		ModuleSnapshot.AllowedYawRotationSteps = {0};
		ModuleSnapshot.DerivedSpanOffers = {
			MakeSpanOffer(
				TEXT("RootNorthUpperA"),
				FIntVector(0, 1, 1),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootNorthUpperB"),
				FIntVector(1, 1, 1),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootEastUpperA"),
				FIntVector(1, 0, 1),
				ELayoutFaceDirection::PosX),
			MakeSpanOffer(
				TEXT("RootEastUpperB"),
				FIntVector(1, 1, 1),
				ELayoutFaceDirection::PosX)
		};
		ModuleSnapshot.SeamProviderIntents = {
			MakeSeamProviderIntent(
				TEXT("RootOwnsUpperSharedWalls"),
				true,
				false)
		};
		return RootRequest;
	}

	FLayoutRegionSolveRequest MakeRootMultiLevelParentChildJunctionRequest()
	{
		FLayoutRegionSolveRequest RootRequest;
		RootRequest.RegionDebugPath = TEXT("MultiLevelParentChildJunctionRoot");
		/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		RootRequest.FootprintSize = FIntPoint(2, 2);
		RootRequest.ModuleCatalog.SnapshotId = TEXT("MultiLevelParentChildJunctionRoot.ModuleCatalog");
		RootRequest.ProfileSnapshot.SnapshotId = TEXT("MultiLevelParentChildJunctionRoot.Profile");
		RootRequest.PlannedCells = {
			MakeBoundaryCell(FIntVector(0, 0, 0)),
			MakeBoundaryCell(FIntVector(1, 0, 0)),
			MakeBoundaryCell(FIntVector(0, 1, 0)),
			MakeBoundaryCell(FIntVector(1, 1, 0)),
			MakeBoundaryCell(FIntVector(0, 0, 1)),
			MakeBoundaryCell(FIntVector(1, 0, 1)),
			MakeBoundaryCell(FIntVector(0, 1, 1)),
			MakeBoundaryCell(FIntVector(1, 1, 1))
		};

		FLayoutModuleSolveSnapshot& ModuleSnapshot =
			RootRequest.ModuleCatalog.Modules.AddDefaulted_GetRef();
		ModuleSnapshot.SnapshotId = TEXT("MultiLevelParentChildJunctionRoot.Module");
		ModuleSnapshot.AllowedYawRotationSteps = {0};
		ModuleSnapshot.DerivedSpanOffers = {
			MakeSpanOffer(
				TEXT("RootNorthLower"),
				FIntVector(1, 1, 0),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootNorthUpper"),
				FIntVector(1, 1, 1),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootEastUpper"),
				FIntVector(1, 1, 1),
				ELayoutFaceDirection::PosX)
		};
		ModuleSnapshot.SeamProviderIntents = {
			MakeSeamProviderIntent(
				TEXT("RootOwnsStackedSharedWalls"),
				true,
				false)
		};
		return RootRequest;
	}

	FLayoutRegionSolveRequest MakeRootMultiLevelParentChildEdgeAttachRequest()
	{
		FLayoutRegionSolveRequest RootRequest;
		RootRequest.RegionDebugPath = TEXT("MultiLevelParentChildEdgeAttachRoot");
		/* !RootRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		RootRequest.FootprintSize = FIntPoint(2, 2);
		RootRequest.ModuleCatalog.SnapshotId = TEXT("MultiLevelParentChildEdgeAttachRoot.ModuleCatalog");
		RootRequest.ProfileSnapshot.SnapshotId = TEXT("MultiLevelParentChildEdgeAttachRoot.Profile");
		RootRequest.PlannedCells = {
			MakeBoundaryCell(FIntVector(0, 0, 0)),
			MakeBoundaryCell(FIntVector(1, 0, 0)),
			MakeBoundaryCell(FIntVector(0, 1, 0)),
			MakeBoundaryCell(FIntVector(1, 1, 0)),
			MakeBoundaryCell(FIntVector(0, 0, 1)),
			MakeBoundaryCell(FIntVector(1, 0, 1)),
			MakeBoundaryCell(FIntVector(0, 1, 1)),
			MakeBoundaryCell(FIntVector(1, 1, 1)),
			MakeBoundaryCell(FIntVector(0, 0, 2)),
			MakeBoundaryCell(FIntVector(1, 0, 2)),
			MakeBoundaryCell(FIntVector(0, 1, 2)),
			MakeBoundaryCell(FIntVector(1, 1, 2))
		};

		FLayoutModuleSolveSnapshot& ModuleSnapshot =
			RootRequest.ModuleCatalog.Modules.AddDefaulted_GetRef();
		ModuleSnapshot.SnapshotId = TEXT("MultiLevelParentChildEdgeAttachRoot.Module");
		ModuleSnapshot.AllowedYawRotationSteps = {0};
		ModuleSnapshot.DerivedSpanOffers = {
			MakeSpanOffer(
				TEXT("RootNorthLower"),
				FIntVector(1, 1, 0),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootNorthMiddle"),
				FIntVector(1, 1, 1),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootNorthUpper"),
				FIntVector(1, 1, 2),
				ELayoutFaceDirection::PosY),
			MakeSpanOffer(
				TEXT("RootEastMiddle"),
				FIntVector(1, 1, 1),
				ELayoutFaceDirection::PosX)
		};
		ModuleSnapshot.SeamProviderIntents = {
			MakeSeamProviderIntent(
				TEXT("RootOwnsThreeLevelSharedWalls"),
				true,
				false)
		};
		return RootRequest;
	}

	FNegotiatedDemandResult MakeSuccessfulSiblingSeamDemand(
		const FString& ParentRegionPath,
		const FString& ChildRegionPath,
		const FIntVector& RegionOffset,
		const TArray<FIntVector>& LocalCells,
		const TArray<FLayoutChildCapabilitySeam>& SeamCapabilities)
	{
		FNegotiatedDemandResult Result;
		Result.bSucceeded = true;
		Result.ChildRegionDebugPath = ChildRegionPath;
		Result.ChildRequest.RegionDebugPath = ChildRegionPath;
		Result.ChildRequest.SourceParentRegionDebugPath = ParentRegionPath;
		/* !Result.ChildRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Result.ChildRequest.RegionCellOffset = RegionOffset;
		for (const FIntVector& LocalCell : LocalCells)
		{
			Result.ChildRequest.PlannedCells.Add(MakeBoundaryCell(LocalCell));
		}

		Result.ChildRequest.bUseSuppliedChildCapabilityEnvelope = true;
		Result.ChildRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
			ChildRegionPath;
		Result.ChildRequest.SuppliedChildCapabilityEnvelope.SeamCapabilities =
			SeamCapabilities;
		return Result;
	}

	FNegotiatedDemandResult MakeSuccessfulSiblingSeamDemand(
		const FString& ParentRegionPath,
		const FString& ChildRegionPath,
		const FIntVector& RegionOffset,
		const TArray<FIntVector>& LocalCells,
		const FLayoutChildCapabilitySeam& SeamCapability)
	{
		return MakeSuccessfulSiblingSeamDemand(
			ParentRegionPath,
			ChildRegionPath,
			RegionOffset,
			LocalCells,
			TArray<FLayoutChildCapabilitySeam>{SeamCapability});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerAllowsIndependentLocalPlacementZonesTest,
	"PorismExtension.Layout.Solver.SeamPlanner.AllowsIndependentLocalPlacementZones",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsExactParentChildOverlapSeamTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsExactParentChildOverlapSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerRejectsIncompatiblePairWithoutClearingValidSeamTest,
	"PorismExtension.Layout.Solver.SeamPlanner.RejectsIncompatiblePairWithoutClearingValidSeam",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsSiblingSeamPlanFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsSiblingSeamPlanFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerProofContractCarriesSiblingSeamPlanTest,
	"PorismExtension.Layout.Solver.SeamPlanner.ProofContractCarriesSiblingSeamPlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMixedHeightSiblingJunctionRequirementFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMixedHeightSiblingJunctionRequirementFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsParentChildSeamPlanFromRootAndNegotiatedChildTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsParentChildSeamPlanFromRootAndNegotiatedChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMixedHeightParentChildJunctionRequirementFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMixedHeightParentChildJunctionRequirementFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelParentChildJunctionRequirementFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelParentChildJunctionRequirementFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelParentChildEdgeAttachRequirementFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelParentChildEdgeAttachRequirementFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelSiblingEdgeAttachRequirementFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelSiblingEdgeAttachRequirementFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsSiblingMultiBranchJunctionRequirementsFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsSiblingRunWithTwoSharedCornerJunctionRequirementsFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsSiblingRunWithInteriorEdgeAttachAndEndpointSharedCornerFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsSiblingRunWithInteriorEdgeAttachAndEndpointSharedCornerFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsSiblingRunWithTwoInteriorEdgeAttachJunctionRequirementsFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsSiblingRunWithTwoInteriorEdgeAttachJunctionRequirementsFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelSiblingJunctionRequirementFromNegotiatedChildrenTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelSiblingJunctionRequirementFromNegotiatedChildren",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerEmitsOwnerSideJunctionRequirementForSiblingSharedCornerTest,
	"PorismExtension.Layout.Solver.SeamPlanner.EmitsOwnerSideJunctionRequirementForSiblingSharedCorner",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsOwnerSideJunctionRequirementForSiblingEdgeAttachTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsOwnerSideJunctionRequirementForSiblingEdgeAttach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsOwnerSideJunctionRequirementFromFragmentedSiblingEdgeAttachTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsOwnerSideJunctionRequirementFromFragmentedSiblingEdgeAttach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerKeepsDisconnectedSiblingFragmentsAsSeparateRunsForJunctionMatchingTest,
	"PorismExtension.Layout.Solver.SeamPlanner.KeepsDisconnectedSiblingFragmentsAsSeparateRunsForJunctionMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerKeepsDisconnectedParentChildFragmentsAsSeparateRunsForJunctionMatchingTest,
	"PorismExtension.Layout.Solver.SeamPlanner.KeepsDisconnectedParentChildFragmentsAsSeparateRunsForJunctionMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsOwnerSideJunctionRequirementForParentChildEdgeAttachTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsOwnerSideJunctionRequirementForParentChildEdgeAttach",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerPrefersOwnerDefinedJunctionContractOverPassiveAmbiguityTest,
	"PorismExtension.Layout.Solver.SeamPlanner.PrefersOwnerDefinedJunctionContractOverPassiveAmbiguity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsParentChildSharedCornerJunctionRequirementTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsParentChildSharedCornerJunctionRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelParentChildSharedCornerJunctionRequirementTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelParentChildSharedCornerJunctionRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelParentChildEdgeAttachJunctionRequirementTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelParentChildEdgeAttachJunctionRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsMultiLevelSiblingEdgeAttachJunctionRequirementTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsMultiLevelSiblingEdgeAttachJunctionRequirement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSeamPlannerBuildsTwoSiblingEdgeAttachJunctionRequirementsTest,
	"PorismExtension.Layout.Solver.SeamPlanner.BuildsTwoSiblingSharedCornerJunctionRequirementsFromOneContinuingRun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSeamPlannerAllowsIndependentLocalPlacementZonesTest::RunTest(const FString& Parameters)
{
	FLayoutChildCapabilityEnvelope OwnerEnvelope;
	OwnerEnvelope.SeamCapabilities.Add(MakeSeamCapability(
		TEXT("Owner"),
		FIntVector::ZeroValue,
		ELayoutFaceDirection::PosX,
		true,
		false));
	FLayoutChildCapabilityEnvelope PassiveEnvelope;
	PassiveEnvelope.SeamCapabilities.Add(MakeSeamCapability(
		TEXT("Passive"),
		FIntVector::ZeroValue,
		ELayoutFaceDirection::NegX,
		false,
		true));

	TestTrue(
		TEXT("Committed seam capability agreement is independent of enclosing placement zones"),
		LayoutProfileSolverInternal::TryResolveSharedSeamCapabilitiesForTests(
			OwnerEnvelope,
			ELayoutFaceDirection::PosX,
			PassiveEnvelope,
			ELayoutFaceDirection::NegX,
			LayoutGameplayTags::InterfacePartitionSolid));

	LayoutProfileSolverInternal::FCompiledSeamSegment Segment;
	Segment.RegionAPath = TEXT("Owner");
	Segment.RegionBPath = TEXT("Passive");
	Segment.RegionAFaceDirection = ELayoutFaceDirection::PosX;
	Segment.RegionBFaceDirection = ELayoutFaceDirection::NegX;
	TMap<FString, FLayoutChildCapabilityEnvelope> EnvelopesByRegion;
	EnvelopesByRegion.Add(Segment.RegionAPath, OwnerEnvelope);
	EnvelopesByRegion.Add(Segment.RegionBPath, PassiveEnvelope);
	LayoutProfileSolverInternal::FChosenSeamContract ChosenContract;
	return TestTrue(
		TEXT("Leaf-stage arbitration accepts reciprocal perimeter capabilities without shared zone state"),
		LayoutProfileSolverInternal::TryChooseLeafStageSeamContractForTests(
			{Segment},
			Segment.RegionAPath,
			EnvelopesByRegion,
			ChosenContract));
}

bool FLayoutSeamPlannerBuildsExactParentChildOverlapSeamTest::RunTest(const FString& Parameters)
{
	FLayoutChildCapabilityEnvelope ParentEnvelope;
	ParentEnvelope.SeamCapabilities.Add(MakeSeamCapability(
		TEXT("ParentOwnsEdge"),
		FIntVector::ZeroValue,
		ELayoutFaceDirection::PosX,
		true,
		false));
	FLayoutChildCapabilityEnvelope ChildEnvelope;
	ChildEnvelope.SeamCapabilities.Add(MakeSeamCapability(
		TEXT("ChildAcceptsEdge"),
		FIntVector::ZeroValue,
		ELayoutFaceDirection::PosX,
		false,
		true));

	TArray<FLayoutPartitionSeamRecord> CommittedSeams;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Exact parent/child overlap commits a seam"),
		LayoutProfileSolverInternal::TryBuildCommittedParentChildSeamForTests(
			ParentEnvelope,
			ChildEnvelope,
			CommittedSeams,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	if (!TestEqual(TEXT("Exact parent/child overlap emits one seam"), CommittedSeams.Num(), 1))
	{
		return false;
	}
	const FLayoutPartitionSeamRecord& Seam = CommittedSeams[0];
	TestEqual(TEXT("Parent remains exact seam owner"), Seam.OwnerRegionDebugPath, FString(TEXT("CommittedParent")));
	TestEqual(TEXT("Child remains passive seam region"), Seam.PassiveRegionDebugPath, FString(TEXT("CommittedParentChild")));
	TestEqual(TEXT("Parent/child overlap uses same owner and passive cell"), Seam.OwnerStartCell, Seam.PassiveStartCell);
	return true;
}

bool FLayoutSeamPlannerRejectsIncompatiblePairWithoutClearingValidSeamTest::RunTest(const FString& Parameters)
{
	FLayoutChildCapabilityEnvelope FirstEnvelope;
	FirstEnvelope.SeamCapabilities.Add(MakeSeamCapability(
		TEXT("FirstOwnsEdge"),
		FIntVector::ZeroValue,
		ELayoutFaceDirection::PosX,
		true,
		false));
	FLayoutChildCapabilityEnvelope MiddleEnvelope;
	MiddleEnvelope.SeamCapabilities = {
		MakeSeamCapability(
			TEXT("MiddleAcceptsEdge"),
			FIntVector::ZeroValue,
			ELayoutFaceDirection::NegX,
			false,
			true),
		MakeSeamCapability(
			TEXT("MiddleOwnsEdge"),
			FIntVector::ZeroValue,
			ELayoutFaceDirection::PosX,
			true,
			false)};
	FLayoutChildCapabilityEnvelope LastEnvelope;
	LastEnvelope.SeamCapabilities.Add(MakeSeamCapability(
		TEXT("LastAcceptsCorner"),
		FIntVector::ZeroValue,
		ELayoutFaceDirection::NegX,
		false,
		true));
	LastEnvelope.SeamCapabilities[0].InterfaceFamily =
		LayoutGameplayTags::InterfacePartitionDoor;

	TArray<FLayoutPartitionSeamRecord> CommittedSeams;
	FString FailureReason;
	TestFalse(
		TEXT("Exact committed builder rejects candidate containing one incompatible authored sibling pair"),
		LayoutProfileSolverInternal::TryBuildCommittedSiblingSeamsForTests(
			{FirstEnvelope, MiddleEnvelope, LastEnvelope},
			{FIntVector(0, 0, 0), FIntVector(1, 0, 0), FIntVector(2, 0, 0)},
			CommittedSeams,
			FailureReason));
	TestTrue(
		TEXT("Exact pair failure names incompatible siblings"),
		FailureReason.Contains(TEXT("CommittedSibling1"))
			&& FailureReason.Contains(TEXT("CommittedSibling2")));
	if (!TestEqual(
		TEXT("Valid earlier seam remains available for candidate diagnostics instead of silent whole-plan reset"),
		CommittedSeams.Num(),
		1))
	{
		return false;
	}
	return true;
}

bool FLayoutSeamPlannerBuildsSiblingSeamPlanFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("MixedHeightSiblingRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	TArray<FIntVector> UpperCells;
	for (int32 Y = 0; Y < 2; ++Y)
	{
		for (int32 X = 0; X < 2; ++X)
		{
			UpperCells.Add(FIntVector(X, Y, 0));
		}
	}

	TArray<FIntVector> TallCells;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		for (int32 Y = 0; Y < 2; ++Y)
		{
			for (int32 X = 0; X < 2; ++X)
			{
				TallCells.Add(FIntVector(X, Y, Z));
			}
		}
	}

	const FNegotiatedDemandResult UpperResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingRoot"),
			TEXT("MixedHeightSiblingUpper"),
			FIntVector(0, 0, 1),
			UpperCells,
			MakeSeamCapability(
				TEXT("UpperSharedWall"),
				FIntVector(1, 0, 0),
				ELayoutFaceDirection::PosX));
	const FNegotiatedDemandResult TallResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingRoot"),
			TEXT("MixedHeightSiblingTall"),
			FIntVector(1, 0, 0),
			TallCells,
			MakeSeamCapability(
				TEXT("TallSharedWall"),
				FIntVector(0, 0, 1),
				ELayoutFaceDirection::NegX));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{TallResult, UpperResult});

	if (!TestTrue(
			TEXT("Seam planner builds at least one sibling seam record from successful negotiated children"),
			SeamPlan.PlannedPartitionSeams.Num() > 0))
	{
		return false;
	}

	TestFalse(
		TEXT("Current seam planner bridge does not claim authoritative seam ownership yet"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	TestTrue(
		TEXT("Current seam planner bridge does not emit junction requirements yet"),
		SeamPlan.JunctionRequirements.IsEmpty());

	const FLayoutPartitionSeamRecord* MatchingSiblingSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightSiblingRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightSiblingUpper")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightSiblingTall")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			});
	TestNotNull(
		TEXT("Seam planner keeps the expected mixed-height sibling overlap seam"),
		MatchingSiblingSeam);
	TestTrue(
		TEXT("Current seam planner bridge only emits sibling seams for the negotiated region pair in this focused fixture"),
		!SeamPlan.PlannedPartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath != TEXT("MixedHeightSiblingRoot")
					|| SeamRecord.OwnerRegionDebugPath != TEXT("MixedHeightSiblingUpper")
					|| SeamRecord.PassiveRegionDebugPath != TEXT("MixedHeightSiblingTall");
			}));
	return true;
}

bool FLayoutSeamPlannerProofContractCarriesSiblingSeamPlanTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("MixedHeightSiblingRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult UpperResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingRoot"),
			TEXT("MixedHeightSiblingUpper"),
			FIntVector(0, 0, 1),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0)
			},
			MakeSeamCapability(
				TEXT("UpperSharedWall"),
				FIntVector(1, 0, 0),
				ELayoutFaceDirection::PosX));
	const FNegotiatedDemandResult TallResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingRoot"),
			TEXT("MixedHeightSiblingTall"),
			FIntVector(1, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(0, 0, 1),
				FIntVector(1, 0, 1),
				FIntVector(0, 1, 1),
				FIntVector(1, 1, 1)
			},
			MakeSeamCapability(
				TEXT("TallSharedWall"),
				FIntVector(0, 0, 1),
				ELayoutFaceDirection::NegX));

	const FNegotiatedProofScheduleContract ProofContract =
		BuildNegotiatedProofScheduleContract(
			SolveContext,
			{UpperResult, TallResult});

	if (!TestTrue(
			TEXT("Proof contract keeps at least one sibling seam from the dedicated seam planner"),
			ProofContract.SeamPlan.PlannedPartitionSeams.Num() > 0))
	{
		return false;
	}

	TestTrue(
		TEXT("Proof contract keeps the expected mixed-height sibling overlap seam"),
		ProofContract.SeamPlan.PlannedPartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightSiblingRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightSiblingUpper")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightSiblingTall")
					&& SeamRecord.OwnerStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			}));
	TestFalse(
		TEXT("Proof contract keeps the current non-authoritative seam bridge marker"),
		ProofContract.SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsMixedHeightSiblingJunctionRequirementFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("MixedHeightSiblingJunctionRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult IsolatedUpperResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingJunctionRoot"),
			TEXT("MixedHeightSiblingJunctionUpper"),
			FIntVector(0, 0, 1),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0)
			},
			MakeSeamCapability(
				TEXT("UpperSharedWall"),
				FIntVector(1, 0, 0),
				ELayoutFaceDirection::PosX));
	const FNegotiatedDemandResult IsolatedTallResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingJunctionRoot"),
			TEXT("MixedHeightSiblingJunctionTall"),
			FIntVector(1, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(0, 0, 1),
				FIntVector(1, 0, 1),
				FIntVector(0, 1, 1),
				FIntVector(1, 1, 1)
			},
			MakeSeamCapability(
				TEXT("TallSharedWall"),
				FIntVector(0, 0, 1),
				ELayoutFaceDirection::NegX));

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingJunctionRoot"),
			TEXT("MixedHeightSiblingJunctionUpper"),
			FIntVector(0, 0, 1),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0)
			},
			{
				MakeSeamCapability(
					TEXT("UpperNorthA"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("UpperNorthB"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("UpperEastUpperLevelBranch"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingJunctionRoot"),
			TEXT("MixedHeightSiblingJunctionNorth"),
			FIntVector(0, 1, 1),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptA"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegY),
				MakeSeamCapability(
					TEXT("NorthAcceptB"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY)
			});
	const FNegotiatedDemandResult TallResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightSiblingJunctionRoot"),
			TEXT("MixedHeightSiblingJunctionTall"),
			FIntVector(1, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(0, 0, 1),
				FIntVector(1, 0, 1),
				FIntVector(0, 1, 1),
				FIntVector(1, 1, 1)
			},
			MakeSeamCapability(
				TEXT("TallUpperLevelBranchAccept"),
				FIntVector(0, 1, 1),
				ELayoutFaceDirection::NegX));

	const FNegotiatedSeamPlan IsolatedMixedHeightSeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{IsolatedTallResult, IsolatedUpperResult});
	TestTrue(
		TEXT("Known-good mixed-height sibling seam fixture still keeps the isolated upper/tall overlap seam"),
		IsolatedMixedHeightSeamPlan.PlannedPartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightSiblingJunctionRoot")
					&& ((SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightSiblingJunctionUpper")
							&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightSiblingJunctionTall"))
						|| (SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightSiblingJunctionTall")
							&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightSiblingJunctionUpper")))
					&& SeamRecord.OwnerStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			}));
	TestTrue(
		TEXT("Known-good mixed-height sibling seam fixture stays junction-free in isolation"),
		IsolatedMixedHeightSeamPlan.JunctionRequirements.IsEmpty());

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, TallResult, OwnerResult});

	TestTrue(
		TEXT("Mixed-height sibling junction fixture keeps at least the two expected seam runs"),
		SeamPlan.PlannedPartitionSeams.Num() >= 2);
	TestTrue(
		TEXT("Mixed-height sibling junction fixture keeps the upper/tall mixed-height overlap seam"),
		SeamPlan.PlannedPartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightSiblingJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightSiblingJunctionUpper")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightSiblingJunctionTall")
					&& SeamRecord.OwnerStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			}));
	TestTrue(
		TEXT("Mixed-height sibling junction fixture keeps the upper/north overlap seam"),
		SeamPlan.PlannedPartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightSiblingJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightSiblingJunctionUpper")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightSiblingJunctionNorth")
					&& SeamRecord.OwnerStartCell == FIntVector(0, 1, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(0, 1, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			}));
	if (!TestEqual(
			TEXT("Mixed-height sibling junction fixture keeps exactly the two expected seam runs"),
			SeamPlan.PlannedPartitionSeams.Num(),
			2))
	{
		return false;
	}

	if (!TestEqual(
			TEXT("Mixed-height sibling junction fixture emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Mixed-height sibling junction keeps the upper owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("MixedHeightSiblingJunctionUpper")));
	TestEqual(
		TEXT("Mixed-height sibling junction keeps the tall-room terminating branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("MixedHeightSiblingJunctionTall")));
	TestEqual(
		TEXT("Mixed-height sibling junction keeps the north-room continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("MixedHeightSiblingJunctionNorth")));
	TestEqual(
		TEXT("Mixed-height sibling junction keeps the overlapping upper-level owner cell"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Mixed-height sibling junction keeps the shared-corner adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingSharedCorner.2.0")));
	TestFalse(
		TEXT("Current mixed-height sibling junction bridge remains non-authoritative while this first-pass case is supported"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsParentChildSeamPlanFromRootAndNegotiatedChildTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest = MakeRootParentChildSeamRequest();
	const FLayoutChildCapabilityEnvelope RootEnvelope =
		BuildChildCapabilityEnvelopeFromRequest(RootRequest);
	if (!TestTrue(
			TEXT("Synthetic root request derives the expected parent-owned shared-wall seam capability before seam planning"),
			RootEnvelope.SeamCapabilities.ContainsByPredicate(
				[](const FLayoutChildCapabilitySeam& Capability)
				{
					return Capability.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
						&& Capability.FaceDirection == ELayoutFaceDirection::PosY
						&& Capability.bCanOwnSeam
						&& !Capability.bCanAcceptSeam;
				})))
	{
		return false;
	}

	RootRequest.bUseSuppliedChildCapabilityEnvelope = true;
	RootRequest.SuppliedChildCapabilityEnvelope = RootEnvelope;
	RootRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
		RootRequest.RegionDebugPath;
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult ChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("ParentChildSeamRoot"),
			TEXT("ParentChildSeamChild"),
			FIntVector(0, 1, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0),
				FIntVector(3, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(2, 1, 0),
				FIntVector(3, 1, 0)
			},
			MakeSeamCapability(
				TEXT("ChildAcceptsSharedWall"),
				FIntVector(0, 0, 0),
				ELayoutFaceDirection::NegY,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{ChildResult});

	if (!TestTrue(
			TEXT("Seam planner builds at least one parent-child seam record from the synthetic root request plus negotiated child"),
			SeamPlan.PlannedPartitionSeams.Num() > 0))
	{
		return false;
	}

	TestTrue(
		TEXT("Current seam planner bridge keeps the full parent-owned parent-child north seam"),
		SeamPlan.PlannedPartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("ParentChildSeamRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("ParentChildSeamRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("ParentChildSeamChild")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(0, 1, 0)
					&& SeamRecord.OwnerEndCell == FIntVector(3, 1, 0)
					&& SeamRecord.PassiveStartCell == FIntVector(0, 1, 0)
					&& SeamRecord.PassiveEndCell == FIntVector(3, 1, 0)
					&& SeamRecord.SegmentCount == 4;
			}));
	TestFalse(
		TEXT("Current seam planner bridge still does not claim authoritative seam ownership on parent-child seams"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsMixedHeightParentChildJunctionRequirementFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest = MakeRootMixedHeightParentChildJunctionRequest();
	const FLayoutChildCapabilityEnvelope RootEnvelope =
		BuildChildCapabilityEnvelopeFromRequest(RootRequest);
	if (!TestTrue(
			TEXT("Mixed-height parent-child root request derives upper-level parent-owned seam capabilities before seam planning"),
			RootEnvelope.SeamCapabilities.ContainsByPredicate(
				[](const FLayoutChildCapabilitySeam& Capability)
				{
					return Capability.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
						&& Capability.bCanOwnSeam
						&& !Capability.bCanAcceptSeam
						&& Capability.LocalCell.Z == 1;
				})))
	{
		return false;
	}

	RootRequest.bUseSuppliedChildCapabilityEnvelope = true;
	RootRequest.SuppliedChildCapabilityEnvelope = RootEnvelope;
	RootRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
		RootRequest.RegionDebugPath;
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult NorthChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightParentChildJunctionRoot"),
			TEXT("MixedHeightParentChildNorth"),
			FIntVector(0, 1, 1),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptA"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptB"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MixedHeightParentChildJunctionRoot"),
			TEXT("MixedHeightParentChildEast"),
			FIntVector(1, 0, 1),
			{
				FIntVector(0, 0, 0),
				FIntVector(0, 1, 0)
			},
			{
				MakeSeamCapability(
					TEXT("EastAcceptA"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegX,
					false,
					true),
				MakeSeamCapability(
					TEXT("EastAcceptB"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::NegX,
					false,
					true)
			});

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthChildResult, EastChildResult});

	if (!TestEqual(
			TEXT("Mixed-height parent-child seam planner emits exactly the two expected upper-level parent-owned seam runs"),
			SeamPlan.PlannedPartitionSeams.Num(),
			2))
	{
		return false;
	}

	const FLayoutPartitionSeamRecord* NorthSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightParentChildJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightParentChildJunctionRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightParentChildNorth")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(0, 1, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(0, 1, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			});
	TestNotNull(
		TEXT("Mixed-height parent-child seam planner keeps the upper north parent-child seam"),
		NorthSeam);
	const FLayoutPartitionSeamRecord* EastSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MixedHeightParentChildJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MixedHeightParentChildJunctionRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MixedHeightParentChildEast")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosX
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegX
					&& SeamRecord.OwnerStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 0, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			});
	TestNotNull(
		TEXT("Mixed-height parent-child seam planner keeps the upper east parent-child seam"),
		EastSeam);
	if (!TestEqual(
			TEXT("Mixed-height parent-child seam planner emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Mixed-height parent-child junction keeps the parent owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("MixedHeightParentChildJunctionRoot")));
	TestEqual(
		TEXT("Mixed-height parent-child junction keeps the north child as the branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("MixedHeightParentChildNorth")));
	TestEqual(
		TEXT("Mixed-height parent-child junction keeps the east child as the continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("MixedHeightParentChildEast")));
	TestEqual(
		TEXT("Mixed-height parent-child junction keeps the upper-level owner corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Mixed-height parent-child junction keeps the east seam as the continuing seam"),
		JunctionRequirement.ContinuingSeamId,
		EastSeam != nullptr ? EastSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Mixed-height parent-child junction keeps the north seam as the branch seam"),
		JunctionRequirement.BranchSeamId,
		NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Mixed-height parent-child junction emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildSharedCorner.0.2")));
	TestFalse(
		TEXT("Focused mixed-height parent-child support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelParentChildJunctionRequirementFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest = MakeRootMultiLevelParentChildJunctionRequest();
	const FLayoutChildCapabilityEnvelope RootEnvelope =
		BuildChildCapabilityEnvelopeFromRequest(RootRequest);
	if (!TestTrue(
			TEXT("Multi-level parent-child root request derives parent-owned seam capabilities on both covered north levels before seam planning"),
			RootEnvelope.SeamCapabilities.ContainsByPredicate(
				[](const FLayoutChildCapabilitySeam& Capability)
				{
					return Capability.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
						&& Capability.FaceDirection == ELayoutFaceDirection::PosY
						&& Capability.bCanOwnSeam
						&& !Capability.bCanAcceptSeam
						&& (Capability.LocalCell == FIntVector(1, 1, 0)
							|| Capability.LocalCell == FIntVector(1, 1, 1));
				})))
	{
		return false;
	}

	RootRequest.bUseSuppliedChildCapabilityEnvelope = true;
	RootRequest.SuppliedChildCapabilityEnvelope = RootEnvelope;
	RootRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
		RootRequest.RegionDebugPath;
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult NorthChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelParentChildJunctionRoot"),
			TEXT("MultiLevelParentChildNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(1, 0, 0),
				FIntVector(1, 0, 1)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptLower"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptUpper"),
					FIntVector(1, 0, 1),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelParentChildJunctionRoot"),
			TEXT("MultiLevelParentChildEast"),
			FIntVector(1, 0, 1),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptUpper"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthChildResult, EastChildResult});

	if (!TestEqual(
			TEXT("Multi-level parent-child seam planner emits exactly the stacked north seam and upper east seam"),
			SeamPlan.PlannedPartitionSeams.Num(),
			2))
	{
		return false;
	}

	const FLayoutPartitionSeamRecord* NorthSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelParentChildJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelParentChildJunctionRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelParentChildNorth")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			});
	TestNotNull(
		TEXT("Multi-level parent-child seam planner keeps the stacked north seam"),
		NorthSeam);
	const FLayoutPartitionSeamRecord* EastSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelParentChildJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelParentChildJunctionRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelParentChildEast")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosX
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegX
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 1;
			});
	TestNotNull(
		TEXT("Multi-level parent-child seam planner keeps the upper east seam"),
		EastSeam);
	if (!TestEqual(
			TEXT("Multi-level parent-child seam planner emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level parent-child junction keeps the parent owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("MultiLevelParentChildJunctionRoot")));
	TestEqual(
		TEXT("Multi-level parent-child junction keeps the upper branch passive child"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("MultiLevelParentChildEast")));
	TestEqual(
		TEXT("Multi-level parent-child junction keeps the stacked continuing passive child"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("MultiLevelParentChildNorth")));
	TestEqual(
		TEXT("Multi-level parent-child junction keeps the upper owner-cell corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level parent-child junction keeps the stacked north seam as continuing"),
		JunctionRequirement.ContinuingSeamId,
		NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level parent-child junction keeps the upper east seam as branch"),
		JunctionRequirement.BranchSeamId,
		EastSeam != nullptr ? EastSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level parent-child junction emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildSharedCorner.2.0")));
	TestFalse(
		TEXT("Focused multi-level parent-child support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelParentChildEdgeAttachRequirementFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest = MakeRootMultiLevelParentChildEdgeAttachRequest();
	const FLayoutChildCapabilityEnvelope RootEnvelope =
		BuildChildCapabilityEnvelopeFromRequest(RootRequest);
	if (!TestTrue(
			TEXT("Multi-level parent-child edge-attach root request derives parent-owned seam capabilities on all stacked north levels before seam planning"),
			RootEnvelope.SeamCapabilities.ContainsByPredicate(
				[](const FLayoutChildCapabilitySeam& Capability)
				{
					return Capability.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
						&& Capability.FaceDirection == ELayoutFaceDirection::PosY
						&& Capability.bCanOwnSeam
						&& !Capability.bCanAcceptSeam
						&& (Capability.LocalCell == FIntVector(1, 1, 0)
							|| Capability.LocalCell == FIntVector(1, 1, 1)
							|| Capability.LocalCell == FIntVector(1, 1, 2));
				})))
	{
		return false;
	}

	RootRequest.bUseSuppliedChildCapabilityEnvelope = true;
	RootRequest.SuppliedChildCapabilityEnvelope = RootEnvelope;
	RootRequest.SuppliedChildCapabilityEnvelope.RegionDebugPath =
		RootRequest.RegionDebugPath;
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult NorthChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelParentChildEdgeAttachRoot"),
			TEXT("MultiLevelParentChildEdgeNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(1, 0, 0),
				FIntVector(1, 0, 1),
				FIntVector(1, 0, 2)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptLower"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptMiddle"),
					FIntVector(1, 0, 1),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptUpper"),
					FIntVector(1, 0, 2),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastChildResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelParentChildEdgeAttachRoot"),
			TEXT("MultiLevelParentChildEdgeEast"),
			FIntVector(1, 0, 1),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptMiddle"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthChildResult, EastChildResult});

	if (!TestEqual(
			TEXT("Multi-level parent-child edge-attach seam planner emits exactly the stacked north seam and middle east seam"),
			SeamPlan.PlannedPartitionSeams.Num(),
			2))
	{
		return false;
	}

	const FLayoutPartitionSeamRecord* NorthSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelParentChildEdgeAttachRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelParentChildEdgeAttachRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelParentChildEdgeNorth")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 2)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 2)
					&& SeamRecord.SegmentCount == 3;
			});
	TestNotNull(
		TEXT("Multi-level parent-child edge-attach seam planner keeps the three-level north seam"),
		NorthSeam);
	const FLayoutPartitionSeamRecord* EastSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelParentChildEdgeAttachRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelParentChildEdgeAttachRoot")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelParentChildEdgeEast")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosX
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegX
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 1;
			});
	TestNotNull(
		TEXT("Multi-level parent-child edge-attach seam planner keeps the middle east seam"),
		EastSeam);
	if (!TestEqual(
			TEXT("Multi-level parent-child edge-attach seam planner emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level parent-child edge-attach keeps the parent owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("MultiLevelParentChildEdgeAttachRoot")));
	TestEqual(
		TEXT("Multi-level parent-child edge-attach keeps the middle branch passive child"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("MultiLevelParentChildEdgeEast")));
	TestEqual(
		TEXT("Multi-level parent-child edge-attach keeps the stacked continuing passive child"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("MultiLevelParentChildEdgeNorth")));
	TestEqual(
		TEXT("Multi-level parent-child edge-attach keeps the middle owner-cell junction"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level parent-child edge-attach keeps the stacked north seam as continuing"),
		JunctionRequirement.ContinuingSeamId,
		NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level parent-child edge-attach keeps the middle east seam as branch"),
		JunctionRequirement.BranchSeamId,
		EastSeam != nullptr ? EastSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level parent-child edge-attach emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildEdgeAttach.2.0")));
	TestFalse(
		TEXT("Focused multi-level parent-child edge-attach support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelSiblingEdgeAttachRequirementFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("MultiLevelSiblingEdgeAttachRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelSiblingEdgeAttachRoot"),
			TEXT("MultiLevelSiblingEdgeOwner"),
			FIntVector(0, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(0, 0, 1),
				FIntVector(1, 0, 1),
				FIntVector(0, 1, 1),
				FIntVector(1, 1, 1),
				FIntVector(0, 0, 2),
				FIntVector(1, 0, 2),
				FIntVector(0, 1, 2),
				FIntVector(1, 1, 2)
			},
			{
				MakeSeamCapability(
					TEXT("OwnerNorthLower"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthMiddle"),
					FIntVector(1, 1, 1),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthUpper"),
					FIntVector(1, 1, 2),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerEastMiddle"),
					FIntVector(1, 1, 1),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelSiblingEdgeAttachRoot"),
			TEXT("MultiLevelSiblingEdgeNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(1, 0, 0),
				FIntVector(1, 0, 1),
				FIntVector(1, 0, 2)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptLower"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptMiddle"),
					FIntVector(1, 0, 1),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptUpper"),
					FIntVector(1, 0, 2),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelSiblingEdgeAttachRoot"),
			TEXT("MultiLevelSiblingEdgeEast"),
			FIntVector(1, 0, 1),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptMiddle"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, EastResult, OwnerResult});

	if (!TestEqual(
			TEXT("Multi-level sibling edge-attach seam planner emits exactly the stacked north seam and middle east seam"),
			SeamPlan.PlannedPartitionSeams.Num(),
			2))
	{
		return false;
	}

	const FLayoutPartitionSeamRecord* NorthSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelSiblingEdgeAttachRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelSiblingEdgeOwner")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelSiblingEdgeNorth")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 2)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 2)
					&& SeamRecord.SegmentCount == 3;
			});
	TestNotNull(
		TEXT("Multi-level sibling edge-attach seam planner keeps the three-level north seam"),
		NorthSeam);
	const FLayoutPartitionSeamRecord* EastSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelSiblingEdgeAttachRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelSiblingEdgeOwner")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelSiblingEdgeEast")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosX
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegX
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 1;
			});
	TestNotNull(
		TEXT("Multi-level sibling edge-attach seam planner keeps the middle east seam"),
		EastSeam);
	if (!TestEqual(
			TEXT("Multi-level sibling edge-attach seam planner emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("MultiLevelSiblingEdgeOwner")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the middle branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("MultiLevelSiblingEdgeEast")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the stacked continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("MultiLevelSiblingEdgeNorth")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the middle owner-cell junction"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the stacked north seam as continuing"),
		JunctionRequirement.ContinuingSeamId,
		NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the middle east seam as branch"),
		JunctionRequirement.BranchSeamId,
		EastSeam != nullptr ? EastSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level sibling edge-attach emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingEdgeAttach.2.0")));
	TestFalse(
		TEXT("Focused multi-level sibling edge-attach support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsSiblingMultiBranchJunctionRequirementsFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("SiblingMultiBranchRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchOwner"),
			FIntVector(0, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(2, 1, 0)
			},
			{
				MakeSeamCapability(
					TEXT("OwnerNorthA"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthB"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthC"),
					FIntVector(2, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerWestBranch"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::NegX),
				MakeSeamCapability(
					TEXT("OwnerEastBranch"),
					FIntVector(2, 1, 0),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptA"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptB"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptC"),
					FIntVector(2, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult WestResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchWest"),
			FIntVector(-1, 0, 0),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("WestAccept"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::PosX,
				false,
				true));
	const FNegotiatedDemandResult EastResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchEast"),
			FIntVector(3, 0, 0),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAccept"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, WestResult, EastResult, OwnerResult});

	if (!TestEqual(
			TEXT("Sibling multi-branch seam planner emits the continuing north seam plus both terminating branch seams"),
			SeamPlan.PlannedPartitionSeams.Num(),
			3))
	{
		return false;
	}

	const FLayoutPartitionSeamRecord* NorthSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("SiblingMultiBranchRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("SiblingMultiBranchOwner")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("SiblingMultiBranchNorth")
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(0, 1, 0)
					&& SeamRecord.OwnerEndCell == FIntVector(2, 1, 0)
					&& SeamRecord.SegmentCount == 3;
			});
	TestNotNull(
		TEXT("Sibling multi-branch seam planner keeps the continuing north seam"),
		NorthSeam);
	if (!TestEqual(
			TEXT("Sibling multi-branch seam planner emits two owner-side junction requirements"),
			SeamPlan.JunctionRequirements.Num(),
			2))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement* WestRequirement =
		SeamPlan.JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingMultiBranchOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingMultiBranchWest")
					&& Requirement.ContinuingPassiveRegionDebugPath == TEXT("SiblingMultiBranchNorth")
					&& Requirement.JunctionCell == FIntVector(0, 1, 0)
					&& Requirement.AdjacencyClassId == FLayoutId(TEXT("SiblingSharedCorner.2.1"));
			});
	TestNotNull(
		TEXT("Sibling seam planner keeps the west shared-corner requirement on the same continuing run"),
		WestRequirement);
	if (WestRequirement != nullptr)
	{
		TestEqual(
			TEXT("Sibling multi-branch west requirement keeps the continuing north seam"),
			WestRequirement->ContinuingSeamId,
			NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	}

	const FOwnedSeamJunctionRequirement* EastRequirement =
		SeamPlan.JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingMultiBranchOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingMultiBranchEast")
					&& Requirement.ContinuingPassiveRegionDebugPath == TEXT("SiblingMultiBranchNorth")
					&& Requirement.JunctionCell == FIntVector(2, 1, 0)
					&& Requirement.AdjacencyClassId == FLayoutId(TEXT("SiblingSharedCorner.2.0"));
			});
	TestNotNull(
		TEXT("Sibling seam planner keeps the east shared-corner requirement on the same continuing run"),
		EastRequirement);
	if (EastRequirement != nullptr)
	{
		TestEqual(
			TEXT("Sibling multi-branch east requirement keeps the continuing north seam"),
			EastRequirement->ContinuingSeamId,
			NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	}

	TestFalse(
		TEXT("Focused sibling multi-branch support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsSiblingRunWithInteriorEdgeAttachAndEndpointSharedCornerFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("SiblingTwoEdgeAttachRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoEdgeAttachRoot"),
			TEXT("SiblingTwoEdgeAttachOwner"),
			FIntVector(0, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0),
				FIntVector(3, 0, 0),
				FIntVector(4, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(2, 1, 0),
				FIntVector(3, 1, 0),
				FIntVector(4, 1, 0)
			},
			{
				MakeSeamCapability(
					TEXT("OwnerNorthA"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthB"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthC"),
					FIntVector(2, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthD"),
					FIntVector(3, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthE"),
					FIntVector(4, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerEastBranchA"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosX),
				MakeSeamCapability(
					TEXT("OwnerEastBranchB"),
					FIntVector(3, 1, 0),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoEdgeAttachRoot"),
			TEXT("SiblingTwoEdgeAttachNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0),
				FIntVector(3, 0, 0),
				FIntVector(4, 0, 0)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptA"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptB"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptC"),
					FIntVector(2, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptD"),
					FIntVector(3, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptE"),
					FIntVector(4, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastAResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoEdgeAttachRoot"),
			TEXT("SiblingTwoEdgeAttachEastA"),
			FIntVector(2, 0, 0),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptA"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));
	const FNegotiatedDemandResult EastBResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoEdgeAttachRoot"),
			TEXT("SiblingTwoEdgeAttachEastB"),
			FIntVector(4, 0, 0),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptB"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, EastAResult, EastBResult, OwnerResult});

	if (!TestEqual(
			TEXT("Sibling mixed edge-attach/shared-corner seam planner emits the continuing north seam plus both terminating east seams"),
			SeamPlan.PlannedPartitionSeams.Num(),
			3))
	{
		return false;
	}
	if (!TestEqual(
			TEXT("Sibling mixed edge-attach/shared-corner seam planner emits two owner-side junction requirements"),
			SeamPlan.JunctionRequirements.Num(),
			2))
	{
		return false;
	}
	const FOwnedSeamJunctionRequirement* FirstRequirement =
		SeamPlan.JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingTwoEdgeAttachOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingTwoEdgeAttachEastA");
			});
	TestNotNull(
		TEXT("Sibling mixed edge-attach/shared-corner seam planner keeps the first branch requirement"),
		FirstRequirement);
	if (FirstRequirement != nullptr)
	{
		TestEqual(
			TEXT("Sibling mixed edge-attach/shared-corner seam planner keeps the first branch as an interior edge-attach"),
			FirstRequirement->AdjacencyClassId,
			FLayoutId(TEXT("SiblingEdgeAttach.2.0")));
		TestEqual(
			TEXT("Sibling mixed edge-attach/shared-corner seam planner keeps the first branch on the first interior owner cell"),
			FirstRequirement->JunctionCell,
			FIntVector(1, 1, 0));
	}

	const FOwnedSeamJunctionRequirement* SecondRequirement =
		SeamPlan.JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingTwoEdgeAttachOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingTwoEdgeAttachEastB");
			});
	TestNotNull(
		TEXT("Sibling mixed edge-attach/shared-corner seam planner keeps the second branch requirement"),
		SecondRequirement);
	if (SecondRequirement != nullptr)
	{
		TestEqual(
			TEXT("Sibling mixed edge-attach/shared-corner seam planner keeps the endpoint branch as a shared-corner"),
			SecondRequirement->AdjacencyClassId,
			FLayoutId(TEXT("SiblingSharedCorner.2.0")));
		TestEqual(
			TEXT("Sibling mixed edge-attach/shared-corner seam planner places the endpoint branch on the continuing seam endpoint"),
			SecondRequirement->JunctionCell,
			FIntVector(4, 1, 0));
	}
	TestFalse(
		TEXT("Focused sibling mixed edge-attach/shared-corner support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsSiblingRunWithTwoInteriorEdgeAttachJunctionRequirementsFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("SiblingTwoInteriorEdgeAttachRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoInteriorEdgeAttachRoot"),
			TEXT("SiblingTwoInteriorEdgeAttachOwner"),
			FIntVector(0, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0),
				FIntVector(3, 0, 0),
				FIntVector(4, 0, 0),
				FIntVector(5, 0, 0),
				FIntVector(6, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(2, 1, 0),
				FIntVector(3, 1, 0),
				FIntVector(4, 1, 0),
				FIntVector(5, 1, 0),
				FIntVector(6, 1, 0)
			},
			{
				MakeSeamCapability(
					TEXT("OwnerNorth0"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorth1"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorth2"),
					FIntVector(2, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorth3"),
					FIntVector(3, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorth4"),
					FIntVector(4, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorth5"),
					FIntVector(5, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorth6"),
					FIntVector(6, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerEastBranchA"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosX),
				MakeSeamCapability(
					TEXT("OwnerEastBranchB"),
					FIntVector(5, 1, 0),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoInteriorEdgeAttachRoot"),
			TEXT("SiblingTwoInteriorEdgeAttachNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(2, 0, 0),
				FIntVector(3, 0, 0),
				FIntVector(4, 0, 0),
				FIntVector(5, 0, 0),
				FIntVector(6, 0, 0)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAccept0"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAccept1"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAccept2"),
					FIntVector(2, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAccept3"),
					FIntVector(3, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAccept4"),
					FIntVector(4, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAccept5"),
					FIntVector(5, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAccept6"),
					FIntVector(6, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastAResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoInteriorEdgeAttachRoot"),
			TEXT("SiblingTwoInteriorEdgeAttachEastA"),
			FIntVector(2, 0, 0),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptA"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));
	const FNegotiatedDemandResult EastBResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingTwoInteriorEdgeAttachRoot"),
			TEXT("SiblingTwoInteriorEdgeAttachEastB"),
			FIntVector(5, 0, 0),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptB"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, EastAResult, EastBResult, OwnerResult});

	if (!TestEqual(
			TEXT("Sibling dual interior edge-attach seam planner emits the continuing north seam plus both terminating east seams"),
			SeamPlan.PlannedPartitionSeams.Num(),
			3))
	{
		return false;
	}
	if (!TestEqual(
			TEXT("Sibling dual interior edge-attach seam planner emits two owner-side junction requirements"),
			SeamPlan.JunctionRequirements.Num(),
			2))
	{
		return false;
	}
	const FOwnedSeamJunctionRequirement* FirstRequirement =
		SeamPlan.JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingTwoInteriorEdgeAttachOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingTwoInteriorEdgeAttachEastA")
					&& Requirement.ContinuingPassiveRegionDebugPath == TEXT("SiblingTwoInteriorEdgeAttachNorth")
					&& Requirement.JunctionCell == FIntVector(1, 1, 0)
					&& Requirement.AdjacencyClassId == FLayoutId(TEXT("SiblingEdgeAttach.2.0"));
			});
	TestNotNull(
		TEXT("Sibling dual interior edge-attach seam planner keeps the first interior edge-attach requirement"),
		FirstRequirement);

	const FOwnedSeamJunctionRequirement* SecondRequirement =
		SeamPlan.JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingTwoInteriorEdgeAttachOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingTwoInteriorEdgeAttachEastB")
					&& Requirement.ContinuingPassiveRegionDebugPath == TEXT("SiblingTwoInteriorEdgeAttachNorth")
					&& Requirement.JunctionCell == FIntVector(4, 1, 0)
					&& Requirement.AdjacencyClassId == FLayoutId(TEXT("SiblingEdgeAttach.2.0"));
			});
	TestNotNull(
		TEXT("Sibling dual interior edge-attach seam planner keeps the farther interior edge-attach requirement"),
		SecondRequirement);

	TestFalse(
		TEXT("Focused sibling dual interior edge-attach support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelSiblingJunctionRequirementFromNegotiatedChildrenTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("MultiLevelSiblingJunctionRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelSiblingJunctionRoot"),
			TEXT("MultiLevelSiblingOwner"),
			FIntVector(0, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0),
				FIntVector(0, 0, 1),
				FIntVector(1, 0, 1),
				FIntVector(0, 1, 1),
				FIntVector(1, 1, 1)
			},
			{
				MakeSeamCapability(
					TEXT("OwnerNorthLower"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthUpper"),
					FIntVector(1, 1, 1),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerEastUpper"),
					FIntVector(1, 1, 1),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelSiblingJunctionRoot"),
			TEXT("MultiLevelSiblingNorth"),
			FIntVector(0, 1, 0),
			{
				FIntVector(1, 0, 0),
				FIntVector(1, 0, 1)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptLower"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY,
					false,
					true),
				MakeSeamCapability(
					TEXT("NorthAcceptUpper"),
					FIntVector(1, 0, 1),
					ELayoutFaceDirection::NegY,
					false,
					true)
			});
	const FNegotiatedDemandResult EastResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("MultiLevelSiblingJunctionRoot"),
			TEXT("MultiLevelSiblingEast"),
			FIntVector(1, 0, 1),
			{
				FIntVector(0, 1, 0)
			},
			MakeSeamCapability(
				TEXT("EastAcceptUpper"),
				FIntVector(0, 1, 0),
				ELayoutFaceDirection::NegX,
				false,
				true));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, EastResult, OwnerResult});

	if (!TestEqual(
			TEXT("Multi-level sibling seam planner emits exactly the stacked north seam and upper east seam"),
			SeamPlan.PlannedPartitionSeams.Num(),
			2))
	{
		return false;
	}

	const FLayoutPartitionSeamRecord* NorthSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelSiblingJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelSiblingOwner")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelSiblingNorth")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosY
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegY
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 0)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 2;
			});
	TestNotNull(
		TEXT("Multi-level sibling seam planner keeps the stacked north seam"),
		NorthSeam);
	const FLayoutPartitionSeamRecord* EastSeam =
		SeamPlan.PlannedPartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& SeamRecord)
			{
				return SeamRecord.ParentRegionDebugPath == TEXT("MultiLevelSiblingJunctionRoot")
					&& SeamRecord.OwnerRegionDebugPath == TEXT("MultiLevelSiblingOwner")
					&& SeamRecord.PassiveRegionDebugPath == TEXT("MultiLevelSiblingEast")
					&& SeamRecord.InterfaceFamily.MatchesTagExact(LayoutGameplayTags::InterfacePartitionSolid)
					&& SeamRecord.OwnerFaceDirection == ELayoutFaceDirection::PosX
					&& SeamRecord.PassiveFaceDirection == ELayoutFaceDirection::NegX
					&& SeamRecord.OwnerStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.OwnerEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveStartCell == FIntVector(1, 1, 1)
					&& SeamRecord.PassiveEndCell == FIntVector(1, 1, 1)
					&& SeamRecord.SegmentCount == 1;
			});
	TestNotNull(
		TEXT("Multi-level sibling seam planner keeps the upper east seam"),
		EastSeam);
	if (!TestEqual(
			TEXT("Multi-level sibling seam planner emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level sibling junction keeps the owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("MultiLevelSiblingOwner")));
	TestEqual(
		TEXT("Multi-level sibling junction keeps the upper branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("MultiLevelSiblingEast")));
	TestEqual(
		TEXT("Multi-level sibling junction keeps the stacked continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("MultiLevelSiblingNorth")));
	TestEqual(
		TEXT("Multi-level sibling junction keeps the upper owner-cell corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level sibling junction keeps the stacked north seam as continuing"),
		JunctionRequirement.ContinuingSeamId,
		NorthSeam != nullptr ? NorthSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level sibling junction keeps the upper east seam as branch"),
		JunctionRequirement.BranchSeamId,
		EastSeam != nullptr ? EastSeam->SeamId : NAME_None);
	TestEqual(
		TEXT("Multi-level sibling junction emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingSharedCorner.2.0")));
	TestFalse(
		TEXT("Focused multi-level sibling support stays non-authoritative while the seam bridge remains transitional"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerEmitsOwnerSideJunctionRequirementForSiblingSharedCornerTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest RootRequest;
	RootRequest.RegionDebugPath = TEXT("SiblingJunctionRoot");
	const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);

	const FNegotiatedDemandResult OwnerResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingJunctionRoot"),
			TEXT("SiblingJunctionZOwner"),
			FIntVector(0, 0, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0),
				FIntVector(0, 1, 0),
				FIntVector(1, 1, 0)
			},
			{
				MakeSeamCapability(
					TEXT("OwnerNorthA"),
					FIntVector(0, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerNorthB"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosY),
				MakeSeamCapability(
					TEXT("OwnerEastBranch"),
					FIntVector(1, 1, 0),
					ELayoutFaceDirection::PosX)
			});
	const FNegotiatedDemandResult NorthResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingJunctionRoot"),
			TEXT("SiblingJunctionNorth"),
			FIntVector(0, 2, 0),
			{
				FIntVector(0, 0, 0),
				FIntVector(1, 0, 0)
			},
			{
				MakeSeamCapability(
					TEXT("NorthAcceptA"),
					FIntVector(0, 0, 0),
					ELayoutFaceDirection::NegY),
				MakeSeamCapability(
					TEXT("NorthAcceptB"),
					FIntVector(1, 0, 0),
					ELayoutFaceDirection::NegY)
			});
	const FNegotiatedDemandResult EastResult =
		MakeSuccessfulSiblingSeamDemand(
			TEXT("SiblingJunctionRoot"),
			TEXT("SiblingJunctionEast"),
			FIntVector(2, 1, 0),
			{
				FIntVector(0, 0, 0)
			},
			MakeSeamCapability(
				TEXT("EastAccept"),
				FIntVector(0, 0, 0),
				ELayoutFaceDirection::NegX));

	const FNegotiatedSeamPlan SeamPlan =
		BuildNegotiatedSeamPlan(
			SolveContext,
			{NorthResult, EastResult, OwnerResult});

	if (!TestEqual(
			TEXT("First-pass sibling shared-corner fixture emits one owner-side junction requirement"),
			SeamPlan.JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		SeamPlan.JunctionRequirements[0];
	TestEqual(
		TEXT("Junction requirement keeps the seam owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("SiblingJunctionZOwner")));
	TestEqual(
		TEXT("Junction requirement keeps the terminating branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("SiblingJunctionEast")));
	TestEqual(
		TEXT("Junction requirement keeps the continuing-run passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("SiblingJunctionNorth")));
	TestEqual(
		TEXT("Junction requirement keeps the shared same-level owner cell"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 0));
	TestTrue(
		TEXT("Junction requirement keeps the shared interface family"),
		JunctionRequirement.InterfaceFamily.MatchesTagExact(
			LayoutGameplayTags::InterfacePartitionSolid));
	TestEqual(
		TEXT("Junction requirement keeps the continuing-run owner face direction"),
		static_cast<int32>(JunctionRequirement.ContinuingOwnerFaceDirection),
		static_cast<int32>(ELayoutFaceDirection::PosY));
	TestEqual(
		TEXT("Junction requirement keeps the terminating-branch owner face direction"),
		static_cast<int32>(JunctionRequirement.BranchOwnerFaceDirection),
		static_cast<int32>(ELayoutFaceDirection::PosX));
	TestTrue(
		TEXT("Junction requirement keeps a non-empty shared-corner adjacency class"),
		!JunctionRequirement.AdjacencyClassId.IsNone());
	TestFalse(
		TEXT("Current seam bridge still does not claim authoritative seam ownership while emitting junction requirements"),
		SeamPlan.bPlannedPartitionSeamsAreAuthoritative);
	return true;
}

bool FLayoutSeamPlannerBuildsOwnerSideJunctionRequirementForSiblingEdgeAttachTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeNorth"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			4),
		MakePartitionSeamRecord(
			TEXT("SiblingEastAttach"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeEast"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("First-pass owner-side junction planner emits one sibling edge-attach requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Sibling edge-attach requirement keeps the owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("SiblingEdgeOwner")));
	TestEqual(
		TEXT("Sibling edge-attach requirement keeps the terminating branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("SiblingEdgeEast")));
	TestEqual(
		TEXT("Sibling edge-attach requirement keeps the continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("SiblingEdgeNorth")));
	TestEqual(
		TEXT("Sibling edge-attach requirement keeps the interior owner junction cell"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 0));
	TestEqual(
		TEXT("Sibling edge-attach requirement keeps the edge-attach adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingEdgeAttach.2.0")));
	return true;
}

bool FLayoutSeamPlannerBuildsOwnerSideJunctionRequirementFromFragmentedSiblingEdgeAttachTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun_0"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeNorth"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(0, 1, 0),
			1),
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun_1"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeNorth"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 0),
			1),
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun_2"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeNorth"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(2, 1, 0),
			FIntVector(2, 1, 0),
			FIntVector(2, 1, 0),
			FIntVector(2, 1, 0),
			1),
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun_3"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeNorth"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(3, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(3, 1, 0),
			1),
		MakePartitionSeamRecord(
			TEXT("SiblingEastAttach_0"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeEast"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 0, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 0, 0),
			1),
		MakePartitionSeamRecord(
			TEXT("SiblingEastAttach_1"),
			TEXT("SiblingEdgeRoot"),
			TEXT("SiblingEdgeOwner"),
			TEXT("SiblingEdgeEast"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 0),
			1)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Fragmented sibling edge-attach seams still emit one normalized owner-side junction requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Fragmented sibling edge-attach requirement keeps the owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("SiblingEdgeOwner")));
	TestEqual(
		TEXT("Fragmented sibling edge-attach requirement keeps the terminating branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("SiblingEdgeEast")));
	TestEqual(
		TEXT("Fragmented sibling edge-attach requirement keeps the continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("SiblingEdgeNorth")));
	TestEqual(
		TEXT("Fragmented sibling edge-attach requirement keeps the interior owner junction cell"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 0));
	TestEqual(
		TEXT("Fragmented sibling edge-attach requirement keeps the same adjacency class as the non-fragmented case"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingEdgeAttach.2.0")));
	return true;
}

bool FLayoutSeamPlannerKeepsDisconnectedSiblingFragmentsAsSeparateRunsForJunctionMatchingTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun_A"),
			TEXT("SiblingDisconnectedRoot"),
			TEXT("SiblingDisconnectedOwner"),
			TEXT("SiblingNorthPassive"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(1, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(1, 1, 0),
			2),
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun_B"),
			TEXT("SiblingDisconnectedRoot"),
			TEXT("SiblingDisconnectedOwner"),
			TEXT("SiblingNorthPassive"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(3, 1, 0),
			FIntVector(4, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(4, 1, 0),
			2),
		MakePartitionSeamRecord(
			TEXT("SiblingEastAttach"),
			TEXT("SiblingDisconnectedRoot"),
			TEXT("SiblingDisconnectedOwner"),
			TEXT("SiblingEastPassive"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Disconnected same-passive sibling fragments still emit one junction requirement for the touched run"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("SiblingDisconnectedOwner")));
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the lexically selected continuing passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("SiblingNorthPassive")));
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the touched branch passive region instead of the disconnected fragment"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("SiblingEastPassive")));
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the touched owner-cell corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 0));
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the lexically selected continuing seam id"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("SiblingEastAttach")));
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the touched branch seam id instead of the disconnected fragment"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("SiblingNorthRun_A")));
	TestEqual(
		TEXT("Disconnected sibling-fragment junction keeps the shared-corner adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingSharedCorner.0.2")));
	return true;
}

bool FLayoutSeamPlannerKeepsDisconnectedParentChildFragmentsAsSeparateRunsForJunctionMatchingTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("ParentNorthRun_A"),
			TEXT("ParentDisconnectedRoot"),
			TEXT("ParentDisconnectedRoot"),
			TEXT("ParentNorthChild"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(1, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(1, 1, 0),
			2),
		MakePartitionSeamRecord(
			TEXT("ParentNorthRun_B"),
			TEXT("ParentDisconnectedRoot"),
			TEXT("ParentDisconnectedRoot"),
			TEXT("ParentNorthChild"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(3, 1, 0),
			FIntVector(4, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(4, 1, 0),
			2),
		MakePartitionSeamRecord(
			TEXT("ParentEastAttach"),
			TEXT("ParentDisconnectedRoot"),
			TEXT("ParentDisconnectedRoot"),
			TEXT("ParentEastChild"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Disconnected same-passive parent-child fragments still emit one junction requirement for the touched run"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the parent owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("ParentDisconnectedRoot")));
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the lexically selected continuing passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("ParentNorthChild")));
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the touched branch passive region instead of the disconnected fragment"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("ParentEastChild")));
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the touched owner-cell corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 0));
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the lexically selected continuing seam id"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("ParentEastAttach")));
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the touched branch seam id instead of the disconnected fragment"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("ParentNorthRun_A")));
	TestEqual(
		TEXT("Disconnected parent-child fragment junction keeps the parent-child shared-corner adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildSharedCorner.0.2")));
	return true;
}

bool FLayoutSeamPlannerBuildsOwnerSideJunctionRequirementForParentChildEdgeAttachTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("ParentNorthRun"),
			TEXT("ParentEdgeRoot"),
			TEXT("ParentEdgeRoot"),
			TEXT("ParentEdgeNorthChild"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			4),
		MakePartitionSeamRecord(
			TEXT("ParentEastAttach"),
			TEXT("ParentEdgeRoot"),
			TEXT("ParentEdgeRoot"),
			TEXT("ParentEdgeEastChild"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("First-pass owner-side junction planner emits one parent-child edge-attach requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Parent-child edge-attach requirement keeps the parent owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("ParentEdgeRoot")));
	TestEqual(
		TEXT("Parent-child edge-attach requirement keeps the terminating branch child"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("ParentEdgeEastChild")));
	TestEqual(
		TEXT("Parent-child edge-attach requirement keeps the continuing passive child"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("ParentEdgeNorthChild")));
	TestEqual(
		TEXT("Parent-child edge-attach requirement keeps the interior parent junction cell"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 0));
	TestEqual(
		TEXT("Parent-child edge-attach requirement keeps the explicit parent-child edge adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildEdgeAttach.2.0")));
	return true;
}

bool FLayoutSeamPlannerPrefersOwnerDefinedJunctionContractOverPassiveAmbiguityTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("Z_Run"),
			TEXT("OwnerDefinedJunctionRoot"),
			TEXT("OwnerDefinedJunctionOwner"),
			TEXT("Z_Passive"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			4),
		MakePartitionSeamRecord(
			TEXT("A_Branch"),
			TEXT("OwnerDefinedJunctionRoot"),
			TEXT("OwnerDefinedJunctionOwner"),
			TEXT("A_Passive"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Owner-defined junction contract fixture emits one junction requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Owner-defined junction contract keeps the owner-side continuing seam even when the branch seam sorts earlier lexically"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("Z_Run")));
	TestEqual(
		TEXT("Owner-defined junction contract keeps the owner-side branch seam even when the branch passive region sorts earlier lexically"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("A_Branch")));
	TestEqual(
		TEXT("Owner-defined junction contract keeps the owner-defined continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("Z_Passive")));
	TestEqual(
		TEXT("Owner-defined junction contract keeps the owner-defined terminating branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("A_Passive")));
	TestEqual(
		TEXT("Owner-defined junction contract still uses the sibling edge-attach adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingEdgeAttach.2.0")));
	return true;
}

bool FLayoutSeamPlannerBuildsParentChildSharedCornerJunctionRequirementTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("ParentNorthRun"),
			TEXT("ParentChildSeamRoot"),
			TEXT("ParentChildSeamRoot"),
			TEXT("ParentChildNorthChild"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(3, 1, 0),
			4),
		MakePartitionSeamRecord(
			TEXT("ParentEastBranch"),
			TEXT("ParentChildSeamRoot"),
			TEXT("ParentChildSeamRoot"),
			TEXT("ParentChildEastChild"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(3, 0, 0),
			FIntVector(3, 1, 0),
			FIntVector(3, 0, 0),
			FIntVector(3, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Owner-side junction planner emits one parent-owned parent-child shared-corner junction requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction keeps the parent owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("ParentChildSeamRoot")));
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction keeps the terminating branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("ParentChildEastChild")));
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction keeps the continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("ParentChildNorthChild")));
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction keeps the owner-cell corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(3, 1, 0));
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction keeps the continuing seam id"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("ParentNorthRun")));
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction keeps the branch seam id"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("ParentEastBranch")));
	TestEqual(
		TEXT("Parent-owned parent-child shared-corner junction emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildSharedCorner.2.0")));
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelParentChildSharedCornerJunctionRequirementTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("ParentNorthVerticalRun"),
			TEXT("ParentMultiLevelRoot"),
			TEXT("ParentMultiLevelRoot"),
			TEXT("ParentNorthStackedChild"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 1),
			2),
		MakePartitionSeamRecord(
			TEXT("ParentEastUpperBranch"),
			TEXT("ParentMultiLevelRoot"),
			TEXT("ParentMultiLevelRoot"),
			TEXT("ParentEastUpperChild"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			1)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Owner-side junction planner emits one multi-level parent-owned parent-child shared-corner requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner keeps the parent owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("ParentMultiLevelRoot")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner keeps the upper branch passive child"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("ParentEastUpperChild")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner keeps the stacked continuing passive child"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("ParentNorthStackedChild")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner keeps the upper owner-cell corner"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner keeps the vertical continuing seam id"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("ParentNorthVerticalRun")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner keeps the upper branch seam id"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("ParentEastUpperBranch")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child shared-corner emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildSharedCorner.2.0")));
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelParentChildEdgeAttachJunctionRequirementTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("ParentNorthVerticalRun"),
			TEXT("ParentMultiLevelEdgeRoot"),
			TEXT("ParentMultiLevelEdgeRoot"),
			TEXT("ParentNorthStackedChild"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 2),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 2),
			3),
		MakePartitionSeamRecord(
			TEXT("ParentEastMiddleBranch"),
			TEXT("ParentMultiLevelEdgeRoot"),
			TEXT("ParentMultiLevelEdgeRoot"),
			TEXT("ParentEastMiddleChild"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			1)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Owner-side junction planner emits one multi-level parent-owned parent-child edge-attach requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach keeps the parent owner"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("ParentMultiLevelEdgeRoot")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach keeps the middle branch passive child"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("ParentEastMiddleChild")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach keeps the stacked continuing passive child"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("ParentNorthStackedChild")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach keeps the middle owner-cell junction"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach keeps the vertical continuing seam id"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("ParentNorthVerticalRun")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach keeps the middle branch seam id"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("ParentEastMiddleBranch")));
	TestEqual(
		TEXT("Multi-level parent-owned parent-child edge-attach emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("ParentChildEdgeAttach.2.0")));
	return true;
}

bool FLayoutSeamPlannerBuildsMultiLevelSiblingEdgeAttachJunctionRequirementTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("SiblingNorthVerticalRun"),
			TEXT("SiblingMultiLevelEdgeRoot"),
			TEXT("SiblingMultiLevelEdgeOwner"),
			TEXT("SiblingNorthStackedPassive"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 2),
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 2),
			3),
		MakePartitionSeamRecord(
			TEXT("SiblingEastMiddleBranch"),
			TEXT("SiblingMultiLevelEdgeRoot"),
			TEXT("SiblingMultiLevelEdgeOwner"),
			TEXT("SiblingEastMiddlePassive"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			FIntVector(1, 1, 1),
			1)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Owner-side junction planner emits one multi-level sibling edge-attach requirement"),
			JunctionRequirements.Num(),
			1))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement& JunctionRequirement =
		JunctionRequirements[0];
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the owner region"),
		JunctionRequirement.OwnerRegionDebugPath,
		FString(TEXT("SiblingMultiLevelEdgeOwner")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the middle branch passive region"),
		JunctionRequirement.PassiveRegionDebugPath,
		FString(TEXT("SiblingEastMiddlePassive")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the stacked continuing passive region"),
		JunctionRequirement.ContinuingPassiveRegionDebugPath,
		FString(TEXT("SiblingNorthStackedPassive")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the middle owner-cell junction"),
		JunctionRequirement.JunctionCell,
		FIntVector(1, 1, 1));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the vertical continuing seam id"),
		JunctionRequirement.ContinuingSeamId,
		FLayoutId(TEXT("SiblingNorthVerticalRun")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach keeps the middle branch seam id"),
		JunctionRequirement.BranchSeamId,
		FLayoutId(TEXT("SiblingEastMiddleBranch")));
	TestEqual(
		TEXT("Multi-level sibling edge-attach emits the dedicated adjacency class"),
		JunctionRequirement.AdjacencyClassId,
		FLayoutId(TEXT("SiblingEdgeAttach.2.0")));
	return true;
}

bool FLayoutSeamPlannerBuildsTwoSiblingEdgeAttachJunctionRequirementsTest::RunTest(const FString& Parameters)
{
	const TArray<FLayoutPartitionSeamRecord> PlannedPartitionSeams = {
		MakePartitionSeamRecord(
			TEXT("SiblingNorthRun"),
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchOwner"),
			TEXT("SiblingMultiBranchNorth"),
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY,
			FIntVector(0, 1, 0),
			FIntVector(2, 1, 0),
			FIntVector(0, 1, 0),
			FIntVector(2, 1, 0),
			3),
		MakePartitionSeamRecord(
			TEXT("SiblingWestBranch"),
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchOwner"),
			TEXT("SiblingMultiBranchWest"),
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosX,
			FIntVector(0, 0, 0),
			FIntVector(0, 1, 0),
			FIntVector(0, 0, 0),
			FIntVector(0, 1, 0),
			2),
		MakePartitionSeamRecord(
			TEXT("SiblingEastBranch"),
			TEXT("SiblingMultiBranchRoot"),
			TEXT("SiblingMultiBranchOwner"),
			TEXT("SiblingMultiBranchEast"),
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			FIntVector(2, 0, 0),
			FIntVector(2, 1, 0),
			FIntVector(2, 0, 0),
			FIntVector(2, 1, 0),
			2)
	};

	const TArray<FOwnedSeamJunctionRequirement> JunctionRequirements =
		BuildOwnerSideJunctionRequirements(PlannedPartitionSeams);

	if (!TestEqual(
			TEXT("Owner-side junction planner emits two sibling edge-attach requirements when one continuing seam has two terminating branches"),
			JunctionRequirements.Num(),
			2))
	{
		return false;
	}

	const FOwnedSeamJunctionRequirement* WestRequirement =
		JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingMultiBranchOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingMultiBranchWest")
					&& Requirement.ContinuingPassiveRegionDebugPath == TEXT("SiblingMultiBranchNorth")
					&& Requirement.JunctionCell == FIntVector(0, 1, 0)
					&& Requirement.ContinuingSeamId == FLayoutId(TEXT("SiblingNorthRun"))
					&& Requirement.BranchSeamId == FLayoutId(TEXT("SiblingWestBranch"))
					&& Requirement.AdjacencyClassId == FLayoutId(TEXT("SiblingSharedCorner.2.1"));
			});
	TestNotNull(
		TEXT("Sibling continuing-run junctions keep the west shared-corner requirement"),
		WestRequirement);

	const FOwnedSeamJunctionRequirement* EastRequirement =
		JunctionRequirements.FindByPredicate(
			[](const FOwnedSeamJunctionRequirement& Requirement)
			{
				return Requirement.OwnerRegionDebugPath == TEXT("SiblingMultiBranchOwner")
					&& Requirement.PassiveRegionDebugPath == TEXT("SiblingMultiBranchEast")
					&& Requirement.ContinuingPassiveRegionDebugPath == TEXT("SiblingMultiBranchNorth")
					&& Requirement.JunctionCell == FIntVector(2, 1, 0)
					&& Requirement.ContinuingSeamId == FLayoutId(TEXT("SiblingNorthRun"))
					&& Requirement.BranchSeamId == FLayoutId(TEXT("SiblingEastBranch"))
					&& Requirement.AdjacencyClassId == FLayoutId(TEXT("SiblingSharedCorner.2.0"));
			});
	TestNotNull(
		TEXT("Sibling continuing-run junctions keep the east shared-corner requirement"),
		EastRequirement);
	return true;
}
