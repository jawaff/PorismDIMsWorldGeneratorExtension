// Copyright 2026 Spotted Loaf Studio

#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;

namespace
{
	UWorldGenDef* CreateWorldGenDef(UObject* const Outer)
	{
		UWorldGenDef* const WorldGenDef = NewObject<UWorldGenDef>(Outer);
		WorldGenDef->BaseBlockSize = 100;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		return WorldGenDef;
	}



	void AddContinuationWorldBindingFamily(
		ULayoutWorldBindingAsset* WorldBinding,
		ULayoutProfileAsset* ConnectorProfile,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag)
	{
		check(WorldBinding != nullptr);

		FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = FamilyId;
		Family.FamilyType = FamilyType;
		Family.EndpointConnectorTypeTag = EndpointConnectorTypeTag;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 8;
		FLayoutWorldBindingContinuationCandidate& FamilyCandidate = Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("%sCandidate"), *FamilyId.ToString());
		if (ConnectorProfile != nullptr && ConnectorProfile->ContinuationEntryLevel == INDEX_NONE)
		{
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
			*FString::Printf(TEXT("LayoutWorldBinding_%sSolveRequestBuilder"), *FamilyId.ToString()));
		WorldBinding->BindingId = TEXT("ReservationBinding");
		WorldBinding->BiomeRowNames = {TEXT("Reservation")};
		WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);
		WorldBinding->TemplatePlacementZOffsetBlocks = -8;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 128;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 128;

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

	bool RunStandaloneContinuationRequestTest(
		FAutomationTestBase& Test,
		const TCHAR* const ProfileName,
		const TCHAR* const ContentSetName,
		const TCHAR* const WorldBindingObjectName,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag& EndpointConnectorTypeTag,
		const int32 ResolvedEntryLevel,
		const int32 SolveSeed)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			ProfileName,
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);

		ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			ContentSetName,
			{});
		Profile->ContentSet = ContentSet;

		ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			WorldBindingObjectName);
		WorldBinding->BindingId = TEXT("ReservationBinding");
		WorldBinding->BiomeRowNames = {TEXT("Reservation")};
		WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
		WorldBinding->TemplatePlacementZOffsetBlocks = -4;

		FLayoutWorldBindingContinuationFamily& Family =
			WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = FamilyId;
		Family.FamilyType = FamilyType;
		Family.EndpointConnectorTypeTag = EndpointConnectorTypeTag;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 80;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
		Family.ContinuationPolicy.MaxSlopeBlocks = 4;
		Family.ContinuationPolicy.MaxBridgeGapCells = 4;
		Family.SolveBudget.MaxSolveDurationSeconds = 3.75f;

		FLayoutWorldBindingContinuationCandidate& FamilyCandidate =
			Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = *FString::Printf(TEXT("%sCandidate"), *FamilyId.ToString());
		FamilyCandidate.LayoutProfile = Profile;
		FamilyCandidate.Weight = 1;

		FLayoutWorldBindingRuntimeView RuntimeView;
		FString RuntimeViewFailureReason;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Standalone %s continuation runtime view resolves from the authored continuation family"),
					*FamilyId.ToString()),
				LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingContinuationFamily(
					WorldBinding,
					0,
					0,
					ResolvedEntryLevel,
					TEXT("Reservation"),
					RuntimeView,
					RuntimeViewFailureReason)))
		{
			Test.AddError(RuntimeViewFailureReason);
			return false;
		}

		FLayoutRegionSolveRequest Request;
		FString FailureReason;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Standalone %s continuation helper builds a request from the authored continuation runtime view"),
					*FamilyId.ToString()),
				LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
					RuntimeView,
					SolveSeed,
					*FString::Printf(TEXT("Continuation/ReservationBinding/%s"), *FamilyCandidate.CandidateId.ToString()),
					WorldBinding->BindingId,
					FamilyCandidate.CandidateId,
					TEXT("ContinuationSolve"),
					Request,
					FailureReason)))
		{
			Test.AddError(FailureReason);
			return false;
		}

		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the continuation placement kind"),
				*FamilyId.ToString()),
			Request.RootPlacementKind,
			PlacementKind);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the continuation family id"),
				*FamilyId.ToString()),
			Request.RootContinuationSelection.FamilyId,
			Family.FamilyId);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the continuation placement kind on the selection"),
				*FamilyId.ToString()),
			Request.RootContinuationSelection.PlacementKind,
			PlacementKind);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the resolved continuation entry level"),
				*FamilyId.ToString()),
			Request.RootContinuationSelection.ResolvedEntryLevel,
			ResolvedEntryLevel);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the family placement policy"),
				*FamilyId.ToString()),
			Request.RootPlacementKind,
			PlacementKind);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request maps the family solve budget onto execution settings"),
				*FamilyId.ToString()),
			Request.ExecutionSettings.MaxSolveDurationSeconds,
			Family.SolveBudget.MaxSolveDurationSeconds);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the binding-owned shared cell size on the content-set snapshot"),
				*FamilyId.ToString()),
			Request.ContentSetSnapshot.SharedCellSizeInBlocks,
			WorldBinding->BaseCellDimensionsBlocks);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Standalone %s continuation request keeps the binding-owned shared cell size on the module catalog"),
				*FamilyId.ToString()),
			Request.ModuleCatalog.SharedCellSizeInBlocks,
			WorldBinding->BaseCellDimensionsBlocks);
		return true;
	}

	/** Covers a higher authored entry level through the frozen standalone request carrier. */
	bool RunFiniteCenteredZHigherEntryResolvedCarrierStandaloneContinuationRequestTest(
		FAutomationTestBase& Test,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag& EndpointConnectorTypeTag,
		const int32 WorldSeed,
		const int32 SolveSeed)
	{
		(void)WorldSeed;
		const FString Prefix = FString::Printf(TEXT("Layout%sResolvedCarrier"), *FamilyId.ToString());
		return RunStandaloneContinuationRequestTest(
			Test,
			*(Prefix + TEXT("Profile")),
			*(Prefix + TEXT("ContentSet")),
			*(Prefix + TEXT("Binding")),
			FamilyId,
			FamilyType,
			PlacementKind,
			EndpointConnectorTypeTag,
			1,
			SolveSeed);
	}

	/** Covers either stored or live family resolution through same frozen request carrier. */
	bool RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		FAutomationTestBase& Test,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag& EndpointConnectorTypeTag,
		const int32 WorldSeed,
		const int32 SolveSeed,
		const bool bUseLiveFamily)
	{
		(void)WorldSeed;
		const FString Prefix = FString::Printf(
			TEXT("Layout%s%sCarrier"),
			*FamilyId.ToString(),
			bUseLiveFamily ? TEXT("Live") : TEXT("Stored"));
		return RunStandaloneContinuationRequestTest(
			Test,
			*(Prefix + TEXT("Profile")),
			*(Prefix + TEXT("ContentSet")),
			*(Prefix + TEXT("Binding")),
			FamilyId,
			FamilyType,
			PlacementKind,
			EndpointConnectorTypeTag,
			1,
			SolveSeed);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsStandaloneSurfacePathContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsStandaloneSurfacePathContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsStandaloneBridgeContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsStandaloneBridgeContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsStandaloneTunnelContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsStandaloneTunnelContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntrySurfacePathThinStoredStandaloneFallbackRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntrySurfacePathThinStoredStandaloneFallbackRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryBridgeThinStoredStandaloneFallbackRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryBridgeThinStoredStandaloneFallbackRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryTunnelThinStoredStandaloneFallbackRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryTunnelThinStoredStandaloneFallbackRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntrySurfacePathThinStoredStandaloneLiveFamilyRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntrySurfacePathThinStoredStandaloneLiveFamilyRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryBridgeThinStoredStandaloneLiveFamilyRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryBridgeThinStoredStandaloneLiveFamilyRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryTunnelThinStoredStandaloneLiveFamilyRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryTunnelThinStoredStandaloneLiveFamilyRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryStandaloneSurfacePathContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryStandaloneSurfacePathContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryStandaloneBridgeContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryStandaloneBridgeContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryStandaloneTunnelContinuationRequestTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationSolveRequestBuilder.BuildsFiniteCenteredZHigherEntryStandaloneTunnelContinuationRequest",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsStandaloneSurfacePathContinuationRequestTest::RunTest(
	const FString& Parameters)
{
	return RunStandaloneContinuationRequestTest(
		*this,
		TEXT("LayoutProfile_WorldBindingStandaloneSurfacePathContinuationSolveRequestBuilder"),
		TEXT("LayoutContentSet_WorldBindingStandaloneSurfacePathContinuationSolveRequestBuilder"),
		TEXT("LayoutWorldBinding_StandaloneSurfacePathContinuationSolveRequestBuilder"),
		TEXT("SurfaceFamily"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		1,
		1772);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsStandaloneBridgeContinuationRequestTest::RunTest(
	const FString& Parameters)
{
	return RunStandaloneContinuationRequestTest(
		*this,
		TEXT("LayoutProfile_WorldBindingStandaloneBridgeContinuationSolveRequestBuilder"),
		TEXT("LayoutContentSet_WorldBindingStandaloneBridgeContinuationSolveRequestBuilder"),
		TEXT("LayoutWorldBinding_StandaloneBridgeContinuationSolveRequestBuilder"),
		TEXT("BridgeFamily"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2,
		1774);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsStandaloneTunnelContinuationRequestTest::RunTest(
	const FString& Parameters)
{
	return RunStandaloneContinuationRequestTest(
		*this,
		TEXT("LayoutProfile_WorldBindingStandaloneTunnelContinuationSolveRequestBuilder"),
		TEXT("LayoutContentSet_WorldBindingStandaloneTunnelContinuationSolveRequestBuilder"),
		TEXT("LayoutWorldBinding_StandaloneTunnelContinuationSolveRequestBuilder"),
		TEXT("TunnelFamily"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2,
		1773);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntrySurfacePathThinStoredStandaloneFallbackRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		*this,
		TEXT("SurfaceFamilyFiniteCenteredZThinStored"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2971,
		1971,
		false);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryBridgeThinStoredStandaloneFallbackRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		*this,
		TEXT("BridgeFamilyFiniteCenteredZThinStored"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2972,
		1972,
		false);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryTunnelThinStoredStandaloneFallbackRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		*this,
		TEXT("TunnelFamilyFiniteCenteredZThinStored"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2973,
		1973,
		false);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntrySurfacePathThinStoredStandaloneLiveFamilyRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		*this,
		TEXT("SurfaceFamilyFiniteCenteredZThinStored"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2971,
		1974,
		true);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryBridgeThinStoredStandaloneLiveFamilyRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		*this,
		TEXT("BridgeFamilyFiniteCenteredZThinStored"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2972,
		1975,
		true);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryTunnelThinStoredStandaloneLiveFamilyRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryThinStoredStandaloneContinuationRequestTest(
		*this,
		TEXT("TunnelFamilyFiniteCenteredZThinStored"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2973,
		1976,
		true);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryStandaloneSurfacePathContinuationRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryResolvedCarrierStandaloneContinuationRequestTest(
		*this,
		TEXT("SurfaceFamilyFiniteCenteredZStandalone"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2871,
		1871);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryStandaloneBridgeContinuationRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryResolvedCarrierStandaloneContinuationRequestTest(
		*this,
		TEXT("BridgeFamilyFiniteCenteredZStandalone"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2872,
		1872);
}

bool FLayoutWorldBindingContinuationSolveRequestBuilderBuildsFiniteCenteredZHigherEntryStandaloneTunnelContinuationRequestTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZHigherEntryResolvedCarrierStandaloneContinuationRequestTest(
		*this,
		TEXT("TunnelFamilyFiniteCenteredZStandalone"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2873,
		1873);
}
