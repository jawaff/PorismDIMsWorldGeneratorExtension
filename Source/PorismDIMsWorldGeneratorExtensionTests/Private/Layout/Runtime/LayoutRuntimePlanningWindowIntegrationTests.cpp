// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"

#include "Biome/Noise/WorldGenScaleContext.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/ChunkWorldLifecycleTypes.h"
#include "ChunkWorldStructs/ChunkWorldStructs.h"
#include "FastNoise/FastNoiseEditor.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutWorldBindingRuntimeView.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutWorldBindingSitePlanner.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Runtime/LayoutPlanningAreaQueue.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Terrain/LayoutWorldBindingTerrainFit.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Engine/World.h"
#include "Engine/Canvas.h"
#include "Debug/DebugDrawService.h"
#include "CanvasTypes.h"
#include "SceneView.h"
#include "LevelEditorViewport.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformProcess.h"
#include "Layout/Solver/LayoutSolveExecutionBudget.h"
#include "Misc/AutomationTest.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;
using namespace LayoutRegionScheduleSolverFacade;

namespace
{
	const FLayoutId DirectRootPlacementPolicyId(TEXT("DirectRootExplicit"));
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");
	const int32 BindingSurfaceSearchStartZ = 3;
	const int32 BindingSurfaceSearchDepthBlocks = 8;
	constexpr float PlanningWindowTimeoutBudgetSeconds = 1.0e-6f;

	FLayoutNoiseCoordinateSettings MakeCoordinateSettings(const UWorldGenDef* const WorldGenDef)
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

	FCompiledStructuralInputs BuildCompiledStructuralInputsForRequest(
		const FLayoutRegionSolveRequest& Request)
	{
		return BuildCompiledStructuralInputs(BuildSolveContext(Request));
	}

	int32 CountDistinctSupportSurfaceZs(const FLayoutSteppedTerrainSupportMap& SupportMap)
	{
		TSet<int32> DistinctSupportSurfaceZs;
		for (const FLayoutSteppedTerrainSupportSample& Sample : SupportMap.SupportSamples)
		{
			DistinctSupportSurfaceZs.Add(Sample.SupportSurfaceZ);
		}

		return DistinctSupportSurfaceZs.Num();
	}

	bool TryComputeRuntimeTerrainConformingSiteOffset(
		UChunkWorldLayoutRuntimeComponent* const RuntimeComponent,
		AChunkWorldExtended* const World,
		const int32 WorldSeed,
		const FResolvedLayoutSiteRecord& SiteRecord,
		FIntVector& OutTerrainConformingSiteOffset,
		FString& OutFailureReason)
	{
		OutTerrainConformingSiteOffset = FIntVector::ZeroValue;
		OutFailureReason.Reset();
		if (RuntimeComponent == nullptr || World == nullptr)
		{
			OutFailureReason = TEXT("Runtime terrain-fit offset requires a runtime component and world.");
			return false;
		}

		const FResolvedLayoutSiteSolvedPayload SolvedPayload =
			SiteRecord.GetResolvedSiteSolvedPayload();
		const FIntVector SharedCellSizeInBlocks =
			SolvedPayload.SolveResult.SharedCellSizeInBlocks;
		if (SharedCellSizeInBlocks == FIntVector::ZeroValue)
		{
			OutFailureReason = TEXT("Runtime terrain-fit offset requires a non-zero shared cell size.");
			return false;
		}

		const FName TerrainFitBiomeRowName =
			SiteRecord.GetWorldBindingFrontendSelection().BiomeRowName;
		FLayoutNoiseCoordinateSettings TerrainFitCoordinateSettings;
		FLayoutActiveBiomeSampler TerrainFitActiveBiomeSampler;
		const FLayoutNoiseCoordinateSettings* TerrainFitCoordinateSettingsPtr = nullptr;
		const FLayoutActiveBiomeSampler* TerrainFitActiveBiomeSamplerPtr = nullptr;
		if (SolvedPayload.SolveResult.RootPlacementKind
				== ELayoutWorldBindingPlacementKind::OrdinaryRoot
			&& World->WorldGenDef != nullptr
			&& !TerrainFitBiomeRowName.IsNone()
			&& TerrainFitActiveBiomeSampler.Initialize(
				RuntimeComponent,
				World->WorldGenDef,
				WorldSeed))
		{
			TerrainFitCoordinateSettings = MakeCoordinateSettings(World->WorldGenDef);
			TerrainFitCoordinateSettingsPtr = &TerrainFitCoordinateSettings;
			TerrainFitActiveBiomeSamplerPtr = &TerrainFitActiveBiomeSampler;
		}

		const FIntVector FootprintMinBlockWorldPos =
			UChunkWorldLayoutRuntimeComponent::ComputeFootprintMinBlockWorldPos(
				SiteRecord,
				SharedCellSizeInBlocks);
		FIntVector TerrainAnchorBlockWorldPos = FootprintMinBlockWorldPos;
		ELayoutWorldBindingTerrainFitDiagnosticKind TerrainFitDiagnosticKind =
			ELayoutWorldBindingTerrainFitDiagnosticKind::None;
		// Legacy ResolveRuntimeTerrainFit removed. Planning-window terrain-fit goes through adapter now.

		OutTerrainConformingSiteOffset =
			TerrainAnchorBlockWorldPos - FootprintMinBlockWorldPos;
		return true;
	}

	bool TryComputeExpectedPlanningWindowOrdinaryRootSiteCenter(
		UChunkWorldLayoutRuntimeComponent* const RuntimeComponent,
		AChunkWorldExtended* const World,
		ULayoutWorldBindingAsset* const WorldBinding,
		const FName BiomeRowName,
		const FIntPoint& SiteCenterBlockXY,
		FIntVector& OutSiteCenterBlockWorldPos,
		FString& OutFailureReason)
	{
		OutSiteCenterBlockWorldPos = FIntVector::ZeroValue;
		OutFailureReason.Reset();
		if (RuntimeComponent == nullptr || World == nullptr || World->WorldGenDef == nullptr)
		{
			OutFailureReason = TEXT("Planning-window site-center parity requires a runtime component and world.");
			return false;
		}

		if (WorldBinding == nullptr || WorldBinding->Candidates.IsEmpty())
		{
			OutFailureReason = TEXT("Planning-window site-center parity requires a world binding with one candidate.");
			return false;
		}

		FLayoutWorldBindingRuntimeView RuntimeView;
		if (!LayoutWorldBindingRuntimeView::TryBuildRuntimeViewFromWorldBindingCandidateId(
			WorldBinding,
			WorldBinding->Candidates[0].CandidateId,
			BiomeRowName,
			RuntimeView,
			OutFailureReason))
		{
			return false;
		}

		FLayoutActiveBiomeSampler ActiveBiomeSampler;
		if (!ActiveBiomeSampler.Initialize(
			RuntimeComponent,
			World->WorldGenDef,
			World->Seed))
		{
			OutFailureReason = TEXT("Planning-window site-center parity could not initialize the active biome sampler.");
			return false;
		}

		FLayoutActiveBiomeSurfaceSample Surface;
		if (!ActiveBiomeSampler.FindEligibleBiomeSurface(
			BiomeRowName,
			SiteCenterBlockXY,
			RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
			RuntimeView.PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
			MakeCoordinateSettings(World->WorldGenDef),
			Surface)
			|| !Surface.bIsValid)
		{
			OutFailureReason = TEXT("Planning-window site-center parity could not resolve an eligible active-biome surface.");
			return false;
		}

		OutSiteCenterBlockWorldPos = FIntVector(
			SiteCenterBlockXY.X,
			SiteCenterBlockXY.Y,
			[&]()
			{
				LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds FiniteAxisBounds;
				if (LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
						World,
						FiniteAxisBounds))
				{
					int32 SiteCenterZ = 0;
					if (!LayoutWorldBindingSitePlanner::TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
							RuntimeView.SharedCellSizeInBlocks.Z,
							Surface.SurfaceBlockWorldPos.Z + 1,
							FiniteAxisBounds,
							SiteCenterZ))
					{
						OutFailureReason = TEXT("Planning-window site-center parity rejected the site because the zero-anchored lattice stepped past the finite Z floor/ceiling.");
						return 0;
					}

					return SiteCenterZ;
				}

				return LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(
					RuntimeView.SharedCellSizeInBlocks.Z,
					Surface.SurfaceBlockWorldPos.Z + 1);
			}());
		if (!OutFailureReason.IsEmpty())
		{
			OutSiteCenterBlockWorldPos = FIntVector::ZeroValue;
			return false;
		}
		return true;
	}

	bool TryComputeExpectedObservedChunkOrdinaryRootSiteCenter(
		UChunkWorldLayoutRuntimeComponent* const RuntimeComponent,
		AChunkWorldExtended* const World,
		ULayoutWorldBindingAsset* const WorldBinding,
		const FName BiomeRowName,
		const FIntVector& CandidateSiteCenterBlockWorldPos,
		FIntVector& OutSiteCenterBlockWorldPos,
		FString& OutFailureReason)
	{
		return TryComputeExpectedPlanningWindowOrdinaryRootSiteCenter(
			RuntimeComponent,
			World,
			WorldBinding,
			BiomeRowName,
			FIntPoint(CandidateSiteCenterBlockWorldPos.X, CandidateSiteCenterBlockWorldPos.Y),
			OutSiteCenterBlockWorldPos,
			OutFailureReason);
	}

	bool TryGetObservedChunkPublishedRootMetadata(
		UChunkWorldLayoutRuntimeComponent* const RuntimeComponent,
		const FIntVector& SiteCenterBlockWorldPos,
		FLayoutRootPublicationMetadata& OutPublicationMetadata,
		FString& OutFailureReason)
	{
		OutPublicationMetadata = FLayoutRootPublicationMetadata();
		OutFailureReason.Reset();
		if (RuntimeComponent == nullptr)
		{
			OutFailureReason = TEXT("Observed-chunk publication metadata requires a runtime component.");
			return false;
		}

		ULayoutPlanningWindowStore* const Store = RuntimeComponent->GetLayoutPlanningWindowStore();
		if (Store == nullptr)
		{
			OutFailureReason = TEXT("Observed-chunk publication metadata requires a planning store.");
			return false;
		}

		const FString PlanningRecordKey = RuntimeComponent->GetPlanningRecordKeyForTesting(
			FLayoutSiteReservation::ComputeReservationKey(SiteCenterBlockWorldPos));
		if (PlanningRecordKey.IsEmpty())
		{
			OutFailureReason = TEXT("Observed-chunk publication metadata could not resolve a planning-store key.");
			return false;
		}

		FPlannedLayoutSiteRecord StoredRecord;
		if (!Store->TryGetPlannedLayoutSiteRecord(PlanningRecordKey, StoredRecord))
		{
			OutFailureReason = TEXT("Observed-chunk publication metadata could not load the stored planning record.");
			return false;
		}

		OutPublicationMetadata = StoredRecord.GetRootPublicationMetadata();
		return true;
	}

	ULayoutRegionContentSetAsset* CreateOneCellPlanningStampContentSet(
		UObject* Outer,
		AChunkWorldExtended* World,
		const FIntVector& CellSizeInBlocks);

	ULayoutProfileAsset* CreateOneCellPlanningProfile(UObject* Outer);

	void ConfigurePlanningBiomeRow(UWorldGenDef* WorldGenDef, const FName BiomeRowName);

	ULayoutWorldBindingAsset* CreateWorldBinding(
		UObject* Outer,
		const FName BindingId,
		const FName CandidateId,
		const FName BiomeRowName,
		ULayoutProfileAsset* Profile,
		const bool bPopulateSurfaceSearch,
		const float MaxSolveDurationSeconds,
		const FIntVector& BaseCellDimensionsBlocks);

	bool ConfigureOneCellPlanningWindowRealizationFixture(
		FAutomationTestBase& Test,
		const FName BiomeRowName,
		FLayoutWorldTestHarness& Harness,
		ULayoutWorldBindingAsset*& OutWorldBinding,
		ULayoutRegionContentSetAsset*& OutContentSet,
		ULayoutPlanningWindowStore*& OutStore)
	{
		OutWorldBinding = nullptr;
		OutContentSet = nullptr;
		OutStore = nullptr;
		if (!Test.TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
			|| !Test.TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
			|| !Test.TestNotNull(TEXT("Chunk-world harness creates a WorldGenDef"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr))
		{
			return false;
		}

		ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
		FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 3));

		OutContentSet = CreateOneCellPlanningStampContentSet(GetTransientPackage(), Harness.World, FIntVector(1, 1, 1));
		// Replace default module with permissive variant for 1x1 CSP compatibility.
		{
			UChunkStructureTemplate* PermissiveTemplate = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_PermissiveFixture"), FIntVector(1, 1, 1));
			FLayoutTestWorldSupport::ConfigureSolidTemplate(PermissiveTemplate, Harness.World, FIntVector(1, 1, 1), FIntVector::ZeroValue, SinfullMaterial);
			ULayoutModuleAsset* PermissiveModule = CreateModule(
				GetTransientPackage(),
				TEXT("LayoutModule_PermissiveFixture"),
				PermissiveTemplate,
				{ELayoutCellIntent::Boundary},
				BuildFilledCubeFaces(
					MakeTags({LayoutGameplayTags::FaceSolid}),
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::FaceSolid}),
					MakeTags({LayoutGameplayTags::FaceSolid}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeTags({LayoutGameplayTags::TraversalPrimary}),
				{},
				FGameplayTagContainer());
			PermissiveModule->Roles = {ELayoutModuleRole::Boundary};
			PermissiveModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
			PermissiveModule->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
			PermissiveModule->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
			PermissiveModule->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
			PermissiveModule->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
			PermissiveModule->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
			OutContentSet->Entries[0].ModuleSettings.Module = PermissiveModule;
		}
		ULayoutProfileAsset* Profile = CreateOneCellPlanningProfile(GetTransientPackage());
		Profile->ContentSet = OutContentSet;
		OutWorldBinding = CreateWorldBinding(
			GetTransientPackage(),
			FName(*FString::Printf(TEXT("%sBinding"), *BiomeRowName.ToString())),
			TEXT("PrimaryCandidate"),
			BiomeRowName,
			Profile,
			true,
			2.0f,
			FIntVector(1, 1, 1));

		OutStore = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
		if (!Test.TestNotNull(TEXT("Planning-window runtime exposes a store"), OutStore))
		{
			return false;
		}

		FLayoutPlanningWindowSettings Settings;
		Settings.SampleSpacing = 4;
		Settings.SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;
		return Test.TestTrue(TEXT("Planning-window settings are valid"), OutStore->SetPlanningWindowSettings(Settings).IsValid());
	}

	bool TryResolveOneCellPlanningWindowRequiredChunkOrigins(
		FAutomationTestBase& Test,
		const FName BiomeRowName,
		TSet<FIntVector>& OutRequiredChunkOrigins)
	{
		OutRequiredChunkOrigins.Reset();
		FLayoutWorldTestHarness ProbeHarness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
		ULayoutWorldBindingAsset* WorldBinding = nullptr;
		ULayoutRegionContentSetAsset* ContentSet = nullptr;
		ULayoutPlanningWindowStore* Store = nullptr;
		if (!ConfigureOneCellPlanningWindowRealizationFixture(
				Test,
				BiomeRowName,
				ProbeHarness,
				WorldBinding,
				ContentSet,
				Store))
		{
			return false;
		}

		ProbeHarness.RuntimeComponent->SetLayoutWorldBindings({WorldBinding});
		ProbeHarness.TrackPlanningCharacter(FIntVector(8, 8, 0));
		ProbeHarness.World->OnChunkCreate(FIntVector::ZeroValue, ProbeHarness.World->GetChunkLayerCount() - 1, {});
		ProbeHarness.RuntimeComponent->RunQueuedLayoutWorkForTesting();
		const TArray<FResolvedLayoutSiteRecord> SiteRecords = ProbeHarness.RuntimeComponent->GetResolvedLayoutSiteRecords();
		if (!Test.TestEqual(TEXT("Required-chunk-origin probe imports one resolved site"), SiteRecords.Num(), 1))
		{
			return false;
		}

		const FIntVector SharedCellSizeInBlocks = LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(
			SiteRecords[0].SolveResult,
			ContentSet);
		OutRequiredChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
			SiteRecords[0],
			SharedCellSizeInBlocks,
			ProbeHarness.World->WorldGenDef->ChunkBlockSize);
		if (!Test.TestTrue(TEXT("Required-chunk-origin probe finds at least one required chunk origin"), OutRequiredChunkOrigins.Num() > 0))
		{
			return false;
		}

		return true;
	}

	ULayoutRegionContentSetAsset* CreateOneCellPlanningStampContentSet(
		UObject* Outer,
		AChunkWorldExtended* World,
		const FIntVector& CellSizeInBlocks = FIntVector(1, 1, 1))
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_PlanningWindowStamp"), CellSizeInBlocks);
		FLayoutTestWorldSupport::ConfigureSolidTemplate(Template, World, CellSizeInBlocks, FIntVector::ZeroValue, SinfullMaterial);

		ULayoutModuleAsset* Module = CreateModule(
			Outer,
			TEXT("LayoutModule_PlanningWindowStamp"),
			Template,
			{ELayoutCellIntent::Boundary},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceSolid}),
				MakeTags({LayoutGameplayTags::FaceSolid}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			{},
			FGameplayTagContainer());
		Module->Roles = {ELayoutModuleRole::Boundary};

		FLayoutRegionContentEntry Entry;
		Entry.EntryId = TEXT("PlanningStamp");
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		Entry.Weight = 1;
		ULayoutRegionContentSetAsset* const ContentSet =
			CreateRegionContentSet(Outer, TEXT("LayoutContentSet_PlanningWindowStamp"), {Entry});
		return ContentSet;
	}

	ULayoutRegionContentSetAsset* CreateSteppedPlanningStampContentSet(
		UObject* Outer,
		AChunkWorldExtended* World)
	{
		const FIntVector CellSizeInBlocks(16, 16, 16);
		UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_PlanningWindowSteppedStamp"), CellSizeInBlocks);
		FLayoutTestWorldSupport::ConfigureSolidTemplate(Template, World, CellSizeInBlocks, FIntVector::ZeroValue, SinfullMaterial);

		ULayoutModuleAsset* Module = CreateModule(
			Outer,
			TEXT("LayoutModule_PlanningWindowSteppedStamp"),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::VerticalAccess},
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
		Entry.EntryId = TEXT("PlanningSteppedStamp");
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.Module = Module;
		Entry.Weight = 1;

		ULayoutRegionContentSetAsset* const ContentSet =
			CreateRegionContentSet(Outer, TEXT("LayoutContentSet_PlanningWindowSteppedStamp"), {Entry});
		return ContentSet;
	}

	ULayoutRegionContentSetAsset* CreateParentChildPlanningStampContentSet(
		UObject* Outer,
		AChunkWorldExtended* World,
		ULayoutProfileAsset*& OutChildProfile,
		ULayoutModuleAsset*& OutParentModule,
		ULayoutModuleAsset*& OutChildModule,
		const int32 ParentMaterial,
		const int32 ChildMaterial)
	{
		UChunkStructureTemplate* ParentTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_PlanningParentStamp"), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(ParentTemplate, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, ParentMaterial);

		ULayoutModuleAsset* ParentModule = CreateModule(
			Outer,
			TEXT("LayoutModule_PlanningParentStamp"),
			ParentTemplate,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
		ParentModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};
		OutParentModule = ParentModule;

		UChunkStructureTemplate* ChildTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_PlanningChildStamp"), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(ChildTemplate, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, ChildMaterial);

		ULayoutModuleAsset* ChildModule = CreateModule(
			Outer,
			TEXT("LayoutModule_PlanningChildStamp"),
			ChildTemplate,
			{ELayoutCellIntent::Boundary},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor));
		ChildModule->Roles = {ELayoutModuleRole::Boundary};
		OutChildModule = ChildModule;

		FLayoutRegionContentEntry ChildModuleEntry;
		ChildModuleEntry.EntryId = TEXT("PlanningChildShell");
		ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildModuleEntry.ModuleSettings.Module = ChildModule;
		ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_PlanningChild"),
			{ChildModuleEntry});

		OutChildProfile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningChild"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		OutChildProfile->ContentSet = ChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("PlanningParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = ParentModule;

		FLayoutRegionContentEntry ChildRegionEntry;
		ChildRegionEntry.EntryId = TEXT("PlanningRoom");
		ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		ChildRegionEntry.ChildRegionSettings.RegionProfile = OutChildProfile;
		ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		ULayoutRegionContentSetAsset* const ContentSet = CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_PlanningParentChild"),
			{ParentModuleEntry, ChildRegionEntry});
		return ContentSet;
	}

	ULayoutRegionContentSetAsset* CreateCompositePlanningStampContentSet(
		UObject* Outer,
		AChunkWorldExtended* World)
	{
		UChunkStructureTemplate* FirstTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_PlanningCompositeFirst"), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(FirstTemplate, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, SinfullMaterial);

		ULayoutModuleAsset* FirstLeaf = CreateModule(
			Outer,
			TEXT("LayoutModule_PlanningCompositeFirst"),
			FirstTemplate,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			{},
			FGameplayTagContainer());
		FirstLeaf->Roles = {ELayoutModuleRole::Boundary};
		// PosX faces the second cell — allow the FaceOpen tag so the pair connects.
		// Do NOT set BoundaryRequirement on PosX; it faces another composite cell.
		FirstLeaf->FaceRules.PosX.AllowedConnectionTags.AppendTags(MakeTags({LayoutGameplayTags::FaceOpen}));
		FirstLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		UChunkStructureTemplate* SecondTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_PlanningCompositeSecond"), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(SecondTemplate, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, SinfullMaterial + 11);

		ULayoutModuleAsset* SecondLeaf = CreateModule(
			Outer,
			TEXT("LayoutModule_PlanningCompositeSecond"),
			SecondTemplate,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})),
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			{},
			FGameplayTagContainer());
		SecondLeaf->Roles = {ELayoutModuleRole::Boundary};
		// NegX faces the first cell — allow the FaceOpen tag so the pair connects.
		// Do NOT set BoundaryRequirement on NegX; it faces another composite cell.
		SecondLeaf->FaceRules.NegX.AllowedConnectionTags.AppendTags(MakeTags({LayoutGameplayTags::FaceOpen}));
		SecondLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_PlanningPair"));
		FLayoutCompositeModuleCell& FirstCell = Composite->Cells.AddDefaulted_GetRef();
		FirstCell.Module = FirstLeaf;
		FirstCell.LocalCell = FIntVector(0, 0, 0);
		FirstCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& SecondCell = Composite->Cells.AddDefaulted_GetRef();
		SecondCell.Module = SecondLeaf;
		SecondCell.LocalCell = FIntVector(1, 0, 0);
		SecondCell.RelativeYawRotationSteps = 0;

		FLayoutRegionContentEntry Entry;
		Entry.EntryId = TEXT("PlanningCompositeStamp");
		Entry.ContentKind = ELayoutRegionContentKind::Module;
		Entry.ModuleSettings.CompositeModule = Composite;
		Entry.Weight = 1;
		ULayoutRegionContentSetAsset* const ContentSet =
			CreateRegionContentSet(Outer, TEXT("LayoutContentSet_PlanningCompositeStamp"), {Entry});
		return ContentSet;
	}

	ULayoutProfileAsset* CreateOneCellPlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningWindowStamp"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		Profile->EntryCount = 0;
		Profile->EntryCountMode = ELayoutCountConstraintMode::None;
		Profile->MinEntryCount = 0;
		Profile->MaxEntryCount = 0;
		return Profile;
	}

	ULayoutProfileAsset* CreateSteppedPlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningWindowSteppedStamp"),
			FIntPoint(2, 1),
			FIntPoint(2, 1),
			2,
			0,
			false);
		Profile->bSupportsSteppedTerrainSolve = true;
		return Profile;
	}

	ULayoutProfileAsset* CreateSteppedCorridorPlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningWindowSteppedCorridorStamp"),
			FIntPoint(3, 1),
			FIntPoint(3, 1),
			2,
			0,
			false);
		Profile->bSupportsSteppedTerrainSolve = true;
		return Profile;
	}

	ULayoutModuleAsset* CreateSingleCellPlanningVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::VerticalAccess},
			{
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosX,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegX,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary}))
			});
	}

	ULayoutModuleAsset* CreateSingleCellPlanningJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::VerticalAccess},
			{
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosX,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegX,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsAnyNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary}))
			});
	}

	ULayoutModuleAsset* CreateSingleCellPlanningStructuralModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
			BuildFilledCubeFaces(
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::FaceOpen}),
				MakeTags({LayoutGameplayTags::FaceOpen}),
				ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
				MakeTags({LayoutGameplayTags::TraversalPrimary})));
	}

	ULayoutModuleAsset* CreateSingleCellPlanningEntryModule(UObject* Outer, const TCHAR* BaseName)
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			FIntVector(16, 16, 16));
		const FGameplayTagContainer OpenTags = MakeTags({LayoutGameplayTags::FaceOpen});
		const FGameplayTagContainer OpenAndEntryTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer OpenSolidAndEntryTags = MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceSolid, LayoutGameplayTags::FaceEntry});
		const FGameplayTagContainer SolidTags = MakeTags({LayoutGameplayTags::FaceSolid});
		const FGameplayTagContainer PrimaryTraversal = MakeTags({LayoutGameplayTags::TraversalPrimary});
		return CreateModule(
			Outer,
			*FString::Printf(TEXT("%s_Module"), BaseName),
			Template,
			{ELayoutCellIntent::Entry},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), OpenAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutRegionContentSetAsset* CreateSteppedMultiTransitionPlanningContentSet(UObject* Outer)
	{
		ULayoutModuleAsset* EdgeVerticalAccessModule = CreateSingleCellPlanningVerticalAccessModule(
			Outer,
			TEXT("LayoutPlanningMultiTransitionEdgeVerticalAccess"));
		ULayoutModuleAsset* JunctionVerticalAccessModule = CreateSingleCellPlanningJunctionVerticalAccessModule(
			Outer,
			TEXT("LayoutPlanningMultiTransitionJunctionVerticalAccess"));
		ULayoutModuleAsset* StructuralModule = CreateSingleCellPlanningStructuralModule(
			Outer,
			TEXT("LayoutPlanningMultiTransitionStructural"));

		FLayoutRegionContentEntry EdgeVerticalAccessEntry;
		EdgeVerticalAccessEntry.EntryId = TEXT("LayoutPlanningMultiTransitionEdgeVerticalAccessEntry");
		EdgeVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		EdgeVerticalAccessEntry.ModuleSettings.Module = EdgeVerticalAccessModule;
		EdgeVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

		FLayoutRegionContentEntry JunctionVerticalAccessEntry;
		JunctionVerticalAccessEntry.EntryId = TEXT("LayoutPlanningMultiTransitionJunctionVerticalAccessEntry");
		JunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		JunctionVerticalAccessEntry.ModuleSettings.Module = JunctionVerticalAccessModule;
		JunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry StructuralEntry;
		StructuralEntry.EntryId = TEXT("LayoutPlanningMultiTransitionStructuralEntry");
		StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
		StructuralEntry.ModuleSettings.Module = StructuralModule;

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_PlanningWindowMultiTransition"),
			{StructuralEntry, EdgeVerticalAccessEntry, JunctionVerticalAccessEntry});
	}

	ULayoutRegionContentSetAsset* CreateSteppedThreeColumnPlanningContentSet(UObject* Outer)
	{
		ULayoutModuleAsset* EntryModule = CreateSingleCellPlanningEntryModule(
			Outer,
			TEXT("LayoutPlanningThreeColumnEntry"));
		ULayoutModuleAsset* JunctionVerticalAccessModule = CreateSingleCellPlanningJunctionVerticalAccessModule(
			Outer,
			TEXT("LayoutPlanningThreeColumnJunctionVerticalAccess"));
		ULayoutModuleAsset* StructuralModule = CreateSingleCellPlanningStructuralModule(
			Outer,
			TEXT("LayoutPlanningThreeColumnStructural"));

		FLayoutRegionContentEntry EntryModuleEntry;
		EntryModuleEntry.EntryId = TEXT("LayoutPlanningThreeColumnEntryModuleEntry");
		EntryModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		EntryModuleEntry.ModuleSettings.Module = EntryModule;
		EntryModuleEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

		FLayoutRegionContentEntry JunctionVerticalAccessEntry;
		JunctionVerticalAccessEntry.EntryId = TEXT("LayoutPlanningThreeColumnJunctionVerticalAccessEntry");
		JunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
		JunctionVerticalAccessEntry.ModuleSettings.Module = JunctionVerticalAccessModule;
		JunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

		FLayoutRegionContentEntry StructuralEntry;
		StructuralEntry.EntryId = TEXT("LayoutPlanningThreeColumnStructuralEntry");
		StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
		StructuralEntry.ModuleSettings.Module = StructuralModule;

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_PlanningWindowThreeColumn"),
			{StructuralEntry, EntryModuleEntry, JunctionVerticalAccessEntry});
	}

	ULayoutProfileAsset* CreateSteppedThreeColumnPlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningWindowThreeColumn"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			2,
			0,
			false);
		Profile->bSupportsSteppedTerrainSolve = true;
		Profile->bRequireAllTraversalChannelsReachable = false;
		Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
		Profile->VerticalAccessCount = 1;
		Profile->MinVerticalAccessCount = 1;
		Profile->MaxVerticalAccessCount = 1;
		return Profile;
	}

	ULayoutProfileAsset* CreateSteppedMultiTransitionPlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningWindowMultiTransition"),
			FIntPoint(5, 3),
			FIntPoint(5, 3),
			2,
			0,
			false);
		Profile->bSupportsSteppedTerrainSolve = true;
		Profile->bRequireAllTraversalChannelsReachable = false;
		Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
		Profile->VerticalAccessCount = 3;
		Profile->MinVerticalAccessCount = 3;
		Profile->MaxVerticalAccessCount = 3;
		return Profile;
	}

	FLayoutWorldBindingPlacementPolicy BuildSteppedMultiTransitionPlanningPlacementPolicy()
	{
		FLayoutWorldBindingPlacementPolicy Settings;
		Settings.SurfaceSearch.TerrainSearchStartZ = 10;
		Settings.SurfaceSearch.TerrainSearchDepthBlocks = 40;
		Settings.TerrainSampleGridSpacing = 16;
		Settings.HeightIgnoreThreshold = 0;
		Settings.TerrainTransition.bAllowFoundationFill = true;
		Settings.TerrainTransition.MaxFoundationDepth = 3;
		return Settings;
	}

	ULayoutProfileAsset* CreateParentChildPlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningParentChild"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			1,
			0,
			false);
		return Profile;
	}

	ULayoutProfileAsset* CreateCompositePlanningProfile(UObject* Outer)
	{
		ULayoutProfileAsset* Profile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PlanningComposite"),
			FIntPoint(2, 1),
			FIntPoint(2, 1),
			1,
			0,
			false);
		return Profile;
	}

	int32 CountVerticalAccessPlacements(const FLayoutSolveResult& SolveResult)
	{
		int32 VerticalAccessPlacementCount = 0;
		for (const FLayoutPlacedModule& Placement : SolveResult.Placements)
		{
			if (Placement.Intent == ELayoutCellIntent::VerticalAccess)
			{
				++VerticalAccessPlacementCount;
			}
		}

		return VerticalAccessPlacementCount;
	}

	void PublishRequestOwnedSteppedCarriersOnMergedSolveResult(
		const FLayoutRegionSolveRequest& SolveRequest,
		FLayoutRegionSolveScheduleResult& InOutScheduleResult)
	{
		FLayoutSolveResult& MergedSolveResult = InOutScheduleResult.MergedSolveResult;
		if (MergedSolveResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks <= 0
			&& MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty()
			&& MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.IsEmpty())
		{
			MergedSolveResult.SteppedTerrainSupportMap = SolveRequest.SteppedTerrainSupportMap;
		}

		if (MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
		{
			MergedSolveResult.ForcedPlacementBundleInsertions =
				SolveRequest.ForcedPlacementBundleInsertions;
		}

		if (MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
		{
			MergedSolveResult.RequestOwnedRequiredRouteConstraints =
				SolveRequest.RequiredRouteConstraints;
		}
	}

	int32 NormalizePlanningCompositeYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	FIntVector RotatePlanningCompositeCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		switch (NormalizePlanningCompositeYawRotationSteps(YawRotationSteps))
		{
		case 1:
			return FIntVector(FootprintSize.Y - 1 - Cell.Y, Cell.X, Cell.Z);
		case 2:
			return FIntVector(FootprintSize.X - 1 - Cell.X, FootprintSize.Y - 1 - Cell.Y, Cell.Z);
		case 3:
			return FIntVector(Cell.Y, FootprintSize.X - 1 - Cell.X, Cell.Z);
		case 0:
		default:
			return Cell;
		}
	}

	void ConfigurePlanningBiomeRow(UWorldGenDef* WorldGenDef, const FName BiomeRowName)
	{
		check(WorldGenDef != nullptr);
		WorldGenDef->WorldBiomes.Reset();
		WorldGenDef->WorldBiomesDT = nullptr;
		WorldGenDef->NoiseScale = FVector::OneVector;
		WorldGenDef->NoiseCoordinateOffset = FIntVector::ZeroValue;
		WorldGenDef->WorldGen.Reset();
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

		FBiomeDualData Row;
		Row.BiomeName = BiomeRowName.ToString();
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef, TEXT("FNE_PlanningWindowFlatSolid"));
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
		WorldGenDef->WorldBiomes.Add(Row);
	}

	ULayoutWorldBindingAsset* CreateWorldBinding(
		UObject* Outer,
		const FName BindingId,
		const FName CandidateId,
		const FName BiomeRowName,
		ULayoutProfileAsset* Profile,
		const bool bPopulateSurfaceSearch = true,
		const float MaxSolveDurationSeconds = 2.0f,
		const FIntVector& BaseCellDimensionsBlocks = FIntVector(1, 1, 1))
	{
		ULayoutWorldBindingAsset* Binding = NewObject<ULayoutWorldBindingAsset>(Outer, BindingId);
		Binding->BindingId = BindingId;
		Binding->BiomeRowNames = {BiomeRowName};
		Binding->BaseCellDimensionsBlocks = BaseCellDimensionsBlocks;
		if (bPopulateSurfaceSearch)
		{
			Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = BindingSurfaceSearchStartZ;
			Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = BindingSurfaceSearchDepthBlocks;
		}
		Binding->SolveBudget.MaxSolveDurationSeconds = MaxSolveDurationSeconds;
		FLayoutWorldBindingCandidate& Candidate = Binding->Candidates.AddDefaulted_GetRef();
		Candidate.CandidateId = CandidateId;
		Candidate.LayoutProfile = Profile;
		Candidate.Weight = 1;
		return Binding;
	}

}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowPrioritizesNearestCenterTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.PrioritizesNearestCenter",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimePlanningWindowSolveDiagnosticsTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.SolveDiagnostics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowSolveDiagnosticsTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	ULayoutWorldBindingAsset* Binding = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = nullptr;
	ULayoutPlanningWindowStore* Store = nullptr;
	if (!ConfigureOneCellPlanningWindowRealizationFixture(*this, TEXT("SolveDiagnostics"), Harness, Binding, ContentSet, Store)) return false;
	// The shared fixture is all-solid; explicit placement needs an observed air/surface transition.
	// Reuse the established procedural graph with surface Z=49 (cavity below is irrelevant here).
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAANIC3uQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAADDZCq6AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 60;
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 64;
	Harness.World->SetDetailedDiagnostics(true);
	Harness.RuntimeComponent->SetLayoutSolveExecutionForTesting(MakeUnique<FSynchronousLayoutSolveExecution>());
	Harness.RuntimeComponent->SetLayoutWorldBindings({Binding});
	for (const TCHAR* Origin : {TEXT("automatic"), TEXT("explicit")})
		for (const TCHAR* Phase : {TEXT("site-preparation"), TEXT("prewarm"), TEXT("preflight"), TEXT("solve")})
			for (const TCHAR* Event : {TEXT("begin"), TEXT("end succeeded=1")})
				AddExpectedMessage(FString::Printf(TEXT("\\[LayoutSolveDiag\\] origin=%s .* phase=%s event=%s"), Origin, Phase, Event),
					ELogVerbosity::Display);
	for (const TCHAR* Origin : {TEXT("automatic"), TEXT("explicit")})
		AddExpectedMessage(FString::Printf(TEXT("\\[LayoutSolveDiag\\] origin=%s .* event=terrain-writes fill="), Origin), ELogVerbosity::Display);
	Harness.TrackPlanningCharacter(FIntVector(8, 8, 49));
	Harness.World->OnChunkCreate(FIntVector(0, 0, 48), Harness.World->GetChunkLayerCount() - 1, {});
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	const auto Records = Store->GetPlannedLayoutSiteRecords();
	if (!TestEqual(TEXT("Diagnostics preserve one automatic root record"), Records.Num(), 1)) return false;
	if (!TestTrue(TEXT("Automatic diagnostics accompany a solved or immediately realized root"),
		Records[0].State == EPlannedLayoutSiteState::Accepted || Records[0].State == EPlannedLayoutSiteState::Realized)) return false;
	FResolvedLayoutSiteRecord Site;
	FLayoutRegionSolveScheduleResult Schedule;
	FString Failure;
	TestTrue(TEXT("Explicit root uses the same captured diagnostics path"), Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
		Records[0].GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos,
		Binding, Binding->Candidates[0].LayoutProfile, 123, Site, Schedule, &Failure));
	if (!Failure.IsEmpty()) AddInfo(Failure);
	TestTrue(TEXT("Both runtime paths emitted every expected phase"), HasMetExpectedMessages(ELogVerbosity::Display));
	return true;
}

bool FLayoutRuntimePlanningWindowPrioritizesNearestCenterTest::RunTest(const FString& Parameters)
{
	const auto ComputePriority = &FLayoutPlanningAreaQueue::ComputePriority;
	const TArray<FIntVector> Centers = {FIntVector(100000, -100000, 100), FIntVector(-200000, 0, -100)};
	TestTrue(TEXT("Near-character work outranks work near world origin"),
		ComputePriority(FIntVector(100001, -100000, 100), Centers)
			> ComputePriority(FIntVector::ZeroValue, Centers));
	TestEqual(TEXT("Any active center can give a site near priority"),
		ComputePriority(FIntVector(-200002, 0, -100), Centers), -2);
	TestEqual(TEXT("Underground distance participates in priority"),
		ComputePriority(FIntVector(100000, -100000, 120), Centers), -20);
	const TArray<FIntVector> ReversedCenters = {Centers[1], Centers[0]};
	TestEqual(TEXT("Center iteration order cannot change priority"),
		ComputePriority(FIntVector(-200002, 0, -100), ReversedCenters), -2);
	const TArray<FIntVector> ExtremeCenters = {FIntVector(MIN_int32, 0, 0)};
	TestEqual(TEXT("Far-world subtraction saturates without overflow"),
		ComputePriority(FIntVector(MAX_int32, 0, 0), ExtremeCenters), -MAX_int32);
	TestEqual(TEXT("No active centers gives lowest priority"),
		ComputePriority(FIntVector::ZeroValue, TConstArrayView<FIntVector>()), MIN_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowRealizesImportedSiteUsingFreshChunkObservedBeforeImportTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.RealizesImportedSiteUsingFreshChunkObservedBeforeImport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)





IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowRejectsFiniteCenteredZCeilingOverflowBeforeAcceptanceTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.RejectsFiniteCenteredZCeilingOverflowBeforeAcceptance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowNoPlayersPrunesEndpointsTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.NoPlayersPrunesEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowNoPlayersPrunesEndpointsTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestEqual(TEXT("Harness has no player controllers"), Harness.World->GetWorld()->GetNumPlayerControllers(), 0))
	{
		return false;
	}
	// This test owns no eligible centers; a real editor camera now counts as a center.
	FindFProperty<FBoolProperty>(Harness.RuntimeComponent->GetClass(), TEXT("bFollowEditorCamera"))
		->SetPropertyValue_InContainer(Harness.RuntimeComponent, false);
	Harness.RuntimeComponent->SetLayoutWorldBindings({ NewObject<ULayoutWorldBindingAsset>(GetTransientPackage()) });
	ULayoutPlanningWindowStore* const Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	FLayoutPlanningWindowEndpointRecord Endpoint;
	Endpoint.StableEndpointKey = TEXT("UnusedRoot/Entry");
	Endpoint.RootRecordKey = TEXT("UnusedRoot");
	Endpoint.ContinuationFamilyId = TEXT("UnusedFamily");
	Endpoint.RemainingConnections = 1;
	Endpoint.State = ELayoutContinuationEndpointState::Ready;
	FLayoutPlanningWindowEndpointRecord Stored;
	TestTrue(TEXT("Unused endpoint publishes before runtime pass"), Store->UpsertContinuationEndpointRecord(Endpoint, Stored));
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	TestEqual(TEXT("No-player runtime pass prunes unused endpoint"), Store->GetContinuationEndpointRecords().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowEditorCameraTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.EditorCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowEditorCameraTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	FindFProperty<FBoolProperty>(Harness.RuntimeComponent->GetClass(), TEXT("bFollowEditorCamera"))
		->SetPropertyValue_InContainer(Harness.RuntimeComponent, true);
	class FTestViewport final : public FLevelEditorViewportClient
	{
	public:
		explicit FTestViewport(UWorld* InWorld) : FLevelEditorViewportClient(nullptr), TestWorld(InWorld)
		{
			SetViewportType(LVT_Perspective);
		}
		virtual UWorld* GetWorld() const override { return TestWorld; }
		UWorld* TestWorld;
	} Camera(Harness.World->GetWorld());
	TGuardValue<FLevelEditorViewportClient*> ActiveCamera(GCurrentLevelEditingViewportClient, &Camera);
	const FVector CameraPosition(10000, 20000, 300);
	Camera.SetViewLocation(CameraPosition);
	const FIntVector CameraBlock = Harness.World->UEWorldPosToBlockWorldPos(CameraPosition);
	TestTrue(TEXT("Editor component tick enabled"), Harness.RuntimeComponent->bTickInEditor);
	TestTrue(TEXT("Camera-only center exists"), Harness.RuntimeComponent->CollectPlanningWindowCenters().Contains(CameraBlock));

	APlayerController* const Controller = Harness.World->GetWorld()->SpawnActor<APlayerController>();
	APawn* const Pawn = Harness.World->GetWorld()->SpawnActor<APawn>();
	Pawn->SetActorLocation(FVector(20000, 10000, 300));
	Controller->Possess(Pawn);
	Harness.World->GetWorld()->AddController(Controller);
	const FIntVector PawnBlock = Harness.World->UEWorldPosToBlockWorldPos(Pawn->GetActorLocation());
	const TArray<FIntVector> Combined = Harness.RuntimeComponent->CollectPlanningWindowCenters();
	TestTrue(TEXT("Camera remains alongside character"), Combined.Contains(CameraBlock));
	TestTrue(TEXT("Character remains alongside camera"), Combined.Contains(PawnBlock));
	Camera.SetViewLocation(Pawn->GetActorLocation());
	TestEqual(TEXT("Equivalent camera and character positions deduplicate"), Harness.RuntimeComponent->CollectPlanningWindowCenters().FilterByPredicate(
		[PawnBlock](const FIntVector& Position) { return Position == PawnBlock; }).Num(), 1);
	Camera.SetViewLocation(CameraPosition);
	Camera.TestWorld = nullptr;
	TestFalse(TEXT("Foreign-world active camera is excluded"), Harness.RuntimeComponent->CollectPlanningWindowCenters().Contains(CameraBlock));
	Camera.TestWorld = Harness.World->GetWorld();

	Harness.RuntimeComponent->SetLayoutWorldBindings({ NewObject<ULayoutWorldBindingAsset>(GetTransientPackage()) });
	ULayoutPlanningWindowStore* const Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	FLayoutPlanningWindowEndpointRecord Endpoint;
	Endpoint.StableEndpointKey = TEXT("CameraRoot/Entry");
	Endpoint.RootRecordKey = TEXT("CameraRoot");
	Endpoint.ContinuationFamilyId = TEXT("CameraFamily");
	Endpoint.RemainingConnections = 1;
	Endpoint.State = ELayoutContinuationEndpointState::Ready;
	Endpoint.Endpoint.EndpointBlockWorldPos = CameraBlock;
	FLayoutPlanningWindowEndpointRecord Stored;
	TestTrue(TEXT("Camera endpoint publishes"), Store->UpsertContinuationEndpointRecord(Endpoint, Stored));
	FResolvedLayoutSiteRecord ExplicitMarker;
	ExplicitMarker.RootSolveId = TEXT("CameraExplicitMarker");
	ExplicitMarker.SiteCenterBlockWorldPos = FIntVector(-100000, -100000, 0);
	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(-100, -100), ExplicitMarker);
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	TestEqual(TEXT("Camera maintenance preserves explicitly owned cached roots"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 1);
	TestEqual(TEXT("Camera proximity cannot retain an endpoint without loaded or retained root influence"), Store->GetContinuationEndpointRecords().Num(), 0);
	Harness.World->GetWorld()->RemoveController(Controller);
	Controller->UnPossess();
	Pawn->Destroy();
	Controller->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowPropertyEditTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.LayoutPropertyDoesNotRestartTerrain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowPropertyEditTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	FResolvedLayoutSiteRecord Marker;
	Marker.RootSolveId = TEXT("PropertyEditMarker");
	Marker.SiteCenterBlockWorldPos = FIntVector(111, 222, 333);
	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(1, 2), Marker);
	for (const TCHAR* PropertyName : { TEXT("bEnablePlanningWindowRuntimeUpdates"), TEXT("bFollowEditorCamera") })
	{
		FPropertyChangedEvent Event(FindFProperty<FProperty>(Harness.RuntimeComponent->GetClass(), PropertyName), EPropertyChangeType::ValueSet);
		static_cast<UObject*>(Harness.World)->PostEditChangeProperty(Event);
		TestTrue(TEXT("Layout edit preserves running generation"), Harness.World->IsRunning());
		TestEqual(TEXT("Layout edit does not clear cached records through StartGen"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 1);
	}
	Harness.World->StartGen();
	TestTrue(TEXT("Explicit restart still runs"), Harness.World->IsRunning());
	TestEqual(TEXT("Explicit restart retains existing reset contract"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutRuntimePlanningWindowPreparationTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.PreparationOutsideDeadline",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowPreparationTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	ULayoutWorldBindingAsset* Binding = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = nullptr;
	ULayoutPlanningWindowStore* Store = nullptr;
	if (!ConfigureOneCellPlanningWindowRealizationFixture(*this, TEXT("Preparation"), Harness, Binding, ContentSet, Store)) return false;
	Binding->SolveBudget.MaxSolveDurationSeconds = 0.25f;
	// Current site discovery requires a real sampled surface, not an all-solid fallback.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAANIC3uQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAADDZCq6AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 60;
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 64;
	Harness.World->SetDetailedDiagnostics(true);
	class FDelayedPreparationExecution : public FSynchronousLayoutSolveExecution
	{
	public:
		int32 PreparationPhasesLeft = 2;
		int32 PreparationPhasesRan = 0;
		bool bPreparationHadLedger = false;
		void Enqueue(FLayoutBackgroundSolveWork Work, FLayoutSolveCancellationToken Token,
			TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete) override
		{
			if (PreparationPhasesLeft > 0)
			{
				--PreparationPhasesLeft;
				Work = [this, Inner = MoveTemp(Work)](const FLayoutSolveCancellationToken& Cancellation, FString& Failure)
				{
					++PreparationPhasesRan;
					bPreparationHadLedger |= LayoutSolveExecution::CurrentThreadLedger() != nullptr;
					FPlatformProcess::Sleep(0.30f);
					return Inner(Cancellation, Failure);
				};
			}
			FSynchronousLayoutSolveExecution::Enqueue(MoveTemp(Work), Token, MoveTemp(OnComplete));
		}
	};
	auto Execution = MakeUnique<FDelayedPreparationExecution>();
	const auto* Observer = Execution.Get();
	Harness.RuntimeComponent->MaxConcurrentBackgroundLayoutSolves = 1;
	Harness.RuntimeComponent->bEnablePlanningWindowRuntimeUpdates = true;
	Harness.RuntimeComponent->PlanningCenterSnapshot.Emplace(TArray<FIntVector>{FIntVector::ZeroValue});
	Harness.RuntimeComponent->SetLayoutSolveExecutionForTesting(MoveTemp(Execution));
	Harness.RuntimeComponent->SetDisableAutoPumpForTesting(true);
	Harness.RuntimeComponent->SetLayoutWorldBindings({Binding});
	const double PreviousTerrainTiming = UChunkWorldLayoutRuntimeComponent::GetLastSelectedSiteTerrainMillisecondsForTesting();
	Harness.RuntimeComponent->PlanningAreaQueue->Reset();
	Harness.RuntimeComponent->PlanningAreaQueue->Configure(
		FMath::Min(4, FMath::Min(Binding->BaseCellDimensionsBlocks.X, Binding->BaseCellDimensionsBlocks.Y)),
		Harness.RuntimeComponent->MaxCachedPlanningChunks);
	UChunkWorldLayoutRuntimeComponent::FObservedChunkLoad Created;
	Created.ChunkBlockWorldPos = FIntVector(0, 0, 48);
	Created.DetailLevel = Harness.World->GetChunkLayerCount() - 1;
	Created.EventType = EChunkWorldChunkLifecycleEventType::Created;
	Harness.RuntimeComponent->HandleObservedLoadedChunk(Created);
	FIntPoint Area, Min, Max;
	TestTrue(TEXT("Area preparation is selected"), Harness.RuntimeComponent->PlanningAreaQueue->TakeNext({FIntVector::ZeroValue}, Area));
	Harness.RuntimeComponent->PlanningAreaQueue->GetBounds(Area, Min, Max);
	Harness.RuntimeComponent->SubmitPlanningAreaDiscovery(Area, FIntVector::ZeroValue, Min, Max);
	TestTrue(TEXT("Area preparation is admitted"), Harness.RuntimeComponent->PendingPlanningAreaDiscovery.IsSet());
	TestEqual(TEXT("Admission does not run location or terrain preparation"), Observer->PreparationPhasesRan, 0);
	TestEqual(TEXT("Admission does not sample dense selected-site terrain"),
		UChunkWorldLayoutRuntimeComponent::GetLastSelectedSiteTerrainMillisecondsForTesting(), PreviousTerrainTiming);
	// The admitted request owns its noise: source rows can disappear before its worker executes.
	const auto SourceRows = Harness.World->WorldGenDef->WorldBiomes;
	Harness.World->WorldGenDef->WorldBiomes.Reset();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	TestEqual(TEXT("Location and terrain preparation both execute through the dispatcher"), Observer->PreparationPhasesRan, 2);
	TestFalse(TEXT("Area handoff settles at concurrency one"), Harness.RuntimeComponent->PendingPlanningAreaDiscovery.IsSet());
	TestFalse(TEXT("Preparation has no binding solve ledger"), Observer->bPreparationHadLedger);
	TestEqual(TEXT("Preparation longer than solve timeout still permits the captured root solve"),
		Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted).Num(), 1);

	// Scheduling-only suffix: a successful empty discovery must permit the next area
	// on the next pass, even though priority housekeeping remains inside its interval.
	class FEmptyDiscoveryExecution : public FSynchronousLayoutSolveExecution
	{
	public:
		int32 Submitted = 0;
		void Enqueue(FLayoutBackgroundSolveWork Work, FLayoutSolveCancellationToken Token,
			TFunction<void(FLayoutBackgroundSolveCompletion)> OnComplete) override
		{
			++Submitted;
			FSynchronousLayoutSolveExecution::Enqueue(
				[](const FLayoutSolveCancellationToken&, FString&) { return true; }, Token, MoveTemp(OnComplete));
		}
	};
	Harness.RuntimeComponent->ResetResolvedLayoutRecords(true);
	Harness.World->WorldGenDef->WorldBiomes = SourceRows;
	auto EmptyExecution = MakeUnique<FEmptyDiscoveryExecution>();
	const auto* EmptyObserver = EmptyExecution.Get();
	Harness.RuntimeComponent->BackgroundSolveDispatcher.Reset();
	Harness.RuntimeComponent->SetLayoutSolveExecutionForTesting(MoveTemp(EmptyExecution));
	Harness.RuntimeComponent->SetDisableAutoPumpForTesting(true);
	Harness.RuntimeComponent->HandleObservedLoadedChunk(Created);
	Harness.RuntimeComponent->LastPlanningWindowUpdateTimeSeconds = FPlatformTime::Seconds();
	Harness.RuntimeComponent->UpdateLoadedChunkPlanning();
	TestTrue(TEXT("Discovery admission does not wait for priority housekeeping"), Harness.RuntimeComponent->PendingPlanningAreaDiscovery.IsSet());
	Harness.RuntimeComponent->UpdateLoadedChunkPlanning();
	TestEqual(TEXT("Unfinished discovery cannot queue duplicate captures"), EmptyObserver->Submitted, 1);
	Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	TestFalse(TEXT("Publication never recursively admits the next discovery"), Harness.RuntimeComponent->PendingPlanningAreaDiscovery.IsSet());
	Harness.RuntimeComponent->UpdateLoadedChunkPlanning();
	TestEqual(TEXT("Completed empty discovery wakes another area without a one-second wait"), EmptyObserver->Submitted, 2);
	AddInfo(FString::Printf(TEXT("Bounded discovery check: selection={%s} capture={%s} publication={%s}; empty worker suffix tests admission, not scene throughput."),
		*Harness.RuntimeComponent->DiscoverySelectionTiming.Describe(), *Harness.RuntimeComponent->DiscoveryCaptureTiming.Describe(),
		*Harness.RuntimeComponent->DiscoveryPublicationTiming.Describe()));
	Harness.RuntimeComponent->CancelPlanningAreaDiscovery();
	Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowEditorDebugStatsTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.EditorDebugStats",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowEditorDebugStatsTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	// The shared switch inherits native ShowDebugData's on default; this case explicitly exercises off.
	Harness.RuntimeComponent->SetDebugGenerationStats(false);
	class FTestRenderTarget : public FRenderTarget
	{
	public:
		FIntPoint GetSizeXY() const override { return FIntPoint(800, 600); }
	} Target;
	UWorld* World = Harness.World->GetWorld();
	FCanvas RenderCanvas(&Target, nullptr, World, World->GetFeatureLevel());
	RenderCanvas.SetAllowedModes(0); // Inspect queued canvas render batches without submitting a GPU frame.
	const auto HasRenderBatches = [&RenderCanvas]()
	{
		return RenderCanvas.SortedElements.ContainsByPredicate([](const auto& Element)
		{
			return !Element.RenderBatchArray.IsEmpty();
		});
	};
	FEngineShowFlags Flags(ESFIM_All0);
	Flags.SetOnScreenDebug(true);
	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(&Target, World->Scene, Flags));
	FSceneViewInitOptions Options;
	Options.ViewFamily = &Family;
	Options.SetViewRectangle(FIntRect(0, 0, 800, 600));
	Options.ViewRotationMatrix = FMatrix::Identity;
	Options.ProjectionMatrix = FMatrix::Identity;
	FSceneView View(Options);
	UCanvas* Canvas = NewObject<UCanvas>();
	// Native restart can collect garbage; retain the synthetic viewport canvas across the edit.
	TStrongObjectPtr<UCanvas> CanvasLifetime(Canvas);
	Canvas->Init(800, 600, &View, &RenderCanvas);
	Canvas->Update();
	Harness.RuntimeComponent->DrawEditorDebugGenerationStats(Canvas, nullptr);
	TestFalse(TEXT("Disabled diagnostics queue no render batches"), HasRenderBatches());
	TestTrue(TEXT("Editor component registers its draw delegate"), Harness.RuntimeComponent->EditorDebugDrawHandle.IsValid());
	Harness.RuntimeComponent->SetDebugGenerationStats(true);
	UDebugDrawService::Draw(Flags, Canvas, nullptr);
	TestTrue(TEXT("Viewport debug service reaches this component without the stats HUD"), Harness.RuntimeComponent->bReportedEditorStatsDraw);
	TestTrue(TEXT("Editor draw queues actual canvas render batches"), HasRenderBatches());
	Harness.World->SetDetailedDiagnostics(true);
	FProperty* DetailedProperty = FindFProperty<FProperty>(AChunkWorldExtended::StaticClass(), TEXT("bDetailedDiagnostics"));
	FPropertyChangedEvent DetailedChange(DetailedProperty);
	static_cast<UObject*>(Harness.World)->PostEditChangeProperty(DetailedChange);
	Harness.RuntimeComponent->LastDebugGenerationStatsLines.Reset();
	for (int32 Index = 0; Index < 20; ++Index)
	{
		const FString Context = FString::Printf(TEXT("CombinedDebug.Rollover.%02d"), Index);
		Harness.RuntimeComponent->RecordLayoutSolvePropagationStats(Context, FLayoutSolveResult(), FColor::Cyan);
		TestTrue(TEXT("Combined debug history retains the latest solve"),
			Harness.RuntimeComponent->LastDebugGenerationStatsLines.Last().Message.Contains(Context));
	}
	TestEqual(TEXT("Combined debug history stays bounded"), Harness.RuntimeComponent->LastDebugGenerationStatsLines.Num(), 12);
	Harness.RuntimeComponent->bReportedEditorStatsDraw = false;
	UDebugDrawService::Draw(Flags, Canvas, nullptr);
	TestTrue(TEXT("Both toggles preserve registered HUD drawing after property change and history rollover"),
		Harness.RuntimeComponent->bReportedEditorStatsDraw);
	FProperty* NativeProperty = FindFProperty<FProperty>(AChunkWorldExtended::StaticClass(), TEXT("ShowDebugChunkLines"));
	static_cast<UObject*>(Harness.World)->PreEditChange(NativeProperty);
	FPropertyChangedEvent NativeChange(NativeProperty, EPropertyChangeType::ValueSet);
	static_cast<UObject*>(Harness.World)->PostEditChangeProperty(NativeChange);
	Harness.RuntimeComponent->bReportedEditorStatsDraw = false;
	UDebugDrawService::Draw(Flags, Canvas, nullptr);
	TestTrue(TEXT("Native setting edit restores the live HUD callback"), Harness.RuntimeComponent->bReportedEditorStatsDraw);
	Harness.RuntimeComponent->bReportedEditorStatsDraw = false;
	Flags.SetOnScreenDebug(false);
	UDebugDrawService::Draw(Flags, Canvas, nullptr);
	TestFalse(TEXT("Viewport On Screen Debug switch suppresses drawing"), Harness.RuntimeComponent->bReportedEditorStatsDraw);
	Flags.SetOnScreenDebug(true);
	{
		TGuardValue<FSceneInterface*> MissingScene(Family.Scene, nullptr);
		UDebugDrawService::Draw(Flags, Canvas, nullptr);
		TestFalse(TEXT("Unassociated canvas cannot draw this world's stats"), Harness.RuntimeComponent->bReportedEditorStatsDraw);
	}
	Harness.RuntimeComponent->UnregisterComponent();
	TestFalse(TEXT("Reconstruction removes the draw delegate"), Harness.RuntimeComponent->EditorDebugDrawHandle.IsValid());
	UDebugDrawService::Draw(Flags, Canvas, nullptr);
	TestFalse(TEXT("Unregistered component cannot draw through the service"), Harness.RuntimeComponent->bReportedEditorStatsDraw);
	Harness.RuntimeComponent->RegisterComponent();
	TestTrue(TEXT("Reconstruction rebinds the draw delegate"), Harness.RuntimeComponent->EditorDebugDrawHandle.IsValid());
	{
		TGuardValue<TEnumAsByte<EWorldType::Type>> GameWorldType(World->WorldType, EWorldType::Game);
		Harness.RuntimeComponent->ReportCachedDebugGenerationStats();
		const uint64 MessageKey = static_cast<uint64>(Harness.RuntimeComponent->GetUniqueID()) << 4;
		TestTrue(TEXT("Existing screen-message system receives keyed cache status"), GEngine->OnScreenDebugMessageExists(MessageKey));
		Harness.RuntimeComponent->ReportCachedDebugGenerationStats();
		TestTrue(TEXT("Status refresh retains its stable key"), GEngine->OnScreenDebugMessageExists(MessageKey));
		TestTrue(TEXT("Async counters use their own stable key"), GEngine->OnScreenDebugMessageExists(MessageKey + 1));
		for (int32 Index = 0; Index < 14; ++Index) GEngine->RemoveOnScreenDebugMessage(MessageKey + Index);
	}
	return true;
}

bool FLayoutRuntimePlanningWindowRealizesImportedSiteUsingFreshChunkObservedBeforeImportTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("FreshBeforeImport"));
	TSet<FIntVector> RequiredChunkOrigins;
	if (!TryResolveOneCellPlanningWindowRequiredChunkOrigins(*this, BiomeRowName, RequiredChunkOrigins))
	{
		return false;
	}

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), BiomeRowName);
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		return false;
	}

	FChunkWorldObservedChunkLifecycleEvent CreatedEvent;
	CreatedEvent.DetailLevel = 0;
	CreatedEvent.EventType = EChunkWorldChunkLifecycleEventType::Created;
	CreatedEvent.bServerAuthority = true;
	CreatedEvent.bFinestDetail = true;
	CreatedEvent.bGeneratedForFirstTime = true;
	for (const FIntVector& RequiredChunkOrigin : RequiredChunkOrigins)
	{
		CreatedEvent.ChunkBlockWorldPos = RequiredChunkOrigin;
		Harness.RuntimeComponent->QueueObservedChunkLifecycle(CreatedEvent);
	}
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	TestEqual(TEXT("Fresh chunk events observed before bindings exist do not create resolved sites by themselves"), Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(), 0);

	ULayoutWorldBindingAsset* WorldBinding = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = nullptr;
	ULayoutPlanningWindowStore* Store = nullptr;
	if (!ConfigureOneCellPlanningWindowRealizationFixture(
			*this,
			BiomeRowName,
			Harness,
			WorldBinding,
			ContentSet,
			Store))
	{
		return false;
	}

	Harness.RuntimeComponent->SetLayoutWorldBindings({WorldBinding});
	Harness.TrackPlanningCharacter(FIntVector(8, 8, 0));
	Harness.RuntimeComponent->SetDisableAutoPumpForTesting(true);
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	const auto InitiallyAccepted = Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted);
	if (!TestEqual(TEXT("Loaded Created terrain is admitted without another chunk event"), InitiallyAccepted.Num(), 1)) return false;
	const FLayoutId ExpectedRoot = InitiallyAccepted[0].GetRootPublicationMetadata().RootSolveId;

	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	SiteRecords.RemoveAll([&](const FResolvedLayoutSiteRecord& Site) { return Site.RootSolveId != ExpectedRoot; });
	TestEqual(TEXT("Fresh-before-import later pass imports one resolved site into the realization cache"), SiteRecords.Num(), 1);
	if (SiteRecords.Num() == 1)
	{
		const FIntVector ImportedSharedCellSizeInBlocks = LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(
			SiteRecords[0].SolveResult,
			ContentSet);
		const TSet<FIntVector> ImportedRequiredChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
			SiteRecords[0],
			ImportedSharedCellSizeInBlocks,
			Harness.World->WorldGenDef->ChunkBlockSize);
		TestTrue(
			TEXT("Fresh-before-import later pass keeps the full preobserved fresh chunk set inside the imported site's required chunk set"),
			[&ImportedRequiredChunkOrigins, &RequiredChunkOrigins]()
			{
				for (const FIntVector& RequiredChunkOrigin : RequiredChunkOrigins)
				{
					if (!ImportedRequiredChunkOrigins.Contains(RequiredChunkOrigin))
					{
						return false;
					}
				}

				return true;
			}());
	}
	TestTrue(
		TEXT("Pending fresh-created chunk eligibility survives until the accepted site is imported and realizes on the later runtime pass"),
		SiteRecords.Num() == 1
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);
	TestEqual(
		TEXT("Fresh-before-import later pass promotes the planning-window record to realized after the delayed fresh-created gate is satisfied"),
		Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Realized).Num(),
		1);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}
	return true;
}

bool FLayoutRuntimePlanningWindowRejectsFiniteCenteredZCeilingOverflowBeforeAcceptanceTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationFiniteCenteredZCeilingOverflow"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 256), BiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	TestNotNull(TEXT("Chunk-world harness creates a WorldGenDef"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	Harness.World->WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
	Harness.World->WorldGenDef->NoiseCoordinateOffset = FIntVector(0, 0, -128);
	FWorldGenScaleContextResolver::ClearCache();

	LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
	if (!TestTrue(
			TEXT("Finite centered-Z planning-window ceiling-overflow fixture resolves finite chunk-world bounds from the shared worldgen scale context"),
			LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
				Harness.World,
				Bounds)))
	{
		return false;
	}

	TestTrue(TEXT("Finite centered-Z planning-window ceiling-overflow fixture exposes a finite Z span"), Bounds.bHasFiniteZ);
	TestEqual(TEXT("Finite centered-Z planning-window ceiling-overflow fixture keeps the raw minimum Z block"), Bounds.MinInclusive.Z, 0);
	TestEqual(TEXT("Finite centered-Z planning-window ceiling-overflow fixture keeps the raw maximum Z block"), Bounds.MaxInclusive.Z, 255);

	const TArray<FIntVector> LowerStepColumns = {
		FIntVector(8, 8, 0),
		FIntVector(8, 24, 0),
		FIntVector(8, 40, 0)
	};
	const TArray<FIntVector> MiddleStepColumns = {
		FIntVector(24, 8, 0),
		FIntVector(24, 24, 0),
		FIntVector(24, 40, 0)
	};
	const TArray<FIntVector> UpperStepColumns = {
		FIntVector(40, 8, 0),
		FIntVector(40, 24, 0),
		FIntVector(40, 40, 0)
	};
	auto WriteSteppedSurfaceColumns =
		[&Harness](const TArray<FIntVector>& ColumnBases, const int32 SurfaceZ)
	{
		for (const FIntVector& ColumnBase : ColumnBases)
		{
			for (int32 Z = 208; Z <= 255; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(
					FIntVector(ColumnBase.X, ColumnBase.Y, Z),
					EmptyMaterial,
					false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(
				Harness.World,
				FIntVector(ColumnBase.X, ColumnBase.Y, SurfaceZ));
		}
	};
	WriteSteppedSurfaceColumns(LowerStepColumns, 243);
	WriteSteppedSurfaceColumns(MiddleStepColumns, 245);
	WriteSteppedSurfaceColumns(UpperStepColumns, 247);

	ULayoutRegionContentSetAsset* ContentSet = CreateSteppedThreeColumnPlanningContentSet(GetTransientPackage());
	ULayoutProfileAsset* Profile = CreateSteppedThreeColumnPlanningProfile(GetTransientPackage());
	Profile->ContentSet = ContentSet;
	ULayoutWorldBindingAsset* WorldBinding = CreateWorldBinding(
		GetTransientPackage(),
		TEXT("ReservationFiniteCenteredZCeilingOverflowBinding"),
		TEXT("PrimaryCandidate"),
		BiomeRowName,
		Profile,
		true,
		2.0f,
		FIntVector(16, 16, 16));
	WorldBinding->Candidates[0].ExportedConnectorTypeTags = MakeTags({LayoutGameplayTags::ConnectorRoad});
	WorldBinding->DefaultPlacementPolicy.TerrainSampleGridSpacing = 16;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 240;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 32;
	Harness.RuntimeComponent->SetLayoutWorldBindings({WorldBinding});

	ULayoutPlanningWindowStore* Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	if (!TestNotNull(TEXT("Finite centered-Z planning-window ceiling-overflow fixture exposes a store"), Store))
	{
		return false;
	}

	FLayoutPlanningWindowSettings Settings;
	Settings.SampleSpacing = 16;
	Settings.SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;
	if (!TestTrue(TEXT("Finite centered-Z planning-window ceiling-overflow settings are valid"), Store->SetPlanningWindowSettings(Settings).IsValid()))
	{
		return false;
	}

	TestEqual(
		TEXT("Pure zero-anchored lattice snap would step the finite centered-Z planning-window surface above the raw single-chunk ceiling"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(
			WorldBinding,
			248),
		256);
	int32 RejectedFiniteSiteCenterZ = 0;
	TestFalse(
		TEXT("Finite-axis-aware lattice constraint rejects the finite centered-Z planning-window surface because no valid in-bounds plane remains above the sampled terrain"),
		LayoutWorldBindingSitePlanner::TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
			WorldBinding,
			248,
			Bounds,
			RejectedFiniteSiteCenterZ));

	const FIntVector Center(24, 24, 240);
	Harness.TrackPlanningCharacter(Center);
	Harness.World->OnChunkCreate(FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(Center, Harness.World->WorldGenDef->ChunkBlockSize),
		Harness.World->GetChunkLayerCount() - 1, {});
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	TestEqual(
		TEXT("Planning-window ceiling-overflow path keeps no accepted records"),
		Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted).Num(),
		0);
	TestEqual(
		TEXT("Planning-window ceiling-overflow path keeps no resolved sites"),
		Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(),
		0);

	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	if (!TestTrue(
			TEXT("Finite centered-Z planning-window ceiling-overflow parity helper initializes the active biome sampler"),
			ActiveBiomeSampler.Initialize(
				Harness.RuntimeComponent,
				Harness.World->WorldGenDef,
				Harness.World->Seed)))
	{
		return false;
	}

	TArray<FLayoutReservationPocketSample> Samples;
	TMap<FIntPoint, int32> SurfaceZBySampleGrid;
	if (!TestTrue(
			TEXT("Finite centered-Z planning-window ceiling-overflow fixture resamples the active biome pocket grid"),
			ActiveBiomeSampler.SampleEligibleBiomeSurfaceGrid(
				BiomeRowName,
				FIntPoint(0, 0),
				FIntPoint(48, 48),
				16,
				WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ,
				WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks,
				MakeCoordinateSettings(Harness.World->WorldGenDef),
				Samples,
				&SurfaceZBySampleGrid)))
	{
		return false;
	}

	const TArray<FLayoutReservationPocket> Pockets =
		ULayoutReservationPocketPlanningLibrary::FindConnectedReservationPockets(
			Samples,
			FLayoutReservationPocketPlanningSettings());
	if (!TestEqual(TEXT("Finite centered-Z planning-window ceiling-overflow fixture discovers one eligible pocket"), Pockets.Num(), 1))
	{
		return false;
	}

	const FIntPoint ExpectedSiteCenterBlockXY =
		Pockets[0].CentroidNearestSampleBlockXY;
	FString ExpectedSiteCenterFailureReason;
	FIntVector ExpectedSiteCenterBlockWorldPos = FIntVector::ZeroValue;
	TestFalse(
		TEXT("Finite centered-Z planning-window ceiling-overflow parity also rejects the nearest eligible pocket sample when the zero-anchored lattice would step past the finite ceiling"),
		TryComputeExpectedPlanningWindowOrdinaryRootSiteCenter(
			Harness.RuntimeComponent,
			Harness.World,
			WorldBinding,
			BiomeRowName,
			ExpectedSiteCenterBlockXY,
			ExpectedSiteCenterBlockWorldPos,
			ExpectedSiteCenterFailureReason));
	TestTrue(
		TEXT("Finite centered-Z planning-window ceiling-overflow parity reports the finite-Z rejection reason instead of fabricating one fallback site center"),
		ExpectedSiteCenterFailureReason.Contains(TEXT("finite Z floor/ceiling")));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowRealizesDirectChildRegionsTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.RealizesDirectChildRegions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowRealizesCompositePlacementsTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.RealizesCompositePlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowRealizesDirectChildRegionsTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationChild"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	TestNotNull(TEXT("Chunk-world harness creates a WorldGenDef"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	ULayoutProfileAsset* ChildProfile = nullptr;
	ULayoutModuleAsset* ParentModule = nullptr;
	ULayoutModuleAsset* ChildModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateParentChildPlanningStampContentSet(
		GetTransientPackage(),
		Harness.World,
		ChildProfile,
		ParentModule,
		ChildModule,
		SinfullMaterial,
		SinfullMaterial + 5);
	ULayoutProfileAsset* Profile = CreateParentChildPlanningProfile(GetTransientPackage());
	Profile->ContentSet = ContentSet;
	Harness.RuntimeComponent->SetLayoutWorldBindings({CreateWorldBinding(
		GetTransientPackage(),
		TEXT("ReservationChildBinding"),
		TEXT("PrimaryCandidate"),
		BiomeRowName,
		Profile)});

	ULayoutPlanningWindowStore* Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	TestNotNull(TEXT("Planning-window runtime exposes a store"), Store);
	FLayoutPlanningWindowSettings Settings;
	Settings.SampleSpacing = 4;
	Settings.SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;
	TestTrue(TEXT("Planning-window settings are valid"), Store != nullptr && Store->SetPlanningWindowSettings(Settings).IsValid());

	Harness.TrackPlanningCharacter(FIntVector::ZeroValue);
	Harness.World->OnChunkCreate(FIntVector::ZeroValue, Harness.World->GetChunkLayerCount() - 1, {});
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	const TArray<FPlannedLayoutSiteRecord> AcceptedRecords = Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted);
	TestEqual(TEXT("Accepted parent-child planned record is stored"), AcceptedRecords.Num(), 1);
	if (AcceptedRecords.Num() != 1)
	{
		return false;
	}

	const FLayoutPlannedSiteAcceptedSolvePayload AcceptedSolvePayload =
		AcceptedRecords[0].GetPlannedSiteAcceptedSolvePayload();
	TestTrue(TEXT("Accepted record caches a solved layout"), AcceptedSolvePayload.SolveResult.bSucceeded);
	TestTrue(TEXT("Accepted record includes the child placement in the merged solve result"), AcceptedSolvePayload.SolveResult.Placements.Num() > 1);

	const int32 ImportedCount = Harness.RuntimeComponent->ImportAcceptedPlannedLayoutSiteRecordsForRealization();
	TestEqual(TEXT("Accepted parent-child site imports into the realization cache"), ImportedCount, 1);

	const TArray<FResolvedLayoutSiteRecord> ImportedSiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestEqual(TEXT("Imported parent-child site creates one resolved record before realization"), ImportedSiteRecords.Num(), 1);
	if (ImportedSiteRecords.Num() != 1)
	{
		return false;
	}

	const FIntVector SharedCellSizeInBlocks = ContentSet->GetSharedCellSizeInBlocks();
	const TSet<FIntVector> RequiredChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
		ImportedSiteRecords[0],
		SharedCellSizeInBlocks,
		Harness.World->WorldGenDef->ChunkBlockSize);
	TestTrue(TEXT("Imported parent-child site reports at least one required chunk origin"), !RequiredChunkOrigins.IsEmpty());

	for (const FIntVector& RequiredChunkOrigin : RequiredChunkOrigins)
	{
		Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(RequiredChunkOrigin);

		FChunkWorldObservedChunkLifecycleEvent LoadedChunkEvent;
		LoadedChunkEvent.ChunkBlockWorldPos = RequiredChunkOrigin;
		LoadedChunkEvent.DetailLevel = 0;
		LoadedChunkEvent.EventType = EChunkWorldChunkLifecycleEventType::Created;
		LoadedChunkEvent.bServerAuthority = true;
		LoadedChunkEvent.bFinestDetail = true;
		LoadedChunkEvent.bGeneratedForFirstTime = true;
		Harness.RuntimeComponent->QueueObservedChunkLifecycle(LoadedChunkEvent);
	}
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	const FLayoutId RootId = AcceptedRecords[0].GetRootPublicationMetadata().RootSolveId;
	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().FilterByPredicate(
		[RootId](const FResolvedLayoutSiteRecord& Record) { return Record.RootSolveId == RootId; });
	TestEqual(TEXT("Accepted parent-child root has exactly one resolved record"), SiteRecords.Num(), 1);
	TestTrue(
		TEXT("Imported parent-child site is realized after the touched chunk reports its first-create lifecycle event"),
		SiteRecords.Num() == 1 && SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	FIntVector TerrainConformingSiteOffset = FIntVector::ZeroValue;
	FString TerrainOffsetFailureReason;
	if (!TestTrue(
			TEXT("Imported parent-child site can rebuild the same runtime terrain-fit offset used by the live realization path"),
			TryComputeRuntimeTerrainConformingSiteOffset(
				Harness.RuntimeComponent,
				Harness.World,
				Harness.World->Seed,
				SiteRecords[0],
				TerrainConformingSiteOffset,
				TerrainOffsetFailureReason)))
	{
		AddError(TerrainOffsetFailureReason);
		return false;
	}

	bool bVerifiedParentStamp = false;
	bool bVerifiedChildStamp = false;
	for (const FLayoutPlacedModule& Placement : SiteRecords[0].SolveResult.Placements)
	{
		const FIntVector PlacementAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
			SiteRecords[0],
			Placement,
			SharedCellSizeInBlocks)
			+ TerrainConformingSiteOffset;
		const int32 MaterialAtAnchor =
			Harness.World->GetBlockValueByBlockWorldPos(PlacementAnchor, ERessourceType::MaterialIndex, 0);
		if (Placement.Module == ParentModule)
		{
			bVerifiedParentStamp = bVerifiedParentStamp || MaterialAtAnchor == SinfullMaterial;
		}
		else if (Placement.Module == ChildModule)
		{
			bVerifiedChildStamp = bVerifiedChildStamp || MaterialAtAnchor == SinfullMaterial + 5;
		}
	}

	TestTrue(TEXT("Imported planning parent-child site stamps the parent shell material"), bVerifiedParentStamp);
	TestTrue(TEXT("Imported planning parent-child site stamps the child room material"), bVerifiedChildStamp);
	return true;
}

bool FLayoutRuntimePlanningWindowRealizesCompositePlacementsTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("ReservationComposite"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	TestNotNull(TEXT("Chunk-world harness creates a WorldGenDef"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	ULayoutRegionContentSetAsset* ContentSet = CreateCompositePlanningStampContentSet(GetTransientPackage(), Harness.World);
	ULayoutProfileAsset* Profile = CreateCompositePlanningProfile(GetTransientPackage());
	Profile->ContentSet = ContentSet;
	Harness.RuntimeComponent->SetLayoutWorldBindings({CreateWorldBinding(
		GetTransientPackage(),
		TEXT("ReservationCompositeBinding"),
		TEXT("PrimaryCandidate"),
		BiomeRowName,
		Profile)});

	ULayoutPlanningWindowStore* Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	TestNotNull(TEXT("Planning-window runtime exposes a store"), Store);
	FLayoutPlanningWindowSettings Settings;
	Settings.SampleSpacing = 4;
	Settings.SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;
	TestTrue(TEXT("Planning-window settings are valid"), Store != nullptr && Store->SetPlanningWindowSettings(Settings).IsValid());

	Harness.TrackPlanningCharacter(FIntVector::ZeroValue);
	Harness.World->OnChunkCreate(FIntVector::ZeroValue, Harness.World->GetChunkLayerCount() - 1, {});
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	const TArray<FPlannedLayoutSiteRecord> AcceptedRecords = Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted);
	TestEqual(TEXT("Accepted composite planned record is stored"), AcceptedRecords.Num(), 1);
	if (AcceptedRecords.Num() != 1)
	{
		return false;
	}

	const FLayoutPlannedSiteAcceptedSolvePayload AcceptedSolvePayload =
		AcceptedRecords[0].GetPlannedSiteAcceptedSolvePayload();
	TestTrue(TEXT("Accepted composite record caches a solved layout"), AcceptedSolvePayload.SolveResult.bSucceeded);
	TestTrue(
		TEXT("Accepted composite record preserves one occupied placement bundle"),
		AcceptedSolvePayload.SolveResult.Placements.Num() == 1
			&& AcceptedSolvePayload.SolveResult.Placements[0].OccupiedLocalCells.Num() == 2);

	const int32 ImportedCount = Harness.RuntimeComponent->ImportAcceptedPlannedLayoutSiteRecordsForRealization();
	TestEqual(TEXT("Accepted composite site imports into the realization cache"), ImportedCount, 1);

	const TArray<FResolvedLayoutSiteRecord> ImportedSiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestEqual(TEXT("Imported composite site creates one resolved record before realization"), ImportedSiteRecords.Num(), 1);
	if (ImportedSiteRecords.Num() != 1)
	{
		return false;
	}

	const FIntVector SharedCellSizeInBlocks = ContentSet->GetSharedCellSizeInBlocks();
	const TSet<FIntVector> RequiredChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
		ImportedSiteRecords[0],
		SharedCellSizeInBlocks,
		Harness.World->WorldGenDef->ChunkBlockSize);
	TestTrue(TEXT("Imported composite site reports at least one required chunk origin"), !RequiredChunkOrigins.IsEmpty());

	for (const FIntVector& RequiredChunkOrigin : RequiredChunkOrigins)
	{
		Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(RequiredChunkOrigin);

		FChunkWorldObservedChunkLifecycleEvent LoadedChunkEvent;
		LoadedChunkEvent.ChunkBlockWorldPos = RequiredChunkOrigin;
		LoadedChunkEvent.DetailLevel = 0;
		LoadedChunkEvent.EventType = EChunkWorldChunkLifecycleEventType::Created;
		LoadedChunkEvent.bServerAuthority = true;
		LoadedChunkEvent.bFinestDetail = true;
		LoadedChunkEvent.bGeneratedForFirstTime = true;
		Harness.RuntimeComponent->QueueObservedChunkLifecycle(LoadedChunkEvent);
	}
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	const FLayoutId RootId = AcceptedRecords[0].GetRootPublicationMetadata().RootSolveId;
	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().FilterByPredicate(
		[RootId](const FResolvedLayoutSiteRecord& Record) { return Record.RootSolveId == RootId; });
	TestEqual(TEXT("Accepted composite root has exactly one resolved record"), SiteRecords.Num(), 1);
	TestTrue(
		TEXT("Imported composite site is realized after the touched chunk reports its first-create lifecycle event"),
		SiteRecords.Num() == 1 && SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	const FLayoutPlacedModule& Placement = SiteRecords[0].SolveResult.Placements[0];
	FIntVector TerrainConformingSiteOffset = FIntVector::ZeroValue;
	FString TerrainOffsetFailureReason;
	if (!TestTrue(
			TEXT("Imported composite site can rebuild the same runtime terrain-fit offset used by the live realization path"),
			TryComputeRuntimeTerrainConformingSiteOffset(
				Harness.RuntimeComponent,
				Harness.World,
				Harness.World->Seed,
				SiteRecords[0],
				TerrainConformingSiteOffset,
				TerrainOffsetFailureReason)))
	{
		AddError(TerrainOffsetFailureReason);
		return false;
	}
	const FIntVector BundleAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
		SiteRecords[0],
		Placement,
		SharedCellSizeInBlocks)
		+ TerrainConformingSiteOffset;
	const FIntPoint RotationFootprint(2, 1);
	const FIntVector FirstLeafCell =
		BundleAnchor + RotatePlanningCompositeCellInFootprintYaw(FIntVector(0, 0, 0), RotationFootprint, Placement.YawRotationSteps);
	const FIntVector SecondLeafCell =
		BundleAnchor + RotatePlanningCompositeCellInFootprintYaw(FIntVector(1, 0, 0), RotationFootprint, Placement.YawRotationSteps);
	TestEqual(TEXT("Imported planning composite site stamps the root leaf material"), Harness.World->GetBlockValueByBlockWorldPos(FirstLeafCell, ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Imported planning composite site stamps the shadow leaf material"), Harness.World->GetBlockValueByBlockWorldPos(SecondLeafCell, ERessourceType::MaterialIndex, 0), SinfullMaterial + 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeChunkLifecycleRejectsFiniteCenteredZCeilingOverflowThreeColumnHostBiomeSiteTest,
	"PorismExtension.Layout.Runtime.ChunkLifecycle.RejectsFiniteCenteredZCeilingOverflowThreeColumnHostBiomeSite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeChunkLifecycleRejectsFiniteCenteredZCeilingOverflowThreeColumnHostBiomeSiteTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("LifecycleSteppedHostBiomeFiniteCenteredZCeilingClamp"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 256), BiomeRowName);
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	const int32 FixtureBiomeSwitchIndex = EmptyBiome;
	Harness.World->WorldGenDef->WorldBiomes.SetNum(FixtureBiomeSwitchIndex + 1);
	Harness.World->WorldGenDef->WorldBiomes[FixtureBiomeSwitchIndex].BiomeName = BiomeRowName.ToString();
	Harness.World->WorldGenDef->AxisBehaviorZ = EAxisBehavior::SingleChunk;
	Harness.World->WorldGenDef->NoiseCoordinateOffset = FIntVector(0, 0, -128);
	FWorldGenScaleContextResolver::ClearCache();

	LayoutWorldBindingSitePlanner::FChunkWorldFiniteAxisBlockBounds Bounds;
	if (!TestTrue(
			TEXT("Finite centered-Z ceiling-clamp stepped host-biome fixture resolves finite chunk-world bounds from the shared worldgen scale context"),
			LayoutWorldBindingSitePlanner::TryResolveChunkWorldFiniteAxisBlockBounds(
				Harness.World,
				Bounds)))
	{
		return false;
	}

	TestTrue(TEXT("Finite centered-Z ceiling-clamp stepped host-biome fixture exposes a finite Z span"), Bounds.bHasFiniteZ);
	TestEqual(TEXT("Finite centered-Z ceiling-clamp stepped host-biome fixture keeps the raw minimum Z block"), Bounds.MinInclusive.Z, 0);
	TestEqual(TEXT("Finite centered-Z ceiling-clamp stepped host-biome fixture keeps the raw maximum Z block"), Bounds.MaxInclusive.Z, 255);

	const TArray<FIntVector> LowerStepColumns = {
		FIntVector(8, 8, 0),
		FIntVector(8, 24, 0),
		FIntVector(8, 40, 0)
	};
	const TArray<FIntVector> MiddleStepColumns = {
		FIntVector(24, 8, 0),
		FIntVector(24, 24, 0),
		FIntVector(24, 40, 0)
	};
	const TArray<FIntVector> UpperStepColumns = {
		FIntVector(40, 8, 0),
		FIntVector(40, 24, 0),
		FIntVector(40, 40, 0)
	};
	auto WriteSteppedSurfaceColumns =
		[&Harness](const TArray<FIntVector>& ColumnBases, const int32 SurfaceZ)
	{
		for (const FIntVector& ColumnBase : ColumnBases)
		{
			for (int32 Z = 208; Z <= 255; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(
					FIntVector(ColumnBase.X, ColumnBase.Y, Z),
					EmptyMaterial,
					false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(
				Harness.World,
				FIntVector(ColumnBase.X, ColumnBase.Y, SurfaceZ));
		}
	};
	WriteSteppedSurfaceColumns(LowerStepColumns, 243);
	WriteSteppedSurfaceColumns(MiddleStepColumns, 245);
	WriteSteppedSurfaceColumns(UpperStepColumns, 247);

	UObject* Outer = GetTransientPackage();
	ULayoutModuleAsset* EntryModule = CreateSingleCellPlanningEntryModule(
		Outer,
		TEXT("LayoutPlanningFiniteCenteredZCeilingClampHostEntry"));
	ULayoutModuleAsset* JunctionVerticalAccessModule = CreateSingleCellPlanningJunctionVerticalAccessModule(
		Outer,
		TEXT("LayoutPlanningFiniteCenteredZCeilingClampHostJunctionVerticalAccess"));
	ULayoutModuleAsset* StructuralModule = CreateSingleCellPlanningStructuralModule(
		Outer,
		TEXT("LayoutPlanningFiniteCenteredZCeilingClampHostStructural"));

	FLayoutRegionContentEntry EntryModuleEntry;
	EntryModuleEntry.EntryId = TEXT("LayoutPlanningFiniteCenteredZCeilingClampHostEntryModuleEntry");
	EntryModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
	EntryModuleEntry.ModuleSettings.Module = EntryModule;
	EntryModuleEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

	FLayoutRegionContentEntry JunctionVerticalAccessEntry;
	JunctionVerticalAccessEntry.EntryId = TEXT("LayoutPlanningFiniteCenteredZCeilingClampHostJunctionVerticalAccessEntry");
	JunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
	JunctionVerticalAccessEntry.ModuleSettings.Module = JunctionVerticalAccessModule;
	JunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

	FLayoutRegionContentEntry StructuralEntry;
	StructuralEntry.EntryId = TEXT("LayoutPlanningFiniteCenteredZCeilingClampHostStructuralEntry");
	StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
	StructuralEntry.ModuleSettings.Module = StructuralModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_FiniteCenteredZCeilingClampThreeColumnHost"),
		{StructuralEntry, EntryModuleEntry, JunctionVerticalAccessEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_FiniteCenteredZCeilingClampThreeColumnHost"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		2,
		0,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bRequireAllTraversalChannelsReachable = false;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 1;
	Profile->MinVerticalAccessCount = 1;
	Profile->MaxVerticalAccessCount = 1;
	ULayoutWorldBindingAsset* Binding = CreateWorldBinding(
		GetTransientPackage(),
		TEXT("LifecycleFiniteCenteredZCeilingClampThreeColumnHostBinding"),
		TEXT("PrimaryCandidate"),
		BiomeRowName,
		Profile,
		true,
		2.0f,
		FIntVector(16, 16, 16));
	Binding->DefaultPlacementPolicy.TerrainSampleGridSpacing = 16;
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 240;
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 32;
	Harness.RuntimeComponent->SetLayoutWorldBindings({Binding});

	TestEqual(
		TEXT("Pure zero-anchored lattice snap would step the finite ceiling-clamp host-biome surface above the raw single-chunk ceiling"),
		LayoutWorldBindingSitePlanner::ResolveOrdinaryRootSiteCenterZ(
			Binding,
			248),
		256);
	int32 RejectedFiniteSiteCenterZ = 0;
	TestFalse(
		TEXT("Finite-axis-aware lattice constraint rejects the finite ceiling-clamp host-biome surface because no valid in-bounds plane remains above the sampled terrain"),
		LayoutWorldBindingSitePlanner::TryResolveOrdinaryRootSiteCenterZWithinFiniteAxisBounds(
			Binding,
			248,
			Bounds,
			RejectedFiniteSiteCenterZ));

	FChunkWorldObservedChunkLifecycleEvent CreatedEvent;
	CreatedEvent.ChunkBlockWorldPos = FIntVector::ZeroValue;
	CreatedEvent.DetailLevel = 0;
	CreatedEvent.EventType = EChunkWorldChunkLifecycleEventType::Created;
	CreatedEvent.bServerAuthority = true;
	CreatedEvent.bFinestDetail = true;
	CreatedEvent.bGeneratedForFirstTime = true;
	Harness.RuntimeComponent->QueueObservedChunkLifecycle(CreatedEvent);
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	const TArray<FResolvedLayoutSiteRecord> ResolvedSiteRecords =
		Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	if (!TestEqual(
			TEXT("Finite centered-Z ceiling-overflow stepped host-biome chunk lifecycle resolves no site when the next lattice plane would step past the finite ceiling"),
			ResolvedSiteRecords.Num(),
			0))
	{
		return false;
	}

	FIntVector RejectedSiteCenterBlockWorldPos = FIntVector::ZeroValue;
	FString ExpectedSiteCenterFailureReason;
	TestFalse(
		TEXT("Finite centered-Z ceiling-overflow stepped host-biome parity also rejects the active-biome-derived site center when the zero-anchored lattice would step past the finite ceiling"),
		TryComputeExpectedObservedChunkOrdinaryRootSiteCenter(
			Harness.RuntimeComponent,
			Harness.World,
			Binding,
			BiomeRowName,
			FIntVector(24, 24, 0),
			RejectedSiteCenterBlockWorldPos,
			ExpectedSiteCenterFailureReason));
	TestFalse(
		TEXT("Finite centered-Z ceiling-overflow stepped host-biome parity does not fabricate a fallback in-bounds site center after rejecting the overflowing lattice plane"),
		ExpectedSiteCenterFailureReason.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowAcceptsUndergroundPocketSiteTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.AcceptsUndergroundPocketSite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowAcceptsUndergroundPocketSiteTest::RunTest(const FString& Parameters)
{
	// Verify that a profile with bUndergroundPlacement=true routes through UndergroundPocketPlacement
	// when the site center is below the biome surface. The void interval sampler populates
	// PocketVoidIntervals on the terrain evidence, and the prewarm adapter uses them.
	const FName BiomeRowName(TEXT("UndergroundPocket"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr || Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	// Air at Z=4..6, solid floor/ceiling, surface at Z=49.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAANIC3uQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAADDZCq6AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));

	// Planning Window selects Z=4 from procedural cavity evidence; loading is not required.
	const FIntVector SiteCenter = FIntVector(8, 8, 4);
	// Cover the one-cell selected-site halo around every candidate in the active window.
	for (int32 Y = -24; Y <= 32; ++Y)
	{
		for (int32 X = -24; X <= 32; ++X)
		{
			const FIntVector Floor(X, Y, 3);
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, Floor);
			for (int32 Z = 4; Z <= 6; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(FIntVector(X, Y, Z), EmptyMaterial, false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(X, Y, 7));
		}
	}
	const TArray<int> CavityProbeMaterials = Harness.World->GetBlockValuesByBlockWorldPos(
		{SiteCenter},
		ERessourceType::MaterialIndex);
	if (!TestEqual(TEXT("Underground lifecycle fixture exposes site material through the authoritative batch read"), CavityProbeMaterials.Num(), 1))
	{
		return false;
	}
	TestEqual(TEXT("Underground lifecycle fixture exposes selected cavity air"), CavityProbeMaterials[0], EmptyMaterial);
	ULayoutRegionContentSetAsset* ContentSet = CreateOneCellPlanningStampContentSet(GetTransientPackage(), Harness.World);
	ULayoutProfileAsset* Profile = CreateOneCellPlanningProfile(GetTransientPackage());
	Profile->bUndergroundPlacement = true;
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* Binding = CreateWorldBinding(
		GetTransientPackage(),
		TEXT("UndergroundBinding"),
		TEXT("PrimaryCandidate"),
		BiomeRowName,
		Profile);
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 8;
	Harness.RuntimeComponent->SetLayoutWorldBindings({Binding});

	ULayoutPlanningWindowStore* Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	if (Store != nullptr)
	{
		FLayoutPlanningWindowSettings Settings;
		Settings.SampleSpacing = 8;
		Settings.SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;
		Store->SetPlanningWindowSettings(Settings);
	}

	Harness.TrackPlanningCharacter(SiteCenter);
	Harness.World->OnChunkCreate(FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(SiteCenter, Harness.World->WorldGenDef->ChunkBlockSize),
		Harness.World->GetChunkLayerCount() - 1, {});
	// Hold the game-thread realization pass until the accepted cavity contract is inspected.
	Harness.RuntimeComponent->SetDisableAutoPumpForTesting(true);
	Harness.RuntimeComponent->ProcessQueuedLayoutWorkNow();
	for (int32 Index = 0; Index < 200; ++Index) Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	const int32 PlannedCount = Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted).Num();
	TestEqual(TEXT("Underground site plans one candidate from the active biome"), PlannedCount, 1);
	if (PlannedCount != 1)
	{
		return false;
	}
	// The test owns this publication snapshot; runtime releases heavy contracts after placement.
	const TArray<FPlannedLayoutSiteRecord> AcceptedRecords = Store->GetPlannedLayoutSiteRecordsByState(EPlannedLayoutSiteState::Accepted);

	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();
	TArray<FResolvedLayoutSiteRecord> ResolvedSiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	const FLayoutId ExpectedRoot = AcceptedRecords[0].GetRootPublicationMetadata().RootSolveId;
	ResolvedSiteRecords.RemoveAll([&](const FResolvedLayoutSiteRecord& Site) { return Site.RootSolveId != ExpectedRoot; });
	if (ResolvedSiteRecords.IsEmpty())
	{
		for (const FPlannedLayoutSiteRecord& StoredRecord : Store->GetPlannedLayoutSiteRecords())
		{
			const FLayoutPlannedSiteLifecycleMetadata Lifecycle = StoredRecord.GetPlannedSiteLifecycleMetadata();
			AddInfo(FString::Printf(
				TEXT("Underground Planning Window record at %s state=%d frozenState=%d: %s"),
				*StoredRecord.GetPlannedSiteReservationSourceSelection().SiteCenterBlockWorldPos.ToString(),
				static_cast<int32>(Lifecycle.State),
				static_cast<int32>(Lifecycle.FrozenSubmissionState),
				*Lifecycle.RejectionReason));
		}
	}
	TestEqual(TEXT("Underground-placed site is accepted and resolved through the lifecycle chain"),
		ResolvedSiteRecords.Num(), 1);
	if (ResolvedSiteRecords.Num() != 1)
	{
		return false;
	}
	const FLayoutSolveResult& UndergroundSolveResult = ResolvedSiteRecords[0].SolveResult;
	TestTrue(TEXT("Constrained Underground root publishes a solved artifact"), !ResolvedSiteRecords[0].SolvedArtifactId.IsNone());
	TestTrue(TEXT("Constrained Underground root solve succeeds"), UndergroundSolveResult.bSucceeded);
	TestEqual(TEXT("Constrained Underground root keeps ordinary-root placement kind"), UndergroundSolveResult.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestTrue(
		TEXT("Constrained Underground root emits no stepped support map"),
		UndergroundSolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty()
			&& UndergroundSolveResult.SteppedTerrainSupportMap.AdjacencySteps.IsEmpty());
	TestTrue(TEXT("Constrained Underground root emits no forced stepped bundles"), UndergroundSolveResult.ForcedPlacementBundleInsertions.IsEmpty());
	TestEqual(TEXT("Constrained Underground root published one accepted planning contract"), AcceptedRecords.Num(), 1);
	if (AcceptedRecords.Num() == 1)
	{
		const FLayoutPlannedSiteAcceptedSolvePayload AcceptedPayload = AcceptedRecords[0].GetPlannedSiteAcceptedSolvePayload();
		TestEqual(TEXT("Accepted contract preserves solved artifact id"), AcceptedPayload.SolvedArtifactId, ResolvedSiteRecords[0].SolvedArtifactId);
		TestTrue(TEXT("Accepted Underground contract emits no stepped stage map"), AcceptedPayload.FrozenTerrainContract.StageMap.IsEmpty());
		const FIntVector ContractMin = AcceptedPayload.FrozenTerrainContract.FootprintMinBlockWorldPos;
		const FIntVector ContractMaxExclusive = ContractMin + FIntVector(
			AcceptedPayload.FrozenTerrainContract.FootprintSizeInCells.X * AcceptedPayload.FrozenTerrainContract.SharedCellSizeInBlocks.X,
			AcceptedPayload.FrozenTerrainContract.FootprintSizeInCells.Y * AcceptedPayload.FrozenTerrainContract.SharedCellSizeInBlocks.Y,
			0);
		for (const FLayoutFrozenTerrainWriteRecord& TerrainWrite : AcceptedPayload.FrozenTerrainContract.TerrainWrites)
		{
			TestTrue(TEXT("Accepted Underground terrain write stays inside frozen footprint XY"),
				TerrainWrite.BlockWorldPos.X >= ContractMin.X && TerrainWrite.BlockWorldPos.X < ContractMaxExclusive.X
					&& TerrainWrite.BlockWorldPos.Y >= ContractMin.Y && TerrainWrite.BlockWorldPos.Y < ContractMaxExclusive.Y);
		}
	}

	const FIntVector SharedCellSizeInBlocks = LayoutWorldBindingRuntimeHelpers::ResolveRuntimeSharedCellSizeInBlocks(
		UndergroundSolveResult,
		ContentSet);
	const TSet<FIntVector> RequiredChunkOrigins = FLayoutStreamingWindow::CollectRequiredChunkOriginsForSite(
		ResolvedSiteRecords[0],
		SharedCellSizeInBlocks,
		Harness.World->WorldGenDef->ChunkBlockSize);
	TestTrue(TEXT("Constrained Underground site has chunks to realize"), !RequiredChunkOrigins.IsEmpty());
	FChunkWorldObservedChunkLifecycleEvent CreatedChunkEvent;
	CreatedChunkEvent.DetailLevel = 0;
	CreatedChunkEvent.EventType = EChunkWorldChunkLifecycleEventType::Created;
	CreatedChunkEvent.bServerAuthority = true;
	CreatedChunkEvent.bFinestDetail = true;
	CreatedChunkEvent.bGeneratedForFirstTime = true;
	for (const FIntVector& ChunkOrigin : RequiredChunkOrigins)
	{
		CreatedChunkEvent.ChunkBlockWorldPos = ChunkOrigin;
		Harness.RuntimeComponent->QueueObservedChunkLifecycle(CreatedChunkEvent);
	}
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();
	TArray<FResolvedLayoutSiteRecord> RealizedSiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	RealizedSiteRecords.RemoveAll([&](const FResolvedLayoutSiteRecord& Site) { return Site.RootSolveId != ExpectedRoot; });
	TestTrue(
		TEXT("Constrained Underground site realizes through frozen flat terrain contract"),
		RealizedSiteRecords.Num() == 1 && RealizedSiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized);
	if (RealizedSiteRecords.Num() == 1)
	{
		const FIntVector ExpectedFootprintMinBlockWorldPos = FLayoutStreamingWindow::ComputeFootprintMinBlockWorldPos(
			ResolvedSiteRecords[0].SiteCenterBlockWorldPos,
			UndergroundSolveResult.FootprintSize,
			SharedCellSizeInBlocks);
		TestEqual(
			TEXT("Realized constrained Underground site preserves frozen footprint anchor"),
			RealizedSiteRecords[0].RealizedFootprintMinBlockWorldPos,
			ExpectedFootprintMinBlockWorldPos);
		TestEqual(
			TEXT("Realized constrained Underground site preserves frozen shared cell size"),
			RealizedSiteRecords[0].SolveResult.SharedCellSizeInBlocks,
			SharedCellSizeInBlocks);
		if (!RealizedSiteRecords[0].SolveResult.Placements.IsEmpty())
		{
			const FIntVector TemplateAnchorBlockWorldPos =
				UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
					RealizedSiteRecords[0],
					RealizedSiteRecords[0].SolveResult.Placements[0],
					SharedCellSizeInBlocks);
			TestEqual(
				TEXT("Constrained Underground template stamps at frozen anchor"),
				Harness.World->GetBlockValueByBlockWorldPos(TemplateAnchorBlockWorldPos, ERessourceType::MaterialIndex),
				SinfullMaterial);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimePlanningWindowRejectsSurfaceProfileInsideCavityTest,
	"PorismExtension.Layout.Runtime.PlanningWindow.RejectsSurfaceProfileInsideCavity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimePlanningWindowRejectsSurfaceProfileInsideCavityTest::RunTest(const FString& Parameters)
{
	const FName BiomeRowName(TEXT("SurfaceInsideCavity"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Surface-in-cavity fixture creates runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Surface-in-cavity fixture creates chunk world"), Harness.World)
		|| Harness.World->WorldGenDef == nullptr)
	{
		return false;
	}

	ConfigurePlanningBiomeRow(Harness.World->WorldGenDef, BiomeRowName);
	// Procedural air Z=4..6 has a ceiling; open sky at Z=50 lies beyond this search.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAANIC3uQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAADDZCq6AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));
	// Cover the one-cell selected-site halo so the cavity cannot leak into unsampled open sky.
	for (int32 Y = -24; Y <= 32; ++Y)
	{
		for (int32 X = -24; X <= 32; ++X)
		{
			const FIntVector Floor(X, Y, 3);
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, Floor);
			for (int32 Z = 4; Z <= 6; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(FIntVector(X, Y, Z), EmptyMaterial, false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(X, Y, 7));
		}
	}

	ULayoutProfileAsset* const Profile = CreateOneCellPlanningProfile(GetTransientPackage());
	Profile->bUndergroundPlacement = false;
	Profile->ContentSet = CreateOneCellPlanningStampContentSet(GetTransientPackage(), Harness.World);
	ULayoutWorldBindingAsset* const Binding = CreateWorldBinding(
		GetTransientPackage(),
		TEXT("SurfaceInsideCavityBinding"),
		TEXT("PrimaryCandidate"),
		BiomeRowName,
		Profile);
	Binding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 8;
	Harness.RuntimeComponent->SetLayoutWorldBindings({Binding});

	ULayoutPlanningWindowStore* const Store = Harness.RuntimeComponent->GetLayoutPlanningWindowStore();
	if (!TestNotNull(TEXT("Surface-in-cavity fixture creates planning store"), Store))
	{
		return false;
	}
	FLayoutPlanningWindowSettings Settings;
	Settings.SampleSpacing = 8;
	Settings.SampleSpacingUnit = ELayoutPlanningWindowUnit::Blocks;
	Store->SetPlanningWindowSettings(Settings);

	const FIntVector SelectedSite(8, 8, 4);
	Harness.TrackPlanningCharacter(SelectedSite);
	Harness.World->OnChunkCreate(FLayoutStreamingWindow::BlockWorldPosToChunkOrigin(SelectedSite, Harness.World->WorldGenDef->ChunkBlockSize),
		Harness.World->GetChunkLayerCount() - 1, {});
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();
	TestEqual(
		TEXT("Surface profile inside cavity publishes no resolved site"),
		Harness.RuntimeComponent->GetResolvedLayoutSiteRecords().Num(),
		0);
	TestTrue(
		TEXT("Surface profile inside cavity leaves no pending or rejected solve record"),
		Store->GetPlannedLayoutSiteRecords().IsEmpty());
	return true;
}
