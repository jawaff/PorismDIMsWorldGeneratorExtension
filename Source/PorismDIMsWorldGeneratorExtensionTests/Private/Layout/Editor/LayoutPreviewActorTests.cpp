// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Layout/Actors/LayoutPreviewActor.h"

#include "Algo/Count.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "Components/ArrowComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Editor.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Solver/LayoutWorldBindingSolveRequestBuilder.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Misc/AutomationTest.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;

namespace
{
	const FLayoutId PreviewPlacementPolicyId(TEXT("PreviewActor"));
	const FLayoutId DirectRootPlacementPolicyId(TEXT("DirectRootExplicit"));
	const ELayoutWorldBindingPlacementKind PreviewPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	constexpr float PreviewTimeoutBudgetSeconds = 1.0e-6f;
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");
	const FString CoordinatorFallbackMessageSubstring =
		TEXT("fell back to the legacy recursive body after coordinator failure");

	FLayoutWorldBindingPlacementPolicy BuildExpectedPreviewPlacementPolicy()
	{
		FLayoutWorldBindingPlacementPolicy PlacementPolicy;
		return PlacementPolicy;
	}

	FLayoutNoiseCoordinateSettings BuildNoiseCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
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

	UObject* CreatePreviewActorTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	ULayoutModuleAsset* CreateOneCellStampModule(
		UObject* Outer,
		AChunkWorldExtended* World,
		const TCHAR* ModuleName,
		const int32 MaterialIndex,
		const TArray<ELayoutCellIntent>& SupportedIntents)
	{
		UChunkStructureTemplate* Template = CreateTemplate(Outer, *FString::Printf(TEXT("%s_Template"), ModuleName), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(Template, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, MaterialIndex);

		return CreateModule(
			Outer,
			ModuleName,
			Template,
			SupportedIntents,
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
	}

	ULayoutModuleAsset* CreateSingleCellPreviewRuntimeVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
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

	ULayoutModuleAsset* CreateSingleCellPreviewRuntimeJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegY,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
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

	ULayoutModuleAsset* CreateSingleCellPreviewRuntimeStructuralModule(UObject* Outer, const TCHAR* BaseName)
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

	ULayoutRegionContentSetAsset* CreateExplicitRootParentChildContentSet(
		UObject* Outer,
		AChunkWorldExtended* World,
		ULayoutProfileAsset*& OutChildProfile,
		ULayoutModuleAsset*& OutParentModule,
		ULayoutModuleAsset*& OutChildModule,
		const int32 ParentMaterial,
		const int32 ChildMaterial)
	{
		OutParentModule = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_PreviewParent"),
			ParentMaterial,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		OutParentModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};

		OutChildModule = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_PreviewChild"),
			ChildMaterial,
			{ELayoutCellIntent::Boundary});
		OutChildModule->Roles = {ELayoutModuleRole::Boundary};
		OutChildModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		FLayoutRegionContentEntry ChildModuleEntry;
		ChildModuleEntry.EntryId = TEXT("PreviewChildShell");
		ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildModuleEntry.ModuleSettings.Module = OutChildModule;
		ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_PreviewChild"),
			{ChildModuleEntry});

		OutChildProfile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_PreviewChild"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		OutChildProfile->ContentSet = ChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("PreviewParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = OutParentModule;

		FLayoutRegionContentEntry ChildRegionEntry;
		ChildRegionEntry.EntryId = TEXT("PreviewChildRoom");
		ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		ChildRegionEntry.ChildRegionSettings.RegionProfile = OutChildProfile;
		ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_PreviewParentChild"),
			{ParentModuleEntry, ChildRegionEntry});
	}

	ULayoutProfileAsset* CreateExplicitRootParentProfile(UObject* Outer)
	{
		return CreateProfile(
			Outer,
			TEXT("LayoutProfile_PreviewParent"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			1,
			0,
			false);
	}

	ULayoutCompositeModuleAsset* CreatePreviewCompositeModule(
		UObject* Outer,
		AChunkWorldExtended* World)
	{
		ULayoutModuleAsset* FirstLeaf = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_PreviewCompositeFirstLeaf"),
			SinfullMaterial,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior});
		FirstLeaf->Roles = {ELayoutModuleRole::Boundary};
		FirstLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		FirstLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutModuleAsset* SecondLeaf = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_PreviewCompositeSecondLeaf"),
			SinfullMaterial + 7,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior});
		SecondLeaf->Roles = {ELayoutModuleRole::Boundary};
		SecondLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_PreviewPair"));
		FLayoutCompositeModuleCell& FirstCell = Composite->Cells.AddDefaulted_GetRef();
		FirstCell.Module = FirstLeaf;
		FirstCell.LocalCell = FIntVector(0, 0, 0);
		FirstCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& SecondCell = Composite->Cells.AddDefaulted_GetRef();
		SecondCell.Module = SecondLeaf;
		SecondCell.LocalCell = FIntVector(1, 0, 0);
		SecondCell.RelativeYawRotationSteps = 0;

		return Composite;
	}

	ULayoutCompositeModuleAsset* CreatePreviewCompositeInterfaceModule(
		UObject* Outer,
		AChunkWorldExtended* World)
	{
		UChunkStructureTemplate* EntryTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_PreviewCompositeInterfaceEntry"), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(EntryTemplate, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, SinfullMaterial);
		ULayoutModuleAsset* EntryLeaf = CreateModule(
			Outer,
			TEXT("LayoutModule_PreviewCompositeInterfaceEntry"),
			EntryTemplate,
			{ELayoutCellIntent::Entry, ELayoutCellIntent::Boundary},
			{
				MakeConnectionFaceRule(
					ELayoutFaceDirection::PosX,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
					MakeTags({LayoutGameplayTags::TraversalPrimary})),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegX,
					LayoutGameplayTags::FaceEntry,
					MakeTags({LayoutGameplayTags::FaceEntry}),
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
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeConnectionFaceRule(
					ELayoutFaceDirection::NegZ,
					LayoutGameplayTags::FaceOpen,
					MakeTags({LayoutGameplayTags::FaceOpen}),
					ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			},
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			{},
			FGameplayTagContainer());
		EntryLeaf->Roles = {ELayoutModuleRole::Entry, ELayoutModuleRole::Boundary};
		EntryLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		EntryLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		UChunkStructureTemplate* BoundaryTemplate = CreateTemplate(Outer, TEXT("LayoutTemplate_PreviewCompositeInterfaceBoundary"), FIntVector(1, 1, 1));
		FLayoutTestWorldSupport::ConfigureSolidTemplate(BoundaryTemplate, World, FIntVector(1, 1, 1), FIntVector::ZeroValue, SinfullMaterial + 13);
		ULayoutModuleAsset* BoundaryLeaf = CreateModule(
			Outer,
			TEXT("LayoutModule_PreviewCompositeInterfaceBoundary"),
			BoundaryTemplate,
			{ELayoutCellIntent::Boundary},
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
		BoundaryLeaf->Roles = {ELayoutModuleRole::Boundary};
		BoundaryLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		BoundaryLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		BoundaryLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		BoundaryLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		BoundaryLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		BoundaryLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_PreviewInterfacePair"));
		FLayoutCompositeModuleCell& FirstCell = Composite->Cells.AddDefaulted_GetRef();
		FirstCell.Module = EntryLeaf;
		FirstCell.LocalCell = FIntVector(0, 0, 0);
		FirstCell.RelativeYawRotationSteps = 0;

		FLayoutCompositeModuleCell& SecondCell = Composite->Cells.AddDefaulted_GetRef();
		SecondCell.Module = BoundaryLeaf;
		SecondCell.LocalCell = FIntVector(1, 0, 0);
		SecondCell.RelativeYawRotationSteps = 0;

		return Composite;
	}

	ALayoutPreviewActor* SpawnPreviewActor(FAutomationTestBase& Test)
	{
		UWorld* const EditorWorld = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!Test.TestNotNull(TEXT("Editor world exists for preview actor tests"), EditorWorld))
		{
			return nullptr;
		}

		ALayoutPreviewActor* const PreviewActor = EditorWorld->SpawnActor<ALayoutPreviewActor>();
		Test.TestNotNull(TEXT("Preview actor can be spawned in the editor world"), PreviewActor);
		return PreviewActor;
	}

	bool TryBuildExpectedPreviewRequest(
		FAutomationTestBase& Test,
		const ALayoutPreviewActor& PreviewActor,
		AChunkWorldExtended* const PreviewChunkWorld,
		FLayoutRegionSolveRequest& OutRequest)
	{
		const FString PreviewRootPath = FString::Printf(TEXT("Preview/%s"), *PreviewActor.GetName());
		const FLayoutWorldBindingPlacementPolicy PreviewPlacementPolicy = PreviewActor.PreviewPlacementPolicy;
		const FLayoutWorldBindingRuntimeView PreviewRuntimeView =
			LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
				PreviewActor.LayoutProfile,
				PreviewActor.LayoutProfile != nullptr ? PreviewActor.LayoutProfile->ContentSet.Get() : nullptr,
				PreviewActor.SolveBudget,
				PreviewPlacementPolicy);
		FString FailureReason;
		if (PreviewChunkWorld != nullptr && PreviewChunkWorld->WorldGenDef != nullptr)
		{
			FLayoutActiveBiomeSampler ActiveBiomeSampler;
			if (!Test.TestTrue(
				TEXT("Equivalent world-backed preview stepped-support sampler initializes"),
				ActiveBiomeSampler.Initialize(GetTransientPackage(), PreviewChunkWorld->WorldGenDef, PreviewChunkWorld->Seed)))
			{
				return false;
			}

			const bool bBuilt = LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
				PreviewRuntimeView,
				NAME_None,
				FLayoutStreamingWindow::SnapSiteCenterBlockWorldPosToCellLattice(
					PreviewChunkWorld->UEWorldPosToBlockWorldPos(PreviewActor.GetActorLocation()),
					PreviewActor.LayoutProfile != nullptr && PreviewActor.LayoutProfile->ContentSet != nullptr
						? PreviewActor.LayoutProfile->ContentSet->GetSharedCellSizeInBlocks()
						: FIntVector(16, 16, 16)),
				PreviewActor.Seed,
				BuildNoiseCoordinateSettings(PreviewChunkWorld->WorldGenDef),
				ActiveBiomeSampler,
				OutRequest,
				FailureReason,
				PreviewChunkWorld);
			if (!Test.TestTrue(TEXT("Equivalent world-backed preview request can be built from the explicit-root runtime-view seam"), bBuilt))
			{
				if (!FailureReason.IsEmpty())
				{
					Test.AddError(FailureReason);
				}
				return false;
			}

			return true;
		}

		const bool bBuilt = LayoutWorldBindingSolveRequestBuilder::TryBuildStandaloneSolveRequest(
			PreviewRuntimeView,
			PreviewActor.Seed,
			PreviewRootPath,
			PreviewPlacementPolicyId,
			FLayoutId(*PreviewRootPath),
			FLayoutId(*PreviewRootPath),
			OutRequest,
			FailureReason);
		if (!Test.TestTrue(TEXT("Equivalent standalone preview request can be built from the runtime-view seam"), bBuilt))
		{
			if (!FailureReason.IsEmpty())
			{
				Test.AddError(FailureReason);
			}
		}
		return bBuilt;
	}

	const FLayoutRegionSolveResult* FindExpectedRootRegionResult(
		const FLayoutRegionSolveScheduleResult& ScheduleResult,
		const FLayoutRegionSolveRequest& Request)
	{
		return ScheduleResult.RegionResults.FindByPredicate([&Request](const FLayoutRegionSolveResult& RegionResult)
		{
			return RegionResult.RegionDebugPath == Request.RegionDebugPath;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorDrawsCompositePlacementCellsTest,
	"PorismExtension.Layout.Editor.PreviewActor.DrawsCompositePlacementCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorPublishesAnyActiveSteppedSupportTest,
	"PorismExtension.Layout.Editor.PreviewActor.PublishesAnyActiveSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorPublishesThreeColumnAnyActiveSteppedSupportTest,
	"PorismExtension.Layout.Editor.PreviewActor.PublishesThreeColumnAnyActiveSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorPublishesMultiTransitionAnyActiveSteppedSupportTest,
	"PorismExtension.Layout.Editor.PreviewActor.PublishesMultiTransitionAnyActiveSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorDrawsCompositePlacementCellsWithoutBundleBoundsTest,
	"PorismExtension.Layout.Editor.PreviewActor.DrawsCompositePlacementCellsWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorDrawsLeafPlacementCellsWithoutOccupiedLocalCellsTest,
	"PorismExtension.Layout.Editor.PreviewActor.DrawsLeafPlacementCellsWithoutOccupiedLocalCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutPreviewActorDrawsCompositeInterfaceMarkersTest,
	"PorismExtension.Layout.Editor.PreviewActor.DrawsCompositeInterfaceMarkers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutPreviewActorPublishesAnyActiveSteppedSupportTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorAnyActiveSteppedSupport"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(Outer, FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Chunk-world harness creates a world generation definition"), Harness.World->WorldGenDef.Get()))
	{
		return false;
	}

	Harness.World->WorldGenDef->WorldBiomes.Reset();
	FBiomeDualData& Row = Harness.World->WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);

	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutPreviewAnyActiveSteppedSupportTemplate"),
		FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutPreviewAnyActiveSteppedSupportModule"),
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

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("PreviewAnyActiveSteppedSupportEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewAnyActiveSteppedSupport"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewAnyActiveSteppedSupport"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = 2713;
	PreviewActor->PreviewPlacementPolicy = BuildExpectedPreviewPlacementPolicy();
	PreviewActor->SetActorLocation(Harness.World->BlockWorldPosToUEWorldPos(FIntVector(16, 8, 16)));

	if (!TestTrue(TEXT("Preview actor solves the any-active stepped-support fixture"), PreviewActor->RebuildPreview()))
	{
		PreviewActor->Destroy();
		return false;
	}

	FLayoutRegionSolveRequest ExpectedRequest;
	if (!TryBuildExpectedPreviewRequest(*this, *PreviewActor, Harness.World, ExpectedRequest))
	{
		PreviewActor->Destroy();
		return false;
	}

	const FLayoutRegionSolveScheduleResult ExpectedScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(ExpectedRequest);
	const FLayoutRegionSolveResult* const ExpectedRootRegionResult =
		FindExpectedRootRegionResult(ExpectedScheduleResult, ExpectedRequest);

	TestTrue(
		TEXT("Preview actor publishes support samples for any-active stepped support"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SupportSamples.Num() > 0);
	TestEqual(
		TEXT("Preview actor any-active stepped-support carrier keeps the same shared cell height as the equivalent standalone request"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks,
		ExpectedRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks);
	TestEqual(
		TEXT("Preview actor any-active stepped-support carrier keeps the same support-sample count as the equivalent standalone request"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SupportSamples.Num(),
		ExpectedRequest.SteppedTerrainSupportMap.SupportSamples.Num());
	TestEqual(
		TEXT("Preview actor any-active stepped-support carrier keeps the same adjacency-step count as the equivalent standalone request"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.AdjacencySteps.Num(),
		ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps.Num());
	TestTrue(
		TEXT("Equivalent standalone preview solve also publishes support samples for the stepped fixture"),
		ExpectedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num() > 0);
	if (TestNotNull(TEXT("Preview actor caches the stepped root-region result"), ExpectedRootRegionResult))
	{
		TestEqual(
			TEXT("Cached root region keeps the same stepped support-sample count as the equivalent standalone preview solve"),
			PreviewActor->GetCachedRegionResultForTesting().SteppedTerrainSupportMap.SupportSamples.Num(),
			ExpectedRootRegionResult->SteppedTerrainSupportMap.SupportSamples.Num());
	}

	PreviewActor->Destroy();
	return true;
}

bool FLayoutPreviewActorPublishesThreeColumnAnyActiveSteppedSupportTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorThreeColumnAnyActiveSteppedSupport"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(Outer, FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Chunk-world harness creates a world generation definition"), Harness.World->WorldGenDef.Get()))
	{
		return false;
	}

	Harness.World->WorldGenDef->WorldBiomes.Reset();
	FBiomeDualData& Row = Harness.World->WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);

	const TArray<FIntVector> LowerStepColumns = {
		FIntVector(8, 8, 0),
		FIntVector(8, 9, 0),
		FIntVector(7, 8, 0),
		FIntVector(9, 8, 0)
	};
	const TArray<FIntVector> MiddleStepColumns = {
		FIntVector(24, 8, 0),
		FIntVector(24, 9, 0),
		FIntVector(23, 8, 0),
		FIntVector(25, 8, 0)
	};
	const TArray<FIntVector> UpperStepColumns = {
		FIntVector(40, 8, 0),
		FIntVector(40, 9, 0),
		FIntVector(39, 8, 0),
		FIntVector(41, 8, 0)
	};
	auto WriteSteppedSurfaceColumns =
		[&Harness](const TArray<FIntVector>& ColumnBases, const int32 SurfaceZ)
	{
		for (const FIntVector& ColumnBase : ColumnBases)
		{
			for (int32 Z = 0; Z <= 16; ++Z)
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
	WriteSteppedSurfaceColumns(LowerStepColumns, 3);
	WriteSteppedSurfaceColumns(MiddleStepColumns, 5);
	WriteSteppedSurfaceColumns(UpperStepColumns, 7);

	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutPreviewThreeColumnAnyActiveSteppedSupportTemplate"),
		FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutPreviewThreeColumnAnyActiveSteppedSupportModule"),
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

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("PreviewThreeColumnAnyActiveSteppedSupportEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewThreeColumnAnyActiveSteppedSupport"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewThreeColumnAnyActiveSteppedSupport"),
		FIntPoint(3, 1),
		FIntPoint(3, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = 2714;
	PreviewActor->PreviewPlacementPolicy = BuildExpectedPreviewPlacementPolicy();
	PreviewActor->SetActorLocation(Harness.World->BlockWorldPosToUEWorldPos(FIntVector(24, 8, 16)));

	if (!TestTrue(TEXT("Preview actor solves the three-column any-active stepped-support fixture"), PreviewActor->RebuildPreview()))
	{
		PreviewActor->Destroy();
		return false;
	}

	FLayoutRegionSolveRequest ExpectedRequest;
	if (!TryBuildExpectedPreviewRequest(*this, *PreviewActor, Harness.World, ExpectedRequest))
	{
		PreviewActor->Destroy();
		return false;
	}

	const FLayoutRegionSolveScheduleResult ExpectedScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(ExpectedRequest);
	const FLayoutRegionSolveResult* const ExpectedRootRegionResult =
		FindExpectedRootRegionResult(ExpectedScheduleResult, ExpectedRequest);

	TestTrue(
		TEXT("Preview actor publishes a wider stepped support surface for the three-column any-active stepped fixture"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SupportSamples.Num() > 2);
	TestTrue(
		TEXT("Preview actor publishes multiple stepped adjacencies for the three-column any-active stepped fixture"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.AdjacencySteps.Num() > 1);
	TestEqual(
		TEXT("Preview actor three-column any-active stepped-support carrier keeps the same shared cell height as the equivalent standalone request"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks,
		ExpectedRequest.SteppedTerrainSupportMap.SharedCellHeightInBlocks);
	TestEqual(
		TEXT("Preview actor three-column any-active stepped-support carrier keeps the same support-sample count as the equivalent standalone request"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SupportSamples.Num(),
		ExpectedRequest.SteppedTerrainSupportMap.SupportSamples.Num());
	TestEqual(
		TEXT("Preview actor three-column any-active stepped-support carrier keeps the same adjacency-step count as the equivalent standalone request"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.AdjacencySteps.Num(),
		ExpectedRequest.SteppedTerrainSupportMap.AdjacencySteps.Num());
	TestTrue(
		TEXT("Equivalent standalone preview solve also publishes a wider stepped support surface for the three-column fixture"),
		ExpectedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num() > 2);
	TestTrue(
		TEXT("Equivalent standalone preview solve also publishes multiple stepped adjacencies for the three-column fixture"),
		ExpectedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.Num() > 1);
	TestEqual(
		TEXT("Preview actor three-column any-active stepped fixture keeps the standalone forced-insertion count"),
		PreviewActor->PreviewResult.ForcedPlacementBundleInsertions.Num(),
		ExpectedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num());
	TestEqual(
		TEXT("Preview actor three-column any-active stepped fixture keeps the standalone route-constraint count"),
		PreviewActor->PreviewResult.RequestOwnedRequiredRouteConstraints.Num(),
		ExpectedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Preview actor three-column any-active stepped fixture keeps the standalone vertical-access placement count"),
		CountVerticalAccessPlacements(PreviewActor->PreviewResult),
		CountVerticalAccessPlacements(ExpectedScheduleResult.MergedSolveResult));
	if (TestNotNull(TEXT("Preview actor caches the three-column stepped root-region result"), ExpectedRootRegionResult))
	{
		TestEqual(
			TEXT("Cached root region keeps the same stepped support-sample count as the equivalent standalone preview solve for the three-column fixture"),
			PreviewActor->GetCachedRegionResultForTesting().SteppedTerrainSupportMap.SupportSamples.Num(),
			ExpectedRootRegionResult->SteppedTerrainSupportMap.SupportSamples.Num());
		TestEqual(
			TEXT("Cached root region keeps the same forced-insertion count as the equivalent standalone preview solve for the three-column fixture"),
			PreviewActor->GetCachedRegionResultForTesting().ForcedPlacementBundleInsertions.Num(),
			ExpectedRootRegionResult->ForcedPlacementBundleInsertions.Num());
		TestEqual(
			TEXT("Cached root region keeps the same route-constraint count as the equivalent standalone preview solve for the three-column fixture"),
			PreviewActor->GetCachedRegionResultForTesting().RequiredRouteConstraints.Num(),
			ExpectedRootRegionResult->RequiredRouteConstraints.Num());
		TestEqual(
			TEXT("Cached root region keeps the same vertical-access placement count as the equivalent standalone preview solve for the three-column fixture"),
			CountVerticalAccessPlacements(PreviewActor->GetCachedRegionResultForTesting().SolveResult),
			CountVerticalAccessPlacements(ExpectedRootRegionResult->SolveResult));
	}

	PreviewActor->Destroy();
	return true;
}

bool FLayoutPreviewActorPublishesMultiTransitionAnyActiveSteppedSupportTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorMultiTransitionAnyActiveSteppedSupport"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(Outer, FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Chunk-world harness creates a world generation definition"), Harness.World->WorldGenDef.Get()))
	{
		return false;
	}

	Harness.World->WorldGenDef->WorldBiomes.Reset();
	FBiomeDualData& Row = Harness.World->WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);

	const TArray<FIntVector> FirstStepColumns = {
		FIntVector(8, 8, 0),
		FIntVector(8, 24, 0),
		FIntVector(8, 40, 0)
	};
	const TArray<FIntVector> SecondStepColumns = {
		FIntVector(24, 8, 0),
		FIntVector(24, 24, 0),
		FIntVector(24, 40, 0)
	};
	const TArray<FIntVector> ThirdStepColumns = {
		FIntVector(40, 8, 0),
		FIntVector(40, 24, 0),
		FIntVector(40, 40, 0)
	};
	const TArray<FIntVector> FourthStepColumns = {
		FIntVector(56, 8, 0),
		FIntVector(56, 24, 0),
		FIntVector(56, 40, 0)
	};
	const TArray<FIntVector> FifthStepColumns = {
		FIntVector(72, 8, 0),
		FIntVector(72, 24, 0),
		FIntVector(72, 40, 0)
	};
	auto WriteSteppedSurfaceColumns =
		[&Harness](const TArray<FIntVector>& ColumnBases, const int32 SurfaceZ)
	{
		for (const FIntVector& ColumnBase : ColumnBases)
		{
			for (int32 Z = 0; Z <= 16; ++Z)
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
	WriteSteppedSurfaceColumns(FirstStepColumns, 3);
	WriteSteppedSurfaceColumns(SecondStepColumns, 5);
	WriteSteppedSurfaceColumns(ThirdStepColumns, 7);
	WriteSteppedSurfaceColumns(FourthStepColumns, 9);
	WriteSteppedSurfaceColumns(FifthStepColumns, 11);

	ULayoutModuleAsset* EdgeVerticalAccessModule = CreateSingleCellPreviewRuntimeVerticalAccessModule(
		Outer,
		TEXT("LayoutPreviewMultiTransitionEdgeVerticalAccess"));
	ULayoutModuleAsset* JunctionVerticalAccessModule = CreateSingleCellPreviewRuntimeJunctionVerticalAccessModule(
		Outer,
		TEXT("LayoutPreviewMultiTransitionJunctionVerticalAccess"));
	ULayoutModuleAsset* StructuralModule = CreateSingleCellPreviewRuntimeStructuralModule(
		Outer,
		TEXT("LayoutPreviewMultiTransitionStructural"));

	FLayoutRegionContentEntry EdgeVerticalAccessEntry;
	EdgeVerticalAccessEntry.EntryId = TEXT("LayoutPreviewMultiTransitionEdgeVerticalAccessEntry");
	EdgeVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
	EdgeVerticalAccessEntry.ModuleSettings.Module = EdgeVerticalAccessModule;
	EdgeVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

	FLayoutRegionContentEntry JunctionVerticalAccessEntry;
	JunctionVerticalAccessEntry.EntryId = TEXT("LayoutPreviewMultiTransitionJunctionVerticalAccessEntry");
	JunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
	JunctionVerticalAccessEntry.ModuleSettings.Module = JunctionVerticalAccessModule;
	JunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

	FLayoutRegionContentEntry StructuralEntry;
	StructuralEntry.EntryId = TEXT("LayoutPreviewMultiTransitionStructuralEntry");
	StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
	StructuralEntry.ModuleSettings.Module = StructuralModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewMultiTransitionAnyActiveSteppedSupport"),
		{StructuralEntry, EdgeVerticalAccessEntry, JunctionVerticalAccessEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewMultiTransitionAnyActiveSteppedSupport"),
		FIntPoint(5, 3),
		FIntPoint(5, 3),
		2,
		0,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;
	Profile->bRequireAllTraversalChannelsReachable = false;
	Profile->VerticalAccessCountMode = ELayoutCountConstraintMode::Exact;
	Profile->VerticalAccessCount = 3;
	Profile->MinVerticalAccessCount = 3;
	Profile->MaxVerticalAccessCount = 3;

	const FLayoutValidationResult ContentSetValidation = ContentSet->ValidateContentSet();
	if (!TestTrue(TEXT("Preview multi-transition fixture content set validates"), ContentSetValidation.IsValid()))
	{
		for (const FLayoutValidationMessage& Message : ContentSetValidation.Messages)
		{
			AddInfo(Message.Message);
		}
		return false;
	}

	const FLayoutValidationResult ProfileValidation = Profile->ValidateProfile();
	if (!TestTrue(TEXT("Preview multi-transition fixture profile validates"), ProfileValidation.IsValid()))
	{
		for (const FLayoutValidationMessage& Message : ProfileValidation.Messages)
		{
			AddInfo(Message.Message);
		}
		return false;
	}

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	const FIntVector SiteCenterBlockWorldPos(40, 24, 16);
	const int32 SolveSeed = FLayoutSiteReservation::ComputeSiteSolveSeed(
		SiteCenterBlockWorldPos,
		Harness.World->Seed);
	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = SolveSeed;
	PreviewActor->PreviewPlacementPolicy = BuildExpectedPreviewPlacementPolicy();
	PreviewActor->PreviewPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	PreviewActor->PreviewPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	PreviewActor->PreviewPlacementPolicy.HeightIgnoreThreshold = 0;
	PreviewActor->PreviewPlacementPolicy.TerrainSampleGridSpacing = 16;
	PreviewActor->PreviewPlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	PreviewActor->PreviewPlacementPolicy.TerrainTransition.MaxFoundationDepth = 3;
	PreviewActor->SetActorLocation(Harness.World->BlockWorldPosToUEWorldPos(SiteCenterBlockWorldPos));

	if (!TestTrue(TEXT("Preview actor solves the multi-transition any-active stepped-support fixture"), PreviewActor->RebuildPreview()))
	{
		PreviewActor->Destroy();
		return false;
	}

	FLayoutRegionSolveRequest ExpectedRequest;
	if (!TryBuildExpectedPreviewRequest(*this, *PreviewActor, Harness.World, ExpectedRequest))
	{
		PreviewActor->Destroy();
		return false;
	}

	const FLayoutRegionSolveScheduleResult ExpectedScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(ExpectedRequest);
	const FLayoutRegionSolveResult* const ExpectedRootRegionResult =
		FindExpectedRootRegionResult(ExpectedScheduleResult, ExpectedRequest);

	TestTrue(
		TEXT("Preview actor publishes a richer stepped support surface for the multi-transition fixture"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SupportSamples.Num() > 3);
	TestTrue(
		TEXT("Preview actor publishes multiple stepped adjacencies for the multi-transition fixture"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.AdjacencySteps.Num() > 2);
	TestTrue(
		TEXT("Preview actor multi-transition stepped fixture keeps more than two vertical-access placements"),
		CountVerticalAccessPlacements(PreviewActor->PreviewResult) > 2);
	TestEqual(
		TEXT("Preview actor multi-transition stepped fixture keeps the equivalent standalone support-sample count"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.SupportSamples.Num(),
		ExpectedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num());
	TestEqual(
		TEXT("Preview actor multi-transition stepped fixture keeps the equivalent standalone adjacency-step count"),
		PreviewActor->PreviewResult.SteppedTerrainSupportMap.AdjacencySteps.Num(),
		ExpectedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.Num());
	TestTrue(
		TEXT("Preview actor multi-transition stepped fixture publishes non-empty prepared forced insertions"),
		PreviewActor->PreviewResult.ForcedPlacementBundleInsertions.Num() > 0);
	TestTrue(
		TEXT("Preview actor multi-transition stepped fixture publishes non-empty request-owned stepped route constraints"),
		PreviewActor->PreviewResult.RequestOwnedRequiredRouteConstraints.Num() > 0);
	TestEqual(
		TEXT("Preview actor multi-transition stepped fixture keeps the equivalent standalone forced-insertion count"),
		PreviewActor->PreviewResult.ForcedPlacementBundleInsertions.Num(),
		ExpectedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num());
	TestEqual(
		TEXT("Preview actor multi-transition stepped fixture keeps the equivalent standalone route-constraint count"),
		PreviewActor->PreviewResult.RequestOwnedRequiredRouteConstraints.Num(),
		ExpectedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Preview actor multi-transition stepped fixture keeps the equivalent standalone vertical-access placement count"),
		CountVerticalAccessPlacements(PreviewActor->PreviewResult),
		CountVerticalAccessPlacements(ExpectedScheduleResult.MergedSolveResult));
	TestTrue(
		TEXT("Preview actor multi-transition stepped fixture preserves a junction-capable vertical-access placement"),
		PreviewActor->PreviewResult.Placements.ContainsByPredicate(
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.Module != nullptr
					&& Placement.Module->GetFName() == FLayoutId(TEXT("LayoutPreviewMultiTransitionJunctionVerticalAccess_Module"));
			}));
	if (TestNotNull(TEXT("Preview actor caches the multi-transition stepped root-region result"), ExpectedRootRegionResult))
	{
		TestEqual(
			TEXT("Cached root region keeps the same stepped support-sample count as the equivalent standalone preview solve for the multi-transition fixture"),
			PreviewActor->GetCachedRegionResultForTesting().SteppedTerrainSupportMap.SupportSamples.Num(),
			ExpectedRootRegionResult->SteppedTerrainSupportMap.SupportSamples.Num());
		TestEqual(
			TEXT("Cached root region keeps the same forced-insertion count as the equivalent standalone preview solve for the multi-transition fixture"),
			PreviewActor->GetCachedRegionResultForTesting().ForcedPlacementBundleInsertions.Num(),
			ExpectedRootRegionResult->ForcedPlacementBundleInsertions.Num());
		TestEqual(
			TEXT("Cached root region keeps the same route-constraint count as the equivalent standalone preview solve for the multi-transition fixture"),
			PreviewActor->GetCachedRegionResultForTesting().RequiredRouteConstraints.Num(),
			ExpectedRootRegionResult->RequiredRouteConstraints.Num());
		TestEqual(
			TEXT("Cached root region keeps the same vertical-access placement count as the equivalent standalone preview solve for the multi-transition fixture"),
			CountVerticalAccessPlacements(PreviewActor->GetCachedRegionResultForTesting().SolveResult),
			CountVerticalAccessPlacements(ExpectedRootRegionResult->SolveResult));
	}

	PreviewActor->Destroy();
	return true;
}

bool FLayoutPreviewActorDrawsCompositePlacementCellsTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorCompositePlacements"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(Outer);
	if (Harness.World == nullptr)
	{
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreatePreviewCompositeModule(Outer, Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Preview composite fixture validates before solving"), CompositeValidation.IsValid()))
	{
		return false;
	}

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("PreviewCompositeEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewComposite"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewComposite"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = 1771;
	PreviewActor->bDrawRootFootprintBounds = false;
	PreviewActor->bDrawPlannedCells = false;
	PreviewActor->bDrawPlacements = true;
	PreviewActor->bDrawChildRegionBounds = false;
	PreviewActor->bDrawExportedEntryCells = false;
	PreviewActor->bDrawExportedBoundaryPoints = false;
	PreviewActor->bDrawClosureSegments = false;
	PreviewActor->bDrawClosureRuns = false;
	PreviewActor->bDrawPartitionSeams = false;
	PreviewActor->bDrawResidualCells = false;
	PreviewActor->bDrawSparsePlacements = false;
	PreviewActor->VisiblePlacementRoles = {ELayoutModuleRole::Boundary};

	if (!TestTrue(TEXT("Preview actor solves the simple same-level composite profile"), PreviewActor->RebuildPreview()))
	{
		AddError(PreviewActor->PreviewResult.FailureReason);
		PreviewActor->Destroy();
		return false;
	}

		FLayoutRegionSolveRequest ExpectedRequest;
		if (!TryBuildExpectedPreviewRequest(*this, *PreviewActor, Harness.World, ExpectedRequest))
		{
			PreviewActor->Destroy();
			return false;
		}
		const FLayoutRegionSolveScheduleResult ExpectedScheduleResult =
			FLayoutProfileSolver::SolveRegionTree(ExpectedRequest);
	const FLayoutRegionSolveResult* const ExpectedRootRegionResult =
		FindExpectedRootRegionResult(ExpectedScheduleResult, ExpectedRequest);
	TestEqual(TEXT("Preview actor publishes one solved composite placement bundle"), PreviewActor->PreviewResult.Placements.Num(), 1);
	const FLayoutWorldBindingPlacementPolicy ExpectedPreviewPlacementPolicy = BuildExpectedPreviewPlacementPolicy();
	TestTrue(TEXT("Equivalent standalone preview solve succeeds for the simple composite fixture"), ExpectedScheduleResult.MergedSolveResult.bSucceeded);
	TestEqual(TEXT("Equivalent standalone preview solve keeps the explicit preview placement kind on the merged solve result"), ExpectedScheduleResult.MergedSolveResult.RootPlacementKind, PreviewPlacementKind);
	TestEqual(TEXT("Equivalent standalone preview solve keeps the explicit preview terrain sample spacing on the merged solve result"), ExpectedScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, ExpectedPreviewPlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(TEXT("Equivalent standalone preview solve keeps the explicit preview height ignore threshold on the merged solve result"), ExpectedScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold, ExpectedPreviewPlacementPolicy.HeightIgnoreThreshold);
	TestEqual(TEXT("Solved preview result keeps the solver-owned preview placement kind"), PreviewActor->PreviewResult.RootPlacementKind, ExpectedScheduleResult.MergedSolveResult.RootPlacementKind);
	TestEqual(TEXT("Solved preview result keeps the solver-owned preview terrain sample spacing"), PreviewActor->PreviewResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, ExpectedScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(TEXT("Solved preview result keeps the solver-owned preview height ignore threshold"), PreviewActor->PreviewResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold, ExpectedScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold);
	if (!TestNotNull(TEXT("Preview actor standalone composite solve keeps the expected root region result"), ExpectedRootRegionResult))
	{
		PreviewActor->Destroy();
		return false;
	}
	TestEqual(TEXT("Solved cached root region result keeps the solver-owned preview placement kind"), PreviewActor->GetCachedRegionResultForTesting().SolveResult.RootPlacementKind, ExpectedRootRegionResult->SolveResult.RootPlacementKind);
	TestEqual(TEXT("Solved cached root region result keeps the solver-owned preview terrain sample spacing"), PreviewActor->GetCachedRegionResultForTesting().SolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, ExpectedRootRegionResult->SolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(TEXT("Solved cached root region result keeps the solver-owned preview height ignore threshold"), PreviewActor->GetCachedRegionResultForTesting().SolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold, ExpectedRootRegionResult->SolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold);
	if (!TestTrue(
		TEXT("Preview actor preserves occupied local cells on the solved composite bundle"),
		PreviewActor->PreviewResult.Placements.Num() == 1
			&& PreviewActor->PreviewResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		PreviewActor->Destroy();
		return false;
	}
	PreviewActor->LayoutProfile->ContentSet = nullptr;
	PreviewActor->PostEditChange();

	TArray<UBoxComponent*> PlacementBoxes;
	PreviewActor->GetComponents<UBoxComponent>(PlacementBoxes);
	const int32 PreviewPlacementCount = Algo::CountIf(PlacementBoxes, [](const UBoxComponent* Component)
	{
		return Component != nullptr && Component->GetName().StartsWith(TEXT("PreviewPlacement"));
	});

	TestTrue(TEXT("Preview actor still resolves one cached preview result without the live content-set source"), PreviewActor->PreviewResult.SharedCellSizeInBlocks != FIntVector::ZeroValue);
	TestEqual(TEXT("Preview actor draws one structural box per occupied composite cell"), PreviewPlacementCount, 2);
	PreviewActor->Destroy();
	return true;
}

bool FLayoutPreviewActorDrawsCompositePlacementCellsWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorCompositePlacementsThinCarrier"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(Outer);
	if (Harness.World == nullptr)
	{
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreatePreviewCompositeModule(Outer, Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Preview composite thin-carrier fixture validates before solving"), CompositeValidation.IsValid()))
	{
		return false;
	}

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("PreviewCompositeThinCarrierEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewCompositeThinCarrier"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewCompositeThinCarrier"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = 1773;
	PreviewActor->bDrawRootFootprintBounds = false;
	PreviewActor->bDrawPlannedCells = false;
	PreviewActor->bDrawPlacements = true;
	PreviewActor->bDrawChildRegionBounds = false;
	PreviewActor->bDrawExportedEntryCells = false;
	PreviewActor->bDrawExportedBoundaryPoints = false;
	PreviewActor->bDrawClosureSegments = false;
	PreviewActor->bDrawClosureRuns = false;
	PreviewActor->bDrawPartitionSeams = false;
	PreviewActor->bDrawResidualCells = false;
	PreviewActor->bDrawSparsePlacements = false;
	PreviewActor->VisiblePlacementRoles = {ELayoutModuleRole::Boundary};

	if (!TestTrue(TEXT("Preview actor solves the thin-carrier composite profile"), PreviewActor->RebuildPreview()))
	{
		PreviewActor->Destroy();
		return false;
	}

	if (!TestTrue(
		TEXT("Preview actor keeps one solved composite bundle before clearing BundleBoundsCells"),
		PreviewActor->PreviewResult.Placements.Num() == 1
			&& PreviewActor->PreviewResult.Placements[0].CompositeModule == Composite
			&& PreviewActor->PreviewResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		PreviewActor->Destroy();
		return false;
	}

	PreviewActor->PreviewResult.Placements[0].BundleBoundsCells = FIntVector::ZeroValue;
	PreviewActor->PostEditChange();

	TArray<UBoxComponent*> PlacementBoxes;
	PreviewActor->GetComponents<UBoxComponent>(PlacementBoxes);
	const int32 PreviewPlacementCount = Algo::CountIf(PlacementBoxes, [](const UBoxComponent* Component)
	{
		return Component != nullptr && Component->GetName().StartsWith(TEXT("PreviewPlacement"));
	});

	TestEqual(TEXT("Preview actor still draws one structural box per occupied composite cell without BundleBoundsCells"), PreviewPlacementCount, 2);
	PreviewActor->Destroy();
	return true;
}

bool FLayoutPreviewActorDrawsLeafPlacementCellsWithoutOccupiedLocalCellsTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorLeafPlacementsThinCarrier"));

	UChunkStructureTemplate* Template = CreateTemplate(Outer, TEXT("LayoutTemplate_PreviewLeafThinCarrier"), FIntVector(8, 16, 8));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutModule_PreviewLeafThinCarrier"),
		Template,
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
	Module->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};
	SetModuleTemplateSize(Module, FIntVector(8, 16, 8));

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("PreviewLeafThinCarrierEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewLeafThinCarrier"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewLeafThinCarrier"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = 1775;
	PreviewActor->bDrawRootFootprintBounds = false;
	PreviewActor->bDrawPlannedCells = false;
	PreviewActor->bDrawPlacements = true;
	PreviewActor->bDrawChildRegionBounds = false;
	PreviewActor->bDrawExportedEntryCells = false;
	PreviewActor->bDrawExportedBoundaryPoints = false;
	PreviewActor->bDrawClosureSegments = false;
	PreviewActor->bDrawClosureRuns = false;
	PreviewActor->bDrawPartitionSeams = false;
	PreviewActor->bDrawResidualCells = false;
	PreviewActor->bDrawSparsePlacements = false;
	PreviewActor->VisiblePlacementRoles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};

	if (!TestTrue(TEXT("Preview actor solves the thin-carrier leaf profile"), PreviewActor->RebuildPreview()))
	{
		PreviewActor->Destroy();
		return false;
	}

	if (!TestTrue(
		TEXT("Preview actor initial solve preserves one occupied local cell on the leaf placement"),
		PreviewActor->PreviewResult.Placements.Num() == 1
			&& PreviewActor->PreviewResult.Placements[0].OccupiedLocalCells.Num() == 1))
	{
		PreviewActor->Destroy();
		return false;
	}

	PreviewActor->PreviewResult.Placements[0].OccupiedLocalCells.Reset();
	PreviewActor->PostEditChange();

	TArray<UBoxComponent*> PlacementBoxes;
	PreviewActor->GetComponents<UBoxComponent>(PlacementBoxes);
	const int32 PreviewPlacementCount = Algo::CountIf(PlacementBoxes, [](const UBoxComponent* Component)
	{
		return Component != nullptr && Component->GetName().StartsWith(TEXT("PreviewPlacement"));
	});

	TestEqual(TEXT("Preview actor rebuilds one occupied cell from the leaf module when OccupiedLocalCells are absent"), PreviewPlacementCount, 1);
	PreviewActor->Destroy();
	return true;
}

bool FLayoutPreviewActorDrawsCompositeInterfaceMarkersTest::RunTest(const FString& Parameters)
{
	UObject* const Outer = CreatePreviewActorTestOuter(TEXT("LayoutPreviewActorCompositeInterfaces"));
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(Outer);
	if (Harness.World == nullptr)
	{
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreatePreviewCompositeInterfaceModule(Outer, Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Preview composite interface fixture validates before solving"), CompositeValidation.IsValid()))
	{
		return false;
	}

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("PreviewCompositeInterfaceEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_PreviewCompositeInterfaces"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_PreviewCompositeInterfaces"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		1,
		false);
	Profile->ContentSet = ContentSet;

	ALayoutPreviewActor* const PreviewActor = SpawnPreviewActor(*this);
	if (PreviewActor == nullptr)
	{
		return false;
	}

	PreviewActor->LayoutProfile = Profile;
	PreviewActor->Seed = 1777;
	PreviewActor->bDrawRootFootprintBounds = false;
	PreviewActor->bDrawPlannedCells = false;
	PreviewActor->bDrawPlacements = false;
	PreviewActor->bDrawChildRegionBounds = false;
	PreviewActor->bDrawExportedEntryCells = true;
	PreviewActor->bDrawExportedBoundaryPoints = true;
	PreviewActor->bDrawClosureSegments = false;
	PreviewActor->bDrawClosureRuns = false;
	PreviewActor->bDrawPartitionSeams = false;
	PreviewActor->bDrawResidualCells = false;
	PreviewActor->bDrawSparsePlacements = false;

	if (!TestTrue(TEXT("Preview actor solves the simple composite interface profile"), PreviewActor->RebuildPreview()))
	{
		PreviewActor->Destroy();
		return false;
	}

	TestEqual(TEXT("Preview actor exports one entry cell for the composite interface fixture"), PreviewActor->PreviewResult.ExportedEntryCells.Num(), 1);
	PreviewActor->PostEditChange();

	TArray<USphereComponent*> EntrySpheres;
	PreviewActor->GetComponents<USphereComponent>(EntrySpheres);
	const int32 PreviewEntryCount = Algo::CountIf(EntrySpheres, [](const USphereComponent* Component)
	{
		return Component != nullptr && Component->GetName().StartsWith(TEXT("PreviewEntryCell"));
	});

	TArray<UArrowComponent*> BoundaryArrows;
	PreviewActor->GetComponents<UArrowComponent>(BoundaryArrows);
	const int32 PreviewBoundaryCount = Algo::CountIf(BoundaryArrows, [](const UArrowComponent* Component)
	{
		return Component != nullptr && Component->GetName().StartsWith(TEXT("PreviewBoundaryPoint"));
	});
	bool bSawShadowCellBoundaryPoint = false;
	for (const UArrowComponent* Component : BoundaryArrows)
	{
		if (Component != nullptr
			&& Component->GetName().StartsWith(TEXT("PreviewBoundaryPoint"))
			&& Component->GetRelativeLocation().X > 0.0)
		{
			bSawShadowCellBoundaryPoint = true;
			break;
		}
	}

	TestEqual(TEXT("Preview actor draws one exported entry marker for the composite fixture"), PreviewEntryCount, 1);
	TestTrue(TEXT("Preview actor draws exported boundary markers for the composite fixture"), PreviewBoundaryCount > 0);
	TestTrue(TEXT("Preview actor boundary markers include at least one shadow-cell marker from the composite bundle"), bSawShadowCellBoundaryPoint);

	PreviewActor->Destroy();
	return true;
}
