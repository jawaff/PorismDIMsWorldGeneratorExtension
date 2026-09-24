// Copyright 2026 Spotted Loaf Studio

#include <limits>

#include "Layout/Types/LayoutId.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"

#include "Async/Async.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Planning/LayoutNoiseCoordinateLibrary.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Editor.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;

namespace
{
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");
	FLayoutNoiseCoordinateSettings BuildNoiseCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings CoordinateSettings;
		if (WorldGenDef != nullptr)
		{
			CoordinateSettings.BaseBlockSize = WorldGenDef->BaseBlockSize;
			CoordinateSettings.NoiseScale = WorldGenDef->NoiseScale;
			CoordinateSettings.NoiseCoordinateOffset = WorldGenDef->NoiseCoordinateOffset;
		}
		return CoordinateSettings;
	}

	void ConfigurePlanningBiomeRow(UWorldGenDef* WorldGenDef, const FName BiomeRowName)
	{
		check(WorldGenDef != nullptr);
		WorldGenDef->WorldBiomes.Reset();
		WorldGenDef->WorldBiomesDT = nullptr;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef, TEXT("FNE_WorldBindingSitePlannerWorld"));

		FBiomeDualData Row;
		Row.BiomeName = BiomeRowName.ToString();
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef, TEXT("FNE_WorldBindingSitePlanner"));
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
		WorldGenDef->WorldBiomes.Add(Row);
	}

	bool TryResolveExpectedOrdinaryRootSiteCenter(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FName BiomeRowName,
		const FIntPoint& SiteCenterBlockXY,
		const FLayoutTerrainSurfaceSearchSettings& SurfaceSearch,
		const FLayoutNoiseCoordinateSettings& CoordinateSettings,
		const FLayoutActiveBiomeSampler& ActiveBiomeSampler,
		FIntVector& OutSiteCenterBlockWorldPos)
	{
		FLayoutActiveBiomeSurfaceSample Surface;
		if (!ActiveBiomeSampler.FindEligibleBiomeSurface(
				BiomeRowName,
				SiteCenterBlockXY,
				SurfaceSearch.TerrainSearchStartZ,
				SurfaceSearch.TerrainSearchDepthBlocks,
				CoordinateSettings,
				Surface)
			|| !Surface.bIsValid)
		{
			return false;
		}

		OutSiteCenterBlockWorldPos = FIntVector(
			SiteCenterBlockXY.X,
			SiteCenterBlockXY.Y,
			LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(
				WorldBinding,
				Surface.SurfaceBlockWorldPos.Z + 1));
		return true;
	}

	// Exercise the active pocket producer for each authored row, including hidden/nonmatching rows.
	TArray<FPlannedLayoutSiteRecord> BuildPocketRecords(
		const ULayoutWorldBindingAsset* Binding, const FLayoutWorldTestHarness& Harness,
		const FLayoutActiveBiomeSampler& Sampler)
	{
		FLayoutReservationPocket Pocket;
		Pocket.SampleBlockXYs = {FIntPoint(8, 8)};
		TArray<FPlannedLayoutSiteRecord> Records;
		for (const FName Row : Binding->BiomeRowNames)
		{
			const auto Inputs = LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(
				Binding, Row, Harness.World->Seed,
				Binding->DefaultPlacementPolicy.SurfaceSearch,
				BuildNoiseCoordinateSettings(Harness.World->WorldGenDef));
			Records.Append(LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets({Pocket}, Inputs, Sampler));
		}
		return Records;
	}

	bool ContainsPlannedSiteCenter(
		const TArray<FPlannedLayoutSiteRecord>& Records,
		const FIntVector& SiteCenterBlockWorldPos)
	{
		return Records.ContainsByPredicate([&SiteCenterBlockWorldPos](const FPlannedLayoutSiteRecord& Record)
		{
			return Record.GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos
				== SiteCenterBlockWorldPos;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerBuildsPendingSiteRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.BuildsPendingSiteRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerSnapsSiteZToBindingLatticeTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.SnapsSiteZToBindingLattice",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerSnapsSiteZAcrossZeroAnchoredPositiveAndNegativePlanesTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.SnapsSiteZAcrossZeroAnchoredPositiveAndNegativePlanes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerClampsFiniteAxisSiteZToValidLatticePlanesTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.ConstrainsFiniteAxisSiteZToValidLatticePlanes",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerUsesSharedDeterministicCandidateTargetSelectionTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.UsesSharedDeterministicCandidateTargetSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerQualifiesPlanningWindowEnvironmentBeforePublicationTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.QualifiesPlanningWindowEnvironmentBeforePublication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerResolvesObservedChunkFiniteAxisBoundsTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.ResolvesObservedChunkFiniteAxisBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerResolvesCenteredObservedChunkFiniteAxisBoundsTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.ResolvesCenteredObservedChunkFiniteAxisBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerAppliesBindingSiteSpacingAcrossPendingPocketsTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.AppliesBindingSiteSpacingAcrossPendingPockets",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerPocketSiteIdentityTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.PocketSiteIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerPocketWeightedCandidateTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.PocketWeightedCandidate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerPocketEligibleSurfaceTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.PocketEligibleSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingSitePlannerPocketEligibleBindingRowTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.PocketEligibleBindingRow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPlanningFootprintBiomeAdmissionTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.FootprintBiomeAdmission",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningFootprintBiomeAdmissionTest::RunTest(const FString& Parameters)
{
	using LayoutWorldBindingSitePlanner::BuildBoundedNormalCellSiteCenters;
	const FName BindingId(TEXT("SparseSites"));
	const auto NormalCenters = BuildBoundedNormalCellSiteCenters(FIntPoint(0), FIntPoint(15), FIntVector(5), FIntPoint(1), 0, 42, BindingId);
	TestEqual(TEXT("Normal-cell refinement stays bounded"), NormalCenters.Num(), 16);
	TestTrue(TEXT("Refinement includes the viable cell missed by a four-block sample grid"), NormalCenters.Contains(FIntPoint(15, 15)));
	const auto NegativeCenters = BuildBoundedNormalCellSiteCenters(FIntPoint(-16), FIntPoint(-1), FIntVector(5), FIntPoint(1), 0, 42, BindingId);
	TestEqual(TEXT("Negative regions enumerate lattice centers, not truncated aliases"), NegativeCenters.Num(), 9);
	TestTrue(TEXT("Negative lattice starts inside ownership bounds"), NegativeCenters.Contains(FIntPoint(-15, -15)));
	TestEqual(TEXT("Anisotropic cells preserve their separate XY strides"),
		BuildBoundedNormalCellSiteCenters(FIntPoint(0), FIntPoint(15), FIntVector(4, 6, 1), FIntPoint(1), 0, 42, BindingId).Num(), 12);
	TestTrue(TEXT("Oversized regions cannot allocate a whole coarse chunk"),
		BuildBoundedNormalCellSiteCenters(FIntPoint(MIN_int32), FIntPoint(MAX_int32), FIntVector(1), FIntPoint(1), 0, 42, BindingId).IsEmpty());
	TestTrue(TEXT("Pitch one has no jitter room"), NormalCenters == BuildBoundedNormalCellSiteCenters(
		FIntPoint(0), FIntPoint(15), FIntVector(5), FIntPoint(1), 1, 42, BindingId));
	struct FJitterCase { FIntPoint Bucket; float Jitter; };
	for (const auto& Case : TArray<FJitterCase>{{FIntPoint(-1, -1), 0}, {FIntPoint(0, 0), 0.8f}, {FIntPoint(1, -2), 1}})
	{
		const FIntPoint Min(Case.Bucket.X * 15, Case.Bucket.Y * 28), Max = Min + FIntPoint(14, 27);
		const auto Sites = BuildBoundedNormalCellSiteCenters(Min, Max, FIntVector(5, 7, 1), FIntPoint(3, 4), Case.Jitter, 42, BindingId);
		TestEqual(TEXT("Exactly one sparse candidate per whole bucket"), Sites.Num(), 1);
		if (Sites.Num() != 1) continue;
		TestTrue(TEXT("Jitter repeats from stable inputs"), Sites == BuildBoundedNormalCellSiteCenters(
			Min, Max, FIntVector(5, 7, 1), FIntPoint(3, 4), Case.Jitter, 42, BindingId));
		TestTrue(TEXT("Final position is in-bucket and normal-cell aligned"), Sites[0].X >= Min.X && Sites[0].X <= Max.X
			&& Sites[0].Y >= Min.Y && Sites[0].Y <= Max.Y && Sites[0].X % 5 == 0 && Sites[0].Y % 7 == 0);
		if (Case.Jitter == 0) TestEqual(TEXT("Zero jitter chooses central normal cell"), Sites[0], Min + FIntPoint(5, 14));
		const auto Left = BuildBoundedNormalCellSiteCenters(Min, FIntPoint(Min.X + 7, Max.Y),
			FIntVector(5, 7, 1), FIntPoint(3, 4), Case.Jitter, 42, BindingId);
		const auto Right = BuildBoundedNormalCellSiteCenters(FIntPoint(Min.X + 8, Min.Y), Max,
			FIntVector(5, 7, 1), FIntPoint(3, 4), Case.Jitter, 42, BindingId);
		TestEqual(TEXT("Final-position ownership neither loses nor duplicates boundary candidates"), Left.Num() + Right.Num(), 1);
		TestTrue(TEXT("Owner subdivision does not reroll jitter"), Left.Contains(Sites[0]) || Right.Contains(Sites[0]));
	}
	TestEqual(TEXT("Wide bucket arithmetic safely clips extreme coordinates"), BuildBoundedNormalCellSiteCenters(
		FIntPoint(MIN_int32), FIntPoint(MAX_int32), FIntVector(1), FIntPoint(MAX_int32), 0, 42, BindingId).Num(), 4);
	TestTrue(TEXT("Invalid pitch fails closed"), BuildBoundedNormalCellSiteCenters(
		FIntPoint(0), FIntPoint(15), FIntVector(1), FIntPoint(0), 0, 42, BindingId).IsEmpty());
	TestTrue(TEXT("Nonfinite jitter fails closed"), BuildBoundedNormalCellSiteCenters(
		FIntPoint(0), FIntPoint(15), FIntVector(1), FIntPoint(1), std::numeric_limits<float>::quiet_NaN(), 42, BindingId).IsEmpty());
	auto Harness = CreateChunkWorldHarness(GetTransientPackage());
	const FName RowName(TEXT("Reservation"));
	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, RowName);
	Harness.World->WorldGenDef->BaseBlockSize = 1;
	auto& Row = Harness.World->WorldGenDef->WorldBiomes[0];
	// Positive domain only for block X=0..24; both candidate centres are eligible.
	Row.Domain = TEXT("HQAEAAAAgD8AAAAAAAAAAAAAAAAXt1E4AAAAAAAAAAAAAAAAAQQAAACAvwAAAAAAAAAAAAAAAC6QILsAAAAAAAAAAAAAAAA=");
	Row.GenA = TEXT("AAAAAIC/");
	Row.GenARun = nullptr;
	FLayoutActiveBiomeSampler Sampler;
	if (!TestTrue(TEXT("Bounded biome initializes"), Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, 1337))) return false;
	auto* Profile = CreateProfile(GetTransientPackage(), TEXT("FootprintAdmission"), FIntPoint(3), FIntPoint(3), 1, 0, false);
	Profile->ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("FootprintAdmissionContent"), {});
	auto* Binding = NewObject<ULayoutWorldBindingAsset>();
	Binding->BindingId = TEXT("FootprintAdmission");
	Binding->BiomeRowNames = {RowName};
	Binding->BaseCellDimensionsBlocks = FIntVector(5);
	auto& Candidate = Binding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("Root");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;
	FLayoutReservationPocket Pocket;
	Pocket.SampleBlockXYs = {FIntPoint(0, 10), FIntPoint(10, 10)};
	FLayoutTerrainSurfaceSearchSettings Search;
	Search.TerrainSearchStartZ = 3;
	Search.TerrainSearchDepthBlocks = 8;
	const auto Records = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{Pocket},
		LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(Binding, RowName, 99, Search, BuildNoiseCoordinateSettings(Harness.World->WorldGenDef)),
		Sampler);
	if (!TestEqual(TEXT("Planner admits the fitting footprint and rejects an out-of-biome edge site"), Records.Num(), 1)) return false;
	TestEqual(TEXT("Eligible centre with out-of-biome footprint is skipped"),
		Records[0].GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos.X, 10);

	// Two eight-block fragments meet at X=16; neither contains a fifteen-block footprint.
	// Sample their union exactly as canonical discovery does, without native-edge clipping.
	TArray<FLayoutReservationPocketSample> BoundarySamples;
	const auto Coordinates = BuildNoiseCoordinateSettings(Harness.World->WorldGenDef);
	if (!TestTrue(TEXT("Neighboring fragments sample together"), Sampler.SampleEligibleBiomeSurfaceGrid(
		RowName, FIntPoint(8, 15), FIntPoint(23, 15), 1, Search.TerrainSearchStartZ,
		Search.TerrainSearchDepthBlocks, Coordinates, BoundarySamples))) return false;
	auto BoundaryPockets = ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(
		BoundarySamples, FLayoutReservationPocketPlanningSettings());
	if (!TestEqual(TEXT("Native boundary does not split connected biome space"), BoundaryPockets.Num(), 1)) return false;
	TestEqual(TEXT("Both fragments contribute to the pocket"), BoundaryPockets[0].SampleCount, 16);
	// A single core-owned center isolates cross-boundary footprint ownership.
	BoundaryPockets[0].SampleBlockXYs = {FIntPoint(15, 15)};
	auto BoundaryInputs = LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(
		Binding, RowName, 99, Search, Coordinates);
	const auto BoundaryRecords = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		BoundaryPockets, BoundaryInputs, Sampler);
	if (!TestEqual(TEXT("Combined fragments admit the boundary-spanning footprint"), BoundaryRecords.Num(), 1)) return false;
	TestEqual(TEXT("Core center survives neighboring-fragment admission"),
		BoundaryRecords[0].GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos.X, 15);
	const FVector FinalCenter(BoundaryRecords[0].GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos);
	BoundaryInputs.CandidateCenterBoundsInBlocks = FBox(FinalCenter, FinalCenter);
	TestEqual(TEXT("Inclusive final-center ownership preserves identity and does not clip the crossing footprint"),
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(BoundaryPockets, BoundaryInputs, Sampler).Num(), 1);
	auto WrongHeightInputs = BoundaryInputs;
	WrongHeightInputs.CandidateCenterBoundsInBlocks = FBox(FinalCenter + FVector(0, 0, 1), FinalCenter + FVector(0, 0, 2));
	WrongHeightInputs.Candidates[0].FootprintProfile.MinimumFootprintInCells = FIntPoint(6);
	WrongHeightInputs.Candidates[0].FootprintProfile.MaximumFootprintInCells = FIntPoint(6);
	int32 OwnerRejected = 0;
	TestTrue(TEXT("Foreign-height candidate is omitted"), LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		BoundaryPockets, WrongHeightInputs, Sampler, nullptr, nullptr, &OwnerRejected).IsEmpty());
	TestEqual(TEXT("Owner rejection precedes the otherwise failing footprint check"), OwnerRejected, 1);
	BoundaryInputs.CandidateCenterBoundsInBlocks = FBox(ForceInit);
	BoundaryInputs.OccupancyProbability = 0.0f;
	TestTrue(TEXT("Zero occupancy emits no automatic proposals"), LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		BoundaryPockets, BoundaryInputs, Sampler).IsEmpty());
	BoundaryInputs.OccupancyProbability = 1.0f;
	// Both positions fit the biome. Discovery must not erase the second before runtime
	// can reject the first against current coverage, failures or an existing reservation.
	FLayoutReservationPocket Alternatives = BoundaryPockets[0];
	Alternatives.SampleBlockXYs = {FIntPoint(10, 15), FIntPoint(15, 15)};
	auto AlternativeInputs = BoundaryInputs;
	const auto AlternativeRecords = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{Alternatives}, AlternativeInputs, Sampler);
	if (!TestEqual(TEXT("Overlapping unreserved proposals retain both valid alternatives"), AlternativeRecords.Num(), 2)) return false;
	FLayoutRootSpacingReservation ExistingRoot;
	ExistingRoot.BindingId = AlternativeInputs.BindingId;
	ExistingRoot.Min = FIntPoint(0, 0);
	ExistingRoot.Max = FIntPoint(4, 30);
	auto Reservations = MakeShared<TMap<FString, FLayoutRootSpacingReservation>, ESPMode::ThreadSafe>();
	Reservations->Add(TEXT("ExistingRoot"), ExistingRoot);
	AlternativeInputs.RootReservations = Reservations;
	TSet<FString> Blockers;
	const auto Filtered = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{Alternatives}, AlternativeInputs, Sampler, &Blockers);
	if (!TestEqual(TEXT("Captured reservation rejects only the first overlapping alternative"), Filtered.Num(), 1)) return false;
	TestEqual(TEXT("Surviving alternative retains canonical identity"), Filtered[0].StableRecordKey, AlternativeRecords[1].StableRecordKey);
	TestTrue(TEXT("Publication can identify stale reservation evidence"), Blockers.Contains(TEXT("ExistingRoot")));

	BoundaryInputs.Candidates[0].FootprintProfile.MinimumFootprintInCells = FIntPoint(6);
	BoundaryInputs.Candidates[0].FootprintProfile.MaximumFootprintInCells = FIntPoint(6);
	TestTrue(TEXT("Truly insufficient biome width still rejects full footprint"),
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(BoundaryPockets, BoundaryInputs, Sampler).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutPlanningOccupancyTest,
	"PorismExtension.Layout.Planning.WorldBindingSitePlanner.Occupancy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPlanningOccupancyTest::RunTest(const FString& Parameters)
{
	using LayoutWorldBindingSitePlanner::PassesOccupancy;
	ULayoutWorldBindingAsset* Binding = NewObject<ULayoutWorldBindingAsset>();
	TestEqual(TEXT("Bindings default to greedy occupancy"), Binding->OccupancyProbability, 1.0f);
	for (int32 Index = -128; Index < 128; ++Index)
	{
		const FIntVector Site(Index * 5, -Index * 10, 195);
		TestTrue(TEXT("Probability one always passes"), PassesOccupancy(99, TEXT("OccupancyBinding"), Site, 1.0f));
		TestFalse(TEXT("Probability zero never passes"), PassesOccupancy(99, TEXT("OccupancyBinding"), Site, 0.0f));
		const bool Accepted = PassesOccupancy(99, TEXT("OccupancyBinding"), Site, 0.25f);
		TestEqual(TEXT("Repeated site and case aliases reuse the decision"),
			PassesOccupancy(99, TEXT("occupancybinding"), Site, 0.25f), Accepted);
		TestTrue(TEXT("Higher threshold never rejects an already accepted draw"),
			!Accepted || PassesOccupancy(99, TEXT("OccupancyBinding"), Site, 0.75f));
	}
	for (const float Probability : {-0.1f, 1.1f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		Binding->OccupancyProbability = Probability;
		TestFalse(TEXT("Invalid occupancy fails closed"), PassesOccupancy(99, TEXT("OccupancyBinding"), FIntVector::ZeroValue, Probability));
		const auto Validation = Binding->ValidateWorldBinding();
		TestTrue(TEXT("Asset validation reports invalid occupancy"), Validation.Messages.ContainsByPredicate(
			[](const FLayoutValidationMessage& Message) { return Message.Message.Contains(TEXT("OccupancyProbability")); }));
	}
	return true;
}

bool FLayoutWorldBindingSitePlannerBuildsPendingSiteRecordsTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("Reservation"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Active biome sampler initializes from the planning test world"), Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, 1337));

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSitePlanner"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSitePlanner"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_SitePlanner"));
	WorldBinding->BindingId = TEXT("ReservationBinding");
	WorldBinding->BiomeRowNames = {BiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutReservationPocket FirstPocket;
	FirstPocket.CentroidBlockXY = FVector2D(4.2, 7.8);
	FirstPocket.ApproximateInteriorBlockXY = FIntPoint(12, 20);
	FirstPocket.SampleBlockXYs = {FirstPocket.ApproximateInteriorBlockXY};

	FLayoutReservationPocket SecondPocket;
	SecondPocket.CentroidBlockXY = FVector2D(40.0, 44.0);
	SecondPocket.ApproximateInteriorBlockXY = FIntPoint(40, 44);
	SecondPocket.SampleBlockXYs = {SecondPocket.ApproximateInteriorBlockXY};

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 3;
	SurfaceSearch.TerrainSearchDepthBlocks = 8;

	FLayoutNoiseCoordinateSettings CoordinateSettings;
	CoordinateSettings.BaseBlockSize = Harness.World->WorldGenDef->BaseBlockSize;
	CoordinateSettings.NoiseScale = Harness.World->WorldGenDef->NoiseScale;
	CoordinateSettings.NoiseCoordinateOffset = Harness.World->WorldGenDef->NoiseCoordinateOffset;

	const int32 WorldSeed = 99;

	const TArray<FPlannedLayoutSiteRecord> Records = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{FirstPocket, SecondPocket},
		LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(WorldBinding, BiomeRowName, WorldSeed, SurfaceSearch, CoordinateSettings),
		Sampler);

	TestEqual(TEXT("Default occupancy retains both valid pocket proposals"), Records.Num(), 2);
	if (Records.Num() != 2)
	{
		return false;
	}

	FIntVector ExpectedSiteCenterBlockWorldPos;
	if (!TestTrue(
			TEXT("Pending-record planner test can rebuild the same active-biome surface and binding-owned lattice snap as the live helper"),
			TryResolveExpectedOrdinaryRootSiteCenter(
				WorldBinding,
				BiomeRowName,
				FirstPocket.ApproximateInteriorBlockXY,
				SurfaceSearch,
				CoordinateSettings,
				Sampler,
				ExpectedSiteCenterBlockWorldPos)))
	{
		return false;
	}

	const FPlannedLayoutSiteRecord& Record = Records[0];
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
		Record.GetWorldBindingFrontendSelection();
	const FLayoutPlannedSiteReservationSourceSelection ReservationSourceSelection =
		Record.GetPlannedSiteReservationSourceSelection();
	const FLayoutPlannedSiteLifecycleMetadata LifecycleMetadata =
		Record.GetPlannedSiteLifecycleMetadata();
	const FLayoutRootPublicationMetadata PublicationMetadata =
		Record.GetRootPublicationMetadata();
	TestEqual(TEXT("Pending record keeps the world binding id"), FrontendSelection.WorldBindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Pending record keeps the selected candidate id"), FrontendSelection.WorldBindingCandidateId, Candidate.CandidateId);
	TestEqual(TEXT("Ordinary root pending records leave the continuation family id unset"), FrontendSelection.ResolvedContinuationSelection.FamilyId, NAME_None);
	TestEqual(TEXT("Ordinary root pending records leave the continuation placement kind unset"), FrontendSelection.ResolvedContinuationSelection.PlacementKind, ELayoutWorldBindingPlacementKind::None);
	TestEqual(TEXT("Ordinary root pending records leave the continuation-entry level unresolved"), FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel, INDEX_NONE);
	TestEqual(TEXT("Pending record keeps the biome row name"), FrontendSelection.BiomeRowName, BiomeRowName);
	TestTrue(TEXT("Pending record preserves the source world-binding soft pointer"), ReservationSourceSelection.WorldBinding == WorldBinding);
	TestEqual(TEXT("Approximate interior selection mode chooses the interior XY sample and the live binding-owned Z lattice"), ReservationSourceSelection.SiteCenterBlockWorldPos, ExpectedSiteCenterBlockWorldPos);
	TestEqual(TEXT("Pending record uses exact site XY, not a coarse reservation grid"), ReservationSourceSelection.ReservationKey,
		FIntPoint(ExpectedSiteCenterBlockWorldPos.X, ExpectedSiteCenterBlockWorldPos.Y));
	TestEqual(TEXT("Pending record keeps the requested world seed"), ReservationSourceSelection.WorldSeed, WorldSeed);
	TestEqual(
		TEXT("Pending record precomputes the deterministic planning-window lifecycle key"),
		LifecycleMetadata.StableRecordKey,
		ULayoutPlanningWindowStore::MakePlannedLayoutSiteRecordKeyFromSelections(
			FrontendSelection,
			ReservationSourceSelection));
	TestEqual(TEXT("Pending record starts on the pending lifecycle state"), LifecycleMetadata.State, EPlannedLayoutSiteState::Pending);
	TestTrue(TEXT("Pending record starts with no rejection reason"), LifecycleMetadata.RejectionReason.IsEmpty());
	TestEqual(TEXT("Pending record starts with no structured terrain-fit diagnostic"), LifecycleMetadata.TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::None);
	TestEqual(TEXT("Pending record precomputes the planning-window root solve id"), PublicationMetadata.RootSolveId, FLayoutId(*LifecycleMetadata.StableRecordKey));
	TestEqual<FLayoutId>(TEXT("Pending record precomputes the selected root candidate id"), PublicationMetadata.RootCandidateId, Candidate.CandidateId);
	TestEqual<FLayoutId>(TEXT("Pending record precomputes the selected root placement-policy id"), PublicationMetadata.RootPlacementPolicyId, WorldBinding->BindingId);
	TestEqual(
		TEXT("Pending record computes the same deterministic solve seed as the reservation helper"),
		Record.GetSiteSolveSourceSelection().SolveSeed,
		FLayoutSiteReservation::ComputeSiteSolveSeed(ReservationSourceSelection.SiteCenterBlockWorldPos, ReservationSourceSelection.WorldSeed));
	TestTrue(TEXT("Pending record preserves the profile soft pointer"), Record.GetSiteSolveSourceSelection().LayoutProfile == Profile);
	TestTrue(TEXT("Pending record preserves the unified content-set soft pointer"), Record.GetSiteSolveSourceSelection().ContentSet == ContentSet);
	return true;
}

bool FLayoutWorldBindingSitePlannerSnapsSiteZToBindingLatticeTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationLattice"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Active biome sampler initializes from the planning test world"), Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, 7331));

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSitePlannerLattice"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSitePlannerLattice"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_SitePlannerLattice"));
	WorldBinding->BindingId = TEXT("ReservationLatticeBinding");
	WorldBinding->BiomeRowNames = {BiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 5);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;
	(void)Candidate;

	FLayoutReservationPocket Pocket;
	Pocket.CentroidBlockXY = FVector2D(12.0, 20.0);
	Pocket.ApproximateInteriorBlockXY = FIntPoint(12, 20);
	Pocket.SampleBlockXYs = {Pocket.ApproximateInteriorBlockXY};

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 3;
	SurfaceSearch.TerrainSearchDepthBlocks = 8;

	FLayoutNoiseCoordinateSettings CoordinateSettings;
	CoordinateSettings.BaseBlockSize = Harness.World->WorldGenDef->BaseBlockSize;
	CoordinateSettings.NoiseScale = Harness.World->WorldGenDef->NoiseScale;
	CoordinateSettings.NoiseCoordinateOffset = Harness.World->WorldGenDef->NoiseCoordinateOffset;

	const TArray<FPlannedLayoutSiteRecord> Records = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{Pocket},
		LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(WorldBinding, BiomeRowName, 101, SurfaceSearch, CoordinateSettings),
		Sampler);

	TestEqual(TEXT("Planner helper builds one pending record on the binding lattice"), Records.Num(), 1);
	if (Records.Num() != 1)
	{
		return false;
	}

	FIntVector ExpectedSiteCenterBlockWorldPos;
	if (!TestTrue(
			TEXT("Binding-lattice planner test can rebuild the same active-biome surface and binding-owned Z snap as the live helper"),
			TryResolveExpectedOrdinaryRootSiteCenter(
				WorldBinding,
				BiomeRowName,
				Pocket.ApproximateInteriorBlockXY,
				SurfaceSearch,
				CoordinateSettings,
				Sampler,
				ExpectedSiteCenterBlockWorldPos)))
	{
		return false;
	}

	TestEqual(TEXT("Pending record snaps the ordinary-root site Z to the first valid binding lattice plane"), Records[0].GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos, ExpectedSiteCenterBlockWorldPos);
	return true;
}

bool FLayoutWorldBindingSitePlannerSnapsSiteZAcrossZeroAnchoredPositiveAndNegativePlanesTest::RunTest(const FString& Parameters)
{
	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ZeroAnchoredLattice"));
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 5);

	TestEqual(
		TEXT("Zero-anchored binding lattice keeps the origin plane when the terrain-top surface Z already lands on zero"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(WorldBinding, 0),
		0);
	TestEqual(
		TEXT("Zero-anchored binding lattice snaps the first positive terrain-top surface Z up to the first positive plane"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(WorldBinding, 1),
		5);
	TestEqual(
		TEXT("Zero-anchored binding lattice keeps the first positive plane when the terrain-top surface Z already lands on it"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(WorldBinding, 5),
		5);
	TestEqual(
		TEXT("Zero-anchored binding lattice snaps one negative terrain-top surface Z back up to the origin plane"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(WorldBinding, -1),
		0);
	TestEqual(
		TEXT("Zero-anchored binding lattice snaps a lower negative terrain-top surface Z up to the next valid negative plane"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(WorldBinding, -6),
		-5);

	return true;
}

bool FLayoutWorldBindingSitePlannerClampsFiniteAxisSiteZToValidLatticePlanesTest::RunTest(const FString& Parameters)
{
	LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds PositiveBounds;
	PositiveBounds.bHasFiniteZ = true;
	PositiveBounds.MinInclusive.Z = 0;
	PositiveBounds.MaxInclusive.Z = 255;
	int32 PositiveCeilingSiteCenterZ = 0;
	TestFalse(
		TEXT("Finite-axis site Z constraint rejects one zero-anchored lattice snap that would step past a positive single-chunk ceiling"),
		LayoutWorldBindingSitePlanner::TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
			16,
			248,
			PositiveBounds,
			PositiveCeilingSiteCenterZ));

	LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds MixedBounds;
	MixedBounds.bHasFiniteZ = true;
	MixedBounds.MinInclusive.Z = -120;
	MixedBounds.MaxInclusive.Z = 127;
	int32 RaisedFloorSiteCenterZ = 0;
	TestTrue(
		TEXT("Finite-axis site Z constraint raises one below-floor zero-anchored lattice snap back onto the first valid in-bounds plane"),
		LayoutWorldBindingSitePlanner::TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
			16,
			-145,
			MixedBounds,
			RaisedFloorSiteCenterZ));
	TestEqual(
		TEXT("Finite-axis site Z constraint publishes the raised in-bounds floor plane"),
		RaisedFloorSiteCenterZ,
		-112);
	int32 MixedCeilingSiteCenterZ = 0;
	TestFalse(
		TEXT("Finite-axis site Z constraint rejects one zero-anchored lattice snap that would step past a finite mixed-sign ceiling"),
		LayoutWorldBindingSitePlanner::TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
			16,
			121,
			MixedBounds,
			MixedCeilingSiteCenterZ));

	return true;
}

bool FLayoutWorldBindingSitePlannerUsesSharedDeterministicCandidateTargetSelectionTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationWeighted"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Active biome sampler initializes from the weighted-candidate planning test world"), Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, 4551));

	ULayoutProfileAsset* ProfileA = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSitePlannerWeightedA"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSetA = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSitePlannerWeightedA"),
		{});
	ProfileA->ContentSet = ContentSetA;

	ULayoutProfileAsset* ProfileB = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingSitePlannerWeightedB"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSetB = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingSitePlannerWeightedB"),
		{});
	ProfileB->ContentSet = ContentSetB;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_SitePlannerWeighted"));
	WorldBinding->BindingId = TEXT("ReservationWeightedBinding");
	WorldBinding->BiomeRowNames = {BiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);

	FLayoutWorldBindingCandidate& CandidateA = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateA.CandidateId = TEXT("PrimaryCandidateA");
	CandidateA.LayoutProfile = ProfileA;
	CandidateA.Weight = 1;
	const FName CandidateAId = CandidateA.CandidateId;

	FLayoutWorldBindingCandidate& CandidateB = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateB.CandidateId = TEXT("PrimaryCandidateB");
	CandidateB.LayoutProfile = ProfileB;
	CandidateB.Weight = 3;
	const FName CandidateBId = CandidateB.CandidateId;

	FLayoutReservationPocket Pocket;
	Pocket.CentroidBlockXY = FVector2D(4.2, 7.8);
	Pocket.ApproximateInteriorBlockXY = FIntPoint(12, 20);
	Pocket.SampleBlockXYs = {Pocket.ApproximateInteriorBlockXY};

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 3;
	SurfaceSearch.TerrainSearchDepthBlocks = 8;

	FLayoutNoiseCoordinateSettings CoordinateSettings;
	CoordinateSettings.BaseBlockSize = Harness.World->WorldGenDef->BaseBlockSize;
	CoordinateSettings.NoiseScale = Harness.World->WorldGenDef->NoiseScale;
	CoordinateSettings.NoiseCoordinateOffset = Harness.World->WorldGenDef->NoiseCoordinateOffset;

	const int32 WorldSeed = 99;
	const auto Inputs = LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(
		WorldBinding, BiomeRowName, WorldSeed, SurfaceSearch, CoordinateSettings);
	const TArray<FPlannedLayoutSiteRecord> Records = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{Pocket}, Inputs, Sampler);

	TestEqual(TEXT("Planner helper builds one pending record while testing shared candidate selection"), Records.Num(), 1);
	if (Records.Num() != 1)
	{
		return false;
	}

	FIntVector ExpectedSiteCenterBlockWorldPos;
	if (!TestTrue(
			TEXT("Weighted-candidate planner test can rebuild the same active-biome surface and binding-owned lattice snap as the live helper"),
			TryResolveExpectedOrdinaryRootSiteCenter(
				WorldBinding,
				BiomeRowName,
				Pocket.ApproximateInteriorBlockXY,
				SurfaceSearch,
				CoordinateSettings,
				Sampler,
				ExpectedSiteCenterBlockWorldPos)))
	{
		return false;
	}

	const uint32 SelectionSeed = HashCombineFast(
		HashCombineFast(static_cast<uint32>(WorldSeed), GetTypeHash(WorldBinding->BindingId)),
		HashCombineFast(GetTypeHash(ExpectedSiteCenterBlockWorldPos), static_cast<uint32>(WorldBinding->Candidates.Num())));
	const int32 WeightedPick = static_cast<int32>(SelectionSeed % 4u);
	const FName ExpectedCandidateId = WeightedPick < 1 ? CandidateAId : CandidateBId;
	ULayoutProfileAsset* const ExpectedProfile = WeightedPick < 1 ? ProfileA : ProfileB;

	TestEqual(TEXT("Ordinary-root site planning keeps the deterministic weighted candidate selected by the shared target helper"), Records[0].WorldBindingCandidateId, ExpectedCandidateId);
	TestTrue(TEXT("Ordinary-root site planning also keeps the weighted candidate profile resolved through the shared target-based runtime-view rebuild"), Records[0].LayoutProfile == ExpectedProfile);

	WorldBinding->Candidates.Reset();
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(77);
	ProfileA->MinimumFootprintInCells = ProfileA->MaximumFootprintInCells = FIntPoint(20);
	ProfileB->MinimumFootprintInCells = ProfileB->MaximumFootprintInCells = FIntPoint(20);
	bool bRanOffThread = false;
	auto Future = Async(EAsyncExecution::ThreadPool, [Inputs, Pocket, &Sampler, &bRanOffThread]
	{
		bRanOffThread = !IsInGameThread();
		return LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets({Pocket}, Inputs, Sampler);
	});
	const auto CapturedRecords = Future.Get();
	TestTrue(TEXT("Copied location inputs execute off the game thread"), bRanOffThread);
	if (TestEqual(TEXT("Source edits do not change captured candidate production"), CapturedRecords.Num(), Records.Num()))
	{
		TestEqual(TEXT("Captured weighted selection preserves candidate"), CapturedRecords[0].WorldBindingCandidateId, Records[0].WorldBindingCandidateId);
		TestEqual(TEXT("Captured location preserves stable identity"), CapturedRecords[0].StableRecordKey, Records[0].StableRecordKey);
		TestEqual(TEXT("Captured location preserves solve seed"), CapturedRecords[0].SolveSeed, Records[0].SolveSeed);
	}
	return true;
}

bool FLayoutWorldBindingSitePlannerQualifiesPlanningWindowEnvironmentBeforePublicationTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationEnvironment"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Environment-qualified planner creates a live chunk world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}
	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	// X<27: surface Z=0. X>=27: air Z=6..14 beneath roof Z=15..20.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HQAeAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAF7dRuAAAAAABBAAAAIA/AAAAAAAAAAAAAAAAn6stuwAAAAAAAAAAAAAAAAEeAB4AHQAEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAOAtELoAAAAAAQQAAAAAAAAAAAAAAIC/AAAAAAAAAAAAAAAA7Q2+ugAAAAABBAAAAAAAAAAAAAAAgD8AAAAAAAAAAAAAAABLWQa7AAAAAAEEAAAAgL8AAAAAAAAAAAAAAACfqy27AAAAAAAAAAAAAAAA"));
	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Environment-qualified planner initializes biome sampler"),
		Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, 7171));

	auto BuildBinding = [&](const TCHAR* Name, const bool bUnderground)
	{
		ULayoutProfileAsset* const Profile = CreateProfile(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutProfile_%s"), Name),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		Profile->bUndergroundPlacement = bUnderground;
		Profile->ContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutContentSet_%s"), Name),
			{});
		ULayoutWorldBindingAsset* const Binding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutWorldBinding_%s"), Name));
		Binding->BindingId = FName(Name);
		Binding->BiomeRowNames = {BiomeRowName};
		Binding->BaseCellDimensionsBlocks = FIntVector(1, 1, 5);
		FLayoutWorldBindingCandidate& Candidate = Binding->Candidates.AddDefaulted_GetRef();
		Candidate.CandidateId = TEXT("PrimaryCandidate");
		Candidate.LayoutProfile = Profile;
		Candidate.Weight = 1;
		return Binding;
	};

	const FIntPoint SurfaceXY(12, 20);
	const FIntPoint CavityXY(40, 20);
	for (int32 Z = -10; Z <= 51; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(SurfaceXY.X, SurfaceXY.Y, Z), EmptyMaterial, false);
		// Contradict procedural floors and roof; saved edits cannot relocate solve candidates.
		Harness.World->SetBlockValueByBlockWorldPos(
			FIntVector(CavityXY.X, CavityXY.Y, Z), EmptyMaterial, false);
	}

	FLayoutTerrainSurfaceSearchSettings Search;
	Search.TerrainSearchStartZ = 20;
	Search.TerrainSearchDepthBlocks = 31;
	const FLayoutNoiseCoordinateSettings Coordinates = BuildNoiseCoordinateSettings(Harness.World->WorldGenDef);
	auto BuildPocket = [](const FIntPoint XY)
	{
		FLayoutReservationPocket Pocket;
		Pocket.CentroidBlockXY = FVector2D(XY);
		Pocket.ApproximateInteriorBlockXY = XY;
		Pocket.SampleBlockXYs = {XY};
		return Pocket;
	};

	ULayoutWorldBindingAsset* const SurfaceBinding = BuildBinding(TEXT("SurfaceEnvironmentBinding"), false);
	SurfaceBinding->DefaultPlacementPolicy.SurfaceSearch = Search;
	const TArray<FPlannedLayoutSiteRecord> SurfaceRecords =
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
			{BuildPocket(SurfaceXY)},
			LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(SurfaceBinding, BiomeRowName, 7171, Search, Coordinates, 10, Harness.World),
			Sampler);
	TestEqual(TEXT("Surface producer publishes one final-occupancy candidate"), SurfaceRecords.Num(), 1);
	if (SurfaceRecords.Num() == 1)
	{
		TestEqual(TEXT("Surface producer ignores misleading Planning Window reference Z and uses top support"),
			SurfaceRecords[0].SiteCenterBlockWorldPos.Z,
			5);
		TestTrue(TEXT("Surface candidate freezes discovered environment mode"), SurfaceRecords[0].bHasDiscoveredEnvironmentMode);
		TestFalse(TEXT("Surface candidate freezes Surface mode"), SurfaceRecords[0].bDiscoveredUnderground);
		TestTrue(TEXT("Surface candidate records procedural occupancy qualification"), SurfaceRecords[0].bEnvironmentDiscoveryQualifiedProceduralOccupancy);
		FLayoutFrozenTerrainBiomeAdapterInput MatchingEvidence;
		MatchingEvidence.bHasRelativeEnvironmentClassification = true;
		MatchingEvidence.bIsClassifiedUnderground = false;
		FString ValidationFailure;
		TestTrue(TEXT("Matching final-occupancy environment survives prewarm verification"),
			LayoutWorldBindingSitePlanner::ValidateDiscoveredEnvironmentEvidence(
				SurfaceRecords[0], true, MatchingEvidence, ValidationFailure));
		MatchingEvidence.bIsClassifiedUnderground = true;
		TestFalse(TEXT("Changed final-occupancy environment fails prewarm verification"),
			LayoutWorldBindingSitePlanner::ValidateDiscoveredEnvironmentEvidence(
				SurfaceRecords[0], true, MatchingEvidence, ValidationFailure));
		TestTrue(TEXT("Changed environment verification reports expected and observed modes"),
			ValidationFailure.Contains(TEXT("expected=Surface observed=Underground")));
		TestFalse(TEXT("Missing final-occupancy environment evidence fails prewarm verification"),
			LayoutWorldBindingSitePlanner::ValidateDiscoveredEnvironmentEvidence(
				SurfaceRecords[0], false, FLayoutFrozenTerrainBiomeAdapterInput(), ValidationFailure));
	}

	ULayoutWorldBindingAsset* const UndergroundBinding = BuildBinding(TEXT("UndergroundEnvironmentBinding"), true);
	UndergroundBinding->DefaultPlacementPolicy.SurfaceSearch = Search;
	const TArray<FPlannedLayoutSiteRecord> UndergroundRecords =
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
			{BuildPocket(CavityXY)},
			LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(UndergroundBinding, BiomeRowName, 7171, Search, Coordinates, 0, Harness.World),
			Sampler);
	TestEqual(TEXT("Underground producer publishes one enclosed final-occupancy candidate"), UndergroundRecords.Num(), 1);
	if (UndergroundRecords.Num() == 1)
	{
		TestEqual(TEXT("Underground producer ignores misleading Planning Window reference Z and uses cavity floor"),
			UndergroundRecords[0].SiteCenterBlockWorldPos.Z,
			10);
		TestTrue(TEXT("Underground candidate freezes discovered environment mode"), UndergroundRecords[0].bHasDiscoveredEnvironmentMode);
		TestTrue(TEXT("Underground candidate freezes Underground mode"), UndergroundRecords[0].bDiscoveredUnderground);
		TestTrue(TEXT("Underground candidate records procedural occupancy qualification"), UndergroundRecords[0].bEnvironmentDiscoveryQualifiedProceduralOccupancy);
	}

	const TArray<FPlannedLayoutSiteRecord> SurfaceAboveCavityRecords =
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
			{BuildPocket(CavityXY)},
			LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(SurfaceBinding, BiomeRowName, 7171, Search, Coordinates, 10, Harness.World),
			Sampler);
	const TArray<FPlannedLayoutSiteRecord> UndergroundOnSurfaceRecords =
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
			{BuildPocket(SurfaceXY)},
			LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(UndergroundBinding, BiomeRowName, 7171, Search, Coordinates, 0, Harness.World),
			Sampler);
	TestEqual(TEXT("Surface producer relocates a cavity-height reference onto the open roof surface"),
		SurfaceAboveCavityRecords.Num(), 1);
	if (SurfaceAboveCavityRecords.Num() == 1)
	{
		TestEqual(TEXT("Surface producer publishes the roof-support lattice plane"),
			SurfaceAboveCavityRecords[0].SiteCenterBlockWorldPos.Z, 25);
	}
	TestTrue(TEXT("Underground producer does not publish open Surface candidates"), UndergroundOnSurfaceRecords.IsEmpty());

	const TArray<FPlannedLayoutSiteRecord> RepeatedUndergroundRecords =
		LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
			{BuildPocket(CavityXY)},
			LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(UndergroundBinding, BiomeRowName, 7171, Search, Coordinates, 0, Harness.World),
			Sampler);
	TestEqual(TEXT("Repeated Underground discovery preserves candidate count"),
		RepeatedUndergroundRecords.Num(), UndergroundRecords.Num());
	if (RepeatedUndergroundRecords.Num() == 1 && UndergroundRecords.Num() == 1)
	{
		TestEqual(TEXT("Repeated Underground discovery preserves authoritative site"),
			RepeatedUndergroundRecords[0].SiteCenterBlockWorldPos,
			UndergroundRecords[0].SiteCenterBlockWorldPos);
		TestEqual(TEXT("Repeated Underground discovery preserves stable record identity"),
			RepeatedUndergroundRecords[0].StableRecordKey,
			UndergroundRecords[0].StableRecordKey);
	}
	return true;
}

bool FLayoutWorldBindingSitePlannerResolvesObservedChunkFiniteAxisBoundsTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 24, 256));
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	Harness.World->WorldGenDef->AxisBehaviorX = EAxisBehavior::SingleChunk;
	Harness.World->WorldGenDef->AxisBehaviorY = EAxisBehavior::Infinity;
	Harness.World->WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;

	LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
	TestTrue(
		TEXT("Observed-chunk lattice resolves finite axis bounds from the shared worldgen scale context"),
		LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
			Harness.World,
			Bounds));
	TestTrue(TEXT("Observed-chunk lattice marks X as finite when the world uses one X chunk"), Bounds.bHasFiniteX);
	TestFalse(TEXT("Observed-chunk lattice leaves Y unbounded when the world uses infinite Y"), Bounds.bHasFiniteY);
	TestTrue(TEXT("Observed-chunk lattice marks Z as finite when the world uses one Z chunk"), Bounds.bHasFiniteZ);
	TestEqual(TEXT("Observed-chunk lattice resolves the raw minimum X block"), Bounds.MinInclusive.X, 0);
	TestEqual(TEXT("Observed-chunk lattice resolves the raw maximum X block"), Bounds.MaxInclusive.X, 15);
	TestEqual(TEXT("Observed-chunk lattice resolves the raw minimum Z block"), Bounds.MinInclusive.Z, 0);
	TestEqual(TEXT("Observed-chunk lattice resolves the raw maximum Z block"), Bounds.MaxInclusive.Z, 255);
	TestTrue(
		TEXT("Observed-chunk lattice accepts one block-world site center inside the finite X/Z chunk span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(8, 1000, 128)));
	TestFalse(
		TEXT("Observed-chunk lattice rejects one block-world site center beyond the finite X chunk span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(24, 1000, 128)));
	TestFalse(
		TEXT("Observed-chunk lattice rejects one block-world site center beyond the finite Z chunk span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(8, 1000, 256)));
	return true;
}

bool FLayoutWorldBindingSitePlannerResolvesCenteredObservedChunkFiniteAxisBoundsTest::RunTest(const FString& Parameters)
{
	UWorld* const EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull(TEXT("Centered finite-axis bounds test can resolve the editor world"), EditorWorld);
	if (EditorWorld == nullptr)
	{
		return false;
	}

	AChunkWorldExtended* const ChunkWorld = EditorWorld->SpawnActor<AChunkWorldExtended>();
	TestNotNull(TEXT("Centered finite-axis bounds test spawns a chunk world"), ChunkWorld);
	if (ChunkWorld == nullptr)
	{
		return false;
	}

	ChunkWorld->WorldGenDef = NewObject<UWorldGenDef>(ChunkWorld);
	ChunkWorld->WorldGenDefPredefined = true;
	ChunkWorld->WorldGenDef->BaseBlockSize = 1;
	ChunkWorld->WorldGenDef->NoiseScale = FVector::OneVector;
	ChunkWorld->WorldGenDef->ChunkBlockSize = FIntVector(16, 24, 256);
	ChunkWorld->WorldGenDef->AxisBehaviorX = EAxisBehavior::SingleChunk;
	ChunkWorld->WorldGenDef->AxisBehaviorY = EAxisBehavior::Infinity;
	ChunkWorld->WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
	ChunkWorld->WorldGenDef->NoiseCoordinateOffset = FIntVector(-8, 0, -128);
	FChunkDataParams ChunkParams;
	ChunkParams.BlockSizeMulti = 1.0;
	ChunkParams.ChunkSizeMulti = FVector(1.0, 1.0, 1.0);
	ChunkWorld->WorldGenDef->WorldChunks.Add(ChunkParams);

	FWorldGenScaleContextResolver::ClearCache();
	FWorldGenScaleSettings ScaleSettings;
	ScaleSettings.BaseBlockSize = 1;
	ScaleSettings.NoiseScale = FVector::OneVector;
	ScaleSettings.NoiseCoordinateOffset = FIntVector(-8, 0, -128);
	ScaleSettings.AxisBehaviorX = EAxisBehavior::SingleChunk;
	ScaleSettings.AxisBehaviorY = EAxisBehavior::Infinity;
	ScaleSettings.AxisBehaviorZ = EAxisBehavior::SingleChunk;
	ScaleSettings.FallbackFiniteAxisBlockSpan = FIntVector(16, 24, 256);
	const FResolvedWorldGenScaleContext CenteredContext =
		FWorldGenScaleContextResolver::Resolve(nullptr, &ScaleSettings, true);
	TestEqual(TEXT("Centered finite-axis scale context keeps the authored minimum X block around zero"), static_cast<float>(CenteredContext.AuthoredMinBlock.X), -8.0f);
	TestEqual(TEXT("Centered finite-axis scale context keeps the authored maximum X block around zero"), static_cast<float>(CenteredContext.AuthoredMaxBlock.X), 8.0f);
	TestEqual(TEXT("Centered finite-axis scale context keeps the authored minimum Z block around zero"), static_cast<float>(CenteredContext.AuthoredMinBlock.Z), -128.0f);
	TestEqual(TEXT("Centered finite-axis scale context keeps the authored maximum Z block around zero"), static_cast<float>(CenteredContext.AuthoredMaxBlock.Z), 128.0f);

	LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
	TestTrue(
		TEXT("Observed-chunk lattice resolves raw finite axis bounds from the same centered worldgen scale context"),
		LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
			ChunkWorld,
			Bounds));
	TestTrue(TEXT("Observed-chunk lattice marks X as finite when the centered world uses one X chunk"), Bounds.bHasFiniteX);
	TestFalse(TEXT("Observed-chunk lattice leaves Y unbounded when the centered world uses infinite Y"), Bounds.bHasFiniteY);
	TestTrue(TEXT("Observed-chunk lattice marks Z as finite when the centered world uses one Z chunk"), Bounds.bHasFiniteZ);
	TestEqual(TEXT("Observed-chunk lattice still resolves the raw minimum X block"), Bounds.MinInclusive.X, 0);
	TestEqual(TEXT("Observed-chunk lattice still resolves the raw maximum X block"), Bounds.MaxInclusive.X, 15);
	TestEqual(TEXT("Observed-chunk lattice still resolves the raw minimum Z block"), Bounds.MinInclusive.Z, 0);
	TestEqual(TEXT("Observed-chunk lattice still resolves the raw maximum Z block"), Bounds.MaxInclusive.Z, 255);
	TestTrue(
		TEXT("Observed-chunk lattice accepts one raw block-world site center inside the finite X/Z span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(8, 1000, 128)));
	TestFalse(
		TEXT("Observed-chunk lattice rejects one raw block-world site center beyond the finite X chunk span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(16, 1000, 128)));
	TestFalse(
		TEXT("Observed-chunk lattice rejects one raw block-world site center beyond the finite positive Z chunk span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(8, 1000, 256)));
	TestFalse(
		TEXT("Observed-chunk lattice rejects one raw block-world site center below the finite Z chunk span"),
		LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
			Bounds,
			FIntVector(8, 1000, -1)));
	ChunkWorld->Destroy();
	return true;
}

bool FLayoutWorldBindingSitePlannerAppliesBindingSiteSpacingAcrossPendingPocketsTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationPendingSpacing"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Active biome sampler initializes from the pending-spacing planning test world"), Sampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, 4551));

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_WorldBindingPendingSpacing"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_WorldBindingPendingSpacing"),
		{});
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_PendingSpacing"));
	WorldBinding->BindingId = TEXT("ReservationPendingSpacingBinding");
	// One-block roots require 31 free normal cells; the 16-block key grid only supplies identity.
	WorldBinding->MinimumRootGapCells = 31;
	WorldBinding->BiomeRowNames = {BiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutReservationPocket FirstPocket;
	FirstPocket.CentroidBlockXY = FVector2D(0.0, 0.0);
	FirstPocket.ApproximateInteriorBlockXY = FIntPoint(0, 0);
	FirstPocket.SampleBlockXYs = {FirstPocket.ApproximateInteriorBlockXY};

	FLayoutReservationPocket SecondPocket;
	SecondPocket.CentroidBlockXY = FVector2D(16.0, 0.0);
	SecondPocket.ApproximateInteriorBlockXY = FIntPoint(16, 0);
	SecondPocket.SampleBlockXYs = {SecondPocket.ApproximateInteriorBlockXY};

	FLayoutReservationPocket ThirdPocket;
	ThirdPocket.CentroidBlockXY = FVector2D(64.0, 64.0);
	ThirdPocket.ApproximateInteriorBlockXY = FIntPoint(64, 64);
	ThirdPocket.SampleBlockXYs = {ThirdPocket.ApproximateInteriorBlockXY};

	FLayoutTerrainSurfaceSearchSettings SurfaceSearch;
	SurfaceSearch.TerrainSearchStartZ = 3;
	SurfaceSearch.TerrainSearchDepthBlocks = 8;

	FLayoutNoiseCoordinateSettings CoordinateSettings;
	CoordinateSettings.BaseBlockSize = Harness.World->WorldGenDef->BaseBlockSize;
	CoordinateSettings.NoiseScale = Harness.World->WorldGenDef->NoiseScale;
	CoordinateSettings.NoiseCoordinateOffset = Harness.World->WorldGenDef->NoiseCoordinateOffset;

	const TArray<FPlannedLayoutSiteRecord> Records = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{FirstPocket, SecondPocket, ThirdPocket},
		LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(WorldBinding, BiomeRowName, 99, SurfaceSearch, CoordinateSettings),
		Sampler);

	TestEqual(TEXT("Unreserved pocket proposals cannot exclude one another"), Records.Num(), 3);
	if (Records.Num() != 3)
	{
		return false;
	}

	FIntVector FirstExpectedSiteCenterBlockWorldPos;
	FIntVector SecondExpectedSiteCenterBlockWorldPos;
	FIntVector ThirdExpectedSiteCenterBlockWorldPos;
	if (!TestTrue(
			TEXT("Pending-spacing planner test can rebuild the first pocket site center from the live surface and lattice contract"),
			TryResolveExpectedOrdinaryRootSiteCenter(
				WorldBinding,
				BiomeRowName,
				FirstPocket.ApproximateInteriorBlockXY,
				SurfaceSearch,
				CoordinateSettings,
				Sampler,
				FirstExpectedSiteCenterBlockWorldPos))
		|| !TestTrue(
			TEXT("Pending-spacing planner test can rebuild the second pocket site center from the live surface and lattice contract"),
			TryResolveExpectedOrdinaryRootSiteCenter(
				WorldBinding,
				BiomeRowName,
				SecondPocket.ApproximateInteriorBlockXY,
				SurfaceSearch,
				CoordinateSettings,
				Sampler,
				SecondExpectedSiteCenterBlockWorldPos))
		|| !TestTrue(
			TEXT("Pending-spacing planner test can rebuild the third pocket site center from the live surface and lattice contract"),
			TryResolveExpectedOrdinaryRootSiteCenter(
				WorldBinding,
				BiomeRowName,
				ThirdPocket.ApproximateInteriorBlockXY,
				SurfaceSearch,
				CoordinateSettings,
				Sampler,
				ThirdExpectedSiteCenterBlockWorldPos)))
	{
		return false;
	}

	const bool bKeptOneNearbyPocket =
		ContainsPlannedSiteCenter(Records, FirstExpectedSiteCenterBlockWorldPos)
		|| ContainsPlannedSiteCenter(Records, SecondExpectedSiteCenterBlockWorldPos);
	const bool bKeptDistantPocket =
		ContainsPlannedSiteCenter(Records, ThirdExpectedSiteCenterBlockWorldPos);
	const bool bKeptBothNearbyPockets =
		ContainsPlannedSiteCenter(Records, FirstExpectedSiteCenterBlockWorldPos)
		&& ContainsPlannedSiteCenter(Records, SecondExpectedSiteCenterBlockWorldPos);

	TestTrue(TEXT("Pending-pocket site planning keeps one member of the nearby pocket pair"), bKeptOneNearbyPocket);
	TestTrue(TEXT("Pending-pocket site planning keeps the materially distant pocket"), bKeptDistantPocket);
	TestTrue(TEXT("Nearby alternatives survive until an actual reservation exists"), bKeptBothNearbyPockets);
	auto ReservedInputs = LayoutWorldBindingSitePlanner::CaptureSitePlanningInputs(WorldBinding, BiomeRowName, 99, SurfaceSearch, CoordinateSettings);
	FLayoutRootSpacingReservation Reservation;
	TestTrue(TEXT("Captured first root uses authored maximum envelope"), FLayoutRootSpacingReservation::TryBuild(
		ReservedInputs.BindingId, FirstExpectedSiteCenterBlockWorldPos, Profile->MaximumFootprintInCells,
		WorldBinding->BaseCellDimensionsBlocks, Reservation));
	auto Reservations = MakeShared<TMap<FString, FLayoutRootSpacingReservation>, ESPMode::ThreadSafe>();
	Reservations->Add(TEXT("ReservedFirstRoot"), Reservation);
	ReservedInputs.RootReservations = Reservations;
	const auto Remaining = LayoutWorldBindingSitePlanner::BuildPendingSiteRecordsFromPockets(
		{FirstPocket, SecondPocket, ThirdPocket}, ReservedInputs, Sampler);
	TestEqual(TEXT("Actual reservation suppresses its site and the too-close alternative"), Remaining.Num(), 1);
	TestTrue(TEXT("Reservation preserves the distant site"), ContainsPlannedSiteCenter(Remaining, ThirdExpectedSiteCenterBlockWorldPos));
	return true;
}

bool FLayoutWorldBindingSitePlannerPocketSiteIdentityTest::RunTest(const FString& Parameters)
	{
		const FName BiomeRowName(TEXT("ObservedChunkSite"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ObservedChunkSite"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ObservedChunkSite"),
		{});
	Profile->ContentSet = ContentSet;
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 3));

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ObservedChunkSite"));
	WorldBinding->BindingId = TEXT("ObservedChunkSiteBinding");
	WorldBinding->BiomeRowNames = {BiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 8;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

		FLayoutActiveBiomeSampler ActiveBiomeSampler;
		if (!TestTrue(
				TEXT("Observed-chunk site discovery initializes the active biome sampler"),
				ActiveBiomeSampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
		{
			return false;
		}

	const auto Records = BuildPocketRecords(WorldBinding, Harness, ActiveBiomeSampler);
	if (!TestEqual(TEXT("Pocket producer publishes one candidate"), Records.Num(), 1)) return false;
	const auto DiscoveredSite = Records[0].GetPlannedSiteReservationSourceSelection();
	const auto Frontend = Records[0].GetWorldBindingFrontendSelection();
	TestEqual(TEXT("Pocket producer keeps the matching biome row"), Frontend.BiomeRowName, BiomeRowName);
	FLayoutActiveBiomeSurfaceSample EligibleSurface;
	if (!TestTrue(
			TEXT("Observed-chunk site discovery can rebuild the same active-biome surface the live site-center snap uses"),
			ActiveBiomeSampler.FindEligibleBiomeSurfaceFromAnyRow(
				WorldBinding->BiomeRowNames,
				FIntPoint(8, 8),
				WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
				WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
				BuildNoiseCoordinateSettings(Harness.World->WorldGenDef),
				EligibleSurface)))
	{
		return false;
	}

	const FIntVector ExpectedSiteCenterBlockWorldPos(
		8,
		8,
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(
			WorldBinding,
			EligibleSurface.SurfaceBlockWorldPos.Z + 1));
	TestEqual(TEXT("Observed-chunk site discovery snaps the site center onto the binding-owned lattice"), DiscoveredSite.SiteCenterBlockWorldPos, ExpectedSiteCenterBlockWorldPos);
	TestEqual(TEXT("Pocket producer preserves exact site XY identity"), DiscoveredSite.ReservationKey, FIntPoint(8, 8));
	TestNotEqual(TEXT("Adjacent sites do not collapse into a coarse reservation cell"),
		FLayoutSiteReservation::ComputeReservationKey(FIntVector(8, 8, 0)),
		FLayoutSiteReservation::ComputeReservationKey(FIntVector(9, 8, 0)));
	TestEqual(TEXT("Negative site XY remains exact"),
		FLayoutSiteReservation::ComputeReservationKey(FIntVector(-8, -9, 0)), FIntPoint(-8, -9));
	TestEqual(
		TEXT("Observed-chunk site discovery computes the same deterministic solve seed as the shared reservation helper"),
		Records[0].GetSiteSolveSourceSelection().SolveSeed,
		FLayoutSiteReservation::ComputeSiteSolveSeed(DiscoveredSite.SiteCenterBlockWorldPos, Harness.World->Seed));
	TestEqual(TEXT("Observed-chunk site discovery keeps the authored binding id on the resolved runtime view"), Frontend.WorldBindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Observed-chunk site discovery keeps the authored candidate id on the resolved runtime view"), Frontend.WorldBindingCandidateId, Candidate.CandidateId);
	return true;
}

bool FLayoutWorldBindingSitePlannerPocketWeightedCandidateTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ObservedChunkWeightedSite"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);

	ULayoutProfileAsset* ProfileA = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ObservedChunkWeightedSiteA"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSetA = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ObservedChunkWeightedSiteA"),
		{});
	ProfileA->ContentSet = ContentSetA;

	ULayoutProfileAsset* ProfileB = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ObservedChunkWeightedSiteB"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSetB = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ObservedChunkWeightedSiteB"),
		{});
	ProfileB->ContentSet = ContentSetB;

	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 3));

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ObservedChunkWeightedSite"));
	WorldBinding->BindingId = TEXT("ObservedChunkWeightedSiteBinding");
	WorldBinding->BiomeRowNames = {BiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 8;

	FLayoutWorldBindingCandidate& CandidateA = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateA.CandidateId = TEXT("ObservedChunkCandidateA");
	CandidateA.LayoutProfile = ProfileA;
	CandidateA.Weight = 1;
	const FName CandidateAId = CandidateA.CandidateId;

	FLayoutWorldBindingCandidate& CandidateB = WorldBinding->Candidates.AddDefaulted_GetRef();
	CandidateB.CandidateId = TEXT("ObservedChunkCandidateB");
	CandidateB.LayoutProfile = ProfileB;
	CandidateB.Weight = 3;
	const FName CandidateBId = CandidateB.CandidateId;

	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	if (!TestTrue(
			TEXT("Pocket weighted selection initializes the active biome sampler"),
			ActiveBiomeSampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
	{
		return false;
	}

	const auto Records = BuildPocketRecords(WorldBinding, Harness, ActiveBiomeSampler);
	if (!TestEqual(TEXT("Pocket producer publishes one weighted candidate"), Records.Num(), 1)) return false;
	const auto DiscoveredSite = Records[0].GetPlannedSiteReservationSourceSelection();
	const auto Frontend = Records[0].GetWorldBindingFrontendSelection();
	const uint32 SelectionSeed = HashCombineFast(
		HashCombineFast(static_cast<uint32>(Harness.World->Seed), GetTypeHash(WorldBinding->BindingId)),
		HashCombineFast(GetTypeHash(DiscoveredSite.SiteCenterBlockWorldPos), static_cast<uint32>(WorldBinding->Candidates.Num())));
	const int32 WeightedPick = static_cast<int32>(SelectionSeed % 4u);
	const FName ExpectedCandidateId = WeightedPick < 1 ? CandidateAId : CandidateBId;
	ULayoutProfileAsset* const ExpectedProfile = WeightedPick < 1 ? ProfileA : ProfileB;

	TestEqual(TEXT("Observed-chunk weighted site discovery keeps the deterministic weighted candidate id on the resolved runtime view"), Frontend.WorldBindingCandidateId, ExpectedCandidateId);
	TestTrue(TEXT("Observed-chunk weighted site discovery also keeps the selected weighted candidate profile"), Records[0].GetSiteSolveSourceSelection().LayoutProfile == ExpectedProfile);
	return true;
}

bool FLayoutWorldBindingSitePlannerPocketEligibleSurfaceTest::RunTest(const FString& Parameters)
{
	const FName StoredSurfaceBiomeRowName(TEXT("ObservedChunkStoredSurfaceRow"));
	const FName EligibleBiomeRowName(TEXT("ObservedChunkEligibleSurfaceRow"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), EligibleBiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	Harness.World->WorldGenDef->WorldBiomes.Reset();
	Harness.World->WorldGenDef->WorldBiomesDT = nullptr;
	Harness.World->WorldGenDef->NoiseScale = FVector::OneVector;
	Harness.World->WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
	Harness.World->WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);

	FBiomeDualData HiddenStoredSurfaceRow;
	HiddenStoredSurfaceRow.BiomeName = StoredSurfaceBiomeRowName.ToString();
	HiddenStoredSurfaceRow.BiomeHidden = true;
	Harness.World->WorldGenDef->WorldBiomes.Add(HiddenStoredSurfaceRow);

	FBiomeDualData EligibleRow;
	EligibleRow.BiomeName = EligibleBiomeRowName.ToString();
	EligibleRow.Domain = ConstantPositiveFastNoise;
	EligibleRow.DualSwitch = ConstantPositiveFastNoise;
	EligibleRow.GenARun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef, TEXT("FNE_ObservedChunkEligibleSurface"));
	EligibleRow.DomainOver = 1.0f;
	EligibleRow.GenU_Mat1.AddDefaulted();
	Harness.World->WorldGenDef->WorldBiomes.Add(EligibleRow);

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ObservedChunkEligibleSurface"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ObservedChunkEligibleSurface"),
		{});
	Profile->ContentSet = ContentSet;
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 3));

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ObservedChunkEligibleSurface"));
	WorldBinding->BindingId = TEXT("ObservedChunkEligibleSurfaceBinding");
	WorldBinding->BiomeRowNames = {EligibleBiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 8;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	if (!TestTrue(
			TEXT("Pocket eligible-surface selection initializes the active biome sampler"),
			ActiveBiomeSampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
	{
		return false;
	}

	const auto Records = BuildPocketRecords(WorldBinding, Harness, ActiveBiomeSampler);
	if (!TestEqual(TEXT("Pocket producer ignores hidden stored-surface row"), Records.Num(), 1)) return false;
	const auto Frontend = Records[0].GetWorldBindingFrontendSelection();
	TestEqual(TEXT("Pocket producer preserves eligible procedural row"), Frontend.BiomeRowName, EligibleBiomeRowName);
	TestEqual(TEXT("Pocket producer preserves candidate identity"), Frontend.WorldBindingCandidateId, Candidate.CandidateId);
	return true;
}

bool FLayoutWorldBindingSitePlannerPocketEligibleBindingRowTest::RunTest(const FString& Parameters)
{
	const FName StoredSurfaceBiomeRowName(TEXT("ObservedChunkStoredSurfaceRow"));
	const FName InactiveBindingBiomeRowName(TEXT("ObservedChunkInactiveBindingRow"));
	const FName EligibleBindingBiomeRowName(TEXT("ObservedChunkEligibleBindingRow"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), EligibleBindingBiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a world"), Harness.World);
	if (Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	Harness.World->WorldGenDef->WorldBiomes.Reset();
	Harness.World->WorldGenDef->WorldBiomesDT = nullptr;
	Harness.World->WorldGenDef->NoiseScale = FVector::OneVector;
	Harness.World->WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
	Harness.World->WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);

	FBiomeDualData HiddenStoredSurfaceRow;
	HiddenStoredSurfaceRow.BiomeName = StoredSurfaceBiomeRowName.ToString();
	HiddenStoredSurfaceRow.BiomeHidden = true;
	Harness.World->WorldGenDef->WorldBiomes.Add(HiddenStoredSurfaceRow);

	FBiomeDualData InactiveBindingRow;
	InactiveBindingRow.BiomeName = InactiveBindingBiomeRowName.ToString();
	InactiveBindingRow.BiomeHidden = true;
	Harness.World->WorldGenDef->WorldBiomes.Add(InactiveBindingRow);

	FBiomeDualData EligibleBindingRow;
	EligibleBindingRow.BiomeName = EligibleBindingBiomeRowName.ToString();
	EligibleBindingRow.Domain = ConstantPositiveFastNoise;
	EligibleBindingRow.DualSwitch = ConstantPositiveFastNoise;
	EligibleBindingRow.GenARun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef, TEXT("FNE_ObservedChunkEligibleBindingSurface"));
	EligibleBindingRow.DomainOver = 1.0f;
	EligibleBindingRow.GenU_Mat1.AddDefaulted();
	Harness.World->WorldGenDef->WorldBiomes.Add(EligibleBindingRow);

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_ObservedChunkAnyEligibleBindingRow"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_ObservedChunkAnyEligibleBindingRow"),
		{});
	Profile->ContentSet = ContentSet;
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 3));

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_ObservedChunkAnyEligibleBindingRow"));
	WorldBinding->BindingId = TEXT("ObservedChunkAnyEligibleBindingRowBinding");
	WorldBinding->BiomeRowNames = {InactiveBindingBiomeRowName, EligibleBindingBiomeRowName};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(1, 1, 1);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 3;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 8;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PrimaryCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	if (!TestTrue(
			TEXT("Pocket multi-row selection initializes the active biome sampler"),
			ActiveBiomeSampler.Initialize(GetTransientPackage(), Harness.World->WorldGenDef, Harness.World->Seed)))
	{
		return false;
	}

	const auto Records = BuildPocketRecords(WorldBinding, Harness, ActiveBiomeSampler);
	if (!TestEqual(TEXT("Pocket producer skips inactive authored row and finds eligible row"), Records.Num(), 1)) return false;
	const auto Frontend = Records[0].GetWorldBindingFrontendSelection();
	TestEqual(TEXT("Pocket producer preserves eligible authored row"), Frontend.BiomeRowName, EligibleBindingBiomeRowName);
	TestEqual(TEXT("Pocket producer preserves candidate identity across rows"), Frontend.WorldBindingCandidateId, Candidate.CandidateId);
	return true;
}
