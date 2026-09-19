// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"

#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutRegionRequestSnapshotBuilder.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Math/RandomStream.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;

namespace
{
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");

	UWorldGenDef* CreateWorldGenDef(UObject* const Outer)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(Outer);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		return WorldGenDef;
	}

	FLayoutNoiseCoordinateSettings MakeTestCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}

	TArray<FLayoutRegionContentEntry> BuildSteppedPlanningWindowContentEntries(
		UObject* const Outer,
		const FString& NamePrefix)
	{
		UChunkStructureTemplate* const Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%sTemplate"), *NamePrefix),
			FIntVector(16, 16, 16));
		ULayoutModuleAsset* const Module = CreateModule(
			Outer,
			*FString::Printf(TEXT("%sModule"), *NamePrefix),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Interior, ELayoutCellIntent::VerticalAccess},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				MakeTags({LayoutGameplayTags::TraversalPrimary})));

		FLayoutRegionContentEntry ContentEntry;
		ContentEntry.EntryId = FName(*FString::Printf(TEXT("%sEntry"), *NamePrefix));
		ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
		ContentEntry.ModuleSettings.Module = Module;
		return {ContentEntry};
	}

	bool ExpectEquivalentSteppedSupportRequest(
		FAutomationTestBase& Test,
		const FLayoutRegionSolveRequest& ExpectedRequest,
		const FLayoutRegionSolveRequest& ActualRequest,
		const TCHAR* const Context)
	{
		bool bPassed = true;

		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same debug path"), Context), ActualRequest.RegionDebugPath, ExpectedRequest.RegionDebugPath);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same seed"), Context), ActualRequest.Seed, ExpectedRequest.Seed);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same placement policy id"), Context), ActualRequest.RootPlacementPolicyId, ExpectedRequest.RootPlacementPolicyId);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same candidate id"), Context), ActualRequest.RootCandidateId, ExpectedRequest.RootCandidateId);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same solve id"), Context), ActualRequest.RootSolveId, ExpectedRequest.RootSolveId);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same placement kind"), Context), ActualRequest.RootPlacementKind, ExpectedRequest.RootPlacementKind);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same supplied-planned-cells flag"), Context), !ActualRequest.PlannedCells.IsEmpty(), !ExpectedRequest.PlannedCells.IsEmpty());
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same footprint size"), Context), ActualRequest.FootprintSize, ExpectedRequest.FootprintSize);
		bPassed &= Test.TestEqual(FString::Printf(TEXT("%s keeps the same planned-cell count"), Context), ActualRequest.PlannedCells.Num(), ExpectedRequest.PlannedCells.Num());
		for (int32 PlannedCellIndex = 0; PlannedCellIndex < FMath::Min(ActualRequest.PlannedCells.Num(), ExpectedRequest.PlannedCells.Num()); ++PlannedCellIndex)
		{
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps planned cell %d coordinates"), Context, PlannedCellIndex),
				ActualRequest.PlannedCells[PlannedCellIndex].Cell,
				ExpectedRequest.PlannedCells[PlannedCellIndex].Cell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps planned cell %d intent"), Context, PlannedCellIndex),
				ActualRequest.PlannedCells[PlannedCellIndex].Intent,
				ExpectedRequest.PlannedCells[PlannedCellIndex].Intent);
		}

		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped shared cell height"), Context),
			ActualRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks,
			ExpectedRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks);
		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped maximum neighbor delta"), Context),
			ActualRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta,
			ExpectedRequest.SteppedTerrainSupportMap.MaximumObservedNeighborHeightDelta);
		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped support-sample count"), Context),
			ActualRequest.SteppedTerrainSupportMap.SupportSamples.Num(),
			ExpectedRequest.SteppedTerrainSupportMap.SupportSamples.Num());
		for (int32 SampleIndex = 0; SampleIndex < FMath::Min(
			ActualRequest.SteppedTerrainSupportMap.SupportSamples.Num(),
			ExpectedRequest.SteppedTerrainSupportMap.SupportSamples.Num()); ++SampleIndex)
		{
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps support sample %d local cell"), Context, SampleIndex),
				ActualRequest.SteppedTerrainSupportMap.SupportSamples[SampleIndex].LocalCell,
				ExpectedRequest.SteppedTerrainSupportMap.SupportSamples[SampleIndex].LocalCell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps support sample %d surface Z"), Context, SampleIndex),
				ActualRequest.SteppedTerrainSupportMap.SupportSamples[SampleIndex].SupportSurfaceZ,
				ExpectedRequest.SteppedTerrainSupportMap.SupportSamples[SampleIndex].SupportSurfaceZ);
		}

		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped adjacency-step count"), Context),
			ActualRequest.SteppedTerrainSupportMap.AdjacencySteps.Num(),
			ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps.Num());
		for (int32 StepIndex = 0; StepIndex < FMath::Min(
			ActualRequest.SteppedTerrainSupportMap.AdjacencySteps.Num(),
			ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps.Num()); ++StepIndex)
		{
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps adjacency step %d from-cell"), Context, StepIndex),
				ActualRequest.SteppedTerrainSupportMap.AdjacencySteps[StepIndex].FromCell,
				ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps[StepIndex].FromCell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps adjacency step %d to-cell"), Context, StepIndex),
				ActualRequest.SteppedTerrainSupportMap.AdjacencySteps[StepIndex].ToCell,
				ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps[StepIndex].ToCell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps adjacency step %d height"), Context, StepIndex),
				ActualRequest.SteppedTerrainSupportMap.AdjacencySteps[StepIndex].StepHeightBlocks,
				ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps[StepIndex].StepHeightBlocks);
		}

		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same validation-assertion count"), Context),
			ActualRequest.ValidationAssertions.Num(),
			ExpectedRequest.ValidationAssertions.Num());
		for (int32 AssertionIndex = 0; AssertionIndex < FMath::Min(ActualRequest.ValidationAssertions.Num(), ExpectedRequest.ValidationAssertions.Num()); ++AssertionIndex)
		{
			const FLayoutValidationAssertionRecord& ActualAssertion = ActualRequest.ValidationAssertions[AssertionIndex];
			const FLayoutValidationAssertionRecord& ExpectedAssertion = ExpectedRequest.ValidationAssertions[AssertionIndex];
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps assertion %d id"), Context, AssertionIndex),
				ActualAssertion.AssertionId,
				ExpectedAssertion.AssertionId);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps assertion %d pass state"), Context, AssertionIndex),
				ActualAssertion.bPassed,
				ExpectedAssertion.bPassed);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps assertion %d failure reason"), Context, AssertionIndex),
				ActualAssertion.FailureReason,
				ExpectedAssertion.FailureReason);
		}

		return bPassed;
	}

	FPlannedLayoutSiteRecord BuildPlanningWindowPlannedRecord(
		const FName WorldBindingId,
		const FName WorldBindingCandidateId,
		const FName BiomeRowName,
		const FString& StableRecordKey,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 SolveSeed)
	{
		FPlannedLayoutSiteRecord PlannedRecord;
		FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
		FrontendSelection.WorldBindingId = WorldBindingId;
		FrontendSelection.WorldBindingCandidateId = WorldBindingCandidateId;
		FrontendSelection.BiomeRowName = BiomeRowName;
		PlannedRecord.SetWorldBindingFrontendSelection(FrontendSelection);
		FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
		LifecycleMetadata.StableRecordKey = StableRecordKey;
		PlannedRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootPlacementPolicyId = WorldBindingId;
		PublicationMetadata.RootCandidateId = WorldBindingCandidateId;
		PublicationMetadata.RootSolveId = FLayoutId(*LifecycleMetadata.StableRecordKey);
		PlannedRecord.SetRootPublicationMetadata(PublicationMetadata);
		FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection;
		ReservationSourceSelection.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		PlannedRecord.SetPlannedSiteReservationSourceSelection(ReservationSourceSelection);
		FLayoutSiteSolveSourceSelection SiteSolveSourceSelection;
		SiteSolveSourceSelection.SolveSeed = SolveSeed;
		PlannedRecord.SetSiteSolveSourceSelection(SiteSolveSourceSelection);
		return PlannedRecord;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderPlanningWindowRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.BuildsPlanningWindowRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderRejectsPlanningWindowRequestWithoutPublicationMetadataTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.RejectsPlanningWindowRequestWithoutPublicationMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderRejectsMismatchedProfileContentSetTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.RejectsMismatchedProfileContentSet",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderPreservesProfileOwnedContentSetSnapshotTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.PreservesProfileOwnedContentSetSnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderPreservesSeedSelectedFootprintAcrossEntryPathsTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.PreservesSeedSelectedFootprintAcrossEntryPaths",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderBuildsStandaloneContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.BuildsStandaloneContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderUsesBindingSharedCellSizeWhenContentSetCompatibilityFieldIsStaleTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.UsesBindingSharedCellSizeWhenContentSetCompatibilityFieldIsStale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderBuildsPlanningWindowSteppedTerrainSupportMapTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.BuildsPlanningWindowSteppedTerrainSupportMap",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderRejectsPlanningWindowSteppedTerrainSupportMapWithoutBiomeRowTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.RejectsPlanningWindowSteppedTerrainSupportMapWithoutBiomeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderPlanningWindowSteppedPolicyRequiresSteppedCapableFrozenProfileTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.PlanningWindowSteppedPolicyRequiresSteppedCapableFrozenProfile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingSolveRequestBuilderPlanningWindowRequestTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSolveRequestBuilder"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSolveRequestBuilder"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_WorldBindingSolveRequestBuilder"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = 1.25f;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Planning-window runtime view resolves from the authored world binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));
	RuntimeView.ContentSet = ContentSet;
	RuntimeView.TemplatePlacementZOffsetBlocks = -2;

	FPlannedLayoutSiteRecord PlannedRecord;
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FrontendSelection.WorldBindingId = WorldBinding->BindingId;
	FrontendSelection.WorldBindingCandidateId = Candidate.CandidateId;
	PlannedRecord.SetWorldBindingFrontendSelection(FrontendSelection);
	FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata;
	LifecycleMetadata.StableRecordKey = TEXT("ReservationBinding:PrimaryCandidate:Reservation:0,0:0,0,4:99");
	PlannedRecord.SetPlannedSiteLifecycleMetadata(LifecycleMetadata);
	FLayoutRootPublicationMetadata PublicationMetadata;
	PublicationMetadata.RootPlacementPolicyId = WorldBinding->BindingId;
	PublicationMetadata.RootCandidateId = Candidate.CandidateId;
	PublicationMetadata.RootSolveId = FLayoutId(*LifecycleMetadata.StableRecordKey);
	PlannedRecord.SetRootPublicationMetadata(PublicationMetadata);
	FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection;
	ReservationSourceSelection.SiteCenterBlockWorldPos = FIntVector(0, 0, 4);
	PlannedRecord.SetPlannedSiteReservationSourceSelection(ReservationSourceSelection);
	FLayoutSiteSolveSourceSelection SiteSolveSourceSelection;
	SiteSolveSourceSelection.SolveSeed = 1337;
	PlannedRecord.SetSiteSolveSourceSelection(SiteSolveSourceSelection);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Planning-window helper builds a request from the planned-site record"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedRecord,
			Request,
			FailureReason));
	const FLayoutRootPublicationMetadata PlannedPublicationMetadata =
		PlannedRecord.GetRootPublicationMetadata();
	TestEqual(TEXT("Planning-window request keeps the expected debug path"), Request.RegionDebugPath, FString(TEXT("PlanningWindow/ReservationBinding/PrimaryCandidate/X=0 Y=0 Z=4")));
	TestEqual(TEXT("Planning-window request keeps the planned publication placement-policy id"), Request.RootPlacementPolicyId, PlannedPublicationMetadata.RootPlacementPolicyId);
	TestEqual(TEXT("Planning-window request keeps the planned publication candidate id"), Request.RootCandidateId, PlannedPublicationMetadata.RootCandidateId);
	TestEqual(TEXT("Ordinary planning-window requests keep the ordinary-root placement kind"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Planning-window request keeps the binding-owned terrain search start"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Planning-window request keeps the binding-owned terrain search depth"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestEqual(TEXT("Ordinary planning-window requests leave continuation family id unset"), Request.RootContinuationSelection.FamilyId, NAME_None);
	TestEqual(TEXT("Ordinary planning-window requests leave continuation placement kind unset"), Request.RootContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Ordinary planning-window requests leave continuation-entry level unresolved"), Request.RootContinuationSelection.ResolvedEntryLevel, INDEX_NONE);
	TestEqual(TEXT("Planning-window request keeps the planned publication solve id"), Request.RootSolveId, PlannedPublicationMetadata.RootSolveId);
	TestEqual(TEXT("Planning-window request keeps the stored planned publication solve id"), Request.RootSolveId, FLayoutId(*PlannedRecord.GetPlannedSiteLifecycleMetadata().StableRecordKey));
	TestEqual(TEXT("Planning-window request maps the authored solve budget"), Request.ExecutionSettings.MaxSolveDurationSeconds, RuntimeView.SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Planning-window request carries the binding-owned placement offset"), Request.TemplatePlacementZOffsetBlocks, RuntimeView.TemplatePlacementZOffsetBlocks);
	const FLayoutValidationAssertionRecord* LatticeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.WorldPlacementLatticeContractValid");
	});
	if (!TestNotNull(TEXT("Planning-window request records a world-placement lattice assertion"), LatticeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Planning-window lattice assertion uses the request-contract kind"), LatticeAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestTrue(TEXT("Planning-window lattice assertion passes for an aligned site center"), LatticeAssertion->bPassed);
	return true;
}

/** Verifies world-binding request assembly rejects content not owned by selected profile. */
bool FLayoutWorldBindingSolveRequestBuilderRejectsMismatchedProfileContentSetTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_WorldBindingMismatchedContentSet"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = CreateRegionContentSet(Outer, TEXT("LayoutContentSet_ProfileOwned"), {});
	ULayoutRegionContentSetAsset* MismatchedContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_Mismatched"),
		{});

	FLayoutWorldBindingRuntimeView RuntimeView;
	RuntimeView.LayoutProfile = Profile;
	RuntimeView.ContentSet = MismatchedContentSet;
	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestFalse(
		TEXT("World-binding request rejects a content set that differs from profile-owned content"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			41,
			TEXT("Standalone/MismatchedContentSet"),
			TEXT("DirectRoot"),
			TEXT("MismatchedCandidate"),
			TEXT("MismatchedSolve"),
			Request,
			FailureReason));
	TestTrue(
		TEXT("World-binding mismatch reports profile-owned content contract"),
		FailureReason.Contains(TEXT("must match profile")));
	return true;
}

/** Verifies standalone, explicit-root, and world-binding request paths retain one content snapshot identity. */
bool FLayoutWorldBindingSolveRequestBuilderPreservesProfileOwnedContentSetSnapshotTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_ContentSetSnapshotParity"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = Profile->ContentSet;
	const FLayoutRegionSolveRequest StandaloneRequest = FLayoutProfileSolver::BuildStandaloneRegionRequest(
		Profile,
		67,
		TEXT("Standalone/ContentSetSnapshotParity"));

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 1.0f;
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			FLayoutWorldBindingPlacementPolicy());
	FLayoutRegionSolveRequest WorldBindingRequest;
	FString FailureReason;
	TestTrue(
		TEXT("World-binding request builds from profile-owned content"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			67,
			TEXT("WorldBinding/ContentSetSnapshotParity"),
			TEXT("Binding"),
			TEXT("Candidate"),
			TEXT("Solve"),
			WorldBindingRequest,
			FailureReason));

	FLayoutRegionSolveRequest ExplicitRootRequest;
	TestTrue(
		TEXT("Explicit-root request builds from profile-owned content"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			FIntVector::ZeroValue,
			67,
			ExplicitRootRequest,
			FailureReason));
	TestEqual(
		TEXT("World-binding request preserves standalone content snapshot identity"),
		WorldBindingRequest.ContentSetSnapshot.SnapshotId,
		StandaloneRequest.ContentSetSnapshot.SnapshotId);
	TestEqual(
		TEXT("Explicit-root request preserves standalone content snapshot identity"),
		ExplicitRootRequest.ContentSetSnapshot.SnapshotId,
		StandaloneRequest.ContentSetSnapshot.SnapshotId);
	TestEqual(
		TEXT("All request paths preserve profile-owned module catalog identity"),
		ExplicitRootRequest.ModuleCatalog.SnapshotId,
		WorldBindingRequest.ModuleCatalog.SnapshotId);
	return true;
}

/** Verifies every request entry path freezes the profile's deterministic footprint selection. */
bool FLayoutWorldBindingSolveRequestBuilderPreservesSeedSelectedFootprintAcrossEntryPathsTest::RunTest(const FString& Parameters)
{
	constexpr int32 Seed = 73;
	ULayoutProfileAsset* Profile = CreateProfileWithUniversalContentSet(
		GetTransientPackage(),
		TEXT("LayoutProfile_FootprintRequestParity"),
		FIntPoint(2, 3),
		FIntPoint(4, 5),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = Profile->ContentSet;
	FRandomStream ExpectedRandom(Seed);
	const FIntPoint ExpectedFootprint(
		ExpectedRandom.RandRange(2, 4),
		ExpectedRandom.RandRange(3, 5));

	const FLayoutRegionSolveRequest StandaloneRequest =
		FLayoutProfileSolver::BuildStandaloneRegionRequest(Profile, Seed, TEXT("Standalone/FootprintParity"));
	FLayoutRootSolveBudgetSettings SolveBudget;
	FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			FLayoutWorldBindingPlacementPolicy());
	RuntimeView.BindingId = TEXT("FootprintParityBinding");

	FString FailureReason;
	FLayoutRegionSolveRequest WorldBindingRequest;
	TestTrue(TEXT("World-binding request builds for footprint parity"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			Seed,
			TEXT("WorldBinding/FootprintParity"),
			TEXT("FootprintParityBinding"),
			TEXT("FootprintParityCandidate"),
			TEXT("FootprintParitySolve"),
			WorldBindingRequest,
			FailureReason));

	FLayoutRegionSolveRequest PlanningWindowRequest;
	const FPlannedLayoutSiteRecord PlannedRecord = BuildPlanningWindowPlannedRecord(
		RuntimeView.BindingId,
		TEXT("FootprintParityCandidate"),
		NAME_None,
		TEXT("FootprintParityRecord"),
		FIntVector::ZeroValue,
		Seed);
	TestTrue(TEXT("Planning-window request builds for footprint parity"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedRecord,
			PlanningWindowRequest,
			FailureReason));

	FLayoutRegionSolveRequest ExplicitRootRequest;
	TestTrue(TEXT("Explicit-root request builds for footprint parity"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			FIntVector::ZeroValue,
			Seed,
			ExplicitRootRequest,
			FailureReason));

	TestEqual(TEXT("Standalone request freezes seeded footprint"), StandaloneRequest.FootprintSize, ExpectedFootprint);
	TestEqual(TEXT("World-binding request preserves seeded footprint"), WorldBindingRequest.FootprintSize, ExpectedFootprint);
	TestEqual(TEXT("Planning-window request preserves seeded footprint"), PlanningWindowRequest.FootprintSize, ExpectedFootprint);
	TestEqual(TEXT("Explicit-root request preserves seeded footprint"), ExplicitRootRequest.FootprintSize, ExpectedFootprint);
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderRejectsPlanningWindowRequestWithoutPublicationMetadataTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSolveRequestBuilderMissingPublication"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSolveRequestBuilderMissingPublication"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_WorldBindingSolveRequestBuilderMissingPublication"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	if (!TestTrue(
			TEXT("Planning-window runtime view resolves from the authored world binding for the missing-publication exact"),
			LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
				WorldBinding,
				0,
				TEXT("Reservation"),
				RuntimeView,
				RuntimeViewFailureReason)))
	{
		return false;
	}
	RuntimeView.ContentSet = ContentSet;

	FPlannedLayoutSiteRecord PlannedRecord =
		BuildPlanningWindowPlannedRecord(
			WorldBinding->BindingId,
			Candidate.CandidateId,
			TEXT("Reservation"),
			TEXT("ReservationBinding:PrimaryCandidate:Reservation:0,0:0,0,4:99"),
			FIntVector(0, 0, 4),
			1337);
	PlannedRecord.SetRootPublicationMetadata(FLayoutRootPublicationMetadata());

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestFalse(
		TEXT("Planning-window helper rejects records that omit authored root publication metadata"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedRecord,
			Request,
			FailureReason));
	TestTrue(
		TEXT("Planning-window helper reports the missing-publication failure reason"),
		FailureReason.Contains(TEXT("requires authored root publication metadata")));
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderBuildsStandaloneContinuationRequestTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingContinuationSolveRequestBuilder"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingContinuationSolveRequestBuilder"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ContinuationSolveRequestBuilder"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -4;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 80;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	Family.ContinuationPolicy.MaxBridgeGapCells = 4;
	Family.SolveBudget.MaxSolveDurationSeconds = 3.75f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("BridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Standalone continuation runtime view resolves from the authored continuation family"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingContinuationFamily(
			WorldBinding,
			0,
			0,
			2,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Standalone continuation helper builds a request from the authored continuation runtime view"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			1771,
			TEXT("Continuation/ReservationBinding/BridgeCandidate"),
			WorldBinding->BindingId,
			FamilyCandidate.CandidateId,
			TEXT("ContinuationSolve"),
			Request,
			FailureReason));
	TestEqual(TEXT("Standalone continuation request keeps the expected debug path"), Request.RegionDebugPath, FString(TEXT("Continuation/ReservationBinding/BridgeCandidate")));
	TestEqual<FLayoutId>(TEXT("Standalone continuation request keeps the world binding as placement policy id"), Request.RootPlacementPolicyId, WorldBinding->BindingId);
	TestEqual<FLayoutId>(TEXT("Standalone continuation request keeps the family candidate id"), Request.RootCandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Standalone continuation request keeps the explicit solve id"), Request.RootSolveId, FLayoutId(TEXT("ContinuationSolve")));
	TestEqual(TEXT("Standalone continuation request keeps the continuation placement kind"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Standalone continuation request keeps the continuation family id"), Request.RootContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Standalone continuation request keeps the continuation placement kind on the selection"), Request.RootContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Standalone continuation request keeps the resolved continuation entry level"), Request.RootContinuationSelection.ResolvedEntryLevel, 2);
	TestEqual(TEXT("Standalone continuation request keeps the family placement policy"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Standalone continuation request maps the family solve budget onto execution settings"), Request.ExecutionSettings.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Standalone continuation request keeps the binding-owned shared cell size on the content-set snapshot"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Standalone continuation request keeps the binding-owned shared cell size on the module-set snapshot"), Request.ModuleCatalog.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderUsesBindingSharedCellSizeWhenContentSetCompatibilityFieldIsStaleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutWorldBindingSolveRequestBuilderStaleSharedCellTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutWorldBindingSolveRequestBuilderStaleSharedCellModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceSolid}),
			MakeTags({LayoutGameplayTags::FaceSolid}),
			ELayoutFaceOccupancyPolicy::RequiresEmptyNeighbor));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("LeafBoundary");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_WorldBindingSolveRequestBuilderStaleSharedCell"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_WorldBindingSolveRequestBuilderStaleSharedCell"),
		{Entry});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBinding_WorldBindingSolveRequestBuilderStaleSharedCell"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(8, 8, 8);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Runtime view resolves from the authored world binding even when the content-set compatibility field is stale"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));

	FPlannedLayoutSiteRecord PlannedRecord = BuildPlanningWindowPlannedRecord(
		WorldBinding->BindingId,
		Candidate.CandidateId,
		NAME_None,
		TEXT("ReservationBinding:PrimaryCandidate:Reservation:0,0:0,0,8:99"),
		FIntVector(0, 0, 8),
		1337);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Planning-window helper builds a request using the binding-owned shared cell size"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedRecord,
			Request,
			FailureReason));
	TestEqual(TEXT("Request content-set snapshot uses the binding-owned shared cell size"), Request.ContentSetSnapshot.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Request module-set snapshot uses the binding-owned shared cell size"), Request.ModuleCatalog.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	if (!TestFalse(TEXT("Request module-set snapshot still contains one compiled module"), Request.ModuleCatalog.Modules.IsEmpty()))
	{
		return false;
	}
	TestEqual(TEXT("Compiled leaf-module snapshot uses the binding-owned shared cell size"), Request.ModuleCatalog.Modules[0].CellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	const FLayoutValidationAssertionRecord* SharedCellAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SharedCellSizeContractValid");
	});
	if (!TestNotNull(TEXT("Request records the shared-cell-size contract assertion"), SharedCellAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Binding-owned request override keeps the request shared-cell-size contract valid"), SharedCellAssertion->bPassed);
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderBuildsPlanningWindowSteppedTerrainSupportMapTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(Outer);

	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	Row.DomainOver = 1.0f;
	Row.GenU_Mat1.AddDefaulted();
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Planning-window stepped-support test initializes the active biome sampler"), Sampler.Initialize(Outer, WorldGenDef, 0));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_WorldBindingSteppedSupportBuilder"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);
	Profile->bSupportsSteppedTerrainSolve = true;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_WorldBindingSteppedSupportBuilder"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBinding_WorldBindingSteppedSupportBuilder"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Planning-window stepped-support runtime view resolves from the authored world binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));

	FPlannedLayoutSiteRecord PlannedRecord = BuildPlanningWindowPlannedRecord(
		WorldBinding->BindingId,
		Candidate.CandidateId,
		TEXT("Reservation"),
		FString(),
		FIntVector(16, 8, 16),
		0);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestTrue(
		TEXT("Planning-window helper builds a stepped support map from the active biome sampler without live chunk reads"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSteppedTerrainSupportMap(
			RuntimeView,
			PlannedRecord,
			FIntPoint(2, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestEqual(TEXT("Stepped support map keeps the binding-owned shared cell height"), SupportMap.SharedCellHeightInBlocks, WorldBinding->BaseCellDimensionsBlocks.Z);
	TestEqual(TEXT("Stepped support map preserves one support sample per planned cell"), SupportMap.SupportSamples.Num(), 2);
	TestEqual(TEXT("Stepped support map preserves one horizontal adjacency step"), SupportMap.AdjacencySteps.Num(), 1);
	if (!TestEqual(TEXT("Stepped support map keeps two addressable support samples"), SupportMap.SupportSamples.Num(), 2))
	{
		return false;
	}
	TestEqual(TEXT("Stepped support map preserves the first support sample Z from the active biome surface"), SupportMap.SupportSamples[0].SupportSurfaceZ, 49);
	TestEqual(TEXT("Stepped support map preserves the second support sample Z from the active biome surface"), SupportMap.SupportSamples[1].SupportSurfaceZ, 49);
	TestEqual(TEXT("Flat active-biome surfaces preserve zero observed neighbor delta"), SupportMap.MaximumObservedNeighborHeightDelta, 0);

	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (Pass == 1)
		{
			// A saved edit deliberately disagrees with the procedural plane.
			Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, 60), 1, false);
		}
		FLayoutSteppedTerrainSupportMap WithWorld;
		TestTrue(TEXT("World presence and saved edits cannot replace procedural support"),
			LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSteppedTerrainSupportMap(
				RuntimeView, PlannedRecord, FIntPoint(2, 1), PlannedCells,
				MakeTestCoordinateSettings(WorldGenDef), Sampler, WithWorld, FailureReason, Harness.World));
		TestTrue(TEXT("Planning support remains identical to the noise-only result"),
			FLayoutTerrainSampling::AreSteppedTerrainSupportMapsContractEquivalent(SupportMap, WithWorld, &FailureReason));
	}
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderRejectsPlanningWindowSteppedTerrainSupportMapWithoutBiomeRowTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(Outer);

	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Planned-site biome-row rejection test initializes the active biome sampler"), Sampler.Initialize(Outer, WorldGenDef, 0));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_WorldBindingSteppedSupportMissingBiome"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_WorldBindingSteppedSupportMissingBiome"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBinding_WorldBindingSteppedSupportMissingBiome"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Planned-site biome-row rejection runtime view resolves from the authored world binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));

	FPlannedLayoutSiteRecord PlannedRecord = BuildPlanningWindowPlannedRecord(
		WorldBinding->BindingId,
		Candidate.CandidateId,
		NAME_None,
		FString(),
		FIntVector::ZeroValue,
		0);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector::ZeroValue, ELayoutCellIntent::Entry});

	FLayoutSteppedTerrainSupportMap SupportMap;
	FString FailureReason;
	TestFalse(
		TEXT("Planning-window helper rejects stepped support-map compilation when the planned site has no biome row"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSteppedTerrainSupportMap(
			RuntimeView,
			PlannedRecord,
			FIntPoint(1, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			SupportMap,
			FailureReason));
	TestTrue(TEXT("Planning-window stepped-support rejection reports the missing planned-site biome row"), FailureReason.Contains(TEXT("planned-site biome row name")));
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderPlanningWindowSteppedPolicyRequiresSteppedCapableFrozenProfileTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(Outer);

	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	Row.DomainOver = 1.0f;
	Row.GenU_Mat1.AddDefaulted();
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Stepped-capability request-contract test initializes the active biome sampler"), Sampler.Initialize(Outer, WorldGenDef, 0));

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_WorldBindingSteppedCapabilityFrozenRequest"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_WorldBindingSteppedCapabilityFrozenRequest"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBinding_WorldBindingSteppedCapabilityFrozenRequest"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Stepped-capability request-contract runtime view resolves from the authored world binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));

	FPlannedLayoutSiteRecord PlannedRecord = BuildPlanningWindowPlannedRecord(
		WorldBinding->BindingId,
		Candidate.CandidateId,
		TEXT("Reservation"),
		TEXT("ReservationBinding:PrimaryCandidate:Reservation:0,0:16,8,16:103"),
		FIntVector(16, 8, 16),
		7332);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	const bool bBuiltSteppedCapabilityRequest =
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequestWithSteppedTerrainSupport(
			RuntimeView,
			PlannedRecord,
			FIntPoint(2, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			Request,
			FailureReason);
	if (!TestTrue(
			TEXT("Stepped-capability request builder captures supplied cells before prewarm validation"),
			bBuiltSteppedCapabilityRequest))
	{
		if (!FailureReason.IsEmpty())
		{
			AddError(FailureReason);
		}
		return false;
	}
	const FLayoutValidationAssertionRecord* CapabilityAssertion = Request.ValidationAssertions.FindByPredicate(
		[](const FLayoutValidationAssertionRecord& Assertion)
		{
			return Assertion.AssertionId == TEXT("RegionRequest.SteppedProfileCapabilityContractValid");
		});
	if (!TestNotNull(TEXT("Captured request contains the frozen stepped-capability assertion"), CapabilityAssertion))
	{
		return false;
	}
	TestFalse(TEXT("Stepped support is rejected for a frozen profile without stepped capability"), CapabilityAssertion->bPassed);
	TestTrue(TEXT("Capability rejection identifies the frozen profile contract"),
		CapabilityAssertion->FailureReason.Contains(TEXT("does not declare stepped-terrain capability")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderStandaloneRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.BuildsStandaloneRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSolveRequestBuilderPlanningWindowLatticeAssertionTest,
	"PorismExtension.Layout.Planning.WorldBindingSolveRequestBuilder.PlanningWindowLatticeAssertion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingSolveRequestBuilderStandaloneRequestTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSolveRequestStandalone"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSolveRequestBuilder"),
		{});
	Profile->ContentSet = ContentSet;

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	FLayoutWorldBindingRuntimeView RuntimeView;
	RuntimeView.LayoutProfile = Profile;
	RuntimeView.ContentSet = ContentSet;
	RuntimeView.SolveBudget.MaxSolveDurationSeconds = 2.5f;
	RuntimeView.TemplatePlacementZOffsetBlocks = 4;
	TestTrue(
		TEXT("Standalone world-binding helper builds a content-set-backed request"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			RuntimeView,
			41,
			TEXT("Standalone/WorldBinding"),
			TEXT("DirectRootExplicit"),
			TEXT("StandaloneCandidate"),
			TEXT("StandaloneSolve"),
			Request,
			FailureReason));
	TestEqual(TEXT("Standalone request keeps the explicit debug path"), Request.RegionDebugPath, FString(TEXT("Standalone/WorldBinding")));
	TestEqual(TEXT("Standalone request keeps the requested placement policy id"), Request.RootPlacementPolicyId, FLayoutId(TEXT("DirectRootExplicit")));
	TestEqual(TEXT("Standalone request keeps the requested candidate id"), Request.RootCandidateId, FLayoutId(TEXT("StandaloneCandidate")));
	TestEqual(TEXT("Standalone request keeps the requested solve id"), Request.RootSolveId, FLayoutId(TEXT("StandaloneSolve")));
	TestEqual(TEXT("Standalone request keeps the supplied solve seed"), Request.Seed, 41);
	TestEqual(TEXT("Standalone direct-root requests keep no world-binding placement kind by default"), Request.RootPlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Standalone request keeps the supplied execution settings"), Request.ExecutionSettings.MaxSolveDurationSeconds, RuntimeView.SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Standalone request keeps the binding-owned placement offset"), Request.TemplatePlacementZOffsetBlocks, RuntimeView.TemplatePlacementZOffsetBlocks);
	return true;
}

bool FLayoutWorldBindingSolveRequestBuilderPlanningWindowLatticeAssertionTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSolveRequestBuilderLattice"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSolveRequestBuilderLattice"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_WorldBindingSolveRequestBuilderLattice"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 5);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView RuntimeView;
	FString RuntimeViewFailureReason;
	TestTrue(
		TEXT("Lattice runtime view resolves from the authored world binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			RuntimeView,
			RuntimeViewFailureReason));
	RuntimeView.ContentSet = ContentSet;

	FPlannedLayoutSiteRecord PlannedRecord = BuildPlanningWindowPlannedRecord(
		WorldBinding->BindingId,
		Candidate.CandidateId,
		NAME_None,
		TEXT("ReservationBinding:PrimaryCandidate:Reservation:0,0:0,0,6:99"),
		FIntVector(0, 0, 6),
		1337);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Planning-window helper still builds a request for a misaligned planned-site record"),
		LayoutWorldBindingSolveRequestBuilder::TryBuildPlanningWindowSolveRequest(
			RuntimeView,
			PlannedRecord,
			Request,
			FailureReason));

	const FLayoutValidationAssertionRecord* LatticeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.WorldPlacementLatticeContractValid");
	});
	if (!TestNotNull(TEXT("Misaligned planning-window request records a world-placement lattice assertion"), LatticeAssertion))
	{
		return false;
	}
	TestFalse(TEXT("Misaligned planning-window request fails the lattice assertion"), LatticeAssertion->bPassed);
	TestTrue(TEXT("Misaligned planning-window request reports the planned stable key in the lattice failure reason"), LatticeAssertion->FailureReason.Contains(TEXT("ReservationBinding:PrimaryCandidate:Reservation:0,0:0,0,6:99")));
	TestTrue(TEXT("Misaligned planning-window request reports the site center in the lattice failure reason"), LatticeAssertion->FailureReason.Contains(TEXT("X=0 Y=0 Z=6")));
	TestTrue(TEXT("Misaligned planning-window request references the frontend world-binding id on the lattice assertion"), LatticeAssertion->RelatedIds.Contains(WorldBinding->BindingId));
	TestTrue(TEXT("Misaligned planning-window request references the frontend candidate id on the lattice assertion"), LatticeAssertion->RelatedIds.Contains(Candidate.CandidateId));
	return true;
}
