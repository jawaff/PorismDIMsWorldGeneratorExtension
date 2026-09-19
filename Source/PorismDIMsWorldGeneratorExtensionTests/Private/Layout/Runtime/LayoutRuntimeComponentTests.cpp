// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Types/LayoutGameplayTags.h"
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

	bool ExpectFrozenTerrainEvidenceInsideBounds(
		FAutomationTestBase& Test,
		const FLayoutFrozenTerrainBiomeAdapterInput& Artifact,
		const FIntVector& SharedCellSizeInBlocks,
		const TCHAR* const Context)
	{
		auto IsInsideBounds = [&Artifact](const FIntPoint& BlockXY)
		{
			return BlockXY.X >= Artifact.SearchMinBlockXY.X
				&& BlockXY.X <= Artifact.SearchMaxBlockXY.X
				&& BlockXY.Y >= Artifact.SearchMinBlockXY.Y
				&& BlockXY.Y <= Artifact.SearchMaxBlockXY.Y;
		};
		auto ResolveCellBlockXY = [&Artifact, &SharedCellSizeInBlocks](const FIntVector& Cell)
		{
			return FIntPoint(
				Artifact.FootprintMinBlockWorldPos.X + Cell.X * SharedCellSizeInBlocks.X,
				Artifact.FootprintMinBlockWorldPos.Y + Cell.Y * SharedCellSizeInBlocks.Y);
		};

		bool bPassed = true;
		bPassed &= Test.TestTrue(FString::Printf(TEXT("%s carries finite frozen search bounds"), Context), Artifact.bHasFiniteSearchBounds);
		for (int32 Index = 0; Index < Artifact.SurfaceSamples.Num(); ++Index)
		{
			bPassed &= Test.TestTrue(
				FString::Printf(TEXT("%s surface sample %d is inside frozen bounds"), Context, Index),
				IsInsideBounds(Artifact.SurfaceSamples[Index].BlockXY));
		}
		for (int32 Index = 0; Index < Artifact.BiomeOwnershipSamples.Num(); ++Index)
		{
			bPassed &= Test.TestTrue(
				FString::Printf(TEXT("%s biome ownership sample %d is inside frozen bounds"), Context, Index),
				IsInsideBounds(Artifact.BiomeOwnershipSamples[Index].BlockXY));
		}
		for (int32 Index = 0; Index < Artifact.SteppedSupportSamples.Num(); ++Index)
		{
			bPassed &= Test.TestTrue(
				FString::Printf(TEXT("%s stepped support sample %d is inside frozen bounds"), Context, Index),
				IsInsideBounds(ResolveCellBlockXY(Artifact.SteppedSupportSamples[Index].LocalCell)));
		}
		for (int32 Index = 0; Index < Artifact.FootprintClassification.CellClassifications.Num(); ++Index)
		{
			bPassed &= Test.TestTrue(
				FString::Printf(TEXT("%s footprint classification %d is inside frozen bounds"), Context, Index),
				IsInsideBounds(Artifact.FootprintClassification.CellClassifications[Index].BlockXY));
		}
		for (int32 Index = 0; Index < Artifact.TerrainPlacementCells.Num(); ++Index)
		{
			bPassed &= Test.TestTrue(
				FString::Printf(TEXT("%s placement evidence %d is inside frozen bounds"), Context, Index),
				IsInsideBounds(ResolveCellBlockXY(Artifact.TerrainPlacementCells[Index].Cell)));
		}
		return bPassed;
	}

	bool ExpectSteppedRuntimeConnectorRecord(
		FAutomationTestBase& Test,
		const TArray<FResolvedLayoutConnectorRecord>& ConnectorRecords,
		const FName FamilyId,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const bool bExpectRequestOwnedSteppedCarriers,
		const FIntVector& ExpectedMiddleSteppedCell = FIntVector(1, 0, 0))
	{
		const FResolvedLayoutConnectorRecord* const Record =
			ConnectorRecords.FindByPredicate(
				[&](const FResolvedLayoutConnectorRecord& Candidate)
				{
					return Candidate.ContinuationFamilyId == FamilyId;
				});
		if (!Test.TestNotNull(
			*FString::Printf(
				TEXT("Runtime refresh keeps stepped connector record %s"),
				*FamilyId.ToString()),
			Record))
		{
			return false;
		}
		Test.TestEqual(
			*FString::Printf(
				TEXT("Stepped runtime connector %s keeps the resolved continuation placement kind"),
				*FamilyId.ToString()),
			Record->ResolvedContinuationSelection.PlacementKind,
			PlacementKind);
		Test.TestTrue(
			*FString::Printf(
				TEXT("Stepped runtime connector %s keeps the stepped support summary satisfied"),
				*FamilyId.ToString()),
			Record->SolveResult.bCanSatisfySteppedTerrainTransitions);
		Test.TestTrue(
			*FString::Printf(
				TEXT("Stepped runtime connector %s preserves non-empty stepped support samples on the cached solve surface"),
				*FamilyId.ToString()),
			!Record->SolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty());
		Test.TestTrue(
			*FString::Printf(
				TEXT("Stepped runtime connector %s preserves a passing stepped prepared-contract assertion on the cached solve surface"),
				*FamilyId.ToString()),
			Record->SolveResult.ValidationAssertions.ContainsByPredicate(
				[](const FLayoutValidationAssertionRecord& Assertion)
				{
					return Assertion.AssertionId == TEXT("RegionRequest.RootTerrainSteppedPreparedSolveContractValid")
						&& Assertion.bPassed;
				}));
		Test.TestTrue(
			*FString::Printf(
				TEXT("Stepped runtime connector %s keeps no unsupported stepped-transition diagnostics"),
				*FamilyId.ToString()),
			Record->SolveResult.UnsupportedSteppedTerrainTransitionDiagnostics.IsEmpty());
		if (bExpectRequestOwnedSteppedCarriers)
		{
			Test.TestEqual(
				*FString::Printf(
					TEXT("Stepped runtime connector %s preserves non-empty forced insertions on the cached solve surface"),
					*FamilyId.ToString()),
				Record->SolveResult.ForcedPlacementBundleInsertions.Num(),
				1);
			Test.TestTrue(
				*FString::Printf(
					TEXT("Stepped runtime connector %s preserves non-empty route constraints on the cached solve surface"),
					*FamilyId.ToString()),
				Record->SolveResult.RequestOwnedRequiredRouteConstraints.Num() > 0);
				Test.TestTrue(
					*FString::Printf(
						TEXT("Stepped runtime connector %s keeps a request-owned stepped route obligation on the middle connector cell"),
						*FamilyId.ToString()),
				Record->SolveResult.RequestOwnedRequiredRouteConstraints.ContainsByPredicate(
					[ExpectedMiddleSteppedCell](const FLayoutRouteConstraintRecord& Constraint)
					{
						return Constraint.Cell == ExpectedMiddleSteppedCell;
					}));
		}
		else
		{
			Test.TestTrue(
				*FString::Printf(
					TEXT("Stepped runtime connector %s still publishes zero forced insertions on the cached solve surface"),
					*FamilyId.ToString()),
				Record->SolveResult.ForcedPlacementBundleInsertions.IsEmpty());
			Test.TestTrue(
				*FString::Printf(
					TEXT("Stepped runtime connector %s still publishes zero route constraints on the cached solve surface"),
					*FamilyId.ToString()),
				Record->SolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty());
		}
		const FLayoutPlacedModule* const MiddlePlacement =
			Record->SolveResult.Placements.FindByPredicate(
				[ExpectedMiddleSteppedCell](const FLayoutPlacedModule& Placement)
				{
					return Placement.Cell == ExpectedMiddleSteppedCell;
				});
		if (!Test.TestNotNull(
			*FString::Printf(
				TEXT("Stepped runtime connector %s publishes the middle stepped placement"),
				*FamilyId.ToString()),
			MiddlePlacement))
		{
			return false;
		}
		Test.TestEqual(
			*FString::Printf(
				TEXT("Stepped runtime connector %s keeps VerticalAccess intent on the middle stepped cell"),
				*FamilyId.ToString()),
			MiddlePlacement->Intent,
			ELayoutCellIntent::VerticalAccess);
		return true;
	}

	ULayoutProfileAsset* CreateContinuationTestProfile(ULayoutRegionContentSetAsset* ContentSet)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_RuntimeContinuation"),
			FIntPoint(2, 1),
			FIntPoint(2, 1),
			1,
			2,
			false);
		Profile->ContentSet = ContentSet;
		return Profile;
	}


	ULayoutProfileAsset* CreateSteppedContinuationTestProfile(
		ULayoutRegionContentSetAsset* ContentSet)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_RuntimeSteppedContinuation"),
			FIntPoint(3, 1),
			FIntPoint(3, 1),
			2,
			2,
			false);
		Profile->ContentSet = ContentSet;
		Profile->bSupportsSteppedTerrainSolve = true;
		Profile->bRestrictVerticalAccessModulesToVerticalAccessCells = true;
		return Profile;
	}

	FResolvedLayoutSiteRecord CreateRuntimeContinuationSiteRecord(
		const FIntVector& SiteCenterBlockWorldPos,
		const FName BindingId,
		const FGameplayTag EndpointConnectorTypeTag)
	{
		FResolvedLayoutSiteRecord SiteRecord;
		SiteRecord.bLayoutSolved = true;
		SiteRecord.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		SiteRecord.WorldBindingId = BindingId;
		SiteRecord.BiomeRowName = TEXT("Reservation");
		SiteRecord.SolveResult.FootprintSize = FIntPoint(1, 1);
		SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
		SiteRecord.ExportedEntryCells = {FIntVector::ZeroValue};
		SiteRecord.ExportedConnectorTypeTags = MakeTags({EndpointConnectorTypeTag});
		return SiteRecord;
	}

	void PopulateConnectorRootPublicationMetadataForTesting(
		FResolvedLayoutConnectorRecord& ConnectorRecord)
	{
		const FLayoutRootPublicationMetadata ExistingMetadata =
			ConnectorRecord.GetRootPublicationMetadata();
		if (!ExistingMetadata.RootPlacementPolicyId.IsNone()
			&& !ExistingMetadata.RootCandidateId.IsNone()
			&& !ExistingMetadata.RootSolveId.IsNone())
		{
			return;
		}

		const FString RegionDebugPath = FString::Printf(
			TEXT("Connector/%s/%s"),
			*ConnectorRecord.StartEndpointBlockWorldPos.ToString(),
			*ConnectorRecord.EndEndpointBlockWorldPos.ToString());
		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootPlacementPolicyId = TEXT("ConnectorExplicit");
		PublicationMetadata.RootCandidateId = FLayoutId(*RegionDebugPath);
		PublicationMetadata.RootSolveId = FLayoutId(*RegionDebugPath);
		ConnectorRecord.SetRootPublicationMetadata(PublicationMetadata);
	}

	bool BuildExpectedSteppedContinuationRecordFromActivePath(
		FAutomationTestBase& Test,
		UWorldGenDef* const WorldGenDef,
		ULayoutWorldBindingAsset* const WorldBinding,
		ULayoutProfileAsset* const Profile,
		const FName BiomeRowName,
		const FGameplayTag& ConnectorTypeTag,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FIntVector& StartEndpointBlockWorldPos,
		const FIntVector& EndEndpointBlockWorldPos,
		const int32 SolveSeed,
		FResolvedLayoutConnectorRecord& OutRecord)
	{
		OutRecord = FResolvedLayoutConnectorRecord();
		if (WorldGenDef == nullptr
			|| WorldBinding == nullptr
			|| Profile == nullptr
			|| WorldBinding->ContinuationFamilies.IsEmpty()
			|| WorldBinding->ContinuationFamilies[0].Candidates.IsEmpty())
		{
			Test.AddError(
				TEXT("Higher-entry stepped continuation parity helper requires a populated world binding, profile, and worldgen definition."));
			return false;
		}

		FLayoutActiveBiomeSampler ActiveBiomeSampler;
		if (!Test.TestTrue(
			TEXT("Higher-entry stepped continuation parity helper initializes the active-biome sampler"),
			ActiveBiomeSampler.Initialize(GetTransientPackage(), WorldGenDef, 0)))
		{
			return false;
		}

		FLayoutConnectorTerrainPathContext TerrainContext;
		TerrainContext.ActiveBiomeSampler = &ActiveBiomeSampler;
		TerrainContext.CoordinateSettings = MakeTestCoordinateSettings(WorldGenDef);

		const FLayoutWorldBindingContinuationFamily& Family =
			WorldBinding->ContinuationFamilies[0];
		const FLayoutWorldBindingContinuationCandidate& Candidate = Family.Candidates[0];

		OutRecord.WorldBindingId = WorldBinding->BindingId;
		OutRecord.BiomeRowName = BiomeRowName;
		OutRecord.ContinuationFamilyId = Family.FamilyId;
		OutRecord.ContinuationFamilyCandidateId = Candidate.CandidateId;
		OutRecord.PlacementKind = PlacementKind;
		OutRecord.ResolvedContinuationSelection.FamilyId = Family.FamilyId;
		OutRecord.ResolvedContinuationSelection.PlacementKind = PlacementKind;
		OutRecord.ResolvedContinuationSelection.ResolvedEntryLevel =
			Profile->ContinuationEntryLevel;
		OutRecord.TerrainPathSelection.bEnableTerrainAwarePathing = true;
		OutRecord.TerrainPathSelection.PathBiomeRowName = BiomeRowName;
		OutRecord.TerrainPathSelection.PathBiomeRowNames = {BiomeRowName};
		OutRecord.WorldBindingPlacementPolicy = WorldBinding->DefaultPlacementPolicy;
		if (Family.bOverrideTerrainTransitionPolicy)
		{
			OutRecord.WorldBindingPlacementPolicy.TerrainTransition =
				Family.TerrainTransitionPolicyOverride;
		}
		OutRecord.ContinuationPolicy = Family.ContinuationPolicy;
		OutRecord.SolveBudget = Family.SolveBudget;
// [MODULESET REMOVED]
		OutRecord.LayoutProfile =
			TSoftObjectPtr<ULayoutProfileAsset>(Profile);
		OutRecord.ConnectorTypeTag = ConnectorTypeTag;
		OutRecord.StartEndpointBlockWorldPos = StartEndpointBlockWorldPos;
		OutRecord.EndEndpointBlockWorldPos = EndEndpointBlockWorldPos;
		OutRecord.SolveSeed = SolveSeed;
		PopulateConnectorRootPublicationMetadataForTesting(OutRecord);

		FLayoutPreparedContinuationRoute PreparedRoute;
		FString RequestFailureReason;
		if (!Test.TestTrue(
			TEXT("Higher-entry stepped continuation parity helper prepares one terrain-aware connector route"),
			FLayoutConnectorPlanning::TryPrepareContinuationRoute(
				OutRecord,
				WorldBinding,
				SolveSeed,
				&TerrainContext,
				PreparedRoute,
				RequestFailureReason)))
		{
			if (!RequestFailureReason.IsEmpty())
			{
				Test.AddError(RequestFailureReason);
			}
			return false;
		}

		// Solve is now submitted through the background lifecycle chain (prewarm -> preflight -> solve).
		// The synchronous SolveConnectorRecord path has been deleted per RecursiveNegotiationDesign rule 6.

		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentWiringTest,
	"PorismExtension.Layout.Runtime.ComponentWiring",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeComponentWiringTest::RunTest(const FString& Parameters)
{
	AChunkWorldExtended* ChunkWorld = NewObject<AChunkWorldExtended>();
	TestNotNull(TEXT("Chunk world extended creates the layout runtime component"), ChunkWorld->GetLayoutRuntimeComponent());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersBuildExplicitRootRequestTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.BuildExplicitRootRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersBuildResolvedSiteRecordFromRuntimeSolveTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.BuildResolvedSiteRecordFromRuntimeSolve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingRuntimeHelpersBuildExplicitRootRequestTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutRuntimeHelper_Template"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_Module"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ContentEntry;
	ContentEntry.EntryId = TEXT("RuntimeHelperEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_ContentSet"),
		{ContentEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_Profile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 1.25f;
	const FIntVector SiteCenterBlockWorldPos(16, 32, 56);
	const int32 SolveSeed = 1337;
	const FString RegionDebugPath = FString::Printf(TEXT("DirectRoot/%s"), *SiteCenterBlockWorldPos.ToString());
	const FLayoutId RegionPathName(*RegionDebugPath);
	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 12;
	PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 34;
	PlacementPolicy.TerrainSampleGridSpacing = 5;
	PlacementPolicy.HeightIgnoreThreshold = 6;
	PlacementPolicy.TerrainTransition.bAllowFoundationFill = false;
	PlacementPolicy.TerrainTransition.MaxFoundationDepth = 8;
	const FLayoutWorldBindingRuntimeView ContentSetRuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);
	TestEqual(TEXT("Explicit-root runtime-view builder keeps the content set"), ContentSetRuntimeView.ContentSet, ContentSet);
// [MODULESET REMOVED]
	TestEqual(TEXT("Explicit-root runtime-view builder keeps the solve budget"), ContentSetRuntimeView.SolveBudget.MaxSolveDurationSeconds, SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Explicit-root runtime-view builder keeps the derived shared-cell size"), ContentSetRuntimeView.SharedCellSizeInBlocks, Module->GetEffectiveCellSizeInBlocks());
	TestEqual(TEXT("Explicit-root runtime-view builder keeps the placement kind"), ContentSetRuntimeView.PlacementKind, PlacementKind);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit-root helper builds a request from the shared runtime-view content-set path"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			ContentSetRuntimeView,
			SiteCenterBlockWorldPos,
			SolveSeed,
			Request,
			FailureReason));
	TestEqual(TEXT("Content-set request keeps the direct-root debug path"), Request.RegionDebugPath, RegionDebugPath);
	TestEqual(TEXT("Content-set request keeps the direct-root placement policy"), Request.RootPlacementPolicyId, FLayoutId(TEXT("DirectRootExplicit")));
	TestEqual<FLayoutId>(TEXT("Content-set request keeps the direct-root candidate id"), Request.RootCandidateId, RegionPathName);
	TestEqual<FLayoutId>(TEXT("Content-set request keeps the direct-root solve id"), Request.RootSolveId, RegionPathName);
	TestEqual(TEXT("Content-set request keeps the direct-root solve budget"), Request.ExecutionSettings.MaxSolveDurationSeconds, SolveBudget.MaxSolveDurationSeconds);
	TestEqual(TEXT("Direct-root requests keep the explicit placement kind"), Request.RootPlacementKind, PlacementKind);
	TestEqual(TEXT("Direct-root requests keep the explicit placement terrain search start Z"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ, PlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
	TestEqual(TEXT("Direct-root requests keep the explicit placement terrain search depth"), Request.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks, PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks);
	TestEqual(TEXT("Direct-root requests keep the explicit terrain sample spacing"), Request.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, PlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(TEXT("Direct-root requests keep the explicit height ignore threshold"), Request.WorldBindingPlacementPolicy.HeightIgnoreThreshold, PlacementPolicy.HeightIgnoreThreshold);
	TestEqual(TEXT("Direct-root requests keep explicit foundation-fill authorization"), Request.WorldBindingPlacementPolicy.TerrainTransition.bAllowFoundationFill, PlacementPolicy.TerrainTransition.bAllowFoundationFill);
	TestEqual(TEXT("Direct-root requests keep explicit max foundation depth"), Request.WorldBindingPlacementPolicy.TerrainTransition.MaxFoundationDepth, PlacementPolicy.TerrainTransition.MaxFoundationDepth);
	const FLayoutValidationAssertionRecord* LatticeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.WorldPlacementLatticeContractValid");
	});
	if (!TestNotNull(TEXT("Direct-root request records a world-placement lattice assertion"), LatticeAssertion))
	{
		return false;
	}
	TestEqual(TEXT("Direct-root lattice assertion uses the request-contract kind"), LatticeAssertion->AssertionKind, ELayoutValidationAssertionKind::RequestContractValid);
	TestTrue(TEXT("Direct-root lattice assertion passes for an aligned site center"), LatticeAssertion->bPassed);

	// Both sources must be absent to exercise missing content, not mismatched ownership.
	Profile->ContentSet = nullptr;
	Request = FLayoutRegionSolveRequest();
	FailureReason.Reset();
	const FLayoutWorldBindingRuntimeView MissingSourcesRuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			nullptr,
			SolveBudget,
			PlacementPolicy);
	TestFalse(
		TEXT("Explicit-root helper rejects a shared runtime view with no content sources"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			MissingSourcesRuntimeView,
			SiteCenterBlockWorldPos,
			SolveSeed,
			Request,
			FailureReason));
	TestTrue(TEXT("Missing source failure explains the direct-root requirement"), FailureReason.Contains(TEXT("requires a content set")));
	return true;
}

bool FLayoutWorldBindingRuntimeHelpersBuildResolvedSiteRecordFromRuntimeSolveTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutRuntimeHelper_RecordTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_RecordModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ContentEntry;
	ContentEntry.EntryId = TEXT("RuntimeHelperRecordEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_RecordContentSet"),
		{ContentEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_RecordProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 0.75f;
	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);
	RuntimeView.ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorRoad});

	const FIntVector SiteCenterBlockWorldPos(12, 24, 36);
	const int32 SolveSeed = 2468;
	FLayoutRegionSolveRequest SolveRequest;
	FString FailureReason;
	if (!TestTrue(
			TEXT("Resolved-site helper test builds the explicit runtime request first"),
			LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
				RuntimeView,
				SiteCenterBlockWorldPos,
				SolveSeed,
				SolveRequest,
				FailureReason)))
	{
		return false;
	}

	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FrontendSelection.WorldBindingId = TEXT("RuntimeBinding");
	FrontendSelection.WorldBindingCandidateId = TEXT("RuntimeCandidate");
	FrontendSelection.BiomeRowName = TEXT("Reservation");
	FrontendSelection.ResolvedContinuationSelection.FamilyId = TEXT("RuntimeContinuation");
	FrontendSelection.ResolvedContinuationSelection.PlacementKind = ELayoutWorldBindingPlacementKind::SurfacePath;
	FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel = 0;

	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.FootprintSize = FIntPoint(1, 1);
	SolveResult.SharedCellSizeInBlocks = RuntimeView.SharedCellSizeInBlocks;
	SolveResult.RootPlacementKind = SolveRequest.RootPlacementKind;

	FResolvedLayoutSiteLocationMetadata LocationMetadata;
	LocationMetadata.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
	LocationMetadata.SiteTags = MakeTags({LayoutGameplayTags::ConnectorRoad});
	const FResolvedLayoutSiteRecord SiteRecord =
		LayoutWorldBindingRuntimeHelpers::BuildResolvedSiteRecordFromRuntimeSolve(
			LocationMetadata,
			RuntimeView,
			SolveSeed,
			SolveRequest,
			SolveResult,
			&FrontendSelection);

	const FLayoutRootPublicationMetadata PublicationMetadata =
		SiteRecord.GetRootPublicationMetadata();
	const FLayoutSiteSolveSourceSelection SolveSourceSelection =
		SiteRecord.GetSiteSolveSourceSelection();
	const FResolvedLayoutSiteLocationMetadata ResolvedLocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutWorldBindingSiteFrontendSelection ResolvedFrontendSelection =
		SiteRecord.GetWorldBindingFrontendSelection();
	const FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	TestEqual(TEXT("Resolved-site helper keeps the explicit runtime solve id"), PublicationMetadata.RootSolveId, SolveRequest.RootSolveId);
	TestEqual(TEXT("Resolved-site helper keeps the explicit runtime candidate id"), PublicationMetadata.RootCandidateId, SolveRequest.RootCandidateId);
	TestEqual(TEXT("Resolved-site helper keeps the explicit runtime placement-policy id"), PublicationMetadata.RootPlacementPolicyId, SolveRequest.RootPlacementPolicyId);
	TestEqual(TEXT("Resolved-site helper keeps the explicit runtime solve seed"), SolveSourceSelection.SolveSeed, SolveSeed);
	TestTrue(TEXT("Resolved-site helper keeps the explicit runtime profile"), SolveSourceSelection.LayoutProfile == RuntimeView.LayoutProfile);
	TestTrue(TEXT("Resolved-site helper keeps the explicit runtime content set"), SolveSourceSelection.ContentSet == RuntimeView.ContentSet);
	TestTrue(
		TEXT("Resolved-site helper keeps the compiled exported connector contract on the solve-source carrier"),
		SolveSourceSelection.ExportedConnectorTypeTags.HasTagExact(LayoutGameplayTags::ConnectorRoad));
	TestEqual(TEXT("Resolved-site helper keeps the explicit runtime site center"), ResolvedLocationMetadata.SiteCenterBlockWorldPos, SiteCenterBlockWorldPos);
	TestTrue(TEXT("Resolved-site helper keeps the caller-owned site tags on the location carrier"), ResolvedLocationMetadata.SiteTags.HasTagExact(LayoutGameplayTags::ConnectorRoad));
	TestEqual(TEXT("Resolved-site helper keeps the caller-owned frontend binding id"), ResolvedFrontendSelection.WorldBindingId, FrontendSelection.WorldBindingId);
	TestEqual(TEXT("Resolved-site helper keeps the caller-owned frontend candidate id"), ResolvedFrontendSelection.WorldBindingCandidateId, FrontendSelection.WorldBindingCandidateId);
	TestEqual(TEXT("Resolved-site helper keeps the caller-owned frontend continuation family id"), ResolvedFrontendSelection.ResolvedContinuationSelection.FamilyId, FrontendSelection.ResolvedContinuationSelection.FamilyId);
	TestTrue(TEXT("Resolved-site helper keeps the solved payload"), SolvedPayload.SolveResult.bSucceeded);
	TestTrue(
		TEXT("Resolved-site helper publishes the compiled exported connector contract on the solved payload"),
		SolvedPayload.ExportedConnectorTypeTags.HasTagExact(LayoutGameplayTags::ConnectorRoad));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersBuildExplicitRootRequestLatticeAssertionTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.BuildExplicitRootRequestLatticeAssertion",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersPopulateExplicitRuntimeSteppedTerrainSupportOnRequestTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.PopulateExplicitRuntimeSteppedTerrainSupportOnRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersRejectsMisalignedExplicitRootSolveBeforeExecutionTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.RejectsMisalignedExplicitRootSolveBeforeExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupportTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.BuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersComposedExplicitRuntimeSteppedSupportMatchesManualFlowTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.ComposedExplicitRuntimeSteppedSupportMatchesManualFlow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingRuntimeHelpersBuildExplicitRootRequestLatticeAssertionTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutRuntimeHelper_LatticeTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_LatticeModule"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ContentEntry;
	ContentEntry.EntryId = TEXT("RuntimeHelperLatticeEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_LatticeContentSet"),
		{ContentEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_LatticeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutRootSolveBudgetSettings SolveBudget;
	const FIntVector SiteCenterBlockWorldPos(12, 34, 57);
	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit-root helper still builds a request for a misaligned site center"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			SiteCenterBlockWorldPos,
			1337,
			Request,
			FailureReason));

	const FLayoutValidationAssertionRecord* LatticeAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.WorldPlacementLatticeContractValid");
	});
	if (!TestNotNull(TEXT("Misaligned direct-root request records a world-placement lattice assertion"), LatticeAssertion))
	{
		return false;
	}
	TestFalse(TEXT("Misaligned direct-root request fails the lattice assertion"), LatticeAssertion->bPassed);
	TestTrue(TEXT("Misaligned direct-root request reports the site center in the lattice failure reason"), LatticeAssertion->FailureReason.Contains(TEXT("X=12 Y=34 Z=57")));
	return true;
}

bool FLayoutWorldBindingRuntimeHelpersPopulateExplicitRuntimeSteppedTerrainSupportOnRequestTest::RunTest(const FString& Parameters)
{
	UObject* Outer = GetTransientPackage();
	UWorldGenDef* const WorldGenDef = CreateWorldGenDef(Outer);
	WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

	FBiomeDualData Row;
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
	Row.DomainOver = 1.0f;
	Row.GenU_Mat1.AddDefaulted();
	WorldGenDef->WorldBiomes.Add(Row);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Explicit-runtime stepped-support helper initializes the active biome sampler"), Sampler.Initialize(Outer, WorldGenDef, 0));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutRuntimeHelper_SteppedSupportTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutRuntimeHelper_SteppedSupportModule"),
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
	ContentEntry.EntryId = TEXT("RuntimeHelperSteppedSupportEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutRuntimeHelper_SteppedSupportContentSet"),
		{ContentEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutRuntimeHelper_SteppedSupportProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 1.25f;
	const FIntVector SiteCenterBlockWorldPos(16, 8, 16);
	const int32 SolveSeed = 1337;

	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	PlacementPolicy.TerrainSampleGridSpacing = 16;
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit-runtime helper builds the baseline request before stepped support is attached"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			SiteCenterBlockWorldPos,
			SolveSeed,
			Request,
			FailureReason));

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	TestTrue(
		TEXT("Explicit-runtime helper materializes stepped support onto the supplied request"),
		LayoutWorldBindingRuntimeHelpers::TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
			RuntimeView,
			TEXT("Reservation"),
			SiteCenterBlockWorldPos,
			FIntPoint(2, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			Request,
			FailureReason));
	TestTrue(TEXT("Explicit-runtime stepped-support helper turns on supplied planned cells"), !Request.PlannedCells.IsEmpty());
	TestEqual(TEXT("Explicit-runtime stepped-support helper preserves the supplied footprint size"), Request.FootprintSize, FIntPoint(2, 1));
	TestEqual(TEXT("Explicit-runtime stepped-support helper preserves the supplied planned cells"), Request.PlannedCells.Num(), PlannedCells.Num());
	TestEqual(TEXT("Explicit-runtime stepped-support helper preserves one support sample per planned cell"), Request.SteppedTerrainSupportMap.SupportSamples.Num(), PlannedCells.Num());

	const FLayoutValidationAssertionRecord* SteppedSupportAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportContractValid");
	});
	if (!TestNotNull(TEXT("Explicit-runtime stepped-support helper refreshes the stepped support assertion"), SteppedSupportAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Explicit-runtime stepped-support helper leaves the stepped support assertion passing"), SteppedSupportAssertion->bPassed);

	const FLayoutValidationAssertionRecord* CoverageAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
	});
	if (!TestNotNull(TEXT("Explicit-runtime stepped-support helper refreshes the stepped support coverage assertion"), CoverageAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Explicit-runtime stepped-support helper leaves the stepped support coverage assertion passing"), CoverageAssertion->bPassed);

	auto Harness = PorismLayoutWorldTestUtilities::CreateChunkWorldHarness(nullptr);
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		if (Pass == 1)
		{
			Harness.World->SetBlockValueByBlockWorldPos(SiteCenterBlockWorldPos, 1, false);
		}
		FLayoutRegionSolveRequest WithWorld = Request;
		TestTrue(TEXT("Explicit support and entry approaches do not require loaded terrain"),
			LayoutWorldBindingRuntimeHelpers::TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
				RuntimeView, TEXT("Reservation"), SiteCenterBlockWorldPos, FIntPoint(2, 1), PlannedCells,
				MakeTestCoordinateSettings(WorldGenDef), Sampler, WithWorld, FailureReason, Harness.World));
		TestTrue(TEXT("World edits cannot replace the explicit noise support map"),
			FLayoutTerrainSampling::AreSteppedTerrainSupportMapsContractEquivalent(
				Request.SteppedTerrainSupportMap, WithWorld.SteppedTerrainSupportMap, &FailureReason));
		const auto& ExpectedCells = Request.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells;
		const auto& ActualCells = WithWorld.FrozenTerrainBiomeAdapterInput.TerrainPlacementCells;
		if (TestEqual(TEXT("World-independent entry evidence keeps every cell"), ActualCells.Num(), ExpectedCells.Num()))
		{
			for (int32 Index = 0; Index < ExpectedCells.Num(); ++Index)
			{
				TestEqual(TEXT("Entry verdict remains procedural after a saved edit"),
					ActualCells[Index].EntryTraversability, ExpectedCells[Index].EntryTraversability);
			}
		}
	}
	return true;
}

bool FLayoutWorldBindingRuntimeHelpersRejectsMisalignedExplicitRootSolveBeforeExecutionTest::RunTest(const FString& Parameters)
{
	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutRuntimeHelper_MisalignedLatticeTemplate"), FIntVector(8, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_MisalignedLatticeModule"),
		Template,
		{ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			FGameplayTagContainer(),
			FGameplayTagContainer(),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));

	FLayoutRegionContentEntry ContentEntry;
	ContentEntry.EntryId = TEXT("RuntimeHelperMisalignedLatticeEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_MisalignedLatticeContentSet"),
		{ContentEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_MisalignedLatticeProfile"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		1,
		false);
	Profile->ContentSet = ContentSet;

	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 24;
	PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 16;
	PlacementPolicy.TerrainSampleGridSpacing = 1;
	PlacementPolicy.HeightIgnoreThreshold = 0;

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 0.5f;

	const FIntVector SiteCenterBlockWorldPos(12, 34, 57);
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Explicit-root helper still builds a request for a misaligned site center"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			SiteCenterBlockWorldPos,
			1337,
			Request,
			FailureReason));

	const FLayoutRegionSolveScheduleResult ScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(Request);
	TestFalse(
		TEXT("Misaligned direct-root solve now rejects before recursive scheduling proceeds"),
		ScheduleResult.bSucceeded);
	TestTrue(
		TEXT("Misaligned direct-root solve reports the site center in the failure reason"),
		ScheduleResult.FailureReason.Contains(TEXT("X=12 Y=34 Z=57")));
	TestEqual(
		TEXT("Misaligned direct-root merged failure reason matches the schedule failure"),
		ScheduleResult.MergedSolveResult.FailureReason,
		ScheduleResult.FailureReason);
	TestTrue(
		TEXT("Misaligned direct-root failure preserves the failed lattice assertion on the merged result"),
		ScheduleResult.MergedSolveResult.ValidationAssertions.ContainsByPredicate(
			[](const FLayoutValidationAssertionRecord& Assertion)
			{
				return Assertion.AssertionId == TEXT("RegionRequest.WorldPlacementLatticeContractValid")
					&& !Assertion.bPassed;
			}));

	const FLayoutRegionSolveResult RegionResult =
		FLayoutProfileSolver::SolveRegion(Request);
	TestFalse(
		TEXT("Misaligned direct-root leaf solve also rejects before execution proceeds"),
		RegionResult.SolveResult.bSucceeded);
	TestTrue(
		TEXT("Misaligned direct-root leaf solve reports the site center in the failure reason"),
		RegionResult.SolveResult.FailureReason.Contains(TEXT("X=12 Y=34 Z=57")));
	return true;
}

bool FLayoutWorldBindingRuntimeHelpersBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupportTest::RunTest(const FString& Parameters)
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
	WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Composed explicit-runtime stepped-support helper initializes the active biome sampler"), Sampler.Initialize(Outer, WorldGenDef, 0));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutRuntimeHelper_ComposedSteppedSupportTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutRuntimeHelper_ComposedSteppedSupportModule"),
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
	ContentEntry.EntryId = TEXT("RuntimeHelperComposedSteppedSupportEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutRuntimeHelper_ComposedSteppedSupportContentSet"),
		{ContentEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutRuntimeHelper_ComposedSteppedSupportProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 1.25f;
	const FIntVector SiteCenterBlockWorldPos(16, 8, 16);
	const int32 SolveSeed = 1776;

	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	PlacementPolicy.TerrainSampleGridSpacing = 16;
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutRegionSolveRequest Request;
	FString FailureReason;
	TestTrue(
		TEXT("Composed explicit-runtime helper builds the runtime request and stepped support on the same request surface"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
			RuntimeView,
			TEXT("Reservation"),
			SiteCenterBlockWorldPos,
			SolveSeed,
			FIntPoint(2, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			Request,
			FailureReason));
	TestEqual(TEXT("Composed explicit-runtime helper keeps the direct-root placement policy id"), Request.RootPlacementPolicyId, FLayoutId(TEXT("DirectRootExplicit")));
	TestEqual(TEXT("Composed explicit-runtime helper keeps the direct-root solve seed"), Request.Seed, SolveSeed);
	TestTrue(TEXT("Composed explicit-runtime helper turns on supplied planned cells"), !Request.PlannedCells.IsEmpty());
	TestEqual(TEXT("Composed explicit-runtime helper preserves one support sample per planned cell"), Request.SteppedTerrainSupportMap.SupportSamples.Num(), PlannedCells.Num());

	const FLayoutValidationAssertionRecord* CoverageAssertion = Request.ValidationAssertions.FindByPredicate([](const FLayoutValidationAssertionRecord& Assertion)
	{
		return Assertion.AssertionId == TEXT("RegionRequest.SteppedTerrainSupportCoverageContractValid");
	});
	if (!TestNotNull(TEXT("Composed explicit-runtime helper refreshes the stepped support coverage assertion"), CoverageAssertion))
	{
		return false;
	}
	TestTrue(TEXT("Composed explicit-runtime helper leaves the stepped support coverage assertion passing"), CoverageAssertion->bPassed);
	return true;
}

bool FLayoutWorldBindingRuntimeHelpersComposedExplicitRuntimeSteppedSupportMatchesManualFlowTest::RunTest(const FString& Parameters)
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
	WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

	FLayoutActiveBiomeSampler Sampler;
	TestTrue(TEXT("Explicit-runtime parity test initializes the active biome sampler"), Sampler.Initialize(Outer, WorldGenDef, 0));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutRuntimeHelper_ComposedSteppedSupportParityTemplate"), FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutRuntimeHelper_ComposedSteppedSupportParityModule"),
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
	ContentEntry.EntryId = TEXT("RuntimeHelperComposedSteppedSupportParityEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutRuntimeHelper_ComposedSteppedSupportParityContentSet"),
		{ContentEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutRuntimeHelper_ComposedSteppedSupportParityProfile"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds = 1.25f;
	const FIntVector SiteCenterBlockWorldPos(16, 8, 16);
	const int32 SolveSeed = 1888;

	FLayoutWorldBindingPlacementPolicy PlacementPolicy;
	const ELayoutWorldBindingPlacementKind PlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	PlacementPolicy.TerrainSampleGridSpacing = 16;
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	TArray<FLayoutPlannedCell> PlannedCells;
	PlannedCells.Add({FIntVector(0, 0, 0), ELayoutCellIntent::Entry});
	PlannedCells.Add({FIntVector(1, 0, 0), ELayoutCellIntent::Interior});

	FLayoutRegionSolveRequest ManualRequest;
	FString ManualFailureReason;
	TestTrue(
		TEXT("Explicit-runtime parity test builds the baseline request"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequest(
			RuntimeView,
			SiteCenterBlockWorldPos,
			SolveSeed,
			ManualRequest,
			ManualFailureReason));
	TestTrue(
		TEXT("Explicit-runtime parity test materializes stepped support onto the baseline request"),
		LayoutWorldBindingRuntimeHelpers::TryPopulateExplicitRuntimeSteppedTerrainSupportOnRequest(
			RuntimeView,
			TEXT("Reservation"),
			SiteCenterBlockWorldPos,
			FIntPoint(2, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			ManualRequest,
			ManualFailureReason));

	FLayoutRegionSolveRequest ComposedRequest;
	FString ComposedFailureReason;
	TestTrue(
		TEXT("Explicit-runtime parity test builds the composed stepped-support request"),
		LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
			RuntimeView,
			TEXT("Reservation"),
			SiteCenterBlockWorldPos,
			SolveSeed,
			FIntPoint(2, 1),
			PlannedCells,
			MakeTestCoordinateSettings(WorldGenDef),
			Sampler,
			ComposedRequest,
			ComposedFailureReason));

	bool bPassed = ExpectEquivalentSteppedSupportRequest(*this, ManualRequest, ComposedRequest, TEXT("Explicit-runtime composed stepped-support helper"));
	bPassed &= ExpectFrozenTerrainEvidenceInsideBounds(
		*this,
		ComposedRequest.FrozenTerrainBiomeAdapterInput,
		RuntimeView.SharedCellSizeInBlocks,
		TEXT("Explicit-runtime composed stepped-support helper"));
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingRuntimeHelpersResolveSharedCellSizeTest,
	"PorismExtension.Layout.Runtime.WorldBindingHelpers.ResolveSharedCellSize",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingRuntimeHelpersResolveSharedCellSizeTest::RunTest(const FString& Parameters)
{
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutRuntimeHelper_SharedCellContentSet"),
		{});


	FLayoutSolveResult SolveResult;
	SolveResult.SharedCellSizeInBlocks = FIntVector(11, 11, 11);
	TestEqual(
		TEXT("Solve-result shared cell size wins over live content/module assets"),
		LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(SolveResult, ContentSet),
		SolveResult.SharedCellSizeInBlocks);

	SolveResult.SharedCellSizeInBlocks = FIntVector::ZeroValue;
	TestEqual(
		TEXT("Content-set shared cell size is the fallback when the content set has no derived structural metrics"),
		LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(SolveResult, ContentSet),
		FIntVector::ZeroValue);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentComputesPlacementAnchorsTest,
	"PorismExtension.Layout.Runtime.ComputesPlacementAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeComponentComputesPlacementAnchorsTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutSiteRecord SiteRecord;
	FResolvedLayoutSiteLocationMetadata LocationMetadata;
	LocationMetadata.SiteCenterBlockWorldPos = FIntVector(320, 640, 20);
	SiteRecord.SetResolvedSiteLocationMetadata(LocationMetadata);
	FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	SolvedPayload.SolveResult.FootprintSize = FIntPoint(3, 3);

	FLayoutPlacedModule Placement;
	Placement.Cell = FIntVector(2, 1, 1);
	SolvedPayload.SolveResult.Placements.Add(Placement);
	SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);

	const FIntVector SharedCellSize(16, 16, 16);
	const FIntVector FootprintMin = UChunkWorldLayoutRuntimeComponent::ComputeFootprintMinBlockWorldPos(SiteRecord, SharedCellSize);
	const FIntVector PlacementAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(SiteRecord, Placement, SharedCellSize);
	const TSet<FIntVector> RequiredChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
		SiteRecord,
		SharedCellSize,
		FIntVector(32, 32, 32));

	TestEqual(TEXT("Footprint origin centers the solved footprint around the site center"), FootprintMin, FIntVector(296, 616, 20));
	TestEqual(TEXT("Placement anchor offsets from the footprint origin using cell size"), PlacementAnchor, FIntVector(328, 632, 36));
	TestTrue(TEXT("Required chunk origins include the chunk derived from the location-carrier site center and solved placement anchor"), RequiredChunkOrigins.Contains(FIntVector(320, 608, 32)));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentAppliesTemplatePlacementZOffsetToAnchorsTest,
	"PorismExtension.Layout.Runtime.AppliesTemplatePlacementZOffsetToAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeComponentAppliesTemplatePlacementZOffsetToAnchorsTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutSiteRecord SiteRecord;
	FResolvedLayoutSiteLocationMetadata LocationMetadata;
	LocationMetadata.SiteCenterBlockWorldPos = FIntVector(320, 640, 20);
	SiteRecord.SetResolvedSiteLocationMetadata(LocationMetadata);
	FResolvedLayoutSiteSolvedPayload SolvedPayload =
		SiteRecord.GetResolvedSiteSolvedPayload();
	SolvedPayload.SolveResult.FootprintSize = FIntPoint(3, 3);
	SolvedPayload.SolveResult.TemplatePlacementZOffsetBlocks = -2;
	SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);

	FLayoutPlacedModule SitePlacement;
	SitePlacement.Cell = FIntVector(2, 1, 1);
	const FIntVector SharedCellSize(16, 16, 16);

	const FIntVector SitePlacementAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
		SiteRecord,
		SitePlacement,
		SharedCellSize);
	TestEqual(TEXT("Site placement anchor applies the solve-result template placement Z offset"), SitePlacementAnchor, FIntVector(328, 632, 34));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentSkipsCommittedOrUnsolvedConnectorsTest,
	"PorismExtension.Layout.Runtime.SkipsCommittedOrUnsolvedConnectors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeComponentSkipsCommittedOrUnsolvedConnectorsTest::RunTest(const FString& Parameters)
{
	UChunkWorldLayoutRuntimeComponent* RuntimeComponent = NewObject<UChunkWorldLayoutRuntimeComponent>();
	TestNotNull(TEXT("Layout runtime component is created"), RuntimeComponent);

	FResolvedLayoutConnectorRecord UnsolvedRecord;
	UnsolvedRecord.bLayoutSolved = false;

	FResolvedLayoutConnectorRecord SolvedRecord;
	SolvedRecord.bLayoutSolved = true;

	FResolvedLayoutConnectorRecord CommittedRecord;
	CommittedRecord.bLayoutSolved = true;
	CommittedRecord.bHasBeenCommittedToChunkWorld = true;

	TestFalse(TEXT("Unsolved connectors are not eligible for realization"), RuntimeComponent->ShouldAttemptConnectorRealization(UnsolvedRecord));
	TestTrue(TEXT("Solved uncommitted connectors remain eligible for realization"), RuntimeComponent->ShouldAttemptConnectorRealization(SolvedRecord));
	TestFalse(TEXT("Committed connectors are skipped by the realization pass"), RuntimeComponent->ShouldAttemptConnectorRealization(CommittedRecord));
	return true;
}
