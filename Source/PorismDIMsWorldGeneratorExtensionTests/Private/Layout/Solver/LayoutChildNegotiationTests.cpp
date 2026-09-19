// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
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

	UObject* CreateChildNegotiationTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}




	FLayoutRegionSolveRequest BuildSingleCellRequest(
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const FIntVector& Cell,
		const ELayoutCellIntent Intent = ELayoutCellIntent::Boundary)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FIntPoint(1, 1);
		FLayoutPlannedCell& PlannedCell = Request.PlannedCells.AddDefaulted_GetRef();
		PlannedCell.Cell = Cell;
		PlannedCell.Intent = Intent;
		return Request;
	}

	FLayoutRegionSolveRequest BuildSuppliedRequest(
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FootprintSize;
		Request.PlannedCells = PlannedCells;
		return Request;
	}

	FLayoutRegionSolveRequest BuildSuppliedRequest(
		ULayoutRegionContentSetAsset* ContentSet,
		ULayoutProfileAsset* Profile,
		const int32 Seed,
		const FString& RegionPath,
		const FIntPoint& FootprintSize,
		const TArray<FLayoutPlannedCell>& PlannedCells)
	{
		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(ContentSet, Profile, Seed, RegionPath);
		/* !Request.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
		Request.FootprintSize = FootprintSize;
		Request.PlannedCells = PlannedCells;
		return Request;
	}

	/** Builds one snapshot-backed result for shared evaluator tests without child scheduling. */
	FLayoutRegionSolveResult BuildSyntheticClosureAuditResult(const FLayoutRegionSolveRequest& Request)
	{
		FLayoutRegionSolveResult Result;
		Result.RegionDebugPath = Request.RegionDebugPath;
		Result.SolveResult.bSucceeded = true;
		Result.SolveResult.PlannedCells = Request.PlannedCells;
		if (Request.PlannedCells.IsEmpty() || Request.ModuleCatalog.Modules.IsEmpty())
		{
			return Result;
		}

		const FLayoutModuleSolveSnapshot& Snapshot = Request.ModuleCatalog.Modules[0];
		FLayoutPlacedModule& Placement = Result.SolveResult.Placements.AddDefaulted_GetRef();
		Placement.Cell = Request.PlannedCells[0].Cell;
		Placement.Intent = Request.PlannedCells[0].Intent;
		Placement.Module = Snapshot.SourceModule;
		Placement.CompositeModule = Snapshot.SourceCompositeModule;
		Placement.SourceContentEntryId = Snapshot.SourceContentEntryId;
		Placement.ModuleSnapshotId = Snapshot.SnapshotId;
		Placement.ModuleSnapshotIndex = 0;
		Placement.BundleBoundsCells = Snapshot.BoundsCells;
		Placement.OccupiedLocalCells = Snapshot.OccupiedLocalCells;
		if (Placement.OccupiedLocalCells.IsEmpty())
		{
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
		return Result;
	}

	ULayoutRegionContentSetAsset* CreateStackedChildTraversalContentSet(UObject* Outer)
	{
		UChunkStructureTemplate* LowerEntryTemplate =
			CreateTemplate(Outer, TEXT("StackedChildLowerEntryTemplate"), FIntVector(8, 8, 8));
		UChunkStructureTemplate* UpperEntryTemplate =
			CreateTemplate(Outer, TEXT("StackedChildUpperEntryTemplate"), FIntVector(8, 8, 8));
		UChunkStructureTemplate* StairTemplate =
			CreateTemplate(Outer, TEXT("StackedChildStairTemplate"), FIntVector(8, 8, 8));
		UChunkStructureTemplate* FillerTemplate =
			CreateTemplate(Outer, TEXT("StackedChildFillerTemplate"), FIntVector(8, 8, 8));
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenAndEntryTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer OpenSolidAndEntryTags =
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid, LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
		const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});

		ULayoutModuleAsset* LowerEntry = CreateModule(
			Outer,
			TEXT("StackedChildLowerEntry"),
			LowerEntryTemplate,
			{ELayoutCellIntent::Entry},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), OpenAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, MakeTags({LayoutGameplayTags::FaceEntry}), OpenAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
			});
		LowerEntry->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		LowerEntry->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		LowerEntry->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		LowerEntry->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutModuleAsset* UpperEntry = CreateModule(
			Outer,
			TEXT("StackedChildUpperEntry"),
			UpperEntryTemplate,
			{ELayoutCellIntent::Connector, ELayoutCellIntent::Interior},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, PrimaryTraversal)
			});
		UpperEntry->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		UpperEntry->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		UpperEntry->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		UpperEntry->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutModuleAsset* Stair = CreateModule(
			Outer,
			TEXT("StackedChildVerticalAccess"),
			StairTemplate,
			{ELayoutCellIntent::VerticalAccess},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, OpenTags, OpenAndEntryTags, ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor)
			});
		Stair->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		Stair->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		Stair->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutModuleAsset* Filler = CreateModule(
			Outer,
			TEXT("StackedChildFiller"),
			FillerTemplate,
			{
				ELayoutCellIntent::Boundary,
				ELayoutCellIntent::Core,
				ELayoutCellIntent::Interior,
				ELayoutCellIntent::Connector
			},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});

		FLayoutRegionContentEntry LowerEntryRow;
		LowerEntryRow.EntryId = TEXT("LowerEntry");
		LowerEntryRow.ContentKind = ELayoutRegionContentKind::Module;
		LowerEntryRow.ModuleSettings.Module = LowerEntry;
		LowerEntryRow.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
		LowerEntryRow.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		LowerEntryRow.ModuleSettings.SpecificLevel = 0;

		FLayoutRegionContentEntry UpperEntryRow;
		UpperEntryRow.EntryId = TEXT("UpperEntry");
		UpperEntryRow.ContentKind = ELayoutRegionContentKind::Module;
		UpperEntryRow.ModuleSettings.Module = UpperEntry;
		UpperEntryRow.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;
		UpperEntryRow.ModuleSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
		UpperEntryRow.ModuleSettings.SpecificLevel = 1;

		FLayoutRegionContentEntry StairRow;
		StairRow.EntryId = TEXT("Stair");
		StairRow.ContentKind = ELayoutRegionContentKind::Module;
		StairRow.ModuleSettings.Module = Stair;
		StairRow.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;

		FLayoutRegionContentEntry FillerRow;
		FillerRow.EntryId = TEXT("Filler");
		FillerRow.ContentKind = ELayoutRegionContentKind::Module;
		FillerRow.ModuleSettings.Module = Filler;
		FillerRow.ModuleSettings.PlacementZone = ELayoutPlacementZone::Any;

		return CreateRegionContentSet(
			Outer,
			TEXT("StackedChildTraversalContentSet"),
			{LowerEntryRow, UpperEntryRow, StairRow, FillerRow});
	}

	TArray<FLayoutPlannedCell> BuildResidualParentCells()
	{
		TArray<FLayoutPlannedCell> PlannedCells;
		for (int32 Level = 0; Level < 2; ++Level)
		{
			for (int32 Y = 0; Y < 3; ++Y)
			{
				for (int32 X = 0; X < 5; ++X)
				{
					const FIntVector Cell(X, Y, Level);
					if (Cell == FIntVector(1, 1, 0) || Cell == FIntVector(2, 1, 0) || Cell == FIntVector(2, 1, 1))
					{
						continue;
					}

					FLayoutPlannedCell& PlannedCell = PlannedCells.AddDefaulted_GetRef();
					PlannedCell.Cell = Cell;
					const bool bBoundary = X == 0 || Y == 0 || X == 4 || Y == 2;
					PlannedCell.Intent = bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
				}
			}
		}

		for (FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			if (PlannedCell.Cell == FIntVector(0, 1, 0))
			{
				PlannedCell.Intent = ELayoutCellIntent::Entry;
				break;
			}
		}

		return PlannedCells;
	}

	TArray<FLayoutPlannedCell> BuildResidualParentCellsWithComposedProvider()
	{
		TArray<FLayoutPlannedCell> PlannedCells = BuildResidualParentCells();
		for (FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			if (PlannedCell.Cell == FIntVector(3, 1, 0))
			{
				PlannedCell.Intent = ELayoutCellIntent::VerticalAccess;
				break;
			}
		}
		return PlannedCells;
	}

	TArray<FLayoutPlannedCell> BuildStackedChildCells()
	{
		TArray<FLayoutPlannedCell> PlannedCells;
		FLayoutPlannedCell& LowerEntry = PlannedCells.AddDefaulted_GetRef();
		LowerEntry.Cell = FIntVector(0, 0, 0);
		LowerEntry.Intent = ELayoutCellIntent::Entry;

		FLayoutPlannedCell& Stair = PlannedCells.AddDefaulted_GetRef();
		Stair.Cell = FIntVector(1, 0, 0);
		Stair.Intent = ELayoutCellIntent::VerticalAccess;

		FLayoutPlannedCell& UpperEntry = PlannedCells.AddDefaulted_GetRef();
		UpperEntry.Cell = FIntVector(1, 0, 1);
		UpperEntry.Intent = ELayoutCellIntent::Connector;

		return PlannedCells;
	}

	void AddOuterPerimeterClosure(ULayoutProfileAsset* Profile)
	{
		FLayoutClosureRequirement& ClosureRequirement = Profile->ClosureRequirements.AddDefaulted_GetRef();
		ClosureRequirement.ClosureId = TEXT("OuterPerimeter");
		ClosureRequirement.BoundsPolicy.Mode = ELayoutBoundsPolicyMode::SolvedFootprint;
		ClosureRequirement.BoundsPolicy.InsetCells = 0;
		ClosureRequirement.BoundsPolicy.MinLevel = 0;
		ClosureRequirement.BoundsPolicy.MaxLevel = 0;
		ClosureRequirement.MinThicknessCells = 1;
			}

	FLayoutNegotiatedChildResponsibilityContract BuildNegotiatedContractForValidation(
		const ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility)
	{
		FLayoutNegotiatedChildResponsibilityContract Contract;
		Contract.ParentRegionDebugPath = TEXT("Parent");
		Contract.ChildRegionDebugPath = TEXT("Child");
		Contract.HostVerticalAccessResponsibility = Responsibility;
		Contract.RequiredHostProviderCount = 2;

		FLayoutNegotiatedLevelCellSet& GroundReplacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
		GroundReplacement.Level = 0;
		GroundReplacement.Cells = {
			FIntVector(1, 1, 0)
		};

		FLayoutNegotiatedLevelCellSet& UpperReplacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
		UpperReplacement.Level = 1;
		UpperReplacement.Cells = {
			FIntVector(1, 1, 1)
		};

		Contract.bHasRequiredHostIngressAnchor = true;
		Contract.RequiredHostIngressAnchor.CommitmentId = TEXT("LowerDoor");
		Contract.RequiredHostIngressAnchor.LocalCell = FIntVector(1, 1, 0);
		Contract.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegX;
		Contract.RequiredHostIngressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

		Contract.bHasRequiredHostEgressAnchor = true;
		Contract.RequiredHostEgressAnchor.CommitmentId = TEXT("UpperDoor");
		Contract.RequiredHostEgressAnchor.LocalCell = FIntVector(1, 1, 1);
		Contract.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
		Contract.RequiredHostEgressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
		Contract.RequiredChildGenerallyConnectableAnchorPairId = TEXT("LowerDoor__UpperDoor");
		Contract.RequiredChildInternalVerticalSpanLevels = {0, 1};

		Contract.RequiredChildInternalVerticalRouteCells = {
			FIntVector(1, 1, 0),
			FIntVector(1, 1, 1)
		};
		FLayoutNegotiatedLevelInterfaceContract& LowerInterface =
			Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
		LowerInterface.Level = 0;
		LowerInterface.EndpointAnchors = {Contract.RequiredHostIngressAnchor};
		FLayoutNegotiatedLevelInterfaceContract& UpperInterface =
			Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
		UpperInterface.Level = 1;
		UpperInterface.EndpointAnchors = {Contract.RequiredHostEgressAnchor};

		return Contract;
	}

	void ConfigureDirectProofContract(
		FLayoutNegotiatedChildResponsibilityContract& Contract,
		const ELayoutNegotiatedHostVerticalAccessResponsibility Responsibility,
		const int32 RequiredHostProviderCount,
		const int32 CountedParentProviderCount,
		const TArray<FIntVector>& CountedParentVerticalAccessCells,
		const TArray<FString>& CountedChildProviderRegionDebugPaths)
	{
		Contract = BuildNegotiatedContractForValidation(Responsibility);
		Contract.ParentRegionDebugPath = TEXT("Parent");
		Contract.ChildRegionDebugPath = TEXT("Child");
		Contract.RequiredHostProviderCount = RequiredHostProviderCount;
		Contract.CountedParentProviderCount = CountedParentProviderCount;
		Contract.CountedParentVerticalAccessCells = CountedParentVerticalAccessCells;
		Contract.CountedChildProviderRegionDebugPaths = CountedChildProviderRegionDebugPaths;
		Contract.ReplacementVolumeByLevel.Reset();

		FLayoutNegotiatedLevelCellSet& LowerReplacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
		LowerReplacement.Level = 0;
		LowerReplacement.Cells = {FIntVector(1, 1, 0), FIntVector(2, 1, 0)};

		FLayoutNegotiatedLevelCellSet& UpperReplacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
		UpperReplacement.Level = 1;
		UpperReplacement.Cells = {FIntVector(2, 1, 1)};

		Contract.bHasRequiredHostIngressAnchor = true;
		Contract.RequiredHostIngressAnchor.CommitmentId = TEXT("LowerHostDoor");
		Contract.RequiredHostIngressAnchor.LocalCell = FIntVector(0, 0, 0);
		Contract.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegX;
		Contract.RequiredHostIngressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
		Contract.RequiredHostIngressAnchor.AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceEntry, LayoutGameplayTags::FaceOpen});
		Contract.RequiredHostIngressAnchor.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});

		Contract.bHasRequiredHostEgressAnchor = true;
		Contract.RequiredHostEgressAnchor.CommitmentId = TEXT("UpperHostDoor");
		Contract.RequiredHostEgressAnchor.LocalCell = FIntVector(1, 0, 1);
		Contract.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
		Contract.RequiredHostEgressAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
		Contract.RequiredHostEgressAnchor.AllowedConnectionTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid});
		Contract.RequiredHostEgressAnchor.TraversalChannels = MakeTags({LayoutGameplayTags::TraversalPrimary});
		Contract.RequiredChildGenerallyConnectableAnchorPairId = TEXT("LowerHostDoor__UpperHostDoor");
		Contract.RequiredChildInternalVerticalSpanLevels = {0, 1};

		Contract.RequiredChildInternalVerticalRouteCells = {
			FIntVector(0, 0, 0),
			FIntVector(1, 0, 0),
			FIntVector(1, 0, 1)
		};
		Contract.CommittedParentChildInterfacesByLevel.Reset();

		FLayoutNegotiatedLevelInterfaceContract& LowerInterface = Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
		LowerInterface.Level = 0;
		LowerInterface.EndpointAnchors = {Contract.RequiredHostIngressAnchor};

		FLayoutNegotiatedLevelInterfaceContract& UpperInterface = Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
		UpperInterface.Level = 1;
		UpperInterface.EndpointAnchors = {Contract.RequiredHostEgressAnchor};
	}

	bool ValidateDirectProofContract(
		FAutomationTestBase& Test,
		const TCHAR* Label,
		const FLayoutNegotiatedChildResponsibilityContract& Contract)
	{
		FString ContractFailureReason;
		const bool bContractValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
			Contract,
			ContractFailureReason);
		Test.TestTrue(Label, bContractValid);
		if (!bContractValid)
		{
			Test.AddError(ContractFailureReason);
		}
		return bContractValid;
	}

	FLayoutRegionSolveScheduleRequest BuildDirectCommittedScheduleRequest(
		const FLayoutRegionSolveRequest& ParentRequest,
		const FLayoutRegionSolveRequest& ChildRequest,
		const FLayoutNegotiatedChildResponsibilityContract& Contract)
	{
		FLayoutRegionSolveScheduleRequest ScheduleRequest;
		ScheduleRequest.RegionRequests = {ParentRequest, ChildRequest};
		ScheduleRequest.Dependencies.Add({TEXT("Parent"), TEXT("Child")});

		FLayoutDirectChildRegionCommitment& Commitment = ScheduleRequest.DirectChildCommitments.AddDefaulted_GetRef();
		Commitment.ParentRegionDebugPath = TEXT("Parent");
		Commitment.ChildRegionDebugPath = TEXT("Child");
		Commitment.EndpointCommitments = {
			Contract.RequiredHostIngressAnchor,
			Contract.RequiredHostEgressAnchor
		};
		Commitment.NegotiatedResponsibilityContract = Contract;
		return ScheduleRequest;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationDeferredParentClosureAuditTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.DeferredParentClosureAudit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationDeferredParentClosureAuditTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateChildNegotiationTestOuter(TEXT("LayoutChildDeferredClosureAudit"));

	ULayoutProfileAsset* ParentProfile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("DeferredParentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* ChildProfile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("DeferredChildProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ParentProfile->ContentSet->Entries.SetNum(1);
	ChildProfile->ContentSet->Entries.SetNum(1);
	AddOuterPerimeterClosure(ParentProfile);

	ULayoutRegionContentSetAsset* ChildContentSet = ChildProfile->ContentSet;
	FLayoutClosureProviderIntent& ProviderIntent = ChildContentSet->Entries[0].ClosureProviderIntents.AddDefaulted_GetRef();
	ProviderIntent.ProviderIntentId = TEXT("GatePerimeterProvider");
	ProviderIntent.ClosureId = TEXT("OuterPerimeter");

	FLayoutRegionSolveRequest ParentRequest = BuildSingleCellRequest(ParentProfile, 301, TEXT("Parent"), FIntVector(0, 0, 0), ELayoutCellIntent::Entry);
	FLayoutRegionSolveRequest ChildRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(ChildContentSet, ChildProfile, 302, TEXT("Child"));
	/* !ChildRequest.PlannedCells.IsEmpty() removed — derive from !PlannedCells.IsEmpty() */
	ChildRequest.FootprintSize = FIntPoint(1, 1);
	FLayoutPlannedCell& ChildPlannedCell = ChildRequest.PlannedCells.AddDefaulted_GetRef();
	ChildPlannedCell.Cell = FIntVector(1, 0, 0);
	ChildPlannedCell.Intent = ELayoutCellIntent::Entry;

	FLayoutRegionSolveScheduleResult ScheduleResult;
	ScheduleResult.RegionResults = {
		BuildSyntheticClosureAuditResult(ParentRequest),
		BuildSyntheticClosureAuditResult(ChildRequest)
	};
	const TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath = {
		{TEXT("Parent"), &ParentRequest},
		{TEXT("Child"), &ChildRequest}
	};
	const TMap<FString, int32> RegionResultIndexByPath = {
		{TEXT("Parent"), 0},
		{TEXT("Child"), 1}
	};
	const TMap<FString, TArray<FString>> ChildrenByParent = {
		{TEXT("Parent"), {TEXT("Child")}}
	};
	ScheduleResult.bSucceeded = LayoutProfileSolverInternal::ReevaluateDeferredClosureCoverageForRegion(
		TEXT("Parent"),
		RequestsByPath,
		RegionResultIndexByPath,
		ChildrenByParent,
		ScheduleResult,
		ScheduleResult.FailureReason);
	TestTrue(TEXT("Committed parent-child schedule with deferred closure audit succeeds"), ScheduleResult.bSucceeded);
	TestEqual(TEXT("Two region results are returned"), ScheduleResult.RegionResults.Num(), 2);
	if (!ScheduleResult.bSucceeded || ScheduleResult.RegionResults.Num() < 2)
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* ParentResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& Result)
	{
		return Result.RegionDebugPath == TEXT("Parent");
	});
	TestNotNull(TEXT("Parent result is present"), ParentResult);
	if (ParentResult == nullptr)
	{
		return false;
	}

	TestEqual(TEXT("Deferred parent closure audit emits one closure summary"), ParentResult->SolveResult.ClosureCoverage.Num(), 1);
	if (ParentResult->SolveResult.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestTrue(TEXT("Deferred parent closure audit is satisfied"), ParentResult->SolveResult.ClosureCoverage[0].bSatisfied);
	TestEqual(TEXT("Union of parent and child planned cells yields six required perimeter segments"), ParentResult->SolveResult.ClosureCoverage[0].RequiredSegmentCount, 6);
	TestEqual(TEXT("Every deferred-audit segment is covered"), ParentResult->SolveResult.ClosureCoverage[0].CoveredSegmentCount, 6);
	TestEqual(TEXT("The two-cell line perimeter groups into two long side runs and four end-cap runs"), ParentResult->SolveResult.ClosureRuns.Num(), 6);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationPassiveSeamClosureAuditRequiresAuthorizedOwnerCreditTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.PassiveSeamClosureAuditRequiresAuthorizedOwnerCredit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationPassiveSeamClosureAuditRequiresAuthorizedOwnerCreditTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	UObject* Outer = CreateChildNegotiationTestOuter(TEXT("LayoutPassiveSeamClosureAudit"));
	ULayoutProfileAsset* OwnerProfile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("PassiveSeamOwnerProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* PassiveProfile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("PassiveSeamPassiveProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	OwnerProfile->ContentSet->Entries.SetNum(1);
	PassiveProfile->ContentSet->Entries.SetNum(1);
	AddOuterPerimeterClosure(PassiveProfile);

	FLayoutSeamProviderIntent& OwnerSeamIntent = OwnerProfile->ContentSet->Entries[0].SeamProviderIntents.AddDefaulted_GetRef();
	OwnerSeamIntent.SeamIntentId = TEXT("OwnerWall");
	OwnerSeamIntent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	OwnerSeamIntent.bCanOwnSeam = true;

	FLayoutRegionSolveRequest LeftOwnerRequest = BuildSingleCellRequest(
		OwnerProfile, 401, TEXT("LeftOwner"), FIntVector(0, 1, 0));
	FLayoutRegionSolveRequest RightOwnerRequest = BuildSingleCellRequest(
		OwnerProfile, 402, TEXT("RightOwner"), FIntVector(2, 1, 0));
	FLayoutRegionSolveRequest BottomOwnerRequest = BuildSingleCellRequest(
		OwnerProfile, 403, TEXT("BottomOwner"), FIntVector(1, 0, 0));
	FLayoutRegionSolveRequest TopOwnerRequest = BuildSingleCellRequest(
		OwnerProfile, 404, TEXT("TopOwner"), FIntVector(1, 2, 0));
	FLayoutRegionSolveRequest PassiveRequest = BuildSingleCellRequest(
		PassiveProfile, 405, TEXT("Passive"), FIntVector(1, 1, 0));
	for (FLayoutModuleSolveSnapshot& Snapshot : PassiveRequest.ModuleCatalog.Modules)
	{
		Snapshot.DerivedSpanOffers.Reset();
	}

	const auto MakeSeam = [](const TCHAR* OwnerPath, const FIntVector& OwnerCell, const ELayoutFaceDirection OwnerFace, const ELayoutFaceDirection PassiveFace)
	{
		FLayoutPartitionSeamRecord Seam;
		Seam.SeamId = FLayoutId(OwnerPath);
		Seam.OwnerRegionDebugPath = OwnerPath;
		Seam.PassiveRegionDebugPath = TEXT("Passive");
		Seam.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		Seam.OwnerFaceDirection = OwnerFace;
		Seam.PassiveFaceDirection = PassiveFace;
		Seam.OwnerStartCell = Seam.OwnerEndCell = OwnerCell;
		Seam.PassiveStartCell = Seam.PassiveEndCell = FIntVector(1, 1, 0);
		Seam.SegmentCount = 1;
		Seam.bCountsTowardClosure = true;
		return Seam;
	};
	TArray<FLayoutPartitionSeamRecord> Seams = {
		MakeSeam(TEXT("LeftOwner"), FIntVector(0, 1, 0), ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX),
		MakeSeam(TEXT("RightOwner"), FIntVector(2, 1, 0), ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosX),
		MakeSeam(TEXT("BottomOwner"), FIntVector(1, 0, 0), ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY),
		MakeSeam(TEXT("TopOwner"), FIntVector(1, 2, 0), ELayoutFaceDirection::NegY, ELayoutFaceDirection::PosY)
	};

	const TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath = {
		{TEXT("LeftOwner"), &LeftOwnerRequest},
		{TEXT("RightOwner"), &RightOwnerRequest},
		{TEXT("BottomOwner"), &BottomOwnerRequest},
		{TEXT("TopOwner"), &TopOwnerRequest},
		{TEXT("Passive"), &PassiveRequest}
	};
	const TMap<FString, int32> RegionResultIndexByPath = {
		{TEXT("LeftOwner"), 0},
		{TEXT("RightOwner"), 1},
		{TEXT("BottomOwner"), 2},
		{TEXT("TopOwner"), 3},
		{TEXT("Passive"), 4}
	};
	const auto BuildScheduleResult = [&LeftOwnerRequest, &RightOwnerRequest, &BottomOwnerRequest, &TopOwnerRequest, &PassiveRequest]()
	{
		FLayoutRegionSolveScheduleResult Result;
		Result.RegionResults = {
			BuildSyntheticClosureAuditResult(LeftOwnerRequest),
			BuildSyntheticClosureAuditResult(RightOwnerRequest),
			BuildSyntheticClosureAuditResult(BottomOwnerRequest),
			BuildSyntheticClosureAuditResult(TopOwnerRequest),
			BuildSyntheticClosureAuditResult(PassiveRequest)
		};
		return Result;
	};

	Seams[0].bCountsTowardClosure = false;
	FLayoutRegionSolveScheduleResult UncreditedResult = BuildScheduleResult();
	FString FailureReason;
	TestFalse(
		TEXT("Ordinary seam record cannot grant passive closure credit"),
		LayoutProfileSolverInternal::ReevaluatePassiveSeamClosureCoverageForRegion(
			TEXT("Passive"), Seams, RequestsByPath, RegionResultIndexByPath, UncreditedResult, FailureReason));
	TestTrue(TEXT("Uncredited passive seam leaves the shared face uncovered"), FailureReason.Contains(TEXT("not fully covered")));
	TestEqual(
		TEXT("Uncredited passive audit covers only its three non-shared faces"),
		UncreditedResult.RegionResults[4].SolveResult.ClosureCoverage[0].CoveredSegmentCount,
		3);

	Seams[0].bCountsTowardClosure = true;
	FLayoutRegionSolveScheduleResult CreditedResult = BuildScheduleResult();
	FailureReason.Reset();
	const bool bCredited = LayoutProfileSolverInternal::ReevaluatePassiveSeamClosureCoverageForRegion(
		TEXT("Passive"), Seams, RequestsByPath, RegionResultIndexByPath, CreditedResult, FailureReason);
	TestTrue(TEXT("Authorized owner seam grants passive closure credit"), bCredited);
	if (!bCredited)
	{
		AddError(FailureReason);
		return false;
	}

	const FLayoutSolveResult& PassiveResult = CreditedResult.RegionResults[4].SolveResult;
	TestEqual(TEXT("Passive audit emits one closure summary"), PassiveResult.ClosureCoverage.Num(), 1);
	if (PassiveResult.ClosureCoverage.Num() == 0)
	{
		return false;
	}

	TestTrue(TEXT("Authorized owner credit satisfies the passive closure"), PassiveResult.ClosureCoverage[0].bSatisfied);
	TestEqual(TEXT("Authorized owner credit covers every passive segment"), PassiveResult.ClosureCoverage[0].CoveredSegmentCount, 4);
	TestTrue(TEXT("Authorized owner credit covers the previously missing passive face"), PassiveResult.ClosureSegments.ContainsByPredicate([](const FLayoutClosureCoverageSegmentRecord& Segment)
	{
		return Segment.Cell == FIntVector(1, 1, 0)
			&& Segment.FaceDirection == ELayoutFaceDirection::NegX
			&& Segment.bCovered;
	}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationDeferredParentClosureAuditRejectsMissingProviderIntentTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.DeferredParentClosureAuditRejectsMissingProviderIntent",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationSelectsNegotiatedHostVerticalAccessAnchorsTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.SelectsNegotiatedHostVerticalAccessAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationSelectsNegotiatedHostVerticalAccessAnchorsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutCommittedEndpointAnchor> CandidateCommitments;
	FLayoutCommittedEndpointAnchor& LowerAnchor = CandidateCommitments.AddDefaulted_GetRef();
	LowerAnchor.CommitmentId = TEXT("LowerDoor");
	LowerAnchor.LocalCell = FIntVector(1, 1, 0);
	LowerAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	LowerAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	FLayoutCommittedEndpointAnchor& UpperAnchor = CandidateCommitments.AddDefaulted_GetRef();
	UpperAnchor.CommitmentId = TEXT("UpperDoor");
	UpperAnchor.LocalCell = FIntVector(1, 1, 1);
	UpperAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	UpperAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	const TArray<FLayoutPlannedCell> ChildPlannedCells = {
		{FIntVector(1, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::VerticalAccess}
	};
	const TArray<FIntVector> ChildVerticalAccessLocalCells = {
		FIntVector(1, 1, 0),
		FIntVector(1, 1, 1)
	};

	FLayoutCommittedEndpointAnchor SelectedIngressAnchor;
	FLayoutCommittedEndpointAnchor SelectedEgressAnchor;
	TArray<FIntVector> SelectedRouteCells;
	FString FailureReason;
	const bool bSucceeded = LayoutProfileSolverInternal::TrySelectNegotiatedHostVerticalAccessAnchorsForTests(
		CandidateCommitments,
		ChildPlannedCells,
		ChildVerticalAccessLocalCells,
		SelectedIngressAnchor,
		SelectedEgressAnchor,
		SelectedRouteCells,
		FailureReason);

	TestTrue(TEXT("Distinct lower and upper anchors with vertical-access cells succeed"), bSucceeded);
	TestTrue(TEXT("Successful anchor selection reports no failure reason"), FailureReason.IsEmpty());
	TestEqual(TEXT("Negotiated ingress anchor is the lowest committed host-facing anchor"), SelectedIngressAnchor.CommitmentId, FLayoutId(TEXT("LowerDoor")));
	TestEqual(TEXT("Negotiated egress anchor is the highest committed host-facing anchor"), SelectedEgressAnchor.CommitmentId, FLayoutId(TEXT("UpperDoor")));
	TestEqual(TEXT("Negotiated route keeps the stacked child path"), SelectedRouteCells.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationSelectsNegotiatedHostVerticalAccessAnchorsWithBoundaryShellContactTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.SelectsNegotiatedHostVerticalAccessAnchorsWithBoundaryShellContact",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationSelectsNegotiatedHostVerticalAccessAnchorsWithBoundaryShellContactTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutCommittedEndpointAnchor> CandidateCommitments;
	FLayoutCommittedEndpointAnchor& LowerAnchor = CandidateCommitments.AddDefaulted_GetRef();
	LowerAnchor.CommitmentId = TEXT("LowerDoor");
	LowerAnchor.LocalCell = FIntVector(1, 1, 0);
	LowerAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	LowerAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	FLayoutCommittedEndpointAnchor& UpperSharedShellAnchor = CandidateCommitments.AddDefaulted_GetRef();
	UpperSharedShellAnchor.CommitmentId = TEXT("UpperSharedShell");
	UpperSharedShellAnchor.LocalCell = FIntVector(2, 1, 2);
	UpperSharedShellAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	UpperSharedShellAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;

	const TArray<FLayoutPlannedCell> ChildPlannedCells = {
		{FIntVector(1, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::VerticalAccess},
		{FIntVector(1, 1, 2), ELayoutCellIntent::Connector},
		{FIntVector(2, 1, 2), ELayoutCellIntent::Boundary}
	};
	const TArray<FIntVector> ChildVerticalAccessLocalCells = {
		FIntVector(1, 1, 1)
	};

	FLayoutCommittedEndpointAnchor SelectedIngressAnchor;
	FLayoutCommittedEndpointAnchor SelectedEgressAnchor;
	TArray<FIntVector> SelectedRouteCells;
	FString FailureReason;
	const bool bSucceeded = LayoutProfileSolverInternal::TrySelectNegotiatedHostVerticalAccessAnchorsForTests(
		CandidateCommitments,
		ChildPlannedCells,
		ChildVerticalAccessLocalCells,
		SelectedIngressAnchor,
		SelectedEgressAnchor,
		SelectedRouteCells,
		FailureReason);

	TestTrue(TEXT("Upper shared-shell contacts on a child boundary cell still qualify when the child proves an internal vertical route"), bSucceeded);
	TestTrue(TEXT("Boundary-shell anchor selection reports no failure reason"), FailureReason.IsEmpty());
	TestEqual(TEXT("Boundary-shell negotiation keeps the lower ingress anchor"), SelectedIngressAnchor.CommitmentId, FLayoutId(TEXT("LowerDoor")));
	TestEqual(TEXT("Boundary-shell negotiation keeps the upper shell egress anchor"), SelectedEgressAnchor.CommitmentId, FLayoutId(TEXT("UpperSharedShell")));
	TestEqual(TEXT("Boundary-shell negotiation reconstructs the full child ascent route"), SelectedRouteCells.Num(), 4);
	TestEqual(TEXT("Boundary-shell route starts at the lower host contact"), SelectedRouteCells[0], FIntVector(1, 1, 0));
	TestEqual(TEXT("Boundary-shell route reaches the upper boundary contact"), SelectedRouteCells.Last(), FIntVector(2, 1, 2));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsSingleLevelNegotiatedHostVerticalAccessAnchorsTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsSingleLevelNegotiatedHostVerticalAccessAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsSingleLevelNegotiatedHostVerticalAccessAnchorsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutCommittedEndpointAnchor> CandidateCommitments;
	FLayoutCommittedEndpointAnchor& FirstAnchor = CandidateCommitments.AddDefaulted_GetRef();
	FirstAnchor.CommitmentId = TEXT("DoorA");
	FirstAnchor.LocalCell = FIntVector(1, 1, 0);
	FirstAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	FirstAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	FLayoutCommittedEndpointAnchor& SecondAnchor = CandidateCommitments.AddDefaulted_GetRef();
	SecondAnchor.CommitmentId = TEXT("DoorB");
	SecondAnchor.LocalCell = FIntVector(2, 1, 0);
	SecondAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	SecondAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	const TArray<FLayoutPlannedCell> ChildPlannedCells = {
		{FIntVector(1, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(2, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::VerticalAccess}
	};
	const TArray<FIntVector> ChildVerticalAccessLocalCells = {
		FIntVector(1, 1, 0),
		FIntVector(1, 1, 1)
	};

	FLayoutCommittedEndpointAnchor SelectedIngressAnchor;
	FLayoutCommittedEndpointAnchor SelectedEgressAnchor;
	TArray<FIntVector> SelectedRouteCells;
	FString FailureReason;
	const bool bSucceeded = LayoutProfileSolverInternal::TrySelectNegotiatedHostVerticalAccessAnchorsForTests(
		CandidateCommitments,
		ChildPlannedCells,
		ChildVerticalAccessLocalCells,
		SelectedIngressAnchor,
		SelectedEgressAnchor,
		SelectedRouteCells,
		FailureReason);

	TestFalse(TEXT("Single-level committed anchors cannot satisfy stacked host vertical-access negotiation"), bSucceeded);
	TestTrue(
		TEXT("Single-level anchor rejection explains the missing distinct lower and upper host contacts"),
		FailureReason.Contains(TEXT("distinct lower and upper host contacts")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsDisconnectedNegotiatedHostVerticalAccessAnchorsTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsDisconnectedNegotiatedHostVerticalAccessAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsDisconnectedNegotiatedHostVerticalAccessAnchorsTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutCommittedEndpointAnchor> CandidateCommitments;
	FLayoutCommittedEndpointAnchor& LowerAnchor = CandidateCommitments.AddDefaulted_GetRef();
	LowerAnchor.CommitmentId = TEXT("LowerDoor");
	LowerAnchor.LocalCell = FIntVector(1, 1, 0);
	LowerAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	LowerAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	FLayoutCommittedEndpointAnchor& UpperAnchor = CandidateCommitments.AddDefaulted_GetRef();
	UpperAnchor.CommitmentId = TEXT("UpperDoor");
	UpperAnchor.LocalCell = FIntVector(3, 3, 1);
	UpperAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	UpperAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	const TArray<FLayoutPlannedCell> ChildPlannedCells = {
		{FIntVector(1, 1, 0), ELayoutCellIntent::Entry},
		{FIntVector(1, 1, 1), ELayoutCellIntent::VerticalAccess},
		{FIntVector(3, 3, 1), ELayoutCellIntent::Entry}
	};
	const TArray<FIntVector> ChildVerticalAccessLocalCells = {
		FIntVector(1, 1, 1)
	};

	FLayoutCommittedEndpointAnchor SelectedIngressAnchor;
	FLayoutCommittedEndpointAnchor SelectedEgressAnchor;
	TArray<FIntVector> SelectedRouteCells;
	FString FailureReason;
	const bool bSucceeded = LayoutProfileSolverInternal::TrySelectNegotiatedHostVerticalAccessAnchorsForTests(
		CandidateCommitments,
		ChildPlannedCells,
		ChildVerticalAccessLocalCells,
		SelectedIngressAnchor,
		SelectedEgressAnchor,
		SelectedRouteCells,
		FailureReason);

	TestFalse(TEXT("Disconnected stacked anchors cannot satisfy child-owned host vertical-access negotiation"), bSucceeded);
	TestTrue(
		TEXT("Disconnected anchor rejection explains the missing internal child route"),
		FailureReason.Contains(TEXT("internal route")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsComposedContractWithInexactParentProviderSetTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsComposedContractWithInexactParentProviderSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsComposedContractWithInexactParentProviderSetTest::RunTest(const FString& Parameters)
{
	FLayoutNegotiatedChildResponsibilityContract Contract =
		BuildNegotiatedContractForValidation(ELayoutNegotiatedHostVerticalAccessResponsibility::Composed);
	Contract.CountedParentProviderCount = 1;
	Contract.CountedParentVerticalAccessCells = {
		FIntVector(0, 1, 0),
		FIntVector(1, 1, 0)
	};
	Contract.CountedChildProviderRegionDebugPaths = {
		TEXT("Child")
	};

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestFalse(TEXT("Composed negotiated responsibility contract rejects an inexact counted parent provider set"), bValid);
	TestTrue(
		TEXT("Inexact counted parent provider set rejection explains the mismatch"),
		FailureReason.Contains(TEXT("exact counted set")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationAllowsParentOwnedContractWithoutContractLocalProvidersTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.AllowsParentOwnedContractWithoutContractLocalProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationChildOwnedContractProofScheduleTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.ChildOwnedContractProofSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationComposedContractProofScheduleTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.ComposedContractProofSchedule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationNormalizesEquivalentCandidatesTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.NormalizesEquivalentCandidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationAllowsParentOwnedContractWithoutContractLocalProvidersTest::RunTest(const FString& Parameters)
{
	FLayoutNegotiatedChildResponsibilityContract Contract =
		BuildNegotiatedContractForValidation(ELayoutNegotiatedHostVerticalAccessResponsibility::ParentOwned);
	Contract.RequiredHostProviderCount = 1;
	Contract.CountedParentProviderCount = 0;

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestTrue(TEXT("ParentOwned child contract defers global provider counting to residual parent-plan validation"), bValid);
	TestTrue(TEXT("Accepted ParentOwned child contract reports no contract-local failure"), FailureReason.IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsChildOwnedContractWithNonzeroParentProvidersTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsChildOwnedContractWithNonzeroParentProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsChildOwnedContractWithNonzeroParentProvidersTest::RunTest(const FString& Parameters)
{
	// ChildOwned responsibility means the child provides all vertical access.
	// The contract must not count any parent providers.
	FLayoutNegotiatedChildResponsibilityContract Contract =
		BuildNegotiatedContractForValidation(ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	Contract.RequiredHostProviderCount = 1;
	Contract.CountedParentProviderCount = 2;
	Contract.CountedParentVerticalAccessCells = {FIntVector(0, 1, 0), FIntVector(1, 1, 0)};
	Contract.CountedChildProviderRegionDebugPaths = {TEXT("Child")};

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestFalse(TEXT("ChildOwned contract with nonzero counted parent providers is rejected"), bValid);
	TestTrue(
		TEXT("Rejected ChildOwned contract explains the unexpected parent providers"),
		FailureReason.Contains(TEXT("parent providers were still counted")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsComposedContractWithoutParentProvidersTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsComposedContractWithoutParentProviders",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsComposedContractWithoutParentProvidersTest::RunTest(const FString& Parameters)
{
	// Composed responsibility means parent and child both contribute.
	// The contract must retain at least one counted parent provider.
	FLayoutNegotiatedChildResponsibilityContract Contract =
		BuildNegotiatedContractForValidation(ELayoutNegotiatedHostVerticalAccessResponsibility::Composed);
	Contract.RequiredHostProviderCount = 2;
	Contract.CountedParentProviderCount = 0;
	Contract.CountedChildProviderRegionDebugPaths = {TEXT("Child")};

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestFalse(TEXT("Composed contract without counted parent providers is rejected"), bValid);
	TestTrue(
		TEXT("Rejected Composed contract explains the missing counted parent providers"),
		FailureReason.Contains(TEXT("retained no counted parent")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsContractWithSameLevelIngressEgressAnchorsTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsContractWithSameLevelIngressEgressAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsContractWithSameLevelIngressEgressAnchorsTest::RunTest(const FString& Parameters)
{
	// Host vertical access requires distinct lower and upper levels.
	// Ingress and egress anchors on the same level are invalid.
	FLayoutNegotiatedChildResponsibilityContract Contract =
		BuildNegotiatedContractForValidation(ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	Contract.RequiredHostProviderCount = 1;
	Contract.CountedParentProviderCount = 0;
	Contract.CountedChildProviderRegionDebugPaths = {TEXT("Child")};
	Contract.bHasRequiredHostEgressAnchor = true;
	Contract.RequiredHostEgressAnchor.CommitmentId = TEXT("SameLevelDoor");
	Contract.RequiredHostEgressAnchor.LocalCell = FIntVector(2, 1, 0);
	Contract.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	Contract.RequiredHostEgressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	Contract.CommittedParentChildInterfacesByLevel.Reset();
	FLayoutNegotiatedLevelInterfaceContract& SameLevelInterface =
		Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	SameLevelInterface.Level = 0;
	SameLevelInterface.EndpointAnchors = {
		Contract.RequiredHostIngressAnchor,
		Contract.RequiredHostEgressAnchor};

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestFalse(TEXT("Contract with ingress and egress anchors on the same level is rejected"), bValid);
	TestTrue(
		TEXT("Same-level anchor rejection explains the single-level host contacts"),
		FailureReason.Contains(TEXT("on the same level")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsContractWithMissingIntermediateReplacementLevelTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsContractWithMissingIntermediateReplacementLevel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsContractWithMissingIntermediateReplacementLevelTest::RunTest(const FString& Parameters)
{
	// When host ascent spans levels 0–2, every covered level must reserve
	// at least one replacement cell. Missing level 1 is invalid.
	FLayoutNegotiatedChildResponsibilityContract Contract;
	Contract.ParentRegionDebugPath = TEXT("Parent");
	Contract.ChildRegionDebugPath = TEXT("Child");
	Contract.HostVerticalAccessResponsibility = ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	Contract.RequiredHostProviderCount = 1;
	Contract.CountedChildProviderRegionDebugPaths = {TEXT("Child")};

	// Replacement volume covers level 0 and 2, but not 1.
	FLayoutNegotiatedLevelCellSet& Level0Replacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	Level0Replacement.Level = 0;
	Level0Replacement.Cells = {FIntVector(1, 1, 0)};

	FLayoutNegotiatedLevelCellSet& Level2Replacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	Level2Replacement.Level = 2;
	Level2Replacement.Cells = {FIntVector(1, 1, 2)};

	Contract.bHasRequiredHostIngressAnchor = true;
	Contract.RequiredHostIngressAnchor.CommitmentId = TEXT("LowerDoor");
	Contract.RequiredHostIngressAnchor.LocalCell = FIntVector(1, 1, 0);
	Contract.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	Contract.RequiredHostIngressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	Contract.bHasRequiredHostEgressAnchor = true;
	Contract.RequiredHostEgressAnchor.CommitmentId = TEXT("UpperDoor");
	Contract.RequiredHostEgressAnchor.LocalCell = FIntVector(1, 1, 2);
	Contract.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	Contract.RequiredHostEgressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	Contract.RequiredChildGenerallyConnectableAnchorPairId = TEXT("LowerDoor__UpperDoor");
	Contract.RequiredChildInternalVerticalSpanLevels = {0, 1, 2};

	Contract.RequiredChildInternalVerticalRouteCells = {
		FIntVector(1, 1, 0),
		FIntVector(1, 1, 1),
		FIntVector(1, 1, 2)
	};
	FLayoutNegotiatedLevelInterfaceContract& LowerInterface =
		Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	LowerInterface.Level = 0;
	LowerInterface.EndpointAnchors = {Contract.RequiredHostIngressAnchor};
	FLayoutNegotiatedLevelInterfaceContract& UpperInterface =
		Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	UpperInterface.Level = 2;
	UpperInterface.EndpointAnchors = {Contract.RequiredHostEgressAnchor};

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestFalse(TEXT("Contract with missing intermediate replacement level is rejected"), bValid);
	TestTrue(
		TEXT("Missing intermediate replacement level rejection explains the unreserved ascent level"),
		FailureReason.Contains(TEXT("does not reserve any replacement cells on covered host-ascent level")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutChildNegotiationRejectsContractWithIncompleteVerticalSpanTest,
	"PorismExtension.Layout.Solver.ChildNegotiation.RejectsContractWithIncompleteVerticalSpan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutChildNegotiationRejectsContractWithIncompleteVerticalSpanTest::RunTest(const FString& Parameters)
{
	// The negotiated anchor-pair proof span must cover every level between
	// the ingress and egress anchor levels.
	FLayoutNegotiatedChildResponsibilityContract Contract;
	Contract.ParentRegionDebugPath = TEXT("Parent");
	Contract.ChildRegionDebugPath = TEXT("Child");
	Contract.HostVerticalAccessResponsibility = ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	Contract.RequiredHostProviderCount = 1;
	Contract.CountedChildProviderRegionDebugPaths = {TEXT("Child")};

	FLayoutNegotiatedLevelCellSet& Level0Replacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	Level0Replacement.Level = 0;
	Level0Replacement.Cells = {FIntVector(1, 1, 0)};

	FLayoutNegotiatedLevelCellSet& Level1Replacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	Level1Replacement.Level = 1;
	Level1Replacement.Cells = {FIntVector(1, 1, 1)};

	FLayoutNegotiatedLevelCellSet& Level2Replacement = Contract.ReplacementVolumeByLevel.AddDefaulted_GetRef();
	Level2Replacement.Level = 2;
	Level2Replacement.Cells = {FIntVector(1, 1, 2)};

	Contract.bHasRequiredHostIngressAnchor = true;
	Contract.RequiredHostIngressAnchor.CommitmentId = TEXT("LowerDoor");
	Contract.RequiredHostIngressAnchor.LocalCell = FIntVector(1, 1, 0);
	Contract.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	Contract.RequiredHostIngressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;

	Contract.bHasRequiredHostEgressAnchor = true;
	Contract.RequiredHostEgressAnchor.CommitmentId = TEXT("UpperDoor");
	Contract.RequiredHostEgressAnchor.LocalCell = FIntVector(1, 1, 2);
	Contract.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	Contract.RequiredHostEgressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	// Span only covers levels 0 and 2, skipping 1.
	Contract.RequiredChildGenerallyConnectableAnchorPairId = TEXT("LowerDoor__UpperDoor");
	Contract.RequiredChildInternalVerticalSpanLevels = {0, 2};

	Contract.RequiredChildInternalVerticalRouteCells = {
		FIntVector(1, 1, 0),
		FIntVector(1, 1, 2)
	};
	FLayoutNegotiatedLevelInterfaceContract& LowerInterface =
		Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	LowerInterface.Level = 0;
	LowerInterface.EndpointAnchors = {Contract.RequiredHostIngressAnchor};
	FLayoutNegotiatedLevelInterfaceContract& UpperInterface =
		Contract.CommittedParentChildInterfacesByLevel.AddDefaulted_GetRef();
	UpperInterface.Level = 2;
	UpperInterface.EndpointAnchors = {Contract.RequiredHostEgressAnchor};

	FString FailureReason;
	const bool bValid = LayoutProfileSolverInternal::ValidateNegotiatedChildResponsibilityContractForTests(
		Contract,
		FailureReason);

	TestFalse(TEXT("Contract with incomplete vertical span between anchors is rejected"), bValid);
	TestTrue(
		TEXT("Incomplete vertical span rejection explains the missing intermediate ascent level"),
		FailureReason.Contains(TEXT("does not cover ascent level")));
	return true;
}

bool FLayoutChildNegotiationChildOwnedContractProofScheduleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateChildNegotiationTestOuter(TEXT("LayoutChildOwnedContractProof"));

	ULayoutProfileAsset* ParentProfile = CreateProfileWithUniversalContentSet(Outer, TEXT("ChildOwnedParentProfile"), FIntPoint(5, 3), FIntPoint(5, 3), 2, 0, false);
	ParentProfile->bRequireAllTraversalChannelsReachable = false;
	FLayoutSparsePreserveTerrainRule PreserveRule;
	PreserveRule.RuleId = TEXT("ChildOwnedGroundInteriorPreserve");
	PreserveRule.PlacementZone = ELayoutPlacementZone::Interior;
	PreserveRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	ParentProfile->SparsePlacementRules.Add(FInstancedStruct::Make(PreserveRule));
	ULayoutModuleAsset* ParentEntryModule = ParentProfile->ContentSet->Entries[1].ModuleSettings.Module;
	ParentEntryModule->Roles = {ELayoutModuleRole::Entry};
	ParentEntryModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	ParentEntryModule->FaceRules.PosX.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);

	ULayoutRegionContentSetAsset* ChildContentSet = CreateStackedChildTraversalContentSet(Outer);
	ULayoutProfileAsset* ChildProfile = CreateProfile(Outer, TEXT("ChildOwnedChildProfile"), FIntPoint(2, 1), FIntPoint(2, 1), 2, 0, false);
	ChildProfile->ContentSet = ChildContentSet;
	ChildProfile->bRequireAllTraversalChannelsReachable = false;

	FLayoutRegionSolveRequest ParentRequest = BuildSuppliedRequest(
		ParentProfile,
		501,
		TEXT("Parent"),
		FIntPoint(5, 3),
		BuildResidualParentCells());

	FLayoutRegionSolveRequest ChildRequest = BuildSuppliedRequest(
		ChildContentSet,
		ChildProfile,
		502,
		TEXT("Child"),
		FIntPoint(2, 1),
		BuildStackedChildCells());
	ChildRequest.RegionCellOffset = FIntVector(1, 1, 0);

	FLayoutNegotiatedChildResponsibilityContract Contract;
	ConfigureDirectProofContract(
		Contract,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned,
		1,
		0,
		{},
		{TEXT("Child")});
	if (!ValidateDirectProofContract(
		*this,
		TEXT("Manual child-owned negotiated contract validates before the schedule runs"),
		Contract))
	{
		return false;
	}

	ParentRequest.NegotiatedChildResponsibilityContracts = {Contract};
	const FLayoutRegionSolveScheduleRequest ScheduleRequest =
		BuildDirectCommittedScheduleRequest(ParentRequest, ChildRequest, Contract);
	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	TestTrue(TEXT("Direct committed child-owned schedule succeeds"), ScheduleResult.bSucceeded);
	if (!ScheduleResult.bSucceeded)
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* ParentResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& Result)
	{
		return Result.RegionDebugPath == TEXT("Parent");
	});
	const FLayoutRegionSolveResult* ChildResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& Result)
	{
		return Result.RegionDebugPath == TEXT("Child");
	});
	if (!TestNotNull(TEXT("Parent result is present"), ParentResult)
		|| !TestNotNull(TEXT("Child result is present"), ChildResult))
	{
		return false;
	}

	const FLayoutNegotiatedChildResponsibilityContract* ParentContract = ParentResult->NegotiatedChildResponsibilityContracts.FindByPredicate(
		[](const FLayoutNegotiatedChildResponsibilityContract& Candidate)
		{
			return Candidate.ChildRegionDebugPath == TEXT("Child");
		});
	const FLayoutNegotiatedChildResponsibilityContract* ChildContract = ChildResult->NegotiatedChildResponsibilityContracts.FindByPredicate(
		[](const FLayoutNegotiatedChildResponsibilityContract& Candidate)
		{
			return Candidate.ParentRegionDebugPath == TEXT("Parent")
				&& Candidate.ChildRegionDebugPath == TEXT("Child");
		});
	if (!TestNotNull(TEXT("Parent result keeps the child-owned negotiated contract"), ParentContract)
		|| !TestNotNull(TEXT("Child result keeps the child-owned negotiated contract"), ChildContract))
	{
		return false;
	}

	TestEqual(
		TEXT("Schedule keeps host ascent child-owned"),
		ParentContract->HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned);
	TestEqual(TEXT("Child-owned contract keeps zero counted parent providers"), ParentContract->CountedParentProviderCount, 0);
	TestTrue(TEXT("Child-owned contract counts the child provider"), ParentContract->CountedChildProviderRegionDebugPaths.Contains(TEXT("Child")));
	TestTrue(TEXT("Child-owned contract keeps a lower ingress anchor"), ParentContract->bHasRequiredHostIngressAnchor);
	TestTrue(TEXT("Child-owned contract keeps an upper egress anchor"), ParentContract->bHasRequiredHostEgressAnchor);
	TestEqual(TEXT("Child-owned contract keeps the negotiated generally connectable anchor-pair proof"), ParentContract->RequiredChildGenerallyConnectableAnchorPairId, FLayoutId(TEXT("LowerHostDoor__UpperHostDoor")));
	TestEqual(TEXT("Child-owned contract keeps the coarse negotiated ascent span"), ParentContract->RequiredChildInternalVerticalSpanLevels.Num(), 2);
	TestTrue(
		TEXT("Parent proof validates the child-owned negotiated contract before solving"),
		ParentResult->ValidationAssertions.ContainsByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionKind == ELayoutValidationAssertionKind::ChildCommitmentContractValid && Assertion.bPassed;
		}));
	TestTrue(
		TEXT("Child proof validates the child-owned negotiated contract before solving"),
		ChildResult->ValidationAssertions.ContainsByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionKind == ELayoutValidationAssertionKind::ChildCommitmentContractValid && Assertion.bPassed;
		}));

	bool bParentPlacedVerticalAccess = false;
	for (const FLayoutPlacedModule& Placement : ParentResult->SolveResult.Placements)
	{
		if (Placement.Intent == ELayoutCellIntent::VerticalAccess)
		{
			bParentPlacedVerticalAccess = true;
			break;
		}
	}
	TestFalse(
		TEXT("Preserved-space parent solve does not place parent-owned vertical access after child-owned negotiation"),
		bParentPlacedVerticalAccess);
	TestEqual(TEXT("Child solve keeps both committed host-facing anchors"), ChildResult->CommittedEndpointAnchors.Num(), 2);
	TestFalse(TEXT("Child solve still exports at least one entry-capable cell"), ChildResult->SolveResult.ExportedEntryCells.IsEmpty());
	return true;
}

bool FLayoutChildNegotiationComposedContractProofScheduleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateChildNegotiationTestOuter(TEXT("LayoutComposedContractProof"));

	ULayoutProfileAsset* ParentProfile = CreateProfileWithUniversalContentSet(Outer, TEXT("ComposedParentProfile"), FIntPoint(5, 3), FIntPoint(5, 3), 2, 0, false);
	ParentProfile->bRequireAllTraversalChannelsReachable = false;
	ULayoutModuleAsset* ParentEntryModule = ParentProfile->ContentSet->Entries[1].ModuleSettings.Module;
	ParentEntryModule->Roles = {ELayoutModuleRole::Entry};
	ParentEntryModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	ParentEntryModule->FaceRules.PosX.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceEntry);

	ULayoutRegionContentSetAsset* ChildContentSet = CreateStackedChildTraversalContentSet(Outer);
	ULayoutProfileAsset* ChildProfile = CreateProfile(Outer, TEXT("ComposedChildProfile"), FIntPoint(2, 1), FIntPoint(2, 1), 2, 0, false);
	ChildProfile->ContentSet = ChildContentSet;
	ChildProfile->bRequireAllTraversalChannelsReachable = false;

	FLayoutRegionSolveRequest ParentRequest = BuildSuppliedRequest(
		ParentProfile,
		601,
		TEXT("Parent"),
		FIntPoint(5, 3),
		BuildResidualParentCellsWithComposedProvider());

	FLayoutRegionSolveRequest ChildRequest = BuildSuppliedRequest(
		ChildContentSet,
		ChildProfile,
		602,
		TEXT("Child"),
		FIntPoint(2, 1),
		BuildStackedChildCells());
	ChildRequest.RegionCellOffset = FIntVector(1, 1, 0);

	FLayoutNegotiatedChildResponsibilityContract Contract;
	ConfigureDirectProofContract(
		Contract,
		ELayoutNegotiatedHostVerticalAccessResponsibility::Composed,
		1,
		1,
		{FIntVector(3, 1, 0)},
		{TEXT("Child")});
	if (!ValidateDirectProofContract(
		*this,
		TEXT("Manual composed negotiated contract validates before the schedule runs"),
		Contract))
	{
		return false;
	}

	ParentRequest.NegotiatedChildResponsibilityContracts = {Contract};
	const FLayoutRegionSolveScheduleRequest ScheduleRequest =
		BuildDirectCommittedScheduleRequest(ParentRequest, ChildRequest, Contract);
	const FLayoutRegionSolveScheduleResult ScheduleResult = FLayoutProfileSolver::SolveRegionsSynchronously(ScheduleRequest);
	TestTrue(TEXT("Direct committed composed schedule succeeds"), ScheduleResult.bSucceeded);
	if (!ScheduleResult.bSucceeded)
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}

	const FLayoutRegionSolveResult* ParentResult = ScheduleResult.RegionResults.FindByPredicate([](const FLayoutRegionSolveResult& Result)
	{
		return Result.RegionDebugPath == TEXT("Parent");
	});
	if (!TestNotNull(TEXT("Composed parent result is present"), ParentResult))
	{
		return false;
	}

	const FLayoutNegotiatedChildResponsibilityContract* ParentContract = ParentResult->NegotiatedChildResponsibilityContracts.FindByPredicate(
		[](const FLayoutNegotiatedChildResponsibilityContract& Candidate)
		{
			return Candidate.ChildRegionDebugPath == TEXT("Child");
		});
	if (!TestNotNull(TEXT("Composed parent result keeps the negotiated contract"), ParentContract))
	{
		return false;
	}

	TestEqual(
		TEXT("Schedule keeps host ascent composed"),
		ParentContract->HostVerticalAccessResponsibility,
		ELayoutNegotiatedHostVerticalAccessResponsibility::Composed);
	TestEqual(TEXT("Composed contract keeps one counted parent provider"), ParentContract->CountedParentProviderCount, 1);
	TestEqual(TEXT("Composed contract keeps the exact counted parent provider set"), ParentContract->CountedParentVerticalAccessCells.Num(), 1);
	TestTrue(TEXT("Composed contract counts the child provider"), ParentContract->CountedChildProviderRegionDebugPaths.Contains(TEXT("Child")));
	TestEqual(TEXT("Composed contract keeps the negotiated generally connectable anchor-pair proof"), ParentContract->RequiredChildGenerallyConnectableAnchorPairId, FLayoutId(TEXT("LowerHostDoor__UpperHostDoor")));
	TestEqual(TEXT("Composed contract keeps the coarse negotiated ascent span"), ParentContract->RequiredChildInternalVerticalSpanLevels.Num(), 2);

	bool bParentPlacedVerticalAccess = false;
	for (const FLayoutPlacedModule& Placement : ParentResult->SolveResult.Placements)
	{
		if (Placement.Intent == ELayoutCellIntent::VerticalAccess && Placement.Cell == FIntVector(3, 1, 0))
		{
			bParentPlacedVerticalAccess = true;
			break;
		}
	}
	TestTrue(
		TEXT("Residual parent solve keeps the counted parent vertical-access provider outside the replacement volume"),
		bParentPlacedVerticalAccess);
	return true;
}

bool FLayoutChildNegotiationNormalizesEquivalentCandidatesTest::RunTest(const FString& Parameters)
{
	LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests BaseCandidate;
	BaseCandidate.BoundaryCertificateId = TEXT("BoundarySignatureA");
	BaseCandidate.Score = 120;
	BaseCandidate.TieBreakHash = 10;
	BaseCandidate.NegotiatedHostVerticalAccessResponsibility =
		ELayoutNegotiatedHostVerticalAccessResponsibility::ChildOwned;
	BaseCandidate.ParentPlanReservedCells = {
		FIntVector(1, 1, 0),
		FIntVector(2, 1, 0),
		FIntVector(2, 1, 1)
	};
	LayoutProfileSolverInternal::FNegotiatedSharedParentChildFaceForTests& SharedFace =
		BaseCandidate.SharedParentChildFaces.AddDefaulted_GetRef();
	SharedFace.ParentCell = FIntVector(3, 1, 0);
	SharedFace.ChildLocalCell = FIntVector(1, 0, 0);
	SharedFace.FaceDirection = ELayoutFaceDirection::PosX;
	SharedFace.InterfaceFamily = LayoutGameplayTags::InterfacePartitionDoor;
	FLayoutCommittedTraversalAnchor& TraversalAnchor =
		BaseCandidate.ParentCommittedTraversalAnchors.AddDefaulted_GetRef();
	TraversalAnchor.Cell = FIntVector(3, 1, 1);
	TraversalAnchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	BaseCandidate.bHasRequiredHostIngressAnchor = true;
	BaseCandidate.RequiredHostIngressAnchor.CommitmentId = TEXT("LowerAnchor");
	BaseCandidate.RequiredHostIngressAnchor.LocalCell = FIntVector(0, 0, 0);
	BaseCandidate.RequiredHostIngressAnchor.FaceDirection = ELayoutFaceDirection::NegX;
	BaseCandidate.RequiredHostIngressAnchor.ConnectionTag = LayoutGameplayTags::FaceEntry;
	BaseCandidate.bHasRequiredHostEgressAnchor = true;
	BaseCandidate.RequiredHostEgressAnchor.CommitmentId = TEXT("UpperAnchor");
	BaseCandidate.RequiredHostEgressAnchor.LocalCell = FIntVector(1, 0, 1);
	BaseCandidate.RequiredHostEgressAnchor.FaceDirection = ELayoutFaceDirection::PosX;
	BaseCandidate.RequiredHostEgressAnchor.ConnectionTag = LayoutGameplayTags::FaceOpen;
	BaseCandidate.RequiredChildInternalVerticalRouteCells = {
		FIntVector(0, 0, 0),
		FIntVector(1, 0, 0),
		FIntVector(1, 0, 1)
	};
	BaseCandidate.Commitments = {
		BaseCandidate.RequiredHostIngressAnchor,
		BaseCandidate.RequiredHostEgressAnchor
	};

	LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests EquivalentLowerScore = BaseCandidate;
	EquivalentLowerScore.Score = 80;
	EquivalentLowerScore.TieBreakHash = 20;

	LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests DistinctBoundarySignature = BaseCandidate;
	DistinctBoundarySignature.BoundaryCertificateId = TEXT("BoundarySignatureB");
	DistinctBoundarySignature.Score = 90;
	DistinctBoundarySignature.TieBreakHash = 40;

	LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests DistinctCandidate = BaseCandidate;
	DistinctCandidate.Score = 100;
	DistinctCandidate.TieBreakHash = 30;
	DistinctCandidate.RequiredHostEgressAnchor.CommitmentId = TEXT("UpperAnchorB");
	DistinctCandidate.RequiredChildInternalVerticalRouteCells = {
		FIntVector(0, 0, 0),
		FIntVector(0, 1, 0),
		FIntVector(0, 1, 1)
	};
	DistinctCandidate.Commitments = {
		DistinctCandidate.RequiredHostIngressAnchor,
		DistinctCandidate.RequiredHostEgressAnchor
	};

	TArray<LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests> Candidates = {
		EquivalentLowerScore,
		DistinctCandidate,
		DistinctBoundarySignature,
		BaseCandidate
	};

	int32 CollapsedCount = 0;
	int32 BeamTrimmedCount = 0;
	LayoutProfileSolverInternal::NormalizeSuccessfulChildPlacementCandidatesForTests(
		Candidates,
		false,
		1,
		CollapsedCount,
		BeamTrimmedCount);

	TestEqual(TEXT("Only candidates with the same mapping and boundary signature collapse"), Candidates.Num(), 3);
	TestEqual(TEXT("Exactly one candidate was collapsed"), CollapsedCount, 1);
	TestEqual(TEXT("No beam trimming was needed for the small normalization sample"), BeamTrimmedCount, 0);
	TestEqual(TEXT("The highest-scoring representative of the collapsed candidate cluster is retained"), Candidates[0].Score, 120);
	TestTrue(
		TEXT("The distinct normalized candidate survives collapse"),
		Candidates.ContainsByPredicate([](const LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests& Candidate)
		{
			return Candidate.RequiredHostEgressAnchor.CommitmentId == FLayoutId(TEXT("UpperAnchorB"));
		}));
	TestTrue(
		TEXT("Same-transform alternative with a different boundary certificate survives collapse"),
		Candidates.ContainsByPredicate([](const LayoutProfileSolverInternal::FNegotiatedCandidateNormalizationInputForTests& Candidate)
		{
			return Candidate.BoundaryCertificateId == FLayoutId(TEXT("BoundarySignatureB"));
		}));
	return true;
}

bool FLayoutChildNegotiationDeferredParentClosureAuditRejectsMissingProviderIntentTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateChildNegotiationTestOuter(TEXT("LayoutChildDeferredClosureAuditMissingIntent"));

	ULayoutProfileAsset* ParentProfile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("DeferredParentMissingIntentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ULayoutProfileAsset* ChildProfile = CreateProfileWithUniversalContentSet(
		Outer, TEXT("DeferredChildMissingIntentProfile"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	ParentProfile->ContentSet->Entries.SetNum(1);
	ChildProfile->ContentSet->Entries.SetNum(1);
	AddOuterPerimeterClosure(ParentProfile);

	FLayoutRegionSolveRequest ParentRequest = BuildSingleCellRequest(ParentProfile, 401, TEXT("Parent"), FIntVector(0, 0, 0), ELayoutCellIntent::Entry);
	FLayoutRegionSolveRequest ChildRequest = BuildSingleCellRequest(ChildProfile, 402, TEXT("Child"), FIntVector(1, 0, 0), ELayoutCellIntent::Entry);

	FLayoutRegionSolveScheduleResult ScheduleResult;
	ScheduleResult.RegionResults = {
		BuildSyntheticClosureAuditResult(ParentRequest),
		BuildSyntheticClosureAuditResult(ChildRequest)
	};
	const TMap<FString, const FLayoutRegionSolveRequest*> RequestsByPath = {
		{TEXT("Parent"), &ParentRequest},
		{TEXT("Child"), &ChildRequest}
	};
	const TMap<FString, int32> RegionResultIndexByPath = {
		{TEXT("Parent"), 0},
		{TEXT("Child"), 1}
	};
	const TMap<FString, TArray<FString>> ChildrenByParent = {
		{TEXT("Parent"), {TEXT("Child")}}
	};
	ScheduleResult.bSucceeded = LayoutProfileSolverInternal::ReevaluateDeferredClosureCoverageForRegion(
		TEXT("Parent"),
		RequestsByPath,
		RegionResultIndexByPath,
		ChildrenByParent,
		ScheduleResult,
		ScheduleResult.FailureReason);
	TestFalse(TEXT("Synthetic deferred audit rejects child without provider intent"), ScheduleResult.bSucceeded);
	TestTrue(TEXT("Failure reason cites deferred closure coverage"), ScheduleResult.FailureReason.Contains(TEXT("not fully covered")));
	return true;
}
