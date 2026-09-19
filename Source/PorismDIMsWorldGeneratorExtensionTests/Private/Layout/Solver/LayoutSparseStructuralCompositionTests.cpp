// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "LayoutSparseStructuralTestFixture.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutProfileSolverInternal.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseStructuralCroppedTopologyTest,
	"PorismExtension.Layout.Solver.SparseStructural.CroppedTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseStructuralShiftedMetadataTest,
	"PorismExtension.Layout.Solver.SparseStructural.ShiftedMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseStructuralExactStairRouteTest,
	"PorismExtension.Layout.Solver.SparseStructural.ExactStairRoute",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseStructuralWitnessRetryTest,
	"PorismExtension.Layout.Solver.SparseStructural.WitnessRetry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Replacement must start from original child domains; failed selection must not publish partial narrowing. */
bool FLayoutSparseStructuralWitnessRetryTest::RunTest(const FString& Parameters)
{
	using namespace LayoutProfileSolverInternal;
	FLayoutRegionSolveRequest Request;
	FLayoutCellCandidateDomainRestriction Root;
	Root.Cell = FIntVector(1, 1, 0);
	Root.RestrictionId = TEXT("Child.Root");
	Root.AllowedCandidates = {{TEXT("A"), 0}, {TEXT("B"), 1}};
	FLayoutCellCandidateDomainRestriction Support = Root;
	Support.Cell = FIntVector(2, 1, 1);
	Support.RestrictionId = TEXT("Child.Support");
	Request.CandidateDomainRestrictions = {Root, Support};
	FLayoutVerticalAccessHostGroup Group;
	Group.GroupId = TEXT("VA");
	for (int32 Index = 0; Index < 2; ++Index)
	{
		FLayoutVerticalAccessHostOption& Option = Group.Options.AddDefaulted_GetRef();
		Option.bHasExactCandidateWitness = true;
		Option.LowerCell = Root.Cell;
		Option.UpperCell = Root.Cell + FIntVector(0, 0, 1);
		Option.UpperCandidateLocalCell = FIntVector(0, 0, 1);
		Option.LowerModuleSnapshotId = Root.AllowedCandidates[Index].ModuleSnapshotId;
		Option.LowerYawRotationSteps = Root.AllowedCandidates[Index].YawRotationSteps;
		Option.RequiredFilledSupportCells = {Support.Cell};
		FLayoutVerticalAccessSupportCandidate Offer;
		Offer.Cell = Support.Cell;
		Offer.RootCell = Support.Cell;
		Offer.ModuleSnapshotId = Option.LowerModuleSnapshotId;
		Offer.YawRotationSteps = Option.LowerYawRotationSteps;
		Option.FilledSupportCandidates = {Offer};
	}
	FString Failure;
	TestTrue(TEXT("First witness intersects child root and support"),
		ApplySelectedVerticalAccessCandidateWitnessesForTests({Group}, {0}, Request, Failure));
	const FLayoutId FirstRestrictionId = Request.CandidateDomainRestrictions[0].RestrictionId;
	TestTrue(TEXT("Replacement restores original child domains before intersection"),
		ApplySelectedVerticalAccessCandidateWitnessesForTests({Group}, {1}, Request, Failure));
	TestNotEqual(TEXT("Replacement changes exact restriction identity"), Request.CandidateDomainRestrictions[0].RestrictionId, FirstRestrictionId);
	for (const FLayoutCellCandidateDomainRestriction& Restriction : Request.CandidateDomainRestrictions)
	{
		TestEqual(TEXT("Replacement leaves singleton"), Restriction.AllowedCandidates.Num(), 1);
		if (!Restriction.AllowedCandidates.IsEmpty())
		{
			TestEqual(TEXT("Replacement selects B, not stale A"), Restriction.AllowedCandidates[0].ModuleSnapshotId, FLayoutId(TEXT("B")));
		}
	}
	Group.Options[0].FilledSupportCandidates.Reset();
	TestFalse(TEXT("Missing exact support domain rejects witness"),
		ApplySelectedVerticalAccessCandidateWitnessesForTests({Group}, {0}, Request, Failure));
	TestEqual(TEXT("Rejected support leaves prior root unchanged"),
		Request.CandidateDomainRestrictions[0].AllowedCandidates[0].ModuleSnapshotId, FLayoutId(TEXT("B")));
	Root.AllowedCandidates.Reset();
	Request.CandidateDomainRestrictions = {Root};
	TestFalse(TEXT("Empty child domain cannot be resurrected by exact witness"),
		ApplySelectedVerticalAccessCandidateWitnessesForTests({Group}, {1}, Request, Failure));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutSparseStructuralLocalSupportTest,
	"PorismExtension.Layout.Solver.SparseStructural.LocalSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Empty background cannot supply mandatory support; a committed compatible neighbor can. */
bool FLayoutSparseStructuralLocalSupportTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	using namespace LayoutProfileSolverInternal;
	UObject* Outer = CreatePackage(TEXT("/Temp/SparseStructuralLocalSupport"));
	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("Cell"), FIntVector(8, 8, 8));
	const FIntVector Root(1, 1, 0);
	const FIntVector Neighbor(2, 1, 0);
	for (const ELayoutFaceOccupancyPolicy Policy : {
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
		ELayoutFaceOccupancyPolicy::RequiresWalkableFilledNeighbor})
	{
		TArray<FLayoutFaceRule> Faces;
		for (int32 Index = 0; Index < 6; ++Index)
		{
			const ELayoutFaceDirection Direction = static_cast<ELayoutFaceDirection>(Index);
			Faces.Add(MakeConnectionFaceRule(Direction, LayoutGameplayTags::FaceOpen,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				Direction == ELayoutFaceDirection::PosX ? Policy : ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				Index < 4 ? MakeTags({LayoutGameplayTags::TraversalPrimary}) : FGameplayTagContainer()));
		}
		ULayoutModuleAsset* Module = CreateModule(Outer,
			*FString::Printf(TEXT("Support%d"), static_cast<int32>(Policy)), Template,
			{ELayoutCellIntent::Interior}, Faces);
		FSolveContext Owning;
		Owning.FootprintSize = FIntPoint(3, 3);
		Owning.ProfileSnapshot.LevelCount = 1;
		Owning.ModuleSnapshots = {FLayoutProfileSolver::BuildModuleSnapshot(Module, 1)};
		Owning.ModuleSnapshots[0].AllowedYawRotationSteps = {0};
		for (int32 Y = 0; Y < 3; ++Y)
		{
			for (int32 X = 0; X < 3; ++X)
			{
				FLayoutPlannedCell Cell;
				Cell.Cell = FIntVector(X, Y, 0);
				Cell.Intent = ELayoutCellIntent::Interior;
				Cell.ModuleLevelIndex = 0;
				Cell.PlacementZone = ELayoutPlacementZone::Interior;
				Owning.OwningTopologyCellsByPhysicalCell.Add(Cell.Cell, Cell);
				Owning.TerrainResidualRuleIdByCell.Add(Cell.Cell, TEXT("Preserve"));
			}
		}
		FSolveContext Local;
		FString Failure;
		TestTrue(TEXT("Local support view builds"), TryBuildSparseStructuralLocalSolveView(Owning, {Root}, Local, Failure));
		const auto HasRoot = [&Root](const FSolveContext& Context)
		{
			const TArray<FSolveCandidate>* Domain = Context.InitialDomains.Find(Root);
			return Domain && Domain->ContainsByPredicate([](const FSolveCandidate& Candidate) { return IsOccupiedCandidate(Candidate); });
		};
		TestEqual(TEXT("Only empty-compatible face admits preserved empty support"), HasRoot(Local),
			Policy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
		FSolveContext Wide;
		TestTrue(TEXT("Wider tentative work view builds"), TryBuildSparseStructuralLocalSolveView(Owning, {Root, Neighbor}, Wide, Failure));
		TestTrue(TEXT("Nested crop builds"), TryBuildSparseStructuralLocalSolveView(Wide, {Root}, Local, Failure));
		TestEqual(TEXT("Nested crop restores preserved-empty authority outside retained work"), HasRoot(Local),
			Policy == ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
		FSolveContext::FSolvePlacement Placement;
		Placement.bEmpty = false;
		Placement.VariantIndex = 0;
		Placement.ModuleSnapshotIndex = 0;
		Placement.ModuleSnapshotId = Owning.ModuleSnapshots[0].SnapshotId;
		Placement.BundleRootCell = Neighbor;
		Owning.Placements.Add(Neighbor, Placement);
		TestTrue(TEXT("Filled support view builds"), TryBuildSparseStructuralLocalSolveView(Owning, {Root}, Local, Failure));
		if (!TestTrue(TEXT("Compatible claimed neighbor supplies actual support, including later fill"), HasRoot(Local)))
		{
			if (const TArray<FString>* Reasons = Local.InitialDomainAdmissionFailuresByCell.Find(Root))
			{
				AddError(FString::Join(*Reasons, TEXT(" | ")));
			}
		}
		Faces[static_cast<int32>(ELayoutFaceDirection::NegX)] = MakeConnectionFaceRule(
			ELayoutFaceDirection::NegX, LayoutGameplayTags::FaceSolid, MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, MakeTags({LayoutGameplayTags::TraversalPrimary}));
		ULayoutModuleAsset* BadNeighbor = CreateModule(Outer,
			*FString::Printf(TEXT("BadSupport%d"), static_cast<int32>(Policy)), Template,
			{ELayoutCellIntent::Boundary}, Faces);
		Owning.ModuleSnapshots.Add(FLayoutProfileSolver::BuildModuleSnapshot(BadNeighbor, 1));
		Owning.ModuleSnapshots[1].AllowedYawRotationSteps = {0};
		Placement.VariantIndex = 1;
		Placement.ModuleSnapshotIndex = 1;
		Placement.ModuleSnapshotId = Owning.ModuleSnapshots[1].SnapshotId;
		Owning.Placements[Neighbor] = Placement;
		TestTrue(TEXT("Incompatible filled neighbor view builds"), TryBuildSparseStructuralLocalSolveView(Owning, {Root}, Local, Failure));
		TestFalse(TEXT("Empty-compatible does not permit incompatible later filled faces"), HasRoot(Local));
	}
	return true;
}

/** Proves local sparse work keeps owning-region topology instead of manufacturing exterior faces. */
bool FLayoutSparseStructuralCroppedTopologyTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	using namespace LayoutProfileSolverInternal;

	UObject* Outer = CreatePackage(TEXT("/Temp/LayoutSparseStructural_CroppedTopology"));
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("CroppedTopology"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::None;
	Profile->bRequireAllTraversalChannelsReachable = false;
	FLayoutSparsePreserveTerrainRule PreserveRule;
	PreserveRule.RuleId = TEXT("PreservedInterior");
	PreserveRule.PlacementZone = ELayoutPlacementZone::Interior;
	PreserveRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	Profile->SparsePlacementRules.Add(FInstancedStruct::Make(PreserveRule));

	const FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		4312,
		TEXT("SparseStructural/CroppedTopology"));
	FSolveContext OwningContext;
	if (!TestTrue(
		TEXT("Canonical preserving-region preparation succeeds"),
		TryPrepareRequestSolveContextThroughRouteDomainStage(Request, OwningContext, false)))
	{
		AddError(OwningContext.Result.FailureReason);
		return false;
	}

	FSolveContext InteriorLocalContext;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Interior POI local view builds"),
		TryPrepareSparseStructuralLocalSolveView(
			Request,
			{FIntVector(2, 2, 0)},
			InteriorLocalContext,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	const FSolveCellFaceInterfaceSet* InteriorFaces =
		InteriorLocalContext.CompiledFaceInterfaces.Find(FIntVector(2, 2, 0));
	TestNotNull(TEXT("Interior local cell keeps compiled faces"), InteriorFaces);
	if (InteriorFaces != nullptr)
	{
		TestEqual(
			TEXT("Cropped preserved neighbor remains internal empty occupancy"),
			InteriorFaces->Faces[static_cast<int32>(ELayoutFaceDirection::PosX)].NeighborKind,
			ESolveCellFaceNeighborKind::TerrainResidualNeighbor);
	}
	TestTrue(
		TEXT("Cropped cell keeps canonical Interior zone"),
		DoesCellMatchResolvedPlacementZone(
			InteriorLocalContext,
			FIntVector(2, 2, 0),
			ELayoutPlacementZone::Interior));
	TestEqual(
		TEXT("Cropped cell keeps authored module level"),
		GetFinalizedCellModuleLevel(InteriorLocalContext, FIntVector(2, 2, 0)),
		0);

	FSolveContext CornerLocalContext;
	if (!TestTrue(
		TEXT("Corner local view builds"),
		TryPrepareSparseStructuralLocalSolveView(
			Request,
			{FIntVector(0, 0, 0)},
			CornerLocalContext,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	const FSolveCellFaceInterfaceSet* CornerFaces =
		CornerLocalContext.CompiledFaceInterfaces.Find(FIntVector::ZeroValue);
	TestNotNull(TEXT("Corner local cell keeps compiled faces"), CornerFaces);
	if (CornerFaces != nullptr)
	{
		TestEqual(
			TEXT("True owning-region exterior remains exterior"),
			CornerFaces->Faces[static_cast<int32>(ELayoutFaceDirection::NegX)].NeighborKind,
			ESolveCellFaceNeighborKind::OuterBoundary);
		TestEqual(
			TEXT("Cropped normal-zone neighbor remains internal conditional interface"),
			CornerFaces->Faces[static_cast<int32>(ELayoutFaceDirection::PosX)].NeighborKind,
			ESolveCellFaceNeighborKind::ExternalPlannedNeighbor);
	}
	TestTrue(
		TEXT("Cropped corner keeps canonical Corner zone"),
		DoesCellMatchResolvedPlacementZone(
			CornerLocalContext,
			FIntVector::ZeroValue,
			ELayoutPlacementZone::Corner));
	const TArray<FSolveCandidate>* NeighborDomain = OwningContext.InitialDomains.Find(FIntVector(3, 2, 0));
	const FSolveCandidate* FilledNeighbor = NeighborDomain != nullptr
		? NeighborDomain->FindByPredicate([](const FSolveCandidate& Candidate) { return IsOccupiedCandidate(Candidate); })
		: nullptr;
	if (TestNotNull(TEXT("Fixture has a concrete neighboring module"), FilledNeighbor))
	{
		FSolveContext::FSolvePlacement Placement;
		Placement.bEmpty = false;
		Placement.VariantIndex = FilledNeighbor->VariantIndex;
		Placement.ModuleSnapshotIndex = FilledNeighbor->ModuleSnapshotIndex;
		Placement.ModuleSnapshotId = FilledNeighbor->ModuleSnapshotId;
		Placement.YawRotationSteps = FilledNeighbor->YawRotationSteps;
		Placement.BundleRootCell = FIntVector(3, 2, 0);
		OwningContext.Placements.Add(Placement.BundleRootCell, Placement);
		OwningContext.ChildReservationCells.Add(FIntVector(2, 3, 0));
		FSolveContext OccupiedLocal;
		TestTrue(TEXT("Local view retains committed and child occupancy"),
			TryBuildSparseStructuralLocalSolveView(OwningContext, {FIntVector(2, 2, 0)}, OccupiedLocal, FailureReason));
		TestTrue(TEXT("Outside placement survives cropping"), OccupiedLocal.Placements.Contains(Placement.BundleRootCell));
		const FSolveCellFaceInterfaceSet* Faces = OccupiedLocal.CompiledFaceInterfaces.Find(FIntVector(2, 2, 0));
		if (TestNotNull(TEXT("Occupied local faces compile"), Faces))
		{
			TestEqual(TEXT("Claimed preserved neighbor is filled, not residual empty"),
				Faces->Faces[static_cast<int32>(ELayoutFaceDirection::PosX)].NeighborKind,
				ESolveCellFaceNeighborKind::FixedFilledNeighbor);
			TestEqual(TEXT("Real child boundary survives cropping"),
				Faces->Faces[static_cast<int32>(ELayoutFaceDirection::PosY)].NeighborKind,
				ESolveCellFaceNeighborKind::ChildRegionContact);
		}
	}
	FSolveContext FailedLocal;
	OwningContext.bTimeBudgetExceeded = true;
	TestFalse(TEXT("Local preparation cannot hide exhausted shared budget"),
		TryBuildSparseStructuralLocalSolveView(OwningContext, {FIntVector(2, 2, 0)}, FailedLocal, FailureReason));
	return true;
}

/** Proves physical cropping cannot rewrite authored level, seam, zone, or Entry provenance. */
bool FLayoutSparseStructuralShiftedMetadataTest::RunTest(const FString& Parameters)
{
	using namespace LayoutProfileSolverInternal;

	FSolveContext OwningContext;
	OwningContext.FootprintSize = FIntPoint(5, 5);
	for (int32 Y = 0; Y < 5; ++Y)
	{
		for (int32 X = 0; X < 5; ++X)
		{
			FLayoutPlannedCell Cell;
			Cell.Cell = FIntVector(X, Y, 2);
			Cell.Intent = ELayoutCellIntent::Interior;
			Cell.ModuleLevelIndex = 0;
			Cell.PlacementZone = X == 0 || Y == 0 || X == 4 || Y == 4
				? ELayoutPlacementZone::Edge
				: ELayoutPlacementZone::Interior;
			OwningContext.OwningTopologyCellsByPhysicalCell.Add(Cell.Cell, Cell);
		}
	}
	FLayoutPlannedCell& ShiftedEntry =
		OwningContext.OwningTopologyCellsByPhysicalCell.FindChecked(FIntVector(2, 2, 2));
	ShiftedEntry.Intent = ELayoutCellIntent::Entry;
	ShiftedEntry.EntryOrigin = ELayoutEntryOrigin::TerrainSeam;
	ShiftedEntry.TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
	OwningContext.TerrainResidualRuleIdByCell.Add(ShiftedEntry.Cell, TEXT("ShiftedPreserve"));

	FSolveContext LocalContext;
	FString FailureReason;
	if (!TestTrue(
		TEXT("Shifted sparse local view builds"),
		TryBuildSparseStructuralLocalSolveView(
			OwningContext,
			{ShiftedEntry.Cell},
			LocalContext,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	const FLayoutPlannedCell* LocalEntry =
		LocalContext.OwningTopologyCellsByPhysicalCell.Find(ShiftedEntry.Cell);
	TestNotNull(TEXT("Shifted local view retains owning cell"), LocalEntry);
	if (LocalEntry == nullptr)
	{
		return false;
	}
	TestEqual(TEXT("Physical Z remains shifted"), LocalEntry->Cell.Z, 2);
	TestEqual(TEXT("GroundOnly authority remains authored level zero"),
		GetFinalizedCellModuleLevel(LocalContext, ShiftedEntry.Cell), 0);
	TestTrue(TEXT("Shifted local cell remains Interior"),
		DoesCellMatchResolvedPlacementZone(
			LocalContext, ShiftedEntry.Cell, ELayoutPlacementZone::Interior));
	TestEqual(TEXT("Terrain-seam mask survives cropping"),
		GetFinalizedTerrainSeamFaceMask(LocalContext, ShiftedEntry.Cell),
		LayoutFaceDirectionMask(ELayoutFaceDirection::PosX));
	TestEqual(TEXT("Entry provenance survives cropping"),
		LocalEntry->EntryOrigin, ELayoutEntryOrigin::TerrainSeam);
	return true;
}

/** Shared authored stair content; count scenarios retain separate preparation and assertions. */
static ULayoutProfileAsset* BuildSparseStructuralStairProfile()
{
	using namespace PorismLayoutTestUtilities;

	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("SparseStructuralExactStairRoute"),
		FIntPoint(7, 7),
		FIntPoint(7, 7),
		2,
		1,
		false);
	ULayoutModuleAsset* GenericLandingSource = Profile->ContentSet->Entries[0].ModuleSettings.Module;
	UChunkStructureTemplate* LandingTemplate = GenericLandingSource->Template.Get();
	ULayoutModuleAsset* ExactStairLower = CreateModule(
		GetTransientPackage(),
		TEXT("ExactStairLower"),
		LandingTemplate,
		{ELayoutCellIntent::VerticalAccess},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			FGameplayTagContainer(),
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	ULayoutModuleAsset* ExactStairUpper = CreateModule(
		GetTransientPackage(),
		TEXT("ExactStairUpper"),
		LandingTemplate,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			FGameplayTagContainer()));
	ExactStairLower->FaceRules.NegZ.OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
	ExactStairUpper->FaceRules.PosZ.OccupancyPolicy = ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor;
	ULayoutCompositeModuleAsset* ExactStairComposite =
		NewObject<ULayoutCompositeModuleAsset>(GetTransientPackage(), TEXT("ExactStairComposite"));
	FLayoutCompositeModuleCell& LowerPart = ExactStairComposite->Cells.AddDefaulted_GetRef();
	LowerPart.Module = ExactStairLower;
	LowerPart.LocalCell = FIntVector::ZeroValue;
	FLayoutCompositeModuleCell& UpperPart = ExactStairComposite->Cells.AddDefaulted_GetRef();
	UpperPart.Module = ExactStairUpper;
	UpperPart.LocalCell = FIntVector(0, 0, 1);
	FLayoutRegionContentEntry ExactStairEntry;
	ExactStairEntry.EntryId = TEXT("ExactStairCompositeEntry");
	ExactStairEntry.ContentKind = ELayoutRegionContentKind::Module;
	ExactStairEntry.ModuleSettings.CompositeModule = ExactStairComposite;
	Profile->ContentSet->Entries.Add(ExactStairEntry);
	Profile->bRequireAllTraversalChannelsReachable = true;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	FLayoutSparsePreserveTerrainRule PreserveRule;
	PreserveRule.RuleId = TEXT("PreservedInterior");
	PreserveRule.PlacementZone = ELayoutPlacementZone::Interior;
	PreserveRule.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::AnyLevel;
	Profile->SparsePlacementRules.Add(FInstancedStruct::Make(PreserveRule));
	return Profile;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutSparseStructuralPortlessHostTest,
	"PorismExtension.Layout.Solver.SparseStructural.PortlessHostProduction",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/** Optional ascent may reach the upper shell without exposing a ground traversal port. */
bool FLayoutSparseStructuralPortlessHostTest::RunTest(const FString& Parameters)
{
	using namespace LayoutProfileSolverInternal;
	ULayoutProfileAsset* Profile = BuildSparseStructuralStairProfile();
	Profile->bRequireAllTraversalChannelsReachable = false;
	auto* Composite = Profile->ContentSet->Entries.Last().ModuleSettings.CompositeModule.Get();
	auto* Lower = Composite->Cells[0].Module.Get();
	// Use separate modules: the current composite asset contract requires an
	// exposed traversal face on every VA leaf after internal glue suppression.
	auto& Entry = Profile->ContentSet->Entries.Last();
	Entry.EntryId = TEXT("PortlessAscent");
	Entry.ModuleSettings.CompositeModule = nullptr;
	Entry.ModuleSettings.Module = Lower;
	for (const auto Direction : {ELayoutFaceDirection::PosX, ELayoutFaceDirection::NegX, ELayoutFaceDirection::PosY, ELayoutFaceDirection::NegY})
	{
		Lower->FaceRules.FindRule(Direction)->ConnectedTraversalChannels.Reset();
	}
	auto Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, 8173, TEXT("SparseStructural/PortlessHost"));
	const auto Topology = BuildRequestBackedRegionPreparedTopology(Request);
	if (!TestTrue(TEXT("Portless topology prepares"), Topology.SolveResult.bSucceeded))
	{
		AddError(Topology.SolveResult.FailureReason);
		for (const auto& Snapshot : Request.ModuleCatalog.Modules)
		{
			for (const auto& Message : Snapshot.Validation.Messages) AddInfo(Message.Message);
		}
		for (const auto& Message : Request.ContentSetSnapshot.Validation.Messages) AddInfo(Message.Message);
		for (const auto& Message : Request.ProfileSnapshot.Validation.Messages) AddInfo(Message.Message);
		return false;
	}
	auto Cells = Topology.SolveResult.PlannedCells;
	TArray<FLayoutVerticalAccessHostGroup> Groups;
	FString Failure;
	if (!TestTrue(TEXT("Optional portless host admission succeeds"),
		RebuildVerticalAccessPlanningForPreparedTopology(Request, Cells, Groups, Failure)))
	{
		AddError(Failure);
		return false;
	}
	if (!TestEqual(TEXT("One counted host demand"), Groups.Num(), 1)) return false;
	const auto* LowerSnapshot = Request.ModuleCatalog.Modules.FindByPredicate([](const auto& Snapshot)
		{ return Snapshot.DebugName.ToString().Contains(TEXT("ExactStairLower")); });
	if (!TestNotNull(TEXT("Portless module snapshot exists"), LowerSnapshot)) return false;
	Groups[0].Options.RemoveAll([&](const auto& Option)
		{ return Option.LowerModuleSnapshotId != LowerSnapshot->SnapshotId; });
	if (!TestTrue(TEXT("Exact portless module remains available"), !Groups[0].Options.IsEmpty())) return false;
	Groups[0].Options.SetNum(1);
	TestEqual(TEXT("No invented lower route mask"), Groups[0].Options[0].LowerTraversalPortFaceMask, uint8(0));
	Request.PlannedCells = Cells;
	Request.PrecomputedPlannedCells = Cells;
	Request.VerticalAccessHostGroups = Groups;
	LayoutSolveExecution::FScope Scope(0.0, 50000);
	FLayoutRegionSolveRequest Authority;
	const auto Result = SolveRequestBackedRegion(Request, true, &Authority);
	if (!TestTrue(TEXT("Optional portless ascent survives production proof"), Result.SolveResult.bSucceeded))
	{
		AddError(Result.SolveResult.FailureReason);
		return false;
	}
	TestTrue(TEXT("Upper shell connection was attempted and retained"), Result.SolveResult.RouteConstraints.ContainsByPredicate(
		[](const auto& Constraint) { return Constraint.Cell.Z == 1; }));
	TestTrue(TEXT("Production used sparse work"), LayoutSolveExecution::CurrentThreadLedger()->SparseWork > 0);
	Request.ProfileSnapshot.bRequireAllTraversalChannelsReachable = true;
	const auto Required = SolveRequestBackedRegion(Request, true);
	TestFalse(TEXT("Required traversal rejects the missing ground connection"), Required.SolveResult.bSucceeded);
	return true;
}

/** Proves one exact selected stair, gate, and accepted child connect through placed corridor modules. */
bool FLayoutSparseStructuralExactStairRouteTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;
	using namespace LayoutProfileSolverInternal;
	ULayoutProfileAsset* Profile = BuildSparseStructuralStairProfile();
	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		8173,
		TEXT("SparseStructural/ExactStairRoute"));
	FLayoutCommittedTraversalAnchor& Anchor =
		Request.CommittedTraversalAnchors.AddDefaulted_GetRef();
	Anchor.Cell = FIntVector(4, 4, 0);
	Anchor.TraversalChannel = LayoutGameplayTags::TraversalPrimary;
	const FIntVector AnchorCell = Anchor.Cell;
	const FGameplayTag AnchorChannel = Anchor.TraversalChannel;
	const FLayoutRegionSolveRequest ChildRequest = SparseStructuralTestFixture::BuildChildRequest(AnchorCell + FIntVector(1, 0, 0));
	const FLayoutRegionSolveResult ChildProof = FLayoutProfileSolver::SolveRegion(ChildRequest);
	if (!TestTrue(TEXT("Compact child solves before parent composition"), ChildProof.SolveResult.bSucceeded))
	{
		AddError(ChildProof.SolveResult.FailureReason);
		return false;
	}
	const FLayoutCommittedEndpointAnchor ChildEndpoint = ChildProof.CommittedEndpointAnchors[0];

	const FLayoutRegionSolveResult PreparedTopology =
		BuildRequestBackedRegionPreparedTopology(Request);
	if (!TestTrue(TEXT("Sparse topology prepares without parent solve"), PreparedTopology.SolveResult.bSucceeded))
	{
		AddError(PreparedTopology.SolveResult.FailureReason);
		return false;
	}
	TArray<FLayoutPlannedCell> PreparedCells = PreparedTopology.SolveResult.PlannedCells;
	FString FailureReason;
	TArray<FLayoutVerticalAccessHostGroup> HostGroups;
	if (!TestTrue(TEXT("Exact1 local host alternatives prepare"),
		RebuildVerticalAccessPlanningForPreparedTopology(Request, PreparedCells, HostGroups, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	if (!TestEqual(TEXT("Exact1 prepares one counted host group"), HostGroups.Num(), 1)
		|| HostGroups[0].Options.IsEmpty())
	{
		return false;
	}
	HostGroups[0].Options.RemoveAll([](const FLayoutVerticalAccessHostOption& Option)
	{
		return !Option.LowerModuleSnapshotId.ToString().Contains(TEXT("ExactStairComposite"));
	});
	if (!TestTrue(TEXT("Exact1 retains vertically traversable composite witness"), !HostGroups[0].Options.IsEmpty()))
	{
		return false;
	}
	HostGroups[0].Options.SetNum(1);
	Request.PlannedCells = PreparedCells;
	Request.PrecomputedPlannedCells = PreparedCells;
	Request.VerticalAccessHostGroups = HostGroups;
	const FLayoutPlannedCell* Entry = PreparedCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry;
	});
	if (!TestNotNull(TEXT("Prepared topology retains qualified gate"), Entry))
	{
		return false;
	}
	const FLayoutModuleSolveSnapshot* GateSnapshot = Request.ModuleCatalog.Modules.FindByPredicate(
		[](const FLayoutModuleSolveSnapshot& Snapshot)
		{
			return Snapshot.SnapshotId.ToString().Contains(TEXT("EntryModule"));
		});
	if (!TestNotNull(TEXT("Fixture retains exact gate module"), GateSnapshot))
	{
		return false;
	}
	FLayoutCellCandidateDomainRestriction GateRestriction;
	GateRestriction.Cell = Entry->Cell;
	GateRestriction.RestrictionId = TEXT("Gate.Exact");
	GateRestriction.AllowedCandidates.Add({GateSnapshot->SnapshotId, 2});
	Request.CandidateDomainRestrictions.Add(GateRestriction);
	if (!TestTrue(TEXT("Exact stair witness constrains the local claim"),
		ApplySelectedVerticalAccessCandidateWitnessesForTests(HostGroups, {0}, Request, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	{
		LayoutSolveExecution::FScope Scope(0.0, 50000);
		FLayoutRegionSolveRequest FinalAuthority;
		const auto Production = SolveRequestBackedRegion(Request, true, &FinalAuthority);
		if (!TestTrue(TEXT("Selected multi-level host uses production sparse assembly"), Production.SolveResult.bSucceeded))
		{
			AddError(Production.SolveResult.FailureReason);
			return false;
		}
		TestTrue(TEXT("Production host assignment charges sparse work"), LayoutSolveExecution::CurrentThreadLedger()->SparseWork > 0);
		TestEqual(TEXT("Production retains selected host demand"), FinalAuthority.VerticalAccessHostGroups.Num(), 1);
	}
	const FLayoutVerticalAccessHostOption& Stair = HostGroups[0].Options[0];
	TSet<FIntVector> LocalWorkCells;
	for (const FLayoutPlannedCell& Cell : PreparedCells)
	{
		if (Cell.PlacementZone == ELayoutPlacementZone::Interior
			|| Cell.Intent == ELayoutCellIntent::Entry
			|| Stair.OccupiedCells.Contains(Cell.Cell)
			|| Stair.RequiredFilledSupportCells.Contains(Cell.Cell))
		{
			LocalWorkCells.Add(Cell.Cell);
		}
	}
	FSolveContext PreparedContext;
	if (!TestTrue(TEXT("Sparse local candidate view prepares"),
		TryPrepareSparseStructuralLocalSolveView(Request, LocalWorkCells, PreparedContext, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	TArray<FSparseStructuralTraversalPort> Ports;
	if (!TestTrue(TEXT("Selected proofs compile exact traversal ports"),
		BuildSparseStructuralTraversalPorts(PreparedContext, HostGroups, {0}, Ports, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Gate and lower/upper stair ports compile before child import"), Ports.Num(), 3);
	TArray<FSparseStructuralTraversalPort> FailedPorts = Ports;
	TestFalse(TEXT("Invalid late host selection rejects port compilation"),
		BuildSparseStructuralTraversalPorts(PreparedContext, HostGroups, {INDEX_NONE}, FailedPorts, FailureReason));
	TestTrue(TEXT("Late compilation failure clears all partial and stale ports"), FailedPorts.IsEmpty());

	FSolveContext ChannelContext = PreparedContext;
	FLayoutModuleSolveSnapshot& ChannelSnapshot = ChannelContext.ModuleSnapshots[Stair.LowerModuleSnapshotIndex];
	for (FLayoutLocalCellFaceRuleSnapshot& CellRules : ChannelSnapshot.GeneratedLocalCellFaceRules)
	{
		if (CellRules.LocalCell != FIntVector::ZeroValue) continue;
		for (FLayoutFaceRule& Face : CellRules.ExposedFaceRules)
		{
			if (FLayoutDirectionUtils::RotateYaw(Face.Direction, Stair.LowerYawRotationSteps) == ELayoutFaceDirection::PosX)
			{
				Face.ConnectedTraversalChannels = MakeTags({LayoutGameplayTags::TraversalSecondary});
			}
		}
	}
	TArray<FSparseStructuralTraversalPort> ChannelPorts;
	TestTrue(TEXT("Distinct authored port channels remain separate"),
		BuildSparseStructuralTraversalPorts(ChannelContext, HostGroups, {0}, ChannelPorts, FailureReason));
	TestTrue(TEXT("Secondary channel retains only its actual candidate-local face"),
		ChannelPorts.ContainsByPredicate([&Stair](const FSparseStructuralTraversalPort& Port)
		{
			return Port.Cell == Stair.LowerCell && Port.TraversalChannel == LayoutGameplayTags::TraversalSecondary
				&& Port.RouteFaceMask == LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
		}));
	TestFalse(TEXT("Primary channel cannot borrow secondary-only face"),
		ChannelPorts.ContainsByPredicate([&Stair](const FSparseStructuralTraversalPort& Port)
		{
			return Port.Cell == Stair.LowerCell && Port.TraversalChannel == LayoutGameplayTags::TraversalPrimary
				&& LayoutFaceMaskContainsDirection(Port.RouteFaceMask, ELayoutFaceDirection::PosX);
		}));

	if (!TestTrue(TEXT("Accepted child joins the Exact1 claim"),
		TryImportSparseStructuralChildProof(PreparedContext, ChildRequest, ChildProof, Ports, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestEqual(TEXT("Gate, actual child ingress and stair ports compile"), Ports.Num(), 4);
	FSolveContext SolvedContext;
	FSparseStructuralRouteAssignment Assignment;
	if (!TestTrue(TEXT("Exact-port corridor assignment solves placed graph"),
		TrySolveSparseStructuralPortAssignment(PreparedContext, Ports, SolvedContext, Assignment, FailureReason)))
	{
		AddError(FailureReason);
		for (const TArray<FIntVector>& Path : Assignment.Paths)
		{
			TArray<FString> Cells;
			for (const FIntVector& Cell : Path) Cells.Add(Cell.ToString());
			AddError(FString::Printf(TEXT("Rejected path: %s"), *FString::Join(Cells, TEXT(" -> "))));
		}
		return false;
	}
	TestEqual(TEXT("Accepted route work advances inherited budget"),
		PreparedContext.CandidateAttemptCount, SolvedContext.CandidateAttemptCount);
	TestTrue(TEXT("Route expansion and corridor work consume budget"), PreparedContext.CandidateAttemptCount > 0);
	int32 VerticalAccessRootCount = 0;
	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& Pair : SolvedContext.Placements)
	{
		if (Pair.Value.bBundleRoot
			&& FSolveContext::IsOccupiedPlacement(Pair.Value)
			&& SolvedContext.PlannedCellIntents.FindRef(Pair.Key) == ELayoutCellIntent::VerticalAccess)
		{
			++VerticalAccessRootCount;
		}
	}
	TestEqual(TEXT("Exact1 route commits one counted stair root"), VerticalAccessRootCount, 1);
	TestTrue(TEXT("Child proof identity survives route solve"), Assignment.RetainedProofIds.Contains(ChildEndpoint.CommitmentId));
	TestTrue(TEXT("Exact child endpoint payload survives route solve"),
		Assignment.RetainedEndpointCommitments.ContainsByPredicate([&ChildEndpoint, AnchorChannel](const FLayoutCommittedEndpointAnchor& Retained)
		{
			return Retained.CommitmentId == ChildEndpoint.CommitmentId
				&& Retained.LocalCell == ChildEndpoint.LocalCell
				&& Retained.FaceDirection == ChildEndpoint.FaceDirection
				&& Retained.TraversalChannels.HasTagExact(AnchorChannel);
		}));
	TestTrue(TEXT("Required placed route includes child endpoint anchor"),
		SolvedContext.Result.CompiledReservations.ContainsByPredicate([AnchorCell](const FLayoutCellReservationRecord& Reservation)
		{
			return Reservation.Cell == AnchorCell
				&& Reservation.ReservationKind == ELayoutCellReservationKind::RequiredRoute;
		}));
	TestTrue(TEXT("Unclaimed upper Interior remains preserved empty"),
		PreparedCells.ContainsByPredicate([&SolvedContext](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell.Z == 1
				&& Cell.PlacementZone == ELayoutPlacementZone::Interior
				&& !SolvedContext.Placements.Contains(Cell.Cell);
		}));

	FLayoutRegionSolveRequest RetryRequest = Request;
	const FLayoutModuleSolveSnapshot* RetryGeneric = RetryRequest.ModuleCatalog.Modules.FindByPredicate(
		[](const FLayoutModuleSolveSnapshot& Snapshot)
		{
			return Snapshot.SnapshotId.ToString().Contains(TEXT("GenericModule"));
		});
	if (!TestNotNull(TEXT("Retry fixture retains corridor source"), RetryGeneric))
	{
		return false;
	}
	FLayoutModuleSolveSnapshot BlockedRouteSnapshot = *RetryGeneric;
	BlockedRouteSnapshot.SnapshotId = TEXT("BlockedRouteModule");
	BlockedRouteSnapshot.DebugName = TEXT("BlockedRouteModule");
	auto BlockHorizontalFaces = [](FLayoutModuleFaceRules& FaceRules)
	{
		for (const ELayoutFaceDirection Direction : {
			ELayoutFaceDirection::PosX,
			ELayoutFaceDirection::NegX,
			ELayoutFaceDirection::PosY,
			ELayoutFaceDirection::NegY})
		{
			FLayoutFaceRule* FaceRule = FaceRules.FindRule(Direction);
			FaceRule->ConnectionTag = LayoutGameplayTags::FaceSolid;
			FaceRule->AllowedConnectionTags.Reset();
			FaceRule->AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
		}
	};
	BlockHorizontalFaces(BlockedRouteSnapshot.EffectiveFaceRules);
	for (FLayoutLocalCellFaceRuleSnapshot& LocalCellRules : BlockedRouteSnapshot.GeneratedLocalCellFaceRules)
	{
		for (FLayoutFaceRule& FaceRule : LocalCellRules.ExposedFaceRules)
		{
			if (FaceRule.Direction != ELayoutFaceDirection::PosZ && FaceRule.Direction != ELayoutFaceDirection::NegZ)
			{
				FaceRule.ConnectionTag = LayoutGameplayTags::FaceSolid;
				FaceRule.AllowedConnectionTags.Reset();
				FaceRule.AllowedConnectionTags.AddTag(LayoutGameplayTags::FaceSolid);
			}
		}
	}
	RetryRequest.ModuleCatalog.Modules.Add(BlockedRouteSnapshot);
	FIntVector BlockedCell = AnchorCell;
	for (const TArray<FIntVector>& Path : Assignment.Paths)
	{
		if (Path.Num() > 3 && Path.Last() == AnchorCell)
		{
			BlockedCell = Path[Path.Num() - 3];
			break;
		}
	}
	if (!TestNotEqual(TEXT("Baseline route has an obstructable child approach"), BlockedCell, AnchorCell))
	{
		return false;
	}
	FLayoutCellCandidateDomainRestriction BlockedRouteRestriction;
	BlockedRouteRestriction.Cell = BlockedCell;
	BlockedRouteRestriction.RestrictionId = TEXT("Route.FirstApproach");
	BlockedRouteRestriction.AllowedCandidates.Add({BlockedRouteSnapshot.SnapshotId, 0});
	RetryRequest.CandidateDomainRestrictions.Add(BlockedRouteRestriction);
	FSolveContext RetryContext;
	if (!TestTrue(TEXT("Obstructed sparse route view prepares"),
		TryPrepareSparseStructuralLocalSolveView(RetryRequest, LocalWorkCells, RetryContext, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TArray<FSparseStructuralTraversalPort> RetryPorts;
	if (!TestTrue(TEXT("Retry reuses the same accepted child result"),
		BuildSparseStructuralTraversalPorts(RetryContext, HostGroups, {0}, RetryPorts, FailureReason)
		&& TryImportSparseStructuralChildProof(RetryContext, ChildRequest, ChildProof, RetryPorts, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	const int32 BaselinePlacementCount = RetryContext.Placements.Num();
	FSolveContext RetriedSolvedContext;
	FSparseStructuralRouteAssignment RetriedAssignment;
	if (!TestTrue(TEXT("Rejected first corridor retries through legal alternate"),
		TrySolveSparseStructuralPortAssignment(RetryContext, RetryPorts, RetriedSolvedContext, RetriedAssignment, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestTrue(TEXT("Obstructed route consumed more than one bounded attempt"), RetriedAssignment.AttemptCount > 1);
	TestTrue(TEXT("Rejected route cell is absent from committed alternate"),
		RetriedAssignment.RejectedRouteCells.Contains(BlockedCell)
		&& !RetriedSolvedContext.Placements.Contains(BlockedCell));
	TestTrue(TEXT("Unrelated child proof survives route retry"),
		RetriedAssignment.RetainedEndpointCommitments.ContainsByPredicate([&ChildEndpoint](const FLayoutCommittedEndpointAnchor& Retained)
		{
			return Retained.CommitmentId == ChildEndpoint.CommitmentId
				&& Retained.LocalCell == ChildEndpoint.LocalCell
				&& Retained.FaceDirection == ChildEndpoint.FaceDirection;
		}));
	TestEqual(TEXT("Rejected route leaves prepared proof context unchanged"),
		RetryContext.Placements.Num(), BaselinePlacementCount);

	TestTrue(TEXT("Corridor retries retain the exact immutable child placement proof"),
		RetriedAssignment.RetainedChildProofs.Num() == 1
		&& RetriedAssignment.RetainedChildProofs[0] == RetryContext.SparseStructuralChildProofs[0]);
	TestFalse(TEXT("Parent never re-places child occupancy"), RetriedSolvedContext.Placements.Contains(ChildProof.RegionCellOffset));
	TArray<FSparseStructuralTraversalPort> ImpossiblePorts = RetryPorts;
	ImpossiblePorts[0].RouteFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::NegX);
	FSolveContext RejectedContext = RetriedSolvedContext;
	FSparseStructuralRouteAssignment RejectedAssignment;
	TestFalse(TEXT("Impossible exact port rejects without apply authority"),
		TrySolveSparseStructuralPortAssignment(RetryContext, ImpossiblePorts, RejectedContext, RejectedAssignment, FailureReason));
	TestTrue(TEXT("Rejected assignment publishes no placements or route claims"),
		RejectedContext.Placements.IsEmpty() && RejectedContext.Result.RouteConstraints.IsEmpty());
	TestEqual(TEXT("Impossible route also leaves prepared proof context unchanged"),
		RetryContext.Placements.Num(), BaselinePlacementCount);

	TArray<FSparseStructuralTraversalPort> OverlappingPorts = RetryPorts;
	FSparseStructuralTraversalPort Overlap = OverlappingPorts[1];
	Overlap.ProofId = TEXT("OverlappingProof");
	Overlap.EndpointCommitment.Reset();
	OverlappingPorts.Add(MoveTemp(Overlap));
	RejectedContext = RetriedSolvedContext;
	TestFalse(TEXT("Different selected proofs cannot overlap occupied claims"),
		TrySolveSparseStructuralPortAssignment(RetryContext, OverlappingPorts, RejectedContext, RejectedAssignment, FailureReason));
	TestTrue(TEXT("Overlapping selected proofs also publish no solve state"),
		RejectedContext.Placements.IsEmpty() && RejectedContext.Result.RouteConstraints.IsEmpty());

	TArray<FSparseStructuralTraversalPort> SharedSupportPorts = Ports;
	SharedSupportPorts[0].RequiredSupportCells.Add(AnchorCell);
	SharedSupportPorts.Last().RequiredSupportCells.Add(AnchorCell);
	FSolveContext SharedSupportResult;
	TestTrue(TEXT("Two providers may require the same already owned filled support"),
		TrySolveSparseStructuralPortAssignment(PreparedContext, SharedSupportPorts, SharedSupportResult, RejectedAssignment, FailureReason));

	FSolveContext Disconnected = PreparedContext;
	for (auto It = Disconnected.PlannedCellIntents.CreateIterator(); It; ++It)
	{
		if (It.Key().X == 3) It.RemoveCurrent();
	}
	RejectedContext = SolvedContext;
	TestFalse(TEXT("Disconnected local routing space rejects rather than crossing omitted topology"),
		TrySolveSparseStructuralPortAssignment(Disconnected, Ports, RejectedContext, RejectedAssignment, FailureReason));
	TestTrue(TEXT("Disconnected routes clear all apply output"), RejectedContext.Placements.IsEmpty());

	TArray<FSparseStructuralTraversalPort> ClearanceConflict = Ports;
	ClearanceConflict[0].RequiredClearanceCells.Add(AnchorCell);
	TestFalse(TEXT("Selected clearance cannot overlap another proof's occupied cell"),
		TrySolveSparseStructuralPortAssignment(PreparedContext, ClearanceConflict, RejectedContext, RejectedAssignment, FailureReason));
	FSolveContext OneWorkUnit = PreparedContext;
	OneWorkUnit.MaxCandidateAttempts = OneWorkUnit.CandidateAttemptCount + 1;
	TestFalse(TEXT("Route expansion cannot exceed remaining shared work"),
		TrySolveSparseStructuralPortAssignment(OneWorkUnit, Ports, RejectedContext, RejectedAssignment, FailureReason));
	TestEqual(TEXT("Rejected work stays charged for subsequent attempts"),
		OneWorkUnit.CandidateAttemptCount, OneWorkUnit.MaxCandidateAttempts);
	for (int32 BudgetCase = 0; BudgetCase < 2; ++BudgetCase)
	{
		FSolveContext Exhausted = PreparedContext;
		if (BudgetCase == 0) Exhausted.MaxCandidateAttempts = Exhausted.CandidateAttemptCount;
		else
		{
			Exhausted.MaxSolveDurationSeconds = 0.001;
			Exhausted.SolveStartTimeSeconds = FPlatformTime::Seconds() - 1.0;
		}
		RejectedContext = SolvedContext;
		TestFalse(TEXT("Inherited exhaustion rejects before sparse route work"),
			TrySolveSparseStructuralPortAssignment(Exhausted, Ports, RejectedContext, RejectedAssignment, FailureReason));
		TestEqual(TEXT("Exhaustion starts no route assignment"), RejectedAssignment.AttemptCount, 0);
		TestTrue(TEXT("Exhaustion clears stale solve output"), RejectedContext.Placements.IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutSparseStructuralExactTwoStairsTest,
	"PorismExtension.Layout.Solver.SparseStructural.ExactTwoStairs",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutSparseStructuralExactTwoStairsTest::RunTest(const FString& Parameters)
{
	using namespace LayoutProfileSolverInternal;
	ULayoutProfileAsset* Profile = BuildSparseStructuralStairProfile();
	Profile->VerticalAccessCount = 2;
	const FIntVector AnchorCell(4, 4, 0);
	const FGameplayTag AnchorChannel = LayoutGameplayTags::TraversalPrimary;
	const FLayoutCommittedEndpointAnchor ChildEndpoint =
		SparseStructuralTestFixture::BuildChildRequest(AnchorCell + FIntVector(1, 0, 0)).CommittedEndpointAnchors[0];
	FString FailureReason;
	FLayoutRegionSolveRequest TwoStairRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		8173,
		TEXT("SparseStructural/TwoExactStairs"));
	FLayoutCommittedTraversalAnchor TwoStairAnchor;
	TwoStairAnchor.Cell = AnchorCell;
	TwoStairAnchor.TraversalChannel = AnchorChannel;
	TwoStairRequest.CommittedTraversalAnchors.Add(TwoStairAnchor);
	FLayoutCommittedEndpointAnchor TwoStairEndpoint = ChildEndpoint;
	TwoStairEndpoint.LocalCell = AnchorCell;
	TwoStairRequest.CommittedEndpointAnchors.Add(TwoStairEndpoint);
	const FLayoutRegionSolveResult TwoStairTopology =
		BuildRequestBackedRegionPreparedTopology(TwoStairRequest);
	if (!TestTrue(TEXT("Two-stair sparse topology prepares"), TwoStairTopology.SolveResult.bSucceeded))
	{
		AddError(TwoStairTopology.SolveResult.FailureReason);
		return false;
	}
	TArray<FLayoutPlannedCell> TwoStairCells = TwoStairTopology.SolveResult.PlannedCells;
	TArray<FLayoutVerticalAccessHostGroup> TwoStairGroups;
	if (!TestTrue(TEXT("Two counted host groups prepare"),
		RebuildVerticalAccessPlanningForPreparedTopology(
			TwoStairRequest,
			TwoStairCells,
			TwoStairGroups,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	if (!TestEqual(TEXT("Exact2 produces two host groups"), TwoStairGroups.Num(), 2))
	{
		return false;
	}
	for (FLayoutVerticalAccessHostGroup& Group : TwoStairGroups)
	{
		Group.Options.RemoveAll([](const FLayoutVerticalAccessHostOption& Option)
		{
			return !Option.LowerModuleSnapshotId.ToString().Contains(TEXT("ExactStairComposite"));
		});
	}
	int32 FirstOptionIndex = INDEX_NONE;
	int32 SecondOptionIndex = INDEX_NONE;
	for (int32 FirstIndex = 0; FirstIndex < TwoStairGroups[0].Options.Num() && FirstOptionIndex == INDEX_NONE; ++FirstIndex)
	{
		for (int32 SecondIndex = 0; SecondIndex < TwoStairGroups[1].Options.Num(); ++SecondIndex)
		{
			const FLayoutVerticalAccessHostOption& First = TwoStairGroups[0].Options[FirstIndex];
			const FLayoutVerticalAccessHostOption& Second = TwoStairGroups[1].Options[SecondIndex];
			const FIntVector Separation = First.LowerCell - Second.LowerCell;
			if (Separation.Z == 0 && FMath::Abs(Separation.X) + FMath::Abs(Separation.Y) == 1
				&& !First.OccupiedCells.Contains(AnchorCell)
				&& !Second.OccupiedCells.Contains(AnchorCell)
				&& !First.OccupiedCells.ContainsByPredicate([&Second](const FIntVector& Cell)
				{
					return Second.OccupiedCells.Contains(Cell);
				}))
			{
				FirstOptionIndex = FirstIndex;
				SecondOptionIndex = SecondIndex;
				break;
			}
		}
	}
	if (!TestTrue(TEXT("Exact2 retains legal adjacent non-overlapping composite witnesses"),
		FirstOptionIndex != INDEX_NONE && SecondOptionIndex != INDEX_NONE))
	{
		return false;
	}
	TwoStairGroups[0].Options = {TwoStairGroups[0].Options[FirstOptionIndex]};
	TwoStairGroups[1].Options = {TwoStairGroups[1].Options[SecondOptionIndex]};
	for (FLayoutPlannedCell& Cell : TwoStairCells)
	{
		const bool bSelectedRoot = TwoStairGroups.ContainsByPredicate([&Cell](const FLayoutVerticalAccessHostGroup& Group)
		{
			return Group.Options[0].LowerCell == Cell.Cell;
		});
		if (bSelectedRoot)
		{
			Cell.Intent = ELayoutCellIntent::VerticalAccess;
		}
		else if (Cell.Intent == ELayoutCellIntent::VerticalAccess)
		{
			Cell.Intent = Cell.PlacementZone == ELayoutPlacementZone::Interior
				? ELayoutCellIntent::Interior : ELayoutCellIntent::Boundary;
		}
	}
	TwoStairRequest.PlannedCells = TwoStairCells;
	TwoStairRequest.PrecomputedPlannedCells = TwoStairCells;
	TwoStairRequest.VerticalAccessHostGroups = TwoStairGroups;
	const FLayoutPlannedCell* TwoStairEntry = TwoStairCells.FindByPredicate([](const FLayoutPlannedCell& Cell)
	{
		return Cell.Intent == ELayoutCellIntent::Entry;
	});
	if (!TestNotNull(TEXT("Exact2 retains gate"), TwoStairEntry)
		|| !TestEqual(TEXT("Exact2 deterministic gate remains on negative X edge"), TwoStairEntry->Cell.X, 0))
	{
		return false;
	}
	const FLayoutModuleSolveSnapshot* TwoStairGateSnapshot = TwoStairRequest.ModuleCatalog.Modules.FindByPredicate(
		[](const FLayoutModuleSolveSnapshot& Snapshot)
		{
			return Snapshot.SnapshotId.ToString().Contains(TEXT("EntryModule"));
		});
	if (!TestNotNull(TEXT("Exact2 retains exact gate module"), TwoStairGateSnapshot))
	{
		return false;
	}
	FLayoutCellCandidateDomainRestriction TwoStairGateRestriction;
	TwoStairGateRestriction.Cell = TwoStairEntry->Cell;
	TwoStairGateRestriction.RestrictionId = TEXT("TwoStairGate.Exact");
	TwoStairGateRestriction.AllowedCandidates.Add({TwoStairGateSnapshot->SnapshotId, 2});
	TwoStairRequest.CandidateDomainRestrictions.Add(TwoStairGateRestriction);
	if (!TestTrue(TEXT("Both exact stair witnesses constrain one local claim"),
		ApplySelectedVerticalAccessCandidateWitnessesForTests(
			TwoStairGroups,
			{0, 0},
			TwoStairRequest,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TSet<FIntVector> TwoStairWorkCells;
	for (const FLayoutPlannedCell& Cell : TwoStairCells)
	{
		if (Cell.PlacementZone == ELayoutPlacementZone::Interior || Cell.Intent == ELayoutCellIntent::Entry)
		{
			TwoStairWorkCells.Add(Cell.Cell);
		}
	}
	for (const FLayoutVerticalAccessHostGroup& Group : TwoStairGroups)
	{
		TwoStairWorkCells.Append(Group.Options[0].OccupiedCells);
		TwoStairWorkCells.Append(Group.Options[0].RequiredFilledSupportCells);
	}
	FSolveContext TwoStairPreparedContext;
	if (!TestTrue(TEXT("Two-stair sparse candidate view prepares"),
		TryPrepareSparseStructuralLocalSolveView(
			TwoStairRequest,
			TwoStairWorkCells,
			TwoStairPreparedContext,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TArray<FSparseStructuralTraversalPort> TwoStairPorts;
	if (!TestTrue(TEXT("Two selected roots compile correlated exact ports"),
		BuildSparseStructuralTraversalPorts(
			TwoStairPreparedContext,
			TwoStairGroups,
			{0, 0},
			TwoStairPorts,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	FSolveContext TwoStairSolvedContext;
	FSparseStructuralRouteAssignment TwoStairAssignment;
	if (!TestTrue(TEXT("Two non-overlapping stairs join one placed graph"),
		TrySolveSparseStructuralPortAssignment(
			TwoStairPreparedContext,
			TwoStairPorts,
			TwoStairSolvedContext,
			TwoStairAssignment,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	int32 TwoStairRootCount = 0;
	for (const TPair<FIntVector, FSolveContext::FSolvePlacement>& Pair : TwoStairSolvedContext.Placements)
	{
		if (Pair.Value.bBundleRoot
			&& FSolveContext::IsOccupiedPlacement(Pair.Value)
			&& TwoStairSolvedContext.PlannedCellIntents.FindRef(Pair.Key) == ELayoutCellIntent::VerticalAccess)
		{
			++TwoStairRootCount;
		}
	}
	TestEqual(TEXT("Exact2 commits two counted roots without overlap"), TwoStairRootCount, 2);
	return true;
}
