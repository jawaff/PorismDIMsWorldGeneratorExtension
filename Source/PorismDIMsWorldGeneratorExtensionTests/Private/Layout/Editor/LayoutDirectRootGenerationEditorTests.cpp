// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "ChunkWorld/Actors/ChunkWorldExtended.h"
#include "EditorModeManager.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutModuleAsset.h"
#include "Layout/Assets/LayoutProfileAsset.h"
#include "Layout/Assets/LayoutRegionContentSetAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Editor/LayoutDirectRootGenerationController.h"
#include "Layout/Editor/LayoutDirectRootGenerationEdMode.h"
#include "Layout/Editor/LayoutDirectRootGenerationSettings.h"
#include "Layout/Editor/LayoutDirectRootGenerationToolkit.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Support/LayoutTestUtilities.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Terrain/LayoutWorldBindingTerrainFit.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Modules/ModuleManager.h"
#include "Misc/AutomationTest.h"
#include "PorismDIMsWorldGeneratorExtensionEditor.h"
#include "PrimitiveDrawInterface.h"
#include "SceneView.h"
#include "Math/PerspectiveMatrix.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;

namespace
{
	const FEditorModeID DirectRootLayoutGenerationModeId(TEXT("EM_PorismLayoutDirectRootGeneration"));
	const FLayoutId DirectRootPlacementPolicyId(TEXT("DirectRootExplicit"));
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");

	void ConfigureFlatActiveBiomeSurface(UWorldGenDef* const WorldGenDef, const FName RowName)
	{
		if (WorldGenDef == nullptr)
		{
			return;
		}

		WorldGenDef->WorldBiomes.Reset();
		FBiomeDualData& Row = WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
		Row.BiomeName = RowName.ToString();
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenA = ConstantPositiveFastNoise;
	}

	class FRecordingPrimitiveDrawInterface final : public FPrimitiveDrawInterface
	{
	public:
		int32 LineCount = 0;
		int32 PointCount = 0;
		int32 MeshCount = 0;

		FRecordingPrimitiveDrawInterface()
			: FPrimitiveDrawInterface(nullptr)
		{
		}

		virtual bool IsHitTesting() override
		{
			return false;
		}

		virtual void SetHitProxy(HHitProxy* HitProxy) override
		{
		}

		virtual void RegisterDynamicResource(FDynamicPrimitiveResource* DynamicResource) override
		{
		}

		virtual void AddReserveLines(uint8 DepthPriorityGroup, int32 NumLines, bool bDepthBiased = false, bool bThickLines = false) override
		{
		}

		virtual void DrawSprite(
			const FVector& Position,
			float SizeX,
			float SizeY,
			const FTexture* Sprite,
			const FLinearColor& Color,
			uint8 DepthPriorityGroup,
			float U,
			float UL,
			float V,
			float VL,
			uint8 BlendMode = 1,
			float OpacityMaskRefVal = .5f) override
		{
		}

		virtual void DrawLine(
			const FVector& Start,
			const FVector& End,
			const FLinearColor& Color,
			uint8 DepthPriorityGroup,
			float Thickness = 0.0f,
			float DepthBias = 0.0f,
			bool bScreenSpace = false) override
		{
			++LineCount;
		}

		virtual void DrawTranslucentLine(
			const FVector& Start,
			const FVector& End,
			const FLinearColor& Color,
			uint8 DepthPriorityGroup,
			float Thickness = 0.0f,
			float DepthBias = 0.0f,
			bool bScreenSpace = false) override
		{
			++LineCount;
		}

		virtual void DrawPoint(
			const FVector& Position,
			const FLinearColor& Color,
			float PointSize,
			uint8 DepthPriorityGroup) override
		{
			++PointCount;
		}

		virtual int32 DrawMesh(const FMeshBatch& Mesh) override
		{
			++MeshCount;
			return 1;
		}
	};

	FLayoutRootSolveBudgetSettings MakeAutomationExplicitRootSolveBudget(
		const float MaxSolveDurationSeconds = FLayoutRootSolveBudgetSettings().MaxSolveDurationSeconds)
	{
		FLayoutRootSolveBudgetSettings SolveBudget;
		SolveBudget.MaxSolveDurationSeconds = MaxSolveDurationSeconds;
		return SolveBudget;
	}

	FLayoutWorldBindingPlacementPolicy MakeAutomationExplicitRootPlacementPolicy()
	{
		return FLayoutWorldBindingPlacementPolicy();
	}

	FLayoutNoiseCoordinateSettings BuildNoiseCoordinateSettings(
		const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}

	bool PumpLayoutRuntimeUntil(
		UChunkWorldLayoutRuntimeComponent* const RuntimeComponent,
		TFunctionRef<bool()> Predicate,
		const int32 MaxPumpCount = 400)
	{
		if (RuntimeComponent == nullptr)
		{
			return false;
		}

		for (int32 PumpIndex = 0; PumpIndex < MaxPumpCount; ++PumpIndex)
		{
			RuntimeComponent->PumpBackgroundLayoutSolves();
			if (Predicate())
			{
				return true;
			}
		}

		RuntimeComponent->PumpBackgroundLayoutSolves();
		return Predicate();
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

	ULayoutModuleAsset* CreateSingleCellEditorRuntimeVerticalAccessModule(
		UObject* Outer,
		const TCHAR* BaseName,
		const FIntVector& TemplateSize = FIntVector(16, 16, 16))
	{
		UChunkStructureTemplate* Template = CreateTemplate(
			Outer,
			*FString::Printf(TEXT("%s_Template"), BaseName),
			TemplateSize);
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

	ULayoutModuleAsset* CreateSingleCellEditorRuntimeJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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

	ULayoutModuleAsset* CreateSingleCellEditorRuntimeStructuralModule(UObject* Outer, const TCHAR* BaseName)
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
			TEXT("LayoutModule_DirectRootParent_Editor"),
			ParentMaterial,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		OutParentModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};

		OutChildModule = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_DirectRootChild_Editor"),
			ChildMaterial,
			{ELayoutCellIntent::Boundary});
		OutChildModule->Roles = {ELayoutModuleRole::Boundary};

		// Child module is always placed on a boundary cell inside a 1x1 replacement
		// volume. All faces face incoming parent boundary points that require
		// boundary-facing. Mark every face accordingly.
		OutChildModule->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		OutChildModule->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		FLayoutRegionContentEntry ChildModuleEntry;
		ChildModuleEntry.EntryId = TEXT("DirectRootChildShell");
		ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildModuleEntry.ModuleSettings.Module = OutChildModule;
		ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootChild_Editor"),
			{ChildModuleEntry});

		OutChildProfile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_DirectRootChild_Editor"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
		OutChildProfile->ContentSet = ChildContentSet;

		FLayoutRegionContentEntry ParentModuleEntry;
		ParentModuleEntry.EntryId = TEXT("DirectRootParentShell");
		ParentModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ParentModuleEntry.ModuleSettings.Module = OutParentModule;

		FLayoutRegionContentEntry ChildRegionEntry;
		ChildRegionEntry.EntryId = TEXT("DirectRootChildRoom");
		ChildRegionEntry.ContentKind = ELayoutRegionContentKind::ChildRegion;
		ChildRegionEntry.ChildRegionSettings.RegionProfile = OutChildProfile;
		ChildRegionEntry.ChildRegionSettings.PlacementZone = ELayoutPlacementZone::Interior;

		ULayoutRegionContentSetAsset* ParentContentSet = CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootParentChild_Editor"),
			{ParentModuleEntry, ChildRegionEntry});
		return ParentContentSet;
	}

	ULayoutRegionContentSetAsset* CreateExplicitRootSingleLeafContentSet(
		UObject* Outer,
		AChunkWorldExtended* World,
		ULayoutModuleAsset*& OutModule,
		const int32 MaterialIndex)
	{
		OutModule = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_DirectRootSingleLeaf_Editor"),
			MaterialIndex,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		OutModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};

		FLayoutRegionContentEntry ModuleEntry;
		ModuleEntry.EntryId = TEXT("DirectRootSingleLeaf");
		ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ModuleEntry.ModuleSettings.Module = OutModule;

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootSingleLeaf_Editor"),
			{ModuleEntry});
	}

	ULayoutProfileAsset* CreateExplicitRootParentProfile(UObject* Outer)
	{
		return CreateProfile(
			Outer,
			TEXT("LayoutProfile_DirectRootParent_Editor"),
			FIntPoint(3, 3),
			FIntPoint(3, 3),
			1,
			0,
			false);
	}

	ULayoutProfileAsset* CreateExplicitRootSingleLeafProfile(UObject* Outer)
	{
		return CreateProfile(
			Outer,
			TEXT("LayoutProfile_DirectRootSingleLeaf_Editor"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
	}

	ULayoutCompositeModuleAsset* CreateDirectRootCompositeModule(
		UObject* Outer,
		AChunkWorldExtended* World)
	{
		ULayoutModuleAsset* FirstLeaf = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_DirectRootCompositeFirstLeaf"),
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
			TEXT("LayoutModule_DirectRootCompositeSecondLeaf"),
			SinfullMaterial + 9,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior});
		SecondLeaf->Roles = {ELayoutModuleRole::Boundary};
		SecondLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, TEXT("LayoutComposite_DirectRootPair"));
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

	int32 NormalizeDirectRootTestYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	FIntVector RotateDirectRootTestPlacementCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		switch (NormalizeDirectRootTestYawRotationSteps(YawRotationSteps))
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

	TArray<FIntVector> SortDirectRootPlacementCells(TArray<FIntVector> Cells)
	{
		Cells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			if (Left.Z != Right.Z)
			{
				return Left.Z < Right.Z;
			}
			if (Left.Y != Right.Y)
			{
				return Left.Y < Right.Y;
			}
			return Left.X < Right.X;
		});
		return Cells;
	}

	float ResolveAutomationExplicitRootSolveBudgetSeconds()
	{
		return MakeAutomationExplicitRootSolveBudget().MaxSolveDurationSeconds;
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

	/** Verifies that adapter-expanded planned cells retain their source terrain support evidence. */
	bool ExpectPropagatedSteppedSupportMap(
		FAutomationTestBase& Test,
		const FLayoutSteppedTerrainSupportMap& SourceSupportMap,
		const FLayoutSteppedTerrainSupportMap& PropagatedSupportMap,
		const TArray<FLayoutPlannedCell>& PlannedCells,
		const TCHAR* const Context)
	{
		bool bPassed = true;
		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s preserves the shared cell height"), Context),
			PropagatedSupportMap.SharedCellHeightInBlocks,
			SourceSupportMap.SharedCellHeightInBlocks);
		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s publishes one support sample per planned cell"), Context),
			PropagatedSupportMap.SupportSamples.Num(),
			PlannedCells.Num());

		for (const FLayoutSteppedTerrainSupportSample& SourceSample : SourceSupportMap.SupportSamples)
		{
			bPassed &= Test.TestTrue(
				FString::Printf(TEXT("%s preserves source support for %s"), Context, *SourceSample.LocalCell.ToString()),
				PropagatedSupportMap.SupportSamples.ContainsByPredicate([&SourceSample](const FLayoutSteppedTerrainSupportSample& Candidate)
				{
					return Candidate.LocalCell == SourceSample.LocalCell
						&& Candidate.SupportSurfaceZ == SourceSample.SupportSurfaceZ;
				}));
		}

		for (const FLayoutPlannedCell& PlannedCell : PlannedCells)
		{
			const FLayoutSteppedTerrainSupportSample* const SourceSupport =
				SourceSupportMap.SupportSamples.FindByPredicate([&PlannedCell](const FLayoutSteppedTerrainSupportSample& Candidate)
				{
					return Candidate.LocalCell.X == PlannedCell.Cell.X
						&& Candidate.LocalCell.Y == PlannedCell.Cell.Y;
				});
			bPassed &= Test.TestNotNull(
				FString::Printf(TEXT("%s has source support for planned cell %s"), Context, *PlannedCell.Cell.ToString()),
				SourceSupport);
			if (SourceSupport != nullptr)
			{
				bPassed &= Test.TestTrue(
					FString::Printf(TEXT("%s preserves source support at planned cell %s"), Context, *PlannedCell.Cell.ToString()),
					PropagatedSupportMap.SupportSamples.ContainsByPredicate([&PlannedCell, SourceSupport](const FLayoutSteppedTerrainSupportSample& Candidate)
					{
						return Candidate.LocalCell == PlannedCell.Cell
							&& Candidate.SupportSurfaceZ == SourceSupport->SupportSurfaceZ
							&& Candidate.SnappedSupportFloorZ == SourceSupport->SnappedSupportFloorZ;
					}));
			}
		}

		return bPassed;
	}

	bool ExpectEquivalentSteppedSupportMap(
		FAutomationTestBase& Test,
		const FLayoutSteppedTerrainSupportMap& ExpectedSupportMap,
		const FLayoutSteppedTerrainSupportMap& ActualSupportMap,
		const TCHAR* const Context)
	{
		bool bPassed = true;

		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped shared cell height"), Context),
			ActualSupportMap.SharedCellHeightInBlocks,
			ExpectedSupportMap.SharedCellHeightInBlocks);
		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped maximum neighbor delta"), Context),
			ActualSupportMap.MaximumObservedNeighborHeightDelta,
			ExpectedSupportMap.MaximumObservedNeighborHeightDelta);
		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped support-sample count"), Context),
			ActualSupportMap.SupportSamples.Num(),
			ExpectedSupportMap.SupportSamples.Num());
		for (int32 SampleIndex = 0; SampleIndex < FMath::Min(ActualSupportMap.SupportSamples.Num(), ExpectedSupportMap.SupportSamples.Num()); ++SampleIndex)
		{
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps support sample %d local cell"), Context, SampleIndex),
				ActualSupportMap.SupportSamples[SampleIndex].LocalCell,
				ExpectedSupportMap.SupportSamples[SampleIndex].LocalCell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps support sample %d surface Z"), Context, SampleIndex),
				ActualSupportMap.SupportSamples[SampleIndex].SupportSurfaceZ,
				ExpectedSupportMap.SupportSamples[SampleIndex].SupportSurfaceZ);
		}

		bPassed &= Test.TestEqual(
			FString::Printf(TEXT("%s keeps the same stepped adjacency-step count"), Context),
			ActualSupportMap.AdjacencySteps.Num(),
			ExpectedSupportMap.AdjacencySteps.Num());
		for (int32 StepIndex = 0; StepIndex < FMath::Min(ActualSupportMap.AdjacencySteps.Num(), ExpectedSupportMap.AdjacencySteps.Num()); ++StepIndex)
		{
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps adjacency step %d from-cell"), Context, StepIndex),
				ActualSupportMap.AdjacencySteps[StepIndex].FromCell,
				ExpectedSupportMap.AdjacencySteps[StepIndex].FromCell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps adjacency step %d to-cell"), Context, StepIndex),
				ActualSupportMap.AdjacencySteps[StepIndex].ToCell,
				ExpectedSupportMap.AdjacencySteps[StepIndex].ToCell);
			bPassed &= Test.TestEqual(
				FString::Printf(TEXT("%s keeps adjacency step %d height"), Context, StepIndex),
				ActualSupportMap.AdjacencySteps[StepIndex].StepHeightBlocks,
				ExpectedSupportMap.AdjacencySteps[StepIndex].StepHeightBlocks);
		}

		return bPassed;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationToolRegistersArtifactsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.RegistersArtifacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationToolRegistersArtifactsTest::RunTest(const FString& Parameters)
{
	(void)FModuleManager::LoadModuleChecked<IModuleInterface>("PorismDIMsWorldGeneratorExtensionEditor");
	if (GLevelEditorModeTools().IsModeActive(DirectRootLayoutGenerationModeId))
	{
		GLevelEditorModeTools().DeactivateMode(DirectRootLayoutGenerationModeId);
	}

	GLevelEditorModeTools().ActivateMode(DirectRootLayoutGenerationModeId);
	FEdMode* const ActiveMode = GLevelEditorModeTools().GetActiveMode(DirectRootLayoutGenerationModeId);
	TestNotNull(TEXT("Direct root generation mode can be activated"), ActiveMode);
	TestTrue(TEXT("Direct root generation mode now exposes a docked toolkit"), ActiveMode != nullptr && ActiveMode->GetToolkit().IsValid());
	GLevelEditorModeTools().DeactivateMode(DirectRootLayoutGenerationModeId);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationTerrainSeamDebugInfoTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.TerrainSeamDebugInfo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationChildEntryUsesCommittedFaceTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ChildEntryUsesCommittedFace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationToolTabActivatesModeTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.TabActivatesMode",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationContinuationPreviewAlignmentTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ContinuationPreviewAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationTerrainSeamDebugInfoTest::RunTest(const FString& Parameters)
{
	TArray<FLayoutPlannedCell> PlannedCells;
	for (int32 Y = 0; Y < 3; ++Y)
	{
		for (int32 X = 0; X < 3; ++X)
		{
			FLayoutPlannedCell& Cell = PlannedCells.AddDefaulted_GetRef();
			Cell.Cell = FIntVector(X, Y, 0);
			Cell.Intent = X == 0 || X == 2 || Y == 0 || Y == 2
				? ELayoutCellIntent::Boundary
				: ELayoutCellIntent::Interior;
			if (Cell.Cell == FIntVector(1, 1, 0))
			{
				Cell.PlacementZone = ELayoutPlacementZone::Edge;
				Cell.TerrainSeamFaceMask = LayoutFaceDirectionMask(ELayoutFaceDirection::PosX);
			}
		}
	}

	TSet<FIntVector> PreviewCells;
	for (const FLayoutPlannedCell& Cell : PlannedCells)
	{
		PreviewCells.Add(Cell.Cell);
	}

	const FLayoutCellDebugInfo DebugInfo =
		FLayoutDirectRootGenerationController::BuildCellDebugInfo(
			PlannedCells[4],
			PlannedCells,
			PreviewCells);
	TestEqual(TEXT("Terrain seam base topology remains Interior"), DebugInfo.BaseZone, ELayoutPlacementZone::Interior);
	TestEqual(TEXT("Terrain seam effective zone remains Edge"), DebugInfo.EffectiveZone, ELayoutPlacementZone::Edge);
	TestTrue(TEXT("Terrain seam positive-X marker survives debug assembly"),
		LayoutFaceMaskContainsDirection(DebugInfo.TerrainSeamFaceMask, ELayoutFaceDirection::PosX));

	TArray<FLayoutPlannedCell> ChildCells;
	FLayoutPlannedCell& ChildCell = ChildCells.AddDefaulted_GetRef();
	ChildCell.Cell = FIntVector::ZeroValue;
	ChildCell.Intent = ELayoutCellIntent::Interior;
	ChildCell.PlacementZone = ELayoutPlacementZone::Interior;
	TSet<FIntVector> ExternalCells = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0)
	};
	const FLayoutCellDebugInfo ChildDebugInfo =
		FLayoutDirectRootGenerationController::BuildCellDebugInfo(
			ChildCell,
			ChildCells,
			ExternalCells);
	TestEqual(TEXT("Adjacent solved regions preserve child base Interior zone"),
		ChildDebugInfo.BaseZone,
		ELayoutPlacementZone::Interior);

	const FString OverlayLegend =
		FLayoutDirectRootGenerationToolkit::GetOverlayLegendTextForTesting().ToString();
	TestTrue(TEXT("Overlay legend describes base and effective zone borders"),
		OverlayLegend.Contains(TEXT("Outer wire cube"))
			&& OverlayLegend.Contains(TEXT("Inner wire cube")));
	TestTrue(TEXT("Overlay legend describes terrain seam, outer boundary, and unoccupied markers"),
		OverlayLegend.Contains(TEXT("Teal face square"))
			&& OverlayLegend.Contains(TEXT("Bright yellow pyramid"))
			&& OverlayLegend.Contains(TEXT("Red diagonal X")));
	TestTrue(TEXT("Overlay legend describes entry and vertical-access markers"),
		OverlayLegend.Contains(TEXT("entry cell"))
			&& OverlayLegend.Contains(TEXT("vertical access cell")));
	return true;
}

bool FLayoutDirectRootGenerationChildEntryUsesCommittedFaceTest::RunTest(const FString& Parameters)
{
	FLayoutPlannedCell EntryCell;
	EntryCell.Cell = FIntVector(1, 1, 0);
	EntryCell.Intent = ELayoutCellIntent::Entry;
	EntryCell.PlacementZone = ELayoutPlacementZone::Edge;
	const TArray<FLayoutPlannedCell> ChildCells = {EntryCell};
	const TSet<FIntVector> ParentCells = {
		FIntVector(0, 1, 0), FIntVector(2, 1, 0),
		FIntVector(1, 0, 0), FIntVector(1, 2, 0)};
	FLayoutCommittedEndpointAnchor Anchor;
	Anchor.LocalCell = EntryCell.Cell;
	Anchor.FaceDirection = ELayoutFaceDirection::NegX;
	Anchor.TraversalChannels.AddTag(LayoutGameplayTags::TraversalPrimary);
	const TArray<FLayoutCommittedEndpointAnchor> Anchors = {Anchor};

	const FLayoutCellDebugInfo DebugInfo =
		FLayoutDirectRootGenerationController::BuildCellDebugInfo(
			EntryCell,
			ChildCells,
			ParentCells,
			FIntVector(5, 4, 0),
			&Anchors);
	TestTrue(TEXT("Child Entry debug record retains planned Entry intent"), DebugInfo.bIsPlannedEntryCell);
	TestEqual(TEXT("Child Entry debug record uses one committed face"), DebugInfo.EntryFaces.Num(), 1);
	return TestTrue(
		TEXT("Child Entry debug record preserves exact committed face despite surrounding parent cells"),
		DebugInfo.EntryFaces.Contains(ELayoutFaceDirection::NegX));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLayoutContinuationSourceSummaryTest,
	"PorismExtension.Layout.Editor.ContinuationSourceSummary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutContinuationSourceSummaryTest::RunTest(const FString& Parameters)
{
	FLayoutDirectRootGenerationController Controller;
	auto* Settings = Controller.GetSettingsObject();
	Settings->LayoutWorldBinding = NewObject<ULayoutWorldBindingAsset>();
	Settings->LayoutProfile = NewObject<ULayoutProfileAsset>();
	auto& Family = Settings->LayoutWorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.Candidates.AddDefaulted_GetRef().LayoutProfile = Settings->LayoutProfile;
	const FString Summary = Controller.GetGenerationSummaryText().ToString();
	TestTrue(TEXT("Continuation source selection reports continuation mode"), Summary.Contains(TEXT("Mode: Continuation")));
	TestFalse(TEXT("Continuation selection does not request a root solve"), Summary.Contains(TEXT("No explicit-root")));
	return true;
}

bool FLayoutDirectRootGenerationContinuationPreviewAlignmentTest::RunTest(const FString& Parameters)
{
	FResolvedLayoutConnectorRecord ConnectorRecord;
	ConnectorRecord.ResolvedContinuationSelection.ResolvedEntryLevel = 1;

	FLayoutSolveResult PreflightRejectedResult;
	TestEqual(
		TEXT("Preflight-rejected continuation markers retain selected entry alignment"),
		FLayoutDirectRootGenerationController::ResolveContinuationPreviewAlignmentLevelForTesting(
			PreflightRejectedResult,
			ConnectorRecord),
		1);

	FLayoutSolveResult SolverResult;
	SolverResult.ResolvedTerrainAlignmentLevel = 2;
	TestEqual(
		TEXT("Solver-produced alignment overrides preserved continuation selection"),
		FLayoutDirectRootGenerationController::ResolveContinuationPreviewAlignmentLevelForTesting(
			SolverResult,
			ConnectorRecord),
		2);
	return true;
}

bool FLayoutDirectRootGenerationToolTabActivatesModeTest::RunTest(const FString& Parameters)
{
	(void)FModuleManager::LoadModuleChecked<IModuleInterface>("PorismDIMsWorldGeneratorExtensionEditor");

	if (GLevelEditorModeTools().IsModeActive(DirectRootLayoutGenerationModeId))
	{
		GLevelEditorModeTools().DeactivateMode(DirectRootLayoutGenerationModeId);
	}

	GLevelEditorModeTools().ActivateMode(DirectRootLayoutGenerationModeId);
	TestTrue(TEXT("Direct root generation mode activates through the shared editor-mode dropdown path"), GLevelEditorModeTools().IsModeActive(DirectRootLayoutGenerationModeId));
	GLevelEditorModeTools().DeactivateMode(DirectRootLayoutGenerationModeId);
	TestFalse(TEXT("Direct root generation mode cleanly deactivates"), GLevelEditorModeTools().IsModeActive(DirectRootLayoutGenerationModeId));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationControllerPreviewApplyWorkflowTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ControllerPreviewApplyWorkflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationApplyUsesCachedPreviewWorldTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ApplyUsesCachedPreviewWorld",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationApplyRehydratesLeafPlacementCarrierTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ApplyRehydratesLeafPlacementCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationControllerCompositePreviewApplyWorkflowTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ControllerCompositePreviewApplyWorkflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationControllerPublishesAnyActiveSteppedSupportTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ControllerPublishesAnyActiveSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationControllerPublishesThreeColumnAnyActiveSteppedSupportTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ControllerPublishesThreeColumnAnyActiveSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationControllerPublishesMultiTransitionAnyActiveSteppedSupportTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ControllerPublishesMultiTransitionAnyActiveSteppedSupport",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationControllerCompositePreviewApplyWithoutBundleBoundsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ControllerCompositePreviewApplyWorkflowWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationLeafPlacementSizeUsesEffectiveTemplateDimensionsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.LeafPlacementSizeUsesEffectiveTemplateDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationBindingAwarePreviewSnapsHoveredSiteToNearestLatticePlaneTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.BindingAwarePreviewSnapsHoveredSiteToNearestLatticePlane",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationSurfaceUndergroundEnvironmentMatrixTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.SurfaceUndergroundEnvironmentMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationBindingAwarePreviewUsesAnyActiveBiomeSurfaceTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.BindingAwarePreviewUsesAnyActiveBiomeSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationBindingAwareContinuationPreviewUsesAnyActiveBiomeSurfaceTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.BindingAwareContinuationPreviewUsesAnyActiveBiomeSurface",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationBindingAwarePreviewUsesWorldBindingAuthorityOverStandaloneSynthesisTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.BindingAwarePreviewUsesWorldBindingAuthorityOverStandaloneSynthesis",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationSupersededPreviewPublishesLatestOnlyTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.SupersededPreviewPublishesLatestOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationLeafPlacementSizeRejectsInvalidTemplateDimensionsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.LeafPlacementSizeRejectsInvalidTemplateDimensions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationPlacementCellsUseOccupiedLocalCellsBeforeModuleBoundsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.PlacementCellsUseOccupiedLocalCellsBeforeModuleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationPlacementCellsRebuildFromLeafModuleWithoutOccupiedLocalCellsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.PlacementCellsRebuildFromLeafModuleWithoutOccupiedLocalCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationControllerPreviewApplyWorkflowTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutProfileAsset* ChildProfile = nullptr;
	ULayoutModuleAsset* ParentModule = nullptr;
	ULayoutModuleAsset* ChildModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootParentChildContentSet(
		GetTransientPackage(),
		Harness.World,
		ChildProfile,
		ParentModule,
		ChildModule,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* ParentProfile = CreateExplicitRootParentProfile(GetTransientPackage());
	ParentProfile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = ParentProfile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2711;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	TestTrue(TEXT("Controller preview succeeds against the injected hovered chunk-world location"), Controller.PreviewSolveAtHoveredLocation());
	TestTrue(TEXT("Controller caches a solved explicit-root site after a successful preview"), Controller.GetCachedSiteRecordForTesting().bLayoutSolved);
	TestTrue(TEXT("Controller preview caches recursive child-region results"), Controller.GetCachedScheduleResultForTesting().RegionResults.Num() > 1);
	const FLayoutWorldBindingPlacementPolicy ExpectedPlacementPolicy =
		MakeAutomationExplicitRootPlacementPolicy();
	const FLayoutRegionSolveScheduleResult& CachedScheduleResult = Controller.GetCachedScheduleResultForTesting();
	TestTrue(TEXT("Controller preview keeps the merged recursive solve successful"), CachedScheduleResult.MergedSolveResult.bSucceeded);
	TestEqual(TEXT("Controller preview retains the parent and child region results"), CachedScheduleResult.RegionResults.Num(), 2);
	TestEqual(TEXT("Controller preview merges every parent and child placement"), CachedScheduleResult.MergedSolveResult.Placements.Num(), 10);
	TestEqual(TEXT("Controller preview keeps the configured parent footprint"), Controller.GetCachedSiteRecordForTesting().SolveResult.FootprintSize, FIntPoint(3, 3));
	TestEqual(TEXT("Controller cached schedule result keeps the explicit placement kind"), CachedScheduleResult.MergedSolveResult.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Controller cached schedule result keeps the explicit terrain sample spacing"), CachedScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, ExpectedPlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(TEXT("Controller cached schedule result keeps the explicit height ignore threshold"), CachedScheduleResult.MergedSolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold, ExpectedPlacementPolicy.HeightIgnoreThreshold);
	TestEqual(TEXT("Controller cached site record keeps the explicit placement kind"), Controller.GetCachedSiteRecordForTesting().SolveResult.RootPlacementKind, ELayoutWorldBindingPlacementKind::OrdinaryRoot);
	TestEqual(TEXT("Controller cached site record keeps the explicit terrain sample spacing"), Controller.GetCachedSiteRecordForTesting().SolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing, ExpectedPlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(TEXT("Controller cached site record keeps the explicit height ignore threshold"), Controller.GetCachedSiteRecordForTesting().SolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold, ExpectedPlacementPolicy.HeightIgnoreThreshold);
	TestEqual(
		TEXT("Controller preview keeps the direct-root placement policy id"),
		Controller.GetCachedSiteRecordForTesting().GetRootPublicationMetadata().RootPlacementPolicyId,
		DirectRootPlacementPolicyId);
	TestTrue(
		TEXT("Controller diagnostics summarize the accepted preview attempt"),
		Controller.GetStatusText().ToString().Contains(TEXT("Accepted")));
	TestTrue(
		TEXT("Generation summary centers the current explicit-root attempt instead of only generic diagnostics"),
		Controller.GetGenerationSummaryText().ToString().Contains(TEXT("Generation Attempt")));
	TestTrue(
		TEXT("Generation summary reports that the cached solve is ready to apply"),
		Controller.GetGenerationSummaryText().ToString().Contains(TEXT("ready to apply")));
	TestTrue(
		TEXT("Generation summary reports the solve timing for the current attempt"),
		Controller.GetGenerationSummaryText().ToString().Contains(TEXT("Timing: total=")));
	TestTrue(
		TEXT("Generation summary drops the old cached preview site-center line"),
		!Controller.GetGenerationSummaryText().ToString().Contains(TEXT("Site Center:")));

	TestTrue(TEXT("Controller apply succeeds once for the cached solve"), Controller.ApplyCachedSolve());
	TestTrue(TEXT("Controller apply replaces the same cached solve on a second commit attempt"), Controller.ApplyCachedSolve());
	TestTrue(
		TEXT("Generation summary reports that the explicit-root attempt has been committed after apply"),
		Controller.GetGenerationSummaryText().ToString().Contains(TEXT("committed to the chunk world")));

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationApplyUsesCachedPreviewWorldTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutProfileAsset* ChildProfile = nullptr;
	ULayoutModuleAsset* ParentModule = nullptr;
	ULayoutModuleAsset* ChildModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootParentChildContentSet(
		GetTransientPackage(),
		Harness.World,
		ChildProfile,
		ParentModule,
		ChildModule,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* ParentProfile = CreateExplicitRootParentProfile(GetTransientPackage());
	ParentProfile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = ParentProfile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2721;
	Settings->bDrawHoverSquare = true;
	Settings->bDrawSolvedFootprint = true;
	Settings->bDrawCellZoneMarkers = true;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds before cached-world apply coverage"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	Controller.SetHoveredLocationForTesting(nullptr, FIntVector::ZeroValue);

	FLayoutDirectRootGenerationOverlaySummary Summary = Controller.BuildOverlaySummary();
	TestFalse(TEXT("Clearing hover removes the live hover indicator"), Summary.bHasHoveredBlock);
	TestTrue(TEXT("Clearing hover keeps the cached solved preview available"), Summary.bHasSolvedPreview);

	FRecordingPrimitiveDrawInterface Recorder;
	Controller.Render(nullptr, &Recorder);
	TestTrue(TEXT("Cached solved overlay still renders after hover is cleared"), Recorder.LineCount > 0);

	TestTrue(TEXT("Controller apply still succeeds after hover is cleared"), Controller.ApplyCachedSolve());
	TestTrue(TEXT("Controller apply still replaces the same cached solve after hover is cleared"), Controller.ApplyCachedSolve());

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Cleared-hover apply still commits one realized explicit-root site"),
		SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized && SiteRecords[0].bHasBeenCommittedToChunkWorld);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationApplyRehydratesLeafPlacementCarrierTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutModuleAsset* LeafModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootSingleLeafContentSet(
		GetTransientPackage(),
		Harness.World,
		LeafModule,
		SinfullMaterial);
	ULayoutProfileAsset* Profile = CreateExplicitRootSingleLeafProfile(GetTransientPackage());
	Profile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2722;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds before carrier rehydration coverage"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FResolvedLayoutSiteRecord& MutableCachedSiteRecord =
		const_cast<FResolvedLayoutSiteRecord&>(Controller.GetCachedSiteRecordForTesting());
	if (!TestTrue(
		TEXT("Controller cached solve preserves realized placements before carrier stripping"),
		MutableCachedSiteRecord.SolveResult.Placements.Num() == 1))
	{
		Controller.SetToolActive(false);
		return false;
	}

	for (FLayoutPlacedModule& Placement : MutableCachedSiteRecord.SolveResult.Placements)
	{
		Placement.Module = nullptr;
		Placement.CompositeModule = nullptr;
	}

	TestTrue(TEXT("Controller apply succeeds after cached leaf placement carriers are stripped"), Controller.ApplyCachedSolve());

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Carrier-rehydrated editor apply commits one realized explicit-root site"),
		SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized && SiteRecords[0].bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FIntVector SharedCellSizeInBlocks =
		SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.SharedCellSizeInBlocks;
	bool bVerifiedLeafStamp = false;
	for (const FLayoutPlacedModule& Placement : SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.Placements)
	{
		const FIntVector Anchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
			SiteRecords[0],
			Placement,
			SharedCellSizeInBlocks);
		const int32 MaterialAtAnchor = Harness.World->GetBlockValueByBlockWorldPos(
			Anchor,
			ERessourceType::MaterialIndex,
			0);
		if (Placement.Module == LeafModule)
		{
			bVerifiedLeafStamp = bVerifiedLeafStamp || MaterialAtAnchor == SinfullMaterial;
		}
	}

	TestTrue(TEXT("Carrier-rehydrated editor apply stamps the leaf placement template"), bVerifiedLeafStamp);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationControllerCompositePreviewApplyWorkflowTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreateDirectRootCompositeModule(Harness.World, Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Direct-root composite fixture validates before preview"), CompositeValidation.IsValid()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("DirectRootCompositePreviewApplyEntry");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Harness.World,
		TEXT("LayoutContentSet_DirectRootCompositePreviewApply"),
		{CompositeEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		Harness.World,
		TEXT("LayoutProfile_DirectRootCompositePreviewApply"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	Composite->AddToRoot();
	ContentSet->AddToRoot();
	Profile->AddToRoot();
	for (const FLayoutCompositeModuleCell& CompositeCell : Composite->Cells)
	{
		if (ULayoutModuleAsset* const LeafModule = CompositeCell.Module)
		{
			LeafModule->AddToRoot();
			if (UChunkStructureTemplate* const LeafTemplate = LeafModule->Template.LoadSynchronous())
			{
				LeafTemplate->AddToRoot();
			}
		}
	}

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2716;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds for the simple composite explicit-root profile"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	TestTrue(TEXT("Controller caches a solved composite explicit-root site"), CachedSiteRecord.bLayoutSolved);
	TestEqual(TEXT("Controller caches one solved composite placement bundle"), CachedSiteRecord.SolveResult.Placements.Num(), 1);
	if (!TestTrue(
		TEXT("Controller cached composite placement preserves occupied local cells"),
		CachedSiteRecord.SolveResult.Placements.Num() == 1
			&& CachedSiteRecord.SolveResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		Controller.SetToolActive(false);
		return false;
	}

	Harness.World->StopGen();
	TestFalse(TEXT("Composite cached solve stops the chunk world before apply"), Harness.World->IsRunning());

	const bool bAppliedCachedSolve = Controller.ApplyCachedSolve();
	if (!bAppliedCachedSolve)
	{
		AddInfo(Controller.GetStatusText().ToString());
	}
	TestTrue(TEXT("Controller apply succeeds for the cached composite solve"), bAppliedCachedSolve);
	TestTrue(TEXT("Controller apply restarts the stopped chunk world before stamping the cached composite solve"), Harness.World->IsRunning());
	TestTrue(TEXT("Controller apply replaces the same cached composite solve on a second commit attempt"), Controller.ApplyCachedSolve());

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Composite direct-root apply commits exactly one realized site"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized && SiteRecords[0].bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FLayoutPlacedModule& Placement = SiteRecords[0].SolveResult.Placements[0];
	const FIntVector SharedCellSizeInBlocks = SiteRecords[0].SolveResult.SharedCellSizeInBlocks;
	if (!TestNotEqual(TEXT("Composite direct-root apply preserves one realized shared-cell carrier"), SharedCellSizeInBlocks, FIntVector::ZeroValue))
	{
		Controller.SetToolActive(false);
		return false;
	}
	const FIntVector BundleAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
		SiteRecords[0],
		Placement,
		SharedCellSizeInBlocks);
	const FIntPoint RotationFootprint(2, 1);
	const FIntVector FirstLeafAnchor =
		BundleAnchor + RotateDirectRootTestPlacementCellInFootprintYaw(FIntVector(0, 0, 0), RotationFootprint, Placement.YawRotationSteps);
	const FIntVector SecondLeafAnchor =
		BundleAnchor + RotateDirectRootTestPlacementCellInFootprintYaw(FIntVector(1, 0, 0), RotationFootprint, Placement.YawRotationSteps);

	TestEqual(
		TEXT("Composite direct-root apply stamps the first glued leaf material"),
		Harness.World->GetBlockValueByBlockWorldPos(FirstLeafAnchor, ERessourceType::MaterialIndex, 0),
		SinfullMaterial);
	TestEqual(
		TEXT("Composite direct-root apply stamps the second glued leaf material"),
		Harness.World->GetBlockValueByBlockWorldPos(SecondLeafAnchor, ERessourceType::MaterialIndex, 0),
		SinfullMaterial + 9);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationControllerPublishesAnyActiveSteppedSupportTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Chunk-world harness creates a world generation definition"), Harness.World->WorldGenDef.Get()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	Harness.World->WorldGenDef->WorldBiomes.Reset();
	FBiomeDualData& Row = Harness.World->WorldGenDef->WorldBiomes.AddDefaulted_GetRef();
	Row.BiomeName = TEXT("Reservation");
	Row.Domain = ConstantPositiveFastNoise;
	Row.DualSwitch = ConstantPositiveFastNoise;
	Row.GenARun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutEditorAnyActiveSteppedSupportTemplate"),
		FIntVector(16, 16, 16));
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutEditorAnyActiveSteppedSupportModule"),
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
	Entry.EntryId = TEXT("EditorAnyActiveSteppedSupportEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_EditorAnyActiveSteppedSupport"),
		{Entry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_EditorAnyActiveSteppedSupport"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		MakeAutomationExplicitRootPlacementPolicy();

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2713;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(16, 8, 15));
	if (!TestTrue(TEXT("Controller preview succeeds for the any-active stepped-support fixture"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FLayoutRegionSolveScheduleResult& CachedScheduleResult = Controller.GetCachedScheduleResultForTesting();
	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	TestTrue(
		TEXT("Controller cached schedule result publishes support samples for any-active stepped support"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num() > 0);
	ExpectEquivalentSteppedSupportMap(
		*this,
		CachedSiteRecord.SolveResult.SteppedTerrainSupportMap,
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap,
		TEXT("Controller cached site record for any-active stepped support"));
	TestEqual(
		TEXT("Controller any-active stepped-support carrier keeps the shared cell height on the cached schedule result"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks,
		16);
	TestEqual(
		TEXT("Controller any-active stepped-support carrier keeps the shared cell height on the cached site record"),
		CachedSiteRecord.SolveResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks,
		16);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationControllerPublishesThreeColumnAnyActiveSteppedSupportTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Chunk-world harness creates a world generation definition"), Harness.World->WorldGenDef.Get()))
	{
		Controller.SetToolActive(false);
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

	UObject* Outer = GetTransientPackage();
	UChunkStructureTemplate* Template = CreateTemplate(
		Outer,
		TEXT("LayoutEditorThreeColumnAnyActiveSteppedSupportTemplate"),
		FIntVector(16, 16, 16));
	FLayoutTestWorldSupport::ConfigureSolidTemplate(
		Template,
		Harness.World,
		FIntVector(16, 16, 16),
		FIntVector::ZeroValue,
		SinfullMaterial);
	ULayoutModuleAsset* Module = CreateModule(
		Outer,
		TEXT("LayoutEditorThreeColumnAnyActiveSteppedSupportModule"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Interior},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary}),
			FGameplayTagContainer(),
			FGameplayTagContainer()));
	ULayoutModuleAsset* VerticalAccessModule = CreateModule(
		Outer,
		TEXT("LayoutEditorThreeColumnAnyActiveSteppedSupportVerticalAccessModule"),
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

	ULayoutModuleAsset* BoundaryModule = CreateModule(
		Outer,
		TEXT("LayoutEditorThreeColumnAnyActiveSteppedSupportBoundaryModule"),
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

	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("EditorThreeColumnAnyActiveSteppedSupportEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	FLayoutRegionContentEntry VerticalAccessEntry;
	VerticalAccessEntry.EntryId = TEXT("EditorThreeColumnAnyActiveSteppedSupportVerticalAccessEntry");
	VerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
	VerticalAccessEntry.ModuleSettings.Module = VerticalAccessModule;
	FLayoutRegionContentEntry BoundaryEntry;
	BoundaryEntry.EntryId = TEXT("EditorThreeColumnAnyActiveSteppedSupportBoundaryEntry");
	BoundaryEntry.ContentKind = ELayoutRegionContentKind::Module;
	BoundaryEntry.ModuleSettings.Module = BoundaryModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_EditorThreeColumnAnyActiveSteppedSupport"),
		{Entry, VerticalAccessEntry, BoundaryEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_EditorThreeColumnAnyActiveSteppedSupport"),
		FIntPoint(3, 1),
		FIntPoint(3, 1),
		2,
		1,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		MakeAutomationExplicitRootPlacementPolicy();

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2714;

	const FIntVector SiteCenterBlockWorldPos(24, 8, 16);
	const FIntVector HoveredBlockWorldPos = SiteCenterBlockWorldPos - FIntVector(0, 0, 1);
	Controller.SetHoveredLocationForTesting(Harness.World, HoveredBlockWorldPos);
	if (!TestTrue(TEXT("Controller preview succeeds for the three-column any-active stepped-support fixture"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FString RegionDebugPath =
		FString::Printf(TEXT("DirectRoot/%s"), *SiteCenterBlockWorldPos.ToString());
	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds =
		ResolveAutomationExplicitRootSolveBudgetSeconds();
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	if (!TestTrue(
			TEXT("Three-column controller stepped-support parity initializes the active biome sampler"),
			ActiveBiomeSampler.Initialize(
				GetTransientPackage(),
				Harness.World->WorldGenDef,
				Harness.World->Seed)))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionSolveRequest ExpectedRequest;
	FString RequestFailureReason;
	if (!TestTrue(
			TEXT("Three-column controller stepped-support parity rebuilds the same optional explicit-root request the live caller uses"),
			LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
				RuntimeView,
				NAME_None,
				SiteCenterBlockWorldPos,
				Settings->ManualSolveSeed,
				BuildNoiseCoordinateSettings(Harness.World->WorldGenDef),
				ActiveBiomeSampler,
				ExpectedRequest,
				RequestFailureReason,
				Harness.World)))
	{
		AddError(RequestFailureReason);
		Controller.SetToolActive(false);
		return false;
	}
	TestEqual(TEXT("Three-column controller parity request uses the direct-root region path as candidate id"), ExpectedRequest.RootCandidateId, FLayoutId(*RegionDebugPath));
	TestEqual(TEXT("Three-column controller parity request uses the direct-root region path as solve id"), ExpectedRequest.RootSolveId, FLayoutId(*RegionDebugPath));
	TestEqual(TEXT("Three-column controller parity request uses the direct-root placement policy id"), ExpectedRequest.RootPlacementPolicyId, DirectRootPlacementPolicyId);

	const FLayoutRegionSolveScheduleResult ExpectedScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(ExpectedRequest);

	const FLayoutRegionSolveScheduleResult& CachedScheduleResult =
		Controller.GetCachedScheduleResultForTesting();
	const FResolvedLayoutSiteRecord& CachedSiteRecord =
		Controller.GetCachedSiteRecordForTesting();
	TestTrue(
		TEXT("Controller cached schedule result publishes a wider stepped support surface for the three-column fixture"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num() > 2);
	TestTrue(
		TEXT("Controller cached schedule result publishes multiple stepped adjacencies for the three-column fixture"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.Num() > 1);
	ExpectPropagatedSteppedSupportMap(
		*this,
		ExpectedRequest.SteppedTerrainSupportMap,
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap,
		CachedScheduleResult.MergedSolveResult.PlannedCells,
		TEXT("Controller cached schedule result for the three-column any-active stepped fixture"));
	ExpectPropagatedSteppedSupportMap(
		*this,
		ExpectedRequest.SteppedTerrainSupportMap,
		CachedSiteRecord.SolveResult.SteppedTerrainSupportMap,
		CachedSiteRecord.SolveResult.PlannedCells,
		TEXT("Controller cached site record for the three-column any-active stepped fixture"));
	TestEqual(
		TEXT("Controller three-column any-active stepped fixture keeps the standalone forced-insertion count on the cached schedule result"),
		CachedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num(),
		ExpectedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num());
	TestEqual(
		TEXT("Controller three-column any-active stepped fixture keeps the standalone route-constraint count on the cached schedule result"),
		CachedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num(),
		ExpectedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Controller three-column any-active stepped fixture keeps the standalone vertical-access placement count on the cached schedule result"),
		CountVerticalAccessPlacements(CachedScheduleResult.MergedSolveResult),
		CountVerticalAccessPlacements(ExpectedScheduleResult.MergedSolveResult));
	TestEqual(
		TEXT("Controller three-column any-active stepped fixture keeps the standalone forced-insertion count on the cached site record"),
		CachedSiteRecord.SolveResult.ForcedPlacementBundleInsertions.Num(),
		ExpectedScheduleResult.MergedSolveResult.ForcedPlacementBundleInsertions.Num());
	TestEqual(
		TEXT("Controller three-column any-active stepped fixture keeps the standalone route-constraint count on the cached site record"),
		CachedSiteRecord.SolveResult.RequestOwnedRequiredRouteConstraints.Num(),
		ExpectedScheduleResult.MergedSolveResult.RequestOwnedRequiredRouteConstraints.Num());
	TestEqual(
		TEXT("Controller three-column any-active stepped fixture keeps the standalone vertical-access placement count on the cached site record"),
		CountVerticalAccessPlacements(CachedSiteRecord.SolveResult),
		CountVerticalAccessPlacements(ExpectedScheduleResult.MergedSolveResult));
	const bool bAppliedCachedSolve = Controller.ApplyCachedSolve();
	if (!bAppliedCachedSolve)
	{
		AddInfo(Controller.GetStatusText().ToString());
	}
	TestTrue(
		TEXT("Controller apply succeeds for the three-column any-active stepped fixture with route constraints"),
		bAppliedCachedSolve);
	const TArray<FResolvedLayoutSiteRecord> AppliedSiteRecords =
		Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	if (!TestTrue(
			TEXT("Controller three-column any-active stepped apply commits one realized site"),
			AppliedSiteRecords.Num() == 1
				&& AppliedSiteRecords[0].bLayoutRealized
				&& AppliedSiteRecords[0].bHasBeenCommittedToChunkWorld))
	{
		Controller.ClearCachedSolve();
		Controller.SetToolActive(false);
		return false;
	}
	const FResolvedLayoutSiteRecord& AppliedSiteRecord = AppliedSiteRecords[0];
	const FLayoutPlacedModule* const RepresentativePlacement =
		AppliedSiteRecord.SolveResult.Placements.FindByPredicate([](const FLayoutPlacedModule& Placement)
	{
		return Placement.Intent != ELayoutCellIntent::VerticalAccess;
	});
	if (!TestNotNull(
			TEXT("Controller three-column any-active stepped apply keeps one representative template placement"),
			RepresentativePlacement))
	{
		Controller.ClearCachedSolve();
		Controller.SetToolActive(false);
		return false;
	}
	const FIntVector RepresentativeAnchor =
		UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
			AppliedSiteRecord,
			*RepresentativePlacement,
			AppliedSiteRecord.SolveResult.SharedCellSizeInBlocks);
	TestEqual(
		TEXT("Controller three-column any-active stepped apply stamps the representative template material"),
		Harness.World->GetBlockValueByBlockWorldPos(
			RepresentativeAnchor,
			ERessourceType::MaterialIndex,
			0),
		SinfullMaterial);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationControllerPublishesMultiTransitionAnyActiveSteppedSupportTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Chunk-world harness creates a world generation definition"), Harness.World->WorldGenDef.Get()))
	{
		Controller.SetToolActive(false);
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

	UObject* Outer = GetTransientPackage();
	ULayoutModuleAsset* EdgeVerticalAccessModule = CreateSingleCellEditorRuntimeVerticalAccessModule(
		Outer,
		TEXT("LayoutEditorMultiTransitionEdgeVerticalAccess"));
	ULayoutModuleAsset* JunctionVerticalAccessModule = CreateSingleCellEditorRuntimeJunctionVerticalAccessModule(
		Outer,
		TEXT("LayoutEditorMultiTransitionJunctionVerticalAccess"));
	ULayoutModuleAsset* StructuralModule = CreateSingleCellEditorRuntimeStructuralModule(
		Outer,
		TEXT("LayoutEditorMultiTransitionStructural"));

	FLayoutRegionContentEntry EdgeVerticalAccessEntry;
	EdgeVerticalAccessEntry.EntryId = TEXT("LayoutEditorMultiTransitionEdgeVerticalAccessEntry");
	EdgeVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
	EdgeVerticalAccessEntry.ModuleSettings.Module = EdgeVerticalAccessModule;
	EdgeVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;

	FLayoutRegionContentEntry JunctionVerticalAccessEntry;
	JunctionVerticalAccessEntry.EntryId = TEXT("LayoutEditorMultiTransitionJunctionVerticalAccessEntry");
	JunctionVerticalAccessEntry.ContentKind = ELayoutRegionContentKind::Module;
	JunctionVerticalAccessEntry.ModuleSettings.Module = JunctionVerticalAccessModule;
	EdgeVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Edge;
	JunctionVerticalAccessEntry.ModuleSettings.PlacementZone = ELayoutPlacementZone::Interior;

	FLayoutRegionContentEntry StructuralEntry;
	StructuralEntry.EntryId = TEXT("LayoutEditorMultiTransitionStructuralEntry");
	StructuralEntry.ContentKind = ELayoutRegionContentKind::Module;
	StructuralEntry.ModuleSettings.Module = StructuralModule;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_EditorMultiTransitionAnyActiveSteppedSupport"),
		{StructuralEntry, EdgeVerticalAccessEntry, JunctionVerticalAccessEntry});

	ULayoutProfileAsset* Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_EditorMultiTransitionAnyActiveSteppedSupport"),
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
	if (!TestTrue(TEXT("Controller multi-transition fixture content set validates"), ContentSetValidation.IsValid()))
	{
		for (const FLayoutValidationMessage& Message : ContentSetValidation.Messages)
		{
			AddInfo(Message.Message);
		}
		Controller.SetToolActive(false);
		return false;
	}

	const FLayoutValidationResult ProfileValidation = Profile->ValidateProfile();
	if (!TestTrue(TEXT("Controller multi-transition fixture profile validates"), ProfileValidation.IsValid()))
	{
		for (const FLayoutValidationMessage& Message : ProfileValidation.Messages)
		{
			AddInfo(Message.Message);
		}
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		MakeAutomationExplicitRootPlacementPolicy();
	PlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 10;
	PlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 40;
	PlacementPolicy.HeightIgnoreThreshold = 0;
	PlacementPolicy.TerrainSampleGridSpacing = 16;
	PlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	PlacementPolicy.TerrainTransition.MaxFoundationDepth = 3;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	const FIntVector SiteCenterBlockWorldPos(40, 24, 16);
	const int32 SolveSeed = FLayoutSiteReservation::ComputeSiteSolveSeed(
		SiteCenterBlockWorldPos,
		Harness.World->Seed);
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = SolveSeed;

	const FIntVector HoveredBlockWorldPos = SiteCenterBlockWorldPos - FIntVector(0, 0, 1);
	Controller.SetHoveredLocationForTesting(Harness.World, HoveredBlockWorldPos);
	if (!TestTrue(TEXT("Controller preview succeeds for the multi-transition any-active stepped-support fixture"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FResolvedLayoutSiteRecord& CachedSiteRecord =
		Controller.GetCachedSiteRecordForTesting();
	const FLayoutFrozenTerrainContract& CachedFrozenTerrainContract =
		Controller.GetCachedPreviewFrozenTerrainContractForTesting();
	const FIntVector PreviewRootFootprintMinBlockWorldPos =
		Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting();
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection =
		CachedSiteRecord.GetWorldBindingFrontendSelection();
	const FString RegionDebugPath =
		FString::Printf(TEXT("DirectRoot/%s"), *SiteCenterBlockWorldPos.ToString());
	FLayoutRootSolveBudgetSettings SolveBudget;
	SolveBudget.MaxSolveDurationSeconds =
		ResolveAutomationExplicitRootSolveBudgetSeconds();
	const FLayoutWorldBindingRuntimeView RuntimeView =
		LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);

	FLayoutActiveBiomeSampler ActiveBiomeSampler;
	if (!TestTrue(
			TEXT("Multi-transition controller stepped-support parity initializes the active biome sampler"),
			ActiveBiomeSampler.Initialize(
				GetTransientPackage(),
				Harness.World->WorldGenDef,
				Harness.World->Seed)))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionSolveRequest ExpectedRequest;
	FString RequestFailureReason;
	if (!TestTrue(
			TEXT("Multi-transition controller stepped-support parity rebuilds the same optional explicit-root request the live caller uses"),
			LayoutWorldBindingRuntimeHelpers::TryBuildExplicitRuntimeSolveRequestWithSteppedTerrainSupport(
				RuntimeView,
				FrontendSelection.bUseAnyActiveBiomeSurface ? NAME_None : FrontendSelection.BiomeRowName,
				SiteCenterBlockWorldPos,
				SolveSeed,
				BuildNoiseCoordinateSettings(Harness.World->WorldGenDef),
				ActiveBiomeSampler,
				ExpectedRequest,
				RequestFailureReason,
				Harness.World)))
	{
		AddError(RequestFailureReason);
		Controller.SetToolActive(false);
		return false;
	}
	TestEqual(TEXT("Multi-transition controller parity request uses the direct-root region path as candidate id"), ExpectedRequest.RootCandidateId, FLayoutId(*RegionDebugPath));
	TestEqual(TEXT("Multi-transition controller parity request uses the direct-root region path as solve id"), ExpectedRequest.RootSolveId, FLayoutId(*RegionDebugPath));
	TestEqual(TEXT("Multi-transition controller parity request uses the direct-root placement policy id"), ExpectedRequest.RootPlacementPolicyId, DirectRootPlacementPolicyId);

	const FLayoutRegionSolveScheduleResult ExpectedScheduleResult =
		FLayoutProfileSolver::SolveRegionTree(ExpectedRequest);

	const FLayoutRegionSolveScheduleResult& CachedScheduleResult =
		Controller.GetCachedScheduleResultForTesting();
	TestTrue(
		TEXT("Controller cached schedule result publishes a richer stepped support surface for the multi-transition fixture"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num() > 3);
	TestTrue(
		TEXT("Controller cached schedule result publishes multiple stepped adjacencies for the multi-transition fixture"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.Num() > 2);
	TestTrue(
		TEXT("Controller cached schedule result publishes more than two vertical-access placements for the multi-transition fixture"),
		CountVerticalAccessPlacements(CachedScheduleResult.MergedSolveResult) > 2);
	TestEqual(
		TEXT("Controller cached schedule result keeps the same stepped support-sample count on the cached site record for the multi-transition any-active stepped fixture"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.SupportSamples.Num(),
		CachedSiteRecord.SolveResult.SteppedTerrainSupportMap.SupportSamples.Num());
	TestEqual(
		TEXT("Controller cached schedule result keeps the same stepped adjacency count on the cached site record for the multi-transition any-active stepped fixture"),
		CachedScheduleResult.MergedSolveResult.SteppedTerrainSupportMap.AdjacencySteps.Num(),
		CachedSiteRecord.SolveResult.SteppedTerrainSupportMap.AdjacencySteps.Num());
	TestEqual(
		TEXT("Controller multi-transition any-active stepped fixture keeps the same cached schedule/site vertical-access placement count"),
		CountVerticalAccessPlacements(CachedScheduleResult.MergedSolveResult),
		CountVerticalAccessPlacements(CachedSiteRecord.SolveResult));
	TestEqual(
		TEXT("Controller multi-transition any-active stepped fixture keeps the same cached schedule/site placement count"),
		CachedScheduleResult.MergedSolveResult.Placements.Num(),
		CachedSiteRecord.SolveResult.Placements.Num());
	TestTrue(
		TEXT("Controller multi-transition any-active stepped fixture preserves a junction-capable vertical-access placement on the cached schedule result"),
		CachedScheduleResult.MergedSolveResult.Placements.ContainsByPredicate(
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.Module != nullptr
					&& Placement.Module->GetFName() == FLayoutId(TEXT("LayoutEditorMultiTransitionJunctionVerticalAccess_Module"));
			}));
	TestTrue(
		TEXT("Controller multi-transition any-active stepped fixture preserves a junction-capable vertical-access placement on the cached site record"),
		CachedSiteRecord.SolveResult.Placements.ContainsByPredicate(
			[](const FLayoutPlacedModule& Placement)
			{
				return Placement.Module != nullptr
					&& Placement.Module->GetFName() == FLayoutId(TEXT("LayoutEditorMultiTransitionJunctionVerticalAccess_Module"));
			}));
	TestFalse(
		TEXT("Controller multi-transition any-active stepped fixture caches one stable frozen terrain contract id"),
		CachedFrozenTerrainContract.ContractId.IsNone());
	TestEqual(
		TEXT("Controller multi-transition any-active stepped fixture derives overlay terrain writes from the cached frozen terrain contract"),
		Controller.GetCachedPreviewTerrainWriteCountForTesting(),
		CachedFrozenTerrainContract.TerrainWrites.Num());
	TestEqual(
		TEXT("Controller multi-transition any-active stepped fixture keeps the cached frozen terrain contract footprint anchor aligned with preview"),
		CachedFrozenTerrainContract.FootprintMinBlockWorldPos,
		PreviewRootFootprintMinBlockWorldPos);
	TestTrue(
		TEXT("Controller apply succeeds for the cached multi-transition stepped preview"),
		Controller.ApplyCachedSolve());
	TestEqual(
		TEXT("Controller apply keeps the cached stepped ordinary-root footprint anchor used by preview"),
		Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting(),
		PreviewRootFootprintMinBlockWorldPos);
	TestTrue(
		TEXT("Controller apply keeps the cached multi-transition stepped site realized and committed"),
		Controller.GetCachedSiteRecordForTesting().GetResolvedSiteRuntimeState().bLayoutRealized
			&& Controller.GetCachedSiteRecordForTesting().GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationControllerCompositePreviewApplyWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreateDirectRootCompositeModule(GetTransientPackage(), Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Direct-root thin-carrier composite fixture validates before preview"), CompositeValidation.IsValid()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("DirectRootCompositePreviewApplyThinCarrierEntry");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_DirectRootCompositePreviewApplyThinCarrier"),
		{CompositeEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_DirectRootCompositePreviewApplyThinCarrier"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2717;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds for the thin-carrier composite explicit-root profile"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FResolvedLayoutSiteRecord& MutableCachedSiteRecord =
		const_cast<FResolvedLayoutSiteRecord&>(Controller.GetCachedSiteRecordForTesting());
	if (!TestTrue(
		TEXT("Controller cached thin-carrier solve preserves one composite placement bundle before clearing BundleBoundsCells"),
		MutableCachedSiteRecord.SolveResult.Placements.Num() == 1
			&& MutableCachedSiteRecord.SolveResult.Placements[0].CompositeModule == Composite
			&& MutableCachedSiteRecord.SolveResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		Controller.SetToolActive(false);
		return false;
	}

	MutableCachedSiteRecord.SolveResult.Placements[0].BundleBoundsCells = FIntVector::ZeroValue;
	TestTrue(TEXT("Controller apply succeeds for the cached thin-carrier composite solve"), Controller.ApplyCachedSolve());
	TestTrue(TEXT("Controller thin-carrier apply still replaces the same cached composite solve on a second commit attempt"), Controller.ApplyCachedSolve());

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Thin-carrier composite direct-root apply commits exactly one realized site"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized && SiteRecords[0].bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FLayoutPlacedModule& Placement = SiteRecords[0].SolveResult.Placements[0];
	const FIntVector SharedCellSizeInBlocks = SiteRecords[0].SolveResult.SharedCellSizeInBlocks;
	if (!TestNotEqual(TEXT("Thin-carrier composite direct-root apply preserves one realized shared-cell carrier"), SharedCellSizeInBlocks, FIntVector::ZeroValue))
	{
		Controller.SetToolActive(false);
		return false;
	}
	const FIntVector BundleAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
		SiteRecords[0],
		Placement,
		SharedCellSizeInBlocks);
	const FIntPoint RotationFootprint(2, 1);
	const FIntVector FirstLeafAnchor =
		BundleAnchor + RotateDirectRootTestPlacementCellInFootprintYaw(FIntVector(0, 0, 0), RotationFootprint, Placement.YawRotationSteps);
	const FIntVector SecondLeafAnchor =
		BundleAnchor + RotateDirectRootTestPlacementCellInFootprintYaw(FIntVector(1, 0, 0), RotationFootprint, Placement.YawRotationSteps);

	TestEqual(
		TEXT("Thin-carrier composite direct-root apply stamps the first glued leaf material"),
		Harness.World->GetBlockValueByBlockWorldPos(FirstLeafAnchor, ERessourceType::MaterialIndex, 0),
		SinfullMaterial);
	TestEqual(
		TEXT("Thin-carrier composite direct-root apply stamps the second glued leaf material"),
		Harness.World->GetBlockValueByBlockWorldPos(SecondLeafAnchor, ERessourceType::MaterialIndex, 0),
		SinfullMaterial + 9);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationLeafPlacementSizeUsesEffectiveTemplateDimensionsTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_DirectRootEffectiveLeafSize"), FIntVector(16, 8, 8));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_DirectRootEffectiveLeafSize"),
		Template,
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
	FLayoutPlacedModule Placement;
	Placement.Module = Module;
	Placement.YawRotationSteps = 0;

	const FIntVector SharedCellSizeInBlocks(8, 8, 8);
	TestEqual(
		TEXT("Direct-root leaf placement size uses the module's effective template dimensions when explicit TemplateDimensionsBlocks are absent"),
		Controller.ResolvePlacementSizeInBlocksForTesting(Placement, SharedCellSizeInBlocks),
		FIntVector(16, 8, 8));

	Placement.YawRotationSteps = 1;
	TestEqual(
		TEXT("Direct-root leaf placement size rotates effective template dimensions with yaw"),
		Controller.ResolvePlacementSizeInBlocksForTesting(Placement, SharedCellSizeInBlocks),
		FIntVector(8, 16, 8));

	return true;
}

bool FLayoutDirectRootGenerationSurfaceUndergroundEnvironmentMatrixTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage(), FIntVector(16, 16, 16), TEXT("Reservation"));
	if (!TestNotNull(TEXT("Environment matrix creates runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Environment matrix creates chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Environment matrix creates world definition"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr))
	{
		Controller.SetToolActive(false);
		return false;
	}
	ConfigureFlatActiveBiomeSurface(Harness.World->WorldGenDef, TEXT("Reservation"));

	UObject* const Outer = GetTransientPackage();
	UChunkStructureTemplate* const Template = CreateTemplate(
		Outer,
		TEXT("LayoutTemplate_SurfaceUndergroundEnvironmentMatrix"),
		FIntVector(5, 5, 5));
	FLayoutTestWorldSupport::ConfigureSolidTemplate(
		Template,
		Harness.World,
		FIntVector(5, 5, 5),
		FIntVector::ZeroValue,
		SinfullMaterial);
	const TArray<FLayoutFaceRule> EnvironmentFaces = BuildFilledCubeFaces(
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::FaceOpen}),
		MakeTags({LayoutGameplayTags::FaceOpen}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::TraversalPrimary}));
	ULayoutModuleAsset* const Module = CreateModule(
		Outer,
		TEXT("LayoutModule_SurfaceUndergroundEnvironmentMatrix"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior},
		EnvironmentFaces);
	TArray<FLayoutFaceRule> EntryFaces = EnvironmentFaces;
	EntryFaces[0] = MakeConnectionFaceRule(
		ELayoutFaceDirection::PosX,
		LayoutGameplayTags::FaceEntry,
		MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
		ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
		MakeTags({LayoutGameplayTags::TraversalPrimary}));
	ULayoutModuleAsset* const EntryModule = CreateModule(
		Outer,
		TEXT("LayoutModule_SurfaceUndergroundEnvironmentMatrixEntry"),
		Template,
		{ELayoutCellIntent::Entry},
		EntryFaces);
	ULayoutModuleAsset* const VerticalAccessModule = CreateSingleCellEditorRuntimeVerticalAccessModule(
		Outer,
		TEXT("LayoutModule_SurfaceUndergroundEnvironmentMatrixVerticalAccess"),
		FIntVector(5, 5, 5));
	FLayoutRegionContentEntry ContentEntry;
	ContentEntry.EntryId = TEXT("SurfaceUndergroundEnvironmentMatrixEntry");
	ContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	ContentEntry.ModuleSettings.Module = Module;
	FLayoutRegionContentEntry EntryContentEntry;
	EntryContentEntry.EntryId = TEXT("SurfaceUndergroundEnvironmentMatrixExplicitEntry");
	EntryContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	EntryContentEntry.ModuleSettings.Module = EntryModule;
	FLayoutRegionContentEntry VerticalAccessContentEntry;
	VerticalAccessContentEntry.EntryId = TEXT("SurfaceUndergroundEnvironmentMatrixVerticalAccess");
	VerticalAccessContentEntry.ContentKind = ELayoutRegionContentKind::Module;
	VerticalAccessContentEntry.ModuleSettings.Module = VerticalAccessModule;
	ULayoutRegionContentSetAsset* const ContentSet = CreateRegionContentSet(
		Outer,
		TEXT("LayoutContentSet_SurfaceUndergroundEnvironmentMatrix"),
		{ContentEntry, EntryContentEntry, VerticalAccessContentEntry});
	ULayoutProfileAsset* const Profile = CreateProfile(
		Outer,
		TEXT("LayoutProfile_SurfaceUndergroundEnvironmentMatrix"),
		FIntPoint(3, 3),
		FIntPoint(3, 3),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;
	Profile->bSupportsSteppedTerrainSolve = true;

	ULayoutWorldBindingAsset* const WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		Outer,
		TEXT("LayoutWorldBinding_SurfaceUndergroundEnvironmentMatrix"));
	WorldBinding->BindingId = TEXT("SurfaceUndergroundEnvironmentMatrixBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 30;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchDepthBlocks = 80;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.bFallbackToFlatTerrainWhenSteppedReservedOpenIsInfeasible = true;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.bAllowPerimeterRampTransition = true;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.bAllowFoundationFill = true;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.MaxFoundationDepth = 3;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("SurfaceUndergroundEnvironmentMatrixCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	const FIntVector SurfaceSite(10, 10, 5);
	const FIntVector CavityFloorSite(50, 10, 5);
	const FIntVector CavityAirSite(50, 10, 10);
	const FIntVector OffsetCavityRequestedSite(90, 10, 5);
	const int32 OffsetCavityFloorZ = 6;
	const FIntVector BlockedCavityRequestedSite(130, 10, 5);
	const FIntPoint BlockedCavityColumn(135, 10);
	const FIntVector SteepCavityRequestedSite(170, 10, 5);
	const FIntVector MildSteppedCavityRequestedSite(210, 10, 5);
	const FIntVector SealedCavityRequestedSite(250, 10, 5);
	const FIntVector FoundationCavityRequestedSite(290, 10, 5);
	const FIntVector ExistingFoundationSupportBlock(284, 4, 4);
	const FIntVector MultiEntryCavityRequestedSite(350, 10, 5);
	TArray<FIntVector> MaterialPositions;
	TArray<int32> MaterialValues;
	auto AddMaterial = [&MaterialPositions, &MaterialValues](const FIntVector& Position, const int32 Material)
	{
		MaterialPositions.Add(Position);
		MaterialValues.Add(Material);
	};
	for (int32 Y = -5; Y <= 25; ++Y)
	{
		for (int32 X = -5; X <= 25; ++X)
		{
			AddMaterial(FIntVector(X, Y, SurfaceSite.Z), SinfullMaterial);
			for (int32 Z = SurfaceSite.Z + 1; Z <= 100; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
		}
	}
	for (int32 Y = -5; Y <= 25; ++Y)
	{
		for (int32 X = 35; X <= 65; ++X)
		{
			AddMaterial(FIntVector(X, Y, CavityFloorSite.Z), SinfullMaterial);
			for (int32 Z = CavityFloorSite.Z + 1; Z <= 14; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
			AddMaterial(FIntVector(X, Y, 15), SinfullMaterial);
		}
		for (int32 X = 75; X <= 105; ++X)
		{
			AddMaterial(FIntVector(X, Y, OffsetCavityFloorZ), SinfullMaterial);
			for (int32 Z = OffsetCavityFloorZ + 1; Z <= 14; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
			AddMaterial(FIntVector(X, Y, 15), SinfullMaterial);
		}
		for (int32 X = 115; X <= 145; ++X)
		{
			AddMaterial(FIntVector(X, Y, CavityFloorSite.Z), SinfullMaterial);
			for (int32 Z = CavityFloorSite.Z + 1; Z <= 14; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
			AddMaterial(FIntVector(X, Y, 15), SinfullMaterial);
		}
		for (int32 X = 155; X <= 185; ++X)
		{
			const int32 FloorZ = X < 173 ? 5 : 15;
			for (int32 Z = 5; Z <= FloorZ; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), SinfullMaterial);
			}
			for (int32 Z = FloorZ + 1; Z <= 24; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
			AddMaterial(FIntVector(X, Y, 25), SinfullMaterial);
		}
		for (int32 X = 195; X <= 225; ++X)
		{
			const int32 FloorZ = Y < 13 ? 5 : 10;
			for (int32 Z = 5; Z <= FloorZ; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), SinfullMaterial);
			}
			for (int32 Z = FloorZ + 1; Z <= 24; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
			AddMaterial(FIntVector(X, Y, 25), SinfullMaterial);
		}
		for (int32 X = 235; X <= 265; ++X)
		{
			const bool bInsideCore = X >= 243 && X <= 257 && Y >= 3 && Y <= 17;
			for (int32 Z = 5; Z <= 15; ++Z)
			{
				AddMaterial(
					FIntVector(X, Y, Z),
					bInsideCore && Z >= 6 && Z <= 14 ? EmptyMaterial : SinfullMaterial);
			}
		}
		for (int32 X = 275; X <= 305; ++X)
		{
			AddMaterial(FIntVector(X, Y, 2), SinfullMaterial);
			for (int32 Z = 3; Z <= 14; ++Z)
			{
				AddMaterial(FIntVector(X, Y, Z), EmptyMaterial);
			}
			AddMaterial(FIntVector(X, Y, 15), SinfullMaterial);
		}
	}
	for (int32 Y = -15; Y <= 35; ++Y)
	{
		for (int32 X = 325; X <= 375; ++X)
		{
			const bool bInsideCore = X >= 333 && X <= 367 && Y >= -7 && Y <= 27;
			const bool bClearNegativeYEntryHalo = X >= 333 && X <= 367 && Y >= -12 && Y < -7;
			AddMaterial(FIntVector(X, Y, 5), SinfullMaterial);
			for (int32 Z = 6; Z <= 14; ++Z)
			{
				AddMaterial(
					FIntVector(X, Y, Z),
					bInsideCore || bClearNegativeYEntryHalo ? EmptyMaterial : SinfullMaterial);
			}
			AddMaterial(FIntVector(X, Y, 15), SinfullMaterial);
		}
	}
	Harness.World->SetBlockValuesByBlockWorldPos(MaterialPositions, MaterialValues, false);
	for (int32 Y = 0; Y <= 2; ++Y)
	{
		for (int32 Z = 6; Z <= 14; ++Z)
		{
			Harness.World->SetBlockValueByBlockWorldPos(FIntVector(250, Y, Z), EmptyMaterial, false);
		}
	}
	for (int32 Z = CavityFloorSite.Z; Z <= 15; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(
			FIntVector(BlockedCavityColumn.X, BlockedCavityColumn.Y, Z),
			SinfullMaterial,
			false);
	}
	Harness.World->SetBlockValueByBlockWorldPos(ExistingFoundationSupportBlock, SinfullMaterial, false);
	// Stored ownership is no longer a solve input. The configured procedural row
	// supplies ownership; stored blocks above remain application-test terrain.
	TestEqual(TEXT("Environment matrix surface support material exists"), Harness.World->GetBlockValueByBlockWorldPos(SurfaceSite, ERessourceType::MaterialIndex, 0), SinfullMaterial);
	FLayoutActiveBiomeSampler OwnershipSampler;
	TestTrue(TEXT("Environment matrix initializes procedural ownership"),
		OwnershipSampler.Initialize(Harness.World, Harness.World->WorldGenDef, Harness.World->Seed));
	FLayoutNoiseCoordinateSettings OwnershipCoordinates;
	OwnershipCoordinates.BaseBlockSize = Harness.World->WorldGenDef->BaseBlockSize;
	OwnershipCoordinates.NoiseScale = Harness.World->WorldGenDef->NoiseScale;
	OwnershipCoordinates.NoiseCoordinateOffset = Harness.World->WorldGenDef->NoiseCoordinateOffset;
	FLayoutActiveBiomeSample OwnershipSample;
	TestTrue(TEXT("Environment matrix samples procedural ownership at the support position"),
		OwnershipSampler.SampleAtBlockPosition(FIntVector(5, 5, SurfaceSite.Z), OwnershipCoordinates, OwnershipSample)
		&& OwnershipSample.bAnyPositiveDomain);
	TestEqual(TEXT("Environment matrix uses its configured procedural row"), OwnershipSample.WinningRow.RowName, FName(TEXT("Reservation")));
	TestEqual(TEXT("Environment matrix cavity floor material exists"), Harness.World->GetBlockValueByBlockWorldPos(CavityFloorSite, ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Environment matrix cavity air is empty"), Harness.World->GetBlockValueByBlockWorldPos(CavityAirSite, ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Environment matrix cavity roof material exists"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(50, 10, 15), ERessourceType::MaterialIndex, 0), SinfullMaterial);

	ULayoutDirectRootGenerationSettings* const Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->LayoutWorldBinding = WorldBinding;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 82323;

	auto RunCase = [this, &Controller, &Profile, HarnessWorld = Harness.World](
		const TCHAR* const Label,
		const bool bUnderground,
		const FIntVector& RequestedSite,
		const bool bExpectedSuccess,
		const TCHAR* const ExpectedFailureText) -> bool
	{
		Profile->bUndergroundPlacement = bUnderground;
		Profile->bSupportsSteppedTerrainSolve = bUnderground;
		Controller.ClearCachedSolve();
		Controller.SetHoveredLocationForTesting(HarnessWorld, RequestedSite);
		const bool bSucceeded = Controller.PreviewSolveAtHoveredLocation();
		const FString Status = Controller.GetStatusText().ToString();
		AddInfo(FString::Printf(TEXT("EnvironmentMatrix[%s] requested=%s succeeded=%d status=%s"), Label, *RequestedSite.ToString(), bSucceeded ? 1 : 0, *Status));
		bool bPassed = TestEqual(FString::Printf(TEXT("%s result"), Label), bSucceeded, bExpectedSuccess);
		if (!bExpectedSuccess)
		{
			bPassed &= TestTrue(FString::Printf(TEXT("%s rejection reason"), Label), Status.Contains(ExpectedFailureText));
		}
		return bPassed;
	};

	bool bPassed = true;
	bPassed &= RunCase(TEXT("SurfaceOnSurface"), false, SurfaceSite, true, TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		bPassed &= TestEqual(
			TEXT("Surface preview footprint uses selected surface support"),
			Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting().Z,
			SurfaceSite.Z);
	}
	Profile->bUndergroundPlacement = false;
	Profile->bSupportsSteppedTerrainSolve = true;
	Controller.ClearCachedSolve();
	Controller.SetHoveredLocationForTesting(Harness.World, SurfaceSite);
	bPassed &= TestTrue(
		TEXT("Surface-only profile accepts representative open Surface with stepped terrain enabled"),
		Controller.PreviewSolveAtHoveredLocation());
	bPassed &= RunCase(TEXT("SurfaceInCavity"), false, CavityFloorSite, false, TEXT("Layout profile only supports surface placement."));
	bPassed &= RunCase(TEXT("UndergroundOnSurface"), true, SurfaceSite, false, TEXT("Layout profile only supports underground placement."));
	bPassed &= RunCase(TEXT("SurfaceInOffsetCavity"), false, OffsetCavityRequestedSite, false, TEXT("Layout profile only supports surface placement."));
	bPassed &= RunCase(TEXT("UndergroundInOffsetCavity"), true, OffsetCavityRequestedSite, true, TEXT(""));
	bPassed &= RunCase(TEXT("UndergroundInBlockedCavity"), true, BlockedCavityRequestedSite, true, TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		const FLayoutFrozenTerrainContract& BlockedCavityContract =
			Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		bPassed &= TestTrue(
			TEXT("Blocked cavity falls back to selected-site non-stepped fit"),
			BlockedCavityContract.StageMap.IsEmpty());
		bPassed &= TestEqual(
			TEXT("Blocked cavity fallback keeps selected footprint base"),
			Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting().Z,
			BlockedCavityRequestedSite.Z);
		bPassed &= TestFalse(
			TEXT("Blocked cavity template overlap produces no duplicate footprint excavation"),
			BlockedCavityContract.TerrainWrites.ContainsByPredicate(
				[&BlockedCavityColumn](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.Material == EmptyMaterial
						&& Write.BlockWorldPos.X == BlockedCavityColumn.X
						&& Write.BlockWorldPos.Y == BlockedCavityColumn.Y;
				}));
		bPassed &= TestTrue(TEXT("Blocked cavity fallback cached apply succeeds"), Controller.ApplyCachedSolve());
	}
	Profile->bEnableTerrainSeams = true;
	bPassed &= RunCase(TEXT("UndergroundInMildSteppedCavity"), true, MildSteppedCavityRequestedSite, true, TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		const FLayoutFrozenTerrainContract& MildSteppedContract =
			Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		bPassed &= TestTrue(TEXT("Mild Underground cavity publishes stepped StageMap"), !MildSteppedContract.StageMap.IsEmpty());
		bPassed &= TestTrue(
			TEXT("Mild Underground cavity plans a nonzero terrain seam"),
			Controller.GetCachedSiteRecordForTesting().SolveResult.PlannedCells.ContainsByPredicate(
				[](const FLayoutPlannedCell& Cell)
				{
					return Cell.TerrainSeamFaceMask != 0;
				}));
	}
	Profile->bEnableTerrainSeams = false;
	bPassed &= RunCase(TEXT("UndergroundInSteepCavity"), true, SteepCavityRequestedSite, true, TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		const FLayoutFrozenTerrainContract& SteepCavityContract =
			Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		bPassed &= TestTrue(
			TEXT("Steep cavity falls back to selected-site non-stepped fit"),
			SteepCavityContract.StageMap.IsEmpty());
		const FIntVector SteepFootprintMin = Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting();
		bPassed &= TestFalse(
			TEXT("Steep cavity fallback produces no duplicate template-footprint excavation"),
			SteepCavityContract.TerrainWrites.ContainsByPredicate(
				[&SteepFootprintMin](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.Material == EmptyMaterial
						&& Write.BlockWorldPos.X >= SteepFootprintMin.X
						&& Write.BlockWorldPos.X < SteepFootprintMin.X + 15
						&& Write.BlockWorldPos.Y >= SteepFootprintMin.Y
						&& Write.BlockWorldPos.Y < SteepFootprintMin.Y + 15;
				}));
		bPassed &= TestEqual(
			TEXT("Steep cavity fallback keeps selected footprint base"),
			Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting().Z,
			SteepCavityRequestedSite.Z);
	}
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = 2;
	Profile->MinEntryCount = 2;
	Profile->MaxEntryCount = 2;
	bPassed &= RunCase(TEXT("UndergroundInMixedEntryCavity"), true, SealedCavityRequestedSite, true, TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		const FResolvedLayoutSiteRecord& SealedSiteRecord = Controller.GetCachedSiteRecordForTesting();
		const FLayoutFrozenTerrainContract& SealedContract = Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		int32 SolvedEntryCount = 0;
		for (const FLayoutPlacedModule& Placement : SealedSiteRecord.SolveResult.Placements)
		{
			SolvedEntryCount += Placement.Intent == ELayoutCellIntent::Entry ? 1 : 0;
		}
		bPassed &= TestEqual(TEXT("Mixed cavity solve preserves two authored Entries"), SolvedEntryCount, 2);
		bPassed &= TestFalse(
			TEXT("Mixed cavity does not invent exterior excavation without sampled terrain overlap"),
			SealedContract.TerrainWrites.ContainsByPredicate(
				[](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.Material == EmptyMaterial;
				}));
		bPassed &= TestTrue(
			TEXT("Mixed cavity perimeter ramp may shape terrain independently of final Entry selection"),
			SealedContract.CellContracts.ContainsByPredicate(
				[](const FLayoutTerrainCellContractRecord& CellContract)
				{
					return CellContract.bHasRampTransitionEvidence;
				}));
		bPassed &= TestTrue(TEXT("Mixed cavity Entry excavation cached apply succeeds"), Controller.ApplyCachedSolve());
	}
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	Profile->EntryCount = 1;
	Profile->MinEntryCount = 0;
	Profile->MaxEntryCount = 0;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.MaxFoundationDepth = 1;
	TArray<FIntVector> UnadjustedFoundationWritePositions;
	bPassed &= RunCase(
		TEXT("UndergroundFoundationDepthOnlyBoundsWrites"),
		true,
		FoundationCavityRequestedSite,
		true,
		TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		const FIntVector FoundationFootprintMin = Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting();
		const FLayoutFrozenTerrainContract& BoundedFoundationContract =
			Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		bPassed &= TestTrue(
			TEXT("Foundation depth one emits missing support writes"),
			!BoundedFoundationContract.TerrainWrites.IsEmpty());
		for (const FLayoutFrozenTerrainWriteRecord& Write : BoundedFoundationContract.TerrainWrites)
		{
			UnadjustedFoundationWritePositions.Add(Write.BlockWorldPos);
		}
		bPassed &= TestFalse(
			TEXT("Foundation fill skips an existing support block inside configured slab"),
			BoundedFoundationContract.TerrainWrites.ContainsByPredicate(
				[&ExistingFoundationSupportBlock](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.BlockWorldPos == ExistingFoundationSupportBlock;
				}));
		bPassed &= TestTrue(
			TEXT("Foundation depth one never writes below its configured slab"),
			BoundedFoundationContract.TerrainWrites.ContainsByPredicate(
				[&FoundationFootprintMin](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.BlockWorldPos.Z == FoundationFootprintMin.Z - 1;
				})
			&& !BoundedFoundationContract.TerrainWrites.ContainsByPredicate(
				[&FoundationFootprintMin](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.BlockWorldPos.Z < FoundationFootprintMin.Z - 1;
				}));
	}
	WorldBinding->TemplatePlacementZOffsetBlocks = 2;
	bPassed &= RunCase(
		TEXT("UndergroundFoundationUsesTemplatePlacementOffset"),
		true,
		FoundationCavityRequestedSite,
		true,
		TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		TArray<FIntVector> OffsetFoundationWritePositions;
		for (const FLayoutFrozenTerrainWriteRecord& Write : Controller.GetCachedPreviewFrozenTerrainContractForTesting().TerrainWrites)
		{
			OffsetFoundationWritePositions.Add(Write.BlockWorldPos);
		}
		TArray<int32> UnadjustedFoundationWriteZ;
		TArray<int32> OffsetFoundationWriteZ;
		for (const FIntVector& Position : UnadjustedFoundationWritePositions) { UnadjustedFoundationWriteZ.Add(Position.Z); }
		for (const FIntVector& Position : OffsetFoundationWritePositions) { OffsetFoundationWriteZ.Add(Position.Z); }
		UnadjustedFoundationWriteZ.Sort();
		OffsetFoundationWriteZ.Sort();
		bPassed &= TestTrue(
			TEXT("Template placement offset keeps nonempty foundation writes"),
			!OffsetFoundationWriteZ.IsEmpty() && !UnadjustedFoundationWriteZ.IsEmpty());
		if (!OffsetFoundationWriteZ.IsEmpty() && !UnadjustedFoundationWriteZ.IsEmpty())
		{
			bPassed &= TestEqual(
				TEXT("Template placement offset moves foundation slab minimum below resolved template base"),
				OffsetFoundationWriteZ[0],
				UnadjustedFoundationWriteZ[0] + WorldBinding->TemplatePlacementZOffsetBlocks);
			bPassed &= TestEqual(
				TEXT("Template placement offset moves foundation slab maximum below resolved template base"),
				OffsetFoundationWriteZ.Last(),
				UnadjustedFoundationWriteZ.Last() + WorldBinding->TemplatePlacementZOffsetBlocks);
		}
	}
	WorldBinding->TemplatePlacementZOffsetBlocks = 0;
	WorldBinding->DefaultPlacementPolicy.TerrainTransition.MaxFoundationDepth = 3;
	bPassed &= RunCase(TEXT("UndergroundInFoundationCavity"), true, FoundationCavityRequestedSite, true, TEXT(""));
	if (Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
	{
		const FLayoutFrozenTerrainContract& FoundationContract =
			Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		const FIntVector FoundationFootprintMin = Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting();
		bPassed &= TestTrue(
			TEXT("Foundation cavity freezes support writes below selected base"),
			FoundationContract.TerrainWrites.ContainsByPredicate(
				[&FoundationFootprintMin](const FLayoutFrozenTerrainWriteRecord& Write)
				{
					return Write.BlockWorldPos.Z == FoundationFootprintMin.Z - 1
						&& Write.Material == SinfullMaterial;
				}));
		bPassed &= TestTrue(TEXT("Foundation cavity cached apply succeeds"), Controller.ApplyCachedSolve());
		bPassed &= TestEqual(
			TEXT("Foundation cavity realizes frozen support material below selected base"),
			Harness.World->GetBlockValueByBlockWorldPos(
				FoundationFootprintMin - FIntVector(0, 0, 1),
				ERessourceType::MaterialIndex),
			SinfullMaterial);
	}

	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 16;
	Profile->MinimumFootprintInCells = FIntPoint(7, 7);
	Profile->MaximumFootprintInCells = FIntPoint(7, 7);
	Profile->EntryCountMode = ELayoutCountConstraintMode::Exact;
	Profile->EntryCount = 2;
	Profile->MinEntryCount = 2;
	Profile->MaxEntryCount = 2;
	TSet<FString> MultiSeedEntrySignatures;
	TArray<double> MultiEntryDurationsMs;
	TArray<double> MultiEntrySelectedTerrainDurationsMs;
	FString FirstSeedSignature;
	FString RepeatedFirstSeedSignature;
	const int32 EntrySeeds[] = {82323, 82324, 82325, 82326, 82323};
	for (int32 SeedIndex = 0; SeedIndex < UE_ARRAY_COUNT(EntrySeeds); ++SeedIndex)
	{
		Settings->ManualSolveSeed = EntrySeeds[SeedIndex];
		const double StartSeconds = FPlatformTime::Seconds();
		const FString Label = FString::Printf(TEXT("UndergroundMultiEntrySeed%d"), EntrySeeds[SeedIndex]);
		bPassed &= RunCase(*Label, true, MultiEntryCavityRequestedSite, true, TEXT(""));
		MultiEntryDurationsMs.Add((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
		MultiEntrySelectedTerrainDurationsMs.Add(
			UChunkWorldLayoutRuntimeComponent::GetLastSelectedSiteTerrainMillisecondsForTesting());
		if (!Controller.GetCachedSiteRecordForTesting().bLayoutSolved)
		{
			continue;
		}

		TArray<FIntVector> EntryCells;
		TSet<int32> EntrySides;
		for (const FLayoutPlacedModule& Placement : Controller.GetCachedSiteRecordForTesting().SolveResult.Placements)
		{
			if (Placement.Intent != ELayoutCellIntent::Entry)
			{
				continue;
			}
			EntryCells.Add(Placement.Cell);
			if (Placement.Cell.X == 0) { EntrySides.Add(0); }
			else if (Placement.Cell.X == 6) { EntrySides.Add(1); }
			else if (Placement.Cell.Y == 0) { EntrySides.Add(2); }
			else if (Placement.Cell.Y == 6) { EntrySides.Add(3); }
		}
		EntryCells.Sort([](const FIntVector& Left, const FIntVector& Right)
		{
			return Left.Y == Right.Y ? Left.X < Right.X : Left.Y < Right.Y;
		});
		FString Signature;
		for (const FIntVector& Cell : EntryCells)
		{
			Signature += FString::Printf(TEXT("%d,%d;"), Cell.X, Cell.Y);
		}
		const FLayoutFrozenTerrainContract& MultiEntryContract = Controller.GetCachedPreviewFrozenTerrainContractForTesting();
		int32 SelectedRampEntryCount = 0;
		for (const FLayoutTerrainCellContractRecord& CellContract : MultiEntryContract.CellContracts)
		{
			if (!CellContract.bHasRampTransitionEvidence)
			{
				continue;
			}
			bPassed &= TestTrue(
				FString::Printf(TEXT("%s keeps ramp authority on perimeter cells"), *Label),
				CellContract.Cell.X == 0 || CellContract.Cell.X == 6
					|| CellContract.Cell.Y == 0 || CellContract.Cell.Y == 6);
			SelectedRampEntryCount += EntryCells.Contains(CellContract.Cell) ? 1 : 0;
		}
		bPassed &= TestTrue(
			FString::Printf(TEXT("%s derives selected Entry ramps from sampled terrain rather than side label"), *Label),
			SelectedRampEntryCount <= EntryCells.Num());
		MultiSeedEntrySignatures.Add(Signature);
		AddInfo(FString::Printf(TEXT("TerrainQualifiedEntrySelection seed=%d signature=%s sideCount=%d"), EntrySeeds[SeedIndex], *Signature, EntrySides.Num()));
		if (SeedIndex == 0) { FirstSeedSignature = Signature; }
		if (SeedIndex == UE_ARRAY_COUNT(EntrySeeds) - 1) { RepeatedFirstSeedSignature = Signature; }
		bPassed &= TestEqual(FString::Printf(TEXT("%s resolves exact Entry count"), *Label), EntryCells.Num(), 2);
		bPassed &= TestTrue(
			FString::Printf(TEXT("%s separates Entries across cardinal sides"), *Label),
			EntrySides.Num() >= 2);
	}
	MultiEntryDurationsMs.Sort();
	MultiEntrySelectedTerrainDurationsMs.Sort();
	if (!MultiEntryDurationsMs.IsEmpty() && MultiEntrySelectedTerrainDurationsMs.Num() == MultiEntryDurationsMs.Num())
	{
		const int32 P50Index = FMath::Clamp(FMath::FloorToInt((MultiEntryDurationsMs.Num() - 1) * 0.50), 0, MultiEntryDurationsMs.Num() - 1);
		const int32 P95Index = FMath::Clamp(FMath::CeilToInt((MultiEntryDurationsMs.Num() - 1) * 0.95), 0, MultiEntryDurationsMs.Num() - 1);
		const double SelectedTerrainP50Ratio = MultiEntryDurationsMs[P50Index] > 0.0
			? MultiEntrySelectedTerrainDurationsMs[P50Index] / MultiEntryDurationsMs[P50Index]
			: 1.0;
		AddInfo(FString::Printf(
			TEXT("SelectedSiteTerrainTiming fixture=7x7 runs=%d totalP50Ms=%.3f totalP95Ms=%.3f terrainP50Ms=%.3f terrainP95Ms=%.3f terrainP50Ratio=%.3f"),
			MultiEntryDurationsMs.Num(),
			MultiEntryDurationsMs[P50Index],
			MultiEntryDurationsMs[P95Index],
			MultiEntrySelectedTerrainDurationsMs[P50Index],
			MultiEntrySelectedTerrainDurationsMs[P95Index],
			SelectedTerrainP50Ratio));
		bPassed &= TestTrue(
			TEXT("Selected-site terrain p50 remains within 25 percent of total root solve"),
			SelectedTerrainP50Ratio <= 0.25);
	}
	bPassed &= TestTrue(TEXT("Terrain-qualified Entry cells vary across solve seeds"), MultiSeedEntrySignatures.Num() > 1);
	bPassed &= TestEqual(TEXT("Repeated multi-Entry seed preserves selected Entry cells"), RepeatedFirstSeedSignature, FirstSeedSignature);

	Profile->MinimumFootprintInCells = FIntPoint(3, 3);
	Profile->MaximumFootprintInCells = FIntPoint(3, 3);
	Profile->EntryCountMode = ELayoutCountConstraintMode::None;
	Profile->EntryCount = 1;
	Profile->MinEntryCount = 0;
	Profile->MaxEntryCount = 0;
	Settings->ManualSolveSeed = 82323;
	WorldBinding->DefaultPlacementPolicy.SurfaceSearch.TerrainSearchStartZ = 30;
	bPassed &= RunCase(TEXT("UndergroundInCavityAir"), true, CavityAirSite, true, TEXT(""));
	bPassed &= RunCase(TEXT("UndergroundInCavityFloorClick"), true, CavityFloorSite, true, TEXT(""));
	const FLayoutId FirstFloorSolveArtifactId = Controller.GetCachedSiteRecordForTesting().SolvedArtifactId;
	const FLayoutId FirstFloorContractId = Controller.GetCachedPreviewFrozenTerrainContractForTesting().ContractId;
	const TArray<FLayoutPlacedModule> FirstFloorPlacements = Controller.GetCachedSiteRecordForTesting().SolveResult.Placements;
	bPassed &= RunCase(TEXT("UndergroundInCavityFloorClickRepeat"), true, CavityFloorSite, true, TEXT(""));
	bPassed &= TestEqual(
		TEXT("Repeated cavity solve preserves solved artifact identity"),
		Controller.GetCachedSiteRecordForTesting().SolvedArtifactId,
		FirstFloorSolveArtifactId);
	bPassed &= TestEqual(
		TEXT("Repeated cavity solve preserves frozen terrain contract identity"),
		Controller.GetCachedPreviewFrozenTerrainContractForTesting().ContractId,
		FirstFloorContractId);
	bPassed &= TestEqual(
		TEXT("Repeated cavity solve preserves placement count"),
		Controller.GetCachedSiteRecordForTesting().SolveResult.Placements.Num(),
		FirstFloorPlacements.Num());
	if (Controller.GetCachedSiteRecordForTesting().SolveResult.Placements.Num() == FirstFloorPlacements.Num())
	{
		for (int32 PlacementIndex = 0; PlacementIndex < FirstFloorPlacements.Num(); ++PlacementIndex)
		{
			const FLayoutPlacedModule& RepeatedPlacement =
				Controller.GetCachedSiteRecordForTesting().SolveResult.Placements[PlacementIndex];
			bPassed &= TestEqual(TEXT("Repeated cavity placement cell remains deterministic"),
				RepeatedPlacement.Cell, FirstFloorPlacements[PlacementIndex].Cell);
			bPassed &= TestEqual(TEXT("Repeated cavity placement module remains deterministic"),
				RepeatedPlacement.ModuleSnapshotId, FirstFloorPlacements[PlacementIndex].ModuleSnapshotId);
		}
	}

	const FResolvedLayoutSiteRecord& CavitySiteRecord = Controller.GetCachedSiteRecordForTesting();
	const FLayoutSolveResult& CavitySolveResult = CavitySiteRecord.SolveResult;
	const FLayoutFrozenTerrainContract& CavityContract = Controller.GetCachedPreviewFrozenTerrainContractForTesting();
	bPassed &= TestTrue(TEXT("Accepted cavity publishes support samples"), !CavitySolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty());
	bPassed &= TestTrue(TEXT("Accepted cavity publishes StageMap"), !CavityContract.StageMap.IsEmpty());
	bPassed &= TestFalse(TEXT("Accepted cavity publishes solved artifact id"), CavitySiteRecord.SolvedArtifactId.IsNone());
	bPassed &= TestEqual(TEXT("Accepted cavity artifact preserves active cells"), CavitySiteRecord.SolvedArtifactActiveCellCount, 9);
	if (!CavitySolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty())
	{
		bPassed &= TestEqual(TEXT("Accepted cavity support uses cavity floor"), CavitySolveResult.SteppedTerrainSupportMap.SupportSamples[0].SupportSurfaceZ, CavityFloorSite.Z);
	}
	if (!CavityContract.StageMap.IsEmpty() && !CavitySolveResult.Placements.IsEmpty())
	{
		const FLayoutPlacedModule& FirstPlacement = CavitySolveResult.Placements[0];
		const FLayoutFrozenTerrainStageCellRecord* const PlacementStage = CavityContract.StageMap.FindByPredicate(
			[&FirstPlacement](const FLayoutFrozenTerrainStageCellRecord& Stage)
			{
				return Stage.FootprintCellXY == FIntPoint(FirstPlacement.Cell.X, FirstPlacement.Cell.Y);
			});
		bPassed &= TestNotNull(TEXT("First cavity placement has frozen stage anchor"), PlacementStage);
		if (PlacementStage != nullptr)
		{
			const FIntVector PlacementAnchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
				CavitySiteRecord,
				FirstPlacement,
				CavitySolveResult.SharedCellSizeInBlocks);
			bPassed &= TestEqual(TEXT("Cavity placement anchor uses frozen selected support"), PlacementAnchor.Z, PlacementStage->ResolvedStageBaseBlockWorldZ);
			bPassed &= TestEqual(TEXT("Cavity preview footprint uses material-proven selected floor"), Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting().Z, CavityFloorSite.Z);
			bPassed &= TestEqual(
				TEXT("Cavity StageMap resolves placement base one cell above selected support floor"),
				PlacementStage->ResolvedStageBaseBlockWorldZ,
				PlacementStage->SnappedSupportFloorZ + CavitySolveResult.SharedCellSizeInBlocks.Z);
			const FIntVector AuthoredEmptyVoxel = PlacementAnchor + FIntVector(1, 0, 0);
			Harness.World->SetBlockValueByBlockWorldPos(AuthoredEmptyVoxel, SinfullMaterial, false);
			bPassed &= TestTrue(TEXT("Cavity cached apply succeeds"), Controller.ApplyCachedSolve());
			bPassed &= TestEqual(
				TEXT("Cavity realization stamps first template at preview anchor"),
				Harness.World->GetBlockValueByBlockWorldPos(PlacementAnchor, ERessourceType::MaterialIndex, 0),
				SinfullMaterial);
			bPassed &= TestEqual(
				TEXT("Cavity realization replaces authored empty template voxel without terrain pre-clear"),
				Harness.World->GetBlockValueByBlockWorldPos(AuthoredEmptyVoxel, ERessourceType::MaterialIndex, 0),
				EmptyMaterial);
		}
	}

	Settings->LayoutWorldBinding = nullptr;
	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return bPassed;
}

bool FLayoutDirectRootGenerationBindingAwarePreviewSnapsHoveredSiteToNearestLatticePlaneTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Binding-aware preview harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Binding-aware preview harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ConfigureFlatActiveBiomeSurface(Harness.World->WorldGenDef, TEXT("Reservation"));
	ConfigureProceduralSurface(Harness.World->WorldGenDef, 255);

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_BindingAwarePreviewSnap"), FIntVector(5, 5, 5));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_BindingAwarePreviewSnap"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("BindingAwarePreviewSnapEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("LayoutContentSet_BindingAwarePreviewSnap"), {Entry});
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_BindingAwarePreviewSnap"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(GetTransientPackage(), TEXT("LayoutWorldBinding_BindingAwarePreviewSnap"));
	WorldBinding->BindingId = TEXT("PreviewBinding");
	WorldBinding->BiomeRowNames = {TEXT("Reservation")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PreviewCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->LayoutWorldBinding = WorldBinding;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 4101;

	for (int32 X = 6; X <= 10; ++X)
	{
		for (int32 Y = 6; Y <= 10; ++Y)
		{
			for (int32 Z = 0; Z <= 255; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(FIntVector(X, Y, Z), EmptyMaterial, false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(X, Y, 255));
		}
	}

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 256));
	const bool bPreviewSucceeded = Controller.PreviewSolveAtHoveredLocation();
	if (!bPreviewSucceeded)
	{
		AddInfo(Controller.GetStatusText().ToString());
	}
	TestTrue(TEXT("Binding-aware preview succeeds for an off-lattice hovered site by snapping to the nearest binding plane"), bPreviewSucceeded);
	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection = CachedSiteRecord.GetWorldBindingFrontendSelection();
	TestEqual(TEXT("Binding-aware preview snaps the cached site center onto the nearest binding lattice plane"), CachedSiteRecord.SiteCenterBlockWorldPos.Z, 255);
	TestEqual(TEXT("Binding-aware preview preserves the selected world binding id on the cached site record"), FrontendSelection.WorldBindingId, WorldBinding->BindingId);
	TestEqual(TEXT("Binding-aware preview preserves the selected world binding candidate id on the cached site record"), FrontendSelection.WorldBindingCandidateId, Candidate.CandidateId);
	TestTrue(TEXT("Binding-aware preview status reports the snapped origin when the hovered site center moved"), Controller.GetStatusText().ToString().Contains(TEXT("snapped from X=8 Y=8 Z=256")));

	Settings->LayoutWorldBinding = nullptr;
	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationBindingAwarePreviewUsesAnyActiveBiomeSurfaceTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Binding-aware any-active-biome preview harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Binding-aware any-active-biome preview harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Binding-aware any-active-biome preview harness creates a world definition"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ConfigureFlatActiveBiomeSurface(Harness.World->WorldGenDef, TEXT("Reservation"));
	ConfigureProceduralSurface(Harness.World->WorldGenDef, 255);
	// The owning row need not be the first configured active row.
	FBiomeDualData NonOwningRow = Harness.World->WorldGenDef->WorldBiomes[0];
	NonOwningRow.BiomeName = TEXT("NonOwning");
	NonOwningRow.Domain.Reset();
	NonOwningRow.DomainRun = NewObject<UBiomeFastNoiseEditor>(Harness.World->WorldGenDef);
	Harness.World->WorldGenDef->WorldBiomes.Insert(NonOwningRow, 0);

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_BindingAwarePreviewAnyActiveBiomeRoot"), FIntVector(5, 5, 5));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_BindingAwarePreviewAnyActiveBiomeRoot"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("BindingAwarePreviewAnyActiveBiomeRootEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("LayoutContentSet_BindingAwarePreviewAnyActiveBiomeRoot"), {Entry});
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_BindingAwarePreviewAnyActiveBiomeRoot"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(GetTransientPackage(), TEXT("LayoutWorldBinding_BindingAwarePreviewAnyActiveBiomeRoot"));
	WorldBinding->BindingId = TEXT("PreviewAnyActiveBiomeRootBinding");
	WorldBinding->BiomeRowNames = {TEXT("Secondary")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PreviewAnyActiveBiomeRootCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->LayoutWorldBinding = WorldBinding;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 4102;

	for (int32 X = 6; X <= 10; ++X)
	{
		for (int32 Y = 6; Y <= 10; ++Y)
		{
			for (int32 Z = 0; Z <= 255; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(FIntVector(X, Y, Z), EmptyMaterial, false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(X, Y, 255));
		}
	}

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 255));
	const bool bPreviewSucceeded = Controller.PreviewSolveAtHoveredLocation();
	if (!bPreviewSucceeded)
	{
		AddInfo(Controller.GetStatusText().ToString());
	}
	TestTrue(TEXT("Binding-aware direct-root preview succeeds on the manually hovered active biome surface even when the binding row list names a different biome"), bPreviewSucceeded);
	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection = CachedSiteRecord.GetWorldBindingFrontendSelection();
	TestTrue(TEXT("Binding-aware direct-root preview marks the cached site to use any active biome surface"), FrontendSelection.bUseAnyActiveBiomeSurface);
	TestTrue(TEXT("Binding-aware direct-root preview leaves the cached specific biome-row carrier empty"), FrontendSelection.BiomeRowName.IsNone());
	TestTrue(TEXT("Binding-aware direct-root preview leaves the cached biome-row allow-list empty for the manual tool path"), FrontendSelection.CompatibleBiomeRowNames.IsEmpty());
	TestTrue(
		TEXT("Binding-aware direct-root preview keeps one resolved terrain-alignment level on the merged solve result"),
		CachedSiteRecord.GetResolvedSiteSolvedPayload().SolveResult.ResolvedTerrainAlignmentLevel != INDEX_NONE);
	const FIntVector ActualAnchor = Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting();
	TestEqual(
		TEXT("Binding-aware direct-root preview keeps the cached overlay footprint anchor aligned to the preview terrain anchor for ordinary roots"),
		ActualAnchor,
		FIntVector(8, 8, 255));
	TestTrue(TEXT("Binding-aware direct-root preview summary still reports the accepted preview attempt"), Controller.GetGenerationSummaryText().ToString().Contains(TEXT("Accepted")));

	FResolvedLayoutSiteRecord SynchronousSite;
	FLayoutRegionSolveScheduleResult SynchronousSchedule;
	FString SynchronousFailure;
	TestTrue(TEXT("Synchronous binding preview accepts the same active terrain"),
		Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
			FIntVector(8, 8, 255), WorldBinding, Profile, Settings->ManualSolveSeed,
			SynchronousSite, SynchronousSchedule, &SynchronousFailure));
	const FLayoutWorldBindingSiteFrontendSelection SynchronousFrontend = SynchronousSite.GetWorldBindingFrontendSelection();
	TestTrue(TEXT("Synchronous binding preview preserves any-active placement metadata"), SynchronousFrontend.bUseAnyActiveBiomeSurface);
	TestTrue(TEXT("Synchronous binding preview does not retain an unrelated binding biome row"), SynchronousFrontend.BiomeRowName.IsNone());
	TestTrue(TEXT("Synchronous binding preview does not retain an automatic placement allow-list"), SynchronousFrontend.CompatibleBiomeRowNames.IsEmpty());

	Settings->LayoutWorldBinding = nullptr;
	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationBindingAwareContinuationPreviewUsesAnyActiveBiomeSurfaceTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Binding-aware any-active-biome preview harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Binding-aware any-active-biome preview harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Binding-aware any-active-biome preview harness creates a world definition"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ConfigureFlatActiveBiomeSurface(Harness.World->WorldGenDef, TEXT("Reservation"));
	ConfigureProceduralSurface(Harness.World->WorldGenDef, 255);

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_BindingAwarePreviewAnyActiveBiome"), FIntVector(5, 5, 5));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_BindingAwarePreviewAnyActiveBiome"),
		Template,
		{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			MakeTags({LayoutGameplayTags::FaceOpen, LayoutGameplayTags::FaceEntry}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("BindingAwarePreviewAnyActiveBiomeEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("LayoutContentSet_BindingAwarePreviewAnyActiveBiome"), {Entry});
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_BindingAwarePreviewAnyActiveBiome"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 1, false);
	Profile->ContentSet = ContentSet;
	Profile->ContinuationEntryLevel = 0;
	Profile->bRequireAllTraversalChannelsReachable = true;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(GetTransientPackage(), TEXT("LayoutWorldBinding_BindingAwarePreviewAnyActiveBiome"));
	WorldBinding->BindingId = TEXT("PreviewAnyActiveBiomeBinding");
	WorldBinding->BiomeRowNames = {TEXT("Secondary")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	FLayoutWorldBindingContinuationFamily& Family = WorldBinding->ContinuationFamilies.AddDefaulted_GetRef();
	Family.FamilyId = TEXT("PreviewAnyActiveBiomeContinuationFamily");
	Family.FamilyType = ELayoutWorldBindingContinuationFamilyType::SurfacePath;
	Family.EndpointConnectorTypeTag = LayoutGameplayTags::ConnectorRoad;
	Family.SolveBudget.MaxSolveDurationSeconds = 3.5f;
	FLayoutWorldBindingContinuationCandidate& Candidate = Family.Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("PreviewAnyActiveBiomeContinuationCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->LayoutWorldBinding = WorldBinding;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 4102;

	for (int32 X = 6; X <= 10; ++X)
	{
		for (int32 Y = 6; Y <= 10; ++Y)
		{
			for (int32 Z = 0; Z <= 255; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(FIntVector(X, Y, Z), EmptyMaterial, false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(X, Y, 255));
		}
	}

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 255));
	const bool bPreviewSucceeded = Controller.PreviewSolveAtHoveredLocation();
	if (!bPreviewSucceeded)
	{
		AddInfo(Controller.GetStatusText().ToString());
	}
	TestTrue(TEXT("Binding-aware continuation preview succeeds on the manually hovered active biome surface even when the binding row list names a different biome"), bPreviewSucceeded);
	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	const FLayoutWorldBindingSiteFrontendSelection FrontendSelection = CachedSiteRecord.GetWorldBindingFrontendSelection();
	TestEqual(TEXT("Binding-aware continuation preview keeps the selected continuation placement kind"), CachedSiteRecord.SolveResult.RootPlacementKind, ELayoutWorldBindingPlacementKind::SurfacePath);
	TestEqual(TEXT("Binding-aware continuation preview keeps the selected continuation family"), FrontendSelection.ResolvedContinuationSelection.FamilyId, Family.FamilyId);
	TestTrue(TEXT("Binding-aware continuation preview marks the cached site to use any active biome surface"), FrontendSelection.bUseAnyActiveBiomeSurface);
	TestTrue(TEXT("Binding-aware continuation preview leaves the cached specific biome-row carrier empty"), FrontendSelection.BiomeRowName.IsNone());
	TestTrue(TEXT("Binding-aware continuation preview leaves the cached biome-row allow-list empty for the manual tool path"), FrontendSelection.CompatibleBiomeRowNames.IsEmpty());
	TestEqual(
		TEXT("Binding-aware continuation preview keeps the selected continuation entry level"),
		FrontendSelection.ResolvedContinuationSelection.ResolvedEntryLevel,
		Profile->ContinuationEntryLevel);
	const FIntVector ActualAnchor = Controller.ResolveCachedRootFootprintMinBlockWorldPosForTesting();
	if (ActualAnchor != FIntVector(8, 8, 255))
	{
		AddInfo(FString::Printf(TEXT("BindingAwareContinuationPreview anchor mismatch: actual=[%s] expected=[8,8,255] siteCenter=[%s] sharedCellSize=[%s]"),
			*ActualAnchor.ToString(),
			*Controller.GetCachedSiteRecordForTesting().SiteCenterBlockWorldPos.ToString(),
			*Controller.GetCachedSiteRecordForTesting().SolveResult.SharedCellSizeInBlocks.ToString()));
	}
	TestEqual(
		TEXT("Binding-aware continuation preview keeps the cached overlay footprint anchor aligned to the preview terrain anchor"),
		ActualAnchor,
		FIntVector(8, 8, 255));
	TestTrue(TEXT("Binding-aware continuation preview summary still reports the accepted preview attempt"), Controller.GetGenerationSummaryText().ToString().Contains(TEXT("Accepted")));

	Settings->LayoutWorldBinding = nullptr;
	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationBindingAwarePreviewUsesWorldBindingAuthorityOverStandaloneSynthesisTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Binding-authority preview harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Binding-authority preview harness creates a transient chunk world"), Harness.World)
		|| !TestNotNull(TEXT("Binding-authority preview harness creates a world definition"), Harness.World != nullptr ? Harness.World->WorldGenDef.Get() : nullptr))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ConfigureFlatActiveBiomeSurface(Harness.World->WorldGenDef, TEXT("Reservation"));
	ConfigureProceduralSurface(Harness.World->WorldGenDef, 255);

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_BindingAuthorityPreview"), FIntVector(5, 5, 5));
	ULayoutModuleAsset* Module = CreateModule(
		GetTransientPackage(),
		TEXT("LayoutModule_BindingAuthorityPreview"),
		Template,
		{ELayoutCellIntent::Boundary},
		BuildFilledCubeFaces(
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::FaceOpen}),
			MakeTags({LayoutGameplayTags::FaceOpen}),
			ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor,
			MakeTags({LayoutGameplayTags::TraversalPrimary})));
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("BindingAuthorityPreviewEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(GetTransientPackage(), TEXT("LayoutContentSet_BindingAuthorityPreview"), {Entry});
	ULayoutProfileAsset* Profile = CreateProfile(GetTransientPackage(), TEXT("LayoutProfile_BindingAuthorityPreview"), FIntPoint(1, 1), FIntPoint(1, 1), 1, 0, false);
	Profile->ContentSet = ContentSet;

	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(GetTransientPackage(), TEXT("LayoutWorldBinding_BindingAuthorityPreview"));
	WorldBinding->BindingId = TEXT("BindingAuthorityPreviewBinding");
	WorldBinding->BiomeRowNames = {TEXT("Secondary")};
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(5, 5, 5);
	WorldBinding->SolveBudget.MaxSolveDurationSeconds = 2.5f;
	WorldBinding->DefaultPlacementPolicy.TerrainSampleGridSpacing = 3;
	WorldBinding->DefaultPlacementPolicy.HeightIgnoreThreshold = 7;
	FLayoutWorldBindingCandidate& Candidate = WorldBinding->Candidates.AddDefaulted_GetRef();
	Candidate.CandidateId = TEXT("BindingAuthorityPreviewCandidate");
	Candidate.LayoutProfile = Profile;
	Candidate.Weight = 1;

	FLayoutWorldBindingPlacementPolicy StandalonePlacementPolicy = MakeAutomationExplicitRootPlacementPolicy();
	StandalonePlacementPolicy.TerrainSampleGridSpacing = 99;
	StandalonePlacementPolicy.HeightIgnoreThreshold = 123;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->LayoutWorldBinding = WorldBinding;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 4104;

	for (int32 X = 6; X <= 10; ++X)
	{
		for (int32 Y = 6; Y <= 10; ++Y)
		{
			for (int32 Z = 0; Z <= 255; ++Z)
			{
				Harness.World->SetBlockValueByBlockWorldPos(FIntVector(X, Y, Z), EmptyMaterial, false);
			}
			FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(X, Y, 255));
		}
	}

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 255));
	const bool bPreviewSucceeded = Controller.PreviewSolveAtHoveredLocation();
	if (!bPreviewSucceeded)
	{
		AddInfo(Controller.GetStatusText().ToString());
	}
	TestTrue(TEXT("Binding-aware preview succeeds even when non-binding standalone synthesis would be hostile"), bPreviewSucceeded);
	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	TestEqual(
		TEXT("Binding-aware preview keeps the requested solve seed while ignoring unrelated standalone synthesis values"),
		CachedSiteRecord.GetSiteSolveSourceSelection().SolveSeed,
		Settings->ManualSolveSeed);
	TestEqual(
		TEXT("Binding-aware preview uses the world binding terrain sample spacing instead of unrelated standalone synthesis values"),
		CachedSiteRecord.SolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing,
		WorldBinding->DefaultPlacementPolicy.TerrainSampleGridSpacing);
	TestEqual(
		TEXT("Binding-aware preview uses the world binding height ignore threshold instead of unrelated standalone synthesis values"),
		CachedSiteRecord.SolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold,
		WorldBinding->DefaultPlacementPolicy.HeightIgnoreThreshold);
	TestTrue(
		TEXT("Binding-aware preview no longer echoes the standalone terrain sample spacing override"),
		CachedSiteRecord.SolveResult.WorldBindingPlacementPolicy.TerrainSampleGridSpacing != StandalonePlacementPolicy.TerrainSampleGridSpacing);
	TestTrue(
		TEXT("Binding-aware preview no longer echoes the standalone height ignore threshold override"),
		CachedSiteRecord.SolveResult.WorldBindingPlacementPolicy.HeightIgnoreThreshold != StandalonePlacementPolicy.HeightIgnoreThreshold);

	Settings->LayoutWorldBinding = nullptr;
	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationSupersededPreviewPublishesLatestOnlyTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Superseded preview harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Superseded preview harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutModuleAsset* Module = CreateOneCellStampModule(
		GetTransientPackage(),
		Harness.World,
		TEXT("LayoutModule_SupersededPreviewLatestOnly"),
		SinfullMaterial,
		{ELayoutCellIntent::Boundary});
	FLayoutRegionContentEntry Entry;
	Entry.EntryId = TEXT("SupersededPreviewLatestOnlyEntry");
	Entry.ContentKind = ELayoutRegionContentKind::Module;
	Entry.ModuleSettings.Module = Module;
	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_SupersededPreviewLatestOnly"),
		{Entry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_SupersededPreviewLatestOnly"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->LayoutWorldBinding = nullptr;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 4103;

	const TSharedRef<FThreadSafeBool, ESPMode::ThreadSafe> PreviewWorkGate =
		MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
	Harness.RuntimeComponent->SetExplicitPreviewWorkGateForTesting(PreviewWorkGate);
	Harness.RuntimeComponent->SetDisableAutoPumpForTesting(true);

	const FIntVector FirstSiteCenterBlockWorldPos(8, 8, 1);
	const FIntVector SecondSiteCenterBlockWorldPos(24, 8, 1);
	Controller.SetHoveredLocationForTesting(Harness.World, FirstSiteCenterBlockWorldPos - FIntVector(0, 0, 1));
	TestTrue(
		TEXT("First preview schedules without pumping so a later request can supersede it"),
		Controller.StartPreviewSolveAtHoveredLocationForTesting());
	Controller.SetHoveredLocationForTesting(Harness.World, SecondSiteCenterBlockWorldPos - FIntVector(0, 0, 1));
	TestTrue(
		TEXT("Second preview schedules and supersedes the older explicit-root preview handle"),
		Controller.StartPreviewSolveAtHoveredLocationForTesting());

	*PreviewWorkGate = true;
	const bool bPublishedLatestPreview = PumpLayoutRuntimeUntil(
		Harness.RuntimeComponent,
		[&Controller, &SecondSiteCenterBlockWorldPos]()
		{
			const FResolvedLayoutSiteRecord& SiteRecord = Controller.GetCachedSiteRecordForTesting();
			return SiteRecord.bLayoutSolved
				&& SiteRecord.GetResolvedSiteLocationMetadata().SiteCenterBlockWorldPos == SecondSiteCenterBlockWorldPos;
		});
	TestTrue(
		TEXT("Latest explicit-root preview publishes after the superseded request is canceled"),
		bPublishedLatestPreview);

	for (int32 PumpIndex = 0; PumpIndex < 40; ++PumpIndex)
	{
		Harness.RuntimeComponent->PumpBackgroundLayoutSolves();
	}
	Harness.RuntimeComponent->SetExplicitPreviewWorkGateForTesting(nullptr);

	const FResolvedLayoutSiteRecord& CachedSiteRecord = Controller.GetCachedSiteRecordForTesting();
	TestEqual(
		TEXT("Superseded preview coverage keeps the cached site center on the latest submitted location after late pumping"),
		CachedSiteRecord.GetResolvedSiteLocationMetadata().SiteCenterBlockWorldPos,
		SecondSiteCenterBlockWorldPos);
	TestTrue(
		TEXT("Superseded preview coverage reports the latest preview location in controller diagnostics"),
		Controller.GetStatusText().ToString().Contains(SecondSiteCenterBlockWorldPos.ToString()));
	TestFalse(
		TEXT("Superseded preview coverage never lets the stale first location overwrite the cached controller result"),
		Controller.GetStatusText().ToString().Contains(FirstSiteCenterBlockWorldPos.ToString()));
	TestTrue(
		TEXT("Controller apply still succeeds after a superseded preview race"),
		Controller.ApplyCachedSolve());

	const TArray<FResolvedLayoutSiteRecord> ResolvedSiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Superseded preview apply realizes only the latest explicit-root site"),
		ResolvedSiteRecords.Num() == 1
			&& ResolvedSiteRecords[0].GetResolvedSiteLocationMetadata().SiteCenterBlockWorldPos == SecondSiteCenterBlockWorldPos);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationLeafPlacementSizeRejectsInvalidTemplateDimensionsTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();

	UChunkStructureTemplate* Template = CreateTemplate(GetTransientPackage(), TEXT("LayoutTemplate_DirectRootInvalidLeafSize"), FIntVector::ZeroValue);
	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_DirectRootInvalidLeafSize"));
	Module->Template = Template;
	FLayoutPlacedModule Placement;
	Placement.Module = Module;

	const FIntVector SharedCellSizeInBlocks(8, 8, 8);
	TestEqual(
		TEXT("Direct-root leaf placement size no longer falls back to shared cell size when the module template dimensions are unreadable"),
		Controller.ResolvePlacementSizeInBlocksForTesting(Placement, SharedCellSizeInBlocks),
		FIntVector::ZeroValue);
	return true;
}

bool FLayoutDirectRootGenerationPlacementCellsUseOccupiedLocalCellsBeforeModuleBoundsTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();

	ULayoutModuleAsset* const Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_DirectRootOccupiedCellPlacement"));
	FLayoutPlacedModule Placement;
	Placement.Cell = FIntVector(10, 20, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = Module;
	Placement.YawRotationSteps = 1;
	Placement.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};

	const TArray<FIntVector> PlacementCells = SortDirectRootPlacementCells(Controller.BuildPlacementCellsForTesting(Placement));
	const TArray<FIntVector> ExpectedCells = SortDirectRootPlacementCells({FIntVector(10, 20, 0), FIntVector(11, 20, 0)});

	TestEqual(TEXT("Direct-root overlay placement cells use occupied local cells before live module bounds"), PlacementCells, ExpectedCells);
	return true;
}

bool FLayoutDirectRootGenerationPlacementCellsRebuildFromLeafModuleWithoutOccupiedLocalCellsTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();

	ULayoutModuleAsset* const Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_DirectRootLeafThinCarrier"));
	FLayoutPlacedModule Placement;
	Placement.Cell = FIntVector(10, 20, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = Module;
	Placement.YawRotationSteps = 1;

	const TArray<FIntVector> PlacementCells = SortDirectRootPlacementCells(Controller.BuildPlacementCellsForTesting(Placement));
	const TArray<FIntVector> ExpectedCells = SortDirectRootPlacementCells({FIntVector(10, 20, 0)});

	TestEqual(TEXT("Direct-root overlay placement cells fall back to one leaf cell when OccupiedLocalCells are absent"), PlacementCells, ExpectedCells);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationCachedOverlaySummaryTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.CachedOverlaySummaryHonorsPhaseAndLayers",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationCompositeOverlaySummaryTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.CachedOverlaySummaryCountsCompositePlacementCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationCompositeOverlaySummaryWithoutBundleBoundsTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.CachedOverlaySummaryCountsCompositePlacementCellsWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationOverlaySummaryUsesSolveResultSharedCellSizeWithoutLiveSourcesTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.CachedOverlaySummaryUsesSolveResultSharedCellSizeWithoutLiveSources",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationCachedOverlaySummaryTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutProfileAsset* ChildProfile = nullptr;
	ULayoutModuleAsset* ParentModule = nullptr;
	ULayoutModuleAsset* ChildModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootParentChildContentSet(
		GetTransientPackage(),
		Harness.World,
		ChildProfile,
		ParentModule,
		ChildModule,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* ParentProfile = CreateExplicitRootParentProfile(GetTransientPackage());
	ParentProfile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = ParentProfile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2712;
	Settings->bDrawHoverSquare = true;
	Settings->bDrawSolvedFootprint = true;
	Settings->bDrawCellZoneMarkers = true;
	Settings->bDrawChildBounds = true;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds before cached overlay inspection"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FIntVector CachedCenter = Controller.GetCachedSiteRecordForTesting().SiteCenterBlockWorldPos;

	FLayoutDirectRootGenerationOverlaySummary Summary = Controller.BuildOverlaySummary();
	TestTrue(TEXT("Cached overlay summary keeps the hover square visible when enabled"), Summary.bDrawsHoverSquare);
	TestEqual(TEXT("Cached overlay summary draws exactly one solved footprint box"), Summary.SolvedFootprintCount, 1);
	TestTrue(TEXT("Cached overlay summary reports zone markers from the preview solve"), Summary.CellZoneMarkerCount > 0);
	TestTrue(TEXT("Cached overlay summary draws cached child bounds without re-solving"), Summary.ChildBoundsCount > 0);

	Settings->bDrawCellZoneMarkers = false;
	FLayoutDirectRootGenerationOverlaySummary HiddenZoneSummary = Controller.BuildOverlaySummary();
	TestEqual(TEXT("Zone marker toggle hides cached zone marker content"), HiddenZoneSummary.CellZoneMarkerCount, 0);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationCompositeOverlaySummaryTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreateDirectRootCompositeModule(GetTransientPackage(), Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Direct-root composite fixture validates before preview"), CompositeValidation.IsValid()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("DirectRootCompositeEntry");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_DirectRootComposite"),
		{CompositeEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_DirectRootComposite"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2715;
	Settings->bDrawHoverSquare = false;
	Settings->bDrawSolvedFootprint = false;
	Settings->bDrawCellZoneMarkers = true;
	Settings->bDrawChildBounds = false;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds for the simple composite explicit-root profile"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	TestEqual(TEXT("Controller caches one solved composite placement bundle"), Controller.GetCachedSiteRecordForTesting().SolveResult.Placements.Num(), 1);
	if (!TestTrue(
		TEXT("Controller cached solve preserves occupied local bundle cells"),
		Controller.GetCachedSiteRecordForTesting().SolveResult.Placements.Num() == 1
			&& Controller.GetCachedSiteRecordForTesting().SolveResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		Controller.SetToolActive(false);
		return false;
	}

	const FLayoutDirectRootGenerationOverlaySummary StructuralSummary = Controller.BuildOverlaySummary();
	TestEqual(TEXT("Direct-root structural overlay counts zone markers from the preview solve"), StructuralSummary.CellZoneMarkerCount, 2);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationCompositeOverlaySummaryWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreateDirectRootCompositeModule(GetTransientPackage(), Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Direct-root composite fixture validates before preview"), CompositeValidation.IsValid()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("DirectRootCompositeEntry_NoBundleBounds");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_DirectRootComposite_NoBundleBounds"),
		{CompositeEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_DirectRootComposite_NoBundleBounds"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2716;
	Settings->bDrawHoverSquare = false;
	Settings->bDrawSolvedFootprint = false;
	Settings->bDrawCellZoneMarkers = true;
	Settings->bDrawChildBounds = false;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds for the simple composite explicit-root profile"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	if (!TestTrue(
		TEXT("Controller cached solve preserves one composite placement bundle"),
		Controller.GetCachedSiteRecordForTesting().SolveResult.Placements.Num() == 1))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FResolvedLayoutSiteRecord& MutableCachedSiteRecord =
		const_cast<FResolvedLayoutSiteRecord&>(Controller.GetCachedSiteRecordForTesting());
	MutableCachedSiteRecord.SolveResult.Placements[0].BundleBoundsCells = FIntVector::ZeroValue;
	const FLayoutDirectRootGenerationOverlaySummary StructuralSummary = Controller.BuildOverlaySummary();
	TestEqual(
		TEXT("Direct-root structural overlay still counts zone markers when BundleBoundsCells is absent"),
		StructuralSummary.CellZoneMarkerCount,
		2);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

bool FLayoutDirectRootGenerationOverlaySummaryUsesSolveResultSharedCellSizeWithoutLiveSourcesTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreateDirectRootCompositeModule(GetTransientPackage(), Harness.World);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Direct-root composite fixture validates before shared-cell-size carrier inspection"), CompositeValidation.IsValid()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FLayoutRegionContentEntry CompositeEntry;
	CompositeEntry.EntryId = TEXT("DirectRootCompositeSharedCellCarrierEntry");
	CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
	CompositeEntry.ModuleSettings.CompositeModule = Composite;

	ULayoutRegionContentSetAsset* ContentSet = CreateRegionContentSet(
		GetTransientPackage(),
		TEXT("LayoutContentSet_DirectRootCompositeSharedCellCarrier"),
		{CompositeEntry});
	ULayoutProfileAsset* Profile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_DirectRootCompositeSharedCellCarrier"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	Profile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = Profile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2720;
	Settings->bDrawHoverSquare = false;
	Settings->bDrawSolvedFootprint = false;
	Settings->bDrawCellZoneMarkers = true;
	Settings->bDrawChildBounds = false;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));
	if (!TestTrue(TEXT("Controller preview succeeds before shared-cell-size carrier inspection"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FResolvedLayoutSiteRecord& MutableCachedSiteRecord =
		const_cast<FResolvedLayoutSiteRecord&>(Controller.GetCachedSiteRecordForTesting());
	MutableCachedSiteRecord.ContentSet = nullptr;

	const FLayoutDirectRootGenerationOverlaySummary Summary = Controller.BuildOverlaySummary();
	TestTrue(TEXT("Cached direct-root overlay summary still sees a solved preview without live content/module sources"), Summary.bHasSolvedPreview);
	TestEqual(TEXT("Cached direct-root overlay summary still counts zone markers from solve-result shared cell size"), Summary.CellZoneMarkerCount, 2);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationShortcutTriggersPreviewTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ShortcutTriggersPreview",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationShortcutTriggersPreviewTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutProfileAsset* ChildProfile = nullptr;
	ULayoutModuleAsset* ParentModule = nullptr;
	ULayoutModuleAsset* ChildModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootParentChildContentSet(
		GetTransientPackage(),
		Harness.World,
		ChildProfile,
		ParentModule,
		ChildModule,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* ParentProfile = CreateExplicitRootParentProfile(GetTransientPackage());
	ParentProfile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = ParentProfile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2713;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));

	FLayoutDirectRootGenerationEdMode EdMode;
	TestFalse(TEXT("Non-shortcut input does not consume the preview action"), EdMode.HandlePreviewShortcutForTesting(EKeys::RightMouseButton, IE_Pressed, true));
	TestFalse(TEXT("Left click without Ctrl does not consume the preview action"), EdMode.HandlePreviewShortcutForTesting(EKeys::LeftMouseButton, IE_Pressed, false));
	TestTrue(TEXT("Ctrl + Left Click consumes the preview shortcut"), EdMode.HandlePreviewShortcutForTesting(EKeys::LeftMouseButton, IE_Pressed, true));
	TestTrue(TEXT("Ctrl + Left Click through the editor mode caches a solved preview"), Controller.GetCachedSiteRecordForTesting().bLayoutSolved);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationRenderEmitsOverlayGeometryTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.RenderEmitsOverlayGeometry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationRenderEmitsOverlayGeometryTest::RunTest(const FString& Parameters)
{
	FPorismDIMsWorldGeneratorExtensionEditorModule& EditorModule =
		FModuleManager::LoadModuleChecked<FPorismDIMsWorldGeneratorExtensionEditorModule>("PorismDIMsWorldGeneratorExtensionEditor");
	FLayoutDirectRootGenerationController& Controller = EditorModule.GetLayoutDirectRootGenerationController();
	Controller.ClearCachedSolve();
	Controller.SetToolActive(true);

	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent)
		|| !TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), Harness.World))
	{
		Controller.SetToolActive(false);
		return false;
	}

	ULayoutProfileAsset* ChildProfile = nullptr;
	ULayoutModuleAsset* ParentModule = nullptr;
	ULayoutModuleAsset* ChildModule = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootParentChildContentSet(
		GetTransientPackage(),
		Harness.World,
		ChildProfile,
		ParentModule,
		ChildModule,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* ParentProfile = CreateExplicitRootParentProfile(GetTransientPackage());
	ParentProfile->ContentSet = ContentSet;

	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->TargetChunkWorld = Harness.World;
	Settings->LayoutProfile = ParentProfile;
	Settings->bUseSiteDerivedSeed = false;
	Settings->ManualSolveSeed = 2714;
	Settings->bDrawHoverSquare = true;
	Settings->bDrawSolvedFootprint = true;
	Settings->bDrawCellZoneMarkers = true;
	Settings->bDrawChildBounds = true;
	Settings->bDrawInterfaceBounds = true;

	Controller.SetHoveredLocationForTesting(Harness.World, FIntVector(8, 8, 1));

	FRecordingPrimitiveDrawInterface HoverRecorder;
	Controller.Render(nullptr, &HoverRecorder);
	TestTrue(TEXT("Hovered direct-root tool emits overlay line geometry before preview solve"), HoverRecorder.LineCount > 0);

	if (!TestTrue(TEXT("Controller preview succeeds before render overlay inspection"), Controller.PreviewSolveAtHoveredLocation()))
	{
		Controller.SetToolActive(false);
		return false;
	}

	FRecordingPrimitiveDrawInterface PreviewRecorder;
	Controller.Render(nullptr, &PreviewRecorder);
	TestTrue(TEXT("Preview overlay emits line geometry for hover and solved footprint"), PreviewRecorder.LineCount > HoverRecorder.LineCount);

	Settings->bDrawInterfaceBounds = true;
	FRecordingPrimitiveDrawInterface InterfaceRecorder;
	Controller.Render(nullptr, &InterfaceRecorder);
	TestTrue(TEXT("Preview overlay emits cached seam or closure draw geometry"), InterfaceRecorder.LineCount > 0 || InterfaceRecorder.PointCount > 0);

	Controller.ClearCachedSolve();
	Controller.SetToolActive(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationPreviewRenderingCostTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.PreviewRenderingCost",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationPreviewRenderingCostTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Preview world exists"), Harness.World)) return false;
	FLayoutDirectRootGenerationController Controller;
	Controller.SetToolActive(true);
	Controller.CachedPreviewChunkWorld = Harness.World;
	Controller.CachedSharedCellSizeInBlocks = FIntVector(1);
	Controller.CachedSiteRecord.bLayoutSolved = true;
	Controller.CachedSiteRecord.SolveResult.FootprintSize = FIntPoint(100, 100);
	Controller.CachedSiteRecord.SolveResult.PlannedCells.AddDefaulted();
	ULayoutDirectRootGenerationSettings* Settings = Controller.GetSettingsObject();
	Settings->bDrawHoverSquare = false;
	Settings->bDrawSolvedFootprint = false;
	Settings->bDrawCellZoneMarkers = false;
	Settings->bDrawBoundaryFacePyramids = false;
	Settings->bDrawUnoccupiedCellMarkers = false;
	Settings->bDrawEntryMarkers = false;
	Settings->bDrawVerticalAccessMarkers = false;
	Settings->bDrawChildBounds = false;
	Settings->bDrawInterfaceBounds = false;
	// Fixed payload isolates rendering from solve time: 100,000 writes in 1,000 contiguous rows.
	for (int32 Z = 0; Z < 10; ++Z)
	for (int32 Y = 0; Y < 100; ++Y)
	for (int32 X = 0; X < 100; ++X)
	{
		FLayoutFrozenTerrainWriteRecord& Write = Controller.CachedPreviewFrozenTerrainContract.TerrainWrites.AddDefaulted_GetRef();
		Write.BlockWorldPos = FIntVector(X - 50, Y - 50, Z);
		Write.Material = Z < 5 ? EmptyMaterial : SinfullMaterial;
		Controller.CachedPreviewTerrainWritePositions.Add(Write.BlockWorldPos);
		Controller.CachedPreviewTerrainWriteMaterials.Add(Write.Material);
	}
	Controller.BuildCellDebugInfos();
	TArray<double> Times;
	int32 Lines = 0;
	for (int32 Frame = 0; Frame < 7; ++Frame)
	{
		FRecordingPrimitiveDrawInterface Recorder;
		const double Start = FPlatformTime::Seconds();
		Controller.Render(nullptr, &Recorder);
		Times.Add((FPlatformTime::Seconds() - Start) * 1000.0);
		Lines = Recorder.LineCount;
	}
	Times.Sort();
	AddInfo(FString::Printf(TEXT("Preview submission: writes=100000 lines=%d medianCPU=%.3fms (7 frames; counting PDI, not GPU)"), Lines, Times[3]));
	TestEqual(TEXT("Contiguous terrain preview submits one box per row"), Lines, 12000);
	TestEqual(TEXT("Preview optimization leaves terrain writes intact"), Controller.CachedPreviewFrozenTerrainContract.TerrainWrites.Num(), 100000);
	TestEqual(TEXT("One cached terrain run per row"), Controller.CachedTerrainPreviewRuns.Num(), 1000);
	Settings->bMergeTerrainPreviewBlocks = false;
	FRecordingPrimitiveDrawInterface LegacyRecorder;
	Controller.Render(nullptr, &LegacyRecorder);
	TestEqual(TEXT("Individual-block inspection remains available"), LegacyRecorder.LineCount, 1200000);
	Settings->bMergeTerrainPreviewBlocks = true;

	FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(
		nullptr, Harness.World->GetWorld()->Scene, FEngineShowFlags(ESFIM_Editor)));
	FSceneViewInitOptions Options;
	Options.ViewFamily = &Family;
	Options.SetViewRectangle(FIntRect(0, 0, 1920, 1080));
	Options.ViewRotationMatrix = FMatrix::Identity;
	Options.ProjectionMatrix = FMatrix::Identity;
	Options.ProjectionMatrix.M[0][0] = Options.ProjectionMatrix.M[1][1] = 0.001;
	FSceneView FarView(Options);
	// Isolate LOD from visibility; a separate plane below tests culling.
	FarView.ViewFrustum = FConvexVolume();
	FSceneView HiddenView(Options);
	HiddenView.ViewFrustum.Planes.Reset();
	HiddenView.ViewFrustum.Planes.Add(FPlane(1, 0, 0, -1000000));
	HiddenView.ViewFrustum.Init();
	FRecordingPrimitiveDrawInterface HiddenTerrainRecorder;
	Controller.Render(&HiddenView, &HiddenTerrainRecorder);
	TestEqual(TEXT("Offscreen terrain runs submit no lines"), HiddenTerrainRecorder.LineCount, 0);

	FLayoutGeneratorContinuationSegmentSolveResult Segment;
	Segment.bSucceeded = true;
	Segment.FrozenTerrainContract = Controller.CachedPreviewFrozenTerrainContract;
	Segment.ConnectorRecord.SolveResult = Controller.CachedSiteRecord.SolveResult;
	Segment.PreviewGeometry.SharedCellSizeInBlocks = FIntVector(1);
	Segment.PreviewGeometry.TerrainCells.AddDefaulted();
	Controller.CachedContinuationSegments.Add(MoveTemp(Segment));
	Controller.bHasCachedAcceptedContinuationPreview = true;
	Controller.BuildContinuationCellDebugInfos();
	FRecordingPrimitiveDrawInterface ContinuationRecorder;
	Controller.Render(nullptr, &ContinuationRecorder);
	TestEqual(TEXT("Continuation preview shares compact terrain drawing"), ContinuationRecorder.LineCount, 12000);
	Settings->bDrawCellZoneMarkers = true;
	FRecordingPrimitiveDrawInterface FarContinuationRecorder;
	Controller.Render(&FarView, &FarContinuationRecorder);
	TestEqual(TEXT("Continuation cells use the same distant overview"), FarContinuationRecorder.PointCount, 1);
	Settings->bDrawCellZoneMarkers = false;
	Controller.bHasCachedAcceptedContinuationPreview = false;

	TArray<FLayoutFrozenTerrainWriteRecord> MixedWrites;
	for (const FIntVector Position : {FIntVector(1,0,0), FIntVector(0,0,0), FIntVector(3,0,0),
		FIntVector(1,0,0), FIntVector(0,1,0), FIntVector(0,0,1)})
	{
		auto& Write = MixedWrites.AddDefaulted_GetRef();
		Write.BlockWorldPos = Position;
		Write.Material = EmptyMaterial;
	}
	auto& Fill = MixedWrites.AddDefaulted_GetRef();
	Fill.BlockWorldPos = FIntVector(2,0,0);
	Fill.Material = SinfullMaterial;
	const auto MixedRuns = Controller.BuildTerrainPreviewRuns(MixedWrites);
	TestEqual(TEXT("Gaps, colors and rows remain separate; duplicates collapse"), MixedRuns.Num(), 5);
	TSet<FIntVector> ClearCells, FillCells;
	for (const auto& Run : MixedRuns)
	{
		for (int32 X = Run.Min.X; X <= Run.Max.X; ++X)
		{
			(Run.bEmpty ? ClearCells : FillCells).Add(FIntVector(X, Run.Min.Y, Run.Min.Z));
		}
	}
	TestEqual(TEXT("All five distinct clear cells retained"), ClearCells.Num(), 5);
	TestTrue(TEXT("Clear bounds do not cover the fill gap"), !ClearCells.Contains(FIntVector(2,0,0)));
	TestTrue(TEXT("Fill bound retains its cell"), FillCells.Num() == 1 && FillCells.Contains(FIntVector(2,0,0)));
	auto& Overwrite = MixedWrites.AddDefaulted_GetRef();
	Overwrite.BlockWorldPos = FIntVector(0,0,0);
	Overwrite.Material = SinfullMaterial;
	const auto OverwrittenRuns = Controller.BuildTerrainPreviewRuns(MixedWrites);
	TestEqual(TEXT("Conflicting duplicate retains only final write color"), OverwrittenRuns.Num(), 6);
	TestTrue(TEXT("Last fill replaces the earlier clear at the same coordinate"),
		OverwrittenRuns.ContainsByPredicate([](const auto& Run)
		{
			return Run.Min == FIntVector::ZeroValue && Run.Max == FIntVector::ZeroValue && !Run.bEmpty;
		}));
	MixedWrites.SetNum(2);
	MixedWrites[0].BlockWorldPos = FIntVector(MIN_int32, 0, 0);
	MixedWrites[1].BlockWorldPos = FIntVector(MAX_int32, 0, 0);
	TestEqual(TEXT("Extreme coordinates cannot overflow adjacency"), Controller.BuildTerrainPreviewRuns(MixedWrites).Num(), 2);

	Controller.CachedPreviewFrozenTerrainContract.TerrainWrites.Reset();
	Controller.CachedPreviewTerrainWritePositions.Reset();
	Controller.CachedPreviewTerrainWriteMaterials.Reset();
	Controller.BuildCellDebugInfos();
	TestTrue(TEXT("Replacement without writes removes old terrain geometry"), Controller.CachedTerrainPreviewRuns.IsEmpty());
	Controller.CachedCellDebugInfos.Reset();
	for (int32 Y = 0; Y < 100; ++Y)
	for (int32 X = 0; X < 100; ++X)
	{
		Controller.CachedCellDebugInfos.AddDefaulted_GetRef().Cell = FIntVector(X, Y, 0);
	}
	Settings->bDrawCellZoneMarkers = true;
	FRecordingPrimitiveDrawInterface FarRecorder;
	Controller.Render(&FarView, &FarRecorder);
	TestEqual(TEXT("Distant 10000-cell overview uses points"), FarRecorder.PointCount, 10000);
	TestEqual(TEXT("Distant cells omit wire detail"), FarRecorder.LineCount, 0);
	Options.ProjectionMatrix = FMatrix::Identity;
	FSceneView NearView(Options);
	NearView.ViewFrustum = FConvexVolume();
	FRecordingPrimitiveDrawInterface NearRecorder;
	Controller.Render(&NearView, &NearRecorder);
	TestEqual(TEXT("Zooming in restores all sphere and zone lines"), NearRecorder.LineCount, 480000);
	TestEqual(TEXT("Near cells do not also draw overview points"), NearRecorder.PointCount, 0);
	Options.ProjectionMatrix = FReversedZPerspectiveMatrix(PI / 4.0, 1920.0, 1080.0, 1.0);
	Options.ViewOrigin = FVector(0, 0, -10000);
	FSceneView PerspectiveView(Options);
	PerspectiveView.ViewFrustum = FConvexVolume();
	FRecordingPrimitiveDrawInterface PerspectiveRecorder;
	Controller.Render(&PerspectiveView, &PerspectiveRecorder);
	TestEqual(TEXT("Perspective depth reduces distant marker detail"), PerspectiveRecorder.PointCount, 10000);
	Settings->MinimumDetailedMarkerRadiusPixels = 0;
	FRecordingPrimitiveDrawInterface FullDetailRecorder;
	Controller.Render(&FarView, &FullDetailRecorder);
	TestEqual(TEXT("Zero threshold preserves full detail when zoomed out"), FullDetailRecorder.LineCount, NearRecorder.LineCount);
	FRecordingPrimitiveDrawInterface HiddenCellsRecorder;
	Controller.Render(&HiddenView, &HiddenCellsRecorder);
	TestEqual(TEXT("Offscreen cells submit no lines even with full detail"), HiddenCellsRecorder.LineCount, 0);
	Controller.ClearCachedSolve();
	TestTrue(TEXT("Clear releases both terrain drawing caches"), Controller.CachedTerrainPreviewRuns.IsEmpty()
		&& Controller.CachedContinuationTerrainPreviewRuns.IsEmpty());
	return true;
}

// ---- N4.87: Region boundary face classification includes PosZ on top cells ----

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutDirectRootGenerationClassifiesPosZAsTopRegionBoundaryTest,
	"PorismExtension.Layout.Editor.DirectRootGeneration.ClassifiesPosZAsTopRegionBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutDirectRootGenerationClassifiesPosZAsTopRegionBoundaryTest::RunTest(const FString& Parameters)
{
	// Single cell at Z=0 on a 1x1 footprint. MaxPlannedZ=0 means it is also the top.
	// All six faces are boundary faces (cell is simultaneously min and max in all axes).
	{
		const TArray<ELayoutFaceDirection> BoundaryFaces =
			FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
				FIntVector(0, 0, 0), FIntPoint(1, 1), 0);

		TestTrue(TEXT("NegZ boundary on bottom cell at Z=0"),
			BoundaryFaces.Contains(ELayoutFaceDirection::NegZ));
		TestTrue(TEXT("PosZ boundary on topmost cell at Z=0 (sole cell is both top and bottom)"),
			BoundaryFaces.Contains(ELayoutFaceDirection::PosZ));
		TestTrue(TEXT("NegX boundary on 1x1 footprint (leftmost edge)"),
			BoundaryFaces.Contains(ELayoutFaceDirection::NegX));
		TestTrue(TEXT("PosX boundary on 1x1 footprint (rightmost edge)"),
			BoundaryFaces.Contains(ELayoutFaceDirection::PosX));
		TestTrue(TEXT("NegY boundary on 1x1 footprint (frontmost edge)"),
			BoundaryFaces.Contains(ELayoutFaceDirection::NegY));
		TestTrue(TEXT("PosY boundary on 1x1 footprint (backmost edge)"),
			BoundaryFaces.Contains(ELayoutFaceDirection::PosY));
		TestEqual(TEXT("1x1 footprint has 6 boundary faces (all faces)"),
			BoundaryFaces.Num(), 6);
	}

	// Multi-level layout: 2x2 footprint, MaxPlannedZ=2.
	// Cell (0,0,0): corner cell at bottom — should get NegX, NegY, NegZ only.
	{
		const TArray<ELayoutFaceDirection> BottomCornerFaces =
			FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
				FIntVector(0, 0, 0), FIntPoint(2, 2), 2);

		TestTrue(TEXT("NegX boundary on bottom corner cell"),
			BottomCornerFaces.Contains(ELayoutFaceDirection::NegX));
		TestTrue(TEXT("NegY boundary on bottom corner cell"),
			BottomCornerFaces.Contains(ELayoutFaceDirection::NegY));
		TestTrue(TEXT("NegZ boundary on bottom cell at Z=0"),
			BottomCornerFaces.Contains(ELayoutFaceDirection::NegZ));
		TestFalse(TEXT("No PosZ boundary on non-top cell (Z=0, MaxZ=2)"),
			BottomCornerFaces.Contains(ELayoutFaceDirection::PosZ));
		TestEqual(TEXT("Bottom corner cell has exactly 3 boundary faces (NegX, NegY, NegZ)"),
			BottomCornerFaces.Num(), 3);
	}

	// Interior cell at max Z: (1,1,2) in 3x3 footprint, MaxZ=2.
	// Not on X/Y edges, but at the top level — should get PosZ only.
	{
		const TArray<ELayoutFaceDirection> TopInteriorFaces =
			FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
				FIntVector(1, 1, 2), FIntPoint(3, 3), 2);

		TestFalse(TEXT("No NegZ on non-bottom interior cell at Z=2"),
			TopInteriorFaces.Contains(ELayoutFaceDirection::NegZ));
		TestTrue(TEXT("PosZ boundary on topmost interior cell at Z=MaxPlannedZ"),
			TopInteriorFaces.Contains(ELayoutFaceDirection::PosZ));
		TestEqual(TEXT("Top interior cell has exactly 1 boundary face (PosZ)"),
			TopInteriorFaces.Num(), 1);
	}

	// Top corner cell: (0,0,2) in 2x2 footprint, MaxZ=2.
	// Should get NegX, NegY, PosZ (no NegZ since Z!=0).
	{
		const TArray<ELayoutFaceDirection> TopCornerFaces =
			FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
				FIntVector(0, 0, 2), FIntPoint(2, 2), 2);

		TestTrue(TEXT("NegX boundary on top corner cell"),
			TopCornerFaces.Contains(ELayoutFaceDirection::NegX));
		TestTrue(TEXT("NegY boundary on top corner cell"),
			TopCornerFaces.Contains(ELayoutFaceDirection::NegY));
		TestFalse(TEXT("No NegZ on top cell at Z=2"),
			TopCornerFaces.Contains(ELayoutFaceDirection::NegZ));
		TestTrue(TEXT("PosZ boundary on top corner cell"),
			TopCornerFaces.Contains(ELayoutFaceDirection::PosZ));
		TestEqual(TEXT("Top corner cell has exactly 3 boundary faces (NegX, NegY, PosZ)"),
			TopCornerFaces.Num(), 3);
	}

	// Bridge-cell scenario: cells shifted by stepped terrain adapter.
	// Layout where MaxPlannedZ=3 accounts for TopBridge cells.
	// Cell (1,0,0) at Z=0 on 2x1 footprint, MaxZ=3:
	// Edge cell (PosX) at bottom level — should get PosX, NegZ, no PosZ.
	{
		const TArray<ELayoutFaceDirection> BridgeBottomEdgeFaces =
			FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
				FIntVector(1, 0, 0), FIntPoint(2, 1), 3);

		TestTrue(TEXT("PosX boundary on bridge cell at footprint edge"),
			BridgeBottomEdgeFaces.Contains(ELayoutFaceDirection::PosX));
		TestTrue(TEXT("NegZ boundary on bridge cell at Z=0"),
			BridgeBottomEdgeFaces.Contains(ELayoutFaceDirection::NegZ));
		TestFalse(TEXT("No PosZ on bridge cell at Z=0 when MaxZ=3"),
			BridgeBottomEdgeFaces.Contains(ELayoutFaceDirection::PosZ));
	}

	// TopBridge cell: cell at (0,0,3) on 2x1 footprint, MaxZ=3.
	// This is the deck cell at the top level — should get PosZ.
	{
		const TArray<ELayoutFaceDirection> TopBridgeFaces =
			FLayoutDirectRootGenerationController::ClassifyRegionBoundaryFacesForTesting(
				FIntVector(0, 0, 3), FIntPoint(2, 1), 3);

		TestFalse(TEXT("No NegZ on TopBridge cell at Z=3"),
			TopBridgeFaces.Contains(ELayoutFaceDirection::NegZ));
		TestTrue(TEXT("PosZ boundary on TopBridge cell at MaxZ"),
			TopBridgeFaces.Contains(ELayoutFaceDirection::PosZ));
	}

	return true;
}
