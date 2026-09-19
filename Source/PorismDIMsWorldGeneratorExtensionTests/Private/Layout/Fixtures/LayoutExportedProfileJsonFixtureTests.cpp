// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"

#include "ChunkWorldStructs/ChunkWorldEnums.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutFakeSteppedTerrainTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Biome/Noise/Foundation/FoundationTerrainProfilePayloads.h"
#include "Biome/Noise/Foundation/InfinitePlaneFoundationPayloads.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Biome/Noise/WorldGenScaleContext.h"
#include "Biome/Types/BiomeGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "Layout/Solver/LayoutSolveDiagnostics.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Contracts/LayoutContractPipeline.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutProfileSolverInternal.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

using namespace PorismLayoutWorldTestUtilities;

namespace
{
	const FString CoordinatorFallbackMessageSubstring =
		TEXT("fell back to the legacy recursive body after coordinator failure");

	UObject* CreateFixtureTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	bool LoadFixtureAssets(
		FAutomationTestBase& Test,
		const FString& FixturePath,
		const TCHAR* OuterName,
		FLayoutProfileJsonFixtureAssets& OutAssets,
		const bool bUseStableOuter = false)
	{
		FString Json;
		Test.TestTrue(TEXT("Fixture file exists"), FPaths::FileExists(FixturePath));
		Test.TestTrue(TEXT("Fixture loads"), FFileHelper::LoadFileToString(Json, *FixturePath));
		if (Json.IsEmpty())
		{
			return false;
		}

		UObject* Outer = bUseStableOuter
			? CreatePackage(*FString::Printf(TEXT("/Temp/%s"), OuterName))
			: CreateFixtureTestOuter(OuterName);
		TArray<FString> ImportIssues;
		Test.TestTrue(TEXT("Fixture imports"), FLayoutProfileJsonFixture::ImportFromString(Json, Outer, OutAssets, ImportIssues));
		if (!Json.Contains(TEXT("\"enable_terrain_seams\"")))
		{
			Test.TestTrue(TEXT("Imported profile defaults terrain seams on when fixture omits new field"), OutAssets.Profile == nullptr || OutAssets.Profile->bEnableTerrainSeams);
		}
		if (!Test.TestEqual(TEXT("Fixture imports without issues"), ImportIssues.Num(), 0))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
		}

		return OutAssets.Profile != nullptr;
	}

	bool BuildImportedFixtureRootRequest(
		FAutomationTestBase& Test,
		const FLayoutProfileJsonFixtureAssets& ImportedAssets,
		const int32 Seed,
		const FString& RootRegionPath,
		FLayoutRegionSolveRequest& OutRequest)
	{
		FString RequestError;
		const bool bBuilt = FLayoutProfileJsonFixture::BuildImportedRootRequest(
			ImportedAssets,
			Seed,
			RootRegionPath,
			OutRequest,
			FLayoutRootSolveBudgetSettings(),
			NAME_None,
			NAME_None,
			0,
			&RequestError);
		if (!Test.TestTrue(
				*FString::Printf(TEXT("Imported fixture root request builds for %s"), *RootRegionPath),
				bBuilt))
		{
			Test.AddError(RequestError);
			return false;
		}

		Test.TestTrue(
			*FString::Printf(TEXT("Imported fixture root request helper should not report an error for %s"), *RootRegionPath),
			RequestError.IsEmpty());
		return bBuilt && RequestError.IsEmpty();
	}

	/** Builds the shared 8x8 centered-step fixture request used by stepped scheduler regressions. */
	bool BuildLargeFootprintCenteredStepRequest(
		FAutomationTestBase& Test,
		const TCHAR* OuterName,
		const FString& RootRegionPath,
		FLayoutProfileJsonFixtureAssets& OutImportedAssets,
		FLayoutRegionSolveRequest& OutRequest,
		FString& OutFailureReason)
	{
		using namespace PorismLayoutFakeSteppedTerrainTestUtilities;

		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Castle_DA_ContentSet_Castle_MultiChildForcedSeamShare.json"));
		if (!LoadFixtureAssets(Test, FixturePath, OuterName, OutImportedAssets))
		{
			return false;
		}

		OutImportedAssets.Profile->MinimumFootprintInCells = FIntPoint(8, 8);
		OutImportedAssets.Profile->MaximumFootprintInCells = FIntPoint(8, 8);
		if (!BuildImportedFixtureRootRequest(
			Test,
			OutImportedAssets,
			-149679109,
			RootRegionPath,
			OutRequest))
		{
			return false;
		}
		OutRequest.FootprintSize = FIntPoint(8, 8);
		if (!Test.TestTrue(
			TEXT("8x8 fixture builds flat source plan"),
			LayoutProfileSolverInternal::BuildAuthoredPlan(
				OutRequest.ProfileSnapshot,
				OutRequest.ModuleCatalog,
				OutRequest.Seed,
				OutRequest.FootprintSize,
				OutRequest.PlannedCells,
				OutFailureReason)))
		{
			Test.AddError(OutFailureReason);
			return false;
		}

		FCenteredHalfStepConfig TerrainConfig;
		TerrainConfig.FootprintMinBlockWorldPos = FIntVector(380, 300, 195);
		TerrainConfig.LowerSurfaceZ = 195;
		TerrainConfig.SplitAxis = ECenteredHalfStepAxis::X;
		TerrainConfig.SplitCellIndex = 4;
		TerrainConfig.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
		if (!Test.TestTrue(
			TEXT("8x8 fixture builds centered stepped terrain plan"),
			ApplyCenteredHalfStepAndPrecompute(OutRequest, TerrainConfig, OutFailureReason)))
		{
			Test.AddError(OutFailureReason);
			return false;
		}
		return true;
	}

	int32 BuildDeterministicFixtureSolveSeed(const TCHAR* FixtureLabel)
	{
		const uint32 StableHash = GetTypeHash(FString(FixtureLabel));
		return static_cast<int32>((StableHash & 0x7fffffffU) | 1U);
	}

	/** Resolves one world-facing rule from a solved one-cell fixture placement. */
	bool TryGetOneCellPlacementWorldFaceRule(
		const FLayoutPlacedModule& Placement,
		const ELayoutFaceDirection WorldDirection,
		FLayoutFaceRule& OutFaceRule)
	{
		if (!Placement.LocalCellFaceRules.IsEmpty())
		{
			const FLayoutPlacedLocalCellFaceRuleSnapshot* const CellRules =
				Placement.LocalCellFaceRules.FindByPredicate(
					[](const FLayoutPlacedLocalCellFaceRuleSnapshot& Candidate)
					{
						return Candidate.LocalCell == FIntVector::ZeroValue;
					});
			if (CellRules == nullptr)
			{
				return false;
			}

			const FLayoutFaceRule* const Rule = CellRules->ExposedFaceRules.FindByPredicate(
				[&Placement, WorldDirection](const FLayoutFaceRule& Candidate)
				{
					return FLayoutDirectionUtils::RotateYaw(
						Candidate.Direction,
						Placement.YawRotationSteps) == WorldDirection;
				});
			if (Rule == nullptr)
			{
				return false;
			}

			OutFaceRule = *Rule;
			OutFaceRule.Direction = WorldDirection;
			return true;
		}

		if (Placement.Module == nullptr)
		{
			return false;
		}

		const ELayoutFaceDirection AuthoredDirection =
			FLayoutDirectionUtils::RotateYaw(WorldDirection, -Placement.YawRotationSteps);
		const FLayoutFaceRule* const Rule =
			Placement.Module->GetEffectiveFaceRules().FindRule(AuthoredDirection);
		if (Rule == nullptr)
		{
			return false;
		}

		OutFaceRule = *Rule;
		OutFaceRule.Direction = WorldDirection;
		return true;
	}

	/** Routes an imported fixture root request through the lifecycle sequencer.
	 *  Builds a ChunkWorld harness, constructs the RuntimeView, solves via
	 *  TrySolveExplicitRootLayoutSite (lifecycle path), then tears down the harness. */
	bool SolveImportedFixtureRootViaLifecycleSequencer(
		FAutomationTestBase& Test,
		const FLayoutProfileJsonFixtureAssets& ImportedAssets,
		const FIntVector& SiteCenterBlockWorldPos,
		const int32 Seed,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy,
		FLayoutRegionSolveScheduleResult& OutScheduleResult,
		FString& OutFailureReason)
	{
		FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("FixtureHarnessBiome"));
		Test.TestNotNull(TEXT("Fixture lifecycle harness creates a runtime component"), Harness.RuntimeComponent);
		if (Harness.RuntimeComponent == nullptr)
		{
			return false;
		}

		// Use the native infinite-plane strategy so generated ownership and final occupancy share one surface.
		if (Harness.World != nullptr && Harness.World->WorldGenDef != nullptr)
		{
			Harness.World->WorldGenDef->WorldBiomes.Reset();
			Harness.World->WorldGenDef->WorldBiomesDT = nullptr;
			Harness.World->WorldGenDef->BaseBlockSize = 100;
			FWorldGenScaleContextResolver::ClearCache();
			const FGameplayTag FoundationTag = BiomeGameplayTags::Foundation.GetTag();
			UBiomeStrategyData* const Strategy = NewObject<UBiomeStrategyData>(Harness.World->WorldGenDef);
			Strategy->RootFoundationProvider.DebugName = TEXT("FixtureFloor");
			Strategy->RootFoundationProvider.ContributionType = EFoundationContributionType::AdditiveBiome;
			Strategy->RootFoundationProvider.BiomeTag = FoundationTag;
			Strategy->RootFoundationProvider.ProviderType = EFoundationProviderType::InfinitePlane;
			const FResolvedWorldGenScaleContext ScaleContext =
				FWorldGenScaleContextResolver::Resolve(Harness.World, nullptr, false);
			FInfinitePlaneFoundationPayload PlanePayload;
			PlanePayload.SurfaceZ = SiteCenterBlockWorldPos.Z
				- ScaleContext.AuthoredOriginToRawBlockOffset.Z;
			Strategy->RootFoundationProvider.ProviderPayload.InitializeAs<FInfinitePlaneFoundationPayload>(PlanePayload);
			Strategy->RootFoundationProvider.TerrainProfile.InitializeAs<FFlatFoundationTerrainProfilePayload>();

			FBiomeDualData& BiomeRow = Harness.World->WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
			BiomeRow.BiomeName = TEXT("FixtureHarnessBiome");
			BiomeRow.Domain = TEXT("AAAAAIA/");
			BiomeRow.DualSwitch = TEXT("AAAAAIA/");
			UBiomeFastNoiseEditor* const GenA = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);
			GenA->Strategy = Strategy;
			GenA->NoiseSlot = EBiomeNoiseSlot::GenA;
			GenA->BiomeTag = FoundationTag;
			BiomeRow.GenARun = GenA;
		}

		// Keep stored terrain for application checks; solve ownership comes from the configured noise row.
		const FIntPoint FootprintSize = ImportedAssets.Profile != nullptr
			? ImportedAssets.Profile->MaximumFootprintInCells
			: FIntPoint(7, 7);
		const FIntVector SharedCellSize = ImportedAssets.ContentSet != nullptr
			? ImportedAssets.ContentSet->GetSharedCellSizeInBlocks()
			: FIntVector(5, 5, 5);
		const int32 FootprintExtentBlocks = FMath::Max(FootprintSize.X, FootprintSize.Y)
			* FMath::Max(1, FMath::Max(SharedCellSize.X, SharedCellSize.Y));
		for (int32 Y = -FootprintExtentBlocks; Y < FootprintExtentBlocks; ++Y)
		{
			for (int32 X = -FootprintExtentBlocks; X < FootprintExtentBlocks; ++X)
			{
				const int32 WorldX = SiteCenterBlockWorldPos.X + X;
				const int32 WorldY = SiteCenterBlockWorldPos.Y + Y;
				const FIntVector FloorPosition(WorldX, WorldY, SiteCenterBlockWorldPos.Z - 1);
				FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FloorPosition);
			}
		}

		FLayoutWorldBindingRuntimeView RuntimeView =
			LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
				ImportedAssets.Profile.Get(),
				ImportedAssets.ContentSet.Get(),
				FLayoutRootSolveBudgetSettings(),
				PlacementPolicy);
		RuntimeView.MatchingBiomeRowName = TEXT("FixtureHarnessBiome");

		FResolvedLayoutSiteRecord SiteRecord;
		FString LocalFailureReason;
		const bool bSolved = Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
			SiteCenterBlockWorldPos,
			RuntimeView,
			Seed,
			SiteRecord,
			OutScheduleResult,
			&LocalFailureReason);
		if (!bSolved)
		{
			OutFailureReason = LocalFailureReason;
		}
		return bSolved;
	}

	bool VerifyImportedFixtureStructuralInputsAndSolveExpectation(
		FAutomationTestBase& Test,
		const FString& FixturePath,
		const TCHAR* OuterName,
		const FString& RootRegionPath,
		const int32 Seed,
		const int32 ExpectedDemandCount,
		const bool bRequireAnySeamOffer,
		const bool bExpectSolveSuccess)
	{
		using namespace LayoutRegionScheduleSolverFacade;

		FLayoutProfileJsonFixtureAssets ImportedAssets;
		if (!LoadFixtureAssets(Test, FixturePath, OuterName, ImportedAssets)
			|| !Test.TestNotNull(TEXT("Imported fixture content set reconstructed"), ImportedAssets.ContentSet.Get()))
		{
			return false;
		}

		FLayoutRegionSolveRequest RootRequest;
		if (!BuildImportedFixtureRootRequest(
				Test,
				ImportedAssets,
				Seed,
				RootRegionPath,
				RootRequest))
		{
			return false;
		}

		const FRecursiveScheduleSolveContext SolveContext = BuildSolveContext(RootRequest);
		const FCompiledStructuralInputs StructuralInputs = BuildCompiledStructuralInputs(SolveContext);
		if (!Test.TestEqual(
				TEXT("Imported fixture compiles the expected child-demand count"),
				StructuralInputs.Demands.Num(),
				ExpectedDemandCount))
		{
			return false;
		}

		bool bAnyDemandPreservesSeamOffers = false;
		for (const FNegotiationDemandPlan& DemandPlan : StructuralInputs.Demands)
		{
			Test.TestTrue(
				TEXT("Imported fixture preserves endpoint-offer level summaries on every child demand"),
				DemandPlan.ChildSummary.EndpointOffersByLevel.Num() > 0);
			Test.TestTrue(
				TEXT("Imported fixture preserves traversal summaries on every child demand"),
				DemandPlan.ChildSummary.TraversalSummariesByLevel.Num() > 0);
			Test.TestTrue(
				TEXT("Imported fixture preserves placement-bundle consequence carriers on every child demand"),
				DemandPlan.ChildSummary.PlacementBundles.Num() > 0);
			bAnyDemandPreservesSeamOffers |= DemandPlan.ChildSummary.SeamOffersByLevel.Num() > 0;
		}

		if (bRequireAnySeamOffer)
		{
			Test.TestTrue(
				TEXT("Imported fixture preserves seam-offer level summaries on at least one child demand"),
				bAnyDemandPreservesSeamOffers);
		}

		// Route through the lifecycle sequencer.
		const FLayoutWorldBindingPlacementPolicy FixturePlacementPolicy;
		FLayoutRegionSolveScheduleResult ScheduleResult;
		FString LifecycleFailureReason;
		if (!SolveImportedFixtureRootViaLifecycleSequencer(
				Test,
				ImportedAssets,
				RootRequest.RootSiteCenterBlockWorldPos,
				Seed,
				FixturePlacementPolicy,
				ScheduleResult,
				LifecycleFailureReason))
		{
			if (bExpectSolveSuccess)
			{
				Test.AddError(FString::Printf(
					TEXT("Imported fixture should solve through lifecycle. Failure: %s"),
					*LifecycleFailureReason));
				return false;
			}
			return true;
		}
		if (bExpectSolveSuccess)
		{
			if (!Test.TestTrue(TEXT("Imported fixture still solves successfully after the pre-solve carrier assertions"), ScheduleResult.bSucceeded))
			{
				Test.AddError(ScheduleResult.FailureReason);
				for (const FLayoutValidationMessage& Message : ScheduleResult.MergedSolveResult.Messages)
				{
					Test.AddInfo(Message.Message);
				}
				return false;
			}

			Test.TestFalse(
				TEXT("Imported fixture stays on the coordinator-owned public recursive path"),
				ScheduleResult.MergedSolveResult.Messages.ContainsByPredicate(
					[](const FLayoutValidationMessage& Message)
					{
						return Message.Message.Contains(CoordinatorFallbackMessageSubstring);
					}));

			if (ExpectedDemandCount > 0)
			{
				const int32 RegionCount = ScheduleResult.RegionResults.Num();
				Test.TestTrue(
					TEXT("Imported fixture with child demands produces at least one child region result"),
					RegionCount > 1);
				for (int32 Index = 1; Index < RegionCount; ++Index)
				{
					const FLayoutRegionSolveResult& ChildResult =
						ScheduleResult.RegionResults[Index];
					Test.TestTrue(
						FString::Printf(
							TEXT("Child region '%s' solved successfully"),
							*ChildResult.RegionDebugPath),
						ChildResult.SolveResult.bSucceeded);
					Test.TestTrue(
						FString::Printf(
							TEXT("Child region '%s' has placements"),
							*ChildResult.RegionDebugPath),
						ChildResult.SolveResult.Placements.Num() > 0);
				}
			}

			return true;
		}

		if (ScheduleResult.bSucceeded)
		{
			Test.AddError(TEXT("Imported fixture unexpectedly succeeded."));
			return false;
		}

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureRoadCenteredHalfStepSharedTopologyTest,
	"PorismExtension.Layout.Fixtures.Json.RoadCenteredHalfStepSharedTopology",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureRoadCenteredHalfStepProductionContinuationTest,
	"PorismExtension.Layout.Fixtures.Json.RoadCenteredHalfStepProductionContinuation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureFortressRequiredTraversabilityTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.FortressRequiredTraversability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureFortressOptionalTraversabilityTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.FortressOptionalTraversability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureFortressChildRegionOptionalTraversabilityTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.FortressChildRegionOptionalTraversability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureFortressTightChildPlacementTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.FortressTightChildPlacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureFortressExact1InteriorRequiredTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.FortressExact1InteriorRequired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureFortressExact1InteriorOptionalTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.FortressExact1InteriorOptional",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureSteppedParentAuthorityPreservesTerrainSeamEntryTest,
	"PorismExtension.Layout.Fixtures.Json.Repro.SteppedParentAuthorityPreservesTerrainSeamEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutProfileJsonFixtureLargeFootprintSteppedSharedSeamPlacementStaysBoundedTest,
	"PorismExtension.Layout.Fixtures.Json.Performance.LargeFootprintSteppedSharedSeamPlacementStaysBounded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutProfileJsonFixtureRoadCenteredHalfStepSharedTopologyTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutFakeSteppedTerrainTestUtilities;

	const FString FixturePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Road_DA_ContentSet_Road.json"));
	FLayoutProfileJsonFixtureAssets ImportedAssets;
	if (!LoadFixtureAssets(
			*this,
			FixturePath,
			TEXT("LayoutJsonExportedRoadCenteredHalfStepContinuation"),
			ImportedAssets))
	{
		return false;
	}

	FLayoutRegionSolveRequest Request;
	if (!BuildImportedFixtureRootRequest(
			*this,
			ImportedAssets,
			672319588,
			TEXT("Continuation/FakeCenteredHalfStep"),
			Request))
	{
		return false;
	}
	Request.FootprintSize = FIntPoint(10, 10);
	FString PlanFailureReason;
	TestTrue(
		TEXT("Road fixture builds its authored 10x10 plan before fake terrain adaptation"),
		LayoutProfileSolverInternal::BuildAuthoredPlan(
			Request.ProfileSnapshot,
			Request.ModuleCatalog,
			Request.Seed,
			Request.FootprintSize,
			Request.PlannedCells,
			PlanFailureReason));
	if (!PlanFailureReason.IsEmpty())
	{
		AddError(PlanFailureReason);
		return false;
	}

	FCenteredHalfStepConfig TerrainConfig;
	TerrainConfig.SharedCellSizeInBlocks = FIntVector(5, 5, 5);
	TerrainConfig.LowerSurfaceZ = 190;
	TerrainConfig.StepHeightBlocks = 5;
	TerrainConfig.SplitAxis = ECenteredHalfStepAxis::X;
	TerrainConfig.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	FString TerrainFailureReason;
	FLayoutSteppedTerrainSupportMap SourceSupportMap;
	TestTrue(
		TEXT("Road fixture precomputes real stepped adapter output over centered five-block half-step terrain"),
		ApplyCenteredHalfStepAndPrecompute(Request, TerrainConfig, TerrainFailureReason, &SourceSupportMap));
	if (!TerrainFailureReason.IsEmpty())
	{
		AddError(TerrainFailureReason);
		return false;
	}

	int32 RaisedSampleCount = 0;
	for (const FLayoutSteppedTerrainSupportSample& Sample : SourceSupportMap.SupportSamples)
	{
		RaisedSampleCount += Sample.SupportSurfaceZ == TerrainConfig.LowerSurfaceZ + TerrainConfig.StepHeightBlocks ? 1 : 0;
	}
	TestEqual(TEXT("Exactly half of 10x10 terrain support is five blocks higher"), RaisedSampleCount, 50);
	TestTrue(
		TEXT("Fake half-plane freezes cardinal terrain halo evidence"),
		Request.FrozenTerrainBiomeAdapterInput.SteppedNeighborHaloSamples.Num() > 0);
	TestTrue(
		TEXT("Adapter shifts cells onto an upper terrain stage"),
		Request.PrecomputedPlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell.Z > 0 || Cell.ModuleLevelIndex != Cell.Cell.Z;
		}));
	TestTrue(
		TEXT("Adapter injects bridge cells across centered terrain step"),
		Request.PrecomputedPlannedCells.ContainsByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.bIsBridgeCell;
		}));

	const FLayoutIndexedDomainSnapshot Domains = FLayoutProfileSolver::BuildIndexedDomainSnapshot(Request);
	if (!TestTrue(TEXT("Road centered-half-step continuation reaches indexed route domains"), Domains.bSucceeded))
	{
		AddError(Domains.FailureReason);
		return false;
	}

	const FLayoutModuleSolveSnapshot* const SolidSnapshot = Request.ModuleCatalog.Modules.FindByPredicate(
		[](const FLayoutModuleSolveSnapshot& Module)
		{
			return Module.DebugName.ToString().Contains(TEXT("DA_Module_Blocks_Solid"));
		});
	if (!TestNotNull(TEXT("Road fixture includes Solid"), SolidSnapshot))
	{
		return false;
	}
	TestTrue(TEXT("Solid supports Interior"), SolidSnapshot->SupportsIntent(ELayoutCellIntent::Interior));
	TestTrue(TEXT("Solid supports Boundary"), SolidSnapshot->SupportsIntent(ELayoutCellIntent::Boundary));
	TestEqual(TEXT("Solid uses Any placement zone"), SolidSnapshot->PlacementZone, ELayoutPlacementZone::Any);
	const FLayoutFaceRule* const SolidPosZ = SolidSnapshot->EffectiveFaceRules.FindRule(ELayoutFaceDirection::PosZ);
	TestTrue(
		TEXT("Solid requires the generated upper neighbor on PosZ"),
		SolidPosZ != nullptr && SolidPosZ->OccupancyPolicy == ELayoutFaceOccupancyPolicy::RequiresFilledNeighbor);

	TSet<FIntVector> PlannedPositions;
	for (const FLayoutPlannedCell& Cell : Request.PrecomputedPlannedCells)
	{
		PlannedPositions.Add(Cell.Cell);
	}
	const bool bHasTerrainBackedBridgeSupportShape = Request.PrecomputedPlannedCells.ContainsByPredicate(
		[&PlannedPositions](const FLayoutPlannedCell& Cell)
		{
			if (!Cell.bIsBridgeCell || !PlannedPositions.Contains(Cell.Cell + FIntVector(0, 0, 1)))
			{
				return false;
			}
			return !PlannedPositions.Contains(Cell.Cell + FIntVector(1, 0, 0))
				|| !PlannedPositions.Contains(Cell.Cell + FIntVector(-1, 0, 0))
				|| !PlannedPositions.Contains(Cell.Cell + FIntVector(0, 1, 0))
				|| !PlannedPositions.Contains(Cell.Cell + FIntVector(0, -1, 0));
		});
	TestTrue(
		TEXT("Adapter produces a bridge support with an occupied upper cell and an unplanned horizontal terrain-backed neighbor"),
		bHasTerrainBackedBridgeSupportShape);
	const FLayoutTerrainBackedNeighborFaceRecord* const TerrainBackedFace =
		Request.PrecomputedFrozenTerrainContract.TerrainBackedNeighborFaces.FindByPredicate(
			[&PlannedPositions](const FLayoutTerrainBackedNeighborFaceRecord& Face)
			{
				return PlannedPositions.Contains(Face.Cell + FIntVector(0, 0, 1))
					&& !PlannedPositions.Contains(Face.NeighborCell);
			});
	if (!TestNotNull(TEXT("Adapter freezes a provenance-backed terrain occupancy face"), TerrainBackedFace))
	{
		return false;
	}
	TestFalse(TEXT("Terrain-backed proof records stable source evidence"), TerrainBackedFace->SourceEvidenceId.IsNone());
	const bool bAnyTerrainBackedBridgeAdmitsSolid =
		Request.PrecomputedFrozenTerrainContract.TerrainBackedNeighborFaces.ContainsByPredicate(
			[&Domains](const FLayoutTerrainBackedNeighborFaceRecord& Face)
			{
				const FLayoutIndexedCellDomain* const Domain = Domains.CellDomains.FindByPredicate(
					[&Face](const FLayoutIndexedCellDomain& CandidateDomain)
					{
						return CandidateDomain.Cell == Face.Cell;
					});
				return Domain != nullptr && Domain->OrderedCandidateIndices.ContainsByPredicate(
					[&Domains](const int32 CandidateIndex)
					{
						const FLayoutIndexedDomainCandidate* const Candidate = Domains.Candidates.FindByPredicate(
							[CandidateIndex](const FLayoutIndexedDomainCandidate& Candidate)
							{
								return Candidate.CandidateIndex == CandidateIndex;
							});
						return Candidate != nullptr
							&& Candidate->ModuleDebugName.ToString().Contains(TEXT("DA_Module_Blocks_Solid"));
					});
			});
	TestTrue(
		TEXT("At least one terrain-backed bridge support admits Solid before full CSP"),
		bAnyTerrainBackedBridgeAdmitsSolid);
	const FLayoutIndexedCellDomain* const StackedShellDomain = Domains.CellDomains.FindByPredicate(
		[](const FLayoutIndexedCellDomain& Domain)
		{
			return Domain.Cell == FIntVector(4, 0, 0);
		});
	TestTrue(
		TEXT("Generated TopBridge stacked shell retains a non-empty exterior support domain"),
		StackedShellDomain != nullptr && !StackedShellDomain->OrderedCandidateIndices.IsEmpty());

	const FLayoutRegionSolveResult SolveResult = FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Shared stepped bridge topology solves Road centered half-step"), SolveResult.SolveResult.bSucceeded))
	{
		AddError(SolveResult.SolveResult.FailureReason);
		return false;
	}
	return true;
}

bool FLayoutProfileJsonFixtureRoadCenteredHalfStepProductionContinuationTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutFakeSteppedTerrainTestUtilities;

	const FString FixturePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Road_DA_ContentSet_Road.json"));
	FLayoutProfileJsonFixtureAssets ImportedAssets;
	if (!LoadFixtureAssets(
			*this,
			FixturePath,
			TEXT("LayoutJsonRoadProductionContinuation"),
			ImportedAssets)
		|| !TestNotNull(TEXT("Road profile imports for production continuation"), ImportedAssets.Profile.Get())
		|| !TestNotNull(TEXT("Road content set imports for production continuation"), ImportedAssets.ContentSet.Get()))
	{
		return false;
	}

	ImportedAssets.Profile->ContinuationEntryLevel = 0;
	ULayoutWorldBindingAsset* const WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(), TEXT("RoadCenteredHalfStepProductionBinding"));
	WorldBinding->BindingId = TEXT("RoadCenteredHalfStepProductionBinding");
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.MaxFoundationDepth = 5;
	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("RoadCenteredHalfStepProductionFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::BridgeContinuation;
	FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
	FamilyCandidate.CandidateId = TEXT("RoadCenteredHalfStepProductionCandidate");
	FamilyCandidate.LayoutProfile = ImportedAssets.Profile.Get();

	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.WorldBindingId = WorldBinding->BindingId;
	ConnectorRecord.ContinuationFamilyId = Family.FamilyId;
	ConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	ConnectorRecord.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
	ConnectorRecord.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 0;
	ConnectorRecord.WorldBindingPlacementPolicy = WorldBinding->DefaultPlacementPolicy;
	ConnectorRecord.ContinuationPolicy = Family.ContinuationPolicy;
	ConnectorRecord.SolveBudget = Family.SolveBudget;
	ConnectorRecord.LayoutProfile = ImportedAssets.Profile.Get();
	ConnectorRecord.StartEndpointBlockWorldPos = FIntVector(0, 0, 192);
	ConnectorRecord.EndEndpointBlockWorldPos = FIntVector(40, 0, 197);
	ConnectorRecord.StartRootEntryFaceDirection = ELayoutFaceDirection::NegX;
	ConnectorRecord.EndRootEntryFaceDirection = ELayoutFaceDirection::PosX;
	ConnectorRecord.SolveSeed = 672319588;
	FLayoutResolvedConnectorFrontendSelection FrontendSelection;
	FrontendSelection.WorldBindingId = WorldBinding->BindingId;
	FrontendSelection.ContinuationFamilyId = Family.FamilyId;
	FrontendSelection.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
	FrontendSelection.PlacementKind = ELayoutWorldBindingPlacementKind::BridgeContinuation;
	FrontendSelection.ResolvedContinuationSelection = ConnectorRecord.ResolvedContinuationSelection;
	FrontendSelection.WorldBindingPlacementPolicy = WorldBinding->DefaultPlacementPolicy;
	FrontendSelection.ContinuationPolicy = Family.ContinuationPolicy;
	FrontendSelection.SolveBudget = Family.SolveBudget;
	FrontendSelection.SharedCellSizeInBlocks = WorldBinding->BaseCellDimensionsBlocks;
	ConnectorRecord.SetResolvedConnectorFrontendSelection(FrontendSelection);
	FLayoutResolvedConnectorSolveSourceSelection SolveSourceSelection;
	SolveSourceSelection.LayoutProfile = ImportedAssets.Profile.Get();
	SolveSourceSelection.ContentSet = ImportedAssets.ContentSet.Get();
	SolveSourceSelection.SolveSeed = ConnectorRecord.SolveSeed;
	ConnectorRecord.SetResolvedConnectorSolveSourceSelection(SolveSourceSelection);
	FLayoutRootPublicationMetadata PublicationMetadata;
	PublicationMetadata.RootPlacementPolicyId = TEXT("RoadProductionPolicy");
	PublicationMetadata.RootCandidateId = TEXT("RoadProductionCandidate");
	PublicationMetadata.RootSolveId = TEXT("RoadProductionSolve");
	ConnectorRecord.SetRootPublicationMetadata(PublicationMetadata);

	FLayoutContinuationRouteRecord Route;
	Route.RouteKey = TEXT("RoadCenteredHalfStepProductionRoute");
	Route.WorldBindingId = WorldBinding->BindingId;
	Route.ContinuationFamilyId = Family.FamilyId;
	Route.StartRootEndpoint.EndpointBlockWorldPos = ConnectorRecord.StartEndpointBlockWorldPos;
	Route.EndRootEndpoint.EndpointBlockWorldPos = ConnectorRecord.EndEndpointBlockWorldPos;
	Route.CenterlineCells = {
		FIntPoint(0, 0), FIntPoint(1, 0), FIntPoint(2, 0), FIntPoint(3, 0), FIntPoint(4, 0),
		FIntPoint(5, 0), FIntPoint(6, 0), FIntPoint(7, 0), FIntPoint(8, 0)};

	FLayoutContinuationSegmentDescriptor Segment;
	Segment.SegmentIndex = 0;
	Segment.FirstRouteCellIndex = 0;
	Segment.LastRouteCellIndex = Route.CenterlineCells.Num() - 1;
	Segment.CandidateId = FamilyCandidate.CandidateId;
	Segment.LayoutProfile = ImportedAssets.Profile.Get();
	Segment.IngressFaceDirection = ELayoutFaceDirection::NegX;
	Segment.EgressFaceDirection = ELayoutFaceDirection::PosX;

	FCenteredHalfStepConfig TerrainConfig;
	TerrainConfig.SharedCellSizeInBlocks = FIntVector(5, 5, 5);
	TerrainConfig.FootprintMinBlockWorldPos = FIntVector(-2, -2, 190);
	TerrainConfig.LowerSurfaceZ = 190;
	TerrainConfig.StepHeightBlocks = 5;
	TerrainConfig.SplitAxis = ECenteredHalfStepAxis::X;
	TerrainConfig.SplitCellIndex = 4;
	TArray<FIntPoint> TerrainSampleCells;
	for (int32 Y = -5; Y <= 5; ++Y)
	{
		for (int32 X = -5; X <= 13; ++X)
		{
			TerrainSampleCells.Add(FIntPoint(X, Y));
		}
	}
	FLayoutFrozenTerrainBiomeAdapterInput TerrainEvidence;
	if (!TestTrue(
			TEXT("Production continuation freezes centered half-step route terrain"),
			BuildCenteredHalfStepFrozenSurfaceEvidence(
				TerrainConfig,
				FIntPoint::ZeroValue,
				TerrainSampleCells,
				TerrainEvidence)))
	{
		return false;
	}

	FLayoutPreparedContinuation Prepared;
	FString FailureReason;
	if (!TestTrue(
			TEXT("Production continuation prepares through public segment API"),
			FLayoutConnectorPlanning::TryPrepareContinuationSegment(
				ConnectorRecord,
				Segment,
				Route,
				WorldBinding,
				ConnectorRecord.SolveSeed,
				TerrainEvidence,
				Prepared,
				FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	FLayoutRegionSolveRequest Request = Prepared.SolveRequest;
	if (!TestTrue(
			TEXT("Production continuation finalizes shared stepped adapter topology"),
			FLayoutContractPipeline::TryPrecomputeAdapterOutput(Request, FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}
	TestFalse(TEXT("Production continuation keeps resolved family identity"), Request.RootContinuationSelection.FamilyId.IsNone());
	TestTrue(TEXT("Production continuation expands route into a corridor"), Request.PlannedCells.Num() > Route.CenterlineCells.Num());
	TestEqual(TEXT("Production continuation keeps exactly two predetermined Entries"),
		Request.PlannedCells.FilterByPredicate([](const FLayoutPlannedCell& Cell)
		{
			return Cell.Intent == ELayoutCellIntent::Entry;
		}).Num(), 2);
	TestEqual(TEXT("Production continuation keeps exactly two committed Entry anchors"), Request.CommittedEndpointAnchors.Num(), 2);
	TestTrue(TEXT("Production continuation shifts raised terrain cells"), Request.PrecomputedPlannedCells.ContainsByPredicate(
		[](const FLayoutPlannedCell& Cell)
		{
			return Cell.Cell.Z > 0 || Cell.ModuleLevelIndex != Cell.Cell.Z;
		}));
	TestTrue(TEXT("Production continuation retains shared bridge topology"), Request.PrecomputedPlannedCells.ContainsByPredicate(
		[](const FLayoutPlannedCell& Cell)
		{
			return Cell.bIsBridgeCell;
		}));
	TestTrue(TEXT("Production continuation retains frozen terrain-backed face proof"),
		!Request.PrecomputedFrozenTerrainContract.TerrainBackedNeighborFaces.IsEmpty());
	TestTrue(TEXT("Production continuation protects committed endpoint columns from generated bridge cells"),
		!Request.CommittedEndpointAnchors.ContainsByPredicate([&Request](const FLayoutCommittedEndpointAnchor& Anchor)
		{
			return Request.PrecomputedPlannedCells.ContainsByPredicate([&Anchor](const FLayoutPlannedCell& Cell)
			{
				return Cell.Cell.X == Anchor.LocalCell.X
					&& Cell.Cell.Y == Anchor.LocalCell.Y
					&& Cell.bIsBridgeCell;
			});
		}));
	const FLayoutRegionSolveResult SolveResult = FLayoutProfileSolver::SolveRegion(Request);
	if (!TestTrue(TEXT("Production continuation solves Road centered half-step"), SolveResult.SolveResult.bSucceeded))
	{
		AddError(SolveResult.SolveResult.FailureReason);
		return false;
	}
	return true;
}

namespace
{
	/** Imports one immutable Fortress contract, verifies its distinct traversal policy, then exercises the recursive solve. */
	bool RunFortressTraversabilityFixture(
		FAutomationTestBase& Test,
		const TCHAR* FixtureFilename,
		const TCHAR* OuterName,
		const bool bExpectedRequiredTraversability,
		const ELayoutCountConstraintMode ExpectedVerticalAccessMode,
		const bool bExpectedStairs)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir()
			/ TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data")
			/ FixtureFilename);
		FLayoutProfileJsonFixtureAssets ImportedAssets;
		if (!LoadFixtureAssets(Test, FixturePath, OuterName, ImportedAssets, true)
			|| !Test.TestNotNull(TEXT("Fortress fixture profile reconstructed"), ImportedAssets.Profile.Get())
			|| !Test.TestNotNull(TEXT("Fortress fixture content set reconstructed"), ImportedAssets.ContentSet.Get()))
		{
			return false;
		}

		const ULayoutProfileAsset& Profile = *ImportedAssets.Profile;
		Test.TestEqual(TEXT("Fortress traversal enforcement remains authored"), Profile.bRequireAllTraversalChannelsReachable, bExpectedRequiredTraversability);
		Test.TestEqual(TEXT("Fortress VerticalAccess count mode remains authored"), Profile.VerticalAccessCountMode, ExpectedVerticalAccessMode);
		if (bExpectedRequiredTraversability)
		{
			Test.TestEqual(TEXT("Required Fortress VerticalAccess minimum remains authored"), Profile.MinVerticalAccessCount, 2);
			Test.TestEqual(TEXT("Required Fortress VerticalAccess maximum remains authored"), Profile.MaxVerticalAccessCount, 6);
		}
		Test.TestTrue(TEXT("Fortress LevelFillRules remain empty"), Profile.LevelFillRules.IsEmpty());
		Test.TestEqual(TEXT("Fortress keeps one sparse placement rule"), Profile.SparsePlacementRules.Num(), 1);
		if (Profile.SparsePlacementRules.Num() == 1)
		{
			const FLayoutSparsePreserveTerrainRule* PreserveRule =
				Profile.SparsePlacementRules[0].GetPtr<FLayoutSparsePreserveTerrainRule>();
			Test.TestNotNull(TEXT("Fortress sparse rule remains PreserveTerrain"), PreserveRule);
			if (PreserveRule != nullptr)
			{
				Test.TestEqual(TEXT("Fortress PreserveTerrain rule remains Interior"), PreserveRule->PlacementZone, ELayoutPlacementZone::Interior);
				Test.TestEqual(TEXT("Fortress PreserveTerrain rule remains AnyLevel"), PreserveRule->LevelPlacementPolicy, ELayoutLevelPlacementPolicy::AnyLevel);
			}
		}

		const TArray<FLayoutRegionContentEntry>& Entries = ImportedAssets.ContentSet->Entries;
		Test.TestEqual(TEXT("Fortress fixture keeps its authored content count"), Entries.Num(), bExpectedStairs ? 7 : 6);
		const auto FindEntry = [&Entries](const TCHAR* EntryId)
		{
			return Entries.FindByPredicate(
				[EntryId](const FLayoutRegionContentEntry& Entry)
				{
					return Entry.EntryId == FLayoutId(EntryId);
				});
		};
		const auto VerifyModuleEntry = [&Test, &FindEntry](
			const TCHAR* EntryId,
			const ELayoutPlacementZone ExpectedZone,
			const ELayoutLevelPlacementPolicy ExpectedLevelPolicy)
		{
			const FLayoutRegionContentEntry* Entry = FindEntry(EntryId);
			Test.TestNotNull(*FString::Printf(TEXT("Fortress keeps %s content"), EntryId), Entry);
			if (Entry != nullptr)
			{
				Test.TestEqual(*FString::Printf(TEXT("Fortress %s placement zone remains authored"), EntryId), Entry->ModuleSettings.PlacementZone, ExpectedZone);
				Test.TestEqual(*FString::Printf(TEXT("Fortress %s level policy remains authored"), EntryId), Entry->ModuleSettings.LevelPlacementPolicy, ExpectedLevelPolicy);
			}
		};
		VerifyModuleEntry(TEXT("Wall"), ELayoutPlacementZone::Perimeter, ELayoutLevelPlacementPolicy::GroundOnly);
		VerifyModuleEntry(TEXT("Gate"), ELayoutPlacementZone::Edge, ELayoutLevelPlacementPolicy::GroundOnly);
		VerifyModuleEntry(TEXT("Floor"), ELayoutPlacementZone::Interior, ELayoutLevelPlacementPolicy::GroundOnly);
		VerifyModuleEntry(TEXT("BattlementEdge"), ELayoutPlacementZone::Edge, ELayoutLevelPlacementPolicy::TopLevelOnly);
		VerifyModuleEntry(TEXT("BattlementCorner"), ELayoutPlacementZone::Corner, ELayoutLevelPlacementPolicy::TopLevelOnly);

		const FLayoutRegionContentEntry* CastleEntry = FindEntry(TEXT("Castle"));
		Test.TestNotNull(TEXT("Fortress keeps Castle child content"), CastleEntry);
		if (CastleEntry != nullptr)
		{
			Test.TestEqual(TEXT("Fortress Castle remains Interior"), CastleEntry->ChildRegionSettings.PlacementZone, ELayoutPlacementZone::Interior);
			Test.TestEqual(TEXT("Fortress Castle remains GroundOnly"), CastleEntry->ChildRegionSettings.LevelPlacementPolicy, ELayoutLevelPlacementPolicy::GroundOnly);
		}
		if (bExpectedStairs)
		{
			VerifyModuleEntry(TEXT("Stairs"), ELayoutPlacementZone::Any, ELayoutLevelPlacementPolicy::AnyLevel);
		}
		else
		{
			Test.TestNull(TEXT("Optional Fortress export remains authored without stairs"), FindEntry(TEXT("Stairs")));
		}

		FLayoutRegionSolveRequest RootRequest;
		if (!BuildImportedFixtureRootRequest(
			Test,
			ImportedAssets,
			0,
			TEXT("DirectRoot/X=355 Y=410 Z=190"),
			RootRequest))
		{
			return false;
		}

		const FLayoutRegionSolveScheduleResult Schedule =
			FLayoutProfileSolver::SolveRegionTree(RootRequest);
		if (!Test.TestTrue(TEXT("Fortress fixture solves under its authored traversability policy"), Schedule.bSucceeded))
		{
			Test.AddError(Schedule.FailureReason);
			return false;
		}

		const FLayoutRegionSolveResult* RootResult = Schedule.RegionResults.FindByPredicate(
			[&RootRequest](const FLayoutRegionSolveResult& Result)
			{
				return Result.RegionDebugPath == RootRequest.RegionDebugPath
					&& Result.SourceContentEntryId.IsNone();
			});
		const FLayoutRegionSolveResult* CastleResult = Schedule.RegionResults.FindByPredicate(
			[](const FLayoutRegionSolveResult& Result)
			{
				return Result.SourceContentEntryId == TEXT("Castle")
					&& !Result.bDroppedAsOptionalChild;
			});
		Test.TestNotNull(TEXT("Fortress solved result retains root region"), RootResult);
		Test.TestNotNull(TEXT("Fortress commits required Castle child"), CastleResult);
		if (RootResult == nullptr || CastleResult == nullptr)
		{
			return false;
		}

		const int32 RootGateCount = RootResult->SolveResult.Placements.FilterByPredicate(
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.SourceContentEntryId == TEXT("Gate");
			}).Num();
		Test.TestEqual(TEXT("Fortress root retains one authored Gate Entry"), RootGateCount, 1);
		const int32 RootStairCount = RootResult->SolveResult.Placements.FilterByPredicate(
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.SourceContentEntryId == TEXT("Stairs")
					|| Placement.ModuleSnapshotId.ToString().Contains(TEXT("Stairs"));
			}).Num();
		const int32 GroundRouteCount = RootResult->SolveResult.RouteConstraints.FilterByPredicate(
			[](const FLayoutRouteConstraintRecord& Constraint)
			{
				return Constraint.Cell.Z == 0;
			}).Num();
		const int32 GroundResidualCount = RootResult->SolveResult.ResidualUnoccupiedCells.FilterByPredicate(
			[](const FLayoutResidualCellRecord& Residual)
			{
				return Residual.Cell.Z == 0;
			}).Num();
		const int32 UpperResidualCount = RootResult->SolveResult.ResidualUnoccupiedCells.FilterByPredicate(
			[](const FLayoutResidualCellRecord& Residual)
			{
				return Residual.Cell.Z > 0;
			}).Num();
		Test.TestTrue(TEXT("Fortress ground route claims remain solved"), GroundRouteCount > 0);
		Test.TestTrue(TEXT("Fortress keeps ground sparse residual courtyard cells"), GroundResidualCount > 0);
		Test.TestTrue(TEXT("Fortress keeps upper sparse residual courtyard cells"), UpperResidualCount > 0);
		Test.TestFalse(
			TEXT("Direct Castle Entry handoff creates no parent/child Door seam"),
			Schedule.MergedSolveResult.PartitionSeams.ContainsByPredicate(
				[&RootRequest, CastleResult](const FLayoutPartitionSeamRecord& Seam)
				{
					const bool bConnectsRootAndCastle =
						(Seam.OwnerRegionDebugPath == RootRequest.RegionDebugPath
							&& Seam.PassiveRegionDebugPath == CastleResult->RegionDebugPath)
						|| (Seam.PassiveRegionDebugPath == RootRequest.RegionDebugPath
							&& Seam.OwnerRegionDebugPath == CastleResult->RegionDebugPath);
					return bConnectsRootAndCastle
						&& Seam.InterfaceFamily == LayoutGameplayTags::InterfacePartitionDoor;
				}));

		if (bExpectedRequiredTraversability)
		{
			Test.TestEqual(TEXT("Strict Fortress counts authored Range minimum"), Schedule.RecursiveVerticalAccessSummary.RequiredHostProviderCount, 2);
			Test.TestTrue(TEXT("Strict Fortress resolves required stair providers"), Schedule.RecursiveVerticalAccessSummary.ResolvedHostProviderCount >= 2);
			Test.TestTrue(TEXT("Strict Fortress places legal stairs"), RootStairCount >= 2);
		}
		else
		{
			Test.TestEqual(TEXT("Optional Fortress has no VerticalAccess obligation"), Schedule.RecursiveVerticalAccessSummary.RequiredHostProviderCount, 0);
			Test.TestEqual(TEXT("Optional Fortress places no unauthored stairs"), RootStairCount, 0);
		}
		return true;
	}
}

/** Locks and solves the required-traversability Fortress export without profile mutation. */
bool FLayoutProfileJsonFixtureFortressRequiredTraversabilityTest::RunTest(const FString& Parameters)
{
	return RunFortressTraversabilityFixture(
		*this,
		TEXT("DA_Profile_Fortress_RequiredTraversability_DA_ContentSet_Fortress.json"),
		TEXT("LayoutJsonFortressRequiredTraversability"),
		true,
		ELayoutCountConstraintMode::Range,
		true);
}

/** Locks and solves the optional-traversability Fortress export without profile mutation. */
bool FLayoutProfileJsonFixtureFortressOptionalTraversabilityTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutFakeSteppedTerrainTestUtilities;
	const FString FixturePath = FPaths::ConvertRelativePathToFull(
		FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Fortress_DA_ContentSet_Fortress_OptionalTraversability.json"));
	FLayoutProfileJsonFixtureAssets Assets;
	if (!LoadFixtureAssets(*this, FixturePath, TEXT("LayoutJsonFortressOptionalTraversability"), Assets, true))
	{
		return false;
	}
	TestFalse(TEXT("Fortress traversal remains optional"), Assets.Profile->bRequireAllTraversalChannelsReachable);
	TestEqual(TEXT("Fortress retains two Entries"), Assets.Profile->EntryCount, 2);
	TestEqual(TEXT("Fortress retains Range stairs"), Assets.Profile->VerticalAccessCountMode, ELayoutCountConstraintMode::Range);
	TestEqual(TEXT("Fortress stair minimum"), Assets.Profile->MinVerticalAccessCount, 2);
	TestEqual(TEXT("Fortress stair maximum"), Assets.Profile->MaxVerticalAccessCount, 6);
	TestEqual(TEXT("Fortress keeps eight module entries"), Assets.ContentSet->Entries.Num(), 8);

	// Exercise both terrain modes without changing authored faces, counts or budgets.
	for (const bool bStepped : {false, true})
	{
		FLayoutRegionSolveRequest Request;
		if (!BuildImportedFixtureRootRequest(*this, Assets, 0, TEXT("OptionalFortress"), Request))
		{
			return false;
		}
		FString Failure;
		if (bStepped)
		{
			if (!LayoutProfileSolverInternal::BuildAuthoredPlan(Request.ProfileSnapshot,
				Request.ModuleCatalog, Request.Seed, Request.FootprintSize, Request.PlannedCells, Failure))
			{
				AddError(Failure);
				continue;
			}
			FCenteredHalfStepConfig Terrain;
			Terrain.FootprintMinBlockWorldPos = FIntVector(380, 380, 195);
			Terrain.LowerSurfaceZ = 195;
			Terrain.SplitAxis = ECenteredHalfStepAxis::X;
			Terrain.SplitCellIndex = Request.FootprintSize.X / 2;
			Terrain.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
			if (!ApplyCenteredHalfStepAndPrecompute(Request, Terrain, Failure))
			{
				AddError(FString::Printf(TEXT("Stepped Fortress preparation: %s"), *Failure));
				continue;
			}
		}
		const FLayoutRegionSolveScheduleResult Schedule = FLayoutProfileSolver::SolveRegionTree(Request);
		if (!TestTrue(bStepped ? TEXT("Stepped Fortress solves without fallback") : TEXT("Flat Fortress solves"), Schedule.bSucceeded))
		{
			AddError(Schedule.FailureReason);
			continue;
		}
		const TArray<FLayoutPlacedModule>& Placements = Schedule.MergedSolveResult.Placements;
		TestEqual(TEXT("Fortress places both Gates"), Placements.FilterByPredicate(
			[](const FLayoutPlacedModule& P) { return P.SourceContentEntryId == TEXT("Gate"); }).Num(), 2);
		TestTrue(TEXT("Fortress places authored stair minimum"), Placements.FilterByPredicate(
			[](const FLayoutPlacedModule& P) { return P.SourceContentEntryId == TEXT("Stairs"); }).Num() >= 2);
		TestEqual(TEXT("Fortress places required Castle module"), Placements.FilterByPredicate(
			[](const FLayoutPlacedModule& P) { return P.SourceContentEntryId == TEXT("Castle"); }).Num(), 1);
	}
	return true;
}

/** Exercises the unchanged recursive export; parent optional traversal cannot relax Castle/Room contracts. */
bool FLayoutProfileJsonFixtureFortressChildRegionOptionalTraversabilityTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutFakeSteppedTerrainTestUtilities;
	const FString FixturePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()
		/ TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Fortress_DA_ContentSet_Fortress_ChildRegion_OptionalTraversibility.json"));
	FLayoutProfileJsonFixtureAssets Assets;
	if (!LoadFixtureAssets(*this, FixturePath, TEXT("LayoutJsonFortressChildOptional"), Assets, true)) return false;
	for (const bool bStepped : {false, true})
	{
		FLayoutRegionSolveRequest Request;
		if (!BuildImportedFixtureRootRequest(*this, Assets, 0, TEXT("OptionalFortressChild"), Request)) return false;
		FString Failure;
		if (bStepped)
		{
			if (!LayoutProfileSolverInternal::BuildAuthoredPlan(Request.ProfileSnapshot, Request.ModuleCatalog,
				Request.Seed, Request.FootprintSize, Request.PlannedCells, Failure)) { AddError(Failure); continue; }
			FCenteredHalfStepConfig Terrain;
			Terrain.FootprintMinBlockWorldPos = FIntVector(365, 360, 190);
			Terrain.LowerSurfaceZ = 190;
			Terrain.SplitAxis = ECenteredHalfStepAxis::X;
			Terrain.SplitCellIndex = Request.FootprintSize.X / 2;
			Terrain.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
			if (!ApplyCenteredHalfStepAndPrecompute(Request, Terrain, Failure)) { AddError(Failure); continue; }
		}
		// Opt-in reproduction also exercises the live request diagnostics; retain authored limits.
		TOptional<LayoutSolveExecution::FScope> DiagnosticBudget;
		TOptional<LayoutSolveExecution::FDiagnosticScope> DiagnosticPhase;
		if (!FPlatformMisc::GetEnvironmentVariable(TEXT("PORISM_LAYOUT_LOG_RECURSIVE_PERF")).IsEmpty())
		{
			DiagnosticBudget.Emplace(Request.ExecutionSettings.MaxSolveDurationSeconds, Request.ExecutionSettings.MaxCandidateAttempts);
			auto* Ledger = LayoutSolveExecution::CurrentThreadLedger();
			Ledger->DiagnosticContext = FString::Printf(TEXT("origin=fixture profile=OptionalFortressChild seed=%d stepped=%d"), Request.Seed, bStepped);
			DiagnosticPhase.Emplace(Ledger->DiagnosticContext, TEXT("fixture-solve"), &Failure, 0.0, Ledger);
		}
		const double Started = FPlatformTime::Seconds();
		const FLayoutRegionSolveScheduleResult Schedule = FLayoutProfileSolver::SolveRegionTree(Request);
		Failure = Schedule.FailureReason;
		if (DiagnosticPhase.IsSet()) DiagnosticPhase->Finish(Schedule.bSucceeded);
		AddInfo(FString::Printf(TEXT("Recursive Fortress %s: %.3fs, regions=%d"),
			bStepped ? TEXT("stepped") : TEXT("flat"), FPlatformTime::Seconds() - Started, Schedule.RegionResults.Num()));
		if (!TestTrue(bStepped ? TEXT("Stepped recursive Fortress solves without fallback") : TEXT("Flat recursive Fortress solves"), Schedule.bSucceeded))
		{
			AddError(Schedule.FailureReason);
			continue;
		}
		TestEqual(TEXT("Exactly one required Castle region"), Schedule.RegionResults.FilterByPredicate(
			[](const auto& R) { return R.SourceContentEntryId == TEXT("Castle") && !R.bDroppedAsOptionalChild; }).Num(), 1);
		// Independent child proof collapses its successful subtree into the Castle
		// result. Count distinct Room commitments from that accepted subtree, not
		// preparation calls or nonexistent top-level Room result rows.
		const auto* Castle = Schedule.RegionResults.FindByPredicate(
			[](const auto& R) { return R.SourceContentEntryId == TEXT("Castle") && !R.bDroppedAsOptionalChild; });
		if (Castle)
		{
			TSet<FLayoutId> RoomProviders;
			for (const auto& Provider : Castle->SolveResult.ZoneFeatureProviderCommitments)
			{
				if (Provider.SourceContentEntryId != TEXT("Room")
					|| Provider.SourceRegionDebugPath != Castle->RegionDebugPath) continue;
				TestFalse(TEXT("Room proof commitment has stable identity"), Provider.ProviderCommitmentId.IsNone());
				RoomProviders.Add(Provider.ProviderCommitmentId);
			}
			TestEqual(TEXT("Both required Room descendants in accepted Castle subtree"), RoomProviders.Num(), 2);
		}
		for (const auto& Region : Schedule.RegionResults)
			TestTrue(TEXT("Each retained regional proof succeeded"), Region.bDroppedAsOptionalChild || Region.SolveResult.bSucceeded);
	}
	return true;
}

/** The 13x13/7x7 export leaves stair space while checking Castle placement across the centered step. */
bool FLayoutProfileJsonFixtureFortressTightChildPlacementTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutFakeSteppedTerrainTestUtilities;
	const FString FixturePath = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir()
		/ TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Fortress_DA_ContentSet_Fortress_ChildRegion_TightChildPlacement.json"));
	FLayoutProfileJsonFixtureAssets Assets;
	if (!LoadFixtureAssets(*this, FixturePath, TEXT("LayoutJsonFortressTightChild"), Assets, true)) return false;
	TestEqual(TEXT("Authored Fortress minimum"), Assets.Profile->MinimumFootprintInCells, FIntPoint(13, 13));
	TestEqual(TEXT("Authored Fortress maximum"), Assets.Profile->MaximumFootprintInCells, FIntPoint(13, 13));
	const auto* CastleEntry = Assets.ContentSet->Entries.FindByPredicate(
		[](const auto& Entry) { return Entry.EntryId == TEXT("Castle"); });
	if (!TestNotNull(TEXT("Castle entry retained"), CastleEntry)) return false;
	auto* CastleProfile = CastleEntry->ChildRegionSettings.RegionProfile.Get();
	if (!TestNotNull(TEXT("Castle profile retained"), CastleProfile)) return false;
	TestEqual(TEXT("Authored Castle minimum"), CastleProfile->MinimumFootprintInCells, FIntPoint(7, 7));
	TestEqual(TEXT("Authored Castle maximum"), CastleProfile->MaximumFootprintInCells, FIntPoint(7, 7));
	TestTrue(TEXT("Castle supports stepped terrain"), CastleProfile->bSupportsSteppedTerrainSolve);
	TestEqual(TEXT("Castle keeps GroundOnly placement"), CastleEntry->ChildRegionSettings.LevelPlacementPolicy,
		ELayoutLevelPlacementPolicy::GroundOnly);

	for (const bool bStepped : {false, true})
	{
		FLayoutRegionSolveRequest Request;
		if (!BuildImportedFixtureRootRequest(*this, Assets, 0, TEXT("TightFortressChild"), Request)) return false;
		TestEqual(TEXT("Resolved parent footprint"), Request.FootprintSize, FIntPoint(13, 13));
		FString Failure;
		if (bStepped)
		{
			if (!LayoutProfileSolverInternal::BuildAuthoredPlan(Request.ProfileSnapshot, Request.ModuleCatalog,
				Request.Seed, Request.FootprintSize, Request.PlannedCells, Failure)) { AddError(Failure); continue; }
			FCenteredHalfStepConfig Terrain;
			Terrain.FootprintMinBlockWorldPos = FIntVector(420, 345, 200);
			Terrain.LowerSurfaceZ = 200;
			Terrain.SplitAxis = ECenteredHalfStepAxis::X;
			Terrain.SplitCellIndex = 6;
			Terrain.StepHeightBlocks = 5;
			Terrain.PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
			if (!ApplyCenteredHalfStepAndPrecompute(Request, Terrain, Failure))
			{
				AddError(FString::Printf(TEXT("Tight stepped parent preparation: %s"), *Failure));
				continue;
			}
			// Inspect the production mapper independently of boundary certification,
			// using the same authored plan and first rejected offset, without changing the solve request.
			FLayoutProfileJsonFixtureAssets ChildAssets;
			ChildAssets.Profile = CastleProfile;
			ChildAssets.ContentSet = CastleProfile->ContentSet;
			FLayoutRegionSolveRequest ChildRequest;
			FLayoutChildStageMappingResult Probe;
			if (BuildImportedFixtureRootRequest(*this, ChildAssets, 0, TEXT("TightCastleMappingProbe"), ChildRequest)
				&& LayoutProfileSolverInternal::BuildAuthoredPlan(ChildRequest.ProfileSnapshot, ChildRequest.ModuleCatalog,
					ChildRequest.Seed, ChildRequest.FootprintSize, ChildRequest.PlannedCells, Failure)
				&& LayoutProfileSolverInternal::TryBuildChildStageMappingForTests(Request, Request.PlannedCells,
					ChildRequest.FootprintSize, FIntVector(2, 2, 0), TEXT("Castle"), 0, true,
					ChildRequest.PlannedCells, Probe, Failure, &ChildRequest))
			{
				FLayoutRegionSolveRequest Standalone = ChildRequest;
				FCenteredHalfStepConfig ChildTerrain = Terrain;
				ChildTerrain.SplitCellIndex = 4;
				ChildTerrain.FootprintMinBlockWorldPos += FIntVector(10, 10, 0);
				if (TestTrue(TEXT("Standalone Castle terrain preparation"), ApplyCenteredHalfStepAndPrecompute(Standalone, ChildTerrain, Failure)))
				{
					TestEqual(TEXT("Inherited Castle has standalone topology size"), Probe.ChildLocalPlannedCells.Num(), Standalone.PlannedCells.Num());
					for (const auto& Cell : Standalone.PlannedCells)
					{
						const auto* Mapped = Probe.ChildLocalPlannedCells.FindByPredicate([&](const auto& C) { return C.Cell == Cell.Cell; });
						TestTrue(FString::Printf(TEXT("Inherited/standalone terrain authority %s"), *Cell.Cell.ToString()),
							Mapped != nullptr && Mapped->ModuleLevelIndex == Cell.ModuleLevelIndex
							&& Mapped->bIsBridgeCell == Cell.bIsBridgeCell && Mapped->bIsTopBridgeOffer == Cell.bIsTopBridgeOffer
							&& Mapped->TerrainSeamFaceMask == Cell.TerrainSeamFaceMask
							&& Mapped->VerticalAccessLandingContactMask == Cell.VerticalAccessLandingContactMask);
					}
					const auto StandaloneSolve = FLayoutProfileSolver::SolveRegionTree(Standalone);
					if (!TestTrue(TEXT("Standalone stepped Castle solves on inherited terrain"), StandaloneSolve.bSucceeded))
						AddError(StandaloneSolve.FailureReason);
				}
				else AddError(Failure);
				TSet<FIntVector> ParentCells, ReservedCells;
				for (const auto& Cell : Request.PlannedCells) ParentCells.Add(Cell.Cell);
				for (const auto& Cell : Probe.Cells) ReservedCells.Add(Cell.ParentCell);
				TArray<FLayoutVerticalAccessHostGroup> ParentHosts;
				if (TestTrue(TEXT("Parent stairs admit alternatives outside mapped Castle volume"),
					LayoutProfileSolverInternal::TrySelectAdmittedParentVerticalAccessHostsForTests(
						Request.VerticalAccessHostGroups, ParentCells, ReservedCells, ParentHosts, Failure)))
				{
					TestFalse(TEXT("Parent stair alternatives exist after child reservation"), ParentHosts.IsEmpty());
					for (const auto& Group : ParentHosts)
						for (const auto& Option : Group.Options)
						{
							TestFalse(TEXT("Parent stair root outside child"), ReservedCells.Contains(Option.LowerCell));
							TestFalse(TEXT("Parent stair upper outside child"), ReservedCells.Contains(Option.UpperCell));
							for (const auto& Cell : Option.OccupiedCells) TestFalse(TEXT("Parent stair occupancy outside child"), ReservedCells.Contains(Cell));
							for (const auto& Cell : Option.RequiredFilledSupportCells) TestFalse(TEXT("Parent stair support outside child"), ReservedCells.Contains(Cell));
							for (const auto& Cell : Option.RequiredEmptyClearanceCells) TestFalse(TEXT("Parent stair clearance outside child"), ReservedCells.Contains(Cell));
						}
				}
				else AddError(Failure);
				for (const FIntVector Source : {FIntVector(3, 0, 0), FIntVector(4, 0, 0)})
				{
					const auto* Cell = Probe.Cells.FindByPredicate([&](const auto& C) { return C.SourceChildCell == Source; });
					if (Cell == nullptr) continue;
					const FIntVector Neighbor = Cell->ParentCell + FIntVector(1, 0, 0);
					const auto* ParentNeighbor = Request.PlannedCells.FindByPredicate([&](const auto& C) { return C.Cell == Neighbor; });
					const bool bChildOwnsNeighbor = Probe.Cells.ContainsByPredicate([&](const auto& C) { return C.ParentCell == Neighbor; });
					AddInfo(FString::Printf(TEXT("Tight mapping source=%s mapped=%s parent=%s PosX=%s childOwnsNeighbor=%d parentBridge=%d parentTopOffer=%d parentLevel=%d parentIntent=%d sourceCount=%d mappedCount=%d"),
						*Source.ToString(), *Cell->MappedChildCell.ToString(), *Cell->ParentCell.ToString(), *Neighbor.ToString(),
						bChildOwnsNeighbor, ParentNeighbor != nullptr && ParentNeighbor->bIsBridgeCell,
						ParentNeighbor != nullptr && ParentNeighbor->bIsTopBridgeOffer,
						ParentNeighbor ? ParentNeighbor->ModuleLevelIndex : INDEX_NONE,
						ParentNeighbor ? static_cast<int32>(ParentNeighbor->Intent) : INDEX_NONE,
						ChildRequest.PlannedCells.Num(), Probe.Cells.Num()));
				}
			}
			else AddError(FString::Printf(TEXT("Tight mapping probe: %s"), *Failure));
		}
		const double Started = FPlatformTime::Seconds();
		const auto Schedule = FLayoutProfileSolver::SolveRegionTree(Request);
		AddInfo(FString::Printf(TEXT("Tight Fortress %s seed=0: %.3fs, regions=%d mappings=%d"),
			bStepped ? TEXT("stepped") : TEXT("flat"), FPlatformTime::Seconds() - Started,
			Schedule.RegionResults.Num(), Schedule.ChildStageMappings.Num()));
		if (!TestTrue(bStepped ? TEXT("Tight stepped composition succeeds without fallback")
			: TEXT("Tight flat composition succeeds"), Schedule.bSucceeded))
		{
			AddError(Schedule.FailureReason);
			continue;
		}
		const auto* Castle = Schedule.RegionResults.FindByPredicate(
			[](const auto& Region) { return Region.SourceContentEntryId == TEXT("Castle") && !Region.bDroppedAsOptionalChild; });
		if (!TestNotNull(TEXT("Required Castle proof retained"), Castle)) continue;
		TestTrue(TEXT("Castle interior proof succeeds"), Castle->SolveResult.bSucceeded);
		TSet<FLayoutId> RoomProviders;
		for (const auto& Provider : Castle->SolveResult.ZoneFeatureProviderCommitments)
			if (Provider.SourceContentEntryId == TEXT("Room") && Provider.SourceRegionDebugPath == Castle->RegionDebugPath
				&& !Provider.ProviderCommitmentId.IsNone())
				RoomProviders.Add(Provider.ProviderCommitmentId);
		TestEqual(TEXT("Both required Room proofs retained"), RoomProviders.Num(), 2);
		if (bStepped)
		{
			const auto* Mapping = Schedule.ChildStageMappings.FindByPredicate([](const auto& Item)
			{
				return Item.ChildLocalTerrainContract.FootprintSizeInCells == FIntPoint(7, 7);
			});
			if (!TestNotNull(TEXT("Castle's exact stage mapping retained"), Mapping)) continue;
			TSet<int32> GroundHeights;
			bool bTouchesLowerHalf = false, bTouchesUpperHalf = false;
			for (const auto& Cell : Mapping->Cells)
			{
				const auto* Planned = Mapping->ChildLocalPlannedCells.FindByPredicate(
					[&Cell](const auto& Item) { return Item.Cell == Cell.MappedChildCell; });
				if (Cell.ModuleLevelIndex != 0 || Planned == nullptr || Planned->bIsBridgeCell) continue;
				GroundHeights.Add(Cell.ParentCell.Z);
				bTouchesLowerHalf |= Cell.ParentCell.X < 6;
				bTouchesUpperHalf |= Cell.ParentCell.X >= 6;
			}
			TestTrue(TEXT("Castle overlaps both sides of centered parent step"), bTouchesLowerHalf && bTouchesUpperHalf);
			TestTrue(TEXT("Castle's own ground follows multiple physical heights"), GroundHeights.Num() > 1);
		}
	}
	return true;
}

namespace
{
	bool RunFortressExact1InteriorFixture(
		FAutomationTestBase& Test,
		const TCHAR* OuterName,
		const bool bRequireTraversal)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir()
			/ TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data")
			/ TEXT("DA_Profile_Fortress_RequiredTraversability_DA_ContentSet_Fortress.json"));
		FLayoutProfileJsonFixtureAssets ImportedAssets;
		if (!LoadFixtureAssets(Test, FixturePath, OuterName, ImportedAssets, true)
			|| ImportedAssets.Profile == nullptr
			|| ImportedAssets.ContentSet == nullptr)
		{
			return false;
		}

		ImportedAssets.Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
		ImportedAssets.Profile->VerticalAccessCount = 1;
		ImportedAssets.Profile->bRequireAllTraversalChannelsReachable = bRequireTraversal;
		FLayoutRegionContentEntry* StairEntry = ImportedAssets.ContentSet->Entries.FindByPredicate(
			[](const FLayoutRegionContentEntry& Entry)
			{
				return Entry.EntryId == TEXT("Stairs");
			});
		if (!Test.TestNotNull(TEXT("Exact1 Fortress keeps authored stair content"), StairEntry))
		{
			return false;
		}
		StairEntry->ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionSolveRequest Request;
		if (!BuildImportedFixtureRootRequest(
				Test,
				ImportedAssets,
				0,
				TEXT("DirectRoot/Exact1InteriorFortress"),
				Request))
		{
			return false;
		}
		Test.TestEqual(TEXT("Exact1 reaches frozen solver request"),
			Request.ProfileSnapshot.VerticalAccessCountMode,
			ELayoutCountConstraintMode::Exact);
		Test.TestEqual(TEXT("Frozen solver request owns one ascent provider"),
			Request.ProfileSnapshot.VerticalAccessCount,
			1);
		const FLayoutRegionSolveResult PreparedTopology =
			LayoutProfileSolverInternal::BuildRequestBackedRegionPreparedTopology(Request);
		if (!Test.TestTrue(TEXT("Exact1 Fortress prepares owning topology"),
			PreparedTopology.SolveResult.bSucceeded))
		{
			Test.AddError(PreparedTopology.SolveResult.FailureReason);
			return false;
		}
		TArray<FLayoutPlannedCell> WitnessCells = PreparedTopology.SolveResult.PlannedCells;
		TArray<FLayoutVerticalAccessHostGroup> WitnessGroups;
		FString WitnessFailure;
		if (!Test.TestTrue(
			TEXT("Exact1 Fortress prepares local stair alternatives"),
			LayoutProfileSolverInternal::RebuildVerticalAccessPlanningForPreparedTopology(
				Request, WitnessCells, WitnessGroups, WitnessFailure)))
		{
			Test.AddError(WitnessFailure);
			return false;
		}
		Test.TestTrue(
			TEXT("Fortress local alternatives retain exact filled support obligations"),
			WitnessGroups.Num() == 1
				&& WitnessGroups[0].Options.ContainsByPredicate(
					[](const FLayoutVerticalAccessHostOption& Option)
					{
						return Option.bHasExactCandidateWitness
							&& !Option.RequiredFilledSupportCells.IsEmpty();
					}));
		const FLayoutRegionSolveScheduleResult Schedule =
			FLayoutProfileSolver::SolveRegionTree(Request);
		if (!Test.TestTrue(bRequireTraversal
			? TEXT("Required Exact1 Interior Fortress solves")
			: TEXT("Optional Exact1 Interior Fortress solves"), Schedule.bSucceeded))
		{
			Test.AddError(Schedule.FailureReason);
			return false;
		}
		const FLayoutRegionSolveResult* RootResult = Schedule.RegionResults.FindByPredicate(
			[&Request](const FLayoutRegionSolveResult& RegionResult)
			{
				return RegionResult.RegionDebugPath == Request.RegionDebugPath;
			});
		if (!Test.TestNotNull(TEXT("Exact1 result retains direct root proof"), RootResult))
		{
			return false;
		}
		const TArray<FLayoutPlacedModule> RootStairs = RootResult->SolveResult.Placements.FilterByPredicate(
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.SourceContentEntryId == TEXT("Stairs")
					|| Placement.ModuleSnapshotId.ToString().Contains(TEXT("Stairs"));
			});
		Test.TestEqual(TEXT("Exact1 counts one direct root stair"), RootStairs.Num(), 1);
		if (RootStairs.Num() == 1)
		{
			Test.TestTrue(
				TEXT("Exact1 stair retains composite upper landing"),
				RootStairs[0].OccupiedLocalCells.Contains(FIntVector(0, 0, 1)));
			const FIntVector UpperStairCell =
				RootStairs[0].Cell + FIntVector(0, 0, 1);
			const bool bTouchesBattlement = RootResult->SolveResult.Placements.ContainsByPredicate(
				[&UpperStairCell](const FLayoutPlacedModule& Placement)
				{
					return Placement.ModuleSnapshotId.ToString().Contains(TEXT("Battlement"))
						&& Placement.Cell.Z == UpperStairCell.Z
						&& FMath::Abs(Placement.Cell.X - UpperStairCell.X)
							+ FMath::Abs(Placement.Cell.Y - UpperStairCell.Y) == 1;
				});
			Test.TestTrue(TEXT("Exact1 upper landing contacts battlement directly"), bTouchesBattlement);
		}
		Test.TestTrue(
			TEXT("Unclaimed upper Interior remains residual empty space"),
			RootResult->SolveResult.ResidualUnoccupiedCells.ContainsByPredicate(
				[](const FLayoutResidualCellRecord& Residual)
				{
					return Residual.Cell.Z > 0
						&& Residual.PlacementZone == ELayoutPlacementZone::Interior;
				}));
		Test.TestEqual(TEXT("Exact1 recursive ownership resolves one provider"),
			Schedule.RecursiveVerticalAccessSummary.ResolvedHostProviderCount,
			1);
		return true;
	}
}

bool FLayoutProfileJsonFixtureFortressExact1InteriorRequiredTest::RunTest(const FString& Parameters)
{
	return RunFortressExact1InteriorFixture(
		*this,
		TEXT("LayoutJsonFortressExact1InteriorRequired"),
		true);
}

bool FLayoutProfileJsonFixtureFortressExact1InteriorOptionalTest::RunTest(const FString& Parameters)
{
	return RunFortressExact1InteriorFixture(
		*this,
		TEXT("LayoutJsonFortressExact1InteriorOptional"),
		false);
}

bool FLayoutProfileJsonFixtureSteppedParentAuthorityPreservesTerrainSeamEntryTest::RunTest(const FString& Parameters)
{
	FLayoutProfileJsonFixtureAssets ImportedAssets;
	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	if (!BuildLargeFootprintCenteredStepRequest(
		*this,
		TEXT("LayoutJsonSteppedParentAuthorityPreservesTerrainSeamEntry"),
		TEXT("SteppedParentAuthorityPreservesTerrainSeamEntry"),
		ImportedAssets,
		Request,
		FailureReason))
	{
		return false;
	}

	const FLayoutPlannedCell* const OriginalTerrainSeamEntry = Request.PlannedCells.FindByPredicate(
		[](const FLayoutPlannedCell& PlannedCell)
		{
			return PlannedCell.Intent == ELayoutCellIntent::Entry
				&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam;
		});
	if (!TestNotNull(TEXT("Centered step creates one internal terrain-seam Entry"), OriginalTerrainSeamEntry))
	{
		return false;
	}

	FLayoutRegionSolveRequest FrozenAuthorityRequest;
	const TSet<FIntVector> NoForbiddenCells;
	if (!TestTrue(
		TEXT("Stepped parent authority freezes"),
		LayoutProfileSolverInternal::TryFreezeParentAuthorityCellsFromPreparedPrefix(
			Request,
			true,
			NoForbiddenCells,
			NoForbiddenCells,
			FrozenAuthorityRequest,
			FailureReason)))
	{
		AddError(FailureReason);
		return false;
	}

	int32 AuthoredBoundaryEntryCount = 0;
	bool bAuthoredEntryStaysOnGroundLevel = true;
	for (const FLayoutPlannedCell& PlannedCell : FrozenAuthorityRequest.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry
			&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary)
		{
			++AuthoredBoundaryEntryCount;
			bAuthoredEntryStaysOnGroundLevel &= PlannedCell.Cell.Z == 0;
		}
	}
	TestEqual(TEXT("Authority freeze keeps exactly one perimeter Entry"), AuthoredBoundaryEntryCount, 1);
	TestTrue(TEXT("Perimeter Entry remains on ground level"), bAuthoredEntryStaysOnGroundLevel);
	const FIntVector OriginalTerrainSeamEntryCell = OriginalTerrainSeamEntry->Cell;
	TestTrue(
		TEXT("Authority freeze preserves internal terrain-seam Entry location"),
		FrozenAuthorityRequest.PlannedCells.ContainsByPredicate(
			[OriginalTerrainSeamEntryCell](const FLayoutPlannedCell& PlannedCell)
			{
				return PlannedCell.Cell == OriginalTerrainSeamEntryCell
					&& PlannedCell.Intent == ELayoutCellIntent::Entry
					&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam;
			}));

	FLayoutRegionSolveRequest PreparedRootRequest;
	ELayoutSolvePreparationFailureKind PreparationFailureKind =
		ELayoutSolvePreparationFailureKind::None;
	int32 PreparationCandidateAttemptCount = 0;
	if (!TestTrue(
		TEXT("Stepped two-child structural prewarm succeeds"),
		LayoutRegionScheduleSolverFacade::TryPrepareRequiredChildParentAuthorityWitness(
			Request,
			PreparedRootRequest,
			FailureReason,
			&PreparationFailureKind,
			&PreparationCandidateAttemptCount)))
	{
		AddError(FailureReason);
		return false;
	}

	int32 PreparedAuthoredBoundaryEntryCount = 0;
	bool bPreparedAuthoredEntryStaysOnGroundLevel = true;
	for (const FLayoutPlannedCell& PlannedCell : PreparedRootRequest.PlannedCells)
	{
		if (PlannedCell.Intent == ELayoutCellIntent::Entry
			&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::AuthoredBoundary)
		{
			++PreparedAuthoredBoundaryEntryCount;
			bPreparedAuthoredEntryStaysOnGroundLevel &= PlannedCell.Cell.Z == 0;
		}
	}
	TestEqual(
		TEXT("Structural prewarm keeps exactly one configured perimeter Entry"),
		PreparedAuthoredBoundaryEntryCount,
		1);
	TestTrue(
		TEXT("Structural prewarm keeps configured perimeter Entry on ground level"),
		bPreparedAuthoredEntryStaysOnGroundLevel);

	TestEqual(
		TEXT("Structural prewarm prepares both required child placements"),
		PreparedRootRequest.PreparedChildPlacementHints.Num(),
		2);
	return TestTrue(
		TEXT("Structural prewarm preserves internal terrain-seam Entry location"),
		PreparedRootRequest.PlannedCells.ContainsByPredicate(
			[OriginalTerrainSeamEntryCell](const FLayoutPlannedCell& PlannedCell)
			{
				return PlannedCell.Cell == OriginalTerrainSeamEntryCell
					&& PlannedCell.Intent == ELayoutCellIntent::Entry
					&& PlannedCell.EntryOrigin == ELayoutEntryOrigin::TerrainSeam;
			}));
}

bool FLayoutProfileJsonFixtureLargeFootprintSteppedSharedSeamPlacementStaysBoundedTest::RunTest(const FString& Parameters)
{
	FLayoutProfileJsonFixtureAssets ImportedAssets;
	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	if (!BuildLargeFootprintCenteredStepRequest(
		*this,
		TEXT("LayoutJsonLargeFootprintSteppedSharedSeamPlacementStaysBounded"),
		TEXT("LargeFootprintSteppedSharedSeam"),
		ImportedAssets,
		Request,
		FailureReason))
	{
		return false;
	}

	const double SolveStartSeconds = FPlatformTime::Seconds();
	const FLayoutRegionSolveScheduleResult ScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(Request);
	const double SolveDurationSeconds = FPlatformTime::Seconds() - SolveStartSeconds;
	TestTrue(
		TEXT("8x8 stepped shared-seam child placement stays bounded"),
		SolveDurationSeconds < 3.0);
	if (!TestTrue(TEXT("8x8 stepped shared-seam fixture solves"), ScheduleResult.bSucceeded))
	{
		AddError(ScheduleResult.FailureReason);
		return false;
	}
	return true;
}
