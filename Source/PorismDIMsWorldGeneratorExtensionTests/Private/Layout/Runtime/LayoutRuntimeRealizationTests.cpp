// Copyright 2026 Spotted Loaf Studio

#include "Layout/Types/LayoutId.h"
#include "Biome/Noise/Strategy/BiomeFastNoiseEditor.h"
#include "Biome/Island/IslandBiomeFastNoiseTestSupport.h"
#include "Layout/Runtime/ChunkWorldLayoutRuntimeComponent.h"
#include "Layout/Assets/LayoutCompositeModuleAsset.h"
#include "Layout/Assets/LayoutWorldBindingAsset.h"
#include "Layout/Planning/LayoutActiveBiomeSampling.h"
#include "Layout/Planning/LayoutSiteReservation.h"
#include "Layout/Planning/LayoutPlanningWindowStore.h"
#include "Layout/Runtime/LayoutWorldBindingRuntimeHelpers.h"
#include "Layout/Solver/LayoutProfileSolver.h"
#include "Layout/Streaming/LayoutStreamingWindow.h"
#include "Layout/Terrain/LayoutWorldBindingTerrainFit.h"
#include "Layout/Terrain/LayoutTerrainSampling.h"
#include "Layout/Testing/LayoutProfileJsonFixture.h"
#include "Layout/Testing/LayoutTestWorldSupport.h"
#include "Layout/Support/LayoutWorldTestUtilities.h"
#include "Layout/Support/LayoutTestUtilities.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Contracts/LayoutContractModeSelection.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Contracts/LayoutContractPipeline.h"
#include "HAL/PlatformProcess.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Runtime/LayoutRealizationWritePlan.h"
#include "../../../../PorismDIMsWorldGeneratorExtension/Private/Layout/Solver/LayoutRegionScheduleSolverFacade.h"

using namespace PorismLayoutTestUtilities;
using namespace PorismLayoutWorldTestUtilities;
using namespace LayoutRegionScheduleSolverFacade;

namespace
{
	const TCHAR* const ConstantPositiveFastNoise = TEXT("AAAAAIA/");
	const FLayoutId DirectRootPlacementPolicyId(TEXT("DirectRootExplicit"));
	constexpr float DirectRootTimeoutBudgetSeconds = 1.0e-6f;
	const FString CoordinatorFallbackMessageSubstring =
		TEXT("fell back to the legacy recursive body after coordinator failure");

	FLayoutNoiseCoordinateSettings MakeTestCoordinateSettings(const UWorldGenDef* const WorldGenDef)
	{
		FLayoutNoiseCoordinateSettings Settings;
		Settings.BaseBlockSize = WorldGenDef != nullptr ? WorldGenDef->BaseBlockSize : 100;
		Settings.NoiseScale = WorldGenDef != nullptr ? WorldGenDef->NoiseScale : FVector::OneVector;
		Settings.NoiseCoordinateOffset = WorldGenDef != nullptr ? WorldGenDef->NoiseCoordinateOffset : FIntVector::ZeroValue;
		return Settings;
	}

	bool PumpRuntimeComponentUntil(
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

	int32 CountDistinctSupportSurfaceZs(const FLayoutSteppedTerrainSupportMap& SupportMap)
	{
		TSet<int32> DistinctSupportSurfaceZs;
		for (const FLayoutSteppedTerrainSupportSample& Sample : SupportMap.SupportSamples)
		{
			DistinctSupportSurfaceZs.Add(Sample.SupportSurfaceZ);
		}

		return DistinctSupportSurfaceZs.Num();
	}

	FCompiledStructuralInputs BuildCompiledStructuralInputsForRequest(
		const FLayoutRegionSolveRequest& Request)
	{
		return BuildCompiledStructuralInputs(BuildSolveContext(Request));
	}

	ULayoutModuleAsset* CreateSingleCellRuntimeVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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

	ULayoutModuleAsset* CreateSingleCellRuntimeJunctionVerticalAccessModule(UObject* Outer, const TCHAR* BaseName)
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

	ULayoutModuleAsset* CreateSingleCellRuntimeStructuralModule(UObject* Outer, const TCHAR* BaseName)
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

	ULayoutModuleAsset* CreateSingleCellRuntimeEntryStructuralModule(UObject* Outer, const TCHAR* BaseName)
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
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Entry, ELayoutCellIntent::Interior},
			{
				MakeFaceRule(ELayoutFaceDirection::PosX, MakeTags({LayoutGameplayTags::FaceEntry}), OpenAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegX, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::NegY, OpenTags, OpenSolidAndEntryTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor, PrimaryTraversal),
				MakeFaceRule(ELayoutFaceDirection::PosZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor),
				MakeFaceRule(ELayoutFaceDirection::NegZ, SolidTags, SolidTags, ELayoutFaceOccupancyPolicy::AllowsEmptyOrFilledNeighbor)
			});
	}

	ULayoutModuleAsset* CreateSingleCellRuntimeEntryModule(UObject* Outer, const TCHAR* BaseName)
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

	void ConfigureFlatActiveBiomeSurface(UWorldGenDef* const WorldGenDef, const FName RowName)
	{
		check(WorldGenDef != nullptr);
		WorldGenDef->WorldBiomes.Reset();
		WorldGenDef->WorldBiomesDT = nullptr;
		WorldGenDef->WorldGen.Reset();
		WorldGenDef->WorldGenRun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);

		FBiomeDualData Row;
		Row.BiomeName = RowName.ToString();
		Row.Domain = ConstantPositiveFastNoise;
		Row.DualSwitch = ConstantPositiveFastNoise;
		Row.GenARun = NewObject<UBiomeFastNoiseEditor>(WorldGenDef);
		Row.DomainOver = 1.0f;
		Row.GenU_Mat1.AddDefaulted();
		WorldGenDef->WorldBiomes.Add(Row);
	}

	void SetSolidColumnSurface(
		AChunkWorldExtended* const World,
		const FIntPoint& ColumnXY,
		const int32 SurfaceZ)
	{
		for (int32 Z = 0; Z <= SurfaceZ; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(ColumnXY.X, ColumnXY.Y, Z), SinfullMaterial, false);
		}
	}

	void BuildOneCellPerimeterCutFixture(
		AChunkWorldExtended* const World,
		const int32 ImmediateNorthSurfaceZ,
		const int32 OuterNorthSurfaceZ)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
		}

		SetSolidColumnSurface(World, FIntPoint(8, 8), 5);
		SetSolidColumnSurface(World, FIntPoint(8, 7), ImmediateNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(8, 6), OuterNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(8, 9), 5);
		SetSolidColumnSurface(World, FIntPoint(7, 8), 5);
		SetSolidColumnSurface(World, FIntPoint(9, 8), 5);
	}

	void BuildTwoCellPerimeterCutFixture(
		AChunkWorldExtended* const World,
		const int32 ImmediateNorthSurfaceZ = 7,
		const int32 OuterNorthSurfaceZ = 8)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
		}

		SetSolidColumnSurface(World, FIntPoint(7, 8), 3);
		SetSolidColumnSurface(World, FIntPoint(8, 8), 5);
		SetSolidColumnSurface(World, FIntPoint(6, 8), 4);
		SetSolidColumnSurface(World, FIntPoint(7, 7), ImmediateNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(7, 6), OuterNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(7, 9), 4);
		SetSolidColumnSurface(World, FIntPoint(8, 7), ImmediateNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(8, 6), OuterNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(8, 9), 4);
		SetSolidColumnSurface(World, FIntPoint(9, 8), 4);
	}

	void BuildTwoByTwoPerimeterCutFixture(
		AChunkWorldExtended* const World,
		const int32 ImmediateNorthSurfaceZ = 7,
		const int32 OuterNorthSurfaceZ = 8)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 9, Z), EmptyMaterial, false);
		}

		SetSolidColumnSurface(World, FIntPoint(7, 8), 3);
		SetSolidColumnSurface(World, FIntPoint(7, 9), 3);
		SetSolidColumnSurface(World, FIntPoint(8, 8), 5);
		SetSolidColumnSurface(World, FIntPoint(8, 9), 5);
		SetSolidColumnSurface(World, FIntPoint(6, 8), 4);
		SetSolidColumnSurface(World, FIntPoint(6, 9), 4);
		SetSolidColumnSurface(World, FIntPoint(7, 7), ImmediateNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(7, 6), OuterNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(7, 10), 4);
		SetSolidColumnSurface(World, FIntPoint(8, 7), ImmediateNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(8, 6), OuterNorthSurfaceZ);
		SetSolidColumnSurface(World, FIntPoint(8, 10), 4);
		SetSolidColumnSurface(World, FIntPoint(9, 8), 4);
		SetSolidColumnSurface(World, FIntPoint(9, 9), 4);
	}

	void BuildTwoByTwoPartialRingPerimeterRampFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 6, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 6, 4));
	}

	void BuildTwoByTwoMissingPerimeterFoundationFillFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 6, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 6, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 9, 4));
	}

	void BuildTwoByTwoPartialRingPerimeterRampWithoutFoundationFillFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 6, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 6, 4));
	}

	void BuildTwoCellSupportablePerimeterRampFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 8, 4));
	}

	void BuildTwoByTwoSparseSupportFootprint(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 5));
	}

	void BuildTwoByTwoPureGapFootprint(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		}
	}

	void BuildTwoByTwoLeadingGapSparseSupportFootprint(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
	}

	void BuildTwoByTwoSupportablePerimeterRampFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 3));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 10, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 10, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 9, 4));
	}

	void BuildTwoByTwoPerimeterRampWithoutFoundationFillFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 10, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 9, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 10, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 10, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 9, 4));
	}

	void BuildTwoCellPerimeterRampWithoutFoundationFillFixture(AChunkWorldExtended* const World)
	{
		for (int32 Z = 0; Z <= 16; ++Z)
		{
			World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
			World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
		}

		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 8, 5));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(6, 8, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(7, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 7, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(8, 9, 4));
		FLayoutTestWorldSupport::WriteSurfaceBlock(World, FIntVector(9, 8, 4));
	}

	FLayoutWorldBindingPlacementPolicy BuildPlacementPolicy(
		const int32 TerrainSearchStartZ,
		const int32 TerrainSearchDepthBlocks,
		const int32 TerrainSampleGridSpacing,
		const int32 HeightIgnoreThreshold,
		const bool bAllowFoundationFill,
		const int32 MaxFoundationDepth,
		const bool bAllowPerimeterRampTransition = false)
	{
		FLayoutWorldBindingPlacementPolicy Settings;
		Settings.SurfaceSearch.TerrainSearchStartZ = TerrainSearchStartZ;
		Settings.SurfaceSearch.TerrainSearchDepthBlocks = TerrainSearchDepthBlocks;
		Settings.TerrainSampleGridSpacing = TerrainSampleGridSpacing;
		Settings.HeightIgnoreThreshold = HeightIgnoreThreshold;
		Settings.TerrainTransition.bAllowFoundationFill = bAllowFoundationFill;
		Settings.TerrainTransition.MaxFoundationDepth = MaxFoundationDepth;
		Settings.TerrainTransition.bAllowPerimeterRampTransition = bAllowPerimeterRampTransition;
		return Settings;
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

	ULayoutCompositeModuleAsset* CreateTwoCellStampComposite(
		UObject* Outer,
		AChunkWorldExtended* World,
		const TCHAR* CompositeName,
		const int32 FirstMaterialIndex,
		const int32 SecondMaterialIndex)
	{
		ULayoutModuleAsset* FirstLeaf = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_RuntimeCompositeFirstLeaf"),
			FirstMaterialIndex,
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
			TEXT("LayoutModule_RuntimeCompositeSecondLeaf"),
			SecondMaterialIndex,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior});
		SecondLeaf->Roles = {ELayoutModuleRole::Boundary};
		SecondLeaf->FaceRules.PosX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegX.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegY.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.PosZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;
		SecondLeaf->FaceRules.NegZ.BoundaryRequirement = ELayoutFaceBoundaryRequirement::MustFaceExterior;

		ULayoutCompositeModuleAsset* Composite = NewObject<ULayoutCompositeModuleAsset>(Outer, CompositeName);
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
			TEXT("LayoutModule_DirectRootParent"),
			ParentMaterial,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		OutParentModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};

		OutChildModule = CreateOneCellStampModule(
			Outer,
			World,
			TEXT("LayoutModule_DirectRootChild"),
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
		ChildModuleEntry.EntryId = TEXT("DirectRootChildShell");
		ChildModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ChildModuleEntry.ModuleSettings.Module = OutChildModule;
		ULayoutRegionContentSetAsset* ChildContentSet = CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootChild"),
			{ChildModuleEntry});

		OutChildProfile = CreateProfile(
			Outer,
			TEXT("LayoutProfile_DirectRootChild"),
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

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootParentChild"),
			{ParentModuleEntry, ChildRegionEntry});
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
			TEXT("LayoutModule_DirectRootSingleLeaf"),
			MaterialIndex,
			{ELayoutCellIntent::Boundary, ELayoutCellIntent::Interior, ELayoutCellIntent::Core});
		OutModule->Roles = {ELayoutModuleRole::Boundary, ELayoutModuleRole::Interior};

		FLayoutRegionContentEntry ModuleEntry;
		ModuleEntry.EntryId = TEXT("DirectRootSingleLeaf");
		ModuleEntry.ContentKind = ELayoutRegionContentKind::Module;
		ModuleEntry.ModuleSettings.Module = OutModule;

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootSingleLeaf"),
			{ModuleEntry});
	}

	ULayoutProfileAsset* CreateExplicitRootParentProfile(UObject* Outer)
	{
		return CreateProfile(
			Outer,
			TEXT("LayoutProfile_DirectRootParent"),
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
			TEXT("LayoutProfile_DirectRootSingleLeaf"),
			FIntPoint(1, 1),
			FIntPoint(1, 1),
			1,
			0,
			false);
	}

	ULayoutRegionContentSetAsset* CreateExplicitRootCompositeContentSet(
		UObject* Outer,
		AChunkWorldExtended* World,
		ULayoutCompositeModuleAsset*& OutComposite,
		const int32 FirstMaterial,
		const int32 SecondMaterial)
	{
		OutComposite = CreateTwoCellStampComposite(
			Outer,
			World,
			TEXT("LayoutComposite_DirectRootRuntimePair"),
			FirstMaterial,
			SecondMaterial);

		FLayoutRegionContentEntry CompositeEntry;
		CompositeEntry.EntryId = TEXT("DirectRootRuntimeComposite");
		CompositeEntry.ContentKind = ELayoutRegionContentKind::Module;
		CompositeEntry.ModuleSettings.CompositeModule = OutComposite;

		return CreateRegionContentSet(
			Outer,
			TEXT("LayoutContentSet_DirectRootRuntimeComposite"),
			{CompositeEntry});
	}

	ULayoutProfileAsset* CreateExplicitRootCompositeProfile(UObject* Outer)
	{
		return CreateProfile(
			Outer,
			TEXT("LayoutProfile_DirectRootRuntimeComposite"),
			FIntPoint(2, 1),
			FIntPoint(2, 1),
			1,
			0,
			false);
	}

	FIntVector ResolveExpectedRealizedSitePlacementAnchor(
		const FResolvedLayoutSiteRecord& SiteRecord,
		const FLayoutPlacedModule& Placement,
		const FLayoutFrozenTerrainContract& FrozenContract)
	{
		const FResolvedLayoutSiteSolvedPayload SolvedPayload =
			SiteRecord.GetResolvedSiteSolvedPayload();
		const FIntVector SharedCellSizeInBlocks =
			SolvedPayload.SolveResult.SharedCellSizeInBlocks;
		FIntVector Anchor = UChunkWorldLayoutRuntimeComponent::ComputePlacementAnchorBlockWorldPos(
			SiteRecord,
			Placement,
			SharedCellSizeInBlocks);
		const bool bUsesSteppedOrdinaryRootTerrainAnchor =
			SolvedPayload.SolveResult.RootPlacementKind == ELayoutWorldBindingPlacementKind::OrdinaryRoot
			&& SolvedPayload.SolveResult.SteppedTerrainSupportMap.SharedCellHeightInBlocks > 0
			&& !SolvedPayload.SolveResult.SteppedTerrainSupportMap.SupportSamples.IsEmpty();
		if (SolvedPayload.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::None
			&& (SolvedPayload.SolveResult.RootPlacementKind != ELayoutWorldBindingPlacementKind::OrdinaryRoot
				|| bUsesSteppedOrdinaryRootTerrainAnchor))
		{
			Anchor += FrozenContract.FootprintMinBlockWorldPos
				- UChunkWorldLayoutRuntimeComponent::ComputeFootprintMinBlockWorldPos(
					SiteRecord,
					SharedCellSizeInBlocks);
		}

		return Anchor;
	}

	int32 NormalizeRuntimeCompositeYawRotationSteps(const int32 YawRotationSteps)
	{
		int32 NormalizedSteps = YawRotationSteps % 4;
		if (NormalizedSteps < 0)
		{
			NormalizedSteps += 4;
		}

		return NormalizedSteps;
	}

	FString BuildPartitionSeamSignature(const FLayoutPartitionSeamRecord& SeamRecord)
	{
		return FString::Printf(
			TEXT("%s|%s|%s|%s|%d|%d|%s|%s|%s|%s|%d"),
			*SeamRecord.ParentRegionDebugPath,
			*SeamRecord.OwnerRegionDebugPath,
			*SeamRecord.PassiveRegionDebugPath,
			*SeamRecord.InterfaceFamily.ToString(),
			static_cast<int32>(SeamRecord.OwnerFaceDirection),
			static_cast<int32>(SeamRecord.PassiveFaceDirection),
			*SeamRecord.OwnerStartCell.ToString(),
			*SeamRecord.OwnerEndCell.ToString(),
			*SeamRecord.PassiveStartCell.ToString(),
			*SeamRecord.PassiveEndCell.ToString(),
			SeamRecord.SegmentCount);
	}

	FIntVector RotateRuntimeCompositeCellInFootprintYaw(
		const FIntVector& Cell,
		const FIntPoint& FootprintSize,
		const int32 YawRotationSteps)
	{
		switch (NormalizeRuntimeCompositeYawRotationSteps(YawRotationSteps))
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

	UObject* CreateRuntimeFixtureTestOuter(const TCHAR* BaseName)
	{
		const FString PackageName = FString::Printf(
			TEXT("/Temp/%s_%s"),
			BaseName,
			*FGuid::NewGuid().ToString(EGuidFormats::Digits));
		return CreatePackage(*PackageName);
	}

	int32 BuildDeterministicRuntimeFixtureSolveSeed(const TCHAR* FixtureLabel)
	{
		const uint32 StableHash = GetTypeHash(FString(FixtureLabel));
		return static_cast<int32>((StableHash & 0x7fffffffU) | 1U);
	}

	bool LoadRuntimeFixtureAssets(
		FAutomationTestBase& Test,
		const FString& FixturePath,
		const TCHAR* OuterName,
		FLayoutProfileJsonFixtureAssets& OutAssets)
	{
		FString Json;
		Test.TestTrue(TEXT("Runtime fixture file exists"), FPaths::FileExists(FixturePath));
		Test.TestTrue(TEXT("Runtime fixture loads"), FFileHelper::LoadFileToString(Json, *FixturePath));
		if (Json.IsEmpty())
		{
			return false;
		}

		UObject* Outer = CreateRuntimeFixtureTestOuter(OuterName);
		TArray<FString> ImportIssues;
		Test.TestTrue(TEXT("Runtime fixture imports"), FLayoutProfileJsonFixture::ImportFromString(Json, Outer, OutAssets, ImportIssues));
		if (!Test.TestEqual(TEXT("Runtime fixture imports without issues"), ImportIssues.Num(), 0))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
		}

		return OutAssets.Profile != nullptr;
	}

	bool BuildAuthoredCastleMultiLevelChildReplacementRuntimeFixtureAssets(
		FAutomationTestBase& Test,
		FLayoutProfileJsonFixtureAssets& OutImportedAssets)
	{
		const FString FixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Castle_MultiLevelChildReplacement.json"));
		if (!LoadRuntimeFixtureAssets(
				Test,
				FixturePath,
				TEXT("LayoutRuntimeAuthoredCastleMultiLevelChildReplacementFixture"),
				OutImportedAssets)
			|| !Test.TestNotNull(TEXT("Authored castle runtime fixture imports a content set"), OutImportedAssets.ContentSet.Get()))
		{
			return false;
		}

		const FLayoutRegionContentEntry* ImportedRoomEntry = OutImportedAssets.ContentSet->Entries.FindByPredicate([](const FLayoutRegionContentEntry& Entry)
		{
			return Entry.EntryId == FLayoutId(TEXT("Room"))
				&& Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
		});
		if (!Test.TestNotNull(TEXT("Authored castle runtime fixture keeps the Room child entry"), ImportedRoomEntry))
		{
			return false;
		}

		Test.TestTrue(
			TEXT("Authored castle runtime fixture keeps child host-vertical-access contribution enabled"),
			ImportedRoomEntry->ChildRegionSettings.bContributesHostVerticalAccess);
		if (!Test.TestNotNull(
				TEXT("Authored castle runtime fixture keeps the multi-level room child profile"),
				ImportedRoomEntry->ChildRegionSettings.RegionProfile.Get()))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("Authored castle runtime fixture keeps the 2-story child level count"),
			ImportedRoomEntry->ChildRegionSettings.RegionProfile->LevelCount,
			2);
		if (OutImportedAssets.Profile != nullptr)
		{
			Test.TestEqual(
				TEXT("Authored castle runtime fixture keeps the exact-one vertical-access constraint on the imported root profile"),
				OutImportedAssets.Profile->VerticalAccessCount,
				1);
			Test.TestEqual(
				TEXT("Authored castle runtime fixture keeps the exact vertical-access count mode on the imported root profile"),
				OutImportedAssets.Profile->VerticalAccessCountMode,
				ELayoutCountConstraintMode::Exact);
		}
		return true;
	}

	bool BuildAuthoredCastleMixedHeightChildWallSeamRuntimeFixtureAssets(
		FAutomationTestBase& Test,
		FLayoutProfileJsonFixtureAssets& OutImportedAssets)
	{
		const FString BaseFixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Castle_ChildRoomWallSeamTest.json"));
		const FString TallRoomFixturePath = FPaths::ConvertRelativePathToFull(
			FPaths::ProjectDir() / TEXT("Plugins/PorismDIMsWorldGeneratorExtension/Source/PorismDIMsWorldGeneratorExtensionTests/Private/Layout/Fixtures/Data/DA_Profile_Room_2StoryRoom.json"));
		FLayoutProfileJsonFixtureAssets BaseFixtureAssets;
		FLayoutProfileJsonFixtureAssets TallRoomFixtureAssets;
		if (!LoadRuntimeFixtureAssets(
				Test,
				BaseFixturePath,
				TEXT("LayoutRuntimeAuthoredCastleMixedHeightBaseWallSeamFixture"),
				BaseFixtureAssets)
			|| !LoadRuntimeFixtureAssets(
				Test,
				TallRoomFixturePath,
				TEXT("LayoutRuntimeAuthoredCastleMixedHeightTallRoomFixture"),
				TallRoomFixtureAssets)
			|| !Test.TestNotNull(TEXT("Mixed-height runtime fixture reconstructs the base wall-seam profile"), BaseFixtureAssets.Profile.Get())
			|| !Test.TestNotNull(TEXT("Mixed-height runtime fixture reconstructs the base wall-seam content set"), BaseFixtureAssets.ContentSet.Get())
			|| !Test.TestNotNull(TEXT("Mixed-height runtime fixture reconstructs the two-story room profile"), TallRoomFixtureAssets.Profile.Get()))
		{
			return false;
		}

		FLayoutRegionContentEntry* ShortRoomEntry = BaseFixtureAssets.ContentSet->Entries.FindByPredicate([](const FLayoutRegionContentEntry& Entry)
		{
			return Entry.EntryId == FLayoutId(TEXT("Room"))
				&& Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
		});
		if (!Test.TestNotNull(TEXT("Mixed-height runtime fixture resolves the base short room child entry"), ShortRoomEntry)
			|| !Test.TestNotNull(TEXT("Mixed-height runtime fixture resolves the base short room child profile"), ShortRoomEntry != nullptr ? ShortRoomEntry->ChildRegionSettings.RegionProfile.Get() : nullptr))
		{
			return false;
		}

		UObject* SourceOuter = BaseFixtureAssets.Profile.Get()->GetOuter();
		ULayoutProfileAsset* TallRoomProfile = DuplicateObject<ULayoutProfileAsset>(
			TallRoomFixtureAssets.Profile.Get(),
			SourceOuter,
			FName(TEXT("FixtureMixedHeightTallImportedRoomProfile")));
		if (!Test.TestNotNull(TEXT("Mixed-height runtime fixture duplicates the two-story room profile"), TallRoomProfile))
		{
			return false;
		}
		if (TallRoomProfile->ContentSet != nullptr)
		{
			ULayoutRegionContentSetAsset* DuplicatedTallContentSet = DuplicateObject<ULayoutRegionContentSetAsset>(
				TallRoomProfile->ContentSet,
				SourceOuter,
				FName(TEXT("FixtureMixedHeightTallImportedRoomContentSet")));
			if (!Test.TestNotNull(TEXT("Mixed-height runtime fixture duplicates the two-story room content set"), DuplicatedTallContentSet))
			{
				return false;
			}
			TallRoomProfile->ContentSet = DuplicatedTallContentSet;
		}

		TallRoomProfile->MinimumFootprintInCells = FIntPoint(3, 3);
		TallRoomProfile->MaximumFootprintInCells = FIntPoint(3, 3);
		TallRoomProfile->EntryCountMode = ELayoutCountConstraintMode::Exact;
		TallRoomProfile->EntryCount = 1;
		TallRoomProfile->MinEntryCount = 1;
		TallRoomProfile->MaxEntryCount = 1;
		TallRoomProfile->bRequireAllTraversalChannelsReachable = false;

		ShortRoomEntry->ProvidedZoneFeatures.Reset();
		ShortRoomEntry->ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
		ShortRoomEntry->ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoomSleeping);
		ShortRoomEntry->ChildRegionSettings.bContributesHostVerticalAccess = false;

		FLayoutRegionContentEntry TallRoomEntry = *ShortRoomEntry;
		TallRoomEntry.EntryId = TEXT("RoomTall");
		TallRoomEntry.ProvidedZoneFeatures.Reset();
		TallRoomEntry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoom);
		TallRoomEntry.ProvidedZoneFeatures.AddTag(LayoutGameplayTags::FeatureRoomStorage);
		TallRoomEntry.ChildRegionSettings.RegionProfile = TallRoomProfile;
		TallRoomEntry.ChildRegionSettings.bContributesHostVerticalAccess = false;
		BaseFixtureAssets.ContentSet->Entries.Add(TallRoomEntry);

		BaseFixtureAssets.Profile->MinimumFootprintInCells = FIntPoint(9, 9);
		BaseFixtureAssets.Profile->MaximumFootprintInCells = FIntPoint(9, 9);
		BaseFixtureAssets.Profile->ZoneFeatureRequirements.Reset();
		FLayoutZoneFeatureRequirement& ShortRoomRequirement =
			BaseFixtureAssets.Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
		ShortRoomRequirement.RequirementId = FName(TEXT("ShortRoom"));
		ShortRoomRequirement.Zone = ELayoutPlacementZone::Interior;
		ShortRoomRequirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoomSleeping);
		ShortRoomRequirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
		ShortRoomRequirement.MinCount = 1;
		ShortRoomRequirement.MaxCount = 1;

		FLayoutZoneFeatureRequirement& TallRoomRequirement =
			BaseFixtureAssets.Profile->ZoneFeatureRequirements.AddDefaulted_GetRef();
		TallRoomRequirement.RequirementId = FName(TEXT("TallRoom"));
		TallRoomRequirement.Zone = ELayoutPlacementZone::Interior;
		TallRoomRequirement.RequiredFeatures.AddTag(LayoutGameplayTags::FeatureRoomStorage);
		TallRoomRequirement.MatchMode = ELayoutZoneFeatureMatchMode::Any;
		TallRoomRequirement.MinCount = 1;
		TallRoomRequirement.MaxCount = 1;
		BaseFixtureAssets.Profile->ContentSet = BaseFixtureAssets.ContentSet.Get();

		FString ExportedJson;
		FString ExportError;
		if (!Test.TestTrue(
				TEXT("Mixed-height runtime fixture exports to JSON"),
				FLayoutProfileJsonFixture::ExportToString(
					BaseFixtureAssets.Profile.Get(),
					BaseFixtureAssets.ContentSet.Get(),
					ExportedJson,
					ExportError)))
		{
			Test.AddError(ExportError);
			return false;
		}
		if (!Test.TestTrue(TEXT("Mixed-height runtime fixture export should not report an error"), ExportError.IsEmpty()))
		{
			Test.AddError(ExportError);
			return false;
		}

		UObject* ImportOuter = CreateRuntimeFixtureTestOuter(TEXT("LayoutRuntimeAuthoredCastleMixedHeightChildWallSeam"));
		TArray<FString> ImportIssues;
		if (!Test.TestTrue(
				TEXT("Mixed-height runtime fixture re-imports from JSON"),
				FLayoutProfileJsonFixture::ImportFromString(ExportedJson, ImportOuter, OutImportedAssets, ImportIssues)))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
			return false;
		}
		if (!Test.TestEqual(
				TEXT("Mixed-height runtime fixture re-imports without issues"),
				ImportIssues.Num(),
				0))
		{
			for (const FString& Issue : ImportIssues)
			{
				Test.AddInfo(Issue);
			}
			return false;
		}

		const FLayoutRegionContentEntry* ImportedShortRoomEntry = OutImportedAssets.ContentSet->Entries.FindByPredicate([](const FLayoutRegionContentEntry& Entry)
		{
			return Entry.EntryId == FLayoutId(TEXT("Room"))
				&& Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
		});
		const FLayoutRegionContentEntry* ImportedTallRoomEntry = OutImportedAssets.ContentSet->Entries.FindByPredicate([](const FLayoutRegionContentEntry& Entry)
		{
			return Entry.EntryId == FLayoutId(TEXT("RoomTall"))
				&& Entry.ContentKind == ELayoutRegionContentKind::ChildRegion;
		});
		if (!Test.TestNotNull(TEXT("Re-imported mixed-height runtime fixture keeps the short room child entry"), ImportedShortRoomEntry)
			|| !Test.TestNotNull(TEXT("Re-imported mixed-height runtime fixture keeps the tall room child entry"), ImportedTallRoomEntry))
		{
			return false;
		}

		Test.TestEqual(
			TEXT("Re-imported mixed-height runtime fixture keeps the short room child level count"),
			ImportedShortRoomEntry->ChildRegionSettings.RegionProfile->LevelCount,
			1);
		Test.TestEqual(
			TEXT("Re-imported mixed-height runtime fixture keeps the tall room child level count"),
			ImportedTallRoomEntry->ChildRegionSettings.RegionProfile->LevelCount,
			2);
		return true;
	}

	bool TryCreateRuntimeDirectRootHarness(
		FAutomationTestBase& Test,
		FLayoutWorldTestHarness& OutHarness)
	{
		OutHarness = CreateChunkWorldHarness(GetTransientPackage());
		return Test.TestNotNull(TEXT("Chunk-world harness creates a runtime component"), OutHarness.RuntimeComponent)
			&& Test.TestNotNull(TEXT("Chunk-world harness creates a transient chunk world"), OutHarness.World);
	}

	struct FRuntimeStandaloneDirectRootComparison
	{
		bool bRuntimeSolved = false;
		FString DirectRootRegionPath;
		FString RuntimeFailureReason;
		FResolvedLayoutSiteRecord RuntimeSiteRecord;
		FLayoutRegionSolveScheduleResult RuntimeScheduleResult;
		FLayoutRegionSolveScheduleResult StandaloneScheduleResult;
	};

	void LogDirectRootComparisonFailureDetails(
		FAutomationTestBase& Test,
		const FRuntimeStandaloneDirectRootComparison& Comparison)
	{
		Test.AddInfo(FString::Printf(TEXT("Runtime failure: %s"), *Comparison.RuntimeFailureReason));
		for (const FLayoutValidationMessage& Message : Comparison.RuntimeScheduleResult.MergedSolveResult.Messages)
		{
			Test.AddInfo(FString::Printf(TEXT("Runtime message: %s"), *Message.Message));
		}

		Test.AddInfo(FString::Printf(TEXT("Standalone failure: %s"), *Comparison.StandaloneScheduleResult.FailureReason));
		for (const FLayoutValidationMessage& Message : Comparison.StandaloneScheduleResult.MergedSolveResult.Messages)
		{
			Test.AddInfo(FString::Printf(TEXT("Standalone message: %s"), *Message.Message));
		}
	}

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

	FLayoutWorldBindingRuntimeView BuildAutomationExplicitRootRuntimeView(
		ULayoutProfileAsset* const Profile,
		ULayoutRegionContentSetAsset* const ContentSet,
		const FLayoutRootSolveBudgetSettings& SolveBudget,
		const FLayoutWorldBindingPlacementPolicy& PlacementPolicy)
	{
		return LayoutWorldBindingRuntimeHelpers::BuildExplicitRootRuntimeView(
			Profile,
			ContentSet,
			SolveBudget,
			PlacementPolicy);
	}

	/** Mirrors the transitional submit-side terrain evidence builder used by explicit-root runtime publication. */
	FLayoutFrozenTerrainBiomeAdapterInput BuildStandaloneTerrainBiomeAdapterInput(
		const FLayoutId ArtifactId,
		const FLayoutId ModePlanId,
		const FName EligibleBiomeRowName,
		const FIntVector SiteCenterBlockWorldPos,
		const FIntVector FootprintMinBlockWorldPos,
		const FIntVector SharedCellSizeInBlocks,
		const FIntPoint FootprintInCells,
		TFunctionRef<int32(int32 CellBlockX, int32 CellBlockY)> ResolveSurfaceZ,
		const int32 MaxFoundationDepth)
	{
		FLayoutFrozenTerrainBiomeAdapterInput Input;
		Input.ArtifactId = ArtifactId;
		Input.ModePlanId = ModePlanId;
		Input.EligibleBiomeRowName = EligibleBiomeRowName;
		Input.SiteCenterBlockWorldPos = SiteCenterBlockWorldPos;
		Input.FootprintMinBlockWorldPos = FootprintMinBlockWorldPos;
		Input.FootprintSizeInBlocks = FIntPoint(
			FootprintInCells.X * SharedCellSizeInBlocks.X,
			FootprintInCells.Y * SharedCellSizeInBlocks.Y);
		Input.bHasTerrainPlacementEvidence = true;
		Input.bHasFiniteSearchBounds = true;
		Input.bHasSampledColumnEvidence = true;
		Input.SearchDepthBlocks = MaxFoundationDepth > 0 ? MaxFoundationDepth : SharedCellSizeInBlocks.Z;
		Input.SearchMinBlockXY = FIntPoint(FootprintMinBlockWorldPos.X, FootprintMinBlockWorldPos.Y);
		Input.SearchMaxBlockXY = FIntPoint(
			FootprintMinBlockWorldPos.X + FMath::Max(0, FootprintInCells.X - 1) * SharedCellSizeInBlocks.X,
			FootprintMinBlockWorldPos.Y + FMath::Max(0, FootprintInCells.Y - 1) * SharedCellSizeInBlocks.Y);

		Input.SurfaceSamples.Reserve(FootprintInCells.X * FootprintInCells.Y);
		Input.TerrainPlacementCells.Reserve(FootprintInCells.X * FootprintInCells.Y);
		for (int32 CellY = 0; CellY < FootprintInCells.Y; ++CellY)
		{
			for (int32 CellX = 0; CellX < FootprintInCells.X; ++CellX)
			{
				const int32 CellBlockX = FootprintMinBlockWorldPos.X + CellX * SharedCellSizeInBlocks.X;
				const int32 CellBlockY = FootprintMinBlockWorldPos.Y + CellY * SharedCellSizeInBlocks.Y;
				const int32 CellBottomZ = SiteCenterBlockWorldPos.Z;
				const int32 CellTopZ = SiteCenterBlockWorldPos.Z + SharedCellSizeInBlocks.Z;
				const int32 SurfaceZ = ResolveSurfaceZ(CellBlockX, CellBlockY);

				FLayoutTerrainSurfaceSample Sample;
				Sample.bIsValid = true;
				Sample.BlockXY = FIntPoint(CellBlockX, CellBlockY);
				Sample.SurfaceBlockWorldPos = FIntVector(CellBlockX, CellBlockY, SurfaceZ);
				Input.SurfaceSamples.Add(MoveTemp(Sample));

				FLayoutTerrainPlacementCellEvidence CellEvidence;
				CellEvidence.Cell = FIntVector(CellX, CellY, 0);
				CellEvidence.bPlaceableForSelectedMode = true;
				CellEvidence.TerrainStageIndex = 0;
				CellEvidence.VerticalShiftBlocks = 0;
				CellEvidence.ProvenanceId = TEXT("BuildStandaloneTerrainBiomeAdapterInput");
				if (SurfaceZ > CellTopZ)
				{
					CellEvidence.bHasExcavationEvidence = true;
					CellEvidence.bHasLocalOverlapZ = true;
					CellEvidence.OverlapMinLocalZ = FLayoutLocalBlockCoord8(0);
					CellEvidence.OverlapMaxLocalZ = FLayoutLocalBlockCoord8(FMath::Min(SurfaceZ - CellBottomZ, 255));
				}
				else if (SurfaceZ < CellBottomZ)
				{
					CellEvidence.bHasFoundationFillEvidence = true;
					CellEvidence.RequiredFoundationDepth = FMath::Max(1, CellBottomZ - SurfaceZ - 1);
					CellEvidence.FoundationMaterial = SinfullMaterial;
				}
				else
				{
					CellEvidence.bHasClearanceEvidence = true;
				}

				const bool bOnBoundary = CellX == 0 || CellX == FootprintInCells.X - 1
					|| CellY == 0 || CellY == FootprintInCells.Y - 1;
				if (bOnBoundary && MaxFoundationDepth > 0)
				{
					int32 OutsideBlockX = CellBlockX;
					int32 OutsideBlockY = CellBlockY;
					if (CellX == 0) { OutsideBlockX -= SharedCellSizeInBlocks.X; }
					else if (CellX == FootprintInCells.X - 1) { OutsideBlockX += SharedCellSizeInBlocks.X; }
					if (CellY == 0) { OutsideBlockY -= SharedCellSizeInBlocks.Y; }
					else if (CellY == FootprintInCells.Y - 1) { OutsideBlockY += SharedCellSizeInBlocks.Y; }
					const int32 Delta = FMath::Abs(SurfaceZ - ResolveSurfaceZ(OutsideBlockX, OutsideBlockY));
					const int32 HalfFoundation = MaxFoundationDepth / 2;
					CellEvidence.EntryTraversability = Delta <= HalfFoundation
						? ELayoutEntryTraversabilityVerdict::Walkable
						: (Delta <= MaxFoundationDepth
							? ELayoutEntryTraversabilityVerdict::RampNeeded
							: ELayoutEntryTraversabilityVerdict::CliffEdge);
				}

				Input.TerrainPlacementCells.Add(MoveTemp(CellEvidence));
			}
		}
		return Input;
	}

	/** Mirrors the world-facing request-owned carrier publication that runtime applies after contract precompute. */
	void PublishStandaloneRequestOwnedSteppedCarriersOnMergedSolveResult(
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
		else if (SolveRequest.ForcedPlacementBundleInsertions.Num() > MergedSolveResult.ForcedPlacementBundleInsertions.Num())
		{
			MergedSolveResult.ForcedPlacementBundleInsertions =
				SolveRequest.ForcedPlacementBundleInsertions;
		}
		if (MergedSolveResult.ForcedPlacementBundleInsertions.IsEmpty())
		{
			for (const FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
			{
				if (!RegionResult.ForcedPlacementBundleInsertions.IsEmpty())
				{
					MergedSolveResult.ForcedPlacementBundleInsertions =
						RegionResult.ForcedPlacementBundleInsertions;
					break;
				}
			}
		}

		if (MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
		{
			MergedSolveResult.RequestOwnedRequiredRouteConstraints =
				SolveRequest.RequiredRouteConstraints;
		}
		if (MergedSolveResult.RequestOwnedRequiredRouteConstraints.IsEmpty())
		{
			for (const FLayoutRegionSolveResult& RegionResult : InOutScheduleResult.RegionResults)
			{
				if (!RegionResult.RequiredRouteConstraints.IsEmpty())
				{
					MergedSolveResult.RequestOwnedRequiredRouteConstraints =
						RegionResult.RequiredRouteConstraints;
					break;
				}
			}
		}

		if (MergedSolveResult.PlannedCells.IsEmpty() && !SolveRequest.PlannedCells.IsEmpty())
		{
			MergedSolveResult.PlannedCells = SolveRequest.PlannedCells;
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRequiresObservedChunksTest,
	"PorismExtension.Layout.Runtime.RealizationRequiresObservedChunks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationRequiresObservedChunksTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeObserved"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	ULayoutWorldBindingAsset* WorldBinding = NewObject<ULayoutWorldBindingAsset>(
		GetTransientPackage(),
		TEXT("LayoutWorldBinding_RuntimeObserved"));
	WorldBinding->BindingId = TEXT("RuntimeObserved");
	WorldBinding->BaseCellDimensionsBlocks = FIntVector(2, 1, 1);
	WorldBinding->TemplatePlacementZOffsetBlocks = 3;
	Harness.RuntimeComponent->SetLayoutWorldBindings({WorldBinding});
	ULayoutModuleAsset* StampModule = CreateOneCellStampModule(
		GetTransientPackage(),
		Harness.World,
		TEXT("LayoutModule_RuntimeObserved"),
		SinfullMaterial,
		{ELayoutCellIntent::Boundary});

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& FirstPlacement = Placements.AddDefaulted_GetRef();
	FirstPlacement.Cell = FIntVector(0, 0, 0);
	FirstPlacement.Intent = ELayoutCellIntent::Boundary;
	FirstPlacement.Module = StampModule;

	FLayoutPlacedModule& SecondPlacement = Placements.AddDefaulted_GetRef();
	SecondPlacement.Cell = FIntVector(1, 0, 0);
	SecondPlacement.Intent = ELayoutCellIntent::Boundary;
	SecondPlacement.Module = StampModule;

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(2, 1), Placements);
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FrontendSelection.WorldBindingId = WorldBinding->BindingId;
	FrontendSelection.WorldBindingCandidateId = TEXT("RuntimeObservedCandidate");
	SiteRecord.SetWorldBindingFrontendSelection(FrontendSelection);
	FLayoutSiteSolveSourceSelection SolveSourceSelection;
	SolveSourceSelection.LayoutProfile = LayoutProfile;
	SiteRecord.SetSiteSolveSourceSelection(SolveSourceSelection);

	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, 3), EmptyMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, 3), EmptyMaterial, false);

	const FIntPoint ReservationKey(0, 0);
	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(ReservationKey, SiteRecord);

	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();
	TestEqual(
		TEXT("Unobserved sites are not stamped before the required chunk window arrives"),
		Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 3), ERessourceType::MaterialIndex, 0),
		EmptyMaterial);

	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestEqual(TEXT("One cached site record remains after realization"), SiteRecords.Num(), 1);
	TestTrue(
		TEXT("Observed chunk windows allow the cached site to realize"),
		SiteRecords.Num() == 1 && SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized);
	TestTrue(
		TEXT("Realized sites are committed so later passes do not restamp them"),
		SiteRecords.Num() == 1 && SiteRecords[0].GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);
	TestEqual(TEXT("The first occupied cell stamps its template exactly once"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 3), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("The second occupied cell stamps its template exactly once"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 3), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationStampsCompositeSiteWithoutLiveLeafModuleTest,
	"PorismExtension.Layout.Runtime.StampsCompositeSiteWithoutLiveLeafModule",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationStampsCompositeSiteWithoutLiveLeafModuleTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = CreateTwoCellStampComposite(
		GetTransientPackage(),
		Harness.World,
		TEXT("LayoutComposite_RuntimeObservedNoLiveLeafModule"),
		SinfullMaterial,
		42);
	const FLayoutValidationResult CompositeValidation = Composite->ValidateCompositeModule();
	if (!TestTrue(TEXT("Runtime composite site fixture validates the authored composite"), CompositeValidation.IsValid()))
	{
		return false;
	}

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.ModuleSnapshotId = FLayoutProfileSolver::BuildCompositeModuleSnapshot(Composite).SnapshotId;
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.BundleBoundsCells = Composite->GetBoundsCells();
	Placement.OccupiedLocalCells = Composite->GetOccupiedLocalCells();
	for (const FLayoutCompositeModuleCell& CompositeCell : Composite->Cells)
	{
		FLayoutPlacedLocalCellFaceRuleSnapshot& LocalCellDescriptor =
			Placement.LocalCellFaceRules.AddDefaulted_GetRef();
		LocalCellDescriptor.LocalCell = CompositeCell.LocalCell;
		LocalCellDescriptor.TemplatePath = CompositeCell.Module->Template.ToSoftObjectPath();
		LocalCellDescriptor.RelativeYawRotationSteps = CompositeCell.RelativeYawRotationSteps;
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), nullptr, FIntPoint(2, 1), Placements);
	FLayoutWorldBindingSiteFrontendSelection FrontendSelection;
	FrontendSelection.WorldBindingId = TEXT("CompositeThinLeafBinding");
	FrontendSelection.WorldBindingCandidateId = TEXT("CompositeThinLeafCandidate");
	SiteRecord.SetWorldBindingFrontendSelection(FrontendSelection);
	FLayoutRootPublicationMetadata PublicationMetadata;
	PublicationMetadata.RootSolveId = TEXT("CompositeThinLeafRoot");
	PublicationMetadata.RootCandidateId = TEXT("CompositeThinLeafCandidate");
	PublicationMetadata.RootPlacementPolicyId = TEXT("CompositeThinLeafPolicy");
	SiteRecord.SetRootPublicationMetadata(PublicationMetadata);
	FLayoutSiteSolveSourceSelection SolveSourceSelection;
	SolveSourceSelection.SolveSeed = 5151;
	SiteRecord.SetSiteSolveSourceSelection(SolveSourceSelection);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = Composite->GetSharedCellSizeInBlocks();

	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, 0), EmptyMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, 0), EmptyMaterial, false);

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->RunQueuedLayoutWorkForTesting();

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Composite-backed site record realizes without a live leaf Placement.Module"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}
	const FResolvedLayoutSiteRecord& RealizedSiteRecord = SiteRecords[0];
	const FResolvedLayoutSiteLocationMetadata RealizedLocationMetadata =
		RealizedSiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutWorldBindingSiteFrontendSelection RealizedFrontendSelection =
		RealizedSiteRecord.GetWorldBindingFrontendSelection();
	const FLayoutRootPublicationMetadata RealizedPublicationMetadata =
		RealizedSiteRecord.GetRootPublicationMetadata();
	const FLayoutSiteSolveSourceSelection RealizedSolveSourceSelection =
		RealizedSiteRecord.GetSiteSolveSourceSelection();
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the zero-offset cached site center"),
		RealizedLocationMetadata.SiteCenterBlockWorldPos,
		FIntVector(8, 8, 0));
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the cached binding id on the named frontend carrier"),
		RealizedFrontendSelection.WorldBindingId,
		FrontendSelection.WorldBindingId);
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the cached candidate id on the named frontend carrier"),
		RealizedFrontendSelection.WorldBindingCandidateId,
		FrontendSelection.WorldBindingCandidateId);
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the cached root solve id on the named publication carrier"),
		RealizedPublicationMetadata.RootSolveId,
		PublicationMetadata.RootSolveId);
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the cached root candidate id on the named publication carrier"),
		RealizedPublicationMetadata.RootCandidateId,
		PublicationMetadata.RootCandidateId);
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the cached root placement-policy id on the named publication carrier"),
		RealizedPublicationMetadata.RootPlacementPolicyId,
		PublicationMetadata.RootPlacementPolicyId);
	TestEqual(
		TEXT("Composite-backed site without a live leaf module preserves the cached solve seed on the named solve-source carrier"),
		RealizedSolveSourceSelection.SolveSeed,
		SolveSourceSelection.SolveSeed);
	TestTrue(
		TEXT("Composite-backed site without a live leaf module keeps the layout profile empty on the named solve-source carrier"),
		RealizedSolveSourceSelection.LayoutProfile == nullptr);
	TestTrue(
		TEXT("Composite-backed site without a live leaf module keeps the content-set carrier empty on the named solve-source carrier"),
		!RealizedSolveSourceSelection.ContentSet.IsValid());
	TestEqual(TEXT("Composite-backed site stamps the root leaf material from its frozen descriptor"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 0), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Composite-backed site stamps the shadow leaf material from its frozen descriptor"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 0), ERessourceType::MaterialIndex, 0), 42);

	FLayoutWorldTestHarness FailedHarness = CreateChunkWorldHarness(GetTransientPackage());
	if (!TestNotNull(TEXT("Failure harness creates a runtime component"), FailedHarness.RuntimeComponent)
		|| FailedHarness.World == nullptr)
	{
		return false;
	}
	TArray<FLayoutPlacedModule> FailedPlacements = Placements;
	FailedPlacements[0].LocalCellFaceRules[1].TemplatePath = FSoftObjectPath(TEXT("/Game/Test/MissingCompositeLeaf.MissingCompositeLeaf"));
	FResolvedLayoutSiteRecord FailedSiteRecord = BuildSolvedSiteRecord(FIntVector(24, 8, 0), nullptr, FIntPoint(2, 1), FailedPlacements);
	FailedSiteRecord.SolveResult.SharedCellSizeInBlocks = Composite->GetSharedCellSizeInBlocks();
	FailedHarness.World->SetBlockValueByBlockWorldPos(FIntVector(23, 8, 0), EmptyMaterial, false);
	FailedHarness.World->SetBlockValueByBlockWorldPos(FIntVector(24, 8, 0), EmptyMaterial, false);
	FailedHarness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), FailedSiteRecord);
	FailedHarness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	FailedHarness.RuntimeComponent->RunQueuedLayoutWorkForTesting();
	TestEqual(TEXT("Composite preflight leaves root anchor untouched when shadow descriptor cannot load"), FailedHarness.World->GetBlockValueByBlockWorldPos(FIntVector(23, 8, 0), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Composite preflight leaves shadow anchor untouched when shadow descriptor cannot load"), FailedHarness.World->GetBlockValueByBlockWorldPos(FIntVector(24, 8, 0), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootCompositeSolveApplyWorkflowTest,
	"PorismExtension.Layout.Runtime.DirectRoot.CompositeSolveApplyWorkflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootCompositeSolveApplyWorkflowWithoutBundleBoundsTest,
	"PorismExtension.Layout.Runtime.DirectRoot.CompositeSolveApplyWorkflowWithoutBundleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootAsyncSubmitRehydratesLeafPlacementCarrierTest,
	"PorismExtension.Layout.Runtime.DirectRoot.AsyncSubmitRehydratesLeafPlacementCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootAsyncSubmitRehydratesCompositePlacementCarrierTest,
	"PorismExtension.Layout.Runtime.DirectRoot.AsyncSubmitRehydratesCompositePlacementCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootApplyRehydratesLeafPlacementCarrierTest,
	"PorismExtension.Layout.Runtime.DirectRoot.ApplyRehydratesLeafPlacementCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootApplyRehydratesCompositePlacementCarrierTest,
	"PorismExtension.Layout.Runtime.DirectRoot.ApplyRehydratesCompositePlacementCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeDirectRootAsyncSubmitRehydratesLeafPlacementCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (Harness.World) ConfigureProceduralSurface(Harness.World->WorldGenDef, 0);
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
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

	ULayoutRegionContentSetAsset* const EffectiveContentSet =
		LayoutWorldBindingRuntimeHelpers::ResolveRuntimePreferredContentSet(Profile);
	const FLayoutWorldBindingRuntimeView RuntimeView =
		BuildAutomationExplicitRootRuntimeView(
			Profile,
			EffectiveContentSet,
			MakeAutomationExplicitRootSolveBudget(),
			MakeAutomationExplicitRootPlacementPolicy());

	bool bCompleted = false;
	bool bSucceeded = false;
	FResolvedLayoutSiteRecord AsyncSiteRecord;
	FLayoutRegionSolveScheduleResult AsyncScheduleResult;
	FString AsyncFailureReason;
	Harness.RuntimeComponent->SubmitExplicitRootLayoutSiteSolve(
		FIntVector(8, 8, 1),
		RuntimeView,
		2724,
		[&](const bool bAsyncSucceeded, const FResolvedLayoutSiteRecord& SiteRecord, const FLayoutRegionSolveScheduleResult& ScheduleResult, const FString& FailureReason)
		{
			bSucceeded = bAsyncSucceeded;
			AsyncSiteRecord = SiteRecord;
			AsyncScheduleResult = ScheduleResult;
			AsyncFailureReason = FailureReason;
			bCompleted = true;
		});

	TestTrue(TEXT("Async explicit-root submit publishes one completion for the leaf-carrier fixture"), PumpRuntimeComponentUntil(Harness.RuntimeComponent, [&bCompleted]() { return bCompleted; }));
	if (!TestTrue(TEXT("Async explicit-root submit succeeds for the leaf-carrier fixture"), bSucceeded))
	{
		AddError(AsyncFailureReason);
		return false;
	}
	if (!TestTrue(TEXT("Async explicit-root submit keeps one site placement"), AsyncSiteRecord.SolveResult.Placements.Num() == 1)
		|| !TestTrue(TEXT("Async explicit-root submit keeps one merged schedule placement"), AsyncScheduleResult.MergedSolveResult.Placements.Num() == 1)
		|| !TestTrue(TEXT("Async explicit-root submit keeps one region schedule result placement"), AsyncScheduleResult.RegionResults.Num() == 1 && AsyncScheduleResult.RegionResults[0].SolveResult.Placements.Num() == 1))
	{
		return false;
	}

	TestTrue(TEXT("Async explicit-root submit rehydrates the leaf module carrier on the published site record"), AsyncSiteRecord.SolveResult.Placements[0].Module == LeafModule);
	TestTrue(TEXT("Async explicit-root submit rehydrates the leaf module carrier on the published merged schedule result"), AsyncScheduleResult.MergedSolveResult.Placements[0].Module == LeafModule);
	TestTrue(TEXT("Async explicit-root submit rehydrates the leaf module carrier on the published region schedule result"), AsyncScheduleResult.RegionResults[0].SolveResult.Placements[0].Module == LeafModule);
	TestNull(TEXT("Async explicit-root submit keeps no composite carrier on the leaf site record"), AsyncSiteRecord.SolveResult.Placements[0].CompositeModule.Get());
	TestNull(TEXT("Async explicit-root submit keeps no composite carrier on the leaf region schedule result"), AsyncScheduleResult.RegionResults[0].SolveResult.Placements[0].CompositeModule.Get());
	TestTrue(TEXT("Async explicit-root apply succeeds with the rehydrated leaf site record"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(AsyncSiteRecord, &AsyncFailureReason, false));
	return true;
}

bool FLayoutRuntimeDirectRootAsyncSubmitRehydratesCompositePlacementCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (Harness.World) ConfigureProceduralSurface(Harness.World->WorldGenDef, 1);
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootCompositeContentSet(
		GetTransientPackage(),
		Harness.World,
		Composite,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* Profile = CreateExplicitRootCompositeProfile(GetTransientPackage());
	Profile->ContentSet = ContentSet;
	if (!TestNotNull(TEXT("Async explicit-root composite fixture builds a composite"), Composite))
	{
		return false;
	}

	ULayoutRegionContentSetAsset* const EffectiveContentSet =
		LayoutWorldBindingRuntimeHelpers::ResolveRuntimePreferredContentSet(Profile);
	const FLayoutWorldBindingRuntimeView RuntimeView =
		BuildAutomationExplicitRootRuntimeView(
			Profile,
			EffectiveContentSet,
			MakeAutomationExplicitRootSolveBudget(),
			MakeAutomationExplicitRootPlacementPolicy());

	bool bCompleted = false;
	bool bSucceeded = false;
	FResolvedLayoutSiteRecord AsyncSiteRecord;
	FLayoutRegionSolveScheduleResult AsyncScheduleResult;
	FString AsyncFailureReason;
	Harness.RuntimeComponent->SubmitExplicitRootLayoutSiteSolve(
		FIntVector(8, 8, 2),
		RuntimeView,
		2725,
		[&](const bool bAsyncSucceeded, const FResolvedLayoutSiteRecord& SiteRecord, const FLayoutRegionSolveScheduleResult& ScheduleResult, const FString& FailureReason)
		{
			bSucceeded = bAsyncSucceeded;
			AsyncSiteRecord = SiteRecord;
			AsyncScheduleResult = ScheduleResult;
			AsyncFailureReason = FailureReason;
			bCompleted = true;
		});

	TestTrue(TEXT("Async explicit-root submit publishes one completion for the composite-carrier fixture"), PumpRuntimeComponentUntil(Harness.RuntimeComponent, [&bCompleted]() { return bCompleted; }));
	if (!TestTrue(TEXT("Async explicit-root submit succeeds for the composite-carrier fixture"), bSucceeded))
	{
		AddError(AsyncFailureReason);
		return false;
	}
	if (!TestTrue(TEXT("Async explicit-root submit keeps one composite site placement"), AsyncSiteRecord.SolveResult.Placements.Num() == 1)
		|| !TestTrue(TEXT("Async explicit-root submit keeps one composite merged schedule placement"), AsyncScheduleResult.MergedSolveResult.Placements.Num() == 1)
		|| !TestTrue(TEXT("Async explicit-root submit keeps one composite region schedule result placement"), AsyncScheduleResult.RegionResults.Num() == 1 && AsyncScheduleResult.RegionResults[0].SolveResult.Placements.Num() == 1))
	{
		return false;
	}

	TestTrue(TEXT("Async explicit-root submit rehydrates the composite carrier on the published site record"), AsyncSiteRecord.SolveResult.Placements[0].CompositeModule == Composite);
	TestTrue(TEXT("Async explicit-root submit rehydrates the composite carrier on the published merged schedule result"), AsyncScheduleResult.MergedSolveResult.Placements[0].CompositeModule == Composite);
	TestTrue(TEXT("Async explicit-root submit rehydrates the composite carrier on the published region schedule result"), AsyncScheduleResult.RegionResults[0].SolveResult.Placements[0].CompositeModule == Composite);
	TestTrue(TEXT("Async explicit-root composite site keeps occupied local cells after rehydration"), AsyncSiteRecord.SolveResult.Placements[0].OccupiedLocalCells.Num() == 2);
	TestTrue(TEXT("Async explicit-root composite region schedule result keeps occupied local cells after rehydration"), AsyncScheduleResult.RegionResults[0].SolveResult.Placements[0].OccupiedLocalCells.Num() == 2);
	TestTrue(TEXT("Async explicit-root apply succeeds with the rehydrated composite site record"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(AsyncSiteRecord, &AsyncFailureReason, false));
	return true;
}

bool FLayoutRuntimeDirectRootApplyRehydratesLeafPlacementCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (Harness.World) ConfigureProceduralSurface(Harness.World->WorldGenDef, 0);
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
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

	FResolvedLayoutSiteRecord SiteRecord;
	FLayoutRegionSolveScheduleResult ScheduleResult;
	FString FailureReason;
	const bool bSolved = Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
		FIntVector(8, 8, 1),
		Profile,
		2723,
		SiteRecord,
		ScheduleResult,
		&FailureReason);
	if (!TestTrue(TEXT("Runtime direct-root solve succeeds before carrier rehydration coverage"), bSolved))
	{
		AddError(FailureReason);
		return false;
	}

	for (FLayoutPlacedModule& Placement : SiteRecord.SolveResult.Placements)
	{
		Placement.Module = nullptr;
		Placement.CompositeModule = nullptr;
	}

	TestTrue(TEXT("Runtime direct-root apply succeeds after solved leaf placement carriers are stripped"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &FailureReason, false));
	if (!FailureReason.IsEmpty())
	{
		AddInfo(FailureReason);
	}

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Carrier-rehydrated runtime apply commits exactly one realized site"),
		SiteRecords.Num() == 1
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	FLayoutFrozenTerrainContract ExpectedFrozenContract; const bool bExpectedContractValid = Harness.RuntimeComponent->TryGetAcceptedFrozenTerrainContract(SiteRecords[0], ExpectedFrozenContract, nullptr) && !ExpectedFrozenContract.ContractId.IsNone();
	if (!TestTrue(
		TEXT("Carrier-rehydrated runtime apply keeps one valid runtime terrain-fit anchor on the realized site record"),
		bExpectedContractValid))
	{
		AddError(TEXT("Frozen contract not found or has none ContractId"));
		return false;
	}

	bool bVerifiedLeafStamp = false;
	for (const FLayoutPlacedModule& Placement : SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.Placements)
	{
		const FIntVector Anchor =
			ResolveExpectedRealizedSitePlacementAnchor(SiteRecords[0], Placement, ExpectedFrozenContract);
		const int32 MaterialAtAnchor = Harness.World->GetBlockValueByBlockWorldPos(
			Anchor,
			ERessourceType::MaterialIndex,
			0);
		if (Placement.Module == LeafModule)
		{
			bVerifiedLeafStamp = bVerifiedLeafStamp || MaterialAtAnchor == SinfullMaterial;
		}
	}

	TestTrue(TEXT("Carrier-rehydrated runtime apply stamps the leaf placement template"), bVerifiedLeafStamp);
	return true;
}

bool FLayoutRuntimeDirectRootApplyRehydratesCompositePlacementCarrierTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	if (Harness.World) ConfigureProceduralSurface(Harness.World->WorldGenDef, 1);
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}

	ULayoutCompositeModuleAsset* Composite = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootCompositeContentSet(
		GetTransientPackage(),
		Harness.World,
		Composite,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* Profile = CreateExplicitRootCompositeProfile(GetTransientPackage());
	Profile->ContentSet = ContentSet;
	if (!TestNotNull(TEXT("Runtime direct-root composite apply fixture builds a composite"), Composite))
	{
		return false;
	}

	FResolvedLayoutSiteRecord SiteRecord;
	FLayoutRegionSolveScheduleResult ScheduleResult;
	FString FailureReason;
	const bool bSolved = Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
		FIntVector(8, 8, 2),
		Profile,
		2726,
		SiteRecord,
		ScheduleResult,
		&FailureReason);
	if (!TestTrue(TEXT("Runtime direct-root composite solve succeeds before carrier rehydration coverage"), bSolved))
	{
		AddError(FailureReason);
		return false;
	}

	for (FLayoutPlacedModule& Placement : SiteRecord.SolveResult.Placements)
	{
		Placement.Module = nullptr;
		Placement.CompositeModule = nullptr;
	}

	TestTrue(TEXT("Runtime direct-root apply succeeds after solved composite placement carriers are stripped"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &FailureReason, false));
	if (!FailureReason.IsEmpty())
	{
		AddInfo(FailureReason);
	}

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Composite carrier-rehydrated runtime apply commits exactly one realized site"),
		SiteRecords.Num() == 1
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	FLayoutFrozenTerrainContract ExpectedFrozenContract; const bool bExpectedContractValid = Harness.RuntimeComponent->TryGetAcceptedFrozenTerrainContract(SiteRecords[0], ExpectedFrozenContract, nullptr) && !ExpectedFrozenContract.ContractId.IsNone();
	if (!TestTrue(
		TEXT("Composite carrier-rehydrated runtime apply keeps one valid runtime terrain-fit anchor on the realized site record"),
		bExpectedContractValid))
	{
		AddError(TEXT("Frozen contract not found or has none ContractId"));
		return false;
	}

	bool bVerifiedCompositeCarrier = false;
	bool bVerifiedFirstCompositeStamp = false;
	bool bVerifiedSecondCompositeStamp = false;
	for (const FLayoutPlacedModule& Placement : SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.Placements)
	{
		const FIntVector Anchor =
			ResolveExpectedRealizedSitePlacementAnchor(SiteRecords[0], Placement, ExpectedFrozenContract);
		if (Placement.CompositeModule == Composite)
		{
			bVerifiedCompositeCarrier = true;
			bVerifiedFirstCompositeStamp = bVerifiedFirstCompositeStamp
				|| Harness.World->GetBlockValueByBlockWorldPos(Anchor, ERessourceType::MaterialIndex, 0) == SinfullMaterial;
			bVerifiedSecondCompositeStamp = bVerifiedSecondCompositeStamp
				|| Harness.World->GetBlockValueByBlockWorldPos(Anchor + FIntVector(1, 0, 0), ERessourceType::MaterialIndex, 0) == 42;
		}
	}

	TestTrue(TEXT("Composite carrier-rehydrated runtime apply restores composite placement carrier"), bVerifiedCompositeCarrier);
	TestTrue(TEXT("Composite carrier-rehydrated runtime apply stamps first composite leaf template"), bVerifiedFirstCompositeStamp);
	TestTrue(TEXT("Composite carrier-rehydrated runtime apply stamps second composite leaf template"), bVerifiedSecondCompositeStamp);
	return true;
}

bool FLayoutRuntimeDirectRootCompositeSolveApplyWorkflowTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}
	// Air at Z=2, solid floor/ceiling, surface at Z=49.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAUUkduQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAABuEoO5AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));

	ULayoutCompositeModuleAsset* Composite = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootCompositeContentSet(
		Harness.World,
		Harness.World,
		Composite,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* Profile = CreateExplicitRootCompositeProfile(Harness.World);
	Profile->bUndergroundPlacement = true;
	Profile->ContentSet = ContentSet;
	if (Composite != nullptr)
	{
		Composite->AddToRoot();
		for (const FLayoutCompositeModuleCell& CompositeCell : Composite->Cells)
		{
			if (CompositeCell.Module != nullptr)
			{
				CompositeCell.Module->AddToRoot();
				if (UChunkStructureTemplate* const Template =
					CompositeCell.Module->Template.LoadSynchronous())
				{
					Template->AddToRoot();
				}
			}
		}
	}
	ContentSet->AddToRoot();
	Profile->AddToRoot();

	FLayoutValidationResult CompositeValidation;
	if (Composite != nullptr)
	{
		CompositeValidation = Composite->ValidateCompositeModule();
	}
	else
	{
		CompositeValidation.AddError(TEXT("Composite runtime explicit-root fixture was not created."));
	}
	if (!TestTrue(TEXT("Direct-root runtime composite fixture validates before solve"), CompositeValidation.IsValid()))
	{
		return false;
	}

	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, 2), EmptyMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, 2), EmptyMaterial, false);

	FResolvedLayoutSiteRecord SiteRecord;
	FLayoutRegionSolveScheduleResult ScheduleResult;
	FString FailureReason;
	const bool bSolved = Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
		FIntVector(8, 8, 2),
		Profile,
		2717,
		SiteRecord,
		ScheduleResult,
		&FailureReason);
	if (!TestTrue(TEXT("Runtime direct-root solve succeeds for the simple composite profile"), bSolved))
	{
		AddError(FailureReason);
		return false;
	}

	TestTrue(TEXT("Direct-root composite site is marked solved"), SiteRecord.bLayoutSolved);
	TestTrue(TEXT("Direct-root composite merged solve succeeds"), SiteRecord.SolveResult.bSucceeded);
	TestEqual(TEXT("Direct-root composite solve keeps one placement bundle"), SiteRecord.SolveResult.Placements.Num(), 1);
	const FResolvedLayoutSiteLocationMetadata SiteLocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutRootPublicationMetadata SitePublicationMetadata =
		SiteRecord.GetRootPublicationMetadata();
	const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection =
		SiteRecord.GetSiteSolveSourceSelection();
	TestEqual(TEXT("Direct-root composite site keeps the solved site center on the named location carrier"), SiteLocationMetadata.SiteCenterBlockWorldPos, FIntVector(8, 8, 2));
	TestEqual(TEXT("Direct-root composite site keeps the deterministic direct-root solve id on the named publication carrier"), SitePublicationMetadata.RootSolveId, FLayoutId(TEXT("DirectRoot/X=8 Y=8 Z=2")));
	TestEqual(TEXT("Direct-root composite site keeps the deterministic direct-root candidate id on the named publication carrier"), SitePublicationMetadata.RootCandidateId, FLayoutId(TEXT("DirectRoot/X=8 Y=8 Z=2")));
	TestEqual(TEXT("Direct-root composite site keeps the deterministic direct-root placement-policy id on the named publication carrier"), SitePublicationMetadata.RootPlacementPolicyId, FLayoutId(TEXT("DirectRootExplicit")));
	TestEqual(TEXT("Direct-root composite site keeps the explicit solve-source seed on the named solve-source carrier"), SiteSolveSourceSelection.SolveSeed, 2717);
	TestTrue(TEXT("Direct-root composite site keeps the explicit layout profile on the named solve-source carrier"), SiteSolveSourceSelection.LayoutProfile == Profile);
	TestTrue(TEXT("Direct-root composite site keeps the explicit content set on the named solve-source carrier"), SiteSolveSourceSelection.ContentSet == ContentSet);
	if (!TestTrue(
		TEXT("Direct-root composite placement preserves occupied local cells"),
		SiteRecord.SolveResult.Placements.Num() == 1
			&& SiteRecord.SolveResult.Placements[0].CompositeModule == Composite
			&& SiteRecord.SolveResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		return false;
	}

	TestTrue(TEXT("Runtime direct-root apply succeeds for the solved composite site"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &FailureReason, false));
	if (!FailureReason.IsEmpty())
	{
		AddInfo(FailureReason);
	}

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Composite direct-root runtime apply commits exactly one realized site"),
		SiteRecords.Num() == 1
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	FLayoutFrozenTerrainContract ExpectedFrozenContract; const bool bExpectedContractValid = Harness.RuntimeComponent->TryGetAcceptedFrozenTerrainContract(SiteRecords[0], ExpectedFrozenContract, nullptr) && !ExpectedFrozenContract.ContractId.IsNone();
	if (!TestTrue(
		TEXT("Composite direct-root apply keeps one valid runtime terrain-fit anchor on the realized site record"),
		bExpectedContractValid))
	{
		AddError(TEXT("Frozen contract not found or has none ContractId"));
		return false;
	}
	const FLayoutPlacedModule& Placement =
		SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.Placements[0];
	TestTrue(
		TEXT("Composite runtime direct-root apply keeps the realized composite bundle on the committed site record"),
		Placement.CompositeModule == Composite && Placement.OccupiedLocalCells.Num() == 2);

	FString DuplicateFailureReason;
	TestFalse(
		TEXT("Composite runtime direct-root apply rejects a second commit of the same solved site"),
		Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &DuplicateFailureReason, false));
	TestTrue(
		TEXT("Composite runtime duplicate apply explains the existing committed reservation"),
		DuplicateFailureReason.Contains(TEXT("existing committed record")) || DuplicateFailureReason.Contains(TEXT("already own the reservation")));
	return true;
}

bool FLayoutRuntimeDirectRootCompositeSolveApplyWorkflowWithoutBundleBoundsTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}
	// Air at Z=2, solid floor/ceiling, surface at Z=49.
	ConfigureProceduralCavity(Harness.World->WorldGenDef,
		TEXT("HgAdAAQAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAUUkduQAAAAABBAAAAAAAAAAAAAAAgL8AAAAAAAAAAAAAAABuEoO5AAAAAAEEAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAJwzorsAAAAA"));

	ULayoutCompositeModuleAsset* Composite = nullptr;
	ULayoutRegionContentSetAsset* ContentSet = CreateExplicitRootCompositeContentSet(
		GetTransientPackage(),
		Harness.World,
		Composite,
		SinfullMaterial,
		42);
	ULayoutProfileAsset* Profile = CreateExplicitRootCompositeProfile(GetTransientPackage());
	Profile->bUndergroundPlacement = true;
	Profile->ContentSet = ContentSet;

	FLayoutValidationResult CompositeValidation;
	if (Composite != nullptr)
	{
		CompositeValidation = Composite->ValidateCompositeModule();
	}
	else
	{
		CompositeValidation.AddError(TEXT("Composite runtime explicit-root thin-carrier fixture was not created."));
	}
	if (!TestTrue(TEXT("Direct-root runtime thin-carrier composite fixture validates before solve"), CompositeValidation.IsValid()))
	{
		return false;
	}

	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, 2), EmptyMaterial, false);
	Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, 2), EmptyMaterial, false);

	FResolvedLayoutSiteRecord SiteRecord;
	FLayoutRegionSolveScheduleResult ScheduleResult;
	FString FailureReason;
	const bool bSolved = Harness.RuntimeComponent->TrySolveExplicitRootLayoutSite(
		FIntVector(8, 8, 2),
		Profile,
		2718,
		SiteRecord,
		ScheduleResult,
		&FailureReason);
	if (!TestTrue(TEXT("Runtime direct-root solve succeeds for the thin-carrier composite profile"), bSolved))
	{
		AddError(FailureReason);
		return false;
	}

	const FResolvedLayoutSiteLocationMetadata SiteLocationMetadata =
		SiteRecord.GetResolvedSiteLocationMetadata();
	const FLayoutRootPublicationMetadata SitePublicationMetadata =
		SiteRecord.GetRootPublicationMetadata();
	const FLayoutSiteSolveSourceSelection SiteSolveSourceSelection =
		SiteRecord.GetSiteSolveSourceSelection();
	TestEqual(TEXT("Direct-root thin-carrier composite site keeps the solved site center on the named location carrier"), SiteLocationMetadata.SiteCenterBlockWorldPos, FIntVector(8, 8, 2));
	TestEqual(TEXT("Direct-root thin-carrier composite site keeps the deterministic direct-root solve id on the named publication carrier"), SitePublicationMetadata.RootSolveId, FLayoutId(TEXT("DirectRoot/X=8 Y=8 Z=2")));
	TestEqual(TEXT("Direct-root thin-carrier composite site keeps the deterministic direct-root candidate id on the named publication carrier"), SitePublicationMetadata.RootCandidateId, FLayoutId(TEXT("DirectRoot/X=8 Y=8 Z=2")));
	TestEqual(TEXT("Direct-root thin-carrier composite site keeps the deterministic direct-root placement-policy id on the named publication carrier"), SitePublicationMetadata.RootPlacementPolicyId, FLayoutId(TEXT("DirectRootExplicit")));
	TestEqual(TEXT("Direct-root thin-carrier composite site keeps the explicit solve-source seed on the named solve-source carrier"), SiteSolveSourceSelection.SolveSeed, 2718);
	TestTrue(TEXT("Direct-root thin-carrier composite site keeps the explicit layout profile on the named solve-source carrier"), SiteSolveSourceSelection.LayoutProfile == Profile);
	TestTrue(TEXT("Direct-root thin-carrier composite site keeps the explicit content set on the named solve-source carrier"), SiteSolveSourceSelection.ContentSet == ContentSet);
	if (!TestTrue(
		TEXT("Direct-root thin-carrier composite placement preserves occupied local cells before clearing BundleBoundsCells"),
		SiteRecord.SolveResult.Placements.Num() == 1
			&& SiteRecord.SolveResult.Placements[0].CompositeModule == Composite
			&& SiteRecord.SolveResult.Placements[0].OccupiedLocalCells.Num() == 2))
	{
		return false;
	}

	SiteRecord.SolveResult.Placements[0].BundleBoundsCells = FIntVector::ZeroValue;
	TestTrue(TEXT("Runtime direct-root apply succeeds for the solved thin-carrier composite site"), Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &FailureReason, false));
	if (!FailureReason.IsEmpty())
	{
		AddInfo(FailureReason);
	}

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(
		TEXT("Thin-carrier composite direct-root runtime apply commits exactly one realized site"),
		SiteRecords.Num() == 1
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bLayoutRealized
			&& SiteRecords[0].GetResolvedSiteRuntimeState().bHasBeenCommittedToChunkWorld);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	FLayoutFrozenTerrainContract ExpectedFrozenContract; const bool bExpectedContractValid = Harness.RuntimeComponent->TryGetAcceptedFrozenTerrainContract(SiteRecords[0], ExpectedFrozenContract, nullptr) && !ExpectedFrozenContract.ContractId.IsNone();
	if (!TestTrue(
		TEXT("Thin-carrier composite direct-root apply keeps one valid runtime terrain-fit anchor on the realized site record"),
		bExpectedContractValid))
	{
		AddError(TEXT("Frozen contract not found or has none ContractId"));
		return false;
	}
	const FLayoutPlacedModule& Placement =
		SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.Placements[0];
	TestTrue(
		TEXT("Thin-carrier composite runtime direct-root apply keeps the realized composite bundle on the committed site record"),
		Placement.CompositeModule == Composite && Placement.OccupiedLocalCells.Num() == 2);

	FString DuplicateFailureReason;
	TestFalse(
		TEXT("Thin-carrier composite runtime direct-root apply rejects a second commit of the same solved site"),
		Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(SiteRecord, &DuplicateFailureReason, false));
	TestTrue(
		TEXT("Thin-carrier composite runtime duplicate apply explains the existing committed reservation"),
		DuplicateFailureReason.Contains(TEXT("existing committed record")) || DuplicateFailureReason.Contains(TEXT("already own the reservation")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootApplyRejectsMissingCachedWritePlanTest,
	"PorismExtension.Layout.Runtime.DirectRoot.ApplyRejectsMissingCachedWritePlan",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeDirectRootApplyRejectsStaleCachedWritePlanMetadataTest,
	"PorismExtension.Layout.Runtime.DirectRoot.ApplyRejectsStaleCachedWritePlanMetadata",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeDirectRootApplyRejectsMissingCachedWritePlanTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeMissingCachedWritePlan"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutPlacedModule Placement;
	Placement.Cell = FIntVector::ZeroValue;
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("RuntimeMissingCachedWritePlanModule"));
	Placement.TemplatePath = FSoftObjectPath();
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), {Placement});
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	FLayoutRootPublicationMetadata PublicationMetadata;
	PublicationMetadata.RootSolveId = FLayoutId(TEXT("DirectRoot/MissingCachedWritePlan"));
	PublicationMetadata.RootCandidateId = PublicationMetadata.RootSolveId;
	PublicationMetadata.RootPlacementPolicyId = DirectRootPlacementPolicyId;
	SiteRecord.SetRootPublicationMetadata(PublicationMetadata);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("SolvedArtifact.MissingCachedWritePlan"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;
	SiteRecord.CachedRealizationWritePlanId = FLayoutId(TEXT("RealizationWritePlan.Site.SolvedArtifact.MissingCachedWritePlan.FrozenTerrain.MissingCachedWritePlan"));
	SiteRecord.CachedRealizationWritePlanHash = 12345;
	SiteRecord.CachedRealizationProvenanceId = PublicationMetadata.RootSolveId;
	SiteRecord.CachedRealizationIdempotencyMarker = FLayoutId(TEXT("RealizationApplied.MissingCachedWritePlan"));
	SiteRecord.CachedFrozenTerrainContractId = FLayoutId(TEXT("FrozenTerrain.MissingCachedWritePlan"));
	SiteRecord.CachedChunkWritePassCount = 3;
	SiteRecord.CachedTemplatePlacementCount = 1;
	SiteRecord.bWritePlanReady = true;

	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FrozenTerrainContract.ContractId = SiteRecord.CachedFrozenTerrainContractId;
	FrozenTerrainContract.SiteCenterBlockWorldPos = SiteRecord.SiteCenterBlockWorldPos;
	FrozenTerrainContract.FootprintMinBlockWorldPos = FIntVector(8, 8, 2);
	FrozenTerrainContract.SharedCellSizeInBlocks = SiteRecord.SolveResult.SharedCellSizeInBlocks;
	FrozenTerrainContract.FootprintSizeInCells = FIntPoint(1, 1);
	FrozenTerrainContract.ActiveCells = {{FIntVector::ZeroValue}};
	FrozenTerrainContract.CellContracts = {{FIntVector::ZeroValue, ELayoutFrozenTerrainCellContract::Active}};

	FString FailureReason;
	TestFalse(
		TEXT("Write-plan-ready direct-root apply rejects missing cached realization write plan"),
		Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(
			SiteRecord,
			&FailureReason,
			false,
			&FrozenTerrainContract));
	TestTrue(
		TEXT("Missing cached realization write plan failure is explicit"),
		FailureReason.Contains(TEXT("missing_cached_write_plan")));
	TestNotEqual(
		TEXT("Missing cached realization write plan does not stamp the template anchor"),
		Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 2), ERessourceType::MaterialIndex, 0),
		SinfullMaterial);
	return true;
}

bool FLayoutRuntimeDirectRootApplyRejectsStaleCachedWritePlanMetadataTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);
	if (Harness.RuntimeComponent == nullptr || Harness.World == nullptr)
	{
		return false;
	}

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeStaleCachedWritePlan"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);

	FLayoutPlacedModule Placement;
	Placement.Cell = FIntVector::ZeroValue;
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("RuntimeStaleCachedWritePlanModule"));
	Placement.TemplatePath = FSoftObjectPath();
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), {Placement});
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	FLayoutRootPublicationMetadata PublicationMetadata;
	PublicationMetadata.RootSolveId = FLayoutId(TEXT("DirectRoot/StaleCachedWritePlan"));
	PublicationMetadata.RootCandidateId = PublicationMetadata.RootSolveId;
	PublicationMetadata.RootPlacementPolicyId = DirectRootPlacementPolicyId;
	SiteRecord.SetRootPublicationMetadata(PublicationMetadata);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("SolvedArtifact.StaleCachedWritePlan"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;

	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FrozenTerrainContract.ContractId = FLayoutId(TEXT("FrozenTerrain.StaleCachedWritePlan"));
	FrozenTerrainContract.SiteCenterBlockWorldPos = SiteRecord.SiteCenterBlockWorldPos;
	FrozenTerrainContract.FootprintMinBlockWorldPos = FIntVector(8, 8, 2);
	FrozenTerrainContract.SharedCellSizeInBlocks = SiteRecord.SolveResult.SharedCellSizeInBlocks;
	FrozenTerrainContract.FootprintSizeInCells = FIntPoint(1, 1);
	FrozenTerrainContract.ActiveCells = {{FIntVector::ZeroValue}};
	FrozenTerrainContract.CellContracts = {{FIntVector::ZeroValue, ELayoutFrozenTerrainCellContract::Active}};

	FLayoutRealizationWritePlan AcceptedWritePlan;
	FString BuildFailureReason;
	TestTrue(
		TEXT("Test fixture can build accepted realization write plan"),
		LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Site,
			SiteRecord.SolvedArtifactId,
			SiteRecord.SolvedArtifactActiveCellCount,
			SiteRecord.SolveResult,
			FrozenTerrainContract,
			AcceptedWritePlan,
			BuildFailureReason));
	if (AcceptedWritePlan.WritePlanId.IsNone())
	{
		AddError(FString::Printf(TEXT("Write-plan fixture failed: %s"), *BuildFailureReason));
		return false;
	}

	Harness.RuntimeComponent->InjectRootRealizationWritePlanForTesting(PublicationMetadata.RootSolveId, AcceptedWritePlan);
	SiteRecord.CachedRealizationWritePlanId = AcceptedWritePlan.WritePlanId;
	SiteRecord.CachedRealizationWritePlanHash = AcceptedWritePlan.WritePlanHash + 1;
	SiteRecord.CachedRealizationProvenanceId = PublicationMetadata.RootSolveId;
	SiteRecord.CachedRealizationIdempotencyMarker = FLayoutId(TEXT("RealizationApplied.StaleCachedWritePlan"));
	SiteRecord.CachedFrozenTerrainContractId = FrozenTerrainContract.ContractId;
	SiteRecord.CachedChunkWritePassCount = AcceptedWritePlan.ChunkWriteBatch.PassOrder.Num();
	SiteRecord.CachedTerrainWriteCount = AcceptedWritePlan.ChunkWriteBatch.TerrainWriteCount;
	SiteRecord.CachedTemplatePlacementCount = AcceptedWritePlan.ChunkWriteBatch.TemplatePlacementCount;
	SiteRecord.bWritePlanReady = true;

	FString FailureReason;
	TestFalse(
		TEXT("Write-plan-ready direct-root apply rejects stale cached realization metadata"),
		Harness.RuntimeComponent->TryApplySolvedExplicitRootLayoutSite(
			SiteRecord,
			&FailureReason,
			false,
			&FrozenTerrainContract));
	TestTrue(
		TEXT("Stale cached realization metadata failure is explicit"),
		FailureReason.Contains(TEXT("Cached realization-prep metadata is stale")));
	TestNotEqual(
		TEXT("Stale cached realization metadata does not stamp the template anchor"),
		Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 2), ERessourceType::MaterialIndex, 0),
		SinfullMaterial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationTerrainFootprintUsesSharedCellSizeCarrierTest,
	"PorismExtension.Layout.Runtime.TerrainFootprintUsesSharedCellSizeCarrier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationTerrainFootprintUsesOccupiedLocalCellsBeforeModuleBoundsTest,
	"PorismExtension.Layout.Runtime.TerrainFootprintUsesOccupiedLocalCellsBeforeModuleBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationTerrainFootprintRebuildsFromLeafModuleWithoutOccupiedLocalCellsTest,
	"PorismExtension.Layout.Runtime.TerrainFootprintRebuildsFromLeafModuleWithoutOccupiedLocalCells",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationTerrainFootprintUsesSharedCellSizeCarrierTest::RunTest(const FString& Parameters)
{
	using namespace PorismLayoutTestUtilities;

	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_RuntimeTerrainFootprintCarrier"));
	SetModuleTemplateSize(Module, FIntVector(99, 77, 16));

	FLayoutPlacedModule Placement;
	Placement.Module = Module;
	Placement.Intent = ELayoutCellIntent::Boundary;

	TestEqual(
		TEXT("Runtime terrain-footprint sizing uses the solved shared-cell-size carrier instead of live module cell metrics for plain leaf placements"),
		UChunkWorldLayoutRuntimeComponent::ResolvePlacementTerrainFootprintSizeInBlocksForTesting(
			Placement,
			FIntVector(8, 8, 8)),
		FIntPoint(8, 8));
	return true;
}

bool FLayoutRuntimeRealizationTerrainFootprintUsesOccupiedLocalCellsBeforeModuleBoundsTest::RunTest(const FString& Parameters)
{
	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_RuntimeTerrainFootprintOccupiedCells"));
	FLayoutPlacedModule Placement;
	Placement.Module = Module;
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.YawRotationSteps = 1;
	Placement.OccupiedLocalCells = {FIntVector(0, 0, 0), FIntVector(0, 1, 0)};

	TestEqual(
		TEXT("Runtime terrain-footprint sizing prefers occupied local cells before live module bounds"),
		UChunkWorldLayoutRuntimeComponent::ResolvePlacementTerrainFootprintSizeInBlocksForTesting(
			Placement,
			FIntVector(8, 8, 8)),
		FIntPoint(8, 16));
	return true;
}

bool FLayoutRuntimeRealizationTerrainFootprintRebuildsFromLeafModuleWithoutOccupiedLocalCellsTest::RunTest(const FString& Parameters)
{
	ULayoutModuleAsset* Module = NewObject<ULayoutModuleAsset>(GetTransientPackage(), TEXT("LayoutModule_RuntimeTerrainFootprintThinCarrier"));
	FLayoutPlacedModule Placement;
	Placement.Module = Module;
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.YawRotationSteps = 1;

	TestEqual(
		TEXT("Runtime terrain-footprint sizing falls back to one occupied leaf cell when OccupiedLocalCells are absent"),
		UChunkWorldLayoutRuntimeComponent::ResolvePlacementTerrainFootprintSizeInBlocksForTesting(
			Placement,
			FIntVector(8, 8, 8)),
		FIntPoint(8, 8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsFoundationFillTest,
	"PorismExtension.Layout.Runtime.BuildsFoundationFillForHillsidePlacements",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsFoundationFillWithMissingPerimeterSamplesTest,
	"PorismExtension.Layout.Runtime.BuildsFoundationFillWithMissingPerimeterSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsPerimeterRampWithMissingPerimeterSamplesTest,
	"PorismExtension.Layout.Runtime.BuildsPerimeterRampWithMissingPerimeterSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampWithMissingPerimeterSamplesTest,
	"PorismExtension.Layout.Runtime.BuildsTwoByTwoOrdinaryRootPerimeterRampWithMissingPerimeterSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsTwoByTwoFoundationFillWithMissingPerimeterSamplesTest,
	"PorismExtension.Layout.Runtime.BuildsTwoByTwoFoundationFillWithMissingPerimeterSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationWithMissingPerimeterSamplesTest,
	"PorismExtension.Layout.Runtime.BuildsTwoByTwoOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationWithMissingPerimeterSamples",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsTwoByTwoFoundationDepthOverrunForSiteTest,
	"PorismExtension.Layout.Runtime.RejectsTwoByTwoFoundationDepthOverrunForSite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsFoundationDepthOverrunForSiteTest,
	"PorismExtension.Layout.Runtime.RejectsFoundationDepthOverrunForSite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsFoundationFillWithTemplatePlacementOffsetFloorAlignmentTest,
	"PorismExtension.Layout.Runtime.BuildsFoundationFillWithTemplatePlacementOffsetFloorAlignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsOrdinaryRootPerimeterRampTest,
	"PorismExtension.Layout.Runtime.BuildsOrdinaryRootPerimeterRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsMultiCellOrdinaryRootPerimeterRampTest,
	"PorismExtension.Layout.Runtime.BuildsMultiCellOrdinaryRootPerimeterRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampTest,
	"PorismExtension.Layout.Runtime.BuildsTwoByTwoOrdinaryRootPerimeterRamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationTest,
	"PorismExtension.Layout.Runtime.BuildsOrdinaryRootPerimeterRampWithoutFoundationFillAuthorization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsMultiCellOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationTest,
	"PorismExtension.Layout.Runtime.BuildsMultiCellOrdinaryRootPerimeterRampWithoutFoundationFillAuthorization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationTest,
	"PorismExtension.Layout.Runtime.BuildsTwoByTwoOrdinaryRootPerimeterRampWithoutFoundationFillAuthorization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsOrdinaryRootDownhillPerimeterCutTest,
	"PorismExtension.Layout.Runtime.BuildsOrdinaryRootDownhillPerimeterCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsMultiCellOrdinaryRootDownhillPerimeterCutTest,
	"PorismExtension.Layout.Runtime.BuildsMultiCellOrdinaryRootDownhillPerimeterCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootDownhillPerimeterCutTest,
	"PorismExtension.Layout.Runtime.BuildsTwoByTwoOrdinaryRootDownhillPerimeterCut",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsOrdinaryRootPerimeterTransitionDepthOverrunTest,
	"PorismExtension.Layout.Runtime.RejectsOrdinaryRootPerimeterTransitionDepthOverrun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsMultiCellOrdinaryRootPerimeterTransitionDepthOverrunTest,
	"PorismExtension.Layout.Runtime.RejectsMultiCellOrdinaryRootPerimeterTransitionDepthOverrun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsTwoByTwoOrdinaryRootPerimeterTransitionDepthOverrunTest,
	"PorismExtension.Layout.Runtime.RejectsTwoByTwoOrdinaryRootPerimeterTransitionDepthOverrun",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationBuildsFoundationFillTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	// Create a mild slope across the future template footprint so the terrain helper
	// chooses the higher anchor and fills the lower side for support.
	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeHill"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy = BuildPlacementPolicy(16, 16, 1, 3, true, 4);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestFoundationFillHillsideSnap"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestFoundationFillHillsideArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;
	SiteRecord.LayoutProfile = nullptr;

	const FIntPoint ReservationKey(0, 0);
	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestFoundationFillHillsideContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}




	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(ReservationKey, SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Terrain-conforming sites can realize successfully on mild slopes without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Terrain-conforming site preserves the structured foundation-fill terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFoundationFill);
	TestEqual(TEXT("The authored template stamps one block lower so the first realized layer overlaps the sampled terrain"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Foundation fill bridges the lower side of the hillside placement"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Foundation fill stops below the overlapping root layer"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsFoundationFillWithMissingPerimeterSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 7, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 6, 4));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 7, 4));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeHillMissingPerimeter"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy = BuildPlacementPolicy(16, 16, 1, 3, true, 4);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestFoundationFillMissingPerimeterSnap"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestFoundationFillMissingPerimeterArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestFoundationFillMissingPerimeterContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 9, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Terrain-conforming site still realizes successfully when only part of the immediate perimeter ring is sampled"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Missing perimeter samples do not change the accepted foundation-fill terrain-fit outcome"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFoundationFill);
	TestEqual(TEXT("Lower footprint column still receives support fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Lower footprint column still fills through the resolved anchor base"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Missing west perimeter column stays untouched when no terrain sample exists there"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Missing south perimeter column stays untouched when no terrain sample exists there"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsPerimeterRampWithMissingPerimeterSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(6, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 6, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 7, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 6, 4));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 7, 4));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimePartialRingPerimeterRamp"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& FirstPlacement = Placements.AddDefaulted_GetRef();
	FirstPlacement.Cell = FIntVector(0, 0, 0);
	FirstPlacement.Intent = ELayoutCellIntent::Boundary;
	FirstPlacement.Module = nullptr;
	FirstPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestPerimeterRampMissingPerimeterSnap"));
	FirstPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FLayoutPlacedModule& SecondPlacement = Placements.AddDefaulted_GetRef();
	SecondPlacement.Cell = FIntVector(1, 0, 0);
	SecondPlacement.Intent = ELayoutCellIntent::Boundary;
	SecondPlacement.Module = nullptr;
	SecondPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestPerimeterRampMissingPerimeterSnap"));
	SecondPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(2, 1), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestPerimeterRampMissingPerimeterArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 2;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestPerimeterRampMissingPerimeterContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Partial-ring ordinary-root perimeter-ramp site realizes successfully when the sampled north side has enough outward terrain"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Partial-ring ordinary-root perimeter-ramp site preserves the accepted ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Partial-ring ordinary-root support fill still raises the lower footprint column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring ordinary-root ramp still raises the available north perimeter sample beside the lower footprint column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring ordinary-root ramp keeps the next northward support column as the bounded terrace base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring ordinary-root ramp does not overfill above the bounded outer terrace base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Missing west perimeter column stays untouched at runtime when no immediate sample exists"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampWithMissingPerimeterSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoPartialRingPerimeterRampFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoPartialRingPerimeterRamp"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("TestTwoByTwoRampMissingPerimeterSnap"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestTwoByTwoRampMissingPerimeterArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestTwoByTwoRampMissingPerimeterContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 9, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 9, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Partial-ring 2x2 ordinary-root perimeter-ramp site realizes successfully when the sampled north side has enough outward terrain"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root perimeter-ramp site preserves the accepted ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root support fill still raises the northwest footprint column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root support fill still raises the southwest footprint column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root ramp still raises the available north perimeter sample beside the northwest footprint column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root ramp still raises the available north perimeter sample beside the northeast footprint column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root ramp keeps the first northward support column as the bounded terrace base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root ramp keeps the second northward support column as the bounded terrace base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root ramp does not overfill above the first bounded outer terrace base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Partial-ring 2x2 ordinary-root ramp does not overfill above the second bounded outer terrace base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Missing west perimeter column beside the north row stays untouched at runtime when no immediate sample exists"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Missing west perimeter column beside the south row stays untouched at runtime when no immediate sample exists"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 9, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsTwoByTwoFoundationFillWithMissingPerimeterSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoMissingPerimeterFoundationFillFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoHillMissingPerimeter"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy = BuildPlacementPolicy(16, 16, 1, 3, true, 4);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("TestTwoByTwoFoundationFillMissingPerimeterSnap"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestTwoByTwoFoundationFillMissingPerimeterArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestTwoByTwoFoundationFillMissingPerimeterContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 9, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 9, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 10, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 10, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("2x2 terrain-conforming site still realizes successfully when only part of the immediate perimeter ring is sampled"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Missing perimeter samples do not change the accepted 2x2 foundation-fill terrain-fit outcome"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFoundationFill);
	TestEqual(TEXT("2x2 lower northwest footprint column still receives support fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 lower southwest footprint column still receives support fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 lower northwest footprint column does not overfill above the resolved support base"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 lower southwest footprint column does not overfill above the resolved support base"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Missing south perimeter column beside the west footprint column stays untouched when no terrain sample exists there"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 10, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Missing south perimeter column beside the east footprint column stays untouched when no terrain sample exists there"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 10, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationWithMissingPerimeterSamplesTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoPartialRingPerimeterRampWithoutFoundationFillFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoPartialRingPerimeterRampNoFoundationFill"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, false, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("TestTwoByTwoRampNoFillMissingPerimeterSnap"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestTwoByTwoRampNoFillMissingPerimeterArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestTwoByTwoRampNoFillMissingPerimeterContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddFreshCreatedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("2x2 partial-ring ordinary-root perimeter-ramp site realizes successfully when generic foundation fill is disabled but the sampled north side alone can support the bounded terrace"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("2x2 partial-ring ordinary-root perimeter-ramp site still preserves the structured ramp terrain-fit outcome when generic foundation fill is disabled"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter-ramp site needs no under-footprint support fill when generic foundation fill is disabled"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter ramp still raises the available north perimeter sample beside the west footprint column without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter ramp still raises the available north perimeter sample beside the east footprint column without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter ramp keeps the first northward support column as the bounded terrace base without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter ramp keeps the second northward support column as the bounded terrace base without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter ramp does not overfill above the first bounded outer terrace base without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 partial-ring runtime perimeter ramp does not overfill above the second bounded outer terrace base without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationRejectsTwoByTwoFoundationDepthOverrunForSiteTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoMissingPerimeterFoundationFillFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoHillReject"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy = BuildPlacementPolicy(16, 16, 1, 3, true, 1);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("Test2x2FoundationDepthRejectPlacement"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 5), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("Test2x2FoundationDepthRejectArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("Test2x2FoundationDepthRejectContract")),
			FIntVector(8, 9, 5),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 3); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bLayoutRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestEqual(TEXT("2x2 foundation-depth rejection keeps one cached resolved site after runtime realization fails"), SiteRecords.Num(), 1);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	const FResolvedLayoutSiteRuntimeState RuntimeState = SiteRecords[0].GetResolvedSiteRuntimeState();
	TestFalse(TEXT("2x2 ordinary-root foundation-depth rejection leaves the cached resolved site unrealized"), bLayoutRealized);
	TestEqual(TEXT("Rejected 2x2 ordinary-root cached site preserves the structured foundation-depth rejection"), RuntimeState.TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::RejectedFoundationDepthExceeded);
	TestEqual(TEXT("Rejected 2x2 ordinary-root site does not stamp the northwest elevated anchor block"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Rejected 2x2 ordinary-root site does not stamp the southwest elevated anchor block"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Rejected 2x2 ordinary-root site does not write foundation fill before the failure on the northwest lower column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Rejected 2x2 ordinary-root site does not write foundation fill before the failure on the southwest lower column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationRejectsFoundationDepthOverrunForSiteTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeHillReject"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy = BuildPlacementPolicy(16, 16, 1, 3, true, 1);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestFoundationDepthRejectPlacement"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 5), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestFoundationDepthRejectArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestFoundationDepthRejectContract")),
			FIntVector(8, 8, 5),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 3); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	FString RealizeFailureReason;
	const bool bLayoutRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestEqual(TEXT("Foundation-depth rejection keeps one cached resolved site after runtime realization fails"), SiteRecords.Num(), 1);
	if (SiteRecords.Num() != 1)
	{
		return false;
	}

	const FResolvedLayoutSiteRuntimeState RuntimeState = SiteRecords[0].GetResolvedSiteRuntimeState();
	TestFalse(TEXT("Ordinary-root foundation-depth rejection leaves the cached resolved site unrealized"), bLayoutRealized);
	TestEqual(TEXT("Rejected ordinary-root cached site preserves the structured foundation-depth rejection"), RuntimeState.TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::RejectedFoundationDepthExceeded);
	TestEqual(TEXT("Rejected ordinary-root site does not stamp the elevated anchor block"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Rejected ordinary-root site does not write foundation fill before the failure"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsFoundationFillWithTemplatePlacementOffsetFloorAlignmentTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeHillOffset"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy = BuildPlacementPolicy(16, 16, 1, 3, true, 4);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestFoundationFillTemplateOffsetSnap"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -3;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestFoundationFillTemplateOffsetArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestFoundationFillTemplateOffsetContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 8, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	for (int32 FoundationZ = -6; FoundationZ <= -4; ++FoundationZ)
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, FoundationZ);
		W.Material = SinfullMaterial;
		W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Offset terrain-conforming sites still realize successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Offset terrain-conforming site preserves the foundation-fill diagnostic when the policy authorizes fill even though the deep offset sinks the root into the terrain"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedFoundationFill);
	TestEqual(TEXT("Offset floor-plane alignment no longer raises the lower support column above the overlapping root layers"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Offset floor-plane alignment leaves the lower support column free of raised fill above the preserved terrain"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Offset floor-plane alignment also leaves the higher support column free of raised fill above the preserved terrain"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	for (int32 FoundationZ = -6; FoundationZ <= -4; ++FoundationZ)
	{
		TestEqual(
			FString::Printf(TEXT("Offset foundation keeps bounded fill at Z=%d without counting template offset as extra depth"), FoundationZ),
			Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, FoundationZ), ERessourceType::MaterialIndex, 0),
			SinfullMaterial);
	}
	return true;
}

bool FLayoutRuntimeRealizationBuildsOrdinaryRootPerimeterRampTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 7, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 6, 2));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 9, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(9, 8, 5));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimePerimeterRamp"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 3, true);
	const FIntVector LatticeOwnedSiteCenterBlockWorldPos(8, 8, 6);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestOrdinaryRampPlacement"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(LatticeOwnedSiteCenterBlockWorldPos, LayoutProfile, FIntPoint(1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestOrdinaryRampArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestOrdinaryRampContract")),
			LatticeOwnedSiteCenterBlockWorldPos,
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	// Ramp fill: raise the downhill columns to the footprint plane.
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	// The injected site record starts from a pre-fit root anchor one block below the
	// terrain plane, so the observation gate needs both the zero chunk and the
	// pre-fit negative-Z chunk before realization can run.
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Ordinary-root perimeter-ramp site realizes successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Ordinary-root perimeter-ramp site preserves the structured ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(
		TEXT("Ordinary-root perimeter-ramp site keeps the realized footprint origin on the global cell lattice"),
		SiteRecords[0].GetResolvedSiteLocationMetadata().RealizedFootprintMinBlockWorldPos,
		UChunkWorldLayoutRuntimeComponent::ComputeFootprintMinBlockWorldPos(
			SiteRecords[0],
			SiteRecords[0].GetResolvedSiteSolvedPayload().SolveResult.SharedCellSizeInBlocks));
	TestEqual(TEXT("Runtime perimeter ramp raises the immediate edge column to the floor plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Runtime perimeter ramp terraces the next outer column one step down"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsMultiCellOrdinaryRootPerimeterRampTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoCellSupportablePerimeterRampFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoCellPerimeterRamp"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestMultiCellRampPlacement"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestMultiCellRampArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestMultiCellRampContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Two-cell ordinary-root perimeter-ramp site realizes successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Two-cell ordinary-root perimeter-ramp site preserves the structured ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Two-cell ordinary-root fill still supports the lower footprint column at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell ordinary-root fill still raises the lower footprint column to the resolved anchor base at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell ordinary-root perimeter shaping raises the west edge to the floor plane at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell ordinary-root perimeter shaping also raises the north edge beside the upper footprint column at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoSupportablePerimeterRampFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoPerimeterRamp"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("TestTwoByTwoPerimeterRampSnap"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestTwoByTwoPerimeterRampArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestTwoByTwoPerimeterRampContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 9, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 9, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("2x2 ordinary-root perimeter-ramp site realizes successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("2x2 ordinary-root perimeter-ramp site preserves the structured ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("2x2 ordinary-root fill still supports the northwest footprint column at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 ordinary-root fill still supports the southwest footprint column at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 ordinary-root perimeter shaping raises the west edge beside the north row at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 ordinary-root perimeter shaping raises the west edge beside the south row at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 9, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 ordinary-root perimeter shaping also raises the north edge beside the east footprint column at runtime"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
	}
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 8, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 7, 3));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 6, 2));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(8, 9, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(7, 8, 5));
	FLayoutTestWorldSupport::WriteSurfaceBlock(Harness.World, FIntVector(9, 8, 5));

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimePerimeterRampNoFoundationFill"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, false, 3, true);
	const FIntVector LatticeOwnedSiteCenterBlockWorldPos(8, 8, 6);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestOrdinaryRampNoFillPlacement"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(LatticeOwnedSiteCenterBlockWorldPos, LayoutProfile, FIntPoint(1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestOrdinaryRampNoFillArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestOrdinaryRampNoFillContract")),
			LatticeOwnedSiteCenterBlockWorldPos,
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 4); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Ordinary-root perimeter-ramp site realizes successfully when generic foundation fill is disabled but perimeter ramping alone is sufficient"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Ordinary-root perimeter-ramp site still preserves the structured ramp terrain-fit outcome when generic foundation fill is disabled"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Runtime perimeter ramp still raises the immediate edge column to the floor plane without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Runtime perimeter ramp still terraces the next outer column one step down without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 4), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsMultiCellOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoCellPerimeterRampWithoutFoundationFillFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeMultiCellPerimeterRampNoFoundationFill"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, false, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& FirstPlacement = Placements.AddDefaulted_GetRef();
	FirstPlacement.Cell = FIntVector(0, 0, 0);
	FirstPlacement.Intent = ELayoutCellIntent::Boundary;
	FirstPlacement.Module = nullptr;
	FirstPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestMultiCellRampNoFillSnap"));
	FirstPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FLayoutPlacedModule& SecondPlacement = Placements.AddDefaulted_GetRef();
	SecondPlacement.Cell = FIntVector(1, 0, 0);
	SecondPlacement.Intent = ELayoutCellIntent::Boundary;
	SecondPlacement.Module = nullptr;
	SecondPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestMultiCellRampNoFillSnap"));
	SecondPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(2, 1), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestMultiCellRampNoFillArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 2;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestMultiCellRampNoFillContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Two-cell ordinary-root perimeter-ramp site realizes successfully when generic foundation fill is disabled but perimeter ramping alone is sufficient"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Two-cell ordinary-root perimeter-ramp site still preserves the structured ramp terrain-fit outcome when generic foundation fill is disabled"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Two-cell runtime perimeter-ramp site needs no under-footprint support fill when generic foundation fill is disabled"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Two-cell runtime perimeter ramp still raises the west edge to the floor plane without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell runtime perimeter ramp still raises the north edge beside the lower footprint column without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell runtime perimeter ramp still raises the north edge beside the upper footprint column without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootPerimeterRampWithoutFoundationFillAuthorizationTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoPerimeterRampWithoutFoundationFillFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoPerimeterRampNoFoundationFill"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, false, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("TestTwoByTwoRampNoFillSnap"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestTwoByTwoRampNoFillArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;
	SiteRecord.LayoutProfile = nullptr;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestTwoByTwoRampNoFillContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 4); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(6, 9, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));
	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("2x2 ordinary-root perimeter-ramp site realizes successfully when generic foundation fill is disabled but perimeter ramping alone is sufficient"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("2x2 ordinary-root perimeter-ramp site still preserves the structured ramp terrain-fit outcome when generic foundation fill is disabled"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("2x2 runtime perimeter-ramp site needs no under-footprint support fill when generic foundation fill is disabled"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 4), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 runtime perimeter ramp still raises the west edge beside the north row without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 runtime perimeter ramp still raises the west edge beside the south row without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(6, 9, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 runtime perimeter ramp still raises the north edge beside the east footprint column without generic foundation fill"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsOrdinaryRootDownhillPerimeterCutTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildOneCellPerimeterCutFixture(Harness.World, 7, 8);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimePerimeterCut"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 3, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestPerimeterCutPlacement"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestPerimeterCutArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestPerimeterCutContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 6); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 8); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Bounded downhill ordinary-root perimeter cut realizes successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Bounded downhill ordinary-root perimeter cut preserves the structured ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Runtime downhill perimeter cut clears the immediate hill crest down to the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Runtime downhill perimeter cut also clears the immediate column just above the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Runtime downhill perimeter cut preserves the terraced support under the cut column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Runtime downhill perimeter cut terraces the next outer hill column one level higher than the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 8), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Runtime downhill perimeter cut preserves the next outer terraced support level"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 6), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsMultiCellOrdinaryRootDownhillPerimeterCutTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoCellPerimeterCutFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeMultiCellPerimeterCut"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& FirstPlacement = Placements.AddDefaulted_GetRef();
	FirstPlacement.Cell = FIntVector(0, 0, 0);
	FirstPlacement.Intent = ELayoutCellIntent::Boundary;
	FirstPlacement.Module = nullptr;
	FirstPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestMultiCellPerimeterCutPlacement0"));
	FirstPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FLayoutPlacedModule& SecondPlacement = Placements.AddDefaulted_GetRef();
	SecondPlacement.Cell = FIntVector(1, 0, 0);
	SecondPlacement.Intent = ELayoutCellIntent::Boundary;
	SecondPlacement.Module = nullptr;
	SecondPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestMultiCellPerimeterCutPlacement1"));
	SecondPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(2, 1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestMultiCellPerimeterCutArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 2;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestMultiCellPerimeterCutContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	// Immediate north crests (Z=7) plus one level below for each of the two footprint columns.
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 6); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 6); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	// Outer north terraces (Z=8) for each of the two footprint columns.
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 8); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 8); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	// Preserve the lower footprint support column that would otherwise be cleared because
	// the column at (7,8) has a lower surface (Z=3) than the template place offset implies.
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Two-cell bounded downhill ordinary-root perimeter cut realizes successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("Two-cell bounded downhill ordinary-root perimeter cut preserves the structured ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Two-cell runtime downhill cut still fills the lower footprint support column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell runtime downhill cut clears the first north hill crest down to the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Two-cell runtime downhill cut clears the second north hill crest down to the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Two-cell runtime downhill cut preserves the terraced support beneath the first cut column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell runtime downhill cut preserves the terraced support beneath the second cut column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Two-cell runtime downhill cut terraces the first outer hill column one level higher than the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 8), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Two-cell runtime downhill cut terraces the second outer hill column one level higher than the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 8), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationBuildsTwoByTwoOrdinaryRootDownhillPerimeterCutTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoPerimeterCutFixture(Harness.World);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoPerimeterCut"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("Test2x2PerimeterCutPlacement"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("Test2x2PerimeterCutArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("Test2x2PerimeterCutContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 7, 6); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 7); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 6); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 6, 8); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 6, 8); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	// Preserve the lower footprint support columns that would otherwise be cleared.
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 8, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(7, 9, 5); W.Material = 1; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("2x2 bounded downhill ordinary-root perimeter cut realizes successfully without a live profile asset"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	TestEqual(TEXT("2x2 bounded downhill ordinary-root perimeter cut preserves the structured ramp terrain-fit outcome on the cached record"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("2x2 runtime downhill cut still fills the northwest footprint support column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 runtime downhill cut still fills the southwest footprint support column"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 9, 5), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("2x2 runtime downhill cut clears the first north hill crest down to the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 runtime downhill cut clears the second north hill crest down to the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 7), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 runtime downhill cut terraces the first outer hill column one level higher than the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 6, 8), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("2x2 runtime downhill cut terraces the second outer hill column one level higher than the footprint plane"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 6, 8), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	return true;
}

bool FLayoutRuntimeRealizationRejectsOrdinaryRootPerimeterTransitionDepthOverrunTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	for (int32 Z = 0; Z <= 16; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), EmptyMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), EmptyMaterial, false);
	}
	for (int32 Z = 0; Z <= 5; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 8, Z), SinfullMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 9, Z), SinfullMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(7, 8, Z), SinfullMaterial, false);
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(9, 8, Z), SinfullMaterial, false);
	}
	for (int32 Z = 0; Z <= 10; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 7, Z), SinfullMaterial, false);
	}
	for (int32 Z = 0; Z <= 11; ++Z)
	{
		Harness.World->SetBlockValueByBlockWorldPos(FIntVector(8, 6, Z), SinfullMaterial, false);
	}

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimePerimeterTransitionReject"),
		FIntPoint(1, 1),
		FIntPoint(1, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 3, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
	Placement.Cell = FIntVector(0, 0, 0);
	Placement.Intent = ELayoutCellIntent::Boundary;
	Placement.Module = nullptr;
	Placement.ModuleSnapshotId = FLayoutId(TEXT("TestClampedPerimTransitionPlacement"));
	Placement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(1, 1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestClampedPerimTransitionArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 1;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestClampedPerimTransitionContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(1, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	// Capped carve: only the top MaxPerimeterTransitionDepth (3) layers of the crest.
	// Immediate north crest surface at Z=10: clear Z=10,9,8.
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 10); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 9); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}
	{
		FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef();
		W.BlockWorldPos = FIntVector(8, 7, 8); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active;
	}

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Ordinary-root clamped perimeter-transition site realizes successfully"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	if (SiteRecords.Num() != 1 || !SiteRecords[0].bLayoutRealized)
	{
		return false;
	}

	TestEqual(TEXT("Clamped ordinary-root cached site preserves the accepted perimeter-ramp diagnostic"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Clamped perimeter-transition site still preserves the overlap-layer footprint block as empty"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped perimeter-transition site carves only the top three layers of the original hill crest"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 10), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped perimeter-transition site keeps the remaining hill crest support below the capped cut depth"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 7), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationRejectsMultiCellOrdinaryRootPerimeterTransitionDepthOverrunTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoCellPerimeterCutFixture(Harness.World, 10, 11);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeMultiCellPerimeterCutReject"),
		FIntPoint(2, 1),
		FIntPoint(2, 1),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	FLayoutPlacedModule& FirstPlacement = Placements.AddDefaulted_GetRef();
	FirstPlacement.Cell = FIntVector(0, 0, 0);
	FirstPlacement.Intent = ELayoutCellIntent::Boundary;
	FirstPlacement.Module = nullptr;
	FirstPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestClampedMultiCellPlacement0"));
	FirstPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FLayoutPlacedModule& SecondPlacement = Placements.AddDefaulted_GetRef();
	SecondPlacement.Cell = FIntVector(1, 0, 0);
	SecondPlacement.Intent = ELayoutCellIntent::Boundary;
	SecondPlacement.Module = nullptr;
	SecondPlacement.ModuleSnapshotId = FLayoutId(TEXT("TestClampedMultiCellPlacement1"));
	SecondPlacement.OccupiedLocalCells = {FIntVector::ZeroValue};

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 8, 0), LayoutProfile, FIntPoint(2, 1), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestClampedMultiCellPerimArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 2;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestClampedMultiCellPerimContract")),
			FIntVector(8, 8, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 1),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	// Capped carve: top 3 layers of the two crest columns (Z=10,9,8).
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(7, 7, 10); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(7, 7, 9);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(7, 7, 8);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(8, 7, 10); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(8, 7, 9);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(8, 7, 8);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("Two-cell ordinary-root clamped perimeter-transition site realizes successfully"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	if (SiteRecords.Num() != 1 || !SiteRecords[0].bLayoutRealized)
	{
		return false;
	}
	TestEqual(TEXT("Clamped two-cell ordinary-root cached site preserves the accepted perimeter-ramp diagnostic"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Clamped two-cell perimeter-transition site still preserves the lower overlap-layer footprint block as empty"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped two-cell perimeter-transition site still preserves the upper overlap-layer footprint block as empty"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 8, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped two-cell perimeter-transition site carves only the top three layers of the first original hill crest"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 10), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped two-cell perimeter-transition site carves only the top three layers of the second original hill crest"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 10), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped two-cell perimeter-transition site keeps the first remaining hill crest support below the capped cut depth"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 7), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Clamped two-cell perimeter-transition site keeps the second remaining hill crest support below the capped cut depth"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 7), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

bool FLayoutRuntimeRealizationRejectsTwoByTwoOrdinaryRootPerimeterTransitionDepthOverrunTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	BuildTwoByTwoPerimeterCutFixture(Harness.World, 10, 11);

	ULayoutProfileAsset* LayoutProfile = CreateProfile(
		GetTransientPackage(),
		TEXT("LayoutProfile_RuntimeTwoByTwoPerimeterCutReject"),
		FIntPoint(2, 2),
		FIntPoint(2, 2),
		1,
		0,
		false);
	const FLayoutWorldBindingPlacementPolicy PlacementPolicy =
		BuildPlacementPolicy(16, 16, 1, 3, true, 4, true);

	TArray<FLayoutPlacedModule> Placements;
	for (int32 LocalY = 0; LocalY < 2; ++LocalY)
	{
		for (int32 LocalX = 0; LocalX < 2; ++LocalX)
		{
			FLayoutPlacedModule& Placement = Placements.AddDefaulted_GetRef();
			Placement.Cell = FIntVector(LocalX, LocalY, 0);
			Placement.Intent = ELayoutCellIntent::Boundary;
			Placement.Module = nullptr;
			Placement.ModuleSnapshotId = FLayoutId(TEXT("TestClamped2x2PerimPlacement"));
			Placement.OccupiedLocalCells = {FIntVector::ZeroValue};
		}
	}

	FResolvedLayoutSiteRecord SiteRecord = BuildSolvedSiteRecord(FIntVector(8, 9, 0), LayoutProfile, FIntPoint(2, 2), Placements);
	SiteRecord.SolveResult.SharedCellSizeInBlocks = FIntVector(16, 16, 1);
	SiteRecord.SolveResult.RootPlacementKind = ELayoutWorldBindingPlacementKind::OrdinaryRoot;
	SiteRecord.SolveResult.WorldBindingPlacementPolicy = PlacementPolicy;
	SiteRecord.SolveResult.ResolvedTerrainAlignmentLevel = 0;
	SiteRecord.SolveResult.TemplatePlacementZOffsetBlocks = -1;
	SiteRecord.LayoutProfile = nullptr;
	SiteRecord.SolvedArtifactId = FLayoutId(TEXT("TestClamped2x2PerimArtifact"));
	SiteRecord.SolvedArtifactActiveCellCount = 4;

	const FLayoutFrozenTerrainContract FrozenTerrainContract =
		PorismLayoutWorldTestUtilities::BuildPerimeterTestFrozenTerrainContract(
			FLayoutId(TEXT("TestClamped2x2PerimContract")),
			FIntVector(8, 9, 0),
			FIntVector(16, 16, 1),
			FIntPoint(2, 2),
			PlacementPolicy);
	FLayoutFrozenTerrainContract MutableContract = FrozenTerrainContract;
	// Capped carve: top 3 layers of the two crest columns (Z=10,9,8).
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(7, 7, 10); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(7, 7, 9);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(7, 7, 8);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(8, 7, 10); W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(8, 7, 9);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }
	{ FLayoutFrozenTerrainWriteRecord& W = MutableContract.TerrainWrites.AddDefaulted_GetRef(); W.BlockWorldPos = FIntVector(8, 7, 8);  W.Material = 1048575; W.SourceContract = ELayoutFrozenTerrainCellContract::Active; }

	Harness.RuntimeComponent->AddResolvedLayoutSiteRecordForTesting(FIntPoint(0, 0), SiteRecord);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector::ZeroValue);
	Harness.RuntimeComponent->AddObservedLoadedChunkOriginForTesting(FIntVector(0, 0, -16));

	FString RealizeFailureReason;
	const bool bRealized = Harness.RuntimeComponent->TryRealizeSiteRecordByKeyForTesting(
		FLayoutSiteReservation::ComputeReservationKey(SiteRecord.SiteCenterBlockWorldPos), MutableContract, &RealizeFailureReason);

	const TArray<FResolvedLayoutSiteRecord> SiteRecords = Harness.RuntimeComponent->GetResolvedLayoutSiteRecords();
	TestTrue(TEXT("2x2 ordinary-root clamped perimeter-transition site realizes successfully"), SiteRecords.Num() == 1 && SiteRecords[0].bLayoutRealized);
	if (SiteRecords.Num() != 1 || !SiteRecords[0].bLayoutRealized)
	{
		return false;
	}
	TestEqual(TEXT("Clamped 2x2 ordinary-root cached site preserves the accepted perimeter-ramp diagnostic"), SiteRecords[0].TerrainFitDiagnosticKind, ELayoutWorldBindingTerrainFitDiagnosticKind::AcceptedPerimeterRamp);
	TestEqual(TEXT("Clamped 2x2 perimeter-transition site still preserves the northwest overlap-layer footprint block as empty"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 8, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped 2x2 perimeter-transition site still preserves the southeast overlap-layer footprint block as empty"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 9, 6), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped 2x2 perimeter-transition site carves only the top three layers of the first original hill crest"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 10), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped 2x2 perimeter-transition site carves only the top three layers of the second original hill crest"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 10), ERessourceType::MaterialIndex, 0), EmptyMaterial);
	TestEqual(TEXT("Clamped 2x2 perimeter-transition site keeps the first remaining hill crest support below the capped cut depth"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(7, 7, 7), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	TestEqual(TEXT("Clamped 2x2 perimeter-transition site keeps the second remaining hill crest support below the capped cut depth"), Harness.World->GetBlockValueByBlockWorldPos(FIntVector(8, 7, 7), ERessourceType::MaterialIndex, 0), SinfullMaterial);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationBridgeCellsWritePlanAnchorsTest,
	"PorismExtension.Layout.Runtime.Realization.BridgeCellsWritePlanAnchors",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationBridgeCellsWritePlanAnchorsTest::RunTest(const FString& Parameters)
{
	// Build a FrozenTerrainContract with two XY columns.
	FLayoutFrozenTerrainContract FrozenTerrainContract;
	FrozenTerrainContract.ContractId = FLayoutId(TEXT("TerrainBiome.BridgeCellAnchorTest"));
	FrozenTerrainContract.SiteCenterBlockWorldPos = FIntVector(8, 8, 16);
	FrozenTerrainContract.FootprintMinBlockWorldPos = FIntVector(0, 0, 16);
	FrozenTerrainContract.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	FrozenTerrainContract.FootprintSizeInCells = FIntPoint(2, 1);

	// StageMap: one entry per XY column. ResolvedStageBaseBlockWorldZ is the base Z for Z=0 cells.
	{
		FLayoutFrozenTerrainStageCellRecord& Stage0 = FrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		Stage0.FootprintCellXY = FIntPoint(0, 0);
		Stage0.ResolvedStageBaseBlockWorldZ = 16;
		FLayoutFrozenTerrainStageCellRecord& Stage1 = FrozenTerrainContract.StageMap.AddDefaulted_GetRef();
		Stage1.FootprintCellXY = FIntPoint(1, 0);
		Stage1.ResolvedStageBaseBlockWorldZ = 16;
	}

	// ActiveCells: lower column Z=0,1 (Z=1 is TopBridge deck).
	// Higher column Z=0,1 (Z=0 is bridge aligned with unshifted, Z=1 is shifted original).
	// No Z=-1 cells — column-level Z-shift for the higher column eliminates the gap.
	FrozenTerrainContract.ActiveCells = {
		{FIntVector(0, 0, 0)},
		{FIntVector(1, 0, 0)},
		{FIntVector(0, 0, 1)},
		{FIntVector(1, 0, 1)}};

	// CellContracts: matching entries for all active cells.
	FrozenTerrainContract.CellContracts = {
		{FIntVector(0, 0, 0), ELayoutFrozenTerrainCellContract::Active},
		{FIntVector(1, 0, 0), ELayoutFrozenTerrainCellContract::Active},
		{FIntVector(0, 0, 1), ELayoutFrozenTerrainCellContract::Active},
		{FIntVector(1, 0, 1), ELayoutFrozenTerrainCellContract::Active}};

	// Build a SolveResult with four placements matching the active cells.
	FLayoutSolveResult SolveResult;
	SolveResult.bSucceeded = true;
	SolveResult.SharedCellSizeInBlocks = FIntVector(8, 8, 8);
	SolveResult.TemplatePlacementZOffsetBlocks = 0;

	auto AddPlacement = [&SolveResult](const FIntVector& Cell, const FLayoutId& SnapshotId)
	{
		FLayoutPlacedModule& P = SolveResult.Placements.AddDefaulted_GetRef();
		P.Cell = Cell;
		P.Intent = ELayoutCellIntent::Interior;
		P.ModuleSnapshotId = SnapshotId;
		P.ModuleSnapshotIndex = 0;
		P.TemplatePath = FSoftObjectPath(TEXT("/Game/Test/T_BridgeModule.T_BridgeModule"));
		P.YawRotationSteps = 0;
	};
	AddPlacement(FIntVector(0, 0, 0), FLayoutId(TEXT("Module.Base.0")));
	AddPlacement(FIntVector(1, 0, 0), FLayoutId(TEXT("Module.ShiftedBridge")));
	AddPlacement(FIntVector(0, 0, 1), FLayoutId(TEXT("Module.TopBridge")));
	AddPlacement(FIntVector(1, 0, 1), FLayoutId(TEXT("Module.ShiftedTop")));

	const FLayoutId SolvedArtifactId(TEXT("SolvedArtifact.BridgeCellAnchors"));
	const int32 ActiveCellCount = FrozenTerrainContract.ActiveCells.Num();

	FLayoutRealizationWritePlan WritePlan;
	FString FailureReason;
	const bool bBuilt = LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
		ELayoutRealizationWritePlanSource::Site,
		SolvedArtifactId,
		ActiveCellCount,
		SolveResult,
		FrozenTerrainContract,
		WritePlan,
		FailureReason);

	if (!bBuilt)
	{
		AddError(FString::Printf(TEXT("Write plan build failed: %s"), *FailureReason));
		return false;
	}

	TestEqual(TEXT("Write plan has four template placement entries"), WritePlan.TemplatePlacements.Entries.Num(), 4);

	// Verify the TopBridge at Z=1 gets the correct anchor: baseZ + 1 * cellHeight + offset.
	// Shifted column bridge at Z=0 is at unshifted height (baseZ + 0*cellHeight = 16).
	bool bFoundTopBridge = false;
	bool bFoundShiftedBridge = false;
	for (const FLayoutRealizationTemplatePlacementEntry& Entry : WritePlan.TemplatePlacements.Entries)
	{
		if (Entry.Cell == FIntVector(0, 0, 1))
		{
			bFoundTopBridge = true;
			// Expected: 16 + 1*8 + 0 = 24
			TestEqual(TEXT("TopBridge anchor block world Z"), Entry.AcceptedAnchorBlockWorldPos.Z, 24);
		}
		if (Entry.Cell == FIntVector(1, 0, 0))
		{
			bFoundShiftedBridge = true;
			// Expected: 16 + 0*8 + 0 = 16 (aligned with unshifted column)
			TestEqual(TEXT("Shifted column bridge anchor block world Z"), Entry.AcceptedAnchorBlockWorldPos.Z, 16);
		}
	}
	TestTrue(TEXT("TopBridge entry at Z=1 found in write plan"), bFoundTopBridge);
	TestTrue(TEXT("Shifted column bridge at Z=0 found in write plan"), bFoundShiftedBridge);

	// Continuation profiles can reserve Z=1 for their entry deck. That level
	// aligns to terrain base; only Z=2 is one generated deck above ground.
	FLayoutSolveResult ContinuationSolveResult = SolveResult;
	ContinuationSolveResult.ResolvedTerrainAlignmentLevel = 1;
	for (FLayoutPlacedModule& Placement : ContinuationSolveResult.Placements)
	{
		++Placement.Cell.Z;
	}
	FLayoutFrozenTerrainContract ContinuationContract = FrozenTerrainContract;
	for (FLayoutContractActiveCellRecord& ActiveCell : ContinuationContract.ActiveCells)
	{
		++ActiveCell.Cell.Z;
	}
	for (FLayoutTerrainCellContractRecord& CellContract : ContinuationContract.CellContracts)
	{
		++CellContract.Cell.Z;
	}
	FLayoutRealizationWritePlan ContinuationWritePlan;
	TestTrue(
		TEXT("Continuation write plan builds from structural entry-level cells"),
		LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Connector,
			FLayoutId(TEXT("SolvedArtifact.ContinuationBridgeCellAnchors")),
			ContinuationContract.ActiveCells.Num(),
			ContinuationSolveResult,
			ContinuationContract,
			ContinuationWritePlan,
			FailureReason));
	const FLayoutRealizationTemplatePlacementEntry* const ContinuationBaseEntry =
		ContinuationWritePlan.TemplatePlacements.Entries.FindByPredicate([](const FLayoutRealizationTemplatePlacementEntry& Entry)
		{
			return Entry.Cell == FIntVector(0, 0, 1);
		});
	const FLayoutRealizationTemplatePlacementEntry* const ContinuationDeckEntry =
		ContinuationWritePlan.TemplatePlacements.Entries.FindByPredicate([](const FLayoutRealizationTemplatePlacementEntry& Entry)
		{
			return Entry.Cell == FIntVector(0, 0, 2);
		});
	TestNotNull(TEXT("Continuation structural base entry exists"), ContinuationBaseEntry);
	TestNotNull(TEXT("Continuation generated deck entry exists"), ContinuationDeckEntry);
	if (ContinuationBaseEntry != nullptr && ContinuationDeckEntry != nullptr)
	{
		TestEqual(TEXT("Continuation entry level anchors at terrain base"), ContinuationBaseEntry->AcceptedAnchorBlockWorldPos.Z, 16);
		TestEqual(TEXT("Continuation generated deck anchors one cell above terrain base"), ContinuationDeckEntry->AcceptedAnchorBlockWorldPos.Z, 24);
	}

	// Bridge/tunnel contracts have no StageMap, but still own every connector
	// anchor; route origin is never a realization authority.
	FLayoutFrozenTerrainContract NonSteppedConnectorContract = FrozenTerrainContract;
	NonSteppedConnectorContract.StageMap.Reset();
	FLayoutRealizationWritePlan NonSteppedConnectorWritePlan;
	TestTrue(
		TEXT("Non-stepped connector write plan builds from frozen footprint authority"),
		LayoutRealizationWritePlan::TryBuildFromAcceptedSolveInputs(
			ELayoutRealizationWritePlanSource::Connector,
			FLayoutId(TEXT("SolvedArtifact.NonSteppedConnectorAnchors")),
			NonSteppedConnectorContract.ActiveCells.Num(),
			SolveResult,
			NonSteppedConnectorContract,
			NonSteppedConnectorWritePlan,
			FailureReason));
	const FLayoutRealizationTemplatePlacementEntry* const NonSteppedEntry =
		NonSteppedConnectorWritePlan.TemplatePlacements.Entries.FindByPredicate([](const FLayoutRealizationTemplatePlacementEntry& Entry)
		{
			return Entry.Cell == FIntVector(1, 0, 1);
		});
	TestNotNull(TEXT("Non-stepped connector entry exists"), NonSteppedEntry);
	if (NonSteppedEntry != nullptr)
	{
		TestEqual(TEXT("Non-stepped connector anchor uses frozen footprint base"), NonSteppedEntry->AcceptedAnchorBlockWorldPos, FIntVector(8, 0, 24));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsLoadedFromSaveChunkOriginsTest,
	"PorismExtension.Layout.Runtime.Realization.RejectsLoadedFromSaveChunkOrigins",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationRejectsLoadedFromSaveChunkOriginsTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	// Simulate one chunk loaded from save by injecting the LoadedFromSave sentinel.
	static const FName LoadedFromSaveSentinel(TEXT("LoadedFromSave"));
	Harness.RuntimeComponent->AddChunkStampMarkForTesting(FIntVector::ZeroValue, LoadedFromSaveSentinel, NAME_None);

	TSet<FIntVector> RequiredChunkOrigins;
	RequiredChunkOrigins.Add(FIntVector::ZeroValue);
	FString FailureReason;
	const bool bCanStamp = Harness.RuntimeComponent->CanStampRequiredChunkOrigins(
		RequiredChunkOrigins,
		FLayoutId(TEXT("TestSavedChunkOriginsArtifact")),
		FLayoutId(TEXT("TestRootSolve")),
		FailureReason);

	TestFalse(TEXT("Rejects chunk with LoadedFromSave sentinel"), bCanStamp);
	TestTrue(TEXT("Failure reason mentions loaded from save"),
		FailureReason.Contains(TEXT("was loaded from save")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationAllowsCrossSiteSharedChunkTest,
	"PorismExtension.Layout.Runtime.Realization.AllowsCrossSiteSharedChunk",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationAllowsCrossSiteSharedChunkTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	// Simulate site A having already stamped the chunk.
	const FLayoutId SiteAArtifactId(TEXT("SiteA.Artifact"));
	const FLayoutId SiteARootSolveId(TEXT("SiteA.RootSolve"));
	Harness.RuntimeComponent->AddChunkStampMarkForTesting(FIntVector::ZeroValue, SiteAArtifactId, SiteARootSolveId);

	// Site B may write its distinct accepted plan into the same coarse chunk.
	TSet<FIntVector> RequiredChunkOrigins;
	RequiredChunkOrigins.Add(FIntVector::ZeroValue);
	FString FailureReason;
	const bool bCanStamp = Harness.RuntimeComponent->CanStampRequiredChunkOrigins(
		RequiredChunkOrigins,
		FLayoutId(TEXT("SiteB.Artifact")),
		FLayoutId(TEXT("SiteB.RootSolve")),
		FailureReason);

	TestTrue(TEXT("Allows distinct stable layout roots to share one chunk"), bCanStamp);
	TestTrue(TEXT("Shared-chunk admission has no failure reason"), FailureReason.IsEmpty());
	TestEqual(TEXT("First stable root remains recorded"),
		Harness.RuntimeComponent->GetChunkStampedRootCountForTesting(FIntVector::ZeroValue),
		1);
	Harness.RuntimeComponent->MarkRequiredChunkOriginsStamped(
		RequiredChunkOrigins,
		FLayoutId(TEXT("SiteB.Artifact")),
		FLayoutId(TEXT("SiteB.RootSolve")));
	TestEqual(TEXT("Second stable root is recorded without replacing first root identity"),
		Harness.RuntimeComponent->GetChunkStampedRootCountForTesting(FIntVector::ZeroValue),
		2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationAllowsIdempotentSameSiteChunkStampTest,
	"PorismExtension.Layout.Runtime.Realization.AllowsIdempotentSameSiteChunkStamp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationAllowsIdempotentSameSiteChunkStampTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	// Simulate the same artifact already having stamped this chunk (idempotent retry).
	const FLayoutId ArtifactId(TEXT("SameArtifact"));
	const FLayoutId RootSolveId(TEXT("SameRootSolve"));
	Harness.RuntimeComponent->AddChunkStampMarkForTesting(FIntVector::ZeroValue, ArtifactId, RootSolveId);

	// Same artifact tries to stamp again.
	TSet<FIntVector> RequiredChunkOrigins;
	RequiredChunkOrigins.Add(FIntVector::ZeroValue);
	FString FailureReason;
	const bool bCanStamp = Harness.RuntimeComponent->CanStampRequiredChunkOrigins(
		RequiredChunkOrigins,
		ArtifactId,
		RootSolveId,
		FailureReason);

	TestTrue(TEXT("Allows idempotent stamp from same artifact"), bCanStamp);
	TestTrue(TEXT("Failure reason is empty on idempotent pass"), FailureReason.IsEmpty());

	FailureReason.Reset();
	const bool bCanReplaceStableRootArtifact = Harness.RuntimeComponent->CanStampRequiredChunkOrigins(
		RequiredChunkOrigins,
		FLayoutId(TEXT("DifferentArtifact")),
		RootSolveId,
		FailureReason);
	TestFalse(TEXT("Rejects a different artifact for the same stable root"), bCanReplaceStableRootArtifact);
	TestTrue(TEXT("Same-root replacement failure names stable root"),
		FailureReason.Contains(RootSolveId.ToString()));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLayoutRuntimeRealizationRejectsImpossibleLifecycleStateChainTest,
	"PorismExtension.Layout.Runtime.Realization.RejectsImpossibleLifecycleStateChain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FLayoutRuntimeRealizationRejectsImpossibleLifecycleStateChainTest::RunTest(const FString& Parameters)
{
	FLayoutWorldTestHarness Harness = CreateChunkWorldHarness(GetTransientPackage());
	TestNotNull(TEXT("Chunk-world harness creates a runtime component"), Harness.RuntimeComponent);

	// Construct a site record with an impossible state: realized without solved.
	FResolvedLayoutSiteRecord CorruptedRecord;
	CorruptedRecord.SiteCenterBlockWorldPos = FIntVector(0, 0, 0);
	CorruptedRecord.bLayoutSolved = false;
	CorruptedRecord.bLayoutRealized = true;
	CorruptedRecord.bHasBeenCommittedToChunkWorld = false;

	const bool bShouldAttempt = Harness.RuntimeComponent->ShouldAttemptRealization(CorruptedRecord);
	TestFalse(TEXT("Rejects realized-but-unsolved site record"), bShouldAttempt);
	return true;
}
