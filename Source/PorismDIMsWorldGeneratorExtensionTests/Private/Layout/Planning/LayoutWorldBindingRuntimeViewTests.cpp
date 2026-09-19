// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"

#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsAuthoredWorldBindingViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsAuthoredWorldBindingView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewUsesBindingSharedCellSizeWhenContentSetCompatibilityFieldIsStaleTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.UsesBindingSharedCellSizeWhenContentSetCompatibilityFieldIsStale",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewResolvesWorldBindingCandidateTargetDeterministicallyTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.ResolvesWorldBindingCandidateTargetDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsAuthoredWorldBindingViewFromCandidateTargetTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsAuthoredWorldBindingViewFromCandidateTarget",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsAuthoredWorldBindingViewFromCandidateIdTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsAuthoredWorldBindingViewFromCandidateId",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsAuthoredContinuationFamilyViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsAuthoredContinuationFamilyView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsBindingAwareExplicitToolContinuationViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsBindingAwareExplicitToolContinuationView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewResolvesContinuationFamilyTargetByEndpointTagTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.ResolvesContinuationFamilyTargetByEndpointTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewResolvesTunnelContinuationFamilyTargetByEndpointTagTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.ResolvesTunnelContinuationFamilyTargetByEndpointTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewResolvesContinuationFamilyCandidateTargetDeterministicallyTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.ResolvesContinuationFamilyCandidateTargetDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewResolvesTunnelContinuationFamilyCandidateTargetDeterministicallyTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.ResolvesTunnelContinuationFamilyCandidateTargetDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingRuntimeViewBuildsAuthoredWorldBindingViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingRuntimeViewAsset"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingRuntimeViewAsset"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_RuntimeViewAsset"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	WorldBinding->TemplatePlacementZOffsetBlocks = -3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 30;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = 2.5f;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;
	Candidate.bOverrideTerrainTransitionPolicy = true;
	Candidate.TerrainTransitionPolicyOverride.bAllowFoundationFill = true;
	Candidate.TerrainTransitionPolicyOverride.MaxFoundationDepth = 7;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Authored world binding resolves a runtime view from the selected candidate"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Authored world binding view resolves the selected profile"), ResolvedView.LayoutProfile, Profile);
	TestEqual(TEXT("Authored world binding view keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Authored world binding view keeps the selected candidate id"), ResolvedView.CandidateId, Candidate.CandidateId);
	TestEqual(TEXT("Authored world binding view resolves the preferred content set"), ResolvedView.ContentSet, ContentSet);
	TestEqual(TEXT("Authored world binding view resolves the ordinary-root placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Authored world binding view keeps the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Authored world binding view keeps the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Authored world binding view keeps the binding-owned terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Authored world binding view keeps the binding-owned terrain search depth"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestTrue(TEXT("Authored world binding view applies the candidate terrain-transition override"), ResolvedView.PlacementPolicy.TerrainTransition.bAllowFoundationFill);
	TestEqual(TEXT("Authored world binding view keeps the candidate terrain-transition override depth"), ResolvedView.PlacementPolicy.TerrainTransition.MaxFoundationDepth, Candidate.TerrainTransitionPolicyOverride.MaxFoundationDepth);
	TestEqual(TEXT("Authored world binding view keeps the binding-owned solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, WorldBinding->SolveBudget.MaxSolveDurationSeconds);
	return true;
}

bool FLayoutWorldBindingRuntimeViewUsesBindingSharedCellSizeWhenContentSetCompatibilityFieldIsStaleTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutWorldBindingRuntimeViewStaleSharedCellTemplate"),
		FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutWorldBindingRuntimeViewStaleSharedCellModule"),
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
		TEXT("LayoutProfile_WorldBindingRuntimeViewStaleSharedCell"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_WorldBindingRuntimeViewStaleSharedCell"),
		{Entry});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBinding_RuntimeViewStaleSharedCell"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(8, 8, 8);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Runtime view still builds when the content-set compatibility shared-cell-size field is stale"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBinding(
			WorldBinding,
			0,
			TEXT("Reservation"),
			ResolvedView,
			FailureReason));
	TestEqual(
		TEXT("Runtime view keeps the binding-owned shared cell size instead of the stale content-set compatibility field"),
		ResolvedView.SharedCellSizeInBlocks,
		WorldBinding->BaseCellDimensionsBlocks);
	return true;
}

bool FLayoutWorldBindingRuntimeViewResolvesWorldBindingCandidateTargetDeterministicallyTest::RunTest(const FString& Parameters)
{
	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_CandidateTargetSelection"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);

	FLayoutWorldBindingCandidate& CandidateA = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateA.CandidateId = TEXT("RootCandidateA");
	CandidateA.Weight = 1;
	const FLayoutId CandidateAId = CandidateA.CandidateId;

	FLayoutWorldBindingCandidate& CandidateB = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateB.CandidateId = TEXT("RootCandidateB");
	CandidateB.Weight = 3;
	const FLayoutId CandidateBId = CandidateB.CandidateId;

	const FIntVector SiteCenterBlockWorldPos(12, 20, 4);
	const int32 WorldSeed = 99;
	const uint32 SelectionSeed = HashCombineFast(
		HashCombineFast(static_cast<uint32>(WorldSeed), GetTypeHash(WorldBinding->BindingId)),
		HashCombineFast(GetTypeHash(SiteCenterBlockWorldPos), static_cast<uint32>(WorldBinding->Candidates.Num())));
	const int32 WeightedPick = static_cast<int32>(SelectionSeed % 4u);
	const int32 ExpectedCandidateIndex = WeightedPick < 1 ? 0 : 1;

	FLayoutWorldBindingCandidateTarget ResolvedTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Shared ordinary-root target lookup resolves one deterministic weighted root candidate"),
		LayoutWorldBindingRuntimeView::TryResolveWorldBindingCandidateTarget(
			WorldBinding,
			SiteCenterBlockWorldPos,
			WorldSeed,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Deterministic root target keeps the weighted candidate index"), ResolvedTarget.CandidateIndex, ExpectedCandidateIndex);
	TestTrue(
		TEXT("Deterministic root target keeps the weighted candidate id"),
		ResolvedTarget.Candidate != nullptr
			&& ResolvedTarget.Candidate->CandidateId
				== (ExpectedCandidateIndex == 0 ? CandidateAId : CandidateBId));
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsAuthoredWorldBindingViewFromCandidateTargetTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* ProfileA = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingRuntimeViewTargetA"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSetA = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingRuntimeViewTargetA"),
		{});
	ProfileA->ContentSet = ContentSetA;

	ULayoutProfileAsset* ProfileB = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingRuntimeViewTargetB"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSetB = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingRuntimeViewTargetB"),
		{});
	ProfileB->ContentSet = ContentSetB;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_RuntimeViewCandidateTarget"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -2;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 28;
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = 3.5f;

	FLayoutWorldBindingCandidate& CandidateA = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateA.CandidateId = TEXT("RootCandidateA");
	CandidateA.LayoutProfile = ProfileA;
	CandidateA.Weight = 1;

	FLayoutWorldBindingCandidate& CandidateB = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateB.CandidateId = TEXT("RootCandidateB");
	CandidateB.LayoutProfile = ProfileB;
	CandidateB.Weight = 3;

	const FIntVector SiteCenterBlockWorldPos(12, 20, 4);
	const int32 WorldSeed = 99;
	FLayoutWorldBindingCandidateTarget CandidateTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Shared target lookup resolves the authored ordinary-root candidate before runtime-view rebuild"),
		LayoutWorldBindingRuntimeView::TryResolveWorldBindingCandidateTarget(
			WorldBinding,
			SiteCenterBlockWorldPos,
			WorldSeed,
			CandidateTarget,
			FailureReason));
	const bool bSelectedCandidateB =
		CandidateTarget.Candidate != nullptr
		&& CandidateTarget.Candidate->CandidateId == CandidateB.CandidateId;
	ULayoutProfileAsset* const ExpectedProfile = bSelectedCandidateB ? ProfileB : ProfileA;
	ULayoutRegionContentSetAsset* const ExpectedContentSet = bSelectedCandidateB ? ContentSetB : ContentSetA;
	const FName ExpectedCandidateId = bSelectedCandidateB ? CandidateB.CandidateId : CandidateA.CandidateId;

	FLayoutWorldBindingRuntimeView ResolvedView;
	TestTrue(
		TEXT("Shared target-based runtime-view rebuild keeps the selected authored root candidate"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingCandidateTarget(
			WorldBinding,
			CandidateTarget,
			TEXT("Reservation"),
			ResolvedView,
			FailureReason));

	TestEqual(TEXT("Target-based runtime-view rebuild keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Target-based runtime-view rebuild keeps the selected candidate id"), ResolvedView.CandidateId, ExpectedCandidateId);
	TestEqual(TEXT("Target-based runtime-view rebuild resolves the selected candidate profile"), ResolvedView.LayoutProfile, ExpectedProfile);
	TestEqual(TEXT("Target-based runtime-view rebuild resolves the selected candidate content set"), ResolvedView.ContentSet, ExpectedContentSet);
	TestEqual(TEXT("Target-based runtime-view rebuild keeps the ordinary-root placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Target-based runtime-view rebuild keeps the binding-owned terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Target-based runtime-view rebuild keeps the binding-owned solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, WorldBinding->SolveBudget.MaxSolveDurationSeconds);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsAuthoredWorldBindingViewFromCandidateIdTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingRuntimeViewCandidateId"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingRuntimeViewCandidateId"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_RuntimeViewCandidateId"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	WorldBinding->TemplatePlacementZOffsetBlocks = -3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 30;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = 2.5f;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Record-owned candidate-id rebuild resolves the authored ordinary-root runtime view without reopening weighted target selection"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingCandidateId(
			WorldBinding,
			Candidate.CandidateId,
			TEXT("Reservation"),
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Candidate-id runtime-view rebuild keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Candidate-id runtime-view rebuild keeps the selected candidate id"), ResolvedView.CandidateId, Candidate.CandidateId);
	TestEqual(TEXT("Candidate-id runtime-view rebuild resolves the selected profile"), ResolvedView.LayoutProfile, Profile);
	TestEqual(TEXT("Candidate-id runtime-view rebuild resolves the preferred content set"), ResolvedView.ContentSet, ContentSet);
	TestEqual(TEXT("Candidate-id runtime-view rebuild keeps the ordinary-root placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Candidate-id runtime-view rebuild keeps the binding-owned terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Candidate-id runtime-view rebuild keeps the continuation family unset"), ResolvedView.ContinuationSelection.FamilyId, NAME_None);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsAuthoredContinuationFamilyViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingContinuationFamilyRuntimeView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingContinuationFamilyRuntimeView"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_AuthoredContinuationFamilyRuntimeView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 21;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 43;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;
	Family.ContinuationPolicy.MaxBridgeGapCells = 3;
	Family.bOverrideTerrainTransitionPolicy = true;
	Family.TerrainTransitionPolicyOverride.bAllowFoundationFill = true;
	Family.TerrainTransitionPolicyOverride.MaxFoundationDepth = 6;
	Family.SolveBudget.MaxSolveDurationSeconds = 4.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("BridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Authored continuation family resolves one runtime view from the selected family candidate"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingContinuationFamily(
			WorldBinding,
			0,
			0,
			1,
			TEXT("Reservation"),
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Authored continuation-family view keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Authored continuation-family view keeps the selected family candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Authored continuation-family view resolves the selected profile"), ResolvedView.LayoutProfile, Profile);
	TestEqual(TEXT("Authored continuation-family view resolves the preferred content set"), ResolvedView.ContentSet, ContentSet);
	TestEqual(TEXT("Authored continuation-family view resolves the continuation placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Authored continuation-family view keeps the authored continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Authored continuation-family view keeps the authored continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Authored continuation-family view keeps the resolved continuation entry level"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 1);
	TestEqual(TEXT("Authored continuation-family view keeps the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Authored continuation-family view keeps the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Authored continuation-family view derives placement kind from family type instead of the stale nested continuation policy"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Authored continuation-family view keeps shared surface-search start from the binding default"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Authored continuation-family view keeps shared surface-search depth from the binding default"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestTrue(TEXT("Authored continuation-family view applies the family terrain-transition override"), ResolvedView.PlacementPolicy.TerrainTransition.bAllowFoundationFill);
	TestEqual(TEXT("Authored continuation-family view keeps the family terrain-transition override depth"), ResolvedView.PlacementPolicy.TerrainTransition.MaxFoundationDepth, Family.TerrainTransitionPolicyOverride.MaxFoundationDepth);
	TestEqual(TEXT("Authored continuation-family view resolves the family-specific solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsBindingAwareExplicitToolContinuationViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingExplicitToolContinuationRuntimeView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContinuationEntryLevel = 0;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingExplicitToolContinuationRuntimeView"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ExplicitToolContinuationRuntimeView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	WorldBinding->TemplatePlacementZOffsetBlocks = -2;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 40;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 64;

	{
		FLayoutWorldBindingRuntimeView MissingProfileView;
		FLayoutWorldBindingSiteFrontendSelection MissingProfileSelection;
		FString MissingProfileFailureReason;
		TestFalse(
			TEXT("Binding-aware explicit tool view rejects a profile missing from the selected world binding"),
			LayoutWorldBindingRuntimeView::TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
				WorldBinding,
				Profile,
				MissingProfileView,
				MissingProfileSelection,
				MissingProfileFailureReason));
		TestTrue(
			TEXT("Missing-profile rejection identifies the absent world-binding reference"),
			MissingProfileFailureReason.Contains(TEXT("is not referenced by world binding")));
		TestTrue(
			TEXT("Missing-profile rejection names the standalone preview configuration field"),
			MissingProfileFailureReason.Contains(TEXT("Candidates[].LayoutProfile")));
		TestTrue(
			TEXT("Missing-profile rejection distinguishes child-region references"),
			MissingProfileFailureReason.Contains(TEXT("Child-region content entries do not make profiles directly previewable")));
	}

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("SurfacePathFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 3.5f;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("SurfacePathCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FString FailureReason;
	TestTrue(
		TEXT("Binding-aware explicit tool view resolves one continuation-family runtime view from the selected profile"),
		LayoutWorldBindingRuntimeView::TryBuildExplicitToolRuntimeViewFromWorldBindingProfile(
			WorldBinding,
			Profile,
			ResolvedView,
			FrontendSelection,
			FailureReason));
	TestEqual(TEXT("Binding-aware explicit tool continuation view keeps the world binding id"), FrontendSelection.WorldBindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Binding-aware explicit tool continuation view keeps the continuation candidate id"), FrontendSelection.WorldBindingCandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Binding-aware explicit tool continuation view keeps the continuation family id"), FrontendSelection.ResolvedContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Binding-aware explicit tool continuation view resolves the continuation placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::SurfacePath);
	TestEqual(TEXT("Binding-aware explicit tool continuation view keeps the resolved continuation entry level"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 0);
	TestEqual(TEXT("Binding-aware explicit tool continuation view keeps the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Binding-aware explicit tool continuation view keeps the family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	return true;
}

bool FLayoutWorldBindingRuntimeViewResolvesContinuationFamilyTargetByEndpointTagTest::RunTest(const FString& Parameters)
{
	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ContinuationFamilyTargetSelection"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);

	FLayoutWorldBindingContinuationFamily& BridgeFamily = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	BridgeFamily.FamilyId = TEXT("BridgeFamily");
	BridgeFamily.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	BridgeFamily.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;

	FLayoutWorldBindingContinuationFamily& TrailBridgeFamily = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	TrailBridgeFamily.FamilyId = TEXT("TrailBridgeFamily");
	TrailBridgeFamily.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	TrailBridgeFamily.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTrail;

	FLayoutWorldBindingContinuationFamilyTarget ResolvedTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Shared authored family-target lookup resolves the bridge-tagged continuation family"),
		LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyTarget(
			WorldBinding,
			ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
			LayoutGameplayTags::ConnectorBridge,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Bridge-tagged family target keeps the authored family index"), ResolvedTarget.FamilyIndex, 0);
	TestTrue(
		TEXT("Bridge-tagged family target keeps the authored family id"),
		ResolvedTarget.Family != nullptr && ResolvedTarget.Family->FamilyId == BridgeFamily.FamilyId);

	TestTrue(
		TEXT("Shared authored family-target lookup resolves the trail-tagged continuation family"),
		LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyTarget(
			WorldBinding,
			ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
			LayoutGameplayTags::ConnectorTrail,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Trail-tagged family target keeps the authored family index"), ResolvedTarget.FamilyIndex, 1);
	TestTrue(
		TEXT("Trail-tagged family target keeps the authored family id"),
		ResolvedTarget.Family != nullptr && ResolvedTarget.Family->FamilyId == TrailBridgeFamily.FamilyId);
	return true;
}

bool FLayoutWorldBindingRuntimeViewResolvesTunnelContinuationFamilyTargetByEndpointTagTest::RunTest(const FString& Parameters)
{
	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_TunnelContinuationFamilyTargetSelection"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);

	FLayoutWorldBindingContinuationFamily& TunnelFamily = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	TunnelFamily.FamilyId = TEXT("TunnelFamily");
	TunnelFamily.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	TunnelFamily.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;

	FLayoutWorldBindingContinuationFamily& TrailTunnelFamily = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	TrailTunnelFamily.FamilyId = TEXT("TrailTunnelFamily");
	TrailTunnelFamily.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	TrailTunnelFamily.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTrail;

	FLayoutWorldBindingContinuationFamilyTarget ResolvedTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Shared authored family-target lookup resolves the tunnel-tagged continuation family"),
		LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyTarget(
			WorldBinding,
			ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
			LayoutGameplayTags::ConnectorTunnel,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Tunnel-tagged family target keeps the authored family index"), ResolvedTarget.FamilyIndex, 0);
	TestTrue(
		TEXT("Tunnel-tagged family target keeps the authored family id"),
		ResolvedTarget.Family != nullptr && ResolvedTarget.Family->FamilyId == TunnelFamily.FamilyId);

	TestTrue(
		TEXT("Shared authored family-target lookup resolves the trail-tagged tunnel continuation family"),
		LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyTarget(
			WorldBinding,
			ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
			LayoutGameplayTags::ConnectorTrail,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Trail-tagged tunnel family target keeps the authored family index"), ResolvedTarget.FamilyIndex, 1);
	TestTrue(
		TEXT("Trail-tagged tunnel family target keeps the authored family id"),
		ResolvedTarget.Family != nullptr && ResolvedTarget.Family->FamilyId == TrailTunnelFamily.FamilyId);
	return true;
}

bool FLayoutWorldBindingRuntimeViewResolvesContinuationFamilyCandidateTargetDeterministicallyTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* ProfileA = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ContinuationCandidateTargetA"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutProfileAsset* ProfileB = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ContinuationCandidateTargetB"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ContinuationFamilyCandidateTargetSelection"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);

	FLayoutWorldBindingContinuationFamily& BridgeFamily = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	BridgeFamily.FamilyId = TEXT("BridgeFamily");
	BridgeFamily.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	BridgeFamily.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;

	FLayoutWorldBindingContinuationCandidate& CandidateA = BridgeFamily.Candidates.AddDefaulted_GetRef();
	CandidateA.CandidateId = TEXT("BridgeCandidateA");
	CandidateA.LayoutProfile = ProfileA;
	CandidateA.Weight = 1;
	const FLayoutId CandidateAId = CandidateA.CandidateId;

	FLayoutWorldBindingContinuationCandidate& CandidateB = BridgeFamily.Candidates.AddDefaulted_GetRef();
	CandidateB.CandidateId = TEXT("BridgeCandidateB");
	CandidateB.LayoutProfile = ProfileB;
	CandidateB.Weight = 3;
	const FLayoutId CandidateBId = CandidateB.CandidateId;

	const uint64 PairKey = 0x12345678ABCDEF01ull;
	const int32 WorldSeed = 417;
	const uint32 SelectionSeed = HashCombineFast(
		HashCombineFast(static_cast<uint32>(WorldSeed), GetTypeHash(BridgeFamily.FamilyId)),
		HashCombineFast(static_cast<uint32>(PairKey), static_cast<uint32>(BridgeFamily.Candidates.Num())));
	const int32 WeightedPick = static_cast<int32>(SelectionSeed % 4u);
	const int32 ExpectedCandidateIndex = WeightedPick < 1 ? 0 : 1;

	FLayoutWorldBindingContinuationFamilyCandidateTarget ResolvedTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Shared authored family-plus-candidate lookup resolves one deterministic bridge candidate"),
		LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyCandidateTarget(
			WorldBinding,
			ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
			LayoutGameplayTags::ConnectorBridge,
			PairKey,
			WorldSeed,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Deterministic bridge family target keeps the authored family index"), ResolvedTarget.FamilyIndex, 0);
	TestEqual(TEXT("Deterministic bridge family target keeps the weighted candidate index"), ResolvedTarget.CandidateIndex, ExpectedCandidateIndex);
	TestTrue(
		TEXT("Deterministic bridge family target keeps the authored family id"),
		ResolvedTarget.Family != nullptr && ResolvedTarget.Family->FamilyId == BridgeFamily.FamilyId);
	TestTrue(
		TEXT("Deterministic bridge family target keeps the weighted candidate id"),
		ResolvedTarget.Candidate != nullptr
			&& ResolvedTarget.Candidate->CandidateId
				== (ExpectedCandidateIndex == 0 ? CandidateAId : CandidateBId));
	return true;
}

bool FLayoutWorldBindingRuntimeViewResolvesTunnelContinuationFamilyCandidateTargetDeterministicallyTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* ProfileA = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_TunnelContinuationCandidateTargetA"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutProfileAsset* ProfileB = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_TunnelContinuationCandidateTargetB"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_TunnelContinuationFamilyCandidateTargetSelection"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);

	FLayoutWorldBindingContinuationFamily& TunnelFamily = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	TunnelFamily.FamilyId = TEXT("TunnelFamily");
	TunnelFamily.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	TunnelFamily.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;

	FLayoutWorldBindingContinuationCandidate& CandidateA = TunnelFamily.Candidates.AddDefaulted_GetRef();
	CandidateA.CandidateId = TEXT("TunnelCandidateA");
	CandidateA.LayoutProfile = ProfileA;
	CandidateA.Weight = 1;
	const FLayoutId CandidateAId = CandidateA.CandidateId;

	FLayoutWorldBindingContinuationCandidate& CandidateB = TunnelFamily.Candidates.AddDefaulted_GetRef();
	CandidateB.CandidateId = TEXT("TunnelCandidateB");
	CandidateB.LayoutProfile = ProfileB;
	CandidateB.Weight = 3;
	const FLayoutId CandidateBId = CandidateB.CandidateId;

	const uint64 PairKey = 0x0FEDCBA987654321ull;
	const int32 WorldSeed = 417;
	const uint32 SelectionSeed = HashCombineFast(
		HashCombineFast(static_cast<uint32>(WorldSeed), GetTypeHash(TunnelFamily.FamilyId)),
		HashCombineFast(static_cast<uint32>(PairKey), static_cast<uint32>(TunnelFamily.Candidates.Num())));
	const int32 WeightedPick = static_cast<int32>(SelectionSeed % 4u);
	const int32 ExpectedCandidateIndex = WeightedPick < 1 ? 0 : 1;

	FLayoutWorldBindingContinuationFamilyCandidateTarget ResolvedTarget;
	FString FailureReason;
	TestTrue(
		TEXT("Shared authored family-plus-candidate lookup resolves one deterministic tunnel candidate"),
		LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyCandidateTarget(
			WorldBinding,
			ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
			LayoutGameplayTags::ConnectorTunnel,
			PairKey,
			WorldSeed,
			ResolvedTarget,
			FailureReason));
	TestEqual(TEXT("Deterministic tunnel family target keeps the authored family index"), ResolvedTarget.FamilyIndex, 0);
	TestEqual(TEXT("Deterministic tunnel family target keeps the weighted candidate index"), ResolvedTarget.CandidateIndex, ExpectedCandidateIndex);
	TestTrue(
		TEXT("Deterministic tunnel family target keeps the authored family id"),
		ResolvedTarget.Family != nullptr && ResolvedTarget.Family->FamilyId == TunnelFamily.FamilyId);
	TestTrue(
		TEXT("Deterministic tunnel family target keeps the weighted candidate id"),
		ResolvedTarget.Candidate != nullptr
			&& ResolvedTarget.Candidate->CandidateId
				== (ExpectedCandidateIndex == 0 ? CandidateAId : CandidateBId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsInconsistentContinuationSelectionTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsInconsistentContinuationSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsPlannedContinuationSiteViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsPlannedContinuationSiteView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedContinuationSiteViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedContinuationSiteView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedSiteViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedSiteView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedConnectorView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedConnectorLiveFamilyView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedBridgeConnectorLiveFamilyView",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewWithoutSharedBiomeRowTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedConnectorLiveFamilyViewWithoutSharedBiomeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorCarrierWithoutLiveBindingTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedConnectorCarrierWithoutLiveBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorCarrierWithoutLiveBindingTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedBridgeConnectorCarrierWithoutLiveBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorCarrierWhenLiveCandidateCannotBeRebuiltTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedConnectorCarrierWhenLiveCandidateCannotBeRebuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorCarrierWhenLiveCandidateCannotBeRebuiltTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedBridgeConnectorCarrierWhenLiveCandidateCannotBeRebuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorThinStoredCarrierWhenLiveCandidateCannotBeRebuiltTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedConnectorThinStoredCarrierWhenLiveCandidateCannotBeRebuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorThinStoredCarrierWhenLiveCandidateCannotBeRebuiltTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedBridgeConnectorThinStoredCarrierWhenLiveCandidateCannotBeRebuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewFromThinStoredCarrierWithoutSharedBiomeRowTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedConnectorLiveFamilyViewFromThinStoredCarrierWithoutSharedBiomeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierWithoutSharedBiomeRowTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierWithoutSharedBiomeRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorThinStoredCarrierWithoutLiveBindingTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedConnectorThinStoredCarrierWithoutLiveBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorThinStoredCarrierWithoutLiveBindingTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.RejectsResolvedBridgeConnectorThinStoredCarrierWithoutLiveBinding",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeViewBuildsLegacyResolvedConnectorDirectRootViewWithoutFrontendCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingRuntimeView.BuildsLegacyResolvedConnectorDirectRootViewWithoutFrontendCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingRuntimeViewRejectsInconsistentContinuationSelectionTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingRuntimeViewContinuation"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingRuntimeViewContinuation"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_RuntimeViewContinuation"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;

	FPlannedLayoutSiteRecord PlannedRecord;
	PlannedRecord.WorldBindingId = WorldBinding->BindingId;
	PlannedRecord.WorldBindingCandidateId = Candidate.CandidateId;
	PlannedRecord.BiomeRowName = TEXT("Reservation");
	PlannedRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	PlannedRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	PlannedRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Runtime-view rebuild rejects a planned continuation selection whose stored placement kind disagrees with the authored family"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromPlannedSiteRecord(
			WorldBinding,
			PlannedRecord,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason names the inconsistent continuation selection"),
		FailureReason.Contains(TEXT("storedPlacementKind")));
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsPlannedContinuationSiteViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingPlannedContinuation"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingPlannedContinuation"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_PlannedContinuationView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;
	Family.ContinuationPolicy.MaxSlopeBlocks = 5;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 21;

	FPlannedLayoutSiteRecord PlannedRecord;
	PlannedRecord.WorldBindingId = WorldBinding->BindingId;
	PlannedRecord.WorldBindingCandidateId = Candidate.CandidateId;
	PlannedRecord.BiomeRowName = TEXT("Reservation");
	PlannedRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	PlannedRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	PlannedRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 2;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Runtime-view rebuild resolves one planned continuation selection"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromPlannedSiteRecord(
			WorldBinding,
			PlannedRecord,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Planned continuation view keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Planned continuation view keeps the selected candidate id"), ResolvedView.CandidateId, Candidate.CandidateId);
	TestEqual(TEXT("Planned continuation view keeps the selected continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Planned continuation view keeps the selected continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Planned continuation view keeps the resolved continuation-entry level"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 2);
	TestEqual(TEXT("Planned continuation view resolves the bridge placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Planned continuation view resolves the family placement policy"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Planned continuation view reconstructs the authored continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
	TestEqual(TEXT("Planned continuation view rebuild derives the family placement kind from family type instead of the stale nested continuation policy"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Planned continuation view rebuild uses the binding default terrain-search contract"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedContinuationSiteViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedContinuation"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedContinuation"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedContinuationView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 1;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 17;

	FResolvedLayoutSiteRecord SiteRecord;
	SiteRecord.WorldBindingId = WorldBinding->BindingId;
	SiteRecord.WorldBindingCandidateId = Candidate.CandidateId;
	SiteRecord.BiomeRowName = TEXT("Reservation");
	SiteRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	SiteRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	SiteRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved site rebuild resolves one imported continuation selection"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedSiteRecord(
			WorldBinding,
			SiteRecord,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Resolved continuation view keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Resolved continuation view keeps the selected candidate id"), ResolvedView.CandidateId, Candidate.CandidateId);
	TestEqual(TEXT("Resolved continuation view keeps the selected continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Resolved continuation view keeps the selected continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
	TestEqual(TEXT("Resolved continuation view keeps the resolved continuation-entry level"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 1);
	TestEqual(TEXT("Resolved continuation view resolves the tunnel placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
	TestEqual(TEXT("Resolved continuation view resolves the family placement policy"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
	TestEqual(TEXT("Resolved continuation view reconstructs the authored continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
	TestEqual(TEXT("Resolved continuation view rebuild derives the family placement kind from family type instead of the stale nested continuation policy"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
	TestEqual(TEXT("Resolved continuation view rebuild uses the binding default terrain-search contract"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedSiteViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedSite"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedSite"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedSiteView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 1);
	WorldBinding->TemplatePlacementZOffsetBlocks = -3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 30;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = 2.5f;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FResolvedLayoutSiteRecord SiteRecord;
	SiteRecord.WorldBindingId = WorldBinding->BindingId;
	SiteRecord.WorldBindingCandidateId = Candidate.CandidateId;
	SiteRecord.BiomeRowName = TEXT("Reservation");

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved site rebuilds the authored world-binding runtime view"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedSiteRecord(
			WorldBinding,
			SiteRecord,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Resolved site view keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Resolved site view keeps the selected candidate id"), ResolvedView.CandidateId, Candidate.CandidateId);
	TestEqual(TEXT("Resolved site view resolves the ordinary-root placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Resolved site view keeps the binding-owned terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Resolved site view keeps the continuation family unset for ordinary roots"), ResolvedView.ContinuationSelection.FamilyId, NAME_None);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedConnector"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedConnector"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedConnectorView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorBridge;
	Family.ContinuationPolicy.MaxSlopeBlocks = 5;
	Family.SolveBudget.MaxSolveDurationSeconds = 4.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("BridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;
	ConnectorRecord.ContinuationPolicy = Family.ContinuationPolicy;
	ConnectorRecord.SolveBudget = Family.SolveBudget;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved connector rebuild resolves the authored continuation-family runtime view"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Resolved connector view keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Resolved connector view keeps the continuation-family candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Resolved connector view keeps the selected continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Resolved connector view keeps the resolved continuation-entry level"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 1);
	TestEqual(TEXT("Resolved connector view resolves the bridge placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Resolved connector view keeps the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Resolved connector view keeps the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Resolved connector view reconstructs the authored continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
	TestEqual(TEXT("Resolved connector view reconstructs the binding default continuation terrain-search contract"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestEqual(TEXT("Resolved connector view uses the authored family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedConnectorLiveFamily"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedConnectorLiveFamily"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedConnectorLiveFamilyView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveTunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved connector runtime view can re-enter the live authored continuation-family path from the structured carrier when the same-biome family candidate is still known"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Structured-carrier live-family rebuild keeps the live candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Structured-carrier live-family rebuild uses the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Structured-carrier live-family rebuild uses the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Structured-carrier live-family rebuild keeps the live continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Structured-carrier live-family rebuild keeps the live continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
	TestEqual(TEXT("Structured-carrier live-family rebuild uses the binding default terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Structured-carrier live-family rebuild uses the binding default terrain search depth"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestEqual(TEXT("Structured-carrier live-family rebuild uses the live family continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
	TestEqual(TEXT("Structured-carrier live-family rebuild uses the live family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Structured-carrier live-family rebuild backfills the resolved continuation-entry level from the structured carrier"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedBridgeConnectorLiveFamily"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedBridgeConnectorLiveFamily"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedBridgeConnectorLiveFamilyView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveBridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved bridge connector runtime view can re-enter the live authored continuation-family path from the structured carrier when the same-biome family candidate is still known"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild keeps the live candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild uses the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild uses the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild keeps the live continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild keeps the live continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild uses the binding default terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild uses the binding default terrain search depth"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild uses the live family continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild uses the live family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Structured-carrier bridge live-family rebuild backfills the resolved continuation-entry level from the structured carrier"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewWithoutSharedBiomeRowTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedConnectorFallback"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedConnectorFallback"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedConnectorFallbackView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 2;
	Family.SolveBudget.MaxSolveDurationSeconds = 6.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("TunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = NAME_None;
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 2;
	ConnectorRecord.ContinuationPolicy = Family.ContinuationPolicy;
	ConnectorRecord.SolveBudget = Family.SolveBudget;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved connector rebuild can re-enter the live authored continuation-family path without one shared biome row"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestEqual(TEXT("Resolved connector live-family rebuild keeps the binding id"), ResolvedView.BindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Resolved connector live-family rebuild keeps the continuation-family candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
	TestEqual(TEXT("Resolved connector live-family rebuild keeps the resolved continuation-entry level"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 2);
	TestEqual(TEXT("Resolved connector live-family rebuild still resolves the tunnel placement kind"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
	TestEqual(TEXT("Resolved connector live-family rebuild uses the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
	TestEqual(TEXT("Resolved connector live-family rebuild keeps the live family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Resolved connector live-family rebuild reconstructs the authored continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorCarrierWithoutLiveBindingTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedConnectorWithoutLiveBinding"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedConnectorWithoutLiveBinding"),
		{});
	Profile->ContentSet = ContentSet;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = TEXT("ReservationBinding");
	ConnectorRecord.ContinuationFamilyId = TEXT("TunnelFamily");
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("TunnelCandidate");
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = TEXT("TunnelFamily");
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 2;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy = FLayoutWorldBindingContinuationPolicy();
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.ContinuationPolicy.MaxBridgeGapCells = 0;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.FrontendSharedCellSizeInBlocks = FIntVector(2, 2, 2);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved connector rebuild rejects a normalized world-binding carrier when one live world binding UObject is unavailable"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			nullptr,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason names the missing live world binding requirement"),
		FailureReason.Contains(TEXT("requires one live world binding")));
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorCarrierWithoutLiveBindingTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedBridgeConnectorWithoutLiveBinding"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedBridgeConnectorWithoutLiveBinding"),
		{});
	Profile->ContentSet = ContentSet;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = TEXT("ReservationBinding");
	ConnectorRecord.ContinuationFamilyId = TEXT("BridgeFamily");
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("BridgeCandidate");
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = TEXT("BridgeFamily");
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 2;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy = FLayoutWorldBindingContinuationPolicy();
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.ContinuationPolicy.MaxBridgeGapCells = 0;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.FrontendSharedCellSizeInBlocks = FIntVector(2, 2, 2);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved bridge connector rebuild rejects a normalized world-binding carrier when one live world binding UObject is unavailable"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			nullptr,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason names the missing live world binding requirement"),
		FailureReason.Contains(TEXT("requires one live world binding")));
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorCarrierWhenLiveCandidateCannotBeRebuiltTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingResolvedConnectorStaleCandidate"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingResolvedConnectorStaleCandidate"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedConnectorStaleCandidateView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveTunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("StaleTunnelCandidate");
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 2;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	FLayoutResolvedConnectorFrontendSelection FrontendSelection =
		ConnectorRecord.GetResolvedConnectorFrontendSelection();
	FrontendSelection.SharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
	FrontendSelection.TemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
	ConnectorRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved connector rebuild rejects one normalized carrier whose continuation-family candidate cannot be rebuilt from the live binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason reports one strict rejection"),
		!FailureReason.IsEmpty());
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorCarrierWhenLiveCandidateCannotBeRebuiltTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ResolvedBridgeConnectorStaleCandidateView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ResolvedBridgeConnectorStaleCandidateView"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ResolvedBridgeConnectorStaleCandidateView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveBridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("StaleBridgeCandidate");
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 2;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	FLayoutResolvedConnectorFrontendSelection FrontendSelection =
		ConnectorRecord.GetResolvedConnectorFrontendSelection();
	FrontendSelection.SharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
	FrontendSelection.TemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
	ConnectorRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved bridge connector rebuild rejects one normalized carrier whose continuation-family candidate cannot be rebuilt from the live binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason reports one strict rejection"),
		!FailureReason.IsEmpty());
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorThinStoredCarrierWhenLiveCandidateCannotBeRebuiltTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ThinStoredResolvedConnectorStaleCandidateView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ThinStoredResolvedConnectorStaleCandidateView"),
		{});
	UChunkStructureTemplate* ThinStoredTunnelTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_ThinStoredResolvedConnectorStaleCandidateView"),
		FIntVector(2, 2, 2));
	ULayoutModuleAsset* ThinStoredTunnelModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_ThinStoredResolvedConnectorStaleCandidateView"),
		ThinStoredTunnelTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry ThinStoredTunnelEntry;
	ThinStoredTunnelEntry.EntryId = TEXT("ThinStoredTunnelEntry");
	ThinStoredTunnelEntry.ContentKind = ELayoutRegionContentKind::Module;
	ThinStoredTunnelEntry.ModuleSettings.Module = ThinStoredTunnelModule;
	ContentSet->Entries = {ThinStoredTunnelEntry};
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ThinStoredResolvedConnectorStaleCandidateView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveTunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("StaleTunnelCandidate");
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;
	FLayoutResolvedConnectorFrontendSelection FrontendSelection =
		ConnectorRecord.GetResolvedConnectorFrontendSelection();
	FrontendSelection.SharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
	FrontendSelection.TemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
	ConnectorRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved connector rebuild rejects one thin stored normalized carrier whose continuation-family candidate cannot be rebuilt from the live binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason reports one strict rejection"),
		!FailureReason.IsEmpty());
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorThinStoredCarrierWhenLiveCandidateCannotBeRebuiltTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ThinStoredResolvedBridgeConnectorStaleCandidateView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ThinStoredResolvedBridgeConnectorStaleCandidateView"),
		{});
	UChunkStructureTemplate* ThinStoredBridgeTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_ThinStoredResolvedBridgeConnectorStaleCandidateView"),
		FIntVector(2, 2, 2));
	ULayoutModuleAsset* ThinStoredBridgeModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_ThinStoredResolvedBridgeConnectorStaleCandidateView"),
		ThinStoredBridgeTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry ThinStoredBridgeEntry;
	ThinStoredBridgeEntry.EntryId = TEXT("ThinStoredBridgeEntry");
	ThinStoredBridgeEntry.ContentKind = ELayoutRegionContentKind::Module;
	ThinStoredBridgeEntry.ModuleSettings.Module = ThinStoredBridgeModule;
	ContentSet->Entries = {ThinStoredBridgeEntry};
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ThinStoredResolvedBridgeConnectorStaleCandidateView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveBridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("StaleBridgeCandidate");
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;
	FLayoutResolvedConnectorFrontendSelection FrontendSelection =
		ConnectorRecord.GetResolvedConnectorFrontendSelection();
	FrontendSelection.SharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
	FrontendSelection.TemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
	ConnectorRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved bridge connector rebuild rejects one thin stored normalized carrier whose continuation-family candidate cannot be rebuilt from the live binding"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason reports one strict rejection"),
		!FailureReason.IsEmpty());
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewFromThinStoredCarrierWithoutSharedBiomeRowTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ThinStoredResolvedConnectorCrossBiomeView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ThinStoredResolvedConnectorCrossBiomeView"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ThinStoredResolvedConnectorCrossBiomeView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("ReservationA"), TEXT("ReservationB")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("CrossBiomeTunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = NAME_None;
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved connector rebuild rejects one thin stored cross-biome carrier that no longer preserves the normalized continuation placement contract"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Thin stored cross-biome rejection reports one strict failure reason"),
		!FailureReason.IsEmpty());
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierWithoutSharedBiomeRowTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ThinStoredResolvedBridgeConnectorCrossBiomeView"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ThinStoredResolvedBridgeConnectorCrossBiomeView"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ThinStoredResolvedBridgeConnectorCrossBiomeView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("ReservationA"), TEXT("ReservationB")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("CrossBiomeBridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = NAME_None;
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved bridge connector rebuild rejects one thin stored cross-biome carrier that no longer preserves the normalized continuation placement contract"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Thin stored cross-biome bridge rejection reports one strict failure reason"),
		!FailureReason.IsEmpty());
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingThinStoredConnectorLiveFamily"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingThinStoredConnectorLiveFamily"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ThinStoredConnectorLiveFamilyView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("TunnelFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::TunnelContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorTunnel;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveTunnelCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::TunnelContinuation;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved connector rebuild can re-enter the live authored continuation-family path from the thinner stored carrier when the same-biome family candidate is still known"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
		TestEqual(TEXT("Thin stored live-family rebuild keeps the live candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
		TestEqual(TEXT("Thin stored live-family rebuild uses the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
		TestEqual(TEXT("Thin stored live-family rebuild uses the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
		TestEqual(TEXT("Thin stored live-family rebuild keeps the live continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Thin stored live-family rebuild keeps the live continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::TunnelContinuation);
		TestEqual(TEXT("Thin stored live-family rebuild uses the binding default terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
		TestEqual(TEXT("Thin stored live-family rebuild uses the live family continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
		TestEqual(TEXT("Thin stored live-family rebuild uses the live family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
		TestEqual(TEXT("Thin stored live-family rebuild backfills the resolved continuation-entry level from the cached solve-result alignment"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 2);
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingThinStoredBridgeConnectorLiveFamily"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingThinStoredBridgeConnectorLiveFamily"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ThinStoredBridgeConnectorLiveFamilyView"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
	WorldBinding->TemplatePlacementZOffsetBlocks = -5;

	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("BridgeFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.ContinuationPolicy.MaxSlopeBlocks = 9;
	Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("LiveBridgeCandidate");
	FamilyCandidate.LayoutProfile = Profile;
	FamilyCandidate.Weight = 1;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.BiomeRowName = TEXT("Reservation");
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
	ConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Resolved bridge connector rebuild can re-enter the live authored continuation-family path from the thinner stored carrier when the same-biome family candidate is still known"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			WorldBinding,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
		TestEqual(TEXT("Thin stored bridge live-family rebuild keeps the live candidate id"), ResolvedView.CandidateId, FamilyCandidate.CandidateId);
		TestEqual(TEXT("Thin stored bridge live-family rebuild uses the binding-owned shared cell size"), ResolvedView.SharedCellSizeInBlocks, WorldBinding->BaseCellDimensionsBlocks);
		TestEqual(TEXT("Thin stored bridge live-family rebuild uses the binding-owned placement offset"), ResolvedView.TemplatePlacementZOffsetBlocks, WorldBinding->TemplatePlacementZOffsetBlocks);
		TestEqual(TEXT("Thin stored bridge live-family rebuild keeps the live continuation family id"), ResolvedView.ContinuationSelection.FamilyId, Family.FamilyId);
	TestEqual(TEXT("Thin stored bridge live-family rebuild keeps the live continuation placement kind"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::BridgeContinuation);
		TestEqual(TEXT("Thin stored bridge live-family rebuild uses the binding default terrain search start"), ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ, WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
		TestEqual(TEXT("Thin stored bridge live-family rebuild uses the live family continuation slope contract"), ResolvedView.ContinuationPolicy.MaxSlopeBlocks, Family.ContinuationPolicy.MaxSlopeBlocks);
		TestEqual(TEXT("Thin stored bridge live-family rebuild uses the live family solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, Family.SolveBudget.MaxSolveDurationSeconds);
		TestEqual(TEXT("Thin stored bridge live-family rebuild backfills the resolved continuation-entry level from the cached solve-result alignment"), ResolvedView.ContinuationSelection.ResolvedEntryLevel, 2);
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedConnectorThinStoredCarrierWithoutLiveBindingTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingThinStoredConnectorCarrier"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingThinStoredConnectorCarrier"),
		{});
	UChunkStructureTemplate* LegacyThinTunnelTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingThinStoredConnectorCarrier"),
		FIntVector(2, 2, 2));
	ULayoutModuleAsset* LegacyThinTunnelModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingThinStoredConnectorCarrier"),
		LegacyThinTunnelTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry LegacyThinTunnelEntry;
	LegacyThinTunnelEntry.EntryId = TEXT("LegacyThinTunnelEntry");
	LegacyThinTunnelEntry.ContentKind = ELayoutRegionContentKind::Module;
	LegacyThinTunnelEntry.ModuleSettings.Module = LegacyThinTunnelModule;
	ContentSet->Entries = {LegacyThinTunnelEntry};
	Profile->ContentSet = ContentSet;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = TEXT("ReservationBinding");
	ConnectorRecord.ContinuationFamilyId = TEXT("TunnelFamily");
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("TunnelCandidate");
	ConnectorRecord.ContinuationPolicy = FLayoutWorldBindingContinuationPolicy();
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 1;

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved connector rebuild rejects one thin stored normalized carrier when one live world binding UObject is unavailable"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			nullptr,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason names the missing live world binding requirement"),
		FailureReason.Contains(TEXT("requires one live world binding")));
	return true;
}

bool FLayoutWorldBindingRuntimeViewRejectsResolvedBridgeConnectorThinStoredCarrierWithoutLiveBindingTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingThinStoredBridgeConnectorCarrier"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingThinStoredBridgeConnectorCarrier"),
		{});
	UChunkStructureTemplate* LegacyThinBridgeTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingThinStoredBridgeConnectorCarrier"),
		FIntVector(2, 2, 2));
	ULayoutModuleAsset* LegacyThinBridgeModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingThinStoredBridgeConnectorCarrier"),
		LegacyThinBridgeTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry LegacyThinBridgeEntry;
	LegacyThinBridgeEntry.EntryId = TEXT("LegacyThinBridgeEntry");
	LegacyThinBridgeEntry.ContentKind = ELayoutRegionContentKind::Module;
	LegacyThinBridgeEntry.ModuleSettings.Module = LegacyThinBridgeModule;
	ContentSet->Entries = {LegacyThinBridgeEntry};
	Profile->ContentSet = ContentSet;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = TEXT("ReservationBinding");
	ConnectorRecord.ContinuationFamilyId = TEXT("BridgeFamily");
	ConnectorRecord.ContinuationFamilyCandidateId = TEXT("BridgeCandidate");
	ConnectorRecord.ContinuationPolicy = FLayoutWorldBindingContinuationPolicy();
	ConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
	ConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
	ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 1;

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestFalse(
		TEXT("Resolved bridge connector rebuild rejects one thin stored normalized carrier when one live world binding UObject is unavailable"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			nullptr,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
	TestTrue(
		TEXT("Failure reason names the missing live world binding requirement"),
		FailureReason.Contains(TEXT("requires one live world binding")));
	return true;
}

bool FLayoutWorldBindingRuntimeViewBuildsLegacyResolvedConnectorDirectRootViewWithoutFrontendCarrierTest::RunTest(const FString& Parameters)
{
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingLegacyResolvedConnector"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingLegacyResolvedConnector"),
		{});
	UChunkStructureTemplate* LegacyDirectRootTemplate = CreateTemplate(
		GetTransientPackage(),
		TEXT("LayoutTemplate_WorldBindingLegacyResolvedConnector"),
		FIntVector(2, 2, 2));
	ULayoutModuleAsset* LegacyDirectRootModule = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_WorldBindingLegacyResolvedConnector"),
		LegacyDirectRootTemplate,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
	FLayoutRegionContentEntry LegacyDirectRootEntry;
	LegacyDirectRootEntry.EntryId = TEXT("LegacyDirectRootEntry");
	LegacyDirectRootEntry.ContentKind = ELayoutRegionContentKind::Module;
	LegacyDirectRootEntry.ModuleSettings.Module = LegacyDirectRootModule;
	ContentSet->Entries = {LegacyDirectRootEntry};
	Profile->ContentSet = ContentSet;

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
	ConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);

	FLayoutRootSolveBudgetSettings FallbackSolveBudget;
	FallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;

	FLayoutWorldBindingRuntimeView ResolvedView;
	FString FailureReason;
	TestTrue(
		TEXT("Legacy resolved connector records without explicit frontend carrier now rebuild through the shared connector runtime-view seam"),
		LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
			nullptr,
			ConnectorRecord,
			FallbackSolveBudget,
			ResolvedView,
			FailureReason));
		TestEqual(TEXT("Legacy resolved connector runtime view keeps the root placement kind non-world-facing"), ResolvedView.PlacementKind, ELayoutWorldBindingPlacementKind::None);
		TestEqual(TEXT("Legacy resolved connector runtime view keeps the continuation family unset"), ResolvedView.ContinuationSelection.FamilyId, NAME_None);
		TestEqual(TEXT("Legacy resolved connector runtime view keeps the continuation placement kind non-world-facing"), ResolvedView.ContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::None);
		TestEqual(TEXT("Legacy resolved connector runtime view keeps the selected candidate id empty"), ResolvedView.CandidateId, NAME_None);
		TestEqual(TEXT("Legacy resolved connector runtime view keeps the caller solve budget"), ResolvedView.SolveBudget.MaxSolveDurationSeconds, FallbackSolveBudget.MaxSolveDurationSeconds);
		TestEqual(TEXT("Legacy resolved connector runtime view keeps the placement offset at the direct-root default"), ResolvedView.TemplatePlacementZOffsetBlocks, 0);
	return true;
}
