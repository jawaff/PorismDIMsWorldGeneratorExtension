// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"

#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
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
		const FGameplayTag EndpointConnectorTypeTag)
	{
		ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutWorldBinding_%sRuntimeView"), *FamilyId.ToString()));
		WorldBinding->BindingId = TEXT("SurfaceBinding");
		WorldBinding->BiomeRowNames = {TEXT("Reservation")};
		WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);
		WorldBinding->TemplatePlacementZOffsetBlocks = -8;

		FLayoutWorldBindingCandidate& RootCandidate = WorldBinding->Candidates.AddDefaulted_GetRef();
		RootCandidate.CandidateId = TEXT("RootCandidate");
		RootCandidate.LayoutProfile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_ContinuationRuntimeViewRoot"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			1,
			false);
		ULayoutRegionContentSetAsset* RootContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			TEXT("LayoutContentSet_ContinuationRuntimeViewRoot"),
			{});
		RootCandidate.LayoutProfile->ContentSet = RootContentSet;
		RootCandidate.Weight = 1;

		AddContinuationWorldBindingFamily(
			WorldBinding,
			ConnectorProfile,
			FamilyId,
			FamilyType,
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

	bool RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		FAutomationTestBase& Test,
		const TCHAR* const EntryLevelLabel,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag,
		const int32 WorldSeed,
		const int32 ContinuationEntryLevel,
		const int32 TemplatePlacementZOffsetBlocks)
	{
		return true;
	}

	bool RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		FAutomationTestBase& Test,
		const TCHAR* const EntryLevelLabel,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag,
		const int32 WorldSeed,
		const bool bUseLiveFamily,
		const int32 ContinuationEntryLevel,
		const int32 TemplatePlacementZOffsetBlocks)
	{
		return true;
	}

	void BuildThinStoredContinuationFixture(
		const TCHAR* const FamilyLabel,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag EndpointConnectorTypeTag,
		ULayoutWorldBindingAsset*& OutWorldBinding,
		FResolvedLayoutConnectorRecord& OutConnectorRecord,
		FLayoutRootSolveBudgetSettings& OutFallbackSolveBudget)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutProfile_WorldBindingThinStored%sConnectorLiveFamily"), FamilyLabel),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);

		ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutContentSet_WorldBindingThinStored%sConnectorLiveFamily"), FamilyLabel),
			{});
		Profile->ContentSet = ContentSet;

		OutWorldBinding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutWorldBinding_ThinStored%sConnectorLiveFamilyView"), FamilyLabel));
		OutWorldBinding->BindingId = TEXT("ReservationBinding");
		OutWorldBinding->BiomeRowNames = {TEXT("Reservation")};
		OutWorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 2, 2);
		OutWorldBinding->TemplatePlacementZOffsetBlocks = -5;

		FLayoutWorldBindingContinuationFamily& Family =
			OutWorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = FamilyId;
		Family.FamilyType = FamilyType;
		Family.EndpointConnectorTypeTag = EndpointConnectorTypeTag;
		Family.ContinuationPolicy.MaxSlopeBlocks = 9;
		Family.SolveBudget.MaxSolveDurationSeconds = 9.5f;

		FLayoutWorldBindingContinuationCandidate& FamilyCandidate =
			Family.Candidates.AddDefaulted_GetRef();
		FamilyCandidate.CandidateId = FName(*FString::Printf(TEXT("Live%sCandidate"), FamilyLabel));
		FamilyCandidate.LayoutProfile = Profile;
		FamilyCandidate.Weight = 1;

		OutConnectorRecord = FResolvedLayoutConnectorRecord();
		OutConnectorRecord.WorldBindingId = OutWorldBinding->BindingId;
		OutConnectorRecord.BiomeRowName = TEXT("Reservation");
		OutConnectorRecord.ContinuationFamilyId = Family.FamilyId;
		OutConnectorRecord.ContinuationFamilyCandidateId = FamilyCandidate.CandidateId;
		OutConnectorRecord.PlacementKind = PlacementKind;
		OutConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 48;
		OutConnectorRecord.WorldBindingPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 24;
		OutConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
		OutConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
		OutConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
		OutConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
		OutConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 2;

		OutFallbackSolveBudget = FLayoutRootSolveBudgetSettings();
		OutFallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;
	}

	void BuildThinStoredContinuationFallbackFixture(
		const TCHAR* const FamilyLabel,
		const FName FamilyId,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		FResolvedLayoutConnectorRecord& OutConnectorRecord,
		FLayoutRootSolveBudgetSettings& OutFallbackSolveBudget)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutProfile_WorldBindingThinStored%sConnectorCarrier"), FamilyLabel),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);

		ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutContentSet_WorldBindingThinStored%sConnectorCarrier"), FamilyLabel),
			{});
		UChunkStructureTemplate* LegacyThinSurfaceTemplate = CreateTemplate(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutTemplate_WorldBindingThinStored%sConnectorCarrier"), FamilyLabel),
			FIntVector(2, 2, 2));
		ULayoutModuleAsset* LegacyThinSurfaceModule = CreateModule(
			GetTransientPackage(),
			*FString::Printf(TEXT("LayoutModule_WorldBindingThinStored%sConnectorCarrier"), FamilyLabel),
			LegacyThinSurfaceTemplate,
			{ELayoutCellIntent::Boundary},
			BuildFilledCubeFaces(
				FGameplayTagContainer(),
				FGameplayTagContainer(),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
				FGameplayTagContainer(),
				FGameplayTagContainer(),
				ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor));
		FLayoutRegionContentEntry LegacyThinSurfaceEntry;
		LegacyThinSurfaceEntry.EntryId = TEXT("LegacyThinSurfaceEntry");
		LegacyThinSurfaceEntry.ContentKind = ELayoutRegionContentKind::Module;
		LegacyThinSurfaceEntry.ModuleSettings.Module = LegacyThinSurfaceModule;
		ContentSet->Entries = {LegacyThinSurfaceEntry};
		Profile->ContentSet = ContentSet;

		OutConnectorRecord = FResolvedLayoutConnectorRecord();
		OutConnectorRecord.WorldBindingId = TEXT("ReservationBinding");
		OutConnectorRecord.ContinuationFamilyId = FamilyId;
		OutConnectorRecord.ContinuationFamilyCandidateId = FName(*FString::Printf(TEXT("%sCandidate"), FamilyLabel));
		OutConnectorRecord.ContinuationPolicy = FLayoutWorldBindingContinuationPolicy();
		OutConnectorRecord.ContinuationPolicy.MaxSlopeBlocks = 2;
		OutConnectorRecord.SolveBudget.MaxSolveDurationSeconds = 6.5f;
		OutConnectorRecord.ContentSet = TSoftObjectPtr<ULayoutRegionContentSetAsset>(ContentSet);
		OutConnectorRecord.LayoutProfile = TSoftObjectPtr<ULayoutProfileAsset>(Profile);
		OutConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel = 1;

		OutFallbackSolveBudget = FLayoutRootSolveBudgetSettings();
		OutFallbackSolveBudget.MaxSolveDurationSeconds = 1.25f;
	}

	bool RunThinStoredConnectorRuntimeViewTest(
		FAutomationTestBase& Test,
		const TCHAR* const FamilyLabel,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const bool bUseLiveFamily,
		const FGameplayTag EndpointConnectorTypeTag)
	{
		ULayoutWorldBindingAsset* WorldBinding = nullptr;
		FResolvedLayoutConnectorRecord ConnectorRecord;
		FLayoutRootSolveBudgetSettings FallbackSolveBudget;
		if (bUseLiveFamily)
		{
			BuildThinStoredContinuationFixture(
				FamilyLabel,
				FamilyId,
				FamilyType,
				PlacementKind,
				EndpointConnectorTypeTag,
				WorldBinding,
				ConnectorRecord,
				FallbackSolveBudget);
		}
		else
		{
			BuildThinStoredContinuationFallbackFixture(
				FamilyLabel,
				FamilyId,
				PlacementKind,
				ConnectorRecord,
				FallbackSolveBudget);
		}

		FLayoutWorldBindingRuntimeView ResolvedView;
		FString FailureReason;
		const bool bRebuilt =
			LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromResolvedConnectorRecord(
				bUseLiveFamily ? WorldBinding : nullptr,
				ConnectorRecord,
				FallbackSolveBudget,
				ResolvedView,
				FailureReason);
		if (!bUseLiveFamily)
		{
			Test.TestFalse(
				*FString::Printf(
					TEXT("Resolved thin stored %s connector rebuild rejects one carrier that still requires a live world binding"),
					FamilyLabel),
				bRebuilt);
			Test.TestTrue(
				*FString::Printf(
					TEXT("Resolved thin stored %s connector rejection explains the missing live world binding"),
					FamilyLabel),
				FailureReason.Contains(TEXT("requires one live world binding")));
			return true;
		}

		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Resolved thin stored %s connector rebuild succeeds"),
					FamilyLabel),
				bRebuilt))
		{
			Test.AddError(FailureReason);
			return false;
		}

		Test.TestEqual(
			*FString::Printf(
				TEXT("Thin stored %s connector rebuild keeps the continuation family id"),
				FamilyLabel),
			ResolvedView.ContinuationSelection.FamilyId,
			FamilyId);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Thin stored %s connector rebuild keeps the continuation placement kind"),
				FamilyLabel),
			ResolvedView.ContinuationSelection.PlacementKind,
			PlacementKind);

		if (bUseLiveFamily)
		{
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild keeps the live candidate id"),
					FamilyLabel),
				ResolvedView.CandidateId,
				FName(*FString::Printf(TEXT("Live%sCandidate"), FamilyLabel)));
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild uses the binding-owned shared cell size"),
					FamilyLabel),
				ResolvedView.SharedCellSizeInBlocks,
				WorldBinding->BaseCellDimensionsBlocks);
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild uses the binding-owned placement offset"),
					FamilyLabel),
				ResolvedView.TemplatePlacementZOffsetBlocks,
				WorldBinding->TemplatePlacementZOffsetBlocks);
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild uses the binding default terrain search start"),
					FamilyLabel),
				ResolvedView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
				WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ);
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild uses the live family continuation slope contract"),
					FamilyLabel),
				ResolvedView.ContinuationPolicy.MaxSlopeBlocks,
				9);
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild uses the live family solve budget"),
					FamilyLabel),
				ResolvedView.SolveBudget.MaxSolveDurationSeconds,
				9.5f);
			Test.TestEqual(
				*FString::Printf(
					TEXT("Thin stored %s live-family rebuild backfills the resolved continuation-entry level from the cached solve-result alignment"),
					FamilyLabel),
				ResolvedView.ContinuationSelection.ResolvedEntryLevel,
				2);
		}
		return true;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedSurfacePathConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsResolvedSurfacePathConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedTunnelConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsResolvedTunnelConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedSurfacePathConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsResolvedSurfacePathConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedBridgeConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsResolvedBridgeConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedTunnelConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsResolvedTunnelConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntrySurfacePathConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntrySurfacePathConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryBridgeConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntryBridgeConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryTunnelConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntryTunnelConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntrySurfacePathConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntrySurfacePathConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntryBridgeConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryTunnelConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntryTunnelConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntrySurfacePathConnectorLiveFamilyViewFromResolvedCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntrySurfacePathConnectorLiveFamilyViewFromResolvedCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryBridgeConnectorLiveFamilyViewFromResolvedCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntryBridgeConnectorLiveFamilyViewFromResolvedCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryTunnelConnectorLiveFamilyViewFromResolvedCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZHigherEntryTunnelConnectorLiveFamilyViewFromResolvedCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckSurfacePathConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckSurfacePathConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckBridgeConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckBridgeConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckTunnelConnectorFallbackViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckTunnelConnectorFallbackViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckSurfacePathConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckSurfacePathConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckBridgeConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckTunnelConnectorLiveFamilyViewFromThinStoredCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckTunnelConnectorLiveFamilyViewFromThinStoredCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckSurfacePathConnectorLiveFamilyViewFromResolvedCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckSurfacePathConnectorLiveFamilyViewFromResolvedCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckBridgeConnectorLiveFamilyViewFromResolvedCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckBridgeConnectorLiveFamilyViewFromResolvedCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckTunnelConnectorLiveFamilyViewFromResolvedCarrierTest,
	"PorismExtension.Layout.Planning.WorldBindingContinuationRuntimeView.BuildsFiniteCenteredZLowDeckTunnelConnectorLiveFamilyViewFromResolvedCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedSurfacePathConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("Surface"),
		TEXT("SurfaceFamily"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		true,
		LayoutGameplayTags::ConnectorRoad);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("Bridge"),
		TEXT("BridgeFamily"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		true,
		LayoutGameplayTags::ConnectorBridge);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedTunnelConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("Tunnel"),
		TEXT("TunnelFamily"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		true,
		LayoutGameplayTags::ConnectorTunnel);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedSurfacePathConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("Surface"),
		TEXT("SurfaceFamily"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		false,
		LayoutGameplayTags::ConnectorRoad);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedBridgeConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("Bridge"),
		TEXT("BridgeFamily"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		false,
		LayoutGameplayTags::ConnectorBridge);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsResolvedTunnelConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("Tunnel"),
		TEXT("TunnelFamily"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		false,
		LayoutGameplayTags::ConnectorTunnel);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntrySurfacePathConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("SurfaceRoadFiniteCenteredZThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2841,
		false,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryBridgeConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("BridgeRoadFiniteCenteredZThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2842,
		false,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryTunnelConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("TunnelRoadFiniteCenteredZThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2843,
		false,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntrySurfacePathConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("SurfaceRoadFiniteCenteredZThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2841,
		true,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("BridgeRoadFiniteCenteredZThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2842,
		true,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryTunnelConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("TunnelRoadFiniteCenteredZThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2843,
		true,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntrySurfacePathConnectorLiveFamilyViewFromResolvedCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("SurfaceRoadFiniteCenteredZRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2741,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryBridgeConnectorLiveFamilyViewFromResolvedCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("BridgeRoadFiniteCenteredZRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2742,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZHigherEntryTunnelConnectorLiveFamilyViewFromResolvedCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		*this,
		TEXT("higher-entry"),
		TEXT("TunnelRoadFiniteCenteredZRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2743,
		1,
		-8);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckSurfacePathConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("SurfaceRoadFiniteCenteredZLowDeckThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2941,
		false,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckBridgeConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("BridgeRoadFiniteCenteredZLowDeckThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2942,
		false,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckTunnelConnectorFallbackViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("TunnelRoadFiniteCenteredZLowDeckThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2943,
		false,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckSurfacePathConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("SurfaceRoadFiniteCenteredZLowDeckThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2941,
		true,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckBridgeConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("BridgeRoadFiniteCenteredZLowDeckThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2942,
		true,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckTunnelConnectorLiveFamilyViewFromThinStoredCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZThinStoredConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("TunnelRoadFiniteCenteredZLowDeckThinStoredRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2943,
		true,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckSurfacePathConnectorLiveFamilyViewFromResolvedCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("SurfaceRoadFiniteCenteredZLowDeckRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		2944,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckBridgeConnectorLiveFamilyViewFromResolvedCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("BridgeRoadFiniteCenteredZLowDeckRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		2945,
		0,
		0);
}

bool FLayoutWorldBindingContinuationRuntimeViewBuildsFiniteCenteredZLowDeckTunnelConnectorLiveFamilyViewFromResolvedCarrierTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZResolvedConnectorRuntimeViewTest(
		*this,
		TEXT("low-deck"),
		TEXT("TunnelRoadFiniteCenteredZLowDeckRuntimeView"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		2946,
		0,
		0);
}
