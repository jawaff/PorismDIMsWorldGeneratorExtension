// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutSolvedArtifact.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Async/LayoutWorkerSolvePacket.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutZoneFeatureDemand.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Async/Async.h"
#include "Misc/AutomationTest.h"
#include "Misc/Guid.h"
#include "UObject/Package.h"

namespace
{
	using namespace PorismLayoutTestUtilities;

	UObject* CreateZoneFeatureDemandTestOuter(const TCHAR* BaseName)
	{
		return CreatePackage(*FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
	}

	FLayoutRegionContentEntry MakeZoneFeatureModuleEntry(
		const FName EntryId,
		ULayoutModuleAsset* Module,
		const bool bProvidesRoom)
	{
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = EntryId;
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		if (bProvidesRoom)
		{
			Entry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
		}
		return Entry;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandExactTagMatchingTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.ExactTagMatching",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandCanonicalEligibilityTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.CanonicalEligibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandCompilesRequirementsInStableOrderTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.CompilesRequirementsInStableOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandPassiveSeamSuppressionRemovesModuleCreditTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.PassiveSeamSuppressionRemovesModuleCredit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandThreeLevelDirectChildContractTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.ThreeLevelDirectChildContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandSharedSeamPerimeterChildPlacementTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.SharedSeamPerimeterChildPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandConstructiveLeafSolveTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.ConstructiveLeafSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandCompositeCountsOnceTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.CompositeCountsOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandRequiredChildPreparationFailureIsTypedTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.RequiredChildPreparationFailureIsTyped",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandHostIndependentCapacityFailureTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.HostIndependentCapacityFailure",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutZoneFeatureDemandParentOnlyVerticalAccessAfterOptionalChildTest,
	"PorismExtension.Layout.Solver.ZoneFeatureDemand.ParentOnlyVerticalAccessAfterOptionalChild",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutZoneFeatureDemandParentOnlyVerticalAccessAfterOptionalChildTest::RunTest(const FString& Parameters)
{
	FLayoutRegionSolveRequest Request;
	Request.RegionDebugPath = TEXT("ParentOnlyVerticalAccessRoot");
	Request.ProfileSnapshot.VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Request.ProfileSnapshot.VerticalAccessCount = 1;
	Request.ProfileSnapshot.MaxVerticalAccessCount = 1;

	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Z = 0; Z < 2; ++Z)
	{
		FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
		Cell.Cell = FIntVector(0, 0, Z);
		Cell.ModuleLevelIndex = Z;
		Cell.Intent = ELayoutCellIntent::VerticalAccess;
	}

	const FLayoutRecursiveVerticalAccessSummary Summary =
		LayoutProfileSolverInternal::ResolveParentOnlyVerticalAccessOwnershipForTests(
			Request,
			PlannedCells);
	TestTrue(TEXT("Parent-only host VerticalAccess remains satisfied without child contracts"),
		Summary.FailureReason.IsEmpty());
	TestEqual(TEXT("Parent-only host VerticalAccess keeps exact required count"),
		Summary.RequiredHostProviderCount,
		1);
	TestEqual(TEXT("Parent-only host VerticalAccess resolves one provider group"),
		Summary.ResolvedHostProviderCount,
		1);
	TestEqual(TEXT("Parent-only host VerticalAccess counts one retained parent provider"),
		Summary.CountedParentProviderCount,
		1);
	TestEqual(TEXT("Parent-only host VerticalAccess keeps one representative cell"),
		Summary.CountedParentVerticalAccessCells.Num(),
		1);
	return true;
}

bool FLayoutZoneFeatureDemandExactTagMatchingTest::RunTest(const FString& Parameters)
{
	FGameplayTagContainer ProvidedFeatures;
	ProvidedFeatures.AddTag(LayoutGameplayTags::FeatureRoom);

	FLayoutZoneFeatureRequirement Requirement;
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureGate);
	Requirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
	TestTrue(
		TEXT("Any matching uses exact provided feature tags"),
		LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchRequirement(ProvidedFeatures, Requirement));

	Requirement.MatchMode = ELayoutZoneFeatureMatchMode::All;
	TestFalse(
		TEXT("All matching rejects a partial provided feature set"),
		LayoutZoneFeatureDemand::DoesProvidedFeatureSetMatchRequirement(ProvidedFeatures, Requirement));
	return true;
}

bool FLayoutZoneFeatureDemandHostIndependentCapacityFailureTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateZoneFeatureDemandTestOuter(TEXT("ZoneFeatureHostIndependentFailure"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("ZoneFeatureHostIndependentTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("ZoneFeatureHostIndependentModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ZoneFeatureHostIndependentContent"),
		{MakeZoneFeatureModuleEntry(TEXT("RoomProvider"), Module, true)});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ZoneFeatureHostIndependentProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	FLayoutZoneFeatureRequirement& Requirement =
		Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("Room");
	Requirement.Zone = ELayoutPlacementZone::Any;
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MinCount = 1;
	Requirement.MaxCount = 1;

	FLayoutRegionSolveRequest Request =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			920,
			TEXT("ZoneFeatureHostIndependentRoot"));
	// Simulate a frozen-request authority mismatch after valid asset validation.
	// The request boundary must identify this before host-specific preparation.
	for (FLayoutRegionContentEntrySolveSnapshot& Entry : Request.ContentSetSnapshot.Entries)
	{
		Entry.ProvidedZoneFeatures.Reset();
	}
	FLayoutVerticalAccessHostGroup& HostGroup =
		Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
	HostGroup.GroupId = TEXT("AuthoredVerticalAccess_0_0");
	HostGroup.DeckCell = FIntVector(0, 0, 1);
	HostGroup.FirstRejectedAdmission = TEXT("Unrelated rejected host detail");
	for (int32 X = 0; X < 2; ++X)
	{
		FLayoutVerticalAccessHostOption& Option = HostGroup.Options.AddDefaulted_GetRef();
		Option.LowerCell = FIntVector(X, 0, 0);
		Option.UpperCell = FIntVector(X, 0, 1);
	}

	const double MissingProviderStartSeconds = FPlatformTime::Seconds();
	const FLayoutSolveResult Result =
		FLayoutProfileSolver::SolveRegion(Request).SolveResult;
	const double MissingProviderWallMilliseconds =
		(FPlatformTime::Seconds() - MissingProviderStartSeconds) * 1000.0;
	TestFalse(TEXT("Zero provider capacity rejects frozen request"), Result.bSucceeded);
	TestTrue(
		TEXT("Feature capacity remains primary failure"),
		Result.FailureReason.StartsWith(TEXT("Hard zone-feature requirement 'Room'")));
	TestFalse(
		TEXT("Host-independent failure is not wrapped as VerticalAccess exhaustion"),
		Result.FailureReason.Contains(TEXT("VerticalAccess host alternatives exhausted")));
	TestFalse(
		TEXT("Unrelated rejected host detail is absent"),
		Result.FailureReason.Contains(TEXT("Unrelated rejected host detail")));
	TestEqual(
		TEXT("Host-independent feature capacity failure stays typed"),
		Result.PreparationFailureKind,
		ELayoutSolvePreparationFailureKind::ZoneFeatureProviderCapacityInfeasible);
	TestEqual(
		TEXT("Host-independent preparation consumes zero candidate attempts"),
		Result.PropagationStats.CandidateAttemptCount,
		0);

	FLayoutRegionSolveRequest ZoneMismatchRequest = Request;
	ZoneMismatchRequest.ProfileSnapshot.ZoneFeatureRequirements[0].Zone =
		ELayoutPlacementZone::Interior;
	for (FLayoutRegionContentEntrySolveSnapshot& Entry : ZoneMismatchRequest.ContentSetSnapshot.Entries)
	{
		Entry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
		Entry.ModulePlacementZone = ELayoutPlacementZone::Corner;
	}
	const FLayoutSolveResult ZoneMismatchResult =
		FLayoutProfileSolver::SolveRegion(ZoneMismatchRequest).SolveResult;
	TestFalse(TEXT("Incompatible provider zone rejects frozen request"), ZoneMismatchResult.bSucceeded);
	TestTrue(
		TEXT("Provider-zone failure names excluded entry and provided zone"),
		ZoneMismatchResult.FailureReason.Contains(TEXT("ZoneMismatchEntries=RoomProvider(Corner)")));
	TestEqual(
		TEXT("Provider-zone failure stays host-independent and typed"),
		ZoneMismatchResult.PreparationFailureKind,
		ELayoutSolvePreparationFailureKind::ZoneFeatureProviderCapacityInfeasible);
	TestEqual(
		TEXT("Provider-zone failure consumes zero candidate attempts"),
		ZoneMismatchResult.PropagationStats.CandidateAttemptCount,
		0);
	AddInfo(FString::Printf(
		TEXT("Host-independent feature failure perf: hostGroups=%d hostOptions=%d candidateAttempts=%d wallMs=%.3f."),
		Request.VerticalAccessHostGroups.Num(),
		HostGroup.Options.Num(),
		Result.PropagationStats.CandidateAttemptCount,
		MissingProviderWallMilliseconds));
	return true;
}

bool FLayoutZoneFeatureDemandCanonicalEligibilityTest::RunTest(const FString& Parameters)
{
	FLayoutPlannedCell ShiftedAuthoredGroundCell;
	ShiftedAuthoredGroundCell.Cell = FIntVector(0, 2, 3);
	ShiftedAuthoredGroundCell.ModuleLevelIndex = 0;
	ShiftedAuthoredGroundCell.PlacementZone = ELayoutPlacementZone::Edge;

	TestTrue(
		TEXT("Shifted authored-ground cell uses finalized Edge metadata"),
		LayoutZoneFeatureDemand::DoesFinalizedCellMatchRequirementZone(
			ShiftedAuthoredGroundCell,
			FIntPoint(5, 5),
			ELayoutPlacementZone::Edge));

	ShiftedAuthoredGroundCell.Cell = FIntVector(0, 0, 3);
	ShiftedAuthoredGroundCell.PlacementZone = ELayoutPlacementZone::Interior;
	TestTrue(
		TEXT("Shifted authored-ground cell uses ModuleLevelIndex for Core"),
		LayoutZoneFeatureDemand::DoesFinalizedCellMatchRequirementZone(
			ShiftedAuthoredGroundCell,
			FIntPoint(1, 1),
			ELayoutPlacementZone::Core));

	ShiftedAuthoredGroundCell.bIsBridgeCell = true;
	TestFalse(
		TEXT("Generated bridge cell cannot provide zone-feature credit"),
		LayoutZoneFeatureDemand::DoesFinalizedCellMatchRequirementZone(
			ShiftedAuthoredGroundCell,
			FIntPoint(5, 5),
			ELayoutPlacementZone::Edge));
	return true;
}

bool FLayoutZoneFeatureDemandPassiveSeamSuppressionRemovesModuleCreditTest::RunTest(const FString& Parameters)
{
	const FIntVector SuppressedCell(2, 1, 0);
	FLayoutZoneFeatureProviderCommitment PassiveModuleCommitment;
	PassiveModuleCommitment.ProviderCommitmentId = TEXT("PassiveModuleProvider");
	PassiveModuleCommitment.RequirementId = TEXT("RoomRequirement");
	PassiveModuleCommitment.Cell = SuppressedCell;
	PassiveModuleCommitment.ModuleSnapshotId = TEXT("PassiveModuleSnapshot");
	FLayoutZoneFeatureProviderCommitment DirectChildCommitment =
		PassiveModuleCommitment;
	DirectChildCommitment.ProviderCommitmentId = TEXT("DirectChildProvider");
	DirectChildCommitment.ModuleSnapshotId = NAME_None;
	FLayoutZoneFeatureProviderCommitment UnsuppressedModuleCommitment =
		PassiveModuleCommitment;
	UnsuppressedModuleCommitment.ProviderCommitmentId =
		TEXT("UnsuppressedModuleProvider");
	UnsuppressedModuleCommitment.Cell = FIntVector(3, 1, 0);

	TArray<FLayoutZoneFeatureProviderCommitment> Commitments = {
		PassiveModuleCommitment,
		DirectChildCommitment,
		UnsuppressedModuleCommitment};
	LayoutZoneFeatureDemand::RemoveSuppressedPassiveModuleCommitments(
		{SuppressedCell},
		Commitments);
	TestEqual(TEXT("Passive seam suppression removes one module provider"), Commitments.Num(), 2);
	TestFalse(TEXT("Suppressed passive module loses feature credit"), Commitments.ContainsByPredicate(
		[](const FLayoutZoneFeatureProviderCommitment& Commitment)
		{
			return Commitment.ProviderCommitmentId == TEXT("PassiveModuleProvider");
		}));
	TestTrue(TEXT("Direct child at same cell keeps independent feature credit"), Commitments.ContainsByPredicate(
		[](const FLayoutZoneFeatureProviderCommitment& Commitment)
		{
			return Commitment.ProviderCommitmentId == TEXT("DirectChildProvider");
		}));
	return true;
}

bool FLayoutZoneFeatureDemandCompilesRequirementsInStableOrderTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutZoneFeatureRequirement> Requirements;
	FLayoutZoneFeatureRequirement& RequirementB = Requirements.AddDefaulted_GetRef();
	RequirementB.RequirementId = TEXT("RequirementB");
	RequirementB.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	RequirementB.MinCount = 2;
	RequirementB.MaxCount = 3;

	FLayoutZoneFeatureRequirement& RequirementA = Requirements.AddDefaulted_GetRef();
	RequirementA.RequirementId = TEXT("RequirementA");
	RequirementA.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureGate);
	RequirementA.MinCount = 1;

	TArray<LayoutZoneFeatureDemand::FHardDemand> Demands;
	LayoutZoneFeatureDemand::CompileHardDemands(Requirements, Demands);
	if (!TestEqual(TEXT("Every valid requirement compiles"), Demands.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("Demand ids sort deterministically"), Demands[0].RequirementId, FName(TEXT("RequirementA")));
	TestEqual(TEXT("Count bounds remain pointer-free values"), Demands[1].MinCount, 2);
	TestEqual(TEXT("Maximum count remains exact"), Demands[1].MaxCount, 3);

	FLayoutRegionContentEntrySolveSnapshot ModuleProvider;
	ModuleProvider.EntryId = TEXT("ModuleRoom");
	ModuleProvider.ContentKind = ELayoutRegionContentKind::Module;
	ModuleProvider.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	ModuleProvider.ModulePlacementZone = ELayoutPlacementZone::Edge;
	FLayoutRegionContentEntrySolveSnapshot ChildProvider;
	ChildProvider.EntryId = TEXT("ChildRoom");
	ChildProvider.ContentKind = ELayoutRegionContentKind::ChildRegion;
	ChildProvider.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	ChildProvider.ChildPlacementZone = ELayoutPlacementZone::Interior;
	ChildProvider.ChildLevelPlacementPolicy = ELayoutLevelPlacementPolicy::SpecificLevel;
	ChildProvider.ChildSpecificLevel = 2;
	ChildProvider.bChildOptional = true;

	TArray<LayoutZoneFeatureDemand::FProviderChoiceSummary> ProviderChoices;
	LayoutZoneFeatureDemand::CompileProviderChoiceSummaries(
		TEXT("DemandSnapshot"),
		Demands,
		{ModuleProvider, ChildProvider},
		ProviderChoices);
	if (!TestEqual(TEXT("Module and direct child compile as interchangeable hard-provider choices"), ProviderChoices.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("Provider choices sort by stable source id"), ProviderChoices[0].SourceContentEntryId, FName(TEXT("ChildRoom")));
	TestEqual(TEXT("Direct-child choice keeps content kind"), ProviderChoices[0].ContentKind, ELayoutRegionContentKind::ChildRegion);
	TestEqual(TEXT("Direct-child choice keeps authored level"), ProviderChoices[0].SpecificLevel, 2);
	TestTrue(TEXT("Direct-child choice keeps authored optional policy without selecting it"), ProviderChoices[0].bOptional);
	TestFalse(TEXT("Provider summary id is stable and nonempty"), ProviderChoices[0].ProviderSummaryId.IsNone());
	return true;
}

bool FLayoutZoneFeatureDemandCompositeCountsOnceTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateZoneFeatureDemandTestOuter(TEXT("ZoneFeatureComposite"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("ZoneFeatureCompositeTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* Leaf = CreateModule(
		Outer,
		TEXT("ZoneFeatureCompositeLeaf"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
	Leaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	Leaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	Leaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	Leaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	Leaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
	Leaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

	ULayoutCompositeModuleAsset* Composite =
		NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("ZoneFeatureCompositeModule"));
	for (const FIntVector LocalCell : {FIntVector(0, 0, 0), FIntVector(1, 0, 0)})
	{
		FLayoutCompositeModuleCell& CompositeCell = Composite->Cells.AddDefaulted_GetRef();
		CompositeCell.Module = Leaf;
		CompositeCell.LocalCell = LocalCell;
	}

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("CompositeRoom");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = Composite;
	Entry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ZoneFeatureCompositeContent"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ZoneFeatureCompositeProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	FLayoutZoneFeatureRequirement& Requirement =
		Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("OneCompositeRoom");
	Requirement.Zone = ELayoutPlacementZone::Any;
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MinCount = 1;
	Requirement.MaxCount = 1;

	const FLayoutRegionSolveRequest Request =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			915,
			TEXT("ZoneFeatureCompositeRoot"));
	const FLayoutRegionSolveResult Result =
		FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Composite provider solve succeeds"), Result.SolveResult.bSucceeded))
	{
		AddError(Result.SolveResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Composite publishes one bundle-root placement"), Result.SolveResult.Placements.Num(), 1);
	TestEqual(TEXT("Two-cell composite contributes one provider count"), Result.SolveResult.ZoneFeatureProviderCommitments.Num(), 1);

	FLayoutRegionSolveRequest ShiftedRequest = Request;
	ShiftedRequest.RegionDebugPath = TEXT("ZoneFeatureShiftedCompositeRoot");
	ShiftedRequest.FootprintSize = FIntPoint(3, 1);
	ShiftedRequest.ProfileSnapshot.LevelCount = 2;
	ShiftedRequest.ProfileSnapshot.bSupportsSteppedTerrainSolve = true;
	ShiftedRequest.ProfileSnapshot.ZoneFeatureRequirements[0].Zone =
		ELayoutPlacementZone::Corner;
	for (FLayoutModuleSolveSnapshot& ModuleSnapshot : ShiftedRequest.ModuleCatalog.Modules)
	{
		ModuleSnapshot.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	}
	ShiftedRequest.bHasSelectedModePlan = true;
	ShiftedRequest.SelectedModePlan.EnvironmentMode =
		ELayoutContractEnvironmentMode::SteppedSurfacePlacement;
	ShiftedRequest.SelectedModePlan.bUsesSteppedTerrainTopology = true;
	ShiftedRequest.PrecomputedPlannedCells.Reset();
	ShiftedRequest.PrecomputedActiveCells.Reset();
	ShiftedRequest.PrecomputedFrozenTerrainContract = FLayoutFrozenTerrainContract();
	ShiftedRequest.SteppedTerrainSupportMap = FLayoutSteppedTerrainSupportMap();
	ShiftedRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks = 8;
	ShiftedRequest.SteppedTerrainSupportMap.MaximumObservedSnappedLevelDelta = 1;
	for (int32 X = 0; X < 3; ++X)
	{
		FLayoutSteppedTerrainSupportSample& Support =
			ShiftedRequest.SteppedTerrainSupportMap.SupportSamples.AddDefaulted_GetRef();
		Support.LocalCell = FIntVector(X, 0, 0);
		Support.SnappedSupportFloorZ = X == 0 ? 8 : 16;
		Support.SnappedSupportCeilingZ = Support.SnappedSupportFloorZ + 8;
		Support.SupportSurfaceZ = Support.SnappedSupportFloorZ;

		FLayoutFrozenTerrainStageCellRecord& StageRecord =
			ShiftedRequest.PrecomputedFrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		StageRecord.FootprintCellXY = FIntPoint(X, 0);
		StageRecord.TerrainStageIndex = X == 0 ? 0 : 1;
		StageRecord.VerticalShiftBlocks = X == 0 ? 0 : 8;
		StageRecord.ResolvedStageBaseBlockWorldZ = X == 0 ? 8 : 16;
	}
	ShiftedRequest.PrecomputedSteppedTerrainSupportMap =
		ShiftedRequest.SteppedTerrainSupportMap;
	for (int32 X = 1; X <= 2; ++X)
	{
		FLayoutPlannedCell& FinalizedCell =
			ShiftedRequest.PrecomputedPlannedCells.AddDefaulted_GetRef();
		FinalizedCell.Cell = FIntVector(X, 0, 1);
		FinalizedCell.ModuleLevelIndex = 0;
		FinalizedCell.Intent = ELayoutCellIntent::Boundary;

		FLayoutContractActiveCellRecord& ActiveCell =
			ShiftedRequest.PrecomputedActiveCells.AddDefaulted_GetRef();
		ActiveCell.Cell = FinalizedCell.Cell;
		FLayoutContractActiveCellRecord& FrozenActiveCell =
			ShiftedRequest.PrecomputedFrozenTerrainContract.ActiveCells.AddDefaulted_GetRef();
		FrozenActiveCell.Cell = FinalizedCell.Cell;
		FLayoutTerrainCellContractRecord& CellContract =
			ShiftedRequest.PrecomputedFrozenTerrainContract.CellContracts.AddDefaulted_GetRef();
		CellContract.Cell = FinalizedCell.Cell;
		CellContract.Contract = ELayoutFrozenTerrainCellContract::Active;
	}
	ShiftedRequest.bHasFinalizedSteppedTerrainIntents = true;

	const FLayoutRegionSolveResult ShiftedResult =
		FLayoutProfileSolver::SolveRegion(ShiftedRequest);
	if (!TestTrue(TEXT("Shifted Stepped composite provider solve succeeds"), ShiftedResult.SolveResult.bSucceeded))
	{
		AddError(ShiftedResult.SolveResult.FailureReason);
		return false;
	}
	if (!TestEqual(TEXT("Shifted Stepped composite still counts once"), ShiftedResult.SolveResult.ZoneFeatureProviderCommitments.Num(), 1))
	{
		return false;
	}
	const FLayoutZoneFeatureProviderCommitment& ShiftedCommitment =
		ShiftedResult.SolveResult.ZoneFeatureProviderCommitments[0];
	TestEqual(TEXT("Shifted commitment preserves physical level"), ShiftedCommitment.Cell.Z, 1);
	TestEqual(TEXT("Shifted commitment uses authored ModuleLevelIndex"), ShiftedCommitment.ModuleLevelIndex, 0);
	TestEqual(TEXT("Shifted commitment preserves frozen terrain stage"), ShiftedCommitment.TerrainStageIndex, 1);
	return true;
}

bool FLayoutZoneFeatureDemandRequiredChildPreparationFailureIsTypedTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateZoneFeatureDemandTestOuter(TEXT("ZoneFeatureTypedChildPreparation"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("TypedChildPreparationTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* ParentModule = CreateModule(
		Outer,
		TEXT("TypedChildPreparationParent"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	ULayoutModuleAsset* DisconnectedChildModule = CreateModule(
		Outer,
		TEXT("TypedChildPreparationDisconnectedChild"),
		Template,
		{ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));

	ULayoutModuleAsset* GenericChildModule = CreateModule(
		Outer,
		TEXT("TypedChildPreparationGenericChild"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry EntryModuleEntry =
		MakeZoneFeatureModuleEntry(
			TEXT("DisconnectedChildEntry"),
			DisconnectedChildModule,
			false);
	FLayoutRegionContentEntry GenericModuleEntry =
		MakeZoneFeatureModuleEntry(
			TEXT("DisconnectedChildShell"),
			GenericChildModule,
			false);
	ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TypedChildPreparationChildContent"),
		{EntryModuleEntry, GenericModuleEntry});
	ULayoutProfileAsset* ChildProfile = CreateProfile(
		Outer,
		TEXT("TypedChildPreparationChildProfile"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		2,
		false);
	ChildProfile->ContentSet = ChildContentSet;

	FLayoutRegionContentEntry ParentModuleEntry =
		MakeZoneFeatureModuleEntry(TEXT("TypedParentShell"), ParentModule, false);
	FLayoutRegionContentEntry RequiredChildEntry;
	RequiredChildEntry.EntryId = TEXT("RequiredDisconnectedChild");
	RequiredChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
	RequiredChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
	RequiredChildEntry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
		Outer,
		TEXT("TypedChildPreparationParentContent"),
		{ParentModuleEntry, RequiredChildEntry});
	ULayoutProfileAsset* ParentProfile = CreateProfile(
		Outer,
		TEXT("TypedChildPreparationParentProfile"),
		FIntPoint(5, 5),
		FIntPoint(5, 5),
		1,
		0,
		false);
	ParentProfile->ContentSet = ParentContentSet;
	FLayoutZoneFeatureRequirement& RequiredRoom =
		ParentProfile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	RequiredRoom.RequirementId = TEXT("RequiredRoom");
	RequiredRoom.Zone = ELayoutPlacementZone::Any;
	RequiredRoom.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	RequiredRoom.MinCount = 1;
	RequiredRoom.MaxCount = 1;

	const FLayoutRegionSolveScheduleResult Result =
		FLayoutProfileSolver::SolveRegionTree(
			FLayoutProfileSolver::BuildStandaloneRegionRequest(
				ParentContentSet,
				ParentProfile,
				918,
				TEXT("TypedChildPreparationRoot")));
	TestFalse(TEXT("Required child preparation rejection fails schedule"), Result.bSucceeded);
	TestEqual(TEXT("Required child preparation failure remains typed"),
		Result.MergedSolveResult.PreparationFailureKind,
		ELayoutSolvePreparationFailureKind::TraversalTopologyInfeasible);
	TestEqual(TEXT("Required preparation rejection performs zero candidate CSP attempts"),
		Result.MergedSolveResult.PropagationStats.CandidateAttemptCount,
		0);
	TestTrue(TEXT("Required preparation rejection never becomes optional drop"),
		Result.MergedSolveResult.DroppedOptionalChildren.IsEmpty());
	const FLayoutRegionalFailureRecord& RegionalFailure =
		Result.MergedSolveResult.RegionalFailure;
	TestTrue(TEXT("Required feature-child failure preserves structured first cause"),
		RegionalFailure.IsSet());
	TestEqual(TEXT("Required feature-child failure keeps child scope"),
		RegionalFailure.Scope,
		ELayoutRegionalFailureScope::Child);
	TestEqual(TEXT("Required feature-child failure keeps source entry"),
		RegionalFailure.SourceContentEntryId,
		FName(TEXT("RequiredDisconnectedChild")));
	TestTrue(TEXT("Required feature-child failure keeps child region identity"),
		RegionalFailure.RegionDebugPath.Contains(TEXT("RequiredDisconnectedChild")));
	TestEqual(TEXT("Required feature-child failure keeps preparation phase"),
		RegionalFailure.Phase,
		FName(TEXT("ChildPreparation")));
	TestTrue(TEXT("Required feature-child failure keeps exact preparation detail"),
		RegionalFailure.FirstCause.Contains(TEXT("RequiredDisconnectedChild")));
	TestFalse(TEXT("Later provider audit does not replace child first cause"),
		Result.FailureReason.Contains(TEXT("no statically compatible module")));
	return true;
}

bool FLayoutZoneFeatureDemandSharedSeamPerimeterChildPlacementTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> MappedChildCells;
	MappedChildCells.AddDefaulted_GetRef().Cell = FIntVector(1, 1, 0);
	MappedChildCells.AddDefaulted_GetRef().Cell = FIntVector(2, 1, 0);
	TestFalse(TEXT("Mapped child value cannot overlap planned parent Entry"),
		LayoutProfileSolverInternal::DoesMappedChildPlanAvoidParentExclusiveCellsForTests(
			MappedChildCells,
			{FIntVector(1, 1, 0)}));
	TestFalse(TEXT("Mapped child value cannot overlap planned parent reserved-open cell"),
		LayoutProfileSolverInternal::DoesMappedChildPlanAvoidParentExclusiveCellsForTests(
			MappedChildCells,
			{FIntVector(2, 1, 0)}));
	TestTrue(TEXT("Mapped child value remains legal when parent-owned exclusive cells stay outside it"),
		LayoutProfileSolverInternal::DoesMappedChildPlanAvoidParentExclusiveCellsForTests(
			MappedChildCells,
			{FIntVector(3, 1, 0)}));

	const auto SolveScenario = [this](
		const TCHAR* ScenarioName,
		const FIntPoint ParentFootprint,
		const int32 ChildCount,
		const int32 Seed)
	{
		UObject* Outer = CreateZoneFeatureDemandTestOuter(ScenarioName);
		ULayoutProfileAsset* ChildProfile = CreateProfileWithUniversalContentSet(
			Outer,
			TEXT("SharedSeamRoomProfile"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			1,
			0,
			false);
		ULayoutProfileAsset* ParentProfile = CreateProfileWithUniversalContentSet(
			Outer,
			TEXT("SharedSeamParentProfile"),
			ParentFootprint,
			ParentFootprint,
			1,
			0,
			false);

		FLayoutSeamProviderIntent ParentSeamIntent;
		ParentSeamIntent.SeamIntentId = TEXT("ParentSharedSolid");
		ParentSeamIntent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
		ParentSeamIntent.bCanOwnSeam = true;
		ParentSeamIntent.bCanAcceptSeam = true;
		ParentProfile->ContentSet->Entries[0].SeamProviderIntents = {ParentSeamIntent};
		FLayoutSeamProviderIntent ChildSeamIntent = ParentSeamIntent;
		ChildSeamIntent.SeamIntentId = TEXT("ChildSharedSolid");
		ChildProfile->ContentSet->Entries[0].SeamProviderIntents = {ChildSeamIntent};
		const auto AuthorSolidSeamModule = [](ULayoutProfileAsset* Profile)
		{
			ULayoutModuleAsset* Module = Profile->ContentSet->Entries[0].ModuleSettings.Module;
			check(Module != nullptr);
			Module->FaceRules = FLayoutModuleFaceRules();
			for (const FLayoutFaceRule& FaceRule : BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor))
			{
				Module->FaceRules.SetRule(FaceRule);
			}
		};
		AuthorSolidSeamModule(ParentProfile);
		AuthorSolidSeamModule(ChildProfile);

		for (int32 ChildIndex = 0; ChildIndex < ChildCount; ++ChildIndex)
		{
			FLayoutRegionContentEntry ChildEntry;
			ChildEntry.EntryId = FName(*FString::Printf(TEXT("RoomChild%d"), ChildIndex));
			ChildEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
			ChildEntry.ChildRegionSettings.RegionProfile = ChildProfile;
			ChildEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;
			ChildEntry.ChildRegionSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
			ChildEntry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
			ParentProfile->ContentSet->Entries.Add(MoveTemp(ChildEntry));
		}

		FLayoutZoneFeatureRequirement& Requirement =
			ParentProfile->ZoneFeatureRequirements.AddDefaulted_GetRef();
		Requirement.RequirementId = TEXT("Rooms");
		Requirement.Zone = ELayoutPlacementZone::Any;
		Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
		Requirement.MinCount = ChildCount;
		Requirement.MaxCount = ChildCount;

		FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ParentProfile->ContentSet,
			ParentProfile,
			Seed,
			ScenarioName);
		Request.FootprintSize = ParentFootprint;
		Request.PlannedCells.Reset();
		for (int32 Y = 0; Y < ParentFootprint.Y; ++Y)
		{
			for (int32 X = 0; X < ParentFootprint.X; ++X)
			{
				FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, 0);
				Cell.ModuleLevelIndex = 0;
				const bool bBoundary = X == 0 || Y == 0
					|| X == ParentFootprint.X - 1
					|| Y == ParentFootprint.Y - 1;
				Cell.Intent = bBoundary
					? ELayoutCellIntent::Boundary
					: ELayoutCellIntent::Interior;
				Cell.PlacementZone = bBoundary
					? ELayoutPlacementZone::Perimeter
					: ELayoutPlacementZone::Interior;
			}
		}
		Request.PrecomputedPlannedCells = Request.PlannedCells;
		for (const FLayoutRegionContentEntrySolveSnapshot& EntrySnapshot : Request.ContentSetSnapshot.Entries)
		{
			if (EntrySnapshot.ContentKind == ELayoutRegionContentKind::ChildRegion)
			{
				TestEqual(
					TEXT("Frozen child keeps Interior placement policy"),
					EntrySnapshot.ChildPlacementZone,
					ELayoutPlacementZone::Interior);
			}
		}
		Request.ExecutionSettings.MaxCandidateAttempts = 100000;
		Request.ExecutionSettings.MaxSolveDurationSeconds = 10.0f;
		return FLayoutProfileSolver::SolveRegionTree(Request);
	};

	const FLayoutRegionSolveScheduleResult OneChildResult = SolveScenario(
		TEXT("SharedSeam4x5OneRoom"),
		FIntPoint(4, 5),
		1,
		657820862);
	if (!TestTrue(TEXT("4x5 parent accepts one 3x3 perimeter-sharing Interior child"), OneChildResult.bSucceeded))
	{
		AddError(OneChildResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("4x5 solve contains parent and one child"), OneChildResult.RegionResults.Num(), 2);
	TestTrue(TEXT("4x5 solve commits exact parent-child seam"),
		OneChildResult.MergedSolveResult.PartitionSeams.ContainsByPredicate(
			[](const FLayoutPartitionSeamRecord& Seam)
			{
				return Seam.InterfaceFamily == LayoutGameplayTags::InterfacePartitionSolid
					&& (Seam.OwnerRegionDebugPath == TEXT("SharedSeam4x5OneRoom")
						|| Seam.PassiveRegionDebugPath == TEXT("SharedSeam4x5OneRoom"));
			}));

	const FLayoutRegionSolveScheduleResult TwoChildResult = SolveScenario(
		TEXT("SharedSeam7x7TwoRooms"),
		FIntPoint(7, 7),
		2,
		2118245868);
	if (!TestTrue(TEXT("7x7 parent accepts two 3x3 perimeter-sharing Interior children"), TwoChildResult.bSucceeded))
	{
		AddError(TwoChildResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("7x7 solve contains parent and two children"), TwoChildResult.RegionResults.Num(), 3);
	int32 ParentChildSeamCount = 0;
	for (const FLayoutPartitionSeamRecord& Seam : TwoChildResult.MergedSolveResult.PartitionSeams)
	{
		ParentChildSeamCount += Seam.InterfaceFamily == LayoutGameplayTags::InterfacePartitionSolid
			&& (Seam.OwnerRegionDebugPath == TEXT("SharedSeam7x7TwoRooms")
				|| Seam.PassiveRegionDebugPath == TEXT("SharedSeam7x7TwoRooms"));
	}
	TestTrue(TEXT("7x7 solve commits exact parent-child seams for both children"), ParentChildSeamCount >= 2);
	return true;
}

bool FLayoutZoneFeatureDemandThreeLevelDirectChildContractTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateZoneFeatureDemandTestOuter(TEXT("ZoneFeatureThreeLevelChild"));
	ULayoutProfileAsset* ChildProfile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ZoneFeatureThreeLevelChildProfile"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	ULayoutProfileAsset* ParentProfile = CreateProfileWithUniversalContentSet(
		Outer,
		TEXT("ZoneFeatureThreeLevelParentProfile"),
		FIntPoint(6, 4),
		FIntPoint(6, 4),
		3,
		0,
		false);

	FLayoutSeamProviderIntent SeamIntent;
	SeamIntent.SeamIntentId = TEXT("SharedWall");
	SeamIntent.InterfaceFamily = LayoutGameplayTags::InterfacePartitionSolid;
	SeamIntent.bCanOwnSeam = true;
	SeamIntent.bCanAcceptSeam = true;
	ChildProfile->ContentSet->Entries[0].SeamProviderIntents = {SeamIntent};
	ParentProfile->ContentSet->Entries[0].SeamProviderIntents = {SeamIntent};

	FLayoutRegionContentEntry RoomChild;
	RoomChild.EntryId = TEXT("RoomChild");
	RoomChild.ContentKind = ELayoutRegionContentKind::ChildRegion;
	RoomChild.ChildRegionSettings.RegionProfile = ChildProfile;
	RoomChild.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Corner;
	RoomChild.ChildRegionSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	RoomChild.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	ParentProfile->ContentSet->Entries.Add(RoomChild);

	FLayoutRegionContentEntry WallChild;
	WallChild.EntryId = TEXT("WallChild");
	WallChild.ContentKind = ELayoutRegionContentKind::ChildRegion;
	WallChild.ChildRegionSettings.RegionProfile = ChildProfile;
	WallChild.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Corner;
	WallChild.ChildRegionSettings.LevelPlacementPolicy = ELayoutLevelPlacementPolicy::GroundOnly;
	ParentProfile->ContentSet->Entries.Add(WallChild);
	const FLayoutValidationResult ChildProfileValidation = ChildProfile->ValidateProfile();
	if (!TestTrue(TEXT("Three-level child profile validates"), ChildProfileValidation.IsValid()))
	{
		for (const FLayoutValidationMessage& Message : ChildProfileValidation.Messages)
		{
			AddError(Message.Message);
		}
		return false;
	}

	FLayoutZoneFeatureRequirement& Requirement =
		ParentProfile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("Room");
	Requirement.Zone = ELayoutPlacementZone::Any;
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MinCount = 1;
	Requirement.MaxCount = 1;

	FLayoutRegionSolveRequest Request = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		ParentProfile->ContentSet,
		ParentProfile,
		921,
		TEXT("ZoneFeatureThreeLevelRoot"));
	if (!TestEqual(TEXT("Three-level frozen parent snapshot includes room and wall children"), Request.ContentSetSnapshot.Entries.Num(), 5))
	{
		for (const FLayoutValidationMessage& Message : Request.ContentSetSnapshot.Validation.Messages)
		{
			AddError(Message.Message);
		}
		return false;
	}
	Request.FootprintSize = FIntPoint(6, 4);
	for (int32 Z = 0; Z < 3; ++Z)
	{
		for (int32 Y = 0; Y < 4; ++Y)
		{
			for (int32 X = 0; X < 6; ++X)
			{
				const bool bReservedChildExclusionCell =
					Z == 0
					&& ((X == 0 && (Y == 0 || Y == 2))
						|| (X == 2 && Y == 2));
				if (bReservedChildExclusionCell)
				{
					continue;
				}
				FLayoutPlannedCell& Cell = Request.PlannedCells.AddDefaulted_GetRef();
				Cell.Cell = FIntVector(X, Y, Z);
				Cell.ModuleLevelIndex = Z;
				const bool bBoundary = X == 0 || X == 5 || Y == 0 || Y == 3;
				Cell.Intent = bBoundary ? ELayoutCellIntent::Boundary : ELayoutCellIntent::Interior;
				Cell.PlacementZone = bBoundary
					? ELayoutPlacementZone::Perimeter
					: ELayoutPlacementZone::Interior;
			}
		}
	}
	Request.PrecomputedPlannedCells = Request.PlannedCells;
	for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
	{
		FLayoutVerticalAccessHostGroup& Group = Request.VerticalAccessHostGroups.AddDefaulted_GetRef();
		Group.GroupId = FLayoutId(*FString::Printf(TEXT("ThreeLevelHost%d"), GroupIndex));
		const FIntVector LowerCell = GroupIndex == 0
			? FIntVector(2, 1, 0)
			: FIntVector(2, 2, 1);
		Group.DeckCell = LowerCell + FIntVector(0, 0, 1);
		FLayoutVerticalAccessHostOption& Option = Group.Options.AddDefaulted_GetRef();
		Option.LowerCell = LowerCell;
		Option.UpperCell = Group.DeckCell;
	}

	const FLayoutRegionSolveScheduleResult FirstResult =
		FLayoutProfileSolver::SolveRegionTree(Request);
	if (!TestTrue(TEXT("Three-level direct-child contract solves"), FirstResult.bSucceeded))
	{
		AddError(FirstResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Three-level contract selects parent plus room and wall children"), FirstResult.RegionResults.Num(), 3);
	TestTrue(TEXT("Every child proof returns its pre-parent candidate-domain certificate"),
		FirstResult.RegionResults.ContainsByPredicate(
		([](const FLayoutRegionSolveResult& Result)
		{
			return Result.RegionDebugPath.EndsWith(TEXT("/RoomChild"))
				&& !Result.SolveResult.CandidateDomainCertificateId.IsNone()
				&& !Result.SolveResult.CandidateDomainRestrictionIds.IsEmpty();
		})));
	TestTrue(TEXT("Parent residual proof returns its combined child-boundary certificate"),
		FirstResult.RegionResults.ContainsByPredicate(
		([](const FLayoutRegionSolveResult& Result)
		{
			return Result.RegionDebugPath == TEXT("ZoneFeatureThreeLevelRoot")
				&& !Result.SolveResult.CandidateDomainCertificateId.IsNone();
		})));
	TestEqual(TEXT("Three-level contract freezes one stage mapping per child"), FirstResult.ChildStageMappings.Num(), 2);
	TestEqual(TEXT("Merged solve publishes both stage mapping ids"), FirstResult.MergedSolveResult.ChildStageMappingIds.Num(), 2);
	TestTrue(TEXT("Every selected child stage mapping is complete"), FirstResult.ChildStageMappings.ContainsByPredicate(
		[](const FLayoutChildStageMappingResult& Mapping)
		{
			return Mapping.IsValid();
		}));
	if (!TestEqual(TEXT("Three-level contract commits exactly one room provider"), FirstResult.MergedSolveResult.ZoneFeatureProviderCommitments.Num(), 1))
	{
		return false;
	}
	TestEqual(
		TEXT("Three-level provider comes from parent-facing room child entry"),
		FirstResult.MergedSolveResult.ZoneFeatureProviderCommitments[0].SourceContentEntryId,
		FName(TEXT("RoomChild")));
	TestTrue(TEXT("Three-level contract reaches merged post-structural placement output"), !FirstResult.MergedSolveResult.Placements.IsEmpty());
	const FLayoutPartitionSeamRecord* RoomWallSeam =
		FirstResult.MergedSolveResult.PartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& Seam)
			{
				const bool bRoomOwnsWall =
					Seam.OwnerRegionDebugPath.EndsWith(TEXT("/RoomChild"))
					&& Seam.PassiveRegionDebugPath.EndsWith(TEXT("/WallChild"));
				const bool bWallOwnsRoom =
					Seam.OwnerRegionDebugPath.EndsWith(TEXT("/WallChild"))
					&& Seam.PassiveRegionDebugPath.EndsWith(TEXT("/RoomChild"));
				return bRoomOwnsWall || bWallOwnsRoom;
			});
	if (!TestNotNull(
		TEXT("Three-level contract commits an exact sibling-child seam before post-structural merge"),
		RoomWallSeam))
	{
		AddError(FString::Printf(
			TEXT("Selected regions: %s. Committed seams: %s."),
			*FString::JoinBy(
				FirstResult.RegionResults,
				TEXT(" | "),
				[](const FLayoutRegionSolveResult& Region)
				{
					return FString::Printf(
						TEXT("%s@%s"),
						*Region.RegionDebugPath,
						*Region.RegionCellOffset.ToString());
				}),
			*FString::JoinBy(
				FirstResult.MergedSolveResult.PartitionSeams,
				TEXT(" | "),
				[](const FLayoutPartitionSeamRecord& Seam)
				{
					return FString::Printf(
						TEXT("%s:%s>%s"),
						*Seam.SeamId.ToString(),
						*Seam.OwnerRegionDebugPath,
						*Seam.PassiveRegionDebugPath);
				})));
		return false;
	}
	TestFalse(TEXT("Committed sibling seam has stable id"), RoomWallSeam->SeamId.IsNone());
	const FLayoutPartitionSeamRecord* ExactParentChildSeam =
		FirstResult.MergedSolveResult.PartitionSeams.FindByPredicate(
			[](const FLayoutPartitionSeamRecord& Seam)
			{
				const bool bParentOwnsChild =
					Seam.OwnerRegionDebugPath == TEXT("ZoneFeatureThreeLevelRoot")
					&& (Seam.PassiveRegionDebugPath.EndsWith(TEXT("/RoomChild"))
						|| Seam.PassiveRegionDebugPath.EndsWith(TEXT("/WallChild")));
				const bool bChildOwnsParent =
					Seam.PassiveRegionDebugPath == TEXT("ZoneFeatureThreeLevelRoot")
					&& (Seam.OwnerRegionDebugPath.EndsWith(TEXT("/RoomChild"))
						|| Seam.OwnerRegionDebugPath.EndsWith(TEXT("/WallChild")));
				return Seam.InterfaceFamily == LayoutGameplayTags::InterfacePartitionSolid
					&& (bParentOwnsChild || bChildOwnsParent);
			});
	TestNotNull(
		TEXT("Joint boundary search selects an exact parent-child seam pair before proof"),
		ExactParentChildSeam);
	TArray<FLayoutId> FirstSeamIds;
	for (const FLayoutPartitionSeamRecord& Seam : FirstResult.MergedSolveResult.PartitionSeams)
	{
		FirstSeamIds.Add(Seam.SeamId);
	}
	FirstSeamIds.Sort();
	FLayoutSolvedArtifact ScheduleArtifact;
	FString ArtifactFailureReason;
	if (!TestTrue(
		TEXT("Three-level accepted schedule builds artifact"),
		LayoutSolvedArtifact::TryBuildFromScheduleResult(
			TEXT("ThreeLevelDirectChildArtifact"),
			TEXT("ZoneFeatureThreeLevelRoot"),
			FirstResult,
			ScheduleArtifact,
			ArtifactFailureReason)))
	{
		AddError(ArtifactFailureReason);
		return false;
	}
	TestTrue(TEXT("Accepted artifact marks seam/provider authority"), ScheduleArtifact.bStructuralCommitmentsAuthoritative);
	TestTrue(TEXT("Accepted artifact preserves parent and child candidate-domain certificates"), ScheduleArtifact.CandidateDomainCertificateIds.Num() >= 2);
	TestEqual(TEXT("Accepted artifact preserves every child stage mapping"), ScheduleArtifact.ChildStageMappings.Num(), FirstResult.ChildStageMappings.Num());
	TestTrue(TEXT("Accepted artifact preserves room provider id"), ScheduleArtifact.ZoneFeatureProviderCommitments.ContainsByPredicate(
		[&FirstResult](const FLayoutZoneFeatureProviderCommitment& Commitment)
		{
			return Commitment.ProviderCommitmentId == FirstResult.MergedSolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId;
		}));
	TArray<FLayoutId> ArtifactSeamIds;
	for (const FLayoutPartitionSeamRecord& Seam : ScheduleArtifact.PartitionSeams)
	{
		ArtifactSeamIds.Add(Seam.SeamId);
	}
	ArtifactSeamIds.Sort();
	TestEqual(TEXT("Accepted artifact preserves exact committed seam ids"), ArtifactSeamIds, FirstSeamIds);
	FLayoutPlannedSiteAcceptedSolvePayload AcceptedPayload;
	AcceptedPayload.SolveResult = FirstResult.MergedSolveResult;
	TestEqual(TEXT("Accepted payload preserves every child stage mapping id"), AcceptedPayload.SolveResult.ChildStageMappingIds, FirstResult.MergedSolveResult.ChildStageMappingIds);
	TestTrue(TEXT("Accepted payload preserves room provider id"), AcceptedPayload.SolveResult.ZoneFeatureProviderCommitments.ContainsByPredicate(
		[&FirstResult](const FLayoutZoneFeatureProviderCommitment& Commitment)
		{
			return Commitment.ProviderCommitmentId == FirstResult.MergedSolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId;
		}));
	TArray<FLayoutId> AcceptedPayloadSeamIds;
	for (const FLayoutPartitionSeamRecord& Seam : AcceptedPayload.SolveResult.PartitionSeams)
	{
		AcceptedPayloadSeamIds.Add(Seam.SeamId);
	}
	AcceptedPayloadSeamIds.Sort();
	TestEqual(TEXT("Accepted payload preserves exact committed seam ids"), AcceptedPayloadSeamIds, FirstSeamIds);
	TestTrue(TEXT("Three-level contract consumes candidate work"), FirstResult.MergedSolveResult.PropagationStats.CandidateAttemptCount > 0);
	AddInfo(FString::Printf(
		TEXT("Three-level direct-child perf: hostGroups=%d hostOptions=%d candidateAttempts=%d seams=%d."),
		Request.VerticalAccessHostGroups.Num(),
		Request.VerticalAccessHostGroups[0].Options.Num() + Request.VerticalAccessHostGroups[1].Options.Num(),
		FirstResult.MergedSolveResult.PropagationStats.CandidateAttemptCount,
		FirstResult.MergedSolveResult.PartitionSeams.Num()));

	const TSharedRef<bool, ESPMode::ThreadSafe> bRepeatedSolveRanOffGameThread =
		MakeShared<bool, ESPMode::ThreadSafe>(false);
	TFuture<FLayoutRegionSolveScheduleResult> RepeatedWorkerSolve = Async(
		EAsyncExecution::ThreadPool,
		[WorkerRequest = Request, bRepeatedSolveRanOffGameThread]() mutable
		{
			*bRepeatedSolveRanOffGameThread = !IsInGameThread();
			return FLayoutProfileSolver::SolveRegionTree(WorkerRequest);
		});
	const FLayoutRegionSolveScheduleResult SecondResult = RepeatedWorkerSolve.Get();
	TestTrue(
		TEXT("Repeated three-level contract executes on a worker thread"),
		*bRepeatedSolveRanOffGameThread);
	if (!TestTrue(TEXT("Repeated three-level contract solves"), SecondResult.bSucceeded)
		|| !TestEqual(TEXT("Repeated three-level contract keeps one provider"), SecondResult.MergedSolveResult.ZoneFeatureProviderCommitments.Num(), 1))
	{
		return false;
	}
	TestEqual(
		TEXT("Three-level provider id is deterministic"),
		SecondResult.MergedSolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId,
		FirstResult.MergedSolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId);
	TestEqual(
		TEXT("Three-level stage mapping ids are deterministic"),
		SecondResult.MergedSolveResult.ChildStageMappingIds,
		FirstResult.MergedSolveResult.ChildStageMappingIds);
	TArray<FLayoutId> SecondSeamIds;
	for (const FLayoutPartitionSeamRecord& Seam : SecondResult.MergedSolveResult.PartitionSeams)
	{
		SecondSeamIds.Add(Seam.SeamId);
	}
	SecondSeamIds.Sort();
	TestEqual(TEXT("Three-level seam ids are deterministic"), SecondSeamIds, FirstSeamIds);
	return true;
}

bool FLayoutZoneFeatureDemandConstructiveLeafSolveTest::RunTest(const FString& Parameters)
{
	UObject* Outer = CreateZoneFeatureDemandTestOuter(TEXT("ZoneFeatureConstructiveLeaf"));
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("ZoneFeatureTemplate"),
		FIntVector(8, 8, 8));
	const TArray<FLayoutFaceRule> FaceRules = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor);
	ULayoutModuleAsset* NonProviderModule = CreateModule(
		Outer,
		TEXT("NonProviderModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
		FaceRules);
	ULayoutModuleAsset* ProviderModule = CreateModule(
		Outer,
		TEXT("ProviderModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
		FaceRules);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("ZoneFeatureContentSet"),
		{
			MakeZoneFeatureModuleEntry(TEXT("NonProvider"), NonProviderModule, false),
			MakeZoneFeatureModuleEntry(TEXT("RoomProvider"), ProviderModule, true)
		});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("ZoneFeatureProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	const FLayoutRegionSolveResult ControlResult =
		FLayoutProfileSolver::SolveRegion(
			FLayoutProfileSolver::BuildStandaloneRegionRequest(
				ContentSet,
				Profile,
				913,
				TEXT("ZoneFeatureControlRoot")));
	if (!TestTrue(TEXT("No-feature control solve succeeds"), ControlResult.SolveResult.bSucceeded))
	{
		AddError(ControlResult.SolveResult.FailureReason);
		return false;
	}

	FLayoutZoneFeatureRequirement& Requirement =
		Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
	Requirement.RequirementId = TEXT("ExactlyOneRoom");
	Requirement.Zone = ELayoutPlacementZone::Any;
	Requirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
	Requirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
	Requirement.MinCount = 1;
	Requirement.MaxCount = 1;

	const FLayoutRegionSolveRequest Request =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			913,
			TEXT("ZoneFeatureRoot"));
	const FLayoutRegionSolveResult FirstResult =
		FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Counted-demand solve succeeds"), FirstResult.SolveResult.bSucceeded))
	{
		AddError(FirstResult.SolveResult.FailureReason);
		return false;
	}

	if (!TestEqual(TEXT("Exactly one provider commitment survives search"), FirstResult.SolveResult.ZoneFeatureProviderCommitments.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Provider commitment preserves authored module level"), FirstResult.SolveResult.ZoneFeatureProviderCommitments[0].ModuleLevelIndex, 0);
	TestEqual(TEXT("Flat provider commitment preserves absent terrain stage"), FirstResult.SolveResult.ZoneFeatureProviderCommitments[0].TerrainStageIndex, INDEX_NONE);
	TestEqual(TEXT("One hard demand compiles once"), FirstResult.SolveResult.PropagationStats.HardZoneFeatureDemandCount, 1);
	TestEqual(TEXT("Two physical roots index provider capacity"), FirstResult.SolveResult.PropagationStats.HardZoneFeatureProviderRootCount, 2);
	int32 ProviderPlacementCount = 0;
	for (const FLayoutPlacedModule& Placement : FirstResult.SolveResult.Placements)
	{
		ProviderPlacementCount += Placement.SourceContentEntryId == TEXT("RoomProvider");
	}
	TestEqual(TEXT("Exactly one provider module is placed"), ProviderPlacementCount, 1);
	TestTrue(TEXT("Maximum-bound pruning occurs before final audit"), FirstResult.SolveResult.PropagationStats.HardZoneFeatureCountBoundPruneCount > 0);
	TestEqual(TEXT("Covered hard solve has no late hard-feature rejection"), FirstResult.SolveResult.PropagationStats.HardZoneFeatureLateAuditFailureCount, 0);
	TestTrue(
		TEXT("Counted-demand search stays within bounded control overhead"),
		FirstResult.SolveResult.PropagationStats.CandidateAttemptCount
			<= ControlResult.SolveResult.PropagationStats.CandidateAttemptCount + 8);
	AddInfo(FString::Printf(
		TEXT("Zone-feature perf comparison: controlAttempts=%d countedAttempts=%d countBoundPrunes=%d."),
		ControlResult.SolveResult.PropagationStats.CandidateAttemptCount,
		FirstResult.SolveResult.PropagationStats.CandidateAttemptCount,
		FirstResult.SolveResult.PropagationStats.HardZoneFeatureCountBoundPruneCount));

	FLayoutSolvedArtifact Artifact;
	FString ArtifactFailureReason;
	if (!TestTrue(
			TEXT("Solved artifact accepts counted-provider result"),
			LayoutSolvedArtifact::TryBuildFromSolveResult(
				TEXT("ZoneFeatureArtifact"),
				FirstResult,
				Artifact,
				ArtifactFailureReason)))
	{
		AddError(ArtifactFailureReason);
		return false;
	}
	if (!TestEqual(TEXT("Solved artifact preserves exact provider commitment"), Artifact.ZoneFeatureProviderCommitments.Num(), 1))
	{
		return false;
	}
	TestEqual(
		TEXT("Artifact commitment id matches final-audit carrier"),
		Artifact.ZoneFeatureProviderCommitments[0].ProviderCommitmentId,
		FirstResult.SolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId);

	const FLayoutRegionSolveScheduleResult ScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(Request);
	if (!TestTrue(TEXT("Recursive root-only schedule preserves counted solve"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("Merged solve preserves exact provider commitment"), ScheduleResult.MergedSolveResult.ZoneFeatureProviderCommitments.Num(), 1);
	FLayoutSolvedArtifact ScheduleArtifact;
	if (!TestTrue(
			TEXT("Schedule artifact accepts merged counted-provider result"),
			LayoutSolvedArtifact::TryBuildFromScheduleResult(
				TEXT("ZoneFeatureScheduleArtifact"),
				TEXT("ZoneFeatureRoot"),
				ScheduleResult,
				ScheduleArtifact,
				ArtifactFailureReason)))
	{
		AddError(ArtifactFailureReason);
		return false;
	}
	TestEqual(TEXT("Schedule artifact preserves merged provider commitment"), ScheduleArtifact.ZoneFeatureProviderCommitments.Num(), 1);

	const FLayoutRegionSolveResult SecondResult =
		FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Repeated counted-demand solve succeeds"), SecondResult.SolveResult.bSucceeded)
		|| !TestEqual(TEXT("Repeated solve preserves one commitment"), SecondResult.SolveResult.ZoneFeatureProviderCommitments.Num(), 1))
	{
		return false;
	}
	TestEqual(
		TEXT("Same seed reproduces provider commitment id"),
		SecondResult.SolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId,
		FirstResult.SolveResult.ZoneFeatureProviderCommitments[0].ProviderCommitmentId);

	Requirement.RequirementId = TEXT("TwoRooms");
	Requirement.MinCount = 2;
	Requirement.MaxCount = 0;
	const FLayoutRegionSolveRequest TwoProviderRequest =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(
			ContentSet,
			Profile,
			914,
			TEXT("ZoneFeatureTwoProviderRoot"));
	const FLayoutRegionSolveResult TwoProviderResult =
		FLayoutProfileSolver::SolveRegion(TwoProviderRequest);
	if (!TestTrue(TEXT("MinCount two solve succeeds"), TwoProviderResult.SolveResult.bSucceeded))
	{
		AddError(TwoProviderResult.SolveResult.FailureReason);
		return false;
	}
	TestEqual(TEXT("MinCount two commits two provider instances"), TwoProviderResult.SolveResult.ZoneFeatureProviderCommitments.Num(), 2);
	return true;
}
