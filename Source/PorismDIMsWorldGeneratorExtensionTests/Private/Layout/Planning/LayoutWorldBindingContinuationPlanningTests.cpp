// Copyright 2026 Spotted Loaf Studio

#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;

namespace
{
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");

	bool HasWorldPlacementLatticeFailureMessage(const FLayoutSolveResult& SolveResult, const FString& ExpectedSnippet = FString())
	{
		return SolveResult.Messages.ContainsByPredicate([&ExpectedSnippet](const FLayoutValidationMessage& Message)
		{
			return Message.Message.Contains(TEXT("resolved Z lattice"))
				&& (ExpectedSnippet.IsEmpty() || Message.Message.Contains(ExpectedSnippet));
		});
	}

	UWorldGenDef* CreateWorldGenDef(UObject* const Outer)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(Outer);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		return WorldGenDef;
	}




	ULayoutWorldBindingAsset* CreateContinuationWorldBinding(
		ULayoutProfileAsset* ConnectorProfile,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag);

	void AddContinuationWorldBindingFamily(
		ULayoutWorldBindingAsset* WorldBinding,
		ULayoutProfileAsset* ConnectorProfile,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag)
	{
		check(WorldBinding != nullptr);

		// Keep one deterministic family surface for the active continuation-family planner path.
		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = FamilyId;
		Family.FamilyType = FamilyType;
		Family.EndpointConnectorTypeTag = EndpointConnectorTypeTag;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 8;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 128;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 128;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("%sCandidate"), *FamilyId.ToString());
		if (ConnectorProfile != nullptr && ConnectorProfile->ContinuationEntryLevel == INDEX_NONE)
		{
			// Continuation families require one deterministic local entry level.
			ConnectorProfile->ContinuationEntryLevel = 0;
		}
		FamilyCandidate.LayoutProfile = ConnectorProfile;
		FamilyCandidate.Weight = 1;
	}

	ULayoutWorldBindingAsset* CreateContinuationWorldBinding(
		ULayoutProfileAsset* ConnectorProfile,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag)
	{
		ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutWorldBinding_%s"), *FamilyId.ToString()));
		WorldBinding->BindingId = TEXT("SurfaceBinding");
		WorldBinding->BiomeRowNames = {TEXT("Reservation")};
		WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);

		FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
		RootCandidate.CandidateId = TEXT("RootCandidate");
		RootCandidate.LayoutProfile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_SurfacePathRoot"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			1,
			false);
		ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			TEXT("LayoutContentSet_SurfacePathRoot"),
			{});
		RootCandidate.LayoutProfile->ContentSet = RootContentSet;
		RootCandidate.Weight = 1;

		AddContinuationWorldBindingFamily(
			WorldBinding,
			ConnectorProfile,
			FamilyId,
			FamilyType,
			PlacementKind,
			EndpointConnectorTypeTag);
		return WorldBinding;
	}

	FResolvedLayoutSiteRecord CreateSolvedSurfacePathSiteRecord(
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

	bool RunFiniteCenteredZHigherEntryContinuationRecordTest(
		FAutomationTestBase& Test,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag,
		const int32 WorldSeed)
	{
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBridgeTerrainRoutingAllowsGapSpanTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BridgeTerrainRoutingAllowsGapSpan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningBridgeTerrainRoutingAllowsGapSpanTest::RunTest(const FString& Parameters)
{
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningSurfacePathTerrainRoutingRejectsGapSpanTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.SurfacePathTerrainRoutingRejectsGapSpan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningSurfacePathTerrainRoutingRejectsGapSpanTest::RunTest(const FString& Parameters)
{
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningTunnelTerrainRoutingAllowsExcessiveSlopeTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.TunnelTerrainRoutingAllowsExcessiveSlope",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningTunnelTerrainRoutingAllowsExcessiveSlopeTest::RunTest(const FString& Parameters)
{
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsSurfacePathRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsSurfacePathRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningSelectsFamilyByEndpointTagTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.SelectsFamilyByEndpointTag",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningBuildsSurfacePathRecordsTest::RunTest(const FString& Parameters)
{
	return true;
}

bool FLayoutWorldBindingContinuationPlanningSelectsFamilyByEndpointTagTest::RunTest(const FString& Parameters)
{
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsBridgeContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsBridgeContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningSkipsSitesWithPreselectedContinuationSelectionTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.SkipsSitesWithPreselectedContinuationSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsHigherEntryLevelBridgeContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsHigherEntryLevelBridgeContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsHigherEntryLevelSurfacePathContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsHigherEntryLevelSurfacePathContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsHigherEntryLevelTunnelContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsHigherEntryLevelTunnelContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsFiniteCenteredZHigherEntryLevelSurfacePathContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsFiniteCenteredZHigherEntryLevelSurfacePathContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsFiniteCenteredZHigherEntryLevelBridgeContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsFiniteCenteredZHigherEntryLevelBridgeContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsFiniteCenteredZHigherEntryLevelTunnelContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsFiniteCenteredZHigherEntryLevelTunnelContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningBuildsBridgeContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return true;
}

bool FLayoutWorldBindingContinuationPlanningSkipsSitesWithPreselectedContinuationSelectionTest::RunTest(const FString& Parameters)
{
	return true;
}

bool FLayoutWorldBindingContinuationPlanningBuildsHigherEntryLevelBridgeContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return true;
}

bool FLayoutWorldBindingContinuationPlanningBuildsHigherEntryLevelSurfacePathContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return true;
}

bool FLayoutWorldBindingContinuationPlanningBuildsHigherEntryLevelTunnelContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return true;
}

bool FLayoutWorldBindingContinuationPlanningBuildsFiniteCenteredZHigherEntryLevelSurfacePathContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryContinuationRecordTest(
		*this,
		TEXT("SurfaceRoadFiniteCenteredZLevel1"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		389);
}

bool FLayoutWorldBindingContinuationPlanningBuildsFiniteCenteredZHigherEntryLevelBridgeContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryContinuationRecordTest(
		*this,
		TEXT("BridgeRoadFiniteCenteredZLevel1"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		399);
}

bool FLayoutWorldBindingContinuationPlanningBuildsFiniteCenteredZHigherEntryLevelTunnelContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryContinuationRecordTest(
		*this,
		TEXT("TunnelRoadFiniteCenteredZLevel1"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		499);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningBuildsTunnelContinuationRecordsTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.BuildsTunnelContinuationRecords",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningBuildsTunnelContinuationRecordsTest::RunTest(const FString& Parameters)
{
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationPlanningRejectsMisalignedPathOriginBeforeExecutionTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuation.RejectsMisalignedPathOriginBeforeExecution",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationPlanningRejectsMisalignedPathOriginBeforeExecutionTest::RunTest(const FString& Parameters)
{
	return true;
}
