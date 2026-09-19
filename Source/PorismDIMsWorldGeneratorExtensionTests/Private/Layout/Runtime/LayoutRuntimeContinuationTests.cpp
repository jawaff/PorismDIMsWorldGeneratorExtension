// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutConnectorPlanning.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Types/LayoutGameplayTags.h"
#include "Misc/AutomationTest.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;

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

	ELayoutWorldBindingPlacementKind ResolveContinuationPlacementKindForTest(
		const ELayoutWorldBindingContinuationFamilyType FamilyType)
	{
		switch (FamilyType)
		{
		case ELayoutWorldBindingContinuationFamilyType::SurfacePath:
			return ELayoutWorldBindingPlacementKind::SurfacePath;
		case ELayoutWorldBindingContinuationFamilyType::BridgeContinuation:
			return ELayoutWorldBindingPlacementKind::BridgeContinuation;
		case ELayoutWorldBindingContinuationFamilyType::TunnelContinuation:
			return ELayoutWorldBindingPlacementKind::TunnelContinuation;
		default:
			return ELayoutWorldBindingPlacementKind::None;
		}
	}

	FLayoutWorldBindingPlacementPolicy BuildNormalizedContinuationPlacementPolicyForTest(
		const ULayoutWorldBindingAsset* const WorldBinding,
		const FLayoutWorldBindingContinuationFamily& Family)
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy =
			WorldBinding != nullptr
				? WorldBinding->DefaultPlacementPolicy
				: FLayoutWorldBindingPlacementPolicy();
		if (Family.bOverrideTerrainTransitionPolicy)
		{
			PlacementPolicy.TerrainTransition =
				Family.TerrainTransitionPolicyOverride;
		}
		return PlacementPolicy;
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

		ULayoutRegionContentSetAsset* CreateContinuationTestContentSet()
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			GetTransientPackage(),
			TEXT("LayoutTemplate_RuntimeContinuation"),
			FIntVector(16, 16, 16));
		ULayoutModuleAsset* EntryModule = CreateModule(
			GetTransientPackage(),
			TEXT("LayoutModule_RuntimeContinuationEntry"),
			Template,
			{ELayoutCellIntent::Entry},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
		FLayoutFaceRule EntryExteriorRule = MakeFaceRule(
			ELayoutFaceDirection::PosX,
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}));
		EntryExteriorRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryModule->FaceRules.SetRule(EntryExteriorRule);
		FLayoutFaceRule EntryBottomRule = MakeFaceRule(
			ELayoutFaceDirection::NegZ,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
		EntryBottomRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryModule->FaceRules.SetRule(EntryBottomRule);
		ULayoutModuleAsset* ConnectorModule = CreateModule(
			GetTransientPackage(),
			TEXT("LayoutModule_RuntimeContinuationConnector"),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Connector},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
		FLayoutFaceRule ConnectorBottomRule = MakeFaceRule(
			ELayoutFaceDirection::NegZ,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor);
		ConnectorBottomRule.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		ConnectorModule->FaceRules.SetRule(ConnectorBottomRule);
		ULayoutModuleAsset* VerticalAccessModule = CreateModule(
			GetTransientPackage(),
			TEXT("LayoutModule_RuntimeContinuationVerticalAccess"),
			Template,
			{ELayoutCellIntent::VerticalAccess},
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
		FLayoutRegionContentEntry Entry;
		Entry.EntryId = TEXT("RuntimeContinuationEntry");
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = EntryModule;
		FLayoutRegionContentEntry Connector;
		Connector.EntryId = TEXT("RuntimeContinuationConnector");
		Connector.ContentKind = ELayoutRegionContentKind::Module;
		Connector.ModuleSettings.Module = ConnectorModule;
		FLayoutRegionContentEntry VerticalAccess;
		VerticalAccess.EntryId = TEXT("RuntimeContinuationVerticalAccess");
		VerticalAccess.ContentKind = ELayoutRegionContentKind::Module;
		VerticalAccess.ModuleSettings.Module = VerticalAccessModule;
		return CreateRegionContentSet(
			GetTransientPackage(),
			TEXT("LayoutContentSet_RuntimeContinuation"),
			{Entry, Connector, VerticalAccess});
	}

	ULayoutProfileAsset* CreateContinuationTestProfile(ULayoutRegionContentSetAsset* ContentSet)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_RuntimeContinuation"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			1,
			2,
			false);
		Profile->ContentSet = ContentSet;
		Profile->bRequireAllTraversalChannelsReachable = true;
		return Profile;
	}


		FResolvedLayoutSiteRecord CreateRuntimeContinuationSiteRecord(
		const FIntVector& SiteCenterBlockWorldPos,
		const FName BindingId,
		const FGameplayTag EndpointConnectorTypeTag,
		const ELayoutFaceDirection ExposedEntryFaceDirection = ELayoutFaceDirection::PosX)
	{
		FResolvedLayoutSiteLocationMetadata LocationMetadata;
		LocationMetadata.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;

		FLayoutSiteSolveSourceSelection SolveSourceSelection;
		SolveSourceSelection.ExportedConnectorTypeTags =
			MakeTags({EndpointConnectorTypeTag});

		FLayoutSolveResult SolveResult;
		SolveResult.bSucceeded = true;
		SolveResult.FootprintSize = FIntPoint(1, 1);
		SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 16);
		SolveResult.ExportedEntryCells = {FIntVector::ZeroValue};
		ULayoutModuleAsset* const EntryModule = NewObject<ULayoutModuleAsset>(GetTransientPackage());
		// Placed-root readiness requires real template coverage, not an endpoint-only module.
		EntryModule->Template = CreateTemplate(EntryModule, TEXT("PlacedRootEntry"), FIntVector(1));
		EntryModule->FaceRules.SetRule(MakeFaceRule(
			ExposedEntryFaceDirection,
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
		FLayoutPlacedModule& EntryPlacement = SolveResult.Placements.AddDefaulted_GetRef();
		EntryPlacement.Cell = FIntVector::ZeroValue;
		EntryPlacement.Intent = ELayoutCellIntent::Entry;
		EntryPlacement.Module = EntryModule;

		FLayoutRootPublicationMetadata PublicationMetadata;
		PublicationMetadata.RootSolveId = FLayoutId(*FString::Printf(
			TEXT("RuntimeContinuationRoot_%s"),
			*SiteCenterBlockWorldPos.ToString()));
		PublicationMetadata.RootCandidateId = PublicationMetadata.RootSolveId;
		PublicationMetadata.RootPlacementPolicyId = TEXT("RuntimeContinuation");
		FResolvedLayoutSiteRecord SiteRecord =
			FLayoutSiteReservation::BuildResolvedSiteRecord(
				LocationMetadata,
				SolveSourceSelection,
				nullptr,
				PublicationMetadata,
				&SolveResult);
		FResolvedLayoutSiteRuntimeState RuntimeState;
		RuntimeState.bLayoutSolved = true;
		RuntimeState.CachedApplyability = ELayoutCachedApplyability::Solved;
		RuntimeState.bLayoutRealized = false;
		RuntimeState.bHasBeenCommittedToChunkWorld = false;
		RuntimeState.TerrainFitDiagnosticKind =
			ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFlatFit;
		SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
		SiteRecord.WorldBindingId = BindingId;
		SiteRecord.BiomeRowName = TEXT("Reservation");
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

		bool RunFiniteCenteredZContinuationRuntimeTest(
		FAutomationTestBase& Test,
		const TCHAR* const EntryLevelLabel,
		const FString& FamilyLabel,
		const TCHAR* const ProfileObjectName,
		const TCHAR* const WorldBindingObjectName,
		const FName FamilyId,
		const ELayoutWorldBindingContinuationFamilyType FamilyType,
		const ELayoutWorldBindingPlacementKind PlacementKind,
		const FGameplayTag& ConnectorTypeTag,
		const FIntPoint& StartGridCell,
		const FIntPoint& EndGridCell,
		const FIntVector& StartSiteCenterBlockWorldPos,
		const FIntVector& EndSiteCenterBlockWorldPos,
		const int32 SolveSeed,
		const int32 ContinuationEndpointTransitionDepth,
		const int32 ContinuationEntryLevel,
		const int32 TemplatePlacementZOffsetBlocks,
		const FIntPoint& ProfileFootprint,
		const int32 ProfileMinDepth,
		const int32 ProfileMaxDepth,
		const int32 ExpectedLocalDeckLevel)
	{
		ULayoutRegionContentSetAsset* ContentSet =
			CreateContinuationTestContentSet();
		UChunkStructureTemplate* const ContinuationTemplate =
			ContentSet->Entries[0].ModuleSettings.Module->Template.Get();
		TStrongObjectPtr<UChunkStructureTemplate> ContinuationTemplateLifetime(
			ContinuationTemplate);
		ULayoutProfileAsset* Profile = CreateProfile(
			GetTransientPackage(),
			ProfileObjectName,
			ProfileFootprint,
			FIntPoint(7, 7),
			ProfileMinDepth,
			ProfileMaxDepth,
			false);
		Profile->ContentSet = ContentSet;
		Profile->bRequireAllTraversalChannelsReachable = true;
		Profile->ContinuationEntryLevel = ContinuationEntryLevel;

		ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
			GetTransientPackage(),
			WorldBindingObjectName);
		TStrongObjectPtr<ULayoutWorldBindingAsset> WorldBindingLifetime(WorldBinding);
		WorldBinding->BindingId = TEXT("SurfaceBinding");
		WorldBinding->BiomeRowNames = {TEXT("ConnectorTerrain")};
		WorldBinding->BaseCellDimensionsBlocks = FIntVector(16, 16, 16);
		WorldBinding->TemplatePlacementZOffsetBlocks = TemplatePlacementZOffsetBlocks;
		auto CreateRootEntryModule = [](const TCHAR* const ModuleName, const ELayoutFaceDirection EntryDirection)
		{
			UChunkStructureTemplate* Template = CreateTemplate(
				GetTransientPackage(),
				*FString::Printf(TEXT("%s_Template"), ModuleName),
				FIntVector(16, 16, 16));
			ULayoutModuleAsset* Module = CreateModule(
				GetTransientPackage(),
				ModuleName,
				Template,
				{ELayoutCellIntent::Entry},
				BuildFilledCubeFaces(
					MakeTags({LayoutGameplayTags::FaceOpen}),
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})));
			Module->FaceRules.SetRule(MakeFaceRule(
				EntryDirection,
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
			return Module;
		};
		ULayoutModuleAsset* const StartEntryModule = CreateRootEntryModule(
			TEXT("LayoutModule_RuntimeContinuationStartEntry"),
			ELayoutFaceDirection::PosX);
		TStrongObjectPtr<ULayoutModuleAsset> StartEntryModuleLifetime(StartEntryModule);
		TStrongObjectPtr<UChunkStructureTemplate> StartEntryTemplateLifetime(StartEntryModule->Template.Get());
		ULayoutModuleAsset* const EndEntryModule = CreateRootEntryModule(
			TEXT("LayoutModule_RuntimeContinuationEndEntry"),
			ELayoutFaceDirection::NegX);
		TStrongObjectPtr<ULayoutModuleAsset> EndEntryModuleLifetime(EndEntryModule);
		TStrongObjectPtr<UChunkStructureTemplate> EndEntryTemplateLifetime(EndEntryModule->Template.Get());

		FLayoutWorldBindingCandidate& RootCandidate =
			WorldBinding->Candidates.AddDefaulted_GetRef();
		RootCandidate.CandidateId = TEXT("RootCandidate");
		RootCandidate.LayoutProfile = CreateProfile(
			GetTransientPackage(),
			TEXT("LayoutProfile_RuntimeFiniteCenteredZLowDeckContinuationRoot"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			1,
			false);
		RootCandidate.LayoutProfile->ContentSet = CreateRegionContentSet(
			GetTransientPackage(),
			TEXT("LayoutContentSet_RuntimeFiniteCenteredZLowDeckContinuationRoot"),
			{});
		RootCandidate.LayoutProfile->bRequireAllTraversalChannelsReachable = true;
		RootCandidate.Weight = 1;

		FLayoutWorldBindingContinuationFamily& Family =
			WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = FamilyId;
		Family.FamilyType = FamilyType;
		Family.EndpointConnectorTypeTag = ConnectorTypeTag;
		Family.MaxConnectionsPerSite = 1;
		Family.MaxConnectionDistanceInCells = 8;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
		WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
		Family.ContinuationPolicy.MaxSlopeBlocks = 64;
		Family.ContinuationPolicy.MaxBridgeGapCells = 4;
		Family.SolveBudget.MaxSolveDurationSeconds = 6.5f;
		if (PlacementKind != ELayoutWorldBindingPlacementKind::SurfacePath)
		{
			Family.bOverrideTerrainTransitionPolicy = true;
		}
		FLayoutWorldBindingContinuationCandidate& Candidate =
			Family.Candidates.AddDefaulted_GetRef();
		Candidate.CandidateId = *FString::Printf(TEXT("%sCandidate"), *FamilyId.ToString());
		Candidate.LayoutProfile = Profile;
		Candidate.Weight = 1;

		FLayoutWorldTestHarness WorldHarness = CreateChunkWorldHarness(
			GetTransientPackage(),
			FIntVector(16, 16, 256),
			TEXT("ConnectorTerrain"));
		AChunkWorldExtended* const ChunkWorld = WorldHarness.World;
		if (!Test.TestNotNull(
			*FString::Printf(
				TEXT("Finite centered-Z %s %s continuation fixture creates a chunk world"),
				EntryLevelLabel,
				*FamilyLabel),
			ChunkWorld))
		{
			return false;
		}
		if (ChunkWorld->IsRunning())
		{
			ChunkWorld->StopGen();
		}
		UWorldGenDef* const WorldGenDef = ChunkWorld->WorldGenDef;
		WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
		WorldGenDef->NoiseCoordinateOffset = FIntVector(0, 0, -128);
		WorldGenDef->WorldBiomes.Reset();
		WorldGenDef->WorldGen.Reset();
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		FBiomeDualData Row;
		Row.BiomeName = TEXT("ConnectorTerrain");
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		// Exported entries need an owning terrain row, not a noise-only contribution.
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
		WorldGenDef->WorldBiomes.Add(Row);
		ChunkWorld->StartGen();

		LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fixture resolves finite chunk-world bounds from the shared worldgen scale context"),
					EntryLevelLabel,
					*FamilyLabel),
				LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
					ChunkWorld,
					Bounds)))
		{
			return false;
		}
		Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fixture exposes a finite Z span"),
					EntryLevelLabel,
					*FamilyLabel),
			Bounds.bHasFiniteZ);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fixture keeps the raw minimum Z block"),
					EntryLevelLabel,
					*FamilyLabel),
			Bounds.MinInclusive.Z,
			0);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fixture keeps the raw maximum Z block"),
					EntryLevelLabel,
					*FamilyLabel),
			Bounds.MaxInclusive.Z,
			255);

		UChunkWorldLayoutRuntimeComponent* RuntimeComponent =
			ChunkWorld->GetLayoutRuntimeComponent();
		if (!Test.TestNotNull(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s runtime refresh has an owning layout runtime component"),
					EntryLevelLabel,
					*FamilyLabel),
				RuntimeComponent))
		{
			return false;
		}

		TMap<FIntPoint, FResolvedLayoutSiteRecord> SiteRecords;
		FLayoutActiveBiomeSampler ActiveBiomeSampler;
		FLayoutConnectorTerrainPathContext TerrainPathContext;
		const FLayoutConnectorTerrainPathContext* TerrainPathContextPtr = nullptr;
		if (Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fixture initializes the active-biome sampler"),
					EntryLevelLabel,
					*FamilyLabel),
				ActiveBiomeSampler.Initialize(
					GetTransientPackage(),
					WorldGenDef,
					0)))
		{
			TerrainPathContext.ActiveBiomeSampler = &ActiveBiomeSampler;
			TerrainPathContext.CoordinateSettings =
				MakeTestCoordinateSettings(WorldGenDef);
			TerrainPathContextPtr = &TerrainPathContext;
		}
		else
		{
			return false;
		}
		const auto MakeConnectorTerrainSiteRecord =
			[&](const FIntVector& SiteCenterBlockWorldPos, ULayoutModuleAsset* const EntryModule)
		{
			FResolvedLayoutSiteRecord SiteRecord = CreateRuntimeContinuationSiteRecord(
				SiteCenterBlockWorldPos,
				WorldBinding->BindingId,
				ConnectorTypeTag);
			FResolvedLayoutSiteSolvedPayload SolvedPayload =
				SiteRecord.GetResolvedSiteSolvedPayload();
			FLayoutPlacedModule& EntryPlacement =
				SolvedPayload.SolveResult.Placements.AddDefaulted_GetRef();
			EntryPlacement.Cell = FIntVector::ZeroValue;
			EntryPlacement.Intent = ELayoutCellIntent::Entry;
			EntryPlacement.Module = EntryModule;
			SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);
			// This fixture tests route geometry between existing placed roots, not root realization.
			auto RuntimeState = SiteRecord.GetResolvedSiteRuntimeState();
			RuntimeState.bLayoutRealized = true;
			RuntimeState.bHasBeenCommittedToChunkWorld = true;
			SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
			SiteRecord.BiomeRowName = TEXT("ConnectorTerrain");
			return SiteRecord;
		};
		SiteRecords.Add(
			StartGridCell,
			MakeConnectorTerrainSiteRecord(StartSiteCenterBlockWorldPos, StartEntryModule));
		SiteRecords.Add(
			EndGridCell,
			MakeConnectorTerrainSiteRecord(EndSiteCenterBlockWorldPos, EndEntryModule));
		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z %s %s continuation fixture exposes one start endpoint"),
				EntryLevelLabel,
				*FamilyLabel),
			FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(StartGridCell, SiteRecords[StartGridCell]).Num(),
			1);
		Test.TestEqual(
			*FString::Printf(
				TEXT("Finite centered-Z %s %s continuation fixture exposes one end endpoint"),
				EntryLevelLabel,
				*FamilyLabel),
			FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(EndGridCell, SiteRecords[EndGridCell]).Num(),
			1);
		FLayoutWorldBindingContinuationFamilyTarget FamilyTarget;
		FString FamilyTargetFailureReason;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fixture resolves its family target"),
					EntryLevelLabel,
					*FamilyLabel),
				LayoutWorldBindingRuntimeView::TryResolveContinuationFamilyTarget(
					WorldBinding,
					FamilyType,
					ConnectorTypeTag,
					FamilyTarget,
					FamilyTargetFailureReason)))
		{
			Test.AddError(FamilyTargetFailureReason);
			return false;
		}

		const auto StartEndpoints = FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(StartGridCell, SiteRecords[StartGridCell]);
		const auto EndEndpoints = FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(EndGridCell, SiteRecords[EndGridCell]);
		if (StartEndpoints.Num() != 1 || EndEndpoints.Num() != 1) return false;
		FResolvedLayoutConnectorRecord ExpectedRecord;
		FString ExpectedRequestFailureReason;
		if (!Test.TestTrue(TEXT("Active endpoint-pair path builds the expected continuation record"),
			FLayoutConnectorPlanning::TryBuildContinuationRecordForEndpointPair(
				StartEndpoints[0], EndEndpoints[0], WorldBinding, Profile, SolveSeed,
				ExpectedRecord, ExpectedRequestFailureReason)))
		{
			Test.AddError(ExpectedRequestFailureReason);
			return false;
		}
		FLayoutPreparedContinuationRoute ExpectedPreparedRoute;
		if (!Test.TestTrue(
				*FString::Printf(
					TEXT("Leaf finite centered-Z %s %s continuation builder prepares its connector route"),
					EntryLevelLabel,
					*FamilyLabel),
				FLayoutConnectorPlanning::TryPrepareContinuationRoute(
					ExpectedRecord,
					WorldBinding,
					SolveSeed,
					TerrainPathContextPtr,
					ExpectedPreparedRoute,
					ExpectedRequestFailureReason)))
		{
			Test.AddError(ExpectedRequestFailureReason);
			return false;
		}
		const FLayoutPreparedContinuationRouteSegment* const ExpectedPreparedSegment =
			ExpectedPreparedRoute.Segments.FindByPredicate([](const FLayoutPreparedContinuationRouteSegment& Segment)
			{
				return Segment.PreparedContinuation.IsSet();
			});
		if (!Test.TestNotNull(TEXT("Leaf continuation fixture retains one prepared segment"), ExpectedPreparedSegment))
		{
			return false;
		}
		const FIntVector ExpectedSegmentPathOrigin = ExpectedPreparedSegment->ConnectorRecord.PathOriginBlockWorldPos;

		RuntimeComponent->SetLayoutWorldBindings({WorldBinding});
		APlayerController* Controller = ChunkWorld->GetWorld()->SpawnActor<APlayerController>();
		APawn* Pawn = ChunkWorld->GetWorld()->SpawnActor<APawn>();
		if (!Test.TestNotNull(TEXT("Automatic continuation fixture owns a controller"), Controller)
			|| !Test.TestNotNull(TEXT("Automatic continuation fixture owns a planning center"), Pawn)) return false;
		Controller->Possess(Pawn);
		ChunkWorld->GetWorld()->AddController(Controller);
		for (const auto& Pair : SiteRecords)
		{
			FResolvedLayoutSiteRecord CoverageRecord = Pair.Value;
			CoverageRecord.SolveResult.TemplatePlacementZOffsetBlocks = WorldBinding->TemplatePlacementZOffsetBlocks;
			// Observe whole placed templates, including binding offset, not only cell anchors.
			for (const auto& Placement : CoverageRecord.SolveResult.Placements)
			{
				const UChunkStructureTemplate* Template = Placement.Module->Template.Get();
				if (!Test.TestNotNull(TEXT("Placed entry supplies template coverage"), Template)) return false;
				const FIntVector Anchor = FLayoutStreamingWindow::ComputeSitePlacementAnchorBlockWorldPos(
					CoverageRecord, Placement, CoverageRecord.SolveResult.SharedCellSizeInBlocks);
				const FIntVector Min = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(Anchor, WorldGenDef->ChunkBlockSize);
				const FIntVector Max = FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(
					Anchor + Template->SizeInBlocks - FIntVector(1), WorldGenDef->ChunkBlockSize);
				for (int32 Z = Min.Z; Z <= Max.Z; Z += WorldGenDef->ChunkBlockSize.Z)
				for (int32 Y = Min.Y; Y <= Max.Y; Y += WorldGenDef->ChunkBlockSize.Y)
				for (int32 X = Min.X; X <= Max.X; X += WorldGenDef->ChunkBlockSize.X)
					RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(X, Y, Z));
			}
		}
		RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(
			StartGridCell,
			SiteRecords[StartGridCell]);
		RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(
			EndGridCell,
			SiteRecords[EndGridCell]);

		RuntimeComponent->RefreshConnectorRecordsForTesting();

		const TArray<FResolvedLayoutConnectorRecord> ConnectorRecords =
			RuntimeComponent->GetResolvedLayoutConnectorRecords();
		if (!Test.TestEqual(
				*FString::Printf(
					TEXT("Runtime refresh keeps one finite centered-Z %s %s continuation connector record"),
					EntryLevelLabel,
					*FamilyLabel),
				ConnectorRecords.Num(),
				1))
		{
			Test.AddInfo(FString::Printf(
				TEXT("Runtime continuation dispatcher state: %s"),
				*RuntimeComponent->GetBackgroundSolveDiagnostics().ToDebugString()));
			return false;
		}

		const FResolvedLayoutConnectorRecord& ConnectorRecord =
			ConnectorRecords[0];
		if (PlacementKind == ELayoutWorldBindingPlacementKind::SurfacePath)
		{
			FLayoutFrozenTerrainContract FrozenTerrainContract;
			Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation caches a frozen terrain contract for replay"),
					EntryLevelLabel,
					*FamilyLabel),
				RuntimeComponent->TryGetResolvedConnectorFrozenTerrainContractForTesting(
					ConnectorRecord,
					FrozenTerrainContract));
			if (RuntimeComponent->TryGetResolvedConnectorFrozenTerrainContractForTesting(
					ConnectorRecord,
					FrozenTerrainContract))
			{
				Test.TestFalse(
					*FString::Printf(
						TEXT("Finite centered-Z %s %s continuation replay contract has stable id"),
						EntryLevelLabel,
						*FamilyLabel),
					FrozenTerrainContract.ContractId.IsNone());
			}
		}

		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the authored family id"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.ContinuationFamilyId,
			FamilyId);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the resolved continuation family id"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.ResolvedContinuationSelection.FamilyId,
			FamilyId);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the resolved continuation placement kind"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.ResolvedContinuationSelection.PlacementKind,
			PlacementKind);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the explicit continuation entry level"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel,
			ContinuationEntryLevel);
		Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation still solves on the runtime cache path"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.bLayoutSolved);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the resolved continuation alignment level"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.SolveResult.ResolvedTerrainAlignmentLevel,
			ContinuationEntryLevel);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same path-origin Z as the prepared leaf segment"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.PathOriginBlockWorldPos.Z,
			ExpectedSegmentPathOrigin.Z);
		Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the runtime path origin inside the finite Z chunk span"),
					EntryLevelLabel,
					*FamilyLabel),
			LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
				Bounds,
				ConnectorRecord.PathOriginBlockWorldPos));
		Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the prepared leaf segment path origin inside the finite Z chunk span"),
					EntryLevelLabel,
					*FamilyLabel),
			LayoutWorldBindingSitePlanner::IsBlockWorldPosInsideFiniteAxisBounds(
				Bounds,
				ExpectedSegmentPathOrigin));
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same start endpoint world position as the leaf continuation builder"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.StartEndpointBlockWorldPos,
			ExpectedRecord.StartEndpointBlockWorldPos);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same end endpoint world position as the leaf continuation builder"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.EndEndpointBlockWorldPos,
			ExpectedRecord.EndEndpointBlockWorldPos);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same top-level placement kind as the leaf continuation builder"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.PlacementKind,
			ExpectedRecord.PlacementKind);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same connector placement-policy id as the leaf continuation builder"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.RootPlacementPolicyId,
			ExpectedRecord.RootPlacementPolicyId);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same connector candidate id as the leaf continuation builder"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.RootCandidateId,
			ExpectedRecord.RootCandidateId);
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation keeps the same connector solve id as the leaf continuation builder"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.RootSolveId,
			ExpectedRecord.RootSolveId);
		Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation publishes non-empty terrain-resampled cells"),
					EntryLevelLabel,
					*FamilyLabel),
			!ConnectorRecord.SolveResult.PlannedCells.IsEmpty());
		Test.TestEqual(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation fills every assembled connector cell"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.SolveResult.Placements.Num(),
			ConnectorRecord.SolveResult.PlannedCells.Num());
		Test.TestTrue(
				*FString::Printf(
					TEXT("Finite centered-Z %s %s continuation planned cells stay on the requested local deck level after runtime refresh"),
					EntryLevelLabel,
					*FamilyLabel),
			ConnectorRecord.SolveResult.PlannedCells.ContainsByPredicate(
				[ExpectedLocalDeckLevel](const FLayoutPlannedCell& Cell)
				{
					return Cell.Cell.Z == ExpectedLocalDeckLevel;
				}));
		Test.TestTrue(
			*FString::Printf(
				TEXT("Finite centered-Z %s %s continuation resolved placements also stay on the requested local deck level after runtime refresh"),
				EntryLevelLabel,
				*FamilyLabel),
			ConnectorRecord.SolveResult.Placements.ContainsByPredicate(
				[ExpectedLocalDeckLevel](const FLayoutPlacedModule& Placement)
				{
					return Placement.Cell.Z == ExpectedLocalDeckLevel;
				}));
		return true;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentCollectsRotatedEntryFaceInWorldSpaceTest,
	"PorismExtension.Layout.Runtime.CollectsRotatedEntryFaceInWorldSpace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentKeepsSameReservationDirectRootsDistinctTest,
	"PorismExtension.Layout.Runtime.DirectRootsKeepSameReservationDistinct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRejectsAppliedPartialRootEntryTest,
	"PorismExtension.Layout.Runtime.RejectsAppliedPartialRootEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentIgnoresInternalTerrainSeamEntryTest,
	"PorismExtension.Layout.Runtime.IgnoresInternalTerrainSeamEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)







IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRefreshesFiniteCenteredZLowDeckSurfacePathContinuationFamilyTest,
	"PorismExtension.Layout.Runtime.RefreshesFiniteCenteredZLowDeckSurfacePathContinuationFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRefreshesFiniteCenteredZLowDeckBridgeContinuationFamilyTest,
	"PorismExtension.Layout.Runtime.RefreshesFiniteCenteredZLowDeckBridgeContinuationFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRefreshesFiniteCenteredZLowDeckTunnelContinuationFamilyTest,
	"PorismExtension.Layout.Runtime.RefreshesFiniteCenteredZLowDeckTunnelContinuationFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)




IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRefreshesFiniteCenteredZHigherEntrySurfacePathContinuationFamilyTest,
	"PorismExtension.Layout.Runtime.RefreshesFiniteCenteredZHigherEntrySurfacePathContinuationFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRefreshesFiniteCenteredZHigherEntryBridgeContinuationFamilyTest,
	"PorismExtension.Layout.Runtime.RefreshesFiniteCenteredZHigherEntryBridgeContinuationFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeComponentRefreshesFiniteCenteredZHigherEntryTunnelContinuationFamilyTest,
	"PorismExtension.Layout.Runtime.RefreshesFiniteCenteredZHigherEntryTunnelContinuationFamily",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)






bool FLayoutRuntimeComponentCollectsRotatedEntryFaceInWorldSpaceTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutSiteRecord SiteRecord = CreateRuntimeContinuationSiteRecord(
		FIntVector(16, 16, 16),
		TEXT("RotatedEntryBinding"),
		LayoutGameplayTags::ConnectorRoad,
		ELayoutFaceDirection::PosX);
	FResolvedLayoutSiteSolvedPayload SolvedPayload = SiteRecord.GetResolvedSiteSolvedPayload();
	if (!TestTrue(TEXT("Rotated entry fixture contains one placement"), !SolvedPayload.SolveResult.Placements.IsEmpty()))
	{
		return false;
	}
	SolvedPayload.SolveResult.Placements[0].YawRotationSteps = 1;
	SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);

	const TArray<FResolvedLayoutConnectorEndpoint> Endpoints =
		FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(FIntPoint(0, 0), SiteRecord);
	if (!TestEqual(TEXT("Rotated entry fixture exposes one endpoint"), Endpoints.Num(), 1))
	{
		return false;
	}
	TestEqual(
		TEXT("Collected endpoint uses the placed module's world-facing entry direction"),
		Endpoints[0].ExposedEntryFaceDirection,
		ELayoutFaceDirection::PosY);
	return true;
}

bool FLayoutRuntimeComponentIgnoresInternalTerrainSeamEntryTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutSiteRecord SiteRecord = CreateRuntimeContinuationSiteRecord(
		FIntVector(16, 16, 16),
		TEXT("InternalTerrainSeamBinding"),
		LayoutGameplayTags::ConnectorRoad);
	FResolvedLayoutSiteSolvedPayload SolvedPayload = SiteRecord.GetResolvedSiteSolvedPayload();
	SolvedPayload.SolveResult.FootprintSize = FIntPoint(2, 1);
	FLayoutPlannedCell& AuthoredEntry = SolvedPayload.SolveResult.PlannedCells.AddDefaulted_GetRef();
	AuthoredEntry.Cell = FIntVector::ZeroValue;
	AuthoredEntry.Intent = ELayoutCellIntent::Entry;
	AuthoredEntry.EntryOrigin = ELayoutEntryOrigin::AuthoredBoundary;
	FLayoutPlannedCell& TerrainSeamEntry = SolvedPayload.SolveResult.PlannedCells.AddDefaulted_GetRef();
	TerrainSeamEntry.Cell = FIntVector(1, 0, 0);
	TerrainSeamEntry.Intent = ELayoutCellIntent::Entry;
	TerrainSeamEntry.EntryOrigin = ELayoutEntryOrigin::TerrainSeam;
	SolvedPayload.SolveResult.ExportedEntryCells.Add(TerrainSeamEntry.Cell);
	SolvedPayload.ExportedEntryCells.Add(TerrainSeamEntry.Cell);
	FLayoutPlacedModule TerrainSeamPlacement = SolvedPayload.SolveResult.Placements[0];
	TerrainSeamPlacement.Cell = TerrainSeamEntry.Cell;
	SolvedPayload.SolveResult.Placements.Add(MoveTemp(TerrainSeamPlacement));
	SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);

	const TArray<FResolvedLayoutConnectorEndpoint> Endpoints =
		FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(FIntPoint::ZeroValue, SiteRecord);
	return TestEqual(TEXT("Internal terrain-seam gate is not a root continuation endpoint"), Endpoints.Num(), 1);
}

bool FLayoutRuntimeComponentRejectsAppliedPartialRootEntryTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutSiteRecord SiteRecord = CreateRuntimeContinuationSiteRecord(
		FIntVector(16, 16, 16),
		TEXT("AppliedPartialBinding"),
		LayoutGameplayTags::ConnectorRoad);
	FResolvedLayoutSiteSolvedPayload SolvedPayload = SiteRecord.GetResolvedSiteSolvedPayload();
	SolvedPayload.SolveResult.bSucceeded = false;
	SiteRecord.SetResolvedSiteSolvedPayload(SolvedPayload);
	FResolvedLayoutSiteRuntimeState RuntimeState = SiteRecord.GetResolvedSiteRuntimeState();
	RuntimeState.bLayoutSolved = false;
	RuntimeState.CachedApplyability = ELayoutCachedApplyability::Partial;
	SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
	TestEqual(
		TEXT("Unapplied retained-partial root does not export an Entry"),
		FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(FIntPoint::ZeroValue, SiteRecord).Num(),
		0);

	RuntimeState.bLayoutRealized = true;
	RuntimeState.bHasBeenCommittedToChunkWorld = true;
	SiteRecord.SetResolvedSiteRuntimeState(RuntimeState);
	TestEqual(
		TEXT("Applying failed partial geometry does not certify a continuation Entry"),
		FLayoutConnectorPlanning::CollectConnectorEndpointsForSite(FIntPoint::ZeroValue, SiteRecord).Num(),
		0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimeContinuationHoverReservationTest,
	"PorismExtension.Layout.Runtime.ContinuationHoverReservation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeContinuationHoverReservationTest::RunTest(const FString& Parameters)
{
	auto* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>();
	auto* Binding = NewObject<ULayoutWorldBindingAsset>();
	Binding->BindingId = TEXT("HoverBinding");
	Binding->BaseCellDimensionsBlocks = FIntVector(16);
	auto& Family = Binding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("Road");
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.MaxConnectionsPerSite = 2;
	auto& Candidate = Family.Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("Road");
	Candidate.LayoutProfile = CreateProfile(GetTransientPackage(), TEXT("HoverRoad"), FIntPoint(1), FIntPoint(8), 1, 1, false);
	Candidate.LayoutProfile->ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("HoverRoadContent"), {});
	Candidate.LayoutProfile->ContinuationEntryLevel = 0;
	Runtime->SetLayoutWorldBindings({Binding});
	Runtime->AddResolvedLayoutSiteRecordForTesting(FIntPoint(16), CreateRuntimeContinuationSiteRecord(FIntVector(16), Binding->BindingId, LayoutGameplayTags::ConnectorRoad));
	Runtime->AddResolvedLayoutSiteRecordForTesting(FIntPoint(80, 16), CreateRuntimeContinuationSiteRecord(FIntVector(80, 16, 16), Binding->BindingId, LayoutGameplayTags::ConnectorRoad, ELayoutFaceDirection::NegX));
	auto* Store = Runtime->GetLayoutPlanningWindowStore();
	const auto Records = Store->GetContinuationEndpointRecords();
	if (!TestEqual(TEXT("Both roots export endpoints"), Records.Num(), 2)) return false;
	FResolvedLayoutConnectorEndpoint Picked;
	FString Failure, Edge;
	const FIntVector Hover = Records[0].Endpoint.EndpointBlockWorldPos;
	TestTrue(TEXT("Ready endpoint is selectable"), Runtime->TryFindContinuationEndpointAtBlock(Hover, Binding, Candidate.LayoutProfile, Picked, Failure));
	TestTrue(TEXT("Automatic route reserves endpoints"), Store->ReserveContinuationEndpointPair(Records[0].Endpoint, Records[1].Endpoint, Family.FamilyId, Candidate.CandidateId, Edge, Failure));
	TestFalse(TEXT("Reserved endpoint cannot be selected"), Runtime->TryFindContinuationEndpointAtBlock(Hover, Binding, Candidate.LayoutProfile, Picked, Failure));
	TestTrue(TEXT("Picker explains reservation instead of claiming no endpoint exists"), Failure.Contains(TEXT("reserved")));
	Store->ReleaseContinuationEndpointPair(Edge);
	TestTrue(TEXT("Completion/cancellation release makes endpoint selectable again"), Runtime->TryFindContinuationEndpointAtBlock(Hover, Binding, Candidate.LayoutProfile, Picked, Failure));
	TestTrue(TEXT("Released pair can be reserved again"), Store->ReserveContinuationEndpointPair(Records[0].Endpoint, Records[1].Endpoint, Family.FamilyId, Candidate.CandidateId, Edge, Failure));
	TestTrue(TEXT("Placed route consumes endpoints"), Store->ConsumeContinuationEndpointPair(Edge));
	TestFalse(TEXT("Committed endpoint cannot be selected"), Runtime->TryFindContinuationEndpointAtBlock(Hover, Binding, Candidate.LayoutProfile, Picked, Failure));
	TestEqual(TEXT("Committed endpoints are pruned from exported ledger"), Store->GetContinuationEndpointRecords().Num(), 0);
	TestTrue(TEXT("Missing-entry guidance explains committed entries are unavailable"), Failure.Contains(TEXT("committed entries")));
	TestFalse(TEXT("Unrelated block still has no endpoint"), Runtime->TryFindContinuationEndpointAtBlock(FIntVector(10000), Binding, Candidate.LayoutProfile, Picked, Failure));
	TestTrue(TEXT("Missing endpoint keeps distinct reason"), Failure.Contains(TEXT("No compatible exported")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimeEmptyBindingIdPublishesEndpointsTest,
	"PorismExtension.Layout.Runtime.EmptyBindingIdPublishesEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeEmptyBindingIdPublishesEndpointsTest::RunTest(const FString& Parameters)
{
	for (const bool bNamedBinding : {false, true})
	{
		auto* Runtime = NewObject<UChunkWorldLayoutRuntimeComponent>(GetTransientPackage());
		auto* Binding = NewObject<ULayoutWorldBindingAsset>(GetTransientPackage());
		Binding->BindingId = bNamedBinding ? FName(TEXT("AuthoredBinding")) : NAME_None;
		const FName EffectiveId = bNamedBinding ? Binding->BindingId : Binding->GetFName();
		auto& Family = Binding->ContinuationFamilies.AddDefaulted_GetRef();
		Family.FamilyId = TEXT("Road");
		Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
		Family.MaxConnectionsPerSite = 1;
		Runtime->SetLayoutWorldBindings({Binding});
		const auto Root = CreateRuntimeContinuationSiteRecord(FIntVector(16), EffectiveId, LayoutGameplayTags::ConnectorRoad);
		Runtime->AddResolvedLayoutSiteRecordForTesting(FIntPoint::ZeroValue, Root);
		const auto Endpoints = Runtime->GetLayoutPlanningWindowStore()->GetContinuationEndpointRecords();
		TestEqual(TEXT("Authored and fallback binding IDs both publish endpoints"), Endpoints.Num(), 1);
		if (!Endpoints.IsEmpty()) TestEqual(TEXT("Published endpoint keeps canonical ID"), Endpoints[0].WorldBindingId, EffectiveId);
		Runtime->AddResolvedLayoutSiteRecordForTesting(FIntPoint::ZeroValue, Root);
		TestEqual(TEXT("Republishing does not duplicate endpoints"), Runtime->GetLayoutPlanningWindowStore()->GetContinuationEndpointRecords().Num(), 1);
	}
	return true;
}

bool FLayoutRuntimeComponentKeepsSameReservationDirectRootsDistinctTest::RunTest(const FString& Parameters)
{
	UChunkWorldLayoutRuntimeComponent* const RuntimeComponent =
		NewObject<UChunkWorldLayoutRuntimeComponent>(GetTransientPackage());
	if (!TestNotNull(TEXT("Runtime component exists"), RuntimeComponent))
	{
		return false;
	}

	const FIntPoint SharedReservationKey(0, 0);
	ULayoutWorldBindingAsset* const WorldBinding = NewObject<ULayoutWorldBindingAsset>(GetTransientPackage());
	WorldBinding->BindingId = TEXT("SameReservationBinding");
	RuntimeComponent->SetLayoutWorldBindings({WorldBinding});
	const FGameplayTag ConnectorTag = FGameplayTag::RequestGameplayTag(
		TEXT("Layout.Connector.Test"), false);
	const FResolvedLayoutSiteRecord FirstRoot = CreateRuntimeContinuationSiteRecord(
		FIntVector(16, 16, 16), TEXT("SameReservationBinding"), ConnectorTag);
	const FResolvedLayoutSiteRecord SecondRoot = CreateRuntimeContinuationSiteRecord(
		FIntVector(32, 16, 16), TEXT("SameReservationBinding"), ConnectorTag);

	RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(SharedReservationKey, FirstRoot);
	RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(SharedReservationKey, SecondRoot);
	TestEqual(
		TEXT("Distinct direct roots in one reservation cell both remain cached"),
		RuntimeComponent->GetResolvedLayoutSiteRecords().Num(),
		2);

	RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(SharedReservationKey, FirstRoot);
	TestEqual(
		TEXT("Reapplying one direct root replaces only its own stable record"),
		RuntimeComponent->GetResolvedLayoutSiteRecords().Num(),
		2);
	return true;
}

bool FLayoutRuntimeComponentRefreshesFiniteCenteredZLowDeckSurfacePathContinuationFamilyTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZContinuationRuntimeTest(
		*this,
		TEXT("low-deck"),
		TEXT("SurfacePath"),
		TEXT("LayoutProfile_RuntimeFiniteCenteredZLowDeckSurfacePathContinuation"),
		TEXT("LayoutWorldBinding_RuntimeFiniteCenteredZLowDeckSurfacePathContinuation"),
		TEXT("SurfaceRoadFiniteCenteredZLevel0"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		FIntPoint(0, 0),
		FIntPoint(4, 0),
		FIntVector(0, 0, 32),
		FIntVector(64, 0, 32),
		392,
		4,
		0,
		0,
		FIntPoint(3, 3),
		1,
		2,
		0);
}

bool FLayoutRuntimeComponentRefreshesFiniteCenteredZLowDeckBridgeContinuationFamilyTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZContinuationRuntimeTest(
		*this,
		TEXT("low-deck"),
		TEXT("bridge"),
		TEXT("LayoutProfile_RuntimeFiniteCenteredZLowDeckBridgeContinuation"),
		TEXT("LayoutWorldBinding_RuntimeFiniteCenteredZLowDeckBridgeContinuation"),
		TEXT("BridgeRoadFiniteCenteredZLevel0"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		FIntPoint(0, 4),
		FIntPoint(4, 4),
		FIntVector(0, 64, 32),
		FIntVector(64, 64, 32),
		393,
		4,
		0,
		0,
		FIntPoint(3, 3),
		1,
		2,
		0);
}

bool FLayoutRuntimeComponentRefreshesFiniteCenteredZLowDeckTunnelContinuationFamilyTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZContinuationRuntimeTest(
		*this,
		TEXT("low-deck"),
		TEXT("tunnel"),
		TEXT("LayoutProfile_RuntimeFiniteCenteredZLowDeckTunnelContinuation"),
		TEXT("LayoutWorldBinding_RuntimeFiniteCenteredZLowDeckTunnelContinuation"),
		TEXT("TunnelRoadFiniteCenteredZLevel0"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		FIntPoint(0, 8),
		FIntPoint(4, 8),
		FIntVector(0, 128, 32),
		FIntVector(64, 128, 32),
		394,
		5,
		0,
		0,
		FIntPoint(3, 3),
		1,
		2,
		0);
}

bool FLayoutRuntimeComponentRefreshesFiniteCenteredZHigherEntrySurfacePathContinuationFamilyTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZContinuationRuntimeTest(
		*this,
		TEXT("higher-entry"),
		TEXT("SurfacePath"),
		TEXT("LayoutProfile_RuntimeFiniteCenteredZHigherEntrySurfacePathContinuation"),
		TEXT("LayoutWorldBinding_RuntimeFiniteCenteredZHigherEntrySurfacePathContinuation"),
		TEXT("SurfaceRoadFiniteCenteredZLevel1"),
		ELayoutWorldBindingContinuationFamilyType::SurfacePath,
		ELayoutWorldBindingPlacementKind::SurfacePath,
		LayoutGameplayTags::ConnectorRoad,
		FIntPoint(0, 0),
		FIntPoint(4, 0),
		FIntVector(0, 0, 32),
		FIntVector(64, 0, 32),
		390,
		0,
		1,
		0,
		FIntPoint(3, 3),
		2,
		2,
		1);
}

bool FLayoutRuntimeComponentRefreshesFiniteCenteredZHigherEntryBridgeContinuationFamilyTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZContinuationRuntimeTest(
		*this,
		TEXT("higher-entry"),
		TEXT("bridge"),
		TEXT("LayoutProfile_RuntimeFiniteCenteredZHigherEntryBridgeContinuation"),
		TEXT("LayoutWorldBinding_RuntimeFiniteCenteredZHigherEntryBridgeContinuation"),
		TEXT("BridgeRoadFiniteCenteredZLevel1"),
		ELayoutWorldBindingContinuationFamilyType::BridgeContinuation,
		ELayoutWorldBindingPlacementKind::BridgeContinuation,
		LayoutGameplayTags::ConnectorBridge,
		FIntPoint(0, 4),
		FIntPoint(4, 4),
		FIntVector(0, 64, 32),
		FIntVector(64, 64, 32),
		391,
		4,
		1,
		0,
		FIntPoint(3, 3),
		2,
		2,
		1);
}

bool FLayoutRuntimeComponentRefreshesFiniteCenteredZHigherEntryTunnelContinuationFamilyTest::RunTest(
	const FString& Parameters)
{
	return RunFiniteCenteredZContinuationRuntimeTest(
		*this,
		TEXT("higher-entry"),
		TEXT("tunnel"),
		TEXT("LayoutProfile_RuntimeFiniteCenteredZHigherEntryTunnelContinuation"),
		TEXT("LayoutWorldBinding_RuntimeFiniteCenteredZHigherEntryTunnelContinuation"),
		TEXT("TunnelRoadFiniteCenteredZLevel1"),
		ELayoutWorldBindingContinuationFamilyType::TunnelContinuation,
		ELayoutWorldBindingPlacementKind::TunnelContinuation,
		LayoutGameplayTags::ConnectorTunnel,
		FIntPoint(0, 8),
		FIntPoint(4, 8),
		FIntVector(0, 128, 32),
		FIntVector(64, 128, 32),
		395,
		5,
		1,
		0,
		FIntPoint(3, 3),
		2,
		2,
		1);
}
